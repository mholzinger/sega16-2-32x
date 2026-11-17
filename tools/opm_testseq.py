#!/usr/bin/env python3
"""A deterministic YM2151 test sequence, MDFourier-style, as a VGZ plus a manifest. KIT.

    python3 tools/opm_testseq.py [--channel 0] [--out build/testseq/opm_testseq]
      -> OUT.vgz   YM2151 at 4 MHz (System 16B), one channel, every test in turn
         OUT.json  the manifest: each test's name, group, window [t0, t1) in seconds,
                   and the parameter it varies

WHY (2026-10-31, mdfourier 1). Music hides what a test tone shows: a feedback voice masks
a quiet one, a 2.7-cent pitch slip vanishes in an octave band, an envelope that decays a
step early is a 0.4 dB window somewhere. MDFourier's method (Artemio Urbina) is a fixed
stimulus played on every device, aligned on a sync pulse, compared bin by bin. This is
that stimulus for the arcade sound chain: the same VGZ renders through jt51 (the oracle),
sndtest/opm (the synth), channel_ab/ymfm, and the cart's own transcode pipeline
(vgm2ym.load -> build_vgm_music), so each stage is measured on identical input.

The tests, each a note on ONE channel (--channel) with every other register quiet:
  sync      two 50 ms clicks at the start and one at the end: alignment marks
  alg       the 8 algorithms on a fixed 4-op patch (M1/C1/M2/C2 TL 16/12/12/0)
  fb        feedback 0-7 on a lone M1 (algorithm 7, the other ops silent)
  ar/d1r/d1l/d2r/rr  one envelope parameter swept, the others pinned
  ks        key scaling 0-3 at a high note with a slow decay
  mul/dt1/dt2/kc/kf  pitch: multiplier, both detunes, the 12 notes of octave 4, KF
No LFO and no noise: Altered Beast never sets them (census 2026-10-30). Compare windows
are the manifest's [t0, t1); a write of TL 127 to every op closes each window so a slow
release cannot bleed into the next test.
"""
import argparse
import gzip
import json
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from channel_ab import vgm_header, finish      # noqa: E402

RATE = 44100
CLOCK = 4000000
SLOT = (0, 16, 8, 24)                          # M1, C1, M2, C2 register offsets
KEY = (0x08, 0x10, 0x20, 0x40)                 # key-on bits in the same order
A4 = 0x4A                                      # KC: octave 4, note A


class Seq:
    def __init__(self, ch):
        self.ch = ch
        self.ev = []                           # (sample, reg, val)
        self.tests = []
        self.t = 0.0

    def w(self, reg, val, t=None):
        self.ev.append((int(round((self.t if t is None else t) * RATE)), reg, val))

    def op(self, n, dt1=0, mul=1, tl=0, ks=0, ar=31, d1r=0, d2r=0, dt2=0, d1l=0, rr=15, t=None):
        s = SLOT[n] + self.ch
        self.w(0x40 + s, (dt1 << 4) | mul, t)
        self.w(0x60 + s, tl, t)
        self.w(0x80 + s, (ks << 6) | ar, t)
        self.w(0xA0 + s, d1r, t)
        self.w(0xC0 + s, (dt2 << 6) | d2r, t)
        self.w(0xE0 + s, (d1l << 4) | rr, t)

    def note(self, kc=A4, kf=0, t=None):
        self.w(0x28 + self.ch, kc, t)
        self.w(0x30 + self.ch, kf << 2, t)

    def con(self, alg, fb=0):
        self.w(0x20 + self.ch, 0xC0 | (fb << 3) | alg)

    def play(self, name, group, param, keymask, hold, gap, **extra):
        """key on now, off after `hold`, close the window after `gap`."""
        t0 = self.t
        self.w(0x08, keymask | self.ch)
        self.w(0x08, self.ch, t0 + hold)
        for n in range(4):                      # close the window: silence whatever remains
            self.w(0x60 + SLOT[n] + self.ch, 127, t0 + hold + gap - 0.02)
        self.tests.append(dict(name=name, group=group, param=param, t0=round(t0, 4),
                               t1=round(t0 + hold + gap, 4), hold=hold, **extra))
        self.t = t0 + hold + gap

    def quiet(self):
        for n in range(4):
            self.op(n, tl=127)
        self.w(0x08, self.ch)


