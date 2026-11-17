/* r60_clear.inc -- the clear passes over the hidden bank, and the text heal.
 * Part of r60_main.c: included there, in this order, so the compiler sees one unit. */
#define CXCODE R60CODE __attribute__((noinline))
/* THE LISTS A BANK'S CLEAR WALKS: the slave's, and under MBAND the master's for the same bank (its
 * band's spans, uncached; the master appends to it only after CLEARED). Both fold into the same
 * per-row extents: a row's run covers every span of that row, whichever core drew it. */
#define CX_NL 1u
#define CX_L(bk, li)  (r60_rowspans[bk])
#define CX_N(bk, li)  ((unsigned)r60_rowspan_n[bk])
#define CX_SETN(bk, li, v) (r60_rowspan_n[bk] = (uint16_t)(v))
/* zero groups [g0, g1) of canvas row y: the line's ROWFILL rule (short -> CPU stores after the fill in
 * flight, sw154; else the fill engine, split at 256-word boundaries) */
static CXCODE void r60_clr_emit(unsigned y, unsigned g0, unsigned g1)
{
    unsigned a = 0x100u + y * 160u + (g0 << 1), left = (g1 - g0) << 1;
    CLRP(2, 1); CLRP(3, g1 - g0);
    do {
        unsigned room = 0x100u - (a & 0xFFu), nn = left < room ? left : room;
        R60_FENW(7);
        if (left < 12u) {                        /* short: CPU stores (never during a fill, sw154) */
            volatile uint32_t *d = FB_DIR(y * FB_W + (g0 << 2)); unsigned nw = left >> 1;
            while (nw--) *d++ = 0;
            CLRP(5, 1);
            return;
        }
        MARS_VDP_FILLEN = (uint16_t)(nn - 1u);
        MARS_VDP_FILADR = (uint16_t)a;
        MARS_VDP_FILDAT = 0u;
        a += nn; left -= nn; g0 += nn >> 1;
        CLRP(4, 1);
    } while (left);
}
static __attribute__((noinline)) void cx_mark(unsigned y, unsigned lo, unsigned hi)   /* the text pass's view of what was cleared (cart: the rare paths) */
{
    cleared_rows[y >> 5] |= 1u << (y & 31u);
    { unsigned tr = y >> 3; if (tr < 28u) { if ((lo << 2) < cl_x0[tr]) cl_x0[tr] = (uint16_t)(lo << 2); if ((hi << 2) > cl_x1[tr]) cl_x1[tr] = (uint16_t)(hi << 2); } }
}
/* THE ROW EXTENT CLEAR (R60CLREXT, 2026-10-24). One run per row, [first span start, last span end)
 * of the row's spans, instead of one fill per span. In the demo fight (R60CLRPROBE, frames 600-700,
 * sw309) 417 spans a pass took 395 fills, 3,766 longwords (the parts' overlaps cleared twice) and
 * 46 lines (event ring, ev 8 -> 15); the rows are ~110, and their runs are 136 fills, 3,512 longwords,
 * 36 lines. What is left is ~15 lines of fill-engine busy time the CPU waits on (7 + 3 cycles a word,
 * ares vdp.cpp:88) and the bookkeeping. The run also zeroes the gaps between a row's spans. A gap holds
 * no sprite pixel (every drawn row is a span, in the slave's list or under MBAND the master's; an
 * overflowed list clears the whole canvas) and its text is redrawn (cl_x0/cl_x1 span the row's
 * min..max), but it can hold a KEPT sprite: the run is cut around the row's kept extent [kl, kh), and
 * a cleared span reaching into that extent clears its own part there, as before. A hold (planes in
 * the bank, a display hold) takes the per-span clear. The kept spans of each list are carried to its
 * front, in order. */
#define CX_ROW  ((uint8_t *)0x06029440u)    /* row y at 4y: 255 - lowest start, highest end of ALL its spans, 255 - lowest kept
                                             * start, highest kept end; 0 = none; zero between clears. 0x29440-0x297BF reads
                                             * zero on the line (ares --watch-sdram over the attract: boot writes only);
                                             * R60PRIOROW's keep masks and row cells and R60MBTIME's ring, off the line */
