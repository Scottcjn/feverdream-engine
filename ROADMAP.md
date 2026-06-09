# Roadmap

Phases are ordered so each one **proves something measurable** before the next
starts. Frame-rate targets are ceilings from `bench.sh` until a real loop
measures the full cost on real scenes.

### Phase 0 — Proof (done)
- [x] Measure parse vs trace vs wall-clock → `bench.sh`
- [x] Tri-brain architecture review → `ARCHITECTURE.md`
- [x] Wire protocol draft → `daemon/PROTOCOL.md`

### Phase 1 — Resident daemon PoC (next)
- [ ] Check out POV-Ray 3.7 source; build stock to confirm toolchain
- [ ] Patch the frontend main loop: init once → accept SCENE_FULL/RENDER on the
      socket → render to in-memory RGBA → return/blit → loop (no PNG, no exit)
- [ ] Audit + reset process globals between frames (radiosity, photons, media,
      font/INI) — verify no cross-frame artifacts
- [ ] SDL2 display client + keyboard transform of one object
- [ ] **Gate:** sustained >30 fps live at 240×135 on a real scene (rotating
      battlecruiser). If not, profile the *full* loop, don't hand-wave.

### Phase 2 — Game-shaped animation
- [ ] Move animation out of SDL clock into the host loop for game scenes
      (host computes transforms; `.pov` = static geometry)
- [ ] Proposal B: TRANSFORM_DELTA path + per-frame accel-structure **refit**
      (not rebuild) — measure the refit cost, confirm correctness under motion
- [ ] Topology add/remove → accel rebuild only on change

### Phase 3 — DLSS-style temporal layer (RTX 5070 on .106)
- [ ] Emit depth + object-ID buffers from the tracer (cheap)
- [ ] Motion-vector reprojection from analytic transform deltas
- [ ] Decide empirically: reproject-to-60 vs just render more real frames
- [ ] Posterize + bayer-dither post pass (match the VGA look)

### Phase 4 — Productize
- [ ] Resolve AGPL posture before any binary ships (`ARCHITECTURE.md` → Licensing)
- [ ] Health-check + auto-restart supervisor; separate daemon per consumer
- [ ] Wire a playable demo into BoTTube / RustChain (feverdream addon sibling)

## Non-goals
- Replacing the batch pre-render pipeline (`bottube-feverdream`) — it stays.
- Smooth full-res real-time. The aesthetic IS low-res + dither; that's the point.
