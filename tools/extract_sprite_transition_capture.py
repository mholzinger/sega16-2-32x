#!/usr/bin/env python3
"""Turn exact renderer admission captures into a transition manifest.

The probe stores the first six words of every drawing record contiguously,
even though the live tables use an eight-word stride.  Symbol offsets are
supplied explicitly because each diagnostic ROM has its own link map.
"""
import argparse
import json
import struct
from pathlib import Path


def number(text):
    return int(text, 0)


def capture_arg(text):
    tick, separator, path = text.partition(":")
    if not separator:
        raise argparse.ArgumentTypeError("capture must be TICK:SDRAM_DUMP")
    return number(tick), Path(path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--capture-offset", required=True, type=number)
    parser.add_argument("--count-offset", required=True, type=number)
    parser.add_argument("--key", required=True)
    parser.add_argument("--palette-map", action="append", default=[],
                        metavar="LOGICAL:BANK")
    parser.add_argument("--capture", action="append", required=True,
                        type=capture_arg)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    entries = []
    for tick, path in args.capture:
        raw = path.read_bytes()
        old_count, new_count = struct.unpack_from(">2H", raw,
                                                   args.count_offset)
        if not 0 < old_count < 64 or not 0 < new_count < 64:
            raise ValueError(f"{path}: invalid counts {old_count}/{new_count}")
        side_bytes = 64 * 6 * 2
        old_at = args.capture_offset
        new_at = old_at + side_bytes
        old = raw[old_at:old_at + old_count * 12]
        new = raw[new_at:new_at + new_count * 12]
        if len(old) != old_count * 12 or len(new) != new_count * 12:
            raise ValueError(f"{path}: truncated SDRAM dump")
        entries.append({
            "tick": tick,
            "old_state_hex": old.hex(),
            "new_state_hex": new.hex(),
        })

    manifest = {
        "format": "altbeast-smoke-transitions-v1",
        "key": args.key,
        "entries": entries,
    }
    if args.palette_map:
        palette_banks = {}
        for spec in args.palette_map:
            logical, separator, bank = spec.partition(":")
            if not separator:
                raise ValueError("palette map must be LOGICAL:BANK")
            palette_banks[str(number(logical))] = number(bank)
        manifest["palette_banks"] = palette_banks
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"{args.output}: {len(entries)} exact transition capture(s)")


if __name__ == "__main__":
    main()
