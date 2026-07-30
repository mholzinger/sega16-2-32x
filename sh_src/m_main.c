#include "mars.h"
#include "buildstamp.h"

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
/* Regions start at 0x19000: .bss had SILENTLY GROWN past 0x18000 and
 * the cache-tag tail (later pri_lut) overlapped tilemap page 0 — the
 * window page-copies stomped the tags every cycle (steady-state
 * miss=9.9/window instead of ~0). The Makefile now FAILS the build if
 * __end crosses SDRAM_BSS_LIMIT. */
#define TILEMAP_U   ((volatile uint16_t *)0x26019000)   /* 13 pages x 2K words */
#define TILEMAP_C   ((const uint16_t *)0x06019000)      /* page 12 = blank */
#define TEXT_U      ((volatile uint16_t *)0x26026000)   /* 2048 words */
#define TEXT_C      ((const uint16_t *)0x06026000)
/* Palette: read straight from FB staging in-window — no shadow, no
 * stream (game palette writes are all word/long; zero-byte-drop safe). */
/* Palette arrives via the COMM stream into SDRAM (slave writes PAL_SH;
 * see s_main.c). FB staging couldn't carry it: MD FB-window writes are
 * dropped by arbitration when the SH-2 owns the FB (ares/hardware
 * strict, MAME lenient), and per-bank staging left never-written rows
 * as zeros — the black-actor family. */
#define PAL_SH      ((volatile uint16_t *)0x26027000)   /* 2048 words */
#define DIAG        ((volatile uint32_t *)0x26028000)   /* profiling, lua-read */
#define SPR_SNAP    ((volatile uint16_t *)0x26028400)   /* 512-word sprite-list
                                                         * snapshot: FB staging
                                                         * is BANK-DEPENDENT and
                                                         * the access bank isn't
                                                         * fs0 after the flip
                                                         * (ares defers the
                                                         * restore latch) — all
                                                         * sprite readers use
                                                         * this copy */
#define SYNC        ((volatile uint16_t *)0x26028800)   /* [0] cmd  [1] echo */
#define CACHE_C     ((uint8_t *)0x06029000)             /* 1024 slots x 64B */

#define FB_STAGING  ((volatile uint16_t *)0x24012000)   /* game tile RAM */
#define FB_SPR      ((volatile uint16_t *)0x2401E000)   /* game sprite RAM */
#define SPR_LAND    ((volatile uint16_t *)0x26028C00)   /* DREQ landing, 1KB
                                                         * (0x28C00-0x28FFF) */
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
#define cache_rot ((uint8_t *)0x06028880)  /* NSETS<=128B, after blank_tile;
                                            * round-robin eviction way,
                                            * master-only (cache_fill) */

/* Per-CPU miss queues: appended (write-through) during concurrent compose,
 * drained by the master in the next window. */
#define MISSQ_CAP 192   /* was 256; miss rates run 10-36/window,
                         * cap trimmed to fit .bss under the guard */
static uint16_t missq[2][MISSQ_CAP];
static volatile uint16_t miss_n[2];

/* Placeholder pixels for uncached tiles: all pen 0 -> the tile color's
 * base entry. Must be RAM (.bss), never ROM — it is read at RV=1. */
/* fixed SDRAM: .bss squeezed by the 0x19000 region guard. LAYOUT:
 * 28000 DIAG | 28100 BM (+alloc state at 28360) | 28400 SPR_SNAP |
 * 28800 SYNC (+blank_tile 28840) | 28900 cram_mirror | 28B00
 * shadow_lut | 28C00-28FFF SPR_LAND (DREQ DMA target - EXCLUSIVE:
 * the DMA rewrites this whole KB every frame).
 * blank_tile is constant zeros after master init (coherent for both
 * CPUs); the grp/pr allocator state is master-only (bm_tail). */
#define blank_tile ((uint8_t *)0x06028840)              /* 64B, after SYNC */

/* Color maps, double-buffered by window parity (slave prescans par^1
 * during window N; both CPUs compose frame N+1 from par^1 after the
 * toggle). */
static uint8_t tile_grp[2][128];
static uint8_t spr_pair[2][64];
static uint8_t text_grp[2][8];              /* text colors: on-demand too */
/* Sticky ownership (persistent across cycles — see build_maps): which
 * color owns each CRAM group / sprite pair, and how long since it was
 * last seen on screen. */
#define grp_key  ((uint8_t *)0x06028360)               /* 32B, BM tail */
#define grp_kind ((uint8_t *)0x06028380)               /* 32B */
#define grp_age  ((uint8_t *)0x060283A0)               /* 32B */
#define pr_key   ((uint8_t *)0x060283C0)               /* 16B */
#define pr_age   ((uint8_t *)0x060283D0)               /* 16B */
/* PER-PIXEL SPRITE/TILE PRIORITY (segas16b_v.cpp screen_update):
 * tile pixels carry a level — BG cat0=1, BG cat1=2, FG cat0=2,
 * FG cat1=4, text cat0=4, text cat1=8 — and a sprite pixel draws iff
 * (1 << its 2-bit priority field) > level. We recover the level from
 * the composed pixel VALUE via a 256-entry LUT: the allocator knows
 * which CRAM group serves which color, and the prescan records each
 * color's level. Exact except when one color is used at two levels in
 * the same frame (counted in DIAG[15]); sprite-pair groups stay level
 * 0 so sprite-over-sprite remains hardware list-order overwrite. */
static uint8_t pri_lut[2][256];
static uint8_t pri_max[2];                  /* max level in pri_lut[par] */
/* SHADOW sprites (color 0x3F): the arcade darkens the UNDERLYING
 * pixel via a shadowed copy of the whole palette (segas16b_v:
 * dest[x] += palette_entries). 32X CRAM has no spare bank, so we
 * remap through shadow_lut: each CRAM index -> the existing entry
 * closest to half its brightness. Rebuilt lazily (idle loop, 64
 * entries per visit) whenever apply_cram changes CRAM; silhouette
 * fallback while dirty. cram_mirror exists because CRAM itself is
 * unreadable outside the window (FM=0). */
/* Fixed SDRAM (0x28900/0x28B00, behind SYNC): .bss hit the 0x19000
 * region guard; these are master-only and boot-initialized in m_main
 * (fixed blocks are NOT zeroed like .bss). */
#define cram_mirror ((uint16_t *)0x06028900)   /* 512B */
#define shadow_lut  ((uint8_t *)0x06028B00)    /* 256B */
static volatile uint8_t shadow_dirty = 1;
static uint8_t shadow_cur;                  /* rebuild chunk cursor */
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
    if (n < MISSQ_CAP) {
        missq[cpu][n] = (uint16_t)code;
        miss_n[cpu] = (uint16_t)(n + 1);
    }
    /* MISS: read the tile STRAIGHT FROM CART ROM (cached 0x02 view).
     * Legal at any time under unpair (RV pinned 0) — the old
     * blank-sentinel "keep last frame" dance was an RV=1 artifact.
     * It also could never converge on working sets larger than the
     * cache: the animated title backdrop (~1120 cycling codes vs 1024
     * slots) thrashed forever and rendered as a black/purple field.
     * The queue still promotes hot tiles into SDRAM for speed. */
    return altbeast_tiles + (unsigned)code * 64;
}

typedef struct {
    uint8_t pq[4];
    int vx0, vy0;
    /* ALTERNATE register set + rowscroll (segaic16 tilemap_16b_draw_
     * layer): every 8-screen-row band reads its rowscroll word; bit 15
     * there switches the band to the ALT pages/scrolls (text words
     * 0x742+which / 0x74A / 0x74E), and bit 15 of the PRIMARY xscroll
     * makes the rowscroll word the band's xscroll (per-row parallax).
     * The attract title screen draws ENTIRELY through the alt set —
     * without this it composed empty page 0: black backdrop. */
    uint8_t pq_a[4];
    int vx0_a, vy0_a;
    uint16_t xs_raw;                        /* primary xscroll, unmasked */
    uint16_t rs[28];                        /* rowscroll per 8-row band */
    uint8_t any_special;                    /* any alt/rowscroll bit set */
} layer_regs;

