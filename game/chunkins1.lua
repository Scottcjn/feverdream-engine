-- SPDX-License-Identifier: MIT
-- CHUNKINS: THE SEARCH FOR THE GOLDEN ACORN
-- World 1 — Acorn Meadow (tutorial: run, jump, stomp — and meet your first
-- friends and gadgets)
--
-- A sunny meadow. Bonk the sleepy waddler, find five acorns, and a STAR rises
-- at the center — touch it to move on. New this time, Mario-64 style:
--   * MABEL THE MOLE — walk up to her and she pops the hedge-gate open for you.
--   * a BOUNCE PAD (orange) — stand on it to spring high and grab a floating acorn.
--   * a STAR EXIT — collect every acorn, then reach the star to finish the world.

game_title = "Golden Acorn 1: Acorn Meadow"
next_level = "chunkins2.lua"

config = {
  speed = 4.4, turn_rate = 2.8, step_rate = 11.0,
  gravity = -26.0, jump_v = 9.8, player_radius = 0.45,
}

boxes = {
  -- 1-4: hedges (the meadow walls)
  { cx =   0, cz =  14, hx = 14.6, hz = 0.6, h = 1.0, r = 0.20, g = 0.48 },
  { cx =   0, cz = -14, hx = 14.6, hz = 0.6, h = 1.0, r = 0.20, g = 0.48 },
  { cx =  14, cz =   0, hx = 0.6, hz = 13.4, h = 1.0, r = 0.20, g = 0.48 },
  { cx = -14, cz =   0, hx = 0.6, hz = 13.4, h = 1.0, r = 0.20, g = 0.48 },
  -- 5: a gentle crate (acorn sits on top)
  { cx =  5, cz =  5, hx = 1.1, hz = 1.1, h = 0.30, r = 0.72, g = 0.48 },
  -- 6: the waddler (slow, one bonk and the meadow is safe)
  { cx = 10, cz = -10, hx = 0.45, hz = 0.55, h = 0.9, cy = 0, dyn = true, shape = "baddie" },
  -- 7: BOUNCE PAD (orange) — spring up to the floating acorn above it
  { cx = -7, cz =  6, hx = 1.0, hz = 1.0, h = 0.20, r = 0.95, g = 0.55 },
  -- 8-10: MABEL'S secret nook (east + north walls, and a GATE on the south)
  { cx =  12, cz =  9, hx = 0.6, hz = 2.2, h = 1.2, r = 0.20, g = 0.48 },
  { cx =  10, cz = 11, hx = 2.2, hz = 0.6, h = 1.2, r = 0.20, g = 0.48 },
  { cx =  10, cz =  7, hx = 2.2, hz = 0.6, h = 1.2, r = 0.22, g = 0.40, dyn = true },  -- the gate
  -- 11: MABEL THE MOLE (friendly NPC — approach to open the gate)
  { cx =  8, cz =  5.5, hx = 0.5, hz = 0.5, h = 0.7, cy = 0, dyn = true, r = 0.52, g = 0.34, shape = "npc" },
  -- 12: the STAR exit (hidden until every acorn is found)
  { cx =  0, cz =  0, hx = 0.4, hz = 0.4, h = 0.6, cy = -10, dyn = true, solid = false, shape = "star" },
  -- 13-17: five acorns
  { cx =  0, cz =  3, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" }, -- on the +Z lane
  { cx =  5, cz =  5, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" }, -- on the crate
  { cx = -7, cz =  6, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" }, -- floating (bounce pad)
  { cx = 10, cz =  9, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" }, -- inside the nook
  { cx = -9, cz = -8, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" }, -- open corner
}

local BADDIES = { { box = boxes[6], speed = 1.5, home = {x=10, z=-10}, alive = true } }
local PAD     = boxes[7]
local GATE    = boxes[10]
local MABEL   = boxes[11]
local STAR    = boxes[12]
local ACORNS  = { boxes[13], boxes[14], boxes[15], boxes[16], boxes[17] }
local base_y  = { 0.45, 0.75, 1.20, 0.45, 0.45 }   -- acorn 3 hovers low over the pad

game_score, game_lives, game_state = 0, 3, "playing"
game_world = 1
local collected, invuln = {}, 0
local gate_open = false

-- shared enemy routine: chase Chunkins, get bonked from above, bite from the side
local function run_baddies(t, dt, player)
  invuln = math.max(0, invuln - dt)
  for _, bd in ipairs(BADDIES) do
    local b = bd.box
    if bd.alive then
      local dx, dz = player.x - b.cx, player.z - b.cz
      local d = math.sqrt(dx * dx + dz * dz)
      if d > 0.05 then
        b.cx = b.cx + dx / d * bd.speed * dt
        b.cz = b.cz + dz / d * bd.speed * dt
        b.ry = math.deg(math.atan(dx, dz))
      end
      dx, dz = player.x - b.cx, player.z - b.cz
      d = math.sqrt(dx * dx + dz * dz)
      local top = b.cy + b.h
      local over = math.abs(dx) < b.hx + 0.45 and math.abs(dz) < b.hz + 0.45
      if over and player.vy < -0.5 and player.jump > top - 0.30 then
        bd.alive = false; b.cy = -10
        bounce = 7.5; play_sound("bump", 1.0)
      elseif over and player.jump < top - 0.15 and invuln <= 0 then
        game_lives = game_lives - 1; invuln = 1.5
        play_sound("bump", 1.3)
        local len = math.max(0.2, d)
        push_x, push_z = dx / len * 2.4, dz / len * 2.4
        if game_lives <= 0 then game_state = "lost"; play_sound("land", 1.3) end
      end
    end
  end
end

-- stand on the orange pad (feet near the ground) -> spring high
local function run_bounce_pad(player)
  if not player.grounded then return end
  if math.abs(player.x - PAD.cx) < PAD.hx + 0.4 and
     math.abs(player.z - PAD.cz) < PAD.hz + 0.4 then
    bounce = 11.5
    play_sound("jump", 1.1)
  end
end

-- walk up to Mabel the Mole and she opens the hedge-gate to the secret acorn
local function run_mabel(t, player)
  MABEL.cy = 0.05 * math.sin(t * 3.0)            -- a friendly little bob
  if not gate_open then
    local dx, dz = player.x - MABEL.cx, player.z - MABEL.cz
    if dx * dx + dz * dz < 2.2 * 2.2 then
      gate_open = true
      GATE.cy = -10                              -- the gate drops away
      play_sound("blip", 1.2)
    end
  end
end

function on_tick(t, dt, player)
  if game_state ~= "playing" then return end
  run_baddies(t, dt, player)
  run_bounce_pad(player)
  run_mabel(t, player)

  local got = 0
  for i, ac in ipairs(ACORNS) do
    if collected[i] then
      ac.cy = -10; got = got + 1
    else
      ac.cy = base_y[i] + 0.15 * math.sin(t * 2.2 + i)
      local dx, dz = player.x - ac.cx, player.z - ac.cz
      local dy = player.jump - ac.cy
      if dx * dx + dz * dz < 0.85 * 0.85 and dy > -0.7 and dy < 1.6 then
        collected[i] = true
        game_score = game_score + 1
        play_sound("blip", 1.0)
      end
    end
  end

  -- all acorns found -> the STAR rises at the meadow center; reach it to win
  if got >= #ACORNS then
    STAR.cy = 1.0 + 0.20 * math.sin(t * 2.5)
    local dx, dz = player.x - STAR.cx, player.z - STAR.cz
    local dy = player.jump - STAR.cy
    if dx * dx + dz * dz < 1.0 * 1.0 and dy > -1.0 and dy < 1.8 then
      game_state = "won"
      play_sound("jump", 1.3)
    end
  end
end

worlds = { "chunkins1.lua", "chunkins2.lua", "chunkins3.lua", "chunkins4.lua", "chunkins5.lua", "chunkins_hazelnut_bridges.lua", "chunkins7.lua", "chunkins8.lua" }
