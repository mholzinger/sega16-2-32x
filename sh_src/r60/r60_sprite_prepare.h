#ifndef R60_SPRITE_PREPARE_H
#define R60_SPRITE_PREPARE_H
#include <stdint.h>
/* A persistent, caller-owned prepared pose. Source keys identify immutable baked
 * art; epoch identifies its mapped source page. Output retains the row packet
 * format, with palette-bank bits applied only to nonzero pixels. No positions,
 * clipping, framebuffer bank or RGB colors belong in this cache key. */
#ifndef R60_SP_CODE
#define R60_SP_CODE static __attribute__((noinline))
#endif
struct r60_sp_cache {
    /* Caller-owned packet storage. Capacity is measured in uint32_t words. */
    uint32_t *data;
    unsigned capacity;

    /* Identity and lifetime of the resident prepared pose. */
    uint32_t key, bank, epoch;
    unsigned height;
    /* Publication flag: clear while data is replaced, set after it is complete. */
    volatile unsigned valid;
    /* Resident hits during the current render pass. */
    unsigned uses;

    /* A challenger must repeat before it may replace the resident pose. */
    uint32_t candidate_key, candidate_bank;
    unsigned candidate_count, candidate_height;

    /* Diagnostic counters; volatile so hardware probes can observe them. */
    volatile unsigned lookups, hits, prepares, declined, words, bank_mask;
};
/* Begin one render pass. A page-epoch change invalidates the resident because
 * its source mapping may have changed. Within one epoch the prepared packet is
 * retained, while per-pass reuse and challenger history start over. */
R60_SP_CODE void r60_sp_begin(struct r60_sp_cache *c, unsigned epoch)
{
    if (c->epoch != epoch) c->valid = 0;
    c->epoch = epoch;
    c->uses = 0;
    c->candidate_count = 0;
}
/* Return a prepared packet, or NULL to make the caller use its established
 * fallback plotter. `height` is the complete baked pose height; `rows` is the
 * visible prefix needed for this draw. `source` points at the first AB1 row,
 * after the pose's u16-height/u16-width header.
 *
 * A miss first becomes a challenger. Seeing the same challenger twice admits
 * it only when it has more demand this pass than the current resident. Before
 * replacing anything, a full size scan proves the pose fits. The copy then
 * applies the numeric palette bank to nonzero pixels and publishes only after
 * the complete packet and its identity are available. */
R60_SP_CODE const uint32_t *r60_sp_get(struct r60_sp_cache *c, uint32_t key,
    unsigned bank, const volatile uint32_t *source, unsigned height, unsigned rows)
{
    unsigned y, total = 0, at = 0;
    const volatile uint32_t *p;
    c->lookups++;
    /* This renderer accepts at most 224 framebuffer rows and 16 palette banks. */
    if (!rows || rows > height || height > 224u || bank > 15u) {
        c->declined++;
        return 0;
    }

    /* Fast path: identity excludes placement, clipping and framebuffer state. */
    if (c->valid && c->key == key && c->bank == bank && c->height == height) {
        c->hits++; c->uses++;
        return c->data;
    }

    /* Count consecutive demand for one nonresident identity during this pass. */
    if (c->candidate_count && c->candidate_key == key
        && c->candidate_bank == bank && c->candidate_height == height) {
        c->candidate_count++;
    } else {
        c->candidate_key = key;
        c->candidate_bank = bank;
        c->candidate_height = height;
        c->candidate_count = 1;
    }
    /* Admit repeated work. A one-off challenger must not displace a pose being
     * reused more often in this pass. Keep resident data across pass boundaries. */
    if (c->candidate_count < 2u
        || (c->valid && c->candidate_count <= c->uses)) return 0;
    /* Preflight packet size. Each row starts with a word whose bits 16..23 hold
     * the number of four-pixel groups that follow (AB1 limits it to 255). */
    p = source;
    for (y = 0; y < height; y++) {
        unsigned ng = (*p >> 16) & 255u;
        if (total >= c->capacity || ng > c->capacity - total - 1u) {
            c->declined++;
            return 0;
        }
        total += 1u + ng;
        if (y + 1u < height) p += 1u + ng;
    }
    /* Preflight precedes every write: an oversized challenger leaves the old
     * resident completely intact. Clear publication before overwriting data;
     * the compiler barrier keeps packet writes between the valid transitions. */
    c->valid = 0;
    __asm__ volatile ("" : : : "memory");
    p = source;
    for (y = 0; y < height; y++) {
        uint32_t header = *p++;
        unsigned ng = (header >> 16) & 255u, k;
        c->data[at++] = header;
        for (k = 0; k < ng; k++) {
            uint32_t v = *p++;
            /* Per byte, adding 0x0f carries into bit 4 exactly when the source
             * pen is nonzero. Multiplying that mask applies `bank` without
             * changing transparent zero bytes. */
            uint32_t opaque = (v + 0x0F0F0F0Fu) & 0x10101010u;
            c->data[at++] = v | opaque * bank;
        }
    }
    c->key = key;
    c->bank = bank;
    c->height = height;
    /* Publish identity and packet contents before readers observe valid=1. */
    __asm__ volatile ("" : : : "memory");
    c->valid = 1;
    c->uses = 1;
    c->prepares++;
    c->words += total;
    c->bank_mask |= 1u << bank;
    return c->data;
}
/* Two size classes partition one existing arena. No pose is split and the
 * original single-cache API is intact. The build policy owns the split and
 * routing threshold; tools/baked_hole_census.py reports candidate policies. */
#ifndef R60_SP_ARENA_WORDS
#define R60_SP_ARENA_WORDS 1024u
#endif
#if R60_SP_SMALL_WORDS < 2u || R60_SP_SMALL_WORDS >= R60_SP_ARENA_WORDS
#error "R60 sprite preparation needs two nonempty slots inside its arena"
#endif
#define R60_SP_LARGE_WORDS (R60_SP_ARENA_WORDS - R60_SP_SMALL_WORDS)
R60_SP_CODE void r60_sp_begin2(struct r60_sp_cache *large,
    struct r60_sp_cache *small, unsigned epoch)
{
    r60_sp_begin(large, epoch);
    r60_sp_begin(small, epoch);
}
R60_SP_CODE const uint32_t *r60_sp_get2(struct r60_sp_cache *large,
    struct r60_sp_cache *small, uint32_t key, unsigned bank,
    const volatile uint32_t *source, unsigned height, unsigned rows)
{
    const uint32_t *prepared;
    /* A small-class miss always falls through. This includes first sighting,
     * admission loss and oversize decline, so the large slot maintains its own
     * independent challenger history and remains the safe fallback. */
    if (height <= R60_SP_SMALL_HEIGHT) {
        prepared = r60_sp_get(small, key, bank, source, height, rows);
        if (prepared) return prepared;
    }
    return r60_sp_get(large, key, bank, source, height, rows);
}
#endif