/* Scroll/page registers are LATCHED once per window into this snapshot,
 * used by BOTH the prescan and the whole of the next frame's compose.
 * The game (running concurrently now) mutates the live regs continuously;
 * without the latch, compose sees rows/columns the prescan never colored
 * (garish placeholder tiles at the screen edges). Real System 16B latches
 * these at scanline 261 — this mirrors the hardware. */
static layer_regs snap[2];

static inline void decode_pages(uint16_t pages, uint8_t *pq)
{
    /* 16 selectable pages, but the game only writes 0-11; 12-15 all
     * map to the single blank page 12 of the shadow.
     * Quadrant nibbles per segaic16 draw_virtual_tilemap: upper-left
     * = bits 0-3, upper-right = 4-7, lower-left = 8-11, lower-right
     * = 12-15. The old decode had each pair X-SWAPPED — the attract
     * title (pages 0x1212, art in page 2, camera over the left half)
     * composed the EMPTY page 1: black title backdrop. */
    uint8_t p;
    p = pages & 0xF;         pq[0] = p > 12 ? 12 : p;
    p = (pages >> 4) & 0xF;  pq[1] = p > 12 ? 12 : p;
    p = (pages >> 8) & 0xF;  pq[2] = p > 12 ? 12 : p;
    p = (pages >> 12) & 0xF; pq[3] = p > 12 ? 12 : p;
}

RAMCODE static void latch_layer_regs(void)
{
    for (int which = 0; which < 2; which++) {
        layer_regs *lr = &snap[which];
        uint16_t xraw  = TEXT_C[0x74C + which];
        uint16_t ysc   = TEXT_C[0x748 + which] & 0x1FF;
        decode_pages(TEXT_C[0x740 + which], lr->pq);
        lr->xs_raw = xraw;
        /* xscroll is a FULL 10-bit value (virtual map = 1024px wide;
         * MAME applies no latch mask). The old &0x1FF made every pan
         * phase with xs >= 0x200 off by 512px — the wrong map half.
         * The eye sequence pans across both halves. */
        lr->vx0 = ((0xC0 - (xraw & 0x3FF)) & 0x3FF);
        /* X convention PINNED by the attract scream screen (art at
         * cols 24-63 displayed full-bleed at xs=0): source vx =
         * screen x + ((0xC0 - xs) & 0x3FF) — matching segaic16's
         * effxscroll directly. The previously-negated form survived
         * for weeks because the title is sign-agnostic (xs=0xC0 ->
         * eff=0) and gameplay backgrounds are locally periodic. */
        /* MAME's tilemap scroll convention is ASYMMETRIC: the 16B driver
         * negates X itself (0xC0 - xsc) but passes Y raw — positive
         * scrolly moves the SOURCE WINDOW DOWN: vy = sy + ysc. The minus
         * form wrapped the screen top to the virtual map's bottom rows
         * (phantom rock band + black gap; user-spotted). */
        lr->vy0 = ysc;
        /* alternate set (text words +2) + per-band rowscroll table */
        decode_pages(TEXT_C[0x742 + which], lr->pq_a);
        lr->vy0_a = TEXT_C[0x74A + which] & 0x1FF;
        lr->vx0_a = ((0xC0 - (TEXT_C[0x74E + which] & 0x3FF)) & 0x3FF);
        uint16_t any = xraw & 0x8000;
        for (int rw = 0; rw < 28; rw++) {
            uint16_t v = TEXT_C[0x7C0 + 0x20 * which + rw];
            lr->rs[rw] = v;
            any |= (uint16_t)(v & 0x8000);
        }
        lr->any_special = (uint8_t)(any != 0);
    }
}

/* Prescan (slave, in-window): visible tilemap + sprite list -> color
 * groups for the NEXT window's frame. Tiles ascend from BG0_GRP+1,
 * sprite pairs descend from 15. */
/* build_maps accumulators live in a fixed SDRAM block (0x28100-0x283FF,
 * between SYNC and CACHE_C) so the scan can run CHUNKED: under sustained
 * overload the band queue is never empty, and the idle-only owed-maps
 * path starved forever — stale tile_grp -> prescan misses -> the group-1
 * red/white/blue garbage frames on ares. Chunks bound the per-window
 * cost; the inline phase-6 path still runs the whole build at once. */
struct bm_state {
    uint16_t tcount[128];
    uint8_t sused[64], txused[8];
    uint8_t col_lvl[128], txt_lvl[8];
    uint8_t amb_col[128];
    uint8_t active, par, which, aset, row;
};
#define BM ((struct bm_state *)0x06028100)  /* 768B free after DIAG */
/* All bm_* helpers take the state by pointer: the inline (phase-6)
 * build uses a STACK instance so the hot path optimizes exactly as the
 * original stack-local code did; only the chunked path pays for the
 * fixed SDRAM block. */
#define tcount  (a->tcount)
#define sused   (a->sused)
#define txused  (a->txused)
#define col_lvl (a->col_lvl)
#define txt_lvl (a->txt_lvl)
#define amb_col (a->amb_col)

RAMCODE static void bm_reset(struct bm_state *a)
{
    for (int i = 0; i < 128; i++) { tcount[i] = 0; col_lvl[i] = 0; amb_col[i] = 0; }
    for (int i = 0; i < 64; i++) sused[i] = 0;
    for (int i = 0; i < 8; i++) { txused[i] = 0; txt_lvl[i] = 0; }
}

/* scan `nr` tilemap rows of (which, aset) starting at row r0;
 * returns rows actually available for that pass (28 or 29) */
RAMCODE static int bm_scan_rows(struct bm_state *a, int which, int aset, int r0, int nr)
{
    const layer_regs *lr = &snap[which];
    const uint8_t *pq = aset ? lr->pq_a : lr->pq;
    int vy0 = aset ? lr->vy0_a : lr->vy0;
    int vx00 = aset ? lr->vx0_a : lr->vx0;
    int yf = vy0 & 7;
    int nrows = yf ? 29 : 28;
    for (int r = r0; r < nrows && r < r0 + nr; r++) {
        int vy = (vy0 - yf + r * 8) & 0x1FF;
        int trow = (int)(((unsigned)vy >> 3) & 0x1F);
        int qy = (int)(((unsigned)vy >> 7) & 2);
        for (int c = -1; c <= 42; c++) {
            int vx = ((vx00 & ~7) + c * 8) & 0x3FF;
            uint16_t w = TILEMAP_C[pq[qy + (((unsigned)vx >> 9) & 1)] * 0x800
                                   + trow * 64 + (((unsigned)vx >> 3) & 0x3F)];
            unsigned cc = ((unsigned)w >> 6) & 0x7F;
            tcount[cc]++;
            if (w) {
                uint8_t lvl = which
                    ? ((w & 0x8000) ? 2 : 1)
                    : ((w & 0x8000) ? 4 : 2);
                if (col_lvl[cc] && col_lvl[cc] != lvl)
                    amb_col[cc] = 1;
                if (lvl > col_lvl[cc])
                    col_lvl[cc] = lvl;
            }
        }
    }
    return nrows;
}

static void bm_tail(struct bm_state *a, int par);

