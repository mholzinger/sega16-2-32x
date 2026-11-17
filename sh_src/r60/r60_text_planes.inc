/* r60_text_planes.inc -- the text scan and plan, and the MD plane packet (prepare, publish).
 * Part of r60_main.c: included there, in this order, so the compiler sees one unit. */
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
/* THE ODD HOLD PACKET OFF THE CANVAS (R60PKTODDHI, 2026-10-24). At 0x11800 it overlapped the canvas
 * (rows 222-223 end at 0x11C00, the spill at 0x11E00): every odd direct packet left its words as
 * pixels on the bottom two lines of the bank it landed in, which no sprite clear removes (ares,
 * sw312 attract frame 3290: 437 bytes in rows 222-225 of the shown bank, visible on line 223).
 * Staged packets are at most R60_PKTA_CAP_STAGE / 2 + 8 longwords (8,032 B): 0x11E00..0x13D60. */
#define R60_PKT_A_ALT ((volatile uint16_t *)(FB_WBASE + 0x11E00u))  /* 68K 0x851E00 (md_src/r60_shim.h R60_PKTODD_68K) */
#define R60_PKT_A_ALTW ((volatile uint16_t *)0x24011E00u)
#define R60_PKT_A_DW  ((volatile uint16_t *)0x24017000u)            /* the even-parity direct packet, written uncached in place */
#define R60_HOLDFLIP  (*(volatile uint16_t *)0x26028FBAu)           /* slave -> master: no flips while the hold lasts (the write bank is stable) */
#define R60_PKTA_DIRECT 0x80000000u                                 /* R60_PKTA_LEN flag: built in place, nothing to copy */
#define R60_BUILDING  (*(volatile uint16_t *)0x26028FBCu)           /* master -> slave: a direct build is in progress, keep FM up */
#define R60_FMSOON    (*(volatile uint16_t *)0x26028FBEu)           /* slave -> master: FM is about to drop, do not start a build */
#define R60_FMSOON_CLR() (R60_FMSOON = 0)
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
#define R60_PKTA_CAP_DIRECT 11264u
#define R60_ARTMAX_DIRECT 500u

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
static uint16_t r60_rects_mem[2][2][64][4] __attribute__((aligned(16)));   /* [list][bank][i] x0 x1 y0 y1 */
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
/* STABLE GROUPS. A pal keeps its group while its colours stay the group's colours, and a
 * group whose only member cycles its colours keeps the group with the new colours (the
 * CRAM entries change, the pens do not). Rebuilding the map from scratch each window
 * moved every pal's group whenever one pal cycled ("INSERT COIN" flashes), and the slave
 * then redrew all 1,120 cells every pass: 185 lines (sw38). */
/* CART, 2026-11-04: .ramtext had 44 bytes left and the pool-15 borrow count did not fit; it sat
 * in .ramtext2 for a day until the baked-art copy (r60_bk_sprite, per sprite on the slave) needed
 * that room. Once a window on the master, whose demand walk already runs from cart; the FPGA pays
 * the cart fetch on it (watch the master's idle share on the rig, 60% on 2026-11-03). */
