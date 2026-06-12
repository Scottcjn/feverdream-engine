# Daemon wire protocol (v0 — implemented by fd-daemon.cpp / fd_client.py)

The resident POV-Ray daemon listens on a Unix domain socket (local, lowest
latency). One client at a time (the game/display loop). Length-prefixed binary
frames; little-endian. Versioned from byte 0 so we can evolve without guessing.

## Why a socket (not stdin, not shared files)

POV-Ray has no server mode and can't stream scenes over stdin in a loop, so the
daemon is our own wrapper around a patched frontend. A socket gives clean framing,
backpressure, and a single place to enforce versioning + partial-read handling —
the failure mode Grok flagged (a delta that mutates an object the daemon already
baked → silently wrong frame). Every message is explicit and validated.

## Message header (8 bytes)

```
offset size  field
0      1     magic   = 0xFD ('feverdream')
1      1     version = 0x00
2      1     type    (see below)
3      1     flags   (bit0 = client wants framebuffer echoed back)
4      4     length  (uint32, payload bytes that follow)
```

## Message types

| type | name           | payload                                             |
|------|----------------|-----------------------------------------------------|
| 0x01 | SCENE_FULL     | UTF-8 POV-Ray SDL text — full scene, re-parsed      |
| 0x02 | RENDER         | u16 w, u16 h, f32 clock, u8 aa, u8 ndecl, declares  |
| 0x10 | TRANSFORM_DELTA| object-id (u32) + 16×f32 matrix — Proposal B only   |
| 0x11 | TOPOLOGY       | add/remove object — forces accel-structure rebuild  |
| 0x7E | PING           | none → daemon replies type 0x7E, length 0           |
| 0x7F | SHUTDOWN       | none                                                |

**Declares** (the host→renderer animation channel; how the game drives a rig):
after the fixed 10 RENDER bytes come `ndecl` entries of
`u8 namelen, namelen ascii bytes, f32 value` — each becomes a POV
`Declare=NAME=value` option. Names are whitelisted by the daemon to
`[A-Za-z_][A-Za-z0-9_]{0,31}` (anything else rejects the whole message) so a
client can never inject renderer options through a name. Values are formatted
as floats daemon-side, never spliced raw. (The old draft's reserved pad byte
became `ndecl`; a v0-draft client sending 0 is a valid no-declares render.)

Keeping this channel to **plain SDL text + generic name=float pairs** is
deliberate: it's the same input any POV user feeds the stock binary, which
keeps the daemon↔game boundary arm's-length (GAME_ENGINE.md §4).

## Replies

| type | name      | payload                                              |
|------|-----------|------------------------------------------------------|
| 0x81 | FRAME     | u32 w, u32 h, u32 frame_time_us [, w*h*4 RGBA8]      |
| 0x82 | SCENE_ACK | u32 status (0 = ok)                                  |
| 0xEE | ERROR     | u32 code, utf-8 message                              |

Error codes: 1 bad payload · 2 no scene loaded · 3 render failed · 4 bad declare.

RENDER always gets a FRAME (0x81) reply: the 8-byte header, then u32 w, u32 h,
u32 frame_time_us (measured daemon-side, SetOptions→render-done). With request
flags bit0 set, the RGBA8 framebuffer follows and the reply echoes bit0; with
bit0 clear only the 12 info bytes return (cheap frame-pacing probe). A future
zero-copy path (shared memory or daemon-side blit) can be added as a new flag
without breaking v0 clients.

## Contract rules (from the regression review)

- **SCENE_FULL is the safe default.** Re-parse every frame; 5 ms, dodges the
  parse-time-clock and accel-invalidation landmines. TRANSFORM_DELTA/TOPOLOGY
  are the later Proposal-B optimization and MUST be paired (a delta on an unknown
  object-id is rejected, not silently applied).
- **Object identity** is assigned by the daemon at parse and returned in a
  SCENE_FULL ack (TODO: define ack). Clients never invent ids.
- **Partial reads**: a message is processed only when all `length` bytes arrive.
  Short read on the socket = wait, never render a half-applied scene.
- **One bad message never poisons the next frame** — validate header magic +
  version + bounded length before reading payload; on violation, drain + reset.
- **Health**: a watchdog PINGs; on miss → restart the daemon (resident processes
  lose the per-frame crash isolation the batch path has).
