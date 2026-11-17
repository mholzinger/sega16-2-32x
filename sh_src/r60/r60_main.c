#include "../../md_src/frontend.h"
static volatile unsigned frontend_paused;
/* THE REBUILD, P1 SKELETON (Mike's order, 2026-10-14): the old renderer is DELETED, not
 * optimised -- "it only worked for the 68K port". docs/design/REBUILD.md section 2 is the
 * design. This unit replaces sh_src/m_main.c and sh_src/s_main.c entirely.
 *
 * THE WHOLE CONTRACT WITH THE REST OF THE ROM IS FOUR SYMBOLS -- m_main, s_main, visr_vbi
 * and amb_dma_handler, the four that sh_src/mars_start.s vectors. Everything else between
 * the renderer and the game is DATA in SDRAM that the port writes through its A68VID
 * macros: the sprite table at SPR_SNAP, the tilemap, the palette at PAL_SH, the text page.
 * That is why the old renderer can be removed without touching the port at all.
 *
 * WHAT P1 DOES AND DOES NOT DO. It draws the port's own sprite records straight into the
 * HIDDEN framebuffer bank through the 32X overwrite image, and flips. There is no sbuf, no
 * blit, no page capture, no restore, no slot cache and no allocator -- the five subsystems
 * that made the old renderer a 68K-era machine. The MD plane path is NOT here yet; it is
 * the one piece REBUILD says to carry over by copy, and it comes in the next build so this
 * one can be measured on the board on its own.
 *
 * THE HOT CODE LIVES IN .ramtext, which mars_start.s copies to SDRAM at boot. With the old
 * renderer gone that section was EMPTY and the link failed on its own assert ("size must be
 * a non-zero multiple of 4") -- a useful reminder that .ramtext is not an optimisation here
 * but the place the renderer runs from.
 *
 * THE OVERWRITE IMAGE IS WHY THERE IS NO TRANSPARENCY TEST. Writing a byte of 0 through
 * 0x24020000 does not store (SILICON.md's zero-byte law), so a sprite's transparent pixels
 * cost a write that the hardware drops and nothing else. The old renderer's per-pixel
 * `(pix-1) < 14u` test, its punch, and its mask machinery all exist to avoid a store the
 * hardware already refuses.
 */
#include "mars.h"
#include "sdram_map.h"
#include "game_cfg.h"
#include "r60_zoom_keep.h"
#include "r60_tiles.h"

/* .ramtext is where the renderer RUNS: mars_start.s copies it to SDRAM at boot, and with
 * the old renderer deleted the section was empty and the link failed on its own assert. */
#define R60CODE __attribute__((section(".ramtext")))

#define FB_ROW0       0x200u            /* the line table occupies the first 0x200 bytes */
#define FB_W          320u
#define FB_H          224u
/* BISECT STEP 1 (R60_FBCACHED): WRITE THE FRAMEBUFFER THROUGH THE CACHED WINDOW.
 * marks2's blit_half, the only writer ever proven to land on the MiSTer, stored through
 * 0x04000200 + y*320 (write-through; stores ride the SH-2's 4-deep write buffer,
 * native1 m_main.c:3119-3124). r60 has only ever used the uncached alias 0x24000000.
 * ares does not distinguish the two. Reads stay UNCACHED (FB_DIR_U): a cached read can
 * answer from a line filled before the last flip, the other bank's pixels.
 *
 * THE CACHED WINDOW IS THE DEFAULT because it is the write path marks2's blit_half used
 * (native1 m_main.c:3119-3124) and it costs nothing on ares. It is NOT the board fix it
 * was first announced as: the uncached line r60win1 draws the same full picture on
 * mister.office.local (2026-10-14 21:24); the speckled quarter-picture was the tv rig.
 * R60_FBUNCACHED=1 restores the uncached alias for an A/B. */
#ifdef R60_FBUNCACHED
#define FB_WBASE      0x24000000u
#else
#define FB_WBASE      0x04000000u
#endif
#define FB_OVR(off)   ((volatile uint8_t *)(FB_WBASE + 0x20000u + FB_ROW0 + (off)))
#define FB_DIR(off)   ((volatile uint32_t *)(FB_WBASE + FB_ROW0 + (off)))
#define FB_DIRB(off)  ((volatile uint8_t  *)(FB_WBASE + FB_ROW0 + (off)))
#define FB_DIR_U(off) ((const volatile uint32_t *)(0x24000000u + FB_ROW0 + (off)))
/* THE PUNCH'S READ-MODIFY-WRITE GOES THROUGH THE UNCACHED ALIAS (2026-10-24). It read through the
 * cached window (FB_DIR): the read allocates a line, the fill engine and the overwrite-image
 * plotter never touch that line, and the SH-2 window shows the OTHER bank after the next flip, so
 * a later punch at the same address read a line filled one or many passes earlier and wrote its
 * bytes back: old sprite pixels resurrected outside every span on the row (ares models the SH-2
 * cache, sh7604/cache.cpp; the tape, cxidg 2026-10-24: row 183 x 164-175 held the same bytes in
 * one bank for 13 passes while sprite 1 moved, its clear and its redraw both landing around them).
 * R60PUNCHCACHED=1 restores the cached read. */
#define FB_RMW(off)   ((volatile uint32_t *)(0x24000000u + FB_ROW0 + (off)))

#define SPR_SNAP      ((const volatile uint16_t *)SDRAM_SPR_SNAP)  /* 64 x 8 words, the port's */
/* THE SLAVE'S OWN COPY OF THE SPRITE TABLE (R60_TICKLATE). The master copies SPR_SNAP
 * here right before GO, while the slave is idle, and then runs the game tick WHILE the
 * slave draws (REBUILD section 2: tick and draw in parallel). The tick rewrites
 * SPR_SNAP freely; the slave never reads it. Both sides use the uncached alias. */
static uint16_t r60_snap2_mem[2][512] __attribute__((aligned(16)));
#define R60_SNAP2     ((volatile uint16_t *)((uint32_t)r60_snap2_mem[0] | 0x20000000u))
#define R60_SNAPSLOT(k) ((volatile uint16_t *)((uint32_t)r60_snap2_mem[(k) & 1u] | 0x20000000u))
/* R60_SLAVEWIN: THE SLAVE OWNS THE FRAME. The master only ticks the game and publishes a
 * sprite-table snapshot after each tick (R60_SNAPSEQ, two slots). The slave waits for a
 * new snapshot with the window closed and the flip applied, draws it, then does the
 * window itself at once: plane packet, CRAM commit, flip request, FM down. The 68K
 * closes the span with its post as before. MEASURED on ares with the event ring: the
 * master was inside the game tick when the slave finished and saw DONE only at the next
 * vint edge, after the 68K had already run, so every frame cost two vints. */
#define R60_SNAPSEQ   (*(volatile uint16_t *)0x26028E36u)   /* snapshots published */
#define R60_DRAWSEQ   (*(volatile uint16_t *)0x26028E38u)   /* the one the slave draws */
/* R60_EVRING: the master's beat as a ring of (event, frame, line-from-vint). Master FRT
 * only, so every entry is on one clock. Events: 1 vint, 2 GO, 3 DONE seen (span open +
 * FS request), 4 post seen (span closed), 5 flip applied (FPEND cleared).
 * THE RING IS [0..127] AND ITS INDEX [255]; [128..247] are the slave's (R60CLRPRE's sprite
 * runs [128..223], R60CLRPROBE's counters [224..247]: .bss is at its guard). A reader parses
 * [0..127] only. */
static uint32_t r60_evring_mem[256] __attribute__((aligned(16)));
#define R60_EVRING    ((volatile uint32_t *)((uint32_t)r60_evring_mem | 0x20000000u))
#define LOGOCOUNT(ph) ((void)0)
#define R60_EVPUTS(ev) do { uint16_t _l = (uint16_t)(r60_frt() - R60_TVINT) / 184u; \
        R60_EVRING[R60_EVRING[255] & 0x7Fu] = ((uint32_t)(ev) << 28) | ((uint32_t)(R60_DIAG[0] & 0xFFFu) << 16) | _l; \
        R60_EVRING[255] = (R60_EVRING[255] + 1u) & 0x7Fu; } while (0)
#define R60_EVPUT(ev) do { uint16_t _l = (uint16_t)(r60_frt() - R60_TVINT) / 184u; \
        R60_EVRING[R60_EVRING[255] & 0x7Fu] = ((uint32_t)(ev) << 28) | ((uint32_t)(r60_frame & 0xFFFu) << 16) | _l; \
        R60_EVRING[255] = (R60_EVRING[255] + 1u) & 0x7Fu; } while (0)
#define PAL_SH        ((const volatile uint16_t *)SDRAM_PAL_SH)  /* the port's palette shadow */

/* PASS SEQUENCE. GO is the pass leased to renderer work; DONE echoes the last
 * completed slave pass. Snapshot ownership is tracked separately by SNAPSEQ and
 * DRAWSEQ. See docs/design/RENDERER-PHASES.md for the full ownership table. */
#define R60_GO        (*(volatile uint16_t *)0x26028E00u)
#define R60_DONE      (*(volatile uint16_t *)0x26028E02u)
/* THE CONTROL WORDS END AT 0x28E1F AND THE COUNTERS START AT 0x28E20. The first
 * layout put R60_TDROP at 0x28E12, which is the low half of the LONGWORD R60_DIAG[0]:
 * every FM drop stamped the master's vint counter, which then read 48,984 for 687
 * actual entries. A second census block proved the ISR itself was fine. */
#define R60_DIAG      ((volatile uint32_t *)0x26028E40u)   /* [0] vints [1] flips [2] frames
                                                            * drawn [3] sprites drawn */

/* THE PHYSICAL WRITE BANK. Declared OUT HERE deliberately: it is read and written
 * unconditionally (the plotter's spans, the per-bank tables, the row mask, and the
 * flip at `r60_bkp ^= 1`), but it used to live inside the R60_KEEPSPR block below,
 * so any build without that flag failed to compile -- the third instance of a
 * declaration hiding inside a flag block (builder, 2026-10-19). */
static unsigned r60_bkp;


/* THE PORT->RENDERER DATA INTERFACE, and it is two words. The port's tile-RAM store
 * sites mark a page bit here and the renderer decides what that means; the old renderer
 * hashed 13 pages to discover the same fact and marks2 replaced the hash with these marks.
 * P1 does not consume them yet -- it draws no tiles -- but the port writes them
 * unconditionally, so they must exist or the rom does not link. Keeping the names and the
 * types exactly as m_main.c declared them is what makes the renderer swappable. */
/* THE FRAMEBUFFER DRAIN RATE IS THE LAW ON THIS BOARD, and it is a RATE, not a count.
 * The core's FB write FIFO is FOUR entries (VDPFIFO.v:80, lpm_numwords = 4, width 35 =
 * {A[17:1],UWR,LWR,DI}) and it drains ONE 16-bit write per pop + 5 wait + 1 = ~7 cycles
 * of clk_sys (VDP.sv:234-247), which is about 130 ns, about THREE SH-2 cycles. When it
 * is full the VDP withholds ACK and the CPU should stall -- but the interface's
 * in-flight guard is commented out (IF.sv:974-983), so a store issued while the
 * previous one waits RE-LATCHES OVER IT and the earlier write is lost. A loop that
 * sustains more than one write per ~3 SH-2 cycles loses the excess; bursts of up to 4
 * are absorbed. That is the shape of everything measured on this board: the tighter the
 * loop the worse it is (a solid-bar plot with no cart read between stores put 2 pixels
 * of 320 on screen), and four times fewer but wider stores did not help because a
 * 32-bit store is TWO FIFO entries.
 *
 * R60_PACE is the knob: nops after each framebuffer store. On real silicon the same
 * loop simply stalls instead of losing writes, so pacing costs nothing there. */
#define R60_PACE 0
#define R60_STR2(x) #x
#define R60_STR(x)  R60_STR2(x)
#define R60_SPACE() do { } while (0)

static uint32_t r60_rowpix;             /* sprite row-pixels written, this pass */
/* WHICH ROWS THIS BANK HOLDS, so the next pass into it clears those and nothing else.
 * TWO masks, because the banks alternate: the rows to erase are the ones written two
 * passes ago, into THIS bank, not the ones written last pass into the other one.
 * MEASURED, and this is why it is worth doing: the pass costs 213 lines of a 262-line
 * frame at GOVERNOR=2 and 217 at GOVERNOR=1 while the pixels it puts down FALL from
 * 3,992 to 2,988 -- the cost is not tracking the work, it is the full-screen clear,
 * 17,920 long writes every pass whatever is on screen. */
static uint32_t r60_rowmask[2][7];
#define R60_ROWSET(m, y) ((m)[(y) >> 5] |= (uint32_t)1u << ((y) & 31))
volatile uint16_t port_tile_dirty;      /* one bit per 4 KB tilemap page */
volatile uint32_t pp_marks_p2;          /* the port's own mark counter */

extern const uint16_t altbeast_sprites[];   /* 512K WORDS big-endian, cart ROM */
extern const uint8_t  altbeast_tiles[];     /* 16384 tiles x 64 B, one byte a pixel */
/* THE TEXT LAYER (R60_TEXT): 64 x 28 cells in text RAM, screen columns 24..63, cell =
 * code (9 bits) | pal (3 bits) << 9, tiles from the tile ROM, 3-bit pixels, pixel 0
 * transparent (native1 compose_text). It is drawn by the slave after the sprites through
 * the overwrite image. Its colours: a text pal's eight entries are PAL_SH[pal*8 + c]; the
 * attract uses all eight pals (the high-score table) and the sprites use 32X CRAM sets
 * 0-11 and 15, so sets 12-14 (entries 192-239) carry up to SIX text colour groups, pals
 * with identical colours sharing one. The master plans the map each window. */
