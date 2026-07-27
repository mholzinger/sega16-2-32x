#!/usr/bin/env python3
"""Decode altbeast System 16B tile and sprite ROMs to preview PNGs.

Tiles: 3 planar ROMs (opr-11674/75/76), 8x8 3bpp, 16384 tiles.
Sprites: 8 ROMs interleaved into 16-bit words, 4bpp packed nibbles,
drawn row-wise by the sprite chip (no fixed tile size) — previewed here
as a raw nibble bitmap at a chosen width.
"""
import sys
from pathlib import Path
from PIL import Image

ROMS = Path(__file__).resolve().parent.parent / 'roms' / 'altbeast'
OUT = Path(__file__).resolve().parent.parent / 'gfx-preview'
OUT.mkdir(exist_ok=True)

# ---- tiles ----
planes = [ (ROMS / n).read_bytes() for n in
           ('opr-11674.a14', 'opr-11675.a15', 'opr-11676.a16') ]
ntiles = len(planes[0]) // 8          # 8 bytes per tile per plane
COLS = 128
rows = (ntiles + COLS - 1) // COLS
img = Image.new('L', (COLS * 8, rows * 8))
px = img.load()
for t in range(ntiles):
    tx, ty = (t % COLS) * 8, (t // COLS) * 8
    for y in range(8):
        b = [p[t * 8 + y] for p in planes]
        for x in range(8):
            bit = 7 - x
            pen = (((b[2] >> bit) & 1) << 2) | (((b[1] >> bit) & 1) << 1) | ((b[0] >> bit) & 1)
            px[tx + x, ty + y] = pen * 36
img.save(OUT / 'tiles.png')
print(f'tiles.png: {ntiles} tiles -> {img.size}')

# ---- sprites ----
# ROM_LOAD16_BYTE pairs: (even byte, odd byte) per 0x40000 chunk
pairs = [('epr-11681.b5', 'epr-11677.b1'), ('epr-11682.b6', 'epr-11678.b2'),
         ('epr-11683.b7', 'epr-11679.b3'), ('epr-11684.b8', 'epr-11680.b4')]
data = bytearray()
for even, odd in pairs:
    e, o = (ROMS / even).read_bytes(), (ROMS / odd).read_bytes()
    for eb, ob in zip(e, o):
        data += bytes((eb, ob))
# Sprite rows: stream of 16-bit words, 4 nibbles each (MSB first);
# a row ends when a word's final nibble is 0xF (sega16sp.cpp:1389).
rows, row = [], []
for i in range(0, len(data), 2):
    w = (data[i] << 8) | data[i + 1]
    nibs = [(w >> s) & 0xF for s in (12, 8, 4, 0)]
    row += nibs
    if nibs[3] == 0xF:
        # drop rows that are pure filler (all 0x0 or 0xF)
        if any(n not in (0, 0xF) for n in row):
            rows.append(row[:320])
        row = []

COLW, COLH, GUT = 320, 4096, 8
ncols = min(8, (len(rows) + COLH - 1) // COLH)
shown = min(len(rows), ncols * COLH)
img = Image.new('L', (ncols * (COLW + GUT), COLH))
px = img.load()
for r in range(shown):
    cx, cy = (r // COLH) * (COLW + GUT), r % COLH
    for x, n in enumerate(rows[r]):
        px[cx + x, cy] = n * 17
img.save(OUT / 'sprites.png')
print(f'sprites.png: {len(rows)} rows parsed, {shown} shown -> {img.size}')
