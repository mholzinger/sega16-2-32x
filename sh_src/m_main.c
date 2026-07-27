#include "mars.h"

/* Stage C step 6: CONCURRENT TILE COMPOSE via an SDRAM tile cache.
 *
 * The hard 32X rule: while the game's 68K owns the cart (RV=1), the SH-2s
 * may not touch cart ROM — where the 2MB of tile/sprite pixel data lives.
 * Previously the whole compose therefore ran inside the render window
 * with the game frozen (~36ms/window in heavy scenes = 1/3 game speed).
 *
 * Now the BG/FG tile layers compose OUTSIDE the window, concurrently with
 * the game: everything they need is SH-2-legal at RV=1 — the SDRAM
 * shadows, the color maps, sbuf, and a 64KB SDRAM TILE CACHE (512 sets x
 * 2 ways x 64B). Cache misses render as a flat placeholder (the tile's
 * pen-0 color) and are queued; the master fills them from cart ROM inside
 * the next window (budgeted), so scenes converge in a few frames and
 * steady state runs miss-free. Sprites (large, dynamic frames) and text
 * still compose in-window, on top of the concurrently-composed tiles,
 * followed by the verified-flip dual blit.
 *
 * Division of labor per cycle:
 *   WINDOW N (RV=0, FM=1, 68K stalled): master: staging page copy, CRAM
 *   from maps[par], cache fills (both CPUs' miss queues), sprite+text
 *   bottom half, blits+flips; slave: sprite+text top half + prescan of
 *   maps[par^1] for the next frame.
 *   CONCURRENT N+1 (RV=1, game running): both CPUs compose BG/FG halves
 *   of the next frame from the cache; the slave also SERVICES THE MD
 *   STREAM (palette/text COMM batches) — the master is busy composing,
 *   and between strips the slave polls so the MD never stalls long.
 *
 * Master<->slave signaling moved OFF the COMM registers into SDRAM
 * mailboxes (SYNC): the MD stream owns COMM2..COMM10 at any moment the
 * game is running, so COMM4/COMM6 handshakes would race batch payloads.
 *
 * System-16B facts, staging layout, verified-flip discipline, pixel-
 * value-0 transparency, CRAM allocation: see NOTES.md (unchanged). */

#define RAMCODE __attribute__((section(".ramtext")))

extern const uint8_t altbeast_tiles[];      /* 16384 tiles x 64B, cart ROM */
extern const uint16_t altbeast_sprites[];   /* 512K words BE, cart ROM */

/* ---- SDRAM map (stacks: master grows down from 0x0603F000, slave from
 * 0x06040000; .bss ends well below 0x06018000 — checked per build) ----
 * Uncached (0x26..) views for cross-CPU/stream writes; cached (0x06..)
 * views for render reads after a purge. */
#define TILEMAP_U   ((volatile uint16_t *)0x26018000)   /* 13 pages x 2K words */
#define TILEMAP_C   ((const uint16_t *)0x06018000)      /* page 12 = blank */
#define TEXT_U      ((volatile uint16_t *)0x26025000)   /* 2048 words */
#define TEXT_C      ((const uint16_t *)0x06025000)
/* Palette: read straight from FB staging in-window — no shadow, no
 * stream (game palette writes are all word/long; zero-byte-drop safe). */
#define FB_PAL      ((volatile uint16_t *)0x2401F000)   /* 2048 words */
#define DIAG        ((volatile uint32_t *)0x26027000)   /* profiling, lua-read */
#define SYNC        ((volatile uint16_t *)0x26027800)   /* [0] cmd  [1] echo */
#define CACHE_C     ((uint8_t *)0x06028000)             /* 1024 slots x 64B */

#define FB_STAGING  ((volatile uint16_t *)0x24012000)   /* game tile RAM */
#define FB_SPR      ((volatile uint16_t *)0x2401E000)   /* game sprite RAM */
#define NPAGES      12

