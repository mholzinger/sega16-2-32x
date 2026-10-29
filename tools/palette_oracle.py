#!/usr/bin/env python3
"""Diff any palette dump against the ARCADE's own palette RAM.

    python3 tools/palette_oracle.py <scene> <dump.bin> [--all]

`discover/cram/arcade/sceneN.bin` is 0x1000 bytes of the arcade's palette
RAM at 0x840000, captured with `tools/arcade_palram.lua` on the attract
step that shows the scene. The port mirrors palette RAM 1:1 at work RAM
0xFF9000, so any port dump of that region compares word for word.

By default it reports only the tile palettes the scene's own tilemap
references, which is the set that has to be right for the background to
be right.

THE LAYOUT, since two formats live in this 4 kB (LOOP-DECOMPILE 81):

    tile palettes    128 x 8 colours   0x840000 + p*16    p = 0..127
    sprite palettes   64 x 16 colours  0x840800 + s*32    s = 0..63

Tile pixels are 3bpp so a tile palette is EIGHT colours, which is why
bake_tilecram.py reads `base + p*16` and pens 1..7. Sprites are 4bpp.
Nothing overlaps: the tile half ends exactly where the sprite half starts.
"""
import sys, collections

TILES_N = 20480
ROM = 'roms/altbeast/prog68k.bin'


def unpack(d, ptr):
    hi = bytearray()
    p = ptr
    while len(hi) < TILES_N and p + 1 < len(d):
        hi.extend(bytes([d[p + 1]]) * (d[p] + 1))
        p += 2
    lo = bytearray()
    while len(lo) < TILES_N and p < len(d):
        d0 = d[p]
        p += 1
        if d0:
            lo.append(d0)
            continue
        d2 = d[p]
        p += 1
        if d2 == 0:
            lo.append(0)
        else:
            lo.extend(b'\x00' * d2)
    return [(hi[i] << 8) | lo[i % len(lo)] for i in range(TILES_N)]


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    sc = int(sys.argv[1])
    dump = open(sys.argv[2], 'rb').read()
    everything = '--all' in sys.argv
    ref = open('discover/cram/arcade/scene%d.bin' % sc, 'rb').read()
    d = open(ROM, 'rb').read()
    o = 0x1CE2 + 6 * sc
    used = sorted({(t >> 6) & 0x7F
                   for t in unpack(d, int.from_bytes(d[o + 2:o + 6], 'big'))
                   if t & 0x1FFF})
    look = range(128) if everything else used
    bad = []
    for p in look:
        a, b = ref[p * 16:p * 16 + 16], dump[p * 16:p * 16 + 16]
        if a[2:16] != b[2:16]:            # pen 0 is transparent
            bad.append(p)
    print('scene %d: %d tile palettes checked (%s), %d differ'
          % (sc, len(list(look)), 'all 128' if everything else 'map-referenced',
             len(bad)))
    for p in bad:
        fa = ' '.join('%04X' % int.from_bytes(ref[p * 16 + 2 * k:p * 16 + 2 * k + 2],
                                              'big') for k in range(1, 8))
        fb = ' '.join('%04X' % int.from_bytes(dump[p * 16 + 2 * k:p * 16 + 2 * k + 2],
                                              'big') for k in range(1, 8))
        print('  pal %3d  arcade %s' % (p, fa))
        print('           dump   %s' % fb)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
