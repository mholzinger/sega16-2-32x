#!/usr/bin/env python3
"""Price exact two-frame-bank sprite transition patches before integration.

The 32X renderer alternates physical framebuffer banks.  For a stable scene,
the bank about to be drawn normally contains the target sprite image from two
native ticks earlier.  This tool composes one target-key run in painter order,
then counts the physical four-pixel words that differ between tick N-2 and N.

Each priced patch entry contains y, x-group, a four-bit background-restore mask
and the four new logical palette/pen bytes (8 bytes total).  A restore bit means
the old sprite owned that byte and the new sprite does not; a live renderer must
restore the established background there, not blindly write zero.  Therefore
this is a bounded opportunity/storage census, not a renderer or speed claim.
"""
import argparse
import bisect
import hashlib
import json
import re
import struct
from collections import Counter
from pathlib import Path

import sprite_macro_census as macro


def number(text):
    return int(text, 0)


def surface(records, key, poses, palette_map=None):
    """Return final target-owned screen words and the old scalar store count."""
    pixels = {}
    scalar = 0
    for x, y, bottom, zoom, palette in records:
        rows = poses[key, zoom][0]
        for relative, row in enumerate(rows[:max(0, min(bottom, 224) - y)]):
            yy = y + relative
            scalar += len(macro.stored_words(row, x))
            if not 0 <= yy < 224:
                continue
            for relative_x, pen in enumerate(row):
                xx = x + relative_x
                if pen and 0 <= xx < 320:
                    pixels[yy, xx] = ((palette_map[palette] << 4) | pen
                                      if palette_map is not None else palette | pen)
    words = {}
    for (y, x), value in pixels.items():
        key_at = y, x >> 2
        if key_at not in words:
            words[key_at] = bytearray(4)
        words[key_at][x & 3] = value
    return {at: bytes(value) for at, value in words.items()}, scalar


def patch(old, new):
    """Serialize changed words; never imply that zero is a safe background."""
    entries = []
    restored = 0
    for y, group in sorted(set(old) | set(new)):
        before = old.get((y, group), b"\0\0\0\0")
        after = new.get((y, group), b"\0\0\0\0")
        if before == after:
            continue
        restore = sum((1 << index) for index, (a, b) in
                      enumerate(zip(before, after)) if a and not b)
        restored += bool(restore)
        entries.append(struct.pack(">4B4s", y, group, restore, 0, after))
    blob = struct.pack(">HH", len(entries), 0) + b"".join(entries)
    return blob, restored


def run_patch(old, new):
    """Pack consecutive changed words as {y,x,count,0}+four bytes per word.

    A zero output byte means restore the established background byte. Restoring
    an already-background byte in the same changed word is harmless for the
    static-scene opportunity model and avoids a per-word mask stream.
    """
    changed = []
    for y, group in sorted(set(old) | set(new)):
        before = old.get((y, group), b"\0\0\0\0")
        after = new.get((y, group), b"\0\0\0\0")
        if before != after:
            changed.append((y, group, after))
    runs = []
    index = 0
    while index < len(changed):
        y, group, after = changed[index]
        values = [after]
        index += 1
        while (index < len(changed) and changed[index][0] == y and
               changed[index][1] == group + len(values) and len(values) < 255):
            values.append(changed[index][2])
            index += 1
        runs.append(struct.pack(">4B", y, group, len(values), 0) + b"".join(values))
    if len(runs) > 65535:
        raise ValueError("transition run count exceeds u16")
    return struct.pack(">HH", len(runs), 0) + b"".join(runs)


def budget_rows(rows, budgets, size_key="bytes"):
    ranked = sorted((row for row in rows if row["saved_stores"] > 0), key=lambda row:
                    row["frequency"] * row["saved_stores"] /
                    max(1, row[size_key]), reverse=True)
    total = sum(row["frequency"] * row["saved_stores"] for row in ranked)
    out = []
    for budget in budgets:
        used = saved = frames = count = 0
        for row in ranked:
            if used + row[size_key] > budget:
                continue
            used += row[size_key]
            saved += row["frequency"] * row["saved_stores"]
            frames += row["frequency"]
            count += 1
        out.append(dict(budget=budget, used=used, packets=count,
                        ticks=frames, weighted_saved_stores=saved,
                        saved_store_coverage_percent=100.0 * saved / max(1, total)))
    return out


