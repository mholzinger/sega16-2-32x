#!/usr/bin/env python3
"""Place the decompiled corpus's absolute symbols at their TRUE addresses.

    tools/cport_layout.py <outdir> [--rom roms/altbeast/prog68k.bin]

WHY. The decompiled C reaches 68K memory by SYMBOL (`_DAT_fffff014`), and the
linker was placing those symbols wherever it liked. That breaks every access
that crosses one -- `*(undefined2 *)(&DAT_x + 2)` has to land on the NEXT
symbol, and the game does that constantly. Placing them at their real offsets
makes the block contiguous and correct, and it is the same layout the port needs
anyway (FULL-PORT F0(a): 15,974 B budgeted, this corpus spans 16,258 B).

FOUR REGIONS, from the 588 absolute symbols:

    68K WORK RAM   401 syms  0xFFC000-0xFFFF82   -> one contiguous .bss block
    program ROM    101 syms  0x000000-0x0242A0   -> the REAL prog68k.bin, so
                                                    constant reads are correct
    S16 video/IO    27 syms  0x400000-0x4FFFFF   -> zeroed MMIO stubs
    S16 palette     18 syms  0x840000-0x85FFFF   -> zeroed MMIO stubs
    S16 I/O ports   41 syms  0xC40001, 0xC41001. -> zeroed MMIO stubs

Ghidra writes work RAM in the sign-extended short form (0xFFFFF014 for the 68K's
0xFFF014), which is what the 68K's `abs.w` addressing produces. The SH ABI also
prefixes every C symbol with one underscore, so `_DAT_fffff014` in C is
`__DAT_fffff014` to the linker.
"""
import argparse, os, re, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cport_names

ap = argparse.ArgumentParser()
ap.add_argument('outdir')
ap.add_argument('--rom', default='roms/altbeast/prog68k.bin')
ap.add_argument('--wram-base', type=lambda x: int(x, 0), default=0x0C040000)
ap.add_argument('--rom-base',  type=lambda x: int(x, 0), default=0x0C100000)
ap.add_argument('--mmio-base', type=lambda x: int(x, 0), default=0x0C060000)
ap.add_argument('--io-base',   type=lambda x: int(x, 0), default=0x0C060000,
                help='the sparse S16 I/O ports; video rides the renderer shadows')
a = ap.parse_args()
O = a.outdir

syms = []
for l in open(os.path.join(O, 'dat_syms.h')):
    m = re.search(r'\b(_?(?:DAT|[a-z]{1,3}Ram)_?[0-9a-f]{4,8}(?:_\d+)?)\b.*0x([0-9A-F]{8})', l)
    if m:
        syms.append((int(m.group(2), 16), m.group(1)))

# ROM-RESIDENT TABLES ARE PLACED, NEVER FABRICATED.
#
# A PTR_*/switch*/LAB_*/s_* symbol that is not placed here gets DEFINED as a
# 64-byte ZEROED array in defs.c, so the port reads zeros where the 68K reads a
# jump table or a character table. First found 2026-09-28: 208 of one frame's
# indirect calls resolved to address 0.
#
# THE MATCH IS BY ADDRESS, NOT BY NAME (tools/cport_names.py has the measurement).
# Exact string equality leaked twice -- `+` is not a C identifier character, and
# Ghidra's listing and decompiler disagree on PTR_DAT_ vs PTR_FUN_ for the same
# address -- which silently dropped six tables, one of them the table the main
# loop's prologue reads to set the mode byte 0xFFFFF142.
#
# So each entry is placed under EVERY spelling at its address, the listing's and
# the decompiler's alike. They name one object; an alias is exact, not a guess.
_sa = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   'docs', 'audit', 'sym_addrs.txt')
_extra, _alias = 0, 0
_byaddr = cport_names.load_sym_addrs(_sa)
_have = {n for _, n in syms}
for av in sorted(_byaddr):
    for nm in _byaddr[av]:
        if nm in _have or not cport_names.is_rom_table(nm):
            continue
        syms.append((av, nm))
        _have.add(nm)
        _extra += 1
