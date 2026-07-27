#include "mars.h"

/* Stage C: render the System-16B scene — BG+FG tile layers, SPRITES, and
 * the text layer, with the live palette via dynamic CRAM allocation.
 *
 * Data paths into the SH-2:
 *   COMM0 = 0x8000|idx : palette batch, 5 words, entries idx..idx+4 of the
 *                        full 2048 (tiles/text 0-1023, sprites 1024-2047).
 *   COMM0 = 0x4000|idx : text batch, 5 text-RAM words at word index idx.
 *   COMM0 = 0x2000     : render window (MD dropped RV, raised FM). COMM2
 *                        holds the tile-bank-1 value (rom_5704 slot 1).
 *   Tilemap: the game's tile RAM is remapped to a FRAMEBUFFER staging area
 *   (MD 0x852000.. / SH-2 0x24012000.., byte offset 0x12000 in the access
 *   bank, past the 0x11A00 display image). Each render window the SH-2
 *   copies 2 of the 12 game-written 4KB pages into an SDRAM shadow (full
 *   refresh every 6 windows), then renders from the shadow so both
 *   framebuffers can be drawn regardless of the flip state.
 *   Sprites: sprite RAM is likewise staged in the framebuffer (MD
 *   0x85E000.. / SH-2 0x2401E000..) and read DIRECTLY during compose —
 *   compose happens before any flip, so no SDRAM copy is needed.
 *
 * CRAM allocation (unified prescan, master only, no cross-CPU races): the
 * master walks the visible tilemap windows and the sprite list at window
 * start and builds this frame's maps — tile colors get 8-pen groups
 * ascending from 8 (0-7 identity, shared with text exactly as they share
 * palette entries 0-63 on hardware), sprite colors get ALIGNED PAIRS of
 * groups (16 pens) descending from pair 15. CRAM is applied from the same
 * maps in the same window: no one-frame color lag. The slave reads the
 * maps from SDRAM after its own cache purge (they are complete before the
 * COMM4 go command).
 *
 * System-16B facts (MAME segaic16.cpp/sega16sp.cpp, verified vs reference):
 *   tile word: code = data & 0x1FFF, color = (data>>6) & 0x7F, bank slot =
 *   code>>12, banksize 0x1000, bank[0] always 0 (MCU writes slot 0 = 0).
 *   text word: code = data & 0x1FF, color = (data>>9) & 7, pen 0 clear.
 *   Latched regs (word idx in text RAM): pages 0x740+which, yscroll
 *   0x748+which, xscroll 0x74C+which; which: 0=FG, 1=BG.
 *   Raw pages word quadrants (1024x512 virtual, 16 pages): UL=(p>>4)&0xF,
 *   UR=p&0xF, LL=(p>>12)&0xF, LR=(p>>8)&0xF. Screen: vx=(sx-(0xC0-xsc))
 *   &0x3FF, vy=(sy-ysc)&0x1FF.
 *   Sprites (8 words): d0 = bottom<<8|top, d1 = xpos (0x1FF; screen x =
 *   raw-184), d2 = end(15)/hide(14)/hflip(8)/signed pitch(7-0), d3 = word
 *   addr in bank, d4 = bank(11-8, identity, %8)/prio(7-6)/color(5-0), d5 =
 *   vzoom(9-5)/hzoom(4-0). Rows: addr += pitch BEFORE each row; vzoom
 *   accumulates in bit 15 to skip rows; 4 nibbles/word MSB-first (LSB
 *   when flipped), pen 0/15 transparent, last nibble 15 ends the row.
 *   Palette entry = 1024 + color*16 + pen.
 *   Draw order: BG opaque, FG, SPRITES, TEXT (pen0-clear each).
 *
 * CPU split: fixed SCREEN row seam at y=112 for every layer (slave takes
 * 0-111). A tile-row-index split would drift with yscroll and let one
 * CPU's tiles overwrite the other's sprites near the seam.
 *
 * RV discipline: the COMM loop runs from SDRAM (.ramtext) and never touches
 * cart ROM while RV=1. The render path executes only inside the window
 * (RV=0), so it reads the 1MB chunky tile set and the 1MB sprite stream
 * straight from cart ROM. */

