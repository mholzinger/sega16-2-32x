#!/usr/bin/env python3
"""Actual native emitter body: conditional identity and FIFO backpressure."""
from pathlib import Path
import re,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'tools'))
from cport_entry_guards import apply_entry_guards
source=(root/'sh_src/portfull/altbeast_port.c').read_text()
body=re.search(r'^uint FUN_00002e50\(\)\n\{.*?^\}',source,re.M|re.S).group(0)+'\n'
guarded,count=apply_entry_guards(body,'FUN_00002e50:R60_WORKSET:port_sound_mcu_ready')
assert count==1 and body.split('{\n',1)[1] in guarded
assert apply_entry_guards(body,'')==(body,0)
for bad in ['missing:R60_WORKSET:ready','FUN_00002e50:BAD-X:ready','x:y','FUN_00002e50:X:y,FUN_00002e50:X:y']:
    try:apply_entry_guards(body,bad)
    except ValueError:pass
    else:raise AssertionError(bad)
prefix='''#include <stdint.h>
#include <assert.h>
#include "r60_comm_arbiter.h"
typedef unsigned int uint;typedef unsigned char undefined;
static uint8_t ram[0x4000],DAT_fffff03c,DAT_fffff0c4;
static short _DAT_fffff03e;
#define DAT_fffff040 ram[0x3040]
#define W68(x) (ram+(((uint32_t)(x)&65535u)-0xc000u))
#define W68PTR(x) W68(x)
#define A68OF(x) ((uint32_t)((x)-ram)+0xffffc000u)
unsigned port_sound_mcu_ready(void){return DAT_fffff0c4==255;}
'''
main='''
static r60_comm_state state;static unsigned speeches;
static void capture(void){if(DAT_fffff0c4!=255 && r60_comm_push(&state,DAT_fffff0c4)){speeches++;DAT_fffff0c4=255;}}
int main(void){
 r60_comm_init(&state,8);for(unsigned i=0;i<32;i++)assert(r60_comm_push(&state,(uint8_t)i));
 _DAT_fffff03e=(short)0xf040;DAT_fffff03c=1;ram[0x3040]=42;DAT_fffff0c4=255;
 FUN_00002e50();assert(DAT_fffff0c4==42 && DAT_fffff03c==0);capture();assert(DAT_fffff0c4==42 && speeches==0);
 DAT_fffff03c=1;ram[0x3041]=43;FUN_00002e50();assert(DAT_fffff0c4==42 && DAT_fffff03c==1);
 assert(r60_comm_audio_posted(&state,1));capture();assert(speeches==1 && DAT_fffff0c4==255);
 FUN_00002e50();assert(DAT_fffff0c4==43 && !DAT_fffff03c);capture();assert(speeches==1 && DAT_fffff0c4==43);
 /* Native explicit RESET keeps its deliberate queue/latch cancellation. */
 DAT_fffff03c=0;DAT_fffff0c4=0;FUN_00002e50();capture();assert(!DAT_fffff03c && !DAT_fffff0c4 && speeches==1);
 assert(r60_comm_audio_posted(&state,2));capture();assert(speeches==2 && DAT_fffff0c4==255);
 uint8_t value;unsigned tick=3;
 for(unsigned i=2;i<32;i++){assert(r60_comm_peek(&state,&value)&&value==i);assert(r60_comm_audio_posted(&state,tick++));}
 assert(r60_comm_peek(&state,&value)&&value==42);assert(r60_comm_audio_posted(&state,tick++));
 assert(r60_comm_peek(&state,&value)&&value==0);assert(r60_comm_audio_posted(&state,tick));assert(!r60_comm_depth(&state));
 return 0;}
'''
with tempfile.TemporaryDirectory(prefix='entry-guard-') as tmp:
    p=Path(tmp);a=p/'old.c';b=p/'new.c';exe=p/'check'
    a.write_text(prefix+body);b.write_text(prefix+guarded)
    includes=['-I'+str(root/'sh_src/r60')]
    old=subprocess.check_output(['clang','-E','-P',*includes,str(a)])
    new=subprocess.check_output(['clang','-E','-P',*includes,str(b)])
    assert old==new,'disabled entry guard changed preprocessed C'
    b.write_text(prefix+guarded+main)
    subprocess.run(['clang','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-DR60_WORKSET',*includes,str(b),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS actual2E50 guard: exactoffCPP, full latch retained, no repeat speech, acceptedFIFOorder, nativeRESET cancellation')
