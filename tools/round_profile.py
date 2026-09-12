#!/usr/bin/env python3
"""Histogram MAME instruction traces into a per-round 68000 profile.

    RP_N=<round> RP_OUT=tr<N>.txt mame altbeast ... -debug -debugger none \
        -autoboot_script tools/round_profile.lua
    python3 tools/round_profile.py tr0.txt tr1.txt ...

TWO THINGS THE TRACE FORMAT WILL COST YOU IF YOU MISS THEM, and both did:

  * MAME prints trace addresses in UPPERCASE hex. A `[0-9a-f]` match keeps
    only the addresses that happen to be all digits — 8% of the file.
  * MAME COLLAPSES tight loops into `(loops for N instructions)`. Ignore
    those lines and every loop counts once. They were 30x the total here.

Together they under-reported the work by a factor of 29 and the first
profile looked plausible anyway, which is the point.
"""
import re, sys, collections, bisect

EXT = {0x40E: 0x57A, 0x5FA8: 0x602C, 0x60E6: 0x6168,
       0x63CC: 0x644E, 0x6C44: 0x6CB8, 0x18146: 0x181E4}
DROP = {0x6E7A, 0x8532, 0xDE56, 0x18F38, 0x1A0B8}
PAT = re.compile(r'^([0-9A-Fa-f]{6}):')
LOOP = re.compile(r'^\s+\(loops for (\d+) instructions\)')
SPIN = {0x3982, 0x3986, 0x3988}          # the frame wait: idle, not work


def funcs():
    out = []
    for l in open('docs/audit/function_map2.md'):
        m = re.match(r'\|\s*0x([0-9A-F]+)\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|\s*([^|]+)\|', l)
        if m:
            a = int(m.group(1), 16)
            if a not in DROP:
                out.append((a, EXT.get(a, a + int(m.group(2))), m.group(4).strip()))
    out.sort()
    return out


def main():
    fn = funcs()
    starts = [x[0] for x in fn]

    def owner(pc):
        i = bisect.bisect_right(starts, pc) - 1
        return fn[i][2] if i >= 0 and fn[i][0] <= pc < fn[i][1] else 'unbounded'

    frames = int(sys.argv[1]) if sys.argv[1].isdigit() else 20
    files = sys.argv[2:] if sys.argv[1].isdigit() else sys.argv[1:]
    print('round  instructions     idle     work   work/frame')
    profs = []
    for i, f in enumerate(files):
        h = collections.Counter()
        tot = idle = 0
        last = None
        for ln in open(f):
            m = PAT.match(ln)
            if m:
                pc = int(m.group(1), 16)
                last = pc
                tot += 1
                if pc in SPIN:
                    idle += 1
                else:
                    h[owner(pc)] += 1
                continue
            m = LOOP.match(ln)
            if m and last is not None:
                n = int(m.group(1))
                tot += n
                if last in SPIN:
                    idle += n
                else:
                    h[owner(last)] += n
        profs.append(h)
        print('  %d   %12d %8d %8d %11d'
              % (i, tot, idle, tot - idle, (tot - idle) // frames))
    print()
    names = [k for k, _ in sorted(
        {k: sum(p[k] for p in profs) for p in profs for k in p}.items(),
        key=lambda kv: -kv[1])[:10]]
    print('per frame, by routine')
    print('%-28s %s' % ('routine', ' '.join('r%d' % i for i in range(len(profs)))))
    for n in names:
        print('%-28s %s' % (n[:28],
                            ' '.join('%5d' % (p[n] // frames) for p in profs)))


if __name__ == '__main__':
    main()
