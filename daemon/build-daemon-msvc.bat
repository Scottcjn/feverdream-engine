@echo off
REM build-daemon-msvc.bat — compile fd-daemon.exe against the MSVC-built POV-Ray
REM libs. Run AFTER build-pov-msvc.bat. Paths derive from this script's location.
setlocal

for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "POV=%ROOT%\vendor-povray"
set "POVLIB=%POV%\windows\vs10\bin64\lib"
set "DAEMON=%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath 2^>nul`) do set "VS=%%i"
if not defined VS ( echo build-daemon-msvc: no Visual Studio found & exit /b 1 )
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1 || ( echo build-daemon-msvc: vcvars64 failed & exit /b 1 )
cd /d "%DAEMON%"

REM include dirs — mirror povbackend.vcxproj (vfe\win FIRST so vfeplatform.h
REM resolves to the Windows session, not the Unix one).
set INC=/I"%POV%\libraries\boost" /I"%POV%\vfe\win" /I"%POV%\vfe" /I"%POV%\source" /I"%POV%\source\base" /I"%POV%\source\backend" /I"%POV%\source\frontend" /I"%POV%\libraries\jpeg" /I"%POV%\libraries\zlib" /I"%POV%\libraries\png" /I"%POV%\libraries\tiff\libtiff" /I"%POV%\libraries\ilmbase\config.windows" /I"%POV%\libraries\openexr\config.windows"

REM defines — POV-Ray Release|x64 set. BUILDING_AMD64 = POV's 64-bit marker
REM (syspovconfig.h bitness guard); _CONSOLE = required or the link fails.
set DEF=/DBUILDING_AMD64 /D_CONSOLE /DBOOST_ALL_NO_LIB /DNDEBUG /DWIN32 /DWIN32_LEAN_AND_MEAN /D_WINDOWS /DWINVER=0x0500 /D_WIN32_WINNT=0x0500 /DCOMMONCTRL_VERSION=0x500 /DNOMINMAX /D_CRT_SECURE_NO_DEPRECATE /D_SCL_SECURE_NO_DEPRECATE /D_HAS_ITERATOR_DEBUGGING=0

set LIBS=vfe64.lib povbackend64.lib povfrontend64.lib povbase64.lib rtrsupport64.lib IlmImf64.lib Half64.lib Iex64.lib IlmThread64.lib tiff64.lib jpeg64.lib libpng64.lib zlib64.lib libboost_thread64.lib libboost_system64.lib libboost_date_time64.lib
set SYSLIBS=ws2_32.lib user32.lib gdi32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib comdlg32.lib version.lib winmm.lib

REM delete any stale binary so a failed build can't masquerade as success.
if exist fd-daemon.exe del /q fd-daemon.exe

REM provenance: compile version info + manifest + icon (rc.exe from the SDK), so
REM the daemon isn't a bare metadata-less exe (the classic AV false-positive
REM profile). Linked in below as fd-daemon.res.
echo Compiling resources (version info + manifest + icon)...
rc /nologo /fo fd-daemon.res fd-daemon.rc
if errorlevel 1 ( echo *** RC FAILED *** & exit /b 1 )

echo ============================================================
echo  Compiling + linking fd-daemon.exe (MSVC, /MT, x64)
echo ============================================================
REM fd_win_stubs.cpp supplies the povwin:: + pov_frontend:: platform hooks the
REM (replaced) GUI app would otherwise provide. /LTCG: POV libs are built /GL.
cl /nologo /EHsc /O2 /MT /bigobj %DEF% %INC% fd-daemon.cpp fd_win_stubs.cpp fd-daemon.res ^
   /Fe:fd-daemon.exe ^
   /link /LTCG /LIBPATH:"%POVLIB%" %LIBS% %SYSLIBS%
if errorlevel 1 ( echo *** CL/LINK FAILED *** & exit /b 1 )
if not exist fd-daemon.exe ( echo *** fd-daemon.exe NOT produced *** & exit /b 1 )
echo BUILT: & dir fd-daemon.exe
exit /b 0
