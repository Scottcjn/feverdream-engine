# From Renderer to Engine — researched plan

Deep-research pass (2026-06-11): 5 search angles, 23 sources fetched, 112 claims
extracted, 25 verified by 3-vote adversarial panels → **15 confirmed**. A rate
limit killed the verifier mid-pass, so several temporal-reconstruction claims
are **unverified, not refuted** — they're marked below. Verified claims cite
their source; everything else is engineering judgment grounded in `FINDINGS.md`
and the working daemons.

## Where we actually are

`walk.cpp` is already a proto-game-engine: variable-dt host loop (clamped),
host-side physics (jump arc integrated in C++), pose state fed to POV as
`Declare=` scalars. The host-computes / renderer-receives split that
ARCHITECTURE.md's Proposal B demanded is *already the working pattern* — what's
missing is everything between input and pixels, plus Phase 2 renderer work.

## 1. The host/renderer split is the industry pattern (verified)

Embree's canonical dynamic-scene flow is exactly ours: **animation lives in the
host**; the CPU rewrites vertex/transform data, then notifies the renderer
(`rtcUpdateGeometryBuffer` + `rtcCommitGeometry` per object, ONE
`rtcCommitScene` per frame)
([dynamic_scene_device.cpp](https://github.com/RenderKit/embree/blob/master/tutorials/dynamic_scene/dynamic_scene_device.cpp), 3-0).
The renderer never owns game time. This confirms the Proposal-B direction:
POV's `clock`/SDL animation is for the batch pipeline; the live engine feeds
transforms.

Loop shape (canon: Gaffer on Games "Fix Your Timestep", Game Programming
Patterns — fetched, uncontested): **fixed-timestep simulation + free-running
render**. The accumulator pattern:

```
accumulator += frame_time
while (accumulator >= SIM_DT):   # e.g. 120 Hz sim
    simulate(SIM_DT)             # input, physics, AI, scripts
    accumulator -= SIM_DT
render(interpolate(prev, curr, accumulator/SIM_DT))
```

walk.cpp's clamped variable dt is fine for a PoC; move to the accumulator when
gameplay needs determinism (replays, network, consistent jump heights).

## 2. BVH strategy — the strongest verified cluster

POV builds one monolithic bounding hierarchy at parse. The entire industry
converged on a **two-level structure**, and the rules are unusually clean:

| Rule | Source | Vote |
|---|---|---|
| Refit (update) per-object BVH only after *limited* deformation — much cheaper than rebuild, but quality decays | [NVIDIA RTX best practices](https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/) | 3-0 |
| Rebuild after large deformations; rebuild refitted BVHs *periodically* (decay is hard to detect) | same | 3-0 |
| **Top level: always rebuild, never refit** — it's cheap and its quality dominates trace speed | same | 3-0 |
| Refit from new transforms/vertices is much faster than rebuild (instance matrices + TLAS update-in-place) | [DXR tutorial: refit](https://developer.nvidia.com/rtx/raytracing/dxr/DX12-Raytracing-tutorial/Extra/dxr_tutorial_extra_refit) | 3-0 |
| Embree's dynamic mode = two-level index with fast partial updates (`RTC_BUILD_QUALITY_LOW`) | [rtcSetSceneBuildQuality](https://github.com/RenderKit/embree/blob/master/doc/src/api/rtcSetSceneBuildQuality.md) | 3-0 |
| Build quality vs commit time is an explicit per-geometry dial; mixing refit/rebuild per object in one scene is supported practice | same + dynamic_scene tutorial | 3-0 |

(Genuinely refuted 0-3: "Embree marks the whole scene dynamic at LOW quality" —
the real pattern is **per-geometry granularity**, not whole-scene.)

**Mapping to Phase 2:** make POV's hierarchy two-level.
- Per-object sub-hierarchies built ONCE at load (the .kkrieger lesson — POV SDL
  macros are the procedural recipes; parse/generate at load, keep resident).
- Rigid motion = update the instance transform + world bounds; sub-BVH untouched.
  No refit-decay problem at all for rigid objects.
- Top level over object bounds: **rebuild every frame**. For our scene sizes
  (tens-to-hundreds of objects) that's microseconds.
- Deforming geometry (the walk rig): refit per frame, rebuild every N frames
  (NVIDIA's "periodically"), or treat each rigid limb as its own instance and
  sidestep deformation entirely — the rig is already split body + 2 legs.

## 3. Subsystem plan (minimal-but-complete, retro ethos)

.kkrieger (verified 3-0 ×2): full FPS in 97,280 bytes; assets stored as
*generation recipes* (textures = operator history, meshes = deformed solids),
all regenerated at load
([kkrieger-werkkzeug3 source](https://github.com/jaromil/kkrieger-werkkzeug3)).
We're already this — `.pov` macros + `mdl2pov.py` ARE the recipes. Keep it.

| Subsystem | Choice | Why / precedent |
|---|---|---|
| **Sim core** | Fixed-timestep host loop, plain structs-of-entities (no ECS framework) | Entity count is tiny; an ECS library is gold-plating. Array of `{id, transform, kind, state}` + per-kind update fns |
| **Collision** | Separate simplified collision world: AABBs/spheres/capsules + a ground heightfield, in the HOST | Never query renderer geometry — Quake-lineage engines collide against brushes/BSP, not render meshes. walk.cpp's jump already does host physics |
| **Renderer IPC** | The PROTOCOL.md socket daemon | Doom 3 BFG-style frontend/backend split, pushed across a process boundary. Also the licensing firewall (§4) |
| **Audio** | SDL2_mixer to start; a tiny tracker/synth later if the size ethos demands (.kkrieger used the V2 synth) | SDL2 is already a dependency of the game side |
| **Input** | SDL2 (already in walk.cpp) | done |
| **Scripting** | Lua 5.4, embedded in the GAME process only — never the daemon | Smallest mature embed (~200KB); keeps the daemon lean and the AGPL boundary clean |
| **Assets** | Procedural recipes: POV SDL macros, mdl2pov rigs. Generated at load, resident after | .kkrieger pattern, verified |

## 4. AGPL — verified positions, and a catch in our own tree

Verified (all from [FSF GPL FAQ](https://www.gnu.org/licenses/gpl-faq.html)):

- Pipes/sockets/argv "are communication mechanisms normally used between two
  separate programs" → daemon-over-socket is *presumptively* separate (3-0).
- BUT "if the semantics of the communication are intimate enough, exchanging
  complex internal data structures," the parts can be one program regardless of
  IPC (3-0).
- The line is "a legal question, which ultimately judges will decide" — there is
  NO guaranteed process-boundary safe harbor, only the FSF's stated criterion (3-0).
- Distributing GPL software *alongside* a proprietary system is fine if they
  "communicate at arms length" (2-0).
- The maximalist vendor position exists: Artifex/Ghostscript claims server-side
  deployment of its AGPL code obligates disclosing the ENTIRE application
  ([ghostscript.com/licensing](https://ghostscript.com/licensing/), 3-0). POV-Ray's
  copyright holders aren't Artifex, but that's the aggressive end of the spectrum.

**The catch:** `resident`/`live`/`walk` link `libvfe`+`libpovray` directly. The
MIT headers cover our source text, but each linked *binary* is a derivative
work of POV-Ray — AGPL applies to the whole. Today that's fine (the repo is
open and the patches are published). But it means **game logic currently lives
inside the AGPL boundary**. The socket daemon is what moves it out.

**Protocol design rules that keep the boundary arm's-length:**
1. Wire format = standard POV SDL text + generic render params (width, height,
   `Declare=` floats). That's the same document any POV user feeds the stock
   binary — hard to call "intimate internal data structures."
2. Be wary of TRANSFORM_DELTA with daemon-assigned object IDs (PROTOCOL.md
   0x10): shared internal object identity drifts toward "intimate semantics."
   If Phase 2 needs it, spec it as an open, documented, renderer-agnostic
   protocol any client/server could implement.
3. Daemon stays a standalone, separately useful, separately distributed AGPL
   tool with its own repo/release. The game ships as its own work that *talks
   to* it.
4. Get it checked by a human lawyer before any commercial binary ships
   (ARCHITECTURE.md already says this; the research confirms there's no
   settled doctrine to hide behind).

## 5. Temporal reconstruction — UNVERIFIED tier (rate-limited, flagged)

The verifier died on these; they match the literature as we know it but carry
no adversarial confirmation. Treat as leads, not facts:

- Q2VKPT/Quake II RTX: ~1 spp + A-SVGF temporal denoising; static BLAS at map
  load, dynamic BLAS rebuilt per frame (GTC 2019 talk).
- NVIDIA "Temporally Dense Ray Tracing": quarter-resolution rays per subframe +
  reprojection ≈ 3.5× throughput; 94% of study subjects preferred 240fps sparse
  over 60fps full.
- Frame *interpolation* (DLSS 3/FSR 3 class) adds ≥1 frame of latency;
  *extrapolation* doesn't (arXiv 2406.18551).

**Recommendation (unchanged from ARCHITECTURE.md, now better informed):** at
320×180 trace-bound 77fps, more real frames beats frame-gen — frame-gen pays
when real frames are expensive. The cheap near-term win is **temporal
accumulation for AA** (AA is currently off): jitter the camera sub-pixel,
reproject with analytic motion vectors (we know every transform delta), blend
with a clamped exponential moving average. That's TAA-lite and it also buys
effective supersampling at the same fps. Decide frame-gen empirically in
Phase 3 on the 5070, per the roadmap.

## Build order (maps onto ROADMAP Phase 2–4)

1. **fd-daemon** — promote resident.cpp into the PROTOCOL.md socket daemon
   (SCENE_FULL + RENDER + PING; shared-memory framebuffer option later).
   This is the AGPL firewall — do it before any more game code grows.
2. **fd-game** — new MIT host binary, no POV linkage: fixed-timestep loop,
   entity array, collision world (AABB/sphere + ground), input, SDL window +
   upscale blit (port from live.cpp), socket client.
3. **Phase 2 renderer work inside the daemon** (per FINDINGS "what's left"):
   persistent thread pool, resident scene, two-level hierarchy with per-frame
   top-level rebuild + rigid-instance transforms (§2).
4. **Lua scripting + audio** in fd-game once something is playable.
5. **Temporal AA** (jitter + reproject + clamp) in the daemon or as a post pass.
6. **Phase 3 frame-gen experiment** on the 5070 — only if heavy scenes/res
   make real frames expensive again.

## Sources (verified-claim sources, primary)

- NVIDIA RTX BVH best practices · DXR refit tutorial
- Embree `rtcSetSceneBuildQuality` docs · `dynamic_scene` tutorial source
- kkrieger-werkkzeug3 released source
- GNU GPL FAQ (FSF) · ghostscript.com/licensing (vendor-maximalist datapoint)
- Fetched but claims uncontested/unverified: Gaffer on Games (fixed timestep),
  Game Programming Patterns (game loop), Fabien Sanglard's Doom 3 BFG renderer
  writeup, GTC 2019 Q2VKPT talk, Temporally Dense Ray Tracing (NVIDIA Research),
  GFFE (arXiv 2406.18551), brashandplucky TAA notes.
