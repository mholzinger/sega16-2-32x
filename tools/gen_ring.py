#!/usr/bin/env python3
"""Read the GENRING generation stamp ring (make port ... PHASECENSUS=1 GENRING=1).

    python3 tools/gen_ring.py build/port/rom/genring.32x 780 500

Per generation the master stamps the V-ISR vint number (DIAG[49]) at six events --
launch, slave chain echo landed, master tail done, compose close, ship, ISR flip --
into a 64-entry ring at SDRAM 0x38000, stored as vint+1 so 0 means "never happened".
GR[384] is the launch count, which says whether the ring wrapped.

The ring holds the LAST 64 generations before the named frame, so the window a run
reports is the one it prints, not the one you asked for. ares --dump reflects the END
state whatever frame is named, so this runs ares once per frame point.

Prints, per frame point, the distribution of each event's offset from launch IN VINTS
and the share of generations whose compose CLOSES in the launch vint -- the number
P2's one-vint frame depends on."""
import os, struct, subprocess, sys, tempfile
from collections import Counter
PHH_ = {}

ARES = os.environ.get("ARES", "/Users/mikeholzinger/src/ares-debug/build_macos/headless-ui/Release/ares-headless")
EV = ["launch", "echo", "tail", "close", "ship", "flip"]

def read(rom, n):
    with tempfile.TemporaryDirectory() as td:
        f = os.path.join(td, "g.bin")
        p2 = os.path.join(td, "p.bin")
        subprocess.run([ARES, "--frames", str(n), "--dump", f"sdram:0x38000:0x384:{f}",
                        "--dump", f"sdram:0x28E00:0x200:{p2}", rom],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        b = open(f, "rb").read(); q = open(p2, "rb").read()
    w = struct.unpack(">%dH" % (len(b) // 2), b)
    g32 = lambda o: struct.unpack(">I", q[o:o+4])[0]
    ph = dict(echo=g32(0x40), mtask=g32(0x44), lag=g32(0x48), ship=g32(0x4C),
              flip=g32(0x50), gens=g32(0x150 + 4), flips=g32(0x40 + 40))
    return [list(w[i*7:i*7+7]) for i in range(64)], w[448], ph

def dist(c, tot):
    return " ".join("%d:%d(%.0f%%)" % (k, c[k], 100.0*c[k]/tot) for k in sorted(c))

for n in map(int, sys.argv[2:]):
    ring, launched, ph = read(os.path.abspath(sys.argv[1]), n)
    PHH_[n] = ph
    live = [r for r in ring if r[0]]
    if not live:
        print("frame %d: ring empty (launches=%d)" % (n, launched)); continue
    lv = [r[0] - 1 for r in live]
    print("\n=== frame %d: %d of 64 ring slots filled, %d generations launched, "
          "ring covers vints %d..%d ===" % (n, len(live), launched, min(lv), max(lv)))
    if launched > 64 and len(live) < 64:
        print("    WARNING: ring wrapped but has holes -- a slot was launched and never stamped")
    for e in range(1, 6):
        got = [r for r in live if r[e]]
        if not got:
            print("  %-6s never stamped in this window" % EV[e]); continue
        c = Counter(r[e] - r[0] for r in got)
        print("  %-6s - launch, vints: %s   [%d of %d generations reached it]"
              % (EV[e], dist(c, len(got)), len(got), len(live)))
    off = [r[6] - 1 for r in live if r[6]]
    if off:
        off.sort()
        print("  launch offset into its vint, LINES: min %d  median %d  mean %.0f  max %d  (262/vint)"
              % (off[0], off[len(off)//2], sum(off)/len(off), off[-1]))
    closed = [r for r in live if r[3]]
    same = sum(1 for r in closed if r[3] == r[0])
    print("  COMPOSE CLOSES IN THE LAUNCH VINT: %d of %d closed generations = %.1f%%"
          % (same, len(closed), 100.0*same/len(closed) if closed else 0.0))
    shipped = [r for r in live if r[4]]
    if shipped:
        s0 = sum(1 for r in shipped if r[4] == r[0])
        print("  SHIPS IN THE LAUNCH VINT:          %d of %d shipped generations = %.1f%%"
              % (s0, len(shipped), 100.0*s0/len(shipped)))

pts = sorted(PHH_)
if len(pts) >= 2:
    print("\n=== PHASE_CENSUS means, WINDOWED (later point minus earlier), FRT ticks/12052 = vints ===")
    for a, b in zip(pts, pts[1:]):
        A, B = PHH_[a], PHH_[b]
        g = B["gens"] - A["gens"]
        if g <= 0:
            print("  %d->%d: no generations closed in between" % (a, b)); continue
        print("  vints %d->%d, %d generations closed:" % (a, b, g))
        for k in ("echo", "mtask", "lag", "ship", "flip"):
            print("     %-6s %6.3f vints" % (k, (B[k] - A[k]) / float(g) / 12052.0))
