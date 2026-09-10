#!/usr/bin/env python3
"""Per-band sprite palette census (docs/log/LOOP-DECOMPILE.md 7).

Re-runs the LOOP29 126 band count and splits each band's colour sets into
the ones that carry a real palette and the ones that carry a single
colour, so the "collapse the trivial palettes" idea from
docs/log/LOOP-DECOMPILE.md 5 can be costed.

    tools/palette_bands.py rom/s16.32x --frames 1800,2600,3000,3400,3800

Sprite records are read from the game's own sprite RAM at 0xFF7000: 128
records of 8 words, Y span = w[0] low byte top, high byte bottom, colour
set = w[4] & 0x3F. That is the same decode LOOP29 126 used. The colour
set is the SLOT the allocator handed out, not the palette identity, so
the slot is mapped back through the request table at 0xFFF500 to reach
the palette_index whose colours can then be counted
(docs/log/LOOP-DECOMPILE.md 2 and 3).
"""
import argparse
import collections
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from palette_demand import load_rom, palette, live_set, ROM, PAL_COUNT  # noqa: E402

ARES = os.environ.get(
    'ARES',
    '/Users/mikeholzinger/src/ares-debug/build_macos/headless-ui/Release/ares-headless')

SPRITE_BASE = 0xFF7000
SPRITE_LEN = 0x800          # 128 records of 16 bytes
SPRITE_RECORDS = 128
LINES = 224
BAND = 28                   # LOOP29 126's band height
BANDS = LINES // BAND


def run_ares(rom, frames, inputs, outdir):
    spr = os.path.join(outdir, 'spr_%d.bin' % frames)
    req = os.path.join(outdir, 'req_%d.bin' % frames)
    cmd = [ARES, '--frames', str(frames), '--input', inputs,
           '--dump', 'wram:0x%X:0x%X:%s' % (SPRITE_BASE, SPRITE_LEN, spr),
           '--dump', 'wram:0xFFF400:0x200:%s' % req,
           rom]
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if r.returncode != 0:
        sys.exit('ares failed at frame %d:\n%s' % (frames, r.stdout.decode()[-2000:]))
    return spr, req


def records(blob):
    """Yield (top, bottom, slot) for every live sprite record."""
    for n in range(SPRITE_RECORDS):
        o = n * 16
        w = [(blob[o + 2 * k] << 8) | blob[o + 2 * k + 1] for k in range(8)]
        if w[0] == 0xFFFF:
            break
        top = w[0] & 0xFF
        bottom = (w[0] >> 8) & 0xFF
        if top >= bottom:
            continue
        yield top, bottom, w[4] & 0x3F


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom')
    ap.add_argument('--frames', default='1800,2600,3000,3400,3800')
    ap.add_argument('--input', default=os.path.join(ROOT, 'discover/inputs/play_level1.csv'))
    ap.add_argument('--trivial-max', type=int, default=2)
    ap.add_argument('--keep', help='directory for the dumps (default: temporary)')
    a = ap.parse_args()

    rom = load_rom(ROM)
    sets = [frozenset(palette(rom, i)) for i in range(PAL_COUNT)]

    outdir = a.keep or tempfile.mkdtemp(prefix='palbands_')
    if a.keep and not os.path.isdir(a.keep):
        os.makedirs(a.keep)

    frames = [int(f) for f in a.frames.split(',')]
    print('band height %d lines, %d bands; trivial = at most %d distinct colours'
          % (BAND, BANDS, a.trivial_max))
    print()
    print('frame |' + ''.join(' b%d   ' % b for b in range(BANDS)) + '| frame')
    print('      |' + ' real/triv ' * 0 + '(real+trivial per band)'.ljust(6 * BANDS - 1)
          + '|')

    worst_real = 0
    trivial_seen = collections.Counter()
    for f in frames:
        spr, req = run_ares(a.rom, f, a.input, outdir)
        with open(spr, 'rb') as fh:
            blob = fh.read()
        with open(req, 'rb') as fh:
            allocated, _ = live_set(fh.read())
        slot_to_index = {}
        for idx, slot in allocated.items():
            slot_to_index[slot] = idx

        band_sets = [set() for _ in range(BANDS)]
        for top, bottom, slot in records(blob):
            for b in range(BANDS):
                if top < (b + 1) * BAND and bottom > b * BAND:
                    band_sets[b].add(slot)

        cells = []
        frame_real = set()
        frame_triv = set()
        for b in range(BANDS):
            real = triv = 0
            for slot in band_sets[b]:
                idx = slot_to_index.get(slot)
                if idx is None:
                    continue
                if len(sets[idx]) <= a.trivial_max:
                    triv += 1
                    frame_triv.add(idx)
                    trivial_seen[idx] += 1
                else:
                    real += 1
                    frame_real.add(idx)
            cells.append('%d+%d' % (real, triv))
            worst_real = max(worst_real, real)
        print('%5d |' % f + ''.join(' %-5s' % c for c in cells)
              + '| %d+%d' % (len(frame_real), len(frame_triv)))

    print()
    print('worst BAND real colour sets: %d' % worst_real)
    if trivial_seen:
        print('trivial palettes appearing in a band: %s'
              % ', '.join('0x%02X x%d' % (i, c) for i, c in sorted(trivial_seen.items())))
    else:
        print('trivial palettes appearing in a band: none')
    if not a.keep:
        print('(dumps in %s)' % outdir)


if __name__ == '__main__':
    main()