#define R60_TXTGRP    ((volatile uint16_t *)0x26028F80u)  /* pal -> group 0..5, 0xFF none (words: byte
                                                           * stores at odd uncached addresses read back
                                                           * as their even neighbour, sw51) */
#define R60_TXTPAL    ((volatile uint16_t *)0x26028FA0u)  /* group -> pal */
/* THE DISPLAY GATE (adopted from the old renderer, m_main.c:8315-8345; Mike 2026-09-05
 * "boot screen and transitions slow and littered with tiles", again 2026-10-15 from his
 * sw74 capture: garbage at every cut). The arcade hides every tilemap load behind its
 * video-enable bit, port 0xC40001 bit 5 (MAME segas16b misc_io_w set_display_enable(data
 * & 0x20); jts16_main.v:247): both cuts in the corpus are four black frames then the
 * whole scene. The port keeps the game's byte at DAT_00c40001 (layout.ld: 0x0602C801).
 * Our planes lag the game's writes by windows, so: blank while the game says off, and
 * after it says on keep blanking until two consecutive windows shipped no art and
 * blanked no cell, at least five windows after the rise (one rotation), capped. The
 * 68K drops the MD display on packet bit 13; the slave draws no sprite and no text and
 * clears each bank once. */
#define R60_VIDEO_EN  (*(volatile uint8_t *)0x2602C801u)
#define R60_DISPHOLD  (*(volatile uint16_t *)0x26028FE0u)
#define R60_HOLD_MAX  120u
#define R60_GATE_HELD (*(volatile uint32_t *)0x26028FE4u)
#define R60_GATE_REL  (*(volatile uint32_t *)0x26028FE8u)
#define R60_GATE_HOLES (*(volatile uint32_t *)0x26028FECu)
#define R60_GATE_SINCE (*(volatile uint32_t *)0x26028FF0u)   /* windows since the game's enable rose */
#define R60_GATE_FILLV (*(volatile uint32_t *)0x26028FF4u)   /* 32X fill: vints spanned << 16 | calls */
/* THE CANVAS OFFSET (step A of the incremental 32X fill, 2026-10-15). Screen pixel (x, y)
 * lives at framebuffer byte y * 320 + r60_xo + x: the line table starts every line
 * r60_xo >> 1 words in and the VDP shift register (0x20004102 bit 0) adds the odd pixel,
 * so the bank scrolls with no pixel copied and every writer adds r60_xo to x. Sega
 * bulletin 14: the shift bit is invalid when a line entry's low byte is 0xFF, so such a
 * frame takes one more word and no shift (a uniform 1-px error, never a per-row tear).
 * The V-ISR writes the shift bit with the flip from R60_SHIFTNEXT. */
static unsigned r60_xo;                                /* this pass's offset, pixels (even + shift) */
static unsigned r60_xo_next, r60_kw_next, r60_sh_next;
#define R60_PH ((volatile uint32_t *)0x26028FC0u)   /* slave pass phase ticks (probe): clear, offset, fill, sprites, text, passes */
static uint16_t r60_ph_t;
#ifdef R60_RIGSPLIT
/* R60RIGSPLIT (PROBE, 2026-10-24, the rig phase split): both cores' time, contiguous segments of
 * their own FRT (each stamp closes the segment since the last), 32-bit sums in uncached SDRAM at
 * 0x30C00 (free: the port work RAM ends at 0x30BFF, .ramtext starts at 0x31000; .hero_overlay is empty):
 *  slave  [0] wait (pass end -> t0) [1] keep (t0 -> clear start) [2] clear [3] prep (clear done ->
 *         hold fill done) [4] sprite phase [5] band wait (MBAND) [6] text [7] these bars [8] publish
 *         [9] cram commit [10] flip + span open; [12] plot [13] punch (inside [4]); [14] passes; [15] stamp
 *  master [16] idle [17] tick [18] snapshot [19] CLEARED wait [20] band [21] planes prepare
 *         [22] late band (inside the idle loop); [23] iterations; [24] stamp; [25] plot/punch t0
 *  wait buckets by the condition that blocked each spin: [26] no snapshot [27] the 68K span [28] flip
 *  pending ([0] keeps the last fragment); [32..64] the bars' window snapshot, [72..99] the painted values, [100..127] last lengths per bank.
 *  [65..69] wide probe IRQ sum/count/max and window snapshots (master writes65..67,
 *  slave writes68..69); mode4 uses70/71 for peak plot ticks / last plot total.
 * The bars (r60_rs_bars) replace every other bar, rows196..223. Mode1:1line/pixel;
 * mode2:plot/punch2lines/pixel; mode3:all slave durations8lines/pixel and row219
 * is mean C V-ISR FRT ticks. Master shares/window counters keep their units. */
#define RSX ((volatile uint32_t *)0x26030C00u)
static inline uint16_t r60_frt(void);
static __attribute__((noinline)) void r60_rs(unsigned k) { uint16_t n = r60_frt(); RSX[k] += (uint16_t)(n - (uint16_t)RSX[15]); RSX[15] = n; }
static __attribute__((noinline)) void r60_rm(unsigned k) { uint16_t n = r60_frt(); RSX[16u + k] += (uint16_t)(n - (uint16_t)RSX[24]); RSX[24] = n; }
#define RS(k) r60_rs(k)
#define RM(k) r60_rm(k)
#define PHT_B() (RSX[25] = r60_frt())
#define PHT_E(k) (RSX[12u + (k)] += (uint16_t)(r60_frt() - (uint16_t)RSX[25]))
#else
#define PHT_B() ((void)0)
#define PHT_E(k) ((void)0)
#endif
#ifndef R60_RIGSPLIT
#define RS(k) ((void)0)
#define RM(k) ((void)0)
#endif
#define R60_FATSPAN 4u
#define R60_FATCAP 160u
unsigned r60_artcap = 40u;                             /* art records a plane a window; the master lowers it under the 32X fill */
static unsigned r60_fill_anchor[2], r60_fill_prevk[2], r60_fill_rows[2], r60_fill_k, r60_fill_mode2;   /* step B per-bank canvas state */
extern void r60_fill_cells(volatile uint8_t *, volatile uint8_t *, unsigned, unsigned, unsigned, unsigned, unsigned);
extern void r60_fill_cram(const volatile uint16_t *);
static uint16_t txt_copy[28u * 40u];                   /* the slave's text layer state (hoisted: the clear phase needs it) */
static uint8_t  txt_pend[2][28u * 40u];
static uint16_t txt_init;
static uint8_t  txt_force[2];
static unsigned txt_xo[2];                             /* the offset this bank's glyphs were drawn at */
/* TXTFAST (2026-10-17): the text phase was 47 lines a pass in the demo, 19 of them a
 * 1,120-cell scan and 27 converting 45 glyphs at 886 cycles each. Rows with no content
 * and no pending cells are skipped by count; converted glyphs are cached per (pal, code)
 * and flushed when the pool LUT changes. */
static uint8_t  txt_rowcnt[28];                        /* cells with content per row */
static uint8_t  txt_rowpend[2][28];                    /* pending cells per row, per bank */
static uint8_t  txt_allpend[2];                        /* a forced pass: every row may hold pending cells */
static uint32_t txt_gc[64][16];                        /* converted glyphs: 8 rows x 2 longs (128: .bss guard) */
static uint32_t txt_gtag[64];                          /* (gen << 16) | (pal << 9) | code, 0 = empty */
static uint16_t txt_ggen = 1;
#define R60_SHIFTNEXT (*(volatile uint16_t *)0x26028FF8u)
#define R60_CRAMMASK_W (*(volatile uint16_t *)0x26028FB0u)   /* master -> slave: sprite colour sets whose words changed */
#define R60_POOLDIRTY  (*(volatile uint16_t *)0x26028FB2u)   /* master -> slave: the text pool's colours changed */
#define R60_MDHOLD     (*(volatile uint16_t *)0x26028FE2u)   /* the packet's bit 13 source: hold without the slave's extra window */
#ifndef R60_HOLD_HOLES
#define R60_HOLD_QUIETN 28u                /* sw180: records built by a 14-row window that count as a quiet one */
#define R60_HOLD_HOLES 80u                 /* a scrolling demo's steady edge reads ~60 over five windows */
#endif
#define R60_TXTBASE   192u
/* THE TEXT POOL (2026-10-16). The arcade's sprites use ALL 16 colour sets (MAME census over
 * frames 1-6000: sets 12-14 from frame 1733, the beast scenes), so the text cannot live in
 * sets 12-14: the white wolf (set 12) drew in text colours. Sprite pens 0 and 15 are never
 * plotted (R60_PX draws 1..14; 15 is the end nibble), so CRAM entries b*16 (b>=1) and b*16+15
 * are 31 free entries. The master allocates them to the (group, pen) pairs the visible text
 * actually uses (font pen masks x text RAM) and publishes a byte LUT per group; the slave's
 * text draw maps each glyph pixel through it. Words, uncached (the odd-byte trap, sw51). */
static uint16_t r60_txtpool_mem[128] __attribute__((aligned(4)));
#define R60_TXTLUT    ((volatile uint16_t *)((uint32_t)r60_txtpool_mem | 0x20000000u))        /* [8][8] FB byte per (pal, pen); [p][0] = 0 */
#define R60_POOLCOL   ((volatile uint16_t *)(((uint32_t)r60_txtpool_mem | 0x20000000u) + 128u)) /* [31] S16 colour word per pool slot, 0xFFFE = free */
#define R60_TXTLUTSEQ (*(volatile uint16_t *)(((uint32_t)r60_txtpool_mem | 0x20000000u) + 192u))
#define R60_TXTLUTPM  (*(volatile uint32_t *)(((uint32_t)r60_txtpool_mem | 0x20000000u) + 196u))   /* R60TXTLUTPAL: LUTSEQ << 16 | the pals whose LUT rows that change moved (one longword: read whole) */
/* THE HYBRID POOL (2026-11-04, measured on p15i: it seats every pair pool-15 borrowed, 0
 * windows short on the attract and the Neff tape). Slots 0-15 are pen 15 of every bank,
 * always; slots 16-30 are pen 0 of banks 1-15, available only while no sprite drew in that
 * bank for three passes (R60_BANKBUSY from the slave's palette pass). A seated pen-0 slot is
 * published in R60_POOL0MASK and the slave hands a NEW palette to such a bank last; when a
 * bank with seated text does go busy, its pairs re-seat next window. A pen-0 entry with no
 * text reads as entry 0: the through pen the baked rows need. */
#define R60_POOLN      31u
#define R60_POOLBYTE(s) ((s) < 16u ? (s) * 16u + 15u : ((s) - 15u) * 16u)
#define R60_POOL0FREE(b) (R60_POOLCOL[15u + (b)] == 0xFFFEu)   /* pen 0 of bank b (1..15) seats no text */
#define R60_THRU(b, p)  ((p) == 0u && ((b) == 0u || R60_POOL0FREE(b)))
volatile unsigned r60_txt_borrow[4];     /* [0] (pal,pen) pairs the pool could not seat, summed over windows [1] the worst window
                                          * [2] windows whose borrows exceed the banks idle for three passes (a pen-0 slot on an
                                          *     idle bank would not have seated them) [3] most busy banks seen in a borrowing window */
static uint16_t r60_bankbusy_m, r60_pool0_m;
#define R60_BANKBUSY  (*(volatile uint16_t *)((uint32_t)&r60_bankbusy_m | 0x20000000u))   /* slave -> master: banks drawn in the last three passes */
#define R60_POOL0MASK (*(volatile uint16_t *)((uint32_t)&r60_pool0_m | 0x20000000u))     /* master -> slave: banks whose pen 0 seats text (R60TXTPOOLHYB) */
#define TEXT_UW       ((const volatile uint16_t *)0x26026000u)
/* R60_TXTDIFF: the MASTER scans the 1,120 visible text cells once a window against its
 * own copy and publishes the changed ones (cell index, value); the slave keeps a cached
 * copy and redraws only those plus the cells in rows the clear wiped. Scanning text RAM
 * uncached on the slave every pass cost ~30 lines (sw46). */
static uint16_t r60_txtlist_mem[2][1 + 2 * 256] __attribute__((aligned(16)));   /* [parity] n, pairs */
#define R60_TXTLIST(k) ((volatile uint16_t *)((uint32_t)r60_txtlist_mem[(k) & 1u] | 0x20000000u))
#define R60_TXTSEQ    (*(volatile uint16_t *)0x26028F6Au)   /* lists published */
#define R60_TXTTAKEN  (*(volatile uint16_t *)0x26028F6Cu)   /* lists the slave applied */
#define R60_SPR_BANK(b) ((b) & 7)            /* GAME_SPR_BANK, US set */
extern void port_play_tick(void);

