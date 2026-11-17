#!/usr/bin/env python3
"""Pack owner-safe macro-sprite patterns into a bounded MC1 blob.

Every OP1 pattern retains original 32-bit-store ownership, so a future live
caller can preserve ``owner stores N -> foreground punch N`` painter order.  The
directory fingerprint is only a prefilter; each pattern carries the complete
absolute record signature for collision-free admission.

This tool only builds and verifies immutable data.  It does not install a live
renderer or claim a cadence improvement.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

import s16art
import sprite_macro_census as macro
from bake_art import keyword


def number(value):
    return int(value, 0)


def raw_unscaled_pose(sprites_path, manifest_path, target_key):
    """Decode one immutable unscaled pose without transient artbake files."""
    raw = sprites_path.read_bytes()
    if len(raw) & 1:
        raise ValueError("raw sprite ROM has an odd byte count")
    words = struct.unpack(f'>{len(raw) // 2}H', raw)
    manifest = json.loads(manifest_path.read_text())
    matches = []
    for entry in manifest.get('entries', []):
        bank, address, pitch, flip = entry['key']
        if keyword((bank, address, pitch, flip)) != target_key:
            continue
        rows, _width, row_words = s16art.decode(
            words, bank, address, pitch, flip, entry['height'])
        matches.append((rows, row_words))
    if len(matches) != 1:
        raise ValueError("raw sprite manifest must identify one target pose")
    return matches[0]


def verify(blob):
    """Validate all MC1/OP1 bounds and embedded packet decodes."""
    if len(blob) < 32:
        raise ValueError("MC1 header is truncated")
    (magic, version, count, target, directory_at, body_at, total,
     asset_at, asset_bytes) = struct.unpack_from('>4sHHIIIIII', blob)
    if (magic != b'MC1\0' or version not in (1, 2) or not count or not target or
            directory_at != 32 or body_at != 32 + count * 12 or
            total != len(blob) or
            (version == 1 and (asset_at or asset_bytes)) or
            (version == 2 and (asset_at < body_at or asset_at & 3 or
                               not asset_bytes or asset_at + asset_bytes != total))):
        raise ValueError("invalid MC1 header")
    ranges = []
    previous_fingerprint = -1
    for index in range(count):
        fingerprint, offset, size = struct.unpack_from(
            '>III', blob, directory_at + index * 12)
        if version == 2 and fingerprint < previous_fingerprint:
            raise ValueError("MC1 v2 directory is not fingerprint-sorted")
        previous_fingerprint = fingerprint
        pattern_limit = asset_at if version == 2 else len(blob)
        if offset < body_at or size < 16 or offset + size > pattern_limit:
            raise ValueError("OP1 directory entry exceeds MC1")
        if offset & 3:
            raise ValueError("OP1 pattern is not four-byte aligned")
        ranges.append((offset, offset + size))
        magic, op_version, records, declared, actual_fingerprint = \
            struct.unpack_from('>4sHHII', blob, offset)
        if (magic != b'OP1\0' or op_version != (4 if version == 1 else 5) or
                not records or
                declared != size or actual_fingerprint != fingerprint or
                16 + records * 24 > size):
            raise ValueError("invalid OP1 header")
        payload_floor = 16 + records * 24
        for record in range(records):
            descriptor = struct.unpack_from(
                '>hhHBBhhIIHH', blob, offset + 16 + record * 24)
            (_x, _y, _zoom, height, _palette, _draw_x, _draw_y,
             packet_at, packet_bytes, pixels, metadata_quads) = descriptor
            metadata_at = metadata_quads * 4
            if not height or pixels & 3:
                raise ValueError("OP1 record has zero source height")
            if not metadata_quads:
                raise ValueError("OP1 scalar metadata offset is zero")
            if bool(packet_at) != bool(packet_bytes):
                raise ValueError("OP1 empty packet has inconsistent bounds")
            if packet_bytes:
                if (packet_at & 3 or
                        (version == 1 and
                         (packet_at < payload_floor or
                          packet_at + packet_bytes > size)) or
                        (version == 2 and
                         (packet_at < asset_at or
                          packet_at + packet_bytes > len(blob)))):
                    raise ValueError("OP1 packet exceeds pattern")
                packet_base = offset + packet_at if version == 1 else packet_at
                _rows, _words, end = s16art.unpack_frame(blob, packet_base)
                if end != packet_base + packet_bytes:
                    raise ValueError("OP1 packet length differs from descriptor")
                if version == 1:
                    payload_floor = (packet_at + packet_bytes + 3) & ~3
            metadata_base = (offset + metadata_at if version == 1
                             else metadata_at)
            if (metadata_at & 3 or
                    (version == 1 and metadata_at < payload_floor) or
                    (version == 1 and metadata_at + 4 + height > size) or
                    (version == 2 and metadata_at < asset_at) or
                    metadata_base + 4 + height > len(blob)):
                raise ValueError("OP1 scalar metadata exceeds pattern")
            box_width, rows, reserved = struct.unpack_from(
                '>HBB', blob, metadata_base)
            ends = blob[metadata_base + 4:metadata_base + 4 + height]
            if (not box_width or rows != height or reserved or
                    len(ends) != height or not all(ends)):
                raise ValueError("OP1 scalar metadata is invalid")
            if version == 1:
                payload_floor = (metadata_at + 4 + height + 3) & ~3
    for left, right in zip(sorted(ranges), sorted(ranges)[1:]):
        if left[1] > right[0]:
            raise ValueError("OP1 patterns overlap")
    return count


def pack_patterns(patterns, key, poses, budget, min_saved_stores=0):
    """Select and serialize patterns under the exact page-art budget."""
    candidates = []
    for pattern in patterns:
        body, metrics = macro.build_owner_pattern(pattern['records'], key, poses)
        row = {**pattern, **metrics, 'body': body}
        if row['owner_saved_stores'] >= max(1, min_saved_stores):
            candidates.append(row)
    candidates.sort(key=lambda row:
                    (row['frequency'] * row['owner_saved_stores']) /
                    max(1, len(row['body']) + 12), reverse=True)
    selected, used = [], 32
    for row in candidates:
        added = 12 + len(row['body'])
        if used + added <= budget:
            selected.append(row)
            used += added
    if not selected:
        raise ValueError("budget admits no owner-safe patterns")

    def serialize(rows):
        blob = bytearray(32 + 12 * len(rows))
        for index, row in enumerate(rows):
            blob += bytes(macro.align(len(blob)) - len(blob))
            offset = len(blob)
            blob += row['body']
            fingerprint = struct.unpack_from('>I', row['body'], 12)[0]
            struct.pack_into('>III', blob, 32 + index * 12,
                             fingerprint, offset, len(row['body']))
        struct.pack_into('>4sHHIIIIII', blob, 0, b'MC1\0', 1, len(rows),
                         key, 32, 32 + 12 * len(rows), len(blob), 0, 0)
        return blob

    blob = serialize(selected)
    while len(blob) > budget and selected:
        selected.pop()
        if selected:
            blob = serialize(selected)
    if not selected:
        raise ValueError("budget admits no owner-safe patterns and row metadata")
    verify(blob)
    report = dict(format='sprite-owner-macros-v5', key=f'{key:08x}',
                  budget=budget, bytes=len(blob), patterns=len(selected),
                  min_saved_stores=min_saved_stores,
                  record_metadata_bytes=sum(row['owner_metadata_bytes']
                                            for row in selected),
                  captured_frames=sum(row['frequency'] for row in selected),
                  weighted_saved_stores=sum(row['frequency'] *
                                            row['owner_saved_stores']
                                            for row in selected),
                  selected=[dict(first_tick=row['first_tick'],
                                 frequency=row['frequency'],
                                 records=len(row['records']),
                                 bytes=len(row['body']),
                                 owner_saved_stores=row['owner_saved_stores'])
                            for row in selected])
    return bytes(blob), report


def pack_patterns_shared(patterns, key, poses, budget, min_saved_stores=0):
    """Pack OP1 v5 descriptors against one immutable cross-pattern pool."""
    candidates = []
    for pattern in patterns:
        body, metrics = macro.build_owner_pattern(pattern['records'], key, poses)
        if metrics['owner_saved_stores'] < max(1, min_saved_stores):
            continue
        records, assets = [], set()
        for index in range(len(pattern['records'])):
            fields = struct.unpack_from('>hhHBBhhIIHH', body,
                                        16 + index * 24)
            packet_at, packet_bytes, height = fields[7], fields[8], fields[3]
            metadata_at = fields[10] * 4
            packet = body[packet_at:packet_at + packet_bytes] if packet_bytes else b''
            metadata = body[metadata_at:metadata_at + 4 + height]
            packet_key = ('p', packet) if packet else None
            metadata_key = ('m', metadata)
            if packet_key:
                assets.add(packet_key)
            assets.add(metadata_key)
            records.append((fields, packet_key, metadata_key))
        candidates.append({**pattern, **metrics, 'records_v5': records,
                           'assets': assets,
                           'pattern_bytes': 16 + len(records) * 24,
                           'fingerprint': struct.unpack_from('>I', body, 12)[0]})

    selected, selected_ids, pooled, used = [], set(), set(), 32
    while True:
        best = None
        for index, row in enumerate(candidates):
            if index in selected_ids:
                continue
            added = 12 + row['pattern_bytes'] + sum(
                (len(asset[1]) + 3) & ~3 for asset in row['assets'] - pooled)
            if used + added > budget:
                continue
            score = row['frequency'] * row['owner_saved_stores'] / added
            choice = (score, -added, -index, index, added, row)
            if best is None or choice[:3] > best[:3]:
                best = choice
        if best is None:
            break
        index, added, row = best[3:]
        selected.append(row)
        selected_ids.add(index)
        pooled.update(row['assets'])
        used += added
    if not selected:
        raise ValueError("budget admits no shared owner-safe patterns")

    # MC1 v2 directories are fingerprint-sorted so target admission can use a
    # bounded lower-bound search.  Equal fingerprints stay in selection order
    # and still receive the complete collision-proof record comparison.
    selected.sort(key=lambda row: row['fingerprint'])

    body_at = 32 + 12 * len(selected)
    pattern_offsets, cursor = [], body_at
    for row in selected:
        pattern_offsets.append(cursor)
        cursor += row['pattern_bytes']
    asset_at = (cursor + 3) & ~3
    asset_offsets, asset_order = {}, []
    cursor = asset_at
    for row in selected:
        for _fields, packet_key, metadata_key in row['records_v5']:
            for asset in (packet_key, metadata_key):
                if asset is None or asset in asset_offsets:
                    continue
                asset_offsets[asset] = cursor
                asset_order.append(asset)
                cursor += (len(asset[1]) + 3) & ~3
    if cursor > budget or cursor > 0x40000:
        raise ValueError("shared asset offsets exceed the bounded MC1 format")

    blob = bytearray(cursor)
    for index, (row, offset) in enumerate(zip(selected, pattern_offsets)):
        fingerprint = row['fingerprint']
        size = row['pattern_bytes']
        struct.pack_into('>III', blob, 32 + index * 12,
                         fingerprint, offset, size)
        struct.pack_into('>4sHHII', blob, offset, b'OP1\0', 5,
                         len(row['records_v5']), size, fingerprint)
        for record, (fields, packet_key, metadata_key) in enumerate(
                row['records_v5']):
            values = list(fields)
            values[7] = asset_offsets[packet_key] if packet_key else 0
            values[10] = asset_offsets[metadata_key] // 4
            struct.pack_into('>hhHBBhhIIHH', blob,
                             offset + 16 + record * 24, *values)
    for asset in asset_order:
        offset = asset_offsets[asset]
        blob[offset:offset + len(asset[1])] = asset[1]
    struct.pack_into('>4sHHIIIIII', blob, 0, b'MC1\0', 2, len(selected),
                     key, 32, body_at, len(blob), asset_at,
                     len(blob) - asset_at)
    verify(blob)
    report = dict(format='sprite-owner-macros-v6-shared', key=f'{key:08x}',
                  budget=budget, bytes=len(blob), patterns=len(selected),
                  min_saved_stores=min_saved_stores,
                  shared_assets=len(asset_order),
                  shared_asset_bytes=len(blob) - asset_at,
                  captured_frames=sum(row['frequency'] for row in selected),
                  weighted_saved_stores=sum(row['frequency'] *
                                            row['owner_saved_stores']
                                            for row in selected),
                  selected=[dict(first_tick=row['first_tick'],
                                 frequency=row['frequency'],
                                 records=len(row['records']),
                                 bytes=row['pattern_bytes'],
                                 owner_saved_stores=row['owner_saved_stores'])
                            for row in selected])
    return bytes(blob), report


def collect(args, poses):
    patterns = {}
    ranges = macro.tick_ranges(args.ticks)
    for tick, table in macro.sprite_captures(args.corpus):
        if not macro.in_ranges(tick, ranges):
            continue
        for records in macro.live_runs(table, args.key, poses):
            signature = tuple((x, y, bottom - y, zoom, palette)
                              for x, y, bottom, zoom, palette in records)
            if signature not in patterns:
                patterns[signature] = dict(frequency=0, first_tick=tick,
                                           records=records)
            patterns[signature]['frequency'] += 1
    return list(patterns.values())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('corpus', type=Path)
    parser.add_argument('--workset', type=Path, required=True)
    parser.add_argument('--workset-json', type=Path, required=True)
    parser.add_argument('--global-root', type=Path)
    parser.add_argument('--raw-sprites', type=Path,
                        help='immutable raw sprite ROM for the target zoom-0 pose')
    parser.add_argument('--raw-manifest', type=Path,
                        help='manifest describing the target zoom-0 raw pose')
    parser.add_argument('--key', type=number, required=True)
    parser.add_argument('--ticks', action='append', default=[])
    parser.add_argument('--budget', type=number, required=True)
    parser.add_argument('--min-saved-stores', type=number, default=0,
                        help='exclude patterns whose one-frame store saving is too small to repay admission')
    parser.add_argument('--shared-assets', action='store_true',
                        help='emit MC1 v2 with one deduplicated cross-pattern asset pool')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    try:
        page = args.workset.read_bytes()
        metadata = json.loads(args.workset_json.read_text())
        poses = macro.zoom_packets(page, metadata)
        if bool(args.raw_sprites) != bool(args.raw_manifest):
            raise ValueError('--raw-sprites and --raw-manifest are a pair')
        if args.raw_sprites:
            poses[args.key, 0] = raw_unscaled_pose(
                args.raw_sprites, args.raw_manifest, args.key)
        else:
            poses.update(macro.unscaled_packets(page, metadata, args.workset,
                                                args.global_root))
        inputs = {path: path.read_bytes() for path in
                  (args.workset, args.workset_json)}
        if args.raw_sprites:
            inputs[args.raw_sprites] = args.raw_sprites.read_bytes()
            inputs[args.raw_manifest] = args.raw_manifest.read_bytes()
        patterns = collect(args, poses)
        pack = pack_patterns_shared if args.shared_assets else pack_patterns
        blob, report = pack(patterns, args.key, poses, args.budget,
                            args.min_saved_stores)
        report['sha256'] = hashlib.sha256(blob).hexdigest()
        for path, raw in inputs.items():
            if path.read_bytes() != raw:
                raise ValueError(f'input changed during pack: {path}')
        if args.out.resolve() == args.report.resolve():
            raise ValueError('binary and report outputs must differ')
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_bytes(blob)
        args.report.write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
        if args.out.read_bytes() != blob or verify(args.out.read_bytes()) != report['patterns']:
            raise ValueError('MC1 output readback differs')
        print(f"owner macros: {report['patterns']} patterns, "
              f"{report['captured_frames']} captured frames, {len(blob)} bytes")
    except (ValueError, AssertionError, OSError, KeyError, IndexError,
            struct.error) as error:
        parser.exit(2, f'pack_sprite_macros: {error}\n')


if __name__ == '__main__':
    main()
