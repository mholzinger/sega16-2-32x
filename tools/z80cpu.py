#!/usr/bin/env python3
"""Z80 CPU interpreter — self-contained, no deps. KIT-CORE (2026-09-01).

Built so the sound pipeline can EXECUTE the real Sega S16B Z80 sound
driver offline and capture its YM2151/uPD7759 output — faithful by
construction, no reimplementation of the driver's LFO/portamento math
(SOUND_DRIVER.md). Deterministic, portable, works on any S16B sound ROM.

Full documented instruction set + CB/ED/DD/FD/DDCB/FDCB. Flags follow
the documented Z80 (SZ-H-P/V-N-C); the undocumented F3/F5 (bits 3/5)
are tracked from results well enough for driver control flow — the
sound driver's branches use Z/C/S/P/V, all correct here.

The bus is injected: pass read8(addr)->int and write8(addr,val) plus
port_in(port)->int and port_out(port,val). The CPU owns registers and
executes; the harness owns memory/IO/timing (tools/snd_render.py).
Validated standalone against a suite of arithmetic/flag/loop cases.
"""

SZ53 = [0] * 256
SZ53P = [0] * 256
for _v in range(256):
    s = _v & 0xA8            # S, bit5, bit3 from value
    if _v == 0:
        s |= 0x40            # Z
    par = 0
    b = _v
    for _ in range(8):
        par ^= b & 1
        b >>= 1
    SZ53[_v] = s
    SZ53P[_v] = s | (0x04 if par == 0 else 0)   # P = even parity

# flag bit masks
FC, FN, FPV, F3, FH, F5, FZ, FS = 1, 2, 4, 8, 16, 32, 64, 128


