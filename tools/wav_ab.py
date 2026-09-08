#!/usr/bin/env python3
"""A/B level comparison: arcade (MAME altbeast -wavwrite) vs our 32X ROM
(MAME 32x -wavwrite). KIT — the level-matching oracle.

Removes DC (MAME's 32X PWM output idles at a large DC offset), mixes to
mono, aligns both at the first music onset, then reports per-second RMS
and per-band RMS (bass / low-mid / mid / high) in dB, the ROM-minus-
arcade delta, and the equivalent YM2612 TL offset (0.75 dB per unit).
Positive delta = ROM is louder -> add that many TL units.

  wav_ab.py arcade.wav rom.wav [--secs 25]
"""
import argparse
import sys
import wave

import numpy as np

BANDS = [("bass", 0, 250), ("lowmid", 250, 1000), ("mid", 1000, 4000),
         ("high", 4000, 12000)]


def load(path):
    import io
    import struct
    b = bytearray(open(path, "rb").read())
    try:
        w = wave.open(io.BytesIO(bytes(b)))
    except wave.Error:
        # MAME's 32x session never finalizes -wavwrite's RIFF sizes
        # (observed 2026-09-02); the PCM is intact, so fix the header.
        i = b.find(b"data")
        struct.pack_into("<I", b, 4, len(b) - 8)
        struct.pack_into("<I", b, i + 4, len(b) - (i + 8))
        w = wave.open(io.BytesIO(bytes(b)))
    sr, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
    d = np.frombuffer(w.readframes(n), dtype=np.int16).astype(float)
    if ch == 2:
        d = d.reshape(-1, 2).mean(axis=1)
    return d, sr


def dc_remove(x, sr):
    # high-pass by subtracting a 50ms moving mean (kills the PWM DC and
    # any slow drift, leaves everything above ~20Hz)
    k = int(sr * 0.05)
    m = np.convolve(x, np.ones(k) / k, mode="same")
    return x - m


def onset(x, sr, skip, thresh=300.0):
    # first 50ms window after `skip` seconds whose RMS exceeds thresh —
    # skip the boot blip / inject region so we align on the MUSIC
    k = int(sr * 0.05)
    for i in range(int(skip * sr), len(x) - k, k):
        if np.sqrt((x[i:i + k] ** 2).mean()) > thresh:
            return i
    return int(skip * sr)


def db(v):
    return 20 * np.log10(max(v, 1e-9))


def band_rms(x, sr):
    sp = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    fr = np.fft.rfftfreq(len(x), 1 / sr)
    out = {}
    for name, lo, hi in BANDS:
        m = (fr >= lo) & (fr < hi)
        out[name] = np.sqrt((sp[m] ** 2).sum()) / len(x)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("arcade")
    ap.add_argument("rom")
    ap.add_argument("--secs", type=float, default=25.0)
    ap.add_argument("--skip", type=float, default=2.5,
                    help="ignore the first N seconds (boot blip / inject)")
    a = ap.parse_args()

    A, sa = load(a.arcade)
    R, sr = load(a.rom)
    A = dc_remove(A, sa)
    R = dc_remove(R, sr)
    oa, orr = onset(A, sa, a.skip), onset(R, sr, a.skip)
    A = A[oa:oa + int(a.secs * sa)]
    R = R[orr:orr + int(a.secs * sr)]
    n = min(len(A) // sa, len(R) // sr)
    print(f"aligned at onsets: arcade {oa/sa:.2f}s, rom {orr/sr:.2f}s; "
          f"comparing {n}s")

    print("\nper-second RMS (dBFS)   arcade    rom    delta")
    da, dr = [], []
    for s in range(n):
        ra = np.sqrt((A[s * sa:(s + 1) * sa] ** 2).mean())
        rr = np.sqrt((R[s * sr:(s + 1) * sr] ** 2).mean())
        da.append(db(ra / 32768)); dr.append(db(rr / 32768))
        print(f"  {s:2d}s               {da[-1]:6.1f}  {dr[-1]:6.1f}  {dr[-1]-da[-1]:+5.1f}")
    # overall: only seconds where BOTH are music (exclude rests/silence)
    mus = [(x, y) for x, y in zip(da, dr) if x > -60 and y > -60]
    ma = np.mean([x for x, _ in mus]); mr = np.mean([y for _, y in mus])
    print(f"\nOVERALL mean RMS over {len(mus)} music seconds: arcade {ma:.1f} dBFS, "
          f"rom {mr:.1f} dBFS, delta {mr-ma:+.1f} dB  -> TL offset "
          f"{round((mr-ma)/0.75):+d} units")

    ba = band_rms(A[:n * sa], sa)
    br = band_rms(R[:n * sr], sr)
    print("\nper-band RMS (dB rel)   arcade    rom    delta   -> TL")
    for name, _, _ in BANDS:
        d = db(br[name]) - db(ba[name])
        print(f"  {name:7}              {db(ba[name]):6.1f}  {db(br[name]):6.1f}  "
              f"{d:+5.1f}   {round(d/0.75):+d}")


if __name__ == "__main__":
    main()
