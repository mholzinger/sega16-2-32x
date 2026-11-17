#!/usr/bin/env python3
"""Check settled visible MD plane cells and their pattern pixels independently.

Reconstruct each resident pattern from decoded source art and the captured pen
map. Intended for settled fixed scenes; live/in-flight shadows can lead VRAM.
This is not native RGB parity, cadence or hardware performance evidence.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def audit(sd, listing, vram, art, vsram):
    if (len(sd),len(vram),len(art),len(vsram))!=(0x40000,0x10000,0x100000,0x50):
        raise ValueError('expected full SDRAM, MD VRAM/VSRAM and decoded tile ROM')
    sy={p[2]:int(p[0],16)&0x3ffff for line in listing.splitlines()
        if len(p:=line.split())==3}
    def symbol(name):
        matches=[v for k,v in sy.items() if k==name or k.startswith(name+'.')]
        if len(matches)!=1:raise ValueError('missing/ambiguous symbol '+name)
        return matches[0]
    shadow=symbol('_r60_nt_shadow');maps=symbol('_r60_smap')
    solid=symbol('_r60_solid_lp')
    u16=lambda b,p:struct.unpack_from('>H',b,p)[0]
    u32=lambda b,p:struct.unpack_from('>I',b,p)[0]
    patterns={};planes=[];invalid=[]
    for which in (0,1):
        # Committed hardware scroll, not the newer game/shadow scroll. Include
        # the last partial tile at the right/bottom of a fine-scrolled view.
        xs=(-u16(vram,0xfc00+which*2))&0x3ff
        ys=u16(vsram,which*2)&0x1ff
        cols=(xs%8+320+7)//8; rows=(ys%8+224+7)//8
        bad=0
        for y in range(rows):
            for x in range(cols):
                cell=(((ys//8+y)&31)*64+((xs//8+x)&63))
                wanted=u16(sd,shadow+which*4096+cell*2)
                actual=u16(vram,0xc000+which*8192+cell*2)
                if wanted in (0xffff,0x0400):invalid.append([which,x,y,wanted])
                if actual!=wanted:bad+=1
                slot=actual&2047
                if slot>=1024:
                    invalid.append(['hardware_slot',which,x,y,slot]);continue
                if slot not in patterns:
                    if slot==0:expected=bytes(32)
                    elif slot>=1016:
                        pen=sd[solid+slot-1016]&15;expected=bytes([pen*17])*32
                    else:
                        key=u32(sd,0x3b400+slot*4)
                        if key>=0xfffffffe:invalid.append(['slot',slot,key]);continue
                        code=key&0x3fff;bank=(key>>16)&127;bg=bool(key&0x8000)
                        source=art[code*64:code*64+64];out=[]
                        for p in source:
                            p&=7;out.append(sd[maps+bank*8+p] if p or bg else 0)
                        expected=bytes((out[i]<<4)|out[i+1] for i in range(0,64,2))
                    actual_art=vram[slot*32:slot*32+32]
                    wrong=sum(((a>>s)&15)!=((b>>s)&15)
                              for a,b in zip(actual_art,expected) for s in (0,4))
                    patterns[slot]=wrong
        planes.append(dict(plane='AB'[which],scroll_x=xs,scroll_y=ys,
                           checked_cells=rows*cols,wrong_cells=bad))
    r=dict(scope='settled visible MD cells/pattern pens; not native RGB, cadence or speed',
           planes=planes,checked_patterns=len(patterns),checked_pattern_pixels=len(patterns)*64,
           wrong_pattern_pixels=sum(patterns.values()),
           wrong_patterns={str(k):v for k,v in patterns.items() if v},invalid_descriptors=invalid)
    if '_r60_tb_stats' in sy:
        r['batch_stats']=struct.unpack_from('>8I',sd,sy['_r60_tb_stats'])
    return r


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('sdram','listing','vram','art','vsram'):ap.add_argument(name,type=Path)
    ap.add_argument('--out',type=Path,required=True);a=ap.parse_args()
    r=audit(a.sdram.read_bytes(),a.listing.read_text(),a.vram.read_bytes(),a.art.read_bytes(),a.vsram.read_bytes())
    r['inputs']={str(p):hashlib.sha256(p.read_bytes()).hexdigest()
                 for p in (a.sdram,a.listing,a.vram,a.art,a.vsram)}
    a.out.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
    return int(bool(r['wrong_pattern_pixels'] or r['invalid_descriptors'] or
                    any(p['wrong_cells'] for p in r['planes'])))


if __name__=='__main__':raise SystemExit(main())
