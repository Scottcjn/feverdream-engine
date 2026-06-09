// resident.cpp -- Feverdream Engine: resident POV-Ray render loop.
//
// The thesis (see ../bench.sh): a stock `povray` call spends ~700ms on process
// lifecycle + disk to do ~11ms of actual raytracing. This program creates the
// POV-Ray engine ONCE (vfeSession), then renders many frames against the live
// engine -- no respawn, no PNG, frames captured straight into memory. If the
// thesis holds, per-frame time collapses from ~700ms toward the ~11ms floor.
//
// Milestone 1 (this file): headless -- render N frames of a scene at rising
// `clock`, report real per-frame ms / fps, dump a couple frames to PPM as proof
// the output is real and changes frame to frame. SDL2 live window is next.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <chrono>
#include <string>

#include "vfe.h"
#include "vfeplatform.h"   // vfeUnixSession

using namespace vfe;
using namespace vfePlatform;
using namespace pov_frontend;

// ---- our own framebuffer, owned by us (not tied to display lifetime) -------
static std::vector<unsigned char> g_fb;   // RGBA8, row-major
static int g_w = 0, g_h = 0;
static long g_drawcalls = 0;

static inline void fb_put(unsigned x, unsigned y, const Display::RGBA8& c)
{
    if ((int)x >= g_w || (int)y >= g_h) return;
    unsigned char* p = &g_fb[(y * (size_t)g_w + x) * 4];
    p[0] = c.red; p[1] = c.green; p[2] = c.blue; p[3] = c.alpha;
}

// ---- capture display: POV pushes pixels here during a render ---------------
class CaptureDisplay : public vfeDisplay
{
public:
    CaptureDisplay(unsigned int w, unsigned int h, GammaCurvePtr gamma,
                   vfeSession* s, bool visible)
        : vfeDisplay(w, h, gamma, s, visible) {}

    virtual void Initialise() override
    {
        g_w = GetWidth(); g_h = GetHeight();
        g_fb.assign((size_t)g_w * g_h * 4, 0);
        if (getenv("FD_DBG")) fprintf(stderr, "  [disp] Initialise %dx%d\n", g_w, g_h);
    }
    virtual void DrawPixel(unsigned int x, unsigned int y, const RGBA8& c) override
    {
        g_drawcalls++;
        fb_put(x, y, c);
    }
    virtual void DrawPixelBlock(unsigned int x1, unsigned int y1,
                                unsigned int x2, unsigned int y2,
                                const RGBA8* colour) override
    {
        g_drawcalls++;
        unsigned i = 0;
        for (unsigned y = y1; y <= y2; ++y)
            for (unsigned x = x1; x <= x2; ++x)
                fb_put(x, y, colour[i++]);
    }
};

static vfeDisplay* CreateCaptureDisplay(unsigned int w, unsigned int h,
                                        GammaCurvePtr gamma, vfeSession* s, bool visible)
{
    if (getenv("FD_DBG")) fprintf(stderr, "  [disp] CreateCaptureDisplay %ux%u vis=%d\n", w, h, visible);
    return new CaptureDisplay(w, h, gamma, s, visible);
}

