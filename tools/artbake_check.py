#!/usr/bin/env python3
"""artbake_check.py: the baked art AS THE SH-2 WILL READ IT, out of the finished .32x.

tools/bake_art.py checks its own output before the link; this re-reads the index from the ROM
IMAGE through the SH-2's address map (windows 1-3 straight at cart offset, window 0 behind the
music blob in page 8 = slot 7, SH-2 0x02380000 -> file 0x400000) and re-decodes every frame
against sprites.bin. It is what caught the key-word collision of 2026-11-04 (flip bit on the
pitch's sign: 23 of 115 frames under the wrong key) that the pre-link check could not see.

  python3 tools/artbake_check.py build/port/rom/p15b.32x [--rom sh_src/sprites.bin] [--index 0x023E05A0]

Exit 1 on any mismatch.
"""
import argparse, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import s16art, bake_art  # noqa: E402


def file_off(a, page=8):
    """SH-2 address -> file offset; slot 7 (0x02380000..) shows the given mapper page"""
    return a - 0x02380000 + page * 0x80000 if 0x02380000 <= a < 0x02400000 else a - 0x02000000


def check_page(rom, idx, page, W):
    off = file_off(idx, page)
    if rom[off:off + 4] != b'AB1\0':
        print('page %d: no index magic at SH-2 %08x (file %x)' % (page, idx, off)); return 0, 1
    n, total = struct.unpack_from('>II', rom, off + 4)
    bad = 0
    prev = -1
    for i in range(n):
        kw, addr = struct.unpack_from('>II', rom, off + 16 + 8 * i)
        if kw <= prev:
            print('page %d entry %d: key %08x not above the previous' % (page, i, kw)); bad += 1
        prev = kw
        key = bake_art.unkeyword(kw)
        fo = file_off(addr, page)
        h, _w = struct.unpack_from('>HH', rom, fo)
        rows, _, rownw = s16art.decode(W, *key, h)
        ref = s16art.pack_frame(rows, rownw)
        if rom[fo:fo + len(ref)] != ref:
            print('page %d entry %d: key %08x %s at %08x differs from a fresh decode' % (page, i, kw, key, addr)); bad += 1
    print('  page %d: %d baked frames (%d bytes) re-decoded from the rom image, %d bad' % (page, n, total, bad))
    return n, bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom32x')
    ap.add_argument('--rom', default='sh_src/sprites.bin')
    ap.add_argument('--index', default=None, help='SH-2 address of the index (default: from sh_src/artbake.h or the page-8 scan)')
    ap.add_argument('--pages', type=int, default=None, help='per-level pages to walk (default: from artbake.h, else 1)')
    a = ap.parse_args()
    rom = open(a.rom32x, 'rb').read()
    idx = None
    npages = a.pages
    if a.index:
        idx = int(a.index, 0)
    for hp in ('build/port/sh_src/artbake.h', 'sh_src/artbake.h'):
        if os.path.exists(hp):
            for line in open(hp):
                if line.startswith('#define ARTBAKE_WIN0_ADDR') and idx is None:
                    idx = int(line.split()[2].rstrip('U'), 0)
                if line.startswith('#define ARTBAKE_PAGES') and npages is None:
                    npages = int(line.split()[2])
    if idx is None:   # scan page 8 for the magic
        p = rom.find(b'AB1\0', 0x400000)
        if p < 0:
            print('no index magic in page 8'); sys.exit(1)
        idx = p - 0x400000 + 0x02380000
    if npages is None:
        npages = 1
    W = s16art.load_rom(a.rom)
    # the music blob must be byte-identical in every page: the 68K reads it through slot 7
    # whatever page the level put there
    blob_end = file_off(idx, 8) - 8 * 0x80000
    tot = bad = 0
    for page in range(8, 8 + npages):
        if page > 8 and rom[page * 0x80000:page * 0x80000 + blob_end] != rom[8 * 0x80000:8 * 0x80000 + blob_end]:
            print('page %d: the music blob differs from page 8' % page); bad += 1
        n, b = check_page(rom, idx, page, W)
        tot += n; bad += b
    print('%s: %d page%s, %d baked frames re-decoded from the rom image, %d bad'
          % (os.path.basename(a.rom32x), npages, 's' if npages > 1 else '', tot, bad))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
