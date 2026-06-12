#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# fd_client.py -- Feverdream Engine: reference client for the fd-daemon wire
# protocol (PROTOCOL.md). This is the GAME side of the AGPL firewall: it links
# nothing from POV-Ray, it just speaks plain POV SDL + name=float declares
# over a Unix socket.
#
# As a library:  FdClient(sock_path) -> .scene(text) .render(...) .ping() .shutdown()
# As a script:   self-test -- loads spin.pov, renders N frames with an orbiting
#                camera declare, reports fps, writes fd_selftest.ppm.
#
#   fd_client.py [sock_path] [scene.pov] [frames] [W] [H]

import socket
import struct
import sys
import time

MAGIC, VERSION = 0xFD, 0x00
T_SCENE_FULL, T_RENDER, T_PING, T_SHUTDOWN = 0x01, 0x02, 0x7E, 0x7F
T_FRAME, T_SCENE_ACK, T_ERROR = 0x81, 0x82, 0xEE
FLAG_WANT_FB = 0x01


class FdError(RuntimeError):
    pass


class FdClient:
    def __init__(self, sock_path="/tmp/feverdream.sock"):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.connect(sock_path)

    def _send(self, mtype, flags=0, payload=b""):
        self.s.sendall(struct.pack("<BBBBI", MAGIC, VERSION, mtype, flags,
                                   len(payload)) + payload)

    def _recv(self):
        hdr = self._read(8)
        magic, ver, mtype, flags, length = struct.unpack("<BBBBI", hdr)
        if magic != MAGIC or ver != VERSION:
            raise FdError("bad reply header")
        payload = self._read(length) if length else b""
        if mtype == T_ERROR:
            code = struct.unpack("<I", payload[:4])[0]
            raise FdError(f"daemon error {code}: {payload[4:].decode()}")
        return mtype, flags, payload

    def _read(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.s.recv(n - len(buf))
            if not chunk:
                raise FdError("daemon closed connection")
            buf += chunk
        return buf

    def ping(self):
        self._send(T_PING)
        mtype, _, _ = self._recv()
        return mtype == T_PING

    def scene(self, sdl_text):
        """Load a full POV SDL scene (re-parsed on every render)."""
        self._send(T_SCENE_FULL, 0, sdl_text.encode())
        mtype, _, payload = self._recv()
        if mtype != T_SCENE_ACK or struct.unpack("<I", payload)[0] != 0:
            raise FdError("scene rejected")

    def render(self, w, h, clock=0.0, aa=False, declares=None, want_fb=False):
        """Render one frame. declares = {name: float}. Returns
        (w, h, frame_time_us, rgba_bytes_or_None)."""
        declares = declares or {}
        body = struct.pack("<HHfBB", w, h, clock, 1 if aa else 0, len(declares))
        for name, val in declares.items():
            nb = name.encode("ascii")
            body += struct.pack("<B", len(nb)) + nb + struct.pack("<f", float(val))
        self._send(T_RENDER, FLAG_WANT_FB if want_fb else 0, body)
        mtype, flags, payload = self._recv()
        if mtype != T_FRAME:
            raise FdError(f"unexpected reply type {mtype:#x}")
        rw, rh, us = struct.unpack("<III", payload[:12])
        fb = payload[12:] if (flags & FLAG_WANT_FB) else None
        return rw, rh, us, fb

    def shutdown(self):
        self._send(T_SHUTDOWN)
        self.s.close()

    def close(self):
        self.s.close()


def write_ppm(path, w, h, rgba):
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h))
        for i in range(w * h):
            f.write(rgba[i * 4:i * 4 + 3])


def main():
    sock = sys.argv[1] if len(sys.argv) > 1 else "/tmp/feverdream.sock"
    scene = sys.argv[2] if len(sys.argv) > 2 else "spin.pov"
    frames = int(sys.argv[3]) if len(sys.argv) > 3 else 30
    w = int(sys.argv[4]) if len(sys.argv) > 4 else 320
    h = int(sys.argv[5]) if len(sys.argv) > 5 else 180

    c = FdClient(sock)
    assert c.ping(), "ping failed"
    c.scene(open(scene).read())
    print(f"fd_client: scene {scene} loaded, rendering {frames} frames @ {w}x{h}")

    total_us, fb = 0, None
    t0 = time.time()
    for i in range(frames):
        clk = i / max(frames - 1, 1)
        want = i == frames - 1                     # fetch only the last framebuffer
        rw, rh, us, fb_i = c.render(w, h, clock=clk,
                                    declares={"CAMA": clk * 360.0, "CAMR": 9.0},
                                    want_fb=want)
        total_us += us
        if fb_i:
            fb = fb_i
    wall = time.time() - t0

    print(f"  daemon render: avg {total_us/frames/1000:.2f} ms/frame "
          f"=> {1e6*frames/total_us:.1f} fps")
    print(f"  end-to-end   : avg {wall/frames*1000:.2f} ms/frame "
          f"=> {frames/wall:.1f} fps (incl. socket round-trip)")
    if fb:
        write_ppm("fd_selftest.ppm", rw, rh, fb)
        nonzero = sum(1 for b in fb[:4000] if b)
        print(f"  framebuffer  : {rw}x{rh}, {len(fb)} bytes, "
              f"{'LIVE' if nonzero else 'EMPTY?'} -> fd_selftest.ppm")
    c.shutdown()
    print("fd_client: clean shutdown")


if __name__ == "__main__":
    main()
