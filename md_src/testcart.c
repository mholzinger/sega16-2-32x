/* 32X TEST CARTRIDGE, phase 1 (docs/design/TESTCART.md, 2026-09-27).
 *
 *   make line TESTCART=1        -> rom/s16.32x, boots and runs the suite
 *
 * WHAT IT IS. A machine-CHARACTERISATION suite, not a conformance one: it
 * reports what THIS machine does and a diff across machines is the finding.
 * TESTCART.md says why -- on three facts already (cart-sourced VDP DMA, the
 * RV=1 identity map, the MiSTer core's ROM DTACK wait) the truth came from a
 * divergence and a hard-coded "correct" would have been wrong. (The RV=1 map
 * turned out not to exist at all: T25 measures RV=0 and md_main.c's UNPAIR
 * MODEL says it stays 0 forever. That is the suite finding its own premise
 * stale, which is the same mechanism working.)
 *
 * WHERE IT RUNS FROM. Right after main()'s boot completes, in the same slot
 * BOOT_BANK3HALT uses -- both SH-2s up, RV set, the screen ours -- then it
 * never returns. Bringing up a 32X from scratch took this project weeks; the
 * suite reuses that rather than repeating it.
 *
 * READOUT. Full-screen GREEN if every test passed, RED if any failed, plus a
 * result block at work RAM 0xFFE100 for per-test detail. See the readout
 * comment below for why phase 1 does not use the name-table grid.
 *
 * BARCODE BYTES 3 and 4 are this suite's (5 and 6 belong to PORTTEST, agreed
 * 2026-09-27) -- not yet wired; the grid is the phase-1 readout.
 */
#include "common.h"
#if defined(TEST_CART) || defined(R60_FRONTEND)

#define DRQ_WORDS 64u          /* must match DRQ_N in sh_src/dreq_proof.h */
#define TC_MAX 72
/* FIXED SCRATCH, not .bss. 32 results plus 32 longs is 160 bytes, and .bss
 * here is already close enough to the WRAMCODE region that the link FAILED on
 * it twice -- once when the DMA buffers were statics and again when the test
 * count grew. Every array this suite owns lives at a known address in
 * 0xFFE000-0xFFE9FF, which is also why the results survive for a savestate. */
/* THE SUITE'S SCRATCH MAP. Written down because I put these two arrays at
 * 0xFFE900 first, INSIDE the FMDL staging buffer, and the transport test
 * overwrote its own results -- the table came up showing 0x2081, 0x2083, ...,
 * which is the staging pattern 0x2000+i read back as verdicts. Fixed addresses
 * avoid one hazard and invite another; the map is the answer to the second.
 *
 *   FFE000  T00/T01 patterns
 *   FFE100  result block: magic, run, passed, then one byte per test
 *   FFE200  timing read target
 *   FFE280  4-word DMA source (T06)
 *   FFE300  256-word DMA source (T0D/T0E)
 *   FFE800  736-word FMDL staging, ends FFEEC0
 *   FFF200  tc_v[72], the measured values      (288B, ends FFF320)
 *   FFF320  tc_res[72], the verdicts            ( 72B, ends FFF368)
 *   FFF380  tc_order[72], the paint order       ( 72B, ends FFF3C8)
 *
 * The three grew 32 -> 40 (the Z80 page) -> 48 (the COLOUR page) and at 48
 * they no longer fit under FFF000, so they MOVED OUT of the FFE000 window
 * entirely rather than being squeezed. FFF200 is clear of the game's own work
 * RAM that the shim touches -- 0xFFF02A the scene timer, 0xFFF095 the bank
 * request, 0xFFF0C0-D4 the MCU mailboxes (game_irq.h) -- which is why the base
 * is 0xFFF200 and not 0xFFF000. The game never runs under this build, but a
 * probe that silently aliases the shim's mailboxes is exactly the class of bug
 * the FMDL staging overlap already cost four rebuilds.
 */
#define tc_res ((volatile uint8_t *)0xFFF320)
#define tc_v   ((volatile uint32_t*)0xFFF200)
/* tc_v is the MEASURED VALUE, which is the point:
                                     * a verdict cannot be compared across
                                     * machines and a number can. Two machines
                                     * that both "fail" a timing test say
                                     * nothing until you see 48 against 12. */
static uint16_t tc_n;
/* HOW MANY TESTS ASKED TO RUN, including any the table had no room for. The
 * three tc_* recorders drop silently past TC_MAX -- and on 2026-09-27 three
 * FMDL rows were dropped exactly that way while the table happily showed the
 * COLOUR page's verdicts under their names, because the name list and the
 * results are two arrays that agree only by counting. A silent drop that
 * renders as a PASS is the worst failure an instrument can have. */
static uint16_t tc_asked;

/* EXPLICIT WIDTHS EVERYWHERE. The MD side builds with -mshort, so
 * `unsigned int` is SIXTEEN BITS: the first run of this file failed five of
 * eight tests entirely on that, because 32-bit constants could not match a
 * 16-bit read and the VDP control writes went out half-width. None of those
 * were machine findings. Use uint16_t/uint32_t and nothing else here. */
#define VCTRL (*(volatile uint16_t*)VDP_CTRL_PORT)
#define VWIDE (*(volatile uint32_t*)VDP_CTRL_PORT)
#define VDATA (*(volatile uint16_t*)VDP_DATA_PORT)

static void tc(int ok) { tc_asked++; if (tc_n < TC_MAX) { tc_v[tc_n] = 0; tc_res[tc_n++] = ok ? 1 : 2; } }
static void tc_val(int ok, uint32_t v) { tc_asked++; if (tc_n < TC_MAX) { tc_v[tc_n] = v; tc_res[tc_n++] = ok ? 1 : 2; } }
/* A SWEEP'S FAILURE VALUE NAMES THE CELL, NOT THE COUNT (decompile thread,
 * CARD-DECOMPILE-C-TRAPS 2026-09-27: "a FAIL must report the cell that failed,
 * not the first cell compared"). Five sweeps here reported `bad`, a COUNT --
 * and on real hardware, where the screen is the whole channel, "5 cells wrong"
 * is not actionable while "cell 0x2B read 0x46" is. So a clean sweep reports 0
 * and a dirty one reports:
 *     bits 31-24  how many cells failed
 *     bits 15- 8  the index of the FIRST one
 *     bits  7- 0  the low byte of what it actually read
 * The table shows the low four digits -- index and value, the part you act on
 * -- and the dump carries the count in the top byte. */
#define TC_SWEEP(n_, first_, got_) \
    ((n_) ? (((uint32_t)(n_) << 24) | ((uint32_t)((first_) & 0xFFu) << 8) \
             | ((uint32_t)(got_) & 0xFFu)) : 0ul)

/* REPORT-ONLY: a timing figure has no right answer, only a value to diff. */
static void tc_meas(uint32_t v) { tc_asked++; if (tc_n < TC_MAX) { tc_v[tc_n] = v; tc_res[tc_n++] = 3; } }

/* ---- the clock. MD raster counter at 0xC00008: V in the high byte, H in the
 * low; vblank is V >= 0xE0 (md_main.c:588 uses the same test). Resolution is
 * one LINE, which is the unit the whole project already reasons in ("mean pass
 * 362-400 lines", "halts the 68K for ~48 lines"), so workloads here are sized
 * to span tens of lines rather than fractions of one. Each measurement waits
 * for a known line first so it never straddles the frame wrap. ---- */
#define TC_V() ((uint16_t)((*(volatile uint16_t*)0xC00008) >> 8))
static void tc_wait_line(uint16_t v)
{
    uint32_t guard = 200000ul;
    while (TC_V() != v && --guard) ;
}
/* WAIT FOR THE DMA. Triggering a VDP DMA returns immediately, so two things
 * went wrong at once: the DMA "timing" measured the trigger and read about
 * zero lines, and the transfer was still in flight when the next VDP accesses
 * came -- the font upload and the palette writes were SWALLOWED while the
 * later name-table writes, by then clear of it, landed. The screen came up a
 * uniform colour with perfectly good text invisible behind it.
 * Status register bit 1 (0x0002) is DMA busy. */
/* A SHORT WAIT THAT IS STILL QUIET. tc_hold(1) is ~3 frames of busy loop, and
 * the two cross-processor handshakes polled with it up to 40 times -- about 40
 * seconds before the splash on a machine where both complete in a frame. This
 * waits ONE frame by the raster counter instead: as cheap as tc_hold on the
 * bus (work RAM only, no adapter access) and 3x finer. */
static void tc_frame_wait(void)
{
    uint32_t g = 200000ul;
    while (TC_V() < 0xE0u && --g) ;          /* into vblank */
    g = 200000ul;
    while (TC_V() >= 0xE0u && --g) ;         /* and out again */
}
static void tc_hold(uint16_t units);
static void tc_frame_wait(void);
static void tc_run_late(void);
static void tc_dma_wait(void)
{
    uint32_t guard = 500000ul;
    while ((*(volatile uint16_t*)VDP_CTRL_PORT & 0x0002u) && --guard) ;
}

static uint16_t tc_lines_since(uint16_t v0)
{
    uint16_t v1 = TC_V();
    return (uint16_t)((v1 >= v0) ? (v1 - v0) : (v1 + 262u - v0));
}

/* ---- the tests. Each is self-checking: it decides its own verdict, and
 * where a behaviour is expected to be ABSENT the same mechanism is exercised
 * in the positive direction first as a same-frame control. A probe without a
 * control produces confident nonsense (LESSONS, twice). ---- */
