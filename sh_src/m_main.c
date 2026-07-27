#include "mars.h"

/* Stage C (step 2): render the game's TEXT LAYER with its live palette.
 *
 * The MD shim streams two shadow regions over an acked COMM protocol:
 *   COMM0 = 0x8000|idx : palette batch, 5 System-16 words in COMM2..COMM10,
 *                        entries idx..idx+4 (idx cycles 0..255)
 *   COMM0 = 0x4000|idx : text batch, 5 text-RAM words at word index idx
 *                        (idx 0..2047, 4KB text RAM)
 * The SH-2 stores both into SDRAM shadows and clears COMM0 as the ack.
 *
 * Each vblank (68K heartbeat on COMM12): apply the full palette shadow to
 * CRAM (writable during vblank), flip framebuffers (immediate in vblank),
 * then render the 40x28 visible text window into the fresh back buffer.
 *
 * System-16B text layer (MAME segaic16.cpp tilemap_16b_text_info):
 *   tile code = data & 0x1FF (bank 0 at boot), colour = (data>>9)&7,
 *   palette entry = colour*8 + pen (3bpp), pen 0 transparent (black here).
 *   Name table 64x28; columns 24..63 are the visible 320px (scrolldx -192).
 *
 * RV discipline: everything after the 0x600D handshake runs from SDRAM
 * (.ramtext) and never touches cart ROM. Font tiles are copied to SDRAM
 * during init, before the MD sets RV=1. */

#define RAMCODE __attribute__((section(".ramtext")))

extern const uint8_t altbeast_tiles[];      /* chunky 8bpp, ROM (init only) */

#define SDRAM_TILES ((uint8_t *)0x06020000)          /* 512 tiles x 64B     */
#define TEXT_SHADOW ((volatile uint16_t *)0x26030000)/* 2048 words, uncached*/
#define PAL_SHADOW  ((volatile uint16_t *)0x26031000)/* 256 words, uncached */

/* System-16 palette word  sBGR BBBB GGGG RRRR  ->  32X BGR555. */
static inline uint16_t s16_to_mars(uint16_t v)
{
    uint16_t r = ((v >> 12) & 0x01) | ((v << 1) & 0x1e);
    uint16_t g = ((v >> 13) & 0x01) | ((v >> 3) & 0x1e);
    uint16_t b = ((v >> 14) & 0x01) | ((v >> 7) & 0x1e);
    return (uint16_t)((b << 10) | (g << 5) | r);
}

RAMCODE static void render_text(void)
{
    volatile uint16_t *fb = &MARS_FRAMEBUFFER;
    for (int row = 0; row < 28; row++) {
        for (int col = 24; col < 64; col++) {
            uint16_t d = TEXT_SHADOW[row * 64 + col];
            const uint8_t *tp = SDRAM_TILES + (d & 0x1FF) * 64;
            uint8_t base = (uint8_t)(((d >> 9) & 7) << 3);
            volatile uint16_t *dst = fb + 0x100 + (row * 8) * 160 + (col - 24) * 4;
            for (int y = 0; y < 8; y++) {
                for (int xw = 0; xw < 4; xw++) {
                    uint8_t p0 = tp[xw * 2], p1 = tp[xw * 2 + 1];
                    if (p0) p0 += base;
                    if (p1) p1 += base;
                    dst[xw] = ((uint16_t)p0 << 8) | p1;
                }
                tp += 8;
                dst += 160;
            }
        }
    }
}

RAMCODE void m_main(void)
{
    /* Release the secondary SH-2 from its S_OK wait. */
    MARS_SYS_COMM4 = 0;

    Hw32xInit(MARS_VDP_MODE_256, 0);

    /* 32X layer above the game's (unused) MD VDP layer. */
    MARS_VDP_DISPMODE = MARS_NTSC_FORMAT | MARS_224_LINES | MARS_VDP_PRIO_32X | MARS_VDP_MODE_256;

    /* Init-time work that may still touch cart ROM (RV=0 until we signal):
     * font tiles to SDRAM, shadows cleared, CRAM dark. */
    for (int i = 0; i < 512 * 64; i++)
        SDRAM_TILES[i] = altbeast_tiles[i];
    for (int i = 0; i < 2048; i++)
        TEXT_SHADOW[i] = 0;
    for (int i = 0; i < 256; i++)
        PAL_SHADOW[i] = 0;

    volatile uint16_t *cram = &MARS_CRAM;
    for (int i = 0; i < 256; i++)
        cram[i] = 0;

    /* Master is SDRAM-resident from here on: the MD may set RV=1 now. */
    MARS_SYS_COMM14 = 0x600D;

    for (;;) {
        uint16_t c0 = MARS_SYS_COMM0;
        if (c0 == 0x2000) {                      /* RENDER window (RV=0) */
            for (int i = 0; i < 256; i++)        /* apply live palette */
                cram[i] = s16_to_mars(PAL_SHADOW[i]);
            /* Draw BOTH buffers so the displayed one is always current
             * regardless of ares' flip timing (step-1 lesson). */
            render_text();
            MARS_VDP_FBCTL = (MARS_VDP_FBCTL & MARS_VDP_FS) ^ 1;
            render_text();
            MARS_SYS_COMM0 = 0;                  /* ack: MD may restore RV=1 */
        } else if (c0 & 0x8000) {                /* palette batch (RV=1 ok) */
            int idx = c0 & 0xFF;
            PAL_SHADOW[(idx + 0) & 0xFF] = MARS_SYS_COMM2;
            PAL_SHADOW[(idx + 1) & 0xFF] = MARS_SYS_COMM4;
            PAL_SHADOW[(idx + 2) & 0xFF] = MARS_SYS_COMM6;
            PAL_SHADOW[(idx + 3) & 0xFF] = MARS_SYS_COMM8;
            PAL_SHADOW[(idx + 4) & 0xFF] = MARS_SYS_COMM10;
            MARS_SYS_COMM0 = 0;                  /* ack */
        } else if (c0 & 0x4000) {                /* text batch (RV=1 ok) */
            int idx = c0 & 0x7FF;
            uint16_t w0 = MARS_SYS_COMM2, w1 = MARS_SYS_COMM4, w2 = MARS_SYS_COMM6;
            uint16_t w3 = MARS_SYS_COMM8, w4 = MARS_SYS_COMM10;
            if (idx + 4 < 2048) {
                TEXT_SHADOW[idx + 0] = w0;
                TEXT_SHADOW[idx + 1] = w1;
                TEXT_SHADOW[idx + 2] = w2;
                TEXT_SHADOW[idx + 3] = w3;
                TEXT_SHADOW[idx + 4] = w4;
            }
            MARS_SYS_COMM0 = 0;                  /* ack */
        }
    }
}
