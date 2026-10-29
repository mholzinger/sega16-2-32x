#!/usr/bin/env python3
"""STATIC SDRAM extent map + overlap check.

    tools/sdram_map.py            # the map, with overlaps flagged
    tools/sdram_map.py --quiet    # exit 1 if any CONFIRMED overlap

The companion to `tools/sdram_audit.py`, which finds LIVE spans by
dump-diff and says of itself: "the tool cannot know extents". That blind
spot is the whole problem: `SLC[16..21]` runs from 0x3A780 onto
`SPRPEN` at 0x3A7C0, and `SLC`/`SPRLATE`/`FBP` all declare the same
base. A liveness diff cannot see a declared-extent overrun; only
declared extents can. (Whether both blocks are ever live TOGETHER is a
separate question -- SPR_LATE_DIAG has no dedicated Makefile flag, so
do not assume it: LOOP29 128.)

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
    """Recover a block's extent from its OWN declaration line only.

    Comment prose two lines down is not evidence: the first cut of this
    tool parsed neighbouring sentences and announced pri_lut as 256 KB.
    Same-line, unambiguous forms only; everything else is UNKNOWN, and
    UNKNOWN is reported rather than guessed.
    """
    del tail                       # deliberately not consulted
    unit = 1
    for t, w in WORD.items():
        if t in line:
            unit = w
            break
    cm = line.split("/*", 1)[1] if "/*" in line else ""
    m = re.search(r'\b(\d+)\s*x\s*(\d+)\s*words?\b', cm)
    if m:
        return ("size", int(m.group(1)) * int(m.group(2)) * 2)
    m = re.search(r'\b(\d+)\s*words?\b', cm)
    if m:
        return ("size", int(m.group(1)) * 2)
    m = re.search(r'\b(\d+)\s*B\b', cm)
    if m:
        return ("size", int(m.group(1)))
    dims = re.findall(r'\[\s*(\d+)\s*\]', cm)
    if dims:
        n = 1
        for d in dims:
            n *= int(d)
        return ("size", n * unit)
    return (None, None)


# Aliases that are DELIBERATE. A cached (0x060xxxxx) and uncached
# (0x260xxxxx) view of one block is the same memory on purpose, and some
# names are #ifdef alternatives that can never both be live. Everything
# NOT listed here is reported as a genuine collision.
INTENTIONAL = {
    ("TILEMAP_C", "TILEMAP_U"),        # cached / uncached view
    ("TEXT_C", "TEXT_U"),              # cached / uncached view
    ("spr_pair", "spr_pair_rd"),       # write / uncached read view
    ("MDSPR_SAT", "ROWHASH"),          # guarded by an #error in m_main.c
    ("FBCLEAR", "MDSPR_SAT"),          # DIRECT_FB vs canonical, exclusive
}


def is_intentional(a, b):
    return (a, b) in INTENTIONAL or (b, a) in INTENTIONAL


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
    for i, a in enumerate(blocks):
        if not a["size"]:
            continue
        nxt = [x for x in blocks[i + 1:] if x["addr"] > a["addr"]]
        if not nxt:
            continue
        b = nxt[0]
        if a["addr"] + a["size"] > b["addr"] and a["name"] != b["name"]:
            overlaps.append((a, b))
    # exact aliases: two different names at the same address
    for i, a in enumerate(blocks):
        for b in blocks[i + 1:]:
            if b["addr"] != a["addr"]:
                break
            if a["name"] != b["name"]:
                overlaps.append((a, b))

    if not quiet:
        print("STATIC SDRAM EXTENT MAP (32X SDRAM, 0x06000000 base)\n")
        print("  %-26s %-9s %-8s %s" % ("name", "addr", "size", "declared"))
        for i, b in enumerate(blocks):
            nxt = next((x["addr"] for x in blocks[i + 1:]
                        if x["addr"] > b["addr"]), None)
            gap = (nxt - b["addr"]) if nxt else None
            sz = "%d" % b["size"] if b["size"] else "?"
            tight = ""
            if b["size"] and gap is not None and b["size"] > gap:
                tight = "  <-- NEEDS %d, ONLY %d TO NEXT" % (b["size"], gap)
            print("  %-24s %05X  size %-6s gap %-6s %s%s"
                  % (b["name"], b["addr"], sz,
                     str(gap) if gap is not None else "-", b["where"], tight))
        print("\n  %d blocks, %d with a recoverable extent, %d UNKNOWN"
              % (len(blocks), len(known), len(unknown)))
        if unknown:
            print("  UNKNOWN blocks are NOT overlap-checked -- that is a hole "
                  "in this check, not a pass:")
            for b in unknown:
                print("    %-26s %05X  %s" % (b["name"], b["addr"], b["where"]))

    real = [(a, b) for a, b in overlaps if not is_intentional(a["name"], b["name"])]
    known_ok = len(overlaps) - len(real)
    if known_ok:
        print("\n  %d alias pair(s) skipped as deliberate (cached/uncached "
              "views, #ifdef alternatives)." % known_ok)
    if real:
        print("\n  COLLISIONS (%d):" % len(real))
        for a, b in real:
            print("    %s [%05X + %s, %s]"
                  % (a["name"], a["addr"],
                     ("%d" % a["size"]) if a["size"] else "alias", a["where"]))
            over = a["addr"] + (a["size"] or 0) - b["addr"]
            print("      runs into %s [%05X, %s]  by %d bytes"
                  % (b["name"], b["addr"], b["where"], over))
        return 1
    print("\n  no unexplained collision among blocks with a recoverable extent.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