def dictionary_budget_rows(rows, budgets):
    """Greedy saved-stores per added packet/index/dictionary byte."""
    out = []
    total = sum(row["frequency"] * row["saved_stores"]
                for row in rows if row["saved_stores"] > 0)
    for budget in budgets:
        remaining = [row for row in rows if row["saved_stores"] > 0]
        words = set()
        used = saved = ticks = 0
        selected = 0
        first_ticks = []
        while remaining:
            choices = []
            for row in remaining:
                added = (row["_dictionary_body_bytes"] +
                         4 * len(row["_dictionary_words"] - words))
                value = row["frequency"] * row["saved_stores"]
                if used + added <= budget:
                    choices.append((value / max(1, added), value, -added, row))
            if not choices:
                break
            row = max(choices, key=lambda choice: choice[:3])[3]
            used += (row["_dictionary_body_bytes"] +
                     4 * len(row["_dictionary_words"] - words))
            words.update(row["_dictionary_words"])
            saved += row["frequency"] * row["saved_stores"]
            ticks += row["frequency"]
            selected += 1
            first_ticks.append(row["first_tick"])
            remaining.remove(row)
        out.append(dict(budget=budget, used=used, packets=selected,
                        dictionary_entries=len(words), ticks=ticks,
                        first_ticks=sorted(first_ticks),
                        weighted_saved_stores=saved,
                        saved_store_coverage_percent=100.0 * saved / max(1, total)))
    return out


