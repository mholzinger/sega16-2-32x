#!/usr/bin/env python3
"""Render an arcade sound command with the real Z80 driver and write it as a VGZ. KIT (2026-11-02).
    python3 tools/seg2vgz.py PROG_ROM --samples s1 s2 --cmd 0xB2 --out DIR/sting_b2.vgz [--ticks 1500] [--tail 2.0]
The rips (srcref/audio) cover the eight songs; the jingle songs 0xB1/0xB2/0xD3 (Neff's entrance,
the transform sting, one more: SOUND_DRIVER.md:69) have no rip and empty tap captures, so they
come from tools/snd_render.py (the driver executed for real) through tools/render_music.py's
event form, written as a VGM 1.50 with the YM2151 at 4 MHz so the whole existing chain
(tools/psg_bake.py, tools/build_vgm_music.py, tools/opm_census.py) takes them like a rip.
The stream is cut --tail seconds after the last YM write (a one-shot: no loop point).
"""
import argparse
import gzip
import os
import struct
import sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from snd_render import render                # noqa: E402


def render_seg(prog, samples, cmd, ticks, ym_clock):
    """tools/render_music.py's event form (its render_cmd goes on to transcode): YM writes in seconds"""
    board = render(prog, samples, cmd, ticks, ym_clock)
    ta = board.ta or 1
    period_s = 64 * (1024 - ta) / ym_clock
    denom = 64 * (1024 - ta)
    ym = []
    addr = 0
    for cyc, kind, val in board.events:
        if kind == 'Y0':
            addr = val
        elif kind == 'Y1':
            ym.append(((cyc / denom) * period_s, addr, val))
    return {'ym': ym}

RATE = 44100


def write_vgm(events, clock, total_s, path):
    """events: [(t_seconds, reg, val)] sorted; one-shot (no loop)"""
    body = bytearray()
    cur = 0
    for t, reg, val in events:
        s = int(round(t * RATE))
        while s > cur:
            d = min(s - cur, 65535)
            body += struct.pack('<BH', 0x61, d)
            cur += d
        body += bytes([0x54, reg & 0xFF, val & 0xFF])
    end = int(round(total_s * RATE))
    while end > cur:
        d = min(end - cur, 65535)
        body += struct.pack('<BH', 0x61, d)
        cur += d
    body += b'\x66'
    hdr = bytearray(0x40)
    hdr[0:4] = b'Vgm '
    struct.pack_into('<I', hdr, 0x04, 0x40 + len(body) - 4)      # EOF offset
    struct.pack_into('<I', hdr, 0x08, 0x150)                      # version
    struct.pack_into('<I', hdr, 0x18, cur)                        # total samples
    struct.pack_into('<I', hdr, 0x1C, 0)                          # no loop
    struct.pack_into('<I', hdr, 0x24, 60)                         # rate
    struct.pack_into('<I', hdr, 0x30, clock)                      # YM2151 clock
    struct.pack_into('<I', hdr, 0x34, 0x40 - 0x34)                # data offset -> 0x40
    with gzip.open(path, 'wb') as f:
        f.write(bytes(hdr) + bytes(body))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('prog')
    ap.add_argument('--samples', nargs='*', default=[])
    ap.add_argument('--cmd', required=True)
    ap.add_argument('--ticks', type=int, default=1500)
    ap.add_argument('--ym-clock', type=int, default=4000000)
    ap.add_argument('--tail', type=float, default=2.0, help='seconds kept after the last YM write')
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    prog = open(a.prog, 'rb').read()
    samples = [open(s, 'rb').read() for s in a.samples]
    cmd = int(a.cmd, 16)
    seg = render_seg(prog, samples, cmd, a.ticks, a.ym_clock)
    ev = sorted(seg['ym'], key=lambda e: e[0])
    keyons = [t for t, r, v in ev if r == 0x08 and (v & 0x78)]
    if not ev:
        raise SystemExit('seg2vgz: command 0x%02X produced no YM writes' % cmd)
    last = ev[-1][0]
    write_vgm(ev, a.ym_clock, last + a.tail, a.out)
    print('0x%02X: %d YM writes, %d key-ons, last write at %.2f s, first key-on %s -> %s' % (
        cmd, len(ev), len(keyons), last, ('%.2f s' % keyons[0]) if keyons else 'none', a.out))


if __name__ == '__main__':
    main()
