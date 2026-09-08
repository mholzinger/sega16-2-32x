#!/usr/bin/env python3
"""Per-sample YM2151 (OPM) synthesizer — integer, SH-2-portable. KIT.

The 32X can play all 8 arcade FM channels only by synthesizing them in
software (the YM2612 has 6 FM; opm2opn squashes 2 to PSG — the tonal
loss). This is a per-SAMPLE OPM synth (~480 ops/sample for 8 voices ~=
half an SH-2 at 22kHz), NOT cycle-accurate like Nuked-OPM (which is ~17x
too heavy for real-time). Validated offline against the Nuked reference,
then ported to SH-2 C. Tables + algorithm derived from jt51's opm.c /
the YM2151 datasheet (docs/sound/SOUND_DRIVER.md, [[sh2-fm-synth]]).

Standard OPM operator: phase acc -> logsin(phase+mod) -> +env atten +TL
-> exp -> linear. 4 operators/channel connected by the algorithm, op1
has feedback. Attenuation domain is log2 (0x400 = 6.02 dB * ... per the
tables). This module renders a VGM to 16-bit PCM for validation.
"""
import json
import os
import struct

_T = json.load(open(os.path.join(os.path.dirname(__file__) or ".",
                                 "ymtables.json"))) \
    if os.path.exists(os.path.join(os.path.dirname(__file__) or ".",
                                   "ymtables.json")) else None

# --- envelope rate step table (OPL/OPM standard: per-rate increment ----
# eg advances by a pattern of 0/1 per sample keyed by (rate, counter).
EG_INC = [
    [0, 1, 0, 1, 0, 1, 0, 1],  # 0: rates 0..
    [0, 1, 0, 1, 1, 1, 0, 1],  # 1
    [0, 1, 1, 1, 0, 1, 1, 1],  # 2
    [0, 1, 1, 1, 1, 1, 1, 1],  # 3
]


class OP:
    __slots__ = ("phase", "inc", "env", "state", "ar", "d1r", "d2r", "rr",
                 "d1l", "tl", "ks", "mul", "dt1", "dt2", "kc", "out", "prev")

    def __init__(self):
        self.phase = 0
        self.inc = 0
        self.env = 0x3FF          # start silent (max attenuation)
        self.state = 0            # 0 off/release,1 attack,2 decay,3 sustain
        self.ar = self.d1r = self.d2r = self.rr = 0
        self.d1l = 0
        self.tl = 0
        self.ks = self.mul = self.dt1 = self.dt2 = 0
        self.kc = 0
        self.out = 0
        self.prev = 0             # feedback history


