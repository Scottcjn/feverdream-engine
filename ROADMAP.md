# Roadmap

Phases are ordered so each one **proves something measurable** before the next
starts. Frame-rate targets are ceilings from `bench.sh` until a real loop
measures the full cost on real scenes.

### Phase 0 — Proof (done)
- [x] Measure parse vs trace vs wall-clock → `bench.sh`
- [x] Tri-brain architecture review → `ARCHITECTURE.md`
- [x] Wire protocol draft → `daemon/PROTOCOL.md`

### Phase 1 — Resident daemon PoC (DONE — see FINDINGS.md)
- [x] Check out POV-Ray 3.7 source; build stock to confirm toolchain (`tools/build_engine.sh`)
- [x] Resident frontend: link `libvfe`+`libpovray`, init the engine ONCE, render
      every frame against the live session into an in-memory framebuffer — no PNG,
      no respawn (`daemon/resident.cpp`)
- [x] Found + cut the hidden **50 ms message-queue poll** → 0.1 ms (`patches/`)
- [x] SDL2 live window with orbit camera + live fps (`daemon/live.cpp`)
- [x] **Gate result:** real raytraced frames live in memory; **1.4 → ~9–12 fps**
      (thread-count-dependent). Gate substantially met; >30 fps is Phase 2.

### Phase 2 — Resident thread-pool + scene (next; the .kkrieger lesson)
The remaining wall: POV rebuilds its **render thread-pool every frame** (more
threads = slower on small frames; bigger res = better fps — fixed-overhead
signature, FINDINGS §4). The .kkrieger lesson: do expensive setup ONCE, keep it
resident, lean per-frame loop.
- [ ] Keep the render thread-pool alive across frames (don't spawn/join per render)
- [ ] Keep the scene database + bounding hierarchy resident; per frame mutate only
      transforms + camera (animation moves out of SDL `clock` into the host loop)
- [ ] Accel-structure **refit** on motion; full rebuild only on topology change
- [ ] **Gate:** sustained 60 fps at 320×180 on a real scene

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
