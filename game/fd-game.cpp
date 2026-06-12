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

#include <dlfcn.h>

#include "fd_audio.h"
static FdAudio g_audio;

// ---- optional GPU post (libfdpost.so, CUDA on the local 4070) ---------------
// dlopened only when FD_GPU=1 — the build never needs the CUDA toolkit, and
// a missing .so / missing GPU just means the plain SDL upscale path.
struct GpuPost {
    int  (*init)(int, int, int, int) = NULL;
    int  (*frame)(const uint8_t*, float, int, uint8_t*) = NULL;
    void (*reset)() = NULL;
    void (*shutdown)() = NULL;
    void* handle = NULL;
    bool active = false;

    bool load(int inW, int inH, int outW, int outH) {
        const char* env = getenv("FD_GPU");
        if (!env || !*env || strcmp(env, "0") == 0) return false;
        // resolve the .so next to the EXECUTABLE, never the cwd (tri-brain:
        // cwd-relative dlopen runs untrusted code if launched elsewhere).
        // FD_GPU_LIB overrides with an explicit path.
        char libpath[512];
        const char* override_path = getenv("FD_GPU_LIB");
        if (override_path && *override_path)
            snprintf(libpath, sizeof libpath, "%s", override_path);
        else {
            char exe[448];
            ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
            if (n <= 0) return false;
            exe[n] = 0;
            char* slash = strrchr(exe, '/');
            if (slash) *slash = 0;
            snprintf(libpath, sizeof libpath, "%s/libfdpost.so", exe);
        }
        handle = dlopen(libpath, RTLD_NOW);
        if (!handle) { fprintf(stderr, "fd-game: FD_GPU set but %s\n", dlerror()); return false; }
        init     = (int  (*)(int,int,int,int))         dlsym(handle, "fdpost_init");
        frame    = (int  (*)(const uint8_t*,float,int,uint8_t*)) dlsym(handle, "fdpost_frame");
        reset    = (void (*)())                        dlsym(handle, "fdpost_reset");
        shutdown = (void (*)())                        dlsym(handle, "fdpost_shutdown");
        if (!init || !frame || !shutdown || init(inW, inH, outW, outH) != 0) {
            fprintf(stderr, "fd-game: GPU post init failed — CPU path\n");
            if (shutdown) shutdown();        // free any partial CUDA allocs
            dlclose(handle); handle = NULL;
            return false;
        }
        active = true;
        return true;
    }
    void unload() {
        if (active && shutdown) shutdown();
        if (handle) dlclose(handle);
        handle = NULL; active = false;
    }
};
static GpuPost g_gpu;

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
        // mirror the daemon's whitelist limit: a >32-char name would truncate
        // on the wire and poison the rest of the declare list
        size_t nd = 0;
        for (const Declare& d : decls) if (d.name.size() <= 32) nd++;
        b.push_back((uint8_t)nd);
        for (const Declare& d : decls) {
            if (d.name.size() > 32) {
                fprintf(stderr, "fd-game: declare '%s' too long — skipped\n", d.name.c_str());
                continue;
            }
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
        // a FRAME without the buffer (daemon-side dimension transition) is a
        // soft miss — keep showing the previous frame rather than aborting
        if (flags & FLAG_WANT_FB) fb.assign(p.begin() + 12, p.end());
        return true;
    }
    bool frame_fits(int w, int h) const { return fb.size() == (size_t)w * h * 4; }

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
        if (len > 80u * 1024 * 1024) return false;   // > max 4096x4096 RGBA reply
        p.resize(len);
        return !len || io_full(false, p.data(), len);
    }
};

// ============================ world + tuning =================================
struct Aabb { float cx, cz, hx, hz, h; bool dyn; float r, g; float cy; bool solid;
              bool acorn; };   // shape="acorn" in the script (else a box)
static std::vector<Aabb> g_world;
static float P_RADIUS = 0.45f;