static volatile uint8_t  pp_due;        /* vints owed to the game, the port's own clock */
static volatile uint16_t r60_frame;
static volatile uint16_t r60_fs_want;
/* THE HANDSHAKE FLAGS LIVE IN THE UNCACHED DIAG BLOCK, not in .bss: as plain statics at
 * 0x06000926/0x0600092c the bootstrap store was emitted (mov.b r1,@r10) and the flag read
 * back ZERO on the very first loop iteration, so the first go was never launched and the
 * board stayed black -- but moving them uncached did NOT fix it, and that is the real
 * finding: THE WHOLE CONTROL BLOCK WAS SITTING IN THE PORT'S SPRITE RAM. The 68K's
 * sprite RAM at 0x440000 is 0x800 bytes and SPR_SNAP shadows only 0x400 of it
 * (cport_layout.py:176, and its own _SHADOW_TAIL guard names "sprite records 64+" as the
 * one failure mode this layout has), so every write the game makes to record 64 and up
 * lands on 0x26028800 -- SYNC in the old map, and GO/DONE/ready/launch here. GO read 0
 * for 686 vints with the store sitting in the disassembly at _m_main+0xd6.
 * The control block now lives in the documented free block 0x28D80-0x28FFF
 * (m_main.c:1909-1913). Uncached, so a dump reads it. */
#define R60_SHOWN     (*(volatile uint16_t *)0x26028E04u)
#define R60_PUB       (*(volatile uint16_t *)0x26028E0Au)
#define R60_FMDOWN    (*(volatile uint16_t *)0x26028E0Cu)  /* the 68K's vblank span is open */
#define R60_SEEN      (*(volatile uint16_t *)0x26028E0Eu)  /* COMM0 as it stood at the drop */
#define R60_TDROP     (*(volatile uint16_t *)0x26028E10u)  /* master FRT at the drop */
#define R60_SPANF     (*(volatile uint16_t *)0x26028E12u)  /* the frame whose span is DONE */
#define R60_TFLIP     (*(volatile uint16_t *)0x26028E1Au)  /* master FRT at the flip request */
#define R60_SPANV     (*(volatile uint16_t *)0x26028E1Cu)  /* r60_frame when the span opened */
#include "r60_span_lease.h"
static struct r60_span_lease r60_span_lease;
#define R60_FPEND     (*(volatile uint16_t *)0x26028E1Eu)  /* a flip is requested, not applied */
#define R60_DISPSET   (*(volatile uint16_t *)0x26028E2Au)  /* DISPMODE asserted once, late */
/* THE BOARD'S ONLY CHANNEL IS THE SLAVE'S PIXELS IN THE SPRITE ROWS. Four others have
 * been refused by it -- the framebuffer through both images at rows 216-223, 32X CRAM
 * from the master, the MD backdrop, and 32X CRAM from the 68K inside its vint. So the
 * instrument is built from the one thing that does come back. Two bars, drawn by the
 * SLAVE through the overwrite image in rows the board demonstrably shows, each 32 px a
 * step so it can be read off a photograph:
 *   A (rows 100-103) the 68K's liveness: posts against posts+backstops over 64 vints.
 *                    Full = it posts every span; zero = its vint never runs.
 *   B (rows 106-109) the master's own readback of the bank the slave just drew.
 *                    Full = the stores landed as written; zero = they did not.
 * Both max at 256 px (8 steps), which is the formula's own ceiling. */
#define R60_WA        (*(volatile uint16_t *)0x26028E2Cu)  /* bar A width, px */
#define R60_WB        (*(volatile uint16_t *)0x26028E2Eu)  /* bar B width, px */
#define R60_WPOST     (*(volatile uint16_t *)0x26028E30u)  /* posts at the last edge */
#define R60_WBACK     (*(volatile uint16_t *)0x26028E32u)  /* backstops at the last edge */
#define R60_FREEZE    (*(volatile uint16_t *)0x26028E34u)  /* R60FZ: stop issuing GO */
#define R60_CRAMDUE   (*(volatile uint16_t *)0x26028E14u)  /* this vint's CRAM commit is owed */
#define R60_TVINT     (*(volatile uint16_t *)0x26028E16u)  /* master FRT at the vint edge */
#define R60_BARA      (*(volatile uint16_t *)0x26028E22u)  /* bar A length: presented frames */
#define R60_BARB      (*(volatile uint16_t *)0x26028E24u)  /* bar B length: backstop raises */
#define R60_BARF      (*(volatile uint16_t *)0x26028E26u)  /* flips at the last window edge */
#define R60_BARK      (*(volatile uint16_t *)0x26028E28u)  /* backstops at the last edge */
#define R60_BAR_PEN   255u                                 /* reserved, forced white */
#define R60_CLK_DARK  254u                                 /* PORTRIGCLK only: reserved, forced black */
#if defined(R60_RIGSPLIT) && R60_RIGSPLIT >= 4
#define R60_RS_DARK 253u  /* diagnostic only: through254 and bar255 remain separate */
#endif
/* THE NO-READ PUNCH (R60PUNCHTHRU, 2026-11-05). The FG punch zeroed sprite pixels under priority
 * cells by read-modify-write through the uncached alias: 5 uncached bus ops a solid cell-row,
 * and on the FPGA every uncached access is ~16-18 cycles (R60BKBENCH=2: FB RMW 37.0 a longword,
 * uncached SDRAM RMW 36.5, ares 24.5 / 6.0). A pixel is "through" by its CRAM entry's bit 15
 * (ares vdp.cpp:71-81, S32X_MiSTer VDP.sv:506 YSO_N = ~(PRI ^ PIX_COLOR[15])), not by being 0,
 * so a second through pen written THROUGH THE OVERWRITE IMAGE (zero bytes skipped) punches with
 * plain stores: a mask row becomes up to three longwords of the pen in the masked bytes and 0
 * elsewhere. No framebuffer read, no cache hazard. The pen: 255 (already reserved, the bar pen,
 * one colour of bank 15) on a NOBARS build; 254 on a bars build (bank 15 pen 14, a probe's
 * price). Its entry follows entry 0's colour with bit 15 set, so where the MD is backdrop it
 * shows what pen 0 shows. Pixels it leaves outside a sprite's spans are through, as the 0s
 * were, and every row clear zeroes them. */
#define R60_THRU_PEN  R60_BAR_PEN
#define R60_THRU_4    (R60_THRU_PEN * 0x01010101u)
static uint16_t r60_thru_cram = 0x8000u;                   /* the pen's CRAM word: entry 0's colour with the through bit, kept by the commit */
/* THE BACKSTOP IS COUNTED IN VINTS, NOT TICKS.
 *
 * The span opens when the frame is finished, which is in the middle of ACTIVE DISPLAY,
 * and the 68K can only consume at its own vint -- so the span must survive the wait for
 * the next vblank and the consume inside it. A tick count cannot express that: sized at
 * vblank's own length (1,800) every span closed on the backstop and the 68K consumed 1;
 * sized at one frame (13,000) it worked until the per-row clear made the pass shorter,
 * the span then opened earlier and expired just before the vblank it was waiting for,
 * and consumes fell from 208 to 23. Two vint edges is the thing actually wanted: the
 * 68K gets a whole vblank, whenever in the frame the span opened. */
#define R60_SPAN_VINTS 2u
/* VBLANK ITSELF: ~36 of 262 lines at 46 master FRT ticks a line. */
#define R60_VBLANK_TICKS 1650u
/* FM IS A BYTE WRITE, AND THE BYTE BESIDE IT IS THE INTERRUPT MASK. The boot does it
 * right (mars_start.s:401-404): `mov #0x80,r0 ; mov.b r0,@r1` with r1 = 0x20004000 sets
 * FM, and the SH-2 interrupt ENABLES are the next byte, 0x20004001, written separately
 * on the following lines (0x08 = V int under VISR_FLIP). A 16-bit write to 0x20004000
 * therefore stamps the enables as a side effect: measured, `MARS_SYS_INTMSK |= 0x8000`
 * took the master's own V-ISR count from 687 to 16,513 and then 49,000 over the same
 * 700 frames. Byte writes only, and FM is read as a byte too. */
#define R60_DIAG2     ((volatile uint32_t *)0x26028E80u)   /* second census block */
volatile uint16_t r60_census[8];                                     /* .bss, read by symbol (_r60_census in s16.lst): [0] torn builds (master) [1] slave waits run out [2] last build ticks [3] max [4] game ticks dropped at the pp_due cap */
#define R60_TORN_M    (r60_census[0])                               /* census: direct builds FM fell under (master) */
#define R60_TORN_S    (r60_census[1])                               /* census: slave BUILDING waits that ran out */
#define R60_BUILD_T   (r60_census[2])                               /* census: last planes build, master FRT ticks */
#define R60_BUILD_TMAX (r60_census[3])                              /* census: the longest */
#define R60_FM_REG    (*(volatile uint8_t *)0x20004000u)
#define R60_FM_UP()   (R60_FM_REG = 0x80u)
#define R60_FM_DOWN() (R60_FM_REG = 0x00u)
#define R60_FM_IS_UP  (R60_FM_REG & 0x80u)

static inline uint16_t r60_frt(void)
{
    uint8_t h = SH2_FRT_FRCH, l = SH2_FRT_FRCL;
    return (uint16_t)((h << 8) | l);
}

#define r60_last_flip (*(volatile uint16_t *)0x26028E06u)

void speech_init(void); void speech_pump(void);   /* r60_speech.c: the slave's PWM speech lane */
void speech_pump_chunk(void);
#define R60_WAITBODY speech_pump_chunk()   /* every R60_WAITBODY site is the slave's (R60SPEECHSLAVE) */

#include "r60_palette.inc"           /* bank line tables, the pen map and the CRAM commit */
#include "r60_vblank.inc"            /* the frame clock: the vertical-interrupt body */
#include "r60_text_planes.inc"       /* the text scan and plan, and the MD plane packet (prepare, publish) */
#include "r60_plot.inc"              /* priority mask, punch-through, the row plotters and the zoom rows */
#include "r60_sprite.inc"            /* r60_sprite: one sprite record plotted live from the S16 art */
#include "r60_baked_sprite.inc"      /* the working-set pages, the baked-art lookup and r60_bk_sprite */
#include "r60_clear.inc"             /* the clear passes over the hidden bank, and the text heal */
#include "r60_macro.inc"             /* macro packets: bind, probe, plan and live draw */

__attribute__((noinline)) static void frontend_slave_pause(void)
{
    MARS_SYS_COMM6 = FE_SLAVE_IDLE;
    while (FE_WAITING(MARS_SYS_COMM12)) {
        unsigned delay;
        for (delay = 0; delay < 256u; delay++) __asm__ volatile ("nop");
    }
}
/* ACCEPTED-LINE SLAVE PASS ROADMAP:
 *   accept snapshot/page -> prepare keep/cache decisions -> clear stale spans
 *   -> clear stale text -> plot sprites and punch priority -> redraw text
 *   -> publish planes/CRAM -> request flip and open the 68K span -> DONE
 * Priority punching is coupled to each sprite; it is not a later global phase.
 * Optional probe and MBAND branches preserve this ordering contract. Full map:
 * docs/design/RENDERER-PHASES.md. */
