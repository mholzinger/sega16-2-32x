#!/usr/bin/env python3
"""nt_dump_ares: the nt_dump.lua evidence chain, from ares-headless.

    tools/nt_dump_ares.py ROM FRAME [OUTDIR]

nt_dump.lua can no longer serve this audit: under R60 no packet lands in
MAME (CLAUDE.md, LOOP29), so the mirror and the tags there are not our
machine's.  ares is.  Regions are the same five, as SDRAM offsets."""
import os, re, subprocess, sys
ARES = os.environ.get("ARES", "/Users/mikeholzinger/src/ares-debug/"
                      "build_macos/headless-ui/Release/ares-headless")
HERE = os.path.dirname(os.path.abspath(__file__))

# EVERY OFFSET COMES FROM tools/sdram_map.py, NEVER FROM A CONSTANT HERE.
# Measured 2026-10-10: this table carried snap at 0x052E8 while the port build
# puts it at 0x39940 (m_main.c:4378 -- WIN_TWO moved it into the 0x39800 gap).
# A wrong base does not fail, it reads a neighbouring block as a layer_regs, so
# every number this tool printed between those two addresses was of some other
# structure.  The blocks move with the flags, so resolve by SYMBOL each run and
# die when the symbol is gone or doubled rather than fall back to a literal.
REGIONS = (("snap.bin",  "snap",        168),
           ("ntmir.bin", "md_dbg_nt",  4480),
           ("mdtag.bin", "md_tag",     4096),
           ("sline.bin", "mdp_s_line",  128),
           ("tmap.bin",  "TILEMAP_U", 53248))


def sdram_offsets(names):
    """symbol -> SDRAM offset, out of tools/sdram_map.py's declared map."""
    # NOT check=True: sdram_map exits 1 on the seven pre-existing untriaged
    # overlaps (CLAUDE.md), which is a report, not a failure of the parse.
    r = subprocess.run([sys.executable, os.path.join(HERE, "sdram_map.py")],
                       capture_output=True, text=True)
    txt = r.stdout
    if not txt.strip():
        sys.exit("nt_dump_ares: sdram_map.py printed nothing (exit %d): %s"
                 % (r.returncode, r.stderr.strip()[:200]))
    found = {}
    for line in txt.splitlines():
        m = re.match(r"\s+(\S+)\s+([0-9A-F]{4,6})\b", line)
        if m and m.group(1) in names:
            found.setdefault(m.group(1), set()).add(int(m.group(2), 16))
    out = {}
    for nm in names:
        hits = found.get(nm)
        if not hits:
            sys.exit("nt_dump_ares: sdram_map.py does not declare %r -- the "
                     "symbol was renamed or its #ifdef branch is gone. Fix the "
                     "REGIONS table, do not guess an address." % nm)
        if len(hits) > 1:
            sys.exit("nt_dump_ares: %r is declared at %s. Name the branch you "
                     "mean in REGIONS." % (nm, ", ".join("0x%X" % h
                                                         for h in sorted(hits))))
        out[nm] = hits.pop()
    return out
rom = os.path.abspath(sys.argv[1]); n = int(sys.argv[2])
out = os.path.abspath(sys.argv[3]) if len(sys.argv) > 3 else "/tmp/nt_ares"
os.makedirs(out, exist_ok=True)
OFF = sdram_offsets([sym for _, sym, _ in REGIONS])
cmd = [ARES, "--frames", str(n)]
for name, sym, ln in REGIONS:
    print("%-10s %-11s 0x%05X" % (name, sym, OFF[sym]), file=sys.stderr)
    cmd += ["--dump", "sdram:0x%X:0x%X:%s" % (OFF[sym], ln,
                                              os.path.join(out, name))]
subprocess.run(cmd + [rom], stdout=subprocess.DEVNULL,
               stderr=subprocess.DEVNULL, check=True)
print(out)
