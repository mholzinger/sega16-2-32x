/* r60_plot.inc -- priority mask, punch-through, the row plotters and the zoom rows.
 * Part of r60_main.c: included there, in this order, so the compiler sees one unit. */
/* ONE SPRITE, straight into the hidden bank. P1 reads the S16 4bpp art as it stands; the
 * baked 8-bit form with the pen base folded in is the next build, and this is the control
 * it will be measured against. */
static uint32_t *r60_mask;              /* the mask for the bank being drawn */
static uint32_t cleared_rows[7];         /* the mask at pass start: rows the clear wiped */
/* ROWCLR (2026-10-17): the clear of a bank's old sprites cost 57 lines a pass in the demo
 * (event ring: PASS_START +21 -> CLEAR_DONE +78): 64 x 64 boxes zeroed in full, 9 cycles a
 * longword store, for sprites ~40% opaque. Each drawn row records its own span instead,
 * from its first group to its end marker, and the clear zeroes exactly those spans. */
/* the span a row may have touched: first group (clamped to the row start) to its end x */
/* KEEPSPR (2026-10-17): a sprite whose record is the one this bank drew last time, and
 * whose box no changed sprite's old box, no earlier changed sprite's new box and no pending
 * text cell touches, keeps its pixels: its spans are neither cleared nor redrawn, and are
 * carried into the bank's next span list. The demo's logo and idle figures are such. */
static uint16_t ks_rec[2][64][8];
static uint16_t ks_box[2][64][4];        /* x0 x1 y0 y1 of the last draw, x1 == 0: none */
static uint8_t  ks_n[2];
/* bit 0: new record reuses its pixels; bit 1: transition preserves old spans */
static uint8_t  ks_keep[64];
#define R60_KS_KEEP_DRAW(i) (ks_keep[(i)] & 1u)
volatile unsigned ks_stats[4];           /* [0] kept [1] drawn [2] passes [3] candidates refused */
static unsigned ks_cur;                  /* the record index being drawn: tags its spans */
uint16_t r60_last_box[4];
#define R60_ROWSPAN_REC(xstart, xend) do { \
    unsigned bk_ = bank & 1u; \
    int xs_ = (xstart) < 0 ? 0 : ((xstart) & ~3), xe_ = ((xend) + 3) & ~3, cap_ = (int)(FB_W + 512u); \
    if (xe_ > cap_) xe_ = cap_; \
    if (xe_ > xs_) { \
        unsigned n_ = r60_rowspan_n[bk_]; \
        if (n_ < R60_SSPAN_CAP) { r60_rowspans[bk_][n_] = ((uint32_t)(unsigned)y << 24) | ((uint32_t)(ks_cur & 63u) << 16) | ((uint32_t)(xs_ >> 2) << 8) | (uint32_t)(xe_ >> 2); r60_rowspan_n[bk_] = (uint16_t)(n_ + 1u); } \
        else r60_rowspan_full[bk_] = 1; \
    } } while (0)
static uint32_t r60_rowspans[2][R60_SSPAN_CAP];   /* y << 20 | (x0/4) << 10 | x1/4, per bank (1024: .bss meets the 0x06019000 guard at 2048) */
static uint16_t r60_rowspan_n[2];
static uint8_t  r60_rowspan_full[2];
/* PRIORITY TILES OVER SPRITES (sw156, 2026-10-17). jts16_prio.v:63-67,83-86: a sprite pixel
 * loses to the FOREGROUND (scr1) where the tile carries the priority bit and the tile pixel
 * is opaque (tile[2:0] != 0), whatever the sprite's own priority (obj_prio >= 2 only decides
 * non-priority tiles, and Altered Beast's sprites are priority 2 or 3: census 2026-10-17).
 * The 32X layer sits in front of the MD planes, so after a sprite is drawn its pixels under
 * the opaque pixels of every priority FG cell it overlaps are written back to 0 through the
 * direct image: the plane shows through. The flames page (10) is priority on all 800 cells,
 * the eye's lids (pages 0/1) on all, the level ground band on ~480 a page. The old renderer
 * did the same with per-pixel masks (m_main.c "punch", LESSONS.md:807-830, 1294-1316). */
#define PRIO_TEXT_U   ((const volatile uint16_t *)0x26026000u)  /* the port's text RAM: page selects 0x740, scroll 0x748/0x74C */
#define PRIO_TILEMAP_U ((const volatile uint16_t *)0x26019000u) /* 13 pages x 0x800 words */
volatile unsigned r60_prio_stats[4];         /* [0] sprites punched [1] cells tested [2] priority cells [3] rows written */
#define R60PUNCHCODE R60CODE
static unsigned prio_xs, prio_ys, prio_psel, prio_bank1;  /* the FG scroll and page selects this pass punches against, read once a pass */
static uint32_t ks_fgkey[2];                 /* per bank: the scroll/page key its punches were made under */
static uint16_t ks_fgpsel[2], ks_fggen[2];
#define R60_FGGEN     (*(volatile uint16_t *)0x26028FB6u)   /* bumped by the master's walk when an FG cell changes */
static uint8_t  ks_punched[2][64];           /* per bank: the sprite was punched (kept only while the FG stands still) */
static uint16_t ks_pyr[2][64];               /* per bank: the punched rows, ymin << 8 | ymax+1 */
static uint8_t  ks_repunch[64];              /* kept, but the FG scrolled sideways: redraw and re-punch its punched rows only */
static unsigned prio_ymin, prio_ymax;        /* the rows the last punch hit */
/* Tile opacity: eight bytes/code, bit 7 = leftmost pixel. The accepted path uses
 * 1024 entries in the 10 KiB arena 0x3C400..0x3EC00. MASKBLOCK partitions the same
 * arena into 512 tile entries/tags (5 KiB), 32 composed masks (4 KiB), and their
 * full 16-word source keys (1 KiB). Tags/counters add 152 B of ordinary data/BSS.
 * The master stack above this arena is untouched. */
