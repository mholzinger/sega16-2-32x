#!/bin/sh
# F1.1 reproducible rebuild: a clean Ghidra import to the best known coverage.
# Every step's provenance is in docs/design/F1-FRONTEND.md.
#   tools/ghidra/f1_rebuild.sh
# Why a script: two attempts to adjust the project in place (declaring a calling
# convention, then reverting it) left signatures altered and the SH-2 compile
# rate DOWN from 415/623 to 303/623. In-place edits to an analysed project are
# not reliably reversible; re-importing is.
set -e
cd "$(dirname "$0")/../.."
R=tools/ghidra_run.sh
echo "== import (auto-analyse) =="
$R import >/dev/null 2>&1
echo "== seeds: saved set + map-missing functions + trace-proven PCs =="
python3 - <<'PY'
import json
d = json.load(open('docs/audit/altbeast_seeds.json'))
seeds = set(d['seeds']) | set(d.get('function_seeds', []))
seeds |= {s[2:].upper() if str(s).lower().startswith('0x') else str(s).upper()
          for s in d['repair_seeds']}
try:
    seeds |= {s[2:].upper() if str(s).lower().startswith('0x') else str(s).upper()
              for s in json.load(open('docs/proof/f1_seeds_trace.json'))}
except Exception:
    pass
fs = set(d['function_seeds'])
try:
    fs |= set(json.load(open('/tmp/map_missing.json')))
except Exception:
    pass
json.dump({'seeds': sorted(seeds), 'function_seeds': sorted(fs)},
          open('/tmp/f1_seeds_all.json', 'w'))
print('  seeds %d, function seeds %d' % (len(seeds), len(fs)))
PY
$R script seed_apply.py /tmp/f1_seeds_all.json 2>&1 | grep -E '^seeds '
echo "== shape D: lea pc@(H),An ; move.l An,(d,A6) routine pointers =="
python3 tools/shape_d_seeds.py roms/altbeast/prog68k.bin --json /tmp/seeds_shapeD.json   | grep -E 'install sites'
$R script seed_apply.py /tmp/seeds_shapeD.json 2>&1 | grep -E '^seeds '
echo "== bounds repair =="
$R script fix_bounds.py 2>&1 | grep -E 'mis-bounded|functions now'
echo "== jump tables (F0(d) bases, sentinel rule) =="
$R script f1_tables.py 2>&1 | grep -E 'distinct targets|newly decoded'
echo "== dispatch fixpoint =="
$R script f1_seeds.py 4 2>&1 | grep -E 'f1_seeds:'
echo "== coverage =="
$R script f1_export.py /tmp/f1_export.json 2>&1 | grep -E '^  lo|instructions'
