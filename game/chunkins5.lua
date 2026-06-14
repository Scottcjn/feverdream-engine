-- SPDX-License-Identifier: MIT
-- CHUNKINS: THE SEARCH FOR THE GOLDEN ACORN
-- World 5 — Windmill Pass (rotating platforms, BISCUIT the guard dog,
-- and the first power-ups: a heart and a STAR)
--
-- Past the thief's hollow lies the old windmill meadow where he stashed
-- everything else he ever stole. Sweeping arms turn at ground level (jump
-- them!), a rotating bridge crosses to the stash tower, and the whole place
-- is guarded by BISCUIT — the thief's enormous, extremely enthusiastic dog,
-- chained to his post. Bonk him to stun him; nothing keeps Biscuit down.
-- Grab the STAR to go fast and fearless. The heart? You may need it.

game_title = "Golden Acorn 5: Windmill Pass"
next_level = "chunkins_hazelnut_bridges.lua"   -- community world 6!

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
  -- [5] ground sweeper: a windmill arm at ankle height — JUMP IT as it sweeps
  { cx = -6.0, cz =  4.0, hx = 3.0, hz = 0.5, h = 0.55, r = 0.75, g = 0.42, dyn = true },
  -- [6] the rotating bridge, elevated — cross it while it turns
  { cx = -7.0, cz =  9.0, hx = 2.8, hz = 0.6, h = 0.45, r = 0.70, g = 0.40, cy = 1.6, dyn = true },
  -- [7] square spinner platform: stand on it, the floor turns under your feet
  { cx =  9.0, cz =  2.0, hx = 1.5, hz = 1.5, h = 1.0, r = 0.66, g = 0.44, dyn = true },
  -- [8] stash tower (reachable from the rotating bridge)
  { cx = -11.5, cz = 11.0, hx = 1.4, hz = 1.4, h = 2.6, r = 0.60, g = 0.38 },
  -- [9] Biscuit's post
  { cx =  5.5, cz =  9.5, hx = 0.30, hz = 0.30, h = 1.5, r = 0.30, g = 0.22 },
  -- [10] BISCUIT
  { cx =  5.5, cz =  8.0, hx = 0.50, hz = 0.55, h = 0.95, cy = 0, dyn = true, shape = "chomp" },
  -- [11] the HEART (extra life), deep in Biscuit's yard
  { cx =  9.5, cz = 12.0, hx = 0.3, hz = 0.3, h = 0.5, dyn = true, solid = false, shape = "heart" },
  -- [12] the STAR, atop the spinner platform
  { cx =  9.0, cz =  2.0, hx = 0.3, hz = 0.3, h = 0.5, dyn = true, solid = false, shape = "star" },
  -- [13..19] seven acorns: lane, midfield x2, bridge ledge, tower top, Biscuit's stash x2
  { cx =  0,    cz =  3,   hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx = -3.5,  cz = -3,   hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx =  4,    cz = -7,   hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx = -7.0,  cz =  9.0, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx = -11.5, cz = 11.0, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx =  4.5,  cz = 11.5, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  { cx =  7.0,  cz =  7.5, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
  -- [20] WHISKERS the cat (friendly) — reach her and she yowls Biscuit into a
  -- long stun so you can raid his yard. She needs a moment between yowls.
  { cx =  2.5,  cz =  6.0, hx = 0.45, hz = 0.45, h = 0.55, cy = 0, dyn = true, r = 0.92, g = 0.92, shape = "npc" },
  -- [21] BOUNCE PAD (orange) at the stash-tower base — spring up to the top
  { cx = -11.5, cz =  8.4, hx = 1.0, hz = 1.0, h = 0.20, r = 0.95, g = 0.55 },
}

local WHISKERS, PAD = boxes[20], boxes[21]
local whisker_cd = 0
local SWEEP, BRIDGE, SPIN = boxes[5], boxes[6], boxes[7]
local BISCUIT, HOME, CHAIN_R = boxes[10], { x = 5.5, z = 9.5 }, 3.6
local HEART, STAR = boxes[11], boxes[12]
local ACORNS = { boxes[13], boxes[14], boxes[15], boxes[16], boxes[17], boxes[18], boxes[19] }
local base_y = { 0.45, 0.45, 0.45, 2.50, 3.05, 0.45, 0.45 }

game_score, game_lives, game_state = 0, 3, "playing"
game_world = 5
local collected, invuln = {}, 0
local heart_taken, star_taken, star_t = false, false, 0
local stun, recoil = 0, 0

function on_tick(t, dt, player)
  if game_state ~= "playing" then return end
  invuln = math.max(0, invuln - dt)

  -- the machinery turns
  SWEEP.ry  = (SWEEP.ry or 0) + 55 * dt
  BRIDGE.ry = (BRIDGE.ry or 0) - 38 * dt
  SPIN.ry   = (SPIN.ry or 0) + 30 * dt

  -- STAR power: fast, springy, fearless (8 seconds)
  star_t = math.max(0, star_t - dt)
  if star_t > 0 then speed_mult, jump_mult = 1.45, 1.18
  else speed_mult, jump_mult = 1.0, 1.0 end

  -- WHISKERS the cat: walk up to her and she yowls Biscuit into a long stun
  -- (with a cooldown). A friendly NPC who turns the fight in your favor.
  whisker_cd = math.max(0, whisker_cd - dt)
  WHISKERS.cy = 0.06 * math.abs(math.sin(t * 4))     -- a flicking tail
  do
    local wdx, wdz = player.x - WHISKERS.cx, player.z - WHISKERS.cz
    if wdx * wdx + wdz * wdz < 2.2 * 2.2 and whisker_cd <= 0 then
      stun = 4.0; whisker_cd = 8.0
      play_sound("blip", 1.4)
    end
  end

  -- BOUNCE PAD: stand on the orange pad to spring up to the stash tower
  if player.grounded and
     math.abs(player.x - PAD.cx) < PAD.hx + 0.4 and
     math.abs(player.z - PAD.cz) < PAD.hz + 0.4 then
    bounce = 11.0
    play_sound("jump", 1.1)
  end

  -- BISCUIT: eager hops at his post; lunges when Chunkins wanders close;
  -- the chain always wins. Bonks only stun him.
  stun = math.max(0, stun - dt); recoil = math.max(0, recoil - dt)
  local b = BISCUIT
  local dx, dz = player.x - b.cx, player.z - b.cz
  local d = math.sqrt(dx * dx + dz * dz)
  if stun <= 0 then
    b.ry = math.deg(math.atan(dx, dz))
    if d < 6.0 and recoil <= 0 then
      b.cx = b.cx + dx / math.max(d, 0.1) * 7.2 * dt     -- LUNGE
      b.cz = b.cz + dz / math.max(d, 0.1) * 7.2 * dt
    else
      local hx_, hz_ = b.cx - HOME.x, b.cz - HOME.z
      local hd = math.sqrt(hx_ * hx_ + hz_ * hz_)
      if hd > 0.4 then
        b.cx = b.cx - hx_ / hd * 2.2 * dt                -- amble home
        b.cz = b.cz - hz_ / hd * 2.2 * dt
      end
    end
    local hx_, hz_ = b.cx - HOME.x, b.cz - HOME.z
    local hd = math.sqrt(hx_ * hx_ + hz_ * hz_)
    if hd > CHAIN_R then                                  -- the chain wins
      b.cx = HOME.x + hx_ / hd * CHAIN_R
      b.cz = HOME.z + hz_ / hd * CHAIN_R
      if recoil <= 0 then recoil = 1.1; play_sound("bump", 0.5) end
    end
    b.cy = 0.12 * math.abs(math.sin(t * 6))               -- eager hopping
  end
  -- contact with Biscuit
  dx, dz = player.x - b.cx, player.z - b.cz
  local top = b.cy + b.h
  local over = math.abs(dx) < b.hx + 0.5 and math.abs(dz) < b.hz + 0.5
  if over and player.vy < -0.5 and player.jump > top - 0.30 then
    stun = 1.6; bounce = 8.2                              -- bonk = stun only!
    play_sound("bump", 1.0)
  elseif over and player.jump < top - 0.15 and invuln <= 0 and star_t <= 0 then
    game_lives = game_lives - 1; invuln = 1.5
    play_sound("bump", 1.3)
    local len = math.max(0.2, math.sqrt(dx * dx + dz * dz))
    push_x, push_z = dx / len * 3.0, dz / len * 3.0
    if game_lives <= 0 then game_state = "lost"; play_sound("land", 1.3) end
  end

  -- the HEART: +1 life (up to 5)
  if not heart_taken then
    HEART.cy = 0.5 + 0.15 * math.sin(t * 2.4)
    local hdx, hdz = player.x - HEART.cx, player.z - HEART.cz
    if hdx * hdx + hdz * hdz < 0.85 * 0.85 then
      heart_taken = true; HEART.cy = -10
      game_lives = math.min(5, game_lives + 1)
      play_sound("blip", 1.3)
    end
  end

  -- the STAR: ride the spinner to claim it
  if not star_taken then
    STAR.cy = SPIN.h + 0.55 + 0.12 * math.sin(t * 3)
    STAR.ry = (STAR.ry or 0) + 160 * dt
    local sdx, sdz = player.x - STAR.cx, player.z - STAR.cz
    local sdy = player.jump - STAR.cy
    if sdx * sdx + sdz * sdz < 0.9 * 0.9 and sdy > -0.7 and sdy < 1.6 then
      star_taken = true; STAR.cy = -10
      star_t = 8.0
      play_sound("jump", 1.4)
    end
  end

  -- acorns
  for i, ac in ipairs(ACORNS) do
    if collected[i] then
      ac.cy = -10
    else
      ac.cy = base_y[i] + 0.15 * math.sin(t * 2.2 + i)
      local adx, adz = player.x - ac.cx, player.z - ac.cz
      local ady = player.jump - ac.cy
      if adx * adx + adz * adz < 0.85 * 0.85 and ady > -0.7 and ady < 1.6 then
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

worlds = { "chunkins1.lua", "chunkins2.lua", "chunkins3.lua", "chunkins4.lua", "chunkins5.lua", "chunkins_hazelnut_bridges.lua", "chunkins7.lua", "chunkins8.lua" }
