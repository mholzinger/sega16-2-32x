#!/usr/bin/env python3
"""Read the multi-frame L2 gate: does the SH-2 port SUSTAIN the game?

    python3 tools/portl2m_read.py rom/n256.32x 3000

Two curves, from port_l2m_run() in sh_src/port_l2.c:

  FREE-RUN   seed the arcade's capture frame ONCE, then run N frames without ever
             re-seeding, comparing frame k against the arcade's frame k. The port's
             own output is the next frame's input, so error COMPOUNDS. This is the
             "is it running the game" curve.
  PER-FRAME  re-seed from the arcade's frame k-1 each time. Each frame independent,
             so this says whether the one-frame number generalises.

HOW LONG IS THE RUN, AND WHY. It was 30 frames and 30 was never derived -- it is
the repo's house unit for a sampling window and, worse, the CADENCE BARRIER this
port exists to break. The length is now bounded by two measurements: the cart holds
574 frames of delta records, and l2m_score's merge is O(records x k) so the
per-frame curve is O(k^2) -- 512 frames would not finish before a reasonable dump
frame. The scorer binds, so the dense run is 256 frames = 4.3 s at 59.92 Hz.
Endurance is a separate longer run scored host-side (tools/portl2_endure.py).

THE CONTROL IS THE CALIBRATION. A port that does nothing scores the arcade's own
change count, computed here from the delta blob. 40 wrong looks small until you
know that doing nothing scores 40.

NOT WINNABLE: 0xFFFF00+ differs by 30 bytes between two MAME runs of the SAME
playback (measured 2026-09-30), so the region split matters more than the total.
"""
import os, struct, subprocess, sys, tempfile

ARES = os.environ.get("ARES", "/Users/mikeholzinger/src/ares-debug/build_macos/headless-ui/Release/ares-headless")
TICK_US = 32.0 / 23.01
SEEDPATH = os.environ.get("L2_SEED", "docs/proof/l2m/wram_009000.bin")
BLOB = os.environ.get("L2_BLOB", "docs/proof/l2m/deltas.bin")

def sym(lst, name):
    for line in open(lst):
        f = line.split()
        if len(f) == 3 and f[2] == name:
            return int(f[0], 16)
    raise SystemExit("%s: %s not found -- build with PORTFULL=1" % (lst, name))

rom = os.path.abspath(sys.argv[1])
frames = [int(x) for x in sys.argv[2:]] or [3000]
import romsyms
lst = romsyms.lst_for(rom)
addr = sym(lst, "_port_l2m_out")
print("_port_l2m_out = 0x%08X" % addr)

# the DO-NOTHING control, from the blob: per frame it is the arcade's own change
# count; free-running from a frozen seed it is how many cells differ from the seed
SEED = open(SEEDPATH, "rb").read()
ctl_per, ctl_free = [], []
B = open(BLOB, "rb").read()
nb = struct.unpack(">I", B[:4])[0]
acc = {}
for k in range(1, nb + 1):
    o = struct.unpack(">I", B[4 + 4*(k-1):8 + 4*(k-1)])[0]
    cnt = struct.unpack(">I", B[o:o+4])[0]
    ctl_per.append(cnt)
    for j in range(cnt):
        off, val = struct.unpack(">HH", B[o+4+4*j:o+8+4*j])
        acc[off] = val & 0xFF
    ctl_free.append(sum(1 for off, v in acc.items() if v != SEED[off]))

