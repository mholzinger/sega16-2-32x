#!/usr/bin/env python3
"""shot_identity.py: pixel identity of two roms on headless ares, at the colour level.

Runs each rom once with --screenshot at every sample frame (and no --input: the attract, or
a PORTTAPEPAD rom that carries its own tape), then compares the PNGs frame by frame and
classifies every differing frame by WHERE the pixels differ: inside the text layer's rows
only, or anywhere else. Built for R60TXTPOOL15 (the text pool moves from 31 CRAM entries
to 16, so the FB BYTES differ by design and only the presented colours can be gated).

  python3 tools/shot_identity.py A.32x B.32x --start 300 --end 12000 --step 100 --out dir
      [--counter sdram:0x4668:8]   dump a counter block from each run and print it as u32s

Prints: frames compared, frames identical, frames differing, and for each differing frame
the pixel count and bounding box. Exit 0 when every frame is identical.
"""
import argparse, os, subprocess, struct, sys
import numpy as np
from PIL import Image

ARES = os.environ.get("ARES", os.path.expanduser("~/src/ares-debug/build_macos/headless-ui/Release/ares-headless"))


def run(rom, frames, out, counter):
    os.makedirs(out, exist_ok=True)
    cmd = [ARES, "--frames", str(frames[-1] + 1)]
    for f in frames:
        cmd += ["--screenshot", f"{f}:{out}/f{f:06d}.png"]
    if counter:
        cmd += ["--dump", f"{counter}:{out}/counter.bin"]
    cmd.append(rom)
    with open(f"{out}/ares.log", "w") as log:
        subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, check=False)
    missing = [f for f in frames if not os.path.exists(f"{out}/f{f:06d}.png")]
    if missing:
        print(f"{rom}: {len(missing)} screenshots missing (first {missing[0]}); see {out}/ares.log")
    return missing


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a"); ap.add_argument("b")
    ap.add_argument("--start", type=int, default=300); ap.add_argument("--end", type=int, default=12000)
    ap.add_argument("--step", type=int, default=100)
    ap.add_argument("--out", required=True)
    ap.add_argument("--counter", default=None, help="ares --dump selector without the path, e.g. sdram:0x4668:8")
    ap.add_argument("--reuse", action="store_true")
    a = ap.parse_args()
    frames = list(range(a.start, a.end + 1, a.step))
    outs = [os.path.join(a.out, os.path.basename(r).replace(".32x", "")) for r in (a.a, a.b)]
    for rom, out in zip((a.a, a.b), outs):
        if a.reuse and all(os.path.exists(f"{out}/f{f:06d}.png") for f in frames):
            continue
        run(rom, frames, out, a.counter)
    same = diff = 0
    rows = []
    for f in frames:
        pa, pb = (f"{o}/f{f:06d}.png" for o in outs)
        if not (os.path.exists(pa) and os.path.exists(pb)):
            rows.append((f, -1, None)); continue
        ia = np.asarray(Image.open(pa).convert("RGB"), dtype=np.int16)
        ib = np.asarray(Image.open(pb).convert("RGB"), dtype=np.int16)
        if ia.shape != ib.shape:
            rows.append((f, -2, None)); diff += 1; continue
        m = np.abs(ia - ib).sum(axis=2) > 0
        n = int(m.sum())
        if n == 0:
            same += 1; continue
        diff += 1
        ys, xs = np.nonzero(m)
        rows.append((f, n, (int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max()))))
    print(f"{os.path.basename(a.a)} vs {os.path.basename(a.b)}: {len(frames)} frames {a.start}..{a.end} step {a.step}: "
          f"{same} identical, {diff} differ")
    for f, n, box in rows:
        if n == -1: print(f"  f{f}: screenshot missing")
        elif n == -2: print(f"  f{f}: size differs")
        else: print(f"  f{f}: {n} px differ, box x{box[0]}-{box[2]} y{box[1]}-{box[3]}")
    if a.counter:
        for rom, out in zip((a.a, a.b), outs):
            p = f"{out}/counter.bin"
            if os.path.exists(p):
                d = open(p, "rb").read()
                print(f"  {os.path.basename(rom)} counter: {struct.unpack('>%dI' % (len(d) // 4), d[:len(d) & ~3])}")
    sys.exit(0 if diff == 0 else 1)


if __name__ == "__main__":
    main()
