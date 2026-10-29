#ifdef R60_FRONTEND
#include "../../md_src/frontend.h"
static volatile unsigned frontend_paused;
#endif
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
#if defined(R60_FRONTEND) && (!defined(R60_SLAVEWIN) || defined(R60_NOVDPOVERLAP))
#error "The frontend pause adapter requires the R60 slave-owned frame loop"
#endif
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
#ifdef R60_PUNCHCACHED
#define FB_RMW(off)   FB_DIR(off)
#else
#define FB_RMW(off)   ((volatile uint32_t *)(0x24000000u + FB_ROW0 + (off)))
#endif

#define SPR_SNAP      ((const volatile uint16_t *)0x26028400u)  /* 64 x 8 words, the port's */
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
#ifdef R60_LOGOPROBE
static uint32_t logo_ring[256] __attribute__((aligned(16)));   /* [go<<16 | bank<<8 | phase], count ; index in [255] */
#define LOGOCOUNT(ph) do { unsigned _y, _x, _n = 0; for (_y = 24; _y < 80u; _y++) { const volatile uint8_t *_r = (const volatile uint8_t *)FB_DIR(_y * FB_W + 100u); for (_x = 0; _x < 120u; _x++) if (_r[_x]) _n++; } \
        { unsigned _i = logo_ring[255] & 127u; logo_ring[_i * 2u] = ((uint32_t)go << 16) | ((unsigned)(bkp & 1u) << 8) | (ph); logo_ring[_i * 2u + 1u] = _n; logo_ring[255] = _i + 1u; } } while (0)
#else
#define LOGOCOUNT(ph) ((void)0)
#endif
#define R60_EVPUTS(ev) do { uint16_t _l = (uint16_t)(r60_frt() - R60_TVINT) / 184u; \
        R60_EVRING[R60_EVRING[255] & 0x7Fu] = ((uint32_t)(ev) << 28) | ((uint32_t)(R60_DIAG[0] & 0xFFFu) << 16) | _l; \
        R60_EVRING[255] = (R60_EVRING[255] + 1u) & 0x7Fu; } while (0)
#define R60_EVPUT(ev) do { uint16_t _l = (uint16_t)(r60_frt() - R60_TVINT) / 184u; \
        R60_EVRING[R60_EVRING[255] & 0x7Fu] = ((uint32_t)(ev) << 28) | ((uint32_t)(r60_frame & 0xFFFu) << 16) | _l; \
        R60_EVRING[255] = (R60_EVRING[255] + 1u) & 0x7Fu; } while (0)
#define PAL_SH        ((const volatile uint16_t *)0x26027000u)  /* the port's palette shadow */

/* the slave mailbox: the master posts a frame number, the slave echoes it when the bank is
 * drawn. One word each way, no protocol -- the old renderer's SYNC[] command set, its band
 * queue and its fences are all gone with the sbuf they coordinated. */
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
#ifndef R60_PACE
#define R60_PACE 0
#endif
#define R60_STR2(x) #x
#define R60_STR(x)  R60_STR2(x)
#if R60_PACE > 0
#define R60_SPACE() __asm__ __volatile__(".rept " R60_STR(R60_PACE) "\n\tnop\n\t.endr" \
                                         ::: "memory")
#else
#define R60_SPACE() do { } while (0)
#endif

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
 * The bars (r60_rs_bars) replace every other bar: rows 196..223, one row a value, 1 px a line. */
#define RSX ((volatile uint32_t *)0x26030C00u)
static inline uint16_t r60_frt(void);
static __attribute__((noinline)) void r60_rs(unsigned k) { uint16_t n = r60_frt(); RSX[k] += (uint16_t)(n - (uint16_t)RSX[15]); RSX[15] = n; }
static __attribute__((noinline)) void r60_rm(unsigned k) { uint16_t n = r60_frt(); RSX[16u + k] += (uint16_t)(n - (uint16_t)RSX[24]); RSX[24] = n; }
#define RS(k) r60_rs(k)
#define RM(k) r60_rm(k)
#define PHT_B() (RSX[25] = r60_frt())
#define PHT_E(k) (RSX[12u + (k)] += (uint16_t)(r60_frt() - (uint16_t)RSX[25]))
#elif defined(R60_PHBARS)
static uint32_t r60_pht[2]; static uint16_t r60_pht_t0;   /* probe: [0] sprite plot, [1] priority punch, FRT ticks */
#define PHT_B() (r60_pht_t0 = r60_frt())
#define PHT_E(k) (r60_pht[k] += (uint16_t)(r60_frt() - r60_pht_t0))
#else
#define PHT_B() ((void)0)
#define PHT_E(k) ((void)0)
#endif
#ifndef R60_RIGSPLIT
#define RS(k) ((void)0)
#define RM(k) ((void)0)
#endif
#ifndef R60_FATSPAN
#define R60_FATSPAN 4u
#endif
#ifndef R60_FATCAP
#define R60_FATCAP 160u
#endif
unsigned r60_artcap = 40u;                             /* art records a plane a window; the master lowers it under the 32X fill */
static unsigned r60_fill_anchor[2], r60_fill_prevk[2], r60_fill_rows[2], r60_fill_k, r60_fill_mode2;   /* step B per-bank canvas state */
extern void r60_fill_cells(volatile uint8_t *, volatile uint8_t *, unsigned, unsigned, unsigned, unsigned, unsigned);
extern void r60_fill_cram(const volatile uint16_t *);
static uint16_t txt_copy[28u * 40u];                   /* the slave's text layer state (hoisted: the clear phase needs it) */
static uint8_t  txt_pend[2][28u * 40u];
static uint16_t txt_init;
static uint8_t  txt_force[2];
static unsigned txt_xo[2];                             /* the offset this bank's glyphs were drawn at */
#ifdef R60_TXTFAST
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
#endif
#define R60_SHIFTNEXT (*(volatile uint16_t *)0x26028FF8u)
#ifdef R60_CRAMMASK
#define R60_CRAMMASK_W (*(volatile uint16_t *)0x26028FB0u)   /* master -> slave: sprite colour sets whose words changed */
#define R60_POOLDIRTY  (*(volatile uint16_t *)0x26028FB2u)   /* master -> slave: the text pool's colours changed */
#endif
#define R60_MDHOLD     (*(volatile uint16_t *)0x26028FE2u)   /* the packet's bit 13 source: hold without the slave's extra window */
#ifndef R60_HOLD_HOLES
#define R60_HOLD_QUIETN 28u                /* sw180: records built by a 14-row window that count as a quiet one */
#define R60_HOLD_HOLES 80u                 /* a scrolling demo's steady edge reads ~60 over five windows */
#endif
#define R60_TXTBASE   192u
#ifdef R60_TXTPOOL
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
#define R60_POOLBYTE(s) ((s) < 15u ? ((s) + 1u) * 16u : ((s) - 15u) * 16u + 15u)
#endif
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
#ifndef R60_GOVERNOR
#define R60_GOVERNOR 2                  /* REBUILD section 2's pacing governor: 2 for
                                         * bring-up (clean 30Hz), 1 is the ship state. A
                                         * divider on the same identical frame, not a
                                         * phase machine. */
#endif

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
#ifdef R60_HTPROBE
volatile uint16_t r60_ht_mem[8];   /* PROBE R60HTPROBE: [0] staged copies outside the hold handshake [1] staged copies */
#endif
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
#ifdef R60_EVPROBE
static __attribute__((noinline)) void r60_evp(unsigned ev) { R60_EVPUT(ev); }   /* PROBE: the master's event, cart (.ramtext is full) */
#endif

#ifndef R60_SPEECH
void amb_dma_handler(void) { }
#define R60_WAITBODY ((void)0)
#else
void speech_init(void); void speech_pump(void);   /* r60_speech.c: the slave's PWM speech lane */
#ifdef R60_SPEECHSLAVE
void speech_pump_chunk(void);
#define R60_WAITBODY speech_pump_chunk()   /* every R60_WAITBODY site is the slave's (R60SPEECHSLAVE) */
#else
#define R60_WAITBODY ((void)0)   /* sw240: the speech fill runs in the DMA IRQ, the loop is untouched */
#endif
#endif

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

/* THE 32X CRAM COMMIT. Sixteen banks of sixteen, bank b carrying sprite colour-set b,
 * straight from the port's own PAL_SH shadow. S16 packs a channel as four low bits plus
 * one high bit and 32X CRAM is BGR555, so each channel is (nibble << 1) | highbit --
 * five bits, no quantisation, which is the point of drawing on the 32X side instead of
 * the MD's three. Leaving it out entirely is why the first board launch was BLACK while
 * ares counted 2,427 sprites drawn: the framebuffer held correct indices and nothing
 * resolved them. */
#ifdef R60_PALMAP
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
#endif
#ifdef R60_CRAMDELTA
static uint16_t r60_cram_shadow[256] = { [0 ... 255] = 0xFFFEu };
#ifdef R60_CRAMMASK
static unsigned r60_cm_skip, r60_cm_pool;
#ifdef R60_CMSTATS
volatile unsigned r60_cm_stats[8];       /* [0] commits [1] pool dirty [2] banks compared [3] entries written [4] resends [5] mask bits */
#endif
#endif
R60CODE static inline uint16_t r60_cram_eff(unsigned b, unsigned p)
{
#ifdef R60_TXTPOOL
    if (p == 15u || (p == 0u && b)) {            /* a pool entry: the text colour parked there, or black */
        uint16_t c = R60_POOLCOL[p == 15u ? 15u + b : b - 1u];
        return c == 0xFFFEu ? 0u : c;
    }
#endif
#ifdef R60_PALMAP
    {   unsigned pl = R60_BANKPAL_U[b]; if (pl > 63u) pl = b;
        return PAL_SH[1024u + pl * 16u + p]; }
#else
    return PAL_SH[1024u + b * 16u + p];
#endif
}
#endif
R60CODE static void r60_cram_commit(void)
{
#ifdef BOOT_FLIPRATE
    /* THE VALUE-CHANNEL INSTRUMENT FLOODS CRAM and this renderer OWNS CRAM, so the two
     * cannot both write it: with the commit on, rig_value reads "NO" at every sample.
     * Under the probe the renderer yields the palette -- the picture is wrong and the
     * NUMBER is the point. Probe-only; it costs the shipping build nothing. */
#else
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
    ((volatile uint16_t *)0x20004200u)[R60_BAR_PEN] = 0x7FFFu;
#ifdef R60_CRAMDELTA
    {   /* RESEND EVERYTHING EVERY 64 COMMITS. The first commits at boot are dropped (FM
         * is not yet the SH-2's), yet they updated the shadow, so an entry that never
         * changes again was never written: the text sets read zero for the whole run
         * while their shadow was right (sw55; a ramp probe landed only the one entry
         * that kept changing). 256 writes once a second. */
        static unsigned ncommit;
        if ((++ncommit & 63u) == 1u) { unsigned i; for (i = 0; i < 256u; i++) r60_cram_shadow[i] = 0xFFFEu; R60_DIAG2[0]++; }   /* 0xFFFE: 0xFFFF is a real colour */
#ifdef R60_CRAMMASK
    {   unsigned cm = R60_CRAMMASK_W, pd = R60_POOLDIRTY, resend = (ncommit & 63u) == 1u;
        R60_CRAMMASK_W = 0; R60_POOLDIRTY = 0;
#ifdef R60_PALMAP
        cm |= R60_REMAPMASK; R60_REMAPMASK = 0;
#endif
        r60_cm_skip = resend ? 0u : (~cm & 0xFFFEu);   /* banks the compare may skip this pass (never bank 0); the pool entries get their own pass below */
        r60_cm_pool = pd && !resend;
#ifdef R60_CMSTATS
        r60_cm_stats[0]++; if (pd) r60_cm_stats[1]++; if (resend) r60_cm_stats[4]++; r60_cm_stats[5] += (unsigned)__builtin_popcount(cm & 0xFFFFu);
#endif
    }
#endif
    }
#endif
    /* (A one-off readback of CRAM 176-191 lived here and wrote 0x26028EA0, which is
     * R60_DIAG2[8] onward -- it silently overwrote the flip counters with colour words.
     * It answered its question: bank 11 reads back real colours. Removed.) */
    for (b = 0; b < 16; b++) {
        unsigned p;
#ifdef R60_CRAMDELTA
        {   /* ONLY THE ENTRIES THAT CHANGED: on ares every CRAM write waits for PEN (~200
             * cycles, 36 lines a commit on the profile); a bank with no change costs 16 compares. */
            unsigned q, dirty = 0;
#ifdef R60_CRAMMASK
            if ((r60_cm_skip >> b) & 1u && !(R60_DISPHOLD == 2u && (b == 7u || b == 11u || b == 15u || b == 10u))) continue;   /* the master saw no change in this set */
#endif
            for (q = 0; q < 16; q++)
                if (r60_cram_eff(b, q) != r60_cram_shadow[b * 16u + q]) { dirty = 1; break; }
#ifdef R60_TXTPOOL
            if (!dirty && b && !(R60_DISPHOLD == 2u && (b == 7u || b == 11u || b == 15u || b == 10u))) continue;   /* bank 0 and the fill sets under the hold always; the pool entries are in the compare */
#else
            if (!dirty && b && !(b >= 12u && b < 15u) && !(R60_DISPHOLD == 2u && (b == 7u || b == 11u || b == 15u || b == 10u))) continue;   /* bank 0, the text sets, and the fill sets under the hold always */
#endif
        }
#endif
        R60_PENWAIT();
#ifdef R60_CMSTATS
        r60_cm_stats[2]++;
#endif
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
#if defined(R60_TEXT) && !defined(R60_TXTPOOL)
            if (b >= 12u && b < 15u) {            /* text colour groups: two per set */
                unsigned g = (b - 12u) * 2u + (p >> 3), tp2 = R60_TXTPAL[g];
                v = (tp2 == 0xFFu) ? 0u : PAL_SH[tp2 * 8u + (p & 7u)];
            }
#endif
#ifdef R60_CRAMDELTA
            if (b >= 12u && b < 15u && v != r60_cram_shadow[b * 16u + p]) R60_DIAG2[1]++;   /* text entries about to be written */
            if (v == r60_cram_shadow[b * 16u + p] && (b | p)) continue;   /* entry 0 always:
                                          * the 68K's boot stages write it late (MDSTAGE,
                                          * md_main.c) and it must carry the through bit */
            r60_cram_shadow[b * 16u + p] = v;
#ifdef R60_CMSTATS
            r60_cm_stats[3]++;
#endif
#endif
            unsigned r  = (((v) & 0xFu) << 1) | ((v >> 12) & 1u);
            unsigned g  = (((v >> 4) & 0xFu) << 1) | ((v >> 13) & 1u);
            unsigned bl = (((v >> 8) & 0xFu) << 1) | ((v >> 14) & 1u);
            if (b * 16u + p == R60_BAR_PEN) continue;      /* reserved */
#ifdef R60_CRAMPROBE
            if (b >= 12u && b < 15u) { ((volatile uint16_t *)0x20004200u)[b * 16u + p] = (uint16_t)(0x100u + b * 16u + p); continue; }
#endif
#ifdef R60_RB_PROBE
            if (b == 0u && p == 0u) continue;   /* the 68K owns entry 0 under the probe:
                                                 * it is the readback's verdict colour */
#endif
            /* (CRAM bit 15, the per-pen 32X-over-MD priority, was tried here and
             * changed the display by not one byte -- as did the GLOBAL priority in
             * MARS_VDP_DISPMODE, which reads back 0x8081. Neither is what hides the
             * sprites. The old renderer masked bit 15 OFF, m_main.c:5812, so leaving
             * it clear is also what it did.) */
#ifdef R60_CRAMFLOOD
            /* EVERY ENTRY BRIGHT. The display shows the probe blocks (pens we choose
             * and force) and NOT the sprites (pens 177-187, which the records pick),
             * so the next question is whether those entries hold a colour at all --
             * our commit sources bank b from PAL_SH[1024 + b*16 + p], and if that is
             * the wrong part of the shadow the sprites are drawn in black. */
            ((volatile uint16_t *)0x20004200u)[b * 16u + p] =
                (uint16_t)(0x0400u | ((b * 16u + p) & 0x1Fu) | 0x03E0u);
#else
            /* ENTRY 0 CARRIES THE THROUGH BIT. ares vdp.cpp:71-81 (and the board agrees
             * with ares on office): with the 32X in front (DISPMODE PRI=1) an MD pixel
             * shows only where the 32X pixel's CRAM entry has bit 15 SET and the MD
             * pixel is not backdrop. Pen 0 is the sprite layer's transparent pixel, so
             * entry 0 gets bit 15 and the MD planes show through it; every other entry
             * stays in front. Without this the planes were loaded and invisible. */
            ((volatile uint16_t *)0x20004200u)[b * 16u + p] =
                (uint16_t)((bl << 10) | (g << 5) | r | ((b | p) ? 0u : 0x8000u));
#endif
        }
    }
#if defined(R60_CRAMMASK) && defined(R60_TXTPOOL)
    if (r60_cm_pool) {   /* THE POOL'S OWN PASS: 31 entries compared, not 256 (a third of commits had only the text pool dirty) */
        unsigned s2, waited = 0;
        for (s2 = 0; s2 < 31u; s2++) {
            unsigned e2 = s2 < 15u ? (s2 + 1u) * 16u : (s2 - 15u) * 16u + 15u;
            uint16_t c = R60_POOLCOL[s2], v = (c == 0xFFFEu) ? 0u : c;
            unsigned r, g, bl;
            if (v == r60_cram_shadow[e2]) continue;
            r60_cram_shadow[e2] = v;
            if (!waited) { R60_PENWAIT(); waited = 1; }
            r  = (((v) & 0xFu) << 1) | ((v >> 12) & 1u);
            g  = (((v >> 4) & 0xFu) << 1) | ((v >> 13) & 1u);
            bl = (((v >> 8) & 0xFu) << 1) | ((v >> 14) & 1u);
            ((volatile uint16_t *)0x20004200u)[e2] = (uint16_t)((bl << 10) | (g << 5) | r);
        }
    }
#endif
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
        if (rb == 0x7FFFu) R60_DIAG2[4]++; else { R60_DIAG2[5]++; R60_DIAG2[6] = rb; }
        /* AND SAY IT WHERE THE BOARD CAN SHOW IT. Every 32X pixel resolves through CRAM,
         * so if CRAM is the fault no 32X-side instrument can report it -- the bars are
         * invisible for the same reason the sprites are speckled. The MD BACKDROP is a
         * different path entirely (MD CRAM entry 0, written by the 68K), and it shows
         * wherever the 32X layer is transparent. GREEN = the readback matched, RED = it
         * did not. */
#ifndef R60_FRONTEND
        MARS_SYS_COMM12 = (uint16_t)(0xA000u | (rb == 0x7FFFu ? 0u : 1u));
#endif /* frontend owns COMM12; the verdict remains in DIAG2 */
    }
#endif
}

/* THE FRAME CLOCK. REBUILD section 2's handler shape: tick, commit, out. */
/* The phase counters outgrow region 1. Keep the probe's frame clock in SDRAM
 * too, using region 2's spare room rather than changing a measured phase to ROM. */
#ifdef R60_FRONTEND
__attribute__((noinline)) static int frontend_vblank_pause(void)
{
    if (!frontend_paused) return 0;
    if (MARS_SYS_COMM14 == 0xB007u) MARS_SYS_COMM14 = 0xB008u;
    pp_due = 0; r60_frame++;
    return 1;
}
#endif
#if defined(R60_RIGSPLIT) && defined(PORT_HOT2)
__attribute__((section(".ramtext2")))
#else
R60CODE
#endif
void visr_vbi(void)
{
    R60_DIAG[0]++;
#ifdef R60_FRONTEND
    if (frontend_vblank_pause()) return;
#endif
    if (R60_FM_IS_UP) R60_DIAG2[0]++; else R60_DIAG2[1]++;   /* V-ISR entries by FM state */
    /* ANSWER THE 68K'S BOOT HOLD. md_main.c:7738-7754: the shim posts COMM14=0xB007
     * and then SPINS until the master answers 0xB008 ("hold the game until the
     * master's V-ISR is armed"), 6,000,000 iterations deep -- about 1,900 frames at
     * 7.67 MHz. MEASURED on the skeleton before this line: the 68K's own vint counter
     * 0xFFB0F0 read 2 at frame 700 and md_consume's body ran ONCE for packet A in 700
     * frames, so no MD-plane packet could ever be taken however well it was formed.
     * ANSWER ON SEEING 0xB007, NOT ON THE FIRST ENTRY: the first cut posted 0xB008
     * from the V-ISR's first vint, which lands BEFORE the shim posts 0xB007 -- the
     * shim then overwrote it and waited for an answer that had already been given.
     * COMM14 read 0xB007 for the whole run and the 68K vint count stayed at 2. */
    if (MARS_SYS_COMM14 == 0xB007u) MARS_SYS_COMM14 = 0xB008u;
    R60_DIAG[6] = MARS_SYS_COMM14;       /* the 68K's beacon, as the master sees it */
    /* CRAM COMMIT (REBUILD section 2 step 3), and leaving it out is why the first board
     * launch was BLACK while ares counted 2,427 sprites drawn: the framebuffer held
     * correct 8-bit indices and the 32X palette at 0x20004200 was never written, so every
     * index resolved to colour 0. The diag counters cannot see that; only the board can.
     *
     * Sixteen banks of sixteen, bank b carrying sprite colour-set b, straight from the
     * port's own PAL_SH shadow. S16 packs a channel as four low bits plus one high bit
     * (the same unpack mdp_quant does for the MD), and 32X CRAM is BGR555, so each
     * channel is (nibble << 1) | highbit -- five bits, no quantisation, which is the
     * point of drawing on the 32X side instead of the MD's three. */
    /* THE CRAM COMMIT IS OWED, NOT DONE HERE. It needs TWO things at once that the
     * V-ISR cannot both guarantee: FM=1, because at FM=0 the 68K owns the VDP side and
     * the master's writes are dropped (measured on this rom: 11,297 of 11,513 SH-2
     * framebuffer writes dropped at FM=0); and VBLANK, because 32X CRAM writes need PEN
     * on real hardware. The V-ISR runs at the vint edge, which under the new beat is
     * often INSIDE the 68K's span (194 of 687 vints measured), so a commit here is
     * dropped about a quarter of the time -- which is what the board showed: the right
     * shapes in the right places, speckled, because most of the palette was last
     * frame's. The main loop does it as soon as FM is back up, if still inside vblank.
     *
     * (ares shows none of this: its palette was correct with the commit in the ISR.
     * The rig is the instrument here, as it is for PEN generally.) */
    /* COMMIT AT THE VINT EDGE WHEN FM IS UP, AND NOT AT ALL WHEN IT IS NOT. The edge is
     * inside vblank by construction, which is the PEN window CRAM needs; FM=1 is the
     * other half, because at FM=0 the 68K owns the VDP side. Deferring the commit to
     * the main loop "as soon as both hold" was tried and landed 323 of 687 -- the span
     * is usually still open when vblank runs out. Under GOVERNOR=2 the two states
     * alternate with the beat, so skipping the FM=0 vints costs nothing: the commit
     * lands once per PRESENTED frame, which is the rate the palette changes at. */
#ifdef R60_NOVDPOVERLAP
    /* PROBE D: the ISR touches NO VDP-region address. 32X CRAM (0x20004200) and FBCTL
     * (0x2000410A) both go through the core's single SH-side VDP access path, and
     * IF.sv:974-983 has its in-flight guard COMMENTED OUT -- a new bus start on that
     * region re-latches VDP_A/VDP_DO/the strobes over an access still waiting for the
     * VDP's ack, and the earlier one is LOST. Both SH-2s share that path. So while the
     * slave is drawing, the master must stay off the region entirely. FM (0x20004000)
     * and the COMM registers (0x2000402x) are NOT in it (IF.sv:922-924) and stay. */
#else
    /* GATED ON THE SLAVE BEING IDLE, NOT ON FM. Both are master accesses to the VDP
     * region (32X CRAM is 0x20004200-43FF), and the core has no in-flight guard on the
     * single SH-side VDP path (IF.sv:974-983): an access from one SH-2 re-latches over
     * the other's pending one and that one is LOST. Measured on the board: with the
     * commit running while the slave drew, the sprites came out correctly shaped and
     * almost entirely WHITE (1,750 white px); with every master VDP access confined to
     * the slave-idle window, white fell to 1. After the flip fix a vblank entered with
     * FM up IS mid-pass, so the old FM gate no longer means "the slave is idle".
     * Staying in the V-ISR keeps the commit inside vblank, which is what PEN wants;
     * the frames it skips are counted and cost at most a stale palette. */
    /* BOTH CONDITIONS, AND THAT IS NOT A BELT AND BRACES. "The slave is idle"
     * (DONE == GO) is ALSO true while the 68K's span is open -- the span OPENS on that
     * very condition -- so gating on it alone put the commit at FM=0, where the write
     * is dropped: measured, CRAM committed 695 times and the ares display went back to
     * 23 rows, the border alone. FM up says the master owns the VDP; DONE == GO says
     * the slave is not mid-pass; the renderer needs both before it may touch the
     * region at all. */
    /* THE COMMIT IS NOT HERE ANY MORE. It needs three things at once -- FM up (the
     * master owns the VDP), the slave idle (the core has no in-flight guard on the one
     * SH-side VDP path, IF.sv:974-983), and PEN -- and a vint edge almost never lands
     * where the first two hold: gated on both, it fired 5 times in 1,091 vints and the
     * display went back to the border alone. It runs in the slave-idle window instead,
     * which is outside vblank, so it waits on PEN itself. */
#endif
    /* ONE GAME FRAME PER VINT, OWED, NOT CAPPED (sw185). This was `pp_due = 1`, so a master
     * pass longer than a vint lost a game frame outright: sw184 had drained 1522 ticks by
     * vint 1544 (22 lost, sw166 7), most of them under the fat direct builds, and the game
     * ran that much behind the arcade's clock. The arcade's vint never misses; the loop
     * now drains what it owes, two ticks back to back after a long pass. The cap of 8 is
     * a stall guard only; what it drops is counted. */
    if (pp_due < 8u) pp_due++; else r60_census[4]++;
    r60_frame++;
    R60_TVINT = r60_frt();
    R60_EVPUT(1);
    /* THE BARS' WINDOW: every 64 vints, latch the two rates. 64 vints is a flat second
     * and a bit, and x5 puts 64 presented frames at the full 320-pixel width, so the bar
     * reads directly as "how close to one presented frame per vint". */
    if ((r60_frame & 63u) == 0u) {
        {   /* BAR A: how often the 68K answered, over the last 64 vints */
            uint16_t po = (uint16_t)R60_DIAG[14], bk = (uint16_t)R60_DIAG[15];
            uint16_t dp = (uint16_t)(po - R60_WPOST), db = (uint16_t)(bk - R60_WBACK);
            R60_WPOST = po; R60_WBACK = bk;
            R60_WA = (dp + db) ? (uint16_t)(32u * ((8u * dp) / (dp + db))) : 0u;
#if defined(R60_FZ) && (R60_FZ == 1)
            /* THE FREEZE BIT, AND WHY IT IS SHAPED LIKE THIS. Bar instruments are dead
             * on this board -- r60bars3's unconditional 32 px floors read zero -- so a
             * probe cannot ANSWER in pixels. The one channel proven to work is CHANGE
             * OVER TIME: five captures of one launch came back five distinct images.
             * So the probe answers by STOPPING. Once the 68K has posted at least 8
             * times in the last 64 vints, the master stops issuing GO: no GO, no DONE,
             * no flip, and the picture freezes on whatever was last presented. A frozen
             * board therefore means THE 68K IS POSTING there. */
            if (dp >= 8u) R60_FREEZE = 1;
#elif defined(R60_FZ) && (R60_FZ == 2)
            /* The inverse: freeze when the span closed on the BACKSTOP 8 times in 64
             * vints, i.e. when the 68K is NOT answering. On ares this never fires
             * (backstops are 0), so a frozen board means the 68K never posts there. */
            if (db >= 8u) R60_FREEZE = 1;
#endif
        }
        {   uint16_t f = (uint16_t)R60_DIAG[1], k = (uint16_t)R60_DIAG[15];
        R60_BARA = (uint16_t)((uint16_t)(f - R60_BARF) * 5u);
        R60_BARB = (uint16_t)((uint16_t)(k - R60_BARK) * 5u);
        R60_BARF = f; R60_BARK = k; }
    }
    /* FLIP COMMIT, AND THE WHOLE POINT IS THE WORD *FINISHED*. REBUILD section 2 step 2:
     * this handler IS the vblank, so the commit is in the window by construction and needs
     * no V read.
     *
     * THE FIRST CUT FLIPPED EVERY SECOND VINT UNCONDITIONALLY and drew a PREFIX of the
     * sprite list: measured at f700, the port published 17 live records spanning sx
     * -13..311, and the framebuffer held records 0..7 only -- x 0..148 of 320, rows
     * 114..183 matching those eight records exactly, with record 8 (top=79) absent. The
     * art addressing was right; the handshake was missing. MARS_OVERWRITE_IMG follows the
     * WRITE-side bank, so a flip landing mid-list moves the slave's target out from under
     * it and the next pass's clear wipes what it drew. A bank is presented when the slave
     * says it is done with it, never on a timer.
     *
     * THE EDGE GUARD copied from the old renderer read `*(volatile uint16_t *)0xC00008`
     * and FLIPPED ZERO TIMES in 686 vints: that is the MD VDP's H/V counter, a 68K BUS
     * address the SH-2 cannot read. The edge-guard LAW (a late entry declines rather than
     * tears, LOOP24 Z2) needs the SH-2's own vblank evidence; P1 does not have one yet.
     * Noted, not faked. */
    if (R60_DONE != R60_GO || R60_GO == R60_SHOWN || R60_PUB != R60_GO) {
        R60_DIAG[8]++;                   /* DECLINED: the slave is still drawing, or the
                                          * plane packet for this frame is not out yet.
                                          * FM MUST NOT DROP HERE: the slave's writes are
                                          * dropped at FM=0 (measured 11,297 of 11,513 on
                                          * this rom), so a drop mid-draw corrupts the
                                          * frame it is trying to present. */
        return;
    }
    if ((uint16_t)(r60_frame - r60_last_flip) < R60_GOVERNOR) {
        R60_DIAG[9]++;                   /* held by the bring-up governor */
        return;
    }
    /* NEVER REQUEST A FLIP WHILE ONE IS STILL PENDING. A second request on top of an
     * unapplied one is IGNORED -- measured as 78 of 297 passes whose wait for FS timed
     * out, a count that did not move when the bound went from 2,000 ticks to a whole
     * frame, so they were never late applies. The slave then started against the bank
     * still on screen and the old request landed mid-pass, splitting one frame across
     * both banks: 3,563 image pixels in one and the 640 bar pixels in the other.
     *
     * PENDING IS TRACKED HERE, NOT READ FROM FS. Reading FS in the V-ISR to decide
     * cannot work and was tried: at the vblank edge the apply has not happened yet, so
     * FS never matches and the renderer deadlocks at one flip in 685 vints. The main
     * loop, which samples later, sees the match 231 times in 271 and is what clears
     * this. Declining is the edge-guard law in its own words: a drop beats a tear. */
    /* TWO CONDITIONS, AND BOTH ARE DECLINES, NOT WAITS -- REBUILD section 2 step 2:
     * the V-ISR never waits, it drops, because a drop beats a tear (LOOP24 Z2).
     *
     *   1. A FLIP IS STILL PENDING. A second request on top of an unapplied one is
     *      IGNORED, measured as 78 of 297 passes whose wait for FS timed out at ANY
     *      bound -- the slave then started against the bank still on screen and the old
     *      request landed mid-pass. Pending is tracked here rather than read from FS,
     *      because reading FS in the ISR to decide cannot work and was tried: at the
     *      vblank edge the apply has not happened yet, so FS never matches and the
     *      renderer deadlocks at one flip in 685 vints. The main loop, sampling later,
     *      sees it and clears this.
     *   2. WE ARE NOT INSIDE VBLANK. FBCTL bit 15 is the vblank flag. An FS write
     *      outside vblank is DEFERRED to the next one, and a deferred request applies
     *      after the slave has begun its next pass -- the write-side bank then switches
     *      under it. ares shows this is already true here (--trace-flip: 217 of 220
     *      writes inside vblank, 2 deferred, every write followed by its flip at the
     *      same vcounter), so this guard costs ares nothing and is for the board. */
#ifdef R60_NOVDPOVERLAP
    return;                              /* PROBE D: the flip moves to the window too */
#endif
    /* THE V-ISR NO LONGER FLIPS, AND THAT IS THE BUG THIS FIXES.
     *
     * An SH-2 write to MARS_VDP_FBCTL is DROPPED at FM=0: the SH-side VDP access is
     * only latched with FM (MiSTer IF.sv:974), and ares returns before its own trace
     * hook (io-internal.cpp:344-349), so a dropped write leaves no trace row either.
     * The master opens the 68K's FM=0 span on EXACTLY the condition the V-ISR needed in
     * order to flip -- DONE==GO && PUB==GO -- and it does so EARLIER in the vint. So
     * every flip request since the beat went in at FM=0 and was thrown away.
     *
     * THE EVIDENCE WAS IN MY OWN FIRST TRACE AND I WALKED PAST IT: --trace-flip logs
     * 220 SH-2 writes and the LAST IS AT FRAME 447, none in the remaining 650 frames,
     * which I read as the file ending. Framebuffer half 1 has not changed since: 0
     * bytes different across f1097-f1100 while half 0 alternates by 5,276, and two
     * consecutive screenshots are byte-identical. R60_DIAG2[12] -- "gave up waiting for
     * FS" -- counted 247 of them and I read it as a core quirk.
     *
     * The request moves to the span-open site, where FM is still up. It is then OUTSIDE
     * vblank, so the hardware defers it to the next vblank start (VDP.sv:400,
     * `if (VBLK || MODE==0) FS <= FBCR.FS`) -- in time for the 68K's vint, its post, FM
     * back up, and the next GO. This also retires the "FS matched 231 of 271" and
     * "gating on the match deadlocked" censuses recorded in this file: both were this.
     *
     * (The decline counters DIAG2[14]/[15] stay defined and now read zero, which is the
     * cheapest standing check that the flip has not crept back in here.) */
}

/* THE MD PLANE PACKET, master -> 68K, in the shape md_consume already reads.
 *
 * NOT A LIFT. The old renderer's publish is INLINE inside m_main's 5,700-line body
 * (sh_src/m_main.c:13778 onward, `sc = r60_pkt_flip ? md_pktA : md_pkt` at :18441), so
 * there is no function to carry across; what carries is the CONTRACT, which md_main.c
 * states on its own side and which this writes to:
 *
 *   [0] 0xB6B6 magic, written LAST and cleared by the consumer as its ack
 *   [1] type in the low byte (0 = tile batch, else name-table), bit15 palette present,
 *       bit13 display hold                                      md_main.c:1441,1449
 *   [2] cell0, bit15 = plane A (0xC000) else plane B (0xE000)   md_main.c:2351
 *   [4] VSRAM plane B vy   [6] VSRAM plane A vy                 md_main.c:2321
 *   [5] count, low 12 bits                                      md_main.c:1441
 *   [8..] NT_WRAP payload: EXACTLY 7 rows of
 *            [hdr = (plane_row << 8) | c0]  (bit15 = full 64-cell row, DMA'd)
 *            [w1  = (start << 8) | count]   (bit15 = EDGE42 pair follows)
 *            [count cells]
 *         then the hscroll delta tail [n] followed by n x [(plane<<7)|strip, value]
 *                                                          md_main.c:2342-2412
 * A row of count 0 is legal, so the smallest valid packet is 7 x (hdr, 0) plus a zero
 * tail: 15 words after the header. That is what this first build sends -- the transport
 * and the beat, with no cells, because the cells need the MD VRAM SLOT ALLOCATOR and
 * that is the next box, not this one.
 *
 * WHICH BANK, AND WHY PUBLISH SITS AFTER THE FLIP. The 68K reads the packet at 0x857000 (A; B at 0x85E800),
 * which is the FB window, and the window addresses the bank that is NOT being displayed.
 * Writing the packet before the flip would put it in the bank the flip then DISPLAYS, and
 * the 68K would be left reading the other one. So the beat is DONE -> SHOWN -> PUBLISH,
 * all within one vint and all for one game frame: the alignment the planes need is that
 * they describe the same frame as the sprites, which this keeps. */
/* THE PLANE PACKETS ARE FRAMEBUFFER STORES TOO, so they take the same window as every
 * other store the master makes: cached write-through, not the uncached alias. */
#define R60_PKT_A     ((volatile uint16_t *)(FB_WBASE + 0x17000u))  /* 68K 0x857000 (was 0x1E000): a FAT window is 3072 words, 0x17000..0x18800, above the canvas end 0x11E00 and below B at 0x1E800. The 68K's consume call is in md_src/r60_shim.h, not only md_main.c (sw102 moved md_main.c alone and consumed 0 A packets) */
#define R60_PKT_B     ((volatile uint16_t *)(FB_WBASE + 0x1E800u))  /* 68K 0x85E800 */
#define R60_PKT_A_U   ((const volatile uint16_t *)0x24017000u)      /* reads: uncached */
#ifdef R60_HOLDDIRECT
#ifdef R60_PKTODDHI
/* THE ODD HOLD PACKET OFF THE CANVAS (R60PKTODDHI, 2026-10-24). At 0x11800 it overlapped the canvas
 * (rows 222-223 end at 0x11C00, the spill at 0x11E00): every odd direct packet left its words as
 * pixels on the bottom two lines of the bank it landed in, which no sprite clear removes (ares,
 * sw312 attract frame 3290: 437 bytes in rows 222-225 of the shown bank, visible on line 223).
 * Staged packets are at most R60_PKTA_CAP_STAGE / 2 + 8 longwords (8,032 B): 0x11E00..0x13D60. */
