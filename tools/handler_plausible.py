#!/usr/bin/env python3
"""Is a candidate object-handler address 68K CODE, or junk a harvest picked up?

    python3 tools/handler_plausible.py docs/proof/object_handlers_union.txt

Every harvest of object-handler pointers contains false positives, because each
samples a memory cell and a cell can be read mid-update, out of the pool, or in a
region that is not the pool at all. Three measured examples:

  0x1A70E  a data table (0007 0001 7FFF 4900 4A00 4B00 ...), from a scan whose
           range ran 8 KB past the 64-slot pool
  0x00102  the 68K exception VECTOR TABLE (040C repeated = the spurious stub)
  0x08104  two bytes INTO the instruction at 0x08102, which is genuine

THE DISCRIMINATOR IS DERIVED, NOT EYEBALLED. object_dispatcher (0x0398E) calls
each handler with A6 holding the slot base (docs/log/LOOP-DECOMPILE.md 28), so a
handler's own code reaches its object through A6-relative addressing --
`(d16,A6)`, 68K mode 5 register 6. Real handler prologues are full of it:

  3D6E 0028 0070   move.w (0x28,A6),(0x70,A6)
  526E 0022        addq.w #1,(0x22,A6)
  306E 003C        movea.w (0x3c,A6),a0
  082E 0003 0000   btst   #3,(0,A6)

and the junk above has none. An EA field of (d16,A6) is `101 110` = 0x2E: that is
the low 6 bits for a source operand, and bits 11..6 == 0x35 for a MOVE
destination (register 6 in bits 11..9, mode 5 in bits 8..6).

WHAT THIS CANNOT DECIDE: whether an address is an INSTRUCTION BOUNDARY. 0x08104
and 0x08106 both score 2 here, and both are inside the 6-byte
`move.b #1,(0x22,A6)` at 0x08102 -- the window overlaps the same real instructions,
so the score is inherited rather than earned. Deciding that needs a full 68K length
decoder, and Ghidra already has one: tools/ghidra/handler_entries.py rejects any
address where getInstructionContaining(a).getAddress() != a. This pass is the cheap
filter that keeps data and vector tables out; Ghidra is the authority on alignment.

Reads the ORIGINAL program, not sh_src/game_high.bin -- the patched image rewrites
call targets (`jsr $9065AA` for `jsr $65AA`), and judging code from it once put a
patcher artefact in an analysis.
"""
import re, sys

PROG   = 'roms/altbeast/prog68k.bin'
CORPUS = 'docs/proof/altbeast.c'
WORDS  = 8          # window: a prologue reaches its object almost immediately

def a6_hits(d, t, n=WORDS):
    """How many of the next n words carry an A6-relative effective address."""
    h = 0
    for i in range(n):
        if t + 2*i + 2 > len(d):
            break
        w = int.from_bytes(d[t+2*i:t+2*i+2], 'big')
        if (w & 0x3F) == 0x2E or ((w >> 6) & 0x3F) == 0x35:
            h += 1
    return h

def main():
    src = open(CORPUS).read()
    fns = sorted((int(m.group(1), 16), m.group(2), int(m.group(3)))
                 for m in re.finditer(r'/\* ---- 0x([0-9A-F]+)\s+(\S+)\s+\((\d+) B\)', src))
    d = open(PROG, 'rb').read()
    starts = {a for a, _, _ in fns}

    def owner(t):
        for a, n, sz in fns:
            if a <= t < a + sz:
                return (a, n, sz)
        return None

    addrs = sorted({int(m, 16) for m in
                    re.findall(r'0x([0-9A-Fa-f]{3,6})', open(sys.argv[1]).read())})
    verdicts = {'entry': [], 'split': [], 'create': [], 'junk': []}
    print('%-9s %-5s %-30s %s' % ('addr', 'A6', 'status', 'first words'))
    for t in addrs:
        if t >= len(d) or t % 2:
            verdicts['junk'].append(t); continue
        h = a6_hits(d, t)
        o = owner(t)
        if t in starts:
            v, note = 'entry', 'already a function entry'
        elif h == 0:
            v, note = 'junk', 'NO A6-relative use -- not handler code'
        elif o:
            v, note = 'split', 'inside %s +%d' % (o[1], t - o[0])
        else:
            v, note = 'create', 'code in no function'
        verdicts[v].append(t)
        if v != 'entry':
            print('0x%05X  %-5d %-30s %s' % (t, h, note,
                  ' '.join('%04X' % int.from_bytes(d[t+2*i:t+2*i+2], 'big')
                           for i in range(6))))
    print()
    for v in ('entry', 'split', 'create', 'junk'):
        print('%-7s %3d  %s' % (v, len(verdicts[v]),
              ' '.join('0x%05X' % a for a in verdicts[v]) if v != 'entry' else ''))
    need = verdicts['split'] + verdicts['create']
    if need:
        open('docs/proof/object_handlers_toghidra.txt', 'w').write(
            '# %d handler addresses that are 68K CODE but have no function entry.\n'
            '# %d need a function CREATED (in no function); %d need an existing\n'
            '# function SPLIT. Feed to tools/ghidra/handler_entries.py.\n'
            '# Classified by tools/handler_plausible.py: A6-relative addressing in the\n'
            '# first %d words, because object_dispatcher passes the slot base in A6.\n'
            % (len(need), len(verdicts['create']), len(verdicts['split']), WORDS)
            + '\n'.join('0x%05X' % a for a in sorted(need)) + '\n')
        print('\n-> docs/proof/object_handlers_toghidra.txt (%d addresses)' % len(need))

main()
