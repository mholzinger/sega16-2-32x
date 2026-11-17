#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "r60_sprite_macro.h"
#include "r60_workingset_clip.h"

static void put16(uint8_t *p, unsigned value)
{
    p[0] = (uint8_t)(value >> 8); p[1] = (uint8_t)value;
}

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24); p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8); p[3] = (uint8_t)value;
}

static uint32_t fingerprint(void)
{
    const unsigned fields[] = {40, 20, 4, 1, 2, 44, 20, 4, 1, 3};
    uint32_t value = 2;
    unsigned i;
    for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        value = (value << 5) | (value >> 27);
        value ^= fields[i];
    }
    return value;
}

static void record(uint16_t *r, unsigned x, unsigned palette)
{
    memset(r, 0, 16);
    r[0] = 20u | (24u << 8); r[1] = (uint16_t)(x + 184u);
    r[2] = 0x14; r[3] = 0x01D0; r[4] = (uint16_t)(0x0300u | palette);
    r[5] = 1;
}

static void metadata_differential(void)
{
    uint32_t state = 0x12345678u;
    unsigned trial;
    for (trial = 0; trial < 256u; trial++) {
        uint8_t source[4u + 8u * 20u] = {0};
        uint32_t native[8u * 20u] = {0}, expected[8], actual[8];
        uint8_t destination[320u * 8u + 16u] = {0};
        uintptr_t args[16] = {0};
        r60_mc_record r = {0};
        r60_mc_metadata m;
        unsigned rows = trial % 7u + 1u, width = trial % 61u + 8u;
        unsigned x = 313u + trial % 7u, owner = trial % 63u;
        unsigned cursor = 4u, words = 0u, row;
        unsigned canvas_left = 256u, limit = canvas_left + 320u;
        unsigned x0 = canvas_left + x, xs = x0 & ~3u;
        x0 = canvas_left + x; xs = x0 & ~3u;
        put16(source, rows); put16(source + 2u, width);
        for (row = 0; row < rows; row++) {
            unsigned length, total_groups, first, groups, group;
            state = state * 1664525u + 1013904223u;
            length = 1u + state % width;
            total_groups = (length + 3u) / 4u;
            first = state % total_groups;
            groups = 1u + (state >> 8) % (total_groups - first);
            source[cursor] = (uint8_t)first;
            source[cursor + 1u] = (uint8_t)groups;
            put16(source + cursor + 2u, 0x10000u - length);
            native[words++] = (first << 24) | (groups << 16) |
                              (0x10000u - length);
            cursor += 4u;
            for (group = 0; group < groups; group++) {
                uint32_t data = 0x01020304u + group;
                put32(source + cursor, data); cursor += 4u;
                native[words++] = data;
            }
        }
        r.x = (int16_t)x; r.y = (int16_t)(trial % (224u - rows));
        r.height = (uint8_t)rows; r.zoom = (uint16_t)(1u + trial % 31u);
        args[0] = (uintptr_t)native; args[1] = (uintptr_t)destination;
        args[2] = rows; args[3] = 0u; args[5] = x0; args[6] = xs;
        args[7] = limit; args[8] = (uintptr_t)expected;
        args[9] = (uintptr_t)(expected + 8); args[10] =
            ((uint32_t)owner << 16) | ((uint32_t)(xs >> 2) << 8);
        args[11] = (unsigned)r.y; args[12] = x0 & 3u;
        args[13] = canvas_left + 320u + 512u;
        r60_wc_rows(args, r.zoom & 31u);
        r.pixels = (uint16_t)args[14];
        if (!r60_mc_source_metadata(&r, source, cursor, canvas_left, 320,
                                    224, owner, actual, 8, &m)) {
            fprintf(stderr, "metadata differential trial %u x=%u z=%u pixels=%u\n",
                    trial, x, r.zoom, r.pixels);
            assert(0);
        }
        assert(m.spans == (unsigned)((uint32_t *)args[8] - expected));
        assert(!memcmp(actual, expected, m.spans * sizeof(*actual)));
        assert(m.x0 == x0 && m.x1 == (args[15] < limit ? args[15] : limit));
        assert(m.y0 == (unsigned)r.y && m.y1 == (unsigned)r.y + rows);
        assert(m.pixels == args[14]);
    }
}

