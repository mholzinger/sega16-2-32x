#include "mars.h"

/* Stage C (step 3): render the System-16B BACKGROUND (BG+FG tile layers)
 * and the text layer, with the live palette via dynamic CRAM allocation.
 *
 * Data paths into the SH-2:
 *   COMM0 = 0x8000|idx : palette batch, 5 words, entries idx..idx+4 of the
 *                        first 1024 (tiles use colors 0..127 -> entries to
 *                        1023). idx cycles 0..1023.
 *   COMM0 = 0x4000|idx : text batch, 5 text-RAM words at word index idx.
 *   COMM0 = 0x2000     : render window (MD dropped RV, raised FM). COMM2
 *                        holds the tile-bank-1 value (rom_5704 slot 1).
 *   Tilemap: the game's tile RAM is remapped to a FRAMEBUFFER staging area
 *   (MD 0x852000.. / SH-2 0x24012000.., byte offset 0x12000 in the access
 *   bank, past the 0x11A00 display image). Each render window the SH-2
 *   copies 2 of the 12 game-written 4KB pages into an SDRAM shadow (full
 *   refresh every 6 windows), then renders from the shadow so both
 *   framebuffers can be drawn regardless of the flip state.
 *
 * Dynamic CRAM allocation: a scene references up to ~30 distinct tile
 * colors (0..127) but 256-color mode has only 32 groups of 8 pens.
 * alloc_map[] assigns each referenced color a group lazily during compose;
 * groups 0-7 are identity (text colors and tile colors 0-7 share them,
 * exactly as they share palette entries 0-63 on real hardware). CRAM is
 * written at the START of the window from the PREVIOUS frame's map (still
 * inside vblank; identical in steady state), then the map is rebuilt.
 *
 * System-16B facts (MAME segaic16.cpp, verified against a reference run):
 *   tile word: code = data & 0x1FFF, color = (data>>6) & 0x7F, bank slot =
 *   code>>12, banksize 0x1000, bank[0]=0 always (MCU writes slot 0 = 0).
 *   text word: code = data & 0x1FF, color = (data>>9) & 7, pen 0 clear.
 *   Latched regs (word idx in text RAM): pages 0x740+which, yscroll
 *   0x748+which, xscroll 0x74C+which; which: 0=FG, 1=BG.
 *   Raw pages word quadrants (1024x512 virtual): UL=(p>>4)&7, UR=p&7,
 *   LL=(p>>12)&7, LR=(p>>8)&7. Screen: vx=(sx-(0xC0-xsc))&0x3FF,
 *   vy=(sy-ysc)&0x1FF. Draw order: BG opaque, FG, TEXT (pen0 clear).
 *
 * RV discipline: the COMM loop runs from SDRAM (.ramtext) and never touches
 * cart ROM while RV=1. The render path executes only inside the window
 * (RV=0), so it may read the full 1MB chunky tile set straight from cart
 * ROM (altbeast_tiles) — no SDRAM tile cache needed. */

#define RAMCODE __attribute__((section(".ramtext")))

extern const uint8_t altbeast_tiles[];      /* 16384 tiles x 64B, cart ROM */

/* SDRAM shadows. Uncached (0x26..) views for COMM/copy writes; the renderer
 * reads the cached (0x06..) views after a cache purge at window start (the
 * 68K is stalled during the window, so nothing mutates them under us). */
#define TILEMAP_U   ((volatile uint16_t *)0x26020000)   /* 12 pages x 2K words */
#define TILEMAP_C   ((const uint16_t *)0x06020000)
#define TEXT_U      ((volatile uint16_t *)0x26030000)   /* 2048 words */
#define TEXT_C      ((const uint16_t *)0x06030000)
#define PAL_U       ((volatile uint16_t *)0x26031000)   /* 1024 words */
#define PAL_C       ((const uint16_t *)0x06031000)

#define FB_STAGING  ((volatile uint16_t *)0x24012000)   /* game tile RAM */
#define NPAGES      12

/* Per-CPU color allocation: the compose is split across both SH-2s, so each
 * allocates from its own group range (master 20..31, slave 8..19) into its
 * own map — no cross-CPU races. A color used by both halves burns a group
 * in each range; correct, just slightly wasteful. Groups 0-7 identity. */
static uint8_t alloc_map[2][128];           /* [cpu][s16 color] -> CRAM group */
static uint8_t next_group[2];
static const uint8_t group_base[2] = { 20, 8 };
static const uint8_t group_cap[2]  = { 32, 20 };

/* Composed frame, PADDED so scroll fine-offsets never need clipping: 8px on
 * every side; visible pixel (x,y) lives at [8+x + (8+y)*336]. Compose ONCE
 * per frame here (cached, write-through), then blit to BOTH framebuffers —
 * composing is the expensive half, blitting is cheap 32-bit streaming. */
