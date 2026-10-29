#!/usr/bin/env python3
"""Loops whose behaviour depends on TIMING rather than on an opcode.

    python3 tools/timing_hazards.py

`hazard_census.py` finds anything with an instruction signature. It cannot
find a loop that is calibrated in CPU cycles, or one that busy-waits on
something the arcade guarantees, and those are the classes that bite a
port whose 68000 runs at a different clock and is NOT stalled on a video
bus the way the arcade's is (LOOP-DECOMPILE 82).

Three shapes, and each is asked for separately because they fail
differently:

  CYCLE DELAY   a dbf that branches to ITSELF: the body is empty, so the
                only thing it measures is the clock. Our 7.670 MHz
                unstalled 68000 runs it at a different wall time than the
                arcade's 10 MHz stalled one.
  INTERRUPT     a stop, or a dbf around one: measures INTERRUPTS, not
  DELAY         cycles, so it is safe on any clock and unsafe only if the
                interrupt does not arrive.
  BUSY-WAIT     a two-or-three instruction loop that tests memory and
                branches back. Safe if the port writes that byte; a hang
                if it does not.

A self-branching dbf is easy to miss: its target EQUALS its own address,
so any loop finder that requires target < address drops exactly the shape
this is looking for.
"""
import re, sys

STREAM = 'docs/audit/code_stream.txt'
DBF = re.compile(r'^%d(\d),0x([0-9a-f]+)$')
BR = re.compile(r'^0x([0-9a-f]+)$')
COND = re.compile(r'^b(ra|eq|ne|cc|cs|pl|mi|hi|ls|ge|lt|gt|le|vc|vs)[sw]?$')
TEST = ('tst', 'btst', 'cmp')


def rows():
    out = []
    for ln in open(STREAM):
        f = ln.rstrip('\n').split('\t')
        if len(f) >= 4:
            out.append((int(f[0], 16), int(f[1]), f[2], f[3]))
    return out


def main():
    rs = rows()
    at = {a: (n, mn, ops) for a, n, mn, ops in rs}
    cycle, irq, spin = [], [], []
    for a, n, mn, ops in rs:
        m = DBF.match(ops)
        if m and mn.startswith('db'):
            t = int(m.group(2), 16)
            if t == a:
                prev = [(b, pm, po) for b, _, pm, po in rs
                        if a - 16 < b < a and po.endswith(',%d' + m.group(1))]
                cycle.append((a, prev[-1] if prev else None))
            elif t < a and at.get(t, (0, '', ''))[1] == 'stop':
                irq.append((t, a, at[t][2]))
            continue
        m = BR.match(ops.strip())
        if not (m and COND.match(mn)):
            continue
        t = int(m.group(1), 16)
        if not (t < a and a - t <= 12):
            continue
        b, k = t, []
        while b <= a:
            if b not in at:
                k = None
                break
            k.append(at[b][1])
            b += at[b][0]
        # A conditional branch backwards to an rts/rte/bra is a shared
        # EXIT, not a loop head, and it has the same shape as a spin. Three
        # of the five candidates here were that; filtering on the target
        # instruction removes all three and keeps every real one.
        if k[0].startswith(('rts', 'rte', 'rtr', 'jmp', 'bra')):
            continue
        if k and len(k) <= 3 and any(x.startswith(TEST) for x in k):
            spin.append((t, a, at[t][2]))
    print('CYCLE DELAY — a dbf branching to itself, empty body: %d' % len(cycle))
    for a, prev in cycle:
        print('  0x%05X  count from %s'
              % (a, ('0x%05X %s %s' % prev) if prev else 'unknown'))
    print()
    print('INTERRUPT DELAY — a counted loop around stop: %d' % len(irq))
    for t, a, ops in irq:
        print('  0x%05X-0x%05X  stop %s' % (t, a, ops))
    print()
    print('BUSY-WAIT — test and branch back, three instructions or fewer: %d'
          % len(spin))
    for t, a, ops in spin:
        print('  0x%05X-0x%05X  %s' % (t, a, ops))


if __name__ == '__main__':
    sys.exit(main())
