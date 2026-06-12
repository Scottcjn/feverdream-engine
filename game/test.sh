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

echo "=== level-chain transition (test_win1 -> test_win2)"
CHAIN=$(SDL_AUDIODRIVER=dummy ./fd-game --gametest 300 "$SOCK" test_win1.lua)
echo "$CHAIN" | grep -E "levels advanced|title"
if echo "$CHAIN" | grep -q "levels advanced 1, title 'LEVEL2OK'"; then
    echo "    PASS"
else
    echo "    FAIL"; FAIL=1
fi

if [ -f libfdpost.so ]; then
    run "GPU post math (ctypes)" python3 - <<'EOF'
import ctypes, numpy as np
lib = ctypes.CDLL("./libfdpost.so")
assert lib.fdpost_init(4,4,8,8) == 0
a = np.full((4,4,4),100,np.uint8); b = np.full((4,4,4),200,np.uint8)
out = np.zeros((8,8,4),np.uint8)
lib.fdpost_frame(a.ctypes.data, ctypes.c_float(0.5), 256, out.ctypes.data)
lib.fdpost_frame(b.ctypes.data, ctypes.c_float(0.5), 256, out.ctypes.data)
assert abs(int(out[4,4,0])-150) <= 1
lib.fdpost_shutdown()
EOF
else
    echo "=== GPU post math: SKIPPED (no libfdpost.so — run 'make gpu')"
fi

pkill -x fd-daemon 2>/dev/null
echo
[ "$FAIL" = 0 ] && echo "ALL TESTS PASS" || echo "FAILURES PRESENT"
exit $FAIL