/* Slave commands (SYNC[0]; nonzero = pending; echo to SYNC[1] when done):
 * bits 15-12 opcode, bit 8 map parity, bits 2-0 tile bank 1. */
#define CMD_WIN     0x1000                              /* sprites+text half + prescan */
#define CMD_TILE    0x2000                              /* concurrent BG/FG half */

/* Tile cache bookkeeping (.bss, written master-only in-window). */
#define NSETS       512
static uint16_t cache_tag[NSETS * 2];       /* folded tile code; 0xFFFF empty */
static uint8_t cache_rot[NSETS];            /* round-robin eviction way */

/* Per-CPU miss queues: appended (write-through) during concurrent compose,
 * drained by the master in the next window. */
static uint16_t missq[2][256];
static volatile uint16_t miss_n[2];

/* Placeholder pixels for uncached tiles: all pen 0 -> the tile color's
 * base entry. Must be RAM (.bss), never ROM — it is read at RV=1. */
static uint8_t blank_tile[64] __attribute__((aligned(4)));

/* Color maps, double-buffered by window parity (slave prescans par^1
 * during window N; both CPUs compose frame N+1 from par^1 after the
 * toggle). */
static uint8_t tile_grp[2][128];
static uint8_t spr_pair[2][64];
#define BG0_GRP 8                           /* opaque BG must never emit
                                             * pixel value 0 (MD-through) */

#define SBUF_W 336
#define SBUF_H 240
static uint8_t sbuf[SBUF_W * SBUF_H] __attribute__((aligned(4)));

static inline uint16_t s16_to_mars(uint16_t vv)
{
    unsigned v = vv;                        /* unsigned shifts: no libgcc */
    uint16_t r = ((v >> 12) & 0x01) | ((v << 1) & 0x1e);
    uint16_t g = ((v >> 13) & 0x01) | ((v >> 3) & 0x1e);
    uint16_t b = ((v >> 14) & 0x01) | ((v >> 7) & 0x1e);
    return (uint16_t)((b << 10) | (g << 5) | r);
}

static inline uint16_t frt(void)
{
    uint8_t h = SH2_FRT_FRCH;
    uint8_t l = SH2_FRT_FRCL;
    return (uint16_t)((h << 8) | l);
}

static inline void diag_add(int slot, uint16_t t0)
{
    DIAG[slot] += (uint16_t)(frt() - t0);
}

static inline void cache_purge(void)
{
    *(volatile uint8_t *)0xFFFFFE92 = SH2_CCTL_CP | SH2_CCTL_CE;
}

/* Cache lookup during compose. Returns pixel pointer; on miss, queues the
 * folded code (dup-tolerant; fills re-check tags) and returns the blank
 * placeholder. always_inline: an outlined copy would land in .text (cart
 * ROM), and this runs at RV=1 where ROM fetch is forbidden. */
/* Set index: XOR-fold the high code bits so sequential art ranges (which
 * alias every 512 codes) spread across sets instead of thrashing a way. */
#define CACHE_SET(code) (((code) ^ ((code) >> 9)) & (NSETS - 1))

__attribute__((always_inline))
static inline const uint8_t *tile_pixels(unsigned code, int cpu)
{
    unsigned set = CACHE_SET(code);
    if (cache_tag[set * 2] == code)
        return CACHE_C + (set * 2) * 64;
    if (cache_tag[set * 2 + 1] == code)
        return CACHE_C + (set * 2 + 1) * 64;
    uint16_t n = miss_n[cpu];
    if (n < 256) {
        missq[cpu][n] = (uint16_t)code;
        miss_n[cpu] = (uint16_t)(n + 1);
    }
    return blank_tile;
}

typedef struct {
    uint8_t pq[4];
    int vx0, vy0;
} layer_regs;

