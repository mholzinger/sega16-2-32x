#!/usr/bin/env python3
"""ONE SEEDED FRAME, scored host-side with the COMPLETE wrong-cell list.

    make line NOSPRBAKE=1 PORTFULL=1 PORTSEED=64 RIGBARCODE=1 WRAMCODE=1
    python3 tools/portl2_seed.py rom/seed64.32x 64

The rom seeds work RAM to the arcade's frame 9000+N, runs ONE frame body, and stops.
This script diffs the result against the arcade's capture at 9000+N+1. No inherited
error -- a wrong cell here is a wrong value this frame, not accumulated drift -- and
unlike the in-rom collector (32 cells) the host sees all of them.

With `PORTSKIP=S` the rom dispatches every active slot except S. Pass two dumps to
this script to get a slot's exact contribution:

    python3 tools/portl2_seed.py rom/seed64.32x 64 --vs rom/seed64_no40.32x
"""
import romsyms
import os, struct, subprocess, sys, tempfile, collections

ARES = os.environ.get("ARES", "/Users/mikeholzinger/src/ares-debug/build_macos/headless-ui/Release/ares-headless")
WRAM_SH2, WRAM_BYTES = 0x0602CC00, 16384
TICK_US = 32.0 / 23.01

def sym(lst, name):
    for l in open(lst):
        f = l.split()
        if len(f) == 3 and f[2] == name:
            return int(f[0], 16)
    raise SystemExit("%s not found in %s" % (name, lst))

