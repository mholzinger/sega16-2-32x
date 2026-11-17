#!/usr/bin/env python3
"""Independent synthetic image bundles exercise generic footer aggregation."""
from pathlib import Path
import sys
import tempfile
from PIL import Image, ImageDraw

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from rig_event_summary import summarize


def pair(a,b):
    return a << 16 | b


def image(page, words):
    result=Image.new('RGB',(320,224),(19,29,39))
    draw=ImageDraw.Draw(result)
    for row,word in enumerate(words):
        marker=0xD000 | page << 8 | (1+row//2) << 4 | row%2
        y=188+2*row
        for bit in range(32):
            draw.rectangle((bit*8,y,bit*8+7,y+1),fill='white' if word&(1<<(31-bit)) else 'black')
        for bit in range(16):
            draw.rectangle((256+bit*4,y,259+bit*4,y+1),fill='white' if marker&(1<<(15-bit)) else 'black')
    return result


def bundle(directory,pages):
    for page,words in enumerate(pages):
        image(page,words).save(directory/f'{page:02}.png')


def reject(directory,signature=None):
    try:
        summarize(directory,signature)
    except ValueError:
        return
    raise AssertionError('invalid bundle was accepted')


with tempfile.TemporaryDirectory(prefix='event-summary-') as temp:
    directory=Path(temp)
    # Deliberately use non-Altered-Beast event durations and build ID.
    pages=[[pair(173,180),pair(19,21),pair(91,99),pair(15,18)],
           [pair(20,21),pair(71,12),pair(6,18),pair(42,3)],
           [pair(22,9),pair(32,1),pair(17,2),pair(41,8)],
           [pair(4,11),pair(99,20),pair(12,7),pair(83,14)],
           [pair(18,86),pair(321,0x9182),pair(18,82),pair(765,0x9182)]]
    bundle(directory,pages)
    # Derived montage must never enter the strict decoder.
    Image.new('RGB',(1600,448)).save(directory/'montage.png')
    image(0,pages[0]).save(directory/'20-repeat.png')
    report=summarize(directory)
    assert report['signature']==0x9182 and len(report['screenshots'])==6
    assert report['frt_metrics_available'] is None
    assert report['store_stats_available'] is None
    assert report['producer_max_quanta'] is None
    assert report['producer_lookahead_rows'] is None
    assert report['events'][0]['native_ticks']==173 and report['events'][1]['native_ticks']==91
    assert report['events'][0]['lower_bound_overrun_fraction']==20/21
    assert report['events'][0]['applied_acks_per_native_tick']==19/173
    assert report['events'][1]['tag_gap'] and report['events'][1]['source_uncertain']
    assert summarize(directory,0x9182)==report
    reject(directory,0xEC20)
    (directory/'03.png').unlink();reject(directory);bundle(directory,pages)
    changed=pages[0].copy();changed[0]+=1
    image(0,changed).save(directory/'20-repeat.png');reject(directory)
    (directory/'20-repeat.png').unlink()
    mutations=[(4,0,pair(1,86)),(4,0,pair(18,1)),(4,0,pair(50,86)),
               (0,0,pair(0,180)),(0,1,pair(19,0)),
               (0,1,pair(0,21)),
               (0,0,pair(65535,180)),
               (1,0,pair(20,22)),(1,0,pair(22,21)),
               (4,3,pair(765,0x9183))]
    for page,field,value in mutations:
        changed=[words.copy() for words in pages]
        changed[page][field]=value
        bundle(directory,changed);reject(directory)
    bundle(directory,pages)
    # R60 signatures distinguish an available modular diagnostic from a LIGHT
    # placeholder. Arbitrary signatures above intentionally remain unknown.
    for signature,available,stats,quanta,lead in [(0xEC20,True,True,None,None),(0xECA0,False,True,None,None),
            (0xEC64,True,True,1,32),(0xECE4,False,True,1,32),
            (0xED64,True,False,1,32),(0xEDE4,False,False,1,32),
            (0xEE64,True,True,8,32),(0xEEE4,False,True,8,32),
            (0xEF64,True,False,8,32),(0xEFE4,False,False,8,32),
            (0xE864,True,True,1,8),(0xE8E4,False,True,1,8),
            (0xE964,True,False,1,8),(0xE9E4,False,False,1,8),
            (0xEA64,True,True,8,8),(0xEAE4,False,True,8,8),
            (0xEB64,True,False,8,8),(0xEBE4,False,False,8,8),
            (0xEBE7,False,False,8,8),
            (0xE064,True,True,1,16),(0xE0E4,False,True,1,16),
            (0xE164,True,False,1,16),(0xE1E4,False,False,1,16),
            (0xE264,True,True,8,16),(0xE2E4,False,True,8,16),
            (0xE364,True,False,8,16),(0xE3E4,False,False,8,16),
            (0xE3E7,False,False,8,16),(0xE4A4,None,None,None,None),
            (0xECA5,False,True,1,None),(0xECE7,False,True,1,32)]:
        changed=[words.copy() for words in pages]
        changed[4][1]=pair(321,signature);changed[4][3]=pair(765,signature)
        bundle(directory,changed)
        result=summarize(directory,signature)
        assert result['frt_metrics_available'] is available
        assert result['store_stats_available'] is stats
        assert result['producer_max_quanta']==quanta
        assert result['producer_lookahead_rows']==lead
        assert all(e['frt_metrics_available'] is available for e in result['events'])
        assert all(e['store_stats_available'] is stats for e in result['events'])
        assert all(e['producer_max_quanta']==quanta for e in result['events'])
        assert all(e['producer_lookahead_rows']==lead for e in result['events'])
        if available is False:assert 'not measured zero duration' in result['caveat']
        if stats is False:assert 'not measured zero store totals' in result['caveat']
    bundle(directory,pages)
    Image.new('RGB',(640,448)).save(directory/'08-invalid-source.png')
    reject(directory)
    print('event summary:generic durations/signature, repeated pages, montage exclusion and all invalid bundles pass')
