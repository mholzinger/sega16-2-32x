/* check_speech_adpcm.c -- the speech lane before and after SH2ZAM adpcm-1, side by side.
 * The old per-nibble fill (sp_step) and the new run fill (sp_run over sh2z_ima_decode) are
 * copied from sh_src/r60/r60_speech.inc before and after the change. Random banks, voice
 * counts, positions, predictor/index states, 0-nibble entries and fill chunk splits; every
 * side must be byte-identical and leave identical voice state.
 *   cc -O2 -Ish_src tests/diagnostics/check_speech_adpcm.c sh_src/sh2zam/sh2zam_adpcm.c
 *      -o /tmp/check_speech_adpcm && /tmp/check_speech_adpcm 100000 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sh2zam/adpcm.h"
#define SP_VOICES 4
#define SP_SCALE 1
#define SAMPLE_MIN (8*SP_SCALE)
#define SAMPLE_MAX (1430*SP_SCALE)
#define SAMPLE_CENTER ((SAMPLE_MAX+SAMPLE_MIN)/2)
#define SOFT_HIGH (1290*SP_SCALE)
#define SOFT_LOW (148*SP_SCALE)
#define SP_SHIFT 6
#define SP_BUF 1024
#define SP_HOLD 2
static const uint16_t ima_step[89]={7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
static const int8_t ima_index[16]={-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8};
/* ---- old */
struct ov { const uint8_t *adp; uint32_t nibbles,pos; int32_t pred,idx; uint8_t active; };
static struct ov ovc[SP_VOICES];
static int sp_step(struct ov *x){uint8_t b=x->adp[x->pos>>1];int nib=(x->pos&1)?(b>>4):(b&0x0F);int step=ima_step[x->idx];int delta=step>>3;
 if(nib&4)delta+=step;if(nib&2)delta+=step>>1;if(nib&1)delta+=step>>2;x->pred+=(nib&8)?-delta:delta;
 if(x->pred>32767)x->pred=32767;else if(x->pred<-32768)x->pred=-32768;x->idx+=ima_index[nib];
 if(x->idx<0)x->idx=0;else if(x->idx>88)x->idx=88;if(++x->pos>=x->nibbles){x->active=0;return 0;}return 1;}
static void old_fill(uint16_t *buf,int i,int end){unsigned nact=0,v,only=0;for(v=0;v<SP_VOICES;v++)if(ovc[v].active){nact++;only=v;}
 if(nact==1){struct ov *x=&ovc[only];while(i<end&&x->active){uint16_t out;sp_step(x);out=(uint16_t)(SAMPLE_CENTER+(x->pred>>SP_SHIFT));buf[i]=out;buf[i+1]=out;i+=SP_HOLD;}}
 else if(nact>1){for(;i<end;i+=SP_HOLD){int32_t mix=0,s;unsigned any=0;for(v=0;v<SP_VOICES;v++){struct ov *x=&ovc[v];if(!x->active)continue;sp_step(x);mix+=x->pred>>SP_SHIFT;any=1;}
  if(!any)break;s=SAMPLE_CENTER+mix;if(s>SOFT_HIGH)s=SOFT_HIGH+((s-SOFT_HIGH)>>2);else if(s<SOFT_LOW)s=SOFT_LOW-((SOFT_LOW-s)>>2);
  if(s>SAMPLE_MAX)s=SAMPLE_MAX;else if(s<SAMPLE_MIN)s=SAMPLE_MIN;buf[i]=(uint16_t)s;buf[i+1]=(uint16_t)s;}}
 for(;i<end;i++)buf[i]=SAMPLE_CENTER;}
/* ---- new */
struct nv { const uint8_t *adp; uint32_t nibbles,pos; sh2z_ima_state_t st; uint8_t active; };
static struct nv vcs[SP_VOICES];
#define SP_RUN 32
static unsigned sp_run(struct nv *x,int16_t *out,unsigned n){uint32_t left=x->nibbles>x->pos?x->nibbles-x->pos:1u;if(n>left)n=left;
 sh2z_ima_decode(&x->st,x->adp,x->pos,out,n);x->pos+=n;if(x->pos>=x->nibbles)x->active=0;return n;}