#define CX_ROWL ((uint32_t *)0x06029440u)
static uint8_t cp_state, cp_bk, cp_ylo, cp_yhi;           /* state 0 to do, 1 ready, 2 refused */
static __attribute__((noinline)) void r60_clr_exact(unsigned bk)   /* a hold: the per-span clear, as the line does it (cart: rare) */
{
    unsigned li, k, n, carry;
    if (cp_state == 1) for (k = cp_ylo; k <= cp_yhi; k++) CX_ROWL[k] = 0;   /* a precompute for these lists goes unused */
    cp_state = 0;
    for (li = 0; li < CX_NL; li++) {
        uint32_t *L = CX_L(bk, li);
        n = CX_N(bk, li); carry = 0;
        for (k = 0; k < n; k++) {
            uint32_t sp = L[k]; unsigned y = sp >> 24, g0 = (sp >> 8) & 0xFFu, g1 = sp & 0xFFu;
            if (ks_keep[(sp >> 16) & 63u]) { L[carry++] = sp; continue; }
            r60_clr_emit(y, g0, g1); cx_mark(y, g0, g1);
        }
        CX_SETN(bk, li, carry); CLRP(10, carry);
    }
}
static __attribute__((noinline)) void r60_clr_carry(unsigned bk)   /* cart: each list's kept spans up front, in order */
{
    unsigned li, k, c, n;
    for (li = 0; li < CX_NL; li++) {
        uint32_t *W = CX_L(bk, li);
        n = CX_N(bk, li); c = 0;
        for (k = 0; k < n; k++) if (ks_keep[(W[k] >> 16) & 63u]) W[c++] = W[k];
        CX_SETN(bk, li, c); CLRP(10, c);
    }
}
/* a cleared span reaching into its row's kept extent clears its own part there (cart: only with kept sprites) */
static __attribute__((noinline)) void cx_inner(uint32_t sp)
{
    unsigned y = sp >> 24, g0 = (sp >> 8) & 0xFFu, g1 = sp & 0xFFu, kh, kl;
    if (!(kh = CX_ROW[y * 4u + 3u])) return;
    kl = 255u - CX_ROW[y * 4u + 2u];
    if (g0 < kl) g0 = kl; if (g1 > kh) g1 = kh;
    if (g1 > g0) { r60_clr_emit(y, g0, g1); cx_mark(y, (sp >> 8) & 0xFFu, sp & 0xFFu); }
}
#define R60_CLR_RUN(y_, a_, b_) r60_clr_emit((y_), (a_), (b_))
/* the runs, rows ylo..yhi: [lo, hi) of the row's spans cut around its kept extent; zeroes CX_ROW */
static CXCODE void r60_clr_rows(unsigned ylo, unsigned yhi)
{
    unsigned y, k;
    for (y = ylo & ~31u, k = 1u; y <= yhi; y++, k = (k << 1) | (k >> 31)) {
        uint8_t *r = CX_ROW + y * 4u;
        unsigned lo = 255u - r[0], hi = r[1], kl = 255u - r[2], kh = r[3], tr = y >> 3, m = 0;
        CX_ROWL[y] = 0;
        if (!hi) continue;
        if (!kh || kh <= lo || kl >= hi) { R60_CLR_RUN(y, lo, hi); m = 1; }
        else {
            if (lo < kl) { R60_CLR_RUN(y, lo, kl); m = 1; }
            if (kh < hi) { R60_CLR_RUN(y, kh, hi); m = 1; }
        }
        if (!m) continue;
        cleared_rows[y >> 5] |= k;               /* k = 1 << (y & 31), rotated along (no barrel shifter) */
        if (tr < 28u) { if ((lo << 2) < cl_x0[tr]) cl_x0[tr] = (uint16_t)(lo << 2); if ((hi << 2) > cl_x1[tr]) cl_x1[tr] = (uint16_t)(hi << 2); }
    }
}
/* THE WALK (no precompute stands for this bank; cart under R60CLRPRE): 1, every list's per-row
 * extents of all spans and of the kept ones; 2, with kept spans, the cleared spans' parts inside the
 * kept extents; 3, the runs; 4, the carry */
