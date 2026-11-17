#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../sh_src/r60/r60_sprite_prepare.h"
static uint32_t source[2048], store[1026];
static struct r60_sp_cache cache = { .data=store+1, .capacity=1024 };
static uint32_t scalar(uint32_t v,unsigned bank) {
    uint32_t out=0; unsigned i;
    for(i=0;i<4;i++) { unsigned shift=24-i*8,b=(v>>shift)&255; if(b)b|=bank*16;out|=(uint32_t)b<<shift; }
    return out;
}
static const uint32_t *admit(unsigned key,unsigned bank,unsigned h,unsigned rows) {
    const uint32_t *p=r60_sp_get(&cache,key,bank,source,h,rows);
    if(!p)p=r60_sp_get(&cache,key,bank,source,h,rows);
    return p;
}
int main(void) {
    unsigned bank,n,y,k; const uint32_t *p; uint32_t saved[1024];
    store[0]=store[1025]=0xfacefeed;
    for(bank=0;bank<16;bank++)for(n=0;n<65536;n++) {
        uint32_t v=((n&15)<<24)|(((n>>4)&15)<<16)|(((n>>8)&15)<<8)|(n>>12);
        uint32_t opaque=(v+0x0f0f0f0fu)&0x10101010u;
        assert((v|opaque*bank)==scalar(v,bank));
    }
    for(y=0;y<64;y++) {
        source[y*10]=(y%3u)<<24 | 9u<<16 | 12u;
        for(k=0;k<9;k++)source[y*10+k+1]=0x00010203u*(1u+(y+k)%5u);
    }
    r60_sp_begin(&cache,1);
    assert(!r60_sp_get(&cache,77,12,source,64,64));
    p=admit(77,12,64,64); assert(p==store+1);
    for(y=0;y<64;y++) {
        assert(p[y*10]==source[y*10]);
        for(k=0;k<9;k++)assert(p[y*10+k+1]==scalar(source[y*10+k+1],12));
    }
    assert(cache.prepares==1);
    r60_sp_begin(&cache,1);assert(r60_sp_get(&cache,77,12,source,64,3)==p);
    assert(cache.prepares==1); /* clipping does not rebuild a prepared pose */
    for(n=0;n<8;n++)assert(r60_sp_get(&cache,77,12,source,64,64)==p);
    assert(!r60_sp_get(&cache,78,2,source,64,64));
    assert(!r60_sp_get(&cache,78,2,source,64,64)); /* resident wins this pass */
    memcpy(saved,store+1,sizeof saved);
    r60_sp_begin(&cache,1);
    for(y=0;y<128;y++){source[y*16]=15u<<16|15u;for(k=1;k<16;k++)source[y*16+k]=0x01000100;}
    assert(!admit(79,2,128,128)); /* capacity preflight leaves resident untouched */
    assert(!memcmp(saved,store+1,sizeof saved));assert(cache.valid && cache.key==77);
    r60_sp_begin(&cache,2);assert(!cache.valid);
    source[0]=1u<<16|1u;source[1]=0x000a000b;
    p=admit(77,3,1,1);assert(p && p[1]==0x003a003b);
    r60_sp_begin(&cache,2);assert(!r60_sp_get(&cache,77,4,source,1,1));
    p=admit(77,4,1,1);assert(p && p[1]==0x004a004b);
    r60_sp_begin(&cache,2);
    source[2]=1u<<16|1u;source[3]=0x01000200;
    assert(!r60_sp_get(&cache,77,4,source,2,2));
    assert(cache.height==1); /* identical key/bank with new geometry cannot hit */
    p=admit(77,4,2,2);assert(p && cache.height==2 && p[3]==0x41004200);
    assert(!r60_sp_get(&cache,77,4,source,0,0));
    assert(!r60_sp_get(&cache,77,4,source,225,1));
    assert(!r60_sp_get(&cache,77,4,source,1,2));
    assert(!r60_sp_get(&cache,77,16,source,1,1));
    assert(store[0]==0xfacefeed && store[1025]==0xfacefeed);
    printf("PASS: all65536groups x16banks; reuse, identity, clipping, epoch, admission, bounds\n");
    return 0;
}
