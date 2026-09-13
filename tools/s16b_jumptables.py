#!/usr/bin/env python3
"""Bound every jump table of absolute code pointers in a System 16 program,
two independent ways, and report where they disagree.

    python3 tools/s16b_jumptables.py goldnaxe [--half 0x40000]

Idiom: `lea pc(tbl),aN ... movea.l (aN,dM.w),aN` (or jsr/jmp through it).
A table is longs pointing at instruction starts. Two bounds per table:
  run   : consecutive longs that are even, < code_limit, and land on an
          instruction boundary of the objdump listing (the 'read until
          implausible' filter — AB entry 17 showed it can merge two tables)
  next  : longs up to the next known code start or the next table start,
          whichever comes first (a structural bound)
Also reports the index bound when the consumer's index is `moveb x,d0 ;
addw d0,d0 ; addw d0,d0` preceded by `andi.w #N` / `cmpi.w #N` — the
CONSUMER's own limit (entry 50: the consuming instruction decides).
"""
import re, sys, struct, bisect
game = sys.argv[1]; half = int(sys.argv[sys.argv.index('--half')+1], 16) if '--half' in sys.argv else 0x40000
img = open(f'roms/{game}/prog68k.bin','rb').read()
lines = []
for line in open(f'roms/{game}/prog68k.asm', errors='ignore'):
    m = re.match(r'\s*([0-9a-f]+):\t([0-9a-f ]+)\t(\S+)\s*(.*)$', line.rstrip())
    if m: lines.append((int(m.group(1),16), m.group(3), m.group(4).strip()))
pcs = [l[0] for l in lines]; pcset = set(pcs)
tables = {}
for i,(pc,mn,ops) in enumerate(lines):
    if pc >= half: continue
    if mn == 'moveal' and re.match(r'%a[0-6]@\(0,%d[0-7]:w\)', ops):
        for j in range(i-1, max(i-6,0), -1):
            lm = re.match(r'%pc@\(0x([0-9a-f]+)\)', lines[j][2])
            if lines[j][1]=='lea' and lm:
                t = int(lm.group(1),16); tables.setdefault(t, []).append(pc); break
starts = sorted(tables)
def is_rom_ptr(v): return v % 2 == 0 and 0x400 <= v < len(img)
def is_code_ptr(v): return is_rom_ptr(v) and v < half and v in pcset
def run_of(t, pred):
    n = 0
    while t + n*4 + 4 <= len(img) and pred(struct.unpack('>I', img[t+n*4:t+n*4+4])[0]): n += 1
    return n
out = []
for t in starts:
    run_rom, run_code = run_of(t, is_rom_ptr), run_of(t, is_code_ptr)
    nxt_tbl = next((s for s in starts if s > t), None)
    # next instruction the listing shows that is NOT part of a pointer run (a real code start): first pc > t whose
    # line is a branch/jsr/rts/move — approximated as the first listing pc > t that no table's pointer run covers
    nbound = ((nxt_tbl - t) // 4) if nxt_tbl else run_rom
    idx = None
    for pc in tables[t]:
        k = pcs.index(pc)
        for j in range(k-1, max(k-8,0), -1):
            m2 = re.match(r'#(-?\d+),%d[0-7]', lines[j][2])
            if lines[j][1] in ('andiw','cmpiw','cmpib','andib') and m2: idx = int(m2.group(1)); break
    # verdict: consumer bound wins; else agreement of ROM-run and next-table bound; else unbounded
    # verdict: the consumer's own bound wins (entry 50); otherwise, if the
    # pointer run reaches the next table start the two tables are ADJACENT
    # and the next start is the bound (AB entry 17: a run merges adjacent
    # tables); otherwise the run ended on a non-pointer long and the run
    # is the bound (a filter — HYPOTHESIS until a consumer confirms it).
    if idx is not None: count, how = idx + 1, 'idx'
    elif run_rom >= nbound: count, how = nbound, 'adjacent'
    else: count, how = run_rom, 'run'
    out.append((t, run_rom, run_code, nbound, idx, count, how, tables[t]))
print(f"{len(out)} tables; run_rom = longs that are even ROM addresses, run_code = of those, on an instruction boundary,")
print(f"next = longs to the next table start, idx = the consumer's andi/cmpi immediate (count = idx+1); verdict = count and rule")
print(f"{'table':>7} {'rom':>4} {'code':>4} {'next':>5} {'idx':>4} {'count':>5}  rule       consumers")
kinds = {}
for t, rr, rc, nb, idx, count, how, cons in out:
    kinds[how] = kinds.get(how, 0) + 1
    print(f"{t:07x} {rr:4d} {rc:4d} {nb:5d} {str(idx) if idx is not None else '-':>4} {str(count) if count else '-':>5}  {how:9}  {' '.join(f'{c:x}' for c in cons[:3])}")
print('verdicts:', kinds)
print('TABLE for game_<name>.py REBASE_TABLES: (start, count, rule)')
print('   [' + ', '.join(f"(0x{t:X}, {count}, '{how}')" for t, rr, rc, nb, idx, count, how, cons in out) + ']')
