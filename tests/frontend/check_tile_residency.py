#!/usr/bin/env python3
"""Enter/return from two menus with real input; verify tile-zero reuse and restoration."""
import argparse
import csv
import json
import struct
import subprocess
from pathlib import Path
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('rom',type=Path);ap.add_argument('--md-listing',type=Path,required=True)
ap.add_argument('--out',type=Path,required=True)
ap.add_argument('--pad',choices=['control','fighting'],default='fighting')
ap.add_argument('--ares',type=Path,default=Path.home()/'src/bin/ares-headless-ssf')
a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=True)
events=[(0,'b',1),(60,'b',0),(300,'b',1),(304,'b',0),
        (600,'b',1),(620,'start',1),(632,'start',0),(640,'b',0),
        (720,'b',1),(724,'b',0)]
tape=a.out/'input.csv';tape.write_text('\n'.join(','.join(map(str,e)) for e in events)+'\n')
cmd=[str(a.ares),str(a.rom.resolve()),'--frames','1100','--pad1',a.pad,'--input',str(tape),
     '--gameframe','sdram:0x2fc2a','--gameframe-log',str(a.out/'frames.csv'),
     '--dump',f'wram:0:0x10000:{a.out}/md.bin',
     '--dump',f'VDP VRAM:0:0x10000:{a.out}/vram.bin',
     '--dump',f'sdram:0:0x40000:{a.out}/sd.bin']
with (a.out/'run.log').open('w') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
rows=list(csv.DictReader((a.out/'frames.csv').open()))
assert len({r['gameframe'] for r in rows[660:705]})==1,'second menu did not pause'
assert len({r['gameframe'] for r in rows[800:850]})>1,'second return did not resume'
symbols={p[2]:int(p[0],16)&65535 for l in a.md_listing.read_text().splitlines() if len(p:=l.split())==3}
rx=struct.unpack_from('>6I',(a.out/'md.bin').read_bytes(),symbols['r60_tb_rx'])
assert rx[3]==0 and rx[4]>0 and rx[5]>=2,rx
assert (a.out/'vram.bin').read_bytes()[:32]==bytes(32),'stale pattern zero after menu'
sd=(a.out/'sd.bin').read_bytes();timeouts=struct.unpack_from('>I',sd,0x28e7c)[0]
assert not timeouts,timeouts
r=dict(pad=a.pad,zero_hits=rx[4],zero_initializations=rx[5],malformed_records=rx[3],timeouts=timeouts,
       second_menu_pauses=True,second_return_resumes=True,pattern_zero_restored=True)
(a.out/'result.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r))
