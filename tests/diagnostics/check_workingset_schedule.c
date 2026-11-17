/* Deterministic completed-tick/post8 schedule using actual mapper helpers.
 * Reproduces the observed old ordering failure with an already-ACKed map,
 * then checks the new pre-tick ordering without relaxing the deadline. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../sh_src/r60/r60_workingset.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line%d\n",__LINE__);abort();}}while(0)
static uint16_t cur,pending,gen,mail,go,done,draw,snap,want;
static unsigned calls,native_ticks,post_tick,valid,invalid,published,queued,posted,paused;
static unsigned service(void)
{
 calls++;
 if(pending) {
  if(!r60_ws_mapper_acked(mail))return 1;
  r60_ws_commit(&cur,&gen,&pending);return 0;
 }
 if(want==cur)return 0;
 unsigned d=r60_ws_reserve_or_defer(&pending,want,&done,&go,&draw,&snap,&mail);
 if(d!=R60_WS_POST)return d;
 mail=(uint16_t)(0x5be0u|want);return 1;
}
static void tick_and_check(void)
{
 native_ticks++;
 if(queued && !mail){mail=0x5a3d;queued--;posted++;}
 if(++post_tick==8u) {if(cur==8u && !pending)valid++;else invalid++;}
}
static void iteration(unsigned pre,unsigned due)
{
 if(paused)due=0; /* main frontend service runs BEFORE mapper service. */
 unsigned hold=pre?service():0;
 while(due--)tick_and_check();
 if(!pre)hold=service();
 if(!hold)published++;
}
static void reset(void)
{
 cur=7;pending=8;gen=1;mail=0;go=done=3;draw=snap=5;want=8;
 calls=native_ticks=valid=invalid=published=queued=posted=paused=0;post_tick=7;
}
int main(void)
{
 reset();iteration(0,1);CHECK(invalid==1 && !valid && cur==8 && !pending && calls==1);
 reset();iteration(1,1);CHECK(valid==1 && !invalid && cur==8 && !pending && gen==2 && calls==1 && published==1);
 reset();mail=0x5a3d;queued=1;iteration(1,1);CHECK(valid==1 && mail==0x5a3d && queued==1 && !posted && calls==1);
 reset();mail=0x5be8;queued=1;iteration(1,1);CHECK(invalid==1 && !valid && pending==8 && gen==1 && !published && queued==1 && !posted && calls==1);
 reset();mail=0xdead;iteration(1,1);CHECK(invalid==1 && pending==8 && gen==1 && !published);
 /* Pre-tick posting reserves mailbox before native audio; ACK0 next loop
  * commits before native+8. Audio is still queued, then posted normally. */
 reset();pending=0;post_tick=6;queued=1;iteration(1,1);
 CHECK(mail==0x5be8 && pending==8 && !published && queued==1 && !posted && post_tick==7 && calls==1);
 mail=0;iteration(1,1);CHECK(valid==1 && !invalid && gen==2 && !pending && mail==0x5a3d && queued==0 && posted==1 && published==1 && calls==2);
 /* A new phase can start during tick: this pass stays on current-page fallback,
  * and the next pre-tick service observes the newly completed phase policy. */
 reset();pending=0;want=7;post_tick=0;iteration(1,1);CHECK(cur==7 && !pending && published==1);
 want=8;iteration(1,1);CHECK(pending==8 && published==1 && calls==2);
 reset();paused=1;iteration(1,1);CHECK(native_ticks==0 && post_tick==7 && cur==8 && !pending && calls==1);
 reset();iteration(1,3);CHECK(native_ticks==3 && calls==1 && valid==1 && !invalid);
 puts("PASS pre-tick service: old+8 ordering counterexample,newdeadline pass,heldsnapshot,audio queue preserved,frontend pause,catch-up once-per-loop");
 return 0;
}