static void metadata_ab_differential(void)
{
    uint32_t state = 0x89ABCDEFu;
    unsigned trial;
    for (trial = 0; trial < 256u; trial++) {
        uint8_t source[4u + 8u * 20u] = {0};
        uint32_t expected[8], actual[8];
        r60_mc_record r = {0};
        r60_mc_metadata m;
        unsigned rows = trial % 7u + 1u, width_words = trial % 16u + 1u;
        unsigned x = 280u + trial % 40u, owner = trial % 63u;
        unsigned cursor = 4u, row, pixels = 0u, max_x = 0u;
        put16(source, rows); put16(source + 2u, width_words * 4u);
        for (row = 0; row < rows; row++) {
            unsigned extent, first, groups, capacity, process, group, xe;
            state = state * 1664525u + 1013904223u;
            extent = 1u + state % width_words;
            first = state % extent;
            groups = 1u + (state >> 8) % (extent - first);
            source[cursor] = (uint8_t)first;
            source[cursor + 1u] = (uint8_t)groups;
            put16(source + cursor + 2u, extent); cursor += 4u;
            capacity = (320u - x + 3u) >> 2;
            process = extent < capacity ? extent : capacity;
            process = process > first ? process - first : 0u;
            if (process > groups) process = groups;
            for (group = 0; group < groups; group++) {
                uint32_t data = ((group + row + trial) % 3u) ?
                    0x01020304u + group : 0u;
                put32(source + cursor, data); cursor += 4u;
                if (group < process && data) pixels += 4u;
            }
            xe = 256u + x + 4u * (extent < capacity ? extent : capacity);
            if (xe > max_x) max_x = xe;
            expected[row] = ((uint32_t)(20u + row) << 24) |
                            ((uint32_t)owner << 16) |
                            ((uint32_t)((256u + x) >> 2) << 8) |
                            ((xe + 3u) >> 2);
        }
        r.x = (int16_t)x; r.y = 20; r.height = (uint8_t)rows;
        r.zoom = 0; r.pixels = (uint16_t)pixels;
        assert(r60_mc_source_metadata(&r, source, cursor, 256, 320, 224,
                                      owner, actual, 8, &m));
        assert(m.spans == rows && m.pixels == pixels &&
               m.x0 == 256u + x && m.x1 == (max_x < 576u ? max_x : 576u));
        assert(!memcmp(actual, expected, rows * sizeof(*actual)));
    }
}

