#include <assert.h>
#include <stdint.h>
#include <stdio.h>
/* Deliberately differ from production defaults (256 words, height 24) so this
 * test proves the layout and router are build policy, not fixed constants.
 * Run with:
 *   clang -std=c99 -O1 -g -fsanitize=address,undefined \
 *     tests/diagnostics/check_sprite_prepare2.c -o /tmp/check_sprite_prepare2
 *   /tmp/check_sprite_prepare2
 */
#define R60_SPRPREP2 1
#define R60_SP_SMALL_WORDS 192u
#define R60_SP_SMALL_HEIGHT 12u
#include "../../sh_src/r60/r60_sprite_prepare.h"

static uint32_t source[1024], arena[R60_SP_ARENA_WORDS + 2];
static struct r60_sp_cache large = {
    .data = arena + 1, .capacity = R60_SP_LARGE_WORDS
};
static struct r60_sp_cache small = {
    .data = arena + 1 + R60_SP_LARGE_WORDS, .capacity = R60_SP_SMALL_WORDS
};

static const uint32_t *lookup(unsigned key, unsigned rows)
{
    return r60_sp_get2(&large, &small, key, 3, source, rows, rows);
}

/* Write a uniform packet, then look it up twice because admission needs repeat
 * demand. A row occupies one header word plus `groups` pixel words. */
static const uint32_t *admit_by_repeat(unsigned key, unsigned rows, unsigned groups)
{
    const uint32_t *p;
    unsigned y, k, at = 0;
    for (y = 0; y < rows; y++) {
        source[at++] = groups << 16 | groups;
        for (k = 0; k < groups; k++) source[at++] = 0x01020304u;
    }
    p = lookup(key, rows);
    if (!p) p = lookup(key, rows);
    return p;
}

int main(void)
{
    const uint32_t *p;
    unsigned i;
    assert(R60_SP_LARGE_WORDS == 832u);
    arena[0] = arena[R60_SP_ARENA_WORDS + 1] = 0xfacefeed;
    r60_sp_begin2(&large, &small, 1);

    /* 25 * (1 + 23) = 600 words: routed directly to the 832-word large slot. */
    p = admit_by_repeat(1, 25, 23);
    assert(p == large.data && large.valid && !small.valid);
    for (i = 0; i < 8; i++) assert(lookup(1, 25) == large.data);

    /* 1 * (1 + 100) = 101 words: routed to the 192-word small slot. */
    p = admit_by_repeat(2, 1, 100);
    assert(p == small.data && large.key == 1 && small.key == 2);
    admit_by_repeat(1, 25, 23);
    assert(lookup(1, 25) == large.data);
    admit_by_repeat(2, 1, 100);
    assert(lookup(2, 1) == small.data);

    r60_sp_begin2(&large, &small, 2);
    assert(!large.valid && !small.valid);
    /* 26 * (1 + 32) = 858 words: too large for either partition. */
    assert(!admit_by_repeat(3, 26, 32));
    assert(arena[0] == 0xfacefeed
           && arena[R60_SP_ARENA_WORDS + 1] == 0xfacefeed);
    puts("PASS: two-slot prepared cache reuse, victim admission, epoch and bounds");
    return 0;
}
