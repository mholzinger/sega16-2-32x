#!/usr/bin/env python3
"""Profile a raw ares capture (capture.sh raw -> screenshots/frame_*.png).

The capture is the visual-side oracle: ares renders at 60 fps, the port
updates the display slower, so consecutive identical frames measure the
REAL delivered cadence — the thing state_health.py's vints/cycle
approximates from counters, measured here from what the eye actually
got. Also flags stalls, scene cuts, and per-scene sky-colour stability
(the palette pack's fallback variance shows up as the sky changing
colour between or within scenes).

Usage: frame_profiler.py <dir> [--csv out.csv]
"""
import os
import sys
import hashlib
from collections import Counter

try:
    from PIL import Image
except ImportError:
    sys.exit("needs Pillow (pip install Pillow)")


def frame_key(path):
    """Cheap identity: hash of a 80x56 thumbnail's raw pixels."""
    im = Image.open(path).convert("RGB")
    th = im.resize((80, 56), Image.NEAREST)
    return hashlib.md5(th.tobytes()).digest(), im


def sky_colour(im):
    """Dominant colour of the top third (excluding letterbox border)."""
    w, h = im.size
    region = im.crop((int(w * 0.1), int(h * 0.08), int(w * 0.9), int(h * 0.33)))
    small = region.resize((40, 12), Image.NEAREST)
    return Counter(small.getdata()).most_common(1)[0][0]


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    d = sys.argv[1]
    files = sorted(f for f in os.listdir(d) if f.startswith("frame_") and f.endswith(".png"))
    if not files:
        sys.exit(f"no frame_*.png in {d}")

    runs = []            # (start_frame_no, length)
    skies = []           # (frame_no, colour) sampled at run starts
    cuts = []            # frame numbers where the image changed a lot
    prev_key, prev_im, run_start = None, None, 0
    n = len(files)
    for i, f in enumerate(files):
        key, im = frame_key(os.path.join(d, f))
        if key != prev_key:
            if prev_key is not None:
                runs.append((run_start, i - run_start))
            run_start = i
            skies.append((i, sky_colour(im)))
            if prev_im is not None:
                # scene cut: dominant colour of the whole frame jumps
                a = Counter(prev_im.resize((20, 14), Image.NEAREST).getdata()).most_common(1)[0][0]
                b = Counter(im.resize((20, 14), Image.NEAREST).getdata()).most_common(1)[0][0]
                if sum(abs(x - y) for x, y in zip(a, b)) > 180:
                    cuts.append(i)
            prev_im = im
        prev_key = key
    runs.append((run_start, n - run_start))

    lengths = [l for _, l in runs]
    hist = Counter(lengths)
    uniq = len(runs)
    print(f"{n} frames, {uniq} unique -> mean cadence {n/uniq:.2f} "
          f"capture-frames/update ({60*uniq/n:.1f} updates/sec at 60fps capture)")
    print("run-length histogram (3 = clean 20Hz):")
    for l in sorted(hist):
        bar = "#" * min(60, int(60 * hist[l] / max(hist.values())))
        print(f"  {l:3d}: {hist[l]:5d} {bar}")
    stalls = sorted((l, s) for s, l in runs if l >= 6)[-10:]
    if stalls:
        print("worst stalls (>=6 frames = 100ms+):")
        for l, s in reversed(stalls):
            print(f"  frame_{s+1:06d}: {l} frames ({l/60*1000:.0f} ms)")
    print(f"scene cuts: {len(cuts)} at " +
          ", ".join(f"frame_{c+1:06d}" for c in cuts[:12]))

    # sky stability between cuts
    print("sky colour per segment (dominant, at update boundaries):")
    seg_edges = [0] + cuts + [n]
    for a, b in zip(seg_edges, seg_edges[1:]):
        seg = [c for i, c in skies if a <= i < b]
        if len(seg) < 5:
            continue
        cc = Counter(seg)
        top, cnt = cc.most_common(1)[0]
        stability = 100 * cnt / len(seg)
        flag = "" if stability > 85 else "   <-- UNSTABLE"
        print(f"  frames {a+1:6d}-{b:6d}: {top} {stability:.0f}% stable, "
              f"{len(cc)} distinct{flag}")

    if "--csv" in sys.argv:
        out = sys.argv[sys.argv.index("--csv") + 1]
        with open(out, "w") as fh:
            fh.write("start_frame,run_length\n")
            for s, l in runs:
                fh.write(f"{s+1},{l}\n")
        print(f"per-update runs written to {out}")


if __name__ == "__main__":
    main()
