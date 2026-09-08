#!/usr/bin/env python3
"""Z80 disassembler — self-contained, no deps. KIT-CORE (2026-09-01).

Built to reverse-engineer the Sega System 16B Z80 sound driver
(epr-11671, shared by English AND Japanese Altered Beast and much of the
S16B library), so the sound pipeline can decode music sequence data
straight from the ROM instead of tapping playback. Pure decode — no CPU
state, no flags — so it cannot be wrong the way an emulator can.

Covers the full documented instruction set plus CB/ED/DD/FD/DDCB/FDCB
prefixes and the common undocumented IX/IY-half and DDCB forms.

  z80dis.py ROM [--org N] [--start N] [--end N] [--labels file]
Emits `ADDR: BYTES   MNEMONIC` lines. --labels reads `NAME=HEXADDR`
lines and annotates jump/call targets and referenced data addresses.
"""
import argparse

# 8-bit registers and register pairs for the regular tables
R = ['b', 'c', 'd', 'e', 'h', 'l', '(hl)', 'a']
RP = ['bc', 'de', 'hl', 'sp']
RP2 = ['bc', 'de', 'hl', 'af']
CC = ['nz', 'z', 'nc', 'c', 'po', 'pe', 'p', 'm']
ALU = ['add a,', 'adc a,', 'sub ', 'sbc a,', 'and ', 'xor ', 'or ', 'cp ']
ROT = ['rlc', 'rrc', 'rl', 'rr', 'sla', 'sra', 'sll', 'srl']


def _hex(n, w=2):
    return f"${n:0{w}X}"


