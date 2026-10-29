#!/usr/bin/env python3
"""Sound-post census for a System 16 title (generic form of sound_posts.py,
which read AB's code_stream/function_map artifacts).

    python3 tools/s16b_sound_posts.py goldnaxe 0x3616 [--md docs/audit/goldnaxe/sound_posts.md]

Walks every bsr/jsr/bra/jmp to the post entry in roms/<game>/prog68k.asm,
recovers the command byte from the d0 load immediately before the call
(moveq / move.b #imm / move.w #imm; anything else is 'computed'), names
the enclosing Ghidra function from docs/audit/<game>/timing_census.json,
and writes a Markdown table sorted by command. The sound thread joins the
class (music / sfx / speech) from the Z80 driver later.
"""
import re, sys, json, bisect, collections, argparse
ap = argparse.ArgumentParser(); ap.add_argument('game'); ap.add_argument('entry'); ap.add_argument('--md'); ap.add_argument('--half', default='0x40000')
a = ap.parse_args(); entry = int(a.entry, 16); half = int(a.half, 16)
lines = []
for line in open(f'roms/{a.game}/prog68k.asm', errors='ignore'):
    m = re.match(r'\s*([0-9a-f]+):\t([0-9a-f ]+)\t(\S+)\s*(.*)$', line.rstrip())
    if m: lines.append((int(m.group(1),16), m.group(3), m.group(4).strip()))
try:
    fn = json.load(open(f'docs/audit/{a.game}/timing_census.json'))['functions']
    ents = sorted((int(f['entry'],16), int(f.get('size',0)), f.get('name','')) for f in fn); starts = [e[0] for e in ents]
except FileNotFoundError: ents, starts = [], []
def func(pc):
    i = bisect.bisect_right(starts, pc) - 1
    return ents[i][2] if i >= 0 and pc < ents[i][0] + max(ents[i][1], 2) else '-'
rows = []
for i, (pc, mn, ops) in enumerate(lines):
    if mn not in ('bsrs','bsrw','jsr','braw','bras','jmp'): continue
    m = re.fullmatch(r'0x([0-9a-f]+)', ops)
    if not m or int(m.group(1),16) != entry: continue
    cmd, how = None, 'computed'
    for j in range(i-1, max(i-4, 0), -1):
        pmn, pops = lines[j][1], lines[j][2]
        mm = re.fullmatch(r'#(-?\d+),%d0', pops)
        if pmn in ('moveq','moveb','movew') and mm: cmd, how = int(mm.group(1)) & 0xFF, pmn; break
        if '%d0' in pops or pmn.startswith(('bsr','jsr','rts')): break
    rows.append((pc, cmd, how, mn, func(pc), pc >= half))
by = collections.Counter(r[1] for r in rows if r[1] is not None)
out = [f"# Sound posts — {a.game}\n", f"Entry 0x{entry:X}; {len(rows)} call sites ({sum(1 for r in rows if r[5])} in the second half), {sum(1 for r in rows if r[1] is None)} with a computed command.\n",
       "Convention (LOOP-DECOMPILE-GOLDNAXE 6, 11): d0.b = command; 0 goes straight to the latch (0x3674, stop-all); others are\n"
       "de-duplicated against the 32-byte ring 0xFFEC40-5F (count 0xFFEC3C, read pointer 0xFFEC3E) and pushed; IRQ4 pops one per\n"
       "vint into the mailbox 0xFFECFC (0x3314); the MCU forwards non-0xFF to the Z80 latch and rewrites 0xFF. Command 0x9C is\n"
       "dropped unless 0xFFEC26 bit 0 and 0xFFEC1B bit 1 are set (0x361C-0x3632; HYPOTHESIS: the demo-sounds gate).\n",
       "\n## Commands by frequency of call sites\n", "| cmd | sites |\n|---|---|"]
out += [f"| 0x{c:02X} | {n} |" for c, n in by.most_common()]
out += ["\n## Every call site\n", "| site | cmd | via | call | function | half |\n|---|---|---|---|---|---|"]
for pc, cmd, how, mn, fname, dup in sorted(rows, key=lambda r: (r[1] if r[1] is not None else 0x1FF, r[0])):
    out.append(f"| 0x{pc:05X} | {'0x%02X' % cmd if cmd is not None else 'computed'} | {how} | {mn} | {fname} | {'2nd' if dup else ''} |")
md = '\n'.join(out) + '\n'
if a.md: open(a.md, 'w').write(md); print(f"wrote {a.md}: {len(rows)} sites, {len(by)} distinct commands")
else: print(md)
