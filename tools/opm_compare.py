#!/usr/bin/env python3
"""MDFourier-style per-test, per-band comparison of two renders of the test sequence. KIT.

    python3 tools/opm_compare.py REF OURS --manifest build/testseq/opm_testseq.json \\
        [--rate-ref 62500] [--rate-ours 62500] [--labels jt51,opm] [--out DIR]
      -> a table on stdout, DIR/compare.json, DIR/compare.svg

REF and OURS are .s16 (int16 LE mono, rate from --rate-*) or .wav (any rate, channels
averaged). The manifest is tools/opm_testseq.py's: compare windows on the sequence's own
timeline. REF carries that timeline (the oracle renders from t = 0); OURS is found on it by
cross-correlating the sync clicks, so a cart capture with an arbitrary lead-in works.

Per test, after aligning that window to the sample:
  LEVEL    RMS while sounding, ours - ref (dB)                        flag > 0.2 dB
  PITCH    the strongest spectral peak over the held part, ours vs ref, in cents
           (1/3-octave bands cannot see a 50-cent slip)                   flag > 5 cents
  BANDS    1/3-octave band shares over the held part, ours - ref, on every band the ref
           puts within 30 dB of its loudest; the worst band and its centre   flag > 1.0 dB
  ENV      the RMS envelope (2 ms) in dB over the held part (attack and decay shape, not
           the key-off's timing), max |ours - ref| while the ref is within 40 dB of its
           peak, both floored there                                          flag > 0.5 dB
The thresholds come from the chip itself (opm.h): a decay's first step is 2-8 attenuation
units (0.19-0.75 dB) either way depending on where the key-on lands, so anything under
0.2 dB is write timing, not a model or transcode defect.

WHY (2026-10-31, mdfourier 2). tools/opm_ab.py's music metrics plateaued at "within 0.3 dB
per octave" while five defects sat underneath; the sequence plus per-test numbers found them.
This makes that scratch table a tool, adds the bands and the envelope, and takes any rate so
stages 2-4 (the cart on ares, per chip stream, and on BlastEm) run through the same gate.
"""
import argparse
import json
import os
import sys
import wave

import numpy as np

THIRDS = [1000 * 2 ** (n / 3) for n in range(-11, 13)]     # 79 Hz .. 16 kHz


def dc_free(x, rate):
    """20 Hz first-order high-pass: a YM2612 idles off centre (its ladder DAC), and the
    level metric must not count that offset as signal."""
    from scipy.signal import lfilter
    k = np.exp(-2 * np.pi * 20.0 / rate)
    return lfilter([(1 + k) / 2, -(1 + k) / 2], [1, -k], x - x[:min(len(x), rate)].mean())


def load(path, rate):
    if path.lower().endswith('.wav'):
        w = wave.open(path)
        x = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(np.float64)
        x = x.reshape(-1, w.getnchannels()).mean(axis=1) / 32768.0
        return dc_free(x, w.getframerate()), w.getframerate()
    return dc_free(np.fromfile(path, dtype='<i2').astype(np.float64) / 32768.0, rate), rate


def to_rate(x, src, dst):
    if src == dst:
        return x
    from fractions import Fraction
    from scipy.signal import resample_poly
    fr = Fraction(dst, src).limit_denominator(8000)
    return resample_poly(x, fr.numerator, fr.denominator)


BAND_MAX = None      # --band-max: the highest band centre compared (a 16 kHz lane has nothing above 8 kHz)


