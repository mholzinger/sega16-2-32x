#!/usr/bin/env python3
"""Oracle grading — cadence/scroll metrics over a folder of frames.

The reference corpus is Mike's frame-true arcade capture
(ref_arcade/ref_NNNNNN.png, 320x224, one PNG per emulated 60Hz frame,
via tools/ref_dump.lua). The same metrics run over an ares screenshot
burst of our build, so both sides are graded by ONE instrument.

    oracle_grade.py cadence <dir> <first> <last> [step]
        Per-band (R0 0-71 / R1 72-143 / R2 144-223) change fraction
        per sampled pair + frozen-pair rate. Accepts ref_ or any
        prefix; globs *NNNNNN.png with zero-padded frame numbers.
    oracle_grade.py scroll <dir> <frame> <rows0> <rows1> [maxk]
        BG scroll velocity via x-correlation of a row band between
        <frame> and +1,+2,+4,+8.
    oracle_grade.py match <dirA> <frameA> <dirB> <first> <last> [step]
        Content-anchor search: which frame of B best matches A's
        frame (timeline offsets between builds — the identity law —
        make index-vs-index diffs meaningless; anchor first).

MEASURED ARCADE TRUTHS (2026-08-31, ref_arcade level-1 corpus):
  walk scroll     0.5 px/frame (1px every 2nd frame: the arcade's own
                  walking cadence is ~30Hz-stepped; frozen pairs
                  R0/R1/R2 = 22/6/24% even on the arcade)
  smoke entrance  slow scene: 2%/frame change, 27-29% frozen pairs
  boss fight      the fast scene: 0/2/6% frozen pairs — full 60Hz.
                  THIS is where our 30Hz gap is visible; walking is
                  already at arcade cadence.
"""
import sys, os
import numpy as np
from PIL import Image


def load(d, n):
    for name in (f"ref_{n:06d}.png", f"frame_{n:06d}.png",
                 f"b_{n}.png", f"{n}.png"):
        p = os.path.join(d, name)
        if os.path.exists(p):
            return np.asarray(Image.open(p).convert("L"), dtype=np.int16)
    # fall back: any file ending in the zero-padded number
    for f in os.listdir(d):
        if f.endswith(f"{n:06d}.png") or f.endswith(f"_{n}.png"):
            return np.asarray(Image.open(os.path.join(d, f))
                              .convert("L"), dtype=np.int16)
    raise SystemExit(f"frame {n} not found in {d}")


def scale(im):
    """ares screenshots may be upscaled; normalise to 224-row units."""
    return im.shape[0] // 224


def cadence(d, a, b, step):
    prev = None
    bands = {"R0": [], "R1": [], "R2": []}
    for n in range(a, b + 1, step):
        im = load(d, n)
        s = scale(im)
        if prev is not None:
            df = np.abs(im - prev) > 8
            bands["R0"].append(df[0:72 * s].mean())
            bands["R1"].append(df[72 * s:144 * s].mean())
            bands["R2"].append(df[144 * s:224 * s].mean())
        prev = im
    for k, v in bands.items():
        fz = 100 * np.mean([x < 0.001 for x in v])
        print(f"{k}: median change {np.median(v)*100:.2f}%/pair  "
              f"frozen pairs {fz:.0f}%")


def scroll(d, n, r0, r1, maxk):
    base = load(d, n).astype(np.float32)[r0:r1]
    for k in (1, 2, 4, 8):
        im = load(d, n + k).astype(np.float32)[r0:r1]
        best = (1e18, 0)
        for dx in range(-maxk, maxk + 1):
            v = np.abs(np.roll(im, dx, axis=1)[:, maxk:-maxk]
                       - base[:, maxk:-maxk]).mean()
            if v < best[0]:
                best = (v, dx)
        print(f"+{k} frames: dx={best[1]}")


def match(da, fa, db, b0, b1, step):
    a = load(da, fa).astype(np.float32)
    best = (1e18, None)
    for n in range(b0, b1 + 1, step):
        try:
            im = load(db, n).astype(np.float32)
        except SystemExit:
            continue
        if im.shape != a.shape:
            continue
        v = np.abs(im - a).mean()
        if v < best[0]:
            best = (v, n)
    print(f"{da}@{fa} ~= {db}@{best[1]}  (mean|d| {best[0]:.1f}, "
          f"offset {best[1]-fa:+d})")


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "cadence":
        cadence(sys.argv[2], int(sys.argv[3]), int(sys.argv[4]),
                int(sys.argv[5]) if len(sys.argv) > 5 else 1)
    elif cmd == "scroll":
        scroll(sys.argv[2], int(sys.argv[3]), int(sys.argv[4]),
               int(sys.argv[5]), int(sys.argv[6]) if len(sys.argv) > 6 else 8)
    elif cmd == "match":
        match(sys.argv[2], int(sys.argv[3]), sys.argv[4],
              int(sys.argv[5]), int(sys.argv[6]),
              int(sys.argv[7]) if len(sys.argv) > 7 else 4)
    else:
        raise SystemExit(__doc__)
