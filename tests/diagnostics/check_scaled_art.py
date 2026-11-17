#!/usr/bin/env python3
"""Scaled decoder and serialized row extents versus a stateful carry oracle."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
import s16art
from bake_zoom import pack, compact_packet, unpack_compact

def oracle(words,bank,addr,pitch,flip,height,hz,vz):
    rows=[];counts=[];vertical=0
    for _ in range(height):
        addr=(addr+pitch)&65535
        vertical+=vz
        if vertical>=32:addr=(addr+pitch)&65535;vertical-=32
        source=addr;acc=(hz%16)*4;row=[];n=0
        for _ in range(256):
            word=words[bank*65536+source];n+=1
            source=(source+(-1 if flip else 1))&65535
            pens=[(word>>(j*4))&15 for j in range(4)]
            if not flip:pens.reverse()
            for pen in pens:
                acc+=hz
                if acc>=64:acc-=64
                else:row.append(pen if pen not in (0,15) else 0)
            if pens[-1]==15:break
        else:raise ValueError('no terminator')
        rows.append(row);counts.append(n)
    return rows,sum(counts),counts

# Distinct banks and row contents, with markers in both directions every eighth
# word, including across bank wrap. No random seed or copyrighted test asset.
words=[]
for bank in range(8):
    for n in range(65536):
        w=sum(((bank+n*(j+1)+j*3)%15)<<(j*4) for j in range(4))
        words.append(w|0xf00f if n%8==7 else w)
cases=0
for hz in range(32):
    for vz in range(32):
        for flip in [False,True]:
            for bank,pitch,addr in [(0,0,65535),(3,17,65520),(7,-128,3),(2,127,65535)]:
                args=(words,bank,addr,pitch,flip,20,hz,vz)
                actual=s16art.decode_scaled(*args)
                assert actual==oracle(*args),args[1:]
                packet=pack(actual[0])
                rows,ends,finish=s16art.unpack_frame(packet,0)
                packed=compact_packet(packet)
                cr,ce,cf=unpack_compact(packed,0)
                assert cr==rows and ce==ends and cf==len(packed)
                assert finish==len(packet)
                assert ends==[(-len(r))&65535 for r in actual[0]]
                for a,b in zip(rows,actual[0]):
                    n=max(len(a),len(b))
                    assert a+[0]*(n-len(a))==b+[0]*(n-len(b))
                cases+=1
for bank,pitch,addr in [(0,0,65535),(3,17,65520),(7,-128,3),(2,127,65535)]:
    for flip in [False,True]:
        assert s16art.decode_scaled(words,bank,addr,pitch,flip,20,0,0)==s16art.decode(words,bank,addr,pitch,flip,20)
for height in [0,1,255]:
    for hz,vz in [(0,31),(31,0),(31,31)]:
        args=(words,7,65535,-128,True,height,hz,vz)
        assert s16art.decode_scaled(*args)==oracle(*args)
for row in [[0],[0]*1024,[1,0,14],[0,0,0,0,7]]:
    packet=pack([row]);rows,ends,finish=s16art.unpack_frame(packet,0)
    assert ends==[(-len(row))&65535] and finish==len(packet)
    n=max(len(row),len(rows[0]))
    assert rows[0]+[0]*(n-len(rows[0]))==row+[0]*(n-len(row))
print(f'PASS {cases} scaled frames and packets: all1024zoom pairs, flips, signs, banks and wrap; unity matches existing decoder')
