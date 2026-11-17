#!/usr/bin/env python3
"""Pack proven N/N-2 target transitions into a bounded read-only blob.

The caller supplies exact native ticks selected by the census.  Target states
are stored as their complete six drawing words per record, so runtime admission
can compare both the current snapshot and the physical bank's previous state
without a hash collision.  Pixel bytes use a page-level logical-palette
selector in the high nibble and the raw pen in the low nibble.

This is a host format builder and round-trip verifier.  It does not install a
decoder or change the live renderer.
"""
import argparse
import json
import struct
from pathlib import Path

import sprite_macro_census as macro
import sprite_transition_census as transition
import s16art


def number(value):
    return int(value, 0) if isinstance(value, str) else int(value)


def align(value, boundary=4):
    return (value + boundary - 1) & -boundary


def target_state(blob, key):
    records = []
    active = False
    ended = False
    for slot in range(64):
        words = struct.unpack_from(">8H", blob, slot * 16)
        if words[2] & 0x8000:
            break
        actual = (((words[2] & 0x100) << 20) |
                  (((words[4] >> 8) & 7) << 24) |
                  ((words[2] & 255) << 16) | words[3])
        if actual == key:
            if ended:
                raise ValueError("target is not one contiguous record run")
            active = True
            records.append(words[:6])
        elif active:
            ended = True
    return tuple(records)


def drawing_state(blob):
    """All six drawing words through the terminator, for collision-free admission."""
    records = []
    for slot in range(64):
        words = struct.unpack_from(">8H", blob, slot * 16)
        if words[2] & 0x8000:
            break
        records.append(words[:6])
    return tuple(records)


def state_blob(state):
    body = b"".join(struct.pack(">6H", *record) for record in state)
    return struct.pack(">HH", len(state), 4 + len(body)) + body


def changed_words(old, new, lower=None):
    out = []
    lower = lower or {}
    for y, group in sorted(set(old) | set(new)):
        before = old.get((y, group), b"\0\0\0\0")
        after = new.get((y, group), b"\0\0\0\0")
        if before != after:
            restore = lower.get((y, group), b"\0\0\0\0")
            after = bytes(pixel or restore[index]
                          for index, pixel in enumerate(after))
            out.append((y, group, after))
    return out


def lower_records(blob, target):
    """Drawing records before the contiguous target run, in painter order."""
    out = []
    for slot in range(64):
        record = struct.unpack_from(">8H", blob, slot * 16)
        if record[2] & 0x8000:
            break
        actual = (((record[2] & 0x100) << 20) |
                  (((record[4] >> 8) & 7) << 24) |
                  ((record[2] & 255) << 16) | record[3])
        if actual == target:
            break
        out.append(record)
    return out


def lower_surface(blob, target, sprite_words, palette_map):
    """Final logical pixels beneath the target's painter-order insertion point."""
    pixels = {}
    for record in lower_records(blob, target):
        top, bottom = record[0] & 255, record[0] >> 8
        if record[2] & 0x4000 or top >= bottom:
            continue
        pitch = (record[2] & 127) - (record[2] & 128)
        rows, _source_words, _row_words = s16art.decode_scaled(
            sprite_words, (record[4] >> 8) & 7, record[3], pitch,
            bool(record[2] & 0x100), bottom - top,
            record[5] & 31, (record[5] >> 5) & 31)
        x = (record[1] & 511) - 184
        palette = palette_map[record[4] & 63]
        for relative, row in enumerate(rows[:max(0, min(bottom, 224) - top)]):
            y = top + relative
            for relative_x, pen in enumerate(row):
                xx = x + relative_x
                if pen and 0 <= xx < 320:
                    pixels[y, xx] = (palette << 4) | pen
    words = {}
    for (y, x), value in pixels.items():
        at = y, x >> 2
        if at not in words:
            words[at] = bytearray(4)
        words[at][x & 3] = value
    return {at: bytes(value) for at, value in words.items()}


def runs(words):
    out = []
    index = 0
    while index < len(words):
        y, group, value = words[index]
        values = [value]
        index += 1
        while (index < len(words) and words[index][0] == y and
               words[index][1] == group + len(values) and len(values) < 255):
            values.append(words[index][2])
            index += 1
        out.append((y, group, values))
    return out


