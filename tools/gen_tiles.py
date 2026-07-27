#!/usr/bin/env python3
"""Convert altbeast planar tile ROMs to chunky 8bpp for the SH-2 renderer.

Output: sh_src/tiles.bin — 16384 tiles x 64 bytes, row-major, one pen
(0-7) per byte. Same plane significance as tools/decode_gfx.py.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ROMS = ROOT / 'roms' / 'altbeast'

planes = [(ROMS / n).read_bytes() for n in
          ('opr-11674.a14', 'opr-11675.a15', 'opr-11676.a16')]
ntiles = len(planes[0]) // 8
out = bytearray(ntiles * 64)
i = 0
for t in range(ntiles):
    for y in range(8):
        b0, b1, b2 = (p[t * 8 + y] for p in planes)
        for bit in range(7, -1, -1):
            out[i] = (((b2 >> bit) & 1) << 2) | (((b1 >> bit) & 1) << 1) | ((b0 >> bit) & 1)
            i += 1
(ROOT / 'sh_src' / 'tiles.bin').write_bytes(out)
print(f'tiles.bin: {ntiles} tiles, {len(out)} bytes')
