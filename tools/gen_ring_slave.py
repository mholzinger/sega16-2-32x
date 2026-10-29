#!/usr/bin/env python3
"""Join the GENRING master and slave rings: is the echo the SLAVE'S BODY or the MASTER'S POLL?

    python3 tools/gen_ring_slave.py build/port/rom/genring2.32x 455 500 680 780

The master stamps PHS[0] at its first poll seeing SYNC[1], which is an observation. The
slave ring (0x38A00, s_main.c) carries the chain's own split, keyed to the master's slot
by the launch count latched at pickup. The gap between the slave's own completion and the
master's first poll IS the poll term, and it decides whether this is a body card or a
scheduling card.

Slave fields, all in LINES since pickup except [0] and [7]: [1] band 0 done, [2] fence 1
wait, [3] band 1 done, [4] fence 2 wait, [5] chain complete, [6] echo written."""
import os, struct, subprocess, sys, tempfile
from collections import Counter

ARES = os.environ.get("ARES", "/Users/mikeholzinger/src/ares-debug/build_macos/headless-ui/Release/ares-headless")
LPV = 262.0   # lines per vint

def read(rom, n):
    with tempfile.TemporaryDirectory() as td:
        m, sl = os.path.join(td, "m.bin"), os.path.join(td, "s.bin")
        subprocess.run([ARES, "--frames", str(n),
                        "--dump", f"sdram:0x38000:0x384:{m}",
                        "--dump", f"sdram:0x38A00:0x400:{sl}", rom],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        M, S = open(m, "rb").read(), open(sl, "rb").read()
    mw = struct.unpack(">%dH" % (len(M)//2), M)
    sw = struct.unpack(">%dH" % (len(S)//2), S)
    return ([list(mw[i*7:i*7+7]) for i in range(64)], mw[448], mw[449],
            [list(sw[i*8:i*8+8]) for i in range(64)])

def stat(v, unit="lines"):
    if not v: return "none"
    v = sorted(v)
    m = sum(v)/float(len(v))
    if unit == "vints":
        return "mean %.3fv  median %.3fv  min %.3fv  max %.3fv  n=%d" % (
            m/LPV, v[len(v)//2]/LPV, v[0]/LPV, v[-1]/LPV, len(v))
    return "mean %.0f  median %d  min %d  max %d  n=%d" % (m, v[len(v)//2], v[0], v[-1], len(v))

rom = os.path.abspath(sys.argv[1])
for n in [int(x) for x in sys.argv[2:]]:
    ring, launched, tmo, sring = read(rom, n)
    live = [(i, r) for i, r in enumerate(ring) if r[0]]
    if not live:
        print("frame %d: master ring empty" % n); continue
    vmax = max(r[0] for _, r in live)
    pairs = []
    for i, r in live:
        sr = sring[i]
        # the slave slot must belong to THIS generation: its latched launch count
        # is gr_n at the time, and the master's slot i was written at gr_n-1
        if sr[0] and ((sr[0] - 1) & 63) == i and sr[5]:
            pairs.append((r, sr))
    print()
    print("=" * 76)
    print("FRAME %d: %d master slots, %d launched, %d joined to a completed slave chain"
          % (n, len(live), launched, len(pairs)))
    print("          FENCE TIMEOUTS EVER (GR[449]): %d" % tmo)
    print("=" * 76)
    if not pairs:
        print("  no joined generations"); continue
    comp   = [sr[5] for _, sr in pairs]                 # slave chain complete, lines
    echo_v = [(r[1] - r[0]) for r, _ in pairs if r[1]]  # master first poll, VINTS
    print("  SLAVE CHAIN COMPLETE (its own clock):      %s" % stat(comp, "vints"))
    print("  MASTER FIRST POLL   (PHS[0], vint grain):  %s"
          % ("mean %.3fv  dist %s" % (sum(echo_v)/float(len(echo_v)),
             dict(sorted(Counter(echo_v).items()))) if echo_v else "none"))
    mean_c = sum(comp)/float(len(comp))/LPV
    mean_p = sum(echo_v)/float(len(echo_v)) if echo_v else 0.0
    print("  >>> POLL TERM = first poll - slave complete = %.3f - %.3f = %.3f vints"
          % (mean_p, mean_c, mean_p - mean_c))
    print()
    print("  THE SLAVE'S SPLIT, lines since pickup (262/vint):")
    print("    band 0 done        %s" % stat([sr[1] for _, sr in pairs]))
    print("    fence 1 WAIT       %s" % stat([sr[2] for _, sr in pairs]))
    print("    band 1 done        %s" % stat([sr[3] for _, sr in pairs]))
    print("    fence 2 WAIT       %s" % stat([sr[4] for _, sr in pairs]))
    print("    band 2 done        %s" % stat([sr[5] for _, sr in pairs]))
    e = [sr[6] for _, sr in pairs if sr[6]]
    print("    echo written       %s" % stat(e))
    print()
    b0 = [sr[1] for _, sr in pairs]
    b1 = [sr[3][0] if isinstance(sr[3], list) else sr[3] - sr[2] - sr[1] for _, sr in pairs]
    b2 = [sr[5] - sr[4] - sr[3] for _, sr in pairs]
    f1 = [sr[2] for _, sr in pairs]; f2 = [sr[4] for _, sr in pairs]
    tot = sum(b0) + sum(b1) + sum(b2) + sum(f1) + sum(f2)
    if tot:
        print("  WHERE THE CHAIN GOES (share of the summed chain):")
        for lab, v in (("band 0 body", b0), ("fence 1 wait", f1), ("band 1 body", b1),
                       ("fence 2 wait", f2), ("band 2 body", b2)):
            print("    %-13s %6.1f%%   mean %5.0f lines = %.3f vints"
                  % (lab, 100.0*sum(v)/tot, sum(v)/float(len(v)), sum(v)/float(len(v))/LPV))
