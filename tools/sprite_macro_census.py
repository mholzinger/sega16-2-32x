#!/usr/bin/env python3
"""Size exact painter-order macro sprites from native sprite-table captures.

The tool finds repeated absolute arrangements of one sprite key, composes each in
record order, then separates the final visible pixels into disjoint logical-
palette layers.  Drawing those layers cannot change painter order because no
two layers own the same final pixel.  The report prices packet bytes and
physical framebuffer stores before any live renderer or mapper work begins.

Example (the two captured Neff smoke intervals)::

  python3 tools/sprite_macro_census.py build/blocklive03god/mame \
    --workset build/port/sh_src/r60_workingset.bin \
    --workset-json build/port/sh_src/r60_workingset.json \
    --key 0x031401d0 --ticks 30660:30810 --ticks 33550:33690

This is an opportunity census, not a speed or parity verdict.
"""
import argparse
import csv
import hashlib
import json
import re
import struct
import zlib
from collections import Counter
from pathlib import Path

import s16art


def number(text):
    return int(text, 0)


def align(value, boundary=4):
    return (value + boundary - 1) & -boundary


def tick_ranges(values):
    out = []
    for value in values:
        lo, sep, hi = value.partition(":")
        if not sep:
            raise ValueError("tick range must be LO:HI")
        out.append((number(lo), number(hi)))
    return out


def in_ranges(tick, ranges):
    return not ranges or any(lo <= tick <= hi for lo, hi in ranges)


def sprite_captures(corpus):
    """Yield ``(tick, 1 KiB table)`` from dump files or an ares watch CSV.

    The CSV form keeps the optimization pipeline tied to the port's actual
    renderer input without materializing tens of thousands of tiny files.  Its
    tick is the emulator frame: repeated game frames are still distinct
    presentations and therefore valid frequency samples.
    """
    if corpus.is_dir():
        for path in sorted(corpus.glob("ev_*_sprite.bin")):
            match = re.fullmatch(r"ev_(\d+)_sprite\.bin", path.name)
            if match:
                blob = path.read_bytes()
                if len(blob) != 1024:
                    raise ValueError(f"sprite table must be 1024 bytes: {path}")
                yield int(match.group(1)), blob
        return
    if corpus.suffix.lower() != ".csv":
        raise ValueError("corpus must be an ev_*_sprite.bin directory or ares watch CSV")
    with corpus.open(newline="") as source:
        rows = csv.DictReader(source)
        columns = [name for name in (rows.fieldnames or [])
                   if name.lower().startswith("sdram:") and
                   name.lower().endswith(":0x400")]
        if "frame" not in (rows.fieldnames or []) or len(columns) != 1:
            raise ValueError("watch CSV needs frame and one sdram:*:0x400 column")
        previous = -1
        for row in rows:
            tick = int(row["frame"])
            if tick <= previous:
                raise ValueError("watch CSV frames must increase strictly")
            previous = tick
            try:
                blob = bytes.fromhex(row[columns[0]])
            except ValueError as error:
                raise ValueError(f"invalid sprite-table hex at frame {tick}") from error
            if len(blob) != 1024:
                raise ValueError(f"sprite table at frame {tick} is not 1024 bytes")
            yield tick, blob


def zoom_packets(page, metadata):
    at = metadata["zb1_offset"]
    magic, count, declared, reserved = struct.unpack_from(">4sIII", page, at)
    if magic != b"ZB1\0" or reserved or at + declared > len(page):
        raise ValueError("invalid bounded ZB1 atlas")
    out = {}
    for index in range(count):
        key, zoom, offset = struct.unpack_from(">III", page, at + 16 + index * 12)
        frame_at = at + offset
        pose_width = struct.unpack_from(">H", page, frame_at + 2)[0]
        rows, row_words, end = s16art.unpack_frame(page, frame_at)
        if end > at + declared:
            raise ValueError("ZB1 packet exceeds declared atlas")
        # unpack_frame returns stored whole-word coverage.  Preserve the raw
        # scaler's exact negative pixel extent instead: clipping is chosen from
        # the pose-wide header, while shifted carry stores depend on each exact
        # row end (including zero extension or a partial final word).
        cursor = frame_at + 4
        exact_rows, encoded_rows = [], []
        for row in rows:
            first, groups, encoded = struct.unpack_from(">BBH", page, cursor)
            values = struct.unpack_from(f">{groups}I", page, cursor + 4) if groups else ()
            cursor += 4 + groups * 4
            if not encoded & 0x8000:
                raise ValueError("ZB1 row lacks a negative pixel extent")
            length = 0x10000 - encoded
            if first * 4 > length:
                raise ValueError("ZB1 row starts beyond its exact extent")
            exact_rows.append((row + [0] * max(0, length - len(row)))[:length])
            encoded_rows.append((first, values, length))
        rows = exact_rows
        out[key, zoom] = (rows, row_words, encoded_rows, pose_width)
    return out


