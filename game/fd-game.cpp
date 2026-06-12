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
#include <cerrno>
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
    std::string path_, scene_text_;   // for reconnect: where + what world
public:
    std::vector<uint8_t> fb;          // RGBA8 of the last frame
    uint32_t fb_w = 0, fb_h = 0, frame_us = 0;

    bool connect_to(const char* path) {
        if (fd_ >= 0) { close(fd_); fd_ = -1; }
        fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd_ < 0) return false;
        // a hung/dead daemon must FAIL the call, never freeze the window
        // (2s >> any real frame; checked because this IS the safety guarantee)
        struct timeval tv = { 2, 0 };
        if (setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) != 0 ||
            setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv) != 0)
            fprintf(stderr, "fd-game: WARNING socket timeouts not set (%s) — "
                    "a daemon hang could freeze the window\n", strerror(errno));
        struct sockaddr_un a; memset(&a, 0, sizeof a);
        a.sun_family = AF_UNIX;
        snprintf(a.sun_path, sizeof a.sun_path, "%s", path);
        if (connect(fd_, (struct sockaddr*)&a, sizeof a) != 0) {
            close(fd_); fd_ = -1;
            return false;
        }
        path_ = path;
        return true;
    }
    ~FdRenderer() { if (fd_ >= 0) close(fd_); }

    bool scene(const std::string& sdl) {
        scene_text_ = sdl;            // remembered for reconnect
        if (!send(T_SCENE_FULL, 0, sdl.data(), (uint32_t)sdl.size())) return false;
        uint8_t type, flags; std::vector<uint8_t> p;
        if (!recv_msg(&type, &flags, p)) return false;
        return type == T_SCENE_ACK && p.size() == 4 && p[0] == 0;
    }

    // after a daemon death/restart: fresh socket + resend the current world
    bool reconnect() {
        if (path_.empty() || scene_text_.empty()) return false;
        std::string keep = scene_text_;
        return connect_to(path_.c_str()) && scene(keep);
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
enum BoxShape { SHAPE_BOX = 0, SHAPE_ACORN, SHAPE_BADDIE,
                SHAPE_HEART, SHAPE_STAR, SHAPE_CHOMP };
struct Aabb { float cx, cz, hx, hz, h; bool dyn; float r, g; float cy; bool solid;
              int shape;       // BoxShape — "acorn"/"baddie" in the script
              float ry; };     // facing (deg), baddies only — script-driven
static std::vector<Aabb> g_world;
static float P_RADIUS = 0.45f;

// movement tuning -- overridable from the script's `config` table
static float SIM_HZ = 120.0f;
static float SPEED = 4.2f, TURN_RATE = 2.6f, STEP_RATE = 11.0f;
static float GRAV = -28.0f, JUMP_V = 9.5f;
// power-up channels: scripts publish speed_mult/jump_mult each tick (a star
// power, say); missing/expired = 1.0. Lua owns the duration logic.
static float g_speed_mult = 1.0f, g_jump_mult = 1.0f;

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

// world -> box-local XZ. Matches the renderer exactly: POV `rotate y*ry` maps
// local +Z to world (sin ry, cos ry) — proven by the character's snout facing
// his movement — so collision inverts that matrix and rotating platforms
// collide exactly where they're DRAWN.
static inline void to_local(const Aabb& b, float wx, float wz, float* lx, float* lz) {
    wx -= b.cx; wz -= b.cz;
    if (b.ry != 0) {
        float a = b.ry * (float)M_PI / 180.0f, c = cosf(a), s = sinf(a);
        *lx = wx * c - wz * s;
        *lz = wx * s + wz * c;
    } else { *lx = wx; *lz = wz; }
}

// circle-vs-box push-out in XZ at a given feet height; true if pushed.
// Yaw-rotated boxes (rotating platforms) are handled via the local frame.
static bool collide(float* px, float* pz, float feet) {
    bool pushed = false;
    for (const Aabb& b : g_world) {
        if (!b.solid) continue;                      // collectibles/decor
        if (b.cy + b.h < 0.1f) continue;             // sunken below the floor
        if (b.cy + b.h <= feet + STEP_UP) continue;  // floor at this altitude
        if (b.cy >= feet + HEAD_ROOM) continue;      // overhead — walk under
        float lx, lz;
        to_local(b, *px, *pz, &lx, &lz);
        float nx = fmaxf(-b.hx, fminf(lx, b.hx));
        float nz = fmaxf(-b.hz, fminf(lz, b.hz));
        float dx = lx - nx, dz = lz - nz;
        float d2 = dx * dx + dz * dz;
        if (d2 >= P_RADIUS * P_RADIUS) continue;
        pushed = true;
        if (d2 > 1e-9f) {
            float s = (P_RADIUS - sqrtf(d2)) / sqrtf(d2);
            dx *= s; dz *= s;                        // push-out, LOCAL frame
        } else {
            dx = b.hx + P_RADIUS - lx; dz = 0;       // center inside: pop +local-x
        }
        if (b.ry != 0) {                             // local push -> world
            float a = b.ry * (float)M_PI / 180.0f, c = cosf(a), s2 = sinf(a);
            *px += dx * c + dz * s2;
            *pz += -dx * s2 + dz * c;
        } else { *px += dx; *pz += dz; }
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
        float lx, lz;
        to_local(b, px, pz, &lx, &lz);
        if (lx > -b.hx - reach && lx < b.hx + reach &&
            lz > -b.hz - reach && lz < b.hz + reach)
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
// game state the script publishes for the HUD / cards / end screens
struct GameHud {
    int score = 0, lives = -1;          // lives -1 = script has no lives concept
    int world = 0;                      // game_world: 0 = no indicator
    char state[16] = "playing";         // "playing" | "won" | "lost"
};
static GameHud g_hud;

static lua_State* g_L = NULL;
static char g_title[64] = "fd-game";    // scripts override via game_title
static char g_next[128] = "";           // scripts chain levels via next_level
static char g_current[128] = "";        // the loaded script (for retry-on-loss)
static std::vector<std::string> g_worlds;  // scripts publish a `worlds` list;
                                           // keys 1-9 jump to them (session-wide)

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
    // SANDBOX: levels are community content (bounty #14019) running inside
    // the game process. A level needs math/string/table and our API — it
    // never needs to run programs, touch files, load chunks, or poke C.
    // Strip those capabilities before the script gets a single instruction.
    static const char* banned[] = {
        "os", "io", "package", "require", "dofile", "loadfile",
        "load", "loadstring", "debug", NULL
    };
    for (int bi = 0; banned[bi]; ++bi) {
        lua_pushnil(g_L);
        lua_setglobal(g_L, banned[bi]);
    }
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
            lua_getfield(g_L, -1, "shape");          // "acorn"|"baddie"|box
            b.shape = SHAPE_BOX;
            if (lua_isstring(g_L, -1)) {
                const char* sh = lua_tostring(g_L, -1);
                if (strcmp(sh, "acorn") == 0)  b.shape = SHAPE_ACORN;
                if (strcmp(sh, "baddie") == 0) b.shape = SHAPE_BADDIE;
                if (strcmp(sh, "heart") == 0)  b.shape = SHAPE_HEART;
                if (strcmp(sh, "star") == 0)   b.shape = SHAPE_STAR;
                if (strcmp(sh, "chomp") == 0)  b.shape = SHAPE_CHOMP;
            }
            lua_pop(g_L, 1);
            b.ry = lua_field_num(g_L, "ry", 0);
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

    g_next[0] = 0;                                   // reset per level
    lua_getglobal(g_L, "next_level");
    if (lua_isstring(g_L, -1))
        snprintf(g_next, sizeof g_next, "%s", lua_tostring(g_L, -1));
    lua_pop(g_L, 1);

    // a `worlds` list (entry script usually) feeds the 1-9 world-select keys;
    // it persists for the whole session so later levels inherit it
    lua_getglobal(g_L, "worlds");
    if (lua_istable(g_L, -1)) {
        std::vector<std::string> w;
        int nw = (int)luaL_len(g_L, -1);
        for (int i = 1; i <= nw && i <= 9; ++i) {
            lua_rawgeti(g_L, -1, i);
            if (lua_isstring(g_L, -1)) w.push_back(lua_tostring(g_L, -1));
            lua_pop(g_L, 1);
        }
        if (!w.empty()) g_worlds = w;
    }
    lua_pop(g_L, 1);

    lua_getglobal(g_L, "game_world");        // file-scope: valid at load time,
    if (lua_isnumber(g_L, -1))               // so level cards know their number
        g_hud.world = (int)lua_tointeger(g_L, -1);
    lua_pop(g_L, 1);

    snprintf(g_current, sizeof g_current, "%s", path);
    return true;
}

struct Player {
    float x = 0, z = 0, yaw = 0;
    float step = 0;
    float jy = 0, jv = 0; bool grounded = true;
};

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
    lua_pushnumber(g_L, p.jv);       lua_setfield(g_L, -2, "vy");
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
                g_world[i].ry = lua_field_num(g_L, "ry", g_world[i].ry);
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

    // bounce channel: stomping a baddie pops Chunkins upward
    lua_getglobal(g_L, "bounce");
    if (lua_isnumber(g_L, -1)) {
        p.jv = (float)lua_tonumber(g_L, -1);
        p.grounded = false;
        lua_pushnil(g_L); lua_setglobal(g_L, "bounce");
    }
    lua_pop(g_L, 1);

    // power-up channels (default 1.0 when the script doesn't publish them)
    lua_getglobal(g_L, "speed_mult");
    g_speed_mult = lua_isnumber(g_L, -1) ? (float)lua_tonumber(g_L, -1) : 1.0f;
    lua_pop(g_L, 1);
    lua_getglobal(g_L, "jump_mult");
    g_jump_mult = lua_isnumber(g_L, -1) ? (float)lua_tonumber(g_L, -1) : 1.0f;
    lua_pop(g_L, 1);

    // game state for the HUD: game_score, game_lives, game_state
    lua_getglobal(g_L, "game_score");
    if (lua_isnumber(g_L, -1)) g_hud.score = (int)lua_tointeger(g_L, -1);
    lua_pop(g_L, 1);
    lua_getglobal(g_L, "game_lives");
    if (lua_isnumber(g_L, -1)) g_hud.lives = (int)lua_tointeger(g_L, -1);
    lua_pop(g_L, 1);
    lua_getglobal(g_L, "game_world");
    if (lua_isnumber(g_L, -1)) g_hud.world = (int)lua_tointeger(g_L, -1);
    lua_pop(g_L, 1);
    lua_getglobal(g_L, "game_state");
    if (lua_isstring(g_L, -1))
        snprintf(g_hud.state, sizeof g_hud.state, "%s", lua_tostring(g_L, -1));
    lua_pop(g_L, 1);
}

// ====================== title cards (level / game over / complete) ==========
// Cards are raytraced like everything else: wilderness backdrop + extruded
// gold text + a set piece (spinning acorn+Chunkins, or the smug baddie).
enum CardStyle { CARD_GOLD, CARD_DARK };

static void card_text(std::string& s, const char* msg, float scale,
                      float y, float zoff, const char* rgb, float maxw) {
    // sanitize for a POV string literal, then shrink-to-fit and center
    char clean[96]; size_t n = 0;
    for (const char* c = msg; *c && n < sizeof clean - 1; ++c)
        if (*c != '"' && *c != '\\') clean[n++] = *c;
    clean[n] = 0;
    float w = 0.78f * scale * (float)n;           // timrom ~0.78 units/char/scale
    if (w > maxw && n) { scale *= maxw / w; w = maxw; }
    float x = -0.5f * w;
    char buf[256];
    snprintf(buf, sizeof buf,
        "text { ttf \"timrom.ttf\" \"%s\" 0.35, 0 pigment { rgb <%s> }\n"
        "  finish { phong 0.7 reflection 0.08 } scale <%.2f,%.2f,1>"
        " translate <%.2f,%.2f,%.2f> }\n",
        clean, rgb, scale, scale, x, y, zoff);
    s += buf;
}

static std::string build_card_scene(const char* big, const char* sub, CardStyle style) {
    std::string s =
        "#version 3.7;\n"
        "#ifndef (SPIN) #declare SPIN=0; #end\n"
        "global_settings { assumed_gamma 1.0 }\n"
        "sky_sphere { pigment { gradient y color_map {\n"
        "  [0.0 rgb <0.75,0.85,0.95>][0.25 rgb <0.45,0.65,0.92>][1.0 rgb <0.15,0.35,0.80>] }\n"
        "  scale 2 translate y*-0.2 } }\n"
        "light_source { <-30,40,-25> rgb <1.0,0.97,0.88> }\n"
        "light_source { <20,25,30> rgb <0.3,0.35,0.45> shadowless }\n"
        "plane { y,0 texture { pigment { granite color_map {\n"
        "  [0 rgb <0.18,0.42,0.14>][0.6 rgb <0.26,0.55,0.18>][1 rgb <0.34,0.62,0.24>] } scale 3 }\n"
        "  normal { bumps 0.35 scale 0.4 } finish { ambient 0.4 diffuse 0.7 } } }\n";
    card_text(s, big, 1.42f, 3.4f, 0,
              style == CARD_GOLD ? "1.0,0.82,0.25" : "0.85,0.25,0.20", 7.2f);
    card_text(s, sub, 0.55f, 2.5f, 0, "0.95,0.93,0.85", 6.6f);
    if (style == CARD_GOLD) {
        // Chunkins + the spinning Golden Acorn (splash lineage)
        s +=
            "#declare FUR = pigment { rgb <0.55,0.32,0.15> };\n"
            "#declare CREAM = pigment { rgb <0.93,0.85,0.70> };\n"
            "#declare DARK = pigment { rgb <0.10,0.07,0.05> };\n"
            "union {\n"
            "  sphere { 0, 0.48 scale <0.9,1.0,0.85> translate <0,0.95,0> pigment {FUR} finish { phong 0.6 } }\n"
            "  sphere { <0,1.62,0.10>, 0.30 pigment {FUR} finish { phong 0.6 } }\n"
            "  sphere { 0, 0.13 scale <1,0.8,1.1> translate <0,1.54,0.36> pigment {CREAM} }\n"
            "  sphere { <-0.12,1.72,0.305>, 0.068 pigment { rgb <0.97,0.97,0.95> } finish { phong 0.9 } }\n"
            "  sphere { < 0.12,1.72,0.305>, 0.068 pigment { rgb <0.97,0.97,0.95> } finish { phong 0.9 } }\n"
            "  sphere { <-0.115,1.72,0.355>, 0.034 pigment {DARK} }\n"
            "  sphere { < 0.115,1.72,0.355>, 0.034 pigment {DARK} }\n"
            "  sphere { 0, 0.09 scale <0.8,1.3,0.6> translate <-0.15,1.90,0.04> pigment {FUR} }\n"
            "  sphere { 0, 0.09 scale <0.8,1.3,0.6> translate < 0.15,1.90,0.04> pigment {FUR} }\n"
            "  // little front paws, held up in the classic nibble pose\n"
            "  sphere { <-0.46,0.86,0.22>, 0.10 pigment {FUR} finish { phong 0.5 } }\n"
            "  sphere { < 0.46,0.86,0.22>, 0.10 pigment {FUR} finish { phong 0.5 } }\n"
            "  box { <-0.09,-0.55,-0.09>,<0.09,0,0.09> translate <-0.17,0.55,0> pigment {FUR} }\n"
            "  box { <-0.09,-0.55,-0.09>,<0.09,0,0.09> translate < 0.17,0.55,0> pigment {FUR} }\n"
            "  union { sphere { <0.16,0.76,-0.55>, 0.23 } sphere { <0.18,1.12,-0.54>, 0.26 }\n"
            "    sphere { <0.14,1.44,-0.40>, 0.20 } pigment { rgb <0.62,0.34,0.14> } }\n"
            "  rotate y*168 translate <-3.4, 0, -0.6> scale 1.1 }\n"
            "union {\n"
            "  sphere { 0, 0.20 scale <1,1.15,1> translate <0,0.20,0>\n"
            "    pigment { rgb <1.00,0.84,0.25> } finish { phong 0.9 reflection 0.10 } }\n"
            "  sphere { 0, 0.21 scale <1,0.55,1> translate <0,0.36,0>\n"
            "    pigment { rgb <0.42,0.26,0.12> } finish { phong 0.5 } }\n"
            "  cylinder { <0,0.44,0>, <0,0.56,0>, 0.035 pigment { rgb <0.38,0.23,0.10> } }\n"
            "  scale 3.4 rotate y*SPIN translate <3.4, 0.1, -0.4> }\n";
    } else {
        // the smug baddie, front and center, slowly turning
        s +=
            "union {\n"
            "  sphere { 0, 0.34 scale <1.05,0.80,1.30> translate <0,0.34,0>\n"
            "    pigment { rgb <0.28,0.24,0.30> } finish { phong 0.5 } }\n"
            "  sphere { 0, 0.21 translate <0,0.58,0.34> pigment { rgb <0.30,0.26,0.32> } }\n"
            "  cone { <0,0.55,0.46>, 0.10, <0,0.52,0.68>, 0 pigment { rgb <0.22,0.19,0.24> } }\n"
            "  cone { <-0.12,0.74,0.26>, 0.07, <-0.14,0.94,0.24>, 0 pigment { rgb <0.26,0.22,0.28> } }\n"
            "  cone { < 0.12,0.74,0.26>, 0.07, < 0.14,0.94,0.24>, 0 pigment { rgb <0.26,0.22,0.28> } }\n"
            "  sphere { <-0.09,0.64,0.50>, 0.035 pigment { rgb <1.0,0.85,0.25> } finish { ambient 0.9 } }\n"
            "  sphere { < 0.09,0.64,0.50>, 0.035 pigment { rgb <1.0,0.85,0.25> } finish { ambient 0.9 } }\n"
            "  cone { <0,0.40,-0.30>, 0.09, <0,0.55,-0.85>, 0 pigment { rgb <0.24,0.20,0.26> } }\n"
            "  scale 2.6 rotate y*SPIN translate <0, 0.1, -1.5> }\n";
    }
    s += "camera { location <0, 2.6, -9.5> look_at <0, 2.1, 0> angle 50 right x*16/9 up y }\n";
    return s;
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
        "// wilderness backdrop (bee_world lineage): gradient sky + clouds,\n"
        "// grassy ground, ring of early-CGI trees beyond the play area\n"
        "sky_sphere {\n"
        "  pigment { gradient y color_map {\n"
        "    [0.0 rgb <0.75,0.85,0.95>][0.25 rgb <0.45,0.65,0.92>][1.0 rgb <0.15,0.35,0.80>] }\n"
        "    scale 2 translate y*-0.2 }\n"
        "  pigment { bozo turbulence 0.5 scale <3,1,3> color_map {\n"
        "    [0.0 rgbt <1,1,1,1>][0.55 rgbt <1,1,1,1>][0.75 rgbt <1,1,1,0.1>][1.0 rgbt <1,1,1,0.4>] }\n"
        "    scale 6 }\n"
        "}\n"
        "light_source { <-30,40,-25> rgb <1.0,0.97,0.88> }\n"
        "light_source { <20,25,30> rgb <0.3,0.35,0.45> shadowless }\n"
        "plane { y,0 texture {\n"
        "  pigment { granite color_map {\n"
        "    [0 rgb <0.18,0.42,0.14>][0.6 rgb <0.26,0.55,0.18>][1 rgb <0.34,0.62,0.24>] } scale 3 }\n"
        "  normal { bumps 0.35 scale 0.4 }\n"
        "  finish { ambient 0.4 diffuse 0.7 specular 0 } } }\n"
        "#macro Tree(P, ts, kind)\n"
        "  union {\n"
        "    cylinder { <0,0,0>, <0,2.4,0>, 0.28 pigment { rgb <0.42,0.27,0.13> } finish { ambient 0.35 } }\n"
        "    #if (kind = 0)\n"
        "      cone { <0,1.8,0>,1.6, <0,3.6,0>,0 pigment { rgb <0.12,0.45,0.16> } finish { ambient 0.35 } }\n"
        "      cone { <0,3.0,0>,1.1, <0,4.6,0>,0 pigment { rgb <0.15,0.50,0.18> } finish { ambient 0.35 } }\n"
        "    #else\n"
        "      sphere { <0,3.6,0>,1.7 pigment { rgb <0.16,0.52,0.20> } finish { ambient 0.35 } }\n"
        "      sphere { <0.9,3.0,0.4>,1.1 pigment { rgb <0.14,0.48,0.18> } }\n"
        "    #end\n"
        "    scale ts translate P }\n"
        "#end\n"
        "#declare RS = seed(1942);\n"
        "#declare ti = 0;\n"
        "#while (ti < 10)\n"
        "  #local ang = ti*0.628 + rand(RS)*0.3;\n"
        "  #local rad = 18 + rand(RS)*8;\n"
        "  Tree(<sin(ang)*rad, 0, cos(ang)*rad>, 0.9+rand(RS)*0.7, (rand(RS) > 0.5))\n"
        "  #declare ti = ti+1;\n"
        "#end\n"
        "camera { location <POSX-sin(radians(TURN))*6.5, 3.4+JUMP*0.85,"
        " POSZ-cos(radians(TURN))*6.5>\n"
        "  look_at <POSX, 1.9+JUMP*0.85, POSZ> angle 54 right x*16/9 up y }\n";
    for (size_t i = 0; i < g_world.size(); ++i) {
        const Aabb& b = g_world[i];
        int n = 0;
        if (b.dyn && b.shape == SHAPE_BADDIE) {
            // a prowling critter: low body, head with snout, pointy ears,
            // glowing eyes, tail. BOX<i>R turns him to face his prey.
            float s = b.h / 0.9f;
            n = snprintf(buf, sizeof buf,
                "#ifndef (BOX%zuX) #declare BOX%zuX=%.2f; #end\n"
                "#ifndef (BOX%zuY) #declare BOX%zuY=%.2f; #end\n"
                "#ifndef (BOX%zuZ) #declare BOX%zuZ=%.2f; #end\n"
                "#ifndef (BOX%zuR) #declare BOX%zuR=0; #end\n"
                "union {\n"
                "  sphere { 0, %.2f scale <1.05,0.80,1.30> translate <0,%.2f,0>"
                "    pigment { rgb <0.28,0.24,0.30> } finish { phong 0.5 } }\n"
                "  sphere { 0, %.2f translate <0,%.2f,%.2f>"
                "    pigment { rgb <0.30,0.26,0.32> } finish { phong 0.5 } }\n"
                "  cone { <0,%.2f,%.2f>, %.2f, <0,%.2f,%.2f>, 0"
                "    pigment { rgb <0.22,0.19,0.24> } }\n"
                "  cone { <-%.2f,%.2f,%.2f>, %.2f, <-%.2f,%.2f,%.2f>, 0"
                "    pigment { rgb <0.26,0.22,0.28> } }\n"
                "  cone { <%.2f,%.2f,%.2f>, %.2f, <%.2f,%.2f,%.2f>, 0"
                "    pigment { rgb <0.26,0.22,0.28> } }\n"
                "  sphere { <-%.2f,%.2f,%.2f>, %.3f"
                "    pigment { rgb <1.0,0.85,0.25> } finish { ambient 0.9 } }\n"
                "  sphere { <%.2f,%.2f,%.2f>, %.3f"
                "    pigment { rgb <1.0,0.85,0.25> } finish { ambient 0.9 } }\n"
                "  cone { <0,%.2f,-%.2f>, %.2f, <0,%.2f,-%.2f>, 0"
                "    pigment { rgb <0.24,0.20,0.26> } }\n"
                "  rotate y*BOX%zuR translate <BOX%zuX,BOX%zuY,BOX%zuZ> }\n",
                i, i, b.cx, i, i, b.cy, i, i, b.cz, i, i,
                0.34f*s, 0.34f*s,                              // body
                0.21f*s, 0.58f*s, 0.34f*s,                     // head
                0.55f*s, 0.46f*s, 0.10f*s, 0.52f*s, 0.68f*s,   // snout
                0.12f*s, 0.74f*s, 0.26f*s, 0.07f*s,
                0.14f*s, 0.94f*s, 0.24f*s,                     // ear L
                0.12f*s, 0.74f*s, 0.26f*s, 0.07f*s,
                0.14f*s, 0.94f*s, 0.24f*s,                     // ear R
                0.09f*s, 0.64f*s, 0.50f*s, 0.035f*s,           // eye L
                0.09f*s, 0.64f*s, 0.50f*s, 0.035f*s,           // eye R
                0.30f*s, 0.40f*s, 0.09f*s, 0.55f*s, 0.85f*s,   // tail
                i, i, i, i);
        } else if (b.dyn && b.shape == SHAPE_HEART) {
            // health pickup: two lobes + a diamond point, glossy red
            float s = b.h / 0.5f;
            n = snprintf(buf, sizeof buf,
                "#ifndef (BOX%zuX) #declare BOX%zuX=%.2f; #end\n"
                "#ifndef (BOX%zuY) #declare BOX%zuY=%.2f; #end\n"
                "#ifndef (BOX%zuZ) #declare BOX%zuZ=%.2f; #end\n"
                "union {\n"
                "  sphere { <-%.2f,%.2f,0>, %.2f }\n"
                "  sphere { < %.2f,%.2f,0>, %.2f }\n"
                "  box { <-%.2f,-%.2f,-%.3f>,<%.2f,%.2f,%.3f> rotate z*45 translate <0,%.2f,0> }\n"
                "  pigment { rgb <0.92,0.15,0.20> } finish { phong 0.9 reflection 0.08 }\n"
                "  translate <BOX%zuX,BOX%zuY,BOX%zuZ> }\n",
                i, i, b.cx, i, i, b.cy, i, i, b.cz,
                0.105f*s, 0.34f*s, 0.115f*s,
                0.105f*s, 0.34f*s, 0.115f*s,
                0.13f*s, 0.13f*s, 0.055f*s, 0.13f*s, 0.13f*s, 0.055f*s, 0.215f*s,
                i, i, i);
        } else if (b.dyn && b.shape == SHAPE_STAR) {
            // power star: glowing core + six gold spikes, spins via BOX<i>R
            float s = b.h / 0.5f;
            n = snprintf(buf, sizeof buf,
                "#ifndef (BOX%zuX) #declare BOX%zuX=%.2f; #end\n"
                "#ifndef (BOX%zuY) #declare BOX%zuY=%.2f; #end\n"
                "#ifndef (BOX%zuZ) #declare BOX%zuZ=%.2f; #end\n"
                "#ifndef (BOX%zuR) #declare BOX%zuR=0; #end\n"
                "union {\n"
                "  sphere { <0,%.2f,0>, %.2f }\n"
                "  cone { <0,%.2f,0>, %.2f, <0,%.2f,0>, 0 }\n"
                "  cone { <0,%.2f,0>, %.2f, <0,%.2f,0>, 0 }\n"
                "  cone { <0,%.2f,0>, %.2f, <%.2f,%.2f,0>, 0 }\n"
                "  cone { <0,%.2f,0>, %.2f, <-%.2f,%.2f,0>, 0 }\n"
                "  cone { <0,%.2f,0>, %.2f, <0,%.2f,%.2f>, 0 }\n"
                "  cone { <0,%.2f,0>, %.2f, <0,%.2f,-%.2f>, 0 }\n"
                "  pigment { rgb <1.0,0.86,0.20> } finish { phong 1.0 ambient 0.55 }\n"
                "  rotate y*BOX%zuR translate <BOX%zuX,BOX%zuY,BOX%zuZ> }\n",
                i, i, b.cx, i, i, b.cy, i, i, b.cz, i, i,
                0.30f*s, 0.135f*s,
                0.30f*s, 0.075f*s, 0.62f*s,
                0.30f*s, 0.075f*s, 0.02f*s,
                0.30f*s, 0.075f*s, 0.30f*s, 0.30f*s,
                0.30f*s, 0.075f*s, 0.30f*s, 0.30f*s,
                0.30f*s, 0.075f*s, 0.30f*s, 0.30f*s,
                0.30f*s, 0.075f*s, 0.30f*s, 0.30f*s,
                i, i, i, i);
        } else if (b.dyn && b.shape == SHAPE_CHOMP) {
            // the leashed dog: big dark head, eager eyes, TEETH, chain links
            // trailing behind its facing (the post is back that way)
            float s = b.h / 0.9f;
            n = snprintf(buf, sizeof buf,
                "#ifndef (BOX%zuX) #declare BOX%zuX=%.2f; #end\n"
                "#ifndef (BOX%zuY) #declare BOX%zuY=%.2f; #end\n"
                "#ifndef (BOX%zuZ) #declare BOX%zuZ=%.2f; #end\n"
                "#ifndef (BOX%zuR) #declare BOX%zuR=0; #end\n"
                "union {\n"
                "  sphere { <0,%.2f,0>, %.2f pigment { rgb <0.13,0.13,0.18> } finish { phong 0.9 reflection 0.12 } }\n"
                "  sphere { <-%.2f,%.2f,%.2f>, %.2f pigment { rgb <0.95,0.95,0.92> } }\n"
                "  sphere { < %.2f,%.2f,%.2f>, %.2f pigment { rgb <0.95,0.95,0.92> } }\n"
                "  sphere { <-%.2f,%.2f,%.2f>, %.3f pigment { rgb <0.05,0.05,0.06> } }\n"
                "  sphere { < %.2f,%.2f,%.2f>, %.3f pigment { rgb <0.05,0.05,0.06> } }\n"
                "  cone { <-%.2f,%.2f,%.2f>, %.2f, <-%.2f,%.2f,%.2f>, 0 pigment { rgb <0.96,0.96,0.93> } }\n"
                "  cone { <0,%.2f,%.2f>, %.2f, <0,%.2f,%.2f>, 0 pigment { rgb <0.96,0.96,0.93> } }\n"
                "  cone { <%.2f,%.2f,%.2f>, %.2f, <%.2f,%.2f,%.2f>, 0 pigment { rgb <0.96,0.96,0.93> } }\n"
                "  sphere { <0,%.2f,-%.2f>, %.2f pigment { rgb <0.25,0.25,0.30> } finish { phong 0.8 } }\n"
                "  sphere { <0,%.2f,-%.2f>, %.2f pigment { rgb <0.25,0.25,0.30> } finish { phong 0.8 } }\n"
                "  sphere { <0,%.2f,-%.2f>, %.2f pigment { rgb <0.25,0.25,0.30> } finish { phong 0.8 } }\n"
                "  rotate y*BOX%zuR translate <BOX%zuX,BOX%zuY,BOX%zuZ> }\n",
                i, i, b.cx, i, i, b.cy, i, i, b.cz, i, i,
                0.45f*s, 0.42f*s,
                0.16f*s, 0.62f*s, 0.34f*s, 0.085f*s,
                0.16f*s, 0.62f*s, 0.34f*s, 0.085f*s,
                0.155f*s, 0.62f*s, 0.41f*s, 0.038f*s,
                0.155f*s, 0.62f*s, 0.41f*s, 0.038f*s,
                0.17f*s, 0.30f*s, 0.36f*s, 0.05f*s, 0.17f*s, 0.16f*s, 0.40f*s,
                0.30f*s, 0.40f*s, 0.05f*s, 0.16f*s, 0.44f*s,
                0.17f*s, 0.30f*s, 0.36f*s, 0.05f*s, 0.17f*s, 0.16f*s, 0.40f*s,
                0.22f*s, 0.52f*s, 0.07f*s,
                0.16f*s, 0.74f*s, 0.07f*s,
                0.10f*s, 0.96f*s, 0.07f*s,
                i, i, i, i);
        } else if (b.dyn && b.shape == SHAPE_ACORN) {
            // shape="acorn": nut, cap, stem — sized from the box height
            float s = b.h / 0.5f;
            n = snprintf(buf, sizeof buf,
                "#ifndef (BOX%zuX) #declare BOX%zuX=%.2f; #end\n"
                "#ifndef (BOX%zuY) #declare BOX%zuY=%.2f; #end\n"
                "#ifndef (BOX%zuZ) #declare BOX%zuZ=%.2f; #end\n"
                "union {\n"
                "  // scale-then-translate (POV scale is about the origin)\n"
                "  sphere { 0, %.2f scale <1,1.15,1> translate <0,%.2f,0>"
                "    pigment { rgb <%.2f,%.2f,0.25> } finish { phong 0.9 reflection 0.10 } }\n"
                "  sphere { 0, %.2f scale <1,0.55,1> translate <0,%.2f,0>"
                "    pigment { rgb <0.42,0.26,0.12> } finish { phong 0.5 } }\n"
                "  cylinder { <0,%.2f,0>, <0,%.2f,0>, %.3f"
                "    pigment { rgb <0.38,0.23,0.10> } }\n"
                "  translate <BOX%zuX,BOX%zuY,BOX%zuZ> }\n",
                i, i, b.cx, i, i, b.cy, i, i, b.cz,
                0.20f * s, 0.20f * s, b.r, b.g,
                0.21f * s, 0.36f * s,
                0.44f * s, 0.56f * s, 0.035f * s,
                i, i, i);
        } else if (b.dyn) {
            // dyn solid box: a moving/ROTATING platform — yaw via BOX<i>R,
            // exactly matching the rotated-collision local frame
            n = snprintf(buf, sizeof buf,
                "#ifndef (BOX%zuX) #declare BOX%zuX=%.2f; #end\n"
                "#ifndef (BOX%zuY) #declare BOX%zuY=%.2f; #end\n"
                "#ifndef (BOX%zuZ) #declare BOX%zuZ=%.2f; #end\n"
                "#ifndef (BOX%zuR) #declare BOX%zuR=0; #end\n"
                "box { <%.2f,0,%.2f>, <%.2f,%.2f,%.2f> pigment { rgb <%.2f,%.2f,0.30> }"
                " finish { phong 0.7 reflection 0.08 }"
                " rotate y*BOX%zuR translate <BOX%zuX,BOX%zuY,BOX%zuZ> }\n",
                i, i, b.cx, i, i, b.cy, i, i, b.cz, i, i,
                -b.hx, -b.hz, b.hx, b.h, b.hz, b.r, b.g, i, i, i, i);
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
        "#declare PAW = 0.11*sin(STEP);\n"
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
        "  sphere { <-0.12,1.72,0.305>, 0.068 pigment { rgb <0.97,0.97,0.95> } finish { phong 0.9 } }\n"
        "  sphere { < 0.12,1.72,0.305>, 0.068 pigment { rgb <0.97,0.97,0.95> } finish { phong 0.9 } }\n"
        "  sphere { <-0.115,1.72,0.355>, 0.034 pigment {DARK} }\n"
        "  sphere { < 0.115,1.72,0.355>, 0.034 pigment {DARK} }\n"
        "  sphere { 0, 0.09 scale <0.8,1.3,0.6> translate <-0.15,1.90,0.04> pigment {FUR} }\n"
        "  sphere { 0, 0.09 scale <0.8,1.3,0.6> translate < 0.15,1.90,0.04> pigment {FUR} }\n"
        "  // little front paws, held up in the classic nibble pose\n"
        "  sphere { <-0.47,0.86,0.18+PAW>, 0.10 pigment {FUR} finish { phong 0.5 } }\n"
        "  sphere { < 0.47,0.86,0.18-PAW>, 0.10 pigment {FUR} finish { phong 0.5 } }\n"
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
        p.x += sinf(p.yaw) * in.move * SPEED * g_speed_mult * dt;
        p.z += cosf(p.yaw) * in.move * SPEED * g_speed_mult * dt;
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
        p.jv = JUMP_V * g_jump_mult; p.grounded = false;
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
    char nm[24];
    for (size_t i = 0; i < g_world.size(); ++i) {
        if (!g_world[i].dyn) continue;
        snprintf(nm, sizeof nm, "BOX%zuX", i); d.push_back({nm, g_world[i].cx});
        snprintf(nm, sizeof nm, "BOX%zuY", i); d.push_back({nm, g_world[i].cy});
        snprintf(nm, sizeof nm, "BOX%zuZ", i); d.push_back({nm, g_world[i].cz});
        // anything that can rotate streams its yaw: baddies/chomps face their
        // prey, stars spin, and dyn SOLID boxes are rotating platforms
        if (g_world[i].shape == SHAPE_BADDIE || g_world[i].shape == SHAPE_CHOMP ||
            g_world[i].shape == SHAPE_STAR ||
            (g_world[i].shape == SHAPE_BOX && g_world[i].solid)) {
            snprintf(nm, sizeof nm, "BOX%zuR", i); d.push_back({nm, g_world[i].ry});
        }
    }
    return d;
}

static double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// swap the whole world live — fresh script, fresh player, new SCENE_FULL to
// the daemon (it re-parses next frame; this is what the protocol was built
// for). Used by the win chain, retry-on-loss, and the world-select keys.
static bool switch_level(FdRenderer& r, Player& p, const char* path) {
    char target[sizeof g_current];
    snprintf(target, sizeof target, "%s", path);   // path may alias g_next/current
    if (g_L) { lua_close(g_L); g_L = NULL; }
    g_hud = GameHud();
    if (!load_script(target)) {
        fprintf(stderr, "fd-game: level %s failed to load\n", target);
        default_world();
    }
    p = Player();
    if (!r.scene(build_scene())) {
        // old Lua state is gone and the daemon kept the previous scene —
        // rendering on would mix worlds. A broken level is a build error:
        // die loudly (same policy as scene-chunk truncation).
        fprintf(stderr, "fd-game: FATAL: level '%s' scene rejected by daemon\n", target);
        exit(1);
    }
    g_audio.play(FdAudio::BLIP, 1.2f);
    printf("fd-game: now playing -> %s\n", g_title);
    return true;
}

// the win chain: after the gold-tint linger, follow next_level
static const double WIN_LINGER_S = 2.5;
static bool maybe_advance(FdRenderer& r, Player& p, double simt, double* win_at) {
    if (strcmp(g_hud.state, "won") != 0 || !g_next[0]) {
        if (strcmp(g_hud.state, "won") != 0) *win_at = -1;
        return false;
    }
    if (*win_at < 0) { *win_at = simt; return false; }
    if (simt - *win_at < WIN_LINGER_S) return false;
    *win_at = -1;
    return switch_level(r, p, g_next);
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
    double t = 0, win_at = -1; uint64_t us = 0;
    int score_at_half = -1, levels = 0;
    float max_feet = 0;
    for (int i = 0; i < frames; ++i) {
        Input in; in.move = 1;                  // hold forward...
        in.jump = (i % 60 == 30);               // ...hopping as it goes
        if (maybe_advance(r, p, t, &win_at)) levels++;
        script_tick(t, 2 * dt, p);
        for (int k = 0; k < 2; ++k) { simulate(p, in, dt); in.jump = false; t += dt; }
        max_feet = fmaxf(max_feet, p.grounded ? p.jy : max_feet);
        if (!r.render(320, 180, declares_for(p))) return 1;
        us += r.frame_us;
        if (i == frames / 2) score_at_half = g_hud.score;
    }
    printf("fd-game gametest: %d frames, %.1f fps end-to-end\n",
           frames, 1e6 * frames / (double)us);
    printf("  score %d (half-way %d), lives %d, state %s, highest stand %.2f, "
           "levels advanced %d, title '%s'\n",
           g_hud.score, score_at_half, g_hud.lives, g_hud.state, max_feet,
           levels, g_title);
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

// pixel-art HUD stencils: each string row is a bitmap, drawn as scaled rects.
// Hearts for lives, mini acorns for acorns — squares are for engines, not HUDs.
static const char* HUD_HEART[] = {
    ".XX.XX.",
    "XXXXXXX",
    "XXXXXXX",
    ".XXXXX.",
    "..XXX..",
    "...X...",
    NULL
};
static const char* HUD_ACORN_CAP[] = {     // dark brown: stem + cap
    "...X...",
    ".XXXXX.",
    "XXXXXXX",
    ".......",
    ".......",
    ".......",
    NULL
};
static const char* HUD_ACORN_NUT[] = {     // gold: the nut
    ".......",
    ".......",
    ".......",
    ".XXXXX.",
    ".XXXXX.",
    "..XXX..",
    NULL
};

static void draw_stencil(SDL_Renderer* ren, const char** rows,
                         int x, int y, int px,
                         Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    SDL_SetRenderDrawColor(ren, r, g, b, a);
    for (int row = 0; rows[row]; ++row)
        for (int col = 0; rows[row][col]; ++col)
            if (rows[row][col] == 'X') {
                SDL_Rect dot = { x + col * px, y + row * px, px, px };
                SDL_RenderFillRect(ren, &dot);
            }
}

static int play(FdRenderer& r, int winW, int winH, int rdiv) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_Window* win = SDL_CreateWindow(g_title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winW, winH, SDL_WINDOW_SHOWN);
    // NO PRESENTVSYNC: under XWayland, Mesa's DRI3 vsync wait
    // (xcb_wait_for_special_event in loader_dri3_get_buffers) can block
    // FOREVER when the compositor stops delivering present events (window
    // occluded, screen blank, focus games) — captured live via gdb as the
    // "Chunkins frozen" bug. The daemon's ~10ms renders pace the loop anyway.
    // FD_VSYNC=1 opts back in for setups that want it.
    Uint32 rflags = SDL_RENDERER_ACCELERATED;
    const char* vs = getenv("FD_VSYNC");
    if (vs && strcmp(vs, "1") == 0) rflags |= SDL_RENDERER_PRESENTVSYNC;
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, rflags);
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
    double prev = now_s(), acc = 0, simt = 0, fpsT = prev, win_at = -1;
    int fpsN = 0, rc_out = 0;

    // ---- card machinery: send a card scene, spin it, wait for key/timeout --
    // returns 1 if the player asked to QUIT (ESC / window close), else 0
    auto show_card = [&](const std::string& card, double auto_dismiss_s) -> int {
        if (!r.scene(card)) return 0;            // cards are best-effort
        double t0 = now_s();
        for (;;) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) return 1;
                if (e.type == SDL_KEYDOWN && !e.key.repeat)
                    return e.key.keysym.sym == SDLK_ESCAPE ? 1 : 0;
            }
            if (auto_dismiss_s > 0 && now_s() - t0 >= auto_dismiss_s) return 0;
            std::vector<Declare> sd = { {"SPIN", (float)((now_s() - t0) * 24.0)} };
            if (!r.render(rW, rH, sd)) return 0;
            if (r.frame_fits(rW, rH)) {
                if (gpu && g_gpu.frame(r.fb.data(), 0.6f, 32, gpu_out.data()) == 0)
                    SDL_UpdateTexture(tex, NULL, gpu_out.data(), winW * 4);
                else if (!gpu)
                    SDL_UpdateTexture(tex, NULL, r.fb.data(), rW * 4);
                SDL_RenderClear(ren);
                SDL_RenderCopy(ren, tex, NULL, NULL);
                SDL_RenderPresent(ren);
            }
        }
    };
    // restore the live world after any card + keep the clocks honest
    auto back_to_world = [&]() -> bool {
        if (!r.scene(build_scene())) {
            fprintf(stderr, "fd-game: FATAL: world scene rejected after card\n");
            exit(1);
        }
        if (g_gpu.active && g_gpu.reset) g_gpu.reset();
        prev = now_s(); acc = 0;
        return true;
    };
    // the level card: "WORLD N" + the level's name (auto-dismisses)
    auto level_card = [&]() -> int {
        char big[72], sub[80];
        const char* name = strchr(g_title, ':');
        if (name) { name++; while (*name == ' ') name++; }   // safe past ':'
        snprintf(sub, sizeof sub, "%s", name ? name : "");
        if (g_hud.world > 0) snprintf(big, sizeof big, "WORLD %d", g_hud.world);
        else snprintf(big, sizeof big, "%s", (name && *name) ? "ONWARD!" : g_title);
        int q = show_card(build_card_scene(big, sub, CARD_GOLD), 2.2);
        return back_to_world(), q;
    };

    // ---- splash screen: the raytraced title card (any key starts) ----------
    if (!getenv("FD_NOSPLASH")) {
        // cwd first (matches how level scripts load), exe-dir as fallback so
        // launching from elsewhere still gets the title card (tri-brain)
        FILE* sf = fopen("splash.pov", "rb");
        if (!sf) {
            char exe[448];
            ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
            if (n > 0) {
                exe[n] = 0;
                char* slash = strrchr(exe, '/');
                if (slash) *slash = 0;
                char alt[512];
                snprintf(alt, sizeof alt, "%s/splash.pov", exe);
                sf = fopen(alt, "rb");
            }
        }
        if (sf) {
            std::string splash;
            char chunk[4096]; size_t got;
            while ((got = fread(chunk, 1, sizeof chunk, sf)) > 0)
                splash.append(chunk, got);
            fclose(sf);
            SDL_SetWindowTitle(win, "CHUNKINS — The Search for the Golden Acorn");
            if (show_card(splash, 0)) running = false;       // wait for any key
        }
        if (running && level_card()) running = false;        // "WORLD 1 ..." card
        if (running) back_to_world();
    }
    printf("fd-game: WASD/arrows move+turn, SPACE jump (retry when lost), "
           "1-%zu pick a world, ESC quit\n", g_worlds.empty() ? 9 : g_worlds.size());
    while (running) {
        double now = now_s(), ft = now - prev; prev = now;
        if (ft > 0.25) ft = 0.25;
        acc += ft;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                // !repeat: a held digit key must not reload the level 30x/s
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE) running = false;
                else if (k == SDLK_SPACE) {
                    if (strcmp(g_hud.state, "lost") == 0) {   // RETRY this level
                        if (switch_level(r, p, g_current)) {
                            win_at = -1;
                            if (g_gpu.active && g_gpu.reset) g_gpu.reset();
                        }
                    } else jump_pending = true;
                }
                else if (k >= SDLK_1 && k <= SDLK_9) {        // world select
                    size_t w = (size_t)(k - SDLK_1);
                    if (w < g_worlds.size() &&
                        switch_level(r, p, g_worlds[w].c_str())) {
                        win_at = -1;
                        if (level_card()) running = false;
                    }
                }
            }
        }
        const Uint8* k = SDL_GetKeyboardState(NULL);
        Input in;
        if (k[SDL_SCANCODE_UP]    || k[SDL_SCANCODE_W]) in.move += 1;
        if (k[SDL_SCANCODE_DOWN]  || k[SDL_SCANCODE_S]) in.move -= 1;
        if (k[SDL_SCANCODE_LEFT]  || k[SDL_SCANCODE_A]) in.turn -= 1;
        if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) in.turn += 1;

        if (maybe_advance(r, p, simt, &win_at)) {
            if (level_card()) running = false;   // "WORLD N" + fresh GPU history
        }
        script_tick(simt, ft, p);
        // evaluate AFTER the tick: a state change this frame freezes this frame
        bool over = strcmp(g_hud.state, "playing") != 0;

        // GAME OVER card: a beat after the bonk so the red tint reads, then
        // the smug baddie card waits for any key -> retry this level
        static double lost_at = -1;
        if (strcmp(g_hud.state, "lost") == 0) {
            if (lost_at < 0) lost_at = simt;
            else if (simt - lost_at > 1.0) {
                lost_at = -1;
                if (show_card(build_card_scene("GAME OVER",
                        "Press any key to try again", CARD_DARK), 0)) {
                    running = false;
                } else if (switch_level(r, p, g_current)) {
                    win_at = -1;
                    back_to_world();
                }
                continue;
            }
        } else lost_at = -1;

        // QUEST COMPLETE card: final world won (no next_level to chain to)
        static double done_at = -1;
        if (strcmp(g_hud.state, "won") == 0 && !g_next[0]) {
            if (done_at < 0) done_at = simt;
            else if (simt - done_at > WIN_LINGER_S) {
                done_at = -1;
                char sub[96];
                snprintf(sub, sizeof sub, "Chunkins found the Golden Acorn!");
                if (show_card(build_card_scene("QUEST COMPLETE!", sub, CARD_GOLD), 0)) {
                    running = false;
                } else {
                    const char* home = g_worlds.empty() ? g_current
                                                        : g_worlds[0].c_str();
                    if (switch_level(r, p, home)) {
                        win_at = -1;
                        if (level_card()) running = false;
                    }
                }
                continue;
            }
        } else done_at = -1;
        while (acc >= dt) {
            in.jump = jump_pending; jump_pending = false;   // consumed by one step only
            if (!over) simulate(p, in, dt);                 // end screen: world freezes
            acc -= dt; simt += dt;
        }

        if (!r.render(rW, rH, declares_for(p))) {
            // daemon hiccup (or death + supervisor restart): reconnect and
            // resend the world instead of freezing or quitting. Dead daemon:
            // ~2s (connect fails instantly). Hung-but-alive daemon: up to
            // ~4 x (2s+2s) socket timeouts ≈ 18s worst case before handoff.
            fprintf(stderr, "fd-game: render failed — reconnecting...\n");
            bool back = false;
            for (int tries = 0; tries < 4 && !back; ++tries) {
                SDL_Delay(500);
                back = r.reconnect();
            }
            if (!back) {
                // exit NONZERO: the supervisor restarts the daemon+game pair.
                // rc 0 would read as a clean ESC and stop supervision (drilled)
                fprintf(stderr, "fd-game: daemon gone — handing to supervisor\n");
                rc_out = 2; running = false; break;
            }
            fprintf(stderr, "fd-game: reconnected\n");
            if (g_gpu.active && g_gpu.reset) g_gpu.reset();
            continue;
        }
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

            // HUD: mini acorns for score (top-left), hearts for lives
            // (top-right), sky-blue world pips (bottom-left). px=3 => 21x18.
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            for (int s = 0; s < g_hud.score && s < 32; ++s) {
                int x = 14 + s * 26;
                draw_stencil(ren, HUD_ACORN_CAP, x, 12, 3, 92, 58, 30, 245);
                draw_stencil(ren, HUD_ACORN_NUT, x, 12, 3, 240, 195, 70, 245);
            }
            for (int l = 0; l < g_hud.lives && l < 32; ++l) {
                draw_stencil(ren, HUD_HEART, winW - 36 - l * 26, 13, 3,
                             225, 55, 65, 245);
            }
            SDL_Rect pip;
            for (int w = 0; w < g_hud.world && w < 16; ++w) {
                pip = { 16 + w * 26, winH - 34, 18, 18 };
                SDL_SetRenderDrawColor(ren, 90, 170, 235, 235);
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
                snprintf(ti, sizeof ti, "%s — YOU WIN!  score %d  (1-%zu pick a world, ESC quit)",
                         g_title, g_hud.score, g_worlds.empty() ? 9 : g_worlds.size());
            else if (strcmp(g_hud.state, "lost") == 0)
                snprintf(ti, sizeof ti, "%s — ouch! score %d  (SPACE to retry!)",
                         g_title, g_hud.score);
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
    return rc_out;
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);   // death notes must reach the log
    bool st = argc > 1 && strcmp(argv[1], "--selftest") == 0;
    bool gt = argc > 1 && strcmp(argv[1], "--gametest") == 0;
    bool headless = st || gt;
    // 180 default: the walk phase must REACH the wall for the bump assertion
    // (90 frames ends 1.2 units short — tri-brain caught the impossible default)
    int frames = headless && argc > 2 ? atoi(argv[2]) : 180;
    const char* sock   = headless ? (argc > 3 ? argv[3] : "/tmp/feverdream.sock")
                                  : (argc > 1 ? argv[1] : "/tmp/feverdream.sock");
    const char* script = headless ? (argc > 4 ? argv[4] : (gt ? "relic_sweep.lua" : "arena.lua"))
                                  : (argc > 5 ? argv[5] : "chunkins1.lua");
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
