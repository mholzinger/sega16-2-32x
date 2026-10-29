#!/usr/bin/env python3
"""GENERATE sh_src/port_selftest.c -- run DECOMPILED 68K code on the SH-2 and
check it against the arcade oracle's own vectors, on the rig.

Three things this has to get right, each learned from a wrong verdict
(LESSONS.md, "Decompiled 68K code verified on the SH-2"):

  * EVERY access is volatile. Casting `volatile byte *` to `uint *` discards
    the qualifier; GCC then cached a cell in a register across the call and the
    comparison read a stale copy while memory held the RIGHT answer.
  * A function is testable only if its WHOLE CALL CLOSURE is present in the
    corpus and hardware-free. Rejecting every function that calls anything cost
    130 of 192 vectored functions; emitting the closure recovers them.
  * WORK RAM IS MIRRORED AS SEPARATE WINDOWS, not one block. The SH-2 region
    guard (_end < 0x06019000, sbuf above it) leaves ~2.4 KB, and the candidates
    touch both the object table (0xFFCxxx) and the globals (0xFFFxxx) -- one
    span covering both is 15,331 B. The touched addresses cluster, so we mirror
    the clusters and nothing between. 21 functions fit in 2,333 B.

EXCLUDED, and why: a function that dereferences a pointer whose VALUE came out
of memory (`piVar3 = _DAT_fffff402; *piVar3 = x`) needs a UNIFORM map -- the
68K address it computes has to be translatable at runtime, and a windowed
mirror cannot do that without rewriting the body, which would end "verbatim as
Ghidra emitted it". Nine functions are held back by this, object_dispatcher
among them. The fix is a runtime translation helper, not a bigger window.
"""
import argparse, collections, json, os, re, sys

ap = argparse.ArgumentParser()
ap.add_argument('--max', type=int, default=64)
ap.add_argument('--out', default='sh_src/port_selftest.c')
ap.add_argument('--corpus', default='docs/proof/altbeast.c')
ap.add_argument('--vectors', default='docs/proof/l1_vectors.jsonl')
ap.add_argument('--budget', type=int, default=2400,
                help='mirror bytes allowed (region-guard headroom)')
ap.add_argument('--port', metavar='ROOTS',
                help='emit the set reachable from these roots (comma-separated names or '
                     '0x addresses) as a runnable unit, instead of a vector sweep')
ap.add_argument('--handlers', default='docs/proof/obj_handlers.md',
                help='observed object handlers; they are ROOTS, because the dispatcher goes '
                     'through a pointer table and no call-graph walk can find them')
ap.add_argument('--window-mirror', dest='full_mirror', action='store_false',
                default=True,
                help='OPT OUT of the contiguous 16 KB .portmirror and window work RAM '
                     'per function instead. The contiguous mirror is the DEFAULT '
                     'because it is what the confirmed ares+FPGA result was measured '
                     'on; windowing drops cases over a 2400 B budget (9 of them at '
                     'last count). Only pass this to fit a rom with no .portmirror.')
ap.add_argument('--gap', type=lambda s: int(s, 0), default=0x200,
                help='addresses further apart than this start a new window')
a = ap.parse_args()

WRAM_LO, STACK_LO = 0xFFC000, 0xFFFE00

_NOTATYPE = {'return', 'goto', 'case', 'break', 'continue', 'else', 'do', 'while',
             'if', 'switch', 'default', 'sizeof'}
_PSUSE = re.compile(r'\b((?:unaff|in|extraout)_[A-Za-z0-9_]+)\b')


def _reg_ty(n):
    """The type a pseudo-register's GLOBAL has. An A-register with NO suffix is an
    ADDRESS (`*(byte *)(unaff_A6 + 0x2f)` is byte arithmetic); with a w/b suffix it is
    the word or byte HALF of that register, which is a VALUE -- typing those as
    pointers and then casting to undefined2 truncated a pointer to 16 bits."""
    m = re.match(r'(?:unaff|in|extraout)_([AD])(\d)([bw]?)$', n)
    if not m:
        return 'uint'
    if m.group(1) == 'A' and not m.group(3):
        return 'byte *'
    return {'b': 'byte', 'w': 'ushort', '': 'ushort'}[m.group(3)]


# THE 68K PROGRAM IS REACHABLE FROM THE SH-2 AND IS PART OF THE INPUT STATE.
# The oracle watches work RAM, so a read of a TABLE IN ROM is never recorded --
# and the entry registers and stored pointers point straight at those tables
# (animate_variant's recorded words at 0xFFC924/26 compose to 0x00010220, the
# animation table). The cart carries the 68K program at offset 0x300000 --
# mars.ld's `.gamehigh 0x02300000 : AT(0x00300000)`, verified by finding
# prog68k.bin's bytes at rom offset 0x310220 for 68K 0x010220 -- so a 68K ROM
# address needs no copy, only an add. Cart and SH-2 are both big-endian, so the
# byte order that defeats the HOST harness is not a problem here.
ROM_HI, GAMEHIGH = 0x040000, 0x02300000
GUARD = 8            # 0xA5 bytes each side of every window, checked after every case

# Ghidra's INFERRED WIDTH per address (tools/ghidra/f1_datwidth.py). NOT optional
# and NOT guessable: with this table missing the loader used to fall back to
# 1 byte for every symbol, which declared the 32-bit LCG seed _DAT_fffff014 as a
# `byte`, incremented the wrong half of every word, and reported 23 cases as
# genuine mismatches when the ports were fine. A missing input must stop the
# generator, never quietly change the meaning of every declaration.
#
# The ORACLE's observed widths are NOT a substitute: they are ACCESS widths. The
# oracle writes the 32-bit seed as two word halves, so it reports width 2 for a
# cell whose symbol is 4 bytes.
try:
    DATW = json.load(open('docs/proof/datwidth.json'))
except OSError:
    raise SystemExit('gen_selftest: docs/proof/datwidth.json missing -- regenerate it with\n'
                     '  tools/ghidra_run.sh tools/ghidra/f1_datwidth.py\n'
                     'Refusing to guess widths: the 1-byte default silently declares '
                     'every symbol wrong.')

src = open(a.corpus).read()

_SSYM = re.compile(r'\bs_[^\s,;()\[\]{}]*?_([0-9a-f]{4,8})\b')
_ssyms = {}


def _sanitise_s(m):
    clean = re.sub(r'[^A-Za-z0-9_]', '_', m.group(0))
    _ssyms[clean] = int(m.group(1), 16)
    return clean


src = _SSYM.sub(_sanitise_s, src)

# Ghidra's sub-field syntax: `X._0_2_` is "the 2 bytes at offset 0 of X". cport_prep.py
# rewrites these to a cast; without it they read as struct member access.
def _subfield(m):
    var, off, wid = m.group(1), int(m.group(2)), int(m.group(3))
    ty = {1: 'byte', 2: 'ushort', 4: 'uint'}.get(wid, 'ushort')
    return '(*(%s *)((char *)&%s + %d))' % (ty, var, off)


src = re.sub(r'\b([A-Za-z_]\w*)\._(\d+)_(\d+)_', _subfield, src)
# a `(code)` cast is a byte in this program, and a FUN_ name indexed is a byte base
src = src.replace('(code)', '(byte)')
src = re.sub(r'\b(FUN_[0-9a-f]+)\s*\[', r'((byte *)\1)[', src)


def _fix_code_ptr(block):
    for var in set(re.findall(r'\bcode\s*\*\s*(\w+)\s*;', block)):
        if (re.search(r'\b%s\s*\[' % re.escape(var), block)
                and not re.search(r'\(\s*\*\s*%s\s*\)\s*\(' % re.escape(var), block)):
            block = re.sub(r'\bcode(\s*\*\s*%s\s*;)' % re.escape(var), r'byte\1', block)
    return block


src = ''.join(_fix_code_ptr(b) for b in re.split(r'(?=/\* ---- 0x)', src))

if _ssyms:
    print('  sanitised %d Ghidra string symbol name(s)' % len(_ssyms))

blocks = {}
for b in re.split(r'(?=/\* ---- 0x)', src):
    m = re.match(r'/\* ---- 0x([0-9A-F]+)\s+(\S+)\s+\((\d+) B\)', b)
    if m:
        blocks[int(m.group(1), 16)] = (m.group(2), int(m.group(3)), b)
