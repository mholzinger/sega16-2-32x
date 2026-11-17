#!/usr/bin/env python3
"""Per-channel music levels: the arcade driver, the VGM rip and the cart's stream.

    python3 tools/music_levels.py --cmd 0x94 --vgz "srcref/audio/.../01 Rise....vgz" \
        [--prog roms/altbeast/epr-11671.a10 --samples ...] [--seconds 22]

WHY (2026-10-30). Balancing the lab's music by ear kept failing (Mike: "really torn"). The
channel levels are in the data, so they can be measured instead of guessed. For each of the
arcade's 8 YM2151 channels this reports the average carrier level while a note is held, in dB
(0 = TL 0), from three sources over the same window:

  DRIVER  the arcade's own sound driver run in tools/snd_render.py (sound-test levels)
  RIP     the VGM rip, a hardware log of the arcade in play (in-context mix)
  CART    the sndtest stream (render_music.h rin_<cmd>), after opm2opn and the gag correction;
          channels 0-5 on YM2612 FM, 6-7 on PSG squares (2 dB attenuation steps)

Level = 20*log10 of the time-weighted mean linear amplitude of the loudest carrier
(TL * 0.75 dB) over the time the channel is keyed. Envelopes are ignored: the same patches
play in all three, so they cancel in a comparison. OPM and OPN carriers share one table when
indexed in register order (M1 M2 C1 C2 / OP1 OP3 OP2 OP4), as md_main.c's CARRIER_MASK does.
"""
import argparse
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vgm2ym import load                      # noqa: E402
from lzss import decompress                  # noqa: E402

CARRIER_MASK = [0x8, 0x8, 0x8, 0x8, 0xC, 0xE, 0xE, 0xF]


class Meter:
    """Time-weighted carrier amplitude per channel while keyed."""
    def __init__(self, n):
        self.amp_t = [0.0] * n
        self.key_t = [0.0] * n
        self.keyed = [False] * n
        self.db = [None] * n                 # current carrier level in dB
        self.t = 0.0

    def advance(self, t):
        dt = t - self.t
        if dt > 0:
            for c in range(len(self.keyed)):
                if self.keyed[c] and self.db[c] is not None:
                    self.amp_t[c] += dt * 10 ** (self.db[c] / 20.0)
                    self.key_t[c] += dt
            self.t = t

    def result(self):
        return [(20 * math.log10(a / k) if k > 0 and a > 0 else None, k)
                for a, k in zip(self.amp_t, self.key_t)]


def opm_levels(events, seconds):
    """events: (t, reg, val) YM2151 writes."""
    m = Meter(8)
    tl = [[127] * 4 for _ in range(8)]
    con = [0] * 8

    def level(c):
        mask = CARRIER_MASK[con[c]]
        return -0.75 * min(tl[c][s] for s in range(4) if mask & (1 << s))
    for t, reg, val in events:
        if t > seconds:
            break
        m.advance(t)
        if 0x60 <= reg <= 0x7F:
            c, s = reg & 7, (reg - 0x60) >> 3
            tl[c][s] = val & 0x7F
            m.db[c] = level(c)
        elif 0x20 <= reg <= 0x27:
            c = reg & 7
            con[c] = val & 7
            m.db[c] = level(c)
        elif reg == 0x08:
            c = val & 7
            m.keyed[c] = bool((val >> 3) & 0xF)
            m.db[c] = level(c)
    m.advance(seconds)
    return m.result()


