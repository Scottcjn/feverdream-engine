@echo off
REM ===========================================================================
REM  CHUNKINS - The Search for the Golden Acorn   (double-click this to play)
REM  Starts the POV-Ray renderer (fd-daemon) and then the game (fd-game).
REM  Close the game window to quit; the renderer closes with it.
REM ===========================================================================
cd /d "%~dp0"

echo Starting the renderer...
start "Feverdream renderer" /min fd-daemon.exe 47999

REM give the renderer a moment to start up and open its port
ping -n 3 127.0.0.1 >nul

echo Starting Chunkins - have fun!
REM v0.4.0 workaround: the default interactive splash can stay black on some
REM Windows systems even though the level renders fine. Force the proven
REM gameplay path until the splash-path bug is fixed in-engine.
set "FD_NOSPLASH=1"
fd-game.exe 47999 1280 720 4 chunkins1.lua

REM game closed: stop the renderer
taskkill /im fd-daemon.exe /f >nul 2>&1