def run(rom, frames=8000):
    rom = os.path.abspath(rom)
    lst = romsyms.lst_for(rom)
    out = sym(lst, "_port_l2m_out")
    with tempfile.TemporaryDirectory() as td:
        d1, d2 = os.path.join(td, "o.bin"), os.path.join(td, "w.bin")
        subprocess.run([ARES, "--frames", str(frames),
                        "--dump", "sdram:0x%X:0x400:%s" % (out - 0x06000000, d1),
                        "--dump", "sdram:0x%X:0x%X:%s" % (WRAM_SH2 - 0x06000000, WRAM_BYTES, d2),
                        rom], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        return open(d1, "rb").read(), open(d2, "rb").read()

args = [a for a in sys.argv[1:] if a != "--vs"]
vs = "--vs" in sys.argv
rom = args[0]
N = int(args[1])
other = args[2] if vs and len(args) > 2 else None

cap = "docs/proof/l2m/wram_%06d.bin" % (9000 + N + 1)
if not os.path.exists(cap):
    raise SystemExit("no arcade capture for frame %d" % (9000 + N + 1))
AR = open(cap, "rb").read()

DUMPS = {}
_MAINLOOP = False

FRAME_BODY = [0x012EC,0x01366,0x00BD6,0x03AAE,0x039C8,0x03A00,0x0D80E,0x0E052,
              0x0398E,0x03716,0x00DFA,0x00ECE,0x04254,0x0DCB4]

def body_hits(rom, frames=8000):
    """which of the arcade's 15 frame-body calls RESOLVED (FULL-PORT.md 32).

    A call whose 68K address is not in the dispatch table is a counted MISS, not a
    jump -- so a missing root shows up here as a 0 instead of silently doing
    nothing."""
    rom = os.path.abspath(rom)
    lst = romsyms.lst_for(rom)
    try:
        a = sym(lst, "_port_body_hit")
    except SystemExit:
        return
    with tempfile.TemporaryDirectory() as td:
        d = os.path.join(td, "b.bin")
        subprocess.run([ARES, "--frames", str(frames),
                        "--dump", "sdram:0x%X:0x40:%s" % (a - 0x06000000, d), rom],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        D = open(d, "rb").read()
    hits = [struct.unpack(">I", D[i*4:i*4+4])[0] for i in range(len(FRAME_BODY))]
    done = [i for i, h in enumerate(hits) if h == 2]
    hung = [i for i, h in enumerate(hits) if h == 1]
    never = [i for i, h in enumerate(hits) if h == 0]
    print("  FRAME BODY: %d of %d completed" % (len(done), len(FRAME_BODY)))
    if hung:
        print("    *** HUNG in 0x%05X (entered, never returned) ***" % FRAME_BODY[hung[0]])
    if never:
        print("    never reached: %s" % " ".join("0x%05X" % FRAME_BODY[i] for i in never))

def wrongset(rom):
    D, W = run(rom)
    DUMPS[rom] = W
    g = lambda i: struct.unpack(">I", D[i*4:i*4+4])[0]
    if g(188) != 0x53454544:
        raise SystemExit("%s: no 'SEED' marker (%08X) -- build with PORTSEED=N" % (rom, g(188)))
    print("    register chain after the frame: D3=%08X  D6=%08X   "
          "(arcade: D3=0x12 D6=0xFA80);  write-backs %d" % (g(180), g(181), g(182)))
    print("    port_zoom_scale: ran %d times, last D3=0x%02X, last 68K A0=0x%06X;  chain A1=0x%06X (arcade 0x6F2C)"
          % (g(184), g(183), g(185), g(186)))
    # WHAT DROVE THE FRAME. 'MAIN' in [192] means the real main loop (0x005BE) ran
    # and this driver was its vsync; the frame count and yield count come straight
    # from port_frame_wait. Two yields is one frame: the prologue boundary and the
    # end of the iteration. Anything else means the loop took an inner wait path
    # and the "one frame" claim needs re-reading before any number is believed.
    global _MAINLOOP
    if g(192) == 0x4D41494E:
        _MAINLOOP = True
        print("    MAIN LOOP DROVE THE FRAME (0x005BE rooted): %d frame(s) completed, "
              "%d yield(s)%s" % (g(193), g(194),
                                 "" if g(194) == 2 else "  <== NOT 2: see tools/portl2_seed.py"))
    if g(195) == 0x4E4F4652:
        print("    free-run/endurance REFUSED in this build ('NOFR'): a longjmp cannot "
              "resume, so nothing accumulates across the frame boundary")
    skip = g(179)
    skip = skip - (1 << 32) if skip >= (1 << 31) else skip
    print("  %s: seeded at frame %d, skip slot %s, frame cost %.0f us"
          % (os.path.basename(rom), g(189), skip if skip >= 0 else "none", g(191) * TICK_US))
    return {i for i in range(WRAM_BYTES) if W[i] != AR[i]}

# THE FIFTEEN-CALL HIT LIST ONLY MEANS SOMETHING FOR THE HAND-ROLLED BODY. Under
# PORTMAIN the loop branches for itself, so an address it legitimately skipped would
# read as "never reached" and look like a defect. Report it only when the symbol
# exists AND the rom is not main-loop driven -- decided after the first read, below.
A = wrongset(rom)
_D0, _ = None, None
if DUMPS and not _MAINLOOP:
    body_hits(rom)
# THE GAME'S OWN PROGRESS MARKER, and it is the first thing to read.
#
# The program stores the 68K address it last passed into 0xFFFFFEFC/FEFE -- dead
# stores as far as the decompiler is concerned, and the single most useful cell in
# work RAM: it says WHERE IN THE PROGRAM each machine is. A byte count cannot
# distinguish "the port computed the frame wrongly" from "the port ran a different
# part of the program", and those need opposite fixes.
#
# Measured 2026-10-02 on the rooted main loop: arcade 0x0AE2 (inside the gameplay
# frame loop at 0x970-0xADC), port 0x0094. The port was running the state-init pass
# -- which legitimately clears text/tile/sprite RAM -- so 868 of its 871 wrong cells
# in 0xFFE000 were simply zeroed. That is not a wrong computation; it is the wrong
# position. FULL-PORT.md 36.
def mark(buf):
    o = 0xFFFEFE - 0xFFC000
    return (buf[o] << 8) | buf[o + 1]
_W = DUMPS[rom]
_SD = open("docs/proof/l2m/wram_%06d.bin" % (9000 + N), "rb").read()
# DID THE FRAME ADVANCE THE GAME? READ THIS BEFORE THE CELL COUNT.
#
# 0xFFFFF02A is the game's own frame counter, incremented by FUN_000012EC at the loop
# head (0x97C). Measured 2026-10-02 and it reorders everything below it:
#
#     arcade                       +1
#     the four-call frame body     +0   <- it never ran the loop head at all
#     the rotated real frame       +1
#
# The four-call body's net -43 was a score for something that is not a frame. A cell
# count cannot tell "ran the frame and got cells wrong" from "did not run the frame",
# and the second scores BETTER because the control is also nearly still.
def gframe(buf):
    o = 0xFFF02A - 0xFFC000
    return (buf[o] << 8) | buf[o + 1]
_pa, _aa = gframe(_W) - gframe(_SD), gframe(AR) - gframe(_SD)
print("  PROGRESS: the game's counter 0xFFF02A advanced %d, the arcade's %d%s"
      % (_pa, _aa, "" if _pa == _aa else
         "   <== THE FRAME DID NOT RUN; the cell count below is not comparable"))
print("  PROGRESS MARKER 0xFFFEFE (the 68K address each machine last passed):")
print("    port 0x%04X    arcade 0x%04X    seed 0x%04X%s"
      % (mark(_W), mark(AR), mark(_SD),
         "" if mark(_W) == mark(AR) else
         "   <== DIFFERENT PART OF THE PROGRAM: compare positions before cells"))
ctl = {i for i in range(WRAM_BYTES) if _SD[i] != AR[i]}
print("  wrong: %d of 16384   control (does nothing): %d   net %+d"
      % (len(A), len(ctl), len(A) - len(ctl)))
if not other:
    reg = collections.Counter()
    for i in sorted(A):
        a = 0xFFC000 + i
        reg['objects' if a < 0xFFE000 else ('0xFFE000' if a < 0xFFF000 else
            ('globals' if a < 0xFFFF00 else '0xFFFF00+'))] += 1
    print("  by region: %s" % "  ".join("%s %d" % kv for kv in reg.most_common()))
    sl = collections.Counter(i // 0x80 for i in A if i < 0x2000)
    if sl:
        print("  by slot: %s" % "  ".join("slot %d x%d" % kv for kv in sl.most_common(6)))
    for i in sorted(A)[:40]:
        pass
    # VALUES, NOT JUST ADDRESSES. A cell that is wrong by one is a different defect
    # from a cell holding garbage, and the address alone cannot tell them apart.
    # SEED is what the arcade had going in, so "port == seed" means we never wrote
    # it and "arcade == seed" means the arcade did not change it this frame.
    W = DUMPS[rom]
    SD = open("docs/proof/l2m/wram_%06d.bin" % (9000 + N), "rb").read()
    print("  cell     port  arcade  seed   reading")
    for i in sorted(A)[:28]:
        pv, av, sv = W[i], AR[i], SD[i]
        if pv == sv and av != sv:   why = "we never wrote it; the arcade did"
        elif av == sv and pv != sv: why = "we wrote it; the arcade did not"
        elif abs(pv - av) <= 2:     why = "wrong by %+d" % (pv - av)
        else:                        why = "different value"
        print("  %06X    %02X     %02X     %02X   %s" % (0xFFC000+i, pv, av, sv, why))
    # the sprite record table is 0xFFF800 + record*0x10, which is where
    # sprite_build_depth_banded writes; group the wrong cells that way
    recs = {}
    for i in sorted(A):
        a = 0xFFC000 + i
        if 0xFFF800 <= a < 0xFFFC84:
            recs.setdefault((a - 0xFFF800) // 0x10, []).append((a - 0xFFF800) % 0x10)
    if recs:
        print("  SPRITE RECORDS wrong (0xFFF800 + record*0x10):")
        for r, offs in sorted(recs.items()):
            print("    record %-3d fields %s" % (r, " ".join("+%X" % o for o in offs)))
else:
    B = wrongset(other)
    print("  wrong WITHOUT that slot: %d  ->  the slot costs %+d cells this frame"
          % (len(B), len(A) - len(B)))
    only_a = sorted(A - B)
    only_b = sorted(B - A)
    if only_a:
        print("  WRONG ONLY WITH THE SLOT (its damage): %s"
              % " ".join("%06X" % (0xFFC000+i) for i in only_a[:40]))
    if only_b:
        print("  wrong only WITHOUT it (work it does correctly): %s"
              % " ".join("%06X" % (0xFFC000+i) for i in only_b[:40]))
