#!/usr/bin/env python3
"""NAME THE OBJECT HANDLERS from an obj_dispatch capture.

    tools/obj_handlers.py <capture-dir> [--scenes oracle/scenes.csv]
                          [--state oracle/state.csv] [--out TABLE.md]

Each OBJ line is one `jsr (a0)` from the object loop: the handler, the object
record, and that record's first eight words. This groups them by handler and
reports what distinguishes each one -- the object type at +6, the sprite
attribute at +10, how often it runs, and WHICH ROUNDS it appears in.

The round is what does most of the naming work: a handler seen only in round 2
is a round-2 enemy, and one seen in every round is shared machinery. Nothing
here guesses a name; it reports the evidence a name is read off.
"""
import argparse, collections, csv, re
from pathlib import Path

RE = re.compile(r'^OBJ ([0-9A-F]{8}) ([0-9A-F]{8}) ' + ' '.join([r'([0-9A-F]{4})'] * 8))


def round_of(frame, bounds):
    r = 0
    for f, nr in bounds:
        if frame >= f:
            r = nr
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dir')
    ap.add_argument('--state', default='oracle/state.csv')
    ap.add_argument('--out')
    a = ap.parse_args()

    bounds = []
    try:
        prev = None
        for row in csv.DictReader(open(a.state)):
            rd = row['round']
            if rd != prev and rd not in ('255', 'FF', ''):
                bounds.append((int(row['frame']), int(rd)))
                prev = rd
    except Exception:
        pass

    H = collections.defaultdict(lambda: dict(
        n=0, objs=set(), types=collections.Counter(),
        spr=collections.Counter(), rounds=collections.Counter()))
    for p in sorted(Path(a.dir).glob('*.log')):
        # the frame is the LAST underscore-separated field: merged capture
        # dirs prefix the source ('od2_o000700'), and stripping all non-digits
        # from the whole stem silently folded that '2' into the frame number.
        frame = int(re.sub(r'\D', '', p.stem.split('_')[-1]) or 0)
        rnd = round_of(frame, bounds)
        for line in open(p, errors='replace'):
            m = RE.match(line)
            if not m:
                continue
            h = int(m.group(1), 16)
            rec = [int(m.group(i), 16) for i in range(3, 11)]
            e = H[h]
            e['n'] += 1
            e['objs'].add(int(m.group(2), 16) & 0xFFFFFF)
            e['types'][rec[3]] += 1          # +6
            e['spr'][rec[5]] += 1            # +10
            e['rounds'][rnd] += 1

    lines = []
    lines.append('| handler | calls | slots | rounds | type(+6) | sprite(+10) |')
    lines.append('|---|---|---|---|---|---|')
    for h, e in sorted(H.items(), key=lambda kv: -kv[1]['n']):
        rs = ','.join(str(r) for r in sorted(e['rounds']))
        ty = ' '.join('%04X x%d' % (t, c) for t, c in e['types'].most_common(3))
        sp = ' '.join('%04X x%d' % (t, c) for t, c in e['spr'].most_common(3))
        lines.append('| 0x%05X | %d | %d | %s | %s | %s |'
                     % (h, e['n'], len(e['objs']), rs, ty, sp))
    txt = '\n'.join(lines)
    print('distinct handlers observed: %d   dispatches: %d'
          % (len(H), sum(e['n'] for e in H.values())))
    single = [h for h, e in H.items() if len(e['rounds']) == 1]
    print('handlers seen in exactly ONE round (round-specific): %d' % len(single))
    print(txt)
    if a.out:
        Path(a.out).write_text(txt + '\n')
        print('\n-> ' + a.out)


if __name__ == '__main__':
    main()
