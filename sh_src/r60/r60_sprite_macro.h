#ifndef R60_SPRITE_MACRO_H
#define R60_SPRITE_MACRO_H

#include <stdint.h>

#ifndef R60_MC_API
#define R60_MC_API static inline
#define R60_MC_API_DEFAULT 1
#endif

/* MC1 is a read-only, big-endian directory of owner-safe OP1 patterns.  A
 * fingerprint only rejects obvious misses; admission compares every absolute
 * record field before returning a pattern.  The caller still owns page leases,
 * KEEP/RAW barriers, clipping and foreground-punch execution. */
typedef struct {
    const uint8_t *data;
    uint32_t bytes, target_key, directory_offset, body_offset;
    uint32_t asset_offset, asset_bytes;
    uint16_t patterns, version;
} r60_mc_view;

typedef struct {
    const uint8_t *data, *assets;
    uint32_t bytes, fingerprint;
    uint32_t asset_bytes;
    uint16_t records;
    uint8_t first;
    int16_t anchor_x, anchor_y;
} r60_mc_pattern;

typedef struct {
    int16_t x, y, draw_x, draw_y;
    uint16_t zoom, pixels;
    uint8_t height, palette;
    const uint8_t *packet;
    const uint8_t *metadata;
    uint32_t packet_bytes;
} r60_mc_record;

typedef struct {
    uint16_t x0, x1, y0, y1;
    uint16_t spans;
    uint16_t pixels;
} r60_mc_metadata;

static inline uint16_t r60_mc_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static inline uint32_t r60_mc_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

/* OP1 packet offsets are validated four-byte aligned.  The SH-2 is big-endian,
 * so its hot planner/draw loops can consume a packet word with one aligned
 * load; host tools retain the bytewise endian-independent decoder. */
static inline uint32_t r60_mc_packet_word(const uint8_t *p)
{
#if defined(__sh__)
    return *(const uint32_t *)(const void *)p;
#else
    return r60_mc_u32(p);
#endif
}

static inline int r60_mc_range(uint32_t offset, uint32_t size, uint32_t limit)
{
    return offset <= limit && size <= limit - offset;
}

static inline int r60_mc_packet(const uint8_t *p, uint32_t at,
                                uint32_t bytes, uint32_t limit)
{
    uint32_t end, cursor, row, group;
    unsigned rows;
    if (!bytes || !r60_mc_range(at, bytes, limit) || bytes < 4u) return 0;
    end = at + bytes;
    rows = r60_mc_u16(p + at);
    if (!rows || rows > 224u || !r60_mc_u16(p + at + 2u)) return 0;
    cursor = at + 4u;
    for (row = 0; row < rows; row++) {
        unsigned first, groups;
        if (!r60_mc_range(cursor, 4u, end)) return 0;
        first = p[cursor]; groups = p[cursor + 1u];
        if (first + groups > 256u) return 0;
        cursor += 4u;
        if (!r60_mc_range(cursor, (uint32_t)groups * 4u, end)) return 0;
        for (group = 0; group < (uint32_t)groups * 4u; group++)
            if (p[cursor + group] > 14u) return 0;
        cursor += (uint32_t)groups * 4u;
    }
    return cursor == end;
}