static void tc_run_all(void)
{
#ifdef TC_NOTESTS
    tc(1); tc(1); tc(1); tc(1); tc(1); tc(1); tc(1); tc(1);
    return;
#endif
    /* T0/T1 -- work RAM, the harness's own control. If these fail nothing
     * below means anything. */
    {
        volatile uint16_t *w = (volatile uint16_t*)0xFFE000;
        *w = 0xA55A; tc(*w == 0xA55A);
    }
    {
        volatile uint32_t *l = (volatile uint32_t*)0xFFE004;
        *l = 0x12345678ul; tc(*l == 0x12345678ul);
    }

    /* T2/T3 -- cart through the BANK WINDOW, the only route the 68K has to
     * cart (0x900000 + winoff, bank -> 0xA15104). Bank 3 is the resting value
     * every site restores, and cart 0x300400 is the arcade program's `bra` at
     * 0x400 -- the same constant BOOT_BANK3HALT probes. */
    {
        *(volatile uint16_t*)0xA15104 = 3;
        tc(*(volatile uint32_t*)0x900400 == 0x6000000Cul);
        tc(*(volatile uint16_t*)0x900400 == 0x6000u);   /* word view agrees */
    }

    /* T4 -- MD VRAM write and read back through the data port. */
    {
        uint16_t got;
        VWIDE = ((uint32_t)(0x4000u | (0xB000u & 0x3FFFu)) << 16) | ((0xB000u >> 14) & 3u);
        VDATA = 0x5AA5;
        VWIDE = ((uint32_t)(0x0000u | (0xB000u & 0x3FFFu)) << 16) | ((0xB000u >> 14) & 3u);
        got = VDATA;
        tc(got == 0x5AA5);
    }

    /* T5 -- MD CRAM write and read back. Entry 60 (palette 3 pen 12) is
     * outside the 61-63 the readout below claims. */
    {
        uint16_t got;
        VWIDE = ((uint32_t)(0xC000u | 120u) << 16) | 0u;
        VDATA = 0x0246;
        VWIDE = ((uint32_t)(0x0000u | 120u) << 16) | 0x20u;   /* CRAM read */
        got = (uint16_t)(VDATA & 0x0EEEu);
        tc(got == 0x0246);
    }

    /* T6 -- VDP DMA from WORK RAM to VRAM LANDS. This is the positive control
     * for the cart-DMA test that phase 2 adds: the 2026-09-19 finding is that
     * cart-sourced DMA does not arrive while a WRAM source in the same frame
     * does, and the second half is what makes the first half mean anything. */
    {
        volatile uint16_t *srcbuf = (volatile uint16_t*)0xFFE280;
        uint32_t src;
        uint16_t got;
        srcbuf[0] = 0xC33C; srcbuf[1] = 0xC33C; srcbuf[2] = 0xC33C; srcbuf[3] = 0xC33C;
        src = ((uint32_t)srcbuf) >> 1;
        VCTRL = 0x8F02;                            /* autoinc 2 */
        VCTRL = (uint16_t)(0x9300u | 4u);    /* length 4 words */
        VCTRL = (uint16_t)(0x9400u | 0u);
        VCTRL = (uint16_t)(0x9500u | (src & 0xFFu));
        VCTRL = (uint16_t)(0x9600u | ((src >> 8) & 0xFFu));
        VCTRL = (uint16_t)(0x9700u | ((src >> 16) & 0x7Fu));
        VWIDE = ((uint32_t)(0x4000u | (0xB100u & 0x3FFFu)) << 16)
              | (((0xB100u >> 14) & 3u) | 0x80u);  /* DMA trigger */
        tc_dma_wait();
        VWIDE = ((uint32_t)(0x0000u | (0xB100u & 0x3FFFu)) << 16) | ((0xB100u >> 14) & 3u);
        got = VDATA;
        tc(got == 0xC33C);
    }

    /* T7 -- the SH-2 side is alive. main() already waited for the master's
     * 0x600D on COMM14 before reaching here, so a zero would mean the boot
     * handshake regressed rather than that the test is new. */
    tc(*(volatile uint16_t*)MARS_COMM14 != 0);

#if !defined(TC_BISECT)
    /* ================ WHAT THESE TIMINGS SHOULD BE ================
     * 2026-09-27: thirteen of these rows stopped being baselines. Every loop
     * below was pulled out of the linked ELF (LTO inlines them into main();
     * `objdump -D rom/md_start.elf`, they sit in .data around 0xFF2670) and
     * costed against the 68000 instruction timings. One NTSC line is
     * 53.693175 MHz / 7 / (262 x 59.92) = 488.57 68K cycles.
     *
     * A RASTER LINE IS 477.9 68K CYCLES ON THIS MACHINE, measured, and every
     * prediction below uses that. The nominal arithmetic -- 53.693175 MHz / 7
     * / (262 x 59.92) -- gives 488.57, and it is WRONG BY 2.2% for a reason
     * nobody has established.
     *
     * THERE IS NO RESIDUAL. Two earlier readings of this page were wrong in
     * the same way and both are retired here. The first called the gap a
     * CONSTANT ~2 lines of harness overhead; the second called it a
     * proportional 2.24% derate applied to a correct prediction. Both kept
     * 488.57 and invented something to absorb the difference. At 477.9 the
     * nine instruction-derived rows land within 0.3 of a line with nothing
     * subtracted at all:
     *
     *      test  cycles   at 488.57   at 477.9   measured
     *      T08    43008        88.0       90.0         90
     *      T0B    30720        62.9       64.3         64
     *      T14    51200       104.8      107.1        107
     *      T15    19968        40.9       41.8         42
     *      T1A    34816        71.3       72.9         73
     *
     * The cycle table was never off, and there was never a cost to subtract.
     * A constant discrepancy across several tests is a WRONG CONSTANT before
     * it is an additive overhead -- the decompile thread's reading, and it is
     * the better one because it stops rather than starts an explanation.
     *
     * WHAT IS STILL OPEN is why 477.9 and not 488.57: either the frame is not
     * 262 lines at 59.92 Hz on these machines, or ~2.2% of 68K cycles are
     * being stolen (VDP DRAM refresh is the right size). T24 below is the row
     * that settles it on the rig, and the answer decides whether 477.9
     * transfers to real silicon or is an emulator constant.
     *
     *   T08  movew (xxx).L,Dn 16 + movew Dn,(d16,An) 12 + subqw 4 + bnes 10
     *        = 42 cyc  x1024 / 488.57 =  88.03   vs  90 measured
     *   T09  same four instructions, source 0x900400   =  88.03   vs  90
     *   T0B  movew Dn,(xxx).L 16 + subqw 4 + bnes 10
     *        = 30 cyc  x1024          =  62.88   vs  64
     *   T0C  as T08                                    =  88.03   vs  90
     *   T13  moveb forms, same costs as T08            =  88.03   vs  90
     *   T14  movel (xxx).L,Dn 20 + movel Dn,(d16,An) 16 + 4 + 10
     *        = 50 cyc  x1024          = 104.80   vs 107
     *   T15  movew Dn,Dn 4 + andiw 8 + movew Dn,(xxx).L 16
     *        + movew (xxx).L,Dn 16 + movew Dn,(d16,An) 12 + addqw 4
     *        + cmpiw 8 + bnes 10 = 78 cyc  x256   =  40.87   vs  42
     *   T17  as T0B                                    =  62.88   vs  64
     *   T1A  moveb #imm,(xxx).L 20 + 4 + 10
     *        = 34 cyc  x1024          =  71.26   vs  73
     *
     * AND FIVE ROWS FALL OUT OF ONE RTL CONSTANT, which is the better result.
     * T0A and T16 are the SAME 113 lines despite costing 30 and 42 cycles an
     * iteration -- a loop cannot measure the same as a 40%-cheaper loop unless
     * something other than the 68K is the limiter. It is the VDP's external
     * access slots. Counted from rtl/GEN/vdp.sv:1003-1038, H40 active display:
     *
     *   the slot pattern repeats every 16 H_CNT counts and SLOT_CE fires on
     *   odd H_CNT (vdp.sv:1001), so the candidate is H_CNT == 3 (mod 16);
     *   over H_CNT 0..319 (the H40 branch, H_CNT[8:6] < 5) that is 20 slots,
     *   of which the five with H_CNT[5:4] == 2'b11 become ST_REFRESH
     *   (vdp.sv:1025) leaving 15; plus the three explicit H40 externals at
     *   H_CNT[8:1] == 0xA0, 0xA1, 0xE7 (vdp.sv:1033-1037).
     *
     *   => 18 EXTERNAL ACCESS SLOTS PER LINE.
     *
     * VRAM is byte-wide, so a VRAM WORD costs two slots and a fill's BYTE
     * costs one. That single constant predicts:
     *
     *   T0A  1024 VRAM words / (18/2 per line) = 113.78   vs 113 measured
     *   T16  1024 VRAM words, same budget      = 113.78   vs 113
     *   T0D   256 DMA words to VRAM, active    =  28.44   vs  28
     *   T18   256 DMA fill BYTES, one slot     =  14.22   vs  14
     *   T19   256 DMA copy words               =  28.44   vs  28
     *
     * and it explains T17 too: a CRAM word is ONE slot, so the loop's own
     * 16.29 accesses a line sit under the 18-slot budget and it stays
     * instruction-bound at 62.88 lines. T0A runs the identical instructions
     * and is slot-bound because VRAM doubles the cost. That is why MD CRAM
     * writes are 64 lines and MD VRAM writes are 113.
     *
     * TWO ROWS ARE EVIDENCE OF AN ARES GAP RATHER THAN A MEASUREMENT. T0B and
     * T0C land on their pure instruction cost, which means ares charges
     * NOTHING for a 32X framebuffer access -- the FB-write bus-stall floor
     * that CLAUDE.md already records as unmodelled, arrived at here from the
     * other direction. Those numbers are the emulator's, not the machine's,
     * and the rig is where they mean something.
     *
     * STILL REPORT-ONLY WITH NO DERIVATION: T0E (DMA in vblank -- nearly every
     * slot is external there, vdp.sv:1004-1008, but pinning the count needs
     * the exact H_CNT period and 2 lines is too coarse to fit against) and
     * T0F-T12, which time OUR OWN code and so have no closed form.
     *
     * ================ BUS TIMING, in raster lines ================
     * The measured value is what the machine did; the "vs" above is what it
     * should have done. A deviation is a FINDING, not a failure -- the finding
     * the diff between machines. The pairs are deliberate: WRAM against cart
     * isolates the fetch route, and DMA in active display against DMA in
     * vblank is the VDP choke point this project has measured twice from the
     * other direction (a 384-word burst halting the 68K ~48 lines). */
    {
        uint16_t v0, i;
        volatile uint16_t *w = (volatile uint16_t*)0xFFE200;
        volatile uint16_t junk = 0;

        /* T8 -- 1024 work-RAM word reads */
        tc_wait_line(0x10); v0 = TC_V();
        for (i = 0; i < 1024u; i++) junk = *w;
        tc_meas(tc_lines_since(v0));

        /* T9 -- 1024 cart reads through the bank window. The MiSTer core holds
         * the 68K's DTACK for a full SDRAM access on every MD ROM read and a
         * mask-ROM cartridge does not, so this is the number that tells real
         * hardware from the core. */
        *(volatile uint16_t*)0xA15104 = 3;
        tc_wait_line(0x10); v0 = TC_V();
        for (i = 0; i < 1024u; i++) junk = *(volatile uint16_t*)0x900400;
        tc_meas(tc_lines_since(v0));

        /* T10 -- 1024 VDP data-port writes to VRAM */
        VCTRL = 0x8F02;
        VWIDE = ((uint32_t)(0x4000u | (0xB200u & 0x3FFFu)) << 16) | ((0xB200u >> 14) & 3u);
        tc_wait_line(0x10); v0 = TC_V();
        for (i = 0; i < 1024u; i++) VDATA = 0x1234;
        tc_meas(tc_lines_since(v0));

        /* T11/T12 -- the 32X framebuffer, 1024 word writes then 1024 reads.
         * A 68K FB write at FM=1 is DROPPED on both machines, so the FM bit is
         * checked first and the pair reports 0xFFFF rather than a meaningless
         * figure if FM is up. */
        if (*(volatile uint16_t*)0xA15100 & 0x8000) {
            tc_meas(0xFFFFul); tc_meas(0xFFFFul);
        } else {
            volatile uint16_t *fb = (volatile uint16_t*)0x840200;
            tc_wait_line(0x10); v0 = TC_V();
            for (i = 0; i < 1024u; i++) fb[0] = 0x55AA;
            tc_meas(tc_lines_since(v0));
            tc_wait_line(0x10); v0 = TC_V();
            for (i = 0; i < 1024u; i++) junk = fb[0];
            tc_meas(tc_lines_since(v0));
        }

        /* T13/T14 -- the same 256-word WRAM->VRAM DMA in ACTIVE DISPLAY and in
         * VBLANK. The gap between these two is the VDP's bandwidth story in
         * one number each. */
        {
            /* the DMA source lives at a FIXED scratch address, not in .bss:
             * 512 bytes of statics here pushed 68K .bss into the WRAMCODE
             * region and the link failed outright. 0xFFE000-0xFFE3FF is this
             * suite's scratch and nothing else claims it. */
            volatile uint16_t *dsrc = (volatile uint16_t*)0xFFE300;
            uint32_t src;
            uint16_t k;
            for (k = 0; k < 256u; k++) dsrc[k] = 0x7E7E;
            src = ((uint32_t)dsrc) >> 1;
            VCTRL = 0x8F02;
            VCTRL = (uint16_t)(0x9300u | 0u);          /* length 256 words */
            VCTRL = (uint16_t)(0x9400u | 1u);
            VCTRL = (uint16_t)(0x9500u | (src & 0xFFu));
            VCTRL = (uint16_t)(0x9600u | ((src >> 8) & 0xFFu));
            VCTRL = (uint16_t)(0x9700u | ((src >> 16) & 0x7Fu));
            tc_wait_line(0x10); v0 = TC_V();
            VWIDE = ((uint32_t)(0x4000u | (0xB400u & 0x3FFFu)) << 16)
                  | (((0xB400u >> 14) & 3u) | 0x80u);
            tc_dma_wait();
            tc_meas(tc_lines_since(v0));

            /* RE-SET THE LENGTH, not just the source. A completed DMA has
             * counted its length register down to ZERO, and a length of 0
             * means 65,536 words -- the second transfer then filled the whole
             * of VRAM, font and name table included, and the screen came up as
             * noise. Length first, every time. */
            VCTRL = (uint16_t)(0x9300u | 0u);
            VCTRL = (uint16_t)(0x9400u | 1u);
            VCTRL = (uint16_t)(0x9500u | (src & 0xFFu));
            VCTRL = (uint16_t)(0x9600u | ((src >> 8) & 0xFFu));
            VCTRL = (uint16_t)(0x9700u | ((src >> 16) & 0x7Fu));
            tc_wait_line(0xE8); v0 = TC_V();
            VWIDE = ((uint32_t)(0x4000u | (0xB600u & 0x3FFFu)) << 16)
                  | (((0xB600u >> 14) & 3u) | 0x80u);
            tc_dma_wait();
            tc_meas(tc_lines_since(v0));
        }
        (void)junk;
    }

#endif
    /* ================ THE TRANSPORT ITSELF ================
     * The harness this port is built on: the 68K serialises a per-frame
     * command list into the 32X FRAMEBUFFER and the SH-2 lifts and executes
     * it, all inside one vertical interrupt or the frame is lost. Real
     * figures, not a synthetic stand-in -- 736 words (688 body + 48 palette
     * shadow, md_main.c:1403) at the real packet base 0x851A00.
     *
     * Three legs, each in raster lines, and then the number that decides the
     * ceiling: what share of a 262-line frame the transport costs before any
     * rendering happens at all. */
    if (!(*(volatile uint16_t*)0xA15100 & 0x8000)) {     /* needs FM down */
        volatile uint16_t *stage = (volatile uint16_t*)0xFFE800;
        volatile uint16_t *fb = (volatile uint16_t*)0x851A00uL;
        volatile uint16_t junk = 0;
        uint16_t v0, i, asm_l, blast_l, lift_l;

        tc_wait_line(0x10); v0 = TC_V();
        for (i = 0; i < 736u; i++) stage[i] = (uint16_t)(0x2000u + i);
        asm_l = tc_lines_since(v0);   tc_meas(asm_l);

        tc_wait_line(0x10); v0 = TC_V();
        for (i = 0; i < 736u; i++) fb[i] = stage[i];
        blast_l = tc_lines_since(v0); tc_meas(blast_l);

        tc_wait_line(0x10); v0 = TC_V();
        for (i = 0; i < 736u; i++) junk = fb[i];
        lift_l = tc_lines_since(v0);  tc_meas(lift_l);

        /* the ceiling, as a percentage of one 262-line frame */
        tc_meas((uint32_t)((asm_l + blast_l) * 100u) / 262u);
        (void)junk;
    } else {
        tc_meas(0xFFFFul); tc_meas(0xFFFFul);
        tc_meas(0xFFFFul); tc_meas(0xFFFFul);
    }

    /* ================ BUS WIDTH, the mapper, and the rest of the VDP ====== */
    {
        uint16_t v0, i;
        volatile uint8_t  *b = (volatile uint8_t*)0xFFE200;
        volatile uint32_t *l = (volatile uint32_t*)0xFFE200;
        volatile uint8_t  jb = 0; volatile uint32_t jl = 0; volatile uint16_t jw = 0;

        /* T: 1024 BYTE reads and 1024 LONG reads, against the word figure
         * already measured. Three widths over the same address is the 68K's
         * bus shape in three numbers -- a long is two bus cycles, a byte one,
         * so the ratios say whether a machine models that or charges flat. */
        tc_wait_line(0x10); v0 = TC_V();
        for (i = 0; i < 1024u; i++) jb = *b;
        tc_meas(tc_lines_since(v0));
        tc_wait_line(0x10); v0 = TC_V();
        for (i = 0; i < 1024u; i++) jl = *l;
        tc_meas(tc_lines_since(v0));

        /* T: 256 BANK SWITCHES, each followed by a read. The mapper write is
         * the cost the slim pipeline pays per art fetch, and it has never been
         * priced on its own. */
        tc_wait_line(0x10); v0 = TC_V();
        for (i = 0; i < 256u; i++) {
            *(volatile uint16_t*)0xA15104 = (uint16_t)(i & 3u);
            jw = *(volatile uint16_t*)0x900400;
        }
        tc_meas(tc_lines_since(v0));
        *(volatile uint16_t*)0xA15104 = 3;        /* bank 3 is the resting value */

        /* T: 1024 VRAM READS, against the write figure. A VDP read costs more
         * than a write on real silicon; whether a machine charges that is
         * exactly the kind of thing only a diff reveals. */
        VWIDE = ((uint32_t)(0x0000u | (0xB200u & 0x3FFFu)) << 16) | ((0xB200u >> 14) & 3u);
        tc_wait_line(0x10); v0 = TC_V();
        for (i = 0; i < 1024u; i++) jw = VDATA;
        tc_meas(tc_lines_since(v0));

        /* T: 1024 MD CRAM writes (entry 60, which nothing displays) */
        VWIDE = ((uint32_t)(0xC000u | 120u) << 16) | 0u;
        tc_wait_line(0x10); v0 = TC_V();
        for (i = 0; i < 1024u; i++) VDATA = 0x0246;
        tc_meas(tc_lines_since(v0));

        /* T: the other two VDP DMA modes, same 256 words, same frame position
         * as the WRAM-source figure -- FILL and VRAM-to-VRAM COPY. Three modes
         * priced identically is the VDP's bandwidth in three numbers. */
        VCTRL = 0x8F02;
        VCTRL = (uint16_t)(0x9300u | 0u); VCTRL = (uint16_t)(0x9400u | 1u);
        VCTRL = 0x9780;                          /* DMA mode: VRAM fill */
        tc_wait_line(0x10); v0 = TC_V();
        VWIDE = ((uint32_t)(0x4000u | (0xB800u & 0x3FFFu)) << 16)
              | (((0xB800u >> 14) & 3u) | 0x80u);
        VDATA = 0x3C3C;                          /* fill needs one data write */
        tc_dma_wait();
        tc_meas(tc_lines_since(v0));

        VCTRL = (uint16_t)(0x9300u | 0u); VCTRL = (uint16_t)(0x9400u | 1u);
        VCTRL = (uint16_t)(0x9500u | 0x00u); VCTRL = (uint16_t)(0x9600u | 0xB8u);
        VCTRL = 0x97C0;                          /* DMA mode: VRAM copy */
        tc_wait_line(0x10); v0 = TC_V();
        VWIDE = ((uint32_t)(0x0000u | (0xBA00u & 0x3FFFu)) << 16)
              | (((0xBA00u >> 14) & 3u) | 0xC0u);
        tc_dma_wait();
        tc_meas(tc_lines_since(v0));
        VCTRL = 0x9700;                          /* mode back to WRAM source */

        /* T: the 32X framebuffer's BYTE view, against its word figure. The FB
         * is byte-addressable and the port writes bytes constantly. */
        if (!(*(volatile uint16_t*)0xA15100 & 0x8000)) {
            volatile uint8_t *fbb = (volatile uint8_t*)0x840200;
            tc_wait_line(0x10); v0 = TC_V();
            for (i = 0; i < 1024u; i++) fbb[0] = 0x5A;
            tc_meas(tc_lines_since(v0));
        } else tc_meas(0xFFFFul);
        (void)jb; (void)jl; (void)jw;
    }

    /* T: the 32X bitmap-mode register readback. The UI's whole foundation is
     * that writing 0 here blanks the layer; this checks the write LANDS, which
     * is a different claim from the layer going away and worth separating. */
    {
        uint16_t m;
        *(volatile uint16_t*)0xA15180 = 0;
        m = (uint16_t)(*(volatile uint16_t*)0xA15180 & 3u);
        tc_val(m == 0, m);
    }

    /* ================ the 32X CRAM PEN rule ================
     * 32X palette access is gated on PEN -- hblank or vblank only
     * (srcref/S32X_MiSTer rtl/32X/VDP.sv:170, :403/:405) -- and a write during
     * active scan is SILENTLY DROPPED. That is the whole reason CRAMISR=1
     * exists on the line.
     *
     * THE RULE HAS TWO DOCUMENTED EXCEPTIONS AND MY FIRST TEST RAN INSIDE ONE.
     * ~/src/32XDK/docs/32x-hardware-manual.md:1240 --
     *
     *     "Palette access is possible only during H and V blank."
     *     "Palette can access whenever the bitmap mode is in the direct color
     *      mode, as well as during Blank."
     *
     * so DIRECT COLOR MODE and BLANK MODE both lift it. T1B runs immediately
     * above and writes 0 to the bitmap mode register -- M1:M0 = 00 = BLANK
     * MODE -- so the original T1D wrote CRAM during active scan with the 32X
     * blanked, which the manual ALLOWS. It landed on ares and on the FPGA and
     * I recorded that as "either the rule does not cover the 68K's path, or
     * both emulating machines miss it, real hardware decides". Neither: the
     * machines were right and the test was exercising the exception.
     *
     * So T1D now puts the VDP in PACKED PIXEL MODE first, which is a
     * DISPLAYING mode and outside both exceptions, and reads the result back
     * in the NEXT vblank rather than during active scan -- the rule gates
     * READS too, so reading where the write was refused would measure the
     * read and not the write.
     * T1C (the vblank write) is still T1D's same-run control: without it a
     * machine that cannot write 32X CRAM at all would "pass" T1D. */
    {
        volatile uint16_t *bmmr = (volatile uint16_t*)0xA15180;
        volatile uint16_t *c32 = (volatile uint16_t*)(0xA15200 + 200u * 2u);
        uint16_t got_vb, got_act;
        tc_wait_line(0xE8); *c32 = 0x1234; got_vb = *c32;
        tc_val(got_vb == 0x1234, got_vb);

        /* NTSC | packed pixel: the layer is DISPLAYING, so neither exception
         * applies. Restored to blank before the readout paints. */
        tc_wait_line(0xE8);
        *bmmr = 0x8001;
        *c32 = 0x0000;                  /* in vblank: allowed, so this lands */
        tc_wait_line(0x60);
        *c32 = 0x5678;                  /* active scan: the rule says refused */
        tc_wait_line(0xE8);
        got_act = *c32;                 /* read back where reads are allowed */
        *bmmr = 0x0000;                 /* blank mode again for the readout */
        tc_meas(got_act);               /* 0000 refused (the rule holds),
                                         * 5678 landed (it does not) */
        /* WHAT THE MACHINES SAY, 2026-09-27. ares 5678, BlastEm 5678: BOTH
         * EMULATORS LET IT LAND. The manual says refused, and the MiSTer core
         * implements the manual exactly --
         *   rtl/32X/VDP.sv:402-406
         *     if (H_CNT == 9'h157+3-1 || VBLK || !MODE[0]) PEN <= 1;
         *     else if (H_CNT == 9'h017-1)                  PEN <= 0;
         *   rtl/32X/VDP.sv:170  palette access is gated on PEN
         * and !MODE[0] is precisely the manual's two exceptions, Blank (00)
         * and Direct Color (10). In packed pixel (01) during active scan PEN
         * is LOW and the core must refuse.
         *
         * SO THE PREDICTION FOR THE RIG IS T1D = 0000, and it is worth more
         * than the row: sh_src cram_paint has NO PEN GUARD. If the rule holds
         * on silicon, this port's palette writes during active scan are
         * SILENTLY DROPPED on real hardware -- a hardware-only defect that
         * neither emulator can show and that the FPGA would only show once
         * the test stopped running inside the exception. */
    }

    /* ================ THE Z80 / SOUND BUS PAGE (2026-09-27) ================
     * Zero coverage before today, and the only page in this suite whose
     * expectations come from somewhere other than an ares capture. TWO
     * PROJECTS HIT THE SAME FAILURE INDEPENDENTLY, from two different
     * symptoms, which is the strongest evidence the cart carries:
     *
     *   md_src/md_start.s:175 (ours, at boot) -- "HOLD IT IN RESET,
     *   permanently: one write, no bus-grant poll (TWO POLL VARIANTS BOTH
     *   HUNG BOOT HERE -- grant never arrives in this config)".
     *
     *   ~/src/32x-builder md_src/md_main.c:545 (its SMS harness) -- "ORDER IS
     *   LAW (see the SMS BOOT case): release reset BEFORE the bus request or
     *   the grant never comes."
     *
     * So T1E's expectation -- a bus request against a RESET-HELD Z80 never
     * gets its grant -- is DERIVED, and it is graded. Ours found it as a boot
     * hang, theirs as a machine "left odd"; neither found it by capturing a
     * number and calling it the reference.
     *
     * STATE ON ENTRY is the boot park: reset asserted (md_start.s:180 writes
     * 0xA11200 = 0, forever), bus not requested. That is exactly what T1E
     * needs, so it runs first and sets nothing up.
     *
     * AND THE PAGE MUST PUT THAT STATE BACK. A released Z80 free-runs
     * uninitialised RAM into the YM2612 and the PSG -- the clipping Mike
     * heard, and the whole reason the permanent reset park exists. The
     * cleanup is ordered to undo both paths: the final reset resets the
     * YM2612 with it, and the PSG (which lives in the VDP and is NOT reset by
     * it, md_start.s:183) is re-muted by hand.
     *
     * UNIT FOR THE LATENCIES: poll iterations, not raster lines. A bus grant
     * takes tens of 68K cycles and a line is 488, so the raster counter
     * cannot resolve one. Iterations of one fixed loop in one rom ARE
     * comparable across machines, which is all a characterisation suite
     * needs. The guard is 0xFFFF so that a timeout reports 0xFFFF -- it reads
     * as "never granted" on screen instead of as a large number.
     */
    {
        volatile uint16_t *zbus = (volatile uint16_t*)0xA11100;
        volatile uint16_t *zrst = (volatile uint16_t*)0xA11200;
        volatile uint8_t  *zram = (volatile uint8_t *)0xA00000;
        uint32_t g;
        uint16_t i;

        /* T1E -- BUSREQ against a reset-held Z80. Expect NO grant. Bit 8 of
         * 0xA11100 reads 1 while the Z80 still holds the bus, 0 once granted
         * (the same poll 32x-builder's harness uses). */
        *zbus = 0x100;
        g = 0xFFFFul;
        while ((*zbus & 0x100u) && --g) ;
        tc_val(g == 0, (uint32_t)(0xFFFFul - g));
        *zbus = 0x000;                    /* drop the request either way */

        /* T1F -- reset released FIRST, then BUSREQ. Expect a grant, and the
         * iteration count is the latency. T1E is this test's same-run
         * control: without it a machine that grants unconditionally would
         * pass T1F and say nothing at all. */
        *zrst = 0x100;
        for (i = 0; i < 64u; i++) ;       /* let it leave reset -- the same
                                           * 64-count wait 32x-builder uses */
        *zbus = 0x100;
        g = 0xFFFFul;
        while ((*zbus & 0x100u) && --g) ;
        tc_val(g != 0, (uint32_t)(0xFFFFul - g));

        /* T20 -- Z80 RAM BEFORE WE WRITE IT. 32x-builder's "phantom-mailbox-
         * command lesson" is that it boots 0xFF, which is why that harness
         * zeroes its four mailboxes by hand rather than trusting the copy.
         *
         * IT IS NOT A VIRGIN READ AND CANNOT BE, which the first version of
         * this test got wrong. Reaching Z80 RAM needs the bus grant; the grant
         * needs reset released (T1E proves it); and a released Z80 runs. The
         * first version sampled byte 0 and 0x1FF7 and the two machines
         * disagreed on the second -- 0xFF on ares, 0x00 on the FPGA. That is
         * not a memory-init difference. A Z80 leaving reset with SP = 0 and
         * 0xFF in RAM executes RST 38h, which PUSHES: SP wraps to the top of
         * the 8K space and the stack walks DOWN through 0x1FFE, 0x1FFC, ...
         * straight over 0x1FF7. The test was reading the Z80's own stack and
         * the delta was how many instructions each machine let it retire.
         *
         * So it samples 0x0800 and 0x1200 instead: clear of a PC walking up
         * from the reset vector, clear of a stack walking down from the top,
         * and clear of T21's pattern at 0x1000. Report-only either way -- what
         * powers-on memory holds is something real silicon settles and an
         * emulator guesses. ORDER MATTERS: this must precede T21's writes. */
        tc_meas((uint32_t)(((uint16_t)zram[0x0800] << 8) | zram[0x1200]));

        /* T21 -- byte readback through the 0xA00000 window, under the grant
         * T1F just took. 32x-builder uploads 5888 bytes of Z80 program this
         * way, so the window working is load-bearing for anything Z80 at all.
         * The pattern goes at 0x1000, clear of the reset vector and the RST
         * handlers, so that the brief Z80 run in T23 cannot execute it. The
         * expectation is tautological -- what was written must read back --
         * which is the strongest kind there is. */
        {
            uint16_t bad = 0, first = 0, gotv = 0;
            for (i = 0; i < 256u; i++) zram[0x1000 + i] = (uint8_t)(i ^ 0x5Au);
            for (i = 0; i < 256u; i++)
                if (zram[0x1000 + i] != (uint8_t)(i ^ 0x5Au)) {
                    if (!bad) { first = i; gotv = zram[0x1000 + i]; }
                    bad++;
                }
            tc_val(bad == 0, TC_SWEEP(bad, first, gotv));
        }

        /* T22 -- a 68K WORD write into the Z80 window. The Z80 bus is EIGHT
         * BITS wide, so what a word write does is a characterisation question
         * and not a conformance one: it may land as the high byte only, may
         * mirror into both halves, or may not land at all.
         *
         * IT RUNS AFTER T21 ON PURPOSE, and that is what makes it readable:
         * 0x1010/0x1011 are inside T21's pattern, so the PRIOR contents are
         * known to be 0x4A and 0x4B (i ^ 0x5A) rather than whatever the
         * machine powers up holding. So the result decodes with no ambiguity:
         *   0x1234  both bytes landed
         *   0x124B  the EVEN byte landed, the odd byte kept T21's value
         *   0x1212  the high byte mirrored into both halves
         *   0x4A4B  nothing landed
         * ares reads 0x124B (2026-09-27): even byte only. */
        *(volatile uint16_t*)0xA01010 = 0x1234;
        tc_meas((uint32_t)(((uint16_t)zram[0x1010] << 8) | zram[0x1011]));

        /* T23 -- the SECOND grant, warm. T1F's number alone cannot separate a
         * cold-path cost from a steady-state one, and a measurement without a
         * control produces confident nonsense (LESSONS, twice). */
        *zbus = 0x000;
        for (i = 0; i < 64u; i++) ;
        *zbus = 0x100;
        g = 0xFFFFul;
        while ((*zbus & 0x100u) && --g) ;
        tc_meas((uint32_t)(0xFFFFul - g));

        /* PUT THE PARK BACK, exactly as md_start.s:180 left it, in this
         * order: reset first (it resets the YM2612 with it), then the bus,
         * then the PSG latches by hand because reset does not reach them. */
        *zrst = 0x000;                    /* reset asserted, forever */
        *zbus = 0x000;                    /* bus released */
        {
            volatile uint8_t *psg = (volatile uint8_t*)0xC00011;
            *psg = 0x9F; *psg = 0xBF; *psg = 0xDF; *psg = 0xFF;
        }
    }

    /* ================ THE 68K TIMES ITSELF ================
     * Every derived row on the TIMING page rests on two numbers taken from
     * outside: the 68000's published instruction costs, and 488.57 68K cycles
     * per NTSC line (53.693175 MHz / 7 / (262 x 59.92)). Both are almost
     * certainly right. Neither had been measured ON THIS MACHINE, and a
     * derivation standing on an unmeasured constant is one assumption away
     * from being a guess.
     *
     * So the 68K times itself, in ASSEMBLY rather than C. That is the point:
     * every other timing test had to be disassembled afterwards to learn what
     * the compiler chose. Here the instruction sequence IS the specification.
     *
     *     1: subq.w #1,Dn   4 cycles
     *        bne.s 1b      10 taken, 8 on the last pass
     *     => 14n - 2 cycles for n iterations
     *
     * DIFFERENTIAL, so the harness overhead cancels exactly. Two runs of the
     * same loop differ by 4096 x 14 = 57,344 cycles and by NOTHING else --
     * same entry, same trailing read, same jsr.
     *
     *     T24 = lines(6144 iters) - lines(2048 iters)
     *           => 68K cycles per line = 57344 / T24    (theory: 117 lines)
     *     T25 = lines(4096 iters + a nop) - lines(4096 iters)
     *           => one NOP = T25 x cycles-per-line / 4096  (theory: 33.5)
     *
     * THE ARITHMETIC IS DELIBERATELY NOT DONE HERE. Computing 57344/T24 on a
     * -mshort 68K pulls libgcc's 32-bit divide and multiply in, and the first
     * cut of this test did exactly that -- .bss grew 68 bytes past the boot
     * stack guard and the LINK FAILED. A cart that reports raw line counts is
     * simpler, cannot overflow, and loses nothing: the conversion is one
     * division and it belongs wherever someone is already reading the table.
     *
     * If T24 reads 117 and T25 reads 33, the 68000 table and the line
     * constant both hold on this machine, and the TIMING page's nine
     * instruction-derived rows are standing on measured ground. */
    {
        uint16_t v0, a, b, n;

        n = 2048u;
        tc_wait_line(0x10); v0 = TC_V();
        __asm__ __volatile__("1: subq.w #1,%0\n\tbne.s 1b" : "+d"(n) :: "cc");
        a = tc_lines_since(v0);

        n = 6144u;
        tc_wait_line(0x10); v0 = TC_V();
        __asm__ __volatile__("1: subq.w #1,%0\n\tbne.s 1b" : "+d"(n) :: "cc");
        b = tc_lines_since(v0);
        tc_meas((uint16_t)(b - a));                 /* T24: 57344 cycles */

        n = 4096u;
        tc_wait_line(0x10); v0 = TC_V();
        __asm__ __volatile__("1: subq.w #1,%0\n\tbne.s 1b" : "+d"(n) :: "cc");
        a = tc_lines_since(v0);

        n = 4096u;
        tc_wait_line(0x10); v0 = TC_V();
        __asm__ __volatile__("1: nop\n\tsubq.w #1,%0\n\tbne.s 1b" : "+d"(n) :: "cc");
        b = tc_lines_since(v0);
        tc_meas((uint16_t)(b - a));                 /* T25: 4096 nops */
    }

    tc_run_late();
    /* THE LAST TEST GRADES THE TABLE. If any recorder was refused for want of
     * room, tc_asked outran tc_n and every row above is suspect -- the names
     * and the results are two arrays that agree only by counting. */
    { uint16_t over = (uint16_t)(tc_asked - tc_n); tc_val(over == 0, over); }
}

