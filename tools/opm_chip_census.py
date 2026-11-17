#!/usr/bin/env python3
"""What the YM2612 + PSG could carry of the YM2151's two overflow channels. KIT.

Three measurements over the eight Altered Beast rips, all offline, no rom:

  1. POLYPHONY / dynamic allocation: a simulated allocator hands YM2612 channels to
     YM2151 notes as they key on (the patch travels with the note; the transcoded stream
     is baked offline, so this costs nothing at run time). A note that finds no free
     YM2612 channel is OVERFLOW: what an SH-2 synth would still have to render. Two
     allocators: A = six full channels; B = five full + channel 3 in special mode as two
     half-slots for notes that are effectively 2-op (ops with independent frequencies).
     Busy = keyed, or keyed + a release guard (--guard seconds) so a stolen channel does
     not cut an audible tail.
  2. OPERATOR USE on the overflow channels: how many operators each note really sounds
     (keyed and TL below --tl-dead), and whether the live pair fits one half of a
     YM2612 channel in special mode (a modulator->carrier pair, or two carriers).
  3. PSG PASSABILITY: each unique note on the overflow channels rendered through the
     exact model (sndtest/opm, jt51-identical) and its 1/3-octave sustain spectrum set
     against an ideal square at the note's pitch; the worst band in dB says how far a
     PSG square is from the voice. Time-weighted histogram per channel.

  tools/opm_chip_census.py [--channels 6,7] [--guard 0.25] [--json OUT]
"""
import argparse
import json
import math
import os
import struct
import subprocess
import sys
from collections import Counter, defaultdict

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from vgm2ym import load                      # noqa: E402
import opm_ab                                # noqa: E402
from opm_prerender_census import SONGS, AUDIO, CLOCK, NATIVE, ch_regs   # noqa: E402

# YM2151 operator slots in register order: 0 = M1, 1 = M2, 2 = C1, 3 = C2.
# Per algorithm: edges (src -> dst) and the carriers (jt51 jt51_mod.v connection table).
ALG = {
    0: ([(0, 2), (2, 1), (1, 3)], {3}),
    1: ([(0, 1), (2, 1), (1, 3)], {3}),
    2: ([(0, 3), (2, 1), (1, 3)], {3}),
    3: ([(0, 2), (2, 3), (1, 3)], {3}),
    4: ([(0, 2), (1, 3)], {2, 3}),
    5: ([(0, 2), (0, 1), (0, 3)], {1, 2, 3}),
    6: ([(0, 2)], {1, 2, 3}),
    7: ([], {0, 1, 2, 3}),
}
SLOT_NAME = ['M1', 'M2', 'C1', 'C2']


def kc_hz(kc, kf):
    code = kc & 15
    s = code - code // 4
    o = (kc >> 4) & 7
    return 440.0 * 2 ** ((o - 4) + (s - 8) / 12.0 + (kf >> 2) / 64.0 / 12.0) * (CLOCK / 3579545.0)


def live_ops(snap, mask, tl_dead):
    """snap = the channel's registers in ch_regs order; -> (live set, alg, fbl, carriers)."""
    alg = snap[0] & 7
    fbl = (snap[0] >> 3) & 7
    tl = [snap[4 + 6 * s + 1] & 0x7F for s in range(4)]
    edges, carriers = ALG[alg]
    live = {s for s in range(4) if (mask >> s) & 1 and tl[s] < tl_dead}
    # an op only sounds if it reaches a live carrier through live ops
    reach = set(c for c in carriers if c in live)
    changed = True
    while changed:
        changed = False
        for a, b in edges:
            if a in live and b in reach and a not in reach:
                reach.add(a)
                changed = True
    return reach, alg, fbl, carriers


def half_fit(live, alg, fbl):
    """Does this note fit one half of a YM2612 channel in special mode?
    -> None (no), 'pair' (mod->car, feedback on the modulator allowed once per channel),
       'pair-fb', 'single', 'dual-car'."""
    edges, carriers = ALG[alg]
    if len(live) == 0 or len(live) > 2:
        return None
    if len(live) == 1:
        return 'single'
    a, b = sorted(live)
    if a in carriers and b in carriers:
        return 'dual-car'
    for s, d in edges:
        if {s, d} == {a, b} and d in carriers:
            return 'pair-fb' if (s == 0 and fbl) else 'pair'
    return None


