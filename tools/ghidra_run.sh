#!/bin/sh
# Ghidra headless driver for the DISCOVERY-TIMING arc (docs/handoff/HANDOFF-DISCOVERY.md).
#   tools/ghidra_run.sh import            # one-time: import + auto-analyse prog68k.bin
#   tools/ghidra_run.sh census OUT.json   # run tools/ghidra/timing_census.py on the project
#   tools/ghidra_run.sh script NAME.py [args...]   # any script in tools/ghidra/
# Project lives OUTSIDE the repo (GHIDRA_PROJ, default under the scratch dir):
# it contains the analysed Sega binary and must never be committed.
set -e
GHIDRA=${GHIDRA:-/Users/mikeholzinger/src/kyocera-2235/ghidra_11.2.1_PUBLIC}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PROJ=${GHIDRA_PROJ:-/private/tmp/claude-501/-Users-mikeholzinger-src-sega16-2-32x/ghidra_proj}
NAME=altbeast
BIN=$ROOT/roms/altbeast/prog68k.bin
mkdir -p "$PROJ"
case "$1" in
  import)
    "$GHIDRA/support/analyzeHeadless" "$PROJ" $NAME -import "$BIN" \
        -processor 68000:BE:32:default -loader BinaryLoader -loader-baseAddr 0x0 -overwrite ;;
  census)
    shift
    "$GHIDRA/support/analyzeHeadless" "$PROJ" $NAME -process prog68k.bin -noanalysis \
        -scriptPath "$ROOT/tools/ghidra" -postScript timing_census.py "${1:-$ROOT/docs/audit/timing_census.json}" ;;
  script)
    shift; S=$1; shift
    "$GHIDRA/support/analyzeHeadless" "$PROJ" $NAME -process prog68k.bin -noanalysis \
        -scriptPath "$ROOT/tools/ghidra" -postScript "$S" "$@" ;;
  *) echo "usage: $0 import | census [out.json] | script NAME.py [args]"; exit 2 ;;
esac
