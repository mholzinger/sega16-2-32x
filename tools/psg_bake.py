#!/usr/bin/env python3
"""Bake a song's overflow YM2151 channels into a per-frame PSG stream. KIT (2026-11-01).
    python3 tools/psg_bake.py --vgz "DIR/01 Rise....vgz" --cmd 0x94 [--channels 6,7] [--fps 60]
        [--rate 16000] [--ref 8192] [--out build/gate/psg]
For each channel: the exact engine (sndtest/opm, jt51-exact, through tools/opm_ab.py's host
build) renders it alone; per frame the PSG gets the channel's pitch as a 10-bit period (a
note under the PSG's 109.3 Hz floor is raised an octave until it fits: Mike, 2026-11-01)
and its level as the 4-bit attenuation (2 dB steps) of the frame's peak against --ref, so
the attack, decay and release are the engine's own, not a flat TL. Output, per song:
  OUT/<cmd>_psg.json   frames: [[period_a, att_a, period_b, att_b], ...] plus the header
  OUT/<cmd>_ab.wav     left = the engine's own render of the channels, right = the baked
                       squares (the check), at --rate
and a summary line: frames, bytes, octave-raised share, level correlation. Control data,
not audio: ~4 bytes a frame before packing. Reads the rips (gitignored); never committed.
"""
import argparse
import json
import math
import os
import sys
import wave
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import numpy as np                           # noqa: E402
import opm_ab                                # noqa: E402
from opm2opn import opm_freq                 # noqa: E402
from vgm2ym import load                      # noqa: E402

PSG_CLOCK = 3579545.0
FLOOR = PSG_CLOCK / (32.0 * 1023)


def pitch_track(events, fm_rate, ch, nframes, fps):
    """the channel's (period, keyed) per frame from its register stream; a note under the
    floor is raised an octave until the period fits 10 bits"""
    kc = kf = 0
    keyed = False
    per = [0] * nframes
    on = [False] * nframes
    raised = 0
    j = 0
    ev = [(t / fm_rate, r, v) for t, r, v in events]
    cur_period = 0
    for k in range(nframes):
        t_end = (k + 1) / fps
        while j < len(ev) and ev[j][0] < t_end:
            _, r, v = ev[j]
            j += 1
            if r == 0x08 and (v & 7) == ch:
                keyed = bool(v & 0x78)
            elif r == 0x28 + ch:
                kc = v
            elif r == 0x30 + ch:
                kf = v
        f = opm_freq(kc, kf)
        oct_up = 0
        while f < FLOOR and oct_up < 4:
            f *= 2.0
            oct_up += 1
        div = max(1, min(1023, int(round(PSG_CLOCK / (32.0 * f)))))
        if keyed and oct_up:
            raised += 1
        cur_period = div
        per[k] = cur_period
        on[k] = keyed
    return per, on, raised


def level_track(x, rate, nframes, fps, ref):
    """4-bit PSG attenuation per frame from the render's RMS in that frame: a square of
    amplitude A has RMS A, so matching RMS matches loudness (peak-matching ran +3.9 dB)"""
    att = [15] * nframes
    n = len(x)
    for k in range(nframes):
        a = int(k * rate / fps)
        b = int((k + 1) * rate / fps)
        if a >= n:
            break
        pk = float(np.sqrt(np.mean(x[a:min(b, n)] ** 2))) if b > a else 0.0
        if pk <= 0:
            continue
        db = 20.0 * math.log10(pk / ref)
        s = int(round(-db / 2.0))
        att[k] = 15 if s >= 15 else (0 if s < 0 else s)
    return att


