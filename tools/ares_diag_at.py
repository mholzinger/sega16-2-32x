#!/usr/bin/env python3
"""Run ares-headless to frame N (repeatable) and print the MD-plane
pipeline counters at that instant: SH-2 DIAG slots (builder) and the
68K's WRAM consume counters. Deterministic, ~1s per frame point.

    python3 tools/ares_diag_at.py rom/s16.32x 85 105 130 200

DIAG map (sh_src/m_main.c): [9] cycles [40] tile batches built [41] NT
chunks built [57] tiles shipped [42] publish deferred (68K had not
consumed) [43] build deferred [34] md_phase|forced<<8|pending<<16;
DISP_CENSUS 0x28F7C = blanks<<16 | held vints. WRAM: 0xFFB0E2 packets
consumed, 0xFFB0E4 tile packets, 0xFFB0E6 chunk packets, 0xFFB0F0
vints, 0xFFB0FC entry rejects, 0xFFA0F2 consumes skipped (FM=1),
0xFFA038/3E consume span mean/max, 0xFFA0A0 V at post (last)."""
import os, struct, subprocess, sys, tempfile
ARES = os.environ.get("ARES", "/Users/mikeholzinger/src/ares-debug/build_macos/headless-ui/Release/ares-headless")
rom = os.path.abspath(sys.argv[1])
prev = None
print("frame | cyc  tb   nt   tiles unc  bdef | phase frc pend | blanks held | 68K vints pkts tb  nt  rej  fmskip | Vpost")
for n in map(int, sys.argv[2:]):
    with tempfile.TemporaryDirectory() as td:
        d = os.path.join(td, "d.bin"); w = os.path.join(td, "w.bin")
        subprocess.run([ARES, "--frames", str(n), "--dump", f"sdram:0x28000:0x1000:{d}",
                        "--dump", f"wram:0xFFA000:0x2000:{w}", rom],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        D = open(d, "rb").read(); W = open(w, "rb").read()
    Dg = lambda i: struct.unpack(">I", D[i*4:i*4+4])[0]
    w16 = lambda a: struct.unpack(">H", W[a-0xFFA000:a-0xFFA000+2])[0]
    dc = struct.unpack(">I", D[0xF7C:0xF80])[0]
    ph = Dg(34)
    cur = (Dg(9), Dg(40), Dg(41), Dg(57), Dg(42), Dg(43))
    if prev:
        delta = " (+%d +%d +%d +%d +%d +%d)" % tuple(c - p for c, p in zip(cur, prev))
    else:
        delta = ""
    print("%5d | %4d %4d %4d %5d %4d %4d | %3d %3d %4d | %3d %5d | %5d %5d %4d %4d %4d %4d | %02X%s"
          % (n, *cur, ph & 0xFF, (ph >> 8) & 0xFF, ph >> 16, dc >> 16, dc & 0xFFFF,
             w16(0xFFB0F0), w16(0xFFB0E2), w16(0xFFB0E4), w16(0xFFB0E6), w16(0xFFB0FC), w16(0xFFA0F2),
             w16(0xFFA0A0) >> 8, delta))
    prev = cur
