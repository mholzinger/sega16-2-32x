#!/usr/bin/env python3
"""Fit per-channel brightness and level trims so the YM2612 transcode, heard through the Mega
Drive, matches the arcade YM2151 heard through the arcade board. KIT.

    python3 tools/fit_voices.py --vgz "srcref/audio/.../01 Rise....vgz" [--secs 22] [--out DIR]
      -> prints --ch-mod-gain and --ch-gain for tools/build_vgm_music.py

WHY (2026-10-30). tools/channel_ab.py found the transcode faithful on ymfm (pitch identical, tone
within ~1 dB to 4 kHz) but Mike hears different instruments and balance on ares. Two measured
responses explain the tone: the Mega Drive output (ares vs ymfm on the same stream) cuts 3.8 dB at
4 kHz and 8.4 dB at 8 kHz; the arcade board (the 16-Bit Audiophile FLAC vs ymfm's YM2151) only
1.0 and 2.3 dB. So per channel:

  BRIGHTNESS  sweep the modulator TL offset (lower = higher modulation index = more harmonics) and
              pick the one where ours + MD response best matches arcade + board response, octave
              bands 250 Hz-8 kHz (the lower bands carry the YM2612's key-on thumps)
  LEVEL       then iterate carrier TL trims until each channel's level relative to the mix equals
              the arcade's (channel_ab's measure)

Both are suggestions for the ear: a modulator change alters a voice's character, not only its
brightness, and Mike's pass is the gate.
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vgm2ym import load                                  # noqa: E402
from build_vgm_music import transcode_split               # noqa: E402
from channel_ab import arcade_solo, ours_solo, render, profile, active_rms, OCT   # noqa: E402

# octave bands 63..8000 Hz; measured 2026-10-30 on Rise (see the docstring)
MD_RESPONSE = [0.0, +0.7, +0.1, +0.4, -0.2, -1.2, -3.8, -8.4]      # ares - ymfm YM2612
BOARD_RESPONSE = [-5.5, -1.2, +0.8, +1.8, +0.4, +1.0, -1.0, -2.3]  # real board - ymfm YM2151
FIT_BANDS = range(2, 8)                                             # 250 Hz .. 8 kHz
MOD_LIMIT = -4                                                      # TL units = 3 dB more modulation


def stream_for(seg, secs, ch_mod_gain, ch_gain):
    s = dict(seg)
    s['ym'] = [e for e in seg['ym'] if e[0] < secs + 0.5]
    intro, _ = transcode_split(s, None, None, ch_gain=ch_gain, ch_mod_gain=ch_mod_gain)
    return intro


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--vgz', required=True)
    ap.add_argument('--secs', type=float, default=22.0)
    ap.add_argument('--channels', default='0,1,2,3,4,5')
    ap.add_argument('--out', default='/tmp/fit_voices')
    ap.add_argument('--json', help='merge this song\'s trims into a JSON file for build_vgm_music.py --trims')
    ap.add_argument('--cmd', help='the song\'s command byte, the JSON key (e.g. 0x94)')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    seg, _, _ = load(a.vgz)
    chans = [int(c) for c in a.channels.split(',')]

    arc = {}
    arc_mix = active_rms(render(arcade_solo(a.vgz, None, a.secs), os.path.join(a.out, 'arc_mix')))
    for c in list(chans):
        x = render(arcade_solo(a.vgz, c, a.secs), os.path.join(a.out, 'arc_%d' % c))
        sounding = np.mean(np.abs(x) > 10 ** (-60 / 20)) * a.secs
        lvl, prof = active_rms(x), profile(x)
        if lvl is None or prof is None or sounding < 2.0:
            print('  ch%d: sounds %.1f s in the window -- not fitted' % (c, sounding))
            chans.remove(c)
            continue
        arc[c] = {'bands': [b + r for b, r in zip(prof[0], BOARD_RESPONSE)], 'rel': lvl - arc_mix}

    mod = {}
    print('BRIGHTNESS (modulator TL offset, - = brighter): error = rms dB over 250 Hz-8 kHz')
    for c in chans:
        best = None
        # BOUNDED: a modulator offset past ~3 dB changes the instrument, not its brightness;
        # a fit that pins at the bound is reported, not applied
        for k in range(2, MOD_LIMIT - 1, -1):
            st = stream_for(seg, a.secs, {c: k}, {})
            y = render(ours_solo(st, c, a.secs), os.path.join(a.out, 'fit_%d' % c))
            got = profile(y)
            if got is None:
                continue
            ours = [b + r for b, r in zip(got[0], MD_RESPONSE)]
            err = np.sqrt(np.mean([(ours[i] - arc[c]['bands'][i]) ** 2 for i in FIT_BANDS]))
            if best is None or err < best[0]:
                best = (err, k, ours)
        err0 = None
        st = stream_for(seg, a.secs, {}, {})
        got = profile(render(ours_solo(st, c, a.secs), os.path.join(a.out, 'fit_%d' % c)))
        ours0 = [b + r for b, r in zip(got[0], MD_RESPONSE)]
        err0 = np.sqrt(np.mean([(ours0[i] - arc[c]['bands'][i]) ** 2 for i in FIT_BANDS]))
        pinned = best[1] == MOD_LIMIT
        mod[c] = 0 if pinned else best[1]
        print('  ch%d  offset %+3d%s  error %.1f dB (was %.1f at 0)   ours-arcade per band 250..8k: %s' % (
            c, best[1], ' PINNED, not applied' if pinned else '', best[0], err0,
            ' '.join('%+5.1f' % (best[2][i] - arc[c]['bands'][i]) for i in FIT_BANDS)))

    gain = {c: 0 for c in chans}
    print('LEVEL (carrier TL trim, + = quieter), relative to the mix as the arcade has it:')
    for it in range(3):
        st = stream_for(seg, a.secs, mod, gain)
        om = active_rms(render(ours_solo(st, None, a.secs), os.path.join(a.out, 'fit_mix')))
        d = {}
        for c in chans:
            y = render(ours_solo(st, c, a.secs), os.path.join(a.out, 'fit_%d' % c))
            ly = active_rms(y)
            d[c] = 0.0 if ly is None else (ly - om) - arc[c]['rel']
        print('  pass %d: ours - arcade (dB) %s' % (it, ' '.join('ch%d %+.1f' % (c, d[c]) for c in chans)))
        mean = np.mean(list(d.values()))
        for c in chans:
            gain[c] += int(round((d[c] - mean) / 0.75))
    # a channel the YM2612 headroom guard attenuates (build_vgm_music.LAST_HEADROOM) reads
    # quieter than the arcade on its loud notes BECAUSE those would clip; raising its gain
    # would make the guard take it straight back on those notes and leave the quiet ones
    # too loud (measured 2026-10-31: the refit asked for ch0 -13 on Gaum Hermer). Never louder.
    import build_vgm_music
    for c in chans:
        if c in build_vgm_music.LAST_HEADROOM and gain[c] < 0:
            print('  ch%d: gain %+d -> 0 (the headroom guard owns this channel\'s loud notes)' % (c, gain[c]))
            gain[c] = 0
    if a.json and a.cmd:
        import json
        allt = json.load(open(a.json)) if os.path.exists(a.json) else {}
        allt[a.cmd] = {'ch_mod_gain': {str(c): mod[c] for c in chans if mod[c]},
                       'ch_gain': {str(c): gain[c] for c in chans if gain[c]}}
        json.dump(allt, open(a.json, 'w'), indent=1, sort_keys=True)
    print('\n--ch-mod-gain "%s" --ch-gain "%s"' % (
        ','.join('%d:%d' % (c, mod[c]) for c in chans if mod[c]),
        ','.join('%d:%d' % (c, gain[c]) for c in chans if gain[c])))


if __name__ == '__main__':
    main()
