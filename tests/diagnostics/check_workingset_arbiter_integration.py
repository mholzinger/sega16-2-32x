#!/usr/bin/env python3
"""Compile actual main policy/service and actual MCU capture with host MMIO bindings.
This tests sequential scheduler order, not target memory-order or RV hardware.
"""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'sh_src/r60/r60_main.c').read_text()
def function(name):
    match=re.search(r'^[^\n]*\b'+name+r'\([^;\n]*\)\n\{',s,re.M)
    assert match,name
    start=match.start();brace=s.index('{',match.start());end=brace+1;depth=1
    while depth:
        if s[end]=='{':depth+=1
        elif s[end]=='}':depth-=1
        end+=1
    text=s[start:end]
    text=re.sub(r'__attribute__\(\(.*?\)\)\)', '',text)
    # Host alias binding only; execute the original helper body otherwise.
    return text.replace('(volatile uint16_t *)((uintptr_t)&r60_page_gen | 0x20000000u)','&r60_page_gen')
funcs='\n'.join(function(n) for n in ['r60_ws_reserve_page','r60_ws_commit_page','r60_ws_plan','r60_ws_audio_posted','r60_page_service','r60_ws_pre_ack'])
p=(root/'sh_src/port_l2.c').read_text();start=p.index('#if defined(R60_WORKSET) && defined(PORT_SOUND)\n        if (mcu[4]');end=p.index('#if defined(PORT_SOUND) && !defined(R60_WORKSET)\n        if (port_snd_r',start)
capture=p[start:end]
prefix='''#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "r60_workingset.h"
#include "r60_comm_arbiter.h"
#define R60_WORKSET 1
#define PORT_SOUND 1
#define PORT_SPEECH 1
static volatile r60_comm_state storage;
#define R60_COMM (&storage)
static volatile unsigned R60_COMM_IDLE_MAP;
static volatile uint16_t R60_PAGE_PENDING,R60_PAGE_CURRENT,r60_page_gen;
static volatile uint16_t R60_DONE,R60_GO,R60_DRAWSEQ,R60_SNAPSEQ,MARS_SYS_COMM10;
static unsigned r60_page_stat[4],R60_DIAG[12],r60_frame,wanted;
static unsigned r60_page_want(void){return wanted;}
static unsigned port_play_dbg[4],speeches;static unsigned char mcu[8];
void speech_cmd(unsigned cmd){(void)cmd;speeches++;}
'''
main='''
static void init(void){memset((void*)&storage,0,sizeof(storage));r60_comm_init(R60_COMM,8);R60_COMM_IDLE_MAP=0;R60_PAGE_CURRENT=8;R60_PAGE_PENDING=0;r60_page_gen=0;R60_DONE=R60_GO=R60_DRAWSEQ=R60_SNAPSEQ=0;MARS_SYS_COMM10=0;R60_DIAG[11]=1;r60_frame=1;wanted=8;memset(r60_page_stat,0,sizeof(r60_page_stat));speeches=0;mcu[4]=255;}
int main(void){
 init();for(unsigned i=0;i<32;i++)assert(r60_comm_push(R60_COMM,(uint8_t)i));
 mcu[4]=42;capture();assert(mcu[4]==42&&!speeches);assert(r60_comm_audio_posted(R60_COMM,0));capture();assert(mcu[4]==255&&speeches==1);capture();assert(speeches==1);
 init();wanted=7;assert(r60_comm_push(R60_COMM,33));MARS_SYS_COMM10=0x5a12;
 assert(r60_page_service(1)==1 && MARS_SYS_COMM10==0x5a12&&!R60_PAGE_PENDING);assert(r60_comm_depth(R60_COMM)==1);
 MARS_SYS_COMM10=0;assert(r60_page_service(0)==1&&MARS_SYS_COMM10==0x5be7&&R60_PAGE_PENDING==7);assert(r60_comm_depth(R60_COMM)==1);
 for(unsigned i=0;i<100;i++)assert(r60_page_service(0)==1);assert(r60_page_stat[1]==1);
 MARS_SYS_COMM10=0;r60_ws_pre_ack();assert(R60_PAGE_CURRENT==7&&r60_page_gen==1&&!R60_PAGE_PENDING);
 /* Actual latest-phase policy runs before the modeled observer. */
 assert(r60_page_service(1)==0&&MARS_SYS_COMM10==0x5a21&&!r60_comm_depth(R60_COMM));
 MARS_SYS_COMM10=0;assert(r60_comm_push(R60_COMM,34));assert(r60_page_service(1)==0&&!MARS_SYS_COMM10); /* one/native */
 R60_DIAG[11]++;assert(r60_page_service(0)==0&&!MARS_SYS_COMM10);assert(r60_comm_depth(R60_COMM)==1);assert(r60_page_service(1)==0&&MARS_SYS_COMM10==0x5a22);
 /* Exit can reserve before audio refill, despite previous wanted page7. */
 MARS_SYS_COMM10=0;wanted=8;R60_DIAG[11]++;assert(r60_page_service(1)==1&&MARS_SYS_COMM10==0x5be8);
 MARS_SYS_COMM10=0;r60_ws_pre_ack();assert(R60_PAGE_CURRENT==8&&!R60_PAGE_PENDING&&r60_page_gen==2);
 /* Reader busy: immediate finite fallback/hold, no mailbox overwrite. */
 init();wanted=7;R60_GO=1;assert(r60_page_service(1)==1&&!R60_PAGE_PENDING&&!MARS_SYS_COMM10);assert(r60_page_stat[2]==1);
 assert(R60_COMM_IDLE_MAP==1);R60_DONE=1;assert(r60_page_service(0)==1&&MARS_SYS_COMM10==0x5be7&&!R60_COMM_IDLE_MAP);
 /* No-progress AUDIO policy outcomes must not arm an idle cart poll. */
 init();wanted=7;for(unsigned i=0;i<25;i++)assert(r60_comm_push(R60_COMM,(uint8_t)i));
 MARS_SYS_COMM10=0x5a12;assert(r60_page_service(1)==0&&!R60_COMM_IDLE_MAP&&!R60_PAGE_PENDING);
 init();wanted=7;assert(r60_comm_push(R60_COMM,33));storage.fair_audio_owed=1;
 assert(r60_page_service(0)==1&&!R60_COMM_IDLE_MAP&&!MARS_SYS_COMM10);
 puts("PASS actual main policy/MMIO + MCU capture: full backpressure/speech-once, map priority, ACK-before-observer, latest-phase exit, no idle audio, one post/native, reader drain, dedup waits");return 0;}
'''
# Bind ordering to actual source, rather than claiming extracted whole-main execution.
loop=s[s.index('        r60_ws_pre_ack();'):]
assert loop.index('r60_ws_pre_ack();')<loop.index('port_play_tick();')<loop.index('r60_page_service(1u)')<loop.index('r60_event_master_tick();')
prelude=loop[:loop.index('#endif')]
# Execute the actual no-native-tick prelude (frontend pause/due0).
no_tick='\nstatic unsigned no_tick_hold(void){\n'+prelude+'return r60_page_hold;}'
main=main.replace('int main(void){','''int main(void){
 init();R60_PAGE_PENDING=7;MARS_SYS_COMM10=0x5be7;
 assert(no_tick_hold()==1 && R60_PAGE_PENDING==7 && R60_PAGE_CURRENT==8);
 MARS_SYS_COMM10=0;storage.wanted=7;assert(no_tick_hold()==0 && !R60_PAGE_PENDING && R60_PAGE_CURRENT==7);
''')
with tempfile.TemporaryDirectory(prefix='ws-arbiter-integration-') as d:
    p=Path(d);src=p/'check.c';exe=p/'check';src.write_text(prefix+funcs+'\nstatic void capture(void){\n'+capture+'}\n'+no_tick+main)
    subprocess.run(['clang','-std=c99','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(root/'sh_src/r60'),str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
