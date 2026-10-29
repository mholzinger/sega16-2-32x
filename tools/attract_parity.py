#!/usr/bin/env python3
"""ATTRACT PARITY SCORECARD (2026-09-06). Frame-for-frame grading of our
rom's attract against the arcade's no-coin cold boot, both headless:

  arcade: MAME altbeast, nvram-free cold boot, screen:snapshot() every
          frame (tools/census.lua) -> $ARC/ref_NNNNNN.png (320x224)
  ours:   ares-headless --screenshot at chosen frames (1415x243 with
          overscan; active area = tools/active_area.py, 1280x224+65+11)

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
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import active_area as AA
import numpy as np
from PIL import Image

ARES = os.environ.get("ARES", "/Users/mikeholzinger/src/ares-debug/build_macos/headless-ui/Release/ares-headless")
# NOT THE SCRATCHPAD. This pinned a DEAD SESSION's scratchpad until 2026-10-07, so --arc
# defaulted to a corpus directory that no longer existed and the one tool that grades the
# picture against the ARCADE could not run at all -- found only when a wrong tile turned up
# that the port's own baselines cannot see, because they compare the port against the port.
# The corpus is generated, large and Sega-derived, so it lives under build/ and is rebuilt
# on demand rather than committed.
SCRATCH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "build", "attract")
REGEN = ("mkdir -p %s && mame altbeast -rompath ./mame -skip_gameinfo -video none "
         "-sound none -nothrottle -window -resolution 160x120 -keyboardprovider none "
         "-nomouse -nojoystick -bench 130 -nvram_directory $(mktemp -d) "
         "-snapshot_directory %s -autoboot_script tools/arcade_refdump.lua")
# (arcade first-whole frame, label)
SCENES = [(20, "boot card"), (205, "logo rewrite"), (294, "logo red"),
          (444, "cut black"), (465, "demo scene"), (1060, "face"),
          (1180, "eye"), (1360, "eye pan"), (1500, "demo 2")]
LADDER = [0, 1, 2, 3, 5, 8, 12, 20, 30, 45, 60, 90]

PORT = False     # --port: the SH-2 port build. It never blanks its display at the cut
                 # (0xFFF018 bit 5 stayed set across frames 478-498 on rd12, 2026-10-04), so
                 # the anchor is the game's STATE byte 0xFFF031 reaching 0x0C -- the arcade
                 # does that at frame 448, 5 frames after its blank at 443 -- read from SDRAM
                 # work RAM (tools/a68.py 0xFFF031 -> sdram:0x2FC31:1).
def io_misc_at(rom, n):
    with tempfile.TemporaryDirectory() as td:
        f = os.path.join(td, "w.bin")
        if PORT:
            subprocess.run([ARES, "--frames", str(n), "--dump", f"sdram:0x2FC31:1:{f}", rom],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
            st = open(f, "rb").read()[0]
            return 0 if st == 0x0C else 0x20      # "display off" stands for "state 0C reached"
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
    return hi - (448 if PORT else 443)   # port: first frame in state 0C; arcade enters 0C at 448

def ours(rom, frames, outdir):
    args = [ARES, "--frames", str(max(frames) + 2)]
    for f in frames:
        args += ["--screenshot", f"{f}:{outdir}/o_{f:06d}.png"]
    args.append(rom)
    subprocess.run(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)

def load_ours(path):
    # tools/active_area.py -- the origin here was y=19, eight lines low, so every
    # parity row this tool ever produced compared a window shifted down by 8.
    im = AA.crop_active(Image.open(path), (320, 224), Image.BILINEAR)
    return np.asarray(im.convert("L"), dtype=np.int16)

def load_arc(arc, n):
    return np.asarray(Image.open(f"{arc}/ref_{n:06d}.png").convert("L"), dtype=np.int16)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom"); ap.add_argument("--arc", default=SCRATCH + "/arc6k")
    ap.add_argument("--out", default=SCRATCH + "/parity"); ap.add_argument("--offset", type=int)
    ap.add_argument("--port", action="store_true", help="SH-2 port build: read the display gate from SDRAM work RAM")
    a = ap.parse_args()
    global PORT; PORT = a.port
    rom = os.path.abspath(a.rom); os.makedirs(a.out, exist_ok=True)
    if not os.path.isdir(a.arc) or not os.listdir(a.arc):
        print('no arcade corpus at %s' % a.arc)
        print('regenerate it (RD_LAST=6000, about two minutes):')
        print('  ' + REGEN % (a.arc, a.arc))
        sys.exit(2)
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