static __attribute__((noinline)) void r60_clr_ext(unsigned bk)
{
    unsigned li, k, n, y, ylo = 255u, yhi = 0, nk = 0, sorted = 1;
    for (li = 0; li < CX_NL; li++) {
        const uint32_t *L = CX_L(bk, li);
        unsigned lead = 1;
        n = CX_N(bk, li);
        for (k = 0; k < n; k++) {
            uint32_t sp = L[k]; unsigned g0 = (sp >> 8) & 0xFFu, g1 = sp & 0xFFu, kept = ks_keep[(sp >> 16) & 63u];
            uint8_t *r;
            y = sp >> 24;
            if (y >= FB_H) { if (!kept) { r60_clr_emit(y, g0, g1); cx_mark(y, g0, g1); } continue; }   /* never, by r60_sprite's row clip */
            r = CX_ROW + y * 4u;
            if (y < ylo) ylo = y; if (y > yhi) yhi = y;
            if (255u - g0 > r[0]) r[0] = (uint8_t)(255u - g0);
            if (g1 > r[1]) r[1] = (uint8_t)g1;
            if (kept) {
                nk++; if (!lead) sorted = 0;
                if (255u - g0 > r[2]) r[2] = (uint8_t)(255u - g0);
                if (g1 > r[3]) r[3] = (uint8_t)g1;
            } else lead = 0;
        }
    }
    if (nk) for (li = 0; li < CX_NL; li++) {
        const uint32_t *L = CX_L(bk, li);
        n = CX_N(bk, li);
        for (k = 0; k < n; k++) if (!ks_keep[(L[k] >> 16) & 63u] && (L[k] >> 24) < FB_H) cx_inner(L[k]);
    }
    if (ylo <= yhi) r60_clr_rows(ylo, yhi);
    if (!sorted) r60_clr_carry(bk);
    else for (li = 0; li < CX_NL; li++) {        /* the kept spans lead each list: they stay where they are */
        unsigned c = 0; const uint32_t *L = CX_L(bk, li);
        n = CX_N(bk, li);
        while (c < n && ks_keep[(L[c] >> 16) & 63u]) c++;
        CX_SETN(bk, li, c); CLRP(10, c);
    }
}
/* THE ROW EXTENTS, PRECOMPUTED WHILE THE SLAVE WAITS (R60CLRPRE, 2026-10-24). The bank the next pass
 * clears was drawn two passes ago and its span lists do not change until that clear (the master's
 * band for it was awaited), so its per-row extents and each sprite's run of spans per list are
 * computed at the top of the slave loop, where it then waits for the flip and the snapshot. At the
 * clear only the kept sprites' spans are walked; the runs are the extents of ALL the row's spans cut
 * around the kept extent, which is the cut the walk makes. */
/* sprite i's run of spans in list li (a list holds a sprite's spans as one run, its rows monotonic, so
 * the run's first and last span bound its rows): [li][i] first index, count (0: none). In the event
 * ring's idle middle, [128..223] (384 B): .bss is at its guard. */
