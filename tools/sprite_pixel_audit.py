#!/usr/bin/env python3
"""Audit unzoomed System16B sprite pens, never RGB thumbnail similarity.

Reference inputs are arcade sprite RAM and the interleaved sprites.bin ROM.
This first instrument deliberately rejects zoom and shadow/highlight records.
Row/word rules: jts16_obj_scan.v:78-80, jts16_obj_draw.v:70-115;
independent existing decoder: tools/art_census.py:39-56.

The caller MUST supply the hardware-selected bank, dot shift, and evidence of
tick alignment. This tool does not infer those from whichever image fits best.
Raw mismatches include legitimate foreground/text occlusion: they are NOT a
defect verdict. Exported owner/pen maps permit that classification separately.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

import numpy as np


def drawing_fingerprint(table):
    """Drawing-table fingerprint; full drawing words still need independent proof."""
    h = 2166136261
    for slot in range(min(64, len(table) // 16)):
        e = struct.unpack_from('>8H', table, slot * 16)
        if e[2] & 0x8000:
            break
        if e[2] & 0x4000 or (e[0] & 255) >= (e[0] >> 8):
            continue
        for value in (slot, *e[:6]):
            h = ((h ^ value) * 16777619) & 0xffffffff
    return h


def reference(table, rom):
    if len(table) not in (1024, 2048) or len(rom) != 0x100000:
        raise ValueError('expected 1/2 KiB sprite table and 1 MiB interleaved ROM')
    words = np.frombuffer(rom, dtype='>u2')
    pens = np.full((224, 320), -1, dtype=np.int16)
    owner = np.full((224, 320), -1, dtype=np.int16)
    records = []
    for slot in range(len(table) // 16):
        e = struct.unpack_from('>8H', table, slot * 16)
        if e[2] & 0x8000:
            break
        top, bottom = e[0] & 255, e[0] >> 8
        if e[2] & 0x4000 or top >= bottom:
            continue
        if e[5] & 0x3ff or e[4] & 63 == 63:
            raise ValueError(f'slot {slot}: zoom or shadow requires a separate verified decoder')
        x = (e[1] & 511) - 184
        pitch = (e[2] & 127) - (e[2] & 128)
        flip = bool(e[2] & 256)
        bank = ((e[4] >> 8) & 7) * 65536
        palette = e[4] & 63
        addr = e[3]
        coords = []
        for y in range(top, min(bottom, 224)):
            addr = (addr + pitch) & 65535
            col = x
            for n in range(256):
                word = int(words[bank + ((addr + (-n if flip else n)) & 65535)])
                shifts = (0, 4, 8, 12) if flip else (12, 8, 4, 0)
                for shift in shifts:
                    pen = (word >> shift) & 15
                    if 0 <= col < 320 and 0 < pen < 15:
                        pens[y, col] = palette * 16 + pen
                        owner[y, col] = slot
                        coords.append((col, y))
                    col += 1
                if ((word >> shifts[-1]) & 15) == 15:
                    break
            else:
                raise ValueError(f'slot {slot} row {y}: no end marker in 256 words')
        box = None
        if coords:
            xs, ys = zip(*coords)
            box = [min(xs), min(ys), max(xs) + 1, max(ys) + 1]
        records.append(dict(slot=slot, words=list(e[:6]), box=box,
                            emitted_pixels=len(coords), priority=(e[4] >> 6) & 3))
    return pens, owner, records


def framebuffer(dram, bank, shift):
    if len(dram) != 0x40000 or bank not in (0, 1) or shift not in (0, 1):
        raise ValueError('expected two 128 KiB physical banks, explicit bank 0/1 and shift 0/1')
    data = dram[bank * 0x20000:(bank + 1) * 0x20000]
    rows = []
    for y in range(224):
        start = struct.unpack_from('>H', data, y * 2)[0] * 2 + shift
        if start + 320 > len(data):
            raise ValueError(f'line {y}: framebuffer line table exceeds bank')
        rows.append(np.frombuffer(data[start:start + 320], dtype=np.uint8))
    return np.stack(rows)


def counts(expected, owner, actual, bankpal, records):
    if len(bankpal) != 16:
        raise ValueError('bank-to-System16-palette mapping must have exactly 16 bytes')
    mapped = np.asarray(bankpal, dtype=np.int16)[actual >> 4] * 16 + (actual & 15)
    # Pens 0/15 are reserved through/text; 255 is the foreground punch.
    sprite = ((actual & 15) > 0) & ((actual & 15) < 15)
    matched = sprite & (mapped == expected)
    result = []
    for record in records:
        mask = owner == record['slot']
        absent = mask & ~sprite
        wrong = mask & sprite & ~matched
        extra = np.zeros_like(mask)
        if record['box']:
            x0, y0, x1, y1 = record['box']
            extra[y0:y1, x0:x1] = True
        extra &= (owner < 0) & sprite
        result.append(dict(record, visible_pixels=int(mask.sum()),
                           matched=int((mask & matched).sum()),
                           missing_or_occluded=int(absent.sum()),
                           wrong_palette_or_pen=int(wrong.sum()),
                           extra_in_box=int(extra.sum()),
                           punched=int((mask & (actual == 255)).sum())))
    return result


def native_image_check(expected, owner, records, palette, image):
    """Independent MAME screen check of reference geometry and normal colors.

    Derive channel weights from the resistor network documented in
    srcref/jtcores/cores/s16/doc/segaic16.cpp:400-440. RGB agreement alone
    cannot distinguish two identical-color pens; the port audit still uses pens.
    """
    from PIL import Image
    if len(palette) != 4096:
        raise ValueError('expected 4096-byte arcade palette')
    actual = np.asarray(Image.open(image).convert('RGB'))
    if actual.shape != (224, 320, 3):
        raise ValueError('native screenshot must be unscaled 320x224')
    conductance = np.asarray([1 / 3900, 1 / 2000, 1 / 1000, 1 / 500, 1 / 250])
    weights = conductance / conductance.sum() * 255
    levels = np.asarray([int(sum(weights[k] * ((v >> k) & 1)
                                for k in range(5)) + .5) for v in range(32)])
    palette_words = np.frombuffer(palette, dtype='>u2').astype(np.uint32)
    v = palette_words[1024 + np.maximum(expected, 0)]
    red = ((v >> 12) & 1) | ((v << 1) & 30)
    green = ((v >> 13) & 1) | ((v >> 3) & 30)
    blue = ((v >> 14) & 1) | ((v >> 7) & 30)
    rgb = np.stack([levels[red], levels[green], levels[blue]], axis=2)
    equal = np.all(rgb == actual, axis=2)
    return [dict(slot=r['slot'], visible_pixels=int((owner == r['slot']).sum()),
                 rgb_equal=int(((owner == r['slot']) & equal).sum())) for r in records]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ('table', 'rom', 'dram', 'bankpal', 'out'):
        ap.add_argument('--' + name, type=Path, required=True)
    ap.add_argument('--bank', type=int, choices=(0, 1), required=True)
    ap.add_argument('--shift', type=int, choices=(0, 1), required=True)
    ap.add_argument('--alignment-evidence', required=True,
                    help='description/path of independent same-tick and shown-bank proof')
    ap.add_argument('--arcade-palette', type=Path)
    ap.add_argument('--arcade-image', type=Path)
    args = ap.parse_args()
    table, rom = args.table.read_bytes(), args.rom.read_bytes()
    expected, owner, records = reference(table, rom)
    actual = framebuffer(args.dram.read_bytes(), args.bank, args.shift)
    report = dict(verdict='RAW_COUNTS_ONLY_OCCLUSION_AND_ALIGNMENT_REQUIRE_VERIFICATION',
                  alignment_evidence=args.alignment_evidence,
                  drawing_fingerprint=f'0x{drawing_fingerprint(table):08x}',
                  bank=args.bank, shift=args.shift,
                  inputs={n: dict(path=str(getattr(args, n)), sha256=hashlib.sha256(
                      getattr(args, n).read_bytes()).hexdigest())
                      for n in ('table', 'rom', 'dram', 'bankpal')},
                  sprites=counts(expected, owner, actual,
                                 list(args.bankpal.read_bytes()), records))
    if bool(args.arcade_palette) != bool(args.arcade_image):
        ap.error('--arcade-palette and --arcade-image must be supplied together')
    if args.arcade_palette:
        report['native_reference_rgb_check'] = native_image_check(
            expected, owner, records, args.arcade_palette.read_bytes(), args.arcade_image)
        for name in ('arcade_palette', 'arcade_image'):
            path = getattr(args, name)
            report['inputs'][name] = dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    args.out.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(args.out / 'pixels.npz', expected=expected, owner=owner, actual=actual)
    (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report['sprites'], indent=2))


if __name__ == '__main__':
    main()