#define RAMCODE __attribute__((section(".ramtext")))

extern const uint8_t altbeast_tiles[];      /* 16384 tiles x 64B, cart ROM */
extern const uint16_t altbeast_sprites[];   /* 512K words BE, cart ROM */

/* SDRAM shadows. Uncached (0x26..) views for COMM/copy writes; the renderer
 * reads the cached (0x06..) views after a cache purge at window start (the
 * 68K is stalled during the window, so nothing mutates them under us). */
#define TILEMAP_U   ((volatile uint16_t *)0x26020000)   /* 16 pages x 2K words */
#define TILEMAP_C   ((const uint16_t *)0x06020000)
#define TEXT_U      ((volatile uint16_t *)0x26030000)   /* 2048 words */
#define TEXT_C      ((const uint16_t *)0x06030000)
#define PAL_U       ((volatile uint16_t *)0x26031000)   /* 2048 words */
#define PAL_C       ((const uint16_t *)0x06031000)

#define FB_STAGING  ((volatile uint16_t *)0x24012000)   /* game tile RAM */
#define FB_SPR      ((volatile uint16_t *)0x2401E000)   /* game sprite RAM */
#define NPAGES      12

/* This frame's color maps, built by the master's prescan, read by both. */
static uint8_t tile_grp[128];               /* s16 tile color -> 8-pen group */
static uint8_t spr_pair[64];                /* sprite color -> 16-pen pair */
static uint8_t bg0_grp;                     /* alias group for BG color 0:
                                             * pixel VALUE 0 is transparent-
                                             * to-MD on hardware/ares, so the
                                             * opaque BG must never emit it */

/* Composed frame, PADDED so scroll fine-offsets never need clipping: 8px on
 * every side; visible pixel (x,y) lives at [8+x + (8+y)*336]. Compose ONCE
 * per frame here (cached, write-through), then blit to BOTH framebuffers —
 * composing is the expensive half, blitting is cheap 32-bit streaming. */
#define SBUF_W 336
#define SBUF_H 240
static uint8_t sbuf[SBUF_W * SBUF_H] __attribute__((aligned(4)));

/* Screen-row clip per CPU (sbuf rows are screen+8). */
#define CLIP_LO(cpu) ((cpu) ? 0 : 112)
#define CLIP_HI(cpu) ((cpu) ? 112 : 224)

/* System-16 palette word  sBGR BBBB GGGG RRRR  ->  32X BGR555. */
static inline uint16_t s16_to_mars(uint16_t v)
{
    uint16_t r = ((v >> 12) & 0x01) | ((v << 1) & 0x1e);
    uint16_t g = ((v >> 13) & 0x01) | ((v >> 3) & 0x1e);
    uint16_t b = ((v >> 14) & 0x01) | ((v >> 7) & 0x1e);
    return (uint16_t)((b << 10) | (g << 5) | r);
}

/* Layer register fetch, shared by prescan and compose. */
typedef struct {
    uint8_t pq[4];                          /* [vy9*2+vx9] quadrant page */
    int vx0, vy0;                           /* virtual xy of screen (0,0) */
} layer_regs;

RAMCODE static void get_layer_regs(int which, layer_regs *lr)
{
    uint16_t pages = TEXT_C[0x740 + which];
    uint16_t ysc   = TEXT_C[0x748 + which] & 0x1FF;
    uint16_t xsc   = TEXT_C[0x74C + which] & 0x1FF;
    lr->pq[0] = (pages >> 4) & 0xF;
    lr->pq[1] = pages & 0xF;
    lr->pq[2] = (pages >> 12) & 0xF;
    lr->pq[3] = (pages >> 8) & 0xF;
    lr->vx0 = (0 - ((0xC0 - xsc) & 0x3FF)) & 0x3FF;
    lr->vy0 = (0 - ysc) & 0x1FF;
}