def squares(frames, idx, rate, fps, ref):
    """the baked stream as a square wave, for the check"""
    n = int(len(frames) * rate / fps)
    out = np.zeros(n)
    phase = 0.0
    for k, fr in enumerate(frames):
        per, att = fr[idx], fr[idx + 1]
        a = int(k * rate / fps)
        b = min(int((k + 1) * rate / fps), n)
        if att >= 15 or per <= 0:
            continue
        amp = ref * 10 ** (-att * 2.0 / 20.0)
        f = PSG_CLOCK / (32.0 * per)
        for i in range(a, b):
            out[i] = amp if phase < 0.5 else -amp
            phase += f / rate
            if phase >= 1.0:
                phase -= 1.0
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--vgz', required=True)
    ap.add_argument('--cmd', required=True)
    ap.add_argument('--channels', default='6,7')
    ap.add_argument('--fps', type=float, default=60.0)
    ap.add_argument('--rate', type=int, default=16000)
    ap.add_argument('--ref', type=float, default=8192.0, help='engine amplitude that is PSG attenuation 0')
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'gate', 'psg'))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    chans = [int(c) for c in a.channels.split(',')]
    seg, loop_s, total_s = load(a.vgz)
    secs = total_s
    nframes = int(math.ceil(secs * a.fps))
    nsamp = int(math.ceil(secs * a.rate))
    tracks = []
    renders = []
    for ch in chans:
        ev = opm_ab.ym_events(a.vgz, {ch}, secs + 1)
        evp = os.path.join(a.out, '%s_ch%d.ev' % (a.cmd, ch))
        opm_ab.write_events(ev, evp)
        x = opm_ab.opm_render(evp, nsamp, a.rate, [ch], os.path.join(a.out, '%s_ch%d.s16' % (a.cmd, ch)))
        renders.append(x)
        per, on, raised = pitch_track(ev, opm_ab.FM_RATE, ch, nframes, a.fps)
        att = level_track(x, a.rate, nframes, a.fps, a.ref)
        tracks.append((per, att, raised, sum(on), float(np.max(np.abs(x)))))
    frames = [[tracks[0][0][k], tracks[0][1][k], tracks[1][0][k], tracks[1][1][k]] for k in range(nframes)]
    hdr = dict(cmd=a.cmd, vgz=os.path.basename(a.vgz), channels=chans, fps=a.fps, frames=nframes,
               loop_s=loop_s, total_s=total_s, ref=a.ref, floor_hz=FLOOR)
    with open(os.path.join(a.out, '%s_psg.json' % a.cmd), 'w') as f:
        json.dump(dict(header=hdr, frames=frames), f)
    # the check: engine mix on the left, the squares on the right
    eng = renders[0][:nsamp] + renders[1][:nsamp]
    sq = squares(frames, 0, a.rate, a.fps, a.ref) + squares(frames, 2, a.rate, a.fps, a.ref)
    n = min(len(eng), len(sq))
    eng, sq = eng[:n], sq[:n]
    peak = max(np.max(np.abs(eng)), np.max(np.abs(sq)), 1.0)
    st = np.zeros((n, 2))
    st[:, 0] = eng / peak * 32000
    st[:, 1] = sq / peak * 32000
    with wave.open(os.path.join(a.out, '%s_ab.wav' % a.cmd), 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(a.rate)
        w.writeframes(st.astype('<i2').tobytes())
    # per-frame level agreement
    fl = int(a.rate / a.fps)
    nf = n // fl
    e_rms = np.sqrt(np.mean(eng[:nf * fl].reshape(nf, fl) ** 2, axis=1))
    s_rms = np.sqrt(np.mean(sq[:nf * fl].reshape(nf, fl) ** 2, axis=1))
    corr = float(np.corrcoef(e_rms, s_rms)[0, 1]) if nf > 2 else 0.0
    e_db = 20 * np.log10(np.mean(e_rms) + 1e-9)
    s_db = 20 * np.log10(np.mean(s_rms) + 1e-9)
    changes = sum(1 for k in range(1, nframes) if frames[k] != frames[k - 1])
    print('%s %s: %d frames (%.1f s at %.2f fps), %d bytes raw, %d frames change (%.0f%%)' % (
        a.cmd, hdr['vgz'], nframes, secs, a.fps, nframes * 4, changes, changes * 100.0 / max(nframes, 1)))
    for ch, tr in zip(chans, tracks):
        print('  ch%d: keyed %d frames (%.0f%%), octave-raised %d of them (%.0f%%), engine peak %.0f (ref %.0f), att 0..14 in %d frames' % (
            ch, tr[3], tr[3] * 100.0 / nframes, tr[2], tr[2] * 100.0 / max(tr[3], 1), tr[4], a.ref, sum(1 for v in tr[1] if v < 15)))
    print('  level track: per-frame RMS correlation engine vs squares %.3f; mean level engine %.1f dB, squares %.1f dB (squares %+.1f dB)' % (
        corr, e_db, s_db, s_db - e_db))
    print('  wrote %s and %s' % (os.path.join(a.out, '%s_psg.json' % a.cmd), os.path.join(a.out, '%s_ab.wav' % a.cmd)))


if __name__ == '__main__':
    main()
