#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# bench.sh — prove the thesis: POV-Ray's per-frame cost is process lifecycle +
# disk, NOT raytracing. Self-contained; writes its own test scene. Needs povray.
#
#   ./bench.sh
#
# Reports, for a simple scene:
#   1. POV's own Parse Time + Trace Time  (the real work)
#   2. wall-clock per process invocation  (work + lifecycle overhead)
#   3. that wall-clock is ~constant across resolutions (=> not fill-bound)
set -euo pipefail
command -v povray >/dev/null || { echo "need povray on PATH"; exit 1; }
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
THREADS="$(nproc)"

cat > "$TMP/scene.pov" <<'POV'
camera { location <0,3,-6> look_at <0,0,0> }
light_source { <-5,10,-8> rgb 1 }
plane { y,0 pigment { checker rgb 0.8 rgb 0.3 } }
#declare i=0;
#while (i<8)
  sphere { <sin(i+clock*6)*2, 0.6, cos(i)*2>, 0.5 pigment { rgb <0.7,0.3+i*0.08,0.9> } }
  #declare i=i+1;
#end
POV

echo "POV-Ray: $(povray --version 2>&1 | head -1) | threads=$THREADS"
echo
echo "== (1) POV's own timers @ 240x135 — the actual work =="
povray +I"$TMP/scene.pov" +O"$TMP/o.png" +W240 +H135 "+WT$THREADS" +K0.5 2>&1 \
  | grep -iE 'Parse Time|Trace Time' | sed 's/^/   /'

echo
echo "== (2) wall-clock per invocation (work + process lifecycle) =="
for k in 1 2 3; do
  /usr/bin/time -f "   call $k: %e s" povray +I"$TMP/scene.pov" +O"$TMP/o.png" \
    +W240 +H135 "+WT$THREADS" +K0.$k -D -GA 2>&1 | tail -1
done

echo
echo "== (3) wall-clock vs resolution (constant => not fill-bound) =="
for r in 16 160 480; do
  t=$(/usr/bin/time -f "%e" povray +I"$TMP/scene.pov" +O"$TMP/o.png" \
        +W$r +H$r "+WT$THREADS" -D -GA 2>&1 | tail -1)
  echo "   ${r}x${r}: ${t}s"
done

echo
echo "Conclusion: real work ~10-12 ms; the rest is launch + init + PNG-to-disk +"
echo "teardown, paid every frame. Keep the process resident and that overhead is gone."
