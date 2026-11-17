#ifndef R60_WORKINGSET_H
#define R60_WORKINGSET_H
#include <stdint.h>
/* Cross-core control uses uncached, two-byte-aligned 16-bit storage on SH2. These compiler
 * barriers rely on the platform's ordered uncached transactions, not a C11
 * atomics claim. Hooks let host interleaving tests execute these exact helpers. */
#ifndef R60_WS_LOAD
#define R60_WS_LOAD(p) (*(p))
#endif
#ifndef R60_WS_STORE
#define R60_WS_STORE(p,v) (*(p)=(v))
#endif
#ifndef R60_WS_BARRIER
#define R60_WS_BARRIER() __asm__ volatile ("" : : : "memory")
#endif
static inline unsigned r60_ws_reserve(volatile uint16_t *pending,uint16_t want,
    const volatile uint16_t *done,const volatile uint16_t *go,
    const volatile uint16_t *draw,const volatile uint16_t *snap,
    const volatile uint16_t *mail)
{
    uint16_t done_seq,go_seq,draw_seq,snapshot_seq;
    if (!want) return 0;
    R60_WS_STORE(pending,want);
    R60_WS_BARRIER();
    done_seq=R60_WS_LOAD(done);
    go_seq=R60_WS_LOAD(go);
    if (done_seq!=go_seq) goto busy;
    draw_seq=R60_WS_LOAD(draw);
    snapshot_seq=R60_WS_LOAD(snap);
    if (draw_seq!=snapshot_seq || R60_WS_LOAD(mail)) goto busy;
    return 1;
busy:
    R60_WS_STORE(pending,0);
    return 0;
}
enum {
    R60_WS_DEFER=0u, /* unrelated mailbox traffic: keep using the old page */
    R60_WS_WAIT=1u,  /* renderer is not quiescent: drain before retrying */
    R60_WS_POST=2u   /* reservation succeeded: caller may post the mapper word */
};
/* Busy unrelated mail is not a mapping transaction: keep rendering the current
 * page through exact raw fallback. Reserve/drain only when the mailbox is free. */
static inline unsigned r60_ws_reserve_or_defer(volatile uint16_t *pending,uint16_t want,
    const volatile uint16_t *done,const volatile uint16_t *go,
    const volatile uint16_t *draw,const volatile uint16_t *snap,
    const volatile uint16_t *mail)
{
    if (R60_WS_LOAD(mail)) return R60_WS_DEFER;
    return r60_ws_reserve(pending,want,done,go,draw,snap,mail)?R60_WS_POST:R60_WS_WAIT;
}
/* A retained pending value exists only after a mapper post. The sole master
 * sound producer writes5Axx ONLY after observing mail0; the mapper's controller
 * ACK0 follows RV restoration. Therefore later5Axx proves that ACK happened.
 * This predicate NEVER consumes/clears audio; unknown and5B tags fail closed. */
static __attribute__((always_inline)) inline unsigned r60_ws_mapper_acked(uint16_t mail)
{
    return mail==0u || (mail&0xff00u)==0x5a00u;
}
/* Host/portable wrapper for the exact transaction sequence. Target keeps the
 * final mailbox store in RAM, after its cold reserve helper has returned. */
static inline unsigned r60_ws_request(volatile uint16_t *pending,uint16_t want,
    const volatile uint16_t *done,const volatile uint16_t *go,
    const volatile uint16_t *draw,const volatile uint16_t *snap,
    volatile uint16_t *mail,uint16_t command)
{
    if (!r60_ws_reserve(pending,want,done,go,draw,snap,mail)) return 0;
    R60_WS_STORE(mail,command);return 1;
}
static inline unsigned r60_ws_claim(volatile uint16_t *go,
    const volatile uint16_t *pending,volatile uint16_t *draw,uint16_t seq,
    uint16_t *accepted)
{
    uint16_t previous=R60_WS_LOAD(go),next=(uint16_t)(previous+1u);
    R60_WS_STORE(go,next);
    R60_WS_BARRIER();
    if (R60_WS_LOAD(pending)) {
        R60_WS_STORE(go,previous);
        return 0;
    }
    R60_WS_BARRIER(); /* Acquire before cache/prep or mapped source reads. */
    R60_WS_STORE(draw,seq);
    *accepted=next;
    return 1;
}
static inline void r60_ws_commit(volatile uint16_t *cur,
    volatile uint16_t *gen,volatile uint16_t *pending)
{
    uint16_t page=R60_WS_LOAD(pending),generation;
    R60_WS_STORE(cur,page);
    R60_WS_BARRIER();
    generation=R60_WS_LOAD(gen);
    R60_WS_STORE(gen,(uint16_t)(generation+1u));
    R60_WS_BARRIER();
    R60_WS_STORE(pending,0);
}
/* One optional immutable mapper-page working set. The caller admits only the
 * committed page AFTER its generation/cache barrier; no pointer is retained
 * here across calls or page generations. ZB1 only: packed formats decline. */
