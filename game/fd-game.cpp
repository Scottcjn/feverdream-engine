// SPDX-License-Identifier: MIT
// fd-game.cpp -- Feverdream Engine: the game host (the MIT side of the firewall).
//
// This binary links SDL2 and libc ONLY -- no POV-Ray headers, no POV archives.
// It speaks the PROTOCOL.md wire format to fd-daemon over a Unix socket: the
// scene goes over once as plain POV SDL text, then every frame is RENDER plus
// generic name=float declares. See ../GAME_ENGINE.md §1/§4.
//
// What it is: the smallest real game loop over the raytracer --
//   - fixed-timestep simulation (120 Hz accumulator; Gaffer "Fix Your Timestep")
//   - entity state + a collision world (ground + AABBs) owned by the HOST;
//     the scene text is GENERATED from that same collision data at startup,
//     so the world you collide with is provably the world you see
//   - third-person character: WASD/arrows move+turn, space jumps
//   - render free-running: internal res -> SDL streaming texture upscale
//
//   fd-game [sock] [winW] [winH] [rdiv]
//   fd-game --selftest N [sock]     headless: scripted input, asserts the
//                                   collision wall held, dumps PPM, prints fps

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <chrono>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

// ============================ protocol client ================================
static const uint8_t FD_MAGIC = 0xFD, FD_VERSION = 0x00;
static const uint8_t T_SCENE_FULL = 0x01, T_RENDER = 0x02, T_SHUTDOWN = 0x7F;
static const uint8_t T_FRAME = 0x81, T_SCENE_ACK = 0x82, T_ERROR = 0xEE;
static const uint8_t FLAG_WANT_FB = 0x01;

struct Declare { const char* name; float value; };

class FdRenderer {
    int fd_ = -1;
public:
    std::vector<uint8_t> fb;          // RGBA8 of the last frame
    uint32_t fb_w = 0, fb_h = 0, frame_us = 0;

    bool connect_to(const char* path) {
        fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd_ < 0) return false;
        struct sockaddr_un a; memset(&a, 0, sizeof a);
        a.sun_family = AF_UNIX;
        snprintf(a.sun_path, sizeof a.sun_path, "%s", path);
        return connect(fd_, (struct sockaddr*)&a, sizeof a) == 0;
    }
    ~FdRenderer() { if (fd_ >= 0) close(fd_); }

    bool scene(const std::string& sdl) {
        if (!send(T_SCENE_FULL, 0, sdl.data(), (uint32_t)sdl.size())) return false;
        uint8_t type, flags; std::vector<uint8_t> p;
        if (!recv_msg(&type, &flags, p)) return false;
        return type == T_SCENE_ACK && p.size() == 4 && p[0] == 0;
    }

    bool render(int w, int h, const std::vector<Declare>& decls) {
        std::vector<uint8_t> b;
        auto u16 = [&](uint16_t v){ b.push_back(v); b.push_back(v >> 8); };
        auto f32 = [&](float v){ uint8_t t[4]; memcpy(t, &v, 4); b.insert(b.end(), t, t + 4); };
        u16((uint16_t)w); u16((uint16_t)h); f32(0.0f);        // clock unused: host owns time
        b.push_back(0);                                       // aa off
        b.push_back((uint8_t)decls.size());
        for (const Declare& d : decls) {
            size_t n = strlen(d.name);
            b.push_back((uint8_t)n);
            b.insert(b.end(), d.name, d.name + n);
            f32(d.value);
        }
        if (!send(T_RENDER, FLAG_WANT_FB, b.data(), (uint32_t)b.size())) return false;
        uint8_t type, flags; std::vector<uint8_t> p;
        if (!recv_msg(&type, &flags, p)) return false;
        if (type == T_ERROR) {
            fprintf(stderr, "fd-game: daemon error: %.*s\n",
                    (int)p.size() - 4, (const char*)p.data() + 4);
            return false;
        }
        if (type != T_FRAME || p.size() < 12) return false;
        memcpy(&fb_w, p.data(), 4); memcpy(&fb_h, p.data() + 4, 4);
        memcpy(&frame_us, p.data() + 8, 4);
        if (flags & FLAG_WANT_FB) fb.assign(p.begin() + 12, p.end());
        return fb.size() == (size_t)fb_w * fb_h * 4;
    }

    void shutdown() { send(T_SHUTDOWN, 0, NULL, 0); }