# THE DECOMPILER'S OWN SPELLINGS, taken from the generated unit rather than
# predicted: anything it references that looks like a ROM table and carries an
# address in its name is placed at that address. This is what rescues
# PTR_FUN_00006dc0 (the listing calls it PTR_DAT_00006dc0) and
# PTR_LAB_00010202_1_00001cda (the listing spells it with a +).
_unit = os.path.join(O, 'altbeast_port.c')
if os.path.exists(_unit):
    _txt = open(_unit).read()
    for nm in sorted(set(re.findall(r'\b((?:PTR_|switchD?_|LAB_|s_)[A-Za-z0-9_]+)\b', _txt))):
        if nm in _have:
            continue
        av = cport_names.addr_of(nm)
        if av is None or av >= 0x40000:
            continue                  # not a prog68k.bin address; reported by prep
        # LAB_xxxxxxxx_N is Ghidra's reference INTO a label: 0x4D6A `lea 0x6d5c,a1` is
        # spelled &LAB_00006d5a_2, 0x2688's table &LAB_00002686_2. Placed at the label's own
        # address the HUD icon table read one word early (rd47-rd53: row 25 '.#.#.#').
        _m = re.match(r'LAB_[0-9a-f]{8}_(\d+)$', nm)
        if _m:
            av += int(_m.group(1))
        syms.append((av, nm))
        _have.add(nm)
        _alias += 1
print('layout.ld: %d ROM-resident tables from the listing + %d decompiler '
      'spellings placed instead of fabricated' % (_extra, _alias))

WRAM_LO, WRAM_HI = 0xFFC000, 0xFFFFFF
def region(v):
    if 0xFFFF0000 <= v <= 0xFFFFFFFF and WRAM_LO <= (0xFF0000 | (v & 0xFFFF)) <= WRAM_HI:
        return 'wram', 0xFF0000 | (v & 0xFFFF)
    if v < 0x40000:                return 'rom',  v
    if v < 0x1000000:              return 'mmio', v
    # anything above the 68K's 24-bit bus is a mis-parsed or sign-extended
    # symbol; give it its own compact block rather than a 3 GB span.
    return 'odd', v

buckets = {'wram': [], 'rom': [], 'mmio': [], 'odd': []}
for v, n in syms:
    k, addr = region(v)
    buckets[k].append((addr, n))

wl = [x[0] for x in buckets['wram']]
wlo, whi = (min(wl), max(wl)) if wl else (WRAM_LO, WRAM_LO)
ml = [x[0] for x in buckets['mmio']]
mlo, mhi = (min(ml), max(ml)) if ml else (0, 0)

L = ['/* GENERATED by tools/cport_layout.py -- absolute symbol placement.',
     ' * Work RAM becomes one contiguous block so that pointer arithmetic',
     ' * crossing symbols lands where the 68K put it. Program ROM is the REAL',
     ' * image, so constant reads are correct. MMIO is a zeroed stub. */', '']
L.append('_WRAM_BASE = 0x%08X;   /* 68K 0x%06X, %d bytes, %d symbols */'
         % (a.wram_base, wlo, whi - wlo + 1, len(buckets['wram'])))
L.append('_ROM_BASE  = 0x%08X;   /* the real prog68k.bin */' % a.rom_base)
L.append('')
ol = [x[0] for x in buckets['odd']]
olo = min(ol) if ol else 0

# THE S16 VIDEO REGIONS LAND ON THE RENDERER'S OWN SHADOWS. THAT IS THE OUTPUT PATH.
#
# The 32X renderer does not read S16 memory; it reads four SDRAM shadows that the
# patched 68K fills through tools/patch_game.py remap() (tilemap -> FB staging ->
# TILEMAP_U, text -> TEXT_U, palette -> MD RAM mirror -> PAL_SH, sprite table ->
# SPR_SNAP).  The port writes the SAME S16 state, on the SH-2, into SDRAM.  So the
# output path is not a transport and not a transform: it is a LAYOUT DECISION.  Put
# each S16 video region at the address the renderer already reads and the port's
# picture is the renderer's input with zero code and zero cost per frame.
#
# THE ADDRESSES AND THE SIZES ARE PARSED OUT OF sh_src/sdram_map.h, NEVER COPIED.  Each
# region's size is the distance to the NEXT define in that map, so a renderer-side
# move of any one of them moves the port's layout with it.  A hand-copied constant
# here would be a silent wrong answer the first time the SDRAM map is re-carved.
#
# History kept because it cost a day: the first layout compacted every distinct 68K
# page into its own 4 KB slot, on the argument that the program does arithmetic WITHIN
# a page and never between pages.  THAT IS FALSE FOR THE TILEMAP.  0x400000-0x410FFF
# is one contiguous region the game walks, so a write crossing a 4 KB boundary landed
# in whatever page was compacted next -- measured 2026-10-02, tilemap rows
# (`0940 0940 0940 0941 0942 0943`) turned up inside the I/O PORTS page.  Internal
# offsets are preserved exactly; only the sparse I/O ports are compacted.
_MM = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   'sh_src', 'sdram_map.h')
_rmap = {}
for _l in open(_MM):
    _m = re.match(r'#define\s+SDRAM_(TILEMAP_U|TEXT_U|PAL_SH|DIAG|SPR_SNAP|SYNC)\s'
                  r'.*?0x([0-9A-Fa-f]{8})', _l)
    if _m and _m.group(1) not in _rmap:
        _rmap[_m.group(1)] = int(_m.group(2), 16) & 0x0FFFFFFF | 0x06000000
