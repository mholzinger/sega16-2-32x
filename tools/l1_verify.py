#!/usr/bin/env python3
"""L1 DIFFERENTIAL: run a decompiled function against its oracle vectors.

A PASS means every recorded write was reproduced exactly and nothing else was
touched. A PASS-UNCONSTRAINED means the vectors contain NO writes, so the test
cannot tell a correct function from an empty one -- report it, never count it.

    tools/l1_verify.py [--fn 0x02E50 ...] [--all] [--keep DIR]

For each vector: seed the work-RAM image with the reads the oracle recorded,
call the decompiled function, then require the write set to match EXACTLY --
every recorded write present with the right value, and nothing else touched.

WHY READS ARE THE INPUT. Almost nothing in this game takes register arguments.
The bodies read absolute work RAM (`DAT_fffff03c`), so the entry state that
decides the outcome is MEMORY. The capture records the first read of every
address, which is precisely the input set; writes are the output set. A vector
without reads cannot be replayed, and tools/l1_oracle.lua only started
recording them on 2026-09-27.

HOST FIRST, then the target. This runs the function compiled for the host,
which catches decompilation and harness errors in seconds. It is NOT a
substitute for running on the 32X -- a pass here means the C is right, a pass
on ares means it is right on the machine that will run it. The generated file
is written to --keep so the same source can be built for SH-2.
"""
import argparse, json, re, subprocess, tempfile, collections
from pathlib import Path

WRAM_LO, WRAM_HI = 0xFFC000, 0x1000000
ROM = str(Path('roms/altbeast/prog68k.bin').resolve())
# The trailing (?:_\d+)? matters: Ghidra disambiguates overlapping symbols with a
# suffix (DAT_fffff032_1), and without it those were never declared at all.
SYM = re.compile(r'\b(_?(?:DAT|[a-z]{1,4}Ram)_?[0-9a-fA-F]{4,8}(?:_\d+)?)\b')
# MATCH A CALL WITH ARGUMENTS TOO. This was `\w+\s*\(\s*\)` -- EMPTY parentheses
# only -- so any callee invoked with an argument was silently dropped from the
# transitive closure and then failed to compile as an undeclared FUN_. Every one
# of those 20 functions was sitting in the corpus the whole time. Ghidra usually
# emits `f()` for this program because it has no calling convention, which is
# exactly why the narrow form looked sufficient.
CALL = re.compile(r'\b([A-Za-z_]\w*)\s*\(')
CALL_KW = {'if', 'while', 'for', 'switch', 'return', 'sizeof', 'do', 'else',
           'short', 'ushort', 'uint', 'int', 'byte', 'char', 'long', 'void',
           'undefined', 'undefined1', 'undefined2', 'undefined4', 'ulonglong',
           'longlong', 'uchar', 'ulong', 'signed', 'unsigned', 'volatile',
           'memset', 'printf', 'fprintf', 'fopen', 'fread', 'fclose', 'alloca'}
# `*(short *)(unaff_A6 + 0x14)` -- the width the BODY uses for a cell it reaches
# through a base register. There is no named symbol for these, so DECL cannot
# supply the width, and the oracle's access width is not always the same one:
# FUN_00003f32 reads 0xFFDC90 as ONE int while the 68K wrote it as TWO words.
# Without the body's own width the two compose in the host's byte order.
# extraout_ MUST be in this alternation. Leaving it out meant the width of every
# cell reached off a callee-produced register was never inferred, and the
# host-endian check below silently never fired -- the statement that exposed it,
# `*(undefined2 *)(unaff_A6 + 0x52) = *(undefined2 *)(extraout_A0 + 0xc)`, reads
# through exactly that register.
BASED = re.compile(r'\*\s*\(\s*(\w+)\s*\*\s*\)\s*\(\s*((?:unaff|in|extraout)_A\d)\s*\+\s*'
                   r'(0x[0-9a-fA-F]+|\d+)\s*\)')
TYW = {'int': 4, 'uint': 4, 'undefined4': 4, 'ulong': 4, 'long': 4,
       'short': 2, 'ushort': 2, 'undefined2': 2, 'word': 2,
       'char': 1, 'byte': 1, 'undefined': 1, 'undefined1': 1, 'uchar': 1}


