#!/usr/bin/env python3
"""The registers each 68K function WRITES, transitively, net of save/restore.
    python3 tools/m68k_regwrites.py --json docs/audit/regwrites.json
    python3 tools/m68k_regwrites.py 31fa 451e        (print a few)

WHY (2026-10-25). docs/audit/extraout_producers.json names the call that produces
every `extraout_*` pseudo-register, but Ghidra creates that pseudo exactly where the
callee's declared return does NOT carry the register. So the value the caller reads
is either (a) the register PASSED THROUGH the callee untouched -- 0x451E leaves A0/A1
alone, so 0x4330's `p_A0 = extraout_A0_00` is its own A0 -- or (b) a value the callee
computed and C never returns (0xE2DC's D1). cport_prep's extraout feed needs to know
which, and only the 68K says: this walks each function's control flow from its entry
(ranges from func_ranges.txt, each disassembled from its own start, never a linear
sweep) and unions the destination registers of every reachable instruction and
callee. A register restored from the stack ((sp)+) is preserved, not written.
Each register maps to the WIDEST write in bytes (1/2/4; an address register is
always 4), because Ghidra names sub-registers: `extraout_D1u` is D1's upper word,
which a byte or word write leaves alone (0xE2DC's `move.b ...,d1`).
'?' = an indirect jump or call the walk cannot follow.
"""
import re, subprocess, json, sys, os
OD = os.path.expanduser('~/src/marsdev/mars/m68k-elf/bin/m68k-elf-objdump')
ROM = 'roms/altbeast/prog68k.bin'
ranges = []
for l in open('docs/audit/func_ranges.txt'):
    if l.startswith('#') or not l.strip(): continue
    p = l.split()
    ranges.append((int(p[0], 16), int(p[1], 16), int(p[2], 16), p[3]))
INS = {}
if True:
    for s, e, ent, nm in ranges:
        out = subprocess.run([OD, '-D', '-b', 'binary', '-m', 'm68k:68000', '--start-address=%d' % s,
                              '--stop-address=%d' % (e + 1), ROM], capture_output=True, text=True).stdout
        for ln in out.split('\n'):
            m = re.match(r'\s*([0-9a-f]+):\t[0-9a-f ]+\t(\S+)\s*(.*)$', ln)
            if m:
                INS[int(m.group(1), 16)] = (m.group(2), m.group(3).strip(), ent)
ADDRS = sorted(INS)
NEXT = {a: ADDRS[i + 1] for i, a in enumerate(ADDRS[:-1])}
REG = re.compile(r'%([ad][0-7]|fp|sp)')
def norm(r): return {'fp': 'A6', 'sp': 'A7'}.get(r, r.upper())
def split_ops(s):
    d = 0; cur = ''; out = []
    for ch in s:
        if ch in '([': d += 1
        if ch in ')]': d -= 1
        if ch == ',' and d == 0:
            out.append(cur); cur = ''
        else: cur += ch
    out.append(cur)
    return [x.strip() for x in out if x.strip()]
def is_branch(mn):
    return mn.startswith('b') and not mn.startswith(('bset', 'bclr', 'bchg', 'btst'))
NOWRITE = ('tst', 'cmp', 'btst', 'jmp', 'jsr', 'rts', 'rte', 'nop', 'pea', 'chk', 'trap', 'stop')
RIX = ['D0', 'D1', 'D2', 'D3', 'D4', 'D5', 'D6', 'D7', 'A0', 'A1', 'A2', 'A3', 'A4', 'A5', 'A6', 'A7']
def rix(r):
    return RIX.index(norm(r))
def opsize(mn):
    """bytes of a DATA register the instruction writes (an address register is always 4)"""
    if mn.startswith(('moveq', 'swap', 'exg', 'lea', 'movem', 'mul', 'div', 'bset', 'bclr', 'bchg')):
        return 4                     # movem.w to registers sign-extends to 32 bits
    if mn.startswith('db'):
        return 2
    if mn.startswith('ext'):
        return 4 if mn.endswith('l') else 2
    if mn in ('abcd', 'sbcd', 'nbcd', 'tas') or \
       re.fullmatch(r's(t|f|hi|ls|cc|cs|ne|eq|vc|vs|pl|mi|ge|lt|gt|le)', mn):
        return 1
    return {'b': 1, 'w': 2, 'l': 4}.get(mn[-1], 4)
