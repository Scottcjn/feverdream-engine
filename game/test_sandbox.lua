-- SPDX-License-Identifier: MIT
-- test fixture: proves the level sandbox holds. If any banned capability
-- is reachable, the title screams; the suite greps for SANDBOX_OK.
local breached = (os ~= nil) or (io ~= nil) or (package ~= nil) or
                 (require ~= nil) or (dofile ~= nil) or (loadfile ~= nil) or
                 (load ~= nil) or (debug ~= nil)
-- the allowed kit must still work
local kit_ok = math ~= nil and string ~= nil and table ~= nil and
               type(play_sound) == "function"
game_title = (not breached and kit_ok) and "SANDBOX_OK" or "SANDBOX_BREACH"
boxes = {
  { cx = 0, cz = 2, hx = 0.26, hz = 0.26, h = 0.5, r = 0.78, g = 0.56, dyn = true, solid = false, shape = "acorn" },
}
game_score, game_lives, game_state = 0, 3, "playing"
local A = boxes[1]
function on_tick(t, dt, player)
  if game_state ~= "playing" then return end
  A.cy = 0.45
  local dx, dz = player.x - A.cx, player.z - A.cz
  if dx * dx + dz * dz < 0.85 * 0.85 then game_score, game_state = 1, "won" end
end
