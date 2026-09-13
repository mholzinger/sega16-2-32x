#!/bin/sh
# Ghidra headless driver for the DISCOVERY-TIMING arc (docs/handoff/HANDOFF-DISCOVERY.md).
#   [GAME=goldnaxe] tools/ghidra_run.sh import   # one-time: import + auto-analyse roms/$GAME/prog68k.bin
#   tools/ghidra_run.sh census OUT.json   # run tools/ghidra/timing_census.py on the project
#   tools/ghidra_run.sh script NAME.py [args...]   # any script in tools/ghidra/
# Project lives OUTSIDE the repo (GHIDRA_PROJ, default under the scratch dir):
# it contains the analysed Sega binary and must never be committed.
set -e
GHIDRA=${GHIDRA:-/Users/mikeholzinger/src/kyocera-2235/ghidra_11.2.1_PUBLIC}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PROJ=${GHIDRA_PROJ:-/private/tmp/claude-501/-Users-mikeholzinger-src-sega16-2-32x/ghidra_proj}
# GAME env var selects the title (2026-09-12, Golden Axe thread): each game is
# its own Ghidra project + image; the timing census default lands under
# docs/audit/<GAME>/ for every title but altbeast (whose file predates that).
GAME=${GAME:-altbeast}
NAME=$GAME
BIN=$ROOT/roms/$GAME/prog68k.bin
if [ "$GAME" = altbeast ]; then CENSUS_DEFAULT=$ROOT/docs/audit/timing_census.json
else CENSUS_DEFAULT=$ROOT/docs/audit/$GAME/timing_census.json; mkdir -p "$ROOT/docs/audit/$GAME"; fi
mkdir -p "$PROJ"
case "$1" in
  import)
    "$GHIDRA/support/analyzeHeadless" "$PROJ" $NAME -import "$BIN" \
        -processor 68000:BE:32:default -loader BinaryLoader -loader-baseAddr 0x0 -overwrite ;;
  census)
    shift
    "$GHIDRA/support/analyzeHeadless" "$PROJ" $NAME -process prog68k.bin -noanalysis \
        -scriptPath "$ROOT/tools/ghidra" -postScript timing_census.py "${1:-$CENSUS_DEFAULT}" "$GAME" ;;
  script)
    shift; S=$1; shift
    "$GHIDRA/support/analyzeHeadless" "$PROJ" $NAME -process prog68k.bin -noanalysis \
        -scriptPath "$ROOT/tools/ghidra" -postScript "$S" "$@" ;;
  *) echo "usage: $0 import | census [out.json] | script NAME.py [args]"; exit 2 ;;
esac