def song_notes(seg, total_s, tl_dead):
    """-> per channel: list of notes (t_on, t_off, snapshot, mask, live, fit)."""
    regs = {c: ch_regs(c) for c in range(8)}
    shadow = [0] * 256
    notes = defaultdict(list)
    cur = {}
    for t, r, v in seg['ym']:
        if r == 0x08:
            c = v & 7
            mask = (v >> 3) & 0xF
            if c in cur:
                n = cur.pop(c)
                n[1] = t
                notes[c].append(n)
            if mask:
                snap = tuple(shadow[x] for x in regs[c])
                live, alg, fbl, _ = live_ops(snap, mask, tl_dead)
                cur[c] = [t, None, snap, mask, frozenset(live), half_fit(live, alg, fbl)]
            continue
        shadow[r] = v
    for c, n in cur.items():
        n[1] = total_s
        notes[c].append(n)
    return notes


def allocate(notes, total_s, halves, guard, audible=None):
    """Simulate the YM2612 allocator. halves=0: six full channels; 2: five full + two
    half-slots (channel 3 special mode). A note holds its slot from key-on until it is
    inaudible: key-off, or the point the held envelope has decayed to silence (audible =
    {(snap, mask): seconds} from a render), plus `guard` seconds for the release. A note
    that finds no slot is OVERFLOW. A slot is 'cut' when a note takes it from another
    YM2151 channel's note still inside its guard; a channel re-taking its own slot is
    what the chip does and is not a cut. -> (Counter, {note id: end})."""
    ev = []
    for c, lst in notes.items():
        for n in lst:
            end = n[1]
            if audible is not None:
                end = min(end, n[0] + audible.get((n[2], n[3]), 1e9))
            ev.append((n[0], 1, c, n, end))
            ev.append((end, 0, c, n, end))
    ev.sort(key=lambda e: (e[0], e[1]))
    nfull = 6 - (1 if halves else 0)
    slots = [{'owner': None, 'last': None, 'free_at': 0.0, 'half': i >= nfull}
             for i in range(nfull + halves)]
    owner = {}
    out = Counter()
    over_until = {}
    for t, on, c, n, end in ev:
        if on:
            out['notes'] += 1
            if c in owner:                      # retrigger keeps its slot
                continue
            fit = n[5] is not None
            cands = [s for s in slots if s['owner'] is None and (fit or not s['half'])]
            # the slot this channel last used, then half-slots for 2-op notes, then the
            # longest-free
            cands.sort(key=lambda s: (s['last'] != c, not s['half'], s['free_at']))
            free = [s for s in cands if s['free_at'] <= t or s['last'] == c]
            if free:
                s = free[0]
            elif cands:
                s = cands[0]
                out['tail_cut'] += 1
            else:
                out['overflow'] += 1
                over_until[(c, id(n))] = end
                continue
            s['owner'] = c
            s['last'] = c
            owner[c] = s
        else:
            if c in owner and owner[c]['owner'] == c:
                s = owner.pop(c)
                s['owner'] = None
                s['free_at'] = t + guard
    return out, over_until


def audible_lengths(exe, work, notes, floor=4):
    """Render every unique (snap, mask) with the key HELD for up to 4 s; -> seconds until
    the output stays under `floor` LSB (the held envelope's decay to silence)."""
    out = {}
    for c, lst in notes.items():
        regs = ch_regs(c)
        for t0, t1, snap, mask, live, hf in lst:
            key = (snap, mask)
            if key in out:
                continue
            ev = [(0, r, v) for r, v in zip(regs, snap)]
            kon = 256
            n = kon + int(4.0 * NATIVE)
            ev.append((kon, 0x08, (mask << 3) | c))
            evp = os.path.join(work, 'held.bin')
            with open(evp, 'wb') as f:
                for t, r, v in ev:
                    f.write(struct.pack('<IBB', t, r, v))
            o = os.path.join(work, 'held.s16')
            subprocess.run([exe, evp, o, str(n), '0', str(1 << c), str(CLOCK), '-', '2', '2'],
                           check=True, stdout=subprocess.DEVNULL)
            x = np.fromfile(o, dtype='<i2')
            nz = np.flatnonzero(np.abs(x[kon:]) >= floor)
            out[key] = (int(nz[-1]) + 1) / NATIVE if len(nz) else 0.0
    return out


