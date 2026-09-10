#!/usr/bin/env python3
"""STATIC SDRAM extent map + overlap check.

    tools/sdram_map.py            # the map, with overlaps flagged
    tools/sdram_map.py --quiet    # exit 1 if any CONFIRMED overlap

The companion to `tools/sdram_audit.py`, which finds LIVE spans by
dump-diff and says of itself: "the tool cannot know extents". That blind
spot is the whole problem. Collision #16 (2026-09-10) was `SLC[16..21]`
at 0x3A7C0 landing on `SPRPEN`, both blocks legitimately live, both
written every frame -- a liveness diff cannot see that, only declared
extents can.

This parses every fixed 32X SDRAM address declared in sh_src/ (EVERY
#ifdef branch: the grep that skips the inactive-looking branch is how
collision #13 happened), recovers each block's SIZE from its own
declaration comment, and reports the map sorted by address with any
overlap marked.

Sizes come from the comment forms this codebase actually uses:
    [3][16]  [128][8]  16 words  896B  32 x 4 words  512B
A declaration whose size cannot be recovered is listed as UNKNOWN and
excluded from overlap checking -- it is a gap in the check, not a pass,
and the count of them is printed so it cannot be ignored.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "sh_src")
# 0x060xxxxx cached / 0x260xxxxx uncached are the SAME memory: normalise.
ADDR = re.compile(r'0x[02]60([0-9A-Fa-f]{5})')
DEFINE = re.compile(r'#\s*define\s+(\w+)\s')

WORD = {"uint8_t": 1, "int8_t": 1, "char": 1,
        "uint16_t": 2, "int16_t": 2, "short": 2,
        "uint32_t": 4, "int32_t": 4, "long": 4, "unsigned": 4}


def parse_size(line, tail):
    """Recover a block's byte extent from its declaration + comment."""
    unit = 1
    for t, w in WORD.items():
        if t in line:
            unit = w
            break
    text = line + " " + tail
    # explicit byte counts: "896B", "512 B", "ends 0x3A800"
    m = re.search(r'ends\s+0x[02]?60?([0-9A-Fa-f]{5})', text)
    if m:
        return ("end", int(m.group(1), 16))
    m = re.search(r'\b(\d+)\s*B\b', text)
    if m:
        return ("size", int(m.group(1)))
    # "32 x 4 words", "16 words", "[3][16]", "[128][8]"
    m = re.search(r'\b(\d+)\s*x\s*(\d+)\s*words?', text)
    if m:
        return ("size", int(m.group(1)) * int(m.group(2)) * 2)
    m = re.search(r'\b(\d+)\s*words?', text)
    if m:
        return ("size", int(m.group(1)) * 2)
    dims = re.findall(r'\[\s*(\d+)\s*\]', text)
    if dims:
        n = 1
        for d in dims:
            n *= int(d)
        return ("size", n * unit)
    return (None, None)


def main():
    quiet = "--quiet" in sys.argv
    blocks = []
    for fn in sorted(os.listdir(SRC)):
        if not fn.endswith((".c", ".h")):
            continue
        path = os.path.join(SRC, fn)
        lines = open(path, errors="replace").read().split("\n")
        for i, line in enumerate(lines):
            if "#define" not in line:
                continue
            m = ADDR.search(line)
            if not m:
                continue
            name = DEFINE.search(line)
            if not name:
                continue
            addr = int(m.group(1), 16)
            tail = " ".join(lines[i + 1:i + 3])
            kind, val = parse_size(line, tail)
            if kind == "end":
                size = max(0, val - addr)
            elif kind == "size":
                size = val
            else:
                size = None
            blocks.append({"name": name.group(1), "addr": addr, "size": size,
                           "where": "%s:%d" % (fn, i + 1)})

    blocks.sort(key=lambda b: (b["addr"], b["name"]))
    known = [b for b in blocks if b["size"]]
    unknown = [b for b in blocks if not b["size"]]

    overlaps = []
    for i, a in enumerate(known):
        for b in known[i + 1:]:
            if b["addr"] >= a["addr"] + a["size"]:
                break
            if a["name"] == b["name"]:
                continue          # same block redeclared under #ifdef
            overlaps.append((a, b))

    if not quiet:
        print("STATIC SDRAM EXTENT MAP (32X SDRAM, 0x06000000 base)\n")
        print("  %-26s %-9s %-8s %s" % ("name", "addr", "size", "declared"))
        for b in blocks:
            sz = "%d" % b["size"] if b["size"] else "UNKNOWN"
            end = " -> %05X" % (b["addr"] + b["size"]) if b["size"] else ""
            print("  %-26s %05X     %-8s %s%s"
                  % (b["name"], b["addr"], sz, b["where"], end))
        print("\n  %d blocks, %d with a recoverable extent, %d UNKNOWN"
              % (len(blocks), len(known), len(unknown)))
        if unknown:
            print("  UNKNOWN blocks are NOT overlap-checked -- that is a hole "
                  "in this check, not a pass:")
            for b in unknown:
                print("    %-26s %05X  %s" % (b["name"], b["addr"], b["where"]))

    if overlaps:
        print("\n  OVERLAPS (%d):" % len(overlaps))
        for a, b in overlaps:
            print("    %s [%05X+%d, %s]" % (a["name"], a["addr"], a["size"], a["where"]))
            print("      collides with %s [%05X, %s]  by %d bytes"
                  % (b["name"], b["addr"], b["where"],
                     a["addr"] + a["size"] - b["addr"]))
        return 1
    print("\n  no overlap among blocks with a recoverable extent.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
