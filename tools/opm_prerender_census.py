#!/usr/bin/env python3
"""Size the PRERENDER route for the two overflow YM2151 channels. KIT.

The hybrid's alternative to synthesizing channels 6/7 on the SH-2 in-game: render every
note those channels play OFFLINE through the exact model (sndtest/opm, bit-identical to
jt51) and ship the notes as PCM on the PWM lane, like speech. Zero SH-2 synthesis cost;
the limit is cart bytes. This script counts the notes, dedups them, renders each unique
one to find its true length (hold + release tail to silence), and prices the set at a few
rates and encodings.

A NOTE is a key-on on the channel: identity = the channel's 28 registers at the key-on,
the hold length in frames (to the key-off, or to a retrigger), and every register change
to the channel during the hold (a TL slide or a KF bend makes a different note). Two
dedup counts are printed: with the mid-note changes (what a sample player must store) and
without (the lower bound, if the player could apply slides itself).

  tools/opm_prerender_census.py [--channels 6,7] [--rates 31250,15625,7812] [--json OUT]
"""
import argparse
import json
import os
import struct
import subprocess
import sys
from collections import defaultdict

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from vgm2ym import load                      # noqa: E402
import opm_ab                                # noqa: E402

AUDIO = os.path.join(ROOT, 'srcref', 'audio', 'Altered_Beast_(Sega_System_16B)')
SONGS = [(0x90, '03 Gaum Hermer (Boss Theme).vgz'),
         (0x91, '02 Altered Beast (Power Up).vgz'),
         (0x92, '08 Game Over ~ Name Entry.vgz'),
         (0x93, '05 Closed In Upon Me (Round 2, 5).vgz'),
         (0x94, '01 Rise From Your Grave (Round 1, 4).vgz'),
         (0x95, "04 Crystal's Theme (Round Clear).vgz"),
         (0x96, "06 I'll Be Back (Round 3).vgz"),
         (0x97, "07 Who's Afraid of the Big Bad Wolf (Ending).vgz")]
CLOCK = 4000000
NATIVE = CLOCK / 64.0
FPS = 60.0
TAIL_CAP = 8.0          # seconds of release tail rendered at most
LEAD = 256              # samples between the patch writes and the key-on


def ch_regs(c):
    regs = [0x20 + c, 0x28 + c, 0x30 + c, 0x38 + c]
    for s in range(4):
        regs += [b + 8 * s + c for b in (0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0)]
    return regs


def census(seg, c, total_s):
    """-> list of notes: (snapshot tuple, hold_frames, mid tuple, t_on, t_next_on)."""
    regs = ch_regs(c)
    shadow = [0] * 256
    notes = []
    cur = None                      # (snapshot, t_on, mid list)
    for t, r, v in seg['ym']:
        if r == 0x08 and (v & 7) == c:
            if (v >> 3) & 0xF:
                if cur is not None:         # retrigger: the hold ends here
                    notes.append(close(cur, t))
                cur = (tuple(shadow[x] for x in regs) + ((v >> 3) & 0xF,), t, [])
            elif cur is not None:
                notes.append(close(cur, t))
                cur = None
            continue
        if r in regs:
            if cur is not None and shadow[r] != v:
                cur[2].append((int(round((t - cur[1]) * FPS)), r, v))
            shadow[r] = v
    if cur is not None:
        notes.append(close(cur, total_s))
    for i, n in enumerate(notes):
        nxt = notes[i + 1][3] if i + 1 < len(notes) else total_s
        notes[i] = n + (nxt,)
    return notes


def close(cur, t_off):
    snap, t_on, mid = cur
    hold = max(1, int(round((t_off - t_on) * FPS)))
    return (snap, hold, tuple(mid), t_on)


