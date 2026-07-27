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
static void draw_swatches(void)
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

void m_main(void)
{
    /* Release the secondary SH-2 from its S_OK wait. */
    MARS_SYS_COMM4 = 0;

    Hw32xInit(MARS_VDP_MODE_256, 0);

    volatile uint16_t *cram = &MARS_CRAM;
    for (int i = 0; i < 256; i++)
        cram[i] = 0;

    /* Static swatch grid in both buffers; only CRAM changes per frame. */
    draw_swatches();
    Hw32xScreenFlip(1);
    draw_swatches();
    Hw32xScreenFlip(1);

    /* Fire-and-forget: apply the current batch every iteration (idempotent
     * per index; the MD advances the index each frame). No ack — the MD
     * overwrites COMM0 freely. */
    /* 32X CRAM can only be written during VBLANK — writes during active
     * display are dropped. Apply each batch inside the VBLANK window. */
    for (;;) {
        while (!(MARS_VDP_FBCTL & MARS_VDP_VBLK))
            ;                                    /* wait for VBLANK */
        uint16_t c0 = MARS_SYS_COMM0;
        if (c0 & 0x8000) {
            int start = c0 & 0xFF;
            cram[(start + 0) & 0xFF] = s16_to_mars(MARS_SYS_COMM2);
            cram[(start + 1) & 0xFF] = s16_to_mars(MARS_SYS_COMM4);
            cram[(start + 2) & 0xFF] = s16_to_mars(MARS_SYS_COMM6);
            cram[(start + 3) & 0xFF] = s16_to_mars(MARS_SYS_COMM8);
            cram[(start + 4) & 0xFF] = s16_to_mars(MARS_SYS_COMM10);
        }
        while (MARS_VDP_FBCTL & MARS_VDP_VBLK)
            ;                                    /* wait out VBLANK (one apply/frame) */
    }
}