__attribute__((noinline)) static void r60_text_plan(void)
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
    {   /* THE POOL, keyed by COLOUR (2026-10-16 evening). Groups are gone: every pal is its
         * own LUT row, and a slot holds one colour word shared by every (pal, pen) that wants
         * it (pals 5 and 6 share four, the 7fff highlights are one slot: the high-score table
         * asks ~27 distinct colours of the 31). A pair keeps its slot while its colour stays;
         * a pair whose colour changes recolours its slot in place when it is the only user
         * (the "INSERT COIN" cycle rewrites CRAM, not the LUT), else moves. */
        static uint8_t penmask[512], pm_init, pair_slot[64], users[R60_POOLN];
        static uint16_t slotcol[R60_POOLN], lut_shadow[64];
        uint8_t demand[8];
        unsigned row, col, s, pr, changed = 0, borrowed = 0;
        unsigned lutpals = 0;
        if (!pm_init) {
            unsigned c2, k;
            for (c2 = 0; c2 < 512u; c2++) { unsigned m = 0; for (k = 0; k < 64u; k++) m |= 1u << (game_tiles[c2 * 64u + k] & 7u); penmask[c2] = (uint8_t)m; }
            for (s = 0; s < R60_POOLN; s++) { users[s] = 0; slotcol[s] = 0xFFFEu; }
            for (pr = 0; pr < 64u; pr++) { pair_slot[pr] = 0xFFu; lut_shadow[pr] = 0; }
            pm_init = 1;
        }
        for (pal = 0; pal < 8u; pal++) { R60_TXTGRP[pal] = (uint16_t)pal; demand[pal] = 0; }   /* identity: pal = LUT row */
        for (g = 0; g < 8u; g++) R60_TXTPAL[g] = (uint16_t)g;
        r60_tx_demand(demand, penmask);          /* cart: the cached demand when the text is unchanged */
        (void)row; (void)col;
        /* ENTRY 255 IS THE BAR PEN: the commit forces it white and skips it, so a text pair
         * seated there draws WHITE whatever its colour (p15h, 2026-11-04: the blue "2 CREDITS 1
         * PLAY" of the high-score table came out white; the old pool's slot 30 was the same
         * entry and only the 31st colour ever met it). Never a slot. */
#define R60_SLOTBAR(s) (R60_POOLBYTE(s) == R60_BAR_PEN)
        unsigned busy = (unsigned)R60_BANKBUSY | 1u, p0mask = 0;
#define R60_SLOTOK(s) (!R60_SLOTBAR(s) && ((s) < 16u || !((busy >> ((s) - 15u)) & 1u)))
        /* 0. a pen-0 slot whose bank a sprite now draws in gives its pairs up: they re-seat below */
        for (s = 16; s < R60_POOLN; s++) if (!R60_SLOTOK(s) && users[s]) {
            for (pr = 0; pr < 64u; pr++) if (pair_slot[pr] == s) pair_slot[pr] = 0xFFu;
            users[s] = 0;
        }
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
            for (s = 0; s < R60_POOLN; s++) if (users[s] && slotcol[s] == want) break;            /* share */
            if (s == R60_POOLN) for (s = 0; s < R60_POOLN; s++) if (!users[s] && slotcol[s] == want && R60_SLOTOK(s)) break;   /* a free slot that still holds it */
            if (s == R60_POOLN) for (s = 0; s < R60_POOLN; s++) if (!users[s] && R60_SLOTOK(s)) break;                   /* any free slot (pen 15 first) */
            if (s < R60_POOLN) { slotcol[s] = (uint16_t)want; users[s]++; pair_slot[pr] = (uint8_t)s; }
            /* else: full; the LUT below borrows the nearest colour */
        }
        for (s = 0; s < R60_POOLN; s++) {
            uint16_t nv = users[s] ? slotcol[s] : 0xFFFEu;
            if (R60_POOLCOL[s] != nv) {
                R60_POOLDIRTY = 1;
                /* a pen-0 entry seated <-> freed changes its through bit even when the colour word
                 * does not (black text over entry 0's black): force its CRAM write */
                if (s >= 16u) r60_cram_shadow[R60_POOLBYTE(s)] = 0xFFFEu;
                if (s >= 16u && users[s]) p0mask |= 1u << (s - 15u);
            }
            else if (s >= 16u && users[s]) p0mask |= 1u << (s - 15u);
            R60_POOLCOL[s] = nv;
        }
        R60_POOL0MASK = (uint16_t)p0mask;
        for (pr = 0; pr < 64u; pr++) {
            unsigned pl = pr >> 3, c = pr & 7u, byte = 0;
            if (c && ((demand[pl] >> c) & 1u)) {
                if (pair_slot[pr] != 0xFFu) byte = R60_POOLBYTE(pair_slot[pr]);
                else {                                      /* borrowed: nearest allocated colour */
                    unsigned want = PAL_SH[pl * 8u + c], best = 0xFFFFFFFFu;
                    borrowed++;
                    for (s = 0; s < R60_POOLN; s++) if (users[s]) {
                        unsigned cw = slotcol[s], dd = 0, ch;
                        for (ch = 0; ch < 3u; ch++) { int a = (int)((want >> (ch * 4u)) & 15u) - (int)((cw >> (ch * 4u)) & 15u); dd += (unsigned)(a * a); }
                        if (dd < best) { best = dd; byte = R60_POOLBYTE(s); }
                    }
                }
            }
            if (lut_shadow[pr] != byte) { lut_shadow[pr] = (uint16_t)byte; changed = 1;
                lutpals |= 1u << pl;
            }
            R60_TXTLUT[pr] = (uint16_t)byte;
        }
        if (changed) R60_TXTLUTSEQ++;
        r60_txt_borrow[0] += borrowed; if (borrowed > r60_txt_borrow[1]) r60_txt_borrow[1] = borrowed;
        if (borrowed) {   /* would pen 0 of the banks idle for three passes have seated them? (bank 0 is entry 0, never a slot) */
            unsigned busy = (unsigned)__builtin_popcount(R60_BANKBUSY | 1u);
            if (borrowed > 16u - busy) r60_txt_borrow[2]++;
            if (busy > r60_txt_borrow[3]) r60_txt_borrow[3] = busy;
        }
        if (changed) R60_TXTLUTPM = ((uint32_t)R60_TXTLUTSEQ << 16) | lutpals;
    }
}