def digest(blob):
    return hashlib.sha256(blob).hexdigest()


def unscaled_packets(page, metadata, workset_path, global_root):
    """Resolve the workset AB1 index, including its immutable global chunks."""
    chunks = []
    for item in metadata.get("global_chunk_inputs", []):
        original = Path(item["path"])
        candidates = ([global_root / original] if global_root else []) + [
            original, workset_path.parent / original.name]
        path = next((candidate for candidate in candidates if candidate.is_file()), None)
        if path is None:
            raise ValueError(f"cannot find global AB1 chunk {original}")
        blob = path.read_bytes()
        if len(blob) != item["bytes"] or digest(blob) != item["sha256"]:
            raise ValueError(f"global AB1 chunk differs from workset input: {path}")
        chunks.append((item["address"], blob))

    def packet_at(address):
        page_address = metadata["page_address"]
        if page_address <= address < page_address + len(page):
            return page, address - page_address
        for base, blob in chunks:
            if base <= address < base + len(blob):
                return blob, address - base
        raise ValueError(f"AB1 address {address:#x} is outside declared inputs")

    at = metadata["ab1_offset"]
    magic, count, _declared, reserved = struct.unpack_from(">4sIII", page, at)
    if magic != b"AB1\0" or reserved or count > (len(page) - at - 16) // 8:
        raise ValueError("invalid bounded AB1 index")
    out = {}
    for index in range(count):
        key, address = struct.unpack_from(">II", page, at + 16 + index * 8)
        blob, offset = packet_at(address)
        rows, row_words, _end = s16art.unpack_frame(blob, offset)
        out[key, 0] = (rows, row_words)
    return out


def live_runs(blob, target, poses):
    """Return target records without ever crossing another sprite record."""
    runs, current = [], []
    for slot in range(64):
        words = struct.unpack_from(">8H", blob, slot * 16)
        if words[2] & 0x8000:
            break
        d2 = words[2]
        key = (((d2 & 0x100) << 20) |
               (((words[4] >> 8) & 7) << 24) |
               ((d2 & 255) << 16) | words[3])
        zoom = words[5] & 0x3ff
        if key == target and (key, zoom) in poses:
            current.append(((words[1] & 511) - 184, words[0] & 255,
                            words[0] >> 8, zoom, words[4] & 63))
        else:
            if current:
                runs.append(current)
                current = []
    if current:
        runs.append(current)
    return runs


def signature(records):
    x0 = min(record[0] for record in records)
    y0 = min(record[1] for record in records)
    return tuple((x - x0, y - y0, bottom - y, zoom, palette)
                 for x, y, bottom, zoom, palette in records)


def stored_words(row, x):
    """Physical nonzero longwords emitted by the shifted baked-row writer."""
    return set(emitted_words(row, x))


def emitted_words(row, x):
    """Map each physical framebuffer store to all four bytes it overwrites.

    A nonzero source group causes one aligned 32-bit store after shifting.
    Zero bytes inside that word still overwrite earlier sprite bytes, so macro
    ownership must be assigned per store, not merely per visible pen.
    """
    words = {}
    for pixel, pen in enumerate(row):
        screen_x = x + pixel
        if not 0 <= screen_x < 320:
            continue
        word = screen_x >> 2
        words.setdefault(word, [0, 0, 0, 0])[screen_x & 3] = pen
    return {word: tuple(pens) for word, pens in words.items() if any(pens)}


def runtime_rowpix_words(row, x, zoom, pose_width=None):
    """Return the original renderer's four-pixel accounting units.

    The normal SH-2 copier charges nonzero source groups, even when shifting
    one group produces two destination stores.  The right-clipped scaled C
    helper instead charges destination stores when the zoom phase is nonzero.
    This is bookkeeping, deliberately separate from physical-store pricing.
    """
    if not 0 <= x < 320:
        return 0
    visible = row
    if x + (pose_width if pose_width is not None else len(row)) > 320:
        remaining = 320 - x
        phase = zoom & 31
        seed = (phase & 15) * 4
        words = (64 * (remaining - 1) + seed) // (4 * (64 - phase)) + 1
        logical = min(len(row), 4 * words - ((seed + 4 * words * phase) >> 6))
        visible = row[:min(logical, remaining)]
        if phase:
            return len(emitted_words(visible, x))
    return sum(any(visible[start:start + 4])
               for start in range(0, len(visible), 4))


