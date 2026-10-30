#!/usr/bin/env python3
"""Per-function diff of a regional variant against the reference program, with
every masked operand CHECKED instead of trusted.

    python3 tools/region_diff.py altbeast altbeastj [--map out.tsv] [--all]

tools/game_align.py aligns the two listings on instruction SHAPE, masking any
value in 0x100..ROM_TOP as an address. That mask is also a blind spot: a timer
constant #600 changed to #640 aligns as "same". Here each masked pair (ref value
u, target value t) must satisfy t == map(u), where map is the instruction
alignment for code and a byte-anchor map (unique 16-byte windows, the local
shift) for data. A pair that fails is a real change.

Functions come from docs/audit/func_ranges.txt (the reference's real
boundaries). Classes:
  SAME      every instruction aligned, every masked operand maps
  CHANGED   unaligned instructions on either side, or an operand that does not map
--map writes the reference->target address map (code and data anchors) as TSV,
the input the hand-file translation reads.
"""
import sys, re, bisect
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import game_align as GA

ROOT = GA.ROOT
num_re = re.compile(r'0x([0-9a-f]+)|#(-?\d+)\b')
WIN = 16

def func_ranges():
    fr = {}
    for l in (ROOT / 'docs/audit/func_ranges.txt').read_text().splitlines():
        if l.startswith('#') or not l.strip():
            continue
        s, e, _ent, name = l.split()[:4]
        fr.setdefault(name, []).append((int(s, 16), int(e, 16)))
    return fr

def anchors(ref_bin, tgt_bin):
    """(ref_off, delta) at every 2-aligned ref offset whose 16-byte window is
    unique in both programs: the local shift, data and code alike."""
    def uniq(b):
        seen = {}
        for o in range(0, len(b) - WIN, 2):
            w = b[o:o + WIN]
            seen[w] = -1 if w in seen else o
        return seen
    ru, tu = uniq(ref_bin), uniq(tgt_bin)
    out = []
    for w, o in ru.items():
        if o >= 0 and len(set(w)) > 2 and tu.get(w, -1) >= 0:
            out.append((o, tu[w] - o))
    out.sort()
    return out

def operands(text):
    vals = []
    for m in num_re.finditer(text):
        vals.append(int(m.group(1), 16) if m.group(1) else int(m.group(2)))
    return vals

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    ref_g, tgt_g = args[0], args[1]
    ref, tgt = GA.load(ref_g), GA.load(tgt_g)
    rb = (ROOT / 'roms' / ref_g / 'prog68k.bin').read_bytes()
    tb = (ROOT / 'roms' / tgt_g / 'prog68k.bin').read_bytes()
    amap, matched, _ = GA.align(ref, tgt)
    anc = anchors(rb, tb)
    akeys = [a for a, _ in anc]

    def maps(u, t):
        if amap.get(u) == t:
            return True
        k = bisect.bisect_right(akeys, u) - 1
        # the nearest anchor on either side; a shift changes only at an edit
        return any(0 <= kk < len(anc) and u + anc[kk][1] == t for kk in (k, k + 1))

    tidx = {a: i for i, (a, _, _) in enumerate(tgt)}
    rtext = {a: t for a, _, t in ref}
    ttext = {a: t for a, _, t in tgt}
    raddrs = [a for a, _, _ in ref]
    report = []
    for name, rs in func_ranges().items():
        size = sum(e - s + 1 for s, e in rs)
        unal, bad, tins = [], [], []
        for s, e in rs:
            i = bisect.bisect_left(raddrs, s)
            mapped = []
            while i < len(raddrs) and raddrs[i] <= e:
                u = raddrs[i]
                t = amap.get(u)
                if t is None:
                    unal.append(u)
                else:
                    mapped.append(t)
                    for a, b in zip(operands(rtext[u]), operands(ttext[t])):
                        if a != b and GA.ROM_TOP > a >= 0x100 and not maps(a, b):
                            bad.append((u, rtext[u], ttext[t]))
                            break
                i += 1
            # target-only instructions inside the mapped span (insertions)
            if mapped:
                inv = set(amap.values())
                for j in range(tidx[min(mapped)], tidx[max(mapped)] + 1):
                    if tgt[j][0] not in inv:
                        tins.append(tgt[j][0])
        cls = 'SAME' if not (unal or bad or tins) else 'CHANGED'
        report.append((name, rs[0][0], size, cls, unal, bad, tins))

    same = [r for r in report if r[3] == 'SAME']
    chg = [r for r in report if r[3] == 'CHANGED']
    print(f"{ref_g} -> {tgt_g}: {len(report)} functions; "
          f"SAME {len(same)} ({sum(r[2] for r in same)} B), "
          f"CHANGED {len(chg)} ({sum(r[2] for r in chg)} B); "
          f"{len(anc)} data anchors")
    chg.sort(key=lambda r: -(len(r[4]) + len(r[5]) + len(r[6])))
    for name, s, size, _, unal, bad, tins in chg:
        print(f"  {name:28s} {s:05X} {size:5d} B  ref-only {len(unal):3d}  "
              f"target-only {len(tins):3d}  operand {len(bad):3d}")
        if '--all' in sys.argv:
            for u in unal:
                print(f"      - {u:05x}  {rtext[u]}")
            for t in tins:
                print(f"      + {t:05x}  {ttext[t]}")
            for u, a, b in bad:
                print(f"      ~ {u:05x}  {a:40s} | {b}")
    if '--map' in sys.argv:
        out = Path(sys.argv[sys.argv.index('--map') + 1])
        lines = [f"code\t{k:06x}\t{v:06x}" for k, v in sorted(amap.items())]
        lines += [f"data\t{k:06x}\t{d:+d}" for k, d in anc]
        out.write_text('\n'.join(lines) + '\n')
        print('map ->', out)

if __name__ == '__main__':
    main()