/* THE LATE TESTS LIVE IN ROM, and that is a space decision with a timing
 * consequence worth stating. tc_run_all is inlined into main(), which md.ld
 * puts in .data -- WRAM -- because the TIMING loops must execute from RAM or
 * they measure cart fetch instead of what they claim to. Everything from here
 * down is readback and protocol, where a few cycles of fetch cost nothing, so
 * it is forced into .text. The suite had grown past the boot-stack fence and
 * then past the relaxed one; trimming WRAM is the right answer to that, not
 * moving the fence a second time.
 *
 * NOTHING HERE MAY BE TIMED AGAINST A WRAM BASELINE. The FMDL round-trip cost
 * below includes this function's own cart-resident fetch, so it is a figure
 * for THIS build and a cross-machine diff -- never something to compare with
 * the WRAM-resident timing rows above. */
/* ONE PUSH, PARAMETERISED, so the three variants differ only in the two things
 * under test and share everything else. `reset` issues the shipping code's
 * 68S-off pointer reset first; `dummy` sends one throwaway word ahead of the
 * payload while the SH-2 stays armed for DRQ_WORDS. */
__attribute__((noinline, section(".text")))
static void tc_dreq_push(volatile uint16_t *cmd, volatile uint16_t *ret,
                         volatile int8_t *ctl, volatile uint16_t *dlen,
                         volatile uint16_t *fifo, uint16_t reset, uint16_t dummy)
{
    uint16_t i, armed = 0, fin = 0;
    *ret = 0;
    *cmd = 0x4452u;
    for (i = 0; i < 120u && !armed; i++) { tc_frame_wait(); armed = (*cmd == 0x4441u); }
    if (!armed) return;
    if (reset) *ctl = 0;                       /* md_main.c:3855 */
    *dlen = (uint16_t)(DRQ_WORDS + (dummy ? 1u : 0u));
    *ctl = 4;
    if (dummy) { uint32_t sp = 20000ul; while (*ctl < 0 && --sp) ; fifo[0] = 0xDEADu; }
    for (i = 0; i < DRQ_WORDS; i++) {
        uint32_t sp = 20000ul;
        while (*ctl < 0 && --sp) ;
        if (!sp) break;
        fifo[0] = (uint16_t)(0x1000u + i);
    }
    *ctl = 0;
    for (i = 0; i < 120u && !fin; i++) { tc_frame_wait(); fin = (*cmd == 0x4444u); }
}

/* Reads back dst[0] from the transfer that just happened -- no new push. */
__attribute__((noinline, section(".text")))
static uint16_t tc_dreq_first(volatile uint16_t *cmd, volatile uint16_t *ret)
{
    uint16_t k, got = 0;
    *ret = 0;
    *cmd = 0x4432u;
    for (k = 0; k < 60u && got != 0x4446u; k++) { tc_frame_wait(); got = *cmd; }
    return (got == 0x4446u) ? *ret : 0xFFFFu;
}

