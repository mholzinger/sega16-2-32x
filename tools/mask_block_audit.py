#!/usr/bin/env python3
"""Audit experimental SH-2 composed opacity rows from a full SDRAM dump.

Rebuild every populated cache row independently from its complete stored source
keys and decoded tile art. This checks composition, banking and endian layout;
it does not establish framebuffer correctness, cadence, or hardware speed.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def audit(sdram, listing, art):
    if len(sdram) != 0x40000 or len(art) != 0x100000:
        raise ValueError('expected 256 KiB SDRAM and 1 MiB decoded tiles')
    symbols = {p[2]: int(p[0], 16) & 0x3ffff
               for line in listing.splitlines() if len(p := line.split()) == 3}
    rows, keys, tags, lookups, hits, builds = struct.unpack_from(
        '>6I', sdram, symbols['_r60_mb_cache'])
    rows &= 0x3ffff
    keys &= 0x3ffff
    tags &= 0x3ffff
    if (rows, keys, tags) != (0x3d800, 0x3e800, symbols['_r60_mb_tags']):
        raise ValueError('unexpected experimental cache layout')
    checked = wrong = slots = 0
    for slot in range(32):
        tag = struct.unpack_from('>I', sdram, tags + slot * 4)[0]
        if not tag:
            continue
        if not tag & 0x80000000:
            raise ValueError('invalid cache tag')
        slots += 1
        bank = (tag >> 9) & 7
        source = struct.unpack_from('>16H', sdram, keys + slot * 32)
        for y in range(32):
            expected = 0
            for x in range(32):
                word = source[y // 8 * 4 + x // 8]
                code = word & 8191
                if code & 4096:
                    code = (code & 4095) + bank * 4096
                code &= 16383
                if (word & 32768 and word & 8191 and
                        art[code * 64 + y % 8 * 8 + x % 8] & 7):
                    expected |= 1 << (31 - x)
            actual = struct.unpack_from('>I', sdram, rows + slot * 128 + y * 4)[0]
            wrong += actual != expected
            checked += 1
    diag = struct.unpack_from('>128I', sdram, 0x28e40)
    return dict(scope='stored cache rows only; not a framebuffer or speed verdict',
                slots=slots, checked_rows=checked, wrong_rows=wrong,
                lookups=lookups, hits=hits, builds=builds,
                vints=diag[0], ticks=diag[11], flips=diag[1], span_timeouts=diag[15])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sdram', type=Path)
    parser.add_argument('listing', type=Path)
    parser.add_argument('tiles', type=Path)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    result = audit(args.sdram.read_bytes(), args.listing.read_text(), args.tiles.read_bytes())
    result['inputs'] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                        for p in (args.sdram, args.listing, args.tiles)}
    report = json.dumps(result, indent=2) + '\n'
    if args.out:
        args.out.write_text(report)
    print(report, end='')
    return 0 if result['checked_rows'] and not result['wrong_rows'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
