#!/usr/bin/env python3
"""Measure block-compositor ranges in captured Sega16 sprite tables.

This is an admission census, not a speed result.  A DRAW is counted only when
the exact baked packet exists and the current compositor's geometry rules can
accept it.  Other visible records are painter-order barriers.
"""
from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path


def index(path: Path, scaled: bool) -> dict[tuple[int, int], tuple[int, int]]:
    data = path.read_bytes()
    magic, count, wire, reserved = struct.unpack_from(">4I", data)
    assert magic == (0x5A423100 if scaled else 0x41423100)
    assert not reserved and wire <= len(data)
    step = 12 if scaled else 8
    out: dict[tuple[int, int], tuple[int, int]] = {}
    for slot in range(count):
        at = 16 + slot * step
        if scaled:
            key, zoom, offset = struct.unpack_from(">3I", data, at)
        else:
            key, offset = struct.unpack_from(">2I", data, at)
            zoom = 0
        packet_at = offset + (0 if scaled else 0)
        height, width = struct.unpack_from(">2H", data, packet_at)
        cursor = packet_at + 4
        for _ in range(height):
            groups = data[cursor + 1]
            cursor += 4 + groups * 4
        out[key, zoom] = (height, width)
    return out


def classify(table: bytes, ab: dict, zb: dict) -> list[str]:
    kinds: list[str] = []
    for slot in range(64):
        e = struct.unpack_from(">8H", table, slot * 16)
        if e[2] & 0x8000:
            break
        top, bottom = e[0] & 255, min(e[0] >> 8, 224)
        zoom = e[5] & 1023
        x = (e[1] & 511) - 184
        bank = (e[4] >> 8) & 7
        key = ((e[2] & 0x100) << 20) | (bank << 24) | ((e[2] & 255) << 16) | e[3]
        packet = (zb if zoom else ab).get((key, zoom))
        eligible = (
            not (e[2] & 0xC000) and top < bottom and top < 224 and
            0 <= x < 320 and packet is not None and
            bottom - top <= packet[0] and (not zoom or x + packet[1] <= 320)
        )
        kinds.append("draw" if eligible else "barrier")
    return kinds


def runs(kinds: list[str]) -> list[tuple[int, int]]:
    result = []
    first = None
    draws = 0
    for i in range(len(kinds) + 1):
        kind = kinds[i] if i < len(kinds) else "barrier"
        if kind != "barrier":
            if first is None:
                first, draws = i, 0
            draws += kind == "draw"
        elif first is not None:
            if draws >= 2:
                result.append((first, i))
            first = None
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("captures", type=Path, help="directory containing *_sprite.bin")
    parser.add_argument("--ab", type=Path, required=True, help="AB1 baked packet file")
    parser.add_argument("--zb", type=Path, required=True, help="ZB1 baked packet file")
    args = parser.parse_args()

    ab, zb = index(args.ab, False), index(args.zb, True)
    tables = sorted(args.captures.glob("*_sprite.bin"))
    summary = {"captures": len(tables), "records": 0, "eligible_draws": 0,
               "admitted_draws": 0, "runs": 0, "frames_with_runs": 0,
               "max_draws_in_frame": 0, "max_runs_in_frame": 0}
    for path in tables:
        kinds = classify(path.read_bytes(), ab, zb)
        selected = runs(kinds)
        admitted = sum(last - first for first, last in selected)
        summary["records"] += len(kinds)
        summary["eligible_draws"] += kinds.count("draw")
        summary["admitted_draws"] += admitted
        summary["runs"] += len(selected)
        summary["frames_with_runs"] += bool(selected)
        summary["max_draws_in_frame"] = max(summary["max_draws_in_frame"], admitted)
        summary["max_runs_in_frame"] = max(summary["max_runs_in_frame"], len(selected))
    print(json.dumps(summary, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
