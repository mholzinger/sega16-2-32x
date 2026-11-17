/* r60_macro.inc -- macro packets: bind, probe, plan and live draw.
 * Part of r60_main.c: included there, in this order, so the compiler sees one unit. */
/* Live builds call the page-7 closure below.  Keep header helpers inline so
 * the unused probe-only call graph can disappear instead of leaving a second
 * out-of-line copy in resident code. */
#define R60_MC_API static inline
#include "r60_sprite_macro.h"
#undef R60_MC_API
/* Read-only planner census.  Words 0..11 retain the original stable layout:
 * attempts, barrier-approved patterns/records, KEEP barriers, canvas-phase
 * declines, bad MC1, complete/failed plans, source/metadata/packet failures,
 * and the last one-based failing record.  The admission detail at 12..19 is:
 * target-at-head, valid signature, directory fingerprint hit, exact admission,
 * later-target admission, last record count, last fingerprint, capacity-only
 * decline count, last available spans, and last starting span count.
 * No counter changes framebuffer or renderer state. */
static volatile uint32_t r60_mc_probe_stat[22];
static r60_mc_view r60_mc_probe_view;
static uint8_t r60_mc_probe_bound;

static int r60_mc_bind(void)
{
    const uint8_t *data;
    if (r60_mc_probe_bound) return 1;
    /* A page baked without macro packets declares zero patterns: every sprite
     * then takes the ordinary draw. */
    if (SPRITE_WORKSET_MACRO_PATTERNS == 0u) return 0;
    data = (const uint8_t *)SPRITE_WORKSET_MACRO_ADDRESS;
    /* The page baker fully verifies MC1.  Runtime pays only this immutable
     * generated-header identity check; the bounded parser remains the host
     * and future untrusted-input gate. */
    if (r60_mc_u32(data) != 0x4D433100u ||
        (r60_mc_u16(data + 4u) != 1u &&
         r60_mc_u16(data + 4u) != 2u) ||
        r60_mc_u16(data + 6u) != SPRITE_WORKSET_MACRO_PATTERNS ||
        r60_mc_u32(data + 8u) != SPRITE_WORKSET_MACRO_TARGET_KEY ||
        r60_mc_u32(data + 20u) != SPRITE_WORKSET_MACRO_BYTES)
        return 0;
    r60_mc_probe_view.data = data;
    r60_mc_probe_view.bytes = SPRITE_WORKSET_MACRO_BYTES;
    r60_mc_probe_view.target_key = SPRITE_WORKSET_MACRO_TARGET_KEY;
    r60_mc_probe_view.patterns = SPRITE_WORKSET_MACRO_PATTERNS;
    r60_mc_probe_view.directory_offset = 32u;
    r60_mc_probe_view.body_offset = 32u +
        SPRITE_WORKSET_MACRO_PATTERNS * 12u;
    r60_mc_probe_view.version = r60_mc_u16(data + 4u);
    r60_mc_probe_view.asset_offset = r60_mc_u32(data + 24u);
    r60_mc_probe_view.asset_bytes = r60_mc_u32(data + 28u);
    r60_mc_probe_bound = 1u;
    return 1;
}

/* Reproduce only the bounded signature half of r60_mc_admit so a failed exact
 * admission has a useful reason.  This is deliberately probe-local: the live
 * renderer continues to have one admission implementation. */
static int r60_mc_probe_signature(const volatile uint16_t *records,
                                  unsigned count, uint32_t target_key,
                                  uint32_t *fingerprint, unsigned *number)
{
    unsigned i, field, fields[5], n = 0u;
    uint32_t fp;
    if (!records || !count || count > 64u) return 0;
    for (i = 0; i < count; i++) {
        const volatile uint16_t *r = records + i * 8u;
        int y;
        if (r[2] & 0x8000u) break;
        if (r60_mc_record_key(r) != target_key) break;
        y = r[0] & 255u;
        if ((r[2] & 0xC000u) || (r[0] >> 8) <= (unsigned)y) return 0;
        n++;
    }
    if (!n) return 0;
    fp = n;
    for (i = 0; i < n; i++) {
        const volatile uint16_t *r = records + i * 8u;
        int x = (int)(r[1] & 511u) - 184;
        int y = r[0] & 255u;
        fields[0] = (uint16_t)x;
        fields[1] = (uint16_t)y;
        fields[2] = (uint16_t)((r[0] >> 8) - (unsigned)y);
        fields[3] = r[5] & 0x3FFu;
        fields[4] = r[4] & 0x3Fu;
        for (field = 0; field < 5u; field++) {
            fp = (fp << 5) | (fp >> 27);
            fp ^= fields[field];
        }
    }
    *fingerprint = fp;
    *number = n;
    return 1;
}