/* Scroll/page registers are LATCHED once per window into this snapshot,
 * used by BOTH the prescan and the whole of the next frame's compose.
 * The game (running concurrently now) mutates the live regs continuously;
 * without the latch, compose sees rows/columns the prescan never colored
 * (garish placeholder tiles at the screen edges). Real System 16B latches
 * these at scanline 261 — this mirrors the hardware. */
static layer_regs snap[2];

RAMCODE static void latch_layer_regs(void)
{
    for (int which = 0; which < 2; which++) {
        layer_regs *lr = &snap[which];
        uint16_t pages = TEXT_C[0x740 + which];
        uint16_t ysc   = TEXT_C[0x748 + which] & 0x1FF;
        uint16_t xsc   = TEXT_C[0x74C + which] & 0x1FF;
        /* 16 selectable pages, but the game only writes 0-11; 12-15 all
         * map to the single blank page 12 of the shadow. */
        uint8_t p;
        p = (pages >> 4) & 0xF;  lr->pq[0] = p > 12 ? 12 : p;
        p = pages & 0xF;         lr->pq[1] = p > 12 ? 12 : p;
        p = (pages >> 12) & 0xF; lr->pq[2] = p > 12 ? 12 : p;
        p = (pages >> 8) & 0xF;  lr->pq[3] = p > 12 ? 12 : p;
        lr->vx0 = (0 - ((0xC0 - xsc) & 0x3FF)) & 0x3FF;
        lr->vy0 = (0 - ysc) & 0x1FF;
    }
}

/* Prescan (slave, in-window): visible tilemap + sprite list -> color
 * groups for the NEXT window's frame. Tiles ascend from BG0_GRP+1,
 * sprite pairs descend from 15. */
