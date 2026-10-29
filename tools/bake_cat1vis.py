#!/usr/bin/env python3
"""Bake the CAT1 VISIBILITY bitmaps: which cat1 tiles can actually be seen.

*** VOID -- THE PLANE LABELS IN THIS FILE ARE INVERTED (2026-09-14). ***
Pages 0-4 are the FOREGROUND and 5-9 the BACKGROUND, not the reverse.
Measured, not argued: latch_layer_regs (m_main.c:3103) fills snap[0]
from text word 0x740 and snap[1] from 0x741, the punch pass takes
snap[0] as fg (m_main.c:15239), and docs/audit/pagesel_census.txt shows
which0 holding 0-4 and which1 holding 5-9 in every sampled frame.
bake_cat1hole.py:15-16 had it right all along.

So every cat1 cell (all 23,432 of them, and zero on pages 5-9) sits on
the TOPMOST tile plane. jts16_prio.v:83-95 tests the foreground first,
so no tile plane is above them: tile-occlusion of cat1 is ZERO, not
47.7%. This file measures whether the background covers the foreground,
which is the impossible direction. Do not reuse its numbers.

    tools/bake_cat1vis.py [--stats]

A background cat1 tile sitting under a FULLY OPAQUE foreground tile is
never reached by the priority mixer, so it contributes nothing to the
final image and never needs composing. That is not an inference:
`srcref/jtcores/cores/s16/hdl/jts16_prio.v:83-95` tests lyr1 (scr1, the
foreground) before lyr2 (scr2, the background) unconditionally, and
tile_or_obj (line 58) returns the sprite or the tile but never nothing.
So an opaque foreground pixel ends the search above the background.

Measured 50% of PAGE-0 cat1 cells across the five scenes, 83% in scene 2
(docs/log/LOOP-DECOMPILE.md 57, 58).

MIND THE DENOMINATOR -- two populations, one measurement (NOTES 93,
LOOP29 303). Occlusion can only be TESTED where BG page 0 sits under FG
page 7, so entry 57's 50% is 2,373 of the **4,724 PAGE-0 cat1 cells**.
The --stats table below divides the same 2,373 by cat1 cells across all
TEN tilemap pages (23,432) and so reports 10%. Both are right; they are
not the same question. Verified exactly: page-0 cat1 = 4,724 and scene 2
= 83.4%, reproducing entry 57 to the cell.

The one that SIZES a card is the page-0 figure, because cells outside the
composable BG page are never composed and so cannot be saved.

Emits, beside sh_src/cat1map.bin (which says WHICH tiles are cat1):
  sh_src/cat1vis.bin   1 = this cat1 tile is VISIBLE and must be composed
                       0 = occluded, or not cat1 at all
  sh_src/cat1vis.h     counts per scene
Same geometry as cat1map: one bit per tile, MSB first, 2560 B per scene.
"""
import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.environ.get('GAME', 'altbeast')
ROM = os.path.join(ROOT, 'roms', GAME, 'prog68k.bin')
TILES = os.path.join(ROOT, 'sh_src', 'tiles.bin')

SCENE_TABLE, SCENES, TILES_N = 0x1CE2, 5, 20480
BYTES_PER_SCENE = TILES_N // 8
# THE PAIRING IS (N, N+5) AND IT IS LOCKED -- measured, not assumed
# (NOTES 95, docs/audit/pagesel_census.txt: the arcade's own page selects at
# text words 0x740/0x741, frames 120-5400, every quadrant, delta ALWAYS +5).
#   (0,5) (1,6) (2,7) (3,8) (4,9)
# One foreground per background. Not selectable, so there is nothing to
# maximise over. Use --pairs with this table; see DEFAULT_PAIRS below.
#
# (0,7) WAS NEVER A SIMPLIFICATION -- IT IS A PAIRING THAT NEVER OCCURS.
# Page 0's occluder is page 5. The 2,373 this file used to report tested
# page 0 against a foreground it is never drawn under, so that figure is
# not a floor or a ceiling on the real saving; it is a different quantity
# and it can move either way. Kept only as the legacy default so old
# invocations do not silently change meaning.
BG_PAGE, FG_PAGE = 0, 7          # legacy default; NOT a pairing the game makes
# NOTES 94 / LOOP29 305: that single hardcoded pairing TESTS only 2,373 of
# 23,432 cat1 cells. The other 18,708 are not known to be visible -- they
# are UNEXAMINED, and counted visible by default. --pairs takes the real
# per-scene table instead: "scene:bg:fg,scene:bg:fg,..." .
#
# Structure derived from the rom (LOOP29 305), which bounds the table:
#   * the cat1 MASK comes in exactly two flavours -- pages 0-4 share one,
#     pages 5-9 the other. That is scr2 (background) and scr1 (foreground),
#     five pages each; the tile CODES differ across all ten.
#   * ALL 23,432 cat1 cells are on pages 0-4. Cat-1 is a background-plane
#     phenomenon here and the foreground plane supplies the occluders, so
#     only bg in 0..4 against fg in 5..9 is a legal overlay.
#   * CEILING over the best legal fg for each bg page: 12,080 of 23,432 =
#     51.6%, against today's 2,373 = 10.1%. So a real pairing table can add
#     at most 5.1x, and cannot add more.
BG_GROUP, FG_GROUP = range(0, 5), range(5, 10)
# The locked table, ready to pass: --pairs "$(python3 -c 'print(DEFAULT_PAIRS)')"
DEFAULT_PAIRS = ','.join(f'{s}:{b}:{b+5}' for s in range(SCENES) for b in range(5)) \
    if 'SCENES' in dir() else ''
