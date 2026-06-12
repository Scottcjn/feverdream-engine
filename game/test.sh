#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# test.sh — the fd-game verification suite. Runs every headless check against
# a fresh daemon: engine regression (arena), both games' logic, GPU post math.
# Exit 0 = all green. This is what "verified" means before a commit ships.
set -u
cd "$(dirname "$0")"
SOCK=/tmp/feverdream-test.sock
FAIL=0

pkill -x fd-daemon 2>/dev/null; sleep 0.3; rm -f "$SOCK"
( cd ../daemon && ./fd-daemon "$SOCK" > /tmp/fd-daemon-test.log 2>&1 & )
sleep 1.5

run() {  # run <label> <cmd...>
    echo "=== $1"
    shift
    if SDL_AUDIODRIVER=dummy "$@"; then echo "    PASS"; else echo "    FAIL"; FAIL=1; fi
}

run "engine regression (arena selftest)"     ./fd-game --selftest 180 "$SOCK" arena.lua
run "RELIC SWEEP logic (gametest)"           ./fd-game --gametest 180 "$SOCK" relic_sweep.lua
run "CRATE CLIMB platforming (gametest)"     ./fd-game --gametest 240 "$SOCK" crate_climb.lua
run "GOLDEN ACORN level 1 (gametest)"        ./fd-game --gametest 240 "$SOCK" chunkins1.lua
run "GOLDEN ACORN world 4 (gametest)"        ./fd-game --gametest 240 "$SOCK" chunkins4.lua
run "WINDMILL PASS world 5 (gametest)"       ./fd-game --gametest 240 "$SOCK" chunkins5.lua
run "HAZELNUT BRIDGES world 6 (gametest)"    ./fd-game --gametest 240 "$SOCK" chunkins_hazelnut_bridges.lua
run "CASCADE HOLLOW world 7 (gametest)"      ./fd-game --gametest 240 "$SOCK" chunkins7.lua

echo "=== level sandbox (banned: os/io/load/require/debug)"
SBOX=$(SDL_AUDIODRIVER=dummy ./fd-game --gametest 120 "$SOCK" test_sandbox.lua)
if echo "$SBOX" | grep -q "title 'SANDBOX_OK'"; then
    echo "    PASS"
else
    echo "$SBOX" | grep "title"; echo "    FAIL"; FAIL=1
fi

echo "=== level-chain transition (test_win1 -> test_win2)"
CHAIN=$(SDL_AUDIODRIVER=dummy ./fd-game --gametest 300 "$SOCK" test_win1.lua)
echo "$CHAIN" | grep -E "levels advanced|title"
if echo "$CHAIN" | grep -q "levels advanced 1, title 'LEVEL2OK'"; then
    echo "    PASS"
else
    echo "    FAIL"; FAIL=1
fi

if [ -f libfdpost.so ]; then
    echo "=== GPU post math (ctypes)"
    GPURC=$(python3 - <<'EOF'
import ctypes, numpy as np, sys
lib = ctypes.CDLL("./libfdpost.so")
rc = lib.fdpost_init(4,4,8,8)
if rc == -2:
    print("VRAM_BUSY"); sys.exit(0)   # env: GPU full (llama etc) — not a bug
if rc != 0:
    print("INIT_FAIL"); sys.exit(1)
a = np.full((4,4,4),100,np.uint8); b = np.full((4,4,4),200,np.uint8)
out = np.zeros((8,8,4),np.uint8)
lib.fdpost_frame(a.ctypes.data, ctypes.c_float(0.5), 256, out.ctypes.data)
lib.fdpost_frame(b.ctypes.data, ctypes.c_float(0.5), 256, out.ctypes.data)
ok = abs(int(out[4,4,0])-150) <= 1
lib.fdpost_shutdown()
print("MATH_OK" if ok else "MATH_BAD"); sys.exit(0 if ok else 1)
EOF
)
    case "$GPURC" in
        *MATH_OK*)   echo "    PASS" ;;
        *VRAM_BUSY*) echo "    SKIP (GPU memory exhausted by other processes — not a game bug)" ;;
        *)           echo "    FAIL ($GPURC)"; FAIL=1 ;;
    esac
else
    echo "=== GPU post math: SKIPPED (no libfdpost.so — run 'make gpu')"
fi

pkill -x fd-daemon 2>/dev/null
echo
[ "$FAIL" = 0 ] && echo "ALL TESTS PASS" || echo "FAILURES PRESENT"
exit $FAIL
