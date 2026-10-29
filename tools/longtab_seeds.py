#!/usr/bin/env python3
"""Tables of absolute longs pointing into the code band -- WITH FILTERS.

    tools/longtab_seeds.py roms/<game>/prog68k.bin [--json OUT] [--class CLS.bin]

An unfiltered version of this scan FRAGMENTED the call graph (2026-09-26: 227
targets, 25 new instructions, but functions 687 -> 859, with 257-1K functions
falling 42 -> 30 and 17-64 B rising 226 -> 329 -- LOOP-DECOMPILE 19's failure).
Two false positives dominated it and both are excluded here:

  * 0x00004, 251 entries -- the 68K VECTOR TABLE. Its targets are all
    0x400-0x40C, a span of 12 bytes, so: reject a table whose target span is
    under MIN_SPAN.
  * 0x06DCA, 154 entries -- the zero-word data region beside the dispatch tables
    at 0x06D70/0x06DA0. So: reject a table whose BASE is not itself decoded code
    or is inside a known data run, given a classification map (--class, from
    tools/ghidra/f1_classify.py).

Judge the result on the FUNCTION SIZE DISTRIBUTION (tools/ghidra/f1_funcsan.py),
never on the instruction count.
"""
import argparse, json, struct, collections

ap = argparse.ArgumentParser()
ap.add_argument('rom')
ap.add_argument('--json')
ap.add_argument('--class', dest='cls')
ap.add_argument('--lo', type=lambda x: int(x, 0), default=0x400)
ap.add_argument('--hi', type=lambda x: int(x, 0), default=0x1F000)
ap.add_argument('--min-entries', type=int, default=4)
ap.add_argument('--min-span', type=lambda x: int(x, 0), default=0x20)
a = ap.parse_args()
img = open(a.rom, 'rb').read()
cls = open(a.cls, 'rb').read() if a.cls else None
be32 = lambda o: struct.unpack('>I', img[o:o + 4])[0]

runs, cur, o = [], [], 0
while o < len(img) - 4:
    v = be32(o)
    if a.lo <= v < a.hi and not v & 1:
        cur.append((o, v)); o += 4
    else:
        if len(cur) >= a.min_entries:
            runs.append(cur)
        cur = []; o += 2
if len(cur) >= a.min_entries:
    runs.append(cur)

kept, rejected = [], []
for r in runs:
    base = r[0][0]
    tg = [t for _, t in r]
    span = max(tg) - min(tg)
    why = None
    if span < a.min_span:
        why = 'target span 0x%X < 0x%X (a vector/handler table, not a jump table)' % (span, a.min_span)
    elif len(set(tg)) * 2 < len(tg):
        why = 'only %d distinct of %d entries' % (len(set(tg)), len(tg))
    elif cls and base < len(cls) and cls[base] != 1:
        why = 'base 0x%05X is not decoded code (class %d)' % (base, cls[base])
    (rejected if why else kept).append((base, len(r), span, why))

print('tables found %d; kept %d, rejected %d' % (len(runs), len(kept), len(rejected)))
for b, n, sp, why in rejected:
    print('  REJECT 0x%05X %4d entries -- %s' % (b, n, why))
tg = set()
for b, n, sp, _ in kept:
    for run in runs:
        if run[0][0] == b:
            tg |= {t for _, t in run}
print('kept tables:')
for b, n, sp, _ in kept:
    print('  0x%05X %4d entries, target span 0x%X' % (b, n, sp))
print('distinct targets: %d' % len(tg))
if a.json:
    json.dump({'seeds': sorted('%X' % t for t in tg),
               'function_seeds': sorted('%X' % t for t in tg)}, open(a.json, 'w'))
    print('-> %s' % a.json)