R60_MC_API int r60_mc_open(r60_mc_view *view, const void *blob,
                           uint32_t extent)
{
    const uint8_t *p = (const uint8_t *)blob;
    uint32_t directory_at, body_at, total, previous, previous_fingerprint = 0u;
    uint32_t asset_at, asset_bytes;
    unsigned count, index, version;
    if (!view || !p || extent < 32u || p[0] != 'M' || p[1] != 'C' ||
        p[2] != '1' || p[3])
        return 0;
    version = r60_mc_u16(p + 4u);
    if (version != 1u && version != 2u) return 0;
    count = r60_mc_u16(p + 6u);
    view->target_key = r60_mc_u32(p + 8u);
    directory_at = r60_mc_u32(p + 12u);
    body_at = r60_mc_u32(p + 16u);
    total = r60_mc_u32(p + 20u);
    asset_at = r60_mc_u32(p + 24u);
    asset_bytes = r60_mc_u32(p + 28u);
    if (!count || !view->target_key || directory_at != 32u ||
        body_at != 32u + count * 12u || total != extent ||
        !r60_mc_range(directory_at, (uint32_t)count * 12u, total) ||
        body_at > total ||
        (version == 1u && (asset_at || asset_bytes)) ||
        (version == 2u && ((asset_at & 3u) || asset_at < body_at ||
                           !asset_bytes ||
                           !r60_mc_range(asset_at, asset_bytes, total) ||
                           asset_at + asset_bytes != total)))
        return 0;
    previous = body_at;
    for (index = 0; index < count; index++) {
        const uint8_t *directory = p + directory_at + index * 12u;
        uint32_t fingerprint = r60_mc_u32(directory);
        uint32_t offset = r60_mc_u32(directory + 4u);
        uint32_t size = r60_mc_u32(directory + 8u);
        uint32_t packet_floor;
        unsigned records, record;
        if ((version == 2u && index && fingerprint < previous_fingerprint) ||
            (offset & 3u) || offset < previous || size < 16u ||
            !r60_mc_range(offset, size, total) ||
            (version == 2u && offset + size > asset_at)) return 0;
        previous_fingerprint = fingerprint;
        if (p[offset] != 'O' || p[offset + 1u] != 'P' ||
            p[offset + 2u] != '1' || p[offset + 3u] ||
            r60_mc_u16(p + offset + 4u) != (version == 1u ? 4u : 5u))
            return 0;
        records = r60_mc_u16(p + offset + 6u);
        if (!records || records >= 64u || r60_mc_u32(p + offset + 8u) != size ||
            r60_mc_u32(p + offset + 12u) != fingerprint ||
            16u + records * 24u > size) return 0;
        packet_floor = 16u + records * 24u;
        for (record = 0; record < records; record++) {
            const uint8_t *d = p + offset + 16u + record * 24u;
            uint32_t packet_at = r60_mc_u32(d + 12u);
            uint32_t packet_bytes = r60_mc_u32(d + 16u);
            uint32_t metadata_at = (uint32_t)r60_mc_u16(d + 22u) * 4u;
            uint32_t metadata_bytes = 4u + d[6];
            unsigned row;
            if (!d[6] || r60_mc_u16(d + 4u) > 0x3FFu || d[7] > 0x3Fu ||
                (r60_mc_u16(d + 20u) & 3u) || !metadata_at ||
                (!!packet_at != !!packet_bytes)) return 0;
            if (packet_bytes &&
                ((packet_at & 3u) ||
                 (version == 1u &&
                  (packet_at < packet_floor ||
                   !r60_mc_range(packet_at, packet_bytes, size) ||
                   !r60_mc_packet(p + offset, packet_at, packet_bytes, size))) ||
                 (version == 2u &&
                  (packet_at < asset_at ||
                   !r60_mc_range(packet_at, packet_bytes, total) ||
                   packet_at + packet_bytes > asset_at + asset_bytes ||
                   !r60_mc_packet(p, packet_at, packet_bytes, total)))))
                return 0;
            if (version == 1u && packet_bytes)
                packet_floor = (packet_at + packet_bytes + 3u) & ~3u;
            if ((metadata_at & 3u) ||
                (version == 1u &&
                 (metadata_at < packet_floor ||
                  !r60_mc_range(metadata_at, metadata_bytes, size))) ||
                (version == 2u &&
                 (metadata_at < asset_at ||
                  !r60_mc_range(metadata_at, metadata_bytes, total) ||
                  metadata_at + metadata_bytes > asset_at + asset_bytes)) ||
                !r60_mc_u16((version == 1u ? p + offset : p) + metadata_at) ||
                (version == 1u ? p[offset + metadata_at + 2u] :
                                 p[metadata_at + 2u]) != d[6] ||
                (version == 1u ? p[offset + metadata_at + 3u] :
                                 p[metadata_at + 3u])) return 0;
            for (row = 0; row < d[6]; row++)
                if (!(version == 1u ? p[offset + metadata_at + 4u + row] :
                                      p[metadata_at + 4u + row])) return 0;
            if (version == 1u)
                packet_floor = (metadata_at + metadata_bytes + 3u) & ~3u;
        }
        previous = offset + size;
    }
    view->data = p;
    view->bytes = total;
    view->patterns = (uint16_t)count;
    view->directory_offset = directory_at;
    view->body_offset = body_at;
    view->asset_offset = version == 2u ? asset_at : 0u;
    view->asset_bytes = version == 2u ? asset_bytes : total;
    view->version = (uint16_t)version;
    return 1;
}