#ifndef R60_STAGEDIRECT
#error "R60PKTODDHI fits the staged packet (8 KB) between the canvas end and 0x17000; the unstaged direct packet is 22 KB"
#endif
#define R60_PKT_A_ALT ((volatile uint16_t *)(FB_WBASE + 0x11E00u))  /* 68K 0x851E00 (md_src/r60_shim.h R60_PKTODD_68K) */
#define R60_PKT_A_ALTW ((volatile uint16_t *)0x24011E00u)
#else
#define R60_PKT_A_ALT ((volatile uint16_t *)(FB_WBASE + 0x11800u))  /* 68K 0x851800: the odd-parity direct hold packet (0x11800..0x17000, 11264 words) */
#define R60_PKT_A_ALTW ((volatile uint16_t *)0x24011800u)
#endif
#define R60_PKT_A_DW  ((volatile uint16_t *)0x24017000u)            /* the even-parity direct packet, written uncached in place */
#define R60_HOLDFLIP  (*(volatile uint16_t *)0x26028FBAu)           /* slave -> master: no flips while the hold lasts (the write bank is stable) */
#define R60_PKTA_DIRECT 0x80000000u                                 /* R60_PKTA_LEN flag: built in place, nothing to copy */
#define R60_BUILDING  (*(volatile uint16_t *)0x26028FBCu)           /* master -> slave: a direct build is in progress, keep FM up */
#define R60_FMSOON    (*(volatile uint16_t *)0x26028FBEu)           /* slave -> master: FM is about to drop, do not start a build */
#define R60_FMSOON_CLR() (R60_FMSOON = 0)
#ifdef R60_STAGEDIRECT
/* STAGED DIRECT PACKETS (sw188). The direct build wrote the framebuffer, so it needed FM up and
 * ran back to back with the 68K's consume: a fat window cost ~6 frames (build ~1.3, DMA ~1.7 at
 * 18 words a 68K line, handshakes). Now the master builds the next packet into SDRAM while the
 * 68K consumes the current one, and copies it into the FB region in ~15 lines once FM is up.
 * 0x06039000-0x0603AFFF is free in the port build (m_main.o is filtered out; its probes were
 * the only users; md_ref starts at 0x0603B000). 4096 words: 14 rows + 170 art records. */
#define R60_STAGE_DIRECT  ((volatile uint16_t *)0x26039000u)
#define R60_PKTA_CAP_STAGE 4000u
#define R60_ARTMAX_STAGE   170u   /* per plane (100 measured 3 frames slower at the title cut, sw196: fewer records a window) */
static unsigned r60_stage_full, r60_stage_words;   /* a built packet waits in staging for FM up */
static uint32_t r60_stage_tbuilt, r60_stage_tcopy;  /* census: vint counter at build end / at copy (where a window's frames go) */
static uint16_t r60_stage_nxt;
#endif
#define R60_PKTA_CAP_DIRECT 11264u
#define R60_ARTMAX_DIRECT 500u
#else
#define R60_FMSOON_CLR() ((void)0)
#endif

#ifdef R60_PLANES
extern void r60_line_plan(void);
/* THE PACKETS ARE BUILT ON THE MASTER AFTER ITS TICK into SDRAM staging (two parities,
 * A and B, 768 words each) and COPIED into the framebuffer by the slave in its window:
 * the walk on the slave pushed its pass past the vint (sw13: window 24 lines, 0.77
 * frames a vint). The master has ~230 free lines a vint. */
/* A and B ARE SEPARATE ARRAYS (2026-10-24, PORTHOT2's room): B was [3072] like A but uses 752
 * words at most (rows end before the palette block at 688, the palette is 64 words, and the
 * slave's copy caps it at 376 longs), so 2 x 2304 words of B were never touched. The 9,216
 * bytes go to the hot set's SDRAM region above .bss (mars.ld .ramtext2). */
#define R60_PKTB_STAGE_WORDS 768u
static uint16_t r60_pkt_stage_a[2][3072] __attribute__((aligned(16)));   /* A may be FAT (3072 words, r60_planes.c R60_PKTA_CAP) */
static uint16_t r60_pkt_stage_b[2][R60_PKTB_STAGE_WORDS] __attribute__((aligned(16)));   /* B uses 752 */
_Static_assert(376u * 2u <= R60_PKTB_STAGE_WORDS && 688u + 64u <= R60_PKTB_STAGE_WORDS,
               "packet B's staging must hold the slave's 376-long copy and the palette block at 688");
static unsigned r60_pkta_len_mem[4];              /* [0..1] A words per parity, [2..3] B words */
#define R60_PKTA_LEN ((volatile unsigned *)((uint32_t)r60_pkta_len_mem | 0x20000000u))   /* words of packet A per staging parity (uncached: written by the master, read by the slave) */
#define R60_PKTSTAGE(par, k) ((volatile uint16_t *)((uint32_t)((k) ? r60_pkt_stage_b[(par) & 1u] : r60_pkt_stage_a[(par) & 1u]) | 0x20000000u))
#define R60_PKTSEQ    (*(volatile uint16_t *)0x26028E3Au)
#define R60_PKTDONE   (*(volatile uint16_t *)0x26028E3Cu)   /* the one the slave last copied */
/* R60_MBAND: THE MASTER DRAWS THE SPRITES' ROWS 0..split-1, THE SLAVE split..223, in parallel
 * (e0cff726; revived 2026-10-23 on the ROWCLR/KEEPSPR/PRIO line). The slave clears, sets the
 * offset and the palette map, publishes the BAND DESCRIPTOR (bank, split, offset, the FG
 * scroll the punch uses, the keep decisions) and posts CLEARED; the master, idle after its
 * tick, draws every unkept sprite's rows above the split (and the rows above the split of a
 * kept sprite's re-punch band) into the hidden bank, punches its own boxes, and posts MDONE;
 * the slave waits for MDONE before the text. THE TWO CORES RUN THE SAME CACHED STATICS
 * INCOHERENTLY (write-through, no snooping), so the master writes NONE of the slave's: its
 * spans go to its own list (R60_MSPANS, uncached, folded into the slave's clear and keep-carry
 * exactly like the slave's own), its boxes and punched rows to R60_MBOX / R60_MPYR, which the
 * slave merges into ks_box / ks_pyr after MDONE, and its draw state (span cursor, box, offset,
 * punch scroll, mask scratch) lives in its own r60_dc_t. The FG row-mask cache is the slave's:
 * the master reads it uncached under a tag check before and after (the slave invalidates the
 * tag before a refill) and builds the mask from the art itself when the check fails. */
#define R60_CLEARED   (*(volatile uint16_t *)0x26028E3Eu)   /* slave -> master: the pass whose band may be drawn */
/* NOT at 0x28ED0 (831ab083 had it there): r60_planes.c's md_dirty bitmap is [32] longs from 0x28EC0
 * through 0x28F3F and the master's MD_MARK read-modify-writes md_dirty[4] through the CACHED alias,
 * writing a stale MDONE / SPLIT back: R60MBTIME (2026-10-24) caught the band for one pass re-run
 * 130-250 lines after the slave had already taken it, the split read torn, and the slave's
 * give-ups. 0x3ED78-0x3ED7F is free under the master stack's floor (R60_MSPAN_FULL ends 0x3ED78). */
#define R60_MDONE     (*(volatile uint16_t *)0x2603ED78u)   /* master -> slave: the pass whose band is down */
#define R60_MMASK(bk) ((volatile uint32_t *)(0x26028F00u + ((bk) & 1u) * 32u))   /* the row-mask clear's band mask (not under RECTCLR) */
#define R60_CLRMASK   ((volatile uint32_t *)0x26028F40u)   /* this pass's merged clear mask, 7 words (not under RECTCLR) */
#ifdef R60_MBAND
#if !defined(R60_RECTCLR) || !defined(R60_ROWCLR) || !defined(R60_KEEPSPR) || !defined(R60_PRIO) || !defined(R60_PLOT5) || !defined(R60_SLAVEWIN)
#error "R60_MBAND is written for the line: SLAVEWIN + RECTCLR + ROWCLR + KEEPSPR + PRIO + PLOT5"
#endif
#if defined(R60_PRIOMASK) || defined(R60_PRIOROW) || defined(R60_PUNCHBAND) || defined(R60_PLOT5CHECK) || defined(R60_ZOOMCHECK)
#error "R60_MBAND: the in-plot priority forms, PUNCHBAND and the check builds keep slave statics the master would write (ROWFAST takes the context, 2026-10-24)"
#endif
/* the band descriptor, slave -> master, complete before CLEARED. NOT in the MMASK/CLRMASK words at
 * 0x28F00-0x28F5C: r60_planes.c's md_dirty bitmap is [32] longs from 0x28EC0, i.e. up to 0x28F40,
 * and the keep flags written there cost a night (2026-10-24: the MD cells under every sprite came
 * back torn, in the null probe too). 0x3EC00-0x3ED80 is free under the master stack's floor. */
#define R60_BD        ((volatile uint16_t *)0x2603ED20u)    /* [0] bank [1] split [2] xo [3] prio xs [4] prio ys [5] prio psel [6] hold [7] FG mask cache ready */
#define R60_BD_KEEP   ((volatile uint8_t *)0x2603ED30u)     /* [64] per record: bit 0 keep, bit 1 re-punch (0x3ED30-0x3ED70) */
#define R60_MSPAN_N(bk)    (*(volatile uint16_t *)(0x2603ED70u + ((bk) & 1u) * 2u))   /* the master's span list per bank: count */
#define R60_MSPAN_FULL(bk) (*(volatile uint16_t *)(0x2603ED74u + ((bk) & 1u) * 2u))   /* and its overflow flag (0x3ED70-0x3ED78) */
/* THE SPAN LISTS' SHARE: the slave draws ~2/3 of the sprite rows, the master ~1/3 (the fight: 532 spans
 * a bank, 2026-10-24). A full list is a whole-canvas clear and a full text redraw (25 lines a pass at
 * 8 overflows a window with 256 for the master). 640 + 448 in the memory the 768 list and the idle
 * box lists had (+512 B of .bss). */
#define R60_SSPAN_CAP 640u
#define R60_MSPAN_CAP 448u
#define R60_MSPANS(bk) ((volatile uint32_t *)((uint32_t)r60_rects_mem[(bk) & 1u] | 0x20000000u))   /* the box lists' memory, idle under ROWCLR */
static uint32_t r60_rects_mem[2][R60_MSPAN_CAP] __attribute__((aligned(16)));
static uint16_t r60_mbox_mem[64][2] __attribute__((aligned(16)));                    /* master -> slave: its box per record, [0] x0/4 | y0 << 8, [1] (x1+3)/4 | y1 << 8 (0: none) */
#define R60_MBOX      ((volatile uint16_t *)((uint32_t)r60_mbox_mem | 0x20000000u))
#define R60_MPYR      ((volatile uint16_t *)0x2603EC00u)    /* [64] master -> slave: rows its punch hit, ymin << 8 | ymax, 0 none (0x3EC00-0x3ED80: free under the master stack's sentinel floor) */
#define R60_BD_PYR    ((volatile uint16_t *)0x2603EC80u)    /* [64] slave -> master: the kept sprite's punched rows for a re-punch */
#define R60_MB        ((volatile uint32_t *)0x2603ED00u)    /* master census: [0] band ticks (master FRT, 46 a line) [1] bands [2] sprites drawn [3] wait ticks snapshot -> CLEARED [4] split sum [5] span-list overflows [6] bands not run (hold) [7] max band ticks */
#define R60_PH_MWAIT  (*(volatile uint32_t *)0x26028FD8u)   /* slave: ticks spent waiting for MDONE (R60_PH's neighbour, probe) */
#ifdef R60_MBTIME
/* R60MBTIME (PROBE, 2026-10-24): the band's timeline, both cores, raw FRT stamps (each core its own
 * FRT, 184 ticks a line; the host converts with the offset bracketed below). A ring of 8 pass
 * records x 24 u16 at 0x29440 (uncached; 0x29440-0x2963F reads zero through the tape and only
 * PRIOROW, which MBAND excludes, uses it), the globals at 0x295C0.
 *  slave:  [0] go [1] pass start (GO) [2] DIAG[0] at [1] [3] the span close before it [4] CLEARED
 *          posted [5] sprites done [6] MDONE seen [7] text done [8] publish done [9] TVINT as the
 *          slave reads it at [1] [15] the master's state when
 *          CLEARED was posted [19] (master) planes prepare end, the pass GO named then [22] DIAG[0] at [4] [23] DIAG[0] at [8]
 *  master: [10] band start [11] band end (MDONE) [12] TVINT at [10] [13] DIAG[0] at [10] [14] last
 *          tick end [16] last snapshot published [17] split [18] sprites drawn [20] last tick start
 *          [21] go of the band
 *  states: 1 tick 2 snapshot 3 CLEARED wait 4 band 5 planes prepare 6 idle */
#define R60_MBT       ((volatile uint16_t *)0x26029440u)
#define R60_MBTG      ((volatile uint16_t *)0x260295C0u)   /* [0] master state [1] master idle FRT [2] tick start [3] tick end [4] snapshot [5] slave span close */
static inline uint16_t r60_frt(void);
static __attribute__((noinline)) void r60_mbt(unsigned go, unsigned f) { R60_MBT[(go & 7u) * 24u + f] = r60_frt(); }
static __attribute__((noinline)) void r60_mbtv(unsigned go, unsigned f, unsigned v) { R60_MBT[(go & 7u) * 24u + f] = (uint16_t)v; }
static __attribute__((noinline)) void r60_mbtg(unsigned k) { R60_MBTG[k] = r60_frt(); }
#define MBT(go_, f_)      r60_mbt((go_), (f_))
/* the planes prepare in segments, summed (master FRT ticks): [0] calls that built [1] speech pump
 * [2] text plan/diff + palette mask + plan/gate [3] the build (walk + art) [4] the rest; at 0x295E0 */
#define R60_MBTS      ((volatile uint32_t *)0x260295E0u)
static uint16_t r60_mbts_t;
static __attribute__((noinline)) void r60_mbts(unsigned k) { uint16_t n = r60_frt(); if (k) R60_MBTS[k] += (uint16_t)(n - r60_mbts_t); else R60_MBTS[0]++; r60_mbts_t = n; }
#define MBTS(k_) r60_mbts((k_))
#define MBTV(go_, f_, v_) r60_mbtv((go_), (f_), (v_))
#define MBTG(k_)          r60_mbtg((k_))
#define MBST(st_)         (R60_MBTG[0] = (uint16_t)(st_))
/* the clock offset, measured: the slave spins on the master's vint stamp and takes its own FRT the
 * moment it changes, four times: [6+2k] slave FRT, [7+2k] the master's TVINT it saw (once, at boot) */
static __attribute__((noinline)) void r60_mbt_cal(void)
{
    unsigned k;
    for (k = 0; k < 4u; k++) {
        uint16_t tv = R60_TVINT, t2, s2;
        while ((t2 = R60_TVINT) == tv) ;
        s2 = r60_frt();
        R60_MBTG[6u + 2u * k] = s2; R60_MBTG[7u + 2u * k] = t2;
    }
}
#else
#define MBT(go_, f_)      ((void)0)
#define MBTV(go_, f_, v_) ((void)0)
#define MBTG(k_)          ((void)0)
#define MBST(st_)         ((void)0)
#define MBTS(k_)          ((void)0)
#endif
typedef struct {
    uint32_t *spans;            /* this core's span list for the bank being drawn */
    unsigned  n, cap, full;     /* its cursor, capacity, overflow */
    unsigned  cur;              /* the record index being drawn: tags its spans */
    unsigned  xo;               /* the canvas offset this pass */
    unsigned  rowpix;
    unsigned  pxs, pys, psel;   /* the FG scroll and page selects the punch resolves against */
    unsigned  ymin, ymax;       /* the rows the last punch hit */
    unsigned  master, pready;   /* master: read the mask cache, never write it; pready: the slave has initialised its tags */
    unsigned  pstat[4];         /* [0] sprites punched [1] cells tested [2] priority cells [3] rows written */
    uint16_t  box[4];           /* x0 x1 y0 y1 of the last draw */
    uint8_t   mbuf[8];          /* the master's mask scratch */
} r60_dc_t;
static r60_dc_t r60_dcs __attribute__((aligned(16)));   /* the slave's */
static r60_dc_t r60_dcm __attribute__((aligned(16)));   /* the master's */
#else
static uint16_t r60_rects_mem[2][2][64][4] __attribute__((aligned(16)));   /* [list][bank][i] x0 x1 y0 y1 */
#endif
#define R60_RECTS(bk)  ((volatile uint16_t *)((uint32_t)r60_rects_mem[0][(bk) & 1u] | 0x20000000u))
#define R60_MRECTS(bk) ((volatile uint16_t *)((uint32_t)r60_rects_mem[1][(bk) & 1u] | 0x20000000u))
#define R60_RECTN(bk)  (*(volatile uint16_t *)(0x26028F62u + ((bk) & 1u) * 2u))
#define R60_MRECTN(bk) (*(volatile uint16_t *)(0x26028F66u + ((bk) & 1u) * 2u))
#define R60_CLRSEQ    (*(volatile uint16_t *)0x26028F60u)   /* the pass it belongs to */
#define R60_SPLIT(seq) (*(volatile uint16_t *)(0x2603ED7Au + ((seq) & 1u) * 2u))   /* per snapshot slot (0x3ED7A/0x3ED7C; was 0x28ED2, inside md_dirty: see R60_MDONE) */
#ifndef R60_MBAND_F0
#define R60_MBAND_F0 144u        /* R60MBANDTUNE's start: 9/16 of the rows (the best static share, fight 2026-10-24) */
#endif
/* R60MBANDSTEAL: the band's claim. The master writes MSTART then reads SSTEAL; the slave, done with its
 * own rows and finding no claim, writes SSTEAL then reads MSTART. With both orders kept (uncached
 * words, in-order SH-2s) at least one side sees the other's write: if the slave sees no claim it
 * draws the band itself and the master, arriving, aborts; if it sees the claim it waits for MDONE and
 * draws the band only when MRESULT says the master aborted. MSTART/SSTEAL take R60_MB[7] (a census
 * max nothing reads); MRESULT the word after R60_MBAND_F. */
#define R60_MSTART    (*(volatile uint16_t *)0x2603ED1Cu)
#define R60_SSTEAL    (*(volatile uint16_t *)0x2603ED1Eu)
#define R60_MRESULT   (*(volatile uint16_t *)0x26028FDEu)   /* 1 drawn, 2 aborted (the slave took it) */
#define R60_MBDUR     (*(volatile uint16_t *)0x2603ED7Eu)   /* master -> slave: the last band's length, master FRT ticks (R60MBANDTUNE) */
#define R60_MBAND_F   (*(volatile uint16_t *)0x26028FDCu)   /* slave -> master: the master's share of the sprite rows, 1/256 (R60MBANDTUNE; after R60_PH_MWAIT, read zero) */
extern unsigned r60_planes_build(volatile uint16_t *pa, volatile uint16_t *pb,
                                 unsigned row0, unsigned win);
extern void     r60_planes_reset(void);
#endif

#if defined(R60_TEXT) && defined(R60_TXTPLANFAST)
/* THE TEXT DIFF FIRST, THE DEMAND ONLY WHEN THE TEXT MOVED (R60TXTPLANFAST, 2026-10-24; cart). The plan
 * scanned all 1,120 text cells for the pen demand and the diff scanned them again, every window the
 * master builds (~19 lines of the fight's prepare). Now: the diff's scan runs first, longword pairs
 * against its copy (it publishes after the plan, as before: the slave never sees a list before the
 * LUT it needs); the demand is rescanned only when the text may differ from what it was computed on.
 * r60_tx_dem_ok = the cached demand was computed on exactly what mcopy holds now (a diff ran after it
 * with no overflow), so a diff that finds nothing leaves it valid. Same demand, same list. */
static uint16_t r60_tx_mcopy[28u * 40u] __attribute__((aligned(4)));
static uint8_t  r60_tx_dem[8];
static unsigned r60_tx_dem_ok, r60_tx_skip, r60_tx_ran, r60_tx_n;
static __attribute__((noinline)) void r60_tx_scan(void)
{
    uint16_t seq = R60_TXTSEQ, nxt = (uint16_t)(seq + 1u);
    volatile uint16_t *lst = R60_TXTLIST(nxt);
    unsigned row, col, n = 0, ovf = 0;
    r60_tx_ran = seq == R60_TXTTAKEN;
    r60_tx_skip = 0;
    if (!r60_tx_ran) { r60_tx_dem_ok = 0; return; }   /* the slave has not taken the last list */
    for (row = 0; row < 28u; row++) {
        const volatile uint32_t *tw = (const volatile uint32_t *)(TEXT_UW + row * 64u + 24u);
        uint32_t *mw = (uint32_t *)(r60_tx_mcopy + row * 40u);
        for (col = 0; col < 40u; col += 2u) {
            uint32_t v = tw[col >> 1];
            if (v == mw[col >> 1]) continue;
            {   unsigned k;
                for (k = 0; k < 2u; k++) {
                    uint16_t d = (uint16_t)(k ? v : v >> 16);
                    unsigned ix = row * 40u + col + k;
                    if (d != r60_tx_mcopy[ix]) {
                        r60_tx_mcopy[ix] = d;
                        if (n < 256u) { lst[1 + n * 2] = (uint16_t)((row << 8) | (col + k)); lst[2 + n * 2] = d; n++; }
                        else { r60_tx_mcopy[ix] = 0xFFFFu; ovf = 1; }   /* list full: next window */
                    }
                }
            }
        }
    }
    r60_tx_n = n;
    r60_tx_skip = n == 0u && r60_tx_dem_ok;
    if (!r60_tx_skip) r60_tx_dem_ok = !ovf;     /* the demand computed next is on what mcopy holds (unless a cell overflowed) */
}
static __attribute__((noinline)) void r60_tx_demand(uint8_t *demand, const uint8_t *penmask)
{
    unsigned row, col, k;
    if (r60_tx_skip) { for (k = 0; k < 8u; k++) demand[k] = r60_tx_dem[k]; return; }
    for (row = 0; row < 28u; row++) {
        const volatile uint32_t *tw = (const volatile uint32_t *)(TEXT_UW + row * 64u + 24u);
        for (col = 0; col < 20u; col++) {
            uint32_t v = tw[col];
            if (!(v & 0x0FFF0FFFu)) continue;    /* both cells blank (code 0, pal 0) */
            for (k = 0; k < 2u; k++) {
                uint16_t d = (uint16_t)(k ? v : v >> 16);
                unsigned code = d & 0x1FFu, pl = ((unsigned)d >> 9) & 7u;
                if (!code && !pl) continue;
                demand[pl] |= penmask[code];
            }
        }
    }
    for (k = 0; k < 8u; k++) r60_tx_dem[k] = demand[k];
}
static __attribute__((noinline)) void r60_tx_publish(void)
{
    if (!r60_tx_ran) return;
    R60_TXTLIST((uint16_t)(R60_TXTSEQ + 1u))[0] = (uint16_t)r60_tx_n;
    R60_TXTSEQ = (uint16_t)(R60_TXTSEQ + 1u);
}
#endif
#ifdef R60_TEXT
/* STABLE GROUPS. A pal keeps its group while its colours stay the group's colours, and a
 * group whose only member cycles its colours keeps the group with the new colours (the
 * CRAM entries change, the pens do not). Rebuilding the map from scratch each window
 * moved every pal's group whenever one pal cycled ("INSERT COIN" flashes), and the slave
 * then redrew all 1,120 cells every pass: 185 lines (sw38). */
R60CODE static void r60_text_plan(void)
{
    /* FIXED GROUPS, computed, no tables: pals 0-3 own groups 0-3, pal 6 group 4, pal 7
     * group 5; pals 4 and 5 join a group whose colours equal theirs or draw nothing.
     * (Const tables read from this SDRAM-resident code returned the even neighbour at
     * odd word indices, sw52/sw53: index 3 read as index 2. Not yet understood; the
     * sprite plot reads its art as words and PLOT3/text as longwords.) Stable by
     * construction: the adaptive map thrashed and cells drew in each other's colours. */
    unsigned pal, g, c;
    for (pal = 0; pal < 8u; pal++) {
        unsigned grp = pal < 4u ? pal : (pal == 6u ? 4u : (pal == 7u ? 5u : 0xFFu));
        R60_TXTGRP[pal] = (uint16_t)grp;
    }
    for (g = 0; g < 6u; g++) R60_TXTPAL[g] = (uint16_t)(g < 4u ? g : g + 2u);
    for (pal = 4; pal < 6u; pal++) {
        for (g = 0; g < 6u; g++) {
            unsigned gp = g < 4u ? g : g + 2u;
            for (c = 1; c < 8u; c++) if (PAL_SH[pal * 8u + c] != PAL_SH[gp * 8u + c]) break;
            if (c == 8u) { R60_TXTGRP[pal] = (uint16_t)g; break; }
        }
    }
#ifdef R60_TXTPOOL
    {   /* THE POOL, keyed by COLOUR (2026-10-16 evening). Groups are gone: every pal is its
         * own LUT row, and a slot holds one colour word shared by every (pal, pen) that wants
         * it (pals 5 and 6 share four, the 7fff highlights are one slot: the high-score table
         * asks ~27 distinct colours of the 31). A pair keeps its slot while its colour stays;
         * a pair whose colour changes recolours its slot in place when it is the only user
         * (the "INSERT COIN" cycle rewrites CRAM, not the LUT), else moves. */
        static uint8_t penmask[512], pm_init, pair_slot[64], users[31];
        static uint16_t slotcol[31], lut_shadow[64];
        uint8_t demand[8];
        unsigned row, col, s, pr, changed = 0;
#ifdef R60_TXTLUTPAL
        unsigned lutpals = 0;
#endif
        if (!pm_init) {
            unsigned c2, k;
            for (c2 = 0; c2 < 512u; c2++) { unsigned m = 0; for (k = 0; k < 64u; k++) m |= 1u << (altbeast_tiles[c2 * 64u + k] & 7u); penmask[c2] = (uint8_t)m; }
            for (s = 0; s < 31u; s++) { users[s] = 0; slotcol[s] = 0xFFFEu; }
            for (pr = 0; pr < 64u; pr++) { pair_slot[pr] = 0xFFu; lut_shadow[pr] = 0; }
            pm_init = 1;
        }
        for (pal = 0; pal < 8u; pal++) { R60_TXTGRP[pal] = (uint16_t)pal; demand[pal] = 0; }   /* identity: pal = LUT row */
        for (g = 0; g < 8u; g++) R60_TXTPAL[g] = (uint16_t)g;
#ifdef R60_TXTPLANFAST
        r60_tx_demand(demand, penmask);          /* cart: the cached demand when the text is unchanged */
        (void)row; (void)col;
#else
        for (row = 0; row < 28u; row++)
            for (col = 0; col < 40u; col++) {
                uint16_t d = TEXT_UW[row * 64u + 24u + col];
                unsigned code = d & 0x1FFu, pl = ((unsigned)d >> 9) & 7u;
                if (!code && !pl) continue;
                demand[pl] |= penmask[code];
            }
#endif
        /* 1. release pairs no longer demanded (their slot keeps its colour for a return) */
        for (pr = 0; pr < 64u; pr++) {
            unsigned pl = pr >> 3, c = pr & 7u;
            if (pair_slot[pr] != 0xFFu && (c == 0u || !((demand[pl] >> c) & 1u))) { if (users[pair_slot[pr]]) users[pair_slot[pr]]--; pair_slot[pr] = 0xFFu; }
        }
        /* 2. demanded pairs: keep, recolour in place, join a slot of the colour, take a free one */
        for (pr = 0; pr < 64u; pr++) {
            unsigned pl = pr >> 3, c = pr & 7u, want, cur;
            if (c == 0u || !((demand[pl] >> c) & 1u)) continue;
            want = PAL_SH[pl * 8u + c]; if (want == 0xFFFEu) want = 0xFFFFu;
            cur = pair_slot[pr];
            if (cur != 0xFFu && slotcol[cur] == want) continue;
            if (cur != 0xFFu && users[cur] == 1u) { slotcol[cur] = (uint16_t)want; continue; }
            if (cur != 0xFFu) { users[cur]--; pair_slot[pr] = 0xFFu; }
            for (s = 0; s < 31u; s++) if (users[s] && slotcol[s] == want) break;            /* share */
            if (s == 31u) for (s = 0; s < 31u; s++) if (!users[s] && slotcol[s] == want) break;   /* a free slot that still holds it */
            if (s == 31u) for (s = 0; s < 31u; s++) if (!users[s]) break;                   /* any free slot */
            if (s < 31u) { slotcol[s] = (uint16_t)want; users[s]++; pair_slot[pr] = (uint8_t)s; }
            /* else: full; the LUT below borrows the nearest colour */
        }
        for (s = 0; s < 31u; s++) {
            uint16_t nv = users[s] ? slotcol[s] : 0xFFFEu;
#ifdef R60_CRAMMASK
            if (R60_POOLCOL[s] != nv) R60_POOLDIRTY = 1;
#endif
            R60_POOLCOL[s] = nv;
        }
        for (pr = 0; pr < 64u; pr++) {
            unsigned pl = pr >> 3, c = pr & 7u, byte = 0;
            if (c && ((demand[pl] >> c) & 1u)) {
                if (pair_slot[pr] != 0xFFu) byte = R60_POOLBYTE(pair_slot[pr]);
                else {                                      /* borrowed: nearest allocated colour */
                    unsigned want = PAL_SH[pl * 8u + c], best = 0xFFFFFFFFu;
                    for (s = 0; s < 31u; s++) if (users[s]) {
                        unsigned cw = slotcol[s], dd = 0, ch;
                        for (ch = 0; ch < 3u; ch++) { int a = (int)((want >> (ch * 4u)) & 15u) - (int)((cw >> (ch * 4u)) & 15u); dd += (unsigned)(a * a); }
                        if (dd < best) { best = dd; byte = R60_POOLBYTE(s); }
                    }
                }
            }
            if (lut_shadow[pr] != byte) { lut_shadow[pr] = (uint16_t)byte; changed = 1;
#ifdef R60_TXTLUTPAL
                lutpals |= 1u << pl;
#endif
            }
            R60_TXTLUT[pr] = (uint16_t)byte;
        }
        if (changed) R60_TXTLUTSEQ++;
#ifdef R60_TXTLUTPAL
        if (changed) R60_TXTLUTPM = ((uint32_t)R60_TXTLUTSEQ << 16) | lutpals;
#endif
    }
#endif
}

#ifndef R60_TXTPLANFAST
R60CODE static void r60_text_diff(void)
{
    static uint16_t mcopy[28u * 40u];
    uint16_t seq = R60_TXTSEQ, nxt = (uint16_t)(seq + 1u);
    volatile uint16_t *lst = R60_TXTLIST(nxt);
    unsigned row, col, n = 0;
    if (seq != R60_TXTTAKEN) return;             /* the slave has not taken the last list */
    for (row = 0; row < 28u; row++)
        for (col = 0; col < 40u; col++) {
            uint16_t d = TEXT_UW[row * 64u + 24u + col];
            if (d != mcopy[row * 40u + col]) {
                mcopy[row * 40u + col] = d;
                if (n < 256u) { lst[1 + n * 2] = (uint16_t)((row << 8) | col); lst[2 + n * 2] = d; n++; }
                else { mcopy[row * 40u + col] = 0xFFFFu; }   /* list full: next window */
            }
        }
    lst[0] = (uint16_t)n;
    R60_TXTSEQ = nxt;
}
#endif
#endif

#ifndef MBT   /* the R60MBTIME stamps compile away outside MBAND */
#define MBT(go_, f_)      ((void)0)
#define MBTV(go_, f_, v_) ((void)0)
#define MBTG(k_)          ((void)0)
#define MBST(st_)         ((void)0)
#define MBTS(k_)          ((void)0)
#endif
#if defined(R60_PLANES) && defined(R60_SLAVEWIN)
unsigned r60_bulk_hold; /* in-place picture load: preserve the existing pen plan */
__attribute__((noinline)) static unsigned r60_live_artcap(unsigned cap)
{   /* A fast pan needs an entering strip's art within the same window. */
    static uint32_t prev_scroll;
    uint32_t scroll = ((uint32_t)TEXT_UW[0x74Cu] << 16) | TEXT_UW[0x74Du];
    int da = (int)(((scroll >> 16) - (prev_scroll >> 16)) & 0x3FFu);
    int db = (int)((scroll - prev_scroll) & 0x3FFu);
    if (da > 512) da -= 1024;
    if (db > 512) db -= 1024;
    if (!R60_DISPHOLD && (da >= 8 || da <= -8 || db >= 8 || db <= -8)) cap = 160u;
    prev_scroll = scroll;
    return cap;
}