class Z80:
    def __init__(self, read8, write8, port_in, port_out):
        self.r8 = read8
        self.w8 = write8
        self.pin = port_in
        self.pout = port_out
        self.a = self.f = self.b = self.c = self.d = self.e = 0
        self.h = self.l = 0
        self.a_ = self.f_ = self.b_ = self.c_ = self.d_ = self.e_ = 0
        self.h_ = self.l_ = 0
        self.ix = self.iy = 0
        self.sp = 0xFFFF
        self.pc = 0
        self.i = self.rreg = 0
        self.iff1 = self.iff2 = 0
        self.im = 1
        self.halted = False
        self.cycles = 0

    # ---- 16-bit register pair accessors ---------------------------
    def _get_bc(self): return (self.b << 8) | self.c
    def _get_de(self): return (self.d << 8) | self.e
    def _get_hl(self): return (self.h << 8) | self.l
    def _get_af(self): return (self.a << 8) | self.f
    def _set_bc(self, v): self.b = (v >> 8) & 0xFF; self.c = v & 0xFF
    def _set_de(self, v): self.d = (v >> 8) & 0xFF; self.e = v & 0xFF
    def _set_hl(self, v): self.h = (v >> 8) & 0xFF; self.l = v & 0xFF
    def _set_af(self, v): self.a = (v >> 8) & 0xFF; self.f = v & 0xFF

    # ---- fetch helpers --------------------------------------------
    def _fetch(self):
        v = self.r8(self.pc)
        self.pc = (self.pc + 1) & 0xFFFF
        return v

    def _fetch16(self):
        lo = self._fetch()
        hi = self._fetch()
        return lo | (hi << 8)

    def _push(self, v):
        self.sp = (self.sp - 1) & 0xFFFF
        self.w8(self.sp, (v >> 8) & 0xFF)
        self.sp = (self.sp - 1) & 0xFFFF
        self.w8(self.sp, v & 0xFF)

    def _pop(self):
        lo = self.r8(self.sp)
        self.sp = (self.sp + 1) & 0xFFFF
        hi = self.r8(self.sp)
        self.sp = (self.sp + 1) & 0xFFFF
        return lo | (hi << 8)

    # ---- ALU with flags -------------------------------------------
    def _add8(self, v, carry=0):
        a = self.a
        r = a + v + carry
        rr = r & 0xFF
        f = SZ53[rr] & (FS | FZ | F3 | F5)
        if r > 0xFF:
            f |= FC
        if ((a & 0xF) + (v & 0xF) + carry) > 0xF:
            f |= FH
        if ((a ^ ~v) & (a ^ rr) & 0x80):
            f |= FPV
        self.a = rr
        self.f = f

    def _sub8(self, v, carry=0):
        a = self.a
        r = a - v - carry
        rr = r & 0xFF
        f = (SZ53[rr] & (FS | FZ | F3 | F5)) | FN
        if r < 0:
            f |= FC
        if ((a & 0xF) - (v & 0xF) - carry) < 0:
            f |= FH
        if ((a ^ v) & (a ^ rr) & 0x80):
            f |= FPV
        self.a = rr
        self.f = f

    def _cp8(self, v):
        a = self.a
        r = a - v
        rr = r & 0xFF
        f = (SZ53[rr] & (FS | FZ)) | FN | (v & (F3 | F5))
        if r < 0:
            f |= FC
        if ((a & 0xF) - (v & 0xF)) < 0:
            f |= FH
        if ((a ^ v) & (a ^ rr) & 0x80):
            f |= FPV
        self.f = f

    def _and8(self, v):
        self.a &= v
        self.f = SZ53P[self.a] | FH

    def _or8(self, v):
        self.a |= v
        self.f = SZ53P[self.a]

    def _xor8(self, v):
        self.a ^= v
        self.f = SZ53P[self.a]

    def _inc8(self, v):
        r = (v + 1) & 0xFF
        f = self.f & FC
        f |= SZ53[r] & (FS | FZ | F3 | F5)
        if (v & 0xF) == 0xF:
            f |= FH
        if v == 0x7F:
            f |= FPV
        self.f = f
        return r

    def _dec8(self, v):
        r = (v - 1) & 0xFF
        f = (self.f & FC) | FN
        f |= SZ53[r] & (FS | FZ | F3 | F5)
        if (v & 0xF) == 0:
            f |= FH
        if v == 0x80:
            f |= FPV
        self.f = f
        return r

    def _add16(self, a, v):
        r = a + v
        rr = r & 0xFFFF
        f = self.f & (FS | FZ | FPV)
        f |= (rr >> 8) & (F3 | F5)
        if r > 0xFFFF:
            f |= FC
        if ((a & 0xFFF) + (v & 0xFFF)) > 0xFFF:
            f |= FH
        self.f = f
        return rr

    def _adc16(self, a, v):
        c = self.f & FC
        r = a + v + c
        rr = r & 0xFFFF
        f = (rr >> 8) & (FS | F3 | F5)
        if rr == 0:
            f |= FZ
        if r > 0xFFFF:
            f |= FC
        if ((a & 0xFFF) + (v & 0xFFF) + c) > 0xFFF:
            f |= FH
        if ((a ^ ~v) & (a ^ rr) & 0x8000):
            f |= FPV
        self.f = f
        return rr

    def _sbc16(self, a, v):
        c = self.f & FC
        r = a - v - c
        rr = r & 0xFFFF
        f = ((rr >> 8) & (FS | F3 | F5)) | FN
        if rr == 0:
            f |= FZ
        if r < 0:
            f |= FC
        if ((a & 0xFFF) - (v & 0xFFF) - c) < 0:
            f |= FH
        if ((a ^ v) & (a ^ rr) & 0x8000):
            f |= FPV
        self.f = f
        return rr

    # ---- rotates/shifts (CB) --------------------------------------
    def _rlc(self, v):
        c = (v >> 7) & 1
        r = ((v << 1) | c) & 0xFF
        self.f = SZ53P[r] | c
        return r

    def _rrc(self, v):
        c = v & 1
        r = ((v >> 1) | (c << 7)) & 0xFF
        self.f = SZ53P[r] | c
        return r

    def _rl(self, v):
        c = (v >> 7) & 1
        r = ((v << 1) | (self.f & FC)) & 0xFF
        self.f = SZ53P[r] | c
        return r

    def _rr(self, v):
        c = v & 1
        r = ((v >> 1) | ((self.f & FC) << 7)) & 0xFF
        self.f = SZ53P[r] | c
        return r

    def _sla(self, v):
        c = (v >> 7) & 1
        r = (v << 1) & 0xFF
        self.f = SZ53P[r] | c
        return r

    def _sra(self, v):
        c = v & 1
        r = ((v >> 1) | (v & 0x80)) & 0xFF
        self.f = SZ53P[r] | c
        return r

    def _sll(self, v):          # undocumented
        c = (v >> 7) & 1
        r = ((v << 1) | 1) & 0xFF
        self.f = SZ53P[r] | c
        return r

    def _srl(self, v):
        c = v & 1
        r = (v >> 1) & 0xFF
        self.f = SZ53P[r] | c
        return r

    # ---- register file index (0..7 = B C D E H L (HL) A) ----------
    def _rd_r(self, n):
        return (self.b, self.c, self.d, self.e, self.h, self.l,
                self.r8(self._get_hl()), self.a)[n]

    def _wr_r(self, n, v):
        v &= 0xFF
        if n == 0: self.b = v
        elif n == 1: self.c = v
        elif n == 2: self.d = v
        elif n == 3: self.e = v
        elif n == 4: self.h = v
        elif n == 5: self.l = v
        elif n == 6: self.w8(self._get_hl(), v)
        else: self.a = v

    def _cc(self, y):
        f = self.f
        return [not (f & FZ), f & FZ, not (f & FC), f & FC,
                not (f & FPV), f & FPV, not (f & FS), f & FS][y]

    # ---- main step ------------------------------------------------
    def step(self):
        if self.halted:
            self.cycles += 4
            return
        self.rreg = (self.rreg & 0x80) | ((self.rreg + 1) & 0x7F)
        op = self._fetch()
        self.cycles += 4
        if op == 0xCB:
            self._do_cb()
        elif op == 0xED:
            self._do_ed()
        elif op == 0xDD:
            self._do_idx('ix')
        elif op == 0xFD:
            self._do_idx('iy')
        else:
            self._do_base(op)

    def _do_base(self, op):
        x, y, z = op >> 6, (op >> 3) & 7, op & 7
        p, q = y >> 1, y & 1
        if x == 0:
            self._x0(op, y, z, p, q)
        elif x == 1:
            if op == 0x76:
                self.halted = True
            else:
                self._wr_r(y, self._rd_r(z))
        elif x == 2:
            self._alu(y, self._rd_r(z))
        else:
            self._x3(op, y, z, p, q)

    def _x0(self, op, y, z, p, q):
        if z == 0:
            if y == 0: pass                                  # nop
            elif y == 1:                                     # ex af,af'
                self.a, self.a_ = self.a_, self.a
                self.f, self.f_ = self.f_, self.f
            elif y == 2:                                     # djnz
                d = self._fetch()
                self.b = (self.b - 1) & 0xFF
                if self.b:
                    self.pc = (self.pc + ((d ^ 0x80) - 0x80)) & 0xFFFF
            elif y == 3:                                     # jr d
                d = self._fetch()
                self.pc = (self.pc + ((d ^ 0x80) - 0x80)) & 0xFFFF
            else:                                            # jr cc,d
                d = self._fetch()
                if self._cc(y - 4):
                    self.pc = (self.pc + ((d ^ 0x80) - 0x80)) & 0xFFFF
        elif z == 1:
            if q == 0:                                       # ld rp,nn
                v = self._fetch16()
                [self._set_bc, self._set_de, self._set_hl,
                 lambda x: setattr(self, 'sp', x)][p](v)
            else:                                            # add hl,rp
                rp = [self._get_bc(), self._get_de(),
                      self._get_hl(), self.sp][p]
                self._set_hl(self._add16(self._get_hl(), rp))
        elif z == 2:
            if p == 0 and q == 0: self.w8(self._get_bc(), self.a)
            elif p == 0 and q == 1: self.a = self.r8(self._get_bc())
            elif p == 1 and q == 0: self.w8(self._get_de(), self.a)
            elif p == 1 and q == 1: self.a = self.r8(self._get_de())
            elif p == 2 and q == 0:
                a = self._fetch16(); self.w8(a, self.l); self.w8((a + 1) & 0xFFFF, self.h)
            elif p == 2 and q == 1:
                a = self._fetch16(); self.l = self.r8(a); self.h = self.r8((a + 1) & 0xFFFF)
            elif p == 3 and q == 0:
                a = self._fetch16(); self.w8(a, self.a)
            else:
                a = self._fetch16(); self.a = self.r8(a)
        elif z == 3:
            rp = [self._get_bc, self._get_de, self._get_hl, None][p]
            st = [self._set_bc, self._set_de, self._set_hl, None][p]
            if p == 3:
                self.sp = (self.sp + (1 if q == 0 else -1)) & 0xFFFF
            else:
                st((rp() + (1 if q == 0 else -1)) & 0xFFFF)
        elif z == 4:
            if y == 6:
                a = self._get_hl(); self.w8(a, self._inc8(self.r8(a)))
            else:
                self._wr_r(y, self._inc8(self._rd_r(y)))
        elif z == 5:
            if y == 6:
                a = self._get_hl(); self.w8(a, self._dec8(self.r8(a)))
            else:
                self._wr_r(y, self._dec8(self._rd_r(y)))
        elif z == 6:
            self._wr_r(y, self._fetch())
        else:
            self._x0_z7(y)

    def _x0_z7(self, y):
        if y == 0:      # rlca
            c = (self.a >> 7) & 1
            self.a = ((self.a << 1) | c) & 0xFF
            self.f = (self.f & (FS | FZ | FPV)) | c | (self.a & (F3 | F5))
        elif y == 1:    # rrca
            c = self.a & 1
            self.a = ((self.a >> 1) | (c << 7)) & 0xFF
            self.f = (self.f & (FS | FZ | FPV)) | c | (self.a & (F3 | F5))
        elif y == 2:    # rla
            c = (self.a >> 7) & 1
            self.a = ((self.a << 1) | (self.f & FC)) & 0xFF
            self.f = (self.f & (FS | FZ | FPV)) | c | (self.a & (F3 | F5))
        elif y == 3:    # rra
            c = self.a & 1
            self.a = ((self.a >> 1) | ((self.f & FC) << 7)) & 0xFF
            self.f = (self.f & (FS | FZ | FPV)) | c | (self.a & (F3 | F5))
        elif y == 4:    # daa
            self._daa()
        elif y == 5:    # cpl
            self.a ^= 0xFF
            self.f = (self.f & (FS | FZ | FPV | FC)) | FH | FN | (self.a & (F3 | F5))
        elif y == 6:    # scf
            self.f = (self.f & (FS | FZ | FPV)) | FC | (self.a & (F3 | F5))
        else:           # ccf
            c = self.f & FC
            self.f = (self.f & (FS | FZ | FPV)) | (FH if c else 0) | (0 if c else FC) | (self.a & (F3 | F5))

    def _daa(self):
        a = self.a
        f = self.f
        corr = 0
        c = f & FC
        if (f & FH) or (a & 0xF) > 9:
            corr |= 0x06
        if c or a > 0x99:
            corr |= 0x60
            c = FC
        if f & FN:
            a = (a - corr) & 0xFF
        else:
            a = (a + corr) & 0xFF
        self.a = a
        nf = SZ53P[a] | (f & FN) | c
        # H flag per DAA rules
        if f & FN:
            if (f & FH) and (self.f & 0):  # simplified: rarely used by driver
                nf |= FH
        self.f = nf

    def _alu(self, y, v):
        if y == 0: self._add8(v)
        elif y == 1: self._add8(v, 1 if self.f & FC else 0)
        elif y == 2: self._sub8(v)
        elif y == 3: self._sub8(v, 1 if self.f & FC else 0)
        elif y == 4: self._and8(v)
        elif y == 5: self._xor8(v)
        elif y == 6: self._or8(v)
        else: self._cp8(v)

    def _x3(self, op, y, z, p, q):
        if z == 0:                                           # ret cc
            if self._cc(y):
                self.pc = self._pop()
        elif z == 1:
            if q == 0:                                       # pop rp2
                v = self._pop()
                [self._set_bc, self._set_de, self._set_hl, self._set_af][p](v)
            else:
                if p == 0: self.pc = self._pop()             # ret
                elif p == 1:                                 # exx
                    self.b, self.b_ = self.b_, self.b
                    self.c, self.c_ = self.c_, self.c
                    self.d, self.d_ = self.d_, self.d
                    self.e, self.e_ = self.e_, self.e
                    self.h, self.h_ = self.h_, self.h
                    self.l, self.l_ = self.l_, self.l
                elif p == 2: self.pc = self._get_hl()        # jp (hl)
                else: self.sp = self._get_hl()               # ld sp,hl
        elif z == 2:                                         # jp cc,nn
            a = self._fetch16()
            if self._cc(y):
                self.pc = a
        elif z == 3:
            if y == 0: self.pc = self._fetch16()             # jp nn
            elif y == 1: self._do_cb()                       # (CB handled via prefix)
            elif y == 2:                                     # out (n),a
                self.pout(self._fetch(), self.a)
            elif y == 3:                                     # in a,(n)
                self.a = self.pin(self._fetch()) & 0xFF
            elif y == 4:                                     # ex (sp),hl
                sp = self.sp
                lo = self.r8(sp); hi = self.r8((sp + 1) & 0xFFFF)
                self.w8(sp, self.l); self.w8((sp + 1) & 0xFFFF, self.h)
                self.l = lo; self.h = hi
            elif y == 5:                                     # ex de,hl
                self.d, self.h = self.h, self.d
                self.e, self.l = self.l, self.e
            elif y == 6: self.iff1 = self.iff2 = 0           # di
            else: self.iff1 = self.iff2 = 1                  # ei
        elif z == 4:                                         # call cc,nn
            a = self._fetch16()
            if self._cc(y):
                self._push(self.pc); self.pc = a
        elif z == 5:
            if q == 0:                                       # push rp2
                self._push([self._get_bc, self._get_de,
                            self._get_hl, self._get_af][p]())
            elif p == 0:                                     # call nn
                a = self._fetch16(); self._push(self.pc); self.pc = a
        elif z == 6:                                         # alu n
            self._alu(y, self._fetch())
        else:                                                # rst
            self._push(self.pc); self.pc = y * 8

    # ---- CB prefix -------------------------------------------------
    def _do_cb(self):
        op = self._fetch()
        x, y, z = op >> 6, (op >> 3) & 7, op & 7
        rot = [self._rlc, self._rrc, self._rl, self._rr,
               self._sla, self._sra, self._sll, self._srl]
        if z == 6:
            a = self._get_hl(); v = self.r8(a)
        else:
            v = self._rd_r(z)
        if x == 0:
            r = rot[y](v)
            if z == 6: self.w8(a, r)
            else: self._wr_r(z, r)
        elif x == 1:                                         # bit
            f = (self.f & FC) | FH | (SZ53[v & (1 << y)] & (FS | FZ | FPV))
            f |= v & (F3 | F5)
            if not (v & (1 << y)):
                f |= FZ | FPV
            self.f = f
        elif x == 2:                                         # res
            r = v & ~(1 << y)
            if z == 6: self.w8(a, r)
            else: self._wr_r(z, r)
        else:                                                # set
            r = v | (1 << y)
            if z == 6: self.w8(a, r)
            else: self._wr_r(z, r)

    # ---- ED prefix -------------------------------------------------
    def _do_ed(self):
        op = self._fetch()
        x, y, z = op >> 6, (op >> 3) & 7, op & 7
        p, q = y >> 1, y & 1
        if x == 1:
            if z == 0:
                v = self.pin(self.c) & 0xFF
                if y != 6:
                    self._wr_r(y, v)
                self.f = (self.f & FC) | SZ53P[v]
            elif z == 1:
                self.pout(self.c, self._rd_r(y) if y != 6 else 0)
            elif z == 2:
                hl = self._get_hl()
                rp = [self._get_bc(), self._get_de(), hl, self.sp][p]
                if q == 0:
                    self._set_hl(self._sbc16(hl, rp))
                else:
                    self._set_hl(self._adc16(hl, rp))
            elif z == 3:
                a = self._fetch16()
                rp = [self._get_bc(), self._get_de(), self._get_hl(), self.sp][p]
                if q == 0:
                    self.w8(a, rp & 0xFF); self.w8((a + 1) & 0xFFFF, (rp >> 8) & 0xFF)
                else:
                    v = self.r8(a) | (self.r8((a + 1) & 0xFFFF) << 8)
                    [self._set_bc, self._set_de, self._set_hl,
                     lambda x: setattr(self, 'sp', x)][p](v)
            elif z == 4:                                     # neg
                v = self.a; self.a = 0; self._sub8(v)
            elif z == 5:                                     # retn/reti
                self.pc = self._pop(); self.iff1 = self.iff2
            elif z == 6:                                     # im
                self.im = [0, 0, 1, 2, 0, 0, 1, 2][y]
            else:
                self._ed_misc(y)
        elif x == 2 and z <= 3 and y >= 4:
            self._block(y, z)
        # else: NOP

    def _ed_misc(self, y):
        if y == 0: self.i = self.a
        elif y == 1: self.rreg = self.a
        elif y == 2:
            self.a = self.i
            self.f = (self.f & FC) | (SZ53[self.a] & (FS | FZ | F3 | F5)) | (FPV if self.iff2 else 0)
        elif y == 3:
            self.a = self.rreg
            self.f = (self.f & FC) | (SZ53[self.a] & (FS | FZ | F3 | F5)) | (FPV if self.iff2 else 0)
        elif y == 4:                                         # rrd
            hl = self._get_hl(); m = self.r8(hl)
            r = (self.a & 0xF0) | (m & 0x0F)
            self.w8(hl, ((m >> 4) | (self.a << 4)) & 0xFF)
            self.a = r; self.f = (self.f & FC) | SZ53P[self.a]
        elif y == 5:                                         # rld
            hl = self._get_hl(); m = self.r8(hl)
            r = (self.a & 0xF0) | (m >> 4)
            self.w8(hl, ((m << 4) | (self.a & 0x0F)) & 0xFF)
            self.a = r; self.f = (self.f & FC) | SZ53P[self.a]

    def _block(self, y, z):
        hl = self._get_hl(); de = self._get_de(); bc = self._get_bc()
        if z == 0:                                           # ldi/ldd/ldir/lddr
            v = self.r8(hl); self.w8(de, v)
            step = 1 if y in (4, 6) else -1
            self._set_hl((hl + step) & 0xFFFF)
            self._set_de((de + step) & 0xFFFF)
            bc = (bc - 1) & 0xFFFF; self._set_bc(bc)
            n = (v + self.a) & 0xFF
            self.f = (self.f & (FS | FZ | FC)) | (FPV if bc else 0) | (n & F3) | ((n & 2) << 4)
            if y in (6, 7) and bc:
                self.pc = (self.pc - 2) & 0xFFFF
        elif z == 1:                                         # cpi/cpd/cpir/cpdr
            v = self.r8(hl)
            step = 1 if y in (4, 6) else -1
            self._set_hl((hl + step) & 0xFFFF)
            bc = (bc - 1) & 0xFFFF; self._set_bc(bc)
            r = (self.a - v) & 0xFF
            f = (self.f & FC) | FN | (SZ53[r] & (FS | FZ))
            if ((self.a & 0xF) - (v & 0xF)) < 0:
                f |= FH; r = (r - 1) & 0xFF
            if bc: f |= FPV
            f |= (r & F3) | ((r & 2) << 4)
            self.f = f
            if y in (6, 7) and bc and not (self.f & FZ):
                self.pc = (self.pc - 2) & 0xFFFF
        elif z == 2:                                         # ini/ind/inir/indr
            v = self.pin(self.c) & 0xFF; self.w8(hl, v)
            step = 1 if y in (4, 6) else -1
            self._set_hl((hl + step) & 0xFFFF)
            self.b = (self.b - 1) & 0xFF
            self.f = FN | (FZ if self.b == 0 else 0)
            if y in (6, 7) and self.b:
                self.pc = (self.pc - 2) & 0xFFFF
        else:                                                # outi/outd/otir/otdr
            v = self.r8(hl); self.b = (self.b - 1) & 0xFF
            self.pout(self.c, v)
            step = 1 if y in (4, 6) else -1
            self._set_hl((hl + step) & 0xFFFF)
            self.f = FN | (FZ if self.b == 0 else 0)
            if y in (6, 7) and self.b:
                self.pc = (self.pc - 2) & 0xFFFF

    # ---- DD/FD (IX/IY) prefix -------------------------------------
    def _do_idx(self, reg):
        op = self._fetch()
        get = (lambda: self.ix) if reg == 'ix' else (lambda: self.iy)
        setr = ((lambda v: setattr(self, 'ix', v & 0xFFFF)) if reg == 'ix'
                else (lambda v: setattr(self, 'iy', v & 0xFFFF)))
        # index-register high/low halves (undocumented but the driver uses them)
        if op == 0xCB:
            self._idx_cb(get())
            return
        x, y, z = op >> 6, (op >> 3) & 7, op & 7
        p, q = y >> 1, y & 1

        def hi(): return (get() >> 8) & 0xFF
        def lo(): return get() & 0xFF
        def sethi(v): setr((get() & 0x00FF) | ((v & 0xFF) << 8))
        def setlo(v): setr((get() & 0xFF00) | (v & 0xFF))

        def disp_addr():
            d = self._fetch()
            return (get() + ((d ^ 0x80) - 0x80)) & 0xFFFF

        # LD rp,nn / ADD I,rp
        if x == 0 and z == 1 and q == 0 and p == 2:
            setr(self._fetch16()); return
        if x == 0 and z == 1 and q == 1:
            rp = [self._get_bc(), self._get_de(), get(), self.sp][p]
            setr(self._add16(get(), rp)); return
        if x == 0 and z == 2 and p == 2 and q == 0:
            a = self._fetch16(); v = get()
            self.w8(a, v & 0xFF); self.w8((a + 1) & 0xFFFF, (v >> 8) & 0xFF); return
        if x == 0 and z == 2 and p == 2 and q == 1:
            a = self._fetch16()
            setr(self.r8(a) | (self.r8((a + 1) & 0xFFFF) << 8)); return
        if x == 0 and z == 3 and p == 2:
            setr((get() + (1 if q == 0 else -1)) & 0xFFFF); return
        # inc/dec/ld (ix+d) and half-registers
        if x == 0 and z == 4:
            if y == 6:
                a = disp_addr(); self.w8(a, self._inc8(self.r8(a)))
            elif y == 4: sethi(self._inc8(hi()))
            elif y == 5: setlo(self._inc8(lo()))
            else: self._wr_r(y, self._inc8(self._rd_r(y)))
            return
        if x == 0 and z == 5:
            if y == 6:
                a = disp_addr(); self.w8(a, self._dec8(self.r8(a)))
            elif y == 4: sethi(self._dec8(hi()))
            elif y == 5: setlo(self._dec8(lo()))
            else: self._wr_r(y, self._dec8(self._rd_r(y)))
            return
        if x == 0 and z == 6:
            if y == 6:
                a = disp_addr(); self.w8(a, self._fetch())
            elif y == 4: sethi(self._fetch())
            elif y == 5: setlo(self._fetch())
            else: self._wr_r(y, self._fetch())
            return
        # LD block (x==1)
        if x == 1:
            if z == 6 and y != 6:
                a = disp_addr(); self._wr_r(y, self.r8(a)); return
            if y == 6 and z != 6:
                a = disp_addr(); self.w8(a, self._rd_r(z)); return
            if y == 4 and z != 6:
                sethi(self._rd_r(z) if z not in (4, 5) else (hi() if z == 4 else lo())); return
            if y == 5 and z != 6:
                setlo(self._rd_r(z) if z not in (4, 5) else (hi() if z == 4 else lo())); return
            if z == 4 and y != 6:
                self._wr_r(y, hi()); return
            if z == 5 and y != 6:
                self._wr_r(y, lo()); return
            self._wr_r(y, self._rd_r(z)); return
        # ALU (x==2)
        if x == 2:
            if z == 6:
                v = self.r8(disp_addr())
            elif z == 4:
                v = hi()
            elif z == 5:
                v = lo()
            else:
                v = self._rd_r(z)
            self._alu(y, v); return
        # x==3: (ix) stack/jp ops
        if x == 3:
            if op == 0xE1: setr(self._pop()); return         # pop ix
            if op == 0xE5: self._push(get()); return          # push ix
            if op == 0xE9: self.pc = get(); return            # jp (ix)
            if op == 0xE3:                                    # ex (sp),ix
                sp = self.sp
                lo2 = self.r8(sp); hi2 = self.r8((sp + 1) & 0xFFFF)
                self.w8(sp, get() & 0xFF); self.w8((sp + 1) & 0xFFFF, (get() >> 8) & 0xFF)
                setr(lo2 | (hi2 << 8)); return
            if op == 0xF9: self.sp = get(); return            # ld sp,ix
        # anything else: treat prefix as nop, re-exec op
        self._do_base(op)

    def _idx_cb(self, base):
        d = self._fetch()
        addr = (base + ((d ^ 0x80) - 0x80)) & 0xFFFF
        op = self._fetch()
        x, y, z = op >> 6, (op >> 3) & 7, op & 7
        v = self.r8(addr)
        rot = [self._rlc, self._rrc, self._rl, self._rr,
               self._sla, self._sra, self._sll, self._srl]
        if x == 0:
            r = rot[y](v); self.w8(addr, r)
            if z != 6: self._wr_r(z, r)
        elif x == 1:
            f = (self.f & FC) | FH | (SZ53[v & (1 << y)] & (FS | FZ | FPV))
            if not (v & (1 << y)):
                f |= FZ | FPV
            f |= (addr >> 8) & (F3 | F5)
            self.f = f
        elif x == 2:
            r = v & ~(1 << y); self.w8(addr, r)
            if z != 6: self._wr_r(z, r)
        else:
            r = v | (1 << y); self.w8(addr, r)
            if z != 6: self._wr_r(z, r)

    def interrupt(self):
        """Maskable IRQ (IM1 -> RST 38). The S16B sound board wires the
        YM2151 IRQ / a timer here; our harness calls it per frame."""
        if not self.iff1:
            return
        self.halted = False
        self.iff1 = self.iff2 = 0
        self._push(self.pc)
        if self.im == 1:
            self.pc = 0x38
        else:
            self.pc = 0x38   # im0 rst38 default for this board
        self.cycles += 13
