#!/usr/bin/env python3
"""Execute actual palette queue/drain and active-shim sources with host MMIO.

Host regression only: no target build, emulator, ROM changes or timing claim.
The consume stub in shim tests has no packet; actual queue/immediate branches
are extracted separately. No CPU/DMA concurrency or electrical proof is made.
"""
from pathlib import Path
import re, shlex, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
def body(text,needle):
 start=text.index(needle);at=text.index('{',start)
 masked=re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"',lambda m:' '*len(m[0]),text,flags=re.S)
 level=1;end=at+1
 while level:
  level+=(masked[end]=='{')-(masked[end]=='}');end+=1
 return text[start:end]
def clean(text):return re.sub(r'\s+','',re.sub(r'/\*.*?\*/|//[^\n]*','',text,flags=re.S))
def host(text):
 text=text.replace('*(volatile uint16_t*)VDP_CTRL_PORT','control').replace('*vdp_ctrl_wide','command')
 text=re.sub(r'\(volatile uint16_t\*\)(0x[0-9A-Fa-f]+(?:uL|u)?)',r'mem16(\1)',text)
 text=text.replace('((uint32_t)r60_palette_hold)','((uint32_t)(uintptr_t)r60_palette_hold)').replace('((uint32_t)(sc + 688))','((uint32_t)(uintptr_t)(sc + 688))')
 return text
md=(ROOT/'md_src/md_main.c').read_text();shim=(ROOT/'md_src/r60_shim.h').read_text();fe=(ROOT/'md_src/frontend.c').read_text()
old_dir=ROOT/'build/codex-working-set/wsplay06/source'
# Baseline is optional for portable checkout use; the committed source-fragment
# assertions still exercise the historical absent-call counterexample below.
old_shim=(old_dir/'r60_shim.h').read_text() if (old_dir/'r60_shim.h').exists() else shim.replace('    r60_palette_drain();','')
helper=body(md,'static void r60_palette_drain(');reset=body(md,'void r60_palette_reset(')
queue=body(md[md.index('#ifdef PAL_FIRST',md.index('static void md_consume')):],'if (palp) {')
active=body(shim,'void shim_vblank(');old_active=body(old_shim,'void shim_vblank(')
assert 'r60_palette_drain();' not in old_active
assert active.index('r60_palette_drain();')<active.index('md_stage_play();')<active.index('if (*(volatile uint16_t*)0xA15100 & 0x8000)')
assert fe.index('frontend_menu_screen();')<fe.index('r60_palette_reset();')<fe.index('while (FE_COMM != 0)')
assert fe.index('r60_palette_reset();')<fe.index('#ifdef R60_TILEBATCH',fe.index('frontend_menu_screen();'))
if (old_dir/'md_main.c').exists():
 old_md=(old_dir/'md_main.c').read_text();legacy=body(old_md[old_md.index('if (r60_palette_pending)',old_md.index('#define shim_vblank shim_vblank_legacy')):],'if (r60_palette_pending)')
 assert clean(helper[helper.index('{')+1:-1])==clean(legacy),'Moved DMA sequence/gate must remain exact'
