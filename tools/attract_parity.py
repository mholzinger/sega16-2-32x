#!/usr/bin/env python3
"""ATTRACT PARITY SCORECARD (2026-09-06). Frame-for-frame grading of our
rom's attract against the arcade's no-coin cold boot, both headless:

  arcade: MAME altbeast, nvram-free cold boot, screen:snapshot() every
          frame (tools/census.lua) -> $ARC/ref_NNNNNN.png (320x224)
  ours:   ares-headless --screenshot at chosen frames (1415x243 with
          overscan; active area = crop 1280x224+65+19 -> 320x224)

ALIGNMENT: the game's own timeline, not power-on. The arcade program
blanks its display (port 0xC40001 bit 5, our mailbox 0xFFB001) at
arcade frame 443 for the title->demo cut; we bisect ares for the frame
where our mailbox drops to 0x80 after frame 150 and call the difference
OFFSET. Everything else is compared at arcade frame + OFFSET.

Per anchor scene (arcade first-whole frame S): mean |luma diff| between
ours at S+OFFSET+k and the arcade at S+k for k in a small ladder, so the
column where the diff collapses is our lag in frames. 0-3 = parity.

    python3 tools/attract_parity.py rom/s16.32x [--arc DIR] [--out DIR]
"""
import os, sys, subprocess, tempfile, struct, argparse
import numpy as np
from PIL import Image

ARES = os.environ.get("ARES", "/Users/mikeholzinger/src/ares-debug/build_macos/headless-ui/Release/ares-headless")
SCRATCH = "/private/tmp/claude-501/-Users-mikeholzinger-src-sega16-2-32x/a23722da-c1d5-4cf9-8360-6987d14558ab/scratchpad"
# (arcade first-whole frame, label)
SCENES = [(20, "boot card"), (205, "logo rewrite"), (294, "logo red"),
          (444, "cut black"), (465, "demo scene"), (1060, "face"),
          (1180, "eye"), (1360, "eye pan"), (1500, "demo 2")]
LADDER = [0, 1, 2, 3, 5, 8, 12, 20, 30, 45, 60, 90]

def io_misc_at(rom, n):
    with tempfile.TemporaryDirectory() as td:
        f = os.path.join(td, "w.bin")
        subprocess.run([ARES, "--frames", str(n), "--dump", f"wram:0xFFB000:0x10:{f}", rom],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        return open(f, "rb").read()[1]

def find_offset(rom):
    """first frame >= 150 where the mailbox reads display-off (0x80): a
    coarse 16-frame scan (the cut blank lasts 21 frames), then bisect."""
    prev = 150
    if io_misc_at(rom, prev) & 0x20 == 0:
        return None
    n = prev + 16
    while n <= 1400 and io_misc_at(rom, n) & 0x20:
        prev, n = n, n + 16
    if n > 1400:
        return None
    lo, hi = prev, n
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if io_misc_at(rom, mid) & 0x20: lo = mid
        else: hi = mid
    return hi - 443

def ours(rom, frames, outdir):
    args = [ARES, "--frames", str(max(frames) + 2)]
    for f in frames:
        args += ["--screenshot", f"{f}:{outdir}/o_{f:06d}.png"]
    args.append(rom)
    subprocess.run(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)

def load_ours(path):
    im = Image.open(path).crop((65, 19, 65 + 1280, 19 + 224)).resize((320, 224), Image.BILINEAR)
    return np.asarray(im.convert("L"), dtype=np.int16)

def load_arc(arc, n):
    return np.asarray(Image.open(f"{arc}/ref_{n:06d}.png").convert("L"), dtype=np.int16)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom"); ap.add_argument("--arc", default=SCRATCH + "/arc6k")
    ap.add_argument("--out", default=SCRATCH + "/parity"); ap.add_argument("--offset", type=int)
    a = ap.parse_args()
    rom = os.path.abspath(a.rom); os.makedirs(a.out, exist_ok=True)
    off = a.offset if a.offset is not None else find_offset(rom)
    if off is None:
        print("OFFSET: not found (mailbox never blanked between 150 and 1200)"); sys.exit(1)
    print(f"OFFSET = {off} (our frame = arcade frame + {off}; cut blank at ours {443+off})")
    frames = sorted({s + off + k for s, _ in SCENES for k in LADDER})
    ours(rom, frames, a.out)
    print("%-13s %5s | " % ("scene", "arc") + " ".join("%4d" % k for k in LADDER))
    for s, label in SCENES:
        row = []
        for k in LADDER:
            n = s + k
            try:
                d = np.abs(load_ours(f"{a.out}/o_{n+off:06d}.png") - load_arc(a.arc, n)).mean()
                row.append("%4.0f" % d)
            except FileNotFoundError:
                row.append("   -")
        print("%-13s %5d | " % (label, s) + " ".join(row))
    print("(mean |luma| diff, 0 = identical; the column where it collapses is our lag)")

if __name__ == "__main__":
    main()
