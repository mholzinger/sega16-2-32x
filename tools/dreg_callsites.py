#!/usr/bin/env python3
"""REGISTERS SET TO A CONSTANT IMMEDIATELY BEFORE AN INDIRECT CALL.

    python3 tools/dreg_callsites.py docs/audit/dreg_callsites.json

WHY. Ghidra cannot see through `jsr (An)`, so a register the caller loads with a
constant just before one looks DEAD to the decompiler and is dropped. The callee then
runs on whatever the previous handler left.

MEASURED 2026-10-03 (FULL-PORT.md 64). The player's respawn dispatches its state
routine as

    49B2: moveq #$0, D6
    49B4: moveq #$0, D7
    49B6: jsr (A0)          -> 0x4D9A, which falls into FUN_00004DBA

and the state routine's first act after setting state 4 is `andiw #6,%d6 ; beq` --
with D6 = 0 it KEEPS state 4 and script 0x76B0, which is what the arcade does. The
port arrived with a stale D6 (the dispatcher deliberately THREADS D3/D6 handler to
handler), took `(uVar3 & 2) != 0`, and set state 9 / script 0x7888 instead. From
there the animation script's X delta is 8 instead of 0, the player drifts, its hitbox
misses, and slot 16 stops registering hits -- the whole remaining object error.

THIS IS a6_callsites.json's SIBLING, and deliberately so. That table carries the A6
loads Ghidra drops at call sites; this one carries the DATA registers it drops before
indirect calls. Same shape, same one-at-a-time arbitration by the gate, same reason:
the decompiler's view of liveness ends at an indirect call and the 68000's does not.

DISASSEMBLY IS ALIGNED PER FUNCTION, from docs/audit/func_ranges.txt, for the reason
tools/cport_widths.py gives: a linear sweep of a ROM with embedded tables decodes data
as instructions and invents operands.

WHAT IT DOES NOT CLAIM. A constant load followed by an indirect call is strong
evidence the callee reads that register, but it is not proof -- the load may be for
code after the call. So this NAMES candidates and CPORT_DREG applies the ones the gate
accepts, caller by caller.
"""
import argparse, collections, json, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OBJDUMP = os.environ.get('M68K_OBJDUMP',
    os.path.expanduser('~/src/marsdev/mars/m68k-elf/bin/m68k-elf-objdump'))
ROM = os.path.join(ROOT, 'roms/altbeast/prog68k.bin')
RANGES = os.path.join(ROOT, 'docs/audit/func_ranges.txt')

ap = argparse.ArgumentParser()
ap.add_argument('out', nargs='?', default='docs/audit/dreg_callsites.json')
ap.add_argument('--window', type=int, default=4,
                help='how many instructions back from the call to accept (default 4)')
a = ap.parse_args()

ranges = []
for l in open(RANGES):
    if l.startswith('#'):
        continue
    f = l.split()
    if len(f) >= 4 and f[0].startswith('0x'):
        ranges.append((int(f[0], 16), int(f[1], 16) + 1, f[3]))
ranges.sort()
runs = []
for lo, hi, _n in ranges:
    if runs and lo <= runs[-1][1]:
        runs[-1][1] = max(runs[-1][1], hi)
    else:
        runs.append([lo, hi])
owner = lambda pc: next((n for lo, hi, n in ranges if lo <= pc < hi), None)

# `moveq #k,%dN` and `move.[bwl] #k,%dN` -- a CONSTANT into a data register
_CONST = re.compile(r'^(?:moveq|move[bwl])\s+#(-?\d+|-?0x[0-9a-f]+),%d([0-7])$')
# the indirect call forms: jsr (An), jsr (d,An), jsr (An,Dn)
_ICALL = re.compile(r'^jsr\s+%a[0-7]@')
# THE DISPATCH TABLE'S BASE, so a pass can match the right call site WITHOUT an
# ordinal. FUN_00004870 makes TWO indirect calls -- one through 0x6D70 and one through
# 0x6D90 -- and counting them in source order is exactly the kind of match that is
# right until the decompiler reorders something. The base comes from the `lea` that
# set the table pointer, and the generated C names it (`&PTR_FUN_00006d70`), so the
# match is on a symbol rather than a position.
_LEA = re.compile(r'^lea\s+(?:%pc@\()?(0x[0-9a-f]+)')

rows, nseen = [], 0
for lo, hi in runs:
    out = subprocess.run([OBJDUMP, '-D', '-b', 'binary', '-m', 'm68k:68000',
                          '--start-address=0x%X' % lo, '--stop-address=0x%X' % hi, ROM],
                         capture_output=True, text=True).stdout
    prev = []                      # the last few (pc, mnemonic+operands)
    for l in out.splitlines():
        m = re.match(r'\s*([0-9a-f]+):\s+((?:[0-9a-f]{4}\s+)+)\s*(\S+)\s*(.*)$', l)
        if not m:
            continue
        pc, txt = int(m.group(1), 16), (m.group(3) + ' ' + m.group(4)).strip()
        if _ICALL.match(txt):
            nseen += 1
            sets = {}
            for ppc, ptxt in reversed(prev[-a.window:]):
                cm = _CONST.match(ptxt)
                if not cm:
                    continue
                reg = int(cm.group(2))
                if reg in sets:
                    continue              # a later load wins; we walk backwards
                sets[reg] = int(cm.group(1), 0) & 0xFFFFFFFF
            base = None
            for _ppc, ptxt in reversed(prev[-8:]):
                lm = _LEA.match(ptxt)
                if lm:
                    base = int(lm.group(1), 16)
                    break
            if sets:
                rows.append({'caller': owner(pc) or '?', 'call': pc,
                             'table': ('0x%05X' % base) if base is not None else None,
                             'regs': {str(k): v for k, v in sorted(sets.items())}})
        prev.append((pc, txt))
        if len(prev) > 8:
            prev.pop(0)

bycaller = collections.Counter(r['caller'] for r in rows)
with open(os.path.join(ROOT, a.out), 'w') as fh:
    json.dump(rows, fh, indent=0, sort_keys=True)
    fh.write('\n')
print('dreg_callsites: %d indirect call site(s) seen, %d carry a constant register '
      'load within %d instruction(s) -> %s'
      % (nseen, len(rows), a.window, a.out))
print('  callers, most sites first: %s'
      % ', '.join('%s x%d' % (c, n) for c, n in bycaller.most_common(10)))
