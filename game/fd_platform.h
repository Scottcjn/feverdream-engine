// SPDX-License-Identifier: MIT
// fd_platform.h — the cross-platform socket shim for fd-game.
//
// Why this exists: fd-game speaks the PROTOCOL.md framing to the resident
// renderer over a stream socket. On POSIX that socket is an AF_UNIX domain
// socket at a filesystem path (e.g. /tmp/feverdream.sock) — the Linux build
// and the POV-Ray daemon agree on it out of the box. Windows has no such
// shared /tmp path, and AF_UNIX there needs a Win10-1803 floor and careful
// socket-file cleanup, so on Windows we dial TCP loopback (127.0.0.1:PORT,
// FD_PORT or the default below) instead. The (future, MSVC-built) Windows
// daemon mirrors this transport.
//
// The POSIX path is byte-for-byte the same syscalls fd-game always used —
// this header adds Windows support WITHOUT changing Linux behavior. Keep it
// that way: anything POSIX-specific stays inside #ifndef _WIN32.

#ifndef FD_PLATFORM_H
#define FD_PLATFORM_H

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <climits>

// Loopback port for the Windows transport; overridable with FD_PORT.
#define FD_DEFAULT_PORT 47999

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET fd_sock_t;
  #define FD_BAD_SOCK INVALID_SOCKET

  static inline int  fd_net_startup(void) { WSADATA w; return WSAStartup(MAKEWORD(2, 2), &w); }
  static inline void fd_net_cleanup(void) { WSACleanup(); }
  static inline bool fd_sock_valid(fd_sock_t s) { return s != INVALID_SOCKET; }
  static inline int  fd_sock_close(fd_sock_t s) { return closesocket(s); }
  // Winsock send/recv take a signed int length; clamp to INT_MAX so a >2GB
  // buffer can't be truncated to a negative/garbage size. io_full() loops on
  // the returned count, so a clamped short transfer is resumed correctly.
  static inline int  fd_sock_send(fd_sock_t s, const void* b, size_t n) {
      if (n > (size_t)INT_MAX) n = INT_MAX;
      return ::send(s, (const char*)b, (int)n, 0);
  }
  static inline int  fd_sock_recv(fd_sock_t s, void* b, size_t n) {
      if (n > (size_t)INT_MAX) n = INT_MAX;
      return ::recv(s, (char*)b, (int)n, 0);
  }

  // Create a stream socket, arm a 2s rcv/snd timeout (a hung daemon must FAIL
  // the call, never freeze the window), and connect to 127.0.0.1:<port>. The
  // `path` argument is interpreted as a port if numeric, else FD_PORT, else
  // FD_DEFAULT_PORT — so the same connect_to(path) call works on both OSes.
  // Directory containing the running executable (no trailing separator), so
  // bundled assets (splash.pov, levels) resolve when launched from elsewhere.
  static inline bool fd_exe_dir(char* out, size_t n) {
      DWORD got = GetModuleFileNameA(NULL, out, (DWORD)n);
      if (got == 0 || got >= n) return false;
      char* slash = strrchr(out, '\\');
      if (!slash) slash = strrchr(out, '/');
      if (slash) *slash = '\0';
      return true;
  }

  // Parse a port string; returns 0 (invalid) unless it's a bare 1..65535 —
  // so a bad FD_PORT or path arg can't silently wrap to an unintended service.
  static inline unsigned fd_parse_port(const char* s) {
      if (!s || !*s) return 0;
      char* end = NULL;
      unsigned long p = strtoul(s, &end, 10);
      return (end && *end == '\0' && p > 0 && p < 65536) ? (unsigned)p : 0;
  }

  static inline fd_sock_t fd_sock_dial(const char* path) {
      fd_sock_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (s == INVALID_SOCKET) return FD_BAD_SOCK;

      DWORD tv = 2000;  // ms — bounds the framed send/recv after connect; Win
                        // SO_RCVTIMEO/SNDTIMEO take a DWORD, not a timeval
      if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) != 0 ||
          setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv) != 0)
          fprintf(stderr, "fd-game: WARNING socket timeouts not set (WSA %d) — "
                  "a daemon hang could freeze the window\n", WSAGetLastError());

      // port selection. A `path` whose first char is a digit is an EXPLICIT
      // port (run.bat passes the port here); if it's out of range, fail loudly
      // rather than silently dialing some other local service. A non-numeric
      // path (the default "/tmp/feverdream.sock" sock arg) means "no port
      // given" → fall to FD_PORT, then the default.
      unsigned port;
      if (path && *path >= '0' && *path <= '9') {
          port = fd_parse_port(path);
          if (!port) {
              fprintf(stderr, "fd-game: invalid port '%s' (must be 1..65535)\n", path);
              closesocket(s);
              return FD_BAD_SOCK;
          }
      } else {
          port = fd_parse_port(getenv("FD_PORT"));   // 0 if unset/invalid
          if (!port) port = FD_DEFAULT_PORT;
      }

      struct sockaddr_in a; memset(&a, 0, sizeof a);
      a.sin_family = AF_INET;
      a.sin_port   = htons((unsigned short)port);
      a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

      // A plain blocking connect is safe here BECAUSE the target is hardcoded
      // 127.0.0.1: loopback connects are resolved synchronously in-kernel —
      // a dead port returns WSAECONNREFUSED at once, a live one completes at
      // once. There is no network path to a SYN black-hole, so connect()
      // cannot stall the window (SO_*TIMEO above still bounds the framed I/O
      // against a daemon that accepts then hangs). This mirrors the POSIX
      // AF_UNIX path, which is likewise a local socket that fails fast.
      if (connect(s, (struct sockaddr*)&a, sizeof a) != 0) {
          closesocket(s);
          return FD_BAD_SOCK;
      }
      return s;
  }

