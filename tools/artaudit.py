#!/usr/bin/env python3
"""artaudit.py ROM FRAME [--input CSV] : every VISIBLE Mega Drive plane cell, its name-table
slot, and whether the art in VRAM at that slot is the art the renderer's tag for the slot says
(code, map id, plane) baked through r60_smap. Prints the cells whose VRAM art differs.
Symbols come from tools/romsyms.py: the map proven to be this rom's build, or a refusal."""
import sys, os, struct, subprocess, argparse
ap=argparse.ArgumentParser(); ap.add_argument('rom'); ap.add_argument('frame',type=int); ap.add_argument('--input')
a=ap.parse_args()
A=os.path.expanduser('~/src/ares-debug/build_macos/headless-ui/Release/ares-headless')
import romsyms
LST=romsyms.lst_for(a.rom)
def sym(n):
    for l in open(LST):
        if l.rstrip().endswith(' _'+n): return int(l.split()[0],16)
    raise SystemExit('no symbol '+n)
D=f'/tmp/artaudit_{os.getpid()}'; os.makedirs(D,exist_ok=True)
SM=sym('r60_smap')-0x06000000; TL=sym('altbeast_tiles')-0x02000000
cmd=[A,'--frames',str(a.frame+1)]+(['--input',a.input] if a.input else [])+[
 '--dump',f'VDP VRAM:0:0x10000:{D}/vram.bin','--dump',f'32X SDRAM:0x3B400:0x1000:{D}/tag.bin',
 '--dump',f'32X SDRAM:{SM:#x}:0x400:{D}/smap.bin',
 '--dump',f'VDP VSRAM:0:0x50:{D}/vsram.bin',a.rom]
subprocess.run(cmd,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
v=open(f'{D}/vram.bin','rb').read(); tag=struct.unpack('>1024I',open(f'{D}/tag.bin','rb').read())
smap=open(f'{D}/smap.bin','rb').read(); rom=open(a.rom,'rb').read()
vsram=open(f'{D}/vsram.bin','rb').read()
def expected(slot):
    t=tag[slot]
    if t>=0xFFFFFFFE: return None
    code=t&0x3FFF; bg=(t>>15)&1; mid=(t>>16)&0x7F; m=smap[mid*8:mid*8+8]
    px=rom[TL+code*64:TL+code*64+64]; out=bytearray(32)
    for y in range(8):
        for x in range(8):
            p=px[y*8+x]&7; pen=m[p] if (p or bg) else 0
            out[y*4+(x>>1)] |= pen<<(4 if not (x&1) else 0)
    return bytes(out)
bad=[]
for which,base in ((0,0xC000),(1,0xE000)):
    # Audit the cells the MD actually displays. TEXT_U is the game's newer scroll:
    # sw326's eye pan at f1345-1355 puts it 48-72px ahead of the committed MD scroll.
    # The shipping shim uses full-screen hscroll at FC00/FC02 and VSRAM words 0/1
    # (md_main.c:338,2488), so read those committed values from the VDP itself.
    xs=(-struct.unpack_from('>H',v,0xFC00+which*2)[0])&0x3FF
    ys=struct.unpack_from('>H',vsram,which*2)[0]&0x1FF
    for sy in range(0,224,8):
        for sx in range(0,320,8):
            mcol=((sx+xs)>>3)&63; mrow=((sy+ys)>>3)&31
            e=struct.unpack('>H',v[base+(mrow*64+mcol)*2:base+(mrow*64+mcol)*2+2])[0]; slot=e&0x7FF
            if slot==0 or slot>=1016: continue
            exp=expected(slot)
            if exp is None: bad.append(('AB'[which],sx,sy,slot,'untagged')); continue
            if v[slot*32:slot*32+32]!=exp: bad.append(('AB'[which],sx,sy,slot,hex(tag[slot])))
print(f'{os.path.basename(a.rom)} f{a.frame}: {len(bad)} visible cells whose VRAM art differs from their slot tag')
for b in bad[:30]: print('  plane %s screen (%d,%d) slot %d tag %s'%b)
