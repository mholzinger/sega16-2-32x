#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include "../../sh_src/r60/r60_plane_rows.h"
static uint16_t storage[13*2048+2];
static unsigned cases;
static void check(unsigned selection,unsigned x,unsigned y,unsigned n)
{
    struct r60_pr_view view={storage+1,13,selection};
    const volatile uint16_t *source=0;
    unsigned left=0, pages[4]={selection&15,(selection>>4)&15,(selection>>8)&15,(selection>>12)&15};
    for(unsigned j=0;j<n;j++) {
        unsigned wx=(x+j)&127,wy=y&63;
        unsigned quadrant=(wx>=64?1u:0u)+(wy>=32?2u:0u),page=pages[quadrant];
        uint16_t expected=page<13?storage[1+page*2048+(wy%32)*64+(wx%64)]:0,got;
        if (!left)source=r60_pr_segment(&view,x+j,y,&left);
        assert(left>0 && left<=64);
        got=source?*source++:0;left--;
        assert(got==expected);
    }
    assert(storage[0]==0xface && storage[13*2048+1]==0xbeef);cases++;
}
int main(int argc,char **argv)
{
    storage[0]=0xface;storage[13*2048+1]=0xbeef;
    for(unsigned i=0;i<13*2048;i++)storage[i+1]=(uint16_t)(i+1);
    for(unsigned p=0;p<65536;p++) {
        check(p,60,0,8);check(p,124,31,8);check(p,60,32,8);check(p,124,63,8);
    }
    for(unsigned y=0;y<128;y++)for(unsigned x=0;x<128;x++) {
        check(0x0123,x,y,42);check(0xcdef,x,y,129);
    }
    check(0x7654,UINT_MAX-5,UINT_MAX,193);
    { unsigned count;struct r60_pr_view v={storage+1,13,0x7777};
      const volatile uint16_t *p=r60_pr_segment(&v,4,3,&count);
      unsigned at=1+7*2048+3*64+5;uint16_t old=storage[at];
      storage[at]=0x1234;assert(p[1]==0x1234);storage[at]=old; }
    for(int a=1;a<argc;a++) {
        FILE *f=fopen(argv[a],"rb");assert(f);
        for(unsigned i=0;i<13*2048;i++){int hi=fgetc(f),lo=fgetc(f);assert(hi>=0&&lo>=0);storage[i+1]=(uint16_t)((hi<<8)|lo);}
        fclose(f);
        for(unsigned y=0;y<64;y++)for(unsigned x=0;x<128;x++) {
            check(0x7777,x,y,42);check(0x0000,x,y,42);
            check(0xaaaa,x,y,16);check(0xbbbb,x,y,16);
        }
    }
    printf("PASS: %u row cases; all page selections, wraps, invalid pages, live reads; %d native snapshots\n",cases,argc-1);
}
