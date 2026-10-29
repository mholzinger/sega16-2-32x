#!/usr/bin/env python3
"""The baked blob's cart address lives in TWO files. Fail if they drift.

    sh_src/mars.ld    .tilesmd (DEFINED(__testcart) ? 0x0226B000 : 0x0226A000) :
    md_src/md_main.c  CART_READ_ADDR, and two bounds checks

THE REGEX WAS WRONG AND SO THE CHECK HAD BEEN A NO-OP. It looked for an `AT(...)`
clause that mars.ld stopped carrying when .tilesmd moved to a VMA-only placement, so
every run printed "no .tilesmd AT() in sh_src/mars.ld" and EXITED 0 -- a guard that
reports its own failure as success. Found 2026-10-03 while moving the blob to make a
full-art port build fit the cart, which is exactly the edit it exists to protect.

It now reads the non-testcart VMA and compares it against EVERY cart literal in
md_main.c, not just the slim path's, because the 68K has three.

A silent mismatch makes the 68K fetch gap fill and render garbage a long
way from its cause -- which is exactly how .tilesmd's missing "a" flag
cost an evening (LESSONS). One number, two files, one check.
"""
import re, sys, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

ld = open(os.path.join(ROOT, "sh_src/mars.ld"), encoding="utf-8").read()
md = open(os.path.join(ROOT, "md_src/md_main.c"), encoding="utf-8").read()

# THE AT() IS THE CART ADDRESS, AND IT IS ON THE LINE AFTER THE SECTION NAME --
# which is the whole reason the original regex matched nothing and this check had
# become a silent no-op. Read it across the newline, and read the VMA too so a drift
# BETWEEN them is caught as well: on 2026-10-03 I moved the VMA, left the AT() behind,
# and the link still placed the blob at the old cart address.
# BOTH CASES, because the blob now has TWO cart addresses: PORTFULL builds place it
# 8 KB higher (the port's driver pushes .ramtext's ROM image past the old one) and
# every other build must keep the original, since the accepted line has to stay
# byte-identical. Checking one of two is how a conditional move passes its own guard
# and still breaks the other half.
_LD_RE = (r"\.tilesmd[^\n]*\n?[^\n]*\n?\s*AT\(\s*DEFINED\(__portfull\)\s*\?\s*"
          r"\(DEFINED\(__testcart\)\s*\?\s*0x[0-9A-Fa-f]+\s*:\s*(0x[0-9A-Fa-f]+)\s*\)"
          r"[^\n]*\n?\s*:\s*\(DEFINED\(__testcart\)\s*\?\s*0x[0-9A-Fa-f]+\s*:\s*"
          r"(0x[0-9A-Fa-f]+)\s*\)")
m = re.search(_LD_RE, ld)
if m:
    pf_addr = int(m.group(1), 16) & 0xFFFFFF     # PORTFULL
    ln_addr = int(m.group(2), 16) & 0xFFFFFF     # every other build
    want = {}
    for tag, a in (('PORT_FULL', pf_addr), ('the line', ln_addr)):
        want[tag] = (a, 0x900000 | (a & 0x0FFFFF))
    lits = set()
    for mm in re.finditer(r"0x([29][0-9A-Fa-f]{5})uL", md):
        lits.add(int(mm.group(1), 16))
    bad = []
    for tag, pair in want.items():
        missing = [v for v in pair if v not in lits]
        if missing:
            bad.append('%s needs %s' % (tag, ', '.join('0x%06X' % v for v in missing)))
    if bad:
        sys.exit("FATAL: .tilesmd address drift between sh_src/mars.ld and "
                 "md_src/md_main.c\n  mars.ld: PORT_FULL 0x%06X, the line 0x%06X\n"
                 "  md_main.c names: %s\n  missing: %s\n"
                 "  The 68K would fetch the wrong bytes and render garbage."
                 % (pf_addr, ln_addr,
                    ', '.join('0x%06X' % v for v in sorted(lits)) or 'none',
                    '; '.join(bad)))
    print("  .tilesmd cart address agrees in BOTH cases: PORT_FULL 0x%06X "
          "(banked 0x%06X), the line 0x%06X (banked 0x%06X)"
          % (pf_addr, 0x900000 | (pf_addr & 0xFFFFF),
             ln_addr, 0x900000 | (ln_addr & 0xFFFFF)))
    sys.exit(0)

