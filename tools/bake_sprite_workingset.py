#!/usr/bin/env python3
"""Build one music/AB1/ZB1 mapper page without repacking accepted global art.

Inputs are game-independent sprite descriptors and existing packet formats.
The output is a PAGE, never a modified base ROM. Every requested unscaled pose
must fit; selection/ranking and game-phase detection belong to the caller.

--global-index-json accepts {"entries":[{"key":u32,"address":u32,
"packet_hex":"..."}]} instead of extracting entries from --base-rom's AB1.
Fresh builds can use --global-index-bin artbake_0.bin and repeated
--global-chunk ADDR:FILE instead, without depending on a linked base ROM.
--code-bytes reserves a 64-byte-aligned FF-only region immediately before the
existing guard; no executable bytes are inserted by this tool.
--unscaled-manifest accepts {"entries":[{"key":[bank,addr,pitch,flip],
"height":n}]} (or that entries list alone). --corpus takes a spr.bin file or
directory; its live unscaled records are merged at their maximum height.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

import s16art
import pack_sprite_macros
import pack_sprite_transitions
from bake_art import keyword, unkeyword
from bake_zoom import verify as verify_zoom


def number(value):
    return int(value, 0) if isinstance(value, str) else int(value)


def align(value, boundary=4):
    return (value + boundary - 1) & -boundary


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def packet(data, offset=0, limit=None):
    """Bounded AB1/ZB1 group-packet parser, independent of the serializer."""
    limit = len(data) if limit is None else limit
    require(0 <= offset <= limit - 4, 'packet header out of bounds')
    height, width = struct.unpack_from('>HH', data, offset)
    require(0 < height <= 224, 'packet height must be 1..224')
    at = offset + 4
    for _ in range(height):
        require(at <= limit - 4, 'row header out of bounds')
        groups = data[at + 1]
        at += 4
        require(at + 4 * groups <= limit, 'row payload out of bounds')
        at += 4 * groups
    return bytes(data[offset:at]), height, width


def build_opaque_run_sidecar(atlas, threshold=4):
    """Build one longest fully opaque group interval for every ZB1 row."""
    require(1 <= threshold <= 255, 'opaque run threshold must fit one byte')
    count = struct.unpack_from('>I', atlas, 4)[0]
    sidecar = bytearray(16 + count * 4)
    struct.pack_into('>4sIII', sidecar, 0, b'OR1\0', count, 0, threshold)
    for index in range(count):
        packet_at = struct.unpack_from('>I', atlas, 24 + index * 12)[0]
        height = struct.unpack_from('>H', atlas, packet_at)[0]
        at = packet_at + 4
        body = bytearray(struct.pack('>HH', height, 0))
        for _ in range(height):
            groups = atlas[at + 1]
            at += 4
            best_start = best_count = run_start = run_count = 0
            for group in range(groups):
                pens = atlas[at + group * 4:at + group * 4 + 4]
                require(len(pens) == 4 and all(pen <= 14 for pen in pens),
                        'opaque run input pen must be 0..14')
                if all(pens):
                    if not run_count:
                        run_start = group
                    run_count += 1
                    if run_count > best_count:
                        best_start, best_count = run_start, run_count
                else:
                    run_count = 0
            at += groups * 4
            if best_count < threshold:
                best_start = best_count = 0
            body += bytes((best_start, best_count))
        body += bytes(align(len(body)) - len(body))
        struct.pack_into('>I', sidecar, 16 + index * 4, len(sidecar))
        sidecar += body
    struct.pack_into('>I', sidecar, 8, len(sidecar))
    verify_opaque_run_sidecar(atlas, sidecar)
    return bytes(sidecar)


def verify_opaque_run_sidecar(atlas, sidecar):
    """Independently bind every OR1 interval to its immutable ZB1 row."""
    magic, count, size, threshold = struct.unpack_from('>4sIII', sidecar)
    require(magic == b'OR1\0' and size == len(sidecar),
            'invalid opaque run sidecar header')
    require(count == struct.unpack_from('>I', atlas, 4)[0] and
            1 <= threshold <= 255 and 16 + count * 4 <= size,
            'opaque run sidecar does not match ZB1 index')
    for index in range(count):
        packet_at = struct.unpack_from('>I', atlas, 24 + index * 12)[0]
        height = struct.unpack_from('>H', atlas, packet_at)[0]
        row_at = packet_at + 4
        plan_at = struct.unpack_from('>I', sidecar, 16 + index * 4)[0]
        require(plan_at % 4 == 0 and plan_at <= size - 4,
                'opaque run pose offset out of bounds')
        plan_rows, reserved = struct.unpack_from('>HH', sidecar, plan_at)
        require(plan_rows == height and reserved == 0 and
                plan_at + 4 + 2 * height <= size,
                'opaque run pose rows do not match ZB1 packet')
        plan_at += 4
        for _ in range(height):
            groups = atlas[row_at + 1]
            row_at += 4
            start, run = sidecar[plan_at:plan_at + 2]
            plan_at += 2
            require((run == 0 and start == 0) or
                    (run >= threshold and start + run <= groups),
                    'opaque run lies outside its ZB1 row')
            if run:
                pens = atlas[row_at + start * 4:row_at + (start + run) * 4]
                require(len(pens) == run * 4 and all(pens),
                        'opaque run includes a transparent pen')
            row_at += groups * 4
    return count


def check_unscaled(body, key, words):
    require(keyword(unkeyword(key)) == key, 'noncanonical global sprite key')
    raw, height, _ = packet(body)
    require(len(raw) == len(body), 'unscaled packet has trailing bytes')
    got, counts, end = s16art.unpack_frame(body, 0)
    bank, address, pitch, flip = unkeyword(key)
    expected, _, expected_counts = s16art.decode(words, bank, address, pitch, flip, height)
    for row, count in enumerate(expected_counts):
        start = (address + pitch * (row + 1)) & 65535
        last = words[bank * 65536 + ((start + (-1 if flip else 1) * (count - 1)) & 65535)]
        require((last >> 12 if flip else last & 15) == 15, 'unscaled row lacks directional terminator')
    require(end == len(body) and counts == expected_counts, 'unscaled row extents differ')
    for actual, reference in zip(got, expected):
        width = max(len(actual), len(reference))
        require(actual + [0] * (width - len(actual)) == reference + [0] * (width - len(reference)),
                'unscaled packet pens differ from raw sprite ROM')
    require(body == s16art.pack_frame(expected, expected_counts), 'unscaled packet header differs')
    return height


def load_globals(args, inputs, words):
    globals_ = {}
    chunks = [(address, inputs[path]) for address, path in args.global_chunks]
    previous_end = 0
    for address, raw in sorted(chunks):
        require(address % 4 == 0 and 0 <= address <= 0xffffffff - len(raw), 'invalid global chunk address')
        if raw:
            require(address >= previous_end, 'global chunks overlap')
            previous_end = address + len(raw)
    windows = []
    for spec in args.global_window:
        address, size = (number(v) for v in spec.split(':'))
        require(address % 4 == 0 and size > 0, 'invalid global window')
        windows.append((address, address + size))
    if not windows:
        windows = [(address, address + len(raw)) for address, raw in chunks if raw]
    require(windows, 'supply --global-window or nonempty --global-chunk inputs')
    for address, raw in chunks:
        require(not raw or any(begin <= address and address + len(raw) <= end for begin, end in windows),
                'global chunk outside declared window')

    def read_at(address, extent):
        if chunks:
            for start, raw in chunks:
                if start <= address < start + len(raw):
                    return packet(raw, address - start, min(len(raw), extent - start))[0]
            raise ValueError('global address not provided by chunks')
        require(args.base_rom is not None, 'global packet requires a base ROM or chunk')
        offset = address - args.rom_address_base
        require(0 <= offset < len(inputs[args.base_rom]), 'global address outside base ROM')
        return packet(inputs[args.base_rom], offset,
                      min(len(inputs[args.base_rom]), extent - args.rom_address_base))[0]

    if args.global_index_json:
        doc = json.loads(inputs[args.global_index_json])
        entries = [(number(e['key']), number(e['address']), bytes.fromhex(e['packet_hex']))
                   for e in doc['entries']]
    else:
        if args.global_index_bin:
            require(chunks, '--global-index-bin requires --global-chunk inputs')
            index_data, at = inputs[args.global_index_bin], 0
        else:
            require(args.base_rom is not None and args.index_rom_offset is not None,
                    '--base-rom and --index-rom-offset are required for an AB1 index')
            index_data, at = inputs[args.base_rom], args.index_rom_offset
        require(0 <= at <= len(index_data) - 16, 'base AB1 header out of bounds')
        magic, count, _, reserved = struct.unpack_from('>4sIII', index_data, at)
        require(magic == b'AB1\0' and reserved == 0, 'invalid base AB1 header')
        require(count <= (len(index_data) - at - 16) // 8, 'base AB1 index out of bounds')
        entries, previous = [], -1
        for i in range(count):
            key, address = struct.unpack_from('>II', index_data, at + 16 + 8 * i)
            require(key > previous, 'base AB1 keys must be sorted and unique')
            previous = key
            extent = next((end for begin, end in windows if begin <= address < end), None)
            if extent is None:  # Existing level-local frame: not a reusable global.
                continue
            body = read_at(address, extent)
            entries.append((key, address, body))
    address_bytes = {}
    for key, address, body in entries:
        require(0 <= key <= 0xffffffff and key not in globals_, 'duplicate/invalid global key')
        require(address % 4 == 0 and any(begin <= address and address + len(body) <= end
                                       for begin, end in windows), 'global packet outside declared window')
        require(not (address < args.page_address + args.page_bytes and
                     address + len(body) > args.page_address),
                'global art must not alias the mapped page')
        if args.base_rom is not None:
            offset = address - args.rom_address_base
            require(offset >= 0 and inputs[args.base_rom][offset:offset + len(body)] == body,
                    'global packet bytes differ from accepted base ROM')
        if chunks:
            extent = next(end for begin, end in windows if begin <= address < end)
            require(read_at(address, extent) == body, 'global packet bytes differ from supplied chunk')
        height = check_unscaled(body, key, words)
        for offset, value in enumerate(body):
            previous = address_bytes.setdefault(address + offset, value)
            require(previous == value, 'global packets conflict at the same ROM address')
        globals_[key] = dict(address=address, body=body, height=height)
    require(globals_, 'no accepted global entries found')
    return globals_


def requested_poses(args, inputs):
    poses = {}

    def add(key, height):
        key = tuple(number(v) for v in key)
        require(len(key) == 4, 'unscaled key needs bank,addr,pitch,flip')
        bank, address, pitch, flip = key
        require(0 <= bank < 8 and 0 <= address < 65536 and -128 <= pitch < 128 and flip in (0, 1),
                'invalid unscaled descriptor')
        require(0 < height <= 224, 'unscaled height must be 1..224')
        word = keyword(key)
        poses[word] = max(poses.get(word, 0), height)

    if args.unscaled_manifest:
        document = json.loads(inputs[args.unscaled_manifest])
        for entry in document.get('entries', []) if isinstance(document, dict) else document:
            add(entry['key'], number(entry['height']))
    for path in args.corpus:
        raw = inputs[path]
        require(len(raw) % 1024 == 0, 'sprite corpus must contain complete 1 KiB tables')
        for start in range(0, len(raw), 1024):
            for key, height, zoomed in s16art.records(raw[start:start + 1024]):
                if not zoomed and height > 0:
                    add(key, height)
    return poses


def transition_record_counts(raw):
    """Return exact count pairs plus cheap per-side gates for a TP1 blob."""
    packet_count = struct.unpack_from('>H', raw, 6)[0]
    state_count = struct.unpack_from('>H', raw, 8)[0]
    state_at = struct.unpack_from('>I', raw, 20)[0]
    packet_at = struct.unpack_from('>I', raw, 24)[0]
    counts = []
    fingerprints = []
    at = state_at
    for _ in range(state_count):
        records, size = struct.unpack_from('>HH', raw, at)
        require(records < 64, 'transition record count must fit admission mask')
        counts.append(records)
        value = records
        for record in (0, records - 1):
            for word in (0, 2, 3, 5):
                value = ((value << 5) | (value >> 27)) & 0xffffffff
                value ^= struct.unpack_from('>H', raw,
                                             at + 4 + (record * 6 + word) * 2)[0]
        fingerprints.append(value)
        at += size
    old_mask = new_mask = 0
    pairs = set()
    fingerprint_pairs = set()
    for i in range(packet_count):
        old_state, new_state = struct.unpack_from('>HH', raw, packet_at + i * 24)
        old_mask |= 1 << counts[old_state]
        new_mask |= 1 << counts[new_state]
        pairs.add((counts[old_state], counts[new_state]))
        old_fingerprint = fingerprints[old_state]
        new_fingerprint = fingerprints[new_state]
        fingerprint_pairs.add(old_fingerprint ^
                              (((new_fingerprint << 16) |
                                (new_fingerprint >> 16)) & 0xffffffff))
    return old_mask, new_mask, sorted(pairs), sorted(fingerprint_pairs)


def build(args, inputs):
    require(args.page_bytes > 0 and args.page_bytes % 16 == 0, 'page size must be a positive multiple of 16')
    require(args.page_address % 16 == 0 and 0 <= args.page_address <= 0xffffffff - args.page_bytes,
            'invalid page address')
    require(0 <= args.guard_bytes < args.page_bytes and args.guard_bytes % 4 == 0,
            'guard must be an aligned part of the page')
    code_bytes = getattr(args, 'code_bytes', 0)
    require(type(code_bytes) is int and code_bytes >= 0 and code_bytes % 64 == 0,
            'code capacity must be a nonnegative multiple of 64')
    code_offset = args.page_bytes - args.guard_bytes - code_bytes
    if code_bytes:
        require(code_offset >= 0 and code_offset % 64 == 0 and (args.page_address + code_offset) % 64 == 0,
                'code reservation must fit the page and start on a 64-byte boundary')
    require(len(inputs[args.sprites]) % 2 == 0, 'raw sprite ROM must have whole BE16 words')
    words = struct.unpack('>%dH' % (len(inputs[args.sprites]) // 2), inputs[args.sprites])
    globals_ = load_globals(args, inputs, words)
    poses = requested_poses(args, inputs)
    require(poses, 'no requested unscaled poses')
    atlas = inputs[args.zoom_atlas]
    require(len(atlas) >= 16 and atlas[:4] == b'ZB1\0', 'only an existing raw ZB1 atlas is accepted')
    atlas_count = verify_zoom(atlas, words)  # Independent scaled decode of every serialized entry.
    for i in range(atlas_count):
        offset = struct.unpack_from('>I', atlas, 24 + 12 * i)[0]
        packet(atlas, offset)  # Explicit height and payload bounds as well as pixel checks.
    opaque_runs = (build_opaque_run_sidecar(atlas, args.opaque_runs)
                   if args.opaque_runs else b'')
    transition_packets = getattr(args, 'transition_packets', None)
    transitions = inputs[transition_packets] if transition_packets else b''
    if transitions:
        pack_sprite_transitions.verify(transitions)
        (transition_old_count_mask, transition_new_count_mask,
         transition_count_pairs,
         transition_fingerprint_pairs) = transition_record_counts(transitions)
        transition_target_key = struct.unpack_from('>I', transitions, 16)[0]
    else:
        transition_old_count_mask = transition_new_count_mask = 0
        transition_count_pairs = []
        transition_fingerprint_pairs = []
        transition_target_key = 0
    macro_packets = getattr(args, 'macro_packets', None)
    macros = inputs[macro_packets] if macro_packets else b''
    if macros:
        macro_count = pack_sprite_macros.verify(macros)
        macro_target_key = struct.unpack_from('>I', macros, 8)[0]
        directory_at = struct.unpack_from('>I', macros, 12)[0]
        macro_max_records = max(
            struct.unpack_from('>H', macros,
                               struct.unpack_from('>I', macros,
                                                  directory_at + i * 12 + 4)[0]
                               + 6)[0]
            for i in range(macro_count))
    else:
        macro_count = macro_target_key = macro_max_records = 0
    music = inputs[args.music]
    if args.music_rom_offset is not None:
        require(args.base_rom is not None, '--music-rom-offset requires --base-rom')
        at = args.music_rom_offset
        require(at >= 0 and inputs[args.base_rom][at:at + len(music)] == music,
                'music differs from accepted base ROM')
    index_offset = align(len(music), 16)
    entries = {key: entry['address'] for key, entry in globals_.items()}
    local = {}
    for key, height in poses.items():
        if key in globals_ and globals_[key]['height'] >= height:
            continue
        rows, _, counts = s16art.decode(words, *unkeyword(key), height)
        body = s16art.pack_frame(rows, counts)
        check_unscaled(body, key, words)
        local[key] = body
        entries[key] = 0  # Reserve the index entry before placing any body.
    index_bytes = 16 + len(entries) * 8
    body = bytearray(index_bytes)
    for key in sorted(local):
        require(len(body) % 4 == 0, 'local packet alignment')
        entries[key] = args.page_address + index_offset + len(body)
        body += local[key]
        body += bytes(align(len(body)) - len(body))
    # AB1's bytes field includes referenced global frame bodies, matching bake_art.
    # Shared global pointers are legal; count each physical byte once.
    global_bytes, previous_end = 0, 0
    for start, end in sorted((entry['address'], entry['address'] + len(entry['body']))
                             for entry in globals_.values()):
        global_bytes += max(0, end - max(start, previous_end))
        previous_end = max(previous_end, end)
    struct.pack_into('>4sIII', body, 0, b'AB1\0', len(entries), len(body) + global_bytes, 0)
    for i, key in enumerate(sorted(entries)):
        struct.pack_into('>II', body, 16 + i * 8, key, entries[key])
    atlas_offset = align(index_offset + len(body))
    opaque_runs_offset = align(atlas_offset + len(atlas))
    transition_offset = align(opaque_runs_offset + len(opaque_runs))
    transition_end = transition_offset + len(transitions)
    macro_offset = align(transition_end) if macros else 0
    end = macro_offset + len(macros) if macros else transition_end
    require(end <= code_offset,
            f'working set exceeds page budget by {end - code_offset} bytes')
    page = bytearray(b'\xff' * args.page_bytes)
    page[:len(music)] = music
    page[index_offset:index_offset + len(body)] = body
    page[atlas_offset:atlas_offset + len(atlas)] = atlas
    page[opaque_runs_offset:opaque_runs_offset + len(opaque_runs)] = opaque_runs
    page[transition_offset:transition_end] = transitions
    if macros:
        page[macro_offset:end] = macros

    # Independently walk the serialized AB1 index, resolving global and page-local pointers.
    for i in range(len(entries)):
        key, address = struct.unpack_from('>II', page, index_offset + 16 + i * 8)
        if args.page_address <= address < args.page_address + args.page_bytes:
            offset = address - args.page_address
            require(index_offset + index_bytes <= offset < index_offset + len(body), 'local pointer outside AB1 body')
            encoded, height, _ = packet(page, offset, index_offset + len(body))
        else:
            require(key in globals_ and address == globals_[key]['address'], 'global address changed')
            encoded = globals_[key]['body']
            height = globals_[key]['height']
        check_unscaled(encoded, key, words)
        require(height >= poses.get(key, 0), 'serialized packet shorter than requested pose')
    require(page[:len(music)] == music and
            page[atlas_offset:atlas_offset + len(atlas)] == atlas and
            page[opaque_runs_offset:opaque_runs_offset + len(opaque_runs)] == opaque_runs and
            page[transition_offset:transition_end] == transitions and
            (not macros or page[macro_offset:end] == macros),
            'music or atlas changed during serialization')
    require(page[-args.guard_bytes:] == b'\xff' * args.guard_bytes if args.guard_bytes else True,
            'page guard was modified')
    metadata = dict(format='sprite-working-set-v1', page_address=args.page_address,
                    page_bytes=args.page_bytes, guard_bytes=args.guard_bytes,
                    music_bytes=len(music), music_sha256=digest(music),
                    ab1_offset=index_offset, ab1_address=args.page_address + index_offset,
                    ab1_bytes=len(body), ab1_index_bytes=index_bytes, ab1_entries=len(entries),
                    ab1_declared_bytes=len(body) + global_bytes, global_body_bytes=global_bytes,
                    accepted_global_entries=len(globals_), requested_unscaled_keys=len(poses),
                    local_unscaled_packets=len(local), local_unscaled_bytes=sum(map(len, local.values())),
                    accepted_global_references={f'{key:08x}': entry['address'] for key, entry in sorted(globals_.items())},
                    global_packet_sha256={f'{key:08x}': digest(entry['body']) for key, entry in sorted(globals_.items())},
                    local_overrides_global_keys=[f'{key:08x}' for key in sorted(local) if key in globals_],
                    zb1_offset=atlas_offset, zb1_address=args.page_address + atlas_offset,
                    zb1_bytes=len(atlas), zb1_entries=atlas_count, zb1_sha256=digest(atlas),
                    opaque_runs_offset=opaque_runs_offset if opaque_runs else 0,
                    opaque_runs_address=(args.page_address + opaque_runs_offset
                                         if opaque_runs else 0),
                    opaque_runs_bytes=len(opaque_runs),
                    opaque_runs_threshold=args.opaque_runs or 0,
                    transition_offset=transition_offset if transitions else 0,
                    transition_address=(args.page_address + transition_offset
                                        if transitions else 0),
                    transition_bytes=len(transitions),
                    transition_sha256=digest(transitions) if transitions else '',
                    used_page_bytes=end, free_before_guard_bytes=args.page_bytes - args.guard_bytes - end,
                    page_sha256=digest(page), input_sha256={str(path): digest(raw) for path, raw in inputs.items()},
                    global_chunk_inputs=[dict(address=address, path=str(path), bytes=len(inputs[path]),
                                              sha256=digest(inputs[path])) for address, path in args.global_chunks],
                    output_scope='one mapper page only; base ROM and normal art/pages are not rewritten',
                    validation='all serialized AB1 and ZB1 entries independently decoded against raw sprites; input hashes unchanged')
    if transitions:
        metadata.update(transition_old_count_mask=transition_old_count_mask,
                        transition_new_count_mask=transition_new_count_mask,
                        transition_count_pairs=transition_count_pairs,
                        transition_fingerprint_pairs=transition_fingerprint_pairs,
                        transition_target_key=transition_target_key)
    if macros:
        metadata.update(macro_offset=macro_offset,
                        macro_address=args.page_address + macro_offset,
                        macro_bytes=len(macros), macro_sha256=digest(macros),
                        macro_patterns=macro_count,
                        macro_max_records=macro_max_records,
                        macro_target_key=macro_target_key)
    if code_bytes:
        require(page[code_offset:code_offset + code_bytes] == b'\xff' * code_bytes,
                'code reservation was modified')
        metadata.update(code_offset=code_offset, code_address=args.page_address + code_offset,
                        code_capacity_bytes=code_bytes, free_art_gap_bytes=code_offset - end)
    return bytes(page), metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base-rom', type=Path)
    parser.add_argument('--index-rom-offset', type=number)
    parser.add_argument('--rom-address-base', type=number, default=0x02000000)
    parser.add_argument('--global-index-json', type=Path)
    parser.add_argument('--global-index-bin', type=Path)
    parser.add_argument('--global-chunk', action='append', default=[], metavar='ADDR:FILE')
    parser.add_argument('--global-window', action='append', default=[], metavar='ADDR:BYTES')
    parser.add_argument('--sprites', type=Path, required=True)
    parser.add_argument('--unscaled-manifest', type=Path)
    parser.add_argument('--corpus', action='append', type=Path, default=[])
    parser.add_argument('--zoom-atlas', type=Path, required=True)
    parser.add_argument('--opaque-runs', type=number, metavar='MIN_GROUPS',
                        help='append one longest fully opaque interval per ZB1 row')
    parser.add_argument('--transition-packets', type=Path,
                        help='append one verified TP1 transition blob after ZB1/OR1')
    parser.add_argument('--macro-packets', type=Path,
                        help='append one verified owner-safe MC1 blob after TP1')
    parser.add_argument('--music', type=Path, required=True)
    parser.add_argument('--music-rom-offset', type=number)
    parser.add_argument('--page-address', type=number, default=0x02380000)
    parser.add_argument('--page-bytes', type=number, default=0x80000)
    parser.add_argument('--guard-bytes', type=number, default=64)
    parser.add_argument('--code-bytes', type=number, default=0,
                        help='reserve a 64-byte-aligned FF-only code region before the guard')
    parser.add_argument('--symbol-prefix', default='SPRITE_WORKSET')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--header', type=Path, required=True)
    args = parser.parse_args()
    try:
        require(re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', args.symbol_prefix), 'invalid header symbol prefix')
        args.corpus = [path / 'spr.bin' if path.is_dir() else path for path in args.corpus]
        require(args.unscaled_manifest or args.corpus, 'supply --unscaled-manifest and/or --corpus')
        require(sum(bool(value) for value in (args.global_index_json, args.global_index_bin,
                                             args.index_rom_offset is not None)) == 1,
                'choose one global-index input mode')
        args.global_chunks = []
        for spec in args.global_chunk:
            address, path = spec.split(':', 1)
            args.global_chunks.append((number(address), Path(path)))
        paths = [args.sprites, args.zoom_atlas, args.music] + args.corpus
        paths += [path for path in (args.base_rom, args.global_index_json, args.global_index_bin,
                                    args.unscaled_manifest, args.transition_packets,
                                    args.macro_packets) if path]
        paths += [path for _, path in args.global_chunks]
        outputs = [args.out, args.metadata, args.header]
        require(len({path.resolve() for path in outputs}) == 3, 'output paths must be distinct')
        require(not ({path.resolve() for path in paths} & {path.resolve() for path in outputs}), 'output would overwrite an input')
        for output in outputs:
            if output.exists():
                require(not any(output.samefile(source) for source in paths), 'output aliases an input inode')
                require(not any(output != other and other.exists() and output.samefile(other)
                                for other in outputs), 'output files alias one inode')
        inputs = {path: path.read_bytes() for path in paths}
        page, metadata = build(args, inputs)
        for path, raw in inputs.items():
            require(path.read_bytes() == raw, f'input changed during bake: {path}')
        prefix = args.symbol_prefix
        header = ('/* Generated mapper page; no pointers survive a page-generation change.\n'
                  ' * AB1_BYTES bounds the on-page index/local region. The AB1 wire header\n'
                  ' * also counts external global bodies, as in tools/bake_art.py. */\n')
        for field in ('page_address', 'page_bytes', 'guard_bytes', 'music_bytes', 'ab1_address',
                      'ab1_bytes', 'ab1_entries', 'zb1_address', 'zb1_bytes', 'zb1_entries'):
            header += f'#define {prefix}_{field.upper()} 0x{metadata[field]:X}u\n'
        for field in ('opaque_runs_address', 'opaque_runs_bytes', 'opaque_runs_threshold'):
            header += f'#define {prefix}_{field.upper()} 0x{metadata[field]:X}u\n'
        for field in ('transition_address', 'transition_bytes'):
            header += f'#define {prefix}_{field.upper()} 0x{metadata[field]:X}u\n'
        if metadata['transition_bytes']:
            header += (f'#define {prefix}_TRANSITION_TARGET_KEY '
                       f'0x{metadata["transition_target_key"]:X}u\n')
            for side in ('old', 'new'):
                mask = metadata[f'transition_{side}_count_mask']
                header += (f'#define {prefix}_TRANSITION_{side.upper()}_COUNT_MASK_LO '
                           f'0x{mask & 0xFFFFFFFF:X}u\n')
                header += (f'#define {prefix}_TRANSITION_{side.upper()}_COUNT_MASK_HI '
                           f'0x{mask >> 32:X}u\n')
            cases = ' '.join(f'case 0x{(old << 6) | new:X}u:'
                             for old, new in metadata['transition_count_pairs'])
            header += f'#define {prefix}_TRANSITION_COUNT_PAIR_CASES {cases}\n'
            matches = ' || '.join(f'(pair) == 0x{(old << 6) | new:X}u'
                                  for old, new in metadata['transition_count_pairs'])
            header += (f'#define {prefix}_TRANSITION_COUNT_PAIR_MATCH(pair) '
                       f'({matches})\n')
            cases = ' '.join(f'case 0x{value:X}u:'
                             for value in metadata['transition_fingerprint_pairs'])
            header += f'#define {prefix}_TRANSITION_FINGERPRINT_CASES {cases}\n'
        if args.macro_packets:
            for field in ('macro_address', 'macro_bytes', 'macro_patterns',
                          'macro_max_records'):
                header += f'#define {prefix}_{field.upper()} 0x{metadata[field]:X}u\n'
            header += (f'#define {prefix}_MACRO_TARGET_KEY '
                       f'0x{metadata["macro_target_key"]:X}u\n')
        else:
            # A page without macro packets: zero patterns, which the runtime reads as
            # "never admit" (r60_mc_bind), and a key no sprite record can carry.
            header += (f'#define {prefix}_MACRO_ADDRESS 0x{args.page_address:X}u\n'
                       f'#define {prefix}_MACRO_BYTES 0x0u\n'
                       f'#define {prefix}_MACRO_PATTERNS 0x0u\n'
                       f'#define {prefix}_MACRO_MAX_RECORDS 0x1u\n'
                       f'#define {prefix}_MACRO_TARGET_KEY 0xFFFFFFFFu\n')
        if args.code_bytes:
            for field in ('code_offset', 'code_address', 'code_capacity_bytes', 'free_art_gap_bytes'):
                header += f'#define {prefix}_{field.upper()} 0x{metadata[field]:X}u\n'
        for path in outputs:
            path.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_bytes(page)
        require(args.out.read_bytes() == page, 'page readback differs')
        args.metadata.write_text(json.dumps(metadata, indent=2) + '\n')
        args.header.write_text(header)
        if args.code_bytes:
            print(f'code reservation: {metadata["code_capacity_bytes"]} FF bytes at '
                  f'{metadata["code_address"]:#010x}; {metadata["free_art_gap_bytes"]} art-gap bytes')
        print(f'working set: {len(page)} bytes; {metadata["ab1_entries"]} AB1 entries, '
              f'{metadata["zb1_entries"]} ZB1 poses; {metadata["free_before_guard_bytes"]} bytes spare')
    except (ValueError, AssertionError, OSError, KeyError, IndexError, struct.error) as error:
        parser.exit(2, f'bake_sprite_workingset: {error}\n')


if __name__ == '__main__':
    main()
