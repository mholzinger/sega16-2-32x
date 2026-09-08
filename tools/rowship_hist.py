#!/usr/bin/env python3
"""Decode the ROWSHIP=1 probe scrap (0x38000-0x39000) from two
gameplay_speed.py --extra 0x38000:4096 dumps (a = start, b = end):
per-8-row band, rows shipped per vint and how many of those ships
carried content identical to that bank's previous ship."""
import struct, sys
a = open(sys.argv[1], "rb").read(); b = open(sys.argv[2], "rb").read()
vints = int(sys.argv[3])
def u16s(d, off): return struct.unpack_from(">224H", d, off)
cnt = [(y - x) & 0xFFFF for x, y in zip(u16s(a, 0x800), u16s(b, 0x800))]
idn = [(y - x) & 0xFFFF for x, y in zip(u16s(a, 0xA00), u16s(b, 0xA00))]
tot = struct.unpack_from(">2I", b, 0xF00); tot0 = struct.unpack_from(">2I", a, 0xF00)
print(f"rows/vint top {(tot[0]-tot0[0])/vints:.1f} bottom {(tot[1]-tot0[1])/vints:.1f} "
      f"total {(sum(tot)-sum(tot0))/vints:.1f}; identical-content ships/vint {sum(idn)/vints:.1f}")
print("band   ship/vint  ident/vint   (rows y..y+7)")
for y0 in range(0, 224, 8):
    c = sum(cnt[y0:y0+8]) / vints; i = sum(idn[y0:y0+8]) / vints
    bar = "#" * int(c * 4)
    print(f"{y0:3d}-{y0+7:3d}  {c:6.2f}   {i:6.2f}   {bar}")
