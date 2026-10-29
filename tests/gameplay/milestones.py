#!/usr/bin/env python3
"""Read the milestone frames off a gameplay corpus (timeline.csv + census.csv).

Prints the first frame of each event along the tape, so a corpus can be checked in one
line and two corpora (arcade vs port) compared by their milestone columns:
  credit     0xFFF000 first rises from 0
  start      demo bit (0xFFF026 bit0) first clears after a credit
  xmove      player x (slot 0 +0xC) first differs from its value at start
  score      0xFFE000 first changes from 0
  boss       slot 40 status first has bit 15 set
  boss_1794c slot 40 handler first reads 0x1794C (the transform countdown)
  f14b       0xFFF14B first reads 1 (round-clear release)
  level2     0xFFF142 first reads 1
  scrollmax  the largest 0xFFF132 seen, and its first frame
  l2_enemies the enemy handlers (slots 16-39) seen in the census after level2

    python3 tests/gameplay/milestones.py <corpus dir>
"""
import csv
import os
import sys


def load(path):
    with open(path) as fh:
        return list(csv.DictReader(fh))


def first(rows, pred):
    for r in rows:
        if pred(r):
            return int(r['frame'])
    return None


def main():
    d = sys.argv[1]
    tl = load(os.path.join(d, 'timeline.csv'))
    cs = load(os.path.join(d, 'census.csv')) if os.path.exists(os.path.join(d, 'census.csv')) else []
    h = lambda r, k: int(r[k], 16)
    out = {}
    out['credit'] = first(tl, lambda r: 0 < h(r, 'credits') < 0x10)
    out['start'] = first(tl, lambda r: out['credit'] and int(r['frame']) > out['credit']
                         and h(r, 'demo') == 0 and h(r, 'step') == 8)
    if out['start']:
        byframe = {int(r['frame']): r for r in tl}
        x0 = h(byframe.get(out['start'], tl[0]), 'px')
        out['xmove'] = first(tl, lambda r: int(r['frame']) > out['start'] and h(r, 'px') != x0)
    else:
        out['xmove'] = None
    out['score'] = first(tl, lambda r: out['start'] and int(r['frame']) > out['start']
                         and h(r, 'score') != 0)
    out['boss'] = first(tl, lambda r: out['start'] and int(r['frame']) > out['start']
                        and h(r, 'boss_stat') & 0x8000)
    out['boss_1794c'] = first(tl, lambda r: h(r, 'boss_hand') == 0x1794C)
    out['f14b'] = first(tl, lambda r: out['start'] and int(r['frame']) > out['start']
                        and h(r, 'f14b') == 1)
    out['level2'] = first(tl, lambda r: h(r, 'level') == 1)
    smax, sframe = 0, None
    for r in tl:
        if out['start'] and int(r['frame']) > out['start'] and h(r, 'scroll') > smax:
            smax, sframe = h(r, 'scroll'), int(r['frame'])
    out['scrollmax'] = '%04x@%s' % (smax, sframe)
    l2 = set()
    if out['level2']:
        for r in cs:
            if int(r['frame']) >= out['level2'] and 16 <= int(r['slot']) <= 39:
                l2.add(int(r['handler'], 16))
    out['l2_enemies'] = ' '.join('%x' % x for x in sorted(l2))
    for k, v in out.items():
        print('%-11s %s' % (k, v))
    last = tl[-1]
    print('%-11s frame %s level %s scroll %s lives %s score %s' % (
        'end', last['frame'], last['level'], last['scroll'], last['lives'], last['score']))


if __name__ == '__main__':
    main()
