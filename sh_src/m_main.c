#include "mars.h"

/* Stage C (step 1): display the game's live palette. The MD shim streams
 * System-16 palette words to us in COMM batches (see md_main.c). We convert
 * each to the 32X's BGR555 and write CRAM, then show a 16x16 grid of colour
 * swatches — one cell per CRAM entry — so the game's palette appears on
 * screen and animates as the game runs. Proves the game-data -> 32X path
 * end to end over the proven COMM channel (DREQ bulk transfer comes later).
 *
 * COMM protocol (MD -> SH2):
 *   COMM0  : bit15 = batch-ready, bits7-0 = start CRAM index
 *   COMM2..COMM10 : 5 raw System-16 palette words
 *   SH2 consumes, writes CRAM[start..start+4], acks by clearing COMM0.
 */

/* Functions that run after the MD sets RV=1 MUST live in SDRAM — with RV
 * set the SH-2 is forbidden from touching cart ROM (d32xr rule; ares
 * enforces it, MAME doesn't). mars.ld's .ramtext is copied to SDRAM by the
 * BIOS module load along with .data. */
#define RAMCODE __attribute__((section(".ramtext")))

/* System-16 palette word  sBGR BBBB GGGG RRRR  ->  32X BGR555. */
static inline uint16_t s16_to_mars(uint16_t v)
{
    uint16_t r = ((v >> 12) & 0x01) | ((v << 1) & 0x1e);
    uint16_t g = ((v >> 13) & 0x01) | ((v >> 3) & 0x1e);
    uint16_t b = ((v >> 14) & 0x01) | ((v >> 7) & 0x1e);
    return (uint16_t)((b << 10) | (g << 5) | r);
}

/* Fill the framebuffer with a 16x16 grid of swatches; cell (cx,cy) is
 * painted with pixel value = CRAM index cy*16+cx, so CRAM supplies colour. */
RAMCODE static void draw_swatches(void)
{
    volatile uint16_t *fb = &MARS_FRAMEBUFFER;
    /* 320x224, 8bpp: 160 words/line, pixels packed 2/word. 20px cells. */
    for (int y = 0; y < 224; y++) {
        int cy = y / 14;
        if (cy > 15) cy = 15;
        volatile uint16_t *line = fb + 0x100 + y * 160;
        for (int xw = 0; xw < 160; xw++) {
            int cx = (xw * 2) / 20;
            if (cx > 15) cx = 15;
            uint8_t idx = (uint8_t)(cy * 16 + cx);
            line[xw] = ((uint16_t)idx << 8) | idx;
        }
    }
}

RAMCODE void m_main(void)
{
    /* Release the secondary SH-2 from its S_OK wait. */
    MARS_SYS_COMM4 = 0;

    Hw32xInit(MARS_VDP_MODE_256, 0);

    /* The game's MD-side VDP is enabled but unused (all backdrop), and would
     * cover the 32X layer at the default 68K priority. Give the 32X layer
     * priority so our rendered output shows. */
    MARS_VDP_DISPMODE = MARS_NTSC_FORMAT | MARS_224_LINES | MARS_VDP_PRIO_32X | MARS_VDP_MODE_256;

    /* Init-time CRAM preload: a static rainbow so the swatch grid is visible
     * even before (or without) the live palette stream. Also isolates
     * runtime-CRAM-write failures: rainbow grid + no animation = init writes
     * land, runtime writes don't. */
    volatile uint16_t *cram = &MARS_CRAM;
    for (int i = 0; i < 256; i++) {
        uint16_t r = (i & 0x07) << 2;
        uint16_t g = ((i >> 3) & 0x07) << 2;
        uint16_t b = ((i >> 6) & 0x03) << 3;
        cram[i] = (uint16_t)((b << 10) | (g << 5) | r);
    }

    /* Static swatch grid in both buffers; only CRAM changes per frame. */
    draw_swatches();
    Hw32xScreenFlip(1);
    draw_swatches();
    Hw32xScreenFlip(1);

    /* Fire-and-forget: apply the current batch every iteration (idempotent
     * per index; the MD advances the index each frame). No ack — the MD
     * overwrites COMM0 freely. */
    /* CRAM is only writable during VBLANK. Sync to the 68K's vblank counter
     * (COMM12, incremented by the shim each vblank) — the same proven pattern
     * backrooms uses — rather than the SH-2's FBCTL VBLK bit, which isn't a
     * reliable wait source here. Apply one palette batch per vblank. */
    /* Signal the MD that the master SH-2 is executing from SDRAM: only now
     * may it set RV=1 (which cuts our ROM access). Everything after this
     * point must be ROM-free: the loop reads COMM and writes CRAM only. */
    MARS_SYS_COMM14 = 0x600D;

    uint16_t lastTick = MARS_SYS_COMM12;
    for (;;) {
        while (lastTick == MARS_SYS_COMM12)
            ;                                    /* wait for the 68K vblank */
        lastTick = MARS_SYS_COMM12;
        uint16_t c0 = MARS_SYS_COMM0;
        if (c0 & 0x8000) {
            int start = c0 & 0xFF;
            cram[(start + 0) & 0xFF] = s16_to_mars(MARS_SYS_COMM2);
            cram[(start + 1) & 0xFF] = s16_to_mars(MARS_SYS_COMM4);
            cram[(start + 2) & 0xFF] = s16_to_mars(MARS_SYS_COMM6);
            cram[(start + 3) & 0xFF] = s16_to_mars(MARS_SYS_COMM8);
            cram[(start + 4) & 0xFF] = s16_to_mars(MARS_SYS_COMM10);
        }
    }
}
