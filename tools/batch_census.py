#!/usr/bin/env python3
"""Batch composition census on ares (NOTES 51): per window, from
mdalloc_ctr (MDALLOCWHY=1 build) and DIAG.  One ares run per frame."""
import os, struct, subprocess, sys
ARES="/Users/mikeholzinger/src/ares-debug/build_macos/headless-ui/Release/ares-headless"
rom=sys.argv[1]; inp=sys.argv[2] if sys.argv[2]!='-' else None; frames=[int(x) for x in sys.argv[3:]]
base=None
for line in open('rom/s16.lst'):
    f=line.split()
    if len(f)==3 and f[2].lstrip('_')=='mdalloc_ctr': base=int(f[0],16)
assert base, "mdalloc_ctr not in rom/s16.lst"
off=base&0x3FFFF
def run(fr):
    cp=f"/tmp/bc_ctr_{fr}.bin"; dp=f"/tmp/bc_diag_{fr}.bin"
    cmd=[ARES,"--frames",str(fr)]+(["--input",inp] if inp else [])+["--dump",f"sdram:{off:#x}:192:{cp}","--dump","sdram:0x28000:0x400:"+dp,"--dump","sdram:0x28F54:4:/tmp/bc_gens_%d.bin"%fr,rom]
    subprocess.run(cmd,check=True,capture_output=True)
    c=struct.unpack('>48I',open(cp,'rb').read()); d=struct.unpack('>256I',open(dp,'rb').read()); g=struct.unpack('>I',open('/tmp/bc_gens_%d.bin'%fr,'rb').read())[0]
    return c,d,g
prev=None
print(f"{os.path.basename(rom)} {inp or 'attract'}: per window -- batches(non-empty) tiles tiles/batch | cell chunks, words | hits claims evictions | wiped: flush install freeset | gens")
for fr in frames:
    cur=run(fr)
    if prev:
        (c0,d0,g0),(c1,d1,g1)=prev,cur; fw=fr-pf
        dc=[c1[i]-c0[i] for i in range(48)]; dd=[d1[i]-d0[i] for i in range(256)]
        b=dc[34]; nb=dc[33]; t=dc[32]   # moved off [16..18]: they are the allocator's (2026-09-16)
        print(f"  f{pf}-{fr} ({fw} frames): batches {b} (non-empty {nb}) tiles {t} ({t/max(1,nb):.1f}/batch, {t/fw:.2f}/frame) | chunks {dc[35]} words {dc[36]} ({dc[36]/max(1,dc[35]):.0f}/chunk) | hits {dc[1]} claims {dc[2]} evict {dc[3]} | wiped {dc[10]} {dc[12]} {dc[14]} | gens {g1-g0}")
    prev=cur; pf=fr
