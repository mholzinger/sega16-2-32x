#!/usr/bin/env python3
"""VGZ soundtrack -> LZSS render_music.h with proper intro+loop. KIT.

VGM rips are the authoritative COMPLETE songs with an explicit loop
point. Per command this splits the song at the loop point, transcodes
the INTRO (chip init + pre-loop) and the LOOP (the repeating section)
separately (YM2151->YM2612 via opm2opn), LZSS-compresses each, and emits
render_music.h with {cmd, intro, loop}. The player plays the intro once
then loops the loop section — complete, and seamless, unlike a timed
capture (docs/sound/SOUND_DRIVER.md).

  build_vgm_music.py --map "0x94=01 Rise...vgz" ... --audio DIR --out H
The map is command=vgz-filename; see the assignment in the session log.
"""
import argparse
import math
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
    psg_map = {} if DROP_PSG else {6: 0, 7: 1}
    return fm_map, psg_map


# --drop-psg: channels 6/7 leave the Z80 stream entirely (no squares): the OPM LANE build,
# where the SH-2 synthesizes them from tools/opm_music_gen.py's table (hybrid 3, 2026-10-31)
DROP_PSG = False


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
LAST_HEADROOM = {}       # ch -> (max TL units applied, changes) for the last transcode_split
CARRIERS = {0: (3,), 1: (3,), 2: (3,), 3: (3,), 4: (2, 3),
            5: (1, 2, 3), 6: (1, 2, 3), 7: (0, 1, 2, 3)}


