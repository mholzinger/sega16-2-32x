#!/bin/sh
# Iterate the gap-edge heuristic to a fixpoint.
#   tools/ghidra/f1_gapedge_loop.sh [rounds]
# A gap that begins immediately after rts/rte/jmp/bra is a code boundary: the
# previous function ended and something unreached begins. Measured 2026-09-26:
# 42 of 42 such gaps decoded, no fragmentation (large-function counts held at
# 43 and 7). Each round of decoding creates NEW terminators, so it iterates.
set -e
cd "$(dirname "$0")/../.."
R=tools/ghidra_run.sh
N=${1:-8}
i=1
while [ "$i" -le "$N" ]; do
  $R script f1_gapedge.py /tmp/seeds_gapedge.json 2>/dev/null \
    | grep -E 'CODE boundary' | sed "s/^/round $i: /"
  OUT=$($R script seed_apply.py /tmp/seeds_gapedge.json 2>/dev/null | grep -E '^seeds ')
  echo "  $OUT"
  echo "$OUT" | grep -q '0 newly disassembled' && { echo "  fixpoint"; break; }
  $R script fix_bounds.py 2>/dev/null | grep -E 'functions now' | sed 's/^/  /'
  i=$((i+1))
done
$R script f1_export.py /tmp/f1_export.json 2>/dev/null | grep -E '^  lo|instructions'