private:
    bool io_full(bool wr, void* buf, size_t n) {
        uint8_t* p = (uint8_t*)buf;
        while (n) {
            ssize_t r = wr ? write(fd_, p, n) : read(fd_, p, n);
            if (r <= 0) return false;
            p += r; n -= (size_t)r;
        }
        return true;
    }
    bool send(uint8_t type, uint8_t flags, const void* pl, uint32_t len) {
        uint8_t h[8] = { FD_MAGIC, FD_VERSION, type, flags,
                         (uint8_t)len, (uint8_t)(len >> 8), (uint8_t)(len >> 16), (uint8_t)(len >> 24) };
        if (!io_full(true, h, 8)) return false;
        return !len || io_full(true, (void*)pl, len);
    }
    bool recv_msg(uint8_t* type, uint8_t* flags, std::vector<uint8_t>& p) {
        uint8_t h[8];
        if (!io_full(false, h, 8) || h[0] != FD_MAGIC || h[1] != FD_VERSION) return false;
        *type = h[2]; *flags = h[3];
        uint32_t len = (uint32_t)h[4] | h[5] << 8 | h[6] << 16 | (uint32_t)h[7] << 24;
        if (len > 64u * 1024 * 1024) return false;
        p.resize(len);
        return !len || io_full(false, p.data(), len);
    }
};

// ============================ collision world ================================
// Host-owned, never queried from render geometry (GAME_ENGINE.md §3).
struct Aabb { float cx, cz, hx, hz, h; };          // center XZ, half-extents, height
static const Aabb WORLD[] = {
    {  0.0f,  6.0f, 2.2f, 0.6f, 1.6f },            // wall ahead of spawn
    { -5.0f,  0.0f, 0.8f, 0.8f, 0.9f },            // crate left
    {  5.0f, -2.0f, 0.8f, 0.8f, 2.4f },            // pillar right
    {  4.0f,  5.0f, 1.2f, 1.2f, 0.5f },            // low step
};
static const int   NWORLD   = sizeof WORLD / sizeof WORLD[0];
static const float P_RADIUS = 0.45f;               // player's XZ circle

// circle-vs-AABB push-out in XZ (ignores Y while jumping over low boxes is TODO)
static void collide(float* px, float* pz) {
    for (int i = 0; i < NWORLD; ++i) {
        const Aabb& b = WORLD[i];
        float nx = fmaxf(b.cx - b.hx, fminf(*px, b.cx + b.hx));   // nearest point
        float nz = fmaxf(b.cz - b.hz, fminf(*pz, b.cz + b.hz));
        float dx = *px - nx, dz = *pz - nz;
        float d2 = dx * dx + dz * dz;
        if (d2 >= P_RADIUS * P_RADIUS) continue;
        if (d2 > 1e-9f) {                                          // push out along normal
            float d = sqrtf(d2), s = (P_RADIUS - d) / d;
            *px += dx * s; *pz += dz * s;
        } else {                                                   // center inside: pop +X
            *px = b.cx + b.hx + P_RADIUS;
        }
    }
}

