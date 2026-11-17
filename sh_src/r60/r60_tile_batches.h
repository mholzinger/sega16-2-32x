#ifndef R60_TILE_BATCHES_H
#define R60_TILE_BATCHES_H
#include <stdint.h>
/* Hardware adapter boundary: prepared 4bpp patterns grouped by destination.
 * Keep VRAM deduplication; never allocate duplicate tiles for a world block.
 * Runs hold at most 16 native patterns (512 bytes). Singleton records retain
 * the existing 17-word format; runs use [slot|6000, tiles, contiguous pixels]. */
#define R60_TB_SLOTS 1024u
#define R60_TB_MAX 16u
#define R60_TB_RUN 0x6000u
/* One-word command for the immutable zero pattern, instead of resending its
 * pixels. A VDP-reset/menu adapter must invalidate residency before reuse. */
#define R60_TB_ZERO 0x5000u
struct r60_tb_set { uint32_t bits[32]; };
struct r60_tb_iter { const struct r60_tb_set *set; uint32_t mask; unsigned word, slot, pending; };
static inline void r60_tb_clear(struct r60_tb_set *s)
{ unsigned i; for (i=0; i<32u; i++) s->bits[i]=0; }
static inline unsigned r60_tb_add(struct r60_tb_set *s, unsigned slot)
{
    if (slot>=R60_TB_SLOTS) return 0;
    s->bits[slot>>5] |= (uint32_t)1 << (slot&31u);
    return 1;
}
static inline void r60_tb_begin(struct r60_tb_iter *i, const struct r60_tb_set *s)
{ i->set=s; i->mask=s->bits[0]; i->word=0; i->slot=0; i->pending=R60_TB_SLOTS; }
static inline unsigned r60_tb_slot(struct r60_tb_iter *i)
{
    while (!i->mask) {
        if (++i->word>=32u) return R60_TB_SLOTS;
        i->mask=i->set->bits[i->word]; i->slot=i->word*32u;
    }
    while (!(i->mask&1u)) { i->mask>>=1; i->slot++; }
    i->mask>>=1; return i->slot++;
}
static inline unsigned r60_tb_next(struct r60_tb_iter *i, unsigned *first)
{
    unsigned count=1, slot=i->pending;
    if (slot==R60_TB_SLOTS) slot=r60_tb_slot(i);
    i->pending=R60_TB_SLOTS;
    if (slot==R60_TB_SLOTS) return 0;
    *first=slot;
    while (count<R60_TB_MAX) {
        slot=r60_tb_slot(i);
        if (slot==R60_TB_SLOTS || slot!=*first+count) { i->pending=slot; break; }
        count++;
    }
    return count;
}
/* Decoder checks before any DMA, including logical tile count and source span.
 * The adapter supplies the available words up to its packet/bus boundary. */
static inline unsigned r60_tb_valid(unsigned first, unsigned count,
                                    unsigned remaining, unsigned words)
{
    return count>=2u && count<=R60_TB_MAX && count<=remaining &&
           first>0u && first<R60_TB_SLOTS && count<=R60_TB_SLOTS-first &&
           words>=2u+16u*count;
}
static inline void r60_tb_pixels(volatile uint16_t *out, const uint32_t *in,
                                 const uint8_t *map, unsigned bg)
{
    unsigned y;
    for (y=0; y<8u; y++) {
        uint32_t h=in[y*2u], l=in[y*2u+1u];
        unsigned p0=(h>>24)&7u,p1=(h>>16)&7u,p2=(h>>8)&7u,p3=h&7u;
        unsigned p4=(l>>24)&7u,p5=(l>>16)&7u,p6=(l>>8)&7u,p7=l&7u;
        p0=(p0||bg)?map[p0]:0u; p1=(p1||bg)?map[p1]:0u;
        p2=(p2||bg)?map[p2]:0u; p3=(p3||bg)?map[p3]:0u;
        p4=(p4||bg)?map[p4]:0u; p5=(p5||bg)?map[p5]:0u;
        p6=(p6||bg)?map[p6]:0u; p7=(p7||bg)?map[p7]:0u;
        out[y*2u]=(uint16_t)((p0<<12)|(p1<<8)|(p2<<4)|p3);
        out[y*2u+1u]=(uint16_t)((p4<<12)|(p5<<8)|(p6<<4)|p7);
    }
}
#endif