#ifndef MBT   /* the R60MBTIME stamps compile away outside MBAND */
#define MBT(go_, f_)      ((void)0)
#define MBTV(go_, f_, v_) ((void)0)
#define MBTG(k_)          ((void)0)
#define MBST(st_)         ((void)0)
#define MBTS(k_)          ((void)0)
#endif
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
    MBTS(1);
    static unsigned row0, win;
    uint16_t seq = R60_PKTSEQ, nxt = (uint16_t)(seq + 1u);
    volatile uint16_t *sa = R60_PKTSTAGE(nxt, 0), *sb = R60_PKTSTAGE(nxt, 1);
    unsigned direct = 0;
    /* BUILD ONLY WHEN THE LAST PACKET HAS BEEN TAKEN. The walk updates its shadow as it
     * builds, so a window the slave never copied (a two-vint frame) was lost for good:
     * whole name-table rows stayed empty on the title (sw26: NT A 576 of 800, rows 17-26)
     * until the arcade changed them. */
    r60_census[5]++;                             /* census: prepare entries (sw193 probe) */
    if (seq != R60_PKTDONE) { R60_DIAG2[15]++; return; }
    /* THE DIRECT ABORT COMES FIRST (sw178). It sat below the text diff, the window count, the plan
     * and the gate, so every attempt the slave refused still advanced GATE_SINCE, the holes ring
     * and the quiet count: the hold released on windows that built nothing (sw177 demo 2: twelve
     * aborted attempts in one hold, release with rows 17-20 never filled). SH-2 framebuffer stores
     * at FM=0 are dropped (sw169): build only while the slave holds FM, and tell it so first (sw170
     * tore packets when its wait ran out). The FM bit itself is read last: a flag can lag it. */
    direct = (R60_DISPHOLD == 1u) && R60_HOLDFLIP;
    if (r60_stage_full) {                        /* a packet is built: land it the moment FM is up, nothing else this call */
        unsigned n = (r60_stage_words + 1u) / 2u, i;
        volatile uint32_t *d = (volatile uint32_t *)((r60_stage_nxt & 1u) ? R60_PKT_A_ALTW : R60_PKT_A_DW);
        const volatile uint32_t *src = (const volatile uint32_t *)R60_STAGE_DIRECT;
        R60_BUILDING = 1;
        if (R60_FMDOWN || R60_FMSOON || !R60_FM_IS_UP) { R60_BUILDING = 0; return; }
        if (n > R60_PKTA_CAP_STAGE / 2u + 8u) n = R60_PKTA_CAP_STAGE / 2u + 8u;
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
    r60_tx_scan();
    r60_text_plan();
    r60_tx_publish();
    win++;
    /* THE PLAN AND THE GATE. A plan adopted mid-fill flushes the cache and restarts the
     * fill (sw78: plans 5 -> 6 at ~f495, holes 94 -> 878, the hold ran to f530). So: no
     * periodic plan while held; one plan four windows into a hold (the game has loaded
     * the scene behind its blank by then; sets that appear later pack on demand without
     * a flush); and none for 32 windows after release. */
    {   /* WHICH SPRITE COLOUR SETS CHANGED: the master reads its own palette cached and hands
         * the slave a bank mask, so the commit compares only those (it compared all 256
         * entries uncached every pass: ~10 lines). */
        static uint16_t pal_m[256]; static unsigned pal_init;
        const uint16_t *pc = (const uint16_t *)0x06027000u + 1024u;
        unsigned b, q, m = 0;
        if (!pal_init) { for (b = 0; b < 256u; b++) pal_m[b] = 0xFFFEu; pal_init = 1; }
        for (b = 0; b < 16u; b++) {
            unsigned pl = R60_BANKPAL_U[b]; if (pl > 63u) pl = b;
            for (q = 0; q < 16u; q++) { uint16_t v = pc[pl * 16u + q]; if (v != pal_m[b * 16u + q]) { pal_m[b * 16u + q] = v; m |= 1u << b; } }
        }
        if (m) R60_CRAMMASK_W |= (uint16_t)m;
    }
    {
        static unsigned plan_quiet, plan_hard_quiet, plan_hard_wait, plan_hard_last;
        extern unsigned r60_unlined;
        if (plan_hard_quiet) plan_hard_quiet--;
        unsigned held = R60_DISPHOLD && !r60_bulk_hold;
        { extern void r60_pix_track(void); r60_pix_track(); }   /* 2026-10-25: per (set, pixel) colour stability, every window (r60_planes.c) */
        /* sw283: ran only under R60_PENIDX, which no candidate carries, so no pen was ever recoloured or split on the line (the crystal ball's one-pen picture, Mike's sw282.bs3: DIAG2[13] = 0). */
        if ((win & 3u) == 2u) { extern void r60_recolour_pens(void); r60_recolour_pens(); }   /* a palette write is a CRAM write, not new art; every 4th window: every window cost 9% of the windows and 4 frames at the cuts (sw283) */
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
        unsigned rej = (unsigned)R60_DIAG2[7], holes, k, dlast = rej - rej_last;
        ring[ri] = dlast; ri = (ri + 1u) % 5u; rej_last = rej;
        for (holes = 0, k = 0; k < 5u; k++) holes += ring[k];
        R60_GATE_HOLES = holes;
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
            if (load_hold) min_windows = 4u;
            if ((since >= min_windows && quiet >= 2u) || since >= R60_HOLD_MAX) {
                hold = 0; R60_GATE_REL++; R60_CRAM_RESEND();
                load_hold = 0; r60_bulk_hold = 0;
            }
        }
        if (hold) R60_GATE_HELD++;                   /* windows held */
        /* the 68K sees bit 13 one vint after the packet is built; the slave sees this word
         * at once, so it stays blank one window longer or the sprites lead the planes by
         * a frame (sw79 f490: sprites over black) */
        R60_MDHOLD = (uint16_t)hold;     /* the MD display stays off under our hold: what the 32X has not painted yet is black, not the MD's half-filled planes (sw98 showed magenta there); the sw93 slowdown was the scene's own sprite load, not the display */
        /* the slave's mode: 1 = the game's own blank, 2 = our hold (the 32X paints the planes), 0 = MD planes */
        R60_DISPHOLD = (uint16_t)(!ven ? 1u : ((hold | hold_prev) ? 1u : 0u)); hold_prev = hold;   /* without the 32X fill our hold blanks like the game's */
#undef since
    }
    r60_artcap = (R60_DISPHOLD == 1u) ? R60_FATCAP : (R60_DISPHOLD == 2u) ? 24u : 40u;
    r60_artcap = r60_live_artcap(r60_artcap);
    {   extern unsigned r60_pkta_cap, r60_artmax;
        if (direct) { sa = R60_STAGE_DIRECT; r60_pkta_cap = R60_PKTA_CAP_STAGE; r60_artmax = R60_ARTMAX_STAGE; if (r60_artcap < R60_ARTMAX_STAGE) r60_artcap = R60_ARTMAX_STAGE; }
        else { r60_pkta_cap = 3072u; r60_artmax = 160u; }
    }
    MBTS(2);
    { uint32_t _f0 = R60_DIAG[0];
      R60_DIAG2[6] = r60_planes_build(sa, sb, row0, win);
      MBTS(3);
      R60_BUILD_T = (uint16_t)(R60_DIAG[0] - _f0); if (R60_BUILD_T > R60_BUILD_TMAX) R60_BUILD_TMAX = R60_BUILD_T;   /* census: the walk + art of this window in VINTS (the FRT form wrapped at 1.36 frames, sw193) */
      r60_census[7] += R60_BUILD_T; }
    { extern unsigned r60_walk_rows; row0 = (row0 + r60_walk_rows - 7u) & 31u; }   /* the rotation advances by the rows walked (+7 below) */
    { extern unsigned r60_pkta_words, r60_pktb_words; R60_PKTA_LEN[nxt & 1u] = r60_pkta_words; R60_PKTA_LEN[2u + (nxt & 1u)] = r60_pktb_words; }   /* per parity: the slave copies seq while the master builds nxt */
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
    if (direct) {
        R60_PKTA_LEN[nxt & 1u] |= R60_PKTA_DIRECT;
        if (nxt & 1u) {                          /* the odd packet sits at 0x11800: a stub at 0x17000 points the 68K there */
            volatile uint16_t *st = R60_PKT_A_DW;
            st[1] = (uint16_t)(sa[1] | 0x0800u); st[2] = sa[2]; st[3] = sa[3]; st[4] = sa[4]; st[5] = 0; st[6] = sa[6]; st[7] = sa[7];
            st[0] = 0xB6B6u;
        }
    }
    R60_DIAG2[7] += R60_DIAG2[6];                /* total art records built (probe 2026-10-15) */
    row0 = (row0 + 7u) & 31u;
    if (direct && (R60_FMDOWN || !R60_FM_IS_UP)) R60_TORN_M++;   /* FM fell under a direct build: its stores were dropped and its rows are in the shadow (sw177 seq 872, rows 17-30 of demo 2 lost for the scene) */
    sa[0] = 0xB6B6u; sb[0] = 0xB6B6u;
    R60_PKTSEQ = nxt;
    R60_BUILDING = 0;
    R60_DIAG[7]++;
}

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

R60CODE static void r60_publish_planes(void)
{
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
          if (la & R60_PKTA_DIRECT) na = 0; else
          { na = (la + 1u) / 2u; if (na < 8u) na = 8u; if (na > 1536u) na = 1536u; }
          if (nbw < 8u) nbw = 8u; if (nbw > 376u) nbw = 376u;
          for (i = 0; i < na; i++) da[i] = sa[i];
          for (i = 0; i < nbw; i++) db[i] = sb[i];
          if (!R60_DISPHOLD
              && !(la & R60_PKTA_DIRECT)
          ) r60_refresh_viewport();
        }
        R60_PKTDONE = seq;
        return;
    }
    volatile uint16_t *sc = R60_PKT_A;
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
