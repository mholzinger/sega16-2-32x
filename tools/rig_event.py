#!/usr/bin/env python3
"""Decode raw event-footer words without interpreting modular FRT sums as time.

Only native320x224 and raw ares1415x243 captures are accepted. Page/phase/field
markers and duplicate rows must agree. Visible telemetry remains frozen when
presentation stops; it cannot distinguish a stopped renderer from stopped logic.
"""
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image
from active_area import crop_active


def _bits(im, y, origin, width, count):
    value = 0
    for bit in range(count):
        # Interior samples exclude either boundary and tolerate a1px shift.
        offsets = (1, 2) if width == 4 else (2, 3, 4, 5)
        pixels = [im.getpixel((origin + bit * width + x, y)) for x in offsets]
        if any(p not in ((0, 0, 0), (255, 255, 255)) for p in pixels):
            raise ValueError("event footer uses an unexpected RGB color")
        if len(set(pixels)) != 1:
            raise ValueError("mixed event footer bit")
        value = (value << 1) | (pixels[0] == (255, 255, 255))
    return value


def read(path):
    im = Image.open(path).convert("RGB")
    if im.size not in ((320, 224), (1415, 243)):
        raise ValueError(f"unrecognized capture geometry: {im.size}")
    im = crop_active(im)
    values, markers = [], []
    page = None
    for row, y in enumerate((188, 190, 192, 194)):
        copies = []
        for yy in (y, y + 1):
            value = _bits(im, yy, 0, 8, 32)
            marker = _bits(im, yy, 256, 4, 16)
            p = (marker >> 8) & 15
            expected = 0xD000 | p << 8 | (1 + row // 2) << 4 | row % 2
            if p > 4 or marker != expected:
                raise ValueError("invalid event footer page/phase/field marker")
            if page is None:
                page = p
            if p != page:
                raise ValueError("event footer pages disagree")
            copies.append((value, marker))
        if copies[0] != copies[1]:
            raise ValueError("event footer duplicate rows disagree")
        values.append(copies[0][0])
        markers.append(copies[0][1])
    return dict(page=page, raw32_values=values, markers=markers,
                entrance=dict(phase=1, fields=values[:2]),
                death=dict(phase=2, fields=values[2:]))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("images", nargs="+", type=Path)
    ap.add_argument("--out", type=Path)
    args = ap.parse_args()
    rows = [dict(path=str(p), **read(p), sha256=hashlib.sha256(p.read_bytes()).hexdigest())
            for p in args.images]
    result = json.dumps(dict(scope=__doc__.strip(), samples=rows), indent=2) + "\n"
    if args.out:
        args.out.write_text(result)
    else:
        print(result, end="")


if __name__ == "__main__":
    main()
