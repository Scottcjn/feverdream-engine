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

## The fix

Stop relaunching. Keep one POV-Ray process **resident**: initialize once, then
loop `receive scene → render to an in-memory framebuffer → blit to screen`,
never touching the disk. At ~11 ms of real work that targets **~90 fps even
re-parsing the scene every frame** — and re-parsing every frame is *fine*,
because parse is only 5 ms.

POV-Ray is open source, so we modify it rather than fake it.

## Status

| Piece | State |
|---|---|
| Benchmark proving the thesis (`bench.sh`) | ✅ real, reproducible |
| Architecture, reviewed by 3 independent models | ✅ see `ARCHITECTURE.md` |
| Daemon wire protocol | ✅ spec'd (`daemon/PROTOCOL.md`) |
| Resident POV-Ray daemon (frontend patch) | 🚧 in progress |
| SDL/GL display client + input loop | 🚧 in progress |
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
