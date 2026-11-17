#!/usr/bin/env python3
"""Fault injection for committed viewport, fine edges, pattern pens and tile zero."""
import struct
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from tile_vram_audit import audit
sd=bytearray(0x40000);vram=bytearray(0x10000);vsram=bytearray(0x50);art=bytearray(0x100000)
listing='06000978 b _r60_nt_shadow\n06005000 b _r60_smap\n06005400 b _r60_solid_lp\n'
pack=lambda b,p,v:struct.pack_into('>H',b,p,v)
def cell(which,col,slot):
    pack(vram,0xc000+which*8192+col*2,slot)
    pack(sd,0x978+which*4096+col*2,slot)
struct.pack_into('>I',sd,0x3b404,1)
struct.pack_into('>I',sd,0x3b408,0x18002)
sd[0x5000:0x5008]=bytes(range(8,16));sd[0x5008:0x5010]=bytes(range(8,16))
art[64:128]=bytes([0xf8+x for x in range(8)])*8
vram[32:64]=bytes.fromhex('09abcdef')*8
art[128:192]=bytes(64);vram[64:96]=bytes([0x88])*32
sd[0x5400]=5;vram[1016*32:1017*32]=bytes([0x55])*32
cell(0,0,1);cell(1,0,2);cell(1,1,1016)
def check():return audit(sd,listing,vram,art,vsram)
def good(r):return not r['wrong_pattern_pixels'] and not r['invalid_descriptors'] and not any(p['wrong_cells'] for p in r['planes'])
assert good(check())
cell(0,0,1025);assert check()['invalid_descriptors'];cell(0,0,1)
vram[32]^=1;assert check()['wrong_pattern_pixels']==1;vram[32]^=1
vram[0]^=1;assert check()['wrong_pattern_pixels']==1;vram[0]^=1
vram[0xc000]^=32;assert check()['planes'][0]['wrong_cells']==1;vram[0xc000]^=32
pack(sd,0x26000+0x74c*2,0x123) # source camera leads the committed hardware
assert good(check())
pack(vram,0xfc00,0xffff);pack(vram,0xc000+40*2,1)
assert check()['planes'][0]['wrong_cells']==1 # fine-scroll rightmost partial tile
pack(vram,0xc000+40*2,0);pack(vsram,0,1);pack(vram,0xc000+28*128,1)
assert check()['planes'][0]['wrong_cells']==1 # bottommost partial tile
print('PASS: committed scroll, fine edges, pattern corruption, stale tile zero and palette-cell corruption')
