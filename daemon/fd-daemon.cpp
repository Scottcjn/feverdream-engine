// SPDX-License-Identifier: AGPL-3.0-or-later
// fd-daemon.cpp -- Feverdream Engine: the resident POV-Ray render daemon.
//
// This binary links libvfe+libpovray, so it is a derivative work of POV-Ray
// (AGPLv3) and is licensed accordingly -- it is the AGPL side of the firewall
// described in ../GAME_ENGINE.md §4. Game processes stay OUT of this binary:
// they speak the open wire protocol in PROTOCOL.md over a Unix socket. The
// wire format is plain POV SDL text plus generic name=float declares -- the
// same inputs any POV-Ray user feeds the stock binary.
//
//   fd-daemon [socket_path] [libdir]
//     socket_path  default /tmp/feverdream.sock
//     libdir       default /usr/share/povray-3.7/include
//
// One client at a time (PROTOCOL.md). Engine initialized ONCE; every RENDER
// reuses the live session (the whole point -- see ../FINDINGS.md).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cctype>
#include <vector>
#include <string>
#include <chrono>

#include "fd_listen.h"          // cross-platform listen-side socket shim
#ifdef _WIN32
#include <io.h>                 // _open/_close for exclusive temp-file create
#include <fcntl.h>              // _O_CREAT|_O_EXCL
#include <sys/stat.h>           // _S_IREAD|_S_IWRITE
#else
#include <unistd.h>
#include <signal.h>
#endif

#include "vfe.h"
#include "vfeplatform.h"

using namespace vfe;
using namespace vfePlatform;
using namespace pov_frontend;

// The headless render session: POV's platform front-end subclass. vfe ships a
// Unix and a Windows implementation; pick the one for this build.
#ifdef _WIN32
  typedef vfePlatform::vfeWinSession  FdSession;
#else
  typedef vfePlatform::vfeUnixSession FdSession;
#endif

// ---- wire protocol (PROTOCOL.md) -------------------------------------------
static const uint8_t  FD_MAGIC      = 0xFD;
static const uint8_t  FD_VERSION    = 0x00;
static const uint8_t  T_SCENE_FULL  = 0x01;
static const uint8_t  T_RENDER      = 0x02;
static const uint8_t  T_PING        = 0x7E;
static const uint8_t  T_SHUTDOWN    = 0x7F;
static const uint8_t  T_FRAME       = 0x81;  // reply to RENDER
static const uint8_t  T_SCENE_ACK   = 0x82;  // reply to SCENE_FULL
static const uint8_t  T_ERROR       = 0xEE;  // reply: u32 code + utf8 message
static const uint8_t  FLAG_WANT_FB  = 0x01;  // RENDER flags bit0
static const uint32_t MAX_SCENE     = 32u * 1024 * 1024;
// The declare list is bounded by the wire format itself: ndecl is a u8, and a
// name that survives valid_declare_name() is at most MAX_DECL_NAME bytes. So
// derive the RENDER cap from those limits instead of hard-coding one — a flat
// 4096 cut PROTOCOL.md's own maximum message (9445 bytes) off at ~110 declares.
static const uint32_t MAX_DECLARES  = 255;                 // ndecl is u8
static const uint32_t MAX_DECL_NAME = 32;                  // valid_declare_name()
static const uint32_t RENDER_FIXED  = 10;                  // u16 w,u16 h,f32 clock,u8 aa,u8 ndecl
static const uint32_t MAX_RENDER    = RENDER_FIXED + MAX_DECLARES * (1 + MAX_DECL_NAME + 4);
static const int      MIN_DIM = 16, MAX_DIM = 4096;

// error codes for T_ERROR
enum { E_BAD_PAYLOAD = 1, E_NO_SCENE = 2, E_RENDER_FAIL = 3, E_BAD_DECLARE = 4 };

