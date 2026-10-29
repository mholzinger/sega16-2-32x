/* THE SHIPPING TRANSPORT, PROVED (2026-09-27).
 *
 * WHAT WAS MISSING. The FMDL proof shows the 68K staging a list in the
 * framebuffer and the SH-2 executing it -- but that is not how this port
 * actually moves its per-frame data. The DREQ FIFO is: the 68K pushes words
 * into 0xA15112 with 68S set and the SH-2's DMAC pulls them straight out of
 * 0x20004012 into SDRAM. Every packet the renderer eats arrives that way, and
 * nothing tested it end to end.
 *
 * It is also the path with the most measured pain in this project -- partial
 * DREQ landings, the lost-push belt, "no packet lands in MAME at all under
 * R60". A transport with that history and no test is the wrong shape.
 *
 * THE PROTOCOL, taken from the shipping code rather than invented:
 *     SH-2   SAR0 = 0x20004012 (the FIFO), DAR0 = an uncached SDRAM buffer,
 *            TCR0 = words, DRCR0 = 0, DMAOR = 1, CHCR0 = 0x44E1
 *            (m_main.c:12010-12024, the R60 arm)
 *     68K    length -> 0xA15110, 68S via 0xA15107 = 4, then one word at a
 *            time into 0xA15112, waiting while bit 7 of 0xA15107 says FULL
 *            (md_main.c:3821-3835, "Chaotix protocol")
 *
 * ORDER MATTERS AND THE HANDSHAKE ENFORCES IT: the SH-2 must have armed its
 * DMAC before the first word is pushed, so it posts DREQ_ARMED and the 68K
 * waits for that rather than guessing a delay.
 *
 * THE BUFFER IS ZEROED FIRST so that a DMA which does nothing scores 0 rather
 * than inheriting whatever was there -- the same reason the FMDL proof clears
 * its destination.
 */
#ifndef DREQ_PROOF_H
#define DREQ_PROOF_H

#define DRQ_DST  0x2603C000u             /* 128 B uncached SDRAM, clear of .ramtext
                                          * (ends 0x06037E50), the probe block
                                          * at 0x0603B000 and the master stack
                                          * at 0x0603FC00 */
#define DRQ_N    64u

/* Returns the 16-bit sum of what actually arrived. The 68K pushes 0x1000+i for
 * i in 0..63, so the sum is 64*0x1000 + 2016, which is 0x07E0 in sixteen bits.
 * A SUM rather than a compare because one number crosses the comm port and
 * still catches a wrong value, a wrong order and a short transfer. */
static uint16_t dreq_recv(void)
{
    volatile uint16_t *dst = (volatile uint16_t *)DRQ_DST;
    uint16_t i, sum = 0;
    uint32_t g;

    for (i = 0; i < DRQ_N; i++) dst[i] = 0;

    SH2_DMA_SAR0  = 0x20004012u;         /* the DREQ FIFO */
    SH2_DMA_DAR0  = DRQ_DST;
    SH2_DMA_TCR0  = DRQ_N;
    SH2_DMA_DRCR0 = 0;
    SH2_DMA_DMAOR = 1;
    SH2_DMA_CHCR0 = 0x44E1u;

    MARS_SYS_COMM4 = 0x4441u;            /* 'DA' -- armed, push now */

    g = 4000000ul;
    while (!(SH2_DMA_CHCR0 & 2u) && --g) ;   /* CHCR bit 1 = transfer end */

    for (i = 0; i < DRQ_N; i++) sum = (uint16_t)(sum + dst[i]);
    return sum;
}

/* THE FIRST WORD THAT ARRIVED, reported separately because the SUM alone says
 * how much content is missing and never says WHAT HAPPENED. The FPGA run of
 * 2026-09-27 read 0xF7E0 against 0x07E0 -- exactly 0x1000 short, which is the
 * first pushed word's value, and three different faults produce that:
 *     dst[0] == 0x1000   the first word DID arrive, something else is missing
 *     dst[0] == 0x1001   SHIFTED -- the first was dropped and the rest slid up
 *     dst[0] == 0x0000   the first slot was never written at all
 * One extra word over the comm port turns "something is missing" into which.
 * The buffer is not touched between the two commands, so this reads the same
 * transfer rather than provoking a new one. */
static uint16_t dreq_first(void)
{
    return *(volatile uint16_t *)DRQ_DST;
}
#endif /* DREQ_PROOF_H */
