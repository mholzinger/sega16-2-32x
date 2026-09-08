#!/usr/bin/env python3
"""LOOP 17 JOB 1 — merge sprite-frame discovery CSVs, size the bake.

    tools/sprite_discover.py <dir-or-csv> [...]

Merges every sprite_discover.lua CSV given (a directory means every
*.csv inside it), unions the unique keys, sums the sighting counts and
prints the answer job 2 needs: how many unique NATIVE-zoom sprite
frames exist, what they cost in ROM, and how much of the per-frame
decode load a bake of the top-N would actually cover.

The budget is the measured one from docs/log/LOOP17.md: 768KB free under the
4MB no-mapper ceiling.

Per-frame bake layout assumed (must match tools/bake_sprites.py):
    8B header + 1B length per row + 2B per strip word (4bpp payload)
"""
import csv
import os
import sys

BUDGET = 768 * 1024


def load(paths):
    frames = {}
    files = []
    for p in paths:
        if os.path.isdir(p):
            files += sorted(os.path.join(p, f) for f in os.listdir(p)
                            if f.endswith('.csv'))
        else:
            files.append(p)
    if not files:
        sys.exit('no CSVs found')
    for f in files:
        with open(f) as fh:
            n = 0
            for r in csv.DictReader(fh):
                n += 1
                k = r['key']
                cnt = int(r['count'])
                e = frames.get(k)
                if e:
                    e['count'] += cnt
                    e['zoomed_seen'] += int(r['zoomed_seen'])
                    e['native'] = max(e['native'], int(r['native']))
                else:
                    frames[k] = {
                        'key': k,
                        'bank': int(r['bank']),
                        'addr': int(r['addr'], 16),
                        'pitch': int(r['pitch']),
                        'flip': int(r['flip']),
                        'height': int(r['height']),
                        'rows': int(r['rows_walked']),
                        'words': int(r['words']),
                        'bytes': int(r['bytes']),
                        'native': int(r['native']),
                        'zoomed_seen': int(r['zoomed_seen']),
                        'count': cnt,
                    }
            print(f'  {os.path.basename(f):<20} {n:6d} keys')
    return frames, files


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    print('inputs:')
    frames, _ = load(sys.argv[1:])

    allf = sorted(frames.values(), key=lambda e: -e['count'])
    native = [e for e in allf if e['native']]
    zoomed_only = [e for e in allf if not e['native']]

    sightings = sum(e['count'] for e in allf)
    nat_sight = sum(e['count'] for e in native)
    # decode COST proxy, same unit as the SPRREUSE probe: rows x |pitch|
    # is what that probe counted; here the real payload is exact, so
    # weight each sighting by its measured word count.
    cost = sum(e['count'] * e['words'] for e in allf)
    nat_cost = sum(e['count'] * e['words'] for e in native)

    print(f'\nunique keys        {len(allf):6d}'
          f'   ({len(native)} native, {len(zoomed_only)} zoom-only)')
    print(f'sightings          {sightings:6d}'
          f'   ({100.0 * nat_sight / max(sightings, 1):.1f}% native)')
    print(f'decode words       {cost:6d}'
          f'   ({100.0 * nat_cost / max(cost, 1):.1f}% native)')

    total = sum(e['bytes'] for e in native)
    print(f'\nfull native bake   {total / 1024.0:8.1f} KB'
          f'   budget {BUDGET / 1024} KB'
          f'   -> {"FITS" if total <= BUDGET else "OVER"}')

    # coverage curve: bake the most-seen frames first, stop at budget
    used, covered, n = 0, 0, 0
    for e in native:
        if used + e['bytes'] > BUDGET:
            break
        used += e['bytes']
        covered += e['count'] * e['words']
        n += 1
    print(f'top-{n} by sightings {used / 1024.0:8.1f} KB'
          f'   covers {100.0 * covered / max(cost, 1):.1f}% of decode words')

    for pct in (50, 75, 90, 95, 99):
        want = cost * pct / 100.0
        got, b, cnt = 0, 0, 0
        for e in native:
            if got >= want:
                break
            got += e['count'] * e['words']
            b += e['bytes']
            cnt += 1
        flag = '' if b <= BUDGET else '  OVER BUDGET'
        print(f'  {pct:2d}% of decode words: {cnt:5d} frames, '
              f'{b / 1024.0:7.1f} KB{flag}')

    if allf:
        print('\nlargest frames:')
        for e in sorted(native, key=lambda e: -e['bytes'])[:5]:
            print(f'  {e["key"]}  h={e["height"]:3d} words={e["words"]:5d} '
                  f'{e["bytes"] / 1024.0:6.1f} KB  seen {e["count"]}')


if __name__ == '__main__':
    main()
