-- SPDX-License-Identifier: MIT
-- crate_climb.lua — CHUNKINS' CRATE CLIMB: a gentle platformer for small
-- adventurers. Chunkins the squirrel collects acorns.
--
-- Climb the crate staircase, ride the bobbing elevator to the high ledge,
-- and collect all 4 gold stars. Nothing chases you, nothing hurts you,
-- and falling just means landing somewhere soft. Win = all stars.
--
--   fd-game /tmp/feverdream.sock 1280 720 4 crate_climb.lua

config = {
  speed         = 4.2,
  turn_rate     = 2.8,
  step_rate     = 11.0,
  gravity       = -26.0,   -- a touch floatier than relic sweep
  jump_v        = 9.8,     -- generous jumps — this is for kids
  player_radius = 0.45,
}

boxes = {
  -- yard walls
  { cx =   0, cz =  14, hx = 14.6, hz = 0.6, h = 2.2, r = 0.50, g = 0.42 },
  { cx =   0, cz = -14, hx = 14.6, hz = 0.6, h = 2.2, r = 0.50, g = 0.42 },
  { cx =  14, cz =   0, hx = 0.6, hz = 13.4, h = 2.2, r = 0.50, g = 0.42 },
  { cx = -14, cz =   0, hx = 0.6, hz = 13.4, h = 2.2, r = 0.50, g = 0.42 },

  -- the crate staircase: walk up the first (step-height), jump the rest
  { cx =  0,  cz =  4.0, hx = 1.1, hz = 1.1, h = 0.30, r = 0.72, g = 0.48 },
  { cx =  0,  cz =  6.4, hx = 1.1, hz = 1.1, h = 1.00, r = 0.70, g = 0.46 },
  { cx =  2.6, cz = 6.4, hx = 1.1, hz = 1.1, h = 1.70, r = 0.68, g = 0.44 },
  { cx =  5.2, cz = 6.4, hx = 1.1, hz = 1.1, h = 2.40, r = 0.66, g = 0.42 },

  -- a long bridge you can also walk UNDER (head-room rule)
  { cx =  8.6, cz = 6.4, hx = 2.2, hz = 1.1, h = 0.4, r = 0.62, g = 0.40, cy = 2.4 },

  -- the high ledge in the corner, reachable from the bridge
  { cx = 12.2, cz =  9.5, hx = 1.6, hz = 3.2, h = 3.0, r = 0.58, g = 0.38 },

  -- the elevator: a platform that bobs between floor and bridge height.
  -- stand on it and the rising floor carries you (no special code needed)
  { cx = -5.0, cz = -5.0, hx = 1.3, hz = 1.3, h = 0.4, r = 0.95, g = 0.55, dyn = true },

  -- 4 gold stars: one on the grass (warm-up), staircase top, elevator
  -- summit ledge, and high ledge
  { cx =   0, cz =  2.0, hx = 0.26, hz = 0.26, h = 0.5, r = 1.0, g = 0.85, dyn = true, solid = false, shape = "acorn" },
  { cx =  5.2, cz = 6.4, hx = 0.26, hz = 0.26, h = 0.5, r = 1.0, g = 0.85, dyn = true, solid = false, shape = "acorn" },
  { cx = -5.0, cz = -5.0, hx = 0.26, hz = 0.26, h = 0.5, r = 1.0, g = 0.85, dyn = true, solid = false, shape = "acorn" },
  { cx = 12.2, cz =  9.5, hx = 0.26, hz = 0.26, h = 0.5, r = 1.0, g = 0.85, dyn = true, solid = false, shape = "acorn" },
}

local ELEV   = boxes[11]
local STARS  = { boxes[12], boxes[13], boxes[14], boxes[15] }
-- each star floats above its perch; the elevator star rides the elevator
local star_base = { 0.45, 2.85, nil, 3.45 }   -- nil = computed from elevator

game_score, game_lives, game_state = 0, 3, "playing"
local collected = {}

function on_tick(t, dt, player)
  if game_state ~= "playing" then return end

  -- elevator bobs floor <-> bridge height on an easy 5-second cycle
  ELEV.cy = 1.1 + 1.1 * math.sin(t * math.pi * 2 / 5)
  star_base[3] = ELEV.cy + ELEV.h + 0.45

  for i, st in ipairs(STARS) do
    if collected[i] then
      st.cy = -10
    else
      st.cy = star_base[i] + 0.15 * math.sin(t * 2.2 + i)
      if i == 3 then st.cx, st.cz = ELEV.cx, ELEV.cz end
      local dx, dz = player.x - st.cx, player.z - st.cz
      local dy = player.jump - st.cy
      if dx * dx + dz * dz < 0.9 * 0.9 and dy > -0.7 and dy < 1.6 then
        collected[i] = true
        game_score = game_score + 1
        play_sound("blip", 1.0)
        if game_score >= #STARS then
          game_state = "won"
          play_sound("jump", 1.2)
        end
      end
    end
  end
end

game_title = "Chunkins' Crate Climb"