// movement tuning -- overridable from the script's `config` table
static float SIM_HZ = 120.0f;
static float SPEED = 4.2f, TURN_RATE = 2.6f, STEP_RATE = 11.0f;
static float GRAV = -28.0f, JUMP_V = 9.5f;

// built-in world, used when no script is present (mirrors the original arena)
static void default_world() {
    g_world = {
        {  0.0f,  6.0f, 2.2f, 0.6f, 1.6f, false, 0.55f, 0.35f, 0, true },
        { -5.0f,  0.0f, 0.8f, 0.8f, 0.9f, false, 0.65f, 0.40f, 0, true },
        {  5.0f, -2.0f, 0.8f, 0.8f, 2.4f, false, 0.75f, 0.45f, 0, true },
        {  4.0f,  5.0f, 1.2f, 1.2f, 0.5f, false, 0.85f, 0.50f, 0, true },
    };
}

// platformer rules: a box is a WALL or a FLOOR depending on your altitude.
// top within STEP_UP of your feet -> floor you glide onto; higher -> wall;
// entirely above your head -> you walk underneath it.
static float STEP_UP   = 0.35f;   // max ledge you walk up (config.step_up)
static float HEAD_ROOM = 1.8f;    // boxes above this are overhead (config.head_room)

// circle-vs-AABB push-out in XZ at a given feet height; true if pushed
static bool collide(float* px, float* pz, float feet) {
    bool pushed = false;
    for (const Aabb& b : g_world) {
        if (!b.solid) continue;                      // collectibles/decor
        if (b.cy + b.h < 0.1f) continue;             // sunken below the floor
        if (b.cy + b.h <= feet + STEP_UP) continue;  // floor at this altitude
        if (b.cy >= feet + HEAD_ROOM) continue;      // overhead — walk under
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

// highest standable surface under the player at this XZ, given feet height:
// ground plane (0) or any box top at-or-below feet + tolerance
static float ground_height(float px, float pz, float feet, float tol) {
    float g = 0;
    for (const Aabb& b : g_world) {
        if (!b.solid) continue;                      // can't stand on a star
        if (b.cy + b.h < 0.1f) continue;
        float top = b.cy + b.h;
        if (top > feet + tol) continue;              // too high to stand on
        float reach = P_RADIUS * 0.7f;               // forgiving ledge grab
        if (px > b.cx - b.hx - reach && px < b.cx + b.hx + reach &&
            pz > b.cz - b.hz - reach && pz < b.cz + b.hz + reach)
            g = fmaxf(g, top);
    }
    return g;
}

// ============================ Lua scripting ==================================
// The script owns the world: a global `boxes` array defines the arena (each
// entry {cx,cz,hx,hz,h [,dyn] [,r] [,g]}), an optional `config` table tunes
// movement, and an optional on_tick(t, dt, player) hook runs once per render
// frame -- it may mutate dynamic boxes' cx/cz in the global `boxes` table,
// which the host reads back into BOTH the collision world and the renderer
// declares. Player table is read-only this round.
static lua_State* g_L = NULL;
static char g_title[64] = "fd-game";    // scripts override via game_title

// play_sound("jump"|"land"|"step"|"bump"|"blip" [, gain]) — script audio hook
static int l_play_sound(lua_State* L) {
    const char* want = luaL_checkstring(L, 1);
    float gain = (float)luaL_optnumber(L, 2, 1.0);
    for (int i = 0; i < FdAudio::SOUND_COUNT; ++i)
        if (strcmp(want, FdAudio::name(i)) == 0) {
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

    // boxes first: a script with no world is rejected WHOLE — close the state
    // so its on_tick can't keep running against the built-in fallback arena
    lua_getglobal(g_L, "boxes");
    if (!lua_istable(g_L, -1)) {
        fprintf(stderr, "fd-game: %s defines no `boxes` table — script unloaded\n", path);
        lua_close(g_L); g_L = NULL;
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
            b.cy = lua_field_num(g_L, "cy", 0);
            lua_getfield(g_L, -1, "dyn");
            b.dyn = lua_toboolean(g_L, -1) != 0;
            lua_pop(g_L, 1);
            lua_getfield(g_L, -1, "solid");          // default true; collectibles
            b.solid = lua_isnil(g_L, -1) || lua_toboolean(g_L, -1) != 0;
            lua_pop(g_L, 1);
            lua_getfield(g_L, -1, "shape");          // "acorn" | default box
            b.acorn = lua_isstring(g_L, -1) &&
                      strcmp(lua_tostring(g_L, -1), "acorn") == 0;
            lua_pop(g_L, 1);
            g_world.push_back(b);
        }
        lua_pop(g_L, 1);
    }
    lua_pop(g_L, 1);

    lua_getglobal(g_L, "config");
    if (lua_istable(g_L, -1)) {
        SPEED     = lua_field_num(g_L, "speed", SPEED);
        TURN_RATE = lua_field_num(g_L, "turn_rate", TURN_RATE);
        STEP_RATE = lua_field_num(g_L, "step_rate", STEP_RATE);
        GRAV      = lua_field_num(g_L, "gravity", GRAV);
        JUMP_V    = lua_field_num(g_L, "jump_v", JUMP_V);
        P_RADIUS  = lua_field_num(g_L, "player_radius", P_RADIUS);
        STEP_UP   = lua_field_num(g_L, "step_up", STEP_UP);
        HEAD_ROOM = lua_field_num(g_L, "head_room", HEAD_ROOM);
    }
    lua_pop(g_L, 1);

    snprintf(g_title, sizeof g_title, "fd-game");    // reset before each load
    lua_getglobal(g_L, "game_title");
    if (lua_isstring(g_L, -1))
        snprintf(g_title, sizeof g_title, "%s", lua_tostring(g_L, -1));
    lua_pop(g_L, 1);
    return true;
}

struct Player {
    float x = 0, z = 0, yaw = 0;
    float step = 0;
    float jy = 0, jv = 0; bool grounded = true;
};

// game state the script publishes for the HUD / end screens
struct GameHud {
    int score = 0, lives = -1;          // lives -1 = script has no lives concept
    char state[16] = "playing";         // "playing" | "won" | "lost"
};
static GameHud g_hud;

// call on_tick(t, dt, player); read back dynamic boxes, knockback, game state
static void script_tick(double t, double dt, Player& p) {
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
                g_world[i].cy = lua_field_num(g_L, "cy", g_world[i].cy);
            }
            lua_pop(g_L, 1);
        }
    }
    lua_pop(g_L, 1);

    // knockback channel: script sets push_x/push_z, host applies + clears
    lua_getglobal(g_L, "push_x");
    lua_getglobal(g_L, "push_z");
    if (lua_isnumber(g_L, -2) || lua_isnumber(g_L, -1)) {
        p.x += (float)lua_tonumber(g_L, -2);
        p.z += (float)lua_tonumber(g_L, -1);
        collide(&p.x, &p.z, p.jy);
        lua_pushnil(g_L); lua_setglobal(g_L, "push_x");
        lua_pushnil(g_L); lua_setglobal(g_L, "push_z");
    }
    lua_pop(g_L, 2);

    // game state for the HUD: game_score, game_lives, game_state
    lua_getglobal(g_L, "game_score");
    if (lua_isnumber(g_L, -1)) g_hud.score = (int)lua_tointeger(g_L, -1);
    lua_pop(g_L, 1);
    lua_getglobal(g_L, "game_lives");
    if (lua_isnumber(g_L, -1)) g_hud.lives = (int)lua_tointeger(g_L, -1);
    lua_pop(g_L, 1);
    lua_getglobal(g_L, "game_state");
    if (lua_isstring(g_L, -1))
        snprintf(g_hud.state, sizeof g_hud.state, "%s", lua_tostring(g_L, -1));
    lua_pop(g_L, 1);
}