#else  // ---------------------------------------------------------------- POSIX
  #include <unistd.h>
  #include <sys/socket.h>
  #include <sys/un.h>
  #include <sys/time.h>
  #include <cerrno>
  typedef int fd_sock_t;
  #define FD_BAD_SOCK (-1)

  static inline int  fd_net_startup(void) { return 0; }
  static inline void fd_net_cleanup(void) {}
  static inline bool fd_sock_valid(fd_sock_t s) { return s >= 0; }
  static inline int  fd_sock_close(fd_sock_t s) { return close(s); }
  static inline ssize_t fd_sock_send(fd_sock_t s, const void* b, size_t n) { return write(s, b, n); }
  static inline ssize_t fd_sock_recv(fd_sock_t s, void* b, size_t n) { return read(s, b, n); }

  // AF_UNIX dial — identical to fd-game's original connect path, just hoisted
  // here so the call site is platform-agnostic.
  // Directory containing the running executable (no trailing separator).
  static inline bool fd_exe_dir(char* out, size_t n) {
      ssize_t got = readlink("/proc/self/exe", out, n - 1);
      if (got <= 0) return false;
      out[got] = '\0';
      char* slash = strrchr(out, '/');
      if (slash) *slash = '\0';
      return true;
  }

  static inline fd_sock_t fd_sock_dial(const char* path) {
      fd_sock_t s = socket(AF_UNIX, SOCK_STREAM, 0);
      if (s < 0) return FD_BAD_SOCK;
      struct timeval tv = { 2, 0 };
      if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) != 0 ||
          setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv) != 0)
          fprintf(stderr, "fd-game: WARNING socket timeouts not set (%s) — "
                  "a daemon hang could freeze the window\n", strerror(errno));
      struct sockaddr_un a; memset(&a, 0, sizeof a);
      a.sun_family = AF_UNIX;
      snprintf(a.sun_path, sizeof a.sun_path, "%s", path);
      if (connect(s, (struct sockaddr*)&a, sizeof a) != 0) {
          close(s);
          return FD_BAD_SOCK;
      }
      return s;
  }
#endif

#endif // FD_PLATFORM_H
