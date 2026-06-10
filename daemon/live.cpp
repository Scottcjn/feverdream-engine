// SPDX-License-Identifier: MIT
// live.cpp -- Feverdream Engine: SDL2 live window over the resident POV-Ray engine.
//
// Same idea as resident.cpp, but instead of timing N headless frames it opens a
// window and renders continuously: the raytracer runs in a live loop, each frame
// captured to memory and uploaded to an SDL texture. You can orbit the camera
// with the arrow keys while POV-Ray re-renders every frame in real time.
//
//   live [scene.pov] [winW] [winH] [rdiv] [libdir]
//     winW/winH = output window;  rdiv = upscale factor (raytrace at win/rdiv).
//   arrows orbit/zoom | [ ] internal res (fps<->sharpness) | F filter | space | ESC
//
// rdiv is DLSS-style spatial upscaling: trace tiny, upscale big => huge fps at
// high output res. nearest filter = the VGA/retro crunch; linear/best = smooth.
// This is the "see it" build. Run it on a machine with a display.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <cmath>
#include <SDL2/SDL.h>

#include "vfe.h"
#include "vfeplatform.h"

using namespace vfe;
using namespace vfePlatform;
using namespace pov_frontend;

// ---- framebuffer the raytracer draws into (RGBA8) --------------------------
static std::vector<unsigned char> g_fb;
static int g_w = 0, g_h = 0;

static inline void fb_put(unsigned x, unsigned y, const Display::RGBA8& c)
{
    if ((int)x >= g_w || (int)y >= g_h) return;
    unsigned char* p = &g_fb[(y * (size_t)g_w + x) * 4];
    p[0] = c.red; p[1] = c.green; p[2] = c.blue; p[3] = 255;  // opaque
}

class CaptureDisplay : public vfeDisplay
{
public:
    CaptureDisplay(unsigned int w, unsigned int h, GammaCurvePtr g, vfeSession* s, bool v)
        : vfeDisplay(w, h, g, s, v) {}
    virtual void Initialise() override
    {
        g_w = GetWidth(); g_h = GetHeight();
        if ((int)g_fb.size() != g_w * g_h * 4) g_fb.assign((size_t)g_w * g_h * 4, 0);
    }
    virtual void DrawPixel(unsigned int x, unsigned int y, const RGBA8& c) override { fb_put(x, y, c); }
    virtual void DrawPixelBlock(unsigned int x1, unsigned int y1,
                                unsigned int x2, unsigned int y2, const RGBA8* col) override
    {
        unsigned i = 0;
        for (unsigned y = y1; y <= y2; ++y)
            for (unsigned x = x1; x <= x2; ++x)
                fb_put(x, y, col[i++]);
    }
};

static vfeDisplay* CreateCaptureDisplay(unsigned int w, unsigned int h,
                                        GammaCurvePtr g, vfeSession* s, bool v)
{
    return new CaptureDisplay(w, h, g, s, v);
}

// render one frame against the live session, given camera + clock
static bool render_frame(vfeUnixSession* s, const std::string& scene,
                         const std::string& libdir, int w, int h,
                         double clock, double cama, double camr)
{
    vfeRenderOptions opts;
    // 8 threads is the sweet spot here (16 = per-frame pool churn; see FINDINGS.md)
    const char* tc = getenv("FD_THREADS");
    opts.SetThreadCount(tc ? atoi(tc) : 8);
    if (!libdir.empty()) opts.AddLibraryPath(libdir);
    opts.SetSourceFile(scene);
    char b[80];
    snprintf(b, sizeof b, "Width=%d", w);            opts.AddCommand(b);
    snprintf(b, sizeof b, "Height=%d", h);           opts.AddCommand(b);
    snprintf(b, sizeof b, "Clock=%.5f", clock);      opts.AddCommand(b);
    snprintf(b, sizeof b, "Declare=CAMA=%.3f", cama);opts.AddCommand(b);
    snprintf(b, sizeof b, "Declare=CAMR=%.3f", camr);opts.AddCommand(b);
    opts.AddCommand("Antialias=off");
    opts.AddCommand("Output_to_File=off");
    opts.AddCommand("Display=on");
    opts.AddCommand("Verbose=off");
    opts.AddCommand("Pause_When_Done=off");
    if (s->SetOptions(opts) != vfeNoError) return false;
    if (s->StartRender() != vfeNoError)    return false;
    while ((s->GetStatus(true, 1) & stRenderShutdown) == 0) ;
    return true;
}

