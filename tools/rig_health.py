#!/usr/bin/env python3
"""Decode mode4 clock words. Visible clocks freeze if presentation stops;
they cannot alone distinguish a frozen display from a stopped simulation.
Applied flips are observed bank acknowledgments, not unique image changes.
"""
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image
from active_area import crop_active


def read(path):
    im=Image.open(path).convert('RGB')
    if im.size not in ((320,224),(1415,243)):
        raise ValueError(f'unrecognized capture geometry: {im.size}')
    im=crop_active(im)
    words=[]
    for y in (188,190,192,194):
        rows=[]
        for yy in (y,y+1):
            value=marker=0
            for bit in range(16):
                # Interior samples tolerate the hardware's one-pixel shift.
                pixels=[im.getpixel((bit*16+x,yy)) for x in range(2,14)]
                if any(p not in ((0,0,0),(255,255,255)) for p in pixels):
                    raise ValueError('clock uses an unexpected RGB color')
                if len(set(pixels))!=1:raise ValueError('mixed clock bit')
                value=(value<<1)|(pixels[0]==(255,255,255))
                pixels=[im.getpixel((256+bit*4+x,yy)) for x in (1,2)]
                if len(set(pixels))!=1 or pixels[0] not in ((0,0,0),(255,255,255)):
                    raise ValueError('mixed marker bit')
                marker=(marker<<1)|(pixels[0]==(255,255,255))
            if marker!=0xa55a:raise ValueError('missing mode4 marker')
            rows.append(value)
        if rows[0]!=rows[1]:raise ValueError('clock row disagreement')
        words.append(rows[0])
    return dict(zip(('vints','native_game_clock','tick_calls','applied_flip_acks'),words))


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('images',nargs='+',type=Path)
    ap.add_argument('--out',type=Path,required=True)
    a=ap.parse_args();rows=[]
    for p in a.images:
        rows.append(dict(path=str(p),**read(p),sha256=hashlib.sha256(p.read_bytes()).hexdigest()))
    a.out.write_text(json.dumps(dict(scope=__doc__.strip(),samples=rows),indent=2)+'\n')
    for row in rows:print(row)


if __name__=='__main__':main()
