#!/usr/bin/env python3
"""Classify the GENRING ring and correlate it with the BLIT_CHASE fence.

    python3 tools/gen_ring_classify.py build/port/rom/genring.32x 455 500 680 780

Reads only -- it uses the rom tools/gen_ring.py already built. Three things
tools/gen_ring.py does not do:

  1. WINDOWED PHH. PHH (0x39900) bins echo and mtask latency at 0.125-vint
     resolution but accumulates since boot, so its shape mixes the cheap early
     scenes with demo A. Subtracting two reads gives the window's own histogram.
  2. THE UNFLIPPED, CLASSIFIED. Per generation the ring says whether it closed,
     shipped and flipped, so the generations that never reach the screen split
     into never-shipped, shipped-and-superseded, and late-flipped.
  3. THE FENCE CROSS-TAB. Under BLIT_CHASE the slave waits on SYNC[14] (the first
     master row not yet shipped) before bands 1 and 2, so a compose can be ordered
     behind a blit. For each generation this asks whether some OTHER generation
     shipped between its launch and its echo, and cross-tabulates that against
     whether its echo was slow."""
import os, struct, subprocess, sys, tempfile
from collections import Counter

ARES = os.environ.get("ARES", "/Users/mikeholzinger/src/ares-debug/build_macos/headless-ui/Release/ares-headless")
BINS = ["<0.625", ".625-.75", ".75-.875", ".875-1.0", "1.0-1.125", "1.125-1.25", "1.25-1.375", ">=1.375"]

def read(rom, n):
    with tempfile.TemporaryDirectory() as td:
        g, h = os.path.join(td, "g.bin"), os.path.join(td, "h.bin")
        subprocess.run([ARES, "--frames", str(n),
                        "--dump", f"sdram:0x38000:0x384:{g}",
                        "--dump", f"sdram:0x39900:0x40:{h}", rom],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        G, H = open(g, "rb").read(), open(h, "rb").read()
    w = struct.unpack(">%dH" % (len(G)//2), G)
    hh = struct.unpack(">%dH" % (len(H)//2), H)
    return [list(w[i*7:i*7+7]) for i in range(64)], w[448], list(hh[:16])

rom = os.path.abspath(sys.argv[1])
pts = [int(x) for x in sys.argv[2:]]
data = {n: read(rom, n) for n in pts}

print("=" * 78)
print("1. WINDOWED LATENCY HISTOGRAMS (later read minus earlier), 0.125-vint bins")
print("=" * 78)
for a, b in zip(pts, pts[1:]):
    ha, hb = data[a][2], data[b][2]
    for base, name in ((0, "echo"), (8, "mtask")):
        d = [hb[base+i] - ha[base+i] for i in range(8)]
        tot = sum(d)
        if tot <= 0:
            print("  frames %d->%d %-5s: no generations" % (a, b, name)); continue
        fast = 100.0*d[0]/tot
        slow = 100.0*d[7]/tot
        print("  frames %d->%d %-5s  n=%d   FAST(<0.625v) %.1f%%   SLOW(>=1.375v) %.1f%%   middle %.1f%%"
              % (a, b, name, tot, fast, slow, 100.0 - fast - slow))
        print("      " + "  ".join("%s:%d" % (BINS[i], d[i]) for i in range(8) if d[i]))

for n in pts:
    ring, launched, _ = data[n]
    live = [r for r in ring if r[0]]
    if not live:
        continue
    vmax = max(r[0] for r in live)
    # a generation launched within 4 vints of the read has not had time to flip
    settled = [r for r in live if vmax - r[0] >= 4]
    ships = sorted(r[4] for r in live if r[4])
    print()
    print("=" * 78)
    print("2+3. FRAME %d  (%d of 64 slots, %d launched; %d settled, %d too young to judge)"
          % (n, len(live), launched, len(settled), len(live) - len(settled)))
    print("=" * 78)
    cls = Counter(); late = []
    for r in settled:
        launch, echo, close, ship, flip = r[0], r[1], r[3], r[4], r[5]
        if flip:
            if ship and flip - ship >= 2:
                cls["(d) shipped, flipped LATE (>=2 vints after ship)"] += 1
                late.append(flip - ship)
            else:
                cls["(a) flipped"] += 1
        elif ship:
            # superseded: some other generation shipped after this one did
            if any(s > ship for s in ships):
                cls["(b) shipped, OVERWRITTEN before any flip"] += 1
            else:
                cls["(b') shipped, no flip yet and nothing after it"] += 1
        elif close:
            cls["(c) closed, NEVER SHIPPED"] += 1
        else:
            cls["(e) launched, never closed"] += 1
    tot = sum(cls.values())
    for k in sorted(cls):
        print("   %-52s %3d  %5.1f%%" % (k, cls[k], 100.0*cls[k]/tot))
    if late:
        print("   (d) flip-minus-ship vints: %s" % Counter(late).most_common())
    print("   NOTE: the ship's offset INSIDE its vint is not in the ring -- field 6 carries")
    print("         the LAUNCH offset only. Distinguishing (b) by ship phase needs a build.")

    # fence cross-tab
    slow_cut = 2   # echo - launch >= 2 vints = the slow mode at this resolution
    tab = Counter()
    for r in settled:
        launch, echo = r[0], r[1]
        if not echo:
            continue
        overlapped = any(launch <= s <= echo for s in ships)
        slow = (echo - launch) >= slow_cut
        tab[(overlapped, slow)] += 1
    print()
    print("   FENCE CROSS-TAB: did another generation SHIP between this one's launch and echo?")
    print("   %-28s %-18s %-18s" % ("", "echo FAST (+0/+1)", "echo SLOW (>=+2)"))
    for ov, lab in ((True, "a ship overlapped"), (False, "no ship overlapped")):
        f, s = tab[(ov, False)], tab[(ov, True)]
        t = f + s
        print("   %-28s %-18s %-18s" % (lab, "%d (%.0f%%)" % (f, 100.0*f/t) if t else "0",
                                        "%d (%.0f%%)" % (s, 100.0*s/t) if t else "0"))
