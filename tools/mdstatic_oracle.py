#!/usr/bin/env python3
"""Diff the INSTALLED MD pen tables against the arcade's own palette RAM.

    python3 tools/mdstatic_oracle.py            # round tables, per round
    python3 tools/mdstatic_oracle.py --scenes    # the old 2-slot table
    python3 tools/mdstatic_oracle.py --pens      # dropped-pen audit

Two generated headers are #included by sh_src/m_main.c and nothing else
in the build is:

    pal_rounds_md.h   mdr_*  5 round-keyed tables   (LOOP29 192-195)
    pal_scenes_md.h   mds_*  2 pscene-keyed tables  (what vi39 ships)

`tools/palette_oracle.py` verified a THIRD artifact -- bake_tilecram's
pack out of discover/cram/wide -- and sh_src/tilecram.bin is referenced by
no build source, so that result does not reach either shipping table.
This closes the gap by walking the installed table the way the SH-2 does

    line = s_line[p]                 0 = set absent, software draws it
    pen  = s_map[p*8 + pixel]        pixel 1..7
    col  = line_c[(line-1)*16 + pen]

and comparing that 9-bit colour against mdpen_bake.quant of the arcade
word in discover/cram/arcade/sceneN.bin.

Three outcomes, and they are NOT the same defect:
  ABSENT        s_line 0. By design: the framebuffer draws the set.
  DROPPED PEN   0xFFFF, i.e. MD pixel 0, i.e. transparent -- a hole, not
                a hue. Only matters if the tiles use that pen: --pens
                decodes the 3bpp tile roms for every tile the scene's map
                points at and reports just those.
  WRONG COLOUR  a pack fault. The table disagrees with the hardware.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import palette_oracle as po
from mdpen_bake import quant

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GFX = ('opr-11674.a14', 'opr-11675.a15', 'opr-11676.a16')
FG_PAGE, BG_PAGE = 0, 5                # measured live, LOOP-DECOMPILE 59
SCENES = 5


def header(scenes):
    if scenes:
        return os.path.join(ROOT, 'sh_src', 'pal_scenes_md.h'), 'mds'
    return os.path.join(ROOT, 'sh_src', 'pal_rounds_md.h'), 'mdr'


def arrays(txt, name, hdr):
    m = re.search(r'%s\s*\[[^\]]*\]\s*\[[^\]]*\]\s*=\s*\{(.*?)\n\};' % name,
                  txt, re.S)
    if not m:
        sys.exit('%s: no table %s' % (hdr, name))
    return [[int(v, 0) for v in re.findall(r'0x[0-9A-Fa-f]+|\d+', row)]
            for row in re.findall(r'\{([^{}]*)\}', m.group(1))]


def tables(scenes):
    hdr, pre = header(scenes)
    txt = open(hdr).read()
    return (arrays(txt, pre + '_line_c', hdr),
            arrays(txt, pre + '_s_line', hdr),
            arrays(txt, pre + '_s_map', hdr),
            arrays(txt, pre + '_s_used', hdr), hdr)


def rom():
    return open(os.path.join(ROOT, po.ROM), 'rb').read()


def map_palettes(d, sc, pages=None):
    """Tile palettes the scene's own tilemap references -> the tiles."""
    o = 0x1CE2 + 6 * sc
    words = po.unpack(d, int.from_bytes(d[o + 2:o + 6], 'big'))
    out = {}
    for page in (pages if pages is not None else range(8)):
        for t in words[page * 2048:(page + 1) * 2048]:
            if t & 0x1FFF:
                out.setdefault((t >> 6) & 0x7F, set()).add(t & 0x1FFF)
    return out


def arcade(sc):
    return open(os.path.join(ROOT, 'discover', 'cram', 'arcade',
                             'scene%d.bin' % sc), 'rb').read()


def colours(scenes, everything):
    line_c, s_line, s_map, _u, hdr = tables(scenes)
    d = rom()
    rc = 0
    print('%s: %d table(s)' % (os.path.basename(hdr), len(s_line)))
    for sc in range(SCENES):
        t = 0 if scenes else sc                 # round tables key by round
        if t >= len(s_line):
            continue
        ref = arcade(sc)
        look = range(128) if everything else sorted(map_palettes(d, sc))
        absent, drop, bad = [], [], []
        for p in look:
            li = s_line[t][p]
            if not li:
                absent.append(p)
                continue
            want = [quant(int.from_bytes(ref[p * 16 + 2 * k:p * 16 + 2 * k + 2],
                                         'big')) for k in range(1, 8)]
            got = [line_c[t][(li - 1) * 16 + s_map[t][p * 8 + k]]
                   for k in range(1, 8)]
            if want == got:
                continue
            if all(g == 0xFFFF or g == w for g, w in zip(got, want)):
                drop.append((p, [k + 1 for k, g in enumerate(got)
                                 if g == 0xFFFF]))
            else:
                bad.append((p, li, want, got))
        print('  table %d vs arcade scene %d: %d palettes (%s)  '
              '%d absent, %d dropped-pen, %d WRONG COLOUR'
              % (t, sc, len(list(look)),
                 'all 128' if everything else 'map-referenced',
                 len(absent), len(drop), len(bad)))
        for p, pens in drop:
            print('     pal %3d drops pen(s) %s' % (p, pens))
        for p, li, want, got in bad:
            print('     pal %3d line %d  arcade %s' %
                  (p, li, ' '.join('%03X' % v for v in want)))
            print('                     table  %s' %
                  ' '.join('%03X' % v for v in got))
        rc += len(bad)
    return 1 if rc else 0


def pens(scenes):
    _c, s_line, s_map, s_used, hdr = tables(scenes)
    planes = [open(os.path.join(ROOT, 'roms', 'altbeast', n), 'rb').read()
              for n in GFX]

    def tile_pens(t):
        out = set()
        if t * 8 + 8 > len(planes[0]):
            return out
        for y in range(8):
            b = [pl[t * 8 + y] for pl in planes]
            for x in range(8):
                k = 7 - x
                out.add((((b[2] >> k) & 1) << 2) | (((b[1] >> k) & 1) << 1)
                        | ((b[0] >> k) & 1))
        return out

    d = rom()
    rc = 0
    print('%s: dropped pens the tiles actually USE' % os.path.basename(hdr))
    for sc in range(SCENES):
        t = 0 if scenes else sc
        if t >= len(s_line):
            continue
        byp = map_palettes(d, sc, (FG_PAGE, BG_PAGE))
        holes = []
        for p, tiles in sorted(byp.items()):
            if not s_line[t][p]:
                continue                  # software draws it, not a hole
            real = set()
            for tt in tiles:
                real |= tile_pens(tt)
            real.discard(0)
            miss = sorted(k for k in real if s_map[t][p * 8 + k] == 0)
            if miss:
                holes.append((p, miss, sorted(real), s_used[t][p], len(tiles)))
        print('  table %d scene %d: %d palettes on pages %d/%d, %d drop a pen '
              'their tiles use'
              % (t, sc, len(byp), FG_PAGE, BG_PAGE, len(holes)))
        for p, miss, real, us, n in holes:
            print('     pal %3d drops %s  tiles use %s  s_used %02X  (%d tiles)'
                  % (p, miss, real, us, n))
        rc += len(holes)
    return 1 if rc else 0


def main():
    scenes = '--scenes' in sys.argv
    if '--pens' in sys.argv:
        return pens(scenes)
    return colours(scenes, '--all' in sys.argv)


if __name__ == '__main__':
    sys.exit(main())