__attribute__((noinline, section(".text")))
static void tc_run_late(void)
{
    /* ================ HAND THE SH-2 THE FLOOR ================
     * Everything above is the 68K's view. The cache, the FRT, CCR and every
     * SDRAM and cart access cost live on the far side of the adapter, and they
     * are what the SPEED axis is actually made of -- so sh_src/sh_probe.h runs
     * over there and publishes to SDRAM 0x0603B000, which both instruments
     * dump with the same switch they already use for this file's results.
     *
     * THE HANDSHAKE IS TWO WRITES AND A WAIT. COMM4 is free after boot (its
     * only other use is md_start.s:159's S_OK). The 68K posts SHP_GO and then
     * DELAYS IN WORK RAM rather than polling: an adapter-register poll costs
     * ~85 cycles of bus each time round (REBUILD.md) and would land in the
     * middle of every SH-2 measurement. A quiet 68K is the instrument here,
     * so the wait is a WRAM spin first and a single comm read after.
     *
     * T26 grades only that the probe RAN -- the magic and the tail marker,
     * which together prove it started and finished without truncation. The
     * numbers are not graded here because they are not the 68K's to judge;
     * tools/tc_compare.py diffs them across machines. */
    {
        uint16_t i2, got;
        *(volatile uint16_t*)0xA15124 = 0x5350u;    /* COMM4 = SHP_GO */
        /* WAIT LONG ENOUGH FOR THE SLOWEST MACHINE. Six frames was enough on
         * ares and NOT on BlastEm, which charges ~1.8x for the same SH-2 loop
         * -- the probe had finished and published, and only the 68K's verdict
         * was wrong. Poll up to ~40 frames with a WRAM delay between reads, so
         * the whole wait costs at most 40 adapter accesses. */
        got = 0;
        for (i2 = 0; i2 < 120u && got != 0x5044u; i2++) {
            tc_frame_wait();
            got = *(volatile uint16_t*)0xA15124;
        }
        tc_val(got == 0x5044u, got);                /* SHP_DONE */
    }

    /* ================ THE 32X SYSTEM REGISTERS (2026-09-27) ================
     * Added after reading the official manual in ~/src/32XDK, which settled
     * where these bits actually live -- and raised a question about this port
     * that a grep can only half answer.
     *
     * ADAPTER CONTROL, 0xA15100 (32x-hardware-manual.md, "Adapter Control
     * Register"):  bit15 FM (0 MD / 1 SH2 owns the VDP), bit7 REN (SH2 reset
     * enable), bit1 RES (0 reset / 1 running), bit0 ADEN (32X enabled).
     *
     * DREQ CONTROL, 0xA15106:  bit7 FULL (FIFO full, read-only), bit2 68S,
     * bit0 RV. And the manual is explicit about RV:
     *
     *     "RV: ROM to VRAM DMA -- 0: NO OPERATION (initial value),
     *      1: DMA Start Allowed. The SH2 side cannot access the ROM when
     *      RV = 1."
     *
     * plus 32X Technical Bulletin #9 (1994-09-23), entire body: "All 68000
     * interrupts must be prohibited during the period that the RV bit = 1."
     *
     * WHY THE CART READS IT. This tree talks about running under an "RV=1
     * identity map" in a dozen places -- md_main.c:100, :1464, :1501, :7165,
     * :7517, md_start.s:220 ("main sets RV=1 itself"), and it is on the
     * five-item real-hardware list. But EVERY WRITE to 0xA15107 in the whole
     * tree is 0 or 4: md_start.s:124 clears it, and md_main.c's live path
     * writes only 4 (68S on) and 0 (68S off), both of which put RV = 0.
     * Nothing sets bit 0, ever.
     *
     * A grep proves what the source says; this proves what the machine holds.
     * MEASURED 2026-09-27, ares and BlastEm alike:
     *   T24 adapter control = 0x0083 -- REN | RES | ADEN, FM = 0 (MD owns the
     *       VDP at this point), which is a sane enabled-and-running adapter;
     *   T25 DREQ control    = 0x0000 -- RV = 0.
     *
     * So bulletin #9 is satisfied, VACUOUSLY: no 68000 interrupt can be taken
     * during an RV=1 period because there is no RV=1 period. The open item is
     * the other direction -- a dozen comments in this tree describe a live
     * "RV=1 identity map" that the code does not create. Either those comments
     * are stale or a mechanism was planned and never landed; that is a call
     * for whoever owns the boot map, not for this file.
     *
     * CAVEAT, stated because it bounds the claim: the suite runs INSTEAD of
     * the game, so this is the BOOT state and not the game's steady state.
     * What closes the gap is the grep -- nothing in the tree writes bit 0 at
     * any point -- not this measurement alone.
     *
     * Report-only: what matters is bit 0 of T25 and the reader can see it. */
    {
        tc_meas(*(volatile uint16_t*)0xA15100);     /* T2B adapter control */
        tc_meas(*(volatile uint16_t*)0xA15106);     /* T2C DREQ control, RV b0 */
    }

    /* ================ THE COLOUR PAGE (2026-09-27) ================
     * Mike's original phase-2 ask: "a high color pallete and tiles set to test
     * all available pens, registers". Every expectation on this page is
     * DERIVED -- four from the MiSTer core's RTL with file:line, three because
     * a readback is a tautology -- so it is the first page with no row resting
     * on "ares said so".
     *
     * THE FORMATS, so the numbers below are checkable rather than magic:
     *
     *   MD CRAM is NINE BITS, 0000 BBB0 GGG0 RRR0. That is not a guess: it is
     *   the packing tools/bake_tilecram.py:md_word() emits and the SH-2 reads
     *   back, and the pink-trees bug (LOOP29 196) was exactly this field
     *   layout being wrong. So 512 distinct values exist -- 8 levels a gun --
     *   and T25 writes every one of them.
     *
     *   32X CRAM is 256 entries of SIXTEEN bits, R in 4:0, G in 9:5, B in
     *   14:10, and BIT 15 IS THE PRIORITY BIT:
     *     rtl/32X/VDP.sv:501-503  R/G/B assigned from PIX_COLOR[4:0]/[9:5]/[14:10]
     *     rtl/32X/VDP.sv:506      YSO_N = ~(PRI ^ PIX_COLOR[15]) & YS_N_SYNC
     *   so composition is per-pixel: bit 15 of the entry, XORed with the PRI
     *   bit of the bitmap mode register, decides whether the MD layer shows
     *   through. That is the whole 32X-over-MD rule in one line of Verilog,
     *   and it is why T28 must see bit 15 survive a write.
     *
     *   THE PALETTE RAM IS UNMASKED. rtl/32X/32X_mem.sv:82-95: VDPPAL is
     *   DATA[15:0] x ADDR[7:0] with no mask on either port. So all sixteen
     *   bits store and read back, priority bit included -- T28 is graded, not
     *   reported.
     *
     *   THE BITMAP MODE REGISTER MASKS TO 0x80C3. rtl/32X/32X_PKG.sv:139
     *   BMMR_MASK = 16'h80C3; VDP.sv:133-134 writes DI & BMMR_MASK; VDP.sv:160
     *   reads back {~PAL, 7'h00, BMMR.PRI, BMMR.M240, 4'h0, BMMR.M}. So bit 15
     *   reads ~PAL (1 on NTSC) WHATEVER was written, bit 7 is PRI, bit 6 is
     *   M240, bits 1:0 are the mode, and everything else reads 0. Writing
     *   0xFFFF must therefore read back 0x80C3 on an NTSC machine. T29 is that
     *   test, and it exercises every writable bit of the register at once.
     *
     * SAFE TO TRASH CRAM AND VRAM HERE: tc_screen() re-uploads the font and
     * sets CRAM 0 and 1 itself before painting, so the readout does not
     * inherit anything this page leaves behind. The tile test stays at VRAM
     * 0xB200, clear of the font at 0 and the name table at 0xC000.
     */
    {
        uint16_t bad, i2, got, first = 0, gotv = 0;

        /* T24 -- ALL 64 MD CRAM ENTRIES. Writes every entry FIRST and reads
         * them all back AFTER, deliberately: a write-read pair per entry
         * cannot see an entry clobbered by a later write, which is the actual
         * failure mode (a wrong autoincrement, or an address that wraps). */
        bad = 0;
        for (i2 = 0; i2 < 64u; i2++) {
            uint16_t v = (uint16_t)((((i2 * 7u) & 7u) << 9)
                                  | (((i2 * 5u) & 7u) << 5)
                                  | (((i2 * 3u) & 7u) << 1));
            VWIDE = ((uint32_t)(0xC000u | (i2 * 2u)) << 16) | 0u;
            VDATA = v;
        }
        for (i2 = 0; i2 < 64u; i2++) {
            uint16_t v = (uint16_t)((((i2 * 7u) & 7u) << 9)
                                  | (((i2 * 5u) & 7u) << 5)
                                  | (((i2 * 3u) & 7u) << 1));
            uint16_t r;
            VWIDE = ((uint32_t)(0x0000u | (i2 * 2u)) << 16) | 0x20u;
            r = (uint16_t)(VDATA & 0x0EEEu);
            if (r != v) { if (!bad) { first = i2; gotv = r; } bad++; }
        }
        tc_val(bad == 0, TC_SWEEP(bad, first, gotv));

        /* T25 -- ALL 512 MD PENS, every 8x8x8 combination through entry 1.
         * This is "test all available pens" taken literally for the MD side:
         * 9 bits, 3 per gun, 512 distinct colours and no more. */
        bad = 0;
        for (i2 = 0; i2 < 512u; i2++) {
            uint16_t v = (uint16_t)((((i2 >> 6) & 7u) << 9)
                                  | (((i2 >> 3) & 7u) << 5)
                                  | ((i2 & 7u) << 1));
            uint16_t r;
            VWIDE = ((uint32_t)(0xC000u | 2u) << 16) | 0u;
            VDATA = v;
            VWIDE = ((uint32_t)(0x0000u | 2u) << 16) | 0x20u;
            r = (uint16_t)(VDATA & 0x0EEEu);
            if (r != v) { if (!bad) { first = i2; gotv = r; } bad++; }
        }
        tc_val(bad == 0, TC_SWEEP(bad, first, gotv));

        /* T26 -- WHAT THE MD VDP RETURNS IN THE BITS CRAM DOES NOT HOLD.
         * ANSWERED 2026-09-27: ares reads back 0x0EEE for a write of 0xFFFF,
         * so the VDP DOES mask CRAM to its nine significant bits on readback.
         * That retroactively justifies T5's `& 0x0EEE`, which had been masking
         * for no recorded reason since the file was written.
         * REPORT-ONLY, and it is here because T5 above quietly masks its
         * readback with 0x0EEE: something is being hidden by that mask and no
         * test has ever said what. The nine significant bits are 1-3, 5-7 and
         * 9-11, so a write of 0xFFFF SHOULD read back 0x0EEE if the VDP masks
         * on readback -- but whether it masks, returns the full word, or
         * returns bus junk in the gaps is a characterisation question, and
         * grading it from the format would be the conformance mistake
         * TESTCART.md warns about. Report the raw word, unmasked. */
        VWIDE = ((uint32_t)(0xC000u | 4u) << 16) | 0u;
        VDATA = 0xFFFF;
        VWIDE = ((uint32_t)(0x0000u | 4u) << 16) | 0x20u;
        tc_meas(VDATA);

        /* T27 -- ALL 256 32X PALETTE ENTRIES, written and read back inside one
         * vblank because 32X CRAM access is gated on PEN (VDP.sv:170, 403-405
         * and T1C/T1D above). 512 accesses is about twelve lines and vblank is
         * around thirty, so it fits with room. Write-all-then-read-all, same
         * reason as T24. */
        {
            volatile uint16_t *c32 = (volatile uint16_t*)0xA15200;
            bad = 0;
            tc_wait_line(0xE2);
            for (i2 = 0; i2 < 256u; i2++)
                c32[i2] = (uint16_t)(0x0101u * i2);
            for (i2 = 0; i2 < 256u; i2++)
                if (c32[i2] != (uint16_t)(0x0101u * i2)) {
                    if (!bad) { first = i2; gotv = c32[i2]; }
                    bad++;
                }
            tc_val(bad == 0, TC_SWEEP(bad, first, gotv));

            /* T28 -- BIT 15, THE PRIORITY BIT, MUST SURVIVE. VDPPAL is an
             * unmasked 16-bit RAM (32X_mem.sv:82-95), so the whole word stores
             * -- and bit 15 is not decoration: VDP.sv:506 XORs it with the
             * register's PRI bit to decide, per pixel, whether the MD layer
             * shows through. A core that dropped it would still pass every
             * colour test on this page and silently break composition. */
            tc_wait_line(0xE2);
            c32[255] = 0xFFFF;
            got = c32[255];
            tc_val(got == 0xFFFFu, got);
        }

        /* T29 -- EVERY WRITABLE BIT OF THE BITMAP MODE REGISTER AT ONCE, and
         * THE ONE PREDICTION ON THIS PAGE THAT DID NOT HOLD.
         *
         * The RTL is unambiguous: BMMR_MASK = 16'h80C3 (32X_PKG.sv:139),
         * writes are ANDed with it (VDP.sv:133-134), and the readback
         * substitutes ~PAL into bit 15 whatever was written (VDP.sv:160). So
         * 0xFFFF in must give 0x80C3 back on NTSC -- bit15 ~PAL, bit7 PRI,
         * bit6 M240, bits1:0 mode 3, everything else zero.
         *
         * ares returns 0xB3FF, which is not a plausible register mask.
         *
         * SETTLED THE SAME DAY BY A THIRD INSTRUMENT. The BlastEm fork returns
         * 0x80C3 -- the RTL prediction exactly. Two independent
         * implementations agree and ares is the outlier, so the prediction
         * stands and REF is 0x80C3: the REF column is the SPECIFICATION here
         * and ares announces its own deviation as a delta, which is what that
         * column is for. ares does not appear to mask this register on
         * readback.
         *
         * It stays REPORT-ONLY rather than graded even so. Grading it would
         * paint a red FAIL on every ares run for an emulator bug, and the
         * delta already says everything the FAIL would. Real silicon still
         * gets the last word; two emulators and a core are not a machine.
         *
         * Restored to 0 immediately either way: M240 changes the line count
         * and the mode changes how pixels are fetched, and nothing after this
         * may inherit either. */
        {
            volatile uint16_t *bmmr = (volatile uint16_t*)0xA15180;
            tc_wait_line(0xE2);
            *bmmr = 0xFFFF;
            got = *bmmr;
            *bmmr = 0x0000;                 /* layer off again, mode 0 */
            tc_meas(got);                   /* RTL predicts 0x80C3; ares 0xB3FF */
        }

        /* T2A -- A 4BPP TILE CARRYING ALL SIXTEEN PENS, written to VRAM and
         * read back. The other half of Mike's ask: a pen that CRAM holds is
         * useless if the tile data cannot address it. Eight longs, pens 0-F
         * twice over, so every nibble position is exercised. */
        {
            static const uint16_t ROWS[8] = {
                0x0123u, 0x4567u, 0x89ABu, 0xCDEFu,
                0xFEDCu, 0xBA98u, 0x7654u, 0x3210u
            };
            bad = 0;
            for (i2 = 0; i2 < 8u; i2++) {
                uint32_t a = 0xB200u + (uint32_t)i2 * 2u;
                VWIDE = ((uint32_t)(0x4000u | (a & 0x3FFFu)) << 16) | ((a >> 14) & 3u);
                VDATA = ROWS[i2];
            }
            for (i2 = 0; i2 < 8u; i2++) {
                uint32_t a = 0xB200u + (uint32_t)i2 * 2u;
                uint16_t r;
                VWIDE = ((uint32_t)(0x0000u | (a & 0x3FFFu)) << 16) | ((a >> 14) & 3u);
                r = VDATA;
                if (r != ROWS[i2]) { if (!bad) { first = i2; gotv = r; } bad++; }
            }
            tc_val(bad == 0, TC_SWEEP(bad, first, gotv));
        }
    }
    /* ================ VSRAM, AND THE VDP MODES NOBODY CHECKED ==========
     * Three gaps found by asking what the suite does NOT touch.
     *
     * VSRAM is the VDP's third memory space -- 40 entries in H40, one vertical
     * scroll value per two-column strip -- and nothing in this suite had ever
     * addressed it. The control encoding is the same shape as the CRAM one
     * already proven above: CD=5 for a write is first word 0x4000 | addr,
     * second word 0x10; CD=4 for a read is first word 0x0000 | addr, second
     * 0x10. (CRAM write is CD=3, first word 0xC000, second 0; CRAM read is
     * CD=8, first 0x0000, second 0x20. Same table, different code.)
     *
     * DMA FILL AND COPY WERE TIMED BUT NEVER VERIFIED -- T18 and T19 measure
     * how long they take and read nothing back. A DMA that takes exactly the
     * right number of lines and moves nothing would pass both.
     *
     * AND THE STATUS REGISTER at 0xC00004 has a VBLANK bit that must agree
     * with the raster counter this suite uses as its clock. Two independent
     * indicators of one fact: if they ever disagree, one of them is lying and
     * the timing page is built on the other. */
    {
        uint16_t i5, bad3 = 0, f3 = 0, g3 = 0, st;

        /* VSRAM: all 40 entries, write-all-then-read-all so a later write
         * clobbering an earlier one is visible. Masked to 10 bits -- the
         * width the next test measures rather than assumes. */
        VCTRL = 0x8F02;
        VWIDE = ((uint32_t)0x4000u << 16) | 0x10u;
        for (i5 = 0; i5 < 40u; i5++) VDATA = (uint16_t)(0x0155u ^ (i5 * 9u));
        for (i5 = 0; i5 < 40u; i5++) {
            uint16_t want = (uint16_t)((0x0155u ^ (i5 * 9u)) & 0x03FFu), r;
            VWIDE = ((uint32_t)(i5 * 2u) << 16) | 0x10u;
            r = (uint16_t)(VDATA & 0x03FFu);
            if (r != want) { if (!bad3) { f3 = i5; g3 = r; } bad3++; }
        }
        tc_val(bad3 == 0, TC_SWEEP(bad3, f3, g3));

        /* HOW WIDE IS A VSRAM ENTRY? Write all ones and report. Nothing in
         * this tree states it, so it reports rather than grades. */
        VWIDE = ((uint32_t)0x4000u << 16) | 0x10u;
        VDATA = 0xFFFF;
        VWIDE = ((uint32_t)0x0000u << 16) | 0x10u;
        tc_meas(VDATA);

        /* DMA FILL, AND THIS TIME READ IT BACK.
         *
         * AUTOINCREMENT 1, NOT 2, AND THAT IS THE WHOLE TEST. A VDP VRAM fill
         * writes BYTES, stepping the address by the autoincrement each time --
         * so with the suite's usual autoinc of 2 it writes every OTHER byte
         * and leaves the ones between untouched. The first run of this check
         * failed 127 of 128 words with the low byte right and the high byte
         * stale, which is exactly that: 0x7F00013C, 127 bad, first at word 1.
         *
         * T18 above times a fill at autoinc 2 and its number is unaffected --
         * the VDP still performs 256 write operations either way, so the slot
         * arithmetic that predicts 14.22 lines is about the OPERATIONS, not
         * where they land. Timing and data are separate claims and only the
         * data one needed fixing. */
        VCTRL = 0x8F01;
        VCTRL = (uint16_t)(0x9300u | 0u); VCTRL = (uint16_t)(0x9400u | 1u);
        VCTRL = 0x9780;                                  /* mode: VRAM fill */
        VWIDE = ((uint32_t)(0x4000u | (0xB800u & 0x3FFFu)) << 16)
              | (((0xB800u >> 14) & 3u) | 0x80u);
        VDATA = 0x3C3C;
        tc_dma_wait();
        VCTRL = 0x8F02;
        bad3 = 0; f3 = 0; g3 = 0;
        for (i5 = 0; i5 < 128u; i5++) {
            uint32_t a = 0xB800u + (uint32_t)i5 * 2u;
            uint16_t r;
            VWIDE = ((uint32_t)(0x0000u | (a & 0x3FFFu)) << 16) | ((a >> 14) & 3u);
            r = VDATA;
            if (r != 0x3C3Cu) { if (!bad3) { f3 = i5; g3 = r; } bad3++; }
        }
        tc_val(bad3 == 0, TC_SWEEP(bad3, f3, g3));

        /* DMA VRAM-TO-VRAM COPY, read back. 0xB800 was just filled with
         * 0x3C3C, so a copy of it must arrive as 0x3C3C at 0xBA00 -- and the
         * destination is cleared first so a copy that does NOTHING fails
         * instead of inheriting the right answer. */
        VCTRL = 0x8F02;
        for (i5 = 0; i5 < 128u; i5++) {
            uint32_t a = 0xBA00u + (uint32_t)i5 * 2u;
            VWIDE = ((uint32_t)(0x4000u | (a & 0x3FFFu)) << 16) | ((a >> 14) & 3u);
            VDATA = 0x0000;
        }
        VCTRL = (uint16_t)(0x9300u | 0u); VCTRL = (uint16_t)(0x9400u | 1u);
        VCTRL = (uint16_t)(0x9500u | 0x00u); VCTRL = (uint16_t)(0x9600u | 0xB8u);
        VCTRL = 0x97C0;                                  /* mode: VRAM copy */
        VCTRL = 0x8F01;                                  /* byte steps, as fill */
        VWIDE = ((uint32_t)(0x0000u | (0xBA00u & 0x3FFFu)) << 16)
              | (((0xBA00u >> 14) & 3u) | 0xC0u);
        tc_dma_wait();
        VCTRL = 0x9700;                                  /* mode back */
        VCTRL = 0x8F02;                                  /* autoinc back to 2 */
        bad3 = 0; f3 = 0; g3 = 0;
        for (i5 = 0; i5 < 128u; i5++) {
            uint32_t a = 0xBA00u + (uint32_t)i5 * 2u;
            uint16_t r;
            VWIDE = ((uint32_t)(0x0000u | (a & 0x3FFFu)) << 16) | ((a >> 14) & 3u);
            r = VDATA;
            if (r != 0x3C3Cu) { if (!bad3) { f3 = i5; g3 = r; } bad3++; }
        }
        tc_val(bad3 == 0, TC_SWEEP(bad3, f3, g3));

        /* THE STATUS REGISTER AGAINST THE RASTER COUNTER. Sampled twice, once
         * each side of the vblank edge, and BOTH must agree with V >= 0xE0 --
         * one sample could pass by luck. The FIFO-empty bit is checked in the
         * same read because an idle VDP must report an empty FIFO. */
        {
            uint16_t ok = 1;
            tc_wait_line(0xE8);
            st = *(volatile uint16_t*)VDP_CTRL_PORT;
            if (!(st & 0x0008u)) ok = 0;                 /* vblank says vblank */
            if (!(st & 0x0200u)) ok = 0;                 /* FIFO empty when idle */
            tc_wait_line(0x60);
            st = *(volatile uint16_t*)VDP_CTRL_PORT;
            if (st & 0x0008u) ok = 0;                    /* active says active */
            tc_val(ok, st);
        }
    }

    /* ================ THE DREQ FIFO, THE SHIPPING TRANSPORT ==========
     * The FMDL proof shows the 68K staging a list in the framebuffer and the
     * SH-2 executing it. That is not how this port actually moves per-frame
     * data: the DREQ FIFO is. The 68K pushes words into 0xA15112 with 68S set
     * and the SH-2's DMAC pulls them out of 0x20004012 into SDRAM -- every
     * packet the renderer eats arrives that way, and nothing tested it end to
     * end.
     *
     * It is also the path with the most measured pain in this project:
     * partial DREQ landings, the lost-push belt, "no packet lands in MAME at
     * all under R60". A transport with that history and no test is the wrong
     * shape.
     *
     * THE HANDSHAKE ENFORCES THE ORDER. The SH-2 must have armed its DMAC
     * before the first word goes in, so it posts DREQ_ARMED and this waits for
     * it rather than guessing a delay -- which is the difference between a
     * test and a race.
     *
     * 64 words of 0x1000+i, checked by SUM. One number crosses the comm port
     * and still catches a wrong value, a wrong order and a short transfer:
     * 64 x 0x1000 + 2016 is 0x07E0 in sixteen bits. */
    {
        volatile uint16_t *fifo = (volatile uint16_t*)0xA15112uL;
        volatile int8_t   *ctl  = (volatile int8_t  *)0xA15107uL;
        volatile uint16_t *dlen = (volatile uint16_t*)0xA15110uL;
        volatile uint16_t *cmd  = (volatile uint16_t*)0xA15124uL;   /* COMM4 */
        volatile uint16_t *ret  = (volatile uint16_t*)0xA1512EuL;   /* COMM14 */
        uint16_t i6, armed = 0, fin = 0, sum, pushed = 0, first;

        *ret = 0;
        *cmd = 0x4452u;                              /* 'DR' */
        for (i6 = 0; i6 < 120u && !armed; i6++) {
            tc_frame_wait();
            armed = (uint16_t)(*cmd == 0x4441u);     /* 'DA' */
        }

        if (armed) {
            *dlen = DRQ_WORDS;
            *ctl = 4;                                /* 68S on */
            for (i6 = 0; i6 < DRQ_WORDS; i6++) {
                uint32_t spin = 20000ul;
                while (*ctl < 0 && --spin) ;         /* bit 7 = FIFO FULL */
                if (!spin) break;                    /* never wedge on a full
                                                      * FIFO nobody is draining */
                fifo[0] = (uint16_t)(0x1000u + i6);
                pushed++;
            }
            *ctl = 0;                                /* 68S off */
        }

        for (i6 = 0; i6 < 120u && !fin; i6++) {
            tc_frame_wait();
            fin = (uint16_t)(*cmd == 0x4444u);       /* 'DD' */
        }
        sum = *ret;

        tc_val(armed && pushed == DRQ_WORDS, pushed);   /* the 68K side */
        /* REPORT-ONLY, and the reason is the whole finding below: WITHOUT the
         * FIFO-pointer reset this push is CORRECT on three emulators and loses
         * its first word on the FPGA. A graded row would paint a permanent
         * FAIL on hardware for a variant that exists to document the quirk.
         * The reference is the right answer; the deviation is the finding. */
        tc_meas(sum);

        /* AND WHICH WORD LANDED FIRST. The sum says how much content is
         * missing; it never says what happened. Expected 0x1000. A 0x1001
         * means the transfer SHIFTED -- first word dropped, rest slid up --
         * and a 0x0000 means the first slot was never written. Same buffer,
         * no new transfer, so it describes the run that just failed. */
        first = tc_dreq_first(cmd, ret);
        tc_meas(first);

        /* ================ THE LOST WORD, FOUND ================
         * The FPGA loses it; three emulators do not. The shipping code has
         * TWO workarounds on this path that the control above does not use,
         * and each is a hypothesis:
         *
         *   md_main.c:3855  *ctrl = 0 before *ctrl = 4 -- "68S off: resets the
         *                   FIFO pointers (erec12: an aborted push left 8
         *                   words in the FIFO and every later landing was
         *                   offset by them)"
         *   md_main.c:3851  eight TRAILING ZEROS on every payload -- "the FIFO
         *                   path delivers 8 words LATE (erec13: every landing
         *                   began with the previous push's last 8 words)"
         *
         * So: B repeats the push with the pointer reset, and C pushes one
         * DUMMY word ahead of the payload while the SH-2 stays armed for 64.
         * If the first write after 68S is eaten, C's dummy absorbs it and the
         * payload lands whole. Each reports the first word that arrived --
         * 0x1000 means the transport is now correct.
         *
         * THE CONTROL IS ABOVE AND IT RAN FIRST, so all three share one boot,
         * one SH-2, one FIFO. Three variants in one launch also means the rig
         * answers this in one screenshot instead of three.
         *
         * ANSWERED ON THE RIG, 2026-09-27, in one launch:
         *     A  no reset      first word 0000   LOST
         *     B  with reset    first word 1000   CORRECT
         *     C  reset+dummy   first word DEAD   the dummy landed
         *
         * So the cause is the MISSING POINTER RESET and nothing else, and the
         * dummy hypothesis is refuted by C: a word pushed first arrives first,
         * therefore the write after 68S is not being eaten. B is the graded
         * row because it is correct usage and must hold everywhere; A and C
         * report, because A is correct on three emulators and wrong on the
         * FPGA and that difference is the thing worth seeing. */
        tc_dreq_push(cmd, ret, ctl, dlen, fifo, 1u, 0u);
        first = tc_dreq_first(cmd, ret);
        tc_val(first == 0x1000u, first);         /* B: with the FIFO reset */

        tc_dreq_push(cmd, ret, ctl, dlen, fifo, 1u, 1u);
        first = tc_dreq_first(cmd, ret);
        tc_meas(first);                          /* C: reset + a dummy word */
    }

    /* ================ THE CLOCK THIS SUITE RUNS ON ================
     * Every timing row on the 68K page is measured by counting transitions of
     * the MD raster counter at 0xC00008. That instrument has never itself been
     * checked, and it is the one number an error in would move EVERYTHING.
     *
     * It also bears on the open question. T24 measures 57,344 known cycles as
     * 120 lines, so a line is 477.9 68K cycles -- and the nominal arithmetic
     * says 488.57 because it assumes 262 lines at 59.92 Hz. IF THE FRAME IS
     * NOT 262 LINES, that is the whole discrepancy and there is nothing else
     * to explain. So: count the lines in a frame and find out.
     *
     * Counting V TRANSITIONS rather than reading V, because the MD's V counter
     * is a byte that JUMPS -- NTSC runs 0x00-0xEA then repeats 0xE5-0xFF --
     * so distinct values are not lines and the maximum is not the count. A
     * transition is a line however the counter is numbered.
     *
     * NTSC is 262. If this reads 262 then 488.57 was right about the frame and
     * the missing 2.2% is a steal (VDP refresh is the right size); if it reads
     * ~268 then there was never a steal and the constant was simply wrong. */
    {
        uint16_t last, lines = 0;
        uint32_t g;

        /* start at a vblank edge so the count covers exactly one frame */
        g = 200000ul; while (TC_V() >= 0xE0u && --g) ;
        g = 200000ul; while (TC_V() <  0xE0u && --g) ;
        last = TC_V();
        g = 400000ul;
        while (--g) {                       /* count until the next vblank in */
            uint16_t v = TC_V();
            if (v != last) { lines++; last = v; }
            if (lines > 8u && v >= 0xE0u && lines > 200u) break;
        }
        tc_meas(lines);                     /* lines in one frame */

        /* AND THE COUNTER MUST NOT STALL. A raster counter that stops is a
         * clock that reads every workload as free -- the failure mode that
         * would make the whole timing page quietly meaningless. */
        {
            uint16_t v0 = TC_V(), moved = 0;
            g = 100000ul;
            while (--g) if (TC_V() != v0) { moved = 1; break; }
            tc_val(moved, moved);
        }
    }

    /* ================ THE Z80 EXECUTES INSTRUCTIONS ================
     * The Z80 page proves the BUS: reset, grant, the 0xA00000 window, byte and
     * word access. None of it proves the Z80 ever RUNS anything -- every one of
     * those tests would pass against a socket with no CPU in it.
     *
     * So upload seven bytes and let it work:
     *
     *     F3           DI              no interrupts, nothing else running
     *     3E 20        LD A,0x20
     *     C6 3A        ADD A,0x3A      -- 0x20 + 0x3A = 0x5A, so it must
     *                                     FETCH, DECODE and compute
     *     32 00 1F     LD (0x1F00),A
     *     76           HALT            park it where it cannot wander
     *
     * ADD rather than a second LD on purpose: a machine that merely copied the
     * byte through would produce 0x20, and the answer names which happened.
     *
     * ORDER IS LAW here too (md_start.s:175, 32x-builder md_main.c:545):
     * release reset, take the bus, write, pulse reset so PC is 0, hand the bus
     * back, release reset. Then let it run, take the bus again and read. */
    {
        volatile uint16_t *zbus = (volatile uint16_t*)0xA11100;
        volatile uint16_t *zrst = (volatile uint16_t*)0xA11200;
        volatile uint8_t  *zram = (volatile uint8_t *)0xA00000;
        static const uint8_t PROG[8] = {
            0xF3, 0x3E, 0x20, 0xC6, 0x3A, 0x32, 0x00, 0x1F };
        uint16_t i4;
        uint32_t g;

        *zrst = 0x100;
        for (i4 = 0; i4 < 64u; i4++) ;
        *zbus = 0x100;
        g = 0xFFFFul; while ((*zbus & 0x100u) && --g) ;

        for (i4 = 0; i4 < 8u; i4++) zram[i4] = PROG[i4];
        zram[8] = 0x76;                      /* HALT */
        zram[0x1F00] = 0x00;                 /* clear the answer cell */

        *zrst = 0x000;                       /* reset pulse: PC <- 0 */
        for (i4 = 0; i4 < 64u; i4++) ;
        *zbus = 0x000;                       /* hand the bus back */
        *zrst = 0x100;                       /* and let it run */

        tc_frame_wait();                     /* a frame is ~130k Z80 cycles */

        *zbus = 0x100;
        g = 0xFFFFul; while ((*zbus & 0x100u) && --g) ;
        i4 = zram[0x1F00];
        tc_val(i4 == 0x5Au, i4);             /* 5A ran it; 20 copied it; 00 dead */

        /* park it exactly as md_start.s:180 left it, PSG muted by hand */
        *zrst = 0x000;
        *zbus = 0x000;
        {
            volatile uint8_t *psg = (volatile uint8_t*)0xC00011;
            *psg = 0x9F; *psg = 0xBF; *psg = 0xDF; *psg = 0xFF;
        }
    }

    /* ================ THE ARCHITECTURE, END TO END ================
     * The transport tests above time three legs and the 68K performs all
     * three. Nothing proved the SH-2 LIFTS AND EXECUTES what the 68K staged,
     * which is the spine of this port. This does, in four steps and one
     * verdict.
     *
     *   FM=0  the 68K owns the FB window and writes a display list at
     *         0x851A00 -- the real packet base, not a stand-in
     *   FM=1  the window is handed over and FMDL_GO posted; the SH-2 lifts
     *         the SAME MEMORY at 0x24011A00, executes it, posts the record
     *         count on COMM14 and FMDL_DONE on COMM4
     *   FM=0  the 68K takes the window back and reads what landed
     *
     * Both sides see ONE memory at two addresses (68K 0x840000 is SH-2
     * 0x24000000) and FM alone decides whose turn it is. That handoff is what
     * had no test, and what everything else depends on.
     *
     * THREE ROWS, so a failure says WHERE. A short count means the list was
     * truncated in transit; a matching count with wrong pixels means the
     * WRITES did not land. Different failures, distinguishable from the table
     * alone without a dump. */
    {
        volatile uint16_t *list = (volatile uint16_t*)0x851A00uL;
        volatile uint16_t *fb   = (volatile uint16_t*)0x840200uL;
        volatile uint16_t *adap = (volatile uint16_t*)0xA15100uL;
        volatile uint16_t *cmd  = (volatile uint16_t*)0xA15124uL;   /* COMM4 */
        volatile uint16_t *ret  = (volatile uint16_t*)0xA1512EuL;   /* COMM14 */
        uint16_t i3, v1, bad2 = 0, f2 = 0, g2 = 0, done = 0, ran;
        const uint16_t N = 32u;

        tc_wait_line(0x10); v1 = TC_V();

        /* FM=0: ours. Record k paints word (k*37) with a value keyed to k, so
         * a stuck address and a stuck value both show. */
        *adap = (uint16_t)(*adap & 0x7FFFu);
        for (i3 = 0; i3 < N; i3++) {
            list[i3 * 2u]      = (uint16_t)(i3 * 37u);
            list[i3 * 2u + 1u] = (uint16_t)(0xA000u + i3);
        }
        list[N * 2u] = 0xFFFFu;                      /* terminator */

        /* FM=1: theirs. Comm ports are adapter registers, so they stay
         * readable with the window handed over -- which is what makes a
         * handshake across the FM boundary possible at all. */
        *ret = 0;
        *adap = (uint16_t)(*adap | 0x8000u);
        *cmd = 0x464Du;                              /* FMDL_GO */
        for (i3 = 0; i3 < 120u && !done; i3++) {
            tc_frame_wait();
            done = (uint16_t)(*cmd == 0x4644u);      /* FMDL_DONE */
        }
        ran = *ret;

        /* FM=0: ours again. */
        *adap = (uint16_t)(*adap & 0x7FFFu);
        tc_val(done && ran == N, ran);               /* records executed */

        for (i3 = 0; i3 < N; i3++) {
            uint16_t want = (uint16_t)(0xA000u + i3);
            uint16_t r = fb[i3 * 37u];
            if (r != want) { if (!bad2) { f2 = i3; g2 = r; } bad2++; }
        }
        tc_val(bad2 == 0, TC_SWEEP(bad2, f2, g2));   /* every word landed */
        tc_meas(tc_lines_since(v1));                 /* round-trip cost */

        *cmd = 0x5155u;                              /* QUIT: release the master */
    }

}

