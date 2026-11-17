#!/usr/bin/env python3
"""sndtest/opm (our YM2151 channel engine) against jt51, the RTL the arcade core runs. KIT.

    python3 tools/opm_ab.py --vgz "srcref/audio/.../01 Rise....vgz" [--channels 6,7] [--secs 22]
        [--rates 0,44100,32000,22050,16000] [--out DIR] [--ymfm]

WHY (2026-10-30, hybrid 1). The hybrid plan synthesizes the two arcade channels the YM2612
cannot hold on the SH-2. The spec is jt51 (srcref/jtcores/modules/jt51, Jotego, GPL --
derive, never copy), so the oracle is jt51 itself: tools/jt51_ref.cpp under Verilator,
fed the song's register writes for the chosen channels only, read at the `sample` strobe
on xleft (what jts16b_snd.v:212 takes).

Two gates:
  NATIVE   rate 0: our engine at the chip's own clock/64 Hz (4 MHz/64 = 62.5 kHz on System 16B), sample by sample against
           jt51 -- identical samples, max error, SNR
  RATE     each lower rate the SH-2 might run: jt51 resampled to it (polyphase) vs ours,
           level while sounding and octave bands 63 Hz..min(8k, nyquist): the bar is 1 dB
--ymfm adds ymfm's YM2151 (vgmrender, 44.1 kHz) on the same stream as a second witness.

Builds on demand into build/jt51_ref (needs verilator) and build/opm_host (cc).
"""
import argparse
import gzip
import os
import struct
import subprocess
import sys

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
JT51 = os.path.join(ROOT, 'srcref', 'jtcores', 'modules', 'jt51')
CLOCK = 4000000            # System 16B: the YM2151 runs at 4 MHz (jts16b_snd.v:24); set from the VGZ
FM_RATE = CLOCK / 64.0
GLOBAL_KEEP = {0x01, 0x0F, 0x14, 0x18, 0x19, 0x1B}


def ym_events(vgz, chans, secs):
    """[(native sample, reg, val)] for the YM2151 writes that touch `chans` (key-ons of
    other channels dropped, their registers skipped), to `secs`."""
    d = gzip.decompress(open(vgz, 'rb').read())
    u32 = lambda o: struct.unpack('<I', d[o:o + 4])[0]
    i = (u32(0x34) + 0x34) if u32(0x08) >= 0x150 and u32(0x34) else 0x40
    global CLOCK, FM_RATE
    CLOCK = u32(0x30) & 0x3FFFFFFF or CLOCK
    FM_RATE = CLOCK / 64.0
    t = 0
    limit = secs * 44100
    ev = []
    while i < len(d) and t < limit:
        k = d[i]
        if k == 0x66:
            break
        if k == 0x54:
            r, v = d[i + 1], d[i + 2]
            i += 3
            if r == 0x08:
                keep = (v & 7) in chans
            elif r < 0x20:
                keep = r in GLOBAL_KEEP
            else:
                keep = (r & 7) in chans
            if keep:
                ev.append((int(round(t * FM_RATE / 44100.0)), r, v))
        elif k == 0x61:
            t += d[i + 1] | d[i + 2] << 8
            i += 3
        elif k in (0x62, 0x63):
            t += 735 if k == 0x62 else 882
            i += 1
        elif 0x70 <= k <= 0x7F:
            t += (k & 15) + 1
            i += 1
        elif k == 0x50:
            i += 2
        else:
            raise SystemExit('opm_ab: unhandled VGM command 0x%02X at 0x%X' % (k, i))
    return ev


def write_events(ev, path):
    with open(path, 'wb') as f:
        for t, r, v in ev:
            f.write(struct.pack('<IBB', t, r, v))


def build_jt51():
    b = os.path.join(ROOT, 'build', 'jt51_ref')
    exe = os.path.join(b, 'obj_dir', 'jt51_ref')
    src = os.path.join(ROOT, 'tools', 'jt51_ref.cpp')
    if os.path.exists(exe) and os.path.getmtime(exe) > os.path.getmtime(src):
        return exe
    rtl = os.path.join(b, 'rtl')
    os.makedirs(rtl, exist_ok=True)
    names = subprocess.run(['git', '-C', JT51, 'ls-tree', '--name-only', 'HEAD', 'hdl/'],
                           capture_output=True, text=True, check=True).stdout.split()
    for n in names:
        if n.endswith('.v'):
            body = subprocess.run(['git', '-C', JT51, 'show', 'HEAD:' + n], capture_output=True, check=True).stdout
            open(os.path.join(rtl, os.path.basename(n)), 'wb').write(body)
    subprocess.run(['verilator', '--cc', '--exe', '--build', '-O3', '-Wno-fatal', '-Wno-lint', '-Wno-style',
                    '--top-module', 'jt51', '-Irtl', 'rtl/jt51.v', src, '-o', 'jt51_ref', '-j', '8'],
                   cwd=b, check=True, capture_output=True)
    return exe


