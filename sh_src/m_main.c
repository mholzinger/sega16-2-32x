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
#define SPR_SNAP    ((volatile uint16_t *)0x26027400)   /* 512-word sprite-list
                                                         * snapshot: FB staging
                                                         * is BANK-DEPENDENT and
                                                         * the access bank isn't
                                                         * fs0 after the flip
                                                         * (ares defers the
                                                         * restore latch) — all
                                                         * sprite readers use
                                                         * this copy */
#define SYNC        ((volatile uint16_t *)0x26027800)   /* [0] cmd  [1] echo */
#define CACHE_C     ((uint8_t *)0x06028000)             /* 1024 slots x 64B */

#define FB_STAGING  ((volatile uint16_t *)0x24012000)   /* game tile RAM */
#define FB_SPR      ((volatile uint16_t *)0x2401E000)   /* game sprite RAM */
#define NPAGES      12

/* Slave commands (SYNC[0]; nonzero = pending; echo to SYNC[1] when done):
 * bits 15-12 opcode, bit 8 map parity, bits 2-0 tile bank 1. */
#define CMD_WIN     0x1000                              /* compose window: sprites+text half */
#define CMD_TILE    0x2000                              /* concurrent BG/FG half */
#define CMD_BLIT    0x3000                              /* blit window: top-half blit */

/* Tile cache bookkeeping (.bss, written master-only in-window).
 * 4-WAY x 256 sets (same 64KB): the round-1 sky-gradient codes alias
 * 3+ deep against the scene's ground tiles in a 2-way arrangement and
 * thrashed forever — rendering the sky band as black placeholders. */
/* 8-WAY x 128 sets: the animated cells cycle codes 0x100/0x400 apart
 * (three anim-frame families), and any byte-fold collides the family
 * into one set — 5+ hot codes over 4 ways churned every window (the
 * "same tiles flash in place" bug + fill burn). 8 ways hold them; the
 * >>7 fold spreads the families into different sets as well. */
#define NSETS       128
#define NWAYS       8
static uint16_t cache_tag[NSETS * NWAYS];   /* folded tile code; 0xFFFF empty */
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
static uint8_t text_grp[2][8];              /* text colors: on-demand too */
/* Group 0 is NEVER assigned by the allocator, so no composed pixel is
 * ever VALUE 0 (the MD-through value) — replaces the old BG0 alias. */

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
#define CACHE_SET(code) (((code) ^ ((code) >> 7)) & (NSETS - 1))

__attribute__((always_inline))
static inline const uint8_t *tile_pixels(unsigned code, int cpu)
{
    unsigned set = CACHE_SET(code);
    unsigned s4 = set * NWAYS;
    for (unsigned w2 = 0; w2 < NWAYS; w2++)
        if (cache_tag[s4 + w2] == code)
            return CACHE_C + (s4 + w2) * 64;
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
        /* MAME's tilemap scroll convention is ASYMMETRIC: the 16B driver
         * negates X itself (0xC0 - xsc) but passes Y raw — positive
         * scrolly moves the SOURCE WINDOW DOWN: vy = sy + ysc. The minus
         * form wrapped the screen top to the virtual map's bottom rows
         * (phantom rock band + black gap; user-spotted). */
        lr->vy0 = ysc;
    }
}

/* Prescan (slave, in-window): visible tilemap + sprite list -> color
 * groups for the NEXT window's frame. Tiles ascend from BG0_GRP+1,
 * sprite pairs descend from 15. */