for n in frames:
    with tempfile.TemporaryDirectory() as td:
        d = os.path.join(td, "d.bin")
        subprocess.run([ARES, "--frames", str(n),
                        "--dump", "sdram:0x%X:0x400:%s" % (addr - 0x06000000, d), rom],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        D = open(d, "rb").read()
    g = lambda i: struct.unpack(">I", D[i*4:i*4+4])[0]

    if g(0) != 0x464D4C32:
        print("frame %d: no 'FML2' magic (%08X) -- the chain has not run yet" % (n, g(0)))
        continue
    if g(166):
        print("frame %d: REFUSED -- a delta frame has %d records and the byte cursor "
              "caps at 255. Re-pack with fewer frames or widen the cursor." % (n, g(166)))
        continue
    navail, fa, fb, step = g(1), g(2), g(3), g(7)
    print("frame %d: %d delta frames; free-run completed %d, per-frame completed %d"
          % (n, navail, fa, fb))
    print("  canaries %s" % ("INTACT" if not g(6) else "*** OVERWRITTEN (%d) -- numbers suspect ***" % g(6)))
    # summed PER-FRAME now, so the 16-bit FRT's ~91 ms wrap no longer applies
    print("  cost: free-run %.1f ms total (%.2f ms/frame), per-frame %.1f ms total"
          % (g(4)*TICK_US/1000.0, g(4)*TICK_US/1000.0/max(fa, 1), g(5)*TICK_US/1000.0))

    def curve(base, s_base, cnt, ctl, label):
        """NO VERDICT ON AN UNFINISHED RUN.

        The summary slots (min/max/sum/last/missed/spurious) are written AFTER the
        loop, so on a partial run they read 0 -- and a mean of 0 against a control
        of 156 printed "NET CORRECT" for a curve that had not finished and was in
        fact diverging. Measured 2026-09-30, and it is the same failure as a frame
        scoring better than the control because the game logic had stopped running.
        A partial run now shows its SHAPE and refuses to summarise."""
        if not cnt:
            print("  %s: NOT STARTED" % label)
            return
        done = (cnt >= navail)
        pts = [g(base + j) for j in range(min(32, (cnt + step - 1)//step))]
        print("  %s -- %d of %d frames%s" % (label, cnt, navail,
              "" if done else "  *** STILL RUNNING: no summary, read a later frame ***"))
        # AGAINST THE CONTROL AT THE SAME FRAME, not against its mean. The control
        # is NON-MONOTONIC -- the arcade's state wanders back toward the seed -- so a
        # mean-vs-mean comparison hides a port whose error only ever grows.
        print("    frame :  port  control   net")
        for j, v in enumerate(pts):
            f = j*step + 1
            if f - 1 >= len(ctl):
                break
            print("    f%-5d %5d %7d  %+5d" % (f, v, ctl[f-1], v - ctl[f-1]))
        if not done:
            return
        mn, mx, sm, last = g(s_base), g(s_base+1), g(s_base+2), g(s_base+3)
        miss, spur = g(s_base+4), g(s_base+5)
        c = ctl[:cnt]
        cmean = sum(c)/float(len(c))
        print("    min %d  max %d  mean %.1f  last %d  |  control mean %.1f, control last %d"
              % (mn, mx, sm/float(cnt), last, cmean, c[-1]))
        # THE TWO CURVES NEED DIFFERENT WORDS. Free-run can accumulate, so a rising
        # net IS divergence. Per-frame re-seeds from the arcade every frame, so it
        # CANNOT accumulate -- a rising net there means later game states are harder
        # for the port, which is a different defect and a different fix.
        freerun = (base == 8)
        if last <= c[-1]:
            verdict = "NET CORRECT at the last frame"
        elif freerun:
            verdict = "DIVERGING: the error accumulates and does not come back"
        else:
            verdict = ("the per-frame deficit GROWS WITH GAME STATE (it cannot "
                       "accumulate here -- every frame re-seeds from the arcade)")
        print("    net %+.1f B/frame on the mean, %+d at the LAST frame  ->  %s"
              % (sm/float(cnt) - cmean, last - c[-1], verdict))
        print("    of the wrong bytes: MISSED %.1f/frame (a cell the arcade CHANGED) + "
              "%.1f/frame whose end value it did NOT change"
              % (miss/float(cnt), spur/float(cnt)))

    curve(8,  72, fa, ctl_free, "FREE-RUN  (error compounds)")
    curve(40, 78, fb, ctl_per,  "PER-FRAME (independent)")
    print("  free-run last frame: first wrong %s; by region objects %d / 0xFFE000 %d / "
          "globals %d / 0xFFFF00+ %d"
          % (("0x%06X" % g(88)) if g(88) != 0xFFFFFFFF else "none", g(84), g(85), g(86), g(87)))
    if g(187):
        n2 = g(187)
        print("  PER-SLOT ATTRIBUTION of the sustained error -- the per-frame curve "
              "dispatching ONE slot, over %d frames sampled every 8th:" % n2)
        full = g(174) / float(n2)
        print("    full frame over those frames: %.1f wrong/frame" % full)
        print("    slot SKIPPED | wrong/frame   delta vs full   reading")
        rows = []
        for si, sl in enumerate((0, 16, 17, 40)):
            sm = g(171+si*4) / float(n2)
            rows.append((sl, sm, sm - full))
        for sl, sm, dl in sorted(rows, key=lambda r: r[2]):
            read = ("removing it HELPS -- this slot is doing the damage" if dl < -0.5 else
                    "removing it HURTS -- this slot is doing useful work" if dl > 0.5 else
                    "no effect either way")
            print("    %12d | %10.1f %+14.1f   %s" % (sl, sm, dl, read))

    nsp, nmi = g(164), g(165)
    if nsp:
        print("  UNCHANGED-BUT-WRONG cells (%d%s): %s" % (nsp, "+" if nsp >= 32 else "",
              " ".join("%06X" % g(100+k) for k in range(min(nsp, 32)))))
    if nmi:
        print("  MISSED cells (%d%s): %s" % (nmi, "+" if nmi >= 32 else "",
              " ".join("%06X" % g(132+k) for k in range(min(nmi, 32)))))