#define R60_PRIO_ENTRIES 512u
#define R60_PRIO_INDEX_MASK 511u
#define PRIO_MASK ((uint8_t *)0x0603C400u)
#define PRIO_TAG  ((uint16_t *)0x0603D400u)
static unsigned r60_prio_init;
volatile unsigned r60_prio_miss[2];          /* probe 2026-10-23: [0] mask lookups [1] rebuilds from cart */
static __attribute__((noinline)) const uint8_t *r60_prio_mask_cold(unsigned code)   /* the miss: the table init and a rebuild from the cart art (cart-resident, 2026-10-24: .ramtext room for PUNCH2) */
{
    unsigned i = code & R60_PRIO_INDEX_MASK;
    uint8_t *m = PRIO_MASK + i * 8u;
    if (!r60_prio_init) { unsigned k; for (k = 0; k < R60_PRIO_ENTRIES; k++) PRIO_TAG[k] = 0xFFFFu; r60_prio_init = 1; }
    if (PRIO_TAG[i] != code) {
        r60_prio_miss[1]++;
        const uint8_t *a = game_tiles + code * 64u;
        unsigned py;
        for (py = 0; py < 8u; py++) {
            const uint8_t *r = a + py * 8u;
            unsigned b = 0;
            if (r[0] & 7u) b |= 0x80u; if (r[1] & 7u) b |= 0x40u; if (r[2] & 7u) b |= 0x20u; if (r[3] & 7u) b |= 0x10u;
            if (r[4] & 7u) b |= 0x08u; if (r[5] & 7u) b |= 0x04u; if (r[6] & 7u) b |= 0x02u; if (r[7] & 7u) b |= 0x01u;
            m[py] = (uint8_t)b;
        }
        PRIO_TAG[i] = (uint16_t)code;
    }
    return m;
}
R60CODE static const uint8_t *r60_prio_mask(unsigned code)
{
    unsigned i = code & R60_PRIO_INDEX_MASK;
    r60_prio_miss[0]++;
    if (r60_prio_init && PRIO_TAG[i] == code) return PRIO_MASK + i * 8u;
    return r60_prio_mask_cold(code);
}
/* THE PUNCH'S VARIABLE SHIFTS (2026-10-23): the SH-2 has no barrel shifter, so `b << (4 - sh)`,
 * `0xFF >> (x0 - sx)`, `0xFF << (sx + 8 - x1)` and `psel >> (q * 4)` were each a libgcc call
 * (~25 cycles) per cell-row; R60PHBARS read the punch at 70 lines a pass in the demo fight
 * (660 cell-rows). Constant shifts and tables instead, bit for bit. */
#define PUP_T(v)       do { } while (0)
#define PUP_ADD(k, d)  do { } while (0)
#define PUP_ADDN(k, n) do { } while (0)
#define PUP_INC(k)     do { } while (0)
#define R60_PWIN(b, sh) ((sh) == 0u ? ((b) << 4) : (sh) == 1u ? ((b) << 3) : (sh) == 2u ? ((b) << 2) : ((b) << 1))
static const uint8_t r60_cm_r[8] = { 0xFFu, 0x7Fu, 0x3Fu, 0x1Fu, 0x0Fu, 0x07u, 0x03u, 0x01u };   /* 0xFF >> n */
static const uint8_t r60_cm_l[8] = { 0xFFu, 0xFEu, 0xFCu, 0xF8u, 0xF0u, 0xE0u, 0xC0u, 0x80u };   /* 0xFF << n, low 8 bits */
static const uint32_t r60_nib_mask[16] = {   /* 4 pixels a longword, pixel 0 in the top byte: 0xFF per set bit */
    0x00000000u, 0x000000FFu, 0x0000FF00u, 0x0000FFFFu, 0x00FF0000u, 0x00FF00FFu, 0x00FFFF00u, 0x00FFFFFFu,
    0xFF000000u, 0xFF0000FFu, 0xFF00FF00u, 0xFF00FFFFu, 0xFFFF0000u, 0xFFFF00FFu, 0xFFFFFF00u, 0xFFFFFFFFu };
