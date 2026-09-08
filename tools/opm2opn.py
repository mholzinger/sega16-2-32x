#!/usr/bin/env python3
"""OPM -> OPN2+PSG transcoder (docs/sound/SOUND.md P4).

Input: a tools/snd_tap.lua log (Y0/Y1 = YM2151 address/data writes with
machine timestamps, from the mame altbeast oracle). Output: a C header
holding a stream in the Z80 player's opcode format (wait n / F0 YM-I /
F1 YM-II / F2 PSG), cut between --t0 and --t1 seconds.

All fidelity decisions live HERE, offline and auditable — never at
runtime. Facts the mapping rests on (ymfm, local checkout in
~/src/ares-debug/thirdparty/ymfm/src):

- Operator slots: OPM registers order a channel's operators identically
  to OPN in wiring terms (both use the 1,3,2,4 physical scramble the
  same way: opm operator_map ch0 = opnums 0,16,8,24; opn = 0,6,3,9
  which offset to +0,+8,+4,+12 — same order). So OPM reg 0x40+8*s+ch
  maps to OPN 0x30+4*s+(ch%3) on bank ch/3, and the key-on operator
  mask passes through unchanged (OPM 0x08 bits3-6 -> OPN 0x28 bits4-7).
- Pan is SWAPPED: OPM 0x20 bit7=right, bit6=left; OPN 0xB4 bit7=left,
  bit6=right.
- Pitch: OPM KC/KF (note+fraction, 4.000 MHz arcade clock — 1.1175x
  nominal 3.579545) -> Hz -> OPN F-num/block at 7.670454 MHz
  (fnum = freq * 2^(21-block) * 144 / clock).
- DT2 (OPM 0xC0 bits6-7) has no OPN equivalent: counted and WARNED,
  dropped. LFO is mapped coarsely (nearest OPN rate) only when depth
  is nonzero. Timer regs (0x10-0x14) are dropped — tempo is our wait
  stream, and OPN 0x24-0x27 belong to the Z80 player's clock (LAW).
- 8 OPM channels -> 6 OPN + PSG: the 6 channels with the most key-ons
  in the cut get FM; overflow channels render as PSG squares (pitch
  from KC/KF, attenuation from the slot-3 carrier TL); OPM noise
  (reg 0x0F) on an overflow channel keys PSG noise.

The stream opens with a full state snapshot (patches + pitch, no
key-ons) so the cut is self-contained and restart-deterministic.
"""
import argparse
import math
import sys

OPM_CLOCK = 4000000.0
OPN_CLOCK = 7670454.0
# semitone-above-C for OPM KC note field 0-15 (3,7,11,15 alias upward)
KC_SEMI = [1, 2, 3, 4, 4, 5, 6, 7, 7, 8, 9, 10, 10, 11, 12, 13]
OPN_LFO_HZ = [3.98, 5.56, 6.02, 6.37, 6.88, 9.63, 48.1, 72.2]


def opm_freq(kc, kf):
    oct_ = (kc >> 4) & 7
    semi = KC_SEMI[kc & 15]
    # A4 = oct 4, note A (semi 9 above C), 440 Hz at nominal clock
    n = (oct_ - 4) * 12 + (semi - 9) + (kf >> 2) / 64.0
    return 440.0 * (2.0 ** (n / 12.0)) * (OPM_CLOCK / 3579545.0)


def opn_fnum(freq):
    for block in range(1, 8):
        fnum = int(round(freq * (2 ** (21 - block)) * 144.0 / OPN_CLOCK))
        if fnum <= 2047:
            return block, max(1, fnum)
    return 7, 2047