def runtime_encoded_rowpix_words(encoded_row, x, zoom, pose_width):
    """Mirror the clipped ZB1 helper's accounting from its wire row.

    Decoded pixels are sufficient for visible-surface composition, but they do
    not retain a sparse row's stored group interval.  In a shifted clipped row
    that interval decides whether the copier charges its final carry word.
    """
    first, values, length = encoded_row
    if not 0 <= x < 320:
        return 0
    if not zoom or x + pose_width <= 320:
        return sum(value != 0 for value in values)
    phase = zoom & 31
    remaining = 320 - x
    seed = (phase & 15) * 4
    words = (64 * (remaining - 1) + seed) // (4 * (64 - phase)) + 1
    cut = 4 * words - ((seed + 4 * words * phase) >> 6)
    byte_limit = min(length, cut, remaining)
    total_groups = (byte_limit + 3) // 4
    end = min(first + len(values), total_groups)
    process = max(0, end - first)
    shift = (x & 3) * 8
    carry = stores = 0
    for group, original in enumerate(values[:process]):
        value = original
        if first + group + 1 == total_groups:
            tail = byte_limit & 3
            if tail == 1:
                value &= 0xFF000000
            elif tail == 2:
                value &= 0xFFFF0000
            elif tail == 3:
                value &= 0xFFFFFF00
        if shift:
            out = carry | (value >> shift)
            carry = (value << (32 - shift)) & 0xFFFFFFFF
        else:
            out, carry = value, 0
        stores += (out != 0) if phase else (value != 0)
    if phase and carry and (x & ~3) + 4 * end < 320:
        stores += 1
    return stores


def opacity_runs(rows):
    """Count maximal full-opaque and mixed four-pen runs in packet rows."""
    result = Counter(rows=len(rows))
    for row in rows:
        kinds = [all(row[group:group + 4])
                 for group in range(0, len(row), 4)]
        run = longest = 0
        for kind in kinds:
            run = run + 1 if kind else 0
            longest = max(longest, run)
        result["groups"] += len(kinds)
        result["opaque_groups"] += sum(kinds)
        result["nonempty_groups"] += sum(
            any(row[group:group + 4]) for group in range(0, len(row), 4))
        result["rows_fully_opaque"] += bool(kinds) and all(kinds)
        result["longest_opaque_groups"] += longest
        for threshold in (4, 6, 8):
            if longest >= threshold:
                result[f"longest_opaque_groups_ge{threshold}"] += longest
                result[f"rows_longest_opaque_ge{threshold}"] += 1
        result["rows_with_opaque"] += any(kinds)
        result["rows_single_opaque_run"] += bool(kinds) and sum(
            kind and (index == 0 or not kinds[index - 1])
            for index, kind in enumerate(kinds)) == 1
        previous = None
        for kind in kinds:
            if kind != previous:
                result["opaque_runs" if kind else "mixed_runs"] += 1
                previous = kind
    return result


def record_opacity(records, key, poses):
    """Measure only source rows the clipped records can actually draw."""
    result = Counter(records=len(records))
    for _x, y, bottom, zoom, _palette in records:
        rows = poses[key, zoom][0]
        visible = rows[:max(0, min(bottom, 224) - y)]
        result.update(opacity_runs(visible))
    return result


def pack_sparse(rows):
    """Pack u16 run-count/u16 width plus {y,group,count,0,data} runs."""
    encoded = []
    width = 0
    for y, row in enumerate(rows):
        occupied = sorted({pixel >> 2 for pixel, pen in enumerate(row) if pen})
        first = previous = None
        for group in occupied + [None]:
            if first is not None and (group is None or group != previous + 1):
                count = previous - first + 1
                if y > 255 or first > 255 or count > 255:
                    raise ValueError("sparse macro run exceeds byte fields")
                pixels = list(row[first * 4:(previous + 1) * 4])
                pixels += [0] * (count * 4 - len(pixels))
                encoded.append(struct.pack(">4B", y, first, count, 0) +
                               bytes(pixels))
                width = max(width, (previous + 1) * 4)
                first = None
            if group is not None and first is None:
                first = group
            previous = group
    if len(encoded) > 65535 or width > 65535:
        raise ValueError("sparse macro packet exceeds header fields")
    return struct.pack(">HH", len(encoded), width) + b"".join(encoded)


