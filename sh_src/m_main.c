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
#define SPR_LAND    ((volatile uint16_t *)0x26039000)   /* DREQ landing, past
                                                         * CACHE_C (ends
                                                         * 0x39000); stack top
                                                         * 0x3FC00, so ~27KB
                                                         * of room. LOOP 7:
                                                         * 852 words (1704B,
                                                         * ends 0x396A8).
                                                         * LOOP 7b ORDER —
                                                         * critical-first:
                                                         * 0..19 layer regs
                                                         * (0x740-0x753),
                                                         * 20..79 rowscroll
                                                         * (0x7C0-0x7FB),
                                                         * 80..591 sprites,
                                                         * 592 bitmap,
                                                         * 593 text base,
                                                         * 594..849 text,
                                                         * 850..851 pad */
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
#ifdef MD_BG
/* Probe builds need ~2KB of .bss headroom for the MD path, so the tag
 * arrays move to fixed SDRAM. NOT 0x27000-0x28000: that block is
 * PAL_SH, the live palette stream target (the earlier "free" claim
 * missed it), and the slave rewrote both tag arrays with palette words
 * every stream batch — random palette values read back as tag "hits",
 * so cells mapped to random slots and the plane rendered noise.
 * 0x3A800 sits above missq (0x3A000 + 2x192 words, ends 0x3A300) and
 * ~18KB below the SP init at 0x3F000 — same fixed-address pattern and
 * coherency story as missq (full cache_purge every window). */
#define cache_tag ((uint16_t *)0x0603A800)          /* NSETS*NWAYS words */
#else
static uint16_t cache_tag[NSETS * NWAYS];   /* folded tile code; 0xFFFF empty */
#endif
#ifdef MD_BG
/* PIVOT: MD VRAM slot == SH-2 cache slot. The cache is already a
 * 1024-entry code->slot map with eviction solved, and 1024 fits inside
 * the 1363 VRAM slots below the name tables, so there is no second
 * allocator. A slot whose pixels change is queued for re-upload. */
/* FIXED SDRAM, not .bss -- 128 bytes there crosses the 0x19000 region
 * guard. 0x28EC0 is past ROWHASH (0x28D00 + 448B = 0x28EC0); the older
 * "28D80-28FFF free" comment above predates ROWHASH and is wrong. */
#define md_dirty ((volatile uint32_t *)0x06028EC0)
/* STABLE (code,set)->slot map for MD VRAM, separate from the render
 * cache. The render cache is transient by design -- it evicted and
 * reassigned slots under the name table, so cells pointed at whatever
 * tile had since taken their slot, and the whole plane rendered as one
 * repeated pattern with misses at 263/cycle. MD residency must be
 * STABLE: a key keeps its slot for as long as it is on screen. Same
 * set/way geometry as the render cache, LRU eviction per set (md_ref).
 * 32-BIT tags: the pattern shipped to VRAM is pen-REMAPPED per S16
 * colour set (see the palette pack below), so the same tile code under
 * two sets is two different patterns — the key is (set<<16)|code. */
#define md_tag ((uint32_t *)0x0603B400)     /* NSETS*NWAYS longs; after
                                             * md_ref (ends 0x3B400) */
#define MD_KEY(code, cset) (((uint32_t)(cset) << 16) | (code))
/* Last-referenced window stamp per slot (low byte of win_no). "First-
 * come, no eviction" did not survive contact: the animated title
 * backdrop cycles ~1120 codes, so it fills all 1024 slots in the first
 * seconds and pins dead codes forever — every later scene allocated
 * nothing and rendered blank bands. A slot on screen is re-stamped by
 * the name-table pass at least every 5 windows, so evicting the oldest
 * way keeps the stability guarantee for everything actually visible. */
#define md_ref ((uint8_t *)0x0603B000)      /* NSETS*NWAYS bytes */
#define MD_MARK(sl) (md_dirty[(sl) >> 5] |= 1u << ((sl) & 31))

/* ---- MD PALETTE PACK (§11: the depth loss does the merging) ----
 * MEASURED (this loop, live PAL_SH walk over the attract scenes): the
 * visible BG window needs at most 21 distinct S16 colour sets but only
 * 36 distinct MD-quantised colours — so a COLOUR-level pack into MD
 * lines 1-3 (3 x 15 usable pens = 45; line 0 stays the grey ramp for
 * the text path) covers the worst scene with room. Per set: a line and
 * an 8-entry pixel->pen remap, applied when the pattern is converted.
 * Pen COLOURS refresh from live PAL_SH every window (48 CRAM words in
 * the packet), so fades track for free; only the merge GROUPING is
 * static between repacks. A round-robin drift check (4 sets/window)
 * reassigns a set whose live colours no longer match its pens.
 * State is master-only, in fixed SDRAM after md_tag (ends 0x3C400). */
#define MDP_LINES  3
#define mdp_line_c ((uint16_t *)0x0603C400) /* [3][16] 9-bit colour, FFFF free */
#define mdp_pen_rc ((uint8_t  *)0x0603C460) /* [3][16] pen refcount */
#define mdp_s_line ((uint8_t  *)0x0603C4A0) /* [128] line+1, 0 = unassigned */
#define mdp_s_map  ((uint8_t  *)0x0603C520) /* [128][8] pixel -> pen */
/* mdp_s_qc is [128][8] of uint16 = 0x800 BYTES. The first layout
 * placed the next array 0x600 after it — s_qc writes for sets >= 96
 * overran the stamps, the pen owners and the debug mirror. Garbage
 * owners fed garbage into the live CRAM refresh, that corrupted
 * mdp_line_c, and the drift check then free/invalidated sets every
 * window: patterns were un-shipped faster than the shipper could ship
 * them and the whole MD plane rendered as backdrop. Verified on both
 * MAME and ares (both showed the void; the fix restores the plane). */
#define mdp_s_qc   ((uint16_t *)0x0603C920) /* [128][8]; ends 0x3D120 */
#define mdp_s_stmp ((uint8_t  *)0x0603D120) /* [128] LRU stamp; ends 3D1A0 */
#define mdp_pen_own ((uint8_t *)0x0603D1A0) /* [3][16][2] owner set,pixel
                                             * -- drives the live CRAM
                                             * refresh; ends 0x3D200 */
#define md_dbg_nt ((uint16_t *)0x0603D200)  /* [2][28][40] mirror of the
                                             * last entry shipped per
                                             * cell, [0]=plane B [1]=A —
                                             * DEBUG ONLY, lua-readable
                                             * (the FB packet is bank-
                                             * blind from lua); ends
                                             * 0x3E380, ~2.6KB under the
                                             * measured master SP floor */
/* Per-set PIXEL USAGE mask (bit v = some resident tile uses value v).
 * Pens are claimed ONLY for used pixels: S16 art leaves garbage in
 * arcade-invisible entries (FG pixel 0 is transparent there; set 76
 * pixel 0 is literal magenta), and claiming pens for all 8 pixels
 * overflowed the lines — real sky colours got nearest-fallback DARK
 * pens that the drift rule then correctly never re-fired (live ==
 * snapshot). Masks are gathered from the ROM tile at claim time; FG
 * claims exclude bit 0 (the shipper forces FG pixel 0 to pen 0). */
#define mdp_s_used ((uint8_t *)0x0603E380)  /* [128]; ends 0x3E400 */
/* Drift-reassign count per set (saturating). A set that keeps drifting
 * is colour-CYCLING; at >= 2 its pens are claimed EXCLUSIVE (free pen
 * before exact-match sharing), so subsequent drifts hit the sole-owner
 * in-place recolour instead of a free+invalidate+re-ship storm. */
#define mdp_s_vol  ((uint8_t *)0x0603E400)  /* [128]; ends 0x3E480 */
#endif
#define cache_rot ((uint8_t *)0x06028880)  /* NSETS<=128B, after blank_tile;
                                            * round-robin eviction way,
                                            * master-only (cache_fill) */

/* Per-CPU miss queues: appended (write-through) during concurrent compose,
 * drained by the master in the next window. */
#define MISSQ_CAP 192   /* was 256; miss rates run 10-36/window.
                         * LOOP 7g trimmed this to 128 for .ramtext space and
                         * ares answered with band-queue deferrals 48 -> 551
                         * and blit skips 21 -> 37% of cycles: dropped fills
                         * become repeated misses and the queue saturates.
                         * 128 IS TOO SMALL — the adaptive cache_fill drain
                         * escalates above 96 COMBINED, so bursts genuinely
                         * exceed it. Free .ramtext instead of this. */
/* FIXED SDRAM, not .bss. The 0x19000 region guard (.bss + .ramtext must
 * not reach the tilemap shadow) had 72 bytes of headroom and LOOP 7g's
 * split-packet apply needed 328; shaving the cap to 128 bought the space
 * and cost band-queue deferrals 48 -> 551 on ares. So move the array out
 * instead of shrinking it. 0x3A000 sits above SPR_LAND (which now needs
 * only 596 words) and ~23KB below the stack top at 0x3FC00 — the same
 * fixed-address pattern as cache_rot and blank_tile, and the cached alias
 * keeps the existing coherency story (full cache_purge every window). */
#define missq ((uint16_t (*)[MISSQ_CAP])0x0603A000)
static volatile uint16_t miss_n[2];

/* Placeholder pixels for uncached tiles: all pen 0 -> the tile color's
 * base entry. Must be RAM (.bss), never ROM — it is read at RV=1. */
/* fixed SDRAM: .bss squeezed by the 0x19000 region guard. LAYOUT:
 * 28000 DIAG | 28100 BM (+alloc state at 28360) | 28400 SPR_SNAP |
 * 28800 SYNC (+blank_tile 28840) | 28900 cram_mirror | 28B00
 * shadow_lut | 28C00 PAL_SETGEN (192 words = 384B, so it really runs to
 * 28D80 — the old "28D00 free" here was off by 0x80) | 28D80-28FFF free.
 * (SPR_LAND, the DREQ DMA target, is NOT here — it lives at 0x39000;
 * this comment claimed 28C00-28FFF for it, which was wrong.)
 * blank_tile is constant zeros after master init (coherent for both
 * CPUs); the grp/pr allocator state is master-only (bm_tail). */
#define blank_tile ((uint8_t *)0x06028840)              /* 64B, after SYNC */

/* LOOP 11 — YIELD FLAG for interrupt-driven window pickup. Set by the
 * CMD ISR (mars_start.s main_cmd_irq) the instant the 68000 signals a
 * window; cleared when the window is picked up. UNCACHED alias on both
 * sides: same CPU, but the ISR writes and the strip loop reads, and a
 * cached read would sit on a stale line until the next purge.
 * Lives in the documented 28D80-28FFF free block. */
#define win_pend ((volatile uint8_t *)0x26028D80)

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

/* Rows of a compose strip run between yield checks. 12 = the old
 * atomic strip (no yielding). 4 bounds the overrun to ~1/3 of a strip
 * at the cost of two extra call setups per strip. */
#ifndef YIELD_ROWS
#define YIELD_ROWS 4
#endif

/* PIVOT: tiles shipped per window, and how many to ship in total. 40
 * tiles = 1280 bytes + a 4-byte header, inside the 1536-byte dead FB
 * block. 1120 covers the visible 40x28 grid; VRAM holds 1363 below the
 * name tables at 0xB000. */
#define MD_BATCH     40
#define MD_TILE_MAX  1120

/* Cache slot for a code, or -1 if it is not resident. Same probe as
 * tile_pixels but without queueing a miss -- the name table must not
 * generate cache traffic just by being built. */
#define MD_BLANK_SLOT 1023

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

#ifdef MD_BG
/* S16B palette word -> 9-bit MD-quantised colour (bbbgggrrr). Channel
 * layout per s16_to_mars: 4 bits + LSB in bits 12-14. ROUND to the
 * nearest 3-bit level, never truncate: S16 art dithers adjacent pens 1
 * LSB apart (the stage-1 sky is a 5/6 checker), and truncation renders
 * a straddling pair at 4x its arcade contrast — the sky came out as a
 * visible checkerboard. Rounding recentres the buckets so 1-LSB pairs
 * collapse back to one colour almost everywhere, and fixes the global
 * darkening truncation caused. */
static uint16_t mdp_quant(uint16_t v)
{
    unsigned r = ((((v)       & 0xF) << 1) | ((v >> 12) & 1)) + 2;
    unsigned g = ((((v >> 4)  & 0xF) << 1) | ((v >> 13) & 1)) + 2;
    unsigned b = ((((v >> 8)  & 0xF) << 1) | ((v >> 14) & 1)) + 2;
    r >>= 2; g >>= 2; b >>= 2;
    if (r > 7) r = 7;
    if (g > 7) g = 7;
    if (b > 7) b = 7;
    return (uint16_t)((b << 6) | (g << 3) | r);
}

/* Release a set's pens and INVALIDATE its VRAM slots: the pattern in
 * VRAM is pen-remapped under the old assignment, and the (code,set)
 * key would keep matching after a re-assign with a different remap —
 * the cells re-claim, re-mark dirty, and the shipper re-converts. */
static void mdp_free_set(unsigned s)
{
    if (!mdp_s_line[s])
        return;
    unsigned l = (unsigned)(mdp_s_line[s] - 1);
    for (int p = 0; p < 8; p++) {
        unsigned pen;
        if (!(mdp_s_used[s] & (1u << p)))
            continue;                        /* never claimed a pen */
        pen = mdp_s_map[s * 8 + p];
        if (mdp_pen_rc[l * 16 + pen] && !--mdp_pen_rc[l * 16 + pen])
            mdp_line_c[l * 16 + pen] = 0xFFFF;
    }
    mdp_s_line[s] = 0;
    mdp_s_used[s] = 0;
    for (int i = 0; i < NSETS * NWAYS; i++)
        if (md_tag[i] != 0xFFFFFFFFu && ((md_tag[i] >> 16) & 0x7F) == s)
            md_tag[i] = 0xFFFFFFFFu;         /* both planes' variants */
    DIAG[37]++;                              /* set frees/invalidations */
}

/* Claim a pen in line l for quantised colour q; returns the pen and
 * bumps its refcount. Exact match first, then a free pen, then the
 * nearest occupied pen (counted — should be rare once pens are only
 * claimed for used pixels). */
static unsigned mdp_claim_pen(unsigned l, uint16_t q, unsigned s, unsigned p)
{
    unsigned pen16 = 0, freepen = 0;
    for (unsigned pen = 1; pen < 16; pen++) {
        if (mdp_line_c[l * 16 + pen] == q) { pen16 = pen; break; }
        if (!freepen && mdp_line_c[l * 16 + pen] == 0xFFFF)
            freepen = pen;
    }
    if (mdp_s_vol[s] >= 2 && freepen)
        pen16 = 0;                           /* volatile set: prefer an
                                              * EXCLUSIVE pen over sharing */
    if (!pen16 && freepen) {
        pen16 = freepen;
        mdp_line_c[l * 16 + pen16] = q;
        mdp_pen_own[(l * 16 + pen16) * 2]     = (uint8_t)s;
        mdp_pen_own[(l * 16 + pen16) * 2 + 1] = (uint8_t)p;
    }
    if (!pen16) {
        unsigned bd = 0xFFFF;
        for (unsigned pen = 1; pen < 16; pen++) {
            uint16_t c = mdp_line_c[l * 16 + pen];
            int dr, dg, db;
            unsigned d;
            if (c == 0xFFFF) continue;
            dr = (int)(c & 7) - (int)(q & 7);
            dg = (int)((c >> 3) & 7) - (int)((q >> 3) & 7);
            db = (int)((c >> 6) & 7) - (int)((q >> 6) & 7);
            d = (unsigned)(dr * dr + dg * dg + db * db);
            if (d < bd) { bd = d; pen16 = pen; }
        }
        if (!pen16) pen16 = 1;
        DIAG[36]++;                          /* nearest-colour fallbacks */
    }
    mdp_pen_rc[l * 16 + pen16]++;
    return pen16;
}