# Result with it (LOOP29 306): 11,187 of 23,432 cat1 cells occluded = 47.7%,
# against the legacy 2,373 = 10.1%. That is 4.7x, and 93% of the 12,080 that
# free choice of foreground would allow -- the game's locked pairing is
# very nearly optimal for occlusion.


def hi_pass(rom, src):
    out = bytearray(); p = src
    while len(out) < TILES_N and p + 1 < len(rom):
        out.extend(bytes([rom[p + 1]]) * (rom[p] + 1)); p += 2
    return out[:TILES_N], p


def lo_pass(rom, src):
    out = bytearray(); p = src
    while len(out) < TILES_N and p < len(rom):
        d0 = rom[p]; p += 1
        if d0:
            out.append(d0); continue
        d2 = rom[p]; p += 1
        if d2 == 0:
            out.append(0); continue
        out.extend(b'\x00' * d2)
    return out[:TILES_N]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--stats', action='store_true')
    ap.add_argument('--pairs', metavar='S:BG:FG,...',
                    help='per-scene (bg,fg) page pairings to test for occlusion, '
                         'replacing the single hardcoded (0,7). A scene may list '
                         'several pairs; a cell counts occluded if ANY of its '
                         "scene's pairs occludes it. Only bg 0-4 / fg 5-9 is legal "
                         '(LOOP29 305). Without this the bake tests one pairing '
                         'and reports the other 18,708 cat1 cells as visible by '
                         'default, which is an ASSUMPTION, not a measurement.')
    a = ap.parse_args()
    with open(ROM, 'rb') as fh:
        rom = fh.read()
    if not os.path.exists(TILES):
        sys.exit('sh_src/tiles.bin missing — run tools/gen_tiles.py first')
    with open(TILES, 'rb') as fh:
        tiles = fh.read()

    def fully_opaque(idx):
        o = idx * 64
        if o + 64 > len(tiles):
            return False
        return 0 not in tiles[o:o + 64]

    # --pairs "0:0:7,0:1:9,2:3:9" -> {0: [(0,7),(1,9)], 2: [(3,9)]}
    table = {}
    if a.pairs:
        for item in a.pairs.split(','):
            sc, bg, fg = (int(x) for x in item.split(':'))
            if bg not in BG_GROUP or fg not in FG_GROUP:
                sys.exit(f'illegal pairing {item}: bg must be 0-4 (scr2), fg 5-9 (scr1) '
                         '-- see the page-group derivation at the top of this file')
            table.setdefault(sc, []).append((bg, fg))
    def pairs_for(sc):
        return table.get(sc, [(BG_PAGE, FG_PAGE)])

    maps, stats = [], []
    for s in range(SCENES):
        o = SCENE_TABLE + 6 * s
        ptr = int.from_bytes(rom[o + 2:o + 6], 'big')
        hi, after = hi_pass(rom, ptr)
        lo = lo_pass(rom, after)
        words = [(hi[i] << 8) | lo[i] for i in range(TILES_N)]
        bits = bytearray(BYTES_PER_SCENE)
        cat1 = vis = 0
        for i in range(TILES_N):
            if not (words[i] & 0x8000):
                continue
            cat1 += 1
            page, cell = divmod(i, 2048)
            if any(page == bg and fully_opaque(words[fg * 2048 + cell] & 0x1FFF)
                   for bg, fg in pairs_for(s)):
                continue                       # occluded: leave the bit clear
            bits[i >> 3] |= 0x80 >> (i & 7)
            vis += 1
        maps.append(bits)
        stats.append((s, cat1, vis))

    print('scene   cat1   visible   occluded   saving')
    print('  (cat1/saving are over ALL TEN tilemap pages; the figure that')
    print('   sizes a card is occluded / PAGE-0 cat1 = 50.2% -- NOTES 93)')
    tc = tv = 0
    for s, cat1, vis in stats:
        tc += cat1; tv += vis
        print('  %d    %5d   %5d     %5d     %3.0f%%'
              % (s, cat1, vis, cat1 - vis,
                 100.0 * (cat1 - vis) / cat1 if cat1 else 0))
    print('total  %5d   %5d     %5d     %3.0f%%'
          % (tc, tv, tc - tv, 100.0 * (tc - tv) / tc if tc else 0))
    if a.stats:
        return
    with open(os.path.join(ROOT, 'sh_src', 'cat1vis.bin'), 'wb') as fh:
        for m in maps:
            fh.write(m)
    with open(os.path.join(ROOT, 'sh_src', 'cat1vis.h'), 'w') as fh:
        fh.write('/* generated by tools/bake_cat1vis.py — 1 = this cat1 tile\n'
                 ' * is VISIBLE and must be composed; 0 = occluded by a fully\n'
                 ' * opaque foreground tile and never reached by the mixer\n'
                 ' * (jts16_prio.v:83-95). Same geometry as cat1map.bin. */\n')
        fh.write('#define CAT1VIS_BYTES_PER_SCENE %d\n' % BYTES_PER_SCENE)
        fh.write('static const unsigned short cat1vis_counts[%d] = { %s };\n'
                 % (SCENES, ', '.join(str(v) for _, _, v in stats)))
    print('wrote sh_src/cat1vis.bin and sh_src/cat1vis.h')


if __name__ == '__main__':
    main()
