#!/usr/bin/env python3
"""Interleave the altbeast sprite ROMs into the 16-bit-BE stream the sprite
hardware reads (MAME ROM_REGION16_BE "sprites": even byte = b5-b8 socket,
odd byte = b1-b4, four 256KB pair-blocks).

Output: sh_src/sprites.bin — 1MB, word i == MAME spritedata[i].
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ROMS = ROOT / 'roms' / 'altbeast'

PAIRS = [('epr-11681.b5', 'epr-11677.b1'),
         ('epr-11682.b6', 'epr-11678.b2'),
         ('epr-11683.b7', 'epr-11679.b3'),
         ('epr-11684.b8', 'epr-11680.b4')]

out = bytearray()
for hi_name, lo_name in PAIRS:
    hi = (ROMS / hi_name).read_bytes()
    lo = (ROMS / lo_name).read_bytes()
    assert len(hi) == len(lo) == 0x20000
    block = bytearray(0x40000)
    block[0::2] = hi
    block[1::2] = lo
    out += block
(ROOT / 'sh_src' / 'sprites.bin').write_bytes(out)
print(f'sprites.bin: {len(out)} bytes')