def build(args):
    selected = {}
    physical_palette_banks = None
    if args.manifest:
        manifests = [json.loads(args.manifest.read_text())]
        manifests += [json.loads(path.read_text())
                      for path in args.extra_manifest]
        manifest_key = number(manifests[0]["key"])
        if "palette_banks" in manifests[0]:
            physical_palette_banks = {
                number(palette): number(bank)
                for palette, bank in manifests[0]["palette_banks"].items()
            }
        if args.key is None:
            args.key = manifest_key
        elif args.key != manifest_key:
            raise ValueError("manifest target key differs from --key")

        def expand(text):
            raw = bytes.fromhex(text)
            if not raw or len(raw) % 12 or len(raw) // 12 >= 64:
                raise ValueError("manifest state is not 1..63 six-word records")
            table = bytearray(1024)
            for slot in range(len(raw) // 12):
                table[slot * 16:slot * 16 + 12] = raw[slot * 12:(slot + 1) * 12]
            struct.pack_into(">H", table, len(raw) // 12 * 16 + 4, 0x8000)
            return bytes(table)

        for manifest in manifests:
            if (manifest.get("format") != "altbeast-smoke-transitions-v1" or
                    number(manifest["key"]) != manifest_key):
                raise ValueError("transition manifests have incompatible format or target key")
            banks = {number(palette): number(bank)
                     for palette, bank in manifest.get("palette_banks", {}).items()}
            if banks and banks != physical_palette_banks:
                raise ValueError("transition manifests have incompatible physical palette maps")
            for entry in manifest["entries"]:
                tick = number(entry["tick"])
                state_pair = (expand(entry["old_state_hex"]),
                              expand(entry["new_state_hex"]))
                if tick in selected and selected[tick] != state_pair:
                    raise ValueError(f"tick {tick} differs between transition manifests")
                selected[tick] = state_pair
        ticks = args.tick or sorted(selected)

        def tables(tick):
            if tick not in selected:
                raise ValueError(f"tick {tick} is absent from the manifest")
            return selected[tick]
    else:
        if args.key is None or not args.tick:
            raise ValueError("stream mode requires --key and --tick")
        stream = args.sprite_stream.read_bytes()
        if len(stream) % 1024:
            raise ValueError("sprite stream is not an integral number of tables")

        def table(tick):
            index = tick - args.sprite_from
            if not 0 <= index < len(stream) // 1024:
                raise ValueError(f"tick {tick} is outside the sprite stream")
            return stream[index * 1024:(index + 1) * 1024]

        ticks = args.tick
        tables = lambda tick: (table(tick - 2), table(tick))

    if not args.sprites:
        raise ValueError("exact lower-sprite restoration requires --sprites")
    words = s16art.load_rom(args.sprites)
    if args.workset:
        page = args.workset.read_bytes()
        metadata = json.loads(args.workset_json.read_text())
        poses = macro.zoom_packets(page, metadata)
        poses.update(macro.unscaled_packets(page, metadata, args.workset,
                                            args.global_root))
    else:
        atlas = args.zoom_atlas.read_bytes()
        poses = macro.zoom_packets(atlas, {"zb1_offset": 0})
        height = max((record[0] >> 8) - (record[0] & 255)
                     for tick in ticks for table_at in tables(tick)
                     for record in target_state(table_at, args.key)
                     if not (record[5] & 1023))
        bank, address = (args.key >> 24) & 7, args.key & 0xffff
        pitch = (args.key >> 16) & 255
        if pitch & 128:
            pitch -= 256
        rows, _source_words, row_words = s16art.decode(
            words, bank, address, pitch, bool(args.key & 0x10000000), height)
        poses[args.key, 0] = (rows, row_words)
    palettes = sorted({record[4] & 63 for tick in ticks
                       for table_at in tables(tick)
                       for record in (target_state(table_at, args.key) +
                                      tuple(r[:6] for r in lower_records(
                                          table_at, args.key)))})
    if not palettes or len(palettes) > 15:
        raise ValueError("selected states need 1..15 logical palettes")
    palette_map = {palette: index + 1 for index, palette in enumerate(palettes)}
    if physical_palette_banks is not None:
        if not set(palettes).issubset(physical_palette_banks) or any(
                not 0 <= bank < 16 for bank in physical_palette_banks.values()):
            raise ValueError("physical palette map must cover every used logical palette with bank 0..15")

    def physical_value(value):
        if physical_palette_banks is None:
            return value
        out = bytearray()
        for pixel in value:
            if not pixel:
                out.append(0)
                continue
            selector = pixel >> 4
            logical_palette = palettes[selector - 1]
            out.append((physical_palette_banks[logical_palette] << 4) |
                       (pixel & 15))
        return bytes(out)

    states = []
    state_index = {}
    packets = []
    dictionary = []
    dictionary_index = {}
    for tick in ticks:
        old_table, new_table = tables(tick)
        old_target, new_target = (target_state(old_table, args.key),
                                  target_state(new_table, args.key))
        if not old_target or not new_target:
            raise ValueError(f"tick {tick} lacks target in one physical-bank state")
        old_state, new_state = drawing_state(old_table), drawing_state(new_table)
        indexes = []
        for state in (old_state, new_state):
            if state not in state_index:
                state_index[state] = len(states)
                states.append(state)
            indexes.append(state_index[state])
        old_runs = macro.live_runs(old_table, args.key, poses)
        new_runs = macro.live_runs(new_table, args.key, poses)
        if len(old_runs) != 1 or len(new_runs) != 1:
            raise ValueError(f"tick {tick} target is not one drawable run")
        old, _ = transition.surface(old_runs[0], args.key, poses, palette_map)
        new, scalar = transition.surface(new_runs[0], args.key, poses, palette_map)
        lower = lower_surface(new_table, args.key, words, palette_map)
        word_rows = changed_words(old, new, lower)
        word_rows = [(y, group, physical_value(value))
                     for y, group, value in word_rows]
        encoded_runs = []
        for y, group, values in runs(word_rows):
            encoded = bytearray(struct.pack(">4B", y, group, len(values), 0))
            for value in values:
                if value not in dictionary_index:
                    dictionary_index[value] = len(dictionary)
                    dictionary.append(value)
                encoded += struct.pack(">H", dictionary_index[value])
            encoded_runs.append(bytes(encoded))
        body = b"".join(encoded_runs)
        packets.append(dict(tick=tick, old_state=indexes[0],
                            new_state=indexes[1], runs=len(encoded_runs),
                            words=len(word_rows), scalar_stores=scalar,
                            saved_stores=max(0, scalar - len(word_rows)), body=body))

    header_bytes = 40
    palette_blob = bytes(palettes)
    if physical_palette_banks is not None:
        palette_blob += bytes(physical_palette_banks[palette]
                              for palette in palettes)
    palette_blob += bytes(align(len(palette_blob)) - len(palette_blob))
    states_blob = b"".join(state_blob(state) for state in states)
    state_offsets = []
    at = header_bytes + len(palette_blob)
    for state in states:
        state_offsets.append(at)
        at += len(state_blob(state))
    packet_offset = at
    packet_bytes = 24 * len(packets)
    dictionary_offset = packet_offset + packet_bytes
    dictionary_blob = b"".join(dictionary)
    body_offset = dictionary_offset + len(dictionary_blob)
    bodies = bytearray()
    descriptors = bytearray()
    for packet in packets:
        packet_body_offset = body_offset + len(bodies)
        descriptors += struct.pack(">HHIIIHHI", packet["old_state"],
                                   packet["new_state"], packet_body_offset,
                                   len(packet["body"]), packet["tick"],
                                   packet["runs"], packet["words"],
                                   packet["saved_stores"])
        bodies += packet["body"]
    total = body_offset + len(bodies)
    version = 2 if physical_palette_banks is not None else 1
    header = struct.pack(">4sHHHHIIIIIII", b"TP1\0", version, len(packets),
                         len(states), len(palettes), len(dictionary), args.key,
                         header_bytes + len(palette_blob), packet_offset,
                         dictionary_offset, body_offset, total)
    blob = header + palette_blob + states_blob + descriptors + dictionary_blob + bodies
    if len(blob) != total:
        raise AssertionError("transition blob layout mismatch")
    verify(blob)
    report = dict(format="sprite-transition-page-v1", bytes=len(blob),
                  palettes=palettes, states=len(states), packets=[
                      {key: value for key, value in packet.items() if key != "body"}
                      for packet in packets], dictionary_entries=len(dictionary),
                  saved_stores=sum(packet["saved_stores"] for packet in packets),
                  scope="host packet and round-trip proof only; no live decoder")
    if physical_palette_banks is not None:
        report["physical_palette_banks"] = physical_palette_banks
    return blob, report


def verify(blob):
    (magic, version, packet_count, state_count, palette_count, dictionary_count,
     target_key, state_offset, packet_offset, dictionary_offset, body_offset,
     total) = struct.unpack_from(">4sHHHHIIIIIII", blob)
    if magic != b"TP1\0" or version not in (1, 2) or total != len(blob):
        raise ValueError("invalid transition header")
    palette_bytes = palette_count * (2 if version == 2 else 1)
    if not target_key or not (40 + palette_bytes <= state_offset <= packet_offset <= dictionary_offset <=
            body_offset <= total and 1 <= palette_count <= 15):
        raise ValueError("invalid transition section bounds")
    at = state_offset
    for _ in range(state_count):
        count, size = struct.unpack_from(">HH", blob, at)
        if size != 4 + count * 12 or at + size > packet_offset:
            raise ValueError("invalid exact state")
        at += size
    if at != packet_offset or packet_offset + packet_count * 24 != dictionary_offset:
        raise ValueError("invalid descriptor bounds")
    if dictionary_offset + dictionary_count * 4 != body_offset:
        raise ValueError("invalid dictionary bounds")
    if version == 2 and any(bank >= 16 for bank in
                            blob[40 + palette_count:40 + palette_count * 2]):
        raise ValueError("invalid physical palette bank")
    for index in range(packet_count):
        old, new, offset, size, _tick, run_count, word_count, _saved = \
            struct.unpack_from(">HHIIIHHI", blob, packet_offset + index * 24)
        if old >= state_count or new >= state_count or not body_offset <= offset <= total - size:
            raise ValueError("invalid packet descriptor")
        at = offset
        words = 0
        for _ in range(run_count):
            _y, _x, count, reserved = struct.unpack_from(">4B", blob, at)
            at += 4
            if not count or reserved or at + count * 2 > offset + size:
                raise ValueError("invalid transition run")
            for entry in struct.unpack_from(f">{count}H", blob, at):
                if entry >= dictionary_count:
                    raise ValueError("transition dictionary index out of bounds")
            at += count * 2
            words += count
        if at != offset + size or words != word_count:
            raise ValueError("transition packet extent mismatch")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--sprite-stream", type=Path)
    source.add_argument("--manifest", type=Path)
    parser.add_argument("--extra-manifest", type=Path, action="append", default=[],
                        help="merge another compatible exact-state manifest")
    parser.add_argument("--sprite-from", type=int, default=1)
    art = parser.add_mutually_exclusive_group(required=True)
    art.add_argument("--workset", type=Path)
    art.add_argument("--zoom-atlas", type=Path)
    parser.add_argument("--workset-json", type=Path)
    parser.add_argument("--sprites", type=Path)
    parser.add_argument("--global-root", type=Path)
    parser.add_argument("--key", type=number)
    parser.add_argument("--tick", type=number, action="append")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    if args.workset and not args.workset_json:
        parser.error("--workset requires --workset-json")
    if args.zoom_atlas and not args.sprites:
        parser.error("--zoom-atlas requires --sprites")
    blob, report = build(args)
    args.out.write_bytes(blob)
    args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(f"transition page: {len(blob)} bytes, {report['states']} exact states, "
          f"{len(report['packets'])} packets, {report['dictionary_entries']} words")


if __name__ == "__main__":
    main()
