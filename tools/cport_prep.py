#!/usr/bin/env python3
"""Make Ghidra's decompiled C compilable for SH-2.

    tools/cport_prep.py docs/proof/altbeast.c OUTDIR

Emits into OUTDIR:
    ghidra_types.h    Ghidra's output typedefs (freestanding: no stdint.h,
                      matching the project's -ffreestanding build)
    ghidra_rt.h       the decompiler's intrinsics (CONCATnn, SUBnn, ZEXT, SEXT)
    dat_syms.h        a DECLARATION for every DAT_/_DAT_ symbol, placed at its
                      address, split by region
    altbeast_port.c   the corpus with the headers included

WHY THIS IS MECHANICAL. 32X SH-2 is BIG-ENDIAN (elf32-sh, -m2), the same as the
System 16 68000, so struct layout, byte order and word packing carry over with
no rewrite. What Ghidra leaves behind is only: its own type names, its
intrinsics, and undeclared absolute-address globals. All three are generated,
not hand-written.

WHAT THIS CANNOT FIX, and it is the real remaining work: functions whose
parameters arrive in address registers show up as `unaff_A6` / `extraout_A0`,
because Ghidra has no calling convention for this program. The 68K game passes
the current object in a6 and the slot base in a4 (the hand translation
docs/design/collision_sh2.s fixes r14/r4 for exactly those). Those functions
need the convention declared in Ghidra before their C means anything.
"""

# ---- A PASS THAT MATCHES NOTHING IS A BUG IN THE PASS, NOT A QUIET DAY ---------------
# 2026-10-04: three passes in one session silently matched zero, each because the text
# at this stage did not look the way it was assumed to (the gcc feedback loop adds the
# casts LATER; CPORT_STUBS redirects by #define, not by rewriting; `out` is a list of
# CHUNKS with the whole corpus in one element). Each cost a build. So a pass states
# what it expects and the build FAILS if it gets zero -- the count is checked, not read.
def _pass_report(label, n, expect_min=1):
    sys.stderr.write('cport_prep: %s: %d\n' % (label, n))
    if n < expect_min:
        raise SystemExit('cport_prep: PASS MATCHED NOTHING -- "%s" found %d, expected at '
                         'least %d. The text at this stage does not look the way the '
                         'pass assumes; dump it before fixing the regex.' % (label, n, expect_min))
import os
import re, sys, os, json, collections

src_path, outdir = sys.argv[1], sys.argv[2]
os.makedirs(outdir, exist_ok=True)
src = open(src_path).read()

# ---- `ram0xNNNNNNNN` IS A DAT SPELLING GHIDRA INVENTS, AND IT FELL THROUGH --------
#
# Ghidra occasionally names a plain memory reference `ram0x000070be` instead of
# `DAT_000070be`. cport_layout.py only recognises the decompiler spellings
# (PTR_|switchD?_|LAB_|s_) at cport_layout.py:83, so this one matched nothing, got no
# ROM placement, and fell to the FABRICATED-GLOBAL path: defs.c declared
# `uint ram0x000070be;` in .bss, reading a constant ZERO.
#
# WHAT IT COST (advisor, Mike's sw271 state): the round-clear release object's
# countdown. 0x6190 is `move.w 0x70be,d0 ; move.w d0,(62,fp)` and ROM 0x70BE is
# 0x00D2 = 210; the handler at 0x619A sets 0xFFF14B when +0x3E counts down to ROM
# 0x70C0 = 180, thirty frames later. Starting from 0 instead, +0x3E wrapped and the
# match was ~65,000 frames away -- the head hangs on screen after Neff.
#
# RENAMED, NOT REGEX-WIDENED, BECAUSE WIDTH IS THE POINT. The 68K reads a WORD. Give
# the layout a `ram0x` symbol and it would place a uint at 0x70BE, which reads
# 0x00D200B4 -- and `*(undefined2 *)(fp + 0x3e) = ...` keeps the LOW half, 0x00B4 =
# 180, so the compare fires on the FIRST frame instead of never. As DAT_000070be it
# joins its neighbour DAT_000070c0, which dat_syms.h already declares `extern ushort`
# and layout.ld places at ROM 0x024070C0.
src, _ramdat = re.subn(r'\bram0x0*([0-9a-fA-F]{1,8})\b',
                       lambda m: 'DAT_%08x' % int(m.group(1), 16), src)
_pass_report('ram0xNNNN spellings renamed to DAT_ (so they get a ROM word, not .bss)',
             _ramdat, 1)

ROOTS = os.getenv('CPORT_ROOTS')
if ROOTS:
    _blk, _byaddr = {}, {}
    for _b in re.split(r'(?=/\* ---- 0x)', src):
        _m = re.match(r'/\* ---- 0x([0-9A-F]+)\s+(\S+)\s+\((\d+) B\)', _b)
        if _m:
            _a = int(_m.group(1), 16)
            _blk[_m.group(2)] = (_a, _b); _byaddr[_a] = _m.group(2)
    _KW = {'if', 'while', 'for', 'return', 'switch', 'sizeof', 'do', 'else', 'goto',
           'short', 'ushort', 'uint', 'byte', 'char', 'int', 'long', 'void', 'bool',
           'code', 'unsigned', 'signed', 'volatile', 'undefined', 'undefined2',
           'undefined4', 'ulonglong', 'longlong', 'uchar', 'ulong'}

    def _cal(nm):
        b = _blk[nm][1]
        out = {c for c in re.findall(r'\b([A-Za-z_]\w*)\s*\(', b)
               if c not in _KW and c != nm}
        out |= (set(re.findall(r'\b([A-Za-z_]\w*)\b', b)) & set(_blk)) - {nm}
        return out

    _roots = set()
    for _r in ROOTS.split(','):
        _r = _r.strip()
        if _r in _blk:
            _roots.add(_r)
        elif _r.startswith('0x') and int(_r, 16) in _byaddr:
            _roots.add(_byaddr[int(_r, 16)])
    # CPORT_HANDLERS is a FILE PATH, not a name list, and it is parsed for HEX
    # ADDRESSES. Passing names to it is a SILENT no-op -- measured 2026-09-28: a run
    # with 99 handler names in the variable reported the same 27 functions as a run
    # with none, and nothing said why.
    _hf = os.getenv('CPORT_HANDLERS')
    if _hf:
        if not os.path.exists(_hf):
            raise SystemExit('CPORT_HANDLERS=%r is not a file. It must be a PATH to a '
                             'file of hex addresses (docs/proof/object_handlers.txt), '
                             'not a list of names.' % _hf)
        _unsplit = []
        for _h in re.findall(r'0x([0-9A-Fa-f]{4,6})', open(_hf).read()):
            _a = int(_h, 16)
            if _a in _byaddr:
                _roots.add(_byaddr[_a])
            else:
                _unsplit.append(_a)
        # NOT an error, but never silent: these are real handler addresses the
        # object table holds that are not function STARTS, so Ghidra merged them
        # into a neighbour and the port has no entry point for them
        # (FULL-PORT.md 12). They need re-decompilation with these as entry points.
        if _unsplit:
            print('CPORT_HANDLERS: %d of %d addresses are NOT function starts and '
                  'were DROPPED -- Ghidra must be re-run with them as entry points: %s'
                  % (len(_unsplit), len(_unsplit) + len(_roots),
                     ' '.join('0x%05X' % a for a in sorted(_unsplit))))
    _seen, _st = set(_roots), list(_roots)
    while _st:
        _x = _st.pop()
        for _c in _cal(_x):
            if _c in _blk and _c not in _seen:
                _seen.add(_c); _st.append(_c)
    _keep = ''.join(_blk[n][1] for n in sorted(_seen, key=lambda q: _blk[q][0]))
    print('reachable set: %d of %d functions kept' % (len(_seen), len(_blk)))
    src = src[:src.index('/* ---- 0x')] + _keep

# globals gcc reported as too narrow, and the unresolved func_0x targets.
#
# PER TREE AND PER BUILD. This file used to be a fixed /tmp path and its `undeclared`
# list is a UNION that only ever grows (cport_build.sh, `u |= ...`), so it leaked two
# ways: across time, and BETWEEN TREES -- the main checkout and
# .claude/worktrees/advisor-bisect share /tmp, so one thread's harvest fabricated
# globals in the other thread's rom. Measured 2026-10-20: the copy on this machine was
# two days old and still carried ram0x000070be, which kept a dead .bss global in every
# rom after the symbol had been renamed to DAT_000070be. It is not only dead weight --
# a defs.c definition WINS over layout.ld's PROVIDE (see the note at the fabrication
# loop below), so a stale entry can override a correct ROM placement.
# cport_build.sh now points CPORT_OVERRIDES at $OUT and deletes it before its pass
# loop, so the union still accumulates across the five passes of ONE build and starts
# empty for the next.
_ovp = os.getenv('CPORT_OVERRIDES') or '/tmp/cport_overrides.json'
try:
    _ov = json.load(open(_ovp))
except Exception:
    _ov = {}
WIDE = set(_ov.get('wide', []))
# GHIDRA'S OWN WIDTH per address (tools/ghidra/f1_datwidth.py). Guessing widths
# from the symbol's first letter declared `_DAT_fffff014` -- the PRNG state that
# rng_next multiplies by 0x29 and shifts right 16 -- as a `byte`, and 89 symbols
# were byte while used in wide arithmetic. That alone makes a CORRECT
# decompilation fail its differential: rng_next's C reproduces both oracle
# vectors exactly when evaluated at the right width. Derive, do not guess.
# NOT OPTIONAL. This used to be `except Exception: DATW = {}`, which would have
# declared every one of the 771 functions' globals 1 byte wide -- silently, and
# the port would still compile. Verified 2026-09-27 that the table WAS present
# for the run that produced the current port (uint _DAT_fffff014), but nothing
# in the tool would have said otherwise. A missing input stops the generator.
DATW_PATH = 'docs/proof/datwidth.json'
try:
    DATW = json.load(open(DATW_PATH))
except Exception:
    raise SystemExit('cport_prep: %s missing -- regenerate it with\n'
                     '  tools/ghidra_run.sh tools/ghidra/f1_datwidth.py\n'
                     'Refusing to guess widths: the 1-byte default declares every '
                     'global wrong and still compiles.' % DATW_PATH)
# A global the decompiler DEREFERENCES (`*_DAT_fffff0ec = ...`) holds a pointer,
# so `uint` is the wrong declaration and gcc says "invalid type argument of
# unary '*'". Detect it from the source rather than from a list.
# the lowest work-RAM address any symbol names -- the same origin cport_layout.py
# uses for _WRAM_BASE, so W68PTR and the placed symbols agree.
_wr = [0xFF0000 | (int(h, 16) & 0xFFFF)
       for h in re.findall(r'\b_?(?:DAT|[a-z]{1,3}Ram)_?([0-9a-f]{4,8})(?:_\d+)?\b', src)
       if 0xFFFF0000 <= int(h, 16) <= 0xFFFFFFFF]
_WLO = min(_wr) if _wr else 0xFFC000

DEREF = set(re.findall(r'\*\s*(_?(?:DAT|[a-z]{1,3}Ram)_?[0-9a-f]{4,8}(?:_\d+)?)\b', src))

# ---- THE POINTEE WIDTH OF A STORED POINTER ---------------------------------------
#
# Every cell in DEREF is typed `uint *`, so every store through one is a 32-BIT
# store. The 68000's was whatever its instruction said, and for the S16 LAYER
# REGISTERS it said 16:
#
#     2b02: 2078 f0ec    moveal 0xfffff0ec,%a0    ; A0 = the pointer
#     2b06: 3038 f0f4    movew  0xfffff0f4,%d0    ; D0 = the value
#     2b0a: 3080         movew  %d0,%a0@          ; MOVE.W -- 16 bits
#
# (roms/altbeast/prog68k.bin, disassembled from the 0x2ACE boundary.) The port stored
# 32, so writing 0x0101 put 0x0000 in 0x410E80 and 0x0101 in 0x410E82 -- the value
# landed one register late and the page register read zero.
#
# WHAT THAT COST. 0x410E80/82 are the tilemap PAGE ASSIGNMENT registers.
# latch_layer_regs feeds them to decode_pages, so a zero there means "page 0
# everywhere" and the compose draws a blank screen. MEASURED 2026-10-03: a
# port-alone build had the right source values in work RAM (0xFFF0F4 = 0x0101,
# 0xFFF0F6 = 0x5656, both matching the arcade oracle exactly) and all zeros in the
# registers, with every master compose counter at 0 against the line's 1394.
#
# Keyed by cell address; the value is the pointee type. CPORT_PTRW adds to it
# (CELL=BITS, comma-separated) so the gate can arbitrate another one without an edit.
_PTR_POINTEE = {0xFFFFF0EC: 'ushort',    # 0x410E80, tilemap page assignment, proven above
                0xFFFFF0F0: 'ushort'}    # 0x410E82, the same store two instructions on
for _pw in filter(None, os.getenv('CPORT_PTRW', '').split(',')):
    _pc, _pb = _pw.split('=')
    _PTR_POINTEE[int(_pc, 16) | 0xFFFF0000 if int(_pc, 16) < 0x1000000 and int(_pc, 16) >= 0xF00000
                 else int(_pc, 16)] = {8: 'byte', 16: 'ushort', 32: 'uint'}[int(_pb)]
FUNCPTRS = [f for f in _ov.get('intr', []) if f.startswith('func_0x')]

open(os.path.join(outdir, 'ghidra_types.h'), 'w').write("""\
/* GENERATED by tools/cport_prep.py -- Ghidra output types, freestanding. */
#ifndef GHIDRA_TYPES_H
#define GHIDRA_TYPES_H
typedef unsigned char       byte, undefined, undefined1, uchar;
typedef unsigned short      ushort, undefined2, word;
typedef unsigned int        uint, undefined4, dword, ulong;
typedef unsigned long long  undefined8, qword, ulonglong;
typedef long long           longlong;
typedef unsigned int        undefined3, undefined5, undefined6, undefined7;
typedef unsigned int        uint3, uint5, uint6, uint7;
typedef int                 int3, int5, int6, int7;
/* Ghidra's marker for an unresolved address space, used as a cast target. */
typedef byte                BADSPACEBASE;
typedef uint                code();       /* a call TARGET, so `code *p`\n                                           * is a function pointer and\n                                           * (*p)() is a call */
typedef signed char         sbyte;
typedef short               sword;
typedef int                 sdword;
/* Ghidra emits `bool`, `true`, `false`. Freestanding C99 has no stdbool.h. */
typedef int                 bool;
#define true  1
#define false 0
/* PORTHOT2 (2026-10-24): the profile-chosen hot set (tools/hot_set.txt) to SDRAM regions
 * A (.ramtext2, above .bss) and B (.ramtext3); the redeclarations at the end of protos.h and
 * cport_dispatch's thunks use these. Empty without -DPORT_HOT2. */
#ifdef PORT_HOT2
#define PORTHOT2 __attribute__((section(".ramtext2")))
#define PORTHOT3 __attribute__((section(".ramtext3")))
#else
#define PORTHOT2
#define PORTHOT3
#endif
#endif
""")

# ---- RAW VIDEO-ADDRESS LITERALS -------------------------------------------------
#
# A 68K video address that reaches the port as a NAMED symbol is already handled:
# cport_layout places DAT_00400518 at its shadow. One that reaches it as a bare
# INTEGER is not, and the decompiler emits plenty:
#
#     FUN_00000d84(0x400518, 0xf, ...);          the callee dereferences it
#     puVar5 = (undefined4 *)0x405598;
#
# On the 68000 those ARE the hardware. On the SH-2, 0x400518 is nowhere, so the
# write vanished silently -- the single hardest failure mode this port has, because
# nothing faults and the only symptom is a blank layer.
#
# MEASURED 2026-10-03: a cold-booted port-alone build cleared its tile RAM at the
# state transition and then never refilled it (0 non-zero words, every sample from
# frame 1500 on), while the MAME arcade at the SAME state had pages 0/1/5/6 holding
# 1152/468/462/300 words. The tile builder shipped 1 tile against the line's 3,172.
#
# WHY A COMPILE-TIME REWRITE AND NOT A RUNTIME CAST. A68ANY discriminates three
# domains at runtime -- SH-2 pointer, work RAM, program ROM -- and video is a FOURTH
# it never had. But a literal needs no discrimination: its domain is known when it is
# written down, so the map is exact and costs nothing at runtime. Only literals that
# land INSIDE a mapped shadow are rewritten; anything in a region's tail (tile pages
# 13-15, sprite records 64+) is left alone and reported, because moving it would
# silently alias a different page.
#
# The shadow bases are parsed from sh_src/sdram_map.h, the same single source
# cport_layout derives VIDREG from and the renderer compiles -- not copied, so none can drift.
_mm = open(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                        'sh_src', 'sdram_map.h')).read()
_shad = {}
for _m in re.finditer(r'#define\s+SDRAM_(TILEMAP_U|TEXT_U|PAL_SH|DIAG|SPR_SNAP|SYNC)\s+'
                      r'0x([0-9A-Fa-f]{8})u?\b', _mm):
    _shad[_m.group(1)] = int(_m.group(2), 16) & 0x0FFFFFFF | 0x06000000
_vmiss = [n for n in ('TILEMAP_U', 'TEXT_U', 'PAL_SH', 'DIAG', 'SPR_SNAP', 'SYNC')
          if n not in _shad]
if _vmiss:
    raise SystemExit('cport_prep: sh_src/sdram_map.h no longer defines SDRAM_%s -- the video '
                     'literal map cannot be derived' % ', SDRAM_'.join(_vmiss))
_VIDMAP = tuple((_lo, _lo + (_shad[_nx] - _shad[_f]), _shad[_f], _nm) for _lo, _nm, _f, _nx in (
    (0x400000, 'tile',    'TILEMAP_U', 'TEXT_U'),
    (0x410000, 'text',    'TEXT_U',    'PAL_SH'),
    (0x840000, 'palette', 'PAL_SH',    'DIAG'),
    (0x440000, 'sprite',  'SPR_SNAP',  'SYNC'),
))

_VIDTB = [m for m in _VIDMAP if m[3] == "tile"][0][2]
_VIDTL = [m for m in _VIDMAP if m[3] == "tile"][0][1] - 0x400000

open(os.path.join(outdir, 'w68.h'), 'w').write("""\
/* GENERATED by tools/cport_prep.py -- 68K ADDRESS FORMS the port has to translate.
 *
 * The 68000 reached work RAM two ways the SH-2 cannot: absolute-short addressing
 * (a 16-bit displacement sign-extended to 0xFFxxxx) and pointers stored as 16 bits.
 * Both name a real work-RAM cell; neither is a value an SH-2 pointer can hold. The
 * port keeps work RAM as one contiguous block at _WRAM_BASE (tools/cport_layout.py),
 * so both translate by one affine map.
 *
 * Validated on hardware before being brought into the port: the same two rewrites
 * took the SH-2 self-test to 86 of 101 arcade-oracle vectors.
 */
#ifndef W68_H
#define W68_H
extern char WRAM_BASE[], ROM_BASE[];   /* layout.ld defines _WRAM_BASE /
 * _ROM_BASE in the LINKER namespace; the SH-ELF compiler adds the underscore,
 * so C names them WITHOUT it. */
#define WRAM_68K_ORIGIN 0x%06Xu        /* the lowest work-RAM address the port uses */
#define W68PTR(a68)  ((char *)WRAM_BASE + ((a68) - WRAM_68K_ORIGIN))

/* THE SIGN DECIDES THE REGION, and dropping it was a real bug. The 68000 SIGN-EXTENDS
 * a 16-bit absolute address: 0xC880 becomes 0xFFFFC880, i.e. work RAM 0xFFC880 -- but
 * 0x0100 becomes 0x00000100, which is PROGRAM ROM, not work RAM. An unconditional
 * `0xFF0000 | v` maps every POSITIVE short into work RAM and is simply wrong for
 * them. Branch on the sign, exactly as cport_layout.py's region() does.
 *
 * (The `(unsigned)` casts this replaces were also redundant: `& 0xFFFF` already
 * discards the sign extension, because `short & unsigned` promotes to int first.
 * Mike, 2026-09-27: "Why unsigned though? especially if we are using the SH-2 and
 * 32bit pointers already?" -- the casts were noise, and they hid the sign.) */
#define W68(v)       ((short)(v) < 0 \
                        ? W68PTR(0xFF0000u | ((v) & 0xFFFFu)) \
                        : (char *)ROM_BASE + ((v) & 0xFFFFu))

/* A 68K WORK-RAM ADDRESS AND AN SH-2 WORK-RAM POINTER ARE DISJOINT BY CONSTRUCTION,
 * which is what makes this exact rather than a guess:
 *
 *     68K work RAM   0xFFC000-0xFFFFFF, and its sign-extended form 0xFFFFC000+
 *     the port's     _WRAM_BASE (0x0602CC00) and 16 KB
 *
 * Nothing in the decompiled C tells them apart -- Ghidra renders both as `int` --
 * but their RANGES never overlap, so a value can be classified at runtime with no
 * ambiguity. `walk_player_slots` calls FUN_0000D608 with an SH-2 pointer twice and
 * with the bare constants 0xffffc000 and 0xffffc400 twice, into ONE `int` parameter
 * that the body dereferences; on the SH-2 those constants address the ON-CHIP
 * PERIPHERAL REGISTER FILE (0xFFFF8000-0xFFFFFFFF on the SH7604), so a collision
 * outcome was being decided by a peripheral register. FULL-PORT.md 44-45.
 *
 * ROM is deliberately NOT handled: a small 68K ROM address is indistinguishable from
 * a plain integer, and guessing there is what produced 339 errors the last time.
 * Work RAM is the range that can be decided. */
#define A68IS(v)     ((((((unsigned)(v)) >> 24) == 0xFFu)                         || ((((unsigned)(v)) >> 24) == 0x00u))                        && ((((unsigned)(v)) & 0xFFFFFFu) >= WRAM_68K_ORIGIN))
#define A68NORM(v)   (A68IS(v)                         ? (unsigned)((char *)WRAM_BASE                                     + ((((unsigned)(v)) & 0xFFFFFFu) - WRAM_68K_ORIGIN))                         : (unsigned)(v))

/* THE INVERSE MAP: an SH-2 work-RAM pointer back to the 68K address a 16-bit cell
 * is supposed to hold. The decompiled program stores pointers in `short`s --
 * `sVar11 = (short)psVar10;` -- because on the 68000 truncating a work-RAM pointer
 * to 16 bits and sign-extending it back is the IDENTITY. On the SH-2 work RAM is at
 * _WRAM_BASE, so the truncation destroys the pointer; storing A68OF(p) instead
 * keeps the round trip exact, because W68 is its inverse.
 *
 * Only meaningful for a pointer INTO work RAM, which is what every such site holds
 * (measured: 2 sites, FUN_00002E50 and FUN_0000E16A). */
#define A68OF(p)     ((unsigned)((const char *)(p) - (const char *)WRAM_BASE) \
                        + WRAM_68K_ORIGIN)

/* THE SAME INVERSE, DOMAIN-GUARDED -- A68NORM's exact mirror.
 *
 * A68OF above is unconditional and its own comment says so: "only meaningful for a
 * pointer INTO work RAM". That is fine at the two sites it was written for and wrong
 * anywhere the value might ALREADY be a 68K address -- which is most places, because
 * A68NORM exists precisely because both domains reach the same parameter. This one
 * tests the range first and is the IDENTITY otherwise, so it can be applied without
 * knowing which domain a value is in.
 *
 * MEASURED 2026-10-03 (FULL-PORT.md 65): FUN_0000D608 registers a hit with
 * `move.w A4,($4c,A6)` -- the defender records WHO hit it, as the attacker's address
 * truncated to 16 bits. The 68000 stores 0xC000 (A4 = 0xFFFFC000, the player); the
 * port stored 0xCC from its SH-2 pointer 0x0602CC00, so the defender recorded a hit
 * from an object that does not exist. On a re-seeded frame 145 that single byte,
 * 0xFFC84C, was the ONLY wrong object byte in all 16 KB. */
#define A68SH(v)     ((((unsigned)(v)) - ((unsigned)(char *)WRAM_BASE)) < 0x4000u)
#define A68OF16(v)   (A68SH(v) \
                        ? ((((unsigned)(v)) - ((unsigned)(char *)WRAM_BASE)) \
                           + WRAM_68K_ORIGIN) \
                        : (unsigned)(v))

/* THREE DOMAINS, NOT TWO -- and A68NORM only ever handled two.
 *
 * A68NORM maps a 68K WORK-RAM address to the port's block and leaves everything else
 * alone, which is right when the only two things that can reach a parameter are work
 * RAM and an SH-2 pointer. Some parameters take a 68K ROM address as well, and for
 * those A68NORM is the identity -- so the callee dereferences a 68K ROM address as if
 * it were an SH-2 one.
 *
 * MEASURED 2026-10-03 (FULL-PORT.md 66): FUN_0000D868 is the object allocator, and
 * after claiming a slot it fills the record from the SPAWN-TABLE ENTRY its caller
 * passed -- `*(short *)(p_A0 + 2)`, `+4`, `+6`. That pointer is 68K 0x1D4D6, in
 * program ROM. The slot was allocated and its 31 remaining bytes stayed zero, so the
 * arcade's frame-158 spawn produced an object with no position, no sprite and no
 * script.
 *
 * P68 is NOT usable here: it maps anything below 0xFFC000 into ROM, so it would
 * corrupt an SH-2 pointer. This tests the SH-2 range FIRST (A68SH), then work RAM,
 * then the 68K's 256 KB program, and is the identity for anything else. The three
 * ranges are disjoint by construction, which is the same argument A68NORM's own
 * comment makes -- extended by one domain rather than replaced. */
#define A68ANY(v)    (A68SH(v) ? (unsigned)(v) \
                      : A68IS(v) \
                        ? (unsigned)((char *)WRAM_BASE \
                                     + ((((unsigned)(v)) & 0xFFFFFFu) - WRAM_68K_ORIGIN)) \
                      : ((((unsigned)(v)) & 0xFFFFFFu) < 0x40000u \
                         && (((unsigned)(v)) >> 24) == 0u) \
                        ? (unsigned)((char *)ROM_BASE + (((unsigned)(v)) & 0x3FFFFu)) \
                        : (unsigned)(v))

/* ---- THE VIDEO DOMAIN, BUILT AT RUNTIME --------------------------------------
 *
 * A68ANY knows three domains -- SH-2 pointer, work RAM, program ROM -- and S16
 * VIDEO is a fourth it never had. The decompiler builds one at runtime:
 *
 *     puVar2 = (undefined2 *)CONCAT22(0x40,*p_A0);     FUN_0000258A
 *
 * (0x40 << 16) | a low word read from the data = a tile-RAM address 0x40xxxx,
 * assembled on the fly. On the 68000 that IS the hardware; on the SH-2 0x0040xxxx
 * is nowhere, so the store vanished with no fault.
 *
 * MEASURED 2026-10-03: a MAME debugger watchpoint on the arcade's tile page 0 across
 * the frame it refills caught 1,152 writes -- exactly the word count the page gains
 * -- and resolved every one to FUN_0000258A/FUN_00002552 (`move.w (A0)+,(A2)+` and
 * its dbra). CPORT_COUNT on the port says both RUN (258a 14 times, 25ac 182) and
 * TILEMAP_U stayed empty, which is the signature of a store landing off-map rather
 * than of code that never executes.
 *
 * Exact, not a guess: the tile window is the 13 pages the renderer's shadow covers,
 * and an address past it is left as the bare 68K value so it stays visible rather
 * than silently aliasing page 0. Same argument as _SHADOW_TAIL in cport_layout. */
#define A68VID(v)    ((((unsigned)(v)) & 0xFFFFu) < 0x%Xu \
                        ? 0x%08Xu + (((unsigned)(v)) & 0xFFFFu) \
                        : (unsigned)(v))

/* THE SAME MAP OVER ALL FOUR VIDEO REGIONS, AND IDENTITY OUTSIDE THEM.
 *
 * A68VID above is for a value KNOWN to be tile RAM. This one is for a pointer whose
 * domain is not known until it is read -- which is any video pointer kept in WORK
 * RAM, because a work-RAM seed restores the ARCADE's value and that is a 68K address,
 * while the port's own boot stores an SH-2 one. Both reach the same dereference.
 *
 * MEASURED 2026-10-03: after seeding the arcade's attract state into a running port,
 * 0xFFF0EC held 0x00410E80 where the port's boot had put 0x06026E80, and the two
 * tilemap PAGE registers written THROUGH it read zero while the four written to
 * direct symbols in the same block were live (0x0020, 0x00EF). That split -- direct
 * writes fine, pointer writes dead -- is what names this.
 *
 * Identity outside the four regions, so an SH-2 pointer passes through untouched:
 * the ranges are disjoint by construction, the same argument A68NORM's own comment
 * makes. */
#define A68VIDA(v)   ((((unsigned)(v)) >= 0x400000u && ((unsigned)(v)) < 0x40D000u) \
                        ? 0x%08Xu + (((unsigned)(v)) - 0x400000u) \
                      : (((unsigned)(v)) >= 0x410000u && ((unsigned)(v)) < 0x411000u) \
                        ? 0x%08Xu + (((unsigned)(v)) - 0x410000u) \
                      : (((unsigned)(v)) >= 0x840000u && ((unsigned)(v)) < 0x850000u) \
                        ? 0x%08Xu + ((((unsigned)(v)) - 0x840000u) & 0xFFFu) \
                      : (((unsigned)(v)) >= 0x440000u && ((unsigned)(v)) < 0x440400u) \
                        ? 0x%08Xu + (((unsigned)(v)) - 0x440000u) \
                      : (unsigned)(v))

/* ANY DOMAIN TO AN SH-2 POINTER: video first (A68VIDA), then work RAM / program ROM
 * (A68ANY). Both are the identity for an SH-2 pointer, so this is safe to apply to a
 * parameter whose domain depends on the caller -- which is every pointer the arcade's
 * video writers receive (tools/video_writers.lua census, 2026-10-04). */
#define A68PTR(v)    A68ANY(A68VIDA(v))

/* THE SPRITE-LIST RECORD OF AN OBJECT, FORMED IN 16 BITS (2026-10-23). The 68K builds it as
 * `moveq #0,d6 ; move.b 8(fp),d6 ; lsl #4,d6 ; addw #$F800,d6 ; movea.w d6,a0` (0x3EB8 the
 * record builder, 0x3F04 hide_object_sprite; two sites in the program, objdump census). The
 * sum is a WORD: an index of 0x80 or more wraps past 0xFFFF, movea.w sign-extends the low
 * 16 bits, and the record writes land at 0x000000+ -- program ROM, where the 68000 drops
 * them. The decompile wrote W68PTR(0xFFF800) + index*16 in 32 bits, which for the 0xFF
 * that the despawn walk 0xE16A leaves on an unindexed record reaches SDRAM 0x313F0,
 * inside .ramtext (r60_punch). Indexes 0x40-0x7F land in work RAM 0xFFFC00-0xFFFFF0 on
 * both machines and stay there. The wrapped case writes a 16-byte sink nothing reads. */
extern char port_spr_sink[16];
#define SPRREC(i)    (((unsigned)(i) < 0x80u) ? (W68PTR(0xFFF800u) + (unsigned)(i) * 0x10u) : port_spr_sink)

/* CPORT_ARGLOG's ring (sh_src/port_l2.c): 16 pairs of (id<<24 | port_fn_seq, value) -- .bss has no room for more. */
extern volatile unsigned port_fn_seq, port_watch_out[192];
/* The ring rides port_watch_out[160..176] (PORTWATCH's block, idle in a play build):
 * .bss had no room, and SDRAM scratch 0x0603A700 was found 0x01010101-filled by
 * the renderer (m_main.c 753: "that window is NOT free"). */
#define port_arglog   (&port_watch_out[160])
#define port_arglog_n (port_watch_out[176])
#define PORT_ARGLOG(id, v) do { unsigned _i_ = (port_arglog_n++) & 7u; \
        port_arglog[_i_ * 2] = ((unsigned)(id) << 24) | (port_fn_seq & 0xFFFFFFu); \
        port_arglog[_i_ * 2 + 1] = (unsigned)(v); } while (0)
#endif
""" % (_WLO, _VIDTL, _VIDTB, _VIDTB,
       [m for m in _VIDMAP if m[3] == "text"][0][2],
       [m for m in _VIDMAP if m[3] == "palette"][0][2],
       [m for m in _VIDMAP if m[3] == "sprite"][0][2]))

open(os.path.join(outdir, 'ghidra_rt.h'), 'w').write("""\
/* GENERATED by tools/cport_prep.py -- the decompiler's intrinsics.
 * CONCATab joins an a-byte high part and a b-byte low part; SUBab takes the
 * low b bytes of an a-byte value; ZEXT/SEXT widen. Big-endian on both sides,
 * so these are plain shifts. */
#ifndef GHIDRA_RT_H
#define GHIDRA_RT_H
#include "ghidra_types.h"
#define CONCAT11(a,b) ((ushort)(((ushort)(byte)(a)<<8)|(byte)(b)))
#define CONCAT12(a,b) ((uint)(((uint)(byte)(a)<<16)|(ushort)(b)))
#define CONCAT13(a,b) ((uint)(((uint)(byte)(a)<<24)|((b)&0xFFFFFFu)))
#define CONCAT22(a,b) ((uint)(((uint)(ushort)(a)<<16)|(ushort)(b)))
#define CONCAT24(a,b) ((unsigned long long)(((unsigned long long)(ushort)(a)<<32)|(uint)(b)))
#define CONCAT31(a,b) ((uint)((((uint)(a)&0xFFFFFFu)<<8)|(byte)(b)))
#define CONCAT44(a,b) ((unsigned long long)(((unsigned long long)(uint)(a)<<32)|(uint)(b)))
#define SUB41(a,n)    ((byte)((uint)(a)>>(8*(n))))
#define SUB42(a,n)    ((ushort)((uint)(a)>>(8*(n))))
#define SUB21(a,n)    ((byte)((ushort)(a)>>(8*(n))))
#define SUB84(a,n)    ((uint)((unsigned long long)(a)>>(8*(n))))
#define ZEXT14(a)     ((uint)(byte)(a))
#define ZEXT24(a)     ((uint)(ushort)(a))
#define ZEXT12(a)     ((ushort)(byte)(a))
#define ZEXT48(a)     ((unsigned long long)(uint)(a))
#define SEXT14(a)     ((uint)(int)(signed char)(a))
#define SEXT24(a)     ((uint)(int)(short)(a))
#define SEXT48(a)     ((long long)(int)(a))
/* the decompiler's markers for unreachable / undecodable code. They are
 * assertions, not behaviour: a port that reaches one has a bug. */
extern uint halt_baddata();
extern uint halt_unimplemented();
extern uint switchD();
/* Ghidra's `func_0xNNNNNNNN(...)` form for a call it could not resolve to a
 * function: declare it variadic so any call site type-checks. */
extern uint func_0x00000000();
/* the rest of the decompiler's helpers, harvested from gcc's own
 * "implicit declaration" errors rather than guessed at. */
#define CARRY1(a,b)   ((uint)((byte)(a)   > (byte)(~(byte)(b))))
#define CARRY2(a,b)   ((uint)((ushort)(a) > (ushort)(~(ushort)(b))))
#define CARRY4(a,b)   ((uint)((uint)(a)   > (uint)(~(uint)(b))))
#define SCARRY1(a,b)  CARRY1(a,b)
#define SCARRY2(a,b)  CARRY2(a,b)
#define SCARRY4(a,b)  CARRY4(a,b)
#define SBORROW1(a,b) ((uint)((byte)(a)   < (byte)(b)))
#define SBORROW2(a,b) ((uint)((ushort)(a) < (ushort)(b)))
#define SBORROW4(a,b) ((uint)((uint)(a)   < (uint)(b)))
#define CONCAT14(a,b) ((unsigned long long)(((unsigned long long)(byte)(a)<<32)|(uint)(b)))
extern uint __m68k_trap();
extern uint bcdAdjust();          /* variadic: arity NOT guessed */
extern uint invalidateCacheLines();
extern uint stop();
#endif
""")

# every absolute-address global the decompiler referenced
syms = collections.Counter(re.findall(r'\b(_?DAT_[0-9a-f]{4,8}(?:_\d+)?)\b', src))
# Ghidra also emits typed absolute accesses as <t>RamNNNNNNNN (u/b/i/s/c...).
for m2 in re.finditer(r'\b([a-z]{1,3}Ram[0-9a-f]{4,8})\b', src):
    syms[m2.group(1)] += 1
# and the untyped extraout/unaff pseudo-registers, which are the REAL blocker
pseudo = collections.Counter(re.findall(r'\b((?:extraout|unaff|in)_[A-Za-z0-9_]+)\b', src))

_OVERLAP = {re.sub(r'_\d+$', '', n)[1:] for n in syms if n.startswith('_')}

# ADDRESS-RANGE OVERLAP, which _OVERLAP above does NOT cover: that set is about two
# SPELLINGS of one address (`DAT_x` and `_DAT_x`). This is about a declaration whose
# WIDTH swallows a different declared symbol.
#
# Measured 2026-09-30: `DAT_fffff142` is declared `ushort` by Ghidra and
# `DAT_fffff143` is declared `byte` -- two symbols, one 16-bit cell. 0xFFF142 holds
# 01 00, so the ushort reading is 0x0100 where the byte reading is 1, and the code
# is `(DAT_fffff142 << 2)` indexing a FIVE-entry pointer table: the index becomes
# 0x400 and lands 1 KB past the table. floor_collide then reads a garbage collision
# list, its branch never fires, and THREE of the four cells the multi-frame gate
# flagged are exactly the writes that branch would have made (FULL-PORT.md 22).
#
# TWO conditions, because either alone is not evidence. A 68000 may legitimately do
# `move.w` at 0xFFF142 and `move.b` at 0xFFF143 on the same word, so overlap alone
# proves nothing; and the oracle is ONE window, so "never observed wide" alone is
# only a ceiling. Together -- Ghidra says wide, the arcade was never seen accessing
# it wide, AND another declared symbol sits inside the span -- the wide reading is
# impossible rather than merely unobserved.
# A SYMBOL USED AS AN ARRAY BASE INDEXES IN ITS OWN ELEMENT SIZE. This rule is NOT
# new -- LESSONS 2026-09-27 ("Two more width rules from the same root") states it and
# tools/gen_selftest.py:665 has implemented it since, for the L1 self-test harness
# ONLY. It was never applied to the PORT's own declarations, and the port had the
# exact bug the lesson describes:
#
#   extern uint DAT_ffffec80;          and the body does (&DAT_ffffec80)[iVar1] = ...
#
# Ghidra's width is a WHOLE-SYMBOL inference -- something else in the program reads a
# long there, so it says 4 B -- but the symbol is the base of a BYTE table, so the
# index scaled by 4 and every store landed four times too far out. The oracle's
# NARROWEST observed access at that address is the authority for this use; it
# recorded 1 byte. 0xFFEC80, 0xFFECC0, 0xFFED20 and 0xFFED40 are exactly the cells
# the multi-frame gate reported wrong in the sprite-list block (FULL-PORT.md 20-23).
_ARRBASE = set(re.findall(r'&\s*(_?(?:DAT|[a-z]{1,3}Ram)_?[0-9a-f]{4,8}(?:_\d+)?)\s*\)\s*\[', src))
_ARRCLAMP = []

_DECL_ADDRS = set()
for _n in syms:
    _m = (re.search(r'([0-9a-f]{4,8})$', _n) or re.search(r'([0-9a-f]{4,8})_\d+$', _n))
    if _m:
        _DECL_ADDRS.add(int(_m.group(1), 16) & 0xFFFFFF)
try:
    DATW_OBS = json.load(open('docs/proof/datwidth_observed.json'))
    DATW_OBS_MIN = json.load(open('docs/proof/datwidth_observed_min.json'))
except Exception:
    DATW_OBS = DATW_OBS_MIN = {}
    print('cport_prep: NO docs/proof/datwidth_observed*.json -- width clamping is '
          'OFF (build it with tools/dat_width_observed.py)')
# THE STATIC WIDTH CENSUS, AND IT OUTRANKS THE RUNTIME ONE.
#
# docs/proof/datwidth_observed.json is what ONE MAME SCENE was seen doing, so it
# cannot report a width that scene never exercised. docs/audit/width_census.json is
# read out of the PROGRAM's instruction encodings (tools/cport_widths.py), so it
# covers every path the program has.
#
# 0xFFF132 is exactly the gap between them: nothing is declared at 0xFFF133/134/135,
# the runtime capture never showed the narrow access, the clamp never fired, and the
# camera's 49 movew reads were served from a 4-byte declaration whose every narrowing
# took the WRONG HALF (FULL-PORT.md 56c). The static census has it at the top of its
# mismatch list.
#
# Used only where CPORT_CLAMP names the address, so the gate still arbitrates each one
# at a time. A width census is EVIDENCE, not a licence: a long loaded into a register
# and then used `.w` is the register's low half and leaves no trace at the address.
try:
    DATW_STATIC = {k: v['dominant'] for k, v in
                   json.load(open('docs/audit/width_census.json')).items()}
except Exception:
    DATW_STATIC = {}
    print('cport_prep: NO docs/audit/width_census.json -- static width clamping is '
          'OFF (build it with tools/cport_widths.py --json docs/audit/width_census.json)')

_CLAMPED = []
# WHICH ADDRESSES MAY BE CLAMPED. The rule above selects 11, and clamping all 11 at
# once took the free-run curve 104 -> 179 wrong while provably FIXING three cells --
# one correct fix riding with several wrong ones.
#
# Two of the 11 (FFFFD400, FFFFD580) are inside the OBJECT TABLE, 0xFFC000-0xFFDFFF:
# a 64-entry array of 0x80-byte records. A declared symbol there is one FIELD of one
# record and its neighbour is another field of the same record, so "a declared symbol
# sits inside the span" carries no information -- it is true of almost every field.
# The overlap argument only means something for scalar globals.
#
# So: the object table is excluded structurally, and the remainder is opt-in by
# address via CPORT_CLAMP so the gate can arbitrate one symbol at a time.
_OBJTAB_LO, _OBJTAB_HI = 0xFFC000, 0xFFE000
_CLAMP_OK = os.environ.get('CPORT_CLAMP', 'FFFFF142')
_CLAMP_SET = None if _CLAMP_OK == 'all' else {
    x.strip().upper().lstrip('0') for x in _CLAMP_OK.split(',') if x.strip()}

def _clamp_allowed(addr):
    a = addr & 0xFFFFFF
    if _OBJTAB_LO <= a < _OBJTAB_HI:
        return False                      # a field of an object record, not a scalar
    if _CLAMP_SET is None:
        return True
    return ('%08X' % (0xFF000000 | a if a >= 0xFF0000 else a)).lstrip('0') in _CLAMP_SET

def _clamp_width(addr, w):
    """Ghidra's width, narrowed to the width the ARCADE was observed to use."""
    if w not in (2, 4):
        return w
    # AN EXPLICITLY ALLOWLISTED ADDRESS DOES NOT NEED THE OVERLAP TEST.
    #
    # "a declared symbol sits inside the span" was only ever a way of FINDING
    # candidates cheaply; the observed width is the authority on its own, and the
    # CPORT_CLAMP allowlist is already the gate's one-at-a-time arbitration. Keeping
    # the test as a hard pre-condition hid the worst instance of the class:
    #
    #   _DAT_fffff132 is declared `uint` (4) and the arcade only ever accesses it as
    #   a WORD (2). Nothing is declared at 0xFFF133/134/135, so the clamp never
    #   fired. FUN_0000E052 does `uVar2 = _DAT_fffff132` into a `ushort`, so the port
    #   read 32 bits and TRUNCATION TOOK THE WRONG HALF -- the word at 0xFFF134
    #   instead of 0xFFF132. Its exit test `uVar2 < *(ushort *)(p_A0+sVar4+2)` then
    #   never fired and the routine walked its table 172 entries too far, leaving
    #   0xFFF146 at 0x05A0 where the arcade has 0x0040 (FULL-PORT.md 40-41).
    #
    # A declaration WIDER than any observed access is not a harmless over-read: every
    # narrowing assignment off it silently takes the wrong half.
    explicit = _CLAMP_SET is not None and (
        ('%08X' % (0xFF000000 | (addr & 0xFFFFFF) if (addr & 0xFFFFFF) >= 0xFF0000
                   else (addr & 0xFFFFFF))).lstrip('0') in _CLAMP_SET)
    if not explicit and not any(((addr + b) & 0xFFFFFF) in _DECL_ADDRS
                                for b in range(1, w)):
        return w
    key = '%08X' % (0xFF000000 | (addr & 0xFFFFFF) if (addr & 0xFFFFFF) >= 0xFF0000
                    else (addr & 0xFFFFFF))
    # the STATIC census first: it sees every path, the runtime one sees one scene
    obs = DATW_STATIC.get(key) or DATW_OBS.get(key)
    if not obs or obs >= w:
        return w
    if not _clamp_allowed(addr):
        return w
    _CLAMPED.append((addr, w, obs))
    return obs


# CPORT_WIDTH: DECLARED WIDTHS TAKEN STRAIGHT FROM THE DISASSEMBLY.
#
# CPORT_CLAMP narrows a declaration to a width some census OBSERVED, and its sources
# only cover work RAM -- a census of absolute operands cannot see a cell the program
# reaches through `%a0@+` with a pointer Ghidra resolved. For those the width is still
# knowable, just by reading the instructions, so this takes it as `ADDR=N`.
#
# MEASURED 2026-10-03 (FULL-PORT.md 68). The spawn-init routines copy a per-skin
# TEMPLATE out of ROM with a post-increment walk:
#     da74: movel %a0@+,%fp@(2)      4 bytes at 0xDD86   -> the handler
#     da78: movew %a0@+,%fp@(6)      2 bytes at 0xDD8A
#     da7c: movew %a0@+,%d0          2 bytes at 0xDD8C
#     da82: movel %a0@+,%fp@(80)     4 bytes at 0xDD8E
#     da86: movel %a0@+,%fp@(36)     4 bytes at 0xDD92
# Ghidra resolved every address correctly and declared ALL FIVE as 4 bytes, so the two
# WORD reads became narrowings of a long -- which on a big-endian machine take the LOW
# half. The port stored 0x0040 where the arcade stores 0x00DD, and 0x1A where it stores
# 0x40: a spawned enemy with the wrong sprite bank and the wrong palette index.
#
# The C cannot be used to infer this. `unaff_A6[0xb] = (byte)uRam0000dd8c;` is equally
# consistent with a `moveb` at that address (the top byte), a `movew` then low byte
# (the second byte, which is what the 68000 does here) and a `movel` then low byte.
# Only the instruction says which, so the instruction is what this reads.
_CPORT_WIDTH = {}
for _x in (os.getenv('CPORT_WIDTH') or '').split(','):
    _x = _x.strip()
    if not _x or '=' not in _x:
        continue
    _a, _, _w = _x.partition('=')
    try:
        _CPORT_WIDTH[int(_a, 16) & 0xFFFFFFFF] = int(_w)
    except ValueError:
        raise SystemExit('cport_prep: CPORT_WIDTH wants ADDR=N, got %r' % _x)
if _CPORT_WIDTH:
    print('CPORT_WIDTH: %d declared width(s) forced from the disassembly: %s'
          % (len(_CPORT_WIDTH),
             ', '.join('0x%X=%dB' % (a, w) for a, w in sorted(_CPORT_WIDTH.items()))))

def _dat_type(nm, addr):
    """Ghidra's width wins; the old name heuristic is the fallback."""
    _fw = _CPORT_WIDTH.get(addr) or _CPORT_WIDTH.get(addr & 0xFFFFFF)
    if _fw in (1, 2, 4):
        return {1: 'byte', 2: 'ushort', 4: 'uint'}[_fw]
    if nm in DEREF:
        _pt = _PTR_POINTEE.get(addr) or _PTR_POINTEE.get(addr | 0xFFFF0000)
        return (_pt + ' *') if _pt else 'uint *'
    if not nm.startswith('_') and re.sub(r'_\d+$', '', nm) in _OVERLAP:
        return 'byte'          # the SMALLER of an overlapping pair; see the note above
    w = DATW.get('%08X' % addr)
    if nm in _ARRBASE:
        key = '%08X' % (0xFF000000 | (addr & 0xFFFFFF) if (addr & 0xFFFFFF) >= 0xFF0000
                        else (addr & 0xFFFFFF))
        o = DATW_OBS_MIN.get(key)
        if o and w and o < w:
            _ARRCLAMP.append((addr, w, o))
            return {1: 'byte', 2: 'ushort', 4: 'uint'}[o]
    if w in (2, 4):
        w = _clamp_width(addr, w)
    if w in (1, 2, 4):
        return {1: 'byte', 2: 'ushort', 4: 'uint'}[w]
    if w and w > 4:
        return 'byte'          # an array/struct: keep byte so &X + n is bytes
    if nm in WIDE or nm[0] in 'ui':
        return 'uint'
    return 'ushort' if nm[0] == 's' else 'byte'


REGIONS = [(0x00000000, 0x00040000, 'PROGRAM ROM (const)'),
           (0x00400000, 0x00500000, 'S16 video/IO hardware'),
           (0xFFF00000, 0x100000000, '68K WORK RAM -> ported state')]
def region(a):
    for lo, hi, name in REGIONS:
        if lo <= a < hi:
            return name
    return 'other'

lines = ['/* GENERATED by tools/cport_prep.py -- one declaration per absolute',
         ' * global the decompiler emitted. Addresses are the 68K\'s; the port',
         ' * rebases WORK RAM into SH-2 SDRAM (FULL-PORT F0(a)). */',
         '#ifndef DAT_SYMS_H', '#define DAT_SYMS_H', '#include "ghidra_types.h"', '']
by = collections.defaultdict(list)
_UNAL = []            # 32-bit cells at 2 mod 4: see the note at the emit below
_DATSFX = []          # DAT_<addr>_<N> sub-field symbols, placed at addr + N
for s, n in syms.items():
    # Ghidra's numeric disambiguator puts the suffix AFTER the address
    # (DAT_fffff032_1), so the hex is not at the end. Try the full tail first, then
    # the suffixed form -- and skip a name that carries no address rather than
    # throwing, since the catch-all would otherwise declare it `extern byte X[]`,
    # an ARRAY that silently decays to a pointer.
    _ma = re.search(r'([0-9a-f]{4,8})$', s)
    _sfx = 0
    if not _ma:
        # AND THE SUFFIX IS A BYTE OFFSET, NOT JUST A DISAMBIGUATOR. Ghidra names a
        # sub-component of a data item `<item>_<offset>`: DAT_fffff14e_1 is the SECOND
        # BYTE of the word at 0xFFF14E, i.e. 0xFFF14F. The unit proves it --
        #     _DAT_fffff14e = CONCAT11(DAT_fffff14e + '\x01', DAT_fffff14e_1);
        # CONCAT11's first argument is the HIGH byte, so the low byte it preserves has
        # to be 0xFFF14F. This line used to record the BASE address and layout.ld then
        # emitted PROVIDE(_DAT_fffff14e_1 = 0x0602FD4E), so the round advance read the
        # round number and wrote the OLD round number back into 0xFFF14F every time
        # (measured 2026-10-20). Only DAT_/xxRam_ names reach this loop; a label's
        # `_N` really is a disambiguator and is handled in cport_names.addr_of.
        _ma = re.search(r'([0-9a-f]{4,8})_(\d+)$', s)
        if _ma:
            _sfx = int(_ma.group(2))
            _DATSFX.append((s, int(_ma.group(1), 16), _sfx))
    if not _ma:
        continue
    a = int(_ma.group(1), 16) + _sfx
    by[region(a)].append((a, s, n))
for _, _, name in REGIONS + [(0, 0, 'other')]:
    if name not in by:
        continue
    lines.append('/* ---- %s: %d symbols ---- */' % (name, len(by[name])))
    for a, s, n in sorted(by[name]):
        # declared as a 4-byte cell, not a byte: the decompiler reads the same
        # address at several widths and `assignment to byte` is the result.
        # WIDE overrides come from gcc: "assignment to 'byte' from ..." names
        # exactly the globals the decompiler stores a pointer or long into.
        ty = _dat_type(s, a)
        lines.append('extern %-7s%s;   /* 0x%08X, %d refs */'
                     % (ty + ('' if ty.endswith('*') else ' '), s, a, n))
        if ty in ('uint', 'uint *') and (a & 3) == 2:
            _UNAL.append((a, s, n, ty))
    lines.append('')

# ---- MISALIGNED 32-BIT GLOBALS: THE 68000 NEEDED 2-BYTE ALIGNMENT, THE SH-2 NEEDS 4
#
# An SH7604 32-bit load or store raises a CPU ADDRESS ERROR unless the address is
# 4-byte aligned. A 68000 `.l` needed only 2, and this program uses that constantly,
# so every 68K long sitting at an address 2 mod 4 is UNREACHABLE by a plain `mov.l`.
#
# The pass further down already covers COMPUTED addresses -- `*(uint *)(EXPR)` goes
# through LD32U/ST32, packed accessors that make gcc emit byte-wise code. It does NOT
# cover a BARE NAMED GLOBAL, because the decompiler emits those as plain identifiers
# and there is no cast to rewrite. gcc then trusts the declared type's alignment and
# emits `mov.l`.
#
# MEASURED 2026-10-03, and this is how it was found rather than reasoned about:
# `_DAT_fffff132` is the camera X, 111 references, placed at SH-2 0x0602FD32 -- 2 mod
# 4. objdump on the shipped FUN_000039c8 shows `mov.l @r2,r3` and `mov.l r1,@r2`
# against exactly that address. So the camera's read AND write both fault, the camera
# NEVER ADVANCES (port 0x10CC5000 at free-run frame 80 against the arcade's
# 0x10D65000, frozen at the seed), the level's spawn-table walker FUN_0000D80E
# compares a stale camera against the next trigger, and the slot allocator
# FUN_0000D868 spawns thirteen objects the arcade never spawns -- which is most of
# the free-run divergence (FULL-PORT.md). One defect, found from the far end of a
# five-step causal chain.
#
# 41 of the 119 32-bit symbols are 2 mod 4, carrying 231 references. Among them are
# _DAT_ffffc002, _DAT_ffffd402, _DAT_ffffd482 and friends: an object record is 0x80
# bytes and its HANDLER POINTER is at +0x02, so EVERY named access to an object's
# handler is one of these.
#
# WHY A PACKED STRUCT WITH AN ASM NAME, and not something simpler. Measured all three
# on this toolchain (sh-elf-gcc 15.2.0, -m2 -mb -O2):
#     extern uint X;                      -> mov.l    (faults)
#     extern uint X __attribute__((aligned(1)))  -> mov.l  (gcc IGNORES it on a scalar)
#     packed struct + asm("X") + macro    -> mov.b sequence  (CORRECT)
# The third is also the mechanism LD32U/ST32 already use here, so it is one
# mechanism, not two.
#
# The canonical `extern` line above is left EXACTLY as it was because
# tools/cport_layout.py parses this header to place the symbols, and its regex wants
# the plain declaration. The alias declares the SAME linker symbol under a second C
# identifier; both are extern with no definition, so there is nothing to conflict.
# The macros come last, after every declaration, so they cannot rewrite one.
if _UNAL:
    lines.append('/* ---- %d MISALIGNED 32-BIT CELLS (68K long at 2 mod 4), %d refs.'
                 % (len(_UNAL), sum(n for _, _, n, _ in _UNAL)))
    lines.append(' * Reached BYTE-WISE: an SH-2 mov.l at 2 mod 4 is an ADDRESS ERROR,')
    lines.append(' * and gcc emits one for a plain declaration however it is aligned.')
    lines.append(' * tools/cport_prep.py carries the measurement that found this. ---- */')
    lines.append('typedef struct { uint  v; } __attribute__((packed, aligned(1))) _datu32;')
    lines.append('typedef struct { uint *v; } __attribute__((packed, aligned(1))) _datp32;')
    for a, s, n, ty in sorted(_UNAL):
        st = '_datp32' if ty.endswith('*') else '_datu32'
        lines.append('extern %s _una_%s asm("_%s");' % (st, s, s))
    lines.append('')
    for a, s, n, ty in sorted(_UNAL):
        lines.append('#define %s (_una_%s.v)' % (s, s))
    lines.append('')

lines += ['#endif', '']
if _DATSFX:
    # STDERR, because cport_build.sh sends cport_prep's stdout to /dev/null -- a
    # placement decision nobody can see is how the 0xFFF14E/0xFFF14F collision lasted.
    sys.stderr.write('cport_prep: %d DAT sub-field symbol(s) placed at base + suffix: %s\n'
                     % (len(_DATSFX),
                        ', '.join('%s -> 0x%06X' % (n, b + o) for n, b, o in _DATSFX)))
open(os.path.join(outdir, 'dat_syms.h'), 'w').write('\n'.join(lines))
if _ARRCLAMP:
    print('  ARRAY-BASE width from the oracle\'s NARROWEST access on %d symbol(s): %s'
          % (len(_ARRCLAMP),
             ', '.join('%06X %dB->%dB' % (a & 0xFFFFFF, w, o) for a, w, o in _ARRCLAMP)))
if _CLAMPED:
    print('  width CLAMPED to the arcade\'s observed access on %d symbol(s): %s'
          % (len(_CLAMPED),
             ', '.join('%06X %dB->%dB' % (a & 0xFFFFFF, w, o) for a, w, o in _CLAMPED)))

# FORWARD DECLARATIONS. Ghidra emits calls to FUN_xxxxxxxx before their
# definition, so without prototypes every forward call is an implicit
# declaration error. Signatures are taken from each definition verbatim.
# Functions whose result is CONSUMED cannot be void: the 68K returns in D0 and
# Ghidra did not model it, which is the single largest residual error class
# ("invalid use of void expression", 302 of them). Retype those to uint in BOTH
# the definition and the prototype -- a non-void function with no return
# statement is legal C, and closer to the hardware truth than void.
# NORMALISE EVERY FUNCTION TO `uint NAME()`.
#
# Ghidra has no calling convention for this program (FULL-PORT F0(d)), so the
# parameter lists it emits are invention: the bodies actually read their inputs
# from `unaff_A6` / `in_D0b` style pseudo-registers, which pseudoregs.h declares
# as globals. Keeping the invented parameters produced three error classes at
# once in a single translation unit -- 101 "invalid use of void expression"
# (a D0 return not modelled), 57 "too many arguments" (definition and call site
# disagreeing), and the conflicting-type errors between the two.
#
# An unprototyped `uint NAME()` accepts any call arity in C, returns a value that
# may be used or ignored, and is HONEST about what we know: the SH-2 port will
# declare real signatures as each function is understood, one at a time, against
# the L1 vectors. Until then a uniform declaration beats a fabricated one.
# Ghidra emits a C++-ish scope operator for switch-case pseudo-functions
# (switchD_00001ed2::caseD_4). Convert BEFORE normalising, or they survive as
# `void NAME(void)` and then `return 0;` conflicts with the void return.
src = src.replace('::', '__')
# Ghidra names string literals after their CONTENT, so a symbol can carry
# characters C does not allow: s_TAKYOUR_NAME__<>___RANK_SCORE_RD_00004767.
# Sanitise every such token identically in the source and in the declarations.
def _sanitise(m):
    return re.sub(r'[^A-Za-z0-9_]', '_', m.group(0))
src = re.sub(r'\bs_[^\s,;()\[\]{}]*_[0-9a-f]{4,8}\b', _sanitise, src)
# THE PARAMETER LIST IS NOW REAL AND MUST SURVIVE. 549 functions carry register
# parameters (tools/ghidra/param_fixpoint.sh), which is what makes the decompiler
# emit the argument setup at each call site instead of deleting it. Rewriting every
# signature to `NAME()` would throw that away and every call would pass nothing --
# the exact bug the parameters were declared to fix.
FUNCDEF = re.compile(
    r'^(?:([A-Za-z_][A-Za-z0-9_ \t\*]*?)\s+)??\b([A-Za-z_][A-Za-z0-9_]*)\s*\(([^;{]*)\)\s*$',
    re.M)
names = []
rettype = {}
params = {}
_seen_def = {}
# A RETURN WIDER THAN 32 BITS IS A PACKED MULTI-REGISTER RETURN AND MUST SURVIVE.
# Ghidra expresses two 68K registers left by a routine as one 8-byte value and the
# caller unpacks it -- `uVar12 = zoom_scale_lookup(); uVar10 = (uint)uVar12;
# ... (ulonglong)uVar12 >> 0x20`. Normalising every function to `uint NAME()`
# truncates that to the low register and silently discards the high one, which is
# exactly the value the caller reads. Measured: declaring return storage on 47
# producers took extraout_ reads from 912 to 292, and all of that arrives through
# these returns.
WIDE = ('ulonglong', 'longlong', 'undefined8', 'double')
def _norm(m):
    ty, nm, prm = (m.group(1) or ''), m.group(2), (m.group(3) or '').strip()
    # the map can name two functions identically; C cannot. Suffix the repeats.
    _seen_def[nm] = _seen_def.get(nm, 0) + 1
    if _seen_def[nm] > 1:
        nm = '%s__%d' % (nm, _seen_def[nm])
    names.append(nm)
    t = ty.strip()
    r = 'ulonglong' if t in WIDE else 'uint'
    rettype[nm] = r
    if prm in ('', 'void'):
        params[nm] = ''
        return '%s %s()' % (r, nm)
    params[nm] = prm
    return '%s %s(%s)' % (r, nm, prm)
# ---- A DBF COUNTER THE CALLER LOADS: FUN_00004612 (2026-10-04) ----------------------
# prog68k 0x4612: `move.b (a1)+,d1; add.b #-0x60,d1; move.b d1,(a2); addq.w #2,a2;
# bsr 0x4664; dbf d0,0x4612` -- one character every two frames, D0+1 characters, and
# D0 is loaded by the CALLER (0x45C2 `moveq #22,d0`, 0x45D6 `moveq #2,d0`). Ghidra gave
# the routine no D0 parameter and rendered the dbf as `while (extraout_D0w != 0)` with
# extraout_D0w the pointer FUN_00004664 hands back -- never zero. Measured rd63: the
# score table typed characters across text RAM for 52 s (arcade: 9.5 s, frames
# 2130-2700) and the green/grey scene that followed was the same loop writing past
# text RAM. FUN_0001b9aa is the same class (dbf d1, five callers at 0x1B95C-0x1B992)
# but its caller is not a decompiled function, so the port never reaches it.
_dbfc = [0, 0, 0]
_blocks = re.split(r'(?=/\* ---- 0x)', src)
for _bi, _blk in enumerate(_blocks):
    if _blk.startswith('/* ---- 0x04612 '):
        _blk, k = re.subn(r'\bFUN_00004612\(undefined4 p_A0,undefined4 p_A1,char \*p_A2\)\s*\n\{',
                          'FUN_00004612(uint p_D0,undefined4 p_A0,undefined4 p_A1,char *p_A2)\n{\n'
                          '  short _dbf = (short)p_D0;', _blk)
        _dbfc[0] += k
        _blk, k = re.subn(r'\} while \(extraout_D0w != 0\);', '} while (_dbf-- != 0);', _blk)
        _dbfc[1] += k
        _blocks[_bi] = _blk
    elif _blk.startswith('/* ---- 0x04540 '):
        _blk, k1 = re.subn(r'\bFUN_00004612\((p_A0,s_TAKYOUR[^;]*?0x4103c5)\)', r'FUN_00004612(0x16u,\1)', _blk)
        _blk, k2 = re.subn(r'\bFUN_00004612\((p_A0_00,p_A1,p_A2)\)', r'FUN_00004612(2u,\1)', _blk)
        _dbfc[2] += k1 + k2
        _blocks[_bi] = _blk
src = ''.join(_blocks)
_pass_report('FUN_00004612 takes its dbf count D0', _dbfc[0], 1)
_pass_report('FUN_00004612 loops on the count', _dbfc[1], 1)
_pass_report('FUN_00004612 call sites pass the 68K constant', _dbfc[2], 2)
# ---- FUN_00004540: A0/A1 THREAD THROUGH THE ROW LOOP ----------------------------------
# prog68k 0x45B8-0x460C: A1 is left by the header call (0x4790 + 23 = "1ST2ND3RD4TH5TH..")
# and each row's `bsr 0x4612` (d0=2) reads three more bytes from it; A0 is `lea
# 0xFFFC00,a0` (0x45CE) once before the loop and each row consumes one 8-byte entry
# (`addq.l #4,a0`, four `move.b (a0)+`). Ghidra's p_A1 / p_A0_01 are uninitialised
# locals, so rd64's rank rows typed from a stale pointer and the digit writer read a
# score from whatever A0 held.
_hs = [0, 0, 0]
_blocks = re.split(r'(?=/\* ---- 0x)', src)
for _bi, _blk in enumerate(_blocks):
    if not _blk.startswith('/* ---- 0x04540 '):
        continue
    _blk, k = re.subn(r'(p_D0 = FUN_00004612\(0x16u,p_A0,(s_TAKYOUR\S+?00004767) \+ 0x29,0x4103c5\);\n)',
                      r'\1  p_A1 = (undefined4)(\2 + 0x29 + 0x17);\n  p_A0_01 = (undefined4 *)&DAT_fffffc00;\n', _blk)
    _hs[0] += k
    _blk, k = re.subn(r'(FUN_00004612\(2u,p_A0_00,p_A1,p_A2\);\n)', r'\1    p_A1 = p_A1 + 3;\n', _blk)
    _hs[1] += k
    _blk, k = re.subn(r'(\} while \(sVar2 != -1\);\n    sVar1 = sVar1 \+ -1;\n    p_A2 = p_A2 \+ 0xfa;)',
                      r'} while (sVar2 != -1);\n    p_A0_01 = p_A0_01 + 8;\n    sVar1 = sVar1 + -1;\n    p_A2 = p_A2 + 0xfa;', _blk)
    _hs[2] += k
    _blocks[_bi] = _blk
src = ''.join(_blocks)
_pass_report('FUN_00004540: A1/A0 seeded after the header', _hs[0], 1)
_pass_report('FUN_00004540: A1 advances three per row', _hs[1], 1)
_pass_report('FUN_00004540: A0 advances one entry per row', _hs[2], 1)
# ... and A2 is advanced BY THE CALLEES: 0x4612 writes count+1 bytes at stride 2 (+6
# for the rank), 0x4624 eight digits (+16); the caller's own `addq.w #1/#7,a2`,
# `adda.w #4,a2` and `lea 250(a2),a3` are relative to the advanced register. Ghidra
# wrote every later offset from the row's START: digits at +1 (68K +7), the round at
# +8 (68K +30 = 0x1E), the names from +0xD (68K +0x23), the next row at +0xFA (68K
# +0x100). rd66: "1ST" lost its S and T under the digit writer, every row sat three
# columns left of the arcade's.
_hs2 = [0]
_blocks = re.split(r'(?=/\* ---- 0x)', src)
for _bi, _blk in enumerate(_blocks):
    if not _blk.startswith('/* ---- 0x04540 '):
        continue
    for _old, _new in ((r'FUN_00004624\(p_A0_01,p_A1_00,p_A2 \+ 1\)', 'FUN_00004624(p_A0_01,p_A1_00,p_A2 + 7)'),
                       (r'\*\(byte \*\)\(p_A2 \+ 8\) = ', '*(byte *)(p_A2 + 0x1e) = '),
                       (r'iVar4 = p_A2 \+ 0xd;', 'iVar4 = p_A2 + 0x23;'),
                       (r'p_A2 = p_A2 \+ 0xfa;', 'p_A2 = p_A2 + 0x100;')):
        _blk, k = re.subn(_old, _new, _blk)
        _hs2[0] += k
    _blocks[_bi] = _blk
src = ''.join(_blocks)
_pass_report('FUN_00004540: A2 offsets measured from the advanced register', _hs2[0], 4)
# The row's NAME loop (0x45F4-0x4606): `adda.w #4,a2; moveq #2,d0; { addq.w #1,a2;
# move.b (a0)+,d1; add.b #-0x60,d1; move.b d1,(a2)+; bsr 0x4664; dbf d0 }` -- three
# characters from the entry's name at +5, one per two frames. Ghidra's loop counts on
# `extraout_D0`, which the port fills from the wait's packed return (an uninitialised
# A1 = 0), so it ran once: rd68 printed "H", "U", "S" where the arcade prints HKR UCH
# SAT. And p_A0_01 is a 32-bit INTEGER in Ghidra's text, so `+ 2` was two bytes, not
# one entry (the round byte of row 2 read entry 1's 'K' & 0xF = 0xB).
_hs5 = [0]
_blocks = re.split(r'(?=/\* ---- 0x)', src)
for _bi, _blk in enumerate(_blocks):
    if not _blk.startswith('/* ---- 0x04540 '):
        continue
    _blk, k = re.subn(
        r'    uVar8 = FUN_00004664\(iVar4 \+ 5,p_A1_01\);\n'
        r'    iVar4 = p_A2 \+ 0x23;\n'
        r'    do \{\n.*?\n    \} while \(sVar2 != -1\);\n',
        '    FUN_00004664(0,0);\n'
        '    {\n'
        '      const char *_np = (const char *)(iVar4 + 5);\n'
        '      char *_tp = (char *)(p_A2 + 0x24);\n'
        '      short _k;\n'
        '      for (_k = 0; _k < 3; _k++) { *_tp = (char)(_np[_k] + -0x60); _tp += 2; FUN_00004664(0,0); }\n'
        '    }\n', _blk, count=1, flags=re.S)
    _hs5[0] += k
    _blocks[_bi] = _blk
src = ''.join(_blocks)
_pass_report('FUN_00004540: the name loop runs three characters', _hs5[0], 1)

# ---- FUN_00004624: THE SCORE DIGITS, HAND-DERIVED FROM 0x4624-0x4662 ------------------
# `moveq #7,d2; move.l (a0),d0; { rol.l #4,d0; d1 = d0 & 15; bne digit; blank: addq.l
# #1,a2; move.b #0,(a2)+; bsr 0x4664; dbf d2 } ... digit: add.b #-0x30,d1; addq.l #1,a2;
# move.b d1,(a2)+; bsr 0x4664; dbf d2,next-digit (rol again); all-zero: move.b #-0x30,d1;
# subq.l #1,a2; move.b d1,(a2)+; bsr 0x4664`. Eight nibbles from the top, leading zeros
# blank, every nibble after the first non-zero a digit, an all-zero score prints one '0'
# in the last cell. D0 is PRESERVED across the wait (0x4672 movem) and re-rotated each
# pass; Ghidra re-read it as `extraout_D0`, which the port fills with the wait's packed
# return, so every score printed blank (rd68). A0 is left unchanged (the caller reads
# the round byte at A0+4 from the returned A0).
# ---- FUN_00004178: THE HIGH-SCORE TABLE'S VALIDATE / RESTORE / INSTALL, HAND-DERIVED --
# prog68k 0x4178-0x4212: 0xFFFC00 holds the table (7 x 8 bytes) + its CRC word at +0x38
# (FUN_000041b2: poly 0x8810 over 0x1C words); 0xFFFC3A holds a backup of the same 58
# bytes. 0x41CC leaves CARRY SET when a copy is valid (CRC matches and is non-zero, or
# CRC zero with a non-zero table). Primary valid -> copy it over the backup; else backup
# valid -> copy it over the primary; else ROM 0x4732 (14 longs) -> primary, CRC stored
# at +0x38, then primary -> backup. Finally 0xFFF010 = the top score. Ghidra's version
# tests an uninitialised `in_CF`, copies 32-bit longs from 0x4732 (2 mod 4) and into
# 0xFFFC3A (2 mod 4) -- SH-2 address errors -- and stores the CRC through a stale A0.
# Measured rd64: the port's table sat two bytes shifted from the ROM's (f451).
# FUN_00004196 (0x4196-0x41A8) is the same 58-byte copy A6 -> A5 on its own, called
# with A5 = 0xFFFC3A from the name-entry path (0x4254/0x4276): the destination is
# 2 mod 4, so its 32-bit stores are SH-2 address errors too.
# FUN_00001306 (0x1306-0x1336): the player-record clear called from main_entry
# (0x5D2).  The 68K reads FFE018 (lives) and FFE098 into D2/D3 before the 64-long
# wipe and writes them back after it (0x1310/0x1314, 0x131E/0x1322).  Ghidra does
# not see the pointer loop aliasing the two globals, so it folds "store X = load X"
# away and the port wiped the lives byte that the attract-step table had just set
# (0x1EDA): FFE018 stayed 0 for the whole demo, the dispatcher's D5 button word
# read 0, and the hero punched where the arcade kicks.  Measured 2026-10-04 (rd98:
# port FFE018..1F = 00 00 00 00 f0 28 02 06 vs arcade 01 00 00 01 f0 28 00 06).
# ---- WORD LINKS USED AS A POINTER BASE INSIDE A DEREFERENCE ----
# `movea.w ($3c,A6),A0 ; move.b #1,($1b,A0)` (0x48BE/0x48C2) is the beast-form flag
# of the player record: Ghidra writes it as
#     *(undefined *)(*(short *)(unaff_A6 + 0x3c) + 0x1b) = 1;
# a 16-bit link (0xE000, sign-extended by the 68000 to 0xFFFFE000) plus an offset,
# dereferenced RAW.  On the SH-2 that address is the on-chip area, so the store
# vanished: FFE01B stayed 0, the dispatcher's D5 button word stayed 0 (`form < 3 ||
# FFE01B`), and the demo hero punched where the arcade kicks (2026-10-04, rd100:
# port FFE018..1F 01 00 00 00 .. vs arcade 01 00 00 01 ..).  The W68 pass covered
# the `(int)W68(*(short *)(A6 + 0x3c))` assignments; this one covers the nested
# shape `*(T *)(*(short *)(BASE) + K)` wherever BASE is balanced, innermost first,
# so `*(byte *)(*(short *)(*(short *)(A6 + 0x78) + 0x78) + 0x23)` maps both hops.
# LD32U/LD32S/ST32/P68 sites stay in the 68K domain and are not touched.
# 8/16-bit only: 32-bit loads through a link (`*(int *)(link + 0x24)`, a script
# pointer) belong to the LD32S/LD32U/ST32 passes, which also map the VALUE's domain.
_TYS = '(undefined|char|byte|short|ushort|undefined2)'
def _word_link_bases(text):
    n = 0
    pat = re.compile(r'\*\(' + _TYS + r' \*\)\(\*\(short \*\)\(')
    while True:
        changed = False
        pos = 0
        out = []
        while True:
            m = pat.search(text, pos)
            if not m:
                out.append(text[pos:]); break
            # balanced BASE after `*(short *)(`
            i = m.end(); depth = 1
            while i < len(text) and depth:
                c = text[i]
                if c == '(': depth += 1
                elif c == ')': depth -= 1
                i += 1
            base = text[m.end():i - 1]
            km = re.match(r' \+ (0x[0-9a-f]+|[0-9]+)\)', text[i:])   # `+ 8)` as well as `+ 0x1b)`: Ghidra writes offsets under 10 in decimal (FUN_0000e474's record-index swap through the +0x78 link, sw292)
            if not km or '(' in base and 'W68(' not in base and '*(short *)(' in base:
                # inner hop not yet mapped (will be on the next sweep) or not `+ K)`
                out.append(text[pos:m.start() + 1]); pos = m.start() + 1; continue
            out.append(text[pos:m.start()])
            out.append('*(%s *)((char *)W68(*(short *)(%s)) + %s)' % (m.group(1), base, km.group(1)))
            pos = i + km.end()
            n += 1; changed = True
        text = ''.join(out)
        if not changed:
            return text, n
src, _wlb = _word_link_bases(src)
_pass_report('word links dereferenced as a pointer base mapped through W68', _wlb, 10)
# FUN_00011386 (0x11386-0x113AE): the partner's x-step walker.  A0 is a ROM table of
# (count, delta) byte pairs (0x10CCE, passed by 0x10814/0x10954 as `lea`): D0 =
# --A6[0x7e]; `move.b (a0)+,d1 ; sub.b d1,d0 ; bcs out ; addq.l #1,a0 ; bra` walks the
# pairs until the count exceeds what is left, then `move.b (a0),d1 ; ext.w` takes the
# delta after that count, negated when A6[0x2e] bit 7, added to A6[0xc].  Ghidra lost
# the post-increment (its loop re-reads `*p_A0` forever, advancing a copy) and the
# callers pass the raw 68K address, which the SH-2 dereferenced as its own 0x10CCE:
# a zero count there never exceeds D0, and the demo hung at tick 0x76 the moment the
# partner (slot 1) first ran this (rd101, 2026-10-04, coroutine stack: FUN_000103e0
# +0x5ac -> FUN_00011386).
# FUN_00003706 (0x3706): `lea (8,a5),a0 ; lea 0xFFF036,a1 ; bra 3778` -- the four
# `abcd -(a1),-(a0)` of 0x3774 add the BCD long at 0xFFF032 (the kill's points, set by
# the spawner: 0x500, 0x100 ...) into the player record's pending bonus at +4..+7.
# Ghidra wrote four separate byte adds through `cRamfffff035..32` -- names cport_prep
# never mapped to work RAM, so they were zero globals -- and `bcdAdjust()` is a stub
# that returns 0 and is discarded. Measured 2026-10-04 (MAME wp census on FFE006: the
# writer is PC 0x3780/0x3782; arcade FFE006 1 -> 2 -> 3 as the trio dies, port 0).
# ---- THE OVERRIDDEN JUMP TABLES: SWITCH ON THE 68K TARGET, READ THE TABLE IN ROM ----
# tools/ghidra/make_jumptables.py gave the decompiler the PC-relative tables (13 sites,
# 2026-10-04); it now writes `switch(psVar13) { case (short *)0xfdd0: ...}` -- a switch
# on the computed 68K target with pointer-typed case labels (not C), and the target
# itself as `psVar = (short *)((int)*psVar + (int)psVar)` with the table word read raw.
# Rewritten: the switch and its cases become integers; the `ptr + *ptr` idiom (a
# PC-relative word table, or a script pointer plus the word before it) reads its word
# through A68PTR (identity for an SH-2 pointer, ROM/WRAM for a 68K address).
# WHAT THE DECOMPILER WRITES FOR AN OVERRIDDEN TABLE: it normalises the switch to the
# register the index was LOADED THROUGH and folds the load away, so `move.b (0x23,a0),d0`
# comes out as `switch(iVar12) { case -0x23: ...}` / `switch(unaff_A6) { case (byte *)
# 0xffffffdd: ...}` -- case value = state byte - 0x23 (checked on all eight sites of
# the 2026-10-04 re-export: f90a fd6c 11886 12cba 12f0a 13aa6 15434 1650a; the index
# is +0x23 at every one). Rewritten to switch on `*(byte *)(obj + 0x23) - 0x23` with the
# labels as signed ints; FUN_000143dc's `switch((int)*psVar17 + (int)psVar17)` (the
# target address, integer labels) only needs the table word read through A68PTR.
_jt = [0, 0, 0, 0]
def _jt_switch(m):
    _jt[3] += 1
    var = m.group(1)
    obj = '(char *)(%s)' % var if var == 'unaff_A6' else '(char *)A68PTR(%s)' % var
    return 'switch((int)*(byte *)(%s + 0x23) - 0x23) {   /* the state byte, see cport_prep.py */%s' % (obj, m.group(2))
src = re.sub(r'switch\((unaff_A6|iVar\d+)\) \{((?:\n[^\n]*){0,2}\n\s*case (?:\(byte \*\)0xffffff[0-9a-f]{2}|-0x[0-9a-f]+):)', _jt_switch, src)
def _jt_case(m):
    _jt[0] += 1
    v = int(m.group(1), 16)
    return 'case (int)%su:' % m.group(1) if v >= 0x80000000 else 'case %s:' % m.group(1)
src = re.sub(r'case \((?:short|undefined|byte|char|ushort|undefined2|int) \*\)(0x[0-9a-f]+):', _jt_case, src)
src, k = re.subn(r'\(int\)\*(p[sb]Var\d+) \+ \(int\)\1\b', r'(int)*(short *)A68PTR(\1) + (int)\1', src); _jt[1] += k
src, k = re.subn(r'\b(pbVar\d+) \+ \*\(short \*\)\1\b', r'\1 + *(short *)A68PTR(\1)', src); _jt[1] += k
src, k = re.subn(r'\(int\)\*\(short \*\)\((?P<x>[^;()]+(?:\([^()]*\)[^;()]*)*)\) \+ (?P=x)\b', r'(int)*(short *)A68PTR(\g<x>) + \g<x>', src); _jt[2] += k
_pass_report('overridden jump tables: switches on the state byte', _jt[3], 8)
_pass_report('overridden jump tables: pointer case labels made integers', _jt[0], 20)
_pass_report('overridden jump tables: ptr + *ptr reads through A68PTR', _jt[1], 2)
_pass_report('overridden jump tables: X + *(short *)(X) reads through A68PTR', _jt[2], 4)
# ---- OVERRIDDEN SWITCHES: A STATE THAT SHARES ANOTHER STATE'S TARGET NEEDS ITS OWN LABEL ----
# The 68K state tables map several states to one block (0x12FF2: states 3 AND 4 -> 0x12E12;
# 0x16938: 0, 3, 4 -> 0x16880). The decompiler's switch over an overridden table carries ONE
# case label per target it chose to name, so the second state falls out of the switch into
# the code after it. Measured 2026-10-23 on the attract's level-2 demo (kind 7's head,
# FUN_00012cba, tick 0x1A3 on): in state 4 the arcade's head falls 0x10BE -> 0x1143 and
# settles, the port's rose to 0x1092 and held -- state 4's block (= state 3's) never ran.
# This pass decodes each registered table from prog68k.bin (entry-relative words, the
# `adda.w d0,a0 ; adda.w (a0),a0` form) and, for every state without a label whose target
# some labelled state shares, adds `case <state>:` beside that label. A target nobody
# labels is the fall-through block and is left alone.
_sw_tables = {'FUN_0000f90a': 0x10104, 'FUN_0000fd6c': 0x100f6, 'FUN_00011886': 0x11e34,
              'FUN_00012cba': 0x12ff2, 'FUN_00012f0a': 0x12ffc, 'FUN_00015434': 0x15e60,
              'FUN_0001650a': 0x16938}
_sw_bases = sorted([0x346a, 0x88ec, 0xd0ec, 0xd5a2, 0x100f6, 0x10104, 0x11e34, 0x128da, 0x12ff2,
                    0x12ffc, 0x13926, 0x141b8, 0x148a2, 0x15e60, 0x16938])   # tools/ghidra/f1_tables.py BASES
_sw_rom = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'roms', 'altbeast', 'prog68k.bin')
_sw = [0, 0]
def _sw_labels(txt):
    import struct
    if not os.path.exists(_sw_rom):
        return txt
    rom = open(_sw_rom, 'rb').read()
    heads = [(m.start(), m.group(2), int(m.group(1), 16), int(m.group(3)))
             for m in re.finditer(r'/\* ---- 0x([0-9A-F]+)  (\w+)  \((\d+) B\)', txt)]
    out = txt
    for m in list(re.finditer(r'switch\(\(int\)\*\(byte \*\)\([^\n]*\+ 0x23\) - 0x23\) \{', txt))[::-1]:
        fn = None
        for pos, name, addr, size in heads:
            if pos <= m.start(): fn = (name, addr, size)
        if fn is None or fn[0] not in _sw_tables:
            continue
        base = _sw_tables[fn[0]]
        nxt = [b for b in _sw_bases if b > base]
        n = (nxt[0] - base) // 2 if nxt and nxt[0] - base <= 32 else 8
        ents = {}
        for i in range(n):
            e = base + i * 2
            tgt = e + struct.unpack('>h', rom[e:e + 2])[0]
            if not (fn[1] - 0x40 <= tgt < fn[1] + max(fn[2], 0x800) + 0x200):
                break
            ents[i] = tgt
        end = txt.find('\n/* ---- 0x', m.start())
        body = txt[m.start():end if end > 0 else len(txt)]
        labels = {}
        for lm in re.finditer(r'(case \(int\)0x(ffffff[0-9a-f]{2})u:|case (-0x[0-9a-f]+):)', body):
            v = int(lm.group(2), 16) - 0x100000000 if lm.group(2) else int(lm.group(3), 16)
            labels.setdefault(v + 0x23, lm)
        adds = []
        for st, tgt in ents.items():
            if st in labels:
                continue
            twins = [s2 for s2, t2 in ents.items() if s2 != st and t2 == tgt and s2 in labels]
            if twins:
                adds.append((st, labels[twins[0]]))
        for st, lm in sorted(adds, key=lambda x: -x[1].start()):
            at = m.start() + lm.start()
            lab = 'case (int)0x%08xu:   /* state %d shares state %s\'s block (table 0x%05X): cport_prep.py */\n%s' % (
                (st - 0x23) & 0xffffffff, st, [s2 for s2, t2 in ents.items() if t2 == ents[st] and s2 in labels][0], base,
                ' ' * (len(lm.group(0)) - len(lm.group(0).lstrip())))
            out = out[:at] + lab + out[at:]
            _sw[0] += 1
        if adds: _sw[1] += 1
    return out
src = _sw_labels(src)
_pass_report('overridden switches: shared-target states labelled', _sw[0], 1)
# ---- ROM TABLE LITERALS ASSIGNED TO A LOCAL THAT IS THEN DEREFERENCED ----
# `lea table(pc),a0` followed by `(a0)+` reads: Ghidra writes `psVar = (short *)0xeb98;`
# and dereferences the 68K ROM address raw -- on the SH-2 that is the boot ROM / open
# bus.  Measured 2026-10-04 (rd102 census): 0xEB98 (FUN_0000e474), 0x141B2
# (FUN_00014152), 0x1515A (FUN_00014bb8), 0x15E6C/0x15E76 (FUN_00015b1e).  Mapped
# through P68 at the assignment.  EXCLUDED: 0x10212 (FUN_0000f90a stores it into the
# object's +0x24 script pointer -- a 68K VALUE, never dereferenced here) and the boot
# card's 0x1986/0xFE0020 pair in FUN_0000040e (the boot path is its own card).
_rl = [0]
def _rom_local_literal(m):
    v = int(m.group(3), 16)
    if v in (0x10212, 0x1986) or v >= 0x40000:
        return m.group(0)
    _rl[0] += 1
    return '%s = (%s *)P68(%s);   /* a 68K ROM table: cport_prep.py */' % (m.group(1), m.group(2), m.group(3))
# 8/16-bit pointees only: the `(undefined4 *)0x...` templates (0xF56A, 0x1233A, 0x141C2 are
# 2 mod 4) belong to the spawn-installer passes, which read them with LD32U.
src = re.sub(r'\b(\w+) = \((byte|char|short|ushort|undefined|undefined2) \*\)(0x[0-9a-f]{3,5});', _rom_local_literal, src)
_pass_report('ROM table literals assigned to a dereferenced local mapped through P68', _rl[0], 5)
# ---- 0xFFF142/0xFFF143: THE ROUND BYTE AND ITS COUNTER, STORED AT THE 68K's WIDTH ----
# CPORT_CLAMP narrows `_DAT_fffff142` to a byte (the program reads it with `move.b`,
# `cmpi.b` at 30+ sites), but Ghidra wrote the program's BYTE stores to the pair as
# word compositions, and a word composition assigned to a byte keeps its LOW half:
#   0x0670 `move.b tbl(d0),0xFFF142`  -> `_DAT_fffff142 = CONCAT11(tbl, DAT_fffff143)`
#                                        clamped: FFF142 = FFF143 (the second demo's
#                                        round read 0 where the arcade's reads 1, rd67)
#   0x0A2A `addq.b #1,0xFFF143`       -> `_DAT_fffff142 = CONCAT11(FFF142, FFF143 + 1)`
#                                        clamped: FFF142 = FFF143 + 1
#   0x0BCE `clr.b 0xFFF143`           -> `_DAT_fffff142 = (ushort)FFF142 << 8`
#                                        clamped: FFF142 = 0
#   0x05DE `clr.w 0xFFF142`           -> `_DAT_fffff142 = 0`: clamped, FFF143 survives.
# Each form is rewritten to the byte store the instruction performs.
_rb = [0, 0, 0, 0]
src, _rb[0] = re.subn(r"_DAT_fffff142 = CONCAT11\(DAT_fffff142,DAT_fffff143 \+ '\\x01'\);",
                      "DAT_fffff143 = DAT_fffff143 + 1;", src)
src, _rb[1] = re.subn(r'_DAT_fffff142 = \(ushort\)DAT_fffff142 << 8;', 'DAT_fffff143 = 0;', src)
src, _rb[2] = re.subn(r'_DAT_fffff142 =\s*CONCAT11\((.*?),\s*DAT_fffff143\);',
                      lambda m: 'DAT_fffff142 = (byte)(%s);' % m.group(1), src, flags=re.S)
src, _rb[3] = re.subn(r'_DAT_fffff142 = 0;', 'DAT_fffff142 = 0; DAT_fffff143 = 0;', src)
_pass_report('0xFFF143 += 1 (addq.b)', _rb[0], 2)
_pass_report('0xFFF143 = 0 (clr.b)', _rb[1], 2)
_pass_report('0xFFF142 = table byte (move.b)', _rb[2], 2)
_pass_report('0xFFF142/143 cleared (clr.w)', _rb[3], 1)
# ... and the two READ forms the clamp breaks: 0x08C4 `move.b 0xFFF142,d0` as the
# music-table index came out `_DAT_fffff142 >> 8` (clamped: always 0 -- every round
# would play round 1's music), 0x074C `tst.w 0xFFF142` as `_DAT_fffff142 == 0`
# (clamped: FFF143 ignored).
_rb2 = [0, 0]
src, _rb2[0] = re.subn(r'\(short\)\(_DAT_fffff142 >> 8\)', '(short)DAT_fffff142', src)
src, _rb2[1] = re.subn(r'\(_DAT_fffff142 == 0\)', '((DAT_fffff142 | DAT_fffff143) == 0)', src)
_pass_report('0xFFF142 read as the music index (move.b)', _rb2[0], 4)
_pass_report('0xFFF142 word test (tst.w)', _rb2[1], 2)
# ---- FUN_0000a3b0: THE HERO'S ATTACK LOOKUP RETURNS A0 WITH CARRY ----------------------
# prog68k 0xA3B0-0xA414: from D6 (buttons, bits 1-2) and D7 (bit 4) it leaves the state
# handler in A0 and carry SET (0xA140 / 0xA416 / 0xA4DC / 0xA664), or carry CLEAR for
# "no attack" (0xA410). The five callers (0xA1D8 and its siblings) do `bsr; bcc skip;
# jmp (a0)`. Ghidra returned a D0 fragment and rendered the carry as a stale `bVar5`,
# so the port never took the jump: the hero's partner object (slot 1, handler 0xA286,
# installed by 0xA1E0 `lea 0x80(fp),a5`) never appeared and the second demo's hero
# stood at x 0x120F while the arcade's walked to 0x12D0 (rd69 tick 0xA0). The handler
# also reads D5 (0xA14E `btst #1,d5`: kick vs punch); measured rd96 tick 0x34 the port
# took state 5 (0xA140, frame 0xA9F2) where the arcade took state 6 (0xA198, frame
# 0xAA4C) because the register-file D5 was 0 -- the site now publishes p_D5 too.
_a3 = [0, 0]
_blocks = re.split(r'(?=/\* ---- 0x)', src)
for _bi, _blk in enumerate(_blocks):
    if _blk.startswith('/* ---- 0x0A3B0 '):
        pass   # the body is sh_src/portfull/hand/FUN_0000a3b0.c
    else:
        _blk, k = re.subn(
            r'if \(\(!bVar5\) && \(FUN_0000a3b0\(p_D0,p_D6,(p_D7(?:_00)?)\), bVar5\)\) \{\n((?:[^\n]*WARNING[^\n]*\n)*)(\s*)\(\*UNRECOVERED_JUMPTABLE(?:_\d+)?\)\(\);',
            lambda m: ('if ((!bVar5) && (_a3b0_h = FUN_0000a3b0(p_D0,p_D6,%s)) != 0) {\n%s%sport_set_reg(5, (uint)p_D5); port_set_reg(6, (uint)p_D6); port_set_reg(7, (uint)%s); port_call68_at(9, _a3b0_h);'
                       % (m.group(1), m.group(2), m.group(3), m.group(1))), _blk)
        _a3[1] += k
        _blocks[_bi] = _blk
src = ''.join(_blocks)
if _a3[1]:
    src = 'static uint _a3b0_h;   /* FUN_0000a3b0: the handler A0 carried to the jump */\n' + src
_pass_report('FUN_0000a3b0 callers jump through the returned handler', _a3[1], 5)

# ---- THE OTHER SIX FORMS' ATTACK LOOKUPS: THE SAME IDIOM AS FUN_0000a3b0 --------------------
# 0x5062, 0x7F58, 0x8D66, 0xBBC6, 0xC9B4 (four targets each) and 0xAF4A (two) return the
# transition handler in A0 with carry set, and every caller does `bsr ; bcc skip ; jmp (a0)`.
# Ghidra rendered the carry as a stale bVarN and the jump as a call through an uninitialised
# `code *`, in two shapes: `if ((!bVarN) && (X = FUN(args), bVarN)) { (*UNRECOVERED..)(); }`
# and `X = FUN(args); if (bVarN) { (*UNRECOVERED..)(); }` (0xAF4A also as `FUN(args); if
# (!bVarN) { return; } (*UNRECOVERED..)();`). Measured 2026-10-04, demo A tick 0xED: the
# arcade's hero (form 2, state 6) takes 0x8D66's 0x8DCC on the button edge -- sound 0xB6,
# the trail -- and the port kept walking. The deciders are hand files returning the 68K
# address (hand/FUN_<addr>.c), the targets hand functions (hand/AT_FUN_<addr>.c).
_DECS = r'FUN_0000(?:5062|7f58|8d66|bbc6|c9b4|af4a)'
_dc = [0, 0, 0]
def _dec_regs(args):
    _a = [x.strip() for x in args.split(',')]
    return 'port_dec_jump(_dec_h, (uint)p_D5, (uint)%s, (uint)%s); ' % (_a[1], _a[2])
def _dec_s1(m):
    _dc[0] += 1
    _x = ('%s = ' % m.group(2)) if m.group(2) else ''
    _y = ('%s = ' % m.group(7)) if m.group(7) else ''
    return ('if ((!%s) && ((%s(_dec_h = %s(%s))), _dec_h != 0)) {\n%s%s%s%s'
            % (m.group(1), _x, m.group(3), m.group(4), m.group(5), m.group(6), _y, _dec_regs(m.group(4))))
def _dec_s2(m):
    _dc[1] += 1
    _x = ('%s = ' % m.group(1)) if m.group(1) else ''
    _y = ('%s = ' % m.group(8)) if m.group(8) else ''
    return ('%s(_dec_h = %s(%s));\n%sif (_dec_h != 0) {\n%s%s%s%s'
            % (_x, m.group(2), m.group(3), m.group(4), m.group(6), m.group(7), _y, _dec_regs(m.group(3))))
def _dec_s3(m):
    _dc[2] += 1
    return ('_dec_h = FUN_0000af4a(%s);\n%sif (_dec_h == 0) {\n%s%s}\n%s%s%s'
            % (m.group(1), m.group(2), m.group(4), m.group(2), m.group(5), m.group(6), _dec_regs(m.group(1))))
_blocks = re.split(r'(?=/\* ---- 0x)', src)
for _bi, _blk in enumerate(_blocks):
    if re.match(r'/\* ---- 0x0(?:5062|7F58|8D66|BBC6|C9B4|AF4A) ', _blk):
        continue   # the deciders themselves are hand files
    _blk = re.sub(r'if \(\(!(bVar\d+)\) && \((?:(\w+) = )?(' + _DECS + r')\(([^)]*)\), \1\)\) \{\n((?:[^\n]*WARNING[^\n]*\n)*)(\s*)(?:(\w+) = )?\(\*UNRECOVERED_JUMPTABLE(?:_\d+)?\)\(\);', _dec_s1, _blk)
    _blk = re.sub(r'(?:(\w+) = )?(' + _DECS + r')\(([^)]*)\);\n(\s*)if \((bVar\d+)\) \{\n((?:[^\n]*WARNING[^\n]*\n)*)(\s*)(?:(\w+) = )?\(\*UNRECOVERED_JUMPTABLE(?:_\d+)?\)\(\);', _dec_s2, _blk)
    _blk = re.sub(r'FUN_0000af4a\(([^)]*)\);\n(\s*)if \(!(bVar\d+)\) \{\n((?:[^\n]*\n){1,6}?)\2\}\n((?:[^\n]*WARNING[^\n]*\n)*)(\s*)\(\*UNRECOVERED_JUMPTABLE(?:_\d+)?\)\(\);', _dec_s3, _blk)
    _blocks[_bi] = _blk
src = ''.join(_blocks)
if sum(_dc):
    src = 'static uint _dec_h;   /* the forms\' attack lookups: the handler A0 carried to the jump */\n' + src
_pass_report('attack lookup callers (structured) jump through the returned handler', _dc[0], 230)
_pass_report('attack lookup callers (plain) jump through the returned handler', _dc[1], 10)
_pass_report('FUN_0000af4a plain callers jump through the returned handler', _dc[2], 1)
# ---- FUN_00017188 / FUN_000171fa: THE COLOUR CYCLE, HAND-DERIVED --------------------
# prog68k 0x17188-0x171F8: every 8 ticks (callers test 0xFFF02A & 7) step each word of
# the live palette table 0xFFE400 one unit toward the target at 0xFFE600, one nibble
# at a time; 0x171FA (d2 mask, d3 half-step bit, d4 step, d6 target, d7 current)
# modifies D7 IN PLACE and sets 0xFFE800 when it moved. Ghidra dropped the D7 result
# (its FUN_000171fa only sets the flag and returns A1) and wrote the target pointer
# as `extraout_A0` -- the port's register-file A0, whatever the last thunk left --
# so every eighth tick the loop read its target through a stale pointer. A0 after
# the spawn walk points into the enemy slots. Measured rd71: slot 48's byte 0x22
# became 0xFF one tick after the zombie was installed (the arcade never writes it
# after the install), the zombie rose past y = 0 and was culled.
# ---- FUN_0001a6ba: THE COLOUR-CYCLE TABLE'S INITIALISER READS ITS ROM TABLE UNALIGNED --
# prog68k 0x1A6CA-0x1A6F0: three 6-byte entries at 0x1A6FC (word kind, long script
# pointer); `movea.l (a0)+,a1` reads the pointer at 0x1A6FE -- 2 mod 4 -- and
# `move.w 2(a1),d0` reads the script's timer. Ghidra's `iVar1 = *piVar5` is a 32-bit
# load the SH-2 cannot do at that address (measured rd82: the record held 0x00130001,
# the long at 0x1A6FC, where the arcade holds 0x0001A70E), and `*(iVar1 + 2)` then
# dereferenced a 68K ROM address as a host pointer. LD32S for the load, P68 for the
# script. Until this the colour cycle ran on a garbage table every frame.
_cci = [0, 0]
_blocks = re.split(r'(?=/\* ---- 0x)', src)
for _bi, _blk in enumerate(_blocks):
    if not _blk.startswith('/* ---- 0x1A6BA '):
        continue
    _blk, k = re.subn(r'iVar1 = \*piVar5;', 'iVar1 = LD32S(piVar5);', _blk); _cci[0] += k
    _blk, k = re.subn(r'pbVar6\[1\] = \(byte\)\*\(undefined2 \*\)\(iVar1 \+ 2\);', 'pbVar6[1] = (byte)*(undefined2 *)P68(iVar1 + 2);', _blk); _cci[1] += k
    _blocks[_bi] = _blk
src = ''.join(_blocks)
_pass_report('FUN_0001a6ba: the script pointer loaded unaligned', _cci[0], 1)
_pass_report('FUN_0001a6ba: the script timer read through P68', _cci[1], 1)
# ---- FUN_00016b6a: THE DEMO DIRECTOR'S INIT OWNS A6 = 0xFFD400 FOR ITS WHOLE BODY ----
# prog68k 0x16B6A `lea 0xffffd400,%fp` and the body falls through into the director's
# per-frame handler (0x16BF0-0x16D3C: restore_position, integrate_position,
# FUN_0000669c, hitbox_a/b_expand, animation_frame_fetch, walk_player_slots -- all on
# fp). Ghidra merged that tail into FUN_00016b6a and wrote the director's own cells as
# absolute `_DAT_ffffd4xx`, so the CPORT_A6 restore covered only the first call
# (request_palette_update) and every later callee read the register-file A6 -- the
# last object the dispatcher ran, slot 48, the first zombie. Measured rd94/95
# (transition ring, caller return address main_entry+0x6c8): one write at demo-B
# start between the waits at 0x928 and 0x94C; the zombie's +0x22 went 0xFF (the anim
# stepper's countdown), its y ran negative (integrate) and +0x30.. filled from its own
# anim list (FUN_0000669c), then it was culled and the demo diverged. A6 is set for
# the whole function.
_db = [0]
_blocks = re.split(r'(?=/\* ---- 0x)', src)
for _bi, _blk in enumerate(_blocks):
    if not _blk.startswith('/* ---- 0x16B6A '):
        continue
    _blk, k = re.subn(r'(void FUN_00016b6a\(undefined4 p_D3,undefined4 p_D4,undefined4 p_D6,undefined4 p_D7\)\s*\n\{\n)',
                      r'\1  _r_unaff_A6 = (byte *)W68PTR(0xFFD400u);   /* 0x16B6A lea 0xffffd400,%fp: cport_prep.py */\n', _blk, count=1)
    _db[0] += k
    _blocks[_bi] = _blk
src = ''.join(_blocks)
_pass_report('FUN_00016b6a: A6 = 0xFFD400 for the whole body', _db[0], 1)
# ---- GENERIC: A CALLER WHOSE FIRST INSTRUCTION LOADS A6 OWNS IT FOR ITS WHOLE BODY ----
# docs/audit/a6_callsites.json records each dropped `lea X,%fp`; when the load sits at
# the function's own entry, every callee in the body sees that A6 on the 68K, not
# only the one call the per-site restore covers. Same measurement as above.
_dba = [0]
try:
    _a6rows = json.load(open(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                          'docs', 'audit', 'a6_callsites.json')))
except Exception:
    _a6rows = []
_entry_a6 = {}
_blocks = re.split(r'(?=/\* ---- 0x)', src)
_hdr = {}
for _bi, _blk in enumerate(_blocks):
    _m = re.match(r'/\* ---- 0x([0-9A-F]+)\s+(\S+)', _blk)
    if _m:
        _hdr[_m.group(2)] = (int(_m.group(1), 16), _bi)
for _r in _a6rows:
    _fn = _r.get('caller'); _at = _r.get('at'); _a6 = _r.get('a6')
    if _fn in _hdr and _at == _hdr[_fn][0] and _a6 and 0xFFC000 <= _a6 <= 0xFFFFFF:
        _entry_a6[_fn] = _a6
for _fn, _a6 in sorted(_entry_a6.items()):
    _bi = _hdr[_fn][1]; _blk = _blocks[_bi]
    if '_r_unaff_A6 = (byte *)W68PTR(' in _blk.split('\n{\n', 1)[-1][:120]:
        continue                                  # already set by a specific pass above
    _blk, k = re.subn(r'(\n(?:[a-z]\w*\s+\*?)+%s\([^)]*\)\s*\n\{\n)' % re.escape(_fn),
                      lambda m: m.group(1) + '  _r_unaff_A6 = (byte *)W68PTR(0x%06Xu);   /* entry lea into A6: a6_callsites.json */\n' % _a6,
                      _blk, count=1)
    _dba[0] += k
    _blocks[_bi] = _blk
src = ''.join(_blocks)
sys.stderr.write('cport_prep: entry-A6 callers: %s\n' % ', '.join('%s=0x%X' % kv for kv in sorted(_entry_a6.items())))
_pass_report('callers whose entry loads A6 get it for the whole body', _dba[0], 0)
# ---- RAW 68K WORK-RAM ADDRESSES CAST TO POINTERS ------------------------------------
# `psVar4 = (short *)0xffffde80;` (FUN_0000dbb8, the zombie's parts spawn scanning
# 0xFFDE80 downward), `(char *)0xfffffbfc` (FUN_00004330), `(undefined4 *)0xfffffc38`
# (FUN_00004238): Ghidra kept the sign-extended 68K address as the pointer value and
# no earlier pass touched a literal in a CAST, so on the SH-2 these read and write
# 0xFFFFxxxx -- the on-chip peripheral space -- and nothing lands in work RAM. Mapped
# through W68PTR like every other work-RAM pointer. The comparison in
# palette_queue_drain (`(int *)0xfffff7ff < _DAT_fffff402`) is left alone here: its
# other side is a stored pointer whose domain is decided elsewhere.
_rawp = [0]
def _rawptr(txt):
    def _fn(m):
        _rawp[0] += 1
        return '(%s *)W68PTR(0xFF%su)' % (m.group(1), m.group(2).upper())
    # ONLY the three assignment forms measured: the general literal form is the
    # business of the later "68K bound constants moved to the pointer domain" pass.
    return re.sub(r'= \((short|char|undefined4) \*\)0xffff(de80|fbfc|fc38);', lambda m: '= ' + _fn(m) + ';', txt)
src = _rawptr(src)
_pass_report('raw 68K work-RAM pointer literals mapped through W68PTR', _rawp[0], 3)
# ---- CPORT_WATCHFN: a measurement hook at the entry of named functions ---------------
# `port_watch_hook(0xADDR)` (port_l2.c, under PORT_FFWATCH) samples watched work-RAM
# cells at every named entry, so a transition can be bracketed between two calls of
# the main loop without a debugger. Off unless the env var names functions.
_wf = [x.strip() for x in os.environ.get('CPORT_WATCHFN', '').split(',') if x.strip()]
_wfn = 0
for _fn in _wf:
    _m = re.search(r'FUN_([0-9a-f]{8})$', _fn)
    if _m:
        _tag = int(_m.group(1), 16)
    else:   # a named function: its 68K address is in the block header
        _h = re.search(r'/\* ---- 0x([0-9A-F]+)\s+%s\s' % re.escape(_fn), src)
        _tag = int(_h.group(1), 16) if _h else 0
    src, k = re.subn(r'(\n(?:[a-z]\w* \*?)+%s\([^)]*\)\s*\n\{\n)' % re.escape(_fn),
                     lambda m: m.group(1) + '  port_watch_hook(0x%xu);\n' % _tag, src, count=1)
    _wfn += k
    if not k:
        sys.stderr.write('cport_prep: CPORT_WATCHFN: no definition matched for %s\n' % _fn)
if _wf:
    src = 'extern void port_watch_hook(unsigned int tag);\n' + src
    _pass_report('CPORT_WATCHFN hooks inserted', _wfn, 1)
src = FUNCDEF.sub(_norm, src)
# a normalised function must not `return;` bare
src = re.sub(r'\breturn\s*;', 'return 0;', src)
# DROP LOCAL DECLARATIONS OF PSEUDO-REGISTERS -- the highest-leverage transform
# available (builder, 2026-09-27). Ghidra declares `short in_D0w;` etc. as a LOCAL
# in the function that reads it, which SHADOWS the global of the same name. A
# differential harness sets the globals; the body then reads uninitialised stack
# instead. Measured: 165 of 193 vectored functions shadow, 24,550 bytes, all behind
# this one change. Removing the declaration makes the name bind to the global in
# pseudoregs.h, which is what a register set by the caller actually is.
#
# A declarator is dropped only when its name is a pseudo-register; a mixed
# declaration keeps its other names. Assignments to the name are left alone: a
# function that writes `extraout_var` is writing that register, and the global is
# the right destination.
_PSEUDO_DECL = re.compile(
    r'^([ \t]+)([A-Za-z_][A-Za-z0-9_]*(?:[ \t]+\*?|[ \t]*\*[ \t]*))'
    r'([^;=\n]*);[ \t]*$', re.M)
_PS_NAME = re.compile(r'^(?:unaff|in|extraout)_[A-Za-z0-9_]+$')
_shadow_dropped = [0, 0]
def _drop_pseudo(m):
    indent, ty, rest = m.group(1), m.group(2), m.group(3)
    parts = [x.strip() for x in rest.split(',')]
    keep = []
    for d in parts:
        nm = d.lstrip('*').strip()
        if _PS_NAME.match(nm):
            _shadow_dropped[0] += 1
        else:
            keep.append(d)
    if len(keep) == len(parts):
        return m.group(0)
    _shadow_dropped[1] += 1
    if not keep:
        return indent + '/* pseudo-register locals dropped: they shadow the globals */'
    return '%s%s%s;' % (indent, ty, ', '.join(keep))
# the type each pseudo-register's GLOBAL has, by the same rule pseudoregs.h uses:
# an A-register is always `byte *` (Ghidra writes `*(byte *)(unaff_A6 + 0x2f)`, i.e.
# BYTE arithmetic), and a D-register takes its width from the name suffix.
_GLOBTY = {}
for _n in pseudo:
    _m = re.match(r'(?:unaff|in|extraout)_([AD])(\d)([bw]?)$', _n)
    # An A-register with NO suffix is an address; with a w/b suffix it is the word
    # or byte HALF of that register, which is a value.
    _GLOBTY[_n] = ('uint' if not _m
                   else 'byte *' if _m.group(1) == 'A' and not _m.group(3)
                   else {'b': 'byte', 'w': 'ushort', '': 'uint'}[_m.group(3)])

# A SHADOWING DECLARATION MUST BE INITIALISED, NOT DELETED.
#
# Deleting it was called the highest-leverage transform available, and it is also
# SILENTLY WRONG wherever Ghidra's local type differs from the global's. Ghidra
# declares the element type PER FUNCTION: FUN_000024ec has `undefined2 *unaff_A6;`, so
# `unaff_A6[6] = 0x14a0` is the WORD at byte offset 12. Bound to the global
# `byte *unaff_A6` the same text means the BYTE at offset 6 and truncates to 0xA0 --
# wrong address AND wrong value, across 10,700 references. gcc reported 78 of them as
# -Woverflow and the address scaling was silent everywhere else.
#
# So per function: remember the type Ghidra gave each pseudo-register, strip every
# such declaration, then declare each REFERENCED register once, initialised from the
# `_r_` global, with Ghidra's type where it gave one and the global's type otherwise.
# The `*` binds to the NAME in Ghidra's output -- `  byte *in_A1;` -- so there is no
# whitespace between the stars and the identifier. Requiring some was why 136
# declarations survived, collided with the injected one, and produced
# "redeclaration with no linkage".
_PS_DECL = re.compile(
    r'^[ \t]+(?:const\s+)?([A-Za-z_]\w*)\s*((?:\*\s*)*)'
    r'((?:unaff|in|extraout)_[A-Za-z0-9_]+)\s*;[ \t]*\r?\n', re.M)
# `return in_D0;` is a STATEMENT, not a declaration, and reading `return` as a type
# name produced `return in_D0 = (return)_r_in_D0;`. A type cannot be a C keyword.
_NOTATYPE = {'return', 'goto', 'case', 'break', 'continue', 'else', 'do', 'while',
             'if', 'switch', 'default', 'sizeof'}
_PS_USE = re.compile(r'\b((?:unaff|in|extraout)_[A-Za-z0-9_]+)\b')
_ninit = [0, 0]


def _fix_pseudo(block):
    if '{' not in block:
        return block
    declared = {m.group(3): (m.group(1) + ' ' + m.group(2).replace(' ', '')).strip()
                for m in _PS_DECL.finditer(block) if m.group(1) not in _NOTATYPE}
    block = _PS_DECL.sub(lambda m: m.group(0) if m.group(1) in _NOTATYPE else '', block)
    used = sorted(set(_PS_USE.findall(block)))
    if not used:
        return block
    i = block.index('{')
    out = []
    for n in used:
        ty = declared.get(n) or _GLOBTY.get(n, 'uint')
        sep = '' if ty.endswith('*') else ' '
        out.append('\n  %s%s%s = (%s)_r_%s;' % (ty, sep, n, ty, n))
        _ninit[0 if n in declared else 1] += 1
    return block[:i + 1] + ''.join(out) + block[i + 1:]


src = ''.join(_fix_pseudo(b) for b in re.split(r'(?=/\* ---- 0x)', src))
print('pseudo-registers: %d kept Ghidra\'s type, %d took the global\'s' % tuple(_ninit))

# An A-register global is `byte *`, which is right for the dereferences and
# indexing that dominate. A few sites instead MASK the address
# (`unaff_A2 & 0xfffffff0` -- the alignment round-down the 68K instruction does),
# and C rejects & on a pointer. A general regex for this over-matched badly (132
# errors against the 4 it fixed), so the rule is deliberately literal: cast only
# where a pseudo A-register is the LEFT operand of a mask, optionally after a
# constant displacement.
src = re.sub(r'\(((?:unaff|in|extraout)_A\d)((?:\s*\+\s*0x[0-9a-fA-F]+)?)'
             r'\s*&\s*(0x[0-9a-fA-F]+)\)',
             lambda m: '((uint)%s%s & %s)' % (m.group(1), m.group(2), m.group(3)),
             src)

# Ghidra's sub-field access on a scalar local, `X._2_2_`, means the 2-byte field
# at offset 2. C has no such syntax; rewrite it to the equivalent cast.
# Ghidra sometimes SUBSCRIPTS A FUNCTION NAME -- `FUN_00001848[i]` -- because a
# jump table begins where a function does. In C a function name is not an array,
# so read it through a byte pointer, which is what the 68K is doing anyway.
src = re.sub(r'\b(FUN_[0-9a-f]+)\s*\[', r'((byte *)\1)[', src)

# 68K ABSOLUTE-SHORT ADDRESSING, WHICH GHIDRA RENDERS AS A NEGATIVE CONSTANT.
# `move.b #$FF,($ECC0).w` sign-extends its 16-bit displacement to 0xFFFFECC0 -- work
# RAM 0xFFECC0 -- and Ghidra writes `*(undefined *)(iVar1 + -0x1340)`, because
# 0x10000 - 0x1340 = 0xECC0. Rewritten to the port's own work-RAM block via W68PTR.
# Any intervening casts are allowed: FUN_000137cc has
# `*(undefined4 *)(int)(short)((ushort)unaff_A6[8] * 0x10 + -0x800)`.
_ABSSHORT = re.compile(r'\(\s*(\w+)\s*\*\s*\)\s*(?:\(\s*\w+\s*\)\s*)*'
                       r'\(\s*([^()]*(?:\([^()]*\)[^()]*)*?)\s*\+\s*-0x([0-9a-fA-F]+)\s*\)')
_nabs = [0]


def _absshort(m):
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
    if not (0xFFC000 <= a68 <= 0xFFFFFF):
        return m.group(0)
    _nabs[0] += 1
    return '(%s *)(W68PTR(0x%06Xu) + (%s))' % (ty, a68, expr)


src = _ABSSHORT.sub(_absshort, src)
# A NARROWER DESTINATION TAKES THE HIGH BITS, because the 68K is big-endian.
# `_DAT_fffff138 = _DAT_fffff132` with a LONG at 0xFFF132 is the 68K reading the WORD AT
# THAT ADDRESS -- the HIGH half. C's implicit conversion takes the LOW half, so the
# emitted C is silently wrong. The oracle settles it: the long is 0x102E_C800 and the
# write to 0xFFF138 is 0x102E.
#
# This is the defect behind FUN_000039c8, which Ghidra flags itself ("Globals starting with
# '_' overlap smaller symbols at the same address"). Only 4 sites in the corpus, 2 distinct
# pairs -- narrow enough to rewrite exactly rather than by pattern-and-hope.
_NARROW_ASSIGN = re.compile(r'\b(_?(?:DAT|[a-z]{1,3}Ram)_?[0-9a-f]{4,8}(?:_\d+)?)\s*=\s*'
                            r'(_?(?:DAT|[a-z]{1,3}Ram)_?[0-9a-f]{4,8}(?:_\d+)?)\s*;')
_nnarrow = [0]


def _wid_of(nm):
    m = re.search(r'([0-9a-f]{4,8})(?:_\d+)?$', nm)
    if not m:
        return None
    return DATW.get('%08X' % (0xFFFF0000 | (int(m.group(1), 16) & 0xFFFF)))


def _narrow_assign(m):
    d, sc = m.group(1), m.group(2)
    wd, ws = _wid_of(d), _wid_of(sc)
    if not wd or not ws or wd >= ws:
        return m.group(0)
    _nnarrow[0] += 1
    return '%s = %s >> %d;' % (d, sc, 8 * (ws - wd))


src = _NARROW_ASSIGN.sub(_narrow_assign, src)
print('narrower-destination assignments given the HIGH bits: %d' % _nnarrow[0])

# NOT REWRITTEN, DELIBERATELY: `(short)POINTER` -- the 68K's `move.w a0,d0`, storing the
# LOW HALF OF AN ADDRESS. A68 below is the correct translation for it and the write-side
# partner of W68, but WHICH pointers point into the mirror cannot be told from the text.
# A blanket rewrite substituted 372 sites for the 2 that actually warn and produced 339
# errors -- the same over-reach as the absolute-short pass, where 377 candidates were
# really 143. Two warnings that name their own line are worth more than 370 silent
# rewrites. Apply A68 by hand at a site the oracle has actually exercised.


# A WORK-RAM ADDRESS HELD AS 16 BITS. The 68000 stored only the low half because its
# addressing mode sign-extended it; the port needs the same CELL, not the encoding, so
# it translates at the point of use (Mike, 2026-09-27: the C port is a different
# toolset from a 68K conversion).
# THE LOOKAHEAD IS "NOT AN ADDEND", and it used to recognise only `+ identifier`.
# A SELF-RELATIVE ROM TABLE is written `+ (int)...` and slipped straight through:
#
#     p_A0 = (undefined *)((int)*(short *)(&DAT_0001d33e + idx)
#                        + (int)(&DAT_0001d33e + idx));          /* FUN_0000E052 */
#
# That 16-bit value is a pc-relative OFFSET being added to the address it was read
# from -- the 68000's `lea %pc@(table),%a0 ; adda.w (a0,d0.w),%a0` idiom -- not a
# work-RAM address. W68 turned it into `ROM_BASE + 0x750` and the sum was garbage, so
# FUN_0000E052 walked a table that is not a table: its exit test never fired and it
# ran 172 entries past the end, leaving 0xFFF146 at 0x05A0 where the arcade has
# 0x0040. Measured 2026-10-02; it is also what the older "hangs in 0x0E052" was.
# FULL-PORT.md 41.
_SX16 = re.compile(r'\(int\)\s*(\*\s*\(\s*short\s*\*\s*\)\s*\((?:[^()]|\([^()]*\))*\))'
                   r'(?!\s*\+\s*(?:[A-Za-z_]|\(\s*int\s*\)))')
def _sx16sub(m):
    if 'register0x' in m.group(1):
        return m.group(0)         # a stack slot, not an absolute address
    return '(int)W68(%s)' % m.group(1)


src, _nsx = _SX16.subn(_sx16sub, src)
print('absolute-short rewritten: %d   16-bit work-RAM pointers: %d' % (_nabs[0], _nsx))

# a `(code)` cast (not `(code *)`) is a cast to a function TYPE, which C rejects;
# the value is a byte of code being compared, so read it as one.
src = src.replace('(code)', '(byte)')
# A `code *` local that is SUBSCRIPTED is being read as bytes of code, not
# called. C cannot subscript a pointer-to-function, so retype those locals --
# per function, because the same name is a real call target elsewhere.
def _fix_code_ptr(block):
    for var in set(re.findall(r'\bcode\s*\*\s*(\w+)\s*;', block)):
        if re.search(r'\b%s\s*\[' % re.escape(var), block) and \
           not re.search(r'\(\s*\*\s*%s\s*\)\s*\(' % re.escape(var), block):
            block = re.sub(r'\bcode(\s*\*\s*%s\s*;)' % re.escape(var),
                           r'byte\1', block)
    return block
src = ''.join(_fix_code_ptr(b) for b in re.split(r'(?=/\* ---- 0x)', src))
_W = {'1': 'byte', '2': 'ushort', '4': 'uint', '8': 'unsigned long long'}
def _sub(m):
    var, off, wid = m.group(1), int(m.group(2)), m.group(3)
    return '(*(%s *)((byte *)&%s + %d))' % (_W.get(wid, 'uint'), var, off)
src = re.sub(r'\b([A-Za-z_][A-Za-z0-9_]*)\._(\d+)_(\d+)_', _sub, src)

# ---- A 68K POINTER ROUND-TRIPPED THROUGH 16 BITS -----------------------------
#
# The 68000 held any work-RAM pointer in 16 bits: work RAM was 0xFFxxxx and the
# hardware sign-extended a 16-bit absolute. The decompiled C does the same -- it
# stores a pointer in a `short` and re-expands it with `(T *)(int)x`. On the SH-2
# work RAM sits at _WRAM_BASE (0x0602CC00), so the TRUNCATION DESTROYS the pointer
# and the RE-EXPANSION names the 32X BIOS.
#
# MEASURED 2026-10-02: this is why the rooted main loop hangs. FUN_0000e16a scans
# the object table with a for-loop whose body is EMPTY -- its only content is a
# label -- which terminates only on a negative sentinel, and follows
#
#     psVar10 = (short *)(int)psVar10[0x3c];     a 16-bit work-RAM pointer
#     sVar11  = (short)psVar10;                  an SH-2 pointer truncated
#
# so it walks into the BIOS and never returns: 8000 ares frames without finishing
# one seeded frame (FULL-PORT.md 35).
#
# THREE RULES, each decided from a DECLARED WIDTH rather than from the shape:
#
#   P1  (T *)(int)(short)<pointer>   ->  (T *)(<pointer>)
#       Truncation and re-expansion cancel on the 68K. On the SH-2 the only
#       faithful reading is to do neither.
#   P2  (T *)(int)<16-bit operand>   ->  (T *)W68(<16-bit operand>)
#       w68.h's W68 branches on the SIGN, which is what decides work RAM
#       (0xC880 -> 0xFFC880) from program ROM (0x1844 -> ROM_BASE + 0x1844).
#   P3  <16-bit lvalue> = (short)<pointer>;  ->  = (short)A68OF(<pointer>);
#       The inverse map, so the value STORED is the 68K address P2 will expand.
#
# WHAT IS DELIBERATELY NOT REWRITTEN. An operand whose width is not DECLARED
# 16-bit is left alone -- guessing is what this whole class came from. So are
# Ghidra's stack placeholders (`in_stack_*`, `register0x*`): those are
# uninitialised globals in the port and W68 would only move the wild write
# somewhere tidier. tools/port_guard.py check 4 counts whatever is left.
_P68_SKIP = ('W68', 'W68PTR', 'A68OF', 'LD32U', 'ST32')
_PRIMARY = r'(?:&\s*)?[A-Za-z_]\w*(?:\[[^\[\]]*\]|\.\w+|->\w+)*'
_p1 = _p2 = _p3 = 0

def _ptr_roundtrip(blk):
    """rewrite one function block; needs its own DECLARED local types"""
    global _p1, _p2, _p3
    if not blk.startswith('/* ---- 0x'):
        return blk
    ty = {}
    for _d, _v in re.findall(r'^\s{2}([\w ]+?)\s+(\*?\w+);', blk, re.M):
        ty[_v.lstrip('*')] = _d.strip() + ('*' if _v.startswith('*') else '')

    def _is16(expr):
        """is this operand a DECLARED 16-bit quantity holding a 68K address?"""
        m = re.match(r'&?\s*([A-Za-z_]\w*)((?:\[[^\[\]]*\])?)', expr)
        if not m:
            return False
        base, sub = m.group(1), m.group(2)
        if base in _P68_SKIP or base.startswith(('in_stack', 'register0x')):
            return False
        t = ty.get(base)
        if sub:                                  # psVar10[0x3c] -- a short* element
            return bool(t) and t.rstrip('*') in ('short', 'ushort') and t.endswith('*')
        if t in ('short', 'ushort', 'undefined2'):
            return True
        if t:                                    # declared something else: leave it
            return False
        # a global: ask Ghidra's own width table, keyed by 68K address
        g = re.match(r'_?(?:DAT|[a-z]{1,3}Ram)_?([0-9a-fA-F]{4,8})', base)
        if g and DATW.get(g.group(1).upper()) == 2:
            return True
        # a 16-bit A-register pseudo (unaff_A6w, extraout_A1w): the low word of an
        # address register, which is a work-RAM address by construction
        return bool(re.match(r'(?:extraout|unaff|in)_A\d w?$'.replace(' ', ''), base))

    def _sub_expand(m):
        global _p1, _p2
        cast, operand = m.group(1), m.group(2)
        # P1: the truncation is right there -- cancel both halves
        c = re.match(r'\(short\)\s*(' + _PRIMARY + r')', operand)
        if c:
            inner = c.group(1)
            ib = re.match(r'&?\s*([A-Za-z_]\w*)', inner).group(1)
            # a pseudo A-register is declared `byte *` in pseudoregs.h, NOT as a
            # local, so ty has nothing for it -- and `(short)unaff_A6` is exactly
            # the truncation this rule exists to cancel (FUN_00014B5E).
            is_ptr = ('*' in ty.get(ib, '')
                      or bool(re.match(r'(?:extraout|unaff|in)_A\d$', ib)))
            if is_ptr:
                _p1 += 1
                return '(%s *)(%s)' % (cast, inner)
            return m.group(0)
        if _is16(operand):
            _p2 += 1
            return '(%s *)W68(%s)' % (cast, operand)
        return m.group(0)

    blk = re.sub(r'\((\w+)\s*\*\)\(int\)\s*((?:\(short\)\s*)?' + _PRIMARY + r')',
                 _sub_expand, blk)

    def _sub_trunc(m):
        global _p3
        lhs, rhs = m.group(1), m.group(2)
        if '*' not in ty.get(rhs, ''):
            return m.group(0)
        lt = ty.get(lhs)
        if lt is None:
            g = re.match(r'_?(?:DAT|[a-z]{1,3}Ram)_?([0-9a-fA-F]{4,8})', lhs)
            if not (g and DATW.get(g.group(1).upper()) == 2):
                return m.group(0)
        elif lt not in ('short', 'ushort'):
            return m.group(0)
        _p3 += 1
        return '%s = (short)A68OF(%s);' % (lhs, rhs)

    return re.sub(r'(\w+)\s*=\s*\(short\)\s*([A-Za-z_]\w*)\s*;', _sub_trunc, blk)

src = ''.join(_ptr_roundtrip(b) for b in re.split(r'(?=/\* ---- 0x)', src))

# ---- P3's MISSING HALF: A TRUNCATING STORE THROUGH A DEREFERENCE ------------------
#
# P3 rewrites `name = (short)ptr;` to go through A68OF, because the 68000 kept
# work-RAM pointers in 16 bits and truncating an SH-2 pointer destroys it. Its
# matcher requires the LHS to be a BARE IDENTIFIER, so it never saw the far more
# common form -- a store through a cast:
#
#     *(short *)(unaff_A6 + 0x4c) = (short)p_A4;
#
# MEASURED 2026-10-03 (FULL-PORT.md 65). That is FUN_0000D608 registering a hit: the
# defender records WHO hit it, as the attacker's address truncated to 16 bits. The
# 68000 stores 0xC000 (A4 = 0xFFFFC000, the player); the port stored 0xCC from its
# SH-2 pointer 0x0602CC00. On a re-seeded frame 145 that single byte -- 0xFFC84C --
# was the ONLY wrong object byte in all 16 KB.
#
# IT USES THE GUARDED INVERSE A68OF16, not A68OF. The LHS cast proves the store is 16
# bits, but nothing proves the RHS is an SH-2 pointer rather than a value that is
# already a 68K address: of the 10 sites, `p_A4` and `unaff_A6` are pointers and
# `uVar1`, `uVar4`, `uVar5`, `p_D0_00` may not be. A68OF16 tests the range and is the
# identity otherwise, so it is correct for both without having to know which.
_P3D = re.compile(r'(\*\(\s*u?short\s*\*\s*\)\s*\([^;]*?\)\s*=\s*\(short\))'
                  r'\s*([A-Za-z_]\w*)\s*;')
src, _p3d = _P3D.subn(lambda m: '%s A68OF16(%s);' % (m.group(1), m.group(2)), src)
print('truncating stores through a dereference given the guarded inverse (P3d): %d'
      % _p3d)

# ---- A `.w` READ AT THE BASE OF A 4-BYTE CELL TAKES THE *HIGH* HALF ------------
#
# The 68000's `move.w (0xfffff132).w,D0w` reads the two bytes AT that address, which
# on a big-endian machine are the TOP half of the long stored there. Ghidra renders
# it as a bare narrowing assignment -- `uVar2 = _DAT_fffff132;` with uVar2 a ushort --
# and C defines that as the LOW-order bits, i.e. the bytes at 0xFFF134. The far half.
#
# MEASURED 2026-10-02. 0xFFFFF132 is a 32-bit fixed-point camera position: FUN_000039C8
# advances it by `_DAT_fffff158 * 0x100` and takes `(uint)_DAT_fffff132 >> 0x10` as the
# integer part. FUN_0000E052 and FUN_000049BA read the top half with `move.w` to clamp
# the player, got the fraction instead, and the player's position came out 0x30EC where
# the arcade has 0x110C -- a constant +0x1FE0 across eight fields of the player record.
#
# NARROWING THE DECLARATION IS THE WRONG FIX and was tried: it makes `>> 0x10` always
# zero, so 0xFFF120 became the literal 0x1000 and the camera died.
#
# THE EXCLUSION MATTERS. `rng_next` does
#     (short)(_DAT_fffff014 >> 0x10) + (short)_DAT_fffff014
# where the bare narrowing really is the low half -- both halves of one long in one
# expression, genuine 32-bit arithmetic and not a memory read. So a function that ALSO
# takes the symbol's high half is left alone. That is the only one of the seven
# candidates in the program; the other six are all 0xFFFFF132.
_WIDEDAT = {('%sDAT_%s' % (p_, a.lower().lstrip('0')))
            for a, w_ in DATW.items() if w_ == 4 for p_ in ('_', '')}
_hi = [0]

def _hi_half(blk):
    if not blk.startswith('/* ---- 0x'):
        return blk
    ty = {}
    for _t, _v in re.findall(r'^\s{2}([\w ]+?)\s+(\*?\w+);', blk, re.M):
        ty[_v.lstrip('*')] = _t.strip()

    def _sub(m):
        lhs, rhs = m.group(1), m.group(2)
        if ty.get(lhs) not in ('ushort', 'short', 'undefined2') or rhs not in _WIDEDAT:
            return m.group(0)
        if ('%s >> 0x10' % rhs) in blk:
            return m.group(0)          # genuine 32-bit arithmetic; see rng_next
        _hi[0] += 1
        return '%s = (ushort)(%s >> 0x10);' % (lhs, rhs)

    return re.sub(r'\b(\w+)\s*=\s*(_?DAT_[0-9a-f]{4,8})\s*;', _sub, blk)

src = ''.join(_hi_half(b) for b in re.split(r'(?=/\* ---- 0x)', src))

# ---- A NARROWING INSIDE AN EXPRESSION IS ALSO A .w READ OF THE LONG'S HIGH HALF ---
#
# _hi_half above only matches a BARE assignment `lhs = DAT_x;` with a 16-bit lhs, and
# it skips any function that reads the same cell with an explicit `>> 0x10` anywhere
# (the rng_next exclusion: a long loaded into a REGISTER and then used `.w` really is
# the low half, and the decompiled C cannot tell the two apart).
#
# That misses the far more common form, a narrowing inside an expression:
#
#     if (*(ushort *)(unaff_A6 + 0xc) <= (ushort)(_DAT_fffff132 + 0x20U)) ...
#
# On the 68000 that is `cmp.w 0xFFF132, Dn` -- a WORD READ AT THE LONG'S OWN ADDRESS,
# which on a big-endian machine is the HIGH half. In C the cast takes the LOW half.
#
# MEASURED 2026-10-03. _DAT_fffff132 is the camera: 0x10CC5000 at the seed, high half
# 0x10CC = the position in PIXELS, low half 0x5000 = the fraction. The site above
# clamps the player to the camera's left edge, and the port wrote 0x5000 + 0x18 =
# 0x5018 into slot 0 +0x0C where the arcade has 0x110C. The watch named the writer in
# one run (FUN_000049BA, slot 0's own handler, 0x11 -> 0x50 at 0xFFC00C).
#
# WHY IT IS SAFE HERE AND NOT IN GENERAL. A `.w` read AT a long's base address can
# only ever be its high half -- to reach the low half the 68000 reads at base+2, which
# the decompiler spells as a different symbol. The exception is a long that went
# through a REGISTER first, and that is what the rng_next exclusion protects. So this
# runs over a NAMED CELL LIST, arbitrated by the gate one address at a time, exactly
# as CPORT_CLAMP is -- not over every 4-byte symbol.
#
# The census says the scope is narrow and uniform: 54 sites, ALL on _DAT_fffff132,
# ALL of the form `(ushort)(CELL + CONST)`. No other 4-byte cell in the corpus has
# this shape, so there is nothing else to hit by accident.
#
# STILL OPEN, and recorded rather than guessed: the twelve WRITES of this cell
# (`_DAT_fffff132 = 0x1000`) set the whole long to 0x00001000, where the 68000's
# `move.w #0x1000,0xFFF132` sets the PIXEL half and leaves the fraction. Level one
# starts at pixel 0x1000 and the seed reads 0x10CC, so the word form is the right
# one -- but those resets do not run in this scene, so the gate cannot arbitrate
# them yet and they are left alone.
# THE WIDTH CENSUS COMES FROM THE 68K BYTES, NOT FROM A PLAUSIBILITY ARGUMENT.
#
# m68k-elf-objdump over the whole program (it is in the tree:
# ~/src/marsdev/mars/m68k-elf/bin/m68k-elf-objdump -D -b binary -m m68k:68000)
# answers "what width does this program use at this address" exhaustively. For
# 0xFFF132:
#
#     49 movew  +  1 addw      reads  -> the HIGH half
#      5 movew                 writes -> the HIGH half, fraction left alone
#      1 addl   +  1 movel            -> BOTH inside FUN_000039c8, and they are the
#                                        fixed-point advance itself:
#         39d2: extl %d0        39d4: lsll #8,%d0
#         39d6: addl 0xfffff132,%d0   39da: movel %d0,0xfffff132
#         39de: swap %d0        39e0: subiw #4096,%d0     <- takes the high half next
#
# So the rule for this cell is not a heuristic: EVERY access outside FUN_000039c8 is
# 16 bits, and 16 bits at a long's own address is its HIGH half on a big-endian
# machine. The exempt function is named in the variable, so the exemption is as
# explicit as the cell.
#
# The spellings this has to cover are all of them, because Ghidra renders a `.w` read
# of a cell it declared 4 bytes wide in whatever shape the surrounding expression
# takes:
#     (ushort)(CELL + 0x20U)                  a narrowing cast
#     CELL < _DAT_fffff13c                    a comparison with a ushort cell
#     sVar9 = CELL + 0x90;                    a 16-bit local
#     *(ushort *)(unaff_A6 + 0xc) = CELL + 0x18U;    a 16-bit store
#     CONCAT22(uVar3, CELL - 0x50U)           a 16-bit half of a packed pair
# and the WRITES, where `CELL = 0x1000` sets the whole long to 0x00001000 but
# `movew #4096,0xfffff132` sets the PIXEL half and leaves the fraction. Level one
# starts at pixel 0x1000 and the seed reads 0x10CC, so the word form is the right one.
#
# MEASURED 2026-10-03, and this is what the cell gates: FUN_000039C8, the camera
# update, was ENTERED 0 TIMES in the port's frame (CPORT_COUNT) because
# `if (CELL < _DAT_fffff13c)` compared a 32-bit camera against a 16-bit limit and read
# FALSE where `movew/cmpw` reads TRUE.
_CPORT_HI = []
for _x in (os.getenv('CPORT_HI') or 'FFFFF132@FUN_000039c8').split(','):
    _x = _x.strip()
    if not _x:
        continue
    _a, _, _ex = _x.partition('@')
    _CPORT_HI.append((_a.strip().upper(),
                      set(e for e in _ex.split('+') if e)))
_nhi_r = _nhi_w = 0
_HI_RE = None
for _cell, _exempt in _CPORT_HI:
    _nm = '_DAT_%s' % _cell.lower()
    if _nm not in src:
        _nm = 'DAT_%s' % _cell.lower()
    _blocks = re.split(r'(?=/\* ---- 0x)', src)
    for _bi, _blk in enumerate(_blocks):
        # the block's OWN header names the function it is; an exempt one is skipped
        if _exempt and any(re.search(r'^/\* ---- 0x[0-9A-F]+\s+%s\b' % re.escape(e), _blk)
                           for e in _exempt):
            continue
        out, i = [], 0
        while True:
            j = _blk.find(_nm, i)
            if j < 0:
                out.append(_blk[i:]); break
            # a longer identifier that merely CONTAINS this name is not this symbol
            before = _blk[j-1] if j else ' '
            after_c = _blk[j+len(_nm)] if j + len(_nm) < len(_blk) else ' '
            if (before.isalnum() or before == '_') or (after_c.isalnum() or after_c == '_'):
                out.append(_blk[i:j+len(_nm)]); i = j + len(_nm); continue
            rest = _blk[j+len(_nm):]
            if re.match(r'\s*>>\s*(0x10|16)\b', rest):
                out.append(_blk[i:j+len(_nm)]); i = j + len(_nm); continue
            wm = re.match(r'\s*=(?!=)\s*', rest)
            if wm:
                # a WRITE: find the value, to the first top-level delimiter
                depth, end = 0, -1
                for k in range(wm.end(), len(rest)):
                    ch = rest[k]
                    if ch in '([': depth += 1
                    elif ch in ')]':
                        if depth == 0: end = k; break
                        depth -= 1
                    elif depth == 0 and ch in ';,': end = k; break
                if end < 0:
                    out.append(_blk[i:j+len(_nm)]); i = j + len(_nm); continue
                val = rest[wm.end():end]
                out.append(_blk[i:j])
                # THE VALUE CAN READ THE CELL TOO. `C = C + 0xc00` is a read-modify-write
                # of the HIGH half, and emitting SETHI16(C, (C + 0xc00)) fed the whole
                # 32-bit cell into a 16-bit store. Measured 2026-10-04 on 0xFFC014, the
                # eye's X velocity: the arcade pans 12 units a frame, the port ~1. Rewrite
                # the reads inside the value the same way as reads anywhere else.
                if not re.search(r'>>\s*(0x10|16)\b', val):
                    val = re.sub(r'(?<![\w])%s(?![\w])' % re.escape(_nm),
                                 '(ushort)(%s >> 0x10)' % _nm, val)
                out.append('SETHI16(%s, (%s))' % (_nm, val))
                i = j + len(_nm) + end
                _nhi_w += 1
            else:
                out.append(_blk[i:j])
                out.append('(ushort)(%s >> 0x10)' % _nm)
                i = j + len(_nm)
                _nhi_r += 1
        _blocks[_bi] = ''.join(out)
    src = ''.join(_blocks)
print('CPORT_HI (%s): HIGH half taken at %d read(s) and %d write(s)'
      % (','.join('%s@%s' % (c, '+'.join(sorted(e)) or '-') for c, e in _CPORT_HI),
         _nhi_r, _nhi_w))

print('word-read-of-a-long rewritten to the HIGH half: %d site(s)' % _hi[0])

# ---- CPORT_COUNT: DID THIS FUNCTION ACTUALLY RUN, AND HOW MANY TIMES ------------
#
# The port-side counterpart of the 68K's COUNTSITES, and it exists because a
# question kept coming up that no other instrument here can answer. PORTWATCH names
# the function that CHANGED a cell -- but a function whose every write is IDEMPOTENT
# on the current state changes nothing, so a zero-span watch cannot distinguish "it
# did not run" from "it ran and wrote the same value".
#
# That is exactly the camera: FUN_000039C8 does
#     _DAT_fffff138 = camera >> 16;   _DAT_fffff132 += _DAT_fffff158 * 0x100;
#     _DAT_fffff120 = -((camera >> 0x10) - 0x1000);
# and with the camera frozen at the seed EVERY one of those writes is a no-op, so
# watching any of them is circular.
#
# `CPORT_COUNT=a,b,c` inserts `port_fn_count[i]++;` as the first statement of each
# named function. The driver owns the array and publishes it, so this costs nothing
# when the variable is unset. Max 8, because that is what the result block has room
# for without moving anything.
_counts = [x.strip() for x in (os.getenv('CPORT_COUNT') or '').split(',') if x.strip()]
if len(_counts) > 8:
    raise SystemExit('cport_prep: CPORT_COUNT takes at most 8 names (got %d)'
                     % len(_counts))
if _counts:
    _ncins = 0
    for _ci, _cn in enumerate(_counts):
        # the DEFINITION, not a call: `uint NAME(...)\n{`
        _cp = re.compile(r'(\b(?:uint|ulonglong|int|void)\s+%s\s*\([^;{]*\)\s*\n\{\n)' % re.escape(_cn))   # any return type (2026-10-04: sprite_frame_lookup is ulonglong)
        # (first-entry sequence << 16) | count -- see sh_src/port_l2.c. The shared
        # tick happens BEFORE the latch so the first function entered reads 1, not 0,
        # and 0 stays unambiguously "never entered".
        src, _k = _cp.subn(
            r'\1  { port_fn_seq++; if ((port_fn_count[%d] >> 16) == 0) '
            r'port_fn_count[%d] |= (port_fn_seq & 0xFFFFu) << 16; '
            r'port_fn_count[%d]++; }\n' % (_ci, _ci, _ci), src, count=1)
        if not _k:
            raise SystemExit('cport_prep: CPORT_COUNT names %s, which has no '
                             'definition in the generated unit' % _cn)

        # THE CALL-SITE COUNT BESIDE THE ENTRY COUNT, every time. An entry count on a
        # function with 21 call sites says nothing about WHICH site ran; twice on
        # 2026-10-04 a conclusion was drawn from exactly that and had to be withdrawn.
        _sites = len(re.findall(r'\b%s\s*\(' % re.escape(_cn), src)) - 1
        sys.stderr.write('cport_prep: CPORT_COUNT[%d] %s -- %d call site(s) in the unit; an entry count\n'
                         '             on a multi-site function does not name the site\n' % (_ci, _cn, _sites))
        _ncins += _k
    print('CPORT_COUNT: entry counters inserted in %d function(s): %s'
          % (_ncins, ', '.join(_counts)))
print('pointer round-trip: %d cancelled (P1), %d W68-expanded (P2), '
      '%d A68OF-truncated (P3)' % (_p1, _p2, _p3))
# CPORT_STUBS: replace a function's BODY with `return 0;`.
#
# Needed because Ghidra renders a hardware wait it cannot see through as an
# infinite loop. `wait_frame_flag` is literally
#     /* WARNING: Do nothing block with infinite loop */ do { } while( true );
# because the flag it polls is set by IRQ4, which is not in the decompiled
# program. Calling it in a rom HANGS THE MACHINE -- no barcode, no diagnosis.
#
# This is a SUBSTITUTION and it must stay visible: the generated body says so,
# and a driver that stubs the vsync barrier is supplying the frame boundary
# itself. Never stub anything that computes -- only a wait whose signal is
# outside the program.
# An entry may be `name` (body becomes `return 0;`) or `name->target`, whose body
# becomes `return target();`. The redirect form exists for object_dispatcher: its
# decompiled body calls a 68K ADDRESS out of work RAM and passes no A6, so the port
# supplies its own (see tools/cport_dispatch.py). Redirecting rather than deleting
# keeps every existing caller working.
_stubs = {}
for _x in (os.getenv('CPORT_STUBS') or '').split(','):
    _x = _x.strip()
    if not _x:
        continue
    _n, _, _t = _x.partition('->')
    _stubs[_n.strip()] = _t.strip() or None
if _stubs:
    _done = set()
    def _stub_body(m):
        rty, nm, prm = m.group(1), m.group(2), m.group(3)
        if nm not in _stubs:
            return m.group(0)
        _done.add(nm)
        tgt = _stubs[nm]
        if tgt:
            # THE SIGNATURE HAS TO SURVIVE THE REDIRECT. This used to emit
            # `uint NAME() { return tgt(); }` unconditionally, which drops the
            # return TYPE and every ARGUMENT -- fine for a void-ish stub and wrong
            # for a redirect that has to pass A0 and return a packed pair
            # (port_zoom_scale, FULL-PORT.md 30d).
            args = [a.strip().split()[-1].lstrip('*') for a in prm.split(',')
                    if a.strip() and a.strip() != 'void']
            decl = prm if prm.strip() else 'void'
            return ('%s %s(%s)\n{\n    /* REDIRECTED via CPORT_STUBS to %s: the decompiled\n'
                    '     * body cannot run on the SH-2 as written. */\n'
                    '    extern %s %s(%s);\n    return %s(%s);\n}'
                    % (rty, nm, prm, tgt, rty, tgt, decl, tgt, ', '.join(args)))
        return ('%s %s(%s)\n{\n    /* STUBBED via CPORT_STUBS: Ghidra rendered this as an\n'
                '     * infinite loop because the signal it waits on is set outside the\n'
                '     * decompiled program (IRQ4). Running the real body hangs the SH-2. */\n'
                '    return 0;\n}' % (rty, nm, prm))
    # the signature may carry register PARAMETERS now; `\(\)` stops matching and
    # the stub silently does not apply, which for wait_frame_flag means the rom
    # hangs in a decompiled infinite loop
    src = re.sub(r'(uint|ulonglong) (\w+)\(([^)]*)\)\s*\n\{.*?\n\}',
                 _stub_body, src, flags=re.S)
    _missing = set(_stubs) - _done
    if _missing:
        raise SystemExit('CPORT_STUBS names %d function(s) not in the unit: %s'
                         % (len(_missing), ', '.join(sorted(_missing))))
    print('stubbed/redirected %d function(s): %s'
      % (len(_done), ', '.join('%s%s' % (n, '->' + _stubs[n] if _stubs[n] else '')
                               for n in sorted(_done))))

# EVERY INDIRECT CALL THROUGH A 68K CODE POINTER IS A WILD JUMP.
#
# `(**(code **)(X))()` means: read a code pointer from address X and call it. On
# the 68K that pointer is a program address like 0x0EE80. On the SH-2 it is
# meaningless, and calling it jumps into nowhere. Measured 2026-09-28 on the first
# PORTFULL rom: the master ended up executing at PC 0x00000002 and then span in
# main_irq (0x06000264) for 40% of all cycles -- a hang with no other symptom.
#
# object_dispatcher is the famous one, but it is not the only one: there are five
# more, four of them 68K JUMP TABLES (`&DAT_0000f556 + n*4`). They are rewritten to
# go through port_call68(), which looks the address up in the generated table and
# COUNTS a miss instead of jumping to it (sh_src/port_l2.c).
#
# A balanced-paren scan, not a regex: the operand nests parentheses freely
# (`(**(code **)((int)&PTR_FUN_00017e24 + (int)(short)((ushort)DAT_fffff142 << 2)))()`).
_MARK = '(**(code **)'
_ind_literal_rom = [0]
def _rewrite_indirect(text):
    out, i, n = [], 0, 0
    while True:
        j = text.find(_MARK, i)
        if j < 0:
            out.append(text[i:]); break
        k = j + len(_MARK)
        while k < len(text) and text[k] in ' \t':
            k += 1
        if k >= len(text) or text[k] != '(':
            out.append(text[i:k]); i = k; continue
        depth, m = 0, k
        while m < len(text):                      # span the pointer EXPRESSION
            if text[m] == '(': depth += 1
            elif text[m] == ')':
                depth -= 1
                if depth == 0: break
            m += 1
        expr = text[k:m + 1]
        rest = text[m + 1:]
        if not rest.startswith(')()'):
            out.append(text[i:m + 1]); i = m + 1; continue
        # An indexed code-pointer table may have a numeric ROM base instead
        # of a linker symbol. Native 19B64..19B76 loads table 1A076, then
        # indexes it by state*4. LD32U only handles alignment, not rebasing:
        # the old raw read returned zero and every boss state dispatch missed.
        # Restrict this to a scaled index plus a literal program-ROM base;
        # object-field offsets and already mapped/symbolic tables stay intact.
        literal = re.search(r'\+\s*(0x[0-9a-f]+)\s*\)$', expr)
        if literal and '<< 2' in expr and 'ROM_BASE' not in expr:
            addr = int(literal.group(1), 16)
            if 0x100 <= addr < 0x40000:
                expr = expr[:literal.start(1)] + '((int)ROM_BASE + ' + literal.group(1) + ')' + expr[literal.end(1):]
                _ind_literal_rom[0] += 1
        out.append(text[i:j])
        # a SITE ID, so a miss names the call site and not just the target.
        # Measured 2026-09-28: all 208 misses in a frame reported target 0x00000,
        # which identifies the value but not which of the six indirect sites read
        # it, and three rounds of guessing from the target alone got nowhere.
        # LD32U, not `*(uint *)`: this rewrite runs AFTER the unaligned-access pass,
        # so emitting a raw 32-bit deref here puts back exactly what that pass
        # removed. Measured 2026-09-28: it did, and the jump-table read kept
        # returning 0 through two more build cycles.
        out.append('port_call68_at(%d, LD32U%s)' % (n, expr))
        i = m + 1 + 3
        n += 1
    return ''.join(out), n
src, _nind = _rewrite_indirect(src)
print('indirect 68K code-pointer calls routed through port_call68: %d' % _nind)
_pass_report('indexed indirect tables with literal ROM bases rebased', _ind_literal_rom[0], 1)

# A CALL TO A LITERAL 68K ADDRESS IS ALSO A WILD JUMP, and _rewrite_indirect above
# does NOT catch it: that pass handles `(**(code **)(X))()` -- a pointer read FROM
# MEMORY -- while Ghidra also emits
#
#     uVar2 = (*(code *)0x4ebe)();
#
# when the target is a constant it never made a function. There is no pointer to
# load, so the pattern never matched, and the call went straight to SH-2 address
# 0x4EBE -- low SDRAM, not the 68K program.
#
# FOUND BY AUDIT, NOT BY A FAILURE (tools/cport_audit.py, 2026-10-01): 18 sites
# across 5 targets (0x04EBE, 0x07DB0, 0x08BBE, 0x09FFC, 0x0B8BC), none of them in
# the dispatch table. Every port defect before this one was found by a failing cell
# pointing at it; this class had no cell pointing at it because the functions
# holding these sites are not reached in the gate's frame.
#
# Routed through the same lookup, so an unknown target is a COUNTED MISS and not a
# jump. The real fix is to create those 5 functions in Ghidra and add them to the
# roots; until then the gate will report them.
_FNAME = {}
try:
    for _l in open('docs/audit/func_ranges.txt'):
        if _l.startswith('#'):
            continue
        _f = _l.split()
        if len(_f) >= 4:
            _FNAME[_f[2].upper().replace('0X', '').lstrip('0').rjust(5, '0')] = _f[3]
except Exception:
    pass

_LITCALL = re.compile(r'\(\s*\*\s*\(\s*code \*\s*\)\s*(0x[0-9a-fA-F]+)\s*\)\s*\(\s*\)')
def _rewrite_litcall(text, first_site):
    n = [first_site]
    def rep(m):
        site = n[0] % 8          # port_call68_at buckets per-site counts into 8
        n[0] += 1
        return 'port_call68_at(%d, %su)' % (site, m.group(1))
    text, cnt = _LITCALL.subn(rep, text)
    return text, cnt
src, _nlit = _rewrite_litcall(src, _nind)

# ---- A CALL THROUGH A code* VARIABLE THAT HOLDS A 68K ADDRESS IS THE SAME WILD JUMP ---------
#
# The third shape, and the one that FROZE THE GAME. Every player form's state handler
# is `lea TAIL(pc),a0 ; andi.w #7,d6 ; bne shared ; TAIL: ...` and the shared button
# block ends `jmp (a0)` (prog68k 0x7CC0/0x7D16 for form 1, the same bytes at 0x4DC4,
# 0x8ACE, 0x9F0C, 0xB7CC, 0xC50A for the others). Ghidra keeps the lea as
#
#     UNRECOVERED_JUMPTABLE_01 = (code *)0x7cca;   ...   uVar4 = (*UNRECOVERED_JUMPTABLE_01)();
#
# a call through a LOCAL that holds the literal: not a load from memory (_rewrite_indirect)
# and not a literal cast at the call (_rewrite_litcall), so neither pass matched and the
# SH-2 jumped to cart address 0x7CCA, where the 68K program's bytes are not SH-2 code.
#
# MEASURED 2026-10-24, the R60ROWFAST tape stall (rom rh_g, vint 32754): the SDRAM write
# watch showed the master taking 80,830 illegal-instruction exceptions in two frames at
# PC 0x7CCA with PR = FUN_00007cc0+0x20e, the hero form 1 (0xFFC06A = 1) in state 0 with
# flags +0x2c = 0: button bit 0 edged while +0x2c bit 4 was clear, so the shared block
# took none of its three transitions and jumped back to the handler's tail. The V-ISR
# kept counting vints while the tick counter froze, the slave waited for a snapshot
# that never came and the window stayed open: the stall looked like a renderer ordering
# bug and was a port wild jump. 195 call sites in 121 functions carry this shape; the
# targets are the six forms' two tails each (0x4DCE/0x4EBE, 0x7CCA/0x7DB0, 0x8AD8/0x8BBE,
# 0x9F16/0x9FFC, 0xB7D6/0xB8BC, 0xC514/0xC5FA), hand functions at hand/AT_FUN_<tail>.c;
# a tail without one is a COUNTED miss (port_l2_out[13], [40..47]) and not a jump.
# The live D5/D6/D7 are published first, as the attack-lookup callers do: the tails
# read D7 (direction, bit 4 and 0xC0) and the shared block has already masked D6.
_PTRCALL = re.compile(r'\(\*(UNRECOVERED_JUMPTABLE(?:_\d+)?)\)\(\)')
def _rewrite_ptrcall(text, first_site):
    n = [first_site]; cnt = [0]; fns = set()
    blocks = re.split(r'(?=/\* ---- 0x)', text)
    for bi, blk in enumerate(blocks):
        if not _PTRCALL.search(blk):
            continue
        pub = ''.join('port_set_reg(%d, (uint)p_D%d), ' % (r, r) for r in (5, 6, 7)
                      if re.search(r'\bp_D%d\b' % r, blk))
        def rep(m):
            site = n[0] % 8
            n[0] += 1; cnt[0] += 1
            return '(%sport_call68_at(%d, (uint)%s))' % (pub, site, m.group(1))
        blocks[bi] = _PTRCALL.sub(rep, blk)
        fns.add(bi)
    return ''.join(blocks), cnt[0], len(fns)
src, _nptr, _nptrf = _rewrite_ptrcall(src, _nind + _nlit)
_pass_report('calls through a code pointer holding a 68K address routed through port_call68 (%d functions)' % _nptrf, _nptr, 40)

# ---- FEED EVERY extraout_ FROM THE CALL THAT PRODUCES IT ----------------------
#
# Section 16 declared return storage on 47 producers, which made their bodies
# materialise. This is the OTHER HALF: the CALLER still reads `extraout_A1` -- a
# pseudo-register the port turns into a global that nothing ever writes -- instead
# of the value the call just returned.
#
# Measured 2026-10-01 end to end. sprite_build_depth_banded does
#
#     uVar11 = zoom_scale_lookup(p_A0);     <- returns the sprite scale
#     ...
#     uVar10 = extraout_A1;                 <- reads a global that is 0
#
# and `uVar10` drives sprite-record fields +6/+7, which come out 0 where the arcade
# has 0x3796. docs/audit/extraout_producers.json (Ghidra's own SSA) names the
# producer for all 423 sites in 194 callers, with ZERO produced by an indirect call.
#
# WHY NOT TRUST GHIDRA'S REGISTER NAME: it says this value is A1, and the oracle
# proves zoom_scale_lookup never changes A1 across any of its four captured calls --
# the real output is D1 = 0x40. The NAME is wrong and the VALUE is real, so the port
# assigns from the call's RETURN rather than trying to model the register.
#
# A packed two-register return is CONCAT44(first, second): the second is the low
# half, and that is the computed value where the pseudo's own storage is not in the
# declared return at all (zoom_scale_lookup's case exactly).
try:
    _EXPROD = json.load(open('docs/audit/extraout_producers.json'))
except Exception:
    _EXPROD = []
    print('cport_prep: no docs/audit/extraout_producers.json -- extraout feeding is OFF'
          ' (regenerate with tools/ghidra_run.sh script extraout_producers.py)')

_RETOF = {}
for _e in _CONV if '_CONV' in dir() else []:
    pass
try:
    for _e in json.load(open('docs/audit/reg_convention.json')):
        _RETOF[_e['entry'].upper().lstrip('0').rjust(5, '0')] = _e.get('ret') or ''
except Exception:
    pass

_RPARAM = {}
try:
    for _e in json.load(open('docs/audit/reg_convention.json')):
        _RPARAM[_e['entry'].upper().lstrip('0').rjust(5, '0')] = {
            _p['storage'].split(':')[0].upper().rstrip('WUB'): _p['name'] for _p in _e.get('params', [])}
except Exception:
    pass

# WHICH REGISTERS EACH 68K FUNCTION WRITES (tools/m68k_regwrites.py, 2026-10-25).
# Ghidra creates an `extraout_R` pseudo exactly where the producer's declared return
# does NOT carry R, so the value is one of two things, and only the 68K says which:
#   PASSED THROUGH -- the producer never writes R, so R is whatever the caller had
#     before the call (0x451E leaves A0/A1 alone: 0x4330's `p_A0 = extraout_A0_00`
#     is its own A0, not the return of a 0x442E call three blocks later);
#   WRITTEN AND NOT RETURNED -- the producer computes R and C cannot see it (0xE2DC's
#     D1). Only a hand body that PUBLISHES the register can supply it (_XO_PUB).
try:
    _REGW = json.load(open('docs/audit/regwrites.json'))
except Exception:
    _REGW = {}
    print('cport_prep: no docs/audit/regwrites.json -- extraout feeds cannot tell a passed-through'
          ' register from a written one (python3 tools/m68k_regwrites.py --json docs/audit/regwrites.json)')

# (producer, register) -> the global its hand body publishes that register in.
# The enemy-AI helpers' hand bodies (see the AI-helper pass below) publish D0.
# 0xE2DC's D1 is NOT here: its hand body computes the byte and drops it, and every read
# found (0x103E0, 0x13B2C, 0x15434) passes it on as 0xE23C's D1, which 0xE23C ignores.
_XO_PUB = {('0E23C', 'D0'): 'port_e_d0', ('0E266', 'D0'): 'port_e_d0',
           ('0E2C2', 'D0'): 'port_e_d0'}

_XO_LOG = []

def _xo_reg(s):
    return s.split(':')[0].upper().rstrip('WUB')

def _xo_sub(s):
    # Ghidra's sub-register spellings: D1u = bits 16-31, D1w = 0-15, D1b = 0-7
    m = re.match(r'[AD][0-7]([wub])?:', s)
    return (m.group(1) or '') if m else '?'

def _xo_writes(key, R, sub):
    """True/False: does the producer change this (sub-)register; None: cannot tell"""
    w = _REGW.get(key)
    if w is None or '?' in w or sub == '?':
        return None
    n = w.get(R, 0)
    return n == 4 if sub == 'u' else n > 0

def _xo_parts(key):
    return [x.strip() for x in _RETOF.get(key, '').split(',') if x.strip() and x.strip() != '<VOID>']

def _xo_split_args(a):
    out, d, cur = [], 0, ''
    for ch in a:
        if ch in '([': d += 1
        elif ch in ')]': d -= 1
        if ch == ',' and d == 0:
            out.append(cur); cur = ''
        else:
            cur += ch
    out.append(cur)
    return [x.strip() for x in out]

def _xo_calls(body, names):
    """Every STATEMENT-level call of a function in `names`, assigned (`v = f(...)`)
    or bare (`f(...)`), with the position of the ';' that ENDS the statement. A read
    inside the argument list is BEFORE that ';' and so BEFORE the call's result."""
    out = []
    for c in re.finditer(r'\n([ \t]*)(?:(\w+)\s*=\s*(?:\([\w ]+\**\)\s*)?)?(\w+)\s*\(', body):
        if c.group(3) not in names:
            continue
        i, d = c.end(), 1
        while i < len(body) and d:
            if body[i] == '(': d += 1
            elif body[i] == ')': d -= 1
            i += 1
        if not re.match(r'\s*;', body[i:]):
            continue                      # part of a larger expression: not a statement
        out.append({'start': c.start() + 1, 'ind': c.group(1), 'var': c.group(2),
                    'fn': c.group(3), 'fpos': c.start(3), 'semi': body.find(';', i),
                    'args': body[c.end():i - 1]})
    return out

def _xo_encloses(body, a, b):
    # a statement whose block CLOSES between it and the read cannot reach the read
    # (a sibling branch, a conditional before it)
    d = 0
    for ch in body[a:b]:
        if ch == '{': d += 1
        elif ch == '}':
            d -= 1
            if d < 0:
                return False
    return True

def _xo_old(body, r, sites, key, R, ps):
    """THE 868ae772 RULE, kept for the producers whose value C cannot express (the
    register is written by the producer and not returned, or the 68K walk cannot see
    the producer whole): the attributed call, or a call in this body whose return
    declares the register; where the caller has two or more sites of the producer, the
    last enclosing call before the read. Changed only where it was wrong by its own
    terms: a call whose ARGUMENT LIST holds the read is not before the read (0x3190's
    second 0x31FA call read `extraout_D0` in its own arguments, 2026-10-25)."""
    name = _FNAME.get(key)
    parts = _xo_parts(key)
    if R not in [_xo_reg(q) for q in parts]:
        for _k2, _r2 in _RETOF.items():
            _p2 = _xo_parts(_k2)
            if R not in [_xo_reg(q) for q in _p2]:
                continue
            _n2 = _FNAME.get(_k2)
            if not _n2 or not re.search(r'\b(?!_xo\d+\b)\w+\s*=\s*' + re.escape(_n2) + r'\s*\(', body):
                continue
            name, parts = _n2, _p2
            break
    # (a `_xoN` temp is this pass's own naming of a bare call for a DIFFERENT pseudo:
    # the old rule never saw those calls, so it does not see them now)
    _calls = [c for c in re.finditer(r'\n(\s*)(\w+)\s*=\s*' + re.escape(name) + r'\s*\(', body)
              if not re.match(r'_xo\d+$', c.group(2))]
    cm = _calls[0] if _calls else None
    _multi = sum(1 for s_ in sites if s_['callee'] == r['callee']) > 1
    _rd = [mm for mm in re.finditer(r'\b' + re.escape(ps) + r'\b', body)
           if not re.match(r'\s*=[^=]', body[mm.end():])] if _multi else []
    if _rd and len(_calls) > 1:
        _before = [c for c in _calls if body.find(';', c.end()) < _rd[0].start()
                   and _xo_encloses(body, body.find(';', c.end()), _rd[0].start())]
        if _before:
            cm = _before[-1]
    if not cm:
        return None
    semi = body.find(';', cm.end())
    if semi < 0:
        return None
    return {'ind': cm.group(1), 'var': cm.group(2), 'fn': name, 'semi': semi,
            'start': cm.start() + 1}, name, parts

# WHICH CALL FEEDS A READ, AND WITH WHAT (2026-10-25). 868ae772 fed a multi-site pseudo
# from the last enclosing call before its read, but counted a call whose ARGUMENT LIST
# holds the read as before it: 0x3190 / 0x38C0 read `extraout_D0` inside the second
# 0x31FA call's arguments and got the entry value. The rule is now per register class:
#   declared    the producer returns R: its last call that dominates the read (a bare
#               call is given a `_xoN` name);
#   published   an enemy-AI hand body publishes R (_XO_PUB): that global, after its
#               dominating call -- 0x13B2C's `extraout_D0w_03` had no feed at all;
#   passthrough the producer never writes R (docs/audit/regwrites.json), so R is what
#               the caller had: the argument it passed in R, else the return of the last
#               dominating call that declares R, else the caller's own p_R. 0x4330's
#               `p_A0 = extraout_A0_00` after 0x451E is its own A0, where the old feed
#               took a 0x442E call on another path;
#   written / unknown: the producer computes R and C never sees it, or the walk cannot
#               tell -- no rule expresses the value, so the 868ae772 feed is kept as is.
# "Dominates" is textual: the statement ENDS before the read and no block closes between.
def _feed_extraout(text):
    by_caller = {}
    for r in _EXPROD:
        if r.get('indirect'):
            continue
        by_caller.setdefault(r['caller_name'], []).append(r)
    _key_of = {v: k for k, v in _FNAME.items()}
    def _clobber(seg, R, sub):
        # any call in `seg` to a function that may write R: the value is no longer
        # the one this rule found (a sibling branch counts too -- conservative)
        for cm_ in re.finditer(r'\b(\w+)\s*\(', seg):
            k_ = _key_of.get(cm_.group(1))
            if k_ is None:
                continue
            if _xo_writes(k_, R, sub) is not False:
                return cm_.group(1)
        return None
    n = 0
    ntmp = [0]
    for caller, sites in by_caller.items():
        m = re.search(r'^[A-Za-z_][\w ]*\**\s*' + re.escape(caller) + r'\s*\(([^;{]*)\)\s*\n\{',
                      text, re.M)
        if not m:
            continue
        end = text.find('\n}', m.end())
        if end < 0:
            continue
        body = text[m.end():end]
        cparams = m.group(1)
        decls = []
        fed = set()
        # a feed is inserted right after its call, so the LAST one placed reads first:
        # place the published (enemy-AI) D0 feeds last, so they sit directly under the
        # call where the AI-helper pass below recognises them
        sites = sorted(sites, key=lambda r_: (r_['callee'].upper().lstrip('0').rjust(5, '0'),
                                              _xo_reg(r_['storage'])) in _XO_PUB)
        for r in sites:
            key = r['callee'].upper().lstrip('0').rjust(5, '0')
            cname = _FNAME.get(key)
            if not cname:
                continue
            ps = r['pseudo']
            if ps not in body:
                continue
            R = _xo_reg(r['storage'])
            sub = _xo_sub(r['storage'])
            partsC = _xo_parts(key)
            wr = _xo_writes(key, R, sub)
            if R in [_xo_reg(q) for q in partsC] and sub != '?':
                cls = 'declared'
            elif wr is False:
                cls = 'passthrough'
            elif (key, R) in _XO_PUB and sub != '?':
                cls = 'published'
            elif wr is None:
                cls = 'unknown'
            else:
                cls = 'written'
            # the FIRST READ of the pseudo: one pseudo is one SSA definition, so its
            # producer dominates every read and the first one decides
            reads = [mm.start() for mm in re.finditer(r'\b' + re.escape(ps) + r'\b', body)
                     if not re.match(r'\s*=[^=]', body[mm.end():])]
            rd = reads[0] if reads else None
            def dom(cs):
                if rd is None:
                    return cs[:1]
                return [c for c in cs if c['semi'] < rd and _xo_encloses(body, c['semi'], rd)]
            src = None; expr = None; before = False; how = cls
            name, parts = cname, partsC
            if cls == 'passthrough':
                # (1) the producer TAKES R as an argument: R is that argument
                pn = _RPARAM.get(key, {}).get(R)
                dm = dom(_xo_calls(body, {cname}))
                sm = re.search(r'\b' + re.escape(cname) + r'\s*\(([^;{]*)\)\s*\n\{', text)
                if pn and dm and sm:
                    pl = [q.strip() for q in sm.group(1).split(',')]
                    ix = [i for i, q in enumerate(pl) if re.search(r'\b%s$' % re.escape(pn), q)]
                    args = _xo_split_args(dm[-1]['args'])
                    if ix and len(args) == len(pl):
                        src, before, how = dm[-1], True, 'passthrough-arg'
                        expr = '(uint)(%s)' % args[ix[0]]
                # (2) else the last call before the read that WRITES R, if it returns R
                if src is None and rd is not None:
                    allc = dom(_xo_calls(body, set(_key_of)))
                    for c in reversed(allc):
                        k2 = _key_of[c['fn']]
                        p2 = _xo_parts(k2)
                        if R in [_xo_reg(q) for q in p2]:
                            src, name, parts, how = c, c['fn'], p2, 'passthrough-call'
                            break
                        if _xo_writes(k2, R, sub) is not False:
                            how = 'passthrough-unexpressible(%s)' % c['fn']
                            break
                    else:
                        # (3) nothing before the read writes R: the caller's own R
                        # (the statement holding the read may itself assign p_R:
                        # 0x4330's `p_A0 = extraout_A0_00` -- cut at its line)
                        _pre = re.sub(r'\bp_%s = \([\w ]+\**\)A68\w*\(p_%s\);' % (R, R), '',
                                      body[:body.rfind('\n', 0, rd)])
                        # ONLY where this unit itself calls the caller with arguments: a
                        # function the driver enters with none (irq4_handler from
                        # port_l2.c) holds no 68K value in its register parameters
                        _called = re.search(r'[=(,;{}]\s*' + re.escape(caller) + r'\s*\(\s*[^)\s]', text)
                        if _called and re.search(r'\bp_%s\b' % R, cparams) and \
                           not re.search(r'\bp_%s(?:_\d+)?\s*=[^=]' % R, _pre):
                            dm2 = dom(_xo_calls(body, {cname}))
                            if dm2:
                                src, how, expr = dm2[-1], 'passthrough-param', '(uint)p_%s' % R
            elif cls in ('declared', 'published'):
                cs = dom(_xo_calls(body, {cname}))
                if cs:
                    src = cs[-1]
            if src is not None and how.startswith('passthrough-') and rd is not None:
                cl = _clobber(body[0 if how == 'passthrough-param' else src['semi']:rd], R, sub)
                if cl:
                    how = 'passthrough-clobbered(%s)' % cl
                    src = None
            if src is None and cls in ('declared', 'published'):
                _XO_LOG.append((caller, ps, how + '-nocall', None, None))
                continue
            if src is None:
                # the value is not expressible from a return or an argument: keep the
                # 868ae772 feed (see _xo_old) rather than trade one guess for another
                o_ = _xo_old(body, r, sites, key, R, ps)
                if o_ is None:
                    _XO_LOG.append((caller, ps, how + '-old-unfed', None, None))
                    continue
                src, name, parts = o_
                how += '-old'
            elif ps in fed:
                _XO_LOG.append((caller, ps, how + '-dup', src['fn'], None))
                continue
            if expr is None:
                if cls == 'published':
                    # the enemy-AI pass's own spelling, which it recognises as done
                    expr = '(ushort)%s;   /* D0 after the call: cport_prep.py */' % _XO_PUB[(key, R)]
                elif not parts and not how.endswith('-old'):
                    _XO_LOG.append((caller, ps, how + '-void', src['fn'], None))
                    continue
                else:
                    half = None
                    if len(parts) >= 2:
                        mine = [i for i, q in enumerate(parts) if _xo_reg(q) == R]
                        half = mine[0] if mine else len(parts) - 1
                    var = src['var']
                    if var is None:
                        # a BARE producer call: give its result a name
                        var = '_xo%d' % ntmp[0]; ntmp[0] += 1
                        decls.append('%s %s;' % ('ulonglong' if len(parts) >= 2 else 'uint', var))
                        body = body[:src['fpos']] + var + ' = ' + body[src['fpos']:]
                        sh = len(var) + 3
                        src = dict(src, semi=src['semi'] + sh, var=var)
                    if len(parts) >= 2:
                        expr = ('(uint)((ulonglong)%s >> 0x20)' % var) if half == 0 else '(uint)%s' % var
                    else:
                        expr = '(uint)%s' % var
            if sub == 'u' and not how.endswith('-old'):
                expr = '(%s >> 0x10)' % expr          # the UPPER word of the register
            ind = src['ind']
            # ASSIGN THE LOCAL, NOT THE GLOBAL. cport_prep hoists every
            # pseudo-register into a local at function ENTRY
            # (`uint extraout_A1 = (uint)_r_extraout_A1;`), so a later write to the
            # global is invisible to the body -- measured 2026-10-01: the assignment
            # landed on line 29 and the read on line 49 still used the entry copy.
            # Write the local where one exists, and the global otherwise.
            lhs = ps if re.search(r'^\s*[\w ]+\**\s*' + re.escape(ps) + r'\s*=', body, re.M) else '_r_' + ps
            _XO_LOG.append((caller, ps, how, src['fn'], expr))
            if not how.endswith('-old'):
                fed.add(ps)
            if before:
                ins = '%s%s = %s%s\n' % (ind, lhs, expr, '' if expr.endswith('*/') else ';')
                body = body[:src['start']] + ins + body[src['start']:]
            else:
                ins = '\n%s%s = %s%s' % (ind, lhs, expr, '' if expr.endswith('*/') else ';')
                body = body[:src['semi'] + 1] + ins + body[src['semi'] + 1:]
            n += 1
        if decls:
            body = ''.join('\n  ' + d_ for d_ in decls) + body
        text = text[:m.end()] + body + text[end:]
    if os.environ.get('CPORT_FEEDLOG'):
        with open(os.environ['CPORT_FEEDLOG'], 'w') as f_:
            for row in _XO_LOG:
                f_.write('\t'.join(str(x) for x in row) + '\n')
    return text, n

src, _nfeed = _feed_extraout(src)

# ---- THE REGISTER CHAIN: WRITE A MODIFIED REGISTER PARAMETER BACK ------------
#
# D3 and D6 are THREADED handler to handler. Proven from the frame-matched oracle
# (FULL-PORT.md 30): each handler's EXIT D3/D6 is the next handler's ENTRY D3/D6,
# three transitions for three, and the 68K's object_dispatcher computes NEITHER --
# it only preserves them across the loop.
#
# Section 19 declared these as PARAMETERS, which recovered the argument setup and
# took the frame from 176 wrong to 49. But a by-value C parameter cannot carry a
# modification back out, so that same change severed the write-back the chain needs.
# Declaring return storage cannot express it either: 44 functions change BOTH D3 and
# D6 in every captured call and Ghidra's return holds at most two registers.
#
# So the port gets what the 68000 had: a REGISTER FILE in globals. A function with a
# `p_D3`/`p_D6` parameter writes it back before returning --
#
#   ONLY IF ITS BODY ASSIGNS TO IT. That condition is the whole correctness of this.
#   If F takes p_D3, never touches it, and calls G which DOES modify D3, then F
#   writing its own stale p_D3 back on return would CLOBBER G's update. A function
#   that never assigns the parameter has nothing to contribute and stays silent.
# DATA REGISTERS ONLY. A1 was tried and backed out: the port keeps A-registers as
# TRANSLATED SH-2 POINTERS while the 68000 kept 24-bit addresses that are sometimes
# used as plain integers. Measured 2026-10-01: with A1 in the chain the file read
# 0x0601C142 -- a real SDRAM pointer, written back correctly by whichever function
# uses A1 as a pointer -- where the zoom call site wants the arcade's 0x6F2C and
# uses it as a NUMBER (uVar10 >> 1 is a screen coordinate). One global cannot serve
# both domains, and nothing in the file says which domain a value is in.
# D7 joined 2026-10-01: sprite-record +3 is `uVar5`, which reads p_D7, and
# port_zoom_scale computes D7 (the width term) from the 68K. Data registers only --
# see the A1 note above.
_CHAINREG = {'D3': 3, 'D6': 6, 'D7': 7}

def _chain_writeback(text):
    out, n = [], 0
    i = 0
    FD = re.compile(r'^(?:[A-Za-z_]\w*[\w \*]*?)(\w+)\(([^;{]*)\)\s*\n\{', re.M)
    while True:
        m = FD.search(text, i)
        if not m:
            out.append(text[i:]); break
        end = text.find('\n}', m.end())
        if end < 0:
            out.append(text[i:]); break
        body = text[m.end():end]
        mine = [(r, ix) for r, ix in _CHAINREG.items()
                if ('p_' + r) in m.group(2)
                # the body must ASSIGN it, not merely read it
                and re.search(r'\bp_' + r + r'\s*=[^=]', body)]
        if mine:
            # A COUNTER, because "the chain reads 0" has two causes needing
            # different fixes: the write-backs never EXECUTE (the assigning
            # functions are not reached) or they execute and write ZERO (the value
            # is derived from other zero inputs). Measured 2026-10-01: 3,647
            # write-back references in the unit and D3/D6 both 0 after a full frame.
            wb = ''.join('\n  _port_reg[%d] = (uint)p_%s;' % (ix, r)   # no _port_reg_n++: 7029 sites cost ~40 KB of .portcode (2026-10-04)
                         for r, ix in sorted(mine))
            body, k = re.subn(r'\n(\s*)return\b', lambda mm: wb + '\n' + mm.group(1) + 'return', body)
            n += k
        out.append(text[i:m.end()]); out.append(body)
        i = end
    return ''.join(out), n

src, _nchain = _chain_writeback(src)

# ---- AND THE RELOAD: A BY-VALUE COPY IS STALE AFTER A CALL -------------------
#
# The write-back half is proven: port_zoom_scale (hand-written from the 68K,
# FULL-PORT.md 30d) runs 4 times a frame and its last D3 is 0x12 -- the arcade's
# value to the byte -- and it lands in _port_reg[3]. But the CALLER reads its own
# `p_D3`, a by-value parameter copied at entry, so the new value is invisible. Write
# back without reload is half a register.
#
# The 68000 had no copy: after `jsr`, D3 IS whatever the callee left. So after every
# call, a function's register parameters are refreshed from the file.
#
# ONLY AFTER A CALL TO A FUNCTION IN THIS UNIT. A reload after an arbitrary
# statement would be noise, and after a call to a RUNTIME helper (LD32U, P68,
# port_call68_at) it would be wrong -- those are the port's own machinery and do not
# touch the 68K register file.
def _chain_reload(text, unit_names):
    out, n = [], 0
    i = 0
    FD = re.compile(r'^(?:[A-Za-z_]\w*[\w \*]*?)(\w+)\(([^;{]*)\)\s*\n\{', re.M)
    callpat = re.compile(r'\b(\w+)\s*\(')
    while True:
        m = FD.search(text, i)
        if not m:
            out.append(text[i:]); break
        end = text.find('\n}', m.end())
        if end < 0:
            out.append(text[i:]); break
        body = text[m.end():end]
        mine = [(r, ix) for r, ix in _CHAINREG.items() if ('p_' + r) in m.group(2)]
        if m.group(1) not in _RELOAD_FNS:
            mine = []
        if mine:
            lines = body.split('\n')
            res = []
            for ln in lines:
                res.append(ln)
                st = ln.strip()
                if not st.endswith(';'):
                    continue
                if st.startswith('for') or st.startswith('while') or st.startswith('return'):
                    continue
                hits = [c for c in callpat.findall(ln) if c in unit_names]
                if not hits:
                    continue
                ind = ln[:len(ln) - len(ln.lstrip())]
                for r, ix in sorted(mine):
                    res.append('%sp_%s = (undefined4)_port_reg[%d];' % (ind, r, ix))
                    n += 1
            body = '\n'.join(res)
        out.append(text[i:m.end()]); out.append(body)
        i = end
    return ''.join(out), n

# SCOPED, because the blanket version OVERFLOWED THE CART. Inserting a reload after
# every call in all 161 functions with a p_D3 parameter pushed .portcode past
# 0x02400000 ("address 0x2403560 ... is not within region rom"), and .portcode's
# budget is fixed at ~464 KB by .portrom and .portdata below it.
#
# So prove the mechanism where it can be CHECKED -- sprite_build_depth_banded, whose
# sprite-record fields the gate reads cell by cell -- and widen only if it pays.
_RELOAD_FNS = set(x.strip() for x in
                  os.environ.get('CPORT_RELOAD', 'sprite_build_depth_banded').split(',')
                  if x.strip())
_UNITNAMES = set(re.findall(r'^(?:[A-Za-z_]\w*[\w \*]*?)(\w+)\([^;{]*\)\s*$', src, re.M))
src, _nreload = _chain_reload(src, _UNITNAMES)
print('register-chain reloads inserted after calls: %d' % _nreload)
print('register-chain write-backs inserted (D3/D6, only where the body assigns): %d' % _nchain)
print('extraout_ pseudo-registers fed from the producing call: %d' % _nfeed)
print('calls to a LITERAL 68K address routed through port_call68: %d' % _nlit)

# A BARE 68K PROGRAM ADDRESS USED AS A POINTER BASE READS GARBAGE ON THE SH-2.
#
# Ghidra emits the 68000's absolute long addressing verbatim:
#     *(ushort *)((uint)*(ushort *)(unaff_A6 + 6) * 6 + 0x255e0)
# 0x255E0 is an address in the 256 KB program, and a table lives there. On the
# SH-2 that address is nothing, so the load returns whatever the 32X maps at
# 0x255E0. Measured 2026-09-28: 34 such dereferences over 30 distinct bases in the
# 351-function frame unit.
#
# The existing W68 macro does NOT cover these. It handles the 68000's absolute
# SHORT form, a 16-bit displacement sign-extended -- these bases are wider than
# 16 bits and never pass through it.
#
# THE BASE IS THE LITERAL AT THE TOP LEVEL of the address expression. A literal
# inside a NESTED dereference is an index into something else, so nested
# sub-expressions are removed before looking. Getting that wrong in the detector
# missed 0x255E0, whose expression mentions unaff_A6 inside a nested load.
_DEREF = re.compile(r'\*\(\s*(?:undefined\d?|u?short|u?int|u?char|byte|code|long)'
                    r'\s*\*+\s*\)\s*\(')

def _paren_span(t, i):
    d = 0
    for j in range(i, len(t)):
        if t[j] == '(':
            d += 1
        elif t[j] == ')':
            d -= 1
            if d == 0:
                return j
    return -1

def _top_level(e):
    """e without its nested parenthesised sub-expressions."""
    out, i, d = [], 0, 0
    for ch in e:
        if ch == '(':
            d += 1
            if d <= 1:
                out.append(ch)
            continue
        if ch == ')':
            if d <= 1:
                out.append(ch)
            d -= 1
            continue
        if d <= 1:
            out.append(ch)
    return ''.join(out)

def _rom_bases(text):
    """Rewrite every dereference whose TOP-LEVEL base is a 68K program address."""
    n = 0
    i = 0
    out = []
    while True:
        m = _DEREF.search(text, i)
        if not m:
            out.append(text[i:])
            break
        open_i = m.end() - 1
        close_i = _paren_span(text, open_i)
        if close_i < 0:
            out.append(text[i:m.end()])
            i = m.end()
            continue
        expr = text[open_i:close_i + 1]
        tl = _top_level(expr)
        cand = [int(x, 16) for x in re.findall(r'0x([0-9a-f]{3,6})\b', tl)]
        base = [v for v in cand if 0x400 <= v < 0x40000]
        if (base and 'ROM_BASE' not in expr and 'WRAM_BASE' not in expr
                and 'W68' not in expr
                and not re.search(r'\b(unaff_A\d|in_A\d|extraout_A\d)\b', tl)):
            b = max(base)
            # replace that one literal with the translated base, in the top level
            lit = '0x%x' % b
            if lit in expr:
                newexpr = expr.replace(lit, '((int)ROM_BASE + %s)' % lit, 1)
                out.append(text[i:open_i])
                out.append(newexpr)
                i = close_i + 1
                n += 1
                continue
        out.append(text[i:close_i + 1])
        i = close_i + 1
    return ''.join(out), n

src, _nrom = _rom_bases(src)

# ONCE A LITERAL IS A ROM BASE, IT IS ONE EVERYWHERE IN THAT STATEMENT. The
# rewrite above only fixes the literal that BASES a dereference; the same table
# address is often also added to the loaded value to form a pointer that the
# CALLER dereferences:
#     return *(ushort *)(idx * 6 + ((int)ROM_BASE + 0x255e0)) + 0x255e0;
#                                                              ^^^^^^^^ still 68K
# Deciding that in general needs dataflow, but within one statement it does not:
# the compiler is indexing one table, so every occurrence of that literal is the
# same base. Scoped to the statement, so an unrelated constant of the same value
# elsewhere is untouched.
def _rom_bases_rest(text):
    n = 0
    out = []
    for stmt in text.split(';'):
        for lit in set(re.findall(r'\(\(int\)ROM_BASE \+ (0x[0-9a-f]{3,6})\)', stmt)):
            parts = stmt.split('((int)ROM_BASE + %s)' % lit)
            for k in range(len(parts)):
                if re.search(r'%s\b' % re.escape(lit), parts[k]):
                    parts[k] = re.sub(r'%s\b' % re.escape(lit),
                                      '((int)ROM_BASE + %s)' % lit, parts[k])
                    n += 1
            stmt = ('((int)ROM_BASE + %s)' % lit).join(parts)
        out.append(stmt)
    return ';'.join(out), n

src, _nrom2 = _rom_bases_rest(src)
print('bare 68K program addresses given ROM_BASE: %d as a deref base, %d more '
      'in the same statement' % (_nrom, _nrom2))

# THE 68000 ALLOWS A 32-BIT ACCESS AT A 2-BYTE-ALIGNED ADDRESS. THE SH-2 DOES NOT.
#
# An SH7604 32-bit load or store requires 4-byte alignment and raises an ADDRESS
# ERROR otherwise. The 68000 requires only 2, and this program uses that freely:
# measured 2026-09-28, 147 of the 302 absolute symbols the port places sit at an
# address that is 2 mod 4, and the state-machine jump table the object handlers
# dispatch through is one of them (DAT_0000f556 -> 0x0234F556).
#
# The symptom is not a crash that names itself. It is a silent wrong value and an
# exception: 208 of one frame's indirect calls read 0x00000 out of a table whose
# real first entry is 0x0000EFD2, and an earlier build spent 40% of all master
# cycles at PC 0x00000002 inside main_irq -- address-error vectors.
#
# Every 32-bit access whose address is computed therefore goes through a packed
# accessor, which makes gcc emit byte-wise code. Narrower accesses are unaffected:
# 16-bit needs 2-byte alignment, which the 68000 also required.
_LD32 = {'uint': 'LD32U', 'int': 'LD32S', 'undefined4': 'LD32U'}
# EXACTLY ONE STAR. `\*+` also matched `*(int **)(X)` -- a pointer-to-pointer --
# and rewriting the cast left the OUTER dereference stranded as `*LD32S(X)`, which
# is a type error on 7 sites. The `**(T **)(X)` form is a double 32-bit load and
# BOTH loads can be misaligned, so it is handled first and separately.
_DEREF32 = re.compile(r'\*\(\s*(uint|int|undefined4)\s*\*\s*\)\s*\(')
_DEREF32X2 = re.compile(r'\*\*\(\s*(uint|int|undefined4)\s*\*\s*\*\s*\)\s*\(')

def _unaligned32x2(text):
    n = 0
    out, i = [], 0
    while True:
        m = _DEREF32X2.search(text, i)
        if not m:
            out.append(text[i:])
            break
        ty = m.group(1)
        open_i = m.end() - 1
        close_i = _paren_span(text, open_i)
        if close_i < 0:
            out.append(text[i:m.end()])
            i = m.end()
            continue
        expr = text[open_i:close_i + 1]
        out.append(text[i:m.start()])
        out.append('%s(LD32U%s)' % (_LD32[ty], expr))
        i = close_i + 1
        n += 1
    return ''.join(out), n

def _unaligned32(text):
    nr = nw = 0
    out, i = [], 0
    while True:
        m = _DEREF32.search(text, i)
        if not m:
            out.append(text[i:])
            break
        ty = m.group(1)
        open_i = m.end() - 1
        close_i = _paren_span(text, open_i)
        if close_i < 0:
            out.append(text[i:m.end()])
            i = m.end()
            continue
        expr = text[open_i:close_i + 1]
        rest = text[close_i + 1:]
        # a WRITE is `*(T *)(X) = V;` -- and `==` is a comparison, not a write
        wm = re.match(r'\s*=(?!=)\s*', rest)
        out.append(text[i:m.start()])
        if wm:
            # THE VALUE ENDS AT THE FIRST DELIMITER AT DEPTH 0, not at the next
            # `;`. An assignment is not always a statement: Ghidra emits comma
            # operators inside conditions --
            #   if ((*(undefined4 *)(A6 + 0x24) = 0xd066, *(short *)(A6+0x80) != 0))
            # -- and scanning to the next `;` swallowed the rest of the condition
            # and the opening brace of the block.
            end, depth = -1, 0
            for k in range(wm.end(), len(rest)):
                ch = rest[k]
                if ch in '([':
                    depth += 1
                elif ch in ')]':
                    if depth == 0:
                        end = k
                        break
                    depth -= 1
                elif depth == 0 and ch in ';,':
                    end = k
                    break
            if end < 0:
                out.append(text[m.start():close_i + 1])
                i = close_i + 1
                continue
            val = rest[wm.end():end]
            # PARENTHESISE THE VALUE. A comma inside it -- a comma operator, or
            # a call's argument list -- makes the preprocessor see three macro
            # arguments and the build dies with "ST32 passed 3 arguments".
            out.append('ST32(%s, (%s))' % (expr, val))
            i = close_i + 1 + end
            nw += 1
        else:
            out.append('%s(%s)' % (_LD32[ty], expr))
            i = close_i + 1
            nr += 1
    return ''.join(out), nr, nw

# A 68K POINTER LOADED OUT OF MEMORY MUST BE TRANSLATED AT USE TIME.
#
# `*(ushort **)(X)` loads a 32-bit value and uses it as a pointer. In this program
# that value is a 68K address stored in DATA -- not a literal in the code, so no
# static rewrite of the source can catch it.
#
# Measured 2026-09-28: floor_collide walks a table whose base comes from
# `*(ushort **)((int)&PTR_DAT_0000dec4 + ...)`, and that table holds 0x0000DED8,
# 0x0000DF0E, 0x0000DF14... -- ROM addresses. Dereferenced as SH-2 addresses they
# are garbage, the loop's terminator never compares true, and floor_collide took
# 87.6% of all master cycles. It hung the whole rom, and the rom read zero for
# every counter because the driver never reached its compare.
#
# P68 branches on the region exactly as W68 does, but on a RUNTIME value:
# work RAM if the address is 0xFFC000 and up, program ROM otherwise.
_DEREFPP = re.compile(r'\*\(\s*(u?short|u?int|u?char|byte|undefined[0-9]?)\s*\*\s*\*\s*\)\s*\(')

def _ptr_from_mem(text):
    n = 0
    out, i = [], 0
    while True:
        m = _DEREFPP.search(text, i)
        if not m:
            out.append(text[i:])
            break
        ty = m.group(1)
        open_i = m.end() - 1
        close_i = _paren_span(text, open_i)
        if close_i < 0:
            out.append(text[i:m.end()])
            i = m.end()
            continue
        expr = text[open_i:close_i + 1]
        rest = text[close_i + 1:]
        # a STORE through such a pointer is a different problem and is left alone
        if re.match(r'\s*=(?!=)', rest):
            out.append(text[i:close_i + 1])
            i = close_i + 1
            continue
        out.append(text[i:m.start()])
        out.append('(%s *)P68(LD32U%s)' % (ty, expr))
        i = close_i + 1
        n += 1
    return ''.join(out), n

src, _nptr = _ptr_from_mem(src)
print('68K pointers loaded from memory routed through P68: %d' % _nptr)

src, _n32x2 = _unaligned32x2(src)

src, _nr32, _nw32 = _unaligned32(src)
print('double 32-bit dereferences (**(T **)(X)) rewritten: %d' % _n32x2)
print('32-bit accesses routed through the unaligned accessors: %d loads, %d stores'
      % (_nr32, _nw32))

# A NESTED 32-BIT LOAD INSIDE AN ADDRESS EXPRESSION IS ITSELF AN ADDRESS.
#
# `LD32U(LD32S(X) + Y)` is a double indirection: load a 32-bit value, add an
# offset, load again. The 68000 stored a pointer in that first cell, so the inner
# result is a 68K address and needs P68 exactly like `*(T **)(X)` does. It is not
# caught by that rule because the C never spells it as a pointer type.
#
# Measured 2026-09-29: this is the whole of the remaining indirect-call failure --
# site 1 of six, 80 misses a frame, every one reading target 0x00000 through
# `&PTR_FUN_00006da0`'s table of 68K pointers.
#
# Runs AFTER _unaligned32, so the inner loads are already LD32S/LD32U calls.
_LDCALL = re.compile(r'\b(LD32U|LD32S|ST32)\s*\(')

def _nested_ptr(text):
    n = 0
    i = 0
    while True:
        m = _LDCALL.search(text, i)
        if not m:
            break
        open_i = m.end() - 1
        close_i = _paren_span(text, open_i)
        if close_i < 0:
            i = m.end()
            continue
        inner = _LDCALL.search(text, m.end())
        # the nested call has to be INSIDE this one's argument list
        if inner and inner.start() < close_i:
            i2 = inner.end() - 1
            c2 = _paren_span(text, i2)
            if c2 > 0 and c2 < close_i and 'P68' not in text[inner.start():c2]:
                nested = text[inner.start():c2 + 1]
                text = (text[:inner.start()] + '(char *)P68(' + nested + ')'
                        + text[c2 + 1:])
                n += 1
                i = m.end()
                continue
        i = m.end()
    return text, n

src, _nnest = _nested_ptr(src)
print('nested 32-bit loads treated as addresses (P68): %d' % _nnest)

# ---- STORING A HANDLER POINTER INTO 68K MEMORY STORES THE 68K ADDRESS -----------
#
# The decompiled program changes an object's behaviour by writing a code pointer into
# the object record:
#
#     *(code **)(unaff_A6 + 2)    = FUN_000059d2;    the object's HANDLER
#     *(code **)(unaff_A6 + 0x24) = FUN_000075fa;    its animation SCRIPT
#
# On the 68000 the value stored is a 68K address. In the generated unit `FUN_000059d2`
# is an SH-2 symbol, so the port stored an SH-2 address into 68K work RAM -- and
# EVERY consumer reads it back as a 68K address:
#   * port_object_dispatch binary-searches port_handlers[] by the 68K address at
#     slot+2, so a slot the game has re-pointed stops resolving;
#   * port_call68_at looks up the same way;
#   * FUN_0000669C `moveal %fp@(36),%a0` walks the animation script as DATA from
#     A6+0x24, which has to be a 68K ROM address.
#
# MEASURED 2026-10-03. The gate's free-run kills slot 16 at frame 38, the frame the
# arcade's own capture shows that enemy starting to MOVE. At that frame the port's
# slot 16 holds +0x24..0x27 = 0x0238E968 where the arcade holds 0x0000F5D0 --
# 0x0238xxxx is .portcode, an SH-2 address -- and its X/Y read 0xD2F5/0x5F8D against
# the arcade's 0x1267/0x10D4. PORTWATCH named the innermost writer in one run
# (FUN_0000669C, the animation stepper, 0x12 -> 0xAB at 0xFFC80C).
#
# IT IS BROKEN TWICE OVER, which is why ST32 is the right destination and not a cast.
# An object record is 0x80 bytes, so slot+2 is 2 mod 4 and `*(code **)(A6 + 2) = ...`
# is a 32-bit store at a misaligned address -- a CPU address error on an SH7604. The
# `_unaligned32` pass does not cover it: its cast list is uint/int/undefined4 and
# this one says `code *`. ST32 fixes the width and the value in one rewrite.
#
# 1,154 of the 1,159 sites are `= FUN_<8 hex>`, where the symbol's own name IS the
# address, so the rewrite is exact rather than inferred. The other 5 name a function
# the map renamed, and docs/audit/func_ranges.txt gives the address for those.
_NAME2A68 = {}
for _l in open(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                            'docs', 'audit', 'func_ranges.txt')):
    if _l.startswith('#'):
        continue
    _f = _l.split()
    if len(_f) >= 4 and _f[2].startswith('0x'):
        _NAME2A68.setdefault(_f[3], int(_f[2], 16))

_CODEST = re.compile(r'\*\(\s*code\s*\*\s*\*\s*\)\s*\(')
def _code_ptr_stores(text):
    n = miss = 0
    out, i = [], 0
    while True:
        m = _CODEST.search(text, i)
        if not m:
            out.append(text[i:]); break
        open_i = m.end() - 1
        close_i = _paren_span(text, open_i)
        if close_i < 0:
            out.append(text[i:m.end()]); i = m.end(); continue
        expr = text[open_i:close_i + 1]
        rest = text[close_i + 1:]
        wm = re.match(r'\s*=(?!=)\s*([A-Za-z_]\w*)\s*;', rest)
        if not wm:
            out.append(text[i:close_i + 1]); i = close_i + 1; continue
        nm = wm.group(1)
        mh = re.match(r'^FUN_([0-9a-f]{4,8})$', nm)
        a68 = int(mh.group(1), 16) if mh else _NAME2A68.get(nm)
        if a68 is None:
            miss += 1
            out.append(text[i:close_i + 1]); i = close_i + 1; continue
        out.append(text[i:m.start()])
        out.append('ST32(%s, 0x%xu)' % (expr, a68))
        i = close_i + 1 + wm.end() - 1          # keep the `;`
        n += 1
    return ''.join(out), n, miss

src, _ncst, _ncstmiss = _code_ptr_stores(src)
print('handler pointers stored into 68K memory as the 68K ADDRESS: %d (%d unresolved '
      'name(s) left alone)' % (_ncst, _ncstmiss))



# ---- THE SAME 68K POINTER, BUT THROUGH A NAMED LOCAL ACROSS TWO STATEMENTS -------
#
# _nested_ptr catches `LD32U(LD32S(X) + Y)` -- the load and the use in ONE
# expression. The decompiler just as often splits them:
#
#     iVar1 = LD32S((&DAT_0001d32a + (mode << 2)));    a 68K ROM address, in a local
#     ...
#     bVar5 = uVar2 < *(ushort *)(iVar1 + sVar3);      dereferenced UNTRANSLATED
#
# and no single-expression pattern can see that. The argument for rewriting it is
# the one _ptr_from_mem already makes and which has not been wrong yet: a 32-bit
# value LOADED OUT OF 68K MEMORY and then used as an address can only be a 68K
# address, because 68K code could never have stored an SH-2 pointer there.
#
# MEASURED 2026-10-03, and this is the site above, verbatim. FUN_0000D80E is the
# level's SPAWN-TABLE WALKER: it compares the camera against the next spawn
# trigger and stops when the camera has not reached it. `iVar1` is the round's
# trigger table at 68K 0x0001D4D6; dereferenced as an SH-2 address that is past the
# 32X's 16 KB boot ROM, so the comparison is against garbage, the walker does not
# stop, and FUN_0000D868 (the slot allocator) spawns THIRTEEN objects the arcade
# never spawns -- slots 41..53, one per frame from frame 2, each wrong in exactly
# the two fields that allocator writes (+0x00 = 0x80 and +0x2F = 0x00).  Read
# straight out of the captures: the arcade has those slots at status 0x00 and
# handler 0x000000 at frame 9000 AND frame 9080.
#
# ONLY A TOP-LEVEL ADDITIVE TERM COUNTS.  `*(T *)(V + X)`, `*(T *)(X + V)` and
# `*(T *)(V)` are address arithmetic on V; `*(T *)(V * 4 + &DAT_x)` is V used as an
# INDEX into an SH-2 object and must not be touched.  Restricting to top-level `+`
# terms is what separates the two, and it is checked rather than assumed.
_ASSIGN_LD32 = re.compile(r'\b([A-Za-z_]\w*)\s*=\s*LD32[SU]\s*\(')
_DEREF_ANY = re.compile(r'\*\(\s*(?:u?short|u?int|u?char|byte|code|undefined[0-9]?)'
                        r'\s*\*\s*\)\s*\(|\bLD32[SU]\s*\(')

def _top_terms(expr):
    """the top-level `+`/`-` separated terms of a parenthesised expression body"""
    terms, depth, last = [], 0, 0
    for k, ch in enumerate(expr):
        if ch in '([':
            depth += 1
        elif ch in ')]':
            depth -= 1
        elif depth == 0 and ch in '+-':
            terms.append(expr[last:k]); last = k + 1
    terms.append(expr[last:])
    return [t.strip() for t in terms]

def _ld32_local_ptr(block):
    """P68 a deref whose BASE is a local loaded 32-bit out of 68K memory"""
    n = 0
    loaded = set(_ASSIGN_LD32.findall(block))
    if not loaded:
        return block, 0
    out, i = [], 0
    while True:
        m = _DEREF_ANY.search(block, i)
        if not m:
            out.append(block[i:])
            break
        open_i = m.end() - 1
        close_i = _paren_span(block, open_i)
        if close_i < 0:
            out.append(block[i:m.end()]); i = m.end(); continue
        body = block[open_i + 1:close_i]
        hit = any(t in loaded for t in _top_terms(body))
        # the assignment that DEFINES the local is not a use of it
        if hit and 'P68' not in body:
            out.append(block[i:m.start()])
            out.append(block[m.start():open_i])
            out.append('((char *)P68(%s))' % body)
            i = close_i + 1
            n += 1
        else:
            out.append(block[i:close_i + 1])
            i = close_i + 1
    return ''.join(out), n

_nlocp = 0
_blocks = re.split(r'(?=/\* ---- 0x)', src)
for _bi in range(len(_blocks)):
    _blocks[_bi], _k = _ld32_local_ptr(_blocks[_bi])
    _nlocp += _k
src = ''.join(_blocks)
print('68K pointers held in a LOCAL loaded from memory routed through P68: %d'
      % _nlocp)



# A `goto LAB_<hex>` WHOSE LABEL IS NOW IN ANOTHER FUNCTION IS A TAIL CALL.
#
# Splitting a function to give an indirect-call target its own entry point breaks
# any branch that crossed the split: Ghidra emits `goto LAB_00004dba;` in the
# remainder while the label now sits inside FUN_00004dba. Measured 2026-09-29:
# creating entries for the 164 jump-table targets split 92 existing functions and
# produced 33 "label used but not defined" errors.
#
# The 68000 branch was a `bra` to that address, and that address is now a function
# entry, so the faithful translation is a TAIL CALL -- and the label name carries
# the address, which is what makes the mapping exact rather than a guess.
_LABGOTO = re.compile(r'\bgoto\s+(LAB_([0-9a-f]{8}))\s*;')
_byaddr_name = {}
for _b in re.split(r'(?=/\* ---- 0x)', src):
    _m = re.match(r'/\* ---- 0x([0-9A-F]+)\s+(\S+)', _b)
    if _m:
        _byaddr_name[int(_m.group(1), 16)] = _m.group(2)

def _tailcall_gotos(text):
    n = 0
    out = []
    for blk in re.split(r'(?=/\* ---- 0x)', text):
        defined = set(re.findall(r'^(LAB_[0-9a-f]{8}):', blk, re.M))
        def _fix(m):
            global_n = None
            lab, hexa = m.group(1), m.group(2)
            if lab in defined:
                return m.group(0)
            tgt = _byaddr_name.get(int(hexa, 16))
            if not tgt:
                return m.group(0)
            return 'return %s();' % tgt
        new, k = _LABGOTO.subn(_fix, blk)
        n += sum(1 for _ in re.finditer(r'\breturn [A-Za-z_]\w*\(\);', new)) if False else 0
        if new != blk:
            n += k
        out.append(new)
    return ''.join(out), n

src, _ntail = _tailcall_gotos(src)
print('cross-function gotos turned into tail calls: %d' % _ntail)

# A POINTER RETURNED BY A FUNCTION IS A 68K ADDRESS TOO.
#
# `pbVar10 = (byte *)FUN_0000673e();` -- that routine's return lives in A0 and is
# an animation-script pointer in the 68K program. Dereferenced as an SH-2 address
# it reads garbage, and FUN_0000669c then adds the garbage to the object's X:
# measured 2026-09-29, slot 16's +0x0c came out 0x1269 where the arcade had
# 0x1263, and the arcade's own progress markers proved the port's CONTROL FLOW
# through that handler was already correct -- only the value was wrong.
#
# Which functions return a pointer is not a guess: docs/audit/reg_convention.json
# records each one's return STORAGE, and an A-register return is an address.
_conv_path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                          'docs', 'audit', 'reg_convention.json')
_aret = set()
if os.path.exists(_conv_path):
    for _r in json.load(open(_conv_path)):
        if _r.get('ret') and re.search(r'\bA\d', _r['ret']):
            _aret.add(_r['name'])
_npret = 0
if _aret:
    _pat = re.compile(r'\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*(\*+)\s*\)\s*(%s)\(\)'
                      % '|'.join(re.escape(x) for x in sorted(_aret)))
    def _wrap(m):
        global _npret
        _npret += 1
        return '(%s %s)P68(%s())' % (m.group(1), m.group(2), m.group(3))
    src = _pat.sub(_wrap, src)
print('pointer-returning calls wrapped in P68: %d (of %d A-returning functions)'
      % (_npret, len(_aret)))

# THE RECORD'S SCRIPT POINTER (+0x24) IS A 68K ADDRESS WHEN IT IS INDEXED.
#
# A handler that walks its own animation table reads `+0x24`, adds an index and
# dereferences: `(undefined2 *)((int)(short)(state << 2) + LD32S((unaff_A6 + 0x12)))`
# (Ghidra's short-indexed form of +0x24, FUN_00006cb8 at 0x6CC2..0x6CCE). The value
# is a program address (0x7B16: the trail's sprite table) and read as an SH-2 pointer
# it returns zeros -- measured 2026-10-04, demo A tick 0x74: the five trail objects
# had +0x4e = 0 and sprite id +0x06 = 0 where the arcade read the table. Every
# short-indexed use in the unit is such an address base (8 sites, 6 functions, read
# by hand); the byte-indexed form is mostly compared as a VALUE and is left alone
# except the one cast-to-pointer site (FUN_00009270).
_sp = [0]
def _spw(m):
    _sp[0] += 1
    return '(int)P68(%s)' % m.group(0)
src = re.sub(r'LD32S\(\(unaff_A6 \+ 0x12\)\)', _spw, src)
_pass_report('short-indexed script pointer (+0x24 as undefined2*) through P68', _sp[0], 8)
_sp2 = re.subn(r'\(short \*\)\(\(int\)\(short\)\(\(ushort\)unaff_A6\[0x21\] \* 6\) \+ LD32S\(\(unaff_A6 \+ 0x24\)\)\)',
               r'(short *)((int)(short)((ushort)unaff_A6[0x21] * 6) + (int)P68(LD32S((unaff_A6 + 0x24))))', src)
src = _sp2[0]
_pass_report('FUN_00009270: script pointer cast to short* through P68', _sp2[1], 1)

# THE KILLER'S HUD LINK IS READ THROUGH THE KILLER'S 16-BIT LINK. `movea.w ($4c,a6),a0 ;
# movea.w ($3c,a0),a5 ; jsr 0x3706` awards points to the record that last hit this one:
# +0x4c is a 16-bit record address, +0x3c of THAT record the HUD struct. Ghidra wrote
# `W68(*(short *)(sVarN + 0x3c))` -- the outer link mapped, the inner one dereferenced
# as an SH-2 address 0x0000Cxxx -- so the award went into W68(garbage)+4 (measured
# 2026-10-04, demo A tick 0x118: 0xFFE006 = 1 on the arcade, 0 on the port, slot 21's
# facing byte flipped). Six sites, one form.
_kl = re.subn(r'W68\(\*\(short \*\)\((sVar\d+) \+ 0x3c\)\)',
              r'W68(*(short *)((char *)W68(\1) + 0x3c))', src)
src = _kl[0]
_pass_report("the killer's HUD link read through the mapped killer link", _kl[1], 6)

# ... AND EVERY OTHER DEREFERENCE THROUGH A 16-BIT RECORD LINK. A `short` local holding
# `*(short *)(A6 + 0x3c)` or `+ 0x4c` (the HUD struct, the killer) is a 68K address; the
# decompiled text reads and writes through it as if it were a pointer: `*(ushort *)(sVar10
# + 0x40)` (the killer's x: which way the dying pair is knocked -- demo A tick 0x117, the
# port's pair faced the other way), `*(undefined *)(sVar7 + 0x18) = 1` (a HUD flag, written
# into SH-2 0xE018), `*(sVar4 + 0x2e)` (a facing copy). W68 maps either domain.
# Include decimal 8: 0x14BC4/0x14BCA rotate sprite-entry bytes through the
# harpy's second link. Those two dereferences escaped the hex-only matcher,
# causing measured SH-2 reads/writes at 0xFFFFC908 instead of work RAM.
_rl = re.subn(r'\*\(((?:byte|char|short|ushort|undefined|undefined2|int|uint)) \*\)\((sVar\d+) \+ (0x[0-9a-f]+|8)\)',
              r'*(\1 *)((char *)W68(\2) + \3)', src)
src = _rl[0]
_pass_report('dereferences through a 16-bit record link mapped with W68', _rl[1], 21)

# A CODE LABEL'S ADDRESS STORED AS A SCRIPT POINTER IS THE 68K VALUE. `&LAB_000101fe`
# (FUN_0000f90a: a script that starts inside a function's bytes) was declared as a symbol
# the linker placed at ROM_BASE+0x101FE, so the object's +0x24 held 0x023501FE and the
# interpreter read it through P68 as garbage (demo B tick 0x6E, slot 18). Two sites.
_lab = re.subn(r'&LAB_([0-9a-f]{8})\b', lambda m: '((undefined *)0x%xu)' % int(m.group(1), 16), src)
src = _lab[0]
_pass_report('label addresses stored as script pointers are 68K values', _lab[1], 2)

# THE HELPERS THAT RUN ON A RECORD THE CALLER JUST FILLED. A spawner writes the new record
# through A5 (or A0, or A6+0x80), then `move.l a6,-(sp) ; movea.l a5,a6 ; jsr 0x3B2E ;
# movea.l (sp)+,a6`: request_palette_update (and release_palette_slot, sprite_build_and_cull)
# read +0x0b and write +0x0a of A6, so they run on the NEW record. Ghidra kept A6 for the
# parent and the port's helpers ran on it: the parent's palette was re-requested and the
# new record never got a slot (demo B tick 0x7C: the partner's +0x0a stayed 0, the
# allocator's refcounts diverged for the rest of the demo). Sites from the 68K: every
# `movel %fp,%sp@-` followed by `moveal %a5/%a0,%fp` (tools asm census, 2026-10-05); the
# record expression is the one the function's own stores use. FUN_0000a1e0, FUN_0000f28a,
# FUN_000090f4, FUN_0000d776 and FUN_00006c44 already carry theirs.
_A6SW = {
    'FUN_00005430': ('unaff_A6 + 0x80', ('request_palette_update',), 1),   # 0x5482 movea.l a0,a6
    'FUN_00005530': ('W68PTR(0xFFDD80u)', ('request_palette_update',), 1), # 0x559E: a5 = 0xFFDD80
    'FUN_000058e4': ('p_A5', ('request_palette_update',), 1),               # 0x591A
    'FUN_00005b72': ('p_A5 + 0x80', ('request_palette_update',), 1),        # 0x5BF4: after 0x5B72 lea 0x80(a5),a5
    'FUN_00005c64': ('p_A5 + 0x80', ('request_palette_update',), 4),        # 0x5CA8
    'FUN_00005e04': ('p_A5', ('request_palette_update',), 1),               # 0x5E44
    'FUN_00008246': ('p_A0', ('request_palette_update',), 1),               # 0x8288 movea.l a0,a6
    'FUN_0000a5ae': ('unaff_A6 + 0x80', ('request_palette_update',), 1),   # 0xA606
    # 2026-10-23 (advisor): the two the census missed. 0xAF8C `movea.l fp,a5 ; lea 0x80(fp),fp`,
    # request at 0xB046 on the new record; 0xB204 walks four records (`lea 0x80(fp),fp` per
    # pass, request at 0xB2AA on each; Ghidra's psVar5 advances with it). Measured on the
    # attract's level-2 demo: the dragon's attack objects (slots 9-14, set 0x5B) held +0xA = 0
    # on the port and 4 on the arcade from tick 0x4F, and the allocator drifted from there.
    'FUN_0000af8c': ('unaff_A6 + 0x80', ('request_palette_update',), 1),   # 0xB046
    'FUN_0000b204': ('psVar5', ('request_palette_update',), 1),            # 0xB2AA
    'FUN_0000b0a0': ('unaff_A6 + 0x300', ('request_palette_update',), 1),  # 0xB16E
    'FUN_0000baa0': ('unaff_A6 + 0x80', ('request_palette_update',), 1),   # 0xBB14
    'FUN_0000c7e6': ('unaff_A6 + 0x80', ('request_palette_update',), 1),   # 0xC886
    'FUN_0000cbdc': ('unaff_A6 + 0x80', ('request_palette_update',), 1),   # 0xCC10
    'FUN_0001124e': ('puVar6', ('request_palette_update', 'sprite_build_and_cull'), 2),   # 0x11316: 3b2e then 3dd8
    'FUN_00017c1a': ('puVar1', ('request_palette_update',), 1),             # 0x17CAC
    'FUN_0001848e': ('pbVar7', ('request_palette_update',), 2),             # 0x1854C
    'FUN_00018c82': ('psVar2', ('request_palette_update',), 1),             # 0x18D42..4C: palette on the new shot, not its parent
    'FUN_0001962c': ('psVar2', ('request_palette_update',), 1),             # 0x196A2
}
_a6n = {}
_blocks = re.split(r'(?=/\* ---- 0x)', src)
for _bi, _blk in enumerate(_blocks):
    _m = re.match(r'/\* ---- 0x([0-9A-F]+)\s+(\w+)', _blk)
    if not _m or _m.group(2) not in _A6SW:
        continue
    _rec, _helpers, _want = _A6SW[_m.group(2)]
    for _h in _helpers:
        _blk, k = re.subn(r'^(\s*)((?:\w+ = )?%s\([^;]*\);)$' % _h,
                          lambda m: ('%s{ void *_sv = _r_unaff_A6; _r_unaff_A6 = (byte *)(%s); %s _r_unaff_A6 = _sv; }   /* the 68K runs this on the record it just filled (movea.l a5,a6): cport_prep.py */'
                                     % (m.group(1), _rec, m.group(2))), _blk, flags=re.M)
        _a6n[_m.group(2)] = _a6n.get(_m.group(2), 0) + k
    _blocks[_bi] = _blk
src = ''.join(_blocks)
for _fn, (_rec, _helpers, _want) in _A6SW.items():
    _pass_report('%s: helpers run on %s' % (_fn, _rec), _a6n.get(_fn, 0), _want)

# COMPARING AN UNSIGNED VALUE AGAINST -1 IS ALWAYS TRUE IN C.
#
# The 68000's `dbra` tests a 16-bit counter against 0xFFFF, and Ghidra writes that
# as `(*(ushort *)X) != -1`. In C the ushort promotes to int (0..65535) and -1 stays
# -1, so the comparison can never hold and the loop never ends.
#
# Measured 2026-09-30: textram_stride_write is a five-line string copy whose whole
# loop is that one test, and it took 89% of all master cycles -- the rom hung in a
# routine that writes text.
#
# Only the UNSIGNED forms are wrong. `sVar2 != -1` on a signed short is correct and
# is left alone; 106 of the 110 comparisons against -1 in the unit are that.
_UNS_NEG1 = re.compile(r'(\(\s*\*\(\s*(?:ushort|byte|uint)\s*\*\)[^;]{0,120}?\)\s*)'
                       r'([!=]=)\s*-1\b')
_MASK = {'ushort': '0xFFFFu', 'byte': '0xFFu', 'uint': '0xFFFFFFFFu'}

def _fix_neg1(text):
    n = 0
    def rep(m):
        global_n = None
        lhs, op = m.group(1), m.group(2)
        ty = re.search(r'\*\(\s*(ushort|byte|uint)\s*\*\)', lhs).group(1)
        return '%s%s %s' % (lhs, op, _MASK[ty])
    text, n = _UNS_NEG1.subn(rep, text)
    return text, n

src, _nneg1 = _fix_neg1(src)
print('unsigned comparisons against -1 given the width mask: %d' % _nneg1)

protos = ['%s %s(%s);' % (rettype.get(n, 'uint'), n, params.get(n, ''))
          for n in sorted(set(names))]
print('functions with register PARAMETERS kept: %d of %d'
      % (sum(1 for n in set(names) if params.get(n)), len(set(names))))
_nwide = sum(1 for n in set(names) if rettype.get(n) == 'ulonglong')
print('functions returning a PACKED multi-register value kept as ulonglong: %d'
      % _nwide)
print('normalised %d function definitions to uint NAME()' % len(names))
open(os.path.join(outdir, 'protos.h'), 'w').write(
    '/* GENERATED by tools/cport_prep.py -- forward declarations.\n'
    ' * Every function is `uint NAME()`: unprototyped, because Ghidra has no\n'
    ' * calling convention for this program and its parameter lists are\n'
    ' * invention. Real signatures get declared per function as the port\n'
    ' * understands each one against the L1 vectors. */\n'
    '#ifndef PROTOS_H\n#define PROTOS_H\n#include "ghidra_types.h"\n'
    + '\n'.join(protos) + '\n#endif\n')

# PSEUDO-REGISTERS AS GLOBALS. Ghidra emits `unaff_A6` for a register the
# caller sets and the callee reads, because no calling convention is declared.
# Declaring the convention in Ghidra was TRIED AND MEASURED WORSE (415 -> 253
# functions compiling, errors 589 -> 812: forcing a D0 return on functions that
# return nothing, and adding parameters call sites do not pass, costs more than
# it buys -- tools/ghidra/f1_unconvention.py records it). Declaring them as
# GLOBALS compiles, and for A6 it is also semantically right: A6 is UNAFFECTED
# across calls, so in a single-threaded game loop it IS a global current-object
# pointer, which is what the object dispatcher at 0x039AC maintains.
plines = ['/* GENERATED by tools/cport_prep.py -- the decompiler\'s pseudo-registers,',
          ' * declared as globals. Typed by register class and width suffix:',
          ' * A* = pointer, D* = value; trailing b = byte, w = word, else long. */',
          '#ifndef PSEUDOREGS_H', '#define PSEUDOREGS_H',
          '#include "ghidra_types.h"', '#include "obj_record.h"', '']
for nm in sorted(pseudo):
    m2 = re.match(r'(?:extraout|unaff|in)_([AD])(\d)([bw]?)', nm)
    if not m2:
        plines.append('extern uint _r_%s;' % nm); continue
    cls, _, wid = m2.groups()
    if cls == 'A' and not wid:
        # ALWAYS byte *, never OBJ *. Ghidra writes `*(byte *)(unaff_A6 + 0x2f)`
        # meaning BYTE arithmetic; typing A6 as OBJ * makes `+ 0x2f` stride by
        # 0x80 and is both wrong and a type error. MEASURED: OBJ * gave
        # 303/623 compiling, byte * gives more. obj_record.h stays as the
        # documented layout for the port to use deliberately.
        ty = 'byte *'
    else:
        ty = {'b': 'byte', 'w': 'ushort', '': 'uint'}[wid]
    plines.append('extern %s_r_%s;   /* %d refs */' % (ty + ('' if ty.endswith('*') else ' '),
                                                       nm, pseudo[nm]))
for f in FUNCPTRS:
    plines.append('extern uint %s();' % f)
plines += ['', '#endif', '']
open(os.path.join(outdir, 'pseudoregs.h'), 'w').write('\n'.join(plines))

# THE OBJECT RECORD, read off the oracle by the builder 2026-09-26
# (docs/proof/obj_handlers.md): A6 walks the table in 0x80 steps.
open(os.path.join(outdir, 'obj_record.h'), 'w').write('''\
/* GENERATED by tools/cport_prep.py.
 * The System 16 object record, read off a live capture of the dispatcher at
 * 0x039AC over Mike's full playthrough (docs/proof/obj_handlers.md): 1,613
 * dispatches, 83 distinct handlers. A6 walks the table in 0x80 steps -- the
 * 128-byte stride PORT-CENSUS flags as cache-hostile and FULL-PORT §6(b)
 * proposes re-striding to 144 so every slot gets its own cache set. */
#ifndef OBJ_RECORD_H
#define OBJ_RECORD_H
#include "ghidra_types.h"
#define OBJ_STRIDE 0x80
typedef struct OBJ {
    ushort flags;          /* +0x00  status/flags, observed 0x88xx / 0x89xx  */
    void  *handler;        /* +0x02  handler pointer, dispatched by jsr (A0) */
    ushort type;           /* +0x06  object type                            */
    ushort unk_08;         /* +0x08                                         */
    ushort sprite_attr;    /* +0x0A  sprite attribute                       */
    byte   rest[OBJ_STRIDE - 0x0C];
} OBJ;
#endif
''')

# EXTERNS FROM GCC'S OWN ERRORS. Ghidra names string literals (s_FOO_0001be0e),
# pointer cells (PTR_DAT_00019848), switch markers (switchD_00002ef0), stack and
# register placeholders (stack0xfffffffc, register0x0000003c) and second symbols
# at one address (DAT_fffff032_1). Predicting that vocabulary is a losing game:
# instead tools/cport_build.sh compiles, harvests "X undeclared", and writes them
# here, so one more pass closes whatever Ghidra invented.
xl = ['/* GENERATED by tools/cport_prep.py from gcc\'s undeclared list. */',
      '#ifndef EXTRAS_H', '#define EXTRAS_H', '#include "ghidra_types.h"', '']
TYPEDEFD = {'BADSPACEBASE', 'code', 'bool'}
# SYMBOLS THE DRIVER OWNS. gcc's undeclared harvest runs on an EARLIER compile pass
# than the preamble this script writes, so a symbol the preamble declares can still
# turn up in that list -- and then extras.h declares it `extern uint` and defs.c
# DEFINES it, which collides with the driver's real definition at link time. Naming
# them here is the fix; the link error is the good outcome, since a silently separate
# copy would read zero forever.
_DRIVER_OWNED = {'port_fn_count', 'port_fn_seq', 'port_set_reg'}
_DECLARED = set(syms) | set(pseudo) | _DRIVER_OWNED
for nm in sorted(set(_ov.get('undeclared', [])) - TYPEDEFD - _DECLARED):
    if re.match(r'^(s_|PTR_|DAT_|_DAT_|[a-z]{1,3}Ram)', nm):
        xl.append('extern byte %s[];' % nm)
    elif nm.startswith('switch'):
        xl.append('extern byte %s[];' % nm)
    else:
        # LAB_ data labels are BYTE-typed: the decompile does `&LAB_00002686 + sVar5` and
        # `(&LAB_00006d5a_2 + iVar1)` expecting byte arithmetic; as `uint` the pointer
        # stepped 4x (rd56: the eye-pan camera read its script entries 32 bytes apart,
        # x velocity 0x0300/0x7333 where the arcade's are 0x01c8/0xfff8).
        xl.append('extern %s %s;' % ('byte' if nm.startswith('LAB_') else 'uint', nm))
xl += ['', '#endif', '']
open(os.path.join(outdir, 'extras.h'), 'w').write('\n'.join(xl))

# DEFINITIONS. Everything above is `extern`, so the corpus COMPILES but will not
# LINK. Emit one definition per symbol into its own file: the 68K work RAM, the
# program-ROM constants, the S16 hardware window and the pseudo-registers all
# become plain objects the linker can place. The port will later map the work-RAM
# block onto real SDRAM (FULL-PORT F0(a)'s 15.6 KB); for verification it only has
# to exist and be addressable.
dl = ['/* GENERATED by tools/cport_prep.py -- definitions for every extern.',
      ' * Lets the corpus LINK, which is what a per-function differential needs. */',
      '#include "ghidra_types.h"', '#include "obj_record.h"',
      '#include "w68.h"',              # port_reset_sp needs W68PTR
      '#include "extras.h"',           # ... and register0x0000003c's declaration
      '']
# NOTE: the absolute symbols are NOT defined here. tools/cport_layout.py emits
# layout.ld, which PROVIDEs each at its true 68K address so that pointer
# arithmetic crossing symbols lands correctly. Defining them here too would
# collide, and would scatter the work RAM the port needs contiguous.
for nm in sorted(pseudo):
    m4 = re.match(r'(?:extraout|unaff|in)_([AD])(\d)([bw]?)$', nm)
    if not m4:
        dl.append('uint _r_%s;' % nm); continue
    c4, _, w4 = m4.groups()
    ty = ('byte *' if c4 == 'A' and not w4
          else {'b': 'byte', 'w': 'ushort', '': 'uint'}[w4])
    dl.append('%s_r_%s;' % (ty + ('' if ty.endswith('*') else ' '), nm))
# DO NOT FABRICATE A SYMBOL THE LINKER SCRIPT WILL PLACE. A `byte NAME[64];` here
# is a ZEROED array, and for a PTR_*/switchD_*/LAB_*/s_* symbol that is a ROM table
# read as zeros -- measured 2026-09-28, 208 indirect calls a frame resolving to
# address 0. And `PROVIDE` in layout.ld only defines a symbol that is NOT otherwise
# defined, so a definition here does not merely duplicate the placement: IT WINS.
#
# THE DECISION IS BY ADDRESS, NOT BY NAME, and it must agree with cport_layout.py
# exactly -- hence tools/cport_names.py, which both import. Exact string equality
# leaked twice (`+` is not a C identifier character; the listing says PTR_DAT_ where
# the decompiler says PTR_FUN_) and silently dropped six tables on 2026-10-02, one
# of them PTR_LAB_00010202_1_00001cda at ROM 0x1CDA -- the table the MAIN LOOP'S
# PROLOGUE reads to set the mode byte 0xFFFFF142, which the frame loop branches on.
import importlib.util as _ilu
_spec = _ilu.spec_from_file_location(
    'cport_names', os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                'cport_names.py'))
_cn = _ilu.module_from_spec(_spec)
_spec.loader.exec_module(_cn)

_sa = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   'docs', 'audit', 'sym_addrs.txt')
_placed = {_n for _av, _ns in _cn.load_sym_addrs(_sa).items() for _n in _ns
           if _cn.is_rom_table(_n)}
_fab, _unplaceable, _addrfab = 0, [], []
for nm in sorted(set(_ov.get('undeclared', [])) - {'BADSPACEBASE', 'code', 'bool'} - _DECLARED):
    if nm in _placed:
        continue                       # layout.ld PROVIDEs it under this spelling
    if _cn.is_rom_table(nm):
        _av = _cn.addr_of(nm)
        if _av is not None and _av < 0x40000:
            continue                   # layout.ld places it from the name's address
        # NO PLACEMENT AND NO ADDRESS IN THE NAME. Ghidra truncates a long string
        # literal's name (s_TAKYOUR_NAME__); that is the only case where the
        # address cannot be recovered from the symbol itself.
        _unplaceable.append(nm)
        dl.append('byte %s[64];' % nm)
        _fab += 1
        continue
    if re.match(r'^(s_|PTR_|DAT_|_DAT_|[a-z]{1,3}Ram|switch)', nm):
        dl.append('byte %s[64];' % nm)
        _fab += 1
    else:
        dl.append('uint %s;' % nm)

# THE WILD-JUMP CLASS REFUSES. Zeros in a string table are blank text; zeros in a
# PTR_/switch/LAB_ table are a dispatch to address 0, which is how a frame's worth
# of indirect calls disappeared in September. It does not get to be silent again.
_wild = [n for n in _unplaceable if _cn.is_jump_table(n)]
if _wild:
    raise SystemExit('cport_prep: %d jump/pointer table(s) have no ROM address and '
                     'would be fabricated as ZEROED arrays, which dispatches to '
                     'address 0:\n  %s\nAdd them to docs/audit/sym_addrs.txt '
                     '(tools/ghidra/sym_addrs.py).' % (len(_wild), ' '.join(_wild)))
if _unplaceable:
    print('defs.c: *** %d ROM symbol(s) FABRICATED AS ZEROS -- no address in the '
          'name, so each reads 0 where the 68K reads ROM: %s'
          % (len(_unplaceable), ' '.join(_unplaceable)))
print('defs.c: %d symbol(s) fabricated, %d ROM tables named by the listing'
      % (_fab, len(_placed)))
for f in FUNCPTRS:
    dl.append('uint %s(){return 0;}' % f)
# EVERY RUN MUST START FROM THE SAME MACHINE STATE, not just the same work RAM.
# The pseudo-registers are globals that PERSIST between calls, so a routine run
# twice sees whatever the previous run left in them. Measured 2026-09-28: the L2
# score for one fixed call sequence read 191 in one rom and 207 in another whose
# only difference was that MORE variants ran after it -- the residue from the
# later variants changed the earlier one's result on the next sweep. A harness
# that re-seeds memory but not these is comparing different machines.
dl.append('')
dl.append('/* THE 68K STACK POINTER (A7 = register space 0x3C). 0xFFFFFF00 is what')
dl.append(' * `lea 0xffffff00,%sp` loads at 0x40E/0x4A0/0x54E/0x5BE/0x62A -- inside')
dl.append(' * work RAM, so the decompiled stack spills land where the 68000 put them.')
dl.append(' * See the note in tools/cport_prep.py. */')
dl.append('#define M68K_RESET_SP 0xFFFF00u')
dl.append('void port_reset_sp(void)')
dl.append('{')
dl.append('    register0x0000003c = (uint)(W68PTR(M68K_RESET_SP));')
dl.append('}')
dl.append('')
dl.append('/* reset every pseudo-register: see the note in tools/cport_prep.py */')
dl.append('void port_reset_pseudoregs(void)')
dl.append('{')
for nm in sorted(pseudo):
    dl.append('    _r_%s = 0;' % nm)
dl.append('    port_reset_sp();')
# THE 68K STACK POINTER IS A REAL REGISTER WITH REAL MEMORY BEHIND IT.
#
# Ghidra names 68K register-space offset 0x3C -- which is A7, the SP -- as
# `register0x0000003c`, and it could not model `move.w d0,-(sp)` / `move.w (sp)+,d0`
# as locals, so the decompiled code SPILLS THROUGH IT:
#
#     *(undefined2 *)((int)register0x0000003c + -8) = (short)uVar4;   /* the loop counter */
#     hide_object_sprite();
#     uVar2 = *(undefined2 *)((int)register0x0000003c + -8);          /* read back */
#     if (0x17 < (char)uVar2 + 1) return 0;                           /* the EXIT TEST */
#
# Defined as a bare `uint`, it is 0, so those writes land at address -8 and the
# reload cannot terminate the loop. Measured 2026-10-02: FUN_0000e16a took 42% of
# the master's instructions and the rooted main loop never finished a frame
# (FULL-PORT.md 35).
#
# THE VALUE IS NOT A GUESS. The program loads it five times in the listing --
# prog68k.asm:272, 326, 365 and at 0x5BE/0x62A, the main loop's entry and its loop
# top -- always `lea 0xffffff00,%sp`. 0xFFFFFF00 is inside work RAM, so the port's
# SP is simply that cell's SH-2 address and the spills land exactly where the 68000
# put them.
dl.append('}')
dl.append('')

dl += ['uint halt_baddata(){return 0;}', 'uint halt_unimplemented(){return 0;}',
       'uint switchD(){return 0;}', 'uint __m68k_trap(){return 0;}',
       'uint bcdAdjust(){return 0;}', 'uint invalidateCacheLines(){return 0;}',
       'uint stop(){return 0;}', '']
open(os.path.join(outdir, 'defs.c'), 'w').write('\n'.join(dl))

out = ['#include "ghidra_types.h"', '#include "ghidra_rt.h"', '#include "w68.h"',
       '#ifdef PORT_TILEDIRTY',
       'extern volatile unsigned short port_tile_dirty;   /* sh_src/r60/r60_main.c */',
       '#endif',
       '#include "extras.h"',
       '/* The SH-2 needs 4-byte alignment for a 32-bit access; the 68000 needed',
       ' * only 2, and 147 of the 302 symbols this port places are 2 mod 4. A',
       ' * packed struct makes gcc emit byte-wise code, so a misaligned access',
       ' * reads the right value instead of raising an address error. */',
       '/* A 68K address held in DATA, translated when it is USED as a pointer.',
       ' * Same region split as W68, but on a runtime value. See the note in',
       ' * tools/cport_prep.py: an untranslated one made floor_collide walk',
       ' * forever and took 87.6%% of the master. */',
       '/* An SH-2 pointer reaches P68 too: FUN_0000e20c returns W68(link) and its',
       ' * callers read `*(byte *)P68(iVar12 + 0x23)` (the parent zombie\'s state).',
       ' * Masking that to 24 bits made it a ROM offset and the child zombies never',
       ' * saw the parent reach state 4 (rd109, 2026-10-04: the trio survived the',
       ' * flame kick). A 68K address has its top byte 0x00 or 0xFF (sign-extended',
       ' * work RAM); anything else is already a pointer and is passed through. */',
       '#define P68(v) ((((uint)(v)) >> 24) != 0u && (((uint)(v)) >> 24) != 0xFFu ? (void *)(uint)(v) : \\',
       '        (void *)(((uint)(v) & 0xFFFFFFu) >= 0xFFC000u \\',
       '        ? (char *)WRAM_BASE + ((((uint)(v)) & 0xFFFFFFu) - 0xFFC000u) \\',
       '        : (char *)ROM_BASE  + (((uint)(v)) & 0x3FFFFu)))',
       'typedef struct { uint v; } __attribute__((packed, aligned(1))) _u32un;',
       'typedef struct { int  v; } __attribute__((packed, aligned(1))) _i32un;',
       '#define LD32U(p) (((const _u32un *)(void *)(p))->v)',
       '#define LD32S(p) (((const _i32un *)(void *)(p))->v)',
       '#define ST32(p, x) (((_u32un *)(void *)(p))->v = (uint)(x))',
       '/* CPORT_HI: a 68000 `move.w #k,0xFFF132` sets a long\'s HIGH half and leaves',
       ' * its low half. A C assignment to the 4-byte cell replaces both, which zeroes',
       ' * the fixed-point fraction. Census from the program bytes: 5 movew writes at',
       ' * 0xFFF132 and ONE movel, and that one is the advance inside FUN_000039C8. */',
       '#define SETHI16(c, v) ((c) = (((c) & 0xFFFFu) | ((unsigned)(v) << 16)))',
       '/* the 68K -> SH-2 indirect call: every `(**(code **)(X))()` in the',
       ' * decompiled program was rewritten to go through this, because the',
       ' * pointer it reads is a 68K program address and calling it directly is a',
       ' * wild jump. Defined by the driver (sh_src/port_l2.c), which owns the',
       ' * address table and the miss counter. */',
       '/* THE REGISTER FILE. D0..D7 = [0..7], A0..A6 = [8..14] -- the same order',
       ' * the dispatch thunks index. It exists because the 68000 threads values',
       ' * between routines in registers and a by-value C parameter cannot carry a',
       ' * modification back out (FULL-PORT.md 30). Defined by sh_src/port_l2.c. */',
       'extern unsigned int _port_reg[15];',
       'extern unsigned int _port_reg_n;   /* write-backs EXECUTED */',
       'uint port_dec_jump(unsigned int h, unsigned int d5, unsigned int d6, unsigned int d7);   /* the forms\' attack lookups: D5-D7 then the handler at h (port_l2.c) */',
       'uint port_call68_at(unsigned int site, unsigned int a68);',
       '/* set a LIVE 68K register before an indirect call: the dispatch thunks read',
       ' * the driver\'s port_cur_regs, not _port_reg. See sh_src/port_l2.c. */',
       'void port_set_reg(unsigned int ix, unsigned int v);',
       '/* CPORT_COUNT: entry counters, owned and published by the driver. DECLARED',
       ' * here so defs.c does not FABRICATE it -- an unknown symbol in the unit',
       ' * becomes a zeroed definition there, and two definitions is a link error',
       ' * (which is the good case; a silently separate copy would read zero). */',
       'extern volatile unsigned int port_fn_count[8];',
       'extern volatile unsigned int port_fn_seq;',
       '#include "obj_record.h"', '#include "dat_syms.h"',
       '#include "pseudoregs.h"', '#include "protos.h"',
       '/* PORTHOT (CPORT_HOT): the named hot functions move out of .portcode (cart) into',
       ' * .ramtext (SDRAM, boot-copied). Empty without -DPORT_HOT, so a plain build is',
       ' * byte-identical. Defined here rather than in ghidra_types.h because this unit',
       ' * includes only pseudoregs.h and protos.h. */',
       '#ifdef PORT_HOT',
       '#define PORTHOT __attribute__((section(".ramtext")))',
       '#else',
       '#define PORTHOT',
       '#endif', '']
# THIS PASS RUNS LAST, ON THE FINAL BODY. Placed earlier it saw signatures and
# bodies that later passes still rewrite -- FUN_0000D608, the function that
# motivated it, was silently skipped -- and a rewrite that depends on the shape of
# the code has to run after everything that changes that shape.
# ---- NORMALISE AN `int` PARAMETER THAT IS USED AS A POINTER -------------------
#
# The 68000 passed addresses in A-registers and Ghidra types them `int` when it
# cannot decide. The SAME parameter then receives an SH-2 pointer at one call site
# and a BARE 68K ADDRESS at another -- `walk_player_slots` does exactly this to
# FUN_0000D608, twice each way -- and the body dereferences it. On the SH-2 a bare
# 0xFFFFC000 addresses the ON-CHIP PERIPHERAL REGISTERS, so a collision outcome was
# decided by whatever a peripheral held (FULL-PORT.md 44).
#
# 1,238 bare 0xFFFFxxxx constants appear in the program and NOT all of them are
# addresses -- `sound_enqueue(0xffffff92)` is the sound command -110 -- so the call
# sites cannot be classified. THE CALLEE CAN BE. A parameter that the body
# DEREFERENCES is an address by definition, whatever its declared type, and the two
# domains are disjoint by range, so one normalisation at entry settles every call
# site at once and costs one compare per call.
#
# Deliberately NOT a rewrite of the 1,238 constants: that is the over-reach
# cport_prep already carries a scar from ("372 sites for the 2 that actually warn,
# and 339 errors").
_INTTY = ('int', 'uint', 'undefined4', 'long', 'ulong')
_norm = [0, 0]

def _norm_params(blk):
    m = re.match(r'/\* ---- (0x[0-9A-F]+)\s+(\w+)', blk)
    if not m:
        return blk
    sig = re.search(r'\n(\w[\w ]*\s+%s\s*)\(([^)]*)\)\s*\n?\s*\{' % re.escape(m.group(2)), blk)
    if not sig:
        if os.environ.get('CPORT_NORMDBG') and m.group(2) in os.environ['CPORT_NORMDBG']:
            print('  NORMDBG %s: NO SIGNATURE MATCH' % m.group(2))
        return blk
    body = blk[sig.end():]
    hits = []
    for prm in sig.group(2).split(','):
        pm = re.match(r'^\s*([\w ]+?)\s+(\w+)\s*$', prm)
        if not pm or pm.group(1).strip() not in _INTTY:
            continue
        v = re.escape(pm.group(2))
        # LD32S/LD32U/ST32 ARE DEREFERENCES TOO. The 32-bit access rewrite (_LD32, above)
        # runs BEFORE this pass and turns `*(int *)(p_A5 + 8)` into `LD32S((p_A5 + 8))`,
        # so a parameter whose only use was a 32-bit load was never normalised.
        # MEASURED 2026-10-25 (game-over lane): FUN_000011f0 / FUN_00001206 / FUN_0000121c
        # / FUN_0000316c take P1's record as the bare constant 0xffffe000 (`lea
        # 0xffffe000,a5`; P2's is the symbol &DAT_ffffe080) and read its text pointer
        # with LD32S((p_A5 + 8)) -- SH-2 0xFFFFE008, the on-chip register file -- so
        # P1's CONTINUE countdown, PUSH 1P START / INSERT COIN and lives icon never
        # reached text RAM while P2's did.
        if (re.search(r'\*\s*\(\s*\w+\s*\*\s*\)\s*\(\s*%s\b' % v, body)
                or re.search(r'\*\s*\(\s*\w+\s*\*\s*\)\s*%s\b' % v, body)
                or re.search(r'\b(?:LD32[SU]|ST32)\(\s*\(*\s*%s\b' % v, body)
                or re.search(r'\b%s\s*\[' % v, body)):
            hits.append(pm.group(2))
    if not hits:
        if os.environ.get('CPORT_NORMDBG') and m.group(2) in os.environ['CPORT_NORMDBG']:
            print('  NORMDBG %s: params=%r  sig=%s' % (m.group(2), sig.group(2), bool(sig)))
        return blk
    _norm[0] += len(hits); _norm[1] += 1
    # AFTER THE DECLARATION BLOCK, AND INSIDE THE FUNCTION. The decompiled bodies
    # separate declarations from code with a line holding two SPACES, not an empty
    # line, so a search for '\n\n' runs past the closing brace and plants the
    # statement at FILE SCOPE -- which is what happened the first time. Bound the
    # search to this function and match a whitespace-only line.
    o = sig.end()
    end = blk.find('\n}', o)
    if end < 0:
        end = len(blk)
    gap = re.search(r'\n[ \t]*\n', blk[o:end])
    ins = o + gap.end() if gap else o
    # CPORT_ANYNORM NAMES THE FUNCTIONS WHOSE PARAMETER CAN ALSO BE A 68K ROM ADDRESS.
    #
    # A68NORM handles TWO domains -- 68K work RAM and an SH-2 pointer -- and is the
    # identity for everything else, so a parameter that can also carry a 68K PROGRAM
    # address is left untranslated and the callee dereferences it as an SH-2 one.
    #
    # MEASURED 2026-10-03 (FULL-PORT.md 66): FUN_0000D868 is the object allocator, and
    # after claiming a slot it fills the record from the SPAWN-TABLE ENTRY its caller
    # passed (`*(short *)(p_A0 + 2)`, `+4`, `+6`) -- 68K 0x1D4D6, in program ROM. The
    # slot was allocated and its 31 remaining bytes stayed zero, so the arcade's
    # frame-158 spawn produced an object with no position, no sprite and no script.
    #
    # Named rather than applied everywhere, because A68ANY adds a ROM branch and a
    # small integer that is NOT an address would be remapped by it. The gate
    # arbitrates each function, the same discipline as CPORT_CLAMP/HI/A6/AREG/DREG.
    _anyset = {x.strip() for x in (os.environ.get('CPORT_ANYNORM') or '').split(',')
               if x.strip()}
    _nf = 'A68ANY' if (m.group(2) in _anyset or 'all' in _anyset) else 'A68NORM'
    stmt = ('\n  /* a 68K work-RAM address and an SH-2 pointer reach this parameter from\n'
            '   * different call sites; they are disjoint by range. See w68.h.%s */\n'
            % ('\n   * A68ANY: this one can also carry a 68K PROGRAM address (CPORT_ANYNORM).'
               if _nf == 'A68ANY' else '')
            + ''.join('  %s = (int)%s(%s);\n' % (h, _nf, h) for h in hits))
    return blk[:ins] + stmt + blk[ins:]

# ---- RESTORE THE A6 THE CALLER SET -------------------------------------------
#
# Ghidra FOLDS A6 at the uses it can see and DROPS it at the call. `lea (-0x2000).w,A6
# ; bsr 0x374a ; clr.l (0x4,A6)` comes out as `_DAT_ffffe004 = 0; FUN_0000374a();` --
# it resolved the value for the `clr.l` and passed nothing to the callee. A6 is a
# global in the port, so the callee runs against whatever the last dispatch left.
#
# Measured 2026-10-02: ONE such site (FUN_00003716) was worth 7 cells at the seeded
# frame -- 15 wrong to 8 -- and took the 8-frame free-run from 241 wrong to 31, its
# first NET CORRECT. 210 instructions in the program load A6.
#
# THE TABLE IS DERIVED FROM THE DISASSEMBLY, NOT GUESSED: docs/audit/a6_callsites.json
# lists every (caller, callee, A6) where A6 is loaded with a WORK-RAM CONSTANT and a
# call follows before it is reloaded. 27 triples, 20 of them unambiguous (the caller
# calls that callee exactly once). Only the unambiguous ones are applied; the other
# seven need an ordinal match and are left for a site-by-site pass.
_A6TAB = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                      'docs', 'audit', 'a6_callsites.json')
_a6n = [0]
try:
    _a6rows = json.load(open(_A6TAB))
except Exception:
    _a6rows = []
    print('cport_prep: no %s -- A6 call-site restoration is OFF' % _A6TAB)
_a6cnt = {}
for _r in _a6rows:
    _a6cnt[(_r['caller'], _r['callee'])] = _a6cnt.get((_r['caller'], _r['callee']), 0) + 1
# OPT-IN PER CALLER, because applying all twenty at once BROKE THE GAME. Measured
# 2026-10-02: the seeded frame stayed at 8 wrong and the 8-frame free-run drove the
# game's own counter to -1217 of 8 -- caught only by the progress check, which is
# exactly why that check exists. A table derived from the disassembly can still be
# wrong about WHICH C call a 68K call site is, and one bad A6 corrupts the run.
#
# So CPORT_A6 names the callers to apply, the gate arbitrates each, and the default is
# the set that has been MEASURED. `all` applies everything.
_A6_OK = os.environ.get('CPORT_A6', '')
_a6_set = None if _A6_OK == 'all' else {x.strip() for x in _A6_OK.split(',') if x.strip()}
_a6map = {}
for _r in _a6rows:
    if _a6cnt[(_r['caller'], _r['callee'])] != 1:
        continue
    if _a6_set is not None and _r['caller'] not in _a6_set:
        continue
    _a6map.setdefault(_r['caller'], []).append((_r['callee'], _r['a6']))

def _a6_restore(blk):
    m = re.match(r'/\* ---- (0x[0-9A-F]+)\s+(\w+)', blk)
    if not m or m.group(2) not in _a6map:
        return blk
    for callee, a6 in _a6map[m.group(2)]:
        pat = re.compile(r'(^([ \t]*))(' + re.escape(callee) + r'\s*\([^;]*\);)', re.M)
        def _ins(mm):
            # SAVE AND RESTORE, because A6 is a GLOBAL in the port and a bare
            # assignment leaks past the call. On the 68000 every consumer loads A6 for
            # itself, so nothing downstream depends on the value surviving; in the port
            # it persists and the next dispatched handler inherits it. Measured
            # 2026-10-02: assigning without restoring left the game's own frame counter
            # at -1217 of 8 on a free-run while the seeded frame still read 8 wrong --
            # caught only by the progress check.
            _a6n[0] += 1
            i = mm.group(2)
            return ('%s{   /* the caller loads A6 with 0x%06X here and Ghidra drops it;\n'
                    '%s     * docs/audit/a6_callsites.json */\n'
                    '%s    byte *_a6sv = _r_unaff_A6;\n'
                    '%s    _r_unaff_A6 = (byte *)W68PTR(0x%06Xu);\n'
                    '%s    %s\n'
                    '%s    _r_unaff_A6 = _a6sv;\n'
                    '%s}'
                    % (i, a6, i, i, i, a6, i, mm.group(3), i, i))
        _n_before = _a6n[0]
        blk = pat.sub(_ins, blk, count=1)
        if _a6n[0] != _n_before:
            continue
        # ---- THE CALL IS NOT A STATEMENT. Ghidra puts it inside a condition:
        #   if ((bVar5) || (iVar4 = FUN_0000d868(a,b,0), !bVar5)) break;
        # and the statement pattern above -- which wants the callee at the start of a
        # line, ending in `);` -- never saw it. MEASURED 2026-10-03 (FULL-PORT.md 66):
        # that is the SPAWN-TABLE WALKER, whose `lea 0xffffd800,%fp` at 0xD822 points
        # the allocator at SLOT 48. Without it the allocator scanned from whatever
        # slot was dispatched last, so the arcade's spawn at frame 158 never happened
        # and 33 bytes of slot 48 stayed zero.
        #
        # So wrap the ENCLOSING STATEMENT instead: back to the previous `;`, `{` or
        # `}` and forward to the next `;` at brace depth 0. For the `if` above that is
        # the whole `if (...) break;`, which is where A6 has to be set anyway -- the
        # call sits in the condition and is evaluated as part of it.
        _ci = blk.find(callee + '(')
        if _ci < 0:
            continue
        _st = max(blk.rfind(';', 0, _ci), blk.rfind('{', 0, _ci), blk.rfind('}', 0, _ci)) + 1
        _depth, _en = 0, -1
        for _k in range(_ci, len(blk)):
            _c = blk[_k]
            if _c in '({[':
                _depth += 1
            elif _c in ')}]':
                _depth -= 1
            elif _c == ';' and _depth <= 0:
                _en = _k
                break
        if _en < 0:
            continue
        _stmt = blk[_st:_en + 1].strip()
        _ind = re.match(r'[ \t]*', blk[_st:]).group(0) or '    '
        _a6n[0] += 1
        _rep = ('%s{   /* the caller loads A6 with 0x%06X here and Ghidra drops it;\n'
                '%s     * docs/audit/a6_callsites.json. The call is inside a CONDITION,\n'
                '%s     * so the whole statement is wrapped. */\n'
                '%s    byte *_a6sv = _r_unaff_A6;\n'
                '%s    _r_unaff_A6 = (byte *)W68PTR(0x%06Xu);\n'
                '%s    %s\n'
                '%s    _r_unaff_A6 = _a6sv;\n'
                '%s}' % (_ind, a6, _ind, _ind, _ind, _ind, a6, _ind, _stmt, _ind, _ind))
        blk = blk[:_st] + _rep + blk[_en + 1:]
    return blk

src = ''.join(_a6_restore(b) for b in re.split(r'(?=/\* ---- 0x)', src))
print('A6 restored at %d call site(s) of %d unambiguous in the table'
      % (_a6n[0], sum(len(v) for v in _a6map.values())))
if _a6rows and _a6n[0] < sum(len(v) for v in _a6map.values()):
    print('  (the shortfall is callers not in this root set, or a callee name the C '
          'spells differently -- check before trusting a measurement)')

src = ''.join(_norm_params(b) for b in re.split(r'(?=/\* ---- 0x)', src))
print('pointer-ish int parameters normalised at entry: %d in %d function(s)'
      % (_norm[0], _norm[1]))

# A REWRITE PASS THAT CHANGED NOTHING IS A BUG, NOT A NO-OP, AND IT MUST SAY SO.
#
# Every text pass in this file is a regex over GENERATED C, and a regex that stops
# matching after some other pass reshapes the code fails SILENTLY -- the build
# succeeds, the rom measures the same, and the only evidence is a number that did not
# move. That happened three times in a row on 2026-10-02 writing this very pass:
# wrong pipeline position, then a brace that was not where the pattern expected, then
# an insertion point that landed PAST THE CLOSING BRACE, at file scope.
#
# So: CPORT_EXPECT names functions a pass is aimed at, and the build REFUSES if any of
# them came out untouched. Checking the one function you are targeting costs nothing
# and is the difference between a measurement and a guess.
_EXPECT = [x.strip() for x in (os.environ.get('CPORT_EXPECT') or '').split(',') if x.strip()]
if _EXPECT:
    _missed = [f for f in _EXPECT
               if not re.search(r'/\* ---- 0x[0-9A-F]+\s+%s\b[^\0]*?A68NORM' % re.escape(f),
                                src)]
    if _missed:
        raise SystemExit('cport_prep: CPORT_EXPECT named %d function(s) that the '
                         'normalisation did NOT touch: %s\nA pass that misses its '
                         'target is a silent failure; fix the pass, do not build.'
                         % (len(_missed), ', '.join(_missed)))
    print('  CPORT_EXPECT: all %d named function(s) were touched' % len(_EXPECT))

# RUNS LAST, AND THAT POSITION IS LOAD-BEARING. The `(uint)` cast in
# `_DAT_ffffc002 = (uint)FUN_00002490;` is not Ghidra's -- a LATER pass adds it to
# silence gcc's "assignment to uint from code *". Placed with the store rewrite
# earlier in the pipeline this pass found nothing and SAID SO IN A COUNTER THAT
# READ ZERO, which is the only reason it was caught. Anything that rewrites a cast
# the diagnostics feedback loop introduces has to run after that loop.
# THE SAME VALUE REACHES 68K MEMORY TWO MORE WAYS, and both are the same defect.
#
#   (a) `_DAT_ffffc002 = (uint)FUN_00002490;`  -- a slot's handler written through the
#       NAMED work-RAM cell rather than through A6. 62 sites, and every LHS is a slot
#       base + 2 (_DAT_ffffc002, _DAT_ffffc082, _DAT_ffffd402, pcRamffffd802 ...),
#       checked rather than assumed.
#
#   (b) `*(code **)(unaff_A6 + 0x24) == FUN_0000ec98` -- a comparison of the STORED
#       pointer against a function. 24 sites. These were already wrong (an SH-2
#       symbol against whatever the 68K left there) and the store fix makes them
#       wrong the other way, so they have to move together: the load is also a 32-bit
#       read at a possibly-misaligned address, so it becomes LD32U.
#
# One more use is left alone deliberately and recorded instead:
# `LD32U(FUN_000099a2 + ...)` reads a TABLE at a code address, so it needs ROM_BASE
# and not a 68K-address substitution. One site; it belongs to the _rom_bases family,
# not here.
# The named-cell form reaches here as `_DAT_ffffc002 = FUN_00002490;` -- Ghidra emits
# it with NO cast, and the `(uint)` seen in the finished unit is added afterwards by
# cport_build.sh's gcc-diagnostics feedback loop to silence "assignment to uint from
# code *". Rewriting the BARE form means no cast is ever needed, because the value
# becomes a plain integer. Both spellings are matched so the pass cannot be defeated
# by where it sits in the pipeline.
_CASTFN = re.compile(r'\(\s*uint\s*\)\s*FUN_([0-9a-f]{4,8})\b')
src, _ncast = _CASTFN.subn(lambda m: '0x%xu' % int(m.group(1), 16), src)
_BAREFN = re.compile(r'\b(_?DAT_[0-9a-f]{4,8}|[a-z]{1,3}Ram[0-9a-f]{4,8})\s*=\s*'
                     r'FUN_([0-9a-f]{4,8})\s*;')
src, _nbare = _BAREFN.subn(lambda m: '%s = 0x%xu;' % (m.group(1), int(m.group(2), 16)), src)
_ncast += _nbare

def _code_ptr_cmps(text):
    n = 0
    out, i = [], 0
    while True:
        m = _CODEST.search(text, i)
        if not m:
            out.append(text[i:]); break
        open_i = m.end() - 1
        close_i = _paren_span(text, open_i)
        if close_i < 0:
            out.append(text[i:m.end()]); i = m.end(); continue
        expr = text[open_i:close_i + 1]
        rest = text[close_i + 1:]
        cm = re.match(r'\s*(==|!=)\s*FUN_([0-9a-f]{4,8})\b', rest)
        if not cm:
            out.append(text[i:close_i + 1]); i = close_i + 1; continue
        out.append(text[i:m.start()])
        out.append('LD32U%s %s 0x%xu' % (expr, cm.group(1), int(cm.group(2), 16)))
        i = close_i + 1 + cm.end()
        n += 1
    return ''.join(out), n

src, _ncmp = _code_ptr_cmps(src)
print('handler pointers: %d written through a NAMED work-RAM cell, %d COMPARED '
      'against a function' % (_ncast, _ncmp))

# ---- PUBLISH THE A-REGISTERS A FUNCTION COMPUTED, BEFORE IT CALLS INDIRECTLY ----
#
# The object dispatcher establishes A6 and only A6 -- "the only one derivable without
# the oracle". Every other 68K address register a handler computes lives in a
# decompiler LOCAL, and an INDIRECT call goes through a generated thunk that reads
# `_port_reg[n]`. Nothing wrote those, so the callee got whatever a previous handler
# left.
#
# MEASURED 2026-10-03 (FULL-PORT.md 62), and it is why the player is never respawned:
#   0049CC: movea.w ($3c,A6),A5        becomes
#   iVar12 = (int)W68(*(short *)(unaff_A6 + 0x3c));      correct, no register identity
# then FUN_000049BA calls FUN_000053A8 indirectly, whose only parameter IS A5
#   thk_FUN_000053a8(r) { return FUN_000053a8((int)(r[13])); }
# so the `bset #$5,(A4)` that needs A4 = word at A5+0x1C never sets the "player died"
# flag the respawn is gated on. CPORT_COUNT proved which side failed: the gate
# FUN_00000DFA runs, and FUN_00003858 -- reachable only inside the taken branch --
# runs zero times.
#
# THE IDENTITY COMES FROM GHIDRA, because nothing else knows it. The tempting rule
# "a local assigned W68(*(short *)(A6 + 0x3c)) is A5" is FALSE, counted from the
# instruction encodings before this was written: A6+0x3C is loaded into A0 25 times,
# A1 3 times and A5 8 times. Guessing from the offset would corrupt 28 sites to fix 8.
# tools/ghidra/areg_locals.py asks the decompiler for variable STORAGE instead --
# 2,379 address-register symbols across 685 functions, read-only.
#
# ONLY UNAMBIGUOUS REGISTERS ARE PUBLISHED. Ghidra splits a register into several
# locals when its value is reassigned (FUN_000049BA has FOUR A0 locals: p_A0,
# p_A0_00, pbVar11, puVar10), and picking the live one needs dataflow this pass does
# not have. A register with exactly ONE local in its function is unambiguous; the
# rest are left alone and counted. 456 are unambiguous and 427 are not, and only 10
# sites across 5 functions are in a function that calls indirectly at all -- so this
# is surgical, not sweeping.
#
# A6 IS EXCLUDED: the driver owns it (`_r_unaff_A6 = slot`) and writing it from here
# would give one object's A6 to another.
#
# IT GOES THROUGH port_set_reg, NOT `_port_reg`. Those are two different arrays and
# confusing them cost a measurement round: `_port_reg` is the WRITE-BACK file the
# ported code stores into on return, while the dispatch thunks read `port_cur_regs`,
# which is static to the driver. Publishing into `_port_reg` produced the correct
# value in the wrong place and moved the score by one byte.
#
# THE REWRITE IS AN EXPRESSION, not a statement, because `port_call68_at(...)` appears
# inside assignments and conditions and inserting a statement before it would need the
# enclosing statement's boundary:
#     (_port_reg[13] = (uint)iVar12, port_call68_at(n, X))
# The comma operator evaluates the publishes first, and C guarantees that order.
try:
    _AREG = json.load(open(os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
        'docs', 'audit', 'areg_locals.json')))
except Exception:
    _AREG = {}
    print('cport_prep: NO docs/audit/areg_locals.json -- A-register publishing is OFF '
          '(build it with tools/ghidra_run.sh script areg_locals.py '
          'docs/audit/areg_locals.json)')

# WHICH REGISTERS, ONE AT A TIME, ARBITRATED BY THE GATE -- the same discipline as
# CPORT_CLAMP and CPORT_HI. Publishing every unambiguous A-register at once took the
# seeded frame 68 -> 147 wrong and broke slot 40 as well, so "all of them" is not the
# answer and the set has to be found rather than assumed. Default OFF.
_AREG_IX = set()
for _x in (os.getenv('CPORT_AREG') or '').replace('A', '').split(','):
    _x = _x.strip()
    if _x.isdigit():
        _AREG_IX.add(int(_x))
if _AREG and not _AREG_IX:
    _AREG = {}
    print('A-register publishing: OFF (set CPORT_AREG=13 for A5, etc.)')

if _AREG:
    _npub = _nambig = 0
    _pubfns = set()
    _blocks = re.split(r'(?=/\* ---- 0x)', src)
    for _bi, _blk in enumerate(_blocks):
        _m = re.match(r'/\* ---- 0x([0-9A-F]+)\s', _blk)
        if not _m or 'port_call68_at(' not in _blk:
            continue
        _e = _AREG.get(_m.group(1).zfill(5))
        if not _e:
            continue
        _byreg = {}
        for _r in _e['locals']:
            _byreg.setdefault(_r['ix'], []).append(_r)
        _pend = []
        for _ix in sorted(_byreg):
            if _ix == 14:                      # A6: the driver owns it
                continue
            if _ix not in _AREG_IX:
                continue
            _cands = [r['name'] for r in _byreg[_ix]]
            if len(_cands) != 1:
                # AMBIGUOUS: Ghidra SPLIT the register into several locals because its
                # value is reassigned, and the live one is whichever was assigned LAST
                # before the call. That is not a guess about the program -- it is what
                # the SSA naming means, and it is resolved per CALL SITE below rather
                # than per function, because a function with two indirect calls can
                # have a different version live at each.
                #
                # MEASURED 2026-10-03 (FULL-PORT.md 69): the player's handler reaches
                # its second state dispatch with D7 = 0x40, which is what decides
                # whether the state routine NEGATES the new velocity. Ghidra gave D7
                # three locals there (p_D7, uVar8, uVar9) and the one live at the call
                # is uVar8 -- masked `& 0xffff00c0` six lines above it, exactly the
                # `andiw #0xC0,%d7` the 68000 does. Skipping ambiguous registers left
                # the port negating a velocity the arcade does not.
                _nambig += 1
            _pend.append((_ix, _cands))
            # `extraout_X` IS NOT X's LIVE VALUE. Ghidra's name means "X as it is
            # AFTER a call returns" -- a RESULT, not an input -- so publishing it
            # BEFORE a call is semantically backwards. Measured 2026-10-03: including
            # it took the seeded frame 68 -> 147 wrong and broke slot 40 as well,
            # because FUN_000049BA's A1 local is `extraout_A1`.
            if _nm.startswith('extraout_'):
                continue
            pass
        if not _pend:
            continue
        # RESOLVE PER CALL SITE: for each register, the candidate whose last
        # assignment sits nearest ABOVE this particular call.
        _o, _i = [], 0
        while True:
            _j = _blk.find('port_call68_at(', _i)
            if _j < 0:
                _o.append(_blk[_i:]); break
            _op = _j + len('port_call68_at(') - 1
            _cl = _paren_span(_blk, _op)
            if _cl < 0:
                _o.append(_blk[_i:_j + 1]); _i = _j + 1; continue
            _pubs = []
            for _ix, _cands in _pend:
                _best, _bpos = None, -1
                for _c in _cands:
                    if _c.startswith('extraout_'):
                        continue
                    for _am in re.finditer(r'\b%s\s*=(?!=)' % re.escape(_c), _blk[:_j]):
                        if _am.start() > _bpos:
                            _best, _bpos = _c, _am.start()
                if _best is None:
                    # no assignment above this call: a bare `unaff_`/parameter form is
                    # still the live value, an SSA successor is not
                    _un = [c for c in _cands
                           if c.startswith('unaff_') or c.startswith('p_')]
                    if len(_un) == 1 and re.search(r'\b%s\b' % re.escape(_un[0]), _blk):
                        _best = _un[0]
                if _best is None or not re.search(r'\b%s\b' % re.escape(_best), _blk):
                    continue
                _pubs.append('port_set_reg(%d, (uint)%s)' % (_ix, _best))
            if _pubs:
                _o.append(_blk[_i:_j]); _o.append('(' + ', '.join(_pubs) + ', ')
                _o.append(_blk[_j:_cl + 1]); _o.append(')')
                _npub += 1
            else:
                _o.append(_blk[_i:_cl + 1])
            _i = _cl + 1
        _blocks[_bi] = ''.join(_o)
        _pubfns.add(_m.group(1))
    src = ''.join(_blocks)
    print('A-registers published before %d indirect call(s) in %d function(s); %d '
          'register(s) left alone as ambiguous' % (_npub, len(_pubfns), _nambig))

# ---- REGISTERS SET TO A CONSTANT BEFORE AN INDIRECT CALL, WHICH GHIDRA DROPS -------
#
# The decompiler's view of liveness ends at `jsr (An)`, so a register the caller loads
# with a constant just before one looks DEAD and is dropped. The callee then runs on
# whatever the previous handler left -- and this dispatcher deliberately THREADS D3
# and D6 handler to handler, so "whatever was left" is never zero.
#
# MEASURED 2026-10-03 (FULL-PORT.md 64), and it is the last link in the player's
# chain. The respawn dispatches its state routine as
#     49B2: moveq #$0, D6      49B4: moveq #$0, D7      49B6: jsr (A0)
# and that routine, after setting state 4, does `andiw #6,%d6 ; beq` -- with D6 = 0 it
# KEEPS state 4 and script 0x76B0, which is what the arcade does. The port arrived
# with a stale D6, took the other arm, and set state 9 / script 0x7888. From there the
# animation script's X delta is 8 instead of 0, the player drifts, its hitbox misses,
# and slot 16 stops registering hits.
#
# THE TABLE IS READ OUT OF THE 68K's BYTES, not guessed: tools/dreg_callsites.py ->
# docs/audit/dreg_callsites.json. 12 indirect call sites in the program, 3 of them
# carrying a constant load within 4 instructions.
#
# THE MATCH IS ON THE DISPATCH TABLE, NOT AN ORDINAL. FUN_00004870 makes TWO indirect
# calls -- through 0x6D70 and through 0x6D90 -- and counting them in source order is
# the kind of match that is right until the decompiler reorders something. The scanner
# records the base the preceding `lea` loaded and the generated C names it
# (`&PTR_FUN_00006d70`), so this matches a symbol.
#
# CPORT_DREG names the callers to apply, like CPORT_A6 does, because a constant load
# before a call is strong evidence the callee reads it and NOT proof -- the load could
# be for code after the call. The gate arbitrates each.
try:
    _DREG_ROWS = json.load(open(os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
        'docs', 'audit', 'dreg_callsites.json')))
except Exception:
    _DREG_ROWS = []
_DREG_OK = os.environ.get('CPORT_DREG', '')
_dreg_set = None if _DREG_OK == 'all' else {x.strip() for x in _DREG_OK.split(',') if x.strip()}
_ndreg = 0
if _DREG_ROWS and (_dreg_set is None or _dreg_set):
    _blocks = re.split(r'(?=/\* ---- 0x)', src)
    for _bi, _blk in enumerate(_blocks):
        _m = re.match(r'/\* ---- 0x([0-9A-F]+)\s+(\S+)', _blk)
        if not _m:
            continue
        for _r in _DREG_ROWS:
            if _r['caller'] != _m.group(2):
                continue
            if _dreg_set is not None and _r['caller'] not in _dreg_set:
                continue
            if not _r.get('table'):
                continue          # no table to match on; needs a site-by-site pass
            _tag = '0000%s' % _r['table'][2:].lower().lstrip('0')
            _pubs = ', '.join('port_set_reg(%s, 0x%xu)' % (k, v)
                              for k, v in sorted(_r['regs'].items(), key=lambda kv: int(kv[0])))
            _o, _i, _done = [], 0, False
            while True:
                _j = _blk.find('port_call68_at(', _i)
                if _j < 0:
                    _o.append(_blk[_i:]); break
                _op = _j + len('port_call68_at(') - 1
                _cl = _paren_span(_blk, _op)
                if _cl < 0:
                    _o.append(_blk[_i:_j + 1]); _i = _j + 1; continue
                _expr = _blk[_j:_cl + 1]
                if not _done and _tag in _expr.lower():
                    _o.append(_blk[_i:_j]); _o.append('(' + _pubs + ', ')
                    _o.append(_expr); _o.append(')')
                    _done = True
                    _ndreg += 1
                else:
                    _o.append(_blk[_i:_cl + 1])
                _i = _cl + 1
            if _done:
                _blocks[_bi] = ''.join(_o)
                _blk = _blocks[_bi]
    src = ''.join(_blocks)
if _DREG_ROWS:
    print('dropped constant registers published before %d indirect call(s) '
          '(CPORT_DREG=%s)' % (_ndreg, _DREG_OK or 'OFF'))

out.append(src)

# (the map itself is derived further up, before w68.h is written)
_vidn = collections.Counter()
_vidtail = []
def _vidlit(m):
    v = int(m.group(1), 16)
    for lo, hi, base, nm in _VIDMAP:
        if lo <= v < hi:
            _vidn[nm] += 1
            return '0x%08Xu' % (base + (v - lo))
        # a literal in the REGION but past the shadow: report, do not move
        if lo <= v < lo + 0x10000 and v >= hi:
            _vidtail.append((v, nm))
            return m.group(0)
    return m.group(0)
_vidpat = re.compile(r'\b0x(4[0-4][0-9a-fA-F]{4}|84[01][0-9a-fA-F]{3})\b')
out = [_vidpat.sub(_vidlit, _l) for _l in out]

# ---- AND THE RUNTIME CONSTRUCTION OF ONE ----------------------------------------
#
#     puVar2 = (undefined2 *)CONCAT22(0x40,*p_A0);        FUN_0000258A
#
# The high word is a literal 0x40 and the low word comes from the data, so the
# address only exists at runtime and the literal pass above cannot see it. It is
# still unambiguous: CONCAT22(0x40, x) can only ever be S16 tile RAM. A68VID maps
# it (w68.h), and leaves anything past the renderer's 13-page shadow as the bare
# 68K value so it stays visible instead of aliasing page 0.
# ---- A WORK-RAM POINTER HELD IN A CELL ------------------------------------------
#
# _DAT_fffff402 is the palette-queue ring pointer. It holds a 68K WORK-RAM address
# (0xFFFFF600..0xFFFFF7FF), and two separate things were wrong with it:
#
# 1. THE DOMAIN. The port dereferenced the stored value directly, so a correct
#    0xFFFFF600 addressed SH-2 0xFFFFF600 -- the SH7604's ON-CHIP PERIPHERAL
#    REGISTER FILE (0xFFFF8000+), exactly the hazard w68.h's own comment names. This
#    only became reachable once the ring header's width was fixed: before that the
#    cell held 0x00000010 and the drain did nothing. MEASURED 2026-10-03: the width
#    fix alone took the rendered picture from 6.0% of pixels to 0.0%, because the
#    port started writing the FRT and DMAC registers every drain.
#
# 2. THE POINTER ARITHMETIC. `_DAT_fffff402 = piVar3 + 2` on an `int *` is +8 BYTES,
#    one queue entry of two longs. The cast this generator inserts to store into a
#    uint cell landed OUTSIDE the arithmetic -- `(uint)piVar3 + 2` -- so the ring
#    stepped 2 bytes instead of 8.
#
# Kept in the 68K domain in the CELL and the SH-2 domain in the LOCAL: read through
# A68NORM, store back through A68OF. That keeps the ring's own bound test --
# `if ((int *)0xfffff7ff < _DAT_fffff402)` -- comparing a 68K address against a 68K
# constant, which is what makes it correct; translating the cell instead would have
# silently broken that wrap.
_PTR68_CELLS = set(x.strip() for x in
                   (os.getenv('CPORT_PTR68') or '_DAT_fffff402').split(',') if x.strip())
_ptr68n = [0, 0]
def _ptr68_fix(l):
    for _c in _PTR68_CELLS:
        if _c not in l:
            continue
        # STORE: CELL = <ptr local> <op> N;   ->   CELL = A68OF(<ptr> <op> N);
        # The cast this generator later inserts to store into a uint cell lands
        # OUTSIDE the arithmetic, so match the pre-cast form (cport_build.sh adds
        # the casts after this pass runs -- its stdout is discarded, which is why
        # a pass written against the cast form fires zero times and says nothing).
        def _st(m):
            _ptr68n[0] += 1
            return '%s = A68OF(%s %s %s);' % (_c, m.group(1), m.group(2), m.group(3))
        l = re.sub(r'\b' + re.escape(_c) + r'\s*=\s*([A-Za-z_]\w*)\s*([+-])\s*(-?0x[0-9a-f]+|-?\d+)\s*;',
                   _st, l)
        # LOAD: <ptr local> = CELL;   ->   = (void *)A68NORM(CELL);
        def _ld(m):
            _ptr68n[1] += 1
            return '%s = (void *)A68NORM(%s);' % (m.group(1), _c)
        l = re.sub(r'\b([A-Za-z_]\w*)\s*=\s*' + re.escape(_c) + r'\s*;', _ld, l)
    return l
out = [_ptr68_fix(_l) for _l in out]
_pass_report('68K pointer cells, stores via A68OF', _ptr68n[0])
_pass_report('68K pointer cells, loads via A68NORM', _ptr68n[1])

# ---- MULTI-FRAME wait_frame_flag CALLS ------------------------------------------
#
# wait_frame_flag ends in `dbf %d0,0x397E`: one call waits D0+1 frames, and the
# decompiler lost D0. Derived from prog68k.bin rather than guessed -- of the 34
# `jsr 0x397E` sites, 32 are preceded by `moveq #0,%d0` and exactly two are not:
#
#     jsr @0x000BA0   303C 00F0   move.w #240,%d0   -> 241 frames   marker 0xBA6
#     jsr @0x000D02   303C 0078   move.w #120,%d0   -> 121 frames   marker 0xD08
#
# Identified by the game's own breadcrumb: it writes `_DAT_fffffefe = <return
# address>` immediately before each call, and the return address is the jsr + 6.
# Both markers are confirmed in the corpus directly above a wait_frame_flag() call,
# so the rewrite is anchored on a value the PROGRAM writes, not on line numbers.
_WAITN = {0xBA6: 241, 0xD08: 121}
_waitn_hits = [0]
# STATELESS, ON THE WHOLE TEXT. `out` is a list of CHUNKS, not lines -- the entire
# function corpus is a single element with embedded newlines -- so a pass that
# remembers "the previous line" never fires on it. (Found by dumping len(out): 52.
# Third stage-mismatch of the session, hence the counter printed to stderr.)
# Anchored on the game's own breadcrumb immediately before the call. The call is
# still spelled wait_frame_flag() HERE -- CPORT_STUBS redirects it with a #define
# at compile time, not by rewriting this source.
def _waitn_all(txt):
    for _mk, _n in _WAITN.items():
        txt, k = re.subn(r'(0x%x\s*\)*\s*;\s*)wait_frame_flag\(\)\s*;' % _mk,
                         r'\1port_frame_wait_n(%du);' % _n, txt)
        _waitn_hits[0] += k
    return txt
out = [_waitn_all(_c) for _c in out]
_pass_report('multi-frame waits rewritten (4 = both copies of the duplicated loop)', _waitn_hits[0], 4)

# ---- CPORT_AFTER: A CALL INSERTED RIGHT AFTER A MARKED CALL SITE (2026-10-30) -------
#
# CPORT_AFTER='574=port_region_boot,...' -- MARKER=hook. The marker is the game's own
# breadcrumb (`_DAT_fffffefc = <jsr + 6>`), so the site is anchored on a value the
# program writes, as _WAITN is. The hook runs after the call that follows the marker.
# For a regional variant that adds a step the reference program lacks (altbeastj's boot
# warning, games/altbeast/glue.c port_region_boot): the decompile stays the reference's
# and the glue decides at runtime.
_after = []
for _x in (os.getenv('CPORT_AFTER') or '').split(','):
    _mk, _, _fn = _x.strip().partition('=')
    if _fn:
        _after.append((int(_mk, 16), _fn.strip()))
for _mk, _fn in _after:
    _k = [0]
    def _ins(txt, _mk=_mk, _fn=_fn):
        txt, k = re.subn(r'(_DAT_fffffefc = 0x%x\s*;\s*\n(\s*)[A-Za-z_]\w*\([^;\n]*\)\s*;)' % _mk,
                         r'\1\n\2%s();' % _fn, txt)
        _k[0] += k
        return txt
    out = [_ins(_c) for _c in out]
    _pass_report('CPORT_AFTER %s after marker 0x%x' % (_fn, _mk), _k[0], 1)

# ---- THE LOAD BLANK, PACED TO THE ARCADE (the builder, 2026-10-17) ---------------
#
# The game blanks the display across a scene load by clearing bit 5 of its own
# VIDEO_EN shadow 0xFFF018, runs the clears and the scene setup, then sets the bit.
# MEASURED against the arc6k corpus: the arcade's load blanks for 21 frames
# (title->demo black 444-464, eye->demo 2 black 1478-1498) and ours for 4, because
# the loop is WORK-paced and the work runs ~5x faster on the SH-2. face->eye blanks
# 4 on both and is not a load.
#
# SELF-IDENTIFYING RATHER THAN A LINE TABLE. There are 21 blank-off sites and the
# game's own breadcrumb disambiguates only three of them, so a site table would be
# fragile. Instead clear_spriteram_and_pool marks the blank as a LOAD, and only a
# marked blank is paced -- which leaves the no-clear and partial-clear blanks, and
# therefore face->eye, exactly as they are.
#
# port_blank_off BLOCKS (port_frame_wait from inside the load's own call stack) and
# does not merely hold the display, so the game's scene timer 0xFFF02A parks through
# the load as the arcade's does. That is the gate, not the blank length.
_blank_hits = [0, 0, 0]
_blank_site = [0]   # blank-OFF sites are numbered in source order; the index is the table key
def _blankpace(txt):
    txt, k = re.subn(r'DAT_fffff018 = DAT_fffff018 & 0xdf;',
                     'DAT_fffff018 = port_blank_on(DAT_fffff018);', txt)
    _blank_hits[0] += k
    def _off(_m):
        _i = _blank_site[0]; _blank_site[0] += 1
        return 'DAT_fffff018 = port_blank_off(DAT_fffff018, %du);' % _i
    txt, k = re.subn(r'DAT_fffff018 = DAT_fffff018 \| 0x20;', _off, txt)
    _blank_hits[1] += k
    txt, k = re.subn(r'(\buint clear_spriteram_and_pool\(\)\s*\n\{\n)',
                     r'\1  port_blank_work();\n', txt)
    _blank_hits[2] += k
    return txt
out = [_blankpace(_c) for _c in out]
_pass_report('load blank: blank-ON sites hooked', _blank_hits[0])
_pass_report('load blank: blank-OFF sites hooked', _blank_hits[1])
_pass_report('load blank: clear_spriteram_and_pool marked', _blank_hits[2], 1)

# ---- PROTECTED-BUT-ADDRESS-TAKEN 32-BIT GLOBALS (the builder, 2026-10-18) --------
#
# The misaligned-globals pass above gives every 2-mod-4 32-bit symbol a packed alias
# and a macro, so a NAMED read emits byte-wise code. IT DOES NOT SURVIVE &. Taking
# the address of a packed member yields a plain `uint *`, gcc forgets the packing,
# and the dereference is a `mov.l` again -- which faults or, on ares, silently reads
# the 4-ALIGNED LONGWORD BELOW.
#
# MEASURED, and this is how it was found: FUN_00004540 does `puVar3 = &DAT_000046e2;`
# and copies 20 longwords to palette entry 8. sh-elf-objdump on the shipped function
# showed `mov.l @r3+` with r3 = 0x023446E2. The high-score palette therefore loaded
# one WORD early -- entry 8 held ROM[0x46E0] = 0x9801 and entries 9..47 held
# ROM[0x46E2..] -- which is the garbled scoreboard header Mike reported. The symbol
# was protected the whole time; the protection was lost at the `&`.
#
# TWO SITES, swept against the symbols the pass actually protects rather than against
# an address rule: &DAT_000046e2 (FUN_00004540) and &DAT_0001c302 (FUN_0001a52c).
# The second propagates through a second pointer (`puVar6 = puVar5`), so the taint
# follows plain pointer copies inside the function. Only tainted READS are wrapped;
# the destination pointer of a copy is untouched.
_una_names = set(re.findall(r'extern _datu32 _una_(\w+) asm', ''.join(lines)))
_unap = [0, 0]
def _una_addr_taken(txt):
    out_fn = []
    for _fn in re.split(r'(?m)(?=^[a-zA-Z_][\w ]*\**\w+\([^;]*\)\s*$)', txt):
        _t = set(re.findall(r'(\w+)\s*=\s*&(\w+)\s*;', _fn))
        _tainted = {v for v, sym in _t if sym in _una_names}
        if _tainted:
            for _ in range(3):          # follow plain copies, bounded
                for v, src in re.findall(r'(\w+)\s*=\s*(\w+)\s*;', _fn):
                    if src in _tainted:
                        _tainted.add(v)
            # SKIP DECLARATIONS. `undefined4 *puVar3;` matches "*var not followed by
            # ="; rewriting it produced `undefined4 LD32U(puVar3);`, a function
            # declaration, and the loop below then compiled against an implicit int
            # and still emitted mov.l. Caught by disassembling the result rather
            # than by trusting the pass's own count (2026-10-18).
            _decl = re.compile(r'^\s*(?:undefined\d*|uint|int|short|long|byte|char|'
                               r'ushort|ulong|bool)\s+\*')
            _ls = _fn.split('\n')
            for _i, _l in enumerate(_ls):
                if _decl.match(_l):
                    continue
                for v in sorted(_tainted):
                    _l, k = re.subn(r'(?<![\w.>])\*%s\b(?!\s*=)' % re.escape(v),
                                    'LD32U(%s)' % v, _l)
                    _unap[1] += k
                _ls[_i] = _l
            _fn = '\n'.join(_ls)
            _unap[0] += 1
        out_fn.append(_fn)
    return ''.join(out_fn)
out = [_una_addr_taken(_c) for _c in out]
_pass_report('unaligned globals whose address is taken: functions fixed', _unap[0], 2)
_pass_report('unaligned globals whose address is taken: reads routed via LD32U', _unap[1])

# ---- THE VBL IRQ'S FAKE-RETURN REDIRECTS (the builder, 2026-10-19) ---------------
#
# The 68K VBL handler sometimes does not return to the interrupted code: it builds a
# fake exception frame and rte's elsewhere, abandoning the game thread. The idiom is
# `pea target.l ; move.w #$2300,-(sp) ; rte`. Traced on the arcade in MAME (advisor,
# handler tail 0x2C1A-0x2DBC):
#     0x2C98  pea 0x1E4E   a credit landed while a demo runs  -> the title
#     0x2D76  pea 0x05BE   START while a demo runs            -> main_entry, the GAME
#     0x2D9C  pea 0x1AFDE  service / test       NOT WIRED
#     0x2DB0  pea 0x0400   reset                NOT WIRED
# GHIDRA DROPS THE REDIRECT AND KEEPS THE STATE WRITES, so each became a plain
# `return` -- f026/f028/f000 were updated and the game thread then resumed the demo
# it was already in. That is why a coin never cut the attract and why START only kept
# the demo playing.
#
# ANCHORED ON THE PROGRAM'S OWN STATE WRITES, not on line numbers: each site has a
# distinctive tail, and _pass_report fails loudly if a count changes.
#
# THE TWO SERVICE/RESET SITES ARE DELIBERATELY NOT MARKED. Neither is on a gate and a
# wrong target there hangs the attract; they stay exactly as they are today.
_IRQRED = (
    # (regex on the site's tail, the 68K target, a name for the report)
    (r'(DAT_fffff00e = 0;\s*\n\s*)return uVar12 & 0xffffffff;',      0x05BE, 'START -> main_entry'),
    (r'(\(DAT_fffff026 & 1\) != 0\)\) \{\s*\n\s*)return CONCAT44\(CONCAT31\(uVar3,~DAT_fffff0c2\),uVar4\);',
                                                                      0x1E4E, 'credit -> title'),
)
_irqred_hits = [0]
def _irq_redirects(txt):
    for _rx, _tgt, _nm in _IRQRED:
        txt, k = re.subn(_rx, lambda m, t=_tgt: '%sport_irq_redirect(0x%Xu);\n        return 0;' % (m.group(1), t), txt)
        _irqred_hits[0] += k
    return txt
out = [_irq_redirects(_c) for _c in out]
_pass_report('VBL IRQ fake-return redirects wired (START, credit)', _irqred_hits[0], 2)

# ---- LD32S VALUES THAT ARE DEREFERENCED ARE 68K ADDRESSES (the builder, 2026-10-19) --
#
# LD32S reads a 32-bit field and returns it AS STORED -- a 68K address. Dereferencing
# that raw on the SH-2 reads the wrong memory entirely. Of the 66 LD32S sites exactly
# one was wrapped in A68PTR by hand; the other eight that dereference were not.
#
# WHAT IT COST, root-caused by the advisor 2026-10-19 against MAME and Mike's own
# savestate: the intro child places the player with
# `movea.l 108(fp),a3 ; move.w #0x10D8,68(a3)`, which the generator rendered as
#     *(undefined2 *)(LD32S((unaff_A6 + 0x6c)) + 0x44) = 0x10d8;
# LD32S returns 0xFFFFC000, so the store went to SH-2 0xFFFFC044 and the player's X
# never became 0x10D8. It stayed 0x1200, the draw test in 0x49BA never passed, the draw
# bit (bit 3, set at 0x3EB2) never set, and THE PLAYER WAS INVISIBLE all through
# gameplay. Four more sites write the HUD through LD32S((p_A5 + 8)) + 0x100/0x81/0x77 --
# a raw 0x41xxxx is CART space on the SH-2, not the port's text RAM -- which is the
# missing lives, score and credits rows.
#
# ONLY DEREFERENCE CONTEXTS ARE WRAPPED. A68PTR is domain-guarded and would pass an
# ordinary integer through, but a DATA value that happened to land in work-RAM or ROM
# range would be mapped and silently corrupted, so the pass requires the `*(T *)(LD32S(`
# shape and leaves every other LD32S alone. Paren-matched rather than regex-closed,
# because LD32S's own argument contains parentheses.
_ld32s_hits = [0]
def _ld32s_deref(txt):
    """Wrap LD32S(...) in A68PTR wherever its value is used as a BASE ADDRESS, which at
    this stage of the pipeline always shows as `LD32S(..) + <offset>`. Earlier passes
    reshape the surrounding syntax -- the value turns up inside a cast, as a term in a
    sum, or assigned to a local and used later -- so anchoring on `*(T *)(LD32S(` finds
    only three of the eight. The `+ offset` is what every real site has in common."""
    out_s, i = [], 0
    while True:
        j = txt.find('LD32S(', i)
        if j < 0:
            out_s.append(txt[i:]); break
        k = txt.index('(', j); depth = 0
        while True:
            if txt[k] == '(': depth += 1
            elif txt[k] == ')':
                depth -= 1
                if depth == 0: break
            k += 1
        tail = txt[k+1:k+9]
        if re.match(r'\s*\+\s*(0x[0-9a-fA-F]+|\d)', tail) and not txt[max(0,j-8):j].rstrip().endswith('A68PTR('):
            out_s.append(txt[i:j]); out_s.append('A68PTR(' + txt[j:k+1] + ')')
            _ld32s_hits[0] += 1
        else:
            out_s.append(txt[i:k+1])
        i = k + 1
    return ''.join(out_s)
out = [_ld32s_deref(_c) for _c in out]
_pass_report('LD32S used as a base address, wrapped in A68PTR', _ld32s_hits[0])

# ---- VIDEO POINTERS HELD IN WORK RAM --------------------------------------------
# `*_DAT_fffff0ec = _DAT_fffff0f4;` writes a tilemap PAGE register through a pointer
# the game keeps in work RAM. Whose domain that is depends on who wrote it: the
# port's own boot stores an SH-2 address, a work-RAM seed restores the arcade's 68K
# one. A68VIDA decides at runtime and is the identity for anything already mapped.
_PTRVID = ('_DAT_fffff0ec', '_DAT_fffff0f0')
_ptrvid_n = [0]
def _ptrvid_fix(txt):
    for _c in _PTRVID:
        txt, k = re.subn(r'\*\s*' + re.escape(_c) + r'\s*=',
                         '*(ushort *)A68VIDA(%s) =' % _c, txt)
        _ptrvid_n[0] += k
    return txt
out = [_ptrvid_fix(_c) for _c in out]
_pass_report('video pointers mapped at deref', _ptrvid_n[0], 2)

# ---- POINTER PARAMETERS OF THE ARCADE'S VIDEO WRITERS ----------------------------
#
# MEASURED 2026-10-04 on the arcade (tools/video_writers.lua: MAME watchpoints on the
# four video ranges, ~1 minute of attract; tools/video_writers_hist.py): the bulk
# stores come from a dozen functions, and the ones that take their destination or
# source in an ADDRESS REGISTER get it from the caller -- FUN_00002622 (18,808
# palette writes), FUN_00003972 (7,168), textram_stride_write (26,157), FUN_00001732
# (3,200), tilemap_unpack_hi/lo (40,960 each). The port's rd3 build landed 25 of
# 2,048 palette words and 2,359 of 26,624 tilemap words against the line's 1,187 and
# 19,520: the parameters arrived in the 68K domain and the stores went nowhere.
# A68PTR is the identity for an SH-2 pointer, so normalising every pointer parameter
# of a listed function at entry is exact whichever caller reached it.
_PARAMNORM = [x.strip() for x in (os.getenv('CPORT_PARAMNORM') or '').split(',') if x.strip()]
_pn_fns = [0]
def _paramnorm(txt):
    for _fn in _PARAMNORM:
        def _sub(m):
            params = m.group(2)
            lines = []
            for pm in re.finditer(r'((?:\w+\s*\*+\s*)|(?:\bint\s+))(p_A\d)\b', params):
                ty = pm.group(1).strip()
                lines.append('  %s = (%s)A68PTR(%s);' % (pm.group(2), ty, pm.group(2)))
            if lines:
                _pn_fns[0] += 1
            return m.group(1) + params + m.group(3) + '\n'.join(lines) + ('\n' if lines else '')
        txt = re.sub(r'(\b(?:uint|ulonglong|int|void)\s+%s\s*\()([^)]*)(\)\s*\n\{\n)' % re.escape(_fn), _sub, txt, count=1)   # any return type (FUN_0000135e is ulonglong)
    return txt
out = [_paramnorm(_c) for _c in out]
_pass_report('video-writer pointer parameters normalised (functions)', _pn_fns[0], len(_PARAMNORM))

# ---- THE TILEMAP UNPACKER CHAIN -----------------------------------------------------
# prog68k.bin 0x16D6-0x16DC: `moveal (a1)+,a1; bsr 0x16E8; bsr 0x1708` -- unpack_hi
# consumes the compressed bytes through A1 and unpack_lo CONTINUES FROM THE SAME A1.
# The decompiler lost that: unpack_level_tilemap passes an uninitialised local to
# tilemap_unpack_lo. unpack_hi returns its final pointer and the caller threads it.
_unp = [0, 0]
def _unpack_chain(txt):
    # the argument's spelling changes with the width/cast passes, so it is not matched
    # (stage text, dumped 2026-10-04: `tilemap_unpack_lo(p_A1);` -- the (byte *) cast is
    # added by the later cast pass, so it is optional here)
    txt, k = re.subn(r'(?<!uint )tilemap_unpack_hi\((.*?)\);\n(\s*)tilemap_unpack_lo\((?:\(byte \*\))?\(?p_A1\)?\);',
                     r'p_A1 = tilemap_unpack_hi(\1);\n\2tilemap_unpack_lo((byte *)(p_A1));', txt)
    _unp[0] += k
    if not k:
        for _nm in ('tilemap_unpack_lo(', 'FUN_000016de('):
            _i = txt.find(_nm)
            if _i >= 0:
                sys.stderr.write('cport_prep: unpack-chain stage text: %r\n' % txt[max(0,_i-220):_i+60])
    m = re.search(r'uint tilemap_unpack_hi\([^)]*\)\s*\n\{.*?\n\}', txt, re.S)
    if m:
        body, k2 = re.subn(r'return 0;', 'return (uint)p_A1;', m.group(0))
        _unp[1] += k2
        txt = txt[:m.start()] + body + txt[m.end():]
    return txt
out = [_unpack_chain(_c) for _c in out]
_pass_report('tilemap unpacker chain: caller threads A1', _unp[0], 1)
_pass_report('tilemap unpacker chain: unpack_hi returns A1', _unp[1], 1)

# ---- THE PALETTE QUEUE ----------------------------------------------------------------
# palette_queue_drain pops (destination, source) address pairs the game queued as 68K
# longs in work RAM: the destination is palette RAM (0x840xxx), the source ROM or work
# RAM. Both were dereferenced raw (1,514 + 462 arcade writes a minute lost).
_pq = [0, 0]
def _palq(txt):
    # THE DESTINATION WRAPS INTO THE 4 KB PALETTE: the arcade's queue targets 0x841002 +
    # set*32 for sets 64-95 (MAME wp census: palette_queue_drain wrote 'sets 128-159'),
    # which the S16B decodes modulo 0x1000 as sets 0-31 -- the text/HUD palettes. The
    # producer's literal is already an SH-2 address, so the wrap is applied to the
    # pointer: PAL_SH base + ((p - base) & 0xFFF). MEASURED 2026-10-04 (rd18 f2200):
    # PAL_SH sets 0-2 held the round table's words where the arcade had the queue's.
    txt, k1 = re.subn(r'puVar1 = \(undefined4 \*\)\*piVar3;', 'puVar1 = (undefined4 *)(0x06027000u + ((A68VIDA(*piVar3) - 0x06027000u) & 0xFFFu));', txt)
    txt, k2 = re.subn(r'puVar2 = \(undefined4 \*\)piVar3\[1\];', 'puVar2 = (undefined4 *)A68ANY(piVar3[1]);', txt)
    _pq[0] += k1; _pq[1] += k2
    return txt
out = [_palq(_c) for _c in out]
_pass_report('palette queue: destination via A68VIDA', _pq[0], 1)
_pass_report('palette queue: source via A68ANY', _pq[1], 1)

# The queued destination is 0x840802 + set*32 (colour 1 of a sprite set): word-aligned
# for the 68000, NOT long-aligned for the SH-2, so the seven `puVar1[k] = puVar2[k]`
# longs must go through the byte-wise ST32/LD32U pair the width passes already use.
# MEASURED 2026-10-04 (rd9): PAL_SH sets 64-79 absent, the queue's own producer
# stores 0x06027802u + idx*32 as the destination, the drain ran 1,654 times.
_pq2 = [0]
def _palq_align(txt):
    m = re.search(r'uint palette_queue_drain\(\)\s*\n\{.*?\n\}', txt, re.S)
    if not m:
        return txt
    body, k = re.subn(r'puVar1\[(\d)\] = puVar2\[(\d)\];', r'ST32((puVar1 + \1), LD32U((puVar2 + \2)));', m.group(0))
    body, k2 = re.subn(r'\*puVar1 = \*puVar2;', 'ST32((puVar1), LD32U((puVar2)));', body)
    _pq2[0] += k + k2
    return txt[:m.start()] + body + txt[m.end():]
out = [_palq_align(_c) for _c in out]
_pass_report('palette queue: 7 longs copied byte-wise (dest 0x...802)', _pq2[0], 7)

if os.getenv('CPORT_PQ_PROBE'):
    # log the drain's resolved destination and source pointers (ids 0/1)
    _pqp = [0]
    def _pq_probe(txt):
        txt, k = re.subn(r'(puVar2 = \(undefined4 \*\)A68ANY\(piVar3\[1\]\);\n)', r'\1      PORT_ARGLOG(0, (uint)puVar1); PORT_ARGLOG(1, (uint)puVar2);\n', txt)
        _pqp[0] += k
        return txt
    out = [_pq_probe(_c) for _c in out]
    _pass_report('PQ probe inserted', _pqp[0], 1)

# ---- clear_tileram WALKS 64 KB; THE PORT'S TILE RAM IS 13 PAGES --------------------
# prog68k.bin 0x36B0: `lea 0x400000,%a0; move.w #0x3FFF,%d0; clr.l (a0)+; dbf` -- the
# arcade clears all 16 pages of tile RAM (64 KB). The port maps 0x400000 onto a
# 13-page shadow (TILEMAP_U, 0xD000 bytes, what the renderer covers) and packs the
# text, palette and sprite shadows right behind it, so the last 0x3000 bytes of the
# clear land on TEXT_U, PAL_SH and SPR_SNAP. MEASURED 2026-10-04 (rd5): PAL_SH held
# 193 colour words at frame 300 and 1 at frame 600, across the round transition that
# calls clear_tileram; every palette writer had run (CPORT_COUNT). Pages 13-15 are
# not in the renderer's shadow, so stopping the clear at page 13 loses nothing the
# port can show. 13 pages * 0x1000 B / 4 = 0x3400 longs, dbf count 0x33FF.
_ctr = [0]
def _clear_tileram(txt):
    m = re.search(r'uint clear_tileram\(\)\s*\n\{.*?\n\}', txt, re.S)
    if not m:
        return txt
    body, k = re.subn(r'sVar1 = 0x3fff;', 'sVar1 = 0x33ff;   /* 13 pages: the port\'s shadow, see cport_prep.py */', m.group(0))
    _ctr[0] += k
    return txt[:m.start()] + body + txt[m.end():]
out = [_clear_tileram(_c) for _c in out]
_pass_report('clear_tileram capped at the 13-page shadow', _ctr[0], 1)

# ---- A BYTE COPY RENDERED AS A SHIFTED WORD ------------------------------------------
# prog68k.bin 0x2C00: `moveb 0xfffff148,0xfffff149` -- the fade engine copies the
# brightness level to the target. Ghidra: `DAT_fffff149 = DAT_fffff148 >> 8;` which is
# always 0 for a byte, so once the level is non-zero the "fade to black" branch above
# it would repeat every vint forever. Found 2026-10-04 while reading the fade engine.
_b8 = [0]
def _byte_copy_fix(txt):
    txt, k = re.subn(r'DAT_fffff149 = DAT_fffff148 >> 8;', 'DAT_fffff149 = DAT_fffff148;   /* moveb, not a shifted word: cport_prep.py */', txt)
    _b8[0] += k
    return txt
out = [_byte_copy_fix(_c) for _c in out]
_pass_report('fade level byte copy (0x2C00 moveb) restored', _b8[0], 1)

# ---- THE CREDIT WRITER'S A0 ----------------------------------------------------------
# prog68k.bin 0x3AD8-0x3AFA (FUN_00003aae): `lea 0x4128(pc),a0; moveq #5,d0; bsr 3a9a`
# copies "CREDIT" and leaves A0 at the 'S'; `addq #1,a0` skips it, `subq #1,a0` restores
# it when credits >= 2, then one more 1-char copy and the digit. Ghidra captured the
# helper's A1 return as A0 (`extraout_A0 = p_A1_00`) and left `p_A0` uninitialised, so
# the port wrote 0xFF 0xFF for "S " (rd37 TEXT_U row 26: 0043 0052 0045 0044 0049 0054
# 00ff 00ff 0037 vs the arcade's ... 0053 0020 0037). A0 after a stride copy of n+1
# chars is src + n + 1; the helper returns only A1.
_cr = [0, 0]
def _credit_a0(txt):
    m = re.search(r'uint FUN_00003aae\(\)\s*\n\{.*?\n\}', txt, re.S)
    if not m:
        return txt
    body = m.group(0)
    body, k1 = re.subn(r'extraout_A0 = (?:\(uint\))?p_A1_00;',
                       'p_A0 = (int)(s_CREDITS_FREE_PLAY_00004128 + 6);   /* A0 after the 6-char copy, prog68k 0x3ADE: cport_prep.py */', body)
    body, k2 = re.subn(r'p_A0_00 = extraout_A0;', 'p_A0_00 = p_A0 + 1;   /* the 1-char copy advanced A0: cport_prep.py */', body)
    _cr[0] += k1; _cr[1] += k2
    return txt[:m.start()] + body + txt[m.end():]
out = [_credit_a0(_c) for _c in out]
_pass_report('credit writer: A0 after the CREDIT copy', _cr[0], 1)
_pass_report('credit writer: A0 after the S copy', _cr[1], 1)

# ---- THE DEMO SCRIPT POINTER ---------------------------------------------------------
# The input reader (prog68k 0x2C6A region, `DAT_fffff026 & 1` = attract playback) takes
# the P1/P2 bytes from a ROM script: PTR_DAT_00001834[(state & 0x18) >> 1] + frame * 3.
# Ghidra left the loaded 68K address raw -- `(byte *)(counter * 3 + LD32S(...))` -- so
# the SH-2 read bytes at 0x0000xxxx instead of the cart, 0xFFE01E came out 0xFF (every
# button held: measured rd40 f500, record 0xFFE000 +1e/+20 = ff/ff vs the arcade's
# 00/00), the demo hero jumped in place (state 9, +0x1a = 0xFB80) and never walked to
# the enemy spawns, the spirit balls, the morph (0x9072) or the text clear.
_ds = [0]
def _demo_script_ptr(txt):
    txt, k = re.subn(r'\(byte \*\)\(\(uint\)_DAT_fffff02a \* 3 \+\s*(LD32S\(\(\(int\)&PTR_DAT_00001834.*?\)\))\)',
                     r'(byte *)P68((uint)_DAT_fffff02a * 3 + \1)   /* ROM script, P68: cport_prep.py */', txt, flags=re.S)
    _ds[0] += k
    return txt
out = [_demo_script_ptr(_c) for _c in out]
_pass_report('demo script pointer through P68', _ds[0], 1)

# ---- THE SCRIPTED-SPAWN DISPATCH: A CARRY GHIDRA READ FROM THE WRONG INSTRUCTION ------
# prog68k 0xE0AC-0xE0D2 (FUN_0000e052): `bsr e0f4; bcc e0da; ... lea 0x1d2ea,a1; jsr (a1)`.
# FUN_0000e0f4 returns ONLY on success (`move #1,CCR` at 0xE164 sets the carry); with
# every slot full it jumps into the caller's own exit (0xE0E0) and never comes back.
# Ghidra attributed the `bcc` to the last flag-setting instruction it could see --
# the kind-0 branch's `tas` (bVar7 = false) or the `addqb` carry -- so for every
# kind-0 record (the demo director, 0xEE0A) the port skipped the dispatch, and on the
# other kinds it dispatched through `iVar5 = FUN_0000e0f4(...)`, whose FAILURE path
# returned a stale extraout_A0 (rd41 unknown-handler census: 0x00FE0001, 0x00FD0001,
# 0 -- reads off a garbage record pointer). Measured 2026-10-04: the arcade installs
# 0xEE80 into slot 16 at f513 (PC 0xEE02 via 0xE0C6); the port's slot 16 held the
# position fields FUN_0000e0f4 writes and a zero handler (rd41 f650).
_sd = [0, 0]
def _spawn_dispatch(txt):
    m = re.search(r'uint FUN_0000e0f4\([^)]*\)\s*\n\{.*?\n\}', txt, re.S)
    if m:
        body, k = re.subn(r'FUN_0000e16a\(p_D1\);\n  return extraout_A0;',
                          'FUN_0000e16a(p_D1);\n  return 0;   /* no free slot: the 68K never returns here, it unwinds into 0xE0E0: cport_prep.py */', m.group(0))
        _sd[0] += k
        txt = txt[:m.start()] + body + txt[m.end():]
    m = re.search(r'uint FUN_0000e052\([^)]*\)\s*\n\{.*?\n\}', txt, re.S)
    if m:
        body, k = re.subn(r'if \(!bVar7\) \{\n(\s+)p_D4_00 = \(uint\)\(ushort\)\(sVar4 \+ 8\);\n(\s+)goto LAB_0000e0e0;\n(\s+)\}\n(\s+)port_call68_at\((\d+),',
                          r'if (iVar5 == 0) {   /* FUN_0000e0f4 found no slot: cport_prep.py */\n\1p_D4_00 = (uint)(ushort)sVar4;\n\2goto LAB_0000e0e0;\n\3}\n\4port_call68_at(\5,', m.group(0))
        _sd[1] += k
        txt = txt[:m.start()] + body + txt[m.end():]
    return txt
out = [_spawn_dispatch(_c) for _c in out]
_pass_report('scripted spawn: allocator failure returns 0', _sd[0], 1)
_pass_report('scripted spawn: dispatch gated on the allocator, not a stray carry', _sd[1], 1)

# ---- THE COLOUR-CYCLE STREAMER READS ITS TABLE LONGS MISALIGNED ----------------------
# prog68k 0x30F2-0x30FE: `move.w (a0)+,d0` then four `move.l (a0)+,(a1)+` -- the entry
# is a word followed by four longs, so the longs sit at table + 18k + 4: 2 mod 4 for
# half the entries. Ghidra: `*(undefined4 *)(puVar4 + 1)` on a short pointer; the SH-2
# masks the low bits of a longword load and reads the long one word EARLY. Measured
# rd41 f1192 vs arcade f1200: palette set 20 = 0004 0a00 100f ... where the arcade's is
# 0a00 100f 100f ... (its 0004 is the entry's leading word), set 21 word 6 = 305f (the
# spill). LD32U is the unaligned loader every other table read already uses.
_cc = [0]
def _streamer_ld32(txt):
    m = re.search(r'uint colour_cycle_streamer\([^)]*\)\s*\n\{.*?\n\}', txt, re.S)
    if not m:
        return txt
    body, k = re.subn(r'\(\*\(undefined4 \*\)\(puVar4 \+ ([1357])\)\)', r'LD32U((puVar4 + \1))', m.group(0))
    _cc[0] += k
    return txt[:m.start()] + body + txt[m.end():]
out = [_streamer_ld32(_c) for _c in out]
_pass_report('colour_cycle_streamer: table longs through LD32U', _cc[0], 4)

# 0xDA74 copies the petrified-enemy template selected by 0xD9F6. All five
# templates in the 0xDE22 pointer table are word-aligned, not long-aligned.
# Typed pointer loads escaped _unaligned32: SH-2 reads the preceding word,
# installing e.g. 0xDE860000 instead of handler 0xDABC. The inert records
# accumulate and never process the second hit that should shatter them.
_stone = [0]
def _stone_template_longs(txt):
    m = re.search(r'uint FUN_0000d9f6\([^)]*\)\s*\n\{.*?\n\}', txt, re.S)
    if not m:
        return txt
    body, k = re.subn(r'\(\*puVar1\)', 'LD32U(puVar1)', m.group(0))
    body, j = re.subn(r'\(puVar1\[([23])\]\)', r'LD32U(puVar1 + \1)', body)
    _stone[0] += k + j
    return txt[:m.start()] + body + txt[m.end():]
out = [_stone_template_longs(_c) for _c in out]
_pass_report('petrified-enemy template: longs through LD32U', _stone[0], 3)

# ---- THE SPAWN INSTALLERS (2026-10-04 re-export): ROM templates and the found slot ---
# (1) FUN_0000edf0 copies a 12-byte template from ROM 0xF56A into the new record:
#     `puVar4 = (undefined4 *)0xf56a` is a raw 68K address (the FUN_00004178 class) and
#     0xF56A is 2 mod 4, so the longs go through LD32U. FUN_00010cd6 has the same shape
#     at 0x113D4. (rd43 f650: slot 16 allocated, handler 0 -- the copy read SH-2 0xF56A.)
# (2) prog68k 0xE0F4 walks A6 to the free slot and the installer at 0xE0C6 runs with
#     THAT A6; the generated caller saves/restores _r_unaff_A6 around the allocator (the
#     a6_callsites wrapper), so the installer wrote into the caller's record instead.
#     FUN_0000e0f4 publishes the slot it found in port_spawn_a6 (port_l2.c); the
#     dispatch runs with it and tests ITS flags word (0xE0C8 `tst.w (A6)`), not the
#     constant 0xFFC800 Ghidra propagated.
_si = [0, 0, 0, 0, 0]
def _spawn_installers(txt):
    m = re.search(r'uint FUN_0000edf0\([^)]*\)\s*\n\{.*?\n\}', txt, re.S)
    if m:
        body, k1 = re.subn(r'puVar4 = \(undefined4 \*\)0xf56a;', 'puVar4 = (undefined4 *)((char *)ROM_BASE + 0xf56a);   /* ROM template: cport_prep.py */', m.group(0))
        body, k2 = re.subn(r'\*puVar3 = \*puVar4;', 'ST32(puVar3, LD32U(puVar4));   /* 0xF56A is 2 mod 4: cport_prep.py */', body)
        _si[0] += k1; _si[1] += k2
        txt = txt[:m.start()] + body + txt[m.end():]
    m = re.search(r'uint FUN_00010cd6\([^)]*\)\s*\n\{.*?\n\}', txt, re.S)
    if m:
        body, k1 = re.subn(r'puVar10 = \(undefined4 \*\)0x113d4;', 'puVar10 = (undefined4 *)((char *)ROM_BASE + 0x113d4);   /* ROM template: cport_prep.py */', m.group(0))
        body, k2 = re.subn(r'\*\(undefined4 \*\)(pbVar\d+) = \*puVar10;', r'ST32(\1, LD32U(puVar10));   /* 0x113D4 template: cport_prep.py */', body)
        _si[2] += k1 + (1 if k2 else 0)
        txt = txt[:m.start()] + body + txt[m.end():]
    m = re.search(r'uint FUN_0000e0f4\([^)]*\)\s*\n\{.*?\n\}', txt, re.S)
    if m:
        body, k = re.subn(r'(\n\s+)return p_A0;', r'\1port_spawn_a6 = (uint)unaff_A6;   /* the slot found, for the installer dispatch: cport_prep.py */\1return p_A0;', m.group(0), count=1)
        _si[3] += k
        txt = txt[:m.start()] + body + txt[m.end():]   # port_spawn_a6 is harvested into defs.c/extras.h as a uint; no extern here (rd44-46: conflicting types, NO object)
    m = re.search(r'uint FUN_0000e052\([^)]*\)\s*\n\{.*?\n\}', txt, re.S)
    if m:
        body, k = re.subn(r'(\s+)(port_call68_at\(\d+, [^\n]*\);\n)(\s+)if \(-1 < _DAT_ffffc800\) \{\n(\s+DAT_fffff154 = DAT_fffff154 (?:\+ -1|- 1);\n\s+\}\n)',
                          r'\1{ byte *_a6d = _r_unaff_A6; _r_unaff_A6 = (byte *)port_spawn_a6;   /* 68K: A6 = the new slot through the jsr: cport_prep.py */\1\2\3if (-1 < *(short *)port_spawn_a6) {\n\4\3_r_unaff_A6 = _a6d; }\n', m.group(0), count=1)
        _si[4] += k
        txt = txt[:m.start()] + body + txt[m.end():]
    return txt
out = [_spawn_installers(_c) for _c in out]
_pass_report('spawn installers: FUN_0000edf0 template through ROM_BASE', _si[0], 1)
_pass_report('spawn installers: FUN_0000edf0 copy through LD32U/ST32', _si[1], 1)
_pass_report('spawn installers: FUN_00010cd6 template through ROM_BASE (+copy)', _si[2], 2)
_pass_report('spawn installers: FUN_0000e0f4 publishes the found slot', _si[3], 1)
_pass_report('spawn installers: FUN_0000e052 dispatches with the found slot', _si[4], 1)

# ---- THE ONE-RECORD KIND INITS: THE SAME 12-BYTE ROM TEMPLATE, READ RAW (2026-10-25) ---
# Kinds 8 (0x12422 -> handler 0x124BC), 9 (0x1208E -> 0x1211A), 12 (0x16484 -> 0x1650A),
# 13 (0x13AA6 -> 0x13B2C) and 14 (0x1435C -> 0x143DC) commit the enemy count 0xFFF15D,
# then `lea tmpl(pc),a1 ; lea (a6),a0 ; moveq #2,d2 ; move.l (a1)+,(a0)+ ; dbf` copies
# 8000 <handler long> ... into their own record (prog68k 0x120A0-0x120B0, 0x12442-0x12452).
# Ghidra kept the template as `puVar10 = (undefined4 *)0x1233a;`, a raw 68K address the
# SH-2 reads as its boot ROM; 0x1233A/0x141C2 are 2 mod 4 too. The record got no handler,
# the count was never released (only the handler's despawn gives it back), and three
# spawns reach the 0xE0A0 cap of 10: level 3 (kinds 8, 9 = 93 of its 168 spawn entries)
# spawned nothing after its first seconds (Mike's sw324 pass). The same shape as the
# 0x113D4 rewrite above, for every function that has it.
_kt = [0, 0]
def _kind_templates(txt):
    def _fn(m):
        body, k1 = re.subn(r'(puVar\d+) = \(undefined4 \*\)(0x1[0-9a-f]{4});',
                           r'\1 = (undefined4 *)((char *)ROM_BASE + \2);   /* ROM template: cport_prep.py */', m.group(0))
        if not k1:
            return m.group(0)
        body, k2 = re.subn(r'\*\(undefined4 \*\)(pbVar\d+) = \*(puVar\d+);',
                           r'ST32(\1, LD32U(\2));   /* ROM template, may be 2 mod 4: cport_prep.py */', body)
        _kt[0] += k1; _kt[1] += k2
        return body
    return re.sub(r'uint FUN_000(?:12422|1208e|16484|13aa6|1435c)\([^)]*\)\s*\n\{.*?\n\}', _fn, txt, flags=re.S)
out = [_kind_templates(_c) for _c in out]
_pass_report('one-record kind inits: ROM template through ROM_BASE', _kt[0], 5)
_pass_report('one-record kind inits: template copy through LD32U/ST32', _kt[1], 5)

# ---- BOSS PARTS TEST THE BOSS's STATUS BYTE, NOT ITS WORD --------------------
# 0x18AFC, 0x18B4A, 0x18B90 (boss slots 41-43) open `btst #3,0xffffd400`: bit 3 of the
# BYTE at 0xFFD400, the high half of slot 40's status word (0x8800 = drawn). DAT_ffffd400
# is declared ushort (33 word refs), so `(DAT_ffffd400 & 8)` tested the LOW byte, read 0,
# and every part took its hide branch every frame: the snail's shell never drew and its
# 0x18D50 shots never spawned (Mike's sw324 pass: "the main boss never materializes").
# Native 19476/194A4 (level4) do the same byte BTST through A5. The
# old function allowlist missed both: the stomach and detached parts stayed
# hidden despite the head being drawn (sw335 Miyoo play pass,2026-10-28).
_bb = [0]
def _boss_status_byte(txt):
    def _fn(m):
        body, k = re.subn(r'\(DAT_ffffd400 & 8\)',
                          r'(*(volatile byte *)&DAT_ffffd400 & 8)   /* btst #3 on the BYTE 0xFFD400: cport_prep.py */',
                          m.group(0))
        _bb[0] += k
        return body
    return re.sub(r'uint FUN_000(?:18(?:afc|b4a|b90)|194(?:6c|a0))\([^)]*\)\s*\n\{.*?\n\}', _fn, txt, flags=re.S)
out = [_boss_status_byte(_c) for _c in out]
_pass_report('level-3/4 boss parts: btst #3 reads the status BYTE 0xFFD400', _bb[0], 5)

# ---- FUN_0000e032: THE PAIR-SLOT ALLOCATOR RETURNS A CARRY AND MOVES A5 -------------
# prog68k 0xE032: walk 24 records from 0xFFC800; a free one -> A5 = it, `move #1,CCR`
# (carry SET); none -> carry clear. Six installers (FUN_0000f74a, FUN_000102cc,
# FUN_00010cd6, FUN_000143dc, FUN_00014a5a x2) do `jsr e032; bcs install`, and Ghidra
# rendered each as `bVarN = false; FUN_0000e032(); if (bVarN)` -- the install branch
# was dead, the record was cleared (`clr.w (A6)`), and the zombies/wolf pairs of the
# demo never existed (rd47 f540-f630: no 0x103E0/0x10824 objects, 0xFFF154 = 0 where
# the arcade's is 3). The callee now returns the carry and publishes A5; each caller
# takes the return and re-reads A5 after the call.
_e32 = [0, 0, 0, 0]
def _pair_alloc(txt):
    m = re.search(r'uint FUN_0000e032\(\)\s*\n\{.*?\n\}', txt, re.S)
    if m:
        body, k = re.subn(r"if \(\*pcVar2 == '\\0'\) \{\n(\s+)return 0;",
                          r"if (*pcVar2 == '\\0') {\n\1_r_unaff_A5 = (byte *)pcVar2;   /* 0xE04C: A5 = the free record, carry set: cport_prep.py */\n\1return 1;", m.group(0), count=1)
        _e32[0] += k
        txt = txt[:m.start()] + body + txt[m.end():]
    def _site(mm):
        kind, num, between, ws = mm.group(1), mm.group(2), mm.group(3), mm.group(4)
        _e32[1] += 1
        return '%sVar%s = %s;\n%s%s%sVar%s = FUN_0000e032();   /* its carry: cport_prep.py */' % (
            kind, num, 'false' if kind == 'b' else '0', between, ws, kind, num)
    txt = re.sub(r'(b|u)Var(\d+) = (?:false|0);\n((?:[^\n]*\n){0,2}?)(\s*)FUN_0000e032\(\);', _site, txt)
    # A5 after the call, in every function that models it as a local -- OR AS A
    # PARAMETER, which is the spelling that was missed.
    #
    # FUN_0000e032 walks A5 from 0xFFFFC800 over 24 records and returns the free one
    # IN A5 with the carry set (0xE032-0xE050). Ten of its callers hold A5 as `p_A5`
    # rather than `unaff_A5`, so the refresh below never fired and they went on using
    # the A5 they were CALLED with: `*p_A5 = 0x8000` set the live flag on the wrong
    # record and the new object never existed.
    #
    # WHAT IT COST (measured 2026-10-21, PORTKINDCENSUS over a scripted run to level 3):
    # the enemy-count byte 0xFFF15D is committed by the kind init BEFORE the search
    # (`DAT_fffff15d = bVar1` in FUN_0000e316) and released only by the enemy's own
    # death path. With the object never installed the count leaked, reached the cap the
    # spawner tests at 0xE0A0 (`cmpi.b #10,0xFFF15D ; bcc` = refuse), and level 2 then
    # spawned NOTHING: distinct enemy handlers in slots 16-39 read 10 for round 0, ONE
    # for round 1 and 3 for round 2, against the arcade's sixteen for round 1. At the
    # dump, 0xFFF15D = 10 with ZERO live records in the band. The affected functions are
    # kind inits 5, 6, 7, 10, 11 and 15 (0xE316, 0x132A0, 0x12B08, 0x1176A, 0x14A8C,
    # 0x15306) plus 0x143DC and 0x14A5A -- the level-2 roster.
    def _fn(mm):
        body = mm.group(0)
        if 'FUN_0000e032();' not in body:
            return body
        # THE PARAMETER FORM IS ALLOWLISTED, ONE FUNCTION AT A TIME.
        #
        # Applying it to all ten p_A5 callers FREEZES THE ATTRACT. Measured 2026-10-22
        # with a control and a proper freeze test (does the tick ADVANCE, not what it
        # reads once): lv2cen and a build with this disabled both go 299 -> 698 between
        # frames 3000 and 3400, while every build with it on sticks at 242 from frame
        # 3000 onward. The advisor bisected the freeze to cea7da1a, but that commit only
        # ADDED to this one -- reverting it alone does not unfreeze, and disabling this
        # does.
        #
        # It is still the right model -- A5 is a register the callee really does change,
        # and FUN_0000e316 needs it or kind 5's composite never installs (handlers
        # 0xE474 / 0xEA5E, f32fd553). So it goes in per function, each earning its place
        # against the attract freeze test and the kind census.
        _PA5_OK = ('FUN_0000e316',
                   # THE LEVEL-2 ROSTER, 2026-10-22 (advisor): each of these passed the attract
                   # freeze test alone (tick 298 -> 698 between frames 3000 and 3400, 5400-frame
                   # run); FUN_00012b08 was the one that froze (its three chained searches need
                   # the moved FP, hand/FUN_00012b08.c) and is a hand file, not allowlisted.
                   # FUN_000132a0 FUN_0001176a FUN_00014a8c FUN_00015306 are hand files now
                   # (their chained searches and templates, sh_src/portfull/hand/); FUN_00014a5a is
                   # Ghidra's function over the data bytes 0x14A5A-0x14A78 and is never called.
                   'FUN_000143dc', 'FUN_00014a5a')
        _fname = re.search(r'^(?:uint|ulonglong) (\w+)\(', body, re.M)
        _v = 'unaff_A5' if 'unaff_A5' in body else (
             'p_A5' if ('p_A5' in body and _fname and _fname.group(1) in _PA5_OK) else None)
        if _v is None:
            return body
        # GUARDED ON THE CARRY, and that guard is the whole lesson of cea7da1a.
        #
        # FUN_0000e032 only publishes _r_unaff_A5 when it FINDS a record. Re-reading
        # unconditionally therefore hands the caller a pointer that has nothing to do
        # with this call: before cea7da1a it was whatever the last SUCCESSFUL search
        # left (a stale live record), and cea7da1a "fixed" that by publishing on the
        # failure path too -- which is faithful to the 68K (`lea (0x80,a5),a5` runs
        # every iteration, so a failed scan returns A5 = 0xFFC800 + 24*0x80 = 0xFFD400)
        # and is WORSE, because 0xFFD400 IS SLOT 40, the boss record. The advisor
        # bisected the attract freezing at frame ~2940 to exactly that commit.
        #
        # On the 68K the post-failure A5 is DEAD -- every caller branches on the carry
        # to a backout and never writes through it -- so the only thing the port owes
        # is to not invent a pointer. Keeping the caller's own A5 when the search
        # failed does that, and needs no sink and no new symbol.
        body, k = re.subn(r'(\n(\s*))((\w+) = FUN_0000e032\(\);[^\n]*)',
                          r'\1\3\1if (\4) %s = (typeof(%s))_r_unaff_A5;   /* A5 moved by the callee, ONLY when it found one: cport_prep.py */'
                          % (_v, _v), body)
        _e32[2 if _v == 'unaff_A5' else 3] += k
        return body
    txt = re.sub(r'\n(?:uint|ulonglong) \w+\([^)]*\)\s*\n\{.*?\n\}', _fn, txt, flags=re.S)
    return txt
out = [_pair_alloc(_c) for _c in out]
_pass_report('pair allocator: FUN_0000e032 returns its carry', _e32[0], 1)
_pass_report('pair allocator: callers take the carry', _e32[1], 6)
_pass_report('pair allocator: callers re-read A5 (local unaff_A5)', _e32[2], 1)
_pass_report('pair allocator: callers re-read A5 (PARAMETER p_A5, allowlisted)', _e32[3], 2)

# ---- A POINTER REGISTER TRUNCATED TO A 68K SHORT (the P3d class, bare-register form) --
# `sVar1 = (short)unaff_A5;`, `p_A0[0x1e] = (short)unaff_A6;`: the 68K stores the low
# 16 bits of an address in 0xFFC000-0xFFFFFF (0xC880 for record 17); the SH-2 pointer
# 0x0602D480 truncates to 0xD480. P3d above only catches `*(short *)(...) = (short)X;`.
# rd48: FUN_000102cc cross-linked the zombie pair through `(short)unaff_A5` and
# `(int)(short)unaff_A5`, the pair's +0x78/+0x3c pointed into the wrong records, slot 61
# filled with 0x07070707 and the game died by f560 (gate eye 98). The 68K's
# `moveaw %sp@+,%fp` (0x10362) sign-extends that short back into an address, which on
# the SH-2 is simply the pointer itself.
_p3e = [0, 0]
def _ptr_short(txt):
    txt, k1 = re.subn(r'\(int\)\(short\)unaff_A([56])\b(?!\[)', r'(int)unaff_A\1', txt)
    txt, k2 = re.subn(r'\(short\)unaff_A([56])\b(?!\[)', r'(short) A68OF16(unaff_A\1)', txt)
    # THE PARAMETER SPELLING TOO. The same register reaches a function as `p_A5` when
    # Ghidra models it as an argument, and `(short)` of an SH-2 pointer is not the 68K's
    # low word: work RAM starts at 0x0602CC00 against the 68K's 0xFFC000, so the
    # truncation comes out 0xC00 HIGH and the value names a record 24 slots away.
    #
    # Measured 2026-10-22, and it is the third place the unaff_/p_ split has bitten
    # (see the A5 re-read and FUN_0000e032's publish): FUN_0000e316 claims a record with
    # `*p_A5 = 0x8000`, keeps `sVar4 = (short)p_A5` as the link, and the second-search
    # backout at 0xE308 clears `W68(sVar4)`. With sVar4 0xC00 high the backout cleared
    # the WRONG record and the claimed one stayed live with a null handler -- slots 18,
    # 19 and 20 read `80 00 00 00 00 00 ...` in round 1, which the arcade never shows.
    txt, k3 = re.subn(r'\(int\)\(short\)p_A([0-7])\b(?!\[)', r'(int)p_A\1', txt)
    txt, k4 = re.subn(r'\(short\)p_A([0-7])\b(?!\[)', r'(short) A68OF16(p_A\1)', txt)
    k1 += k3; k2 += k4
    _p3e[0] += k1; _p3e[1] += k2
    return txt
out = [_ptr_short(_c) for _c in out]
_pass_report('pointer registers truncated to a 68K short: A68OF16', _p3e[1], 9)
_pass_report('pointer registers sign-extended back: the pointer itself', _p3e[0], 1)
# ... and the short that came out of A68OF16 goes back through W68 when it is widened
# into an address (`sVar4 = (short)A6; ... iVar3 = (int)sVar4; *(iVar3 + 0x10) = ...`:
# prog68k 0xF7B2/0xF7CE `movew %fp,%sp@-` / `moveaw %sp@+,%fp`). Without this the P3e
# rewrite above hands the SH-2 a 68K offset as a pointer.
_p3f = [0]
def _short_back(txt):
    def _fn(mm):
        body = mm.group(0); k = 0
        for n in set(re.findall(r'(\w+) = \(short\) A68OF16\(unaff_A[56]\);', body)):
            body, j = re.subn(r'\(int\)%s\b(?!\s*\))' % n, '(int)W68(%s)' % n, body)
            k += j
        _p3f[0] += k
        return body
    return re.sub(r'\n(?:uint|ulonglong) \w+\([^)]*\)\s*\n\{.*?\n\}', _fn, txt, flags=re.S)
out = [_short_back(_c) for _c in out]
_pass_report('A68OF16 shorts widened back into pointers: W68', _p3f[0], 3)

# ---- FUN_000102cc: THE SECOND TEMPLATE GOES INTO THE PAIR SLOT -------------------------
# prog68k 0x10362 `moveaw %sp@+,%fp`: A6 becomes the pair record (the A5 the allocator
# found) before the second FUN_00010394 fill; the decompile kept writing through
# _r_unaff_A6 (the first record), so rd50 f630 had slots 17-19 = 0x10824 (the second
# template over the first record) and no 0x103E0 partner at all.
_t2 = [0]
def _pair_second_template(txt):
    m = re.search(r'uint FUN_000102cc\([^)]*\)\s*\n\{.*?\n\}', txt, re.S)
    if not m:
        return txt
    body, k = re.subn(r'(\n(\s+))(FUN_00010394\((?:\(undefined4 \*\)\()?0x10994\)?,0x10c48\);)',
                      r'\1_r_unaff_A6 = (byte *)iVar9;   /* 0x10362 moveaw sp+,fp -- A6 = the pair slot: cport_prep.py */\1\3', m.group(0), count=1)
    _t2[0] += k
    return txt[:m.start()] + body + txt[m.end():]
out = [_pair_second_template(_c) for _c in out]
_pass_report('zombie pair: the second template fills the pair slot', _t2[0], 1)

# ---- THE HERO'S PER-STATE DISPATCH PASSES D5/D6 (the input bits) ------------------------
# prog68k 0x4A98-0x4AC2: D5 = record+0x1f (0 when form >= 3 and record+0x1b == 0),
# D6 = ~record+0x20 & record+0x1e (new presses), D7 = record+0x1e (held); all three
# zero while 0xFFF14C (the demo-start timer) runs. The state handlers behind 0x6DA0
# (0x4C16 / FUN_00004870's twin) test D6 bit 1 for the jump. The generated dispatch
# published only D7 and A5, so the thunk handed the handler a STALE r[6]: rd47-rd51
# the demo hero was in state 9 (jump, sound 0xB6 queued every landing) from its first
# frame, with the input record reading 00 06 00 06 exactly like the arcade's. The
# register file is the ABI here; every value the 68K puts in a D register before a
# table jsr has to be published, not only the ones Ghidra happened to name.
_d56 = [0]
def _hero_dispatch_regs(txt):
    def _site(mm):
        d7, a5, site = mm.group(1), mm.group(2), mm.group(3)
        d6 = 'p_D6' if d7 == 'uVar11' else 'p_D6_00'   # the site tag is renumbered by every re-export; the local names pair the two copies
        _d56[0] += 1
        return ('(port_set_reg(5, (_DAT_fffff14c != 0) ? 0u : ((unaff_A6[0x6a] < 3 || *(byte *)(%s + 0x1b) != 0) ? (uint)*(byte *)(%s + 0x1f) : 0u)), '
                'port_set_reg(6, (uint)%s), port_set_reg(7, (uint)%s), port_set_reg(13, (uint)%s), port_call68_at(%s,'
                % (a5, a5, d6, d7, a5, site))
    return re.sub(r'\(port_set_reg\(7, \(uint\)(uVar11|uVar8)\), port_set_reg\(13, \(uint\)(iVar16|iVar12)\), port_call68_at\((\d+),', _site, txt)
out = [_hero_dispatch_regs(_c) for _c in out]
_pass_report('hero per-state dispatch publishes D5/D6', _d56[0], 2)

# ---- A SCRIPT POINTER DERIVED THROUGH A RAW 68K ADDRESS ------------------------------
# FUN_00010824 (and FUN_000103e0, FUN_00011e..): the pair's animation script is the
# parent's script pointer plus the word stored two bytes before it:
# `movea.l 0x24(a0),a1; move.w -2(a1),d0; add.l a1,d0; move.l d0,0x24(a6)`. Ghidra:
# `psVar = (short *)(LD32S(parent + 0x24) + -2); A6+0x24 = *psVar + (int)psVar` -- the
# 68K ROM address dereferenced on the SH-2 and then re-stored as if it were still a
# 68K value. rd57 f548: the partner ran the parent's script (+0x24 0x10a58 beside the
# parent's 0x10a5a; arcade 0x10c36). The word is read through P68; the stored base
# stays the 68K address.
_sp = [0]
def _script_offset(txt):
    txt, k1 = re.subn(r'psVar(\d+) = \(short \*\)\(LD32S\(\((\w+ \+ 0x24)\)\) \+ -2\);\n(\s+)ST32\(\((\w+) \+ 0x24\), \(\(int\)\*psVar\1 \+ \(int\)psVar\1\)\);',
                      r'{ int _s68 = LD32S((\2)) + -2;   /* a 68K ROM address: cport_prep.py */\n\3psVar\1 = (short *)P68(_s68);\n\3ST32((\4 + 0x24), ((int)*psVar\1 + _s68)); }', txt)
    txt, k2 = re.subn(r'\(int\)W68\(\*\(short \*\)\(LD32S\(\(unaff_A6 \+ 0x24\)\) \+ -2\)\)',
                      r'(int)W68(*(short *)P68(LD32S((unaff_A6 + 0x24)) + -2))', txt)
    # FUN_00011ce8: the same through the +0x3c link SHORT (a 68K record address) --
    # W68 for the link, P68 for the script word; the pointer is only dereferenced after.
    txt, k3 = re.subn(r'psVar(\d+) = \(short \*\)\(LD32S\(\(\*\(short \*\)\(unaff_A6 \+ 0x3c\) \+ 0x24\)\) \+ -2\);',
                      r'psVar\1 = (short *)P68(LD32S(((int)W68(*(short *)(unaff_A6 + 0x3c)) + 0x24)) + -2);   /* link short -> record, script word in ROM: cport_prep.py */', txt)
    _sp[0] += k1 + k2 + k3
    return txt
out = [_script_offset(_c) for _c in out]
# 2 since the 2026-10-04 re-export (jump-table overrides): one of the k1 copies now reads
# `*(int *)(unaff_A6 + 0x24) = (int)*(short *)(iVar11 + -2) + iVar11 + -2` (FUN_0000ea5e
# and two siblings) and the generic `X + *(short *)(X)` rule of the jump-table pass maps
# that word through A68PTR.
_pass_report('script pointer offsets read through P68', _sp[0], 2)

# 0x12824..0x1283E, the purple tailed runner: A0 = script; ADDA.W -(A0),A0;
# load the phase's speed byte and LSL.W #8. The word is a signed ROM-relative
# displacement, not a 68K short address. Mapping that displacement with W68
# reads unrelated RAM and can produce a screen-wide step in either facing.
_runner_speed = [0]
def _runner_speed_rom(txt):
    if not re.search(r'\bFUN_000124bc\(', txt):
        return txt
    pat = (r'uVar9 = \(ushort\)\*\(byte \*\)\(LD32S\(\(unaff_A6 \+ 0x24\)\) \+ -2 \+'
           r'[^;]+;')
    replacement = ('{ unsigned _base = LD32U(unaff_A6 + 0x24) - 2u;\n'
                   '          int _delta = *(const short *)P68(_base);\n'
                   '          uVar9 = *(const byte *)P68(_base + _delta + unaff_A6[0x21]); }')
    txt, n = re.subn(pat, replacement, txt)
    _runner_speed[0] += n
    return txt
out = [_runner_speed_rom(_c) for _c in out]
_pass_report('purple runner speed uses signed ROM displacement', _runner_speed[0], 1)

# ---- THE TILEMAP UNPACKERS COST THE 68K ~5 FRAMES EACH -------------------------------
# prog68k 0x16C8/0x16EE: 0x5000 RLE byte stores per pass, interrupted by vints; the
# arcade's first writes land at f451/f453 (hi) and f456/f459 (lo) and the demo hero is
# created at f464; the port did both in zero frames and created the hero at f443, so
# every attract scene after the cut led the arcade by ~20 frames (gate "cut black"
# 88-131 and "demo scene" 83-118 at lags 0-12). A frame yield every 0x1000 stores --
# the stubbed wait_frame_flag, the same yield the game loop uses -- reproduces the
# 68K's pace within a frame (five per pass).
_uy = [0]
def _unpack_yield(txt):
    for fn in ('tilemap_unpack_hi', 'tilemap_unpack_lo'):
        m = re.search(r'uint %s\([^)]*\)\s*\n\{.*?\n\}' % fn, txt, re.S)
        if not m:
            continue
        body, k = re.subn(r'(\n(\s+))sVar3 = sVar3 \+ -1;', r'\1sVar3 = sVar3 + -1;\1if ((sVar3 & 0xFFF) == 0) wait_frame_flag();   /* the 68K pace: cport_prep.py */', m.group(0))
        _uy[0] += k
        txt = txt[:m.start()] + body + txt[m.end():]
    return txt
out = [_unpack_yield(_c) for _c in out]
_pass_report('tilemap unpackers yield at the 68K pace', _uy[0], 3)

# ---- clear_tileram COSTS THE 68K ~1.5 FRAMES ----------------------------------------
# prog68k 0x36B0: 0x3400 `clr.l (a0)+` iterations (~12 cycles each at 10 MHz = 1.3
# frames) run at every scene cut inside the sequencer's entry (0x1E8A-0x1E9C: text,
# tile, sprite clears, then the palette copy) between its `jsr 397e` and the state
# dispatch. The arcade spends f15-f19 there; the port spent one frame (rd59: state 00
# at f17, 08 at f18). One yield per 0x2000 longs.
_ct = [0]
def _clear_pace(txt):
    m = re.search(r'uint clear_tileram\(\)\s*\n\{.*?\n\}', txt, re.S)
    if not m:
        return txt
    body, k = re.subn(r'(\n(\s+))sVar1 = sVar1 \+ -1;', r'\1sVar1 = sVar1 + -1;\1if ((sVar1 & 0x1FFF) == 0) wait_frame_flag();   /* the 68K pace: cport_prep.py */', m.group(0), count=1)
    _ct[0] += k
    return txt[:m.start()] + body + txt[m.end():]
out = [_clear_pace(_c) for _c in out]
_pass_report('clear_tileram yields at the 68K pace', _ct[0], 1)

# ---- SIGNED BYTE CLAMPS ON UNSIGNED DAT DECLARATIONS ---------------------------
# The 68K uses tst.b/bpl/clr.b at 0x778 and 0xA44, and subq.b/bpl/clr.b
# at 0x25C8. Ghidra declares their cells byte (unsigned), making `x < '\0'`
# permanently false. Preserve unsigned storage and test the byte's signed value
# only at the clamp, AFTER the original decrement. Five generated sites: four
# enemy-count copies and the card fade (LESSONS.md, 2026-10-17). Without the fade
# clamp, 0 -> FF restores the old card's full palette before the logo replaces it.
_neg_byte_clamps = [0]
def _signed_byte_clamp(m):
    name = m.group(1)
    if _dat_type(name, int(name.split('_')[1], 16)) != 'byte':
        return m.group(0)
    _neg_byte_clamps[0] += 1
    return re.sub(r'if \([^)]*\)', 'if ((signed char)%s < 0)' % name,
                  m.group(0), count=1)
_neg_byte_pat = re.compile(
    r"if \((DAT_[0-9a-f]{8}) < (?:'\\0'|0)\) \{\s*\1 = (?:'\\0'|0);\s*\}")
out = [_neg_byte_pat.sub(_signed_byte_clamp, c) for c in out]
_pass_report('signed zero clamps on unsigned byte globals', _neg_byte_clamps[0], 5)

# ---- order_list_insert STORES D0 = THE SLOT INDEX ------------------------------------
# prog68k.bin 0x3CA6: `bsr 0x3CB6; d1 = byte(fp+0x2F) << 6; moveb d0,(a0,d1.w)` -- and
# 0x3CB6 leaves D0 = byte(fp+8) (the object's slot index) with A0 = 0xFFEC80 + D0.
# Ghidra folded A0 and D0 into one return value and the C stores the low byte of the
# RETURNED POINTER (an SH-2 address, bit 7 set) where the 68K stores the index; the
# vint's sprite copy skips every entry with bit 7 set, so no sprite ever reached
# SPR_SNAP (rd9/rd10 2026-10-04: 3 records, the terminator at entry 0).
_oli = [0]
def _order_list_fix(txt):
    m = re.search(r'uint order_list_insert\(\)\s*\n\{.*?\n\}', txt, re.S)
    if not m:
        return txt
    body, k = re.subn(r'extraout_D0b = \(uint\)iVar1;', 'extraout_D0b = *(byte *)(unaff_A6 + 8);   /* 68K D0 = fp(8), the slot index: cport_prep.py */', m.group(0))
    _oli[0] += k
    return txt[:m.start()] + body + txt[m.end():]
out = [_order_list_fix(_c) for _c in out]
_pass_report('order_list_insert stores the slot index (0x3CA6 moveb d0)', _oli[0], 1)

# ---- THE SPRITE TERMINATOR'S SECOND WORD ---------------------------------------------
# irq4_handler ends the copied sprite table with 0xFFFF in word 0 AND word 2 (bit 15 of
# word 2 is the S16 end-of-list mark the renderer tests). The second store had been
# wrapped as `P68(p_A2 + 1)`: p_A2 is already an SH-2 pointer into SPR_SNAP, and P68 maps
# anything below 0xFFC000 into ROM, so the mark landed in ROM and every consumer walked
# past the live records. 2026-10-04.
_spt = [0]
def _spr_term_fix(txt):
    txt, k = re.subn(r'\*\(undefined2 \*\)\(\(char \*\)P68\(p_A2 \+ 1\)\) = 0xffff;', '*(undefined2 *)(p_A2 + 1) = 0xffff;   /* word 2 of the terminator: cport_prep.py */', txt)
    _spt[0] += k
    return txt
out = [_spr_term_fix(_c) for _c in out]
_pass_report('sprite terminator word 2 (P68 dropped)', _spt[0], 1)

# ---- THE PALETTE-SLOT BLOCK INIT: ONE ADDRESS, TWO WIDTHS ----------------------------
# prog68k.bin 0x3B08: `lea 0xfffff400,%a0; lea 0x4120,%a1; movel (a1)+,(a0)+; movel
# (a1),(a0)` copies 8 ROM bytes over 0xFFF400..407 -- the slot counter (byte, read by
# 0x3B44 moveb / 0x3B4E addqb), 0xFFF401 (byte) and the queue pointer 0xFFF402 (long).
# A per-cell width cannot express that: CPORT_WIDTH FFFFF400=4 broke the byte compare
# (no sprite palette was ever allocated, rd9-rd12 2026-10-04); as a byte the init
# copied one byte. The init is the only long access, so it is rewritten here as two
# byte-wise 32-bit stores and the cell stays a byte everywhere else.
_pi = [0]
def _palslot_init(txt):
    txt, k1 = re.subn(r'_DAT_fffff400 = DAT_00004120;', 'ST32(W68PTR(0xFFF400u), LD32U(&DAT_00004120));   /* 0x3B10 movel: cport_prep.py */', txt)
    txt, k2 = re.subn(r'_DAT_fffff404 = DAT_00004124;', 'ST32(W68PTR(0xFFF404u), LD32U(&DAT_00004124));   /* 0x3B12 movel */', txt)
    _pi[0] += k1 + k2
    return txt
out = [_palslot_init(_c) for _c in out]
_pass_report('palette-slot block init: two longs (0x3B10/0x3B12)', _pi[0], 2)

# ---- A 68K WORK-RAM CONSTANT CAST TO A POINTER IS A WORK-RAM POINTER ---------------
# `FUN_0000f28a(0xffffc000,(short int *)(0xffffdf00),...)` (0xF1C0): the demo's enemy
# installer gets object slot 62 as a bare 68K address cast to `short *`, and every
# store through it -- the handler 0xF352 included -- went to SH-2 0xFFFFDF00, the
# on-chip peripheral file. MEASURED 2026-10-04 (rd14 f985 vs arcade f947): the arcade
# has slots 62/63 active with handler 0xF352, the port never. The cast form is exact:
# a literal in 0xFFC000-0xFFFFFF named by the program IS work RAM.
_wc = [0]
def _wram_cast(txt):
    def _r(m):
        _wc[0] += 1
        return '(%s)W68PTR(0x%su)' % (m.group(1), m.group(2)[4:].upper().rjust(6, 'F'))
    txt = re.sub(r'\((\w+(?: int)? \*)\)\(0x(ffff[c-f][0-9a-f]{3})\)', _r, txt)   # gcc's form (0x...)
    # (not in a comparison: `puVar1 == (undefined *)0xfffff060` is the bound-constant pass's anchor)
    # Ghidra's no-paren form (7 sites) is NOT rewritten: rd28 added it together with the
    # ROM casts and the gate regressed; re-enable one site at a time against the 68K.
    return txt
out = [_wram_cast(_c) for _c in out]
# (the cast is added by the later gcc feedback loop; at this stage the argument is the bare
#  constant, so the callee's A68PTR entry normalisation -- CPORT_PARAMNORM FUN_0000f28a -- is
#  what actually carries it. Reported, not required.)
_pass_report('68K work-RAM constants cast to pointers -> W68PTR', _wc[0], 0)

# ---- A PROGRAM-ROM CONSTANT CAST TO A POINTER IS A ROM POINTER -------------------------
# FUN_00004178 (the high-score table init): `puVar3 = (undefined4 *)0x4732;` then 14
# longs copied to 0xFFFC00. 0x4732 is the ROM default table; as an SH-2 address it is
# the vector area. MEASURED 2026-10-04 (rd27 f985 vs arcade f993): the arcade's table
# reads 41 28 00 05 20 20 20 ... (the 412800 score the HUD shows), the port's is zero
# and its score row is spaces. Ghidra's own cast, present at this stage. Only the
# program range (0x400-0x3FFFF) and only pointer-typed casts.
_rc = [0]
def _rom_cast(txt):
    def _r(m):
        v = int(m.group(2), 16)
        if v < 0x400 or v >= 0x40000:
            return m.group(0)
        _rc[0] += 1
        return '(%s)((char *)ROM_BASE + 0x%X)' % (m.group(1), v)
    # NOT `(code *)0xNNNN`: those are handler ADDRESSES stored into 68K records for the
    # dispatcher (248 sites) and must stay 68K values. Data pointers only.
    # RESTRICTED to FUN_00004178 (2026-10-04): applied to all 10 data-pointer sites (rd28)
    # the deterministic gate moved the eye row from 34-44 to 60-72 and 32X CRAM from 198
    # to 122 -- one of the other nine is a VALUE, not a pointer. Each gets verified
    # against the 68K before it is added here.
    m = re.search(r'uint FUN_00004178\(\)\s*\n\{.*?\n\}', txt, re.S)
    if not m:
        return txt
    body = re.sub(r'\(((?!code\b)\w+(?: int)? \*)\)0x([0-9a-f]{3,5})\b', _r, m.group(0))
    return txt[:m.start()] + body + txt[m.end():]
out = [_rom_cast(_c) for _c in out]
# FUN_00004178 is hand-derived above (byte copies from ROM_BASE + 0x4732), so the
# constant this pass was written for is gone; 0 is the expected count now (2026-10-04).
_pass_report('program-ROM constants cast to pointers -> ROM_BASE (FUN_00004178)', _rc[0], 0)

# ---- sprite_frame_lookup RETURNS A1 = A ROM POINTER READ FROM A ROM TABLE -----------
# prog68k.bin 0x3C84..0x3CA4: `a0 = table + frame*6; d1 = (a0)+; a1 = (a0); a0 = d1 +
# 0x255E0` -- A1 is a 68K program address loaded from the sprite-frame table, and the
# C returns it raw in the low half of CONCAT44. sprite_build_and_cull then walks it as
# an SH-2 pointer (`extraout_A1 - 2`), reading the SH-2 vector area as sprite data.
# MEASURED 2026-10-04 (arcade wp census frames 945-948): sprite_build_and_cull and
# hide_object_sprite are the only 0xFFF800 writers; the port's table held 6 records
# against the arcade's 30 at the matched state. A68ANY maps a program address to
# ROM_BASE and is the identity for anything already SH-2.
_sfl = [0]
def _sprite_frame_fix(txt):
    m = re.search(r'ulonglong sprite_frame_lookup\(\)\s*\n\{.*?\n\}', txt, re.S)
    if not m:
        return txt
    # REVERTED 2026-10-04 (rd20-22): A1 is never dereferenced downstream -- 0x3EA4 adjusts it
    # (`addaw a0@,a1`) and 0x3ED2-0x3EE0 STORE a1>>1 into the sprite record as the sprite
    # ROM address the hardware reads. Translating it put the SH-2 ROM base into the
    # record's bank byte (0xFA where the arcade writes 0xF0) and the hero vanished. A
    # 68K address that is stored for the HARDWARE must stay in the 68K domain.
    k = 1
    body = m.group(0)
    _sfl[0] += k
    return txt[:m.start()] + body + txt[m.end():]
out = [_sprite_frame_fix(_c) for _c in out]
_pass_report('sprite_frame_lookup: A1 translated (0x3C9E moveal (a0),a1)', _sfl[0], 1)

# ---- EVERY zoom_scale_lookup CALLER RELOADS D3/D6/D7 FROM THE STUB -------------------
# 0x3CD4 returns the sprite's pitch (D3), height (D6) and width (D7) in registers; the
# stub publishes them to _port_reg[3/6/7]. sprite_build_depth_banded reloads them
# (the register-plumbing pass covered it); sprite_build_and_cull did not, and built
# its records from the STALE parameters: MEASURED 2026-10-04 (rd27 f1192 vs arcade
# f1200, same sprite data c866/f480): word 0 = 0x8070 / pitch 0 against 0x935d / 0x0E
# -- the eye's iris a 16-line band, the title logo and the heroes collapsed.
_zr = [0]
def _zoom_reload(txt):
    lines = txt.split('\n'); out_l = []
    i = 0
    while i < len(lines):
        out_l.append(lines[i])
        if re.search(r'=\s*zoom_scale_lookup\([^;]*\);', lines[i]) and not lines[i].lstrip().startswith(('uint ', 'ulonglong ')):
            nxt = '\n'.join(lines[i+1:i+4])
            if '_port_reg[3]' not in nxt:
                ind = re.match(r'\s*', lines[i]).group(0)
                out_l.append(ind + 'p_D3 = (undefined4)_port_reg[3]; p_D6 = (undefined4)_port_reg[6]; p_D7 = (undefined4)_port_reg[7];   /* 0x3CD4 returns D3/D6/D7: cport_prep.py */')
                _zr[0] += 1
        i += 1
    return '\n'.join(out_l)
out = [_zoom_reload(_c) for _c in out]
_pass_report('zoom_scale_lookup callers reloading D3/D6/D7', _zr[0], 1)

# ---- ... AND THE INLINED CULL MUST READ THOSE RELOADED REGISTERS --------------------
# Ghidra inlines the sprite build's tail call (`jmp 0x3D36 / 0x3DD8`) into each handler,
# and in 12 of the 102 copies the cull's HEIGHT and WIDTH come from something that is not
# the live D6/D7: a pseudo-register nothing updates after the lookup (`unaff_D6`,
# `unaff_D7w`: 7 copies) or a local holding an unrelated value -- FUN_000103e0 subtracts
# `uVar7`, last assigned a masked flags byte (`& 0xffc0`) 240 lines earlier, and
# FUN_00013b2c/FUN_00015434 the same. 0x3D42-0x3D4A is `movew fp@(16),d0 ; ... ; movew
# d0,d1 ; subw d6,d1 ; tstw d0 ; bmi`, so the subtrahend is D6.w as the lookup left it.
# MEASURED 2026-10-05: the hardware sprite list 0xFFF800.. differed from the arcade's at
# 240 of 319 swept ticks, entry byte +1 (the sprite's second y) and the address word, all
# of them entries owned by handler 0x103E0 (demo A tick 0x74 entries 17/19, 0x92 adds 21).
# Scoped to the window between the reload and the end of the cull, so the 90 copies that
# already read p_D6/p_D7 are unchanged.
_cull = [0, 0, 0]
_YLINE = re.compile(r'^(\s*)(\w+) = \(_DAT_fffff0fa \+ \*\(short \*\)\(unaff_A6 \+ 0x10\) \+ -0x1000\) - _DAT_fffff128;$')
_XLINE = re.compile(r'^(\s*)(\w+) = _DAT_fffff120 \+ _DAT_fffff0f8 \+ \*\(short \*\)\(unaff_A6 \+ 0xc\) \+ -0xf40;$')
def _cull_regs(txt):
    if '0x3CD4 returns D3/D6/D7' not in txt:
        return txt
    lines = txt.split('\n')
    for i, l in enumerate(lines):
        if '0x3CD4 returns D3/D6/D7' not in l:
            continue
        # the window: the reload to the record write that ends the cull (or 45 lines)
        hi = len(lines)
        for j in range(i + 1, min(len(lines), i + 46)):
            if re.search(r'\*\(ushort \*\)\((\w+) \+ 10\) = ', lines[j]):
                hi = j + 1
                break
        else:
            hi = min(len(lines), i + 46)
        for j in range(i + 1, hi):
            # (a) the height on the line right below the y line (before the pseudo-register
            # substitution below, so the comment names the value the decompiler used)
            ym = _YLINE.match(lines[j])
            if ym and j + 1 < hi:
                sub = re.match(r'^\s*(\w+) = %s - ([^;]+);$' % re.escape(ym.group(2)), lines[j + 1])
                if sub and 'p_D6' not in sub.group(2):
                    lines[j + 1] = '%s%s = %s - (short)p_D6;   /* 0x3D46 subw d6,d1: the height the zoom lookup just returned, not %s: cport_prep.py */' % (
                        ym.group(1), sub.group(1), ym.group(2), sub.group(2))
                    _cull[1] += 1
            # (b) the WIDTH, on the two lines below the x line: 0x3E18 is `asrw #1,d7 ;
            # subxw d7,d2 ; cmpiw #512,d2 ; bcc ; addw d7,d4 ; cmpiw #185,d4 ; bcs`, so
            # both the subtrahend and the 0xB9 test read D7.w as the lookup left it.
            # FUN_0000fd6c subtracts `uVar10`, assigned 0 six lines BELOW the use (demo B
            # tick 0xA2: its entries 22/23 x came out 16 px right of the arcade's);
            # FUN_0000d9f6 uses `p_D7 << 2` from before the call and drops subx's carry.
            xm = _XLINE.match(lines[j])
            if xm and j + 2 < hi:
                sub = re.match(r'^(\s*)(\w+) = %s - \(\(ushort\)\(\((\w+) & 1\) != 0\) \+ \(\(short\)\3 >> 1\)\);$'
                               % re.escape(xm.group(2)), lines[j + 1])
                alt = re.match(r'^(\s*)(\w+) = %s - \((\w+) >> 1\);$' % re.escape(xm.group(2)), lines[j + 1])
                hit = sub or alt
                if hit and 'p_D7' not in hit.group(3):
                    src = hit.group(3)
                    lines[j + 1] = ('%s%s = %s - ((ushort)(((short)p_D7 & 1) != 0) + ((short)p_D7 >> 1));'
                                    '   /* 0x3E18 asrw #1,d7 ; subxw d7,d2: the width the zoom lookup just'
                                    ' returned, not %s: cport_prep.py */' % (hit.group(1), hit.group(2), xm.group(2), src))
                    lines[j + 2] = re.sub(r'\(\(short\)%s >> 1\)' % re.escape(src), '((short)p_D7 >> 1)',
                                          re.sub(r'\(%s >> 1\)' % re.escape(src), '((short)p_D7 >> 1)', lines[j + 2]))
                    _cull[2] += 1
            # (c) the pseudo-registers the lookup does not write
            lines[j], k1 = re.subn(r'\(short\)unaff_D([67])\b', r'(short)p_D\1', lines[j])
            lines[j], k2 = re.subn(r'\bunaff_D([67])w\b', r'(short)p_D\1', lines[j])
            _cull[0] += k1 + k2
    return '\n'.join(lines)
out = [_cull_regs(_c) for _c in out]

# ---- THE ATTRACT'S INSTALL CHAINS WALK A6, AND THE DECOMPILER RESOLVED IT AWAY -------
# 0x1FC2 and 0x20B8 are `lea 0xffffc000,%fp`, and each install that follows is a run of
# `movew #x,%fp@(n)` ending in `lea %fp@(128),%fp` -- A6 walks slot 0, 1, 2, ... Ghidra
# resolved every one of those writes to an ABSOLUTE (_DAT_ffffc000, _DAT_ffffc080,
# _DAT_ffffc100 ...), so the records are written correctly and the A6 walk vanished
# entirely. But the calls INTERLEAVED with the installs still run on A6:
# `jsr 0x3B2E` (request_palette_update reads the record's +0x0b and writes its +0x0a) and
# `jsr 0x3D14` (sprite_build_depth_banded writes the sprite entry at byte +8).
# MEASURED 2026-10-05, both halves of this:
#   * the attract (caseD_8, one install): palette slot 0 held cset 0x40 where the arcade
#     holds 0x5D, from the attract's first frame, because the request ran on whatever
#     record A6 last pointed at. 19 of the sweep's 23 differing ticks.
#   * the high-score step (caseD_10, eight installs): the port's seventh object came out
#     in slot 62 instead of slot 6 -- 0x250A rewrites its own record to handler 0x24A4,
#     so the wrong A6 moved the whole object. Every record in the step differed.
# So A6 for one of these calls is the base of the most recent record install above it, and
# it is restored afterwards (the 68K leaves it walking; the port's later lines are
# absolutes either way, and a local set cannot leak).
_RECBASE = re.compile(r'^\s*_DAT_ffffc([0-9a-f]{3}) = 0x8000;$')
_A6CALLS = ('request_palette_update();', 'sprite_build_depth_banded(',
            'release_palette_slot();', 'hide_object_sprite();')
# ... and the chain's LAST `lea %fp@(128),%fp` is followed by a call, not an install:
# 0x21F2 advances A6 to slot 6 and 0x21F6 is `bsr 0x250A`, a handler that writes its own
# record (`*unaff_A6 = 0x8000; ST32((unaff_A6 + 1), 0x24a4u)`) -- so it installs the
# seventh high-score object. With A6 left at slot 5 the port put that object in slot 62
# (0xFFDF00, measured 2026-10-05: the arcade's slot 6 holds handler 0x24A4, the port's 62).
_A6NEXT = {'FUN_0000250a(': 0x80}
# ... and one call in the chain sets its OWN base: 0x1F38 `lea 0xffffc000,%fp ; bsr 0x24ec`
# installs the title object INTO SLOT 0 (the idle player record). Ghidra dropped the lea, so
# the four inlined copies ran FUN_000024ec on the chain's last A6 and the title object lived
# in slot 48 (measured 2026-10-23 against a fresh-NVRAM arcade: slot 0 h 024c0 / slot 48
# empty on the arcade, the reverse on the port, every title-scene dump).
_A6FIXED = {'FUN_000024ec(': 0xFFC000}
_a6abs = [0, 0]
def _a6_absolute_block(blk):
    """One FUNCTION block. A6 walks within an install chain, so the base must reset at
    every function boundary -- tracking it across the whole unit rewrote 402 calls."""
    if '_DAT_ffffc000 = 0x8000;' not in blk:
        return blk            # only the inlined copies of the attract's install chains
    lines = blk.split('\n'); o = []
    base = None
    for l in lines:
        m = _RECBASE.match(l)
        if m:
            off = int(m.group(1), 16)
            base = (0xFFC000 + off) if off % 0x80 == 0 else None
            _a6abs[1] += 1
        st = l.strip()
        _fx = next((v for k, v in _A6FIXED.items() if st.startswith(k)), None)
        if _fx is not None:
            ind = re.match(r'\s*', l).group(0)
            o.append('%s{ void *_sv = _r_unaff_A6; _r_unaff_A6 = (byte *)W68PTR(0x%Xu); %s'
                      ' _r_unaff_A6 = _sv; }   /* 0x1F38 lea 0xffffc000,fp ; bsr 0x24ec: the title'
                      ' object installs into slot 0: cport_prep.py */' % (ind, _fx, st))
            _a6abs[0] += 1
            continue
        _nx = next((v for k, v in _A6NEXT.items() if st.startswith(k)), None)
        if base is not None and _nx is not None:
            ind = re.match(r'\s*', l).group(0)
            o.append('%s{ void *_sv = _r_unaff_A6; _r_unaff_A6 = (byte *)W68PTR(0x%Xu); %s'
                      ' _r_unaff_A6 = _sv; }   /* 0x21F2 lea 0x80(a6),a6 ; 0x21F6 bsr: the'
                      ' handler installs itself into the NEXT record: cport_prep.py */'
                      % (ind, base + _nx, st))
            _a6abs[0] += 1
            continue
        if base is not None and any(st.startswith(c) for c in _A6CALLS):
            ind = re.match(r'\s*', l).group(0)
            o.append('%s{ void *_sv = _r_unaff_A6; _r_unaff_A6 = (byte *)W68PTR(0x%Xu); %s'
                      ' _r_unaff_A6 = _sv; }   /* 0x20B8.. lea 0xffffc000,a6 + lea 0x80(a6),a6:'
                      ' this call runs on the record just installed: cport_prep.py */'
                      % (ind, base, st))
            _a6abs[0] += 1
            continue
        o.append(l)
    return '\n'.join(o)
out = [''.join(_a6_absolute_block(_b) for _b in re.split(r'(?=/\* ---- 0x)', _c)) for _c in out]

# ---- THE REST OF THE `lea <abs>,%fp` CENSUS -----------------------------------------
# tools/disasm_funcs.py finds 39 absolute A6 loads in 28 functions. 20 are covered by a
# hand file, a CPORT_STUBS redirect or the A6 plumbing, 9 are the attract install chains
# above, and six were left: FUN_00003036/3044/3052/3062, FUN_00016E1E, FUN_0001A88C.
# MEASURED 2026-10-06, which of those the port actually needs: the four 0x30xx read A6
# ZERO times and call nothing (Ghidra resolved their whole bodies to absolutes -- the
# checksum walk over 0xFFFFFF00 and the copy to 0xFFFFFF80), so there is nothing for A6 to
# change there and they are not defects. The other two are:
#   FUN_00016E1E  0x16E22 `lea 0xffffd400,%fp` + `lea 0x80(fp),fp`: a free-record walk that
#                 installs handler 0x16E94 and ends `jsr 0x3B2E`. Ghidra wrote the new
#                 record through the walked local and the PARENT through unaff_A6 (both
#                 right), but left the palette request on the parent.
#   FUN_0001A88C  0x1A890/0x1A89A/0x1A8D8 `lea 0xffffff80|00,%fp`: the high-score table
#                 pair. Its two helpers (0x1A8FE, 0x1A866) read A6 and nothing else, and
#                 the 68K sets it once per call pair.
# A6 here is not derivable from the C -- the loads are gone -- so the value comes from the
# listing, one table row per site, anchored on the call text and its occurrence.
_A6AT = {
    'FUN_00016e1e': [('request_palette_update();', 0, '(byte *)psVar2')],
    'FUN_0001a88c': [('uVar1 = FUN_0001a8fe();',     0, '(byte *)W68PTR(0xFFFF80u)'),
                     ('FUN_0001a866(uVar1);',        0, '(byte *)W68PTR(0xFFFF80u)'),
                     ('uVar1 = FUN_0001a8fe();',     1, '(byte *)W68PTR(0xFFFF00u)'),
                     ('FUN_0001a866(uVar1);',        1, '(byte *)W68PTR(0xFFFF00u)'),
                     ('_DAT_ffffff0e = FUN_0001a8fe();', 0, '(byte *)W68PTR(0xFFFF00u)')],
}
_a6at = [0]
def _a6_at_sites(blk):
    m = re.match(r'/\* ---- 0x[0-9A-F]+\s+(\w+)', blk)
    if not m or m.group(1) not in _A6AT:
        return blk
    rows = _A6AT[m.group(1)]
    seen = {}
    lines = blk.split('\n'); o = []
    for l in lines:
        st = l.strip()
        n = seen.get(st, 0)
        hit = next((r for r in rows if r[0] == st and r[1] == n), None)
        if hit:
            ind = re.match(r'\s*', l).group(0)
            o.append('%s{ void *_sv = _r_unaff_A6; _r_unaff_A6 = %s; %s _r_unaff_A6 = _sv; }'
                      '   /* the absolute A6 load the decompiler dropped: cport_prep.py */'
                      % (ind, hit[2], st))
            _a6at[0] += 1
            seen[st] = n + 1
            continue
        if st:
            seen[st] = n + 1
        o.append(l)
    return '\n'.join(o)
out = [''.join(_a6_at_sites(_b) for _b in re.split(r'(?=/\* ---- 0x)', _c)) for _c in out]

# ---- THE TILEMAP PAGE MARK, AT EVERY FUNCTION THAT NAMES A SHADOW ADDRESS -------------
# The renderer finds changed tilemap pages by hashing all 13 of them once per game frame:
# 0.27 vints a frame, the largest single section of the port's clock, to find 0.045 changed
# pages. port_tile_dirty replaces that poll, but only if EVERY writer marks.
#
# The two blit loops got hand files because they needed DERIVING (Ghidra had inlined one).
# The rest do not: their decompiled bodies already write the right places -- cport_layout
# resolved `lea 0x40xxxx,%a0` to a literal shadow address (0x06019518u) or to a placed
# _DAT_0040xxxx -- so a hand file there would be a rewrite with no derivation in it, which
# is exactly what went wrong on 2026-10-07: a rewritten tilemap_block_04 named
# &DAT_0040b230, dat_syms.h does not declare it, cport_prep's own feedback loop invented it
# in extras.h outside the shadow, and the writes scribbled over the high-score records.
# An idiom across many functions is a pass with a count.
#
# Any function whose text names an address inside the 13-page shadow gets ONE mark, at its
# top, of the pages those addresses fall in. Marking before the writes is equivalent (the
# mark is consumed at the next flip), and marking on a READ costs one spare page upload
# where a missed write costs a wrong picture.
_SHADOW_LO, _SHADOW_HI = 0x06019000, 0x06026000
_pgmark = [0]


def _page_mark(blk):
    if not re.match(r'/\* ---- 0x[0-9A-F]+\s+\w+', blk):
        return blk
    pages = set()
    for lit in re.findall(r'0x060[12][0-9A-Fa-f]{4}u', blk):
        a = int(lit[:-1], 16)
        if _SHADOW_LO <= a < _SHADOW_HI:
            pages.add((a - _SHADOW_LO) >> 12)
    for nm in re.findall(r'_?DAT_0040([0-9a-f]{4})', blk):
        pages.add(int(nm, 16) >> 12)
    pages = set(p for p in pages if p < 13)
    if not pages:
        return blk
    mask = 0
    for p in pages:
        mask |= 1 << p
    i = blk.find('\n{\n')
    if i < 0:
        return blk
    _pgmark[0] += 1
    return (blk[:i + 3] + '#ifdef PORT_TILEDIRTY\n  port_tile_dirty |= 0x'
            + ('%04X' % mask) + 'u;   /* the tilemap pages this function names:'
            ' cport_prep.py */\n#endif\n' + blk[i + 3:])


out = [''.join(_page_mark(_b) for _b in re.split(r'(?=/\* ---- 0x)', _c)) for _c in out]
_pass_report('tilemap page marks at the functions that name a shadow address', _pgmark[0], 8)

_pass_report('dropped absolute A6 loads restored at their call sites',
             _a6at[0], sum(len(v) for v in _A6AT.values()))

_pass_report('attract install chains: A6-using calls run on the installed record', _a6abs[0], 20)
_pass_report('  record bases seen in those chains', _a6abs[1], 30)
_pass_report('inlined cull: stale D6/D7 pseudo-registers reloaded', _cull[0], 9)
_pass_report('inlined cull: the height reads the reloaded D6', _cull[1], 5)
_pass_report('inlined cull: the width reads the reloaded D7', _cull[2], 4)

# ---- FUN_00001338: THE OBJECT-TABLE CLEAR AT EVERY ATTRACT LOOP ----------------------
# prog68k.bin 0x1338: `lea 0xffffc000,a0; moveq #63,d3; 1: bsr 0x135E (32 longs of 0,
# A0 += 0x80); move.b d1,-0x78(a0); move.b #3,-0x51(a0); move.b d2,-0x77(a0); addq #1,d1;
# dbf d3`. The C passed A0 as the bare int -0x4000 (so the clears went to the SH-2
# peripheral file) and rendered the three negative displacements as `W68PTR(0xFFFF88u)
# + p_A0` -- an absolute work-RAM cell plus a register, which is never a 68K thing.
# MEASURED 2026-10-04 (rd18 f2200, the attract's loop point): the arcade has ONE active
# object (the high-score controller), the port 16 (the whole demo still alive).
# FUN_0000135e's p_A0 is normalised by CPORT_PARAMNORM; the caller is rewritten here.
_tc = [0]
def _table_clear_fix(txt):
    m = re.search(r'uint FUN_00001338\(\)\s*\n\{.*?\n\}', txt, re.S)
    if not m:
        return txt
    body = m.group(0); k = 0
    body, k1 = re.subn(r'p_A0 = -0x4000;', 'p_A0 = (int)W68PTR(0xFFC000u);   /* lea 0xffffc000,a0 */', body)
    body, k2 = re.subn(r'W68PTR\(0xFFFF88u\) \+ \(p_A0\)', '(char *)(p_A0) - 0x78', body)
    body, k3 = re.subn(r'W68PTR\(0xFFFFAFu\) \+ \(p_A0\)', '(char *)(p_A0) - 0x51', body)
    body, k4 = re.subn(r'W68PTR\(0xFFFF89u\) \+ \(p_A0\)', '(char *)(p_A0) - 0x77', body)
    # D0 is `moveq #0,d0` once and the helper never touches it; Ghidra's `extraout_D0 =
    # (uint)uVar2` took the helper's D1 result as D0, so every slot after the first was
    # cleared to its own index instead of zero (rd22 f2200: 0x30 in the low byte of
    # every long of slot 48, the arcade's are 0x00).
    body, k5 = re.subn(r'p_D0 = extraout_D0;', 'p_D0 = 0;   /* d0 stays 0: cport_prep.py */', body)
    _tc[0] += k1 + k2 + k3 + k4 + k5
    return txt[:m.start()] + body + txt[m.end():]
out = [_table_clear_fix(_c) for _c in out]
_pass_report('object-table clear (0x1338): base, three displacements, d0 stays 0', _tc[0], 5)

# ---- 0x24A4: A HANDLER GHIDRA NEVER MADE A FUNCTION OF --------------------------------
# func_ranges: FUN_00002490 ends at 0x24A3, FUN_000024c0 starts at 0x24C0; the 28 bytes
# between are the credit object's handler, installed by FUN_0000250a as the C label
# `&LAB_000024a4` (an SH-2 address stored into the record -> no dispatch). prog68k.bin
# 0x24A4: `cmpiw #0x1180,0xffffc00c; bcc 0x24C0; cmpiw #0x1001,0xffffc00c; bcc 1f;
# tstw 0xffffc014; beq 0x24C0; 1: jsr integrate_position` then FALLS INTO 0x24C0.
# MEASURED 2026-10-04 (rd34 f1192 vs arcade f1200): the arcade's eye scene has slot 6
# with handler 0x24A4 active, the port never. Hand-written from the bytes above;
# 0x024A4 is in docs/proof/indirect_roots.txt so cport_dispatch.py thunks it.
_h24 = [0]
def _handler_24a4(txt):
    k = 0
    txt, k1 = re.subn(r'\*\(undefined \*\*\)\(unaff_A6 \+ 1\) = (?:\(undefined \*\))?&LAB_000024a4;',
                      'ST32(((char *)unaff_A6 + 2), 0x24a4u);   /* handler 0x24A4, the 68K value: cport_prep.py */', txt)
    k += k1
    if 'uint FUN_000024c0(undefined4 p_D3,undefined4 p_D6,undefined4 p_D7)\n{' in txt and 'uint FUN_000024a4(' not in txt:
        fn = ('uint FUN_000024a4(undefined4 p_D3,undefined4 p_D6,undefined4 p_D7);\n'
              'uint FUN_000024a4(undefined4 p_D3,undefined4 p_D6,undefined4 p_D7)\n{\n'
              '  unsigned short _x = *(unsigned short *)W68PTR(0xFFC00Cu);\n'
              '  if (_x < 0x1180u && (_x >= 0x1001u || *(short *)W68PTR(0xFFC014u) != 0))\n'
              '    integrate_position();\n'
              '  return FUN_000024c0(p_D3, p_D6, p_D7);\n}\n')
        txt = txt.replace('uint FUN_000024c0(undefined4 p_D3,undefined4 p_D6,undefined4 p_D7)\n{', fn + 'uint FUN_000024c0(undefined4 p_D3,undefined4 p_D6,undefined4 p_D7)\n{', 1)
        k += 1
    _h24[0] += k
    return txt
out = [_handler_24a4(_c) for _c in out]
_pass_report('handler 0x24A4: store rewritten + function defined', _h24[0],
             0 if any('uint FUN_000024a4(' in _c for _c in out) else 2)   # Ghidra provides it since the 2026-10-04 re-export (make_kind_inits.py)

# ---- CPORT_ARGLOG: record a parameter's incoming value ------------------------------
# "FUN:param,FUN:param" -> at each named function's entry, BEFORE the A68PTR lines
# (inserted above, so this lands first), push (id<<24 | port_fn_seq, value) into the
# driver's ring. The value is the caller's raw argument -- the domain question answered
# by measurement instead of by reading callers. tools/port_arglog_read.py decodes it.
_ARGLOG = [x.strip() for x in (os.getenv('CPORT_ARGLOG') or '').split(',') if x.strip()]
_al_n = [0]
def _arglog(txt):
    for _id, _spec in enumerate(_ARGLOG):
        _fn, _pm = _spec.split(':')
        txt, k = re.subn(r'(\buint\s+%s\s*\([^)]*\)\s*\n\{\n)' % re.escape(_fn),
                         r'\1  PORT_ARGLOG(%d, %s);\n' % (_id, _pm), txt, count=1)
        _al_n[0] += k
    return txt
out = [_arglog(_c) for _c in out]
_pass_report('CPORT_ARGLOG entries inserted', _al_n[0], len(_ARGLOG))

# ---- CPORT_SBDB_PROBE: the player cull's inputs ------------------------------------
# sprite_build_depth_banded hides every object in the port (rd19: hide 11,256 /
# sprite_build_and_cull 2 in 985 frames) while the arcade draws them. Log, per call,
# the zoom stub's D6/D7 and the object's Y/X right after the zoom lookup; ids 0/1.
if os.getenv('CPORT_SBDB_PROBE'):
    _sb = [0]
    def _sbdb_probe(txt):
        m = re.search(r'uint sprite_build_depth_banded\([^)]*\)\s*\n\{.*?\n\}', txt, re.S)
        if not m:
            return txt
        body, k = re.subn(r'(uVar11 = zoom_scale_lookup\([^;]*\);\n)',
                          r'\1  PORT_ARGLOG(0, (_port_reg[6] << 16) | (_port_reg[7] & 0xFFFFu));\n  PORT_ARGLOG(1, ((uint)*(ushort *)(unaff_A6 + 0x10) << 16) | *(ushort *)(unaff_A6 + 0xc));\n', m.group(0), count=1)
        # and WHICH test hides: every `goto hide_object_sprite;` logs its source line (id 2)
        _lines = body.split('\n')
        for _i, _l in enumerate(_lines):
            if 'goto hide_object_sprite;' in _l:
                _lines[_i] = _l.replace('goto hide_object_sprite;', '{ PORT_ARGLOG(2, %du); goto hide_object_sprite; }' % _i)
        body = '\n'.join(_lines)
        _sb[0] += k
        return txt[:m.start()] + body + txt[m.end():]
    out = [_sbdb_probe(_c) for _c in out]
    _pass_report('SBDB probe inserted', _sb[0], 1)

# ---- A 68K ADDRESS CONSTANT COMPARED AGAINST A POINTER ---------------------------
#
#     puVar1 = (undefined *)W68(_DAT_fffff03e) + 1;          an SH-2 pointer
#     if (puVar1 == (undefined *)0xfffff060) puVar1 = &DAT_fffff040;
#
# The sound queue is a 32-byte ring at 0xFFF040..0xFFF05F and this is its wrap. The
# local is translated (W68) and the bound is not, so they can never be equal: the
# ring never wraps and every enqueue walks one byte further up work RAM. MEASURED
# 2026-10-04: 96 enqueues later it reached the i8751 mailboxes at 0xFFF0C0 and filled
# MCU_COINS with 0xB6 -- bits 2 and 7 -- which irq4_handler then waited on forever.
# 14 sites (sound_enqueue, FUN_00002E50 and their copies), all this one constant.
#
# Translate the CONSTANT, not the local: when the other operand is a pointer local
# that earlier passes already mapped, the constant must live in the same domain. The
# one site whose other operand is a CELL in the 68K domain (the palette ring's
# `0xfffff7ff < _DAT_fffff402`) is left alone by construction -- the pattern requires
# a p*Var local -- and its own deref is mapped below instead.
_cmpk = [0]
def _cmp_const(m):
    _cmpk[0] += 1
    return '%s(%s *)W68PTR(0x%06Xu)' % (m.group(1), m.group(2), int(m.group(3), 16) & 0xFFFFFF)
out = [re.sub(r'(\b(?:p[a-zA-Z]*Var\d+)\s*(?:==|!=|<=|>=|<|>)\s*)\((char|undefined\d?|int|short|byte|ushort|uint)\s*\*\)\s*0x(ffff[0-9a-f]{4})\b',
              _cmp_const, _c) for _c in out]
_pass_report('68K bound constants moved to the pointer domain', _cmpk[0])

# ---- THE PALETTE-QUEUE BUILDER DEREFERENCES A 68K-DOMAIN POINTER --------------------
#
#     piVar1 = (int *)(_DAT_fffff402 + (short)((ushort)DAT_fffff406 << 3));
#     if ((int *)0xfffff7ff < piVar1) piVar1 = piVar1 + -0x80;     <- correct as is
#     *piVar1 = ...;  piVar1[1] = ...;                              <- 0xFFFFF6xx on the SH-2
#
# _DAT_fffff402 is kept in the 68K domain on purpose (its ring test above needs it),
# so a pointer formed from it is a 68K address and its DEREFERENCE is the SH7604's
# on-chip peripheral file -- the exact hazard the drain hit last time. Map at the two
# stores only; the arithmetic and the bound stay 68K.
_pq = [0]
def _pqfix(txt):
    m = re.search(r'\b(\w+) = \(int \*\)\(_DAT_fffff402 \+', txt)
    if not m: return txt
    n = m.group(1)
    txt, k1 = re.subn(r'\n(\s*)\*%s = ' % re.escape(n), r'\n\1*(int *)A68NORM(%s) = ' % n, txt)
    txt, k2 = re.subn(r'\n(\s*)%s\[1\] = ' % re.escape(n), r'\n\1((int *)A68NORM(%s))[1] = ' % n, txt)
    _pq[0] += k1 + k2
    return txt
out = [_pqfix(_c) for _c in out]
_pass_report('palette-queue builder derefs mapped', _pq[0], 2)

_vidc = [0]
def _vidcat(m):
    _vidc[0] += 1
    return 'A68VID(0x400000u | (unsigned short)(%s))' % m.group(1)
out = [re.sub(r'CONCAT22\(\s*0x40\s*,\s*(.+?)\s*\)(?=\s*[;,)])', _vidcat, _l) for _l in out]
print('cport_prep: runtime tile-address constructions mapped: %d' % _vidc[0])
print('cport_prep: video literals rebased %s'
      % (', '.join('%s %d' % (k, v) for k, v in sorted(_vidn.items())) or 'none'))
if _vidtail:
    print('cport_prep: %d video literal(s) PAST a shadow, left alone: %s'
          % (len(_vidtail), ', '.join('0x%06X(%s)' % t for t in _vidtail[:8])))

# ---- FUN_0000f74a: THE THREE-SLOT SPAWNER THREADS A6 THROUGH ITS INSTALLS ----
# prog68k 0xF7A2-0xF8BE: after two FUN_0000e032 slot searches the 68K installs the
# template into ITSELF (`bsr f8c0` with fp = the spawner), then `movea.w ($78,fp),fp`
# and installs the first new slot, then `movea.w ($78,fp),fp` and the second -- and
# the second search's `move.w a5,($78,fp)` writes the NEW a5 into the first slot's
# link.  Ghidra kept A6 as the local iVar3 (so FUN_0000f8c0, request_palette_update
# and the +0x4a/+0xb stores after each install all ran on the register-file A6 = the
# spawner, three times over) and reused the FIRST search's A5 for the second link.
# Measured 2026-10-04 (rd103, tick 0x5A): the arcade's slots 18/19/20 (handlers
# 0xF90A/0xFD6C/0xFD6C, links 0/C900/C900) against the port's slot 18 with x/y and
# +0x20 written and no header, and 19/20 never installed.
_f74a = [0]
def _f74a_fix(_blk):
    if 'FUN_0000f74a()\n{' not in _blk:
        return _blk
    _blk, k1 = re.subn(r'puVar2\[0x3c\] = sVar1;',
                       r'puVar2[0x3c] = (short)A68OF16(unaff_A5);   /* the SECOND search\'s a5: cport_prep.py */', _blk, count=1)
    _blk, k2 = re.subn(r'(\n(\s+))iVar3 = \(int\)W68\(\*\(short \*\)\(iVar3 \+ 0x78\)\);',
                       r'\1iVar3 = (int)W68(*(short *)(iVar3 + 0x78));\1_r_unaff_A6 = (byte *)iVar3;   /* movea.w ($78,fp),fp: cport_prep.py */', _blk)
    _f74a[0] += k1 + k2
    return _blk
out = [_f74a_fix(_c) for _c in out]
_pass_report('FUN_0000f74a threads A6 through its three installs', _f74a[0], 3)

# ---- `movea.w (0x78,fp),fp`: THE A6 RE-POINT ALONG THE PAIR LINK --------------
#
# A record's +0x78 is its link to the next record of a multi-part enemy, and a
# composite's init walks it to fill each part in turn:
#
#     e3c8: move.w fp,-(sp)
#     e3ca: movea.w (0x78,fp),fp      <-- A6 IS NOW THE CHILD
#     e3ce: lea (0xebae,pc),a1 ; lea (0xed3c,pc),a2
#     e3d6: bsr 0xe42e                 the 12-byte template copier
#     e3f4: movea.w (0x78,fp),fp      <-- and the next one
#
# Ghidra DOES track that walk -- it emits `iVar8 = (int)W68(*(short *)(iVar8 + 0x78));`
# -- but into a LOCAL, and the callees reach their record through the GLOBAL
# _r_unaff_A6 (FUN_0000e42e: `byte *unaff_A6 = (byte *)_r_unaff_A6;` and it writes
# twelve bytes to *pbVar3; request_palette_update the same). So every part of the
# composite was written over the FIRST record.
#
# WHAT IT COST: handlers 0xE474 and 0xEA5E -- kind 5's three-record enemy -- never
# appear on the port, where an arcade harness saw them in 53 and 102 samples of level 1
# (and the DIPs match: port_l2.c:2504 writes MAME's own 0x54/0x66, measured 0xFFF01A =
# 0x66 / 0xFFF01B = 0x54 on a play dump, so the comparison is valid). The second and
# third templates overwrote the first's handler and the two allocated part-records were
# left LIVE WITH A NULL HANDLER -- the slots 18/19/20 triple seen in round 1, which the
# arcade never shows in 201 samples. FUN_00012b08, FUN_000132a0, FUN_00014a5a,
# FUN_00014a8c and FUN_00015306 all carry the same walk.
#
# A site whose next line already publishes is left alone: FUN_0000f74a's own pass above
# does two of these, and a double insert would be harmless but misleading.
_a678 = [0]
_A678_RE = re.compile(r'^(\s*)(\w+) = \(int\)W68\(\*\(short \*\)\((\w+) \+ 0x78\)\);\s*$')
# ALLOWLISTED, like the A5 re-read, and for the same reason: 6 of the 10 functions
# carrying this walk also RESTORE A6 before returning (`movea.w (sp)+,fp` --
# FUN_0000e316 x2, FUN_0000f74a, FUN_00012b08 x3, FUN_00014a8c x2, FUN_00015306,
# FUN_00015434), and publishing the WALK without publishing the RESTORE leaves the
# global pointing at the last child for everything after. Only FUN_0000e316 has earned
# its place -- it is the one with a measured payoff (handlers 0xE474 / 0xEA5E, f32fd553)
# and the one whose restore is handled just below.
_A678_OK = ('FUN_0000e316',)
def _a678_fix(_blk):
    # `out` can hold the whole corpus in ONE element, so track the current function by
    # its definition line rather than matching the chunk (the third time that has bitten).
    if '+ 0x78))' not in _blk:
        return _blk
    _lines = _blk.split('\n')
    _res, _cur = [], None
    for _i, _ln in enumerate(_lines):
        _dm = re.match(r'^(?:uint|ulonglong) (\w+)\(', _ln)
        if _dm:
            _cur = _dm.group(1)
        _res.append(_ln)
        if _cur not in _A678_OK:
            continue
        _m = _A678_RE.match(_ln)
        if not _m:
            continue
        _nxt = _lines[_i + 1] if _i + 1 < len(_lines) else ''
        if '_r_unaff_A6 =' in _nxt:
            continue
        _res.append('%s_r_unaff_A6 = (byte *)%s;   /* movea.w (0x78,fp),fp: cport_prep.py */'
                    % (_m.group(1), _m.group(2)))
        _a678[0] += 1
    return '\n'.join(_res)
out = [_a678_fix(_c) for _c in out]
_pass_report('A6 re-pointed along the +0x78 pair link, published (allowlisted)',
             _a678[0], 2)

# AND THE RESTORE. 0xE34E `move.w fp,-(sp)` / 0xE368 `movea.w (sp)+,fp` brackets the
# walk, and the port renders the pop as `unaff_A6 = (undefined2 *)W68(sVar9);` -- it
# puts the LOCAL back and leaves the GLOBAL on the last child, so every later callee
# that reads _r_unaff_A6 runs on the wrong record.
_a678r = [0]
def _a678_restore(_blk):
    if 'unaff_A6 = (undefined2 *)W68(' not in _blk:
        return _blk
    _lines = _blk.split('\n')
    _res, _cur = [], None
    for _ln in _lines:
        _dm = re.match(r'^(?:uint|ulonglong) (\w+)\(', _ln)
        if _dm:
            _cur = _dm.group(1)
        _res.append(_ln)
        if _cur not in _A678_OK:
            continue
        _rm = re.match(r'^(\s*)unaff_A6 = \(undefined2 \*\)W68\(\w+\);\s*$', _ln)
        if _rm:
            _res.append('%s_r_unaff_A6 = (byte *)unaff_A6;   /* movea.w (sp)+,fp: cport_prep.py */'
                        % _rm.group(1))
            _a678r[0] += 1
    return '\n'.join(_res)
out = [_a678_restore(_c) for _c in out]
_pass_report('  and the matching A6 restore published', _a678r[0], 1)

# ---- THE BOSS INSTALLERS: EACH HELPER CALL OWNS A SLOT AND A TABLE ENTRY ------
#
# prog68k 0x1769E (round 1's boss) is seven calls to one helper:
#
#     lea  0xffffd400,fp      ; fp = slot 40
#     movea.l fp,a4           ; a4 STAYS here, all seven calls
#     lea  (0x17e40,pc),a5    ; the record table
#     move.l #0x1794c,(2,fp)  ; this slot's handler
#     bsr  0x17774
#     ... field inits ...
#     lea  (0x80,fp),fp       ; SLOT 41
#     move.l #0x177d0,(2,fp)
#     bsr  0x17774
#     ... five more ...
#
# and the helper walks A5 by TEN BYTES a call -- `move.w (a5)+,(6,fp)`,
# `add.w (a5)+,d0`, `move.w (a5)+,d0`, `move.l (a5)+,(48,fp)` (0x1777A, 0x177A4,
# 0x177B2, 0x177BC). FUN_000180c2 is the same helper for the later rounds, same
# ten bytes (0x180C8, 0x180EE, 0x180FC, 0x1810C).
#
# GHIDRA RESOLVED THE A6 WALK FOR THE FIELD STORES AND NOT FOR THE CALLS: the
# handler longwords land at _DAT_ffffd402, d482, d502 ... correctly, while every
# call passes `&DAT_ffffd400` and the table's FIRST entry, and only the first call
# gets an A6 (from docs/audit/a6_callsites.json). So slot 40 is re-initialised
# seven times and slots 41-46 are never touched.
#
# WHAT IT COSTS: Neff. He sits in 0x1794C, which steps +0x3E by 2 every 8 frames
# until it EQUALS 26. The installer writes `move.b #0,(62,fp)` at 0x176C8, which is
# AFTER the first call -- but the re-runs land the helper's `move.w #0x708,(62,fp)`
# (0x177C4) after it, so +0x3E starts at 0x07, stays ODD, never equals 26, and the
# transform hangs. Mike's sw266 states read +0x3E = 0xAF with 41-46 dead; the
# arcade's 41-46 go 0x8000 at the install.
#
# THE RULE IS THE SHAPE, not these two functions: the slot base is the address of
# the handler store that precedes the call, which is always base+2 because the
# records are 0x80 apart ((addr & 0x7F) == 2). The cursor index resets at each
# `case` label, since Ghidra turned the 0x17E24 dispatch into a switch and the
# cases are alternatives. A call the A6 pass already wrapped is LEFT ALONE -- it is
# the first of its chain, so its base is right and its cursor offset is zero.
_BOSS_HELPERS = {'FUN_00017774': 10, 'FUN_000180c2': 10}   # A5 bytes per call
# THE CASTS ARE NOT THERE YET at this point in the pipeline -- gcc's feedback loop
# adds them later, so the call reads `FUN_000180c2(&DAT_ffffd400,0x186fa);`. Match the
# argument list whole and split it at the first top-level comma, which works before and
# after the casts land.
_BOSS_CALL = re.compile(r'^(\s*)(FUN_00017774|FUN_000180c2)\((.+)\);\s*$')
def _boss_split(_args):
    _d = 0
    for _i, _ch in enumerate(_args):
        if _ch in '([':
            _d += 1
        elif _ch in ')]':
            _d -= 1
        elif _ch == ',' and _d == 0:
            return _args[:_i], _args[_i + 1:]
    return None, None
_BOSS_STORE = re.compile(r'^_?DAT_(ffff[0-9a-f]{4}) = 0x[0-9a-f]+u;$')
_boss = [0, 0]
def _boss_fix(_blk):
    if not any(_h + '(' in _blk for _h in _BOSS_HELPERS):
        return _blk
    # `out` can hold the whole corpus in ONE element, so this walks every function
    # and the cursor state has to reset at the banner as well as at a case label.
    _res, _base, _k, _pend = [], None, 0, False
    for _ln in _blk.split('\n'):
        _s = _ln.strip()
        if _ln.startswith('/* ---- 0x') or re.match(r'(case \d+:|default:)$', _s):
            _base, _k, _pend = None, 0, False
        _m = _BOSS_STORE.match(_s)
        if _m:
            _a = int(_m.group(1), 16)
            if (_a & 0x7F) == 2:
                _base = _a - 2
        if '_r_unaff_A6 = (byte *)W68PTR(' in _ln:
            _pend = True
        _mc = _BOSS_CALL.match(_ln)
        _a4 = _a5 = None
        if _mc:
            _a4, _a5 = _boss_split(_mc.group(3))
        if _mc and _a5 is not None and _base is not None:
            _ind, _fn = _mc.group(1), _mc.group(2)
            if _pend:                                 # a6_callsites got this one
                _pend = False
                _res.append(_ln); _k += 1; _boss[1] += 1
                continue
            _off = _BOSS_HELPERS[_fn] * _k
            # A BARE 68K ADDRESS STAYS IN THE INTEGER DOMAIN -- the later passes map it,
            # and the map is address-additive, so the offset belongs on the address.
            _lit = re.match(r'^\s*(0x[0-9a-fA-F]+)\s*$', _a5)
            if not _off:
                _arg = _a5
            elif _lit:
                _arg = '0x%x' % (int(_lit.group(1), 16) + _off)
            else:
                _arg = '(undefined2 *)((byte *)(%s) + %d)' % (_a5.strip(), _off)
            _res.append('%s{   /* lea 0x80(fp),fp + the A5 cursor: cport_prep.py */' % _ind)
            _res.append('%s    byte *_a6sv = _r_unaff_A6;' % _ind)
            _res.append('%s    _r_unaff_A6 = (byte *)W68PTR(0x%06Xu);' % (_ind, _base & 0xFFFFFF))
            _res.append('%s    %s((int)(%s),%s);' % (_ind, _fn, _a4, _arg))
            _res.append('%s    _r_unaff_A6 = _a6sv;' % _ind)
            _res.append('%s}' % _ind)
            _k += 1; _boss[0] += 1
            continue
        _res.append(_ln)
    return '\n'.join(_res)
out = [_boss_fix(_c) for _c in out]
_pass_report('boss installers: helper calls given their own slot and table entry', _boss[0], 22)

# ---- A CALLEE THAT RETURNS WITH FP ADVANCED MUST SAY SO -----------------------
#
# Three `lea (0x80,fp),fp` in the program sit immediately before an `rts`, so the
# routine hands its caller a NEW record base and the caller carries on from it:
#
#     0x6162 / 0x62B0   FUN_000060e6   (its two paths)
#     0x19FCC           FUN_00019f86
#
# Ghidra drops a trailing lea -- there is no C statement for "and the frame pointer
# is now 0x80 higher on the way out" -- so the port returned with _r_unaff_A6 exactly
# where it came in.
#
# WHAT IT COST (advisor, Mike's sw269 state): FUN_00005efc runs 0x5F16, 0x60E6, 0x5F16
# from 0xFFD980 with NO lea between them (0x5EFC-0x5F14). 0x60E6 is the round-clear
# release object and installs handler 0x6168, the ONLY writer of 0xFFF14B
# (`move.b #1,0xfffff14b`, 0x61AA). The player in 0x59D2 waits on that byte; it read 0
# and the round clear after Neff hung. hand/FUN_00005efc.c threads A6 through the three
# calls and hand/FUN_00005f16.c writes its own +6*0x80 back; this closes 0x60E6.
#
# VERIFIED PER FUNCTION, not inferred from the shape: each entry below is a routine
# whose every return path is one of those three tails, so the advance is exactly 0x80.
# A routine with a trailing lea on SOME path only would need its returns split and is
# not listed.
_A6WB = {'FUN_000060e6': 0x80, 'FUN_00019f86': 0x80}
_a6wb = [0, 0]
def _a6wb_fix(_blk):
    # `out` can hold the whole corpus in ONE element, so track the function by its
    # DEFINITION line rather than assuming one function per chunk.
    if not any(_n in _blk for _n in _A6WB):
        return _blk
    _res, _cur, _armed = [], None, False
    for _ln in _blk.split('\n'):
        _dm = re.match(r'^(?:uint|ulonglong) (\w+)\(', _ln)
        if _dm:
            _cur = _dm.group(1) if _dm.group(1) in _A6WB else None
            _armed = False
        if _cur and not _armed and \
           re.match(r'^\s*undefined2 \*unaff_A6 = \(undefined2 \*\)_r_unaff_A6;', _ln):
            _res.append(_ln)
            _res.append('  byte *_a6wb_entry = _r_unaff_A6;   /* trailing lea: cport_prep.py */')
            _armed = True
            _a6wb[1] += 1
            continue
        _rm = re.match(r'^(\s*)return\b', _ln)
        if _cur and _armed and _rm:
            _res.append('%s_r_unaff_A6 = _a6wb_entry + 0x%X;   /* lea (0x80,fp),fp before the rts */'
                        % (_rm.group(1), _A6WB[_cur]))
            _a6wb[0] += 1
        _res.append(_ln)
        if _ln == '}':
            _cur, _armed = None, False
    return '\n'.join(_res)
out = [_a6wb_fix(_c) for _c in out]
_pass_report('callees that return with fp advanced: writebacks inserted', _a6wb[0], 3)
_pass_report('  functions given an entry-A6 anchor', _a6wb[1], 2)

# ---- FUN_00002ee4: THE movem.w RESTORE READS THE WRONG STACK SLOTS ---------------
#
# prog68k 0x2FA6-0x2FB2:
#     movem.w d0-d2/a0-a1,-(sp) ; moveb #-67,d0 ; bsr 0x2DF2
#     movem.w (sp)+,d0-d2/a0-a1 ; bra 0x3052
#
# `movem.w` moves WORDS, so after the push the stack is D0, D1, D2, A0, A1 two bytes
# apart -- and the generated READS are right: reg+0, reg+2, reg+4. THE STORES are the
# ones at a four-byte stride (auStack_14[0], uStack_10 at -0x10, uStack_c at -0xc), so
# reg+2 lands on -0x12, which holds nothing, and D1/D2 came back as rubbish. D0 is the
# one that matters: it is clobbered by `p_D0 = CONCAT31(uVar3,0xbd)` between the save
# and the restore, and the restore is the only thing that brings it back.
#
# THE READS ARE REPOINTED AT THE LOCALS, not the stores widened: auStack_14 is declared
# `undefined2 [2]`, so storing D1/D2 at index 1 and 2 would run off it. Three lines,
# each unique in the corpus. (Advisor's call, 2026-10-20: narrow rather than hand-derive
# a switch this size.)
_e4 = [0]
def _ee4_fix(_t):
    for _old, _new in (
        ('p_D0 = (int)*(short *)register0x0000003c;',
         'p_D0 = (int)(short)auStack_14[0];   /* movem.w slot 0: cport_prep.py */'),
        ('p_D1 = (uint)*(short *)((int)register0x0000003c + 2);',
         'p_D1 = (uint)(short)uStack_10;   /* movem.w slot 1 (stored at -0x10) */'),
        ('p_D2 = (uint)*(short *)((int)register0x0000003c + 4);',
         'p_D2 = (uint)(short)uStack_c;   /* movem.w slot 2 (stored at -0xc) */')):
        if _old in _t:
            _t = _t.replace(_old, _new, 1)
            _e4[0] += 1
    return _t
out = [_ee4_fix(_c) for _c in out]
_pass_report('FUN_00002ee4: movem.w restores repointed at the slots the stores used',
             _e4[0], 3)
_pass_report('  boss installer calls the A6 pass already wrapped', _boss[1], 2)
# ---- FUN_0000a1e0: THE PARTNER'S PALETTE REQUEST RUNS ON A6 = THE PARTNER ----
# prog68k 0xA270: `move.l fp,-(sp) ; movea.l a5,fp ; jsr 3b2e ; moveq #-72,d0 ; jsr 3352 ;
# movea.l (sp)+,fp` -- request_palette_update allocates for the object in A6, and A6 is
# the partner (fp+0x80) for that call.  Ghidra wrote `request_palette_update();` with
# nothing moved, so the port allocated for the hero's colour set again (already held)
# and the partner's +0xa slot stayed 0: arcade FFF480 `.. 004b 0050`, port `.. 004b
# ffff`; arcade partner pal 0550, port 0050 (tick 0x46, rd103, 2026-10-04).  This is
# one of 66 `movea ..,fp ; jsr` sites in the program (19 of them call 0x3B2E); the
# general mechanism is an open card.
_a1e0 = [0]
def _a1e0_fix(_blk):
    if 'FUN_0000a1e0()\n{' not in _blk:
        return _blk
    _blk, k = re.subn(r'(\n(\s+))request_palette_update\(\);\n\s+sound_enqueue\(0xffffffb8\);',
                      r'\1_r_unaff_A6 = (byte *)(unaff_A6 + 0x80);   /* movea.l a5,fp: cport_prep.py */\1request_palette_update();\1sound_enqueue(0xffffffb8);\1_r_unaff_A6 = (byte *)unaff_A6;   /* movea.l (sp)+,fp */', _blk, count=1)
    _a1e0[0] += k
    return _blk
out = [_a1e0_fix(_c) for _c in out]
_pass_report('FUN_0000a1e0 palette request on the partner', _a1e0[0], 1)
# ---- FUN_0000fd6c: THE HIT PATH's SCRIPT POINTER IS THE A1 LOADED AT 0xFF10 ----
# prog68k 0xFF10 `movea.l ($24,fp),a1` ... `jsr 3352` (a0/a1 saved around it) ...
# `jsr 3f20` (integrate_position: d0 only) ... 0xFF6E `subq.l #2,a1 ; adda.w -(a1),a1 ;
# move.l a1,($24,fp)`. Ghidra lost A1 across the calls and wrote `iVar14 =
# extraout_A1_00 + -2` -- the register-file pseudo, 0 -- so the child zombie's script
# became 0xFFFFFFFC (rd110 2026-10-04, slots 17/19 after the parent's hit).
_fd6c = [0]
def _fd6c_a1(_blk):
    if 'FUN_0000fd6c(' not in _blk:
        return _blk
    _blk, k = re.subn(r'iVar14 = extraout_A1_00 \+ -2;', r'iVar14 = LD32S((unaff_A6 + 0x24)) + -2;   /* a1 = ($24,fp) from 0xFF10: cport_prep.py */', _blk)
    _fd6c[0] += k
    return _blk
out = [_fd6c_a1(_c) for _c in out]
_pass_report('FUN_0000fd6c hit path script pointer', _fd6c[0], 1)
# ---- MEASUREMENT: CPORT_PROBE stamps the tick and named locals at matching lines ----
# CPORT_PROBE=<fn addr> CPORT_PROBE_AT='<line substring>' CPORT_PROBE_VARS='a,b,c': in
# that function, before every line containing the substring, store the tick (|0x10000)
# and the listed locals into port_probe_<8*site+k> (uint globals, read back from an
# ares SDRAM dump at the ELF's _port_probe_0). Without CPORT_PROBE_AT the sites are the
# `goto LAB_00010700;` entries (the first use, 2026-10-04). Never on a candidate build.
_PROBE = (os.getenv('CPORT_PROBE') or '').strip()
if _PROBE:
    _pat = os.getenv('CPORT_PROBE_AT') or 'goto LAB_00010700;'
    _vars = [v for v in (os.getenv('CPORT_PROBE_VARS') or '').split(',') if v.strip()]
    _pr = [0]
    def _probe_fn(_blk):
        # CPORT_PROBE is an address (FUN_0000xxxx) or, for a function Ghidra named, that
        # name -- request_palette_update is 0x3B2E and has no FUN_ spelling to match.
        _key = ('FUN_%s(' % _PROBE.zfill(8)) if re.fullmatch(r'[0-9a-fA-F]{1,8}', _PROBE) else (_PROBE + '(')
        if _key not in _blk:
            return _blk
        lines = _blk.split('\n'); o = []
        for l in lines:
            if _pat in l and not l.lstrip().startswith('/*'):
                ind = re.match(r'\s*', l).group(0)
                k = 8 * _pr[0]; _pr[0] += 1
                st = ['port_probe_%d = (uint)*(ushort *)W68PTR(0xFFF02Au) | 0x10000u;' % k]
                st += ['port_probe_%d = (uint)(%s);' % (k + 1 + n, v) for n, v in enumerate(_vars[:7])]
                o.append(ind + ' '.join(st) + '   /* CPORT_PROBE */')
            o.append(l)
        return '\n'.join(o)
    out = [_probe_fn(_c) for _c in out]
    sys.stderr.write('cport_prep: CPORT_PROBE=%s at %r: %d sites stamped\n' % (_PROBE, _pat, _pr[0]))
# ---- SCRIPT POINTERS GHIDRA TYPED AS CODE: THE 68K VALUE, NOT THE SH-2 SYMBOL ----
# 0x113F8 is both a function and the flame object's script, so Ghidra writes
# `pcVar15 = FUN_000113f8; *(code **)(unaff_A6 + 0x24) = pcVar15;` -- the SH-2 address
# of the function (0x0238DD40) stored where the 68K keeps 0x113F8. rd112 2026-10-04:
# the hero's special-attack flame (slot 18, 0x10DD4) had +0x24 = 0238DD40, y = F77F,
# and never touched zombie 49 (arcade: 8 damage every 8 ticks from tick 0x206).
# Nine sites: the bare-symbol assignment, the store, the load and the script-offset
# form `p + -2 + *(short *)(p + -2)`.
_cp = [0]
def _code_values(_blk):
    k = 0
    _blk, n = re.subn(r'\b(pcVar\d+) = FUN_([0-9a-f]{8});', lambda m: '%s = (code *)0x%xu;   /* the 68K address: cport_prep.py */' % (m.group(1), int(m.group(2), 16)), _blk); k += n
    # the same for any other local (`p_A0 = FUN_0001762c;` -- the director's script)
    _blk, n = re.subn(r'\b(p_A\d|puVar\d+|iVar\d+|uVar\d+) = FUN_([0-9a-f]{8});', lambda m: '%s = (typeof(%s))0x%xu;   /* the 68K address: cport_prep.py */' % (m.group(1), m.group(1), int(m.group(2), 16)), _blk); k += n
    _blk, n = re.subn(r'\*\(code \*\*\)\(([^;=]+?)\) = (pcVar\d+) \+ -2 \+ \*\(short \*\)\(\2 \+ -2\);', r'ST32((\1), ((uint)\2 - 2 + *(short *)A68PTR((uint)\2 - 2)));   /* script - 2 + word: cport_prep.py */', _blk); k += n
    _blk, n = re.subn(r'\*\(code \*\*\)\(([^;=]+?)\) = (pcVar\d+);', r'ST32((\1), ((uint)\2));', _blk); k += n
    _blk, n = re.subn(r'\b(pcVar\d+) = \*\(code \*\*\)\(([^;]+?)\);', r'\1 = (code *)LD32U(\2);', _blk); k += n
    _cp[0] += k
    return _blk
out = [_code_values(_c) for _c in out]
_pass_report('code-typed script pointers kept as 68K values', _cp[0], 9)
# ---- THE ENEMY AI's PLAYER-DISTANCE HELPERS RETURN D0 AND THE CARRY ----------------
# prog68k 0xE23C/0xE266/0xE2C2/0xE2DC are called by every enemy handler to pick a
# player and measure the distance; their results are D0 (a word) and the carry, and
# Ghidra modelled neither: FUN_0000e23c returned CONCAT44(A0, D1) and the callers took
# `extraout_D0w = (uint)result` (D1), FUN_0000e2dc returned A0 on both paths with the
# carry placeholder `bVarN = false` never written, FUN_0000e266 chose its pointer from
# a distance.  Measured 2026-10-04 (rd103, slot 16 = the first demo zombie pair): the
# arcade stays in state 0 for tens of ticks (hero 0xE8 px away); the port took the
# "within 0x30 px" branch on its first tick (state 2, script 0x10A5A, dy 0x80) because
# `extraout_D0w_00 < 0x30` read a stale pseudo-register.  The four are hand-derived from
# the 68K; D0 is published in port_e_d0 and the carry in port_carry, and every call
# site below is rewritten to read them where the 68K reads D0/C.
#   0xE2C2 (A0 slot): D0 = slot live ? |slot.x40 - self.x40| : 0xFFFF.
#   0xE266: d1 = E2C2(C400); d0 = E2C2(C000); a0 = C000, a1 = C400; if (d0 > d1)
#           exg a0,a1; live(a0) ? (chasers[a0] < 2 ? D0 = a0 (the 68K word) : D0 = 0)
#           : D0 = 0xFFFF; self.+0x7c = D0; return A0.  (FFF156/FFF157 = the per-player
#           chaser counts; the entry decrements the one named by +0x7c.)
#   0xE2DC (A0 target): d1 = a0.2f; if (d1 == self.2f) C = 0 else { d1 += self.2f;
#           C = d1 < 3; d1 -= 3 }; return A0.
#   0xE23C (A0 target): d0 = a0.x40 - self.x40; borrow ? (d0 = -d0, C = !facing)
#           : C = facing, facing = self.2e bit 7; return CONCAT44(A0, d0).
_ai = [0, 0, 0, 0]
_AI_CALL = re.compile(r'FUN_000(0e266|0e2dc|0e23c|0f77c|0d776|0dbb8|100b0|154c0|17bf8|1953c)\(')
_AI_D0 = ('0e266', '0e2dc', '0e23c')
_AI_CF = ('0e2dc', '0e23c', '0f77c', '0d776', '0dbb8', '100b0', '154c0', '17bf8', '1953c')
def _ai_sites(_blk):
    lines = _blk.split('\n')
    o = []
    i = 0
    n = len(lines)
    while i < n:
        l = lines[i]
        o.append(l)
        m = _AI_CALL.search(l)
        # A CALL WHOSE ARGUMENTS RUN OVER SEVERAL LINES is still one statement (2026-10-25:
        # 0x13B2C's `jsr 0xe23c` at 0x13F60 spans three lines and was skipped, so neither
        # its D0 (`extraout_D0w_03 < 0x34`) nor its carry was ever written).
        k = i
        if m and not re.match(r'\s*(uint|ulonglong|void) FUN_', l) and not l.rstrip().endswith(';') \
           and re.match(r'\s*(?:\w+ = )?FUN_000\w+\(', l):
            d = l.count('(') - l.count(')')
            while d > 0 and k + 1 < n:
                k += 1
                d += lines[k].count('(') - lines[k].count(')')
            if d != 0 or not lines[k].rstrip().endswith(';'):
                k = i
        if not m or re.match(r'\s*(uint|ulonglong|void) FUN_', l) or not lines[k].rstrip().endswith(';'):
            i += 1; continue
        for q in range(i + 1, k + 1):
            o.append(lines[q])
        ind = re.match(r'\s*', l).group(0)
        kind = m.group(1)
        am = re.match(r'\s*(\w+) = FUN_0000e', l)
        avar = am.group(1) if am else None
        i = k
        # (1) `extraout_D0.. = (uint)avar;` on the following lines -> the published D0
        j = i + 1
        d0_done = False
        # (or one the extraout feed already spelled from port_e_d0, _XO_PUB)
        while kind in _AI_D0 and j < n and (
                (avar and re.match(r'\s*extraout_D0\w* = \(uint\)%s;' % re.escape(avar), lines[j])) or
                re.match(r'\s*extraout_D0\w* = \(ushort\)port_e_d0;   /\* D0 after the call', lines[j])):
            lines[j] = re.sub(r'= \(uint\)%s;' % re.escape(avar or '#'), '= (ushort)port_e_d0;   /* D0 after the call: cport_prep.py */', lines[j])
            _ai[1] += 1; d0_done = True
            o.append(lines[j]); j += 1
        # (1b) 0xE266 leaves A2 on the chaser byte; a caller that took A2 as a parameter or
        #      pseudo-register reads it after the call (FUN_000103e0 `if (1 < *p_A2)`: the
        #      second pair of the demo stopped in state 3 at tick ~0x170 on a stale A2,
        #      rd114 2026-10-04, x 0x1C short by tick 0x1FE).
        if kind == '0e266':
            # `jsr e266 ; beq` (0x10650, 0x11C20, 0x14DBA, 0x156EC, 0x15832, 0x166FE): the Z of
            # e266's D0 (0 = the chosen player already has two chasers). Ghidra tests a bool it
            # computed BEFORE the call (`bVar16 = (bVar6 & 0xc0) == 0; ... e266(); if (bVar16)`),
            # so the port always took the "D0 == 0" path: with player 2 in state 0x14 at tick
            # 0x161 that path reads "target busy -> wait" and the demo's second pair stopped
            # (rd117 probe, 2026-10-04). The bool right after the call IS the flag.
            for k in range(j, min(n, j + 4)):
                if _AI_CALL.search(lines[k]):
                    break
                _new, _k = re.subn(r'^(\s*if \(+)([bu]Var\d+)(\)(?: \{|\s*\|\|))', r'\1(port_e_d0 == 0)\3', lines[k], count=1)
                if _k:
                    lines[k] = _new; _ai[2] += 1
                    break
            for _nm in ('p_A2', 'unaff_A2'):
                if re.search(r'\b%s\b' % _nm, _blk):
                    o.append('%s%s = (typeof(%s))port_e_a2;   /* A2 after 0xE266: cport_prep.py */' % (ind, _nm, _nm))
                    _ai[1] += 1
        # (2) the carry placeholder tested within the next 4 lines
        if kind in _AI_CF:
            _ph = None
            for k in range(i - 1, max(-1, i - 5), -1):   # the placeholder assigned false/0 just before
                pm = re.match(r'\s*([bu]Var\d+) = (?:false|0);\s*$', lines[k])
                if pm:
                    _ph = pm.group(1); break
            if not _ph:
                for k in range(j, min(n, j + 4)):   # or tested just after
                    cm = re.search(r'if \(+!?\(?(?:\(bool\))?(in_CF|[bu]Var\d+)\)', lines[k])
                    if cm:
                        _ph = cm.group(1); break
                    if _AI_CALL.search(lines[k]):
                        break
            if _ph:
                o.append('%s%s = (byte)port_carry;   /* the callee\'s carry: cport_prep.py */' % (ind, _ph))
                _ai[2] += 1
        # (3) a D0 read with no assignment within the next 6 lines
        if kind in _AI_D0 and not d0_done:
            for k in range(j, min(n, j + 6)):
                if _AI_CALL.search(lines[k]) and k != i:
                    break
                rm = re.search(r'\b(extraout_D0w?(?:_\d+)?)\b(?!\s*=[^=])', lines[k])
                if rm:
                    o.append('%s%s = (ushort)port_e_d0;   /* D0 after the call: cport_prep.py */' % (ind, rm.group(1)))
                    _ai[3] += 1
                    break
        i = j
    _blk = '\n'.join(o)
    # (4) the comma form inside a condition: `(x = FUN_0000e2dc(a), bVarN)`
    _blk, k = re.subn(r'\((\w+) = (FUN_0000e2dc|FUN_0000e23c|FUN_0000f77c|FUN_000154c0|FUN_000100b0)\(([^()]*)\), ([bu]Var\d+)\)',
                      r'(\1 = \2(\3), (\4 = (byte)port_carry))', _blk)
    _ai[2] += k
    # `notw d0 ; beq` after e266 = D0 == 0xFFFF (no live player): Ghidra's `extraout_D0w == -1`
    # compares a ushort promoted to int with -1 and is never true; the byte form likewise.
    _blk, k = re.subn(r'\b(extraout_D0w(?:_\d+)?) (==|!=) -1\b', r'\1 \2 0xffff', _blk); _ai[1] += k
    _blk, k = re.subn(r'\b(extraout_D0b(?:_\d+)?) (==|!=) -1\b', r'\1 \2 0xff', _blk); _ai[1] += k
    # `(FUN_0000f77c(a,b), !bVarN)` and `(FUN_0000e032(), bVarN)` inside a condition
    _blk, k = re.subn(r'\((FUN_0000f77c|FUN_0000e2dc|FUN_0000e23c|FUN_000154c0|FUN_000100b0|FUN_00017bf8)\(([^()]*)\), (!?)([bu]Var\d+)\)',
                      r'(\1(\2), (\4 = (byte)port_carry), \3\4)', _blk)
    _ai[2] += k
    # 0xE032 moves A5 to the record it found; the statement-form callers re-read it
    # (the pair-allocator pass), the comma form must too: FUN_0000f74a's FIRST search
    # is `(FUN_0000e032(), bVar5)` and its result went into `sVar1 = A68OF16(unaff_A5)`
    # from the register file -- the pair spawner's A5, slot 17, so the trio's first
    # child overwrote the pair's second (rd110 2026-10-04: 17 = 0x0824 at tick 0x51,
    # 0xFD6C at 0x52).
    _rr = ', (unaff_A5 = (typeof(unaff_A5))_r_unaff_A5)' if 'unaff_A5' in _blk else ''
    _blk, k = re.subn(r'\(FUN_0000e032\(\), (!?)([bu]Var\d+)\)',
                      r'((\2 = (byte)FUN_0000e032())%s, \1\2)' % _rr, _blk)
    _ai[2] += k
    return _blk
out = [_ai_sites(_c) for _c in out]
_pass_report('AI helpers: D0 assignments redirected', _ai[1], 10)
_pass_report('AI helpers: carry placeholders written', _ai[2], 10)
# 11 before 2026-10-25; the extraout feed now places most of these itself (_XO_PUB)
_pass_report('AI helpers: D0 reads published', _ai[3], 2)
_ai4 = [0]
def _ai_callers(_blk):
    # FUN_0000db98: `jsr dbb8` consumes 12 bytes of the part list through A4 per call
    _blk, k1 = re.subn(r'FUN_0000dbb8\(puVar1 \+ 1,p_D3,p_D6,uVar2\);',
                       r'puVar1 = (undefined2 *)FUN_0000dbb8(puVar1 + 1,p_D3,p_D6,uVar2) - 1;   /* A4 advanced by the callee: cport_prep.py */', _blk)
    # FUN_00017bf8 callers: `move.l a5,($6c,fp)` stores the record it found
    _blk, k2 = re.subn(r'(FUN_00017bf8\(\);\n(\s+)(?:bVar\d+ = \(byte\)port_carry;[^\n]*\n\s+)?if \(bVar\d+\) \{\n\s+)ST32\(\(unaff_A6 \+ 0x6c\), \(p_A5\)\);',
                       r'\1ST32((unaff_A6 + 0x6c), (0xFFFF0000u | (ushort)A68OF16((byte *)_r_unaff_A5)));   /* the record 0x17BF8 found: cport_prep.py */', _blk)
    _ai4[0] += k1 + k2
    return _blk
out = [_ai_callers(_c) for _c in out]
_pass_report('AI helpers: callers continue from A4 / store A5', _ai4[0], 3)
# ---- TEMPLATE INSTALLERS COPY BYTES; SYMBOL ADDRESSES STORED AS VALUES ARE 68K ----
# FUN_0000f8c0 / FUN_00014b5e / FUN_00010394 copy a 12-byte object header from a ROM
# template through `*(undefined4 *)pbVar3 = *p_A1`.  The templates FUN_0000f74a names
# (0x10112, 0x1011E, 0x1012A) are 2 mod 4: the SH-2 load masked the address and the
# header landed two bytes early (rd106, slot 18: +0 FBB8, +2 80000000, +9 3D -- the
# template is 8000 0000F90A 013D 0002 0028).  Byte copies.  The same spawner passes
# its second and third scripts as `&PTR_PTR_LAB_..._00010246` -- cport_cast.py derives
# the integer cast and rewrites it to the 68K value 0x10246 (the cast exists only
# after that stage).
_ti = [0, 0]
def _template_bytes(_blk):
    for _fn in ('FUN_0000f8c0', 'FUN_00014b5e', 'FUN_00010394'):
        m = re.search(r'uint %s\([^)]*\)\s*\n\{.*?\n\}' % _fn, _blk, re.S)
        if not m:
            continue
        body, k = re.subn(r'\*(?:\(undefined4 \*\))?(\w+) = \*(p_A1);',
                          r'{ byte *_d = (byte *)\1; const byte *_s = (const byte *)\2; _d[0] = _s[0]; _d[1] = _s[1]; _d[2] = _s[2]; _d[3] = _s[3]; }   /* 2 mod 4 templates: cport_prep.py */', m.group(0), count=1)
        _ti[0] += k
        _blk = _blk[:m.start()] + body + _blk[m.end():]
    return _blk
out = [_template_bytes(_c) for _c in out]
_pass_report('template installers copy bytes', _ti[0], 3)
# ---- THE CREDIT TAKER'S CARRY (FUN_00001192) --------------------------------------------
# prog68k 0x1192 takes a credit for the player A0 points at and answers in the CARRY
# (`move #1,CCR` at 0x11E4 = taken, `move #0,CCR` at 0x11EA = refused). Its callers test
# nothing else: 0xFA6 `bsr 0x1192 ; bcc 0x1028` (P1, FUN_00000ece) and 0x10FC `bsr 0x1192 ;
# bcc 0x10a2` (P2). Ghidra returned D0 and let both callers test a `bVar4` it had set false
# before the call, so the credit was never taken and the restart never ran (Mike on the
# rig, sw292, 2026-10-22: "when the player loses the last life the game can't pick up the
# credits and start again"). hand/FUN_00001192.c returns the carry; this makes the callers
# test it. On success D0 is `moveq #0` (0x11D2), so the P1 site's `uVar2 = extraout_D0u`
# (D0's high word) becomes 0 there and keeps its value on failure, as on the 68K.
_cc = [0]
def _credit_carry(txt):
    txt, k1 = re.subn(r'\(FUN_00001192\(((?:[^()]|\([^()]*\))*)\), p_D1 = extraout_D1_\w+, uVar2 = extraout_D0u_\w+, bVar4\)',
                      r'(FUN_00001192(\1) ? (uVar2 = 0, 1) : 0)   /* bsr 0x1192 ; bcc: the carry, D0 = 0 on success: cport_prep.py */', txt)
    txt, k2 = re.subn(r'\(FUN_00001192\(((?:[^()]|\([^()]*\))*)\), bVar4\)',
                      r'FUN_00001192(\1)   /* bsr 0x1192 ; bcc: the carry: cport_prep.py */', txt)
    _cc[0] += k1 + k2
    return txt
out = [_credit_carry(_c) for _c in out]
_pass_report('credit taker FUN_00001192: callers test its carry', _cc[0], 2)
# ---- THE HUD ROW CLEAR ADVANCES A0 (0x1180, 2026-10-25) ----------------------------------
# prog68k 0x1180 is `lea a0@(128),a0 ; movea.l a0,a1 ; 8 x clr.l (a1)+ ; rts`: it clears the
# row BELOW a0 and returns with A0 on that row, so `bsr 0x1180 ; bsr 0x1180` (0xF42, 0xF7A,
# 0xFDA P1; 0x109A, 0x10D0, 0x1130 P2) clears two consecutive rows. Ghidra passed the second
# call an uninitialised `p_A0_N`, so the "CONTINUE n" row (the second) was never cleared and
# the name entry drew over "CONTINUE 0" (game-over lane, ares vs MAME). The second call
# takes the first call's argument + 0x80.
_rc = [0]
def _rowclr_a0(txt):
    txt, k = re.subn(r'FUN_00001180\(([^;\n]+)\);\n(\s*)FUN_00001180\(p_A0\w*\);',
                     r'FUN_00001180(\1);\n\2FUN_00001180((\1) + 0x80);   /* A0 advanced by 0x1180: cport_prep.py */', txt)
    _rc[0] += k
    return txt
out = [_rowclr_a0(_c) for _c in out]
_pass_report('0x1180 row clears: the second call takes the advanced A0', _rc[0], 6)
# ---- THE SPRITE-LIST RECORD ADDRESS IS A 16-BIT SUM (2026-10-23) -------------------------
# `lsl #4,d6 ; addw #$F800,d6 ; movea.w d6,a0` at 0x3EB8 (the record builder) and 0x3F04
# (hide_object_sprite): the decompile wrote the sum in 32 bits, so an index of 0x80 or more
# -- the 0xFF the despawn walk leaves on an unindexed record -- wrote 16 bytes into .ramtext
# at 0x313F0 instead of into program ROM where the 68000 drops them. Nine sites carry the
# shape (hide_object_sprite is inlined at its `bra 3f04` tail calls); w68.h SPRREC keeps
# the 16-bit map. The standalone hide_object_sprite is a hand file and keeps its own.
_sr = [0]
def _spr_rec(txt):
    txt, k = re.subn(r'\(W68PTR\(0xFFF800u\) \+ \(\(ushort\)(\w+)\[8\] \* 0x10\)\)',
                     r'SPRREC((ushort)\1[8])   /* lsl #4 ; addw #$F800 ; movea.w: a 16-bit sum, cport_prep.py */', txt)
    _sr[0] += k
    return txt
out = [_spr_rec(_c) for _c in out]
_pass_report('sprite-list record address formed in 16 bits (SPRREC)', _sr[0], 9)
# ---- 0xE474's TWO SITES AROUND THE VELOCITY PAIR (2026-10-23, the immortal kind-5 flyer) ----
# 0xE7FE `jsr 0x33ba` returns D0 = vx, D1 = vy (hand/FUN_000033ba.c returns D1 and publishes
# D0 in _r_extraout_D0); the decompile took D0 from an extraout captured at function entry.
# 0xE8BE `move.w 0x40(a5),d0` with a5 = `movea.w a0,a5`: the decompile truncated the SH-2
# record pointer to 16 bits before the read.
_k5 = [0, 0]
def _kind5_sites(txt):
    txt, k1 = re.subn(r'= \(short\)\(extraout_D0w_00 \* 3\) >> 1;',
                      r'= (short)((short)_r_extraout_D0 * 3) >> 1;   /* 0xE804: D0 from jsr 0x33ba, the pair: cport_prep.py */', txt)
    txt, k2 = re.subn(r'\*\(ushort \*\)\(\(short\)(iVar\d+) \+ 0x40\);',
                      r'*(ushort *)(\1 + 0x40);   /* 0xE8BE: movea.w a0,a5 keeps the record pointer: cport_prep.py */', txt)
    _k5[0] += k1; _k5[1] += k2
    return txt
out = [_kind5_sites(_c) for _c in out]
_pass_report('kind 5 0xE7FE takes vx from the published D0', _k5[0], 1)
_pass_report('kind 5 0xE8BE reads the hero through the full pointer', _k5[1], 1)
# ---- 0x34FE's THREE CALLERS TAKE vx FROM THE PUBLISHED D0 (2026-10-23) --------------------
# 0x34FE returns the pair (D0 = vx, D1 = vy) like 0x33BA; hand/FUN_000034fe.c returns D1 and
# publishes D0. reg_convention lists 034FE's return as D1 alone, so _feed_extraout fed each
# caller `extraout_D0w = (uint)<the D1 return>` and 0xC8F8, 0x184F8 and 0x1A636 computed
# vx := vy (found by the kind-5 verifiers: 321 of 512 angles wrong as generated).
_v34 = [0]
def _vel34fe_callers(txt):
    txt, k = re.subn(r'(= FUN_000034fe\([^;]*\);\n(\s*))extraout_D0w = \(uint\)\w+;',
                     r'\1extraout_D0w = (ushort)_r_extraout_D0;   /* 0x34FE returns the pair: D0 = vx: cport_prep.py */', txt)
    _v34[0] += k
    return txt
out = [_vel34fe_callers(_c) for _c in out]
_pass_report('0x34FE callers take vx from the published D0', _v34[0], 3)
# ---- HAND-DERIVED FUNCTIONS ARE C FILES: sh_src/portfull/hand/FUN_<addr>.c -------------
# Mike, 2026-10-04: a hand-derived function is a C file, not a regex pass. Each file
# replaces the decompiled definition of its function in the final text with an
# #include (so no pass rewrites the hand code), and the signature line is taken from
# protos.h so the hand file and the prototype cannot disagree. The hand code sees the
# unit's own vocabulary (w68.h macros, the register file, port_e_d0/port_carry).
# games/<title>/hand since framework 3c (2026-10-29); the unit still says #include "hand/X.c" and
# tools/cport_build.sh puts the title directory on the include path.
_hand_dir = os.environ.get('CPORT_HAND_DIR') or os.path.join(outdir, 'hand')
# DISCOVERY WAS HEX-ONLY WHILE EVERY OTHER STEP MATCHES BY NAME. The swap below keys on
# the function's name -- protos.h, the definition in the unit, the #include -- so a hand
# file for a NAMED routine worked in every respect except being found: it was skipped in
# silence. Measured 2026-10-07: hand/tilemap_unpack_hi.c and _lo.c, the tilemap's bulk
# writers, were ignored for a whole build-and-measure cycle and the page marks they add
# read 8 of 104 unchanged. Any identifier now, hex or named.
_hand = sorted(f[:-2] for f in os.listdir(_hand_dir)
               if re.match(r'[A-Za-z_][A-Za-z0-9_]*\.c$', f) and not f.startswith('AT_')) \
        if os.path.isdir(_hand_dir) else []
_protos = open(os.path.join(outdir, 'protos.h')).read() if os.path.exists(os.path.join(outdir, 'protos.h')) else ''
_text = '\n'.join(out)
_hand_n = 0
for _fn in _hand:
    _pm = re.search(r'^([a-z][^\n]* \*?%s\([^)]*\));$' % _fn, _protos, flags=re.M)
    _hp = os.path.join(_hand_dir, _fn + '.c')
    _hs = open(_hp).read()
    _dm = re.search(r'^[a-z][^\n]* \*?%s\([^)]*\)\s*\n\{' % _fn, _hs, flags=re.M)
    # A HAND FILE IS THE DERIVATION; THE GENERATOR MUST NOT EDIT IT. Both of these used
    # to be rewritten in place ("the prototype wins"), which means a file committed as
    # read-from-the-68K-listing could be silently replaced by Ghidra's idea of the
    # signature on the next build and nobody would see it in the diff. They are build
    # errors now: fix the hand file, or fix what gives protos.h the other signature.
    if _pm and _dm:
        _have = _hs[_dm.start():_hs.index(')', _dm.start()) + 1]
        if _pm.group(1) != _have:
            raise SystemExit('cport_prep: %s.c signature disagrees with protos.h\n'
                             '  hand file: %s\n  protos.h : %s\n'
                             '  (the hand file is the derivation -- edit it, or fix the prototype; '
                             'the generator no longer rewrites it)' % (_fn, _have, _pm.group(1)))
    if _pm and not _pm.group(1).startswith('void ') and re.search(r'\breturn;', _hs):
        raise SystemExit('cport_prep: %s.c has a bare `return;` but protos.h declares it '
                         'non-void (%s)\n  gcc 15 makes that an error -- write `return 0;` '
                         'in the hand file' % (_fn, _pm.group(1)))
    _text, k = re.subn(r'^[a-z][^\n]* \*?%s\([^)]*\)\s*\n\{.*?\n\}\n' % _fn,
                       '#include "hand/%s.c"   /* hand-derived: games/<title>/hand */\n' % _fn,
                       _text, count=1, flags=re.M | re.S)
    if k != 1:
        raise SystemExit('cport_prep: hand file %s.c has no definition to replace in the unit' % _fn)
    _hand_n += 1
# ---- HAND FUNCTIONS FOR 68K ENTRY POINTS GHIDRA NEVER MADE FUNCTIONS OF ------------------
# sh_src/portfull/hand/AT_FUN_<addr>.c defines `... FUN_<addr>(...) { ... }` for an address
# the decompile has no function at (0x3D60 / 0x3D8A: the trail objects' sprite-draw
# entries, stored into +0x6c by 0x6BD0/0x6BBC and reached only through `jmp (a0)` at
# 0x6CE4 -- Ghidra attributed the bytes to neighbours). The file is included at the end
# of the unit, its prototype appended to protos.h (objdispatch.c includes protos.h, not
# the unit), and cport_dispatch.py thunks it by the address in the file name.
_at = sorted(f[:-2] for f in os.listdir(_hand_dir) if re.match(r'AT_FUN_[0-9a-f]{8}\.c$', f)) if os.path.isdir(_hand_dir) else []
_at_protos = []
for _fn in _at:
    _hs = open(os.path.join(_hand_dir, _fn + '.c')).read()
    _dm = re.search(r'^([a-z][^\n]* \*?%s\([^)]*\))\s*\n\{' % _fn[3:], _hs, flags=re.M)
    if not _dm:
        raise SystemExit('cport_prep: %s.c defines no %s' % (_fn, _fn[3:]))
    _at_protos.append(_dm.group(1) + ';')
    _text += '#include "hand/%s.c"   /* hand function at a 68K entry Ghidra has no function for */\n' % _fn
if _at_protos:
    with open(os.path.join(outdir, 'protos.h'), 'a') as _pf:
        _pf.write('\n'.join(_at_protos) + '\n')
print('cport_prep: hand functions at non-Ghidra entries (hand/AT_*.c): %d' % len(_at))
out = [_text]
print('cport_prep: hand-derived functions included from %s: %d' % (_hand_dir, _hand_n))
# ---- 68K SP-RESET ENTRIES ARE JUMPS, NOT CALLS (2026-10-24) ------------------------
#
# A 68K routine that opens with `lea 0xffffff00,%sp` discards every frame below it, and
# the game reaches each one by `jmp` (every attract step ends `jmp 0x1e54`, 0x227C and
# 0x2286). Decompiled, each became a C CALL that gcc keeps as `jsr`, so every attract
# step leaked a frame on the coroutine stack until it ran off the bottom of .portstack
# at ~frame 52,000 and the master crashed: the 15.4-minute attract hang (sw308-sw315).
# The body is renamed and the original name becomes a wrapper through port_sp_reset
# (sh_src/port_l2.c), which resets the SH-2 SP to the coroutine root first.
#
# DERIVED FROM THE BINARY: the set is every Ghidra function entry (docs/audit/
# func_ranges.txt) whose first 10 bytes, inside its own range, hold `4ff8 ff00`, and the build stops if the
# table and that census disagree. 0x500 also loads the SP, but at 0x54E behind a test,
# and it has no definition in this unit.
_SPRST = {0x0040E: 'FUN_0000040e', 0x005BE: 'main_entry', 0x01E54: 'FUN_00001e54'}
_sp_rom = open(_sw_rom, 'rb').read()
_sp_fr = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'docs', 'audit', 'func_ranges.txt')
_sp_census = set()
for _l in open(_sp_fr):
    _p = _l.split()
    if len(_p) >= 3 and not _l.startswith('#') and int(_p[0], 16) == int(_p[2], 16):
        _e, _end = int(_p[2], 16), int(_p[1], 16) + 1   # the entry's own range only:
        # 0x40C is a lone `rte` whose range ends where 0x40E's lea begins
        if b'\x4f\xf8\xff\x00' in _sp_rom[_e:min(_e + 10, _end)]:
            _sp_census.add(_e)
if _sp_census != set(_SPRST):
    raise SystemExit('cport_prep: SP-reset entries in prog68k.bin %s != the pass table %s'
                     % (sorted(map(hex, _sp_census)), sorted(map(hex, _SPRST))))
_sp_txt = '\n'.join(out)
_sp_n = 0
_sp_wrap = ['', '/* 68K SP-reset entries (tools/cport_prep.py): the lea, then the body. */',
            'extern uint port_sp_reset(void *fn, uint a, uint b, uint c, uint d, uint e);']
for _e, _nm in sorted(_SPRST.items()):
    _dre = re.compile(r'^uint %s\(([^)]*)\)(\s*\n\{)' % _nm, re.M)
    _m = _dre.search(_sp_txt)
    if not _m or len(_dre.findall(_sp_txt)) != 1:
        raise SystemExit('cport_prep: SP-reset entry %s: need exactly one definition' % _nm)
    _params = _m.group(1).strip()
    _names = [] if _params in ('', 'void') else [x.split()[-1] for x in _params.split(',')]
    _args = ['(uint)%s' % x for x in _names] + ['0'] * (5 - len(_names))
    _sp_txt = (_sp_txt[:_m.start()] + 'static uint sprst_%s(%s)%s' % (_nm, _params, _m.group(2))
               + _sp_txt[_m.end():])
    _sp_wrap += ['uint %s(%s)' % (_nm, _params), '{',
                 '  return port_sp_reset((void *)sprst_%s, %s);   /* 68K 0x%X: lea 0xffffff00,%%sp */'
                 % (_nm, ', '.join(_args), _e), '}']
    _sp_n += 1
out = (_sp_txt + '\n' + '\n'.join(_sp_wrap) + '\n').split('\n')
_pass_report('68K SP-reset entries wrapped (0x40E, 0x5BE, 0x1E54)', _sp_n, 3)
# CPORT_HOT (2026-10-13): move the named functions out of .portcode (cart) and into
# .ramtext (SDRAM). Measured on the line: the master is 267,550 cyc/vint cart-resident at
# CPI 3.06, and these run at CPI 9-13.5 purely because .portcode is cart. The attribute
# goes on the DEFINITION; PORTHOT is defined in ghidra_types.h and is empty without
# -DPORT_HOT, so a non-PORTHOT build is byte-identical. Every name must match exactly one
# definition or the build stops -- a silently unmoved function would read as a null result.
_hot_all = [x.strip() for x in (os.getenv('CPORT_HOT') or '').split(',') if x.strip()]
# PORTHOT2 (2026-10-24): `name@N` sends a function to SDRAM region N (2 = .ramtext2, 3 =
# .ramtext3; mars.ld), chosen by tools/hot_set.py into tools/hot_set.txt. Those go by a
# REDECLARATION carrying the section attribute, appended to protos.h: gcc merges the
# attribute into the definition, the definition line itself is untouched (so neither
# cport_dispatch's `have` anchor nor any other column-0 matcher can lose it), and it works
# the same for a hand file, whose definition is not in this text but behind an #include.
# Thunk names (thk_/thk0_) belong to cport_dispatch.py, runtime names to their own source
# (port_l2.c, -DHOT2_<name>); both are skipped here. A bare name keeps the PORTHOT path.
_hot = [x for x in _hot_all if '@' not in x]
_hotN = [x.split('@') for x in _hot_all if '@' in x]
if _hotN:
    _pp = os.path.join(outdir, 'protos.h')
    _ptxt = open(_pp).read()
    _unit_txt = '\n'.join(out)
    _decl = []
    for _hn, _hr in _hotN:
        if _hr not in ('2', '3'):
            sys.exit('cport_prep: CPORT_HOT %s@%s: the region must be 2 or 3' % (_hn, _hr))
        if _hn.startswith(('thk_', 'thk0_')):
            continue
        _pm = re.findall(r'^([A-Za-z_][^\n;]*?\b%s\s*\([^;]*\))\s*;' % re.escape(_hn), _ptxt, re.M)
        _defined = re.search(r'^[A-Za-z_][\w \*]*\b%s\s*\(' % re.escape(_hn), _unit_txt, re.M) or \
                   re.search(r'^#include "hand/(?:AT_)?%s\.c"' % re.escape(_hn), _unit_txt, re.M)
        if not _defined:
            if os.getenv('CPORT_HOT_RUNTIME_OK', '') and _hn in os.getenv('CPORT_HOT_RUNTIME_OK').split(','):
                continue
            sys.exit('cport_prep: CPORT_HOT %s@%s is not defined in the unit (and is not a thunk or a '
                     'listed runtime function) -- a silently unmoved function reads as a null result'
                     % (_hn, _hr))
        _pm = sorted(set(_pm))
        if len(_pm) != 1:
            sys.exit('cport_prep: CPORT_HOT %s@%s: protos.h has %d prototypes, need exactly 1'
                     % (_hn, _hr, len(_pm)))
        _decl.append('%s PORTHOT%s;' % (_pm[0], _hr))
    with open(_pp, 'a') as _pf:
        _pf.write('/* PORTHOT2: the hot set (tools/hot_set.txt), section by redeclaration */\n'
                  + '\n'.join(_decl) + '\n')
    sys.stderr.write('cport_prep: CPORT_HOT moved %d functions to SDRAM regions 2/3\n' % len(_decl))
if _hot:
    _txt = '\n'.join(out)
    for _hn in _hot:
        _hre = re.compile(r'^(?=[A-Za-z_][\w ]*\*?\s*\b%s\s*\()' % re.escape(_hn), re.M)
        _n = len(_hre.findall(_txt))
        if _n != 1:
            sys.stderr.write('cport_prep: CPORT_HOT %s matched %d definitions, need exactly 1\n'
                             % (_hn, _n))
            sys.exit(1)
        # THE ATTRIBUTE GOES ON ITS OWN LINE, NOT AS A PREFIX. tools/cport_dispatch.py:61
        # builds the set of functions "in the unit" with `^(?:uint|ulonglong) (\w+)\(`
        # anchored at column 0, so `PORTHOT uint FUN_000103e0(` is not seen as a definition:
        # the handler falls into missing_fn at :78, gets NO dispatch-table row, and the
        # binary search in port_l2.c:990-993 then skips the object entirely. Measured
        # 2026-10-13: FUN_000103e0 and FUN_00010824 read ZERO cycles from boot, thunks
        # included, while FUN_0000669c kept running because it is reached by direct C calls
        # that follow the symbol rather than through the table. The same anchor is at
        # cport_dispatch.py:154 and cport_audit.py:76, so the own-line form is the one that
        # is safe against tools not grepped.
        _txt = _hre.sub('PORTHOT\n', _txt, count=1)
    out = _txt.split('\n')
    sys.stderr.write('cport_prep: CPORT_HOT moved %d functions to .ramtext: %s\n'
                     % (len(_hot), ','.join(_hot)))
# CPORT_F154LOG (measurement only, set by the Makefile under R60LOCKPROBE): every write to the
# enemy count 0xFFF154 and every zero-test of it is routed through a recorder in sh_src/port_l2.c
# (port_f154_note / port_f154_read). The level-5 lock reads F154 = FF on the FPGA footer while no
# tick-end sample ever sees it negative, so the writes themselves are logged: site, old, new, tick,
# and the object A6 points at. Site ids are printed here and in a comment in the unit.
if os.environ.get('CPORT_F154LOG'):
    _f154_sites = []
    _w = re.compile(r'^(\s*)(_?DAT_fffff154) = (.+);\s*$')
    _r = re.compile(r'\bDAT_fffff154 == (?:0|\'\\0\')')
    _fn = None
    for _i, _l in enumerate(out):
        _m = re.match(r'^(?:uint|void|int|ulonglong|undefined4|bool|byte|ushort|short|char)\s+\*?(\w+)\(', _l)
        if _m:
            _fn = _m.group(1)
        _m = _w.match(_l)
        if _m:
            _f154_sites.append(('W', _fn))
            _id = len(_f154_sites)
            out[_i] = '%s{ byte _f154o = DAT_fffff154; %s = %s; port_f154_note(%d, _f154o); }   /* F154LOG site %d: %s */' % (
                _m.group(1), _m.group(2), _m.group(3), _id, _id, _fn)
            continue
        if _r.search(_l):
            _f154_sites.append(('R', _fn))
            _id = len(_f154_sites)
            out[_i] = _r.sub('port_f154_read(%d, DAT_fffff154) == 0' % _id, _l)
    out.insert(0, 'extern void port_f154_note(unsigned site, unsigned char old); extern unsigned char port_f154_read(unsigned site, unsigned char v);')
    out.insert(1, '/* F154LOG sites: %s */' % ' '.join('%d=%s:%s' % (i + 1, k, f) for i, (k, f) in enumerate(_f154_sites)))
    sys.stderr.write('cport_prep: F154LOG %d sites: %s\n' % (len(_f154_sites), ' '.join('%d=%s:%s' % (i + 1, k, f) for i, (k, f) in enumerate(_f154_sites))))
open(os.path.join(outdir, 'altbeast_port.c'), 'w').write('\n'.join(out))
# the hand-defined gap handler (see "handler 0x24A4" above) must be visible to the
# dispatcher unit too: objdispatch.c includes protos.h, not altbeast_port.c (rd42:
# implicit declaration of FUN_000024a4). Appended, so a Ghidra-provided prototype
# of the same shape merely repeats it.
if 'uint FUN_000024a4(undefined4 p_D3,undefined4 p_D6,undefined4 p_D7)' in '\n'.join(out):
    with open(os.path.join(outdir, 'protos.h'), 'a') as _pf:
        _pf.write('uint FUN_000024a4(undefined4 p_D3,undefined4 p_D6,undefined4 p_D7);\n')

print('forward declarations emitted: %d' % len(protos))
print('DAT symbols: %d' % len(syms))
for _, _, name in REGIONS + [(0, 0, 'other')]:
    if name in by:
        print('  %-28s %5d' % (name, len(by[name])))
print('\nPSEUDO-REGISTERS declared as globals: %d distinct' % len(pseudo))
for k, v in pseudo.most_common(10):
    print('  %-16s %4d refs' % (k, v))
