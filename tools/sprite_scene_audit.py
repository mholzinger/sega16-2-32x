#!/usr/bin/env python3
"""Compare native sprite pens with the port after explicit FG/text occlusion.

Inputs are already aligned source/pen arrays, not RGB guesses. Callers must prove
source identity, palette-bank mapping and the shown framebuffer bank separately.
The bottom calibration area is excluded by an explicit audited-height parameter.
"""
import numpy as np


def compare_scene(expected, owners, records, actual, bankpal, foreground, text,
                  audited_height=208, through_pen=255):
    shape = (224, 320)
    if any(a.shape != shape for a in (expected, owners, actual, foreground, text)):
        raise ValueError('expected native 320x224 arrays')
    if len(bankpal) != 16 or not 0 < audited_height <= 224:
        raise ValueError('bad bank mapping or audited height')
    region = np.zeros(shape, dtype=bool)
    region[:audited_height] = True
    occupied = region & (owners >= 0)
    text_cut = occupied & text
    covered = occupied & ~text & foreground
    visible = occupied & ~text & ~foreground
    opaque = (actual != 0) & (actual != through_pen)
    mapped = np.asarray(bankpal, dtype=np.int16)[actual >> 4] * 16 + (actual & 15)
    missing = visible & ~opaque
    wrong = visible & opaque & (mapped != expected)
    leaks = covered & opaque
    wrong_cut_pen = covered & (actual != through_pen)
    extras = region & (owners < 0) & ~text & opaque
    per_sprite = []
    for record in records:
        own = owners == record['slot']
        box = np.zeros(shape, dtype=bool)
        if record['box']:
            x0, y0, x1, y1 = record['box']
            box[y0:y1, x0:x1] = True
        per_sprite.append(dict(slot=record['slot'],
            visible=int((own & visible).sum()),
            foreground_covered=int((own & covered).sum()),
            text_covered=int((own & text_cut).sum()),
            outside_audited_rows=int((own & ~region).sum()),
            missing=int((own & missing).sum()),
            wrong_palette_or_pen=int((own & wrong).sum()),
            foreground_leaks=int((own & leaks).sum()),
            wrong_through_pen=int((own & wrong_cut_pen).sum()),
            extra_in_box=int((box & extras).sum())))
    totals = dict(visible=int(visible.sum()), foreground_covered=int(covered.sum()),
                  text_covered=int(text_cut.sum()), missing=int(missing.sum()),
                  wrong_palette_or_pen=int(wrong.sum()), foreground_leaks=int(leaks.sum()),
                  wrong_through_pen=int(wrong_cut_pen.sum()), extra=int(extras.sum()))
    passed = not any(totals[k] for k in ('missing', 'wrong_palette_or_pen',
                                       'foreground_leaks', 'wrong_through_pen', 'extra'))
    return dict(audited_height=audited_height, totals=totals, sprites=per_sprite, passed=passed)
