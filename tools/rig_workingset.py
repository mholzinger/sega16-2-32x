#!/usr/bin/env python3
"""Decode F-family working-set event counters, paired with completed D records.

Counts are individually atomic observations across CPUs at master event-class
boundaries before that tick's mapper service. Mapper work outside the selected
native phases (including post-event restoration) is excluded. These are not
one simultaneous snapshot or the same rendered cohort. Mode1 appends a fixed
eight-native-tick postwindow that verifies normal-page restoration; mode2
has no inclusive total. Switch
and withheld-VInt counts include mapper costs; they do not measure CPU time.
Zero counters alone cannot prove completion. No FPS or performance claim.
"""
import argparse
import json
from pathlib import Path
import rig_worker
from rig_event_summary import summarize as event_summary

COUNTERS=('page_switches','withheld_ack_vints','withheld_busy_vints',
          'scaled_requests','scaled_copied_records','scaled_copied_rows',
          'scaled_clipped_records','post8_clock_or_reserved')

def read(path):
    return rig_worker.read(path,family=0xF000,counters=COUNTERS)

def summarize(directory,signature=None):
    events=event_summary(directory,signature)
    if events['working_set'] is None:
        raise ValueError('not a working-set/boot-control signature domain')
    result=rig_worker.summarize(directory,family=0xF000,counters=COUNTERS)
    mode=(events['signature']>>4)&3
    for phase,event in zip(result['phases'],events['events']):
        values=phase['counters'];post=values['post8_clock_or_reserved']
        if events['working_set'] is False and any(values[name] for name in COUNTERS[:7]):
            raise ValueError('boot-only control has nonzero working-set counters')
        if mode==1:
            pv,pa=post>>16,post&65535
            if pv==65535 or pa==65535:
                raise ValueError('post8 unfinished, late, saturated or not restored')
            phase['post8']=dict(native_ticks=8,vints=pv,applied_acks=pa,restored_by_end=True)
            ticks=event['native_ticks']+8;vints=event['vints']+pv;acks=event['applied_acks']+pa
            phase['event_plus_post8']=dict(native_ticks=ticks,vints=vints,applied_acks=acks,
                applied_acks_per_native_tick=acks/ticks)
        elif post:
            raise ValueError('nonzero reserved working-set counter outside mode1')
    result.update(event_footer=events,caveat=__doc__.strip())
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs',nargs='+',type=Path)
    parser.add_argument('--signature',type=lambda value:int(value,0))
    parser.add_argument('--out',type=Path)
    args=parser.parse_args()
    result=[summarize(p,args.signature) if p.is_dir() else dict(path=str(p),**read(p)) for p in args.inputs]
    text=json.dumps(result,indent=2)+'\n'
    if args.out:args.out.write_text(text)
    else:print(text,end='')

if __name__=='__main__':main()
