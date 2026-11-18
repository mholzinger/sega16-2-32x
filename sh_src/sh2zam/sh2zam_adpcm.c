/* IMA/DVI ADPCM decoding; see include/sh2zam/adpcm.h for the contract.
 * MIT; Copyright (c) 2026 SH2ZAM Contributors. */
#include "adpcm.h"   /* vendored: upstream includes "sh2zam/adpcm.h" */

#include "sh2zam_adpcm_tables.h"

/* One nibble. `row` is index * 8. Each clamp only tests the side the nibble moves its
 * value toward: an add to pred can only overflow upward and a subtract only downward;
 * nibbles 4..7 raise the index by 2,4,6,8 (clamp at 88) and 0..3 lower it by 1 (at 0). */
#define SH2Z_IMA_NIBBLE(nib)                                         \
    do {                                                             \
        const uint32_t n_=(nib);                                     \
        const uint32_t t_=row+(n_&7u);                               \
        const int32_t d_=(int32_t)diff[t_];                          \
        if(n_&4u) { row+=((n_&3u)+1u)<<4; if(row>88u*8u) row=88u*8u; }         else if(row) row-=8u;                                        \
        if(n_&8u) { pred-=d_; if(pred<-32768) pred=-32768; }         \
        else      { pred+=d_; if(pred>32767) pred=32767; }           \
        *out++=(int16_t)pred;                                        \
    } while(0)

void sh2z_ima_decode(sh2z_ima_state_t *state,const uint8_t *src,uint32_t pos,
                     int16_t *out,size_t count) {
    if(!count) return;
    const uint16_t *const diff=sh2z_ima_diff;
    int32_t pred=state->pred;
    uint32_t row=(uint32_t)state->index<<3;
    const uint8_t *p=src+(pos>>1);
    if(pos&1u) {                       /* start on a high nibble */
        SH2Z_IMA_NIBBLE((uint32_t)*p++>>4);
        count--;
    }
    for(size_t pairs=count>>1;pairs;pairs--) {
        const uint32_t b=*p++;
        SH2Z_IMA_NIBBLE(b&15u);
        SH2Z_IMA_NIBBLE(b>>4);
    }
    if(count&1u) SH2Z_IMA_NIBBLE((uint32_t)*p&15u);
    state->pred=pred;
    state->index=(int32_t)(row>>3);
}
#undef SH2Z_IMA_NIBBLE
