// SPDX-License-Identifier: MIT
// fd-game.cpp -- Feverdream Engine: the game host (the MIT side of the firewall).
//
// This binary links SDL2, Lua 5.4 and libc ONLY -- no POV-Ray headers, no POV
// archives. It speaks the PROTOCOL.md wire format to fd-daemon over a Unix
// socket: the scene goes over once as plain POV SDL text, then every frame is
// RENDER plus generic name=float declares. See ../GAME_ENGINE.md §1/§4.
//
// What it is: the smallest real game loop over the raytracer --
//   - fixed-timestep simulation (120 Hz accumulator; Gaffer "Fix Your Timestep")
//   - Lua 5.4 scripting (arena.lua): the script DEFINES the world (boxes) and
//     tunes movement; an on_tick(t, dt, player) hook animates dynamic boxes.
//     Dynamic boxes are simultaneously collision volumes AND raytraced
//     geometry -- their positions go to the renderer as declares, so the world
//     you collide with is provably the world you see (single source of truth,
//     now in the script)
//   - third-person character: WASD/arrows move+turn, space jumps
//   - render free-running: internal res -> SDL streaming texture upscale
//
//   fd-game [sock] [winW] [winH] [rdiv] [script.lua]
//   fd-game --selftest N [sock] [script.lua]   headless: scripted input,
//                                              asserts collision, dumps PPM

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

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include "fd_audio.h"
static FdAudio g_audio;

// ============================ protocol client ================================
static const uint8_t FD_MAGIC = 0xFD, FD_VERSION = 0x00;
static const uint8_t T_SCENE_FULL = 0x01, T_RENDER = 0x02, T_SHUTDOWN = 0x7F;
static const uint8_t T_FRAME = 0x81, T_SCENE_ACK = 0x82, T_ERROR = 0xEE;
static const uint8_t FLAG_WANT_FB = 0x01;

struct Declare { std::string name; float value; };

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
            b.push_back((uint8_t)d.name.size());
            b.insert(b.end(), d.name.begin(), d.name.end());
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

// ============================ world + tuning =================================
struct Aabb { float cx, cz, hx, hz, h; bool dyn; float r, g; };
static std::vector<Aabb> g_world;
static float P_RADIUS = 0.45f;

// movement tuning -- overridable from the script's `config` table
static float SIM_HZ = 120.0f;
static float SPEED = 4.2f, TURN_RATE = 2.6f, STEP_RATE = 11.0f;
static float GRAV = -28.0f, JUMP_V = 9.5f;

// built-in world, used when no script is present (mirrors the original arena)
static void default_world() {
    g_world = {
        {  0.0f,  6.0f, 2.2f, 0.6f, 1.6f, false, 0.55f, 0.35f },
        { -5.0f,  0.0f, 0.8f, 0.8f, 0.9f, false, 0.65f, 0.40f },
        {  5.0f, -2.0f, 0.8f, 0.8f, 2.4f, false, 0.75f, 0.45f },
        {  4.0f,  5.0f, 1.2f, 1.2f, 0.5f, false, 0.85f, 0.50f },
    };
}

// circle-vs-AABB push-out in XZ; returns true if any box pushed the player
static bool collide(float* px, float* pz) {
    bool pushed = false;
    for (const Aabb& b : g_world) {
        float nx = fmaxf(b.cx - b.hx, fminf(*px, b.cx + b.hx));
        float nz = fmaxf(b.cz - b.hz, fminf(*pz, b.cz + b.hz));
        float dx = *px - nx, dz = *pz - nz;
        float d2 = dx * dx + dz * dz;
        if (d2 >= P_RADIUS * P_RADIUS) continue;
        pushed = true;
        if (d2 > 1e-9f) {
            float d = sqrtf(d2), s = (P_RADIUS - d) / d;
            *px += dx * s; *pz += dz * s;
        } else {
            *px = b.cx + b.hx + P_RADIUS;
        }
    }
    return pushed;
}

