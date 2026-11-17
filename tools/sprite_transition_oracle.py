#!/usr/bin/env python3
"""Prove a carried two-bank sprite transition against the native sprite layer.

The accepted renderer clears the physical bank's N-2 sprite spans before it
draws tick N.  A transition patch can reuse N-2 only if the target run is
treated as a kept object.  This oracle models that alternate clear exactly at
the sprite-span level, draws every non-target record in native painter order,
applies the target patch at its original list position, and compares the full
320x224 logical sprite surface with an ordinary tick-N redraw.

Foreground priority and the 32X text pass are deliberately outside this
proof.  A zero mismatch is therefore a necessary sprite-layer admission gate,
not a live-renderer or whole-frame parity verdict.
"""
import argparse
import bisect
import hashlib
import json
import re
import struct
from pathlib import Path

import s16art

WIDTH, HEIGHT = 320, 224


def number(text):
    return int(text, 0)


def sprite_key(e):
    return (((e[2] & 0x100) << 20) | (((e[4] >> 8) & 7) << 24) |
            ((e[2] & 255) << 16) | e[3])


def decode_table(blob, words):
    records = []
    for slot in range(64):
        e = struct.unpack_from(">8H", blob, slot * 16)
        if e[2] & 0x8000:
            break
        top, bottom = e[0] & 255, e[0] >> 8
        if e[2] & 0x4000 or top >= bottom:
            continue
        pitch = (e[2] & 127) - (e[2] & 128)
        rows, _source_words, _row_words = s16art.decode_scaled(
            words, (e[4] >> 8) & 7, e[3], pitch, bool(e[2] & 256),
            bottom - top, e[5] & 31, (e[5] >> 5) & 31)
        spans = []
        x = (e[1] & 511) - 184
        for offset, row in enumerate(rows[:max(0, min(bottom, HEIGHT) - top)]):
            opaque = [i for i, pen in enumerate(row) if pen]
            if not opaque:
                spans.append(None)
                continue
            lo = max(0, (x + opaque[0]) & ~3)
            hi = min(WIDTH, (x + len(row) + 3) & ~3)
            spans.append((lo, hi) if hi > lo else None)
        records.append(dict(slot=slot, key=sprite_key(e), words=e, x=x, y=top,
                            palette=(e[4] & 63) << 4, rows=rows,
                            spans=spans, box=(max(0, x),
                            max((span[1] for span in spans if span), default=0),
                            top, min(bottom, HEIGHT))))
    return records


def draw_record(frame, record):
    for dy, row in enumerate(record["rows"]):
        y = record["y"] + dy
        if not 0 <= y < HEIGHT:
            continue
        base = y * WIDTH
        for dx, pen in enumerate(row):
            x = record["x"] + dx
            if pen and 0 <= x < WIDTH:
                frame[base + x] = (record["palette"] | pen) & 255


def compose(records):
    frame = bytearray(WIDTH * HEIGHT)
    for record in records:
        draw_record(frame, record)
    return frame


def target_surface(records, target):
    frame = bytearray(WIDTH * HEIGHT)
    for record in records:
        if record["key"] == target:
            draw_record(frame, record)
    return frame


def target_slots(records, target):
    slots = [record["slot"] for record in records if record["key"] == target]
    if not slots:
        return None
    if slots != list(range(slots[0], slots[-1] + 1)):
        raise ValueError("target records are not one contiguous painter-order run")
    return slots[0], slots[-1]


def spans_by_row(records, predicate):
    rows = [[] for _ in range(HEIGHT)]
    for record in records:
        if not predicate(record):
            continue
        for dy, span in enumerate(record["spans"]):
            y = record["y"] + dy
            if span and 0 <= y < HEIGHT:
                rows[y].append(span)
    return rows


def overlaps(a, b):
    return (a[1] > a[0] and a[3] > a[2] and b[1] > b[0] and b[3] > b[2]
            and a[0] < b[1] and b[0] < a[1]
            and a[2] < b[3] and b[2] < a[3])