// ====================== scene recipe (generated) =============================
// Static boxes are baked into the SDL text; dynamic boxes are emitted centered
// at the origin and placed per frame via BOX<i>X/BOX<i>Z declares -- same
// values that drive their collision AABBs.
static std::string build_scene() {
    // 512 truncated the acorn block mid-cylinder -> unbalanced braces -> the
    // daemon parse-failed EVERY frame and the screen went black. Size checked
    // below; never assume a snprintf fit.
    char buf[2048];
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
        "camera { location <POSX-sin(radians(TURN))*7, 4.4+JUMP*0.85,"
        " POSZ-cos(radians(TURN))*7>\n"
        "  look_at <POSX, 1.0+JUMP*0.85, POSZ> angle 52 right x*16/9 up y }\n";
    for (size_t i = 0; i < g_world.size(); ++i) {
        const Aabb& b = g_world[i];
        int n = 0;
        if (b.dyn && b.acorn) {
            // shape="acorn": nut, cap, stem — sized from the box height
            float s = b.h / 0.5f;
            n = snprintf(buf, sizeof buf,
                "#ifndef (BOX%zuX) #declare BOX%zuX=%.2f; #end\n"
                "#ifndef (BOX%zuY) #declare BOX%zuY=%.2f; #end\n"
                "#ifndef (BOX%zuZ) #declare BOX%zuZ=%.2f; #end\n"
                "union {\n"
                "  // scale-then-translate (POV scale is about the origin)\n"
                "  sphere { 0, %.2f scale <1,1.15,1> translate <0,%.2f,0>"
                "    pigment { rgb <0.78,0.56,0.28> } finish { phong 0.8 } }\n"
                "  sphere { 0, %.2f scale <1,0.55,1> translate <0,%.2f,0>"
                "    pigment { rgb <0.42,0.26,0.12> } finish { phong 0.5 } }\n"
                "  cylinder { <0,%.2f,0>, <0,%.2f,0>, %.3f"
                "    pigment { rgb <0.38,0.23,0.10> } }\n"
                "  translate <BOX%zuX,BOX%zuY,BOX%zuZ> }\n",
                i, i, b.cx, i, i, b.cy, i, i, b.cz,
                0.20f * s, 0.20f * s,
                0.21f * s, 0.36f * s,
                0.44f * s, 0.56f * s, 0.035f * s,
                i, i, i);
        } else if (b.dyn) {
            n = snprintf(buf, sizeof buf,
                "#ifndef (BOX%zuX) #declare BOX%zuX=%.2f; #end\n"
                "#ifndef (BOX%zuY) #declare BOX%zuY=%.2f; #end\n"
                "#ifndef (BOX%zuZ) #declare BOX%zuZ=%.2f; #end\n"
                "box { <%.2f,0,%.2f>, <%.2f,%.2f,%.2f> pigment { rgb <%.2f,%.2f,0.30> }"
                " finish { phong 0.7 reflection 0.08 } translate <BOX%zuX,BOX%zuY,BOX%zuZ> }\n",
                i, i, b.cx, i, i, b.cy, i, i, b.cz,
                -b.hx, -b.hz, b.hx, b.h, b.hz, b.r, b.g, i, i, i);
        } else {
            n = snprintf(buf, sizeof buf,
                "box { <%.2f,%.2f,%.2f>, <%.2f,%.2f,%.2f> pigment { rgb <%.2f,%.2f,0.30> }"
                " finish { phong 0.6 } }\n",
                b.cx - b.hx, b.cy, b.cz - b.hz,
                b.cx + b.hx, b.cy + b.h, b.cz + b.hz, b.r, b.g);
        }
        if (n < 0 || n >= (int)sizeof buf) {
            // a truncated chunk = unbalanced braces = the daemon parse-fails
            // EVERY frame and the screen goes black. Die loudly instead.
            fprintf(stderr, "fd-game: FATAL: scene chunk for box %zu truncated "
                    "(%d bytes > %zu buffer)\n", i, n, sizeof buf);
            exit(1);
        }
        s += buf;
    }
    s +=
        "// CHUNKINS the squirrel: plump body, cream belly, ears, snout, and\n"
        "// the all-important bushy tail. Legs swing by STEP; the tail wags.\n"
        "#declare LEG = 28*sin(STEP);\n"
        "#declare WAG = 14*sin(STEP*0.7);\n"
        "#declare FUR   = pigment { rgb <0.55,0.32,0.15> };\n"
        "#declare CREAM = pigment { rgb <0.93,0.85,0.70> };\n"
        "#declare DARK  = pigment { rgb <0.10,0.07,0.05> };\n"
        "union {\n"
        "  // scale-THEN-translate: POV scale is about the origin, so a scaled\n"
        "  // sphere must be built at 0 and moved after (or its position scales)\n"
        "  sphere { 0, 0.48 scale <0.9,1.0,0.85> translate <0,0.95,0>"
        " pigment {FUR} finish { phong 0.6 } }\n"
        "  sphere { 0, 0.34 scale <0.75,0.85,0.6> translate <0,0.88,0.20>"
        " pigment {CREAM} finish { phong 0.5 } }\n"
        "  sphere { <0,1.62,0.10>, 0.30 pigment {FUR} finish { phong 0.6 } }\n"
        "  sphere { 0, 0.13 scale <1,0.8,1.1> translate <0,1.54,0.36>"
        " pigment {CREAM} finish { phong 0.5 } }\n"
        "  sphere { <0,1.57,0.47>, 0.045 pigment {DARK} }\n"
        "  sphere { <-0.12,1.70,0.30>, 0.05 pigment {DARK} }\n"
        "  sphere { < 0.12,1.70,0.30>, 0.05 pigment {DARK} }\n"
        "  sphere { 0, 0.09 scale <0.8,1.3,0.6> translate <-0.15,1.90,0.04> pigment {FUR} }\n"
        "  sphere { 0, 0.09 scale <0.8,1.3,0.6> translate < 0.15,1.90,0.04> pigment {FUR} }\n"
        "  box { <-0.09,-0.55,-0.09>,<0.09,0,0.09> rotate x*LEG  translate <-0.17,0.55,0>\n"
        "    pigment {FUR} finish { phong 0.5 } }\n"
        "  box { <-0.09,-0.55,-0.09>,<0.09,0,0.09> rotate x*-LEG translate < 0.17,0.55,0>\n"
        "    pigment {FUR} finish { phong 0.5 } }\n"
        "  union {\n"
        "    // tail stays BELOW the head and curves slightly right of center\n"
        "    // so the body still reads from the chase cam\n"
        "    sphere { <0.10,0.42,-0.40>, 0.17 }\n"
        "    sphere { <0.16,0.76,-0.55>, 0.23 }\n"
        "    sphere { <0.18,1.12,-0.54>, 0.26 }\n"
        "    sphere { <0.14,1.44,-0.40>, 0.20 }\n"
        "    pigment { rgb <0.62,0.34,0.14> } finish { phong 0.55 }\n"
        "    rotate y*WAG\n"
        "  }\n"
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
    if (collide(&p.x, &p.z, p.jy) && moving && bump_cool == 0.0f) {
        g_audio.play(FdAudio::BUMP, 0.8f);
        bump_cool = 0.25f;
    }
    if (in.jump && p.grounded) {
        p.jv = JUMP_V; p.grounded = false;
        g_audio.play(FdAudio::JUMP);
    }
    if (p.grounded) {
        // follow the floor: step up small ledges, ride rising platforms,
        // and FALL when the floor is no longer under you (edges, sinking lifts)
        float g = ground_height(p.x, p.z, p.jy, STEP_UP);
        if (g < p.jy - 0.05f) p.grounded = false;    // walked off an edge
        else p.jy = g;
    }
    if (!p.grounded) {
        float prev_jy = p.jy;
        p.jv += GRAV * dt; p.jy += p.jv * dt;
        if (p.jv <= 0) {                             // land only while falling
            // SWEPT landing: any surface we passed between previous and new
            // feet height catches us — a fast fall can't tunnel a platform
            // between ticks (tri-brain)
            float g = ground_height(p.x, p.z, prev_jy, 0.05f);
            if (p.jy <= g) {
                p.jy = g; p.jv = 0; p.grounded = true;
                g_audio.play(FdAudio::LAND);
            }
        }
        if (p.jy < 0) { p.jy = 0; p.jv = 0; p.grounded = true; }  // safety floor
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
        snprintf(nm, sizeof nm, "BOX%zuY", i); d.push_back({nm, g_world[i].cy});
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
    if (!r.frame_fits(320, 180)) {       // same pixel guard as gametest
        printf("  pixels: MISSING — scene failing to render?\n");
        script_ok = false;
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

// gametest: headless proof the GAME plays — walk forward through the relic
// the script placed on the +Z lane, assert the script awarded the pickup
static int gametest(FdRenderer& r, int frames) {
    Player p;
    const float dt = 1.0f / SIM_HZ;
    double t = 0; uint64_t us = 0;
    int score_at_half = -1;
    float max_feet = 0;
    for (int i = 0; i < frames; ++i) {
        Input in; in.move = 1;                  // hold forward...
        in.jump = (i % 60 == 30);               // ...hopping as it goes
        script_tick(t, 2 * dt, p);
        for (int k = 0; k < 2; ++k) { simulate(p, in, dt); in.jump = false; t += dt; }
        max_feet = fmaxf(max_feet, p.grounded ? p.jy : max_feet);
        if (!r.render(320, 180, declares_for(p))) return 1;
        us += r.frame_us;
        if (i == frames / 2) score_at_half = g_hud.score;
    }
    printf("fd-game gametest: %d frames, %.1f fps end-to-end\n",
           frames, 1e6 * frames / (double)us);
    printf("  score %d (half-way %d), lives %d, state %s, highest stand %.2f\n",
           g_hud.score, score_at_half, g_hud.lives, g_hud.state, max_feet);
    // PIXEL assertion: a parse-failing scene "renders" at full speed with no
    // framebuffer — logic asserts alone let a black screen pass (it happened)
    bool pixels = r.frame_fits(320, 180);
    if (pixels) {
        pixels = false;
        for (size_t k = 4; k < r.fb.size(); k += 4)
            if (r.fb[k] != r.fb[0] || r.fb[k+1] != r.fb[1]) { pixels = true; break; }
    }
    printf("  pixels: %s\n", pixels ? "REAL FRAME (non-uniform 320x180)"
                                    : "MISSING/BLANK — scene failing to render?");
    bool ok = pixels && g_hud.score >= 1 && g_hud.lives >= 0 &&
              strcmp(g_hud.state, "lost") != 0;
    printf("  %s\n", ok ? "GAME LOGIC LIVE (relic collected via Lua)" : "GAME LOGIC FAILED");
    FILE* f = fopen("fd_gametest.ppm", "wb");
    if (f) {
        fprintf(f, "P6\n%u %u\n255\n", r.fb_w, r.fb_h);
        for (size_t i = 0; i < (size_t)r.fb_w * r.fb_h; ++i) fwrite(&r.fb[i * 4], 1, 3, f);
        fclose(f);
    }
    return ok ? 0 : 1;
}

// ============================ interactive (SDL2) =============================
#include <SDL2/SDL.h>

static int play(FdRenderer& r, int winW, int winH, int rdiv) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_Window* win = SDL_CreateWindow(g_title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winW, winH, SDL_WINDOW_SHOWN);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    int rW = winW / rdiv, rH = winH / rdiv;
    // GPU post path: accumulate + upscale + dither on the 4070; the texture
    // is then full window res. CPU path: stream internal res, SDL upscales.
    bool gpu = g_gpu.load(rW, rH, winW, winH);
    std::vector<uint8_t> gpu_out;
    if (gpu) gpu_out.resize((size_t)winW * winH * 4);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STREAMING, gpu ? winW : rW, gpu ? winH : rH);
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);   // the VGA crunch

    Player p;
    const float dt = 1.0f / SIM_HZ;
    bool running = true, jump_pending = false;   // latched until a sim step consumes it
    double prev = now_s(), acc = 0, simt = 0, fpsT = prev; int fpsN = 0;
    printf("fd-game: WASD/arrows move+turn, SPACE jump, ESC quit\n");
    while (running) {
        double now = now_s(), ft = now - prev; prev = now;
        if (ft > 0.25) ft = 0.25;
        acc += ft;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) running = false;
                if (e.key.keysym.sym == SDLK_SPACE)  jump_pending = true;
            }
        }
        const Uint8* k = SDL_GetKeyboardState(NULL);
        Input in;
        if (k[SDL_SCANCODE_UP]    || k[SDL_SCANCODE_W]) in.move += 1;
        if (k[SDL_SCANCODE_DOWN]  || k[SDL_SCANCODE_S]) in.move -= 1;
        if (k[SDL_SCANCODE_LEFT]  || k[SDL_SCANCODE_A]) in.turn -= 1;
        if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) in.turn += 1;

        script_tick(simt, ft, p);
        // evaluate AFTER the tick: a state change this frame freezes this frame
        bool over = strcmp(g_hud.state, "playing") != 0;
        while (acc >= dt) {
            in.jump = jump_pending; jump_pending = false;   // consumed by one step only
            if (!over) simulate(p, in, dt);                 // end screen: world freezes
            acc -= dt; simt += dt;
        }

        if (!r.render(rW, rH, declares_for(p))) { fprintf(stderr, "render failed\n"); break; }
        static int soft_miss = 0;
        if (!r.frame_fits(rW, rH)) {
            // tolerate transitions, but a black screen must NOT be silent
            if (++soft_miss == 60)
                fprintf(stderr, "fd-game: 60 consecutive frames without a "
                        "framebuffer — the scene is failing to render "
                        "(check the daemon / scene syntax)\n");
        } else soft_miss = 0;
        if (r.frame_fits(rW, rH)) {              // soft miss: keep last frame
            if (gpu) {
                // motion-adaptive temporal alpha from the SIM, not estimation:
                // still -> deep accumulation (AA), moving -> fresh (no ghosts)
                float speed = fabsf(in.move) * SPEED + fabsf(in.turn) * 4.0f
                            + fabsf(p.jv) * 0.4f;
                float alpha = speed > 0.1f ? 0.85f : 0.30f;
                if (g_gpu.frame(r.fb.data(), alpha, 32, gpu_out.data()) == 0)
                    SDL_UpdateTexture(tex, NULL, gpu_out.data(), winW * 4);
                else {
                    // fall back SAFELY: the texture must shrink to internal
                    // res or the rW-pitch upload reads out of bounds (tri-brain)
                    fprintf(stderr, "fd-game: GPU post error — CPU path\n");
                    g_gpu.unload(); gpu = false;
                    SDL_DestroyTexture(tex);
                    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32,
                                            SDL_TEXTUREACCESS_STREAMING, rW, rH);
                    SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
                }
            }
            if (!gpu) SDL_UpdateTexture(tex, NULL, r.fb.data(), rW * 4);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, NULL, NULL);

            // HUD pips: score (gold, top-left), lives (red, top-right)
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_Rect pip;
            for (int s = 0; s < g_hud.score && s < 32; ++s) {
                pip = { 16 + s * 26, 14, 18, 18 };
                SDL_SetRenderDrawColor(ren, 240, 200, 60, 235);
                SDL_RenderFillRect(ren, &pip);
            }
            for (int l = 0; l < g_hud.lives && l < 32; ++l) {
                pip = { winW - 34 - l * 26, 14, 18, 18 };
                SDL_SetRenderDrawColor(ren, 220, 60, 60, 235);
                SDL_RenderFillRect(ren, &pip);
            }
            if (over) {                          // tint: gold for won, red for lost
                bool won = strcmp(g_hud.state, "won") == 0;
                SDL_SetRenderDrawColor(ren, won ? 255 : 160, won ? 215 : 30,
                                       won ? 80 : 30, 70);
                SDL_Rect full = { 0, 0, winW, winH };
                SDL_RenderFillRect(ren, &full);
            }
            SDL_RenderPresent(ren);
        }

        if (++fpsN >= 10) {
            char ti[160];
            if (strcmp(g_hud.state, "won") == 0)
                snprintf(ti, sizeof ti, "%s — YOU WIN!  score %d  (ESC quit)", g_title, g_hud.score);
            else if (strcmp(g_hud.state, "lost") == 0)
                snprintf(ti, sizeof ti, "%s — ouch! score %d  (ESC quit)", g_title, g_hud.score);
            else if (g_hud.lives >= 0)       // a script publishing game state
                snprintf(ti, sizeof ti, "%s  %dx%d->%dx%d  %.0f fps (trace %.1f ms)"
                         "  score %d  lives %d", g_title, rW, rH, winW, winH,
                         fpsN / (now - fpsT), r.frame_us / 1000.0,
                         g_hud.score, g_hud.lives);
            else                             // plain world script (e.g. arena)
                snprintf(ti, sizeof ti, "%s  %dx%d->%dx%d  %.0f fps (trace %.1f ms)",
                         g_title, rW, rH, winW, winH, fpsN / (now - fpsT), r.frame_us / 1000.0);
            SDL_SetWindowTitle(win, ti);
            fpsT = now; fpsN = 0;
        }
    }
    g_gpu.unload();
    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}

