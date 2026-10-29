#!/usr/bin/env python3
"""temporal_metric.py -- what the stills cannot see: change between CONSECUTIVE
presented frames.

    tools/temporal_metric.py rom.32x --start 600 --n 60 [--out DIR]

One ares-headless run takes N consecutive screenshots from frame START. For each
consecutive pair the ACTIVE-AREA pixels that changed are counted. Three figures:

  presented   frames whose picture differs from the previous one (a repeat = no flip
              reached the screen that vint); the gaps between presented frames give
              the cadence pattern and its evenness (coefficient of variation of the
              inter-flip interval; 0 = perfectly even, 1 flip a vint or 1 every two)
  changed     mean pixels changed per presented frame (motion + fill + flicker)
  flicker     pixels that REVERT within two frames (frame i == frame i+2 and
              frame i != frame i+1): a cell or sprite that blinks, a tile that is
              drawn and undrawn, a palette that lands late and is corrected

2026-10-16: written because two eye verdicts in two days (sw109 rejected for uneven
motion after the largest parity gain on record; sw108 promoted for "less tile flicker"
with parity and census equal) turned on properties every tool in tools/ is blind to:
attract_parity is one frame against one arcade frame, the soak is a 30k mean, the
censuses are counts at a frame, R60_PH is time inside one pass. This one reads the
difference between consecutive presented frames, which is what the eye judges.
"""
import argparse, os, subprocess, sys
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import active_area as AA

ARES = os.path.expanduser("~/src/ares-debug/build_macos/headless-ui/Release/ares-headless")

def load(path):
    im = AA.crop_active(Image.open(path), (320, 224), Image.NEAREST).convert("RGB")
    return np.asarray(im, dtype=np.int16)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom"); ap.add_argument("--start", type=int, required=True)
    ap.add_argument("--n", type=int, default=60); ap.add_argument("--out", default=None)
    ap.add_argument("--quiet", action="store_true"); ap.add_argument("--reuse", action="store_true", help="use screenshots already in --out")
    ap.add_argument("--arcade", default=None, help="read ref_NNNNNN.png from this corpus dir instead of running ares (320x224, no crop)")
    ap.add_argument("--heatmap", default=None, help="write a PNG of where the reverting pixels are")
    a = ap.parse_args()
    out = a.out or os.path.join(os.environ.get("TMPDIR", "/tmp"), f"tm_{os.path.basename(a.rom)}_{a.start}")
    os.makedirs(out, exist_ok=True)
    cmd = [ARES, "--frames", str(a.start + a.n)]
    for f in range(a.start, a.start + a.n):
        cmd += ["--screenshot", f"{f}:{out}/f{f:06d}.png"]
    cmd.append(a.rom)
    if a.arcade:
        frames = [np.asarray(Image.open(f"{a.arcade}/ref_{f:06d}.png").convert("RGB"), dtype=np.int16) for f in range(a.start, a.start + a.n)]
    else:
        if not (a.reuse and all(os.path.exists(f"{out}/f{f:06d}.png") for f in range(a.start, a.start + a.n))):
            subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
        frames = [load(f"{out}/f{f:06d}.png") for f in range(a.start, a.start + a.n)]
    # 1. the presented sequence: collapse runs of identical captures (a repeat = no flip
    #    reached the screen that vint). The builder's point: flicker measured on raw
    #    captures scores a slower build as less flickery for its cadence alone.
    changed_raw = [int((np.abs(frames[i] - frames[i + 1]).sum(axis=2) > 0).sum()) for i in range(len(frames) - 1)]
    keep = [0] + [i + 1 for i, c in enumerate(changed_raw) if c > 0]
    pres = [frames[i] for i in keep]
    gaps = np.diff(keep) if len(keep) > 1 else np.array([])
    cv = float(gaps.std() / gaps.mean()) if len(gaps) and gaps.mean() > 0 else 0.0
    # 2. per presented transition: changed pixels, and FLICKER = pixels that revert
    #    within two PRESENTED frames
    changed, flick = [], []
    heat = np.zeros(pres[0].shape[:2], dtype=np.int32)
    for i in range(len(pres) - 1):
        d = (np.abs(pres[i] - pres[i + 1]).sum(axis=2) > 0)
        changed.append(int(d.sum()))
        if i + 2 < len(pres):
            back = (np.abs(pres[i] - pres[i + 2]).sum(axis=2) == 0) & d
            flick.append(int(back.sum())); heat += back
        else:
            flick.append(0)
    if a.heatmap:
        hm = np.clip(heat * (255 // max(1, int(heat.max()))), 0, 255).astype(np.uint8)
        Image.fromarray(hm).resize((640, 448), Image.NEAREST).save(a.heatmap)
    def dist(v):
        if not v: return "n/a"
        q = np.percentile(v, [50, 90, 100]); return f"p50 {q[0]:.0f} p90 {q[1]:.0f} max {q[2]:.0f}"
    if not a.quiet:
        print("presented-frame  changed  flicker(reverts by +2)")
        for j, (c, k) in enumerate(zip(changed, flick)):
            print(f"{a.start + keep[j + 1]:6d} {c:8d} {k:8d}")
    print(f"{os.path.basename(a.rom)} {a.start}-{a.start + a.n}: presented {len(keep) - 1}/{a.n - 1} "
          f"gap mean {gaps.mean() if len(gaps) else 0:.2f} evenness(cv) {cv:.2f} | "
          f"changed/transition {dist(changed)} | flicker/transition {dist(flick)} total {sum(flick)}")

if __name__ == "__main__":
    main()
