#!/usr/bin/env python3
"""What each YM2151 channel of a song carries, from its VGZ rip. KIT (2026-11-01).
    python3 tools/opm_census.py --map "0x94=01 Rise....vgz" ... --audio DIR [--clock 4000000] [--floor 109.3]
Per song and channel: the patch (CON/FL, carrier MUL, whether any operator looks noise-like),
key-ons, note range in Hz at the chip's clock, the share of notes under --floor (the PSG's
lowest tone, 3579545 / (32 * 1023) = 109.3 Hz), median note length, duty (share of the song
keyed on), and a role guess. Also the share of time two channels sound together.
The question it answers: which two channels go to the PSG and what that costs. Reads the
rips only (gitignored); prints a table.
"""
import argparse
import os
import statistics
import sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from vgm2ym import load                      # noqa: E402

IDX = {0: 0, 1: 1, 2: 2, 4: 3, 5: 4, 6: 5, 8: 6, 9: 7, 10: 8, 12: 9, 13: 10, 14: 11}   # KC note code -> semitone from C#
NAMES = ['C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B', 'C']
CARRIER = {0: [3], 1: [3], 2: [3], 3: [3], 4: [1, 3], 5: [1, 2, 3], 6: [1, 2, 3], 7: [0, 1, 2, 3]}   # slots M1 C1 M2 C2 = 0..3 (reg order)


def kc_hz(kc, kf, clock):
    oct_ = (kc >> 4) & 7
    code = kc & 15
    if code not in IDX:
        code = max(c for c in IDX if c < code)
    midi = 12 * (oct_ + 1) + 1 + IDX[code] + kf / 64.0
    return 440.0 * 2 ** ((midi - 69) / 12.0) * (clock / 3579545.0), midi