#define SBUF_W 336
#define SBUF_H 240
static uint8_t sbuf[SBUF_W * SBUF_H] __attribute__((aligned(4)));

/* System-16 palette word  sBGR BBBB GGGG RRRR  ->  32X BGR555. */
static inline uint16_t s16_to_mars(uint16_t v)
{
    uint16_t r = ((v >> 12) & 0x01) | ((v << 1) & 0x1e);
    uint16_t g = ((v >> 13) & 0x01) | ((v >> 3) & 0x1e);
    uint16_t b = ((v >> 14) & 0x01) | ((v >> 7) & 0x1e);
    return (uint16_t)((b << 10) | (g << 5) | r);
}

static inline uint8_t grp(int cpu, uint8_t color)
{
    uint8_t g = alloc_map[cpu][color];
    if (g == 0xFF) {
        g = (next_group[cpu] < group_cap[cpu])
            ? next_group[cpu]++
            : (uint8_t)(group_cap[cpu] - 1);        /* overflow -> last group */
        alloc_map[cpu][color] = g;
    }
    return g;
}

RAMCODE static void alloc_reset(int cpu)
{
    for (int i = 8; i < 128; i++)
        alloc_map[cpu][i] = 0xFF;
    for (int i = 0; i < 8; i++)
        alloc_map[cpu][i] = (uint8_t)i;             /* identity: shared w/ text */
    next_group[cpu] = group_base[cpu];
}

/* CRAM from the previous frame's allocations, both CPUs' maps (called at
 * window start, still in vblank; the master's post-purge cache sees the
 * slave's write-through map). Steady state: identical mapping. */
RAMCODE static void apply_cram(void)
{
    volatile uint16_t *cram = &MARS_CRAM;
    for (int cpu = 0; cpu < 2; cpu++) {
        for (int c = 0; c < 128; c++) {
            uint8_t g = alloc_map[cpu][c];
            if (g == 0xFF)
                continue;
            const uint16_t *src = PAL_C + c * 8;
            volatile uint16_t *dst = cram + g * 8;
            for (int p = 0; p < 8; p++)
                dst[p] = s16_to_mars(src[p]);
        }
    }
}

/* Compose one tile layer into sbuf, tile-major (each tilemap word fetched
 * once, then 64 pixels drawn). opaque=1: BG; opaque=0: FG (pen 0 clear).
 * cpu selects the tile-row split: slave (1) rows [0,14), master (0) the
 * rest — both SH-2s compose their half of the same frame concurrently. */
RAMCODE static void compose_layer(int cpu, int which, int opaque, uint16_t bank1)
{
    uint16_t pages = TEXT_C[0x740 + which];
    uint16_t ysc   = TEXT_C[0x748 + which] & 0x1FF;
    uint16_t xsc   = TEXT_C[0x74C + which] & 0x1FF;
    uint16_t effx  = (uint16_t)((0xC0 - xsc) & 0x3FF);
    uint8_t pq[4];                                  /* [vy9][vx9] quadrant page */
    pq[0] = (pages >> 4) & 0xF;                     /* UL */
    pq[1] = pages & 0xF;                            /* UR */
    pq[2] = (pages >> 12) & 0xF;                    /* LL */
    pq[3] = (pages >> 8) & 0xF;                     /* LR */

    int vx0 = (0 - effx) & 0x3FF;                   /* virtual xy of screen 0,0 */
    int vy0 = (0 - ysc) & 0x1FF;
    int xf = vx0 & 7, yf = vy0 & 7;
    int nrows = yf ? 29 : 28;
    int r0 = cpu ? 0 : 14;
    int r1 = cpu ? 14 : nrows;

    for (int r = r0; r < r1; r++) {
        int vy = (vy0 - yf + r * 8) & 0x1FF;        /* aligned tile row */
        int trow = (vy >> 3) & 0x1F;
        int qy = (vy & 0x100) ? 2 : 0;
        uint8_t *drow = sbuf + (8 - yf + r * 8) * SBUF_W + (8 - xf);
        for (int c = 0; c <= 40; c++) {             /* 41 tiles cover 320+xf px */
            int vx = (vx0 - xf + c * 8) & 0x3FF;
            uint16_t w = TILEMAP_C[pq[qy + ((vx >> 9) & 1)] * 0x800
                                   + trow * 64 + ((vx >> 3) & 0x3F)];
            uint8_t *dst = drow + c * 8;
            if (w == 0 && !opaque)                  /* tile 0 is blank */
                continue;
            unsigned code = w & 0x1FFF;
            if (code & 0x1000)
                code = (code & 0xFFF) + bank1 * 0x1000u;
            const uint8_t *tp = altbeast_tiles + code * 64;
            uint8_t base = (uint8_t)(grp(cpu, (w >> 6) & 0x7F) << 3);
            for (int y = 0; y < 8; y++) {
                if (opaque) {
                    dst[0] = (uint8_t)(base + tp[0]);
                    dst[1] = (uint8_t)(base + tp[1]);
                    dst[2] = (uint8_t)(base + tp[2]);
                    dst[3] = (uint8_t)(base + tp[3]);
                    dst[4] = (uint8_t)(base + tp[4]);
                    dst[5] = (uint8_t)(base + tp[5]);
                    dst[6] = (uint8_t)(base + tp[6]);
                    dst[7] = (uint8_t)(base + tp[7]);
                } else {
                    if (tp[0]) dst[0] = (uint8_t)(base + tp[0]);
                    if (tp[1]) dst[1] = (uint8_t)(base + tp[1]);
                    if (tp[2]) dst[2] = (uint8_t)(base + tp[2]);
                    if (tp[3]) dst[3] = (uint8_t)(base + tp[3]);
                    if (tp[4]) dst[4] = (uint8_t)(base + tp[4]);
                    if (tp[5]) dst[5] = (uint8_t)(base + tp[5]);
                    if (tp[6]) dst[6] = (uint8_t)(base + tp[6]);
                    if (tp[7]) dst[7] = (uint8_t)(base + tp[7]);
                }
                tp += 8;
                dst += SBUF_W;
            }
        }
    }
}