def envelope(x, rate, ms=2.0):
    w = max(1, int(rate * ms / 1000))
    seg = x[:len(x) // w * w].reshape(-1, w)
    return np.sqrt((seg ** 2).mean(axis=1)), w


def locate(ref, ours, rate, a, b, lo=0, hi=None):
    """ours index where ref[a:b] sits: normalised cross-correlation of the dB envelopes
    (so a loud note next to a quiet click cannot outweigh the click), and its value."""
    er, w = envelope(ref[a:b], rate)
    eo, _ = envelope(ours[lo:hi], rate)
    er = np.maximum(20 * np.log10(er + 1e-6), -60)
    eo = np.maximum(20 * np.log10(eo + 1e-6), -60)
    er = er - er.mean()
    n = len(er)
    if len(eo) < n:
        return lo, 0.0
    c = np.correlate(eo, er, 'valid')
    cs = np.concatenate([[0.0], np.cumsum(eo)])
    cs2 = np.concatenate([[0.0], np.cumsum(eo ** 2)])
    mean = (cs[n:] - cs[:-n]) / n
    var = (cs2[n:] - cs2[:-n]) - n * mean ** 2
    ncc = (c - mean * er.sum()) / (np.sqrt(np.maximum(var, 1e-9) * (er ** 2).sum()))
    k = int(np.argmax(ncc))
    return lo + k * w, float(ncc[k])


def find_offset(ref, ours, rate, man):
    """(offset, scale, corr): ours index = offset + ref index * scale. The start clicks give
    the offset; the end click gives the scale, so a player that keeps time to 0.2% (the
    cart's Z80, measured) still lands every window: 0.2% of 57 s is 115 ms, far past the
    per-test lag search."""
    syncs = [t for t in man['tests'] if t['group'] == 'sync']
    start = [t for t in syncs if t['name'] != 'sync_end']
    a, b = int(start[0]['t0'] * rate), int(start[-1]['t1'] * rate)
    # the start clicks sit at the start: search the first 20 s of OURS only, so a lead-in
    # or a missing first click cannot make the end click win
    o0, corr = locate(ref, ours, rate, a, b, 0, min(len(ours), b + 20 * rate))
    off = o0 - a
    end = [t for t in syncs if t['name'] == 'sync_end']
    scale = 1.0
    if end:
        c, d = int(end[0]['t0'] * rate), int(end[0]['t1'] * rate)
        span = int(0.003 * (d - a))                    # allow 0.3% drift: the cart's lanes keep 0.03%; 1% let a neighbouring onset win (lane, 2026-10-31)
        lo, hi = max(0, c + off - span), min(len(ours), d + off + span)
        if hi - lo > d - c:
            o1, c1 = locate(ref, ours, rate, c, d, lo, hi)
            if c1 > 0.5:
                scale = (o1 - o0) / float(c - a)
    return off, scale, corr


def fine_lag(r, o, span):
    best, bv = 0, -1e30
    for L in range(-span, span + 1):
        if L >= 0:
            v = float(np.dot(r[L:], o[:len(o) - L])) if L < len(r) else -1e30
        else:
            v = float(np.dot(r[:L], o[-L:]))
        if v > bv:
            bv, best = v, L
    return best


def band_shares(x, rate, top):
    n = min(4096, len(x) // 2 * 2)
    if n < 512:
        return None
    fr = np.lib.stride_tricks.sliding_window_view(x, n)[::n // 2]
    p = (np.abs(np.fft.rfft(fr * np.hanning(n), axis=1)) ** 2).mean(axis=0)
    f = np.fft.rfftfreq(n, 1.0 / rate)
    tot = p.sum() + 1e-30
    out = []
    for c in THIRDS:
        if c * 2 ** (1 / 6) > top:
            break
        m = (f >= c / 2 ** (1 / 6)) & (f < c * 2 ** (1 / 6))
        out.append(10 * np.log10(p[m].sum() / tot + 1e-15))
    return out


def peak_hz(x, rate):
    """frequency of the strongest spectral line, parabolic-interpolated."""
    n = 1 << int(np.log2(max(1024, min(len(x), 1 << 16))))
    if len(x) < 1024:
        return None
    seg = x[:n] * np.hanning(min(n, len(x)))[:n] if len(x) >= n else np.concatenate([x * np.hanning(len(x)), np.zeros(n - len(x))])
    p = np.abs(np.fft.rfft(seg))
    k = int(np.argmax(p[1:-1])) + 1
    a, b, c = np.log(p[k - 1] + 1e-12), np.log(p[k] + 1e-12), np.log(p[k + 1] + 1e-12)
    d = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0.0
    return (k + d) * rate / n


def rms_db(x):
    return 20 * np.log10(np.sqrt((x ** 2).mean()) + 1e-12)


def compare(ref, ours, rate, man, thr):
    off, scale, corr = find_offset(ref, ours, rate, man)
    top = 0.45 * rate
    rows = []
    for t in man['tests']:
        a, b = int(t['t0'] * rate), int(t['t1'] * rate)
        r = ref[a:b]
        oa = int(round(off + a * scale))
        ob = oa + (b - a)
        if oa < 0 or ob > len(ours) or len(r) < 64:
            rows.append(dict(name=t['name'], group=t['group'], missing=True))
            continue
        o = ours[oa:ob]
        L = fine_lag(r, o, int(rate * 0.003))
        o = ours[oa - L:ob - L] if 0 <= oa - L and ob - L <= len(ours) else o
        # sounding part: from the ref's first sample above -60 dBFS to the key-off
        on = np.nonzero(np.abs(r) > 1e-3)[0]
        if len(on) == 0:
            rows.append(dict(name=t['name'], group=t['group'], silent=True))
            continue
        s0 = int(on[0])
        s1 = min(len(r), s0 + int(t.get('hold', 0.3) * rate))
        lvl = rms_db(o[s0:s1]) - rms_db(r[s0:s1])
        # pitch over the loudest 100 ms of the held part: a decaying or noise-like test has
        # no tone later on, and the metric only means something where there is one
        w100 = max(1024, int(0.1 * rate))
        e100, we = envelope(r[s0:s1], rate, 10.0)
        pk = int(np.argmax(e100)) * we
        fr_, fo_ = peak_hz(r[s0 + pk:s0 + pk + w100], rate), peak_hz(o[s0 + pk:s0 + pk + w100], rate)
        cents = 1200 * np.log2(fo_ / fr_) if fr_ and fo_ else 0.0
        br, bo = band_shares(r[s0:s1], rate, top), band_shares(o[s0:s1], rate, top)
        worst, worst_hz, nb = 0.0, None, 0
        if br and bo:
            peak = max(br)
            for c, u, v in zip(THIRDS, br, bo):
                if u >= peak - 30 and (BAND_MAX is None or c <= BAND_MAX):
                    nb += 1
                    if abs(v - u) > abs(worst):
                        worst, worst_hz = v - u, c
        er, _ = envelope(r[s0:s1], rate)
        eo, _ = envelope(o[s0:s1], rate)
        n = min(len(er), len(eo))
        er, eo = 20 * np.log10(er[:n] + 1e-6), 20 * np.log10(eo[:n] + 1e-6)
        floor = er.max() - 40
        live = er >= floor
        er, eo = np.maximum(er, floor), np.maximum(eo, floor)
        env = float(np.abs(eo - er)[live].max()) if live.any() else 0.0
        flags = []
        if abs(lvl) > thr[0]:
            flags.append('level')
        if abs(cents) > thr[3]:
            flags.append('pitch')
        if abs(worst) > thr[1]:
            flags.append('band')
        if env > thr[2]:
            flags.append('env')
        rows.append(dict(name=t['name'], group=t['group'], param=t.get('param'), lag=L,
                         level_db=round(lvl, 3), cents=round(float(cents), 1), peak_hz=round(fr_ or 0, 1), worst_band_db=round(worst, 2), worst_band_hz=worst_hz,
                         bands_compared=nb, env_db=round(env, 3), flags=flags,
                         bands_ref=[round(v, 2) for v in (br or [])], bands_ours=[round(v, 2) for v in (bo or [])]))
    return off, scale, corr, rows


def svg(rows, labels, thr, path):
    groups = []
    for r in rows:
        if r['group'] not in groups:
            groups.append(r['group'])
    W, rowh, left, barw = 1100, 26, 90, 0.0
    tests = [r for r in rows if 'level_db' in r]
    H = 60 + rowh * len(tests) + 20
    out = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" font-family="monospace" font-size="12">' % (W, H),
           '<rect width="100%%" height="100%%" fill="white"/>',
           '<text x="%d" y="20">%s - %s per test: level (dB), worst 1/3-octave band (dB), envelope max (dB); '
           'red = over threshold (%.1f / %.1f / %.1f)</text>' % (left, labels[1], labels[0], *thr[:3])]
    cols = [('level_db', thr[0], 250), ('worst_band_db', thr[1], 550), ('env_db', thr[2], 850)]
    scale = 60.0                                        # px per dB
    for key, th, x0 in cols:
        out.append('<line x1="%d" y1="40" x2="%d" y2="%d" stroke="#888"/>' % (x0, x0, H - 10))
        out.append('<text x="%d" y="36" fill="#444">%s</text>' % (x0 - 30, key.replace('_db', '')))
    y = 50
    for r in tests:
        out.append('<text x="4" y="%d">%s</text>' % (y + 12, r['name']))
        for key, th, x0 in cols:
            v = r[key]
            v = max(-4.0, min(4.0, v))
            colour = '#c33' if key.replace('_db', '').replace('worst_band', 'band') in r['flags'] else '#39f'
            x1, x2 = sorted((x0, x0 + v * scale))
            out.append('<rect x="%.1f" y="%d" width="%.1f" height="14" fill="%s"/>' % (x1, y + 2, max(1.0, x2 - x1), colour))
            if key == 'worst_band_db' and r['worst_band_hz']:
                out.append('<text x="%d" y="%d" fill="#666" font-size="10">%d Hz</text>' % (x0 + 250, y + 12, r['worst_band_hz']))
        y += rowh
    out.append('</svg>')
    open(path, 'w').write('\n'.join(out))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ref')
    ap.add_argument('ours')
    ap.add_argument('--manifest', required=True)
    ap.add_argument('--rate-ref', type=int, default=62500)
    ap.add_argument('--rate-ours', type=int, default=62500)
    ap.add_argument('--labels', default='ref,ours')
    ap.add_argument('--band-max', type=float, help='skip 1/3-octave bands centred above this Hz (ours cannot carry them: a lower-rate lane)')
    ap.add_argument('--thresholds', default='0.2,1.0,0.5,5', help='level, band, envelope (dB), pitch (cents)')
    ap.add_argument('--out', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build', 'opm_compare'))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    thr = tuple(float(v) for v in a.thresholds.split(','))
    global BAND_MAX
    BAND_MAX = a.band_max
    labels = a.labels.split(',')
    man = json.load(open(a.manifest))
    ref, rr = load(a.ref, a.rate_ref)
    ours, ro = load(a.ours, a.rate_ours)
    rate = min(rr, ro)
    ref, ours = to_rate(ref, rr, rate), to_rate(ours, ro, rate)
    off, scale, corr, rows = compare(ref, ours, rate, man, thr)
    print('%s vs %s at %d Hz; %s found at %+.4f s, running %+.3f%% (sync correlation %.2f)' % (
        labels[1], labels[0], rate, labels[1], off / rate, (scale - 1) * 100, corr))
    print('%-9s %-5s %6s %6s  %7s %7s  %6s  %s' % ('test', 'grp', 'level', 'cents', 'band', 'at Hz', 'env', 'flags'))
    nflag = 0
    for r in rows:
        if 'level_db' not in r:
            print('%-9s %-5s  %s' % (r['name'], r['group'], 'missing' if r.get('missing') else 'silent'))
            continue
        nflag += bool(r['flags'])
        print('%-9s %-5s %+6.2f %+6.1f  %+7.2f %7s  %6.2f  %s' % (
            r['name'], r['group'], r['level_db'], r['cents'], r['worst_band_db'],
            '%d' % r['worst_band_hz'] if r['worst_band_hz'] else '-', r['env_db'], ' '.join(r['flags'])))
    print('flagged: %d of %d' % (nflag, len(rows)))
    json.dump(dict(ref=a.ref, ours=a.ours, rate=rate, offset_s=off / rate, scale=scale, labels=labels, thresholds=thr, tests=rows),
              open(os.path.join(a.out, 'compare.json'), 'w'), indent=1)
    svg(rows, labels, thr, os.path.join(a.out, 'compare.svg'))
    print('wrote %s/compare.json and compare.svg' % a.out)
    return 1 if nflag else 0


if __name__ == '__main__':
    sys.exit(main())
