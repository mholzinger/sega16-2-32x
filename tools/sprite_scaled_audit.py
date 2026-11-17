#!/usr/bin/env python3
"""Check a scaled S16B sprite reference against native MAME RGB after FG/text cuts.

This is a reference-decoder/observed-atlas census, not a port pixel, cadence or
performance proof. RGB cannot distinguish equal-colored pens. Priority0/1 and
shadow records are rejected. Tile-bank mapping is supplied by the caller.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
import numpy as np
from s16art import decode_scaled, load_rom
from sprite_pixel_audit import native_image_check
from tile_block_census import tile_at, physical_code


def reference(table, words):
    if len(table) not in (1024, 2048):
        raise ValueError('expected a 1/2 KiB native sprite table')
    pens = np.full((224, 320), -1, dtype=np.int16)
    owners = np.full((224, 320), -1, dtype=np.int16)
    records, candidates = [], []
    for slot in range(len(table) // 16):
        e = struct.unpack_from('>8H', table, slot * 16)
        if e[2] & 0x8000:
            break
        top, bottom = e[0] & 255, e[0] >> 8
        if e[2] & 0x4000 or top >= bottom:
            continue
        if e[4] & 63 == 63 or ((e[4] >> 6) & 3) < 2:
            raise ValueError(f'slot{slot}: unsupported shadow/low priority')
        x = (e[1] & 511) - 184
        bank, addr = (e[4] >> 8) & 7, e[3]
        pitch, flip = (e[2] & 127) - (e[2] & 128), bool(e[2] & 256)
        hz, vz = e[5] & 31, (e[5] >> 5) & 31
        rows, source_words, _ = decode_scaled(words, bank, addr, pitch, flip,
                                              bottom - top, hz, vz)
        emitted, box = 0, None
        body_bytes = 4
        for r, row in enumerate(rows):
            opaque = [i for i, p in enumerate(row) if p]
            groups = (opaque[-1] // 4 - opaque[0] // 4 + 1) if opaque else 0
            body_bytes += 4 + groups * 4
            y = top + r
            if y >= 224:
                continue
            for i in opaque:
                col = x + i
                if not 0 <= col < 320:
                    continue
                pens[y, col] = (e[4] & 63) * 16 + row[i]
                owners[y, col] = slot
                emitted += 1
                box = [col, y, col + 1, y + 1] if box is None else [
                    min(box[0], col), min(box[1], y), max(box[2], col + 1), max(box[3], y + 1)]
        records.append(dict(slot=slot, words=list(e[:6]), box=box,
                            emitted_pixels=emitted, zoom=e[5] & 1023,
                            priority=(e[4] >> 6) & 3))
        if hz or vz:
            candidates.append(dict(key=[bank, addr, pitch, int(flip), hz, vz],
                                   height=bottom-top, body_bytes=body_bytes,
                                   source_words=source_words,
                                   uniform_even=hz == vz and hz % 2 == 0))
    return pens, owners, records, candidates


def audit(prefix, words, art, tile_bank):
    read = lambda name: Path(str(prefix) + '_' + name + '.bin').read_bytes()
    pens, owners, records, candidates = reference(read('sprite'), words)
    tilemap = struct.unpack('>32768H', read('tilemap'))
    text = struct.unpack('>2048H', read('text'))
    psel, xs, ys = text[0x740], (192 - text[0x74c]) & 1023, text[0x748] & 511
    for y in range(224):
        for x in range(320):
            w = tile_at(tilemap, psel, (x + xs) // 8, (y + ys) // 8, valid_pages=16)
            foreground = bool(w & 32768 and w & 8191 and
                              art[physical_code(w, tile_bank)*64 + (y+ys)%8*8 + (x+xs)%8] & 7)
            t = text[y//8*64 + 24 + x//8]
            glyph = bool(t & 0xfff and art[(t & 511)*64 + y%8*8 + x%8] & 7)
            if foreground or glyph:
                owners[y, x] = -1
    checks = native_image_check(pens, owners, records, read('palette'), prefix.with_suffix('.png'))
    emitted = sum(r['emitted_pixels'] for r in records)
    # Count unique source-sprite pixels before FG/text cuts separately from RGB visibility.
    return dict(prefix=str(prefix), tile_bank=tile_bank, records=records,
                checks=checks, visible=sum(r['visible_pixels'] for r in checks),
                different_visible_rgb_pixels=sum(r['visible_pixels']-r['rgb_equal'] for r in checks),
                clipped_source_opaque_pixels=emitted, candidates=candidates,
                inputs={str(Path(str(prefix)+'_'+n+'.bin')): hashlib.sha256(read(n)).hexdigest()
                        for n in ('sprite', 'tilemap', 'text', 'palette', 'wram')},
                image_sha256=hashlib.sha256(prefix.with_suffix('.png').read_bytes()).hexdigest())


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('prefixes', nargs='+', type=Path)
    ap.add_argument('--rom', type=Path, required=True)
    ap.add_argument('--tiles', type=Path, required=True)
    bank = ap.add_mutually_exclusive_group(required=True)
    bank.add_argument('--tile-bank', type=int)
    bank.add_argument('--tile-bank-offset', type=lambda s: int(s, 0))
    ap.add_argument('--out', type=Path, required=True)
    a = ap.parse_args()
    words, art = load_rom(a.rom), a.tiles.read_bytes()
    frames, atlas = [], {}
    for p in a.prefixes:
        b = a.tile_bank
        if b is None:
            b = Path(str(p)+'_wram.bin').read_bytes()[a.tile_bank_offset] & 7
        frame = audit(p, words, art, b)
        frames.append(frame)
        for c in frame['candidates']:
            key = tuple(c['key'])
            if key not in atlas or c['height'] > atlas[key]['height']:
                atlas[key] = c
    result = dict(scope=__doc__.strip(), frames=frames,
                  visible_pixels=sum(f['visible'] for f in frames),
                  different_visible_rgb_pixels=sum(f['different_visible_rgb_pixels'] for f in frames),
                  observed_zoom_keys=len(atlas),
                  estimated_group_packet_body_bytes=sum(c['body_bytes'] for c in atlas.values()),
                  atlas=list(atlas.values()),
                  storage_scope='observed keys only; index/mapper allocation excluded; pixel-end packet extension required; not a whole-game size or runtime hit rate')
    a.out.write_text(json.dumps(result, indent=2)+'\n')
    print({k: v for k, v in result.items() if k not in ('frames', 'atlas')})
    if not result['visible_pixels'] or result['different_visible_rgb_pixels']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
