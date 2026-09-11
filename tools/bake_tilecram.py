#!/usr/bin/env python3
"""Bake the per-scene MD CRAM assignment for the TILE layers.

    tools/bake_tilecram.py [--stats]

The whole tile layer of every scene fits in the Mega Drive's four CRAM
lines (docs/log/LOOP-DECOMPILE.md 60, 61), which means both tile planes
can be VDP-rendered instead of composed in software.

Two facts make it work and neither is a compromise:
  - MD CRAM is 3 bits per channel against System 16's 5, so colours
    collapse on quantisation. ARCHITECTURE.md:838 already measured that
    loss at max 2 in 0-31 and called the images indistinguishable; this
    reuses it rather than introducing anything.
  - System 16 tile palettes share colours heavily: scene 0's worst
    viewport uses 25 palettes drawn from just 24 distinct MD colours.

Emits per scene: four 16-entry CRAM lines, and for each System 16 tile
palette the line it lives on plus the slot each of its 7 pens maps to.
That pen map is what a tile bake needs to rewrite pixel values with.

  sh_src/tilecram.bin   5 scenes x 4 lines x 16 words (MD colour format)
  sh_src/tilecram.h     per-palette line + pen mapping
"""
import argparse
import os
import random
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.environ.get('GAME', 'altbeast')
ROM = os.path.join(ROOT, 'roms', GAME, 'prog68k.bin')
SCENES, TILES_N, LINES, SLOTS = 5, 20480, 4, 16
FG_PAGE, BG_PAGE = 0, 5          # measured live, entry 59
VIS_ROWS, VIS_COLS = range(4, 32), 40


def load():
    with open(ROM, 'rb') as fh:
        return fh.read()


def w16(rom, o):
    return (rom[o] << 8) | rom[o + 1]


def md(v):
    """System 16 colour word -> MD 3-bit-per-gun triple."""
    r = ((v >> 0) & 0xF) << 1 | ((v >> 12) & 1)
    g = ((v >> 4) & 0xF) << 1 | ((v >> 13) & 1)
    b = ((v >> 8) & 0xF) << 1 | ((v >> 14) & 1)
    return (r >> 2, g >> 2, b >> 2)


def md_word(c):
    return (c[2] << 9) | (c[1] << 5) | (c[0] << 1)   # MD CRAM: 0000BBB0GGG0RRR0


def unpack(rom, ptr):
    hi = bytearray(); p = ptr
    while len(hi) < TILES_N and p + 1 < len(rom):
        hi.extend(bytes([rom[p + 1]]) * (rom[p] + 1)); p += 2
    lo = bytearray()
    while len(lo) < TILES_N and p < len(rom):
        d0 = rom[p]; p += 1
        if d0:
            lo.append(d0); continue
        d2 = rom[p]; p += 1
        if d2 == 0:
            lo.append(0); continue
        lo.extend(b'\x00' * d2)
    return [(hi[i] << 8) | lo[i % len(lo)] for i in range(TILES_N)]


def worst_viewport(words, cols):
    best = None
    fg = words[FG_PAGE * 2048:(FG_PAGE + 1) * 2048]
    bg = words[BG_PAGE * 2048:(BG_PAGE + 1) * 2048]
    for c0 in range(64):
        pal = set()
        for r in VIS_ROWS:
            for dc in range(VIS_COLS):
                c = (c0 + dc) & 63
                for t in (fg[r * 64 + c], bg[r * 64 + c]):
                    if t & 0x1FFF:
                        pal.add((t >> 6) & 0x7F)
        u = set()
        for p in pal:
            u |= cols(p)
        if best is None or len(u) > best[1]:
            best = (sorted(pal), len(u))
    return best[0]


def pack(pal, cols, order):
    """Best-fit: every palette must sit ENTIRELY inside one line, because a
    tile selects one line for all its pens."""
    groups = [set() for _ in range(LINES)]
    for p in order:
        c = cols(p)
        cand = sorted((len(g | c) - len(g), i) for i, g in enumerate(groups)
                      if len(g | c) <= SLOTS - 1)
        if not cand:
            return None
        groups[cand[0][1]] |= c
    return groups


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--stats', action='store_true')
    a = ap.parse_args()
    rom = load()
    out_bin, out_h = [], []
    for s in range(SCENES):
        o = 0x1CE2 + 6 * s
        blk = w16(rom, o) & 3
        base = 0x232A0 + blk * 0x400
        words = unpack(rom, int.from_bytes(rom[o + 2:o + 6], 'big'))

        def cols(p, _b=base):
            return frozenset(md(w16(rom, _b + p * 16 + 2 * k)) for k in range(1, 8))

        pal = worst_viewport(words, cols)
        order = sorted(pal, key=lambda p: -len(cols(p)))
        groups = pack(pal, cols, order)
        if groups is None:
            random.seed(7)
            for _ in range(20000):
                sh = order[:]; random.shuffle(sh)
                groups = pack(pal, cols, sh)
                if groups:
                    break
        if groups is None:
            sys.exit('scene %d: no %d-line packing found' % (s, LINES))

        slot = []
        for g in groups:
            m = {c: i + 1 for i, c in enumerate(sorted(g))}   # slot 0 = transparent
            slot.append(m)
        assign = {}
        for p in pal:
            c = cols(p)
            for li, g in enumerate(groups):
                if c <= g:
                    assign[p] = (li, [slot[li][md(w16(rom, base + p * 16 + 2 * k))]
                                      for k in range(1, 8)])
                    break
        line_words = []
        for li, g in enumerate(groups):
            row = [0] * SLOTS
            for c, i in slot[li].items():
                row[i] = md_word(c)
            line_words.append(row)
        out_bin.append(line_words)
        out_h.append((s, len(pal), [len(g) for g in groups], assign))
        print('scene %d: %2d palettes, lines %s, %d slots used'
              % (s, len(pal), [len(g) for g in groups], sum(len(g) for g in groups)))

    if a.stats:
        return
    with open(os.path.join(ROOT, 'sh_src', 'tilecram.bin'), 'wb') as fh:
        for sc in out_bin:
            for row in sc:
                for v in row:
                    fh.write(bytes([(v >> 8) & 0xFF, v & 0xFF]))
    with open(os.path.join(ROOT, 'sh_src', 'tilecram.h'), 'w') as fh:
        fh.write('/* generated by tools/bake_tilecram.py — per-scene MD CRAM\n'
                 ' * for the tile layers, plus the line and pen mapping for\n'
                 ' * every System 16 tile palette in the worst-case viewport.\n'
                 ' * See docs/log/LOOP-DECOMPILE.md 60-61. */\n')
        fh.write('#define TILECRAM_SCENES %d\n#define TILECRAM_LINES %d\n'
                 % (SCENES, LINES))
        for s, n, sizes, assign in out_h:
            fh.write('/* scene %d: %d palettes, line fill %s */\n' % (s, n, sizes))
            fh.write('static const unsigned char tilepal_line_%d[128] = {' % s)
            fh.write(','.join(str(assign.get(p, (0xFF, None))[0] & 0xFF)
                              for p in range(128)))
            fh.write('};\n')
    print('wrote sh_src/tilecram.bin and sh_src/tilecram.h')


if __name__ == '__main__':
    main()
