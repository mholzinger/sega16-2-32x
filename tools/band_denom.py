#!/usr/bin/env python3
"""Band denominators: what band 2 composes in demo A, against bands 0 and 1.

    python3 tools/band_denom.py build/port/rom/genring2.32x 680 700 720 740 760 780

Reads the LATCHED sprite snapshot SPR_SNAP (0x28400, 64 x 8 words -- the compose's
only sprite source, m_main.c:9542) plus the per-pass sums STB and the chain counters,
and attributes every sprite's output rows to the band its rows fall in.

Band rows come from slave_concurrent_k (m_main.c:11611-11613) with the flags this rom
was built with (BAND_SHIFT=36, BAND_SHIFT_RG2=40):
    band 0 = rows   0..71   (72 rows)
    band 1 = rows  72..143  (72 rows)
    band 2 = rows 144..223  (80 rows)

Record fields, taken from our own decoder at m_main.c:7036-7070 and :7198, never guessed:
    e[0] low = top, high = bottom      e[1] & 0x1FF = x
    e[2] bit15 terminator, bit14 skip, bit13 MD-claimed, bit8 flip, low byte = pitch (s8)
    e[4] bits 6-7 = priority pp        e[5] vzoom = (>>5)&0x1F, hzoom = &0x1F
    1:1 iff (vzoom | hzoom) == 0 (the same test m_main.c:7242 uses)
Width in pixels = min(|pitch| * 4, 320), as m_main.c:7229-7231 computes it.

CAVEAT THIS CANNOT REMOVE: SPR_SNAP is a SNAPSHOT at the run's end, so each frame
point is ONE generation's sprite set, not a mean over the 64 in the ring. Several
frame points are read for that reason."""
import os, struct, subprocess, sys, tempfile

ARES = os.environ.get("ARES", "/Users/mikeholzinger/src/ares-debug/build_macos/headless-ui/Release/ares-headless")
BANDS = [("band 0", 0, 72), ("band 1", 72, 144), ("band 2", 144, 224)]

def read(rom, n):
    with tempfile.TemporaryDirectory() as td:
        a, b = os.path.join(td, "a.bin"), os.path.join(td, "b.bin")
        subprocess.run([ARES, "--frames", str(n),
                        "--dump", f"sdram:0x28400:0x400:{a}",
                        "--dump", f"sdram:0x39900:0x60:{b}", rom],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
        return open(a, "rb").read(), open(b, "rb").read()

def census(S):
    w = struct.unpack(">512H", S)
    out = []
    for i in range(64):
        e = w[i*8:i*8+8]
        if e[2] & 0x8000: break                       # terminator
        if e[2] & 0x2000: continue                    # MD VDP renders it
        top, bot = e[0] & 0xFF, e[0] >> 8
        if (e[2] & 0x4000) or top >= bot: continue    # skipped / empty
        pitch = e[2] & 0xFF
        if pitch > 127: pitch -= 256
        wid = min(abs(pitch) * 4, 320)
        vz, hz = (e[5] >> 5) & 0x1F, e[5] & 0x1F
        out.append(dict(top=top, bot=bot, w=wid, zoom=(vz | hz) != 0,
                        pp=(e[4] >> 6) & 3))
    return out

rom = os.path.abspath(sys.argv[1])
pts = [int(x) for x in sys.argv[2:]]
agg = {b[0]: dict(n=0, spr=0, rows=0, px=0, zspr=0, zrows=0) for b in BANDS}
print("frame | band   | sprites  rows   row-pixels | zoomed  z-rows | mean width")
for n in pts:
    S, _ = read(rom, n)
    recs = census(S)
    for name, lo, hi in BANDS:
        sel = [(r, min(r["bot"], hi) - max(r["top"], lo)) for r in recs]
        sel = [(r, k) for r, k in sel if k > 0]
        rows = sum(k for _, k in sel)
        px = sum(k * r["w"] for r, k in sel)
        z = [(r, k) for r, k in sel if r["zoom"]]
        a = agg[name]
        a["n"] += 1; a["spr"] += len(sel); a["rows"] += rows; a["px"] += px
        a["zspr"] += len(z); a["zrows"] += sum(k for _, k in z)
        print("%5d | %-6s | %7d %6d %11d | %6d %7d | %6.1f"
              % (n, name, len(sel), rows, px, len(z), sum(k for _, k in z),
                 px/float(rows) if rows else 0))
    print("      |        | %d records live of 64" % len(recs))

print()
print("MEANS over %d frame points" % len(pts))
print("band   | sprites  rows   row-pixels | zoomed z-rows | mean width | rows/band-row")
for name, lo, hi in BANDS:
    a = agg[name]; k = float(a["n"]); br = hi - lo
    print("%-6s | %7.1f %6.1f %11.0f | %6.1f %6.1f | %6.1f     | %5.2f"
          % (name, a["spr"]/k, a["rows"]/k, a["px"]/k, a["zspr"]/k, a["zrows"]/k,
             a["px"]/float(a["rows"]) if a["rows"] else 0, a["rows"]/k/br))
