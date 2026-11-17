/* Execute the ACTUAL control helpers under every SC load/store interleaving.
 * Replay captured reads, yielding at the next unknown access; no implementation
 * rewrite, threads or concurrent C undefined behavior. Hardware ordering remains
 * the separate SH2 uncached-transaction platform precondition. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
static uint16_t load16(const volatile uint16_t *p);
static void store16(volatile uint16_t *p,uint16_t v);
#define R60_WS_LOAD(p) load16(p)
#define R60_WS_STORE(p,v) store16(p,(uint16_t)(v))
#include "../../sh_src/r60/r60_workingset.h"
enum {PENDING,GO,DONE,DRAW,SNAP,MAIL,CUR,GEN,READING,SOURCE_EPOCH,SOURCE_CUR,NV};
static uint16_t mem[NV];
struct op {unsigned write,index;uint16_t value;};
static struct op history[2][64],nextop;
static struct {unsigned thread;struct op op;} schedule[128];
static unsigned lengths[2],thread_id,access_at,physical_gen,physical_page,legacy,newmail,postsound,master_result;
static unsigned long leaves,nodes,failures,rollbacks;
static jmp_buf yield;
#define CHECK(v) do {if(!(v)){fprintf(stderr,"line%d\n",__LINE__);abort();}}while(0)
static uint16_t access16(const volatile uint16_t *p,unsigned write,uint16_t value)
{
 unsigned index=(unsigned)(p-mem);CHECK(index<NV);
 if(access_at<lengths[thread_id]) {
  struct op *o=&history[thread_id][access_at++];
  CHECK(o->index==index && o->write==write && (!write || o->value==value));
  return o->value;
 }
 nextop=(struct op){write,index,value};longjmp(yield,1);
}
static uint16_t load16(const volatile uint16_t *p){return access16(p,0,0);}
static void store16(volatile uint16_t *p,uint16_t v){(void)access16(p,1,v);}
static void master(void)
{
 if(legacy) {
  uint16_t d=load16(mem+DONE),g=load16(mem+GO),r,s;
  if(d!=g)return;r=load16(mem+DRAW);s=load16(mem+SNAP);
  if(r!=s || load16(mem+MAIL))return;
  store16(mem+PENDING,7);store16(mem+MAIL,0x5be7);
 } else if(newmail) {
  master_result=r60_ws_reserve_or_defer(mem+PENDING,7,mem+DONE,mem+GO,mem+DRAW,mem+SNAP,mem+MAIL);
  if(master_result!=R60_WS_POST)return;
  store16(mem+MAIL,0x5be7); /* Target emits this store in RAM after cold return. */
 } else if(!r60_ws_request(mem+PENDING,7,mem+DONE,mem+GO,mem+DRAW,mem+SNAP,mem+MAIL,0x5be7))return;
 store16(mem+MAIL,0); /* Controller restored RV, then ACK; physical page now7. */
 if(newmail && postsound && load16(mem+MAIL)==0)store16(mem+MAIL,0x5a3d);
 if(newmail && !r60_ws_mapper_acked(load16(mem+MAIL)))return;
 r60_ws_commit(mem+CUR,mem+GEN,mem+PENDING);
}
static void slave(void)
{
 uint16_t seq=load16(mem+SNAP),draw=load16(mem+DRAW),go,epoch,page;
 if(seq==draw)return;
 if(legacy) {
  store16(mem+DRAW,seq);epoch=load16(mem+GEN);
  go=(uint16_t)(load16(mem+GO)+1u);store16(mem+GO,go);
 } else {
  if(!r60_ws_claim(mem+GO,mem+PENDING,mem+DRAW,seq,&go))return;
  epoch=load16(mem+GEN);
 }
 page=load16(mem+CUR);
 store16(mem+SOURCE_EPOCH,epoch);store16(mem+SOURCE_CUR,page);store16(mem+READING,1);
 store16(mem+READING,0);store16(mem+DONE,go);
}
static unsigned next(unsigned who)
{
 thread_id=who;access_at=0;
 if(setjmp(yield))return 1;
 if(who)slave();else master();
 CHECK(access_at==lengths[who]);return 0;
}
static void visit(void)
{
 nodes++;
 unsigned any=0;
 for(unsigned t=0;t<2;t++) {
  if(legacy && failures)return;
  if(!next(t))continue;
  any=1;struct op o=nextop;
  uint16_t saved[NV];memcpy(saved,mem,sizeof(mem));unsigned pg=physical_gen,pp=physical_page;
  if(!o.write)o.value=mem[o.index];
  else {
   if(o.index==MAIL && o.value==0x5be7 && mem[READING])failures++;
   if(o.index==MAIL && !o.value && mem[MAIL]==0x5be7){physical_gen=(physical_gen+1u)&65535u;physical_page=7;}
   if(o.index==READING && o.value && (mem[SOURCE_EPOCH]!=physical_gen || mem[SOURCE_CUR]!=physical_page || mem[MAIL]==0x5be7))failures++;
   if(t==1 && o.index==GO)for(unsigned i=0;i<lengths[t];i++)
    if(history[t][i].write && history[t][i].index==GO){rollbacks++;break;}
   mem[o.index]=o.value;
  }
  unsigned depth=lengths[0]+lengths[1];schedule[depth].thread=t;schedule[depth].op=o;
  history[t][lengths[t]++]=o;
  if(failures && legacy) {
   static const char *names[]={"pending","GO","DONE","DRAWSEQ","SNAPSEQ","COMM","CUR","GEN","reading","epoch","page"};
   puts("Old protocol counterexample:");
   for(unsigned i=0;i<=depth;i++)printf(" %c %c%s=%u\n",schedule[i].thread?'S':'M',schedule[i].op.write?'W':'R',names[schedule[i].op.index],schedule[i].op.value);
  }
  if(failures && !legacy) {fprintf(stderr,"new protocol failure\n");abort();}
  if(!failures)visit();
  lengths[t]--;memcpy(mem,saved,sizeof(mem));physical_gen=pg;physical_page=pp;
 }
 if(!any) {
  if(newmail) {
   CHECK(!mem[PENDING]);
   if(physical_page==7)CHECK(mem[CUR]==7 && mem[GEN]==physical_gen && mem[MAIL]==(postsound?0x5a3d:0));
   else {
    CHECK(mem[CUR]==8 && (mem[MAIL]==0 || mem[MAIL]==0x5a12));
    if(mem[MAIL]==0x5a12)CHECK(master_result==R60_WS_DEFER && lengths[0]==1 && !history[0][0].write && history[0][0].index==MAIL);
   }
  }
  leaves++;
 }
}
static void finish_alone(unsigned who)
{
 lengths[who]=0;
 while(next(who)) {
  struct op o=nextop;if(!o.write)o.value=mem[o.index];else mem[o.index]=o.value;
  history[who][lengths[who]++]=o;
 }
}
static void busy_pipeline(void)
{
 memset(mem,0,sizeof(mem));mem[CUR]=physical_page=8;mem[GEN]=physical_gen=0;mem[MAIL]=0x5a12;lengths[0]=lengths[1]=0;
 finish_alone(0);CHECK(master_result==R60_WS_DEFER && mem[MAIL]==0x5a12 && !mem[PENDING]);
 if(master_result==R60_WS_DEFER)mem[SNAP]=2; /* Actual main returns0: publish new snapshot. */
 finish_alone(1);CHECK(mem[DRAW]==2 && mem[DONE]==1 && mem[CUR]==8 && mem[MAIL]==0x5a12);
 puts("PASS busy-no-map DEFER publishes/consumes new snapshot on old page without touching sound");
}
static void frontend_resume(void)
{
 /* The main frontend branch continues its outer loop: do not pass heldseq1
  * to claim after the frontend replaces the snapshot/page while parked. */
 memset(mem,0,sizeof(mem));mem[SNAP]=2;mem[DRAW]=0;mem[CUR]=physical_page=7;mem[GEN]=physical_gen=1;
 lengths[0]=lengths[1]=0;
 while(next(1)) {
  struct op o=nextop;if(!o.write)o.value=mem[o.index];else mem[o.index]=o.value;
  history[1][lengths[1]++]=o;
 }
 CHECK(mem[DRAW]==2 && mem[GO]==1 && mem[DONE]==1 && mem[SOURCE_CUR]==7 && mem[SOURCE_EPOCH]==1);
 /* A reservation seen on the second check rolls back without consumption. */
 memset(mem,0,sizeof(mem));mem[SNAP]=3;mem[GO]=mem[DONE]=9;mem[PENDING]=7;lengths[1]=0;
 while(next(1)) {
  struct op o=nextop;if(!o.write)o.value=mem[o.index];else mem[o.index]=o.value;
  history[1][lengths[1]++]=o;
 }
 CHECK(mem[GO]==9 && mem[DONE]==9 && !mem[DRAW] && !mem[READING]);
 puts("PASS frontend resume rereads snapshot; reserved acceptance rolls back before consumption");
}

