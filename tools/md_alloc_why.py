#!/usr/bin/env python3
"""md_alloc_why: the MD residency allocator's own census, per frame.

    make ship-us <line flags> MDALLOCWHY=1
    tools/md_alloc_why.py ROM 600 1800 2400 ...

Answers what LOOP29 152 could not: whether a blank cell never claimed a
slot or claimed one that something then wiped.  DIAG[39]/[50]/[53] are
shared with the DREQ path and cannot be read for this (152)."""
import os, struct, subprocess, sys, tempfile
ARES = os.environ.get("ARES", "/Users/mikeholzinger/src/ares-debug/"
                      "build_macos/headless-ui/Release/ares-headless")
N = ("cells", "hit", "claim", "evict", "c1decl", "blkcut", "blkdrt",
     "blkfg", "blkbot", "flush", "flushed", "instl", "instwipe",
     "freeset", "fswipe")
rom = os.path.abspath(sys.argv[1]); prev = None; pn = None
# the counters live in .bss: take the address from the build's symbol list
LST = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "rom", "s16.lst")
base = None
for line in open(LST):
    f = line.split()
    if len(f) == 3 and f[2].lstrip("_") == "mdalloc_ctr":
        base = int(f[0], 16)
if base is None:
    sys.exit("mdalloc_ctr not in rom/s16.lst - build with MDALLOCWHY=1")
off = base - 0x06000000
print("mdalloc_ctr at 0x%08X" % base)
print("frame | " + " ".join("%7s" % n for n in N) + " |  tags")
for n in map(int, sys.argv[2:]):
    with tempfile.TemporaryDirectory() as td:
        a = os.path.join(td, "a"); b = os.path.join(td, "b")
        subprocess.run([ARES, "--frames", str(n),
                        "--dump", f"sdram:0x{off:X}:0x40:{a}",
                        "--dump", f"sdram:0x3B400:0x1000:{b}", rom],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       check=True)
        A = open(a, "rb").read(); B = open(b, "rb").read()
    cur = [struct.unpack_from(">I", A, i * 4)[0] for i in range(len(N))]
    tags = sum(1 for i in range(1024)
               if struct.unpack_from(">I", B, i * 4)[0] != 0xFFFFFFFF)
    print("%5d | %s | %5d" % (n, " ".join("%7d" % c for c in cur), tags))
    if prev is not None:
        d = (n - pn)
        print("      + " + " ".join("%6.1f/f" % ((c - p) / d)
                                    for c, p in zip(cur, prev)))
    prev, pn = cur, n
