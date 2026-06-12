-- SPDX-License-Identifier: MIT
-- CHUNKINS: THE SEARCH FOR THE GOLDEN ACORN
-- World 4 — Thief's Hollow (the true finale)
--
-- The acorn on the mountain was PAINTED. The thief swapped it seasons ago,
-- and the trail ends here: his hollow in the deep woods. Two fast prowlers
-- guard the approach, the thief still works his trade, and somewhere past
-- the den roof gleams the REAL Golden Acorn. Good luck, little squirrel.

game_title = "Golden Acorn 4: Thief's Hollow"
next_level = "chunkins5.lua"   -- one more meadow: the windmill pass

config = {
  speed = 5.0, turn_rate = 3.0, step_rate = 12.0,
  gravity = -26.0, jump_v = 10.2, player_radius = 0.45,
}

boxes = {
  -- hedges
  { cx =   0, cz =  14, hx = 14.6, hz = 0.6, h = 1.0, r = 0.20, g = 0.48 },
  { cx =   0, cz = -14, hx = 14.6, hz = 0.6, h = 1.0, r = 0.20, g = 0.48 },
  { cx =  14, cz =   0, hx = 0.6, hz = 13.4, h = 1.0, r = 0.20, g = 0.48 },
  { cx = -14, cz =   0, hx = 0.6, hz = 13.4, h = 1.0, r = 0.20, g = 0.48 },
  -- the den: two thick walls and a roof slab you can walk UNDER (head-room)
  -- or climb ON via the platform ring
  { cx = -2.6, cz = 10.0, hx = 0.7, hz = 2.4, h = 2.2, r = 0.40, g = 0.30 },
  { cx =  2.6, cz = 10.0, hx = 0.7, hz = 2.4, h = 2.2, r = 0.40, g = 0.30 },
  { cx =  0.0, cz = 10.0, hx = 3.3, hz = 2.6, h = 0.5, r = 0.36, g = 0.26, cy = 2.2 },
  -- the platform ring: real jumps, real gaps
  { cx = -8.0, cz =  2.0, hx = 1.0, hz = 1.0, h = 1.0, r = 0.70, g = 0.46 },
  { cx = -9.5, cz =  6.0, hx = 1.0, hz = 1.0, h = 2.0, r = 0.68, g = 0.44 },
  { cx = -7.0, cz =  9.5, hx = 1.0, hz = 1.0, h = 3.0, r = 0.66, g = 0.42 },
  { cx =  7.0, cz =  9.5, hx = 1.0, hz = 1.0, h = 3.0, r = 0.66, g = 0.42 },
  { cx =  9.5, cz =  6.0, hx = 1.0, hz = 1.0, h = 2.0, r = 0.68, g = 0.44 },
  { cx =  8.0, cz =  2.0, hx = 1.0, hz = 1.0, h = 1.0, r = 0.70, g = 0.46 },
  -- the guards and the thief himself
  { cx =  11, cz = -11, hx = 0.45, hz = 0.55, h = 0.9, cy = 0, dyn = true, shape = "baddie" },
  { cx = -11, cz = -11, hx = 0.45, hz = 0.55, h = 0.9, cy = 0, dyn = true, shape = "baddie" },
  { cx =   0, cz =  8,  hx = 0.45, hz = 0.55, h = 0.9, cy = 0, dyn = true, shape = "baddie" },
  -- seven acorns scattered through the hollow...
  { cx =  0,   cz =  3,   hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx = -8.0, cz =  2.0, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx = -7.0, cz =  9.5, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx =  7.0, cz =  9.5, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx =  9.5, cz =  6.0, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx = -12,  cz = -6,   hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx =  12,  cz = -6,   hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  -- ...and THE REAL GOLDEN ACORN on the den roof, biggest of all
  { cx =  0, cz = 10.0, hx = 0.6, hz = 0.6, h = 1.2, r = 1.00, g = 0.84, dyn = true, solid = false, shape = "acorn" },
}

local BADDIES = {
  { box = boxes[14], speed = 3.0, home = {x=11,  z=-11}, alive = true, respawn = 8 },
  { box = boxes[15], speed = 2.5, home = {x=-11, z=-11}, alive = true, respawn = 10 },
  { box = boxes[16], speed = 2.4, home = {x=0,   z=8},   alive = true, respawn = 12, thief = true },
}
local ACORNS = { boxes[17], boxes[18], boxes[19], boxes[20], boxes[21],
                 boxes[22], boxes[23], boxes[24] }
local GOLD   = #ACORNS
local base_y = { 0.45, 1.45, 3.45, 3.45, 2.45, 0.45, 0.45, 3.15 }

game_score, game_lives, game_state = 0, 3, "playing"
game_world = 4
local collected, invuln = {}, 0
local stolen = nil

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
      local dirx, dirz = dx, dz
      if bd.thief and stolen then dirx, dirz = -dx, -dz end
      if bd.thief and not stolen then
        local best, bx, bz = nil, 0, 0
        for i, ac in ipairs(ACORNS) do
          if not collected[i] and i ~= GOLD and base_y[i] < 1.0 then
            local adx, adz = ac.cx - b.cx, ac.cz - b.cz
            local ad = adx * adx + adz * adz
            if not best or ad < best then best, bx, bz = ad, adx, adz end
          end
        end
        if best then
          dirx, dirz = bx, bz
          if best < 0.8 * 0.8 then
            for i, ac in ipairs(ACORNS) do
              if not collected[i] and i ~= GOLD and base_y[i] < 1.0 then
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
        b.cx = math.max(-12.9, math.min(12.9, b.cx))
        b.cz = math.max(-12.9, math.min(12.9, b.cz))
      end
      if bd.thief and stolen then
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
        if bd.thief and stolen then stolen = nil end
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
    elseif i == stolen then
      -- riding the thief
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
        play_sound("blip", i == GOLD and 1.5 or 1.0)
        if game_score >= #ACORNS then
          game_state = "won"           -- THE REAL GOLDEN ACORN IS FOUND
          play_sound("jump", 1.4)
        end
      end
    end
  end
end
