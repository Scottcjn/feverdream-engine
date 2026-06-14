-- SPDX-License-Identifier: MIT
-- CHUNKINS: THE SEARCH FOR THE GOLDEN ACORN
-- World 7 — Cascade Hollow (BIG: a waterfall hiding secrets, and the
-- GREAT OAK to climb, leaf pad by leaf pad)
--
-- The biggest meadow yet. A waterfall pours off the north cliff — walk
-- straight THROUGH the curtain to find what the water hides. The Great Oak
-- rises in the east: jump the leaf pads to the very top. Check your map
-- (bottom-right) — gold dots are acorns, red dots are trouble.

game_title = "Golden Acorn 7: Cascade Hollow"
next_level = "chunkins8.lua"   -- onward to the boss: THE MAGPIE KING

config = {
  speed = 5.2, turn_rate = 3.0, step_rate = 12.0,
  gravity = -26.0, jump_v = 10.4, player_radius = 0.45,
  step_up = 0.58,
}

boxes = {
  -- the big yard
  { cx =   0, cz =  24, hx = 24.6, hz = 0.6, h = 1.0, r = 0.20, g = 0.48 },
  { cx =   0, cz = -24, hx = 24.6, hz = 0.6, h = 1.0, r = 0.20, g = 0.48 },
  { cx =  24, cz =   0, hx = 0.6, hz = 23.4, h = 1.0, r = 0.20, g = 0.48 },
  { cx = -24, cz =   0, hx = 0.6, hz = 23.4, h = 1.0, r = 0.20, g = 0.48 },
  -- [5] the north cliff
  { cx =  0, cz = 23.0, hx = 9.0, hz = 2.0, h = 6.0, r = 0.48, g = 0.40 },  -- face at z=21: a walkable grotto behind the falls
  -- [6] the WATERFALL pouring off it (walk through the curtain!)
  { cx =  0, cz = 19.6, hx = 3.2, hz = 0.25, h = 5.6, dyn = true, solid = false, shape = "waterfall" },
  -- [7..9] THE GREAT OAK: trunk segments (east side)
  { cx = 14, cz =  4, hx = 1.2, hz = 1.2, h = 2.2, r = 0.40, g = 0.26 },
  { cx = 14, cz =  4, hx = 1.0, hz = 1.0, h = 2.0, r = 0.42, g = 0.28, cy = 2.2 },
  { cx = 14, cz =  4, hx = 0.8, hz = 0.8, h = 1.8, r = 0.44, g = 0.30, cy = 4.2 },
  -- [10..15] leaf pads spiraling up the oak
  { cx = 11.6, cz =  4.0, hx = 1.0, hz = 1.0, h = 0.4, r = 0.22, g = 0.55, shape = "leaf" },
  { cx = 12.4, cz =  6.6, hx = 1.0, hz = 1.0, h = 0.4, r = 0.20, g = 0.58, cy = 1.1, shape = "leaf" },
  { cx = 15.6, cz =  6.8, hx = 1.0, hz = 1.0, h = 0.4, r = 0.24, g = 0.52, cy = 2.2, shape = "leaf" },
  { cx = 16.6, cz =  4.0, hx = 1.0, hz = 1.0, h = 0.4, r = 0.20, g = 0.56, cy = 3.3, shape = "leaf" },
  { cx = 15.2, cz =  1.6, hx = 1.0, hz = 1.0, h = 0.4, r = 0.23, g = 0.54, cy = 4.4, shape = "leaf" },
  { cx = 14.0, cz =  4.0, hx = 1.3, hz = 1.3, h = 0.4, r = 0.18, g = 0.60, cy = 5.6, shape = "leaf" },  -- the crown
  -- [16..17] two patrollers in the open meadow
  { cx = -10, cz = -10, hx = 0.45, hz = 0.55, h = 0.9, cy = 0, dyn = true, shape = "baddie" },
  { cx =  10, cz = -14, hx = 0.45, hz = 0.55, h = 0.9, cy = 0, dyn = true, shape = "baddie" },
  -- [18] the heart, hidden BEHIND the waterfall
  { cx = -1.5, cz = 20.3, hx = 0.3, hz = 0.3, h = 0.5, dyn = true, solid = false, shape = "heart" },
  -- [19] the star, on the oak's crown
  { cx = 14.0, cz =  4.0, hx = 0.3, hz = 0.3, h = 0.5, dyn = true, solid = false, shape = "star" },
  -- [20..27] eight acorns: lane, meadow x3, leaf mid, oak crown, falls x2
  { cx =   0,   cz =   3,   hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx = -12,   cz =   6,   hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx = -18,   cz = -14,   hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx =  18,   cz = -18,   hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx = 15.6,  cz =  6.8,  hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx = 14.0,  cz =  4.0,  hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx =  1.5,  cz = 20.3,  hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx =  0.0,  cz = 18.6,  hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  -- [28] BOUNCE PAD (orange) at the oak's foot — spring up to the first leaf
  { cx = 11.6, cz =  1.4, hx = 1.0, hz = 1.0, h = 0.20, r = 0.95, g = 0.55 },
}

local FALLS = boxes[6]
local PAD   = boxes[28]
-- the middle leaf pads BOB — moving platforms up the Great Oak (ride them up)
local LEAVES   = { boxes[11], boxes[12], boxes[13], boxes[14] }
local LEAF_CY  = { 1.1, 2.2, 3.3, 4.4 }
local BADDIES = {
  { box = boxes[16], speed = 2.2, home = {x=-10, z=-10}, alive = true, respawn = 10 },
  { box = boxes[17], speed = 2.6, home = {x=10,  z=-14}, alive = true, respawn = 10 },
}
local HEART, STAR = boxes[18], boxes[19]
local ACORNS = { boxes[20], boxes[21], boxes[22], boxes[23],
                 boxes[24], boxes[25], boxes[26], boxes[27] }
local base_y = { 0.45, 0.45, 0.45, 0.45, 1.95, 6.45, 0.45, 0.45 }

game_score, game_lives, game_state = 0, 3, "playing"
game_world = 7
local collected, invuln = {}, 0
local heart_taken, star_taken, star_t = false, false, 0

worlds = { "chunkins1.lua", "chunkins2.lua", "chunkins3.lua", "chunkins4.lua",
           "chunkins5.lua", "chunkins_hazelnut_bridges.lua", "chunkins7.lua", "chunkins8.lua" }

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
        b.cx = math.max(-22.9, math.min(22.9, b.cx))
        b.cz = math.max(-22.9, math.min(22.9, b.cz))
      end
      dx, dz = player.x - b.cx, player.z - b.cz
      d = math.sqrt(dx * dx + dz * dz)
      local top = b.cy + b.h
      local over = math.abs(dx) < b.hx + 0.45 and math.abs(dz) < b.hz + 0.45
      if over and player.vy < -0.5 and player.jump > top - 0.30 then
        bd.alive = false; b.cy = -10; bd.timer = bd.respawn
        bounce = 7.5
        play_sound("bump", 1.0)
      elseif over and player.jump < top - 0.15 and invuln <= 0 and star_t <= 0 then
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

  -- the water falls (R = scroll phase; one full texture cycle per second)
  FALLS.ry = (FALLS.ry or 0) + 1.1 * dt

  -- the leaf pads bob — time your hops as they rise and dip (vertical only, so
  -- Chunkins rides them instead of sliding off)
  for i, lf in ipairs(LEAVES) do
    lf.cy = LEAF_CY[i] + 0.35 * math.sin(t * 1.2 + i * 1.3)
  end

  -- BOUNCE PAD: spring off the orange pad to reach the first leaf
  if player.grounded and
     math.abs(player.x - PAD.cx) < PAD.hx + 0.4 and
     math.abs(player.z - PAD.cz) < PAD.hz + 0.4 then
    bounce = 11.0
    play_sound("jump", 1.1)
  end

  -- star power
  star_t = math.max(0, star_t - dt)
  if star_t > 0 then speed_mult, jump_mult = 1.45, 1.18
  else speed_mult, jump_mult = 1.0, 1.0 end

  if not heart_taken then
    HEART.cy = 0.5 + 0.15 * math.sin(t * 2.4)
    local dx, dz = player.x - HEART.cx, player.z - HEART.cz
    if dx * dx + dz * dz < 0.85 * 0.85 then
      heart_taken = true; HEART.cy = -10
      game_lives = math.min(5, game_lives + 1)
      play_sound("blip", 1.3)
    end
  end
  if not star_taken then
    STAR.cy = 6.45 + 0.12 * math.sin(t * 3)
    STAR.ry = (STAR.ry or 0) + 160 * dt
    local dx, dz = player.x - STAR.cx, player.z - STAR.cz
    local dy = player.jump - STAR.cy
    if dx * dx + dz * dz < 0.9 * 0.9 and dy > -0.7 and dy < 1.6 then
      star_taken = true; STAR.cy = -10
      star_t = 8.0
      play_sound("jump", 1.4)
    end
  end

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
          play_sound("jump", 1.4)
        end
      end
    end
  end
end
