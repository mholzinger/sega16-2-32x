#!/usr/bin/env python3
"""Generate the object dispatch table: 68K handler address -> SH-2 function.

    python3 tools/cport_dispatch.py <outdir>

WHY THE PORT CANNOT USE GHIDRA'S object_dispatcher AS DECOMPILED. It reads:

    pcVar1 = &DAT_ffffc000;
    do { if (*pcVar1 < '\\0') { ... (**(code **)(pcVar1 + 2))(); }
         pcVar1 = pcVar1 + 0x80; } while (DAT_fffff109 < 0x40);

Two things in that line do not survive the port, and both are silent:

1. THE POINTER IS A 68K ADDRESS. `*(code **)(pcVar1 + 2)` is a value like
   0x0EE80 sitting in work RAM -- an address in the 68K program, not an SH-2
   function. Calling through it jumps into the middle of the SH-2's own address
   space. It has to be looked up.

2. THE CALL PASSES NO ARGUMENT, BUT THE 68K PASSED A6. object_dispatcher holds
   the slot base in A6 and every handler reaches its object through it
   (docs/log/LOOP-DECOMPILE.md 28; 7208 references to `unaff_A6` in the port).
   Ghidra models that as ONE GLOBAL, `_r_unaff_A6`, so without help every
   handler in a sweep would see whichever slot was set last -- or nothing.

So the dispatcher is supplied by hand (sh_src/port_l2.c) and it does what the
68K did: set A6 to the slot base, then call the handler this table names.

The table's addresses come from docs/proof/object_handlers_union.txt (every
handler any instrument has observed) and its names from
docs/audit/func_ranges.txt, which carries each function's ENTRY POINT -- not
min(body), which differs for 10 of 788 functions.

A handler address seen at run time that is NOT in this table is COUNTED rather
than called, because calling it would be a wild jump. That count is also the
test of whether the census is complete: it should be zero.
"""
import os, re, sys

outdir = sys.argv[1] if len(sys.argv) > 1 else 'sh_src/portfull'

entry = {}
for line in open('docs/audit/func_ranges.txt'):
    if line.startswith('#'):
        continue
    f = line.split()
    if len(f) == 4:
        entry.setdefault(int(f[2], 16), f[3])

want = sorted({int(m, 16) for m in
               re.findall(r'0x([0-9A-Fa-f]{3,6})',
                          open('docs/proof/object_handlers_union.txt').read())})

# only names the unit actually defines: the reachable set may exclude a handler
# observed in a round this build's root set does not reach
# PARAMETERS ARE PART OF THE SIGNATURE NOW. `^uint NAME()` stopped matching when
# 549 functions gained register parameters, so the table silently fell from 425
# entries to 202 and every object handler dispatch became an "unknown handler" --
# the frame then scored BETTER (34 of 16,384) purely because the game logic had
# stopped running.
_unit = open(os.path.join(outdir, 'altbeast_port.c')).read()
have = set(re.findall(r'^(?:uint|ulonglong) (\w+)\(', _unit, re.M))
# HAND-DERIVED FUNCTIONS ARE DEFINED TOO. cport_prep replaces a function whose body
# lives in hand/FUN_<addr>.c with `#include "hand/FUN_<addr>.c"`, so its definition
# line is not in altbeast_port.c; without this every hand-derived handler fell out of
# the table and its objects were counted as unknown instead of run (2026-10-04: the
# flame, 0x10DD4, froze at its spawn position and its seed 0x10CD6 never fired).
have |= set(re.findall(r'^#include "hand/(?!AT_)(\w+)\.c"', _unit, re.M))
# NAMED hand files too (2026-10-25): cport_prep swaps any identifier (hand/input_edges.c,
# hand/tilemap_unpack_hi.c), and the hex-only anchor dropped those functions' rows in
# silence -- input_edges lost its row the build its hand file landed (hot_check 795 / 796).
# ... and the hand functions at entries Ghidra has no function for (hand/AT_FUN_<addr>.c):
# their address is the file name's, since docs/audit/func_ranges.txt cannot carry them.
_at_entries = {n: int(n[4:], 16) for n in re.findall(r'^#include "hand/AT_(FUN_[0-9a-f]{8})\.c"', _unit, re.M)}
have |= set(_at_entries)

