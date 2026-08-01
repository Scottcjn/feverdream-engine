#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# run.sh — build daemon/fd-daemon.cpp and run the wire-protocol suite against
# the binary that comes out. No POV-Ray, no vendored tree: the daemon is linked
# against tests/stub/ (see stub/vfe.h for why that is honest and what it costs).
#
# This is the check that a vendor-free machine — including CI — can run, and
# the one that would have caught daemon/fd-daemon.cpp failing to compile at all
# between 2026-07-04 and this commit. Exit 0 = green.
set -u
cd "$(dirname "$0")"
CXX=${CXX:-g++}
SOCK=${SOCK:-/tmp/feverdream-proto-test.sock}
: "${FD_READ_TIMEOUT:=2}"
export FD_READ_TIMEOUT
BIN=./fd-daemon-stub
LOG=./fd-daemon-stub.log

echo "=== compile daemon/fd-daemon.cpp (stubbed renderer, -Wall -Werror)"
rm -f "$BIN"
# -Werror matters: an unterminated string literal or a stray directive shows up
# as a warning in some front ends before it shows up as an error.
if ! "$CXX" -std=c++11 -Wall -Werror -I stub -I .. -o "$BIN" ../fd-daemon.cpp; then
    echo "    FAIL — the daemon does not compile"
    exit 1
fi
echo "    ok   $BIN built"

echo "=== boot the daemon (FD_READ_TIMEOUT=${FD_READ_TIMEOUT}s)"
rm -f "$SOCK"
"$BIN" "$SOCK" >"$LOG" 2>&1 &
DPID=$!
trap 'kill "$DPID" 2>/dev/null; rm -f "$SOCK"' EXIT
for _ in $(seq 1 100); do
    [ -S "$SOCK" ] && break
    kill -0 "$DPID" 2>/dev/null || { echo "    FAIL — daemon exited"; cat "$LOG"; exit 1; }
    sleep 0.05
done
[ -S "$SOCK" ] || { echo "    FAIL — no socket at $SOCK"; cat "$LOG"; exit 1; }
echo "    ok   listening on $SOCK"

echo "=== protocol suite"
python3 test_protocol.py "$SOCK"
RC=$?

kill "$DPID" 2>/dev/null
wait "$DPID" 2>/dev/null
[ "$RC" = 0 ] || { echo "--- daemon log ---"; cat "$LOG"; }
exit "$RC"
