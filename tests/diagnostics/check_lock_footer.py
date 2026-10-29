#!/usr/bin/env python3
"""Execute the real footer against guarded buffers; game RAM must be unchanged."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
code=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#define FB_W 320u
#define R60_BAR_PEN 255u
static unsigned R60_DISPHOLD, R60_HOLDFLIP;
static uint32_t fbguard[320*224/4+8];
static uint8_t work[0x4000],before[0x4000];
static uint32_t diag[32];
#define R60_DIAG diag
#define R60_LOCKPROBE_RAM work
#define FB_DIR(off) (fbguard+4+(off)/4)
#define FB_DIRB(off) ((uint8_t*)(fbguard+4)+(off))
#include "sh_src/r60/r60_lockprobe.h"
static void be16(unsigned a,unsigned v) {work[a]=v>>8;work[a+1]=v;}
static unsigned glyph(unsigned x,unsigned y)
{
 unsigned bits=0,r,k; uint8_t *fb=(uint8_t*)(fbguard+4);
 for(r=0;r<5;r++)for(k=0;k<3;k++)bits=(bits<<1)|(fb[(y+r*2)*320+x+k*2]==255);
 return bits;
}
static void verify(void)
{
 unsigned i;
 memcpy(before,work,sizeof(work));memset(fbguard,0x5a,sizeof(fbguard));
 r60_lockprobe_draw();
 assert(!memcmp(before,work,sizeof(work)));
 for(i=0;i<4;i++) { assert(fbguard[i]==0x5a5a5a5a);assert(fbguard[4+320*224/4+i]==0x5a5a5a5a); }
 for(i=4;i<4+196*320/4;i++)assert(fbguard[i]==0x5a5a5a5a);
}
int main(void)
{
 /* Display-hold ownership: no framebuffer or game writes. */
 memset(fbguard,0x5a,sizeof(fbguard));memcpy(before,work,sizeof(work));
 R60_DISPHOLD=1;r60_lockprobe_draw();
 for(unsigned i=0;i<sizeof(fbguard)/sizeof(fbguard[0]);i++)assert(fbguard[i]==0x5a5a5a5a);
 assert(!memcmp(work,before,sizeof(work)));
 R60_DISPHOLD=0;R60_HOLDFLIP=1;r60_lockprobe_draw();
 for(unsigned i=0;i<sizeof(fbguard)/sizeof(fbguard[0]);i++)assert(fbguard[i]==0x5a5a5a5a);
 R60_HOLDFLIP=0;
 /* Empty ordinary table: show boss record40 (hex28), still in bounds. */
 verify();assert(glyph(72,211)==r60_lp_font[2]);assert(glyph(80,211)==r60_lp_font[8]);
 /* One invisible actor and one visible actor: select the invisible one. */
 work[16*128]=0x80;work[17*128]=0x88;
 work[16*128+3]=0;work[16*128+4]=0xee;work[16*128+5]=0x80;
 be16(16*128+12,0x1300);be16(16*128+16,0x10d6);
 be16(0x3132,0x12e8);be16(0x313c,0x15d0);work[0x3154]=2;
 diag[0]=360;
 verify();assert(glyph(72,211)==r60_lp_font[1]);assert(glyph(80,211)==r60_lp_font[0]);
 assert(glyph(12,197)==r60_lp_font[1]);assert(glyph(20,197)==r60_lp_font[2]);
 assert(glyph(28,197)==r60_lp_font[14]);assert(glyph(36,197)==r60_lp_font[8]);
 assert(glyph(108,197)==r60_lp_font[2]);
 /* With both visible, every120vints cycles over the live records. */
 work[16*128]=0x88;diag[0]=120;verify();
 assert(glyph(72,211)==r60_lp_font[1]);assert(glyph(80,211)==r60_lp_font[1]);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='codex-lock-footer-') as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c99','-I',str(root),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: footer is read-only, stays inside framebuffer bounds, decodes fields, selects/cycles actors')
