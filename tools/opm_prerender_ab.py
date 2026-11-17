#!/usr/bin/env python3
"""The prerender route's fidelity: chip-continuous vs note-sample replay, channels 6/7. KIT.

A = the exact model (sndtest/opm, bit-identical to jt51) rendering the whole song's register
    stream, output = channels 6 and 7 only. This is what the arcade chip plays.
B = what a sample player would play: every unique note rendered ONCE in isolation (from
    silence, phase 0), then placed at each key-on and CUT at the channel's next key-on.
The difference is the prerender's inexactness: a retriggered note on the chip restarts its
envelope from the fading tail's level, the sample starts from silence; and the chip's phase
is not reset to the sample's.

Both sides use the same write convention as the gate (tools/opm_ab.py: latency 2, 2 samples
a write), so key-ons land on the same chip samples and the compare is sample-aligned.

  tools/opm_prerender_ab.py [--songs 0x94,0x97] [--out build/prerender/ab]
Prints per song: unique notes, retriggers (key-on while the previous note still sounded),
level difference per 0.25 s window (max, 95th percentile), SNR; writes <cmd>_chip.wav and
<cmd>_replay.wav at the native 62.5 kHz for an ear test.
"""
import argparse
import os
import struct
import subprocess
import sys
import wave

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from vgm2ym import load                                       # noqa: E402
import opm_ab                                                 # noqa: E402
from opm_prerender_census import AUDIO, SONGS, CLOCK, NATIVE, ch_regs   # noqa: E402

LATENCY, WRITE_SAMPLES = 2, 2
TICK = 739.5 / 44100.0 * NATIVE     # the sound driver's update period in chip samples (~59.6 Hz)
Q = [TICK]                          # mid-write/hold quantum; --exact sets 1 sample (no dedup)
LEAD = 256
TAIL_CAP = 8.0
WIN = 0.25


def landed(events):
    """(due sample, reg, val) -> (landed sample, reg, val) under the write convention."""
    out, prev = [], -10 ** 9
    for due, r, v in events:
        land = max(due + LATENCY, prev + WRITE_SAMPLES)
        out.append((land, r, v))
        prev = land
    return out


def render(exe, events, n, mask, path):
    with open(path + '.bin', 'wb') as f:
        for t, r, v in events:
            f.write(struct.pack('<IBB', t, r, v))
    subprocess.run([exe, path + '.bin', path, str(n), '0', str(mask), str(CLOCK), '-',
                    str(LATENCY), str(WRITE_SAMPLES)], check=True, stdout=subprocess.DEVNULL)
    return np.fromfile(path, dtype='<i2').astype(np.int32)


def notes_of(land_ev, c, total_n):
    """Notes on channel c from the LANDED stream: (snapshot, hold_ticks, mid, on, next_on)."""
    regs = ch_regs(c)
    shadow = [0] * 256
    notes, cur = [], None
    for L, r, v in land_ev:
        if r == 0x08 and (v & 7) == c:
            if (v >> 3) & 0xF:
                if cur is not None:
                    notes.append(close(cur, L))
                cur = (tuple(shadow[x] for x in regs) + ((v >> 3) & 0xF,), L, [])
            elif cur is not None:
                notes.append(close(cur, L))
                cur = None
            continue
        if r in regs:
            if cur is not None and shadow[r] != v:
                cur[2].append((int(round((L - cur[1]) / Q[0])), r, v))
            shadow[r] = v
    if cur is not None:
        notes.append(close(cur, total_n))
    return [n + ((notes[i + 1][3] if i + 1 < len(notes) else total_n),) for i, n in enumerate(notes)]


def close(cur, L_off):
    snap, L_on, mid = cur
    return (snap, max(1, int(round((L_off - L_on) / Q[0]))), tuple(mid), L_on)