def overflow_seconds(notes, over_until):
    iv = []
    for c, lst in notes.items():
        for n in lst:
            if (c, id(n)) in over_until:
                iv.append((n[0], over_until[(c, id(n))]))
    iv.sort()
    tot = 0.0
    cur_a = cur_b = None
    for a, b in iv:
        if cur_b is None or a > cur_b:
            if cur_b is not None:
                tot += cur_b - cur_a
            cur_a, cur_b = a, b
        else:
            cur_b = max(cur_b, b)
    if cur_b is not None:
        tot += cur_b - cur_a
    return tot


def third_octave_bands(x, rate, lo=100.0, hi=8000.0):
    n = len(x)
    w = np.hanning(n)
    spec = np.abs(np.fft.rfft(x * w)) ** 2
    f = np.fft.rfftfreq(n, 1.0 / rate)
    bands = []
    fc = lo
    while fc <= hi:
        a, b = fc / 2 ** (1 / 6), fc * 2 ** (1 / 6)
        bands.append((fc, float(spec[(f >= a) & (f < b)].sum())))
        fc *= 2 ** (1 / 3)
    return bands


def square_bands(f0, lo=100.0, hi=8000.0):
    bands = []
    fc = lo
    while fc <= hi:
        a, b = fc / 2 ** (1 / 6), fc * 2 ** (1 / 6)
        e = 0.0
        k = 1
        while k * f0 < b:
            if k * f0 >= a:
                e += 1.0 / (k * k)
            k += 2
        bands.append((fc, e))
        fc *= 2 ** (1 / 3)
    return bands