def note_name(midi):
    m = int(round(midi))
    return '%s%d' % (['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'][m % 12], m // 12 - 1)


def census(path, clock, floor):
    seg, loop_s, total_s = load(path)
    ev = sorted(seg['ym'], key=lambda e: e[0])
    end = ev[-1][0] if ev else 0.0
    regs = {}
    kc = [0] * 8
    kf = [0] * 8
    on = [None] * 8               # key-on time
    notes = [[] for _ in range(8)]    # (t_on, t_off, hz, midi)
    patches = [set() for _ in range(8)]
    noise_on = False              # reg 0x0F bit 7: the noise generator replaces channel 7's C2 operator
    for t, reg, val in ev:
        regs[reg] = val
        if reg == 0x0F and val & 0x80:
            noise_on = True
        if reg == 0x08:
            ch = val & 7
            if val & 0x78:
                if on[ch] is not None:
                    notes[ch].append((on[ch], t, *kc_hz(kc[ch], kf[ch], clock)))
                on[ch] = t
                p = tuple(regs.get(0x20 + ch, 0) & 0x3f for _ in [0]) + tuple(
                    (regs.get(0x40 + ch + 8 * s, 0), regs.get(0x60 + ch + 8 * s, 127), regs.get(0x80 + ch + 8 * s, 0),
                     regs.get(0xA0 + ch + 8 * s, 0), regs.get(0xC0 + ch + 8 * s, 0), regs.get(0xE0 + ch + 8 * s, 0)) for s in range(4))
                patches[ch].add(p)
            elif on[ch] is not None:
                notes[ch].append((on[ch], t, *kc_hz(kc[ch], kf[ch], clock)))
                on[ch] = None
        elif 0x28 <= reg < 0x30:
            kc[reg - 0x28] = val
        elif 0x30 <= reg < 0x38:
            kf[reg - 0x30] = val >> 2
    for ch in range(8):
        if on[ch] is not None:
            notes[ch].append((on[ch], end, *kc_hz(kc[ch], kf[ch], clock)))
    rows = []
    for ch in range(8):
        ns = notes[ch]
        if not ns:
            rows.append((ch, 0, None))
            continue
        hz = [n[2] for n in ns]
        lens = [n[1] - n[0] for n in ns]
        duty = sum(min(l, 2.0) for l in lens) / max(end, 1e-9)
        under = sum(1 for h in hz if h < floor) / len(hz)
        con_fl = [p[0] for p in patches[ch]]
        con = con_fl[0] & 7 if con_fl else -1
        fl = (con_fl[0] >> 3) & 7 if con_fl else -1
        p0 = sorted(patches[ch])[0] if patches[ch] else None
        noise = noise_on and ch == 7
        cmul = []
        env = ''
        if p0:
            for s in range(4):
                dt_mul, tl, ks_ar, d1r, dt2_d2r, d1l_rr = p0[1 + s]
                if s in CARRIER[con]:
                    cmul.append(dt_mul & 15)
                    if not env:
                        env = 'AR%d D1R%d D2R%d D1L%d RR%d' % (ks_ar & 31, d1r & 31, dt2_d2r & 31, d1l_rr >> 4, d1l_rr & 15)
        med_len = statistics.median(lens)
        distinct = len(set(int(round(n[3])) for n in ns))
        rep = sum(1 for a, b in zip(ns, ns[1:]) if int(round(a[3])) == int(round(b[3]))) / max(len(ns) - 1, 1)
        hzm = statistics.median(hz)
        # a channel DOUBLING another: its key-ons land within 5 ms of the other's at the same note
        dbl = ''
        for o in range(8):
            if o == ch or not notes[o]:
                continue
            ot = sorted(notes[o])
            offs = []
            j = 0
            for n in ns:
                while j < len(ot) and ot[j][0] < n[0] - 0.25:
                    j += 1
                k = j
                while k < len(ot) and ot[k][0] <= n[0] + 0.25:
                    if abs(ot[k][3] - n[3]) < 0.6 or abs(ot[k][3] - n[3] - 12) < 0.6 or abs(ot[k][3] - n[3] + 12) < 0.6:
                        offs.append(n[0] - ot[k][0])
                        break
                    k += 1
            if len(offs) >= 0.8 * len(ns) and len(offs) >= 8:
                dbl += '%d(%+.0fms) ' % (o, statistics.median(offs) * 1000)
        if duty < 0.02:
            role = 'sparse'
        elif noise:
            role = 'NOISE percussion'
        elif distinct <= 2 and len(ns) >= 20:
            role = 'drum/drone (one pitch)'
        elif hzm < 200 and distinct >= 3:
            role = 'bass'
        elif med_len < 0.12 and rep > 0.5:
            role = 'stabs/arp'
        elif distinct >= 8 and med_len >= 0.12:
            role = 'lead/melody'
        else:
            role = 'pad/harmony'
        if dbl:
            role += ' DOUBLES ch ' + dbl.strip()      # (+ms = this channel plays that long AFTER the other: an echo voice)
        rows.append((ch, len(ns), dict(hz_min=min(hz), hz_max=max(hz), hz_med=hzm, note_lo=note_name(min(n[3] for n in ns)),
                                        note_hi=note_name(max(n[3] for n in ns)), under=under, med_len=med_len, duty=duty,
                                        patches=len(patches[ch]), con=con, fl=fl, cmul=cmul, role=role, noise=noise, env=env)))
    # simultaneity between 6 and 7 and between each and the rest: share of song time
    def busy(ch, t):
        return any(n[0] <= t < n[1] for n in notes[ch])
    step = 0.02
    both = 0
    n = 0
    t = 0.0
    while t < end:
        n += 1
        if busy(6, t) and busy(7, t):
            both += 1
        t += step
    return rows, end, loop_s, both / max(n, 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--map', nargs='+', required=True)
    ap.add_argument('--audio', required=True)
    ap.add_argument('--clock', type=float, default=4000000.0, help='the YM2151 clock (System 16B: 4 MHz)')
    ap.add_argument('--floor', type=float, default=3579545.0 / (32 * 1023), help='the PSG lowest tone, Hz')
    a = ap.parse_args()
    print('PSG floor %.1f Hz; pitches at the %.0f Hz chip clock' % (a.floor, a.clock))
    for m in a.map:
        cmd, fname = m.split('=', 1)
        rows, end, loop_s, both = census(os.path.join(a.audio, fname), a.clock, a.floor)
        print('\n== %s  %s  (%.1f s, loop %s)   6 and 7 together: %.0f%% of the song' % (cmd, fname, end, '%.1f s' % loop_s if loop_s else 'none', both * 100))
        print('  ch  notes  range (Hz)        notes       <floor  med.len  duty  CON FL cMUL    patches role')
        for ch, cnt, d in rows:
            if not d:
                print('  %d   %5d  (silent)' % (ch, cnt))
                continue
            print('  %d%s  %5d  %6.0f..%-6.0f  %4s..%-4s  %4.0f%%   %5.2fs  %3.0f%%   %d  %d  %-7s %2d      %s%s' % (
                ch, '*' if ch >= 6 else ' ', cnt, d['hz_min'], d['hz_max'], d['note_lo'], d['note_hi'], d['under'] * 100,
                d['med_len'], d['duty'] * 100, d['con'], d['fl'], ','.join(map(str, d['cmul'])), d['patches'], d['role'],
                ('  [' + d['env'] + ']') if ch >= 6 else ''))


if __name__ == '__main__':
    main()
