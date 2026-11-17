/* r60_sprite.inc -- r60_sprite: one sprite record plotted live from the S16 art.
 * Part of r60_main.c: included there, in this order, so the compiler sees one unit. */
/* ROWFAST is the line's row loop (derived from the flags on the line, not a flag of its own): the
 * PLOT5 row functions with ROWCLR spans and the RECTCLR/KEEPSPR box, zoom by the word form, no
 * priority mask and no probe plotter. Any other set takes the loop with every form below it. */
#define R60_ROWFAST   /* R60ROWFAST=1 opts in (2026-10-23): identity-gated and 0.46 -> 0.51 passes/vint in the fight,
                       * but its rh_g tape run parked the slave at vint ~32750 (s_main's DRAWSEQ test sits before
                       * the FMDOWN timeout close); off the line until that is understood */
#define R60SPRCODE R60CODE
/* the plotter's per-core reads: under MBAND through the draw context (the master must never
 * read the slave's cached r60_xo nor write its box / pixel count), else the slave statics */
#define XO_ r60_xo
#define RP_ r60_rowpix
#define LB_ r60_last_box
R60SPRCODE static void r60_sprite(const volatile uint16_t *e, unsigned ylo, unsigned yhi,
                               volatile uint32_t *mask, unsigned bank, unsigned mlist)
{
    unsigned bx0 = FB_W + 512u, bx1 = 0, by0 = FB_H, by1 = 0;
    int      top   = e[0] & 0xFF, bot = e[0] >> 8;
    int      xpos  = (int)(e[1] & 0x1FF) - 184 + (int)XO_;
    uint16_t d2    = e[2];
    int      pitch = (int)(signed char)(d2 & 0xFF);
    int      flip  = d2 & 0x100;
    unsigned addr  = e[3];
    const uint16_t *sd = altbeast_sprites + R60_SPR_BANK((e[4] >> 8) & 0xF) * 0x10000u;
    unsigned base  = (unsigned)(R60_BANK_OF(e[4] & 0x3Fu) << 4);   /* FB byte = bank*16 + pen; bank = this palette's 32X bank */
    int      y;
    const uint16_t *pt5; int lim5 = (int)(FB_W + XO_), xo5 = (int)XO_; unsigned npx5 = 0;   /* per sprite, not per row */
    if (!r60_pt_init) r60_pt_build();
    pt5 = r60_pt[flip ? R60_PT_FI : 0u][base >> 4];
    /* ZOOM, as the chip does it (jts16_obj_scan.v:78-80, jts16_obj_draw.v:70-93). Vertical:
     * a 5-bit accumulator adds vzoom (e[5] bits 9:5) each row; its carry doubles the
     * current row's pitch, so a source row is skipped and the sprite shrinks. Horizontal:
     * a 6-bit accumulator starts at (hzoom & 15) << 2 and adds hzoom (bits 4:0) per
     * source pixel; on its carry the pixel is neither drawn nor advanced. */
    unsigned vz = ((unsigned)e[5] >> 5) & 0x1Fu, hz = (unsigned)e[5] & 0x1Fu;
    unsigned vacc = 0, vcarry = 0;
    /* THE HORIZONTAL DROP PATTERN, ONCE A SPRITE (2026-10-15). The 6-bit accumulator
     * restarts at (hz & 15) << 2 on every row and adds hz a source pixel, so whether
     * source pixel k is kept is (((h0 + k * hz) & 63) + hz) < 64: a function of k mod 64
     * alone (the period divides 64). Two 32-bit words hold it; a row reads four bits a
     * source word instead of running the accumulator per pixel. One zoomed logo sprite
     * (vz 9 hz 9, ~93 px) tipped the eye scenes to two-vint frames (sw58 0.77/0.83 ->
     * sw60 0.62/0.67 flips/vint in windows 1000-2000 / 4000-5000). */
    uint32_t keep_lo = 0, keep_hi = 0;
    uint8_t zkn[16];                              /* the pattern as 16 nibbles, once a sprite (was per row inside the row function) */
    if (hz) {
        unsigned k;
        r60_zoom_keep_copy(hz, zkn);
        /* Retain masks for scalar left clipping, other backends and checks.
         * Reverse nibble order keeps every shift constant on the SH-2. */
        for (k = 8u; k; k--) {
            keep_lo = (keep_lo << 4) | zkn[k - 1u];
            keep_hi = (keep_hi << 4) | zkn[k + 7u];
        }
    }

    if (top >= bot || xpos >= (int)(FB_W + XO_))
        return;
#ifdef R60_ROWFAST
    /* ROWFAST (2026-10-23): the row loop below spent ~150 instructions a row on per-SPRITE
     * constants and stack reloads (plotter-audit, sw307 objdump: the band test, the x & 3 /
     * flip / left-clip dispatch at the PLOT5 entry, the bx0/by0/by1 box arithmetic, and
     * R60_ROWSPAN_REC's eight stack words to pack one span word). Here the rows above the
     * band only step the address, the drawn rows are [ys, ye), the flip and the zoom form
     * are chosen once a sprite, the span word is a per-sprite header | y << 24 | the row's end
     * group through a carried pointer, and bx1 is the running maximum clamped once. Every
     * row calls the same row function with the same arguments as the loop below, so it
     * stores the same bytes; the span word and the box are the same by construction, and
     * R60PLOT5CHECK=1 shadows the old per-row formulas and counts disagreements in
     * r60_span_bad (rows + 0x10000 a box). */
    {
        int ys = top > (int)ylo ? top : (int)ylo, ye = bot < (int)yhi ? bot : (int)yhi;
        int xo = (int)XO_, x0 = xpos;
        int xs = x0 < 0 ? 0 : (x0 & ~3), cap = (int)(FB_W + 512u), xmax = 0;
        unsigned sp_full = 0;
        unsigned bk = bank & 1u;
        uint32_t sp_hdr = ((uint32_t)(ks_cur & 63u) << 16) | ((uint32_t)(xs >> 2) << 8);
        uint32_t *spw = &r60_rowspans[bk][r60_rowspan_n[bk]], *sp_end = &r60_rowspans[bk][R60_SSPAN_CAP];
        (void)vcarry;
#define RF_VSTEP() do { vacc += vz; if (vacc & 0x20u) { addr = (unsigned)(addr + pitch); vacc &= 0x1Fu; } } while (0)
#define RF_STEP() do { addr = (unsigned)(addr + pitch); RF_VSTEP(); } while (0)
#define RF_SHADOW(xe, had, w) do { } while (0)
        /* the span and the box of a drawn row that ended at xe (R60_ROWSPAN_REC(xpos, xe), hoisted) */
#define RF_SPAN(xe) do { int xe4_ = ((int)(xe) + 3) & ~3; unsigned had_ = 0; uint32_t w_ = 0; if (xe4_ > cap) xe4_ = cap; \
            if (xe4_ > xs) { had_ = 1; w_ = sp_hdr | ((uint32_t)(unsigned)y << 24) | (uint32_t)(xe4_ >> 2); \
                if (spw < sp_end) *spw++ = w_; else sp_full = 1; } \
            if ((int)(xe) > xmax) xmax = (int)(xe); RF_SHADOW(xe, had_, w_); } while (0)
#define RF_CHECK5() do { } while (0)
#define RF_ROWS5(FN) \
        for (y = ys; y < ye; y++) { \
            volatile uint8_t *row; unsigned xe; \
            RF_STEP(); \
            row = FB_OVR((unsigned)y * FB_W); \
            RF_CHECK5(); \
            xe = FN(sd, addr, x0, row, lim5, xo5, pt5, &npx5); \
            RF_SPAN(xe); \
        }
        for (y = top; y < ys; y++) RF_STEP();      /* above the band: the address steps, nothing is drawn */
        if (ys < ye) {
            if (x0 > xo && (unsigned)x0 < bx0) bx0 = (unsigned)x0; else if (x0 <= xo) bx0 = XO_;
            by0 = (unsigned)ys; by1 = (unsigned)ye;
        }
        if (hz) {
            if (x0 >= xo) {                          /* the word form: the row starts on the canvas */
                unsigned zpx = 0;
                for (y = ys; y < ye; y++) {
                    volatile uint8_t *row; unsigned xe;
                    RF_STEP();
                    row = FB_OVR((unsigned)y * FB_W);
                    xe = flip ? r60_zoom_row_f(sd, addr, x0, row, lim5, pt5, zkn, &zpx)
                              : r60_zoom_row_n(sd, addr, x0, row, lim5, pt5, zkn, &zpx);
                    RF_SPAN(xe);
                }
                RP_ += zpx;
            } else {                                 /* left-clipped: the per-pixel form */
                for (y = ys; y < ye; y++) {
                    volatile uint8_t *row; unsigned xe;
                    RF_STEP();
                    row = FB_OVR((unsigned)y * FB_W);
                    xe = r60_zoom_row_ref(sd, addr, flip ? 1 : 0, x0, row, lim5, xo, base, keep_lo, keep_hi);
                    RF_SPAN(xe);
                }
            }
        } else
        if (flip) RF_ROWS5(r60_plot_row5f) else RF_ROWS5(r60_plot_row5n)
        if (xmax > 0) { unsigned xe2 = (unsigned)xmax > FB_W + XO_ ? FB_W + XO_ : (unsigned)xmax; if (xe2 > bx1) bx1 = xe2; }
        r60_rowspan_n[bk] = (uint16_t)(spw - r60_rowspans[bk]);
        if (sp_full) r60_rowspan_full[bk] = 1;
#undef RF_ROWS5
#undef RF_CHECK5
#undef RF_SPAN
#undef RF_SHADOW
#undef RF_STEP
#undef RF_VSTEP
    }