def pack_blocks(rows, size=32):
    """Return immutable nonempty block packets, keyed only by their pen bytes."""
    packets, references = {}, []
    width = max(map(len, rows), default=0)
    for by in range(0, len(rows), size):
        for bx in range(0, width, size):
            block = [row[bx:bx + size] for row in rows[by:by + size]]
            if not any(any(row) for row in block):
                continue
            block += [[] for _ in range(size - len(block))]
            row_words = [0 if not any(row) else
                         (max(i for i, pen in enumerate(row) if pen) + 4) // 4
                         for row in block]
            packet = s16art.pack_frame(block, row_words)
            key = digest(packet)
            packets[key] = packet
            references.append((bx, by, key))
    return packets, references


def compose(records, key, poses):
    x0 = min(record[0] for record in records)
    y0 = min(record[1] for record in records)
    x1 = max(x + max((len(row) for row in poses[key, zoom][0]), default=0)
             for x, _y, _bottom, zoom, _palette in records)
    y1 = max(min(bottom, 224) for _x, _y, bottom, _zoom, _palette in records)
    width, height = x1 - x0, y1 - y0
    owner = [[None] * width for _ in range(height)]
    pens = [[0] * width for _ in range(height)]
    scalar_stores = 0
    for x, y, bottom, zoom, palette in records:
        rows = poses[key, zoom][0]
        for relative, row in enumerate(rows[:max(0, min(bottom, 224) - y)]):
            scalar_stores += len(stored_words(row, x))
            yy = y + relative - y0
            if not 0 <= yy < height:
                continue
            for pixel, pen in enumerate(row):
                xx = x + pixel - x0
                if pen and 0 <= xx < width:
                    owner[yy][xx] = palette
                    pens[yy][xx] = pen
    layer_bytes = []
    sparse_layer_bytes = []
    deflate_layer_bytes = []
    block_packets, block_refs = {}, []
    sparse_runs = 0
    macro_stores = 0
    palettes = sorted({palette for row in owner for palette in row
                       if palette is not None})
    for palette in palettes:
        rows = [[pens[y][x] if owner[y][x] == palette else 0
                 for x in range(width)] for y in range(height)]
        row_words = [0 if not any(row) else
                     (max(i for i, pen in enumerate(row) if pen) + 4) // 4
                     for row in rows]
        packet = s16art.pack_frame(rows, row_words)
        decoded, _words, end = s16art.unpack_frame(packet, 0)
        if end != len(packet):
            raise AssertionError("macro packet has trailing bytes")
        for y, row in enumerate(decoded):
            for x, pen in enumerate(row):
                expected = (pens[y][x]
                            if x < width and owner[y][x] == palette else 0)
                if pen != expected:
                    raise AssertionError("macro packet round trip changed a pixel")
        layer_bytes.append(len(packet))
        sparse_packet = pack_sparse(rows)
        sparse_layer_bytes.append(len(sparse_packet))
        sparse_runs += struct.unpack_from(">H", sparse_packet)[0]
        deflate_layer_bytes.append(len(zlib.compress(sparse_packet, 9)))
        packets, references = pack_blocks(rows)
        block_packets.update(packets)
        block_refs.extend((palette, bx, by, packet_key)
                          for bx, by, packet_key in references)
        macro_stores += sum(len(stored_words(row, x0)) for row in rows)
    return dict(width=width, height=height, layers=len(palettes),
                layer_bytes=layer_bytes, bytes=sum(layer_bytes),
                sparse_layer_bytes=sparse_layer_bytes,
                sparse_bytes=sum(sparse_layer_bytes), sparse_runs=sparse_runs,
                deflate_layer_bytes=deflate_layer_bytes,
                deflate_bytes=sum(deflate_layer_bytes),
                block32_packet_bytes=sum(map(len, block_packets.values())),
                block32_refs=len(block_refs), _block32_packets=block_packets,
                _block32_refs=block_refs,
                scalar_stores=scalar_stores, macro_stores=macro_stores,
                saved_stores=scalar_stores - macro_stores)


def owner_signature_fingerprint(records):
    """Cheap prefilter; live admission must still compare every descriptor."""
    value = len(records)
    for x, y, bottom, zoom, palette in records:
        for field in (x, y, bottom - y, zoom, palette):
            value = ((value << 5) | (value >> 27)) & 0xffffffff
            value ^= field & 0xffff
    return value


