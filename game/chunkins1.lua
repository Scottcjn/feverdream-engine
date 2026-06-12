-- SPDX-License-Identifier: MIT
-- CHUNKINS: THE SEARCH FOR THE GOLDEN ACORN
-- Level 1 — Acorn Meadow (easy: learn to run, jump, and stomp)
--
-- Legend says a Golden Acorn waits at the top of Acorn Mountain. The path
-- starts here: a sunny meadow, four acorns, and one sleepy waddler who'd
-- rather Chunkins didn't have any of them. Jump on his head to bonk him!

game_title = "Golden Acorn 1: Acorn Meadow"
next_level = "chunkins2.lua"

config = {
  speed = 4.4, turn_rate = 2.8, step_rate = 11.0,
  gravity = -26.0, jump_v = 9.8, player_radius = 0.45,
}

boxes = {
  -- hedges
  { cx =   0, cz =  14, hx = 14.6, hz = 0.6, h = 1.0, r = 0.20, g = 0.48 },
  { cx =   0, cz = -14, hx = 14.6, hz = 0.6, h = 1.0, r = 0.20, g = 0.48 },
  { cx =  14, cz =   0, hx = 0.6, hz = 13.4, h = 1.0, r = 0.20, g = 0.48 },
  { cx = -14, cz =   0, hx = 0.6, hz = 13.4, h = 1.0, r = 0.20, g = 0.48 },
  -- two gentle crates
  { cx =  5, cz =  5, hx = 1.1, hz = 1.1, h = 0.30, r = 0.72, g = 0.48 },
  { cx = -6, cz = -5, hx = 1.1, hz = 1.1, h = 0.60, r = 0.70, g = 0.46 },
  -- the waddler (slow, no respawn — one good bonk and the meadow is safe)
  { cx = 10, cz = -10, hx = 0.45, hz = 0.55, h = 0.9, cy = 0, dyn = true, shape = "baddie" },
  -- four acorns
  { cx =  0, cz =  3, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx =  5, cz =  5, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx = -8, cz =  6, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx = -6, cz = -5, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
}

local BADDIES = { { box = boxes[7], speed = 1.5, home = {x=10, z=-10}, alive = true } }
local ACORNS  = { boxes[8], boxes[9], boxes[10], boxes[11] }
local base_y  = { 0.45, 0.75, 0.45, 1.05 }     -- two sit on their crates

game_score, game_lives, game_state = 0, 3, "playing"
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
        b.ry = math.deg(math.atan(dx, dz))     -- face Chunkins
      end
      dx, dz = player.x - b.cx, player.z - b.cz   -- fresh after the move
      d = math.sqrt(dx * dx + dz * dz)
      local top = b.cy + b.h
      local over = math.abs(dx) < b.hx + 0.45 and math.abs(dz) < b.hz + 0.45
      if over and player.vy < -0.5 and player.jump > top - 0.30 then
        bd.alive = false; b.cy = -10           -- BONK!
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