# GUARD (2026-10-13): a definition the `have` anchor cannot see is INVISIBLE HERE and the
# handler silently loses its table row -- the object then never runs and nothing says so.
# That is exactly what an attribute written as a PREFIX did (`PORTHOT uint FUN_...(`), and
# it cost a build that looked like a 21.6% win and was a regression. Any attribute-looking
# line immediately above a definition is fine (cport_prep emits PORTHOT on its own line and
# the anchor still matches); a definition the anchor MISSES while the name is plainly
# defined is a hard error, not a warning.
_shadowed = set(re.findall(r'^[A-Z][A-Z0-9_]*\s+(?:uint|ulonglong)\s+(\w+)\(', _unit, re.M))
_shadowed -= have
if _shadowed:
    sys.exit('cport_dispatch: %d definition(s) hidden from the `have` anchor by a prefix '
             'attribute: %s. They would lose their dispatch-table row and never run. Emit '
             'the attribute on its own line (cport_prep CPORT_HOT) or widen the anchor at '
             'cport_dispatch.py:61, :154 and cport_audit.py:76.'
             % (len(_shadowed), ','.join(sorted(_shadowed))))

rows, missing_fn, not_entry = [], [], []
for a in want:
    n = entry.get(a)
    if n is None:
        not_entry.append(a)
    elif n not in have:
        missing_fn.append((a, n))
    else:
        rows.append((a, n))

# EVERY FUNCTION IN THE UNIT, not just the object handlers. port_call68 resolves
# ALL of the program's indirect calls, and most of them are not object dispatch:
# the object handlers themselves dispatch a STATE MACHINE through a 16-entry jump
# table (FUN_0000ee80 -> &DAT_0000f556 + (state & 0xf) * 4), and those targets are
# ordinary functions. Measured 2026-09-28: with only the 139 handlers in the table,
# the state dispatch missed every time and the state machines never ran.
#
# There is no reason to restrict the table. An address that names a function in
# this unit is callable; one that does not is still counted rather than jumped to.
_byname = {}
for line in open('docs/audit/func_ranges.txt'):
    if line.startswith('#'):
        continue
    f = line.split()
    if len(f) == 4:
        _byname.setdefault(f[3], int(f[2], 16))
for _n, _a in _at_entries.items():
    _byname.setdefault(_n, _a)
_seen = {a for a, _ in rows}
_extra = 0
for n in sorted(have):
    a = _byname.get(n)
    if a is None or a in _seen:
        continue
    rows.append((a, n))
    _seen.add(a)
    _extra += 1
# ---- 68K THUNKS: THE TABLE ADDRESS IS NOT THE BODY'S ADDRESS ------------------
#
# Three entries in docs/audit/func_ranges.txt are named `thunk_FUN_<addr>` over a
# 1-byte range: 0x00408 -> 0x0040C, 0x14A78 -> 0x14A8C, 0x152E2 -> 0x15306. Each is
# a single `bra.s` sitting in front of the real body's tail (verified with
# m68k-elf-objdump over roms/altbeast/prog68k.bin), and each is what a POINTER TABLE
# stores -- 0x14A78 and 0x152E2 are entries 11 and 15 of the kind table at 0x1D2EA,
# which FUN_0000e052 dispatches on a spawn record's kind byte.
#
# Neither loop above can give them a row. `want` holds only addresses an instrument
# OBSERVED, and a table target is never observed as a handler; `_byname` keys on a
# name the unit defines, and the unit defines FUN_00014a8c, not thunk_FUN_00014a8c.
# So port_call68_at missed on both, the slot kept handler 0, and the spawner's
# `tst.w (a6); bmi` path skipped the decrement that 0xFFF154 is waiting on.
#
# A thunk address resolves to the body it jumps to. This is the mechanical rule for
# the shape, not a row per function: any future `thunk_FUN_<addr>` is picked up.
_thunks = 0
for _n, _a in sorted(_byname.items()):
    if not _n.startswith('thunk_FUN_'):
        continue
    _t = _n[len('thunk_'):]
    if _t in have and _a not in _seen:
        rows.append((_a, _t))
        _seen.add(_a)
        _thunks += 1
if _thunks:
    print('dispatch table: %d 68K thunk address(es) resolved to the body they jump to'
          % _thunks)

rows.sort()
print('dispatch table: %d object handlers + %d other functions in the unit'
      % (len(rows) - _extra, _extra))

