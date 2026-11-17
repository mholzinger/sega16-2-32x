#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../sh_src/r60/r60_tile_batches.h"
static uint16_t packet[17412], actual[16384], expected[16384];
static uint32_t rng=0x18376a91u;
static unsigned random32(void) { rng=rng*1664525u+1013904223u;return rng; }
static uint16_t pixel_word(unsigned slot,unsigned k) { return slot?(uint16_t)(slot*37u+k*173u):0; }
static void check_set(unsigned mode)
{
    struct r60_tb_set set; struct r60_tb_iter it;
    unsigned used=0, first=0, count, tiles=0, seen[1024]={0}, k, i, batches=0;
    r60_tb_clear(&set);
    assert(!r60_tb_add(&set,1024));assert(!r60_tb_add(&set,65535));
    for (i=0;i<1024;i++) {
        unsigned slot=1023u-i;
        if (mode==0 || (mode==1 && slot>=17 && slot<82) ||
            (mode==2 && (slot==0 || slot==31 || slot==32 || slot==1023)) ||
            (mode>3 && (random32()&15u)<mode-3u)) {
            assert(r60_tb_add(&set,slot));assert(r60_tb_add(&set,slot));seen[slot]=1;
        }
    }
    memset(actual,0x7e,sizeof actual);memcpy(expected,actual,sizeof actual);
    for (i=0;i<1024;i++) if (seen[i]) for(k=0;k<16;k++)expected[i*16+k]=pixel_word(i,k);
    r60_tb_begin(&it,&set);
    while ((count=r60_tb_next(&it,&first))) {
        assert(count<=16 && first+count<=1024);
        if (!first) {
            packet[used++]=R60_TB_ZERO;tiles++;
            first++; if (!--count)continue;
        }
        packet[used++]=(uint16_t)(first|(count>1?0x6000:0x4000));
        if (count>1)packet[used++]=(uint16_t)count;
        for (i=0;i<count;i++)for(k=0;k<16;k++)packet[used++]=pixel_word(first+i,k);
        tiles+=count;batches++;
    }
    assert(used<=17u*tiles);
    /* Independent transfer interpreter: stream -> VRAM, not the set iterator. */
    for (i=0,k=0;i<used;) {
        unsigned header=packet[i++],dst=header&1023u,n=1;
        if (header==0x5000) { memset(actual,0,32);k++;continue; }
        if (header&0x2000u)n=packet[i++];
        assert(n>=1 && n<=16 && dst+n<=1024);
        for (unsigned t=0;t<n;t++)for(unsigned p=0;p<16;p++)actual[(dst+t)*16+p]=packet[i++];
        k+=n;
    }
    assert(k==tiles && !memcmp(actual,expected,sizeof actual));
    if (!mode) assert(tiles==1024 && batches==64);
}
int main(void)
{
    uint32_t in[16];uint16_t out[18];uint8_t map[8];
    for(unsigned seed=0;seed<16;seed++)for(unsigned q=0;q<4096;q++)for(unsigned bg=0;bg<2;bg++) {
        for(unsigned p=0;p<8;p++)map[p]=(uint8_t)((p*(seed|1u)+seed)&15u);
        for(unsigned y=0;y<16;y++) {
            unsigned a=(q+y)&7u,b=((q>>3)+y)&7u,c=((q>>6)+y)&7u,d=((q>>9)+y)&7u;
            in[y]=0xf8f8f8f8u|a<<24|b<<16|c<<8|d;
        }
        out[0]=out[17]=0xface;for(unsigned p=1;p<17;p++)out[p]=0xffff;
        r60_tb_pixels(out+1,in,map,bg);
        for(unsigned y=0;y<16;y++) {
            unsigned want=0;
            for(unsigned p=0;p<4;p++) { unsigned px=(in[y]>>(24-p*8))&7u;want=(want<<4)|((px||bg)?map[px]:0u); }
            assert(out[y+1]==want);
        }
        assert(out[0]==0xface && out[17]==0xface);
    }
    for(unsigned mode=0;mode<20;mode++)check_set(mode);
    assert(r60_tb_valid(1008,16,16,258));
    assert(!r60_tb_valid(1009,16,16,258));
    assert(!r60_tb_valid(0,0,16,258));assert(!r60_tb_valid(0,1,16,258));
    assert(!r60_tb_valid(0,17,17,274));assert(!r60_tb_valid(0,16,15,258));
    assert(!r60_tb_valid(0,16,16,257));assert(!r60_tb_valid(1024,2,2,34));
    assert(!r60_tb_valid(0,2,2,34)); /* slot zero is owned by its compact command */
    puts("PASS: 131072 color cases, all VRAM slots, sparse/duplicate/reversed sets, bounded runs and malformed headers");
}