_need = ('TILEMAP_U', 'TEXT_U', 'PAL_SH', 'DIAG', 'SPR_SNAP', 'SYNC')
_miss = [n for n in _need if n not in _rmap]
if _miss:
    raise SystemExit('cport_layout: sh_src/sdram_map.h no longer defines SDRAM_%s -- the '
                     'renderer SDRAM map moved and the port layout cannot be '
                     'derived' % ', '.join(_miss))

# (68K lo, 68K hi, name, renderer shadow base).  hi comes from the shadow's own size.
def _rgn(lo, nm, first, nxt):
    return (lo, lo + (_rmap[nxt] - _rmap[first]), nm, _rmap[first])
VIDREG = (
    _rgn(0x400000, 'tile',    'TILEMAP_U', 'TEXT_U'),    # 13 pages, 0xD000
    _rgn(0x410000, 'text',    'TEXT_U',    'PAL_SH'),    # 0x1000, regs at 0xE80
    _rgn(0x840000, 'palette', 'PAL_SH',    'DIAG'),      # 0x1000, 128 colour sets
    _rgn(0x440000, 'sprite',  'SPR_SNAP',  'SYNC'),      # 0x400 = 64 records x 8 words
)
for _i in range(len(VIDREG)):
    for _j in range(_i + 1, len(VIDREG)):
        _a, _b = VIDREG[_i], VIDREG[_j]
        if _a[3] < _b[3] + (_b[1] - _b[0]) and _b[3] < _a[3] + (_a[1] - _a[0]):
            raise SystemExit('cport_layout: %s and %s overlap in SDRAM (0x%08X+0x%X '
                             'vs 0x%08X+0x%X)' % (_a[2], _b[2], _a[3], _a[1] - _a[0],
                                                  _b[3], _b[1] - _b[0]))

# A symbol PAST a shadow's end is the one failure mode this layout can have, and it
# must never be silent: the tilemap shadow is 13 pages where the 68K's tile RAM is 16,
# and the sprite shadow is the 64 records spr_list_build walks where remap() allows
# 0x800.  patch_game.py already WARNs above tile page 13 for the same reason.
_SHADOW_TAIL = ((0x40D000, 0x410000, 'tile pages 13-15 (TILEMAP_U is 13 pages)'),
                (0x440400, 0x440800, 'sprite records 64+ (SPR_SNAP is 64 records)'))
_past = [(addr, n) for addr, n in buckets['mmio']
         for l, h, _ in _SHADOW_TAIL if l <= addr < h]
if _past:
    raise SystemExit('cport_layout: %d symbol(s) fall past a renderer shadow: %s'
                     % (len(_past), ', '.join('%s @0x%06X' % (n, a)
                                              for a, n in sorted(_past)[:6])))

_vbase = {(l, h): base for l, h, _, base in VIDREG}
_off = 0
_IO_BASE = 0                          # relative to a.io_base below
IOPORTS_A68 = (0xC40001, 0xC41001, 0xC41003, 0xC41005, 0xC41007,
               0xC42001, 0xC42003, 0xC43001, 0xC43007)
_have_mmio = {addr for addr, _ in buckets['mmio']}
for _p in IOPORTS_A68:
    if _p not in _have_mmio:
        buckets['mmio'].append((_p, 'DAT_00%06x' % _p))

# THE SPARSE I/O PAGES GET A 0x100 STRIDE, NOT 0x1000. Every I/O address the program
# touches sits at offset <= 7 inside its 64K page (0xC40001, 0xC41001/3/5/7,
# 0xC42001/3, 0xC43001/7), so a 4 KB slot each wasted 16 KB and pushed the span past
# the port's work RAM at 0x0602CC00. 0x100 keeps every offset distinct with room to
# spare.
_IO_STRIDE = 0x100
_io_pages = sorted({a >> 12 for a, _ in buckets['mmio']
                    if not any(l <= a < h for l, h, _n, _b in VIDREG)})
