#!/usr/bin/env python3
"""Histogram tools/video_writers.lua's log: writes per region per 68K function.

    tools/video_writers_hist.py /tmp/vw/vw.log [docs/audit/func_ranges.txt]
"""
import sys, re, collections, bisect
log = sys.argv[1]
ranges = sys.argv[2] if len(sys.argv) > 2 else 'docs/audit/func_ranges.txt'
starts, names, ends = [], [], []
for ln in open(ranges):                      # "0x00404 0x00407 0x00404 irq4_handler"
    f = ln.split()
    if len(f) < 4 or not f[0].startswith('0x'): continue
    a, b, ent, nm = int(f[0], 16), int(f[1], 16), int(f[2], 16), f[3]
    if b < a: continue
    starts.append(a); ends.append(b); names.append(nm if nm.startswith('FUN_') or not ent else f'{nm}@{ent:05X}')
order = sorted(range(len(starts)), key=lambda i: starts[i])
starts = [starts[i] for i in order]; ends = [ends[i] for i in order]; names = [names[i] for i in order]
def fn(pc):
    i = bisect.bisect_right(starts, pc) - 1
    if i >= 0 and pc <= ends[i]: return names[i]
    return f'?{pc:06X}'
# --frames: bin by the "F <frame> <state>" markers the lua emits once armed, and print
# per-state writer lists (which functions write which region in which game state)
by_state = collections.defaultdict(lambda: collections.defaultdict(collections.Counter))
state, frame = None, None
per = collections.defaultdict(collections.Counter)
pcs = collections.defaultdict(collections.Counter)
total = collections.Counter()
for ln in open(log, errors='replace'):
    mf = re.match(r'F (\d+) ([0-9A-F]+)', ln)
    if mf:
        frame = int(mf.group(1)); state = mf.group(2); continue
    m = re.match(r'V (\w+) ([0-9A-F]+) ([0-9A-F]+) ([0-9A-F]+)', ln)
    if not m: continue
    reg, pc = m.group(1), int(m.group(2), 16)
    total[reg] += 1; per[reg][fn(pc)] += 1; pcs[reg][pc] += 1
    if state is not None: by_state[state][reg][fn(pc)] += 1
if '--frames' in sys.argv:
    for st in sorted(by_state):
        print(f"== state {st}:")
        for reg in ('tile', 'text', 'spr', 'pal'):
            if by_state[st][reg]:
                print(f"   {reg:5s} " + ', '.join(f'{f}={n}' for f, n in by_state[st][reg].most_common(8)))
for reg in ('tile', 'text', 'spr', 'pal'):
    print(f"== {reg}: {total[reg]} writes")
    for f, n in per[reg].most_common(12):
        top = [f'{p:06X}' for p, _ in sorted(((p, c) for p, c in pcs[reg].items() if fn(p) == f), key=lambda x: -x[1])[:3]]
        print(f"   {n:7d}  {f}  pcs {' '.join(top)}")