/* ---- readout ----
 *
 * PHASE 1 USES THE PROVEN CHANNEL, not the grid TESTCART.md sketches. The MD
 * name-table grid painted BLACK in ares: the 32X layer sits on top of the MD
 * planes, which is exactly why main()'s boot tracer is visible at all -- it
 * writes 32X palette entry 0 (0xA15200) and the whole screen takes that
 * colour. A grid therefore needs the 32X FRAMEBUFFER and its line table, which
 * is real work and unverified; shipping it unverified would be the opposite of
 * what this suite is for.
 *
 * So phase 1 reports two ways, both exact:
 *   SCREEN   32X palette entry 0. GREEN and STAYS green = every test passed.
 *            Otherwise it walks the results: blue separator, then one colour
 *            per test in order, white pass / red fail, for ever. Needs no
 *            tooling and works on a TV, where no memory dump exists.
 *   MEMORY   a result block at 68K work RAM 0xFFE100, for per-test detail:
 *              +0  0x7C21 magic, proves the suite ran
 *              +2  tests run        +4  tests passed
 *              +6.. one byte per test: 1 pass, 2 fail, 0 not run
 *            Read with  ares-headless --dump wram:0xFFE100:0x20:out.bin
 *
 * Phase 1b: the framebuffer grid, once its line-table setup is verified
 * against both machines rather than assumed.
 */