R60CODE void s_main(void)
{
#ifdef TC_PADTEST
    /* Supply the boot heartbeat until the MD reaches its controller page,
     * then stop adapter traffic as well as rendering. PD never enters play. */
    __asm__ volatile ("ldc %0,sr" : : "r" (0xF0u) : "memory");
    while (MARS_SYS_COMM14 != 0x5044u) {
        unsigned delay;
        MARS_SYS_COMM6++;
        for (delay = 0; delay < 256u; delay++) __asm__ volatile ("nop");
    }
    for (;;) __asm__ volatile ("nop");
#endif
    while (MARS_SYS_COMM14 != 0xB007)
        MARS_SYS_COMM6++;                /* the startup handshake, unchanged */
#ifdef LAUNCHER
    { extern void launcher_secondary(void); launcher_secondary(); }   /* r60_launcher.c: until the primary is done with the launcher's page */
#endif
    /* A cold-boot request can park before the first render pass. */
    if (MARS_SYS_COMM12 == FE_REQUEST) frontend_slave_pause();
    R60_DIAG[4]++;                       /* slave reached its loop: 0 here means the
                                          * COMM14 handshake never completed */
#ifdef R60_RIGSPLIT
    { unsigned k; for (k = 0; k < 16u; k++) RSX[k] = 0; for (k = 25; k < 32u; k++) RSX[k] = 0; for (k = 32; k < 128u; k++) RSX[k] = 0; RSX[64] = R60_DIAG[0]; RSX[15] = r60_frt(); }
#endif
    speech_init();
    for (;;) {
        uint16_t go = R60_GO;
        unsigned r60_ws_active;
        /* THE PHYSICAL WRITE BANK (sw226). Tracked, not read: go & 1 advances every pass but a
         * HOLDFLIP pass does not flip, and the FS bit (sw225) does not hold on the MiSTer (the
         * rig stuck on the title, SEGA mark flickering). r60_bkp starts at go & 1 and toggles only
         * where a flip is issued; every per-bank table, the plotter's spans and the row mask use it. */
        unsigned bkp; { static unsigned bkp_init; if (!bkp_init) { r60_bkp = go & 1u; bkp_init = 1; } bkp = r60_bkp; }
        uint16_t t0;
        R60_DIAG[5]++;
        speech_pump_chunk();               /* the loop top spins while the slave waits for its next pass */
        if (!cp_state) r60_clr_pre(bkp);   /* the next clear's extents, where the slave would wait */
        /* PHASE 1 -- ACCEPT A SNAPSHOT AND CLAIM ITS PAGE/PASS LEASE. */
        {
            uint16_t seq = R60_SNAPSEQ;
            if (seq == R60_DRAWSEQ) { RS(26); continue; }   /* no new snapshot yet (RIGSPLIT: bucket 26) */
            if (R60_FMDOWN) {                            /* the 68K's span: close it */
                if (MARS_SYS_COMM0 != R60_SEEN) {
                    R60_FM_UP(); R60_FMDOWN = 0; R60_FMSOON_CLR(); R60_SPANF = R60_GO; R60_DIAG[14]++; R60_EVPUTS(12); MBTG(5);
                    R60_FLIP_NOW();
                } else if (
                    r60_span_expired(&r60_span_lease, MARS_SYS_COMM4,
                        (uint16_t)R60_DIAG[0],
                    ((R60_DISPHOLD == 1u || R60_PKTA_LEN[R60_PKTDONE & 1u] > 736u
                      || R60_PKTA_LEN[2u + (R60_PKTDONE & 1u)] > 400u) ? R60_FATSPAN : 2u)
                    )
                    ) {   /* classify the held packet, not the master's next build */
                    /* ... OR A LONG PACKET B (2026-11-05, the transformation seam): a live page switch zeroes the
                     * scroll, the pan strip ships 32 rows x 16 cells on BOTH planes (584 words each, 64 DMAs each)
                     * with the display up, and the 68K's consume ran 1779 -> 1781 line 498 on ares: the 2-vint
                     * timeout closed the span under B's DMAs (R60_DIAG[15] 0 -> 1 at that window, the run's only
                     * one) and plane B's columns 54-63 stayed level tiles for the whole cut. A B packet over 400
                     * words (a 7-row window is ~316) gets the FAT span too. */
                    /* under the game's own blank the 68K takes a FAT packet (~150 records, ~190 lines of DMA) and the
                     * slave has nothing to draw: give its span R60_FATSPAN vints. sw113 timed out 9-11 spans a cut,
                     * raised FM under the 68K's DMA, and the scene landed with garbage art */
                    R60_FM_UP(); R60_FMDOWN = 0; R60_FMSOON_CLR(); R60_SPANF = R60_GO; R60_DIAG[15]++; MBTG(5);
                    R60_FLIP_NOW();
                    { extern void r60_nt_flush(void); r60_nt_flush(); }   /* the rows the 68K had not DMA'd landed nothing: re-send every cell (r60_planes.c) */
                } else { RS(27); continue; }   /* RIGSPLIT: the 68K span */
            }
            if (R60_FPEND) {                             /* the flip must have taken */
                if ((uint16_t)(MARS_VDP_FBCTL & MARS_VDP_FS) == r60_fs_want) {
                    R60_FPEND = 0; R60_DIAG2[11]++;
                } else if ((uint16_t)((uint16_t)R60_DIAG[0] - R60_SPANV) >= 2u) {
                    R60_FPEND = 0; R60_DIAG2[12]++;
                } else { RS(28); continue; }   /* RIGSPLIT: flip pending */
            }
            if (MARS_SYS_COMM12==FE_REQUEST) {
                frontend_slave_pause();continue; /* reread snapshot after resume */
            }
            /* Busy claim precedes consumption. A mapper reservation wins by
             * making us roll back before DRAWSEQ, purge or atlas/prep access. */
            if (!r60_ws_claim_latest(&R60_GO,&R60_PAGE_PENDING,&R60_DRAWSEQ,&R60_SNAPSEQ,&go)) continue;
            R60_PAGE_SLAVE();                /* R60BAKEPAGES: a completed page switch purges the cache first */
            /* The accepted GO lease keeps this committed page stable until
             * DONE. Bind once, after its generation/cache boundary. */
            r60_ws_active = R60_PAGE_CURRENT == R60_WS_PAGE;
#ifdef R60_BAKEARTCHECK
            if (!r60_sp_cache.lookups) { unsigned k_; for (k_ = 0; k_ < 8u; k_++) r60_sp_checks[k_] = 0; }
#endif
            R60_SP_BEGIN(r60_page_seen);
            R60_DIAG[9]++; R60_EVPUTS(9);
        }
        t0 = r60_frt();                  /* the slave's own FRT: phi/8, 184 ticks a line */
#ifdef R60_RIGSPLIT
        RS(0); RSX[14]++;
#endif
        /* PHASE 2 -- PLAN RETAINED CONTENT, THEN CLEAR THE WRITE BANK.
         * Keep decisions live inside the clear block because they decide which
         * old sprite spans survive. See RENDERER-PHASES.md before moving them. */
        r60_rowpix = 0;
        /* CLEAR BY RECTANGLE. Every sprite the plotter drew into this bank last time left
         * its box in the bank's rect list; clear exactly those boxes (longword stores,
         * 4-aligned) and nothing else. Clearing whole rows cost 150 lines a pass with
         * stores and 51 with the fill engine once the figures covered 140 rows (sw36/37);
         * the boxes are about 1.5x the sprites' own pixels. Text cells clear themselves
         * when they change. Under R60_MBAND the master's boxes are in a second list. */
        {
            unsigned li, nl = 1u;
            { unsigned w3; for (w3 = 0; w3 < 7u; w3++) cleared_rows[w3] = 0; }
            { unsigned w3; for (w3 = 0; w3 < 28u; w3++) { cl_x0[w3] = 0xFFFFu; cl_x1[w3] = 0; } }
            {   /* WHICH SPRITES KEEP THEIR PIXELS THIS PASS */
                unsigned bk = bkp, i2, j2, nn = 0;
                const volatile uint16_t *tb = R60_SNAPSLOT(R60_DRAWSEQ);
                unsigned hold2 = R60_DISPHOLD == 1u, force2 = txt_force[bk] || (txt_init && txt_xo[bk] != r60_xo_next) || R60_DISPHOLD == 2u || r60_fill_rows[bk];
                static uint8_t chg[64]; uint16_t nbx[64][4];
                unsigned ux0 = 0xFFFFu, ux1 = 0, uy0 = 0xFFFFu, uy1 = 0;   /* union of pending text cells */
                ks_stats[2]++;
                for (i2 = 0; i2 < 64u; i2++) { ks_keep[i2] = 0; chg[i2] = 1; }
                prio_psel = PRIO_TEXT_U[0x740u]; prio_xs = (0xC0u - PRIO_TEXT_U[0x74Cu]) & 0x3FFu; prio_ys = PRIO_TEXT_U[0x748u] & 0x1FFu; prio_bank1 = r60_tile_bank1();
                while (nn < 64u && !(tb[nn * 8u + 2u] & 0x8000u)) nn++;    /* records to the terminator */
                if (!hold2 && !force2 && txt_init) {
                    uint8_t *pd = txt_pend[bk];
                    unsigned nmax = nn > ks_n[bk] ? nn : ks_n[bk], r;
                    for (r = 0; r < 28u; r++) {
                        if (!txt_rowpend[bk][r]) continue;   /* rows with no pending cell: nothing to scan */
                        for (i2 = r * 40u; i2 < r * 40u + 40u; i2++) if (pd[i2]) {
                            unsigned c = i2 % 40u, cx = c * 8u + r60_xo_next, cy = r * 8u;
                            if (cx < ux0) ux0 = cx; if (cx + 8u > ux1) ux1 = cx + 8u; if (cy < uy0) uy0 = cy; if (cy + 8u > uy1) uy1 = cy + 8u;
                        }
                    }
                    /* changed set: a record that differs from the bank's last, appeared, or vanished */
                    for (i2 = 0; i2 < nmax; i2++) {
                        unsigned same = 0, k3;
                        if (i2 < nn && i2 < ks_n[bk]) { same = 1; for (k3 = 0; k3 < 8u; k3++) if (tb[i2 * 8u + k3] != ks_rec[bk][i2][k3]) { same = 0; break; } }
                        chg[i2] = !same;
                        /* a changed record's new box, estimated: its rows exactly, its width from the old draw (else 128) */
                        if (i2 < nn) {
                            int xq = (int)(tb[i2 * 8u + 1u] & 0x1FFu) - 184 + (int)r60_xo_next;
                            unsigned w = (i2 < ks_n[bk] && ks_box[bk][i2][1] > ks_box[bk][i2][0]) ? (unsigned)(ks_box[bk][i2][1] - ks_box[bk][i2][0]) + 8u : 136u;
                            nbx[i2][0] = (uint16_t)(xq < 0 ? 0 : xq); nbx[i2][1] = (uint16_t)((xq < 0 ? 0 : xq) + w);
                            nbx[i2][2] = (uint16_t)(tb[i2 * 8u] & 0xFFu); nbx[i2][3] = (uint16_t)(tb[i2 * 8u] >> 8);
                        } else { nbx[i2][0] = nbx[i2][1] = nbx[i2][2] = nbx[i2][3] = 0; }
                    }
                    for (i2 = 0; i2 < nn && i2 < ks_n[bk]; i2++) {
                        const uint16_t *b = ks_box[bk][i2];
                        unsigned ok = !chg[i2] && b[1] > b[0] && b[3] > b[2] && !(tb[i2 * 8u + 2u] & 0x4000u);
                        if (r60_rowspan_full[bk]) ok = 0;   /* sw203: nothing is kept through the full-canvas clear that an overflowed span table triggers */
                        ks_repunch[i2] = 0;
                        if (ok && ks_punched[bk][i2] && (ks_fgkey[bk] != ((prio_bank1 << 26) | (prio_xs << 16) | prio_ys) || ks_fgpsel[bk] != (uint16_t)prio_psel || ks_fggen[bk] != R60_FGGEN)) {
                            /* its holes were cut for another FG: a sideways scroll alone redraws and re-punches
                             * only the rows it punched (the ground band under a standing sprite); anything
                             * else (vertical scroll, page select, cells) redraws it whole */
                            if ((ks_fgkey[bk] >> 26) == prio_bank1 && (ks_fgkey[bk] & 0xFFFFu) == prio_ys && ks_fgpsel[bk] == (uint16_t)prio_psel && ks_fggen[bk] == R60_FGGEN && (ks_pyr[bk][i2] >> 8) < (ks_pyr[bk][i2] & 0xFFu)) ks_repunch[i2] = 1;
                            else ok = 0;
                        }
                        if (!ok) continue;
                        if (ux1 > ux0 && b[0] < ux1 && ux0 < b[1] && b[2] < uy1 && uy0 < b[3]) { ks_stats[3]++; continue; }   /* a text cell clears inside it */
                        for (j2 = 0; j2 < nmax && ok; j2++) {
                            if (j2 == i2) continue;
                            /* A lower record can be unchanged but invalidated by another
                             * sprite or text clear. Its repaint can cover this keeper.
                             * Walk in draw order so these dependencies propagate. */
                            if (!chg[j2] && (j2 >= i2 || (R60_KS_KEEP_DRAW(j2)
                                && !ks_repunch[j2]
                            ))) continue;
                            if (j2 < ks_n[bk]) {     /* its old box is cleared this pass */
                                const uint16_t *o = ks_box[bk][j2];
                                if (o[1] > o[0] && b[0] < o[1] && o[0] < b[1] && b[2] < o[3] && o[2] < b[3]) ok = 0;
                            }
                            if (ok && j2 < i2 && j2 < nn) {   /* drawn before this one: its new pixels must go under */
                                const uint16_t *q = nbx[j2];
                                if (q[1] > q[0] && q[3] > q[2] && b[0] < q[1] && q[0] < b[1] && b[2] < q[3] && q[2] < b[3]) ok = 0;
                            }
                        }
                        if (ok) ks_keep[i2] |= 1u;
                    }
                }
            }
            {   /* zero the spans this bank's sprites drew two passes ago */
                unsigned bk = bkp, n = r60_rowspan_n[bk], k;
                unsigned ks_carry = 0;
                /* the lists: the bank's own, and under MBAND the master's for the same bank (the
                 * master appends to it only after CLEARED, so the walk and the carry own it here) */
                unsigned li2, nl2 = 1u, full = r60_rowspan_full[bk];
                uint32_t *lst = r60_rowspans[bk];
                LOGOCOUNT(0);
                R60_EVPUTS(8);                   /* clear start (probe) */
                RS(1);
                r60_clr_stats[0]++; r60_clr_stats[1] += n; if (full) r60_clr_stats[3]++;
                if (full) {                      /* overflow: the whole canvas */
                    r60_bank_init();
                    for (k = 0; k < 7u; k++) cleared_rows[k] = 0xFFFFFFFFu;
                    for (k = 0; k < 28u; k++) { cl_x0[k] = 0; cl_x1[k] = 0xFFFFu; }
                }
                else if (r60_fill_rows[bk] || R60_DISPHOLD) r60_clr_exact(bk);   /* the hold fill's planes, a display hold: per span */
                else r60_clr_run(bk);
                (void)li2; (void)nl2; (void)lst; (void)ks_carry;
                if (full) r60_rowspan_n[bk] = 0;   /* the whole canvas went: no span is carried */
                r60_rowspan_full[bk] = 0;
                R60_FENW(8);   /* the last fill lands before anything draws (sw233: bounded) */
                R60_RECTN(bkp) = 0; R60_MRECTN(bkp) = 0;   /* the box lists are not used */
                (void)nl; (void)li;
            }
        }
        R60_EVPUTS(15);                      /* clear done */
        RS(2);
        {
            unsigned i, drew = 0;
            static uint8_t blanked[2];
            unsigned hold = R60_DISPHOLD == 1u;   /* only the game's own blank draws nothing */
            /* A DIRECT PACKET STILL PENDING (sw177): the master built this window's packet in place
             * for the write bank; a flip now would hide it from the 68K (the release-side orphan:
             * demo 2 and the card came back incomplete). Draw nothing and keep the bank one more
             * window, the 68K takes the packet, the next window flips. */
            if (!hold && R60_PKTSEQ != R60_PKTDONE && (R60_PKTA_LEN[R60_PKTSEQ & 1u] & R60_PKTA_DIRECT)) { hold = 1; R60_DIAG2[9] += 0x10000u; }
            r60_ph_t = r60_frt(); R60_PH[5]++;
            {   /* the next offset, decided here so the clear phase and the table agree.
                 * In mode 2 (our hold) the canvas is anchored to the scene's FG scroll:
                 * k = xs_now - anchor + 64 (a 64-px margin for a leftward scroll). */
                unsigned k = 0, kw, sh;
                unsigned mode2 = R60_DISPHOLD == 2u, bank2 = bkp;
                if (mode2 && R60_GATE_SINCE < 5u) mode2 = 0;   /* the plan runs four windows in; a band painted before it skips every unlined set (sw95: empty top rows) */
                if (mode2) {
                    unsigned xs = (0xC0u - ((const volatile uint16_t *)0x26026000u)[0x74Cu]) & 0x3FFu;
                    if (!r60_fill_rows[bank2]) { r60_fill_anchor[bank2] = xs; r60_fill_prevk[bank2] = 64u; }
                    {   int kk = (int)xs - (int)r60_fill_anchor[bank2] + 64;
                        if (kk < 0 || kk > 400) { r60_fill_anchor[bank2] = xs; r60_fill_rows[bank2] = 0; r60_fill_prevk[bank2] = 64u; kk = 64; }   /* re-anchor: repaint */
                        k = (unsigned)kk;
                    }
                } else if (r60_fill_rows[bank2]) {   /* the MD planes are on: the 32X copy leaves this bank */
                    volatile uint32_t *fd = (volatile uint32_t *)FB_DIR(0);
                    unsigned k9;
                    for (k9 = 0; k9 < (FB_W * FB_H + 1024u) / 4u; k9++) fd[k9] = 0;
                    r60_fill_rows[bank2] = 0;
                }
                kw = k >> 1; sh = k & 1u;
                if (sh) { kw++; if ((kw & 31u) == 31u) sh = 0; }   /* bulletin 14 invalidates only the SHIFT bit: clear it, keep the word (steps 2 then 0, max error 1 px) */
                r60_xo_next = sh ? kw * 2u - 1u : kw * 2u; r60_kw_next = kw; r60_sh_next = sh;
                r60_fill_k = k; r60_fill_mode2 = mode2;
            }
            {   /* take the master's text list FIRST, so the clear phase below erases the old glyph
                 * of every cell that changed. Taken after it (sw108) the changed cell was drawn over
                 * its old glyph in the bank that took the list and cleared first in the other: the
                 * two banks alternated at 60 Hz (temporal_metric: 990/847 text pixels, eye span). */
                {   /* a LUT change restyles every glyph: force BOTH banks through the clear phase first
                     * (sw120 redrew over the old glyphs through the overwrite image: bold, doubled letters) */
                    static uint16_t lutseq_top;
                    /* ONLY THE PALS WHOSE LUT ROWS MOVED (R60TXTLUTPAL, 2026-10-24): their cells go pending in both
                     * banks (the clear phase erases the old glyph, the draw restyles it). The force redrew every
                     * glyph in both banks for a one-pal change: the fight's pal-7 slot moves (677/682/701/706) cost
                     * ~80 slave lines in each of two passes. A missed sequence (two changes between passes) forces. */
                    uint32_t pm = R60_TXTLUTPM;
                    uint16_t ls = (uint16_t)(pm >> 16);
                    if (ls != lutseq_top) {
                        unsigned m = (ls == (uint16_t)(lutseq_top + 1u) && txt_init) ? (pm & 0xFFu) : 0x100u;
                        lutseq_top = ls;
                        if (m & 0x100u) { txt_force[0] = 1; txt_force[1] = 1; }
                        else r60_txt_palmark(m);
                        txt_ggen++; if (!txt_ggen) txt_ggen = 1;   /* the glyph cache is keyed on the LUT generation */
                    }
                }
                uint16_t sq = R60_TXTSEQ;
                if (sq != R60_TXTTAKEN) {
                    volatile uint16_t *lst = R60_TXTLIST(sq);
                    unsigned n = lst[0], k;
                    if (!txt_init) { for (k = 0; k < 28u * 40u; k++) { txt_copy[k] = 0; txt_pend[0][k] = 0; txt_pend[1][k] = 0; } txt_init = 1; }
                    for (k = 0; k < n; k++) {
                        unsigned ci = lst[1 + k * 2], idx = ((ci >> 8) & 31u) * 40u + (ci & 63u);
                        uint16_t d = lst[2 + k * 2];
                        if (idx < 28u * 40u) {
                            unsigned r3 = idx / 40u; uint16_t od = txt_copy[idx];
                            unsigned on = ((od & 0x1FFu) || (od & 0x0E00u)) ? 1u : 0u, nn = ((d & 0x1FFu) || (d & 0x0E00u)) ? 1u : 0u;
                            if (on && !nn) { if (txt_rowcnt[r3]) txt_rowcnt[r3]--; } else if (!on && nn) txt_rowcnt[r3]++;
                            if (!txt_pend[0][idx]) txt_rowpend[0][r3]++;
                            if (!txt_pend[1][idx]) txt_rowpend[1][r3]++;
                            txt_copy[idx] = d; txt_pend[0][idx] = 1; txt_pend[1][idx] = 1;
                        }
                    }
                    R60_TXTTAKEN = sq;
                }
            }
            LOGOCOUNT(2);
            {   /* PHASE 3 -- CLEAR STALE TEXT BEFORE SPRITES. Every glyph that will be
                 * redrawn (changed cell, or the canvas moved under the bank) is erased at the
                 * offset it was drawn with. Clearing inside the text draw, after the sprites,
                 * erased the sprites too: every cell's box covers the screen (xo6: 134k sprite
                 * pixels written a frame, 967 bytes left in the bank). */
                unsigned bank = bkp, idx;
                uint8_t *pend = txt_pend[bank];
                {   /* THE TEXT LAYER HEALS ITSELF (sw261). A cell is redrawn only when its glyph changes,
                     * so a glyph erased in one bank by anything else stays erased until the game rewrites
                     * that cell: after START, bank 1 lacked the score row and "CRE" of CREDITS from 1100 to
                     * ~2000 while bank 0 had them (FB reconstruction, frame 1500), and every other presented
                     * frame showed the stale bank. Every 16 passes each bank redraws its non-blank cells
                     * (~50 glyphs, into the hidden bank: nothing visible changes when it was already right). */
                    static uint8_t heal[2];
                    (void)heal;
                    if (txt_init && R60_DISPHOLD != 1u) r60_txt_heal(bank);   /* where the forced heal was: after the keep decisions */
                }
                unsigned force = txt_force[bank] || (txt_init && txt_xo[bank] != r60_xo_next);
                if (txt_init && R60_DISPHOLD != 1u) {
                    for (idx = 0; idx < 28u * 40u; idx++) {
                        uint16_t d = txt_copy[idx];
                        if (!(force || pend[idx])) continue;
                        if (force && !pend[idx] && !((d & 0x1FFu) || (d & 0x0E00u))) continue;   /* a forced redraw skips cells that are blank now; a PENDING blank cell still loses its old glyph (sw146: "2 CINSERT COINAY" -- the ends of the longer string were pending blanks skipped under force, in both banks, for good) */
                        if (force) pend[idx] = 1;
                        if (force) txt_allpend[bank] = 1;
                        (void)d;
                        {
                            unsigned y0 = (idx / 40u) * 8u, col = idx % 40u, y3;
                            volatile uint16_t *cd = (volatile uint16_t *)FB_DIRB(y0 * FB_W + col * 8u + txt_xo[bank]);
                            for (y3 = 0; y3 < 8u; y3++) { cd[0] = 0; cd[1] = 0; cd[2] = 0; cd[3] = 0; cd += FB_W / 2u; }
                        }
                    }
                    txt_force[bank] = 0;
                }
            }
            { uint16_t _n = r60_frt(); R60_PH[0] += (uint16_t)(_n - r60_ph_t); r60_ph_t = _n; }
            {   /* THE OFFSET FOR THIS PASS: the draw bank's line table, the shift bit for the
                 * ISR, and the writers' offset (bulletin 14 guarded above). */
                unsigned i, kw = r60_kw_next;
                r60_xo = r60_xo_next;
                *(volatile uint32_t *)0x26028FFCu = (r60_xo << 16) | kw;   /* probe readout */
                {   volatile uint16_t *lt = (volatile uint16_t *)FB_WBASE;
                    for (i = 0; i < FB_H; i++) lt[i] = (uint16_t)(i * (FB_W / 2u) + 0x100u + kw);
                    for (i = FB_H; i < 256u; i++) lt[i] = (uint16_t)((FB_H - 1u) * (FB_W / 2u) + 0x100u + kw);
                }
                R60_SHIFTNEXT = (uint16_t)r60_sh_next;
            }
            { uint16_t _n = r60_frt(); R60_PH[1] += (uint16_t)(_n - r60_ph_t); r60_ph_t = _n; }
            if (r60_fill_mode2) {   /* STEP B: the 32X paints the planes under this hold */
                unsigned bank2 = bkp, k = r60_fill_k, pk = r60_fill_prevk[bank2], rows = r60_fill_rows[bank2];
                const volatile uint16_t *palblk = R60_PKTSTAGE(R60_PKTSEQ, 1) + 688;
                uint32_t _v0 = R60_DIAG[0];
                (void)palblk;                        /* the master's commit carries the fill sets' colours under mode 2 */
                uint8_t *tp = txt_pend[bank2];
                if (rows < 29u) {                    /* a band of three rows a pass, top down (seven read 1.3 vints a pass, sw89) */
                    unsigned r1 = rows + 3u > 29u ? 29u : rows + 3u, c9, r9;
                    r60_fill_cells(FB_DIRB(0), FB_OVR(0), r60_fill_anchor[bank2], k, k + FB_W, rows, r1);
                    for (r9 = rows; r9 < r1 + 1u && r9 < 28u; r9++) for (c9 = 0; c9 < 40u; c9++) tp[r9 * 40u + c9] = 1;   /* glyphs under the band */
                    r60_fill_rows[bank2] = r1; rows = r1;
                }
                if (k != pk) {                       /* the column that scrolled in: its text cells again */
                    unsigned c9, r9, cl = (k > pk) ? (FB_W - (k - pk)) / 8u : 0u, ch = (k > pk) ? 40u : ((pk - k) + 7u) / 8u + 1u;
                    if (k > pk) r60_fill_cells(FB_DIRB(0), FB_OVR(0), r60_fill_anchor[bank2], pk + FB_W, k + FB_W, 0, rows);
                    else        r60_fill_cells(FB_DIRB(0), FB_OVR(0), r60_fill_anchor[bank2], k, pk, 0, rows);
                    if (ch > 40u) ch = 40u;
                    for (r9 = 0; r9 < 28u; r9++) for (c9 = cl; c9 < ch; c9++) tp[r9 * 40u + c9] = 1;
                }
                r60_fill_prevk[bank2] = k;
                R60_GATE_FILLV += (R60_DIAG[0] - _v0) * 0x10000u + 1u;
            }
            if (hold) {
                if (!blanked[bkp]) {         /* clear this bank once per hold: no sprite, no text */
                    r60_bank_init();
                    blanked[bkp] = 1;
                    ks_n[bkp] = 0;           /* sw201: a wiped bank keeps no sprite */
                    r60_rowspan_n[bkp] = 0; r60_rowspan_full[bkp] = 0;   /* and has no spans left to clear */
                }
                /* the shown bank is already black: stop flipping, the write bank holds still and
                 * the master builds its hold packets straight into it */
                if (blanked[(bkp ^ 1u)]) R60_HOLDFLIP = 1;
            } else {
                blanked[bkp] = 0;
                R60_HOLDFLIP = 0;
            }
            { uint16_t _n = r60_frt(); R60_PH[2] += (uint16_t)(_n - r60_ph_t); r60_ph_t = _n; }
            RS(3);
            if (hold == 0) r60_palmap_assign();
            /* PHASE 4 -- DRAW SPRITES AND RESTORE FOREGROUND PRIORITY.
             * Priority repair belongs to each sprite below, not to a later pass. */
            for (i = 0; hold == 0 && i < 64; i++) {
                RS(4);                                   /* RIGSPLIT: a stamp a sprite (the wrap) */
                const volatile uint16_t *e = R60_SNAPSLOT(R60_DRAWSEQ) + i * 8;
                uint16_t d2 = e[2];
                if (d2 & 0x8000) break;                  /* terminator */
                { unsigned k4; for (k4 = 0; k4 < 8u; k4++) ks_rec[bkp][i][k4] = e[k4]; }
                if ((r60_mc_live_active || r60_mc_live_skip_count ||
                     r60_mc_record_key(e) == SPRITE_WORKSET_MACRO_TARGET_KEY) &&
                    r60_mc_draw_live(bkp, i, e, r60_ws_active)) {
                    drew++; continue;
                }
                if (R60_KS_KEEP_DRAW(i)) {               /* its pixels are already in this bank */
                    if (ks_repunch[i]) {
                        unsigned by0 = ks_pyr[bkp][i] >> 8, by1 = ks_pyr[bkp][i] & 0xFFu, span0b = r60_rowspan_n[bkp];
                        uint16_t sv[4]; unsigned k6;
                        for (k6 = 0; k6 < 4u; k6++) sv[k6] = r60_last_box[k6];
                        ks_cur = i;
                        PHT_B(); R60_SPRITE_S(e, by0, by1);   /* the band only: same pixels, the holes refilled */ PHT_E(0);
                        { PHT_B(); if (!r60_rowspan_full[bkp]) R60_PUNCH_SPANS(span0b, r60_rowspan_n[bkp]);
                        else if (r60_last_box[1] > r60_last_box[0]) R60_PUNCH(r60_last_box); PHT_E(1); }
                        if (prio_ymax > prio_ymin) ks_pyr[bkp][i] = (uint16_t)((prio_ymin << 8) | prio_ymax);
                        r60_text_touched(r60_last_box);
                        for (k6 = 0; k6 < 4u; k6++) r60_last_box[k6] = sv[k6];
                        r60_prio_stats[0]++;
                    }
                    ks_stats[0]++; drew++; continue;
                }
                ks_cur = i;
                if (d2 & 0x4000) {
                    ks_box[bkp][i][0] = ks_box[bkp][i][1] = ks_box[bkp][i][2] = ks_box[bkp][i][3] = 0;
                    continue;               /* skipped */
                }
                { unsigned span0 = r60_rowspan_n[bkp];
                PHT_B();
                {   /* a baked frame through the copy, else the plotter; FRT ticks and pixels per path in r60_bk_stat[5..7] */
                    uint16_t t0_ = r60_frt();
                    unsigned rp0_ = r60_rowpix;
                    /* An unavailable scaled atlas cannot draw this record.
                     * Keep unscaled AB1/SPREP and the common raw path intact. */
                    unsigned raw_only = !r60_ws_active && (e[5] & 0x3FFu);
                    if (raw_only) R60_ZB_STAT(0,1u);
                    if (raw_only || !r60_bk_sprite(e, bkp, i)) { R60_SPRITE_S(e, 0u, FB_H); r60_bk_stat[6] += (uint16_t)(r60_frt() - t0_);
                        r60_bk_stat[7] += r60_rowpix - rp0_;
                    }
                    else r60_bk_stat[5] += (uint16_t)(r60_frt() - t0_);
                }
                PHT_E(0);
                /* the box punch for a full draw (one cell lookup per cell: sw162's span punch here
                 * looked every cell up once a row and cost 6x the cells); the span punch serves
                 * the band re-punch of a kept sprite, where the band is a few rows */
                (void)span0;
                PHT_B(); ks_punched[bkp][i] = (uint8_t)(r60_last_box[1] > r60_last_box[0] && R60_PUNCH(r60_last_box)); PHT_E(1);
                ks_pyr[bkp][i] = ks_punched[bkp][i] && prio_ymax > prio_ymin ? (uint16_t)((prio_ymin << 8) | prio_ymax) : 0u;
                }
                { unsigned k4; for (k4 = 0; k4 < 4u; k4++) ks_box[bkp][i][k4] = r60_last_box[k4]; ks_stats[1]++; }
                r60_text_touched(r60_last_box);
                drew++;
            }
            ks_n[bkp] = (uint8_t)(hold ? 0u : i);   /* under the hold nothing was drawn */
            ks_fgkey[bkp] = (prio_bank1 << 26) | (prio_xs << 16) | prio_ys; ks_fgpsel[bkp] = (uint16_t)prio_psel; ks_fggen[bkp] = R60_FGGEN;
            if (hold) { unsigned k5; for (k5 = 0; k5 < 64u; k5++) ks_box[bkp][k5][1] = 0; }
            R60_DIAG[3] += drew;
        }
        { uint16_t _n = r60_frt(); R60_PH[3] += (uint16_t)(_n - r60_ph_t); r60_ph_t = _n; }
        RS(4);
        R60_EVPUTS(5); MBT(go, 5);           /* sprites done (code 5, 'APLY' slot reused) */
        {   /* PHASE 5 -- DRAW TEXT IN FRONT OF THE COMPLETED SPRITE LAYER.
             * The master's change list is applied to
             * the slave's cached copy and marks the cell pending in BOTH banks; a cell is
             * drawn into this bank when pending here, when its rows were cleared this pass
             * (a sprite crossed them), or when its pal's colour group moved. */
            static uint16_t txt_grp_seen[8];
            uint8_t  pal_moved[8];
            unsigned moved_any = 0;
            uint8_t *pend = txt_pend[bkp];
            uint16_t t_txt = r60_frt();
            uint32_t t_draw = 0;
            unsigned row, col;
            if (!txt_init) { unsigned k; for (k = 0; k < 28u * 40u; k++) { txt_copy[k] = 0; txt_pend[0][k] = 0; txt_pend[1][k] = 0; } txt_init = 1; }
            /* the master's list is taken before the clear phase (top of the pass) */
            LOGOCOUNT(3);
            {   /* a pal whose group moved: only ITS cells are stale */
                unsigned k2, moved = 0;
                for (k2 = 0; k2 < 8u; k2++) { pal_moved[k2] = txt_grp_seen[k2] != R60_TXTGRP[k2]; if (pal_moved[k2]) { moved = 1; txt_grp_seen[k2] = R60_TXTGRP[k2]; } }
                if (moved) R60_DIAG2[12]++;
                moved_any = moved;
            }
            {   /* held: nothing is drawn; the clear phase and pend marks handle the rest */
                if (R60_DISPHOLD == 1u) { txt_force[bkp] = 1; goto txt_done; }
                /* mode 2: the fill marks the cells it painted under as pending */
            }
            (void)moved_any;
            for (row = 0; row < 28u; row++) {
                unsigned y0 = row * 8u, y2, cleared = 0;
                {   unsigned bk = bkp;
                    if (!txt_rowcnt[row]) {          /* nothing to draw on this row: just retire its pending marks */
                        if (txt_rowpend[bk][row] || txt_allpend[bk]) { for (col = 0; col < 40u; col++) pend[row * 40u + col] = 0; txt_rowpend[bk][row] = 0; }
                        continue;
                    }
                }
                for (y2 = 0; y2 < 8u; y2++)
                    if ((cleared_rows[(y0 + y2) >> 5] >> ((y0 + y2) & 31u)) & 1u) { cleared = 1; break; }
                if (!cleared && !moved_any && !txt_rowpend[bkp][row] && !txt_allpend[bkp]) continue;
                for (col = 0; col < 40u; col++) {
                    unsigned idx = row * 40u + col;
                    uint16_t d = txt_copy[idx];
                    unsigned code = d & 0x1FFu, g, base, y, draw;
                    const uint8_t *tp;
                    volatile uint8_t *dst;
                    {   unsigned cx = col * 8u + r60_xo;   /* a cleared row redraws only the cells the clear touched */
                        unsigned hit = cleared && (cx + 8u > cl_x0[row]) && (cx < cl_x1[row]);
                        draw = pend[idx] || hit || pal_moved[((unsigned)d >> 9) & 7u];
                    }
                    if (!draw) continue;
                    if (pend[idx]) {
                        pend[idx] = 0;   /* its old glyph was cleared in the clear phase */
                        if (txt_rowpend[bkp][row]) txt_rowpend[bkp][row]--;
                    }
                    if (code == 0u && !(d & 0x0E00u)) continue;
                    g = R60_TXTGRP[((unsigned)d >> 9) & 7u];
                    if (g == 0xFFu) continue;
                    base = R60_TXTBASE + g * 8u;
                    tp = altbeast_tiles + code * 64u;
                    dst = FB_OVR(y0 * FB_W + col * 8u + r60_xo);
                    R60_DIAG2[2]++;
                    { uint16_t _td = r60_frt();
                    uint32_t L[8];
                    uint32_t *gc; 
                    {   unsigned pl4 = ((unsigned)d >> 9) & 7u, h = (code * 8u + pl4) & 63u;
                        uint32_t key = ((uint32_t)txt_ggen << 16) | (pl4 << 9) | code;
                        gc = txt_gc[h];
                        if (txt_gtag[h] != key) {    /* convert once, keep */
                            const volatile uint16_t *lut = R60_TXTLUT + g * 8u; unsigned k3; const uint8_t *tq = tp;
                            for (k3 = 0; k3 < 8u; k3++) L[k3] = lut[k3];
                            for (k3 = 0; k3 < 8u; k3++) {
                                gc[k3 * 2u]      = (L[tq[0]] << 24) | (L[tq[1]] << 16) | (L[tq[2]] << 8) | L[tq[3]];
                                gc[k3 * 2u + 1u] = (L[tq[4]] << 24) | (L[tq[5]] << 16) | (L[tq[6]] << 8) | L[tq[7]];
                                tq += 8;
                            }
                            txt_gtag[h] = key;
                        }
                    }
                    (void)base;
                    for (y = 0; y < 8u; y++) {
                        uint32_t a0 = gc[y * 2u], a1 = gc[y * 2u + 1u];
                        if (r60_xo & 2u) {           /* word-aligned only: four word stores */
                            ((volatile uint16_t *)dst)[0] = (uint16_t)(a0 >> 16); ((volatile uint16_t *)dst)[1] = (uint16_t)a0;
                            ((volatile uint16_t *)dst)[2] = (uint16_t)(a1 >> 16); ((volatile uint16_t *)dst)[3] = (uint16_t)a1;
                        } else {
                        ((volatile uint32_t *)dst)[0] = a0;
                        ((volatile uint32_t *)dst)[1] = a1;
                        }
                        tp += 8; dst += FB_W;
                    }
                    t_draw += (uint16_t)(r60_frt() - _td); }
                }
            }
            txt_allpend[bkp] = 0;            /* the forced pass is done (the hold path jumps past this and keeps it) */
        txt_done:
            LOGOCOUNT(4);
            if (R60_DISPHOLD != 1u) txt_xo[bkp] = r60_xo;   /* every glyph now sits at this offset */
            R60_DIAG[12] += (uint32_t)(uint16_t)(r60_frt() - t_txt);
            R60_DIAG[13] += t_draw;
        }
        { uint16_t _n = r60_frt(); R60_PH[4] += (uint16_t)(_n - r60_ph_t); r60_ph_t = _n; }
        RS(6);
        R60_EVPUTS(6); MBT(go, 7);           /* text done (code 6) */
/* R60_LTPROBE superseded by the canvas offset above (R60_XOFFPROBE) */
        RS(7);
        R60_DIAG[2]++;
        /* WHAT THE PASS COST AND WHAT IT PUT DOWN. Draw time in slave FRT ticks (184 a
         * line) and the row-pixels actually stored, so the two can be read against each
         * other: a pass that costs more for the same pixels is the renderer getting
         * slower, one that costs more for more pixels is the scene. */
        R60_DIAG2[8] += (uint32_t)(uint16_t)(r60_frt() - t0);
        R60_DIAG2[10] += r60_rowpix;
        {   /* THE WINDOW, HERE, THE MOMENT THE PASS ENDS: slave idle by definition, FM up,
             * and the master is not in the VDP region at all (it only ticks). */
            uint16_t fs_o;
            R60_EVPUTS(10);
            /* PHASE 6 -- PUBLISH PLANES/CRAM, PRESENT, THEN LEASE THE VDP
             * window to the 68K. R60_DONE is posted only after this section. */
            if (R60_HOLDFLIP && R60_DISPHOLD == 1u) {   /* the master builds the hold packet in place while we hold FM: wait for a packet (bounded), then
                                                          * for the build to end (it checks our flag before it starts, so one of us always yields) */
                uint16_t _t = r60_frt();
                while (R60_PKTSEQ == R60_PKTDONE && (uint16_t)(r60_frt() - _t) < 60000u) R60_WAITBODY;
                R60_FMSOON = 1;
                {   /* a 14-row direct build with ~360 new cells runs ~2 frames (sw177 f1492-1493); the 60000-tick
                     * (1.24-frame) wait ran out under it, FM fell, every store after was dropped, and the shadow kept
                     * rows 17-30 the name table never got. Nothing else needs the span under the hold: wait it out. */
                    unsigned n = 0;
                    for (;;) { _t = r60_frt(); while (R60_BUILDING && (uint16_t)(r60_frt() - _t) < 60000u) R60_WAITBODY; if (!R60_BUILDING || ++n >= 8u) break; }
                    if (R60_BUILDING) R60_TORN_S++;
                }
            } else R60_FMSOON = 1;
            if (R60_DISPHOLD == 1u && !R60_HOLDFLIP) {
                /* THE HOLD'S FIRST WINDOW (sw176): this pass flips to the bank it just blanked, and a
                 * packet published now lands in the bank the flip then hides: never consumed, its
                 * rows already in the shadow -- demo 2 came back with ~700 cells of the eye (sw174).
                 * Publish next window, when the flips have stopped and the write bank holds still. */
                R60_DIAG2[9]++;
            } else
            { r60_publish_planes(); R60_PUB = go; }  /* FB packet, at FM=1 */
            R60_EVPUTS(2); MBT(go, 8); MBTV(go, 23, R60_DIAG[0]);   /* publish done (code 2) */
            RS(8);
            r60_cram_commit(); R60_DIAG2[3]++;       /* PEN-waited per bank */
            R60_EVPUTS(11);
            RS(9);
            fs_o = MARS_VDP_FBCTL & MARS_VDP_FS;
            if ((R60_HOLDFLIP && R60_DISPHOLD == 1u) || (R60_PKTA_LEN[R60_PKTDONE & 1u] & R60_PKTA_DIRECT)) { R60_SHOWN = go; }   /* no flip: under the hold with the shown bank black, or a direct packet just published for this bank */
            else
            {
            MARS_VDP_FBCTL = (uint16_t)(fs_o ^ 1);   /* deferred to the next vblank */
            *(volatile uint16_t *)0x20004102u = R60_SHIFTNEXT;   /* the odd pixel of the canvas offset, with the flip */
            r60_fs_want = (uint16_t)(fs_o ^ 1);
            R60_FPEND = 1; R60_SHOWN = go; R60_DIAG[1]++;
            r60_bkp ^= 1u;   /* sw226: the physical bank advances only with a flip */
            }
            R60_SEEN = MARS_SYS_COMM0;
            R60_SPANV = (uint16_t)R60_DIAG[0];
            R60_FMDOWN = 1;
            r60_span_open(&r60_span_lease, MARS_SYS_COMM4, R60_SPANV);
            R60_FM_DOWN();                           /* the 68K's span opens NOW */
            R60_EVPUTS(13);
        }
        RS(10);
        R60_DONE = go;
    }
}

