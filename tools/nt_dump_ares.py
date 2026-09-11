#!/usr/bin/env python3
"""nt_dump_ares: the nt_dump.lua evidence chain, from ares-headless.

    tools/nt_dump_ares.py ROM FRAME [OUTDIR]

nt_dump.lua can no longer serve this audit: under R60 no packet lands in
MAME (CLAUDE.md, LOOP29), so the mirror and the tags there are not our
machine's.  ares is.  Regions are the same five, as SDRAM offsets."""
import os, subprocess, sys
ARES = os.environ.get("ARES", "/Users/mikeholzinger/src/ares-debug/"
                      "build_macos/headless-ui/Release/ares-headless")
REGIONS = (("snap.bin",  0x052E8,   168),
           ("ntmir.bin", 0x3D200,  4480),
           ("mdtag.bin", 0x3B400,  4096),
           ("sline.bin", 0x3C4A0,   128),
           ("tmap.bin",  0x19000, 53248))
rom = os.path.abspath(sys.argv[1]); n = int(sys.argv[2])
out = os.path.abspath(sys.argv[3]) if len(sys.argv) > 3 else "/tmp/nt_ares"
os.makedirs(out, exist_ok=True)
cmd = [ARES, "--frames", str(n)]
for name, off, ln in REGIONS:
    cmd += ["--dump", "sdram:0x%X:0x%X:%s" % (off, ln, os.path.join(out, name))]
subprocess.run(cmd + [rom], stdout=subprocess.DEVNULL,
               stderr=subprocess.DEVNULL, check=True)
print(out)