name2addr = {v[0]: k for k, v in blocks.items()}

vec = collections.defaultdict(list)
recs_all = []
for l in open(a.vectors):
    d = json.loads(l)
    recs_all.append(d)
    vec[int(d['callee'], 16)].append(d)

# The (?:_\d+)? is Ghidra's overlap disambiguator (DAT_fffff032_1). FOURTH site today to
# need it -- cport_prep, cport_layout and the self-test's other patterns were the first three.
SYM = re.compile(r'\b(_?(?:DAT|[a-z]{1,3}Ram)_?([0-9a-f]{4,8})(?:_\d+)?)\b')
RT_PATH = 'docs/proof/cport/ghidra_rt.h'
try:
    _rt = open(RT_PATH).read()
except OSError:
    raise SystemExit('gen_selftest: %s missing -- regenerate it with\n'
                     '  python3 tools/cport_prep.py docs/proof/altbeast.c docs/proof/cport\n'
                     'Refusing to re-derive the decompiler intrinsics: a subtly wrong '
                     'CARRY1 or SUB42 makes a CORRECT function report FAIL.' % RT_PATH)
# the macro bodies, guards and #include stripped -- inlined into the generated file
RT_BODY = [l for l in _rt.split('\n')
           if l.startswith('#define') and not l.startswith('#define GHIDRA_RT_H')]
RT_NAMES = {l.split()[1].split('(')[0] for l in RT_BODY}

# defs.c stubs these to `return 0`, which is NOT their semantics. A function whose
# closure reaches one cannot be verified: the answer would be the stub's, not the
# game's. Reject with a reason rather than passing on a fiction.
STUBS = {'halt_baddata', 'halt_unimplemented', 'stop', 'bcdAdjust'}

KW = {'if', 'while', 'for', 'return', 'short', 'ushort', 'uint', 'byte', 'switch',
      'sizeof', 'undefined2', 'undefined4', 'do', 'else', 'goto', 'undefined',
      'uint3', 'int', 'char', 'long', 'unsigned', 'signed', 'void', 'volatile'}
# a pointer whose VALUE came out of memory -- needs a uniform map, see the docstring
# THE SIGN-EXTENDED 16-BIT WORK-RAM POINTER IDIOM CANNOT BE SEEDED, on any host or
# target. The 68K stores only the LOW 16 BITS of a work-RAM address and recovers
# 0xFFxxxx by sign extension: FUN_0000e20c does
# `iVar2 = (int)*(short *)(unaff_A6 + 0x3c)` with 0xC880 in that cell, meaning 68K
# 0xFFC880. For the body's deref to land in the mirror the cell would have to hold
# the mirror's address -- but it is 16 bits wide, and sign extension can only ever
# produce 0x0000xxxx or 0xFFFFxxxx, never the 0x0600xxxx of SDRAM. There is no value
# to seed. These belong to the L2 whole-program differential, where work RAM is
# wherever the port puts it and pointers are consistent by construction.
SIGNEXT_PTR = re.compile(r'=\s*\(int\)\s*\*\s*\(\s*short\s*\*\s*\)\s*\(')
PTRFROMMEM = re.compile(r'\*\s*\(\s*\w+\s*\*\s*\)\s*\(?\s*\w*Var\d+\b')


_IDENT = re.compile(r'\b([A-Za-z_]\w*)\b')


def callees(ad):
    nm, _, body = blocks[ad]
    called = {c for c in re.findall(r'\b([A-Za-z_][A-Za-z0-9_]{2,})\s*\(', body)
              if c not in KW and c != nm}
    # a function used as a VALUE (a handler pointer) has no parentheses
    referenced = (set(_IDENT.findall(body)) & set(name2addr)) - {nm}
    return (called | referenced) - RT_NAMES


def closure(ad):
    seen, miss, st = {ad}, set(), [ad]
    while st:
        x = st.pop()
        for c in callees(x):
            t = name2addr.get(c)
            if t is None:
                miss.add(c); continue
            if t not in seen:
                seen.add(t); st.append(t)
    return seen, miss


def nonstack(v):
    return [w for w in (v.get('writes') or []) if int(w[0], 16) < STACK_LO]


def sym_addr(nm2):
    m = re.search(r'([0-9a-fA-F]{4,8})$', nm2)
    if not m:
        return None
    v = int(m.group(1), 16)
    return (0xFF0000 | (v & 0xFFFF)) if 0xFFFF0000 <= v <= 0xFFFFFFFF else v


def wid(addr):
    w = DATW.get('%08X' % (0xFFFF0000 | (addr & 0xFFFF)), 0)
    return w if w in (1, 2, 4) else 0


UNKNOWN_W = set()


_BASEOFF = re.compile(r'\b(?:unaff|in|extraout)_A(\d)\s*\+\s*(0x[0-9a-fA-F]+|\d+)')
_BASEW = re.compile(r'\*\s*\(\s*(\w+)\s*\*\s*\)\s*\(\s*(?:unaff|in|extraout)_A(\d)'
                    r'\s*\+\s*(0x[0-9a-fA-F]+|\d+)\s*\)')
_TYW = {'int': 4, 'uint': 4, 'undefined4': 4, 'ulong': 4, 'long': 4, 'code': 4,
        'short': 2, 'ushort': 2, 'undefined2': 2, 'word': 2,
        'char': 1, 'byte': 1, 'undefined': 1, 'undefined1': 1, 'uchar': 1}


def based_widths(txt, ent):
    """address -> the width the BODY accesses it at, for cells off a base register."""
    out, a = {}, (ent.get('a') or [0] * 8)
    for ty, rg, offs in _BASEW.findall(txt):
        w = _TYW.get(ty)
        if not w:
            continue
        ad = (a[int(rg)] & 0xFFFFFF) + int(offs, 16 if offs.startswith('0x') else 10)
        out[ad] = max(out.get(ad, 0), w)
    return out


def _maxoff(txt, reg, floor=0x4F):
    """The largest constant offset the closure indexes off A<reg>, plus its width.

    Guessing a record size under-sized the window and gcc turned the provable
    out-of-bounds access into a call to abort(), which failed the link (and, when
    it was stubbed instead, produced a rom that died on the FPGA with no barcode).
    """
    hi = floor
    for rg, off in _BASEOFF.findall(txt):
        if rg == str(reg):
            hi = max(hi, int(off, 16 if off.startswith('0x') else 10) + 4)
    return hi


# 68K ABSOLUTE-SHORT ADDRESSING, RENDERED BY GHIDRA AS A NEGATIVE CONSTANT.
# `move.b #$FF,($ECC0).w` sign-extends its 16-bit displacement to 0xFFFFECC0, i.e.
# work RAM 0xFFECC0, and Ghidra writes that as `*(undefined *)(iVar1 + -0x1340)` --
# because 0x10000 - 0x1340 = 0xECC0. The address is a real work-RAM address wearing
# a negative constant, so it must go through the mirror like any other; left alone it
# dereferences 0xFFFFECC0 on the SH-2, which is the on-chip register area.
#
# FUN_00003cb6 shows both styles in one body: `(&DAT_ffffec80)[iVar1] = 0xff` (a
# named symbol -- already mirrored, and it worked) beside three of these (which did
# not). This is a MECHANICAL rewrite of the addressing mode, in the same family as
# stripping a shadowing declaration: it changes how the address is formed, never
# what the code computes, and it is applied only when the sign-extended value lands
# inside work RAM.
# Matches ANY pointer cast of an absolute-short address, not only the immediately
# dereferenced form. release_palette_slot computes the address into a variable first:
# `pcVar1 = (char *)((short)(ushort)bVar2 + -0xbc0)` with bVar2 = 0x1B gives
# 0x1B - 0xBC0 = 0xF45B, i.e. work RAM 0xFFF45B. The earlier regex required a leading
# `*` and so saw none of these. A cast without `*` is left intact by keeping the
# replacement a cast too, so `*(T *)(...)` still works with its `*` outside.
_ABSSHORT = re.compile(r'\(\s*(\w+)\s*\*\s*\)\s*(?:\(\s*\w+\s*\)\s*)*'
                       r'\(\s*([^()]*(?:\([^()]*\)[^()]*)*?)\s*\+\s*-0x([0-9a-fA-F]+)\s*\)')