#define TC_OUT ((volatile uint16_t*)0xFFE100)

/* MD backdrop = CRAM entry 0, 9-bit BGR. vdp_color() in md_main.c is static,
 * so this is the same two writes it makes. */
static void tc_backdrop(uint16_t color)
{
    VWIDE = ((uint32_t)(0xC000u) << 16) | 0u;
    VDATA = color;
}

static void tc_paint(void)
{
    uint16_t i, pass = 0;
    for (i = 0; i < tc_n; i++) if (tc_res[i] == 1) pass++;

    TC_OUT[0] = 0x7C21;                  /* magic: the suite ran */
    TC_OUT[1] = tc_n;
    TC_OUT[2] = pass;
    for (i = 0; i < TC_MAX && i < 72u; i++)
        ((volatile uint8_t*)0xFFE106)[i] = tc_res[i];

    /* TWO LAYERS, BOTH SET, and this is not cosmetic. The 32X layer covers the
     * active area; the BORDER is the MD backdrop, and main()'s boot tracer
     * leaves its last stage colour there -- CYAN for "slave alive". Mike's
     * first run showed a green centre inside a CYAN frame, and a stale boot
     * colour sitting beside a verdict is a readout that can be misread,
     * especially on hardware where a red boot stage would frame a green pass.
     * So the suite drives both and they always agree.
     *   32X CRAM at 0xA15200 is 15-bit BGR; the MD backdrop is 9-bit BGR. */
    *(volatile uint16_t*)0xA15200 =
        (uint16_t)(pass == tc_n ? 0x03E0 : 0x001F);       /* green : red */
    tc_backdrop((uint16_t)(pass == tc_n ? 0x00E0 : 0x000E));
}

/* ON REAL HARDWARE THERE IS NO MEMORY DUMP, so a red screen would say that
 * something failed and not which. When everything passed the screen simply
 * STAYS GREEN -- best for a screenshot, and nothing more is needed. When
 * anything failed it then walks the results: BLUE separator, then one colour
 * per test in order, white for pass and red for fail, repeating for ever. So
 * the detail appears exactly when it is wanted and never muddies a clean
 * pass. */
static void tc_hold(uint16_t units)
{
    volatile uint16_t i, j;
    for (i = 0; i < units; i++)
        for (j = 0; j < 20000u; j++) ;
}


/* ---- text on the MD plane (2026-09-27) --------------------------------
 *
 * THE FONT IS ALREADY IN VRAM. md_start.s:99-109 uploads md_src/font.s -- 45
 * 8x8 4bpp tiles -- to VRAM address 0 at boot with foreground forced to pen 1.
 * So a character costs one name-table write. The glyph ORDER is a compacted
 * ASCII, decoded from the bitmaps rather than guessed:
 *   0 space | 1 '!' | 2-11 '0'-'9' | 12-37 'A'-'Z' | 38-44 : . - > | + %
 * There is NO LOWERCASE and no '/' or '=', which is why every label here is
 * upper case and uses ':' and '-'.
 *
 * AND THE 32X LAYER MUST BE BLANKED or none of it is visible: the 32X layer
 * sits on top of the MD planes, which is what made the first grid paint black.
 * Bitmap mode 0 at 0xA15180 is no display, and the MD plane shows through.
 * Text appearing at all is the proof that write does what the register map
 * says -- it was derived, not measured, until this ran.
 *
 * The window plane is used rather than plane A because its base is known
 * (reg 3 = 0x30 -> 0xC000, the same one the barcode uses) and reg 0x12 = 0x80
 * makes the window cover from row 0 down, i.e. the whole screen. Plane A's
 * base at this point in boot is not ours to assume.
 */
#ifdef R60_FRONTEND
#include "frontend.h"
#include "../sh_src/game_cfg.h"   /* GAME_TITLE, GAME_ROUNDS, GAME_REGIONS_*, GAME_DIP_* (games/<title>/game.py) */
/* The game uses C000-CFFF and E000-EFFF plane tables, art below C000,
 * sprite attributes at F000 and hscroll at FC00. These two gaps are UI-owned. */
#define TC_NAMES 0xD000u
#define TC_FONT 0xF400u
#define TC_TILE (0x8000u | (TC_FONT / 32u))
#else
#define TC_NAMES 0xC000u
#define TC_FONT 0u
#define TC_TILE 0u
#endif
static uint16_t tc_glyph(char c)
{
    if (c == ' ') return 0;
    if (c == '!') return 1;
    if (c >= '0' && c <= '9') return (uint16_t)(2 + (c - '0'));
    if (c >= 'A' && c <= 'Z') return (uint16_t)(12 + (c - 'A'));
    switch (c) {
    case ':': return 38; case '.': return 39; case '-': return 40;
    case '>': return 41; case '|': return 42; case '+': return 43;
    case '%': return 44;
    }
    return 0;
}

static void tc_puts(uint16_t row, uint16_t col, const char *str)
{
    uint32_t a = TC_NAMES + ((uint32_t)row * 64u + col) * 2u;
    for (; *str; str++, a += 2u) {
        VWIDE = ((uint32_t)(0x4000u | (a & 0x3FFFu)) << 16) | ((a >> 14) & 3u);
        VDATA = TC_TILE + tc_glyph(*str);
    }
}

/* COUNTS GO OUT IN DECIMAL. Everything else on this screen is hex -- values,
 * references, deltas -- so tc_hex was used for the tallies too, and the first
 * render of the key read "10E" for SIXTEEN tests. A measured value is read as
 * hex because that is what it is; a count of tests is read as decimal by
 * anyone alive. Two digits covers TC_MAX. */
static void tc_dec(uint16_t row, uint16_t col, uint16_t v)
{
    char buf[3];
    buf[0] = (char)('0' + (v / 10u) % 10u);
    buf[1] = (char)('0' + v % 10u);
    buf[2] = 0;
    tc_puts(row, col, buf);
}
static void tc_hex(uint16_t row, uint16_t col, uint32_t v, uint16_t digits)
{
    char buf[9];
    uint16_t i;
    for (i = 0; i < digits; i++) {
        uint16_t nib = (uint16_t)((v >> ((digits - 1u - i) * 4u)) & 0xFu);
        buf[i] = (char)(nib < 10u ? ('0' + nib) : ('A' + nib - 10u));
    }
    buf[digits] = 0;
    tc_puts(row, col, buf);
}

/* PINNED TO ROM. md.ld folds .rodata into .text at 0x008C0410, but a pointer
 * array can still land in .data, which grows WRAM -- and the 68K .bss guard
 * at 0xFF4600 is unconditional, so it failed the link twice as the test count
 * grew. The names are ROM data and belong in ROM. */
/* THE REFERENCE COLUMN, which is the answer to "what should I expect to see".
 * These are the values ARES measured on 2026-09-27, and they are labelled REF
 * rather than EXPECTED on purpose: a deviation is a FINDING, not a failure.
 * The cart shows VALUE, REF and the delta, so on a machine nobody has profiled
 * -- real hardware, a new core build -- the numbers that differ announce
 * themselves on screen instead of needing a transcription and a diff later.
 * 0xFFFF means no reference yet. */
static const uint16_t TC_REF[TC_MAX] __attribute__((section(".rodata"))) = {
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x005A, 0x005A, 0x0071, 0x0040,
    0x005A, 0x001C, 0x0002, 0x0037, 0x005C, 0x0044,
    0x0038, 0x005A, 0x006B, 0x002A, 0x0071, 0x0040,
    0x000E, 0x001C, 0x0049, 0x0000, 0x1234, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x124B, 0x0000,
    0x0078, 0x0023, 0x0000, 0x0083, 0x0000, 0x0000,
    0x0000, 0x0EEE, 0x0000, 0x0000, 0x80C3, 0x0000,
    0x0000, 0x07FF, 0x0000, 0x0000, 0x0000, 0x0040,
    0x07E0, 0x1000, 0x1000, 0xDEAD, 0x0106, 0x0000,
    0x0000, 0x0000, 0x0000, 0xFFFF, 0x0000
};

/* THE CATEGORY, which is what a page is (Mike, 2026-09-27: "maybe categorized
 * by page"). Overflow paging was the first cut and it is the weaker use of the
 * same mechanism: a page meaning "tests 27 to 30" tells you nothing about what
 * you are looking at, and a page meaning "Z80 BUS" tells you what every number
 * on it is for. */
#define TC_NCAT 7u
/* PAGE 6 IS NOT A CATEGORY. It is the pen grid -- a PICTURE, graded on the
 * host from a screenshot, because the cart cannot read its own composited
 * output back. So the page count is one more than the category count. */
#define TC_NPAGE (TC_NCAT + 1u)
static const char *const TC_CATNAME[TC_NCAT] __attribute__((section(".rodata"))) = {
    "CORE", "68K TIMING", "Z80 BUS", "32X REGS", "COLOUR", "TRANSPORT",
    "MD VDP"
};
static const uint8_t TC_CAT[TC_MAX] __attribute__((section(".rodata"))) = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 3, 3, 3, 2, 2, 2, 2, 2, 2,
    1, 1, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4,
    6, 6, 6, 6, 6, 5, 5, 5, 5, 5, 1, 1,
    2, 5, 5, 5, 0
};

/* PROVENANCE OF THE REFERENCE, and the honest reason it exists: before today
 * every REF in this file was one ares run on 2026-09-27. That catches CHANGE
 * and it cannot catch BOTH MACHINES BEING WRONG, so the column says which
 * numbers are specifications and which are merely baselines.
 *
 *   C  COMPUTED. A first-principles prediction exists and REF agrees with it.
 *      Worked for T08 on 2026-09-27 from the linked ELF: the loop is
 *      MOVE.W (xxx).L,Dn 16 + MOVE.W Dn,(d16,An) 12 + SUBQ.W 4 + Bcc.B taken
 *      10 = 42 cycles, x1024 = 43008, / 488.57 cycles per NTSC line = 88.0
 *      lines against a measured 90. T13 (byte, same 42) predicts 88 and reads
 *      90; T14 (long, 20+16+4+10 = 50) predicts 104.8 and reads 107. Three
 *      loops, and the residual is CONSTANT at ~2 lines rather than scaling
 *      with the loop -- fixed harness overhead, not a broken cycle table.
 *      A readback gate is also C: "what was written must read back" is a
 *      tautology, which is the strongest expectation available.
 *   R  RTL. Read out of the MiSTer core's Verilog, file:line at the test.
 *   D  DOCUMENTED elsewhere -- the 240p suite, or a behaviour two independent
 *      projects hit from two different symptoms (the whole Z80 page).
 *   E  EMPIRICAL ONLY. REF is one ares run and nothing corroborates it.
 *
 * The E list IS the worklist. 24 of 36 today. */
static const char *const TC_PROVCH[4] __attribute__((section(".rodata"))) = {
    "C", "R", "D", "E"
};
static const uint8_t TC_PROV[TC_MAX] __attribute__((section(".rodata"))) = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,
    0, 1, 3, 3, 3, 3, 3, 0, 0, 0, 1, 0,
    1, 1, 0, 1, 0, 2, 2, 2, 2, 0, 3, 3,
    0, 0, 0, 2, 2, 0, 0, 3, 0, 1, 2, 0,
    0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 3, 0
};

static const char *const TC_NAME[TC_MAX] __attribute__((section(".rodata"))) = {
    "WRAM WORD", "WRAM LONG", "CART BANK LONG",
    "CART BANK WORD", "MD VRAM", "MD CRAM",
    "DMA WRAM-VRAM", "SH-2 ALIVE", "T-1K WRAM READ",
    "T-1K CART READ", "T-1K VRAM WRITE", "T-1K FB WRITE",
    "T-1K FB READ", "T-DMA256 ACTIVE", "T-DMA256 VBLANK",
    "FMDL ASSEMBLE", "FMDL BLAST", "FMDL LIFT",
    "FMDL PCT OF FRA", "T-1K BYTE READ", "T-1K LONG READ",
    "T-256 BANKSWITCH", "T-1K VRAM READ", "T-1K MDCRAM WRI",
    "T-DMA256 FILL", "T-DMA256 COPY", "T-1K FB BYTE WR",
    "32X MODE REG", "32X CRAM VBLANK", "32X CRAM ACTIVE",
    "Z80 REQ IN RST", "Z80 GRANT LAT", "Z80 RAM UNINIT",
    "Z80 RAM BYTE", "Z80 RAM WORD", "Z80 GRANT WARM",
    "68K 57344 CYC", "68K 4096 NOPS", "SH-2 PROBE RAN",
    "ADAPTER CTRL", "DREQ CTRL RV", "MD CRAM 64",
    "MD PEN SWEEP", "MD CRAM MASK", "32X CRAM 256",
    "32X CRAM MASK", "32X BMMR MASK", "VRAM TILE 16",
    "VSRAM 40 ENTRY", "VSRAM WIDTH", "DMA FILL LANDED",
    "DMA COPY LANDED", "STATUS VS VCNT", "DREQ 68K PUSH",
    "DREQ SH2 SUM", "DREQ FIRST WORD", "DREQ B RESET", "DREQ C DUMMY", "LINES PER FRM", "RASTER CTR RUN",
    "Z80 EXECUTES", "FMDL RECORDS", "FMDL WORDS",
    "FMDL ROUNDTRIP", "TABLE NOT FULL"
};

/* UPLOAD THE FONT OURSELVES. md_start.s puts it at VRAM 0 early, but boot then
 * clears VRAM and copies the game's tile blob over it ("PURPLE: VDP clear
 * done, before the bank-2 blob copy"), so by the time the suite runs the glyphs
 * are gone -- the screen came up a uniform CRAM-0 colour with the name table
 * full of perfectly good text behind it. The suite owns its own display rather
 * than inheriting whatever boot left.
 *
 * Same packing md_start.s:104-109 uses: each .long is 8 pixels at 4bpp, and
 * masking with 0x11111111 puts the glyph in pen 1 on a pen-0 ground. */
extern const uint32_t font_data[];
extern uint16_t read_joypad(uint8_t player);

/* PEN IS A PARAMETER because the pen grid makes pen 1 BLACK. The tables draw
 * glyphs in pen 1 over a pen-0 ground and set CRAM 1 to white; the grid needs
 * all sixteen pens for its ramps, and in a ramp pen 1 is level 0 -- so the
 * first render of that page had a perfectly good label painted black on
 * black. The grid asks for pen 15, which its own palette 0 puts at (7,7,7). */
static void tc_font_pen(uint32_t mask)
{
    uint16_t i;
    VCTRL = 0x8F02;
    VWIDE = ((uint32_t)(0x4000u | (TC_FONT & 0x3FFFu)) << 16) | (TC_FONT >> 14);
    for (i = 0; i < 45u * 8u; i++) {
        uint32_t row = font_data[i] & mask;
#ifdef R60_FRONTEND
        row = 0x22222222ul ^ ((row & 0x11111111ul) * 3ul); /* opaque black ground */
#endif
        VDATA = (uint16_t)(row >> 16);
        VDATA = (uint16_t)row;
    }
}
static void tc_font(void) { tc_font_pen(0x11111111ul); }

/* ROWS 1..23 ARE THE TABLE. Row 0 is the header and rows 24-27 are the KEY
 * (Mike, 2026-09-27: "present a key at the bottom of the screen ... to
 * identify what the letter results mean"). A one-letter column that needs a
 * doc open beside it is not a readout -- and on real hardware, where no memory
 * dump exists, the screen IS the entire record. The key is in a FIXED place on
 * every page rather than tucked under the last row, so it is where the eye
 * expects it whether a page holds 3 tests or 19.
 * The largest category is 19 tests, so 23 rows leaves four spare. */
#define TC_ROWS 23u
#define tc_order ((volatile uint8_t*)0xFFF380)
static uint16_t tc_shown;

/* ================ THE PEN GRID, page 6 (2026-09-27) ================
 * The other half of Mike's phase-2 ask, and a DIFFERENT CLAIM from anything
 * on the COLOUR page. T24/T25 prove CRAM HOLDS a value; nothing yet proves a
 * pen DISPLAYS. Those are not the same thing -- a palette that reads back
 * perfectly and reaches the DAC wrong is exactly the pink-trees failure, and
 * the project's acceptance gate is pixel truth on ares, not readback.
 *
 * So this page paints, and tools/tc_pattern.py VERIFIES IT FROM A SCREENSHOT.
 * The cart cannot grade this one itself -- it has no way to read the composited
 * output back -- so the grading lives on the host where the pixels are.
 *
 * FOUR RAMPS, one per MD palette, sixteen pens across:
 *   palette 0  grey   (L,L,L)      palette 2  green (0,L,0)
 *   palette 1  red    (L,0,0)      palette 3  blue  (0,0,L)
 * with L = pen >> 1, so each ramp walks all EIGHT levels of its gun twice and
 * the pair-per-level makes a dropped LSB visible as a step that fails to
 * repeat rather than as a colour nobody can name.
 *
 * GEOMETRY IS THE CONTRACT with the verifier, so it is stated once here and
 * parsed from this comment on the host side:
 *   GRID origin tile col 4, tile row 4; cell 2 tiles wide, 3 tiles tall;
 *   16 columns (pen 0..15) x 4 rows (palette 0..3).
 *   => cell (p,n) centre in screen pixels is x = 40 + 16n, y = 44 + 24p.
 *
 * Sixteen SOLID TILES carry the pens: tile 256+n is eight longs of
 * n * 0x11111111, so every one of the 64 pixels in it is pen n. The name
 * table then picks the palette per cell. Nothing about the font or the name
 * table's own region is touched below tile 256. */
