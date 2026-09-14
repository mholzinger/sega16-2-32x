#!/usr/bin/env python3
"""PER-ROUND MD COLOUR-LINE BAKE (NOTES 62/65, LOOP29 272, LOOP-DECOMPILE 121).

    python3 tools/bake_mdlines.py [--lines 3] [--stats] [--out sh_src/mdlines_md.h]

MDSTATIC pins colour SETS to slots. This pins COLOURS to LINES, which is
what the hardware actually constrains: a tile draws from ONE MD CRAM line
of 15 usable pens, so eviction is impossible exactly when every set a
scene shows has a home line for the whole scene.

Inputs, both from the decompile thread:
  docs/audit/mdpen_scene_sets.txt   on-screen set indices, 20 sampled
                                    scenes over rounds 0-4 (arcade, both
                                    planes' visible windows)
  discover/palscenes/r0124_0575     palette anchor, rounds 0/1/2
  discover/palscenes/r34_0575       palette anchor, rounds 3/4

ANCHOR MAP: rounds 0,1,2 -> r0124_0575; rounds 3,4 -> r34_0575. NOTES 65's
PROSE says 0/1/2/4 and 3; its FILENAMES say 0/1/2 and 3/4, and the
filenames are right -- round 4 scene 1 reads 42 colours (no partition) on
r0124 and 29 (lines [15,14,10]) on r34, and 29 is what LOOP-DECOMPILE 121
itself reported for round 4. See LOOP29 272.

CONSERVATIVE BY CONSTRUCTION: the set lists carry no pixel-usage mask, so
every listed set contributes ALL EIGHT of its pens. Every occupancy this
prints is an upper bound on the real demand; a bake that passes here
passes with real masks too.

TABLE KEY: the runtime knows the ROUND (MD_ROUND, COMM10 bits 13-15, from
the game's own 0xFFF142) and cannot see a sub-scene. Rounds 0-3 take one
table each -- round 2 visits two disjoint set groups but their union
packs. Round 4's two groups do NOT pack together (46 colours against 45,
and no partition even at 43 without pixel 0), so it takes two tables and
one discriminator bit. Its groups are disjoint -- area 0 is sets 96-111,
area 1 is sets 22-36 -- so any single live set index separates them.

SCOPE: demos only. Play past round 0, the cutscenes and the ending are
unsampled, and MDSTATIC's own anchors cover a scene class this does not.
"""
import argparse, re, sys
from pathlib import Path
import importlib.util

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import palscene_bake as psb
_spec = importlib.util.spec_from_file_location('mb', ROOT / 'tools' / 'mdpen_bake.py')
mb = importlib.util.module_from_spec(_spec); _spec.loader.exec_module(mb)

SETS_TXT = ROOT / 'docs' / 'audit' / 'mdpen_scene_sets.txt'
PALSRC = ROOT / 'discover' / 'palscenes'
ANCHOR = {0: 'r0124_0575', 1: 'r0124_0575', 2: 'r0124_0575',
          3: 'r34_0575', 4: 'r34_0575'}
# round 4 splits; the sets that mark area 1 (LOOP29 272)
R4_AREA1 = {22, 23, 24, 25, 26, 27, 28, 29, 30, 33, 34, 35, 36}
NPENS = 15


def read_scenes():
    out = []
    for ln in open(SETS_TXT):
        m = re.match(r'round (\d+) scene (\d+)\s+BG sets ([\d,]*)\s*\|\s*FG sets ([\d,]*)', ln)
        if not m:
            continue
        sets = set()
        for g in (m.group(3), m.group(4)):
            sets |= {int(x) for x in g.split(',') if x != ''}
        out.append((int(m.group(1)), int(m.group(2)), sets))
    if not out:
        sys.exit(f'{SETS_TXT}: no "round N scene M BG sets ... | FG sets ..." lines')
    return out


def tables_wanted(scenes):
    """-> [(name, round, set-union)] in emission order."""
    acc = {}
    for r, sc, sets in scenes:
        if sets == {0}:                       # blank/fade: no art on screen
            continue
        key = (r, 1 if (r == 4 and (sets & R4_AREA1)) else 0)
        acc.setdefault(key, set()).update(sets)
    order = sorted(acc)
    return [(f'round{r}' + (f'_area{a}' if (r, 1) in acc and a is not None and
                            any(k[0] == r and k[1] == 1 for k in acc) else ''),
             r, acc[(r, a)]) for (r, a) in order]