__attribute__((noinline)) static int r60_frontend_service(void)
{
    volatile uint8_t *w = (volatile uint8_t*)0x2602CC00u;
    uint16_t command = MARS_SYS_COMM12;
    if (command == FE_REQUEST && MARS_SYS_COMM6 == FE_SLAVE_IDLE) {
        extern void port_frontend_settings(unsigned, unsigned, unsigned);
        frontend_paused = 1;
        R60_FM_DOWN();
        MARS_SYS_COMM12 = FE_GRANTED;
        while (MARS_SYS_COMM12 == FE_GRANTED) {
            unsigned delay;
            for (delay = 0; delay < 256u; delay++) __asm__ volatile ("nop");
        }
        command = MARS_SYS_COMM12;
        if ((command & 0xFF00u) == FE_RESULT) {
            /* THE REST OF THE RESULT, one word at a time (md_src/frontend.h) */
            unsigned dsw1, dsw2;
            MARS_SYS_COMM12 = FE_MORE1;
            while ((MARS_SYS_COMM12 & 0xFF00u) != FE_DSW1) __asm__ volatile ("nop");
            dsw1 = MARS_SYS_COMM12 & 0xFFu;
            MARS_SYS_COMM12 = FE_MORE2;
            while ((MARS_SYS_COMM12 & 0xFF00u) != FE_DSW2) __asm__ volatile ("nop");
            dsw2 = MARS_SYS_COMM12 & 0xFFu;
            port_frontend_settings(command & 0xFFu, dsw1, dsw2);
        }
        pp_due = 0;
        R60_FM_UP();
        r60_cram_stale = 1;              /* the MD owned CRAM through the pause */
        frontend_paused = 0;
        MARS_SYS_COMM12 = 0;
    } else if (!FE_WAITING(command)) {
        /* The arcade sequencer's title step is 8; never intercept gameplay. */
        MARS_SYS_COMM6 = (!(w[0x3028] & 3u) && w[0x3031] == 8u && (w[0x3018] & 0x20u))
                       ? FE_TITLE : 0xB600u;
    }
    return FE_WAITING(MARS_SYS_COMM12);
}

