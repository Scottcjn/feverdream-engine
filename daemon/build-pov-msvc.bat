@echo off
REM build-pov-msvc.bat — build the POV-Ray static libs fd-daemon links against,
REM in dependency order, with the latest installed VS (toolset v142). Skips the
REM GUI. Stops at the first failing project so the error is isolated.
REM Run from anywhere: paths derive from this script's location.
setlocal enabledelayedexpansion

REM repo root = parent of the daemon\ dir this script lives in
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "POV=%ROOT%\vendor-povray"

REM locate Visual Studio via vswhere (no hardcoded install path)
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath 2^>nul`) do set "VS=%%i"
if not defined VS ( echo build-pov-msvc: no Visual Studio found via vswhere & exit /b 1 )
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || ( echo build-pov-msvc: vcvars64 failed & exit /b 1 )

cd /d "%POV%\windows\vs10" || ( echo build-pov-msvc: POV solution dir not found & exit /b 1 )

set OPTS=/p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v142 /p:WindowsTargetPlatformVersion=10.0 /m /v:m /nologo /clp:Summary

REM POV-Ray's license REQUIRES an unofficial build to name its builder; fill it
REM in and drop the #error (idempotent). Override with FD_POV_BUILDER; the patch
REM only touches the vendored (gitignored) tree, never committed source.
if not defined FD_POV_BUILDER set "FD_POV_BUILDER= Elyan Labs / Feverdream Engine - github.com/Scottcjn/feverdream-engine"
echo Patching POV-Ray distribution message (builder: %FD_POV_BUILDER%)...
powershell -NoProfile -Command "$f='..\..\source\backend\povray.h'; $c=Get-Content -Raw $f; $c=$c -replace '#error Please complete the following DISTRIBUTION_MESSAGE_2 definition','// builder identity filled in below'; $c=$c -replace ' FILL IN NAME HERE\.+', $env:FD_POV_BUILDER; Set-Content -NoNewline $f $c"

set PROJS=zlib libpng jpeg tiff openexr_toFloat openexr_eLut openexr_Half openexr_Iex openexr_IlmThread openexr_IlmImf boost_system boost_thread boost_date_time povbase povbackend povfrontend rtrsupport vfewin

for %%P in (%PROJS%) do (
  echo(
  echo ============================================================
  echo  BUILDING %%P  (Release^|x64, v142^)
  echo ============================================================
  msbuild %%P.vcxproj %OPTS%
  if errorlevel 1 (
    echo *** FAILED at project: %%P  ^(exitcode !errorlevel!^) ***
    exit /b 1
  )
)
echo(
echo ============================================================
echo  ALL LIBS BUILT
echo ============================================================
dir /s /b bin64\lib\*.lib 2>nul
exit /b 0
