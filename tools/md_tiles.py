#!/usr/bin/env python3
"""System 16 tile ROMs -> Mega Drive 4bpp planar, with verification.

The pivot needs S16 patterns in MD VRAM. S16 tiles are 3 bitplanes (pens
0-7); MD wants 4bpp packed, 32 bytes per tile, 4 bytes per row, HIGH
NIBBLE = LEFT pixel. Pens 0-7 drop straight into a nibble, which leaves
8-15 free -- that is what makes one MD palette able to hold an FG set in
pens 0-7 and a BG set in 8-15 (ARCHITECTURE.md section 4).

  tools/md_tiles.py verify        round-trip every tile, report mismatches
  tools/md_tiles.py png OUT [N]   render 256 tiles from index N to a PNG
  tools/md_tiles.py bin OUT       emit all tiles as MD planar
"""
import sys, zlib, struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ROMS = ROOT / 'roms' / 'altbeast'
PLANES = [(ROMS / n).read_bytes() for n in
          ('opr-11674.a14', 'opr-11675.a15', 'opr-11676.a16')]
NTILES = len(PLANES[0]) // 8


def s16_tile(t):
    px = []
    for y in range(8):
        b0, b1, b2 = (p[t * 8 + y] for p in PLANES)
        for bit in range(7, -1, -1):
            px.append((((b2 >> bit) & 1) << 2) | (((b1 >> bit) & 1) << 1)
                      | ((b0 >> bit) & 1))
    return px


def md_planar(px):
    out = bytearray(32)
    for y in range(8):
        for i in range(4):
            out[y * 4 + i] = (px[y * 8 + i * 2] << 4) | px[y * 8 + i * 2 + 1]
    return bytes(out)


def md_unplanar(b):
    px = []
    for y in range(8):
        for i in range(4):
            v = b[y * 4 + i]
            px.append(v >> 4)
            px.append(v & 15)
    return px


def png(path, w, h, rows):
    raw = b''.join(b'\x00' + bytes(r) for r in rows)

    def ck(t, d):
        c = t + d
        return struct.pack('>I', len(d)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n'
                           + ck(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                           + ck(b'IDAT', zlib.compress(raw)) + ck(b'IEND', b''))


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'verify'
    if cmd == 'verify':
        bad = sum(1 for t in range(NTILES)
                  if md_unplanar(md_planar(s16_tile(t))) != s16_tile(t))
        print(f'{NTILES} tiles, {bad} round-trip mismatches')
        sys.exit(1 if bad else 0)
    if cmd == 'bin':
        out = bytearray()
        for t in range(NTILES):
            out += md_planar(s16_tile(t))
        Path(sys.argv[2]).write_bytes(out)
        print(f'{sys.argv[2]}: {NTILES} tiles, {len(out)} bytes')
        return
    if cmd == 'png':
        base = int(sys.argv[3]) if len(sys.argv) > 3 else 0
        pal = [(0, 0, 0), (60, 50, 40), (96, 80, 60), (130, 110, 80),
               (165, 145, 110), (200, 180, 140), (230, 215, 180), (255, 250, 235)]
        w, h = 32 * 8, 8 * 8
        rows = [bytearray(w * 3) for _ in range(h)]
        for i in range(256):
            px = md_unplanar(md_planar(s16_tile(base + i)))
            tx, ty = (i % 32) * 8, (i // 32) * 8
            for y in range(8):
                for x in range(8):
                    o = (tx + x) * 3
                    rows[ty + y][o:o + 3] = bytes(pal[px[y * 8 + x]])
        png(sys.argv[2], w, h, rows)
        print(f'{sys.argv[2]}: tiles {base}..{base + 255}')
        return
    print(__doc__)


main()