#ifdef LAUNCHER
/* THE LAUNCHER (r60_launcher.c), from m_main before the first game tick. Cart-resident and
 * not inlined: m_main lives in .ramtext, which has no room for it. */
static __attribute__((noinline)) void r60_launcher_run(void)
{
    /* The launcher draws on the 32X layer with its own line tables and palette, so the boot's
     * framebuffer state is made again after it, the palette is marked for a full resend, and
     * the vblanks it took are not owed to the game. */
    extern unsigned launcher_boot(void);
    extern void port_launch_region(unsigned region);
    extern void port_frontend_settings(unsigned, unsigned, unsigned);
    unsigned choice;
    uint16_t fs_in = MARS_VDP_FBCTL & MARS_VDP_FS;   /* the renderer tracks the write bank from here, it never reads it back */
    uint8_t fm_in = R60_FM_REG & 0x80u;              /* FM as the boot left it */
    choice = launcher_boot();
    if (choice != 0xFFFFFFFFu) {         /* the game has not booted: the region and both switch banks still decide it */
        port_launch_region(choice & 3u);
        if (choice & 0x80u)              /* the switch sheet was used for this region */
            port_frontend_settings(((choice >> 8) & 7u) | ((choice & 3u) << 3), (choice >> 16) & 0xFFu, choice >> 24);
    }
    R60_FM_UP();
    r60_boot_banks();
    MARS_VDP_DISPMODE = MARS_NTSC_FORMAT | MARS_224_LINES | MARS_VDP_PRIO_32X | MARS_VDP_MODE_256;
    /* THE MODE IS WRITTEN AND LATCHED UNDER FM, IN HBLANK (r60_boot_banks). The first version
     * restored FM before this write: the 32X layer stayed off for the rest of the run with its
     * framebuffer and palette correct (launcher02, 2026-11-13). See one whole HBlank go by. */
    while (MARS_VDP_FBCTL & MARS_VDP_HBLK) ;
    while (!(MARS_VDP_FBCTL & MARS_VDP_HBLK)) ;
    while (MARS_VDP_FBCTL & MARS_VDP_HBLK) ;
    if ((MARS_VDP_FBCTL & MARS_VDP_FS) != fs_in) {
        MARS_VDP_FBCTL = fs_in;
        while ((MARS_VDP_FBCTL & MARS_VDP_FS) != fs_in) ;
    }
    R60_FM_REG = fm_in;
    R60_CRAM_RESEND();
    pp_due = 0;
}
#endif

