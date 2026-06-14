@echo off
REM verify-win.bat — end-to-end native Windows test. Starts fd-daemon.exe and
REM captures its EXACT pid (so we never kill an unrelated daemon), runs
REM fd-game.exe --gametest against it over TCP loopback, and passes only if the
REM game prints "REAL FRAME". Paths derive from this script's location.
setlocal
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set PORT=47999
set SDL_AUDIODRIVER=dummy
set SDL_VIDEODRIVER=dummy
set "DLOG=%ROOT%\daemon\daemon-run.log"
set "PIDFILE=%ROOT%\daemon\daemon.pid"
del /q "%PIDFILE%" 2>nul

echo --- starting fd-daemon.exe on port %PORT% (capturing exact pid) ---
REM Start-Process -PassThru gives the real child pid (the daemon is a console
REM app, so redirection + PassThru work); we record it and kill THAT pid only.
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$d = Start-Process -FilePath '%ROOT%\daemon\fd-daemon.exe' -ArgumentList '%PORT%' -PassThru -NoNewWindow -RedirectStandardOutput '%DLOG%' -RedirectStandardError '%ROOT%\daemon\daemon-err.log'; Set-Content -Path '%PIDFILE%' -Value $d.Id"
ping -n 4 127.0.0.1 >nul
set "DPID="
if exist "%PIDFILE%" set /p DPID=<"%PIDFILE%"

echo --- running fd-game.exe --gametest via 127.0.0.1:%PORT% ---
cd /d "%ROOT%\game"
fd-game.exe --gametest 240 %PORT% chunkins1.lua > "%TEMP%\fd-gametest.out" 2>&1
set "RC=%ERRORLEVEL%"
type "%TEMP%\fd-gametest.out"
echo GAME_EXIT=%RC%

echo --- daemon log ---
type "%DLOG%" 2>nul
if defined DPID ( taskkill /pid %DPID% /f >nul 2>&1 ) else ( echo (warning: daemon pid not captured) )

REM pass requires BOTH a real rendered frame AND a clean game exit
findstr /c:"REAL FRAME" "%TEMP%\fd-gametest.out" >nul
if errorlevel 1 ( echo VERIFY: FAIL ^(no REAL FRAME^) & endlocal & exit /b 1 )
if not "%RC%"=="0" ( echo VERIFY: FAIL ^(game exit %RC%^) & endlocal & exit /b 1 )
echo VERIFY: PASS
endlocal & exit /b 0
