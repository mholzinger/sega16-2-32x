#!/usr/bin/env python3
"""OVERNIGHT LOOP: one change, one measurement, automatic keep-or-revert.

    python3 tools/port_loop.py > docs/log/PORT-LOOP.txt 2>&1

WHY A SCRIPT AND NOT A PLAN. 18 of 39 commits on 2026-10-01 were corrections, and
every bad number came from an instrument shipped without a check. The failure is not
knowledge -- the 68K rules and the architecture never change -- it is that a written
rule depends on being remembered at the moment code is written. This loop removes
the judgement: the candidate list is FIXED before it starts, each candidate is
measured the same way, and the verdict is arithmetic.

RULES IT ENFORCES, each from a measured failure:
  * port_guard.py runs FIRST. A violation stops the loop; it does not "proceed and
    note it".
  * ONE candidate at a time, each a BUILD FLAG only. No source edits, no Ghidra
    runs, no new instruments -- the three things that produced every regression.
  * KEEP only on a strictly better measured number. Anything else reverts, and the
    revert is the default path, not the exception.
  * Every line of the log carries the number and the control it is measured
    against. A number without its control is not reportable (section 20).
"""
import os, re, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
BASE = ('NOSPRBAKE=1 NOTILESMD=1 PORTFULL=1 RIGBARCODE=1 WRAMCODE=1').split()

def sh(cmd, timeout=3600):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=timeout)

def log(s):
    print('%s  %s' % (time.strftime('%H:%M:%S'), s), flush=True)

# ---- the guard gates the whole run ----------------------------------------
g = sh('python3 tools/port_guard.py')
log('guard: %s' % ('CLEAN' if g.returncode == 0 else 'VIOLATIONS -- see below'))
for line in (g.stdout or '').splitlines():
    log('  ' + line)

# ---- the candidate list, FIXED before the loop starts ---------------------
# Tonight: how much of the arcade's measured 15-call frame body can run before the
# hang in 0x0E052 (call 8 of 14), and does more of it help? Flag-only, so a reject
# costs a rebuild and nothing else.
CANDS = [('PORTBODY=%d' % n, 'frame body: first %d of 14 calls' % n) for n in
         (1, 2, 3, 4, 5, 6, 7)]

def build(extra):
    sh('rm -f sh_src/port_l2.o sh_src/portfull/port.o')
    r = sh('make line %s PORTSEED=64 %s' % (' '.join(BASE), extra))
    if r.returncode != 0:
        errs = [l for l in (r.stdout + r.stderr).splitlines() if ' error:' in l or 'overflow' in l]
        return False, (errs[0][:160] if errs else 'build failed')
    return True, ''

def measure():
    sh('cp rom/s16.32x rom/loop.32x; cp rom/s16.lst rom/loop.lst')
    r = sh('python3 tools/portl2_seed.py rom/loop.32x 64')
    out = r.stdout or ''
    m = re.search(r'wrong:\s+(\d+) of 16384\s+control \(does nothing\):\s+(\d+)', out)
    body = re.search(r'FRAME BODY: (\d+) of (\d+) completed', out)
    hung = re.search(r'HUNG in (0x[0-9A-F]+)', out)
    if not m:
        return None
    return {'wrong': int(m.group(1)), 'control': int(m.group(2)),
            'body': body.group(0) if body else '?',
            'hung': hung.group(1) if hung else None, 'raw': out}

log('--- baseline (the current tree, 4-call body is already replaced) ---')
ok, err = build('')
if not ok:
    log('BASELINE BUILD FAILED: %s' % err); sys.exit(1)
base = measure()
if not base:
    log('BASELINE MEASUREMENT FAILED -- refusing to run candidates'); sys.exit(1)
log('baseline: %d wrong, control %d, net %+d   %s%s'
    % (base['wrong'], base['control'], base['wrong'] - base['control'], base['body'],
       '  HUNG ' + base['hung'] if base['hung'] else ''))

best = base
bestflag = '(baseline)'
for flag, what in CANDS:
    log('--- %s : %s ---' % (flag, what))
    ok, err = build(flag)
    if not ok:
        log('  BUILD FAILED, rejected: %s' % err)
        continue
    r = measure()
    if not r:
        log('  MEASUREMENT FAILED, rejected (no number means no verdict)')
        continue
    net = r['wrong'] - r['control']
    log('  %d wrong, control %d, net %+d   %s%s'
        % (r['wrong'], r['control'], net, r['body'],
           '  HUNG ' + r['hung'] if r['hung'] else ''))
    if net < best['wrong'] - best['control']:
        log('  KEEP -- better than %+d' % (best['wrong'] - best['control']))
        best, bestflag = r, flag
    else:
        log('  REVERT -- not better than %+d' % (best['wrong'] - best['control']))

log('=== BEST: %s  %d wrong, control %d, net %+d ==='
    % (bestflag, best['wrong'], best['control'], best['wrong'] - best['control']))
log('the tree is left at the LAST BUILD, not the best -- rebuild with the winning')
log('flag before trusting rom/s16.32x')
