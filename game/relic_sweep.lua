-- SPDX-License-Identifier: MIT
-- relic_sweep.lua — RELIC SWEEP: the first real Feverdream game.
--
-- Collect all 6 golden relics. Dodge the patrol slabs — they speed up with
-- every relic you take. Jump clears them (they're knee-high). 3 lives.
-- All game logic lives HERE; the C++ host provides sim/collision/render/audio.

config = {
  speed         = 4.6,
  turn_rate     = 2.8,
  step_rate     = 11.0,
  gravity       = -28.0,
  jump_v        = 9.5,
  player_radius = 0.45,
}

-- yard walls (static) + pillars, then 2 patrols + 6 relics (dyn)
boxes = {
  -- boundary: a 26x26 yard
  { cx =   0, cz =  13, hx = 13.6, hz = 0.6, h = 2.0, r = 0.45, g = 0.40 },
  { cx =   0, cz = -13, hx = 13.6, hz = 0.6, h = 2.0, r = 0.45, g = 0.40 },
  { cx =  13, cz =   0, hx = 0.6, hz = 12.4, h = 2.0, r = 0.45, g = 0.40 },
  { cx = -13, cz =   0, hx = 0.6, hz = 12.4, h = 2.0, r = 0.45, g = 0.40 },
  -- interior pillars (cover to break patrol lanes)
  { cx = -6, cz =  6, hx = 0.9, hz = 0.9, h = 2.6, r = 0.60, g = 0.42 },
  { cx =  6, cz = -6, hx = 0.9, hz = 0.9, h = 2.6, r = 0.60, g = 0.42 },
  -- patrols: knee-high slabs (h=0.8) — jumpable. dyn, script-driven.
  { cx =  0, cz =  4, hx = 1.5, hz = 0.7, h = 0.8, r = 0.80, g = 0.25, dyn = true },
  { cx =  0, cz = -7, hx = 1.5, hz = 0.7, h = 0.8, r = 0.80, g = 0.25, dyn = true },
  -- relics: small gold cubes, bobbing. First one dead ahead (gametest path).
  { cx =   0, cz =   3, hx = 0.28, hz = 0.28, h = 0.55, r = 1.0, g = 0.85, dyn = true },
  { cx =   8, cz =   8, hx = 0.28, hz = 0.28, h = 0.55, r = 1.0, g = 0.85, dyn = true },
  { cx =  -8, cz =   8, hx = 0.28, hz = 0.28, h = 0.55, r = 1.0, g = 0.85, dyn = true },
  { cx =   9, cz =  -9, hx = 0.28, hz = 0.28, h = 0.55, r = 1.0, g = 0.85, dyn = true },
  { cx =  -9, cz =  -9, hx = 0.28, hz = 0.28, h = 0.55, r = 1.0, g = 0.85, dyn = true },
  { cx =   0, cz = -11, hx = 0.28, hz = 0.28, h = 0.55, r = 1.0, g = 0.85, dyn = true },
}

local PATROLS = { boxes[7], boxes[8] }
local RELICS  = { boxes[9], boxes[10], boxes[11], boxes[12], boxes[13], boxes[14] }
local home_x  = {}                          -- relic home positions
for i, rl in ipairs(RELICS) do home_x[i] = { x = rl.cx, z = rl.cz } end

game_score, game_lives, game_state = 0, 3, "playing"
local collected = {}
local invuln = 0

function on_tick(t, dt, player)
  if game_state ~= "playing" then return end
  invuln = math.max(0, invuln - dt)

  -- relics bob in place; collected ones are sunk for good
  for i, rl in ipairs(RELICS) do
    if collected[i] then
      rl.cy = -10
    else
      rl.cy = 0.35 + 0.22 * math.sin(t * 2.4 + i * 1.3)
      local dx, dz = player.x - rl.cx, player.z - rl.cz
      if dx * dx + dz * dz < 0.85 * 0.85 then
        collected[i] = true
        game_score = game_score + 1
        play_sound("blip", 1.0)
        if game_score >= #RELICS then
          game_state = "won"
          play_sound("jump", 1.2)
        end
      end
    end
  end

  -- patrols sweep their lanes; every relic taken makes them 25% faster
  local rate = 1.0 + 0.25 * game_score
  PATROLS[1].cx =  10.5 * math.sin(t * 0.55 * rate)
  PATROLS[2].cx = -10.5 * math.sin(t * 0.50 * rate + 1.1)

  -- patrol hits: grounded contact costs a life + knockback; JUMPING CLEARS
  if invuln <= 0 and player.jump < 0.8 then
    for _, pt in ipairs(PATROLS) do
      local dx, dz = player.x - pt.cx, player.z - pt.cz
      if math.abs(dx) < pt.hx + 0.45 and math.abs(dz) < pt.hz + 0.45 then
        game_lives = game_lives - 1
        invuln = 1.5
        play_sound("bump", 1.2)
        local len = math.max(0.2, math.sqrt(dx * dx + dz * dz))
        push_x, push_z = dx / len * 2.2, dz / len * 2.2
        if game_lives <= 0 then
          game_state = "lost"
          play_sound("land", 1.3)
        end
        break
      end
    end
  end
end
