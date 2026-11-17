#!/usr/bin/env python3
"""Complete D/F bundles, native/raw geometry and strict counter domains."""
from pathlib import Path
import sys,tempfile
from PIL import Image,ImageDraw
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from active_area import X0,Y0,W,H,RAW
from rig_event import read as event_read
from rig_event_summary import summarize as event_summary
from rig_worker import read as worker_read
from rig_workingset import read,summarize,COUNTERS

def pair(a,b):return a<<16|b
def paint(dp,fp,signature=0xDCA1,family=0xF000,control=False,postword=None):
    pages=[[pair(126,130),pair(31,33),pair(124,128),pair(29,32)],
           [pair(3,33),0,pair(2,32),0],[0]*4,[0]*4,
           [pair(18,82),pair(321,signature),pair(18,82),pair(654,signature)]]
    counts=[0]*4 if control else [fp*4+i+1 for i in range(4)]
    if fp%2:counts[-1]=(pair(9,4) if (signature>>4)&3==1 else 0) if postword is None else postword
    im=Image.new('RGB',(320,224),(19,29,39));draw=ImageDraw.Draw(im)
    for start,page,base,words in [(188,dp,0xD000,pages[dp]),(196,fp,family,counts)]:
        for row,value in enumerate(words):
            marker=base|page<<8|(((1+row//2)<<4|row%2) if base==0xD000 else row)
            y=start+2*row
            for bit in range(32):draw.rectangle((8*bit,y,8*bit+7,y+1),fill='white' if value&(1<<(31-bit)) else 'black')
            for bit in range(16):draw.rectangle((256+4*bit,y,259+4*bit,y+1),fill='white' if marker&(1<<(15-bit)) else 'black')
    return im

def rejects(call):
    try:call()
    except ValueError:return
    raise AssertionError('invalid capture accepted')

with tempfile.TemporaryDirectory(prefix='workingset-words-') as temp:
    directory=Path(temp);one=directory/'single.png'
    for n in range(20):
        im=paint(n%5,n%4)
        im.save(one);expected=read(one)
        assert event_read(one)['page']==n%5
        rejects(lambda:worker_read(one))
        for dx in (-1,0,1):
            shifted=Image.new('RGB',im.size,'black');shifted.paste(im,(dx,0));shifted.save(one)
            assert read(one)==expected
            raw=Image.new('RGB',RAW,(19,29,39));raw.paste(shifted.resize((W,H),Image.Resampling.NEAREST),(X0,Y0));raw.save(one)
            assert read(one)==expected
        im.save(directory/f'{n:02}.png')
    result=summarize(directory,0xDCA1)
    assert result['event_footer']['working_set'] is True and result['event_footer']['boot_banks'] is True
    assert result['event_footer']['frt_metrics_available'] is False
    assert result['event_footer']['store_stats_available'] is True
    assert result['event_footer']['producer_max_quanta'] is None
    assert result['phases'][0]['counters']==dict(zip(COUNTERS,[1,2,3,4,5,6,7,0]))
    rejects(lambda:summarize(directory,0xD4A1))
    paint(0,0,family=0xE000).save(one);rejects(lambda:read(one));assert worker_read(one)['page']==0
    for signature,ws,boot in [(0xD4A1,False,True),(0xD8A1,True,False),(0xDCA1,True,True),(0xD421,False,True),(0xD491,False,True),(0xDC91,True,True)]:
        for n in range(20):paint(n%5,n%4,signature,control=not ws).save(directory/f'{n:02}.png')
        result=summarize(directory,signature);e=result['event_footer']
        assert e['working_set']==ws and e['boot_banks']==boot
        assert e['frt_metrics_available']==(not bool(signature&0x80))
        assert e['producer_lookahead_rows'] is None
        if (signature>>4)&3==1:
            assert result['phases'][0]['post8']==dict(native_ticks=8,vints=9,applied_acks=4,restored_by_end=True)
            assert result['phases'][0]['event_plus_post8']['native_ticks']==134
            assert result['phases'][0]['event_plus_post8']['vints']==139
            assert result['phases'][0]['event_plus_post8']['applied_acks']==35
            for fp in range(4):
                im=paint(4,fp,signature,control=not ws);im.save(one);native=read(one)
                raw=Image.new('RGB',RAW);raw.paste(im.resize((W,H),Image.Resampling.NEAREST),(X0,Y0));raw.save(one)
                assert read(one)==native
        else:assert 'event_plus_post8' not in result['phases'][0]
    for invalid in (0xffffffff,0xfffffffe,0x0001ffff):
        for n in range(20):paint(n%5,n%4,0xDC91,postword=invalid).save(directory/f'{n:02}.png')
        rejects(lambda:summarize(directory))
    for n in range(20):paint(n%5,n%4,0xD4A1).save(directory/f'{n:02}.png')
    rejects(lambda:summarize(directory)) # control may not claim nonzero WS work
    # Wrong signature domain, changed retained F page, and missing F/D pages reject.
    for n in range(20):paint(n%5,n%4,0xECA1).save(directory/f'{n:02}.png')
    rejects(lambda:summarize(directory))
    for n in range(20):paint(n%5,n%4).save(directory/f'{n:02}.png')
    paint(0,0,control=True).save(directory/'20.png');rejects(lambda:summarize(directory));(directory/'20.png').unlink()
    for n in range(20):
        if n%4==3:(directory/f'{n:02}.png').unlink()
    rejects(lambda:summarize(directory))
    for n in range(20):paint(n%5,n%4).save(directory/f'{n:02}.png')
    for n in range(20):
        if n%5==4:(directory/f'{n:02}.png').unlink()
    rejects(lambda:summarize(directory))
    print('PASS D/F native/raw shifted decoding, five/four-page bundles, signatures and domain rejection')