int main(void)
{
 legacy=1;mem[SNAP]=1;mem[CUR]=physical_page=8;visit();CHECK(failures);
 printf("PASS old mapper counterexample found after%lu replay nodes\n",nodes);
 legacy=0;failures=nodes=leaves=rollbacks=0;
 for(unsigned unread=0;unread<2;unread++)for(unsigned wrap=0;wrap<2;wrap++)for(unsigned gw=0;gw<2;gw++)for(unsigned busy=0;busy<2;busy++) {
  memset(mem,0,sizeof(mem));mem[SNAP]=1;mem[DRAW]=unread?0:1;mem[CUR]=8;
  mem[GO]=mem[DONE]=wrap?0xffff:0;mem[GEN]=physical_gen=gw?0xffff:0;physical_page=8;mem[MAIL]=busy?0x5a12:0;lengths[0]=lengths[1]=0;
  visit();
 }
 CHECK(leaves==10700ul && nodes==54300ul && rollbacks==4496ul); /* Frozen pre-split access-order census. */
 printf("PASS actual mapper helpers: %lu SC schedules/%lu replay nodes,0 failures,%lu rollback branches; unread/consumed snapshots,GO/generation wrap,busy mailbox,ACK page/generation release\n",leaves,nodes,rollbacks);
 newmail=1;failures=nodes=leaves=rollbacks=0;
 for(unsigned unread=0;unread<2;unread++)for(unsigned wrap=0;wrap<2;wrap++)for(unsigned gw=0;gw<2;gw++)for(unsigned busy=0;busy<2;busy++)for(postsound=0;postsound<2;postsound++) {
  memset(mem,0,sizeof(mem));mem[SNAP]=1;mem[DRAW]=unread?0:1;mem[CUR]=8;
  mem[GO]=mem[DONE]=wrap?0xffff:0;mem[GEN]=physical_gen=gw?0xffff:0;physical_page=8;mem[MAIL]=busy?0x5a12:0;lengths[0]=lengths[1]=0;
  visit();
 }
 printf("PASS mailbox-fix actual helpers: %lu SC schedules/%lu nodes,0 failures; audio-after-ACK retained,DEFER renders old page\n",leaves,nodes);
 busy_pipeline();frontend_resume();
 unsigned accepted_tags=0;
 for(unsigned v=0;v<65536u;v++) {
  unsigned ack=r60_ws_mapper_acked((uint16_t)v);
  CHECK(ack==(v==0 || (v&0xff00u)==0x5a00u));accepted_tags+=ack;
 }
 CHECK(accepted_tags==257u && !r60_ws_mapper_acked(0x5be7) && !r60_ws_mapper_acked(0xdead));
 puts("PASS ACK classification:0 andall5Axx accepted;5B/unknown held, audio word untouched");
 return 0;
}