// ====================== scene recipe (generated, .kkrieger-style) ============
// The SAME WORLD[] that drives collision is emitted as POV box{}es: one source
// of truth. Character + camera are driven per frame by declares only.
static std::string build_scene() {
    char buf[512];
    std::string s =
        "#version 3.7;\n"
        "// generated by fd-game from its collision world -- do not hand-edit\n"
        "#ifndef (POSX) #declare POSX=0; #end\n"
        "#ifndef (POSZ) #declare POSZ=0; #end\n"
        "#ifndef (JUMP) #declare JUMP=0; #end\n"
        "#ifndef (TURN) #declare TURN=0; #end\n"   // degrees
        "#ifndef (STEP) #declare STEP=0; #end\n"
        "global_settings { assumed_gamma 1.0 }\n"
        "background { rgb <0.07,0.08,0.13> }\n"
        "light_source { <-14,18,-10> rgb 1 }\n"
        "light_source { <10,8,6> rgb <0.25,0.25,0.4> shadowless }\n"
        "plane { y,0 pigment { checker rgb 0.78 rgb 0.28 } finish { ambient 0.35 } }\n"
        "camera { location <POSX-sin(radians(TURN))*7, 4.4+JUMP*0.4,"
        " POSZ-cos(radians(TURN))*7>\n"
        "  look_at <POSX, 1.0+JUMP*0.6, POSZ> angle 52 right x*16/9 up y }\n";
    for (int i = 0; i < NWORLD; ++i) {
        const Aabb& b = WORLD[i];
        snprintf(buf, sizeof buf,
            "box { <%.2f,0,%.2f>, <%.2f,%.2f,%.2f> pigment { rgb <%.2f,%.2f,0.30> }"
            " finish { phong 0.6 } }\n",
            b.cx - b.hx, b.cz - b.hz, b.cx + b.hx, b.h, b.cz + b.hz,
            0.55 + 0.1 * i, 0.35 + 0.05 * i);
        s += buf;
    }
    s +=
        "// character: torso+head, legs swing about the hip by STEP\n"
        "#declare LEG = 28*sin(STEP);\n"
        "union {\n"
        "  sphere { <0,1.15,0>, 0.42 scale <0.8,1.15,0.6>"
        "    pigment { rgb <0.85,0.45,0.2> } finish { phong 0.7 } }\n"
        "  sphere { <0,1.95,0>, 0.26 pigment { rgb <0.95,0.8,0.65> } finish { phong 0.7 } }\n"
        "  box { <-0.10,-0.85,-0.10>,<0.10,0,0.10> rotate x*LEG  translate <-0.16,0.85,0>\n"
        "    pigment { rgb <0.25,0.3,0.6> } finish { phong 0.5 } }\n"
        "  box { <-0.10,-0.85,-0.10>,<0.10,0,0.10> rotate x*-LEG translate < 0.16,0.85,0>\n"
        "    pigment { rgb <0.25,0.3,0.6> } finish { phong 0.5 } }\n"
        "  rotate y*TURN translate <POSX,JUMP,POSZ>\n"
        "}\n";
    return s;
}

// ============================ simulation =====================================
struct Player {
    float x = 0, z = 0, yaw = 0;       // yaw radians; forward = (sin,cos)
    float step = 0;                    // walk-cycle phase
    float jy = 0, jv = 0; bool grounded = true;
};
struct Input { float move = 0, turn = 0; bool jump = false; };

static const float SIM_DT = 1.0f / 120.0f;          // fixed timestep
static const float SPEED = 4.2f, TURN_RATE = 2.6f, STEP_RATE = 11.0f;
static const float GRAV = -28.0f, JUMP_V = 9.5f;

static void simulate(Player& p, const Input& in) {
    p.yaw += in.turn * TURN_RATE * SIM_DT;
    if (in.move != 0) {
        p.step += STEP_RATE * SIM_DT * (in.move > 0 ? 1 : -1);
        p.x += sinf(p.yaw) * in.move * SPEED * SIM_DT;
        p.z += cosf(p.yaw) * in.move * SPEED * SIM_DT;
        collide(&p.x, &p.z);
    }
    if (in.jump && p.grounded) { p.jv = JUMP_V; p.grounded = false; }
    if (!p.grounded) {
        p.jv += GRAV * SIM_DT; p.jy += p.jv * SIM_DT;
        if (p.jy <= 0) { p.jy = 0; p.jv = 0; p.grounded = true; }
    }
}

static std::vector<Declare> declares_for(const Player& p) {
    return { {"POSX", p.x}, {"POSZ", p.z}, {"JUMP", p.jy},
             {"TURN", p.yaw * 57.29578f}, {"STEP", p.step} };
}

