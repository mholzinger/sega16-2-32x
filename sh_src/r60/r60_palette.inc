/* r60_palette.inc -- bank line tables, the pen map and the CRAM commit.
 * Part of r60_main.c: included there, in this order, so the compiler sees one unit. */
/* THE LINE TABLE IS PER BANK, AND Hw32xInit WRITES ONE.
 *
 * It flips once, waits for the flip to take, and THEN fills the table (mars.c:43-48),
 * so exactly one of the two banks gets it. A renderer that flips every frame presents
 * the other bank half the time. On the board, a full-screen ramp written to all 224
 * rows of the hidden bank appeared on SCREEN ROW 0 ONLY, full width, 125 of 320 columns
 * lit -- which is what a bank whose line table was never written looks like. ares has a
 * valid table in BOTH banks and shows the whole ramp, so ares cannot see this at all.
 *
 * Writing it into both banks at boot costs 512 words once and cannot hurt either way. */
static __attribute__((noinline)) void r60_bank_init(void)   /* cart since MBAND (2026-10-24): boot, and the two whole-canvas clears */
{
    /* AND WIPE THE BANK ONCE. With the per-row clear the renderer only erases what it
     * drew, so whatever the bank powered up holding stays there for ever: measured, the
     * first row-clear build left all 224 rows carrying ~73 pixels of boot garbage under
     * the picture, where the old full clear had been hiding it every pass. */
    volatile uint32_t *d = FB_DIR(0);
    unsigned n = (FB_W * FB_H) / 4u;
    while (n--) *d++ = 0;
}

/* cart-resident since sw243: .ramtext is full and this is cold */ static void r60_line_table(void)
{
    volatile uint16_t *lt = (volatile uint16_t *)FB_WBASE;
    unsigned i;
    for (i = 0; i < FB_H; i++)
        lt[i] = (uint16_t)(i * (FB_W / 2u) + 0x100u);
    for (i = FB_H; i < 256u; i++)        /* overscan: the last image row, never the
                                          * table itself -- entries left at zero render
                                          * the table AS PIXELS (the lavender row) */
        lt[i] = (uint16_t)((FB_H - 1u) * (FB_W / 2u) + 0x100u);
}

/* Cold cart code: keep MODE off until BOTH complete banks, including packet
 * tails, have been initialized. No slave/68K ownership or boot posts change. */
static __attribute__((noinline)) void r60_boot_banks(void)
{
    while (!(MARS_SYS_INTMSK & MARS_SH2_ACCESS_VDP)) ;
    MARS_VDP_DISPMODE = MARS_VDP_MODE_OFF;
    /* MODE is latched in HBlank. Observe a complete fresh HBlank rather than
     * letting the first FS request race that latch after a MODE256 write. */
    while (MARS_VDP_FBCTL & MARS_VDP_HBLK) ;
    while (!(MARS_VDP_FBCTL & MARS_VDP_HBLK)) ;
    while (MARS_VDP_FBCTL & MARS_VDP_HBLK) ;
    uint16_t fs0 = MARS_VDP_FBCTL & MARS_VDP_FS;
    for (unsigned bank = 0; bank < 2u; bank++) {
        volatile uint32_t *p = (volatile uint32_t *)FB_WBASE;
        for (unsigned n = 0; n < 0x8000u; n++) *p++ = 0;
        r60_line_table();
        uint16_t fs = (uint16_t)(fs0 ^ ((bank + 1u) & 1u));
        MARS_VDP_FBCTL = fs;
        while ((MARS_VDP_FBCTL & MARS_VDP_FS) != fs) ;
    }
    /* Preserve the old console initializer's two palette writes. R60 has no
     * subsequent console calls requiring its private cursor/currentFB state. */
    (&MARS_CRAM)[0] = 0;
    (&MARS_CRAM)[255] = 0x7fffu;
}

/* THE 32X CRAM COMMIT. Sixteen banks of sixteen, bank b carrying sprite colour-set b,
 * straight from the port's own PAL_SH shadow. S16 packs a channel as four low bits plus
 * one high bit and 32X CRAM is BGR555, so each channel is (nibble << 1) | highbit --
 * five bits, no quantisation, which is the point of drawing on the 32X side instead of
 * the MD's three. Leaving it out entirely is why the first board launch was BLACK while
 * ares counted 2,427 sprites drawn: the framebuffer held correct indices and nothing
 * resolved them. */