#define cp_first ((uint16_t (*)[64])&r60_evring_mem[128])
#define cp_cnt   ((uint8_t (*)[64])&r60_evring_mem[192])
static uint16_t cp_n[CX_NL]; static uint32_t cp_x[CX_NL]; /* cp_x = first ^ last span */
static __attribute__((noinline)) void r60_clr_pre(unsigned bk)   /* cart: it runs where the slave would idle */
{
    unsigned li, k,
        i,
        ylo = 255u, yhi = 0;
    cp_state = 2;
    if (r60_rowspan_full[bk]) return;
    for (li = 0; li < CX_NL; li++) {
        const uint32_t *L = CX_L(bk, li);
        unsigned n = CX_N(bk, li), prev = 64u, yp = 0, dir = 0;
        uint16_t *cf = cp_first[li]; uint8_t *cc = cp_cnt[li];
        for (i = 0; i < 64u; i++) cc[i] = 0;
        for (k = 0; k < n; k++) {
            uint32_t sp = L[k]; unsigned y = sp >> 24, g0 = (sp >> 8) & 0xFFu, g1 = sp & 0xFFu;
            uint8_t *r = CX_ROW + y * 4u;
            i = (sp >> 16) & 63u;
            if (y >= FB_H) goto refuse;
            if (i != prev) {
                if (cc[i]) goto refuse;              /* not one run: the clear walks the lists instead */
                cf[i] = (uint16_t)k; prev = i; dir = 0;
            } else if (y != yp) {                    /* the rows must run one way: first and last bound them */
                unsigned d = y > yp ? 1u : 2u;
                if (dir && d != dir) goto refuse;
                dir = d;
            }
            if (cc[i] == 255u) goto refuse;
            cc[i]++; yp = y;
            if (y < ylo) ylo = y; if (y > yhi) yhi = y;
            if (255u - g0 > r[0]) r[0] = (uint8_t)(255u - g0);
            if (g1 > r[1]) r[1] = (uint8_t)g1;
        }
        cp_n[li] = (uint16_t)n; cp_x[li] = n ? L[0] ^ L[n - 1u] : 0u;
    }
    if (ylo > yhi) return;                       /* nothing drawn: the walk is free */
    cp_bk = (uint8_t)bk; cp_ylo = (uint8_t)ylo; cp_yhi = (uint8_t)yhi; cp_state = 1;
    return;
  refuse:
    for (k = 0; k < FB_H; k++) CX_ROWL[k] = 0;
}
static __attribute__((noinline)) void r60_clr_fall(unsigned bk)   /* cart: no precompute for these lists */
{
    unsigned y;
    if (cp_state == 1) for (y = cp_ylo; y <= cp_yhi; y++) CX_ROWL[y] = 0;
    cp_state = 0;
    r60_clr_ext(bk);
}
static __attribute__((noinline)) void r60_clr_inner(unsigned bk)   /* cart: only with kept sprites */
{
    unsigned li, i, k, kylo = 255u, kyhi = 0;
    for (li = 0; li < CX_NL; li++) {
        const uint32_t *L = CX_L(bk, li);
        for (i = 0; i < 64u; i++) if (cp_cnt[li][i] && ks_keep[i]) {
            unsigned ya = L[cp_first[li][i]] >> 24, yb = L[cp_first[li][i] + cp_cnt[li][i] - 1u] >> 24;
            if (ya > yb) { unsigned t = ya; ya = yb; yb = t; }
            if (ya < kylo) kylo = ya; if (yb > kyhi) kyhi = yb;
        }
    }
    for (li = 0; li < CX_NL; li++) {
        const uint32_t *L = CX_L(bk, li);
        for (i = 0; i < 64u; i++) {
            unsigned c = cp_cnt[li][i], f = cp_first[li][i], ya, yb;
            if (!c || ks_keep[i]) continue;
            ya = L[f] >> 24; yb = L[f + c - 1u] >> 24;
            if (ya > yb) { unsigned t = ya; ya = yb; yb = t; }
            if (yb < kylo || ya > kyhi) continue;
            for (k = f; k < f + c; k++) cx_inner(L[k]);
        }
    }
}
static __attribute__((noinline)) void r60_clr_run(unsigned bk)   /* cart (once a pass; its loops are 2 x 64 tests and the kept sprites' spans): .ramtext keeps the per-row emit, and under MBAND the slot has no room for this */
{
    unsigned li, i, k, nk = 0, moved = 0, nkl[CX_NL];
    if (cp_state != 1 || cp_bk != bk) { r60_clr_fall(bk); return; }
    for (li = 0; li < CX_NL; li++) {             /* not the lists it was computed for: the walk */
        const uint32_t *L = CX_L(bk, li); unsigned n = CX_N(bk, li);
        if (cp_n[li] != n || (n && cp_x[li] != (L[0] ^ L[n - 1u]))) { r60_clr_fall(bk); return; }
    }
    cp_state = 0;
    for (li = 0; li < CX_NL; li++) {             /* 1: the kept sprites' extents, from their own spans */
        const uint32_t *L = CX_L(bk, li);
        unsigned kend = 0, nl = 0;
        for (i = 0; i < 64u; i++) {
            unsigned c = cp_cnt[li][i], f;
            if (!c || !ks_keep[i]) continue;
            f = cp_first[li][i]; nl += c; if (f + c > kend) kend = f + c;
            for (k = f; k < f + c; k++) {
                uint32_t sp = L[k]; unsigned g0 = (sp >> 8) & 0xFFu, g1 = sp & 0xFFu;
                uint8_t *r = CX_ROW + (sp >> 24) * 4u;
                if (255u - g0 > r[2]) r[2] = (uint8_t)(255u - g0);
                if (g1 > r[3]) r[3] = (uint8_t)g1;
            }
        }
        if (kend != nl) moved = 1;               /* kept spans not all ahead of the cleared ones */
        nk += nl; nkl[li] = nl;
    }
    if (nk) r60_clr_inner(bk);                   /* 2 */
    r60_clr_rows(cp_ylo, cp_yhi);                /* 3 */
    if (moved) r60_clr_carry(bk);                /* 4 */
    else { for (li = 0; li < CX_NL; li++) CX_SETN(bk, li, nkl[li]); CLRP(10, nk); }   /* the kept spans lead: they stay */
}

