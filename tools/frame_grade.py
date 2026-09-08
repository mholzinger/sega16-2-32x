#!/usr/bin/env python3
"""Automatic defect grader for capture corpora (Mike's screenshots/).

    tools/frame_grade.py <dir> [--csv out.csv]

Detectors (each prints per-frame flags + a summary):
  STRIP  displaced horizontal strip: an 8-row band whose horizontal
         phase disagrees with BOTH neighbours while the neighbours
         agree with each other (parallax moves bands smoothly; the
         defect is a lone out-of-phase strip).
  SILH   large solid-black blob inside the playfield (the shadow-actor
         silhouette fallback). Legit black art exists (the wolf), so
         this flags size >= ~3% of the playfield.
  HUD    lives-icon region at top-left reads as flat sky (HUD dropout).

Calibrated against Mike's labelled frames 2026-08-26 (74/800 strips,
96/112/150 silhouettes); tune thresholds there before trusting shifts.
"""
import sys, os, glob, csv
from PIL import Image

def load(p, W=375, H=289):
    im = Image.open(p).convert("L")
    return im.resize((W, H))

def crop_active(px, W, H):
    # active area: skip letterbox (rows/cols mostly black)
    def rs(y): return sum(px[x, y] for x in range(0, W, 4))
    def cs(x): return sum(px[x, y] for y in range(0, H, 4))
    top = next((y for y in range(H) if rs(y) > 800), 0)
    bot = next((y for y in range(H-1, -1, -1) if rs(y) > 800), H-1)
    left = next((x for x in range(W) if cs(x) > 600), 0)
    right = next((x for x in range(W-1, -1, -1) if cs(x) > 600), W-1)
    return left, top, right, bot

def strip_shift(px, y0, y1, x0, x1, py0, py1):
    # best horizontal offset of strip [y0,y1) vs strip [py0,py1)
    best, bo = None, 0
    for off in range(-6, 7):
        d = 0; n = 0
        for y in range(y0, y1, 2):
            yy = py0 + (y - y0)
            if yy >= py1: break
            for x in range(x0+8, x1-8, 3):
                xx = x + off
                if x0 <= xx < x1:
                    d += abs(px[x, y] - px[xx, yy]); n += 1
        if n:
            d /= n
            if best is None or d < best: best, bo = d, off
    return bo

def grade(path):
    im = load(path)
    px = im.load(); W, H = im.size
    L, T, R, B = crop_active(px, W, H)
    flags = []
    # ---- SILH: large black blob in the playfield (rows 15%..85%)
    y0 = T + (B-T)*15//100; y1 = T + (B-T)*85//100
    blk = 0; tot = 0
    cols = {}
    for y in range(y0, y1, 2):
        for x in range(L, R, 2):
            tot += 1
            if px[x, y] < 22:
                blk += 1
                cols[x//8] = cols.get(x//8, 0) + 1
    # contiguity: black concentrated in a horizontal run of columns
    if tot and blk/tot > 0.03:
        run = 0; best = 0
        for cx in range(L//8, R//8+1):
            run = run + 1 if cols.get(cx, 0) >= 4 else 0
            best = max(best, run)
        if best >= 6:
            flags.append("SILH")
    # ---- STRIP: lone out-of-phase 8-row strip (active-line strips)
    sh = []  # per-strip shift vs previous strip
    strip_h = max(4, (B-T)//28)
    ys = list(range(T + strip_h*4, B - strip_h*6, strip_h))
    for y in ys:
        sh.append(strip_shift(px, y, y+strip_h, L, R, y-strip_h, y))
    for i in range(1, len(sh)-1):
        # strip i out of phase: enters shifted, exits shifted back
        if abs(sh[i]) >= 3 and abs(sh[i] + sh[i+1]) <= 1 and abs(sh[i-1]) <= 1:
            flags.append("STRIP@%d" % (ys[i]))
            break
    # ---- HUD: lives icon region flat (top-left ~x 8-14%, y 10-16%)
    hx0 = L + (R-L)*8//100;  hx1 = L + (R-L)*15//100
    hy0 = T + (B-T)*10//100; hy1 = T + (B-T)*17//100
    vals = [px[x, y] for y in range(hy0, hy1) for x in range(hx0, hx1, 2)]
    if vals:
        mean = sum(vals)/len(vals)
        var = sum((v-mean)**2 for v in vals)/len(vals)
        if var < 40:                      # flat = sky, icon gone
            flags.append("HUD")
    return flags

def main():
    d = sys.argv[1]
    files = sorted(glob.glob(os.path.join(d, "frame_*.png")))
    out = None
    if "--csv" in sys.argv:
        out = csv.writer(open(sys.argv[sys.argv.index("--csv")+1], "w"))
        out.writerow(["frame", "flags"])
    counts = {}
    bad = []
    for p in files:
        fl = grade(p)
        if fl:
            bad.append((os.path.basename(p), fl))
            for f in fl:
                counts[f.split("@")[0]] = counts.get(f.split("@")[0], 0) + 1
        if out:
            out.writerow([os.path.basename(p), " ".join(fl)])
    print("frames: %d   flagged: %d" % (len(files), len(bad)))
    for k, v in sorted(counts.items()):
        print("  %-6s %d frames" % (k, v))
    print("first 25 flagged:")
    for n, fl in bad[:25]:
        print("  %s  %s" % (n, " ".join(fl)))

if __name__ == "__main__":
    main()