# ---- EVERY ROOT MUST HAVE A ROW ----------------------------------------------
#
# CPORT_ROOTFILE lists the addresses the port is REQUIRED to be able to reach
# indirectly: observed object handlers plus every harvested pointer-table target.
# port_call68_at binary-searches this table and, on a miss, COUNTS instead of
# jumping -- correct (the alternative is a wild jump) and completely silent. A root
# with no row is therefore a handler that never runs and nothing says so.
#
# That is how kinds 11 and 15 of the 0x1D2EA table stayed dead: their addresses are
# one-instruction `bra.s` thunks, so neither `want` (observed only) nor `_byname`
# (keyed on a name the unit defines) could place them. Printed, not fatal, because a
# root may legitimately belong to a round this build's closure does not reach.
_rootfile = os.getenv('CPORT_ROOTFILE') or os.getenv('CPORT_HANDLERS')
if _rootfile and os.path.exists(_rootfile):
    _roots = {int(m, 16) for m in
              re.findall(r'^0x([0-9A-Fa-f]{3,6})\s*$', open(_rootfile).read(), re.M)}
    _uncovered = sorted(_roots - _seen)
    print('dispatch table: %d of %d roots in %s have a row%s'
          % (len(_roots) - len(_uncovered), len(_roots), _rootfile,
             '' if not _uncovered else
             '; UNREACHABLE: ' + ' '.join('0x%05X' % a for a in _uncovered[:12])
             + (' ...' if len(_uncovered) > 12 else '')))

# ---- REGISTER MARSHALLING THUNKS (FULL-PORT.md 26) ----------------------------
#
# The table used to be unprototyped and called with NO arguments, and that was a
# correctness bug, not a limitation: sprite_build_depth_banded's hide/draw decision
# is gated on `p_D6`, so with p_D6 reading 0 NOTHING IS EVER HIDDEN -- every object
# gets its sprite record filled and bit 3 of its flag byte set, every frame.
#
# Positional passing cannot work: the unit has 65 DISTINCT register-parameter
# signatures (D3,D6,D7 x61; D0,D5,D6,D7 x60; A0,D1,D3,D6,D7 x38; ...), so each
# handler needs its arguments in its OWN declared order. Hence a generated thunk per
# handler, which reads a 15-entry register array the dispatcher fills:
#
#   r[0..7]  = D0..D7       r[8..14] = A0..A6
#
# An ARRAY and not a struct deliberately: port_l2.c is hand-written and would
# otherwise need a duplicate struct declaration that can drift from this one.
PROTOS = os.path.join(outdir, 'protos.h')
_decl = {}
if os.path.exists(PROTOS):
    # STRIP COMMENTS AND PREPROCESSOR LINES BEFORE ACCUMULATING.
    #
    # This used to accumulate raw lines until it saw a ';', which meant the file's
    # five-line header comment plus `#ifndef/#define/#include` -- none of which
    # contain a semicolon -- were glued onto the FIRST declaration. The regex below
    # anchors at ^, so that declaration never matched and its handler silently got
    # the no-argument thunk.
    #
    # It was invisible for as long as the first declaration happened to be
    # `uint FUN_00000bd6();`, which takes no arguments, so the wrong thunk was the
    # right thunk. Rooting test_mode_screen moved `FUN_0000057e(byte *p_A1,uint p_D0)`
    # into first place and the compiler said "too few arguments" (2026-10-02).
    _body = re.sub(r'/\*.*?\*/', ' ', open(PROTOS).read(), flags=re.S)
    _body = '\n'.join(_l for _l in _body.splitlines()
                      if not _l.lstrip().startswith('#'))
    _txt, _acc = [], ''
    for _l in _body.splitlines():
        _acc += ' ' + _l.strip()
        if ';' in _acc:
            _txt.append(_acc.strip()); _acc = ''
    for _d in _txt:
        _m = re.match(r'^(?:uint|ulonglong|void|int)\s+(\w+)\s*\((.*)\)\s*;', _d)
        if not _m:
            continue
        _args = _m.group(2).strip()
        if _args in ('void', ''):
            _decl[_m.group(1)] = []
            continue
        _ps = []
        for _a in _args.split(','):
            _a = _a.strip()
            _mm = re.match(r'^(.*?)\s*\*?\s*(p_(\w+))$', _a)
            if not _mm:
                _ps = None
                break
            _ty = _a[:_a.rindex(_mm.group(2))].strip()
            _ps.append((_ty, _mm.group(3)))
        _decl[_m.group(1)] = _ps

_RIDX = {}
for _i, _r in enumerate(('D0','D1','D2','D3','D4','D5','D6','D7')):
    _RIDX[_r] = _i
for _i, _r in enumerate(('A0','A1','A2','A3','A4','A5','A6')):
    _RIDX[_r] = 8 + _i

