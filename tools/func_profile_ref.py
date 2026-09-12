#!/usr/bin/env python3
"""Profile every function without Ghidra, and fix the class that was wrong.

    python3 tools/func_profile_ref.py > docs/audit/function_map2.md

`tools/ghidra/classify.py` labels a function `hardware` when
`func_profile.py` reports any REGION for it — but that profiler's region
list includes `workram` and `objtable` (func_profile.py:32-35), so the
label means "touches memory", not "touches the board". 117 functions carry
it and **45 of them touch an arcade hardware address**. The other 72 touch
work RAM. That is a plausibility label, not a derived one, and it is the
biggest non-leaf class in the map (LOOP-DECOMPILE 78).

This recomputes the profile from `tools/code_stream.py` and the function
bounds, with the arcade surface kept separate from work RAM, and prints
the same table with an honest class column. Bounds come from
`docs/audit/function_map.md` corrected by `docs/audit/bound_repairs.md`.
"""
import re, sys, collections

MAP = 'docs/audit/function_map.md'
STREAM = 'docs/audit/code_stream.txt'

EXTEND = {0x0040E: 0x0057A, 0x05FA8: 0x0602C, 0x060E6: 0x06168,
          0x063CC: 0x0644E, 0x06C44: 0x06CB8, 0x18146: 0x181E4}
DROP = {0x06E7A, 0x08532, 0x0DE56, 0x18F38, 0x1A0B8}

HW = [(0x3F0000, 0x400000, 'tbank'), (0x400000, 0x410000, 'vram'),
      (0x410000, 0x440000, 'text'),  (0x440000, 0x480000, 'objram'),
      (0x840000, 0x850000, 'pal'),   (0xC40000, 0xC50000, 'io')]

ABS = re.compile(r'0x([0-9a-f]{4,8})')
FLD = re.compile(r'%(?:fp|a6)@\((-?\d+)\)')
CALL = re.compile(r'^(?:0x)?([0-9a-f]+)$')
ROW = re.compile(r'\|\s*0x([0-9A-F]+)\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|'
                 r'\s*([^|]+)\|([^|]*)\|\s*([^|]+)\|')


def hw_of(v):
    for lo, hi, n in HW:
        if lo <= v < hi:
            return n
    return None


# Read to their return this session (LOOP-DECOMPILE 78). Same standing as
# the map's other READ rows: a name here means someone followed the code.
NAMES = {
    0x0144A: 'credit_prompt_select',
    0x03AA4: 'textram_clear_run',
    0x03AAE: 'draw_credits_line',
    0x01366: 'read_controls_or_demo',
}


def main():
    funcs = []
    for ln in open(MAP):
        m = ROW.match(ln)
        if m:
            a = int(m.group(1), 16)
            if a in DROP:
                continue
            funcs.append(dict(a=a, end=EXTEND.get(a, a + int(m.group(2))),
                              callers=int(m.group(3)),
                              oldcls=m.group(4).strip(),
                              ev=m.group(6).strip()))
    ins = []
    for ln in open(STREAM):
        f = ln.rstrip('\n').split('\t')
        if len(f) >= 4:
            ins.append((int(f[0], 16), f[2], f[3]))
    ins.sort()

    bounds = sorted((f['a'], f['end'], i) for i, f in enumerate(funcs))
    for f in funcs:
        f.update(hw=set(), fields=set(), wram=set(), callees=set())
    j = 0
    for a, mn, ops in ins:
        while j + 1 < len(bounds) and bounds[j + 1][0] <= a:
            j += 1
        lo, hi, idx = bounds[j]
        if not (lo <= a < hi):
            continue
        f = funcs[idx]
        for m in FLD.finditer(ops):
            f['fields'].add(int(m.group(1)))
        isjump = mn.startswith(('b', 'db', 'jsr', 'jmp', 'bsr')) and mn not in (
            'bset', 'bclr', 'bchg', 'btst')
        for m in ABS.finditer(ops):
            v = int(m.group(1), 16)
            if v >= 0x80000000:
                v &= 0xFFFFFFFF
            if isjump:
                if mn.startswith(('jsr', 'bsr')):
                    f['callees'].add(v)
                continue
            r = hw_of(v)
            if r:
                f['hw'].add(r)
            elif 0xFF0000 <= v <= 0xFFFFFF:
                f['wram'].add(v & 0xFFFFFF)

    print('# Altered Beast: function map, rebuilt without Ghidra\n')
    print('`tools/func_profile_ref.py`. Same 555 functions as')
    print('`function_map.md`, with the arcade hardware surface kept SEPARATE')
    print('from work RAM — the old `hardware` class conflated them and was')
    print('wrong for 72 of the 117 rows that carried it '
          '(LOOP-DECOMPILE 78).\n')
    print('| entry | size | callers | class | hw | fields | evidence |')
    print('|---|---|---|---|---|---|---|')
    counts = collections.Counter()
    for f in sorted(funcs, key=lambda x: x['a']):
        if f['a'] in NAMES:
            cls, f['ev'] = NAMES[f['a']], 'READ'
        elif f['ev'] == 'READ':
            cls = f['oldcls']
        elif f['hw']:
            cls = 'arcade hw'
        elif f['fields']:
            cls = f['oldcls'] if f['oldcls'] not in (
                'hardware', 'leaf/helper') else 'object routine'
        elif f['wram']:
            cls = 'work RAM only'
        else:
            cls = 'leaf/helper'
        counts[cls] += 1
        print('| 0x%05X | %d | %d | %s | %s | %s | %s |'
              % (f['a'], f['end'] - f['a'], f['callers'], cls,
                 ' '.join(sorted(f['hw'])) or '-',
                 ' '.join('$%02X' % x for x in sorted(f['fields'])[:6]) or '-',
                 f['ev']))
    print('\n## Class totals\n')
    for k, v in counts.most_common():
        print('- %-24s %d' % (k, v))


if __name__ == '__main__':
    main()
