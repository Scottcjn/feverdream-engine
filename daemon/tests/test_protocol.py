#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# test_protocol.py — drive the real fd-daemon over a real socket and check it
# against PROTOCOL.md, clause by clause. The daemon under test is the actual
# daemon/fd-daemon.cpp, compiled against tests/stub/ instead of a vendored
# POV-Ray tree (see run.sh); everything exercised here — framing, caps, the
# declare whitelist, the read timeout, per-connection scene state — is renderer
# independent, so the stub costs the tests nothing.
#
#   test_protocol.py <socket_path>            (run.sh boots the daemon first)

import os
import socket
import struct
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from fd_client import FdClient, FdError            # the reference client itself

MAGIC, VER = 0xFD, 0x00
T_SCENE_FULL, T_RENDER, T_PING, T_SHUTDOWN = 0x01, 0x02, 0x7E, 0x7F
T_FRAME, T_SCENE_ACK, T_ERROR = 0x81, 0x82, 0xEE
FLAG_WANT_FB = 0x01
E_BAD_PAYLOAD, E_NO_SCENE, E_RENDER_FAIL, E_BAD_DECLARE = 1, 2, 3, 4

# what PROTOCOL.md says a RENDER message may carry: 10 fixed bytes + 255
# declares of (u8 namelen, <=32 name bytes, f32)
MAX_DECLARES, MAX_DECL_NAME, RENDER_FIXED = 255, 32, 10
MAX_RENDER = RENDER_FIXED + MAX_DECLARES * (1 + MAX_DECL_NAME + 4)   # 9445

SOCK = sys.argv[1] if len(sys.argv) > 1 else "/tmp/feverdream-test.sock"
SCENE = ("#version 3.7;\ncamera { location <0,0,-5> look_at 0 }\n"
         "light_source { <-5,10,-8> rgb 1 }\nsphere { 0,1 pigment { rgb 1 } }\n")

fails = []


def check(cond, label):
    print(("  ok   " if cond else "  FAIL ") + label)
    if not cond:
        fails.append(label)


# ---- raw framing helpers (deliberately NOT fd_client: several cases are
# ---- messages a correct client would never send) ----------------------------
def dial(timeout=20.0):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(SOCK)
    s.settimeout(timeout)
    return s


def send(s, mtype, body=b"", flags=0):
    s.sendall(struct.pack("<BBBBI", MAGIC, VER, mtype, flags, len(body)) + body)


def recv(s):
    """-> (type, flags, payload); type None if the peer hung up."""
    try:
        h = b""
        while len(h) < 8:
            c = s.recv(8 - len(h))
            if not c:
                return None, None, b""
            h += c
        _, _, mtype, flags, ln = struct.unpack("<BBBBI", h)
        p = b""
        while len(p) < ln:
            c = s.recv(ln - len(p))
            if not c:
                break
            p += c
        return mtype, flags, p
    except (ConnectionResetError, BrokenPipeError):
        return None, None, b""


def declares(pairs):
    b = b""
    for name, val in pairs:
        nb = name.encode("ascii")
        b += struct.pack("<B", len(nb)) + nb + struct.pack("<f", val)
    return b


def render_body(w, h, pairs, clock=0.0, aa=0, ndecl=None):
    blob = declares(pairs)
    n = len(pairs) if ndecl is None else ndecl
    return struct.pack("<HHfBB", w, h, clock, aa, n & 0xFF) + blob


def loaded(s):
    send(s, T_SCENE_FULL, SCENE.encode())
    return recv(s)


def err_code(p):
    return struct.unpack("<I", p[:4])[0] if len(p) >= 4 else None


# ============================================================================
print("== reference client round-trip (fd_client.py)")
c = FdClient(SOCK)
check(c.ping(), "PING answers PING")
c.scene(SCENE)
w, h, us, fb = c.render(64, 48, clock=0.5, declares={"CAMA": 90.0, "CAMR": 9.0},
                        want_fb=True)
check((w, h) == (64, 48), f"FRAME reports the requested size (got {w}x{h})")
check(fb is not None and len(fb) == 64 * 48 * 4,
      f"framebuffer is w*h*4 bytes (got {len(fb) if fb else None})")
# the stub paints red=x-ramp, green=y-ramp, blue=clock: proves the pixels came
# through DrawPixelBlock -> fb_put -> the reply, not from a zeroed buffer
check(fb[0:3] == b"\x00\x00\x7f" and fb[63 * 4:63 * 4 + 3] == b"\xff\x00\x7f",
      "pixels arrive through the display path (x-ramp intact)")
w2, h2, us2, fb2 = c.render(64, 48, want_fb=False)
check(fb2 is None, "flags bit0 clear -> info-only reply, no framebuffer")
c.close()

print("== per-connection scene state (PROTOCOL.md: reconnect = fresh session)")
s = dial()
send(s, T_RENDER, render_body(64, 48, []))
t, f, p = recv(s)
check(t == T_ERROR and err_code(p) == E_NO_SCENE, "RENDER before SCENE_FULL -> E_NO_SCENE")
t, f, p = loaded(s)
check(t == T_SCENE_ACK and err_code(p) == 0, "SCENE_FULL -> SCENE_ACK status 0")
send(s, T_SCENE_FULL, b"")
t, f, p = recv(s)
check(t == T_ERROR and err_code(p) == E_BAD_PAYLOAD, "empty SCENE_FULL -> E_BAD_PAYLOAD")
s.close()