#define R60_WS_PAGE 7u
static inline const volatile uint32_t *r60_ws_zb_find_index(unsigned page,
    const volatile uint32_t *ix, unsigned extent, uint32_t key, unsigned zoom,
    unsigned *pose_index)
{
    unsigned bytes,n,lo=0,hi,off,at,rows,width;
    if (page!=R60_WS_PAGE || !ix || extent<16u || (extent&3u) ||
        !zoom || zoom>1023u) return 0;
    if (ix[0]!=0x5a423100u || ix[3]!=0u) return 0;
    bytes=ix[2];n=ix[1];
    if (bytes<16u || bytes>extent || (bytes&3u) || n>(bytes-16u)/12u) return 0;
    hi=n;
    while (lo<hi) {
        unsigned m=lo+((hi-lo)>>1);
        uint32_t k=ix[4u+3u*m],z=ix[5u+3u*m];
        if (k<key || (k==key && z<zoom)) lo=m+1u; else hi=m;
    }
    if (lo==n || ix[4u+3u*lo]!=key || ix[5u+3u*lo]!=zoom) return 0;
    off=ix[6u+3u*lo];
    if ((off&3u) || off<16u+12u*n || off>bytes-4u) return 0;
    at=off>>2;rows=ix[at]>>16;width=ix[at]&65535u;
    if (!rows || rows>224u || width>320u) return 0;
    /* Independently bound every raw row before the assembly consumer reads it.
     * Raw ZB1 extents are negative exact pixel ends, never AB1 word counts. */
    for (unsigned r=0;r<rows;r++) {
        uint32_t hdr;unsigned groups,end;
        if (++at>=bytes/4u) return 0;
        hdr=ix[at];groups=(hdr>>16)&255u;
        end=(0u-(hdr&65535u))&65535u;
        if (!end || end>width || ((hdr>>24)+groups)*4u>((end+3u)&~3u) ||
            groups>bytes/4u-at-1u) return 0;
        at+=groups;
    }
    if (pose_index) *pose_index=lo;
    return ix+(off>>2);
}
static inline const volatile uint32_t *r60_ws_zb_find(unsigned page,
    const volatile uint32_t *ix, unsigned extent, uint32_t key, unsigned zoom)
{
    return r60_ws_zb_find_index(page,ix,extent,key,zoom,0);
}
/* OR1 is parallel to the immutable ZB1 index: one u32 pose offset followed by
 * u16 rows/u16 reserved and u8 start/u8 count pairs. The baker proves opacity;
 * runtime bounds the directory/body. The live caller admits the plan only for
 * complete, unclipped atlas rows; clipped records retain the established copy. */
static inline const volatile uint8_t *r60_ws_or_find(unsigned page,
    const volatile uint32_t *ix,unsigned extent,unsigned pose_index,
    unsigned pose_count,unsigned rows)
{
    unsigned bytes,count,off;
    if(page!=R60_WS_PAGE || !ix || extent<16u || (extent&3u))return 0;
    if(ix[0]!=0x4f523100u)return 0;
    count=ix[1];bytes=ix[2];
    if(ix[3]<1u || ix[3]>255u || bytes<16u || bytes>extent ||
       count!=pose_count || pose_index>=count || count>(bytes-16u)/4u)return 0;
    off=ix[4u+pose_index];
    if((off&3u) || off<16u+4u*count || off>bytes-4u || (ix[off>>2]>>16)!=rows ||
       (ix[off>>2]&65535u)!=0u || rows>(bytes-off-4u)/2u)return 0;
    return (const volatile uint8_t *)ix+off+4u;
}
#endif
