/* Independent file-fixture oracle: Python computes native word/scalar results;
 * this runner executes the actual generic clipping header under sanitizers. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if(!(c)){fprintf(stderr,"case%u line%d\n",case_no,__LINE__);abort();} }while(0)
static unsigned case_no, store_count;
static uint8_t *fb;
static unsigned fb_length;
static void overwrite(volatile uint8_t *p,uint32_t value)
{
 CHECK(!((uintptr_t)p&3u) && p>=fb && p+4<=fb+fb_length);
 for(unsigned i=0;i<4;i++){uint8_t v=(uint8_t)(value>>(24-8*i));if(v)p[i]=v;}
 store_count++;
}
#define R60_WC_STORE(p,v) overwrite((p),(v))
#include "../../sh_src/r60/r60_workingset_clip.h"
/* This include is extracted verbatim from CURRENT main.c by the Python driver,
 * rather than reimplementing the post-copy rectangle publication under test. */
enum { FB_W=320 };
static unsigned r60_bk_stat[8],r60_xo,r60_rowpix;
static uint32_t r60_rowspans[2][6];
static uint16_t r60_rowspan_n[2],r60_rowspan_full[2],r60_last_box[4];
static uint16_t rects[2][65*4],rect_n[2];
#define R60_RECTS(bank) rects[(bank)&1u]
#define R60_RECTN(bank) rect_n[(bank)&1u]
static void postcopy(unsigned bank,unsigned bk,unsigned nrows,int xmax,
    unsigned bx0,unsigned bx1,unsigned by0,unsigned by1,uint32_t *spw,
    unsigned sp_full,unsigned npx5)
{
#include "r60_clip_postcopy.inc"
}
static uint32_t u32(FILE *f)
{
 uint8_t b[4];CHECK(fread(b,1,4,f)==4);
 return (uint32_t)b[0]<<24|(uint32_t)b[1]<<16|(uint32_t)b[2]<<8|b[3];
}
int main(int argc,char **argv)
{
 CHECK(argc==2);FILE *f=fopen(argv[1],"rb");CHECK(f);
 CHECK(u32(f)==0x57434f31);unsigned count=u32(f);unsigned long long rows=0;
 for(case_no=0;case_no<count;case_no++) {
  unsigned nw=u32(f),nr=u32(f),x=u32(f),xo=u32(f),bank=u32(f),room=u32(f);
  unsigned y=u32(f),record=u32(f),expected_px=u32(f),expected_max=u32(f);
  unsigned expected_spans=u32(f),expected_full=u32(f),length=u32(f),hz=u32(f);
  CHECK(nw && nr<=4 && room<=4 && expected_spans<=room && length==nr*320+16);
  uint32_t *source=malloc(nw*4),*before=malloc(nw*4);CHECK(source && before);
  for(unsigned i=0;i<nw;i++)source[i]=u32(f);memcpy(before,source,nw*4);
  uint8_t *allocation=malloc(length+32),*expected=malloc(length);CHECK(allocation && expected);
  memset(allocation,0xa7,length+32);fb=allocation+16;fb_length=length;
  CHECK(fread(fb,1,length,f)==length && fread(expected,1,length,f)==length);
  unsigned bk=case_no&1u;uint32_t *span=r60_rowspans[bk],want[4];
  for(unsigned b=0;b<2;b++)for(unsigned i=0;i<6;i++)r60_rowspans[b][i]=0xa59b2468;
  for(unsigned i=0;i<expected_spans;i++)want[i]=u32(f);
  uintptr_t a[16]={0};a[0]=(uintptr_t)source;a[1]=(uintptr_t)(fb+(x&~3u));
  a[2]=nr;a[3]=bank*16u*0x01010101u;a[4]=(320+xo-x+3)/4;
  a[5]=x;a[6]=x&~3u;a[7]=320+xo;a[8]=(uintptr_t)(span+1);
  a[9]=(uintptr_t)(span+1+room);a[10]=(record<<16)|((x&~3u)/4<<8);
  a[11]=y;a[12]=x&3u;a[13]=832;store_count=0;
  r60_wc_rows(a,hz);
  CHECK(!memcmp(fb,expected,length));CHECK(a[14]==expected_px && a[15]==expected_max);
  CHECK(a[2]==expected_full && a[8]==(uintptr_t)(span+1+expected_spans));
  CHECK(!memcmp(span+1,want,expected_spans*4));
  for(unsigned i=0;i<16;i++)CHECK(allocation[i]==0xa7 && allocation[16+length+i]==0xa7);
  CHECK(span[0]==0xa59b2468);for(unsigned i=expected_spans+1;i<6;i++)CHECK(span[i]==0xa59b2468);
  CHECK(!memcmp(source,before,nw*4));if(hz)CHECK(store_count*4==expected_px);
  /* Execute the actual caller tail, then compare independently native-derived
   * rectangle/box geometry and physical-bank association, including a full list. */
  memset(r60_bk_stat,0,sizeof(r60_bk_stat));r60_bk_stat[3]=17;r60_rowpix=23;r60_xo=xo;
  for(unsigned b=0;b<2;b++){
   r60_rowspan_n[b]=9;r60_rowspan_full[b]=0;
   rect_n[b]=2;for(unsigned i=0;i<65*4;i++)rects[b][i]=0xbeef;
  }
  unsigned old_rect_n=(case_no%3==0)?0:(case_no%3==1)?63:64;
  rect_n[bk]=(uint16_t)old_rect_n;
  postcopy(bk,bk,nr,(int)a[15],x,0,y,y+nr,(uint32_t *)a[8],(unsigned)a[2],(unsigned)a[14]);
  CHECK(r60_bk_stat[3]==17+nr && r60_rowpix==23+expected_px);
  CHECK(r60_rowspan_n[bk]==1+expected_spans && r60_rowspan_full[bk]==expected_full);
  CHECK(r60_rowspan_n[bk^1u]==9 && !r60_rowspan_full[bk^1u]);
  uint16_t box[4]={(uint16_t)x,(uint16_t)(expected_max<320+xo?expected_max:320+xo),
                   (uint16_t)y,(uint16_t)(y+nr)};
  unsigned has_box=nr && box[1]>box[0];if(!has_box)memset(box,0,sizeof(box));
  CHECK(!memcmp(r60_last_box,box,sizeof(box)));
  CHECK(rect_n[bk]==old_rect_n+(has_box && old_rect_n<64));CHECK(rect_n[bk^1u]==2);
  for(unsigned b=0;b<2;b++)for(unsigned i=0;i<65*4;i++){
   uint16_t want_rect=(b==bk && has_box && old_rect_n<64 && i/4==old_rect_n)?box[i%4]:0xbeef;
   CHECK(rects[b][i]==want_rect);
  }
  rows+=nr;free(source);free(before);free(allocation);free(expected);
 }
 CHECK(fgetc(f)==EOF);fclose(f);
 printf("PASS independent native-word oracle %u cases/%llu rows: backing bytes, ends, spans/full, npx, actual main box/rect tail and guards\n",count,rows);
 return 0;
}