int main(int argc, char **argv)
{
    uint8_t blob[136] = {0};
    uint8_t shared[136];
    uint8_t saved;
    uint16_t table[24] = {0};
    uint16_t terminated[32] = {0};
    r60_mc_view view;
    r60_mc_pattern pattern;
    r60_mc_record macro_record;
    r60_mc_metadata metadata;
    uint8_t source[28] = {0};
    uint8_t packet_with_empty_row[16] = {
        0, 2, 0, 4, 0, 1, 0, 1, 1, 2, 3, 4, 0, 0, 0, 0
    };
    uint32_t spans[4] = {
        0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu
    };
    uint32_t ready_spans[4] = {0};
    uint32_t *framebuffer;
    unsigned stores;
    uint32_t fp = fingerprint();

    memcpy(blob, "MC1", 3); put16(blob + 4, 1); put16(blob + 6, 1);
    put32(blob + 8, 0x031401D0u); put32(blob + 12, 32);
    put32(blob + 16, 44); put32(blob + 20, sizeof(blob));
    put32(blob + 32, fp); put32(blob + 36, 44); put32(blob + 40, 92);
    memcpy(blob + 44, "OP1", 3); put16(blob + 48, 4); put16(blob + 50, 2);
    put32(blob + 52, 92); put32(blob + 56, fp);
    /* Record 0: absolute (40,20), zoom 1, source height 4, palette 2. */
    put16(blob + 60, 40); put16(blob + 62, 20); put16(blob + 64, 1);
    blob[66] = 4; blob[67] = 2; put16(blob + 68, 0); put16(blob + 70, 0);
    put32(blob + 72, 64); put32(blob + 76, 12); put16(blob + 80, 8);
    put16(blob + 82, 19); /* pattern-relative metadata offset 76 / 4. */
    /* Record 1: absolute x 44, no owner packet. */
    put16(blob + 84, 44); put16(blob + 86, 20); put16(blob + 88, 1);
    blob[90] = 4; blob[91] = 3; put16(blob + 104, 8);
    put16(blob + 106, 21); /* pattern-relative metadata offset 84 / 4. */
    /* One-row packet at OP1-relative offset 64. */
    put16(blob + 108, 1); put16(blob + 110, 4);
    blob[112] = 0; blob[113] = 1; put16(blob + 114, 1);
    blob[116] = 1; blob[117] = 2; blob[118] = 3; blob[119] = 4;
    /* Exact scalar box width and four relative aligned row ends per record. */
    put16(blob + 120, 4); blob[122] = 4;
    blob[124] = blob[125] = blob[126] = blob[127] = 1;
    put16(blob + 128, 4); blob[130] = 4;
    blob[132] = blob[133] = blob[134] = blob[135] = 1;

    assert(r60_mc_open(&view, blob, sizeof(blob)));
    assert(view.patterns == 1 && view.target_key == 0x031401D0u);
    record(table, 40, 2); record(table + 8, 44, 3);
    assert(r60_mc_admit(&view, table, 2, 8, &pattern));
    assert(pattern.first == 0 && pattern.records == 2);
    assert(pattern.anchor_x == 40 && pattern.anchor_y == 20);
    memcpy(terminated, table, 16u * sizeof(*table));
    terminated[2u * 8u + 2u] = 0x8000u;
    record(terminated + 3u * 8u, 48, 4); /* stale RAM after terminator */
    assert(r60_mc_admit(&view, terminated, 4, 8, &pattern));
    assert(pattern.first == 0 && pattern.records == 2);
    assert(r60_mc_pattern_record(&pattern, 0, &macro_record));
    assert(macro_record.x == 40 && macro_record.y == 20 &&
           macro_record.zoom == 1 && macro_record.height == 4 &&
           macro_record.palette == 2 && macro_record.pixels == 8 &&
           macro_record.packet && macro_record.packet_bytes == 12 &&
           macro_record.metadata == blob + 120);
    assert(r60_mc_baked_metadata(&macro_record, 256, 320, 224, 7,
                                 spans, 4, &metadata));
    assert(metadata.x0 == 296 && metadata.x1 == 300 &&
           metadata.y0 == 20 && metadata.y1 == 24 &&
           metadata.spans == 4 && metadata.pixels == 8);
    assert(spans[0] == ((20u << 24) | (7u << 16) | (74u << 8) | 75u));
    r60_mc_write_baked_spans_ready(&macro_record, 256, 7, ready_spans);
    assert(!memcmp(ready_spans, spans, sizeof(spans)));
    framebuffer = (uint32_t *)malloc(80u * 224u * sizeof(*framebuffer));
    assert(framebuffer);
    memset(framebuffer, 0xA5, 80u * 224u * sizeof(*framebuffer));
    assert(r60_mc_draw_packet(&macro_record, 0, 320, 224, 0x20,
                              framebuffer, 80, &stores));
    assert(stores == 1u && framebuffer[20u * 80u + 10u] == 0x21222324u);
    framebuffer[20u * 80u + 10u] = 0xA5A5A5A5u;
    assert(r60_mc_packet_placement_ready(&macro_record, 0, 320, 224));
    assert(r60_mc_packet_preflight(&macro_record, 0, 320, 224, &stores));
    assert(r60_mc_draw_packet_ready(&macro_record, 0, 0x20,
                                    framebuffer, 80, &stores));
    assert(stores == 1u && framebuffer[20u * 80u + 10u] == 0x21222324u);
    blob[116] = 0; blob[117] = 2; blob[118] = 0; blob[119] = 4;
    assert(r60_mc_draw_packet(&macro_record, 0, 320, 224, 0x20,
                              framebuffer, 80, &stores));
    assert(stores == 1u && framebuffer[20u * 80u + 10u] == 0x00220024u);
    blob[116] = 1; blob[117] = 2; blob[118] = 3; blob[119] = 4;
    macro_record.draw_x = -41; /* unaligned/outside: fail without a store */
    framebuffer[20u * 80u + 10u] = 0xA5A5A5A5u;
    assert(!r60_mc_packet_placement_ready(&macro_record, 0, 320, 224));
    assert(!r60_mc_draw_packet(&macro_record, 0, 320, 224, 0x20,
                               framebuffer, 80, &stores));
    assert(stores == 0u && framebuffer[20u * 80u + 10u] == 0xA5A5A5A5u);
    macro_record.draw_x = 0;
    free(framebuffer);
    macro_record.packet = packet_with_empty_row;
    macro_record.packet_bytes = sizeof(packet_with_empty_row);
    macro_record.x = 40; macro_record.y = 20;
    assert(r60_mc_packet_preflight(&macro_record, 0, 320, 224, &stores));
    assert(stores == 1u);
    assert(r60_mc_pattern_record(&pattern, 1, &macro_record));
    assert(!macro_record.packet && !macro_record.packet_bytes);

    /* MC1 v2 keeps the same descriptors but resolves packet and metadata
     * offsets from one cross-pattern immutable pool. */
    memcpy(shared, blob, sizeof(shared));
    put16(shared + 4, 2); put32(shared + 24, 108); put32(shared + 28, 28);
    put32(shared + 40, 64); put16(shared + 48, 5); put32(shared + 52, 64);
    put32(shared + 72, 108); put16(shared + 82, 30);
    put16(shared + 106, 32);
    assert(r60_mc_open(&view, shared, sizeof(shared)) && view.version == 2u);
    assert(r60_mc_admit(&view, table, 2, 8, &pattern));
    assert(r60_mc_pattern_record(&pattern, 0, &macro_record));
    assert(macro_record.packet == shared + 108 &&
           macro_record.metadata == shared + 120);
    assert(r60_mc_pattern_record(&pattern, 1, &macro_record));
    assert(!macro_record.packet && macro_record.metadata == shared + 128);

    /* Original right-clipped ZB1 metadata is retained even when its pixels
     * will be supplied by an owner packet.  x=316 on a 320-wide canvas. */
    macro_record.x = 316; macro_record.y = 20; macro_record.height = 2;
    macro_record.zoom = 1; macro_record.pixels = 4;
    put16(source, 2); put16(source + 2, 12);
    source[4] = 0; source[5] = 1; put16(source + 6, 0xFFF8u);
    put32(source + 8, 0x01020304u);
    source[12] = 1; source[13] = 2; put16(source + 14, 0xFFF4u);
    put32(source + 16, 0x01020304u); put32(source + 20, 0x05060708u);
    assert(r60_mc_source_metadata(&macro_record, source, 24, 256, 320,
                                  224, 7, spans, 2, &metadata));
    assert(metadata.x0 == 572 && metadata.x1 == 576 &&
           metadata.y0 == 20 && metadata.y1 == 22 &&
           metadata.spans == 2 && metadata.pixels == 4);
    assert(spans[0] == ((20u << 24) | (7u << 16) | (143u << 8) | 144u));
    assert(spans[1] == ((21u << 24) | (7u << 16) | (143u << 8) | 144u));
    spans[0] = spans[1] = 0xDEADBEEFu;
    assert(!r60_mc_source_metadata(&macro_record, source, 24, 256, 320,
                                   224, 7, spans, 1, &metadata));
    assert(spans[0] == 0xDEADBEEFu && spans[1] == 0xDEADBEEFu);
    macro_record.zoom = 0; macro_record.x = 40; macro_record.pixels = 12;
    put16(source + 6, 2); put16(source + 14, 3); /* AB1 word extents. */
    assert(r60_mc_source_metadata(&macro_record, source, 24, 256, 320,
                                  224, 7, spans, 2, &metadata));
    assert(metadata.x0 == 296 && metadata.x1 == 308 && metadata.pixels == 12);
    put16(source + 6, 0xFFF8u); put16(source + 14, 0xFFF4u);
    metadata_differential();
    metadata_ab_differential();

    table[9]++; /* absolute placement differs */
    assert(!r60_mc_admit(&view, table, 2, 8, &pattern));
    table[9]--;
    table[12] ^= 1; /* palette differs */
    assert(!r60_mc_admit(&view, table, 2, 8, &pattern));
    table[12] ^= 1;
    table[2] |= 0x4000u; /* skipped records are never macro-admissible */
    assert(!r60_mc_admit(&view, table, 2, 8, &pattern));
    table[2] &= (uint16_t)~0x4000u;

    saved = blob[119]; blob[119] = 15; /* serialized pen 15 is forbidden */
    assert(!r60_mc_open(&view, blob, sizeof(blob))); blob[119] = saved;
    put32(blob + 76, 13); /* descriptor length differs from packet */
    assert(!r60_mc_open(&view, blob, sizeof(blob))); put32(blob + 76, 12);
    put32(blob + 32, fp ^ 1u); /* directory/header fingerprint mismatch */
    assert(!r60_mc_open(&view, blob, sizeof(blob)));
    put32(blob + 32, fp);
    put16(blob + 6, 8); put32(blob + 16, 128); /* directory exceeds total */
    assert(!r60_mc_open(&view, blob, sizeof(blob)));
    put16(blob + 6, 1); put32(blob + 16, 44);
    if (argc >= 2) {
        FILE *file = fopen(argv[1], "rb");
        uint8_t *external;
        long size;
        assert(file && fseek(file, 0, SEEK_END) == 0);
        size = ftell(file); assert(size > 0 && fseek(file, 0, SEEK_SET) == 0);
        external = (uint8_t *)malloc((size_t)size); assert(external);
        assert(fread(external, 1, (size_t)size, file) == (size_t)size);
        assert(fclose(file) == 0);
        assert(r60_mc_open(&view, external, (uint32_t)size));
        assert(view.patterns && view.target_key == 0x031401D0u);
        if (argc == 3) {
            FILE *sprites = fopen(argv[2], "rb");
            uint8_t raw[1024];
            uint16_t words[512];
            unsigned slot, found = 0;
            assert(sprites && fread(raw, 1, sizeof(raw), sprites) == sizeof(raw));
            assert(fclose(sprites) == 0);
            for (slot = 0; slot < 512u; slot++)
                words[slot] = (uint16_t)((raw[slot * 2u] << 8) |
                                         raw[slot * 2u + 1u]);
            for (slot = 0; slot < 64u; slot++) {
                if (r60_mc_admit(&view, words + slot * 8u, 64u - slot, 8u,
                                 &pattern) && !pattern.first) {
                    found++;
                    assert(pattern.records && pattern.records < 64u);
                }
            }
            assert(found == 1u);
        }
        free(external);
    }
    return 0;
}