thunks, nthunk, nplain = [], 0, 0
# PORTHOT2 (2026-10-24): thunks named in CPORT_HOT as thk_<name>@N go to SDRAM region N
# (ghidra_types.h PORTHOT2/PORTHOT3; tools/hot_set.txt chooses them from the profile).
_hot_thk = {x.split('@')[0]: x.split('@')[1] for x in (os.getenv('CPORT_HOT') or '').split(',')
            if '@' in x and x.strip().startswith(('thk_', 'thk0_'))}
_hot_thk = {k.strip(): v.strip() for k, v in _hot_thk.items()}
_hot_thk_done = set()
def _thk_attr(tn):
    r = _hot_thk.get(tn)
    if r is None:
        return ''
    _hot_thk_done.add(tn)
    return 'PORTHOT%s ' % r
def thunk_for(name):
    """the thunk's name, emitting it if the handler takes register parameters"""
    global nthunk, nplain
    ps = _decl.get(name)
    if ps is None:                      # a signature this parser does not understand
        nplain += 1
        return None
    if not ps:
        nplain += 1
        return None                     # no parameters: call it directly
    args = []
    for ty, reg in ps:
        idx = _RIDX.get(reg.upper().replace('W', '').replace('U', ''))
        if idx is None:
            nplain += 1
            return None                 # an unrecognised register: do not guess
        args.append('(%s)(r[%d])' % (ty, idx))
    tn = 'thk_' + name
    thunks.append('%sstatic uint %s(const uint *r) { return %s(%s); }'
                  % (_thk_attr(tn), tn, name, ', '.join(args)))
    nthunk += 1
    return tn

out = ['/* GENERATED by tools/cport_dispatch.py -- 68K handler address -> SH-2 function.',
       ' * See that file for why the decompiled object_dispatcher cannot be used as-is.',
       ' */',
       '#include "ghidra_types.h"',
       '#include "protos.h"',
       '',
       '/* EVERY ENTRY TAKES THE REGISTER ARRAY. r[0..7] = D0..D7, r[8..14] = A0..A6,',
       ' * filled by the dispatcher (sh_src/port_l2.c) before each call. A handler',
       ' * with register PARAMETERS is reached through a generated thunk that passes',
       ' * them in its own declared order; one with none is wrapped the same way so',
       ' * the table has a single type.',
       ' *',
       ' * The table was unprototyped and called with NO arguments until 2026-10-01,',
       ' * and that was a CORRECTNESS BUG: sprite_build_depth_banded gates its',
       ' * hide/draw decision on p_D6, so with p_D6 reading 0 nothing was ever',
       ' * hidden -- every object got its sprite record filled and bit 3 of its flag',
       ' * byte set, every frame, every slot. FULL-PORT.md 26. */',
       'typedef uint (*port_handler_fn)(const uint *r);',
       '',
       'struct port_handler { unsigned int a68; port_handler_fn fn; };',
       '']
_tbl = []
for a, n in rows:
    tn = thunk_for(n)
    if tn is None:
        tn = 'thk0_' + n
        thunks.append('%sstatic uint %s(const uint *r) { (void)r; return %s(); }' % (_thk_attr(tn), tn, n))
    _tbl.append('    { 0x%05Xu, %s },' % (a, tn))
out += thunks + ['',
       '/* sorted by a68 so the lookup can bisect */',
       'const struct port_handler port_handlers[] = {']
out += _tbl
out += ['};',
        'const unsigned int port_handlers_n = %d;' % len(rows),
        '']
if set(_hot_thk) - _hot_thk_done:
    sys.exit('cport_dispatch: CPORT_HOT names thunk(s) this table does not emit: %s'
             % ','.join(sorted(set(_hot_thk) - _hot_thk_done)))
open(os.path.join(outdir, 'objdispatch.c'), 'w').write('\n'.join(out))

print('dispatch table: %d handlers -> %s/objdispatch.c  (%d register thunks, %d plain)'
      % (len(rows), outdir, nthunk, nplain))
if not_entry:
    print('  %d address(es) are not a function ENTRY (junk, or need a Ghidra split): %s'
          % (len(not_entry), ' '.join('0x%05X' % a for a in not_entry)))
if missing_fn:
    print('  %d handler(s) exist as functions but are NOT in this unit\'s reachable set '
          '(a round the roots do not reach): %s'
          % (len(missing_fn), ' '.join('0x%05X=%s' % (a, n) for a, n in missing_fn[:8])))