_SX16 = re.compile(r'\(int\)\s*(\*\s*\(\s*short\s*\*\s*\)\s*\((?:[^()]|\([^()]*\))*\))'
                   r'(?!\s*\+\s*[A-Za-z_])')


def _sx16(txt):
    """`(int)*(short *)(X)` -> `(int)W68(*(short *)(X))`: a 16-bit work-RAM address
    translated at the point of use. See the W68 note in the emitted prologue."""
    return _SX16.subn(lambda m: m.group(0) if 'register0x' in m.group(1)
                      else '(int)W68(%s)' % m.group(1), txt)


def _absshort(txt):
    n = [0]

    def rep(m):
        ty, expr, disp = m.group(1), m.group(2).strip(), int(m.group(3), 16)
        if not (0 < disp <= 0x8000):
            return m.group(0)
        # GHIDRA NAMES POINTERS p<type>Var<N> AND INTEGERS <type>Var<N>. An expression
        # based on a pointer is ordinary pointer arithmetic -- `*(T *)(puVar8 + -0x1340)`
        # walks backwards through a block that ALREADY points into work RAM -- and must be
        # left alone; rewriting it produced 30 "invalid operands to binary +" errors,
        # char * + byte *. Absolute-short addressing forms its address from an INTEGER.
        # A BARE register term is the address BASE; a SUBSCRIPTED or DEREFERENCED one is a
        # VALUE being read. `(unaff_A6 + -0x78)` is the 68K's (-$78,A6), a displacement --
        # exclude it. `((ushort)unaff_A6[8] * 0x10 + -0x800)` reads a byte to compute an
        # INDEX and the address itself is an integer expression -- that IS absolute-short,
        # and the broad guard was rejecting it (FUN_0001828e, whose store then went to the
        # SH-2's on-chip register area).
        _bare = re.compile(r'\b(?:p[a-z]Var\d+|(?:unaff|in|extraout)_A\d\w*|register0x[0-9a-fA-F]+)'
                           r'(?!\s*\[)')
        if any(not re.match(r'\s*\[', expr[m.end():]) and
               not expr[:m.start()].rstrip().endswith('*')
               for m in _bare.finditer(expr)):
            return m.group(0)

        a68 = 0xFF0000 | ((0x10000 - disp) & 0xFFFF)
        if not (WRAM_LO <= a68 < STACK_LO):
            return m.group(0)
        n[0] += 1
        return '(%s *)(WP(0x%06Xu) + (%s))' % (ty, a68, expr)

    return _ABSSHORT.sub(rep, txt), n[0]


def cells(vs, txt):
    """every work-RAM address the closure names, the vectors touch, or an entry
    A-register points at. The A-registers matter: they are the object-record base
    pointers the body indexes off, and a case run with an unmapped base writes
    through a bogus pointer -- measured, it tripped the window guards and 32 cases
    reported FAIL for a reason that had nothing to do with the decompiled code."""
    t = {int(h, 16) & 0xFFFFFF for _, h in SYM.findall(txt)}
    nA = len(set(re.findall(r'\b(?:unaff|in)_A(\d)\b', txt)))
    for v in vs:
        for k in ('reads', 'writes'):
            for e in (v.get(k) or []):
                x = int(e[0], 16) & 0xFFFFFF
                if WRAM_LO <= x < STACK_LO:
                    t.add(x)
        for k in ('reads', 'writes'):
            for e in (v.get(k) or []):
                w = e[2] if len(e) > 2 else 2
                val = e[1] & 0xFFFFFFFF
                if w == 4 and WRAM_LO <= (val & 0xFFFFFF) < STACK_LO:
                    t.add(val & 0xFFFFFF)                    # a pointer target ...
                    t.add(min((val & 0xFFFFFF) + 0x4F, STACK_LO - 1))   # ... and its record
        if nA:
            for r in set(re.findall(r'\b(?:unaff|in)_A(\d)\b', txt)):
                av = v['entry']['a'][int(r)] & 0xFFFFFF
                if WRAM_LO <= av < STACK_LO:
                    t.add(av)
                    t.add(min(av + _maxoff(txt, r), STACK_LO - 1))
    return {x for x in t if WRAM_LO <= x < STACK_LO}


cands, rej = [], collections.Counter()
if a.port:
    _roots = set()
    for _r in a.port.split(','):
        _r = _r.strip()
        if _r in name2addr:
            _roots.add(name2addr[_r])
        elif _r.startswith('0x') and int(_r, 16) in blocks:
            _roots.add(int(_r, 16))
    if a.handlers and os.path.exists(a.handlers):
        for _h in re.findall(r'0x([0-9A-Fa-f]{4,6})', open(a.handlers).read()):
            if int(_h, 16) in blocks:
                _roots.add(int(_h, 16))
    _seen, _st = set(_roots), list(_roots)
    while _st:
        _x = _st.pop()
        for _c in callees(_x):
            _t = name2addr.get(_c)
            if _t is not None and _t not in _seen:
                _seen.add(_t); _st.append(_t)
    for _adr in sorted(_seen):
        _nm, _sz, _bd = blocks[_adr]
        _txt = ''.join(blocks[x][2] for x in closure(_adr)[0])
        # NO VECTORS in port mode: this unit is the game, not a test. Clearing them here
        # rather than later matters, because the case list is built from the candidates and
        # emptying `vec` afterwards changes nothing -- 203 cases of scaffolding put 460 KB
        # into a 135 KB port.
        cands.append([_adr, _nm, closure(_adr)[0], [],
                      cells([], _txt), _txt])
    print('port mode: %d roots -> %d functions reachable' % (len(_roots), len(_seen)))
for addr, vs in ({} if a.port else vec).items():
    if addr not in blocks:
        rej['no decompiled body'] += 1; continue
    cl, miss = closure(addr)
    if miss:
        rej['call closure incomplete'] += 1; continue
    txt = ''.join(blocks[x][2] for x in cl)
    # hardware symbols are absorbed by a stub (see the note above), not a rejection
    if not sum(len(nonstack(v)) for v in vs):
        rej['no constraining write'] += 1; continue
    # A pointer READ FROM MEMORY is no longer a rejection. Its value is a 68K
    # address, which is not a valid SH-2 address either -- but an SH-2 pointer is
    # 32 bits, so the value FITS the cell it came out of and can simply be
    # rebased onto the mirror when the vector is seeded. That is impossible on a
    # 64-bit host, which is why 42% of the L1 corpus is host-unverifiable and why
    # this generator is the instrument that has to reach it (LESSONS, "A THIRD of
    # the corpus is target-only"). Only 4-byte cells are rebased: a 2-byte cell
    # cannot hold a work-RAM address, so there is no ambiguity with data.
    # PTR_FUN_* is a CODE-pointer table: the body dispatches through other code,
    # so mirroring work RAM is not enough to run it.
    if re.search(r'\bPTR_[A-Za-z]', txt):
        rej['dispatches through a code-pointer table'] += 1; continue
    if re.search(r'\(\s*\*\s*\*?\s*\(\s*code\s*\*', txt):
        rej['indirect call through a memory pointer (no SH-2 target exists)'] += 1
        continue
    _t2 = re.sub(r'/\*.*?\*/', ' ', txt, flags=re.S)
    if any(re.search(r'\bvoid\s+%s\s*\(' % re.escape(_c), _t2)
           and re.search(r'=\s*%s\s*\(' % re.escape(_c), _t2)
           for _c in {blocks[x][0] for x in cl}):
        rej["callee typed void but its result is used (return not modelled)"] += 1
        continue
    if set(_IDENT.findall(txt)) & STUBS:
        rej['closure reaches a decompiler stub (halt_baddata/bcdAdjust)'] += 1; continue

    if re.search(r'\bextraout_[AD]\d', txt):
        # blocks[addr][1], NOT a bare `sz`: there is no `sz` in this loop, so the
        # name resolved to a stale global from an earlier one and the range was
        # wrong -- the check looked present and did nothing.
        _rg = [(x, x + blocks[x][1] + 8) for x in cl]
        _in = [q for q in recs_all
               if any(lo5 <= int(q['caller'], 16) < hi5 for lo5, hi5 in _rg)]
        _ex = {(tuple((q['exit']['a'] or [0] * 8)[:8]),
                tuple((q['exit']['d'] or [0] * 8)[:8])) for q in _in}
        if len(_ex) > 1:
            rej['extraout value cannot be paired to a vector '
                '(capture has no call sequence id)'] += 1
            continue
    cands.append([addr, blocks[addr][0], cl, vs, cells(vs, txt), txt])

