#!/usr/bin/env python3
"""Synthetic worker footers, coexistence with event footer, strict bundles."""
from pathlib import Path
import random
import sys
import tempfile
from PIL import Image,ImageDraw

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from active_area import X0,Y0,W,H,RAW
from rig_event import read as event_read
from rig_worker import read,summarize,COUNTERS


def paint(page,words):
    im=Image.new('RGB',(320,224),(24,48,72));draw=ImageDraw.Draw(im)
    for start,base,p,values in ((188,0xD000,0,[123,456,789,1000]),(196,0xE000,page,words)):
        for field,value in enumerate(values):
            marker=base|p<<8|((1+field//2)<<4|field%2 if base==0xD000 else field)
            y=start+field*2
            for bit in range(32):
                draw.rectangle((bit*8,y,bit*8+7,y+1),fill='white' if value&(1<<(31-bit)) else 'black')
            for bit in range(16):
                draw.rectangle((256+bit*4,y,259+bit*4,y+1),fill='white' if marker&(1<<(15-bit)) else 'black')
    return im


with tempfile.TemporaryDirectory(prefix='worker-words-') as temp:
    directory=Path(temp);path=directory/'capture.png';rng=random.Random(0x173462)
    def decode(im):
        im.save(path);return read(path)
    def reject(im):
        try:decode(im)
        except ValueError:return
        raise AssertionError('invalid worker capture accepted')
    for page in range(4):
        for case in range(8):
            words=[rng.getrandbits(32) for _ in range(4)] if case else [0,0xFFFFFFFF,0x80000000,1]
            im=paint(page,words)
            for dx in (-1,0,1):
                shifted=Image.new('RGB',im.size,'black');shifted.paste(im,(dx,0))
                result=decode(shifted)
                assert result['page']==page and result['phase']==1+page//2
                assert result['counter_first']==4*(page%2) and result['raw32_values']==words
                assert event_read(path)['raw32_values']==[123,456,789,1000]
                raw=Image.new('RGB',RAW,(24,48,72));raw.paste(shifted.resize((W,H),Image.Resampling.NEAREST),(X0,Y0))
                assert decode(raw)==result
    im=paint(0,[0,1,2,3])
    reject(im.resize((640,448)))
    bad=im.copy();bad.putpixel((2,196),(128,128,128));reject(bad)
    bad=im.copy();ImageDraw.Draw(bad).rectangle((0,197,7,197),fill='white');reject(bad)
    for marker in (0xD000,0xE400,0xE001,0xE010):
        bad=im.copy();draw=ImageDraw.Draw(bad)
        for bit in range(16):draw.rectangle((256+bit*4,196,259+bit*4,197),fill='white' if marker&(1<<(15-bit)) else 'black')
        reject(bad)
    # Matching pages aggregate sixteen full32bit counts with no clipping.
    for page in range(4):paint(page,[page*4+j for j in range(4)]).save(directory/f'{page:02}.png')
    Image.new('RGB',(1600,448)).save(directory/'montage.png')
    report=summarize(directory)
    assert list(report['phases'][0]['counters'])==list(COUNTERS)
    assert list(report['phases'][0]['counters'].values())==list(range(8))
    assert list(report['phases'][1]['counters'].values())==list(range(8,16))
    (directory/'03.png').unlink()
    try:summarize(directory)
    except ValueError:pass
    else:raise AssertionError('missing page accepted')
    paint(3,[12,13,14,15]).save(directory/'03.png')
    paint(1,[101,102,103,104]).save(directory/'20-changing.png')
    try:summarize(directory)
    except ValueError:pass
    else:raise AssertionError('changing retained page accepted')
    print('worker footer:192 native/raw shifted captures, event-footer coexistence, full32bit bundle and malformed input rejection pass')