static void new_fill(uint16_t *buf,int i,int end){unsigned nact=0,v,only=0;for(v=0;v<SP_VOICES;v++)if(vcs[v].active){nact++;only=v;}
 if(nact==1){struct nv *x=&vcs[only];int16_t run[SP_RUN];while(i<end&&x->active){unsigned want=(unsigned)(end-i+SP_HOLD-1)/SP_HOLD,k,n;
  n=sp_run(x,run,want<SP_RUN?want:SP_RUN);for(k=0;k<n;k++){uint16_t out=(uint16_t)(SAMPLE_CENTER+(run[k]>>SP_SHIFT));buf[i]=out;buf[i+1]=out;i+=SP_HOLD;}}}
 else if(nact>1){int16_t run[SP_VOICES][SP_RUN];unsigned len[SP_VOICES];while(i<end){unsigned want=(unsigned)(end-i+SP_HOLD-1)/SP_HOLD,most=0,k;if(want>SP_RUN)want=SP_RUN;
  for(v=0;v<SP_VOICES;v++){len[v]=vcs[v].active?sp_run(&vcs[v],run[v],want):0u;if(len[v]>most)most=len[v];}
  for(k=0;k<most;k++){int32_t mix=0,s;for(v=0;v<SP_VOICES;v++)if(k<len[v])mix+=run[v][k]>>SP_SHIFT;s=SAMPLE_CENTER+mix;
   if(s>SOFT_HIGH)s=SOFT_HIGH+((s-SOFT_HIGH)>>2);else if(s<SOFT_LOW)s=SOFT_LOW-((SOFT_LOW-s)>>2);if(s>SAMPLE_MAX)s=SAMPLE_MAX;else if(s<SAMPLE_MIN)s=SAMPLE_MIN;
   buf[i]=(uint16_t)s;buf[i+1]=(uint16_t)s;i+=SP_HOLD;}if(most<want)break;}}
 for(;i<end;i++)buf[i]=SAMPLE_CENTER;}
static uint32_t rng=12345;static uint32_t rnd(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main(int argc,char**argv){
 static uint8_t bank[SP_VOICES][3000];unsigned iters=argc>1?atoi(argv[1]):20000;unsigned long samples=0;
 for(unsigned it=0;it<iters;it++){
  for(int v=0;v<SP_VOICES;v++){for(int k=0;k<3000;k++){uint32_t r=rnd();if(it%3==1)r=(r&0x88)?0x77:0xFF;bank[v][k]=(uint8_t)r;}
   int act=(rnd()%3u)!=0; uint32_t nib=(it%17==0)?0:(rnd()%5000u); uint32_t pos=nib?rnd()%nib:0;
   int32_t pred=(int32_t)(rnd()%65536u)-32768, idx=(int32_t)(rnd()%89u);
   ovc[v]=(struct ov){bank[v],nib,pos,pred,idx,(uint8_t)act};
   vcs[v]=(struct nv){bank[v],nib,pos,{pred,idx},(uint8_t)act};}
  /* one side filled in chunks of random size (the slave), or whole (the master) */
  uint16_t a[SP_BUF+2],b[SP_BUF+2];memset(a,0xAB,sizeof a);memset(b,0xAB,sizeof b);
  int i=0;while(i<SP_BUF){int end=(rnd()%4u==0)?SP_BUF:i+2*(int)(1+rnd()%40u);if(end>SP_BUF)end=SP_BUF;old_fill(a,i,end);new_fill(b,i,end);i=end;}
  if(memcmp(a,b,sizeof a)){fprintf(stderr,"FAIL buffer it=%u\n",it);return 1;}
  for(int v=0;v<SP_VOICES;v++)if(ovc[v].pos!=vcs[v].pos||ovc[v].pred!=vcs[v].st.pred||ovc[v].idx!=vcs[v].st.index||ovc[v].active!=vcs[v].active){fprintf(stderr,"FAIL state it=%u v=%d\n",it,v);return 1;}
  samples+=SP_BUF;}
 printf("speech fill old==new: %u sides, %lu samples, voice state equal\n",iters,samples);return 0;}