# smallest footprint first, so the mirror budget buys the most functions
cands.sort(key=lambda c: (len(c[4]), sum(blocks[x][1] for x in c[2])))


WIN_PAD = 0x10


def windows(t, gap):
    s = sorted(t); out = [[s[0], s[0]]]
    for x in s[1:]:
        if x - out[-1][1] <= gap: out[-1][1] = x
        else: out.append([x, x])
    for wnd in out:                       # see WIN_PAD note above
        wnd[1] = min(wnd[1] + WIN_PAD, STACK_LO - 1)
    return out


chosen, touched = [], set()
if a.full_mirror:
    # ALL of work RAM, so nothing is rejected for footprint and every address is
    # reachable at a single constant offset.
    for c in cands[:a.max]:
        chosen.append(c); touched |= c[4]
    cands = chosen
    WINS = [[WRAM_LO, STACK_LO - 1]]
else:
    for c in cands[:a.max]:
        cand = touched | c[4]
        cost = sum(hi - lo + 1 for lo, hi in windows(cand, a.gap)) if cand else 0
        if cost <= a.budget:
            chosen.append(c); touched = cand
        else:
            rej['over the %dB mirror budget' % a.budget] += 1
    cands = chosen
    WINS = windows(touched, a.gap) if touched else []

# every absolute symbol the chosen closures name, with its width
syms, fnset = {}, set()
for addr, nm, cl, vs, t, txt in cands:
    fnset |= cl
    for nm2, hexa in SYM.findall(txt):
        v = int(hexa, 16)
        if 0xFFFF0000 <= v <= 0xFFFFFFFF:
            w = wid(v)
            if not w:
                UNKNOWN_W.add(nm2)
            syms[nm2] = (0xFF0000 | (v & 0xFFFF), w or 1)

O = []
A = O.append
A('/* GENERATED by tools/gen_selftest.py -- do not edit.')
A(' * Remake with:  python3 %s' % ' '.join(sys.argv))
A(' * Decompiled 68K functions running on the SH-2, checked against the arcade')
A(" * oracle's own vectors (mame altbeast, via the L1 harness). Bodies are")
A(' * EXACTLY as Ghidra emitted them; only the scaffolding below is generated.')
A(' *')
A(' * Verdict: ares-headless --dump at port_test_out, or the rig barcode --')
A(' * byte 5 = (run << 4) | passed, tools/rig_barcode.py. port_test_out[3] is a')
A(' * heartbeat, [4] is nonzero if a window GUARD was overwritten, and [6] is a')
A(' * WATCHDOG holding the case currently entered. Read [6] as "hung in case N" ONLY')
A(' * when [8] (completed sweeps) is 0 -- otherwise the sweep simply had not finished')
A(' * when the dump was taken, since it re-runs every publish.')
A(' */')
A('#include "mars.h"')
A('#ifdef PORT_TEST')
A('/* The 68K punned pointers and integers freely and GCC 15 makes that an error.')
A(' * Scoped to THIS file so the rom build keeps its own strictness. */')
A('/* .porttest: the generated bodies live in the free ROM gap above .tilesmd, not')
A(' * in the main .text image. Growing .text pushed the .ramtext ROM image into')
A(' * the .tilesmd blob and capped this test at 13 functions (mars.ld says so in')
A(' * words). Executing from cart is slower and irrelevant here. */')
A('#define PORTTEST_SEC __attribute__((section(".porttest")))')
A('#pragma GCC diagnostic ignored "-Wint-conversion"')
A('#pragma GCC diagnostic ignored "-Wincompatible-pointer-types"')
A('typedef unsigned char  byte;')
A('typedef unsigned short ushort, undefined2;')
A('typedef unsigned int   uint, undefined4;')
A('typedef unsigned char  undefined;')
A('typedef unsigned long long undefined8, qword, ulonglong;')
A('typedef long long           longlong;')
A('/* Ghidra emits `bool`, `true`, `false`; freestanding C99 has no stdbool.h.')
A(' * Same definitions as docs/proof/cport/ghidra_types.h -- do not invent new ones. */')
A('typedef int bool;')
A('/* Ghidra emits `code` for an instruction stream; it only ever appears under a')
A(' * pointer or is called, so it is a FUNCTION type, not a byte. Copied from')
A(' * docs/proof/cport/ghidra_types.h rather than invented -- `unsigned char` made')
A(' * every indirect call "called object is not a function". */')
A('typedef uint code();')
A('typedef unsigned int  undefined3, int3, uint3;   /* Ghidra 3-byte types */')
A('typedef unsigned char  undefined1;')
A('typedef unsigned char BADSPACEBASE;   /* Ghidra: unresolved address space */')
A('typedef unsigned short undefined6;')      # exactly as ghidra_types.h has it: a call TARGET
A('#define true 1')
A('#define false 0')
A('')
A('/* Ghidra intrinsics, verbatim from %s (generated by cport_prep.py).' % RT_PATH)
A(' * Inlined rather than re-derived: a subtly wrong CARRY1 or SUB42 would make a')
A(' * CORRECT function report FAIL. */')
for _l in RT_BODY:
    A(_l)
A('')
A('/* WORK RAM MIRRORED AS %d WINDOWS. Not one block: the candidates touch the' % len(WINS))
A(' * object table AND the globals, and one span covering both is 15,331 B against')
A(" * ~2.4 KB of region-guard headroom (sbuf owns 0x06019000+). The touched")
A(' * addresses cluster, so we mirror the clusters and nothing between.')
A(' *')
A(' * volatile on the array AND on every access through WP(): casting')
A(' * `volatile byte *` to `uint *` discards the qualifier, and GCC then cached a')
A(' * cell in a register across the call -- the comparison read a stale copy while')
A(' * memory held the right answer (wram held 0xC6A301FA, exactly the oracle')
A(' * value, while the test said FAIL).')
A(' *')
A(' * Each window carries %d guard bytes of 0xA5 on each side. A body that indexes' % GUARD)
A(' * off the end of a record writes into a guard instead of another window, and')
A(' * port_test_out[4] reports it -- so a silently wrong mirror cannot read as a')
A(' * pass. */')
off, decl = {}, []
pos = 0
for i, (lo, hi) in enumerate(WINS):
    pos += GUARD
    lo &= ~3                     # base aligned down ...
    pos = (pos + 3) & ~3         # ... and offset aligned up, so offset == addr (mod 4)
    off[i] = (lo, hi, pos)
    decl.append('    0x%06X-0x%06X -> wram[0x%04X..0x%04X]  %d B'
                % (lo, hi, pos, pos + (hi - lo), hi - lo + 1))
    pos += (hi - lo + 1)
pos += GUARD
TOT = (pos + 3) & ~3
for d in decl:
    A('/*' + d + ' */')
A('static volatile byte wram[0x%X] __attribute__((aligned(16)%s));'
  % (TOT, ', section(".portmirror")' if a.full_mirror else ''))
A('')
A('/* Compile-time for a literal address (every generated access), correct at')
A(' * runtime for an indexed one. An address in NO window maps to the trap cell,')
A(' * so a stray access is contained and visible rather than silent. */')
A('#define WTRAP 0u')
terms = ' : '.join('(a) >= 0x%06Xu && (a) <= 0x%06Xu ? %du + ((a) - 0x%06Xu)'
                   % (lo, hi, p, lo) for lo, hi, p in off.values())
