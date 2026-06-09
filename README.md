# Feverdream Engine — real-time POV-Ray

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
live in memory. Measured result so far: **stock 1.4 fps → ~9–12 fps resident**,
after patching a hidden **50 ms message-queue poll** down to 0.1 ms. The next
wall is POV rebuilding its **thread-pool every frame** (more threads = slower on
small frames; bigger resolutions = *better* fps — the fixed-overhead signature).

See **[FINDINGS.md](FINDINGS.md)** for the full measured story and the
`.kkrieger`-inspired Phase 2 (keep the thread-pool + scene resident → target 60+).

## Status

| Piece | State |
|---|---|
| Benchmark proving the thesis (`bench.sh`) | ✅ real, reproducible |
| Architecture, reviewed by 3 independent models | ✅ see `ARCHITECTURE.md` |
| Daemon wire protocol | ✅ spec'd (`daemon/PROTOCOL.md`) |
| Resident POV-Ray daemon (frontend patch) | ✅ built — real frames, ~9–12 fps |
| Resident thread-pool + scene (Phase 2) | 🚧 next — the wall to 60 fps |
| SDL/GL live window + input loop | 🚧 in progress |
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
