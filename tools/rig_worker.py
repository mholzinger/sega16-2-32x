#!/usr/bin/env python3
"""Decode worker-event counters from rows196..203, independently of D-footer.

Page0/1 contains phase1 counters0..3/4..7; page2/3 contains phase2.
Values are independent atomic counter observations at master event boundaries,
not a simultaneous cross-core snapshot or one rendered cohort. A zero value
does not prove capture completed: correlate with the completed event D-footer
for the same ROM. Counts measure work identities, never CPU duration or FPS.
"""
import argparse
import json
from pathlib import Path
from PIL import Image
from active_area import crop_active
from rig_event import _bits

COUNTERS=('requested_chunks','prepared_chunks','used_rows','fallback_rows',
          'cancelled_jobs','declined_jobs','decoded_source_words','stale_ready_discards')


def read(path, *, family=0xE000, counters=COUNTERS):
    if family not in (0xE000,0xF000) or len(counters)!=8:
        raise ValueError('unsupported counter footer domain')
    im=Image.open(path).convert('RGB')
    if im.size not in ((320,224),(1415,243)):
        raise ValueError(f'unrecognized capture geometry:{im.size}')
    im=crop_active(im)
    values=[];markers=[];page=None
    for field,y in enumerate((196,198,200,202)):
        copies=[]
        for yy in (y,y+1):
            value=_bits(im,yy,0,8,32);marker=_bits(im,yy,256,4,16)
            p=(marker>>8)&15
            if p>3 or marker!=(family|p<<8|field):
                raise ValueError('invalid worker page/field marker')
            if page is None:page=p
            if p!=page:raise ValueError('worker pages disagree')
            copies.append((value,marker))
        if copies[0]!=copies[1]:raise ValueError('worker duplicate rows disagree')
        values.append(copies[0][0]);markers.append(copies[0][1])
    first=(page%2)*4
    return dict(page=page,phase=1+page//2,counter_first=first,raw32_values=values,
                counters=dict(zip(counters[first:first+4],values)),markers=markers)


def summarize(directory, *, family=0xE000, counters=COUNTERS):
    pages={};shots=[]
    for path in sorted(Path(directory).glob('[0-9]*.png')):
        decoded=read(path,family=family,counters=counters);page=decoded['page']
        if page in pages and pages[page]!=decoded['raw32_values']:
            raise ValueError(f'worker retained page{page} changed')
        pages[page]=decoded['raw32_values'];shots.append(dict(path=str(path),**decoded))
    if set(pages)!=set(range(4)):raise ValueError('require all four worker pages')
    return dict(directory=str(directory),phases=[dict(phase=i+1,
        counters=dict(zip(counters,pages[i*2]+pages[i*2+1]))) for i in range(2)],
        screenshots=shots,caveat=__doc__.strip())


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs',nargs='+',type=Path)
    parser.add_argument('--out',type=Path)
    args=parser.parse_args()
    result=[summarize(p) if p.is_dir() else dict(path=str(p),**read(p)) for p in args.inputs]
    text=json.dumps(result,indent=2)+'\n'
    if args.out:args.out.write_text(text)
    else:print(text,end='')


if __name__=='__main__':main()