def note_wave(exe, snap, hold, mid, c, work, need):
    regs = ch_regs(c)
    ev = [(0, r, v) for r, v in zip(regs, snap[:-1])]
    ev.append((LEAD, 0x08, (snap[-1] << 3) | c))
    for tk, r, v in mid:
        ev.append((LEAD + int(tk * Q[0]), r, v))
    t_off = LEAD + int(hold * Q[0])
    ev.append((t_off, 0x08, c))
    ev.sort(key=lambda e: e[0])
    kon = landed(ev)[len(regs)][0]
    n = min(kon + need, t_off + int(TAIL_CAP * NATIVE)) + 1
    x = render(exe, ev, n, 1 << c, os.path.join(work, 'note.s16'))[kon:]
    nz = np.flatnonzero(np.abs(x) > 1)
    return x[:int(nz[-1]) + 1] if len(nz) else x[:0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--songs', default=','.join(f'0x{c:02X}' for c, _ in SONGS))
    ap.add_argument('--channels', default='6,7')
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'prerender', 'ab'))
    ap.add_argument('--exact', action='store_true', help='mid-note writes and holds at exact samples (no dedup): isolates tick quantisation')
    a = ap.parse_args()
    if a.exact:
        Q[0] = 1.0
    want = {int(s, 16) for s in a.songs.split(',')}
    chans = [int(x) for x in a.channels.split(',')]
    exe = opm_ab.build_opm()
    os.makedirs(a.out, exist_ok=True)
    print(f"{'song':5} {'uniq':>5} {'retrig':>6} {'retrig%':>7} {'win max dB':>10} {'win p95 dB':>10} "
          f"{'>0.5 dB':>8} {'SNR dB':>7}")
    for cmd, fname in SONGS:
        if cmd not in want:
            continue
        seg, _, total_s = load(os.path.join(AUDIO, fname))
        events = [(int(round(t * NATIVE)), r, v) for t, r, v in seg['ym']]
        n = int(total_s * NATIVE) + int(TAIL_CAP * NATIVE)
        mask = sum(1 << c for c in chans)
        A = render(exe, events, n, mask, os.path.join(a.out, f'{cmd:02X}_chip.s16'))
        land = landed(events)
        B = np.zeros(n, dtype=np.int32)
        uniq, retrig, total = {}, 0, 0
        for c in chans:
            notes = notes_of(land, c, n)
            occ = {}
            for snap, hold, mid, on, nxt in notes:
                occ.setdefault((snap, hold, mid), []).append((on, nxt))
            for key, o in occ.items():
                need = max(nx - on for on, nx in o)
                w = note_wave(exe, *key, c, a.out, need)
                uniq[(c,) + key] = len(w)
                for on, nx in o:
                    m = min(len(w), nx - on, n - on)
                    B[on:on + m] += w[:m]
            # a retrigger = the next key-on arrives while this note's sample still has signal
            for snap, hold, mid, on, nxt in notes:
                total += 1
                if uniq[(c, snap, hold, mid)] > nxt - on:
                    retrig += 1
        w = int(WIN * NATIVE)
        nw = n // w
        la = np.array([np.sqrt(np.mean(A[i * w:(i + 1) * w].astype(np.float64) ** 2)) for i in range(nw)])
        lb = np.array([np.sqrt(np.mean(B[i * w:(i + 1) * w].astype(np.float64) ** 2)) for i in range(nw)])
        live = la > la.max() / 1000.0                       # windows within 60 dB of the loudest
        d = 20 * np.log10(np.maximum(lb[live], 1e-9) / np.maximum(la[live], 1e-9))
        err = (A - B).astype(np.float64)
        snr = 10 * np.log10(np.sum(A.astype(np.float64) ** 2) / max(np.sum(err ** 2), 1e-9))
        print(f"0x{cmd:02X}  {len(uniq):5} {retrig:6} {100.0 * retrig / max(1, total):7.1f} "
              f"{np.max(np.abs(d)):10.2f} {np.percentile(np.abs(d), 95):10.2f} "
              f"{100.0 * np.mean(np.abs(d) > 0.5):7.1f}% {snr:7.1f}")
        for name, x in (('chip', A), ('replay', B)):
            wf = wave.open(os.path.join(a.out, f'{cmd:02X}_{name}.wav'), 'wb')
            wf.setnchannels(1); wf.setsampwidth(2); wf.setframerate(int(NATIVE))
            wf.writeframes(np.clip(x, -32768, 32767).astype('<i2').tobytes()); wf.close()


if __name__ == '__main__':
    main()