RAMCODE static void build_maps(int par, uint16_t bank1)
{
    struct bm_state st;                     /* STACK: hot path stays fast */
    struct bm_state *a = &st;
    (void)bank1;
    BM->active = 0;                         /* invalidate any chunked build */
    bm_reset(a);

    for (int which = 0; which < 2; which++) {
        const layer_regs *lr = &snap[which];
        /* pass 0 = primary regs; pass 1 = ALT set (only when a band
         * selects it), so alt-page tiles get color groups too */
        for (int aset = 0; aset < (lr->any_special ? 2 : 1); aset++) {
            const uint8_t *pq = aset ? lr->pq_a : lr->pq;
            int vy0 = aset ? lr->vy0_a : lr->vy0;
            int vx00 = aset ? lr->vx0_a : lr->vx0;
            int yf = vy0 & 7;
            int nrows = yf ? 29 : 28;
            for (int r = 0; r < nrows; r++) {
                int vy = (vy0 - yf + r * 8) & 0x1FF;
                int trow = (int)(((unsigned)vy >> 3) & 0x1F);
                int qy = (int)(((unsigned)vy >> 7) & 2);
                for (int c = -1; c <= 42; c++) { /* one column BEYOND each
                                                  * edge: freshly scrolled-in
                                                  * columns must already be
                                                  * color-mapped (left-edge
                                                  * purple flecks otherwise) */
                    int vx = ((vx00 & ~7) + c * 8) & 0x3FF;
                    uint16_t w = TILEMAP_C[pq[qy + (((unsigned)vx >> 9) & 1)] * 0x800
                                           + trow * 64 + (((unsigned)vx >> 3) & 0x3F)];
                    unsigned cc = ((unsigned)w >> 6) & 0x7F;
                    tcount[cc]++;
                    /* priority level: which==1 is our BG layer (cat0=1,
                     * cat1=2); which==0 FG (cat0=2, cat1=4) */
                    if (w) {
                        uint8_t lvl = which
                            ? ((w & 0x8000) ? 2 : 1)
                            : ((w & 0x8000) ? 4 : 2);
                        if (col_lvl[cc] && col_lvl[cc] != lvl)
                            amb_col[cc] = 1; /* color at two levels: LUT
                                              * approximation engaged */
                        if (lvl > col_lvl[cc])
                            col_lvl[cc] = lvl;
                    }
                }
            }
        }
    }
    bm_tail(a, par);
}

/* text scan + sprite scan + sticky allocation + priority LUT: the fast
 * final stage, one chunk in the chunked path */
RAMCODE static void bm_tail(struct bm_state *a, int par)
{
    for (int row = 0; row < 28; row++)
        for (int col = 24; col < 64; col++) {
            uint16_t d = TEXT_C[row * 64 + col];
            if ((d & 0x1FF) || (d & 0x0E00)) {
                unsigned tc = ((unsigned)d >> 9) & 7;
                txused[tc] = 1;
                uint8_t lvl = (d & 0x8000) ? 8 : 4;  /* text cat1 : cat0 */
                if (lvl > txt_lvl[tc])
                    txt_lvl[tc] = lvl;
            }
        }
    for (int i = 0; i < 64; i++) {
        volatile uint16_t *d = SPR_SNAP + i * 8;
        uint16_t d2 = d[2];
        if (d2 & 0x8000)
            break;
        uint16_t d0 = d[0];
        if ((d2 & 0x4000) || (d0 & 0xFF) >= (d0 >> 8))
            continue;
        if ((d[4] & 0x3F) == 0x3F)
            continue;                        /* shadows use reserved pair 15 */
        sused[d[4] & 0x3F] = 1;
    }

    /* STICKY group allocation (31 usable — group 0 is never assigned,
     * so no pixel is ever VALUE 0, the MD-through value). The old
     * positional allocator re-numbered EVERY group whenever the used-
     * color set changed — a sprite flashing its color each frame
     * reshuffled the whole map every cycle, and pixels already on
     * screen (composed with last cycle's map) pointed at re-purposed
     * CRAM entries: the full-screen strobe on power-up effects.
     * Now colors OWN their groups across cycles: singles (tile/text)
     * allocate lowest-first, sprite PAIRS (two aligned groups)
     * highest-first, new claims take free slots then the oldest
     * unused ones, and colors unseen for ~90 cycles decay away.
     * Over-subscription overflows into sharing, as before. */
    for (int g = 1; g < 32; g++)
        if (grp_key[g] != 0xFF) {
            int used = grp_kind[g] ? txused[grp_key[g]]
                                   : (tcount[grp_key[g]] != 0);
            if (used)
                grp_age[g] = 0;
            else if (++grp_age[g] > 90)
                grp_key[g] = 0xFF;
        }
    for (int p = 1; p < 16; p++)
        if (pr_key[p] != 0xFF) {
            if (sused[pr_key[p]])
                pr_age[p] = 0;
            else if (++pr_age[p] > 90)
                pr_key[p] = 0xFF;
        }

    /* Budget boundary: sprites need ALIGNED pairs, and tiles filling
     * low groups unbounded starves them (MAME field test: red-
     * silhouette player, purple-less zombies). Reserve pair space for
     * the demand (6..10 pairs, hysteresis via stickiness): singles
     * live strictly below `bound`, pairs strictly above. */
    int nspr = 0;
    for (int sc = 0; sc < 64; sc++)
        if (sused[sc])
            nspr++;
    int need = nspr < 6 ? 6 : (nspr > 10 ? 10 : nspr);
    int bound = 32 - 2 * need;
    for (int q = bound; q < 32; q++)         /* evict IDLE singles from the
                                              * pair zone only: blanket
                                              * eviction of live ones made
                                              * group 14 flip between text
                                              * yellow and sprite pair 7
                                              * every cycle (the yellow
                                              * sprite-ghost artifact) */
        if (grp_key[q] != 0xFF && grp_age[q] > 0)
            grp_key[q] = 0xFF;

    /* sprites first (a pair = aligned groups 2p/2p+1). Pair 15 is
     * DUAL-PURPOSE: allocatable like any pair (a hard reserve cost one
     * pair and blacked out the player in full scenes), but whenever no
     * color claims it, apply_cram parks the shadow ramp there. Shadows
     * always draw through base 240, so under peak pressure they tint
     * with pair 15's owner instead of going dark — cutscenes, where
     * shadows actually star, run far below capacity. */
    uint8_t shared_pair = 15;
    for (int sc = 0; sc < 64; sc++)
        spr_pair[par][sc] = 0xFF;
    for (int sc = 0; sc < 64; sc++) {
        if (!sused[sc])
            continue;
        int p = -1;
        for (int q = 1; q < 16; q++)
            if (pr_key[q] == sc) { p = q; break; }
        if (p < 0)
            for (int q = 15; q >= bound / 2; q--)
                if (pr_key[q] == 0xFF && grp_key[2 * q] == 0xFF
                    && grp_key[2 * q + 1] == 0xFF) { p = q; break; }
        if (p < 0) {                         /* steal the oldest OFF-SCREEN
                                              * pair (age>=3: stealing one
                                              * merely absent THIS scan
                                              * recolors pixels still
                                              * displayed) */
            uint8_t best = 2;
            for (int q = 15; q >= bound / 2; q--)
                if (pr_key[q] != 0xFF && pr_age[q] > best) {
                    best = pr_age[q]; p = q;
                }
        }
        if (p < 0) {
            spr_pair[par][sc] = shared_pair;  /* over budget: share */
            continue;
        }
        pr_key[p] = (uint8_t)sc;
        pr_age[p] = 0;
        grp_key[2 * p] = grp_key[2 * p + 1] = 0xFF;
        spr_pair[par][sc] = (uint8_t)p;
        shared_pair = (uint8_t)p;
    }

    /* text + tile singles (mass tile colors before rare ones) */
    for (int c = 0; c < 8; c++)
        text_grp[par][c] = 0xFF;
    for (int c = 0; c < 128; c++)
        tile_grp[par][c] = 0xFF;
    uint8_t shared_tile = 0xFF;
    for (int pass = 0; pass < 3; pass++) {
        for (int c = 0; c < (pass ? 128 : 8); c++) {
            int kind = pass ? 0 : 1;
            if (pass == 0 && !txused[c]) continue;
            if (pass == 1 && tcount[c] < 24) continue;
            if (pass == 2 && (!tcount[c] || tile_grp[par][c] != 0xFF)) continue;
            int g = -1;
            for (int q = 1; q < bound; q++)
                if (grp_key[q] == (uint8_t)c && grp_kind[q] == kind
                    && pr_key[q >> 1] == 0xFF) { g = q; break; }
            if (g < 0)
                for (int q = 1; q < bound; q++)
                    if (grp_key[q] == 0xFF && pr_key[q >> 1] == 0xFF) {
                        g = q; break;
                    }
            if (g < 0) {                     /* steal the oldest OFF-SCREEN
                                              * single (age>=3, see pairs) */
                uint8_t best = 2;
                for (int q = 1; q < bound; q++)
                    if (grp_key[q] != 0xFF && pr_key[q >> 1] == 0xFF
                        && grp_age[q] > best) {
                        best = grp_age[q]; g = q;
                    }
            }
            if (g < 0) {
                if (kind == 0)
                    tile_grp[par][c] = shared_tile;   /* over budget: share */
                else
                    text_grp[par][c] = shared_tile;
                continue;
            }
            grp_key[g] = (uint8_t)c;
            grp_kind[g] = (uint8_t)kind;
            grp_age[g] = 0;
            if (kind == 0) {
                tile_grp[par][c] = (uint8_t)g;
                shared_tile = (uint8_t)g;
            } else
                text_grp[par][c] = (uint8_t)g;
            if (shared_tile == 0xFF)
                shared_tile = (uint8_t)g;
        }
    }

    /* Build the priority LUT for this parity: pens 1-7 of each group
     * inherit the owning color's level; pen 0 (group base) stays 0 —
     * matching MAME, where the opaque BG pass sets no priority and
     * pen-0 pixels stay level 0. Sprite pairs never appear in
     * tile_grp/text_grp, so their 16-entry blocks stay level 0. */
    {
        uint8_t *pl = pri_lut[par];
        for (int i = 0; i < 256; i++)
            pl[i] = 0;
        for (int c = 0; c < 128; c++) {
            uint8_t g = tile_grp[par][c];
            if (g == 0xFF || !col_lvl[c])
                continue;
            for (int p = 1; p < 8; p++)
                if (col_lvl[c] > pl[g * 8 + p])
                    pl[g * 8 + p] = col_lvl[c];
        }
        for (int c = 0; c < 8; c++) {
            uint8_t g = text_grp[par][c];
            if (g == 0xFF || !txt_lvl[c])
                continue;
            for (int p = 1; p < 8; p++)
                if (txt_lvl[c] > pl[g * 8 + p])
                    pl[g * 8 + p] = txt_lvl[c];
        }
        unsigned na = 0;
        for (int c = 0; c < 128; c++)
            na += amb_col[c];
        DIAG[15] = na;                       /* DISTINCT ambiguous colors
                                              * this frame (not cumulative) */
        uint8_t mx = 0;
        for (int i = 0; i < 256; i++)
            if (pl[i] > mx) mx = pl[i];
        pri_max[par] = mx;                   /* sprites with thr > mx skip
                                              * the per-pixel gate (and its
                                              * dst read) entirely */
    }
}

