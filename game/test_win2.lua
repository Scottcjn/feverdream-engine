-- SPDX-License-Identifier: MIT
-- test fixture: second link of the chain. The gametest asserts this title
-- appears, proving the live world swap worked end to end.
game_title = "LEVEL2OK"
boxes = {
  { cx = 0, cz = 3, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
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
