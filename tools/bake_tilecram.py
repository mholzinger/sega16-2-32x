#!/usr/bin/env python3
"""Bake the per-scene MD CRAM assignment for the TILE layers.

    tools/bake_tilecram.py [--stats]

The whole tile layer of every scene fits in the Mega Drive's four CRAM
lines (docs/log/LOOP-DECOMPILE.md 60, 61), which means both tile planes
can be VDP-rendered instead of composed in software.

Two facts make it work and neither is a compromise:
  - MD CRAM is 3 bits per channel against System 16's 5, so colours
    collapse on quantisation. ARCHITECTURE.md:838 already measured that
    loss at max 2 in 0-31 and called the images indistinguishable; this
    reuses it rather than introducing anything.
  - System 16 tile palettes share colours heavily: scene 0's worst
    viewport uses 25 palettes drawn from just 24 distinct MD colours.

Emits per scene: four 16-entry CRAM lines, and for each System 16 tile
palette the line it lives on plus the slot each of its 7 pens maps to.
That pen map is what a tile bake needs to rewrite pixel values with.

  sh_src/tilecram.bin   5 scenes x 4 lines x 16 words (MD colour format)
  sh_src/tilecram.h     per-palette line + pen mapping
"""
import argparse
import os
import random
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.environ.get('GAME', 'altbeast')
ROM = os.path.join(ROOT, 'roms', GAME, 'prog68k.bin')
SCENES, TILES_N, SLOTS = 5, 20480, 16
# LOOP29 197: THREE, not four. m_main.c:326 sets MDP_LINES 3 by default and
# the fourth line is MDP_LINES4, which carries `#error "MDP_LINES4 takes the
# MD sprite line for tiles"` against MD_SPR -- and every shipping build has
# MD_SPR. mdpen_bake has always used NLINES=3, which is why vi39's table
# leaves the fourth block 0xFFFF. Packing into four lines silently assigns
# sets to a line the background allocator does not own: in round 0 that was
# the SKY (92, 93) and EVERY TREE (95-99).
LINES = int(os.environ.get('TILECRAM_LINES', '3'))
FG_PAGE, BG_PAGE = 0, 5          # measured live, entry 59
VIS_ROWS, VIS_COLS = range(4, 32), 40


def load():
    with open(ROM, 'rb') as fh:
        return fh.read()


def w16(rom, o):
    return (rom[o] << 8) | rom[o + 1]


def md(v):
    """System 16 colour word -> MD 3-bit-per-gun triple.

    LOOP29 196: this MUST be the runtime's own mdp_quant (m_main.c:1692):
    +2 then >>2, clamped to 7. It used to truncate, which put every table
    colour one step dark -- and because the SH-2's drift check compares a
    table colour against mdp_quant of the live word (m_main.c:1842,
    2124-2157), a truncated table disagrees with the runtime on 412 of the
    map-referenced pens and is freed and re-claimed.
    """
    r = min(7, ((((v >> 0) & 0xF) << 1 | ((v >> 12) & 1)) + 2) >> 2)
    g = min(7, ((((v >> 4) & 0xF) << 1 | ((v >> 13) & 1)) + 2) >> 2)
    b = min(7, ((((v >> 8) & 0xF) << 1 | ((v >> 14) & 1)) + 2) >> 2)
    return (r, g, b)


def md_word(c):
    return (c[2] << 9) | (c[1] << 5) | (c[0] << 1)   # MD CRAM: 0000BBB0GGG0RRR0


def md_pack9(c):
    """The 9-bit form mdp_line_c holds (m_main.c:333, built at 1692).

    LOOP29 196: the emitted round tables used to carry md_word() here, so
    the SH-2 re-read every colour with the wrong field positions -- white
    (7,7,7) = 0xEEE came back as (6,5,3). That is the pink trees.
    """
    return (c[2] << 6) | (c[1] << 3) | c[0]


def unpack(rom, ptr):
    hi = bytearray(); p = ptr
    while len(hi) < TILES_N and p + 1 < len(rom):
        hi.extend(bytes([rom[p + 1]]) * (rom[p] + 1)); p += 2
    lo = bytearray()
    while len(lo) < TILES_N and p < len(rom):
        d0 = rom[p]; p += 1
        if d0:
            lo.append(d0); continue
        d2 = rom[p]; p += 1
        if d2 == 0:
            lo.append(0); continue
        lo.extend(b'\x00' * d2)
    return [(hi[i] << 8) | lo[i % len(lo)] for i in range(TILES_N)]