/* the punch's per-core state: under MBAND the draw context's, else the slave statics */
#define PU_YMIN   prio_ymin
#define PU_YMAX   prio_ymax
#define PU_STAT   r60_prio_stats
#define PU_MASK(c) r60_prio_mask((c))
#define PU_HEAD   unsigned psel = prio_psel, xs = prio_xs, ys = prio_ys; int xo = (int)r60_xo;
#define PU_ARGS   const uint16_t *box
#define R60_P2NOTE(p, keep) do { } while (0)
#define R60_P1NIB(k, n) do { if ((n) == 0xFu) fb[k] = 0; else if (n) fb[k] &= ~r60_nib_mask[n]; } while (0)
/* PUNCH2 (2026-10-23): the box punch with its row loop stripped to the stores. R60PHBARS read
 * the loop above at 70 lines a pass in the demo fight for ~650 cell-rows: 158 cycles a
 * cell-row for one mask byte, a window shift and up to three longword read-modify-writes,
 * because every per-cell quantity was recomputed per row (the FB row address by mul.l, the
 * shift, the 12-pixel window select, two volatile stats, prio_ymin/ymax through the stack).
 * Here the row loop is instantiated once per window shift (sx + xo) & 3 so every shift is a
 * constant; the FB pointer walks by FB_W a row; the stats are summed in locals and stored
 * once a call; the rows hit are tracked per cell as first/last and folded into
 * prio_ymin/ymax once a cell; the nibble keep masks sit in .ramtext (the .rodata table is a
 * cart read a nibble on the FPGA); the mask-cache hit test is inline and the rebuild stays
 * a call. Same cells, same clamps (x0/x1/y0/y1, the colmask edges, pages > 12 skipped,
 * w & 0x8000 and w & 0x1FFF), the same longword stores to the same addresses.
 * R60PUNCH2CHECK=1 runs the old punch beside it over a checksum of the box, alternating the
 * order (new-then-old catches a byte the new one missed, old-then-new a byte it zeroed that
 * the old one left) and compares the row count and prio_ymin/ymax: r60_punch2_bad counts
 * any disagreement, r60_punch2_why[] says which. */
static const uint32_t r60_nib_keep[16] __attribute__((section(".ramtext2.nib"))) = {   /* SDRAM region 2 (mars.ld gathers .ramtext2.*): PTONE's swap.b grew r60_sprite 40 B past .ramtext's 12 B of room */
    0xFFFFFFFFu, 0xFFFFFF00u, 0xFFFF00FFu, 0xFFFF0000u, 0xFF00FFFFu, 0xFF00FF00u, 0xFF0000FFu, 0xFF000000u,
    0x00FFFFFFu, 0x00FFFF00u, 0x00FF00FFu, 0x00FF0000u, 0x0000FFFFu, 0x0000FF00u, 0x000000FFu, 0x00000000u };
/* a full nibble is a store of 0 (no FB read), a partial one a masked AND, an empty one nothing */
#define R60_P2NIB(k, n) do { unsigned _n = (n); if (_n == 0xFu) R60_P2Z(k); else if (_n) R60_P2K(k, r60_nib_keep[_n]); } while (0)
/* the no-read form: the through pen in the masked bytes, 0 (skipped by the overwrite image) in the
 * kept ones; ~keep is 0xFF per masked byte. fb points into the overwrite image (R60_PU_FB). */
#define R60_P2Z(k)       do { fb[k] = R60_THRU_4; } while (0)
#define R60_P2K(k, keep) do { fb[k] = ~(keep) & R60_THRU_4; } while (0)
#define R60_PU_FB(off)   ((volatile uint32_t *)(FB_WBASE + 0x20000u + FB_ROW0 + (off)))
#define R60_P2ROWCNT(full) do { } while (0)
/* a solid row (b == 0xFF: the common FG tile) is the same three stores with the nibbles known
 * (shift s: n0 = 0xFF >> (4 + s), n1 = 0xF, n2 = (0xF << (4 - s)) & 0xF), without the nibble tests */
#define R60_P2ROWS(F0, F1, F2, S0, S1, S2) \
    for (; py < py1; py++, fb += FB_W / 4u) { \
        unsigned b = m[py] & colmask; \
        if (!b) continue; \
        if (b == 0xFFu) { R60_P2ROWCNT(1); F0; F1; F2; } else { R60_P2ROWCNT(0); S0; S1; S2; } \
        if (!ch) cfirst = (unsigned)py; \
        clast = (unsigned)py; ch++; \
    }
