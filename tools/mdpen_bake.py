#!/usr/bin/env python3
"""STATIC-SCENE bake: per-scene static MD pen tables (docs/design/STATIC-SCENE.md).
    python3 tools/mdpen_bake.py --harvest H.txt [--harvest ...] [--state X.bs1 ...]
                                [--stats] [--out sh_src/pal_scenes_md.h]

For every LOADABLE scene of tools/palscene_bake.py (same anchors,
discover/palscenes/<scene>.palsh) this computes the four runtime tables
the SH-2 pen allocator keeps (sh_src/m_main.c, "MD PALETTE PACK"):
    line_c[3][16]   9-bit colour per pen, 0xFFFF free (pen 0 unused)
    s_line[128]     MD line 1..3 per colour set, 0 = not in the scene
    s_map[128][8]   pixel -> pen
    s_used[128]     pixel-usage mask
so that EVERY pixel of every set the scene uses has an EXACT pen: the
sets are partitioned across the 3 lines by exhaustive search (each
line's union of quantised colours <= 15). No partition -> the bake
fails loudly; it never emits a table with a nearest-colour fallback.

Inputs:
  --harvest  tools/palharvest_tiles_ares.py output (T lines carry, per
             live set, the runtime's own mdp_s_used mask). Each sample
             is classified to a scene by its PAL_SH against the anchors
             (the bake's clustering rule); its masks join that scene.
  --state    an ares .bs1: the sets in md_tag + mdp_s_used, classified
             the same way (Mike's savestates are the truth samples).
Colours come from the scene ANCHOR image, quantised exactly as the
runtime does (mdp_quant: (v5+2)>>2, clamp 7), so a set whose colours
are fading in a sample is still baked at its resting colours.
"""
import argparse, struct, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / 'discover' / 'palscenes'
sys.path.insert(0, str(ROOT / 'tools'))
import palscene_bake as psb                      # anchors, TOL, REGION, load()

NLINES, NPENS = 3, 15
SD = 0x23B
MD_TAG, MDP_S_USED = 0x3B400, 0x3E380


def quant(v):                                    # == m_main.c mdp_quant
    r = ((((v) & 0xF) << 1) | ((v >> 12) & 1)) + 2
    g = ((((v >> 4) & 0xF) << 1) | ((v >> 13) & 1)) + 2
    b = ((((v >> 8) & 0xF) << 1) | ((v >> 14) & 1)) + 2
    r, g, b = min(7, r >> 2), min(7, g >> 2), min(7, b >> 2)
    return (b << 6) | (g << 3) | r


def classify(pal, scenes):
    best = min(scenes, key=lambda a: sum(1 for i in range(psb.REGION) if pal[i] != scenes[a][i]))
    d = sum(1 for i in range(psb.REGION) if pal[i] != scenes[best][i])
    return (best if d <= psb.TOL else None), d


def read_harvest(path, scenes, masks, seen):
    n = 0
    for ln in open(path):
        if not ln.startswith('T '):
            continue
        parts = ln.split()
        frame = int(parts[1])
        pal = [0] * psb.WORDS
        recs = []
        for tok in parts[4:]:
            f = tok.split(',')
            c, mu = int(f[0], 16), int(f[1], 16)
            words = [int(x, 16) for x in f[3:11]]
            pal[c * 8:c * 8 + 8] = words
            recs.append((c, mu))
        # the harvest only carries LIVE sets' words; classify on those
        # words only (unrecorded sets stay 0 and would swamp the distance)
        live_idx = [c * 8 + k for c, _ in recs for k in range(8)]
        best = min(scenes, key=lambda a: sum(1 for i in live_idx if pal[i] != scenes[a][i]))
        d = sum(1 for i in live_idx if pal[i] != scenes[best][i])
        if d > psb.TOL:
            continue                                 # a fade/flash sample
        for c, mu in recs:
            if mu:
                masks[best][c] = masks[best].get(c, 0) | mu
        seen[best] += 1
        n += 1
    return n


def read_state(path, scenes, masks, seen):
    raw = open(path, 'rb').read()
    sd = raw[SD:SD + 0x40000]
    m = bytes(x for i in range(0, len(sd), 2) for x in (sd[i + 1], sd[i]))
    pal = list(struct.unpack_from(f'>{psb.WORDS}H', m, 0x27000))
    best, d = classify(pal, scenes)
    if best is None:
        sys.exit(f'{path}: matches no anchor ({d} words off)')
    tags = struct.unpack_from('>1024I', m, MD_TAG)
    used = m[MDP_S_USED:MDP_S_USED + 128]
    for t in tags:
        if t == 0xFFFFFFFF:
            continue
        c = (t >> 16) & 0x7F
        if used[c]:
            masks[best][c] = masks[best].get(c, 0) | used[c]
    seen[best] += 1
    print(f'  state {Path(path).name}: {best} ({d} words off)')