def census(args):
    page = args.workset.read_bytes()
    metadata = json.loads(args.workset_json.read_text())
    poses = macro.zoom_packets(page, metadata)
    poses.update(macro.unscaled_packets(page, metadata, args.workset,
                                        args.global_root))
    ranges = macro.tick_ranges(args.ticks)
    if not ranges:
        raise ValueError("at least one --ticks LO:HI range is required")

    stream = args.sprite_stream.read_bytes() if args.sprite_stream else None
    if stream is not None:
        if len(stream) % 1024:
            raise ValueError("sprite stream is not an integral number of 1 KiB tables")
        events = [(args.sprite_from + index, None)
                  for index in range(len(stream) // 1024)]
    else:
        events = []
        for path in args.corpus.glob("ev_*_sprite.bin"):
            match = re.fullmatch(r"ev_(\d+)_sprite\.bin", path.name)
            if match:
                events.append((int(match.group(1)), path))
        events.sort()
    event_ticks = [tick for tick, _path in events]
    ranges_set = {tick for lo, hi in ranges for tick in range(lo, hi + 1)}

    def event_blob(event_tick, path):
        return (stream[(event_tick - args.sprite_from) * 1024:
                       (event_tick - args.sprite_from + 1) * 1024]
                if stream is not None else path.read_bytes())

    palette_ids = set()
    for event_tick, path in events:
        if event_tick not in ranges_set:
            continue
        blob = event_blob(event_tick, path)
        for slot in range(64):
            words = struct.unpack_from(">8H", blob, slot * 16)
            if words[2] & 0x8000:
                break
            key = (((words[2] & 0x100) << 20) |
                   (((words[4] >> 8) & 7) << 24) |
                   ((words[2] & 255) << 16) | words[3])
            if key == args.key:
                palette_ids.add(words[4] & 63)
    if len(palette_ids) > 15:
        raise ValueError("target uses more than 15 logical palettes")
    palette_ids = sorted(palette_ids)
    palette_map = {palette: index + 1
                   for index, palette in enumerate(palette_ids)}
    cache = {}
    admitted = None
    admission_input = None
    if args.admission_json:
        admission_blob = args.admission_json.read_bytes()
        admission = json.loads(admission_blob)
        admitted = {row["tick"] for row in admission["rows"]
                    if row.get("admitted") and (not args.require_zero_lower_restore or
                    row.get("nonzero_lower_sprite_restore_pixels", 0) == 0)}
        admission_input = dict(path=str(args.admission_json),
                               sha256=hashlib.sha256(admission_blob).hexdigest(),
                               admitted_ticks=len(admitted))

    def at_tick(tick):
        index = bisect.bisect_right(event_ticks, tick) - 1
        if index < 0:
            return {}, 0
        event_tick, path = events[index]
        if event_tick not in cache:
            blob = event_blob(event_tick, path)
            runs = macro.live_runs(blob, args.key, poses)
            if len(runs) > 1:
                raise ValueError(f"tick {event_tick} has multiple target runs")
            cache[event_tick] = surface(runs[0], args.key, poses, palette_map) if runs else ({}, 0)
        return cache[event_tick]

    packets = {}
    word_dictionary = set()
    dictionary_packet_bytes = 0
    totals = Counter()
    for lo, hi in ranges:
        if hi < lo:
            raise ValueError("tick range end precedes start")
        for tick in range(lo, hi + 1):
            if admitted is not None and tick not in admitted:
                continue
            new_event = bisect.bisect_right(event_ticks, tick) - 1
            old_event = bisect.bisect_right(event_ticks, tick - 2) - 1
            if new_event < 0 or old_event < 0:
                continue
            if not args.carry_forward and (event_ticks[new_event] != tick or
                                           event_ticks[old_event] != tick - 2):
                continue
            new, scalar = at_tick(tick)
            old, _old_scalar = at_tick(tick - 2)
            encoded, restored = patch(old, new)
            encoded_runs = run_patch(old, new)
            changed = struct.unpack_from(">H", encoded)[0]
            macro_stores = len(new)
            totals.update(ticks=1, scalar_stores=scalar,
                          full_macro_stores=macro_stores,
                          patch_stores=changed,
                          restored_words=restored,
                          patch_bytes=len(encoded))
            digest = hashlib.sha256(encoded).hexdigest()
            if digest not in packets:
                run_count = struct.unpack_from(">H", encoded_runs)[0]
                words = {encoded[at + 4:at + 8]
                         for at in range(4, len(encoded), 8)}
                word_dictionary.update(words)
                # u16 dictionary index per changed word; headers stay direct.
                dictionary_packet_bytes += 4 + 4 * run_count + 2 * changed
                packets[digest] = dict(frequency=0, bytes=len(encoded),
                    run_bytes=len(encoded_runs), dictionary_words=len(words),
                    _dictionary_words=words,
                    _dictionary_body_bytes=4 + 4 * run_count + 2 * changed,
                    stores=changed, restored_words=restored,
                    saved_stores=max(0, scalar - changed), first_tick=tick)
            row = packets[digest]
            row["frequency"] += 1

    rows = sorted(packets.values(), key=lambda row:
                  (-row["frequency"], row["first_tick"]))
    dictionary_budgets = dictionary_budget_rows(rows, args.budget)
    for row in rows:
        row.pop("_dictionary_words")
        row.pop("_dictionary_body_bytes")
    scalar = totals["scalar_stores"]
    full = totals["full_macro_stores"]
    changed = totals["patch_stores"]
    return dict(key=f"{args.key:08x}", bank_age_ticks=2,
                palette_ids=palette_ids,
                admission=admission_input,
                ranges=[list(item) for item in ranges],
                ticks=totals["ticks"], unique_patches=len(rows),
                scalar_stores=scalar, full_macro_stores=full,
                transition_patch_stores=changed,
                patch_vs_scalar_reduction_percent=100.0 * (scalar - changed) / max(1, scalar),
                patch_vs_full_macro_reduction_percent=100.0 * (full - changed) / max(1, full),
                restored_words=totals["restored_words"],
                encoded_patch_bytes=totals["patch_bytes"],
                unique_patch_bytes=sum(row["bytes"] for row in rows),
                unique_run_patch_bytes=sum(row["run_bytes"] for row in rows),
                word_dictionary_entries=len(word_dictionary),
                word_dictionary_bytes=4 * len(word_dictionary),
                dictionary_run_patch_bytes=(4 * len(word_dictionary) +
                                            dictionary_packet_bytes),
                budgets=budget_rows(rows, args.budget),
                run_budgets=budget_rows(rows, args.budget, "run_bytes"),
                dictionary_budgets=dictionary_budgets, patches=rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("corpus", type=Path)
    parser.add_argument("--workset", type=Path, required=True)
    parser.add_argument("--workset-json", type=Path, required=True)
    parser.add_argument("--global-root", type=Path)
    parser.add_argument("--sprite-stream", type=Path,
                        help="contiguous 1 KiB native tables, one per frame")
    parser.add_argument("--sprite-from", type=int, default=1,
                        help="frame number of the stream's first table (default: 1)")
    parser.add_argument("--admission-json", type=Path,
                        help="oracle report; price only rows with admitted=true")
    parser.add_argument("--carry-forward", action="store_true",
                        help="diagnostic only: reuse the latest sparse capture between events")
    parser.add_argument("--require-zero-lower-restore", action="store_true",
                        help="admit only oracle rows whose vacated target bytes restore zero")
    parser.add_argument("--key", type=number, required=True)
    parser.add_argument("--ticks", action="append", default=[])
    parser.add_argument("--budget", type=number, action="append")
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    if args.budget is None:
        args.budget = [0x5580, 0x40000, 0x80000]
    result = census(args)
    text = json.dumps(result, indent=2, sort_keys=True) + "\n"
    print(text, end="")
    if args.json:
        args.json.write_text(text)


if __name__ == "__main__":
    main()