#define R60_P2SLAVE 1
R60PUNCHCODE static unsigned r60_punch2(PU_ARGS)
{
    PU_HEAD
    const unsigned ppage[4] = { psel & 0xFu, (psel >> 4) & 0xFu, (psel >> 8) & 0xFu, (psel >> 12) & 0xFu };
    int x0 = (int)box[0] - xo, x1 = (int)box[1] - xo, y0 = (int)box[2], y1 = (int)box[3];
    int cx, cy, cx0, cx1, cy0, cy1;
    unsigned hit = 0, tested = 0, prio = 0, ymin = FB_H, ymax = 0;
    if (x0 < 0) x0 = 0;
    if (x1 > (int)FB_W) x1 = (int)FB_W;
    if (y0 < 0) y0 = 0;
    if (y1 > (int)FB_H) y1 = (int)FB_H;
    if (x1 <= x0 || y1 <= y0) return 0;
    cx0 = (x0 + (int)xs) >> 3; cx1 = (x1 - 1 + (int)xs) >> 3;
    cy0 = (y0 + (int)ys) >> 3; cy1 = (y1 - 1 + (int)ys) >> 3;
    for (cy = cy0; cy <= cy1; cy++) {
        unsigned wr = (unsigned)cy & 63u;
        int sy = cy * 8 - (int)ys;
        int py0 = y0 - sy, py1 = y1 - sy;          /* rows of the cell inside the box */
        const volatile uint16_t *rowp[2];
        unsigned qh;
        if (py0 < 0) py0 = 0;
        if (py1 > 8) py1 = 8;
        for (qh = 0; qh < 2u; qh++) {
            unsigned page = ppage[qh | (((wr >> 5) & 1u) << 1)];
            rowp[qh] = page > 12u ? (const volatile uint16_t *)0 : PRIO_TILEMAP_U + page * 0x800u + (wr & 31u) * 64u;
        }
        tested += (unsigned)(cx1 - cx0 + 1);
        for (cx = cx0; cx <= cx1; cx++) {
            unsigned wc = (unsigned)cx & 127u;
            unsigned w, code, ci, colmask = 0xFFu, fx, ch = 0, cfirst = 0, clast = 0;
            const uint8_t *m;
            const volatile uint16_t *rp = rowp[(wc >> 6) & 1u];
            volatile uint32_t *fb;
            int sx = cx * 8 - (int)xs, py = py0;
            if (!rp) continue;
            w = rp[wc & 63u];
            if (!(w & 0x8000u) || !(w & 0x1FFFu)) continue;
            prio++;
            PUP_T(_pt0);
            code = r60_tile_index(w, prio_bank1); ci = code & R60_PRIO_INDEX_MASK;
            if (R60_P2SLAVE && r60_prio_init && PRIO_TAG[ci] == code) { r60_prio_miss[0]++; m = PRIO_MASK + ci * 8u; }   /* r60_prio_mask's hit path, inline (the slave's cache; the master reads it uncached under the tag) */
            else { PUP_T(_pc0); m = PU_MASK(code); PUP_T(_pc1); PUP_ADD(5, _pc1 - _pc0); PUP_INC(2); }
            if (sx < x0) colmask &= r60_cm_r[x0 - sx];          /* bit 7 = pixel 0 */
            if (sx + 8 > x1) colmask &= r60_cm_l[sx + 8 - x1];
            fx = (unsigned)(sx + xo);                            /* the cell's first pixel in the FB row: 4a + s */
            fb = R60_PU_FB((unsigned)(sy + py0) * FB_W + (fx & ~3u));
            PUP_T(_pt1); PUP_ADD(3, _pt1 - _pt0); PUP_INC(0); PUP_ADDN(1, (unsigned)(py1 - py0));
            switch (fx & 3u) {                                   /* window = b << (4 - s): pixel p of the cell at window pixel s + p */
            case 0u:  R60_P2ROWS(R60_P2Z(0), R60_P2Z(1), (void)0,
                                 R60_P2NIB(0, b >> 4), R60_P2NIB(1, b & 0xFu), (void)0); break;
            case 1u:  R60_P2ROWS(R60_P2K(0, 0xFF000000u), R60_P2Z(1), R60_P2K(2, 0x00FFFFFFu),      /* n0 7, n2 8 */
                                 R60_P2NIB(0, b >> 5), R60_P2NIB(1, (b >> 1) & 0xFu), R60_P2NIB(2, (b & 1u) << 3)); break;
            case 2u:  R60_P2ROWS(R60_P2K(0, 0xFFFF0000u), R60_P2Z(1), R60_P2K(2, 0x0000FFFFu),      /* n0 3, n2 C */
                                 R60_P2NIB(0, b >> 6), R60_P2NIB(1, (b >> 2) & 0xFu), R60_P2NIB(2, (b & 3u) << 2)); break;
            default:  R60_P2ROWS(R60_P2K(0, 0xFFFFFF00u), R60_P2Z(1), R60_P2K(2, 0x000000FFu),      /* n0 1, n2 E */
                                 R60_P2NIB(0, b >> 7), R60_P2NIB(1, (b >> 3) & 0xFu), R60_P2NIB(2, (b & 7u) << 1)); break;
            }
            { PUP_T(_pt2); PUP_ADD(4, _pt2 - _pt1); PUP_ADDN(6, ch); }
            if (ch) {
                unsigned ya = (unsigned)sy + cfirst, yb = (unsigned)sy + clast + 1u;
                hit += ch;
                if (ya < ymin) ymin = ya;
                if (yb > ymax) ymax = yb;
            }
        }
    }
    PU_YMIN = ymin; PU_YMAX = ymax;
    PU_STAT[1] += tested;
    PU_STAT[2] += prio;
    PU_STAT[3] += hit;
    if (hit) PU_STAT[0]++;
    return hit;
}
#define r60_punch r60_punch2
/* THE SAME PUNCH FROM THE SPRITE'S OWN ROW SPANS (sw162): the rows it drew and the x range it
 * drew on each, 4-px granular, as ROWCLR recorded them -- no empty rows, no slack around a
 * sparse sprite. Spans n0..n1 of this bank's list are the sprite's. */