def partition(colsets):
    """colsets: {set: frozenset(colours)} -> [line][set] or None."""
    order = sorted(colsets, key=lambda s: -len(colsets[s]))
    sys.setrecursionlimit(10000)

    def go(i, lines, assign):
        if i == len(order):
            return assign
        s = order[i]
        tried = set()
        for li in range(NLINES):
            key = lines[li]
            if key in tried:                     # symmetric empty/equal lines
                continue
            tried.add(key)
            u = lines[li] | colsets[s]
            if len(u) <= NPENS:
                r = go(i + 1, [u if j == li else lines[j] for j in range(NLINES)],
                       {**assign, s: li})
                if r:
                    return r
        return None
    return go(0, [frozenset()] * NLINES, {})


def bake_scene(name, anchor, masks):
    colsets = {}
    for s, mu in sorted(masks.items()):
        cs = frozenset(quant(anchor[s * 8 + p]) for p in range(8) if mu & (1 << p))
        if cs:
            colsets[s] = cs
    allc = set().union(*colsets.values()) if colsets else set()
    assign = partition(colsets)
    if assign is None:
        sys.exit(f'BAKE FAIL: scene {name}: {len(colsets)} sets / {len(allc)} colours '
                 f'have no exact {NLINES}x{NPENS} partition — a real capacity limit, '
                 f'not a bug to paper over')
    line_c = [[0xFFFF] * 16 for _ in range(NLINES)]
    s_line = [0] * 128
    s_map = [[0] * 8 for _ in range(128)]
    s_used = [0] * 128
    for s, li in assign.items():
        s_line[s] = li + 1
        s_used[s] = masks[s]
        for p in range(8):
            if not masks[s] & (1 << p):
                continue
            q = quant(anchor[s * 8 + p])
            row = line_c[li]
            if q in row[1:]:
                pen = row.index(q)
            else:
                pen = row.index(0xFFFF, 1)
                row[pen] = q
            s_map[s][p] = pen
    loads = [sum(1 for v in line_c[l][1:] if v != 0xFFFF) for l in range(NLINES)]
    print(f'  {name}: {len(colsets)} sets, {len(allc)} colours -> lines {loads} '
          f'({NLINES * NPENS - sum(loads)} pens spare)')
    return line_c, s_line, s_map, s_used


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--harvest', action='append', default=[])
    ap.add_argument('--state', action='append', default=[])
    ap.add_argument('--stats', action='store_true')
    ap.add_argument('--out', default=str(ROOT / 'sh_src' / 'pal_scenes_md.h'))
    a = ap.parse_args()
    if not a.harvest and not a.state:
        sys.exit('need --harvest and/or --state inputs')
    scenes = {s: psb.load(SRC / f'{s}.palsh') for s in psb.ANCHORS}
    masks = {s: {} for s in psb.ANCHORS}
    seen = {s: 0 for s in psb.ANCHORS}
    for h in a.harvest:
        n = read_harvest(h, scenes, masks, seen)
        print(f'  harvest {Path(h).name}: {n} samples classified')
    for st in a.state:
        read_state(st, scenes, masks, seen)
    print('  samples per scene:', seen)
    tables = {}
    for name in psb.LOADABLE:
        src = name
        if not masks[name]:
            # boss_smoke is detect-only and plays over normal's tiles;
            # any loadable scene with no witnesses inherits normal
            src = 'normal'
            print(f'  {name}: no tile witnesses, inheriting {src}')
        tables[name] = bake_scene(name, scenes[src], masks[src])
    if a.stats:
        return
    with open(a.out, 'w') as f:
        f.write('/* GENERATED by tools/mdpen_bake.py — do not edit.\n'
                ' * Per-scene static MD pen tables; docs/design/STATIC-SCENE.md.\n'
                f' * Scenes: {", ".join(psb.LOADABLE)} (index = pscene id). */\n')
        f.write('#define MDSTATIC_N %d\n' % len(psb.LOADABLE))
        f.write('static const uint16_t mds_line_c[MDSTATIC_N][48]\n    __attribute__((section(".palscenes"))) = {\n')
        for name in psb.LOADABLE:
            lc = tables[name][0]
            f.write('    { ' + ', '.join(f'0x{v:04X}' for row in lc for v in row) + f' },   /* {name} */\n')
        f.write('};\nstatic const uint8_t mds_s_line[MDSTATIC_N][128]\n    __attribute__((section(".palscenes"))) = {\n')
        for name in psb.LOADABLE:
            f.write('    { ' + ', '.join(str(v) for v in tables[name][1]) + ' },\n')
        f.write('};\nstatic const uint8_t mds_s_map[MDSTATIC_N][1024]\n    __attribute__((section(".palscenes"))) = {\n')
        for name in psb.LOADABLE:
            f.write('    { ' + ', '.join(str(v) for row in tables[name][2] for v in row) + ' },\n')
        f.write('};\nstatic const uint8_t mds_s_used[MDSTATIC_N][128]\n    __attribute__((section(".palscenes"))) = {\n')
        for name in psb.LOADABLE:
            f.write('    { ' + ', '.join(f'0x{v:02X}' for v in tables[name][3]) + ' },\n')
        f.write('};\n')
    print('wrote', a.out)


if __name__ == '__main__':
    main()