A('#define WOFF(a) (%s : WTRAP)' % terms)
A('#define WP(a)   (wram + WOFF(a))')
A('')
A('/* A 68K address held as 16 BITS, which the 68000 sign-extended to 0xFFxxxx by its')
A(' * addressing mode. The port does not have to preserve that encoding -- only the')
A(' * cell it names -- so translate at the point of use. Mike, 2026-09-27: the C port')
A(' * is a different toolset from a 68K conversion. */')
A('/* THE SIGN DECIDES THE REGION. The 68000 SIGN-EXTENDS a 16-bit absolute')
A(' * address: 0xC880 -> 0xFFFFC880 = work RAM 0xFFC880, but 0x0100 -> 0x00000100,')
A(' * which is PROGRAM ROM. An unconditional 0xFF0000|v maps every positive short')
A(' * into work RAM and is wrong for them. The cart holds the 68K program at')
A(' * GAMEHIGH, so a positive value resolves there. */')
A('#define W68(v)  ((short)(v) < 0 ? (char *)(wram + WOFF(0xFF0000u | ((v) & 0xFFFFu)))'
  ' : (char *)(0x%08Xu + ((v) & 0xFFFFu)))' % GAMEHIGH)
A('')
# An address carrying BOTH forms anywhere in the CORPUS -- not just in this
# closure. FUN_000171fa uses only `DAT_ffffe800`, while `_DAT_ffffe800` appears in
# other functions, so a per-closure check sees no pair and learns nothing.
_allsyms = {m[0] if isinstance(m, tuple) else m for m in SYM.findall(src)}
_both = {sym_addr(n1) for n1 in _allsyms
         if n1.startswith('_') and n1[1:] in _allsyms and sym_addr(n1) is not None}
_orw = {}
for _addr2, _vs2 in vec.items():
    for _v2 in _vs2:
        for _k2 in ('reads', 'writes'):
            for _e2 in (_v2.get(_k2) or []):
                _a2 = int(_e2[0], 16) & 0xFFFFFF
                _w2 = _e2[2] if len(_e2) > 2 else 2
                _orw[_a2] = min(_orw.get(_a2, 4), _w2)   # the NARROWEST access seen
# `emitted` does not exist yet here; the closure text is on the candidates.
_clostxt = ''.join(c[5] for c in cands)
_arraybase = {n3 for n3 in syms
              if re.search(r'&\s*' + re.escape(n3) + r'\s*\)\s*\[', _clostxt)}
# out-of-work-RAM symbols: each gets a slot in an absorbing stub block
HWSYMS, ROMSYMS = {}, {}
for _addr4, _nm4, _cl4, _vs4, _t4, _txt4 in cands:
    for _n5, _h5 in SYM.findall(_txt4):
        _v5 = int(_h5, 16)
        if 0xFFFF0000 <= _v5 <= 0xFFFFFFFF:
            continue                       # work RAM: the mirror handles it
        if _v5 < ROM_HI:
            ROMSYMS[_n5] = _v5             # a constant table in the program ROM
        else:
            HWSYMS.setdefault(_n5, len(HWSYMS) * 4)
for _sn, _sa in sorted(_ssyms.items(), key=lambda kv: kv[1]):
    if _sa < ROM_HI:
        ROMSYMS.setdefault(_sn, _sa)
if ROMSYMS:
    A('/* CONSTANT DATA IN THE 68K PROGRAM ROM, which the cart carries at GAMEHIGH, so these')
    A(' * resolve to the REAL bytes rather than a stub -- they are tables the game reads.')
    A(' * Stubbing them to 0 regressed release_palette_slot from 4/4 to 2/4. */')
    for _n6, _a6 in sorted(ROMSYMS.items(), key=lambda kv: kv[1]):
        if _n6.startswith('s_'):
            # a STRING symbol is an ARRAY -- the bodies subscript it (`s_X[i]`), so it must be
            # a pointer. A DAT_ base is subscripted through `&sym + n` instead and stays a
            # dereference, which is what makes `&` give a byte offset.
            A('#define %-18s ((volatile byte *)0x%08Xu)   /* 68K 0x%06X: string, an ARRAY */'
              % (_n6, GAMEHIGH + _a6, _a6))
        else:
            A('#define %-18s (*(volatile byte *)0x%08Xu)   /* 68K 0x%06X: byte base, so'
              ' `&sym + n` is a BYTE offset as the 68K meant it */'
              % (_n6, GAMEHIGH + _a6, _a6))
    A('')
if HWSYMS:
    A('/* HARDWARE STUB. These symbols are outside 68K work RAM -- the S16 palette at')
    A(' * 0x840000, video and I/O registers. The stub ABSORBS writes so the function can')
    A(' * run; it does NOT model the hardware, and nothing here is verified against it.')
    A(' * That is sound because every oracle expectation is a work-RAM cell, so a hardware')
    A(' * write is unobserved either way -- and a function must still have constraining')
    A(' * work-RAM writes to be scored at all. */')
    A('static volatile byte hwstub[0x%X];' % (len(HWSYMS) * 4 + 4))
    for _n5, _o5 in sorted(HWSYMS.items(), key=lambda kv: kv[1]):
        A('#define %-18s (*(volatile uint *)(hwstub + %d))' % (_n5, _o5))
    A('')
for nm2, (addr, w) in sorted(syms.items()):
    if nm2 in _arraybase and addr in _orw:
        w = _orw[addr]             # element size of the table it is the base of
    if addr in _both and not nm2.startswith('_'):
        w = 1                      # the smaller of an overlapping pair
    ty = {1: 'byte', 2: 'ushort', 4: 'uint'}[w]
    A('#define %-18s (*(volatile %s *)WP(0x%06Xu))   /* 68K 0x%06X, %d B */'
      % (nm2, ty, addr, addr, w))
A('')
A('static PORTTEST_SEC void wram_zero(uint off, uint n)   /* clear one case\'s span */')
A('{')
A('    uint i;')
A('    for (i = 0; i < n; i++) wram[off + i] = 0;')
A('}')
A('')
A('static PORTTEST_SEC void wram_arm(void)      /* zero the mirror, lay the guards */')
A('{')
A('    uint i;')
A('    for (i = 0; i < sizeof wram; i++) wram[i] = 0;')
for lo, hi, p in off.values():
    A('    for (i = 0; i < %du; i++) { wram[%du + i] = 0xA5u; wram[%du + i] = 0xA5u; }'
      % (GUARD, p - GUARD, p + (hi - lo) + 1))
A('}')
A('')
A('/* 0 = every guard intact; otherwise the 1-BASED INDEX of the window that was')
A(' * overrun, so a mirror artefact names the window to widen. */')
A('static PORTTEST_SEC uint wram_guards_ok(void)')
A('{')
A('    uint i;')
for _wi, (lo, hi, p) in enumerate(off.values(), 1):
    A('    for (i = 0; i < %du; i++) if (wram[%du + i] != 0xA5u || wram[%du + i] != 0xA5u) '
      'return %du;   /* 0x%06X-0x%06X */'
      % (GUARD, p - GUARD, p + (hi - lo) + 1, _wi, lo, hi))
A('    return 0u;')
A('}')
A('')
# LAB_ code labels used as VALUES, and Ghidra's own stubs. In PORT mode the judge is the L2
# frame differential rather than a per-function verdict, so a stub that lets the unit RUN is
# the right trade -- in the self-test the same stub would fake a pass, which is why it is
# rejected there.
# ONLY the LAB_ names used as VALUES. Ghidra also emits `LAB_x:` as a real goto label, and
# #defining one of those to a number breaks the label and cascades -- it took the error count
# from 21 to 670. A name followed by a colon is a label; a name that is not is a value.
_labvals = set()
for _c9 in cands:
    _labvals |= set(re.findall(r'\b(LAB_[0-9a-fA-F][0-9a-fA-F_]*)\b(?!\s*:)', _c9[5]))
    _labvals -= set(re.findall(r'\b(LAB_[0-9a-fA-F][0-9a-fA-F_]*)\s*:', _c9[5]))
for _lb in sorted(_labvals):
    _la = int(re.search(r'([0-9a-fA-F]+)$', _lb).group(1), 16)
    # ADDRESSABLE, not a constant: the bodies take `&LAB_x`, and `&` of a number is not an
    # lvalue. As a dereference at the address both `LAB_x` and `&LAB_x` are valid.
    A('#define %-26s (*(volatile byte *)0x%08Xu)   /* code label, 68K 0x%06X */'
      % (_lb, (GAMEHIGH + _la) if _la < ROM_HI else _la, _la))