// ---- framebuffer the raytracer draws into (RGBA8) --------------------------
static std::vector<unsigned char> g_fb;
static int g_w = 0, g_h = 0;

static inline void fb_put(unsigned x, unsigned y, const Display::RGBA8& c)
{
    if ((int)x >= g_w || (int)y >= g_h) return;
    unsigned char* p = &g_fb[(y * (size_t)g_w + x) * 4];
    p[0] = c.red; p[1] = c.green; p[2] = c.blue; p[3] = 255;
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

// ---- socket I/O: full reads/writes, partial-read safe ----------------------
// Returns: >0 success, 0 on clean EOF (disconnect), -1 on timeout/error.
// Caller should distinguish: -1 means the socket timed out (EAGAIN), 0 means
// the peer closed the connection cleanly.
static long read_full(fd_sock_t fd, void* buf, size_t n)
{
    unsigned char* p = (unsigned char*)buf;
    while (n) {
        long r = (long)fd_sock_read(fd, p, n);
        if (r == 0) return 0;              // clean EOF
        if (r < 0) {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK) return -1;
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK
                || errno == ETIMEDOUT) return -1;
#endif
            return -1;                      // other error treated as timeout
        }
        p += r; n -= (size_t)r;
    }
    return 1;
}

static bool write_full(fd_sock_t fd, const void* buf, size_t n)
{
    const unsigned char* p = (const unsigned char*)buf;
    while (n) {
        long r = (long)fd_sock_write(fd, p, n);
        if (r <= 0) return false;
        p += r; n -= (size_t)r;
    }
    return true;
}

static bool send_msg(fd_sock_t fd, uint8_t type, uint8_t flags,
                     const void* payload, uint32_t len)
{
    uint8_t hdr[8] = { FD_MAGIC, FD_VERSION, type, flags,
                       (uint8_t)(len), (uint8_t)(len >> 8),
                       (uint8_t)(len >> 16), (uint8_t)(len >> 24) };
    if (!write_full(fd, hdr, 8)) return false;
    return len == 0 || write_full(fd, payload, len);
}

static bool send_error(fd_sock_t fd, uint32_t code, const char* msg)
{
    std::vector<uint8_t> p(4 + strlen(msg));
    memcpy(p.data(), &code, 4);
    memcpy(p.data() + 4, msg, strlen(msg));
    return send_msg(fd, T_ERROR, 0, p.data(), (uint32_t)p.size());
}

// ---- little-endian payload readers ------------------------------------------
static uint16_t rd_u16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
static float    rd_f32(const uint8_t* p) { float f; memcpy(&f, p, 4); return f; }

// declare names are spliced into POV option strings -- whitelist strictly so a
// client can't inject renderer options (Output_to_File etc.) through a name
static bool valid_declare_name(const std::string& s)
{
    if (s.empty() || s.size() > MAX_DECL_NAME) return false;
    if (!isalpha((unsigned char)s[0]) && s[0] != '_') return false;
    for (char c : s)
        if (!isalnum((unsigned char)c) && c != '_') return false;
    return true;
}

// ---- render one frame against the live session ------------------------------
struct Declare { std::string name; float value; };