/* SPRITE PALETTES TO 32X COLOUR BANKS (R60PALMAP, sw262). System 16B sprites carry a 6-bit
 * palette, 64 of them (jts16_obj_scan.v:184, pal <= tbl_dout[5:0]); 32X CRAM is 16 banks of 16.
 * The plotter took (e[4] & 0x0F) and the commit loaded bank b from palette b, so a sprite on
 * palette 16-63 drew with the colours of palette (n & 15): the wolf's hit flash (palette 0x11,
 * red 000f) drew yellow (palette 1), Mike's capture 2016-2317 and sw261.bs1. Now the slave
 * assigns each palette in this pass a bank: a palette keeps its bank while it stays in use,
 * and a new one takes only a bank unused by this pass and the two before it (the keep logic
 * reuses pixels drawn two passes ago, and the frame on screen must not change colour under
 * it). Identity is the start state, so palettes 0-15 keep the banks they always had. */
static uint8_t r60_palmap_m[64], r60_bankpal_m[16];
static uint16_t r60_remap_m;
#define R60_PALMAP_U   ((volatile uint8_t *)((uint32_t)r60_palmap_m | 0x20000000u))
#define R60_BANKPAL_U  ((volatile uint8_t *)((uint32_t)r60_bankpal_m | 0x20000000u))
#define R60_REMAPMASK  (*(volatile uint16_t *)((uint32_t)&r60_remap_m | 0x20000000u))   /* slave -> master: banks reassigned */
static void r60_palmap_init(void)
{
    unsigned i;
    for (i = 0; i < 64u; i++) R60_PALMAP_U[i] = (uint8_t)(i < 16u ? i : 0xFFu);
    for (i = 0; i < 16u; i++) R60_BANKPAL_U[i] = (uint8_t)i;
}
#define R60_BANK_OF(pal6) (R60_PALMAP_U[(pal6) & 63u] < 16u ? R60_PALMAP_U[(pal6) & 63u] : ((pal6) & 15u))
static uint16_t r60_cram_shadow[256] = { [0 ... 255] = 0xFFFEu };
/* SET WHEN CRAM WAS NOT OURS (r60_frontend_service): the shadow no longer describes CRAM, so
 * the next commit resends all 256. Without it a palette the game set under the boot window
 * reached CRAM only at the 64-commit resend: altbeastj's warning page drew in black pens
 * from the window's end (frame 146) to frame 203 (2026-10-30). */
static unsigned r60_cram_stale;
/* A RELEASED HOLD RESENDS TOO. The hold covers the boot and every load, which is when the game
 * rewrites its palettes en masse and when commits are lost (FM down, outside PEN): altbeastj's
 * warning page set its colours at frame 16 and they first reached CRAM at the 64-commit
 * resend, frame 203, 62 frames after this hold released. 256 writes per cut. */
