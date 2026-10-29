#!/bin/sh
# Declare register PARAMETERS to a fixpoint.
#
# Declaring a function's inputs makes the decompiler stop deleting the argument
# setup at its call sites -- and that setup then reveals that the CALLER inherits
# the value too, so the caller needs the same parameter. Measured 2026-09-30:
# after one round FUN_0000669c passed `p_D0` while still declared `(void)`, which
# does not compile. The requirement is transitive; iterate until nothing new.
set -e
cd "$(dirname "$0")/../.."
R=tools/ghidra_run.sh
for round in 1 2 3 4 5; do
  $R script reg_convention.py docs/audit/reg_convention.json 2>&1 | grep -o 'reg_convention:.*' || true
  python3 - <<'PY'
import json, os, re
# ACCUMULATE. Once a register becomes a parameter it stops being a pseudo-register,
# so a set rebuilt from scratch each round DROPS what the previous round declared
# and updateFunction then REMOVES it. Measured 2026-09-30: the count oscillated
# 153/161/159/163 forever instead of converging. The set for a function is the
# UNION of what it already has and what it still needs.
ACC = '/tmp/params_acc.json'
acc = json.load(open(ACC)) if os.path.exists(ACC) else {}
V = json.load(open('docs/audit/reg_convention.json'))
W = {'b': 1, 'w': 2, 'u': 2, '': 4}
out = []
for r in V:
    regs = {}
    for ps, st in (r.get('storage') or {}).items():
        if ps.startswith('extraout_'):
            continue                  # an OUTPUT; set_ret_storage.py owns it
        m = re.match(r'([AD]\d)([buw]?):(\d)', st)
        if not m:
            continue
        base, suf = m.group(1), m.group(2)
        if base == 'A6':
            continue                  # the object pointer, supplied by the driver
        cur = regs.get(base)
        if cur is None or W[suf] > W[cur]:
            regs[base] = suf
    have = set(acc.get(r['name'], []))
    want = have | set(regs)
    # a declared parameter also shows in the prototype; keep those too
    for pm in (r.get('params') or []):
        st = pm.get('storage') or ''
        m2 = re.match(r'([AD]\d)', st)
        if m2 and m2.group(1) != 'A6':
            want.add(m2.group(1))
    if want:
        acc[r['name']] = sorted(want)
for n, rs in sorted(acc.items()):
    out.append('%s %s' % (n, ','.join(rs)))
json.dump(acc, open(ACC, 'w'))
open('/tmp/params_file.txt', 'w').write('\n'.join(out) + '\n')
print('  cumulative: %d functions with register parameters' % len(out))
PY
  N=$(wc -l < /tmp/params_file.txt)
  if [ -f /tmp/params_prev.txt ] && cmp -s /tmp/params_file.txt /tmp/params_prev.txt; then
    echo "  FIXPOINT at round $round ($N functions)"
    break
  fi
  cp /tmp/params_file.txt /tmp/params_prev.txt
  $R script set_param_storage.py /tmp/params_file.txt 2>&1 | grep -cE 'set_param_storage: \w+\(' | sed 's/^/  declared /'
done
$R script f1_decompile.py docs/proof/altbeast.c 180 2>&1 | grep -o 'decompiled.*'
$R script func_ranges.py docs/audit/func_ranges.txt 2>&1 | grep -o 'func_ranges:.*'
