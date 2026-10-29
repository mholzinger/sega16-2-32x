#!/usr/bin/env python3
"""PRESENTED FPS — how many DISTINCT pictures reach the screen per second.

    tools/presented_fps.py ROM [--windows 1800,2600,3400] [--input CSV]
    tools/presented_fps.py ROM --windows demoA:1800,title:3400 --no-input

THE SPEED GATE IS: flips per 100 vints, UPDATES PER 60 and LONGEST HOLD, all at matched
(step, tick) -- never at matched emu frame, because a faster rom is further into the scene at
the same frame and the window then covers different content (measured 2026-10-12: marks1 was
22 ticks ahead of clean1 at frame 700). MOTION is kept as the large-change column.

LONGEST HOLD is printed beside the rate (2026-10-11): the number of
CONSECUTIVE frames presenting the same picture.  A scene can read a
respectable average and still stall -- the title step reads single-digit
fps because one picture holds for tens of frames, and an average hides
that.  Hold and rate together are the gate shape for a speed card.

NOT md5-of-the-PNG.  Frame-identity was asked for and is the trap this
file exists to avoid: any single changed pixel counts as a new picture,
which is anim_rate.py's 0.05% rule in its most extreme form, and that
rule scored a CONFETTI build at 3x the shipping line (LOOP28 97).  The
thresholds below were measured; a hold here means the picture did not
change by more than 0.25% of the active area, not that it was bit-equal.

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
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import active_area as AA
import numpy as np
from PIL import Image
ARES = os.path.expanduser("~/src/ares-debug/build_macos/headless-ui/Release/ares-headless")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def window(rom, start, inp, n=60, thresh=0.25):
    td = tempfile.mkdtemp()
    cmd = [ARES, "--frames", str(start + n + 2)]
    if inp:
        cmd += ["--input", inp]
    for f in range(start, start + n):
        cmd += ["--screenshot", f"{f}:{td}/f{f}.png"]
    cmd.append(rom)
    subprocess.run(cmd, capture_output=True)
    prev, changes, worst_black = None, [], [0]
    for f in range(start, start + n):
        p = f"{td}/f{f}.png"
        if not os.path.exists(p):
            continue
        a = np.asarray(Image.open(p).convert("RGB")).astype(np.int16)
        if a.shape[1] >= 1345:
            # tools/active_area.py: rows 11..234, cols 65..1344. y=19 here was
            # EIGHT LINES LOW and put eight rows of border at the bottom of the
            # window -- the "screen rows 216-223 are black" that was filed as a
            # rom defect on 2026-10-11 and chased through three innocent sites.
            a = a[AA.Y0:AA.Y0 + AA.H, AA.X0:AA.X0 + AA.W]
        # BLACK ROWS: fully-black picture rows, counted INSIDE the active area.
        # The arcade has none; a band here is a real defect. Counted per frame and
        # reported as the worst frame of the window.
        blk = int((a.sum(axis=2).max(axis=1) == 0).sum())
        if blk > worst_black[0]:
            worst_black[0] = blk
        if prev is not None:
            changes.append(100.0 * (np.abs(a - prev).sum(axis=2) > 12).mean())
        prev = a
    return sum(1 for c in changes if c > thresh), changes, worst_black[0]


# THE SCENE'S NAME COMES FROM THE GAME, NOT FROM A TABLE OF OURS (2026-10-11).
# check_demo.py's SCENES letters are arcade frame RANGES; a port window labelled from them
# is labelled by where we GUESS the port is. The game says so itself: step 0xFFF031, scene
# key 0xFFF022, tick 0xFFF02A. In the port those live in SDRAM, not MD work RAM -- the 68K
# work RAM is relocated to _WRAM_BASE (sh_src/portfull/w68.h) -- so the offset is read from
# the build's own .lst rather than hardcoded, which is the trap nt_dump_ares fell into.
WRAM_68K_ORIGIN = 0xFFC000

def wram_base(rom):
    import romsyms   # the map proven to be this rom's build; the old fallback read other trees' maps
    for line in open(romsyms.lst_for(rom)):
        if line.rstrip().endswith(" A _WRAM_BASE"):
            return int(line.split()[0], 16) & 0xFFFFFFF
    sys.exit("presented_fps: no _WRAM_BASE in the rom's s16.lst -- cannot locate work RAM")

def game_scene(rom, frame, base):
    """(step, key, tick) as the GAME holds them at `frame`. One ares run: a --dump
    reflects the END state whatever frame is named, so it must be its own run."""
    off = base + (0xFFF020 - WRAM_68K_ORIGIN)
    td = tempfile.mkdtemp(); out = os.path.join(td, "w.bin")
    subprocess.run([ARES, rom, "--frames", str(frame),
                    "--dump", "sdram:0x%X:0x20:%s" % (off, out)], capture_output=True)
    if not os.path.exists(out):
        return (None, None, None)
    d = open(out, "rb").read()
    return (d[0x11], (d[2] << 8) | d[3], (d[0x0A] << 8) | d[0x0B])


def longest_hold(changes, thresh):
    """Most consecutive frames presenting the same picture (no change > thresh)."""
    best = run = 1
    for c in changes:
        if c > thresh:
            run = 1
        else:
            run += 1
            if run > best:
                best = run
    return best

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom"); ap.add_argument("--windows", default="1800,2600,3400")
    ap.add_argument("--input", default=None)
    ap.add_argument("--no-input", action="store_true",
                    help="run with no input tape -- the attract scenes take none")
    ap.add_argument("--game-labels", action="store_true",
                    help="label each window with the GAME's step/key/tick (one extra "
                         "ares run per window), not with a name of ours")
    a = ap.parse_args()
    inp = None if a.no_input else (a.input
                                   or os.path.join(ROOT, "discover/inputs/play_level1.csv"))
    # a window is FRAME or NAME:FRAME, so a scene can carry its name into the table
    wins = []
    for tok in a.windows.split(","):
        if not tok:
            continue
        name, _, fr = tok.rpartition(":")
        wins.append((name or f"f{fr}", int(fr)))
    base = wram_base(a.rom) if a.game_labels else None
    got = []; allnew = []; holds = []
    if a.game_labels:
        print("  window   step   key    tick   MOTION  updates/60  hold  black rows")
    for name, st in wins:
        n, ch, blk = window(a.rom, st, inp)
        big = sum(1 for c in ch if c > 5)
        hold = longest_hold(ch, 0.25)
        got.append(big); allnew.append(n); holds.append(hold)
        if a.game_labels:
            step, key, tick = game_scene(a.rom, st, base)
            lbl = ("  --    --     --  " if step is None
                   else "  %02X   %04X   %04X" % (step, key, tick))
            print(f"  f{st:<6}{lbl}    {big:2d}/60     {n:2d}/60      {hold:2d}     {blk:3d}")
        else:
            print(f"  {name:>10} f{st:<5} MOTION {big:2d}/60   any-change {n:2d}/60   "
                  f"longest hold {hold:2d}   black rows {blk:3d}")
    # UPDATES PER 60 IS THE HEADLINE SMOOTHNESS NUMBER, 2026-10-12. MOTION's 5% threshold
    # reads a TRIPLED flip rate as flat: when the picture updates three times as often each
    # update carries less accumulated change and fewer frames clear 5%. Measured on marks1
    # against clean1 at matched (step, tick) -- demo A flips 12 -> 45 per 100 and longest hold
    # 8 -> 3 while MOTION went 9 -> 8. So report all three and read updates/60 and hold for
    # smoothness; MOTION stays as the LARGE-change column, which is what it actually measures.
    print(f"{os.path.basename(a.rom)}: updates {sum(allnew)/len(allnew):.1f}/60   "
          f"MOTION {sum(got)/len(got):.1f}/60 (large change only)   "
          f"worst hold {max(holds)}   updates {allnew}   motion {got}   holds {holds}")

if __name__ == "__main__":
    main()