int main(int argc, char** argv) {
    bool st = argc > 1 && strcmp(argv[1], "--selftest") == 0;
    bool gt = argc > 1 && strcmp(argv[1], "--gametest") == 0;
    bool headless = st || gt;
    // 180 default: the walk phase must REACH the wall for the bump assertion
    // (90 frames ends 1.2 units short — tri-brain caught the impossible default)
    int frames = headless && argc > 2 ? atoi(argv[2]) : 180;
    const char* sock   = headless ? (argc > 3 ? argv[3] : "/tmp/feverdream.sock")
                                  : (argc > 1 ? argv[1] : "/tmp/feverdream.sock");
    const char* script = headless ? (argc > 4 ? argv[4] : (gt ? "relic_sweep.lua" : "arena.lua"))
                                  : (argc > 5 ? argv[5] : "relic_sweep.lua");
    int winW = (!headless && argc > 2) ? atoi(argv[2]) : 1280;
    int winH = (!headless && argc > 3) ? atoi(argv[3]) : 720;
    int rdiv = (!headless && argc > 4) ? atoi(argv[4]) : 4;

    if (load_script(script))
        printf("fd-game: world + config from %s (%zu boxes)\n", script, g_world.size());
    else {
        printf("fd-game: no script (%s) — built-in arena\n", script);
        default_world();
    }

    // optional: a silent box is a playable box (triggers still count headless)
    bool snd = g_audio.init();
    printf("fd-game: audio %s — %d/%d sfx from assets, rest synthesized\n",
           snd ? "open" : "unavailable (running silent)",
           g_audio.loaded(), (int)FdAudio::SOUND_COUNT);

    FdRenderer r;
    if (!r.connect_to(sock)) {
        fprintf(stderr, "fd-game: cannot connect to %s -- is fd-daemon running?\n", sock);
        return 1;
    }
    if (!r.scene(build_scene())) { fprintf(stderr, "fd-game: scene rejected\n"); return 1; }
    int rc = st ? selftest(r, frames)
            : gt ? gametest(r, frames)
                 : play(r, winW, winH, rdiv < 1 ? 1 : rdiv);
    g_audio.shutdown();
    if (g_L) lua_close(g_L);
    return rc;
}
