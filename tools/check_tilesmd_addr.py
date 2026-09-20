#!/usr/bin/env python3
"""The baked blob's cart address lives in TWO files. Fail if they drift.

    sh_src/mars.ld    .tilesmd ... : AT(0x00268000)
    md_src/md_main.c  the 68K recomputes the cart address from it

A silent mismatch makes the 68K fetch gap fill and render garbage a long
way from its cause -- which is exactly how .tilesmd's missing "a" flag
cost an evening (LESSONS). One number, two files, one check.
"""
import re, sys, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

ld = open(os.path.join(ROOT, "sh_src/mars.ld"), encoding="utf-8").read()
md = open(os.path.join(ROOT, "md_src/md_main.c"), encoding="utf-8").read()

m = re.search(r"\.tilesmd\s+0x[0-9A-Fa-f]+\s*:\s*AT\(\s*(0x[0-9A-Fa-f]+)\s*\)", ld)
if not m:
    sys.exit("check_tilesmd_addr: no .tilesmd AT() in sh_src/mars.ld")
ld_addr = int(m.group(1), 16)

# the 68K's literal, recognised by the index-table term that follows it
m = re.search(r"(0x[0-9A-Fa-f]+)uL\s*\+\s*5u\s*\*\s*128u\s*\*\s*2u", md)
if not m:
    print("  .tilesmd address: md_main.c has no slim-path literal "
          "(TILESLIM not wired?) -- nothing to compare")
    sys.exit(0)
md_addr = int(m.group(1), 16)

if ld_addr != md_addr:
    sys.exit("FATAL: .tilesmd address drift\n"
             "  sh_src/mars.ld   AT(0x%06X)\n"
             "  md_src/md_main.c   0x%06X\n"
             "  The 68K would fetch the wrong bytes and render garbage."
             % (ld_addr, md_addr))
print("  .tilesmd cart address agrees in both files: 0x%06X" % ld_addr)