R60PUNCHCODE static unsigned r60_punch_spans(unsigned bk, unsigned n0, unsigned n1)
{
    PU_HEAD
    const unsigned ppage[4] = { psel & 0xFu, (psel >> 4) & 0xFu, (psel >> 8) & 0xFu, (psel >> 12) & 0xFu };   /* the four page selects, once (2026-10-23: the per-cell `psel >> (q*4)` was a libgcc shift) */
    unsigned k, hit = 0;
    PU_YMIN = FB_H; PU_YMAX = 0;
    for (k = n0; k < n1; k++) {
        uint32_t sp = r60_rowspans[bk][k];
        int y = (int)(sp >> 24), x0 = (int)(((sp >> 8) & 0xFFu) << 2) - xo, x1 = (int)((sp & 0xFFu) << 2) - xo;
        unsigned wr, py;
        int cx, cx0, cx1;
        if (x0 < 0) x0 = 0;
        if (x1 > (int)FB_W) x1 = (int)FB_W;
        if (x1 <= x0 || y < 0 || y >= (int)FB_H) continue;
        wr = (unsigned)((y + (int)ys) >> 3) & 63u; py = (unsigned)(y + (int)ys) & 7u;
        cx0 = (x0 + (int)xs) >> 3; cx1 = (x1 - 1 + (int)xs) >> 3;
        for (cx = cx0; cx <= cx1; cx++) {
            unsigned wc = (unsigned)cx & 127u;
            unsigned q = ((wc >> 6) & 1u) | (((wr >> 5) & 1u) << 1);
            unsigned page = ppage[q];
            unsigned w, b, colmask = 0xFFu, win, n0b, n1b, n2b, sh;
            const uint8_t *m;
            volatile uint32_t *fb;
            int sx = cx * 8 - (int)xs;
            PU_STAT[1]++;
            if (page > 12u) continue;
            w = PRIO_TILEMAP_U[page * 0x800u + (wr & 31u) * 64u + (wc & 63u)];
            if (!(w & 0x8000u) || !(w & 0x1FFFu)) continue;
            PU_STAT[2]++;
            m = PU_MASK(r60_tile_index(w, prio_bank1));
            if (sx < x0) colmask &= r60_cm_r[x0 - sx];
            if (sx + 8 > x1) colmask &= r60_cm_l[sx + 8 - x1];
            b = m[py] & colmask;
            if (!b) continue;
            sh = (unsigned)(sx + xo) & 3u;
            fb = FB_RMW((unsigned)y * FB_W + ((unsigned)(sx + xo) & ~3u));
            win = R60_PWIN(b, sh);
            n0b = (win >> 8) & 0xFu; n1b = (win >> 4) & 0xFu; n2b = win & 0xFu;
            if (n0b == 0xFu) fb[0] = 0; else if (n0b) fb[0] &= ~r60_nib_mask[n0b];
            if (n1b == 0xFu) fb[1] = 0; else if (n1b) fb[1] &= ~r60_nib_mask[n1b];
            if (n2b == 0xFu) fb[2] = 0; else if (n2b) fb[2] &= ~r60_nib_mask[n2b];
            hit++;
            if ((unsigned)y < PU_YMIN) PU_YMIN = (unsigned)y; if ((unsigned)y + 1u > PU_YMAX) PU_YMAX = (unsigned)y + 1u;
        }
    }
    PU_STAT[3] += hit;
    if (hit) PU_STAT[0]++;
    return hit;
}
volatile unsigned r60_clr_stats[4];      /* [0] clears [1] spans [2] longs zeroed [3] overflow clears */
/* FEN must be clear before a fill is programmed or a CPU store reaches the framebuffer (sw154);
 * bounded, because a FEN that never clears must not hang the slave (sw233: 2000 ticks, 11 lines,
 * gave up on the MiSTer's fill and the stores after it were dropped). k = the probe's counter. */
#define CLRP(k, v) ((void)0)
#define R60_FENW(k) do { uint16_t _t = r60_frt(); while ((MARS_VDP_FBCTL & MARS_VDP_FEN) && (uint16_t)(r60_frt() - _t) < 20000u) ; } while (0)
static uint16_t cl_x0[28], cl_x1[28];    /* per text row: the x extent the clear wiped this pass (canvas x) */

/* PLOT4 (2026-10-17): the four pens of a sprite word by TABLE. r60_pt[flip][cset][byte] is
 * the two FB bytes a source byte becomes (base+nibble, 0 for a transparent or end nibble),
 * so a word costs two byte loads, two table loads and one shift/or instead of four nibble
 * extracts, four range tests and four packs (the SH-2 has no barrel shifter). The store
 * logic is PLOT3's. Zoomed rows (hz) keep their own path above. 16 KB, built once. */
/* R60PTONE (2026-11-01): ONE flip of the table. The flipped entry was the byte-swapped twin of
 * the unflipped one (r60_pt_build), so the flipped read swaps instead (swap.b, one instruction
 * a halfword, gcc's __builtin_bswap16): 8 KB of SDRAM back, for the synth lane's hot tables
 * (opm_sin1024 + opm_pow_flat, cart-resident until now: the rig made 2800-4300 of 8000 lane
 * samples/s, build/accept2/rig/pass_lane12tp). Cost: two swap.b a FLIPPED source word. */
#define R60_PT_NF 1
#define R60_PT_FI 0u
#define PT_F(v) ((uint16_t)__builtin_bswap16((uint16_t)(v)))
static uint16_t r60_pt[R60_PT_NF][16][256];
static unsigned r60_pt_init;
volatile unsigned r60_plot4_bad;
static __attribute__((noinline)) void r60_pt_build(void)   /* once: cart */
{
    unsigned c, b;
    for (c = 0; c < 16u; c++)
        for (b = 0; b < 256u; b++) {
            unsigned hi = b >> 4, lo = b & 15u;
            unsigned ph = ((unsigned)(hi - 1u) < 14u) ? c * 16u + hi : 0u;
            unsigned pl = ((unsigned)(lo - 1u) < 14u) ? c * 16u + lo : 0u;
            r60_pt[0][c][b] = (uint16_t)((ph << 8) | pl);
        }
    r60_pt_init = 1;
}
/* PLOT5 (2026-10-17): one row, everything in registers. The alignment k = x & 3 is constant
 * along a row (x steps by 4), so each row runs ONE of four loops with a carried longword:
 * the store at aligned address a covers the tail of the previous group and the head of this
 * one, where PLOT3 stored each group twice. The clip limits are locals, the flip has its own
 * loops, the pixel count is a local added once. Bytes stored equal PLOT3's (the check build
 * replays each row both ways into RAM with overwrite semantics and counts differences).
 * Groups left of the canvas (x < xo) take PLOT3's slow form until the fast loop can start. */