# Exact normal06 feature macros for the active shim. There are no includes in
# extracted function; stubs below model only unrelated work and no-packet input.
flags=shlex.split((ROOT/'build/codex-working-set/wsplay06/wsplay06.flags').read_text()) if (ROOT/'build/codex-working-set/wsplay06/wsplay06.flags').exists() else ['-DR60','-DR60_NEW','-DNT_WRAP','-DPAL_FIRST','-DMD_BG','-DR60_EMPTYPKT','-DR60_HOLDDIRECT','-DR60_TILEBATCH','-DR60_SPANPROGRESS','-DR60_IDLECONSUME','-DR60_FRONTEND','-DR60_LIVEPAD','-DR60_SOUND']
flags=flags[:flags.index('-m2')] if '-m2'in flags else flags
flags=[x for x in flags if x.startswith('-D')]
source=r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define C(x) do{if(!(x)){fprintf(stderr,"palette line %d\n",__LINE__);abort();}}while(0)
static uint16_t fb[0x10000],sys[0x100],wram[0x8000],vdp[8],comm[16];
static volatile uint16_t r60_palette_hold[64],r60_palette_pending;
static uint16_t packet[752],cram[64];static unsigned reg[32],dmas,stages,consumes;
static volatile uint8_t r60_shim_idle;static uint8_t r60_md_consuming,r60_tb_blank_resident;
static uint16_t *mars_comm0=comm,*mars_comm2=comm+1,*mars_comm8=comm+4,*mars_comm12=comm+6;
static uint16_t *mem16(uint32_t a){if(a>=0x840000&&a<0x860000)return fb+(a-0x840000)/2;if(a>=0xff0000)return wram+(a-0xff0000)/2;if(a>=0xa15100&&a<0xa15300)return sys+(a-0xa15100)/2;if(a>=0xc00000&&a<0xc00010)return vdp+(a-0xc00000)/2;abort();}
struct Control{void operator=(uint16_t v)const{C((v&0xc000)==0x8000);reg[(v>>8)&31]=v&255;}};static Control control;
struct Command{void operator=(uint32_t v)const{C(v==0xc0000080u);C(reg[15]==2);C((reg[19]|reg[20]<<8)==64);uint32_t a=((reg[21]|reg[22]<<8|reg[23]<<16)<<1)&0xffffffu;const volatile uint16_t *src=0;if(a==((uintptr_t)r60_palette_hold&0xffffffu))src=r60_palette_hold;else if(a==((uintptr_t)(packet+688)&0xffffffu))src=packet+688;C(src);for(unsigned i=0;i<64;i++)cram[i]=src[i];dmas++;}};static Command command;
static void md_stage_play(void){stages++;}static void md_consume(uint32_t a){(void)a;consumes++;}
static unsigned short read_joypad(unsigned n){(void)n;return 0;}static unsigned md_to_arcade(unsigned n){(void)n;return 255;}
static int frontend_input(unsigned short n){(void)n;return 0;}static void snd_vint(void){}
#define R60_SHIM_DONE 0xc000u
#define R60_PKTODD_68K 0x851e00u
@HELPER@
@RESET@
static void queue_source(void){volatile uint16_t *sc=packet;uint16_t palp=0x8000;@QUEUE@ C(!palp);}
@ACTIVE@
@OLD@
static void setup(unsigned beam,unsigned tag){memset(fb,0,sizeof(fb));memset(sys,0,sizeof(sys));memset(cram,0,sizeof(cram));memset(reg,0,sizeof(reg));dmas=stages=consumes=0;r60_palette_pending=0;vdp[4]=(uint16_t)(beam<<8);for(unsigned i=0;i<64;i++)packet[688+i]=(uint16_t)(tag+i*13);}
int main(){
 for(unsigned b=0;b<256;b++){
  setup(0,0x123);queue_source();C(r60_palette_pending&&dmas==0);vdp[4]=(uint16_t)(b<<8);sys[0]=0x8000;shim_vblank();
  if(b>=0xe0&&b<0xf8){C(!r60_palette_pending&&dmas==1);for(unsigned i=0;i<64;i++)C(cram[i]==packet[688+i]);shim_vblank();C(dmas==1);}else C(r60_palette_pending&&dmas==0);
 }
 setup(0,0x234);queue_source();vdp[4]=0xe000;sys[0]=0x8000;for(unsigned i=0;i<3;i++)old_shim_vblank();C(r60_palette_pending&&dmas==0);shim_vblank();C(!r60_palette_pending&&dmas==1&&consumes==0);
 setup(0,0x345);queue_source();for(unsigned i=0;i<64;i++)packet[688+i]=(uint16_t)(0x777+i);queue_source();vdp[4]=0xe100;shim_vblank();C(dmas==1&&!r60_palette_pending);for(unsigned i=0;i<64;i++)C(cram[i]==0x777+i);
 setup(0,0x456);queue_source();for(unsigned i=0;i<64;i++)packet[688+i]=(uint16_t)(0x888+i);vdp[4]=0xe200;queue_source();C(dmas==1&&!r60_palette_pending);shim_vblank();C(dmas==1);for(unsigned i=0;i<64;i++)C(cram[i]==0x888+i);
 setup(0,0x567);queue_source();r60_palette_reset();vdp[4]=0xe300;shim_vblank();C(!r60_palette_pending&&dmas==0);
 for(unsigned b=0;b<256;b++){setup(b,0x678);sys[0]=0x8000;shim_vblank();C(!dmas&&!r60_palette_pending);}
 puts("PASS palette pending: all256 beam values, exact64word DMA, no-packet/FM-up drain, stranded old shim, supersession, frontend-reset call/order, empty queue");
}
'''
source=source.replace('@HELPER@',host(helper)).replace('@RESET@',host(reset)).replace('@QUEUE@',host(queue)).replace('@ACTIVE@',host(active)).replace('@OLD@',host(old_active.replace('void shim_vblank(','void old_shim_vblank(')))
with tempfile.TemporaryDirectory(prefix='palette-pending-')as d:
 d=Path(d);cpp=d/'check.cpp';cpp.write_text(source);binary=d/'check'
 subprocess.run(['clang++','-std=c++11','-O1','-Wall','-Wextra','-Werror','-Wno-unused-variable','-Wno-unused-function','-fsanitize=address,undefined',*flags,str(cpp),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
 # Frontend reset remains active without TILEBATCH and disappears when this
 # queue feature is absent; preprocess the real service, not a mirrored test.
 service=body(fe,'FE_RAM void frontend_service(')
 for features,wanted in [(['-DR60','-DNT_WRAP','-DPAL_FIRST'],True),(['-DR60','-DNT_WRAP'],False)]:
  emitted=subprocess.check_output(['clang++','-E','-P','-x','c++',*features,'-'],input=service,text=True)
  assert ('r60_palette_reset();' in emitted)==wanted
 print('PASS frontend pending reset is independent of TILEBATCH and feature guarded')