class Dis:
    def __init__(self, data, org=0):
        self.d = data
        self.org = org
        self.targets = set()   # addresses referenced as jump/call/data

    def u8(self, i):
        return self.d[i]

    def s8(self, i):
        v = self.d[i]
        return v - 256 if v >= 128 else v

    def u16(self, i):
        return self.d[i] | (self.d[i + 1] << 8)

    def decode(self, i):
        """-> (length, mnemonic). i is an index into self.d."""
        op = self.d[i]
        if op == 0xCB:
            return self._cb(i)
        if op == 0xED:
            return self._ed(i)
        if op in (0xDD, 0xFD):
            return self._ix(i, 'ix' if op == 0xDD else 'iy')
        return self._base(i, op, R, 'hl', 0)

    # ---- base / DD / FD shared ------------------------------------
    def _base(self, i, op, r, hl, extra):
        """extra = bytes already consumed by an index prefix (0 or 1).
        r/hl are swapped to ix/iy tables under a prefix."""
        x, y, z = op >> 6, (op >> 3) & 7, op & 7
        p, q = y >> 1, y & 1
        n8 = lambda k: self.u8(i + k)
        n16 = lambda k: self.u16(i + k)

        def disp(k):  # (ix+d) operand under prefix
            return self.s8(i + k)

        L = 1 + extra
        if x == 0:
            if z == 0:
                if y == 0: return L, 'nop'
                if y == 1: return L, "ex af,af'"
                if y == 2:
                    d = self.s8(i + L); t = self.org + i + L + 1 + d
                    self.targets.add(t); return L + 1, f'djnz {_hex(t,4)}'
                if y == 3:
                    d = self.s8(i + L); t = self.org + i + L + 1 + d
                    self.targets.add(t); return L + 1, f'jr {_hex(t,4)}'
                d = self.s8(i + L); t = self.org + i + L + 1 + d
                self.targets.add(t); return L + 1, f'jr {CC[y-4]},{_hex(t,4)}'
            if z == 1:
                if q == 0:
                    return L + 2, f'ld {hl if p==2 else RP[p]},{_hex(n16(L),4)}'
                return L, f'add {hl},{hl if p==2 else RP[p]}'
            if z == 2:
                tbl = {0: '(bc),a', 1: 'a,(bc)', 2: '(de),a', 3: 'a,(de)'}
                if p < 2:
                    return L, 'ld ' + tbl[y]
                a = n16(L); self.targets.add(a)
                if p == 2:
                    return L + 2, f'ld ({_hex(a,4)}),{hl}'
                if p == 3 and q == 0:
                    return L + 2, f'ld ({_hex(a,4)}),a'
                if p == 2 and q == 1:
                    return L + 2, f'ld {hl},({_hex(a,4)})'
                return L + 2, f'ld a,({_hex(a,4)})'
            if z == 3:
                return L, ('inc ' if q == 0 else 'dec ') + (hl if p==2 else RP[p])
            if z == 4:
                return self._idx(i, L, r, y, 'inc ')
            if z == 5:
                return self._idx(i, L, r, y, 'dec ')
            if z == 6:
                if r[y] == '(hl)' and hl != 'hl':  # (ix+d),n
                    d = disp(L); n = n8(L + 1)
                    return L + 2, f'ld ({hl}{d:+d}),{_hex(n)}'
                return L + 1, f'ld {r[y]},{_hex(n8(L))}'
            # z==7 rlca/rrca/rla/rra/daa/cpl/scf/ccf
            return L, ['rlca', 'rrca', 'rla', 'rra', 'daa', 'cpl', 'scf', 'ccf'][y]
        if x == 1:
            if z == 6 and y == 6:
                return L, 'halt'
            src, dst = r[z], r[y]
            if hl != 'hl':  # index prefix: (hl)->(ix+d), but H/L stay unless (hl)
                dd = None
                if r[y] == '(hl)' or r[z] == '(hl)':
                    dd = disp(L)
                if r[y] == '(hl)':
                    dst = f'({hl}{dd:+d})'; src = R[z]
                    return L + 1, f'ld {dst},{src}'
                if r[z] == '(hl)':
                    src = f'({hl}{dd:+d})'; dst = R[y]
                    return L + 1, f'ld {dst},{src}'
            return L, f'ld {dst},{src}'
        if x == 2:
            if r[z] == '(hl)' and hl != 'hl':
                d = disp(L); return L + 1, f'{ALU[y]}({hl}{d:+d})'
            return L, f'{ALU[y]}{r[z]}'
        # x == 3
        if z == 0:
            return L, f'ret {CC[y]}'
        if z == 1:
            if q == 0:
                return L, f'pop {RP2[p] if p<3 else "af"}' if p < 4 else L
            return L, ['ret', 'exx', 'jp ' + hl, 'ld sp,' + hl][p]
        if z == 2:
            a = n16(L); self.targets.add(a)
            return L + 2, f'jp {CC[y]},{_hex(a,4)}'
        if z == 3:
            if y == 0:
                a = n16(L); self.targets.add(a); return L + 2, f'jp {_hex(a,4)}'
            if y == 1:
                return L, 'cb-prefix'
            if y == 2:
                return L + 1, f'out ({_hex(n8(L))}),a'
            if y == 3:
                return L + 1, f'in a,({_hex(n8(L))})'
            return L, ['ex (sp),' + hl, 'ex de,hl', 'di', 'ei'][y - 4]
        if z == 4:
            a = n16(L); self.targets.add(a)
            return L + 2, f'call {CC[y]},{_hex(a,4)}'
        if z == 5:
            if q == 0:
                return L, f'push {RP2[p] if p<3 else "af"}'
            if p == 0:
                a = n16(L); self.targets.add(a); return L + 2, f'call {_hex(a,4)}'
            return L, 'prefix'
        if z == 6:
            return L + 1, f'{ALU[y]}{_hex(n8(L))}'
        # z == 7  rst
        t = y * 8; self.targets.add(t)
        return L, f'rst {_hex(t)}'

    def _idx(self, i, L, r, y, mn):
        if r[y] == '(hl)' and r is not R:
            d = self.s8(i + L)
            hl = 'ix' if r[6].startswith('(ix') else 'iy'
            return L + 1, f'{mn}({hl}{d:+d})'
        return L, f'{mn}{r[y]}'

    def _cb(self, i, extra=0, hl='hl', dstr=None):
        op = self.d[i + 1 + extra]
        x, y, z = op >> 6, (op >> 3) & 7, op & 7
        reg = R[z]
        if extra:  # DDCB/FDCB: operand is always (ix+d), d precedes opcode
            d = self.s8(i + 2)
            reg = f'({hl}{d:+d})'
            L = 4
        else:
            L = 2
        if x == 0:
            return L, f'{ROT[y]} {reg}'
        if x == 1:
            return L, f'bit {y},{reg}'
        if x == 2:
            return L, f'res {y},{reg}'
        return L, f'set {y},{reg}'

    def _ed(self, i):
        op = self.d[i + 1]
        x, y, z = op >> 6, (op >> 3) & 7, op & 7
        p, q = y >> 1, y & 1
        if x == 1:
            if z == 0:
                return 2, f'in {R[y] if y!=6 else "f"},(c)'
            if z == 1:
                return 2, f'out (c),{R[y] if y!=6 else "0"}'
            if z == 2:
                return 2, f'{"sbc" if q==0 else "adc"} hl,{RP[p]}'
            if z == 3:
                a = self.u16(i + 2); self.targets.add(a)
                if q == 0:
                    return 4, f'ld ({_hex(a,4)}),{RP[p]}'
                return 4, f'ld {RP[p]},({_hex(a,4)})'
            if z == 4:
                return 2, 'neg'
            if z == 5:
                return 2, 'reti' if y == 1 else 'retn'
            if z == 6:
                return 2, f'im {[0,0,1,2,0,0,1,2][y]}'
            return 2, ['ld i,a', 'ld r,a', 'ld a,i', 'ld a,r', 'rrd', 'rld',
                       'nop', 'nop'][y]
        if x == 2:
            names = {(4, 0): 'ldi', (5, 0): 'ldd', (6, 0): 'ldir', (7, 0): 'lddr',
                     (4, 1): 'cpi', (5, 1): 'cpd', (6, 1): 'cpir', (7, 1): 'cpdr',
                     (4, 2): 'ini', (5, 2): 'ind', (6, 2): 'inir', (7, 2): 'indr',
                     (4, 3): 'outi', (5, 3): 'outd', (6, 3): 'otir', (7, 3): 'otdr'}
            return 2, names.get((y, z), 'nop')
        return 2, 'nop'  # ED with x=0 or 3: no-op on Z80

    def _ix(self, i, xy):
        op = self.d[i + 1]
        r = list(R)
        r[4] = f'{xy}h'; r[5] = f'{xy}l'; r[6] = '(hl)'  # marker, disp handled
        if op == 0xCB:
            return self._cb(i, extra=1, hl=xy)
        if op == 0xED:  # prefix ignored before ED
            return 1, f'{xy}: (ignored prefix)'
        if op in (0xDD, 0xFD):
            return 1, f'{xy}: (redundant prefix)'
        return self._base(i, op, r, xy, 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom')
    ap.add_argument('--org', type=lambda x: int(x, 0), default=0)
    ap.add_argument('--start', type=lambda x: int(x, 0), default=0)
    ap.add_argument('--end', type=lambda x: int(x, 0), default=None)
    ap.add_argument('--labels')
    args = ap.parse_args()
    data = open(args.rom, 'rb').read()
    labels = {}
    if args.labels:
        for ln in open(args.labels):
            ln = ln.split('#')[0].strip()
            if '=' in ln:
                n, v = ln.split('=', 1)
                labels[int(v, 16)] = n.strip()
    dis = Dis(data, args.org)
    end = args.end if args.end is not None else len(data)
    i = args.start
    out = []
    while i < end:
        addr = args.org + i
        try:
            length, mn = dis.decode(i)
        except Exception as e:
            length, mn = 1, f'?? ({e})'
        raw = ' '.join(f'{b:02X}' for b in data[i:i + length])
        lbl = (labels.get(addr, '') + ':') if addr in labels else ''
        out.append(f'{addr:04X}: {raw:<12} {lbl:<10} {mn}')
        i += length
    print('\n'.join(out))


if __name__ == '__main__':
    main()