static inline uint32_t r60_mc_record_key(const volatile uint16_t *r)
{
    return ((uint32_t)(r[2] & 0x100u) << 20) |
           ((uint32_t)((r[4] >> 8) & 7u) << 24) |
           ((uint32_t)(r[2] & 0xFFu) << 16) | r[3];
}

R60_MC_API int r60_mc_admit(const r60_mc_view *view,
                            const volatile uint16_t *records,
                            unsigned count, unsigned stride_words,
                            r60_mc_pattern *pattern)
{
    unsigned i, begin = 0, end, first = 0, number = 0, ended = 0;
    int anchor_x = 32767, anchor_y = 32767;
    uint32_t fingerprint;
    if (!view || !records || !pattern || count > 64u || stride_words < 6u)
        return 0;
    for (i = 0; i < count; i++) {
        const volatile uint16_t *r = records + i * stride_words;
        if (r[2] & 0x8000u) break;
        if (r60_mc_record_key(r) == view->target_key) {
            int x = (int)(r[1] & 511u) - 184;
            int y = r[0] & 255u;
            if (ended || (r[2] & 0xC000u) ||
                (r[0] >> 8) <= (unsigned)y) return 0;
            if (!number) first = i;
            number++;
            if (x < anchor_x) anchor_x = x;
            if (y < anchor_y) anchor_y = y;
        } else if (number) ended = 1;
    }
    if (!number) return 0;
    fingerprint = number;
    for (i = 0; i < number; i++) {
        const volatile uint16_t *r = records + (first + i) * stride_words;
        int x = (int)(r[1] & 511u) - 184;
        int y = r[0] & 255u;
        unsigned fields[5], field;
        fields[0] = (uint16_t)x;
        fields[1] = (uint16_t)y;
        fields[2] = (uint16_t)((r[0] >> 8) - (unsigned)y);
        fields[3] = r[5] & 0x3FFu;
        fields[4] = r[4] & 0x3Fu;
        for (field = 0; field < 5u; field++) {
            fingerprint = (fingerprint << 5) | (fingerprint >> 27);
            fingerprint ^= fields[field];
        }
    }
    end = view->patterns;
    if (view->version == 2u) {
        unsigned lo = 0u, hi = end;
        while (lo < hi) {
            unsigned mid = lo + (hi - lo) / 2u;
            const uint8_t *entry = view->data + view->directory_offset +
                mid * 12u;
            if (r60_mc_u32(entry) < fingerprint) lo = mid + 1u;
            else hi = mid;
        }
        begin = lo;
    }
    for (i = begin; i < end; i++) {
        const uint8_t *directory = view->data + view->directory_offset + i * 12u;
        uint32_t offset, size;
        unsigned record;
        if (r60_mc_u32(directory) != fingerprint) {
            if (view->version == 2u) break;
            continue;
        }
        offset = r60_mc_u32(directory + 4u);
        size = r60_mc_u32(directory + 8u);
        if (r60_mc_u16(view->data + offset + 6u) != number) continue;
        for (record = 0; record < number; record++) {
            const volatile uint16_t *r = records + (first + record) * stride_words;
            const uint8_t *d = view->data + offset + 16u + record * 24u;
            int x = (int)(r[1] & 511u) - 184;
            int y = r[0] & 255u;
            if ((int16_t)r60_mc_u16(d) != x ||
                (int16_t)r60_mc_u16(d + 2u) != y ||
                r60_mc_u16(d + 4u) != (r[5] & 0x3FFu) ||
                d[6] != (uint8_t)((r[0] >> 8) - (unsigned)y) ||
                d[7] != (uint8_t)(r[4] & 0x3Fu))
                break;
        }
        if (record == number) {
            pattern->data = view->data + offset;
            pattern->assets = view->version == 2u ? view->data : pattern->data;
            pattern->bytes = size;
            pattern->asset_bytes = view->version == 2u ? view->bytes : size;
            pattern->fingerprint = fingerprint;
            pattern->records = (uint16_t)number;
            pattern->first = (uint8_t)first;
            pattern->anchor_x = (int16_t)anchor_x;
            pattern->anchor_y = (int16_t)anchor_y;
            return 1;
        }
    }
    return 0;
}