def bake(name, pal, sets, nlines):
    colsets = {}
    for s in sorted(sets):
        cs = frozenset(mb.quant(pal[s * 8 + p]) for p in range(8))
        if cs:
            colsets[s] = cs
    allc = set().union(*colsets.values()) if colsets else set()
    mb.NLINES = nlines
    assign = mb.partition(colsets)
    if assign is None:
        sys.exit(f'BAKE FAIL: {name}: {len(colsets)} sets / {len(allc)} colours have no '
                 f'exact {nlines}x{NPENS} partition -- a real capacity limit, not a bug '
                 f'to paper over. Split the table or raise --lines.')
    line_c = [[0xFFFF] * 16 for _ in range(nlines)]
    s_line = [0] * 128
    s_map = [[0] * 8 for _ in range(128)]
    s_used = [0] * 128
    for s, li in sorted(assign.items()):
        s_line[s] = li + 1
        s_used[s] = 0xFF
        for p in range(8):
            q = mb.quant(pal[s * 8 + p])
            row = line_c[li]
            if q in row[1:]:
                pen = row.index(q)
            else:
                pen = row.index(0xFFFF, 1)
                row[pen] = q
            s_map[s][p] = pen
    loads = [sum(1 for v in line_c[l][1:] if v != 0xFFFF) for l in range(nlines)]
    print(f'  {name:16s} {len(colsets):2d} sets, {len(allc):2d} colours -> lines {loads} '
          f'({nlines * NPENS - sum(loads)} pens spare)')
    return line_c, s_line, s_map, s_used


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--lines', type=int, default=3)
    ap.add_argument('--stats', action='store_true')
    ap.add_argument('--out', default=str(ROOT / 'sh_src' / 'mdlines_md.h'))
    a = ap.parse_args()
    scenes = read_scenes()
    pal = {n: psb.load(PALSRC / f'{n}.palsh') for n in set(ANCHOR.values())}
    want = tables_wanted(scenes)
    print(f'{len(scenes)} sampled scenes -> {len(want)} tables at {a.lines} lines x {NPENS} pens')
    tabs = [(n, r, bake(n, pal[ANCHOR[r]], s, a.lines)) for n, r, s in want]
    if a.stats:
        return
    idx = {n: i for i, (n, _, _) in enumerate(tabs)}
    with open(a.out, 'w') as f:
        f.write('/* GENERATED by tools/bake_mdlines.py -- do not edit.\n'
                ' * Per-round MD colour-LINE assignment (NOTES 62/65, LOOP29 272).\n'
                ' * A tile draws from one CRAM line; pinning every on-screen set of a\n'
                ' * round to a home line makes eviction impossible by construction.\n'
                ' * Conservative: every set contributes all eight pens.\n'
                f' * Tables: {", ".join(n for n, _, _ in tabs)}\n'
                ' * SCOPE: demos of rounds 0-4. Cutscenes, the ending and play past\n'
                ' * round 0 are UNSAMPLED -- the runtime must fall back for those. */\n')
        f.write(f'#define MDL_N {len(tabs)}\n#define MDL_LINES {a.lines}\n')
        f.write('/* round -> table; round 4 needs one discriminator bit (its two areas\n'
                ' * are disjoint: area 1 is sets 22-36), so it lists BOTH. */\n')
        base = [idx[n] for n, r, _ in tabs if r != 4 or n.endswith('area0')]
        f.write('static const uint8_t mdl_of_round[5] = { '
                + ', '.join(str(next(i for i, (n, rr, _) in enumerate(tabs)
                                     if rr == r)) for r in range(5)) + ' };\n')
        r4 = [i for i, (n, rr, _) in enumerate(tabs) if rr == 4]
        f.write('static const uint8_t mdl_round4[2] = { '
                + ', '.join(str(i) for i in (r4 + r4)[:2]) + ' };\n')
        f.write(f'static const uint16_t mdl_line_c[MDL_N][{a.lines * 16}] = {{\n')
        for n, _, t in tabs:
            f.write('    { ' + ', '.join(f'0x{v:04X}' for row in t[0] for v in row) + f' }},   /* {n} */\n')
        f.write('};\nstatic const uint8_t mdl_s_line[MDL_N][128] = {\n')
        for n, _, t in tabs:
            f.write('    { ' + ', '.join(str(v) for v in t[1]) + ' },\n')
        f.write('};\nstatic const uint8_t mdl_s_map[MDL_N][1024] = {\n')
        for n, _, t in tabs:
            f.write('    { ' + ', '.join(str(v) for row in t[2] for v in row) + ' },\n')
        f.write('};\nstatic const uint8_t mdl_s_used[MDL_N][128] = {\n')
        for n, _, t in tabs:
            f.write('    { ' + ', '.join(f'0x{v:02X}' for v in t[3]) + ' },\n')
        f.write('};\n')
    nb = (len(tabs) * (a.lines * 16 * 2 + 128 + 1024 + 128))
    print(f'wrote {a.out}\n  rom cost {nb} bytes as plain const '
          f'({len(tabs)} x {a.lines * 16 * 2} line_c + 128 s_line + 1024 s_map + 128 s_used)')
    lst = ROOT / 'rom' / 's16.lst'
    if lst.exists():
        for ln in open(lst):
            if ln.rstrip().endswith(' _end'):
                end = int(ln.split()[0], 16)
                head = 0x06019000 - end
                print(f'  region headroom now {head} bytes (_end 0x{end:08X}, guard 0x06019000)'
                      f' -> {head - nb} left after this table')
                if nb > head:
                    print('  NOTE: does not fit. s_map holds pen indices 0-15, so nibble-'
                          'packing it to [N][512] saves {} bytes.'.format(len(tabs) * 512))
                break


if __name__ == '__main__':
    main()