/* ---- THE SPLASH AND THE HELP PAGES (2026-09-27) ---------------------
 * Mike: "lets have it as a splash screen similar to the 240p test suite. and
 * then lets have start bring up the explanation pages."
 *
 * The table is dense and every column on it is a convention somebody has to be
 * told. A suite that needs its own documentation open beside it is a suite
 * that will be misread on the one machine where no documentation is available
 * -- real hardware, where the screen is the whole channel. So the explanation
 * ships inside the rom.
 *
 * NO COMMAS, APOSTROPHES OR BRACKETS in any string here: tc_glyph knows space,
 * ! 0-9 A-Z : . - > | + % and nothing else, and an unknown character silently
 * renders as a blank rather than failing. Write around it.
 */
static void tc_frame(const char *title)
{
    uint16_t row, col;
    tc_dma_wait();
#ifdef R60_FRONTEND
    VCTRL = 0x8134;                       /* hide font/window construction */
#endif
    *(volatile uint16_t*)0xA15180 = 0;      /* 32X layer off -- MD shows */
    tc_font();
    VCTRL = 0x8F02;
    VCTRL = 0x8300u | (TC_NAMES >> 10);
    VCTRL = 0x9100;
    VCTRL = 0x9280;
    VWIDE = ((uint32_t)0xC000u << 16) | 0u;
    VDATA = 0x0000;                          /* black */
    VDATA = 0x0EEE;                          /* white */
#ifdef R60_FRONTEND
    VDATA = 0;                               /* opaque black pen 2 */
#endif
    for (row = 0; row < 28u; row++)
        for (col = 0; col < 40u; col++) {
            uint32_t a = TC_NAMES + ((uint32_t)row * 64u + col) * 2u;
            VWIDE = ((uint32_t)(0x4000u | (a & 0x3FFFu)) << 16) | ((a >> 14) & 3u);
            VDATA = TC_TILE;
        }
    tc_puts(0, 1, title);
    for (col = 1; col < 39u; col++) tc_puts(1, col, "-");
#ifdef R60_FRONTEND
    VCTRL = 0x8174;
#endif
}

static void tc_splash(void)
{
    tc_frame("32X TEST CARTRIDGE");
    tc_puts(3,  2, "SEGA SYSTEM 16 TO 32X PORT");
    tc_puts(4,  2, "MACHINE CHARACTERISATION SUITE");

    tc_puts(6,  2, "IT DOES NOT ASK WHETHER THIS");
    tc_puts(7,  2, "MACHINE IS CORRECT. IT REPORTS");
    tc_puts(8,  2, "WHAT IT DOES - AND A DIFFERENCE");
    tc_puts(9,  2, "BETWEEN TWO MACHINES IS THE");
    tc_puts(10, 2, "FINDING.");

    tc_puts(12, 2, "62 TESTS OVER 8 PAGES:");
    tc_puts(13, 4, "CORE        READBACK GATES");
    tc_puts(14, 4, "68K TIMING  LOOPS IN LINES");
    tc_puts(15, 4, "Z80 BUS     THE SOUND CPU");
    tc_puts(16, 4, "32X REGS    ADAPTER AND VDP");
    tc_puts(17, 4, "COLOUR      ALL 512 MD PENS");
    tc_puts(18, 4, "TRANSPORT   FMDL AND DREQ");
    tc_puts(19, 4, "MD VDP      VSRAM AND DMA");
    tc_puts(20, 4, "PEN GRID    A PICTURE");

    tc_puts(22, 2, "D-PAD    CHANGE PAGE");
    tc_puts(23, 2, "START    WHAT THE COLUMNS MEAN");
    tc_puts(25, 2, "PRESS ANY DIRECTION TO BEGIN");
}

static void tc_help(uint16_t page)
{
    if (page == 0) {
        tc_frame("HELP 1 OF 2 - READING A ROW");
        tc_puts(3,  2, "T08 T-1K WRAM READ 005A 005A . C");
        for (page = 2; page < 34u; page++) tc_puts(4, page, "-");

        tc_puts(6,  2, "T08   TEST NUMBER IN HEX");
        tc_puts(8,  2, "NAME  WHAT THE TEST DOES");
        tc_puts(10, 2, "005A  WHAT THIS MACHINE DID");
        tc_puts(12, 2, "005A  WHAT IT SHOULD BE");
        tc_puts(14, 2, ".     THE TWO AGREE");
        tc_puts(15, 8, "+03 IS THREE OVER");
        tc_puts(16, 8, "-03 IS THREE UNDER");
        tc_puts(17, 8, "--- NO REFERENCE YET");
        tc_puts(19, 2, "C     WHERE THE SHOULD-BE CAME");
        tc_puts(20, 8, "FROM. SEE HELP 2 OF 2.");

        tc_puts(23, 2, "SOME TESTS CHECK THEMSELVES AND");
        tc_puts(24, 2, "SHOW PASS OR FAIL INSTEAD.");
        tc_puts(26, 2, "D-PAD NEXT     START BACK");
    } else {
        tc_frame("HELP 2 OF 2 - WHERE REFS COME FROM");
        tc_puts(2,  2, "HOW MUCH TO TRUST THE SHOULD-BE.");

        tc_puts(4,  2, "C  CALCULATED");
        tc_puts(5,  5, "FROM PUBLISHED 68000 CYCLE");
        tc_puts(6,  5, "COSTS. NEVER OBSERVED. IF A");
        tc_puts(7,  5, "MACHINE DISAGREES THEN THE");
        tc_puts(8,  5, "MACHINE IS THE FINDING.");

        tc_puts(10, 2, "R  RTL");
        tc_puts(11, 5, "COUNTED OUT OF THE FPGA CORES");
        tc_puts(12, 5, "VERILOG WITH A LINE NUMBER.");

        tc_puts(14, 2, "D  DOCUMENTED");
        tc_puts(15, 5, "SEGA MANUAL OR A TECHNICAL");
        tc_puts(16, 5, "BULLETIN - OR TWO SEPARATE");
        tc_puts(17, 5, "PROJECTS HITTING IT ALONE.");

        tc_puts(19, 2, "E  ONE RUN ONLY");
        tc_puts(20, 5, "NOBODY DERIVED IT. CATCHES A");
        tc_puts(21, 5, "CHANGE. CANNOT CATCH BOTH");
        tc_puts(22, 5, "MACHINES BEING WRONG TOGETHER.");

        tc_puts(24, 2, "THE TALLY COUNTS THEM PER PAGE.");
        tc_puts(26, 2, "D-PAD BACK     START EXIT");
    }
}

static void tc_pattern(void)
{
    uint16_t n, p2, row, col;

    tc_dma_wait();
    *(volatile uint16_t*)0xA15180 = 0;      /* 32X layer OFF -- MD shows */

    VCTRL = 0x8F02;                          /* autoinc 2 */
    VCTRL = 0x8330;                          /* window plane base 0xC000 */
    VCTRL = 0x9100;
    VCTRL = 0x9280;
    tc_font_pen(0xFFFFFFFFul);               /* pen 15 = white in ramp 0 */

    /* the sixteen solid pen tiles at VRAM 0x2000 = tile 256 */
    VWIDE = ((uint32_t)(0x4000u | (0x2000u & 0x3FFFu)) << 16) | ((0x2000u >> 14) & 3u);
    for (n = 0; n < 16u; n++) {
        uint32_t w = 0x11111111ul * n;
        for (p2 = 0; p2 < 8u; p2++) {        /* 8 rows, each a long */
            VDATA = (uint16_t)(w >> 16);
            VDATA = (uint16_t)w;
        }
    }

    /* the four ramps into all 64 CRAM entries */
    VWIDE = ((uint32_t)0xC000u << 16) | 0u;
    for (p2 = 0; p2 < 4u; p2++)
        for (n = 0; n < 16u; n++) {
            uint16_t L = (uint16_t)(n >> 1);
            uint16_t r = 0, g = 0, b = 0;
            if (p2 == 0) { r = L; g = L; b = L; }
            else if (p2 == 1) r = L;
            else if (p2 == 2) g = L;
            else b = L;
            VDATA = (uint16_t)((b << 9) | (g << 5) | (r << 1));
        }

    for (row = 0; row < 28u; row++) {        /* clear the plane */
        for (col = 0; col < 40u; col++) {
            uint32_t a = TC_NAMES + ((uint32_t)row * 64u + col) * 2u;
            VWIDE = ((uint32_t)(0x4000u | (a & 0x3FFFu)) << 16) | ((a >> 14) & 3u);
            VDATA = 0;
        }
    }
    tc_puts(0, 1, "MD PEN GRID");
    tc_puts(0, 16, "PG");
    tc_hex(0, 19, TC_NPAGE, 1); tc_puts(0, 21, "OF");
    tc_hex(0, 24, TC_NPAGE, 1);

    /* the grid: cell (p,n) is 2 tiles wide, 3 tall, at col 4+2n, row 4+3p */
    for (p2 = 0; p2 < 4u; p2++)
        for (n = 0; n < 16u; n++) {
            uint16_t nt = (uint16_t)((p2 << 13) | (256u + n));
            uint16_t dy, dx;
            for (dy = 0; dy < 3u; dy++)
                for (dx = 0; dx < 2u; dx++) {
                    uint32_t a = 0xC000u
                        + ((uint32_t)(4u + p2 * 3u + dy) * 64u + (4u + n * 2u + dx)) * 2u;
                    VWIDE = ((uint32_t)(0x4000u | (a & 0x3FFFu)) << 16)
                          | ((a >> 14) & 3u);
                    VDATA = nt;
                }
        }
}

static void tc_screen(uint16_t page)
{
    uint16_t i;
    tc_dma_wait();                          /* never paint over a live DMA */
    *(volatile uint16_t*)0xA15180 = 0;      /* 32X layer OFF -- MD shows */
    tc_font();

    VCTRL = 0x8F02;                          /* autoinc 2 */
    VCTRL = 0x8330;                          /* window plane base 0xC000 */
    VCTRL = 0x9100;                          /* window H: from col 0 */
    VCTRL = 0x9280;                          /* window V: from row 0 down */

    /* palette 0: entry 0 the verdict, entry 1 white for the glyphs */
    VWIDE = ((uint32_t)0xC000u << 16) | 0u;
    /* BLACK background (Mike, 2026-09-27): white on green is legible but
     * black is what a test tool should look like, and it keeps the verdict in
     * the words rather than in the wallpaper. */
    VDATA = 0x0000;                                        /* backdrop black */
    VDATA = 0x0EEE;                                        /* white glyphs */

    for (i = 0; i < 28u * 64u; i++) {        /* clear the plane */
        uint32_t a = 0xC000u + (uint32_t)i * 2u;
        VWIDE = ((uint32_t)(0x4000u | (a & 0x3FFFu)) << 16) | ((a >> 14) & 3u);
        VDATA = 0;
    }

    {   /* PAGE = CATEGORY, so a page has a subject rather than a row range.
         * Within a page the GRADED tests still come first: a verdict must
         * never be the thing that falls off the bottom, because a measurement
         * survives in the value block at 0xFFEF00 and in any savestate, and a
         * missing FAIL cannot be guessed. The counts in the header are the
         * WHOLE SUITE's, not the page's -- the verdict worth having is "did
         * anything fail anywhere", and a page holding no graded test would
         * otherwise read 00-00 FAIL. */
        uint16_t n = 0, pass2, graded = 0, ok = 0;
        for (i = 0; i < tc_n; i++) {
            if (tc_res[i] == 1 || tc_res[i] == 2) graded++;
            if (tc_res[i] == 1) ok++;
        }
        for (pass2 = 0; pass2 < 2u; pass2++)
            for (i = 0; i < tc_n; i++) {
                uint16_t g = (uint16_t)(tc_res[i] == 1 || tc_res[i] == 2);
                if (TC_CAT[i] != (uint8_t)page) continue;
                if (g == (pass2 == 0 ? 1u : 0u)) tc_order[n++] = (uint8_t)i;
            }
        tc_shown = n;

        tc_puts(0, 1, TC_CATNAME[page]);
        tc_dec(0, 16, ok); tc_puts(0, 18, "-");
        tc_dec(0, 19, graded);
        tc_puts(0, 22, ok == graded ? "PASS" : "FAIL");
        tc_puts(0, 28, "PG");
        tc_hex(0, 31, (uint16_t)(page + 1u), 1); tc_puts(0, 32, "-");
        tc_hex(0, 33, TC_NCAT, 1);
        /* No category fills a screen today. If one ever does, say how many did
         * not fit rather than dropping them silently -- the deleted colour
         * walk hid its own table once already. */
        if (tc_shown > TC_ROWS) {
            tc_puts(0, 35, "+");
            tc_dec(0, 36, (uint16_t)(tc_shown - TC_ROWS));
        }

        {   /* THE KEY, rows 24-27, with a per-page tally so it earns its four
             * rows twice: it says what the letters mean AND how much of the
             * page in front of you rests on nothing but one ares run. A page
             * reading 00C 00R 00D 13E is telling you it is a baseline, not a
             * specification, without anybody having to remember that. */
            uint16_t nc = 0, nr = 0, nd = 0, ne = 0, j;
            for (j = 0; j < tc_n; j++) {
                if (TC_CAT[j] != (uint8_t)page) continue;
                switch (TC_PROV[j]) {
                case 0: nc++; break;
                case 1: nr++; break;
                case 2: nd++; break;
                default: ne++; break;
                }
            }
            for (j = 1; j < 39u; j++) tc_puts(24, j, "-");
            tc_puts(25, 1, "REF SOURCE");
            tc_dec(25, 24, nc); tc_puts(25, 26, "C");
            tc_dec(25, 28, nr); tc_puts(25, 30, "R");
            tc_dec(25, 32, nd); tc_puts(25, 34, "D");
            tc_dec(25, 36, ne); tc_puts(25, 38, "E");
            tc_puts(26, 1, "C CALC  R RTL  D DOCS  E ONE RUN");

            /* WHICH MACHINE IS THIS? (Mike, 2026-09-27: "maybe a guess at the
             * end -- You are running this in an emulator!")
             *
             * The suite already measures three things the machines disagree
             * on, so it can name itself. It is still an INFERENCE, and the
             * line carries the EVIDENCE beside it for that reason: a bare
             * "ARES" is unfalsifiable from the screen, while "PLATFORM ARES"
             * with "BMMR B3FF" next to it can be checked against the row four
             * lines up. The word is PLATFORM rather than GUESS because the
             * uncertainty belongs in what it NAMES -- "FPGA OR HW" when two
             * platforms are not yet separable -- not in hedging every verdict
             * including the three that are solid.
             *
             *   BMMR readback         THREE emulators, three answers, and the
             *                          register only has five writable bits:
             *                            0xB3FF  ares
             *                            0x80C3  BlastEm -- and the RTL and
             *                                    the official manual
             *                            0x80FF  Genesis Plus GX (OpenEmu,
             *                                    RetroArch), 2026-09-27
             *                          A register whose mask three teams
             *                          implement three ways is a good
             *                          fingerprint and a bad thing to rely on.
             *   cart read slower than  the MiSTer core holds DTACK for an
             *   work-RAM read          SDRAM access on every MD ROM read
             *                          (IF.sv USE_ROM_WAIT); both emulators
             *                          read them equal (T08 vs T09)
             *   neither                BlastEm, by elimination
             *
             * REAL SILICON IS NOT YET DISTINGUISHABLE FROM THE CORE, and the
             * line does not pretend otherwise -- it says FPGA/HW. The row
             * that will separate them is T1D: the manual and the RTL both say
             * a 32X CRAM write during active scan is refused in packed-pixel
             * mode, and a machine that refuses it while reading cart at
             * work-RAM speed is hardware rather than this core. */
            {
                uint16_t bmmr = (uint16_t)tc_v[46];
                uint16_t wram = (uint16_t)tc_v[8], cart = (uint16_t)tc_v[9];
                if (cart >= wram + 2u) {
                    /* checked FIRST because hardware's BMMR is not yet known */
                    tc_puts(27, 1, "PLATFORM FPGA OR HW");
                    tc_puts(27, 22, "CART SLOW");
                } else if (bmmr == 0xB3FFu) {
                    tc_puts(27, 1, "PLATFORM ARES");
                    tc_puts(27, 22, "BMMR B3FF");
                } else if (bmmr == 0x80C3u) {
                    tc_puts(27, 1, "PLATFORM BLASTEM");
                    tc_puts(27, 22, "BMMR 80C3");
                } else if (bmmr == 0x80FFu) {
                    tc_puts(27, 1, "PLATFORM GEN PLUS GX");
                    tc_puts(27, 22, "BMMR 80FF");
                } else {
                    tc_puts(27, 1, "PLATFORM UNKNOWN");
                    tc_puts(27, 22, "NEW MACHINE");
                }
            }
        }
    }
    {
        uint16_t row = 1, k;
        for (k = 0; k < tc_shown && row <= TC_ROWS; k++, row++) {
            uint16_t idx = tc_order[k];
            uint8_t r = tc_res[idx];
            tc_puts(row, 1, "T"); tc_hex(row, 2, idx, 2);
            tc_puts(row, 5, TC_NAME[idx]);
            tc_hex(row, 20, tc_v[idx], 4);
            if (r == 1 || r == 2) {
                tc_puts(row, 25, r == 1 ? "PASS" : "FAIL");
            } else {
                uint16_t ref = TC_REF[idx];
                tc_hex(row, 25, ref, 4);
                /* FOUR DIGITS, not two. T29's ares deviation is 0xB3FF
                 * against 0x80C3 -- a two-digit delta printed that as "3C",
                 * which is not a small difference reported badly, it is a
                 * different number. A readout that truncates is worse than
                 * one that omits. */
                uint32_t d = (tc_v[idx] > ref) ? tc_v[idx] - ref : ref - tc_v[idx];
                /* "---" AND NOT "?": tc_glyph has no question mark, so the
                 * no-reference marker has been drawing as a BLANK since the
                 * column was written -- indistinguishable from a spacing
                 * choice. tools/tc_glyphcheck.py now catches this class. */
                if (ref == 0xFFFFu)        tc_puts(row, 30, "---");
                else if (tc_v[idx] == ref) tc_puts(row, 30, ".");
                else { tc_puts(row, 30, tc_v[idx] > ref ? "+" : "-");
                       tc_hex(row, 31, d, d > 0xFFu ? 4u : 2u); }
            }
            /* WHERE THE EXPECTATION CAME FROM, on every row including the
             * graded ones -- a PASS against a tautology and a PASS against a
             * guess are not the same claim. */
            tc_puts(row, 36, TC_PROVCH[TC_PROV[idx]]);
        }
    }
}

