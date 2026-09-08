#!/usr/bin/env python3
"""Synthesize a boss-scene sprite census from ares savestates.

    python3 tools/boss_census.py rom/*.bs* > discover/boss.csv

The headless bots never transform, so the boss fight exists only in
Mike's savestates. SPR_SNAP (SDRAM 0x28400) carries the same w0-w7
fields the live census records, so the bake's key tuples can be
derived from states directly. Every state given on the command line
contributes; duplicate keys merge (count = records seen). The
'count' column is records-per-state-sample, NOT draws-per-frame —
the bake's boss table uses MIN_COUNT 1 (a key seen once in a
sampled moment is a key the fight uses).
"""
import struct
import sys
from pathlib import Path

# NO set filter (2026-09-01, Mike's three mid-fight states): the
# heads' silhouette animation is the game MARCHING the palette-set
# number through a contiguous range (0x1F..0x24 observed) — the set
# field is the phase counter, so keys are SET-AGNOSTIC (bake emits
# set 0xFF wildcards; the runtime's N-ary anchor follows the live
# set and ships its pens). Bank 2 = the head/debris art bank is the
# class identity; bank-3 set 0x22 is the BOSS BODY (15KB art, blows
# the VRAM window + line budget) and stays SH-2-composed.
BOSS_BANKS = (2,) if '--all-banks' not in sys.argv else tuple(range(16))


def swap16(b):
    return b''.join(b[i + 1:i + 2] + b[i:i + 1]
                    for i in range(0, len(b) & ~1, 2))


def main():
    keys = {}
    for path in sys.argv[1:]:
        if path.startswith('-'):
            continue
        d = Path(path).read_bytes()
        sd = 0x23B
        w = struct.unpack('>512H', swap16(d[sd + 0x28400:sd + 0x28800]))
        for i in range(64):
            e = w[i * 8:i * 8 + 8]
            if e[2] & 0x8000:
                break
            if e[2] & 0x4000:
                continue
            if (e[5] & 0x3FF) and '--with-zoom' not in sys.argv:
                continue                     # zoomed: SH-2 (SCALEBAKE
                                             # harvests them with
                                             # --with-zoom; zoom rides
                                             # in w5)
            if ((e[4] >> 8) & 0xF) not in BOSS_BANKS:
                continue
            top, bot = e[0] & 0xFF, e[0] >> 8
            if top >= bot or bot - top < 4:
                continue                     # degenerate slivers
            pitch = e[2] & 0xFF
            flip = (e[2] >> 8) & 1
            bank = (e[4] >> 8) & 0xF
            k = (f"{bank}_{e[3]:04X}_FFFF_{bot - top:03d}"
                 f"_z{e[5] & 0x3FF:03X}")    # set-agnostic + zoom
            if k in keys:
                keys[k]['count'] += 1
                continue
            keys[k] = {
                'key': k, 'bank': bank, 'addr': f"0x{e[3]:04X}",
                'pitch': pitch, 'flip': flip, 'height': bot - top,
                'rows_walked': bot - top, 'words': 0, 'bytes': 0,
                'native': 1, 'zoomed_seen': 0, 'count': 1,
                'first_frame': 0,
                'w0': f"0x{e[0]:04X}", 'w1': f"0x{e[1]:04X}",
                # w2 masked to pitch|flip: runtime marks (claim bit)
                # must never poison keys
                'w2': f"0x{e[2] & 0x1FF:04X}", 'w3': f"0x{e[3]:04X}",
                'w4': f"0x{e[4]:04X}", 'w5': f"0x{e[5]:04X}",
                'w6': f"0x{e[6]:04X}", 'w7': f"0x{e[7]:04X}",
            }
    cols = ('key,bank,addr,pitch,flip,height,rows_walked,words,bytes,'
            'native,zoomed_seen,count,first_frame,w0,w1,w2,w3,w4,w5,'
            'w6,w7').split(',')
    print(','.join(cols))
    for k in sorted(keys.values(), key=lambda x: -x['count']):
        print(','.join(str(k[c]) for c in cols))


if __name__ == '__main__':
    main()