R60CODE static void r60_planes_prepare(void)
{
    MBTS(0);
#if defined(R60_SPEECH) && !defined(R60_SPEECHSLAVE)
    /* sw243: the speech/SFX decode rides the master here (once a vint from m_main, and under the
     * hold's idle loop). An opaque call inside m_main's own loop grew m_main by 860 bytes (every
     * loop-carried value reloaded around it) and .ramtext overflowed; at the top of this function,
     * before its loops, it costs one call. The slave's DMA IRQ only swaps sides and raises a flag. */
    speech_pump();
#endif
    MBTS(1);
#ifdef R60_EVPROBE
    r60_evp(11u);                               /* PROBE: speech pump done (master code 11) */
#endif
    static unsigned row0, win;
    uint16_t seq = R60_PKTSEQ, nxt = (uint16_t)(seq + 1u);
    volatile uint16_t *sa = R60_PKTSTAGE(nxt, 0), *sb = R60_PKTSTAGE(nxt, 1);
#ifdef R60_HOLDDIRECT
    unsigned direct = 0;
#endif
    /* BUILD ONLY WHEN THE LAST PACKET HAS BEEN TAKEN. The walk updates its shadow as it
     * builds, so a window the slave never copied (a two-vint frame) was lost for good:
     * whole name-table rows stayed empty on the title (sw26: NT A 576 of 800, rows 17-26)
     * until the arcade changed them. */
    r60_census[5]++;                             /* census: prepare entries (sw193 probe) */
    if (seq != R60_PKTDONE) { R60_DIAG2[15]++; return; }
#ifdef R60_HOLDDIRECT
    /* THE DIRECT ABORT COMES FIRST (sw178). It sat below the text diff, the window count, the plan
     * and the gate, so every attempt the slave refused still advanced GATE_SINCE, the holes ring
     * and the quiet count: the hold released on windows that built nothing (sw177 demo 2: twelve
     * aborted attempts in one hold, release with rows 17-20 never filled). SH-2 framebuffer stores
     * at FM=0 are dropped (sw169): build only while the slave holds FM, and tell it so first (sw170
     * tore packets when its wait ran out). The FM bit itself is read last: a flag can lag it. */
    direct = (R60_DISPHOLD == 1u) && R60_HOLDFLIP;
#ifdef R60_STAGEDIRECT
    if (r60_stage_full) {                        /* a packet is built: land it the moment FM is up, nothing else this call */
        unsigned n = (r60_stage_words + 1u) / 2u, i;
        volatile uint32_t *d = (volatile uint32_t *)((r60_stage_nxt & 1u) ? R60_PKT_A_ALTW : R60_PKT_A_DW);
        const volatile uint32_t *src = (const volatile uint32_t *)R60_STAGE_DIRECT;
        R60_BUILDING = 1;
        if (R60_FMDOWN || R60_FMSOON || !R60_FM_IS_UP) { R60_BUILDING = 0; return; }
        if (n > R60_PKTA_CAP_STAGE / 2u + 8u) n = R60_PKTA_CAP_STAGE / 2u + 8u;
#ifdef R60_HTPROBE
        {   volatile uint16_t *ht = (volatile uint16_t *)((uint32_t)r60_ht_mem | 0x20000000u);
            ht[1]++; if (!(R60_DISPHOLD == 1u && R60_HOLDFLIP)) ht[0]++; }
#endif
        for (i = 0; i < n; i++) d[i] = src[i];
        R60_PKTA_LEN[r60_stage_nxt & 1u] = r60_stage_words | R60_PKTA_DIRECT;
        if (r60_stage_nxt & 1u) {                /* the odd packet sits at 0x11800: a stub at 0x17000 points the 68K there */
            volatile uint16_t *st = R60_PKT_A_DW, *sa2 = (volatile uint16_t *)d;
            st[1] = (uint16_t)(sa2[1] | 0x0800u); st[2] = sa2[2]; st[3] = sa2[3]; st[4] = sa2[4]; st[5] = 0; st[6] = sa2[6]; st[7] = sa2[7];
            st[0] = 0xB6B6u;
        }
        if (R60_FMDOWN || !R60_FM_IS_UP) R60_TORN_M++;   /* FM fell under the copy */
        R60_PKTSEQ = r60_stage_nxt;
        R60_BUILDING = 0;
        r60_stage_full = 0;
        R60_DIAG[7]++;
        r60_stage_tcopy = R60_DIAG[0];
        return;
    }
#else
    if (direct) {
        R60_BUILDING = 1;
        if (R60_FMDOWN || R60_FMSOON || !R60_FM_IS_UP) { R60_BUILDING = 0; return; }
    }
#endif
#endif
#ifdef R60_TEXT
#ifdef R60_TXTPLANFAST
    r60_tx_scan();
    r60_text_plan();
    r60_tx_publish();
#else
    r60_text_plan();
    r60_text_diff();
#endif
#endif
    win++;
    /* THE PLAN AND THE GATE. A plan adopted mid-fill flushes the cache and restarts the
     * fill (sw78: plans 5 -> 6 at ~f495, holes 94 -> 878, the hold ran to f530). So: no
     * periodic plan while held; one plan four windows into a hold (the game has loaded
     * the scene behind its blank by then; sets that appear later pack on demand without
     * a flush); and none for 32 windows after release. */
#ifdef R60_CRAMMASK
    {   /* WHICH SPRITE COLOUR SETS CHANGED: the master reads its own palette cached and hands
         * the slave a bank mask, so the commit compares only those (it compared all 256
         * entries uncached every pass: ~10 lines). */
        static uint16_t pal_m[256]; static unsigned pal_init;
        const uint16_t *pc = (const uint16_t *)0x06027000u + 1024u;
        unsigned b, q, m = 0;
        if (!pal_init) { for (b = 0; b < 256u; b++) pal_m[b] = 0xFFFEu; pal_init = 1; }
        for (b = 0; b < 16u; b++) {
#ifdef R60_PALMAP
            unsigned pl = R60_BANKPAL_U[b]; if (pl > 63u) pl = b;
#else
            unsigned pl = b;
#endif
            for (q = 0; q < 16u; q++) { uint16_t v = pc[pl * 16u + q]; if (v != pal_m[b * 16u + q]) { pal_m[b * 16u + q] = v; m |= 1u << b; } }
        }
        if (m) R60_CRAMMASK_W |= (uint16_t)m;
    }
#endif
    {
        static unsigned plan_quiet, plan_hard_quiet, plan_hard_wait, plan_hard_last;
        extern unsigned r60_unlined;
        if (plan_hard_quiet) plan_hard_quiet--;
        unsigned held = R60_DISPHOLD && !r60_bulk_hold;
        { extern void r60_pix_track(void); r60_pix_track(); }   /* 2026-10-25: per (set, pixel) colour stability, every window (r60_planes.c) */
#ifndef R60_NORECOLOUR
        /* sw283: ran only under R60_PENIDX, which no candidate carries, so no pen was ever recoloured or split on the line (the crystal ball's one-pen picture, Mike's sw282.bs3: DIAG2[13] = 0). */
        if ((win & 3u) == 2u) { extern void r60_recolour_pens(void); r60_recolour_pens(); }   /* a palette write is a CRAM write, not new art; every 4th window: every window cost 9% of the windows and 4 frames at the cuts (sw283) */
#endif
#ifdef R60_HOLDDIRECT
        /* THE PLAN COMES FIRST (sw172): with the direct packets a cut's art is in by window 2-3,
         * and the plan at window 4 re-marked every slot it had moved -- the eye's name tables
         * emptied from 1126 to 602 real cells after the plan and never refilled with the display
         * up (sw171). One window after the game's enable the pages are loaded; plan then. */
        /* FILL ON THE GAME'S BLANK (sw187). The loads write every page before they start waiting
         * (builder: clear -> tileram -> palettes -> scene setup -> set bit 5), and with the loads
         * paced to the arcade's 21 frames the pages sit complete behind the blank for ~17 of them.
         * So the plan runs every window of the blank (a partial page plans partially; the next
         * window's plan corrects it, and a flush behind the blank costs nothing visible) and the
         * direct fill runs with it; the plan at the first window after enable is the one that
         * must find nothing to change. */
        if (held) { if (R60_GATE_SINCE == 1u || (R60_GATE_SINCE == 0u && (win & 3u) == 0u)) { uint32_t _f1 = R60_DIAG[0]; r60_line_plan(); r60_census[6] += (uint16_t)(R60_DIAG[0] - _f1); } plan_quiet = 32u; }   /* sw195: a plan is 1-2 vints (census [6]); every fourth blank window and the first after enable */
#else
        if (held) { if (R60_GATE_SINCE == 4u) r60_line_plan(); plan_quiet = 32u; }
#endif
        else if (plan_quiet) plan_quiet--;
        else if (r60_unlined >= 16u && !plan_hard_quiet) {
            /* BACKOFF: a hard plan that did not lower the unlined count could not help (the scene uses
             * more sets than four lines hold, the splash's 800 tiles): wait twice as long each time, up
             * to 256 windows; one that helped resets to 8. The attract soak fired 604 hard plans in 30k
             * vints without it (sw276), none of them moving a gate. */
            plan_hard_quiet = (plan_hard_last && r60_unlined >= plan_hard_last) ? (plan_hard_wait < 256u ? plan_hard_wait * 2u : 256u) : 8u;
            plan_hard_wait = plan_hard_quiet; plan_hard_last = r60_unlined;
            r60_line_plan(); R60_DIAG2[14]++;
        }   /* sw276: A SCENE THAT ARRIVES AFTER ITS BLANK. The crystal-ball
                                                                          * picture (page 5, 254 cells, colour sets 72-75) is written in the frame the game re-enables
                                                                          * the display, so the blank's plans never see its sets, the soft plan refuses to line them,
                                                                          * and the walk skipped every picture cell for the whole scene (Mike's sw275.bs2: 578 free
                                                                          * slots, 0 holding a picture code, a flat magenta disc). 16 unlined visible cells in one
                                                                          * window = a set that is on screen and has no line: plan hard once, then wait 8 windows. */
        else if ((win & 31u) == 1u) { extern unsigned r60_plan_soft; r60_plan_soft = 1u; r60_line_plan(); r60_plan_soft = 0u; }   /* sw183: with the display up a plan that would move lines is refused (the eye pan f1392: ten frames of wrong palette lines) */
        r60_unlined = 0;                         /* counted afresh by this window's walk */
    }
    {   /* the display gate, decided before the packet so bit 13 rides this window */
        /* HOLES ON SCREEN = the cells blanked for want of art over the last five windows
         * (a row is rewritten every fifth window, so a blank written at window w stands
         * until w + 5). Release when that sum is under R60_HOLD_HOLES after one rotation.
         * "No blanks this window" never came in a scrolling scene (sw75/sw76 held 70+
         * frames): the leading edge blanks a few cells every window; the cut blanks
         * hundreds. */
        /* The first prepare can run after the game's first video enable. Start held
         * so boot gets the same completed-fill gate as subsequent blanked scenes. */
        static unsigned hold = 1u, rej_last, ring[5], ri, quiet, hold_prev;
#define since R60_GATE_SINCE
        unsigned ven = R60_VIDEO_EN & 0x20u;
#ifdef PORT_FULL
        extern volatile unsigned port_tile_load_serial;
        static unsigned load_seen, load_hold;
        /* The attract card/logo blitter replaces pictures without clearing video
         * enable. Treat its completed bulk write as a scene boundary too. Cover
         * all 32 physical rows before the quiet test can release this hold. */
        if (load_seen != port_tile_load_serial) {
            load_seen = port_tile_load_serial;
            /* An existing blank/hold already owns its fill and release timing. */
            if (ven && !hold) {
                hold = 1u; since = 0; quiet = 0; load_hold = 1u;
                r60_bulk_hold = 1u;
            }
        }
#endif
        unsigned rej = (unsigned)R60_DIAG2[7], holes, k, dlast = rej - rej_last;
        ring[ri] = dlast; ri = (ri + 1u) % 5u; rej_last = rej;
        for (holes = 0, k = 0; k < 5u; k++) holes += ring[k];
        R60_GATE_HOLES = holes;
#ifdef R60_HOLDDIRECT
        /* RELEASE ON A QUIET ROTATION (sw180). The five-window sum of records built (R60_HOLD_HOLES)
         * remembered the fill itself: sw178 demo 2 built 307+243+177+134 records in four windows,
         * was done at window 5, and held until the sum had forgotten them at window 11 -- twelve
         * frames of black past the fill. A 14-row window that builds under R60_HOLD_QUIETN records
         * found its rows right; sw187: one window after enable for its plan; sw191: 16-row windows,
         * two quiet = all 32 rows.
         * QUIET COUNTS THROUGH THE BLANK (sw245). It used to reset every window while the game's
         * display was off, so a cut whose art had landed well before the enable still waited two
         * windows after it: title->demo art done at 460, enable 463, release 468 (arcade black 21
         * frames, ours 26); eye->demo 2 done 1492, enable 1500, release 1503. Now a window that
         * builds under R60_HOLD_QUIETN records counts whether or not the display is on, so a
         * finished fill releases at the first window after the enable (since 1); a landing in the
         * enable frame still resets it (463 built 57) and the wait is as before. */
        /* sw247: the quiet count is RESET through the blank again. sw245/246 carried it across
         * the enable and released at the first window after it; at face->eye and the demo-2 end
         * the game writes the scene AFTER enabling (its own picture is black for 3 frames: the
         * palette), so the fill showed instead of black (rows 20-31 in flight at 1171). The two
         * cuts whose art lands early (title->demo, eye->demo 2) keep paying two windows. */
        if (!ven) { hold = 1; since = 0; quiet = 0; }
        else if (hold) {                 /* sw248: quiet is counted ONLY in windows with the display on (sw247 counted the
                                          * blank's own windows right after zeroing them: quiet 1 at the enable, release at
                                          * the first window, face->eye showed the fill again) */
            extern unsigned r60_plan_adopted;
            quiet = dlast < R60_HOLD_QUIETN ? quiet + 1u : 0u;
            /* A PLAN THAT RE-SLOTTED THIS WINDOW IS NOT QUIET (sw246). Its records land in the NEXT
             * windows. The plan runs just above, before this decision. */
            if (r60_plan_adopted) { quiet = 0; r60_plan_adopted = 0; }
            since++;
            unsigned min_windows = 1u;
#ifdef PORT_FULL
            if (load_hold) min_windows = 4u;
#endif
            if ((since >= min_windows && quiet >= 2u) || since >= R60_HOLD_MAX) {
                hold = 0; R60_GATE_REL++;
#ifdef PORT_FULL
                load_hold = 0; r60_bulk_hold = 0;
#endif
            }
        }
#else
        if (!ven) { hold = 1; since = 0; quiet = 0; }
        else if (hold) {
            since++;
            quiet = holes < R60_HOLD_HOLES ? quiet + 1u : 0u;   /* three quiet windows in a row: one dip is a full packet, not a whole screen (sw77 released at ~485 into litter) */
            if ((since >= 4u && quiet >= 2u) || since >= R60_HOLD_MAX) { hold = 0; R60_GATE_REL++; }   /* sw168: a 14-row rotation covers the screen in two windows; was 8 and 3 for the 7-row sweep */
        }
#endif
        if (hold) R60_GATE_HELD++;                   /* windows held */
        /* the 68K sees bit 13 one vint after the packet is built; the slave sees this word
         * at once, so it stays blank one window longer or the sprites lead the planes by
         * a frame (sw79 f490: sprites over black) */
        R60_MDHOLD = (uint16_t)hold;     /* the MD display stays off under our hold: what the 32X has not painted yet is black, not the MD's half-filled planes (sw98 showed magenta there); the sw93 slowdown was the scene's own sprite load, not the display */
        /* the slave's mode: 1 = the game's own blank, 2 = our hold (the 32X paints the planes), 0 = MD planes */
#ifdef R60_FILL32
        R60_DISPHOLD = (uint16_t)(!ven ? 1u : ((hold | hold_prev) ? 2u : 0u)); hold_prev = hold;
#else
        R60_DISPHOLD = (uint16_t)(!ven ? 1u : ((hold | hold_prev) ? 1u : 0u)); hold_prev = hold;   /* without the 32X fill our hold blanks like the game's */
#endif
#undef since
    }
#if defined(R60_HOLDDIRECT) && defined(R60_STAGEGATE)
    /* A DIRECT BUILD ONLY WHILE THE HOLD STILL STANDS (R60STAGEGATE, 2026-10-24). `direct` is decided
     * at the top of this call from the LAST window's gate; the gate just above may have released the
     * hold. A direct packet built now is staged, and the next call copies it into the framebuffer with
     * the display up and the slave out of the hold handshake, so the master's ~2000 uncached FB stores
     * run beside the slave's pass (sprite stores, the fill-engine clears, CRAM). On the MiSTer both
     * SH-2s share one VDP path with no in-flight guard (IF.sv:974-983) and the overlapped stores are
     * lost: the packet that finishes the cut lands torn (rig: green eye with garbage rows, torn
     * title). ares has no such loss and shows nothing. Probe R60HTPROBE counted these copies at
     * every cut on both machines (~1 per 10 s of attract). Build this window the normal way. */
    if (direct && !(R60_DISPHOLD == 1u && R60_HOLDFLIP)) direct = 0;
#endif
    r60_artcap = (R60_DISPHOLD == 1u) ? R60_FATCAP : (R60_DISPHOLD == 2u) ? 24u : 40u;
    r60_artcap = r60_live_artcap(r60_artcap);
#ifdef R60_CHURNCAP
    {   /* CHURN: a window that rejected many cells (a scene's tiles rewritten, the title logo
         * turning red) gets a fat packet next window even with the display up: the picture is
         * churning anyway, so trading cadence for art lands the new scene in 3 windows, not 10 */
        static unsigned rej_prev;
        unsigned rej = R60_DIAG2[7], d = rej - rej_prev; rej_prev = rej;
        if (R60_DISPHOLD == 0u && d >= R60_CHURNMIN) r60_artcap = R60_CHURNCAP;
    }
#endif   /* FATCAP = a FAT window under the blank hold (the slave idles, the 68K takes the frame); 24 while the 32X paints */
#ifdef R60_HOLDDIRECT
    {   extern unsigned r60_pkta_cap, r60_artmax;
#ifdef R60_STAGEDIRECT
        if (direct) { sa = R60_STAGE_DIRECT; r60_pkta_cap = R60_PKTA_CAP_STAGE; r60_artmax = R60_ARTMAX_STAGE; if (r60_artcap < R60_ARTMAX_STAGE) r60_artcap = R60_ARTMAX_STAGE; }
#else
        if (direct) {                            /* decided at the top of this function (sw178) */ sa = (nxt & 1u) ? R60_PKT_A_ALTW : R60_PKT_A_DW; r60_pkta_cap = R60_PKTA_CAP_DIRECT; r60_artmax = R60_ARTMAX_DIRECT; if (r60_artcap < R60_ARTMAX_DIRECT) r60_artcap = R60_ARTMAX_DIRECT; }
#endif
        else { r60_pkta_cap = 3072u; r60_artmax = 160u; }
    }
#endif
    MBTS(2);
#ifdef R60_EVPROBE
    r60_evp(12u);                               /* PROBE: text plan/diff + gate done, the walk starts (master code 12) */
#endif
    { uint32_t _f0 = R60_DIAG[0];
      R60_DIAG2[6] = r60_planes_build(sa, sb, row0, win);
      MBTS(3);
#ifdef R60_EVPROBE
      r60_evp(13u);                             /* PROBE: the walk done (master code 13) */
#endif
      R60_BUILD_T = (uint16_t)(R60_DIAG[0] - _f0); if (R60_BUILD_T > R60_BUILD_TMAX) R60_BUILD_TMAX = R60_BUILD_T;   /* census: the walk + art of this window in VINTS (the FRT form wrapped at 1.36 frames, sw193) */
      r60_census[7] += R60_BUILD_T; }
    { extern unsigned r60_walk_rows; row0 = (row0 + r60_walk_rows - 7u) & 31u; }   /* the rotation advances by the rows walked (+7 below) */
    { extern unsigned r60_pkta_words, r60_pktb_words; R60_PKTA_LEN[nxt & 1u] = r60_pkta_words; R60_PKTA_LEN[2u + (nxt & 1u)] = r60_pktb_words; }   /* per parity: the slave copies seq while the master builds nxt */
#ifdef R60_HOLDDIRECT
#ifdef R60_STAGEDIRECT
    if (direct) {
        extern unsigned r60_pkta_words;
        R60_DIAG2[7] += R60_DIAG2[6];
        row0 = (row0 + 7u) & 31u;
        sa[0] = 0xB6B6u; sb[0] = 0xB6B6u;        /* both markers in staging; PKTSEQ moves when the copy lands */
        r60_stage_words = r60_pkta_words; r60_stage_nxt = nxt; r60_stage_full = 1;
        r60_stage_tbuilt = R60_DIAG[0];
        r60_census[7] += (uint16_t)(R60_DIAG[0] - r60_stage_tcopy);     /* frames from the last copy to this build's end (publish + span + build) */
        return;
    }
#endif
    if (direct) {
        R60_PKTA_LEN[nxt & 1u] |= R60_PKTA_DIRECT;
        if (nxt & 1u) {                          /* the odd packet sits at 0x11800: a stub at 0x17000 points the 68K there */
            volatile uint16_t *st = R60_PKT_A_DW;
            st[1] = (uint16_t)(sa[1] | 0x0800u); st[2] = sa[2]; st[3] = sa[3]; st[4] = sa[4]; st[5] = 0; st[6] = sa[6]; st[7] = sa[7];
            st[0] = 0xB6B6u;
        }
    }
#endif
    R60_DIAG2[7] += R60_DIAG2[6];                /* total art records built (probe 2026-10-15) */
    row0 = (row0 + 7u) & 31u;
#ifdef R60_HOLDDIRECT
    if (direct && (R60_FMDOWN || !R60_FM_IS_UP)) R60_TORN_M++;   /* FM fell under a direct build: its stores were dropped and its rows are in the shadow (sw177 seq 872, rows 17-30 of demo 2 lost for the scene) */
#endif
    sa[0] = 0xB6B6u; sb[0] = 0xB6B6u;
    R60_PKTSEQ = nxt;
#ifdef R60_HOLDDIRECT
    R60_BUILDING = 0;
#endif
    R60_DIAG[7]++;
}
#endif

#if defined(R60_PLANES) && defined(R60_SLAVEWIN)
/* The row packets use world coordinates. Refresh their viewport together at the
 * last FM-up publication, rather than carrying the prepare-time camera lag. */
__attribute__((noinline)) static void r60_refresh_viewport(void)
{
    volatile uint16_t *a = R60_PKT_A, *b = R60_PKT_B;
    uint16_t ha = (TEXT_UW[0x74Cu] - 0xC0u) & 0x3FFu;
    uint16_t hb = (TEXT_UW[0x74Du] - 0xC0u) & 0x3FFu;
    uint16_t va = TEXT_UW[0x748u] & 0xFFu, vb = TEXT_UW[0x749u] & 0xFFu;
    if (a[3] != ha || a[7] != hb || a[6] != va || a[4] != vb) {
        a[1] &= ~0x1000u; b[1] &= ~0x1000u;
        a[3] = b[3] = ha; a[7] = b[7] = hb;
        a[6] = b[6] = va; a[4] = b[4] = vb;
    }
}
#endif

R60CODE static void r60_publish_planes(void)
{
#if defined(R60_PLANES) && defined(R60_SLAVEWIN)
    {   /* the slave: copy the staged packets into the framebuffer, 2 x 376 longwords.
        * 368 (736 words) cut the palette block, words 688-751, at word 736: MD line 3
        * read all zero (sw66, sw68 CRAM dumps) and every set packed there drew black
        * (the demo's grass, sw68). The staging is 768 words a packet. */
        uint16_t seq = R60_PKTSEQ;
        const volatile uint32_t *sa = (const volatile uint32_t *)R60_PKTSTAGE(seq, 0);
        const volatile uint32_t *sb = (const volatile uint32_t *)R60_PKTSTAGE(seq, 1);
        volatile uint32_t *da = (volatile uint32_t *)R60_PKT_A, *db = (volatile uint32_t *)R60_PKT_B;
        unsigned i;
        if (seq != R60_PKTDONE && R60_PKT_A_U[0] == 0xB6B6u) R60_DIAG[10]++;   /* a NEW packet lands on one the 68K has not taken (probe 2026-10-15) */
        { unsigned la = R60_PKTA_LEN[seq & 1u], na, nbw = (R60_PKTA_LEN[2u + (seq & 1u)] + 1u) / 2u;   /* the slave copies: uncached, per parity, exact lengths */
#ifdef R60_HOLDDIRECT
          if (la & R60_PKTA_DIRECT) na = 0; else
#endif
          { na = (la + 1u) / 2u; if (na < 8u) na = 8u; if (na > 1536u) na = 1536u; }
          if (nbw < 8u) nbw = 8u; if (nbw > 376u) nbw = 376u;
          for (i = 0; i < na; i++) da[i] = sa[i];
          for (i = 0; i < nbw; i++) db[i] = sb[i];
          if (!R60_DISPHOLD
#ifdef R60_HOLDDIRECT
              && !(la & R60_PKTA_DIRECT)
#endif
          ) r60_refresh_viewport();
        }
        R60_PKTDONE = seq;
        return;
    }
#endif
    volatile uint16_t *sc = R60_PKT_A;
#ifdef R60_PLANES
    /* TWO PACKETS, WHICH IS WHAT THE 68K ALREADY CONSUMES: A carries the tile ART as a
     * type-0 batch, B the name table as a type-1 NT_WRAP chunk plus the palette. Seven
     * plane rows a frame, so a 28-row screen refreshes in four. */
    {
        static unsigned r60_row0, r60_win;
        volatile uint16_t *pb = R60_PKT_B;
        unsigned nart;
        r60_win++;
        nart = r60_planes_build(sc, pb, r60_row0, r60_win);
        { extern unsigned r60_walk_rows; r60_row0 = (r60_row0 + r60_walk_rows) & 31u; }

        pb[0] = 0xB6B6u;                 /* LAST: headers, rows, tails, palette are in */
        sc[0] = 0xB6B6u;
        R60_DIAG[7]++;
        R60_DIAG2[6] = nart;
        return;
    }
#endif
    unsigned r, w = 8;
    sc[1] = 0x0001u;                 /* type 1: name-table, no palette, no hold */
    sc[2] = 0x8000u;                 /* plane A */
    sc[3] = 0; sc[4] = 0; sc[5] = 0; sc[6] = 0; sc[7] = 0;
    for (r = 0; r < 7u; r++) {
        sc[w++] = (uint16_t)(r << 8); /* plane row r, column 0 */
        sc[w++] = 0;                  /* start 0, count 0 */
    }
    sc[w++] = 0;                      /* hscroll delta tail: no entries */
    sc[0] = 0xB6B6u;                  /* LAST: the only completion signal */
    R60_DIAG[7]++;
}


/* ONE SPRITE, straight into the hidden bank. P1 reads the S16 4bpp art as it stands; the
 * baked 8-bit form with the pen base folded in is the next build, and this is the control
 * it will be measured against. */
static uint32_t *r60_mask;              /* the mask for the bank being drawn */
#ifdef R60_TEXT
static uint32_t cleared_rows[7];         /* the mask at pass start: rows the clear wiped */
#ifdef R60_ROWCLR
/* ROWCLR (2026-10-17): the clear of a bank's old sprites cost 57 lines a pass in the demo
 * (event ring: PASS_START +21 -> CLEAR_DONE +78): 64 x 64 boxes zeroed in full, 9 cycles a
 * longword store, for sprites ~40% opaque. Each drawn row records its own span instead,
 * from its first group to its end marker, and the clear zeroes exactly those spans. */
/* the span a row may have touched: first group (clamped to the row start) to its end x */
#ifdef R60_KEEPSPR
/* KEEPSPR (2026-10-17): a sprite whose record is the one this bank drew last time, and
 * whose box no changed sprite's old box, no earlier changed sprite's new box and no pending
 * text cell touches, keeps its pixels: its spans are neither cleared nor redrawn, and are
 * carried into the bank's next span list. The demo's logo and idle figures are such. */
static uint16_t ks_rec[2][64][8];
static uint16_t ks_box[2][64][4];        /* x0 x1 y0 y1 of the last draw, x1 == 0: none */
static uint8_t  ks_n[2];
static uint8_t  ks_keep[64];
volatile unsigned ks_stats[4];           /* [0] kept [1] drawn [2] passes [3] candidates refused */
#ifndef R60_MBAND
static unsigned ks_cur;                  /* the record index being drawn: tags its spans */
uint16_t r60_last_box[4];
#endif                                   /* under MBAND both are the slave's r60_dcs fields (aliased below r60_sprite) */
#else
#define ks_cur 0u
#endif
#ifdef R60_MBAND
/* the same span word into this core's own list, through its draw context */
#define R60_ROWSPAN_REC(xstart, xend) do { \
    int xs_ = (xstart) < 0 ? 0 : ((xstart) & ~3), xe_ = ((xend) + 3) & ~3, cap_ = (int)(FB_W + 512u); \
    if (xe_ > cap_) xe_ = cap_; \
    if (xe_ > xs_) { \
        unsigned n_ = dc->n; \
        if (n_ < dc->cap) { dc->spans[n_] = ((uint32_t)(unsigned)y << 24) | ((uint32_t)(dc->cur & 63u) << 16) | ((uint32_t)(xs_ >> 2) << 8) | (uint32_t)(xe_ >> 2); dc->n = n_ + 1u; } \
        else dc->full = 1; \
    } } while (0)
#else
#define R60_ROWSPAN_REC(xstart, xend) do { \
    unsigned bk_ = bank & 1u; \
    int xs_ = (xstart) < 0 ? 0 : ((xstart) & ~3), xe_ = ((xend) + 3) & ~3, cap_ = (int)(FB_W + 512u); \
    if (xe_ > cap_) xe_ = cap_; \
    if (xe_ > xs_) { \
        unsigned n_ = r60_rowspan_n[bk_]; \
        if (n_ < R60_SSPAN_CAP) { r60_rowspans[bk_][n_] = ((uint32_t)(unsigned)y << 24) | ((uint32_t)(ks_cur & 63u) << 16) | ((uint32_t)(xs_ >> 2) << 8) | (uint32_t)(xe_ >> 2); r60_rowspan_n[bk_] = (uint16_t)(n_ + 1u); } \
        else r60_rowspan_full[bk_] = 1; \
    } } while (0)
#endif
#ifdef R60_MBAND
static uint32_t r60_rowspans[2][R60_SSPAN_CAP];
#else
#define R60_SSPAN_CAP 768u
static uint32_t r60_rowspans[2][768];     /* y << 20 | (x0/4) << 10 | x1/4, per bank (1024: .bss meets the 0x06019000 guard at 2048) */
#endif
static uint16_t r60_rowspan_n[2];
static uint8_t  r60_rowspan_full[2];
#ifdef R60_PRIO
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
#ifdef R60_PRIOMASK
#define R60PUNCHCODE   /* only zoomed sprites still punch: the routines run from cart */
#else
#define R60PUNCHCODE R60CODE
#endif
static unsigned prio_xs, prio_ys, prio_psel, prio_bank1;  /* the FG scroll and page selects this pass punches against, read once a pass */
static uint32_t ks_fgkey[2];                 /* per bank: the scroll/page key its punches were made under */
static uint16_t ks_fgpsel[2], ks_fggen[2];
#define R60_FGGEN     (*(volatile uint16_t *)0x26028FB6u)   /* bumped by the master's walk when an FG cell changes */
static uint8_t  ks_punched[2][64];           /* per bank: the sprite was punched (kept only while the FG stands still) */
static uint16_t ks_pyr[2][64];               /* per bank: the punched rows, ymin << 8 | ymax+1 */
static uint8_t  ks_repunch[64];              /* kept, but the FG scrolled sideways: redraw and re-punch its punched rows only */
#ifndef R60_MBAND
static unsigned prio_ymin, prio_ymax;        /* the rows the last punch hit */
#endif                                       /* under MBAND: r60_dcs.ymin / ymax (aliased below r60_sprite) */
/* THE ROW MASKS, cached per code: eight bytes a tile, bit 7 = pixel 0, set where the tile
 * pixel is opaque. 512 entries direct-mapped on the code, in SDRAM the old renderer's tables
 * used (0x3D000..0x3E400, nothing in r60 touches it; sw156's per-pixel art loop cost 18% of
 * the passes). */