R60CODE void m_main(void)
{
    r60_comm_init(R60_COMM,8u);R60_COMM_IDLE_MAP=0u;
    MARS_SYS_COMM4 = 0;                  /* release the slave */
#ifdef TC_PADTEST
    /* Controller-only diagnostic: acknowledge the existing MD boot, then stay
     * quiet. The slave waits for B007, which the MD diagnostic never posts.
     * The MD owns the screen and ports; no game or renderer runs behind it.
     * Do not enable the legacy TEST_CART SH-2 dispatcher on the R60 boot path. */
    __asm__ volatile ("ldc %0,sr" : : "r" (0xF0u) : "memory");
    MARS_SYS_COMM14 = 0x600D;
    for (;;) __asm__ volatile ("nop");
#endif
    r60_boot_banks();
    /* THE 32X LAYER MUST BE PUT IN FRONT, AND THIS IS THE LINE P1 LOST.
     *
     * MARS_VDP_DISPMODE bit 7 is the layer order (mars.h:80-81: MARS_VDP_PRIO_68K
     * 0x0000, MARS_VDP_PRIO_32X 0x0080). Hw32xInit writes MARS_224_LINES |
     * MARS_VDP_MODE_256 with NO priority bit (mars.c:40), and the old renderer set the
     * full value at its own init (m_main.c:8708 and :13820) -- so deleting the renderer
     * deleted the only place it was ever written, and the 32X layer has been BEHIND the
     * Mega Drive layer ever since. The sprites were in the framebuffer the whole time
     * and the display was covering them with whatever the MD planes hold, which is
     * garbage on the board and mostly opaque on ares: a constant partial density with
     * correct shapes and positions, immune to pacing, write width, access overlap and
     * everything else tried against it for a night.
     *
     * IT HID BECAUSE EVERY ares CHECK WAS A DRAM DUMP. A dump reads the framebuffer and
     * bypasses the display mix entirely, so ares agreed with itself and disagreed with
     * the board. THE PICTURE ON ares IS `--screenshot`, NEVER A DUMP; dumps count
     * pixels, the screenshot is what the board shows.
     *
     * Pen 0 still falls through by the overwrite rule, so the MD planes will show where
     * no sprite was drawn once they exist -- which is the composite the design wants. */
    MARS_VDP_DISPMODE = MARS_NTSC_FORMAT | MARS_224_LINES
                      | MARS_VDP_PRIO_32X | MARS_VDP_MODE_256;
    R60_GO = 0; R60_DONE = 0;
    /* THE BOOT CHAIN, and leaving it out cost a build: the master posts 0x600D, the 68K
     * shim waits for it at md_main.c:7326 before finishing its init, then posts 0xB007,
     * which releases BOTH the slave's warm-up stub (mars_start.s:621, SILICON FACT 1) and
     * s_main's own wait. Without 0x600D the first skeleton ran 687 vints with the master
     * and the game alive, COMM14 reading 0x0000, and the slave never entered its loop.
     * The warm-up itself needs no carrying -- it lives in mars_start.s, not in the
     * renderer, so deleting the renderer did not touch it. */
    MARS_SYS_COMM14 = 0x600D;
    /* THE MASTER OWNS FM FROM HERE. md_main.c:7380 drops FM at the end of boot ("68K
     * owns the FB window during gameplay") and the OLD shim raised it again every vint
     * from the 68K side; the new shim never writes FM at all, by design, so if the
     * master does not raise it nothing does. Measured when it did not: FM stayed 0 for
     * the whole run, every SH-2 framebuffer write was dropped, and the rom drew 0 px. */
    R60_FM_UP();                         /* FM = 1, the steady state */
#ifdef R60_RIGSPLIT
    { unsigned k; for (k = 16; k < 25u; k++) RSX[k] = 0; RSX[24] = r60_frt(); }
#endif
    R60_SHOWN = 0; R60_PUB = 0; R60_FMDOWN = 0;
    r60_planes_reset();
#ifdef LAUNCHER
    r60_launcher_run();                  /* before the first game tick */
#endif
    for (;;) {
#ifdef R60_RIGSPLIT
        RM(0); RSX[23]++;
#endif
        if (r60_frontend_service()) pp_due = 0;
        /* Publish completed ACKs before native execution. Native capture only
         * enqueues; latest-phase mapping/audio policy runs after each tick and
         * before its observer. Idle retries may post mappings, never audio. */
        r60_ws_pre_ack();
        /* A paused frontend may leave no native tick to refresh this policy. */
        unsigned r60_page_hold=R60_PAGE_PENDING || R60_COMM->wanted!=R60_PAGE_CURRENT;
        while (pp_due) { pp_due--; port_play_tick(); R60_DIAG[11]++; RM(1);
            r60_page_hold=r60_page_service(1u);
        }   /* RIGSPLIT: a stamp a tick (a 16-bit FRT delta wraps at 356 lines) */
        {   /* PUBLISH THE SPRITE TABLE after the tick, into the slot the slave is not
             * reading. One tick per vint and a pass under a vint means the other slot is
             * always free; if the slave is still on it, skip this vint and count it. */
            uint16_t seq = R60_SNAPSEQ, nxt = (uint16_t)(seq + 1u);
            if (r60_page_hold) { /* retain the latest completed-tick policy decision */ }
            else if (
                r60_snap_publish_allowed(&R60_DONE,&R60_GO,&R60_DRAWSEQ,seq)
            ) {
                const volatile uint32_t *sp = (const volatile uint32_t *)SPR_SNAP;
                volatile uint32_t *dp = (volatile uint32_t *)R60_SNAPSLOT(nxt);
                unsigned i;
                for (i = 0; i < 256u; i++) dp[i] = sp[i];
                R60_SNAPSEQ = nxt; R60_EVPUT(6); MBTG(4);
            } else { R60_DIAG2[9]++; R60_EVPUT(7); }   /* snapshot skipped: slot busy */
        }
        RM(2);
        r60_planes_prepare();
        RM(5);
        MBTS(4); MBT(R60_GO, 19); MBST(6);   /* prepare end, into the pass running now */
        if (!R60_DISPSET && r60_frame > 120u && R60_DONE == R60_GO && !R60_FMDOWN) {
            MARS_VDP_DISPMODE = MARS_NTSC_FORMAT | MARS_224_LINES
                              | MARS_VDP_PRIO_32X | MARS_VDP_MODE_256;
            R60_DISPSET = 1;
        }
        {   /* idle until the next vint, logging the slave's transitions on the master's clock */
            uint16_t ld = R60_DONE, lg = R60_GO, lf = R60_FMDOWN;
            while (!pp_due) {
                /* No idle audio. Avoid cart policy traffic without map intent;
                 * quiescence is rechecked by the reservation before posting. */
                if((R60_PAGE_PENDING && r60_ws_mapper_acked(MARS_SYS_COMM10)) ||
                    (R60_COMM_IDLE_MAP && !R60_PAGE_PENDING &&
                    MARS_SYS_COMM10==0u && R60_DONE==R60_GO && R60_DRAWSEQ==R60_SNAPSEQ))
                    (void)r60_page_service(0u);
                /* UNDER THE HOLD THE WINDOWS RUN ON THE SLAVE'S CLOCK, NOT THE VINT'S (sw190). The
                 * prepare above runs once a vint, so a staged copy that found FM down waited a whole
                 * frame, and a build could not start until the vint after the slave's publish: the
                 * event ring (sw189 f463-471) showed every other slave window timing out its 60000-tick
                 * wait, four frames a window. Here it returns at once when there is nothing to do. */
                /* sw200: DISABLED. With it the demo's logo sprite blinked at 30 Hz after the cut
                 * (one bank kept an eroded copy; temporal flicker 47k against 18k without it,
                 * sw188 vs sw190); the cause in the slave's keep/clear bookkeeping is not found. */
                if (R60_DISPHOLD == 1u && R60_HOLDFLIP) r60_planes_prepare();   /* PROBE sw221: on, to reproduce the logo erosion deterministically */
                /* A STAGED PACKET RETRIES HERE TOO (2026-10-25). It is built under the hold and lands only
                 * when prepare finds FM up; prepare runs once a vint right after the tick, which can sit in
                 * the 68K's span every vint, and once the hold ends this retry was off: pur1's attract
                 * stranded one for ~530 frames (no packet after f2107, the high-score table never drawn). */
                else if (r60_stage_full) r60_planes_prepare();
                if (R60_GO != lg)     { lg = R60_GO;     R60_EVPUT(2); }
                if (R60_DONE != ld)   { ld = R60_DONE;   R60_EVPUT(3); }
                if (R60_FMDOWN != lf) { lf = R60_FMDOWN; R60_EVPUT(lf ? 8 : 4); }
            }
        }
    }
    for (;;) {
        while (pp_due) {                 /* the game's own clock, drained per vint */
            pp_due--;
            port_play_tick();
            R60_DIAG[11]++;
        }
        /* THE HANDSHAKE, three words and no sticky flag. GO is the frame the slave is
         * drawing or has drawn, DONE what it finished, SHOWN what the V-ISR presented. A
         * go starts only once its predecessor is on the screen, so the slave owns its
         * bank for the whole pass and a flip can never land mid-list. */
        {   /* PROBE: what does the master see of FM, and does publishing only while
             * the 68K owns the framebuffer change what it reads? SILICON FACT 2: at
             * FM=1 the SH-2 owns the FB and 68K reads there return nothing usable. */
            /* ONE PUBLISH PER PRESENTED FRAME, AT FM=1 (measured on this rom: SH-2 FB
             * writes land at FM=1, 45064 of 45361, and are DROPPED at FM=0, 11297 of
             * 11513 -- SILICON FACT 2's mirror image). The consumer's ack is that it
             * zeroes the magic (md_main.c:1409), so the state of the previous packet
             * read back here says whether the 68K took it, with no 68K diag cell in
             * the loop. */
            /* PUBLISH BEFORE THE FLIP, AT FM=1 -- the only time the master may write the
             * framebuffer at all. The V-ISR will not drop FM until PUB == GO, so the
             * packet the 68K finds in its span is always this frame's. */
            if (R60_DONE == R60_GO && R60_PUB != R60_GO && R60_FM_IS_UP) {
                if (R60_PKT_A_U[0] == 0xB6B6u) R60_DIAG[12]++; /* previous NOT consumed */
                else                          R60_DIAG[13]++;  /* previous acked */
                r60_publish_planes();
                R60_PUB = R60_GO;
            }
            /* OPEN THE SPAN AS SOON AS THE FRAME IS READY, NOT IN THE V-ISR. Both
             * vblank handlers fire at the same edge and the 68K's gets there first:
             * with the drop inside the master's V-ISR the 68K read FM=1 and returned
             * on 687 of 688 vints (1 consume, 298 backstop closes). Opening it here --
             * the moment the slave is DONE and the packet is published, which is
             * normally before vblank -- means FM is already down whenever the 68K's
             * vint arrives, and the slave cannot be drawing because its next GO is
             * gated on the span being closed. */
            /* ONE SPAN PER FRAME. Without R60_SPANF the open condition stayed true
             * between a close and the next flip, so the span re-opened immediately:
             * 7,664 closes for 295 frames, FM down on 507 of 687 vints, and the slave's
             * writes dropped -- the framebuffer read 0 px with 1,033 sprites "drawn". */
            if (!R60_FMDOWN && R60_SPANF != R60_GO
                && R60_DONE == R60_GO && R60_PUB == R60_GO) {
                {   /* BAR B: READ BACK THE BANK THE SLAVE JUST DREW, here and nowhere
                     * else -- slave idle, FM up, span closed, so it obeys the
                     * no-overlap rule like every other master VDP access. Rows 60-168,
                     * x 100-180, through the DIRECT image as longwords: 8,640 bytes,
                     * about a millisecond. ares reads ~5,500 on the hero close-up. */
                    unsigned row, i, n = 0;
                    for (row = 60; row < 168u; row++) {
                        const volatile uint32_t *q =
                            FB_DIR_U(row * FB_W + 100u);
                        for (i = 0; i < 20u; i++) {
                            uint32_t v = q[i];
                            if (v & 0xFF000000u) n++;
                            if (v & 0x00FF0000u) n++;
                            if (v & 0x0000FF00u) n++;
                            if (v & 0x000000FFu) n++;
                        }
                    }
                    R60_DIAG2[13] = n;
                    n /= 640u; if (n > 8u) n = 8u;
                    R60_WB = (uint16_t)(32u * n);
                }
                r60_cram_commit();           /* slave idle, FM up: the only safe moment */
                R60_DIAG2[3]++;
                /* FS FIRST, WHILE FM IS STILL UP, AND NOTHING BETWEEN IT AND THE DROP.
                 * The publish is already out (PUB == GO), so the frame is complete and
                 * the bank is safe to present. */
                uint16_t fs_o = MARS_VDP_FBCTL & MARS_VDP_FS;
                MARS_VDP_FBCTL = (uint16_t)(fs_o ^ 1);
                r60_fs_want   = (uint16_t)(fs_o ^ 1);
                R60_TFLIP     = r60_frt();
                R60_FPEND     = 1;
                r60_last_flip = r60_frame;
                R60_SHOWN     = R60_GO;
                R60_DIAG[1]++;                       /* flips REQUESTED */
                R60_SEEN   = MARS_SYS_COMM0;
                R60_TDROP  = r60_frt();
                R60_EVPUT(3);
                R60_SPANV  = r60_frame;
                R60_FMDOWN = 1;
                R60_FM_DOWN();
            }
            /* CLOSE THE SPAN: on the 68K's DONE post, or at vblank end as the backstop.
             * The backstop is what makes a late or dead 68K unable to hold the frame;
             * DIAG[15] counts it and the count is the number that says how late it is. */
            /* CLEAR THE PENDING FLIP once the hardware has applied it, or after a whole
             * frame -- the bound is what keeps an ignored request from stalling the
             * renderer for ever, and it is counted so the two can be told apart. */
            /* AND POLL IT ONLY WHEN THE SLAVE IS IDLE AND THE SPAN IS CLOSED. This read
             * is a master VDP-region access too, and it ran EVERY main-loop iteration
             * during the draw -- the busiest violator of the no-overlap rule in the
             * file. It also could not work where it was: with the span open the SH-2
             * cannot read the VDP either, so it always fell through to the timer
             * (608 of 695 "gave up"). After the flip fix the apply happens at the
             * vblank the 68K consumes in, so the first poll after the post sees it. */
            if (R60_FPEND && !R60_FMDOWN && R60_DONE == R60_GO) {
                if ((uint16_t)(MARS_VDP_FBCTL & MARS_VDP_FS) == r60_fs_want) {
                    R60_FPEND = 0; R60_DIAG2[11]++;      /* applied */
                    R60_EVPUT(5);
                } else if ((uint16_t)(r60_frt() - R60_TFLIP) > 12000u) {
                    R60_FPEND = 0; R60_DIAG2[12]++;      /* gave up on it */
                }
            }
            /* COMMIT CRAM THE MOMENT BOTH CONDITIONS HOLD: FM up and still inside
             * vblank. If vblank runs out first the commit is dropped for this frame and
             * counted -- the palette is a frame stale, never torn. */
            /* FM=1 IS THE STEADY STATE, AND IT HAS TO BE SELF-HEALING. Raising it once
             * at 0x600D is not enough: md_main.c:7380 drops FM later in its own boot
             * ("68K owns the FB window during gameplay"), after the master has posted,
             * and with the new shim nothing on the 68K side ever raises it again. The
             * first cut raised it only at startup and the rom drew 0 px for the whole
             * run. Outside the vblank span the master simply holds it up. */
            /* AND ASSERT IT LATE, WITH FM UP. The SH-side VDP access is gated on FM
             * in the core (IF.sv:974, `SH_VDP_SEL && ADCR.FM && ...`), and md_main.c
             * drops FM at the END of its own boot (:7380) -- after the master's init
             * has run. So the write at init can be thrown away, and measured it was:
             * the ares SCREENSHOT was byte-identical with and without it. Once, after
             * the 68K boot is long done and FM is ours. */
            if (!R60_DISPSET && r60_frame > 120u && R60_FM_IS_UP
                && R60_DONE == R60_GO) {        /* slave idle: no-overlap rule */
                MARS_VDP_DISPMODE = MARS_NTSC_FORMAT | MARS_224_LINES
                                  | MARS_VDP_PRIO_32X | MARS_VDP_MODE_256;
                R60_DISPSET = 1;
                R60_DIAG2[7] = ((uint32_t)MARS_VDP_DISPMODE << 16) | 0xD15Du;
                                                 /* reads back 0x8081: the write lands */
            }
            if (!R60_FMDOWN && !R60_FM_IS_UP) {
                R60_FM_UP();
                R60_DIAG[10]++;                             /* FM re-raised */
            }
            if (R60_FMDOWN) {
                if (MARS_SYS_COMM0 != R60_SEEN) {
                    R60_FM_UP();
                    R60_FMDOWN = 0;
                    R60_SPANF  = R60_GO;
                    R60_DIAG[14]++;                         /* closed on the post */
                    R60_EVPUT(4);
                } else if ((uint16_t)(r60_frame - R60_SPANV) >= 2u) {
                    R60_FM_UP();
                    R60_FMDOWN = 0;
                    R60_SPANF  = R60_GO;
                    R60_DIAG[15]++;                         /* closed on the BACKSTOP */
                }
            }
        }
        /* AND THE FLIP MUST HAVE ACTUALLY TAKEN. Writing FBCTL only REQUESTS the
         * bank change; FS reads back changed when the hardware has applied it, which
         * is why Hw32xInit spins on exactly this (mars.c:45). The V-ISR does not wait
         * -- it must not, it is the vblank handler -- so the wait belongs here, before
         * the slave is allowed to start: otherwise its first writes go into the bank
         * that is still on screen and the rest into the new one, and each bank ends up
         * holding part of a frame. On the board that is what the picture looked like,
         * and it got better the fewer writes a pass made: with the full-screen clear
         * removed (122.7 lines a pass instead of 212.1) the board went from 1 row lit
         * to 117-131, with the stelae and a figure recognisable. */
        /* CENSUS, NOT A GATE. Gating the next go on the flip having taken
         * (`(MARS_VDP_FBCTL & MARS_VDP_FS) == r60_fs_want`, which is the condition
         * Hw32xInit spins on at mars.c:45) DEADLOCKED after one flip: the read-back
         * never matched, so GO never advanced and nothing ever flipped again. Count it
         * instead and see what FS actually does before building on it. */
        /* THE FLIP MUST HAVE TAKEN BEFORE THE SLAVE STARTS. Writing FBCTL only
         * REQUESTS the bank change; FS reads back changed when the hardware applies it,
         * which is what Hw32xInit spins on (mars.c:45). CENSUSED FIRST: FS matched the
         * requested value on 231 of 271 samples and lagged on 40, so the slave was
         * starting its pass into the bank still on screen about one flip in seven, and
         * its first writes went to the wrong one. BOUNDED, because gating on the match
         * alone DEADLOCKED at one flip -- a wait with no way out is not a gate. */
        if (!R60_FMDOWN && R60_DONE == R60_GO && R60_GO == R60_SHOWN
            && R60_PUB == R60_GO && R60_SPANF == R60_GO
            && !R60_FPEND) {
            /* THE FLIP MUST HAVE TAKEN BEFORE THE SLAVE STARTS. Writing FBCTL only
             * REQUESTS the bank change; FS reads back changed when the hardware applies
             * it (what Hw32xInit spins on, mars.c:45), and censused it LAGS on 40 of
             * 271 flips -- so without this a pass in seven starts against the bank
             * still on screen, its first writes land there and the rest in the new one.
             * Measured as that split: one bank holding the sprites and the other the
             * bars, on ares, in the same frame.
             *   IT HAS TO BE THE MASTER THAT WAITS. Tried in the slave, and the slave
             * read FS as unmatched on 278 of 280 passes against the master's 231 of 271
             * matched -- the 32X VDP registers are not reliably readable from there.
             *   AND IT HAS TO BE BOUNDED: gating on the match with no way out
             * DEADLOCKED at one flip in 685 vints. */

            R60_GO = (uint16_t)(R60_GO + 1);   /* FM is up again: the slave may draw */
            R60_EVPUT(2);
            R60_DIAG[9]++;               /* gos launched */
        }
    }
}

#include "r60_speech.inc"   /* the speech + SFX lanes, in this unit so their calls are not opaque */