def transcode_split(seg, loop_t, tl_delta=None, gain=0, psg_extra=0,
                    mod_gain=0, ch_gain=None, ch_mod_gain=None, psg_frames=None, psg_fps=60.0):
    """-> (intro_bytes, loop_bytes). Intro = snapshot + events before
    loop_t; loop = events from loop_t on, no re-snapshot so the chip
    state carries over. Neither gets an end key-off (they flow on).
    tl_delta: per-(slot,ch) TL correction to subtract (tl_delta_vs_gag).
    gain: TL units ADDED to every CARRIER operator (volume only — the
    modulators set timbre and are left alone) to match the arcade's
    output level (tools/wav_ab.py). psg_extra: extra PSG attenuation
    steps (2dB) on the two square-wave channels. ch_gain / ch_mod_gain: per OPM
    channel {ch: TL units} on top of gain / mod_gain -- the measured trims of
    tools/channel_ab.py (carrier = level, modulator = brightness)."""
    fm_map, psg_map = best_maps(seg)
    if psg_frames:
        psg_map = {}                   # the squares come from the bake below, not from key events
    shadow = list(seg["shadow"])
    tc_i = Transcoder(fm_map, psg_map)
    tc_i.s.t = 0.0
    tc_i.psg_extra = psg_extra
    tc_i.snapshot(shadow)
    tc_l = Transcoder(fm_map, psg_map)
    tc_l.s.t = loop_t
    tc_l.psg_extra = psg_extra
    started_loop = False

    # YM2612 HEADROOM (2026-10-31, tools/opm_stage2.sh finding). The YM2612 clamps each
    # channel's operator sum to 14 bits (ares ym2612.cpp:133 sclamp<14>; ymfm rshift 5 clamp
    # 256): one full carrier is 8168, so two carriers near TL 0 clip, where the YM2151 sums
    # into 16 bits (jt51_acc.v lim16) and four fit. Census over the 8 songs: channel 0 runs
    # its carrier sum 3.0-4.7 dB over in 5 songs, on 24-66% of its notes. So every carrier of
    # a channel is attenuated by the same amount, just enough that the sum of their full-scale
    # amplitudes stays at or under one: a quieter note, but the arcade's waveform, not a
    # clipped one. Recomputed whenever a carrier TL or the algorithm changes, and applied at
    # emission only -- the shadow keeps the intended levels.
    amp = lambda tl: 10.0 ** (-(tl & 0x7F) * 0.75 / 20.0)
    head = [0] * 8

    def headroom(ch):
        alg = shadow[0x20 + ch] & 7
        total = sum(amp(shadow[0x60 + 8 * slot + ch]) for slot in CARRIERS[alg])
        # a silent carrier (TL 127) still adds 0.00002; one full carrier must cost nothing
        return 0 if total <= 1.001 else int(math.ceil(20.0 * math.log10(total) / 0.75))

    def emit_tl(tc, slot, ch):
        reg = 0x60 + 8 * slot + ch
        v = shadow[reg]
        if slot in CARRIERS[shadow[0x20 + ch] & 7] and head[ch]:
            v = (v & 0x80) | min(127, (v & 0x7F) + head[ch])
        tc.event(shadow, reg, v)

    def reheadroom(tc, ch, slots):
        new = headroom(ch)
        if new != head[ch]:
            LAST_HEADROOM[ch] = (max(LAST_HEADROOM.get(ch, (0, 0))[0], new),
                                 LAST_HEADROOM.get(ch, (0, 0))[1] + 1)
            head[ch] = new
            for slot in range(4):
                if slot not in slots:
                    emit_tl(tc, slot, ch)

    LAST_HEADROOM.clear()

    # THE BAKED PSG STREAM (tools/psg_bake.py, 2026-11-01): channels 6/7 as per-frame (period,
    # attenuation) pairs on the song's own timeline, written as PSG bytes between the FM writes.
    # Only changes are written; frame 0 writes everything (the snapshot silenced the PSG).
    psg_k = 0
    psg_state = [None, None, None, None]

    def psg_flush(upto):
        nonlocal psg_k
        while psg_frames and psg_k < len(psg_frames) and psg_k / psg_fps <= upto:
            fr = psg_frames[psg_k]
            tk = psg_k / psg_fps
            tc = tc_i if (loop_t is None or tk < loop_t) else tc_l
            tc.s.wait_to(tk)
            for tone in (0, 1):
                per, att = int(fr[2 * tone]), int(fr[2 * tone + 1])
                if per != psg_state[2 * tone]:
                    tc.s.psg(0x80 | (tone << 5) | (per & 0x0F))
                    tc.s.psg((per >> 4) & 0x3F)
                    psg_state[2 * tone] = per
                if att != psg_state[2 * tone + 1]:
                    tc.s.psg(0x90 | (tone << 5) | (att & 0x0F))
                    psg_state[2 * tone + 1] = att
            psg_k += 1

    for t, reg, val in seg["ym"]:
        psg_flush(t)
        if 0x60 <= reg < 0x80:
            slot, ch = (reg - 0x60) >> 3, reg & 7
            tl = val & 0x7F
            if tl_delta and (slot, ch) in tl_delta:
                tl -= tl_delta[(slot, ch)]
            if slot in CARRIERS[shadow[0x20 + ch] & 7]:
                tl += gain + (ch_gain or {}).get(ch, 0)          # volume
            else:
                tl += mod_gain + (ch_mod_gain or {}).get(ch, 0)  # brightness (modulation index)
            val = (val & 0x80) | max(0, min(127, tl))
        tc = tc_i if (loop_t is None or t < loop_t) else tc_l
        tc.s.wait_to(t)
        if 0x60 <= reg < 0x80:
            slot, ch = (reg - 0x60) >> 3, reg & 7
            shadow[reg] = val
            reheadroom(tc, ch, (slot,))
            emit_tl(tc, slot, ch)
            continue
        # the shadow is what emit_b4 / emit_pitch / psg_key read, so it must hold THIS
        # write before the event is emitted: with the old order a channel's RL enable
        # landed one write late (the sequence's first click played muted) and a KF change
        # emitted the previous KF (found 2026-10-31 with tools/opm_stage2.sh)
        if reg == 0x19:
            shadow[0x19 + (val >> 7)] = val
        else:
            shadow[reg] = val
        tc.event(shadow, reg, val)
        if 0x20 <= reg < 0x28:                        # the algorithm moved the carriers
            reheadroom(tc, reg & 7, ())
    psg_flush(1e9)                                    # the frames after the last FM write
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
    ap.add_argument("--ch-gain", default="",
                    help="per-channel carrier TL, 'ch:units,...' (+ = quieter): "
                         "the level trims tools/channel_ab.py measures")
    ap.add_argument("--ch-mod-gain", default="",
                    help="per-channel modulator TL, 'ch:units,...' (- = brighter): "
                         "offsets the Mega Drive output low-pass per voice")
    ap.add_argument("--trims", help="JSON {\"0x94\": {\"ch_gain\": {\"1\": 2}, \"ch_mod_gain\": "
                    "{...}}}: per-song per-channel trims (tools/fit_voices.py --json), "
                    "applied on top of --ch-gain / --ch-mod-gain")
    ap.add_argument("--drop-psg", action="store_true",
                    help="leave channels 6/7 out of the Z80 stream (the SH-2 synth lane plays them)")
    ap.add_argument("--psg-bake", default="",
                    help="DIR of tools/psg_bake.py's <cmd>_psg.json: channels 6/7 as the baked per-frame "
                         "PSG stream (period + 4-bit level from the exact engine) instead of key events")
    ap.add_argument("--mod-gain", type=int, default=0,
                    help="TL units added to every MODULATOR (brightness "
                         "trim: the arcade output is darker than a raw "
                         "YM2612 — its board filter rolls off the top)")
    args = ap.parse_args()
    global DROP_PSG
    DROP_PSG = args.drop_psg
    parse_ch = lambda t: {int(k): int(v) for k, v in (p.split(':') for p in t.split(',') if p)}
    ch_gain, ch_mod_gain = parse_ch(args.ch_gain), parse_ch(args.ch_mod_gain)
    import json
    trims = {int(k, 16): v for k, v in json.load(open(args.trims)).items()} if args.trims else {}

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
        t = trims.get(cmd, {})
        cg = dict(ch_gain); cg.update({int(k): v for k, v in t.get('ch_gain', {}).items()})
        cm = dict(ch_mod_gain); cm.update({int(k): v for k, v in t.get('ch_mod_gain', {}).items()})
        psg_frames, psg_fps = None, 60.0
        if args.psg_bake:
            bake = json.load(open(os.path.join(args.psg_bake, "0x%02x_psg.json" % cmd)))
            psg_frames, psg_fps = bake["frames"], float(bake["header"]["fps"])
        intro, loop = transcode_split(seg, None, delta,
                                      gain=args.gain, psg_extra=args.psg_att,
                                      mod_gain=args.mod_gain, ch_gain=cg,
                                      ch_mod_gain=cm, psg_frames=psg_frames, psg_fps=psg_fps)
        ci, cl = compress(intro), compress(loop)
        assert decompress(ci) == intro and decompress(cl) == loop
        tracks.append((cmd, ci, cl, len(intro), len(loop), total, loop_t))
        print(f"  0x{cmd:02X} {fname[:34]:34} {total:5.1f}s L{loop_t or 0:5.1f}s "
              f"intro {len(intro)}->{len(ci)}  loop {len(loop)}->{len(cl)}",
              file=sys.stderr)
        if LAST_HEADROOM:
            print("       YM2612 headroom (max TL units, changes): " + " ".join(
                f"ch{c}:{h[0]}/{h[1]}" for c, h in sorted(LAST_HEADROOM.items())), file=sys.stderr)

    with open(args.out, "w") as f:
        f.write("/* AUTO-GENERATED by tools/build_vgm_music.py — the\n"
                " * authoritative VGM soundtrack rips, split intro+loop at\n"
                " * the VGM loop point, transcoded + LZSS-compressed. The\n"
                " * player plays the intro once then loops the loop section\n"
                " * (docs/sound/SOUND_DRIVER.md). Do not edit. */\n"
                "#include <stdint.h>\n\n")
        for cmd, ci, cl, il, ll, total, lt in tracks:
            f.write(f"static const uint8_t rin_{cmd:02X}[{len(ci)}] = {{\n")
            for o in range(0, len(ci), 16):
                f.write("\t" + ",".join("0x%02X" % b for b in ci[o:o + 16]) + ",\n")
            f.write("};\n")
            if cl == ci:
                # a whole-song loop plays the same bytes again: ONE copy, the table names rin_
                # for both (tools/sound_blob.py packs a same-named loop once). The MD side has
                # the 32X's 512 KB 68K ROM window and no banking (sndtest/md/md_main.c:65); two
                # copies of eight songs with the baked PSG writes made a 649 KB MD binary and a
                # black cart (sndtest-psg1, 2026-11-01)
                pass
            else:
                f.write(f"static const uint8_t rlp_{cmd:02X}[{len(cl)}] = {{\n")
                for o in range(0, len(cl), 16):
                    f.write("\t" + ",".join("0x%02X" % b for b in cl[o:o + 16]) + ",\n")
                f.write("};\n")
        f.write("\nstruct rtrk { uint8_t cmd; const uint8_t *intro;"
                " uint32_t intro_len; const uint8_t *loop; uint32_t loop_len; };\n")
        f.write(f"static const struct rtrk render_music[{len(tracks)}] = {{\n")
        for cmd, ci, cl, il, ll, total, lt in tracks:
            # loop_len 0 = a ONE-SHOT: the rip has no loop point (round clear, game over, the
            # ending); the game's player ends it on its 0xFF and resumes the song it interrupted
            f.write(f"\t{{ 0x{cmd:02X}, rin_{cmd:02X}, {len(ci)}, "
                    f"{'rin' if cl == ci else 'rlp'}_{cmd:02X}, {len(cl) if lt is not None else 0} }},\n")
        f.write("};\n")
        f.write("#define RENDER_MUSIC_COUNT %d\n" % len(tracks))
    tot = sum(len(ci) + (0 if cl == ci else len(cl)) for _, ci, cl, *_ in tracks)
    print(f"render_music.h: {len(tracks)} tracks, {tot} bytes ({tot/1024:.0f}KB) in the rom"
          + (" -- OVER the 512 KB MD window with the player; the cart will not boot" if tot > 480 * 1024 else ""),
          file=sys.stderr)


if __name__ == "__main__":
    main()