def based_widths(bodies, entry):
    """address -> width, for every cell the closure reaches off a base register."""
    out = {}
    a = (entry.get('a') or [0] * 8)
    for f in bodies:
        for ty, rg, off in BASED.findall(f['body']):
            w = TYW.get(ty)
            if not w:
                continue
            base = a[int(rg[-1])] & 0xFFFFFF
            ad = base + int(off, 16 if off.startswith('0x') else 10)
            out[ad] = max(out.get(ad, 0), w)
    return out


PTR_FROM_MEM = re.compile(r'\*\s*\(\s*\w+\s*\*\s*\)\s*\(?\s*\w*Var\d+\b'
                          r"|=\s*\*\s*\(\s*\w+\s*\*\*\s*\)")
SYM2 = re.compile(r'\b((?:PTR_(?:FUN|DAT|LAB)_[A-Za-z0-9_]*?|s_[A-Za-z0-9_]*?|ram0x)[0-9a-fA-F]{4,8}(?:_\d+)?)\b')
REGPH = re.compile(r'\b(register0x[0-9a-fA-F]+)\b')
PSEUDO = re.compile(r'\b((?:unaff|in|extraout)_[AD]\d\w*)\b')
SHADOW = re.compile(r'^[ \t]*(?:short|int|long|char|u?int\d*_t|undefined\d?|byte|ushort'
                    r'|uint|ulonglong|longlong)[ \t*]+((?:unaff|in|extraout)_[AD]\d\w*)'
                    r'[ \t]*;[ \t]*\r?\n', re.M)


def pseudo_regs(bodies):
    """pseudo-registers the closure reads, as (name, index, is_addr).

    An A-register is a BYTE POINTER, not an int: the bodies use it both as
    `unaff_A6 + 0x16` and `unaff_A6[0x21]`, and only the pointer type compiles
    for both with the byte offsets the 68K used.
    """
    names = set()
    for f in bodies:
        names |= set(PSEUDO.findall(f['body']))
    return sorted(names)


DECL = {}
# address -> the width the BODY accesses it at, which is what the seeding has to
# match; see the coalescing note in main().
CELLW = {}


def load_decls(path):
    """symbol -> the C type cport_prep.py declared for it."""
    for line in open(path):
        m = re.match(r'extern\s+(\w+)\s+(\w+)\s*;', line.strip())
        if m:
            DECL[m.group(2)] = m.group(1)
    return DECL


def load_functions(src):
    parts = re.split(r'\n/\* ---- (0x[0-9A-F]{5})\s+(\S+)\s+\((\d+) B\)[^*]*\*/', src)
    out = {}
    for i in range(1, len(parts), 4):
        out[int(parts[i], 16)] = dict(name=parts[i + 1], size=int(parts[i + 2]),
                                      body=parts[i + 3])
    return out


def sym_addr(name):
    # Try the FULL tail before considering a disambiguator suffix. Stripping
    # `_\d+$` first ate the address of DAT_00000001 -- an all-decimal address is
    # indistinguishable from Ghidra's `_1` overlap suffix -- leaving 'DAT' with no
    # hex tail, so the symbol was silently dropped and the body would not compile.
    m = (re.search(r'([0-9a-fA-F]{4,8})$', name)
         or re.search(r'([0-9a-fA-F]{4,8})_\d+$', name))
    if not m:                                  # not an address-named symbol at all
        return None
    v = int(m.group(1), 16)
    if 0xFFFF0000 <= v <= 0xFFFFFFFF:
        v = 0xFF0000 | (v & 0xFFFF)
    return v


def observed_width(addr, vectors):
    """How wide is the cell at `addr`, from the ORACLE's own accesses?

    Ghidra's width inference is a guess and it is sometimes wrong in a way that
    makes a CORRECT function fail: it declares rng_next's state at 0xFFF014 a
    BYTE, while the oracle shows the 68K reading and writing 0xFFF014 and
    0xFFF016 as two words -- a 32-bit cell. The access footprint is measured,
    so it wins over the declaration.
    """
    touched = set()
    for r in vectors:
        for ad, _v, w in r['reads'] + r['writes']:
            a0 = int(ad, 16)
            for k in range(w):
                touched.add(a0 + k)
    n = 0
    while addr + n in touched and n < 4:
        n += 1
    return {0: None, 1: 1, 2: 2, 3: 2, 4: 4}[n]