print("== dimension + payload validation")
s = dial()
loaded(s)
for w, h, why in ((8, 64, "w below MIN_DIM"), (64, 8, "h below MIN_DIM"),
                  (0, 64, "w zero")):
    send(s, T_RENDER, render_body(w, h, []))
    t, f, p = recv(s)
    check(t == T_ERROR and err_code(p) == E_BAD_PAYLOAD, f"{why} -> E_BAD_PAYLOAD")
send(s, T_RENDER, b"\x00" * (RENDER_FIXED - 1))
t, f, p = recv(s)
check(t == T_ERROR and err_code(p) == E_BAD_PAYLOAD, "short RENDER -> E_BAD_PAYLOAD")
s.close()

print("== declare whitelist")
s = dial()
loaded(s)
for name, why in (("bad name", "space in name"), ("1abc", "leading digit"),
                  ("a-b", "hyphen"), ("Output_to_File=off", "option injection"),
                  ("x" * 33, "over 32 chars")):
    send(s, T_RENDER, render_body(64, 48, [(name, 1.0)]))
    t, f, p = recv(s)
    check(t == T_ERROR and err_code(p) == E_BAD_DECLARE, f"{why} -> E_BAD_DECLARE")
send(s, T_RENDER, render_body(64, 48, [("_ok9", 1.0), ("A" * 32, 2.0)]))
t, f, p = recv(s)
check(t == T_FRAME, "legal names accepted")
s.close()

print("== declare count must match the bytes actually sent")
s = dial()
loaded(s)
# a client whose declare count overflowed the u8 field: 261 entries on the wire,
# ndecl byte reads 5. Answering with a frame here means every animated object
# silently freezes and nobody is told.
pairs = [("POSX", 1.0), ("POSZ", 1.0), ("JUMP", 1.0), ("TURN", 1.0), ("STEP", 1.0)]
pairs += [(f"BOX{i}{ax}", 1.0) for i in range(64) for ax in "XYZR"]
send(s, T_RENDER, render_body(64, 48, pairs, ndecl=len(pairs)))
t, f, p = recv(s)
check(t == T_ERROR and err_code(p) == E_BAD_DECLARE,
      f"{len(pairs)} declares with a wrapped u8 count -> E_BAD_DECLARE (not a frame)")
send(s, T_RENDER, render_body(64, 48, [("A", 1.0), ("B", 2.0)], ndecl=1))
t, f, p = recv(s)
check(t == T_ERROR and err_code(p) == E_BAD_DECLARE, "trailing unparsed bytes -> E_BAD_DECLARE")
send(s, T_RENDER, render_body(64, 48, [("A", 1.0), ("B", 2.0)], ndecl=3))
t, f, p = recv(s)
check(t == T_ERROR and err_code(p) == E_BAD_DECLARE, "count larger than the list -> E_BAD_DECLARE")
s.close()

print("== the RENDER cap is the protocol's own maximum")
s = dial()
loaded(s)
full = [("D" + "x" * (MAX_DECL_NAME - 1), 1.0)] * MAX_DECLARES
body = render_body(64, 48, full)
check(len(body) == MAX_RENDER, f"255 x 32-char declares = {len(body)} bytes")
send(s, T_RENDER, body)
t, f, p = recv(s)
check(t == T_FRAME, "PROTOCOL.md's largest legal RENDER is served, not dropped")
s.close()

s = dial()
loaded(s)
send(s, T_RENDER, b"\x00" * (MAX_RENDER + 1))
t, f, p = recv(s)
check(t == T_ERROR and err_code(p) == E_BAD_PAYLOAD,
      "over-cap RENDER is refused with an ERROR, not a silent hang-up")
s.close()

print("== bad framing does not take the daemon down")
s = dial()
s.sendall(b"\x00" * 8)                       # wrong magic
check(recv(s)[0] is None, "wrong magic -> connection dropped")
s.close()
s = dial()
s.sendall(struct.pack("<BBBBI", MAGIC, 0x7F, T_PING, 0, 0))
check(recv(s)[0] is None, "wrong version -> connection dropped")
s.close()
s = dial()
send(s, 0x55)                                 # unknown type
t, f, p = recv(s)
check(t == T_ERROR and err_code(p) == E_BAD_PAYLOAD, "unknown type -> E_BAD_PAYLOAD, stream survives")
send(s, T_PING)
check(recv(s)[0] == T_PING, "  ...and the next message on the same socket still works")
s.close()

print("== read timeout (issue #5: one stalled client must not own the daemon)")
budget = float(os.environ.get("FD_READ_TIMEOUT", "2")) * 4 + 5
t0 = time.time()
s = dial(timeout=budget)
s.sendall(b"\xFD\x00\x02")                    # 3 of 8 header bytes, then silence
try:
    stalled_hdr = (s.recv(1) == b"")
except socket.timeout:
    stalled_hdr = False
check(stalled_hdr, f"stalled mid-header client is dropped ({time.time()-t0:.1f}s)")
s.close()

t0 = time.time()
s = dial(timeout=budget)
s.sendall(struct.pack("<BBBBI", MAGIC, VER, T_RENDER, 0, 4096) + b"\x00")
try:
    stalled_body = (s.recv(1) == b"")
except socket.timeout:
    stalled_body = False
check(stalled_body, f"stalled mid-payload client is dropped ({time.time()-t0:.1f}s)")
s.close()

c = FdClient(SOCK)
c.scene(SCENE)
check(c.render(32, 32)[0] == 32, "daemon still serves the next client after both stalls")
c.close()

print()
if fails:
    print(f"FAILURES ({len(fails)}):")
    for f_ in fails:
        print("  - " + f_)
    sys.exit(1)
print("ALL PROTOCOL TESTS PASS")