#else /* !R60_ROWFAST: the row loop with every probe form */
    for (y = top; y < bot; y++) {
        volatile uint8_t *row;
        unsigned o;
        /* THE ADDRESS STEPS BEFORE EVERY ROW, THE FIRST INCLUDED: jts16_obj_scan.v:78
         * `next_offset = (first ? offset : tbl_dout) + pitch`, and the old compose did the
         * same (native1 m_main.c:7513). Stepping after the row drew the first row from the
         * record's unstepped address: one row of the previous sprite's data above every
         * sprite, which Mike saw on the board (2026-10-14 22:10). */
        addr = (unsigned)(addr + pitch);
        /* MAME sega16sp.cpp:941-947 ("100% accurate to the real board"): the 5-bit sum
         * carries in THIS row and the extra pitch is taken before the row is drawn. Our FB
         * rows for the eye scene's logo equal a simulation of this on the same record. */
        vacc += vz; if (vacc & 0x20u) { addr = (unsigned)(addr + pitch); vacc &= 0x1Fu; }
        (void)vcarry;
        o = addr;
        int x = xpos;
        if ((unsigned)y < ylo || (unsigned)y >= yhi) {
            continue;                       /* outside this CPU's band; the row still steps addr */
        }
        row = FB_OVR((unsigned)y * FB_W);
        if (x > (int)XO_ && (unsigned)x < bx0) bx0 = (unsigned)x; else if (x <= (int)XO_) bx0 = XO_;
        if ((unsigned)y < by0) by0 = (unsigned)y;
        if ((unsigned)y + 1u > by1) by1 = (unsigned)y + 1u;
        if (hz) {
            unsigned k = 0;
            if (x >= (int)XO_) {
                unsigned zpx = 0;
                x = (int)(flip ? r60_zoom_row_f(sd, o, x, row, (int)(FB_W + XO_), pt5, zkn, &zpx)
                               : r60_zoom_row_n(sd, o, x, row, (int)(FB_W + XO_), pt5, zkn, &zpx));
                RP_ += zpx;
            } else {
                x = (int)r60_zoom_row_ref(sd, o, flip ? 1 : 0, x, row, (int)(FB_W + XO_), (int)XO_, base, keep_lo, keep_hi);
            }
            (void)k;
            if (0) {
            /* the same per-pixel rule as before, with the drop bit read from the pattern
             * and the flip select hoisted: p0..p3 are the four pixels in draw order */
#define R60_ZPIX(g, bit, p) \
            if ((g) & (bit)) { if ((unsigned)((p) - 1u) < 14u && (unsigned)(x - (int)XO_) < FB_W) { row[x] = (uint8_t)(base + (p)); RP_++; } x++; }
            if (!flip) {
                for (;;) {
                    uint16_t w = sd[o++];
                    unsigned g = (unsigned)(((k & 32u) ? keep_hi : keep_lo) >> (k & 31u)) & 0xFu;
                    unsigned p0 = (w >> 12) & 0xFu, p1 = (w >> 8) & 0xFu, p2 = (w >> 4) & 0xFu, p3 = w & 0xFu;
                    R60_ZPIX(g, 1u, p0) R60_ZPIX(g, 2u, p1) R60_ZPIX(g, 4u, p2) R60_ZPIX(g, 8u, p3)
                    k = (k + 4u) & 63u;
                    if (p3 == 15u || x >= (int)(FB_W + XO_)) break;
                }
            } else {
                for (;;) {
                    uint16_t w = sd[o--];
                    unsigned g = (unsigned)(((k & 32u) ? keep_hi : keep_lo) >> (k & 31u)) & 0xFu;
                    unsigned p0 = w & 0xFu, p1 = (w >> 4) & 0xFu, p2 = (w >> 8) & 0xFu, p3 = (w >> 12) & 0xFu;
                    R60_ZPIX(g, 1u, p0) R60_ZPIX(g, 2u, p1) R60_ZPIX(g, 4u, p2) R60_ZPIX(g, 8u, p3)
                    k = (k + 4u) & 63u;
                    if (p3 == 15u || x >= (int)(FB_W + XO_)) break;
                }
            }
#undef R60_ZPIX
            }
            R60_ROWSPAN_REC(xpos, x);            /* zoomed rows are cleared like any other (sw139: never recorded, never cleared, 30 Hz ghosts) */
            if (x > 0) { unsigned xe = (unsigned)x > FB_W + XO_ ? FB_W + XO_ : (unsigned)x; if (xe > bx1) bx1 = xe; }
            continue;
        }
        {
            const uint16_t *pt = pt5;
            unsigned xe;
            xe = flip ? r60_plot_row5f(sd, o, x, row, lim5, xo5, pt, &npx5) : r60_plot_row5n(sd, o, x, row, lim5, xo5, pt, &npx5);
            R60_ROWSPAN_REC(x, (int)xe);
            x = (int)xe;
        }
        if (x > 0) { unsigned xe2 = (unsigned)x > FB_W + XO_ ? FB_W + XO_ : (unsigned)x; if (xe2 > bx1) bx1 = xe2; }
        continue;
        {
            const uint16_t *pt = r60_pt[flip ? R60_PT_FI : 0u][base >> 4];
            unsigned xa;
            if (!r60_pt_init) r60_pt_build();
            for (;;) {
                uint16_t w = flip ? sd[o--] : sd[o++];
                unsigned last = flip ? (unsigned)(w >> 12) : (unsigned)(w & 0xFu);
                uint32_t acc;
                acc = flip ? (((uint32_t)PT_F(pt[w & 0xFFu]) << 16) | PT_F(pt[w >> 8]))
                           : (((uint32_t)pt[w >> 8] << 16) | pt[w & 0xFFu]);
                if (acc && x > (int)XO_ - 4 && x < (int)(FB_W + XO_)) {
                    volatile uint32_t *q4;
                    if (x < 0) {
                        if (x == -1) acc <<= 8; else if (x == -2) acc <<= 16; else acc <<= 24;
                        *(volatile uint32_t *)(row) = acc;
                    } else {
                        xa = (unsigned)x & ~3u;
                        q4 = (volatile uint32_t *)(row + xa);
                        switch ((unsigned)x & 3u) {
                        case 0: q4[0] = acc; break;
                        case 1: q4[0] = acc >> 8;  if (xa + 4u < FB_W + XO_) q4[1] = acc << 24; break;
                        case 2: q4[0] = acc >> 16; if (xa + 4u < FB_W + XO_) q4[1] = acc << 16; break;
                        default: q4[0] = acc >> 24; if (xa + 4u < FB_W + XO_) q4[1] = acc << 8; break;
                        }
                    }
                    RP_ += 4;
                }
                x += 4;
                if (last == 15u) break;
                if (x >= (int)(FB_W + XO_)) break;
            }
        }
        if (x > 0) { unsigned xe = (unsigned)x > FB_W + XO_ ? FB_W + XO_ : (unsigned)x; if (xe > bx1) bx1 = xe; }
        continue;
        /* PLOT3: FOUR PIXELS A STORE through the overwrite image. A sprite word is four
         * nibbles; the four pens (base+nibble, or 0 for a transparent or end nibble) are
         * packed into one longword and stored at the 4-aligned address, split across two
         * stores when x is not aligned, with zeros in the positions the group does not
         * cover. The hardware skips the zero bytes per lane, so no read-modify-write and
         * no masks: this is the overwrite image's whole point. The old "word-store path"
         * that measured slower went through the DIRECT image with a RMW. */
        {
            unsigned xa;
            for (;;) {
                uint16_t w = flip ? sd[o--] : sd[o++];
                unsigned p0, p1, p2, p3, last;
                uint32_t acc;
                if (!flip) { p0 = (w >> 12) & 0xFu; p1 = (w >> 8) & 0xFu; p2 = (w >> 4) & 0xFu; p3 = w & 0xFu; last = p3; }
                else       { p0 = w & 0xFu; p1 = (w >> 4) & 0xFu; p2 = (w >> 8) & 0xFu; p3 = (w >> 12) & 0xFu; last = p3; }
                p0 = ((unsigned)(p0 - 1u) < 14u) ? base + p0 : 0u;
                p1 = ((unsigned)(p1 - 1u) < 14u) ? base + p1 : 0u;
                p2 = ((unsigned)(p2 - 1u) < 14u) ? base + p2 : 0u;
                p3 = ((unsigned)(p3 - 1u) < 14u) ? base + p3 : 0u;
                acc = ((uint32_t)p0 << 24) | ((uint32_t)p1 << 16) | ((uint32_t)p2 << 8) | (uint32_t)p3;
                if (acc && x > (int)XO_ - 4 && x < (int)(FB_W + XO_)) {
                    /* constant shifts only: the SH-2 has no barrel shifter and a variable
                     * shift is a libgcc call (sw47: the plot grew to 203 lines) */
                    volatile uint32_t *q4;
                    if (x < 0) {                             /* left edge: drop the off-screen pixels */
                        if (x == -1) acc <<= 8; else if (x == -2) acc <<= 16; else acc <<= 24;
                        *(volatile uint32_t *)(row) = acc;
                    } else {
                        xa = (unsigned)x & ~3u;
                        q4 = (volatile uint32_t *)(row + xa);
                        switch ((unsigned)x & 3u) {
                        case 0: q4[0] = acc; break;
                        case 1: q4[0] = acc >> 8;  if (xa + 4u < FB_W + XO_) q4[1] = acc << 24; break;
                        case 2: q4[0] = acc >> 16; if (xa + 4u < FB_W + XO_) q4[1] = acc << 16; break;
                        default: q4[0] = acc >> 24; if (xa + 4u < FB_W + XO_) q4[1] = acc << 8; break;
                        }
                    }
                    RP_ += 4;
                }
                x += 4;
                if (last == 15u) break;
                if (x >= (int)(FB_W + XO_)) break;
            }
        }
        if (x > 0) { unsigned xe = (unsigned)x > FB_W + XO_ ? FB_W + XO_ : (unsigned)x; if (xe > bx1) bx1 = xe; }
        continue;
        for (;;) {
            uint16_t w = flip ? sd[o--] : sd[o++];
            unsigned n, pix = 0;
            /* FOUR NIBBLES A WORD. Forward reads them high to low; h-flip walks the
             * SOURCE backward and the nibbles low to high, while x still advances --
             * that asymmetry is the arcade's and m_main.c's NIB/flip pair had it too. */
            for (n = 0; n < 4; n++) {
                pix = flip ? ((w >> (n * 4)) & 0xF) : ((w >> (12 - n * 4)) & 0xF);
                if ((unsigned)(pix - 1) < 14u && x >= (int)XO_ && x < (int)(FB_W + XO_)) {
                    row[x] = (uint8_t)(base + pix);
                    RP_++;
                }
                x++;
            }
            if (pix == 15)                  /* END OF ROW is the FOURTH nibble, which is
                                             * what the old renderer measured, not any 15 */
                break;
            if (x >= (int)(FB_W + XO_))
                break;
        }
    }
#endif /* R60_ROWFAST */
    RP_ += npx5;
    if (by1 > by0 && bx1 > bx0) { LB_[0] = (uint16_t)bx0; LB_[1] = (uint16_t)bx1; LB_[2] = (uint16_t)by0; LB_[3] = (uint16_t)by1; }
    else { LB_[0] = 0; LB_[1] = 0; LB_[2] = 0; LB_[3] = 0; }
    if (by1 > by0 && bx1 > bx0) {
        volatile uint16_t *rc = mlist ? R60_MRECTS(bank) : R60_RECTS(bank);
        unsigned n = mlist ? R60_MRECTN(bank) : R60_RECTN(bank);
        if (n < 64u) {
            rc[n * 4] = (uint16_t)bx0; rc[n * 4 + 1] = (uint16_t)bx1; rc[n * 4 + 2] = (uint16_t)by0; rc[n * 4 + 3] = (uint16_t)by1;
            if (mlist) R60_MRECTN(bank) = (uint16_t)(n + 1u); else R60_RECTN(bank) = (uint16_t)(n + 1u);
        }
    }
    (void)mask;
}
