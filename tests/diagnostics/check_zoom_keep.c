#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../sh_src/r60/r60_zoom_keep.h"

static uint32_t source_hash(void)
{
    uint32_t h=2166136261u;
    for(unsigned hz=0;hz<32;hz++)for(unsigned i=0;i<16;i++)
        h=(h^r60_zoom_keep_nibbles[hz][i])*16777619u;
    return h;
}

int main(void)
{
    unsigned copies=0, decisions=0;
    if(sizeof(r60_zoom_keep_nibbles)!=512 || source_hash()!=0x1a07fff1u)abort();
    for(unsigned hz=0;hz<32;hz++)for(unsigned align=0;align<8;align++) {
        uint8_t buf[48], before[48], want[16]={0};
        for(unsigned i=0;i<48;i++)buf[i]=(uint8_t)(i*97u+hz*13u+align);
        memcpy(before,buf,sizeof(buf));
        uint8_t *dst=buf+8+align;
        r60_zoom_keep_copy(hz,dst);
        copies++;
        for(unsigned i=0;i<48;i++)
            if((i<8+align || i>=24+align) && buf[i]!=before[i])abort();

        /* Independent stateful carry machine, no closed-form table formula.
         * Check every bit and repeated word phases over 128 full periods. */
        unsigned acc=(hz&15u)*4u;
        for(unsigned pixel=0;pixel<8192;pixel++) {
            acc+=hz;
            unsigned kept=acc<64;
            acc&=63u;
            unsigned word=(pixel/4)&15u, bit=pixel&3u;
            if(pixel<64 && kept)want[word]|=(uint8_t)(1u<<bit);
            if(((dst[word]>>bit)&1u)!=kept) {
                fprintf(stderr,"hz=%u align=%u pixel=%u got=%u want=%u\n",
                        hz,align,pixel,(dst[word]>>bit)&1u,kept);
                return 1;
            }
            decisions++;
        }
        if(memcmp(dst,want,16))abort();
        /* Scalar/check paths still consume the original two 32-bit masks.
         * Reconstruct with the same eight constant-shift iterations as the
         * optional main setup, then compare every bit to the carry machine. */
        uint32_t lo=0,hi=0;
        for(unsigned k=8;k;k--) {
            lo=(lo<<4)|dst[k-1]; hi=(hi<<4)|dst[k+7];
        }
        acc=(hz&15u)*4u;
        for(unsigned pixel=0;pixel<64;pixel++) {
            acc+=hz;
            unsigned kept=acc<64; acc&=63u;
            unsigned bit=pixel<32 ? (lo>>pixel)&1u : (hi>>(pixel-32))&1u;
            if(bit!=kept)abort();
        }
        /* Working-buffer mutation must leave the immutable source untouched. */
        memset(dst,0xa5,16);
        if(source_hash()!=0x1a07fff1u)abort();
    }
    /* Exact-size destinations let ASan detect any access past the 16-byte API. */
    for(unsigned hz=0;hz<32;hz++) {
        uint8_t *dst=malloc(16);
        if(!dst)return 2;
        r60_zoom_keep_copy(hz,dst);
        if(memcmp(dst,r60_zoom_keep_nibbles[hz],16))abort();
        free(dst); copies++;
    }
    printf("PASS %u bounded copies, %u carry decisions, scalar masks, all32 settings, source FNV1a=%08x\n",
            copies,decisions,source_hash());
    return 0;
}
