#!/usr/bin/env python3
"""ENDURANCE: free-run the ported game logic N frames and compare the endpoint.

    make line NOSPRBAKE=1 PORTFULL=1 PORTENDURE=900 RIGBARCODE=1 WRAMCODE=1
    python3 tools/portl2_endure.py rom/end900.32x 9900

The rom seeds work RAM from the arcade's capture frame and runs N frame bodies with
no re-seeding and no scoring, leaving work RAM AT THE ENDPOINT. This script dumps
those 16,384 bytes and diffs them against the arcade's own capture at frame 9000+N.

WHY IT IS SEPARATE FROM THE CURVES. The in-rom gate needs a delta record per scored
frame, which bounds it at 574 frames of cart space, and its scorer is O(k^2). Here
the comparison is host-side at a single endpoint, so it costs no cart space and N is
free -- 900 frames is 15 s, which is the run length this repo uses elsewhere.

It also prints EVERY wrong cell, where the in-rom collector holds only the first 32.
"""
import os, struct, subprocess, sys, tempfile, collections

ARES = os.environ.get("ARES", "/Users/mikeholzinger/src/ares-debug/build_macos/headless-ui/Release/ares-headless")
TICK_US = 32.0 / 23.01
WRAM_SH2 = 0x0602CC00          # layout.ld: the port's work-RAM base
WRAM_BYTES = 16384

rom = os.path.abspath(sys.argv[1])
want_frame = int(sys.argv[2]) if len(sys.argv) > 2 else 9900
ares_frames = int(sys.argv[3]) if len(sys.argv) > 3 else 40000
import romsyms
lst = romsyms.lst_for(rom)

def sym(name):
    for l in open(lst):
        f = l.split()
        if len(f) == 3 and f[2] == name:
            return int(f[0], 16)
    raise SystemExit("%s not found -- build with PORTFULL=1" % name)

cap = "docs/proof/l2m_endpoint_%06d.bin" % want_frame
if not os.path.exists(cap):
    cap = "docs/proof/l2m/wram_%06d.bin" % want_frame
if not os.path.exists(cap):
    raise SystemExit("no arcade capture for frame %d (looked for %s)" % (want_frame, cap))
AR = open(cap, "rb").read()
SEED = open("docs/proof/l2m/wram_009000.bin", "rb").read()

out = sym("_port_l2m_out")
with tempfile.TemporaryDirectory() as td:
    d1, d2 = os.path.join(td, "o.bin"), os.path.join(td, "w.bin")
    subprocess.run([ARES, "--frames", str(ares_frames),
                    "--dump", "sdram:0x%X:0x400:%s" % (out - 0x06000000, d1),
                    "--dump", "sdram:0x%X:0x%X:%s" % (WRAM_SH2 - 0x06000000, WRAM_BYTES, d2),
                    rom], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    D, W = open(d1, "rb").read(), open(d2, "rb").read()
g = lambda i: struct.unpack(">I", D[i*4:i*4+4])[0]

if g(188) != 0x454E4452:
    raise SystemExit("no 'ENDR' marker (%08X) -- build with PORTENDURE=N" % g(188))
want, done, ticks = g(189), g(190), g(191)
print("PORTENDURE: %d frames requested, %d completed%s"
      % (want, done, "" if done >= want else "   *** STILL RUNNING -- read a later ares frame ***"))
if done:
    print("  cost %.2f ms/frame on ares (%.1f%% of a vint), ~%.1f%% on hardware at 1.59x"
          % (ticks*TICK_US/1000.0/done, 100.0*ticks*TICK_US/done/(1000000.0/59.92),
             100.0*1.59*ticks*TICK_US/done/(1000000.0/59.92)))
if done < want:
    raise SystemExit(1)

REFUSED = 0xFFFFFFFF
if g(79) and want > g(79):
    print("  N=%d is BEYOND the %d-frame delta chain, so the in-rom scorer and its "
          "self-test are REFUSED by construction -- the host diff below is the only "
          "valid score here." % (want, g(79)))
if g(78) == 0x53454C46 and g(76) != REFUSED:
    print("  SCORER SELF-TEST (work RAM seeded to expected(N) by applying the deltas,"
          " then scored -- the only honest answer is 0): %d wrong%s"
          % (g(76), "" if not g(76) else "   *** l2m_score IS THE BUG, first at 0x%06X ***" % g(77)))
# the SAME state, scored two ways -- see the comment in the driver
if g(72) == REFUSED:
    print("  in-rom scorer: REFUSED (no expected image for N beyond the delta chain)")
else:
    print("  in-rom scorer on this very state: %d wrong (missed %d, unchanged-but-wrong %d)"
          % (g(72), g(73), g(74)))
wrong = [i for i in range(WRAM_BYTES) if W[i] != AR[i]]
ctl = [i for i in range(WRAM_BYTES) if SEED[i] != AR[i]]
# DID THE GAME ACTUALLY ADVANCE? READ THIS BEFORE THE CELL COUNT.
#
# 0xFFFFF02A is the game's own frame counter, incremented by FUN_000012EC at the
# loop head (0x97C). A port that is STUCK scores like the DO-NOTHING control and
# therefore looks GOOD -- measured 2026-10-02: a build whose 0xFFF028 was being
# zeroed by an over-wide declaration fell into the no-players path at 0xB80, sat in
# that path's wait, advanced ONE game frame in eight, and scored 55 wrong against a
# control of 54. Net +1 and completely dead. With the declaration fixed it advances
# 8 of 8 and scores 330. The second is the better port and the cell count says the
# opposite, so the cell count is not allowed to go first.
def gameframe(buf):
    o = 0xFFF02A - 0xFFC000
    return (buf[o] << 8) | buf[o + 1]
_adv = gameframe(W) - gameframe(SEED)
_want = want_frame - 9000
print("  PROGRESS: the game's own counter 0xFFF02A advanced %d of %d frames%s"
      % (_adv, _want,
         "" if _adv == _want else "   <== THE PORT IS NOT RUNNING THE GAME; the cell"
                                  " count below is meaningless until this is %d" % _want))
print("  endpoint vs the arcade at frame %d: %d of %d bytes wrong" % (want_frame, len(wrong), WRAM_BYTES))
print("  the DO-NOTHING control (the seed vs that frame): %d wrong" % len(ctl))
print("  -> net %+d  %s" % (len(wrong) - len(ctl),
      "NET CORRECT" if len(wrong) < len(ctl) else "the port is behind doing nothing"))
reg = collections.Counter()
for i in wrong:
    a = 0xFFC000 + i
    reg['objects 0xFFC000-DFFF' if a < 0xFFE000 else
        ('0xFFE000-EFFF' if a < 0xFFF000 else
         ('globals 0xFFF000-FEFF' if a < 0xFFFF00 else '0xFFFF00+ (not reproducible)'))] += 1
for k, v in reg.most_common():
    print("    %-32s %d" % (k, v))
# which object slots, since the object table dominates
sl = collections.Counter((i // 0x80) for i in wrong if i < 0x2000)
if sl:
    print("  by object slot: %s" % "  ".join("slot %d x%d" % (s, c) for s, c in sl.most_common(8)))
fl = collections.Counter((i % 0x80) for i in wrong if i < 0x2000)
if fl:
    print("  by object field: %s" % "  ".join("+0x%02X x%d" % (f, c) for f, c in fl.most_common(10)))
