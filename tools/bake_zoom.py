#!/usr/bin/env python3
"""Bake observed S16B scaled poses into a bounded, palette-independent atlas.

ZB1: 16-byte header (magic, count, bytes, reserved), sorted 12-byte entries
(native key, exact 10-bit zoom, blob-relative offset), then group packets.
Unlike AB1, row extent is a NEGATIVE signed 16-bit pixel count. Padding remains
transparent. The frame width bounds every row's end, including transparent pens.
Runtime refuses clipping and falls back to the native scaler for absent poses.
Input dumps and generated art are private, ignored build inputs/outputs.
--packed writes ZP1: identical headers, two pens per byte, row payload padded
to four bytes. It retains the RAW-budget selection to reclaim art space rather
than silently fill the savings with additional poses.
"""
import argparse
import collections
import json
import struct
from pathlib import Path
from bake_art import keyword
from s16art import decode_scaled, load_rom, pack_frame, unpack_frame


def pack(rows):
    if any(not 0 < len(row) < 32768 for row in rows):
        raise ValueError('scaled pixel extent is not representable')
    blob = bytearray(pack_frame(rows, [(-len(row)) & 65535 for row in rows]))
    struct.pack_into('>H', blob, 2, max((len(r)+3)&~3 for r in rows))
    return bytes(blob)


def compact_packet(raw):
    height = struct.unpack_from('>H', raw)[0]
    out = bytearray(raw[:4])
    at = 4
    for _ in range(height):
        hdr = raw[at:at+4]
        groups = hdr[1]
        at += 4
        pixels = raw[at:at+groups*4]
        if any(p > 14 for p in pixels):
            raise ValueError('packed baked pen must be0..14')
        payload = bytes((a << 4) | b for a, b in zip(pixels[::2], pixels[1::2]))
        out += hdr + payload + bytes((-len(payload)) & 3)
        at += groups*4
    if at != len(raw):
        raise ValueError('raw packet has trailing or missing bytes')
    return bytes(out)


def unpack_compact(blob, offset):
    height, _ = struct.unpack_from('>HH', blob, offset)
    offset += 4
    rows, ends = [], []
    for _ in range(height):
        first, groups, extent = struct.unpack_from('>BBH', blob, offset)
        offset += 4
        payload = blob[offset:offset+groups*2]
        row = [0]*(first*4)
        for value in payload:
            row.extend((value >> 4, value & 15))
        rows.append(row); ends.append(extent)
        offset += (groups*2+3) & ~3
    return rows, ends, offset


def verify(blob, words):
    magic, count, size, reserved = struct.unpack_from('>4sIII', blob)
    assert magic in (b'ZB1\0', b'ZP1\0') and size == len(blob) and reserved == 0
    previous = (-1, -1)
    for i in range(count):
        key, zoom, offset = struct.unpack_from('>III', blob, 16+12*i)
        assert previous < (key, zoom) and 0 < zoom < 1024
        previous = key, zoom
        assert offset >= 16+12*count and offset % 4 == 0
        height, width = struct.unpack_from('>HH', blob, offset)
        pitch = (key >> 16) & 255
        ref, _, _ = decode_scaled(words, (key >> 24) & 7, key & 65535,
                                  pitch-256 if pitch > 127 else pitch,
                                  (key >> 28) & 1, height, zoom & 31, zoom >> 5)
        rows, ends, end = (unpack_compact if magic == b'ZP1\0' else unpack_frame)(blob, offset)
        assert end <= len(blob) and width == max((len(r)+3)&~3 for r in ref)
        assert ends == [(-len(r)) & 65535 for r in ref]
        for a, b in zip(rows, ref):
            n = max(len(a), len(b))
            assert a+[0]*(n-len(a)) == b+[0]*(n-len(b))
    return count


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--rom', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--budget', type=int, default=49152)
    ap.add_argument('--report', type=Path)
    ap.add_argument('--packed', action='store_true')
    ap.add_argument('runs', nargs='+', help='name=directory containing spr.bin')
    a = ap.parse_args()
    words = load_rom(a.rom)
    count, heights = collections.Counter(), {}
    for run in a.runs:
        data = (Path(run.split('=', 1)[1])/'spr.bin').read_bytes()
        if len(data) % 1024:
            raise ValueError('truncated sprite table corpus')
        for start in range(0, len(data), 1024):
            for slot in range(64):
                e = struct.unpack_from('>8H', data, start+16*slot)
                if e[2] & 32768:
                    break
                top, bottom = e[0] & 255, min(e[0] >> 8, 224)
                zoom = e[5] & 1023
                if e[2] & 16384 or top >= bottom or not zoom:
                    continue
                pitch = e[2] & 255
                key = ((e[4] >> 8) & 7, e[3], pitch-256 if pitch > 127 else pitch,
                       int(bool(e[2] & 256)), zoom & 31, zoom >> 5)
                count[key] += 1
                heights[key] = max(heights.get(key, 0), bottom-top)
    candidates = []
    for key, uses in count.items():
        bank, addr, pitch, flip, hz, vz = key
        rows, nw, _ = decode_scaled(words, bank, addr, pitch, flip, heights[key], hz, vz)
        try:
            body = pack(rows)
        except ValueError:
            continue
        candidates.append((uses*nw/(len(body)+12), key, body, uses*nw))
    chosen, used = [], 16
    for _, key, body, weight in sorted(candidates, reverse=True):
        if used+12+len(body) <= a.budget:
            chosen.append((key, body, weight))
            used += 12+len(body)
    chosen.sort(key=lambda v: (keyword(v[0][:4]), v[0][4] | v[0][5] << 5))
    selection_bytes = used
    if a.packed:
        chosen = [(key, compact_packet(body), weight) for key, body, weight in chosen]
        used = 16 + 12*len(chosen) + sum(len(body) for _, body, _ in chosen)
    blob = bytearray(struct.pack('>4sIII', b'ZP1\0' if a.packed else b'ZB1\0', len(chosen), used, 0)+bytes(12*len(chosen)))
    for i, (key, body, _) in enumerate(chosen):
        struct.pack_into('>III', blob, 16+12*i, keyword(key[:4]), key[4] | key[5] << 5, len(blob))
        blob += body
    assert len(blob) == used <= a.budget
    verified = verify(blob, words)
    a.out.write_bytes(blob)
    report = dict(observed_keys=len(count), selected_keys=verified, bytes=len(blob),
                  budget=a.budget, raw_selection_bytes=selection_bytes, packed=a.packed,
                  selected=[dict(key=list(k), bytes=len(b), weight=w) for k,b,w in chosen],
                  scope='observed corpus, bounded atlas; not runtime coverage or speed proof')
    if a.report:
        a.report.write_text(json.dumps(report, indent=2)+'\n')
    print(f'zoom bake: {verified}/{len(count)} observed keys, {len(blob)}/{a.budget} bytes; all serialized entries re-decoded exactly')


if __name__ == '__main__':
    main()
