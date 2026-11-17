#!/usr/bin/env python3
"""Replay a recorded tape headless and prove the port saw it. KIT (2026-11-02).

    python3 tools/tape_check.py build/port/rom/psg3.32x build/tapes/psg3_neff.csv [--out DIR]
        [--frames N] [--audio-wav out.wav] [--ares BIN]

The tape is tools/ares_record.sh's (or any headless --input csv) keyed to the port's frame
counter; the replay passes --input-gameframe sdram:<_port_gf_count> and watches the latched pad
byte 0xFFE028 every frame. PROOF: for every pad press on the tape, that button's bit is set in
0xFFE028 within 16 frames of the first host frame whose key reaches the edge (the window
parity_compare.derive_pad_bits uses); a press the game never read (attract, a load hold, a
hang) is reported, not excused. Coin (y) and start have no pad bit and are reported by count.
Exit 0 when every press landed, 1 otherwise. The same run can carry --audio-wav for listening.
"""
import argparse
import csv
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from parity_play import sym_sdram, sdram_off          # noqa: E402
from parity_compare import PAD_BITS_STATED            # noqa: E402

ARES = os.path.expanduser('~/src/bin/ares-headless-ssf')
WINDOW = 16


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom')
    ap.add_argument('tape')
    ap.add_argument('--out', default=None)
    ap.add_argument('--frames', type=int, default=None, help='default: last tape frame + 600')
    ap.add_argument('--ares', default=ARES)
    ap.add_argument('--audio-wav', default=None)
    ap.add_argument('--pad1', default='fighting')
    a = ap.parse_args()
    gf = sym_sdram(a.rom, '_port_gf_count')
    if gf is None:
        raise SystemExit('tape_check: %s has no _port_gf_count (.elf beside the rom?)' % a.rom)
    events = []
    for l in open(a.tape):
        l = l.strip()
        if not l or l.startswith('#'):
            continue
        f = l.split(',')
        events.append((int(f[0]), f[1].lower(), int(f[2]), int(f[3]) if len(f) > 3 else 1))
    if not events:
        raise SystemExit('tape_check: %s has no events' % a.tape)
    out = a.out or os.path.join(ROOT, 'build', 'tapes', os.path.splitext(os.path.basename(a.tape))[0] + '_check')
    os.makedirs(out, exist_ok=True)
    # a keyed tape's frames are game frames; the host runs at least that many
    frames = a.frames or (max(e[0] for e in events) + 600)
    log = os.path.join(out, 'gameframe.csv')
    cmd = [a.ares, a.rom, '--frames', str(frames), '--input', a.tape, '--input-gameframe', 'sdram:0x%X' % gf,
           '--pad1', a.pad1, '--gameframe-log', log, '--gameframe-watch', 'sdram:0x%X:1' % sdram_off(0xFFE028)]
    if a.audio_wav:
        cmd += ['--audio-wav', a.audio_wav]
    print('tape_check: ' + ' '.join(cmd))
    r = subprocess.run(cmd, capture_output=True, text=True)
    open(os.path.join(out, 'ares.log'), 'w').write(r.stdout + r.stderr)
    if r.returncode != 0:
        raise SystemExit('tape_check: ares exit %d, see %s/ares.log' % (r.returncode, out))
    rows = []
    for row in csv.reader(open(log)):
        if row and row[0].isdigit() and len(row) >= 3 and row[2]:
            rows.append((int(row[0]), int(row[1]), int(row[2], 16)))
    if not rows:
        raise SystemExit('tape_check: empty gameframe log')
    # host frame at which each game-frame key is first reached (the replayer's min(gf, frame))
    first_at = {}
    for host, g, _ in rows:
        key = min(g if g != 0xFFFF else 0, host)
        for k in range(key, -1, -1):
            if k in first_at:
                break
            first_at[k] = host
    pad = {host: p for host, _, p in rows}
    landed = missed = other = beyond = 0
    misses = []
    last_key = max(first_at)          # the highest game frame the replay reached
    report = ['tape %s on %s: %d events, replay %d frames, counter sdram:0x%X' % (a.tape, a.rom, len(events), frames, gf)]
    for f, b, v, p in events:
        if v != 1 or b not in PAD_BITS_STATED:
            other += (b in ('y', 'start') and v == 1)
            continue
        if f > last_key:
            beyond += 1                # past --frames: not replayed, not a miss
            continue
        h0 = first_at.get(f)
        if h0 is None:
            missed += 1
            misses.append('%d %s: the replay never reached game frame %d' % (f, b, f))
            continue
        bit = PAD_BITS_STATED[b]
        hit = next((h for h in range(h0, h0 + WINDOW) if pad.get(h, 0) >> bit & 1), None)
        if hit is None:
            missed += 1
            misses.append('%d %s: bit %d never set in 0xFFE028 during host frames %d..%d' % (f, b, bit, h0, h0 + WINDOW - 1))
        else:
            landed += 1
    report.append('presses landed %d, missed %d, beyond the replayed %d game frames %d, coin/start presses %d (no pad bit)'
                  % (landed, missed, last_key, beyond, other))
    report += ['  MISS ' + m for m in misses[:20]]
    if len(misses) > 20:
        report.append('  ... %d more' % (len(misses) - 20))
    text = '\n'.join(report)
    print(text)
    open(os.path.join(out, 'REPORT.txt'), 'w').write(text + '\n')
    sys.exit(0 if missed == 0 else 1)


if __name__ == '__main__':
    main()
