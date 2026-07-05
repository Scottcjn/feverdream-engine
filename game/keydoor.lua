-- SPDX-License-Identifier: MIT
-- keydoor.lua — KEY & DOOR: new gate mechanics on the Feverdream engine.
--
-- Three mechanics the earlier levels did not have, all built on the proven
-- host channels (dyn boxes stream every frame; the script sets game_score /
-- game_state / plays sounds; on_tick gets the live player):
--
--   1. KEY pickup      — walk over the gold key; it sinks out of view (dyn cy)
--                        and unlocks the door. +1 score, "blip".
--   2. LOCKED DOOR     — a dyn wall blocking the north exit. While locked it is
--                        solid; once the key is taken it SLIDES open (animated
--                        via cx every frame) so you can pass.
--   3. GOAL + HAZARD   — reach the pad past the door to win (game_state="won").
--                        The two side channels are open pits: fall in (feet
--                        below the floor) and you lose (game_state="lost").
--
-- All logic lives here; the C++ host provides sim/collision/render/audio.

config = {
  speed         = 4.4,
  turn_rate     = 2.7,
  step_rate     = 11.0,
  gravity       = -28.0,
  jump_v        = 9.6,
  player_radius = 0.45,
}

-- Indices into `boxes` are stable, so name the dynamic ones after we build the
-- table rather than hard-coding integers.
boxes = {
  -- boundary walls (static)
  { cx =  0.0, cz =  14.0, hx = 8.2, hz = 0.6, h = 2.2, r = 0.42, g = 0.40 }, -- north (has the door gap)
  { cx =  0.0, cz = -8.0,  hx = 8.2, hz = 0.6, h = 2.2, r = 0.42, g = 0.40 }, -- south
  { cx =  8.0, cz =  3.0,  hx = 0.6, hz = 11.0, h = 2.2, r = 0.42, g = 0.40 }, -- east
  { cx = -8.0, cz =  3.0,  hx = 0.6, hz = 11.0, h = 2.2, r = 0.42, g = 0.40 }, -- west
  -- pit edges: two channels flanking the centre lane (fall between them = lose)
  { cx = -3.2, cz =  2.0,  hx = 0.4, hz = 5.0, h = 0.3, r = 0.30, g = 0.30 },
  { cx =  3.2, cz =  2.0,  hx = 0.4, hz = 5.0, h = 0.3, r = 0.30, g = 0.30 },
  -- KEY: small gold cube, dyn so it can sink when collected
  { cx = -5.5, cz =  1.0,  hx = 0.35, hz = 0.35, h = 0.7, r = 0.95, g = 0.80, dyn = true },
  -- DOOR: dyn wall filling the north gap; slides east (+cx) to open
  { cx =  0.0, cz =  14.0, hx = 1.8, hz = 0.7, h = 2.2, r = 0.70, g = 0.20, dyn = true },
  -- GOAL pad: past the door, low glowing slab (static; on_tick checks proximity)
  { cx =  0.0, cz =  18.0, hx = 1.4, hz = 1.4, h = 0.25, r = 0.40, g = 0.95 },
}

local key  = boxes[#boxes - 2]
local door = boxes[#boxes - 1]
local goal = boxes[#boxes]

local door_home = door.cx
local have_key  = false
local finished  = false
game_score = 0
game_state = "play"

local function dist2(ax, az, bx, bz)
  local dx, dz = ax - bx, az - bz
  return dx * dx + dz * dz
end

function on_tick(t, dt, player)
  if finished then return end

  -- HAZARD: feet below the floor between the two pit edges = fell in a channel.
  -- player.jump is feet height; the lane floor is y=0, the pits read as y<-0.5.
  if player.jump < -0.5 then
    game_state = "lost"
    finished = true
    play_sound("bump", 0.9)
    return
  end

  -- 1. KEY pickup: proximity grab, then sink it out of view and unlock.
  if not have_key and dist2(player.x, player.z, key.cx, key.cz) < 0.9 * 0.9 then
    have_key = true
    key.cy = -9999            -- dyn channel: drop it below the world = "gone"
    game_score = game_score + 1
    play_sound("blip", 0.6)
  end

  -- 2. DOOR: locked -> sits in the gap (solid). Unlocked -> slide east to open.
  if have_key then
    local open_x = door_home + 3.6
    if door.cx < open_x then
      door.cx = math.min(open_x, door.cx + 4.0 * dt)   -- glide open, framerate-independent
      if door.cx >= open_x - 0.01 then play_sound("land", 0.5) end
    end
  end

  -- 3. GOAL: standing on the pad past the door wins (needs the door open to reach).
  if dist2(player.x, player.z, goal.cx, goal.cz) < 1.6 * 1.6 then
    game_state = "won"
    game_score = game_score + 5
    finished = true
    play_sound("land", 0.8)
  end
end