def writes_of(mn, ops):
    """{register: bytes written} for one instruction"""
    w = {}
    def put(r, n):
        r = norm(r)
        if r.startswith('A'):
            n = 4
        w[r] = max(w.get(r, 0), n)
    for o in split_ops(ops):
        m = re.search(r'%([ad][0-7]|fp|sp)@[+]', o) or re.search(r'%([ad][0-7]|fp|sp)@-', o)
        if m: put(m.group(1), 4)
    if mn.startswith('db'):
        put(REG.search(ops).group(1), 2); return w
    if mn.startswith(NOWRITE) or is_branch(mn):
        return w
    if mn.startswith('exg'):
        for r in REG.findall(ops): put(r, 4)
        return w
    if mn.startswith('movem'):
        o = split_ops(ops)
        if '%' in o[-1] and '@' not in o[-1]:
            for part in o[-1].split('/'):
                mm = re.fullmatch(r'%([ad][0-7]|fp|sp)-%([ad][0-7]|fp|sp)', part)
                if mm:
                    for k in range(rix(mm.group(1)), rix(mm.group(2)) + 1): put(RIX[k], 4)
                else:
                    mm = REG.search(part)
                    if mm: put(mm.group(1), 4)
        return w
    o = split_ops(ops)
    if not o: return w
    m = re.fullmatch(r'%([ad][0-7]|fp|sp)', o[-1])
    if m: put(m.group(1), opsize(mn))
    return w
def restores_of(mn, ops):
    # `movem.l (sp)+,regs` / `move.x (sp)+,Rn`: the register comes back from a save
    if not re.match(r'\s*%sp@\+,', ops):
        return set()
    return set(writes_of(mn, ops)) - {'A7'}
def merge(w, w2):
    for r, n in w2.items():
        w[r] = max(w.get(r, 0), n)
def target(ops):
    m = re.fullmatch(r'0x([0-9a-f]+)', ops.strip())
    return int(m.group(1), 16) if m else None
MEMO = {}
def regwrites(entry, stack=()):
    if entry in MEMO: return MEMO[entry]
    if entry in stack: return {}
    seen = set(); todo = [entry]; w = {}; unknown = False; rest = set()
    while todo:
        a = todo.pop()
        while a is not None and a not in seen:
            seen.add(a)
            if a not in INS: unknown = True; break
            mn, ops, _ = INS[a]
            merge(w, writes_of(mn, ops))
            rest |= restores_of(mn, ops)
            if mn.startswith(('rts', 'rte', 'rtr')): break
            if mn.startswith(('bsr', 'jsr')):
                t = target(ops)
                if t is None: unknown = True
                else: merge(w, regwrites(t, stack + (entry,)))
                a = NEXT.get(a); continue
            if mn.startswith(('bra', 'jmp')):
                t = target(ops)
                if t is None: unknown = True; break
                a = t; continue
            if is_branch(mn) or mn.startswith('db'):
                t = target(split_ops(ops)[-1])
                if t is not None: todo.append(t)
            a = NEXT.get(a)
    for r in rest:
        w.pop(r, None)
    if unknown: w['?'] = 4
    MEMO[entry] = w
    return w
if __name__ == '__main__':
    if sys.argv[1:2] == ['--json']:
        out = {}
        for s_, e_, ent, nm in ranges:
            k = '%05X' % ent
            if k not in out:
                out[k] = regwrites(ent)
        json.dump(out, open(sys.argv[2], 'w'), indent=0, sort_keys=True)
        print('%s: %d functions' % (sys.argv[2], len(out)))
    else:
        for x in sys.argv[1:]:
            print(x, sorted(regwrites(int(x, 16)).items()))
