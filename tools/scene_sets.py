#!/usr/bin/env python3
"""Every tile set each round's tilemap uses, from the rom, against the
baked per-round table in sh_src/pal_rounds_md.h.

The table was baked from live palette dumps over 64 scroll positions
(bake_tilecram.py --emit-mds). The rom tilemap is the whole level, so a
set that is here and not in the table is a cell MDS_REFUSE draws as
backdrop (LOOP29 190) wherever the level scrolls to it.

Unpacker format: LOOP-DECOMPILE 10 (high-byte runs, then low-byte
literals with a zero escape). Scene descriptors at 0x1CE2.
Usage: tools/scene_sets.py
"""
import re, struct, collections
rom = open('roms/altbeast/prog68k.bin', 'rb').read()
src = open('sh_src/pal_rounds_md.h').read()

def arr(name):
    m = re.search(name + r'\[MDROUND_N\]\[(\d+)\] = \{(.*?)\n\};', src, re.S)
    rows = re.findall(r'\{([^{}]*)\}', m.group(2))
    return [[int(x, 0) for x in r.replace('\n', ' ').split(',') if x.strip()] for r in rows]

s_line = arr('mdr_s_line')

def unpack(base):
    hi = bytearray(); p = base
    while len(hi) < 20480:
        cnt, val = rom[p], rom[p + 1]; p += 2; hi += bytes([val]) * (cnt + 1)
    lo = bytearray()
    while len(lo) < 20480:
        b = rom[p]; p += 1
        if b: lo.append(b)
        else:
            n = rom[p]; p += 1; lo += bytes(n + 1)   # zero run = n+1 (LOOP29 243: verified word-for-word against live tile RAM)
    return [(hi[i] << 8) | lo[i] for i in range(20480)]

for sc in range(5):
    d = 0x1CE2 + sc * 6
    blk = struct.unpack('>H', rom[d:d + 2])[0]
    base = struct.unpack('>I', rom[d + 2:d + 6])[0]
    words = unpack(base)
    intab = set(i for i, v in enumerate(s_line[sc]) if v)
    fg = collections.Counter((w >> 6) & 0x7F for w in words[0:5 * 2048])
    bg = collections.Counter((w >> 6) & 0x7F for w in words[5 * 2048:10 * 2048])
    missf = {s: n for s, n in sorted(fg.items()) if s not in intab and s}
    missb = {s: n for s, n in sorted(bg.items()) if s not in intab and s}
    print(f"round {sc} (block {blk}, data 0x{base:06X}): table has {len(intab)} sets")
    print(f"   FG pages 0-4 use {len([s for s in fg if s])} sets; NOT in table (set: cells): {missf}")
    print(f"   BG pages 5-9 use {len([s for s in bg if s])} sets; NOT in table (set: cells): {missb}")