R60_MC_API int r60_mc_pattern_record(const r60_mc_pattern *pattern,
                                     unsigned index,
                                     r60_mc_record *record)
{
    const uint8_t *d;
    uint32_t packet_at;
    if (!pattern || !record || index >= pattern->records) return 0;
    d = pattern->data + 16u + index * 24u;
    record->x = (int16_t)r60_mc_u16(d);
    record->y = (int16_t)r60_mc_u16(d + 2u);
    record->zoom = r60_mc_u16(d + 4u);
    record->height = d[6];
    record->palette = d[7];
    record->draw_x = (int16_t)r60_mc_u16(d + 8u);
    record->draw_y = (int16_t)r60_mc_u16(d + 10u);
    packet_at = r60_mc_u32(d + 12u);
    record->packet_bytes = r60_mc_u32(d + 16u);
    record->pixels = r60_mc_u16(d + 20u);
    record->packet = record->packet_bytes ? pattern->assets + packet_at : 0;
    record->metadata = pattern->assets +
        (uint32_t)r60_mc_u16(d + 22u) * 4u;
    return 1;
}

/* Reproduce scalar ROWFAST bookkeeping from immutable metadata baked beside
 * each owner packet.  Span ends and box width are relative to the record; a
 * four-pixel-aligned canvas offset therefore relocates them without changing
 * alignment.  Validation completes before the optional span output pass. */
R60_MC_API int r60_mc_baked_metadata(const r60_mc_record *record,
                                     unsigned canvas_left,
                                     unsigned canvas_width,
                                     unsigned screen_height,
                                     unsigned owner,
                                     uint32_t *spans,
                                     unsigned span_capacity,
                                     r60_mc_metadata *metadata)
{
    unsigned row, rows, width, x0, xs, x1, limit;
    if (!record || !record->metadata || !metadata || owner >= 64u ||
        (canvas_left & 3u) || !canvas_width || !screen_height ||
        record->x < 0 || record->y < 0 || !record->height ||
        (unsigned)record->y >= screen_height ||
        record->height > screen_height - (unsigned)record->y)
        return 0;
    rows = record->metadata[2];
    width = r60_mc_u16(record->metadata);
    if (rows != record->height || record->metadata[3] || !width ||
        rows > span_capacity) return 0;
    x0 = canvas_left + (unsigned)record->x;
    xs = x0 & ~3u;
    limit = canvas_left + canvas_width;
    if (x0 < canvas_left || x0 >= limit || width > limit - x0) return 0;
    x1 = x0 + width;
    for (row = 0; row < rows; row++) {
        unsigned groups = record->metadata[4u + row];
        unsigned xe4 = xs + groups * 4u;
        if (!groups || xe4 <= xs || xe4 > ((limit + 3u) & ~3u)) return 0;
    }
    if (spans) for (row = 0; row < rows; row++) {
        unsigned xe4 = xs + record->metadata[4u + row] * 4u;
        spans[row] = ((uint32_t)((unsigned)record->y + row) << 24) |
                     ((uint32_t)owner << 16) |
                     ((uint32_t)(xs >> 2) << 8) | (xe4 >> 2);
    }
    metadata->x0 = (uint16_t)x0;
    metadata->x1 = (uint16_t)x1;
    metadata->y0 = (uint16_t)record->y;
    metadata->y1 = (uint16_t)((unsigned)record->y + rows);
    metadata->spans = (uint16_t)rows;
    metadata->pixels = record->pixels;
    return 1;
}

/* Emit spans after r60_mc_baked_metadata has validated this immutable record.
 * Pattern planning validates every record before publishing any span, so its
 * commit pass should not repeat the metadata bounds walk. */
R60_MC_API void r60_mc_write_baked_spans_ready(
    const r60_mc_record *record, unsigned canvas_left, unsigned owner,
    uint32_t *spans)
{
    unsigned row;
    unsigned rows = record->metadata[2];
    unsigned xs = (canvas_left + (unsigned)record->x) & ~3u;
    for (row = 0; row < rows; row++) {
        unsigned xe4 = xs + record->metadata[4u + row] * 4u;
        spans[row] = ((uint32_t)((unsigned)record->y + row) << 24) |
                     ((uint32_t)owner << 16) |
                     ((uint32_t)(xs >> 2) << 8) | (xe4 >> 2);
    }
}