static unsigned r60_mc_probe_source_extent(uint32_t address)
{
    if (address >= SPRITE_WORKSET_ZB1_ADDRESS &&
        address < SPRITE_WORKSET_ZB1_ADDRESS + SPRITE_WORKSET_ZB1_BYTES)
        return SPRITE_WORKSET_ZB1_ADDRESS + SPRITE_WORKSET_ZB1_BYTES - address;
#define R60_MC_WINDOW_EXTENT(number) \
    if (address >= ARTBAKE_WIN##number##_ADDR && \
        address < ARTBAKE_WIN##number##_ADDR + ARTBAKE_WIN##number##_BYTES) \
        return ARTBAKE_WIN##number##_ADDR + ARTBAKE_WIN##number##_BYTES - address
    R60_MC_WINDOW_EXTENT(0);
    R60_MC_WINDOW_EXTENT(1);
    R60_MC_WINDOW_EXTENT(2);
    R60_MC_WINDOW_EXTENT(3);
#undef R60_MC_WINDOW_EXTENT
    return 0u;
}

static int r60_mc_probe_source(const r60_mc_record *macro_record,
                               unsigned slot, const uint8_t **source,
                               unsigned *source_bytes)
{
    uint32_t address;
    if (macro_record->zoom) {
        const volatile uint8_t *run_plan = 0;
        address = r60_zb_lookup(SPRITE_WORKSET_MACRO_TARGET_KEY,
                                macro_record->zoom, &run_plan);
    } else {
        unsigned position = r60_bk_lookup(SPRITE_WORKSET_MACRO_TARGET_KEY);
        (void)slot;
        if (position & 0x8000u) return 0;
        address = R60_BK_INDEX[5u + (position - 1u) * 2u];
    }
    *source_bytes = r60_mc_probe_source_extent(address);
    *source = (const uint8_t *)address;
    return address && *source_bytes;
}

static __attribute__((noinline)) void r60_mc_probe(
    unsigned bank, unsigned slot, const volatile uint16_t *record,
    unsigned active)
{
    r60_mc_pattern pattern;
    unsigned index, spans = 0u;
    uint32_t fingerprint;
    unsigned number;
    if (!active) return;
    if (r60_xo & 3u) { r60_mc_probe_stat[4]++; return; }
    if (!r60_mc_bind()) { r60_mc_probe_stat[5]++; return; }
    r60_mc_probe_stat[0]++;
    if (r60_mc_record_key(record) == r60_mc_probe_view.target_key) {
        r60_mc_probe_stat[12]++;
        if (r60_mc_probe_signature(record, 64u - slot,
                                   r60_mc_probe_view.target_key,
                                   &fingerprint, &number)) {
            r60_mc_probe_stat[13]++;
            r60_mc_probe_stat[17] = number;
            r60_mc_probe_stat[18] = fingerprint;
            for (index = 0; index < r60_mc_probe_view.patterns; index++) {
                const uint8_t *directory = r60_mc_probe_view.data +
                    r60_mc_probe_view.directory_offset + index * 12u;
                if (r60_mc_u32(directory) == fingerprint) {
                    r60_mc_probe_stat[14]++;
                    break;
                }
            }
        }
    }
    if (!r60_mc_admit(&r60_mc_probe_view, record, 64u - slot, 8u,
                      &pattern)) return;
    if (pattern.first) { r60_mc_probe_stat[16]++; return; }
    r60_mc_probe_stat[15]++;
    for (index = 0; index < pattern.records; index++) {
        unsigned sprite = slot + index;
        if (R60_KS_KEEP_DRAW(sprite) || ks_repunch[sprite]) {
            r60_mc_probe_stat[3]++;
            return;
        }
    }
    r60_mc_probe_stat[1]++;
    r60_mc_probe_stat[2] += pattern.records;
    for (index = 0; index < pattern.records; index++) {
        const uint8_t *source;
        unsigned source_bytes, packet_stores;
        r60_mc_record macro_record;
        r60_mc_metadata metadata;
        if (!r60_mc_pattern_record(&pattern, index, &macro_record) ||
            !r60_mc_probe_source(&macro_record, slot + index, &source,
                                 &source_bytes)) {
            r60_mc_probe_stat[8]++;
            r60_mc_probe_stat[11] = index + 1u;
            r60_mc_probe_stat[7]++;
            return;
        }
        {
            unsigned existing = r60_rowspan_n[bank & 1u];
            unsigned available = existing + spans <= R60_SSPAN_CAP ?
                R60_SSPAN_CAP - existing - spans : 0u;
            int metadata_ok = existing + spans <= R60_SSPAN_CAP &&
                r60_mc_source_metadata(&macro_record, source, source_bytes,
                                       r60_xo, FB_W, FB_H, slot + index, 0,
                                       available, &metadata);
            if (!metadata_ok) {
                r60_mc_metadata unbounded;
                int capacity_only = r60_mc_source_metadata(
                    &macro_record, source, source_bytes, r60_xo, FB_W, FB_H,
                    slot + index, 0, R60_SSPAN_CAP, &unbounded);
                if (capacity_only) r60_mc_probe_stat[19]++;
                else r60_mc_probe_stat[9]++;
                r60_mc_probe_stat[20] = available;
                r60_mc_probe_stat[21] = existing;
                r60_mc_probe_stat[11] = index + 1u;
                r60_mc_probe_stat[7]++;
                return;
            }
        }
        if (macro_record.packet &&
            !r60_mc_packet_preflight(&macro_record, r60_xo, FB_W, FB_H,
                                     &packet_stores)) {
            r60_mc_probe_stat[10]++;
            r60_mc_probe_stat[11] = index + 1u;
            r60_mc_probe_stat[7]++;
            return;
        }
        spans += metadata.spans;
    }
    r60_mc_probe_stat[6]++;
}

/* Fixture-only owner-macro caller.  Planning validates the complete pattern,
 * its baked scalar bookkeeping, aggregate span capacity and every owner packet
 * before publishing a span or pixel.  A miss therefore returns to the
 * established scalar path without side effects.  The baked metadata is the
 * point of OP1 v4: admission no longer looks up and decodes the original art
 * merely to rediscover boxes and clear spans already known by the packer.
 *
 * stats: attempts, admissions, complete plans, patterns drawn, records drawn,
 * impossible post-plan failures, packet stores, KEEP barriers. */
static volatile uint32_t r60_mc_live_stat[8];
#if SPRITE_WORKSET_MACRO_MAX_RECORDS == 0 || SPRITE_WORKSET_MACRO_MAX_RECORDS >= 64
#error "generated macro record bound is invalid"
#endif
static r60_mc_record
    r60_mc_live_records[SPRITE_WORKSET_MACRO_MAX_RECORDS];
static r60_mc_metadata
    r60_mc_live_metadata[SPRITE_WORKSET_MACRO_MAX_RECORDS];
static uint8_t r60_mc_live_first, r60_mc_live_count, r60_mc_live_active;
static uint8_t r60_mc_live_skip_first, r60_mc_live_skip_count;
static volatile uint32_t r60_mc_live_enabled = R60_SPRITE_MACRO_LIVE_INITIAL;

/* These pure immutable-data helpers execute from the page-7 code reservation.
 * The caller holds the same working-set lease that makes their packet data
 * visible, so no additional mapping transition is needed. */
extern unsigned r60_mc_code_draw_packet_ready(
    const r60_mc_record *, unsigned, unsigned, volatile uint32_t *);
extern int r60_mc_code_admit(const r60_mc_view *,
                             const volatile uint16_t *, unsigned, unsigned,
                             r60_mc_pattern *);
extern int r60_mc_code_pattern_record(const r60_mc_pattern *, unsigned,
                                      r60_mc_record *);
extern int r60_mc_code_baked_metadata(const r60_mc_record *, unsigned,
                                      unsigned, unsigned, unsigned, uint32_t *,
                                      unsigned, r60_mc_metadata *);
extern void r60_mc_code_write_baked_spans_ready(
    const r60_mc_record *, unsigned, unsigned, uint32_t *);
extern int r60_mc_code_packet_placement_ready(
    const r60_mc_record *, unsigned, unsigned, unsigned);

static void r60_mc_live_decline(unsigned slot,
                                const volatile uint16_t *record)
{
    unsigned count = 0u;
    while (slot + count < 64u) {
        const volatile uint16_t *r = record + count * 8u;
        if ((r[2] & 0x8000u) ||
            r60_mc_record_key(r) != SPRITE_WORKSET_MACRO_TARGET_KEY)
            break;
        count++;
    }
    r60_mc_live_skip_first = (uint8_t)slot;
    r60_mc_live_skip_count = (uint8_t)count;
}

static int r60_mc_live_plan(unsigned bank, unsigned slot,
                            const volatile uint16_t *record, unsigned active)
{
    unsigned index, total = 0u, existing, at;
    r60_mc_pattern pattern;
    if (!active || (r60_xo & 3u) || !r60_mc_bind() ||
        r60_mc_record_key(record) != r60_mc_probe_view.target_key)
        return 0;
    r60_mc_live_stat[0]++;
    if (!r60_mc_code_admit(&r60_mc_probe_view, record, 64u - slot, 8u,
                           &pattern) || pattern.first)
        return 0;
    r60_mc_live_stat[1]++;
    existing = r60_rowspan_n[bank & 1u];
    for (index = 0; index < pattern.records; index++) {
        unsigned sprite = slot + index;
        r60_mc_record *mr = &r60_mc_live_records[index];
        r60_mc_metadata *mm = &r60_mc_live_metadata[index];
        if (R60_KS_KEEP_DRAW(sprite) || ks_repunch[sprite]) {
            r60_mc_live_stat[7]++;
            return 0;
        }
        if (!r60_mc_code_pattern_record(&pattern, index, mr) ||
            existing + total > R60_SSPAN_CAP ||
            !r60_mc_code_baked_metadata(
                mr, r60_xo, FB_W, FB_H, sprite, 0,
                R60_SSPAN_CAP - existing - total, mm) ||
            (mr->packet && !r60_mc_code_packet_placement_ready(
                mr, r60_xo, FB_W, FB_H)))
            return 0;
        total += mm->spans;
    }

    /* Commit the already-validated scalar clear spans into unpublished tail
     * storage.  This ready writer avoids decoding immutable OP1 metadata a
     * second time; the count remains unpublished until every span is present. */
    at = existing;
    for (index = 0; index < pattern.records; index++) {
        r60_mc_metadata *expected = &r60_mc_live_metadata[index];
        r60_mc_code_write_baked_spans_ready(
            &r60_mc_live_records[index], r60_xo, slot + index,
            &r60_rowspans[bank & 1u][at]);
        at += expected->spans;
    }
    r60_rowspan_n[bank & 1u] = (uint16_t)at;
    r60_mc_live_first = (uint8_t)slot;
    r60_mc_live_count = (uint8_t)pattern.records;
    r60_mc_live_active = 1u;
    r60_mc_live_stat[2]++;
    return 1;
}

static __attribute__((noinline)) int r60_mc_draw_live(
    unsigned bank, unsigned index, const volatile uint16_t *record,
    unsigned active)
{
    unsigned local, stores = 0u, k;
    r60_mc_record *mr;
    r60_mc_metadata *mm;
    if (r60_mc_live_enabled != 1u) return 0;
    if (r60_mc_live_skip_count) {
        if (index >= r60_mc_live_skip_first &&
            index < (unsigned)r60_mc_live_skip_first + r60_mc_live_skip_count) {
            if (index + 1u == (unsigned)r60_mc_live_skip_first +
                              r60_mc_live_skip_count)
                r60_mc_live_skip_count = 0u;
            return 0;
        }
        r60_mc_live_skip_count = 0u;
    }
    if (!r60_mc_live_active) {
        if (!r60_mc_live_plan(bank, index, record, active)) {
            r60_mc_live_decline(index, record);
            return 0;
        }
    }
    if (index < r60_mc_live_first ||
        index >= (unsigned)r60_mc_live_first + r60_mc_live_count) {
        r60_mc_live_active = 0u;
        r60_mc_live_stat[5]++;
        return 0;
    }
    local = index - r60_mc_live_first;
    mr = &r60_mc_live_records[local];
    mm = &r60_mc_live_metadata[local];
    ks_cur = index;
    if (mr->packet)
        stores = r60_mc_code_draw_packet_ready(
            mr, r60_xo, (unsigned)(R60_BANK_OF(mr->palette) << 4),
            (volatile uint32_t *)FB_OVR(0));
    r60_mc_live_stat[6] += stores;
    r60_rowpix += mm->pixels;
    r60_last_box[0] = mm->x0; r60_last_box[1] = mm->x1;
    r60_last_box[2] = mm->y0; r60_last_box[3] = mm->y1;
    {
        volatile uint16_t *rects = R60_RECTS(bank);
        unsigned n = R60_RECTN(bank);
        if (n < 64u) {
            for (k = 0; k < 4u; k++) rects[n * 4u + k] = r60_last_box[k];
            R60_RECTN(bank) = (uint16_t)(n + 1u);
        }
    }
    PHT_B();
    ks_punched[bank & 1u][index] = (uint8_t)R60_PUNCH(r60_last_box);
    PHT_E(1);
    ks_pyr[bank & 1u][index] = ks_punched[bank & 1u][index] &&
        prio_ymax > prio_ymin ?
        (uint16_t)((prio_ymin << 8) | prio_ymax) : 0u;
    for (k = 0; k < 4u; k++)
        ks_box[bank & 1u][index][k] = r60_last_box[k];
    ks_stats[1]++;
    r60_text_touched(r60_last_box);
    r60_mc_live_stat[4]++;
    if (local + 1u == r60_mc_live_count) {
        r60_mc_live_active = 0u;
        r60_mc_live_stat[3]++;
    }
    return 1;
}