class YM2151:
    def __init__(self, clock=4000000, tables=None):
        self.t = tables or _T
        self.logsin = self.t["logsin"]
        self.exp = self.t["exp"]
        self.freqtab = self.t["freqtable"]
        self.detune = self.t["detune"]
        self.clock = clock
        self.sample_rate = clock / 64.0
        self.reg = [0] * 256
        self.ops = [[OP() for _ in range(4)] for _ in range(8)]  # [ch][op]
        self.alg = [0] * 8
        self.fb = [0] * 8
        self.kc = [0] * 8
        self.kf = [0] * 8
        self.pms = [0] * 8
        self.ams = [0] * 8
        self.pan = [3] * 8

    # OPM operator slot order in the register map is interleaved:
    # register op index m = 0(M1),1(M2? actually order M1,C1,M2,C2)...
    # OPM uses op order: 0=M1,1=M2,2=C1,3=C2 with reg stride 8 per op.
    def write(self, addr, data):
        self.reg[addr] = data
        if addr == 0x08:                      # key on/off
            ch = data & 7
            slots = (data >> 3) & 0xF
            for oi in range(4):
                op = self.ops[ch][oi]
                if slots & (1 << oi):
                    if op.state == 0:
                        op.state = 1          # attack
                        op.phase = 0
                else:
                    if op.state != 0:
                        op.state = 0          # release
        elif 0x20 <= addr < 0x28:
            ch = addr & 7
            self.pan[ch] = (data >> 6) & 3
            self.fb[ch] = (data >> 3) & 7
            self.alg[ch] = data & 7
        elif 0x28 <= addr < 0x30:
            self.kc[addr & 7] = data & 0x7F
            self._recalc(addr & 7)
        elif 0x30 <= addr < 0x38:
            self.kf[addr & 7] = (data >> 2) & 0x3F
            self._recalc(addr & 7)
        elif 0x38 <= addr < 0x40:
            ch = addr & 7
            self.pms[ch] = (data >> 4) & 7
            self.ams[ch] = data & 3
        else:
            self._op_reg(addr, data)

    def _op_slot(self, addr):
        # addr low 5 bits: bit0-2 channel, bit3-4 op. OPM op order 0,2,1,3
        ch = addr & 7
        oi = (addr >> 3) & 3
        return ch, (0, 2, 1, 3)[oi]

    def _op_reg(self, addr, data):
        hi = addr & 0xE0
        ch, oi = self._op_slot(addr)
        op = self.ops[ch][oi]
        if hi == 0x40:      # DT1 | MUL
            op.dt1 = (data >> 4) & 7
            op.mul = data & 0xF
            self._recalc(ch)
        elif hi == 0x60:    # TL
            op.tl = data & 0x7F
        elif hi == 0x80:    # KS | AR
            op.ks = (data >> 6) & 3
            op.ar = data & 0x1F
        elif hi == 0xA0:    # AMS-EN | D1R
            op.d1r = data & 0x1F
        elif hi == 0xC0:    # DT2 | D2R
            op.dt2 = (data >> 6) & 3
            op.d2r = data & 0x1F
        elif hi == 0xE0:    # D1L | RR
            op.d1l = (data >> 4) & 0xF
            op.rr = data & 0xF

    def _kc_to_fnum(self, kcode):
        # exact OPM_KCToFNum (opm.c): kcode 13-bit -> fnum
        kh = (kcode >> 4) & 63
        kl = kcode & 15
        base, atype, slope = self.freqtab[kh]
        s = 0
        if atype:
            for i in range(4):
                if kl & (1 << i):
                    s += slope >> (3 - i)
        else:
            sl = slope | 1
            if kl & 1: s += (sl >> 3) + 2
            if kl & 2: s += 8
            if kl & 4: s += sl >> 1
            if kl & 8: s += sl + 1
            if (kl & 12) == 12 and (slope & 1) == 0: s += 4
        return base + (s >> 1)

    def _recalc(self, ch):
        kcf = (self.kc[ch] << 6) + self.kf[ch]     # 13-bit key code+fraction
        fnum = self._kc_to_fnum(kcf)
        kcode_h = kcf >> 8
        block = kcode_h >> 2
        basefreq = ((fnum << block) >> 2) & 0x1FFFF
        for oi in range(4):
            op = self.ops[ch][oi]
            # detune 1 (dt1) — apply the pg_detune wobble
            det = 0
            if op.dt1 & 3:
                kc2 = min(0x1C, kcode_h)
                blk = kc2 >> 2
                note = kc2 & 3
                dl = op.dt1 & 3
                sm = blk + 9 + ((dl == 3) | (dl & 2))
                det = self.detune[((sm & 1) << 2) | note] >> (9 - (sm >> 1))
            bf = (basefreq - det) if (op.dt1 & 4) else (basefreq + det)
            bf &= 0x1FFFF
            op.inc = ((bf * op.mul) if op.mul else (bf >> 1)) & 0xFFFFF
            op.kc = self.kc[ch]

    def _eg_rate(self, op, base):
        if base == 0:
            return 0
        r = base * 2 + (op.kc >> (5 - op.ks) if op.ks < 5 else op.kc)
        return min(63, r)

    def _operator(self, op, mod, counter):
        # envelope advance
        if op.state == 1:       # attack
            rate = self._eg_rate(op, op.ar)
            if rate >= 62:
                op.env = 0
            else:
                shift = 11 - (rate >> 2)
                if (counter & ((1 << max(0, shift)) - 1)) == 0:
                    inc = EG_INC[rate & 3][(counter >> max(0, shift)) & 7]
                    op.env += ((~op.env * inc) >> 4)
            if op.env <= 0:
                op.env = 0
                op.state = 2
        elif op.state == 2:     # decay to sustain
            rate = self._eg_rate(op, op.d1r)
            shift = 11 - (rate >> 2)
            if rate and (counter & ((1 << max(0, shift)) - 1)) == 0:
                op.env += EG_INC[rate & 3][(counter >> max(0, shift)) & 7]
            if op.env >= (op.d1l << 5):
                op.state = 3
        elif op.state == 3:     # sustain decay (d2r)
            rate = self._eg_rate(op, op.d2r)
            shift = 11 - (rate >> 2)
            if rate and (counter & ((1 << max(0, shift)) - 1)) == 0:
                op.env += EG_INC[rate & 3][(counter >> max(0, shift)) & 7]
            if op.env > 0x3FF:
                op.env = 0x3FF
        else:                   # release
            rate = self._eg_rate(op, op.rr * 2 + 1)
            shift = 11 - (rate >> 2)
            if (counter & ((1 << max(0, shift)) - 1)) == 0:
                op.env += EG_INC[rate & 3][(counter >> max(0, shift)) & 7]
            if op.env > 0x3FF:
                op.env = 0x3FF
        # phase
        op.phase = (op.phase + op.inc) & 0xFFFFF
        ph = ((op.phase >> 10) + mod) & 0x3FF
        quad = (ph >> 8) & 3
        idx = ph & 0xFF
        if quad & 1:
            idx ^= 0xFF
        att = self.logsin[idx] + (op.env << 2) + (op.tl << 5)
        if att > 0x1FFF:
            att = 0x1FFF
        val = (self.exp[att & 0xFF] << 2) >> (att >> 8)
        if quad & 2:
            val = -val
        op.out = val
        return val

    def sample(self, counter):
        left = 0
        for ch in range(8):
            ops = self.ops[ch]
            alg = self.alg[ch]
            # feedback on op0 (M1)
            fbin = 0
            if self.fb[ch]:
                fbin = (ops[0].prev) >> (9 - self.fb[ch])
            m1 = self._operator(ops[0], fbin, counter)
            ops[0].prev = (ops[0].prev + m1) >> 1
            # simplified connection: use algorithm to route. This is the
            # standard OPM 8-alg topology.
            out = self._connect(ch, alg, m1, counter)
            left += out
        # clamp
        if left > 32767: left = 32767
        if left < -32768: left = -32768
        return left

    def _connect(self, ch, alg, m1, counter):
        # op order 0=M1,1=M2,2=C1,3=C2. Modulation = operator output added
        # directly to the modulated op's phase (Nuked: & 1023, no >>1).
        o = self.ops[ch]
        if alg == 0:    # M1->M2->C1->C2
            a = self._operator(o[1], m1, counter)
            b = self._operator(o[2], a, counter)
            return self._operator(o[3], b, counter)
        if alg == 1:    # (M1+M2)->C1->C2
            a = self._operator(o[1], 0, counter)
            b = self._operator(o[2], m1 + a, counter)
            return self._operator(o[3], b, counter)
        if alg == 2:    # M1->C1, M2->C1 ; C1->C2  (M2->C2 too per table)
            a = self._operator(o[1], 0, counter)
            b = self._operator(o[2], a, counter)
            return self._operator(o[3], m1 + b, counter)
        if alg == 3:    # M1->C2, M2->C1->C2
            a = self._operator(o[1], m1, counter)
            b = self._operator(o[2], 0, counter)
            return self._operator(o[3], a + b, counter)
        if alg == 4:    # M1->C1(out), M2->C2(out)
            a = self._operator(o[1], m1, counter)
            b = self._operator(o[2], 0, counter)
            c = self._operator(o[3], b, counter)
            return a + c
        if alg == 5:    # M1->C1,C2,M2  (M1 modulates all three)
            a = self._operator(o[1], m1, counter)
            b = self._operator(o[2], m1, counter)
            c = self._operator(o[3], m1, counter)
            return a + b + c
        if alg == 6:    # M1->C1(out); M2,C2 out
            a = self._operator(o[1], m1, counter)
            b = self._operator(o[2], 0, counter)
            c = self._operator(o[3], 0, counter)
            return a + b + c
        # alg 7: all 4 operators are carriers
        a = self._operator(o[1], 0, counter)
        b = self._operator(o[2], 0, counter)
        c = self._operator(o[3], 0, counter)
        return m1 + a + b + c


