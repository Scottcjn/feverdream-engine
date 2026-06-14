// SPDX-License-Identifier: MIT
// fd_listen.h — the cross-platform LISTEN-side socket shim for fd-daemon.
//
// The mirror of game/fd_platform.h (client/dial side). POSIX keeps the exact
// AF_UNIX listener fd-daemon always used — a filesystem socket with the
// non-socket-refusal + unlink + 0600 safety. Windows binds TCP loopback
// (127.0.0.1:PORT, default below), matching the client's transport. This is
// generic socket glue (no POV-Ray code); it compiles into the AGPL daemon but
// is itself MIT, like the client shim.
//
// Keep the POSIX path byte-for-byte what fd-daemon did before — anything
// platform-specific stays inside the #ifdef.

#ifndef FD_LISTEN_H
#define FD_LISTEN_H

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>

#define FD_DEFAULT_PORT 47999

// Parse a port string; 0 (invalid) unless a bare 1..65535.
static inline unsigned fd_parse_port(const char* s) {
    if (!s || !*s) return 0;
    char* end = NULL;
    unsigned long p = strtoul(s, &end, 10);
    return (end && *end == '\0' && p > 0 && p < 65536) ? (unsigned)p : 0;
}

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET fd_sock_t;
  #define FD_BAD_SOCK INVALID_SOCKET

  static inline int  fd_net_startup(void) { WSADATA w; return WSAStartup(MAKEWORD(2, 2), &w); }
  static inline void fd_net_cleanup(void) { WSACleanup(); }
  static inline bool fd_sock_valid(fd_sock_t s) { return s != INVALID_SOCKET; }
  static inline int  fd_sock_close(fd_sock_t s) { return closesocket(s); }
  static inline int  fd_sock_read(fd_sock_t s, void* b, size_t n) {
      if (n > (size_t)INT_MAX) n = INT_MAX;
      return ::recv(s, (char*)b, (int)n, 0);
  }
  static inline int  fd_sock_write(fd_sock_t s, const void* b, size_t n) {
      if (n > (size_t)INT_MAX) n = INT_MAX;
      return ::send(s, (const char*)b, (int)n, 0);
  }

  // Bind+listen on 127.0.0.1:<port from `path` or FD_PORT or default>. The
  // `path` arg is the daemon's socket-path argument, reused as a port here so
  // the same CLI works on both OSes (a non-numeric path → default port).
  static inline fd_sock_t fd_listen(const char* path) {
      unsigned port = fd_parse_port(path);
      if (!port) port = fd_parse_port(getenv("FD_PORT"));
      if (!port) port = FD_DEFAULT_PORT;

      fd_sock_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (s == INVALID_SOCKET) { fprintf(stderr, "fd-daemon: socket (WSA %d)\n", WSAGetLastError()); return FD_BAD_SOCK; }
      // SO_EXCLUSIVEADDRUSE is the real anti-hijack flag on Windows: it stops a
      // second process from binding the same port (omitting SO_REUSEADDR alone
      // does NOT guarantee that). Correct for a single-instance daemon (tri-brain).
      BOOL excl = TRUE;
      setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&excl, sizeof excl);

      struct sockaddr_in a; memset(&a, 0, sizeof a);
      a.sin_family = AF_INET;
      a.sin_port   = htons((unsigned short)port);
      // 127.0.0.1 keeps the daemon off the network. NOTE: unlike the 0600
      // AF_UNIX path, loopback is reachable by ANY local user — acceptable
      // because the trust model is the stock POV-Ray one (PROTOCOL.md): a local
      // peer can submit scenes exactly as it could run povray on local input.
      a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      if (bind(s, (struct sockaddr*)&a, sizeof a) != 0) {
          fprintf(stderr, "fd-daemon: bind 127.0.0.1:%u (WSA %d)\n", port, WSAGetLastError());
          closesocket(s); return FD_BAD_SOCK;
      }
      if (listen(s, 1) != 0) {
          fprintf(stderr, "fd-daemon: listen (WSA %d)\n", WSAGetLastError());
          closesocket(s); return FD_BAD_SOCK;
      }
      // log the RESOLVED port here (main can't — it only has the raw arg, which
      // for the default launch is a placeholder path, not the real port)
      printf("fd-daemon: listening on 127.0.0.1:%u\n", port);
      return s;
  }

  static inline fd_sock_t fd_accept(fd_sock_t ls) { return ::accept(ls, NULL, NULL); }

#else  // ---------------------------------------------------------------- POSIX
  #include <unistd.h>
  #include <sys/socket.h>
  #include <sys/un.h>
  #include <sys/stat.h>
  #include <cerrno>
  typedef int fd_sock_t;
  #define FD_BAD_SOCK (-1)

  static inline int  fd_net_startup(void) { return 0; }
  static inline void fd_net_cleanup(void) {}
  static inline bool fd_sock_valid(fd_sock_t s) { return s >= 0; }
  static inline int  fd_sock_close(fd_sock_t s) { return close(s); }
  static inline ssize_t fd_sock_read(fd_sock_t s, void* b, size_t n) { return read(s, b, n); }
  static inline ssize_t fd_sock_write(fd_sock_t s, const void* b, size_t n) { return write(s, b, n); }

  // The exact AF_UNIX listener fd-daemon always used: refuse a non-socket at
  // the path (never unlink an arbitrary/hostile file), unlink a stale socket,
  // bind, lock to the local user (0600), listen.
  static inline fd_sock_t fd_listen(const char* path) {
      int s = socket(AF_UNIX, SOCK_STREAM, 0);
      if (s < 0) { perror("socket"); return FD_BAD_SOCK; }
      struct sockaddr_un addr; memset(&addr, 0, sizeof addr);
      addr.sun_family = AF_UNIX;
      snprintf(addr.sun_path, sizeof addr.sun_path, "%s", path);
      struct stat st;
      if (lstat(path, &st) == 0) {
          if (!S_ISSOCK(st.st_mode)) {
              fprintf(stderr, "fd-daemon: %s exists and is not a socket — refusing\n", path);
              close(s); return FD_BAD_SOCK;
          }
          unlink(path);
      }
      if (bind(s, (struct sockaddr*)&addr, sizeof addr) < 0) { perror("bind"); close(s); return FD_BAD_SOCK; }
      chmod(path, 0600);
      if (listen(s, 1) < 0) { perror("listen"); close(s); return FD_BAD_SOCK; }
      return s;   // main() prints the combined "engine resident, listening on..." banner
  }

  static inline fd_sock_t fd_accept(fd_sock_t ls) { return accept(ls, NULL, NULL); }

  // Remove the AF_UNIX socket file on shutdown (no-op concept on Windows TCP).
  static inline void fd_listen_cleanup(const char* path) { unlink(path); }
#endif

#ifdef _WIN32
  static inline void fd_listen_cleanup(const char*) {}   // no socket file on TCP
#endif

#endif // FD_LISTEN_H
