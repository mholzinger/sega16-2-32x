#!/usr/bin/env python3
"""Per-channel A/B: each arcade YM2151 channel against our YM2612 transcode of it, on ymfm.

    python3 tools/channel_ab.py --vgz "srcref/audio/Altered_Beast_(Sega_System_16B)/01 Rise....vgz" \
        --cmd 0x94 [--header sndtest/md/render_music.h] [--secs 22] [--out DIR]

WHY (2026-10-30). The lab's music sounded wrong in tone and balance (Mike: "a, d, mostly") with
tempo and the channel data already matching the arcade. The question left is what the YM2612 does
with each arcade voice, so both sides are rendered by one emulator family -- ymfm's YM2151 for the
arcade rip, ymfm's YM2612 for our stream -- one channel at a time (every other channel's key-ons
masked), and compared: loudness (RMS while the channel sounds), brightness (spectral centroid) and
shape (octave-band profile). A difference here is the transcode or the chip, not an emulator.

Needs ymfm's example renderer: ~/src/ymfm/examples/vgmrender/vgmrender (YMFM_RENDER overrides).
It ignores PSG writes, so channels 6-7 (our PSG squares) are not measured.
"""
import argparse
import gzip
import os
import re
import struct
import subprocess
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lzss import decompress                   # noqa: E402

RENDER = os.environ.get('YMFM_RENDER', os.path.expanduser('~/src/ymfm/examples/vgmrender/vgmrender'))
VGM_RATE = 44100.0


def vgm_header(total_samples, ym2151=0, ym2612=0, sn=0):
    h = bytearray(0x40)
    h[0:4] = b'Vgm '
    struct.pack_into('<I', h, 0x08, 0x150)
    struct.pack_into('<I', h, 0x0C, sn)
    struct.pack_into('<I', h, 0x18, total_samples)
    struct.pack_into('<I', h, 0x2C, ym2612)
    struct.pack_into('<I', h, 0x30, ym2151)
    struct.pack_into('<I', h, 0x34, 0x0C)             # data at 0x40
    return h


def finish(h, body):
    out = h + body + b'\x66'
    struct.pack_into('<I', out, 0x04, len(out) - 4)
    return bytes(out)


def arcade_solo(vgz, solo, secs):
    """The rip's YM2151 stream to `secs`, every channel but `solo` keyed off (None = all)."""
    d = gzip.decompress(open(vgz, 'rb').read())
    u32 = lambda o: struct.unpack('<I', d[o:o + 4])[0]
    i = (u32(0x34) + 0x34) if u32(0x08) >= 0x150 and u32(0x34) else 0x40
    body = bytearray()
    samples = 0
    limit = int(secs * VGM_RATE)
    while i < len(d) and samples < limit:
        k = d[i]
        if k == 0x66:
            break
        if k == 0x54:
            r, v = d[i + 1], d[i + 2]
            if r == 0x08 and solo is not None and (v & 7) != solo:
                v &= 0x07                              # key off: no slots
            body += bytes([0x54, r, v])
            i += 3
        elif k == 0x61:
            n = d[i + 1] | d[i + 2] << 8
            samples += n
            body += d[i:i + 3]
            i += 3
        elif k in (0x62, 0x63):
            samples += 735 if k == 0x62 else 882
            body.append(k)
            i += 1
        elif 0x70 <= k <= 0x7F:
            samples += (k & 15) + 1
            body.append(k)
            i += 1
        elif k == 0x50:
            i += 2
        else:
            raise SystemExit('channel_ab: unhandled VGM command 0x%02X at 0x%X' % (k, i))
    return finish(vgm_header(samples, ym2151=u32(0x30) or 4000000), body)


def ours_solo(stream, solo, secs):
    """Our Z80-player stream as a YM2612 VGM, every FM channel but `solo` keyed off."""
    body = bytearray()
    t_ms = 0.0
    done = 0
    i = 0
    while i < len(stream) and t_ms < secs * 1000:
        b = stream[i]
        if b < 0x80:
            t_ms += b
            want = int(round(t_ms * VGM_RATE / 1000.0))
            while want - done > 0:
                n = min(0xFFFF, want - done)
                body += bytes([0x61, n & 0xFF, n >> 8])
                done += n
            i += 1
        elif b in (0xF0, 0xF1):
            r, v = stream[i + 1], stream[i + 2]
            if b == 0xF0 and r == 0x28 and solo is not None:
                c = v & 7
                c = c - 1 if c >= 4 else c
                if c != solo:
                    v &= 0x0F                          # key off: no slots
            body += bytes([0x52 if b == 0xF0 else 0x53, r, v])
            i += 3
        elif b == 0xF2:
            body += bytes([0x50, stream[i + 1]])
            i += 2
        elif b == 0xFF:
            break
        else:
            i += 1
    return finish(vgm_header(done, ym2612=7670454, sn=3579545), body)


