CHUNKINS - The Search for the Golden Acorn  (Windows, complete & playable)
==========================================================================

HOW TO PLAY
  Double-click  run.bat
  (Two windows open: the renderer - minimized - and the game. That's normal.)

  Controls:
    WASD / arrow keys ... move and turn
    SPACE ............... jump  (jump on an enemy's head to bonk it)
    1 - 8 ............... jump straight to any world
    ESC ................ quit

  Goal: gather the acorns in each of the 8 worlds, reach the star, and in
  World 8 defeat THE MAGPIE KING to claim the real Golden Acorn.

WHAT'S IN HERE
  run.bat          <- double-click this to play
  fd-game.exe      the game
  fd-daemon.exe    the POV-Ray renderer (the game needs it to draw the picture)
  SDL2.dll         required library - keep it next to fd-game.exe
  chunkins*.lua    the eight worlds  /  arena.lua, splash.pov
  assets/          sound effects

WHY TWO PROGRAMS?
  Feverdream renders the whole game with a live POV-Ray raytracer. The game
  (fd-game) computes the world and sends each scene to the renderer (fd-daemon)
  over a LOCAL connection (127.0.0.1) - nothing leaves your PC. run.bat starts
  them together for you.

ANTIVIRUS / SMARTSCREEN
  These binaries are open source but UNSIGNED, so some scanners show a
  false-positive warning (a generic "unknown program" flag - not because they
  do anything harmful). See WINDOWS_ANTIVIRUS.md for details and how to report
  it. You can also build everything yourself from source:
      https://github.com/Scottcjn/feverdream-engine

  fd-game.exe (MIT) is the game. fd-daemon.exe bundles POV-Ray (AGPLv3); its
  source is in the repository above.