def retained_slots(old_records, new_records, target):
    """Renderer keep dependency walk, with the old target run forced carried."""
    old = {record["slot"]: record for record in old_records}
    new = {record["slot"]: record for record in new_records}
    slots = sorted(old.keys() | new.keys())
    changed = {slot: slot not in old or slot not in new or
               old[slot]["words"] != new[slot]["words"] for slot in slots}
    forced = {slot for slot, record in old.items() if record["key"] == target}
    keep = set(forced)
    for slot in slots:
        if slot in forced or changed[slot] or slot not in old or slot not in new:
            continue
        box = old[slot]["box"]
        if box[1] <= box[0] or box[3] <= box[2]:
            continue
        ok = True
        for other in slots:
            if other == slot:
                continue
            if other in forced:
                # It is not cleared, but a target patch earlier in painter order
                # would overwrite this retained higher record.
                if other < slot and other in new and overlaps(box, new[other]["box"]):
                    ok = False
                continue
            if not changed[other] and (other >= slot or other in keep):
                continue
            if other in old and overlaps(box, old[other]["box"]):
                ok = False
            if ok and other < slot and other in new and overlaps(box, new[other]["box"]):
                ok = False
            if not ok:
                break
        if ok:
            keep.add(slot)
    return keep


def carried_clear(frame, old_records, target, retained):
    """Accepted row-extent clear with the target run classified as kept."""
    kept = spans_by_row(old_records, lambda record: record["key"] == target)
    stale = spans_by_row(old_records, lambda record:
                         record["key"] != target and record["slot"] not in retained)
    cleared = collided = 0
    for y in range(HEIGHT):
        if not stale[y]:
            continue
        lo = min(span[0] for span in stale[y])
        hi = max(span[1] for span in stale[y])
        if kept[y]:
            kl = min(span[0] for span in kept[y])
            kh = max(span[1] for span in kept[y])
            runs = [(lo, min(hi, kl)), (max(lo, kh), hi)]
            # cx_inner(): a stale span still clears its actual intersection
            # with the kept extent, even though the row-extent clear cuts it.
            for a, b in stale[y]:
                aa, bb = max(a, kl), min(b, kh)
                if bb > aa:
                    runs.append((aa, bb))
                    collided += bb - aa
        else:
            runs = [(lo, hi)]
        for a, b in runs:
            if b <= a:
                continue
            base = y * WIDTH
            for x in range(a, b):
                if frame[base + x]:
                    cleared += 1
                frame[base + x] = 0
    return cleared, collided


def patched_frame(old_records, new_records, target):
    old_target = target_surface(old_records, target)
    new_target = target_surface(new_records, target)
    frame = compose(old_records)
    old_by_slot = {record["slot"]: record for record in old_records}
    new_by_slot = {record["slot"]: record for record in new_records}
    retained = retained_slots(old_records, new_records, target)
    cleared, collided = carried_clear(frame, old_records, target, retained)
    slots = target_slots(new_records, target)
    if slots is None:
        return frame, cleared, collided, 0, 0
    first, last = slots
    changed_words = nonzero_restore_pixels = 0
    inserted = False
    before = bytearray(WIDTH * HEIGHT)
    for record in new_records:
        if record["slot"] < first:
            if record["slot"] not in retained:
                draw_record(frame, record)
            draw_record(before, record)
            continue
        if first <= record["slot"] <= last and record["key"] == target:
            if inserted:
                continue
            inserted = True
            # The patch owns changed physical words. Vacated target bytes take
            # the current lower-painter result, not an assumed transparent zero.
            for y in range(HEIGHT):
                base = y * WIDTH
                for x in range(0, WIDTH, 4):
                    old = old_target[base + x:base + x + 4]
                    new = new_target[base + x:base + x + 4]
                    if old == new:
                        continue
                    changed_words += 1
                    for k in range(4):
                        if old[k] and not new[k] and before[base + x + k]:
                            nonzero_restore_pixels += 1
                        frame[base + x + k] = (new[k] if new[k]
                                               else before[base + x + k])
            continue
        if record["slot"] in retained:
            continue
        draw_record(frame, record)
    return frame, cleared, collided, changed_words, nonzero_restore_pixels