/* Add newly-seen pixels of an already-assigned set (a fresh tile uses
 * values earlier residents did not). */
static void mdp_extend_set(unsigned s, uint8_t addmask)
{
    unsigned l = (unsigned)(mdp_s_line[s] - 1);
    for (int p = 0; p < 8; p++) {
        uint16_t q;
        if (!(addmask & (1u << p)))
            continue;
        q = mdp_quant(PAL_SH[s * 8 + p]);
        mdp_s_map[s * 8 + p] = (uint8_t)mdp_claim_pen(l, q, s, (unsigned)p);
        mdp_s_qc[s * 8 + p]  = q;
    }
    mdp_s_used[s] |= addmask;
}

static void mdp_assign_set(unsigned s, uint8_t stamp, uint8_t mask);

/* A tile is being claimed for MD residency: fold its pixel values into
 * the set's usage mask and assign/extend the pen mapping to cover
 * exactly the used pixels. FG claims drop bit 0 — the shipper forces
 * FG pixel 0 to pen 0 (transparent), so its (often garbage) colour
 * must not spend a pen. */
static void mdp_note_tile(unsigned cset, unsigned code, int isfg,
                          uint8_t stamp)
{
    const uint8_t *tp = altbeast_tiles + code * 64;
    uint8_t mask = 0;
    for (int i = 0; i < 64; i++)
        mask |= (uint8_t)(1u << tp[i]);
    if (isfg)
        mask &= 0xFE;
    if (!mdp_s_line[cset])
        mdp_assign_set(cset, stamp, mask);
    else if (mask & (uint8_t)~mdp_s_used[cset])
        mdp_extend_set(cset, mask & (uint8_t)~mdp_s_used[cset]);
}

/* Assign a set to a line, claiming pens ONLY for `mask` pixels: greedy
 * best-fit by fewest new pens, evicting LRU sets from the best line if
 * it is full, nearest-colour fallback as last resort. Always succeeds. */
