#!/usr/bin/env python3
"""What every byte of the 68K rom is for, and who reads the rest.

    python3 tools/rom_map.py            # coverage + the work list
    python3 tools/rom_map.py --all      # every unattributed run

Code comes from `tools/code_stream.py`. Data regions come from ATTRIBUTED
below, which is the log's findings with their entry numbers. Everything
else is unattributed, and for each unattributed run this prints the
instructions that REFERENCE it — which is the method that cracked the two
big blocks in LOOP-DECOMPILE 48: finding who reads a block identifies it,
and a block nothing reads is a different kind of answer.

A reference is a pc-relative lea/pea target, an absolute operand, or an
immediate that lands inside the rom. The last is the loose one and it is
marked, because a coordinate or a count can look like a pointer.
"""
import re, sys, collections

STREAM = 'docs/audit/code_stream.txt'
ROM = 'roms/altbeast/prog68k.bin'

# start, end, what it is, the log entry that established it
ATTRIBUTED = [
    (0x00000, 0x00400, '68000 vector table', '-'),
    (0x01834, 0x01844, 'DEMO STREAM pointers, 4 entries indexed by '
     '(0xFFF031 & 0x18) >> 1', '78'),
    (0x01848, 0x01850, 'DIP start-round -> round', '66'),
    (0x018D4, 0x01927, 'attract prompt strings, drawn by 0x144A', '78'),
    (0x04128, 0x0413A, 'credit strings, drawn by 0x3AAE', '78'),
    (0x01858, 0x0185D, 'per-scene music command', '46'),
    (0x01CDA, 0x01CE2, 'round -> scene', '66'),
    (0x01CE2, 0x01D00, 'per-scene descriptor, 6-byte stride', '10/46'),
    (0x0326E, 0x03282, 'per-scene, 4-byte stride, read at 0x2B70', '46'),
    (0x032AE, 0x0336E, 'sky palette, gradient halves', '43/46'),
    (0x04050, 0x04110, 'sky palette, identical halves', '44/46'),
    (0x06DC0, 0x06DCA, 'per-scene word -> 0x73E4 and 0x6D70', '46'),
    (0x073DA, 0x073DF, 'per-scene actor palette id -> $0B', '46'),
    (0x092EA, 0x092EF, 'per-scene actor palette id -> $0B', '46'),
    (0x092F0, 0x09308, 'per-scene DISPATCH, jmp (a0)', '46'),
    (0x099A2, 0x099B6, 'per-scene -> $6C', '46'),
    (0x0DEC4, 0x0DED8, 'per-scene floor geometry', '35/46'),
    (0x173A0, 0x173A5, 'per-scene actor palette id -> $0B', '46'),
    (0x17E24, 0x17E38, 'per-scene DISPATCH, the level scripts', '46'),
    (0x1C622, 0x1D58E, 'five per-scene blocks of 0x288', '46'),
    (0x1D32A, 0x1D33E, 'per-scene, walked with fp=0xFFD800', '34/46'),
    (0x20000, 0x210C4, 'ZOOM SCALE TABLE, 32-byte rows, 0-31', '48'),
    (0x210C4, 0x21400, 'more of the zoom ladder', '46/48'),
    (0x232A0, 0x242A0, 'palette blocks, 4 x 0x400 (64 palettes of 16 bytes)',
     'LOOP29 174; bake_tilecram.py indexes base+blk*0x400, blk 0-3'),
    (0x242A0, 0x255E0, 'ACTOR PALETTE RECORDS, 176 x 28 bytes, indexed by '
     'object $0B', '77'),
    (0x255E0, 0x25A24, 'SPRITE FRAME TABLE, 182 x 6 bytes', '48'),
    (0x26C20, 0x2726C, 'tile upload block', '48'),
    (0x2726C, 0x278B8, 'tile upload block', '48'),
    (0x278B8, 0x28B84, 'tile upload block', '48'),
    (0x28B84, 0x29000, 'tile upload block', '48'),
    (0x3E4B0, 0x40000, 'DEMO INPUT STREAMS, 3 x 0x900, three bytes per '
     'frame (P1, P2, service)', '78'),
]

TILES_N = 20480          # 10 pages of 64x32 words, as bake_tilecram.py has it


def tilemaps(rom):
    """The five scene tilemaps, extents COMPUTED not guessed.

    The scene descriptor at 0x1CE2 carries a long tilemap pointer per
    scene (bake_tilecram.py reads the same field). Running the game's own
    two-pass RLE to completion from each pointer gives the end, and each
    end lands within ten bytes of the next scene's pointer — which is the
    check that the walk is right rather than merely plausible.
    """
    out = []
    for sc in range(5):
        o = 0x1CE2 + 6 * sc
        ptr = int.from_bytes(rom[o + 2:o + 6], 'big')
        p, n = ptr, 0
        while n < TILES_N and p + 1 < len(rom):      # high bytes, RLE
            n += rom[p] + 1
            p += 2
        n = 0
        while n < TILES_N and p < len(rom):          # low bytes, zero-RLE
            d0 = rom[p]
            p += 1
            if d0:
                n += 1
                continue
            d2 = rom[p]
            p += 1
            n += 1 if d2 == 0 else d2
        out.append((ptr, p, 'scene %d TILEMAP, two-pass RLE' % sc, '10/77'))
    return out