def build_opm():
    b = os.path.join(ROOT, 'build', 'opm_host')
    os.makedirs(b, exist_ok=True)
    exe = os.path.join(b, 'opm_render')
    srcs = [os.path.join(ROOT, 'sndtest', 'opm', n) for n in ('opm.c', 'opm_render.c')]
    deps = srcs + [os.path.join(ROOT, 'sndtest', 'opm', n) for n in ('opm.h', 'opm_tables.h')]
    if not os.path.exists(exe) or any(os.path.getmtime(s) > os.path.getmtime(exe) for s in deps):
        subprocess.run(['cc', '-O2', '-std=c99', '-Wall', '-Wextra', '-o', exe] + srcs, check=True)
    return exe


def jt51_render(ev_path, n, out):
    subprocess.run([build_jt51(), ev_path, out, str(n)], check=True,
                   env=dict(os.environ, JT51_KON_ALIGN=KON_ALIGN))
    return np.fromfile(out, dtype='<i2').astype(np.float64)


ROM_STEPS = None


def rom_steps(out_dir):
    """jt51's phase ROM (jt51_phinc_rom.v) as the model's 768-entry table: the non-gap
    notes, x32 to ymfm's 0.10 units. Read from srcref at run time, never committed."""
    import re
    src = subprocess.run(['git', '-C', JT51, 'show', 'HEAD:hdl/jt51_phinc_rom.v'],
                         capture_output=True, text=True, check=True).stdout
    rom = {int(a): int(v) for a, v in re.findall(r"10'd(\d+):\s*phinc = \{ 12'd(\d+) \}", src)}
    vals = [rom[i] * 32 for i in range(1024) if (i >> 6) & 3 != 3]
    path = os.path.join(out_dir, 'rom_steps.bin')
    open(path, 'wb').write(struct.pack('<768I', *vals))
    return path


# THE WRITE CONVENTION (2026-10-31). jt51_kon.v hands a key-on to each operator slot as the
# slot passes, so a write landing mid-pass keys the later slots a sample before the earlier
# ones, and whether an envelope takes its first decay step depends on the envelope counter's
# phase at that sample; on a real board both depend on when the Z80 wrote. The model keys all
# four operators in one sample, the outcome for a write landing just after C2's slot, so the
# harness paces every write to that point (JT51_KON_ALIGN 56 ticks after the strobe: one
# write per two samples, seen two samples after it is due) and the model is given the same
# spacing and latency (OPM_WRITE_SAMPLES 2, OPM_LATENCY 2). Measured: every slot then steps
# its envelope on the same key-on phases as jt51, alone and inside 28-write bursts.
KON_ALIGN = os.environ.get('JT51_KON_ALIGN', '56')
LATENCY = os.environ.get('OPM_LATENCY', '2')
WRITE_SAMPLES = os.environ.get('OPM_WRITE_SAMPLES', '2')


def opm_render(ev_path, n, rate, chans, out):
    mask = sum(1 << c for c in chans)
    extra = [ROM_STEPS or '-', LATENCY, WRITE_SAMPLES]
    subprocess.run([build_opm(), ev_path, out, str(n), str(rate), str(mask), str(CLOCK)] + extra, check=True)
    return np.fromfile(out, dtype='<i2').astype(np.float64)


def resample(x, rate):
    from fractions import Fraction
    from scipy.signal import resample_poly
    fr = Fraction(int(rate) * 64, CLOCK).limit_denominator(8000)
    return resample_poly(x, fr.numerator, fr.denominator)


