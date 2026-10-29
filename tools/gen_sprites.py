#!/usr/bin/env python3
"""Interleave the altbeast sprite ROMs into the 16-bit-BE stream the sprite
hardware reads (MAME ROM_REGION16_BE "sprites": even byte = b5-b8 socket,
odd byte = b1-b4, four 256KB pair-blocks).

Output: sh_src/sprites.bin — 1MB, word i == MAME spritedata[i].
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
import os
GAME = os.environ.get('GAME', 'altbeast')
ROMS = ROOT / 'roms' / GAME
# per-game (hi, lo) pairs in bank order. The Japanese set 7 has seven
# 64KB pairs where the US has four 128KB ones; MAME places each JP pair
# 0x40000 apart (a 128KB hole between), so the game's sprite bank values
# are even and the SH-2 halves them under GAME_ALTBEASTJ. Data is
# byte-equal to the US hi/lo streams.
PAIRS = {
    'altbeast':  [('epr-11681.b5', 'epr-11677.b1'),
                  ('epr-11682.b6', 'epr-11678.b2'),
                  ('epr-11683.b7', 'epr-11679.b3'),
                  ('epr-11684.b8', 'epr-11680.b4')],
    # Golden Axe set 6: three 256 KB pairs, (even byte, odd byte) per MAME's load
    # offsets (ic12 @0 / ic9 @1, ic13 @0x40000 / ic10 @0x40001, ic14 / ic11)
    'goldnaxe':  [('mpr-12379.ic12', 'mpr-12378.ic9'),
                  ('mpr-12381.ic13', 'mpr-12380.ic10'),
                  ('mpr-12383.ic14', 'mpr-12382.ic11')],
    'altbeastj': [('epr-11729.b5', 'epr-11725.b1'),
                  ('epr-11730.b6', 'epr-11726.b2'),
                  ('epr-11731.b7', 'epr-11727.b3'),
                  ('epr-11732.b8', 'epr-11728.b4'),
                  ('epr-11733.b10', 'epr-11717.a1'),
                  ('epr-11734.b11', 'epr-11718.a2'),
                  ('epr-11735.b12', 'epr-11719.a3')],
}[GAME]

out = bytearray()
for hi_name, lo_name in PAIRS:
    hi = (ROMS / hi_name).read_bytes()
    lo = (ROMS / lo_name).read_bytes()
    assert len(hi) == len(lo) and len(hi) in (0x10000, 0x20000, 0x40000)   # AB US 128KB / AB JP 64KB / Golden Axe 256KB pairs
    block = bytearray(2 * len(hi))          # one interleaved pair = one bank unit per half
    block[0::2] = hi
    block[1::2] = lo
    out += block
(ROOT / 'sh_src' / 'sprites.bin').write_bytes(out)
print(f'sprites.bin: {len(out)} bytes')