/* Master prescan: collect every color the frame will reference and assign
 * CRAM groups — tiles ascending from 8, sprite pairs descending from 15,
 * overflow clamps where they meet. Deterministic, once per frame. */
RAMCODE static void build_maps(void)
{
    uint8_t tused[128], sused[64];
    for (int i = 0; i < 128; i++) tused[i] = 0;
    for (int i = 0; i < 64; i++) sused[i] = 0;

    for (int which = 0; which < 2; which++) {
        layer_regs lr;
        get_layer_regs(which, &lr);
        int yf = lr.vy0 & 7;
        int nrows = yf ? 29 : 28;
        for (int r = 0; r < nrows; r++) {
            int vy = (lr.vy0 - yf + r * 8) & 0x1FF;
            int trow = (vy >> 3) & 0x1F;
            int qy = (vy & 0x100) ? 2 : 0;
            const uint16_t *pg;
            for (int c = 0; c <= 40; c++) {
                int vx = ((lr.vx0 & ~7) + c * 8) & 0x3FF;
                pg = TILEMAP_C + lr.pq[qy + ((vx >> 9) & 1)] * 0x800;
                tused[(pg[trow * 64 + ((vx >> 3) & 0x3F)] >> 6) & 0x7F] = 1;
            }
        }
    }
    for (int i = 0; i < 64; i++) {
        volatile uint16_t *d = FB_SPR + i * 8;
        uint16_t d2 = d[2];
        if (d2 & 0x8000)
            break;
        uint16_t d0 = d[0];
        if ((d2 & 0x4000) || (d0 & 0xFF) >= (d0 >> 8))
            continue;
        sused[d[4] & 0x3F] = 1;
    }

    for (int c = 0; c < 8; c++)
        tile_grp[c] = (uint8_t)c;           /* identity, shared with text */
    uint8_t next = 8;
    bg0_grp = next++;                       /* CRAM copy of entries 0-7 */
    for (int c = 8; c < 128; c++)
        tile_grp[c] = tused[c] ? (next < 32 ? next++ : 31) : 0xFF;
    int pair = 15;
    int floor_pair = (next + 1) >> 1;
    for (int sc = 0; sc < 64; sc++) {
        if (!sused[sc]) {
            spr_pair[sc] = 0xFF;
            continue;
        }
        spr_pair[sc] = (uint8_t)(pair >= floor_pair ? pair : floor_pair);
        if (pair >= floor_pair)
            pair--;
    }
}

/* CRAM from this frame's maps (window start, still vblank). */
RAMCODE static void apply_cram(void)
{
    volatile uint16_t *cram = &MARS_CRAM;
    for (int c = 0; c < 128; c++) {
        uint8_t g = tile_grp[c];
        if (g == 0xFF)
            continue;
        const uint16_t *src = PAL_C + c * 8;
        volatile uint16_t *dst = cram + g * 8;
        for (int p = 0; p < 8; p++)
            dst[p] = s16_to_mars(src[p]);
    }
    for (int p = 0; p < 8; p++)             /* BG color-0 alias group */
        cram[bg0_grp * 8 + p] = s16_to_mars(PAL_C[p]);
    for (int sc = 0; sc < 64; sc++) {
        uint8_t pr = spr_pair[sc];
        if (pr == 0xFF)
            continue;
        const uint16_t *src = PAL_C + 1024 + sc * 16;
        volatile uint16_t *dst = cram + pr * 16;
        for (int p = 0; p < 16; p++)
            dst[p] = s16_to_mars(src[p]);
    }
}

/* Compose one tile layer into sbuf, tile-major (each tilemap word fetched
 * once, then up to 64 pixels drawn). opaque=1: BG; opaque=0: FG (pen 0
 * clear). Rows clip to the CPU's fixed screen seam. */
