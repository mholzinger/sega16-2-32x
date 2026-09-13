#!/usr/bin/env python3
"""FM-gate SPANS and ENTRIES for a System 16 title, derived from the
framebuffer-destined writer sites (generic version of LOOP 23's
fmgate_derive.py, which carried AB's censuses as literals).

    python3 tools/s16b_fmgate_spans.py goldnaxe

Inputs: tools/game_<game>.py TILE_DIRTY_SITES / STRIP_BLITTERS /
TXT_LOOP_HEADS / TXT_WRAM_WRITERS / LAYER_REG_SITES (static sites) and
docs/audit/<game>/write_census.txt (executed writers; its PCs are the
instruction AFTER the store). Method: every FB writer is wrapped in its
rts-bounded region (previous rts/rte exclusive .. next rts/rte
inclusive), regions merge into SPANS; every branch/jsr/jmp whose target
lies inside a span and whose source lies outside is an ENTRY. Writers
inside the vblank handler are VINT-context and reported separately
(AB: ungated, the trampoline guarantees FM=0 for the game vint).
Output: the tables and the entry list; nothing is written to the game
file — the builder decides which regions are FB-destined in the port.
"""
import re, sys, bisect, importlib.util as ilu
game = sys.argv[1]
spec = ilu.spec_from_file_location('g', f'tools/game_{game}.py'); g = ilu.module_from_spec(spec); spec.loader.exec_module(g); T = g.TABLES
lines = []
for line in open(f'roms/{game}/prog68k.asm', errors='ignore'):
    m = re.match(r'\s*([0-9a-f]+):\t([0-9a-f ]+)\t(\S+)\s*(.*)$', line.rstrip())
    if m: lines.append((int(m.group(1),16), m.group(3), m.group(4).strip()))
pcs = [l[0] for l in lines]
IRQ4 = (0x2F60, 0x3172)   # goldnaxe: entry 6
sites = set()
for e in T.get('TILE_DIRTY_SITES') or []: sites.add(e[0])
for e in T.get('STRIP_BLITTERS') or []: sites.add(e[0])
for e in T.get('TILE_PTR_USE_SITES') or []: sites.add(e[0])
for e in (T.get('TXT_LOOP_HEADS') or {}).values(): sites.add(e)
for w in T.get('TXT_WRAM_WRITERS') or []: sites.add(w['site']); [sites.add(a) for a in w.get('alt_sites', [])]; sites.add(w.get('helper', w['site']))
# executed writers from the census, tile + text regions, minus one instruction
census = {}
try:
    reg = None
    for line in open(f'docs/audit/{game}/write_census.txt'):
        m = re.match(r'== (\w+): writer PCs', line)
        if m: reg = m.group(1); continue
        m = re.match(r'\s+([0-9a-f]{6})\s+(\d+)\s+([0-9a-f]{6})-([0-9a-f]{6})', line)
        if m and reg in ('tileram', 'textram'):
            nxt = int(m.group(1),16); i = bisect.bisect_left(pcs, nxt) - 1
            if i >= 0: census[pcs[i]] = (reg, int(m.group(2)))
except FileNotFoundError: pass
vint = {p: v for p, v in census.items() if IRQ4[0] <= p <= IRQ4[1]}
main = {p: v for p, v in census.items() if p not in vint}
allw = sorted(sites | set(main))
def region(a):
    i = bisect.bisect_left(pcs, a)
    lo = i
    while lo > 0 and lines[lo-1][1] not in ('rts', 'rte'): lo -= 1
    hi = i
    while hi < len(lines)-1 and lines[hi][1] not in ('rts', 'rte'): hi += 1
    return (pcs[lo], pcs[hi])
spans = []
for a in allw:
    s, e = region(a)
    if spans and s <= spans[-1][1] + 2: spans[-1] = (spans[-1][0], max(spans[-1][1], e))
    else: spans.append((s, e))
def span_of(a):
    for i, (s, e) in enumerate(spans):
        if s <= a <= e: return i
    return None
entries = {}
pat = re.compile(r'\b0x([0-9a-f]{2,6})\b')
for pc, mn, ops in lines:
    if mn.startswith(('bchg','bclr','bset','btst')) or not mn.startswith(('b','db','jsr','jmp')): continue
    for m in pat.finditer(ops):
        tgt = int(m.group(1),16); ri = span_of(tgt)
        if ri is not None and span_of(pc) != ri: entries.setdefault(ri, set()).add(tgt)
print(f"{len(allw)} MAIN-context writer sites ({len(sites)} static, {len(main)} executed in the census), {len(vint)} VINT-context census writers: {' '.join(f'{p:x}' for p in sorted(vint))}")
print(f"{len(spans)} spans:")
for i, (s, e) in enumerate(spans):
    ins = [a for a in allw if s <= a <= e]
    print(f"  span {i:2d} {s:06x}..{e:06x} ({e-s+2:5d} B)  writers {' '.join(f'{a:x}' for a in ins[:6])}{' ...' if len(ins)>6 else ''}")
    print(f"           entries {' '.join(f'{t:x}' for t in sorted(entries.get(i, [])))}")