/* THE HEAL, SPREAD (R60TXTHEAL, 2026-10-24; cart). The sw261 heal forced a bank's whole text layer every
 * 16 of its passes: ~110 glyphs, ~80 slave lines in that one pass, and both banks' counters ran in step,
 * so the fight lost two passes in a row to it (sw313 frames 600-700: 11 of the 15 text spikes, every one
 * ~80 lines; the other 4 are pool LUT changes, 2 of them). Here each pass marks the next R60_TXTHEAL_N
 * non-blank cells of the bank pending (a cursor over the rows with content), so every glyph is still
 * redrawn within ~16 passes and no pass pays more than N. Marked BEFORE the keep decisions, so a kept
 * sprite never stands over a cell the clear phase wipes (the forced heal was set after them). */
#ifndef R60_TXTHEAL_N
#define R60_TXTHEAL_N 8u
#endif
static uint16_t txt_hc[2];
static __attribute__((noinline)) void r60_txt_heal(unsigned bk)
{
    unsigned n = 0, scanned = 0, i = txt_hc[bk];
    uint8_t *pd = txt_pend[bk];
    while (n < R60_TXTHEAL_N && scanned < 28u * 40u) {
        unsigned r = i / 40u;
        if (!txt_rowcnt[r]) {                    /* an empty row: skip it whole */
            unsigned nx = (r + 1u) * 40u;
            scanned += nx - i; i = nx >= 28u * 40u ? 0u : nx;
            continue;
        }
        {   uint16_t d = txt_copy[i];
            if (((d & 0x1FFu) || (d & 0x0E00u)) && !pd[i]) { pd[i] = 1; txt_rowpend[bk][r]++; n++; }
        }
        scanned++; if (++i >= 28u * 40u) i = 0;
    }
    txt_hc[bk] = (uint16_t)i;
}

static __attribute__((noinline)) void r60_txt_palmark(unsigned m)   /* cart: a LUT change's cells, pending in both banks */
{
    unsigned r, c, b;
    for (r = 0; r < 28u; r++) {
        if (!txt_rowcnt[r]) continue;
        for (c = 0; c < 40u; c++) {
            unsigned idx = r * 40u + c; uint16_t d = txt_copy[idx];
            if (!((d & 0x1FFu) || (d & 0x0E00u))) continue;
            if (!((m >> (((unsigned)d >> 9) & 7u)) & 1u)) continue;
            for (b = 0; b < 2u; b++) if (!txt_pend[b][idx]) { txt_pend[b][idx] = 1; txt_rowpend[b][r]++; }
        }
    }
}
/* A new sprite's priority cut can erase a retained glyph outside its old clear
 * spans. Include the new draw bounds in the text restoration damage too. */
static __attribute__((noinline)) void r60_text_touched(const uint16_t *b)
{
    unsigned r, lo = b[2] >> 3, hi = (b[3] + 7u) >> 3;
    if (b[1] <= b[0] || b[3] <= b[2]) return;
    for (r = lo; r < hi && r < 28u; r++) {
        unsigned y = r * 8u;
        if (!txt_rowcnt[r]) continue;
        cleared_rows[y >> 5] |= 1u << (y & 31u);
        if (b[0] < cl_x0[r]) cl_x0[r] = b[0];
        if (b[1] > cl_x1[r]) cl_x1[r] = b[1];
    }
}