#define P5_ST0(ptr, v) (*(volatile uint32_t *)(ptr) = (v))
/* P5_ST is redefined around the masked instantiations (R60PRIOINLINE): macros bind at use */
#define P5_ST(ptr, v) P5_ST0(ptr, v)
#define P5_STORE(ptr, v) P5_ST(ptr, v)
#define P5_ACC(w) (FL ? (((uint32_t)PT_F(pt[(w) & 0xFFu]) << 16) | PT_F(pt[(w) >> 8])) : (((uint32_t)pt[(w) >> 8] << 16) | pt[(w) & 0xFFu]))
#define P5_LAST(w) (FL ? (unsigned)((w) >> 12) : (unsigned)((w) & 0xFu))
#define P5_NEXT()  (FL ? sd[o--] : sd[o++])
/* the generic (PLOT3-shaped) group, used left of the canvas and by the check */
#define P5_KEEP(g) 0xFFFFFFFFu
#define P5_SLOW(w) do { uint32_t acc = P5_ACC(w); \
    if (acc && x > xo4 && x < lim) { \
        if (x < 0) { if (x == -1) acc <<= 8; else if (x == -2) acc <<= 16; else acc <<= 24; P5_STORE(row, acc & P5_KEEP(0)); } \
        else { unsigned xa = (unsigned)x & ~3u; uint8_t *q = (uint8_t *)row + xa; \
            switch ((unsigned)x & 3u) { \
            case 0: P5_STORE(q, acc & P5_KEEP(xa >> 2)); break; \
            case 1: P5_STORE(q, (acc >> 8) & P5_KEEP(xa >> 2));  if (xa + 4u < (unsigned)lim) P5_STORE(q + 4, (acc << 24) & P5_KEEP((xa >> 2) + 1u)); break; \
            case 2: P5_STORE(q, (acc >> 16) & P5_KEEP(xa >> 2)); if (xa + 4u < (unsigned)lim) P5_STORE(q + 4, (acc << 16) & P5_KEEP((xa >> 2) + 1u)); break; \
            default: P5_STORE(q, (acc >> 24) & P5_KEEP(xa >> 2)); if (xa + 4u < (unsigned)lim) P5_STORE(q + 4, (acc << 8) & P5_KEEP((xa >> 2) + 1u)); break; } } \
        npx += 4; } \
    x += 4; } while (0)
/* the fast loop for alignment K (shift S = 8K): carry = the K bytes of the previous group
 * that belong at this aligned address; a group with acc == 0 still flushes a pending carry */
#define P5_KP_DECL
#define P5_KP_AND(v) (v)
#define P5_KP_STEP
#define P5_FAST(S) do { \
    uint8_t *q = (uint8_t *)row + ((unsigned)x & ~3u); uint32_t carry = 0; unsigned pend = 0; P5_KP_DECL \
    for (;;) { \
        uint16_t w = P5_NEXT(); unsigned last = P5_LAST(w); uint32_t acc = P5_ACC(w), out; \
        if (S) { out = carry | (acc >> (S)); carry = acc << (32 - (S)); } else { out = acc; carry = 0; } \
        out = P5_KP_AND(out); \
        if (out) { P5_STORE(q, out); } \
        if (acc) npx += 4; \
        pend = (S) ? (carry != 0) : 0; q += 4; x += 4; P5_KP_STEP \
        if (last == 15u || x >= lim) break; } \
    if (pend && (unsigned)(q - (uint8_t *)row) < (unsigned)lim) P5_STORE(q, P5_KP_AND(carry)); \
} while (0)
/* one body, two instantiations: FL is a literal so every loop is flip-free */
#define P5_ROWFN(NAME, FLV) P5_ROWFN_A(NAME, FLV, R60CODE static inline __attribute__((always_inline)))
#define P5_ROWFN_A(NAME, FLV, ATTR) \
ATTR unsigned NAME(const uint16_t *sd, unsigned o, int x, volatile uint8_t *row, \
                             int lim, int xo, const uint16_t *pt, unsigned *px_out) \
{ \
    enum { FL = (FLV) };                         /* a constant: the nested macros fold on it */ \
    unsigned npx = 0; \
    int xo4 = xo - 4; \
    while (x < xo) {                             /* left of the canvas: the slow form */ \
        uint16_t w = P5_NEXT(); \
        unsigned last = P5_LAST(w); \
        P5_SLOW(w); \
        if (last == 15u || x >= lim) { *px_out += npx; return (unsigned)x; } \
    } \
    switch ((unsigned)x & 3u) { \
    case 0: P5_FAST(0); break; \
    case 1: P5_FAST(8); break; \
    case 2: P5_FAST(16); break; \
    default: P5_FAST(24); break; \
    } \
    *px_out += npx; \
    return (unsigned)x; \
}
P5_ROWFN(r60_plot_row5n, 0)
P5_ROWFN(r60_plot_row5f, 1)
R60CODE static unsigned r60_plot_row5(const uint16_t *sd, unsigned o, int fl, int x, volatile uint8_t *row,
                                      int lim, int xo, const uint16_t *pt, unsigned *px_out)
{
    return fl ? r60_plot_row5f(sd, o, x, row, lim, xo, pt, px_out) : r60_plot_row5n(sd, o, x, row, lim, xo, pt, px_out);
}
/* ZOOMFAST (2026-10-23): a zoomed row by SOURCE WORD instead of by pixel.
 * kn[16] is the drop pattern as 16 nibbles, built once a sprite (zkn in r60_sprite) and read one a word. Neff's entrance smoke
 * is 16 zoomed puffs (19 of 21 sprites hz != 0, 12.7k zoomed source words a pass) and the
 * per-pixel zoom loop below costs ~120 instructions a word against PLOT5's 27-38: the pass
 * ran ~1550 lines = six vints a picture (sw306g tape, frames 29950-30070). Here the word's
 * four pens come from the PLOT5 pen table as one longword (first drawn pixel in the top
 * byte, flip folded into the table), the four keep bits select which bytes are pushed into
 * a running longword, and every fourth byte lands as one aligned overwrite store, exactly
 * the bytes the per-pixel loop wrote (R60ZOOMCHECK=1 replays both forms into RAM and counts
 * differences). The row must start on the canvas (x >= xo): a left-clipped row keeps the
 * per-pixel form. Like PLOT5 a group that straddles the right limit is stored whole. */
