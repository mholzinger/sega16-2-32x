#!/usr/bin/env python3
"""Extend a raw ZB1 atlas without ranking, dropping or changing existing packets.

Manifest: {"entries":[{"key":[bank,address,signed_pitch,flip],"zoom":1,"height":2}]}.
Zoom is the exact native ten-bit value. All additions must be new identities.
Use --empty-base to build directly from explicit poses and raw sprites, or
--base-atlas to preserve and extend existing packets.
This is an offline, palette-independent System16 sprite format operation;
game phases, ROM placement and working-set selection belong to the caller.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct

from bake_art import keyword, unkeyword
from bake_zoom import pack, verify
from s16art import decode_scaled


EMPTY_BASE = struct.pack('>4sIII', b'ZB1\0', 0, 16, 0)


def integer(value, low, high, label):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f'{label} must be an integer in {low}..{high}')
    return value


def raw_packets(blob):
    """Validate bounds independently before any decoder follows packet offsets."""
    if len(blob) < 16 or len(blob) & 3:
        raise ValueError('raw ZB1 atlas must have an aligned complete header')
    magic, count, size, reserved = struct.unpack_from('>4sIII', blob)
    if magic != b'ZB1\0' or size != len(blob) or reserved or count > (size-16)//12:
        raise ValueError('invalid raw ZB1 header/index extent')
    packets, intervals, previous = {}, [], (-1, -1)
    for i in range(count):
        key, zoom, offset = struct.unpack_from('>III', blob, 16+12*i)
        identity = key, zoom
        if keyword(unkeyword(key)) != key or not 0 < zoom < 1024 or identity <= previous:
            raise ValueError('atlas identities must be valid, unique and sorted')
        previous = identity
        if offset & 3 or offset < 16+12*count or offset > size-4:
            raise ValueError('packet offset outside aligned atlas body')
        height, width = struct.unpack_from('>HH', blob, offset)
        if not 0 < height <= 224 or not 0 < width <= 320 or width & 3:
            raise ValueError('packet dimensions exceed raw runtime bounds')
        at = offset+4
        for _ in range(height):
            if at > size-4:
                raise ValueError('truncated packet row header')
            first, groups, negative_end = struct.unpack_from('>BBH', blob, at)
            end = (-negative_end) & 65535
            if not 0 < end <= width or (first+groups)*4 > ((end+3)&~3):
                raise ValueError('invalid negative pixel extent or row groups')
            at += 4
            if groups > (size-at)//4:
                raise ValueError('truncated packet row payload')
            if any(p > 14 for p in blob[at:at+groups*4]):
                raise ValueError('raw packet pens must be 0..14')
            at += groups*4
        intervals.append((offset, at))
        packets[identity] = blob[offset:at]
    intervals.sort()
    if any(a[1] > b[0] for a, b in zip(intervals, intervals[1:])):
        raise ValueError('overlapping atlas packet bodies')
    return packets


def additions(manifest):
    if not isinstance(manifest, dict) or not isinstance(manifest.get('entries'), list):
        raise ValueError('manifest must contain an entries list')
    result = {}
    for entry in manifest['entries']:
        if not isinstance(entry, dict) or set(entry) != {'key', 'zoom', 'height'}:
            raise ValueError('each addition needs exactly key, zoom and height')
        key = entry['key']
        if not isinstance(key, list) or len(key) != 4:
            raise ValueError('key must be [bank,address,signed_pitch,flip]')
        key = tuple(integer(v, lo, hi, label) for v, lo, hi, label in zip(
            key, (0, 0, -128, 0), (7, 65535, 127, 1), ('bank', 'address', 'pitch', 'flip')))
        identity = keyword(key), integer(entry['zoom'], 1, 1023, 'zoom')
        if identity in result:
            raise ValueError('duplicate addition identity')
        result[identity] = integer(entry['height'], 1, 224, 'height')
    return result


def checked(blob, words):
    packets = raw_packets(blob)
    try:
        verify(blob, words)
    except (AssertionError, IndexError, struct.error) as exc:
        raise ValueError('atlas pixels/row extents disagree with raw sprite ROM') from exc
    return packets


def extend(base, manifest, words, budget):
    integer(budget, 16, 0xffffffff, 'budget')
    packets = checked(base, words)
    original = dict(packets)
    entries = additions(manifest)
    for identity, height in entries.items():
        if identity in packets:
            raise ValueError('addition already exists in base atlas; existing packets are immutable')
        key, zoom = identity
        rows, _, _ = decode_scaled(words, *unkeyword(key), height, zoom & 31, zoom >> 5)
        packets[identity] = pack(rows)
    size = 16+12*len(packets)+sum(map(len, packets.values()))
    if size > budget:
        raise ValueError(f'atlas needs {size} bytes, exceeds budget {budget}; no entries dropped')
    blob = bytearray(struct.pack('>4sIII', b'ZB1\0', len(packets), size, 0)+bytes(12*len(packets)))
    for i, ((key, zoom), body) in enumerate(sorted(packets.items())):
        struct.pack_into('>III', blob, 16+12*i, key, zoom, len(blob))
        blob += body
    result = checked(blob, words)
    if any(result[key] != body for key, body in original.items()):
        raise ValueError('base packet preservation failed')
    report = dict(format='ZB1', base_entries=len(original), added_entries=len(entries),
                  entries=len(result), bytes=len(blob), budget=budget,
                  base_packet_bytes_preserved=True, verified_against_raw=True,
                  sha256=hashlib.sha256(blob).hexdigest(),
                  added=[dict(key=list(unkeyword(k)), zoom=z, height=entries[k, z],
                              bytes=len(result[k, z])+12) for k, z in sorted(entries)])
    return bytes(blob), report


def reject_aliases(inputs, outputs):
    for i, output in enumerate(outputs):
        for other in inputs+outputs[:i]:
            if output.resolve() == other.resolve() or (
                    output.exists() and other.exists() and os.path.samefile(output, other)):
                raise ValueError(f'output aliases an input or another output: {output}')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    base = ap.add_mutually_exclusive_group(required=True)
    base.add_argument('--base-atlas', type=Path)
    base.add_argument('--empty-base', action='store_true',
                      help='start with the canonical 16-byte empty raw ZB1 header')
    ap.add_argument('--manifest', type=Path, required=True)
    ap.add_argument('--sprites', type=Path, required=True)
    ap.add_argument('--budget', type=lambda s: int(s, 0), required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--report', type=Path)
    args = ap.parse_args()
    try:
        inputs = ([args.base_atlas] if args.base_atlas else []) + [args.manifest, args.sprites]
        reject_aliases(inputs, [args.out]+([args.report] if args.report else []))
        payloads = [p.read_bytes() for p in inputs]
        words = struct.unpack('>%dH' % (len(payloads[-1]) // 2), payloads[-1])
        blob, report = extend(EMPTY_BASE if args.empty_base else payloads[0],
                              json.loads(payloads[-2]), words, args.budget)
        report['inputs'] = {str(p): hashlib.sha256(data).hexdigest()
                            for p, data in zip(inputs, payloads)}
        if args.empty_base:
            report['base_source'] = dict(kind='canonical-empty-ZB1', bytes=len(EMPTY_BASE),
                                         sha256=hashlib.sha256(EMPTY_BASE).hexdigest())
        args.out.write_bytes(blob)
        if args.report:
            args.report.write_text(json.dumps(report, indent=2)+'\n')
    except (OSError, ValueError, struct.error) as exc:
        ap.exit(1, f'extend_zoom_atlas: {exc}\n')
    print(f'zoom extension: preserved {report["base_entries"]}, added {report["added_entries"]}; '
          f'{len(blob)}/{args.budget} bytes; all packets raw-verified')


if __name__ == '__main__':
    main()