def render(vgm_bytes, path):
    open(path + '.vgm', 'wb').write(vgm_bytes)
    # one fixed scale for every render (vgmrender otherwise peak-normalizes each file,
    # which made a solo channel read louder than the mix containing it)
    env = dict(os.environ, YMFM_FIXED_SCALE=os.environ.get('YMFM_FIXED_SCALE', '120000'))
    r = subprocess.run([RENDER, path + '.vgm', '-o', path + '.wav', '-r', '44100'],
                       check=True, capture_output=True, env=env, text=True)
    if 'clamping' in r.stderr:
        print('  warning: %s clipped (%s)' % (os.path.basename(path), r.stderr.strip().splitlines()[-1]))
    w = wave.open(path + '.wav')
    x = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').reshape(-1, w.getnchannels())
    x = x.mean(axis=1) / 32768.0
    # 25 Hz high-pass: the chip models carry a DC offset that moves with which channels
    # are keyed; left in, it inflated a solo channel 10 dB over the mix containing it
    from scipy.signal import butter, sosfiltfilt
    return sosfiltfilt(butter(2, 25.0, 'highpass', fs=44100.0, output='sos'), x)


OCT = [(c, c / 2 ** 0.5, c * 2 ** 0.5) for c in (63, 125, 250, 500, 1000, 2000, 4000, 8000)]


def profile(x):
    n = 8192
    if len(x) < n:
        return None
    fr = np.lib.stride_tricks.sliding_window_view(x, n)[::n // 2]
    p = (np.abs(np.fft.rfft(fr * np.hanning(n), axis=1)) ** 2).mean(axis=0)
    f = np.fft.rfftfreq(n, 1 / 44100.0)
    tot = p.sum() + 1e-30
    bands = [10 * np.log10(p[(f >= lo) & (f < hi)].sum() / tot + 1e-12) for _, lo, hi in OCT]
    centroid = (f * p).sum() / tot
    return bands, centroid


def active_rms(x):
    """RMS over the 50 ms windows where the channel sounds (> -60 dBFS)."""
    w = 2205
    seg = x[:len(x) // w * w].reshape(-1, w)
    r = np.sqrt((seg ** 2).mean(axis=1))
    on = r > 10 ** (-60 / 20)
    return 20 * np.log10(np.sqrt((r[on] ** 2).mean()) + 1e-12) if on.any() else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--vgz', required=True)
    ap.add_argument('--cmd', type=lambda x: int(x, 0), default=0x94)
    ap.add_argument('--header', default='sndtest/md/render_music.h')
    ap.add_argument('--secs', type=float, default=22.0)
    ap.add_argument('--out', default='/tmp/channel_ab')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    txt = open(a.header).read()
    m = re.search(r'rin_(?i:%02x)\[\d+\]\s*=\s*\{(.*?)\};' % a.cmd, txt, re.S)
    stream = decompress(bytes(int(x, 0) for x in re.findall(r'0x[0-9a-fA-F]+', m.group(1))))

    arc_mix = render(arcade_solo(a.vgz, None, a.secs), os.path.join(a.out, 'arc_mix'))
    our_mix = render(ours_solo(stream, None, a.secs), os.path.join(a.out, 'our_mix'))
    am, om = active_rms(arc_mix), active_rms(our_mix)
    print('cmd 0x%02X, first %.0f s, ymfm YM2151 (arcade rip) vs ymfm YM2612 (our stream), FM only'
          % (a.cmd, a.secs))
    print('mix: arcade %.1f dBFS, ours %.1f dBFS' % (am, om))
    print('%-3s %-26s %-26s %-9s %s' % ('ch', 'arcade: level / centroid', 'ours: level / centroid',
                                        'level', 'octave shape ours-arcade (63..8k Hz, dB)'))
    rows = []
    for c in range(6):
        x = render(arcade_solo(a.vgz, c, a.secs), os.path.join(a.out, 'arc_%d' % c))
        y = render(ours_solo(stream, c, a.secs), os.path.join(a.out, 'our_%d' % c))
        lx, ly = active_rms(x), active_rms(y)
        px, py = profile(x), profile(y)
        if lx is None or ly is None or px is None or py is None:
            print('%-3d silent in %s' % (c, 'arcade' if lx is None else 'ours'))
            continue
        # relative to each side's own mix, so the master gain cancels
        rel = (ly - om) - (lx - am)
        shape = [round(v - u, 1) for u, v in zip(px[0], py[0])]
        print('%-3d %6.1f dB / %5.0f Hz       %6.1f dB / %5.0f Hz       %+5.1f dB  %s' % (
            c, lx - am, px[1], ly - om, py[1], rel, ' '.join('%+5.1f' % s for s in shape)))
        rows.append({'ch': c, 'arc_rel_db': lx - am, 'our_rel_db': ly - om, 'level_diff_db': rel,
                     'arc_centroid': px[1], 'our_centroid': py[1], 'shape_diff_db': shape})
    import json
    json.dump({'cmd': a.cmd, 'secs': a.secs, 'arc_mix_db': am, 'our_mix_db': om, 'channels': rows},
              open(os.path.join(a.out, 'channel_ab.json'), 'w'), indent=1)


if __name__ == '__main__':
    main()
