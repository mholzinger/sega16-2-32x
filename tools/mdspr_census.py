#!/usr/bin/env python3
"""WHICH MD CRAM LINES DOES THE SPRITE ATTRIBUTE TABLE NAME? (2026-10-08)

    tools/mdspr_census.py <rom> [--step N] [--from F] [--to F] [--jobs J]

The Mega Drive has four 16-colour CRAM lines. The port's MD-plane tile pens
occupy 48 of the 64 entries and the MD sprite offload (MD_SPR, m_main.c:6334)
owns the remaining 16. This asks the VDP's own copy of the table, not the SH-2
staging: VRAM 0xF000, 80 entries of 4 words, walked by its LINK field exactly
as the sprite engine walks it, and the palette line of each entry is
(attr >> 13) & 3.

One ares-headless run per sampled frame -- the dump reflects the END state of a
run whatever frame is named (the multi-dump trap), so there is no way to sample
a frame range in one pass. Per frame it also reads:

  MDSPR_CNT  sdram 0x28E20   [0] SAT entries shipped, [1] records claimed
                             -- CUMULATIVE since boot, so a range's claims are
                             its endpoints differenced and no sampling gap can
                             hide a claim that happened between two samples
  VDP CRAM   64 words        entries 1-15 are the sprite line, 16-63 the tiles
"""
import argparse, os, struct, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ARES = os.environ.get('ARES', os.path.expanduser(
    '~/src/ares-debug/build_macos/headless-ui/Release/ares-headless'))
SAT_VRAM, SAT_N = 0xF000, 80
MDSPR_CNT = 0x28E20
WORK = os.environ.get('MDSPR_WORK', '/tmp/mdspr_census')


def sample(rom, frame):
    os.makedirs(WORK, exist_ok=True)
    tag = os.path.join(WORK, '%s_%d' % (os.path.basename(rom).split('.')[0], frame))
    if not os.path.exists(tag + '.sat'):
        subprocess.run([ARES, rom, '--frames', str(frame),
                        '--dump', 'VDP VRAM:0x%x:0x%x:%s.sat' % (SAT_VRAM, SAT_N * 8, tag),
                        '--dump', 'VDP CRAM:0:0x80:%s.cram' % tag,
                        '--dump', 'sdram:0x%x:8:%s.cnt' % (MDSPR_CNT, tag)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    sat = open(tag + '.sat', 'rb').read()
    cram = open(tag + '.cram', 'rb').read()
    cnt = struct.unpack('>2I', open(tag + '.cnt', 'rb').read())
    return sat, cram, cnt


def walk(sat):
    """The entries the sprite engine actually renders: from 0, following LINK
    until it returns 0 or repeats. Returns [(index, y, size, attr, x)]."""
    w = struct.unpack('>%dH' % (len(sat) // 2), sat)
    out, i, seen = [], 0, set()
    while i < SAT_N and i not in seen:
        seen.add(i)
        y, sl, at, x = w[i * 4:i * 4 + 4]
        out.append((i, y, sl >> 8, at, x))
        nxt = sl & 0x7F
        if nxt == 0:
            break
        i = nxt
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom')
    ap.add_argument('--step', type=int, default=25)
    ap.add_argument('--from', dest='f0', type=int, default=25)
    ap.add_argument('--to', dest='f1', type=int, default=2150)
    ap.add_argument('--jobs', type=int, default=8)
    a = ap.parse_args()
    frames = list(range(a.f0, a.f1 + 1, a.step))
    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        rows = list(ex.map(lambda f: (f,) + sample(a.rom, f), frames))

    print('rom %s' % a.rom)
    print('%-7s %-6s %-5s %s %s  %s' % ('frame', 'live', 'drawn', 'SAT entries by CRAM line',
                                        '| cumulative', 'CRAM line words non-zero'))
    print('%-7s %-6s %-5s %-5s %-5s %-5s %-5s | %-7s %-7s %s' % (
        '', 'SAT', 'px', 'ln0', 'ln1', 'ln2', 'ln3', 'entries', 'claims', 'ln0 ln1 ln2 ln3'))
    for f, sat, cram, cnt in rows:
        ent = walk(sat)
        drawn = [e for e in ent if e[1] or e[3] or e[4]]
        by = [0, 0, 0, 0]
        for _, y, sz, at, x in drawn:
            by[(at >> 13) & 3] += 1
        cw = struct.unpack('>64H', cram)
        nz = [sum(1 for v in cw[l * 16:l * 16 + 16] if v) for l in range(4)]
        print('%-7d %-6d %-5d %-5d %-5d %-5d %-5d | %-7d %-7d %2d  %2d  %2d  %2d' % (
            f, len(ent), len(drawn), by[0], by[1], by[2], by[3], cnt[0], cnt[1],
            nz[0], nz[1], nz[2], nz[3]))


if __name__ == '__main__':
    main()
