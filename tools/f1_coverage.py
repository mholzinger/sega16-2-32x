#!/usr/bin/env python3
"""F1.1 coverage: merge arcade traces, find code Ghidra missed, and list the
gaps no trace ever executed.

    tools/f1_coverage.py <trace-dir> <f1_export.json> [--seeds OUT.json]

WHY THE ORACLE AND NOT A LISTING. Executed code needs no heuristic and resolves
every dispatch by construction, so it is the only non-circular seed source here
(LESSONS 2026-09-26 16:15; gating targets on prior decoding failed twice in one
day). Traces come from tools/arcade_trace.lua over `mame altbeast`, the
always-valid oracle.

THE ASYMMETRY, which this tool reports rather than hides: an executed PC PROVES
code. The ABSENCE of executed PCs proves NOTHING -- no set of playthroughs
reaches every routine. So the never-executed list is not a data list. It is the
set the translator must translate blind or trap on, and F0(d)'s trap is the
design for exactly this.
"""
import argparse, json, re, sys
from pathlib import Path

PAT = re.compile(r'^([0-9A-Fa-f]{4,8}):')
CODE_LO, CODE_HI = 0x400, 0x20000


def pcs_from(path):
    out = set()
    with open(path, errors='replace') as fh:
        for l in fh:
            m = PAT.match(l)
            if m:
                out.add(int(m.group(1), 16))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('tracedir')
    ap.add_argument('export')
    ap.add_argument('--seeds')
    a = ap.parse_args()

    per, allpcs = {}, set()
    for p in sorted(Path(a.tracedir).glob('*.log')):
        s = pcs_from(p)
        per[p.stem] = s
        allpcs |= s
    if not allpcs:
        sys.exit('no PCs parsed from ' + a.tracedir)

    print('PER-TRACE COVERAGE (distinct PCs, and what each adds)')
    seen = set()
    for k in per:
        new = per[k] - seen
        seen |= per[k]
        print('  %-10s %6d PCs  +%-6d new  (cumulative %d)'
              % (k, len(per[k]), len(new), len(seen)))
    above = sorted(p for p in allpcs if p >= CODE_HI)
    print('\nunion: %d distinct PCs, range 0x%05X-0x%05X'
          % (len(allpcs), min(allpcs), max(allpcs)))
    print('executed at or above 0x%X: %d %s'
          % (CODE_HI, len(above), '(confirms DATA)' if not above else sorted(hex(x) for x in above[:8])))

    g = json.load(open(a.export))
    gstart = {i['a'] for i in g['instrs']}
    missing = sorted(allpcs - gstart)
    print('\nGhidra decodes %d instructions' % len(gstart))
    print('executed PCs Ghidra decodes    : %d' % len(allpcs & gstart))
    print('executed PCs Ghidra MISSES     : %d   <- seed these, they are code'
          % len(missing))
    if a.seeds:
        json.dump(sorted('%X' % p for p in missing), open(a.seeds, 'w'))
        print('  -> %s' % a.seeds)

    # NEVER-EXECUTED code: decoded by Ghidra but no trace ever reached it
    dec_never = sorted(x for x in gstart - allpcs if CODE_LO <= x < CODE_HI)
    print('\nDECODED BUT NEVER EXECUTED in any trace: %d instructions' % len(dec_never))
    print('  (NOT data -- unreached by these playthroughs. Translate blind or trap.)')

    # undefined space that also never executed
    cov = bytearray(CODE_HI - CODE_LO)
    for i in g['instrs']:
        s0 = i['a']
        if CODE_LO <= s0 < CODE_HI:
            for k in range(s0, min(s0 + i['n'], CODE_HI)):
                cov[k - CODE_LO] = 1
    gaps, s0 = [], None
    for k in range(len(cov)):
        if not cov[k]:
            if s0 is None:
                s0 = k
        elif s0 is not None:
            gaps.append((s0 + CODE_LO, k + CODE_LO)); s0 = None
    if s0 is not None:
        gaps.append((s0 + CODE_LO, CODE_HI))
    big = sorted((g2 for g2 in gaps if g2[1] - g2[0] >= 256),
                 key=lambda g2: g2[1] - g2[0], reverse=True)
    tot = sum(e - b for b, e in gaps)
    print('\nUNDEFINED bytes 0x%X-0x%X: %d in %d gaps; %d gaps >= 256 B (%d B)'
          % (CODE_LO, CODE_HI, tot, len(gaps), len(big),
             sum(e - b for b, e in big)))
    print('  top 10, with any executed PC inside (an executed PC means CODE):')
    for b, e in big[:10]:
        inside = [p for p in allpcs if b <= p < e]
        print('    0x%05X-0x%05X %6d B  executed PCs inside: %d%s'
              % (b, e, e - b, len(inside),
                 '  <- CODE, seed it' if inside else ''))


if __name__ == '__main__':
    main()