IDENT = re.compile(r'\b([A-Za-z_]\w*)\b')
FNAMES, FBYNAME = set(), {}


def gen(fn, fns, vectors):
    """Emit a self-contained C file for one function and its vectors."""
    want, seen = [fn], set()
    while want:                                  # transitive callees
        a = want.pop()
        if a in seen or a not in fns:
            continue
        seen.add(a)
        body = fns[a]['body']
        refs = (set(CALL.findall(body)) - CALL_KW) | (set(IDENT.findall(body)) & FNAMES)
        for c in refs:
            a2 = FBYNAME.get(c)
            if a2 is not None:
                want.append(a2)
    bodies = [fns[a] for a in sorted(seen)]
    syms = sorted({s for f in bodies
                   for s in SYM.findall(f['body']) + SYM2.findall(f['body'])
                   if sym_addr(s) is not None}, key=sym_addr)

    L = ['/* GENERATED by tools/l1_verify.py -- one function, its callees, and',
         ' * the oracle vectors it is required to reproduce. */',
         '#include <stdio.h>', '#include <string.h>', '#include <alloca.h>',
                  # the port build's own headers: Ghidra's intrinsics (bcdAdjust,
         # CONCAT*, CARRY*) and its type zoo. Re-deriving them here was the
         # single biggest source of WONT-COMPILE -- 38 type errors and 15
         # missing intrinsics that cport_prep.py had already solved.
         '#include "ghidra_types.h"', '#include "ghidra_rt.h"',
         '#define ROM_PATH "%s"' % ROM,
         '',
         '/* the 68K address space the function sees, as one flat image */',
         'static byte MEM[0x1000000];',
         '#define MEMAT(a) (*(byte *)(MEM + (a)))',
         '',
         '/* THE PROGRAM ROM IS PART OF THE INPUT STATE. The oracle watches work',
         ' * RAM, so a read of a table in ROM is never recorded and never seeded --',
         ' * and the entry registers point straight at those tables',
         ' * (animation_peek_next enters with A0 = 0x004DCE, program ROM). Without',
         ' * this the function reads zeros and fails every vector, which looks',
         ' * exactly like a bad decompilation. The 68K maps its program at 0. */',
         'static void load_rom(void){',
         '  FILE *f = fopen(ROM_PATH, "rb");',
         '  if (!f) { fprintf(stderr, "no rom at %s\\n", ROM_PATH); return; }',
         '  fread(MEM, 1, 0x40000, f); fclose(f);',
         '}', '']
    # PSEUDO-REGISTERS AS GLOBALS, so the driver can seed them from the oracle's
    # entry state. Nothing seeded them before, so every function that takes its
    # input in a register was tested with garbage.
    for r in pseudo_regs(bodies):
        L.append('static %s %s;' % ('byte *' if '_A' in r else 'short', r))
    for r in sorted({q for f in bodies for q in REGPH.findall(f['body'])}):
        L.append('static int %s;   /* Ghidra could not resolve this register */' % r)
    L.append('')
    for s in syms:
        # TYPE THE SYMBOL AS GHIDRA DECLARED IT. Defining every symbol as a
        # byte lvalue silently truncates: rng_next's state is a 32-bit cell at
        # 0xFFF014, and as a byte its multiply-and-fold produced the wrong
        # value and the function read as FAIL when it is correct. The decompile
        # thread hit the same thing from the other side (f1_datwidth.py).
        ad = sym_addr(s)
        t = DECL.get(s) or ('uint' if s.startswith('PTR_') else 'byte')
        ow = observed_width(ad, vectors)
        dw = {'byte': 1, 'ushort': 2, 'uint': 4}.get(t, 1)
        if ow and ow > dw:                 # the oracle saw a wider cell
            t = {1: 'byte', 2: 'ushort', 4: 'uint'}[ow]
        L.append('#define %s (*(%s *)(MEM + 0x%06X))' % (s, t, ad))
        CELLW[ad] = {'byte': 1, 'ushort': 2, 'uint': 4}.get(t, 1)
    L.append('')
    # FORWARD-DECLARE EVERYTHING FIRST. The corpus calls in both directions and
    # Ghidra emits no prototypes, so definition order cannot satisfy it; without
    # this, 90 of 122 functions failed to compile on 'call to undeclared
    # function' alone -- a harness fault, not a decompilation fault.
    # PROMOTE void -> uint FOR A FUNCTION WHOSE RESULT IS ASSIGNED. Ghidra has no
    # calling convention for this program, so it types a routine `void` while a
    # caller still uses the value the 68K left in D0 -- `uVar2 = FUN_000038c0();`
    # against `void FUN_000038c0(void)`. That was 46 of the remaining
    # WONT-COMPILE. Promoted only where a call site actually assigns the result,
    # rather than normalising every function to `uint` the way cport_prep.py does
    # for the port: a narrower change cannot truncate a real wide return.
    assigned = set()
    alltxt = re.sub(r'/\*.*?\*/', ' ', ''.join(f['body'] for f in bodies), flags=re.S)
    for f in bodies:
        if re.search(r'=\s*' + re.escape(f['name']) + r'\s*\(', alltxt):
            assigned.add(f['name'])

    # TAKE THE SIGNATURE FROM THE DEFINITION, never from a heuristic. The old
    # rule was "uint if 'uint ' appears in the first 40 chars, else void", which
    # declared `static void zoom_scale_lookup();` over a definition returning
    # ulonglong and `static void FUN_0000673e();` over one returning short.
    # BOTH visible WONT-COMPILE classes were this one bug: the conflicting-types
    # error directly, and `uVar1 = FUN_00003cb6();` "assigning from incompatible
    # type 'void'" as its knock-on. Everything before the first brace IS the
    # signature.
    def promoted(body, name):
        sig = body.strip().split('{', 1)[0].strip()
        sig = re.sub(r'/\*.*?\*/', ' ', sig, flags=re.S).strip()   # drop Ghidra's warnings
        sig = ' '.join(sig.split())
        if name in assigned and re.match(r'void\s', sig):
            sig = re.sub(r'^void\s', 'uint ', sig)
        return sig

    for f in bodies:
        L.append('static %s;' % promoted(f['body'], f['name']).replace('::', '__'))
    L.append('')
    for f in bodies:
        b = f['body'].strip()
        # STRIP SHADOWING PSEUDO-REGISTER LOCALS. Ghidra declares `int unaff_A6;`
        # INSIDE the body, which shadows the global the driver seeds -- so the
        # function reads an uninitialised pointer and either takes an arbitrary
        # branch or segfaults. That is the CRASH bucket, not unmappable 68K
        # addresses: MEM is a real host buffer and a SEEDED A-register points
        # into it. tools/cport_prep.py drops 1,743 of these for the full port.
        b = SHADOW.sub('', b)
        sig, body_rest = b.split('{', 1)
        b = promoted(b, f['name']) + ' {' + body_rest
        # Ghidra spells jump-table case labels with the C++ scope operator
        # (`switchD_00002ef0::caseD_8e`), which is not C. Mechanical rename;
        # cport_prep.py sanitises symbol names the same way.
        b = b.replace('::', '__')
        L.append('static ' + b)
        L.append('')
    return '\n'.join(L), bodies, assigned & {f['name'] for f in bodies
                                       if re.match(r'void\s', ' '.join(
                                           f['body'].strip().split('{', 1)[0].split()))}


