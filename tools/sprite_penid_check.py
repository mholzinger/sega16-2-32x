#!/usr/bin/env python3
"""Count sprite pens in an R60PENID+PORTSPRITEFIXTURE calibrated screenshot.

Accepts unscaled320x224 MiSTer PNGs or raw ares PNGs. The bottom16rows
are256 known framebuffer bytes in64columns of5x4 swatches. Refuse ambiguous
calibration instead of selecting the closest RGB color. Pixel counts apply
only to the explicit sprite slots and frozen-tick proof supplied by the caller.
"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image

from active_area import crop_active
from sprite_pixel_audit import reference, counts


def decode(image):
    im = Image.open(image).convert('RGB')
    if im.size not in ((320, 224), (1415, 243)):
        raise ValueError('expected native320x224 or known raw ares1415x243 capture')
    rgb = np.asarray(crop_active(im))
    colors = {}
    for index in range(256):
        if not 0 < index % 16 < 15:
            continue
        x, y = (index % 64) * 5, 208 + (index // 64) * 4
        cell = rgb[y:y + 4, x:x + 5]
        color = tuple(int(c) for c in cell[0, 0])
        if not np.all(cell == cell[0, 0]):
            raise ValueError(f'nonuniform calibration cell {index}')
        if color in colors:
            raise ValueError(f'calibration aliases pens {index}/{colors[color]}')
        colors[color] = index
    actual = np.zeros((224, 320), dtype=np.uint8)
    for color, index in colors.items():
        actual[np.all(rgb == color, axis=2)] = index
    return actual, colors


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ('image', 'table', 'rom', 'bankpal', 'out'):
        ap.add_argument('--' + name, type=Path, required=True)
    ap.add_argument('--slots', default='8,9,10,11')
    ap.add_argument('--alignment-evidence', required=True)
    args = ap.parse_args()
    actual, colors = decode(args.image)
    expected, owner, records = reference(args.table.read_bytes(), args.rom.read_bytes())
    selected = {int(x) for x in args.slots.split(',')}
    records = [r for r in records if r['slot'] in selected]
    if {r['slot'] for r in records} != selected:
        raise ValueError('requested slot absent in reference')
    if any(r['box'] and r['box'][3] > 208 for r in records):
        raise ValueError('calibration strip overlaps selected sprite')
    report = dict(scope='FROZEN_PENID_SELECTED_SPRITES_ONLY_NOT_CADENCE_OR_NORMAL_COLORS',
                  alignment_evidence=args.alignment_evidence, calibration_colors=len(colors),
                  inputs={name: dict(path=str(getattr(args, name)), sha256=hashlib.sha256(
                      getattr(args, name).read_bytes()).hexdigest())
                      for name in ('image', 'table', 'rom', 'bankpal')},
                  sprites=counts(expected, owner, actual,
                                 list(args.bankpal.read_bytes()), records))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
