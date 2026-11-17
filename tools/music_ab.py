#!/usr/bin/env python3
"""Music A/B against a reference recording: alignment, tempo, spectral balance. KIT.

    python3 tools/music_ab.py REF.flac OURS.wav [OURS2.wav ...] [--secs 25] [--json out.json]

REF is the real-board recording (srcref/audio/Altered Beast (The 16-Bit Audiophile Project)/),
OURS a capture from ares-headless-audio --audio-wav (harness 1, 2026-10-30). Anything ffmpeg
reads works; everything is taken to 48 kHz mono with DC removed.

ALIGNMENT is by note onsets, not by the first loud sample: a spectral-flux envelope (10 ms
hop) of OURS is cross-correlated against REF's, so a silent lead-in or a different first
note level cannot shift it. TEMPO is checked by aligning an early and a late window
separately; the two lags differ by the drift.

LIMIT (2026-10-30): the onset alignment assumes OURS runs at the reference's tempo. The lab
cart measured 2.4-4.6% slow over its first 33 s (Z80 block counter vs score time) and ~7.8%
over a whole pass, so the balance tables are only trustworthy once the player keeps time.

BALANCE is what the per-channel fit (harness 3) has to reproduce, so levels are reported two
ways: overall RMS (the master gain question, separate) and third-octave bands each relative to
the signal's own total power (the balance question). ours - ref per band, then per octave per
5 s segment to show whether a difference is steady or follows the music.
"""
import argparse
import json
import subprocess
import sys

import numpy as np
from scipy.signal import correlate, stft

RATE = 48000
HOP = 480                                   # 10 ms


def load(path):
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-ac', '1', '-ar', str(RATE),
                          '-f', 'f32le', '-'], capture_output=True, check=True).stdout
    x = np.frombuffer(raw, dtype='<f4').astype(np.float64)
    return x - x.mean()


def flux(x):
    _, _, z = stft(x, fs=RATE, nperseg=2048, noverlap=2048 - HOP, boundary=None, padded=False)
    m = np.log1p(1000 * np.abs(z))
    f = np.maximum(np.diff(m, axis=1), 0).sum(axis=0)
    f = f - f.mean()
    return f / (f.std() + 1e-12)


def lag_of(ref_f, our_f, our_from, our_len, search):
    """Hop lag (ref index - our index) that best aligns our_f[our_from:our_from+our_len]."""
    seg = our_f[our_from:our_from + our_len]
    lo, hi = max(0, our_from + search[0]), min(len(ref_f), our_from + our_len + search[1])
    c = correlate(ref_f[lo:hi], seg, mode='valid')
    k = int(np.argmax(c))
    return lo + k - our_from, float(c[k] / len(seg))


def third_octaves():
    centres = [1000 * 2 ** (n / 3) for n in range(-13, 12)]       # 50 Hz .. 12.7 kHz
    return [(c, c / 2 ** (1 / 6), c * 2 ** (1 / 6)) for c in centres]


def band_db(x, bands):
    f, p = psd(x)
    tot = p.sum()
    return [10 * np.log10(p[(f >= lo) & (f < hi)].sum() / tot + 1e-20) for _, lo, hi in bands]


def psd(x):
    n = 8192
    if len(x) < n:
        x = np.pad(x, (0, n - len(x)))
    frames = np.lib.stride_tricks.sliding_window_view(x, n)[::n // 2]
    p = (np.abs(np.fft.rfft(frames * np.hanning(n), axis=1)) ** 2).mean(axis=0)
    return np.fft.rfftfreq(n, 1 / RATE), p


def rms_db(x):
    return 20 * np.log10(np.sqrt((x ** 2).mean()) + 1e-12)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ref')
    ap.add_argument('ours', nargs='+')
    ap.add_argument('--secs', type=float, default=25.0)
    ap.add_argument('--json')
    a = ap.parse_args()
    ref = load(a.ref)
    ref_f = flux(ref)
    bands = third_octaves()
    report = {'ref': a.ref, 'secs': a.secs, 'ours': {}}
    win = int(a.secs * RATE / HOP)
    for path in a.ours:
        x = load(path)
        xf = flux(x)
        start = int(np.argmax(np.abs(x) > 0.002) / HOP)                 # first audible hop
        n8 = int(8 * RATE / HOP)
        lag1, c1 = lag_of(ref_f, xf, start, n8, (0, len(ref_f)))
        late = start + win - n8
        lag2, c2 = lag_of(ref_f, xf, late, n8, (lag1 - 200, lag1 + 200)) if late + n8 <= len(xf) else (lag1, 0.0)
        drift = (lag2 - lag1) * HOP / RATE
        ours = x[start * HOP:(start + win) * HOP]
        r0 = (start + lag1) * HOP
        refw = ref[r0:r0 + len(ours)]
        ob, rb = band_db(ours, bands), band_db(refw, bands)
        seg = []
        octs = [(c, c / 2 ** 0.5, c * 2 ** 0.5) for c in (63, 125, 250, 500, 1000, 2000, 4000, 8000)]
        for s in range(0, int(a.secs) - 4, 5):
            o = ours[s * RATE:(s + 5) * RATE]
            r = refw[s * RATE:(s + 5) * RATE]
            seg.append([round(u - v, 1) for u, v in zip(band_db(o, octs), band_db(r, octs))])
        print('== %s' % path)
        print('  aligned: ours %.2f s = ref %.2f s (onset correlation %.2f); late window lag %+.3f s '
              '(corr %.2f) -> tempo drift %+.3f s over %.0f s' % (
                  start * HOP / RATE, r0 / RATE, c1, drift, c2, drift, a.secs - 8))
        print('  overall RMS: ours %.1f dBFS, ref %.1f dBFS (master gain %+.1f dB)' % (
            rms_db(ours), rms_db(refw), rms_db(ours) - rms_db(refw)))
        print('  balance, third-octave band share of total power (dB), ours - ref:')
        for (c, _, _), u, v in zip(bands, ob, rb):
            bar = '+' * min(30, int(max(0, u - v))) if u > v else '-' * min(30, int(max(0, v - u)))
            print('    %6.0f Hz  ours %6.1f  ref %6.1f  %+5.1f %s' % (c, u, v, u - v, bar))
        print('  octave balance (ours - ref, dB) per 5 s:   ' + ' '.join('%6d' % c for c, _, _ in octs))
        for i, row in enumerate(seg):
            print('    %3d-%-3d s %s' % (i * 5, i * 5 + 5, ' '.join('%+6.1f' % d for d in row)))
        report['ours'][path] = {'start_s': start * HOP / RATE, 'ref_s': r0 / RATE, 'corr': c1,
                                'drift_s': drift, 'rms_ours': rms_db(ours), 'rms_ref': rms_db(refw),
                                'bands_hz': [c for c, _, _ in bands], 'ours_db': ob, 'ref_db': rb,
                                'octave_segments': seg}
    if a.json:
        json.dump(report, open(a.json, 'w'), indent=1)


if __name__ == '__main__':
    main()
