-- SPDX-License-Identifier: MIT
-- test fixture: instant-win level proving the level-chain machinery.
-- One acorn dead ahead -> won -> after the linger, host loads test_win2.
game_title = "LEVEL1"
next_level = "test_win2.lua"
boxes = {
  { cx = 0, cz = 2, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
}
game_score, game_lives, game_state = 0, 3, "playing"
local A = boxes[1]
function on_tick(t, dt, player)
  if game_state ~= "playing" then return end
  A.cy = 0.45
  local dx, dz = player.x - A.cx, player.z - A.cz
  if dx * dx + dz * dz < 0.85 * 0.85 then
    game_score, game_state = 1, "won"
  end
end