#define R60_CRAM_RESEND() (r60_cram_stale = 1u)
static unsigned r60_cm_skip, r60_cm_pool;
volatile unsigned r60_cm_stats[8];       /* [0] commits [1] pool dirty [2] banks compared [3] entries written [4] resends [5] mask bits */
R60CODE static inline uint16_t r60_cram_eff(unsigned b, unsigned p)
{
    if (p == 15u) {                               /* a pool entry: the text colour parked there, or black */
        uint16_t c = R60_POOLCOL[b];
        return c == 0xFFFEu ? 0u : c;
    }
    if (p == 0u && b) {                           /* pen 0: the text seated there, else the through pen (reads as entry 0) */
        uint16_t c = R60_POOLCOL[15u + b];
        if (c != 0xFFFEu) return c;
        b = 0u;
    }
    {   unsigned pl = R60_BANKPAL_U[b]; if (pl > 63u) pl = b;
        return PAL_SH[1024u + pl * 16u + p]; }
}
R60CODE static void r60_cram_commit(void)
{
    unsigned b;
    /* ONE PEN WAIT PER BANK. Outside vblank the 32X VDP only accepts CRAM while PEN is
     * asserted (FBCTL bit 13; VDP.sv:165 reads back {VBLK,HBLK,PEN,...}), and sixteen
     * words fit comfortably in one hblank window. Bounded, and the timeouts counted:
     * a missed bank costs a stale colour set for one frame, never a tear. */
#define R60_PENWAIT() do {                                                            \
        uint16_t _t = r60_frt();                                                      \
        while (!(MARS_VDP_FBCTL & 0x2000u)) {                                         \
            if ((uint16_t)(r60_frt() - _t) > 3000u) { R60_DIAG2[9]++; break; }        \
        }                                                                             \
    } while (0)
    /* THE BAR PEN GOES FIRST, AND THAT ORDERING IS THE EXPERIMENT. 32X CRAM writes need
     * PEN (hblank/vblank) on real hardware; this commit is 256 word writes and the bar
     * pen used to be the 257th. If PEN expires partway through, the LATE entries never
     * land -- which is one explanation for both board symptoms at once: the white bar
     * absent (it was last) and sprites in the HIGH colour sets speckled (they are last
     * too). Written first, the bar is a direct read on that: if it appears, the commit
     * is running out of window and must be split across vints. */
    /* THE THROUGH PEN EVERY COMMIT, FIRST (2026-11-05): a pool-only commit and an unchanged bank
     * 0 skip the loop below, and the bar pen's write here had turned the pen WHITE until the next
     * full commit: Mike's "artifacts in the lower third" in play, where the score text keeps the
     * pool dirty and the grass punches the hero. */
    ((volatile uint16_t *)0x20004200u)[R60_THRU_PEN] = r60_thru_cram;
#if R60_THRU_PEN != R60_BAR_PEN
    ((volatile uint16_t *)0x20004200u)[R60_BAR_PEN] = 0x7FFFu;
#endif
#ifdef PORT_RIGCLK
    /* PORTRIGCLK: a second reserved pen, forced BLACK, for the dark blocks of the clock
     * strip. A zero byte does not reliably erase the bar rows (measured 2026-11-03: the
     * frame-3300 DRAM dump held pen 255 in blocks whose bit was 0, so the strip read
     * stale 1s), and pen 0 shows the MD plane through; 254 is an opaque explicit write. */
    ((volatile uint16_t *)0x20004200u)[R60_CLK_DARK] = 0x0000u;
#endif
#ifdef R60_RS_DARK
    ((volatile uint16_t *)0x20004200u)[R60_RS_DARK] = 0x0000u;
