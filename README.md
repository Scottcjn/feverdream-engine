# Feverdream Engine — real-time POV-Ray

[![BCOS Certified](https://img.shields.io/badge/BCOS-Certified-brightgreen?style=flat)](BCOS.md) [![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

> The gorgeous old raytraced look — *ReBoot*, *Donkey Kong Country*, early *Toy
> Story* — rendered **live, in a game loop**, instead of baked overnight.

Everyone "knows" you can't raytrace a game on the CPU: POV-Ray loops at ~3–4 fps
no matter how small you make the picture, so the look only ever shows up
*pre-rendered*. Feverdream Engine is the proof that the wall is in the wrong place.

## The finding (measured, reproducible — run `./bench.sh`)

POV-Ray 3.7, our retro scenes, 240×135:

| What | Time |
|---|---|
| **Parse** the scene | ~5 ms |
| **Trace** (the actual raytracing) | ~6 ms |
| **Real work per frame** | **~11 ms (~90 fps)** |
| Wall-clock per `povray` call | ~700 ms |

A **16×16** frame costs the *same* ~700 ms as a **480×480** one. So the cost is
not the picture and not the raytracing — it's the program **launching itself,
loading config/fonts, encoding a PNG, writing it to disk, and shutting down,
every single frame.** Like quitting Maya and relaunching it for every frame.

## The fix (and what we actually measured)

Stop relaunching. Keep one POV-Ray process **resident**: initialize once, then
loop `receive scene → render to an in-memory framebuffer → blit`, never touching
the disk. POV-Ray is open source, so we modify it rather than fake it.

**This is built** (`daemon/resident.cpp`) and it renders real raytraced frames
live in memory. Measured result: **stock 1.4 fps → 77 fps** (~55×) at 320×180 on a reflective
scene — fully raytraced, live in memory, no disk. Got there by cutting three
hidden coarse poll/sleep loops (a 50ms message-queue poll + 50ms/10ms backend
driver loops) down to ~1ms. It now scales with resolution like a normal
renderer — trace-bound, not overhead-bound.

See **[FINDINGS.md](FINDINGS.md)** for the full measured story (and the
`.kkrieger`-inspired further optimizations for heavy scenes).

## Status

| Piece | State |
|---|---|
| Benchmark proving the thesis (`bench.sh`) | ✅ real, reproducible |
| Architecture, reviewed by 3 independent models | ✅ see `ARCHITECTURE.md` |
| Daemon wire protocol | ✅ spec'd (`daemon/PROTOCOL.md`) |
| Resident POV-Ray daemon (frontend patch) | ✅ built — **77 fps**, real-time |
| Backend driver-loop delays cut (the 60fps wall) | ✅ done — see FINDINGS.md |
| SDL2 live window + orbit controls (`daemon/live.cpp`) | ✅ built |
| DLSS-style temporal upscale on RTX 5070 | ⛅ planned (`ROADMAP.md`) |

This is a sibling to [bottube-feverdream](https://github.com/Scottcjn/bottube-feverdream)
(the batch pre-render pipeline). **The batch pipeline is untouched** — the
resident engine is a separate track, not a replacement.

## License

POV-Ray 3.7 is **AGPLv3**. A patched, distributed daemon is a derivative work
with source-disclosure + network obligations. See `ARCHITECTURE.md` → Licensing
before shipping anything that bundles it. The benchmark + design docs here are
original work under this repo's license.

Built by Elyan Labs · powered by Elyan-class agents.