def render_len(exe, snap, hold, mid, c, work):
    """Render one note at the native rate; -> seconds until the output is silent."""
    regs = ch_regs(c)
    ev = []
    for r, v in zip(regs, snap[:-1]):
        ev.append((0, r, v))
    kon = LEAD
    ev.append((kon, 0x08, (snap[-1] << 3) | c))
    for fr, r, v in mid:
        ev.append((kon + int(fr * NATIVE / FPS), r, v))
    t_off = kon + int(hold * NATIVE / FPS)
    ev.append((t_off, 0x08, c))
    n = t_off + int(TAIL_CAP * NATIVE)
    evp = os.path.join(work, 'note.bin')
    with open(evp, 'wb') as f:
        for t, r, v in ev:
            f.write(struct.pack('<IBB', t, r, v))
    out = os.path.join(work, 'note.s16')
    subprocess.run([exe, evp, out, str(n), '0', str(1 << c), str(CLOCK), '-', '2', '2'],
                   check=True, stdout=subprocess.DEVNULL)
    x = np.fromfile(out, dtype='<i2')
    nz = np.flatnonzero(np.abs(x) > 1)
    last = int(nz[-1]) if len(nz) else kon
    return (last - kon) / NATIVE, (t_off - kon) / NATIVE


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--channels', default='6,7')
    ap.add_argument('--rates', default='31250,15625,7812')
    ap.add_argument('--json')
    a = ap.parse_args()
    chans = [int(x) for x in a.channels.split(',')]
    rates = [int(x) for x in a.rates.split(',')]
    exe = opm_ab.build_opm()
    work = os.path.join(ROOT, 'build', 'prerender')
    os.makedirs(work, exist_ok=True)
    report = {}
    grand = defaultdict(float)
    uniq_all = {}
    cut_all = {}
    print(f"{'song':5} {'ch':2} {'notes':>5} {'uniq':>5} {'uniq-nomid':>10} {'mid%':>5} "
          f"{'sounded s':>9} {'uniq s':>7} {'maxnote s':>9}")
    for cmd, fname in SONGS:
        seg, loop_t, total_s = load(os.path.join(AUDIO, fname))
        for c in chans:
            notes = census(seg, c, total_s)
            keys = {}
            for snap, hold, mid, t_on, t_next in notes:
                keys.setdefault((snap, hold, mid), []).append((t_on, t_next))
            nomid = len({(snap, hold) for snap, hold, mid, _, _ in notes})
            with_mid = sum(1 for n in notes if n[2])
            sounded = 0.0
            uniq_s = 0.0
            maxnote = 0.0
            for (snap, hold, mid), occ in keys.items():
                gk = (c, snap, hold, mid)
                if gk not in uniq_all:
                    uniq_all[gk] = render_len(exe, snap, hold, mid, c, work)
                length, _ = uniq_all[gk]
                # stored length: the tail is only needed up to the longest gap before this
                # note's next key-on on the channel (the player cuts on retrigger, as a
                # one-voice-per-channel replay must)
                need = min(length, max(t_next - t_on for t_on, t_next in occ))
                cut_all[gk] = max(cut_all.get(gk, 0.0), need)
                uniq_s += length
                maxnote = max(maxnote, length)
                for t_on, t_next in occ:
                    sounded += min(length, t_next - t_on)
            song_cut = sum(cut_all[(c, snap, hold, mid)] for (snap, hold, mid) in keys)
            row = dict(notes=len(notes), uniq=len(keys), uniq_nomid=nomid,
                       mid_pct=100.0 * with_mid / max(1, len(notes)),
                       sounded_s=sounded, uniq_s=uniq_s, cut_s=song_cut, maxnote_s=maxnote,
                       song_s=total_s)
            report[f'0x{cmd:02X}/ch{c}'] = row
            grand['notes'] += len(notes); grand['uniq'] += len(keys); grand['nomid'] += nomid
            grand['sounded'] += sounded; grand['uniq_s'] += uniq_s
            print(f"0x{cmd:02X}  {c:2} {len(notes):5} {len(keys):5} {nomid:10} {row['mid_pct']:5.1f} "
                  f"{sounded:9.1f} {uniq_s:7.1f} {maxnote:9.2f}   cut {song_cut:6.1f} s = "
                  f"{song_cut * 16000 / 2 / 1024:6.0f} KB @16k IMA")
    shared = len(uniq_all)
    print(f"\ntotal notes {int(grand['notes'])}, unique per song {int(grand['uniq'])}, "
          f"unique across songs {shared}, sounded {grand['sounded']:.0f} s, unique {grand['uniq_s']:.0f} s "
          f"(cross-song unique {sum(l for l, _ in uniq_all.values()):.0f} s)")
    cross_s = sum(l for l, _ in uniq_all.values())
    cut_s = sum(cut_all.values())
    print(f"stored with tails cut at the longest gap to the next note: {cut_s:.0f} s")
    print(f"\n{'rate':>6} {'8-bit PCM':>10} {'IMA 4-bit':>10}   full tails | {'8-bit PCM':>10} {'IMA 4-bit':>10}   tails cut")
    sizes = {}
    for r in rates:
        pcm, pcmc = cross_s * r, cut_s * r
        sizes[r] = dict(pcm8=int(pcm), ima4=int(pcm / 2), cut_pcm8=int(pcmc), cut_ima4=int(pcmc / 2))
        print(f"{r:6} {pcm / 1048576:8.2f} MB {pcm / 2 / 1048576:8.2f} MB              "
              f"{pcmc / 1048576:8.2f} MB {pcmc / 2 / 1048576:8.2f} MB")
    report['_total'] = dict(unique_s=cross_s, cut_s=cut_s, sounded_s=grand['sounded'], sizes=sizes)
    if a.json:
        json.dump(report, open(a.json, 'w'), indent=1)


if __name__ == '__main__':
    main()
