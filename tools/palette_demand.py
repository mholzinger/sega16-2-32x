#!/usr/bin/env python3
"""Sprite-palette demand census for the decompile thread.

Reads the palette table out of OUR copy of the arcade program and, when
given a WRAM dump, the set of palette indices a running frame actually
holds a hardware slot for.

    tools/palette_demand.py                       # table only
    tools/palette_demand.py --wram palstate.bin   # + live set
    tools/palette_demand.py --json out.json

The table address and stride are derived from the queue-builder at
0x3BEC, not guessed: it computes src = 0x242A0 + 28*$0B and
dst = 0x840800 + 32*$0A + 2 (docs/log/LOOP-DECOMPILE.md 2).

Colour word format is the System 16 gun layout: R = bits 0-3 << 1 | bit
12, G = bits 4-7 << 1 | bit 13, B = bits 8-11 << 1 | bit 14, bit 15 is
the shade flag. 0x7FFF decodes to (31,31,31) and 0x0000 to (0,0,0),
which is the check that pins it.
"""
import argparse
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROM = os.path.join(ROOT, 'roms/altbeast/prog68k.bin')

PAL_BASE = 0x242A0      # from 0x3C3A  lea 0x242a0,%a1
PAL_STRIDE = 28         # from 0x3C32-0x3C38  ((n<<3) - n) << 2
PAL_PENS = 14           # 28 bytes / 2
PAL_COUNT = 176         # highest index the program writes is 0xAE
SLOT_BASE = 0x840800    # from 0x3C20  lea 0x840800,%a1
SLOT_STRIDE = 32        # from 0x3C1E  lslw #5
WRAM_BASE = 0xFFF400    # dump base the live set is read from
REQ_TABLE = 0xFFF500    # request table, indexed by palette_index ($0B)
REFCOUNTS = 0xFFF440    # slot refcounts, indexed by slot ($0A)
NO_SLOT = 0x3F          # 0x3B3E/0x3B48 compare against 63


def load_rom(path):
    with open(path, 'rb') as fh:
        return fh.read()


def palette(rom, index):
    off = PAL_BASE + PAL_STRIDE * index
    return [(rom[off + 2 * k] << 8) | rom[off + 2 * k + 1] for k in range(PAL_PENS)]


def rgb(word):
    r = ((word >> 0) & 0xF) << 1 | ((word >> 12) & 1)
    g = ((word >> 4) & 0xF) << 1 | ((word >> 13) & 1)
    b = ((word >> 8) & 0xF) << 1 | ((word >> 14) & 1)
    return r, g, b, (word >> 15) & 1


def live_set(wram):
    """(palette_index -> slot, slot -> refcount) from a WRAM dump at 0xFFF400."""
    req = wram[REQ_TABLE - WRAM_BASE:REQ_TABLE - WRAM_BASE + PAL_COUNT]
    rc = wram[REFCOUNTS - WRAM_BASE:REFCOUNTS - WRAM_BASE + 64]
    allocated = dict((i, s) for i, s in enumerate(req) if s < NO_SLOT)
    refs = dict((i, c) for i, c in enumerate(rc) if c)
    return allocated, refs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rom', default=ROM)
    ap.add_argument('--wram', help='ares --dump wram:0xFFF400:0x200:FILE')
    ap.add_argument('--json', help='write the census here')
    ap.add_argument('--trivial-max', type=int, default=2,
                    help='palettes with at most this many distinct colours are '
                         'reported as trivial (default 2)')
    a = ap.parse_args()

    rom = load_rom(a.rom)
    if len(rom) < PAL_BASE + PAL_STRIDE * PAL_COUNT:
        sys.exit('rom too short for the palette table')

    pals = [palette(rom, i) for i in range(PAL_COUNT)]
    sets = [frozenset(p) for p in pals]

    dups = {}
    for i, s in enumerate(pals):
        dups.setdefault(tuple(s), []).append(i)
    dup_groups = sorted((v for v in dups.values() if len(v) > 1), key=len, reverse=True)

    out = {
        'table': {'base': '0x%05X' % PAL_BASE, 'stride': PAL_STRIDE,
                  'pens': PAL_PENS, 'count': PAL_COUNT,
                  'slot_base': '0x%06X' % SLOT_BASE, 'slot_stride': SLOT_STRIDE},
        'distinct_palettes': len(dups),
        'exact_duplicate_groups': [['0x%02X' % i for i in g] for g in dup_groups],
        'palettes': [{'index': '0x%02X' % i,
                      'words': ['%04X' % w for w in pals[i]],
                      'distinct': len(sets[i])} for i in range(PAL_COUNT)],
    }

    print('palette table 0x%05X + %d*index, %d pens, %d entries'
          % (PAL_BASE, PAL_STRIDE, PAL_PENS, PAL_COUNT))
    print('distinct palettes %d of %d; %d exact-duplicate groups'
          % (len(dups), PAL_COUNT, len(dup_groups)))
    for g in dup_groups:
        print('  duplicates: %s' % ' '.join('0x%02X' % i for i in g))

    if a.wram:
        with open(a.wram, 'rb') as fh:
            wram = fh.read()
        allocated, refs = live_set(wram)
        trivial = [i for i in allocated if len(sets[i]) <= a.trivial_max]
        real = sorted(i for i in allocated if len(sets[i]) > a.trivial_max)
        print()
        print('live: %d palette indices hold a slot; %d are trivial '
              '(<=%d colours), %d are real'
              % (len(allocated), len(trivial), a.trivial_max, len(real)))
        for i in sorted(allocated):
            print('  0x%02X -> slot %-2d  distinct=%2d  refcount=%s'
                  % (i, allocated[i], len(sets[i]),
                     refs.get(allocated[i], 0)))
        union = set()
        for i in real:
            union |= sets[i]
        print('  distinct colours across the %d real palettes: %d'
              % (len(real), len(union)))
        pairs = []
        for x in range(len(real)):
            for y in range(x + 1, len(real)):
                i, j = real[x], real[y]
                pairs.append((len(sets[i] | sets[j]), i, j))
        pairs.sort()
        print('  tightest pair unions: %s'
              % ', '.join('0x%02X+0x%02X=%d' % (i, j, u) for u, i, j in pairs[:5]))
        fit = [p for p in pairs if p[0] <= 15]
        print('  pairs that fit one 15-pen line: %d' % len(fit))
        out['live'] = {
            'allocated': dict(('0x%02X' % i, s) for i, s in allocated.items()),
            'refcounts': refs,
            'trivial': ['0x%02X' % i for i in sorted(trivial)],
            'real': ['0x%02X' % i for i in real],
            'real_union_colours': len(union),
            'tightest_pairs': [['0x%02X' % i, '0x%02X' % j, u] for u, i, j in pairs[:10]],
            'pairs_fitting_one_line': len(fit),
        }

    if a.json:
        with open(a.json, 'w') as fh:
            json.dump(out, fh, indent=1)
        print('\nwrote %s' % a.json)


if __name__ == '__main__':
    main()
