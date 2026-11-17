/* Actual typed admission helpers, with a test-only ROM load observer proving
 * repeated validated hits do not walk raw row headers. Source is immutable
 * within a generation; PAGE_SLAVE's existing64-token purge owns epoch changes. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d\n",__LINE__);abort();}}while(0)
static uint32_t read_rom(const volatile uint32_t *,unsigned);
#define R60_WS_ROM_LOAD(ix,i) read_rom(ix,i)
#define R60_WORKSET_CACHE 1
#include "../../sh_src/r60/r60_workingset.h"
static const volatile uint32_t *observed;
static unsigned observed_words,loads,rowloads;
static uint8_t *is_row;
static uint32_t read_rom(const volatile uint32_t *ix,unsigned i)
{
 CHECK(ix==observed && i<observed_words);loads++;if(is_row[i])rowloads++;return ix[i];
}
static void observe(uint32_t *ix,unsigned words,unsigned atlas)
{
 free(is_row);is_row=calloc(words,1);CHECK(is_row);observed=ix;observed_words=words;loads=rowloads=0;
 if(atlas)for(unsigned i=0;i<ix[1];i++) {
  unsigned a=ix[6u+3u*i]>>2;CHECK(a<words);unsigned h=ix[a]>>16;
  for(unsigned r=0;r<h;r++){CHECK(++a<words);is_row[a]=1;a+=(ix[a]>>16)&255u;CHECK(a<words);}
 }
}
static uint32_t small[]={0x5a423100,3,88,0, 0x12345678,17,52, 0x12345678,18,64, 0x12345679,17,76,
 0x00010004,0x0001fffc,0x01020304, 0x00010004,0x0001fffc,0x09080706, 0x00010004,0x0001fffc,0x05060708};
static void small_cases(void)
{
 uint32_t a[22];memcpy(a,small,sizeof(a));observe(a,22,1);
 uint16_t tokens[64]={0};CHECK(sizeof(tokens)==128);
 const volatile uint32_t *p=r60_ws_zb_find_cached(7,a,sizeof(a),0x12345678,17,tokens);
 CHECK(p==a+13 && tokens[0]==0x8001 && rowloads==1);
 rowloads=0;CHECK(r60_ws_zb_find_cached(7,a,sizeof(a),0x12345678,17,tokens)==p && !rowloads);
 rowloads=0;CHECK(r60_ws_zb_find(7,a,sizeof(a),0x12345678,17)==p && rowloads==1);
 rowloads=0;CHECK(r60_ws_zb_find_cached(7,a,sizeof(a),0x12345678,18,tokens)==a+16 && tokens[0]==0x8002 && rowloads==1);
 rowloads=0;CHECK(r60_ws_zb_find_cached(7,a,sizeof(a),0x12345679,17,tokens)==a+19 && tokens[0]==0x8003 && rowloads==1);
 for(unsigned i=0;i<6;i++) {
  uint16_t initial[]={0,0x8000,0xbfff,0xc001,1,0x7fff};tokens[0]=initial[i];rowloads=0;
  CHECK(r60_ws_zb_find_cached(7,a,sizeof(a),0x12345678,17,tokens)==a+13 && rowloads==1 && tokens[0]==0x8001);
 }
 unsigned before=loads;CHECK(!r60_ws_zb_find_cached(8,(void *)(uintptr_t)1,88,0x12345678,17,(void *)(uintptr_t)1) && loads==before);
 tokens[0]=0;CHECK(r60_ws_zb_find_cached(7,a,sizeof(a),0x12345678,17,tokens));
 /* Same physical address/key/zoom after a new generation: existing PAGE_SLAVE
  * purges all tokens, forcing validation of the new packet body. */
 memset(tokens,0,sizeof(tokens));a[14]=0x0002fffc;rowloads=0;
 CHECK(!r60_ws_zb_find_cached(7,a,sizeof(a),0x12345678,17,tokens) && tokens[0]==0 && rowloads==1);
 memcpy(a,small,sizeof(a));a[15]=0x0e0d0c0b;rowloads=0;
 CHECK(r60_ws_zb_find_cached(7,a,sizeof(a),0x12345678,17,tokens)==a+13 && a[15]==0x0e0d0c0b && rowloads==1);
 /* Defensive metadata/range checks remain on positive hits. */
 a[6]=89;CHECK(!r60_ws_zb_find_cached(7,a,sizeof(a),0x12345678,17,tokens));a[6]=52;
 a[4]=0x12345677;CHECK(!r60_ws_zb_find_cached(7,a,sizeof(a),0x12345678,17,tokens));a[4]=0x12345678;
 a[5]=19;CHECK(!r60_ws_zb_find_cached(7,a,sizeof(a),0x12345678,17,tokens));a[5]=17;
 a[2]=92;CHECK(!r60_ws_zb_find_cached(7,a,sizeof(a),0x12345678,17,tokens));a[2]=88;
 a[0]=0x5a503100;CHECK(!r60_ws_zb_find_cached(7,a,sizeof(a),0x12345678,17,tokens));a[0]=0x5a423100;
 /* Genuine malformed first admissions never publish an index token. */
 const unsigned changes[][2]={{0,0x5a503100},{1,0xffffffff},{2,92},{2,12},{3,1},{6,16},{6,53},{6,88},{13,0x10000000},{13,0x00010141},{14,0x0002fffc},{14,0x0101fffc},{14,0x00010004},{14,0x0001fff8}};
 for(unsigned i=0;i<sizeof(changes)/sizeof(*changes);i++) {
  memcpy(a,small,sizeof(a));a[changes[i][0]]=changes[i][1];tokens[0]=0;
  CHECK(!r60_ws_zb_find_cached(7,a,sizeof(a),0x12345678,17,tokens) && !tokens[0]);
 }
}
static void tags(void)
{
 uint32_t a[10]={0x41423100,2,0,0,0x12345678,44,0x12345679,56};observe(a,10,0);
 CHECK(r60_ws_ab_recheck(a,1,0x12345678));CHECK(!r60_ws_ab_recheck(a,2,0x12345678));
 for(unsigned t=0x8000;t<0x10000;t++){unsigned n=loads;CHECK(!r60_ws_ab_recheck(a,t,0x12345678) && loads==n);}
 unsigned n=loads;CHECK(!r60_ws_ab_recheck(a,0,0) && n==loads);CHECK(!r60_ws_ab_recheck(a,0x7fff,0));
 /* Old8001/802A/BFFF AB miss encodings are no longer produced; all new
  * negatives are type11, disjoint from genuinely admitted type10 indices. */
 unsigned found=0;for(uint32_t k=0;k<65536;k++) {
  unsigned hash=(k^(k>>16)^(k>>7))&0x7fff;
  if(hash==1 || hash==42 || hash==0x3fff){CHECK((r60_ws_ab_miss(k)&0xc000)==0xc000);found++;}
 }CHECK(found>=3);
 uint32_t *first=malloc(16384u*sizeof(*first));CHECK(first);for(unsigned i=0;i<16384;i++)first[i]=0xffffffff;
 uint32_t x=0,y=0;for(uint32_t k=0;k<65536;k++) {
  unsigned h=r60_ws_ab_miss(k)&0x3fff;
  if(first[h]!=0xffffffff && first[h]!=k){x=first[h];y=k;break;}first[h]=k;
 }free(first);CHECK(x!=y && r60_ws_ab_miss(x)==r60_ws_ab_miss(y));
 CHECK(!r60_ws_ab_recheck(a,r60_ws_ab_miss(y),x)); /* collision selects raw, never art */
 uint32_t *large=calloc(4u+2u*32767u,sizeof(uint32_t));CHECK(large);large[0]=0x41423100;large[1]=32767;large[4u+2u*32766u]=0xabcdef12;observe(large,4u+2u*32767u,0);
 CHECK(r60_ws_ab_recheck(large,0x7fff,0xabcdef12));free(large);
}
static void oversized(void)
{
 unsigned n=16384,body=4u+3u*n,words=body+3u;uint32_t *a=calloc(words,4);CHECK(a);
 a[0]=0x5a423100;a[1]=n;a[2]=words*4u;
 for(unsigned i=0;i<n;i++){a[4u+3u*i]=i+1;a[5u+3u*i]=17;a[6u+3u*i]=body*4u;}
 a[body]=0x00010004;a[body+1]=0x0001fffc;a[body+2]=0x01020304;observe(a,words,1);
 uint16_t token=0;CHECK(r60_ws_zb_find_cached(7,a,words*4u,n,17,&token)==a+body && !token);
 rowloads=0;CHECK(r60_ws_zb_find_cached(7,a,words*4u,n,17,&token)==a+body && !token && rowloads==1);free(a);
}
static void real(const char *path)
{
 FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long len=ftell(f);CHECK(len>=16 && !(len&3));rewind(f);
 uint32_t *a=malloc((size_t)len);CHECK(a);for(long i=0;i<len/4;i++){unsigned char b[4];CHECK(fread(b,1,4,f)==4);a[i]=(uint32_t)b[0]<<24|(uint32_t)b[1]<<16|(uint32_t)b[2]<<8|b[3];}fclose(f);observe(a,(unsigned)len/4u,1);
 uint16_t tokens[64]={0};
 for(unsigned i=0;i<a[1];i++) {
  uint32_t key=a[4u+3u*i],zoom=a[5u+3u*i];
  const volatile uint32_t *ref=r60_ws_zb_find(7,a,(unsigned)len,key,zoom);CHECK(ref);
  memset(tokens,0,sizeof(tokens));
  for(unsigned slot=0;slot<64;slot++) {
   rowloads=0;CHECK(r60_ws_zb_find_cached(7,a,(unsigned)len,key,zoom,&tokens[slot])==ref && rowloads==(*ref>>16));
   rowloads=0;CHECK(r60_ws_zb_find_cached(7,a,(unsigned)len,key,zoom,&tokens[slot])==ref && !rowloads);
  }
  memset(tokens,0,sizeof(tokens)); /* Existing generation acquisition clears ALL slots. */
  for(unsigned slot=0;slot<64;slot++) {
   rowloads=0;CHECK(r60_ws_zb_find_cached(7,a,(unsigned)len,key,zoom,&tokens[slot])==ref && rowloads==(*ref>>16));
  }
 }
 printf("PASS real%u-pose atlas ×64slots: checked/cached exact packet identity,zero row reads on hit,new-generation revalidation\n",a[1]);free(a);
}
int main(int argc,char **argv)
{
 small_cases();tags();oversized();if(argc==2)real(argv[1]);free(is_row);
 puts("PASS admission tag separation,collisions,malformed/truncated bounds,scaled/unscaled reuse,oversize uncached,epoch purge");return 0;
}
