#!/usr/bin/env python3
"""Align two System 16 program disassemblies (objdump listings) and map
code offsets from a REFERENCE program (whose patch tables are hand-derived)
to a TARGET program (a regional/revision variant of the same game).

    python3 tools/game_align.py altbeast altbeastj [--map out.json] [--stats]

Method: every instruction is reduced to a shape — mnemonic plus operands
with ROM-space addresses (0x100..ROM_TOP) and pc-relative/branch targets
masked — and the two shape streams are aligned with difflib's longest-
matching-block algorithm. Inside a matched block every instruction has
exactly one counterpart, so a reference offset maps to a target offset.
Unmatched instructions (regional code: text, MCU protocol, dips) map to
nothing and the caller must decide.

The map is a DERIVATION AID: every translated site is verified by the
patcher's own expect()/assert on the target bytes, never trusted blind.
"""
import re, sys, json, difflib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ROM_TOP = 0x40000
line_re = re.compile(r'^\s*([0-9a-f]+):\t((?:[0-9a-f]{4} )+)\s*(\S.*)?$')
hex_re = re.compile(r'0x([0-9a-f]+)')
dec_re = re.compile(r'#(\d+)\b')

def load(game):
    insns = []
    for line in (ROOT / 'roms' / game / 'prog68k.asm').read_text().splitlines():
        m = line_re.match(line)
        if not m or not m.group(3):
            continue
        addr = int(m.group(1), 16)
        words = m.group(2).split()
        text = m.group(3).strip()
        insns.append((addr, words, text))
    return insns

def shape(text):
    mn = text.split()[0]
    ops = text[len(mn):].strip()
    def hx(m):
        v = int(m.group(1), 16)
        return 'ROM' if 0x100 <= v < ROM_TOP else m.group(0)
    ops = hex_re.sub(hx, ops)
    def dc(m):
        v = int(m.group(1))
        return '#ROM' if 0x100 <= v < ROM_TOP else m.group(0)
    ops = dec_re.sub(dc, ops)
    ops = re.sub(r'%pc@\([^)]*\)', 'PC', ops)
    # branches: bcc/bsr/dbf targets are already ROM-masked; strip trailing labels
    return mn + ' ' + ops

def align(ref, tgt):
    rs = [shape(t) for _, _, t in ref]
    ts = [shape(t) for _, _, t in tgt]
    sm = difflib.SequenceMatcher(None, rs, ts, autojunk=False)
    blocks = sm.get_matching_blocks()
    amap = {}
    for i, j, n in blocks:
        for k in range(n):
            amap[ref[i + k][0]] = tgt[j + k][0]
    matched = sum(n for _, _, n in blocks)
    return amap, matched, blocks

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    ref_g, tgt_g = args[0], args[1]
    ref, tgt = load(ref_g), load(tgt_g)
    amap, matched, blocks = align(ref, tgt)
    print(f"{ref_g}: {len(ref)} insns, {tgt_g}: {len(tgt)} insns, "
          f"matched {matched} ({100*matched/len(ref):.1f}% of ref) in "
          f"{sum(1 for b in blocks if b[2])} blocks")
    if '--stats' in sys.argv:
        big = sorted((n, i, j) for i, j, n in blocks if n)
        print("largest blocks (n, ref@, tgt@, shift):")
        for n, i, j in big[-12:]:
            print(f"  {n:6d}  {ref[i][0]:06x} -> {tgt[j][0]:06x}  "
                  f"{tgt[j][0]-ref[i][0]:+#x}")
        # unmatched ref runs
        runs = []
        prev_end = 0
        for i, j, n in blocks:
            if i > prev_end:
                runs.append((ref[prev_end][0], ref[i-1][0], i - prev_end))
            prev_end = i + n
        runs.sort(key=lambda r: -r[2])
        print("largest UNMATCHED ref runs (from, to, insns):")
        for a, b, n in runs[:12]:
            print(f"  {a:06x}-{b:06x} {n}")
    if '--map' in sys.argv:
        out = sys.argv[sys.argv.index('--map') + 1]
        Path(out).write_text(json.dumps({f"{k:x}": f"{v:x}" for k, v in amap.items()}))
        print("map ->", out)

if __name__ == '__main__':
    main()