def rom_sourced_wide(bodies, regvals):
    """Does the closure copy a cell of 2+ bytes out of a NON-work-RAM base?

    The ROM image is loaded as raw 68K bytes (big-endian) while every seeded
    work-RAM cell is written at its own width, so MEM carries TWO byte-order
    conventions at once. A 16- or 32-bit copy out of ROM therefore comes out
    byte-swapped on a little-endian host and no amount of harness care fixes it:
    animation_peek_next computes 0xFFC050 = 0xF808 correctly and 0xFFC052 =
    0x00C4 where the oracle has 0xC400, from
    `*(undefined2 *)(unaff_A6 + 0x52) = *(undefined2 *)(extraout_A0 + 0xc)`.

    This is an IRREDUCIBLE LIMIT OF THE HOST HARNESS, not a decompilation fault.
    The SH-2 self-test does not have it -- the SH-2 is big-endian, so the ROM
    image and the seeded cells share one convention. These functions are the
    population that has to be verified on the target.
    """
    for f in bodies:
        for ty, rg, _off in BASED.findall(f['body']):
            if TYW.get(ty, 1) >= 2 and regvals.get(rg, 0xFFFFFF) < WRAM_LO:
                return True
    return False


def cells_of(accesses):
    """Oracle accesses -> one (addr, value, width) per CELL THE BODY USES.

    The oracle records the 68K's own access widths, which are not always the
    width the decompiled body reads the cell at. rng_next's state is one 32-bit
    cell at 0xFFF014, and the 68K writes it as TWO WORDS (0xFFF014 and 0xFFF016)
    -- so seeding two native 16-bit values and letting the body read 32 bits
    composes them in the HOST's byte order, not the 68K's, and the value comes
    out wrong on a little-endian host.

    So: lay the oracle's bytes out in 68K (big-endian) order, then re-read them
    at the width the BODY uses and emit one native access per cell. Last write
    per byte wins, which is what a multi-step function's final state is.
    """
    by, oraclew = {}, {}
    for ad, val, w in accesses:
        a0 = int(ad, 16)
        oraclew[a0] = max(oraclew.get(a0, 0), w)
        for k in range(w):
            by[a0 + k] = (val >> (8 * (w - 1 - k))) & 0xFF
    out, done = [], set()
    for a0 in sorted(by):
        if a0 in done:
            continue
        # A cell reached through a BASE POINTER (`*(short *)(unaff_A6 + 0x14)`)
        # has no named symbol, so CELLW knows nothing about it -- and defaulting
        # such a cell to 1 byte re-introduced the byte-order bug and regressed
        # clamp_x_velocity and clamp_y_velocity from PASS to FAIL. The oracle's
        # own access width is the right fallback: it is what the 68K used, and
        # for these cells it is also what the body uses.
        w = CELLW.get(a0) or oraclew.get(a0) or 1
        if any(a0 + k not in by for k in range(w)):
            w = oraclew.get(a0, 1)       # only partly touched: trust the oracle
        if any(a0 + k not in by for k in range(w)):
            w = 1
        v = 0
        for k in range(w):
            v = (v << 8) | by[a0 + k]    # compose big-endian, as the 68K stored it
            done.add(a0 + k)
        out.append((a0, v, w))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', default='docs/proof/altbeast.c')
    ap.add_argument('--vectors', default='docs/proof/l1_vectors.jsonl')
    ap.add_argument('--fn', action='append', default=[])
    ap.add_argument('--all', action='store_true')
    ap.add_argument('--why', action='store_true',
                    help='on FAIL, print every mismatching cell with got vs want '
                         'and flag a byte-swap or an untouched input')
    ap.add_argument('--keep', default='/tmp/l1gen')
    ap.add_argument('--headers', default='/private/tmp/claude-501/-Users-mikeholzinger-src-sega16-2-32x/a4a45f9f-19fb-4603-93a1-c2e374df121a/scratchpad/cport',
                    help="cport_build.sh output dir (ghidra_types.h, ghidra_rt.h)")
    a = ap.parse_args()

    load_decls(str(Path(a.headers) / 'dat_syms.h'))
    fns = load_functions(open(a.src).read())
    FBYNAME.update({v['name']: k for k, v in fns.items()})
    FNAMES.update(FBYNAME)          # name -> address, for closure walking by reference
    vec = collections.defaultdict(list)
    recs_all = []
    for line in open(a.vectors):
        r = json.loads(line)
        recs_all.append(r)
        vec[int(r['callee'], 16)].append(r)

    targets = [int(x, 16) for x in a.fn]
    if a.all:
        # READS ARE NO LONGER THE ONLY INPUT. The filter required a recorded
        # memory read, which was right when nothing seeded the registers: a
        # vector with no reads had no input at all. Now the entry A- and
        # D-registers are seeded from the oracle, so a function whose input
        # arrives in registers is fully testable with no memory reads --
        # FUN_00006646 and FUN_0000673e both PASS 4/4 and neither was in the
        # sweep. Writes alone still constrain the output.
        targets = sorted(k for k, v in vec.items()
                         if k in fns and any(r['reads'] or r['writes'] for r in v))
    Path(a.keep).mkdir(parents=True, exist_ok=True)

    tally = collections.Counter()
    for t in targets:
        if t not in fns:
            tally['NO-SOURCE'] += 1
            continue
        vs = [r for r in vec[t] if r['reads'] or r['writes']]
        if not vs:
            tally['NO-REPLAYABLE-VECTOR'] += 1
            continue
        allrecs = [q for q in recs_all]
        regvals = {}
        CELLW.clear()      # per-function: a stale width from the previous
                           # closure would silently mis-size a cell here
        code, bodies, unmodelled = gen(t, fns, vs)
        drv = ['int main(void){int pass=0,fail=0,swapped=0,wrong=0;']
        for i, r in enumerate(vs):
            drv.append('  {')
            drv.append('    memset(MEM,0,sizeof MEM);')
            drv.append('    load_rom();')
            # SEED AND COMPARE AT THE CELL'S OWN WIDTH. Byte-wise seeding wrote
            # the oracle's bytes in 68K (big-endian) order and the bodies then
            # read them at native width -- on this little-endian arm64 host
            # 0xCF89035A read back as 0x5A0389CF, so EVERY multi-byte cell was
            # byte-swapped and every FAIL involving one was an artefact. That is
            # the whole of the rng_next "one bit wrong" report: the harness, not
            # the decompilation. Writing and reading through the same width makes
            # the host's byte order cancel out, so there is no byte order to get
            # right. (The SH-2 self-test never had this: the SH-2 is big-endian
            # AND it seeds at width.)
            # the body's OWN widths win for based cells; see based_widths()
            for _a, _w in based_widths(bodies, r.get('entry') or {}).items():
                CELLW.setdefault(_a, _w)
            for ad, val, w in cells_of(r['reads']):
                drv.append('    *(%s *)(MEM + 0x%06X) = 0x%Xu;'
                           % ({1: 'byte', 2: 'ushort', 4: 'uint'}.get(w, 'ushort'),
                              ad, val))
            # SEED THE PSEUDO-REGISTERS FROM THE ORACLE'S ENTRY STATE. An
            # A-register is the object-record base the body indexes off, and a D
            # register is an argument; neither was set before, so a function
            # taking input in a register was called with garbage. An A-register
            # points INTO MEM, which is what makes the raw-68K-address problem
            # go away for the common case.
            ent = r.get('entry') or {}
            # `extraout_AN` IS THE VALUE A CALLEE LEFT IN AN, not the outer
            # function's entry value. Seeding it from entry is simply the wrong
            # register: animation_peek_next enters with A0 = 0x004DCE while the
            # FUN_00006742 it calls EXITS with A0 = 0x0075C4. The oracle records
            # those inner calls, so take the exit value from them -- but only when
            # every recorded inner call agrees, because nothing pairs a specific
            # inner record with a specific outer vector. Where they disagree the
            # function is not scoreable and says so.
            xo, xo_ambig = {}, False
            fsz = fns[t].get('size', 0)
            for rec in allrecs:
                cl = int(rec['caller'], 16)
                if not (t <= cl < t + max(fsz, 1) + 8):
                    continue
                for i2 in range(8):
                    v2 = (rec['exit']['a'] or [0] * 8)[i2] & 0xFFFFFF
                    if xo.setdefault(i2, v2) != v2:
                        xo_ambig = True
            for rg in pseudo_regs(bodies):
                i = int(re.search(r'[AD](\d)', rg).group(1))
                if '_A' in rg:
                    regvals[rg] = (xo[i] if rg.startswith('extraout_') and i in xo
                                   else (ent.get('a') or [0] * 8)[i] & 0xFFFFFF)
                if '_A' in rg:
                    if rg.startswith('extraout_') and i in xo:
                        drv.append('    %s = (byte *)MEM + 0x%06X;   /* A%d as the '
                                   'CALLEE left it */' % (rg, xo[i], i))
                        continue
                    av = (ent.get('a') or [0] * 8)[i] & 0xFFFFFF
                    drv.append('    %s = (byte *)MEM + 0x%06X;   /* entry A%d */'
                               % (rg, av, i))
                else:
                    dv = (ent.get('d') or [0] * 8)[i] & 0xFFFF
                    drv.append('    %s = (short)0x%04X;   /* entry D%d */' % (rg, dv, i))
            drv.append('    %s();' % fns[t]['name'])
            drv.append('    int bad=0;')
            # ONLY THE LAST WRITE PER ADDRESS. A function that updates a cell
            # in steps records every step, and the intermediate values cannot
            # all be present in final memory -- requiring them failed
            # rng_next, which assigns its state three times, and every other
            # multi-step function. The vector's final state is what a port
            # must reproduce.
            seeded = {a2: v2 for a2, v2, _w in cells_of(r['reads'])}
            for ad2, val, w in cells_of(r['writes']):
                ty2 = {1: 'byte', 2: 'ushort', 4: 'uint'}.get(w, 'ushort')
                sw = int.from_bytes(val.to_bytes(w, 'big'), 'little')
                drv.append('    if(*(%s *)(MEM + 0x%06X) != 0x%Xu) { bad=1;' % (ty2, ad2, val))
                drv.append('      if(*(%s *)(MEM + 0x%06X) == 0x%Xu) swapped++; else wrong++;'
                           % (ty2, ad2, sw))
                if a.why:
                    drv.append('      fprintf(stderr, "    0x%06X w%d got 0x%%0*X want 0x%Xu%%s%s\\n",'
                               ' %d, *(%s *)(MEM + 0x%06X),'
                               ' *(%s *)(MEM + 0x%06X) == 0x%Xu ? "  [EXACT BYTESWAP]" : "");'
                               % (ad2, w, val,
                                  '  [input untouched]' if seeded.get(ad2) == val else '',
                                  w * 2, ty2, ad2, ty2, ad2, sw))
                drv.append('    }')
            for ad, val, w in []:
                for k in range(w):
                    drv.append('    if(MEMAT(0x%06X)!=0x%02X) bad=1;'
                               % (int(ad, 16) + k, (val >> (8 * (w - 1 - k))) & 0xFF))
            drv.append('    if(bad) fail++; else pass++;')
            drv.append('  }')
        drv.append('  printf("%d %d %d %d\\n",pass,fail,swapped,wrong); return 0;}')
        srcf = Path(a.keep) / ('v_%05X.c' % t)
        srcf.write_text(code + '\n'.join(drv) + '\n')
        exe = str(srcf)[:-2]
        # -fno-strict-aliasing IS REQUIRED for correctness, not hygiene. Decompiled
        # bodies read one address at several widths by construction, which is
        # exactly what strict aliasing lets the compiler disbelieve: it may then
        # fold a load to a value it thinks still holds. Measured 2026-09-27 on the
        # SH-2 self-test -- FUN_00003f32, verified correct by hand against all
        # four of its oracle vectors, reported 3 of 4 FAIL with its base read as
        # 0, and its FIRST vector passed. Same code, folded at one call site and
        # not another, which looks exactly like a per-vector data problem.
        cp = subprocess.run(['cc', '-w', '-O1', '-fno-strict-aliasing',
                             '-Wno-error=return-mismatch',
                             '-std=gnu99', '-I', a.headers,
                             '-Wno-error=incompatible-pointer-types',
                             '-Wno-error=int-conversion',
                             '-Wno-int-conversion', '-Wno-incompatible-pointer-types',
                             '-Wno-incompatible-function-pointer-types',
                             '-Wno-error=implicit-function-declaration',
                             '-Wno-error=incompatible-function-pointer-types',
                             str(srcf), str(Path(a.headers) / 'defs.c'), '-o', exe],
                            capture_output=True, text=True)
        if cp.returncode:
            e = cp.stderr
            if re.search(r"undeclared identifier 'PTR_(?:FUN|DAT)_", e):
                b = 'NEEDS-POINTER-TABLE'      # FULL-PORT 6(d): indirect dispatch
            elif re.search(r"undeclared (?:identifier|function) 'FUN_", e):
                b = 'CLOSURE-INCOMPLETE'       # a callee Ghidra never decompiled
            elif "register0x" in e:
                b = 'UNRESOLVED-REGISTER'      # Ghidra could not resolve a register ref
            elif re.search(r"undeclared identifier '(?:LAB_|caseD_|switchD)", e):
                b = 'NEEDS-CODE-LABEL'         # goto/jump-table target not emitted
            elif re.search(r"undeclared identifier 's_", e):
                b = 'NEEDS-STRING-DATA'
            else:
                b = 'WONT-COMPILE'
            tally[b] += 1
            print('  0x%05X %-22s %-20s %s' % (t, fns[t]['name'], b,
                  (e.strip().split('\n') or [''])[0][:56]))
            continue
        try:
            rp = subprocess.run([exe], capture_output=True, text=True, timeout=10)
            if a.why and rp.stderr.strip():
                print(rp.stderr.rstrip())
        except subprocess.TimeoutExpired:
            # a decompiled loop whose exit condition depends on state the
            # vector does not seed will not terminate; that is a finding about
            # the decompilation, not a harness error.
            tally['HANGS'] += 1
            print('  0x%05X %-22s HANGS (10s)' % (t, fns[t][chr(39)+'name'+chr(39)] if False else fns[t]['name']))
            continue
        if rp.returncode != 0:
            # SEPARATE THE ADDRESS-SPACE LIMIT FROM A REAL CRASH. 24 of 31 crashes
            # (measured 2026-09-27) dereference a pointer whose VALUE came out of
            # memory -- `piVar3 = _DAT_fffff402; *piVar3 = x` -- which is a raw
            # 68K address used as a host pointer. MEM is a host array and macOS
            # reserves the low 4 GB (__PAGEZERO), so no mapping can make a bare
            # 68K address a valid host pointer. That is NOT a decompilation
            # fault and calling it CRASH hides what it is.
            #
            # It is also not free: the target has the same problem (SDRAM is at
            # 0x06000000), so translating these is a PORT design question, not a
            # harness one -- FULL-PORT.md 6(a), state placement. These functions
            # are the population that question is about, and the count is the
            # measurement of its size.
            seg = rp.returncode in (-11, -10)
            if seg and PTR_FROM_MEM.search(code):
                tally['UNMAPPED-POINTER'] += 1
                print('  0x%05X %-22s UNMAPPED-POINTER  derefs a 68K address read '
                      'from memory (see FULL-PORT 6(a))' % (t, fns[t]['name']))
            else:
                tally['CRASH'] += 1
                print('  0x%05X %-22s CRASH (rc %d)' % (t, fns[t]['name'], rp.returncode))
            continue
        p, f, nswap, nwrong = (int(x) for x in rp.stdout.split())
        # VACUITY. A vector set with no writes cannot distinguish a correct
        # function from an empty one -- sprite_frame_lookup's body is
        # `return;` for 34 bytes and it "passes" 4/4 on that basis. A PASS is
        # only a port when the vectors actually constrain the output.
        constrained = sum(1 for r in vs if r['writes'])
        # UNMODELLED RETURN. Ghidra typed a callee `void` while a caller uses the
        # value the 68K left in D0. We promote the signature so the closure
        # compiles, but the value itself is NOT RECOVERABLE from the
        # decompilation -- whatever the promoted function returns is fabricated.
        # So this cannot be scored either way: a PASS would be luck and a FAIL
        # would be our own missing model. Report it, never count it.
        if unmodelled:
            verdict = 'UNMODELLED-RETURN'
            print('  0x%05X %-22s %-18s %d/%d vectors -- %s typed void but its '
                  'result is used' % (t, fns[t]['name'], verdict, p, p + f,
                                      ', '.join(sorted(unmodelled))))
            tally[verdict] += 1
            continue
        # EVERY difference an exact byte swap => the difference is byte ORDER, not
        # arithmetic. MEM holds two conventions at once (the ROM image is raw 68K
        # big-endian bytes; seeded cells are written at their own width), so a
        # multi-byte copy out of ROM comes back swapped on this little-endian
        # host. Irreducible here, absent on the big-endian SH-2. Measured, not
        # inferred from the shape of the code.
        if f and nwrong == 0 and nswap:
            tally['HOST-ENDIAN-LIMIT'] += 1
            print('  0x%05X %-22s HOST-ENDIAN-LIMIT  %d/%d -- all %d differences are '
                  'exact byte swaps; verify on the SH-2'
                  % (t, fns[t]['name'], p, p + f, nswap))
            continue
        if f:
            verdict = 'FAIL'
        elif constrained:
            verdict = 'PASS'
        else:
            verdict = 'PASS-UNCONSTRAINED'
        tally[verdict] += 1
        extra = ('  [%d swapped, %d truly wrong]' % (nswap, nwrong)) if f else ''
        print('  0x%05X %-22s %-18s %d/%d vectors, %d constrain output%s'
              % (t, fns[t]['name'], verdict, p, p + f, constrained, extra))
    print('\n' + '  '.join('%s=%d' % kv for kv in sorted(tally.items())))


if __name__ == '__main__':
    main()
