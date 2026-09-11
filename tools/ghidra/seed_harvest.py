#!/usr/bin/env python3
"""Harvest code-entry seeds the Ghidra auto-analyser misses.

The analyser stops at this program's two indirect-dispatch shapes, which
is why it reaches 54% of the functions (docs/log/LOOP-DECOMPILE.md 6,16):

  A. a PC-relative table of longs
         lea  TABLE(pc),a0
         movea.l (a0,dN.w),a0
         jmp/jsr (a0)
  B. a routine pointer stored into an object field
         move.l #ADDR,$xx(aN)
     later reached by  movea.l $02(a6),a0 ; jsr (a0)

Both put the target in DATA, so nothing in the instruction stream refers
to it and the analyser never follows. This reads the objdump listing for
the shapes, reads the tables out of the ROM, and prints the seed set.

    tools/ghidra/seed_harvest.py LISTING.dis ROM.bin [--json OUT]

Run tools/ghidra/seed_apply.py on the result to disassemble them.
"""
import argparse
import json
import re
import sys

CODE_LO = 0x400          # below this is the vector table
CODE_HI = 0x1F000        # docs/log/LOOP-DECOMPILE.md 16: no code above 0x1EF1E

LINE = re.compile(r'^\s*([0-9a-f]+):\s+((?:[0-9a-f]{4} )+)\s*(\S+)\s*(.*?)\s*$')
LEA_PC = re.compile(r'^lea %pc@\(0x([0-9a-f]+)\),%(a[0-6])$')
IDX_LOAD = re.compile(r'^moveal %(a[0-6])@\(0,%d[0-7]:[wl]\),%(a[0-6])$')
IMM_LONG = re.compile(r'^movel #(-?\d+),')


def plausible(a):
    return CODE_LO <= a < CODE_HI and (a & 1) == 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('listing')
    ap.add_argument('rom')
    ap.add_argument('--json')
    ap.add_argument('--code', help='instruction address list from '
                    'tools/ghidra/instr_list.py. WITHOUT IT, immediate '
                    'routine pointers are harvested from PHANTOM sites — a '
                    '`move.l #imm,d(aN)` that only exists because a linear '
                    'sweep disassembled data. That produced 101 bogus '
                    'functions the first time (LOOP-DECOMPILE 52). Pass it '
                    'on any run after the first.')
    a = ap.parse_args()

    with open(a.rom, 'rb') as fh:
        rom = fh.read()

    rows = []
    for ln in open(a.listing):
        m = LINE.match(ln)
        if m:
            rows.append((int(m.group(1), 16), m.group(3), m.group(4)))

    tables = {}                     # table addr -> dispatch site
    for i, (addr, mn, ops) in enumerate(rows):
        if mn != 'lea':
            continue
        t = LEA_PC.match(mn + ' ' + ops)
        if not t:
            continue
        reg = t.group(2)
        # the indexed load must come within a few instructions and use the
        # register the lea just filled
        for j in range(i + 1, min(i + 5, len(rows))):
            n = IDX_LOAD.match(rows[j][1] + ' ' + rows[j][2])
            if n and n.group(1) == reg:
                tables[int(t.group(1), 16)] = addr
                break

    from_tables = {}
    for t, site in sorted(tables.items()):
        ents = []
        o = t
        while o + 4 <= len(rom):
            v = int.from_bytes(rom[o:o + 4], 'big')
            if not plausible(v):
                break
            ents.append(v)
            o += 4
        if ents:
            from_tables[t] = {'site': '0x%X' % site,
                              'entries': ['0x%X' % v for v in ents]}

    code = None
    if a.code:
        code = set(int(x, 16) for x in open(a.code).read().split())

    from_imm = {}
    skipped = 0
    for addr, mn, ops in rows:
        if mn != 'movel':
            continue
        if code is not None and addr not in code:
            skipped += 1
            continue
        m = IMM_LONG.match(mn + ' ' + ops)
        if not m:
            continue
        v = int(m.group(1)) & 0xFFFFFFFF
        if plausible(v):
            from_imm.setdefault(v, []).append('0x%X' % addr)

    seeds = set()
    for t in from_tables.values():
        seeds.update(int(e, 16) for e in t['entries'])
    seeds.update(from_imm)

    print('dispatch tables found: %d' % len(from_tables))
    for t, info in sorted(from_tables.items()):
        print('  table 0x%05X  %3d entries  dispatched at %s'
              % (t, len(info['entries']), info['site']))
    print('immediate routine pointers: %d distinct targets from %d sites'
          % (len(from_imm), sum(len(v) for v in from_imm.values())))
    if code is not None:
        print('  (%d phantom sites over data skipped by --code)' % skipped)
    else:
        print('  WARNING: no --code, phantom sites INCLUDED')
    print('TOTAL SEEDS: %d' % len(seeds))

    if a.json:
        with open(a.json, 'w') as fh:
            json.dump({'tables': from_tables,
                       'immediates': dict(('0x%X' % k, v) for k, v in from_imm.items()),
                       'seeds': ['0x%X' % s for s in sorted(seeds)]}, fh, indent=1)
        print('wrote %s' % a.json)


if __name__ == '__main__':
    main()