def render_vgm(vgm_path, wav_path, clock=4000000, seconds=None):
    import gzip
    d = open(vgm_path, "rb").read()
    if d[:2] == b"\x1f\x8b":
        d = gzip.decompress(d)
    u32 = lambda o: struct.unpack("<I", d[o:o + 4])[0]
    dataoff = (u32(0x34) + 0x34) if u32(0x08) >= 0x150 and u32(0x34) else 0x40
    chip = YM2151(clock)
    sr = chip.sample_rate
    clk_per_vgm = clock / 44100.0
    out = []
    i = dataoff
    wait = 0.0
    counter = 0
    clk_since = 0
    maxsamp = int(seconds * sr) if seconds else 10**12
    while i < len(d) and len(out) < maxsamp:
        c = d[i]
        if c == 0x54:
            chip.write(d[i + 1], d[i + 2]); i += 3; continue
        elif c == 0x61:
            wait += (d[i + 1] | (d[i + 2] << 8)) * clk_per_vgm; i += 3
        elif c == 0x62:
            wait += 735 * clk_per_vgm; i += 1
        elif c == 0x63:
            wait += 882 * clk_per_vgm; i += 1
        elif 0x70 <= c <= 0x7F:
            wait += ((c & 0xF) + 1) * clk_per_vgm; i += 1
        elif c == 0x66:
            break
        elif c == 0x67:
            i += 7 + u32(i + 3)
        elif c == 0x50:
            i += 2
        else:
            i += 1
        while wait >= 64 and len(out) < maxsamp:
            counter += 1
            out.append(chip.sample(counter))
            wait -= 64
    w = __import__("wave").open(wav_path, "w")
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(int(sr))
    w.writeframes(struct.pack("<%dh" % len(out), *out)); w.close()
    print(f"rendered {len(out)} samples ({len(out)/sr:.1f}s @{sr:.0f}Hz)")


if __name__ == "__main__":
    import sys
    render_vgm(sys.argv[1], sys.argv[2],
               seconds=float(sys.argv[3]) if len(sys.argv) > 3 else None)
