#!/usr/bin/env python3
"""L1 DIFFERENTIAL VECTORS: turn an l1_oracle capture into per-call reference
vectors a translated routine can be verified against mechanically.

    tools/l1_vectors.py <capture-dir> [--out vectors.jsonl] [--max-per-fn N]

INPUT is tools/l1_oracle.lua's log: interleaved
    R <d0..d7> <a0..a7> <sr>      the register file before that instruction
    <pc>: <disassembly>           the instruction
    W <addr> <data>               a WRAM write, as it happens

OUTPUT is one JSON record per observed call:
    {callee, caller, ret, entry:{d,a,sr}, exit:{d,a,sr}, writes:[[addr,data]..]}

WHAT A VECTOR PROVES, and what it does not. Reproducing entry -> exit for the
registers AND the write set is strong evidence a translation is right on the
path that was observed. It says nothing about paths no trace took -- the same
asymmetry f1_coverage.py reports, and the reason F0(d)'s trap exists.

CALL STRUCTURE is taken from the stack pointer, not from the disassembly text:
a call is any instruction whose successor's a7 is 4 lower and where the long at
the new a7 is the address after the call site. That catches jsr and bsr in all
addressing modes, including the `jsr (a0)` dispatches that have no static
caller and are the whole point of the object-handler work.
"""
import argparse, json, re
from pathlib import Path

RE_R = re.compile(r'^R ((?:[0-9A-Fa-f]{8} ){16})([0-9A-Fa-f]{4})\s*$')
RE_W = re.compile(r'^W ([0-9A-Fa-f]{8}) ([0-9A-Fa-f]{8}) (\d+)\s*$')
RE_D = re.compile(r'^D ([0-9A-Fa-f]{8}) ([0-9A-Fa-f]{8}) (\d+)\s*$')
RE_PC = re.compile(r'^([0-9A-Fa-f]{4,8}):\s+(\S+)')


def parse(path):
    """-> list of events: ('i', pc, mnem, regs) and ('w', addr, data)"""
    ev, pend = [], None
    with open(path, errors='replace') as fh:
        for line in fh:
            m = RE_R.match(line)
            if m:
                vals = [int(x, 16) for x in m.group(1).split()]
                pend = (vals[:8], vals[8:16], int(m.group(2), 16))
                continue
            m = RE_W.match(line)
            if m:
                ev.append(('w', int(m.group(1), 16) & 0xFFFFFF, int(m.group(2), 16), int(m.group(3)) // 8))
                continue
            m = RE_D.match(line)
            if m:
                ev.append(('d', int(m.group(1), 16) & 0xFFFFFF, int(m.group(2), 16), int(m.group(3)) // 8))
                continue
            m = RE_PC.match(line)
            if m and pend is not None:
                ev.append(('i', int(m.group(1), 16), m.group(2), pend))
                pend = None
    return ev


def calls(ev):
    """Pair calls with their returns using the stack pointer."""
    ins = [(i, e) for i, e in enumerate(ev) if e[0] == 'i']
    out, stack = [], []
    for k in range(len(ins) - 1):
        idx, e = ins[k]
        nidx, ne = ins[k + 1]
        a7, na7 = e[3][1][7], ne[3][1][7]
        # a CALL: sp dropped by 4 (jsr/bsr) -- or by 6, which is an EXCEPTION
        # (the 68000 pushes SR and PC). Catching 6 is what puts irq4_handler in
        # the vector set: the game's whole per-frame driver is entered by
        # interrupt and never by a call, so a jsr-only rule misses the largest
        # routine that runs every frame.
        d = (a7 - na7) & 0xFFFFFFFF
        if d in (4, 6):
            stack.append(dict(callee=ne[1], caller=e[1], sp=na7, delta=d,
                              kind='irq' if d == 6 else 'call',
                              entry=ne[3], at=nidx, writes=[]))
            continue
        # a RETURN: sp rose back to a frame we are tracking (rts or rte)
        if stack and na7 == (stack[-1]['sp'] + stack[-1]['delta']) & 0xFFFFFFFF:
            fr = stack.pop()
            fr['exit'] = ne[3]
            fr['ret'] = ne[1]
            fr['writes'] = [(e2[1], e2[2], e2[3]) for e2 in ev[fr['at']:nidx] if e2[0] == 'w']
            # FIRST read of each address is the INPUT; later reads may see the
            # function's own writes, which are an output not an input.
            seen_r = {}
            # the rts popping the return address is a read of the 4 bytes at
            # the entry SP; it is call mechanism, not a function input.
            sp = fr['entry'][1][7] & 0xFFFFFF
            for e2 in ev[fr['at']:nidx]:
                if e2[0] == 'd' and e2[1] not in seen_r and not (sp <= e2[1] < sp + 4):
                    seen_r[e2[1]] = (e2[2], e2[3])
            fr['reads'] = sorted(seen_r.items())
            out.append(fr)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dir')
    ap.add_argument('--out', default='/tmp/l1_vectors.jsonl')
    ap.add_argument('--max-per-fn', type=int, default=4)
    a = ap.parse_args()

    per, n_raw = {}, 0
    for p in sorted(Path(a.dir).glob('*.log')):
        got = calls(parse(p))
        n_raw += len(got)
        for c in got:
            per.setdefault(c['callee'], []).append(c)

    with open(a.out, 'w') as fh:
        n = 0
        for fn in sorted(per):
            for c in per[fn][:a.max_per_fn]:
                fh.write(json.dumps({
                    'kind': c.get('kind', 'call'),
                    'callee': '%06X' % c['callee'],
                    'caller': '%06X' % c['caller'],
                    'ret': '%06X' % c['ret'],
                    'entry': {'d': c['entry'][0], 'a': c['entry'][1], 'sr': c['entry'][2]},
                    'exit': {'d': c['exit'][0], 'a': c['exit'][1], 'sr': c['exit'][2]},
                    'reads': [['%06X' % a2, v, w] for a2, (v, w) in c.get('reads', [])],
                    'writes': [['%06X' % a2, v, w] for a2, v, w in c['writes']],
                }) + '\n')
                n += 1
    print('calls observed      : %d' % n_raw)
    print('distinct callees    : %d' % len(per))
    print('vectors written     : %d  -> %s' % (n, a.out))
    top = sorted(per.items(), key=lambda kv: -len(kv[1]))[:10]
    print('most-called: ' + ', '.join('%06X x%d' % (k, len(v)) for k, v in top))


if __name__ == '__main__':
    main()
