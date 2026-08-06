#!/usr/bin/env python3
"""Strobe census over a directory of ares frame captures.

Usage: strobe_scan.py <dir> [glob]      (default glob: frame_*.png)

WHY THIS EXISTS: the black-frame strobe is invisible to MAME by
construction (it latches FBCTL immediately and never defers — see
LOOP.md iteration 7c), so for a long time the only way to see it was a
savestate counter and Mike's eyes. A full ares capture run makes the
symptom itself measurable locally, which is a much shorter loop.

NO PNG DECODE. A fully-black frame compresses to a few hundred bytes
against ~570KB for a content frame, so file size alone separates them
by two orders of magnitude — 10k frames scan in under a second instead
of the minutes a pure-python decoder would take.

Prints the overall rate, then groups blacks into BURSTS (>1s apart).
That grouping is the useful part: a uniform rate would mean a constant
overrun, whereas bursts mean the overrun is LOAD-CORRELATED and the fix
has to target the worst case, not the mean.
"""
import glob
import os
import statistics
import sys
from collections import Counter


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else "screenshots"
    pat = sys.argv[2] if len(sys.argv) > 2 else "frame_*.png"
    fs = glob.glob(os.path.join(d, pat))
    if not fs:
        print(f"no frames matching {pat} in {d}")
        return
    # frame index from the first run of digits in the basename
    def idx_of(p):
        b = "".join(c if c.isdigit() else " " for c in os.path.basename(p))
        return int(b.split()[0]) if b.split() else 0

    # SORT NUMERICALLY, not lexically. Captures named 1.png/10.png/100.png
    # sort as strings into 1,10,100,1000,1001,... so frame indices come out
    # non-monotonic and the burst grouping below reports negative spans.
    sz = sorted((idx_of(f), os.path.getsize(f)) for f in fs)
    med = statistics.median(s for _, s in sz)
    thr = med * 0.06
    dark = [i for i, s in sz if s < thr]
    print(f"frames={len(fs)} median={med / 1024:.1f}KB "
          f"threshold={thr / 1024:.2f}KB")
    print(f"BLACK={len(dark)} ({100.0 * len(dark) / len(fs):.2f}% of frames)")
    if not dark:
        return
    gaps = [b - a for a, b in zip(dark, dark[1:])]
    if gaps:
        print(f"gap histogram (top 6): {Counter(gaps).most_common(6)}")
        print("  a dominant gap = the strobe period; 4 means 3 good frames "
              "then a black one, which is the 3-window cycle plus one.")
    bursts, cur = [], [dark[0]]
    for i in dark[1:]:
        if i - cur[-1] > 60:
            bursts.append(cur)
            cur = [i]
        else:
            cur.append(i)
    bursts.append(cur)
    print(f"{len(bursts)} bursts (split at >60 frames = 1s):")
    for b in bursts:
        span = b[-1] - b[0] + 1
        print(f"  {b[0]:6d}-{b[-1]:6d}  {len(b):3d} black / {span:4d} frames"
              f"  = {100.0 * len(b) / span:3.0f}% of the burst")


if __name__ == "__main__":
    main()
