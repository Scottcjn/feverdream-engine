# Architecture

Reviewed by three independent models (Elyan Labs tri-brain dev loop): Codex 5.5
(adversarial/security), GPT-OSS 120B (coherence), Grok (regression/blast-radius).
Verdict: **CONVERGED** on the plan below. Their BLOCKING findings reshaped it —
recorded here so we don't re-walk into them.

## Core thesis (survives review)

The per-frame cost that kills real-time is **process lifecycle + disk I/O**, not
raytracing (~6 ms) and not parsing (~5 ms). See `bench.sh`.

## What we build: Proposal A — resident process

Keep **one POV-Ray process alive**. Initialize once, then loop:

```
loop:
    scene = receive()                 # full SDL text or a delta, over a socket
    framebuffer = render(scene)       # to in-memory RGBA, NOT a PNG on disk
    blit(framebuffer)                 # SDL2/GL texture upload
    report(frame_time)
```

Re-parsing every frame is **fine** — parse is 5 ms. Target ~90 fps at 240×135.

Implementation path: **patch POV-Ray 3.7's frontend main loop**, not link
`libpovray` directly. The core has no stable embeddable interface and dirty
process-global state (radiosity cache, photon state, media accumulators,
font/INI caches, pattern singletons). The frontend already owns init/teardown;
we make it loop instead of exit, and swap the PNG writer for a framebuffer hand-off.

## What we DON'T do yet: Proposal B — mutate scene graph, skip parse

**Demoted by review (BLOCKING ×2):**

1. **POV evaluates `clock` and all SDL logic at *parse* time.** Our animation
   scenes (`skeleton_walk`, `base_to_war`, `marching_army`, rigged `.inc`s) drive
   motion through `#if (clock…)`, `#while`, functions — baked at parse. Bumping a
   clock variable post-parse re-renders the *same frame*. Skipping parse only
   works if animation moves **out of SDL into the host game loop** (host computes
   transforms, feeds them as deltas; the `.pov` becomes static geometry). That's
   correct for a real game — but it's a per-scene rearchitecture, done later.
2. **Moving objects invalidate the bounding hierarchy.** The BVH/accel structure
   is built on world-space bounds at parse; a moved transform needs at least a
   **refit** or you get missed intersections / wrong shadows that only show under
   motion. So "trace-only ~160 fps" must include a per-frame refit cost.

## DLSS-style temporal layer — Proposal C (planned, see ROADMAP)

Render fewer real frames, reconstruct the rest. A raytracer has an edge: object
motion is **analytic** (we know every transform delta), so motion vectors are
cheaper than a raster game that estimates them. But not "free" — reprojection
still needs per-pixel depth/object-ID + occlusion/disocclusion/transparency
handling. **Decide empirically:** once A makes frames cheap, rendering more real
frames may beat frame-gen. Frame-gen pays only if real frames stay expensive
(big scenes, higher res). Posterize+bayer-dither post pass keeps the VGA look.

## Guardrails (SHOULD-FIX, folded in)

- **Parallel track, not a replacement.** The batch PNG pipeline (ffmpeg,
  crt_post, BoTTube publish, the feverdream stale-file/timeout/process-group
  safety) keeps consuming numbered PNGs. The daemon is separate.
- **fps numbers are ceilings** until a PoC measures the *full* loop (render +
  accel refit + framebuffer transfer + GL upload) on **real** scenes, not
  micro-benchmarks. Battle scenes have more lights/objects/media.
- **Resident = no per-frame crash isolation.** One OOM/assert/infinite-loop
  stops every subsequent frame. Need a health-check + auto-restart, and the game
  daemon must NOT be shared with feverdream batch jobs.
- **Audit POV globals** that must reset between frames (radiosity, photons,
  media, font/INI) — stale state = accumulating or one-frame-lagged artifacts.
- **Frontend feature parity.** Antialias/quality/sampling applied only in the
  current frontend init path must be replicated, or the live path won't match the
  offline reference renders.

## Licensing (BLOCKING before distribution)

POV-Ray 3.7 is **AGPLv3** (verify exact terms before shipping). A patched daemon
distributed with a game is a derivative work → source disclosure, plus AGPL's
network clause if served. Today we ship the *stock* binary and only output
artifacts (PNG/MP4) — clean. A forked daemon changes that. Options: keep the
daemon an open, separately-distributed AGPL component the game talks to over IPC
(arm's-length, but AGPL is aggressive — get it checked), or open-source the fork.
**Resolve before any binary ships.**

## Smallest PoC that proves it

A persistent POV process that inits once, then on a socket: receives a `.pov`,
renders to an in-memory RGBA buffer, blits via SDL2, logs frame time. Drive one
object's transform from the keyboard. **Success = sustained >30 fps live at
240×135 on a real scene** (e.g. one rotating battlecruiser). Batch path untouched.
