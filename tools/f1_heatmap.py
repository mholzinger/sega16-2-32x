#!/usr/bin/env python3
"""Coverage HEAT MAP for the 68K program image.

    tools/f1_heatmap.py <f1_class.bin> [trace-dir] [--cell 256]

Partial EXECUTION coverage is normal and expected -- one playthrough does not
run every routine (Mike, 2026-09-26). The map is how that stops being alarming
and becomes trackable: it shows what is decoded, what has actually run, what is
data, and what is still unknown, all in one screen, and it is regenerable as
coverage improves.

    #  decoded code, OBSERVED EXECUTING     the strongest evidence
    +  decoded code, never executed         fine: other rounds, other enemies
    .  data                                 not code, excluded from the port
    ?  UNKNOWN                              undecoded, INSIDE the code band

BAND-AWARE, and this matters for the headline number. The program's code ends at
~0x1F000: four independent measurements put nothing above it (Ghidra's recursive
descent finds no code; the oracle executed no PC there over four traces; every
branch edge into it originates in bytes Ghidra never reaches; the region carries
only data references). So bytes above the code end are DATA, not unknown --
counting them as unknown made 'UNKNOWN 67.8%' out of what is really 128 KB of
art and level tables. UNKNOWN is reserved for undecoded bytes INSIDE the band,
which is the only figure that measures distance to full coverage.
"""
import re, sys, argparse
from pathlib import Path

PAT = re.compile(r'^([0-9A-Fa-f]{4,8}):')
GLYPH = {0: '?', 1: '+', 2: '.', 3: '.', 4: '#'}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('classbin')
    ap.add_argument('tracedir', nargs='?')
    ap.add_argument('--cell', type=int, default=256)
    ap.add_argument('--width', type=int, default=64)
    ap.add_argument('--code-end', type=lambda x: int(x, 0), default=0x1F000,
                    help='program code ends here; above it, undecoded = DATA')
    a = ap.parse_args()

    cls = bytearray(open(a.classbin, 'rb').read())
    for o in range(a.code_end, len(cls)):
        if cls[o] == 0:
            cls[o] = 2                             # above the code end: DATA
    if a.tracedir:
        for p in Path(a.tracedir).glob('*.log'):
            with open(p, errors='replace') as fh:
                for l in fh:
                    m = PAT.match(l)
                    if m:
                        o = int(m.group(1), 16)
                        if o < len(cls) and cls[o] == 1:
                            cls[o] = 4            # executed
    # a cell takes the glyph of its most significant class present
    PRI = [4, 0, 1, 2, 3]                          # executed > unknown > code > data
    rows = []
    for base in range(0, len(cls), a.cell * a.width):
        row = ''
        for c0 in range(base, min(base + a.cell * a.width, len(cls)), a.cell):
            seg = cls[c0:c0 + a.cell]
            g = '.'
            for p in PRI:
                if p in seg:
                    g = GLYPH[p]; break
            row += g
        rows.append((base, row))
    print('HEAT MAP  cell = %d bytes, row = %d KB   # executed  + code  . data  ? unknown\n'
          % (a.cell, a.cell * a.width // 1024))
    for base, row in rows:
        print('%06X  %s' % (base, row))
    from collections import Counter
    c = Counter(cls)
    tot = len(cls)
    print()
    for k, name in ((4, 'code, EXECUTED'), (1, 'code, not executed'),
                    (2, 'defined data'), (3, 'data-referenced'), (0, 'UNKNOWN')):
        print('  %-20s %7d  %5.1f%%' % (name, c[k], 100.0 * c[k] / tot))
    code = c[4] + c[1]
    print('\n  code total %d B; executed %d B = %.1f%% of code'
          % (code, c[4], 100.0 * c[4] / max(1, code)))
    band = a.code_end - 0x400
    print('  UNKNOWN %d B inside the code band 0x400-0x%X = %.1f%% of the band'
          % (c[0], a.code_end, 100.0 * c[0] / band))
    print('  THAT is the distance to full coverage. Above 0x%X is %d B of data.'
          % (a.code_end, len(cls) - a.code_end))


if __name__ == '__main__':
    main()