#define ZF_STFB(ptr, v)  (*(volatile uint32_t *)(ptr) = (v))
#define ZF_STCHK(ptr, v) do { uint32_t v_ = (v); uint8_t *b_ = (uint8_t *)(ptr); \
    if (v_ >> 24) b_[0] = (uint8_t)(v_ >> 24); if ((v_ >> 16) & 0xFFu) b_[1] = (uint8_t)(v_ >> 16); \
    if ((v_ >> 8) & 0xFFu) b_[2] = (uint8_t)(v_ >> 8); if (v_ & 0xFFu) b_[3] = (uint8_t)v_; } while (0)
#define ZF_PUSH(b) do { cur = (cur << 8) | (uint32_t)(b); \
    if (++j == 4u) { if (cur) { ZF_STORE(q, cur); npx += 4; } q += 4; cur = 0; j = 0; } } while (0)
#define ZF_C(G) case (G): { if ((G) & 1u) ZF_PUSH(acc >> 24); if ((G) & 2u) ZF_PUSH((acc >> 16) & 0xFFu); \
    if ((G) & 4u) ZF_PUSH((acc >> 8) & 0xFFu); if ((G) & 8u) ZF_PUSH(acc & 0xFFu); } break;
#define ZF_ROWFN(NAME, FLV) ZF_ROWFN_A(NAME, FLV, R60CODE static)
#define ZF_ROWFN_A(NAME, FLV, ATTR) \
ATTR unsigned NAME(const uint16_t *sd, unsigned o, int x, volatile uint8_t *row, int lim, \
                             const uint16_t *pt, const uint8_t *kn, unsigned *px_out) \
{ \
    enum { FL = (FLV) }; \
    volatile uint8_t *q = row + ((unsigned)x & ~3u); \
    unsigned j = (unsigned)x & 3u, npx = 0, kw = 0; \
    uint32_t cur = 0; \
    for (;;) { \
        uint16_t w = P5_NEXT(); \
        uint32_t acc = P5_ACC(w); \
        unsigned last = P5_LAST(w), g = kn[kw]; \
        kw = (kw + 1u) & 15u; \
        switch (g) { ZF_C(0) ZF_C(1) ZF_C(2) ZF_C(3) ZF_C(4) ZF_C(5) ZF_C(6) ZF_C(7) \
                     ZF_C(8) ZF_C(9) ZF_C(10) ZF_C(11) ZF_C(12) ZF_C(13) ZF_C(14) ZF_C(15) default: break; } \
        x = (int)(q - row) + (int)j; \
        if (last == 15u || x >= lim) break; \
    } \
    if (j && cur && (int)(q - row) < lim) { \
        if (j == 1u) cur <<= 24; else if (j == 2u) cur <<= 16; else cur <<= 8; \
        ZF_STORE(q, cur); npx += 4; \
    } \
    *px_out += npx; \
    return (unsigned)x; \
}
#define ZF_STORE ZF_STFB
ZF_ROWFN(r60_zoom_row_n, 0)
ZF_ROWFN(r60_zoom_row_f, 1)
#undef ZF_STORE
/* the per-pixel form (the pre-2026-10-23 zoom loop), in cart: the reference for the check and
 * the path for a left-clipped row (x < xo), which the word form does not take */