/* One bounded slice of an owed build (the queue-busy maintenance slot).
 * Returns 1 when the build for `par` completed this call. A build that
 * spans a snapshot boundary mixes two frames' regs — same staleness
 * class as the deferral itself; the next build corrects it. */
RAMCODE static int build_maps_chunk(int par)
{
    struct bm_state *a = BM;
    if (!BM->active || BM->par != (uint8_t)par) {
        bm_reset(a);
        BM->active = 1;
        BM->par = (uint8_t)par;
        BM->which = 0;
        BM->aset = 0;
        BM->row = 0;
        return 0;
    }
    if (BM->which < 2) {
        int nrows = bm_scan_rows(a, BM->which, BM->aset, BM->row, 8);
        BM->row = (uint8_t)(BM->row + 8);
        if (BM->row >= nrows) {
            BM->row = 0;
            if (BM->aset == 0 && snap[BM->which].any_special)
                BM->aset = 1;
            else {
                BM->aset = 0;
                BM->which++;
            }
        }
        return 0;
    }
    bm_tail(a, par);
    BM->active = 0;
    return 1;
}
#undef tcount
#undef sused
#undef txused
#undef col_lvl
#undef txt_lvl
#undef amb_col

__attribute__((always_inline))
/* v carries the S16 word's bit 15 in bit 15 of the MIRROR only: it is
 * the hardware's shadow-exempt flag (jts16_colmix.v: shadow & ~pal[15]).
 * Real CRAM gets bits 0-14 only — bit 15 there is the 32X through-bit. */
static inline void cram_set(volatile uint16_t *dst, int idx, uint16_t v)
{
    dst[0] = (uint16_t)(v & 0x7FFF);
    if (cram_mirror[idx] != v) {
        cram_mirror[idx] = v;
        shadow_dirty = 1;
    }
}

RAMCODE static void apply_cram(int par)
{
    volatile uint16_t *cram = &MARS_CRAM;
    for (int c = 0; c < 128; c++) {
        uint8_t g = tile_grp[par][c];
        if (g == 0xFF)
            continue;
        volatile uint16_t *src = PAL_SH + c * 8;
        volatile uint16_t *dst = cram + g * 8;
        for (int p = 0; p < 8; p++)
            cram_set(dst + p, g * 8 + p,
                     (uint16_t)(s16_to_mars(src[p]) | (src[p] & 0x8000)));
    }
    for (int c = 0; c < 8; c++) {
        uint8_t g = text_grp[par][c];
        if (g == 0xFF)
            continue;
        volatile uint16_t *src = PAL_SH + c * 8;
        volatile uint16_t *dst = cram + g * 8;
        for (int p = 0; p < 8; p++)
            cram_set(dst + p, g * 8 + p,
                     (uint16_t)(s16_to_mars(src[p]) | (src[p] & 0x8000)));
    }
    for (int sc = 0; sc < 64; sc++) {
        uint8_t pr = spr_pair[par][sc];
        if (pr == 0xFF)
            continue;
        volatile uint16_t *src = PAL_SH + 1024 + sc * 16;
        volatile uint16_t *dst = cram + pr * 16;
        for (int p = 0; p < 16; p++)
            cram_set(dst + p, pr * 16 + p,
                     (uint16_t)(s16_to_mars(src[p]) | (src[p] & 0x8000)));
    }
    /* Shadow ramp: when pair 15 has no real owner this frame, its
     * pens 1-14 go near-black so shadow sprites (and over-budget
     * spillover) draw as dark silhouettes. Entry 255 stays free for
     * the debug bar hijack. */
    int p15_used = 0;
    for (int sc = 0; sc < 64; sc++)
        if (spr_pair[par][sc] == 15) { p15_used = 1; break; }
    if (!p15_used)
        for (int p = 1; p < 15; p++)
            cram[240 + p] = 0x0842;
}

/* Compose one tile layer's SCREEN ROW RANGE [ylo,yhi) into sbuf from the
 * SDRAM cache (legal at RV=1). opaque=1: BG packed 32-bit path; 0: FG
 * byte path. catsel (FG path only): 0 = all tiles, 1 = category-0 only,
 * 2 = category-1 (priority) only — the cat-1 pass runs IN-WINDOW after
 * sprites so priority tiles cover them (sega16b: pp=2 sprite pixels lose
 * to FG cat-1's 0x04 mark). Callers pick ranges. */
RAMCODE static void compose_layer_regs(int ylo, int yhi, int cpu, int which,
                                       int opaque, uint16_t bank1, int par,
                                       int catsel, const layer_regs *lrp);

/* Per-band reg selection (segaic16): each 8-screen-row band may use the
 * rowscroll word as its xscroll (primary xscroll bit 15) and/or switch
 * wholesale to the ALT page/scroll set (rowscroll bit 15). The common
 * gameplay case has neither — one full-range call, zero new cost. */
RAMCODE static void compose_layer(int ylo, int yhi, int cpu, int which,
                                  int opaque, uint16_t bank1, int par, int catsel)
{
    const layer_regs *lr = &snap[which];
    if (!lr->any_special) {
        compose_layer_regs(ylo, yhi, cpu, which, opaque, bank1, par,
                           catsel, lr);
        return;
    }
    layer_regs eff = *lr;
    for (int b = ylo & ~7; b < yhi; b += 8) {
        int lo = b < ylo ? ylo : b;
        int hi = b + 8 > yhi ? yhi : b + 8;
        uint16_t rs = lr->rs[(unsigned)b >> 3];
        if (rs & 0x8000) {                   /* band uses the ALT set */
            for (int i = 0; i < 4; i++)
                eff.pq[i] = lr->pq_a[i];
            eff.vx0 = lr->vx0_a;
            eff.vy0 = lr->vy0_a;
        } else {
            for (int i = 0; i < 4; i++)
                eff.pq[i] = lr->pq[i];
            eff.vy0 = lr->vy0;
            eff.vx0 = (lr->xs_raw & 0x8000)  /* per-row parallax x */
                ? (int)((0xC0 - (rs & 0x3FF)) & 0x3FF)   /* sign+mask fixed
                                                          * same as vx0 */
                : lr->vx0;
        }
        compose_layer_regs(lo, hi, cpu, which, opaque, bank1, par,
                           catsel, &eff);
    }
}

