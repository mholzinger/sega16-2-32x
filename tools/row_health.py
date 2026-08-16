#!/usr/bin/env python3
"""Per-band update rate from an ares capture corpus.

Mike spotted by eye that one horizontal band of the screen animated
cleanly while the rest stuttered. This measures that: for every game
row, how many capture frames pass between REAL pixel changes. At a
~57fps capture of a 20Hz display, a fully-updating row reads ~2.85.

  tools/row_health.py [dir] [start] [count]

CONFOUND, and it is a big one: a row that never changes because nothing
is happening there (sky, a static HUD) is indistinguishable from a row
the pipeline is starving. Compare bands with comparable content, and
prefer comparing the SAME band between two builds.
"""
import sys, glob
import numpy as np
from PIL import Image

d = sys.argv[1] if len(sys.argv) > 1 else 'screenshots'
start = int(sys.argv[2]) if len(sys.argv) > 2 else 3100
count = int(sys.argv[3]) if len(sys.argv) > 3 else 300
fs = sorted(glob.glob(f'{d}/frame_*.png'))[start:start + count]
if len(fs) < 10:
    sys.exit(f'need >=10 frames, found {len(fs)}')

full = np.asarray(Image.open(fs[0]).convert('L'), dtype=np.int16)
r = np.where(full.max(axis=1) > 24)[0]
c = np.where(full.max(axis=0) > 24)[0]
top, bot, lft, rgt = r[0], r[-1], c[0], c[-1]
sc = 224.0 / (bot - top + 1)
idx = (np.arange(bot - top + 1) * sc).astype(int).clip(0, 223)
cnt = np.bincount(idx, minlength=224).clip(1)

prev = None
hits = np.zeros(224)
n = 0
for f in fs:
    g = np.asarray(Image.open(f).convert('L'), dtype=np.int16)[top:bot + 1, lft:rgt + 1]
    if prev is not None:
        acc = np.zeros(224)
        np.add.at(acc, idx, (np.abs(g - prev) > 24).sum(axis=1))
        hits += ((acc / cnt) > 1.5)
        n += 1
    prev = g
rate = hits / n
interval = np.where(rate > 0, 1.0 / np.maximum(rate, 1e-9), np.inf)

print(f'{len(fs)} frames from {fs[0].split("/")[-1]}')
print('band          rows       interval   never-updating rows')
for name, a, b in [("R0 slave", 0, 35), ("R0 master", 36, 71), ("R1 slave", 72, 107),
                   ("R1 master", 108, 143), ("R2 slave", 144, 183), ("R2 master", 184, 223)]:
    v = interval[a:b + 1]
    fin = v[np.isfinite(v)]
    print(f'  {name:10} {a:3}-{b:3}   {fin.mean() if len(fin) else float("inf"):7.2f}   {(~np.isfinite(v)).sum()}')