static __attribute__((noinline)) unsigned r60_zoom_row_ref(const uint16_t *sd, unsigned o, int flip, int x, volatile uint8_t *row, int lim, int xo,
                                 unsigned base, uint32_t keep_lo, uint32_t keep_hi)
{
    unsigned k = 0;
#define ZR_PIX(g, bit, p) \
    if ((g) & (bit)) { if ((unsigned)((p) - 1u) < 14u && (unsigned)(x - xo) < FB_W) row[x] = (uint8_t)(base + (p)); x++; }
    for (;;) {
        uint16_t w = flip ? sd[o--] : sd[o++];
        unsigned g = (unsigned)(((k & 32u) ? keep_hi : keep_lo) >> (k & 31u)) & 0xFu;
        unsigned p0, p1, p2, p3;
        if (!flip) { p0 = (w >> 12) & 0xFu; p1 = (w >> 8) & 0xFu; p2 = (w >> 4) & 0xFu; p3 = w & 0xFu; }
        else       { p0 = w & 0xFu; p1 = (w >> 4) & 0xFu; p2 = (w >> 8) & 0xFu; p3 = (w >> 12) & 0xFu; }
        ZR_PIX(g, 1u, p0) ZR_PIX(g, 2u, p1) ZR_PIX(g, 4u, p2) ZR_PIX(g, 8u, p3)
        k = (k + 4u) & 63u;
        if (p3 == 15u || x >= lim) break;
    }
#undef ZR_PIX
    return (unsigned)x;
}
__attribute__((noinline)) static void r60_palmap_assign(void)   /* cart-resident: s_main is in .ramtext and the slot is full */
{
            {
                static unsigned pm_init; static uint16_t used1, used2;   /* banks used one and two passes ago */
                uint16_t used = 0; uint8_t need[64]; unsigned nn = 0, k;
                if (!pm_init) { r60_palmap_init(); pm_init = 1; }
                for (k = 0; k < 64u; k++) {
                    const volatile uint16_t *e = R60_SNAPSLOT(R60_DRAWSEQ) + k * 8;
                    unsigned pl, bk;
                    if (e[2] & 0x8000u) break;                   /* the list's end */
                    pl = e[4] & 0x3Fu; bk = R60_PALMAP_U[pl];
                    if (bk < 16u && R60_BANKPAL_U[bk] == pl) used |= (uint16_t)(1u << bk);
                    else { unsigned j; for (j = 0; j < nn && need[j] != pl; j++) ; if (j == nn) need[nn++] = (uint8_t)pl; }
                }
                for (k = 0; k < nn; k++) {
                    unsigned pl = need[k], b2, busy = used | used1 | used2 | 1u, old;   /* bank 0 (backdrop, through bit) is never handed to another palette */
                    busy |= R60_POOL0MASK;                       /* a bank whose pen 0 seats text is taken last (the steal below may still take it; the master re-seats next window) */
                    b2 = pl & 15u;                               /* its old identity bank first */
                    if ((busy >> b2) & 1u) for (b2 = 0; b2 < 16u && ((busy >> b2) & 1u); b2++) ;
                    if (b2 == 16u) for (b2 = 1; b2 < 16u && ((used >> b2) & 1u); b2++) ;   /* none free for 3 passes: steal one this pass does not use (never bank 0) */
                    if (b2 == 16u) continue;                     /* 16 palettes in one pass: this one aliases (n & 15) */
                    old = R60_BANKPAL_U[b2];
                    if (old < 64u && R60_PALMAP_U[old] == b2) R60_PALMAP_U[old] = 0xFFu;
                    R60_BANKPAL_U[b2] = (uint8_t)pl; R60_PALMAP_U[pl] = (uint8_t)b2;
                    R60_REMAPMASK |= (uint16_t)(1u << b2);
                    used |= (uint16_t)(1u << b2);
                }
                used2 = used1; used1 = used;
                R60_BANKBUSY = (uint16_t)(used | used1 | used2);
            }
}
#ifdef R60_BKBENCH
/* R60BKBENCH ON THE RIG (2026-11-04, with R60NOBARS=): seven bars, four rows each, rows 196..223,
 * 16 px floor + value capped at 300, through the direct image like the phase bars. Read with
 * tools/rig_bkbench.py. The five bench bars are FRT ticks / 32 = 2 x SH-2 cycles a group
 * (512 groups): 196 SDRAM->SDRAM, 200 cart->SDRAM first touch, 204 SDRAM->FB, 208 cart->FB,
 * 212 cart->SDRAM again. 216 = the LIVE copy, cycles a copied row / 4 (r60_bk_stat[5] x 8 /
 * [3] / 4); 220 = the LIVE plotter, cycles a word (r60_bk_stat[6] x 8 / ([7] / 4)), both run
 * averages. ares (p15bbench, Neff tape) reads 25 47 36 30 17 | 86 35. */
extern volatile unsigned r60_bk_bench[8], r60_bk_stat[8];
__attribute__((noinline)) static void r60_bkb_bars(void)
{
    unsigned v[7], r, k;
    for (k = 0; k < 5u; k++) v[k] = r60_bk_bench[k == 4u ? 5u : k] / 32u;
    v[5] = r60_bk_stat[3] ? r60_bk_stat[5] * 2u / r60_bk_stat[3] : 0u;
    v[6] = r60_bk_stat[7] >= 32u ? r60_bk_stat[6] / (r60_bk_stat[7] / 32u) : 0u;
    for (r = 0; r < 28u; r++) {
        volatile uint32_t *q4 = FB_DIR((196u + r) * FB_W);
        unsigned len = 16u + (v[r >> 2] > 300u ? 300u : v[r >> 2]), x;
        for (x = 0; x < FB_W / 4u; x++) {
            unsigned b = x * 4u;
            q4[x] = b + 4u <= len ? 0xFFFFFFFFu : b >= len ? 0u : 0xFFFFFFFFu << (8u * (4u - (len - b)));
        }
    }
}
#endif