/* Preflight one already-owner-separated OP1 packet without writing pixels.
 * Empty owner records are represented by no packet and are valid at the
 * pattern level, but callers invoke this helper only for a nonempty packet. */
R60_MC_API int r60_mc_packet_preflight(const r60_mc_record *record,
                                       unsigned canvas_left,
                                       unsigned canvas_width,
                                       unsigned screen_height,
                                       unsigned *stored_words)
{
    uint32_t cursor;
    unsigned row, stores = 0u, rows, width, x, y, limit;
    if (stored_words) *stored_words = 0u;
    if (!record || !record->packet || !record->packet_bytes ||
        !canvas_width || !screen_height ||
        record->x < 0 || record->y < 0)
        return 0;
    x = canvas_left + (unsigned)record->x + (int)record->draw_x;
    y = (unsigned)record->y + (int)record->draw_y;
    limit = canvas_left + canvas_width;
    if ((x & 3u) || x < canvas_left || x >= limit || y >= screen_height ||
        record->packet_bytes < 4u)
        return 0;
    rows = r60_mc_u16(record->packet);
    width = r60_mc_u16(record->packet + 2u);
    if (!rows || !width || (width & 3u) || rows > screen_height - y ||
        width > limit - x)
        return 0;
    cursor = 4u;
    for (row = 0; row < rows; row++) {
        unsigned first, groups, extent, group;
        if (!r60_mc_range(cursor, 4u, record->packet_bytes)) return 0;
        first = record->packet[cursor];
        groups = record->packet[cursor + 1u];
        extent = r60_mc_u16(record->packet + cursor + 2u);
        cursor += 4u;
        if ((!extent && (first || groups)) || (extent & 0x8000u) ||
            extent > width / 4u || first + groups > extent ||
            !r60_mc_range(cursor, groups * 4u, record->packet_bytes))
            return 0;
        for (group = 0; group < groups; group++) {
            if (r60_mc_packet_word(
                    record->packet + cursor + group * 4u)) stores++;
        }
        cursor += groups * 4u;
    }
    if (cursor != record->packet_bytes) return 0;
    if (stored_words) *stored_words = stores;
    return 1;
}

/* Check only the screen placement of a packet already validated by the MC1
 * parser/packer.  Generated ROM data is immutable, so the live planner need
 * not rescan every packet word before the ready draw reads it again. */
R60_MC_API int r60_mc_packet_placement_ready(
    const r60_mc_record *record, unsigned canvas_left,
    unsigned canvas_width, unsigned screen_height)
{
    int x, y;
    unsigned rows, width, limit;
    if (!record || !record->packet || record->packet_bytes < 4u ||
        !canvas_width || !screen_height) return 0;
    x = (int)canvas_left + record->x + record->draw_x;
    y = record->y + record->draw_y;
    limit = canvas_left + canvas_width;
    rows = r60_mc_u16(record->packet);
    width = r60_mc_u16(record->packet + 2u);
    if (x < (int)canvas_left || x >= (int)limit || y < 0 ||
        (x & 3) || !rows || !width || (width & 3u) ||
        (unsigned)y >= screen_height || rows > screen_height - (unsigned)y ||
        width > limit - (unsigned)x)
        return 0;
    return 1;
}

/* Apply a packet that the caller already preflighted and then kept immutable.
 * The live planner uses this after its all-or-nothing pattern validation so it
 * does not walk every packet twice.  Untrusted callers use draw_packet below. */
