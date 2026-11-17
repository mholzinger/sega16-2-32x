#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define R60_ZOOM_RUN 1
#include "../../sh_src/r60/r60_workingset.h"
#include "../../games/altbeast/r60_game_workingset.h"
#include "../../sh_src/r60/r60_sprite_prepare.h"
#define CHECK(v) do {if(!(v)){fprintf(stderr,"line%d\n",__LINE__);abort();}}while(0)
static uint32_t atlas[]={0x5a423100,1,40,0,0x12345678,17,28,0x00010004,0x0001fffc,0x01020304};
static uint32_t runs[]={0x4f523100,1,28,4,20,0x00010000,0x02040000};
int main(int argc,char **argv)
{
 uint16_t ram[8192]={0};uint16_t *boss=ram+0xa00;
 uint32_t handlers[]={0x16d7e,0x1794c,0x17b54,0x17b88,0x1794d,0,0xffffffff};
 for(unsigned status=0;status<2;status++)for(unsigned h=0;h<7;h++) {
  boss[0]=status?0x8000:0;boss[1]=handlers[h]>>16;boss[2]=handlers[h];
  CHECK(r60_game_workingset((volatile uint8_t *)ram)==(status&&h<4));
 }
 CHECK(!r60_ws_zb_find(8,(void *)(uintptr_t)1,40,0x12345678,17));
 CHECK(!r60_ws_zb_find(7,(void *)(uintptr_t)1,12,0x12345678,17));
 CHECK(r60_ws_zb_find(7,atlas,sizeof(atlas),0x12345678,17)==atlas+7);
 unsigned pose=99;
 CHECK(r60_ws_zb_find_index(7,atlas,sizeof(atlas),0x12345678,17,&pose)==atlas+7 && pose==0);
 const volatile uint8_t *run=r60_ws_or_find(7,runs,sizeof(runs),pose,1,1);
 CHECK(run==(const volatile uint8_t *)runs+24);
 CHECK(!r60_ws_or_find(8,runs,sizeof(runs),pose,1,1));
 CHECK(!r60_ws_or_find(7,runs,sizeof(runs),pose,2,1));
 CHECK(!r60_ws_or_find(7,runs,sizeof(runs),pose,1,2));
 uint32_t badruns[sizeof(runs)/sizeof(runs[0])];
 memcpy(badruns,runs,sizeof(runs)); badruns[0]=0; CHECK(!r60_ws_or_find(7,badruns,sizeof(badruns),0,1,1));
 memcpy(badruns,runs,sizeof(runs)); badruns[1]=4; CHECK(!r60_ws_or_find(7,badruns,sizeof(badruns),0,1,1));
 memcpy(badruns,runs,sizeof(runs)); badruns[3]=0; CHECK(!r60_ws_or_find(7,badruns,sizeof(badruns),0,1,1));
 memcpy(badruns,runs,sizeof(runs)); badruns[4]=16; CHECK(!r60_ws_or_find(7,badruns,sizeof(badruns),0,1,1));
 memcpy(badruns,runs,sizeof(runs)); badruns[4]=21; CHECK(!r60_ws_or_find(7,badruns,sizeof(badruns),0,1,1));
 memcpy(badruns,runs,sizeof(runs)); badruns[4]=28; CHECK(!r60_ws_or_find(7,badruns,sizeof(badruns),0,1,1));
 memcpy(badruns,runs,sizeof(runs)); badruns[5]=0x00010001; CHECK(!r60_ws_or_find(7,badruns,sizeof(badruns),0,1,1));
 CHECK(!r60_ws_zb_find(7,atlas,sizeof(atlas),0x12345678,18));
 CHECK(!r60_ws_zb_find(7,atlas,sizeof(atlas),0x12345677,17));
 CHECK(!r60_ws_zb_find(7,atlas,sizeof(atlas),0x12345678,0));
 const unsigned changes[][2]={{0,0x5a503100},{1,0xffffffff},{2,44},{2,12},{3,1},{6,16},{6,29},{6,40},{7,0x10000000},{7,0x00010141},{8,0x0002fffc},{8,0x0101fffc},{8,0x00010004},{8,0x0001fff8}};
 for(unsigned i=0;i<sizeof(changes)/sizeof(*changes);i++) {
  uint32_t a[10];memcpy(a,atlas,sizeof(a));a[changes[i][0]]=changes[i][1];
  CHECK(!r60_ws_zb_find(7,a,sizeof(a),0x12345678,17));
 }
 /* No atlas pointer cache: page7 -> normalpage -> different generation/page7
  * resolves the same key anew. Existing real SPRPREP epoch invalidates its
  * resident unscaled packet independently of the scaled lookup. */
 uint32_t second[10],storage[16];memcpy(second,atlas,sizeof(second));second[9]=0x09080706;
 struct r60_sp_cache c={.data=storage,.capacity=16};
 r60_sp_begin(&c,3);CHECK(!r60_sp_get(&c,1,0,atlas+8,1,1));CHECK(r60_sp_get(&c,1,0,atlas+8,1,1));
 CHECK(c.valid);r60_sp_begin(&c,4);CHECK(!c.valid);
 CHECK(!r60_ws_zb_find(8,0,0,0x12345678,17));
 CHECK(r60_ws_zb_find(7,second,sizeof(second),0x12345678,17)==second+7);
 CHECK(r60_ws_zb_find(7,second,sizeof(second),0x12345678,17)[2]==0x09080706);
 if(argc==2) {
  FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long len=ftell(f);CHECK(len>=16 && !(len&3));rewind(f);
  uint32_t *ix=malloc((size_t)len);CHECK(ix);
  for(long i=0;i<len/4;i++){unsigned char b[4];CHECK(fread(b,1,4,f)==4);ix[i]=(uint32_t)b[0]<<24|(uint32_t)b[1]<<16|(uint32_t)b[2]<<8|b[3];}fclose(f);
  for(unsigned i=0;i<ix[1];i++)CHECK(r60_ws_zb_find(7,ix,(unsigned)len,ix[4+i*3],ix[5+i*3])==ix+(ix[6+i*3]>>2));
  printf("PASS real raw ZB1: %u entries\n",ix[1]);free(ix);
 }
 puts("PASS workset policy/lookup/page fallback/malformed bounds/SPRPREP epoch");
 return 0;
}
