-- SPDX-License-Identifier: MIT
-- arena.lua — Feverdream Engine: the world lives here now.
--
-- `boxes` defines the arena. Each entry is a collision AABB AND a raytraced
-- box — fd-game generates the POV scene from this table, so what you collide
-- with is what you see. Mark a box `dyn = true` and move it from on_tick():
-- its position streams to the renderer as declares every frame.
--
-- `config` tunes movement; on_tick(t, dt, player) runs once per frame with
-- simulation time (deterministic — the selftest depends on it).

config = {
  speed         = 4.2,
  turn_rate     = 2.6,
  step_rate     = 11.0,
  gravity       = -28.0,
  jump_v        = 9.5,
  player_radius = 0.45,
}

boxes = {
  { cx =  0.0, cz =  6.0, hx = 2.2, hz = 0.6, h = 1.6, r = 0.55, g = 0.35 }, -- wall ahead
  { cx = -5.0, cz =  0.0, hx = 0.8, hz = 0.8, h = 0.9, r = 0.65, g = 0.40 }, -- crate left
  { cx =  5.0, cz = -2.0, hx = 0.8, hz = 0.8, h = 2.4, r = 0.75, g = 0.45 }, -- pillar right
  { cx =  4.0, cz =  5.0, hx = 1.2, hz = 1.2, h = 0.5, r = 0.85, g = 0.50 }, -- low step
  -- the patrol block: slides back and forth across the south lane. dyn=true
  -- makes it a declare-driven box the script may move every frame.
  { cx = -2.0, cz = -5.0, hx = 0.9, hz = 0.9, h = 1.2, r = 0.95, g = 0.30, dyn = true },
}

local patrol = boxes[#boxes]   -- the dyn box above, not a hard-coded index
local last_dir = 1
function on_tick(t, dt, player)
  -- patrol: ±4 units of travel on a 6-second cycle, pure function of sim time
  patrol.cx = -2.0 + 4.0 * math.sin(t * math.pi / 3.0)
  -- blip when the patrol reverses — script-triggered audio via play_sound()
  local dir = math.cos(t * math.pi / 3.0) >= 0 and 1 or -1
  if dir ~= last_dir then play_sound("blip", 0.5) end
  last_dir = dir
end
