#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../sh_src/r60/r60_comm_arbiter.h"
#define C(x) do{if(!(x)){fprintf(stderr,"line%d\n",__LINE__);abort();}}while(0)
static uint32_t random_state=0x71ab59d3u;
static uint32_t rnd(void){random_state^=random_state<<13;random_state^=random_state>>17;return random_state^=random_state<<5;}
int main(void)
{
 struct {uint32_t before[4];r60_comm_state s;uint32_t after[4];} box;
 memset(&box,0xa5,sizeof(box));r60_comm_state *s=&box.s;r60_comm_init(s,8);
 C(sizeof(*s)==88u);uint8_t q[32];uint32_t head=0,count=0,tick=0,pushed=0,popped=0,full=0;
 s->read=s->write=0xfffffff0u;
 for(unsigned i=0;i<1000000u;i++) {
  uint32_t action=rnd();if(action&1u) {
   uint8_t value=(uint8_t)rnd();unsigned got=r60_comm_push(s,value);
   if(count==32){C(!got);full++;}else{C(got);q[(head+count)&31u]=value;count++;pushed++;}
  } else {
   uint8_t value=0;unsigned got=r60_comm_peek(s,&value);C(got==(count!=0));
   if(got){C(value==q[head&31u]);C(r60_comm_can_audio(s,tick));C(r60_comm_audio_posted(s,tick));C(!r60_comm_audio_posted(s,tick));head++;count--;popped++;}
  }
  tick++;C(r60_comm_depth(s)==count && s->highwater<=32u);
  C(s->full_count==full);
 }
 C(pushed>400000 && popped>400000 && full>1000);
 for(unsigned i=0;i<4;i++)C(box.before[i]==0xa5a5a5a5 && box.after[i]==0xa5a5a5a5);
 r60_comm_init(s,8);for(unsigned i=0;i<25;i++)C(r60_comm_push(s,(uint8_t)i));
 r60_comm_set_want(s,7,8,0);C(r60_comm_plan(s,8,0)==R60_COMM_AUDIO);
 C(r60_comm_audio_posted(s,0));C(r60_comm_depth(s)==24);
 C(r60_comm_plan(s,8,0)==(R60_COMM_HOLD|R60_COMM_MAP));
 /* Admission reserves eight subsequent enqueues, not a repeated headroom test. */
 for(unsigned i=1;i<=8;i++) {
  C(r60_comm_push(s,(uint8_t)(i+24)));
  C(r60_comm_plan(s,8,i)==(i==8?(R60_COMM_HOLD|R60_COMM_AUDIO):(R60_COMM_HOLD|R60_COMM_MAP)));
 }
 C(r60_comm_depth(s)==32 && s->budget_yields==1);
 for(unsigned i=0;i<100;i++)C(r60_comm_plan(s,8,8)==(R60_COMM_HOLD|R60_COMM_AUDIO));
 C(s->budget_yields==1 && !r60_comm_push(s,99));
 C(r60_comm_audio_posted(s,8));C(!r60_comm_can_audio(s,8));
 C(r60_comm_plan(s,8,9)==R60_COMM_AUDIO); /* headroom recovery resumes fallback */
 r60_comm_init(s,8);C(r60_comm_push(s,17));r60_comm_set_want(s,7,8,0xfffffffc);
 C(r60_comm_plan(s,8,0xfffffffc)==(R60_COMM_HOLD|R60_COMM_MAP));
 C(r60_comm_plan(s,8,3)==(R60_COMM_HOLD|R60_COMM_MAP));
 C(r60_comm_plan(s,8,4)==(R60_COMM_HOLD|R60_COMM_AUDIO));
 C(r60_comm_audio_posted(s,4));C(r60_comm_push(s,18));r60_comm_map_committed(s);
 r60_comm_set_want(s,9,7,5);C(r60_comm_plan(s,7,5)==(R60_COMM_HOLD|R60_COMM_AUDIO));
 C(r60_comm_audio_posted(s,5));C(r60_comm_plan(s,7,5)==(R60_COMM_HOLD|R60_COMM_MAP));
 for(unsigned k=0;k<2;k++)for(unsigned t=0;t<3;t++) {
  uint32_t v=0xfffffffeu+t;C(r60_comm_wait_first(s,k,v));for(unsigned i=0;i<100;i++)C(!r60_comm_wait_first(s,k,v));
 }
 C(!r60_comm_wait_first(s,2,0));s->full_count=UINT32_MAX;s->read=0;s->write=32;C(!r60_comm_push(s,0));C(s->full_count==UINT32_MAX);
 /* Native RESET may replace an unaccepted full-source latch, never FIFO. */
 uint8_t saved[32];memcpy(saved,s->queue,32);uint8_t mcu=55;mcu=0;
 C(!r60_comm_push(s,mcu));C(!memcmp(saved,s->queue,32) && s->read==0 && s->write==32);
 printf("PASS comm FIFO/policy: %u accepted/%u posted/%u refused; wraps,headroom,bounded fairness,VInt dedup,RESET retention,size%zu\n",pushed,popped,full,sizeof(*s));
 return 0;
}
