#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# build-windows.sh — cross-compile fd-game.exe for Windows x86_64 with
# mingw-w64. The MIT half of the firewall only (SDL2 + Lua 5.4); fd-daemon
# (POV-Ray, AGPL) is built separately with MSVC on the Windows box.
#
# Deps live in ./win-deps (fetched by the build): SDL2 mingw devel + a Lua
# 5.4.8 static archive cross-built for mingw. The result is a standalone exe;
# only SDL2.dll travels alongside it (statically linking SDL is discouraged).
set -eu
cd "$(dirname "$0")"

CXX=x86_64-w64-mingw32-g++
SDL=win-deps/SDL2-2.32.4/x86_64-w64-mingw32
LUA=win-deps/lua-5.4.8/src

[ -d "$SDL" ] || { echo "build-windows: missing $SDL — run the deps fetch first"; exit 1; }
[ -f "$LUA/liblua.a" ] || { echo "build-windows: missing $LUA/liblua.a — build Lua for mingw first"; exit 1; }

# Embed version info + manifest + icon so the exe carries real provenance — a
# bare metadata-less exe is the classic antivirus false-positive profile. (The
# other half of the fix is Authenticode code signing; see WINDOWS_ANTIVIRUS.md.)
echo "build-windows: compiling resources (version info + manifest + icon)..."
x86_64-w64-mingw32-windres fd-game.rc -O coff -o fd-game-res.o

echo "build-windows: compiling fd-game.exe (mingw-w64)..."
$CXX -O2 -Wall -Werror=format -std=c++11 -D__USE_MINGW_ANSI_STDIO=1 \
    -I"$SDL/include" -I"$SDL/include/SDL2" -I"$LUA" \
    -o fd-game.exe fd-game.cpp fd-game-res.o \
    -L"$SDL/lib" -L"$LUA" \
    -lmingw32 -lSDL2main -lSDL2 -llua \
    -lws2_32 \
    -static-libgcc -static-libstdc++ \
    -mwindows

echo "build-windows: copying SDL2.dll next to the exe..."
cp -f "$SDL/bin/SDL2.dll" .

echo "build-windows: done."
x86_64-w64-mingw32-strip fd-game.exe 2>/dev/null || true
ls -la fd-game.exe SDL2.dll
file fd-game.exe