/* Text layer on top: screen-aligned, cols 24..63 = the visible 320px.
 * Pen 0 transparent; colors 0-7 are the identity CRAM groups. */
RAMCODE static void compose_text(int cpu)
{
    int row0 = cpu ? 0 : 14, row1 = cpu ? 14 : 28;
    for (int row = row0; row < row1; row++) {
        for (int col = 24; col < 64; col++) {
            uint16_t d = TEXT_C[row * 64 + col];
            unsigned code = d & 0x1FF;
            if (code == 0 && !(d & 0x0E00))
                continue;                           /* blank glyph fast path */
            const uint8_t *tp = altbeast_tiles + code * 64;
            uint8_t base = (uint8_t)(((d >> 9) & 7) << 3);
            uint8_t *dst = sbuf + (8 + row * 8) * SBUF_W + 8 + (col - 24) * 8;
            for (int y = 0; y < 8; y++) {
                if (tp[0]) dst[0] = (uint8_t)(base + tp[0]);
                if (tp[1]) dst[1] = (uint8_t)(base + tp[1]);
                if (tp[2]) dst[2] = (uint8_t)(base + tp[2]);
                if (tp[3]) dst[3] = (uint8_t)(base + tp[3]);
                if (tp[4]) dst[4] = (uint8_t)(base + tp[4]);
                if (tp[5]) dst[5] = (uint8_t)(base + tp[5]);
                if (tp[6]) dst[6] = (uint8_t)(base + tp[6]);
                if (tp[7]) dst[7] = (uint8_t)(base + tp[7]);
                tp += 8;
                dst += SBUF_W;
            }
        }
    }
}

/* Slave's half of the render window: purge its cache (it reads the shadows
 * and writes sbuf through its own cache; write-through keeps SDRAM true),
 * compose the top half. Called from s_main on the 0xC000 COMM4 command. */
RAMCODE void slave_render(uint16_t bank1)
{
    *(volatile uint8_t *)0xFFFFFE92 = SH2_CCTL_CP | SH2_CCTL_CE;
    alloc_reset(1);
    compose_layer(1, 1, 1, bank1);               /* BG opaque, rows 0..13 */
    compose_layer(1, 0, 0, bank1);               /* FG transparent */
    compose_text(1);
}