static void mdp_assign_set(unsigned s, uint8_t stamp, uint8_t mask)
{
    uint16_t qc[8];
    int bestl = 0, bestneed = 99, bestfit = 0;
    for (int p = 0; p < 8; p++)
        qc[p] = mdp_quant(PAL_SH[s * 8 + p]);

    for (int l = 0; l < MDP_LINES; l++) {
        int need = 0, freep = 0, fits;
        for (int pen = 1; pen < 16; pen++)
            if (mdp_line_c[l * 16 + pen] == 0xFFFF)
                freep++;
        for (int p = 0; p < 8; p++) {
            int dup = 0, found = 0;
            if (!(mask & (1u << p)))
                continue;
            for (int p2 = 0; p2 < p; p2++)
                if ((mask & (1u << p2)) && qc[p2] == qc[p]) dup = 1;
            if (dup) continue;
            for (int pen = 1; pen < 16; pen++)
                if (mdp_line_c[l * 16 + pen] == qc[p]) { found = 1; break; }
            if (!found) need++;
        }
        fits = need <= freep;
        /* prefer any fitting line over any non-fitting one, then fewest
         * new pens */
        if ((fits && !bestfit) || (fits == bestfit && need < bestneed)) {
            bestl = l; bestneed = need; bestfit = fits;
        }
    }
    if (!bestfit) {
        /* evict LRU sets from the chosen line until it fits or nothing
         * evictable is left */
        for (;;) {
            unsigned victim = 128, vage = 0;
            for (unsigned s2 = 0; s2 < 128; s2++) {
                unsigned age;
                if (s2 == s || mdp_s_line[s2] != (uint8_t)(bestl + 1))
                    continue;
                age = (uint8_t)(stamp - mdp_s_stmp[s2]);
                /* >= 12: a set's cells re-stamp every <= 9 windows now
                 * (1 tile + 4 B + 4 A rotation) — the old >= 8 gate
                 * evicted LIVE sets and churned frees/reassigns */
                if (age >= 12 && age >= vage) { vage = age; victim = s2; }
            }
            if (victim == 128)
                break;
            mdp_free_set(victim);
            {
                int freep = 0;
                for (int pen = 1; pen < 16; pen++)
                    if (mdp_line_c[bestl * 16 + pen] == 0xFFFF)
                        freep++;
                if (bestneed <= freep)
                    break;
            }
        }
    }
    for (int p = 0; p < 8; p++) {
        if (!(mask & (1u << p)))
            continue;
        mdp_s_map[s * 8 + p] =
            (uint8_t)mdp_claim_pen((unsigned)bestl, qc[p], s, (unsigned)p);
        mdp_s_qc[s * 8 + p] = qc[p];
    }
    mdp_s_line[s] = (uint8_t)(bestl + 1);
    mdp_s_used[s] = mask;
    mdp_s_stmp[s] = stamp;
    DIAG[35]++;                              /* set assigns */
}
#endif

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

    /* LOOP 10 — PUBLISH STICKY OWNERSHIP THE PRESCAN DID NOT COUNT.
     * The passes above only map colours this prescan actually SAW
     * (tcount[c] != 0). Ownership, though, is sticky for ~90 cycles, so
     * a colour that scrolled in late, or whose scan was owed, can still
     * OWN a group while its map entry stays 0xFF. That was a deadlock:
     * apply_cram paints only the groups tile_grp[par] names, so the
     * group nobody mapped was never painted, and compose then fell back
     * to group 1 — a live group holding an unrelated colour, which is
     * where the white slabs in the tree band and the yellow gravestone
     * flash came from. Measured with the PALMISS probe: 20% of prescan
     * misses were colours that still owned a group nobody was painting.
     * Publishing the ownership closes the loop — apply_cram paints the
     * group AND compose finds it. */
    for (int g = 1; g < 32; g++) {
        uint8_t c = grp_key[g];
        if (c != 0xFF && grp_kind[g] == 0 && tile_grp[par][c] == 0xFF)
            tile_grp[par][c] = (uint8_t)g;
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
/* LOOP 6: the store is GATED on the mirror. apply_cram rewrote all
 * ~2112 mapped CRAM entries every k1 — unconditionally, pre-ack, inside
 * the FM-hold — while this exact compare was already being computed and
 * spent only on shadow_dirty. CRAM is real retaining RAM and these are
 * its ONLY writers (the two mirror-bypassing paths below now keep the
 * mirror coherent), so gating produces BYTE-IDENTICAL CRAM contents and
 * simply deletes the redundant traffic. Steady state (no fade) = zero
 * CRAM writes. This is removed FM-hold work, not redistributed —
 * LOOP.md's law for the ares cadence fix. */
static inline void cram_set(volatile uint16_t *dst, int idx, uint16_t v)
{
    if (cram_mirror[idx] != v) {
        cram_mirror[idx] = v;
        dst[0] = (uint16_t)(v & 0x7FFF);
        shadow_dirty = 1;
        DIAG[19]++;                     /* CRAM writes actually performed */
    }
}

/* Convert-and-store one colour-set's block: the inner half of apply_cram,
 * factored out so the land-time painter below runs BYTE-IDENTICAL code
 * rather than a second transcription of the same arithmetic. */
RAMCODE static void cram_paint(volatile uint16_t *dst, volatile uint16_t *src,
                               int base, int n)
{
    for (int p = 0; p < n; p++)
        cram_set(dst + p, base + p,
                 (uint16_t)(s16_to_mars(src[p]) | (src[p] & 0x8000)));
}

/* LOOP 6 PER-GROUP MEMO. apply_cram ran its full ~2112-entry convert-and-
 * store every k1, pre-ack, inside the FM-hold. Gating the STORES alone
 * (cram_set) killed 98% of the CRAM traffic but not the s16_to_mars
 * arithmetic, which is the bulk of the 0.67ms/cycle MAME measures. A
 * whole-pass gate does not work either: the allocator reshuffles the
 * color->group MAPPING nearly every frame (measured: 13 skips in 1195
 * cycles), even though ~29 of 2112 entries actually change.
 *
 * So memoize PER CRAM GROUP: a group's 8/16 entries can only differ if a
 * DIFFERENT color-set now owns it, or the source palette moved (PAL_GEN,
 * bumped by the slave on every COMM palette batch — zero in steady state).
 * Indexed by 8-entry CRAM slot (32 of them); a sprite pair covers two.
 * The key carries a KIND tag so tile set 3, text set 3 and sprite set 3
 * can never alias into a false hit. cram_set's mirror still backstops
 * correctness, so an over-eager memo can only ever cost work, not truth. */
#define PAL_SETGEN ((volatile uint16_t *)0x26028C00) /* slave-written, 192 */
#define CK_TILE 0x0000u
#define CK_TEXT 0x1000u
#define CK_SPR  0x2000u
static uint16_t cram_key[32];               /* (kind|color-set) per slot */
static uint16_t cram_keygen[32];            /* that set's gen when painted */
static volatile uint8_t cram_hijacked;      /* k2 debug bar clobbered CRAM 255 */

/* Returns 1 if slot already holds color-set `set` at its current
 * generation. The generation is PER SET, so a fade on one color-set no
 * longer invalidates every other group's memo. */
static inline int cram_memo(int slot, uint16_t key, unsigned set)
{
    uint16_t gen = PAL_SETGEN[set];
    if (cram_key[slot] == key && cram_keygen[slot] == gen)
        return 1;
    cram_key[slot] = key;
    cram_keygen[slot] = gen;
    return 0;
}

/* LOOP 10 — PAINT AT LAND TIME. apply_cram runs only at k1, but a palette
 * region pair ships in whichever window it lands: at k0 or k2 the new
 * words sat in PAL_SH while CRAM kept last generation's colours, and the
 * per-set memo then held that stale paint until the NEXT bump. On ares
 * that read as a purple hue on sprites — LOOP 8 logged it as a
 * MAME-PRESSURE-only artifact, which was wrong.
 * The land site is pre-ack (FM still held), so writing CRAM there is
 * exactly as legal as apply_cram's own writes, and it goes through the
 * same memo — so k1 then SKIPS what has already been painted rather than
 * doing it twice. Only the sets that actually changed are touched.
 * `par` here is the parity apply_cram painted at the last k1 and the one
 * the slave is NOT rebuilding (it builds par^1), so the group mapping we
 * read is the mapping CRAM currently holds.
 * SPRITE SETS ONLY — see the caller for why tile/text stays at k1. */
RAMCODE static void cram_paint_spr(int par, unsigned sc)
{
    DIAG[52]++;                          /* sprite sets painted at land time.
                                          * 52, NOT 50: SPAN_PROBE owns
                                          * [34..51] and a shipping counter
                                          * sharing a probe slot corrupts
                                          * both readings. */
    uint8_t pr = spr_pair[par][sc];
    if (pr == 0xFF)
        return;
    uint16_t key = (uint16_t)(CK_SPR | sc);
    int hit = cram_memo(pr * 2, key, 128 + sc);
    hit &= cram_memo(pr * 2 + 1, key, 128 + sc);  /* both slots, no shortcut */
    if (!hit) {
        volatile uint16_t *cram = &MARS_CRAM;
        cram_paint(cram + pr * 16, PAL_SH + 1024 + sc * 16, pr * 16, 16);
    }
}

RAMCODE static void apply_cram(int par)
{
    /* Undo the k2 debug-bar hijack of entry 255 from the mirror (which
     * still holds the true color) — one write, outside the memo. */
    if (cram_hijacked) {
        ((volatile uint16_t *)&MARS_CRAM)[255] = (uint16_t)(cram_mirror[255]
                                                            & 0x7FFF);
        cram_hijacked = 0;
    }
    volatile uint16_t *cram = &MARS_CRAM;
#ifdef MD_BG
    /* PIVOT SLICE 1a — THE THROUGH BIT. On the 32X, transparency is
     * bit 15 of the CRAM ENTRY, not "pixel index == 0". CRAM[0] has
     * always been 0x0000 here, i.e. opaque black, which is exactly why
     * a dropped bank shows as a black frame (NOTES: "through-bit clear
     * = opaque black"). The allocator already reserves group 0 so no
     * composed pixel is ever index 0 -- the slot was kept for this and
     * never armed. Set the through bit and index 0 becomes MD video. */
    cram[0] = 0x8000;
#endif
    for (int c = 0; c < 128; c++) {
        uint8_t g = tile_grp[par][c];
        if (g == 0xFF)
            continue;
        if (cram_memo(g, (uint16_t)(CK_TILE | c), (unsigned)c)) {
            DIAG[20]++;                      /* groups skipped */
            continue;
        }
        cram_paint(cram + g * 8, PAL_SH + c * 8, g * 8, 8);
    }
    for (int c = 0; c < 8; c++) {
        uint8_t g = text_grp[par][c];
        if (g == 0xFF)
            continue;
        if (cram_memo(g, (uint16_t)(CK_TEXT | c), (unsigned)c)) {
            DIAG[20]++;
            continue;
        }
        cram_paint(cram + g * 8, PAL_SH + c * 8, g * 8, 8);
    }
    for (int sc = 0; sc < 64; sc++) {
        uint8_t pr = spr_pair[par][sc];
        if (pr == 0xFF)
            continue;
        /* a 16-entry pair spans two 8-entry slots; both must agree */
        uint16_t key = (uint16_t)(CK_SPR | sc);
        int hit = cram_memo(pr * 2, key, (unsigned)(128 + sc));
        hit &= cram_memo(pr * 2 + 1, key, (unsigned)(128 + sc));
        if (hit) {
            DIAG[20]++;
            continue;
        }
        cram_paint(cram + pr * 16, PAL_SH + 1024 + sc * 16, pr * 16, 16);
    }
    /* Shadow ramp: when pair 15 has no real owner this frame, its
     * pens 1-14 go near-black so shadow sprites (and over-budget
     * spillover) draw as dark silhouettes. Entry 255 stays free for
     * the debug bar hijack. */
    int p15_used = 0;
    for (int sc = 0; sc < 64; sc++)
        if (spr_pair[par][sc] == 15) { p15_used = 1; break; }
    /* MIRROR-COHERENT (LOOP 6): the ramp writes CRAM directly, so it must
     * publish what it wrote — otherwise the gated cram_set above would
     * later see a stale "already correct" mirror for 241-254 and skip
     * restoring pair 15's real colors when a sprite reclaims it. */
    if (!p15_used) {
        for (int p = 1; p < 15; p++)
            if (cram_mirror[240 + p] != 0x0842) {
                cram_mirror[240 + p] = 0x0842;
                cram[240 + p] = 0x0842;
                shadow_dirty = 1;
            }
        /* the ramp owns slots 30/31 now — drop their memo so a sprite
         * that later reclaims pair 15 is guaranteed to repaint them */
        cram_key[30] = cram_key[31] = 0xFFFF;
    }
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
                unsigned tc = ((unsigned)w >> 6) & 0x7F;
                uint8_t g = tg[tc];
                if (g == 0xFF) {
                    /* LOOP 10 — A MISS SKIPS THE TILE, BUT ONLY IN THE
                     * NON-OPAQUE PASSES. Skipping renders nothing and
                     * leaves last cycle's pixels, which beats drawing in
                     * group 1 (a live group holding an unrelated colour —
                     * the white slabs and yellow gravestones).
                     * BUT SKIP MEANS NEVER UPDATE. The opaque BG pass is
                     * the one that must write EVERY pixel; skip there and
                     * a colour that keeps missing freezes whatever sbuf
                     * held forever. One bad frame during an area
                     * transition — tilemap mid-load, placeholder codes —
                     * became permanent: a repeating glyph tiled over sky,
                     * cliff and ground alike, on Mike's level-1 pass.
                     * So: FG passes skip (stale beats wrong colour),
                     * BG stays live (wrong colour beats frozen). */
                    tmiss[c] = !opaque;
                    g = 1;
                }
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
/* PRESENTATION 2.0: capture is now COPY-AND-COMPARE (same trick as the
 * palette pair apply) and reports whether the page's content moved. The
 * report drives the pg_watch set: the game's big staging writers (the
 * RLE passes, the clear-alls, the 0x258A table blitter) mark dirty ONCE
 * at their pointer load and then stream stores for many vints — a scene
 * load runs ~a second. A flip mid-stream would split the stream across
 * banks with no further mark to say so; watching every page until two
 * consecutive captures agree keeps it captured pre-flip and restored
 * post-flip for as long as it is actually moving, so the draw bank
 * always evolves exactly as a single-banked staging would. The compare
 * is one extra SDRAM read per long against the FB read we already pay —
 * noise. */
static uint16_t pg_watch;                    /* see cap_page */
/* (A debug pair briefly lived at 0x26028F10 — INSIDE md_dirty
 * (0x28EC0..0x28F3F). Fifth slot collision of the era; audit the block
 * before parking anything in the "28D00 hole": ROWHASH ends 0x28EC0,
 * md_dirty runs to 0x28F40, FMT/WSPL overlay 0x28F00/0x28F20 under
 * their probe flags. Truly free: 0x28F40..0x28F4F only.) */
static uint16_t pg_pending;                  /* dirty pages awaiting capture
                                              * (file-scope so cap_drain can
                                              * own the scan loop once — the
                                              * 3 inline copies overflowed
                                              * the region guard) */
RAMCODE static void cap_page(int pg)
{
    volatile uint32_t *src = (volatile uint32_t *)(FB_STAGING + pg * 0x800);
    volatile uint32_t *dst = (volatile uint32_t *)(TILEMAP_U + pg * 0x800);
    uint32_t ch = 0;
    CAP_RUNS++;
    for (int i = 0; i < 0x400; i += 4) {
        uint32_t v0 = src[i + 0], v1 = src[i + 1];
        uint32_t v2 = src[i + 2], v3 = src[i + 3];
        ch |= v0 ^ dst[i + 0]; dst[i + 0] = v0;
        ch |= v1 ^ dst[i + 1]; dst[i + 1] = v1;
        ch |= v2 ^ dst[i + 2]; dst[i + 2] = v2;
        ch |= v3 ^ dst[i + 3]; dst[i + 3] = v3;
    }
    if (ch)
        pg_watch |= (uint16_t)(1u << pg);
    else
        pg_watch &= (uint16_t)~(1u << pg);
}

RAMCODE static void cap_drain(int budget)
{
    for (int pc = 0; pc < budget && pg_pending; pc++) {
#ifdef TILE_RATE
        DIAG[54]++;                          /* pages actually copied */
#endif
        int pg = 0;
        while (!(pg_pending & (1u << pg)))
            pg++;
        cap_page(pg);
        pg_pending &= (uint16_t)~(1u << pg);
    }
}

/* PRESENTATION 2.0 — the reverse of copy_pages: replay TILEMAP_U (the
 * SDRAM truth copy_pages maintains) into the game's staging pages of the
 * bank the FB window CURRENTLY maps. Called once per k2 flip, on the NEW
 * draw bank, for exactly the pages the game dirtied since the previous
 * flip (cycle_dirt) — the game's own read-backs (collision tst.w against
 * live tilemap pages, the 1KB scratch save/restore in page 1) then see
 * the same bytes in either bank. Writes ride the cached write-through
 * alias like blit_half (stores use the 4-deep write buffer; write-only,
 * no stale-read hazard). MUST run before the window's ack: the game only
 * writes staging post-ack, so restore-then-ack makes clobbering a fresh
 * game write impossible by construction. Steady state is zero pages
 * (1988 preload design); transition bursts (~10 ares lines/page) land
 * while the game is fading. */
RAMCODE static void restore_pages(uint16_t bm)
{
    for (int pg = 0; pg < 13; pg++) {
        if (!(bm & (1u << pg)))
            continue;
        volatile uint32_t *src = (volatile uint32_t *)(TILEMAP_U + pg * 0x800);
        volatile uint32_t *dst = (volatile uint32_t *)
            (0x04012000u + (unsigned)pg * 0x1000u);
        for (int i = 0; i < 0x400; i += 4) {
            dst[i + 0] = src[i + 0];
            dst[i + 1] = src[i + 1];
            dst[i + 2] = src[i + 2];
            dst[i + 3] = src[i + 3];
        }
    }
}

/* LOOP 9 ROWSTALE_PROBE — is a dirty-row blit worth building?
 * MEASURED ON ARES: the blit costs 13.6 SH-2 cycles per longword, of
 * which only 2.7 is instruction issue (that is the whole MAME figure).
 * Cached and uncached FB writes read 47.34 vs 47.46 us/row — the write
 * buffer buys nothing, so 80% of the blit is a bus-stall floor that no
 * instruction-side rewrite (DMAC included) can move. The only lever
 * left is writing FEWER BYTES, and an SDRAM read is ~5x cheaper than an
 * FB write, so comparing every row pays for itself if more than ~25%
 * of rows can be skipped.
 * This counts, per master row, whether the composed content is
 * IDENTICAL to the same row one k1 cycle ago. It changes no behaviour
 * — it only tells us whether the skip rate clears that 25% bar.
 * [32] rows identical, [33] rows checked. 32-bit hash so a collision
 * cannot fake an "unchanged" row (which would bias the answer toward
 * yes, the direction that costs a wasted arc). Master rows only —
 * 36..71, 108..143, 184..223 fold to 0..111, 448B in the 28D00 hole. */
#define ROWHASH ((uint32_t *)0x06028D00)          /* 112 entries, 448B */
/* LOOP 9 FM_TEST results. NOT in DIAG: that block is 0x28000..0x280FF,
 * 64 slots, and slot 61 is the last one free. This lives in the
 * 28D00-28FFF hole above ROWHASH, uncached so lua/python read it the
 * same way. [0] FM=0 writes that landed, [1] attempts, [2] the FM=1
 * positive control, [3] FM=0 reads that returned the right value. */
#define FMT ((volatile uint32_t *)0x26028F00)
/* LOOP 13 part 4 — DREQ RESIDUE SPLIT (0x28F80, clear of PSRC[0..5]
 * at 0x28F50). When a k1 window finds the sprite push incomplete
 * (DIAG[17]), the TCR0 residue names the mechanism:
 *   [0] residue == 256 exactly  -> PHASE DESYNC: the MD pushed the
 *       340-word TEXT layout while this side expected SPRITE/596 —
 *       a wskip/prev_k disagreement, not a transport failure;
 *   [1] residue 1..8            -> tail-drain stall (the FIFO's last
 *       groups never got DMAC service before this window);
 *   [2] any other residue       -> mid-stream stall / wild TCR;
 *   [3] last residue  [4] max residue.
 * The 68K is already exonerated (push_aborts true-0, spin headroom
 * 2597/2600 on ares bs9 BUILD 1426c951) — this decides the rest. */
#define DRQR ((volatile uint32_t *)0x26028F80)
/* LOOP 9 WAIT_SPLIT_PROBE. k=0 and k=2 hold at 27.3-27.6 lines of span
 * across a light session and a heavy one; k=1 went 31.2 -> 35.2 and owns
 * 100% of the past-vblank restores in both. Four extra rows is a FIXED
 * ~3 lines, so a penalty that doubles under load is not the row count.
 * Inside the pickup->restore span, k=1 differs from the others in only
 * two ways: those 4 rows, and the master's wait on SYNC[2] for the slave
 * to pick up the preempt mailbox — which the slave services BETWEEN
 * COMPOSE STRIPS, making it load-dependent by construction.
 * [0..2] master blit ticks by k, [3..5] SYNC[2] wait ticks by k,
 * [6..8] windows by k. 36 bytes at 28F20, inside the 28D00-28FFF hole. */
#define WSPL ((volatile uint32_t *)0x26028F20)
/* LOOP 9 PICKUP_SRC_PROBE. WHERE does the slave answer SYNC[4] from?
 * m_main.c:1677 maps the compose launched at window k to band R((k+2)%3),
 * so the compose running during window k=1 is R2 — rows 144-184 on the
 * slave — and slave_window_k at k=1 blits rows 144-184. THE SAME ROWS.
 * That makes the k=1 wait a DATA DEPENDENCY (finish composing R2 before
 * you may blit it), not a mailbox-polling delay, and the two want
 * opposite fixes: more service points cannot help a dependency, which
 * is exactly how 7f failed at "bound the pickup latency".
 * [0..2] pickups from INSIDE the concurrent compose, by k;
 * [3..5] pickups from the s_main IDLE loop (compose already done), by k.
 * Idle-loop pickups at k=0/k=2 and in-compose pickups at k=1 confirm it. */
#define PSRC ((volatile uint32_t *)0x26028F50)
extern volatile uint8_t slave_in_compose;
/* blit_half runs on BOTH CPUs (the slave owns 0..35, 72..107, 144..183),
 * so the probe must fold master rows only — a slave row would index the
 * table out of range and both CPUs would race the same DIAG words. */
#ifdef ROWSTALE_PROBE
__attribute__((always_inline))
static inline int rowslot(int y)
{
    if (y >= 36 && y < 72)   return y - 36;       /* 36..71   -> 0..35  */
    if (y >= 108 && y < 144) return y - 108 + 36; /* 108..143 -> 36..71 */
    if (y >= 184 && y < 224) return y - 184 + 72; /* 184..223 -> 72..111 */
    return -1;                                    /* slave row: skip */
}
#endif

/* DMAC CHANNEL 1 FOR THIS BLIT IS RETIRED, NOT UNTRIED: built, correct
 * (statics pixel-identical), and 1.77x SLOWER on ares — 47.34 -> 83.95
 * us/row with 14% of rows never raising TE. Retired from the tree rather
 * than left behind a flag that would hand someone a 1.77x-slower rom;
 * LOOP.md negative 21 carries the numbers and the two traps (CHCR TS
 * bits, per-CPU DMAOR) so it does not have to be rebuilt to be believed.
 * Likewise BLITUNC: cached and uncached FB writes measure 47.34 vs 47.46
 * on ares, so the write-buffer premise below is false — but the CONCLUSION
 * (blit with CPU stores) is right. */
RAMCODE static void blit_half(int ylo, int yhi)
{
    for (int y = ylo; y < yhi; y++) {
        const uint32_t *src = (const uint32_t *)(sbuf + (8 + y) * SBUF_W + 8);
#ifdef MD_PAYOFF
        /* PIVOT PAYOFF PROBE. Costs a full extra read pass over every
         * row, so it INFLATES the blit -- never measure blit time with
         * this on. Moving the BG to the MD does NOT shrink
         * the blit on its own -- the blit ships the whole staging
         * buffer regardless, and that is 28.5 of the 38-line window and
         * the reason a frame costs 3 vints. The win only arrives if
         * enough rows go ENTIRELY transparent (every pixel index 0, the
         * MD-through value) that we can stop shipping them. Section 4
         * assumes the residual falls to ~0.26 of a screen; LOOP 9's
         * dirty-row blit found only 13-17% skippable, but that was with
         * the BG still in the framebuffer dirtying everything.
         * [62] rows checked, [63] rows fully transparent. */
        {
            /* [60]/[61] longs = transparent AREA (what section 4's
             * "0.26 of a screen" actually refers to).
             * [62]/[63] 32-pixel groups = what a blit could REALISTICALLY
             * skip; per-long branching costs more than the store. */
            uint32_t any = 0;
            for (int g = 0; g < 10; g++) {
                uint32_t ga = 0;
                for (int i = 0; i < 8; i++) {
                    uint32_t v = src[g * 8 + i];
                    ga |= v;
                    DIAG[60]++;
                    if (!v) DIAG[61]++;
                }
                DIAG[62]++;
                if (!ga) DIAG[63]++;
                any |= ga;
            }
            (void)any;
        }
#endif
#ifdef ROWSTALE_PROBE
        {
            int sl = rowslot(y);
            if (sl >= 0) {
                uint32_t h = 0;
                for (int i = 0; i < 80; i++)
                    h = (h << 5) - h + src[i];    /* h*31 + v, no libgcc */
                DIAG[33]++;
                if (ROWHASH[sl] == h) DIAG[32]++;
                ROWHASH[sl] = h;
            }
        }
#endif
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
 * inside the window — the 68K is stalled, no batches arrive.
 * SYNC[4]/[5] (iter4): the PREEMPT-BLIT mailbox. The master posts the
 * per-window blit command in SYNC[4] instead of draining the slave's
 * concurrent compose first; the slave services SYNC[4] between compose
 * strips (slave_service_stream) and echoes SYNC[5] when its blit path
 * is done. This removes the full-compose slave_wait from the 68K's
 * pre-ack critical path (LOOP.md iter4: retry-loop saturation). */
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
    /* LOOP 7d THIRDS. The claim above — "~0.8ms, inside vblank even at
     * ares speed" — is MEASURED FALSE: 845 of 845 blit windows finished
     * their flip/restore pair OUTSIDE vblank, worst 55 lines against a
     * 38-line budget, and that overrun IS the black strobe frame (ares
     * defers an out-of-vblank FBCTL write to the next vblank, putting the
     * never-composed bank on screen for a whole frame).
     * Same 224 rows per cycle, spread over THREE windows instead of two,
     * so each pair carries ~2/3 the rows. Aligned to the band regions
     * (R0=[0,72) R1=[72,144) R2=[144,224)) and shipped one window AFTER
     * the window that composes them — W1 composes R0, W2 ships it; W2
     * composes R1, W0 ships it; W0 composes R2, W1 ships it. Blit and
     * concurrent compose stay disjoint by construction, exactly as
     * before. Cost: two mid-screen seams for one frame instead of one.
     * Total blit work per cycle is UNCHANGED — this is a redistribution,
     * so the 68K's per-cycle stall does not grow. */
    if (!skip) {
        if (k == 2)
            blit_half(0, 36);
        else if (k == 0)
            blit_half(72, 108);
        else
            blit_half(144, 184);
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

/* LOOP 10: the band whose cat1+text is owed to the NEXT window gap.
 * SLAVE-ONLY state — written and read exclusively inside
 * slave_concurrent_k, so there is no cross-CPU coherency question. */
#ifndef NOCAT1DEFER
#define NOCAT1DEFER 0
#endif
#ifdef MD_BG
/* Packet staging: built in the post-ack gap, published in-window
 * by a bare copy. .bss (probe builds only; region guard checked). */
static uint16_t md_pkt[768];
#endif
static uint8_t cat1_valid, cat1_lo, cat1_hi, cat1_bank, cat1_par;
static uint8_t cat1_t0, cat1_t1;

RAMCODE void slave_concurrent_k(uint16_t cmd)
{
    /* Full band compose, slave rows: tiles (BG opaque + FG cat0),
     * then sprites + FG cat1 + text over them. Short strips so the
     * MD stream stays serviced. */
    extern void slave_service_stream(void);
    int k = (cmd >> 4) & 3;
    int par = (cmd >> 8) & 1;
    uint16_t bank1 = cmd & 7;
    /* LOOP 9 — COMPOSE_LEAD2. The comment at slave_window_k claims the
     * blit set and the outstanding compose are "DISJOINT by pipeline
     * construction". MEASURED FALSE, and it is the strobe: window k
     * ships R((k+1)%3), and the compose launched at k-1 covers
     * R((k-1+2)%3) — the SAME BAND, in all three windows. k=0 and k=2
     * only get away with it because their 72-row bands finish inside one
     * window gap; R2 is 80 rows and does not, so 32.2% of k=1 windows
     * pick up mid-compose and the master sits on SYNC[2] (6.39 lines
     * against 0.21 for the other two — LOOP.md negative 25).
     * rg = k instead: the outstanding compose is then R(k-1) against a
     * ship of R(k+1), disjoint in every window, and each band gets TWO
     * window-gaps to compose. No second compose slot is needed — the
     * post-ack drain still bounds it after one gap; only the SHIP moves
     * a window later. Cost: one window (~1 frame) of extra latency,
     * applied UNIFORMLY, so the spread between bands — which is what the
     * seams are made of — is unchanged. */
    int rg = k;                                  /* W0->R0, W1->R1, W2->R2 */
    int lo = rg * 72;
    int hi = (rg == 2) ? 184 : lo + 36;          /* slave rows of R(rg) */
#ifdef PICKUP_SRC_PROBE
    slave_in_compose = 1;
#endif
    cache_purge();
    /* LOOP 10 — CAT1 RUNS AT THE HEAD OF THE GAP, NOT THE TAIL.
     * MEASURED (tools/pk_pass.py, ares, Mike's dogs run): of the master's
     * entire SYNC[2] pickup wait, 91.3% sits behind this ONE pass. 401 of
     * 3820 pickups land inside it and each costs a mean of 45.50 lines —
     * more than a whole vblank. Every other pass is noise: idle 0.34
     * lines, text 7.58, sprites 14.38. 87.8% of pickups find the slave
     * already idle.
     * The reason is structural, not size: cat1 is the one pass with NO
     * service point inside it, and it runs LAST, so it is still going
     * when the next window's pickup arrives. The wait is simply whatever
     * is left of it.
     * DO NOT FIX THIS BY STRIPING IT — that is 7f, which hit its target
     * and lost the play pass. Fix it by MOVING it: the previous band's
     * cat1+text now run FIRST, immediately after the ack, where a whole
     * window gap sits in front of them. The striped passes that follow
     * are interruptible, so a late pickup costs one strip instead of the
     * tail of an uninterruptible pass. Same total work, same order
     * within a band (cat1 still lands after that band's sprites, one
     * window later), and every band still completes before its ship:
     * R0 striped at k0 / cat1 at k1 / ships k2; R1 at k1 / k2 / k0;
     * R2 at k2 / k0 / k1. */
    if (!NOCAT1DEFER && cat1_valid) {
        compose_layer(cat1_lo, cat1_hi, 1, 0, 0, cat1_bank, cat1_par, 2);
        slave_service_stream();
#ifndef MD_BG_TEXT
        compose_text(cat1_t0, cat1_t1, cat1_par);
        slave_service_stream();
#endif
        cat1_valid = 0;
    }
#ifdef MD_BG
    /* PIVOT: the SLAVE'S rows too. Slice 1c ifdef'd the master's band
     * queue but left these two passes composing the software BG/FG0
     * over the cleared rows — with the BG colour groups no longer
     * allocated they painted BLACK, and since the master/slave row
     * split alternates bands, the screen showed full-width black bands
     * exactly where the slave had composed. Clear to 0 (MD-through)
     * like the master's phase 0. */
    for (int y = lo; y < hi; y += 12) {
        int ye = (y + 12 > hi) ? hi : y + 12;
        for (int r = y; r < ye; r++) {
            uint8_t *d = &sbuf[(8 + r) * SBUF_W];  /* sbuf row = screen
                                                    * row + 8, per
                                                    * blit_half */
            for (int x = 0; x < SBUF_W; x += 4)
                *(uint32_t *)(d + x) = 0;
        }
        slave_service_stream();
    }
#else
    for (int y = lo; y < hi; y += 12) {
        int ye = (y + 12 > hi) ? hi : y + 12;
        compose_layer(y, ye, 1, 1, 1, bank1, par, 0);
        slave_service_stream();
    }
#endif
#ifndef MD_BG_FG0
    for (int y = lo; y < hi; y += 12) {
        int ye = (y + 12 > hi) ? hi : y + 12;
        compose_layer(y, ye, 1, 0, 0, bank1, par, 1);
        slave_service_stream();
    }
#endif
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
    /* DO NOT STRIPE THIS PASS. It is the one pass here that runs
     * WHOLE-BAND while BG opaque / FG cat0 / sprites above are all
     * striped 12 rows, and that asymmetry is load-bearing — LOOP 7f
     * tried to make it uniform and it was REVERTED. Measured on ares:
     * striping DID do what it was aimed at (worst restore span 74 -> 56
     * lines, the bursty-strobe target) and lost on everything else —
     * V-gate rejects 0.7 -> 5.9%, restore-past-vblank 0.6 -> 3.4%,
     * window/ack 69 -> 96 lines, black frames 1.19 -> 3.56%, plus
     * on-screen SPRITE ARTIFACTS. Two independent costs: the extra
     * service points cost more compose time than the bounded pickup
     * latency saved, and cat1-over-sprites does not survive being cut
     * into strips. Bound the master's wait some other way. */
    /* HAND THIS BAND'S cat1+text TO THE NEXT GAP (see the head of this
     * function). Recorded, not run — the next command executes it first,
     * with a full window gap in front of it. */
    if (NOCAT1DEFER) {   /* A/B ARM: cat1+text inline at the TAIL, the
                          * pre-LOOP-10 order. Costs the strobe win back;
                          * exists only to test whether the deferral is
                          * what took playability. */
        compose_layer(lo, hi, 1, 0, 0, bank1, par, 2);
        slave_service_stream();
        compose_text((rg == 0) ? 0 : (rg == 1) ? 9 : 18,
                     (rg == 0) ? 4 : (rg == 1) ? 13 : 23, par);
        slave_service_stream();
    }
    cat1_lo   = (uint8_t)lo;
    cat1_hi   = (uint8_t)hi;
    cat1_bank = (uint8_t)bank1;
    cat1_par  = (uint8_t)par;
    cat1_t0   = (uint8_t)((rg == 0) ? 0 : (rg == 1) ? 9 : 18);
    cat1_t1   = (uint8_t)((rg == 0) ? 4 : (rg == 1) ? 13 : 23);
    cat1_valid = 1;
#ifdef PICKUP_SRC_PROBE
    slave_in_compose = 0;
#endif
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

/* Arm DMAC0 for the MD's DREQ push (Chaotix sequence: disable, read to
 * clear TE, program, enable). Called EVERY window: the DMA drains one
 * 770-word transfer then stops (TE), so the FIFO must have a fresh armed
 * drain before each vint's push or the 68K blocks on a full FIFO. */
/* LOOP 7g: the packet is SPLIT BY WINDOW PHASE and this must match the
 * md_main push exactly. `k` is the phase of the window being acked, and
 * the MD pushes immediately after that ack:
 *   k==0 -> SPRITE packet, 596 words (lands for w1, where the harvest is)
 *   else -> TEXT packet,   340 words
 * push_aborts read 0 for three ares passes while dreq_incomplete sat at
 * 14-21% of cycles: the 68K pushed every word and the DMA still failed to
 * drain, i.e. the transfer was simply too big. Mean payload 852 -> 425,
 * and most of that is free — the sprite list was pushed on all three
 * phases and consumed on exactly one. */
/* LOOP 8: BOTH packets are now 596 words, so the arm is unconditional.
 *   k==0  SPRITE: 82 prefix + 512 list + 2 pad
 *   else  TEXT:   82 prefix + 256 palette pair + 256 text + 2 pad
 * The MD pushes only 340 of the TEXT packet when no palette region is
 * dirty, and that needs no agreement here: a short push leaves TE clear
 * and TCR holding the remainder, which is exactly the partial-apply path
 * below (landed = armed - TCR is the truth either way). Arming the max
 * and letting `landed` speak is what makes the palette payload OPTIONAL
 * without a second length protocol. 596 is not a new size for the DMA —
 * it is what the sprite push has drained every cycle since 7g. */
#define DREQ_LEN(k)  (596u)

RAMCODE static void dreq_rearm(int k)
{
    SH2_DMA_CHCR0 = 0x44E0;
    (void)SH2_DMA_CHCR0;
    SH2_DMA_SAR0 = 0x20004012;          /* DREQ FIFO */
    SH2_DMA_DAR0 = 0x26039000;          /* SPR_LAND (uncached) */
    SH2_DMA_TCR0 = DREQ_LEN(k);
    SH2_DMA_DRCR0 = 0;
    SH2_DMA_DMAOR = 1;
    SH2_DMA_CHCR0 = 0x44E1;
}

#ifdef IDLE_TOKEN
/* Out of line ON PURPOSE. As an inline macro in m_main's poll branch this
 * cost 628 bytes of _m_main — GCC restructures the loop around the
 * tok_pub test — and that overran the 0x06019000 region guard. A call is
 * a few cycles against an MMIO write it usually skips. RAMCODE because
 * the poll branch runs it every spin. */
RAMCODE __attribute__((noinline)) static void tok_set(uint16_t v)
{
    static uint16_t tok_pub = 0;         /* transition-only: the doorbell
                                          * read is already ~166K MMIO/sec
                                          * and this must not add to it */
    if (tok_pub != v) {
        tok_pub = v;
        MARS_SYS_COMM4 = v;
    }
}
#endif

#ifdef SPAN_PROBE
/* v3: which poll-loop stage was in flight when a window pickup missed
 * the gate. Written only by the master. 0 = idle/poll — a late pickup
 * at stage 0 means the MD posted late, not us. */
static uint8_t m_stage;
#endif

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
    for (int i = 0; i < NSETS * NWAYS; i++) {
        cache_tag[i] = 0xFFFF;
#ifdef MD_BG
        md_tag[i] = 0xFFFFFFFFu;             /* fixed block: not .bss-zeroed */
        md_ref[i] = 0;
#endif
    }
#ifdef MD_BG
    for (int i = 0; i < NSETS * NWAYS / 32; i++)
        md_dirty[i] = 0;
    for (int i = 0; i < 28 * 40; i++)
        md_dbg_nt[i] = 0xDEAD;               /* debug mirror: never-written */
    for (int i = 0; i < MDP_LINES * 16; i++) {
        mdp_line_c[i] = 0xFFFF;
        mdp_pen_rc[i] = 0;
        mdp_pen_own[i * 2] = mdp_pen_own[i * 2 + 1] = 0;
    }
    for (int i = 0; i < 128; i++) {
        mdp_s_line[i] = 0;
        mdp_s_stmp[i] = 0;
        mdp_s_used[i] = 0;
        mdp_s_vol[i]  = 0;
    }
#endif
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
    SYNC[4] = SYNC[5] = 0;              /* (iter4) preempt-blit mailbox */

    volatile uint16_t *cram = &MARS_CRAM;
    for (int i = 0; i < 256; i++)
        cram[i] = 0;

    SH2_FRT_TCR = 1;
    for (int i = 0; i < 16; i++)
        DIAG[i] = 0;
    DIAG[19] = 0;                       /* CRAM writes performed (LOOP 6) */
    DIAG[20] = 0;                       /* apply_cram groups skipped */
    DIAG[21] = 0;                       /* preempt-blit pickup timeouts */
    DIAG[22] = 0;                       /* preempt-blit echo timeouts */
#ifdef WIN_SPLIT_PROBE
    DIAG[23] = DIAG[24] = DIAG[25] = 0; /* LOOP 9 blit / wait / rows split */
#endif
#ifdef FM_TEST
    FMT[0] = FMT[1] = FMT[2] = FMT[3] = 0;
    DRQR[0] = DRQR[1] = DRQR[2] = DRQR[3] = DRQR[4] = 0;
    DRQR[5] = DRQR[6] = 0;      /* drift split: small / catastrophic */
#endif
#ifdef WAIT_SPLIT_PROBE
    for (int i = 0; i < 9; i++)
        WSPL[i] = 0;
#endif
#ifdef PICKUP_SRC_PROBE
    for (int i = 0; i < 6; i++)
        PSRC[i] = 0;
#endif
#ifdef SPAN_PROBE
    for (int i = 34; i <= 60; i++)
        DIAG[i] = 0;                    /* LOOP 9 pickup-V + span histograms */
#endif
#ifdef ROWSTALE_PROBE
    DIAG[32] = DIAG[33] = 0;            /* LOOP 9 rows identical / checked */
    for (int i = 0; i < 112; i++)
        ROWHASH[i] = 0xFFFFFFFFu;       /* 0 is a plausible row hash */
#endif
    for (int i = 0; i < 192; i++)
        PAL_SETGEN[i] = 0;              /* before the slave is released */
    for (int i = 0; i < 32; i++)
        cram_key[i] = 0xFFFF;           /* .bss zeros would alias tile set 0 */

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
        uint8_t sub;      /* rows of the current strip already composed:
                           * a strip that yielded to the window RESUMES
                           * here instead of recomputing from its start */
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
    pg_pending = 0x1FFF;                 /* dirty tilemap pages awaiting
                                          * copy (write-observer ring:
                                          * bitmap rides the DREQ tail;
                                          * boot = all pages once) */
    uint16_t cycle_dirt = 0x1FFF;        /* PRESENTATION 2.0: pages the game
                                          * wrote into the CURRENT draw bank
                                          * since the last flip — exactly the
                                          * pages STALE in the other bank.
                                          * Restored there at the k2 flip
                                          * from TILEMAP_U truth. Boot =
                                          * all pages once, so the ex-decoy
                                          * bank gets a full staging copy at
                                          * the first flip. */
    pg_watch = 0x1FFF;                   /* pages whose last capture CHANGED
                                          * content = a writer stream may
                                          * still be in flight (the big
                                          * writers mark once at pointer
                                          * load, then store for many
                                          * vints). Watched pages are
                                          * recaptured every cycle and
                                          * restored at every flip until
                                          * two consecutive captures agree
                                          * — the split-stream closure; see
                                          * cap_page. Boot: watch all. */
    uint32_t win_no = 0;                 /* window counter (steal rate-limit) */
    uint16_t yield_spin = 0;             /* fruitless-yield guard, see below */
#ifdef MD_BG
    uint16_t md_scan = 0;                /* dirty-slot scan cursor */
    uint8_t  md_phase = 0;               /* 0 = tiles, 1-4 = name table */
    uint16_t md_pending = 0;             /* claimed slots not yet shipped */
    uint8_t  mdp_chk = 0;                /* palette drift check cursor */
    uint8_t  md_forced = 0;              /* consecutive demand-bias tile
                                          * batches; see the starvation
                                          * bound at the packet builder */
#endif
    for (int i = 0; i < 4; i++) {
        bq[i].on = 0;
        bq[i].sub = 0;
    }
    for (int i = 0; i < 256; i++) {      /* fixed blocks aren't .bss-zeroed */
        cram_mirror[i] = 0;
        shadow_lut[i] = (uint8_t)i;      /* identity until first rebuild */
    }
    BM->active = 0;
    DIAG[18] = BUILD_HASH32;             /* savestates self-identify */

#ifdef IDLE_TOKEN
    /* LOOP 11 — CHAOTIX'S IDLE TOKEN. Chaotix's SH-2 publishes "I am
     * parked in a loop touching only COMM0" by zeroing it, and its 68000
     * then takes FM UNILATERALLY — no request, no ack, no round trip. Our
     * MD instead raises FM, posts a command and SPINS for the ack, and the
     * preack probe showed ~79 of the ~210-line window/ack span is the
     * master not having STARTED yet. That is FM held for nothing.
     * So: publish readiness on COMM4 (SH-2 -> MD, otherwise unused) and let
     * the MD skip a window it would only have stalled in. Transition-only
     * writes — the doorbell read is already ~166K MMIO/sec and this must
     * not add to it. */
#define TOK(v)    tok_set((uint16_t)(v))
#define TOK_READY 0x0EAD
#else
#define TOK(v)    do { } while (0)
#endif
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
            /* IDLE TOKEN, published HERE and only here (LOOP 11a fix):
             * readiness is "nothing in flight at the poll", not "work
             * just finished". The first attempt published READY only at
             * the END of a strip or a build_maps chunk, so an EMPTY band
             * queue — which never enters those branches — left the token
             * BUSY forever after the first ack and the MD skipped 3 of
             * every 4 windows. Every path through this branch reaches
             * this line, including the do-nothing one; BUSY is then set
             * immediately before each heavy call below and never needs
             * clearing, because the next poll visit clears it. */
            TOK(TOK_READY);
#ifdef SPAN_PROBE
            m_stage = 0;                     /* nothing in flight at poll */
#endif
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
                    TOK(0);                  /* busy: long, uninterruptible */
#ifdef SPAN_PROBE
                    m_stage = 1;
#endif
                    if (build_maps_chunk(owed_par))
                        maps_owed = 0;
                }
                continue;
            }
            if (!bq[bq_h].on && shadow_dirty) {
                /* palette changed: refresh the shadow LUT, one chunk
                 * per idle visit (silhouette fallback until fresh) */
                uint16_t dt = (uint16_t)(frt() - t_vint);
                if (dt <= 4000) {
                    TOK(0);                  /* busy: a LUT chunk */
#ifdef SPAN_PROBE
                    m_stage = 2;
#endif
                    shadow_lut_chunk();
                }
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
                    TOK(0);                  /* busy: a maintenance chunk */
#ifdef SPAN_PROBE
                    m_stage = 3;
#endif
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
                TOK(0);                          /* busy: a compose strip */
#ifdef SPAN_PROBE
                m_stage = (b->phase == 2) ? 5 : (b->phase >= 3 ? 6 : 4);
#endif
                /* ---- YIELDABLE STRIP (LOOP 11, interrupt pickup) ----
                 * A 12-row strip runs 6-22 scanlines, and the master's
                 * accept bound is only 6 lines wide (v<0xDF||v>0xE4)
                 * with the MD posting inside 0xDF..0xE2. So a strip in
                 * flight overruns the window every time: MEASURED on
                 * ares, 12.1% of pickups land late and each one drops a
                 * blit phase — the stale band that reads as the green
                 * tear.
                 * The row phases are RANGE calls, so they can be cut
                 * into YIELD_ROWS chunks at the CALL SITE and the flag
                 * tested between them. The hot inner loops are not
                 * touched, and b->sub resumes the strip where it
                 * stopped rather than recomputing it.
                 * Phases 4/5/default are single-shot and stay atomic. */
#ifndef CMD_INT
                /* CMDINT IS RETIRED (see LOOP11.md): the shipping build
                 * keeps the original ATOMIC strip. The yieldable form
                 * below costs ~5% of compose on ares for a preemption
                 * that nothing sets, and it reads win_pend, which only
                 * the CMD ISR ever initialises. */
                switch (b->phase) {
                case 0:
#ifdef MD_BG
                    /* PIVOT SLICE 1a: leave the BG rows at 0 so the MD
                     * layer shows through, instead of composing them.
                     * SBUF ROW = SCREEN ROW + 8 (blit_half reads
                     * sbuf[(8+y)*W+8]); the first cut cleared unshifted
                     * rows and left an 8-row stale strip at the tail of
                     * every band — the grey seams at 64-72/136-144. */
                    for (int r = y; r < ye; r++) {
                        uint8_t *d = &sbuf[(8 + r) * SBUF_W];
                        for (int x = 0; x < SBUF_W; x++) d[x] = 0;
                    }
#else
                    compose_layer(y, ye, 0, 1, 1, b->bank, b->bpar, 0);
#endif
                    diag_add(10, tq);
                    break;
                case 1:
#ifdef MD_BG_FG0
                    /* PIVOT CONFIG: FG cat-0 moves to the MD too
                     * (section 4: "the two scroll layers, and only
                     * those"). Phase 0 already left these rows at 0, so
                     * skipping is the same as leaving them transparent. */
#else
                    compose_layer(y, ye, 0, 0, 0, b->bank, b->bpar, 1);
#endif
                    diag_add(11, tq);
                    break;
                case 2:
                    compose_sprites(y, ye, b->bpar);
                    diag_add(12, tq);
                    break;
                case 3:
                    compose_layer(y, ye, 0, 0, 0, b->bank, b->bpar, 2);
                    break;
                default:
                    break;
                }
#else
                if (b->phase <= 3) {
                /* PHASES 0-3 ONLY. The first cut ran this block for every
                 * phase, so `default:` swept up phases 4 (text), 5
                 * (cache_fill) and the build_maps terminator and gave
                 * each of them an EXTRA full 12-row cat-2 layer compose
                 * on top of their real work. On ares that collapsed
                 * throughput: blit skips 26.6% -> 94.8% of cycles and
                 * deferrals 458 -> 3523. MAME only showed ~2 points of
                 * parity for it, which was not loud enough to notice. */
                int yc = y + b->sub;
                int yielded = 0;
                while (yc < ye) {
                    int yn = yc + YIELD_ROWS;
                    if (yn > ye) yn = ye;
                    switch (b->phase) {
                    case 0:
                        compose_layer(yc, yn, 0, 1, 1, b->bank, b->bpar, 0);
                        break;
                    case 1:
                        compose_layer(yc, yn, 0, 0, 0, b->bank, b->bpar, 1);
                        break;
                    case 2:
                        compose_sprites(yc, yn, b->bpar);
                        break;
                    default:                      /* phase 3 only */
                        compose_layer(yc, yn, 0, 0, 0, b->bank, b->bpar, 2);
                        break;
                    }
                    yc = yn;
                    if (yc < ye && *win_pend) { yielded = 1; break; }
                }
                /* STARVATION GUARD, and it is the LOOP 11a lesson applied
                 * before shipping the bug this time: the ISR fires a few
                 * microseconds BEFORE the 68000 writes COMM0, so there is
                 * always a brief window where win_pend is set and no
                 * command is visible yet. That is fine and expected. What
                 * is NOT fine is a raise with no post ever following (a
                 * timed-out ack, a missed write): every strip would yield
                 * forever and the master would stop composing entirely.
                 * Bound it — after this many fruitless yields, clear the
                 * flag and get back to work. */
                if (yielded) {
                    DIAG[62]++;                    /* strips yielded */
                    if (++yield_spin > 64) {
                        yield_spin = 0; *win_pend = 0;
                        DIAG[63]++;                /* guard trips: should
                                                    * be ~0; nonzero means
                                                    * raises without posts */
                    }
                } else {
                    yield_spin = 0;
                }
                b->sub = (uint8_t)(yc - y);
                if (yielded)
                    continue;            /* resume this strip next visit */
                b->sub = 0;
                if (b->phase == 0)      diag_add(10, tq);
                else if (b->phase == 1) diag_add(11, tq);
                else if (b->phase == 2) diag_add(12, tq);
                }
#endif
                switch (b->phase) {
                case 0:
                case 1:
                case 2:
                case 3:
                    break;
                case 4:
#ifdef MD_BG_TEXT
                    break;    /* text to the MD as well -- 96 distinct
                               * patterns, tile-based, MD-friendly */
#endif
                    compose_text((b->rg == 0) ? 4 : (b->rg == 1) ? 13 : 23,
                                 (b->rg == 0) ? 9 : (b->rg == 1) ? 18 : 28,
                                 b->bpar);
                    break;
                case 5:
#ifdef BQ_CHUNK
                    /* LOOP 13 — BOUNDED QUANTA. SPAN_PROBE v3 (ares
                     * bs9): 98.9% of missed pickups sat behind this
                     * stage-6 pair. The dt<=8000 gate budgets ~4000
                     * ticks, which holds on MAME and not on ares (~3x).
                     * Same total budget, 64 codes per visit, stay in
                     * phase until spent — the strip phases run bounded
                     * quanta exactly like this and miss ZERO pickups. */
                    cache_fill(64);
                    diag_add(1, tq);
                    if (++b->cnt < ((miss_n[0] + miss_n[1] > 96) ? 6 : 2))
                        continue;            /* stay in phase 5 */
                    b->cnt = 0;
                    break;
#else
                    /* ADAPTIVE DRAIN: flat 128/window left seconds of
                     * white placeholder tiles on ares when cutscenes
                     * burst-load art (Zeus). Deep backlog gets a 3x
                     * budget — still inside the dt<=8000 heavy slot
                     * (build_maps at ~4ms fits the same gate). */
                    cache_fill((miss_n[0] + miss_n[1] > 96) ? 384 : 128);
                    diag_add(1, tq);
                    break;
#endif
                default:
#ifdef BQ_CHUNK
                    /* Terminator stays IN the band queue, chunked: one
                     * 8-row slice per poll visit under the dt<=8000
                     * gate, staying in this phase until the tail lands.
                     * Full visit rate, so the scan drains in ~14 visits
                     * (a fraction of one window gap) instead of the
                     * owed path's 1-2 chunks/cycle. First cut routed it
                     * through maps_owed and starved BOTH consumers:
                     * maps landed cycles late (black tile slabs) and
                     * the shadow LUT sat behind perpetually-owed maps
                     * in the maintenance slot (every actor a black
                     * silhouette, the corpus run of 2026-08-12).
                     * BM-state caveat: a dropped band setting maps_owed
                     * mid-drain resets the shared BM scan (par check);
                     * rare, self-heals next visit, accepted for now. */
                    if (b->rg == 2 && !build_maps_chunk(b->bpar ^ 1))
                        continue;            /* stay in terminator */
#else
                    if (b->rg == 2)
                        build_maps(b->bpar ^ 1, b->bank);
#endif
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
            *win_pend = 0;               /* window taken: strips run on */
            yield_spin = 0;
#ifdef CMD_INT
            /* MASK CMD ACROSS THE WINDOW. The interrupt exists to
             * preempt a compose strip; inside the window body it can
             * only land in the middle of the blit, which is the one
             * place with 8 lines of margin. CMDPROBE measured that cost
             * on its own: blit skips 26.6% -> 36.2% with no other
             * change. CMD is level 8, so masking at 8 blocks it while
             * leaving VRES (14) and V (12) alone; a CMD raised here
             * stays pending and fires the moment the mask drops. */
            __asm__ __volatile__("mov #-128,r0\n\tldc r0,sr"
                                 ::: "r0", "memory");
#endif
#ifdef CMD_PROBE
            /* PICKUP LATENCY = (poll noticed) - (interrupt arrived).
             * DIAG[59] max, DIAG[60] sum, DIAG[61] samples. ~46 FRT
             * ticks per scanline, so lines = ticks / 46. This is the
             * budget an interrupt-driven pickup could recover; if it is
             * small, the ISR rewrite is not worth doing. */
            {
                uint16_t isr_t = (uint16_t)DIAG[58];
                uint16_t d = (uint16_t)(tw - isr_t);
                /* BOUND IS ONE FRAME (262 lines = 12052 ticks), not
                 * 20000: a window the MD never posts (V-gate reject) or
                 * the master never picks up leaves the delta spanning
                 * multiple frames, and the 16-bit FRT wraps every ~1425
                 * lines. The first cut used 20000 and DIAG[59] came back
                 * as 279 lines -- longer than a frame, i.e. nonsense. */
                if (d < 12052) {
                    if (d > DIAG[59]) DIAG[59] = d;
                    DIAG[60] += d;
                    DIAG[61]++;
                    /* the mean hides the shape: what matters is the
                     * FRACTION of pickups late enough to miss the
                     * master's V<=0xE4 accept bound. [62] > 3 lines,
                     * [63] > 10 lines (a compose strip in flight). */
                    if (d > 138) DIAG[62]++;
                    if (d > 460) DIAG[63]++;
                }
            }
#endif
            t_vint = tw;
            shadow_stole = 0;
            win_no++;
#ifdef FM_TEST
            /* Read back HERE — at pickup, FM=1, and crucially BEFORE the
             * flip. Outside the window the CPU-side bank is Y; the flip
             * makes it X and the restore puts it back. Reading after the
             * flip would read the OTHER bank and report a false negative
             * for reasons that have nothing to do with FM.
             * The post-ack write used the pre-increment win_no. */
            {
                volatile uint32_t *sa = (volatile uint32_t *)0x24011C00;
                uint32_t want = 0xA5A50000u | (uint16_t)(win_no - 1);
                for (int i = 0; i < 8; i++) {
                    FMT[1]++;
                    if (sa[i] == want + i) FMT[0]++;
                }
                /* POSITIVE CONTROL. Same scratch, same instruction shapes,
                 * but written and read entirely inside the window. If this
                 * does not read 8/8 the harness is broken and a zero above
                 * means nothing. */
                volatile uint32_t *sb = (volatile uint32_t *)0x24011D00;
                uint32_t pat = 0x5A5A0000u | (uint16_t)win_no;
                for (int i = 0; i < 8; i++)
                    sb[i] = pat + i;
                for (int i = 0; i < 8; i++)
                    if (sb[i] == pat + i) FMT[2]++;
            }
#endif
            /* LOOP 7 NEGATIVE: anchoring t_vint to the MD's V heartbeat
             * (back-dating tw by the lines elapsed since 0xDF, ~46 FRT
             * ticks/line) instead of the pickup instant changed NOTHING —
             * skips 153 vs 155, and blit_preempt got worse (0.99 ->
             * 1.14ms). The late pickups are therefore NOT a phase latch in
             * the quiet zone; do not re-run this. */

            /* (iter4) the previous window's concurrent compose is NO
             * LONGER drained here: that slave_wait stalled the 68K for a
             * whole compose (the retry-loop saturation, LOOP.md iter4).
             * The wait moves POST-ACK (off the 68K's critical path), and
             * the per-window blit reaches the slave via the SYNC[4]
             * preempt mailbox — pickup latency <=1 compose strip. */
            diag_add(4, tp);

            /* vblank-critical part. If the third-wait ate the vblank,
             * skip the blit (slice ships next cycle) rather than flip
             * mid-frame — a stale band beats a black frame. The clock
             * is the MD's live V-counter heartbeat (COMM12, tag 0xD0xx,
             * written every ack-spin iteration): ares' FBCTL VBLK bit
             * proved untrustworthy (the field bursts of one black frame
             * per cycle — flips passing a stale/false vblank check). */
            int skip;
#ifdef SPAN_PROBE
            /* LOOP 9 — WHERE PICKUP LANDS, and how the blit span is
             * DISTRIBUTED. The existing span counter starts at t_vint
             * (window pickup), so a LATE PICKUP is invisible to it and
             * yet eats the same vblank: the gate accepts anywhere in
             * V=DF..E4, a 6-line spread, and under load the master is
             * mid-compose-strip when the window arrives. If pickup
             * drifts late as scenes get busier, that is "starts fast,
             * then devolves" and no counter we had could see it.
             * [34] V<DF  [35..40] V=DF..E4  [41] V>E4 / no heartbeat.
             * v2 (LOOP 13): the [50]/[51] late-restore split and the
             * [42..60] span/per-window sections are RETIRED — the span
             * question answered (0 of 2655 past vblank) and every slot
             * past 49 collided with pivot-era counters (evictions at
             * [50]/[51], sprite/page/ISR work at [52..60]), which is
             * what made bs9's readout self-contradictory. */
#endif
            {
                uint16_t md_v = MARS_SYS_COMM12;
                if ((md_v & 0xFF00) == 0xD000) {
                    unsigned v = md_v & 0xFF;
                    skip = (v < 0xDF || v > 0xE4);
#ifdef SPAN_PROBE
                    /* v3: the two bs9 runs split the miss mode — run 1
                     * was v<DF wrapped-late (25+ lines), run 2 was
                     * V=E5..FF just-late (5-37 lines, MD heartbeat is
                     * always live so "none" is impossible mid-game).
                     * Both are the master arriving late; [42..49] now
                     * bin every MISSED pickup by what the master was
                     * doing (m_stage): 0 idle/poll (= the MD posted
                     * late, not us), 1 owed build_maps chunk, 2 shadow
                     * LUT chunk, 3 maintenance steal, 4 tile strip
                     * (ph 0/1), 5 sprite strip (ph 2), 6 single-shot
                     * (ph >= 3), 7 post-ack window tail. */
                    if (v < 0xDF)      DIAG[34]++;
                    else if (v > 0xE4) DIAG[41]++;
                    else               DIAG[35 + (v - 0xDF)]++;
#endif
                } else {
                    skip = !(MARS_VDP_FBCTL & 0x8000);
#ifdef SPAN_PROBE
                    DIAG[41]++;                  /* no heartbeat */
#endif
                }
                /* (LOOP 7a's DIAG[23..25] skip-cause split is RETIRED: it
                 * answered its question — every skip is on the LATE side,
                 * never wrapped, never a missing heartbeat — and .ramtext
                 * is the scarce resource now.) */
                if (skip && k == 2)
                    DIAG[7]++;               /* missed flips = dropped frames
                                              * (blits no longer skip at all:
                                              * they write the hidden bank) */
#ifdef SPAN_PROBE
                if (skip)
                    DIAG[42 + m_stage]++;    /* v3: miss by master stage */
#endif
            }
            /* PRESENTATION 2.0 — LIVE DIRTY WORD. The MD publishes the
             * game's tile dirty bitmap (0xFFB9FE) on COMM10 at every vint
             * entry, before the post (COMM10 is free post-boot: the pad
             * publish it was named for was never built, and only the boot
             * handshake touches COMM8). The DREQ word-80 copy of the same
             * bitmap arrives one window LATE (pushed post-ack, applied
             * next window) — under double-buffering that skew would make
             * the k2 pre-flip copy_pages read gap-written pages from the
             * WRONG bank and corrupt TILEMAP_U truth. The live word is
             * exact: the game is stalled from vint entry to ack, so bits
             * read here are complete through this window. Word 80 stays
             * applied too (redundant OR; at worst an identity copy). */
            {
                uint16_t ld = MARS_SYS_COMM10 & 0x1FFF;
                pg_pending |= ld;
                cycle_dirt |= ld;
                MK_ACC |= ld;
            }
            /* PRESENTATION 2.0 (LOOP 13): true double-buffer. Blits write
             * the hidden DRAW bank at ALL three windows — no per-band
             * flip/restore pair, no vblank bound on any blit (the 84-line
             * ares no-fit is moot), and the blank-decoy trick plus the
             * whole restore-past-vblank black-frame class are extinct.
             * ONE FS flip per cycle, at k2, inside the V∈[DF,E4] gate
             * that already protected the pairs (LOOP 7c: ares defers an
             * out-of-vblank FBCTL write a whole frame). The gate now
             * gates ONLY the flip: a missed gate drops one frame — the
             * display keeps the last WHOLE frame — never a band, never a
             * black frame. */
            uint16_t scmd = (uint16_t)(0x3000 | (k << 4) | (par << 8)
                                       | bank1);
            uint32_t guard;
            SYNC[2] = 0;
            SYNC[3] = 0;
            SYNC[5] = 0;                      /* (iter4) preempt-blit echo */
            tp = frt();
            if (k == 2 && !skip) {
                /* THE flip — before this window's blit: R0 of the NEXT
                 * frame must land in the NEW draw bank (frame N completed
                 * at k1). Order is load-bearing:
                 *  1. truth: drain ALL pending dirty pages while the FB
                 *     window still maps the bank the game wrote;
                 *  2. flip (bounded latch spin);
                 *  3. restore every page dirtied since the last flip into
                 *     the new draw bank from TILEMAP_U, BEFORE the ack —
                 *     the game writes staging only post-ack, so a restore
                 *     clobbering a fresh game write is impossible by
                 *     construction, and its read-backs (collision tst.w,
                 *     the page-1 scratch pair) never see a stale bank. */
                cycle_dirt |= pg_pending;
                pg_pending |= pg_watch;      /* unstable pages: recapture the
                                              * latest stream state pre-flip */
                cap_drain(13);               /* ALL of it — correctness */
                uint16_t fs_o = MARS_VDP_FBCTL & MARS_VDP_FS;
                MARS_VDP_FBCTL = fs_o ^ 1;
                /* In-gate = in-vblank: latches in 1 tick on MAME and
                 * immediately on ares (deferral is an OUT-of-vblank
                 * property, LOOP 7j). Bounded anyway: a failed latch
                 * aborts this cycle's flip — no restore, cycle_dirt keeps
                 * accumulating, publish lands in the still-mapped bank —
                 * instead of publishing into an unknown one. DIAG[31]
                 * counts; nonzero means the latch model is wrong. */
                {
                    uint16_t w0 = frt();
                    while ((MARS_VDP_FBCTL & MARS_VDP_FS) != (fs_o ^ 1)
                           && (uint16_t)(frt() - w0) < 200) ;
                }
                if ((MARS_VDP_FBCTL & MARS_VDP_FS) != (fs_o ^ 1)) {
                    DIAG[31]++;              /* flip failed to latch */
                } else {
                    /* watched pages restore too: their stream may still
                     * be mid-flight, and the truth was recaptured THIS
                     * window (pg_pending |= pg_watch above), so the new
                     * bank gets the exact base the game's next stores
                     * expect — the stream continues seamlessly across
                     * the bank swap. */
                    restore_pages((uint16_t)(cycle_dirt | pg_watch));
                    cycle_dirt = 0;
                }
            }
            diag_add(6, tp);                 /* slot 6: flip+truth+restore */
            tp = frt();
            {
                /* (iter4) PREEMPT MAILBOX: hand the slave its blit half
                 * WITHOUT first draining its concurrent compose. The slave
                 * services SYNC[4] between compose strips, so pickup
                 * latency is <=1 strip instead of a whole compose. The
                 * blitted rows and the region the slave is still composing
                 * are DISJOINT by pipeline construction: Wk blits its
                 * blit-set while the outstanding compose (launched at the
                 * previous window) covers a different region band. */
                SYNC[4] = scmd;
                cache_purge();               /* slice rows may hold the OTHER
                                              * CPU's composes from last cycle */
#ifdef WAIT_SPLIT_PROBE
                uint16_t tws = frt();
#endif
                /* LOOP 9 WIN_SPLIT_PROBE: slot 5 (`blit_preempt`) has always
                 * bundled the master's blit_half WITH the post-blit waits
                 * (SYNC[2] pickup, the FBCTL restore + its latch spin, the
                 * SYNC[5] echo). Mission 1 needs the blit ALONE — a DMAC
                 * channel-1 rewrite can only move that term, and if the
                 * waits dominate, the DMA cannot win no matter how fast it
                 * is. [23] blit ticks, [24] post-blit wait ticks, [25] rows
                 * blitted (per-row cost = [23]/[25]). */
#ifdef WIN_SPLIT_PROBE
                uint16_t tb = frt();
#endif
                /* LOOP 7d thirds — master takes the upper half of each
                 * band, the slave the lower (see slave_window_k).
                 * DO NOT EVEN THESE THIRDS — LOOP.md negative 23. */
                if (k == 2)
                    blit_half(36, 72);
                else if (k == 0)
                    blit_half(108, 144);
                else
                    blit_half(184, 224);
#ifdef WIN_SPLIT_PROBE
                diag_add(23, tb);
                DIAG[25] += (k == 1) ? 40 : 36;
                tb = frt();
#endif
                /* LOOP 6d: BOUNDED. These two were the only unguarded
                 * spins in this function — every neighbouring wait
                 * carries guard=2000000. If the slave ever fails to
                 * answer the preempt mailbox the master used to spin
                 * here FOREVER with FM=1, which hangs the 68K too: a
                 * dead machine instead of a dropped frame. Time out into
                 * the already-supported "slave half missing this frame"
                 * state and count it; DIAG[21]/[22] are zero on a healthy
                 * run, so a nonzero value localises the hang exactly. */
#ifdef WAIT_SPLIT_PROBE
                WSPL[k] += (uint32_t)(uint16_t)(frt() - tws);   /* blit only */
                tws = frt();
#endif
                guard = 2000000;
                while (SYNC[2] < 1 && --guard) ;   /* slave picked up */
                if (!guard) DIAG[21]++;
#ifdef WAIT_SPLIT_PROBE
                WSPL[3 + k] += (uint32_t)(uint16_t)(frt() - tws);  /* the wait */
                WSPL[6 + k]++;
#endif
                /* (LOOP 7c/7i/7j's restore-past-vblank + latch-wait
                 * machinery lived here — DIAG[26]/[27] measurement, the
                 * FBCTL restore, the bounded latch spin [29]/[30]. ALL
                 * RETIRED by Presentation 2.0: there is no restore edge
                 * any more, so the class it measured is extinct by
                 * construction. [26]/[27]/[29]/[30] now read 0 forever;
                 * [28] still counts blit windows, [31] counts k2 flip
                 * latch failures (see the flip block above). */
                DIAG[28]++;
                guard = 2000000;
                while (SYNC[5] != scmd && --guard) ;  /* slave path done */
                if (!guard) DIAG[22]++;
                SYNC[4] = 0;
#ifdef WIN_SPLIT_PROBE
                diag_add(24, tb);
#endif
            }
            diag_add(5, tp);

            /* PRE-ACK in-window work (game paused, FM=1). W1 only: the
             * scroll/page latch, DREQ sprite-list harvest + DMA re-arm,
             * dirty-page copy, and CRAM. These MUST run pre-ack:
             *  - copy_pages reads the game's FB staging (0x24012000) —
             *    only legal while the SH-2 owns the FB (FM=1);
             *  - the DMA re-arm MUST precede the game's own DREQ push
             *    (md_main), or the 68K blocks on a full FIFO write with
             *    no armed drain — a hard mutual deadlock (measured: 68K
             *    stuck at the fifo store, ent frozen). The iter4 win is
             *    part (b) (the preempt-blit mailbox removed the full-
             *    compose pre-blit wait) plus moving the compose launch/
             *    drain/band enqueue POST-ACK; copy_pages stays the pre-ack
             *    floor until a full write-log ring retires its FB read. */
            /* Apply the DREQ TEXT/REG tail EVERY window (before re-arm
             * clears TE / overwrites the buffer), not just at k1 — a
             * k1-only apply refreshed text at 1/3 the push rate and the
             * scoreboard regressed. LOOP 7 moved this AHEAD of
             * latch_layer_regs(): the layer regs and rowscroll now arrive
             * in this packet, and latching before applying would hand the
             * compose the PREVIOUS window's scroll.
             *   0..19    layer regs 0x740-0x753
             *   20..79   rowscroll  0x7C0-0x7FB
             *   80..591  sprite list  (harvested at k1, below)
             *   592 bitmap, 593 text base, 594..849 = 256 text words
             *
             * PARTIAL-APPLY (LOOP 7b). This used to be all-or-nothing on
             * TE, and that made the DREQ packet a SINGLE POINT OF FAILURE
             * the moment COMM stopped carrying text and regs as well: a
             * truncated transfer meant NO scroll, NO pages, NO rowscroll,
             * NO text for that cycle. On ares that is not a rare event —
             * dreq_incomplete ran 101 of 214 cycles (47%, against the
             * baseline's 59 of 650 = 9%), which is "tilemap artifacts
             * everywhere". MAME never showed it: dreq_inc reads 1 there.
             * So: ask TCR0 how many words actually landed and apply every
             * block that arrived WHOLE. The packet is ordered smallest-
             * and-most-critical-first precisely so a short transfer still
             * delivers the 80 words the compose cannot fake.
             *
             * LONGWORD copies. Unlike the 68000 (16-bit bus — LOOP 6
             * negative 5: 32-bit compares bought nothing there), the SH-2
             * moves 32 bits per SDRAM transaction. ALIGNMENT (checked):
             * SPR_LAND+0 = byte 0, +20 = 40, +594 = 1188; TEXT_U+tb with
             * tb a multiple of 256 = byte 512k; TEXT_U+0x740 = 0xE80,
             * +0x7C0 = 0xF80. Every one is 4-byte aligned. */
            /* LOOP 7g: what LANDED is the packet pushed after the
             * PREVIOUS window, so it carries that window's layout, not
             * this one's. Pushes follow accepted windows 1:1 (md_main sets
             * window_ok only after the ack), so remembering the last phase
             * is exact — no tag word needed, and a tag would have to
             * survive truncation to be worth anything anyway.
             * No state needed to know it: a gate-rejected vint leaves
             * md_main's wskip UNADVANCED and retries the same phase, so
             * every window the master actually processes has k = prev+1
             * mod 3. (A static here also pushed .bss over the 0x19000
             * region guard.) Before the first push TE is clear and TCR
             * still holds the armed length, so landed computes to 0 and
             * nothing is decoded — the boot case needs no special-casing. */
            int prev_k = k ? k - 1 : 2;   /* no %: SH-2 has no divide */
            unsigned plen = DREQ_LEN(prev_k);
            unsigned landed = (SH2_DMA_CHCR0 & 2)
                            ? plen : (plen - (SH2_DMA_TCR0 & 0xFFFFFFu));
            if (landed > plen) landed = 0;   /* never trust a wild TCR0 */
            int got_spr = (prev_k == 0);
            if (landed >= 80) {
                {
                    volatile uint32_t *d = (volatile uint32_t *)(TEXT_U + 0x740);
                    volatile uint32_t *s = (volatile uint32_t *)(SPR_LAND + 0);
                    for (int i = 0; i < 10; i++)
                        d[i] = s[i];
                }
                {
                    volatile uint32_t *d = (volatile uint32_t *)(TEXT_U + 0x7C0);
                    volatile uint32_t *s = (volatile uint32_t *)(SPR_LAND + 20);
                    for (int i = 0; i < 30; i++)
                        d[i] = s[i];
                }
                /* (No cache purge here. LOOP 7a negative 8 proved these
                 * lines are ALREADY cold when latch_layer_regs reads them —
                 * the post-ack full cache_purge() runs every window — and
                 * the targeted purge was bit-identical either way. It was
                 * dead code kept out of caution; .ramtext is scarcer.) */
            }
            /* Shared prefix: bitmap at 80, text base at 81, in BOTH
             * layouts. The bitmap is applied every window either way — a
             * dropped one loses tile writes permanently. */
            if (landed >= 81) {
                pg_pending |= SPR_LAND[80];
                cycle_dirt |= SPR_LAND[80];  /* belt for the COMM10 live
                                              * word (provably a superset,
                                              * but a missed stale page
                                              * breaks game logic — cheap
                                              * insurance) */
            }
            if (!got_spr && landed >= 338) {
                /* LOOP 8: word 81 is text base (bits 0-10, always a
                 * multiple of 256) plus an optional palette tag —
                 * bit 15 = present, bits 13-11 = which aligned PAIR of
                 * 128-word regions. When set, the pair occupies words
                 * 82..337 and the full 256-word text chunk follows at 338
                 * (packet 596); when clear the text chunk sits at 82
                 * (packet 340). The tag rides in the PREFIX because a tag
                 * after the payload is worthless under truncation — a
                 * short transfer would drop it and the palette words
                 * would land in text RAM.
                 * PAIRS, not single regions: one region per push is
                 * ~0.67/vint and the colour-cycling sets in regions 0-1
                 * are rewritten EVERY vint, so they never converged
                 * (pal_probe: 66%/82% of samples out of sync, every other
                 * region 0%; on screen, a white ALTERED BEAST logo). */
                unsigned w81 = SPR_LAND[81];
                unsigned tb = w81 & 0x7FF, src = 82, need = 338;
                /* LAYOUT COMES FROM THE TAG, COMPLETENESS FROM `landed`.
                 * The palette region is laid out FIRST, so a truncated
                 * push that dropped the text still has palette words at
                 * 82 — reading the layout off `landed` instead would copy
                 * them into text RAM. */
                if (w81 & 0x8000) {
                    unsigned r = ((w81 >> 11) & 7) << 1;   /* pair -> region */
                    volatile uint32_t *d =
                        (volatile uint32_t *)(PAL_SH + (r << 7));
                    volatile uint32_t *s =
                        (volatile uint32_t *)(SPR_LAND + 82);
                    /* A region is 16 tile/text sets (8 words each) below
                     * word 1024, or 8 sprite sets (16 words each) above
                     * it; a pair is two regions, so 32 or 16 sets. */
                    unsigned s0, ns, wps;
                    if (r < 8) { s0 = r << 4;               ns = 32; wps = 4; }
                    else       { s0 = 128 + ((r - 8) << 3); ns = 16; wps = 8; }
                    /* LOOP 10 — COPY AND COMPARE PER SET. LOOP 8 bumped
                     * every generation the pair covers whenever it shipped,
                     * where the path before it bumped the 1-2 sets that
                     * actually changed. Each spurious bump is a memo miss
                     * and a full convert-and-store pass over a set whose
                     * words are identical. The read costs 128 words; the
                     * pass it skips costs 8-16 s16_to_mars conversions.
                     * Bumping STRICTLY AFTER a set's stores still matters:
                     * bumping first lets a paint read the OLD words and
                     * then record the NEW generation, latching that group
                     * stale (LOOP 6 measured it as demo2 20.9 -> 23.4).
                     * The generations are MASTER-written — the slave's COMM
                     * palette path that used to own them is gone — so there
                     * is no cross-CPU RMW race left to argue. */
                    for (unsigned t = 0; t < ns; t++) {
                        uint32_t ch = 0;
                        for (unsigned i = 0; i < wps; i++) {
                            uint32_t v = s[i];
                            ch |= v ^ d[i];
                            d[i] = v;
                        }
                        s += wps;
                        d += wps;
                        if (!ch)
                            continue;
                        PAL_SETGEN[s0 + t]++;
                        /* SPRITE SETS ONLY, and not at k1.
                         * k1 flips par and runs the whole apply_cram a few
                         * lines below, so painting here would use the OLD
                         * parity's mapping to no purpose. k0 and k2 have no
                         * such pass — they are the windows the hue came from.
                         * TILE/TEXT (r < 8) IS DELIBERATELY EXCLUDED. A
                         * cycle is three vints and the blitted image stays
                         * on screen across all of them, so a CRAM write at
                         * k0/k2 recolours pixels that were composed against
                         * the PREVIOUS generation. Sprites re-land every
                         * cycle and tolerate it; the colour-cycling tile
                         * sets in regions 0-1 do not — measured on the
                         * PRESSURE title static, which is exactly the
                         * cycling ALTERED BEAST logo: 2.44% -> 9.72% with
                         * tile/text painted here, 2.44% without, sprites
                         * painted either way. Fidelity says leave them to
                         * k1's single consistent update point. */
                        if (k != 1 && r >= 8)
                            cram_paint_spr(par, s0 + t - 128);
                    }
                    src = 338;
                    need = 594;
                }
                if (landed >= need && tb + 256 <= 2048 && !(tb & 1)) {
                    volatile uint32_t *d = (volatile uint32_t *)(TEXT_U + tb);
                    volatile uint32_t *s = (volatile uint32_t *)(SPR_LAND + src);
                    for (int i = 0; i < 128; i += 4) {
                        d[i + 0] = s[i + 0];
                        d[i + 1] = s[i + 1];
                        d[i + 2] = s[i + 2];
                        d[i + 3] = s[i + 3];
                    }
                }
            }
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
                if (got_spr && landed >= 594) {   /* whole list arrived */
                    for (int i = 0; i < 512; i += 8) {
                        SPR_SNAP[i + 0] = SPR_LAND[82 + i + 0];
                        SPR_SNAP[i + 1] = SPR_LAND[82 + i + 1];
                        SPR_SNAP[i + 2] = SPR_LAND[82 + i + 2];
                        SPR_SNAP[i + 3] = SPR_LAND[82 + i + 3];
                        SPR_SNAP[i + 4] = SPR_LAND[82 + i + 4];
                        SPR_SNAP[i + 5] = SPR_LAND[82 + i + 5];
                        SPR_SNAP[i + 6] = SPR_LAND[82 + i + 6];
                        SPR_SNAP[i + 7] = SPR_LAND[82 + i + 7];
                    }
                }
                if (landed < plen) {
                    DIAG[17]++;              /* incomplete DREQ frames */
                    /* residue split — see DRQR above. plen - landed,
                     * NOT a TCR0 re-read: the DMA may drain words
                     * between the two reads and skew the split. */
                    unsigned res = plen - landed;
                    if (res == 256)          DRQR[0]++;
                    else if (res >= 1 && res <= 8) DRQR[1]++;
                    else                     DRQR[2]++;
                    DRQR[3] = res;
                    if (res > DRQR[4]) DRQR[4] = res;
                }
                /* (re-arm moved to dreq_rearm(), called EVERY window —
                 * see below: the DMA drains one transfer then stops, so a
                 * k1-only re-arm left k0/k2 pushes to fill the FIFO and
                 * block the 68K mid-group-write. Debugger-confirmed:
                 * TE=1, FIFO full, 68K stalled in the push.) */
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
                 * transitions). Reads FB staging: FM-required, pre-ack. */
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
                    /* (The unsettled-bank defer is gone with the flip
                     * pairs: under Presentation 2.0 the FB window maps
                     * one bank for the whole cycle — the k2 flip happens
                     * strictly before any k2 copy, and k1 has no flip at
                     * all. This budgeted drain is now just the early
                     * spread of transition bursts; the k2 pre-flip drain
                     * is the correctness point.) */
                    int budget = (pg_pending >= 0x0FFF) ? 7 : 3;
#ifdef TILE_RATE
                    /* LOOP 11 step 1 — HOW OFTEN DOES THE TILEMAP CHANGE?
                     * The pivot rests on it being rare: if the game's tile
                     * RAM barely moves it can be STREAMED, like the sprite
                     * list and the palette already are; the 68K then stops
                     * needing the framebuffer, FM can be held, and the blit
                     * disappears. MAME attract measured 4.2% of cycles
                     * dirty, 0.22 pages copied per cycle — this exists to
                     * get the same number out of ares GAMEPLAY, which is
                     * where the framerate complaint lives. NEVER SHIP. */
                    if (pg_pending) DIAG[55]++;      /* cycles with any dirt */
                    DIAG[56] += (uint32_t)__builtin_popcount(pg_pending);
#endif
                    cap_drain(budget);
                }
                diag_add(0, tp);
                par ^= 1;                    /* now composing the next frame */
                tp = frt();
                apply_cram(par);
                diag_add(2, tp);
            } else if (k == 2) {
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
                /* debug-bar hijack of entry 255: publish it to the mirror
                 * (LOOP 6) — 255 lies inside pair 15's range, so a stale
                 * mirror would make the gated cram_set skip repainting it. */
                /* Entry 255 lies inside sprite pair 15's range, so this
                 * hijack can clobber a real color. Leave the MIRROR holding
                 * the true value and just flag it: the next apply_cram
                 * restores entry 255 with a single write, instead of the
                 * skip gate having to run a full 2112-entry pass (or worse,
                 * leaving 255 debug-white). */
                ((volatile uint16_t *)&MARS_CRAM)[255] = 0x7FFF;
                cram_hijacked = 1;
                for (int j = 0; j < 3; j++) {
                    int len = lens[j];
                    if (len > 300) len = 300;
                    uint8_t *b = sbuf + (8 + 2 + 2 * j) * SBUF_W + 8;
                    for (int i = 0; i < len; i++)
                        b[i] = 0xFF;
                }
            } else {
                /* k == 0 — EARLY CAPTURE, same budget as k1: spread a
                 * transition burst's truth work across the cycle so the
                 * k2 pre-flip drain (and the MD-cell re-stamp bang that
                 * follows a big truth delta) shrinks. Idempotent —
                 * capture reads the draw bank, which is stable all
                 * cycle. */
                cap_drain((pg_pending >= 0x0FFF) ? 7 : 3);
            }

            /* ---- EARLY ACK (iter4): all FM-required work (blit, DREQ
             * harvest+re-arm, copy_pages, CRAM) is done; release the game
             * NOW — BEFORE the compose launch/drain/band enqueue, which are
             * SDRAM-only and run concurrent with the game below. Combined
             * with part (b) (the preempt-blit mailbox retired the full-
             * compose pre-blit wait), the pre-ack stall drops to blit +
             * copy_pages + apply_cram + the <=1-strip preempt wait; the
             * compose-drain slave_wait leaves the 68K's critical path. */
            /* Re-arm the DREQ DMA EVERY vint (pre-ack) so the FIFO always
             * has a draining transfer when the MD pushes after the ack —
             * the fix for the drains-once-then-blocks hang. */
            dreq_rearm(k);
            if (k == 1)
                DIAG[9]++;
#ifdef MD_BG
            /* PUBLISH the packet PREPARED IN THE GAP (see the post-ack
             * block at the bottom of the window): the in-window cost is
             * one 1.5KB SDRAM->FB copy (~0.2 lines) instead of the full
             * walk/allocator/shipper. That work inside FM=1 was the
             * 209-line window/ack spans on ares -- the 68K lost ~40% of
             * its frame vs the shipping build and the game played SLOW.
             * Magic long is copied LAST so a partial copy never
             * presents as a valid packet. */
            {
                volatile uint32_t *d = (volatile uint32_t *)0x24011A00;
                const uint32_t *ssrc = (const uint32_t *)md_pkt;
                for (int i2 = 1; i2 < 368; i2++)
                    d[i2] = ssrc[i2];
                d[0] = ssrc[0];
            }
#endif
            MARS_SYS_COMM0 = 0;              /* ack: MD drops FM, game runs */
#ifdef CMD_INT
            __asm__ __volatile__("mov #32,r0\n\tldc r0,sr"
                                 ::: "r0", "memory");  /* back to level 2 */
#endif
            TOK(0);                          /* busy: post-ack compose follows */
#ifdef SPAN_PROBE
            m_stage = 7;                     /* v3: post-ack window tail */
#endif

            /* ---- POST-ACK, game running (FM=0, RV=0): SDRAM-only ---- */
#ifdef FM_TEST
            /* LOOP 9 — IS "THE SH-2 MAY ONLY WRITE THE FB WITH FM=1"
             * ACTUALLY TRUE? It rules out the shadow bank AND composing
             * straight into the FB (which would delete the blit outright
             * — the only 2x-class lever left), and it has never been
             * tested. The MD really does clear FM after every ack
             * (md_main.c:177), so the premise is real; the question is
             * only what a write does while it is clear.
             * SCRATCH: 0x24011A00..0x24012000 is 1536 bytes of genuine
             * dead space — past the image (ends 0x11A00) and below the
             * game's tile staging (starts 0x12000). Nothing reads it, so
             * a failed test cannot corrupt the picture.
             * The pattern carries the cycle counter, so a match proves
             * THIS cycle's write landed rather than a stale one. */
            {
                volatile uint32_t *sa = (volatile uint32_t *)0x24011C00;
                uint32_t pat = 0xA5A50000u | (uint16_t)win_no;
                for (int i = 0; i < 8; i++)
                    sa[i] = pat + i;
                /* Does a READ work while FM=0? Read back scratch B, which
                 * was written AND verified inside this same window. */
                volatile uint32_t *sb = (volatile uint32_t *)0x24011D00;
                uint32_t want = 0x5A5A0000u | (uint16_t)win_no;
                for (int i = 0; i < 8; i++)
                    if (sb[i] == want + i) FMT[3]++;
            }
#endif
            /* Drain the PREVIOUS window's concurrent compose before
             * relaunching (SYNC[0] carries one command at a time). This
             * WAS pre-ack (the retry-loop saturation); now it is off the
             * 68K's critical path — the game is already running. */
            tp = frt();
            if (tile_cmd) {
                slave_wait(tile_cmd);
                tile_cmd = 0;
            }
            diag_add(3, tp);

            /* launch band R(k)'s FULL concurrent compose (tiles then
             * sprites, slave rows), then do our own rows. */
            tile_cmd = (uint16_t)(CMD_TILE | (k << 4) | (par << 8) | bank1);
            slave_cmd(tile_cmd);
            diag_add(8, tw);

            {
                /* COMPOSE_LEAD2: see slave_concurrent_k. Both sides
                 * must use the same mapping or the CPUs compose
                 * different bands. */
                int rg = k;                  /* W0->R0, W1->R1, W2->R2 */
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
                    nb->sub = 0;             /* MUST reset: a band that
                                              * yielded mid-strip and was
                                              * then dropped leaves a
                                              * non-zero cursor in the
                                              * slot, and the next band to
                                              * land there would start its
                                              * phase-0 strip part-way in
                                              * and never compose those
                                              * rows. bq[] is a stack
                                              * local, so at boot it is
                                              * garbage too. */
                    bq_t = (bq_t + 1) & 3;
                }
            }

#ifdef MD_BG
            /* GAP-PREP (was PIVOT SLICE 1b, in-window): build the NEXT
             * window's packet into SDRAM staging — FM=0, game running,
             * SDRAM-only, entirely off the 68K's critical path. The
             * in-window publish is a bare copy. Convert a
             * batch of S16 tiles to MD 4bpp planar into the documented
             * dead FB space at 0x11A00 (1536 bytes past the image, below
             * the game's tile staging at 0x12000).
             * SELF-DESCRIBING AND IDEMPOTENT ON PURPOSE: FB staging is
             * PER-BANK, and that bank skew is exactly what broke the
             * palette path once already (see patch_game.py's 0x840000
             * note). Rather than reason about which bank the MD will
             * see, the packet carries its own base code; a stale read
             * just re-uploads tiles the MD already has, which costs a
             * VRAM write and corrupts nothing. */
            {
                /* PACKET at the dead FB block. Header is always valid;
                 * payload alternates tiles / name-table chunk.
                 *   [0] magic  [1] type  [2] param
                 *   [3] hscroll  [4] vscroll   (every window, cheap)
                 *   [8..] payload
                 * type 0: 40 tiles, param = first cache slot
                 * type 1: 280 name-table cells, param = first cell */
                uint16_t *sc = md_pkt;
                const layer_regs *bl = &snap[1];              /* BG layer */
                sc[3] = (uint16_t)(-(bl->vx0 & 7) & 0x3FF);
                sc[4] = (uint16_t)(bl->vy0 & 7);              /* plane B vy */
                sc[6] = (uint16_t)(snap[0].vy0 & 7);          /* plane A vy */
                sc[7] = (uint16_t)win_no;    /* sequence: the receiver's
                                              * tear detector (ares showed
                                              * packets torn mid-consume
                                              * by the bank flip — the
                                              * confetti sky) */
                /* DEMAND BIAS (suspect 2, ordering): a name-table chunk
                 * may reference a slot whose tile has not shipped yet.
                 * When a burst of new claims is outstanding, spend the
                 * window on a tile batch instead of the next chunk (the
                 * chunk cursor does not advance), so tiles chase the
                 * cells referencing them at 40/window instead of
                 * 40/(5 windows). */
                /* STARVATION BOUND (LOOP 13, the tick-row's true root):
                 * under sustained claim pressure (cutscene bursts)
                 * md_pending never drains below MD_BATCH, the demand
                 * bias fires every window, and the cell cursor FREEZES
                 * — plane A cells measured a uniform stale row against
                 * a varied md_dbg_nt mirror while slots kept being
                 * reassigned under them: foreign art = the dashes. At
                 * most 2 consecutive forced batches, then a cell chunk
                 * ships regardless. */
                if (md_phase == 0
                    || (md_pending >= MD_BATCH && md_forced < 2)) {
                    if (md_phase != 0) md_forced++;
                    /* ---- tile payload: ship dirty md_tag slots,
                     * pixels straight from cart ROM (legal under unpair,
                     * RV pinned 0 — same read tile_pixels does on miss).
                     * The render cache is NOT consulted: it is transient
                     * and its slots have nothing to do with md_tag's. ---- */
                    int sent = 0, sl = md_scan;
                    sc[2] = 0xFFFF;
                    for (int i = 0; i < NSETS * NWAYS && sent < MD_BATCH; i++) {
                        sl = (sl + 1) & (NSETS * NWAYS - 1);
                        if (!(md_dirty[sl >> 5] & (1u << (sl & 31))))
                            continue;
                        md_dirty[sl >> 5] &= ~(1u << (sl & 31));
                        uint32_t mkey = md_tag[sl];
                        if (mkey == 0xFFFFFFFFu)     /* never: dirty implies
                                                      * claimed; guard OOB */
                            continue;
                        const uint8_t *px =
                            altbeast_tiles + (mkey & 0xFFFFu) * 64;
                        const uint8_t *map =
                            mdp_s_map + ((mkey >> 16) & 0x7F) * 8;
                        if (sc[2] == 0xFFFF) sc[2] = (uint16_t)sl;
                        ((volatile uint16_t *)(sc + 8))[sent * 17] = (uint16_t)sl;
                        volatile uint8_t *o = (volatile uint8_t *)
                            ((volatile uint16_t *)(sc + 8) + sent * 17 + 1);
                        if (mkey & 0x80000000u) {
                            /* FG (Plane A) variant: pixel 0 stays PEN 0
                             * (transparent over Plane B) instead of
                             * taking the set's pixel-0 colour. */
                            for (int y = 0; y < 8; y++) {
                                const uint8_t *r = px + y * 8;
                                for (int kk = 0; kk < 4; kk++) {
                                    uint8_t a = r[kk * 2], b = r[kk * 2 + 1];
                                    *o++ = (uint8_t)(((a ? map[a] : 0) << 4)
                                                     | (b ? map[b] : 0));
                                }
                            }
                        } else
                        for (int y = 0; y < 8; y++) {
                            const uint8_t *r = px + y * 8;
                            for (int kk = 0; kk < 4; kk++)
                                *o++ = (uint8_t)((map[r[kk * 2]] << 4)
                                                 | map[r[kk * 2 + 1]]);
                        }
                        if (md_pending) md_pending--;
                        sent++;
                    }
                    md_scan = (uint16_t)sl;
                    sc[1] = 0;
                    sc[5] = (uint16_t)sent;
                    DIAG[57] += (uint32_t)sent;
                    if (md_phase == 0) md_phase = 1; /* forced batches do not
                                                      * advance the rotation */
                } else {
                    /* ---- name-table payload: 280 cells of the visible
                     * 40x28 window, mapped code -> cache slot. Cells whose
                     * code is not resident get the blank slot.
                     * PER-BAND REGISTER SELECTION, same rules as
                     * compose_layer: rowscroll bit 15 switches the band
                     * to the ALT page/scroll set (the stage-1 cloud band
                     * lives there — composing it from the primary regs
                     * garbled it the moment the camera panned), and
                     * primary-xscroll bit 15 makes the rowscroll word the
                     * band's xscroll. Coarse X goes into the cells; fine
                     * X ships as 7 per-strip hscroll words after the
                     * payload (MD cell-mode hscroll, one entry per 8
                     * screen lines — the band granularity matches S16's).
                     * KNOWN GAP: a band whose vy fine phase differs from
                     * the primary's is off by up to 7px (VSRAM is
                     * per-column, not per-row). */
                    /* SLICE FG0: phases 1-4 = Plane B (BG layer), phases
                     * 5-8 = Plane A (FG cat-0). Same walk, same
                     * allocator; FG keys carry bit 31 so the shipper
                     * emits the transparent-pixel-0 pattern variant, and
                     * FG cells that are empty or PRIORITY (cat-1 — still
                     * composed on the 32X) resolve to the blank slot. */
                    int isfg = (md_phase >= 5);
                    const layer_regs *wl = isfg ? &snap[0] : bl;
                    md_forced = 0;           /* cell chunk shipped: the
                                              * starvation bound resets */
                    int cell0 = ((isfg ? md_phase - 5 : md_phase - 1)) * 280;
                    volatile uint16_t *o = sc + 8;
                    for (int row = cell0 / 40; row < cell0 / 40 + 7; row++) {
                        const uint8_t *pqb = wl->pq;
                        int vxr = wl->vx0, vyr = wl->vy0;
                        if (wl->any_special) {
                            uint16_t rsw = wl->rs[row];   /* rs[28], row<28 */
                            if (rsw & 0x8000) {
                                pqb = wl->pq_a;
                                vxr = wl->vx0_a;
                                vyr = wl->vy0_a;
                            } else if (wl->xs_raw & 0x8000) {
                                vxr = (int)((0xC0 - (rsw & 0x3FF)) & 0x3FF);
                            }
                        }
                        int vx00 = vxr & ~7;
                        (sc + 8 + 280)[row - cell0 / 40] =
                            (uint16_t)(-(vxr & 7) & 0x3FF);
                        int vy = (vyr - (vyr & 7) + row * 8) & 0x1FF;
                        const uint16_t *pg0 = TILEMAP_C
                            + pqb[(((unsigned)vy >> 7) & 2)] * 0x800
                            + (((unsigned)vy >> 3) & 0x1F) * 64;
                        const uint16_t *pg1 = TILEMAP_C
                            + pqb[(((unsigned)vy >> 7) & 2) + 1] * 0x800
                            + (((unsigned)vy >> 3) & 0x1F) * 64;
                        for (int col = 0; col < 40; col++) {
                            unsigned vx = (unsigned)(vx00 + col * 8) & 0x3FF;
                            uint16_t w = ((vx >> 9) & 1 ? pg1 : pg0)[(vx >> 3) & 0x3F];
                            if (isfg && (w == 0 || (w & 0x8000))) {
                                /* FG: empty or cat-1 cell -> transparent */
                                md_dbg_nt[1120 + row * 40 + col] = MD_BLANK_SLOT;
                                *o++ = MD_BLANK_SLOT;
                                continue;
                            }
                            unsigned code = w & 0x1FFF;
                            if (code & 0x1000)
                                code = (code & 0xFFF) + (unsigned)bank1 * 0x1000u;
                            unsigned cset = ((unsigned)w >> 6) & 0x7F;
                            mdp_s_stmp[cset] = (uint8_t)win_no;
                            /* MD RESIDENCY ALLOCATOR (§16): md_tag, same
                             * set/way geometry as the render cache but
                             * STABLE — hit or claim keeps a slot as long
                             * as it stays on screen (re-stamped every ≤5
                             * windows); only the LRU way of a full set is
                             * evicted (see md_ref). The name-table pass
                             * is the demand source: a claim marks the
                             * slot dirty and the tile shipper uploads it.
                             * Free ways are a suffix, so first-free-way
                             * needs no later-way hit probe. Slot 1023 is
                             * RESERVED as the blank (suspect 1: it must
                             * never be allocatable, or an unresolvable
                             * cell is indistinguishable from a real one). */
                            uint32_t key = MD_KEY(code, cset)
                                | (isfg ? 0x80000000u : 0u);
                            unsigned s4m = CACHE_SET(code) * NWAYS;
                            unsigned slot = MD_BLANK_SLOT;
                            unsigned victim = MD_BLANK_SLOT, vage = 0;
                            int done = 0;
                            for (unsigned w2 = 0; w2 < NWAYS; w2++) {
                                unsigned i2 = s4m + w2;
                                uint32_t t = md_tag[i2];
                                if (t == key) {
                                    md_ref[i2] = (uint8_t)win_no;
                                    slot = i2;
                                    done = 1;
                                    break;
                                }
                                if (t == 0xFFFFFFFFu) {
                                    if (i2 == MD_BLANK_SLOT)
                                        break;   /* reserved: fall through
                                                  * to evict ways 0..6 */
                                    done = 1;
                                    mdp_note_tile(cset, code, isfg,
                                                  (uint8_t)win_no);
                                    md_tag[i2] = key;
                                    md_ref[i2] = (uint8_t)win_no;
                                    MD_MARK(i2);
                                    md_pending++;
                                    DIAG[53]++;          /* md_tag claims */
                                    slot = i2;
                                    break;
                                }
                                unsigned age = (uint8_t)
                                    ((uint8_t)win_no - md_ref[i2]);
                                if (age >= vage) { vage = age; victim = i2; }
                            }
                            if (!done && victim != MD_BLANK_SLOT) {
                                /* set full: evict the LRU way (see md_ref).
                                 * Cells still naming the victim rewrite
                                 * within 4 chunks (~5 windows). */
                                if (vage < 12) DIAG[39]++;   /* HOT evict:
                                                              * victim's cells
                                                              * likely still on
                                                              * screen */
                                mdp_note_tile(cset, code, isfg,
                                              (uint8_t)win_no);
                                md_tag[victim] = key;
                                md_ref[victim] = (uint8_t)win_no;
                                MD_MARK(victim);
                                md_pending++;
                                DIAG[50]++;              /* evictions */
                                slot = victim;
                            }
                            uint16_t ent = (uint16_t)(slot
                                | ((unsigned)mdp_s_line[cset] << 13));
                            md_dbg_nt[(isfg ? 1120 : 0) + row * 40 + col] = ent;
                            *o++ = ent;
                        }
                    }
                    sc[1] = 1;
                    sc[2] = (uint16_t)(cell0 | (isfg ? 0x8000 : 0));
                    sc[5] = 280;
                    md_phase++;
                    if (md_phase > 8) md_phase = 0;   /* 1 tile + 4 B + 4 A */

                    /* PALETTE DRIFT, 4 sets/window round-robin: fires
                     * only when a set's live colour differs from BOTH
                     * the pen it uses (uniform fades track via the
                     * owner refresh below — no fire) AND its assign
                     * snapshot (nearest-colour fallback pens differ by
                     * design — no fire). What remains is real
                     * structural change: colour cycling permutations,
                     * post-fade divergence of merged pens. */
                    for (int n = 0; n < 4; n++) {
                        unsigned s2 = mdp_chk = (uint8_t)((mdp_chk + 1) & 127);
                        if (!mdp_s_line[s2])
                            continue;
                        unsigned lb = (unsigned)(mdp_s_line[s2] - 1) * 16;
                        for (int p = 0; p < 8; p++) {
                            uint16_t lq;
                            unsigned pen;
                            if (!(mdp_s_used[s2] & (1u << p)))
                                continue;    /* pen never claimed */
                            lq = mdp_quant(PAL_SH[s2 * 8 + p]);
                            pen = mdp_s_map[s2 * 8 + p];
                            if (lq == mdp_line_c[lb + pen]
                                || lq == mdp_s_qc[s2 * 8 + p])
                                continue;
                            if (mdp_pen_rc[lb + pen] == 1
                                && mdp_pen_own[(lb + pen) * 2] == s2
                                && mdp_pen_own[(lb + pen) * 2 + 1] == p) {
                                /* SOLE-OWNER DRIFT: colour-cycle the pen
                                 * in place — no free, no invalidation,
                                 * no re-ship; the CRAM block carries the
                                 * new colour next window. This was the
                                 * dominant churn source ([37] ~85/min
                                 * mostly drift; title water cycles). */
                                mdp_line_c[lb + pen] = lq;
                                mdp_s_qc[s2 * 8 + p] = lq;
                                DIAG[51]++;          /* in-place recolours */
                                continue;
                            }
                            /* SHARED-PEN DRIFT: TOLERATED WITHIN A
                             * DISTANCE BOUND. The old unconditional
                             * free+reassign invalidated the whole set's
                             * slots ~1/sec (stage-2 statue-in-floor
                             * mess, two-state sky flip), so ffc8d27
                             * chose tolerance — and the demo walkway
                             * showed the cost's far end: sets 74-80's
                             * OPAQUE BG pixel 0 (jts16_prio.v:87)
                             * merged onto one pen at claim time whose
                             * owner (set 80) lives at quantised PURPLE
                             * 0x1C4 — a d^2=38 error painted across the
                             * bottom of the screen (Mike's "unset tile
                             * garbage", 2026-08-14). Small drifts stay
                             * tolerated (lockstep fades, cycling); a
                             * CATASTROPHIC diverge (d^2 >= 18, ~2+
                             * levels on every channel) re-claims via
                             * mdp_free_set — the set re-assigns against
                             * its LIVE colours at the next note_tile
                             * and its tiles re-ship within a rotation.
                             * Rate measured on MAME attract before
                             * enabling: DRQR[5] small / DRQR[6]
                             * catastrophic. */
                            {
                                uint16_t c0 = mdp_line_c[lb + pen];
                                int ddr = (int)(c0 & 7) - (int)(lq & 7);
                                int ddg = (int)((c0 >> 3) & 7)
                                        - (int)((lq >> 3) & 7);
                                int ddb = (int)((c0 >> 6) & 7)
                                        - (int)((lq >> 6) & 7);
                                unsigned dd = (unsigned)(ddr * ddr
                                            + ddg * ddg + ddb * ddb);
                                if (dd >= 18) {
                                    DRQR[6]++;   /* catastrophic drift */
#ifndef DRIFT_MEASURE_ONLY
                                    mdp_free_set(s2);
                                    break;       /* set gone; next set */
#endif
                                } else
                                    DRQR[5]++;   /* small, tolerated */
                            }
                            DIAG[38]++;              /* tolerated drifts */
                        }
                    }
                }

                /* LIVE CRAM REFRESH, every window: each used pen tracks
                 * its owner's current PAL_SH colour, so fades reach the
                 * MD plane at window cadence. 48 words at a fixed
                 * offset past both payload types. */
                for (int i = 0; i < MDP_LINES * 16; i++) {
                    uint16_t cw = 0;
                    if (mdp_pen_rc[i]) {
                        unsigned os = mdp_pen_own[i * 2];
                        unsigned op = mdp_pen_own[i * 2 + 1];
                        uint16_t q = mdp_quant(PAL_SH[os * 8 + op]);
                        mdp_line_c[i] = q;
                        cw = (uint16_t)((((q >> 6) & 7) << 9)
                                        | (((q >> 3) & 7) << 5)
                                        | ((q & 7) << 1));
                    }
                    sc[688 + i] = cw;
                }
                sc[0] = 0xB6B6;              /* magic LAST: header valid */
            }
#endif
        }
    }
}