#define PRIO_MASK     ((uint8_t *)0x0603C400u)   /* [1024][8], after md_tag's end (0x3C400) */
#define PRIO_TAG      ((uint16_t *)0x0603E400u)  /* [1024], ends 0x3EC00, under the master stack */
static unsigned r60_prio_init;
volatile unsigned r60_prio_miss[2];          /* probe 2026-10-23: [0] mask lookups [1] rebuilds from cart */
#ifdef R60_MBAND
/* the mask from the art, once per miss: cart (the master's misses read the art too) */
static __attribute__((noinline)) void r60_prio_mask_fill(unsigned code, uint8_t *m)
{
    const uint8_t *a = altbeast_tiles + code * 64u;
    unsigned py;
    for (py = 0; py < 8u; py++) {
        const uint8_t *r = a + py * 8u;
        unsigned b = 0;
        if (r[0] & 7u) b |= 0x80u; if (r[1] & 7u) b |= 0x40u; if (r[2] & 7u) b |= 0x20u; if (r[3] & 7u) b |= 0x10u;
        if (r[4] & 7u) b |= 0x08u; if (r[5] & 7u) b |= 0x04u; if (r[6] & 7u) b |= 0x02u; if (r[7] & 7u) b |= 0x01u;
        m[py] = (uint8_t)b;
    }
}
static __attribute__((noinline)) void r60_prio_tags_init(void) { unsigned k; for (k = 0; k < 1024u; k++) PRIO_TAG[k] = 0xFFFFu; r60_prio_init = 1; }   /* once: cart */
R60CODE static const uint8_t *r60_prio_mask(r60_dc_t *dc, unsigned code)
{
    unsigned i = code & 1023u;
    uint8_t *m = PRIO_MASK + i * 8u;
    if (dc->master) {
        /* THE MASTER NEVER WRITES THE CACHE: read the slave's entry uncached under the tag, before
         * and after (the slave invalidates the tag before it refills an entry, so a torn read fails
         * one of the two checks), else build the mask from the art into its own scratch. */
        const volatile uint16_t *tu = (const volatile uint16_t *)((uint32_t)(PRIO_TAG + i) | 0x20000000u);
        const volatile uint32_t *mu = (const volatile uint32_t *)((uint32_t)m | 0x20000000u);
        uint32_t *mb = (uint32_t *)dc->mbuf;
        if (dc->pready && tu[0] == code) {
            uint32_t a = mu[0], b = mu[1];
            if (tu[0] == code) { mb[0] = a; mb[1] = b; return dc->mbuf; }
        }
        r60_prio_mask_fill(code, dc->mbuf);
        return dc->mbuf;
    }
    if (!r60_prio_init) r60_prio_tags_init();
    r60_prio_miss[0]++;
    if (PRIO_TAG[i] != code) {
        r60_prio_miss[1]++;
        PRIO_TAG[i] = 0xFFFFu;               /* invalid while the bytes change: the master's check */
        r60_prio_mask_fill(code, m);
        PRIO_TAG[i] = (uint16_t)code;
    }
    return m;
}
#else
static __attribute__((noinline)) const uint8_t *r60_prio_mask_cold(unsigned code)   /* the miss: the table init and a rebuild from the cart art (cart-resident, 2026-10-24: .ramtext room for PUNCH2) */
{
    unsigned i = code & 1023u;
    uint8_t *m = PRIO_MASK + i * 8u;
    if (!r60_prio_init) { unsigned k; for (k = 0; k < 1024u; k++) PRIO_TAG[k] = 0xFFFFu; r60_prio_init = 1; }
    if (PRIO_TAG[i] != code) {
        r60_prio_miss[1]++;
        const uint8_t *a = altbeast_tiles + code * 64u;
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
    unsigned i = code & 1023u;
    r60_prio_miss[0]++;
    if (r60_prio_init && PRIO_TAG[i] == code) return PRIO_MASK + i * 8u;
    return r60_prio_mask_cold(code);
}
#endif /* R60_MBAND */
/* THE PUNCH'S VARIABLE SHIFTS (2026-10-23): the SH-2 has no barrel shifter, so `b << (4 - sh)`,
 * `0xFF >> (x0 - sx)`, `0xFF << (sx + 8 - x1)` and `psel >> (q * 4)` were each a libgcc call
 * (~25 cycles) per cell-row; R60PHBARS read the punch at 70 lines a pass in the demo fight
 * (660 cell-rows). Constant shifts and tables instead, bit for bit. */
#define R60_PWIN(b, sh) ((sh) == 0u ? ((b) << 4) : (sh) == 1u ? ((b) << 3) : (sh) == 2u ? ((b) << 2) : ((b) << 1))
static const uint8_t r60_cm_r[8] = { 0xFFu, 0x7Fu, 0x3Fu, 0x1Fu, 0x0Fu, 0x07u, 0x03u, 0x01u };   /* 0xFF >> n */
static const uint8_t r60_cm_l[8] = { 0xFFu, 0xFEu, 0xFCu, 0xF8u, 0xF0u, 0xE0u, 0xC0u, 0x80u };   /* 0xFF << n, low 8 bits */
static const uint32_t r60_nib_mask[16] = {   /* 4 pixels a longword, pixel 0 in the top byte: 0xFF per set bit */
    0x00000000u, 0x000000FFu, 0x0000FF00u, 0x0000FFFFu, 0x00FF0000u, 0x00FF00FFu, 0x00FFFF00u, 0x00FFFFFFu,
    0xFF000000u, 0xFF0000FFu, 0xFF00FF00u, 0xFF00FFFFu, 0xFFFF0000u, 0xFFFF00FFu, 0xFFFFFF00u, 0xFFFFFFFFu };
#ifdef R60_PRIOROW
/* PRIOROW (2026-10-23): the FG-priority mask applied IN THE PLOT, lean. R60PHBARS on sw308 read
 * the box punch at 65-70 lines a pass in the demo fight (660 cell-row read-modify-writes of the
 * direct framebuffer image; the rig pays 98-134 there), and PRIOINLINE's per-row mask build at
 * +177 lines of plot. Here: once a sprite, the FG-priority cells under its box are listed by tile
 * row (cell FB x, cached 8-byte row mask); per row, the cells' row masks are folded with the
 * punch's own constant-shift arithmetic into a KEEP array of one longword per 4-pixel group
 * (0xFF per byte the sprite may write, 0x00 where an FG pixel covers it), the PLOT5 loop ANDs
 * every aligned store with it (3 instructions a group, every row, so one loop serves both),
 * and the touched entries are restored to all-ones after the row. The bytes left in the
 * framebuffer are the punch's: the same cells, the same masks, zeroed at write instead of
 * after; R60PRIOROWCHECK=1 runs the old box punch after each sprite over a checksum of its
 * box and counts any byte it still changed. Zoomed sprites and sprites whose box exceeds the
 * table keep the box punch. */
#define R60_PRKEEP ((uint32_t *)0x06029440u)   /* 128 keep masks, FB x >> 2; 0x29440-0x2963F read zero through the level-1 tape (sp_buf starts at 0x29800) */
#define PR_CY 10
#define PR_CX 8
typedef struct { uint16_t n; int16_t fbx[PR_CX]; uint16_t mi[PR_CX]; } pr_row_t;   /* 34 B: cell FB x and the row-mask cache index */
#define pr_cells ((pr_row_t *)0x06029640u)      /* 10 x 34 B = 0x29640-0x29794, the 0x29000 scratch (.bss sits on its guard) */
#define pr_touch ((uint8_t *)0x060297C0u)       /* 24 B, keep-array indices touched this row */
static unsigned pr_cy0, pr_ncy, pr_over, pr_hit, pr_init, pr_nt;
volatile unsigned r60_pr_stats[4];              /* [0] sprites scanned [1] masked rows [2] cells listed [3] over-table sprites */
static __attribute__((noinline)) void r60_pr_scan(int x0, int x1, int y0, int y1)   /* once a sprite: cart */   /* the sprite's box: FB x (with xo) and rows */
{
    unsigned psel = prio_psel, xs = prio_xs, ys = prio_ys;
    int xo = (int)r60_xo, cx, cy, cx0, cx1, cy0, cy1;
    unsigned k;
    pr_over = 0; pr_ncy = 0;
    if (!pr_init) { for (k = 0; k < 128u; k++) R60_PRKEEP[k] = 0xFFFFFFFFu; pr_init = 1; }
    x0 -= xo; x1 -= xo;                            /* canvas x, as the punch clips */
    if (x0 < 0) x0 = 0;
    if (x1 > (int)FB_W) x1 = (int)FB_W;
    if (y0 < 0) y0 = 0;
    if (y1 > (int)FB_H) y1 = (int)FB_H;
    if (x1 <= x0 || y1 <= y0) return;
    r60_pr_stats[0]++;
    cx0 = (x0 + (int)xs) >> 3; cx1 = (x1 - 1 + (int)xs) >> 3;
    cy0 = (y0 + (int)ys) >> 3; cy1 = (y1 - 1 + (int)ys) >> 3;
    if ((unsigned)(cy1 - cy0 + 1) > PR_CY || (unsigned)(cx1 - cx0 + 1) > PR_CX) { pr_over = 1; r60_pr_stats[3]++; return; }
    pr_cy0 = (unsigned)cy0; pr_ncy = (unsigned)(cy1 - cy0 + 1);
    for (cy = cy0; cy <= cy1; cy++) {
        unsigned wr = (unsigned)cy & 63u, n = 0;
        const volatile uint16_t *rowp[2];
        unsigned qh;
        for (qh = 0; qh < 2u; qh++) {
            unsigned page = (psel >> ((qh | (((wr >> 5) & 1u) << 1)) * 4u)) & 0xFu;
            rowp[qh] = page > 12u ? (const volatile uint16_t *)0 : PRIO_TILEMAP_U + page * 0x800u + (wr & 31u) * 64u;
        }
        for (cx = cx0; cx <= cx1; cx++) {
            unsigned wc = (unsigned)cx & 127u, w;
            const volatile uint16_t *rp = rowp[(wc >> 6) & 1u];
            if (!rp) continue;
            w = rp[wc & 63u];
            if (!(w & 0x8000u) || !(w & 0x1FFFu)) continue;
            pr_cells[cy - cy0].fbx[n] = (int16_t)(cx * 8 - (int)xs + xo);
            (void)r60_prio_mask(r60_tile_index(w, prio_bank1));                     /* fills the cache line for this code */
            pr_cells[cy - cy0].mi[n] = (uint16_t)(w & 1023u);
            n++;
        }
        pr_cells[cy - cy0].n = (uint16_t)n; r60_pr_stats[2] += n;
    }
}
/* the keep masks for row y: 1 when any cell masked it (the sprite's x range is [xa, xb) in FB x) */
R60CODE static unsigned r60_pr_row(unsigned y, int xa, int xb)
{
    unsigned cyi = ((y + prio_ys) >> 3) - pr_cy0, py = (y + prio_ys) & 7u, j, any = 0;
    int xo = (int)r60_xo, lo = xo, hi = (int)FB_W + xo;
    if (cyi >= pr_ncy || !pr_cells[cyi].n) return 0;
    if (xa > lo) lo = xa;
    if (xb < hi) hi = xb;
    pr_nt = 0;
    for (j = 0; j < pr_cells[cyi].n; j++) {
        unsigned b = PRIO_MASK[pr_cells[cyi].mi[j] * 8u + py];
        int fbx = pr_cells[cyi].fbx[j];
        if (!b) continue;
        if (fbx + 8 <= lo || fbx >= hi) continue;
        if (fbx >= lo && fbx + 8 <= hi) {                       /* the cell inside: three nibbles, as the punch */
            unsigned g = (unsigned)fbx >> 2, win = R60_PWIN(b, (unsigned)fbx & 3u), n0 = (win >> 8) & 0xFu, n1 = (win >> 4) & 0xFu, n2 = win & 0xFu;
            if (n0) { R60_PRKEEP[g]      &= ~r60_nib_mask[n0]; pr_touch[pr_nt++] = (uint8_t)g; }
            if (n1) { R60_PRKEEP[g + 1u] &= ~r60_nib_mask[n1]; pr_touch[pr_nt++] = (uint8_t)(g + 1u); }
            if (n2) { R60_PRKEEP[g + 2u] &= ~r60_nib_mask[n2]; pr_touch[pr_nt++] = (uint8_t)(g + 2u); }
        } else {                                                /* a cell on the canvas edge: by pixel */
            unsigned i;
            for (i = 0; i < 8u; i++, b <<= 1) {
                int px = fbx + (int)i;
                if ((b & 0x80u) && px >= lo && px < hi) {
                    unsigned g = (unsigned)px >> 2, sh = (unsigned)px & 3u;
                    R60_PRKEEP[g] &= ~(sh == 0u ? 0xFF000000u : sh == 1u ? 0x00FF0000u : sh == 2u ? 0x0000FF00u : 0x000000FFu);
                    pr_touch[pr_nt++] = (uint8_t)g;
                }
            }
        }
        any = 1;
    }
    if (any) { pr_hit = 1; r60_pr_stats[1]++; if (y < prio_ymin) prio_ymin = y; if (y + 1u > prio_ymax) prio_ymax = y + 1u; }
    return any;
}
static __attribute__((noinline)) void r60_pr_restore(void)   /* a few stores a row: cart */
{
    unsigned k;
    for (k = 0; k < pr_nt; k++) R60_PRKEEP[pr_touch[k]] = 0xFFFFFFFFu;
    pr_nt = 0;
}
#ifdef R60_PRIOROWCHECK
volatile unsigned r60_pr_bad, r60_pr_checked;
static uint32_t r60_pr_boxsum(const uint16_t *box)
{
    uint32_t sum = 0; unsigned y, x0 = (unsigned)box[0] & ~3u, x1 = box[1], xo = r60_xo;
    if (x1 > FB_W + xo) x1 = FB_W + xo;
    for (y = box[2]; y < box[3] && y < FB_H; y++) { const volatile uint32_t *fb = FB_DIR(y * FB_W + x0); unsigned k; for (k = 0; x0 + 4u * k < x1; k++) sum = sum * 3u + fb[k]; }
    return sum;
}
#endif
#endif
/* the punch's per-core state: under MBAND the draw context's, else the slave statics */
#ifdef R60_MBAND
#define PU_YMIN   dc->ymin
#define PU_YMAX   dc->ymax
#define PU_STAT   dc->pstat
#define PU_MASK(c) r60_prio_mask(dc, (c))
#define PU_HEAD   unsigned psel = dc->psel, xs = dc->pxs, ys = dc->pys; int xo = (int)dc->xo;
#define PU_ARGS   r60_dc_t *dc, const uint16_t *box
#else
#define PU_YMIN   prio_ymin
#define PU_YMAX   prio_ymax
#define PU_STAT   r60_prio_stats
#define PU_MASK(c) r60_prio_mask((c))
#define PU_HEAD   unsigned psel = prio_psel, xs = prio_xs, ys = prio_ys; int xo = (int)r60_xo;
#define PU_ARGS   const uint16_t *box
#endif
#if defined(R60_PUNCH2CHECK) && defined(R60_MBAND)
#error "R60PUNCH2CHECK is a slave-only identity check: build it without R60MBAND"
#endif
#ifdef R60_PUNCH2CHECK
/* the write set of a punch, as a hash of (address, keep mask) in store order: the identity that
 * does not depend on the framebuffer, which the master writes under the slave (its hold/fill
 * rows), so a checksum of the image between two punches moves on its own (p2chk 2026-10-24:
 * 208 of 7350 attract boxes, both orders, with every row count and ymin/ymax equal) */
volatile uint32_t r60_p2_wlog; volatile unsigned r60_p2_wn;
#define R60_P2NOTE(p, keep) do { r60_p2_wlog = (r60_p2_wlog * 3u + (uint32_t)(p)) * 3u + (uint32_t)(keep); r60_p2_wn++; } while (0)
#define R60_P1NIB(k, n) do { if ((n) == 0xFu) { R60_P2NOTE(&fb[k], 0u); fb[k] = 0; } else if (n) { R60_P2NOTE(&fb[k], ~r60_nib_mask[n]); fb[k] &= ~r60_nib_mask[n]; } } while (0)
#else
#define R60_P2NOTE(p, keep) do { } while (0)
#define R60_P1NIB(k, n) do { if ((n) == 0xFu) fb[k] = 0; else if (n) fb[k] &= ~r60_nib_mask[n]; } while (0)
#endif
#if !defined(R60_PUNCH2) || defined(R60_PUNCH2CHECK)
#ifdef R60_PUNCH2
#define R60_PUNCH1      r60_punch1                 /* R60PUNCH2CHECK: the old punch stays beside the new one, in cart */
#define R60PUNCH1CODE   __attribute__((noinline))
#else
#define R60_PUNCH1      r60_punch
#define R60PUNCH1CODE   R60PUNCHCODE
#endif
R60PUNCH1CODE static unsigned R60_PUNCH1(PU_ARGS)
{
    PU_HEAD
    const unsigned ppage[4] = { psel & 0xFu, (psel >> 4) & 0xFu, (psel >> 8) & 0xFu, (psel >> 12) & 0xFu };   /* the four page selects, once (2026-10-23: the per-cell `psel >> (q*4)` was a libgcc shift) */
    int x0 = (int)box[0] - xo, x1 = (int)box[1] - xo, y0 = (int)box[2], y1 = (int)box[3];
    int cx, cy, cx0, cx1, cy0, cy1;
    unsigned hit = 0;
    if (x0 < 0) x0 = 0;
    if (x1 > (int)FB_W) x1 = (int)FB_W;
    if (y0 < 0) y0 = 0;
    if (y1 > (int)FB_H) y1 = (int)FB_H;
    if (x1 <= x0 || y1 <= y0) return 0;
    PU_YMIN = FB_H; PU_YMAX = 0;
    cx0 = (x0 + (int)xs) >> 3; cx1 = (x1 - 1 + (int)xs) >> 3;
    cy0 = (y0 + (int)ys) >> 3; cy1 = (y1 - 1 + (int)ys) >> 3;
    for (cy = cy0; cy <= cy1; cy++) {
        unsigned wr = (unsigned)cy & 63u;
        int sy = cy * 8 - (int)ys;
        int py0 = y0 - sy, py1 = y1 - sy;          /* rows of the cell inside the box */
        const volatile uint16_t *rowp[2];           /* the tilemap row in each horizontal page (sw167: hoisted) */
        unsigned qh;
        if (py0 < 0) py0 = 0;
        if (py1 > 8) py1 = 8;
        for (qh = 0; qh < 2u; qh++) {
            unsigned page = ppage[qh | (((wr >> 5) & 1u) << 1)];
            rowp[qh] = page > 12u ? (const volatile uint16_t *)0 : PRIO_TILEMAP_U + page * 0x800u + (wr & 31u) * 64u;
        }
        for (cx = cx0; cx <= cx1; cx++) {
            unsigned wc = (unsigned)cx & 127u;
            unsigned w, colmask = 0xFFu;
            const uint8_t *m;
            const volatile uint16_t *rp = rowp[(wc >> 6) & 1u];
            int sx = cx * 8 - (int)xs, py;
            PU_STAT[1]++;
            if (!rp) continue;
            w = rp[wc & 63u];
            if (!(w & 0x8000u) || !(w & 0x1FFFu)) continue;
            PU_STAT[2]++;
            m = PU_MASK(r60_tile_index(w, prio_bank1));
            if (sx < x0) colmask &= r60_cm_r[x0 - sx];          /* bit 7 = pixel 0 */
            if (sx + 8 > x1) colmask &= r60_cm_l[sx + 8 - x1];
            for (py = py0; py < py1; py++) {
                /* LONGWORDS, NEVER BYTES: a byte write of zero to the direct framebuffer image is
                 * dropped (LESSONS.md:10555, the core; ares agrees: sw156/157's byte stores left
                 * every row intact while their counters said they wrote). The cell's 8 pixels
                 * start at sx = 4a + s: a 12-pixel window of three longwords from 4a, the mask
                 * shifted s pixels in; a full nibble is a store of 0, a partial one a masked AND. */
                unsigned b = m[py] & colmask, win, n0, n1, n2;
                volatile uint32_t *fb;
                unsigned sh = (unsigned)(sx + xo) & 3u;
                if (!b) continue;
                fb = FB_RMW((unsigned)(sy + py) * FB_W + ((unsigned)(sx + xo) & ~3u));
                win = R60_PWIN(b, sh);                       /* bits 11..0 = window pixels 0..11 */
                n0 = (win >> 8) & 0xFu; n1 = (win >> 4) & 0xFu; n2 = win & 0xFu;
                R60_P1NIB(0, n0);
                R60_P1NIB(1, n1);
                R60_P1NIB(2, n2);
                hit++;
                { unsigned yy = (unsigned)(sy + py); if (yy < PU_YMIN) PU_YMIN = yy; if (yy + 1u > PU_YMAX) PU_YMAX = yy + 1u; }
            }
        }
    }
    PU_STAT[3] += hit;
    if (hit) PU_STAT[0]++;
    return hit;
}
#endif /* the old box punch */
#ifdef R60_PUNCH2
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
static const uint32_t r60_nib_keep[16] __attribute__((section(".ramtext.nib"))) = {   /* ~r60_nib_mask: 0x00 per set bit of the nibble */
    0xFFFFFFFFu, 0xFFFFFF00u, 0xFFFF00FFu, 0xFFFF0000u, 0xFF00FFFFu, 0xFF00FF00u, 0xFF0000FFu, 0xFF000000u,
    0x00FFFFFFu, 0x00FFFF00u, 0x00FF00FFu, 0x00FF0000u, 0x0000FFFFu, 0x0000FF00u, 0x000000FFu, 0x00000000u };
/* a full nibble is a store of 0 (no FB read), a partial one a masked AND, an empty one nothing */
#define R60_P2NIB(k, n) do { unsigned _n = (n); if (_n == 0xFu) R60_P2Z(k); else if (_n) R60_P2K(k, r60_nib_keep[_n]); } while (0)
#define R60_P2Z(k)       do { R60_P2NOTE(&fb[k], 0u); fb[k] = 0; } while (0)
#define R60_P2K(k, keep) do { uint32_t _kp = (keep); R60_P2NOTE(&fb[k], _kp); fb[k] &= _kp; } while (0)
#ifdef R60_PUNCH2CHECK
volatile unsigned r60_punch2_rows, r60_punch2_full;   /* rows hit, and those that took the solid-row path */
#define R60_P2ROWCNT(full) do { r60_punch2_rows++; r60_punch2_full += (full); } while (0)
#else
#define R60_P2ROWCNT(full) do { } while (0)
#endif
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
#ifdef R60_MBAND
#define R60_P2SLAVE (!dc->master)
#else
#define R60_P2SLAVE 1
#endif
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
            code = r60_tile_index(w, prio_bank1); ci = code & 1023u;
            if (R60_P2SLAVE && r60_prio_init && PRIO_TAG[ci] == code) { r60_prio_miss[0]++; m = PRIO_MASK + ci * 8u; }   /* r60_prio_mask's hit path, inline (the slave's cache; the master reads it uncached under the tag) */
            else m = PU_MASK(code);
            if (sx < x0) colmask &= r60_cm_r[x0 - sx];          /* bit 7 = pixel 0 */
            if (sx + 8 > x1) colmask &= r60_cm_l[sx + 8 - x1];
            fx = (unsigned)(sx + xo);                            /* the cell's first pixel in the FB row: 4a + s */
            fb = FB_RMW((unsigned)(sy + py0) * FB_W + (fx & ~3u));
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
#ifdef R60_PUNCH2CHECK
/* r60_punch2_bad = boxes where the two punches disagree on what they WRITE (the write-set hash,
 * its store count), on the rows hit, or on prio_ymin/ymax, AND still disagree when the pair is
 * run again: a logic difference reproduces on the same inputs, a change of the FG name table
 * under the check (the game writes it from the master at any time; one box in 141,584 on the
 * tape, 2026-10-24) does not and counts in r60_punch2_transient. Must read 0.
 * why (confirmed only): [0] write set [1] store count [2] row count [3] ymin/ymax.
 * r60_punch2_fb[0] = boxes whose FB checksum moved between the two punches (new first),
 * [1] the same with the old first, [2] boxes whose checksum moved between two back-to-back
 * reads with NO punch between (the master writing under us: the noise floor of [0]/[1]). */
volatile unsigned r60_punch2_bad, r60_punch2_checked, r60_punch2_transient, r60_punch2_why[4], r60_punch2_fb[3];
static __attribute__((noinline)) unsigned r60_p2_pair(const uint16_t *box, unsigned nfirst, unsigned *why)   /* both punches; returns the disagreement bits */
{
    uint32_t wn, wo; unsigned hn, ho, yn0, yn1, yo0, yo1, nn, no, d = 0;
    if (nfirst) { r60_p2_wlog = 0; r60_p2_wn = 0; hn = r60_punch2(box); yn0 = prio_ymin; yn1 = prio_ymax; wn = r60_p2_wlog; nn = r60_p2_wn;
                  r60_p2_wlog = 0; r60_p2_wn = 0; ho = r60_punch1(box); yo0 = prio_ymin; yo1 = prio_ymax; wo = r60_p2_wlog; no = r60_p2_wn; }
    else        { r60_p2_wlog = 0; r60_p2_wn = 0; ho = r60_punch1(box); yo0 = prio_ymin; yo1 = prio_ymax; wo = r60_p2_wlog; no = r60_p2_wn;
                  r60_p2_wlog = 0; r60_p2_wn = 0; hn = r60_punch2(box); yn0 = prio_ymin; yn1 = prio_ymax; wn = r60_p2_wlog; nn = r60_p2_wn; }
    if (wn != wo) d |= 1u;
    if (nn != no) d |= 2u;
    if (hn != ho) d |= 4u;
    if (yn0 != yo0 || yn1 != yo1) d |= 8u;
    why[0] = hn; why[1] = yn0; why[2] = yn1;
    return d;
}
static __attribute__((noinline)) uint32_t r60_p2_boxsum(const uint16_t *box)   /* cart: a checksum of the direct image under the box */
{
    uint32_t sum = 0; unsigned y, x0 = (unsigned)box[0] & ~3u, x1 = box[1], xo = r60_xo;
    if (x1 > FB_W + xo) x1 = FB_W + xo;
    for (y = box[2]; y < box[3] && y < FB_H; y++) { const volatile uint32_t *fb = FB_DIR(y * FB_W + x0); unsigned k; for (k = 0; x0 + 4u * k < x1; k++) sum = sum * 3u + fb[k]; }
    return sum;
}
static __attribute__((noinline)) unsigned r60_punch(const uint16_t *box)
{
    uint32_t s0, s1, s2; unsigned r[3], d, nfirst = ++r60_punch2_checked & 1u;
    /* the FB checksum, as a secondary: new/old first, then one more read with nothing between */
    s0 = r60_p2_boxsum(box);
    d = r60_p2_pair(box, nfirst, r);
    s1 = r60_p2_boxsum(box);
    s2 = r60_p2_boxsum(box);
    (void)s0;
    if (d) {
        unsigned r2[3], d2 = r60_p2_pair(box, nfirst, r2);         /* again, on the same inputs */
        if (d2) {
            r60_punch2_bad++;
            if (d2 & 1u) r60_punch2_why[0]++;
            if (d2 & 2u) r60_punch2_why[1]++;
            if (d2 & 4u) r60_punch2_why[2]++;
            if (d2 & 8u) r60_punch2_why[3]++;
        } else r60_punch2_transient++;
        r[0] = r2[0]; r[1] = r2[1]; r[2] = r2[2];
    }
    if (s1 != s2) r60_punch2_fb[2]++;
    prio_ymin = r[1]; prio_ymax = r[2];
    return r[0];
}
#else
#define r60_punch r60_punch2
#endif
#endif /* R60_PUNCH2 */
#ifdef R60_ROWCLR
/* THE SAME PUNCH FROM THE SPRITE'S OWN ROW SPANS (sw162): the rows it drew and the x range it
 * drew on each, 4-px granular, as ROWCLR recorded them -- no empty rows, no slack around a
 * sparse sprite. Spans n0..n1 of this bank's list are the sprite's. */
#ifdef R60_MBAND
R60PUNCHCODE static unsigned r60_punch_spans(r60_dc_t *dc, unsigned n0, unsigned n1)   /* spans n0..n1 of THIS core's list */
#else
R60PUNCHCODE static unsigned r60_punch_spans(unsigned bk, unsigned n0, unsigned n1)
#endif
{
    PU_HEAD
    const unsigned ppage[4] = { psel & 0xFu, (psel >> 4) & 0xFu, (psel >> 8) & 0xFu, (psel >> 12) & 0xFu };   /* the four page selects, once (2026-10-23: the per-cell `psel >> (q*4)` was a libgcc shift) */
    unsigned k, hit = 0;
    PU_YMIN = FB_H; PU_YMAX = 0;
    for (k = n0; k < n1; k++) {
#ifdef R60_MBAND
        uint32_t sp = dc->spans[k];
#else
        uint32_t sp = r60_rowspans[bk][k];
#endif
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
#ifdef R60_PUNCHBAND
/* THE SAME PUNCH BY CELL BAND (2026-10-23): r60_punch_spans resolves the tilemap word, the page,
 * the row mask and the column clip for every CELL on every ROW -- 660 cell-rows a pass in the
 * demo fight, ~80 instructions each before the one to three RMWs, 66-70 lines a pass (R60PHBARS,
 * the shifts already constant). The sprite's spans are consecutive rows; eight of them share one
 * tile row. Here the spans are walked in bands of one tile row: each cell of the band's x union
 * is resolved once, then its rows are punched. The (row, cell) pairs and the masks written are
 * exactly r60_punch_spans's (a cell outside a row's own span is skipped), so the bytes are the
 * same; only the order of the RMWs across cells changes, and they never overlap. */
R60PUNCHCODE static unsigned r60_punch_spans_band(unsigned bk, unsigned n0, unsigned n1)
{
    unsigned psel = prio_psel, xs = prio_xs, ys = prio_ys;
    const unsigned ppage[4] = { psel & 0xFu, (psel >> 4) & 0xFu, (psel >> 8) & 0xFu, (psel >> 12) & 0xFu };
    int xo = (int)r60_xo;
    unsigned k = n0, hit = 0;
    prio_ymin = FB_H; prio_ymax = 0;
    while (k < n1) {
        /* the band: spans k..kb-1 share the tile row of span k (rows are consecutive and ascending) */
        uint32_t sp0 = r60_rowspans[bk][k];
        int y0b = (int)(sp0 >> 24);
        unsigned wr, kb = k;
        int ux0 = 0x7FFF, ux1 = -0x7FFF;
        int rx0[8], rx1[8], ry[8];
        unsigned nr = 0, cxu0, cxu1;
        int cx;
        if (y0b < 0 || y0b >= (int)FB_H) { k++; continue; }
        wr = (unsigned)((y0b + (int)ys) >> 3) & 63u;
        while (kb < n1 && nr < 8u) {
            uint32_t sp = r60_rowspans[bk][kb];
            int y = (int)(sp >> 24), x0 = (int)(((sp >> 8) & 0xFFu) << 2) - xo, x1 = (int)((sp & 0xFFu) << 2) - xo;
            if (y < 0 || y >= (int)FB_H) break;
            if (((unsigned)((y + (int)ys) >> 3) & 63u) != wr) break;
            kb++;
            if (x0 < 0) x0 = 0;
            if (x1 > (int)FB_W) x1 = (int)FB_W;
            if (x1 <= x0) continue;
            rx0[nr] = x0; rx1[nr] = x1; ry[nr] = y; nr++;
            if (x0 < ux0) ux0 = x0;
            if (x1 > ux1) ux1 = x1;
        }
        if (kb == k) kb = k + 1;                 /* a row outside the canvas: step over it */
        k = kb;
        if (nr == 0) continue;
        cxu0 = (unsigned)((ux0 + (int)xs) >> 3); cxu1 = (unsigned)((ux1 - 1 + (int)xs) >> 3);
        for (cx = (int)cxu0; cx <= (int)cxu1; cx++) {
            unsigned wc = (unsigned)cx & 127u;
            unsigned q = ((wc >> 6) & 1u) | (((wr >> 5) & 1u) << 1);
            unsigned page = ppage[q];
            unsigned w, r;
            const uint8_t *m;
            int sx = cx * 8 - (int)xs;
            unsigned sh = (unsigned)(sx + xo) & 3u;
            unsigned fbx = (unsigned)(sx + xo) & ~3u;
            r60_prio_stats[1]++;
            if (page > 12u) continue;
            w = PRIO_TILEMAP_U[page * 0x800u + (wr & 31u) * 64u + (wc & 63u)];
            if (!(w & 0x8000u) || !(w & 0x1FFFu)) continue;
            r60_prio_stats[2]++;
            m = r60_prio_mask(r60_tile_index(w, prio_bank1));
            for (r = 0; r < nr; r++) {
                int x0 = rx0[r], x1 = rx1[r], y = ry[r];
                unsigned colmask = 0xFFu, b, win, n0b, n1b, n2b, py;
                volatile uint32_t *fb;
                if (sx + 8 <= x0 || sx >= x1) continue;          /* this row's span does not reach the cell */
                if (sx < x0) colmask &= r60_cm_r[x0 - sx];
                if (sx + 8 > x1) colmask &= r60_cm_l[sx + 8 - x1];
                py = (unsigned)(y + (int)ys) & 7u;
                b = m[py] & colmask;
                if (!b) continue;
                fb = FB_RMW((unsigned)y * FB_W + fbx);
                win = R60_PWIN(b, sh);
                n0b = (win >> 8) & 0xFu; n1b = (win >> 4) & 0xFu; n2b = win & 0xFu;
                if (n0b == 0xFu) fb[0] = 0; else if (n0b) fb[0] &= ~r60_nib_mask[n0b];
                if (n1b == 0xFu) fb[1] = 0; else if (n1b) fb[1] &= ~r60_nib_mask[n1b];
                if (n2b == 0xFu) fb[2] = 0; else if (n2b) fb[2] &= ~r60_nib_mask[n2b];
                hit++;
                if ((unsigned)y < prio_ymin) prio_ymin = (unsigned)y; if ((unsigned)y + 1u > prio_ymax) prio_ymax = (unsigned)y + 1u;
            }
        }
    }
    r60_prio_stats[3] += hit;
    if (hit) r60_prio_stats[0]++;
    return hit;
}
#define r60_punch_spans r60_punch_spans_band
#endif
#endif
#endif
volatile unsigned r60_clr_stats[4];      /* [0] clears [1] spans [2] longs zeroed [3] overflow clears */
/* FEN must be clear before a fill is programmed or a CPU store reaches the framebuffer (sw154);
 * bounded, because a FEN that never clears must not hang the slave (sw233: 2000 ticks, 11 lines,
 * gave up on the MiSTer's fill and the stores after it were dropped). k = the probe's counter. */
#ifdef R60_CLRPROBE
/* THE CLEAR PROBE (2026-10-24, sw309 base): the ROWCLR clear runs before R60_PH[5]++, so no PHBARS
 * report times it. Slave FRT ticks (184 a line), 32-bit sums of 16-bit deltas, per clearing pass:
 *  [0] passes that ran the clear   [1] ticks clear start -> after the last FEN wait   [2] spans cleared
 *  [3] longwords zeroed            [4] fills issued     [5] short spans stored by the CPU
 *  [6] FEN-wait ticks before short spans   [7] FEN-wait ticks before fills   [8] the final FEN wait
 *  [9] waits entered with FEN set   [10] spans kept (carried)   [11] ticks span-close (t0) -> clear start
 *  [12] full-canvas clears          [13] ticks storing short spans (after their wait)
 *  [21] waits of 4000+ ticks (left out of [6..8]: a backward FRT read or an interrupt); the span-length
 *  histogram is offline (dump r60_rowspans) */
#define r60_clrp ((volatile uint32_t *)&r60_evring_mem[224])   /* [24] in the event ring's idle middle (see R60_EVRING: [224..247]); .bss is at its guard */
#define CLRP(k, v) (r60_clrp[k] += (uint32_t)(v))
#if R60_CLRPROBE == 2   /* the light probe: totals and counts only, the waits untimed (it fits beside CLRMERGE) */
#define R60_FENW(k) do { uint16_t _t = r60_frt(); while ((MARS_VDP_FBCTL & MARS_VDP_FEN) && (uint16_t)(r60_frt() - _t) < 20000u) ; } while (0)
#else
static __attribute__((noinline)) void r60_fenw_probe(unsigned k)   /* one copy, cart-resident: .ramtext is full (ares charges instructions only) */
{
    uint16_t _t = r60_frt(), _d; CLRP(9, (MARS_VDP_FBCTL & MARS_VDP_FEN) != 0);
    while ((MARS_VDP_FBCTL & MARS_VDP_FEN) && (uint16_t)(r60_frt() - _t) < 20000u) ;
    _d = (uint16_t)(r60_frt() - _t); if (_d < 4000u) CLRP(k, _d); else CLRP(21, 1);
}
#define R60_FENW(k) r60_fenw_probe(k)
#endif
#else
#define CLRP(k, v) ((void)0)
#define R60_FENW(k) do { uint16_t _t = r60_frt(); while ((MARS_VDP_FBCTL & MARS_VDP_FEN) && (uint16_t)(r60_frt() - _t) < 20000u) ; } while (0)
#endif
#endif
#ifdef R60_CLREXT
#if !defined(R60_ROWCLR) || !defined(R60_ROWFILL) || !defined(R60_KEEPSPR) || !defined(R60_TXTFAST)
#error "R60CLREXT is written for the line's clear: ROWCLR + ROWFILL + KEEPSPR + TXTFAST"
#endif
#endif
#ifdef R60_TXTFAST
static uint16_t cl_x0[28], cl_x1[28];    /* per text row: the x extent the clear wiped this pass (canvas x) */
#endif
#endif

#ifdef R60_PLOT4
/* PLOT4 (2026-10-17): the four pens of a sprite word by TABLE. r60_pt[flip][cset][byte] is
 * the two FB bytes a source byte becomes (base+nibble, 0 for a transparent or end nibble),
 * so a word costs two byte loads, two table loads and one shift/or instead of four nibble
 * extracts, four range tests and four packs (the SH-2 has no barrel shifter). The store
 * logic is PLOT3's. Zoomed rows (hz) keep their own path above. 16 KB, built once. */
static uint16_t r60_pt[2][16][256];
static unsigned r60_pt_init;
volatile unsigned r60_plot4_bad;
#ifdef R60_PLOTSTATS
volatile unsigned r60_plot_stats[8];            /* [0] words [1] zero words [2] groups stored [3] rows drawn [4] rows skipped (band) [5] sprites [6] rows clipped off-screen */
#endif
static __attribute__((noinline)) void r60_pt_build(void)   /* once: cart */
{
    unsigned c, b;
    for (c = 0; c < 16u; c++)
        for (b = 0; b < 256u; b++) {
            unsigned hi = b >> 4, lo = b & 15u;
            unsigned ph = ((unsigned)(hi - 1u) < 14u) ? c * 16u + hi : 0u;
            unsigned pl = ((unsigned)(lo - 1u) < 14u) ? c * 16u + lo : 0u;
            r60_pt[0][c][b] = (uint16_t)((ph << 8) | pl);
            r60_pt[1][c][b] = (uint16_t)((pl << 8) | ph);
        }
    r60_pt_init = 1;
#ifdef R60_PLOT4CHECK
    {   /* ON-ROM IDENTITY: every source word, both orders, four sets, against PLOT3's arithmetic */
        unsigned w, f, k;
        for (k = 0; k < 4u; k++) for (f = 0; f < 2u; f++) for (w = 0; w < 65536u; w++) {
            unsigned base = (k * 5u) * 16u, p0, p1, p2, p3;
            uint32_t a3, a4;
            if (!f) { p0 = (w >> 12) & 0xFu; p1 = (w >> 8) & 0xFu; p2 = (w >> 4) & 0xFu; p3 = w & 0xFu; }
            else    { p0 = w & 0xFu; p1 = (w >> 4) & 0xFu; p2 = (w >> 8) & 0xFu; p3 = (w >> 12) & 0xFu; }
            p0 = ((unsigned)(p0 - 1u) < 14u) ? base + p0 : 0u; p1 = ((unsigned)(p1 - 1u) < 14u) ? base + p1 : 0u;
            p2 = ((unsigned)(p2 - 1u) < 14u) ? base + p2 : 0u; p3 = ((unsigned)(p3 - 1u) < 14u) ? base + p3 : 0u;
            a3 = ((uint32_t)p0 << 24) | ((uint32_t)p1 << 16) | ((uint32_t)p2 << 8) | (uint32_t)p3;
            a4 = f ? (((uint32_t)r60_pt[1][k * 5u][w & 0xFFu] << 16) | r60_pt[1][k * 5u][w >> 8])
                   : (((uint32_t)r60_pt[0][k * 5u][w >> 8] << 16) | r60_pt[0][k * 5u][w & 0xFFu]);
            if (a3 != a4) r60_plot4_bad++;
        }
        r60_plot4_bad |= 0x80000000u;            /* the check ran */
    }
#endif
}
#endif
#ifdef R60_PLOT5
/* PLOT5 (2026-10-17): one row, everything in registers. The alignment k = x & 3 is constant
 * along a row (x steps by 4), so each row runs ONE of four loops with a carried longword:
 * the store at aligned address a covers the tail of the previous group and the head of this
 * one, where PLOT3 stored each group twice. The clip limits are locals, the flip has its own
 * loops, the pixel count is a local added once. Bytes stored equal PLOT3's (the check build
 * replays each row both ways into RAM with overwrite semantics and counts differences).
 * Groups left of the canvas (x < xo) take PLOT3's slow form until the fast loop can start. */
#ifdef R60_PLOT5CHECK
volatile unsigned r60_plot5_bad, r60_plot5_rows;
volatile unsigned r60_span_bad;            /* ROWFAST: rows whose span word differs from the old formula + 0x10000 a box, 0x100000 a row outside the band */
static uint8_t r60_p5_a[512], r60_p5_b[512];
#define P5_ST0(ptr, v) do { uint32_t v_ = (v); uint8_t *b_ = (uint8_t *)(ptr); \
    if (v_ >> 24) b_[0] = (uint8_t)(v_ >> 24); if ((v_ >> 16) & 0xFFu) b_[1] = (uint8_t)(v_ >> 16); \
    if ((v_ >> 8) & 0xFFu) b_[2] = (uint8_t)(v_ >> 8); if (v_ & 0xFFu) b_[3] = (uint8_t)v_; } while (0)
#else
#define P5_ST0(ptr, v) (*(volatile uint32_t *)(ptr) = (v))
#endif
/* P5_ST is redefined around the masked instantiations (R60PRIOINLINE): macros bind at use */
#define P5_ST(ptr, v) P5_ST0(ptr, v)
#define P5_STORE(ptr, v) P5_ST(ptr, v)
#define P5_ACC(w) (FL ? (((uint32_t)pt[(w) & 0xFFu] << 16) | pt[(w) >> 8]) : (((uint32_t)pt[(w) >> 8] << 16) | pt[(w) & 0xFFu]))
#define P5_LAST(w) (FL ? (unsigned)((w) >> 12) : (unsigned)((w) & 0xFu))
#define P5_NEXT()  (FL ? sd[o--] : sd[o++])
/* the generic (PLOT3-shaped) group, used left of the canvas and by the check */
#ifdef R60_PRIOROW
#define P5_KEEP(g) (R60_PRKEEP[(g)])
#else
#define P5_KEEP(g) 0xFFFFFFFFu
#endif
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
#ifdef R60_PRIOROW
#define P5_KP_DECL const uint32_t *kp = R60_PRKEEP + (((unsigned)x & ~3u) >> 2);
#define P5_KP_AND(v) ((v) & *kp)
#define P5_KP_STEP kp++;
#else
#define P5_KP_DECL
#define P5_KP_AND(v) (v)
#define P5_KP_STEP
#endif
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
#ifdef R60_ZOOMFAST
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
#ifdef R60_ZOOMCHECK
#define ZF_STORE ZF_STCHK
ZF_ROWFN_A(r60_zoom_rowc_n, 0, static __attribute__((noinline)))   /* check-only: cart, not .ramtext (noinline: LTO would fold it into r60_sprite) */
ZF_ROWFN_A(r60_zoom_rowc_f, 1, static __attribute__((noinline)))
#undef ZF_STORE
volatile unsigned r60_zoom_bad, r60_zoom_rows, r60_zoom_badx;   /* differing bytes, rows checked, rows whose end x differed */
#ifdef R60_PLOT5CHECK
#define r60_zc_a r60_p5_a      /* both checks together: one pair of row buffers (two pairs put .bss over the 0x06019000 guard) */
#define r60_zc_b r60_p5_b
#else
static uint8_t r60_zc_a[512], r60_zc_b[512];
#endif
#endif
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
#endif
#ifdef R60_PLOT5CHECK
/* PLOT3's row, into RAM with overwrite semantics: the reference for the check */
#define P5_REFFN(NAME, FLV) \
R60CODE static unsigned NAME(const uint16_t *sd, unsigned o, int x, uint8_t *row, int lim, int xo, const uint16_t *pt) \
{ \
    enum { FL = (FLV) }; \
    int xo4 = xo - 4; unsigned npx = 0; \
    for (;;) { \
        uint16_t w = P5_NEXT(); \
        unsigned last = P5_LAST(w); \
        P5_SLOW(w); \
        if (last == 15u || x >= lim) break; \
    } \
    (void)npx; return (unsigned)x; \
}
P5_REFFN(r60_plot_row3refn, 0)
P5_REFFN(r60_plot_row3reff, 1)
R60CODE static unsigned r60_plot_row3ref(const uint16_t *sd, unsigned o, int fl, int x, uint8_t *row, int lim, int xo, const uint16_t *pt)
{ return fl ? r60_plot_row3reff(sd, o, x, row, lim, xo, pt) : r60_plot_row3refn(sd, o, x, row, lim, xo, pt); }
#endif
#endif
#ifdef R60_PALMAP
__attribute__((noinline)) static void r60_palmap_assign(void)   /* cart-resident: s_main is in .ramtext and the slot is full */
{
            {
                static unsigned pm_init; static uint16_t used1, used2;   /* banks used one and two passes ago */
                uint16_t used = 0; uint8_t need[64]; unsigned nn = 0, k;
                if (!pm_init) { r60_palmap_init(); pm_init = 1; }
                for (k = 0; k < 64u; k++) {
#if defined(R60_SLAVEWIN)
                    const volatile uint16_t *e = R60_SNAPSLOT(R60_DRAWSEQ) + k * 8;
#elif defined(R60_TICKLATE)
                    const volatile uint16_t *e = R60_SNAP2 + k * 8;
#else
                    const volatile uint16_t *e = SPR_SNAP + k * 8;
#endif
                    unsigned pl, bk;
                    if (e[2] & 0x8000u) break;                   /* the list's end */
                    pl = e[4] & 0x3Fu; bk = R60_PALMAP_U[pl];
                    if (bk < 16u && R60_BANKPAL_U[bk] == pl) used |= (uint16_t)(1u << bk);
                    else { unsigned j; for (j = 0; j < nn && need[j] != pl; j++) ; if (j == nn) need[nn++] = (uint8_t)pl; }
                }
                for (k = 0; k < nn; k++) {
                    unsigned pl = need[k], b2, busy = used | used1 | used2 | 1u, old;   /* bank 0 (backdrop, through bit) is never handed to another palette */
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
            }
}
#endif
#ifdef R60_LOCKPROBE
#include "r60_lockprobe.h"
#endif
#if defined(R60_RIGSPLIT) && !defined(R60_NOBARS)
static __attribute__((noinline)) void r60_rs_bars(void)
{
    volatile uint32_t *v = RSX + 72;
    volatile uint16_t *pl = (volatile uint16_t *)(RSX + 100);
    uint32_t np = RSX[14] - RSX[32 + 14];
    unsigned r;
    if (np >= 32u) {
        uint32_t d[32], nv = R60_DIAG[0] - RSX[64], ms = 0, sl = 0, ml;
        unsigned k;
        for (k = 0; k < 32u; k++) { uint32_t c = RSX[k]; d[k] = c - RSX[32 + k]; RSX[32 + k] = c; }
        RSX[64] = R60_DIAG[0];
        for (k = 0; k < 11u; k++) sl += d[k];
        for (k = 0; k < 7u; k++) ms += d[16 + k];
        ml = ms / 262u; if (!ml) ml = 1;
#define LN(x) ((x) / (np * 184u))
        v[0] = LN(d[1]); v[1] = LN(d[2]); v[2] = LN(d[3]); v[3] = LN(d[12]); v[4] = LN(d[13]);
        v[5] = d[4] > d[12] + d[13] ? LN(d[4] - d[12] - d[13]) : 0u;
        v[6] = LN(d[5]); v[7] = LN(d[6]); v[8] = LN(d[7]); v[9] = LN(d[8]); v[10] = LN(d[9]); v[11] = LN(d[10]);
        sl += d[26] + d[27] + d[28];
        v[12] = LN(d[0] + d[26] + d[27] + d[28]) / 2u; v[13] = (262u * nv / np) / 2u; v[14] = LN(sl) / 2u; v[15] = LN(d[26]);
        for (k = 0; k < 7u; k++) v[16 + k] = d[16 + k] / ml;
        v[23] = nv ? ms / nv / 256u : 0u; v[24] = np; v[25] = nv / 2u; v[26] = LN(d[27]); v[27] = LN(d[28]);
#undef LN
    }
    for (r = 0; r < 28u; r++) {
        unsigned l = v[r], p = pl[(r60_bkp & 1u) * 28u + r], x;
        volatile uint8_t *q = FB_DIRB((196u + r) * FB_W);
        volatile uint32_t *q4 = FB_DIR((196u + r) * FB_W);
        if (l > FB_W - 1u) l = FB_W - 1u;
        for (x = 0; x < (l >> 2); x++) q4[x] = 0xFFFFFFFFu;
        for (x = l & ~3u; x < l; x++) q[x] = (uint8_t)R60_BAR_PEN;
        for (x = l; x < ((l + 3u) & ~3u); x++) q[x] = 0u;
        for (x = (l + 3u) >> 2; x < ((p + 4u) >> 2) + 1u && x < FB_W / 4u; x++) q4[x] = 0u;
        pl[(r60_bkp & 1u) * 28u + r] = (uint16_t)l;
    }
}
#endif
#ifndef R60_NOBARS
__attribute__((noinline)) static void r60_bars_paint(void)   /* the bars are instruments: ~40 lines a pass on the ares profile */
{
#ifdef R60_RIGSPLIT
    r60_rs_bars(); return;
#endif
#ifdef R60_PHBARS
    {   /* THE SLAVE'S PHASE BARS (R60PHBARS, sw263): where a pass's time goes, readable on the
         * rig. Every 32 passes, the average SCANLINES per pass (FRT 184 ticks a line) of four
         * phases, one pixel a line, two rows each in screen rows 216..223: prep (clear +
         * offset + hold fill, R60_PH[0..2]), sprites (R60_PH[3]), text (R60_PH[4]), and the
         * whole pass (all five). Nothing else is drawn, so the probe costs ~8 rows a pass. */
        static uint32_t ph0[6], pt0[2]; static uint16_t len[4];
        uint32_t np = R60_PH[5] - ph0[5];
        if (np >= 32u) {
            uint32_t d[5]; unsigned k;
            for (k = 0; k < 5u; k++) { d[k] = R60_PH[k] - ph0[k]; ph0[k] = R60_PH[k]; }
            ph0[5] = R60_PH[5];
            len[0] = (uint16_t)((r60_pht[0] - pt0[0]) / (np * 184u));   /* sprite plot */
            len[1] = (uint16_t)((r60_pht[1] - pt0[1]) / (np * 184u));   /* priority punch */
            pt0[0] = r60_pht[0]; pt0[1] = r60_pht[1];
            len[2] = (uint16_t)(d[3] / (np * 184u));                    /* the whole sprite phase */
            len[3] = (uint16_t)((d[0] + d[1] + d[2] + d[3] + d[4]) / (np * 184u));
        }
        {   unsigned r, x;
            for (r = 216u; r < 224u; r++) {
                volatile uint8_t *q = FB_DIRB(r * FB_W);
                unsigned l = len[(r - 216u) >> 1]; if (l > FB_W) l = FB_W;
                for (x = 0; x < FB_W; x++) q[x] = (x < l) ? (uint8_t)R60_BAR_PEN : 0u;
            }
        }
        return;
    }
#endif
        {   /* THE BARS, into the same hidden bank, through the DIRECT image so the zero
             * tail of a short bar erases the previous frame's longer one (a zero byte
             * through the overwrite image is a no-op by design). Screen rows 216..223,
             * which is where the old renderer's bars landed too -- it wrote sbuf rows
             * 224..231 and sbuf carries an 8-row top margin (m_main.c:2520). */
            unsigned r;
#ifdef R60_BARPROBE
            /* THREE CAUSES, ONE BLOCK. The bars do not appear on the board at all while
             * ares paints them. Mid-screen, where the sprites DO appear: left half in
             * the reserved pen 255, right half in pen 0x11, which is a set the game
             * itself uses. Both visible = the 216..223 rows were the fault; only the
             * right half = pen 255 / the CRAM entry; neither = the slave's writes
             * through the DIRECT image do not land on the FPGA. */
            /* AND ONE BLOCK INSIDE THE ROWS THAT DEMONSTRABLY WORK. On the board the
             * sprites land (rows 120..183 in this scene) while every block at 100..109
             * and every bar at 216..223 is invisible -- so the next thing to rule out is
             * the ROW RANGE itself, i.e. the 32X line table. Same writes, rows 130..139. */
            for (r = 100u; r < 104u; r++) {          /* through the DIRECT image */
                volatile uint8_t *q = FB_DIRB(r * FB_W);
                unsigned x;
                for (x = 0; x < FB_W; x++)
                    q[x] = (x < 160u) ? (uint8_t)R60_BAR_PEN : 0x11u;
            }
            for (r = 106u; r < 110u; r++) {          /* through the OVERWRITE image,
                                                      * which the sprites use and which
                                                      * demonstrably lands on the board */
                volatile uint8_t *q = FB_OVR(r * FB_W);
                unsigned x;
                for (x = 0; x < FB_W; x++)
                    q[x] = (x < 160u) ? (uint8_t)R60_BAR_PEN : 0x11u;
            }
#endif
#ifndef R60_BARSFIRST
            /* THE STORE EXPERIMENT. Sprite pixels land on the board in the SAME ROWS
             * and the SAME PASS as six instrument bars that landed nothing, with pen,
             * position-in-pass and framebuffer image all excluded by measurement. The
             * only remaining difference is what sits BETWEEN the stores: the sprite
             * loop reads the CART every four pixels (sd[o++], a ROM access carrying the
             * core's wait), the bars had nops. Every board result fits one sentence --
             * a store lands only when it is followed by a slow non-VDP bus cycle:
             * art-paced sprites a quarter, no-art solid sprites 2-11 px of 320,
             * nop-paced bars 0, a full-screen ramp showing row 0 alone. IF.sv:974-983
             * is the kind of RTL that does that: with the in-flight guard commented
             * out, the next bus start re-latches VDP_A/VDP_DO over an access still
             * waiting for the VDP's ack.
             *
             * Three bars, each a known 256 px (a 32 px floor that means "I ran", then
             * 224 fixed), differing ONLY in the filler between stores. */
            {
                static volatile uint16_t r60_sink;
                unsigned r, x;
                for (r = 100u; r < 116u; r++) {
                    volatile uint8_t *q;
                    unsigned kind;
                    if ((r >= 104u && r < 106u) || (r >= 110u && r < 112u)) continue;
                    kind = (r < 104u) ? 0u : (r < 110u) ? 1u : 2u;
                    q = FB_OVR(r * FB_W);
                    for (x = 8u; x < 264u; x++) {
                        q[x] = (uint8_t)R60_BAR_PEN;
                        if (kind == 0u)                          /* nops: the control */
                            __asm__ __volatile__("nop\n\tnop\n\tnop\n\tnop"
                                                 ::: "memory");
                        else if (kind == 1u)                     /* a CART read */
                            r60_sink = altbeast_sprites[x & 0xFFFu];
                        else                                     /* an SDRAM read */
                            r60_sink = *(volatile uint16_t *)0x26020000u;
                    }
                }
            }
#endif
            for (r = 216u; r < 224u; r++) {
                /* THE BAR SPLITS THE TWO DIFFERENCES. It renders ~0% on the board
                 * (1 pixel of 660) while the sprites render 30-40%, and the two differ
                 * in BOTH the framebuffer image and the rows. Through the OVERWRITE
                 * image, same rows: if the bar now behaves like the sprites, the
                 * DIRECT image is the dead path; if it stays at zero, the rows are.
                 * (The zero tail cannot erase through the overwrite image by design,
                 * so only the lit run is the measurement.) */
#ifdef R60_BAROVR
                volatile uint8_t *q = FB_OVR(r * FB_W);
#else
                volatile uint8_t *q = FB_DIRB(r * FB_W);
#endif
                unsigned len = (r < 220u) ? R60_BARA : R60_BARB;
                unsigned x;
                /* THE BAR IS THE SCENE-INDEPENDENT DENSITY INSTRUMENT. Its length is
                 * known exactly (R60_BARA/B pixels of one pen, in known rows), unlike
                 * the attract, which moves between captures -- comparing a board
                 * screenshot taken at an arbitrary moment against a fixed ares frame
                 * is not a measurement, and every board "density" figure before this
                 * one carries that error. It is also the TIGHTEST store loop in the
                 * renderer, so by the FIFO-rate account it should be the worst hit:
                 * measured 1-2 pixels of ~95 on the board against 540-660 on ares. */
                for (x = 0; x < FB_W; x++) {
                    q[x] = (x < len) ? (uint8_t)R60_BAR_PEN : 0u;
                    R60_SPACE();
                }
                R60_ROWSET(r60_mask, r);
            }
        }
}
#endif
#if defined(R60_PRIOMASK) && defined(R60_PRIO)
/* THE PRIORITY MASK IN THE PLOT (R60PRIOMASK, sw263). The punch drew a sprite and then erased it
 * where FG priority tiles cover it, a read-modify-write of the framebuffer per masked row: 98-134
 * lines of a 267-308-line pass on the rig in the demos (R60PHBARS). Now a row is plotted into a
 * cached line buffer, the masked pixels are zeroed there, and the row is copied out in longwords
 * through the OVERWRITE image (zero bytes skipped, as P5_STORE already relies on): no framebuffer
 * read, the same stores as before. A pixel under FG priority is simply never written; the bank
 * under it was cleared, or holds an earlier sprite that was masked the same way. */
#define R60_LINEBUF ((uint32_t *)0x06029000u)   /* 512 bytes, cached; 0x29000-0x291FF read zero through 3500 frames (sw236) */
static uint16_t pm_tag[256], pm_val[256];      /* world cell (row<<7|col)|0x8000 -> code|0x8000 when it has priority, 0 when not */
static unsigned pm_psel = 0xFFFFFFFFu, pm_gen = 0xFFFFFFFFu, pm_bank1 = 0xFFFFFFFFu, pm_hit;
static unsigned r60_pm_cell(unsigned cx, unsigned cy)
{
    unsigned key = ((cy & 63u) << 7) | (cx & 127u), i = (key ^ (key >> 8)) & 255u;
    if (pm_tag[i] == (key | 0x8000u)) return pm_val[i];
    {   unsigned wr = cy & 63u, wc = cx & 127u, qh = (wc >> 6) & 1u, v = 0;
        unsigned page = (prio_psel >> ((qh | (((wr >> 5) & 1u) << 1)) * 4u)) & 0xFu;
        if (page <= 12u) {
            uint16_t w = PRIO_TILEMAP_U[page * 0x800u + (wr & 31u) * 64u + (wc & 63u)];
            if ((w & 0x8000u) && (w & 0x1FFFu)) v = r60_tile_index(w, prio_bank1) | 0x8000u;
        }
        pm_tag[i] = (uint16_t)(key | 0x8000u); pm_val[i] = (uint16_t)v;
        return v;
    }
}
/* the row is in R60_LINEBUF at FB x offsets [xa, xe): mask it, copy it out, leave the buffer zero */
static void r60_pm_row(unsigned y, int xa, int xe)
{
    uint8_t *lb = (uint8_t *)R60_LINEBUF;
    int xo = (int)r60_xo, a = xa < xo ? xo : xa, e = xe > (int)FB_W + xo ? (int)FB_W + xo : xe;
    unsigned cy, py, k;
    if (e <= a) { for (k = (unsigned)xa & ~3u; (int)k < xe && k < 512u; k += 4u) *(uint32_t *)(lb + k) = 0; return; }
    if (prio_psel != pm_psel || R60_FGGEN != pm_gen || prio_bank1 != pm_bank1) {   /* the FG changed: the cell cache is stale */
        for (k = 0; k < 256u; k++) pm_tag[k] = 0; pm_psel = prio_psel; pm_gen = R60_FGGEN; pm_bank1 = prio_bank1;
    }
    cy = (y + prio_ys) >> 3; py = (y + prio_ys) & 7u;
    {   int cx0 = (a - xo + (int)prio_xs) >> 3, cx1 = (e - 1 - xo + (int)prio_xs) >> 3, cx;
        for (cx = cx0; cx <= cx1; cx++) {
            unsigned v = r60_pm_cell((unsigned)cx, cy), b, i;
            int sx;
            if (!v) continue;
            b = r60_prio_mask(v & 0x3FFFu)[py];
            if (!b) continue;
            sx = cx * 8 - (int)prio_xs + xo;
            for (i = 0; i < 8u; i++, b <<= 1)
                if (b & 0x80u) { int px = sx + (int)i; if (px >= a && px < e && lb[px]) { lb[px] = 0; pm_hit = 1; } }
            if (y < prio_ymin) prio_ymin = y; if (y + 1u > prio_ymax) prio_ymax = y + 1u;
        }
    }
    {   volatile uint32_t *fb = (volatile uint32_t *)FB_OVR(y * FB_W);
        for (k = (unsigned)a & ~3u; (int)k < e; k += 4u) { uint32_t v = *(uint32_t *)(lb + k); if (v) fb[k >> 2] = v; }
        for (k = (unsigned)xa & ~3u; (int)k < xe + 4 && k < 512u; k += 4u) *(uint32_t *)(lb + k) = 0;
    }
}
#endif
#if defined(R60_PRIOINLINE) && defined(R60_PRIO)
/* THE PRIORITY MASK IN THE STORE (R60PRIOINLINE, sw264). sw263's line buffer cost more than the
 * punch it removed (rig 208 vs 199 lines a pass): the SH-2 cache is write-through, so every buffer
 * store was an SDRAM write, and the copy-out was a second pass. Here a row that crosses priority
 * cells first gets a longword mask (0xFF per covered byte, FB x offsets, 0x29000 cached), and the
 * plotter's own longword store drops the covered bytes: one store per group, as unmasked rows.
 * Rows with no priority cell in reach take the unmodified loops. */
#define R60_PMLW ((uint32_t *)0x06029000u)    /* 128 longwords = 512 FB bytes; the sw263 line buffer's range, left zero */
volatile unsigned r60_pm_over, r60_pm_rows;   /* rows that ran past the masked reach; rows masked */
static unsigned pm_lo = 1u, pm_hi = 0u, pm_init;
/* the priority cells under one cell row of the sprite, cached: the hashed lookups run once per 8
 * rows, not per row (sw264 first cut looked them up per row from cart: rig plot 146 -> 243) */
static unsigned pmc_cy = 0xFFFFFFFFu, pmc_n; static int pmc_xa, pmc_xb;
#define pmc_sx   ((int16_t *)0x06029200u)    /* 64 entries; 0x29200-0x292FF read zero through a 2400-frame play run (sw262); .bss is at its guard */
#define pmc_code ((uint16_t *)0x06029280u)
R60CODE static unsigned r60_pm_build(unsigned y, int xa, int xb)
{
    uint32_t *lw = R60_PMLW;
    int xo = (int)r60_xo, a = xa < xo ? xo : xa, e = xb > (int)FB_W + xo ? (int)FB_W + xo : xb;
    unsigned cy, py, any = 0, lo = 128u, hi = 0u, n;
    if (e <= a) return 0;
    cy = (y + prio_ys) >> 3; py = (y + prio_ys) & 7u;
    if (cy != pmc_cy || a != pmc_xa || e != pmc_xb || prio_bank1 != pm_bank1) {
        int cx0 = (a - xo + (int)prio_xs) >> 3, cx1 = (e - 1 - xo + (int)prio_xs) >> 3, cx;
        if (!pm_init) { unsigned k; for (k = 0; k < 128u; k++) lw[k] = 0; pm_init = 1; }
        if (prio_psel != pm_psel || R60_FGGEN != pm_gen || prio_bank1 != pm_bank1) {
            unsigned k; for (k = 0; k < 256u; k++) pm_tag[k] = 0; pm_psel = prio_psel; pm_gen = R60_FGGEN; pm_bank1 = prio_bank1;
        }
        n = 0;
        for (cx = cx0; cx <= cx1 && n < 64u; cx++) {
            unsigned v = r60_pm_cell((unsigned)cx, cy);
            if (v) { pmc_sx[n] = (int16_t)(cx * 8 - (int)prio_xs + xo); pmc_code[n] = (uint16_t)(v & 0x3FFFu); n++; }
        }
        pmc_n = n; pmc_cy = cy; pmc_xa = a; pmc_xb = e;
    }
    for (n = 0; n < pmc_n; n++) {
        unsigned b = r60_prio_mask(pmc_code[n])[py];
        int sx = pmc_sx[n];
        if (!b) continue;
        if (sx >= a && sx + 8 <= e) {                    /* whole cell inside: three nibbles, as the punch */
            unsigned li = (unsigned)sx >> 2, win = b << (4u - ((unsigned)sx & 3u)), m;
            if ((m = (win >> 8) & 0xFu)) lw[li]      |= r60_nib_mask[m];
            if ((m = (win >> 4) & 0xFu)) lw[li + 1u] |= r60_nib_mask[m];
            if ((m = win & 0xFu))        lw[li + 2u] |= r60_nib_mask[m];
            if (li < lo) lo = li; if (li + 2u > hi) hi = li + 2u;
        } else {
            unsigned i;
            for (i = 0; i < 8u; i++, b <<= 1)
                if (b & 0x80u) { int px = sx + (int)i; if (px >= a && px < e) { unsigned li = (unsigned)px >> 2;
                    lw[li] |= 0xFF000000u >> (((unsigned)px & 3u) << 3); if (li < lo) lo = li; if (li > hi) hi = li; } }
        }
        any = 1;
        if (y < prio_ymin) prio_ymin = y; if (y + 1u > prio_ymax) prio_ymax = y + 1u;
    }
    pm_lo = lo; pm_hi = hi;
    return any && lo <= hi;
}
static inline void r60_pm_clear(void) { unsigned k; for (k = pm_lo; k <= pm_hi; k++) R60_PMLW[k] = 0; }
/* a row that ran past the pitch bound: the tail [xb, xe) is masked after the fact, a read-modify-write
 * as the punch did. Rare (320 rows of 52628 masked in a 2400-frame play run); cart-resident. */
__attribute__((noinline)) static void r60_pm_tail(unsigned y, int xb, int xe)
{
    if (r60_pm_build(y, xb, xe)) {
        volatile uint32_t *fb = FB_RMW(y * FB_W); unsigned k;
        for (k = pm_lo; k <= pm_hi; k++) { uint32_t m = R60_PMLW[k], v = fb[k]; if (v & m) { fb[k] = v & ~m; pm_hit = 1; } }
        r60_pm_clear();
    }
}
#define P5_STM(ptr, v) do { uint32_t vm_ = (v), m_ = R60_PMLW[(unsigned)((uint8_t *)(ptr) - (uint8_t *)row) >> 2]; \
    if (vm_ & m_) { pm_hit = 1; vm_ &= ~m_; } if (vm_) P5_ST0(ptr, vm_); } while (0)
#undef P5_ST
#define P5_ST(ptr, v) P5_STM(ptr, v)
#ifndef R60_PMCODE
#define R60_PMCODE R60CODE __attribute__((noinline)) static   /* the masked rows run from SDRAM; r60_planes.c evicts the plane walk for the room */
#endif
P5_ROWFN_A(r60_plot_row5nm, 0, R60_PMCODE)
P5_ROWFN_A(r60_plot_row5fm, 1, R60_PMCODE)
#undef P5_ST
#define P5_ST(ptr, v) P5_ST0(ptr, v)
#endif
/* ROWFAST is the line's row loop (derived from the flags on the line, not a flag of its own): the
 * PLOT5 row functions with ROWCLR spans and the RECTCLR/KEEPSPR box, zoom by the word form, no
 * priority mask and no probe plotter. Any other set takes the loop with every form below it. */
#if defined(R60_ROWFAST_OPT) && defined(R60_PLOT5) && defined(R60_ROWCLR) && defined(R60_RECTCLR) && (!defined(R60_ZOOM) || defined(R60_ZOOMFAST)) \
    && !defined(R60_PRIOMASK) && !defined(R60_LONGSPR) && !defined(R60_SOLIDSPR) && !defined(R60_WORDPLOT) && !defined(R60_PLOTSTATS)
#define R60_ROWFAST   /* R60ROWFAST=1 opts in (2026-10-23): identity-gated and 0.46 -> 0.51 passes/vint in the fight,
                       * but its rh_g tape run parked the slave at vint ~32750 (s_main's DRAWSEQ test sits before
                       * the FMDOWN timeout close); off the line until that is understood */
#endif
#if defined(R60_PLOT5CHECK) || defined(R60_ZOOMCHECK) || defined(R60_PUNCH2CHECK)
#define R60SPRCODE          /* the check builds: the replay calls (sw308 + ROWFAST) and the punch write log (PUNCH2CHECK) overflow .ramtext, so the plotter runs from cart; correctness only */
#else
#define R60SPRCODE R60CODE
#endif
/* the plotter's per-core reads: under MBAND through the draw context (the master must never
 * read the slave's cached r60_xo nor write its box / pixel count), else the slave statics */
#ifdef R60_MBAND
#define XO_ xo_c
#define RP_ dc->rowpix
#define LB_ dc->box
R60SPRCODE static void r60_sprite(const volatile uint16_t *e, unsigned ylo, unsigned yhi, r60_dc_t *dc)
{
    const unsigned xo_c = dc->xo;
#else
#define XO_ r60_xo
#define RP_ r60_rowpix
#define LB_ r60_last_box
R60SPRCODE static void r60_sprite(const volatile uint16_t *e, unsigned ylo, unsigned yhi,
                               volatile uint32_t *mask, unsigned bank, unsigned mlist)
{
#endif
#ifdef R60_RECTCLR
    unsigned bx0 = FB_W + 512u, bx1 = 0, by0 = FB_H, by1 = 0;
#endif
    int      top   = e[0] & 0xFF, bot = e[0] >> 8;
    int      xpos  = (int)(e[1] & 0x1FF) - 184 + (int)XO_;
    uint16_t d2    = e[2];
    int      pitch = (int)(signed char)(d2 & 0xFF);
    int      flip  = d2 & 0x100;
    unsigned addr  = e[3];
    const uint16_t *sd = altbeast_sprites + R60_SPR_BANK((e[4] >> 8) & 0xF) * 0x10000u;
#ifdef R60_PALMAP
    unsigned base  = (unsigned)(R60_BANK_OF(e[4] & 0x3Fu) << 4);   /* FB byte = bank*16 + pen; bank = this palette's 32X bank */
#else
    unsigned base  = (unsigned)((e[4] & 0x0F) << 4);   /* FB byte = cset*16 + pen */
#endif
    int      y;
#ifdef R60_PLOT5
    const uint16_t *pt5; int lim5 = (int)(FB_W + XO_), xo5 = (int)XO_; unsigned npx5 = 0;   /* per sprite, not per row */
    if (!r60_pt_init) r60_pt_build();
    pt5 = r60_pt[flip ? 1u : 0u][base >> 4];
#endif
#ifdef R60_ZOOM
    /* ZOOM, as the chip does it (jts16_obj_scan.v:78-80, jts16_obj_draw.v:70-93). Vertical:
     * a 5-bit accumulator adds vzoom (e[5] bits 9:5) each row; its carry doubles the
     * NEXT row's pitch, so a source row is skipped and the sprite shrinks. Horizontal:
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
#ifdef R60_ZOOMFAST
    uint8_t zkn[16];                              /* the pattern as 16 nibbles, once a sprite (was per row inside the row function) */
#endif
    if (hz) {
        unsigned k, acc = (hz & 0xFu) << 2;
        for (k = 0; k < 64u; k++) {
            unsigned sum = acc + hz;
            acc = sum & 0x3Fu;
            if (!(sum & 0x40u)) { if (k < 32u) keep_lo |= 1u << k; else keep_hi |= 1u << (k - 32u); }
        }
#ifdef R60_ZOOMFAST
        { uint32_t kl = keep_lo, kh = keep_hi; for (k = 0; k < 8u; k++) { zkn[k] = (uint8_t)(kl & 15u); kl >>= 4; zkn[8u + k] = (uint8_t)(kh & 15u); kh >>= 4; } }
#endif
    }
#endif

    if (top >= bot || xpos >= (int)(FB_W + XO_))
        return;
#if defined(R60_PRIOMASK) && defined(R60_PRIO)
    pm_hit = 0; prio_ymin = FB_H; prio_ymax = 0;   /* this sprite's masked rows, for the keep logic */
#ifdef R60_PRIOINLINE
    pmc_cy = 0xFFFFFFFFu;                          /* the FG may have changed since the last sprite */
#endif
#endif
#if defined(R60_PRIOROW) && defined(R60_PRIO)
    pr_hit = 0; prio_ymin = FB_H; prio_ymax = 0; pr_over = 1; pr_ncy = 0;
#ifdef R60_ZOOM
    if (!hz)
#endif
    {   int w4 = (pitch < 0 ? -pitch : pitch) * 4 + 4;
        r60_pr_scan(xpos, xpos + w4, top, bot);        /* zoomed sprites keep the box punch */
    }
#endif
#ifdef R60_PLOTSTATS
    r60_plot_stats[5]++;
#endif
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
#ifdef R60_MBAND
        uint32_t sp_hdr = ((uint32_t)(dc->cur & 63u) << 16) | ((uint32_t)(xs >> 2) << 8);   /* this core's list, through its context */
        uint32_t *spw = &dc->spans[dc->n], *sp_end = &dc->spans[dc->cap];
#else
        unsigned bk = bank & 1u;
        uint32_t sp_hdr = ((uint32_t)(ks_cur & 63u) << 16) | ((uint32_t)(xs >> 2) << 8);
        uint32_t *spw = &r60_rowspans[bk][r60_rowspan_n[bk]], *sp_end = &r60_rowspans[bk][768];
#endif
#ifdef R60_PLOT5CHECK
        unsigned sbx0 = FB_W + 512u, sbx1 = 0, sby0 = FB_H, sby1 = 0, srows = 0;
        for (y = top; y < bot; y++) if ((unsigned)y >= ylo && (unsigned)y < yhi) srows++;
#endif
#ifdef R60_ZOOM
        (void)vcarry;
#define RF_VSTEP() do { vacc += vz; if (vacc & 0x20u) { addr = (unsigned)(addr + pitch); vacc &= 0x1Fu; } } while (0)
#else
#define RF_VSTEP() do { } while (0)
#endif
#define RF_STEP() do { addr = (unsigned)(addr + pitch); RF_VSTEP(); } while (0)
#ifdef R60_PLOT5CHECK
        /* the old per-row formulas, as the shadow: the span word, the box, the band */
#define RF_SHADOW(xe, had, w) do { \
            int oxs_ = x0 < 0 ? 0 : (x0 & ~3), oxe_ = ((int)(xe) + 3) & ~3; if (oxe_ > (int)(FB_W + 512u)) oxe_ = (int)(FB_W + 512u); \
            unsigned ohad_ = oxe_ > oxs_; \
            uint32_t ow_ = ((uint32_t)(unsigned)y << 24) | ((uint32_t)(ks_cur & 63u) << 16) | ((uint32_t)(oxs_ >> 2) << 8) | (uint32_t)(oxe_ >> 2); \
            if (ohad_ != (had) || (ohad_ && ow_ != (w))) r60_span_bad++; \
            if ((int)(xe) > 0) { unsigned sxe_ = (unsigned)(int)(xe) > FB_W + XO_ ? FB_W + XO_ : (unsigned)(int)(xe); if (sxe_ > sbx1) sbx1 = sxe_; } \
            if (x0 > (int)XO_ && (unsigned)x0 < sbx0) sbx0 = (unsigned)x0; else if (x0 <= (int)XO_) sbx0 = XO_; \
            if ((unsigned)y < sby0) sby0 = (unsigned)y; if ((unsigned)y + 1u > sby1) sby1 = (unsigned)y + 1u; \
            if ((unsigned)y < ylo || (unsigned)y >= yhi) r60_span_bad += 0x100000u; else srows--; } while (0)
#else
#define RF_SHADOW(xe, had, w) do { } while (0)
#endif
        /* the span and the box of a drawn row that ended at xe (R60_ROWSPAN_REC(xpos, xe), hoisted) */
#define RF_SPAN(xe) do { int xe4_ = ((int)(xe) + 3) & ~3; unsigned had_ = 0; uint32_t w_ = 0; if (xe4_ > cap) xe4_ = cap; \
            if (xe4_ > xs) { had_ = 1; w_ = sp_hdr | ((uint32_t)(unsigned)y << 24) | (uint32_t)(xe4_ >> 2); \
                if (spw < sp_end) *spw++ = w_; else sp_full = 1; } \
            if ((int)(xe) > xmax) xmax = (int)(xe); RF_SHADOW(xe, had_, w_); } while (0)
#ifdef R60_PLOT5CHECK
#define RF_CHECK5() do { unsigned k2, xa5, xb5; uint8_t *ra = r60_p5_a, *rb = r60_p5_b; unsigned dummy = 0; \
                for (k2 = 0; k2 < 512u; k2++) { ra[k2] = 0; rb[k2] = 0; } \
                xa5 = r60_plot_row5(sd, addr, flip ? 1 : 0, x0, (volatile uint8_t *)ra, lim5, xo5, pt5, &dummy); \
                xb5 = r60_plot_row3ref(sd, addr, flip ? 1 : 0, x0, rb, lim5, xo5, pt5); \
                r60_plot5_rows++; \
                if (xa5 != xb5) r60_plot5_bad += 0x10000u; \
                for (k2 = 0; k2 < 512u; k2++) if (ra[k2] != rb[k2]) { r60_plot5_bad++; break; } } while (0)
#else
#define RF_CHECK5() do { } while (0)
#endif
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
#ifdef R60_ZOOM
        if (hz) {
            if (x0 >= xo) {                          /* the word form: the row starts on the canvas */
                unsigned zpx = 0;
                for (y = ys; y < ye; y++) {
                    volatile uint8_t *row; unsigned xe;
                    RF_STEP();
                    row = FB_OVR((unsigned)y * FB_W);
#ifdef R60_ZOOMCHECK
                    {   unsigned k2, xa, xb, nd = 0, lim2 = FB_W + XO_;
                        for (k2 = 0; k2 < 512u; k2++) { r60_zc_a[k2] = 0; r60_zc_b[k2] = 0; }
                        xa = flip ? r60_zoom_rowc_f(sd, addr, x0, (volatile uint8_t *)r60_zc_a, (int)lim2, pt5, zkn, &nd)
                                  : r60_zoom_rowc_n(sd, addr, x0, (volatile uint8_t *)r60_zc_a, (int)lim2, pt5, zkn, &nd);
                        xb = r60_zoom_row_ref(sd, addr, flip ? 1 : 0, x0, r60_zc_b, (int)lim2, xo, base, keep_lo, keep_hi);
                        nd = 0;
                        for (k2 = 0; k2 < lim2 && k2 < 512u; k2++) if (r60_zc_a[k2] != r60_zc_b[k2]) nd++;
                        r60_zoom_bad += nd; r60_zoom_rows++; if (xa != xb) r60_zoom_badx++;
                    }
#endif
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
#endif
        if (flip) RF_ROWS5(r60_plot_row5f) else RF_ROWS5(r60_plot_row5n)
        if (xmax > 0) { unsigned xe2 = (unsigned)xmax > FB_W + XO_ ? FB_W + XO_ : (unsigned)xmax; if (xe2 > bx1) bx1 = xe2; }
#ifdef R60_MBAND
        dc->n = (unsigned)(spw - dc->spans);
        if (sp_full) dc->full = 1;
#else
        r60_rowspan_n[bk] = (uint16_t)(spw - r60_rowspans[bk]);
        if (sp_full) r60_rowspan_full[bk] = 1;
#endif
#ifdef R60_PLOT5CHECK
        if (srows != 0 || sbx0 != bx0 || sbx1 != bx1 || sby0 != by0 || sby1 != by1) r60_span_bad += 0x10000u;
#endif
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
#ifdef R60_ZOOM
        /* MAME sega16sp.cpp:941-947 ("100% accurate to the real board"): the 5-bit sum
         * carries in THIS row and the extra pitch is taken before the row is drawn. Our FB
         * rows for the eye scene's logo equal a simulation of this on the same record. */
        vacc += vz; if (vacc & 0x20u) { addr = (unsigned)(addr + pitch); vacc &= 0x1Fu; }
        (void)vcarry;
#endif
        o = addr;
        int x = xpos;
        if ((unsigned)y < ylo || (unsigned)y >= yhi) {
#ifdef R60_PLOTSTATS
            r60_plot_stats[4]++;
#endif
            continue;                       /* outside this CPU's band; the row still steps addr */
        }
        row = FB_OVR((unsigned)y * FB_W);
#ifdef R60_RECTCLR
        if (x > (int)XO_ && (unsigned)x < bx0) bx0 = (unsigned)x; else if (x <= (int)XO_) bx0 = XO_;
        if ((unsigned)y < by0) by0 = (unsigned)y;
        if ((unsigned)y + 1u > by1) by1 = (unsigned)y + 1u;
#else
        R60_ROWSET(mask, (unsigned)y);       /* this bank now holds pixels on this row */
#endif
#ifdef R60_LONGSPR
        /* PROBE B: LONGWORD STORES THROUGH THE DIRECT IMAGE, four pixels at a time --
         * the idiom the old blit used (blit_half, mov.l) and the only writer this repo
         * has ever proven lands on this board, because marks2 drew a correct picture
         * there. Opaque: a transparent pen becomes the set's base+1, which is wrong
         * art and fine for a density test. Only 4-aligned groups are written.
         *
         * THE CORE'S OWN RULE, quoted, VDP.sv:513-514, which is also why this probe is
         * shaped like this:
         *   FB_DRAW_WE[1] = FB_WR & FIFO_FB_WE[1] &
         *       ( (|FIFO_FB_D[15:8] &  FIFO_FB_A[17])            // overwrite image
         *       | ((|FIFO_FB_D[15:8] | FIFO_FB_WE[0]) & ~FIFO_FB_A[17]) )  // direct
         * A[17] IS the overwrite flag and the zero-byte rule is applied PER LANE, so
         * byte writes to the overwrite image are handled correctly by the core. But in
         * the DIRECT image a byte write whose value is ZERO is dropped unless the other
         * lane is also enabled -- i.e. unless it was a word or longer write. */
        {
            volatile uint32_t *rl = (volatile uint32_t *)row;
            x &= ~3;                     /* 4-align the whole run: the position is off
                                          * by up to 3 px, which a DENSITY test can
                                          * afford and the first cut could not -- it
                                          * wrote only the groups that happened to land
                                          * aligned and drew nothing at all on ares. */
            for (;;) {
                uint16_t w = flip ? sd[o--] : sd[o++];
                unsigned n, pix = 0, acc = 0;
                for (n = 0; n < 4; n++) {
                    unsigned b;
                    pix = flip ? ((w >> (n * 4)) & 0xF) : ((w >> (12 - n * 4)) & 0xF);
                    b = ((unsigned)(pix - 1) < 14u) ? (unsigned)(base + pix)
                                                    : (unsigned)(base + 1u);
                    acc = (acc << 8) | b;
                }
                if (x >= 0 && x + 3 < (int)FB_W) {
                    rl[(unsigned)x >> 2] = acc;
                    RP_ += 4;
                }
                x += 4;
                if (pix == 15) break;
                if (x >= (int)(FB_W + XO_)) break;
            }
            continue;
        }
#endif
#ifdef R60_SOLIDSPR
        /* DRAW THE RECORD WITHOUT READING ANY ART. The board renders the sprites
         * correctly SHAPED and correctly PLACED at a constant ~40% density, and that
         * does not move with the write width, the write rate, the framebuffer image or
         * the write order. The one input never questioned is the ART ITSELF: the slave
         * reads it from CART, and cart access is the MiSTer core's known weak point
         * (the ROM DTACK wait, IF.sv USE_ROM_WAIT). Shapes stay right under a bad art
         * read because the rows and columns come from the RECORD, not from the art.
         * So: fill 32 columns of each row from the record alone. A solid bar on the
         * board says the writes are fine and the art read is not. */
        {
            unsigned xx;
            for (xx = 0; xx < 32u; xx++) {
                int px = x + (int)xx;
                if (px >= 0 && px < (int)(FB_W + XO_)) { row[px] = (uint8_t)(base + 1u); RP_++; }
            }
            continue;
        }
#endif
#ifdef R60_WORDPLOT
        /* WORD STORES THROUGH THE OVERWRITE IMAGE, WHICH IS WHAT IT IS FOR.
         *
         * The 32X framebuffer is 16 bits wide and the overwrite image exists so that a
         * WORD write can carry a transparent pixel: the hardware drops a zero byte of
         * the pair and leaves what is under it. A per-PIXEL BYTE write is asking for
         * something else, and the board does not honour it. Two board readings, each
         * three launches, say so: through the overwrite image a byte at a time, the
         * sprites come out correctly shaped and almost entirely WHITE; through the
         * direct image a byte at a time, they come out in their colours (3,609
         * coloured against 1,317 white) but at about 40% density -- which is adjacent
         * pixels sharing a word and one byte of each pair losing the other.
         *
         * So build the pair and store it as a word. A transparent pixel contributes a
         * zero byte and the overwrite image preserves what is under it, which is the
         * same result the per-pixel test gave and the reason this needs no read-back.
         * A leading or trailing odd pixel stores a word whose other byte is zero.
         *
         * KNOWN COST, ACCEPTED: a word-store plot measured 1.059x SLOWER than four
         * byte stores on ares, because the SH-2 has no barrel shift. The board is the
         * gate and the byte plot does not render there.
         *
         * (The two are built from one source and must agree on ares bit for bit; that
         * is the check before any board reading is believed.) */
        {
            volatile uint16_t *rw = (volatile uint16_t *)row;
            unsigned hold = 0;
            for (;;) {
                uint16_t w = flip ? sd[o--] : sd[o++];
                unsigned n, pix = 0;
                for (n = 0; n < 4; n++) {
                    unsigned b;
                    pix = flip ? ((w >> (n * 4)) & 0xF) : ((w >> (12 - n * 4)) & 0xF);
                    b = ((unsigned)(pix - 1) < 14u) ? (unsigned)(base + pix) : 0u;
                    if (x & 1) {
                        /* x >= 1, not x >= 0: the pair covers x-1 and x, and at x=-1
                         * the index (unsigned)(-1)>>1 is an enormous one. Caught by
                         * the bit-exactness check against the byte plot -- 28 bytes
                         * differed and 8 pixels appeared that the byte plot clips. */
                        if (x >= (int)XO_ + 1 && x < (int)(FB_W + XO_) && (hold | b))
                            rw[(unsigned)x >> 1] = (uint16_t)((hold << 8) | b);
                        hold = 0;
                    } else {
                        hold = (x >= (int)XO_ && x < (int)(FB_W + XO_)) ? b : 0u;
                    }
                    if (b && x >= (int)XO_ && x < (int)(FB_W + XO_)) RP_++;
                    x++;
                }
                if (pix == 15) break;
                if (x >= (int)(FB_W + XO_)) break;
            }
            if ((x & 1) && hold && x >= 1 && (x - 1) < (int)(FB_W + XO_))
                rw[(unsigned)(x - 1) >> 1] = (uint16_t)(hold << 8);
            continue;
        }
#endif
#ifdef R60_ZOOM
        if (hz) {
            unsigned k = 0;
#ifdef R60_ZOOMFAST
            if (x >= (int)XO_) {
                unsigned zpx = 0;
#ifdef R60_ZOOMCHECK
                {   unsigned k2, xa, xb, nd = 0, lim2 = FB_W + XO_;
                    for (k2 = 0; k2 < 512u; k2++) { r60_zc_a[k2] = 0; r60_zc_b[k2] = 0; }
                    xa = flip ? r60_zoom_rowc_f(sd, o, x, (volatile uint8_t *)r60_zc_a, (int)lim2, pt5, zkn, &nd)
                              : r60_zoom_rowc_n(sd, o, x, (volatile uint8_t *)r60_zc_a, (int)lim2, pt5, zkn, &nd);
                    xb = r60_zoom_row_ref(sd, o, flip ? 1 : 0, x, r60_zc_b, (int)lim2, (int)XO_, base, keep_lo, keep_hi);
                    nd = 0;
                    for (k2 = 0; k2 < lim2 && k2 < 512u; k2++) if (r60_zc_a[k2] != r60_zc_b[k2]) nd++;
                    r60_zoom_bad += nd; r60_zoom_rows++; if (xa != xb) r60_zoom_badx++;
                }
#endif
                x = (int)(flip ? r60_zoom_row_f(sd, o, x, row, (int)(FB_W + XO_), pt5, zkn, &zpx)
                               : r60_zoom_row_n(sd, o, x, row, (int)(FB_W + XO_), pt5, zkn, &zpx));
                RP_ += zpx;
            } else {
                x = (int)r60_zoom_row_ref(sd, o, flip ? 1 : 0, x, row, (int)(FB_W + XO_), (int)XO_, base, keep_lo, keep_hi);
            }
            (void)k;
            if (0) {
#endif
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
#ifdef R60_ZOOMFAST
            }
#endif
#ifdef R60_ROWCLR
            R60_ROWSPAN_REC(xpos, x);            /* zoomed rows are cleared like any other (sw139: never recorded, never cleared, 30 Hz ghosts) */
#endif
#ifdef R60_RECTCLR
            if (x > 0) { unsigned xe = (unsigned)x > FB_W + XO_ ? FB_W + XO_ : (unsigned)x; if (xe > bx1) bx1 = xe; }
#endif
            continue;
        }
#endif
#ifdef R60_PLOT5
        {
            const uint16_t *pt = pt5;
            unsigned xe;
#ifdef R60_PLOT5CHECK
            {   unsigned k2, xa5, xb5; uint8_t *ra = r60_p5_a, *rb = r60_p5_b; unsigned dummy = 0;
                for (k2 = 0; k2 < 512u; k2++) { ra[k2] = 0; rb[k2] = 0; }
                xa5 = r60_plot_row5(sd, o, flip ? 1 : 0, x, (volatile uint8_t *)ra, (int)(FB_W + XO_), (int)XO_, pt, &dummy);
                xb5 = r60_plot_row3ref(sd, o, flip ? 1 : 0, x, rb, (int)(FB_W + XO_), (int)XO_, pt);
                r60_plot5_rows++;
                if (xa5 != xb5) r60_plot5_bad += 0x10000u;
                for (k2 = 0; k2 < 512u; k2++) if (ra[k2] != rb[k2]) { r60_plot5_bad++; break; }
            }
            /* the real row still goes through PLOT3 below in the check build */
            goto p5_plot3;
#endif
#if defined(R60_PRIOINLINE) && defined(R60_PRIO)
            {   int xb = x + 4 * (pitch < 0 ? -pitch : pitch) + 4;
                if (r60_pm_build((unsigned)y, x, xb)) {
                    xe = flip ? r60_plot_row5fm(sd, o, x, row, lim5, xo5, pt, &npx5) : r60_plot_row5nm(sd, o, x, row, lim5, xo5, pt, &npx5);
                    r60_pm_clear(); r60_pm_rows++;
                } else
                    xe = flip ? r60_plot_row5f(sd, o, x, row, lim5, xo5, pt, &npx5) : r60_plot_row5n(sd, o, x, row, lim5, xo5, pt, &npx5);
                if ((int)xe > xb && xb < lim5) { r60_pm_over++; r60_pm_tail((unsigned)y, xb, (int)xe); }
            }
#elif defined(R60_PRIOMASK) && defined(R60_PRIO)
            {   volatile uint8_t *lb = (volatile uint8_t *)R60_LINEBUF; int xa0 = x;
                xe = flip ? r60_plot_row5f(sd, o, x, lb, lim5, xo5, pt, &npx5) : r60_plot_row5n(sd, o, x, lb, lim5, xo5, pt, &npx5);
                r60_pm_row((unsigned)y, xa0, (int)xe);
            }
#elif defined(R60_PRIOROW) && defined(R60_PRIO)
            {   unsigned masked = (!pr_over && pr_ncy) ? r60_pr_row((unsigned)y, x, x + 4 * (pitch < 0 ? -pitch : pitch) + 4) : 0u;
                xe = flip ? r60_plot_row5f(sd, o, x, row, lim5, xo5, pt, &npx5) : r60_plot_row5n(sd, o, x, row, lim5, xo5, pt, &npx5);
                if (masked) r60_pr_restore();
            }
#else
            xe = flip ? r60_plot_row5f(sd, o, x, row, lim5, xo5, pt, &npx5) : r60_plot_row5n(sd, o, x, row, lim5, xo5, pt, &npx5);
#endif
#ifdef R60_ROWCLR
            R60_ROWSPAN_REC(x, (int)xe);
#endif
            x = (int)xe;
        }
#ifdef R60_RECTCLR
        if (x > 0) { unsigned xe2 = (unsigned)x > FB_W + XO_ ? FB_W + XO_ : (unsigned)x; if (xe2 > bx1) bx1 = xe2; }
#endif
        continue;
#ifdef R60_PLOT5CHECK
    p5_plot3:
#endif
#endif
#ifdef R60_PLOT4
        {
            const uint16_t *pt = r60_pt[flip ? 1u : 0u][base >> 4];
            unsigned xa;
            if (!r60_pt_init) r60_pt_build();
#ifdef R60_PLOTSTATS
            r60_plot_stats[3]++;
#endif
            for (;;) {
                uint16_t w = flip ? sd[o--] : sd[o++];
                unsigned last = flip ? (unsigned)(w >> 12) : (unsigned)(w & 0xFu);
                uint32_t acc;
#ifdef R60_PLOTSTATS
                r60_plot_stats[0]++; if (!w) r60_plot_stats[1]++;
#endif
                acc = flip ? (((uint32_t)pt[w & 0xFFu] << 16) | pt[w >> 8])
                           : (((uint32_t)pt[w >> 8] << 16) | pt[w & 0xFFu]);
                if (acc && x > (int)XO_ - 4 && x < (int)(FB_W + XO_)) {
#ifdef R60_PLOTSTATS
                    r60_plot_stats[2]++;
#endif
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
#ifdef R60_RECTCLR
        if (x > 0) { unsigned xe = (unsigned)x > FB_W + XO_ ? FB_W + XO_ : (unsigned)x; if (xe > bx1) bx1 = xe; }
#endif
        continue;
#endif
#ifdef R60_PLOT3
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
#ifdef R60_RECTCLR
        if (x > 0) { unsigned xe = (unsigned)x > FB_W + XO_ ? FB_W + XO_ : (unsigned)x; if (xe > bx1) bx1 = xe; }
#endif
        continue;
#endif
#ifdef R60_PLOT2
        /* PLOT2: constant shifts (the SH-2 has no barrel shifter; `w >> (12 - n*4)` with
         * a loop variable compiled to a libgcc call per pixel, 19% of the plot on the
         * ares profile), the flip test hoisted out of the pixel loop, and one unsigned
         * compare for both clip edges. Same semantics as the loop below. */
#define R60_PX(sh) do { unsigned pix_ = ((unsigned)w >> (sh)) & 0xFu;                   \
            if ((unsigned)(pix_ - 1u) < 14u && (unsigned)(x - (int)XO_) < FB_W) {                    \
                row[x] = (uint8_t)(base + pix_); RP_++; }                         \
            x++; } while (0)
        if (!flip) {
            for (;;) {
                uint16_t w = sd[o++];
                R60_PX(12); R60_PX(8); R60_PX(4); R60_PX(0);
                if ((w & 0xFu) == 15u || x >= (int)(FB_W + XO_)) break;
            }
        } else {
            for (;;) {
                uint16_t w = sd[o--];
                R60_PX(0); R60_PX(4); R60_PX(8); R60_PX(12);
                if ((w >> 12) == 15u || x >= (int)(FB_W + XO_)) break;
            }
        }
#undef R60_PX
#ifdef R60_RECTCLR
        if (x > 0) { unsigned xe = (unsigned)x > FB_W + XO_ ? FB_W + XO_ : (unsigned)x; if (xe > bx1) bx1 = xe; }
#endif
        continue;
#endif
        for (;;) {
            uint16_t w = flip ? sd[o--] : sd[o++];
            unsigned n, pix = 0;
            /* FOUR NIBBLES A WORD. Forward reads them high to low; h-flip walks the
             * SOURCE backward and the nibbles low to high, while x still advances --
             * that asymmetry is the arcade's and m_main.c's NIB/flip pair had it too. */
            for (n = 0; n < 4; n++) {
                pix = flip ? ((w >> (n * 4)) & 0xF) : ((w >> (12 - n * 4)) & 0xF);
                if ((unsigned)(pix - 1) < 14u && x >= (int)XO_ && x < (int)(FB_W + XO_)) {
#ifdef R60_ONEPEN
                    /* ONE PEN FOR EVERY SPRITE PIXEL. The board draws the right shapes
                     * in the right places and renders them almost entirely WHITE, which
                     * is either a framebuffer holding the wrong indices or a palette
                     * holding the wrong colours -- and from a screenshot those look the
                     * same. With one constant index, a solid single-coloured figure
                     * says the framebuffer is right and the per-pen lookup is not. */
                    row[x] = (uint8_t)R60_ONEPEN;
#else
                    row[x] = (uint8_t)(base + pix);
#endif
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
#ifdef R60_PLOT5
    RP_ += npx5;
#endif
#ifdef R60_KEEPSPR
    if (by1 > by0 && bx1 > bx0) { LB_[0] = (uint16_t)bx0; LB_[1] = (uint16_t)bx1; LB_[2] = (uint16_t)by0; LB_[3] = (uint16_t)by1; }
    else { LB_[0] = 0; LB_[1] = 0; LB_[2] = 0; LB_[3] = 0; }
#endif
#if defined(R60_RECTCLR) && !defined(R60_MBAND)   /* under MBAND + ROWCLR the box lists' memory holds the master's spans */
    if (by1 > by0 && bx1 > bx0) {
        volatile uint16_t *rc = mlist ? R60_MRECTS(bank) : R60_RECTS(bank);
        unsigned n = mlist ? R60_MRECTN(bank) : R60_RECTN(bank);
        if (n < 64u) {
            rc[n * 4] = (uint16_t)bx0; rc[n * 4 + 1] = (uint16_t)bx1; rc[n * 4 + 2] = (uint16_t)by0; rc[n * 4 + 3] = (uint16_t)by1;
            if (mlist) R60_MRECTN(bank) = (uint16_t)(n + 1u); else R60_RECTN(bank) = (uint16_t)(n + 1u);
        }
    }
    (void)mask;
#endif
}

#ifdef R60_MBAND
/* THE SLAVE'S VIEW OF ITS DRAW STATE, by the names the pass below has always used */
#define ks_cur       (r60_dcs.cur)
#define r60_last_box (r60_dcs.box)
#define prio_ymin    (r60_dcs.ymin)
#define prio_ymax    (r60_dcs.ymax)
#define r60_rowpix   (r60_dcs.rowpix)
static unsigned r60_split_s;             /* this pass's split row, the slave's copy */
#ifdef R60_MBAND_TUNE
static uint16_t r60_clr_t;               /* the slave's FRT at CLEARED (r60_band_post) */
#endif
#ifndef R60_MBAND_FLIPA
static unsigned r60_flip_due;            /* the window's flip, written once the 68K has acked the span */
#define R60_FLIP_NOW() do { if (r60_flip_due) { MARS_VDP_FBCTL = r60_fs_want; *(volatile uint16_t *)0x20004102u = R60_SHIFTNEXT; \
    r60_flip_due = 0; R60_SPANV = (uint16_t)R60_DIAG[0]; } } while (0)
#else
#define R60_FLIP_NOW() ((void)0)
#endif
/* THE BAND DESCRIPTOR, then CLEARED (slave, cart): the master draws rows 0..split-1 of every
 * unkept record from here on, with this pass's offset, palette map and FG scroll. Posted after
 * the palette map (R60_BANK_OF is read per sprite) and the offset, never before. */
static __attribute__((noinline)) void r60_band_post(unsigned bkp, unsigned hold, uint16_t go)
{
    unsigned k7, split7 = R60_SPLIT(R60_DRAWSEQ);
    for (k7 = 0; k7 < 64u; k7++) { R60_BD_KEEP[k7] = (uint8_t)(ks_keep[k7] | (ks_repunch[k7] << 1)); R60_BD_PYR[k7] = ks_pyr[bkp][k7]; }
    r60_dcs.xo = r60_xo; r60_dcs.pxs = prio_xs; r60_dcs.pys = prio_ys; r60_dcs.psel = prio_psel; r60_dcs.master = 0;
    R60_BD[0] = (uint16_t)bkp; R60_BD[1] = (uint16_t)split7; R60_BD[2] = (uint16_t)r60_xo;
    R60_BD[3] = (uint16_t)prio_xs; R60_BD[4] = (uint16_t)prio_ys; R60_BD[5] = (uint16_t)prio_psel;
    R60_BD[6] = (uint16_t)hold; R60_BD[7] = (uint16_t)r60_prio_init;
    r60_split_s = split7;
#ifdef R60_MBTIME
    MBT(go, 4); MBTV(go, 15, R60_MBTG[0]); MBTV(go, 22, R60_DIAG[0]);
#endif
#ifdef R60_MBAND_TUNE
    r60_clr_t = r60_frt();
#endif
    R60_CLEARED = go;                    /* the master may draw its band now */
}
#ifdef R60_MBAND_TUNE
/* THE SPLIT, TUNED BY WHAT THE LAST BAND COST (R60MBANDTUNE, 2026-10-24; slave, cart). R60MBTIME read
 * the fight at a third of the rows: the master's band 54 lines, the slave's sprites 165 after CLEARED,
 * and the best static share (9/16) still left the slave waiting when the master's tick ran late. Per
 * pass, from durations only (each on its own core's clock): ds = the slave's sprites since CLEARED,
 * dm = the master's band, w = the slave's wait for MDONE. Per-row cost of each side at this share,
 * Cs = ds / (1 - f), Cm = dm / f; the master's start lag L = ds + w - dm when the slave waited (else
 * taken as 0: MDONE stood when we looked); the share that ends both together: f* = (Cs - L) / (Cs + Cm). f moves half way. */
static __attribute__((noinline)) void r60_band_tune(uint16_t t_spr, uint16_t w)
{
    unsigned f = R60_MBAND_F, ds = (uint16_t)(t_spr - r60_clr_t), dm = R60_MBDUR, cs, cm, L = 0, fs;
    if (f < 16u || f > 240u) f = R60_MBAND_F0;
    if (!ds || !dm) return;
    cs = ds * 256u / (256u - f); cm = dm * 256u / f;
    if (w > 368u && ds + w > dm) L = ds + w - dm;   /* waited (2 lines: the poll's own cost is ~20 ticks): the master ended w after us */
    fs = cs > L ? (cs - L) * 256u / (cs + cm) : 16u;
    if (fs < 16u) fs = 16u;
    if (fs > 240u) fs = 240u;
    R60_MBAND_F = (uint16_t)((f + fs) / 2u);
}
#endif
/* THE MASTER'S BOOKKEEPING, MERGED after MDONE (slave, cart): its box per drawn record widens
 * ks_box (the keep logic's overlap tests need the whole sprite), its punched rows join ks_pyr */
static __attribute__((noinline)) void r60_band_merge(unsigned bkp)
{
    unsigned k8, n8 = ks_n[bkp];
    for (k8 = 0; k8 < n8; k8++) {
        unsigned m1 = R60_MBOX[k8 * 2u + 1u], mp = R60_MPYR[k8];
        if (m1) {
            uint16_t *b = ks_box[bkp][k8];
            unsigned m0 = R60_MBOX[k8 * 2u], mx0 = (m0 & 0xFFu) << 2, my0 = m0 >> 8, mx1 = (m1 & 0xFFu) << 2, my1 = m1 >> 8;
            if (b[1] > b[0]) { if (mx0 < b[0]) b[0] = (uint16_t)mx0; if (mx1 > b[1]) b[1] = (uint16_t)mx1; if (my0 < b[2]) b[2] = (uint16_t)my0; if (my1 > b[3]) b[3] = (uint16_t)my1; }
            else { b[0] = (uint16_t)mx0; b[1] = (uint16_t)mx1; b[2] = (uint16_t)my0; b[3] = (uint16_t)my1; }
        }
        if (mp) {
            unsigned p = ks_pyr[bkp][k8], y0 = mp >> 8, y1 = mp & 0xFFu;
            if (p) { if ((p >> 8) < y0) y0 = p >> 8; if ((p & 0xFFu) > y1) y1 = p & 0xFFu; }
            ks_pyr[bkp][k8] = (uint16_t)((y0 << 8) | y1); ks_punched[bkp][k8] = 1;
        }
    }
}
/* the slave draws through its context: the span cursor is the bank's list, synced around the call */
#define R60_SPRITE_S(e_, ylo_, yhi_) do { \
    r60_dcs.spans = r60_rowspans[bkp]; r60_dcs.n = r60_rowspan_n[bkp]; r60_dcs.cap = R60_SSPAN_CAP; r60_dcs.full = 0; \
    r60_sprite((e_), (ylo_), (yhi_), &r60_dcs); \
    r60_rowspan_n[bkp] = (uint16_t)r60_dcs.n; if (r60_dcs.full) r60_rowspan_full[bkp] = 1; } while (0)
#define R60_PUNCH(box_)          r60_punch(&r60_dcs, (box_))
#define R60_PUNCH_SPANS(n0_, n1_) r60_punch_spans(&r60_dcs, (n0_), (n1_))
/* THE BAND'S RECORDS (cart): rows 0..split-1 of every unkept record and the re-punch of a kept one,
 * through dc into the master's span list, boxes and punched rows. The master's, or the slave's when
 * it takes the band over (R60MBANDSTEAL). Returns the sprites drawn. */
static __attribute__((noinline)) unsigned r60_band_loop(r60_dc_t *dc, unsigned bank, unsigned split)
{
    unsigned i2, nspr = 0;
    const volatile uint16_t *tbl;
    dc->xo = R60_BD[2]; dc->pxs = R60_BD[3]; dc->pys = R60_BD[4]; dc->psel = R60_BD[5];
    dc->spans = (uint32_t *)R60_MSPANS(bank); dc->n = R60_MSPAN_N(bank); dc->cap = R60_MSPAN_CAP; dc->full = 0;
    tbl = R60_SNAPSLOT(R60_DRAWSEQ);
    for (i2 = 0; i2 < 64u; i2++) {
        const volatile uint16_t *e2 = tbl + i2 * 8;
        uint16_t d3 = e2[2];
        unsigned kf, punched;
        if (d3 & 0x8000u) break;
        R60_MPYR[i2] = 0; R60_MBOX[i2 * 2u + 1u] = 0;
        kf = R60_BD_KEEP[i2];
        dc->cur = i2;
        if (kf & 1u) {                            /* kept: its pixels stand; re-punch rows above the split only */
            unsigned pyr = R60_BD_PYR[i2], by0 = pyr >> 8, by1 = pyr & 0xFFu, n0 = dc->n;
            if (!(kf & 2u) || by0 >= split) continue;
            if (by1 > split) by1 = split;
            r60_sprite(e2, by0, by1, dc);
            if (!dc->full) punched = r60_punch_spans(dc, n0, dc->n);
            else punched = dc->box[1] > dc->box[0] && r60_punch(dc, dc->box);
            if (punched && dc->ymax > dc->ymin) R60_MPYR[i2] = (uint16_t)((dc->ymin << 8) | dc->ymax);
            continue;
        }
        if (d3 & 0x4000u) continue;               /* skipped */
        r60_sprite(e2, 0u, split, dc);
        nspr++;
        punched = dc->box[1] > dc->box[0] && r60_punch(dc, dc->box);
        if (dc->box[1] > dc->box[0]) {            /* 4-px granular, rounded outward: a larger box keeps less, never wrongly */
            R60_MBOX[i2 * 2u] = (uint16_t)((dc->box[0] >> 2) | (dc->box[2] << 8));
            R60_MBOX[i2 * 2u + 1u] = (uint16_t)((((unsigned)dc->box[1] + 3u) >> 2) | (dc->box[3] << 8));   /* last: the slave reads it as the entry's validity */
        }
        if (punched && dc->ymax > dc->ymin) R60_MPYR[i2] = (uint16_t)((dc->ymin << 8) | dc->ymax);
    }
    R60_MSPAN_N(bank) = (uint16_t)dc->n;
    if (dc->full) { R60_MSPAN_FULL(bank) = 1; R60_MB[5]++; }
    return nspr;
}
/* THE MASTER'S BAND (cart: the loop is 64 iterations; the plotter and the punch it calls are .ramtext).
 * Runs when the slave has posted CLEARED for the current pass and no MDONE stands for it. */
static __attribute__((noinline)) void r60_band(void)
{
    uint16_t go2, t0 = r60_frt();
    unsigned bank, split, hold, nspr = 0;
    r60_dc_t *dc = &r60_dcm;
    /* GO READ ONCE (2026-10-24, the sw311 ghost): `CLEARED == GO && MDONE != GO` read GO twice. When the
     * slave advanced GO between the two reads (CLEARED = MDONE = n, GO n -> n+1) the test passed for n+1
     * before its clear and its descriptor: the master drew the new snapshot with the LAST pass's bank and
     * split into the bank the slave was about to clear, logged the spans in the other bank's list (which
     * overflowed) and posted MDONE early. Every pixel the slave's clear did not happen to cover stayed in
     * that bank for good (ares g0 frame 586: GO 381 v224 h20, MSTART 381 h74, CLEARED 381 only at 587 v34). */
    go2 = R60_GO;
    if (!(R60_CLEARED == go2 && R60_MDONE != go2)) return;
    bank = R60_BD[0] & 1u; split = R60_BD[1]; hold = R60_BD[6];
    R60_EVPUT(7);                                 /* band start */
#ifdef R60_MBTIME
    MBST(4); MBT(go2, 10); MBTV(go2, 12, R60_TVINT); MBTV(go2, 13, R60_DIAG[0]); MBTV(go2, 14, R60_MBTG[3]); MBTV(go2, 16, R60_MBTG[4]);
    MBTV(go2, 20, R60_MBTG[2]); MBTV(go2, 21, go2); MBTV(go2, 17, split); MBTV(go2, 18, 0);
#endif
    if (hold || split == 0u) { R60_MBDUR = 0; R60_MDONE = go2; R60_MB[6]++; MBT(go2, 11); MBST(6); return; }
#ifdef R60_MBAND_NULL
    R60_MDONE = go2; R60_MB[6]++; return;         /* PROBE: the master draws nothing (the slave still draws split..223) */
#endif
    dc->master = 1; dc->pready = R60_BD[7];
#ifdef R60_MBAND_STEAL
    R60_MSTART = go2;                             /* the claim, then the slave's: one of the two sees the other (R60MBANDSTEAL) */
    if (R60_SSTEAL == go2) { R60_MRESULT = 2u; R60_MBDUR = 0; R60_MDONE = go2; R60_MB[6]++; MBT(go2, 11); MBST(6); return; }
#endif
    nspr = r60_band_loop(dc, bank, split);
    R60_MBDUR = (uint16_t)(r60_frt() - t0);      /* before MDONE: the slave reads it on MDONE */
#ifdef R60_MBAND_STEAL
    R60_MRESULT = 1u;
#endif
    R60_MDONE = go2; R60_EVPUT(14);               /* band done */
    MBT(go2, 11); MBTV(go2, 18, nspr); MBST(6);
    {   uint32_t dt = (uint16_t)(r60_frt() - t0);
        R60_MB[0] += dt; R60_MB[1]++; R60_MB[2] += nspr; R60_MB[4] += split; }
}
#ifdef R60_MBAND_STEALDRAW
static r60_dc_t r60_dcx __attribute__((aligned(16)));   /* the slave's context for a band it takes over */
/* the slave draws the master's band (cart): the same records into the master's lists, so the merge
 * that follows is the one a master band gets */
static __attribute__((noinline)) void r60_band_steal(void)
{
    unsigned bank = R60_BD[0] & 1u, split = R60_BD[1], hold = R60_BD[6];
    r60_dc_t *dc = &r60_dcx;
    if (hold || split == 0u) return;
    dc->master = 0; dc->pready = 1;
    (void)r60_band_loop(dc, bank, split);
    R60_DIAG2[13] += 0x10000u;                    /* steals, high half (give-ups stay in the low half) */
}
#endif
#ifdef R60_MBAND_YIELD
/* THE BAND PREEMPTS THE WALK (R60MBANDYIELD, 2026-10-24; cart). R60MBTIME on the fight: the master's
 * planes prepare runs 100-180 lines every other vint (the walk and its art), so when the slave's
 * CLEARED landed while it ran, the band waited for the whole walk, the next tick started late and
 * the slave waited for the band. r60_planes.c calls this at every walk row and every few art
 * records: a posted band runs at once. */
void r60_band_yield(void)
{
    uint16_t g = R60_GO;                          /* read once: see r60_band */
    if (R60_CLEARED == g && R60_MDONE != g) { r60_band(); MBST(5); }
}
#endif
/* THE SPLIT ROW, once a snapshot (cart): the row above which R60_MBAND_NUM/R60_MBAND_DEN of the
 * sprite rows lie (R60_MBAND_WEIGHT: rows weighted by the sprite's width, so the two bands weigh
 * the pixels rather than the rows). A third to the master: it also ticks the game and walks the
 * planes; at a half its band finished at line 237 and the slave gave up waiting 20 times in 1,100
 * frames (sw33). */
#ifndef R60_MBAND_WAITX
#define R60_MBAND_WAITX 12880u   /* 70 lines: the master's longest wait for a CLEARED that is on its way */
#endif
#ifndef R60_MBAND_NUM
#define R60_MBAND_NUM 1u
#define R60_MBAND_DEN 3u
#endif
static __attribute__((noinline)) unsigned r60_split_row(const volatile uint16_t *t2)
{
    uint16_t hist[FB_H];
    unsigned k2, tot = 0, acc = 0, split = 0;
#ifdef R60_SPLITFAST
    /* THE SAME HISTOGRAM AS A PREFIX SUM (R60SPLITFAST, 2026-10-24): +w at a record's top row, -w at
     * its clipped bottom, one running sum. The per-row fill cost the master ~4.8 lines a vint in the
     * fight (ares profile 600-700); this is 64 records + 224 adds. Same rows, same split. */
    {   int d[FB_H + 1u]; int run = 0;
        for (k2 = 0; k2 <= FB_H; k2++) d[k2] = 0;
        for (k2 = 0; k2 < 64u; k2++) {
            const volatile uint16_t *e2 = t2 + k2 * 8;
            unsigned top, bot, w = 1;
            if (e2[2] & 0x8000u) break;
            if (e2[2] & 0x4000u) continue;
#ifdef R60_MBAND_WEIGHT
            {   int pitch = (int)(signed char)(e2[2] & 0xFFu); w = (unsigned)(pitch < 0 ? -pitch : pitch) * 4u + 4u; }
#endif
            top = e2[0] & 0xFFu; bot = e2[0] >> 8;
            if (bot > FB_H) bot = FB_H;
            if (top >= bot) continue;
            d[top] += (int)w; d[bot] -= (int)w;
        }
        for (k2 = 0; k2 < FB_H; k2++) { run += d[k2]; hist[k2] = (uint16_t)run; }
    }
#else
    for (k2 = 0; k2 < FB_H; k2++) hist[k2] = 0;
    for (k2 = 0; k2 < 64u; k2++) {
        const volatile uint16_t *e2 = t2 + k2 * 8;
        unsigned top, bot, y3, w = 1;
        if (e2[2] & 0x8000u) break;
        if (e2[2] & 0x4000u) continue;
#ifdef R60_MBAND_WEIGHT
        {   int pitch = (int)(signed char)(e2[2] & 0xFFu); w = (unsigned)(pitch < 0 ? -pitch : pitch) * 4u + 4u; }
#endif
        top = e2[0] & 0xFFu; bot = e2[0] >> 8;
        for (y3 = top; y3 < bot && y3 < FB_H; y3++) hist[y3] = (uint16_t)(hist[y3] + w);
    }
#endif
    for (k2 = 0; k2 < FB_H; k2++) tot += hist[k2];
#ifdef R60_MBAND_TUNE
    {   unsigned f8 = R60_MBAND_F;                  /* the slave's running fraction, 1/256 of the rows */
        if (f8 < 16u || f8 > 240u) f8 = R60_MBAND_F0;
        for (k2 = 0; k2 < FB_H; k2++) { acc += hist[k2]; if (acc * 256u >= tot * f8) { split = k2; break; } }
    }
#else
    for (k2 = 0; k2 < FB_H; k2++) { acc += hist[k2]; if (acc * R60_MBAND_DEN >= tot * R60_MBAND_NUM) { split = k2; break; } }
#endif
    return split;
}
#else
#define R60_SPRITE_S(e_, ylo_, yhi_) r60_sprite((e_), (ylo_), (yhi_), r60_mask, bkp, 0u)
#define R60_PUNCH(box_)          r60_punch((box_))
#define R60_PUNCH_SPANS(n0_, n1_) r60_punch_spans(bkp, (n0_), (n1_))
#define R60_FLIP_NOW() ((void)0)
#endif

#ifdef R60_MBAND
/* THE SLAVE'S END OF THE BAND (cart, once a pass: it held .ramtext room the steal needed): wait for
 * MDONE or take the band over (R60MBANDSTEAL), then merge its bookkeeping and retune the share. */
static __attribute__((noinline)) void r60_band_await(uint16_t go, unsigned bkp)
{   /* the master's band must be down before the text goes over it and the frame is presented */
            uint16_t _t = r60_frt();
#ifdef R60_MBAND_STEAL
            unsigned stolen = 0;
#ifdef R60_MBAND_STEALDRAW
            if (R60_MDONE != go && R60_MSTART != go) {   /* the master has not started the band: take it */
                R60_SSTEAL = go;
                if (R60_MSTART != go) { r60_band_steal(); stolen = 1; }
            }
#endif
            if (!stolen) {
                /* once the master has claimed the band it will finish it: wait it out (a give-up here
                 * would present the frame while the master still draws into it). A band nobody has
                 * claimed after 60000 ticks is refused (SSTEAL): the master aborts when it arrives, so
                 * a give-up costs the band's rows for one frame, never a late draw into a shown bank. */
                unsigned lap = 0;
                for (;;) {
                    if (R60_MDONE == go) break;
                    if ((uint16_t)(r60_frt() - _t) >= 60000u) {
                        if (R60_MSTART != go) {
                            R60_SSTEAL = go;
                            if (R60_MSTART != go) break;   /* refused: the master will abort */
                        }
                        if (++lap >= 8u) break;
                        _t = r60_frt();
                    }
                    R60_WAITBODY;
                }
                _t = (uint16_t)(_t - lap * 60000u);   /* _w below measures the whole wait */
#ifdef R60_MBAND_STEALDRAW
                if (R60_MDONE == go && R60_MRESULT == 2u) { r60_band_steal(); stolen = 1; }   /* claimed at the same moment and aborted */
#endif
            }
            if (stolen) R60_MDONE = go;                /* the band is down (the master's own abort writes the same value) */
#else
            while (R60_MDONE != go && (uint16_t)(r60_frt() - _t) < 60000u) R60_WAITBODY;   /* a late band costs a vint, never a torn frame */
#endif
            uint16_t _w = (uint16_t)(r60_frt() - _t);
            R60_PH_MWAIT += _w;
            MBT(go, 6);
            if (R60_MDONE != go) R60_DIAG2[13]++;      /* gave up on the master's band */
            else {
                r60_band_merge(bkp);                   /* its boxes and punched rows into the keep state (cart) */
#ifdef R60_MBAND_TUNE
#ifdef R60_MBAND_STEAL
                if (stolen) { if (r60_split_s && R60_MBAND_F > 40u) R60_MBAND_F = (uint16_t)(R60_MBAND_F - (R60_MBAND_F >> 3)); }   /* the master never came: a smaller share */
                else
#endif
                if (r60_split_s) r60_band_tune(_t, _w);   /* a band that ran: retune the share (cart) */
#endif
            }
        }
#endif
#ifdef R60_CLREXT
#if defined(R60_PRIOROW)
#error "R60CLREXT's row scratch (0x29440-0x297BF) is R60PRIOROW's keep masks and row cells"
#endif
#if defined(R60_MBTIME)
#error "R60CLREXT's row scratch (0x29440-0x297BF) is R60MBTIME's timeline ring and globals (0x29440-0x295FF)"
#endif
#ifdef R60_CLRPROBE
#define CXCODE __attribute__((noinline))   /* a probe does not fit .ramtext beside it: cart (ares charges instructions only) */
#else
#define CXCODE R60CODE __attribute__((noinline))
#endif
/* THE LISTS A BANK'S CLEAR WALKS: the slave's, and under MBAND the master's for the same bank (its
 * band's spans, uncached; the master appends to it only after CLEARED). Both fold into the same
 * per-row extents: a row's run covers every span of that row, whichever core drew it. */
#ifdef R60_MBAND
#define CX_NL 2u
#define CX_L(bk, li)  ((li) ? (uint32_t *)R60_MSPANS(bk) : r60_rowspans[bk])
#define CX_N(bk, li)  ((li) ? (unsigned)R60_MSPAN_N(bk) : (unsigned)r60_rowspan_n[bk])
#define CX_SETN(bk, li, v) do { if (li) R60_MSPAN_N(bk) = (uint16_t)(v); else r60_rowspan_n[bk] = (uint16_t)(v); } while (0)
#else
#define CX_NL 1u
#define CX_L(bk, li)  (r60_rowspans[bk])
#define CX_N(bk, li)  ((unsigned)r60_rowspan_n[bk])
#define CX_SETN(bk, li, v) (r60_rowspan_n[bk] = (uint16_t)(v))
#endif
#ifdef R60_CLRIDENT
/* THE IDENTITY PROBE (R60CLRIDENT, needs R60CLRPROBE=1): before each run is zeroed, every nonzero
 * longword in it is classified against the lists the old per-span clear would have walked:
 * inside a cleared span (the old clear zeroes it too), inside a kept span ([16], must stay 0),
 * else a gap pixel the old clear would have left: [15] when its 8x8 text cell holds a glyph (the
 * text pass redraws it: cl_x0/cl_x1 cover the run), [14] otherwise (must stay 0); [13] of [14] inside
 * a kept sprite's box. [17] runs checked. [18] the odd direct hold packet's residue (FB 0x11800 and up:
 * rows 222-223 and the spill), counted apart. [14]'s hits, located, in r60_cxlog (a ring of 8 x 4 longs):
 * y << 16 | g, the longword, vint, (run g0 << 8 | g1) << 16 | (kept-box sprite + 1) << 8 | spans on the row.
 * Cart, slow (a list scan per run): it changes the cadence, not what is cleared. */
static unsigned cx_bk_probe;
volatile uint32_t r60_cxlog[8][4];
static __attribute__((noinline)) void r60_clr_ident(unsigned y, unsigned g0, unsigned g1)
{
    unsigned bk = cx_bk_probe, li, n, k, g, c = 0;
    uint8_t m[256];
    for (g = 0; g < 256u; g++) m[g] = 0;
    for (li = 0; li < CX_NL; li++) {
        const uint32_t *L = CX_L(bk, li);
        n = CX_N(bk, li);
        for (k = 0; k < n; k++) {
            uint32_t sp = L[k]; unsigned a0 = (sp >> 8) & 0xFFu, a1 = sp & 0xFFu, v = ks_keep[(sp >> 16) & 63u] ? 2u : 1u;
            if ((sp >> 24) != y) continue;
            c++;
            for (g = a0; g < a1; g++) if (m[g] != 1u) m[g] = (uint8_t)v;
        }
    }
    { uint16_t _t = r60_frt(); while ((MARS_VDP_FBCTL & MARS_VDP_FEN) && (uint16_t)(r60_frt() - _t) < 20000u) ; }
    CLRP(17, 1);
    for (g = g0; g < g1; g++) {
        uint32_t v = *FB_DIR_U(y * FB_W + (g << 2));   /* uncached: a cached read would allocate the line the punch then reads */
        if (!v || m[g] == 1u) continue;
        if (m[g] == 2u) { CLRP(16, 1); continue; }
#ifndef R60_PKTODDHI
        if (y * FB_W + (g << 2) + FB_ROW0 >= 0x11800u) { CLRP(18, 1); continue; }   /* the odd hold packet's residue (FB 0x11800-0x11E00 overlaps rows 222-223) */
#endif
        {   unsigned x = g << 2, col = (x - txt_xo[bk]) >> 3, idx = (y >> 3) * 40u + col;
            unsigned in = x >= txt_xo[bk] && col < 40u && y < 224u;
            uint16_t d = in ? txt_copy[idx] : 0;
            if ((d & 0x1FFu) || (d & 0x0E00u) || (in && txt_pend[bk][idx])) CLRP(15, 1);   /* a glyph the text pass redraws, or a pending cell its clear phase erases */
            else {
                unsigned i, kb = 0, w = r60_clrp[14] & 7u;
                CLRP(14, 1);
                for (i = 0; i < 64u; i++) { const uint16_t *b = ks_box[bk][i];
                    if (ks_keep[i] && x >= b[0] && x < b[1] + 4u && y >= b[2] && y < b[3]) { CLRP(13, 1); kb = i + 1u; break; } }
                r60_cxlog[w][0] = (y << 16) | g; r60_cxlog[w][1] = v; r60_cxlog[w][2] = R60_DIAG[0];
                r60_cxlog[w][3] = (((g0 << 8) | g1) << 16) | (kb << 8) | (c & 0xFFu);
            }
        }
    }
}
#endif
/* zero groups [g0, g1) of canvas row y: the line's ROWFILL rule (short -> CPU stores after the fill in
 * flight, sw154; else the fill engine, split at 256-word boundaries) */
static CXCODE void r60_clr_emit(unsigned y, unsigned g0, unsigned g1)
{
    unsigned a = 0x100u + y * 160u + (g0 << 1), left = (g1 - g0) << 1;
    CLRP(2, 1); CLRP(3, g1 - g0);
#ifdef R60_CLRIDENT
    r60_clr_ident(y, g0, g1);
#endif
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
#ifdef R60_CLRPRE
static uint8_t cp_state, cp_bk, cp_ylo, cp_yhi;           /* state 0 to do, 1 ready, 2 refused */
#endif
static __attribute__((noinline)) void r60_clr_exact(unsigned bk)   /* a hold: the per-span clear, as the line does it (cart: rare) */
{
    unsigned li, k, n, carry;
#ifdef R60_CLRPRE
    if (cp_state == 1) for (k = cp_ylo; k <= cp_yhi; k++) CX_ROWL[k] = 0;   /* a precompute for these lists goes unused */
    cp_state = 0;
#endif
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
/* the runs, rows ylo..yhi: [lo, hi) of the row's spans cut around its kept extent; zeroes CX_ROW */
static CXCODE void r60_clr_rows(unsigned ylo, unsigned yhi)
{
    unsigned y, k;
    for (y = ylo & ~31u, k = 1u; y <= yhi; y++, k = (k << 1) | (k >> 31)) {
        uint8_t *r = CX_ROW + y * 4u;
        unsigned lo = 255u - r[0], hi = r[1], kl = 255u - r[2], kh = r[3], tr = y >> 3, m = 0;
        CX_ROWL[y] = 0;
        if (!hi) continue;
        if (!kh || kh <= lo || kl >= hi) { r60_clr_emit(y, lo, hi); m = 1; }
        else {
            if (lo < kl) { r60_clr_emit(y, lo, kl); m = 1; }
            if (kh < hi) { r60_clr_emit(y, kh, hi); m = 1; }
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
#ifdef R60_CLRPRE
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
    unsigned li, k, i, ylo = 255u, yhi = 0;
    cp_state = 2;
    if (r60_rowspan_full[bk]) return;
#ifdef R60_MBAND
    if (R60_MSPAN_FULL(bk)) return;
#endif
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
#endif
#endif

#if defined(R60_TXTHEAL) && defined(R60_TEXT) && defined(R60_TXTFAST)
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
#endif

#if defined(R60_TXTLUTPAL) && defined(R60_TEXT) && defined(R60_TXTFAST) && defined(R60_TXTPOOL)
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
#endif
#if defined(R60_TXTTOUCH) && defined(R60_TEXT) && defined(R60_TXTFAST) && defined(R60_ROWCLR)
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
#else
#define r60_text_touched(b) ((void)0)
#endif

#ifdef R60_FRONTEND
__attribute__((noinline)) static void frontend_slave_pause(void)
{
    MARS_SYS_COMM6 = FE_SLAVE_IDLE;
    while (FE_WAITING(MARS_SYS_COMM12)) {
        unsigned delay;
        for (delay = 0; delay < 256u; delay++) __asm__ volatile ("nop");
    }
}
#endif
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
#ifdef R60_FRONTEND
    /* A cold-boot request can park before the first render pass. */
    if (MARS_SYS_COMM12 == FE_REQUEST) frontend_slave_pause();
#endif
    R60_DIAG[4]++;                       /* slave reached its loop: 0 here means the
                                          * COMM14 handshake never completed */
#ifdef R60_RIGSPLIT
    { unsigned k; for (k = 0; k < 16u; k++) RSX[k] = 0; for (k = 25; k < 32u; k++) RSX[k] = 0; for (k = 32; k < 128u; k++) RSX[k] = 0; RSX[64] = R60_DIAG[0]; RSX[15] = r60_frt(); }
#endif
#ifdef R60_SPEECH
    speech_init();
#endif
#ifdef R60_MBTIME
    r60_mbt_cal();
#endif
    for (;;) {
        uint16_t go = R60_GO;
        /* THE PHYSICAL WRITE BANK (sw226). Tracked, not read: go & 1 advances every pass but a
         * HOLDFLIP pass does not flip, and the FS bit (sw225) does not hold on the MiSTer (the
         * rig stuck on the title, SEGA mark flickering). r60_bkp starts at go & 1 and toggles only
         * where a flip is issued; every per-bank table, the plotter's spans and the row mask use it. */
        unsigned bkp; { static unsigned bkp_init; if (!bkp_init) { r60_bkp = go & 1u; bkp_init = 1; } bkp = r60_bkp; }
        uint16_t t0;
        R60_DIAG[5]++;
#if defined(R60_SPEECH) && defined(R60_SPEECHSLAVE)
        speech_pump_chunk();               /* the loop top spins while the slave waits for its next pass */
#endif
#ifdef R60_CLRPRE
        if (!cp_state) r60_clr_pre(bkp);   /* the next clear's extents, where the slave would wait */
#endif
#ifdef R60_SLAVEWIN
        {
            uint16_t seq = R60_SNAPSEQ;
            if (seq == R60_DRAWSEQ) { RS(26); continue; }   /* no new snapshot yet (RIGSPLIT: bucket 26) */
            if (R60_FMDOWN) {                            /* the 68K's span: close it */
                if (MARS_SYS_COMM0 != R60_SEEN) {
                    R60_FM_UP(); R60_FMDOWN = 0; R60_FMSOON_CLR(); R60_SPANF = R60_GO; R60_DIAG[14]++; R60_EVPUTS(12); MBTG(5);
                    R60_FLIP_NOW();
                } else if ((uint16_t)((uint16_t)R60_DIAG[0] - R60_SPANV) >= ((R60_DISPHOLD == 1u || R60_PKTA_LEN[R60_PKTDONE & 1u] > 736u) ? R60_FATSPAN : 2u)) {   /* any FAT packet (PKTDONE = the packet the 68K holds; PKTSEQ may already be the next build) (churn windows too, sw115: 13 timeouts, a one-frame garbage flash at 311) */
                    /* under the game's own blank the 68K takes a FAT packet (~150 records, ~190 lines of DMA) and the
                     * slave has nothing to draw: give its span R60_FATSPAN vints. sw113 timed out 9-11 spans a cut,
                     * raised FM under the 68K's DMA, and the scene landed with garbage art */
                    R60_FM_UP(); R60_FMDOWN = 0; R60_FMSOON_CLR(); R60_SPANF = R60_GO; R60_DIAG[15]++; MBTG(5);
                    R60_FLIP_NOW();
                } else { RS(27); continue; }   /* RIGSPLIT: the 68K span */
            }
            if (R60_FPEND) {                             /* the flip must have taken */
                if ((uint16_t)(MARS_VDP_FBCTL & MARS_VDP_FS) == r60_fs_want) {
                    R60_FPEND = 0; R60_DIAG2[11]++;
                } else if ((uint16_t)((uint16_t)R60_DIAG[0] - R60_SPANV) >= 2u) {
                    R60_FPEND = 0; R60_DIAG2[12]++;
                } else { RS(28); continue; }   /* RIGSPLIT: flip pending */
            }
            R60_DRAWSEQ = seq;
#ifdef R60_FRONTEND
            if (MARS_SYS_COMM12 == FE_REQUEST) frontend_slave_pause();
#endif
            go = (uint16_t)(R60_GO + 1); R60_GO = go; R60_DIAG[9]++; R60_EVPUTS(9);
#ifdef R60_MBTIME
            MBT(go, 1); MBTV(go, 0, go); MBTV(go, 2, R60_DIAG[0]); MBTV(go, 3, R60_MBTG[5]); MBTV(go, 9, R60_TVINT);
#endif
        }
#else
        if (go == R60_DONE) continue;
#endif
        t0 = r60_frt();                  /* the slave's own FRT: phi/8, 184 ticks a line */
#ifdef R60_RIGSPLIT
        RS(0); RSX[14]++;
#endif
        r60_rowpix = 0;
#if defined(R60_TXTHEAL) && defined(R60_TEXT) && defined(R60_TXTFAST) && !defined(R60_TXTHEAL_LATE)
        if (txt_init && R60_DISPHOLD != 1u) r60_txt_heal(bkp);   /* before the keep decisions read the pending cells */
#endif
#ifdef R60_SOLID
        {   /* FILL THE WHOLE HIDDEN BANK WITH ONE PEN, draw nothing else. If the board
             * shows a solid screen, framebuffer writes and the flip are both sound and
             * the speckle is elsewhere. If it shows bands or speckle, the display is
             * sampling a bank while the slave is still writing it -- that is, the flip
             * is not achieving double buffering on the FPGA, which would explain every
             * board symptom at once: sprites at a quarter density, partial rows, rows
             * lit in proportion to how recently they were written, and a picture that
             * changes between captures. */
#if R60_SOLID == 3
            /* A RAMP OVER THE WHOLE SCREEN, through the overwrite image. One pen proves
             * nothing -- if that CRAM entry is black the screen is black whether or not
             * the write landed, and the board has already shown that most entries are
             * dark. With x selecting the pen, EVERY live entry paints its own column,
             * so the board draws whatever palette it actually has. */
            {
                unsigned y2;
                for (y2 = 0; y2 < FB_H; y2++) {
                    volatile uint8_t *d = FB_OVR(y2 * FB_W);
                    unsigned x2;
                    for (x2 = 0; x2 < FB_W; x2++) d[x2] = (uint8_t)((x2 & 0xFFu) | 1u);
                }
            }
#elif R60_SOLID == 2
            /* through the OVERWRITE image, byte writes -- the exact path the sprite
             * plot uses, which is the only path whose writes reach the board's screen */
            volatile uint8_t *d = FB_OVR(0);
            unsigned n = FB_W * FB_H;
            while (n--) *d++ = 0x11u;
#else
            volatile uint32_t *d = FB_DIR(0);
            unsigned n = (FB_W * FB_H) / 4u;
            while (n--) *d++ = 0x11111111u;
#endif
            R60_DIAG[2]++;
            R60_DONE = go;
            continue;
        }
#endif
#ifdef R60_RECTCLR
        /* CLEAR BY RECTANGLE. Every sprite the plotter drew into this bank last time left
         * its box in the bank's rect list; clear exactly those boxes (longword stores,
         * 4-aligned) and nothing else. Clearing whole rows cost 150 lines a pass with
         * stores and 51 with the fill engine once the figures covered 140 rows (sw36/37);
         * the boxes are about 1.5x the sprites' own pixels. Text cells clear themselves
         * when they change. Under R60_MBAND the master's boxes are in a second list. */
        {
            unsigned li, nl = 1u;
#ifdef R60_TEXT
            { unsigned w3; for (w3 = 0; w3 < 7u; w3++) cleared_rows[w3] = 0; }
#ifdef R60_TXTFAST
            { unsigned w3; for (w3 = 0; w3 < 28u; w3++) { cl_x0[w3] = 0xFFFFu; cl_x1[w3] = 0; } }
#endif
#endif
#ifdef R60_KEEPSPR
            {   /* WHICH SPRITES KEEP THEIR PIXELS THIS PASS */
                unsigned bk = bkp, i2, j2, nn = 0;
                const volatile uint16_t *tb = R60_SNAPSLOT(R60_DRAWSEQ);
                unsigned hold2 = R60_DISPHOLD == 1u, force2 = txt_force[bk] || (txt_init && txt_xo[bk] != r60_xo_next) || R60_DISPHOLD == 2u || r60_fill_rows[bk];
                static uint8_t chg[64]; uint16_t nbx[64][4];
                unsigned ux0 = 0xFFFFu, ux1 = 0, uy0 = 0xFFFFu, uy1 = 0;   /* union of pending text cells */
                ks_stats[2]++;
                for (i2 = 0; i2 < 64u; i2++) { ks_keep[i2] = 0; chg[i2] = 1; }
#ifdef R60_PRIO
                prio_psel = PRIO_TEXT_U[0x740u]; prio_xs = (0xC0u - PRIO_TEXT_U[0x74Cu]) & 0x3FFu; prio_ys = PRIO_TEXT_U[0x748u] & 0x1FFu; prio_bank1 = r60_tile_bank1();
#endif
                while (nn < 64u && !(tb[nn * 8u + 2u] & 0x8000u)) nn++;    /* records to the terminator */
                if (!hold2 && !force2 && txt_init) {
                    uint8_t *pd = txt_pend[bk];
                    unsigned nmax = nn > ks_n[bk] ? nn : ks_n[bk], r;
                    for (r = 0; r < 28u; r++) {
#ifdef R60_TXTFAST
                        if (!txt_rowpend[bk][r]) continue;   /* rows with no pending cell: nothing to scan */
#endif
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
#ifdef R60_ROWCLR
                        if (r60_rowspan_full[bk]) ok = 0;   /* sw203: nothing is kept through the full-canvas clear that an overflowed span table triggers */
#ifdef R60_MBAND
                        if (R60_MSPAN_FULL(bk)) ok = 0;     /* the master's list overflowed: the same full clear */
#endif
#endif
#ifdef R60_PRIO
                        ks_repunch[i2] = 0;
                        if (ok && ks_punched[bk][i2] && (ks_fgkey[bk] != ((prio_bank1 << 26) | (prio_xs << 16) | prio_ys) || ks_fgpsel[bk] != (uint16_t)prio_psel || ks_fggen[bk] != R60_FGGEN)) {
                            /* its holes were cut for another FG: a sideways scroll alone redraws and re-punches
                             * only the rows it punched (the ground band under a standing sprite); anything
                             * else (vertical scroll, page select, cells) redraws it whole */
                            if ((ks_fgkey[bk] >> 26) == prio_bank1 && (ks_fgkey[bk] & 0xFFFFu) == prio_ys && ks_fgpsel[bk] == (uint16_t)prio_psel && ks_fggen[bk] == R60_FGGEN && (ks_pyr[bk][i2] >> 8) < (ks_pyr[bk][i2] & 0xFFu)) ks_repunch[i2] = 1;
                            else ok = 0;
                        }
#endif
                        if (!ok) continue;
                        if (ux1 > ux0 && b[0] < ux1 && ux0 < b[1] && b[2] < uy1 && uy0 < b[3]) { ks_stats[3]++; continue; }   /* a text cell clears inside it */
                        for (j2 = 0; j2 < nmax && ok; j2++) {
                            if (j2 == i2) continue;
                            /* A lower record can be unchanged but invalidated by another
                             * sprite or text clear. Its repaint can cover this keeper.
                             * Walk in draw order so these dependencies propagate. */
                            if (!chg[j2] && (j2 >= i2 || (ks_keep[j2]
#ifdef R60_PRIO
                                && !ks_repunch[j2]
#endif
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
                        if (ok) ks_keep[i2] = 1;
                    }
                }
            }
#endif
#ifdef R60_ROWCLR
            {   /* zero the spans this bank's sprites drew two passes ago */
                unsigned bk = bkp, n = r60_rowspan_n[bk], k;
#ifdef R60_KEEPSPR
                unsigned ks_carry = 0;
#endif
                /* the lists: the bank's own, and under MBAND the master's for the same bank (the
                 * master appends to it only after CLEARED, so the walk and the carry own it here) */
                unsigned li2, nl2 = 1u, full = r60_rowspan_full[bk];
                uint32_t *lst = r60_rowspans[bk];
#ifdef R60_MBAND
                nl2 = 2u; full |= R60_MSPAN_FULL(bk);
#endif
#ifdef R60_CLRPROBE
                uint16_t tc0 = r60_frt();
                CLRP(0, 1); CLRP(11, (uint16_t)(tc0 - t0)); CLRP(12, full != 0);
#endif
                LOGOCOUNT(0);
                R60_EVPUTS(8);                   /* clear start (probe) */
                RS(1);
#ifdef R60_CLRIDENT
                cx_bk_probe = bk;
#endif
                r60_clr_stats[0]++; r60_clr_stats[1] += n; if (full) r60_clr_stats[3]++;
                if (full) {                      /* overflow: the whole canvas */
                    r60_bank_init();
                    for (k = 0; k < 7u; k++) cleared_rows[k] = 0xFFFFFFFFu;
#ifdef R60_TXTFAST
                    for (k = 0; k < 28u; k++) { cl_x0[k] = 0; cl_x1[k] = 0xFFFFu; }
#endif
                }
#ifdef R60_CLREXT
                else if (r60_fill_rows[bk] || R60_DISPHOLD) r60_clr_exact(bk);   /* the hold fill's planes, a display hold: per span */
#ifdef R60_CLRPRE
                else r60_clr_run(bk);
#else
                else r60_clr_ext(bk);
#endif
                (void)li2; (void)nl2; (void)lst; (void)ks_carry;
#else
                else for (li2 = 0; li2 < nl2; li2++) {
#ifdef R60_MBAND
                  if (li2) { lst = (uint32_t *)R60_MSPANS(bk); n = R60_MSPAN_N(bk); ks_carry = 0; }
#endif
                  for (k = 0; k < n; k++) {
                    uint32_t sp = lst[k];
                    unsigned y = sp >> 24, x0 = ((sp >> 8) & 0xFFu) << 2, x1 = (sp & 0xFFu) << 2;
#ifdef R60_KEEPSPR
                    if (ks_keep[(sp >> 16) & 63u]) { lst[ks_carry++] = sp; continue; }   /* kept: not cleared, carried */
#endif
#ifdef R60_ROWFILL
                    {   /* THE FILL ENGINE ZEROES THE SPAN: a CPU longword store into the framebuffer
                         * costs ~18 cycles on ares (59 lines for 4,688 longs a pass in the demo); the
                         * fill engine takes a word address and a length, advances only its low address
                         * byte (a span crossing a 256-word boundary is two fills), and FEN must be
                         * clear before each one is programmed. */
                        unsigned a = 0x100u + y * 160u + (x0 >> 1), left = (x1 - x0) >> 1;
                        r60_clr_stats[2] += left >> 1;
                        if (left < 12u) {            /* a short span: the fill's setup costs more than six stores */
                            volatile uint32_t *d = FB_DIR(y * FB_W + x0); unsigned nw = left >> 1;
                            /* BUT NOT WHILE A FILL IS IN FLIGHT (sw154, 2026-10-17): a CPU store into the
                             * framebuffer during an auto-fill is dropped, so every short span that followed
                             * a long one in this list was never cleared. The eye pan left the highlight's
                             * 8-px top row at every position it had held (bank 0 row 78: 5-12, 101-108,
                             * 125-132, ... 269-276), in both banks, with or without KEEPSPR, and clean
                             * with ROWFILL off. Mike's "uncleared sprite garbage" and "top sprite lines". */
                            { uint16_t _t = r60_frt(); while ((MARS_VDP_FBCTL & MARS_VDP_FEN) && (uint16_t)(r60_frt() - _t) < 20000u) ;   /* sw233: 2000 ticks (11 lines) gave up on the MiSTer's fill and the stores after it were dropped */ }
                            while (nw--) *d++ = 0;
                            left = 0;
                        }
                        while (left) {
                            unsigned room = 0x100u - (a & 0xFFu);
                            unsigned nn = left < room ? left : room;
                            { uint16_t _t = r60_frt(); while ((MARS_VDP_FBCTL & MARS_VDP_FEN) && (uint16_t)(r60_frt() - _t) < 20000u) ;   /* sw233: 2000 ticks (11 lines) gave up on the MiSTer's fill and the stores after it were dropped */ }
                            MARS_VDP_FILLEN = (uint16_t)(nn - 1u);
                            MARS_VDP_FILADR = (uint16_t)a;
                            MARS_VDP_FILDAT = 0u;
                            a += nn; left -= nn;
                        }
                    }
#else
                    volatile uint32_t *d = FB_DIR(y * FB_W + x0);
                    unsigned nw = (x1 - x0) >> 2;
                    r60_clr_stats[2] += nw;
                    while (nw--) *d++ = 0;
#endif
#ifdef R60_TEXT
                    cleared_rows[y >> 5] |= 1u << (y & 31u);
#ifdef R60_TXTFAST
                    { unsigned tr = y >> 3; if (tr < 28u) { if (x0 < cl_x0[tr]) cl_x0[tr] = (uint16_t)x0; if (x1 > cl_x1[tr]) cl_x1[tr] = (uint16_t)x1; } }
#endif
#endif
                }
#ifdef R60_MBAND
                  if (li2) R60_MSPAN_N(bk) = (uint16_t)ks_carry; else r60_rowspan_n[bk] = (uint16_t)ks_carry;
#elif defined(R60_KEEPSPR)
                  r60_rowspan_n[bk] = (uint16_t)ks_carry;
#else
                  r60_rowspan_n[bk] = 0;
                  LOGOCOUNT(1);
#endif
                  }                              /* li2 */
#endif                                   /* R60_CLREXT */
                if (full) r60_rowspan_n[bk] = 0;   /* the whole canvas went: no span is carried */
#ifdef R60_MBAND
                if (full) R60_MSPAN_N(bk) = 0;
                R60_MSPAN_FULL(bk) = 0;
#endif
                r60_rowspan_full[bk] = 0;
#ifdef R60_ROWFILL
                R60_FENW(8);   /* the last fill lands before anything draws (sw233: bounded) */
#endif
#ifdef R60_CLRPROBE
                CLRP(1, (uint16_t)(r60_frt() - tc0));
#endif
                R60_RECTN(go) = 0; R60_MRECTN(go) = 0;   /* the box lists are not used */
                (void)nl; (void)li;
            }
#else
            for (li = 0; li < nl; li++) {
                volatile uint16_t *rc = li ? R60_MRECTS(go) : R60_RECTS(go);
                unsigned n = li ? R60_MRECTN(go) : R60_RECTN(go), i;
                for (i = 0; i < n; i++) {
                    unsigned x0 = rc[i * 4] & ~3u, x1 = (rc[i * 4 + 1] + 4u) & ~3u;
                    unsigned y0 = rc[i * 4 + 2], y1 = rc[i * 4 + 3], y;
                    if (x1 > FB_W + 512u) x1 = FB_W + 512u;   /* boxes are canvas-linear: past 320 is the next row's head, which the shifted line shows */
                    for (y = y0; y < y1; y++) {
                        volatile uint32_t *d = FB_DIR(y * FB_W + x0);
                        unsigned nw = (x1 - x0) >> 2;
                        while (nw--) *d++ = 0;
#ifdef R60_TEXT
                        cleared_rows[y >> 5] |= 1u << (y & 31u);
#ifdef R60_TXTFAST
                        { unsigned tr = y >> 3; if (tr < 28u) { if (x0 < cl_x0[tr]) cl_x0[tr] = (uint16_t)x0; if (x1 > cl_x1[tr]) cl_x1[tr] = (uint16_t)x1; } }
#endif
#endif
                    }
                    if (R60_DISPHOLD == 2u && r60_fill_rows[bkp]) {   /* step B: the planes come back under the box */
                        unsigned r0 = y0 >> 3, r1 = (y1 + 7u) >> 3;
                        if (r1 > r60_fill_rows[bkp]) r1 = r60_fill_rows[bkp];
                        if (r1 > r0) {
                            unsigned c9, r9, xo9 = r60_xo_next, cl = x0 > xo9 ? (x0 - xo9) / 8u : 0u, ch = (x1 - xo9 + 7u) / 8u;
                            r60_fill_cells(FB_DIRB(0), FB_OVR(0), r60_fill_anchor[bkp], x0, x1, r0, r1);
                            if (ch > 40u) ch = 40u;
                            for (r9 = r0; r9 < r1 && r9 < 28u; r9++) for (c9 = cl; c9 < ch; c9++) txt_pend[bkp][r9 * 40u + c9] = 1;   /* glyphs under the box */
                        }
                    }
                }
                if (li) R60_MRECTN(go) = 0; else R60_RECTN(go) = 0;
            }
#endif /* R60_ROWCLR */
        }
#else  /* the row-mask clear (R60_MBAND's old band mask lived here; MBAND now requires RECTCLR) */
        /* CLEAR ONLY THE ROWS THIS BANK HOLDS, through the DIRECT image: a zero byte
         * through the overwrite image is a no-op, so the clear cannot use it. */
        r60_mask = r60_rowmask[bkp];
#ifdef R60_TEXT
        { unsigned w3; for (w3 = 0; w3 < 7u; w3++) cleared_rows[w3] = r60_mask[w3]; }   /* the rows this pass clears */
#ifdef R60_TXTFAST
        { unsigned w3; for (w3 = 0; w3 < 28u; w3++) { cl_x0[w3] = 0; cl_x1[w3] = 0xFFFFu; } }   /* no x knowledge on this path: whole rows */
#endif
#endif
        {
            unsigned w2, y2;
            for (w2 = 0; w2 < 7u; w2++) {
                uint32_t m = r60_mask[w2];
                if (!m) continue;
                r60_mask[w2] = 0;
                for (y2 = 0; y2 < 32u; y2++) {
                    if (!(m & ((uint32_t)1u << y2))) continue;
                    {
                        unsigned row = (w2 << 5) + y2;
                        if (row >= FB_H) break;
#ifdef R60_MBAND
                        if (row < r60_split) continue;   /* the master's rows */
#endif
#ifdef R60_FILLCLR
                        {   /* THE VDP FILLS THE ROW. 160 words from word address
                             * 0x100 + row*160; the fill engine advances only the low
                             * address byte (MARS_VDP_FILADR), so a row that crosses a
                             * 256-word boundary is two fills. Each fill: FEN must be
                             * clear first (ares drops FB writes while it is set,
                             * bus-internal.cpp:48; the core waits). CPU stores cost
                             * ~25 lines a pass for ~50 rows on the profile. */
                            unsigned a = 0x100u + row * 160u, left = 160u;
                            while (left) {
                                unsigned room = 0x100u - (a & 0xFFu);
                                unsigned n = left < room ? left : room;
                                { uint16_t _t = r60_frt();            /* bounded: a FEN that never clears must not hang the slave */
                                  while ((MARS_VDP_FBCTL & MARS_VDP_FEN) && (uint16_t)(r60_frt() - _t) < 20000u) ;   /* sw233: 2000 ticks (11 lines) gave up on the MiSTer's fill and the stores after it were dropped */ }
                                MARS_VDP_FILLEN = (uint16_t)(n - 1u);
                                MARS_VDP_FILADR = (uint16_t)a;
                                MARS_VDP_FILDAT = 0u;          /* starts the fill */
                                a += n; left -= n;
                            }
                            { uint16_t _t = r60_frt();
                              while ((MARS_VDP_FBCTL & MARS_VDP_FEN) && (uint16_t)(r60_frt() - _t) < 20000u) ;   /* sw233: 2000 ticks (11 lines) gave up on the MiSTer's fill and the stores after it were dropped */ }
                        }
#else
                        {
                            volatile uint32_t *d = FB_DIR(row * FB_W);
                            unsigned n = FB_W / 4u;
                            while (n--) *d++ = 0;
                        }
#endif
                    }
                }
            }
        }
#endif /* R60_RECTCLR */
        R60_EVPUTS(15);                      /* clear done */
        RS(2);
#ifdef R60_BARPROBE
        {   /* BEFORE THE SPRITES THIS TIME. On the board the slave's sprite writes land
             * and every full-row write at the END of the pass does not -- through either
             * framebuffer image, at any pen, width or rate. Position in the pass is the
             * only difference left. If the ramp appears here and not there, the pass is
             * being cut short on the FPGA somewhere after the sprite loop. */
            unsigned r;
            /* (the ramp: x selects the pen, so each live entry paints a column)
             * WAS: which CRAM entries are alive -- Every test block so far used
             * ONE pen, and if that entry is black on the FPGA the block is invisible
             * whether or not the write landed -- which fits every board symptom,
             * answered: NONE lit, so it is not the palette. */
            for (r = 130u; r < 138u; r++) {          /* ramp through the DIRECT image */
                volatile uint8_t *q = FB_DIRB(r * FB_W);
                unsigned x;
                for (x = 0; x < FB_W; x++) q[x] = (uint8_t)(x & 0xFFu);
            }
            for (r = 140u; r < 148u; r++) {          /* ramp through the OVERWRITE image,
                                                      * the path the sprites use. Pen 0 is
                                                      * a no-op there, so bias by 1. */
                volatile uint8_t *q = FB_OVR(r * FB_W);
                unsigned x;
                for (x = 0; x < FB_W; x++) q[x] = (uint8_t)((x & 0xFFu) | 1u);
            }
        }
#endif
#ifdef R60_BARSFIRST
        {   /* THE BARS BEFORE THE SPRITES. The one pattern that has held all night on
             * the board is that writes LATE IN A PASS do not land: the probe blocks
             * after the sprite loop read 0 of 320 and the SAME blocks before it read
             * 22-60, and these bars -- drawn after the sprites, in rows where 6,040
             * sprite pixels display in the same capture -- read zero through both
             * framebuffer images and with a pen whose colour the renderer forces.
             * If they appear here, the loss is position in the pass, which is what a
             * bank being written while it is displayed looks like. */
            unsigned r;
            for (r = 100u; r < 110u; r++) {
                volatile uint8_t *q;
                unsigned len, x;
                if (r >= 104u && r < 106u) continue;        /* gap between the bars */
                q   = FB_OVR(r * FB_W);
                len = (r < 104u) ? R60_WA : R60_WB;
                for (x = 8u; x < 8u + len && x < FB_W; x++) {
                    /* THE RESERVED PEN, NOT A GAME ONE. The first cut drew these in
                     * 0x11, whose CRAM entry the renderer does not control: an
                     * invisible bar would then mean "that colour set is black", not
                     * "the write did not land", and the board read both bars as zero
                     * while sprites in the SAME ROWS displayed. Pen 255 is forced to
                     * white by r60_cram_commit, so the only thing it can report is
                     * whether the pixels arrived. */
                    q[x] = (uint8_t)R60_BAR_PEN;
                    __asm__ __volatile__("nop\n\tnop\n\tnop\n\tnop" ::: "memory");
                }
            }
        }
#endif
        {
            unsigned i, drew = 0;
            static uint8_t blanked[2];
            unsigned hold = R60_DISPHOLD == 1u;   /* only the game's own blank draws nothing */
#ifdef R60_HOLDDIRECT
            /* A DIRECT PACKET STILL PENDING (sw177): the master built this window's packet in place
             * for the write bank; a flip now would hide it from the 68K (the release-side orphan:
             * demo 2 and the card came back incomplete). Draw nothing and keep the bank one more
             * window, the 68K takes the packet, the next window flips. */
            if (!hold && R60_PKTSEQ != R60_PKTDONE && (R60_PKTA_LEN[R60_PKTSEQ & 1u] & R60_PKTA_DIRECT)) { hold = 1; R60_DIAG2[9] += 0x10000u; }
#endif
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
#ifdef R60_XOFFPROBE
                if (R60_DIAG[0] > 600u) k = (unsigned)(R60_DIAG[0] - 600u) & 127u;
#endif
#ifdef R60_XOFFCONST
                if (R60_DIAG[0] > 600u) k = R60_XOFFCONST;
#endif
#ifdef R60_XOFFEVEN
                if (R60_DIAG[0] > 600u) k = ((unsigned)(R60_DIAG[0] - 600u) & 63u) * 2u;
#endif
                kw = k >> 1; sh = k & 1u;
                if (sh) { kw++; if ((kw & 31u) == 31u) sh = 0; }   /* bulletin 14 invalidates only the SHIFT bit: clear it, keep the word (steps 2 then 0, max error 1 px) */
                r60_xo_next = sh ? kw * 2u - 1u : kw * 2u; r60_kw_next = kw; r60_sh_next = sh;
                r60_fill_k = k; r60_fill_mode2 = mode2;
            }
            {   /* take the master's text list FIRST, so the clear phase below erases the old glyph
                 * of every cell that changed. Taken after it (sw108) the changed cell was drawn over
                 * its old glyph in the bank that took the list and cleared first in the other: the
                 * two banks alternated at 60 Hz (temporal_metric: 990/847 text pixels, eye span). */
#ifdef R60_TXTPOOL
                {   /* a LUT change restyles every glyph: force BOTH banks through the clear phase first
                     * (sw120 redrew over the old glyphs through the overwrite image: bold, doubled letters) */
                    static uint16_t lutseq_top;
#ifdef R60_TXTLUTPAL
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
#ifdef R60_TXTFAST
                        txt_ggen++; if (!txt_ggen) txt_ggen = 1;   /* the glyph cache is keyed on the LUT generation */
#endif
                    }
#else
                    uint16_t ls = R60_TXTLUTSEQ;
                    if (ls != lutseq_top) { lutseq_top = ls; txt_force[0] = 1; txt_force[1] = 1;
#ifdef R60_TXTFAST
                        txt_ggen++; if (!txt_ggen) txt_ggen = 1;   /* the glyph cache is keyed on the LUT generation */
#endif
                    }
#endif
                }
#endif
                uint16_t sq = R60_TXTSEQ;
                if (sq != R60_TXTTAKEN) {
                    volatile uint16_t *lst = R60_TXTLIST(sq);
                    unsigned n = lst[0], k;
                    if (!txt_init) { for (k = 0; k < 28u * 40u; k++) { txt_copy[k] = 0; txt_pend[0][k] = 0; txt_pend[1][k] = 0; } txt_init = 1; }
                    for (k = 0; k < n; k++) {
                        unsigned ci = lst[1 + k * 2], idx = ((ci >> 8) & 31u) * 40u + (ci & 63u);
                        uint16_t d = lst[2 + k * 2];
                        if (idx < 28u * 40u) {
#ifdef R60_TXTFAST
                            unsigned r3 = idx / 40u; uint16_t od = txt_copy[idx];
                            unsigned on = ((od & 0x1FFu) || (od & 0x0E00u)) ? 1u : 0u, nn = ((d & 0x1FFu) || (d & 0x0E00u)) ? 1u : 0u;
                            if (on && !nn) { if (txt_rowcnt[r3]) txt_rowcnt[r3]--; } else if (!on && nn) txt_rowcnt[r3]++;
                            if (!txt_pend[0][idx]) txt_rowpend[0][r3]++;
                            if (!txt_pend[1][idx]) txt_rowpend[1][r3]++;
#endif
                            txt_copy[idx] = d; txt_pend[0][idx] = 1; txt_pend[1][idx] = 1;
                        }
                    }
                    R60_TXTTAKEN = sq;
                }
            }
            LOGOCOUNT(2);
            {   /* THE TEXT CLEAR PHASE: before any sprite is drawn, every glyph that will be
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
#ifndef R60_TXTHEAL
                    if (((++heal[bank]) & 15u) == 0u) txt_force[bank] = 1;
#else
                    (void)heal;
#ifdef R60_TXTHEAL_LATE
                    if (txt_init && R60_DISPHOLD != 1u) r60_txt_heal(bank);   /* where the forced heal was: after the keep decisions */
#endif
#endif
                }
                unsigned force = txt_force[bank] || (txt_init && txt_xo[bank] != r60_xo_next);
                if (txt_init && R60_DISPHOLD != 1u) {
                    for (idx = 0; idx < 28u * 40u; idx++) {
                        uint16_t d = txt_copy[idx];
                        if (!(force || pend[idx])) continue;
                        if (force && !pend[idx] && !((d & 0x1FFu) || (d & 0x0E00u))) continue;   /* a forced redraw skips cells that are blank now; a PENDING blank cell still loses its old glyph (sw146: "2 CINSERT COINAY" -- the ends of the longer string were pending blanks skipped under force, in both banks, for good) */
                        if (force) pend[idx] = 1;
#ifdef R60_TXTFAST
                        if (force) txt_allpend[bank] = 1;
#endif
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
#ifdef R60_KEEPSPR
                    ks_n[bkp] = 0;           /* sw201: a wiped bank keeps no sprite */
#endif
#ifdef R60_ROWCLR
                    r60_rowspan_n[bkp] = 0; r60_rowspan_full[bkp] = 0;   /* and has no spans left to clear */
#endif
                }
#ifdef R60_HOLDDIRECT
                /* the shown bank is already black: stop flipping, the write bank holds still and
                 * the master builds its hold packets straight into it */
                if (blanked[(bkp ^ 1u)]) R60_HOLDFLIP = 1;
#endif
            } else {
                blanked[bkp] = 0;
#ifdef R60_HOLDDIRECT
                R60_HOLDFLIP = 0;
#endif
            }
            { uint16_t _n = r60_frt(); R60_PH[2] += (uint16_t)(_n - r60_ph_t); r60_ph_t = _n; }
            RS(3);
#if defined(R60_PRIO) && !defined(R60_KEEPSPR)
            prio_psel = PRIO_TEXT_U[0x740u]; prio_xs = (0xC0u - PRIO_TEXT_U[0x74Cu]) & 0x3FFu; prio_ys = PRIO_TEXT_U[0x748u] & 0x1FFu; prio_bank1 = r60_tile_bank1();
#endif
#ifdef R60_PALMAP
            if (hold == 0) r60_palmap_assign();
#endif
#ifdef R60_MBAND
            r60_band_post(bkp, hold, go);        /* the band descriptor, then CLEARED (cart) */
#endif
            for (i = 0; hold == 0 && i < 64; i++) {
                RS(4);                                   /* RIGSPLIT: a stamp a sprite (the wrap) */
#if defined(R60_SLAVEWIN)
                const volatile uint16_t *e = R60_SNAPSLOT(R60_DRAWSEQ) + i * 8;
#elif defined(R60_TICKLATE)
                const volatile uint16_t *e = R60_SNAP2 + i * 8;
#else
                const volatile uint16_t *e = SPR_SNAP + i * 8;
#endif
                uint16_t d2 = e[2];
                if (d2 & 0x8000) break;                  /* terminator */
#ifdef R60_KEEPSPR
                { unsigned k4; for (k4 = 0; k4 < 8u; k4++) ks_rec[bkp][i][k4] = e[k4]; }
                if (ks_keep[i]) {                        /* its pixels are already in this bank */
#ifdef R60_PRIO
                    if (ks_repunch[i]) {
                        unsigned by0 = ks_pyr[bkp][i] >> 8, by1 = ks_pyr[bkp][i] & 0xFFu, span0b = r60_rowspan_n[bkp];
                        uint16_t sv[4]; unsigned k6;
                        for (k6 = 0; k6 < 4u; k6++) sv[k6] = r60_last_box[k6];
                        ks_cur = i;
#ifdef R60_MBAND
                        if (by0 < r60_split_s) by0 = r60_split_s;   /* the rows above the split are the master's */
                        if (by0 >= by1) { ks_stats[0]++; drew++; continue; }
#endif
                        PHT_B(); R60_SPRITE_S(e, by0, by1);   /* the band only: same pixels, the holes refilled */ PHT_E(0);
                        #ifdef R60_PRIOMASK
                        if (e[5] & 0x3FFu)
#elif defined(R60_PRIOROW)
                        if (pr_over)
#endif
                        { PHT_B(); if (!r60_rowspan_full[bkp]) R60_PUNCH_SPANS(span0b, r60_rowspan_n[bkp]);
                        else if (r60_last_box[1] > r60_last_box[0]) R60_PUNCH(r60_last_box); PHT_E(1); }
                        if (prio_ymax > prio_ymin) ks_pyr[bkp][i] = (uint16_t)((prio_ymin << 8) | prio_ymax);
                        r60_text_touched(r60_last_box);
                        for (k6 = 0; k6 < 4u; k6++) r60_last_box[k6] = sv[k6];
                        r60_prio_stats[0]++;
                    }
#endif
                    ks_stats[0]++; drew++; continue;
                }
                ks_cur = i;
#endif
                if (d2 & 0x4000) {
#ifdef R60_KEEPSPR
                    ks_box[bkp][i][0] = ks_box[bkp][i][1] = ks_box[bkp][i][2] = ks_box[bkp][i][3] = 0;
#endif
                    continue;               /* skipped */
                }
#if defined(R60_PRIO) && defined(R60_ROWCLR)
                { unsigned span0 = r60_rowspan_n[bkp];
#endif
#ifndef R60_NOSPR
#ifdef R60_MBAND
                PHT_B(); R60_SPRITE_S(e, r60_split_s, FB_H); PHT_E(0);
#else
                PHT_B(); R60_SPRITE_S(e, 0u, FB_H); PHT_E(0);
#endif
#endif
#ifdef R60_PRIO
                /* the box punch for a full draw (one cell lookup per cell: sw162's span punch here
                 * looked every cell up once a row and cost 6x the cells); the span punch serves
                 * the band re-punch of a kept sprite, where the band is a few rows */
                (void)span0;
#ifdef R60_PRIOMASK
                if (e[5] & 0x3FFu) { PHT_B(); ks_punched[bkp][i] = (uint8_t)(r60_last_box[1] > r60_last_box[0] && R60_PUNCH(r60_last_box)); PHT_E(1); }   /* a zoomed sprite drew unmasked */
                else ks_punched[bkp][i] = (uint8_t)pm_hit;                                                          /* masked in the plot */
#elif defined(R60_PRIOROW)
                if (pr_over) { PHT_B(); ks_punched[bkp][i] = (uint8_t)(r60_last_box[1] > r60_last_box[0] && R60_PUNCH(r60_last_box)); PHT_E(1); }   /* zoomed, or over the table: the box punch */
                else {
                    ks_punched[bkp][i] = (uint8_t)pr_hit;                                                           /* masked in the plot */
#ifdef R60_PRIOROWCHECK
                    if (r60_last_box[1] > r60_last_box[0]) { uint32_t s0 = r60_pr_boxsum(r60_last_box); unsigned sv0 = prio_ymin, sv1 = prio_ymax; R60_PUNCH(r60_last_box); prio_ymin = sv0; prio_ymax = sv1;
                        r60_pr_checked++; if (r60_pr_boxsum(r60_last_box) != s0) r60_pr_bad++; }
#endif
                }
#else
                PHT_B(); ks_punched[bkp][i] = (uint8_t)(r60_last_box[1] > r60_last_box[0] && R60_PUNCH(r60_last_box)); PHT_E(1);
#endif
#endif
#ifdef R60_PRIO
                ks_pyr[bkp][i] = ks_punched[bkp][i] && prio_ymax > prio_ymin ? (uint16_t)((prio_ymin << 8) | prio_ymax) : 0u;
#endif
#if defined(R60_PRIO) && defined(R60_ROWCLR)
                }
#endif
#ifdef R60_KEEPSPR
                { unsigned k4; for (k4 = 0; k4 < 4u; k4++) ks_box[bkp][i][k4] = r60_last_box[k4]; ks_stats[1]++; }
#endif
                r60_text_touched(r60_last_box);
                drew++;
            }
#ifdef R60_KEEPSPR
            ks_n[bkp] = (uint8_t)(hold ? 0u : i);   /* under the hold nothing was drawn */
#ifdef R60_PRIO
            ks_fgkey[bkp] = (prio_bank1 << 26) | (prio_xs << 16) | prio_ys; ks_fgpsel[bkp] = (uint16_t)prio_psel; ks_fggen[bkp] = R60_FGGEN;
#endif
            if (hold) { unsigned k5; for (k5 = 0; k5 < 64u; k5++) ks_box[bkp][k5][1] = 0; }
#endif
            R60_DIAG[3] += drew;
        }
        { uint16_t _n = r60_frt(); R60_PH[3] += (uint16_t)(_n - r60_ph_t); r60_ph_t = _n; }
        RS(4);
        R60_EVPUTS(5); MBT(go, 5);           /* sprites done (code 5, 'APLY' slot reused) */
#ifdef R60_MBAND
        r60_band_await(go, bkp);
        RS(5);
#endif
#ifdef R60_TEXT
        {   /* THE TEXT LAYER, in front of the sprites. The master's change list is applied to
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
#ifdef R60_TXTPOOL
#ifndef R60_TXTLUTPAL   /* under it the changed pals' cells are pending in both banks already */
                {   static uint16_t lutseq_seen;                 /* a LUT change: every glyph's bytes are stale */
                    uint16_t ls = R60_TXTLUTSEQ;
                    if (ls != lutseq_seen) { lutseq_seen = ls; for (k2 = 0; k2 < 8u; k2++) pal_moved[k2] = 1; moved = 1; }
                }
#endif
#endif
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
#ifdef R60_TXTFAST
                {   unsigned bk = bkp;
                    if (!txt_rowcnt[row]) {          /* nothing to draw on this row: just retire its pending marks */
                        if (txt_rowpend[bk][row] || txt_allpend[bk]) { for (col = 0; col < 40u; col++) pend[row * 40u + col] = 0; txt_rowpend[bk][row] = 0; }
                        continue;
                    }
                }
#endif
                for (y2 = 0; y2 < 8u; y2++)
                    if ((cleared_rows[(y0 + y2) >> 5] >> ((y0 + y2) & 31u)) & 1u) { cleared = 1; break; }
#ifdef R60_TXTFAST
                if (!cleared && !moved_any && !txt_rowpend[bkp][row] && !txt_allpend[bkp]) continue;
#endif
                for (col = 0; col < 40u; col++) {
                    unsigned idx = row * 40u + col;
                    uint16_t d = txt_copy[idx];
                    unsigned code = d & 0x1FFu, g, base, y, draw;
                    const uint8_t *tp;
                    volatile uint8_t *dst;
#ifdef R60_TXTFAST
                    {   unsigned cx = col * 8u + r60_xo;   /* a cleared row redraws only the cells the clear touched */
                        unsigned hit = cleared && (cx + 8u > cl_x0[row]) && (cx < cl_x1[row]);
                        draw = pend[idx] || hit || pal_moved[((unsigned)d >> 9) & 7u];
                    }
#else
                    draw = pend[idx] || cleared || pal_moved[((unsigned)d >> 9) & 7u];
#endif
                    if (!draw) continue;
                    if (pend[idx]) {
                        pend[idx] = 0;   /* its old glyph was cleared in the clear phase */
#ifdef R60_TXTFAST
                        if (txt_rowpend[bkp][row]) txt_rowpend[bkp][row]--;
#endif
                    }
                    if (code == 0u && !(d & 0x0E00u)) continue;
                    g = R60_TXTGRP[((unsigned)d >> 9) & 7u];
                    if (g == 0xFFu) continue;
                    base = R60_TXTBASE + g * 8u;
                    tp = altbeast_tiles + code * 64u;
                    dst = FB_OVR(y0 * FB_W + col * 8u + r60_xo);
                    R60_DIAG2[2]++;
                    { uint16_t _td = r60_frt();
#ifdef R60_TXTPOOL
                    uint32_t L[8];
#ifdef R60_TXTFAST
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
#else
                    { const volatile uint16_t *lut = R60_TXTLUT + g * 8u; unsigned k3; for (k3 = 0; k3 < 8u; k3++) L[k3] = lut[k3]; }
#endif
                    (void)base;
#endif
                    for (y = 0; y < 8u; y++) {
#ifdef R60_TXTPOOL
#ifdef R60_TXTFAST
                        uint32_t a0 = gc[y * 2u], a1 = gc[y * 2u + 1u];
#else
                        uint32_t a0 = (L[tp[0]] << 24) | (L[tp[1]] << 16) | (L[tp[2]] << 8) | L[tp[3]];
                        uint32_t a1 = (L[tp[4]] << 24) | (L[tp[5]] << 16) | (L[tp[6]] << 8) | L[tp[7]];
#endif
                        if (r60_xo & 2u) {           /* word-aligned only: four word stores */
                            ((volatile uint16_t *)dst)[0] = (uint16_t)(a0 >> 16); ((volatile uint16_t *)dst)[1] = (uint16_t)a0;
                            ((volatile uint16_t *)dst)[2] = (uint16_t)(a1 >> 16); ((volatile uint16_t *)dst)[3] = (uint16_t)a1;
                        } else {
                        ((volatile uint32_t *)dst)[0] = a0;
                        ((volatile uint32_t *)dst)[1] = a1;
                        }
#else
                        uint32_t w0 = ((const uint32_t *)tp)[0], w1 = ((const uint32_t *)tp)[1];
                        uint32_t bb = base * 0x01010101u, m0, m1;
                        m0 = ((w0 & 0x7F7F7F7Fu) + 0x7F7F7F7Fu) & 0x80808080u; m0 = (m0 >> 7) * 0xFFu;
                        m1 = ((w1 & 0x7F7F7F7Fu) + 0x7F7F7F7Fu) & 0x80808080u; m1 = (m1 >> 7) * 0xFFu;
                        if (r60_xo & 2u) {           /* word-aligned only: four word stores */
                            uint32_t a0 = w0 + (bb & m0), a1 = w1 + (bb & m1);
                            ((volatile uint16_t *)dst)[0] = (uint16_t)(a0 >> 16); ((volatile uint16_t *)dst)[1] = (uint16_t)a0;
                            ((volatile uint16_t *)dst)[2] = (uint16_t)(a1 >> 16); ((volatile uint16_t *)dst)[3] = (uint16_t)a1;
                        } else {
                        ((volatile uint32_t *)dst)[0] = w0 + (bb & m0);
                        ((volatile uint32_t *)dst)[1] = w1 + (bb & m1);
                        }
#endif
                        tp += 8; dst += FB_W;
                    }
                    t_draw += (uint16_t)(r60_frt() - _td); }
                }
            }
#ifdef R60_TXTFAST
            txt_allpend[bkp] = 0;            /* the forced pass is done (the hold path jumps past this and keeps it) */
#endif
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
#endif
#ifdef R60_LOCKPROBE
        r60_lockprobe_draw();
#endif
#ifndef R60_NOBARS
        r60_bars_paint();   /* cart-resident since sw262: the inline block no longer fits .ramtext */
#endif
        RS(7);
#ifndef R60_NOBARS
#endif
        R60_DIAG[2]++;
        /* WHAT THE PASS COST AND WHAT IT PUT DOWN. Draw time in slave FRT ticks (184 a
         * line) and the row-pixels actually stored, so the two can be read against each
         * other: a pass that costs more for the same pixels is the renderer getting
         * slower, one that costs more for more pixels is the scene. */
        R60_DIAG2[8] += (uint32_t)(uint16_t)(r60_frt() - t0);
        R60_DIAG2[10] += r60_rowpix;
#ifdef R60_SLAVEWIN
        {   /* THE WINDOW, HERE, THE MOMENT THE PASS ENDS: slave idle by definition, FM up,
             * and the master is not in the VDP region at all (it only ticks). */
            uint16_t fs_o;
            R60_EVPUTS(10);
#ifdef R60_PADBAR
            {   /* PROBE sw218: the live service word as blocks at the top-left of the write bank, pen 255 (the
                 * bar pen): block 0 = tag seen, 1 = coin, 2 = start1, 3 = start2; row 1: raw P1 byte bits 0-7
                 * (arcade active-low: a LIT block is a RELEASED button). The sprite channel is the one the
                 * board is known to show. */
                extern volatile uint32_t port_live_svc; extern volatile uint16_t port_live_p1;
                unsigned k, x, y; uint32_t v = port_live_svc; unsigned p1 = port_live_p1;
                for (k = 0; k < 4u; k++) {
                    unsigned on = k == 0 ? (v >> 8) & 1u : k == 1 ? v & 1u : k == 2 ? (v >> 4) & 1u : (v >> 5) & 1u;
                    for (y = 2; y < 8u; y++) for (x = 0; x < 6u; x++) *(volatile uint8_t *)FB_DIR(y * FB_W + 2u + k * 8u + x) = on ? 255u : (y == 2u || x == 0u ? 255u : 0u);
                }
                for (k = 0; k < 8u; k++) {
                    unsigned on = (p1 >> k) & 1u;
                    for (y = 10; y < 16u; y++) for (x = 0; x < 6u; x++) *(volatile uint8_t *)FB_DIR(y * FB_W + 2u + k * 8u + x) = on ? 255u : (y == 10u || x == 0u ? 255u : 0u);
                }
                {   /* sw227: rig telemetry rows. row 3 = claims refused (DIAG2[14]) low 8 bits, row 4 = plans adopted
                     * (DIAG[10] >> 16) low 4 bits | torn builds (census[0]) low 2 bits << 4 | slave waits run out (census[1]) low 2 bits << 6,
                     * row 5 = hold releases (GATE_REL) low 8 bits. A lit block is a 1 bit, bit 0 leftmost. */
                    unsigned r3 = R60_DIAG2[14] & 0xFFu, r4 = ((R60_DIAG[10] >> 16) & 0xFu) | ((r60_census[0] & 3u) << 4) | ((r60_census[1] & 3u) << 6), r5 = R60_GATE_REL & 0xFFu;
                    unsigned rowv[3] = { r3, r4, r5 };
                    unsigned rr;
                    for (rr = 0; rr < 3u; rr++)
                        for (k = 0; k < 8u; k++) {
                            unsigned on = (rowv[rr] >> k) & 1u;
                            for (y = 18u + rr * 8u; y < 24u + rr * 8u; y++) for (x = 0; x < 6u; x++) *(volatile uint8_t *)FB_DIR(y * FB_W + 2u + k * 8u + x) = on ? 255u : (y == 18u + rr * 8u || x == 0u ? 255u : 0u);
                        }
                }
            }
#endif
#ifdef R60_HTPROBE
            /* PROBE (2026-10-24, the rig's hold-scene tear): five rows of eight blocks at the top-left
             * of the write bank, longword stores, a full block = a 1 bit (bit 0 leftmost), an outline
             * top line = 0. Drawn only with the display up and no hold, so it never shares the bus with
             * a hold copy. row 0: span backstops (DIAG[15]) low 8; row 1: master copies of a staged
             * direct packet made outside the hold handshake (ht[0]) low 4 | torn master (census 0)
             * low 2 << 4 | slave BUILDING waits run out (census 1) low 2 << 6; rows 2-3: the 68K's
             * word in COMM12 (tag 0xE in the top nibble, else row 2 and 3 all lit): row 2 = FM up at
             * the end of a consume (3 bits) | FS changed under a consume (3 bits) << 3;
             * row 3 = stale skips (3 bits) | A without B or B without A (3 bits) << 3;
             * row 4 = staged copies (ht[1]) low 8 (proves the row is live). */
            if (R60_DISPHOLD == 0u && !R60_HOLDFLIP) {
                extern volatile uint16_t r60_ht_mem[8];
                const volatile uint16_t *ht = (const volatile uint16_t *)((uint32_t)r60_ht_mem | 0x20000000u);
                uint16_t c12 = MARS_SYS_COMM12;
                unsigned ok = (c12 & 0xF000u) == 0xE000u;
                unsigned rowv[5], rr, kk, yy;
                rowv[0] = R60_DIAG[15] & 0xFFu;
                rowv[1] = (ht[0] & 0xFu) | ((r60_census[0] & 3u) << 4) | ((r60_census[1] & 3u) << 6);
                rowv[2] = ok ? (c12 & 0x3Fu) : 0xFFu;
                rowv[3] = ok ? ((c12 >> 6) & 0x3Fu) : 0xFFu;
                rowv[4] = ht[1] & 0xFFu;
                for (rr = 0; rr < 5u; rr++)
                    for (kk = 0; kk < 8u; kk++) {
                        unsigned on = (rowv[rr] >> kk) & 1u;
                        for (yy = 0; yy < 6u; yy++) {
                            volatile uint32_t *p = FB_DIR((20u + rr * 9u + yy) * FB_W + 8u + kk * 12u);
                            uint32_t v = (on || yy == 0u) ? 0xFFFFFFFFu : 0u;
                            p[0] = v; p[1] = v;
                        }
                    }
            }
#endif
#ifdef R60_HOLDDIRECT
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
#endif
            { r60_publish_planes(); R60_PUB = go; }  /* FB packet, at FM=1 */
            R60_EVPUTS(2); MBT(go, 8); MBTV(go, 23, R60_DIAG[0]);   /* publish done (code 2) */
            RS(8);
            r60_cram_commit(); R60_DIAG2[3]++;       /* PEN-waited per bank */
            R60_EVPUTS(11);
            RS(9);
            fs_o = MARS_VDP_FBCTL & MARS_VDP_FS;
#ifdef R60_HOLDDIRECT
            if ((R60_HOLDFLIP && R60_DISPHOLD == 1u) || (R60_PKTA_LEN[R60_PKTDONE & 1u] & R60_PKTA_DIRECT)) { R60_SHOWN = go; }   /* no flip: under the hold with the shown bank black, or a direct packet just published for this bank */
            else
#endif
            {
#if defined(R60_MBAND) && !defined(R60_MBAND_FLIPA)
            /* THE FLIP WAITS FOR THE 68K's ACK (2026-10-24). The 68K consumes the packet from the hidden
             * bank the moment FM drops (R60_IDLECONSUME); a flip requested here lands at the next vblank,
             * and a pass that ends within a consume of the vblank has the bank switch UNDER the consume:
             * the MD planes came back with torn cells (black and purple blocks in the wall band, MBAND
             * null probe, frames 600-1700). The line's 254-line pass never ended there; a shorter pass
             * does. So FS is written when the span closes (FM up: an FS write at FM=0 is dropped), after
             * the consume: the flip lands at the vblank after the ack, never under it. */
            r60_flip_due = 1;
#else
#ifndef R60_NOFLIP
            MARS_VDP_FBCTL = (uint16_t)(fs_o ^ 1);   /* deferred to the next vblank */
#endif
            *(volatile uint16_t *)0x20004102u = R60_SHIFTNEXT;   /* the odd pixel of the canvas offset, with the flip */
#endif
            r60_fs_want = (uint16_t)(fs_o ^ 1);
            R60_FPEND = 1; R60_SHOWN = go; R60_DIAG[1]++;
            r60_bkp ^= 1u;   /* sw226: the physical bank advances only with a flip */
            }
            R60_SEEN = MARS_SYS_COMM0;
            R60_SPANV = (uint16_t)R60_DIAG[0];
            R60_FMDOWN = 1;
            R60_FM_DOWN();                           /* the 68K's span opens NOW */
            R60_EVPUTS(13);
        }
#endif
        RS(10);
        R60_DONE = go;
    }
}

#ifdef R60_MBAND
/* THE MASTER'S WAIT FOR CLEARED (cart, once a vint; see m_main) */
static __attribute__((noinline)) void r60_band_wait_cleared(void)
{
    uint16_t _t = r60_frt();
    MBST(3);
#ifndef R60_MBAND_NOWAIT
    /* 2026-10-24 (R60MBTIME): with the 25-line cap the band missed every CLEARED posted past
     * ~line 81 (a pass that started late or cleared long), the walk ran first and the band
     * started 80-160 lines after CLEARED. Now: 25 lines as before when no pass is in flight;
     * while the slave is in a pass that has not posted CLEARED yet, up to R60_MBAND_WAITX. */
    for (;;) {
        uint16_t el, g = R60_GO;                  /* read once: see r60_band */
        if (R60_CLEARED == g && R60_MDONE != g) break;
        el = (uint16_t)(r60_frt() - _t);
        if (el >= R60_MBAND_WAITX) break;
        if (el >= 4600u && !(R60_DONE != g && R60_CLEARED != g)) break;
    }
#endif
    R60_MB[3] += (uint16_t)(r60_frt() - _t);
}
#endif
#ifdef R60_FRONTEND
__attribute__((noinline)) static int r60_frontend_service(void)
{
    volatile uint8_t *w = (volatile uint8_t*)0x2602CC00u;
    uint16_t command = MARS_SYS_COMM12;
    if (command == FE_REQUEST && MARS_SYS_COMM6 == FE_SLAVE_IDLE) {
        extern void port_frontend_settings(unsigned);
        frontend_paused = 1;
        R60_FM_DOWN();
        MARS_SYS_COMM12 = FE_GRANTED;
        while (MARS_SYS_COMM12 == FE_GRANTED) {
            unsigned delay;
            for (delay = 0; delay < 256u; delay++) __asm__ volatile ("nop");
        }
        command = MARS_SYS_COMM12;
        if ((command & 0xFF00u) == FE_RESULT) port_frontend_settings(command & 0xFFu);
        pp_due = 0;
        R60_FM_UP();
        frontend_paused = 0;
        MARS_SYS_COMM12 = 0;
    } else if (!FE_WAITING(command)) {
        /* The arcade sequencer's title step is 8; never intercept gameplay. */
        MARS_SYS_COMM6 = (!(w[0x3028] & 3u) && w[0x3031] == 8u && (w[0x3018] & 0x20u))
                       ? FE_TITLE : 0xB600u;
    }
    return FE_WAITING(MARS_SYS_COMM12);
}
#endif

R60CODE void m_main(void)
{
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
    Hw32xInit(MARS_VDP_MODE_256, 0);
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
    {   /* both banks, then back to where we started */
        uint16_t fs0 = MARS_VDP_FBCTL & MARS_VDP_FS;
        r60_line_table(); r60_bank_init();
        MARS_VDP_FBCTL = (uint16_t)(fs0 ^ 1u);
        while ((MARS_VDP_FBCTL & MARS_VDP_FS) == fs0) ;
        r60_line_table(); r60_bank_init();
        MARS_VDP_FBCTL = fs0;
        while ((MARS_VDP_FBCTL & MARS_VDP_FS) != fs0) ;
    }
    R60_GO = 0; R60_DONE = 0;
#ifdef R60_MBAND
    R60_CLEARED = 0xFFFFu; R60_MDONE = 0xFFFFu;         /* no band stands for pass 0 */
#ifdef R60_MBAND_TUNE
    R60_MBAND_F = R60_MBAND_F0; R60_MBDUR = 0;
#endif
    R60_MSPAN_N(0) = 0; R60_MSPAN_N(1) = 0; R60_MSPAN_FULL(0) = 0; R60_MSPAN_FULL(1) = 0;
    { unsigned k9; for (k9 = 0; k9 < 7u; k9++) R60_MB[k9] = 0; }
#ifdef R60_MBAND_STEAL
    R60_MSTART = 0xFFFFu; R60_SSTEAL = 0xFFFFu; R60_MRESULT = 0;
#endif
    R60_PH_MWAIT = 0;
    if (!r60_pt_init) r60_pt_build();                   /* the master's own copy of the pen tables (same bytes; the slave builds its own view) */
#endif
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
#ifdef R60_PLANES
    r60_planes_reset();
#endif
#ifdef R60_NOVDPOVERLAP
    /* PROBE D's WHOLE LOOP. Everything that touches the VDP region happens in one
     * window, between the slave finishing and the next go starting; while the slave
     * draws, the master runs the game tick and nothing else. */
    for (;;) {
        while (pp_due) { pp_due--; port_play_tick(); R60_DIAG[11]++; }
        if (R60_DONE != R60_GO) continue;        /* the slave is drawing: hands off */
        r60_cram_commit();                       /* VDP palette region */
        r60_publish_planes();                    /* VDP DRAM region */
        {                                        /* VDP register region */
            uint16_t fs_o = MARS_VDP_FBCTL & MARS_VDP_FS, tw;
            MARS_VDP_FBCTL = (uint16_t)(fs_o ^ 1);
            r60_fs_want = (uint16_t)(fs_o ^ 1);
            tw = r60_frt();
            while ((uint16_t)(MARS_VDP_FBCTL & MARS_VDP_FS) != r60_fs_want
                   && (uint16_t)(r60_frt() - tw) < 12000u)
                ;
            R60_DIAG[1]++;
            R60_SHOWN = R60_GO;
        }
        R60_SEEN  = MARS_SYS_COMM0;              /* COMM is not the VDP region */
        R60_SPANV = r60_frame;
        R60_FM_DOWN();
        while (MARS_SYS_COMM0 == R60_SEEN
               && (uint16_t)(r60_frame - R60_SPANV) < 2u)
            ;
        if (MARS_SYS_COMM0 != R60_SEEN) R60_DIAG[14]++; else R60_DIAG[15]++;
        R60_FM_UP();
        R60_GO = (uint16_t)(R60_GO + 1);
        R60_DIAG[9]++;
    }
#endif
#ifdef R60_SLAVEWIN
    for (;;) {
#ifdef R60_RIGSPLIT
        RM(0); RSX[23]++;
#endif
#ifdef R60_MBTIME
        if (pp_due) { MBST(1); MBTG(2); }
#endif
#ifdef R60_EVPROBE
        if (pp_due) r60_evp(9u);            /* PROBE: tick start (master code 9) */
#endif
#ifdef R60_FRONTEND
        if (r60_frontend_service()) pp_due = 0;
#endif
        while (pp_due) { pp_due--; port_play_tick(); R60_DIAG[11]++; RM(1); }   /* RIGSPLIT: a stamp a tick (a 16-bit FRT delta wraps at 356 lines) */
#ifdef R60_MBTIME
        MBTG(3); MBST(2);
#endif
        {   /* PUBLISH THE SPRITE TABLE after the tick, into the slot the slave is not
             * reading. One tick per vint and a pass under a vint means the other slot is
             * always free; if the slave is still on it, skip this vint and count it. */
            uint16_t seq = R60_SNAPSEQ, nxt = (uint16_t)(seq + 1u);
            if (R60_DRAWSEQ != (uint16_t)(seq - 1u) || R60_DONE == R60_GO) {
                const volatile uint32_t *sp = (const volatile uint32_t *)SPR_SNAP;
                volatile uint32_t *dp = (volatile uint32_t *)R60_SNAPSLOT(nxt);
                unsigned i;
                for (i = 0; i < 256u; i++) dp[i] = sp[i];
#ifdef R60_MBAND
                R60_SPLIT(nxt) = (uint16_t)r60_split_row(R60_SNAPSLOT(nxt));   /* the split row, from the snapshot (cart) */
#endif
                R60_SNAPSEQ = nxt; R60_EVPUT(6); MBTG(4);
            } else { R60_DIAG2[9]++; R60_EVPUT(7); }   /* snapshot skipped: slot busy */
        }
        RM(2);
#ifdef R60_MBAND
        {   /* THE BAND FIRST, the walk after: the band is on the slave's critical path, the
             * walk is not (its packet is taken at the end of the pass). Wait for the clear
             * (bounded: 4600 master ticks = 100 lines; the idle loop below catches a later one),
             * draw, then walk. */
            r60_band_wait_cleared(); RM(3);
            r60_band(); RM(4);
        }
        MBST(5);
#endif
#if defined(R60_PLANES)
        r60_planes_prepare();
#endif
        RM(5);
        MBTS(4); MBT(R60_GO, 19); MBST(6);   /* prepare end, into the pass running now */
#ifdef R60_EVPROBE
        r60_evp(10u);                       /* PROBE: prepare end (master code 10) */
#endif
        if (!R60_DISPSET && r60_frame > 120u && R60_DONE == R60_GO && !R60_FMDOWN) {
            MARS_VDP_DISPMODE = MARS_NTSC_FORMAT | MARS_224_LINES
                              | MARS_VDP_PRIO_32X | MARS_VDP_MODE_256;
            R60_DISPSET = 1;
        }
        {   /* idle until the next vint, logging the slave's transitions on the master's clock */
            uint16_t ld = R60_DONE, lg = R60_GO, lf = R60_FMDOWN;
            while (!pp_due) {
#ifdef R60_STAGEDIRECT
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
#endif
#ifdef R60_MBAND
                RM(0); r60_band(); RM(6);            /* a band the wait above missed (a late clear) */
#endif
#ifdef R60_MBTIME
                R60_MBTG[1] = r60_frt();
#endif
                if (R60_GO != lg)     { lg = R60_GO;     R60_EVPUT(2); }
                if (R60_DONE != ld)   { ld = R60_DONE;   R60_EVPUT(3); }
                if (R60_FMDOWN != lf) { lf = R60_FMDOWN; R60_EVPUT(lf ? 8 : 4); }
            }
        }
    }
#endif
    for (;;) {
#ifndef R60_TICKLATE
        while (pp_due) {                 /* the game's own clock, drained per vint */
            pp_due--;
            port_play_tick();
            R60_DIAG[11]++;
        }
#endif
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
#ifndef R60_NOFLIP
                MARS_VDP_FBCTL = (uint16_t)(fs_o ^ 1);
#endif
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
#if defined(R60_FZ)
            && !R60_FREEZE                      /* the probe's whole readout */
#endif
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

#ifdef R60_TICKLATE
            {   /* the slave's private sprite table, taken while it is idle */
                const volatile uint32_t *s = (const volatile uint32_t *)SPR_SNAP;
                volatile uint32_t *d = (volatile uint32_t *)R60_SNAP2;
                unsigned i;
                for (i = 0; i < 256u; i++) d[i] = s[i];
            }
#endif
            R60_GO = (uint16_t)(R60_GO + 1);   /* FM is up again: the slave may draw */
            R60_EVPUT(2);
            R60_DIAG[9]++;               /* gos launched */
        }
#ifdef R60_TICKLATE
        /* THE GAME TICK RUNS AFTER GO, WHILE THE SLAVE DRAWS. Measured on cw1g
         * (GOVERNOR=1): with the tick at the top of the loop, GO still advanced every
         * second vint -- the vblank raises pp_due, the loop spends ~130 lines in the tick
         * before it looks at the 68K's post, the slave's 150-line pass then runs past the
         * next vblank, and the frame costs two vints. Here the post is answered first. */
        while (pp_due) {
            pp_due--;
            port_play_tick();
            R60_DIAG[11]++;
        }
#endif
    }
}

#ifdef R60_SPEECH
#include "r60_speech.inc"   /* the speech + SFX lanes, in this unit so their calls are not opaque */
#endif