for _sk in ('stack0x00000000', 'stack0xfffffffc', 'ram0x000070be'):
    A('static byte %s[0x40];   /* Ghidra placeholder */' % _sk)
A('typedef unsigned long long uint6;')
for _st in ('halt_baddata', 'halt_unimplemented', 'stop', 'bcdAdjust',
            'invalidateCacheLines', 'func_0x00017bc6', 'func_0x000152ea',
            'func_0x0001baee', 'func_0x0001baec', 'func_0x0001b0ca'):
    A('static uint %s() { return 0; }' % _st)
for _rp in sorted({m for c in cands for m in re.findall(r'\b(register0x[0-9a-fA-F]+)\b', c[5])}):
    A('static int %s;   /* Ghidra could not resolve this register */' % _rp)
for _pt in sorted({m for c in cands
                   for m in re.findall(r'\b(PTR_[A-Za-z]+_[0-9a-fA-F_]+)\b', c[5])}):
    _pa = int(re.search(r'([0-9a-fA-F]+)$', _pt).group(1), 16)
    if _pa < ROM_HI:
        A('#define %-22s (*(volatile uint *)0x%08Xu)   /* pointer table, 68K 0x%06X */'
          % (_pt, GAMEHIGH + _pa, _pa))
    else:
        A('static uint %s;   /* pointer table outside ROM */' % _pt)
A('/* pseudo-registers: Ghidra has no calling convention for this program, so the')
A(' * bodies read their inputs from these. */')
regs = set()
for addr, nm, cl, vs, t, txt in cands:
    # ANY pseudo-register, not only _A<n>/_D<n>: the 68K condition flags appear as in_CF,
    # in_ZF, in_XF and Ghidra also emits extraout_var.
    regs |= set(re.findall(r'\b((?:unaff|in|extraout)_[A-Za-z0-9_]+)\b', txt))
for r in sorted(regs):
    if '_A' in r:
        A('static byte *_r_%s = wram;   /* never NULL: see the note above */' % r)
    else:
        A('static %s _r_%s;' % (_reg_ty(r), r))
A('')

# forward declarations, so a closure's call order does not matter
emitted = []
for ad in sorted(fnset):
    nm, sz, body = blocks[ad]
    m = re.search(r'\n(\w[\w \*]*\s+%s\s*\([^)]*\)\s*\{.*?\n\})' % re.escape(nm), body, re.S)
    if m:
        emitted.append((ad, nm, m.group(1)))
A('/* forward declarations: a closure may call a function defined further down. */')
_alltxt = re.sub(r'/\*.*?\*/', ' ', ''.join(t for _a, _n, t in emitted), flags=re.S)
_assigned = {nm for _a, nm, _t in emitted
             if re.search(r'=\s*' + re.escape(nm) + r'\s*\(', _alltxt)}


def _promote(txt, nm):
    """void -> uint when a call site uses the result. Ghidra types a routine `void`
    while a caller uses the value the 68K left in D0; the comment has to be stripped
    first, because Ghidra puts /* WARNING ... */ before the return type."""
    head, _, rest = txt.partition('{')
    sig = ' '.join(re.sub(r'/\*.*?\*/', ' ', head, flags=re.S).split())
    if nm in _assigned and re.match(r'void\s', sig):
        sig = re.sub(r'^void\s', 'uint ', sig)
    return sig + ' {' + rest


# Ghidra sometimes emits a CALL WITH AN ARGUMENT to a function it declared (void). The bodies
# are verbatim, so the DECLARATION is made unprototyped rather than the call edited.
_with_args = set()
for _a7, _n7, _t7 in emitted:
    _with_args |= set(re.findall(r'\b(FUN_[0-9a-f]+|[A-Za-z_]\w{3,})\s*\(\s*[^)\s]', _t7))
for ad, nm, txt in emitted:
    # the PROMOTED signature, or the declaration and the definition disagree and a
    # caller using the result gets "invalid use of void expression"
    _sig = _promote(txt, nm).split('{', 1)[0].strip()
    if nm in _with_args and _sig.endswith('(void)'):
        _sig = _sig[:-6] + '()'
    A('static %s;' % _sig)
A('')
# STRIP SHADOWING PSEUDO-REGISTER LOCALS. Ghidra declares `int unaff_A6;` INSIDE
# the body, which shadows the global the case setup assigns -- the function then
# reads an uninitialised pointer and takes an arbitrary branch. Measured: this
# alone accounted for most remaining mismatches (clamp_x_velocity returned its
# input unchanged because unaff_A6 was a local). tools/cport_prep.py drops 1,743
# of these for the full port; the same transform is needed here. It is the ONLY
# edit made to a body, and it removes a declaration rather than changing code.
# The `*` binds to the NAME in Ghidra's output (`byte *in_A1;`), a type is never a C
# keyword, and the element type must be KEPT rather than dropped -- all three learned
# from doing this to the port first (LESSONS, "The shadow-DROP transform was
# corrupting address AND value").
SHADOW = re.compile(r'^[ \t]+(?:const\s+)?([A-Za-z_]\w*)\s*((?:\*\s*)*)'
                    r'((?:unaff|in|extraout)_[A-Za-z0-9_]+)\s*;[ \t]*\r?\n', re.M)
def _fix_pseudo(body):
    """Ghidra's local declarations carry the ELEMENT TYPE; keep it, and bind the
    value to the register the case setup wrote."""
    declared = {m.group(3): (m.group(1) + ' ' + m.group(2).replace(' ', '')).strip()
                for m in SHADOW.finditer(body) if m.group(1) not in _NOTATYPE}
    body = SHADOW.sub(lambda m: m.group(0) if m.group(1) in _NOTATYPE else '', body)
    used = sorted(set(_PSUSE.findall(body)))
    if not used or '{' not in body:
        return body, len(declared)
    i = body.index('{')
    out = []
    for n in used:
        ty = declared.get(n) or _reg_ty(n)
        sep = '' if ty.endswith('*') else ' '
        out.append('\n  %s%s%s = (%s)_r_%s;' % (ty, sep, n, ty, n))
    return body[:i + 1] + ''.join(out) + body[i + 1:], len(declared)
nshadow = 0
nabs = 0
nsx = 0
nnarrow = 0
_NARROW_ASSIGN = re.compile(r'\b(_?(?:DAT|[a-z]{1,3}Ram)_?[0-9a-f]{4,8}(?:_\d+)?)\s*=\s*'
                            r'(_?(?:DAT|[a-z]{1,3}Ram)_?[0-9a-f]{4,8}(?:_\d+)?)\s*;')


def _narrow_assign(m):
    d, sc = m.group(1), m.group(2)
    wd, ws = wid(sym_addr(d) or 0), wid(sym_addr(sc) or 0)
    if not wd or not ws or wd >= ws:
        return m.group(0)
    return '%s = %s >> %d;' % (d, sc, 8 * (ws - wd))

for ad, nm, txt in emitted:
    txt, k = _fix_pseudo(txt)
    nshadow += k
    txt, k2 = _absshort(txt)
    nabs += k2
    txt, k3 = _sx16(txt)
    nsx += k3
    # A NARROWER DESTINATION TAKES THE HIGH BITS: the 68K is big-endian, so
    # `_DAT_fffff138 = _DAT_fffff132` (a LONG at 0xFFF132) reads the WORD AT THAT
    # ADDRESS -- the high half -- while C's implicit conversion takes the low half. The
    # oracle settles it: the long is 0x102E_C800 and the write to 0xFFF138 is 0x102E.
    txt, k4 = _NARROW_ASSIGN.subn(_narrow_assign, txt)
    nnarrow += k4
    A('/* ---- 68K 0x%05X %s ---- */' % (ad, nm))
    # PORTTEST_SEC keeps the bodies out of the main .text image (they go in the free
    # ROM gap above .tilesmd), and _promote keeps the definition's return type in
    # step with the forward declaration.
    _bt = _promote(txt, nm)
    if nm in _with_args:
        _bt = _bt.replace('(void)', '()', 1)
    A('static PORTTEST_SEC ' + _bt)
    A('')