// ============================ Lua scripting ==================================
// The script owns the world: a global `boxes` array defines the arena (each
// entry {cx,cz,hx,hz,h [,dyn] [,r] [,g]}), an optional `config` table tunes
// movement, and an optional on_tick(t, dt, player) hook runs once per render
// frame -- it may mutate dynamic boxes' cx/cz in the global `boxes` table,
// which the host reads back into BOTH the collision world and the renderer
// declares. Player table is read-only this round.
static lua_State* g_L = NULL;

// play_sound("jump"|"land"|"step"|"bump"|"blip" [, gain]) — script audio hook
static int l_play_sound(lua_State* L) {
    static const char* names[FdAudio::SOUND_COUNT] =
        { "jump", "land", "step", "bump", "blip" };
    const char* want = luaL_checkstring(L, 1);
    float gain = (float)luaL_optnumber(L, 2, 1.0);
    for (int i = 0; i < FdAudio::SOUND_COUNT; ++i)
        if (strcmp(want, names[i]) == 0) {
            g_audio.play((FdAudio::Sound)i, gain);
            return 0;
        }
    return luaL_error(L, "play_sound: unknown sound '%s'", want);
}

static float lua_field_num(lua_State* L, const char* k, float dflt) {
    lua_getfield(L, -1, k);
    float v = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : dflt;
    lua_pop(L, 1);
    return v;
}

static bool load_script(const char* path) {
    FILE* probe = fopen(path, "rb");
    if (!probe) return false;
    fclose(probe);

    g_L = luaL_newstate();
    luaL_openlibs(g_L);
    lua_pushcfunction(g_L, l_play_sound);
    lua_setglobal(g_L, "play_sound");
    if (luaL_dofile(g_L, path) != LUA_OK) {
        fprintf(stderr, "fd-game: lua error in %s: %s\n", path, lua_tostring(g_L, -1));
        lua_close(g_L); g_L = NULL;
        return false;
    }

    lua_getglobal(g_L, "config");
    if (lua_istable(g_L, -1)) {
        SPEED     = lua_field_num(g_L, "speed", SPEED);
        TURN_RATE = lua_field_num(g_L, "turn_rate", TURN_RATE);
        STEP_RATE = lua_field_num(g_L, "step_rate", STEP_RATE);
        GRAV      = lua_field_num(g_L, "gravity", GRAV);
        JUMP_V    = lua_field_num(g_L, "jump_v", JUMP_V);
        P_RADIUS  = lua_field_num(g_L, "player_radius", P_RADIUS);
    }
    lua_pop(g_L, 1);

    lua_getglobal(g_L, "boxes");
    if (!lua_istable(g_L, -1)) {
        fprintf(stderr, "fd-game: %s defines no `boxes` table\n", path);
        lua_pop(g_L, 1);
        return false;
    }
    g_world.clear();
    int n = (int)luaL_len(g_L, -1);
    for (int i = 1; i <= n && i <= 32; ++i) {
        lua_rawgeti(g_L, -1, i);
        if (lua_istable(g_L, -1)) {
            Aabb b;
            b.cx = lua_field_num(g_L, "cx", 0); b.cz = lua_field_num(g_L, "cz", 0);
            b.hx = lua_field_num(g_L, "hx", 1); b.hz = lua_field_num(g_L, "hz", 1);
            b.h  = lua_field_num(g_L, "h", 1);
            b.r  = lua_field_num(g_L, "r", 0.6f); b.g = lua_field_num(g_L, "g", 0.4f);
            lua_getfield(g_L, -1, "dyn");
            b.dyn = lua_toboolean(g_L, -1) != 0;
            lua_pop(g_L, 1);
            g_world.push_back(b);
        }
        lua_pop(g_L, 1);
    }
    lua_pop(g_L, 1);
    return true;
}

struct Player {
    float x = 0, z = 0, yaw = 0;
    float step = 0;
    float jy = 0, jv = 0; bool grounded = true;
};