RAMCODE static void build_maps(int par, uint16_t bank1)
{
    uint8_t tused[128], sused[64];
    for (int i = 0; i < 128; i++) tused[i] = 0;
    for (int i = 0; i < 64; i++) sused[i] = 0;
    (void)bank1;

    for (int which = 0; which < 2; which++) {
        const layer_regs *lr = &snap[which];
        int yf = lr->vy0 & 7;
        int nrows = yf ? 29 : 28;
        for (int r = 0; r < nrows; r++) {
            int vy = (lr->vy0 - yf + r * 8) & 0x1FF;
            int trow = (int)(((unsigned)vy >> 3) & 0x1F);
            int qy = (int)(((unsigned)vy >> 7) & 2);
            for (int c = 0; c <= 41; c++) {      /* 42: match compose's spill */
                int vx = ((lr->vx0 & ~7) + c * 8) & 0x3FF;
                uint16_t w = TILEMAP_C[lr->pq[qy + (((unsigned)vx >> 9) & 1)] * 0x800
                                       + trow * 64 + (((unsigned)vx >> 3) & 0x3F)];
                tused[((unsigned)w >> 6) & 0x7F] = 1;
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
        tile_grp[par][c] = (uint8_t)c;
    uint8_t next = BG0_GRP + 1;
    for (int c = 8; c < 128; c++)
        tile_grp[par][c] = tused[c] ? (next < 32 ? next++ : 31) : 0xFF;
    int pair = 15;
    int floor_pair = (next + 1) >> 1;
    for (int sc = 0; sc < 64; sc++) {
        if (!sused[sc]) {
            spr_pair[par][sc] = 0xFF;
            continue;
        }
        spr_pair[par][sc] = (uint8_t)(pair >= floor_pair ? pair : floor_pair);
        if (pair >= floor_pair)
            pair--;
    }
}

RAMCODE static void apply_cram(int par)
{
    volatile uint16_t *cram = &MARS_CRAM;
    for (int c = 0; c < 128; c++) {
        uint8_t g = tile_grp[par][c];
        if (g == 0xFF)
            continue;
        volatile uint16_t *src = FB_PAL + c * 8;
        volatile uint16_t *dst = cram + g * 8;
        for (int p = 0; p < 8; p++)
            dst[p] = s16_to_mars(src[p]);
    }
    for (int p = 0; p < 8; p++)
        cram[BG0_GRP * 8 + p] = s16_to_mars(FB_PAL[p]);
    for (int sc = 0; sc < 64; sc++) {
        uint8_t pr = spr_pair[par][sc];
        if (pr == 0xFF)
            continue;
        volatile uint16_t *src = FB_PAL + 1024 + sc * 16;
        volatile uint16_t *dst = cram + pr * 16;
        for (int p = 0; p < 16; p++)
            dst[p] = s16_to_mars(src[p]);
    }
}

/* Compose one tile layer's SCREEN ROW RANGE [ylo,yhi) into sbuf from the
 * SDRAM cache (legal at RV=1). opaque=1: BG packed 32-bit path; 0: FG
 * byte path. Callers pick ranges: master (112,224), slave 16px strips. */
RAMCODE static void compose_layer(int ylo, int yhi, int cpu, int which,
                                  int opaque, uint16_t bank1, int par)
{
    const layer_regs lr = snap[which];       /* latched once per window */
    int xf = lr.vx0 & 7, yf = lr.vy0 & 7;
    int nrows = yf ? 29 : 28;
    int blo = 8 + ylo, bhi = 8 + yhi;
    const uint8_t *tg = tile_grp[par];
    const uint32_t s = (uint32_t)(xf * 8);

    const uint8_t *tptr[42];
    uint32_t tbase[42];

    for (int r = 0; r < nrows; r++) {
        int by = 8 - yf + r * 8;
        int l0 = blo - by, l1 = bhi - by;
        if (l0 < 0) l0 = 0;
        if (l1 > 8) l1 = 8;
        if (l0 >= l1)
            continue;
        int vy = (lr.vy0 - yf + r * 8) & 0x1FF;
        int trow = (int)(((unsigned)vy >> 3) & 0x1F);
        int qy = (int)(((unsigned)vy >> 7) & 2);

        if (opaque) {
            for (int c = 0; c <= 41; c++) {
                int vx = ((lr.vx0 & ~7) + c * 8) & 0x3FF;
                uint16_t w = TILEMAP_C[lr.pq[qy + (((unsigned)vx >> 9) & 1)] * 0x800
                                       + trow * 64 + (((unsigned)vx >> 3) & 0x3F)];
                unsigned code = w & 0x1FFF;
                if (code & 0x1000)
                    code = (code & 0xFFF) + bank1 * 0x1000u;
                tptr[c] = tile_pixels(code, cpu);
                uint8_t g = tg[((unsigned)w >> 6) & 0x7F];
                if (g == 0xFF)
                    g = 31;
                else if (g == 0)
                    g = BG0_GRP;
                tbase[c] = (uint32_t)(g << 3) * 0x01010101u;
            }
            /* CONSTANT-shift specialization per xf case. SH-2 has no
             * variable-shift instruction: `a << s` with runtime s becomes
             * a LIBGCC CALL resident in .text = CART ROM. This path runs
             * at RV=1 where SH-2 ROM fetch is forbidden (ares kills the
             * CPU; proven: slave PC sampled inside __ashrsi3 during the
             * rise-from-grave hang). Constant shifts compile to native
             * shll8/16-composed sequences — faster AND legal. */
#define MERGE_ROW(EXPR0, EXPR1)                                             \
                for (int c = 0; c <= 40; c++) {                             \
                    const uint32_t *tn = (const uint32_t *)(tptr[c + 1] + y * 8); \
                    uint32_t b0 = tn[0] + tbase[c + 1];                     \
                    uint32_t b1 = tn[1] + tbase[c + 1];                     \
                    dst[0] = (EXPR0);                                       \
                    dst[1] = (EXPR1);                                       \
                    dst += 2;                                               \
                    a0 = b0;                                                \
                    a1 = b1;                                                \
                    (void)a0; (void)b1;                                     \
                }
            for (int y = l0; y < l1; y++) {
                uint32_t *dst = (uint32_t *)(sbuf + (by + y) * SBUF_W + 8);
                const uint32_t *t0 = (const uint32_t *)(tptr[0] + y * 8);
                uint32_t a0 = t0[0] + tbase[0], a1 = t0[1] + tbase[0];
                switch (s) {
                case 0:  MERGE_ROW(a0, a1) break;
                case 8:  MERGE_ROW((a0 << 8) | (a1 >> 24), (a1 << 8) | (b0 >> 24)) break;
                case 16: MERGE_ROW((a0 << 16) | (a1 >> 16), (a1 << 16) | (b0 >> 16)) break;
                case 24: MERGE_ROW((a0 << 24) | (a1 >> 8), (a1 << 24) | (b0 >> 8)) break;
                case 32: MERGE_ROW(a1, b0) break;
                case 40: MERGE_ROW((a1 << 8) | (b0 >> 24), (b0 << 8) | (b1 >> 24)) break;
                case 48: MERGE_ROW((a1 << 16) | (b0 >> 16), (b0 << 16) | (b1 >> 16)) break;
                default: MERGE_ROW((a1 << 24) | (b0 >> 8), (b0 << 24) | (b1 >> 8)) break;
                }
            }
#undef MERGE_ROW
            continue;
        }

        /* FG byte path */
        uint8_t *drow = sbuf + (by + l0) * SBUF_W + (8 - xf);
        for (int c = 0; c <= 40; c++) {
            int vx = ((lr.vx0 & ~7) + c * 8) & 0x3FF;
            uint16_t w = TILEMAP_C[lr.pq[qy + (((unsigned)vx >> 9) & 1)] * 0x800
                                   + trow * 64 + (((unsigned)vx >> 3) & 0x3F)];
            uint8_t *dst = drow + c * 8;
            if (w == 0)
                continue;
            unsigned code = w & 0x1FFF;
            if (code & 0x1000)
                code = (code & 0xFFF) + bank1 * 0x1000u;
            const uint8_t *tp = tile_pixels(code, cpu) + l0 * 8;
            uint8_t g = tg[((unsigned)w >> 6) & 0x7F];
            uint8_t base = (uint8_t)((g == 0xFF ? 31 : g) << 3);
            for (int y = l0; y < l1; y++) {
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

/* Sprites: IN-WINDOW (reads cart ROM + FB staging list in place).
 * Faithful to sega16sp.cpp; see NOTES. Row clip [ymin,ymax). */
RAMCODE static void compose_sprites(int ymin, int ymax, int par)
{
    for (int i = 0; i < 64; i++) {
        volatile uint16_t *e = FB_SPR + i * 8;
        uint16_t d2 = e[2];
        if (d2 & 0x8000)
            break;
        uint16_t d0 = e[0];
        int top = d0 & 0xFF, bottom = d0 >> 8;
        if ((d2 & 0x4000) || top >= bottom)
            continue;
        int xpos = e[1] & 0x1FF;
        int flip = d2 & 0x100;
        int pitch = (int8_t)(d2 & 0xFF);
        uint16_t addr = e[3];
        uint16_t d4 = e[4], d5 = e[5];
        const uint16_t *sd = altbeast_sprites + (((d4 >> 8) & 0xF) & 7) * 0x10000;
        uint8_t pr = spr_pair[par][d4 & 0x3F];
        uint8_t base = (uint8_t)((pr == 0xFF ? 15 : pr) << 4);
        int vzoom = (d5 >> 5) & 0x1F, hzoom = d5 & 0x1F;
        uint16_t yacc = 0;

        for (int y = top; y < bottom; y++) {
            addr = (uint16_t)(addr + pitch);
            yacc = (uint16_t)(yacc + (vzoom << 10));
            if (yacc & 0x8000) {
                addr = (uint16_t)(addr + pitch);
                yacc &= 0x7FFF;
            }
            if (y < ymin || y >= ymax)
                continue;

            uint8_t *row = sbuf + (8 + y) * SBUF_W + 8;
            int x = xpos;
            uint16_t o = addr;
            int pix = 0;
            if (!flip && hzoom == 0) {
                while (x < 504) {
                    uint16_t w = sd[o++];
                    unsigned sx;
                    pix = (w >> 12) & 0xF;
                    sx = (unsigned)(x - 184);
                    if ((unsigned)(pix - 1) < 14u && sx < 320)
                        row[sx] = (uint8_t)(base + pix);
                    x++;
                    pix = (w >> 8) & 0xF;
                    sx = (unsigned)(x - 184);
                    if ((unsigned)(pix - 1) < 14u && sx < 320)
                        row[sx] = (uint8_t)(base + pix);
                    x++;
                    pix = (w >> 4) & 0xF;
                    sx = (unsigned)(x - 184);
                    if ((unsigned)(pix - 1) < 14u && sx < 320)
                        row[sx] = (uint8_t)(base + pix);
                    x++;
                    pix = w & 0xF;
                    sx = (unsigned)(x - 184);
                    if ((unsigned)(pix - 1) < 14u && sx < 320)
                        row[sx] = (uint8_t)(base + pix);
                    x++;
                    if (pix == 15)
                        break;
                }
            } else if (hzoom == 0) {
                while (x < 504) {
                    uint16_t w = sd[o--];
                    unsigned sx;
                    pix = w & 0xF;
                    sx = (unsigned)(x - 184);
                    if ((unsigned)(pix - 1) < 14u && sx < 320)
                        row[sx] = (uint8_t)(base + pix);
                    x++;
                    pix = (w >> 4) & 0xF;
                    sx = (unsigned)(x - 184);
                    if ((unsigned)(pix - 1) < 14u && sx < 320)
                        row[sx] = (uint8_t)(base + pix);
                    x++;
                    pix = (w >> 8) & 0xF;
                    sx = (unsigned)(x - 184);
                    if ((unsigned)(pix - 1) < 14u && sx < 320)
                        row[sx] = (uint8_t)(base + pix);
                    x++;
                    pix = (w >> 12) & 0xF;
                    sx = (unsigned)(x - 184);
                    if ((unsigned)(pix - 1) < 14u && sx < 320)
                        row[sx] = (uint8_t)(base + pix);
                    x++;
                    if (pix == 15)
                        break;
                }
            } else {
                /* Zoomed path. Nibbles unrolled with CONSTANT shifts —
                 * a variable shift is a libgcc call on SH-2 (slow; and
                 * kept out of habit-forming reach of the RV=1 paths). */
                int xacc = 4 * hzoom;
#define ZNIB(PIX_EXPR)                                                      \
                    pix = (PIX_EXPR);                                       \
                    xacc = (xacc & 0x3F) + hzoom;                           \
                    if (xacc < 0x40) {                                      \
                        unsigned sx = (unsigned)(x - 184);                  \
                        if (sx < 320 && pix != 0 && pix != 15)              \
                            row[sx] = (uint8_t)(base + pix);                \
                        x++;                                                \
                    }
                while (((xpos - x) & 0x1FF) != 1) {
                    uint16_t w = sd[o];
                    o = (uint16_t)(flip ? o - 1 : o + 1);
                    if (!flip) {
                        ZNIB((w >> 12) & 0xF)
                        ZNIB((w >> 8) & 0xF)
                        ZNIB((w >> 4) & 0xF)
                        ZNIB(w & 0xF)
                    } else {
                        ZNIB(w & 0xF)
                        ZNIB((w >> 4) & 0xF)
                        ZNIB((w >> 8) & 0xF)
                        ZNIB((w >> 12) & 0xF)
                    }
                    if (pix == 15)
                        break;
                    if (x >= 504)
                        break;
                }
#undef ZNIB
            }
        }
    }
}

/* Text: IN-WINDOW (ROM glyphs), above sprites. Row range [row0,row1). */
RAMCODE static void compose_text(int row0, int row1)
{
    for (int row = row0; row < row1; row++) {
        for (int col = 24; col < 64; col++) {
            uint16_t d = TEXT_C[row * 64 + col];
            unsigned code = d & 0x1FF;
            if (code == 0 && !(d & 0x0E00))
                continue;
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

/* Drain both miss queues: copy tiles ROM -> cache (in-window; RV=0).
 * Budgeted; duplicates and already-filled codes skipped by tag check. */
RAMCODE static void cache_fill(int budget)
{
    for (int q = 0; q < 2; q++) {
        uint16_t n = miss_n[q];
        if (n > 256)
            n = 256;
        for (uint16_t i = 0; i < n && budget; i++) {
            unsigned code = missq[q][i];
            unsigned set = CACHE_SET(code);
            if (cache_tag[set * 2] == code || cache_tag[set * 2 + 1] == code)
                continue;
            unsigned way = cache_rot[set] & 1;
            cache_rot[set] ^= 1;
            cache_tag[set * 2 + way] = (uint16_t)code;
            const uint32_t *src = (const uint32_t *)(altbeast_tiles + code * 64);
            uint32_t *dst = (uint32_t *)(CACHE_C + (set * 2 + way) * 64);
            for (int k = 0; k < 16; k += 4) {
                dst[k + 0] = src[k + 0];
                dst[k + 1] = src[k + 1];
                dst[k + 2] = src[k + 2];
                dst[k + 3] = src[k + 3];
            }
            budget--;
        }
        miss_n[q] = 0;
    }
}

/* Slave entry points (called from s_main; see the command mailbox). */
RAMCODE void slave_window_half(uint16_t bank1, int par)
{
    cache_purge();
    compose_sprites(0, 112, par);
    compose_text(0, 14);
    build_maps(par ^ 1, bank1);
}

RAMCODE void slave_tile_half(uint16_t bank1, int par)
{
    /* 16px strips so the slave can service MD stream batches between
     * strips (the MD's window-entry drain waits on the last batch ack —
     * keep that latency well under a millisecond). */
    extern void slave_service_stream(void);
    cache_purge();
    for (int y = 0; y < 112; y += 16) {
        compose_layer(y, y + 16, 1, 1, 1, bank1, par);
        slave_service_stream();
    }
    for (int y = 0; y < 112; y += 16) {
        compose_layer(y, y + 16, 1, 0, 0, bank1, par);
        slave_service_stream();
    }
}

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

RAMCODE static void slave_cmd(uint16_t cmd)
{
    SYNC[1] = 0;
    SYNC[0] = cmd;
}

RAMCODE static void slave_wait(uint16_t cmd)
{
    while (SYNC[1] != cmd) ;
    SYNC[0] = 0;
}

RAMCODE void m_main(void)
{
    /* Release the secondary SH-2 from its S_OK wait. */
    MARS_SYS_COMM4 = 0;

    Hw32xInit(MARS_VDP_MODE_256, 0);
    MARS_VDP_DISPMODE = MARS_NTSC_FORMAT | MARS_224_LINES | MARS_VDP_PRIO_32X | MARS_VDP_MODE_256;

    for (int i = 0; i < 13 * 0x800; i++)
        TILEMAP_U[i] = 0;
    for (int i = 0; i < 2048; i++)
        TEXT_U[i] = 0;
    for (int i = 0; i < NSETS * 2; i++)
        cache_tag[i] = 0xFFFF;
    for (int i = 0; i < NSETS; i++)
        cache_rot[i] = 0;
    miss_n[0] = miss_n[1] = 0;
    for (int i = 0; i < 64; i++)
        blank_tile[i] = 0;
    for (int k = 0; k < 2; k++) {
        for (int i = 0; i < 128; i++)
            tile_grp[k][i] = (uint8_t)(i < 8 ? i : 0xFF);
        for (int i = 0; i < 64; i++)
            spr_pair[k][i] = 0xFF;
    }
    SYNC[0] = SYNC[1] = 0;

    volatile uint16_t *cram = &MARS_CRAM;
    for (int i = 0; i < 256; i++)
        cram[i] = 0;

    SH2_FRT_TCR = 1;
    for (int i = 0; i < 16; i++)
        DIAG[i] = 0;

    /* Master is SDRAM-resident from here on: the MD may set RV=1 now. */
    MARS_SYS_COMM14 = 0x600D;

    uint16_t copy_rotor = 0;
    int par = 0;
    uint16_t last_bank = 0;
    uint16_t tile_cmd = 0;                   /* outstanding CMD_TILE, if any */

    for (;;) {
        uint16_t c0 = MARS_SYS_COMM0;
        if (c0 != 0x2000)
            continue;                        /* stream is the slave's job */

        /* ---- WINDOW (RV=0, FM=1, 68K stalled) ---- */
        uint16_t bank1 = MARS_SYS_COMM2 & 7;
        uint16_t tw = frt(), tp = tw;
        last_bank = bank1;

        if (tile_cmd) {                      /* concurrent compose finished? */
            slave_wait(tile_cmd);
            tile_cmd = 0;
        }
        diag_add(4, tp);

        /* Staging page copy (before any flip). */
        tp = frt();
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
        diag_add(0, tp);

        cache_purge();
        latch_layer_regs();                  /* scanline-261-style reg latch:
                                              * prescan + all of next frame's
                                              * compose use this snapshot */

        tp = frt();
        apply_cram(par);
        diag_add(2, tp);

        tp = frt();
        cache_fill(256);
        diag_add(1, tp);

        /* Sprites + text, split; slave also prescans next maps. */
        slave_cmd((uint16_t)(CMD_WIN | (par << 8) | bank1));
        tp = frt();
        compose_sprites(112, 224, par);
        diag_add(12, tp);
        tp = frt();
        compose_text(14, 28);
        diag_add(13, tp);

        /* Verified-flip dual blit (see NOTES: deferred FBCTL latching). */
        {
            uint16_t fs0 = MARS_VDP_FBCTL & MARS_VDP_FS;
            uint32_t guard = 4000000;
            tp = frt();
            /* Blit bank fs0 EVERY window: under deferred FBCTL latching
             * (ares) it is DISPLAYED for the whole flip-latch wait each
             * window — skipping it showed frames up to 8 windows old
             * (the "flickerfest"). Worth the ~2ms. */
            blit_frame();
            diag_add(5, tp);
            MARS_VDP_FBCTL = fs0 ^ 1;
            tp = frt();
            while ((MARS_VDP_FBCTL & MARS_VDP_FS) != (fs0 ^ 1) && --guard) ;
            diag_add(6, tp);
            tp = frt();
            blit_frame();
            diag_add(7, tp);
            MARS_VDP_FBCTL = fs0;            /* absolute, not a toggle */
        }

        tp = frt();
        slave_wait((uint16_t)(CMD_WIN | (par << 8) | bank1));
        diag_add(3, tp);
        par ^= 1;

        diag_add(8, tw);
        DIAG[9]++;

        MARS_SYS_COMM0 = 0;                  /* ack: MD restores FM/RV, game runs */

        /* ---- CONCURRENT (RV=1, game running): next frame's tiles ---- */
        cache_purge();
        tile_cmd = (uint16_t)(CMD_TILE | (par << 8) | last_bank);
        slave_cmd(tile_cmd);
        tp = frt();
        compose_layer(112, 224, 0, 1, 1, last_bank, par);   /* BG bottom */
        diag_add(10, tp);
        tp = frt();
        compose_layer(112, 224, 0, 0, 0, last_bank, par);   /* FG bottom */
        diag_add(11, tp);
    }
}
