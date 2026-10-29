/* SH-2 HARDWARE PROBE (2026-09-27, Mike: "as many useful hardware level tests
 * ... ram bursts, cache clears ... in pure simple clean code").
 *
 * WHY IT HAD TO BE SH-2 SIDE. TESTCART is 68K code, so everything it knows
 * about the 32X is inferred -- from the MiSTer RTL, from the official manual,
 * or from the 68K's own view through the adapter. The cache, the FRT, CCR and
 * every SDRAM/cart access cost live on the far side of that wall, and they are
 * exactly what the SPEED axis is made of. ares cannot help either: it charges
 * instruction cycles only, so the one term the SH7604 manual says the CPU
 * always waits for -- the write-through store -- is invisible to it.
 *
 * HOW IT REPORTS. One fixed array in SDRAM. Both instruments dump SDRAM with
 * the identical switch, so the results diff exactly like the 68K side's:
 *     ares-headless rom --frames 1500 --dump sdram:0x0603B000:0x80:a.bin
 *     blastem -b 600 -m 32x --dump sdram:0x0603B000:0x80:b.bin rom
 * 0x0603B000 sits above .ramtext (ends 0x06037E50) and ~19 KB below the master
 * stack at 0x0603FC00; mars.ld asserts the gap.
 *
 * IT LIVES IN .porttest, NOT .ramtext, AND THAT CHANGES ITS OWN NUMBERS.
 * The probe first went into the hot SDRAM-resident region with the rest of
 * m_main and pushed the .ramtext ROM image 548 bytes into .tilesmd -- the
 * link failed exactly as mars.ld:112 says it will. .porttest is the measured
 * free gap above the blob (mars.ld:118-131, ~62 KB, "empty and harmless when
 * PORTTEST is off") and a suite that runs once a boot has no business in the
 * renderer's residency budget.
 *
 * The cost is that the probe's own instructions now fetch from CART. The
 * loops are a handful of instructions and go cache-resident immediately, so
 * the DIFFERENTIAL results -- miss costs, the cart/SDRAM ratio, the purge
 * proof -- are unaffected by construction: they are subtractions in which the
 * loop appears on both sides. The one figure that moves is the absolute
 * "resident access + loop", which now includes a cart-fetched loop body.
 * Any absolute figure quoted from before 2026-09-27 19:00 was taken with this
 * code SDRAM-resident and is not comparable.
 *
 * HOW IT IS TRIGGERED. The 68K cart writes SHP_GO to COMM4 and spins; the
 * master runs the probe and writes SHP_DONE. COMM4 is free after boot -- its
 * only other use is md_start.s:159's S_OK handshake. The handshake exists so
 * the 68K is IDLE while the probe times things: an adapter-register poll costs
 * ~85 cycles of bus (REBUILD.md) and would contaminate every number here.
 *
 * THE CLOCK. The master's FRT at TCR=1 is phi/32, so one tick is 32 SH-2
 * cycles (m_main.c:2722). Every figure below is TICKS for a fixed iteration
 * count, so they are comparable to each other and across machines without
 * anyone having to convert anything.
 */
#ifndef SH_PROBE_H
#define SH_PROBE_H

#define SHP      ((volatile uint32_t *)0x0603B000)
#define SHP_GO   0x5350u                 /* 68K -> master: run the probe */
#define SHP_DONE 0x5044u                 /* master -> 68K: results are up */

#define SHP_ITER 1024u                   /* steps per timed walk */
#define SHP_SDRAM 0x06020000u            /* 16 KB read window, port_load's */
#define SHP_CART  0x02010000u            /* cart ROM through CS1 */

/* ONE LONG READ PER STEP, striding by `stride` and wrapping inside a window of
 * `size` bytes. That is the whole instrument: change the stride and you change
 * the miss rate; change the size and you change whether the window fits in the
 * 4 KB cache. Everything below is this loop with different numbers. */
__attribute__((section(".porttest"), noinline))
static uint16_t shp_walk_n(uint32_t base, uint32_t size, uint32_t stride,
                           uint16_t iters)
{
    uint32_t p = base, end = base + size;
    uint16_t i, t0;
    volatile uint32_t sink = 0;
    t0 = frt();
    for (i = 0; i < iters; i++) {
        sink = *(volatile uint32_t *)p;
        p += stride;
        if (p >= end) p = base;
    }
    (void)sink;
    return (uint16_t)(frt() - t0);
}
__attribute__((section(".porttest"), noinline))
static uint16_t shp_walk(uint32_t base, uint32_t size, uint32_t stride)
{
    return shp_walk_n(base, size, stride, SHP_ITER);
}