def build(ch):
    s = Seq(ch)
    s.w(0x0F, 0)                               # noise off
    s.w(0x18, 0); s.w(0x19, 0); s.w(0x19, 0x80)   # LFO rate 0, AM and PM depth 0
    s.w(0x14, 0)
    s.quiet()
    s.t = 0.5

    def click(name):
        s.con(7); s.op(0, mul=2); s.op(1, tl=127); s.op(2, tl=127); s.op(3, tl=127)
        s.note(A4)
        s.play(name, 'sync', None, KEY[0], 0.05, 0.15)
    click('sync0'); click('sync1')
    s.t += 0.5

    # algorithms: a fixed 4-op patch
    for alg in range(8):
        s.con(alg)
        s.op(0, tl=16); s.op(1, tl=12); s.op(2, tl=12); s.op(3, tl=0)
        s.note(A4)
        s.play('alg%d' % alg, 'alg', alg, 0x78, 0.5, 0.3)

    # the rest is one operator: M1 as a carrier (algorithm 7), the others silent
    def solo(name, group, param, hold=0.4, gap=0.2, kc=A4, kf=0, alg=7, fb=0, **op):
        s.con(alg, fb)
        s.op(0, **op); s.op(1, tl=127); s.op(2, tl=127); s.op(3, tl=127)
        s.note(kc, kf)
        s.play(name, group, param, KEY[0], hold, gap, kc=kc, kf=kf)

    for fb in range(8):
        solo('fb%d' % fb, 'fb', fb, 0.5, 0.3, fb=fb)
    for ar in (2, 6, 10, 14, 18, 22, 26, 31):
        solo('ar%02d' % ar, 'ar', ar, ar=ar)
    for d1r in (4, 8, 12, 16, 20, 24):
        solo('d1r%02d' % d1r, 'd1r', d1r, 0.6, 0.2, d1r=d1r, d1l=15)
    for d1l in (0, 2, 4, 8, 12, 15):
        solo('d1l%02d' % d1l, 'd1l', d1l, 0.5, 0.2, d1r=20, d1l=d1l)
    for d2r in (4, 8, 12, 16):
        solo('d2r%02d' % d2r, 'd2r', d2r, 0.6, 0.2, d1r=31, d1l=2, d2r=d2r)
    for rr in (2, 4, 6, 8, 10, 15):
        solo('rr%02d' % rr, 'rr', rr, 0.2, 0.8, rr=rr)
    for ks in range(4):
        solo('ks%d' % ks, 'ks', ks, 0.5, 0.2, kc=0x6A, ks=ks, d1r=12, d1l=15)
    for mul in range(16):
        solo('mul%02d' % mul, 'mul', mul, 0.25, 0.1, kc=0x3A, mul=mul)
    for dt1 in range(8):
        solo('dt1_%d' % dt1, 'dt1', dt1, 0.3, 0.1, dt1=dt1)
    for dt2 in range(4):
        solo('dt2_%d' % dt2, 'dt2', dt2, 0.3, 0.1, dt2=dt2)
    for kc in [n for n in range(16) if n & 3 != 3]:
        solo('kc%02d' % kc, 'kc', kc, 0.25, 0.1, kc=0x40 | kc)
    for kf in (0, 16, 32, 48):
        solo('kf%02d' % kf, 'kf', kf, 0.3, 0.1, kf=kf)
    s.t += 0.3
    click('sync_end')
    s.quiet()
    return s


def to_vgm(ev):
    ev = sorted(ev, key=lambda e: e[0])         # stable: same-time writes keep their order
    body = bytearray()
    t = 0
    for samp, reg, val in ev:
        while samp > t:
            n = min(0xFFFF, samp - t)
            body += bytes([0x61, n & 0xFF, n >> 8])
            t += n
        body += bytes([0x54, reg, val])
    total = t + RATE // 2
    body += bytes([0x61, (RATE // 2) & 0xFF, (RATE // 2) >> 8])
    return finish(vgm_header(total, ym2151=CLOCK), body)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--channel', type=int, default=0)
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'testseq', 'opm_testseq'))
    a = ap.parse_args()
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    s = build(a.channel)
    vgm = to_vgm(s.ev)
    open(a.out + '.vgz', 'wb').write(gzip.compress(vgm))
    man = {'channel': a.channel, 'clock': CLOCK, 'rate': RATE, 'writes': len(s.ev),
           'seconds': round(s.t + 0.5, 3), 'tests': s.tests}
    json.dump(man, open(a.out + '.json', 'w'), indent=1)
    groups = {}
    for t in s.tests:
        groups[t['group']] = groups.get(t['group'], 0) + 1
    print('%s.vgz: %d writes, %.1f s, %d tests: %s' % (
        a.out, len(s.ev), man['seconds'], len(s.tests),
        ' '.join('%s x%d' % kv for kv in groups.items())))


if __name__ == '__main__':
    main()
