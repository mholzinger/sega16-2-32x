#!/usr/bin/env python3
"""WHOLE-SCREEN black-cell census over a GAMEPLAY route.

    tools/black_cells.py <shot-dir> [--rows 0-27] [--cols 0-39]

WHY THIS EXISTS. Every earlier black-tile census was a BOTTOM-BAND census over
the ATTRACT demo (view rows 24-27), and it honestly read 0 while Mike saw black
tiles all through level-1 GAMEPLAY. The reason is load: gameplay runs 1018/1024
VRAM slots with 123/128 hash sets full, attract runs 499 and 0, so the
slot-pressure refusal that causes the defect cannot happen in the window the old
instrument watched (LESSONS 2026-09-26 06:00).

A cell is BLACK when all 64 pixels of its 8x8 block are pure black. Legitimate
black art exists, so the number is only meaningful as a DIFFERENCE between two
roms over the same route at the same frames -- never as an absolute.
"""
import argparse, sys
from pathlib import Path
from PIL import Image

ap = argparse.ArgumentParser()
ap.add_argument('shotdir')
ap.add_argument('--rows', default='0-27')
ap.add_argument('--cols', default='0-39')
a = ap.parse_args()
r0, r1 = (int(x) for x in a.rows.split('-'))
c0, c1 = (int(x) for x in a.cols.split('-'))

def cells(p):
    im = Image.open(p).convert('RGB')
    if im.size != (320, 224):
        if im.size == (1415, 243):        # ares window capture
            im = im.crop((65, 11, 65 + 1280, 11 + 224)).resize((320, 224), Image.NEAREST)
        else:
            im = im.resize((320, 224), Image.NEAREST)
    px = im.load()
    out = []
    for r in range(r0, min(r1 + 1, 28)):
        for c in range(c0, min(c1 + 1, 40)):
            blk = True
            for y in range(r * 8, r * 8 + 8):
                for x in range(c * 8, c * 8 + 8):
                    if px[x, y] != (0, 0, 0):
                        blk = False; break
                if not blk: break
            if blk: out.append((r, c))
    return out

tot = 0
per = {}
for p in sorted(Path(a.shotdir).glob('*.png')):
    cl = cells(p)
    per[p.name] = len(cl)
    tot += len(cl)
    print('  %-28s %4d black cells%s' % (p.name, len(cl),
          '   ' + ' '.join('r%dc%d' % rc for rc in cl[:8]) if cl else ''))
print('\nTOTAL black cells over %d shots: %d  (mean %.1f)'
      % (len(per), tot, tot / max(1, len(per))))