#if defined(TC_PADTEST) || defined(R60_FRONTEND)
/* Diagnostic presentation follows 32x-builder's pad_test_draw: decoded word,
 * lamps in bit order and four distinct recent words. That project's RAW is
 * post-decode (and can be post-sticky-latch); capture the four electrical
 * phase pairs here too, BEFORE rejection can hide the extended bits.
 * Shared UI/data conventions, without importing its COMM8 ownership or sticky
 * latch into this port. The MD owns this screen; neither game CPU is running
 * gameplay. One read per port per video frame lets the pad handshake reset.
 */
extern volatile uint16_t pad_probe[2][6];
static void tc_padtest(uint16_t standalone)
{
    uint16_t hist[2][4] = {{0}}, good[2] = {0}, bad[2] = {0};
    uint16_t prev = 0xFFFFu, frozen = 0, n, k;
    if (standalone) *(volatile uint16_t*)MARS_COMM14 = 0x5044u;  /* PD: park the slave after boot */
    tc_frame("32X CONTROLLER TEST");
    tc_puts(22, 1, "B FREEZE  C CLEAR COUNTERS");
    tc_puts(24, 1, "3 BUTTON CONTROLS WORK HERE");
    tc_puts(25, 1, "NO STICKY OR FORCED 6 BUTTON MODE");
    tc_puts(26, 1, standalone ? "VALUES ARE HEX  RESET TO EXIT" : "VALUES HEX  START OR A+C BACK");
    for (;;) {
        uint16_t pad[2], fresh;
        tc_frame_wait();
        pad[0] = read_joypad(0);
        pad[1] = read_joypad(1);
        fresh = (uint16_t)(pad[0] & ~prev);
        prev = pad[0];
        if (!standalone && ((fresh & 0x80u) || (pad[0] & 0x60u) == 0x60u)) return;
        if (fresh & 0x10u) frozen ^= 1u;
        if (fresh & 0x20u) {
            for (n = 0; n < 2u; n++) {
                good[n] = bad[n] = 0;
                for (k = 0; k < 4u; k++) hist[n][k] = 0;
            }
        }
        for (n = 0; n < 2u; n++) {
            if (pad[n] & 0x1000u) { if (good[n] != 0xFFFFu) good[n]++; }
            else { if (bad[n] != 0xFFFFu) bad[n]++; }
            if (pad[n] != hist[n][0]) {
                for (k = 3; k; k--) hist[n][k] = hist[n][k-1];
                hist[n][0] = pad[n];
            }
        }
        tc_puts(21, 1, frozen ? "FROZEN - B RESUMES" : "LIVE             ");
        if (frozen) continue;
        for (n = 0; n < 2u; n++) {
            uint16_t col = 1u + n * 20u;
            char lamps[13];
            static const char labels[] = "MXYZSACBRLDU";
            tc_puts(2, col, n ? "PORT 2" : "PORT 1");
            tc_puts(3, col, "WORD:"); tc_hex(3, col+5, pad[n], 4);
            tc_puts(4, col, (pad[n] & 0x1000u) ? "TYPE:6 BUTTON   " : "TYPE:3 OR ABSENT");
            tc_puts(5, col, "POLL:"); tc_hex(5, col+5, pad_probe[n][5], 4);
            tc_puts(6, col, labels);
            for (k = 0; k < 12u; k++) lamps[k] = (pad[n] & (1u << (11u-k))) ? labels[k] : '.';
            lamps[12] = 0; tc_puts(7, col, lamps);
            tc_puts(9, col, "PHASE  LOW HIGH");
            for (k = 0; k < 4u; k++) {
                tc_hex(10+k, col, k+1, 1);
                tc_hex(10+k, col+7, pad_probe[n][k] >> 8, 2);
                tc_hex(10+k, col+11, pad_probe[n][k] & 0xFFu, 2);
            }
            tc_puts(15, col, "ID3:"); tc_puts(15, col+4, (pad_probe[n][2] & 0x0F00u) ? "NO " : "YES");
            tc_puts(16, col, "ID4:"); tc_puts(16, col+4, (pad_probe[n][3] & 0x0C00u) ? "YES" : "NO ");
            tc_puts(17, col, "6 OK:"); tc_hex(17, col+5, good[n], 4);
            tc_puts(18, col, "MISS:"); tc_hex(18, col+5, bad[n], 4);
            for (k = 0; k < 4u; k++) tc_hex(19, col+k*5u, hist[n][k], 4);
        }
    }
}
#endif

#ifdef R60_FRONTEND
/* THE OPTIONS SCREEN (hold B at power-on, or START+B on the title). Rows, top to bottom:
 * VERSION (the region byte: games/<title>/game.py REGIONS), STARTING ROUND, the arcade DIP
 * sheet (game.py DIPS), CONTROLLER TEST, RESTORE ARCADE DEFAULTS, RETURN TO ARCADE.
 *
 * WHAT TAKES EFFECT WHEN (2026-10-30, Mike: region at power-on only; the full sheet; a
 * region change resets the switches to that board). The region and the coin switches (bank
 * 1) are read once, by the game's boot, so they are offered only on the power-on menu and
 * shown locked on the title's. Bank 2 is re-latched every frame, so it changes from both. */
static const char *const fe_region_names[] = GAME_REGIONS_NAMES;
static const uint8_t fe_region_dsw1[] = GAME_REGIONS_DSW1, fe_region_dsw2[] = GAME_REGIONS_DSW2;
static const char *const fe_dip_names[] = GAME_DIP_NAMES;
static const char *const fe_dip_labels[] = GAME_DIP_LABELS;
static const uint8_t fe_dip_bank[] = GAME_DIP_BANK, fe_dip_mask[] = GAME_DIP_MASK;
static const uint8_t fe_dip_first[] = GAME_DIP_FIRST, fe_dip_count[] = GAME_DIP_COUNT;
static const uint8_t fe_dip_values[] = GAME_DIP_VALUES;
#define FE_ROW_REGION 0u
#define FE_ROW_ROUND  1u
#define FE_ROW_DIP0   2u
#define FE_ROW_PADS   (FE_ROW_DIP0 + GAME_DIPS_N)
#define FE_ROW_RESET  (FE_ROW_PADS + 1u)
#define FE_ROW_RETURN (FE_ROW_PADS + 2u)
#define FE_ROWS       (FE_ROW_PADS + 3u)

static uint16_t fe_screen_row(uint16_t r)
{
    if (r == FE_ROW_REGION) return 3u;
    if (r == FE_ROW_ROUND) return 4u;
    if (r < FE_ROW_PADS) return (uint16_t)(6u + (r - FE_ROW_DIP0));
    return (uint16_t)(7u + GAME_DIPS_N + (r - FE_ROW_PADS));
}

static int fe_editable(uint16_t r, uint16_t at_boot)
{
    if (r == FE_ROW_REGION) return at_boot && GAME_REGIONS_N > 1u;
    if (r >= FE_ROW_DIP0 && r < FE_ROW_PADS) return at_boot || fe_dip_bank[r - FE_ROW_DIP0] != 1u;
    return 1;
}

/* the option index a switch currently holds, or 0xFF when the byte matches none of them */
static uint16_t fe_dip_option(uint16_t d, const uint8_t *dsw)
{
    uint16_t k, v = dsw[fe_dip_bank[d] - 1u] & fe_dip_mask[d];
    for (k = 0; k < fe_dip_count[d]; k++)
        if (fe_dip_values[fe_dip_first[d] + k] == v) return k;
    return 0xFFu;
}

void frontend_menu_screen(void)
{
    static uint16_t round, region, set_up;
    static uint8_t dsw[2];
    uint16_t palette[3], mode, n, selected = FE_ROW_ROUND, prev = 0xFFFFu, dirty = 1;
    uint16_t at_boot = frontend_at_boot;
    if (!set_up) { dsw[0] = fe_region_dsw1[0]; dsw[1] = fe_region_dsw2[0]; set_up = 1; }
    if (at_boot) selected = FE_ROW_REGION;
    tc_dma_wait();
    VWIDE = 0x00000020ul;                /* CRAM read, auto-increment 2 */
    for (n = 0; n < 3u; n++) palette[n] = VDATA;
    mode = *(volatile uint16_t*)0xA15180;
    VCTRL = 0x8174;                       /* display on; CPU interrupts masked */
    for (;;) {
        uint16_t pad, fresh;
        if (dirty) {
            tc_frame(GAME_TITLE " - OPTIONS");
            tc_puts(3, 2, "VERSION");
            tc_puts(3, 21, fe_region_names[region]);
            if (!at_boot) tc_puts(3, 25, "- POWER-ON ONLY");
            tc_puts(4, 2, "STARTING ROUND"); tc_hex(4, 21, round + 1u, 1);
            for (n = 0; n < GAME_DIPS_N; n++) {
                uint16_t k = fe_dip_option(n, dsw);
                tc_puts(6u + n, 2, fe_dip_names[n]);
                tc_puts(6u + n, 21, k < fe_dip_count[n] ? fe_dip_labels[fe_dip_first[n] + k] : "?");
            }
            tc_puts(fe_screen_row(FE_ROW_PADS), 2, "CONTROLLER TEST");
            tc_puts(fe_screen_row(FE_ROW_RESET), 2, "RESTORE ARCADE DEFAULTS");
            tc_puts(fe_screen_row(FE_ROW_RETURN), 2, "RETURN TO ARCADE");
            tc_puts(fe_screen_row(selected), 0, ">");
            tc_puts(21, 2, "UP DOWN SELECT  LEFT RIGHT CHANGE");
            tc_puts(22, 2, "A OR START CONFIRM  B RETURN");
            tc_puts(24, 2, at_boot ? "VERSION AND COINS: POWER-ON MENU"
                                   : "COINS LOCKED: HOLD B AT POWER-ON");
            tc_puts(25, 2, "ROUND APPLIES TO THE NEXT GAME");
            dirty = 0;
        }
        tc_frame_wait(); pad = read_joypad(0); fresh = pad & ~prev; prev = pad;
        if (fresh & 0x10u) break;
        if (fresh & (1u | 2u)) {          /* up / down, over the rows this menu may edit */
            do selected = (fresh & 1u) ? (selected ? selected - 1u : FE_ROWS - 1u)
                                       : (uint16_t)((selected + 1u) % FE_ROWS);
            while (!fe_editable(selected, at_boot));
            dirty = 1;
        }
        if (fresh & (4u | 8u | 0x40u | 0x0880u)) {
            int direction = (fresh & 4u) ? -1 : 1;
            if (selected == FE_ROW_REGION) {
                region = (uint16_t)((region + GAME_REGIONS_N + direction) % GAME_REGIONS_N);
                dsw[0] = fe_region_dsw1[region];   /* that board's switches (Mike 2026-10-30) */
                dsw[1] = fe_region_dsw2[region];
            }
            if (selected == FE_ROW_ROUND) round = (uint16_t)((round + GAME_ROUNDS + direction) % GAME_ROUNDS);
            if (selected >= FE_ROW_DIP0 && selected < FE_ROW_PADS) {
                uint16_t d = selected - FE_ROW_DIP0, k = fe_dip_option(d, dsw), c = fe_dip_count[d];
                uint8_t *b = &dsw[fe_dip_bank[d] - 1u];
                k = (k < c) ? (uint16_t)((k + c + direction) % c) : 0u;
                *b = (uint8_t)((*b & ~fe_dip_mask[d]) | fe_dip_values[fe_dip_first[d] + k]);
            }
            if (fresh & (0x40u | 0x0880u)) {
                if (selected == FE_ROW_PADS) { tc_padtest(0); prev = 0xFFFFu; }
                if (selected == FE_ROW_RESET) {
                    round = 0;
                    if (at_boot) dsw[0] = fe_region_dsw1[region];
                    dsw[1] = fe_region_dsw2[region];
                }
                if (selected == FE_ROW_RETURN) break;
            }
            dirty = 1;
        }
    }
    /* Consume the whole opening/closing chord, including B and Start. */
    do { tc_frame_wait(); } while (read_joypad(0) & 0x0FFFu);
    tc_dma_wait();
    VWIDE = 0xC0000000ul;
    for (n = 0; n < 3u; n++) VDATA = palette[n];
    VCTRL = 0x832C; VCTRL = 0x9100; VCTRL = 0x9200; /* normal disabled window */
    *(volatile uint16_t*)0xA15180 = mode;
    /* three words, each after the master asks for it (frontend.h) */
    *(volatile uint16_t*)MARS_COMM12 = FE_RESULT | round | (region << 3);
    while (*(volatile uint16_t*)MARS_COMM12 != FE_MORE1) __asm__ volatile ("nop");
    *(volatile uint16_t*)MARS_COMM12 = FE_DSW1 | dsw[0];
    while (*(volatile uint16_t*)MARS_COMM12 != FE_MORE2) __asm__ volatile ("nop");
    *(volatile uint16_t*)MARS_COMM12 = FE_DSW2 | dsw[1];
}
#endif

void testcart_run(void)
{
#ifdef TC_PADTEST
    tc_padtest(1);
#endif
    uint16_t i, pass = 0;
    for (i = 0; i < TC_MAX; i++) tc_res[i] = 0;
    tc_n = 0;
    tc_run_all();
    tc_paint();
    {   /* THE TABLE IS THE READOUT. There used to be a colour "walk" here for
         * hardware, cycling one colour per test -- and once the table existed
         * the walk actively HID it: a failing run repainted the backdrop for
         * ever, so the screen read as a solid colour and I spent four rebuilds
         * hunting VDP corruption that was never there. The suite was reporting
         * a failure exactly as designed, over the top of the evidence.
         * A table needs no walk. */
        uint16_t graded = 0;
        for (i = 0; i < tc_n; i++) {
            if (tc_res[i] == 1) pass++;
            if (tc_res[i] == 1 || tc_res[i] == 2) graded++;
        }
        (void)graded; (void)pass;
#ifndef TC_PAGE0
#define TC_PAGE0 0
#endif
        /* THREE SCREENS, ONE LOOP. The cart opens on the SPLASH, any direction
         * enters the TABLE, and START swaps between the table and the HELP
         * pages from wherever you are. Edge-detected throughout: acting on a
         * HELD button makes a seven-page list unusable.
         *
         * Bits are active-high from read_joypad (md_start.s):
         *   0 0 0 1 M X Y Z S A C B R L D U
         * so UP 0x01, DOWN 0x02, LEFT 0x04, RIGHT 0x08, START 0x80. */
        {
            uint16_t page = TC_PAGE0, prev = 0xFFFFu, helpp = 0;
            uint16_t mode = 0;                       /* 0 splash 1 table 2 help */
#ifdef TC_NOSPLASH
            /* TCPAGE=n SKIPS THE SPLASH, because the only reason to name a
             * start page is that nobody can press a direction -- the MiSTer
             * launch API returns screenshots and takes no pad input. The first
             * rig run of the full suite captured eight copies of the splash. */
            mode = 1u;
            if (page >= TC_NCAT) tc_pattern(); else tc_screen(page);
#else
            tc_splash();
#endif
            for (;;) {
                uint16_t pad = read_joypad(0);
                uint16_t fresh = (uint16_t)(pad & ~prev);
                uint16_t fwd = (uint16_t)(fresh & (0x0002u | 0x0008u));
                uint16_t back = (uint16_t)(fresh & (0x0001u | 0x0004u));
                prev = pad;

                if (fresh & 0x0080u) {               /* START */
                    if (mode == 2u) {
                        mode = 1u;
                        if (page >= TC_NCAT) tc_pattern(); else tc_screen(page);
                    } else {
                        mode = 2u; helpp = 0; tc_help(helpp);
                    }
                } else if (mode == 0u) {
                    if (fwd || back) {
                        mode = 1u;
                        if (page >= TC_NCAT) tc_pattern(); else tc_screen(page);
                    }
                } else if (mode == 2u) {
                    if (fwd && helpp == 0u)      { helpp = 1u; tc_help(helpp); }
                    else if (back && helpp == 1u) { helpp = 0u; tc_help(helpp); }
                } else {
                    if (fwd && page + 1u < TC_NPAGE) {
                        page++;
                        if (page >= TC_NCAT) tc_pattern(); else tc_screen(page);
                    } else if (back && page) {
                        page--;
                        if (page >= TC_NCAT) tc_pattern(); else tc_screen(page);
                    }
                }
                tc_hold(1);                     /* ~one poll a frame is plenty */
            }
        }
    }
}
#endif /* TEST_CART */