R60_MC_API int r60_mc_draw_packet_ready(const r60_mc_record *record,
                                        unsigned canvas_left,
                                        unsigned palette_base,
                                        volatile uint32_t *framebuffer,
                                        unsigned stride_words,
                                        unsigned *stored_words)
{
    uint32_t cursor;
    unsigned row, rows, x, y, stores = 0u;
    if (!record || !record->packet || !framebuffer || !stride_words ||
        palette_base > 0xF0u || (palette_base & 0x0Fu)) {
        if (stored_words) *stored_words = 0u;
        return 0;
    }
    x = canvas_left + (unsigned)record->x + (int)record->draw_x;
    y = (unsigned)record->y + (int)record->draw_y;
    rows = r60_mc_u16(record->packet);
    cursor = 4u;
    for (row = 0; row < rows; row++) {
        unsigned first = record->packet[cursor];
        unsigned groups = record->packet[cursor + 1u];
        unsigned group;
        cursor += 4u;
        for (group = 0; group < groups; group++) {
            uint32_t value = r60_mc_packet_word(
                record->packet + cursor + group * 4u);
            if (value) {
                /* Palette bits belong only to opaque pens.  Adding the bank
                 * to all four bytes turns a transparent zero into a nonzero
                 * overwrite-image pixel.  As in bkcopy.s, adding 15 exposes
                 * bit 4 exactly for bytes 1..14; multiplying those independent
                 * flags by the numeric bank cannot carry between bytes. */
                uint32_t opaque = (value + 0x0F0F0F0Fu) & 0x10101010u;
                value |= opaque * (palette_base >> 4);
                framebuffer[(y + row) * stride_words + x / 4u +
                            first + group] = value;
                stores++;
            }
        }
        cursor += groups * 4u;
    }
    if (stored_words) *stored_words = stores;
    return 1;
}

/* Safe public application: validate the complete packet before its first
 * store.  The packet stores logical pens and palette bits are added only to
 * opaque bytes, exactly like the established baked-row copier. */
R60_MC_API int r60_mc_draw_packet(const r60_mc_record *record,
                                  unsigned canvas_left,
                                  unsigned canvas_width,
                                  unsigned screen_height,
                                  unsigned palette_base,
                                  volatile uint32_t *framebuffer,
                                  unsigned stride_words,
                                  unsigned *stored_words)
{
    unsigned stores;
    if (!framebuffer || !stride_words ||
        !r60_mc_packet_preflight(record, canvas_left, canvas_width,
                                 screen_height, &stores) ||
        stride_words < (canvas_width + 3u) / 4u)
        goto fail;
    return r60_mc_draw_packet_ready(record, canvas_left, palette_base,
                                    framebuffer, stride_words, stored_words);
fail:
    if (stored_words) *stored_words = 0u;
    return 0;
}

static inline unsigned r60_mc_clip_cut(unsigned zoom_phase,
                                       unsigned remaining)
{
    unsigned seed = (zoom_phase & 15u) * 4u;
    unsigned words = (64u * (remaining - 1u) + seed) /
                     (4u * (64u - zoom_phase)) + 1u;
    return 4u * words - ((seed + 4u * words * zoom_phase) >> 6);
}

/* Rebuild the metadata produced by the original AB1/ZB1 draw.  Macro owner
 * packets replace framebuffer stores only: clear spans, the dirty rectangle,
 * and r60_rowpix must remain those of the original record.  The source is the
 * original big-endian pose (u16 rows, u16 width, then row packets).  Positive
 * row extents are AB1 word counts; negative extents are exact ZB1 pixels.
 *
 * This helper validates and counts in a first pass, then writes spans in a
 * second pass.  A malformed packet or short output has no visible side effect.
 * canvas_left is r60_xo, canvas_width is the visible width, and owner is the
 * original sprite-list owner stored in span bits 16..21. */
