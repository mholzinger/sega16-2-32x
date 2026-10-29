#!/usr/bin/env python3
"""Derive each round's colour-set table FROM THE ARCADE CORPUS and merge it into
sh_src/pal_rounds_md.h.

WHY. pal_rounds_md.h pins, per ROUND, which of the 128 S16 colour sets get an MD pen line.
Its own header says what happens to the rest: "A set absent here is REFUSED an MD line and
renders as BACKDROP (m_main.c 2357) -- black tiles, not a fallback." Round 0's table was
harvested from three sampled gameplay scenes (docs/audit/mdpen_scene_sets.txt) and holds 30
sets, all of them level art, 72-101. BUT THE WHOLE ATTRACT RUNS AT ROUND 0 -- every one of
the 497 arcade dumps in tests/demo-b/arcade reads 0 at 0xFFF142 -- and the attract draws
sets the sampled scenes never showed. Measured 2026-10-07 at the eye scene: its cells are
bank 5, codes 0x0580-0x083E, colour sets 22-32, which round 0 does not carry, so the port
drew NOTHING there -- the framebuffer is 97.2% transparent across the face while the name
table is byte-identical to the arcade's (0 of 0xD000 bytes differ) and the iris, whose set
round 0 does carry, draws correctly.

THE DERIVATION. A round's table is not a sample. It is, by construction, every set its tile
RAM references anywhere in that round, and the corpus already says which frames are which
round. So: take the frames of tests/demo-b/arcade whose 0xFFF142 is this round, dump the
arcade's TILE RAM at those frames, and collect set = (word & 0x1FFF) >> 6 over every nonzero
word. Round 0 derives 71 sets against the 30 it carried.

THE PEN MAPS come from the tables already in the header. Rounds 5-9 are harvested ATTRACT
scenes, so every set the attract draws already has a measured pen map there; this merges it
into the round that actually selects it. Nothing is invented. Where two harvested tables
disagree about a set, the lowest-numbered one wins and the tool says so.

    python3 tools/round_sets.py --round 0            # census and report, change nothing
    python3 tools/round_sets.py --round 0 --emit     # merge into sh_src/pal_rounds_md.h
"""
import argparse
import glob
import os
import re
import subprocess
import sys
import tempfile
from collections import Counter, defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ARCADE = os.path.join(ROOT, 'tests', 'demo-b', 'arcade')
PAL = os.path.join(ROOT, 'sh_src', 'pal_rounds_md.h')
ROUND_BYTE = 0x3142            # 0xFFF142 inside a 0xFFC000 work-RAM dump
TILE_BASE, TILE_LEN = 0x400000, 0x10000
SHADOW = 0xD000                # the window the port shadows: the two tile planes


def frames_of_round(r):
    out = set()
    for p in sorted(glob.glob(os.path.join(ARCADE, 'aw_*.bin'))):
        b = open(p, 'rb').read()
        if len(b) > ROUND_BYTE and (b[ROUND_BYTE] & 7) == r:
            out.add(int(os.path.basename(p).rsplit('_', 1)[1][:-4]))
    return sorted(out)