def octave_db(x, rate, top):
    n = 4096
    fr = np.lib.stride_tricks.sliding_window_view(x, n)[::n // 2]
    p = (np.abs(np.fft.rfft(fr * np.hanning(n), axis=1)) ** 2).mean(axis=0)
    f = np.fft.rfftfreq(n, 1.0 / rate)
    tot = p.sum() + 1e-30
    return [10 * np.log10(p[(f >= c / 2 ** .5) & (f < c * 2 ** .5)].sum() / tot + 1e-12)
            for c in (63, 125, 250, 500, 1000, 2000, 4000, 8000) if c * 2 ** .5 <= top]


def active_db(x, rate):
    w = int(rate * 0.05)
    seg = x[:len(x) // w * w].reshape(-1, w)
    r = np.sqrt((seg ** 2).mean(axis=1))
    on = r > 32768 * 10 ** (-60 / 20)
    return 20 * np.log10(np.sqrt((r[on] ** 2).mean()) / 32768.0 + 1e-12) if on.any() else float('nan')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--vgz', required=True)
    ap.add_argument('--channels', default='6,7')
    ap.add_argument('--secs', type=float, default=22.0)
    ap.add_argument('--rates', default='0,44100,32000,22050,16000')
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'opm_ab'))
    ap.add_argument('--ymfm', action='store_true')
    ap.add_argument('--rom-steps', action='store_true',
                    help="use jt51's phase ROM instead of the formula table (a test, not the build)")
    ap.add_argument('--oracle-only', action='store_true', help='render jt51 and stop')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    global ROM_STEPS
    if a.rom_steps:
        ROM_STEPS = rom_steps(a.out)
    chans = [int(c) for c in a.channels.split(',')]
    ev = ym_events(a.vgz, chans, a.secs)
    evp = os.path.join(a.out, 'events.bin')
    write_events(ev, evp)
    n = int(a.secs * FM_RATE)
    ref = jt51_render(evp, n, os.path.join(a.out, 'jt51.s16'))
    print('YM2151 clock %d Hz (FM rate %.1f Hz)' % (CLOCK, FM_RATE))
    print('%d writes, %.0f s, channels %s; jt51 %d samples, peak %d, sounding level %.1f dBFS' % (
        len(ev), a.secs, chans, len(ref), np.abs(ref).max(), active_db(ref, FM_RATE)))
    if a.ymfm:
        from channel_ab import render, arcade_solo
        # vgmrender takes the whole stream; mask by soloing each channel through key-ons
        ys = [render(arcade_solo(a.vgz, c, a.secs), os.path.join(a.out, 'ymfm_%d' % c)) for c in chans]
        y = sum(ys) * 32768.0
        r44 = resample(ref, 44100)
        lr, ly = active_db(r44, 44100), active_db(y, 44100)
        br, by = octave_db(r44, 44100, 22050), octave_db(y, 44100, 22050)
        print('ymfm (vgmrender, fixed scale) vs jt51 at 44.1k: shape ymfm-jt51 per octave 63..8k: %s'
              % ' '.join('%+5.1f' % (u - v) for u, v in zip(by, br)))
        print('  (absolute level not comparable: vgmrender scale %.1f vs jt51 %.1f dBFS)' % (ly, lr))
    if a.oracle_only:
        return
    for rate in [int(r) for r in a.rates.split(',')]:
        if rate == 0:
            ours = opm_render(evp, n, 0, chans, os.path.join(a.out, 'opm_native.s16'))
            # jt51 trails by its pipeline (4 samples on Rise): align on the best lag first
            f0 = int(np.argmax(ref != 0))         # search where it sounds, not the silent lead-in
            lag = max(range(0, 17), key=lambda L: float(np.dot(ref[f0 + L:f0 + L + 600000],
                                                                 ours[f0:f0 + 600000][:len(ref[f0 + L:f0 + L + 600000])])))
            o = np.concatenate([np.zeros(lag), ours])[:len(ref)]
            d = o - ref
            same = np.mean(d == 0) * 100
            snr = 10 * np.log10((ref ** 2).sum() / ((d ** 2).sum() + 1e-9))
            # a structural miss (a note not retriggered, a wrong envelope) shows as one
            # window far off; phase drift from the step table does not move a window's level
            w = int(FM_RATE * 0.25)
            k = len(ref) // w
            lr_ = 20 * np.log10(np.sqrt((ref[:k * w].reshape(k, w) ** 2).mean(1)) + 1)
            lo_ = 20 * np.log10(np.sqrt((o[:k * w].reshape(k, w) ** 2).mean(1)) + 1)
            loud = lr_ > lr_.max() - 40
            wl = np.abs(lo_ - lr_)[loud]
            br, bo = octave_db(ref, FM_RATE, 12000), octave_db(o, FM_RATE, 12000)
            print('NATIVE  lag %d; %.1f%% identical, SNR %.1f dB; 0.25 s windows: worst %.2f dB, 95th %.2f dB; '
                  'octaves 63..8k %s' % (lag, same, snr, wl.max(), np.percentile(wl, 95),
                                         ' '.join('%+4.1f' % (u - v) for u, v in zip(bo, br))))
            continue
        m = int(a.secs * rate)
        ours = opm_render(evp, m, rate, chans, os.path.join(a.out, 'opm_%d.s16' % rate))
        r = resample(ref, rate)[:len(ours)]
        top = min(8000 * 2 ** .5, rate / 2)
        bo, br = octave_db(ours, rate, top), octave_db(r, rate, top)
        lo, lr = active_db(ours, rate), active_db(r, rate)
        worst = max(abs(u - v) for u, v in zip(bo, br))
        print('RATE %5d  level ours-jt51 %+5.2f dB  octaves 63..%dk %s  worst %.1f dB %s' % (
            rate, lo - lr, int(top / 1000) if top < 8000 else 8,
            ' '.join('%+5.1f' % (u - v) for u, v in zip(bo, br)), worst, 'OK' if worst <= 1 and abs(lo - lr) <= 1 else 'OVER'))


if __name__ == '__main__':
    main()