static double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// ============================ selftest (headless) ============================
static int selftest(FdRenderer& r, int frames) {
    Player p;
    double t0 = now_s(); uint64_t sim_us = 0;
    for (int i = 0; i < frames; ++i) {
        Input in;
        if (i < frames * 2 / 3) in.move = 1;        // walk into the wall ahead
        else in.jump = (i == frames * 2 / 3);       // then jump once
        // fixed accumulator: 1/60 of wall time per render frame -> 2 sim steps
        for (int k = 0; k < 2; ++k) simulate(p, in);
        if (!r.render(320, 180, declares_for(p))) return 1;
        sim_us += r.frame_us;
    }
    double wall = now_s() - t0;
    // the wall AABB front face is at z = 6.0-0.6 = 5.4; player radius 0.45
    float zmax = 5.4f - P_RADIUS + 0.001f;
    printf("fd-game selftest: %d frames, daemon avg %.2f ms => %.1f fps, "
           "end-to-end %.1f fps\n", frames, sim_us / 1000.0 / frames,
           1e6 * frames / (double)sim_us, frames / wall);
    printf("  player stopped at z=%.3f (wall clamp %.3f): %s\n", p.z, zmax,
           p.z <= zmax ? "COLLISION HELD" : "COLLISION FAILED");
    FILE* f = fopen("fd_game_selftest.ppm", "wb");
    if (f) {
        fprintf(f, "P6\n%u %u\n255\n", r.fb_w, r.fb_h);
        for (size_t i = 0; i < (size_t)r.fb_w * r.fb_h; ++i) fwrite(&r.fb[i * 4], 1, 3, f);
        fclose(f);
        printf("  last frame -> fd_game_selftest.ppm\n");
    }
    return p.z <= zmax ? 0 : 1;
}

// ============================ interactive (SDL2) =============================
#include <SDL2/SDL.h>

static int play(FdRenderer& r, int winW, int winH, int rdiv) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_Window* win = SDL_CreateWindow("Feverdream fd-game (raytraced live)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winW, winH, SDL_WINDOW_SHOWN);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    int rW = winW / rdiv, rH = winH / rdiv;
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STREAMING, rW, rH);
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);   // the VGA crunch

    Player p;
    bool running = true;
    double prev = now_s(), acc = 0, fpsT = prev; int fpsN = 0;
    printf("fd-game: WASD/arrows move+turn, SPACE jump, ESC quit\n");
    while (running) {
        double now = now_s(), ft = now - prev; prev = now;
        if (ft > 0.25) ft = 0.25;                          // hitch clamp
        acc += ft;

        SDL_Event e; bool jump = false;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) running = false;
                if (e.key.keysym.sym == SDLK_SPACE)  jump = true;
            }
        }
        const Uint8* k = SDL_GetKeyboardState(NULL);
        Input in; in.jump = jump;
        if (k[SDL_SCANCODE_UP]    || k[SDL_SCANCODE_W]) in.move += 1;
        if (k[SDL_SCANCODE_DOWN]  || k[SDL_SCANCODE_S]) in.move -= 1;
        if (k[SDL_SCANCODE_LEFT]  || k[SDL_SCANCODE_A]) in.turn -= 1;
        if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) in.turn += 1;

        while (acc >= SIM_DT) { simulate(p, in); acc -= SIM_DT; in.jump = false; }

        if (!r.render(rW, rH, declares_for(p))) { fprintf(stderr, "render failed\n"); break; }
        SDL_UpdateTexture(tex, NULL, r.fb.data(), rW * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);

        if (++fpsN >= 10) {
            char t[128];
            snprintf(t, sizeof t, "fd-game  %dx%d->%dx%d  %.0f fps (trace %.1f ms)",
                     rW, rH, winW, winH, fpsN / (now - fpsT), r.frame_us / 1000.0);
            SDL_SetWindowTitle(win, t);
            fpsT = now; fpsN = 0;
        }
    }
    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}

int main(int argc, char** argv) {
    bool st = argc > 1 && strcmp(argv[1], "--selftest") == 0;
    int frames = st && argc > 2 ? atoi(argv[2]) : 90;
    const char* sock = st ? (argc > 3 ? argv[3] : "/tmp/feverdream.sock")
                          : (argc > 1 ? argv[1] : "/tmp/feverdream.sock");
    int winW = (!st && argc > 2) ? atoi(argv[2]) : 1280;
    int winH = (!st && argc > 3) ? atoi(argv[3]) : 720;
    int rdiv = (!st && argc > 4) ? atoi(argv[4]) : 4;

    FdRenderer r;
    if (!r.connect_to(sock)) {
        fprintf(stderr, "fd-game: cannot connect to %s -- is fd-daemon running?\n", sock);
        return 1;
    }
    if (!r.scene(build_scene())) { fprintf(stderr, "fd-game: scene rejected\n"); return 1; }
    return st ? selftest(r, frames) : play(r, winW, winH, rdiv < 1 ? 1 : rdiv);
}