_io_pi = {p: i for i, p in enumerate(_io_pages)}

def mmio_at(addr):
    for l, h, _n, base in VIDREG:
        if l <= addr < h:
            return base + (addr - l)
    return a.io_base + _io_pi[addr >> 12] * _IO_STRIDE + (addr & 0xFF)

_io_span = len(_io_pages) * _IO_STRIDE
L.append('/* ---- mmio: %d symbols; 4 video regions ON THE RENDERER\'S SHADOWS, '
         '%d sparse I/O page(s) ---- */' % (len(buckets['mmio']), len(_io_pages)))
L.append('_MMIO_BASE = 0x%08X;   /* the sparse I/O block */' % a.io_base)
L.append('_MMIO_BYTES_SYM = 0x%X;   /* the I/O block only; video is per-region */'
         % _io_span)
for _i, (_l, _h, _n, _b) in enumerate(VIDREG):
    L.append('_VID%d_BASE = 0x%08X;  _VID%d_BYTES_SYM = 0x%X;   /* %s, 68K 0x%06X */'
             % (_i, _b, _i, _h - _l, _n, _l))
L.append('_VID_N_SYM = %d;' % len(VIDREG))
_seen_cell = {}
for addr, n in sorted(set(buckets['mmio'])):
    cell = mmio_at(addr)
    if cell in _seen_cell and _seen_cell[cell] != addr:
        raise SystemExit('cport_layout: MMIO 0x%06X and 0x%06X both map to 0x%08X'
                         % (_seen_cell[cell], addr, cell))
    _seen_cell[cell] = addr
    L.append('PROVIDE(_%s = 0x%08X);' % (n, cell))
L.append('')
print('layout.ld: video on the renderer shadows -- %s; I/O 0x%X B at 0x%08X '
      '(%d page(s))'
      % (', '.join('%s 0x%06X->0x%08X %dB' % (n, l, b, h - l)
                   for l, h, n, b in VIDREG), _io_span, a.io_base, len(_io_pages)))

# THE I/O BLOCK AND THE ODD BUCKET MUST END BEFORE WORK RAM. Measured 2026-10-04
# (rd77 transition watch): with --io-base 0x0602C800 the five 0x100 pages ran to
# 0x0602CCFF, so 0xC43001 -- the output port irq4_handler writes 0xFFF094 to every
# vint -- landed on 0x0602CC01 = work RAM 0xFFC001, byte 1 of object slot 0's flags
# (0xFF from boot frame 21; the arcade keeps 0x00; the hero's partner never spawned),
# and the odd bucket at io_base + span sat on slot 2. The Makefile now passes
# 0x0602C700; the odd bucket goes BELOW the block; both are checked here.
if a.io_base + _io_span > a.wram_base:
    raise SystemExit('cport_layout: I/O block 0x%08X..0x%08X overlaps work RAM at 0x%08X'
                     % (a.io_base, a.io_base + _io_span - 1, a.wram_base))
_odd_base = a.io_base - 0x100
for k, base, lo in (('wram', a.wram_base, wlo),
                    ('rom',  a.rom_base,  0),
                    ('odd',  _odd_base, olo)):
    L.append('/* ---- %s: %d symbols ---- */' % (k, len(buckets[k])))
    for addr, n in sorted(set(buckets[k])):
        L.append('PROVIDE(_%s = 0x%08X);' % (n, base + addr - lo))
    L.append('')
open(os.path.join(O, 'layout.ld'), 'w').write('\n'.join(L) + '\n')
print('layout.ld: wram %d syms (68K 0x%06X-0x%06X = %d B), rom %d, mmio %d'
      % (len(buckets['wram']), wlo, whi, whi - wlo + 1,
         len(buckets['rom']), len(buckets['mmio'])))

# the real program ROM as a linkable blob, so ROM symbol reads are CORRECT
oc = os.path.expanduser('~/src/marsdev/mars/sh-elf/bin/sh-elf-objcopy')
subprocess.run([oc, '-I', 'binary', '-O', 'elf32-sh', '-B', 'sh2',
                '--rename-section', '.data=.romimg,alloc,load,readonly,data',
                a.rom, os.path.join(O, 'romimg.o')], check=True)
print('romimg.o: %s (%d bytes) as .romimg' % (a.rom, os.path.getsize(a.rom)))
