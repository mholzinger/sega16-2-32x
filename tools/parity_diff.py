#!/usr/bin/env python3
"""Parity scoreboard: per-scene pixel diff of ours vs the arcade oracle.

Usage: parity_diff.py <capdir>
Reads <scene>_arc.png / <scene>_ours.png pairs, writes <scene>_diff.png
(mismatches in magenta over a dimmed base) and prints a scoreboard line
per scene: percent of pixels whose RGB differs beyond tolerance.

Tolerance exists because S16 5-bit color -> 32X RGB555 conversion is
exact in our pipeline, but MAME's arcade path renders through its own
palette scaling — small per-channel deltas are conversion noise, not
bugs. TOL=12 (8-bit units) keeps real layer/sprite/priority errors
visible while ignoring rounding.
"""
import struct
import sys
import zlib
from pathlib import Path


def read_png(path):
    data = Path(path).read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos, w, h, bitd, ctype = 8, 0, 0, 0, 0
    idat = b""
    while pos < len(data):
        ln = struct.unpack(">I", data[pos:pos + 4])[0]
        typ = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            w, h, bitd, ctype = struct.unpack(">IIBB", chunk[:10])
        elif typ == b"IDAT":
            idat += chunk
        pos += 12 + ln
    raw = zlib.decompress(idat)
    bpp = {0: 1, 2: 3, 4: 2, 6: 4}[ctype]
    stride = w * bpp
    out = bytearray(w * h * 3)
    prev = bytearray(stride)
    pos = 0
    for y in range(h):
        filt = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        pos += stride
        if filt == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif filt == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif filt == 3:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + (a + prev[i]) // 2) & 0xFF
        elif filt == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        for x in range(w):
            o = (y * w + x) * 3
            if ctype == 2 or ctype == 6:
                out[o:o + 3] = line[x * bpp:x * bpp + 3]
            else:
                out[o] = out[o + 1] = out[o + 2] = line[x * bpp]
        prev = line
    return w, h, out


def write_png(path, w, h, rgb):
    def chunk(t, d):
        c = t + d
        return struct.pack(">I", len(d)) + c + struct.pack(">I", zlib.crc32(c))
    raw = b"".join(b"\x00" + bytes(rgb[y * w * 3:(y + 1) * w * 3])
                   for y in range(h))
    Path(path).write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


TOL = 12
SCENES = ["title", "scream", "eyehold", "demo", "demo2"]


def main():
    d = Path(sys.argv[1])
    total_score = 0.0
    n = 0
    for s in SCENES:
        a, o = d / f"{s}_arc.png", d / f"{s}_ours.png"
        if not a.exists() or not o.exists():
            print(f"{s:8s}  MISSING ({'arc ' if not a.exists() else ''}"
                  f"{'ours' if not o.exists() else ''})")
            continue
        wa, ha, pa = read_png(a)
        wo, ho, po = read_png(o)
        w, h = min(wa, wo), min(ha, ho)
        diff = bytearray(w * h * 3)
        bad = 0
        for y in range(h):
            for x in range(w):
                ia = (y * wa + x) * 3
                io = (y * wo + x) * 3
                od = (y * w + x) * 3
                mism = (abs(pa[ia] - po[io]) > TOL
                        or abs(pa[ia + 1] - po[io + 1]) > TOL
                        or abs(pa[ia + 2] - po[io + 2]) > TOL)
                if mism:
                    bad += 1
                    diff[od], diff[od + 1], diff[od + 2] = 255, 0, 255
                else:
                    diff[od] = pa[ia] // 3
                    diff[od + 1] = pa[ia + 1] // 3
                    diff[od + 2] = pa[ia + 2] // 3
        pct = 100.0 * bad / (w * h)
        total_score += pct
        n += 1
        write_png(d / f"{s}_diff.png", w, h, diff)
        print(f"{s:8s}  {pct:6.2f}%  ({bad}/{w*h})")
    if n:
        print(f"{'TOTAL':8s}  {total_score / n:6.2f}%  (mean of {n} scenes)")


if __name__ == "__main__":
    main()
