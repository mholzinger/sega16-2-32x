#!/usr/bin/env python3
"""Actual clipped copier against stateful native word rendering, no cut formula.

Fixtures include full stride320 backing memory, so XO padding that belongs to
the following row's prefix is compared as well as the visible viewport.
"""
from pathlib import Path
import argparse
import os
import struct
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from bake_zoom import pack


def words(count,flip,pattern,row):
    result=[]
    for n in range(count):
        if pattern==0:p=[1+(n*7+j*3+row)%14 for j in range(4)]
        elif pattern==1:p=[0,0,0,0]
        elif pattern==2:p=[0,0,1,0] if n%2==0 else [0,15,0,0]
        else:p=[((n*13+j*7+row*3)%16) for j in range(4)]
        if p[3]==15:p[3]=0
        if n==count-1:p[3]=15
        shifts=(0,4,8,12) if flip else (12,8,4,0)
        result.append(sum(pen<<shift for pen,shift in zip(p,shifts)))
    return result


def full_row(source,flip,hz):
    acc=(hz&15)*4;result=[]
    for word in source:
        for j in range(4):
            pen=(word>>(j*4 if flip else 12-j*4))&15
            acc+=hz;keep=not(acc&64);acc&=63
            if keep:result.append(pen if 0<pen<15 else 0)
        if pen==15:break
    return result


def native_row(source,flip,hz,x,xo,bank,memory,offset):
    """Original ZF_PUSH stores plus PLOT5's different hz0 diagnostic unit."""
    acc=(hz&15)*4;j=x&3;q=x&~3;cur=0;store_px=0;source_px=0
    def store(address,value):
        nonlocal store_px
        if value:
            assert not address&3 and 0<=address and address+4<=len(memory)-offset
            for i in range(4):
                byte=(value>>(24-8*i))&255
                if byte:memory[offset+address+i]=byte
            store_px+=4
    for word in source:
        opaque=False
        for k in range(4):
            pen=(word>>(k*4 if flip else 12-k*4))&15
            opaque|=0<pen<15
            acc+=hz;keep=not(acc&64);acc&=63
            if keep:
                value=bank*16+pen if 0<pen<15 else 0
                cur=((cur<<8)|value)&0xffffffff;j+=1;x+=1
                if j==4:store(q,cur);q+=4;j=0;cur=0
        if opaque:source_px+=4
        if pen==15 or x>=xo+320:break
    if j and cur and q<xo+320:store(q,cur<<(8*(4-j)))
    return x,store_px if hz else source_px


def encode(source,raw,hz,flip,distance,xo,bank,room,number):
    nrows=len(source)
    x=xo+320-distance;y=220 if nrows==4 else 83;record=number%64
    initial=bytes((n*17+number*3)%255+1 for n in range(nrows*320+16));expected=bytearray(initial)
    spans=[];full=0;px=0;xmax=0
    for r in range(nrows):
        xe,rpx=native_row(source[r],flip,hz,x,xo,bank,expected,r*320);px+=rpx;xmax=max(xmax,xe)
        end=min((xe+3)&~3,832);start=x&~3
        if end>start:
            if len(spans)<room:spans.append(((y+r)<<24)|(record<<16)|((start//4)<<8)|(end//4))
            else:full=1
    assert not len(raw)&3
    fields=[len(raw)//4,nrows,x,xo,bank,room,y,record,px,xmax,len(spans),full,len(initial),hz]
    return struct.pack('>14I',*fields)+raw+initial+expected+struct.pack('>'+str(len(spans))+'I',*spans)


def fixture(nrows,nwords,hz,flip,pattern,distance,xo,bank,room,number):
    source=[words(max(1,nwords-r*3),flip,pattern,r) for r in range(nrows)]
    packet=pack([full_row(w,flip,hz) for w in source]);width=struct.unpack_from('>H',packet,2)[0]
    if width<=distance:return None
    return encode(source,packet[4:],hz,flip,distance,xo,bank,room,number)


def real_fixtures(atlas_path,sprite_path,start):
    from bake_zoom import verify
    from s16art import load_rom
    from bake_art import unkeyword
    blob=atlas_path.read_bytes();raw=load_rom(sprite_path);verify(blob,raw);number=start
    for i in range(struct.unpack_from('>I',blob,4)[0]):
        key,zoom,at=struct.unpack_from('>3I',blob,16+i*12)
        bank,addr,pitch,flip=unkeyword(key);hz,vz=zoom&31,zoom>>5
        height,width=struct.unpack_from('>2H',blob,at);at+=4;packets=[];sources=[]
        for r in range(height):
            groups=blob[at+1];packets.append(blob[at:at+4+groups*4]);at+=4+groups*4
            first=(addr+pitch*(r+1+(r+1)*vz//32))&65535;source=[]
            for n in range(256):
                offset=first+(-n if flip else n)
                if not 0<=offset<65536:raise ValueError('real fixture crosses native raw bank')
                word=raw[bank*65536+offset];source.append(word)
                if (word>>12 if flip else word&15)==15:break
            else:raise ValueError('unterminated real fixture')
            sources.append(source)
        for row in range(height):
            for nr in sorted({1,min(4,height-row)}):
                for distance in sorted({1,2,width//2,width-1}):
                    for xo in range(4):
                        room=number%(nr+1)
                        yield encode(sources[row:row+nr],b''.join(packets[row:row+nr]),hz,flip,
                                     distance,xo,number%16,room,number)
                        number+=1


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--atlas',type=Path)
    ap.add_argument('--sprites',type=Path)
    ap.add_argument('--shared',action='store_true',help='exercise the shared-loop placement candidate')
    args=ap.parse_args()
    if bool(args.atlas)!=bool(args.sprites):ap.error('--atlas and --sprites are required together')
    with tempfile.TemporaryDirectory(prefix='workset-clip-oracle-') as temporary:
        d=Path(temporary);path=d/'fixtures.bin';count=0
        with path.open('wb') as f:
            f.write(b'WCO1\0\0\0\0')
            for hz in range(32):
                for flip in (0,1):
                    for pattern in range(4):
                        for nwords in (1,3,17,80):
                            for distance in (1,2,3,4,7,31,127,319):
                                for xo in range(4):
                                    for nrows in (1,4):
                                        room=(count%5) if nrows==4 else count%2
                                        b=fixture(nrows,nwords,hz,flip,pattern,distance,xo,
                                                  count%16,room,count)
                                        if b:f.write(b);count+=1
            if args.atlas:
                for b in real_fixtures(args.atlas,args.sprites,count):f.write(b);count+=1
            f.seek(4);f.write(struct.pack('>I',count))
        exe=d/'oracle'
        main=(ROOT/'sh_src/r60/r60_main.c').read_text()
        start=main.index('    r60_bk_stat[3] += nrows;',main.index('static R60BKCODE unsigned r60_bk_sprite('))
        end=main.index('    return 1;\n}\n#endif /* R60_BAKEART */',start)
        (d/'r60_clip_postcopy.inc').write_text(main[start:end])
        subprocess.run([os.environ.get('CC','clang'),'-std=c99','-O2','-g','-Wall','-Wextra',
                        '-Werror',*(['-DR60_WC_SHARED=1'] if args.shared else []),'-fsanitize=address,undefined','-fno-sanitize-recover=all',
                        '-I'+str(d),
                        str(Path(__file__).with_suffix('.c')),'-o',str(exe)],check=True)
        subprocess.run([str(exe),str(path)],check=True)


if __name__=='__main__':main()