def worst_viewport(words, cols):
    best = None
    fg = words[FG_PAGE * 2048:(FG_PAGE + 1) * 2048]
    bg = words[BG_PAGE * 2048:(BG_PAGE + 1) * 2048]
    for c0 in range(64):
        pal = set()
        for r in VIS_ROWS:
            for dc in range(VIS_COLS):
                c = (c0 + dc) & 63
                for t in (fg[r * 64 + c], bg[r * 64 + c]):
                    if t & 0x1FFF:
                        pal.add((t >> 6) & 0x7F)
        u = set()
        for p in pal:
            u |= cols(p)
        if best is None or len(u) > best[1]:
            best = (sorted(pal), len(u))
    return best[0]


def pack(pal, cols, order, overflow=None):
    """Best-fit: every palette must sit ENTIRELY inside one line, because a
    tile selects one line for all its pens.

    OVERFLOW (LOOP29 189, Mike's option B): a palette that does not fit is
    no longer a bake failure. It is left UNASSIGNED and the framebuffer
    draws its cells in software, the way the cat-1 pass already draws a
    cell whose set has no MD line. Graceful degradation instead of an
    all-or-nothing bake, which is what makes the scenes that do not pack
    shippable at all. Pass `overflow` as a list to collect them."""
    groups = [set() for _ in range(LINES)]
    for p in order:
        c = cols(p)
        cand = sorted((len(g | c) - len(g), i) for i, g in enumerate(groups)
                      if len(g | c) <= SLOTS - 1)
        if not cand:
            if overflow is None:
                return None
            overflow.append(p)
            continue
        groups[cand[0][1]] |= c
    return groups


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--stats', action='store_true')
    ap.add_argument('--live', nargs='*', default=[],
                    help='live palette dumps (WRAM 0xFF9000, 0x800 bytes). '
                         'Colours for --live-scene come from their UNION, '
                         'which covers every colour-cycler state sampled.')
    ap.add_argument('--live-scene', type=int, default=0)
    ap.add_argument('--live-dir', default=None,
                    help='directory of GATED per-scene palette dumps named '
                         's<scene>_*.bin (discover/cram/wide). Colours for '
                         'EVERY scene come from its own dumps, unioned -- '
                         'which fixes the blocker the decompile thread '
                         'flagged: --live applied live colours to ONE scene '
                         'and fell back to rom for the other four, and rom '
                         'is wrong above palette 63 (LOOP29 192).')
    ap.add_argument('--also', default='',
                    help='extra palettes to pack and PIN, comma-separated. '
                         'The worst-case viewport only sees palettes the '
                         'STATIC scene tilemap references (72-103 for scene '
                         '0); the sets that actually churn are 33 and 37-46, '
                         'measured live off mdp_free_set (LOOP29 189). '
                         'Neither the viewport nor mdpen_bake\'s harvest '
                         'contains them, so they have to be named.')
    ap.add_argument('--emit-mds', metavar='OUT', default=None,
                    help='also write pal_scenes_md.h in the RUNTIME table '
                         'format, using --live-scene\'s pack for every '
                         'PALSTATIC scene slot. This is what pins the sets '
                         'so mdp_free_set cannot churn them (LOOP29 189).')
    a = ap.parse_args()
    # LOOP29 174. The rom block at 0x232A0+blk*0x400 holds 64 palettes of
    # 16 bytes, so `base + p*16` is OUT OF RANGE for p >= 64 -- and scene
    # 0's own viewport uses palettes 72-103. The plan (PLAN-TILES-TO-VDP,
    # "what is NOT verified") flags this for scenes 1-4; it hits scene 0
    # too. Live dumps are the only correct source above 63.
    # per-scene live colours: {scene: {pal: set(colours)}} and a
    # representative per-pixel list for the pen map
    slive, slivepix = {}, {}
    if a.live_dir:
        import glob as _glob
        for sc in range(SCENES):
            fs = sorted(_glob.glob(os.path.join(a.live_dir,
                                                's%d_*.bin' % sc)))
            if not fs:
                continue
            u, px = {}, {}
            for fn in fs:
                d = open(fn, 'rb').read()
                if len(d) < 0x800:
                    sys.exit('%s: want >= 0x800 bytes of palette ram' % fn)
                for pp in range(128):
                    u.setdefault(pp, set()).update(
                        md(w16(d, pp * 16 + 2 * k)) for k in range(1, 8))
                    # LOOP29 195: the pen MAP must be the palette's
                    # RESTING state, not whichever dump sorted first.
                    # setdefault took file[0], and if that sample is
                    # mid-fade every set's pixel->slot mapping is built
                    # from fade colours -- which is what turned level 1's
                    # trees pink and banded its sky. Tally the per-pixel
                    # vectors and take the MODE below.
                    px.setdefault(pp, {})
                    key = tuple(md(w16(d, pp * 16 + 2 * k))
                                for k in range(1, 8))
                    px[pp][key] = px[pp].get(key, 0) + 1
            # collapse each palette's tally to its most common vector
            px = {pp: list(max(v.items(), key=lambda kv: kv[1])[0])
                  for pp, v in px.items()}
            slive[sc], slivepix[sc] = u, px
            print('live scene %d: %d gated dumps' % (sc, len(fs)))
    live = None
    livepix = {}
    if a.live:
        live = {}
        for fn in a.live:
            d = open(fn, 'rb').read()
            if len(d) < 0x800:
                sys.exit('%s: want 0x800 bytes of WRAM 0xFF9000' % fn)
            for p in range(128):
                # per-PIXEL colours, order preserved (pens 1..7). The
                # union across dumps is what the PACKER needs; the
                # per-pixel list is what a pen MAP needs, and collapsing
                # to a set destroys the pixel->colour correspondence.
                live.setdefault(p, set()).update(
                    md(w16(d, p * 16 + 2 * k)) for k in range(1, 8))
                livepix.setdefault(p, [md(w16(d, p * 16 + 2 * k))
                                       for k in range(1, 8)])
        print('live colours from %d dump(s), applied to scene %d'
              % (len(a.live), a.live_scene))
    rom = load()
    out_bin, out_h, out_col = [], [], []
    overflow = {}          # scene -> palettes the framebuffer must draw
    for s in range(SCENES):
        o = 0x1CE2 + 6 * s
        blk = w16(rom, o) & 3
        base = 0x232A0 + blk * 0x400
        words = unpack(rom, int.from_bytes(rom[o + 2:o + 6], 'big'))

        def cols(p, _b=base, _s=s):
            if _s in slive:
                return frozenset(slive[_s][p])
            if live is not None and _s == a.live_scene:
                return frozenset(live[p])
            return frozenset(md(w16(rom, _b + p * 16 + 2 * k)) for k in range(1, 8))

        pal = worst_viewport(words, cols)
        if a.also and s == a.live_scene:
            extra = [int(x) for x in a.also.split(',') if x.strip()]
            pal = sorted(set(pal) | {e for e in extra if len(cols(e)) > 0})
        order = sorted(pal, key=lambda p: -len(cols(p)))
        groups = pack(pal, cols, order)
        if groups is None:
            # exhaustive-ish retry FIRST: an overflow we could have avoided
            # by reordering is not an overflow.
            random.seed(7)
            best = None
            for _ in range(20000):
                sh = order[:]; random.shuffle(sh)
                groups = pack(pal, cols, sh)
                if groups:
                    break
                ov = []
                g2 = pack(pal, cols, sh, ov)
                if best is None or len(ov) < len(best[1]):
                    best = (g2, ov)
            if groups is None:
                groups, over = best
                overflow[s] = over
        if groups is None:
            sys.exit('scene %d: no %d-line packing found' % (s, LINES))

        slot = []
        for g in groups:
            m = {c: i + 1 for i, c in enumerate(sorted(g))}   # slot 0 = transparent
            slot.append(m)
        assign = {}
        for p in pal:
            if p in overflow.get(s, []):
                continue               # unassigned: the FB draws it
            c = cols(p)
            for li, g in enumerate(groups):
                if c <= g:
                    if s in slive:
                        assign[p] = (li, [slot[li][c] for c in slivepix[s][p]])
                    elif live is not None and s == a.live_scene:
                        # BUG FIXED (LOOP29 176): this read `sorted(live[p])`,
                        # a SET, so the emitted pen map was in colour order
                        # and not pixel order -- every tile would have
                        # indexed the wrong slots. Use the per-pixel list.
                        assign[p] = (li, [slot[li][c] for c in livepix[p]])
                    else:
                        assign[p] = (li, [slot[li][md(w16(rom, base + p * 16
                                                          + 2 * k))]
                                          for k in range(1, 8)])
                    break
        line_cols, line_words = [], []
        for li, g in enumerate(groups):
            row = [None] * SLOTS          # None = free; (0,0,0) = BLACK
            for c, i in slot[li].items():
                row[i] = c
            line_cols.append(row)
            line_words.append([0 if c is None else md_word(c) for c in row])
        out_bin.append(line_words)
        out_col.append(line_cols)
        out_h.append((s, len(pal), [len(g) for g in groups], assign))
        ov = overflow.get(s, [])
        print('scene %d: %2d palettes, lines %s, %d slots used%s'
              % (s, len(pal), [len(g) for g in groups],
                 sum(len(g) for g in groups),
                 '' if not ov else
                 '  OVERFLOW %d to the framebuffer: %s'
                 % (len(ov), sorted(ov))))

    if a.stats:
        return
    with open(os.path.join(ROOT, 'sh_src', 'tilecram.bin'), 'wb') as fh:
        for sc in out_bin:
            for row in sc:
                for v in row:
                    fh.write(bytes([(v >> 8) & 0xFF, v & 0xFF]))
    with open(os.path.join(ROOT, 'sh_src', 'tilecram.h'), 'w') as fh:
        fh.write('/* generated by tools/bake_tilecram.py — per-scene MD CRAM\n'
                 ' * for the tile layers, plus the line and pen mapping for\n'
                 ' * every System 16 tile palette in the worst-case viewport.\n'
                 ' * See docs/log/LOOP-DECOMPILE.md 60-61. */\n')
        fh.write('#define TILECRAM_SCENES %d\n#define TILECRAM_LINES %d\n'
                 % (SCENES, LINES))
        for s, n, sizes, assign in out_h:
            fh.write('/* scene %d: %d palettes, line fill %s */\n' % (s, n, sizes))
            fh.write('static const unsigned char tilepal_line_%d[128] = {' % s)
            fh.write(','.join(str(assign.get(p, (0xFF, None))[0] & 0xFF)
                              for p in range(128)))
            fh.write('};\n')
    print('wrote sh_src/tilecram.bin and sh_src/tilecram.h')

    if a.emit_mds:
        # LOOP29 189 — THE RUNTIME TABLES. mds_install already installs
        # per-scene line/map/used tables and PINS every set in them
        # against mdp_free_set (LOOP29 156's mds_pin), which is the whole
        # reason CAT1MD churns: its sets are not in any table. This emits
        # that format from the WORST-CASE VIEWPORT -- exhaustive over all
        # 64 scroll positions, both planes -- instead of mdpen_bake's
        # sampled harvest, which never sees the cat-1 sets at all.
        # The runtime's scene space is PALSTATIC's (normal/boss_smoke),
        # not the game's five rounds; level 1 is 'normal' and boss_smoke
        # inherits it, so one pack fills both slots.
        # LOOP29 192: emit ALL FIVE scenes, keyed by the game's ROUND
        # index, not by the PALSTATIC scene. The runtime's old table space
        # was normal/boss_smoke -- palette-DETECTED scenes -- which is
        # orthogonal to the game's rounds and is why the refuse rule
        # blanked level 2 and the transition (191). The round is the
        # game's own scene variable; the 68K has to publish it, and
        # COMM10 bits 13-15 are spare (LOOP29 187), which is 3 bits for 5
        # rounds.
        nsc = SCENES
        sl_all, su_all, sm_all, lc_all = [], [], [], []
        for sc in range(nsc):
            line_words, (scn, npal, sizes, assign) = out_col[sc], out_h[sc]
            s_line = [0] * 128
            s_used = [0] * 128
            s_map = [[0] * 8 for _ in range(128)]
            for pp, (li, slots) in assign.items():
                s_line[pp] = li + 1
                s_used[pp] = 0xFE
                s_map[pp] = [0] + list(slots)
            sl_all.append(s_line); su_all.append(s_used); sm_all.append(s_map)
            lc_all.append(line_words)
            print('  emit-mds: round %d -> %d sets pinned, lines %s'
                  % (sc, sum(1 for v in s_line if v), sizes))
        with open(a.emit_mds, 'w') as fh:
            fh.write('/* GENERATED by tools/bake_tilecram.py --emit-mds.\n'
                     ' * Per-ROUND static MD pen tables for the TILE layers,\n'
                     ' * from each round\'s own GATED live palette dumps\n'
                     ' * (discover/cram/wide) and the worst-case viewport\n'
                     ' * over all 64 scroll positions, both planes.\n'
                     ' * Indexed by the GAME\'S ROUND (0-4), NOT by the\n'
                     ' * palette-detected scene -- see LOOP29 192 and\n'
                     ' * LOOP-DECOMPILE 66. */\n')
            fh.write('#define MDROUND_N %d\n' % nsc)
            fh.write('static const uint16_t mdr_line_c[MDROUND_N][%d] = {\n'
                     % (LINES * SLOTS))
            for sc in range(nsc):
                fh.write('    { %s },\n'
                         % ', '.join('0xFFFF' if c is None
                                     else '0x%04X' % md_pack9(c)
                                     for ln in lc_all[sc] for c in ln))
            fh.write('};\nstatic const uint8_t mdr_s_line[MDROUND_N][128] = {\n')
            for sc in range(nsc):
                fh.write('    { %s },\n'
                         % ', '.join(str(v) for v in sl_all[sc]))
            fh.write('};\nstatic const uint8_t mdr_s_map[MDROUND_N][1024] = {\n')
            for sc in range(nsc):
                fh.write('    { %s },\n'
                         % ', '.join(str(v) for r in sm_all[sc] for v in r))
            fh.write('};\nstatic const uint8_t mdr_s_used[MDROUND_N][128] = {\n')
            for sc in range(nsc):
                fh.write('    { %s },\n'
                         % ', '.join('0x%02X' % v for v in su_all[sc]))
            fh.write('};\n')
        print('  emit-mds: wrote', a.emit_mds)
        return
        sc = a.live_scene
        line_words, (scn, npal, sizes, assign) = out_bin[sc], out_h[sc]
        s_line = [0] * 128
        s_used = [0] * 128
        s_map = [[0] * 8 for _ in range(128)]
        for p, (li, slots) in assign.items():
            s_line[p] = li + 1
            s_used[p] = 0xFE            # pixels 1-7; 0 is transparent
            s_map[p] = [0] + list(slots)
        n_assigned = sum(1 for v in s_line if v)
        print('  emit-mds: scene %d -> %d sets PINNED across %d lines %s'
              % (sc, n_assigned, LINES, sizes))
        if n_assigned != npal:
            print('  emit-mds: %d of %d palettes UNASSIGNED (the framebuffer '
                  'draws them)' % (npal - n_assigned, npal))
        names = ['normal', 'boss_smoke']
        with open(a.emit_mds, 'w') as fh:
            fh.write('/* GENERATED by tools/bake_tilecram.py --emit-mds.\n'
                     ' * Per-scene static MD pen tables for the TILE layers,\n'
                     ' * from the worst-case VIEWPORT (all 64 scroll\n'
                     ' * positions, both planes) rather than a sampled\n'
                     ' * harvest -- so the cat-1 sets are in it and\n'
                     ' * mds_pin can stop mdp_free_set churning them.\n'
                     ' * Source scene %d: %d palettes, lines %s.\n'
                     ' * See LOOP29 189 and PLAN-TILES-TO-VDP. */\n'
                     % (sc, npal, sizes))
            fh.write('#define MDSTATIC_N %d\n' % len(names))
            fh.write('static const uint8_t mds_table_of[MDSTATIC_N] = { '
                     + ', '.join('0' for _ in names) + ' };\n')
            fh.write('static const uint16_t mds_line_c[MDSTATIC_N][%d] = {\n'
                     % (LINES * SLOTS))
            row = ', '.join('0x%04X' % (v if v else 0xFFFF)
                            for ln in line_words for v in ln)
            for nm in names:
                fh.write('    { %s },   /* %s */\n' % (row, nm))
            fh.write('};\nstatic const uint8_t mds_s_line[MDSTATIC_N][128] = {\n')
            for nm in names:
                fh.write('    { %s },\n' % ', '.join(str(v) for v in s_line))
            fh.write('};\nstatic const uint8_t mds_s_map[MDSTATIC_N][1024] = {\n')
            flat = ', '.join(str(v) for r in s_map for v in r)
            for nm in names:
                fh.write('    { %s },\n' % flat)
            fh.write('};\nstatic const uint8_t mds_s_used[MDSTATIC_N][128] = {\n')
            for nm in names:
                fh.write('    { %s },\n'
                         % ', '.join('0x%02X' % v for v in s_used))
            fh.write('};\n')
        print('  emit-mds: wrote', a.emit_mds)


if __name__ == '__main__':
    main()
