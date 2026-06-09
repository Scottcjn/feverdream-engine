# Findings — measured, not theorized

All numbers from this box (16 logical cores, ~8 physical + HT), POV-Ray built
from `3.7-stable`, captured by `daemon/resident.cpp`. Reproduce with `bench.sh`
(stock) and the resident daemon.

## 1. The per-frame wall is overhead, not raytracing

Stock `povray` invocation: **~700 ms/frame (~1.4 fps)**. POV's own timers say
parse ≈ 5 ms, trace ≈ 6 ms. A **16×16** frame costs the same as **480×480**.
The other ~690 ms is process spawn + runtime init + font/INI load + PNG encode +
**disk write** + teardown — paid every frame because it's a batch CLI tool.

## 2. Resident process — kill the respawn

`daemon/resident.cpp` creates the engine **once** (`vfeUnixSession`) and renders
every frame against the live session, capturing pixels into memory (no PNG, no
disk). This removed the process spawn.

## 3. The 50 ms message poll (the big one)

After going resident, frames were still flat at ~360 ms regardless of resolution
— a tell-tale fixed wait. Traced it to `vfe/vfepovms.cpp`: the frontend↔backend
message queue did `timed_wait` of **50 ms** per receive (the source even has a
`// TODO: have a shorter wait`). Every render phase-change message ate up to
50 ms; ~8 hops = ~360 ms of pure sleep. Cut to 0.1 ms → **patches/feverdream-realtime.patch**.

## 4. Per-frame thread-pool churn (the current wall)

The decisive test — vary thread count on a tiny scene:

```
threads=1:  81 ms     threads=4:  81 ms     threads=16: 118 ms
```

**More threads = slower.** POV rebuilds its render thread-pool *every frame*;
spawning 16 threads to trace 6 ms of pixels costs more than the trace. This also
produces the paradox **"bigger resolutions get better fps"**:

```
spin scene, threads=8:   240x135 → 128 ms   960x540 → 84 ms
```

A fill-rate-bound renderer slows down with resolution. Ours speeds up — because
we're **thread-churn-bound**, and higher res finally gives the freshly-spawned
threads enough work to amortize their spawn cost.

## 5. POV *does* use the cores — on heavy renders

```
spin @1920x1080:  threads=1 → 984 ms (1.0 fps)   threads=16 → 211 ms (4.7 fps)
spin @960x540:    threads=1 → 282 ms             threads=8  → 84 ms (3.3x)
```

So the trace parallelizes well (≈ to physical-core count; HT past 8 adds little).
The cores are squandered only by the *per-frame pool rebuild*, not by the tracer.

## Scoreboard

| stage | per-frame | fps |
|---|---|---|
| stock povray (respawn + disk) | ~700 ms | 1.4 |
| resident process + 50ms→0.1ms poll | ~120 ms | ~8 |
| + low thread count (skip churn) | ~81 ms | ~12 |
| **next:** resident thread-pool + scene (Phase 2) | → trace floor | target 60+ |

~9–12× stock already, producing real raytraced frames live in memory. The
remaining wall is named and located.

## The .kkrieger lesson (Phase 2 direction)

Farbrausch's `.kkrieger` is a 96 KB procedurally-generated FPS: it does ALL the
expensive procedural generation **once at load**, then runs a lean real-time loop
that never regenerates. Two lessons apply directly:

1. **We're already procedural like it** — POV SDL macros generate every unit from
   code; zero asset files, tiny footprint. Same philosophy.
2. **Separate generation from the frame loop.** Don't rebuild the scene database,
   bounding hierarchy, or thread-pool per frame. Amortize all of it at init, keep
   it resident, and let the per-frame loop only mutate transforms + camera, then
   re-trace on a persistent worker pool. That is how 16 cores finally pay off in
   real time — a standing pool fed frames, not 16 threads spawned-and-joined 60×/s.