int main(int argc, char** argv)
{
    // OUTPUT (window) res is fixed; INTERNAL (raytraced) res = output / div.
    // Raytrace tiny, upscale big -- DLSS-style spatial upscaling. The whole point:
    // huge fps from a small render, presented at high res.
    std::string scene  = (argc > 1) ? argv[1] : "spin.pov";
    int winW   = (argc > 2) ? atoi(argv[2]) : 1280;   // output window width
    int winH   = (argc > 3) ? atoi(argv[3]) : 720;    // output window height
    int rdiv   = (argc > 4) ? atoi(argv[4]) : 4;      // internal = output / rdiv
    std::string libdir = (argc > 5) ? argv[5] : "/usr/share/povray-3.7/include";
    if (rdiv < 1) rdiv = 1;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_Window* win = SDL_CreateWindow("Feverdream Engine - live POV-Ray",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winW, winH, SDL_WINDOW_SHOWN);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    vfeUnixSession* session = new vfeUnixSession();
    if (session->Initialize(NULL, NULL) != vfeNoError) {
        fprintf(stderr, "session init: %s\n", session->GetErrorString()); return 1;
    }
    session->SetDisplayCreator(CreateCaptureDisplay);

    // internal render res + texture, (re)built whenever the upscale divisor changes
    int rW = 0, rH = 0;
    SDL_Texture* tex = NULL;
    const char* filtName[3] = { "nearest", "linear", "best" };
    int filt = 0;  // 0=nearest (VGA crunch), 1=linear, 2=best
    auto rebuild_tex = [&]() {
        rW = winW / rdiv; rH = winH / rdiv;
        if (rW < 32) rW = 32; if (rH < 18) rH = 18;
        if (tex) SDL_DestroyTexture(tex);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, rW, rH);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
        SDL_SetTextureScaleMode(tex, (SDL_ScaleMode)filt);
    };
    rebuild_tex();

    double cama = 25.0, camr = 9.0, clock = 0.0;
    bool spinning = true, running = true;
    Uint32 t_start = SDL_GetTicks(), fps_t = t_start;
    int fps_n = 0;
    long maxframes = getenv("FD_MAXFRAMES") ? atol(getenv("FD_MAXFRAMES")) : 0; // headless self-test
    long frames_done = 0;

    printf("Feverdream live: arrows orbit/zoom | [ ] internal res | F filter | space pause | ESC quit\n");
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                    case SDLK_ESCAPE:     running = false; break;
                    case SDLK_LEFT:       cama -= 8;  break;
                    case SDLK_RIGHT:      cama += 8;  break;
                    case SDLK_UP:         camr = fmax(3.0, camr - 0.6); break;
                    case SDLK_DOWN:       camr = fmin(20.0, camr + 0.6); break;
                    case SDLK_SPACE:      spinning = !spinning; break;
                    case SDLK_LEFTBRACKET:  if (rdiv > 1)  { rdiv--; rebuild_tex(); } break; // sharper/slower
                    case SDLK_RIGHTBRACKET: if (rdiv < 16) { rdiv++; rebuild_tex(); } break; // blockier/faster
                    case SDLK_f:          filt = (filt + 1) % 3; SDL_SetTextureScaleMode(tex, (SDL_ScaleMode)filt); break;
                }
            }
        }
        if (spinning) clock = fmod((SDL_GetTicks() - t_start) / 4000.0, 1.0);  // 4s loop

        if (!render_frame(session, scene, libdir, rW, rH, clock, cama, camr)) {
            fprintf(stderr, "render failed: %s\n", session->GetErrorString());
            break;
        }
        if (g_w == rW && g_h == rH && !g_fb.empty()) {
            SDL_UpdateTexture(tex, NULL, g_fb.data(), rW * 4);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, NULL, NULL);   // upscales internal -> window
            SDL_RenderPresent(ren);
        }
        if (maxframes && ++frames_done >= maxframes) {  // headless self-test: dump + exit
            FILE* f = fopen("live_selftest.ppm", "wb");
            if (f) { fprintf(f, "P6\n%d %d\n255\n", g_w, g_h);
                     for (size_t i = 0; i < (size_t)g_w * g_h; ++i) fwrite(&g_fb[i*4], 1, 3, f);
                     fclose(f); }
            printf("self-test: %ld frames at internal %dx%d -> window %dx%d\n", frames_done, rW, rH, winW, winH);
            running = false;
        }
        if (++fps_n >= 10) {  // update title every ~10 frames
            Uint32 now = SDL_GetTicks();
            double fps = fps_n * 1000.0 / (now - fps_t);
            char title[140];
            snprintf(title, sizeof title,
                "Feverdream - raytrace %dx%d -> %dx%d (1/%d, %s)  |  %.0f fps",
                rW, rH, winW, winH, rdiv, filtName[filt], fps);
            SDL_SetWindowTitle(win, title);
            fps_t = now; fps_n = 0;
        }
    }

    session->Shutdown(); delete session;
    if (tex) SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