A('/* 16 summary words then one slot per case, sized to the sweep below. At a fixed [64]')
A(' * the 48 available slots silently overflowed once the sweep passed 48 cases, and only')
A(' * the per-case breakdown was corrupted -- the summary counters are written by this code')
A(' * and stayed correct, which is exactly why it went unnoticed for several builds. */')
A('volatile uint port_test_out[@@NCASES@@];')
A('')
A('/* NO abort() STUB HERE, DELIBERATELY. gcc emits a call to abort() for a path it')
A(' * proves unreachable, and this is a freestanding build with no libc, so the link')
A(' * fails loudly on _abort. That failure is the GUARD: stubbing abort() to record')
A(' * and return is undefined behaviour, because gcc treats everything after the')
A(' * call as dead code. Measured 2026-09-27 -- with the stub, a 16-function build')
A(' * ran 42 of 54 in ares and produced NO BARCODE AT ALL on the FPGA (pt20), while')
A(' * the same set minus the aborting function ran 30/39 on the rig (pt21). It also')
A(' * silently dropped the summary counters, so the barcode read zero passes on a')
A(' * working test. A stub that turns a loud link error into a silent hardware')
A(' * crash is worse than the link error. If the link fails on _abort, LOWER --max')
A(' * until it does not, and record which function was dropped. */')
A('')
A('PORTTEST_SEC uint port_selftest(void)')
A('{')
A('    uint run = 0, pass = 0, guards = 1, tripped = 0;')
# FRT TIMING (SILICON 6b: cache residency is the one thing ares cannot answer -- it
# charges instruction cycles only, no data cache, no SDRAM waits). TCR is already set
# to /8 by m_main, so a tick is 8 CPU clocks = 0.348us at 23.01 MHz and 65536 ticks is
# 22.8ms, longer than any single call: a 16-bit wrap-safe subtract is enough.
# Bulletin #27: FRCH/FRCL reads and TCR are safe; output compare (TOCR/OCR*/TIER/FTCSR)
# arms a double CMD INT on the 32X and must never be written.
A('    uint tt_sum = 0, tt_worst = 0, tt_wi = 0;')
A('#define FRT16() ((uint)(ushort)((SH2_FRT_FRCH << 8) | SH2_FRT_FRCL))')
A('    port_test_out[0] = 0x50525433u;      /* PRT3 */')
A('    wram_arm();      /* ONCE per sweep: zero the mirror and lay the guards. Each case then')
A('                      * clears only the span it touches, which took the sweep rate from 2')
A('                      * to 54 per 400 frames -- the full 15,888-byte clear per case was')
A('                      * ~1.6 MB of writes a vint and Mike saw it as a slideshow. */')

skipped = collections.Counter()
overcon = collections.Counter()
txt_of, ent_of, aval_of, xd_of = {}, {}, {}, {}
INW = lambda q: any(lo <= q <= hi for lo, hi, _ in off.values())


def WOFF_PY(v):
    """the mirror offset of a 68K address, mirroring the emitted WOFF macro"""
    v &= 0xFFFFFF
    for lo, hi, p in off.values():
        if lo <= v <= hi:
            return p + (v - lo)
    return 0


def SH2(v):
    """A 68K address as an SH-2 expression, or None if it is not reachable."""
    v &= 0xFFFFFF
    if INW(v):
        return '(uint)(unsigned long)WP(0x%06Xu)' % v      # the work-RAM mirror
    if v < ROM_HI:
        return '0x%08Xu' % (GAMEHIGH + v)                  # the cart's 68K program
    return None
cases = []
if a.port:
    vec = {}          # no vector cases in port mode: this unit is the game, not a test
for addr, nm, cl, vs, t, txt in cands:
    aregs = sorted(set(re.findall(r'\b((?:unaff|in|extraout)_A\d\w*)\b', txt)))
    for v in vs:
        # addresses this closure can actually write, for THIS vector's register state
        _wr_ok, _ea = set(), v['entry']['a']
        for _nm3 in re.findall(r'\b(_?(?:DAT|[a-z]{1,3}Ram)_?[0-9a-f]{4,8}(?:_\d+)?)\s*=[^=]',
                               txt):
            _a3 = sym_addr(_nm3)
            if _a3:
                _wr_ok.update(range(_a3, _a3 + 4))
        for _ty3, _rg3, _off3 in _BASEW.findall(txt):
            _base3 = _ea[int(_rg3)] & 0xFFFFFF
            _a3 = _base3 + int(_off3, 16 if _off3.startswith('0x') else 10)
            _wr_ok.update(range(_a3, _a3 + 4))
        for _rg3, _idx3 in re.findall(r'\b(?:unaff|in|extraout)_A(\d)\w*\s*\[\s*'
                                      r'(0x[0-9a-fA-F]+|\d+)\s*\]\s*=[^=]', txt):
            _a3 = (_ea[int(_rg3)] & 0xFFFFFF) + int(_idx3, 16 if _idx3.startswith('0x') else 10)
            _wr_ok.update(range(_a3, _a3 + 4))
        # any store we cannot resolve means the writable set is not a bound
        if (re.search(r'\*\s*\(\s*\w+\s*\*\s*\)\s*\(\s*(?:WP|W68)\(', txt)
                or re.search(r'\*\s*p[a-z]Var\d+\s*=[^=]', txt)
                or re.search(r'\bp[a-z]Var\d+\s*\[[^\]]*\]\s*=[^=]', txt)
                or re.search(r'\*\s*\(\s*\w+\s*\*\s*\)\s*\(?\s*[ipu]Var\d', txt)):
            _wr_ok = set()
        _last, _dropped = {}, 0
        for x in nonstack(v):
            _ax = int(x[0], 16)
            if not INW(_ax & 0xFFFFFF):
                continue
            if _wr_ok and (_ax & 0xFFFFFF) not in _wr_ok:
                _dropped += 1          # the closure has no statement targeting it
                continue
            _last[_ax] = (x[1], x[2] if len(x) > 2 else 2)
        if _dropped:
            overcon[nm] += _dropped
        wr = [(adr0, v0, w0) for adr0, (v0, w0) in sorted(_last.items())]
        if not wr:
            continue
        rd = [(int(x[0], 16), x[1], x[2] if len(x) > 2 else 2)
              for x in (v.get('reads') or []) if INW(int(x[0], 16) & 0xFFFFFF)]
        # Only an UNAMBIGUOUS inner-call value is used: every recorded exit must
        # agree. A function whose exits disagree was rejected above, because the
        # capture carries no sequence id to attribute an inner call to an outer one.
        _ranges = [(x, x + blocks[x][1] + 8) for x in cl]
        inner = [q for q in recs_all
                 if any(lo4 <= int(q['caller'], 16) < hi4 for lo4, hi4 in _ranges)]
        xo = {}
        for rec in inner:
            for i2 in range(8):
                ev = (rec['exit']['a'] or [0] * 8)[i2] & 0xFFFFFF
                if xo.setdefault(i2, ev) != ev:
                    xo[i2] = None
        def _aval(r):
            i2 = int(re.search(r'_A(\d)', r).group(1))
            if r.startswith('extraout_') and xo.get(i2) is not None:
                return xo[i2]
            return v['entry']['a'][i2] & 0xFFFFFF
        if any(SH2(_aval(r)) is None for r in aregs):
            skipped[nm] += 1; continue
        aval_of[len(cases)] = {r: _aval(r) for r in aregs}
        _xd = {}
        for rec in inner:
            for i3 in range(8):
                ev = (rec['exit']['d'] or [0] * 8)[i3] & 0xFFFFFFFF
                if _xd.setdefault(i3, ev) != ev:
                    _xd[i3] = None
        xd_of[len(cases)] = {k: v for k, v in _xd.items() if v is not None}
        dregs = [(r, int(n), 2 if r.endswith('w') else 4)
                 for r, n in sorted(set(re.findall(
                     r'\b((?:unaff|in|extraout)_D(\d)\w*)\b', txt)))]
        txt_of[nm] = txt
        ent_of[len(cases)] = v['entry']
        cases.append((nm, aregs, v['entry']['a'], rd, wr, dregs, v['entry']['d']))