def cart_levels(stream, seconds):
    """The Z80 player's opcodes: <0x80 wait ms, F0/F1 reg val (YM part I/II), F2 psg, FF end."""
    m = Meter(8)                              # 0-5 FM, 6-7 the PSG squares
    tl = [[127] * 4 for _ in range(6)]
    alg = [0] * 6
    psg_latch = 0
    t = 0.0

    def level(c):
        mask = CARRIER_MASK[alg[c]]
        return -0.75 * min(tl[c][s] for s in range(4) if mask & (1 << s))
    i = 0
    while i < len(stream) and t <= seconds:
        b = stream[i]
        if b < 0x80:
            t += b / 1000.0
            m.advance(t)
            i += 1
        elif b in (0xF0, 0xF1):
            part = 0 if b == 0xF0 else 3
            reg, val = stream[i + 1], stream[i + 2]
            i += 3
            if 0x40 <= reg <= 0x4F and (reg & 3) < 3:
                c, s = part + (reg & 3), (reg - 0x40) >> 2
                tl[c][s] = val & 0x7F
                m.db[c] = level(c)
            elif 0xB0 <= reg <= 0xB2:
                c = part + (reg - 0xB0)
                alg[c] = val & 7
                m.db[c] = level(c)
            elif reg == 0x28 and part == 0:
                c = val & 7
                c = c - 1 if c >= 4 else c
                if c < 6:
                    m.keyed[c] = bool(val & 0xF0)
                    m.db[c] = level(c)
        elif b == 0xF2:
            v = stream[i + 1]
            i += 2
            if v & 0x80:
                psg_latch = (v >> 5) & 3
                if v & 0x10 and psg_latch < 2:            # attenuation latch, tone 0/1
                    att = v & 0x0F
                    c = 6 + psg_latch
                    m.keyed[c] = att < 15
                    m.db[c] = -2.0 * att
        elif b == 0xFF:
            break
        else:
            i += 1
    m.advance(seconds)
    return m.result()


def header_array(path, name):
    txt = open(path).read()
    mm = re.search(r'%s\[\d+\]\s*=\s*\{(.*?)\};' % re.escape(name), txt, re.S)
    if not mm:
        raise SystemExit('music_levels: %s not in %s' % (name, path))
    return bytes(int(x, 0) for x in re.findall(r'0x[0-9a-fA-F]+|\d+', mm.group(1)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cmd', type=lambda x: int(x, 0), default=0x94)
    ap.add_argument('--vgz', required=True)
    ap.add_argument('--prog', default='roms/altbeast/epr-11671.a10')
    ap.add_argument('--samples', nargs='*', default=['roms/altbeast/opr-11672.a11',
                                                     'roms/altbeast/opr-11673.a12'])
    ap.add_argument('--seconds', type=float, default=22.0)
    ap.add_argument('--header', default='sndtest/md/render_music.h')
    ap.add_argument('--no-driver', action='store_true', help='skip the slow Z80 run')
    a = ap.parse_args()

    seg, loop_t, _ = load(a.vgz)
    rip = opm_levels(seg['ym'], a.seconds)
    cart = cart_levels(decompress(header_array(a.header, 'rin_%02x' % a.cmd)), a.seconds)
    drv = None
    if not a.no_driver:
        from snd_render import render
        board = render(open(a.prog, 'rb').read(), [open(s, 'rb').read() for s in a.samples],
                       a.cmd, int(a.seconds / 0.00838) + 20, 4000000)
        per = 64 * (1024 - board.ta) / 4000000.0
        den = 64 * (1024 - board.ta)
        ev, addr = [], 0
        for cyc, kind, val in board.events:
            if kind == 'Y0':
                addr = val
            elif kind == 'Y1':
                ev.append(((cyc / den) * per, addr, val))
        drv = opm_levels(ev, a.seconds)

    print('cmd 0x%02X, first %.0f s (rip loop point %.1f s). dB = mean carrier level while keyed;'
          ' (kept s) = seconds keyed' % (a.cmd, a.seconds, loop_t or 0))
    print('%-4s %-16s %-16s %-16s %s' % ('ch', 'DRIVER', 'RIP', 'CART', 'cart - driver'))
    fmt = lambda r: ('%6.1f (%4.1fs)' % (r[0], r[1])) if r[0] is not None else '   -   (    -)'
    for c in range(8):
        d = drv[c] if drv else (None, 0)
        diff = ('%+5.1f dB' % (cart[c][0] - d[0])) if (cart[c][0] is not None and d[0] is not None) else ''
        print('%-4s %-16s %-16s %-16s %s' % ('%d%s' % (c, ' psg' if c >= 6 else ''),
                                            fmt(d), fmt(rip[c]), fmt(cart[c]), diff))


if __name__ == '__main__':
    main()
