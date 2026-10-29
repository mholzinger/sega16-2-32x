#!/usr/bin/env python3
"""Rank the SH-2's functions by time, from ares' PC-bucketed profile.

    ares-headless --frames N --profile p.csv build/port/rom/s16.32x
    python3 tools/sh2_profile.py p.csv [build/port/rom/s16.lst] [top]

ares emits per-PC instruction and cycle counts for BOTH SH-2s; this attributes each PC to
the nearest preceding symbol in the linker map and ranks by cycles. The map is `nm -n`
output (rom/s16.lst), so the symbol that owns a PC is the last one at or below it.

THE RANKING IS ARES', AND ARES CHARGES INSTRUCTION CYCLES ONLY -- no SDRAM wait, no cart
fetch penalty, no data cache (LESSONS, "ares charges instructions only"). So this says which
functions EXECUTE the most, which is the right input for deciding what to move into
.ramtext, but the payoff of moving them is a RIG measurement and never this number.
"""
import csv
import os
import sys
from collections import defaultdict


def symbols(path):
    out = []
    for ln in open(path):
        p = ln.split()
        if len(p) >= 3 and len(p[0]) == 8:
            try:
                out.append((int(p[0], 16), p[2]))
            except ValueError:
                pass
    out.sort()
    return out


def owner(syms, pc):
    lo, hi = 0, len(syms) - 1
    if not syms or pc < syms[0][0]:
        return None
    while lo < hi:
        mid = (lo + hi + 1) // 2
        if syms[mid][0] <= pc:
            lo = mid
        else:
            hi = mid - 1
    return syms[lo][1]


def main():
    argv = [a for a in sys.argv[1:] if not a.startswith('--')]
    cpu = None
    for a in sys.argv[1:]:
        if a.startswith('--cpu'):
            cpu = a.split('=', 1)[1] if '=' in a else sys.argv[sys.argv.index(a) + 1]
    prof = argv[0]
    lst = argv[1] if len(argv) > 1 else 'build/port/rom/s16.lst'
    top = int(argv[2]) if len(argv) > 2 else 25
    syms = symbols(lst)
    if not syms:
        sys.exit('sh2_profile: no symbols in %s' % lst)
    cyc = defaultdict(int)
    ins = defaultdict(int)
    tot = 0
    with open(prof) as fh:
        for row in csv.DictReader(fh):
            if cpu and row.get('cpu') != cpu:
                continue
            try:
                pc = int(row.get('pc', row.get('PC', '0')), 0)
                c = int(row.get('cycles', row.get('cycle', '0')) or 0)
                i = int(row.get('instructions', row.get('insns', '0')) or 0)
            except (ValueError, TypeError):
                continue
            who = owner(syms, pc) or '0x%08X' % pc
            cyc[who] += c
            ins[who] += i
            tot += c
    if not tot:
        sys.exit('sh2_profile: no cycles in %s (columns: %s)' % (prof, ','.join(
            next(csv.reader(open(prof))))))
    print('SH-2 %s, %d PCs, %d cycles' % (cpu or 'both', len(cyc), tot))
    print('%-44s %14s %7s %14s' % ('symbol', 'cycles', 'share', 'instructions'))
    for name, c in sorted(cyc.items(), key=lambda kv: -kv[1])[:top]:
        print('%-44s %14d %6.2f%% %14d' % (name[:44], c, 100.0 * c / tot, ins[name]))
    print('%-44s %14d' % ('TOTAL', tot))


if __name__ == '__main__':
    sys.exit(main())