def psg_distance(exe, work, c, snap, mask, hold_s):
    """Render the note's sustain and compare with an ideal square. -> worst band dB."""
    regs = ch_regs(c)
    ev = [(0, r, v) for r, v in zip(regs, snap)]
    kon = 256
    ev.append((kon, 0x08, (mask << 3) | c))
    hold = int(min(hold_s, 0.5) * NATIVE)
    hold = max(hold, int(0.12 * NATIVE))
    ev.append((kon + hold, 0x08, c))
    n = kon + hold + 64
    evp = os.path.join(work, 'note.bin')
    with open(evp, 'wb') as f:
        for t, r, v in ev:
            f.write(struct.pack('<IBB', t, r, v))
    out = os.path.join(work, 'note.s16')
    subprocess.run([exe, evp, out, str(n), '0', str(1 << c), str(CLOCK), '-', '2', '2'],
                   check=True, stdout=subprocess.DEVNULL)
    x = np.fromfile(out, dtype='<i2').astype(np.float64)
    a = kon + int(0.03 * NATIVE)
    seg = x[a:kon + hold]
    if len(seg) < 1024 or np.abs(seg).max() < 4:
        return None
    kc, kf = snap[1], snap[2]
    live, alg, fbl, carriers = live_ops(snap, mask, 0x7F)
    cars = sorted(s for s in live if s in carriers) or [3]
    mul = snap[4 + 6 * cars[0]] & 15
    f0 = kc_hz(kc, kf) * (mul if mul else 0.5)
    n = len(seg)
    spec = np.abs(np.fft.rfft(seg * np.hanning(n))) ** 2
    f = np.fft.rfftfreq(n, 1.0 / NATIVE)
    band = (f >= 60) & (f < 8000)
    E = float(spec[band].sum()) or 1.0
    # a square has odd harmonics at 1/k: the energy the voice puts anywhere else is what
    # a PSG cannot make at all (LEAK); on the odd lines, the deviation from 1/k after one
    # best gain is the SHAPE error
    H = []
    k = 1
    while k * f0 < 8000:
        sel = (f >= k * f0 * 0.97) & (f < k * f0 * 1.03) & band
        H.append(float(spec[sel].sum()))
        k += 2
    hs = sum(H)
    leak = 10 * math.log10(max(1.0 - hs / E, 1e-6))
    pts = [(10 * math.log10(h / E), -20 * math.log10(2 * i + 1)) for i, h in enumerate(H)
           if h / E > 1e-3]
    if pts:
        gain = sum(a - b for a, b in pts) / len(pts)
        shape = math.sqrt(sum((a - b - gain) ** 2 for a, b in pts) / len(pts))
    else:
        shape = 99.0
    return leak, shape


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--channels', default='6,7')
    ap.add_argument('--guard', type=float, default=0.25,
                    help='seconds after key-off a YM2612 channel is held for its release')
    ap.add_argument('--tl-dead', type=int, default=120)
    ap.add_argument('--no-psg', action='store_true')
    ap.add_argument('--json')
    a = ap.parse_args()
    chans = [int(x) for x in a.channels.split(',')]
    exe = None if a.no_psg else opm_ab.build_opm()
    work = os.path.join(ROOT, 'build', 'chipcensus')
    os.makedirs(work, exist_ok=True)
    report = {}
    print('1. POLYPHONY and the YM2612 allocator (A = 6 full, B = 5 full + ch3 special as 2 halves)')
    print(f"{'song':5} {'len s':>6} {'notes':>5} {'max':>3} {'>6 s':>6} {'>6%':>5} "
          f"{'A over':>6} {'A s':>6} {'A%':>5} {'A cut':>5} {'B over':>6} {'B s':>6} {'B%':>5} {'B cut':>5}")
    tot = Counter()
    tot_s = 0.0
    audible = {}
    for cmd, fname in SONGS:
        seg, loop_t, total_s = load(os.path.join(AUDIO, fname))
        notes = song_notes(seg, total_s, a.tl_dead)
        if exe:
            audible.update(audible_lengths(exe, work, notes))
        aud = audible if exe else None
        end = lambda n: min(n[1], n[0] + aud.get((n[2], n[3]), 1e9)) if aud else n[1]
        # polyphony over time: count audible notes at every change
        ev = sorted([(n[0], 1) for l in notes.values() for n in l] +
                    [(end(n), -1) for l in notes.values() for n in l])
        poly = 0
        over6 = 0.0
        pmax = 0
        last = 0.0
        for t, d in ev:
            if poly > 6:
                over6 += t - last
            last = t
            poly += d
            pmax = max(pmax, poly)
        resA, ovA = allocate(notes, total_s, 0, a.guard, aud)
        resB, ovB = allocate(notes, total_s, 2, a.guard, aud)
        sA = overflow_seconds(notes, ovA)
        sB = overflow_seconds(notes, ovB)
        nn = resA['notes']
        print(f"{cmd:#5x} {total_s:6.1f} {nn:5d} {pmax:3d} {over6:6.1f} {100 * over6 / total_s:5.1f} "
              f"{resA['overflow']:6d} {sA:6.1f} {100 * sA / total_s:5.1f} {resA['tail_cut']:5d} "
              f"{resB['overflow']:6d} {sB:6.1f} {100 * sB / total_s:5.1f} {resB['tail_cut']:5d}")
        tot['notes'] += nn
        tot['A'] += resA['overflow']
        tot['B'] += resB['overflow']
        tot['Acut'] += resA['tail_cut']
        tot['Bcut'] += resB['tail_cut']
        tot_s += total_s
        tot['over6'] += over6
        tot['As'] += sA
        tot['Bs'] += sB
        report[f'{cmd:#x}'] = {'len_s': total_s, 'notes': nn, 'poly_max': pmax, 'over6_s': over6,
                               'A_overflow': resA['overflow'], 'A_s': sA, 'A_cut': resA['tail_cut'],
                               'B_overflow': resB['overflow'], 'B_s': sB, 'B_cut': resB['tail_cut']}
        report[f'{cmd:#x}']['notes_by_ch'] = {c: len(l) for c, l in notes.items()}
        report[f'{cmd:#x}']['_notes'] = notes
    print(f"ALL   {tot_s:6.1f} {tot['notes']:5d}     {tot['over6']:6.1f} {100 * tot['over6'] / tot_s:5.1f} "
          f"{tot['A']:6d} {tot['As']:6.1f} {100 * tot['As'] / tot_s:5.1f} {tot['Acut']:5d} "
          f"{tot['B']:6d} {tot['Bs']:6.1f} {100 * tot['Bs'] / tot_s:5.1f} {tot['Bcut']:5d}")
    print(f"  (guard {a.guard}s after key-off; 'cut' = notes that took a channel still in its release)")

    print(f"\n2. OPERATOR USE on channels {chans} (time-weighted, TL >= {a.tl_dead} = dead)")
    print(f"{'song':5} {'ch':2} {'notes':>5} {'1op%':>5} {'2op%':>5} {'3op%':>5} {'4op%':>5} "
          f"{'half-fit%':>9} {'fb-pair%':>8} {'algs'}")
    opuse = {}
    for cmd, fname in SONGS:
        notes = report[f'{cmd:#x}']['_notes']
        for c in chans:
            lst = notes.get(c, [])
            if not lst:
                continue
            w = Counter()
            fit = fbp = 0.0
            algs = Counter()
            T = 0.0
            for t0, t1, snap, mask, live, hf in lst:
                d = t1 - t0
                T += d
                w[len(live)] += d
                if hf:
                    fit += d
                if hf == 'pair-fb':
                    fbp += d
                algs[snap[0] & 7] += d
            T = T or 1.0
            al = ' '.join(f'a{k}:{100 * v / T:.0f}' for k, v in algs.most_common(3))
            print(f"{cmd:#5x} {c:2d} {len(lst):5d} {100 * w[1] / T:5.1f} {100 * w[2] / T:5.1f} "
                  f"{100 * w[3] / T:5.1f} {100 * w[4] / T:5.1f} {100 * fit / T:9.1f} {100 * fbp / T:8.1f} {al}")
            opuse[f'{cmd:#x}/{c}'] = {'notes': len(lst), 'ops': {k: v / T for k, v in w.items()},
                                     'half_fit': fit / T, 'fb_pair': fbp / T}
    report['opuse'] = opuse

    if exe:
        print(f"\n3. PSG PASSABILITY on channels {chans}, time-weighted. LEAK = the voice's sustain "
              f"energy OFF the odd-harmonic lines (a square cannot make it; -10 dB = 10%);\n"
              f"   SHAPE = rms dB error of the odd lines against 1/k after one gain. "
              f"pass = leak <= -10 dB and shape <= 6 dB")
        print(f"{'song':5} {'ch':2} {'uniq':>4} {'leak med':>8} {'leak p90':>8} {'shape med':>9} "
              f"{'shape p90':>9} {'pass%':>6}")
        psg = {}
        for cmd, fname in SONGS:
            notes = report[f'{cmd:#x}']['_notes']
            for c in chans:
                lst = notes.get(c, [])
                if not lst:
                    continue
                cache = {}
                vals = []
                for t0, t1, snap, mask, live, hf in lst:
                    key = (snap, mask)
                    if key not in cache:
                        cache[key] = psg_distance(exe, work, c, snap, mask, t1 - t0)
                    if cache[key] is not None:
                        vals.append((cache[key], t1 - t0))
                if not vals:
                    continue
                T = sum(d for _, d in vals)

                def pct(idx, q):
                    v = sorted(vals, key=lambda x: x[0][idx])
                    acc = 0.0
                    for x, d in v:
                        acc += d
                        if acc >= q * T:
                            return x[idx]
                    return v[-1][0][idx]
                ok = 100 * sum(d for x, d in vals if x[0] <= -10 and x[1] <= 6) / T
                print(f"{cmd:#5x} {c:2d} {len(cache):4d} {pct(0, .5):8.1f} {pct(0, .9):8.1f} "
                      f"{pct(1, .5):9.1f} {pct(1, .9):9.1f} {ok:6.1f}")
                psg[f'{cmd:#x}/{c}'] = {'uniq': len(cache), 'leak_med': pct(0, .5), 'leak_p90': pct(0, .9),
                                       'shape_med': pct(1, .5), 'shape_p90': pct(1, .9), 'pass': ok}
        report['psg'] = psg

    if a.json:
        for k in list(report):
            if isinstance(report[k], dict):
                report[k].pop('_notes', None)
        with open(a.json, 'w') as f:
            json.dump(report, f, indent=1, default=str)


if __name__ == '__main__':
    main()