m = re.search(r"\.tilesmd[^\n]*\n?\s*AT\(\s*DEFINED\(__testcart\)\s*\?\s*"
              r"0x[0-9A-Fa-f]+\s*:\s*(0x[0-9A-Fa-f]+)\s*\)", ld)
_vma = re.search(r"\.tilesmd\s*\(DEFINED\(__testcart\)\s*\?\s*0x[0-9A-Fa-f]+\s*:\s*"
                 r"(0x[0-9A-Fa-f]+)\s*\)", ld)
if m and _vma and (int(_vma.group(1), 16) & 0xFFFFFF) != (int(m.group(1), 16) & 0xFFFFFF):
    sys.exit("FATAL: .tilesmd VMA 0x%06X and AT() 0x%06X disagree in sh_src/mars.ld.\n"
             "  The AT() is what decides where the bytes land in the cart, so the VMA "
             "alone being right places the blob at the OLD address."
             % (int(_vma.group(1), 16) & 0xFFFFFF, int(m.group(1), 16) & 0xFFFFFF))
if not m:
    m = _vma or re.search(r"\.tilesmd\s+(0x[0-9A-Fa-f]+)\s*:", ld)
if not m:
    sys.exit("FATAL: no .tilesmd placement found in sh_src/mars.ld -- this check "
             "cannot be silently skipped, it is the guard against the 68K fetching "
             "gap fill and rendering garbage a long way from its cause.")
ld_addr = int(m.group(1), 16) & 0xFFFFFF   # the VMA is the cart LMA | 0x02000000

# EVERY cart literal the 68K uses for this blob, not just one: CART_READ_ADDR is in
# the banked 0x900000 window (cart | 0x900000) and the two bounds checks are bare cart
# addresses. Checking one of three is how a move passes its own guard and still breaks.
# BANK 2 maps cart 0x200000-0x2FFFFF to the 68K's 0x900000 window, so the banked
# form keeps only the low 20 bits: 0x26A000 -> 0x96A000. `0x900000 | ld_addr` keeps
# bit 0x200000 and yields 0xB6A000, an address that is in neither file -- which the
# check then reported in its own PASS message. A guard whose message is wrong is one
# edit away from a guard whose verdict is wrong.
bank = 0x900000 | (ld_addr & 0x0FFFFF)
lits = set()
for mm in re.finditer(r"0x(9[0-9A-Fa-f]{5})uL", md):
    lits.add(int(mm.group(1), 16))
for mm in re.finditer(r"0x(2[0-9A-Fa-f]{5})uL", md):
    lits.add(int(mm.group(1), 16))
want = {bank, ld_addr}
bad = sorted(v for v in lits if (v & 0xFFFFF000) in
             {(ld_addr & 0xFFFFF000) ^ 0x1000, ((bank & 0xFFFFF000) ^ 0x1000)})
if not want.issubset(lits):
    sys.exit("FATAL: .tilesmd is at 0x%06X in sh_src/mars.ld but md_src/md_main.c "
             "does not name BOTH 0x%06X and its banked form 0x%06X (it names %s).\n"
             "  The 68K would fetch the wrong bytes and render garbage."
             % (ld_addr, ld_addr, bank,
                ', '.join('0x%06X' % v for v in sorted(lits)) or 'none'))
if bad:
    sys.exit("FATAL: .tilesmd address drift -- md_main.c still names %s while "
             "mars.ld says 0x%06X" % (', '.join('0x%06X' % v for v in bad), ld_addr))
print("  .tilesmd cart address agrees: mars.ld 0x%06X, md_main.c names 0x%06X/0x%06X"
      % (ld_addr, ld_addr, bank))
