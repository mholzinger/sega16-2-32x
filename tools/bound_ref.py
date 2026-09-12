#!/usr/bin/env python3
"""Bounding audit of the function map WITHOUT Ghidra (LOOP-DECOMPILE 73).

tools/ghidra/bound_audit.py asks the right question -- does the body
contain a terminator, and if not does it fall through into another
function -- but it can only run inside the analysed project, which is
the one thing the handoff says to open as little as possible.

This asks the same question of the same 560 entries using the REFERENCE
disassembly as the instruction stream: objdump each function from its own
entry, so instruction boundaries are the function's, not a linear
listing's.

    python3 tools/bound_ref.py [--verbose]

ONE TRAP, and it produced eight false defects before it was found:
objdump WRAPS an instruction longer than six bytes onto a second line
that carries an address and bytes but NO mnemonic. Skip those lines and
every `movel #next,%fp@(2)` -- the state machine's exit idiom, and the
last instruction of many object routines -- reads two bytes short, so the
fall-through test looks at the wrong address and fails.
"""
import re, subprocess, sys, os

OD = os.environ.get('M68K_OBJDUMP',
     '/Users/mikeholzinger/src/marsdev/mars/m68k-elf/bin/m68k-elf-objdump')
BIN = 'roms/altbeast/prog68k.bin'
MAP = 'docs/audit/function_map.md'

HEAD = re.compile(r'^\s+([0-9a-f]+):\t([0-9a-f ]+?)\s*\t(\S+)')
CONT = re.compile(r'^\s+([0-9a-f]+):\t([0-9a-f ]+?)\s*$')
TERM = ('rts', 'rte', 'rtr', 'jmp', 'bra')


def isterm(mn):
    return re.split(r'[.wsl]', mn)[0] in TERM or mn.startswith(TERM)


def disasm(start, stop):
    """[(addr, length, mnemonic)], continuation lines folded into length."""
    o = subprocess.run(
        [OD, '-D', '-b', 'binary', '-m', 'm68k:68000', BIN,
         '--start-address=0x%X' % start, '--stop-address=0x%X' % stop],
        capture_output=True, text=True).stdout
    r = []
    for ln in o.splitlines():
        m = HEAD.match(ln)
        if m:
            r.append([int(m.group(1), 16),
                      len(m.group(2).replace(' ', '')) // 2, m.group(3)])
            continue
        m = CONT.match(ln)
        if m and r:
            r[-1][1] += len(m.group(2).replace(' ', '')) // 2
    return r


def funcs():
    out = []
    for ln in open(MAP):
        m = re.match(r'\|\s*0x([0-9A-F]+)\s*\|\s*(\d+)\s*\|', ln)
        if m:
            out.append((int(m.group(1), 16), int(m.group(2))))
    return out


def main():
    fs = funcs()
    entries = {e for e, _ in fs}
    defects = []
    for e, size in fs:
        ins = disasm(e, e + size)
        if not ins:
            defects.append((e, size, 'EMPTY', ''))
            continue
        if any(isterm(m) for _, _, m in ins):
            continue
        a, n, mn = ins[-1]
        if a + n in entries:            # falls through into the next function
            continue
        defects.append((e, size, mn, hex(a + n)))
    print('functions %d, bounding defects %d' % (len(fs), len(defects)))
    for e, size, mn, nxt in sorted(defects):
        print('  0x%05X size %-5d last=%-10s next=%s' % (e, size, mn, nxt))
    return 0


if __name__ == '__main__':
    sys.exit(main())
