#ifndef R60_WORKINGSET_CLIP_H
#define R60_WORKINGSET_CLIP_H
#include <stdint.h>
/* Trusted immutable raw ZB1 packets, prevalidated by the atlas admission layer.
 * Right entry only: x0>=xo, x0<lim; negative row lengths and bounded j0/ng.
 * Same sixteen-slot rows ABI as bkcopy; uintptr_t permits caller-owned host
 * storage without truncating pointers. Target uintptr_t is32bits. No globals,
 * palette pointers, physical addresses or packet mutation. */
#ifndef R60_WC_ARG
#define R60_WC_ARG uintptr_t
#endif
#ifndef R60_WC_CODE
#define R60_WC_CODE static __attribute__((noinline))
#endif
#ifndef R60_WC_LOAD
#define R60_WC_LOAD(p) (*(const volatile uint32_t *)(p))
#endif
#ifndef R60_WC_STORE
#define R60_WC_STORE(p,v) (*(volatile uint32_t *)(p)=(uint32_t)(v))
#endif
static inline unsigned r60_wc_cut(unsigned hz,unsigned remaining)
{
    unsigned seed=(hz&15u)*4u;
    unsigned words=(64u*(remaining-1u)+seed)/(4u*(64u-hz))+1u;
    return 4u*words-((seed+4u*words*hz)>>6);
}
/* Select constant SH2 shifts once per row. hz0 counts original nonzero SOURCE
 * groups even hidden suffix pens; hz>0 counts nonzero DESTINATION stores incl
 * pending carry. hz is0..31; lim-x0 is1..320. */
#define R60_WC_COPY(S) do { \
    uint32_t carry_=0u; unsigned j_; \
    for(j_=j0;j_<end;j_++){ \
        uint32_t g_=R60_WC_LOAD(src+(j_-j0)),out_; \
        if(!hz && g_)npx+=4u; \
        if(j_+1u==groups){ \
            switch(limit&3u){case 1:g_&=0xff000000u;break;case 2:g_&=0xffff0000u;break;case 3:g_&=0xffffff00u;break;default:break;} \
        } \
        g_|=((g_+0x0f0f0f0fu)&0x10101010u)*bank; \
        if(S){out_=carry_|(g_>>(S));carry_=g_<<(32u-((S)?(S):8u));}else{out_=g_;carry_=0u;} \
        if(out_){R60_WC_STORE(row+4u*j_,out_);if(hz)npx+=4u;} \
    } \
    if(carry_ && xs+4u*end<lim){R60_WC_STORE(row+4u*end,carry_);if(hz)npx+=4u;} \
} while(0)
R60_WC_CODE void r60_wc_rows(R60_WC_ARG a[16],unsigned hz)
{
    const volatile uint32_t *src=(const volatile uint32_t *)a[0];
    volatile uint8_t *row=(volatile uint8_t *)a[1];
    uint32_t *spw=(uint32_t *)a[8],*sp_end=(uint32_t *)a[9];
    unsigned nrows=(unsigned)a[2],bank=((unsigned)a[3]&255u)>>4;
    unsigned x0=(unsigned)a[5],xs=(unsigned)a[6],lim=(unsigned)a[7];
    unsigned cut=r60_wc_cut(hz,lim-x0),upper=((lim+3u)&~3u)-x0;
    unsigned sel=(unsigned)a[12],y=(unsigned)a[11],cap=(unsigned)a[13];
    unsigned npx=0u,full=0u,xmax=0u,r;
    for(r=0;r<nrows;r++,row+=320u,y++){
        uint32_t hdr=R60_WC_LOAD(src++);
        unsigned j0=hdr>>24,ng=(hdr>>16)&255u;
        unsigned length=(unsigned)(-(int)(int16_t)(hdr&65535u));
        unsigned logical=length<cut?length:cut;
        unsigned limit=logical<upper?logical:upper;
        unsigned groups=(limit+3u)>>2,end=j0+ng,xe=x0+logical,xe4;
        if(end>groups)end=groups;
#ifdef R60_WC_SHARED
        if(j0<end){
            uint32_t carry=0u;unsigned j;
            for(j=j0;j<end;j++){
                uint32_t g=R60_WC_LOAD(src+(j-j0)),out;
                if(!hz && g)npx+=4u;
                if(j+1u==groups){switch(limit&3u){
                    case 1:g&=0xff000000u;break;
                    case 2:g&=0xffff0000u;break;
                    case 3:g&=0xffffff00u;break;
                    default:break;}}
                g|=((g+0x0f0f0f0fu)&0x10101010u)*bank;
                switch(sel){
                    case 0:out=g;carry=0u;break;
                    case 1:out=carry|(g>>8);carry=g<<24;break;
                    case 2:out=carry|(g>>16);carry=g<<16;break;
                    default:out=carry|(g>>24);carry=g<<8;break;}
                if(out){R60_WC_STORE(row+4u*j,out);if(hz)npx+=4u;}
            }
            if(carry && xs+4u*end<lim){R60_WC_STORE(row+4u*end,carry);if(hz)npx+=4u;}
        }
#else
        if(j0<end){
            switch(sel){case 0:R60_WC_COPY(0);break;case 1:R60_WC_COPY(8);break;case 2:R60_WC_COPY(16);break;default:R60_WC_COPY(24);break;}
        }
#endif
        src+=ng;
        if(xe>xmax)xmax=xe;
        xe4=(xe+3u)&~3u;if(xe4>cap)xe4=cap;
        if(xe4>xs){uint32_t word=(uint32_t)a[10]|(y<<24)|(xe4>>2);
            if(spw<sp_end)*spw++=word;else full=1u;}
    }
    a[2]=full;a[8]=(uintptr_t)spw;a[14]=npx;a[15]=xmax;
}
#undef R60_WC_COPY
#endif
