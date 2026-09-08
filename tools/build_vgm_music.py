#!/usr/bin/env python3
"""VGZ soundtrack -> LZSS render_music.h with proper intro+loop. KIT.

VGM rips are the authoritative COMPLETE songs with an explicit loop
point. Per command this splits the song at the loop point, transcodes
the INTRO (chip init + pre-loop) and the LOOP (the repeating section)
separately (YM2151->YM2612 via opm2opn), LZSS-compresses each, and emits
render_music.h with {cmd, intro, loop}. The player plays the intro once
then loops the loop section — complete, and seamless, unlike a timed
capture (SOUND_DRIVER.md).

  build_vgm_music.py --map "0x94=01 Rise...vgz" ... --audio DIR --out H
The map is command=vgz-filename; see the assignment in the session log.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vgm2ym import load                                       # noqa: E402
from opm2opn import Transcoder                                # noqa: E402
from lzss import compress, decompress                         # noqa: E402

def best_maps(seg):
    """The YM2151 has 8 FM channels; the YM2612 has 6. Two must fall back
    to PSG (square). Keep the SIX most-used channels on FM and sacrifice
    only the two least-used to PSG — so a prominent voice (e.g. ch6, 502
    key-ons in Rise From Your Grave) stays FM instead of becoming a
    square. Static opm2opn always PSG'd ch6/7, which cost real
    instruments (Mike's tonal-loss catch, 2026-09-01)."""
    from collections import Counter
    ko = Counter()
    for _, r, v in seg["ym"]:
        if r == 0x08 and (v >> 3) & 0xF:
            ko[v & 7] += 1
    # REVERTED 2026-09-02: keeping the 6 busiest on FM (moving ch4 to PSG,
    # ch6 to FM) sounded WORSE by ear than the fixed opm2opn mapping —
    # ch6 is a narrow low rhythm part that survives as a square, ch4 is a
    # melodic voice that does not. Use the SAME fixed mapping the earlier
    # (pre-VGZ) tracks used: ch0-5 -> FM, ch6-7 -> PSG.
    fm_map = {c: c for c in range(6)}
    psg_map = {6: 0, 7: 1}
    return fm_map, psg_map


def op_tl_avg(evs):
    """Average TL per (slot, channel) over all 4 operators, regs
    0x60-0x7F (reg = 0x60 + 8*slot + ch). slot3 = carrier = volume;
    slots 0-2 = modulators = timbre/brightness."""
    from collections import defaultdict
    acc = defaultdict(list)
    for _, r, v in evs:
        if 0x60 <= r < 0x80:
            acc[((r - 0x60) >> 3, r & 7)].append(v & 0x7F)
    return {k: sum(a) // len(a) for k, a in acc.items()}


def tl_delta_vs_gag(seg, gag_evs):
    """The VGM rips have channels DUCKED vs the arcade driver's own
    levels (Rise: ch4 carrier 72 vs 18 — ~40dB, the 'missing overarching
    theme'; AND on ch4/ch7 the MODULATORS too (+23..+57), which dulls the
    timbre — the 'balance off', Mike 2026-09-02). The rip carries the
    game's in-context BGM mix; the sound test wants the driver's full
    patch. Compute per-(slot,ch) (VGZ - gag) TL delta over ALL 4
    operators and subtract it, restoring both level and timbre."""
    v = op_tl_avg(seg["ym"])
    g = op_tl_avg(gag_evs)
    return {k: v[k] - g[k] for k in v if k in g}


# YM2151 carrier slots per algorithm, in REGISTER slot order M1=0, M2=1,
# C1=2, C2=3 (reg 0x60+8*slot+ch). OPN algorithm op numbering is
# S1..S4 = M1, C1, M2, C2, so alg 4 (out op2+op4) = slots (2,3), alg 5/6
# (out op2,3,4) = (1,2,3), alg 7 = all; 0-3 = C2 only.
CARRIERS = {0: (3,), 1: (3,), 2: (3,), 3: (3,), 4: (2, 3),
            5: (1, 2, 3), 6: (1, 2, 3), 7: (0, 1, 2, 3)}


def transcode_split(seg, loop_t, tl_delta=None, gain=0, psg_extra=0,
                    mod_gain=0):
    """-> (intro_bytes, loop_bytes). Intro = snapshot + events before
    loop_t; loop = events from loop_t on, no re-snapshot so the chip
    state carries over. Neither gets an end key-off (they flow on).
    tl_delta: per-(slot,ch) TL correction to subtract (tl_delta_vs_gag).
    gain: TL units ADDED to every CARRIER operator (volume only — the
    modulators set timbre and are left alone) to match the arcade's
    output level (tools/wav_ab.py). psg_extra: extra PSG attenuation
    steps (2dB) on the two square-wave channels."""
    fm_map, psg_map = best_maps(seg)
    shadow = list(seg["shadow"])
    tc_i = Transcoder(fm_map, psg_map)
    tc_i.s.t = 0.0
    tc_i.psg_extra = psg_extra
    tc_i.snapshot(shadow)
    tc_l = Transcoder(fm_map, psg_map)
    tc_l.s.t = loop_t
    tc_l.psg_extra = psg_extra
    started_loop = False
    for t, reg, val in seg["ym"]:
        if 0x60 <= reg < 0x80:
            slot, ch = (reg - 0x60) >> 3, reg & 7
            tl = val & 0x7F
            if tl_delta and (slot, ch) in tl_delta:
                tl -= tl_delta[(slot, ch)]
            if slot in CARRIERS[shadow[0x20 + ch] & 7]:
                tl += gain                 # volume
            else:
                tl += mod_gain             # brightness (modulation index)
            val = (val & 0x80) | max(0, min(127, tl))
        tc = tc_i if (loop_t is None or t < loop_t) else tc_l
        tc.s.wait_to(t)
        tc.event(shadow, reg, val)
        if reg == 0x19:
            shadow[0x19 + (val >> 7)] = val
        else:
            shadow[reg] = val
    intro = bytes(tc_i.s.out)
    loop = bytes(tc_l.s.out) if loop_t is not None else b""
    if not loop:                       # non-looping: whole thing is intro,
        loop = intro                   # loop it whole as a fallback
    return intro, loop


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--map", nargs="+", required=True,
                    help="cmd=vgz-basename entries")
    ap.add_argument("--audio", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--gag-log", help="music_sweep.lua log of the arcade "
                    "driver at full level; per-song channel levels are "
                    "restored to it (tl_delta_vs_gag)")
    ap.add_argument("--gain", type=int, default=0,
                    help="TL units added to every carrier (volume) to "
                         "match the arcade level per tools/wav_ab.py")
    ap.add_argument("--psg-att", type=int, default=0,
                    help="extra PSG attenuation steps (2dB) on the two "
                         "square-wave channels")
    ap.add_argument("--mod-gain", type=int, default=0,
                    help="TL units added to every MODULATOR (brightness "
                         "trim: the arcade output is darker than a raw "
                         "YM2612 — its board filter rolls off the top)")
    args = ap.parse_args()

    gag = {}
    if args.gag_log:
        cur = None; addr = 0
        for ln in open(args.gag_log):
            p = ln.split()
            if len(p) != 3:
                continue
            t, k, v = p
            if k == "IJ":
                cur = int(v, 16); gag.setdefault(cur, [])
            elif k == "IX":
                cur = None
            elif cur is not None:
                v = int(v, 16)
                if k == "Y0":
                    addr = v
                elif k == "Y1":
                    gag[cur].append((float(t), addr, v))

    tracks = []
    for m in args.map:
        cmd_s, fname = m.split("=", 1)
        cmd = int(cmd_s, 16)
        seg, loop_t, total = load(os.path.join(args.audio, fname))
        delta = tl_delta_vs_gag(seg, gag[cmd]) if cmd in gag else None
        if delta:
            big = sorted((k, d) for k, d in delta.items() if abs(d) >= 10)
            print(f"  0x{cmd:02X} TL restore (|d|>=10): " + " ".join(
                f"s{s}c{c}{d:+d}" for (s, c), d in big), file=sys.stderr)
        # whole-stream (loop the whole song, re-snapshot each loop — the
        # gag behaviour Mike confirmed); the intro/loop split is kept
        # available via transcode_split(seg, loop_t) for later.
        intro, loop = transcode_split(seg, None, delta,
                                      gain=args.gain, psg_extra=args.psg_att,
                                      mod_gain=args.mod_gain)
        ci, cl = compress(intro), compress(loop)
        assert decompress(ci) == intro and decompress(cl) == loop
        tracks.append((cmd, ci, cl, len(intro), len(loop), total, loop_t))
        print(f"  0x{cmd:02X} {fname[:34]:34} {total:5.1f}s L{loop_t or 0:5.1f}s "
              f"intro {len(intro)}->{len(ci)}  loop {len(loop)}->{len(cl)}",
              file=sys.stderr)

    with open(args.out, "w") as f:
        f.write("/* AUTO-GENERATED by tools/build_vgm_music.py — the\n"
                " * authoritative VGM soundtrack rips, split intro+loop at\n"
                " * the VGM loop point, transcoded + LZSS-compressed. The\n"
                " * player plays the intro once then loops the loop section\n"
                " * (SOUND_DRIVER.md). Do not edit. */\n"
                "#include <stdint.h>\n\n")
        for cmd, ci, cl, il, ll, total, lt in tracks:
            f.write(f"static const uint8_t rin_{cmd:02X}[{len(ci)}] = {{\n")
            for o in range(0, len(ci), 16):
                f.write("\t" + ",".join("0x%02X" % b for b in ci[o:o + 16]) + ",\n")
            f.write("};\n")
            f.write(f"static const uint8_t rlp_{cmd:02X}[{len(cl)}] = {{\n")
            for o in range(0, len(cl), 16):
                f.write("\t" + ",".join("0x%02X" % b for b in cl[o:o + 16]) + ",\n")
            f.write("};\n")
        f.write("\nstruct rtrk { uint8_t cmd; const uint8_t *intro;"
                " uint32_t intro_len; const uint8_t *loop; uint32_t loop_len; };\n")
        f.write(f"static const struct rtrk render_music[{len(tracks)}] = {{\n")
        for cmd, ci, cl, il, ll, total, lt in tracks:
            f.write(f"\t{{ 0x{cmd:02X}, rin_{cmd:02X}, {len(ci)}, "
                    f"rlp_{cmd:02X}, {len(cl)} }},\n")
        f.write("};\n")
        f.write("#define RENDER_MUSIC_COUNT %d\n" % len(tracks))
    tot = sum(len(ci) + len(cl) for _, ci, cl, *_ in tracks)
    print(f"render_music.h: {len(tracks)} tracks, {tot} bytes ({tot/1024:.0f}KB)",
          file=sys.stderr)


if __name__ == "__main__":
    main()