static double now_ms()
{
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

static bool render_frame(FdSession* s, const std::string& scene,
                         const std::string& libdir, int w, int h, float clock,
                         bool aa, const std::vector<Declare>& decls,
                         uint32_t* frame_time_us)
{
    vfeRenderOptions opts;
    const char* tc = getenv("FD_THREADS");
    opts.SetThreadCount(tc ? atoi(tc) : 8);   // 8 = sweet spot, see FINDINGS.md §4
    if (!libdir.empty()) opts.AddLibraryPath(libdir);
    opts.SetSourceFile(scene);
    char b[96];
    snprintf(b, sizeof b, "Width=%d", w);        opts.AddCommand(b);
    snprintf(b, sizeof b, "Height=%d", h);       opts.AddCommand(b);
    snprintf(b, sizeof b, "Clock=%.6f", clock);  opts.AddCommand(b);
    for (const Declare& d : decls) {
        snprintf(b, sizeof b, "Declare=%s=%.6f", d.name.c_str(), d.value);
        opts.AddCommand(b);
    }
    opts.AddCommand(aa ? "Antialias=on" : "Antialias=off");
    opts.AddCommand("Output_to_File=off");
    opts.AddCommand("Display=on");
    opts.AddCommand("Verbose=off");
    opts.AddCommand("Pause_When_Done=off");

    double t0 = now_ms();
    if (s->SetOptions(opts) != vfeNoError) return false;
    if (s->StartRender() != vfeNoError)    return false;
    while ((s->GetStatus(true, 1) & stRenderShutdown) == 0) ;
    *frame_time_us = (uint32_t)((now_ms() - t0) * 1000.0);
    return true;
}

// ---- per-client message loop; returns false when the daemon should exit -----
static bool serve_client(fd_sock_t cfd, FdSession* session,
                         const std::string& scene_path, const std::string& libdir)
{
    // Read timeout: configurable via FD_READ_TIMEOUT env var (seconds, default 5).
    // If setsockopt(SO_RCVTIMEO) fails, log a warning but continue — the socket
    // keeps its prior timeout (often infinite), which is less safe but not a
    // crash.
    const char* timeout_env = getenv("FD_READ_TIMEOUT");
    unsigned read_timeout = timeout_env ? (unsigned)atol(timeout_env) : 5u;
    if (read_timeout > 0) {
        int ret = fd_sock_set_read_timeout(cfd, read_timeout);
        if (ret != 0) {
            fprintf(stderr, "fd-daemon: warning: SO_RCVTIMEO failed (%d) — "
                            "client reads have no timeout\n", ret);
        }
    }
    bool have_scene = false;
    std::vector<uint8_t> payload;

    for (;;) {
        uint8_t hdr[8];
        long rr = read_full(cfd, hdr, 8);
        if (rr < 0) {
            // Timeout (EAGAIN/EWOULDBLOCK/ETIMEDOUT) — partial client: drop.
            // Do NOT log per-client per-timeout to avoid log floods on slow
            // connections; the 5s default is tight enough that a genuine client
            // should not hit it between protocol chunks.
            return true;
        }
        if (rr == 0) return true;  // clean EOF: client disconnected
        uint32_t len = (uint32_t)hdr[4] | hdr[5] << 8 | hdr[6] << 16 | (uint32_t)hdr[7] << 24;
        if (hdr[0] != FD_MAGIC || hdr[1] != FD_VERSION) return true;   // poisoned stream: drop
        uint8_t type = hdr[2], flags = hdr[3];
        uint32_t cap = (type == T_SCENE_FULL) ? MAX_SCENE : MAX_RENDER;
        if (len > cap) {
            // Bounded length: we still refuse to read the body, but say so
            // before hanging up — a silent close leaves the client blocked in
            // recv() until its own timeout with no idea what it did wrong.
            send_error(cfd, E_BAD_PAYLOAD, "payload exceeds cap for this type");
            return true;
        }
        payload.resize(len);
        if (len) {
            long pr = read_full(cfd, payload.data(), len);
            if (pr <= 0) return true;
        }

        switch (type) {
        case T_PING:
            if (!send_msg(cfd, T_PING, 0, NULL, 0)) return true;
            break;

        case T_SHUTDOWN:
            return false;

        case T_SCENE_FULL: {
            if (len == 0) { send_error(cfd, E_BAD_PAYLOAD, "empty scene"); break; }
            FILE* f = fopen(scene_path.c_str(), "wb");
            uint32_t status = 1;
            if (f) {
                status = (fwrite(payload.data(), 1, len, f) == len) ? 0 : 1;
                fclose(f);
            }
            have_scene = (status == 0);
            if (!send_msg(cfd, T_SCENE_ACK, 0, &status, 4)) return true;
            break;
        }

        case T_RENDER: {
            // u16 w, u16 h, f32 clock, u8 aa, u8 ndecl, then ndecl x
            // (u8 namelen, name, f32 value)
            if (len < RENDER_FIXED) { send_error(cfd, E_BAD_PAYLOAD, "short RENDER"); break; }
            if (!have_scene) { send_error(cfd, E_NO_SCENE, "no scene loaded"); break; }
            const uint8_t* p = payload.data();
            int w = rd_u16(p), h = rd_u16(p + 2);
            float clk = rd_f32(p + 4);
            bool aa = p[8] != 0;
            unsigned ndecl = p[9];
            if (w < MIN_DIM || w > MAX_DIM || h < MIN_DIM || h > MAX_DIM) {
                send_error(cfd, E_BAD_PAYLOAD, "bad dimensions"); break;
            }
            std::vector<Declare> decls;
            size_t off = RENDER_FIXED; bool ok = true;
            for (unsigned i = 0; i < ndecl && ok; ++i) {
                if (off + 1 > len) { ok = false; break; }
                unsigned nl = p[off++];
                if (off + nl + 4 > len) { ok = false; break; }
                Declare d;
                d.name.assign((const char*)p + off, nl); off += nl;
                d.value = rd_f32(p + off); off += 4;
                if (!valid_declare_name(d.name)) { ok = false; break; }
                decls.push_back(d);
            }
            // The declare list must consume the payload exactly. Without this,
            // an ndecl that disagrees with the bytes actually sent (a client
            // whose count overflowed the u8 field, or a truncated list) is
            // rendered as a normal frame with the surplus declares silently
            // dropped -- the animation channel goes dead and nobody is told.
            if (ok && off != len) { ok = false; }
            if (!ok) { send_error(cfd, E_BAD_DECLARE, "bad declare list"); break; }

            uint32_t us = 0;
            if (!render_frame(session, scene_path, libdir, w, h, clk, aa, decls, &us)) {
                send_error(cfd, E_RENDER_FAIL, session->GetErrorString());
                break;
            }
            // reply: u32 w, u32 h, u32 frame_time_us [, RGBA8 fb if requested]
            bool want_fb = (flags & FLAG_WANT_FB) && g_w == w && g_h == h;
            uint32_t base = 12, fbn = want_fb ? (uint32_t)g_fb.size() : 0;
            std::vector<uint8_t> r(base + fbn);
            uint32_t uw = (uint32_t)g_w, uh = (uint32_t)g_h;
            memcpy(r.data(),     &uw, 4);
            memcpy(r.data() + 4, &uh, 4);
            memcpy(r.data() + 8, &us, 4);
            if (fbn) memcpy(r.data() + base, g_fb.data(), fbn);
            if (!send_msg(cfd, T_FRAME, want_fb ? FLAG_WANT_FB : 0, r.data(), base + fbn))
                return true;
            break;
        }

        default:
            send_error(cfd, E_BAD_PAYLOAD, "unknown type");
            break;
        }
    }
}

int main(int argc, char** argv)
{
    std::string sock_path = (argc > 1) ? argv[1] : "/tmp/feverdream.sock";
    std::string libdir    = (argc > 2) ? argv[2] : "/usr/share/povray-3.7/include";

#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);   // POSIX: a dead client must not kill us
#endif
    if (fd_net_startup() != 0) {            // WSAStartup on Windows; no-op on POSIX
        fprintf(stderr, "fd-daemon: socket subsystem init failed\n");
        return 1;
    }

    // Temp scene file: SCENE_FULL rewrites it, every RENDER re-parses it.
    char scene_path[260];
#ifdef _WIN32
    // Exclusive create with an unpredictable name — the Windows analog of the
    // POSIX mkstemps below: _O_EXCL fails if the path already exists, so a
    // pre-planted file/link can't redirect POV's scene writes (tri-brain: Codex).
    char tmpdir[MAX_PATH];
    DWORD tn = GetTempPathA(sizeof tmpdir, tmpdir);
    if (tn == 0 || tn + 24 >= sizeof scene_path) { fprintf(stderr, "fd-daemon: temp path too long\n"); return 1; }
    int scene_fd = -1;
    for (int tries = 0; tries < 64 && scene_fd < 0; ++tries) {
        unsigned rnd = (unsigned)GetCurrentProcessId() ^ (GetTickCount() << 8) ^ ((unsigned)tries * 2654435761u);
        snprintf(scene_path, sizeof scene_path, "%sfd-scene-%08x.pov", tmpdir, rnd);
        scene_fd = _open(scene_path, _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);
    }
    if (scene_fd < 0) { fprintf(stderr, "fd-daemon: could not create temp scene file\n"); return 1; }
    _close(scene_fd);   // POV opens by path; the name + exclusive create are what matter
#else
    // mkstemps gives an unpredictable name created O_EXCL — a pre-planted
    // symlink at a guessable path can't redirect our writes (tri-brain review)
    snprintf(scene_path, sizeof scene_path, "%s/fd-scene-XXXXXX.pov",
             access("/dev/shm", W_OK) == 0 ? "/dev/shm" : "/tmp");
    int scene_fd = mkstemps(scene_path, 4);
    if (scene_fd < 0) { perror("mkstemps"); return 1; }
    close(scene_fd);   // POV opens by path; the name + 0600 mode are what matter
#endif

#ifdef _WIN32
    FdSession* session = new FdSession(0);   // vfeWinSession takes a session id
#else
    FdSession* session = new FdSession();
#endif
    if (session->Initialize(NULL, NULL) != vfeNoError) {
        fprintf(stderr, "fd-daemon: session init failed: %s\n", session->GetErrorString());
        return 1;
    }
    session->SetDisplayCreator(CreateCaptureDisplay);

    // AF_UNIX socket (POSIX) / TCP loopback (Windows) — see fd_listen.h. The
    // non-socket-refusal + unlink + 0600 safety lives in the POSIX branch there.
    fd_sock_t sfd = fd_listen(sock_path.c_str());
    if (!fd_sock_valid(sfd)) {               // clean up the temp scene file we just made
        session->Shutdown(); delete session;
        remove(scene_path);
        fd_net_cleanup();
        return 1;
    }

#ifdef _WIN32
    // fd_listen already logged the resolved "listening on 127.0.0.1:<port>";
    // sock_path here is the raw arg (a placeholder path by default), not the
    // real endpoint — so don't restate it.
    printf("fd-daemon: engine resident (scene buffer %s)\n", scene_path);
#else
    // single combined banner — matches the pre-Windows Linux output exactly
    printf("fd-daemon: engine resident, listening on %s (scene buffer %s)\n",
           sock_path.c_str(), scene_path);
#endif
    fflush(stdout);

    bool run = true;
    while (run) {
        fd_sock_t cfd = fd_accept(sfd);
        if (!fd_sock_valid(cfd)) {
#ifndef _WIN32
            if (errno == EINTR) continue;   // signal, not a real failure
            perror("fd-daemon: accept");
#else
            fprintf(stderr, "fd-daemon: accept failed (WSA %d)\n", WSAGetLastError());
#endif
            break;
        }
        run = serve_client(cfd, session, scene_path, libdir);
        fd_sock_close(cfd);
    }

    printf("fd-daemon: shutdown\n");
    session->Shutdown(); delete session;
    fd_sock_close(sfd);
    fd_listen_cleanup(sock_path.c_str());   // unlink AF_UNIX file (POSIX); no-op on Win
    remove(scene_path);                     // portable temp-file removal
    fd_net_cleanup();
    return 0;
}