def tile_dumps(frames, outdir):
    """Arcade tile RAM at these frames, one MAME run, fresh NVRAM (the attract depends on it)."""
    env = dict(os.environ, WD_FRAMES=','.join(map(str, frames)), WD_ADDR=hex(TILE_BASE),
               WD_LEN=hex(TILE_LEN), WD_OUTDIR=outdir)
    with tempfile.TemporaryDirectory() as nv:
        subprocess.run(['mame', 'altbeast', '-rompath', './mame', '-skip_gameinfo',
                        '-video', 'none', '-sound', 'none', '-nothrottle', '-window',
                        '-resolution', '160x120', '-keyboardprovider', 'none', '-nomouse',
                        '-nojoystick', '-bench', str(max(frames) // 60 + 10),
                        '-nvram_directory', nv,
                        '-autoboot_script', 'tools/wram_dump.lua'],
                       cwd=ROOT, env=env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return sorted(glob.glob(os.path.join(outdir, 'aw_*.bin')))


def sets_referenced(paths):
    cells, first = Counter(), {}
    for p in paths:
        fr = int(os.path.basename(p).rsplit('_', 1)[1][:-4])
        b = open(p, 'rb').read()[:SHADOW]
        for i in range(0, len(b) - 1, 2):
            w = (b[i] << 8) | b[i + 1]
            if not w:
                continue
            s = (w & 0x1FFF) >> 6
            cells[s] += 1
            first.setdefault(s, fr)
    return cells, first


def parse_maps(src):
    m = re.search(r'mdr_s_map\[MDROUND_N\]\[1024\]\s*=\s*\{(.*?)\n\};', src, re.S)
    if not m:
        sys.exit('round_sets: mdr_s_map not found in pal_rounds_md.h')
    rows = re.findall(r'\{([^{}]*)\}', m.group(1))
    maps = [[int(x, 0) for x in re.findall(r'0x[0-9a-fA-F]+|\d+', r)] for r in rows]
    maps = [r for r in maps if len(r) == 1024]
    return m, maps


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--round', type=int, default=0)
    ap.add_argument('--key', type=lambda x: int(x, 0), default=None,
                    help='work-RAM address to split the round on (e.g. 0xFFF022)')
    ap.add_argument('--emit', action='store_true',
                    help='REFUSED for now: see the note printed on use')
    a = ap.parse_args()

    frames = frames_of_round(a.round)
    if not frames:
        sys.exit('round_sets: no arcade dumps at round %d -- this round has no corpus yet'
                 % a.round)
    print('round %d: %d arcade frames in the corpus (%d..%d)'
          % (a.round, len(frames), frames[0], frames[-1]))
    with tempfile.TemporaryDirectory() as td:
        paths = tile_dumps(frames, td)
        print('round %d: %d tile-RAM dumps taken' % (a.round, len(paths)))
        cells, first = sets_referenced(paths)

    src = open(PAL).read()
    m, maps = parse_maps(src)
    cur = {s for s in range(128) if any(maps[a.round][s * 8:s * 8 + 8])}
    der = sorted(cells)
    need = [s for s in der if s not in cur]
    print('round %d table: %d sets now, %d derived, %d missing'
          % (a.round, len(cur), len(der), len(need)))
    print('  now     : %s' % sorted(cur))
    print('  derived : %s' % der)
    print('  missing : %s' % need)

    src_of = defaultdict(dict)
    for ri in range(len(maps)):
        for s in need:
            m8 = bytes(maps[ri][s * 8:s * 8 + 8])
            if any(m8):
                src_of[s][ri] = m8
    orphan = [s for s in need if not src_of[s]]
    clash = sorted(s for s in need if len(set(src_of[s].values())) > 1)
    print('  of the missing: %d have no pen map anywhere %s, %d disagree between tables %s'
          % (len(orphan), orphan, len(clash), clash))
    for s in clash:
        print('    set %3d: tables %s -- taking %d'
              % (s, sorted(src_of[s]), min(src_of[s])))
    if orphan:
        print('  REFUSING to invent a pen map for %s; they stay absent.' % orphan)

    if not a.emit:
        print('\n(report only)')
        return 0
    print('\n--emit is REFUSED. Merging mdr_s_map alone does NOTHING: measured 2026-10-07,')
    print('42 sets merged into round 0, tiles_md.bin 252 -> 330 blocks, and the picture did')
    print('not move by one byte on any of attract_parity\'s nine scenes. A set also needs an')
    print('mdr_s_line (which of the 3 MD lines of 16 it uses), an mdr_s_used flag, and its')
    print('colours present in mdr_line_c for that line. That is an ALLOCATION across 3x15')
    print('usable colours, which tools/bake_tilecram.py owns. Until this tool can drive that')
    print('allocation it reports and does not write.')
    return 2

    merged = [list(r) for r in maps]
    added = 0
    for s in need:
        if not src_of[s]:
            continue
        merged[a.round][s * 8:s * 8 + 8] = list(src_of[s][min(src_of[s])])
        added += 1
    rows = ['    { ' + ', '.join(str(v) for v in r) + ' }' for r in merged]
    body = ',\n'.join(rows)
    new = (src[:m.start(1)] + '\n' + body + '\n' + src[m.end(1):])
    open(PAL, 'w').write(new)
    print('\nmerged %d sets into round %d of %s' % (added, a.round, os.path.relpath(PAL, ROOT)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
