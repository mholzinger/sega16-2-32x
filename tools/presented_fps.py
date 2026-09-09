#!/usr/bin/env python3
"""PRESENTED FPS — how many DISTINCT pictures reach the screen per second.

    tools/presented_fps.py ROM [--windows 1800,2600,3400] [--input CSV]

Counts, over 60 consecutive ares frames, how many differ from their
predecessor by more than 0.25% of the active area (a threshold well
above dither/HUD noise but far below a scroll step) and how many change
more than 5% (a real world update: a scroll step, a sprite moving).

MOTION (the >5% count) IS THE HEADLINE.  The any-change count is the
same trap anim_rate.py fell into: opt1 read 12 "new pictures" at f1800
of which only 2 were substantial, so the picture updated twice a second
while small bits flickered - and 21 fps was the wrong story to tell
about it (2026-09-09, Mike: "zero moving frames").

Why not anim_rate.py: its 0.05% threshold counts noise as animation and
a confetti build scored 3x the shipping line on it (LOOP28 97).  Why not
gameplay_speed.py: that measures the game's LOGIC advancing, which can
read 85% on a build whose picture updates 11 times a second (LOOP28 93).
This measures the picture, which is the thing being complained about.
"""
import argparse, os, subprocess, sys, tempfile
import numpy as np
from PIL import Image
ARES = os.path.expanduser("~/src/ares-debug/build_macos/headless-ui/Release/ares-headless")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def window(rom, start, inp, n=60, thresh=0.25):
    td = tempfile.mkdtemp()
    cmd = [ARES, "--frames", str(start + n + 2), "--input", inp]
    for f in range(start, start + n):
        cmd += ["--screenshot", f"{f}:{td}/f{f}.png"]
    cmd.append(rom)
    subprocess.run(cmd, capture_output=True)
    prev, changes = None, []
    for f in range(start, start + n):
        p = f"{td}/f{f}.png"
        if not os.path.exists(p):
            continue
        a = np.asarray(Image.open(p).convert("RGB")).astype(np.int16)
        if a.shape[1] >= 1345:
            a = a[19:243, 65:1345]
        if prev is not None:
            changes.append(100.0 * (np.abs(a - prev).sum(axis=2) > 12).mean())
        prev = a
    return sum(1 for c in changes if c > thresh), changes

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom"); ap.add_argument("--windows", default="1800,2600,3400")
    ap.add_argument("--input", default=None)
    a = ap.parse_args()
    inp = a.input or os.path.join(ROOT, "discover/inputs/play_level1.csv")
    starts = [int(x) for x in a.windows.split(",") if x]
    got = []; allnew = []
    for s in starts:
        n, ch = window(a.rom, s, inp)
        big = sum(1 for c in ch if c > 5)
        got.append(big); allnew.append(n)
        print(f"  f{s}: MOTION {big:2d}/60   (any-change {n:2d}/60)")
    print(f"{os.path.basename(a.rom)}: MOTION {sum(got)/len(got):.1f} fps   "
          f"any-change {sum(allnew)/len(allnew):.1f} fps   motion windows {got}")

if __name__ == "__main__":
    main()