RAMCODE static void compose_layer(int cpu, int which, int opaque, uint16_t bank1)
{
    layer_regs lr;
    get_layer_regs(which, &lr);
    int xf = lr.vx0 & 7, yf = lr.vy0 & 7;
    int nrows = yf ? 29 : 28;
    int blo = 8 + CLIP_LO(cpu), bhi = 8 + CLIP_HI(cpu);

    for (int r = 0; r < nrows; r++) {
        int by = 8 - yf + r * 8;                    /* buffer y of tile row top */
        int l0 = blo - by, l1 = bhi - by;
        if (l0 < 0) l0 = 0;
        if (l1 > 8) l1 = 8;
        if (l0 >= l1)
            continue;
        int vy = (lr.vy0 - yf + r * 8) & 0x1FF;
        int trow = (vy >> 3) & 0x1F;
        int qy = (vy & 0x100) ? 2 : 0;
        uint8_t *drow = sbuf + (by + l0) * SBUF_W + (8 - xf);
        for (int c = 0; c <= 40; c++) {             /* 41 tiles cover 320+xf px */
            int vx = ((lr.vx0 & ~7) + c * 8) & 0x3FF;
            uint16_t w = TILEMAP_C[lr.pq[qy + ((vx >> 9) & 1)] * 0x800
                                   + trow * 64 + ((vx >> 3) & 0x3F)];
            uint8_t *dst = drow + c * 8;
            if (w == 0 && !opaque)                  /* tile 0 is blank */
                continue;
            unsigned code = w & 0x1FFF;
            if (code & 0x1000)
                code = (code & 0xFFF) + bank1 * 0x1000u;
            const uint8_t *tp = altbeast_tiles + code * 64 + l0 * 8;
            uint8_t g = tile_grp[(w >> 6) & 0x7F];
            if (g == 0xFF)
                g = 31;
            else if (g == 0 && opaque)
                g = bg0_grp;                        /* never emit pixel value 0 */
            uint8_t base = (uint8_t)(g << 3);
            for (int y = l0; y < l1; y++) {
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

/* Sprites from the FB staging list (read in place; compose precedes any
 * flip). Faithful to sega16sp.cpp sega_sys16b_sprite_device::draw. */
RAMCODE static void compose_sprites(int cpu)
{
    int ymin = CLIP_LO(cpu), ymax = CLIP_HI(cpu);

    for (int i = 0; i < 64; i++) {
        volatile uint16_t *e = FB_SPR + i * 8;
        uint16_t d2 = e[2];
        if (d2 & 0x8000)
            break;                                  /* end of list */
        uint16_t d0 = e[0];
        int top = d0 & 0xFF, bottom = d0 >> 8;
        if ((d2 & 0x4000) || top >= bottom)
            continue;                               /* hidden / degenerate */
        int xpos = e[1] & 0x1FF;
        int flip = d2 & 0x100;
        int pitch = (int8_t)(d2 & 0xFF);
        uint16_t addr = e[3];
        uint16_t d4 = e[4], d5 = e[5];
        const uint16_t *sd = altbeast_sprites + (((d4 >> 8) & 0xF) & 7) * 0x10000;
        uint8_t pr = spr_pair[d4 & 0x3F];
        uint8_t base = (uint8_t)((pr == 0xFF ? 15 : pr) << 4);
        int vzoom = (d5 >> 5) & 0x1F, hzoom = d5 & 0x1F;
        uint16_t yacc = 0;

        for (int y = top; y < bottom; y++) {
            addr = (uint16_t)(addr + pitch);        /* pre-advance, hw order */
            yacc = (uint16_t)(yacc + (vzoom << 10));
            if (yacc & 0x8000) {                    /* vzoom skips a row */
                addr = (uint16_t)(addr + pitch);
                yacc &= 0x7FFF;
            }
            if (y < ymin || y >= ymax)
                continue;                           /* other CPU / offscreen */

            uint8_t *row = sbuf + (8 + y) * SBUF_W + 8;
            int xacc = 4 * hzoom;
            int x = xpos;
            uint16_t o = addr;
            int pix = 0;
            while (((xpos - x) & 0x1FF) != 1) {
                uint16_t w = sd[o];
                o = (uint16_t)(flip ? o - 1 : o + 1);
                for (int n = 0; n < 4; n++) {
                    pix = flip ? ((w >> (4 * n)) & 0xF)
                               : ((w >> (12 - 4 * n)) & 0xF);
                    xacc = (xacc & 0x3F) + hzoom;
                    if (xacc < 0x40) {
                        unsigned sx = (unsigned)(x - 184);  /* screen x */
                        if (sx < 320 && pix != 0 && pix != 15)
                            row[sx] = (uint8_t)(base + pix);
                        x++;
                        if (((xpos - x) & 0x1FF) == 1)
                            break;
                    }
                }
                if (pix == 15)
                    break;                          /* row terminator */
            }
        }
    }
}

/* Text layer on top: screen-aligned, cols 24..63 = the visible 320px.
 * Pen 0 transparent; colors 0-7 are the identity CRAM groups. Row seam at
 * 14 aligns exactly with the pixel seam (14*8 = 112). */
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
 * and the master-built color maps through its own cache; write-through
 * keeps SDRAM true), compose the top half. Called from s_main on the
 * 0xC000 COMM4 command — the maps are complete before the command. */
RAMCODE void slave_render(uint16_t bank1)
{
    *(volatile uint8_t *)0xFFFFFE92 = SH2_CCTL_CP | SH2_CCTL_CE;
    compose_layer(1, 1, 1, bank1);               /* BG opaque, rows 0-111 */
    compose_layer(1, 0, 0, bank1);               /* FG transparent */
    compose_sprites(1);
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
    for (int i = 0; i < 2048; i++)
        PAL_U[i] = 0;
    for (int i = 0; i < 128; i++)
        tile_grp[i] = (uint8_t)(i < 8 ? i : 0xFF);
    for (int i = 0; i < 64; i++)
        spr_pair[i] = 0xFF;

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

            /* 3. This frame's color maps (prescan) + CRAM, then compose —
             * split across both SH-2s — and blit to BOTH buffers with an
             * EVEN number of flips so the staging bank stays stable. */
            build_maps();
            apply_cram();
            MARS_SYS_COMM6 = 0;                  /* clear stale stream data */
            MARS_SYS_COMM4 = 0xC000 | bank1;     /* slave: go (maps ready) */
            compose_layer(0, 1, 1, bank1);       /* BG opaque, rows 112-223 */
            compose_layer(0, 0, 0, bank1);       /* FG transparent */
            compose_sprites(0);
            compose_text(0);
            while (MARS_SYS_COMM6 != 0xD0) ;     /* slave half done */
            MARS_SYS_COMM4 = 0;                  /* slave: idle (it clears COMM6) */
            blit_frame();
            MARS_VDP_FBCTL = (MARS_VDP_FBCTL & MARS_VDP_FS) ^ 1;
            blit_frame();
            MARS_VDP_FBCTL = (MARS_VDP_FBCTL & MARS_VDP_FS) ^ 1;

            MARS_SYS_COMM0 = 0;                  /* ack: MD restores FM/RV */
        } else if (c0 & 0x8000) {                /* palette batch (RV=1 ok) */
            int idx = c0 & 0x7FF;
            PAL_U[(idx + 0) & 0x7FF] = MARS_SYS_COMM2;
            PAL_U[(idx + 1) & 0x7FF] = MARS_SYS_COMM4;
            PAL_U[(idx + 2) & 0x7FF] = MARS_SYS_COMM6;
            PAL_U[(idx + 3) & 0x7FF] = MARS_SYS_COMM8;
            PAL_U[(idx + 4) & 0x7FF] = MARS_SYS_COMM10;
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
