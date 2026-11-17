/* Optional standalone slave closure. Compile explicitly with -x c -fno-lto;
 * .inc intentionally avoids every normal .c wildcard. There is NO caller yet.
 * Page mode requires the page7 GO lease through all callback returns; fixed
 * mode stays resident. Both retain packet/workspace leases when used later.
 */
#include "r60_workingset_data.h"
#if defined(R60_WORKSET_CODE) && defined(R60_WORKSET_CODE_FIXED)
#error "select exactly one compositor placement"
#endif
#ifdef R60_WORKSET_CODE_FIXED
#if R60_WORKSET_CODE_FIXED != 1
#error "fixed compositor mode must be1"
#endif
#else
#if SPRITE_WORKSET_PAGE_BYTES != 0x80000u || SPRITE_WORKSET_GUARD_BYTES != 64u || SPRITE_WORKSET_CODE_CAPACITY_BYTES != R60_WORKSET_CODE || SPRITE_WORKSET_CODE_OFFSET != 0x80000u-64u-R60_WORKSET_CODE || SPRITE_WORKSET_CODE_ADDRESS != 0x02400000u-64u-R60_WORKSET_CODE
#error "page compositor metadata does not match linker reservation"
#endif
#endif
#include "r60_sprite_macro.h"

/* The macro renderer owns page 7 for the complete planner/draw interval.  Put
 * its self-contained immutable-data helpers in that existing code reservation
 * instead of linking the otherwise-unused block compositor.  These wrappers
 * also give the resident caller ordinary external symbols; the header remains
 * the single implementation used by host tests. */
#define R60_MC_CODE __attribute__((used,noinline,noclone,section(".r60wscomp.text")))

/* Trusted OP1 packet-store leaf.  The all-or-nothing planner has already
 * validated the record, placement, palette and immutable packet bounds.  Keep
 * its four inputs in the SH-2 argument registers and return the physical store
 * count in r0; the former C wrapper passed two more stack arguments, saved
 * seven registers and repeated constant checks once per owner record.
 *
 * ABI: r4 record, r5 canvas_left, r6 palette_base, r7 framebuffer; r0 stores.
 * The framebuffer is fixed at 320 pixels / 80 longwords per row by this R60
 * closure.  The public portable C packet function remains the checked path for
 * host tests and any untrusted caller. */
__asm__(
".section .r60wscomp.text,\"ax\"\n"
".align 2\n"
".global _r60_mc_code_draw_packet_ready\n"
".type _r60_mc_code_draw_packet_ready,@function\n"
"_r60_mc_code_draw_packet_ready:\n"
" mov.l r8,@-r15\n"
" mov.l r9,@-r15\n"
" mov.l r10,@-r15\n"
" mov.l r11,@-r15\n"
" mov.l @(16,r4),r8\n"       /* packet */
" mov.w @r4,r9\n"            /* x */
" mov.w @(4,r4),r0\n"        /* draw_x */
" add r0,r9\n"
" add r5,r9\n"
" shlr2 r9\n"                /* x / 4 */
" mov.w @(2,r4),r0\n"        /* y */
" mov r0,r2\n"
" mov.w @(6,r4),r0\n"        /* draw_y */
" add r2,r0\n"
" mov #80,r1\n"
" mul.l r1,r0\n"
" sts macl,r0\n"
" add r9,r0\n"
" shll2 r0\n"
" add r7,r0\n"
" mov r0,r9\n"               /* first row destination */
" mov r6,r3\n"
" shlr2 r3\n"
" shlr2 r3\n"                /* numeric palette bank */
" mov.w @r8,r11\n"
" extu.w r11,r11\n"          /* rows */
" add #4,r8\n"
" mov.l .Lmc_opaque_mask,r5\n"
" mov.l .Lmc_row_bytes,r6\n"
" mov #0,r10\n"              /* stores */
".Lmc_row:\n"
" mov.b @r8,r0\n"
" extu.b r0,r0\n"            /* first group */
" mov r0,r2\n"
" mov.b @(1,r8),r0\n"
" extu.b r0,r1\n"            /* groups */
" add #4,r8\n"
" shll2 r2\n"
" add r9,r2\n"
" tst r1,r1\n"
" bt .Lmc_row_next\n"
".Lmc_group:\n"
" mov.l @r8+,r4\n"
" tst r4,r4\n"
" bt .Lmc_skip_store\n"
" mov r4,r7\n"
" mov.l .Lmc_opaque_bias,r0\n"
" add r0,r7\n"
" and r5,r7\n"
" mul.l r3,r7\n"
" sts macl,r7\n"
" or r7,r4\n"
" mov.l r4,@r2\n"
" add #1,r10\n"
".Lmc_skip_store:\n"
" add #4,r2\n"
" dt r1\n"
" bf .Lmc_group\n"
".Lmc_row_next:\n"
" add r6,r9\n"
" dt r11\n"
" bf .Lmc_row\n"
" mov r10,r0\n"
" mov.l @r15+,r11\n"
" mov.l @r15+,r10\n"
" mov.l @r15+,r9\n"
" rts\n"
" mov.l @r15+,r8\n"
".align 2\n"
".Lmc_opaque_mask: .long 0x10101010\n"
".Lmc_opaque_bias: .long 0x0f0f0f0f\n"
".Lmc_row_bytes: .long 320\n"
".size _r60_mc_code_draw_packet_ready,.-_r60_mc_code_draw_packet_ready\n"
".previous\n"
);

R60_MC_CODE int r60_mc_code_admit(
    const r60_mc_view *view, const volatile uint16_t *records,
    unsigned count, unsigned stride_words, r60_mc_pattern *pattern)
{
    return r60_mc_admit(view, records, count, stride_words, pattern);
}

R60_MC_CODE int r60_mc_code_pattern_record(
    const r60_mc_pattern *pattern, unsigned index, r60_mc_record *record)
{
    return r60_mc_pattern_record(pattern, index, record);
}

R60_MC_CODE int r60_mc_code_baked_metadata(
    const r60_mc_record *record, unsigned canvas_left, unsigned canvas_width,
    unsigned screen_height, unsigned owner, uint32_t *spans,
    unsigned span_capacity, r60_mc_metadata *metadata)
{
    return r60_mc_baked_metadata(record, canvas_left, canvas_width,
                                 screen_height, owner, spans, span_capacity,
                                 metadata);
}

R60_MC_CODE void r60_mc_code_write_baked_spans_ready(
    const r60_mc_record *record, unsigned canvas_left, unsigned owner,
    uint32_t *spans)
{
    r60_mc_write_baked_spans_ready(record, canvas_left, owner, spans);
}

R60_MC_CODE int r60_mc_code_packet_placement_ready(
    const r60_mc_record *record, unsigned canvas_left, unsigned canvas_width,
    unsigned screen_height)
{
    return r60_mc_packet_placement_ready(record, canvas_left, canvas_width,
                                         screen_height);
}
#undef R60_MC_CODE