def runtime_record_metadata(record, key, poses):
    """Bake ROWFAST's relative box width and per-row aligned span ends.

    Live admission requires a four-pixel-aligned canvas, so these values remain
    valid after adding the runtime canvas offset.  Keeping them in OP1 avoids
    decoding the immutable AB1/ZB1 source twice merely to reproduce scalar
    clear bookkeeping.
    """
    x, y, bottom, zoom, _palette = record
    height = max(0, min(bottom, 224) - y)
    if not height or not 0 <= x < 320:
        raise ValueError("owner metadata record is outside the visible canvas")
    pose = poses[key, zoom]
    rows, row_words = pose[:2]
    if height > len(rows) or height > len(row_words):
        raise ValueError("owner metadata exceeds its immutable pose")
    ends, max_x = [], x
    if zoom:
        if len(pose) < 4 or height > len(pose[2]):
            raise ValueError("scaled owner metadata lacks encoded row extents")
        encoded_rows, pose_width = pose[2], pose[3]
        phase = zoom & 31
        remaining = 320 - x
        seed = (phase & 15) * 4
        words = (64 * (remaining - 1) + seed) // (4 * (64 - phase)) + 1
        cut = 4 * words - ((seed + 4 * words * phase) >> 6)
        clipped = x + pose_width > 320
        logical_rows = [min(encoded_rows[row][2], cut)
                        if clipped else encoded_rows[row][2]
                        for row in range(height)]
    else:
        capacity = (320 - x + 3) // 4
        logical_rows = [4 * min(row_words[row], capacity)
                        for row in range(height)]
    start = x & ~3
    for logical in logical_rows:
        end = min(320, x + logical)
        groups = ((end + 3) & ~3) // 4 - start // 4
        if not 0 < groups <= 255:
            raise ValueError("owner metadata span exceeds its byte field")
        ends.append(groups)
        max_x = max(max_x, end)
    width = max_x - x
    if not 0 < width <= 0xffff:
        raise ValueError("owner metadata box exceeds its word field")
    return struct.pack('>HBB', width, height, 0) + bytes(ends)


