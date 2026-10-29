#!/usr/bin/env python3
"""L1 TRIAGE: which decompiled functions can a differential actually test, and
what blocks the rest.

    tools/l1_triage.py [--src docs/proof/altbeast.c]
                       [--vectors docs/proof/l1_vectors.jsonl] [--out T.md]

The harness drives a function by setting the pseudo-register GLOBALS that
pseudoregs.h declares (`unaff_A6`, `in_D0b`, ...), because every decompiled
function is `NAME(void)` and reads its inputs from those. That mechanism has
two holes, and this measures both rather than discovering them one function at
a time:

  SHADOWED  the body re-declares `in_D0w` / `unaff_A6` / `extraout_*` as a
            LOCAL, which shadows the global. Setting the global then reaches
            nothing and the function reads an uninitialised stack slot. The
            fix is prep-side: drop the local declaration so the name binds to
            the global.
  EMPTY     the body is `return;` for a function with real bytes. A
            differential over such a function passes only if the original also
            changed nothing, so it is a decompilation gap, not a port.

A function is TESTABLE-NOW when it has at least one vector, no shadowed
inputs, and vectors with no writes -- exit registers alone decide it.
"""
import argparse, collections, json, re
from pathlib import Path

# INPUTS ONLY. extraout_* is a value an inner CALL left in a register -- an
# output, read after that call -- so shadowing it as a local does not stop the
# harness driving the function. Only in_*/unaff_* are inputs the harness must
# set, and after the 2026-09-27 shadow-drop transform ZERO of those remain in
# the prepped corpus (73 extraout_*_NN survive and are harmless).
LOCAL = re.compile(r'^\s+\w[\w \*]*\b(in_[A-Z]\w*|unaff_[A-Z]\w*)\s*;', re.M)
EMPTY = re.compile(r'\{\s*return\s*;?\s*\}')


def functions(src):
    parts = re.split(r'\n/\* ---- (0x[0-9A-F]{5})\s+(\S+)\s+\((\d+) B\)', src)
    for i in range(1, len(parts), 4):
        yield int(parts[i], 16), parts[i + 1], int(parts[i + 2]), parts[i + 3]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', default='docs/proof/altbeast.c')
    ap.add_argument('--vectors', default='docs/proof/l1_vectors.jsonl')
    ap.add_argument('--out')
    a = ap.parse_args()

    vec = collections.defaultdict(list)
    for line in open(a.vectors):
        r = json.loads(line)
        vec[int(r['callee'], 16)].append(r)

    rows, tally = [], collections.Counter()
    for addr, name, size, body in functions(open(a.src).read()):
        sh = sorted(set(LOCAL.findall(body)))
        empty = bool(EMPTY.search(body[:200]))
        vs = vec.get(addr, [])
        nw = sum(len(v['writes']) for v in vs)
        if not vs:
            cls = 'NO-VECTOR'
        elif empty:
            cls = 'EMPTY-BODY'
        elif sh:
            cls = 'SHADOWED'
        elif nw:
            cls = 'NEEDS-WRITE-DIFF'
        else:
            cls = 'TESTABLE-NOW'
        tally[cls] += 1
        rows.append((cls, addr, name, size, len(vs), nw, ','.join(sh)))

    order = ['TESTABLE-NOW', 'NEEDS-WRITE-DIFF', 'SHADOWED', 'EMPTY-BODY', 'NO-VECTOR']
    print('L1 TRIAGE over %d functions and %d vectors\n' % (len(rows), sum(len(v) for v in vec.values())))
    for k in order:
        n = tally[k]
        b = sum(r[3] for r in rows if r[0] == k)
        print('  %-18s %4d functions  %6d bytes' % (k, n, b))

    out = ['# L1 triage: what a differential can test', '',
           '| class | functions | bytes |', '|---|---|---|']
    for k in order:
        out.append('| %s | %d | %d |' % (k, tally[k],
                                         sum(r[3] for r in rows if r[0] == k)))
    out += ['', '| class | addr | name | bytes | vectors | writes | shadowed |',
            '|---|---|---|---|---|---|---|']
    for r in sorted(rows, key=lambda r: (order.index(r[0]), -r[3])):
        if r[0] == 'NO-VECTOR':
            continue
        out.append('| %s | 0x%05X | %s | %d | %d | %d | %s |'
                   % (r[0], r[1], r[2], r[3], r[4], r[5], r[6] or '-'))
    if a.out:
        Path(a.out).write_text('\n'.join(out) + '\n')
        print('\n-> ' + a.out)
    return rows


if __name__ == '__main__':
    main()
