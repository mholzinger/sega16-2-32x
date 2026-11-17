#!/usr/bin/env python3
"""Replay a play tape headless on one or more roms, screenshot every STEP frames, scan the shots.

    python3 tools/tape_scan.py ROM [ROM2 ...] [--tape build/tapes/psg5_neff.csv] [--frames 24000]
        [--step 100] [--out DIR] [--rows 150:200] [--run 40]

Made 2026-11-05 when Mike's rig play pass found white lines in the lower third that no attract
run showed (the punch's through pen written white by a pool-only commit, r60_main.c). The Neff
tape (tools/ares_record.sh) reaches the same scenes headless: p15bpt showed the lines at 5 of
~200 sampled frames, p15bpt2 (the fix) at 0, so the tape replay IS an instrument for a play
defect, where the attract corpus is not.

Each rom is replayed in parallel with --input-gameframe keyed to its own _port_gf_count (the .elf
beside the rom). Scan: per frame, the count of pure-white pixels in ROWS and the longest pure-white
horizontal run there (a through pen left at 0x7FFF reads as a thin white line; the white wolf and
Neff's eye are legitimate runs, so LOOK at a hit: the sheet is written beside the shots).
Shots: OUT/<rom>/o_NNNNNN.png (raw ares 1415x243, crop with tools/active_area.py).
"""
import argparse
import glob
import os
import subprocess
import sys

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from parity_play import sym_sdram          # noqa: E402
from active_area import crop_active        # noqa: E402

ARES = os.path.expanduser('~/src/bin/ares-headless-ssf')


def replay(rom, tape, frames, step, out):
    gf = sym_sdram(rom, '_port_gf_count')
    if gf is None:
        raise SystemExit('tape_scan: %s has no _port_gf_count (.elf beside the rom?)' % rom)
    os.makedirs(out, exist_ok=True)
    cmd = [ARES, rom, '--frames', str(frames), '--input', tape, '--input-gameframe', 'sdram:0x%X' % gf,
           '--pad1', 'fighting']
    for f in range(1000, frames, step):
        cmd += ['--screenshot', '%d:%s/o_%06d.png' % (f, out, f)]
    log = open(os.path.join(out, 'ares.log'), 'w')
    return subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT)


def scan(path, y0, y1):
    a = np.asarray(crop_active(Image.open(path).convert('RGB')), dtype=np.int16)[y0:y1]
    w = (a[..., 0] >= 250) & (a[..., 1] >= 250) & (a[..., 2] >= 250)
    best = (0, 0)
    for y in range(w.shape[0]):
        run = 0
        for x in range(w.shape[1]):
            run = run + 1 if w[y, x] else 0
            if run > best[0]:
                best = (run, y0 + y)
    return int(w.sum()), best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('roms', nargs='+')
    ap.add_argument('--tape', default=os.path.join(ROOT, 'build', 'tapes', 'psg5_neff.csv'))
    ap.add_argument('--frames', type=int, default=24000)
    ap.add_argument('--step', type=int, default=100)
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'tapes', 'scan'))
    ap.add_argument('--rows', default='150:200', help='scanned rows y0:y1 (default: the play floor above the credit line)')
    ap.add_argument('--run', type=int, default=40, help='report frames whose longest white run is at least this')
    ap.add_argument('--scan-only', action='store_true')
    a = ap.parse_args()
    y0, y1 = (int(v) for v in a.rows.split(':'))
    names = [os.path.splitext(os.path.basename(r))[0] for r in a.roms]
    if not a.scan_only:
        procs = [replay(r, a.tape, a.frames, a.step, os.path.join(a.out, n)) for r, n in zip(a.roms, names)]
        for p, n in zip(procs, names):
            if p.wait() != 0:
                raise SystemExit('tape_scan: ares failed on %s, see %s/%s/ares.log' % (n, a.out, n))
    hits = {}
    for n in names:
        hits[n] = []
        for p in sorted(glob.glob(os.path.join(a.out, n, 'o_*.png'))):
            f = int(os.path.basename(p)[2:8])
            cnt, (run, y) = scan(p, y0, y1)
            if run >= a.run:
                hits[n].append((f, run, y, cnt))
        print('%s: %d frames with a white run >= %d px in rows %d..%d' % (n, len(hits[n]), a.run, y0, y1 - 1))
        for f, run, y, cnt in hits[n]:
            print('  frame %6d  run %3d px at row %d  (%d white px)' % (f, run, y, cnt))
    # a sheet of the hits' lower halves, three a row, for the eye
    for n in names:
        if not hits[n]:
            continue
        ims = [crop_active(Image.open(os.path.join(a.out, n, 'o_%06d.png' % f)).convert('RGB')).crop((0, 120, 320, 224))
               for f, _, _, _ in hits[n][:12]]
        rows = (len(ims) + 2) // 3
        sheet = Image.new('RGB', (3 * 324, rows * 108))
        for i, im in enumerate(ims):
            sheet.paste(im, ((i % 3) * 324, (i // 3) * 108))
        sp = os.path.join(a.out, n, 'hits.png')
        sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST).save(sp)
        print('  sheet %s' % sp)


if __name__ == '__main__':
    main()
