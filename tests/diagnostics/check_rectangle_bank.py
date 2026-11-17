#!/usr/bin/env python3
"""Execute actual S rectangle-clear fragments with host framebuffer/MMIO proxies.

Covers physical bank versus logical GO parity, including held-flip sequences.
This tests metadata ownership and the non-ROWCLR fallback; it does not assert
that active ROWCLR pixels changed or emulate VDP timing/concurrency.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def fragments(text):
    reset = re.search(r'^\s*R60_RECTN\((?:go|bkp)\) = 0; R60_MRECTN\((?:go|bkp)\) = 0;[^\n]*', text, re.M)[0]
    start = text.index('            for (li = 0; li < nl; li++) {', text.index(reset))
    end = text.index('#endif /* R60_ROWCLR */', start)
    return reset, text[start:end]


current = (ROOT / 'sh_src/r60/r60_main.c').read_text()
reset, fallback = fragments(current)
old_path = ROOT / 'build/codex-working-set/wsbc01/source/r60_main.c'
if old_path.exists():
    old_reset, old_fallback = fragments(old_path.read_text())
    old_source = str(old_path.relative_to(ROOT))
else:
    # Portable checkout counterexample: the historical eight arguments, with
    # all unrelated source statements kept from the extracted current body.
    old_reset = reset.replace('(bkp)', '(go)')
    old_fallback = re.sub(r'(R60_(?:M?RECTS|M?RECTN))\(bkp\)', r'\1(go)', fallback)
    old_source = 'historical eight-argument counterexample reconstructed from current fragments'
# Freeze the intended narrow repair, rather than allowing the negative oracle
# to differ in unrelated rendering statements.
normalize = lambda s: re.sub(r'\s+', '', s)
assert normalize(old_reset.replace('(go)', '(bkp)')) == normalize(reset)
assert normalize(re.sub(r'(R60_(?:M?RECTS|M?RECTN))\(go\)', r'\1(bkp)', old_fallback)) == normalize(fallback)
assert len(re.findall(r'R60_(?:M?RECTS|M?RECTN)\(go\)', old_reset + old_fallback)) == 8

# Use the actual four macro definitions; canonicalize their target pointers to
# native host pointers and route the two count MMIO addresses to checked RAM.
macros = []
for name in ('RECTS', 'MRECTS', 'RECTN', 'MRECTN'):
    line = re.search(r'^#define R60_' + name + r'\(bk\)[^\n]*', current, re.M)[0]
    line = line.replace('(uint32_t)r60_rects_mem', '(uintptr_t)r60_rects_mem').replace(' | 0x20000000u', '')
    line = re.sub(r'\*\(volatile uint16_t \*\)\((0x26028F(?:62|66)u \+ \(\(bk\) & 1u\) \* 2u)\)', r'*mmio16(\1)', line)
    macros.append(line)

source = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define C(x) do{if(!(x)){fprintf(stderr,"rectangle bank line %d\n",__LINE__);abort();}}while(0)
#define FB_W 320u
static uint16_t r60_rects_mem[2][2][64][4], counts[4];
static uint32_t fb[2][(320*224+4)/4];
static unsigned physical, R60_DISPHOLD, r60_fill_rows[2], r60_xo_next;
static uint32_t r60_fill_anchor[2]; static uint8_t txt_pend[2][1120];
static unsigned accesses[4], fill_calls;
static volatile uint16_t *mmio16(uint32_t a){C(a>=0x26028f62u&&a<0x26028f6au&&!(a&1));unsigned i=(a-0x26028f62u)/2;accesses[i]++;return counts+i;}
@MACROS@
static volatile uint32_t *direct(unsigned a){C(!(a&3)&&a<sizeof(fb[0]));return fb[physical]+a/4;}
#define FB_DIR(a) direct(a)
#define FB_DIRB(a) ((uint8_t*)direct(a))
#define FB_OVR(a) ((uint8_t*)direct(a))
static void r60_fill_cells(uint8_t*a,uint8_t*b,unsigned c,unsigned d,unsigned e,unsigned f,unsigned g){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;fill_calls++;}
static void row(unsigned go,unsigned bkp){(void)go;(void)bkp;@RESET@}
static void old_row(unsigned go,unsigned bkp){(void)go;(void)bkp;@OLD_RESET@}
static void boxes(unsigned go,unsigned bkp,unsigned nl){(void)go;unsigned li;@FALLBACK@}
static void old_boxes(unsigned go,unsigned bkp,unsigned nl){(void)go;unsigned li;@OLD_FALLBACK@}
static void setup(void){
 memset(r60_rects_mem,0,sizeof(r60_rects_mem));memset(accesses,0,sizeof(accesses));fill_calls=0;R60_DISPHOLD=0;
 for(unsigned l=0;l<2;l++)for(unsigned b=0;b<2;b++){
  counts[l*2+b]=(uint16_t)(1+l+b);
  for(unsigned i=0;i<counts[l*2+b];i++){
   unsigned x=8+64*b+24*l+8*i,y=4+16*b+8*l+3*i;
   r60_rects_mem[l][b][i][0]=(uint16_t)(x+1);r60_rects_mem[l][b][i][1]=(uint16_t)(x+5);
   r60_rects_mem[l][b][i][2]=(uint16_t)y;r60_rects_mem[l][b][i][3]=(uint16_t)(y+2);
  }
 }
 for(unsigned b=0;b<2;b++)for(unsigned i=0;i<sizeof(fb[0])/4;i++)fb[b][i]=0x12340000u+b*0x10000u+i;
}
/* Independent scalar expected raster; physical bank must select both old boxes
 * and old count. Distinct counts/coordinates expose wrong pointer/count reads. */
static unsigned check(unsigned go,unsigned bkp,unsigned nl,unsigned old,unsigned isrow){
 uint16_t prior[4], records[2][2][64][4];uint32_t expected[2][(320*224+4)/4];setup();physical=bkp;
 memcpy(prior,counts,sizeof(prior));memcpy(records,r60_rects_mem,sizeof(records));memcpy(expected,fb,sizeof(expected));
 if(!isrow)for(unsigned l=0;l<nl;l++)for(unsigned i=0;i<prior[l*2+bkp];i++){
  const uint16_t*r=records[l][bkp][i];unsigned lo=r[0]&~3u,hi=(r[1]+4u)&~3u;
  for(unsigned y=r[2];y<r[3];y++)for(unsigned x=lo;x<hi;x+=4)expected[bkp][(y*FB_W+x)/4]=0;
 }
 if(isrow){if(old)old_row(go,bkp);else row(go,bkp);}else{if(old)old_boxes(go,bkp,nl);else boxes(go,bkp,nl);}
 unsigned result=0,okay=1;
 for(unsigned l=0;l<2;l++)for(unsigned b=0;b<2;b++){
  unsigned want=(b==bkp&&(isrow||l<nl))?0:prior[l*2+b];if(counts[l*2+b]!=want)okay=0;
 }
 if(okay)result|=1;
 if(!memcmp(expected,fb,sizeof(expected)))result|=2;
 C(!memcmp(records,r60_rects_mem,sizeof(records)));C(fill_calls==0);
 if(!old){for(unsigned l=0;l<2;l++){C(!accesses[l*2+(bkp^1)]);if(isrow||l<nl)C(accesses[l*2+bkp]);}}
 return result;
}
int main(void){unsigned cases=0,negatives=0;
 for(unsigned g=0;g<2;g++)for(unsigned b=0;b<2;b++){
  C(check(g,b,2,0,1)==3);cases++;
  unsigned old=check(g,b,2,1,1);C((old==3)==(g==b));if(g!=b){C(old==2);negatives++;}
  for(unsigned nl=1;nl<=2;nl++){
   C(check(g,b,nl,0,0)==3);cases++;
   old=check(g,b,nl,1,0);C((old==3)==(g==b));if(g!=b){C(old==0);negatives++;}
  }
 }
 /* Physical bank advances only when a flip is issued; GO always advances.
  * Includes DONE2503/bank0 then GO2504/bank1, repeated held passes, and wrap. */
 setup();counts[0]=22;counts[1]=7;counts[2]=11;counts[3]=9;
 row(2504,1);C(counts[0]==22&&counts[2]==11&&counts[1]==0&&counts[3]==0);cases++;
 setup();counts[0]=22;counts[1]=7;counts[2]=11;counts[3]=9;
 old_row(2504,1);C(counts[0]==0&&counts[2]==0&&counts[1]==7&&counts[3]==9);negatives++;
 unsigned go=2503,b=0;const unsigned flip[]={1,0,0,1,0,1,1,0};
 for(unsigned i=0;i<sizeof(flip)/sizeof(flip[0]);i++){
  C(check(go,b,2,0,1)==3);C(check(go,b,2,0,0)==3);cases+=2;
  b^=flip[i];go=(go+1)&65535u;
 }
 for(go=65534;go<65538;go++){C(check(go&65535u,1,2,0,1)==3);C(check(go&65535u,1,2,0,0)==3);cases+=2;}
 printf("PASS rectangle bank: %u actual-source cases, %u old-fragment mismatch failures; all four GO/bank parities, slave/master lists, HOLDFLIP/wrap, completed-bank preservation\n",cases,negatives);
}
'''
for key, value in {'MACROS':'\n'.join(macros), 'RESET':reset, 'OLD_RESET':old_reset, 'FALLBACK':fallback, 'OLD_FALLBACK':old_fallback}.items():
    source = source.replace('@' + key + '@', value)
with tempfile.TemporaryDirectory(prefix='rectangle-bank-') as d:
    d = Path(d)
    c = d / 'check.c'
    c.write_text(source)
    binary = d / 'check'
    subprocess.run(['clang', '-std=c99', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-fsanitize=address,undefined', str(c), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Historical negative-control input:', old_source)