/* Stream the composed frame to the current access framebuffer, 32-bit. */
RAMCODE static void blit_frame(void)
{
    for (int y = 0; y < 224; y++) {
        const uint32_t *src = (const uint32_t *)(sbuf + (8 + y) * SBUF_W + 8);
        volatile uint32_t *dst = (volatile uint32_t *)
            ((uintptr_t)&MARS_FRAMEBUFFER + 0x200 + y * 320);
        for (int i = 0; i < 80; i += 8) {
            dst[i + 0] = src[i + 0];
            dst[i + 1] = src[i + 1];
            dst[i + 2] = src[i + 2];
            dst[i + 3] = src[i + 3];
            dst[i + 4] = src[i + 4];
            dst[i + 5] = src[i + 5];
            dst[i + 6] = src[i + 6];
            dst[i + 7] = src[i + 7];
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

    for (int i = 0; i < 16 * 0x800; i++)            /* all 16 selectable pages:
                                                     * 12-15 stay zero (blank) */
        TILEMAP_U[i] = 0;
    for (int i = 0; i < 2048; i++)
        TEXT_U[i] = 0;
    for (int i = 0; i < 1024; i++)
        PAL_U[i] = 0;
    alloc_reset(0);
    alloc_reset(1);

    volatile uint16_t *cram = &MARS_CRAM;
    for (int i = 0; i < 256; i++)
        cram[i] = 0;

    /* Master is SDRAM-resident from here on: the MD may set RV=1 now. */
    MARS_SYS_COMM14 = 0x600D;

    uint16_t copy_rotor = 0;

    for (;;) {
        uint16_t c0 = MARS_SYS_COMM0;
        if (c0 == 0x2000) {                      /* RENDER window (RV=0, FM=1) */
            uint16_t bank1 = MARS_SYS_COMM2 & 7;

            /* 1. Tilemap page copy — BEFORE any flip, while the access bank
             * is still the one the game stages into. 32-bit streaming. */
            for (int n = 0; n < 2; n++) {
                volatile uint32_t *src = (volatile uint32_t *)(FB_STAGING + copy_rotor * 0x800);
                volatile uint32_t *dst = (volatile uint32_t *)(TILEMAP_U + copy_rotor * 0x800);
                for (int i = 0; i < 0x400; i += 8) {
                    dst[i + 0] = src[i + 0];
                    dst[i + 1] = src[i + 1];
                    dst[i + 2] = src[i + 2];
                    dst[i + 3] = src[i + 3];
                    dst[i + 4] = src[i + 4];
                    dst[i + 5] = src[i + 5];
                    dst[i + 6] = src[i + 6];
                    dst[i + 7] = src[i + 7];
                }
                copy_rotor++;
                if (copy_rotor >= NPAGES)
                    copy_rotor = 0;
            }

            /* 2. Purge the cache: the renderer reads the shadows through the
             * cached mirror, and the COMM loop/copy wrote them uncached. */
            *(volatile uint8_t *)0xFFFFFE92 = SH2_CCTL_CP | SH2_CCTL_CE;

            /* 3. Colors for the incoming frame from last frame's maps (still
             * vblank), then rebuild while rendering. */
            apply_cram();
            alloc_reset(0);

            /* 4. Compose the frame ONCE — split across both SH-2s (slave
             * takes the top half via COMM4) — then blit it to BOTH buffers
             * (flip timing is emulator-divergent) with an EVEN number of
             * flips so the access bank the game stages into stays stable. */
            MARS_SYS_COMM6 = 0;                  /* clear stale stream data */
            MARS_SYS_COMM4 = 0xC000 | bank1;     /* slave: go */
            compose_layer(0, 1, 1, bank1);       /* BG opaque, rows 14.. */
            compose_layer(0, 0, 0, bank1);       /* FG transparent */
            compose_text(0);
            while (MARS_SYS_COMM6 != 0xD0) ;     /* slave half done */
            MARS_SYS_COMM4 = 0;                  /* slave: idle (it clears COMM6) */
            blit_frame();
            MARS_VDP_FBCTL = (MARS_VDP_FBCTL & MARS_VDP_FS) ^ 1;
            blit_frame();
            MARS_VDP_FBCTL = (MARS_VDP_FBCTL & MARS_VDP_FS) ^ 1;

            MARS_SYS_COMM0 = 0;                  /* ack: MD restores FM/RV */
        } else if (c0 & 0x8000) {                /* palette batch (RV=1 ok) */
            int idx = c0 & 0x3FF;
            PAL_U[(idx + 0) & 0x3FF] = MARS_SYS_COMM2;
            PAL_U[(idx + 1) & 0x3FF] = MARS_SYS_COMM4;
            PAL_U[(idx + 2) & 0x3FF] = MARS_SYS_COMM6;
            PAL_U[(idx + 3) & 0x3FF] = MARS_SYS_COMM8;
            PAL_U[(idx + 4) & 0x3FF] = MARS_SYS_COMM10;
            MARS_SYS_COMM0 = 0;                  /* ack */
        } else if (c0 & 0x4000) {                /* text batch (RV=1 ok) */
            int idx = c0 & 0x7FF;
            uint16_t w0 = MARS_SYS_COMM2, w1 = MARS_SYS_COMM4, w2 = MARS_SYS_COMM6;
            uint16_t w3 = MARS_SYS_COMM8, w4 = MARS_SYS_COMM10;
            if (idx + 4 < 2048) {
                TEXT_U[idx + 0] = w0;
                TEXT_U[idx + 1] = w1;
                TEXT_U[idx + 2] = w2;
                TEXT_U[idx + 3] = w3;
                TEXT_U[idx + 4] = w4;
            }
            MARS_SYS_COMM0 = 0;                  /* ack */
        }
    }
}
