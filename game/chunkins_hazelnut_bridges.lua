-- SPDX-License-Identifier: MIT
-- CHUNKINS: Hazelnut Bridges
-- Kid-friendly bridge gauntlet level for Feverdream Engine / CHUNKINS.
-- Designed for bounty Scottcjn/rustchain-bounties#14019.
-- Author: Lea Kryosys autonomous earn ops

-- A small, retryable world-5 style level: walk the +Z lane, collect six
-- acorns, bonk or avoid two slow baddies, and cross low bridges with guard
-- rails. The first acorn is directly on the +Z walk lane for headless
-- gametest reachability.

game_title = "Golden Acorn 5: Hazelnut Bridges"
next_level = "chunkins1.lua"

config = {
  speed = 4.3, turn_rate = 2.8, step_rate = 11.0,
  gravity = -26.0, jump_v = 9.8, player_radius = 0.45,
}

boxes = {
  -- outer hedges / soft boundaries
  { cx =   0, cz =  18, hx = 16.0, hz = 0.6, h = 1.0, r = 0.20, g = 0.48 },
  { cx =   0, cz = -18, hx = 16.0, hz = 0.6, h = 1.0, r = 0.20, g = 0.48 },
  { cx =  16, cz =   0, hx = 0.6, hz = 17.4, h = 1.0, r = 0.20, g = 0.48 },
  { cx = -16, cz =   0, hx = 0.6, hz = 17.4, h = 1.0, r = 0.20, g = 0.48 },

  -- three safe meadow islands connected by narrow bridges
  { cx =  0, cz = -12, hx = 5.5, hz = 3.2, h = 0.25, r = 0.38, g = 0.62 },
  { cx =  0, cz =   0, hx = 5.0, hz = 3.0, h = 0.40, r = 0.36, g = 0.58 },
  { cx =  0, cz =  12, hx = 5.5, hz = 3.2, h = 0.55, r = 0.38, g = 0.62 },

  -- bridges on the +Z lane: low enough for children, with visible rails
  { cx =  0, cz =  -6, hx = 1.2, hz = 3.3, h = 0.35, r = 0.62, g = 0.44 },
  { cx =  0, cz =   6, hx = 1.2, hz = 3.3, h = 0.50, r = 0.62, g = 0.44 },
  { cx = -1.55, cz = -6, hx = 0.18, hz = 3.3, h = 0.95, r = 0.55, g = 0.34 },
  { cx =  1.55, cz = -6, hx = 0.18, hz = 3.3, h = 0.95, r = 0.55, g = 0.34 },
  { cx = -1.55, cz =  6, hx = 0.18, hz = 3.3, h = 1.10, r = 0.55, g = 0.34 },
  { cx =  1.55, cz =  6, hx = 0.18, hz = 3.3, h = 1.10, r = 0.55, g = 0.34 },

  -- optional side crates: small jumps, not mandatory
  { cx = -4.0, cz = -11.0, hx = 0.9, hz = 0.9, h = 0.55, r = 0.70, g = 0.46 },
  { cx =  4.0, cz =   1.0, hx = 0.9, hz = 0.9, h = 0.70, r = 0.70, g = 0.46 },
  { cx = -4.0, cz =  13.0, hx = 0.9, hz = 0.9, h = 0.85, r = 0.70, g = 0.46 },

  -- two slow baddies, far enough from bridge starts to feel fair
  { cx = -3.8, cz =  -1.0, hx = 0.45, hz = 0.55, h = 0.9, cy = 0, dyn = true, shape = "baddie" },
  { cx =  4.2, cz =  12.4, hx = 0.45, hz = 0.55, h = 0.9, cy = 0, dyn = true, shape = "baddie" },

  -- six acorns. #1 is on the +Z lane for ./fd-game --gametest reachability.
  { cx =  0.0, cz = -9.6, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx = -4.0, cz = -11.0, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx =  0.0, cz = -3.4, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx =  4.0, cz =  1.0, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx =  0.0, cz =  8.8, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx = -4.0, cz = 13.0, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
}

local BADDIES = {
  { box = boxes[17], speed = 1.25, home = {x=-3.8, z=-1.0}, alive = true, respawn = 8.0 },
  { box = boxes[18], speed = 1.10, home = {x= 4.2, z=12.4}, alive = true, respawn = 8.0 },
}
local ACORNS = { boxes[19], boxes[20], boxes[21], boxes[22], boxes[23], boxes[24] }
local base_y = { 0.70, 1.10, 0.80, 1.20, 0.95, 1.35 }

game_score, game_lives, game_state = 0, 3, "playing"
game_world = 5
local collected, invuln = {}, 0

local function run_baddies(t, dt, player)
  invuln = math.max(0, invuln - dt)
  for _, bd in ipairs(BADDIES) do
    local b = bd.box
    if not bd.alive then
      bd.timer = (bd.timer or bd.respawn) - dt
      if bd.timer <= 0 then
        bd.alive, b.cy, b.cx, b.cz = true, 0, bd.home.x, bd.home.z
      end
    else
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
        bd.alive = false; b.cy = -10; bd.timer = bd.respawn
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
  run_baddies(t, dt, player)
  for i, ac in ipairs(ACORNS) do
    if collected[i] then
      ac.cy = -10
    else
      ac.cy = base_y[i] + 0.15 * math.sin(t * 2.2 + i)
      local dx, dz = player.x - ac.cx, player.z - ac.cz
      local dy = player.jump - ac.cy
      if dx * dx + dz * dz < 0.85 * 0.85 and dy > -0.7 and dy < 1.6 then
        collected[i] = true
        game_score = game_score + 1
        play_sound("blip", 1.0)
        if game_score >= #ACORNS then
          game_state = "won"
          play_sound("jump", 1.2)
        end
      end
    end
  end
end

worlds = { "chunkins1.lua", "chunkins2.lua", "chunkins3.lua", "chunkins4.lua", "chunkins_hazelnut_bridges.lua" }
