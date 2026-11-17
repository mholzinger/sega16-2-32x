#!/usr/bin/env python3
"""Run the actual bake/raw dispatch and common metadata tail on host fixtures.

Backends are deterministic stubs, not replacements for the native pixel oracle.
The test checks routing, side effects and retained metadata; it makes no timing
or cross-core memory-order claim.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT/'sh_src/r60/r60_main.c').read_text()
slave = source[source.index('R60CODE void s_main(void)'):]
binding = 'r60_ws_active = R60_PAGE_CURRENT == R60_WS_PAGE;'
assert slave.index('r60_ws_claim(') < slave.index('R60_PAGE_SLAVE();') < slave.index(binding)
assert slave.index('frontend_slave_pause();continue;') < slave.index(binding)
start = slave.index('                    uint16_t t0_ = r60_frt();')
end = slave.index('                    else r60_bk_stat[5] +=', start)
end = slave.index('\n', end)
dispatch = slave[start:end]
start = slave.index('#ifdef R60_PRIO\n                /* the box punch', end)
end = slave.index('                drew++;', start) + len('                drew++;')
metadata = slave[start:end]

prefix = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#define R60_PRIO
#define R60_KEEPSPR
#define FB_H 224u
#define PHT_B() ((void)0)
#define PHT_E(k) ((void)0)
#define span0 0u
typedef struct {
 unsigned rowpix, bk_stat[8], requests, fb[16], spans[4], rects[4];
 uint16_t last_box[4], box[2][64][4], pyr[2][64];
 uint8_t punched[2][64];
 unsigned stats[4], touched, drew;
} State;
static State state;
static unsigned physical_page, page_reads, bk_calls, raw_calls, fixture_hit, clock_tick;
static unsigned prio_ymin, prio_ymax;
#define r60_rowpix state.rowpix
#define r60_bk_stat state.bk_stat
#define r60_last_box state.last_box
#define ks_box state.box
#define ks_pyr state.pyr
#define ks_punched state.punched
#define ks_stats state.stats
#define drew state.drew
#if TEST_STATS
#define R60_ZB_STAT(k,v) (state.requests += (v))
#else
#define R60_ZB_STAT(k,v) ((void)0)
#endif
static unsigned read_page(void){page_reads++;return physical_page;}
#define R60_PAGE_CURRENT read_page()
#define R60_WS_PAGE 7u
static uint16_t r60_frt(void){return (uint16_t)++clock_tick;}
static void emit(const volatile uint16_t *e,unsigned bkp,unsigned i){
 unsigned seed=e[3];
 for(unsigned j=0;j<16;j++)state.fb[j]=(seed*17u+j*13u)^bkp;
 for(unsigned j=0;j<4;j++){state.spans[j]=(i<<16)|(seed+j);state.rects[j]=seed+j*2u;}
 r60_last_box[0]=(uint16_t)(seed&31u);r60_last_box[1]=(uint16_t)((seed&31u)+16u);
 r60_last_box[2]=83;r60_last_box[3]=87;r60_rowpix+=16;
}
static unsigned r60_bk_sprite(const volatile uint16_t *e,unsigned bkp,unsigned i){
 bk_calls++;
 if(e[5]&0x3ffu){R60_ZB_STAT(0,1u);if(physical_page!=7u)return 0;}
 if(!fixture_hit)return 0;
 emit(e,bkp,i);return 1;
}
static void raw_sprite(const volatile uint16_t *e,unsigned lo,unsigned hi,unsigned bkp,unsigned i){
 assert(lo==0&&hi==224);raw_calls++;emit(e,bkp,i);
}
#define R60_SPRITE_S(e,lo,hi) raw_sprite(e,lo,hi,bkp,i)
static unsigned punch(const uint16_t *box){prio_ymin=box[2];prio_ymax=box[3];return box[0]&1u;}
#define R60_PUNCH(box) punch(box)
static void r60_text_touched(const uint16_t *box){state.touched+=box[1]-box[0];}
'''
functions = '\nstatic unsigned bind_pass(void){unsigned r60_ws_active;'+binding+'return r60_ws_active;}\n'
functions += '#define R60_WORKSET\nstatic void current(const volatile uint16_t *e,unsigned bkp,unsigned i,unsigned r60_ws_active){\n'+dispatch+'\n'+metadata+'\n}\n'
functions += '#undef R60_WORKSET\nstatic void previous(const volatile uint16_t *e,unsigned bkp,unsigned i,unsigned r60_ws_active){\n'+dispatch+'\n'+metadata+'\n}\n'
main = r'''
int main(void){
 unsigned cases=0;
 for(unsigned page=6;page<=8;page++)for(unsigned zoom=0;zoom<1024;zoom++)
 for(unsigned hit=0;hit<2;hit++)for(unsigned bank=0;bank<2;bank++){
  volatile uint16_t e[8]={0};e[3]=(uint16_t)(zoom+hit+1u);e[5]=(uint16_t)zoom;
  physical_page=page;fixture_hit=hit;page_reads=0;
  unsigned active=bind_pass();assert(page_reads==1);
  memset(&state,0,sizeof state);clock_tick=bk_calls=raw_calls=0;
  previous(e,bank,13,active);State expected=state;
  unsigned expected_raw=raw_calls;assert(bk_calls==1);
  memset(&state,0,sizeof state);clock_tick=bk_calls=raw_calls=0;
  current(e,bank,13,active);
  assert(!memcmp(&state,&expected,sizeof state));
  assert(raw_calls==expected_raw);
  assert(bk_calls==((page!=7u&&zoom)?0u:1u));
  assert(page_reads==1); /* No per-sprite committed-page reads. */
  assert(drew==1&&state.stats[1]==1&&state.touched==16);
  assert(!memcmp(state.box[bank][13],state.last_box,sizeof state.last_box));
  cases++;
 }
 printf("PASS %u actual-source dispatch cases: all10-bit zooms, active/off-page, bake hit/miss, bothbanks; raw output and common keep/punch/box/rect/span/text/rowpix metadata retained (stats=%d)\n",cases,TEST_STATS);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='ws-dispatch-') as directory:
    path = Path(directory)
    code = path/'check.c'
    code.write_text(prefix+functions+main)
    for stats in (0, 1):
        exe = path/f'check{stats}'
        subprocess.run(['clang', '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-parameter', '-fsanitize=address,undefined',
                        '-fno-sanitize-recover=all', f'-DTEST_STATS={stats}',
                        str(code), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
