#ifndef R60_PLANE_ROWS_H
#define R60_PLANE_ROWS_H
#include <stdint.h>
/* Resolve one contiguous physical source-row segment. The caller still reads
 * every live word through volatile storage in its original order. No persistent
 * cache, copied rows, dirty-generation assumptions or palette state live here. */
struct r60_pr_view {
    const volatile uint16_t *tiles;
    unsigned pages, selection;
};
static inline const volatile uint16_t *r60_pr_segment(const struct r60_pr_view *v,
    unsigned world_x, unsigned world_y, unsigned *count)
{
    unsigned x=world_x&63u, y=world_y&31u;
    unsigned quadrant=((world_x>>6)&1u)|(((world_y>>5)&1u)<<1);
    unsigned page=(v->selection>>(quadrant*4u))&15u;
    *count=64u-x;
    if (page>=v->pages) return 0;
    return v->tiles+page*2048u+y*64u+x;
}
#endif
