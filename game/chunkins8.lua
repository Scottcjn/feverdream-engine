-- SPDX-License-Identifier: MIT
-- CHUNKINS: THE SEARCH FOR THE GOLDEN ACORN
-- World 8 — THE MAGPIE KING (the boss: the master thief behind it all)
--
-- The little thief only ever worked for someone bigger. High on his roost sits
-- THE MAGPIE KING — the giant magpie who hoarded the REAL Golden Acorn all
-- along. He SWOOPS from above; dodge the dive, and while he's dazed on the
-- ground, leap onto his back to bonk him. Three good bonks and the Golden Acorn
-- is finally yours. Later he flings stolen acorns and calls in waddlers.
--
-- The fight loop:  he rears up (warning) -> SWOOPS at you -> dodge -> he's
-- DAZED (stompable) -> bonk his head. Repeat. He gets meaner each hit.

game_title = "Golden Acorn 8: The Magpie King"
-- terminal: the quest truly ends here

config = {
  speed = 5.0, turn_rate = 3.0, step_rate = 12.0,
  gravity = -26.0, jump_v = 9.8, player_radius = 0.45,
}

boxes = {
  -- 1-4: arena walls
  { cx =   0, cz =  13, hx = 13.6, hz = 0.6, h = 1.4, r = 0.34, g = 0.26 },
  { cx =   0, cz = -13, hx = 13.6, hz = 0.6, h = 1.4, r = 0.34, g = 0.26 },
  { cx =  13, cz =   0, hx = 0.6, hz = 12.4, h = 1.4, r = 0.34, g = 0.26 },
  { cx = -13, cz =   0, hx = 0.6, hz = 12.4, h = 1.4, r = 0.34, g = 0.26 },
  -- 5-6: two pillars for cover from swoops + tossed acorns
  { cx = -6, cz =  4, hx = 1.2, hz = 1.2, h = 2.2, r = 0.46, g = 0.34 },
  { cx =  6, cz =  4, hx = 1.2, hz = 1.2, h = 2.2, r = 0.46, g = 0.34 },
  -- 7-8: two bounce pads (reach his back / reposition fast)
  { cx = -5, cz = -4, hx = 1.0, hz = 1.0, h = 0.20, r = 0.95, g = 0.55 },
  { cx =  5, cz = -4, hx = 1.0, hz = 1.0, h = 0.20, r = 0.95, g = 0.55 },
  -- 9: THE MAGPIE KING (the boss)
  { cx = 0, cz = 6, hx = 1.1, hz = 1.1, h = 1.7, cy = 0, dyn = true, shape = "boss" },
  -- 10-12: three HP pips (stars up high — vanish as he's bonked)
  { cx = -2.0, cz = 11.0, hx = 0.3, hz = 0.3, h = 0.5, cy = 5.0, dyn = true, solid = false, shape = "star" },
  { cx =  0.0, cz = 11.0, hx = 0.3, hz = 0.3, h = 0.5, cy = 5.0, dyn = true, solid = false, shape = "star" },
  { cx =  2.0, cz = 11.0, hx = 0.3, hz = 0.3, h = 0.5, cy = 5.0, dyn = true, solid = false, shape = "star" },
  -- 13-15: tossed-acorn projectiles (hidden until used)
  { cx = 0, cz = 0, hx = 0.3, hz = 0.3, h = 0.5, cy = -10, dyn = true, solid = false, shape = "acorn" },
  { cx = 0, cz = 0, hx = 0.3, hz = 0.3, h = 0.5, cy = -10, dyn = true, solid = false, shape = "acorn" },
  { cx = 0, cz = 0, hx = 0.3, hz = 0.3, h = 0.5, cy = -10, dyn = true, solid = false, shape = "acorn" },
  -- 16-17: summoned waddlers (hidden until phase 3)
  { cx = 0, cz = 0, hx = 0.45, hz = 0.55, h = 0.9, cy = -10, dyn = true, shape = "baddie" },
  { cx = 0, cz = 0, hx = 0.45, hz = 0.55, h = 0.9, cy = -10, dyn = true, shape = "baddie" },
  -- 18: THE REAL GOLDEN ACORN (appears when the King is beaten)
  { cx = 0, cz = 0, hx = 0.6, hz = 0.6, h = 1.2, cy = -10, dyn = true, solid = false, shape = "acorn" },
  -- 19-21: arena acorns (one on the +Z lane for the headless gametest)
  { cx = 0,   cz = 3,  hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx = -9,  cz = -8, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx = 9,   cz = -8, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
}

local KING    = boxes[9]
local HOME    = { x = 0, z = 6 }
local PIPS    = { boxes[10], boxes[11], boxes[12] }
local TOSS    = { boxes[13], boxes[14], boxes[15] }
local MINIONS = {
  { box = boxes[16], alive = false, speed = 2.2 },
  { box = boxes[17], alive = false, speed = 2.2 },
}
local GOLD    = boxes[18]
local ACORNS  = { boxes[19], boxes[20], boxes[21] }

game_score, game_lives, game_state = 0, 4, "playing"
game_world = 8
local collected, invuln = {}, 0
local hp = 3
local state, st = "intro", 0          -- boss state machine
local lock_x, lock_z = 0, 0           -- swoop target (player's position at dive)
local toss_t = {}                     -- per-toss landing timers
local gold_out = false
local atk_n = 0                       -- attack counter (rotation, not RNG)
local defeated_done = false           -- one-shot defeat sound

-- bonk one of the two bounce pads -> spring up (reach his back, or reposition)
local function run_pads(player)
  if not player.grounded then return end
  for _, pad in ipairs({ boxes[7], boxes[8] }) do
    if math.abs(player.x - pad.cx) < pad.hx + 0.4 and
       math.abs(player.z - pad.cz) < pad.hz + 0.4 then
      bounce = 11.0; play_sound("jump", 1.0)
    end
  end
end

-- player takes a hit (knockback from a point), with i-frames
local function hurt(fromx, fromz, player)
  if invuln > 0 then return end
  game_lives = game_lives - 1; invuln = 1.6
  play_sound("bump", 1.3)
  local dx, dz = player.x - fromx, player.z - fromz
  local d = math.max(0.2, math.sqrt(dx * dx + dz * dz))
  push_x, push_z = dx / d * 3.0, dz / d * 3.0
  if game_lives <= 0 then game_state = "lost"; play_sound("land", 1.3) end
end

local function phase() return 4 - hp end   -- 1, 2, 3 as HP falls

-- pick the next attack by ROTATION (cycles through the phase's attacks so each
-- one comes up in turn, rather than a frame-rate-derived "roll"; a fast kill may
-- still end the fight before the last type appears) and do its one-shot setup
-- HERE, at the transition — never in a per-frame branch gated on elapsed time.
local function start_attack(player)
  atk_n = atk_n + 1
  local p = phase()
  local choices = { "swoop" }
  if p >= 2 then choices[#choices + 1] = "toss" end
  if p >= 3 then choices[#choices + 1] = "summon" end
  local pick = choices[((atk_n - 1) % #choices) + 1]

  if pick == "toss" then
    state, st = "toss", 0
    for i, ac in ipairs(TOSS) do          -- scatter three danger spots (deterministic, varies per attack)
      ac.cx = ((atk_n * 5 + i * 7) % 19) - 9
      ac.cz = ((atk_n * 11 + i * 3) % 17) - 8
      toss_t[i] = 0
    end
  elseif pick == "summon" then
    state, st = "summon", 0
    play_sound("blip", 1.2)
    for _, m in ipairs(MINIONS) do        -- spawn exactly ONE free waddler
      if not m.alive then
        m.alive, m.box.cy = true, 0
        m.box.cx, m.box.cz = KING.cx, KING.cz
        break
      end
    end
  else
    state, st = "rear", 0                  -- wind up the swoop
    lock_x, lock_z = player.x, player.z
  end
end

local function run_minions(dt, player)
  for _, m in ipairs(MINIONS) do
    if m.alive then
      local b = m.box
      local dx, dz = player.x - b.cx, player.z - b.cz
      local d = math.sqrt(dx * dx + dz * dz)
      if d > 0.05 then
        b.cx = b.cx + dx / d * m.speed * dt
        b.cz = b.cz + dz / d * m.speed * dt
        b.ry = math.deg(math.atan(dx, dz))
      end
      dx, dz = player.x - b.cx, player.z - b.cz
      local over = math.abs(dx) < b.hx + 0.45 and math.abs(dz) < b.hz + 0.45
      if over and player.vy < -0.5 and player.jump > b.h - 0.3 then
        m.alive = false; b.cy = -10; bounce = 7.0; play_sound("bump", 1.0)
      elseif over and player.jump < b.h - 0.15 then
        hurt(b.cx, b.cz, player)
      end
    end
  end
end

function on_tick(t, dt, player)
  if game_state ~= "playing" then return end
  invuln = math.max(0, invuln - dt)
  st = st + dt
  run_pads(player)
  run_minions(dt, player)

  -- HP pips bob and vanish as HP drops
  for i, pip in ipairs(PIPS) do
    pip.cy = (i <= hp) and (5.0 + 0.15 * math.sin(t * 3 + i)) or -10
    pip.ry = (pip.ry or 0) + 120 * dt
  end

  -- King faces Chunkins (except mid-dive)
  if state ~= "swoop" then
    local dx, dz = player.x - KING.cx, player.z - KING.cz
    KING.ry = math.deg(math.atan(dx, dz))
  end

  -- speed scales with phase (meaner each hit)
  local pace = 1.0 + 0.35 * (phase() - 1)

  if state == "intro" then
    KING.cy = 1.2 + 0.3 * math.sin(t * 2)          -- hovering menace
    if st > 2.5 then state = "idle"; st = 0 end

  elseif state == "idle" then
    KING.cy = 0.15 * math.abs(math.sin(t * 4))     -- perched, ready
    if st > (1.0 / pace) then start_attack(player) end

  elseif state == "rear" then
    KING.cy = 1.0 + 3.0 * math.min(1, st / 0.8)    -- WIND UP: rise high (telegraph)
    if st > 0.8 then
      state = "swoop"; st = 0
      lock_x, lock_z = player.x, player.z          -- lock onto where you ARE
    end

  elseif state == "swoop" then
    -- dive toward the locked spot, dropping to the ground
    local k = math.min(1, st * 2.2 * pace)
    KING.cx = KING.cx + (lock_x - KING.cx) * math.min(1, dt * 6 * pace)
    KING.cz = KING.cz + (lock_z - KING.cz) * math.min(1, dt * 6 * pace)
    KING.cy = 4.0 * (1 - k)
    KING.ry = math.deg(math.atan(lock_x - KING.cx, lock_z - KING.cz))
    -- a direct hit on the way down hurts
    local dx, dz = player.x - KING.cx, player.z - KING.cz
    if dx*dx + dz*dz < 1.4*1.4 and KING.cy < 1.6 then hurt(KING.cx, KING.cz, player) end
    if k >= 1 then state = "dazed"; st = 0 end

  elseif state == "dazed" then
    KING.cy = 0                                     -- grounded, stompable window
    -- BONK: leap onto his back while he's dazed
    local dx, dz = player.x - KING.cx, player.z - KING.cz
    local over = math.abs(dx) < KING.hx + 0.5 and math.abs(dz) < KING.hz + 0.5
    if over and player.vy < -0.4 and player.jump > KING.h - 0.4 then
      hp = hp - 1; play_sound("bump", 1.2); bounce = 9.0
      state = "hurt"; st = 0
    elseif over and player.jump < KING.h - 0.2 then
      hurt(KING.cx, KING.cz, player)               -- walked into him: still dangerous
    end
    if st > 1.6 then state = "return"; st = 0 end   -- window closes, he lifts off

  elseif state == "hurt" then
    KING.cy = 0.6 * math.sin(math.min(st, 0.5) * 6.28)  -- recoil shudder
    if st > 0.6 then
      if hp <= 0 then state = "defeated"; st = 0
      else state = "return"; st = 0 end
    end

  elseif state == "return" then
    KING.cx = KING.cx + (HOME.x - KING.cx) * math.min(1, dt * 4)
    KING.cz = KING.cz + (HOME.z - KING.cz) * math.min(1, dt * 4)
    KING.cy = 0.15 * math.abs(math.sin(t * 4))
    if math.abs(KING.cx - HOME.x) < 0.3 and math.abs(KING.cz - HOME.z) < 0.3 then
      state = "idle"; st = 0
    end

  elseif state == "toss" then
    -- three stolen acorns (placed at the transition) fall, then flash as danger
    local done = true
    for i, ac in ipairs(TOSS) do
      toss_t[i] = (toss_t[i] or 0) + dt
      if toss_t[i] < 0.7 then
        ac.cy = 4.0 * (1 - toss_t[i] / 0.7)         -- falling (telegraph)
        done = false
      elseif toss_t[i] < 1.2 then
        ac.cy = 0.2                                  -- impact danger zone
        local dx, dz = player.x - ac.cx, player.z - ac.cz
        if dx*dx + dz*dz < 1.1*1.1 then hurt(ac.cx, ac.cz, player) end
        done = false
      else
        ac.cy = -10
      end
    end
    if done then state = "idle"; st = 0 end

  elseif state == "summon" then
    KING.cy = 1.0 + 0.3 * math.sin(t * 4)          -- the waddler was spawned at the transition
    if st > 0.8 then state = "idle"; st = 0 end

  elseif state == "defeated" then
    if not defeated_done then play_sound("land", 1.2); defeated_done = true end  -- one-shot
    KING.cy = math.max(-3, KING.cy - dt * 3)        -- crumples
    if not gold_out then
      gold_out = true
      GOLD.cx, GOLD.cz = 0, 6
    end
  end

  -- a lethal hit this tick (minion/swoop/toss/contact) ends it — don't let the
  -- prize/acorn collisions below overwrite a "lost" with a "won" in the same tick
  if game_state ~= "playing" then return end

  -- the prize: once beaten, the Golden Acorn settles in the King's roost
  if gold_out then
    GOLD.cy = 1.0 + 0.2 * math.sin(t * 2.5)
    local dx, dz = player.x - GOLD.cx, player.z - GOLD.cz
    local dy = player.jump - GOLD.cy
    if dx*dx + dz*dz < 1.0*1.0 and dy > -1.0 and dy < 1.8 then
      game_state = "won"; play_sound("jump", 1.5)
    end
  end

  -- arena acorns (also the +Z-lane one the gametest collects)
  for i, ac in ipairs(ACORNS) do
    if collected[i] then ac.cy = -10
    else
      ac.cy = 0.45 + 0.15 * math.sin(t * 2.2 + i)
      local dx, dz = player.x - ac.cx, player.z - ac.cz
      local dy = player.jump - ac.cy
      if dx*dx + dz*dz < 0.85*0.85 and dy > -0.7 and dy < 1.6 then
        collected[i] = true; game_score = game_score + 1; play_sound("blip", 1.0)
      end
    end
  end
end

worlds = { "chunkins1.lua", "chunkins2.lua", "chunkins3.lua", "chunkins4.lua",
           "chunkins5.lua", "chunkins_hazelnut_bridges.lua", "chunkins7.lua",
           "chunkins8.lua", "chunkins_bridge_garden.lua" }
