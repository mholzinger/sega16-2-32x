#!/bin/sh
# Rebuild the altbeast Ghidra project to the seeded state, from scratch.
#
# ORDER MATTERS AND RE-ANALYSIS IS DESTRUCTIVE (docs/log/LOOP-DECOMPILE.md 16):
# import (which auto-analyses) once, then apply the seeds, then stop. Running
# analyzeHeadless -process again over the seeded program CLEARS seeded code.
#
#   tools/ghidra/rebuild.sh [SEEDS.json]
#
# Regenerate the seeds first if the listing changed:
#   m68k-elf-objdump -D -b binary -m 68000 roms/altbeast/prog68k.bin > L.dis
#   tools/ghidra/seed_harvest.py L.dis roms/altbeast/prog68k.bin --json S.json
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SEEDS=${1:-$ROOT/docs/audit/altbeast_seeds.json}
[ -f "$SEEDS" ] || { echo "rebuild: no seeds at $SEEDS"; exit 2; }
"$ROOT/tools/ghidra_run.sh" import
"$ROOT/tools/ghidra_run.sh" script seed_apply.py "$SEEDS"
"$ROOT/tools/ghidra_run.sh" script coverage.py
