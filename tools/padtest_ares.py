#!/usr/bin/env python3
"""Exercise a standalone TCPADTEST ROM with both controller types and no pad.

The diagnostic trace reports four TH pairs, decoded buttons and a poll sequence
per port. Assert type classification, button delivery and one poll per video
frame; save screenshots for manual review of lamps, history and freeze/resume.
This checks an emulator model. It does not establish physical-pad compatibility.
"""
from pathlib import Path
import subprocess,concurrent.futures,csv,json,re,argparse

ap=argparse.ArgumentParser(description='Check a TCPADTEST controller ROM in ares with three/six/absent ports.')
ap.add_argument('rom',type=Path)
ap.add_argument('--md-list',type=Path,help='Matching 68K symbol list; defaults to ROM.md.lst')
ap.add_argument('--out',type=Path,required=True)
ap.add_argument('--ares',type=Path,default=Path.home()/'src/bin/ares-headless-sdram')
a=ap.parse_args();p=a.out.resolve();p.mkdir(parents=True,exist_ok=True);rom=a.rom.resolve()
lst=(a.md_list or rom.with_suffix('.md.lst')).read_text()
addr=int(re.search(r'^([0-9a-f]+) B pad_probe$',lst,re.M)[1],16)&0xffff
(p/'md_start.lst').write_text(lst)
buttons=['up','down','left','right','a','b','c','x','y','z','mode','start']
events=[]
for n,b in enumerate(buttons):
 f=180+n*40;events.extend([(f,b,1,1),(f+16,b,0,1)])
# Unfreeze after the independent B test, before testing the other lamps.
events.extend([(404,'b',1,1),(408,'b',0,1)])
for n,b in enumerate(['a','c','x','y','z','mode','start']):
 f=760+n*40;events.extend([(f,b,1,2),(f+16,b,0,2)])
events.sort()
(p/'buttons.csv').write_text('# frame,button,value,pad\n'+'\n'.join(','.join(map(str,e)) for e in events)+'\n')
def run(t):
 name,p1,p2=t;out=p/name;out.mkdir(exist_ok=True)
 cmd=[str(a.ares),'--frames','1100','--pad1',p1,'--pad2',p2,'--input',str(p/'buttons.csv'),'--gameframe',f'wram:{addr+10:#x}','--gameframe-log',str(out/'trace.csv'),'--gameframe-watch',f'wram:{addr:#x}:0x18','--dump',f'wram:0:0x10000:{out}/wram.bin']
 for f in (150,190,390,470,510,550,590,990):cmd+=['--screenshot',f'{f}:{out}/{f}.png']
 with (out/'run.log').open('w') as log:subprocess.run(cmd+[str(rom)],stdout=log,stderr=subprocess.STDOUT,check=True)
 rows=[]
 for r in csv.DictReader((out/'trace.csv').open()):
  b=bytes.fromhex(r[f'wram:{addr:#x}:0x18']);words=[int.from_bytes(b[i:i+2],'big') for i in range(0,24,2)]
  rows.append((int(r['frame']),words))
 active=[r for r in rows if r[0]>120];assert all(r[1][5] and r[1][11] for r in active),'page never polled'
 masks={'up':1,'down':2,'left':4,'right':8,'a':64,'b':16,'c':32,'x':1024,'y':512,'z':256,'mode':2048,'start':128}
 for port,kind in enumerate((p1,p2)):
  for f,w in active:
   val=w[port*6+4];assert bool(val&0x1000)==(kind=='fighting'),(name,f,port,hex(val))
  for fr,button,value,pad in events:
   if pad!=port+1 or not value:continue
   val=rows[fr+2][1][port*6+4];expected=kind!='none' and (button not in ('x','y','z','mode') or kind=='fighting')
   assert bool(val&masks[button])==expected,(name,fr,port,button,hex(val),expected)
  # Poll once per frame, no repeated handshake before the timeout reset.
  seq=[w[port*6+5] for f,w in active];assert all(b-a==1 for a,b in zip(seq,seq[1:])),(name,port,'poll cadence')
 result=dict(name=name,ports=[p1,p2],trace_address=hex(addr),button_checks='PASS',type_checks='PASS',one_poll_per_frame='PASS',final=[hex(x) for x in rows[-1][1]])
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(result,flush=True);return result
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:results=list(pool.map(run,[('six-three','fighting','control'),('three-six','control','fighting'),('absent','none','none')]))
(p/'checks.json').write_text(json.dumps(results,indent=2)+'\n')