// call on_tick(t, dt, player), then read mutated dynamic-box positions back
static void script_tick(double t, double dt, const Player& p) {
    if (!g_L) return;
    lua_getglobal(g_L, "on_tick");
    if (!lua_isfunction(g_L, -1)) { lua_pop(g_L, 1); return; }
    lua_pushnumber(g_L, t);
    lua_pushnumber(g_L, dt);
    lua_newtable(g_L);
    lua_pushnumber(g_L, p.x);        lua_setfield(g_L, -2, "x");
    lua_pushnumber(g_L, p.z);        lua_setfield(g_L, -2, "z");
    lua_pushnumber(g_L, p.yaw);      lua_setfield(g_L, -2, "yaw");
    lua_pushnumber(g_L, p.jy);       lua_setfield(g_L, -2, "jump");
    lua_pushboolean(g_L, p.grounded);lua_setfield(g_L, -2, "grounded");
    if (lua_pcall(g_L, 3, 0, 0) != LUA_OK) {
        // a script error must never kill the frame loop -- report and carry on
        fprintf(stderr, "fd-game: on_tick error: %s\n", lua_tostring(g_L, -1));
        lua_pop(g_L, 1);
        return;
    }
    lua_getglobal(g_L, "boxes");
    if (lua_istable(g_L, -1)) {
        for (size_t i = 0; i < g_world.size(); ++i) {
            if (!g_world[i].dyn) continue;
            lua_rawgeti(g_L, -1, (int)i + 1);
            if (lua_istable(g_L, -1)) {
                g_world[i].cx = lua_field_num(g_L, "cx", g_world[i].cx);
                g_world[i].cz = lua_field_num(g_L, "cz", g_world[i].cz);
            }
            lua_pop(g_L, 1);
        }
    }
    lua_pop(g_L, 1);
}