RAMCODE static void compose_layer_regs(int ylo, int yhi, int cpu, int which,
                                       int opaque, uint16_t bank1, int par,
                                       int catsel, const layer_regs *lrp)
{
    const layer_regs lr = *lrp;
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
#ifdef SPRITES_OFF_TEST
    /* A/B probe for the cart-bus contention hypothesis (LOOP iter 4):
     * sprite compose is the heaviest SH-2 cart reader (per-pixel
     * sd[o] fetches). If ares V-gate rejects collapse with sprites
     * off, the 68K's chronic lateness is bus contention, not
     * scheduling. Build: make SPROBE=1. NEVER ship. */
    (void)ymin; (void)ymax; (void)par;
    return;
#endif
    /* Gated dst reads go through the UNCACHED sbuf alias: the SH-2
     * cache is write-through/no-allocate, so the write-only fast path
     * never fills lines — a cached gate read would pay a 16-byte line
     * fill per miss just to check one byte. */
    const uint8_t *pl = pri_lut[par];       /* tile level per pixel value */
    uint8_t pmax = pri_max[par];
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
        /* sprite pixel shows iff (1 << pp) > tile level (segas16b_v).
         * pp=2 is exact via the layer ORDER (no reads); only pp<=1
         * sprites gate per pixel (they hide behind BG-cat1/FG-cat0).
         * pp=3 approximated as pp=2, occurrences counted. */
        uint8_t pp = (uint8_t)((d4 >> 6) & 3);
        uint8_t thr = (uint8_t)(1u << pp);
        int gated = (pp <= 1) && (thr <= pmax);
        int shad = 0;
        if (pp == 3)
            DIAG[16]++;
        uint8_t base;
        if ((d4 & 0x3F) == 0x3F) {
            /* SHADOW sprites (color 0x3F): darken the UNDERLYING pixel
             * via shadow_lut (nearest-darker CRAM entry — the arcade
             * indexes a shadowed palette copy). While the LUT is
             * rebuilding after a palette change, fall back to the
             * pair-15 silhouette so the cast never vanishes. */
            base = 15 << 4;
            /* true darkening for normal-size shadows (gameplay drop
             * shadows); the HUGE cutscene actors (~2x per-pixel cost
             * over thousands of pixels) saturated both CPUs into band
             * staleness — worse inaccuracy than their silhouette,
             * which is visually close. Sideband rework will lift the
             * cap. */
            shad = !shadow_dirty && (bottom - top) <= 48;
        } else {
            uint8_t pr = spr_pair[par][d4 & 0x3F];
            base = (uint8_t)((pr == 0xFF ? 15 : pr) << 4);
        }
        int vzoom = (d5 >> 5) & 0x1F, hzoom = d5 & 0x1F;
        uint16_t yacc = 0;

        /* CLOSED-FORM ROW FAST-FORWARD: the strip model used to walk
         * every sprite from its TOP row each strip just to reach
         * [ymin,ymax) — a 150-row boss stepped ~150 times to compose
         * 12 rows, on every strip, on both CPUs (the ares budget
         * shortfall's biggest single waste). The vzoom accumulator is
         * a plain linear sum, so n skipped rows collapse to one
         * multiply: carries = (n*step)>>15, remainder = &0x7FFF —
         * bit-exact with the per-row loop. Fully-outside sprites now
         * cost O(1) per strip. */
        {
            int y0 = top < ymin ? ymin : top;
            int ylim = bottom > ymax ? ymax : bottom;
            if (y0 >= ylim)
                continue;
            unsigned n = (unsigned)(y0 - top);
            if (n) {
                unsigned tot = n * ((unsigned)vzoom << 10);
                addr = (uint16_t)(addr + pitch * (int)(n + (tot >> 15)));
                yacc = (uint16_t)(tot & 0x7FFF);
            }
            top = y0;
            bottom = ylim;
        }
        for (int y = top; y < bottom; y++) {
            addr = (uint16_t)(addr + pitch);
            yacc = (uint16_t)(yacc + (vzoom << 10));
            if (yacc & 0x8000) {
                addr = (uint16_t)(addr + pitch);
                yacc &= 0x7FFF;
            }

            uint8_t *row = sbuf + (8 + y) * SBUF_W + 8;
            const uint8_t *urow = row;       /* gate reads (rare: pp<=1) */
            (void)urow;
            int x = xpos;
            uint16_t o = addr;
            int pix = 0;
            if (shad) {
                /* darken-underlying scalar loop (1:1 and zoom both:
                 * hzoom==0 makes xacc a no-op) */
                int xacc = 4 * hzoom;
#define SNIB(PIX_EXPR)                                                      \
                    pix = (PIX_EXPR);                                       \
                    xacc = (xacc & 0x3F) + hzoom;                           \
                    if (xacc < 0x40) {                                      \
                        unsigned sx = (unsigned)(x - 184);                  \
                        if (sx < 320 && pix != 0 && pix != 15)              \
                            row[sx] = shadow_lut[urow[sx]];                 \
                        x++;                                                \
                    }
                while (((xpos - x) & 0x1FF) != 1) {
                    uint16_t w = sd[o];
                    o = (uint16_t)(flip ? o - 1 : o + 1);
                    if (!flip) {
                        SNIB((w >> 12) & 0xF)
                        SNIB((w >> 8) & 0xF)
                        SNIB((w >> 4) & 0xF)
                        SNIB(w & 0xF)
                    } else {
                        SNIB(w & 0xF)
                        SNIB((w >> 4) & 0xF)
                        SNIB((w >> 8) & 0xF)
                        SNIB((w >> 12) & 0xF)
                    }
                    if (pix == 15)
                        break;
                    if (x >= 504)
                        break;
                }
#undef SNIB
            } else if (hzoom == 0 && !gated) {  /* gated (pp<=1) sprites take
                                              * the scalar path below —
                                              * the gate stays out of the
                                              * unrolled fast macros */
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
#define ZNIB_G(PIX_EXPR)                                                    \
                    pix = (PIX_EXPR);                                       \
                    xacc = (xacc & 0x3F) + hzoom;                           \
                    if (xacc < 0x40) {                                      \
                        unsigned sx = (unsigned)(x - 184);                  \
                        if (sx < 320 && pix != 0 && pix != 15               \
                            && thr > pl[urow[sx]])                          \
                            row[sx] = (uint8_t)(base + pix);                \
                        x++;                                                \
                    }
                if (!gated) {
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
                } else {
                    while (((xpos - x) & 0x1FF) != 1) {
                        uint16_t w = sd[o];
                        o = (uint16_t)(flip ? o - 1 : o + 1);
                        if (!flip) {
                            ZNIB_G((w >> 12) & 0xF)
                            ZNIB_G((w >> 8) & 0xF)
                            ZNIB_G((w >> 4) & 0xF)
                            ZNIB_G(w & 0xF)
                        } else {
                            ZNIB_G(w & 0xF)
                            ZNIB_G((w >> 4) & 0xF)
                            ZNIB_G((w >> 8) & 0xF)
                            ZNIB_G((w >> 12) & 0xF)
                        }
                        if (pix == 15)
                            break;
                        if (x >= 504)
                            break;
                    }
                }
#undef ZNIB
#undef ZNIB_G
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
/* One 4-entry chunk of the shadow LUT rebuild (~0.6ms: 4x256 distance
 * evaluations with three multiplies each — a 64-entry chunk measured
 * ~10ms and blew every gate whenever palettes churned). 64 chunks
 * refresh the table; silhouette fallback covers the interim. */
RAMCODE static void shadow_lut_chunk(void)
{
    unsigned lo = (unsigned)shadow_cur * 4;
    for (unsigned i = lo; i < lo + 4; i++) {
        uint16_t c = cram_mirror[i];
        if (c & 0x8000) {
            /* pal bit 15 = shadow-exempt (jts16_colmix.v:
             * shadow & ~pal[15]): the color shows unshadowed */
            shadow_lut[i] = (uint8_t)i;
            continue;
        }
        /* hardware shadow = each channel x0.75: a - (a>>2)
         * (jts16_colmix.v dim()); nearest CRAM entry to that target
         * is the closest an indexed FB can get (gap flagged) */
        int r = c & 0x1F, g = (c >> 5) & 0x1F, b = (c >> 10) & 0x1F;
        int tr = r - (r >> 2);
        int tg = g - (g >> 2);
        int tb = b - (b >> 2);
        unsigned best = i, bestd = 0xFFFFFFFFu;
        for (unsigned j = 0; j < 256; j++) {
            uint16_t e = cram_mirror[j];
            int dr = (e & 0x1F) - tr;
            int dg = ((e >> 5) & 0x1F) - tg;
            int db = ((e >> 10) & 0x1F) - tb;
            unsigned d = (unsigned)(dr * dr + dg * dg + db * db);
            if (d < bestd) { bestd = d; best = j; }
        }
        shadow_lut[i] = (uint8_t)best;
    }
    shadow_cur = (uint8_t)((shadow_cur + 1) & 63);
    if (shadow_cur == 0)
        shadow_dirty = 0;                    /* full table fresh */
}

RAMCODE static void cache_fill(int budget)
{
    for (int q = 0; q < 2; q++) {
        uint16_t n = miss_n[q];
        if (n > MISSQ_CAP)
            n = MISSQ_CAP;
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
        /* CACHED-AREA FB WRITES (0x04000000 alias): every shipped Sega
         * 32X arcade port (Space Harrier/After Burner/T-MEK literal
         * pools: 111-125 cached FB refs vs a handful uncached) blits
         * through the cached window — SH-2 write-through means stores
         * ride the 4-deep write buffer instead of stalling the bus per
         * word. Write-only path: no stale-read hazard; the buffer
         * drains long before any flip. */
        volatile uint32_t *dst = (volatile uint32_t *)
            (0x04000000u + 0x200 + (unsigned)y * 320);
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
/* ---- UNPAIR STEP 2: compose is fully CONCURRENT. Windows now hold
 * only what genuinely needs the 68K stopped: the vblank blit slices
 * and (window 0) the staging snapshot + CRAM. All composition — tile
 * layers AND sprites/cat1/text — runs while the game executes, since
 * RV=0 lets the SH-2s read cart art at any time. Per-band schedule
 * (bands R0=[0,72) R1=[72,144) R2=[144,224), tile-row aligned,
 * always a strict subset of shipped rows):
 *   after Wk's ack: band R(k) of the frame snapshotted at W0 —
 *   each CPU composes tiles then sprites/cat1/text on ITS OWN rows
 *   (row-split means no cross-CPU ordering is needed); after W2 the
 *   master also prescans maps for the next cycle. */
RAMCODE void slave_window_k(uint16_t cmd)
{
    int k = (cmd >> 4) & 3;
    int skip = (cmd >> 3) & 1;                   /* master lost vblank: no blit */
    cache_purge();
    /* TWO-VBLANK SHIP (parity iteration 1b-lite): the whole coherent
     * sbuf frame ships across w1+w2 vblanks (56 rows per CPU per
     * window, ~0.8ms — inside vblank even at ares speed), w0 ships
     * nothing. The old 3-window rolling sweep put an arbitrary
     * 2-frame composite on screen at every instant; now the only
     * temporal boundary is a FIXED mid-screen seam visible for one
     * frame. Full eviction of staging (true double-buffer) is blocked
     * by 68K read-backs: collision tst.w's (0x6936+) and the round-
     * transition scratch save/restore in page 1 (0x1B760) — see
     * LOOP.md iteration 1b findings. */
    if (!skip) {
        if (k == 1)
            blit_half(0, 56);
        else if (k == 2)
            blit_half(112, 168);
    }
    SYNC[2] = 1;                                 /* master restores bank X */
    if (k == 1) {
        /* SNAPSHOT AT W1, not W0: the game's own vint handler (which
         * rebuilds the sprite list) runs AFTER our window in the
         * interrupt chain — a W0 snapshot reads a stale or mid-rebuild
         * list once the game runs at speed (field: vanishing Zeus,
         * sprites cut at band seams). At W1 the list is complete. */
        /* The sprite-list snapshot moved to the MASTER (k==1 block):
         * taken here it raced the master's bank restore and read the
         * DISPLAY bank (zeros at 0x1E000) — short cutscene lists
         * vanished whole (the missing intro cast). The master is
         * ordered after its own restore by construction. */
        /* steady-state page copies retired (write-observer ring):
         * the master syncs ONLY dirty pages from the DREQ-tail
         * bitmap. The slave's whole k1 copy burden is gone. */
    }
}

RAMCODE void slave_concurrent_k(uint16_t cmd)
{
    /* Full band compose, slave rows: tiles (BG opaque + FG cat0),
     * then sprites + FG cat1 + text over them. Short strips so the
     * MD stream stays serviced. */
    extern void slave_service_stream(void);
    int k = (cmd >> 4) & 3;
    int par = (cmd >> 8) & 1;
    uint16_t bank1 = cmd & 7;
    int rg = (k + 2) % 3;                        /* W1->R0, W2->R1, W0->R2 */
    int lo = rg * 72;
    int hi = (rg == 2) ? 184 : lo + 36;          /* slave rows of R(rg) */
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
    /* Layer order: EXACT per segas16b_v for pp=2 sprites (measured
     * dominant in this game): tiles cat0, sprites, FG cat1, text.
     * pp<=1 sprites additionally gate per pixel via pri_lut so they
     * correctly hide behind BG-cat1/FG-cat0; pp=3 is approximated as
     * pp=2 (counted in DIAG[16]) until the sideband rework.
     * Sprites in SHORT STRIPS with stream service between: one whole-
     * half call of SNIB shadow actors ran ~5ms unserviced and the MD
     * stalled mid-stream past its vint gate (458 skips by the intro). */
    for (int y = lo; y < hi; y += 12) {      /* 12: finer strips multiply
                                              * the full-height row-walk
                                              * of tall zoomed actors */
        int ye = (y + 12 > hi) ? hi : y + 12;
        compose_sprites(y, ye, par);
        slave_service_stream();
    }
    compose_layer(lo, hi, 1, 0, 0, bank1, par, 2);   /* FG cat1 OVER sprites */
    slave_service_stream();
    compose_text((rg == 0) ? 0 : (rg == 1) ? 9 : 18,
                 (rg == 0) ? 4 : (rg == 1) ? 13 : 23, par);
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
    for (int i = 0; i < 32; i++) {
        grp_key[i] = 0xFF;
        grp_age[i] = 0;
    }
    for (int i = 0; i < 16; i++) {
        pr_key[i] = 0xFF;
        pr_age[i] = 0;
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

    /* Master band work is an INTERRUPTIBLE state machine: queued per
     * window, processed in 12-row strips between COMM0 polls. A busy
     * master tail used to delay window pickup past vblank -> silent
     * blit skip -> that band displayed a full-cycle-old frame (the
     * ares "floating heads / split sprites" staleness). Now pickup
     * latency is bounded by one strip (~0.4ms). */
    struct band {
        uint8_t on, rg, bpar, bank, phase, s0, cnt;
    };
    struct band bq[4];
    int bq_h = 0, bq_t = 0;
    int maps_owed = 0;                   /* build_maps from a dropped band */
    uint8_t owed_par = 0;
    /* Per-region stale frontier: strip index where the last DROPPED
     * band's compose had reached. The region's next band starts its
     * strip walk here, so rows left stale by a drop are recomposed
     * FIRST and the frontier rotates. Under sustained overload (ares
     * heavy attract) the old drop-oldest policy victimized the same
     * strips every cycle: a locked stale stripe crawling with the pan
     * (the eye-scene "white band"). Rotation bounds any row's
     * staleness to ~2 cycles. */
    uint8_t drop_s0[3] = {0, 0, 0};
    uint16_t t_vint = 0;                 /* FRT at last window pickup */
    uint8_t shadow_stole = 0;            /* one stolen LUT chunk per window */
    uint16_t pg_pending = 0x1FFF;        /* dirty tilemap pages awaiting
                                          * copy (write-observer ring:
                                          * bitmap rides the DREQ tail;
                                          * boot = all pages once) */
    uint32_t win_no = 0;                 /* window counter (steal rate-limit) */
    for (int i = 0; i < 4; i++)
        bq[i].on = 0;
    for (int i = 0; i < 256; i++) {      /* fixed blocks aren't .bss-zeroed */
        cram_mirror[i] = 0;
        shadow_lut[i] = (uint8_t)i;      /* identity until first rebuild */
    }
    BM->active = 0;
    DIAG[18] = BUILD_HASH32;             /* savestates self-identify */

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
        if ((c0 & 0xFFCF) != 0x2000) {
            /* no window pending: advance the queued band by ONE strip.
             * SELF-PACING: heavy single-shot phases (fills, build_maps)
             * only run in the EARLY part of the frame — near the next
             * vint we stay light so window pickup latency is bounded
             * by one 12-row strip (else the vblank gate skips blits:
             * 923 mskips/run when build_maps sat on the pickup). */
            if (!bq[bq_h].on && maps_owed) {
                /* owed build_maps from a dropped band: CHUNKED — one
                 * bounded slice per visit (idle drains it in ~9 visits;
                 * the queue-busy maintenance slot below keeps it moving
                 * when idle never comes) */
                uint16_t dt = (uint16_t)(frt() - t_vint);
                if (dt <= 4000) {   /* EARLY WINDOW ONLY: any chunk in
                                     * flight when the window signal
                                     * arrives delays pickup past the
                                     * vblank gate — C-rom forensics:
                                     * un-gated maintenance = 94% blit
                                     * skips on ares vs 31% baseline */
                    if (build_maps_chunk(owed_par))
                        maps_owed = 0;
                }
                continue;
            }
            if (!bq[bq_h].on && shadow_dirty) {
                /* palette changed: refresh the shadow LUT, one chunk
                 * per idle visit (silhouette fallback until fresh) */
                uint16_t dt = (uint16_t)(frt() - t_vint);
                if (dt <= 4000)
                    shadow_lut_chunk();
                continue;
            }
            if (bq[bq_h].on && !shadow_stole &&
                (maps_owed || ((win_no & 3) == 0 && shadow_dirty))) {
                /* owed maps get a slot EVERY window (convergence is
                 * user-visible color correctness); shadow chunks
                 * throttled to every 4th — gameplay fades keep the
                 * LUT perpetually dirty, and the every-2nd steal fed
                 * the deferral rate (0.78/cycle on ares = the action
                 * band at half cadence). Shadow staleness degrades to
                 * silhouette fallback; band cadence is the game. */
                /* MAINTENANCE SLOT: one bounded chunk of deferred work
                 * every 2nd window even when band work is pending.
                 * Under sustained overload the queue is NEVER empty, so
                 * idle-only deferrals starve forever — this bit every
                 * subsystem in turn (shadow LUT: permanent silhouettes;
                 * owed maps: stale tile_grp -> prescan misses -> the
                 * group-1 red/white/blue garbage). Owed maps outrank
                 * the shadow LUT: wrong colors beat wrong shadows. */
                uint16_t dt = (uint16_t)(frt() - t_vint);
                if (dt <= 4000) {
                    if (maps_owed) {
                        if (build_maps_chunk(owed_par))
                            maps_owed = 0;
                    } else
                        shadow_lut_chunk();
                    shadow_stole = 1;
                    continue;
                }
            }
            if (bq[bq_h].on) {
                struct band *b = &bq[bq_h];
                uint16_t dt = (uint16_t)(frt() - t_vint);
                /* PRE-VINT QUIET ZONE: the window period is ~12000 FRT
                 * ticks; past 11300 start NOTHING and just poll, so
                 * pickup latency at the heartbeat gate is ~0. A strip
                 * begun here (~0.4ms = 6 scanlines) blew the 2-line
                 * worst-case gate slack in busy scenes (385 mskips/run,
                 * swait=0, bdrain=0 — the strip WAS the latency).
                 * Shrinking strips instead collapsed throughput to
                 * block-drain saturation (2305 mskips). */
                /* sprite strips can run ~1ms+ when big zoomed actors
                 * (cutscene Zeus head) span the rows — give phase 2 a
                 * wider pre-vint margin (mskips 311/run when it shared
                 * the tile strips' 11300 threshold; 10400 was too wide
                 * and starved throughput into block-drains). */
#ifdef PRESSURE_TEST
                /* ares-proxy budgets: MAME is ~3x faster, so cutting
                 * the quiet zone reproduces the ares operating point
                 * (drops in the eye hold ~1/2 cycles). Build with
                 * `make PRESSURE=1` before EVERY ares handoff; the
                 * shadow-steal/palette-flood regression shipped
                 * because this check wasn't routine. */
                if (dt > (b->phase == 2 ? 6000 : 6500))
#else
                if (dt > (b->phase == 2 ? 10300 : 11300))
#endif
                    continue;               /* sprite strips: gated/shadow
                                             * actors can run ~1.4ms */
                if (b->phase >= 5 && dt > 8000)
                    continue;            /* defer heavy work past the vint
                                          * (build_maps ~4ms uninterruptible:
                                          * a 9600 deadline overran the post) */
                int lo = (b->rg == 2) ? 184 : (b->rg * 72 + 36);
                int hi = (b->rg == 2) ? 224 : (b->rg * 72 + 72);
                int ns = (b->rg == 2) ? 4 : 6;
                int idx = b->s0 + b->cnt;
                if (idx >= ns) idx -= ns;
                int y = lo + idx * 12, ye = (y + 12 > hi) ? hi : y + 12;
                uint16_t tq = frt();
                /* Order exact for pp=2 sprites (see slave_concurrent_k) */
                switch (b->phase) {
                case 0:
                    compose_layer(y, ye, 0, 1, 1, b->bank, b->bpar, 0);
                    diag_add(10, tq);
                    break;
                case 1:
                    compose_layer(y, ye, 0, 0, 0, b->bank, b->bpar, 1);
                    diag_add(11, tq);
                    break;
                case 2:
                    compose_sprites(y, ye, b->bpar);
                    diag_add(12, tq);
                    break;
                case 3:
                    compose_layer(y, ye, 0, 0, 0, b->bank, b->bpar, 2);
                    break;
                case 4:
                    compose_text((b->rg == 0) ? 4 : (b->rg == 1) ? 13 : 23,
                                 (b->rg == 0) ? 9 : (b->rg == 1) ? 18 : 28,
                                 b->bpar);
                    break;
                case 5:
                    /* ADAPTIVE DRAIN: flat 128/window left seconds of
                     * white placeholder tiles on ares when cutscenes
                     * burst-load art (Zeus). Deep backlog gets a 3x
                     * budget — still inside the dt<=8000 heavy slot
                     * (build_maps at ~4ms fits the same gate). */
                    cache_fill((miss_n[0] + miss_n[1] > 96) ? 384 : 128);
                    diag_add(1, tq);
                    break;
                default:
                    if (b->rg == 2)
                        build_maps(b->bpar ^ 1, b->bank);
                    b->on = 0;
                    bq_h = (bq_h + 1) & 3;
                    continue;
                }
                if (b->phase >= 4) {         /* single-shot phases */
                    b->phase++;
                    b->cnt = 0;
                } else if (++b->cnt >= ns) {
                    b->phase++;
                    b->cnt = 0;
                }
            }
            continue;
        }
        {
            int k = (c0 >> 4) & 3;
            uint16_t bank1 = MARS_SYS_COMM2 & 7;
            uint16_t tw = frt(), tp = tw;
            t_vint = tw;
            shadow_stole = 0;
            win_no++;

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
            int wblit = !skip && k != 0;     /* two-vblank ship: w0 idle */
            SYNC[2] = 0;
            SYNC[3] = 0;
            tp = frt();
            if (wblit) {
                guard = 2000000;
                MARS_VDP_FBCTL = fs_x ^ 1;   /* -> display bank Y */
                while ((MARS_VDP_FBCTL & MARS_VDP_FS) != (fs_x ^ 1) && --guard) ;
            }
            diag_add(6, tp);
            slave_cmd(scmd);
            cache_purge();                   /* slice rows may hold the OTHER
                                              * CPU's composes from last cycle */
            tp = frt();
            if (wblit) {
                if (k == 1)
                    blit_half(56, 112);
                else
                    blit_half(168, 224);
                while (SYNC[2] < 1) ;        /* slave half blitted */
                MARS_VDP_FBCTL = fs_x;       /* back to staging bank X */
                guard = 2000000;
                while ((MARS_VDP_FBCTL & MARS_VDP_FS) != fs_x && --guard) ;
            }
            diag_add(5, tp);

            /* in-window work (game paused, RV=0): W1 = snapshot + CRAM
             * only (post game-vint: the sprite list is complete);
             * W0/W2 = nothing beyond the blit. */
            if (k == 1) {
                latch_layer_regs();          /* scanline-261-style reg latch */
                /* sprite-list snapshot — DREQ FIFO DMA (the FB staging
                 * copy is retired: the game's vint upload crossed the
                 * FB window exactly when the SH-2 owned the FB, and
                 * ares/hardware DISCARD those MD writes — savestate-
                 * proven 40/64 torn records. The MD shim now walks the
                 * game's order table and pushes the ordered list over
                 * the DREQ FIFO; DMAC0 lands it at SPR_LAND. TE set =
                 * a complete coherent list; TE clear = keep last
                 * frame's list (stale beats torn). */
                if (SH2_DMA_CHCR0 & 2) {     /* TE: transfer complete */
                    for (int i = 0; i < 512; i += 8) {
                        SPR_SNAP[i + 0] = SPR_LAND[i + 0];
                        SPR_SNAP[i + 1] = SPR_LAND[i + 1];
                        SPR_SNAP[i + 2] = SPR_LAND[i + 2];
                        SPR_SNAP[i + 3] = SPR_LAND[i + 3];
                        SPR_SNAP[i + 4] = SPR_LAND[i + 4];
                        SPR_SNAP[i + 5] = SPR_LAND[i + 5];
                        SPR_SNAP[i + 6] = SPR_LAND[i + 6];
                        SPR_SNAP[i + 7] = SPR_LAND[i + 7];
                    }
                    pg_pending |= SPR_LAND[512];  /* DREQ-tail bitmap */
                } else
                    DIAG[17]++;              /* incomplete DREQ frames */
                /* re-arm for the NEXT vint's push (Chaotix sequence:
                 * disable, read to clear TE, program, enable) */
                SH2_DMA_CHCR0 = 0x44E0;
                (void)SH2_DMA_CHCR0;
                SH2_DMA_SAR0 = 0x20004012;   /* DREQ FIFO */
                SH2_DMA_DAR0 = 0x26028C00;   /* SPR_LAND (uncached) */
                SH2_DMA_TCR0 = 516;          /* 512 sprites + bitmap + pad */
                SH2_DMA_DRCR0 = 0;
                SH2_DMA_DMAOR = 1;
                SH2_DMA_CHCR0 = 0x44E1;
                /* NEWLY-mapped pages must be fresh before compose —
                 * changed page regs only (unconditional ORing would
                 * recopy every active page every cycle) */
                {
                    static uint8_t ppq[2][8] = {{0xFF}};
                    for (int w2 = 0; w2 < 2; w2++)
                        for (int q = 0; q < 4; q++) {
                            if (snap[w2].pq[q] != ppq[w2][q]) {
                                ppq[w2][q] = snap[w2].pq[q];
                                pg_pending |= 1u << ppq[w2][q];
                            }
                            if (snap[w2].pq_a[q] != ppq[w2][q + 4]) {
                                ppq[w2][q + 4] = snap[w2].pq_a[q];
                                pg_pending |= 1u << ppq[w2][q + 4];
                            }
                        }
                    pg_pending &= 0x1FFF;
                }
                /* DIRTY-ONLY page sync (write-observer ring): copy at
                 * most 3 pending pages per k1 — steady state is ZERO
                 * (1988 design preloads rounds; tile writes happen at
                 * transitions). This replaces copy_pages(0,6) master +
                 * (6,13) slave EVERY cycle: the k1 FM-hold drops from
                 * 8-15ms on ares (the 67%-gate-reject cadence spiral,
                 * ares verdict d6ed14ac) to ~2ms. */
                tp = frt();
                {
                    /* ADAPTIVE: per-frame animators that mark ALL-dirty
                     * (the 0x258A table blitter drives the title
                     * backdrop every frame) flooded the 3-page budget —
                     * the shadow lagged the animation by ~5 cycles and
                     * the title showed a stale pale phase (parity 70%).
                     * A flood gets bulk copying (those scenes are
                     * static screens; cadence doesn't matter there);
                     * sparse dirt keeps the ~2ms fast path where
                     * cadence IS the game. */
                    int budget = (pg_pending >= 0x0FFF) ? 7 : 3;
                    for (int pc = 0; pc < budget && pg_pending; pc++) {
                        int pg = 0;
                        while (!(pg_pending & (1u << pg)))
                            pg++;
                        copy_pages(pg, pg + 1);
                        pg_pending &= (uint16_t)~(1u << pg);
                    }
                }
                diag_add(0, tp);
                par ^= 1;                    /* now composing the next frame */
                tp = frt();
                apply_cram(par);
                diag_add(2, tp);
            } else {
                if (k == 2) {
                    /* SKIP-RATE BARS (band-staleness debug):
                     *   2 = total in-window time (1px = 87.7us)
                     *   4 = master-side blit skips this cycle x16px
                     *       (each = one band stale a full cycle)
                     *   6 = cumulative master skips (x2px, cap 300) */
                    static uint32_t p0, ps;
                    uint32_t c0v = DIAG[8];
                    int lens[3];
                    lens[0] = (int)((c0v - p0) >> 6);
                    p0 = c0v;
                    lens[1] = (int)((DIAG[7] - ps) * 16);
                    ps = DIAG[7];
                    lens[2] = (int)(DIAG[7] * 2);
                    ((volatile uint16_t *)&MARS_CRAM)[255] = 0x7FFF;
                    for (int j = 0; j < 3; j++) {
                        int len = lens[j];
                        if (len > 300) len = 300;
                        uint8_t *b = sbuf + (8 + 2 + 2 * j) * SBUF_W + 8;
                        for (int i = 0; i < len; i++)
                            b[i] = 0xFF;
                    }
                }
            }

            tp = frt();
            slave_wait(scmd);
            diag_add(3, tp);

            /* launch band R(k)'s FULL concurrent compose (tiles then
             * sprites, slave rows), ack the MD, then do our own rows. */
            tile_cmd = (uint16_t)(CMD_TILE | (k << 4) | (par << 8) | bank1);
            slave_cmd(tile_cmd);
            diag_add(8, tw);
            if (k == 1)
                DIAG[9]++;
            MARS_SYS_COMM0 = 0;              /* ack: MD restores FM/RV, game runs */

            {
                int rg = (k + 2) % 3;        /* window k composes region:
                                              * W1->R0, W2->R1, W0->R2 */
                cache_purge();               /* pages/maps changed in-window:
                                              * cached lines are stale */
                /* COMPLETE-OR-DEFER (accuracy mandate): when the queue
                 * is full (persistently over budget — ares), the NEW
                 * band is simply not enqueued: in-flight bands always
                 * COMPLETE, the region just updates at half cadence
                 * this cycle. Every prior policy that killed partial
                 * bands (drop-oldest, rotation, victim selection,
                 * streak fairness) traded one artifact for another —
                 * stale locked stripes, starved maps, frozen regions,
                 * BG-only rows with the FG phases missing (the "red
                 * box" report). A complete band one cycle late looks
                 * exactly like the arcade one frame ago; a partial
                 * band looks like a broken game. maps_owed machinery
                 * stays for the rare boot/transition races. */
                if (bq[bq_t].on) {
                    DIAG[13]++;              /* queue-full deferrals */
                } else {
                    struct band *nb = &bq[bq_t];
                    nb->on = 1;
                    nb->rg = (uint8_t)rg;
                    nb->bpar = (uint8_t)par;
                    nb->bank = (uint8_t)bank1;
                    nb->phase = 0;
                    nb->s0 = drop_s0[rg];    /* resume rotation point */
                    nb->cnt = 0;
                    bq_t = (bq_t + 1) & 3;
                }
            }
        }
    }
}
