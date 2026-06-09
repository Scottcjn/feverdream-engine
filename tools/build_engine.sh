#!/usr/bin/env bash
# build_engine.sh — fetch POV-Ray 3.7, apply the Feverdream real-time patch, build
# the engine archives, then build the resident render daemon.
#
# We do NOT vendor the POV-Ray tree (it's AGPLv3 — see ARCHITECTURE.md → Licensing).
# We ship only our patch; this script reconstructs the build from upstream.
#
#   ./tools/build_engine.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
POV="$ROOT/vendor-povray"

if [ ! -d "$POV" ]; then
  echo ">> cloning POV-Ray 3.7-stable"
  git clone --depth 1 --branch 3.7-stable https://github.com/POV-Ray/povray.git "$POV"
  echo ">> applying patches/feverdream-realtime.patch"
  ( cd "$POV" && git apply "$ROOT/patches/feverdream-realtime.patch" )
fi

if [ ! -f "$POV/source/libpovray.a" ]; then
  echo ">> building POV-Ray (this takes a few minutes)"
  ( cd "$POV/unix" && ./prebuild.sh )
  ( cd "$POV" && ./configure COMPILED_BY="feverdream-engine" --prefix="$POV/install" \
      && make -j"$(nproc)" )
fi

echo ">> building the resident daemon"
make -C "$ROOT/daemon"

cat <<EOF

Done. Try it:
  daemon/resident daemon/spin.pov 60 320 180 /usr/share/povray-3.7/include
(needs the POV standard include dir — colors.inc etc. — on your system.)
EOF
