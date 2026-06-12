# Reference: how characters work in Feverdream

This folder preserves earlier iterations as worked examples.

## stickman_scene.pov — the original player character

The first playable build's hero: torso, head, two swinging legs. He was
replaced by Chunkins the squirrel, but he's the clearest possible example of
the engine's character contract:

- A character is **just POV scene text** inside the generated scene.
- The host feeds it five generic declares every frame —
  `POSX, POSZ, JUMP, TURN, STEP` — plain name=float pairs over the socket.
- All animation is scene math driven by those floats (legs swing by
  `sin(STEP)`, the body rotates by `TURN`, the whole union translates by
  position). The engine has no skeleton, no animation system, no idea what
  the hero looks like.

To make your own character: render `stickman_scene.pov` with stock POV-Ray,
edit the union until you like it, then drop it into `build_scene()` in
`../fd-game.cpp` in place of the CHUNKINS union. Same declares, new hero.

Two hard-won POV rules (both bit this project):

1. `scale` acts about the **origin** — a positioned sphere scales its
   position too. Build at the origin, `scale`, then `translate`.
2. Generated SDL must be **brace-balanced or the daemon parse-fails
   silently** per frame. `build_scene()` treats snprintf truncation as fatal
   for exactly this reason.

The walk-cycle driver that originally animated him interactively (before the
daemon/game split existed) is preserved at `../../daemon/walk.cpp`.
