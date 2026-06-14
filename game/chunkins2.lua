-- SPDX-License-Identifier: MIT
-- CHUNKINS: THE SEARCH FOR THE GOLDEN ACORN
-- World 2 — Crate Heights (medium: climb, then throw the switch)
--
-- The trail climbs the staircase to a SWITCH at the top — throw it and the
-- bridge extends across to the high ledge. Two prowlers patrol (they come
-- back), the elevator carries an acorn up and down. Gather all five acorns
-- and a STAR rises on the ledge — reach it to move on.

game_title = "Golden Acorn 2: Crate Heights"
next_level = "chunkins3.lua"

config = {
  speed = 4.6, turn_rate = 2.8, step_rate = 11.0,
  gravity = -26.0, jump_v = 9.8, player_radius = 0.45,
}

boxes = {
  -- hedges
  { cx =   0, cz =  14, hx = 14.6, hz = 0.6, h = 1.0, r = 0.20, g = 0.48 },
  { cx =   0, cz = -14, hx = 14.6, hz = 0.6, h = 1.0, r = 0.20, g = 0.48 },
  { cx =  14, cz =   0, hx = 0.6, hz = 13.4, h = 1.0, r = 0.20, g = 0.48 },
  { cx = -14, cz =   0, hx = 0.6, hz = 13.4, h = 1.0, r = 0.20, g = 0.48 },
  -- the staircase
  { cx =  0,   cz = 4.0, hx = 1.1, hz = 1.1, h = 0.30, r = 0.72, g = 0.48 },
  { cx =  0,   cz = 6.4, hx = 1.1, hz = 1.1, h = 1.00, r = 0.70, g = 0.46 },
  { cx =  2.6, cz = 6.4, hx = 1.1, hz = 1.1, h = 1.70, r = 0.68, g = 0.44 },
  { cx =  5.2, cz = 6.4, hx = 1.1, hz = 1.1, h = 2.40, r = 0.66, g = 0.42 },
  -- bridge (starts RETRACTED — throw the switch to extend it) + high ledge
  { cx =  8.6, cz = 6.4, hx = 2.2, hz = 1.1, h = 0.4, r = 0.62, g = 0.40, cy = -10, dyn = true },
  { cx = 12.2, cz =  9.5, hx = 1.6, hz = 3.2, h = 3.0, r = 0.58, g = 0.38 },
  -- the elevator
  { cx = -5.0, cz = -5.0, hx = 1.3, hz = 1.3, h = 0.4, r = 0.95, g = 0.55, dyn = true },
  -- two prowlers (they respawn — the heights are never safe for long)
  { cx =  10, cz = -10, hx = 0.45, hz = 0.55, h = 0.9, cy = 0, dyn = true, shape = "baddie" },
  { cx = -10, cz =  10, hx = 0.45, hz = 0.55, h = 0.9, cy = 0, dyn = true, shape = "baddie" },
  -- five acorns: ground, stair top, elevator rider, ledge, far corner
  { cx =  0,   cz =  3,   hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx =  5.2, cz =  6.4, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx = -5.0, cz = -5.0, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx = 12.2, cz =  9.5, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx = -11,  cz = -11,  hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  -- 19: SWITCH (yellow) on the top stair — stand on it to extend the bridge
  { cx =  5.2, cz =  6.4, hx = 0.9, hz = 0.9, h = 0.20, r = 0.92, g = 0.82, cy = 2.40, dyn = true },
  -- 20: the STAR exit on the high ledge (rises once every acorn is found)
  { cx = 12.2, cz =  9.5, hx = 0.4, hz = 0.4, h = 0.6, cy = -10, dyn = true, solid = false, shape = "star" },
}

local BRIDGE  = boxes[9]
local SWITCH  = boxes[19]
local STAR    = boxes[20]
local ELEV    = boxes[11]
local BADDIES = {
  { box = boxes[12], speed = 2.0, home = {x=10,  z=-10}, alive = true, respawn = 12 },
  { box = boxes[13], speed = 2.4, home = {x=-10, z=10},  alive = true, respawn = 12 },
}
local ACORNS = { boxes[14], boxes[15], boxes[16], boxes[17], boxes[18] }
local base_y = { 0.45, 2.85, nil, 3.45, 0.45 }   -- nil = rides the elevator

game_score, game_lives, game_state = 0, 3, "playing"
game_world = 2
local collected, invuln = {}, 0
local bridge_out = false

local function run_baddies(t, dt, player)
  invuln = math.max(0, invuln - dt)
  for _, bd in ipairs(BADDIES) do
    local b = bd.box
    if not bd.alive then
      if bd.respawn then
        bd.timer = (bd.timer or bd.respawn) - dt
        if bd.timer <= 0 then
          bd.alive, b.cy, b.cx, b.cz = true, 0, bd.home.x, bd.home.z
        end
      end
    else
      local dx, dz = player.x - b.cx, player.z - b.cz
      local d = math.sqrt(dx * dx + dz * dz)
      if d > 0.05 then
        b.cx = b.cx + dx / d * bd.speed * dt
        b.cz = b.cz + dz / d * bd.speed * dt
        b.ry = math.deg(math.atan(dx, dz))
      end
      dx, dz = player.x - b.cx, player.z - b.cz   -- fresh after the move
      d = math.sqrt(dx * dx + dz * dz)
      local top = b.cy + b.h
      local over = math.abs(dx) < b.hx + 0.45 and math.abs(dz) < b.hz + 0.45
      if over and player.vy < -0.5 and player.jump > top - 0.30 then
        bd.alive = false; b.cy = -10
        if bd.respawn then bd.timer = bd.respawn end
        bounce = 7.5
        play_sound("bump", 1.0)
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

function on_tick(t, dt, player)
  if game_state ~= "playing" then return end
  ELEV.cy = 1.1 + 1.1 * math.sin(t * math.pi * 2 / 5)
  run_baddies(t, dt, player)

  -- SWITCH: stand on the yellow pad at the top of the stairs to extend the
  -- bridge across to the high ledge (Mario-64 "press to build a path").
  if not bridge_out then
    if math.abs(player.x - SWITCH.cx) < SWITCH.hx + 0.45 and
       math.abs(player.z - SWITCH.cz) < SWITCH.hz + 0.45 and
       player.jump > SWITCH.cy + SWITCH.h - 0.4 then
      bridge_out = true
      play_sound("blip", 1.2)
    end
  end
  BRIDGE.cy = bridge_out and 2.4 or -10   -- deployed vs retracted

  local got = 0
  for i, ac in ipairs(ACORNS) do
    if collected[i] then
      ac.cy = -10; got = got + 1
    else
      local by = base_y[i]
      if by == nil then
        by = ELEV.cy + ELEV.h + 0.45
        ac.cx, ac.cz = ELEV.cx, ELEV.cz
      end
      ac.cy = by + 0.15 * math.sin(t * 2.2 + i)
      local dx, dz = player.x - ac.cx, player.z - ac.cz
      local dy = player.jump - ac.cy
      if dx * dx + dz * dz < 0.85 * 0.85 and dy > -0.7 and dy < 1.6 then
        collected[i] = true
        game_score = game_score + 1
        play_sound("blip", 1.0)
      end
    end
  end

  -- every acorn found -> the STAR rises on the high ledge; reach it to finish
  if got >= #ACORNS then
    STAR.cy = 3.0 + 0.20 * math.sin(t * 2.5)
    local dx, dz = player.x - STAR.cx, player.z - STAR.cz
    local dy = player.jump - STAR.cy
    if dx * dx + dz * dz < 1.0 * 1.0 and dy > -1.0 and dy < 1.8 then
      game_state = "won"
      play_sound("jump", 1.3)
    end
  end
end

worlds = { "chunkins1.lua", "chunkins2.lua", "chunkins3.lua", "chunkins4.lua", "chunkins5.lua", "chunkins_hazelnut_bridges.lua", "chunkins7.lua", "chunkins8.lua" }
