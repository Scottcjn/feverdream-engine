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

## 6. The backend driver loops — the real wall (and 60fps)

After the message poll, ~81ms/frame remained even at 1 thread, on an empty scene
— flat regardless of resolution, block size, or thread count. Tracing it: the
backend has two driver loops that poll their task queues with coarse sleeps:

- `source/backend/scene/scene.cpp` — parse loop: `Delay(10)` (≈ the 20ms parse phase)
- `source/backend/scene/view.cpp`  — render loop: `Delay(50)` (≈ the 52ms "render" phase)

The render finishes in microseconds, but the driver **sleeps 50ms before noticing
it's done**. Same bug as the message poll, one layer down. Cut both to 1ms.

## 7. Phase 2: the busy-work era (sleeps are dead; long live the profiler)

With the sleeps gone, the daemon pegged ~108% CPU at the frame floor — the
remaining overhead was WORK, not waiting. Method change: stop hunting Delay()
calls, start reading perf.

**The 33% bug.** perf on an empty scene showed `pov::RandomDoubles` eating
**33.5% of total daemon CPU**. Every TraceTask constructor (×N threads ×every
frame) rebuilt the random-sequence tables for pixel jitter (TracePixel) and
radiosity sample directions (RadiosityFunction) — from a **default-seeded**
mt19937, i.e. the identical values every single time. Memoized the sequences
(`randomsequences.cpp`); a follow-up perf pass showed the cache's 32KB vector
copy itself at 12% (`memmove`), so `RandomDoubleSequence` now holds a
`shared_ptr` to the immutable cached vector — zero generation, zero copy.
Bit-identical output (deterministic 180-frame game selftest PPM hash unchanged).

**This also explains §4's paradox at the root**: "more threads = slower" was
never mostly thread-spawn — it was every extra thread's TraceTask regenerating
the same tables. The persistent task pool (below) and the cache were measured
together and separately; after the cache, pooled vs spawn-per-task is a wash on
this box (8.59 vs 8.67 ms @320×180, 60-frame A/B).

**Persistent worker pool** (`task.cpp`): Task::Start now submits to a
process-lifetime worker pool instead of spawning a boost::thread per task
(parser task + N trace tasks per frame). Per-task POVMS context and thread
startup/cleanup semantics unchanged; `FD_NO_TASK_POOL=1` reverts to stock
behavior. Neutral on this box post-cache; keeps us honest on slower boxes and
higher thread counts, and it's the right resident-engine shape.

**Driver polls**: the two control-loop `Delay(1)` polls dropped to 200µs
nanosleeps; the POVMS receive fallback dropped 0.1ms→20µs (measured ≈nothing —
the cv wakes do work; kept anyway, it's free).

## Scoreboard

| stage | per-frame | fps |
|---|---|---|
| stock povray (respawn + disk) | ~700 ms | 1.4 |
| resident process + 50ms→0.1ms message poll | ~120 ms | ~8 |
| + low thread count | ~81 ms | ~12 |
| + cut backend driver delays (50ms+10ms→1ms) | ~13 ms | 77 |
| **+ Phase 2: task pool, 0.2ms polls, sequence cache** | **~9 ms @320×180** | **107–141** |

Phase 2 floors (64×36, fixed overhead): 9.85 → **2.34 ms**.
Real scenes @320×180: spin **107 fps**, bee_world **91 fps**, fd-game arena
scene **141 fps daemon / 138 fps end-to-end through the socket** (game/
selftest). ROADMAP Phase 2 gate (sustained 60 fps @320×180 on a real scene):
**met**. Output verified bit-identical pre/post (md5 of deterministic selftest).

## What's left (further optimization, not blockers)

- Scene database + bounding hierarchy still rebuilt per frame (parse is ~1-1.5ms
  for our scenes; empty-scene pipeline floor is now 2.3ms). Making the scene
  resident with transform-only mutation + two-level BVH (GAME_ENGINE.md §2) is
  the remaining .kkrieger step — it matters once scenes get heavy, not before.
- Antialiasing is off for speed; temporal AA via analytic motion vectors is the
  planned cleanup (GAME_ENGINE.md §5).
- DLSS-style temporal reprojection on the 5070 for 4K/heavy scenes (Phase 3).

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
