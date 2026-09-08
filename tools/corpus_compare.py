#!/usr/bin/env python3
"""Scene-anchored corpus comparison: arcade capture vs ares capture.

Usage: corpus_compare.py <oracle_dir> <ours_dir> [--step N] [--out report.txt]

Both dirs hold frame_%06d.png window captures (any resolution; black
borders are cropped, content rescaled to 320x224). Input streams and
timing differ between corpora, so frames are matched by CONTENT: for
each oracle anchor (every --step frames), the best-matching frame in
ours is found by downsampled L1 distance, searched globally but warm-
started near the previous match (playthroughs are monotonic).

Output: per-anchor match percentage + the worst anchors. This measures
"does our output look like the arcade at every moment of the run" —
it does NOT measure cadence; use the transport counters for that.
"""
import sys, os, glob
from PIL import Image

def load_norm(path, size=(80, 56)):
    im = Image.open(path).convert("RGB")
    # crop black borders: scan rows/cols for content
    g = im.resize((160, 112))
    px = g.load()
    W, H = g.size
    def rowsum(y): return sum(sum(px[x, y]) for x in range(0, W, 4))
    def colsum(x): return sum(sum(px[x, y]) for y in range(0, H, 4))
    top = next((y for y in range(H) if rowsum(y) > 1500), 0)
    bot = next((y for y in range(H-1, -1, -1) if rowsum(y) > 1500), H-1)
    left = next((x for x in range(W) if colsum(x) > 1000), 0)
    right = next((x for x in range(W-1, -1, -1) if colsum(x) > 1000), W-1)
    fx, fy = im.width / W, im.height / H
    box = (int(left*fx), int(top*fy), int((right+1)*fx), int((bot+1)*fy))
    return im.crop(box).resize(size)

def dist(a, b):
    pa, pb = a.tobytes(), b.tobytes()
    return sum(abs(pa[i]-pb[i]) for i in range(0, len(pa), 7))

def main():
    oracle_dir, ours_dir = sys.argv[1], sys.argv[2]
    step = 10
    if "--step" in sys.argv:
        step = int(sys.argv[sys.argv.index("--step")+1])
    ofr = sorted(glob.glob(os.path.join(oracle_dir, "frame_*.png")))[::step]
    afr = sorted(glob.glob(os.path.join(ours_dir, "frame_*.png")))
    if not ofr or not afr:
        print("empty corpus"); return
    ours = [load_norm(p) for p in afr]
    results = []
    hint = 0
    for op in ofr:
        o = load_norm(op)
        # warm window around hint, then global if poor
        best, bi = None, 0
        lo, hi = max(0, hint-40), min(len(ours), hint+80)
        for i in range(lo, hi):
            d = dist(o, ours[i])
            if best is None or d < best: best, bi = d, i
        # global rescue when the local match is bad
        if best > 900000:
            for i in range(0, len(ours), 3):
                d = dist(o, ours[i])
                if d < best: best, bi = d, i
        hint = bi
        # normalize: max possible ~ 255*3*n_samples
        n = (80*56*3)//7
        match = 100.0 * (1 - best/(255.0*n))
        results.append((os.path.basename(op), os.path.basename(afr[bi]), match))
    results_sorted = sorted(results, key=lambda r: r[2])
    mean = sum(r[2] for r in results)/len(results)
    print(f"anchors={len(results)} mean match={mean:.1f}% "
          f"min={results_sorted[0][2]:.1f}%")
    print("worst 10 anchors:")
    for o, a, m in results_sorted[:10]:
        print(f"  {o} <-> {a}  {m:.1f}%")

if __name__ == "__main__":
    main()
