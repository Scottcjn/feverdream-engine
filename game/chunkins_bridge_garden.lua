-- SPDX-License-Identifier: MIT
-- CHUNKINS: THE SEARCH FOR THE GOLDEN ACORN
-- Bonus World 9 — Bridge Garden (easy-medium: side routes and one safe prowler)
--
-- A quiet garden grew around an old footbridge. The Golden Acorn rests on the
-- far stump, but the side leaves hide extra snacks for bold little squirrels.
-- The first acorn sits straight ahead so new players learn the route quickly.

game_title = "Golden Acorn Bonus: Bridge Garden"

config = {
  speed = 4.7, turn_rate = 2.9, step_rate = 11.0,
  gravity = -26.0, jump_v = 10.0, player_radius = 0.45,
}

boxes = {
  -- hedges
  { cx =   0, cz =  14, hx = 14.6, hz = 0.6, h = 1.0, r = 0.20, g = 0.48 },
  { cx =   0, cz = -14, hx = 14.6, hz = 0.6, h = 1.0, r = 0.20, g = 0.48 },
  { cx =  14, cz =   0, hx = 0.6, hz = 13.4, h = 1.0, r = 0.20, g = 0.48 },
  { cx = -14, cz =   0, hx = 0.6, hz = 13.4, h = 1.0, r = 0.20, g = 0.48 },
  -- bridge garden route
  { cx =   0, cz =  5.6, hx = 1.2, hz = 1.3, h = 0.45, r = 0.70, g = 0.46 },
  { cx =   0, cz =  8.2, hx = 1.4, hz = 1.3, h = 0.95, r = 0.66, g = 0.43 },
  { cx =   0, cz = 11.0, hx = 1.8, hz = 1.5, h = 1.45, r = 0.58, g = 0.38 },
  { cx =  -7, cz =  6.2, hx = 1.2, hz = 1.2, h = 1.05, r = 0.68, g = 0.44 },
  { cx =   7, cz =  6.2, hx = 1.2, hz = 1.2, h = 1.05, r = 0.68, g = 0.44 },
  -- a gentle moving leaf ferry between the side pads
  { cx =  -4, cz = -4.0, hx = 1.4, hz = 1.0, h = 0.35, r = 0.38, g = 0.65, dyn = true },
  -- one slow prowler, placed off the main lane
  { cx =  10, cz = -10, hx = 0.45, hz = 0.55, h = 0.9, cy = 0, dyn = true, shape = "baddie" },
  -- six acorns: first lane pickup, bridge, ferry, two side pads, golden stump
  { cx =   0, cz =  3.0, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx =   0, cz =  8.2, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx =  -4, cz = -4.0, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx =  -7, cz =  6.2, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx =   7, cz =  6.2, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx =   0, cz = 11.0, hx = 0.55, hz = 0.55, h = 1.0, r = 1.00, g = 0.84, dyn = true, solid = false, shape = "acorn" },
}

local FERRY = boxes[10]
local BADDIES = { { box = boxes[11], speed = 1.15, home = { x = 10, z = -10 }, alive = true, respawn = 14 } }
local ACORNS = { boxes[12], boxes[13], boxes[14], boxes[15], boxes[16], boxes[17] }
local GOLD = #ACORNS
local base_y = { 0.45, 1.40, nil, 1.50, 1.50, 1.90 }

game_score, game_lives, game_state = 0, 3, "playing"
game_world = 9
local collected, invuln = {}, 0

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
        b.cx = math.max(-12.9, math.min(12.9, b.cx))
        b.cz = math.max(-12.9, math.min(12.9, b.cz))
      end
      dx, dz = player.x - b.cx, player.z - b.cz
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
        push_x, push_z = dx / len * 2.2, dz / len * 2.2
        if game_lives <= 0 then game_state = "lost"; play_sound("land", 1.3) end
      end
    end
  end
end

function on_tick(t, dt, player)
  if game_state ~= "playing" then return end
  FERRY.cx = -4.0 + 8.0 * (0.5 + 0.5 * math.sin(t * math.pi * 2 / 7))
  FERRY.cy = 0.35 + 0.25 * math.sin(t * math.pi * 2 / 5)
  run_baddies(t, dt, player)
  for i, ac in ipairs(ACORNS) do
    if collected[i] then
      ac.cy = -10
    else
      local by = base_y[i]
      if by == nil then
        by = FERRY.cy + FERRY.h + 0.45
        ac.cx, ac.cz = FERRY.cx, FERRY.cz
      end
      ac.cy = by + 0.15 * math.sin(t * 2.2 + i)
      local dx, dz = player.x - ac.cx, player.z - ac.cz
      local dy = player.jump - ac.cy
      if dx * dx + dz * dz < 0.85 * 0.85 and dy > -0.7 and dy < 1.6 then
        collected[i] = true
        game_score = game_score + 1
        play_sound("blip", i == GOLD and 1.4 or 1.0)
        if game_score >= #ACORNS then
          game_state = "won"
          play_sound("jump", 1.25)
        end
      end
    end
  end
end

worlds = { "chunkins1.lua", "chunkins2.lua", "chunkins3.lua", "chunkins4.lua",
           "chunkins5.lua", "chunkins_hazelnut_bridges.lua", "chunkins7.lua",
           "chunkins8.lua", "chunkins_bridge_garden.lua" }
