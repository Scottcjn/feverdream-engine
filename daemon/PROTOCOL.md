# Daemon wire protocol (draft v0)

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
| 0x02 | RENDER         | render params: u16 w, u16 h, f32 clock, u8 aa, u8 _ |
| 0x10 | TRANSFORM_DELTA| object-id (u32) + 16×f32 matrix — Proposal B only   |
| 0x11 | TOPOLOGY       | add/remove object — forces accel-structure rebuild  |
| 0x7E | PING           | none → daemon replies PONG (health check)           |
| 0x7F | SHUTDOWN       | none                                                |

Reply for RENDER (when flags bit0 set), header type 0x81:
```
offset size  field
0      8     header (magic, ver, 0x81, flags, length)
8      4     w (uint32)
12     4     h (uint32)
16     4     frame_time_us (uint32)  # measured render, daemon side
20     w*h*4 RGBA8 framebuffer
```
With bit0 clear, the daemon blits to its own SDL window and returns only the
20-byte header (w/h/frame_time_us) — zero-copy display, no buffer over the wire.

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