// ====================== scene recipe (generated) =============================
// Static boxes are baked into the SDL text; dynamic boxes are emitted centered
// at the origin and placed per frame via BOX<i>X/BOX<i>Z declares -- same
// values that drive their collision AABBs.
static std::string build_scene() {
    char buf[512];
    std::string s =
        "#version 3.7;\n"
        "// generated by fd-game from the script's world table -- do not hand-edit\n"
        "#ifndef (POSX) #declare POSX=0; #end\n"
        "#ifndef (POSZ) #declare POSZ=0; #end\n"
        "#ifndef (JUMP) #declare JUMP=0; #end\n"
        "#ifndef (TURN) #declare TURN=0; #end\n"
        "#ifndef (STEP) #declare STEP=0; #end\n"
        "global_settings { assumed_gamma 1.0 }\n"
        "background { rgb <0.07,0.08,0.13> }\n"
        "light_source { <-14,18,-10> rgb 1 }\n"
        "light_source { <10,8,6> rgb <0.25,0.25,0.4> shadowless }\n"
        "plane { y,0 pigment { checker rgb 0.78 rgb 0.28 } finish { ambient 0.35 } }\n"
        "camera { location <POSX-sin(radians(TURN))*7, 4.4+JUMP*0.4,"
        " POSZ-cos(radians(TURN))*7>\n"
        "  look_at <POSX, 1.0+JUMP*0.6, POSZ> angle 52 right x*16/9 up y }\n";
    for (size_t i = 0; i < g_world.size(); ++i) {
        const Aabb& b = g_world[i];
        if (b.dyn) {
            snprintf(buf, sizeof buf,
                "#ifndef (BOX%zuX) #declare BOX%zuX=%.2f; #end\n"
                "#ifndef (BOX%zuZ) #declare BOX%zuZ=%.2f; #end\n"
                "box { <%.2f,0,%.2f>, <%.2f,%.2f,%.2f> pigment { rgb <%.2f,%.2f,0.30> }"
                " finish { phong 0.6 } translate <BOX%zuX,0,BOX%zuZ> }\n",
                i, i, b.cx, i, i, b.cz,
                -b.hx, -b.hz, b.hx, b.h, b.hz, b.r, b.g, i, i);
        } else {
            snprintf(buf, sizeof buf,
                "box { <%.2f,0,%.2f>, <%.2f,%.2f,%.2f> pigment { rgb <%.2f,%.2f,0.30> }"
                " finish { phong 0.6 } }\n",
                b.cx - b.hx, b.cz - b.hz, b.cx + b.hx, b.h, b.cz + b.hz, b.r, b.g);
        }
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
struct Input { float move = 0, turn = 0; bool jump = false; };

static void simulate(Player& p, const Input& in, float dt) {
    p.yaw += in.turn * TURN_RATE * dt;
    bool moving = in.move != 0;
    if (moving) {
        float prev_step = p.step;
        p.step += STEP_RATE * dt * (in.move > 0 ? 1 : -1);
        // footstep on each half-cycle of the leg swing (sin zero crossings)
        if (p.grounded && (long)floorf(prev_step / (float)M_PI) !=
                          (long)floorf(p.step / (float)M_PI))
            g_audio.play(FdAudio::STEP, 0.6f);
        p.x += sinf(p.yaw) * in.move * SPEED * dt;
        p.z += cosf(p.yaw) * in.move * SPEED * dt;
    }
    // always collide: dynamic boxes can move INTO the player. Bump audio only
    // when the player is driving into geometry, rate-limited by the cooldown.
    static float bump_cool = 0;
    bump_cool = fmaxf(0.0f, bump_cool - dt);
    if (collide(&p.x, &p.z) && moving && bump_cool == 0.0f) {
        g_audio.play(FdAudio::BUMP, 0.8f);
        bump_cool = 0.25f;
    }
    if (in.jump && p.grounded) {
        p.jv = JUMP_V; p.grounded = false;
        g_audio.play(FdAudio::JUMP);
    }
    if (!p.grounded) {
        p.jv += GRAV * dt; p.jy += p.jv * dt;
        if (p.jy <= 0) {
            p.jy = 0; p.jv = 0; p.grounded = true;
            g_audio.play(FdAudio::LAND);
        }
    }
}

static std::vector<Declare> declares_for(const Player& p) {
    std::vector<Declare> d = {
        {"POSX", p.x}, {"POSZ", p.z}, {"JUMP", p.jy},
        {"TURN", p.yaw * 57.29578f}, {"STEP", p.step} };
    char nm[16];
    for (size_t i = 0; i < g_world.size(); ++i) {
        if (!g_world[i].dyn) continue;
        snprintf(nm, sizeof nm, "BOX%zuX", i); d.push_back({nm, g_world[i].cx});
        snprintf(nm, sizeof nm, "BOX%zuZ", i); d.push_back({nm, g_world[i].cz});
    }
    return d;
}

static double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// ============================ selftest (headless) ============================
static int selftest(FdRenderer& r, int frames) {
    Player p;
    const float dt = 1.0f / SIM_HZ;
    double t0 = now_s(); uint64_t sim_us = 0;
    double t = 0;
    // track PEAK deviation of dynamic boxes from spawn — a sinusoidal patrol
    // can be back at spawn on the exact final frame (it was, at 180 frames)
    std::vector<float> spawn_cx; float max_dev = 0;
    for (const Aabb& b : g_world) spawn_cx.push_back(b.cx);
    for (int i = 0; i < frames; ++i) {
        Input in;
        if (i < frames * 2 / 3) in.move = 1;        // walk into the wall ahead
        else in.jump = (i == frames * 2 / 3);       // then jump once
        script_tick(t, 2 * dt, p);                  // sim time, not wall time
        for (size_t bi = 0; bi < g_world.size(); ++bi)
            if (g_world[bi].dyn)
                max_dev = fmaxf(max_dev, fabsf(g_world[bi].cx - spawn_cx[bi]));
        for (int k = 0; k < 2; ++k) { simulate(p, in, dt); t += dt; }
        if (!r.render(320, 180, declares_for(p))) return 1;
        sim_us += r.frame_us;
    }
    double wall = now_s() - t0;
    // wall box front face z=5.4 (first box cz-hz from the script), radius 0.45
    float zmax = 5.4f - P_RADIUS + 0.001f;
    printf("fd-game selftest: %d frames, daemon avg %.2f ms => %.1f fps, "
           "end-to-end %.1f fps\n", frames, sim_us / 1000.0 / frames,
           1e6 * frames / (double)sim_us, frames / wall);
    printf("  player stopped at z=%.3f (wall clamp %.3f): %s\n", p.z, zmax,
           p.z <= zmax ? "COLLISION HELD" : "COLLISION FAILED");
    bool script_ok = true;
    if (g_L) {
        script_ok = max_dev > 0.5f;
        printf("  dynamic patrol box: peak deviation %.2f — %s\n", max_dev,
               script_ok ? "MOVED (script driving world)" : "DID NOT MOVE");
    }
    // audio hooks must fire even with no device: walk = steps, wall = bump,
    // the scripted jump = jump + land
    long steps = g_audio.triggers[FdAudio::STEP], bumps = g_audio.triggers[FdAudio::BUMP];
    long jumps = g_audio.triggers[FdAudio::JUMP], lands = g_audio.triggers[FdAudio::LAND];
    bool audio_ok = steps > 0 && bumps > 0 && jumps == 1 && lands == 1;
    printf("  audio triggers: %ld steps, %ld bumps, %ld jump, %ld land (device %s) — %s\n",
           steps, bumps, jumps, lands, g_audio.device_ok() ? "open" : "absent",
           audio_ok ? "HOOKS FIRED" : "HOOKS MISSING");
    FILE* f = fopen("fd_game_selftest.ppm", "wb");
    if (f) {
        fprintf(f, "P6\n%u %u\n255\n", r.fb_w, r.fb_h);
        for (size_t i = 0; i < (size_t)r.fb_w * r.fb_h; ++i) fwrite(&r.fb[i * 4], 1, 3, f);
        fclose(f);
        printf("  last frame -> fd_game_selftest.ppm\n");
    }
    return (p.z <= zmax && script_ok && audio_ok) ? 0 : 1;
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
    const float dt = 1.0f / SIM_HZ;
    bool running = true;
    double prev = now_s(), acc = 0, simt = 0, fpsT = prev; int fpsN = 0;
    printf("fd-game: WASD/arrows move+turn, SPACE jump, ESC quit\n");
    while (running) {
        double now = now_s(), ft = now - prev; prev = now;
        if (ft > 0.25) ft = 0.25;
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

        script_tick(simt, ft, p);
        while (acc >= dt) { simulate(p, in, dt); acc -= dt; simt += dt; in.jump = false; }

        if (!r.render(rW, rH, declares_for(p))) { fprintf(stderr, "render failed\n"); break; }
        SDL_UpdateTexture(tex, NULL, r.fb.data(), rW * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);

        if (++fpsN >= 10) {
            char ti[128];
            snprintf(ti, sizeof ti, "fd-game  %dx%d->%dx%d  %.0f fps (trace %.1f ms)",
                     rW, rH, winW, winH, fpsN / (now - fpsT), r.frame_us / 1000.0);
            SDL_SetWindowTitle(win, ti);
            fpsT = now; fpsN = 0;
        }
    }
    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}

int main(int argc, char** argv) {
    bool st = argc > 1 && strcmp(argv[1], "--selftest") == 0;
    int frames = st && argc > 2 ? atoi(argv[2]) : 90;
    const char* sock   = st ? (argc > 3 ? argv[3] : "/tmp/feverdream.sock")
                            : (argc > 1 ? argv[1] : "/tmp/feverdream.sock");
    const char* script = st ? (argc > 4 ? argv[4] : "arena.lua")
                            : (argc > 5 ? argv[5] : "arena.lua");
    int winW = (!st && argc > 2) ? atoi(argv[2]) : 1280;
    int winH = (!st && argc > 3) ? atoi(argv[3]) : 720;
    int rdiv = (!st && argc > 4) ? atoi(argv[4]) : 4;

    if (load_script(script))
        printf("fd-game: world + config from %s (%zu boxes)\n", script, g_world.size());
    else {
        printf("fd-game: no script (%s) — built-in arena\n", script);
        default_world();
    }

    // optional: a silent box is a playable box (triggers still count headless)
    printf("fd-game: audio %s\n", g_audio.init() ? "open (procedural synth, 5 sfx)"
                                                 : "unavailable — running silent");

    FdRenderer r;
    if (!r.connect_to(sock)) {
        fprintf(stderr, "fd-game: cannot connect to %s -- is fd-daemon running?\n", sock);
        return 1;
    }
    if (!r.scene(build_scene())) { fprintf(stderr, "fd-game: scene rejected\n"); return 1; }
    int rc = st ? selftest(r, frames) : play(r, winW, winH, rdiv < 1 ? 1 : rdiv);
    g_audio.shutdown();
    if (g_L) lua_close(g_L);
    return rc;
}