for n, (nm, aregs, ea, rd, wr, dregs, ed) in enumerate(cases):
    ent = ent_of[n]
    # the span this case touches, computed BEFORE the clear is emitted
    _ta = [WOFF_PY(x[0]) for x in rd] + [WOFF_PY(x[0]) for x in wr]
    _ta += [WOFF_PY(x[0]) + 3 for x in rd] + [WOFF_PY(x[0]) + 3 for x in wr]
    _lo_touch = min(_ta) if _ta else 0
    _hi_touch = max(_ta) if _ta else -1
    A('    /* --- %s, vector %d --- */' % (nm, n))
    A('    {')
    A('        port_test_out[6] = %du;   /* watchdog: the case currently ENTERED */' % n)
    A('        wram_zero(%du, %du);' % (_lo_touch, _hi_touch - _lo_touch + 1)
      if _hi_touch >= _lo_touch else '        /* nothing to clear */')
    bw = based_widths(txt_of[nm], ent_of[n])
    # NOT COALESCED, and the reason is measured: treating two adjacent words that compose
    # an address as one pointer cell (colour_cycle_streamer's 0xFFF302/0xFFF304 ->
    # 0x0001A70E) made its `for (...; (char)*pbVar5 < 0; pbVar5 += 8)` walk CART data, which
    # never terminates. The rom hung before the first publish and the whole sweep read 0/103
    # -- magic word included. The pointer really is there; seeding it does not make the loop
    # bounded, because the terminator lives in data the vector never captured.
    by = {}
    for adr, val, w in rd:
        for k in range(w):
            by[adr + k] = (val >> (8 * (w - 1 - k))) & 0xFF
    rd2, done = [], set()
    for adr in sorted(by):
        if adr in done:
            continue
        w = bw.get(adr, 0)
        if not w or any(adr + k not in by for k in range(w)):
            w = next((ww for aa, vv, ww in rd if aa == adr), 1)
        v = 0
        for k in range(w):
            v = (v << 8) | by[adr + k]
            done.add(adr + k)
        rd2.append((adr, v, w))
    rd = rd2
    for adr, val, w in rd:
        ty = {1: 'byte', 2: 'ushort', 4: 'uint'}.get(w, 'ushort')
        tr = SH2(val) if w == 4 and (val & 0xFFFFFF) and (
             WRAM_LO <= (val & 0xFFFFFF) < STACK_LO or (val & 0xFFFFFF) < ROM_HI) else None
        if tr:
            A('        *(volatile uint *)WP(0x%06Xu) = %s;   /* 68K 0x%06X rebased */'
              % (adr, tr, val & 0xFFFFFF))
        else:
            A('        *(volatile %s *)WP(0x%06Xu) = 0x%Xu;' % (ty, adr, val))
    for r, rn, w in dregs:
        # an extraout_ register is the callee's OUTPUT; the entry state is the wrong
        # source for it, exactly as for extraout_A<n>
        dv = xd_of[n].get(rn, ed[rn]) if r.startswith('extraout_') else ed[rn]
        A('        _r_%s = %s0x%Xu;   /* entry D%d */'
          % (r, '(short)' if w == 2 else '(int)', dv & (0xFFFF if w == 2 else 0xFFFFFFFF), rn))
    for r in aregs:
        av = aval_of[n][r]
        tr = SH2(av)
        if not tr:
            raise SystemExit('unmapped %s 0x%06X for %s -- case should have been '
                             'skipped' % (r, av, nm))
        # a base in the work-RAM mirror, or in the cart's copy of the 68K program
        A('        _r_%s = (byte *)%s;   /* %s = 68K 0x%06X */' % (r, tr, r, av))
    A('        { uint _t0 = FRT16();')
    A('          %s();' % nm)
    A('          { uint _d = (uint)(ushort)(FRT16() - _t0);')
    A('            tt_sum += _d;')
    A('            if (_d > tt_worst) { tt_worst = _d; tt_wi = run; } } }')
    A('        run++; port_test_out[1] = run;')
    conds = []
    for adr, val, w in wr:
        ty = {1: 'byte', 2: 'ushort', 4: 'uint'}.get(w, 'ushort')
        tr = SH2(val) if w == 4 and (val & 0xFFFFFF) and (
             WRAM_LO <= (val & 0xFFFFFF) < STACK_LO or (val & 0xFFFFFF) < ROM_HI) else None
        if tr:
            # the oracle recorded a 68K address; the port must store the SH-2 one
            conds.append('*(volatile uint *)WP(0x%06Xu) == %s' % (adr, tr))
        else:
            conds.append('*(volatile %s *)WP(0x%06Xu) == 0x%Xu' % (ty, adr, val))
    A('        { uint gw = wram_guards_ok();')
    A('        if (gw) { guards = 0u; tripped++;')
    A('            /* MIRROR ARTEFACT, not a verdict: low nibble = which window */')
    A('            port_test_out[16 + %d] = 0xE0000000u | gw; }' % n)
    A('        else {')
    A('            uint bad = 0u, gv = 0u;')
    for ci, c in enumerate(conds):
        lhs = c.split(' == ')[0]
        A('            if (!bad && !(%s)) { bad = %du; gv = (uint)(%s); }'
          % (c, ci + 1, lhs))
    A('            if (!bad) { pass++; port_test_out[2] = pass;'
      ' port_test_out[16 + %d] = 1u; }' % n)
    A('            /* 0xD0000000 | cond index << 20 | value: WHICH cell disagreed */')
    A('            else port_test_out[16 + %d] = 0xD0000000u | (bad << 20) | (gv & 0xFFFFFu);' % n)
    A('        } }')
    A('    }')
A('    /* STABLE END-OF-SWEEP VALUES. [1] and [2] are written as the sweep runs')
A('     * so a crash mid-sweep still shows progress, but they are MID-SWEEP values')
A('     * and a reader that samples them gets whatever count the sweep had reached.')
A('     * Measured 2026-09-27: the same rom reported 27 in an ares dump and 36 off')
A('     * the rig barcode, purely from sampling at different points. [9]/[10] are')
A('     * only written when a sweep COMPLETES, and [8] counts completed sweeps, so')
A('     * a reader has something that does not move under it. Read [10]. */')
A('    port_test_out[1] = run;')
A('    port_test_out[2] = pass;')
A('    port_test_out[9] = run;')
A('    port_test_out[10] = pass;')
A('    port_test_out[11] = tt_sum;     /* FRT ticks, calls only, whole sweep */')
A('    port_test_out[12] = tt_worst;   /* the most expensive single call */')
A('    port_test_out[13] = tt_wi;      /* which case that was */')
A('    port_test_out[8]++;')
A('    port_test_out[4] = guards ? 0u : 0xBADu;')
A('    port_test_out[6] = 0xFFFFu;   /* watchdog clear: this sweep completed */')
A('    port_test_out[5] = tripped;')
A('    return (run << 8) | pass;')
A('}')
A('#endif /* PORT_TEST */')
out_text = '\n'.join(O).replace('@@NCASES@@', str(16 + len(cases) + 8))
assert '@@NCASES@@' not in out_text
open(a.out, 'w').write(out_text + '\n')

print('  dropped %d shadowing pseudo-register local declaration(s)' % nshadow)
print('  rewrote %d 68K absolute-short reference(s) through the mirror' % nabs)
print('  translated %d 16-bit work-RAM pointer(s) at the point of use' % nsx)
print('  gave %d narrower-destination assignment(s) the HIGH bits' % nnarrow)
print('%s: %d functions (%d incl. callees), %d vector cases, mirror %d B in %d windows'
      % (a.out, len(cands), len(emitted), len(cases), TOT, len(WINS)))
for addr, nm, cl, vs, t, txt in cands:
    print('  0x%05X %-28s closure %d' % (addr, nm, len(cl)))
for k, v in overcon.most_common():
    print('  dropped %d over-constrained expectation(s) from %s: the closure has no '
          'statement targeting them' % (v, k))
for k, v in skipped.most_common():
    print('  skipped %d case(s) of %s: base register outside the mirror' % (v, k))
if UNKNOWN_W:
    print('WIDTH NOT IN THE TABLE for %d symbol(s) -- declared 1 B, treat any FAIL '
          'from these as unproven: %s' % (len(UNKNOWN_W), ', '.join(sorted(UNKNOWN_W)[:8])))
print('rejected:')
for k, v in rej.most_common():
    print('  %4d  %s' % (v, k))