def event_index(corpus, sprite_stream, sprite_from):
    if sprite_stream:
        blob = sprite_stream.read_bytes()
        if len(blob) % 1024:
            raise ValueError("sprite stream is not an integral number of 1 KiB tables")
        events = [(sprite_from + index, None)
                  for index in range(len(blob) // 1024)]
        return events, [tick for tick, _path in events], blob
    events = []
    for path in corpus.glob("ev_*_sprite.bin"):
        match = re.fullmatch(r"ev_(\d+)_sprite\.bin", path.name)
        if match:
            events.append((int(match.group(1)), path))
    events.sort()
    return events, [tick for tick, _path in events], None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("corpus", type=Path)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--sprite-stream", type=Path,
                        help="contiguous 1 KiB native tables, one per frame")
    parser.add_argument("--sprite-from", type=int, default=1,
                        help="frame number of the stream's first table (default: 1)")
    parser.add_argument("--key", type=number, required=True)
    parser.add_argument("--ticks", action="append", required=True,
                        help="inclusive LO:HI; repeatable")
    parser.add_argument("--json", type=Path)
    parser.add_argument("--carry-forward", action="store_true",
                        help="diagnostic only: reuse latest sparse capture between events")
    args = parser.parse_args()
    words = s16art.load_rom(args.rom)
    events, event_ticks, stream = event_index(args.corpus, args.sprite_stream,
                                               args.sprite_from)
    cache = {}

    def at_tick(tick):
        index = bisect.bisect_right(event_ticks, tick) - 1
        if index < 0:
            return None, []
        event_tick, path = events[index]
        if event_tick not in cache:
            blob = (stream[(event_tick - args.sprite_from) * 1024:
                           (event_tick - args.sprite_from + 1) * 1024]
                    if stream is not None else path.read_bytes())
            cache[event_tick] = decode_table(blob, words)
        return event_tick, cache[event_tick]

    rows = []
    for value in args.ticks:
        lo, sep, hi = value.partition(":")
        if not sep:
            parser.error("--ticks must be LO:HI")
        for tick in range(number(lo), number(hi) + 1):
            old_event, old_records = at_tick(tick - 2)
            new_event, new_records = at_tick(tick)
            if old_event is None or new_event is None:
                continue
            if not args.carry_forward and (old_event != tick - 2 or
                                           new_event != tick):
                continue
            if target_slots(old_records, args.key) is None or target_slots(
                    new_records, args.key) is None:
                continue
            expected = compose(new_records)
            actual, cleared, collided, changed, nonzero_restore = patched_frame(
                old_records, new_records, args.key)
            differing = sum(a != b for a, b in zip(actual, expected))
            rows.append(dict(tick=tick, old_event=old_event,
                             new_event=new_event, changed_words=changed,
                             cleared_nonzero_pixels=cleared,
                             stale_span_pixels_inside_kept_extent=collided,
                             nonzero_lower_sprite_restore_pixels=nonzero_restore,
                             differing_sprite_pixels=differing,
                             admitted=differing == 0))
    result = dict(scope="sprite layer only; foreground priority and text excluded",
                  key=f"{args.key:08x}", ticks=len(rows),
                  admitted=sum(row["admitted"] for row in rows),
                  rejected=sum(not row["admitted"] for row in rows),
                  rows=rows,
                  inputs={"rom": hashlib.sha256(args.rom.read_bytes()).hexdigest()})
    text = json.dumps(result, indent=2, sort_keys=True) + "\n"
    print(text, end="")
    if args.json:
        args.json.write_text(text)


if __name__ == "__main__":
    main()