R60_MC_API int r60_mc_source_metadata(const r60_mc_record *record,
                                         const uint8_t *source,
                                         uint32_t source_bytes,
                                         unsigned canvas_left,
                                         unsigned canvas_width,
                                         unsigned screen_height,
                                         unsigned owner,
                                         uint32_t *spans,
                                         unsigned span_capacity,
                                         r60_mc_metadata *metadata)
{
    uint32_t cursor;
    unsigned pass, row, count = 0, max_x = 0, pixels = 0;
    unsigned x0, xs, limit, rows, width, hz, remaining, cut, clipped;
    if (!record || !source || !metadata || !canvas_width || !screen_height ||
        owner >= 64u || record->y < 0 || record->x < 0 || !record->height ||
        (unsigned)record->y >= screen_height ||
        record->height > screen_height - (unsigned)record->y ||
        canvas_left > 0x3FCu || canvas_width > 0x3FCu - canvas_left)
        return 0;
    x0 = canvas_left + (unsigned)record->x;
    limit = canvas_left + canvas_width;
    if (x0 < canvas_left || x0 >= limit || source_bytes < 4u) return 0;
    rows = r60_mc_u16(source);
    width = r60_mc_u16(source + 2u);
    if (rows < record->height || !width) return 0;
    xs = x0 & ~3u;
    hz = record->zoom & 31u;
    remaining = limit - x0;
    cut = r60_mc_clip_cut(hz, remaining);
    clipped = record->zoom && x0 + width > limit;
    for (pass = 0; pass < 2u; pass++) {
        cursor = 4u;
        count = 0u;
        max_x = 0u;
        pixels = 0u;
        for (row = 0; row < record->height; row++) {
            unsigned first, groups, encoded, length, logical, xe, xe4;
            unsigned total_groups, process, group;
            if (!r60_mc_range(cursor, 4u, source_bytes)) return 0;
            first = source[cursor];
            groups = source[cursor + 1u];
            encoded = r60_mc_u16(source + cursor + 2u);
            cursor += 4u;
            if (!r60_mc_range(cursor, groups * 4u, source_bytes)) return 0;
            if (record->zoom) {
                if (!(encoded & 0x8000u) || !encoded) return 0;
                length = 0x10000u - encoded;
                if (!length || length > width) return 0;
                total_groups = (length + 3u) / 4u;
                if (first + groups > total_groups) return 0;
                if (clipped) {
                    unsigned upper = ((limit + 3u) & ~3u) - x0;
                    unsigned byte_limit, end, carry = 0u, shift = (x0 & 3u) * 8u;
                    logical = length < cut ? length : cut;
                    byte_limit = logical < upper ? logical : upper;
                    total_groups = (byte_limit + 3u) / 4u;
                    end = first + groups;
                    if (end > total_groups) end = total_groups;
                    process = end > first ? end - first : 0u;
                    for (group = 0; group < process; group++) {
                        uint32_t value = r60_mc_u32(source + cursor + group * 4u);
                        uint32_t out;
                        if (first + group + 1u == total_groups) {
                            switch (byte_limit & 3u) {
                            case 1u: value &= 0xFF000000u; break;
                            case 2u: value &= 0xFFFF0000u; break;
                            case 3u: value &= 0xFFFFFF00u; break;
                            default: break;
                            }
                        }
                        if (shift) {
                            out = carry | (value >> shift);
                            carry = value << (32u - shift);
                        } else { out = value; carry = 0u; }
                        if (hz ? out != 0u : value != 0u) pixels += 4u;
                    }
                    if (hz && carry && xs + 4u * end < limit) pixels += 4u;
                } else {
                    logical = length;
                    process = groups;
                    for (group = 0; group < process; group++)
                        if (r60_mc_u32(source + cursor + group * 4u)) pixels += 4u;
                }
            } else {
                unsigned capacity = (remaining + 3u) >> 2;
                if (!encoded || (encoded & 0x8000u) ||
                    encoded > (width + 3u) / 4u ||
                    first + groups > encoded) return 0;
                process = encoded < capacity ? encoded : capacity;
                process = process > first ? process - first : 0u;
                if (process > groups) process = groups;
                for (group = 0; group < process; group++)
                    if (r60_mc_u32(source + cursor + group * 4u)) pixels += 4u;
                logical = 4u * (encoded < capacity ? encoded : capacity);
            }
            cursor += groups * 4u;
            xe = x0 + logical;
            if (xe > max_x) max_x = xe;
            xe4 = (xe + 3u) & ~3u;
            if (xe4 > xs) {
                if (pass && spans)
                    spans[count] = ((uint32_t)((unsigned)record->y + row) << 24) |
                                   ((uint32_t)owner << 16) |
                                   ((uint32_t)(xs >> 2) << 8) | (xe4 >> 2);
                count++;
            }
        }
        if (!pass && (count > span_capacity || pixels != record->pixels))
            return 0;
    }
    metadata->x0 = (uint16_t)x0;
    metadata->x1 = (uint16_t)(max_x < limit ? max_x : limit);
    metadata->y0 = (uint16_t)record->y;
    metadata->y1 = (uint16_t)((unsigned)record->y + record->height);
    metadata->spans = (uint16_t)count;
    metadata->pixels = (uint16_t)pixels;
    return metadata->x1 > metadata->x0;
}

#ifdef R60_MC_API_DEFAULT
#undef R60_MC_API_DEFAULT
#undef R60_MC_API
#endif

#endif
