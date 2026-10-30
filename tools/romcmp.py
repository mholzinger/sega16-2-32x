#!/usr/bin/env python3
"""Byte-identity gate for two roms, the build id excluded.

    python3 tools/romcmp.py build/port/rom/A.32x build/port/rom/sw344.32x [--skip lo:hi ...]

Exits 0 when every byte below the build id (0x3FFFD0) matches, 1 otherwise, and names the
symbol around each differing run when an .elf sits beside the first rom.

WHY PYTHON (2026-10-30). The gate was `cmp -l A B | awk '$1-1 < 0x3FFFD0'`. macOS awk reads
0x3FFFD0 as 0, so the filter never printed a line and every framework step "passed": the
8-byte regression from framework 3c (four handler pointers stored as SH-2 addresses) sat
in every build for a day and failed demo B.
"""
import os, subprocess, sys, bisect

BUILD_ID = 0x3FFFD0
NM = os.path.expanduser('~/src/marsdev/mars/sh-elf/bin/sh-elf-nm')

def main():
    args = [x for x in sys.argv[1:] if not x.startswith('--')]
    skips = []
    for i, x in enumerate(sys.argv):
        if x == '--skip':
            lo, hi = sys.argv[i + 1].split(':')
            skips.append((int(lo, 0), int(hi, 0)))
            args.remove(sys.argv[i + 1])
    a, b = (open(p, 'rb').read() for p in args[:2])
    if len(a) != len(b):
        print('romcmp: sizes differ: %d vs %d' % (len(a), len(b)))
        return 1
    diff = [i for i in range(min(len(a), BUILD_ID))
            if a[i] != b[i] and not any(lo <= i < hi for lo, hi in skips)]
    print('romcmp: %d differing bytes below the build id' % len(diff))
    if not diff:
        return 0
    runs = []
    for i in diff:
        if runs and i - runs[-1][1] <= 4:
            runs[-1][1] = i
        else:
            runs.append([i, i])
    syms = []
    elf = os.path.splitext(args[0])[0] + '.elf'
    if os.path.exists(elf) and os.path.exists(NM):
        for l in subprocess.run([NM, '-n', elf], capture_output=True, text=True).stdout.split('\n'):
            p = l.split()
            if len(p) == 3 and p[0].startswith('02'):
                syms.append((int(p[0], 16) - 0x02000000, p[2]))
    keys = [s for s, _ in syms]
    for lo, hi in runs[:20]:
        k = bisect.bisect_right(keys, lo) - 1
        where = ' %s+0x%X' % (syms[k][1], lo - syms[k][0]) if k >= 0 else ''
        print('  0x%06X-0x%06X%s' % (lo, hi, where))
    return 1

if __name__ == '__main__':
    sys.exit(main())
