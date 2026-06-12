#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# run.sh — the kid-proof launcher. Starts the daemon if it isn't running,
# launches the game, and restarts either one if it dies. A crash becomes a
# two-second hiccup instead of a frozen window and a hunt for a terminal.
#
#   ./run.sh                # Chunkins' quest, GPU post if available
#   FD_GPU=0 ./run.sh       # CPU-only path
#   ./run.sh relic_sweep.lua
set -u
cd "$(dirname "$0")"
SOCK=/tmp/feverdream.sock
SCRIPT="${1:-chunkins1.lua}"
export FD_GPU="${FD_GPU:-1}"

# supervise ONLY our own daemon (PID file) — never pkill by name: another
# project's fd-daemon on a different socket is not ours to kill (tri-brain)
PIDFILE=/tmp/fd-daemon.$(echo "$SOCK" | tr / _).pid

daemon_up() {
    [ -S "$SOCK" ] || return 1
    [ -f "$PIDFILE" ] || return 1
    kill -0 "$(cat "$PIDFILE")" 2>/dev/null
}

start_daemon() {
    [ -f "$PIDFILE" ] && kill "$(cat "$PIDFILE")" 2>/dev/null
    sleep 0.3; rm -f "$SOCK"
    ( cd ../daemon && setsid ./fd-daemon "$SOCK" >> /tmp/fd-daemon.log 2>&1 &
      echo $! > "$PIDFILE" )
    for _ in $(seq 1 20); do daemon_up && return 0; sleep 0.3; done
    echo "run.sh: daemon failed to start — see /tmp/fd-daemon.log"
    return 1
}

echo "run.sh: supervising fd-daemon + fd-game (script $SCRIPT). Ctrl-C to stop."
while :; do
    daemon_up || start_daemon || exit 1
    ./fd-game "$SOCK" 1280 720 4 "$SCRIPT" >> /tmp/fd-game.log 2>&1
    rc=$?
    if [ "$rc" = 0 ]; then
        echo "run.sh: clean exit (ESC) — goodbye"
        exit 0
    fi
    echo "run.sh: fd-game exited rc=$rc — restarting in 2s (log: /tmp/fd-game.log)"
    sleep 2
done
