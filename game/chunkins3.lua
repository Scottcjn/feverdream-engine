-- SPDX-License-Identifier: MIT
-- CHUNKINS: THE SEARCH FOR THE GOLDEN ACORN
-- Level 3 — Acorn Mountain (hard: the summit, the thief, THE GOLDEN ACORN)
--
-- This is it. A spiral of ledges winds up the mountain. A fast chaser hunts
-- the ground. And the THIEF — he steals acorns and carries them on his back;
-- bonk him to make him drop his loot, or snatch it right off him mid-jump.
-- At the very top: the Golden Acorn.

game_title = "Golden Acorn 3: Acorn Mountain"
-- plot twist: the summit acorn turns out to be PAINTED — the thief swapped
-- it seasons ago. The trail leads on to his hollow...
next_level = "chunkins4.lua"

config = {
  speed = 4.8, turn_rate = 2.9, step_rate = 11.5,
  gravity = -26.0, jump_v = 10.2, player_radius = 0.45,
}

boxes = {
  -- hedges
  { cx =   0, cz =  14, hx = 14.6, hz = 0.6, h = 1.0, r = 0.20, g = 0.48 },
  { cx =   0, cz = -14, hx = 14.6, hz = 0.6, h = 1.0, r = 0.20, g = 0.48 },
  { cx =  14, cz =   0, hx = 0.6, hz = 13.4, h = 1.0, r = 0.20, g = 0.48 },
  { cx = -14, cz =   0, hx = 0.6, hz = 13.4, h = 1.0, r = 0.20, g = 0.48 },
  -- the mountain core
  { cx =  0, cz =  7, hx = 2.4, hz = 2.4, h = 4.4, r = 0.52, g = 0.40 },
  -- the spiral ascent — WIDE tops so a hop lands square (no sliding off a ledge)
  { cx = -4.5, cz =  3.5, hx = 1.5, hz = 1.5, h = 0.8, r = 0.70, g = 0.46 },
  { cx = -5.5, cz =  7.0, hx = 1.5, hz = 1.5, h = 1.6, r = 0.68, g = 0.44 },
  { cx = -3.5, cz = 10.5, hx = 1.5, hz = 1.5, h = 2.4, r = 0.66, g = 0.42 },
  { cx =  0.5, cz = 11.5, hx = 1.6, hz = 1.6, h = 3.2, r = 0.64, g = 0.41 },
  { cx =  4.0, cz = 10.0, hx = 1.5, hz = 1.5, h = 4.0, r = 0.62, g = 0.40 },
  -- the chaser (fast, relentless, respawns) and THE THIEF
  { cx =  11, cz = -11, hx = 0.45, hz = 0.55, h = 0.9, cy = 0, dyn = true, shape = "baddie" },
  { cx = -11, cz = -8,  hx = 0.45, hz = 0.55, h = 0.9, cy = 0, dyn = true, shape = "baddie" },
  -- five acorns on the way up...
  { cx =  0,   cz =  3,   hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx = -4.5, cz =  3.5, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx = -3.5, cz = 10.5, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx =  4.0, cz = 10.0, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx = -9,   cz = -9,   hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  -- ...and THE GOLDEN ACORN on the summit, twice the size, gleaming
  { cx =  0, cz =  7, hx = 0.5, hz = 0.5, h = 1.0, r = 1.00, g = 0.84, dyn = true, solid = false, shape = "acorn" },
}

local BADDIES = {
  { box = boxes[11], speed = 2.8, home = {x=11,  z=-11}, alive = true, respawn = 10 },
  { box = boxes[12], speed = 2.2, home = {x=-11, z=-8},  alive = true, respawn = 14, thief = true },
}
local ACORNS  = { boxes[13], boxes[14], boxes[15], boxes[16], boxes[17], boxes[18] }
local base_y  = { 0.45, 1.25, 2.85, 4.45, 0.45, 4.85 }   -- last = summit gold

game_score, game_lives, game_state = 0, 3, "playing"
game_world = 3
local collected, invuln = {}, 0
local stolen = nil          -- acorn index riding the thief's back

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
      -- the thief runs FROM Chunkins when carrying; everyone else chases
      local dx, dz = player.x - b.cx, player.z - b.cz
      local d = math.sqrt(dx * dx + dz * dz)
      local dirx, dirz = dx, dz
      if bd.thief and stolen then dirx, dirz = -dx, -dz end
      if bd.thief and not stolen then
        -- thief beelines for the nearest uncollected ground acorn
        local best, bx, bz = nil, 0, 0
        for i, ac in ipairs(ACORNS) do
          if not collected[i] and i ~= 6 and base_y[i] < 1.0 then
            local adx, adz = ac.cx - b.cx, ac.cz - b.cz
            local ad = adx * adx + adz * adz
            if not best or ad < best then best, bx, bz = ad, adx, adz end
          end
        end
        if best then
          dirx, dirz = bx, bz
          if best < 0.8 * 0.8 then       -- snatch!
            for i, ac in ipairs(ACORNS) do
              if not collected[i] and i ~= 6 and base_y[i] < 1.0 then
                local adx, adz = ac.cx - b.cx, ac.cz - b.cz
                if adx * adx + adz * adz < 0.8 * 0.8 then
                  stolen = i; play_sound("blip", 0.6); break
                end
              end
            end
          end
        end
      end
      local dl = math.sqrt(dirx * dirx + dirz * dirz)
      if dl > 0.05 then
        b.cx = b.cx + dirx / dl * bd.speed * dt
        b.cz = b.cz + dirz / dl * bd.speed * dt
        b.ry = math.deg(math.atan(dirx, dirz))
        -- hedges keep baddies in the yard too
        b.cx = math.max(-12.9, math.min(12.9, b.cx))
        b.cz = math.max(-12.9, math.min(12.9, b.cz))
      end
      if bd.thief and stolen then       -- loot rides the thief's back
        local ac = ACORNS[stolen]
        ac.cx, ac.cz, ac.cy = b.cx, b.cz, b.cy + b.h + 0.35
      end
      dx, dz = player.x - b.cx, player.z - b.cz   -- fresh after the move
      d = math.sqrt(dx * dx + dz * dz)
      local top = b.cy + b.h
      local over = math.abs(dx) < b.hx + 0.45 and math.abs(dz) < b.hz + 0.45
      if over and player.vy < -0.5 and player.jump > top - 0.30 then
        bd.alive = false; b.cy = -10
        if bd.respawn then bd.timer = bd.respawn end
        if bd.thief and stolen then stolen = nil end   -- drops the loot HERE
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

-- high on the mountain the wind shoves you sideways — mind your footing
local function run_wind(t, dt, player)
  if player.jump > 2.5 then
    local g = math.sin(t * 0.8) * 3.2 * dt
    push_x = (push_x or 0) + g
    push_z = (push_z or 0) + g * 0.4
  end
end

function on_tick(t, dt, player)
  if game_state ~= "playing" then return end
  run_baddies(t, dt, player)
  run_wind(t, dt, player)
  for i, ac in ipairs(ACORNS) do
    if collected[i] then
      ac.cy = -10
    elseif i == stolen then
      -- riding the thief; still snatchable mid-jump (handled below)
    else
      ac.cy = base_y[i] + 0.15 * math.sin(t * 2.2 + i)
    end
    if not collected[i] then
      local dx, dz = player.x - ac.cx, player.z - ac.cz
      local dy = player.jump - ac.cy
      if dx * dx + dz * dz < 0.85 * 0.85 and dy > -0.7 and dy < 1.6 then
        collected[i] = true
        if i == stolen then stolen = nil end
        game_score = game_score + 1
        play_sound("blip", i == 6 and 1.4 or 1.0)
        if game_score >= #ACORNS then
          game_state = "won"           -- THE GOLDEN ACORN IS FOUND
          play_sound("jump", 1.3)
        end
      end
    end
  end
end