def build_owner_pattern(records, key, poses):
    """Build one exact OP1 owner-pattern body and return it with metrics.

    A palette-only composition is sufficient for a pixel census, but it cannot
    reproduce the foreground-priority punch that follows every original sprite
    record.  This form assigns each final target pixel to the last target record
    whose nonzero byte wrote it.  The 32X overwrite image skips zero bytes per
    lane; they do not erase an earlier sprite merely because another lane in
    the same 32-bit store is nonzero.  A live renderer can therefore draw owner
    N's packet and run record N's unchanged punch before advancing to N+1.

    The returned OP1 body is self-contained:

    - 16-byte pattern header;
    - one 24-byte signature/packet descriptor per original record;
    - four-byte-aligned AB1-style owner packets and scalar row metadata.

    MC1's page-level 32-byte header and 12-byte directory entry are accounted by
    :func:`owner_budget_rows`, not duplicated in every pattern.  The directory
    fingerprint is only a prefilter; the absolute descriptors are the
    collision-free admission identity.
    """
    y0 = min(record[1] for record in records)
    y1 = max(min(bottom, 224) for _x, _y, bottom, _zoom, _palette in records)
    height = y1 - y0
    owners = [[[None, None, None, None] for _ in range(80)]
              for _ in range(height)]
    pens = [[[0, 0, 0, 0] for _ in range(80)] for _ in range(height)]
    scalar_stores = 0
    record_pixels = []
    for owner, (x, y, bottom, zoom, _palette) in enumerate(records):
        pose = poses[key, zoom]
        rows = pose[0]
        pose_width = pose[3] if zoom and len(pose) > 3 else \
            max(map(len, rows), default=0)
        pixels = 0
        for relative, row in enumerate(rows[:max(0, min(bottom, 224) - y)]):
            stores = emitted_words(row, x)
            scalar_stores += len(stores)
            if zoom and len(pose) > 2:
                pixels += runtime_encoded_rowpix_words(
                    pose[2][relative], x, zoom, pose_width) * 4
            else:
                pixels += runtime_rowpix_words(row, x, zoom, pose_width) * 4
            yy = y + relative - y0
            if not 0 <= yy < height:
                continue
            for word, word_pens in stores.items():
                for byte, pen in enumerate(word_pens):
                    if pen:
                        owners[yy][word][byte] = owner
                        pens[yy][word][byte] = pen
        record_pixels.append(pixels)

    packets = []
    packet_descriptors = []
    owner_stores = 0
    visible_owners = 0
    reconstructed_owner = [[[None, None, None, None] for _ in range(80)]
                           for _ in range(height)]
    reconstructed_pens = [[[0, 0, 0, 0] for _ in range(80)]
                          for _ in range(height)]
    for owner, (record_x, record_y, _bottom, _zoom, _palette) in enumerate(records):
        owned = [(word, y) for y in range(height) for word in range(80)
                 if owner in owners[y][word]]
        if not owned:
            packets.append(b'')
            packet_descriptors.append((0, 0))
            continue
        visible_owners += 1
        left = min(word for word, _y in owned)
        right = max(word for word, _y in owned) + 1
        top = min(y for _word, y in owned)
        bottom = max(y for _word, y in owned) + 1
        rows = []
        for y in range(top, bottom):
            packed = []
            for word in range(left, right):
                packed.extend(pen if owners[y][word][byte] == owner else 0
                              for byte, pen in enumerate(pens[y][word]))
            rows.append(packed)
        row_words = [0 if not any(row) else
                     (max(i for i, pen in enumerate(row) if pen) + 4) // 4
                     for row in rows]
        packet = s16art.pack_frame(rows, row_words)
        decoded, _words, end = s16art.unpack_frame(packet, 0)
        if end != len(packet) or any(
                pen != (rows[y][x] if x < len(rows[y]) else 0)
                for y, row in enumerate(decoded)
                for x, pen in enumerate(row)):
            raise AssertionError("owner macro packet round trip changed a pixel")
        for yy, row in enumerate(decoded):
            for word, word_pens in emitted_words(row, left * 4).items():
                gy = top + yy
                for byte, pen in enumerate(word_pens):
                    if not pen:
                        continue
                    if (owners[gy][word][byte] != owner or
                            pens[gy][word][byte] != pen or
                            reconstructed_owner[gy][word][byte] is not None):
                        raise AssertionError("owner macro packet changed final pixel ownership")
                    reconstructed_owner[gy][word][byte] = owner
                    reconstructed_pens[gy][word][byte] = pen
        draw_x = left * 4
        owner_stores += sum(len(stored_words(row, draw_x)) for row in rows)
        # These bounds are the signed-16 fields used by the proposed MC1
        # descriptor.  Validate them here so the census cannot price an
        # arrangement that the live format could not encode.
        draw_dx, draw_dy = draw_x - record_x, y0 + top - record_y
        if not -32768 <= draw_dx <= 32767 or not -32768 <= draw_dy <= 32767:
            raise ValueError("owner packet crop offset exceeds MC1 descriptor")
        packets.append(packet)
        packet_descriptors.append((draw_dx, draw_dy))

    if reconstructed_owner != owners or reconstructed_pens != pens:
        raise AssertionError("owner macro stores differ from scalar target surface")

    body = bytearray(16 + 24 * len(records))
    metadata_bytes = 0
    for index, ((x, y, bottom, zoom, palette), packet,
                (draw_dx, draw_dy)) in enumerate(
                    zip(records, packets, packet_descriptors)):
        if not (-32768 <= x <= 32767 and -32768 <= y <= 32767 and
                0 < bottom - y <= 255 and 0 <= zoom <= 0x3ff and
                0 <= palette <= 0x3f and record_pixels[index] <= 0xffff):
            raise ValueError("owner signature exceeds OP1 descriptor")
        if packet:
            body += bytes(align(len(body)) - len(body))
            packet_offset, packet_size = len(body), len(packet)
            body += packet
        else:
            packet_offset = packet_size = 0
        metadata = runtime_record_metadata((x, y, bottom, zoom, palette),
                                           key, poses)
        body += bytes(align(len(body)) - len(body))
        metadata_offset = len(body)
        body += metadata
        body += bytes(align(len(body)) - len(body))
        if metadata_offset // 4 > 0xffff:
            raise ValueError("owner metadata offset exceeds OP1 descriptor")
        metadata_bytes += len(metadata)
        struct.pack_into('>hhHBBhhIIHH', body, 16 + index * 24,
                         x, y, zoom, bottom - y, palette,
                         draw_dx, draw_dy, packet_offset, packet_size,
                         record_pixels[index], metadata_offset // 4)
    fingerprint = owner_signature_fingerprint(records)
    struct.pack_into('>4sHHII', body, 0, b'OP1\0', 4, len(records),
                     len(body), fingerprint)
    metrics = dict(owner_bytes=len(body),
                owner_packet_bytes=sum(map(len, packets)),
                owner_metadata_bytes=metadata_bytes,
                owner_descriptor_bytes=16 + 24 * len(records),
                owner_visible_records=visible_owners,
                owner_macro_stores=owner_stores,
                owner_saved_stores=scalar_stores - owner_stores)
    return bytes(body), metrics


def compose_owner(records, key, poses):
    """Price the live-safe owner pattern while keeping JSON binary-free."""
    _blob, metrics = build_owner_pattern(records, key, poses)
    return metrics


def budget_rows(patterns, budgets, size_key="bytes"):
    ranked = sorted((row for row in patterns if row["saved_stores"] > 0), key=lambda row:
                    (row["frequency"] * row["saved_stores"]) /
                    max(1, row[size_key]), reverse=True)
    out = []
    total_value = sum(row["frequency"] * row["saved_stores"] for row in ranked)
    for budget in budgets:
        used = frames = saved = count = 0
        for row in ranked:
            if used + row[size_key] > budget:
                continue
            used += row[size_key]
            frames += row["frequency"]
            saved += row["frequency"] * row["saved_stores"]
            count += 1
        out.append(dict(budget=budget, used=used, patterns=count,
                        captured_frames=frames, weighted_saved_stores=saved,
                        saved_store_coverage_percent=100.0 * saved / max(1, total_value)))
    return out


def owner_budget_rows(patterns, budgets):
    """Select owner-safe MC1 patterns with all container bytes included."""
    candidates = [row for row in patterns if row["owner_saved_stores"] > 0]
    ranked = sorted(candidates, key=lambda row:
                    (row["frequency"] * row["owner_saved_stores"]) /
                    max(1, row["owner_bytes"] + 12), reverse=True)
    total_value = sum(row["frequency"] * row["owner_saved_stores"]
                      for row in ranked)
    out = []
    for budget in budgets:
        # MC1 fixed header.  Every admitted pattern adds one 12-byte directory
        # entry plus its complete self-contained body.
        used = 32
        frames = saved = count = 0
        for row in ranked:
            added = 12 + row["owner_bytes"]
            if used + added > budget:
                continue
            used += added
            frames += row["frequency"]
            saved += row["frequency"] * row["owner_saved_stores"]
            count += 1
        out.append(dict(budget=budget, used=used, patterns=count,
                        captured_frames=frames, weighted_saved_stores=saved,
                        saved_store_coverage_percent=100.0 * saved /
                        max(1, total_value)))
    return out


def block_budget_rows(patterns, budgets):
    """Greedy value/added-byte selection with exact shared-block accounting."""
    out = []
    total_value = sum(row["frequency"] * row["saved_stores"]
                      for row in patterns if row["saved_stores"] > 0)
    for budget in budgets:
        remaining = [row for row in patterns if row["saved_stores"] > 0]
        used_keys, used, frames, saved = set(), 0, 0, 0
        while remaining:
            choices = []
            for row in remaining:
                descriptor = 16 + 10 * row["records"] + 8 * row["block32_refs"]
                packets = row["_block32_packets"]
                added = descriptor + sum(len(packets[key]) for key in packets
                                         if key not in used_keys)
                value = row["frequency"] * row["saved_stores"]
                if used + added <= budget:
                    choices.append((value / max(1, added), value, -added, row))
            if not choices:
                break
            row = max(choices, key=lambda choice: choice[:3])[3]
            descriptor = 16 + 10 * row["records"] + 8 * row["block32_refs"]
            new_keys = set(row["_block32_packets"]) - used_keys
            added = descriptor + sum(len(row["_block32_packets"][key])
                                     for key in new_keys)
            used += added
            used_keys.update(new_keys)
            frames += row["frequency"]
            saved += row["frequency"] * row["saved_stores"]
            remaining.remove(row)
        out.append(dict(budget=budget, used=used,
                        patterns=sum(1 for row in patterns if row not in remaining and
                                     row["saved_stores"] > 0),
                        unique_blocks=len(used_keys), captured_frames=frames,
                        weighted_saved_stores=saved,
                        saved_store_coverage_percent=100.0 * saved / max(1, total_value)))
    return out


def census(args):
    page = args.workset.read_bytes()
    metadata = json.loads(args.workset_json.read_text())
    poses = zoom_packets(page, metadata)
    poses.update(unscaled_packets(page, metadata, args.workset,
                                  args.global_root))
    ranges = tick_ranges(args.ticks)
    patterns = {}
    capture_count = run_count = split_capture_count = 0
    source_opacity = Counter()
    for tick, table in sprite_captures(args.corpus):
        if not in_ranges(tick, ranges):
            continue
        runs = live_runs(table, args.key, poses)
        if not runs:
            continue
        capture_count += 1
        run_count += len(runs)
        split_capture_count += len(runs) > 1
        for records in runs:
            source_opacity.update(record_opacity(records, args.key, poses))
            sig = signature(records)
            if sig not in patterns:
                patterns[sig] = dict(frequency=0, first_tick=tick,
                                     records=len(records), data=records)
            patterns[sig]["frequency"] += 1
    rows = []
    for pattern in patterns.values():
        records = pattern.pop("data")
        result = compose(records, args.key, poses)
        owner_result = compose_owner(records, args.key, poses)
        rows.append({**pattern, **result, **owner_result})
    rows.sort(key=lambda row: (-row["frequency"], row["first_tick"]))
    scalar = sum(row["frequency"] * row["scalar_stores"] for row in rows)
    macro = sum(row["frequency"] * row["macro_stores"] for row in rows)
    block_budgets = block_budget_rows(rows, args.budget)
    unique_blocks = {}
    for row in rows:
        unique_blocks.update(row["_block32_packets"])
    for row in rows:
        row.pop("_block32_packets")
        row.pop("_block32_refs")
    opaque_groups = source_opacity["opaque_groups"]
    opaque_runs = source_opacity["opaque_runs"]
    source_opacity_report = dict(source_opacity)
    source_opacity_report.update(
        opaque_group_percent=100.0 * opaque_groups /
        max(1, source_opacity["groups"]),
        opaque_nonempty_group_percent=100.0 * opaque_groups /
        max(1, source_opacity["nonempty_groups"]),
        mean_opaque_run_groups=opaque_groups / max(1, opaque_runs),
        longest_run_opaque_coverage_percent=100.0 *
        source_opacity["longest_opaque_groups"] / max(1, opaque_groups))
    atlas_opacity = Counter()
    atlas_poses = 0
    for (pose_key, _zoom), pose in poses.items():
        if pose_key == args.key:
            pose_rows = pose[0]
            atlas_poses += 1
            atlas_opacity.update(opacity_runs(pose_rows))
    atlas_opacity_report = dict(atlas_opacity)
    atlas_opacity_report.update(
        poses=atlas_poses,
        row_interval_bytes=4 * atlas_poses + 2 * atlas_opacity["rows"],
        rows_multiple_opaque_runs=(atlas_opacity["rows_with_opaque"] -
                                   atlas_opacity["rows_single_opaque_run"]),
        opaque_nonempty_group_percent=100.0 * atlas_opacity["opaque_groups"] /
        max(1, atlas_opacity["nonempty_groups"]),
        mean_opaque_run_groups=atlas_opacity["opaque_groups"] /
        max(1, atlas_opacity["opaque_runs"]),
        longest_run_opaque_coverage_percent=100.0 *
        atlas_opacity["longest_opaque_groups"] /
        max(1, atlas_opacity["opaque_groups"]))
    return dict(key=f"{args.key:08x}", captures=capture_count,
                contiguous_runs=run_count,
                captures_with_multiple_runs=split_capture_count,
                patterns=len(rows), record_counts=dict(Counter(
                    row["records"] for row in rows)),
                unique_packet_bytes=sum(row["bytes"] for row in rows),
                weighted_scalar_stores=scalar, weighted_macro_stores=macro,
                weighted_saved_stores=scalar - macro,
                weighted_store_reduction_percent=100.0 * (scalar - macro) / max(1, scalar),
                budgets=budget_rows(rows, args.budget),
                owner_budgets=owner_budget_rows(rows, args.budget),
                sparse_budgets=budget_rows(rows, args.budget, "sparse_bytes"),
                deflate_budgets=budget_rows(rows, args.budget, "deflate_bytes"),
                block32_unique_packets=len(unique_blocks),
                block32_unique_packet_bytes=sum(map(len, unique_blocks.values())),
                block32_budgets=block_budgets,
                source_opacity=source_opacity_report,
                atlas_opacity=atlas_opacity_report,
                patterns_detail=rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("corpus", type=Path)
    parser.add_argument("--workset", type=Path, required=True)
    parser.add_argument("--workset-json", type=Path, required=True)
    parser.add_argument("--global-root", type=Path,
                        help="root for global AB1 chunk paths in workset metadata")
    parser.add_argument("--key", type=number, required=True)
    parser.add_argument("--ticks", action="append", default=[])
    parser.add_argument("--budget", type=number, action="append",
                        default=[0x40000, 0x80000, 0x100000, 0x180000])
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    result = census(args)
    text = json.dumps(result, indent=2, sort_keys=True) + "\n"
    print(text, end="")
    if args.json:
        args.json.write_text(text)


if __name__ == "__main__":
    main()