RAMCODE static void build_maps(int par, uint16_t bank1)
{
    uint16_t tcount[128];
    uint8_t sused[64], txused[8];
    for (int i = 0; i < 128; i++) tcount[i] = 0;
    for (int i = 0; i < 64; i++) sused[i] = 0;
    for (int i = 0; i < 8; i++) txused[i] = 0;
    (void)bank1;

    for (int which = 0; which < 2; which++) {
        const layer_regs *lr = &snap[which];
        int yf = lr->vy0 & 7;
        int nrows = yf ? 29 : 28;
        for (int r = 0; r < nrows; r++) {
            int vy = (lr->vy0 - yf + r * 8) & 0x1FF;
            int trow = (int)(((unsigned)vy >> 3) & 0x1F);
            int qy = (int)(((unsigned)vy >> 7) & 2);
            for (int c = -1; c <= 42; c++) {     /* one column BEYOND each
                                                  * edge: freshly scrolled-in
                                                  * columns must already be
                                                  * color-mapped (left-edge
                                                  * purple flecks otherwise) */
                int vx = ((lr->vx0 & ~7) + c * 8) & 0x3FF;
                uint16_t w = TILEMAP_C[lr->pq[qy + (((unsigned)vx >> 9) & 1)] * 0x800
                                       + trow * 64 + (((unsigned)vx >> 3) & 0x3F)];
                tcount[((unsigned)w >> 6) & 0x7F]++;
            }
        }
    }
    for (int row = 0; row < 28; row++)
        for (int col = 24; col < 64; col++) {
            uint16_t d = TEXT_C[row * 64 + col];
            if ((d & 0x1FF) || (d & 0x0E00))
                txused[((unsigned)d >> 9) & 7] = 1;
        }
    for (int i = 0; i < 64; i++) {
        volatile uint16_t *d = SPR_SNAP + i * 8;
        uint16_t d2 = d[2];
        if (d2 & 0x8000)
            break;
        uint16_t d0 = d[0];
        if ((d2 & 0x4000) || (d0 & 0xFF) >= (d0 >> 8))
            continue;
        sused[d[4] & 0x3F] = 1;
    }

    /* Group budget (31 usable — group 0 is never assigned, so no pixel
     * is ever VALUE 0, the MD-through value): sprite pairs reserved off
     * the top first; text colors allocate on demand (the old fixed 0-7
     * identity block wasted 5-6 groups every frame); tile colors get the
     * rest ranked by PIXEL WEIGHT — two passes, mass colors (sky, ground)
     * first, rare ones while room lasts, overflow shares the last tile
     * group. Gameplay scenes carry ~20 tile colors + 7 sprite pairs and
     * the old fixed budget clamped the SKY into the shared group. */
    int nspr = 0;
    for (int sc = 0; sc < 64; sc++)
        if (sused[sc])
            nspr++;
    int pair_lo = 16 - nspr;
    if (pair_lo < 6)
        pair_lo = 6;
    uint8_t tile_cap = (uint8_t)(pair_lo * 2);

    uint8_t next = 1;                        /* NEVER group 0 */
    for (int c = 0; c < 8; c++)
        text_grp[par][c] = txused[c] && next < tile_cap ? next++ : 0xFF;
    for (int c = 0; c < 128; c++)
        tile_grp[par][c] = 0xFF;
    for (int c = 0; c < 128; c++)            /* pass 1: mass colors */
        if (tcount[c] >= 24 && next < tile_cap)
            tile_grp[par][c] = next++;
    for (int c = 0; c < 128; c++)            /* pass 2: the rest */
        if (tcount[c] && tile_grp[par][c] == 0xFF)
            tile_grp[par][c] = next < tile_cap ? next++ : (uint8_t)(tile_cap - 1);
    int pair = 15;
    for (int sc = 0; sc < 64; sc++) {
        if (!sused[sc]) {
            spr_pair[par][sc] = 0xFF;
            continue;
        }
        spr_pair[par][sc] = (uint8_t)(pair >= pair_lo ? pair : pair_lo);
        if (pair >= pair_lo)
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
    for (int c = 0; c < 8; c++) {
        uint8_t g = text_grp[par][c];
        if (g == 0xFF)
            continue;
        volatile uint16_t *src = FB_PAL + c * 8;
        volatile uint16_t *dst = cram + g * 8;
        for (int p = 0; p < 8; p++)
            dst[p] = s16_to_mars(src[p]);
    }
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
 * byte path. catsel (FG path only): 0 = all tiles, 1 = category-0 only,
 * 2 = category-1 (priority) only — the cat-1 pass runs IN-WINDOW after
 * sprites so priority tiles cover them (sega16b: pp=2 sprite pixels lose
 * to FG cat-1's 0x04 mark). Callers pick ranges. */
RAMCODE static void compose_layer(int ylo, int yhi, int cpu, int which,
                                  int opaque, uint16_t bank1, int par, int catsel)
{
    const layer_regs lr = snap[which];       /* latched once per window */
    int xf = lr.vx0 & 7, yf = lr.vy0 & 7;
    int nrows = yf ? 29 : 28;
    int blo = 8 + ylo, bhi = 8 + yhi;
    const uint8_t *tg = tile_grp[par];
    const uint32_t s = (uint32_t)(xf * 8);

    const uint8_t *tptr[42];
    uint32_t tbase[42];
    uint8_t tmiss[42];

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
                tmiss[c] = (tptr[c] == blank_tile);
                uint8_t g = tg[((unsigned)w >> 6) & 0x7F];
                if (g == 0xFF)
                    g = 1;                          /* prescan miss: neutral */
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
                    if (!(tmiss[c] | tmiss[c + 1])) {                       \
                        dst[0] = (EXPR0);                                   \
                        dst[1] = (EXPR1);                                   \
                    }                                                       \
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
            if (catsel == 1 && (w & 0x8000))
                continue;                           /* priority tiles: later pass */
            if (catsel == 2 && !(w & 0x8000))
                continue;
            unsigned code = w & 0x1FFF;
            if (code & 0x1000)
                code = (code & 0xFFF) + bank1 * 0x1000u;
            const uint8_t *tpx = tile_pixels(code, cpu);
            if (tpx == blank_tile)
                continue;                           /* miss: keep last frame */
            const uint8_t *tp = tpx + l0 * 8;
            uint8_t g = tg[((unsigned)w >> 6) & 0x7F];
            uint8_t base = (uint8_t)((g == 0xFF ? 1 : g) << 3);
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
        volatile uint16_t *e = SPR_SNAP + i * 8;
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
            if (hzoom == 0) {
                /* 1:1 paths. NIB draws one nibble; when the sprite starts
                 * on-screen (xpos >= 184) the sx<320 test is implied by
                 * the x<504 loop bound — the NC variants drop it. */
#define NIB(PIX_EXPR)                                                       \
                    pix = (PIX_EXPR);                                       \
                    { unsigned sx = (unsigned)(x - 184);                    \
                      if ((unsigned)(pix - 1) < 14u && sx < 320)            \
                          row[sx] = (uint8_t)(base + pix); }                \
                    x++;
#define NIB_NC(PIX_EXPR)                                                    \
                    pix = (PIX_EXPR);                                       \
                    if ((unsigned)(pix - 1) < 14u)                          \
                        row[x - 184] = (uint8_t)(base + pix);               \
                    x++;
                if (!flip && xpos >= 184) {
                    while (x < 504) {
                        uint16_t w = sd[o++];
                        NIB_NC((w >> 12) & 0xF)
                        NIB_NC((w >> 8) & 0xF)
                        NIB_NC((w >> 4) & 0xF)
                        NIB_NC(w & 0xF)
                        if (pix == 15)
                            break;
                    }
                } else if (!flip) {
                    while (x < 504) {
                        uint16_t w = sd[o++];
                        NIB((w >> 12) & 0xF)
                        NIB((w >> 8) & 0xF)
                        NIB((w >> 4) & 0xF)
                        NIB(w & 0xF)
                        if (pix == 15)
                            break;
                    }
                } else if (xpos >= 184) {
                    while (x < 504) {
                        uint16_t w = sd[o--];
                        NIB_NC(w & 0xF)
                        NIB_NC((w >> 4) & 0xF)
                        NIB_NC((w >> 8) & 0xF)
                        NIB_NC((w >> 12) & 0xF)
                        if (pix == 15)
                            break;
                    }
                } else {
                    while (x < 504) {
                        uint16_t w = sd[o--];
                        NIB(w & 0xF)
                        NIB((w >> 4) & 0xF)
                        NIB((w >> 8) & 0xF)
                        NIB((w >> 12) & 0xF)
                        if (pix == 15)
                            break;
                    }
                }
#undef NIB
#undef NIB_NC
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
RAMCODE static void compose_text(int row0, int row1, int par_text)
{
    for (int row = row0; row < row1; row++) {
        for (int col = 24; col < 64; col++) {
            uint16_t d = TEXT_C[row * 64 + col];
            unsigned code = d & 0x1FF;
            if (code == 0 && !(d & 0x0E00))
                continue;
            uint8_t g = text_grp[par_text][((unsigned)d >> 9) & 7];
            if (g == 0xFF)
                continue;
            const uint8_t *tp = altbeast_tiles + code * 64;
            uint8_t base = (uint8_t)(g << 3);
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
        DIAG[14] += n;                       /* miss telemetry */
        for (uint16_t i = 0; i < n && budget; i++) {
            unsigned code = missq[q][i];
            unsigned set = CACHE_SET(code);
            unsigned s4 = set * NWAYS;
            int hit = 0;
            for (unsigned w2 = 0; w2 < NWAYS; w2++)
                if (cache_tag[s4 + w2] == code)
                    hit = 1;
            if (hit)
                continue;
            unsigned way = cache_rot[set] & (NWAYS - 1);
            cache_rot[set] = (uint8_t)(way + 1);
            /* Data FIRST, tag LAST: a concurrent reader (the slave's
             * in-window cat-1 pass) must never hit a tag whose 64 bytes
             * are still half-copied — that tearing rendered plausible-
             * but-wrong tiles at animated cells. */
            cache_tag[s4 + way] = 0xFFFF;
            const uint32_t *src = (const uint32_t *)(altbeast_tiles + code * 64);
            uint32_t *dst = (uint32_t *)(CACHE_C + (s4 + way) * 64);
            for (int k = 0; k < 16; k += 4) {
                dst[k + 0] = src[k + 0];
                dst[k + 1] = src[k + 1];
                dst[k + 2] = src[k + 2];
                dst[k + 3] = src[k + 3];
            }
            cache_tag[s4 + way] = (uint16_t)code;
            budget--;
        }
        miss_n[q] = 0;
    }
}

/* Copy staging tilemap pages [p0,p1) into the SDRAM shadow. FULL refresh
 * every window (split master/slave): the old 2-page rotor left the shadow
 * up to ~200ms stale, and scrolling streams new tile columns continuously
 * — the roaming garbled squares were stale shadow columns. ~0.7ms/CPU. */
RAMCODE static void copy_pages(int p0, int p1)
{
    for (int pg = p0; pg < p1; pg++) {
        volatile uint32_t *src = (volatile uint32_t *)(FB_STAGING + pg * 0x800);
        volatile uint32_t *dst = (volatile uint32_t *)(TILEMAP_U + pg * 0x800);
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
    }
}

RAMCODE static void blit_half(int ylo, int yhi)
{
    for (int y = ylo; y < yhi; y++) {
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

/* Slave entry points (called from s_main; see the command mailbox).
 * The window is now fully two-CPU: each side composes AND BLITS its own
 * half (FM grants the whole SH-2 side, either CPU may write the FB).
 * The global flip edges are synchronized through SYNC[2] (slave step:
 * 1 = first-bank blit done, 2 = second-bank blit done) and SYNC[3]
 * (master: flip latched, second bank writable). No stream servicing
 * inside the window — the 68K is stalled, no batches arrive. */
/* ---- Row-following pipeline, slave side. Each vint window k (0,1,2)
 * blits slice k of the SHIPPING frame (rows 0-75/75-150/150-224), then
 * composes the NEXT frame's sprites/cat1/text into a region already
 * shipped this cycle (window 1 -> rows 0-72, window 2 -> 72-144,
 * window 0 -> the 144-224 leftover with the OLD parity, before the new
 * snapshot). Tile thirds run CONCURRENT between windows via the SDRAM
 * cache. Regions are 72-row (tile-row aligned) and always a strict
 * subset of the rows the blit pointer has passed. */
RAMCODE void slave_window_k(uint16_t cmd)
{
    int k = (cmd >> 4) & 3;
    int par = (cmd >> 8) & 1;
    uint16_t bank1 = cmd & 7;
    int skip = (cmd >> 3) & 1;                   /* master lost vblank: no blit */
    cache_purge();
    if (!skip) {
        int y0 = k * 75;
        blit_half(y0, y0 + 37);
    }
    SYNC[2] = 1;                                 /* master restores bank X */
    if (k == 0) {
        /* finish the shipping frame's tail (old par) */
        compose_sprites(144, 184, par);
        compose_layer(144, 184, 1, 0, 0, bank1, par, 2);
        compose_text(18, 23, par);
        while (SYNC[3] != 1) ;                   /* master's tail done: safe to
                                                  * retire the old snapshot */
        for (int i = 0; i < 512; i += 8) {       /* sprite-list snapshot from
                                                  * bank X (game staging) */
            SPR_SNAP[i + 0] = FB_SPR[i + 0];
            SPR_SNAP[i + 1] = FB_SPR[i + 1];
            SPR_SNAP[i + 2] = FB_SPR[i + 2];
            SPR_SNAP[i + 3] = FB_SPR[i + 3];
            SPR_SNAP[i + 4] = FB_SPR[i + 4];
            SPR_SNAP[i + 5] = FB_SPR[i + 5];
            SPR_SNAP[i + 6] = FB_SPR[i + 6];
            SPR_SNAP[i + 7] = FB_SPR[i + 7];
        }
        copy_pages(6, NPAGES);
    } else {
        int lo = (k == 1) ? 0 : 72;              /* slave half of R(k-1) */
        compose_sprites(lo, lo + 36, par);
        compose_layer(lo, lo + 36, 1, 0, 0, bank1, par, 2);
        compose_text((k == 1) ? 0 : 9, (k == 1) ? 4 : 13, par);
    }
}

RAMCODE void slave_tile_third(uint16_t cmd)
{
    /* Short strips so the slave can service MD stream batches between
     * strips (the MD's window-entry drain waits on the last batch ack —
     * keep that latency well under a millisecond). */
    extern void slave_service_stream(void);
    int k = (cmd >> 4) & 3;
    int par = (cmd >> 8) & 1;
    uint16_t bank1 = cmd & 7;
    int lo = k * 72;
    int hi = (k == 2) ? 184 : lo + 36;           /* slave half of R(k) */
    cache_purge();
    for (int y = lo; y < hi; y += 12) {
        int ye = (y + 12 > hi) ? hi : y + 12;
        compose_layer(y, ye, 1, 1, 1, bank1, par, 0);
        slave_service_stream();
    }
    for (int y = lo; y < hi; y += 12) {
        int ye = (y + 12 > hi) ? hi : y + 12;
        compose_layer(y, ye, 1, 0, 0, bank1, par, 1);
        slave_service_stream();
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
    for (int i = 0; i < NSETS * NWAYS; i++)
        cache_tag[i] = 0xFFFF;
    for (int i = 0; i < NSETS; i++)
        cache_rot[i] = 0;
    miss_n[0] = miss_n[1] = 0;
    for (int i = 0; i < 64; i++)
        blank_tile[i] = 0;
    for (int k = 0; k < 2; k++) {
        for (int i = 0; i < 128; i++)
            tile_grp[k][i] = 0xFF;
        for (int i = 0; i < 64; i++)
            spr_pair[k][i] = 0xFF;
        for (int i = 0; i < 8; i++)
            text_grp[k][i] = 0xFF;
    }
    SYNC[0] = SYNC[1] = SYNC[2] = SYNC[3] = 0;

    volatile uint16_t *cram = &MARS_CRAM;
    for (int i = 0; i < 256; i++)
        cram[i] = 0;

    SH2_FRT_TCR = 1;
    for (int i = 0; i < 16; i++)
        DIAG[i] = 0;

    /* Master is SDRAM-resident from here on: the MD may set RV=1 now. */
    MARS_SYS_COMM14 = 0x600D;

    int par = 0;
    uint16_t tile_cmd = 0;                   /* outstanding CMD_TILE, if any */

    for (;;) {
        uint16_t c0 = MARS_SYS_COMM0;

        /* ---- ROW-FOLLOWING PIPELINE: three windows per cycle, each
         * with a vblank flip-pair + 75-row slice blit of the SHIPPING
         * frame, then in-window compose of the NEXT frame's sprites/
         * cat1/text into rows the blit pointer has already passed.
         * Tile thirds for the next frame run CONCURRENT between
         * windows (SDRAM cache, RV=1). No dedicated compose vint:
         * a full frame ships every 3 vints (20Hz). Window 0 finishes
         * the shipping frame's 144-224 tail with the OLD parity, then
         * snapshots staging (regs, sprite list, pages, CRAM) for the
         * next frame and flips parity. ---- */
        if ((c0 & 0xFFCF) != 0x2000)
            continue;                        /* stream is the slave's job */
        {
            int k = (c0 >> 4) & 3;
            uint16_t bank1 = MARS_SYS_COMM2 & 7;
            uint16_t tw = frt(), tp = tw;

            if (tile_cmd) {                  /* concurrent third finished? */
                slave_wait(tile_cmd);
                tile_cmd = 0;
            }
            diag_add(4, tp);

            /* vblank-critical part. If the third-wait ate the vblank,
             * skip the blit (slice ships next cycle) rather than flip
             * mid-frame — a stale band beats a black frame. The clock
             * is the MD's live V-counter heartbeat (COMM12, tag 0xD0xx,
             * written every ack-spin iteration): ares' FBCTL VBLK bit
             * proved untrustworthy (the field bursts of one black frame
             * per cycle — flips passing a stale/false vblank check). */
            int y0 = k * 75;
            int yend = (k == 2) ? 224 : y0 + 75;
            int skip;
            {
                uint16_t md_v = MARS_SYS_COMM12;
                if ((md_v & 0xFF00) == 0xD000) {
                    unsigned v = md_v & 0xFF;
                    skip = (v < 0xDF || v > 0xE4);
                } else
                    skip = !(MARS_VDP_FBCTL & 0x8000);
                if (skip)
                    DIAG[7]++;               /* master-side silent skips */
            }
            uint16_t scmd = (uint16_t)(0x3000 | (k << 4) | (par << 8)
                                       | bank1 | (skip ? 8 : 0));
            uint16_t fs_x = MARS_VDP_FBCTL & MARS_VDP_FS;
            uint32_t guard;
            SYNC[2] = 0;
            SYNC[3] = 0;
            tp = frt();
            if (!skip) {
                guard = 2000000;
                MARS_VDP_FBCTL = fs_x ^ 1;   /* -> display bank Y */
                while ((MARS_VDP_FBCTL & MARS_VDP_FS) != (fs_x ^ 1) && --guard) ;
            }
            diag_add(6, tp);
            slave_cmd(scmd);
            cache_purge();                   /* slice rows may hold the OTHER
                                              * CPU's composes from last cycle */
            tp = frt();
            if (!skip) {
                blit_half(y0 + 37, yend);
                while (SYNC[2] < 1) ;        /* slave slice blitted */
                MARS_VDP_FBCTL = fs_x;       /* back to staging bank X */
                guard = 2000000;
                while ((MARS_VDP_FBCTL & MARS_VDP_FS) != fs_x && --guard) ;
            }
            diag_add(5, tp);

            /* in-window compose (game paused, RV=0) */
            if (k == 0) {
                /* shipping frame's tail, OLD par (master half of R2) */
                tp = frt();
                compose_sprites(184, 224, par);
                compose_layer(184, 224, 0, 0, 0, bank1, par, 2);
                compose_text(23, 28, par);
                diag_add(12, tp);
                SYNC[3] = 1;                 /* slave may retire the snapshot */
                latch_layer_regs();          /* scanline-261-style reg latch */
                tp = frt();
                copy_pages(0, 6);
                diag_add(0, tp);
                par ^= 1;                    /* now composing the next frame */
            } else {
                /* W0 stays lean (its concurrent gap feeds the slowest
                 * third): CRAM applies at W1, miss fills drain at W2. */
                int lo = (k == 1) ? 36 : 108;    /* master half of R(k-1) */
                if (k == 1) {
                    tp = frt();
                    apply_cram(par);
                    diag_add(2, tp);
                }
                tp = frt();
                cache_fill(256);             /* drain misses EVERY window —
                                              * a 2-window fill delay showed
                                              * as white stale rectangles on
                                              * scroll in the field */
                diag_add(1, tp);
                tp = frt();
                compose_sprites(lo, lo + 36, par);
                compose_layer(lo, lo + 36, 0, 0, 0, bank1, par, 2);
                compose_text((k == 1) ? 4 : 13, (k == 1) ? 9 : 18, par);
                diag_add(12, tp);

                if (k == 2) {
                    /* PERF BAR (debug): ares has no profiler tap, so the
                     * frame itself is the tap. Row 2: this cycle's total
                     * in-window ticks; row 4: window-head tile-third wait.
                     * 1px = 64 FRT ticks = 87.7us; CRAM[255] forced white
                     * (sprite pair 15 pen 15 — never rendered). */
                    static uint32_t pw, pv;
                    uint32_t w = DIAG[8], v = DIAG[4];
                    int lw = (int)((w - pw) >> 6), lv = (int)((v - pv) >> 6);
                    pw = w; pv = v;
                    if (lw > 300) lw = 300;
                    if (lv > 300) lv = 300;
                    ((volatile uint16_t *)&MARS_CRAM)[255] = 0x7FFF;
                    uint8_t *b2 = sbuf + (8 + 2) * SBUF_W + 8;
                    uint8_t *b4 = sbuf + (8 + 4) * SBUF_W + 8;
                    for (int i = 0; i < 300; i++) {
                        b2[i] = (i < lw) ? 0xFF : b2[i];
                        b4[i] = (i < lv) ? 0xFF : b4[i];
                    }
                }
            }

            tp = frt();
            slave_wait(scmd);
            diag_add(3, tp);

            /* launch the concurrent tile third k (rows already shipped
             * this cycle), ack the MD, then do our own half post-ack. */
            tile_cmd = (uint16_t)(CMD_TILE | (k << 4) | (par << 8) | bank1);
            slave_cmd(tile_cmd);
            diag_add(8, tw);
            if (k == 0)
                DIAG[9]++;
            MARS_SYS_COMM0 = 0;              /* ack: MD restores FM/RV, game runs */

            {
                int lo = (k == 2) ? 184 : (k * 72 + 36);
                int hi = (k == 2) ? 224 : (k * 72 + 72);
                tp = frt();
                compose_layer(lo, hi, 0, 1, 1, bank1, par, 0);   /* BG */
                diag_add(10, tp);
                tp = frt();
                compose_layer(lo, hi, 0, 0, 0, bank1, par, 1);   /* FG cat0 */
                diag_add(11, tp);
                if (k == 2)
                    build_maps(par ^ 1, bank1);  /* prescan for the NEXT cycle */
            }
        }
    }
}