static double now_ms()
{
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

static void dump_ppm(const char* path)
{
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", g_w, g_h);
    for (size_t i = 0; i < (size_t)g_w * g_h; ++i)
        fwrite(&g_fb[i * 4], 1, 3, f);     // RGB (drop alpha)
    fclose(f);
}

// render one frame against the ALREADY-RUNNING session; return wall ms (-1 err)
static double render_frame(vfeUnixSession* session, const std::string& scene,
                           const std::string& libdir, int w, int h, double clock)
{
    vfeRenderOptions opts;
    { const char* tc=getenv("FD_THREADS"); opts.SetThreadCount(tc?atoi(tc):sysconf(_SC_NPROCESSORS_ONLN)); }
    if (!libdir.empty()) opts.AddLibraryPath(libdir);
    opts.SetSourceFile(scene);
    char b[64];
    snprintf(b, sizeof b, "Width=%d", w);  opts.AddCommand(b);
    snprintf(b, sizeof b, "Height=%d", h); opts.AddCommand(b);
    snprintf(b, sizeof b, "Clock=%.6f", clock); opts.AddCommand(b);
    opts.AddCommand("Antialias=off");
    opts.AddCommand("Output_to_File=off");   // no disk -- the whole point
    opts.AddCommand("Display=on");           // route pixels to our display
    { const char* bs = getenv("FD_BLOCK");   // bigger blocks => fewer pixel messages
      snprintf(b, sizeof b, "Render_Block_Size=%s", bs ? bs : "32"); opts.AddCommand(b); }
    opts.AddCommand("Preview_Start_Size=1"); // stream pixels to the display...
    opts.AddCommand("Preview_End_Size=1");   // ...at full res in one pass
    opts.AddCommand("Verbose=off");
    opts.AddCommand("Pause_When_Done=off");

    double t0 = now_ms();
    if (session->SetOptions(opts) != vfeNoError) {
        fprintf(stderr, "SetOptions failed: %s\n", session->GetErrorString());
        return -1;
    }
    double t1 = now_ms();
    if (session->StartRender() != vfeNoError) {
        fprintf(stderr, "StartRender failed: %s\n", session->GetErrorString());
        return -1;
    }
    double t2 = now_ms();
    vfeStatusFlags flags;
    // tight poll: render is ~ms, don't sleep 200ms/poll waiting for the done event
    while (((flags = session->GetStatus(true, 1)) & stRenderShutdown) == 0)
        ; // spin until this frame is done
    double t3 = now_ms();
    if (getenv("FD_PHASES"))
        fprintf(stderr, "  [phases] SetOptions=%.1f StartRender=%.1f wait=%.1f\n",
                t1 - t0, t2 - t1, t3 - t2);
    return t3 - t0;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s scene.pov [frames] [W] [H] [libdir]\n", argv[0]);
        return 2;
    }
    std::string scene = argv[1];
    int frames = (argc > 2) ? atoi(argv[2]) : 60;
    int W = (argc > 3) ? atoi(argv[3]) : 240;
    int H = (argc > 4) ? atoi(argv[4]) : 135;
    std::string libdir = (argc > 5) ? argv[5] : "";

    vfeUnixSession* session = new vfeUnixSession();
    if (session->Initialize(NULL, NULL) != vfeNoError) {
        fprintf(stderr, "session init failed: %s\n", session->GetErrorString());
        return 1;
    }
    session->SetDisplayCreator(CreateCaptureDisplay);

    printf("Feverdream resident renderer | scene=%s %dx%d frames=%d\n",
           scene.c_str(), W, H, frames);
    printf("(engine initialized ONCE; each frame reuses the live session)\n\n");

    // warm-up frame (first parse builds caches; report it separately)
    double warm = render_frame(session, scene, libdir, W, H, 0.0);
    printf("warm-up frame: %.1f ms (drawcalls=%ld, fb=%dx%d)\n", warm, g_drawcalls, g_w, g_h);
    dump_ppm("frame_first.ppm");

    double total = 0, best = 1e9, worst = 0;
    for (int i = 0; i < frames; ++i) {
        double clk = (frames > 1) ? (double)i / (frames - 1) : 0.0;
        double ms = render_frame(session, scene, libdir, W, H, clk);
        if (ms < 0) { session->Shutdown(); return 1; }
        total += ms; if (ms < best) best = ms; if (ms > worst) worst = ms;
        if (i == frames / 2) dump_ppm("frame_mid.ppm");
    }
    dump_ppm("frame_last.ppm");

    double avg = total / frames;
    printf("\n--- resident render, %d frames @ %dx%d ---\n", frames, W, H);
    printf("avg   %.2f ms/frame  => %.1f fps\n", avg, 1000.0 / avg);
    printf("best  %.2f ms        => %.1f fps\n", best, 1000.0 / best);
    printf("worst %.2f ms        => %.1f fps\n", worst, 1000.0 / worst);
    printf("\nstock povray per-frame was ~700 ms (~1.4 fps). dumped frame_{first,mid,last}.ppm\n");

    session->Shutdown();
    delete session;
    return 0;
}
