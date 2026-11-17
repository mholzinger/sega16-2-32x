#!/usr/bin/env python3
"""Structural census of larger blocks in arcade tile/text/WRAM event dumps.

Input: prefixes of gameplay_dump.lua ev_N snapshots. Source layout follows
r60_planes.c:675-719 and r60_tiles.h. Counts visible blocks and exact repeated
source keys; these are NOT timing estimates or cache-hit claims. Different
scrolls/planes stay separate. Palette/priority attributes remain in art keys.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def tile_at(tilemap, pages, col, row, valid_pages=13):
    col &= 127
    row &= 63
    quadrant = (col >> 6) | ((row >> 5) << 1)
    page = (pages >> (quadrant * 4)) & 15
    if page >= valid_pages:
        return 0
    return tilemap[page * 2048 + (row & 31) * 64 + (col & 63)]


def physical_code(word, bank):
    code = word & 8191
    if code & 4096:
        code = (code & 4095) + bank * 4096
    return code & 16383


def census(prefix, art, allkeys, bank_offset=0x3095, valid_pages=13):
    paths = {name: Path(str(prefix) + '_' + name + '.bin') for name in ('tilemap', 'text', 'wram')}
    data = {name: path.read_bytes() for name, path in paths.items()}
    tilemap = struct.unpack('>32768H', data['tilemap'])
    text = struct.unpack('>2048H', data['text'])
    wram = data['wram']
    if len(wram) != 16384:
        raise ValueError('expected WRAM FFC000..FFFFFF')
    bank = wram[bank_offset] & 7
    result = dict(prefix=str(prefix), tile_bank=bank, planes=[],
                  inputs={name: dict(path=str(paths[name]), sha256=hashlib.sha256(raw).hexdigest())
                          for name, raw in data.items()})
    for plane in range(2):
        pages = text[0x740 + plane]
        xs = (192 - text[0x74c + plane]) & 1023
        ys = text[0x748 + plane] & 511
        c0, c1 = xs // 8, (xs + 319) // 8
        r0, r1 = ys // 8, (ys + 223) // 8
        cells = (c1 - c0 + 1) * (r1 - r0 + 1)
        stats = dict(plane=plane, pages=f'{pages:04x}', xs=xs, ys=ys,
                     visible_cells=cells, priority_cells=0, empty_priority_cells=0,
                     priority_rows=0, empty_priority_rows=0, blocks={})
        for row in range(r0, r1 + 1):
            for col in range(c0, c1 + 1):
                word = tile_at(tilemap, pages, col, row, valid_pages)
                if word & 32768 and word & 8191:
                    stats['priority_cells'] += 1
                    code = physical_code(word, bank)
                    pixels = art[code * 64:(code + 1) * 64]
                    empty_rows = sum(not any(p & 7 for p in pixels[y:y + 8])
                                     for y in range(0, 64, 8))
                    stats['priority_rows'] += 8
                    stats['empty_priority_rows'] += empty_rows
                    stats['empty_priority_cells'] += empty_rows == 8
        for side in (2, 4, 8):
            keys = []
            priority = []
            for br in range(r0 // side, r1 // side + 1):
                for bc in range(c0 // side, c1 // side + 1):
                    words = tuple(tile_at(tilemap, pages, bc * side + x, br * side + y, valid_pages)
                                  for y in range(side) for x in range(side))
                    keys.append((bank, words))
                    maskkey = tuple(physical_code(w, bank) if w & 32768 and w & 8191 else -1
                                    for w in words)
                    if any(code >= 0 for code in maskkey):
                        priority.append(maskkey)
                        if plane == 0:
                            allkeys[side]['mask'].add(maskkey)
                    if any(w & 8191 for w in words):
                        allkeys[side]['art'].add((bank, words))
            mask_bytes = side * side * 8
            key_bytes = side * side * 2
            stats['blocks'][str(side * 8)] = dict(
                visible_blocks=len(keys), unique_art_keys=len(set(keys)),
                priority_blocks=len(priority), unique_priority_keys=len(set(priority)),
                mask_bytes_per_block=mask_bytes, key_bytes_per_block=key_bytes,
                visible_priority_mask_and_keys_bytes=len(priority) * (mask_bytes + key_bytes),
                cell_visits_per_block=round(cells / len(keys), 2))
        result['planes'].append(stats)
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('prefixes', nargs='+', type=Path)
    ap.add_argument('--tiles', type=Path, default=Path('sh_src/tiles.bin'))
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--bank-offset', type=lambda s: int(s, 0), default=0x3095,
                    help='bank request byte offset in WRAM dump; current Altered Beast default0x3095')
    ap.add_argument('--pages', type=int, choices=range(1, 17), default=13,
                    help='valid physical pages; current port shadow13, full native RAM16')
    args = ap.parse_args()
    art = args.tiles.read_bytes()
    if len(art) != 1048576:
        raise ValueError('expected16K decoded8x8tiles')
    keys = {s: dict(art=set(), mask=set()) for s in (2, 4, 8)}
    if not 0 <= args.bank_offset < 16384:
        ap.error('--bank-offset outside WRAM dump')
    samples = [census(prefix, art, keys, args.bank_offset, args.pages) for prefix in args.prefixes]
    report = dict(scope='sampled visible source blocks, not runtime/keep/dirty/cache timings; art footprint excludes headers and palette-map variants',
                  tiles=dict(path=str(args.tiles), sha256=hashlib.sha256(art).hexdigest()),
                  descriptor=dict(valid_pages=args.pages, bank_offset=args.bank_offset),
                  samples=samples, union={})
    for side, sets in keys.items():
        report['union'][str(side * 8)] = dict(
            unique_nonblank_art_keys=len(sets['art']),
            unique_fg_priority_keys=len(sets['mask']),
            uncompressed_4bpp_source_block_bytes=len(sets['art']) * side * side * 32,
            uncompressed_fg_mask_and_keys_bytes=len(sets['mask']) * side * side * 10)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report['union'], indent=2))


if __name__ == '__main__':
    main()