/* The same loop STORING instead of loading, so the pair isolates the one cost
 * the manual says can never be cached away: "This cache is of the write-through
 * type ... The CPU waits until the writes onto the internal bus are completed"
 * (sh7604-hardware-manual.md 8.4.2). Stores should not get cheaper when the
 * window fits in cache; loads should. */
__attribute__((section(".porttest"), noinline))
static uint16_t shp_store(uint32_t base, uint32_t size, uint32_t stride)
{
    uint32_t p = base, end = base + size;
    uint16_t i, t0;
    t0 = frt();
    for (i = 0; i < SHP_ITER; i++) {
        *(volatile uint32_t *)p = 0x5A5A5A5Au;
        p += stride;
        if (p >= end) p = base;
    }
    return (uint16_t)(frt() - t0);
}

__attribute__((section(".porttest"), noinline))
static void shp_purge(void)
{
    SH2_CCTL = (uint8_t)(SH2_CCTL | SH2_CCTL_CP);   /* CP self-clears */
}

__attribute__((section(".porttest"), noinline))
static void sh_probe(void)
{
    uint16_t hot, cold;

    SHP[0] = 0x53485032u;                /* SHP2: the probe ran */
    SHP[1] = SH2_CCTL;                   /* cache control as it really is */
    SHP[2] = SH2_FRT_TCR;                /* prescale, so ticks are convertible */
    SHP[3] = SHP_ITER;

    /* CACHE CLEAR, PROVED BY BEHAVIOUR rather than by the register write.
     * Walk a 2 KB window twice: the second pass is hot. Purge, walk again --
     * that pass must cost like the first. A CP bit that reads back 0 proves
     * nothing; a re-miss does. */
    (void)shp_walk(SHP_SDRAM, 2048u, 16u);          /* warm it */
    hot = shp_walk(SHP_SDRAM, 2048u, 16u);
    shp_purge();
    cold = shp_walk(SHP_SDRAM, 2048u, 16u);
    SHP[4] = hot;
    SHP[5] = cold;

    /* WORKING SET vs THE 4 KB CACHE. 2 KB fits; 16 KB is 4x oversubscribed,
     * the shape FULL-PORT names as the planning case. Both walk one cache line
     * a step, so the only variable is residency. */
    (void)shp_walk(SHP_SDRAM, 2048u, 16u);
    SHP[6] = shp_walk(SHP_SDRAM, 2048u, 16u);       /* resident */
    (void)shp_walk(SHP_SDRAM, 16384u, 16u);
    SHP[7] = shp_walk(SHP_SDRAM, 16384u, 16u);      /* all-miss */

    /* THE BURST, AND THE FOOTPRINT MUST MATCH. Stride 16 touches a fresh line
     * every step; stride 4 gets three hits then a miss, because one fill
     * brings four longwords (manual 8.1, "Four 32-bit accesses are required to
     * update a line"). So the fair comparison is SAME BYTES COVERED, not same
     * iterations: 1024 x 16 and 4096 x 4 are both exactly the 16 KB window.
     *
     * The first cut ran both at 1024 iterations and read 616 vs 328 -- which
     * is not a 1.9x line-fill payoff, it is a comparison between a 16 KB
     * footprint and a 4 KB one that happens to fit in the cache. [9] is per
     * 1024 steps so it stays comparable to [8]; [15] is the raw 4096. */
    shp_purge();
    SHP[8] = shp_walk_n(SHP_SDRAM, 16384u, 16u, 1024u);
    shp_purge();
    {
        uint16_t t = shp_walk_n(SHP_SDRAM, 16384u, 4u, 4096u);
        SHP[15] = t;
        SHP[9] = (uint32_t)(t / 4u);
    }

    /* WRITE-THROUGH. Loads and stores over the SAME resident 2 KB window.
     * Loads should be cheap (they hit); stores should not (they always go
     * out). If these come back equal, this machine is not modelling the
     * store path -- which is the known ares gap, stated from the SH-2 side. */
    (void)shp_walk(SHP_SDRAM, 2048u, 16u);
    SHP[10] = shp_walk(SHP_SDRAM, 2048u, 16u);
    SHP[11] = shp_store(SHP_SDRAM, 2048u, 16u);

    /* CS AREAS. SDRAM (CS3) against cart ROM (CS1), both all-miss, so the
     * difference is the bus and not the cache. WCR gives CS1 a wait state that
     * CS3 does not have (sh7604 7.2.3, and BLASTEM.md records the BIOS leaving
     * WCR = 0x0055). This is that claim, measured. */
    shp_purge();
    SHP[12] = shp_walk(SHP_SDRAM, 16384u, 16u);
    shp_purge();
    SHP[13] = shp_walk(SHP_CART, 16384u, 16u);

    SHP[14] = 0xC0FFEE00u;               /* tail marker: nothing truncated */
}
#endif /* SH_PROBE_H */