class Stream:
    """Emitter with value-dedup: the arcade driver rewrites unchanged
    TL/pitch values relentlessly (the idle baseline is ~120 writes/s);
    an OPN-side shadow drops them, which is a ~4x size win. Reg 0x28
    (key on/off) is NEVER deduped — a same-value write is a retrigger."""

    def __init__(self):
        self.out = []
        self.t = None   # stream time in seconds
        self.shadow = [{}, {}]
        self.last_psg = None

    def wait_to(self, t):
        if self.t is None:
            self.t = t
            return
        ms = int((t - self.t) * 1000.0)
        if ms <= 0:
            return
        self.t += ms / 1000.0
        while ms > 127:
            self.out.append(127)
            ms -= 127
        if ms:
            self.out.append(ms)

    def ym(self, part, reg, val):
        val &= 0xFF
        if reg != 0x28 and self.shadow[part].get(reg) == val:
            return
        self.shadow[part][reg] = val
        self.out.extend([0xF0 if part == 0 else 0xF1, reg & 0xFF, val])

    def psg(self, val):
        # dedup only immediate repeats of latched attenuation bytes;
        # tone data bytes must always pass through
        self.out.extend([0xF2, val & 0xFF])


class Transcoder:
    def __init__(self, fm_map, psg_map):
        self.fm_map = fm_map      # opm ch -> opn ch (0-5) or None
        self.psg_map = psg_map    # opm ch -> psg tone ch (0-2) or None
        self.s = Stream()
        self.dt2_drops = 0
        self.csm_seen = False
        self.lfo_on = False

    # ---- combined OPN B4 from OPM 0x20 (pan) + 0x38 (PMS/AMS) --------
    def emit_b4(self, shadow, ch, opn):
        r20 = shadow[0x20 + ch]
        r38 = shadow[0x38 + ch]
        left = (r20 >> 6) & 1          # OPM bit6 = left
        right = (r20 >> 7) & 1         # OPM bit7 = right
        pms = (r38 >> 4) & 7
        ams = r38 & 3
        val = (left << 7) | (right << 6) | (ams << 4) | pms
        self.s.ym(opn // 3, 0xB4 + (opn % 3), val)

    def emit_pitch(self, shadow, ch, opn):
        kc = shadow[0x28 + ch]
        kf = shadow[0x30 + ch]
        block, fnum = opn_fnum(opm_freq(kc, kf))
        part, ofs = opn // 3, opn % 3
        self.s.ym(part, 0xA4 + ofs, ((block & 7) << 3) | (fnum >> 8))
        self.s.ym(part, 0xA0 + ofs, fnum & 0xFF)

    def emit_op(self, shadow, ch, opn, slot):
        part, ofs = opn // 3, opn % 3
        src = 8 * slot + ch
        dst = 4 * slot + ofs
        v = shadow[0x40 + src]
        self.s.ym(part, 0x30 + dst, v & 0x7F)          # DT1/MUL
        self.s.ym(part, 0x40 + dst, shadow[0x60 + src] & 0x7F)   # TL
        self.s.ym(part, 0x50 + dst, shadow[0x80 + src])          # KS/AR
        self.s.ym(part, 0x60 + dst, shadow[0xA0 + src])          # AM/D1R
        d2 = shadow[0xC0 + src]
        if d2 & 0xC0:
            self.dt2_drops += 1
        self.s.ym(part, 0x70 + dst, d2 & 0x1F)                   # D2R
        self.s.ym(part, 0x80 + dst, shadow[0xE0 + src])          # SL/RR
        self.s.ym(part, 0x90 + dst, 0)                           # SSG-EG off

    def psg_key(self, shadow, ch, tone, on):
        if not on:
            self.s.psg(0x9F | (tone << 5))
            return
        freq = opm_freq(shadow[0x28 + ch], shadow[0x30 + ch])
        div = max(1, min(1023, int(3579545.0 / (32.0 * freq))))
        self.s.psg(0x80 | (tone << 5) | (div & 0x0F))
        self.s.psg((div >> 4) & 0x3F)
        tl = shadow[0x60 + 8 * 3 + ch] & 0x7F          # slot-3 carrier TL
        # TL is 0.75dB/step, PSG attenuation is 2dB/step, so match the dB:
        # att = TL*0.75/2 = TL*3/8. The old TL>>3 (TL/8) left the PSG
        # channels ~3x too loud, blaring over the FM (2026-09-02, Mike's
        # attenuation catch).
        # psg_extra: additional attenuation steps (2dB each) set by the
        # builder to match the arcade's high band (the squares' harmonics
        # ran +23dB over the arcade FM in the A/B, 2026-09-02).
        att = min(15, ((tl * 3) >> 3) + getattr(self, "psg_extra", 0))
        self.s.psg(0x90 | (tone << 5) | att)

    def snapshot(self, shadow):
        for v in (0x9F, 0xBF, 0xDF, 0xFF):             # PSG all silent
            self.s.psg(v)
        self.s.ym(0, 0x22, 0x00)                       # LFO off until mapped
        self.s.ym(0, 0x2B, 0x00)                       # DAC off
        self.maybe_lfo(shadow)
        for ch in range(8):
            opn = self.fm_map.get(ch)
            if opn is None:
                continue
            r20 = shadow[0x20 + ch]
            part, ofs = opn // 3, opn % 3
            self.s.ym(0, 0x28, 0x00 | self.keych(opn)) # key off
            self.s.ym(part, 0xB0 + ofs, r20 & 0x3F)    # FB/ALG
            self.emit_b4(shadow, ch, opn)
            for slot in range(4):
                self.emit_op(shadow, ch, opn, slot)
            self.emit_pitch(shadow, ch, opn)

    def keych(self, opn):
        return (opn % 3) | ((opn // 3) << 2)

    def maybe_lfo(self, shadow):
        amd = shadow[0x19]
        pmd = shadow[0x1A]                              # ymfm's PM alias
        if amd == 0 and pmd == 0:
            if self.lfo_on:
                self.s.ym(0, 0x22, 0x00)
                self.lfo_on = False
            return
        lfrq = shadow[0x18]
        hz = 0.008 * (2.0 ** (lfrq / 16.0))            # coarse OPM curve
        best = min(range(8), key=lambda i: abs(OPN_LFO_HZ[i] - hz))
        self.s.ym(0, 0x22, 0x08 | best)
        self.lfo_on = True

    def event(self, shadow, reg, val):
        s = self.s
        if reg == 0x08:                                # key on/off
            ch = val & 7
            mask = (val >> 3) & 0xF
            opn = self.fm_map.get(ch)
            if opn is not None:
                s.ym(0, 0x28, (mask << 4) | self.keych(opn))
            elif self.psg_map.get(ch) is not None:
                self.psg_key(shadow, ch, self.psg_map[ch], mask != 0)
            return
        if reg in (0x10, 0x11, 0x12):                  # timers: drop
            return
        if reg == 0x14:
            if val & 0x80:
                self.csm_seen = True
            return
        if reg in (0x18, 0x19, 0x1A):
            self.maybe_lfo(shadow)
            return
        if reg == 0x0F:                                # noise -> PSG ch3
            if val & 0x80:
                rate = 3 - min(3, (val & 0x1F) >> 3)
                s.psg(0xE0 | 0x04 | rate)              # white noise
                s.psg(0xF0 | 0x04)
            else:
                s.psg(0xFF)
            return
        if reg < 0x20:
            return
        ch = reg & 7
        base = reg & 0xF8
        opn = self.fm_map.get(ch)
        if base in (0x28, 0x30):                       # KC / KF
            if opn is not None:
                self.emit_pitch(shadow, ch, opn)
            # PSG overflow pitch updates land on the next key event
            return
        if opn is None:
            return
        part, ofs = opn // 3, opn % 3
        if base == 0x20:
            s.ym(part, 0xB0 + ofs, val & 0x3F)
            self.emit_b4(shadow, ch, opn)
            return
        if base == 0x38:
            self.emit_b4(shadow, ch, opn)
            return
        slot = (reg >> 3) & 3
        fam = reg & 0xE0
        dst = 4 * slot + ofs
        if fam == 0x40:
            s.ym(part, 0x30 + dst, val & 0x7F)
        elif fam == 0x60:
            s.ym(part, 0x40 + dst, val & 0x7F)
        elif fam == 0x80:
            s.ym(part, 0x50 + dst, val)
        elif fam == 0xA0:
            s.ym(part, 0x60 + dst, val)
        elif fam == 0xC0:
            if val & 0xC0:
                self.dt2_drops += 1
            s.ym(part, 0x70 + dst, val & 0x1F)
        elif fam == 0xE0:
            s.ym(part, 0x80 + dst, val)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("out_h")
    ap.add_argument("--t0", type=float, default=9.0)
    ap.add_argument("--t1", type=float, default=35.5)
    ap.add_argument("--name", default="music_track")
    args = ap.parse_args()

    # pass 1: shadow state at t0 + key-on histogram inside the cut
    shadow = [0] * 256
    events = []
    keyons = [0] * 8
    addr = 0
    for line in open(args.log):
        p = line.split()
        if len(p) != 3 or p[1] not in ("Y0", "Y1"):
            continue
        t, val = float(p[0]), int(p[2], 16)
        if p[1] == "Y0":
            addr = val
            continue
        if t < args.t0:
            if addr == 0x19:                    # ymfm PM-depth alias
                shadow[0x19 + (val >> 7)] = val
            else:
                shadow[addr] = val
            continue
        if t > args.t1:
            break
        events.append((t, addr, val))
        if addr == 0x08 and (val >> 3) & 0xF:
            keyons[val & 7] += 1

    order = sorted(range(8), key=lambda c: -keyons[c])
    fm_map = {ch: i for i, ch in enumerate(order[:6])}
    psg_map = {ch: i for i, ch in enumerate(order[6:8]) if keyons[ch]}
    print("keyons/ch:", keyons)
    print("fm_map:", fm_map, " psg_map:", psg_map)

    tc = Transcoder(fm_map, psg_map)
    tc.s.t = args.t0
    tc.snapshot(shadow)
    for t, reg, val in events:
        tc.s.wait_to(t)
        tc.event(shadow, reg, val)
        if reg == 0x19:
            shadow[0x19 + (val >> 7)] = val
        else:
            shadow[reg] = val
    # close: everything off so the 68K's source-wrap restart is clean
    for opn in range(6):
        tc.s.ym(0, 0x28, tc.keych(opn))
    for v in (0x9F, 0xBF, 0xDF, 0xFF):
        tc.s.psg(v)
    tc.s.out.append(60)                          # breath before the loop

    if tc.dt2_drops:
        print("WARNING: %d DT2 values dropped (no OPN equivalent)"
              % tc.dt2_drops, file=sys.stderr)
    if tc.csm_seen:
        print("WARNING: CSM mode requested — unsupported", file=sys.stderr)

    data = tc.s.out
    with open(args.out_h, "w") as f:
        f.write("/* AUTO-GENERATED by tools/opm2opn.py — do not edit.\n"
                " * Cut %.1f..%.1fs of the attract tap. */\n"
                "#include <stdint.h>\n\n" % (args.t0, args.t1))
        f.write("#define %s_LEN %d\n\n" % (args.name.upper(), len(data)))
        f.write("static const uint8_t %s[%s_LEN] = {\n"
                % (args.name, args.name.upper()))
        for off in range(0, len(data), 16):
            f.write("\t" + ",".join("0x%02X" % b
                                    for b in data[off:off + 16]) + ",\n")
        f.write("};\n")
    print("%s: %d bytes (%.1fs cut)" % (args.out_h, len(data),
                                        args.t1 - args.t0))


if __name__ == "__main__":
    main()