ANIM24 = re.compile(r'^#(-?\d+),%(?:fp|a\d)@\(36\)$')


def anim_runs(rows, rom):
    """Blocks the program installs as object $24, the anim script pointer.

    $24 is the anim script pointer in the struct map (entries 28/45), and
    LOOP-DECOMPILE 73 found three blocks that had been mistaken for
    functions because nothing else reads them. A longword written to $24
    is therefore an animation script by its consuming field, not by shape.
    """
    tg = sorted({int(m.group(1))
                 for a, n, mn, ops in rows if mn == 'movel'
                 for m in [ANIM24.match(ops)] if m and 0 <= int(m.group(1)) < len(rom)})
    runs, cur = [], [tg[0], tg[0], 1]
    for v in tg[1:]:
        if v - cur[1] > 512:
            runs.append(tuple(cur))
            cur = [v, v, 1]
        else:
            cur[1] = v
            cur[2] += 1
    runs.append(tuple(cur))
    return runs

PCREL = re.compile(r'%pc@\(0x([0-9a-f]+)\)')
ABS = re.compile(r'(?<![(#])0x([0-9a-f]{4,8})')
IMM = re.compile(r'#(-?\d+)')


def load_stream():
    rows = []
    for ln in open(STREAM):
        f = ln.rstrip('\n').split('\t')
        if len(f) >= 4:
            rows.append((int(f[0], 16), int(f[1]), f[2], f[3]))
    return rows


def main():
    rom = open(ROM, 'rb').read()
    rows = load_stream()
    cov = bytearray(len(rom))
    for a, n, mn, ops in rows:
        for i in range(a, min(a + n, len(rom))):
            cov[i] = 1
    code = sum(cov)
    named = 0
    regions = ATTRIBUTED + tilemaps(rom)
    for lo, hi, what, ent in regions:
        for i in range(lo, min(hi, len(rom))):
            if not cov[i]:
                named += 1
            cov[i] = 2

    # every rom address the code points at, and the instruction that does it
    refs = collections.defaultdict(list)
    for a, n, mn, ops in rows:
        if mn.startswith('.'):
            continue
        tgts = []
        for m in PCREL.finditer(ops):
            tgts.append((int(m.group(1), 16), 'lea'))
        for m in ABS.finditer(ops):
            v = int(m.group(1), 16)
            if v < len(rom) and not mn.startswith(('b', 'db', 'jsr', 'jmp', 'bsr')):
                tgts.append((v, 'abs'))
        for m in IMM.finditer(ops):
            v = int(m.group(1))
            if 0x400 <= v < len(rom):
                tgts.append((v, 'imm?'))
        for v, kind in tgts:
            refs[v].append((a, kind, mn, ops))

    # a pointer inside DATA is still a reference: the scene descriptor
    # reaches the tilemaps that way and nothing in the code does.
    for i in range(0, len(rom) - 3, 2):
        if cov[i]:
            continue
        v = int.from_bytes(rom[i:i + 4], 'big')
        if 0x400 <= v < len(rom):
            refs[v].append((i, 'data', 'long', 'at 0x%05X' % i))

    # runs the anim-script pointers land in are animation scripts
    for lo, hi, cnt in anim_runs(rows, rom):
        i = lo
        while i > 0 and cov[i - 1] == 0:
            i -= 1
        j = hi
        while j < len(rom) and cov[j] == 0:
            j += 1
        for k in range(i, j):
            cov[k] = 2
        named += j - i
        regions.append((i, j, 'animation scripts, %d entries (object $24)'
                        % cnt, '73/77'))

    runs = []
    s = None
    for i in range(len(rom)):
        if cov[i] == 0:
            if s is None:
                s = i
        elif s is not None:
            runs.append((i - s, s, i))
            s = None
    if s is not None:
        runs.append((len(rom) - s, s, len(rom)))

    data = len(rom) - code
    unattr = sum(L for L, _, _ in runs)
    print('rom          %6d bytes' % len(rom))
    print('  code       %6d  %.1f%%   (%d instructions)'
          % (code, 100 * code / len(rom), len(rows)))
    print('  data       %6d  %.1f%%' % (data, 100 * data / len(rom)))
    print('    named    %6d  %.1f%% of data  (%d regions)'
          % (named, 100 * named / data, len(regions)))
    print('    unnamed  %6d  %.1f%% of data  (%d runs)'
          % (unattr, 100 * unattr / data, len(runs)))
    print()
    runs.sort(reverse=True)
    show = runs if '--all' in sys.argv else runs[:25]
    print('UNATTRIBUTED RUNS, largest first — with who points into them')
    for L, lo, hi in show:
        inside = [(v, refs[v]) for v in sorted(refs) if lo <= v < hi]
        print('  0x%05X-0x%05X  %6d bytes  %d reference%s'
              % (lo, hi, L, len(inside), '' if len(inside) == 1 else 's'))
        for v, rs in inside[:4]:
            a, kind, mn, ops = rs[0]
            print('      <- 0x%05X  %-5s %s %s' % (a, kind, mn, ops))
        if len(inside) > 4:
            print('      ... %d more' % (len(inside) - 4))


if __name__ == '__main__':
    main()