#endif
    {   /* RESEND EVERYTHING EVERY 64 COMMITS. The first commits at boot are dropped (FM
         * is not yet the SH-2's), yet they updated the shadow, so an entry that never
         * changes again was never written: the text sets read zero for the whole run
         * while their shadow was right (sw55; a ramp probe landed only the one entry
         * that kept changing). 256 writes once a second. */
        static unsigned ncommit;
        unsigned resend = ((++ncommit & 63u) == 1u) || r60_cram_stale;
        r60_cram_stale = 0;
        if (resend) { unsigned i; for (i = 0; i < 256u; i++) r60_cram_shadow[i] = 0xFFFEu; R60_DIAG2[0]++; }   /* 0xFFFE: 0xFFFF is a real colour */
    {   unsigned cm = R60_CRAMMASK_W, pd = R60_POOLDIRTY;
        R60_CRAMMASK_W = 0; R60_POOLDIRTY = 0;
        cm |= R60_REMAPMASK; R60_REMAPMASK = 0;
        r60_cm_skip = resend ? 0u : (~cm & 0xFFFEu);   /* banks the compare may skip this pass (never bank 0); the pool entries get their own pass below */
        r60_cm_pool = pd && !resend;
        r60_cm_stats[0]++; if (pd) r60_cm_stats[1]++; if (resend) r60_cm_stats[4]++; r60_cm_stats[5] += (unsigned)__builtin_popcount(cm & 0xFFFFu);
    }
    }
    /* (A one-off readback of CRAM 176-191 lived here and wrote 0x26028EA0, which is
     * R60_DIAG2[8] onward -- it silently overwrote the flip counters with colour words.
     * It answered its question: bank 11 reads back real colours. Removed.) */
    for (b = 0; b < 16; b++) {
        unsigned p;
        {   /* ONLY THE ENTRIES THAT CHANGED: on ares every CRAM write waits for PEN (~200
             * cycles, 36 lines a commit on the profile); a bank with no change costs 16 compares. */
            unsigned q, dirty = 0;
            if ((r60_cm_skip >> b) & 1u && !(R60_DISPHOLD == 2u && (b == 7u || b == 11u || b == 15u || b == 10u))) continue;   /* the master saw no change in this set */
            for (q = 0; q < 16; q++)
                if (r60_cram_eff(b, q) != r60_cram_shadow[b * 16u + q]) { dirty = 1; break; }
            if (!dirty && b && !(R60_DISPHOLD == 2u && (b == 7u || b == 11u || b == 15u || b == 10u))) continue;   /* bank 0 and the fill sets under the hold always; the pool entries are in the compare */
        }
        R60_PENWAIT();
        r60_cm_stats[2]++;
        for (p = 0; p < 16; p++) {
            /* +1024 IS CORRECT AND I BRIEFLY REMOVED IT. The sprite colour sets live
             * in the UPPER half of the shadow: measured at f1100, sets 0,1,2,3 are
             * ALL ZERO at PAL_SH[set*16] and hold 13-14 real colours at
             * PAL_SH[1024 + set*16] (set 0: 7DEE 0CDD 7ABB 09AA ...). Both heroes in
             * this scene are colour set 0, so reading the low half makes exactly them
             * black. Reverted; the sprites' invisibility is NOT this. */
            uint16_t v = r60_cram_eff(b, p);
            if (R60_DISPHOLD == 2u && (b == 7u || b == 11u || b == 15u || b == 10u)) {   /* the 32X fill's four MD lines (step B) */
                unsigned l = b == 7u ? 0u : b == 11u ? 1u : b == 15u ? 2u : 3u;
                extern uint16_t r60_fill_mdword(unsigned, unsigned);
                unsigned w = r60_fill_mdword(l, p), r3 = (w >> 1) & 7u, g3 = (w >> 5) & 7u, b3 = (w >> 9) & 7u;
                unsigned r5 = r3 << 2 | r3 >> 1, g5 = g3 << 2 | g3 >> 1, b5 = b3 << 2 | b3 >> 1;
                /* in the S16 word form the loop below converts (4 bits a channel, the LSBs at
                 * 12-14): a BGR555 value here was converted twice (sw96/97) */
                v = (uint16_t)((r5 >> 1) | ((g5 >> 1) << 4) | ((b5 >> 1) << 8) | ((r5 & 1u) << 12) | ((g5 & 1u) << 13) | ((b5 & 1u) << 14));
            }
            if (b >= 12u && b < 15u && v != r60_cram_shadow[b * 16u + p]) R60_DIAG2[1]++;   /* text entries about to be written */
            if (v == r60_cram_shadow[b * 16u + p] && (b | p)) continue;   /* entry 0 always:
                                          * the 68K's boot stages write it late (MDSTAGE,
                                          * md_main.c) and it must carry the through bit */
            r60_cram_shadow[b * 16u + p] = v;
            r60_cm_stats[3]++;
            unsigned r  = (((v) & 0xFu) << 1) | ((v >> 12) & 1u);
            unsigned g  = (((v >> 4) & 0xFu) << 1) | ((v >> 13) & 1u);
            unsigned bl = (((v >> 8) & 0xFu) << 1) | ((v >> 14) & 1u);
            if (b * 16u + p == R60_BAR_PEN) continue;      /* reserved */
#ifdef R60_RS_DARK
            if (b * 16u + p == R60_RS_DARK) continue;
#endif
            if (b * 16u + p == R60_THRU_PEN) continue;     /* reserved: the punch's through pen, written with entry 0 below */
            if ((b | p) == 0u) {                           /* entry 0's colour, bit 15: through, and what shows where the MD is backdrop */
                r60_thru_cram = (uint16_t)(0x8000u | (bl << 10) | (g << 5) | r);
                ((volatile uint16_t *)0x20004200u)[R60_THRU_PEN] = r60_thru_cram;
            }
#ifdef PORT_RIGCLK
            if (b * 16u + p == R60_CLK_DARK) continue;     /* reserved: the clock strip's dark pen */
#endif
            /* (CRAM bit 15, the per-pen 32X-over-MD priority, was tried here and
             * changed the display by not one byte -- as did the GLOBAL priority in
             * MARS_VDP_DISPMODE, which reads back 0x8081. Neither is what hides the
             * sprites. The old renderer masked bit 15 OFF, m_main.c:5812, so leaving
             * it clear is also what it did.) */
            /* ENTRY 0 CARRIES THE THROUGH BIT. ares vdp.cpp:71-81 (and the board agrees
             * with ares on office): with the 32X in front (DISPMODE PRI=1) an MD pixel
             * shows only where the 32X pixel's CRAM entry has bit 15 SET and the MD
             * pixel is not backdrop. Pen 0 is the sprite layer's transparent pixel, so
             * entry 0 gets bit 15 and the MD planes show through it; every other entry
             * stays in front. Without this the planes were loaded and invisible. */
            /* R60_THRU: entry 0 only on the line; pen 0 of every bank under R60TXTPOOL15; pen 0 of
             * the banks seating no text under R60TXTPOOLHYB. The fill sets under the hold are
             * excepted: their pen 0 is one of the four MD lines' colours (never bank 0). */
            ((volatile uint16_t *)0x20004200u)[b * 16u + p] =
                (uint16_t)((bl << 10) | (g << 5) | r |
                           ((R60_THRU(b, p) && !(R60_DISPHOLD == 2u && (b == 7u || b == 11u || b == 15u || b == 10u))) ? 0x8000u : 0u));
        }
    }
    if (r60_cm_pool) {   /* THE POOL'S OWN PASS: 31 entries compared, not 256 (a third of commits had only the text pool dirty) */
        unsigned s2, waited = 0;
        for (s2 = 0; s2 < R60_POOLN; s2++) {
            unsigned e2 = R60_POOLBYTE(s2), b2 = e2 >> 4, p2 = e2 & 15u;
            uint16_t v = r60_cram_eff(b2, p2);   /* the slot's colour, or (HYB, pen 0) entry 0's with the through bit */
            unsigned r, g, bl;
            if (v == r60_cram_shadow[e2]) continue;
            r60_cram_shadow[e2] = v;
            if (!waited) { R60_PENWAIT(); waited = 1; }
            r  = (((v) & 0xFu) << 1) | ((v >> 12) & 1u);
            g  = (((v >> 4) & 0xFu) << 1) | ((v >> 13) & 1u);
            bl = (((v >> 8) & 0xFu) << 1) | ((v >> 14) & 1u);
            ((volatile uint16_t *)0x20004200u)[e2] = (uint16_t)((bl << 10) | (g << 5) | r | (R60_THRU(b2, p2) ? 0x8000u : 0u));
        }
    }
    /* THE BAR PEN IS RESERVED AND FORCED WHITE. The old bars picked the brightest pen
     * in the live palette every window because a fixed index could be black and a bar
     * nobody can see measures nothing (m_main.c:14176-14184). This renderer OWNS CRAM,
     * so it can simply keep one entry: 255 is bank 15 pen 15, the last colour set, and
     * reserving it costs one colour of one set. */
    /* CRAM READBACK, THE PROBE THE RIG NOTES HAVE BEEN ASKING FOR. On the board the
     * sprites come out speckled and the white bar does not appear at all, which is one
     * symptom if CRAM writes are not landing: every index resolves to whatever the entry
     * held before. ares shows neither fault, so only the board can answer. Read two
     * entries straight back in the same window -- the bar pen, which is a constant, and
     * one game entry. */
    {
        uint16_t rb = ((volatile uint16_t *)0x20004200u)[R60_BAR_PEN];
#if defined(R60_PUNCHTHRU) && R60_THRU_PEN == R60_BAR_PEN
        if (rb == r60_thru_cram) R60_DIAG2[4]++; else { R60_DIAG2[5]++; R60_DIAG2[6] = rb; }
#else
        if (rb == 0x7FFFu) R60_DIAG2[4]++; else { R60_DIAG2[5]++; R60_DIAG2[6] = rb; }
#endif
        /* AND SAY IT WHERE THE BOARD CAN SHOW IT. Every 32X pixel resolves through CRAM,
         * so if CRAM is the fault no 32X-side instrument can report it -- the bars are
         * invisible for the same reason the sprites are speckled. The MD BACKDROP is a
         * different path entirely (MD CRAM entry 0, written by the 68K), and it shows
         * wherever the 32X layer is transparent. GREEN = the readback matched, RED = it
         * did not. */
    }
}
