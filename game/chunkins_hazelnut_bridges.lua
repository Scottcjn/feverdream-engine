-- SPDX-License-Identifier: MIT
-- CHUNKINS: Hazelnut Bridges
-- Kid-friendly bridge gauntlet level for Feverdream Engine / CHUNKINS.
-- Designed for bounty Scottcjn/rustchain-bounties#14019.
-- Author: Lea Kryosys autonomous earn ops

-- A small, retryable world-5 style level: walk the +Z lane, collect six
-- acorns, bonk or avoid two slow baddies, and cross low bridges with guard
-- rails. The first acorn is directly on the +Z walk lane for headless
-- gametest reachability.

game_title = "Golden Acorn 6: Hazelnut Bridges"
next_level = "chunkins7.lua"   -- onward to Cascade Hollow

config = {
  speed = 4.3, turn_rate = 2.8, step_rate = 11.0,
  gravity = -26.0, jump_v = 9.8, player_radius = 0.45,
  step_up = 0.58,   -- integration: islands (h .40/.55) are meant to be walkable;
                    -- rails (top .95 from deck .35) stay walls
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
  { cx =  0.0, cz =  2.6, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" }, -- integration: moved to the FORWARD +Z lane (was -9.6, behind spawn) for the headless gametest
  { cx = -4.0, cz = -11.0, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx =  0.0, cz = -3.4, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx =  4.0, cz =  1.0, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx =  0.0, cz =  8.8, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx = -4.0, cz = 13.0, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  -- [25] HAZEL the squirrel-friend on the far island — she holds the STAR and
  -- hands it over once every acorn is gathered.
  { cx =  0.0, cz = 14.5, hx = 0.45, hz = 0.45, h = 0.6, cy = 0, dyn = true, r = 0.90, g = 0.52, shape = "npc" },
  -- [26] the STAR exit (Hazel reveals it when the count is full)
  { cx =  0.0, cz = 14.5, hx = 0.3, hz = 0.3, h = 0.5, cy = -10, dyn = true, solid = false, shape = "star" },
}

local HAZEL, STAR = boxes[25], boxes[26]
local BADDIES = {
  { box = boxes[17], speed = 1.25, home = {x=-3.8, z=-1.0}, alive = true, respawn = 8.0 },
  { box = boxes[18], speed = 1.10, home = {x= 4.2, z=12.4}, alive = true, respawn = 8.0 },
}
local ACORNS = { boxes[19], boxes[20], boxes[21], boxes[22], boxes[23], boxes[24] }
local base_y = { 0.85, 1.05, 0.55, 1.20, 0.95, 1.35 }   -- integration: #2,#3 into the collect window

game_score, game_lives, game_state = 0, 3, "playing"
game_world = 6
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
  HAZEL.cy = 0.06 * math.abs(math.sin(t * 3.5))     -- a friendly little hop

  local got = 0
  for i, ac in ipairs(ACORNS) do
    if collected[i] then
      ac.cy = -10; got = got + 1
    else
      ac.cy = base_y[i] + 0.15 * math.sin(t * 2.2 + i)
      -- acorn #4 is SHY: it scampers away across its island when you get near
      if i == 4 then
        local fx, fz = ac.cx - player.x, ac.cz - player.z
        local fd = math.sqrt(fx * fx + fz * fz)
        if fd < 2.6 and fd > 0.05 then
          ac.cx = math.max(-4.5, math.min(4.5, ac.cx + fx / fd * 3.0 * dt))
          ac.cz = math.max(-2.6, math.min(2.6, ac.cz + fz / fd * 3.0 * dt))
        end
      end
      local dx, dz = player.x - ac.cx, player.z - ac.cz
      local dy = player.jump - ac.cy
      if dx * dx + dz * dz < 0.85 * 0.85 and dy > -0.7 and dy < 1.6 then
        collected[i] = true
        game_score = game_score + 1
        play_sound("blip", 1.0)
      end
    end
  end

  -- every acorn gathered -> Hazel reveals the STAR; reach her to finish
  if got >= #ACORNS then
    STAR.cy = 0.9 + 0.20 * math.sin(t * 2.5)
    local dx, dz = player.x - STAR.cx, player.z - STAR.cz
    local dy = player.jump - STAR.cy
    if dx * dx + dz * dz < 1.0 * 1.0 and dy > -1.0 and dy < 1.8 then
      game_state = "won"
      play_sound("jump", 1.3)
    end
  end
end

worlds = { "chunkins1.lua", "chunkins2.lua", "chunkins3.lua", "chunkins4.lua", "chunkins5.lua", "chunkins_hazelnut_bridges.lua", "chunkins7.lua", "chunkins8.lua", "chunkins_bridge_garden.lua" }
