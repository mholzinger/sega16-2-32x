#!/usr/bin/env python3
"""Attribute PC-sampler ring dumps to game functions (2026-09-24).

A `make line WRAMCODE=1 PCSAMP=1 PCSAMPN=15` rom samples the 68K's PC every
16 lines (13 a vint) into the 64-entry ring at 0xFFA200 (md_start.s PC_SAMP;
V|object index at 0xFFA300, running index at 0xFFA3FE). Dump the ring on
ares every 4 frames across the scene (one run per frame -- ares' --dump
reflects the end state):

    for f in $(seq 2040 4 2900); do ares-headless --frames $f \
        --dump wram:0xFFA200:0x200:r$f.bin rom/night/wcp2.32x; done
    tools/pc_profile.py <dir> [top]

Samples are de-duplicated by their running index, classified (WRAM-resident
game code, our shim/thunks, the game's idle loop, cart game code) and the
cart ones attributed to docs/audit/function_map.md entries (bounds repaired
per docs/audit/bound_repairs.md). Cart samples ON an entry stub (the jmp
planted at a relocated routine's ROM entry) count as cart: that is what the
call-site rewrite in tools/wram_reloc.py removes.

Level-1 demo on ares, 2026-09-24 (wcp2, 216 dumps): WRAM game 40%, idle 31%,
shim 17%, cart game 12%; the cart residue's top ranges became the second
WRAM block set (tools/game_altbeast.py WRAM_BLOCKS).
"""
import sys, glob, re, struct, collections
MAP='/Users/mikeholzinger/src/sega16-2-32x/docs/audit/function_map.md'
EXTEND = {0x0040E: 0x0057A, 0x05FA8: 0x0602C, 0x060E6: 0x06168,
          0x063CC: 0x0644E, 0x06C44: 0x06CB8, 0x18146: 0x181E4}
funcs=[]
for l in open(MAP):
    m=re.match(r'\| 0x([0-9A-Fa-f]+) \| (\d+) \| (\d+) \| ([^|]+) \|',l)
    if m:
        e=int(m.group(1),16); sz=int(m.group(2)); 
        if e in EXTEND: sz=EXTEND[e]-e
        funcs.append((e,sz,m.group(4).strip()))
funcs.sort()
import bisect
ents=[f[0] for f in funcs]
def lookup(off):
    i=bisect.bisect_right(ents,off)-1
    if i<0: return None
    e,sz,cl=funcs[i]
    return (e,sz,cl, off<e+sz)
d=sys.argv[1]; top=int(sys.argv[2]) if len(sys.argv)>2 else 30
seen=set(); samples=[]
for f in sorted(glob.glob(d+'/r*.bin'), key=lambda p:int(re.search(r'r(\d+)',p).group(1))):
    b=open(f,'rb').read()
    idx=struct.unpack('>H',b[0x1FE:0x200])[0]
    for q in range(64):
        pc=struct.unpack('>I',b[q*4:q*4+4])[0]
        vo=struct.unpack('>H',b[0x100+q*2:0x102+q*2])[0]
        # global sample number: ring slot q holds sample n with n%64==q, n in [idx-64, idx)
        n=(idx-64)+((q-(idx-64))%64)
        if n<0 or n in seen: continue
        seen.add(n); samples.append((n,pc,vo>>8,vo&0xFF))
print('samples',len(samples),'from',len(glob.glob(d+'/r*.bin')),'dumps')
cls=collections.Counter(); byfn=collections.Counter(); bypc=collections.Counter()
for n,pc,v,obj in samples:
    if 0xFF5000<=pc<0xFF5E00: cls['wram-game']+=1
    elif pc>=0xFF0000: cls['shim/thunk']+=1
    elif 0x903980<=pc<=0x903990: cls['idle']+=1
    elif 0x900000<=pc<0xA00000:
        cls['cart-game']+=1; bypc[pc-0x900000]+=1
        r=lookup(pc-0x900000)
        byfn[r[0] if r else -1]+=1
    else: cls['other %06X'%pc]+=1
tot=len(samples)
for k,v in cls.most_common(): print(f'{k:14s} {v:6d} {100*v/tot:5.1f}%')
print('\ncart-game by function (entry size class  samples  %of cart)')
cg=cls['cart-game'] or 1; acc=0
for e,c in byfn.most_common(top):
    if e<0: print('  <unmapped>',c); continue
    i=ents.index(e); sz=funcs[i][1]; cl=funcs[i][2]
    acc+=c
    print(f'  0x{e:05X} {sz:5d} {cl:18s} {c:5d} {100*c/cg:5.1f}%  cum {100*acc/cg:5.1f}%')
print('\ntop raw PCs:'); 
for pc,c in bypc.most_common(15): print(f'  0x{pc:05X} {c}')
