#include "mars.h"
#include "buildstamp.h"
#include "../md_src/packet_fmt.h"   /* DREQ packet format single source */

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

/* ---- SDRAM map (stacks: master grows down from 0x0603F800, slave from
 * 0x06040000 — floor 0x3F800; .bss ends well below 0x06018000, checked
 * per build). MASTER TOP WAS 0x3F000: the R60 call graph measured
 * 2176B deep and marched through ALL of md_pkt (0x3E780..0x3ED7F),
 * killing the B channel — 317 stack-slot writes caught at 0x3E780 by
 * MAME watchpoint. 2KB moved from the slave (flat compose loops,
 * shallow) to the master. Sentinels painted at boot under R60 verify
 * both watermarks from an ares dump. ----
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
/* CENSUS BLOCK (LOOP27 72). NOT DIAG: writes to DIAG[64] and beyond are
 * silently LOST in this build. Calibrated at m_main entry, a site that
 * must run exactly once: DIAG[62] and 0x2602FF00 both read 1 there,
 * DIAG[64] and DIAG[84] both read 0. Every SH-2-side counter above slot
 * 63 that the 2026-09-08 session quoted was reading residue, including
 * a "0.2% packet delivery" that was really 100%. CEN[10] carries that
 * same must-be-1 sanity count in every census build — check it before
 * believing any other slot. */
#define CEN ((volatile uint32_t *)0x2602FF00)
#ifdef HS_CENSUS
#define hsc_win  (*(volatile uint16_t *)0x26028D82)   /* vint counter (ISR entry) */
#define HSC_RING ((volatile uint16_t *)0x26028D40)   /* [16][2]: pkt win, flip win */
#define HSC_IDX  (*(volatile uint16_t *)0x26028D80)
/* 0x28D80 is ALSO win_pend (m_main.c, shipping under CLAIMNEW/NATIVE).
 * HS_CENSUS is probe-only, so the two must never build together. */
#ifdef NATIVE_FRAME
#error "HS_CENSUS ring at 0x28D80 collides with the shipping win_pend"
#endif
#endif
/* CLAIMNEW (LOOP27 77): the late claim scans the sprite list to decide
 * which colour sets need a pair. It has always scanned SPR_SNAP, the
 * snapshot the compose reads. When the snap latch SKIPS a refresh —
 * which it does whenever a compose chain is mid-flight, the exact case
 * entry 6 diagnosed — the claim is then scanning last frame's records
 * and never claims a pair for a set that arrived this frame. That set
 * draws with base 15, the shadow ramp. Scanning FB_SPR instead makes
 * the claim see the arriving records a window earlier; the two are
 * identical content whenever the refresh did happen, so this only
 * changes behaviour in the failing case. */
#ifdef CLAIM_NEW
#define CLAIM_SRC   FB_SPR
#else
#define CLAIM_SRC   SPR_SNAP
#endif
#define SPR_SNAP    ((volatile uint16_t *)0x26028400)   /* 512-word sprite-list
                                                         * snapshot: FB staging
                                                         * is BANK-DEPENDENT and
                                                         * the access bank isn't
                                                         * fs0 after the flip
                                                         * (ares defers the
                                                         * restore latch) — all
                                                         * sprite readers use
                                                         * this copy */
#define SYNC        ((volatile uint16_t *)0x26028800)   /* [0] cmd  [1] echo
                     * [8] slave compose-open band mask, [9] master's —
                     * ROW-DEFER (2026-08-25): bit rg set while band rg's
                     * half is mid-compose (cleared rows not yet redrawn);
                     * blit_half defers those rows so a bank never ships
                     * them. Single-writer per word: [8] slave, [9] master
                     * (boot init aside) — no cross-CPU RMW. */
/* ROW_DEFER needs ROWLIVE, which only DIRTY_ROW builds carry. */
#if defined(DIRTY_ROW) && !defined(NO_ROW_DEFER)
#define ROW_DEFER 1
#endif
/* Compose-open masks now UNCONDITIONAL (2026-08-26): ROW_DEFER is
 * retired but SYNC[9] gained a second consumer — the snapshot latch
 * (text_capture refreshes SPR_SNAP only when no compose half is
 * still reading it). Two stores per band per cycle. */
#define RD_OPEN_M(rgv)  (SYNC[9] |= (uint16_t)(1u << (rgv)))
/* SESSION 7 task 0: the RENDER cache is 512 slots (CSETS 64 x 8 ways,
 * 0x29000-0x31000). The upper half of the old 64KB block, 0x31000-
 * 0x39000, now holds .ramtext (mars.ld) so the 0x19000 region guard
 * stops fighting every build. The MD VRAM slot map keeps its full
 * NSETS x NWAYS = 1024 geometry (md_tag/md_ref/md_dirty) — the two
 * were coupled through CACHE_SET; MD_SET is the 128-set fold now. */
#define CACHE_C     ((uint8_t *)0x06029000)             /* 512 slots x 64B */

#define FB_STAGING  ((volatile uint16_t *)0x24012000)   /* game tile RAM */
#define FB_SPR      ((volatile uint16_t *)0x2401E000)   /* game sprite RAM */
#ifdef FB_TEXT_READ
/* LOOP 20 — TEXT IN PLACE. The game's text-RAM writes land here
 * (patch_game remap 0x85F000 under FBTEXT=1) and the SH-2 captures the
 * whole 2048-word region into TEXT_U each k1, replacing the DREQ text
 * chunks AND the 80-word regs/rowscroll prefix: the S16 keeps its layer
 * regs at text words 0x740-0x7FF, so they ride the same capture and
 * latch_layer_regs' TEXT_C reads are untouched.
 * 0x85F000 was FB_PAL — VERIFIED DEAD twice over: no SH-2 file
 * references the 0x1F000 region, and the "rotating quarters" copy that
 * fed it no longer exists in md_main.c (the LOOP 8 DREQ palette pairs
 * replaced it; only a stale comment survives).
 * Unlike the sprite list, text is NOT fully rewritten every vint, so
 * the flip needs a RESTORE: TEXT_U is the truth, written back into the
 * new draw bank right after the FBCTL flip (with restore_pages), so the
 * bank the game writes next already holds every glyph and the game's
 * own read-modify-writes see coherent RAM. */
#define FB_TEXT     ((volatile uint16_t *)0x2401F000)   /* game text RAM */
#endif
#if defined(K2_FREE) && (!defined(VISR_FLIP) \
                         || (defined(FB_SPR_READ) && !defined(R60)) \
                         || defined(PKT_SLIM) || defined(IDLE_TOKEN))
#error K2_FREE requires VISR_FLIP and excludes FBSPR/PKTSLIM/IDLETOKEN (LOOP24)
#endif
/* R60 + FB_SPR_READ is the docs/design/PIPELINE.md S1 strike: the snapshot runs
 * pre-flip in text_capture (slave-parallel), not in the LOOP20 k1
 * window the LOOP24 exclusion was written against. */
#ifdef K2_FREE
/* LOOP 24 K2FREE: the k2 packet lands in its OWN buffer — with the
 * same-vint push/harvest pairing, a k2 landing at SPR_LAND would
 * clobber the k1 records before the k2 snapshot copies them.
 * 0x394C0: right after SPR_LAND's 604-word arm (ends 0x394B8); the
 * slim k2 arm is 144 words = 288B, ends 0x395E0, clear of md_dbg
 * (0x39800). The 0x39A00..0x3A000 gap this buffer used to occupy now
 * holds md_pktA — the A-channel MD-plane staging (1472B, ends
 * 0x39FC0, clear of missq at 0x3A000). */
#define SPR_LAND_K2 ((volatile uint16_t *)0x260394C0)
static uint8_t  k2f_spr_ok;      /* the k1 packet validated at its own
                                  * window; the k2 snapshot may copy.
                                  * Cleared on a failed k1 harvest so the
                                  * snapshot keeps last frame's SPR_SNAP —
                                  * stale beats torn (an aborted push
                                  * leaves partial garbage in SPR_LAND). */
static uint16_t k2f_spr_landed;
#define md_pktA ((uint16_t *)0x06039A00)     /* A-channel packet staging */
static uint8_t k2f_pendA, k2f_pendB;         /* staging built, unpublished */
#endif
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
#define NSETS       128                 /* MD VRAM slot map sets */
#define NWAYS       8
#define CSETS       64                  /* render cache sets (task 0) */
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
#define cache_tag ((uint16_t *)0x0603A800)          /* CSETS*NWAYS words (1KB, ends 0x3AC00) */
#else
static uint16_t cache_tag[CSETS * NWAYS];   /* folded tile code; 0xFFFF empty */
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
/* GAME LAYOUT REMAPS (2026-09-05): the Japanese set 7 has the same tile
 * and sprite BYTES as the US set in a different ROM arrangement. MAME's
 * tile region places each plane's second 64KB half at +0x20000, so game
 * tile codes 0x4000-0x5FFF are the US 0x2000-0x3FFF and 0x2000-0x3FFF is
 * a hole; its sprite region places each 128KB pair 0x40000 apart, so the
 * game's bank values are even. Both fold onto the US images. */
#ifdef GAME_ALTBEASTJ
#define GAME_TILE_REMAP(c) do { if ((c) >= 0x4000) (c) -= 0x2000; \
                                else if ((c) >= 0x2000) (c) = 0; } while (0)
#define GAME_SPR_BANK(b) (((b) >> 1) & 7)
#else
#define GAME_TILE_REMAP(c) ((void)0)
#define GAME_SPR_BANK(b) ((b) & 7)
#endif
#define MD_KEY(code, cset) (((uint32_t)(cset) << 16) | (code))
/* C1 (2026-09-02): a cat-1 FG tile claims a pen line only if one FITS
 * without evicting — the lines are the scarce resource (3 x 15 pens,
 * measured 1 free pen per line in Mike's states) and cat-1 cells have
 * the FB fallback (CAT1_PEND); cat-0 cells have nothing. */
#ifdef CAT1_MD
#define C1_SOFT (isfg && (w & 0x8000))
#else
#define C1_SOFT 0
#endif
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
/* BGPACK2 (LOOP29 121): the pack was sized against 45 pens but the
 * background only ever holds 30 DISTINCT COLOURS after the 9-bit MD
 * decode — 10 of its 40 CRAM entries are duplicates, measured at 8
 * frames across level-1 (29,30,30,30,30,30,30,30). Two lines therefore
 * hold it with ZERO colour loss and free MD CRAM line 3 for a second
 * sprite palette (set 0x0A = 25.3% of all sprite records). Per-scene:
 * m_main.c:288 claims a worst BG window of 36 distinct, which does NOT
 * fit two lines, and the attract scenes are unsampled — so this stays
 * default-off until a play pass says otherwise. */
#ifdef BG_PACK2
#define MDP_LINES  2
#else
#define MDP_LINES  3
#endif
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
#ifdef DIRECT_FB
#define DFB_SHADOW_PIX(sx) \
    if ((sx) & 1) row[sx] = shadow_lut[0];
#else
#define DFB_SHADOW_PIX(sx) { \
    uint8_t up_ = urow[sx]; \
    if (up_)          row[sx] = shadow_lut[up_]; \
    else if ((sx) & 1) row[sx] = shadow_lut[0]; }
#endif
#define mdp_s_qc   ((uint16_t *)0x0603C920) /* [128][8]; ends 0x3D120 */
#define mdp_s_stmp ((uint8_t  *)0x0603D120) /* [128] LRU stamp; ends 3D1A0 */
#define mdp_pen_own ((uint8_t *)0x0603D1A0) /* [3][16][2] owner set,pixel
                                             * -- drives the live CRAM
                                             * refresh; ends 0x3D200 */
#ifdef NT_WRAP
/* LOOP15 wrap protocol: per view row, (prow<<8)|c0 — where that row's
 * cells landed in the wrapped 64x32 plane. Fixed block 0x3E780 (free
 * space after pri_lut; audit reads it from bs9 states). [2][28].
 * md_dbg_hs = last SHIPPED per-strip hscroll (delta tracker for the
 * all-strips hs tail section every type-1 packet carries). Both are
 * fixed blocks: boot inits them to 0xFFFF explicitly. */
/* Fixed at 0x39800: the verified-free gap between the DREQ landing
 * (SPR_LAND 0x39000 + 852 words = 0x396A8 max, WIN_TWO) and missq at
 * 0x3A000 — 2.3KB nobody owns (grep'd the whole fixed map). WIN_TWO's
 * code growth pushed .bss 256B over the guard; these moved here.
 * Boot-inits to 0xFFFF explicitly (fixed blocks are never zeroed).
 * (History: first placement 0x3E380 collided with mdp_s_used — slot
 * collision #6; then .bss; now here.) */
#define md_dbg_base ((uint16_t *)0x06039800)  /* [56] */
#define md_dbg_hs   ((uint16_t *)0x06039870)  /* [56] */
#endif
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
/* scratch 0x28C88 (region-guard diet 2026-09-01; boot-zeroed) */
#define miss_n ((volatile uint16_t *)0x26028C88)

#ifdef BLIT_SKIP
/* LOOP 18 job 1, ATTEMPT 2. Per-BANK, per-row, 10-bit "this 32px group
 * is already all-zero in this bank" mask: 2 banks x 224 rows x 2 bytes
 * = 896B at 0x3A300 — the verified-free window between missq (768B,
 * ends 0x3A300) and cache_tag (0x3A800), so 384B still spare.
 * WE PAGE-FLIP, so a skipped group does not keep LAST frame's pixels —
 * it keeps the pixels from TWO frames ago, in THAT bank. Skipping is
 * only sound against the TARGET BANK's own history, which is what this
 * mask is and why it is indexed by bank.
 * UNCACHED alias on purpose: attempt 1 died of the two CPUs disagreeing
 * about which bank they were writing, and a stale cache line is the
 * same bug wearing a different hat. One 16-bit read plus at most one
 * 16-bit write per row is nothing against 320 bytes of FB traffic.
 * Boot must zero it (fixed blocks are never .bss-cleared). 0 = "not
 * known to be clear" = write it — the safe direction, so a lost or
 * garbage mask costs speed, never pixels. */
#define FBCLEAR ((volatile uint16_t *)0x2603A300)      /* [2][224] */
#endif

#ifdef MD_ALLOC_WHY
/* LOOP29 153: the MD residency allocator's OWN counters. Entry 152 hit
 * the DIAG minefield's new arm -- [39], [50] and [53] are each written
 * by three subsystems (the DREQ landing path among them), so every
 * claims/evictions figure read out of them was the DREQ counters.
 * IN .bss, NOT a fixed scratch address: the first cut of this block sat
 * at 0x3A680, the 384B the FBCLEAR comment calls spare, and read back
 * 0x01010101 -- that window is NOT free, and fixed blocks are never
 * boot-zeroed anyway. .bss is zeroed and the region guard has ~19KB.
 * Read the `mdalloc_ctr` address out of rom/s16.lst (the MTASKWHY
 * pattern); tools/md_alloc_why.py does it for you.
 *   [0] cells visited      [1] key already resident (hit)
 *   [2] free-way claims    [3] evictions (set full)
 *   [4] cat-1 slot-pressure declines (cell kept on the FB)
 *   [5] blanked: slot dirty, cut mode   [6] blanked: slot dirty, no cut
 *   [7] blanked: FG cell empty          [8] blanked: bottom band
 *   [9] mds_flush calls    [10] tags wiped by mds_flush
 *  [11] mds_install calls  [12] tags wiped by mds_install's changed[]
 *  [13] mdp_free_set calls [14] tags wiped by mdp_free_set
 * PROBE ONLY. */
volatile uint32_t mdalloc_ctr[32];
/* [15] free_set calls declined because the set is pinned by the scene
 * table. mdalloc_relo[s] = times colour set s was relocated; the pair
 * says whether the churn is in sets the baked table names or outside
 * it. mdalloc_pin[s] = mds_pin[s] sampled at the last relocation. */
volatile uint32_t mdalloc_relo[128];
/* per colour set: tile slots wiped WHILE ON SCREEN. 156 showed the call
 * count is the wrong ranking (set 33 takes 44 of 57 frees and owns no
 * on-screen tile); this is the one that says which sets the scene table
 * must cover. */
volatile uint32_t mdalloc_onscr[128];
/* LOOP29 158: WHAT IS SET 33? It is 65% of the on-screen tiles destroyed
 * (156) and it never appears in a tile harvest (157), so identify it at
 * the event: [0] cset [1] its MD line [2] the owner set it conflicts
 * with [3] on-screen cells naming its slots [4] tiles wiped, then 16
 * tile codes. Armed once, for the first free of the set in MDA_WATCH. */
#ifndef MDA_WATCH
#define MDA_WATCH 33
#endif
volatile uint32_t mdalloc_id[24];
volatile uint8_t  mdalloc_pin[128];
#define MDA(i) (mdalloc_ctr[i]++)
#define MDA_ADD(i, n) (mdalloc_ctr[i] += (uint32_t)(n))
#else
#define MDA(i) ((void)0)
#define MDA_ADD(i, n) ((void)0)
#endif

#ifdef DIRTY_ROW
/* LOOP 18 job 2. "Is sbuf row R entirely zero right now?" — the one
 * fact that lets the blit skip a row WITHOUT READING IT, which is the
 * only thing that recovers a row's full cost. Four ares probes said the
 * blit is throughput-bound and partial removal returns sub-linearly:
 * dropping 57% of the stores kept 86% of the cost, dropping 79 of 80
 * loads kept 71%. Rows, whole, or nothing.
 *
 * WHY A "WAS IT WRITTEN" FLAG DOES NOT WORK: under MD_BG the background
 * lives on the MD plane and sbuf is EXPLICITLY ZEROED every row every
 * cycle (see the clear in slave_concurrent_k). Every row is written
 * every cycle, so a write-mark marks everything. What we need is a
 * CONTENT fact, and the clear is the thing that establishes it: the
 * clear sets "this row is zero", and every draw that puts a pixel in a
 * row clears that.
 *
 * CONSERVATIVE IN ONE DIRECTION ONLY. A row wrongly marked LIVE costs a
 * blit we did not need. A row wrongly marked ZERO is a dropped layer.
 * So every mark site marks on INTENT TO DRAW, before knowing whether
 * any pixel survived its transparency test — the tile paths mark their
 * whole row range, not the rows that turned out to have pens.
 *
 * Indexed by SBUF row (0..231), not screen row, so no call site has to
 * remember the +8. 232 bytes at 0x3A680, FBCLEAR's own tail; uncached
 * because both CPUs write it, same reasoning as FBCLEAR. */
#define ROWLIVE ((volatile uint8_t *)0x2603A680)       /* [232] by sbuf row */
/* C1: per screen tile-row (28) "a cat-1 cell here is NOT yet resident
 * on the MD" (slot dirty or colour set unassigned) — written by the
 * master's plane walk, read by the slave's cat-1 pass, which then
 * draws that row in the FB as before. 0x28F60-0x28F7B: the audited-
 * free 32B span (docs/design/INTEGRATION.md). Boot-zeroed. */
#define CAT1_PEND ((volatile uint8_t *)0x26028F60)      /* [28] */
/* 0x3A680 map, all inside FBCLEAR's 384-byte tail below cache_tag:
 *   3A680 ROWLIVE [232]        ends 3A768
 *   3A768 DRVC    [3] u32      ends 3A774   (DIRTY_ROW_VERIFY)
 *   3A774 BSCNT   [6] u32      ends 3A78C   (BLIT_SKIP_COUNT)
 * ROWSTALE_PROBE's per-bank hash table also wants 3A680 and is 384B —
 * it cannot coexist. Say so at compile time rather than discover it as
 * a slot collision; this file has already paid for six of those. */
#if defined(ROWSTALE_PROBE)
#error "ROWSTALE=1 and DIRTYROW=1 both claim 0x3A680 - build them separately"
#endif
#define DRVC ((volatile uint32_t *)0x2603A768)
#ifdef DIRECT_FB
/* ONE-BANK GHOST (2026-08-27): ROWLIVE is one fact for TWO banks. A
 * row wiped in bank A goes dead; two cycles later bank B's clear
 * skips it and B keeps its old sprite pixels forever — the load-in
 * patches (pens 244-248, one bank). Per-bank history: marked at draw
 * time into the CURRENT draw bank, cleared when that bank's row is
 * wiped. The clear wipes when ROWLIVE (live now) OR the bank's own
 * history says it holds content. */
static uint8_t dfb_hist[2][232];
static uint8_t dfb_curbank;
#define RL_MARK(sr)  (ROWLIVE[(sr)] = 1, dfb_hist[dfb_curbank][(sr)] = 1)
#else
#define RL_MARK(sr)  (ROWLIVE[(sr)] = 1)
#endif
#define RL_ZERO(sr)  (ROWLIVE[(sr)] = 0)
#ifdef ROW_GEN
/* ROWGEN v2 (SESSION 7, 2026-09-07): WRITE-KNOWLEDGE ROW SKIP FOR THE
 * SHIP ONLY. The blit is LOAD-bound (BLITHASH skipped 384 group
 * writes/vint and measured no faster), so the only row cost that can
 * be removed is the row that is never READ: a row whose sbuf content
 * provably equals what the target bank already holds. The compose is
 * left exactly as it is (every row cleared and recomposed every
 * generation — sbuf is always the truth); what changes is which rows
 * blit_half reads and writes.
 * Per bank, an ACCUMULATED dirty mask (master-written only — no
 * cross-CPU RMW anywhere in this scheme):
 *   launch(g): D_g = spans(snapshot g) | spans(snapshot g-1) | pending
 *              marks (text rows that changed at capture, scroll strips
 *              that moved, new-scene) — or ALL rows when anything the
 *              cat-1 pass depends on moved: a latched layer register,
 *              a tilemap page, a cut, a palette-group reassignment.
 *              ACC[0] |= D_g; ACC[1] |= D_g.
 *   ship(g) to bank X (window, before the slave is posted):
 *              SHIPM = ACC[X] | pending | deferred[X]; ACC[X] = 0.
 *   blit_half: rows outside SHIPM are not touched; a row the ROW_DEFER
 *              policy declines is re-marked in deferred[cpu][X] so the
 *              next ship to X carries it.
 * spans(g-1) is what the first version missed: a row a sprite VACATED
 * is dirty in the frame it vacates, but only the previous snapshot
 * knows the sprite was there (stale sprite blocks, 2026-09-07 f1199).
 * ROWGENVERIFY=1 reads every skipped row back from the FB uncached
 * and counts mismatches in DIAG[11] — the only proof that counts. */
/* MASTER-ONLY state lives in cached .bss (the ISR and the body are the
 * same CPU); only what the SLAVE reads (the ship mask) or writes (its
 * deferred rows) goes through the uncached alias. The first cut kept
 * everything uncached and cost ~2 lines/frame in RMW traffic. */
static uint32_t rg_acc[2][8];              /* per-bank dirty since its ship */
static uint32_t rg_pend[8];                /* marks since the last build */
static uint32_t rg_spprev[8];              /* spans of snapshot g-1 */
static uint32_t rg_spcur[8];               /* spans of the latest snapshot
                                            * (built inside the copy loop) */
static uint32_t rg_txth[28];               /* per-text-row hash at capture */
static uint32_t rg_flags;                  /* bit0 = all dirty */
static uint32_t rg_pgcap;                  /* pages whose CONTENT changed at
                                            * capture since the last build */
static uint32_t rg_lrhash;                 /* FG layer regs signature */
static uint32_t rg_palsig[2];              /* per-parity pen-map signature */
static uint32_t rg_lsig, rg_lpar;          /* signature/parity at launch */
static uint32_t rg_shared[8 + 32];         /* [0..7] ship mask, then
                                            * deferred[cpu][bank][8] */
#define RGU        ((volatile uint32_t *)((uint32_t)rg_shared | 0x20000000u))
#define RG_ACC(b)  (rg_acc[b])
#define RG_SHIPM   (RGU)
#define RG_PEND    (rg_pend)
#define RG_FLAGS   (rg_flags)
#define RG_SPPREV  (rg_spprev)
#define RG_DEFER(cpu, b) (RGU + 8 + ((cpu) * 2 + (b)) * 8)
#define RG_LRHASH  (rg_lrhash)
#define RG_PALSIG  (rg_palsig)
#define RG_LSIG    (rg_lsig)
#define RG_LPAR    (rg_lpar)
#define RG_PGCAP   (rg_pgcap)
#define RG_MARK_SPAN(y0, y1) do { \
        for (int _y = (y0); _y < (y1); _y++) \
            RG_PEND[_y >> 5] |= 1u << (_y & 31); } while (0)
#define RG_SHIP(y) (rg_shipl[(y) >> 5] & (1u << ((y) & 31)))
#define RG_COUNT ((volatile uint32_t *)0x26038E00)   /* probe scrap: [0]
                                              * rows skipped [1] verify
                                              * mismatches [2] verify
                                              * checks [3] all-dirty gens
                                              * [4] gens */
#endif
#else
#define RL_MARK(sr)  ((void)0)
#define RL_ZERO(sr)  ((void)0)
#ifdef CAT1_MD
#error "CAT1MD=1 needs DIRTYROW=1 (ROWLIVE gates the FB cat-1 pass)"
#endif
#endif

#ifdef SPR_LINE_PROBE
/* LOOP 18 / Mike's architecture question: COULD THE MD VDP'S SPRITE
 * HARDWARE DRAW THESE SPRITES, the way MDBGALL moved the background to
 * its tile planes? That move is what made 62.7% of framebuffer groups
 * transparent and unlocked everything since; the sprite chip is the
 * same trade one layer up, and it is currently idle while two SH-2s
 * software-rasterize ~13 sprites and then blit 71,680 bytes.
 *
 * The count is already inside budget — SPRTRUNC measured a mean of 12.5
 * live records, max 21, against the MD's 80 hardware sprites. What
 * decides feasibility is the PER-LINE budget, which in H40 is 20
 * sprites and 320 sprite-pixels per scanline. So measure that, per
 * scanline, over real play.
 *
 * Width comes from the sprite pitch: S16B sprite data is 4bpp packed,
 * a word is 4 pixels, so width = |pitch| * 4. An MD sprite is at most
 * 4x4 cells, so a row of an S16 sprite needs ceil(width/32) of them.
 * Counted on INTENT (before clipping and transparency), because the MD
 * charges a sprite's full width against the line budget whether or not
 * its pixels are opaque.
 *
 * Arrays live in .bss — which the sbuf purchase made affordable — and
 * are reached through the UNCACHED alias, because both CPUs accumulate
 * into them (disjoint rows, but the fold at k1 must see both). */
static uint16_t sl_buf[224 * 3];
#define SL     ((volatile uint16_t *)((unsigned)sl_buf | 0x20000000u))
#define SL_SPR(y) SL[(y)]
#define SL_PX(y)  SL[224 + (y)]
#define SL_MD(y)  SL[448 + (y)]
/* [0] line-instances needing >20 MD sprites   [1] needing >320 px
 * [2] worst MD sprites on a line              [3] worst px on a line
 * [4] line-instances counted                  [5] sprites that CANNOT
 *     go to MD hardware at all (zoomed, gated, or shadow)
 * THE SECOND GATE, and the one that can kill the hybrid outright:
 * an MD hardware sprite picks ONE of the VDP's four 16-colour lines,
 * and MDBGALL is already spending those on the tile planes. S16 sprites
 * index 64 colour sets. So what matters is how many DISTINCT sets are
 * live in a frame — if that fits in the lines left over, hardware
 * sprites are palettizable; if it does not, the geometry result is
 * irrelevant.
 * [6] worst distinct sets in one cycle (all sprites)
 * [7] worst distinct sets among MD-ELIGIBLE sprites only
 * [8..14] histogram of eligible distinct-set count: 1,2,3,4,5-6,7-8,>8
 * [15] cycles counted */
#if defined(SPR_LATE_DIAG) || defined(FB_PROBE)
#error "SPR_LINE_PROBE's SLC is 0x3A780, which SPR_LATE_DIAG (SPRLATE) and \
FB_PROBE (FBP) also claim - collision #16, build them separately"
#endif
#define SLC ((volatile uint32_t *)0x2603A780)
#endif

#ifdef SPR_LATE
/* [0] sets the pre-built map missed   [1] of those, no free pair left
 * [2] claimed in time   [3] sprites that STILL drew with pr==0xFF, i.e.
 * in the shadow ramp — the number this whole change exists to zero.
 * 0x3A7D8, the tail of FBCLEAR's spare block (SLC ends 0x3A7D8). */
/* CACHED alias on purpose, unlike every other fixed block here. MAME's
 * SH-2 debugger space serves ONLY 0x06......: a lua read at 0x26......
 * returns a clean zero that reads exactly like "never incremented"
 * (proven with a boot sentinel — see TOOLKIT.md). This is a probe
 * counter, not protocol state, so a racy increment between the two CPUs
 * costs nothing and being READABLE is worth more than being exact. */
#ifdef SPR_LATE_DIAG
#define SPRLATE ((volatile uint32_t *)0x0603A780)   /* 16 words, ends 3A7C0 */
#else
/* Lean build uses only [0..3]; park them at 0x3A7D8, the address the
 * Makefile header, tools/sprlate_probe.lua and the scoring rig all
 * read. 10 words end exactly at cache_tag (0x3A800) — never cross it. */
#define SPRLATE ((volatile uint32_t *)0x0603A7D8)
#endif
/* PEN USAGE PER COLOUR SET, 64 x u16 bitmask of pixel values actually
 * drawn. Two sets whose USED pens are disjoint can share one 16-entry
 * pair with NO per-pixel remap — the draw stays `base + pixel`. That is
 * the only sharing that is free, and S16 art is known to leave garbage
 * in arcade-invisible entries (see mdp_s_used on the tile side), so the
 * used set can be far smaller than 15. 0x3A7C0, ends 0x3A840 — past
 * cache_tag at 0x3A800, so PROBE BUILDS ONLY (they do not run MD_BG's
 * tag array and this flag is never shipped). */
#define SPRPEN ((volatile uint16_t *)0x0603A7C0)
#ifdef SPR_LATE_DIAG
#define PENTAP(sc, pix) (SPRPEN[(sc) & 0x3F] |= (uint16_t)(1u << (pix)))
#else
#define PENTAP(sc, pix) ((void)0)
#endif
static uint32_t sl_set[2], sl_seteg[2];      /* 64-bit used-set masks */
/* HOW MANY RASTER SWAPS WOULD IT TAKE? The port fires ONE HINT per
 * frame (md_start.s: reg 0x8A = 0xDF, counter 223, "arcade-style") and
 * _hblank is an rte, so the three MD palette lines are STATIC for the
 * whole frame and must serve every colour set at once. The standard MD
 * answer is to rewrite CRAM mid-frame — but per-SCANLINE swapping costs
 * 224 interrupts on a 68K that is already the bottleneck.
 * The useful question is therefore not "can we swap" but "how FEW swaps
 * buy the demand", because sprites cluster vertically. Accumulate the
 * colour-set mask per 28-line SPAN; merging adjacent spans then gives
 * the 4-swap and 2-swap answers for free from the same data. */
#define SL_SPANS 8                           /* 224 / 28 */
static uint32_t sl_span[SL_SPANS][2];
/* SWAP ANSWERS IN .bss (LOOP29 120). These used to report into
 * SLC[16..21] at 0x3A780+0x40 = 0x3A7C0, which is SPRPEN's address and
 * inside SPRLATE's lean block — collision #16. The census read 403
 * distinct colour sets out of a possible 64 and had clearly never been
 * believed by anyone. volatile: nothing reads them, so LTO would
 * dead-store them (the mdspr_why lesson, LOOP29 119). */
static volatile uint32_t sl_ans[8];   /* [0] worst-frame sets [1] w8 [2] w4
                                       * [3] w2 [4] fits3@8 [5] fits3@4
                                       * [6] fits3@2 [7] frames */
static unsigned sl_pop(uint32_t a, uint32_t b)
{
    return (unsigned)(__builtin_popcount(a) + __builtin_popcount(b));
}
#endif
#ifndef SPR_LATE
#define PENTAP(sc, pix) ((void)0)
#endif

#ifdef FBSPR_PROBE
/* LOOP 20 step 1 — CAN THE SPRITE LIST BE READ IN PLACE AT FB_SPR?
 * The DREQ push (48.6 of the 68K's 64-line handler, the single largest
 * cost in the pipeline) exists because the in-place read was tried and
 * RETIRED: "the game's vint upload crossed the FB window exactly when
 * the SH-2 owned the FB, and ares/hardware DISCARD those MD writes -
 * savestate-proven 40/64 torn records". That measurement predates the
 * unpair rework, the window schedule, and Presentation 2.0's flip
 * protocol - the configuration changed underneath it, which is this
 * repo's most repeated trap. So re-measure; do not inherit the verdict.
 * The MD shim pushes an ORDERED list (it walks the game's order table),
 * so FB_SPR's raw slots cannot be compared positionally. Compare by
 * CONTENT instead: for each landed record, scan all 64 raw FB_SPR
 * records for a word-identical match. A landed record with no match is
 * data the FB window never received (a dropped or torn write).
 * [0] records compared  [1] records with NO match in FB_SPR
 * [2] cycles probed     [3] cycles with at least one miss
 * Counters at 0x3A780 (SPR_LATE owns them otherwise - do not combine
 * the two flags). MEASURE ONLY, NEVER SHIP, and build WITHOUT SPRTRUNC:
 * in MAME a SPRTRUNC list never lands and everything reads zero. */
#define FBP ((volatile uint32_t *)0x0603A780)
#endif

#ifdef BLIT_SKIP
#if defined(BLIT_SKIP_VERIFY) && !defined(BLIT_SKIP_COUNT)
#define BLIT_SKIP_COUNT 1
#endif
#ifdef BLIT_SKIP_COUNT
/* Skip-rate meter, in FBCLEAR's own spare tail (0x3A680, 384B free
 * below cache_tag at 0x3A800). "Identical pixels" proves nothing until
 * you know the skip FIRED — a mask that never credits anything is
 * trivially correct and worth nothing. Indexed by which CPU owns the
 * row under WIN_TWO (slave 0-56 and 112-168, master the rest) so the
 * two CPUs never race the same word: [0]/[1] slave groups/skipped,
 * [2]/[3] master. [4]/[5] under BLIT_SKIP_VERIFY: skips where the
 * framebuffer did NOT actually hold zero — i.e. THE MASK LIED. */
#define BSCNT ((volatile uint32_t *)0x2603A774)
#endif

/* THE BANK IDENTITY, and the whole reason attempt 1 was reverted.
 * Attempt 1 sniffed `MARS_VDP_FBCTL & MARS_VDP_FS` inside blit_half.
 * That is a RACE: the k2 flip sits between the k1 blit and the k2 blit,
 * both CPUs blit around it, and the slave can read FS on the wrong side
 * of an edge the MASTER owns — wrong half of the mask, needed writes
 * skipped, stale groups marked clear (Zeus's head vanished; a yellow
 * block stood).
 * So DO NOT SNIFF. The master already owns the only FS write in the
 * program (the k2 flip); it keeps the parity here and PUBLISHES it to
 * the slave in the blit command word (bit 6), which is posted to
 * SYNC[4] *after* the flip. Both CPUs then blit a window with the same
 * label by construction, with no shared-state read at all.
 * The label need not match physical FS — it only has to TOGGLE when the
 * hidden bank changes. Boot starts it at 0 against an unknown bank; the
 * all-zero mask makes that first cycle write everything, and the two
 * labels are self-consistent from then on.
 * Toggled at the FBCTL WRITE, not the latch: "once the write is issued
 * the flip is committed" (see the late-latch note at the flip). */
static uint8_t fb_draw_par;

/* Keeps non-BLIT_SKIP builds byte-identical: the extra argument does
 * not exist there, so the canonical rom is unchanged. (Region guard has
 * 280 bytes spare — this is not the place to spend them.) */
#define BLIT_HALF(lo, hi, bk) blit_half((lo), (hi), (bk))
#else
#define BLIT_HALF(lo, hi, bk) blit_half((lo), (hi))
#endif

#ifdef DIRECT_FB
/* DIRECTFB STAGE-1 FLIP GATE. Set at the k2 chain launch, consumed by
 * flip_span. Under the blit pipeline an AUTO-30 launch skip was
 * harmless — the k2 blit re-shipped sbuf's last coherent frame, so the
 * flip showed a one-frame HOLD. With compose writing the FB directly,
 * a skipped interval leaves the draw bank TWO intervals old, and
 * flipping it would step the display BACKWARD one frame (and with
 * compose_skipped ~50% of cycles under load, half the flips would).
 * So a flip only ships if its interval actually composed; a declined
 * flip is exactly the K2_FREE edge-guard decline the 68K already
 * understands (F1FF release, truth kept, cycle_dirt carried).
 * Master-only, ISR-read (VISR_FLIP runs flip_span in the V-ISR):
 * volatile like visr_flip_done. */
static volatile uint8_t dfb_drawn;
/* A/B BISECT KNOB (2026-08-26, kept for the stage-2 session): 1 =
 * flip every k2 even when the interval composed nothing. That arm
 * steps the display BACKWARD one frame on every AUTO-30 skip (~45% of
 * k2s under load) but HIDES the load-in pair-thrash by alternating
 * banks: ramp-blob frames 15/356 (hold) vs 6/356 (no-hold) vs 3/356
 * (canonical) on the play2 battery — the thrash (map_missed 200->350)
 * is the same in both arms; the hold just keeps the ramped frames on
 * screen. Hold is the faithful translation of the old pipeline's
 * re-blit hold, so it ships as default; the map-miss growth is the
 * bug to chase. */
enum { dfb_nohold = 0 };
#endif

#ifdef NATIVE_FRAME
/* NATIVE WHOLE-FRAME PIPELINE (docs/design/NATIVE.md, branch native1). One
 * generation in flight and it is the whole frame; the band as a
 * schedulable unit is deleted. File-scope on purpose: a block static
 * near the launch site read 0 every entry (the DIAG[36] .bss-wipe
 * open bug) — these live with the other proven file-scope state. */
#ifndef NAT_DRAIN_CUT
#define NAT_DRAIN_CUT 11300   /* FRT ticks after the vint past which the
                               * master stops draining maps / composing,
                               * to be at the window on time. The maps
                               * drain sits on the close path through
                               * THIS number (2026-09-04): a launch at
                               * 0.38v leaves ~0.47v of drain before it. */
#endif
#ifdef HS_SHIP
static void hs_close(void);
static void hs_promote(void);
#define HS_PROMOTE() hs_close()
#else
#define HS_PROMOTE() ((void)0)
#endif
static uint8_t nat_gen_open;     /* a whole-frame compose is in flight */
static uint8_t nat_gen_ready;    /* closed generation awaiting its blit */
#ifdef BLIT_CHASE
#ifndef LAUNCH_EARLY
#error BLIT_CHASE needs LAUNCH_EARLY (the launch site it blits after)
#endif
static uint8_t nat_ship_now;     /* BLIT CHASE: this window's blit is in
                                  * flight (slave half posted, master
                                  * half after the launch) — the launch
                                  * may open a generation over a READY
                                  * one because the fence (SYNC[14])
                                  * orders the slave behind the blit */
#endif
static volatile uint8_t nat_shipped;  /* hidden bank holds a fresh whole
                                       * frame; ISR-consumed at the flip
                                       * (volatile like visr_flip_done) */
static uint8_t nat_mtask;        /* master per-gen tail: 1-3 text half-
                                  * bands, 4 build_maps chunks, 0 idle */
static uint8_t nat_par;          /* the generation's compose parity */
static uint8_t nat_spr_ok;       /* last landing carried whole records:
                                  * SPR_LAND -> SPR_SNAP at next launch
                                  * (stale beats torn, gen-latched) */
static uint16_t nat_rec0, nat_nrec;   /* that landing's record layout */
static uint8_t nat_skip_run;     /* consecutive overrun windows (wedge
                                  * belt: 8 forces a fresh launch) */
static uint16_t nat_t0;          /* launch FRT stamp — the gen-wall
                                  * census below (0x28F50 sum / F54
                                  * count / F58 max, PICKUP_SRC_PROBE's
                                  * scrap — probe-only overlap): wall =
                                  * launch -> close, the number the
                                  * schedule is built around. Wraps at
                                  * 5.4 vints; the belt fires first. */
#ifndef MD_BG
#error "NATIVE_FRAME assumes MDBGALL (row clears are MD-through fills)"
#endif
#if ((36 + BAND_SHIFT) & 7) || ((184 + BAND_SHIFT_RG2) & 7)
#error "NATIVE_FRAME: 36+BANDSHIFT and 184+RG2SHIFT must be multiples of 8 (text-row ownership splits there)"
#endif
/* THE 60HZ REBALANCE (oracle arc, 2026-08-31): the master owns rows
 * NAT_MLO..NAT_MHI of each band OUTRIGHT — clear, sprites, cat1 and
 * the text rows over them — as mtask stage 1, in 12-row strips under
 * the old strip quiet-zone numbers. Row-partitioned ownership: the
 * slave's inline text range ends where the master's rows begin, so
 * no cross-CPU write ordering exists to get wrong (the old executor's
 * master-text-into-slave-rows overlap is deleted, not inherited).
 * BANDSHIFT=36 / RG2SHIFT=40 make every master range EMPTY — the
 * all-slave configuration Mike passed ships byte-equivalent. */
#define NAT_MLO(rg) ((rg) == 2 ? (184 + BAND_SHIFT_RG2) \
                               : ((rg) * 72 + 36 + BAND_SHIFT))
#define NAT_MHI(rg) ((rg) == 2 ? 224 : ((rg) * 72 + 72))
#define NAT_TB(rg)  ((rg) == 2 ? ((184 + BAND_SHIFT_RG2) >> 3) \
                               : (((rg) * 72 + 36 + BAND_SHIFT) >> 3))
#define NAT_TE(rg)  ((rg) == 2 ? 28 : (((rg) + 1) * 9))
/* BANDSHIFT=36 / RG2SHIFT=40: every master range is EMPTY (docs/design/ORACLE.md:
 * the rebalance measured dead, 4 points). Compile stage 1 out then —
 * the launch goes straight to the maps drain (2026-09-01, ~100B of
 * .ramtext for the C1 arc). Any other split keeps the machinery. */
#if (36 + BAND_SHIFT) == 72 && (184 + BAND_SHIFT_RG2) == 224
#define NAT_ALL_SLAVE 1
#else
#define NAT_ALL_SLAVE 0
#endif
static uint8_t nat_bank;         /* the generation's bank word (cat1) */
static uint8_t nat_mrg;          /* master compose cursor: band */
static uint8_t nat_mphase;       /* 0 clear, 1 sprites, 2 cat1, 3 text */
#ifdef MTASK_WHY
/* MTASK SPLIT (LOOP29 123). PHASECENSUS says mtask is 2.73 of the
 * 2.74-vint generation wall, and on the ship line NAT_ALL_SLAVE=1 makes
 * the master's tail maps-ONLY. So is that 2.73 vints of build_maps WORK,
 * or a short drain spread thin because the master round-robins it against
 * its other tasks? Different fixes. volatile + .bss: the mdspr_why
 * lessons (LTO dead-stores a never-read static; the 0x28Fxx scratch has
 * collisions to #16). PROBE ONLY. */
static volatile uint32_t mt_drain_ticks;   /* FRT actually inside the drain */
static volatile uint32_t mt_drain_chunks;  /* build_maps_chunk calls */
static volatile uint32_t mt_drain_visits;  /* times the drain branch ran */
static volatile uint32_t mt_gate_skips;    /* mtask set but past the cut */
static volatile uint32_t mt_done;          /* drains that completed */
#endif
static uint8_t nat_my;           /* strip cursor within the band */
#define NAT_WALL ((volatile uint32_t *)0x26028F50)
#ifdef PHASE_CENSUS
/* GEN PHASE-SPLIT CENSUS (pipelining arc datum #1, 2026-09-01): where
 * the ~1.4v per-generation wall goes. Scratch 0x28E40-0x28E7F = the
 * audited-free 0x28E38-0x28E7F span (docs/design/BOSSFIGHT.md MAP AUDIT), uncached,
 * boot-zeroed, PROBE-ONLY. Master FRT ticks (12052/vint):
 *   [0] sum echo  = slave chain done (first poll seeing the echo) - launch
 *   [1] sum mtask = master tail done - launch
 *   [2] sum lag   = close - max(echo, mtask)   (poll + vint quantization)
 *   [3] sum ship  = blit done - close
 *   [4] sum flip  = ISR flip - blit done
 *   [5..9] max of each; [10] flips counted; gens = NAT_WALL[1]
 *   [11] sum blit-done offset into the vint (per ship)
 *   PHS (u16 at 0x28E70): [0..3] stamps echo/mtask/close/ship,
 *   [4] echo seen this gen, [5] slave pickup seen (SYNC[13]), [6..7] as
 *   u32 = sum launch->pickup latency (acks == vints, WRAM 0xB0F0)
 *   PHL (0x28E38): [0] sum launch offset into the vint, [1] sum ack
 *   offset (per ack) — the window's critical path in three numbers.
 * The arithmetic lives in ROM (nat_ph_*: plain .text) so .ramtext pays
 * only the stamps and calls — the region guard has 48B. Readers:
 * tools/nat_score.py (dump), tools/state_health.py (savestate). */
#define PH ((volatile uint32_t *)0x26028E40)
/* + histograms at 0x39900-0x3991F (audited-free span): u16 bins of
 * 0.125v from 0.5v to 1.5v (bin 0 = <0.625v, bin 7 = >=1.375v);
 * [0..7] echo latency, [8..15] mtask latency. Quantization shows as a
 * pile-up in the bins straddling 1.0v. */
#define PHH ((volatile uint16_t *)0x26039900)   /* moved from 0x28F60
                                                  * (CAT1_PEND lives there) */
#define PHL ((volatile uint32_t *)0x26028E38)
#define PHS ((volatile uint16_t *)0x26028E70)
#define STR ((volatile uint32_t *)0x260398E0)    /* gen trace, s_main.c */
extern void st_s(unsigned ev);                   /* slave stamps (s_main.c) */
#ifndef ST_ARM_GEN
#define ST_ARM_GEN 600                           /* make PHASEGEN=n */
#endif
static void st_m(unsigned ev);                   /* ROM */
static void nat_ph_close(void);
static void nat_ph_ship(uint16_t tv);
static void nat_ph_flip(void);
#define NAT_CLOSE() do { nat_ph_close(); \
        nat_gen_open = 0; nat_gen_ready = 1; HS_PROMOTE(); } while (0)
#define PHP (*(volatile uint32_t *)0x26028E7C)   /* sum launch->slave
                                                    * pickup (SYNC[13]) */
static int nat_ph_check(uint16_t pw);   /* ROM: pickup/echo stamps;
                                          * returns echo-in */
#define NAT_CLOSE_CHECK() do { \
        if (nat_gen_open && nat_ph_check(pend_wait) && !nat_mtask) \
            NAT_CLOSE(); } while (0)
#else
#define st_s(ev) ((void)0)                       /* slave pass stamps: off */
#define NAT_CLOSE() do { \
        uint16_t wl_ = (uint16_t)(frt() - nat_t0); \
        NAT_WALL[0] += wl_; NAT_WALL[1]++; \
        if (wl_ > NAT_WALL[2]) NAT_WALL[2] = wl_; \
        nat_gen_open = 0; nat_gen_ready = 1; HS_PROMOTE(); } while (0)
/* the three close sites (gap poll, window entry, last-call) share this
 * so the census build changes ONE expression */
#define NAT_CLOSE_CHECK() do { \
        if (nat_gen_open && !nat_mtask && SYNC[1] == pend_wait) \
            NAT_CLOSE(); } while (0)
#endif
static volatile uint8_t nat_capt;    /* ISR captured+declined this vint:
                                      * body fallback skips its second
                                      * flip_span (consume-once) */
#ifdef PACE30
static uint16_t nat_launch_win;  /* PACE30: launch every 2nd window —
                                  * a steady 30 for Mike's eye against
                                  * the irregular 35 (1-or-2-vint
                                  * periods at random = the "frame
                                  * drops" he feels on the player) */
#endif
static uint8_t nat_genbit;       /* generation parity bit (cmd bit 3,
                                  * unread by every consumer): the
                                  * whole-frame cmd is otherwise BIT-
                                  * IDENTICAL every launch, and the
                                  * slave's post-echo wait loop
                                  * (while SYNC[0]==cmd) reads a
                                  * missed 0->repost transition as its
                                  * OWN command still standing — the
                                  * first battery wedged 195 of 340
                                  * generations exactly this way
                                  * (D27=195, 8-window belt each).
                                  * Consecutive cmds must differ. */
#endif

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
/* tile_grp RELOCATED .bss -> fixed block (LOOP 13): the magic-tail
 * alignment gate pushed the MDBGALL flavor's _end past the 0x19000
 * region guard; 0x3E480..0x3EFFF is unclaimed (mdp_s_vol ends 0x3E480,
 * master stack top 0x3F000). Same cached-SDRAM access + purge
 * discipline as the .bss placement — only the address moved. NOT
 * zero-inited by crt0: build_maps rewrites every entry it uses per
 * pass, and boot writes 0xFF below before first compose. */
#define tile_grp ((uint8_t (*)[128])0x0603E480)         /* [2][128] */
/* spr_pair RELOCATED .bss -> the slave stack's FLOOR (2026-09-01, the
 * region guard at 0 bytes). Slave stack: top 0x40000, measured depth
 * ~330B (audit dumps: deepest live 0x3FEBC; sentinel floor 0x3F800).
 * 0x3F800-0x3F87F is 1.6KB below the watermark. Same cached-SDRAM +
 * purge discipline as tile_grp; rewritten per pass, never zero-inited.
 * s_main.c's sentinel paint now starts at 0x3F880 so the watermark
 * stays readable. Region guard headroom bought: 128B. */
#define spr_pair ((uint8_t (*)[64])0x0603F800)          /* [2][64] */
/* LOOP 27 q4 SLVPAIR: the SLAVE composes from this table but never
 * purges its cache (s_main.c has no purge), so a pair the master claims
 * late in the window (LOOP 19 late claim, after the slave last touched
 * the line) reads as 0xFF there and the set draws in the shadow ramp.
 * Census 2026-09-07: every ramp draw was on the slave. The compose
 * reads through the uncached alias — one byte per record. */
#define BMT_PAR    (*(volatile uint32_t *)0x26028FB8)   /* bm_tail's par, 0xFF idle */
#define CLAIM_DONE (*(volatile uint32_t *)0x2603A7B0)   /* late claim done this cycle (SLC scratch, DIAG-only otherwise) */
#ifdef SLV_PAIR_UNCACHED
#define spr_pair_rd ((volatile uint8_t (*)[64])0x2603F800)
#else
#define spr_pair_rd spr_pair
#endif
#ifdef CAT1_MD
/* PEN LINE MEMORY (2026-09-02, Mike's black cells): a set freed by the
 * drift check renders palette 0 — the sprite line, black at pens 13-15 —
 * until its next slot claim re-assigns it; with the lines full that loop
 * runs for seconds. Emit with the set's LAST line instead (stale colour
 * beats black). 2 bits per set. */
#define mdp_s_last ((uint8_t *)0x06028820)  /* 32B: after SYNC[16],
                                              * before blank_tile 0x28840
                                              * (audited free 2026-09-02) */
#define MDP_LAST_GET(s) ((mdp_s_last[(s) >> 2] >> (((s) & 3) * 2)) & 3)
#define MDP_LAST_SET(s, l) (mdp_s_last[(s) >> 2] = (uint8_t)((mdp_s_last[(s) >> 2] & ~(3u << (((s) & 3) * 2))) | ((l) << (((s) & 3) * 2))))
#endif
#ifdef EDGE42
static uint32_t e42_pend[2];     /* per plane, bit row: last edge pair sent
                                  * art-pending -> resend once resolved */
#endif
#ifdef PHASE_CENSUS
static uint8_t sused_prev[64];
#else
#define sused_prev ((uint8_t *)0x060398E0)   /* audited free 96B span; the
                                              * census PHH owns it on
                                              * PHASECENSUS builds */
#endif   /* sets seen in the previous bm_tail pass */
#ifdef TILE_CLASS
#include "tile_classes.h"                /* generated: palpack_tiles.py --emit */
#ifdef PAL_STATIC
#include "pal_scenes.h"                  /* generated: palscene_bake.py —
                                          * per-scene PAL_SH images +
                                          * detect probes (docs/design/PALSTATIC.md) */
/* Scene state + LUT rush. A scene switch collapses the cut's palette
 * trickle: the whole tile+text half of PAL_SH loads from the baked
 * image in ONE window and every tile/text memo generation bumps, so
 * apply_cram repaints the full picture the same window. The game's
 * trailing deltas arrive as no-op confirmations (cram_set's mirror
 * gate). pscene_rush widens the shadow-LUT drain gates for the next
 * gaps so silhouette fallback shrinks with the color smear.
 * Switch census at 0x28F5C (PICKUP_SRC probe scrap, free). */
static uint8_t pscene_cur = 0xFF;  /* last LOADED scene; 0xFF = unknown
                                    * (boot, or the live image walked
                                    * foreign — e.g. the transform
                                    * flash span, which is animated
                                    * and deliberately not loadable).
                                    * Booting unknown means the first
                                    * confirmed normal match LOADS, so
                                    * every run exercises the load +
                                    * heal path once. */
static uint8_t pscene_rush;
/* (HEAL-COMPLETION BELT of 2026-09-02 REMOVED the same day: it was
 * built on a comparison against the RETIRED FB palette copy at
 * 0x85F000; the 68K ships from WRAM 0xFF9000, and against that the
 * shadow was right. The belt re-posted 0xBAD2 forever because the
 * rotor diet legitimately never re-ships some blocks. Law: the game
 * palette is WRAM 0xFF9000 — never compare against the FB copy.) */
static uint8_t pscene_cand;     /* v1.1 confirmed detect: candidate + */
static uint8_t pscene_conf;     /* consecutive-landing streak (>=3
                                 * loads; any miss or scene flap
                                 * resets — a fade transient cannot
                                 * hold a full 8-probe match across 3
                                 * consecutive palette landings) */
static uint8_t pscene_nomatch;  /* consecutive no-match landings; at
                                 * 32 the live image is provably
                                 * foreign (fades resolve in ~10) ->
                                 * cur = unknown, so the RETURN cut
                                 * out of a foreign span gets the
                                 * atomic whole-image load (the
                                 * gravestone-smear class). */
#define PSCENE_SW (*(volatile uint32_t *)0x26028F5C)
#ifdef MD_STATIC
/* STATIC-SCENE (docs/design/STATIC-SCENE.md): per-scene static MD pen
 * tables. At a confirmed scene load the four allocator tables are
 * installed from the bake (tools/mdpen_bake.py) and the table's sets
 * are PINNED: eviction and drift-free skip them, so a set's pens are
 * exact for the whole scene whatever order the walk meets them in.
 * Sets the table does not name still use the dynamic path on the
 * spare pens. Unknown scene -> pins drop, dynamic allocator as before.
 * Tables are plain const in .text (the .palscenes slot is 0x1100 bytes
 * and pal_scenes.h already fills it). Counters in .bss (the 0x28F40
 * scrap the old comment called free is pg_quiet/pg_deep — measured:
 * two of four words read 0 every window): [0] installs, [1] evictions
 * refused by a pin, [2] slot flushes at display-off, [3] frees refused
 * by a pin, [4] installs refused because the live palette was not the
 * scene's (title), [5] installs done from the display-on hold.
 * tools/mdstatic_gate.py finds them by _mds_ctr. */
#include "pal_scenes_md.h"
static uint8_t mds_pin[128];
static uint8_t mds_scene_cur = 0xFF;     /* scene whose MD tables are installed; 0xFF none */
static uint8_t mds_loadgap;              /* vints since a PAL_SH image load during which
                                          * PAL_SH is the IMAGE, not the game (heal ~9 vints):
                                          * the hold-time distance check must not run then
                                          * (it read 0 at the title and installed there) */
static volatile uint32_t mds_ctr[6];     /* [4] installs refused (palette
                                          * beyond MDS_TOL of the anchor),
                                          * [5] spare */
#define MDS mds_ctr
#define MDS_TOL 40                       /* == tools/palscene_bake.py TOL */
#endif
#endif
#ifdef GLOW_ANIM
#include "glow_tab.h"                    /* generated: glow_bake.py —
                                          * the graveyard glow animators
                                          * derived from capture */
/* PALSTATIC v2: the glow is GAME code animating 17 masked words
 * (blocks 4-5) every logic tick; the 68K rotor no longer ships them
 * (GLOW_MASK, -13.7 handler lines measured) and this animator plays
 * the baked rules at the SH-2's 60Hz vint — the ARCADE's rate.
 * Discipline (the differ-visibility law): the animator OWNS the
 * words only while the 68K is silent about them. Any landing that
 * carries block 4/5 (fade storms, heal raw-ships) PAUSES it and the
 * game's shipped truth stands; it resumes only after 60 quiet vints
 * AND a successful re-seed of both phases from live PAL_SH. */
/* animator state in the MDSPR scratch tail (0x28E30+, uncached): the
 * SDRAM .bss region guard sits at 0x19000 and these bytes were the
 * straddle; scratch also makes them ares-dump-readable. */
#define glow_on      (*(volatile uint8_t *)0x26028E30)
#define glow_pause   (*(volatile uint8_t *)0x26028E31)
#define glow_rp      (*(volatile uint8_t *)0x26028E32)
#define glow_ws      (*(volatile uint8_t *)0x26028E33)
#define glow_dw      (*(volatile uint8_t *)0x26028E34)
#define glow_seen_rp (*(volatile uint8_t *)0x26028E35)
#define glow_seen_ws (*(volatile uint8_t *)0x26028E36)
#define glow_streak  (*(volatile uint8_t *)0x26028E37)
static uint8_t glow_post;       /* pending COMM8 mask grant: 3 = mask
                                 * OFF (animator yielded — 68K ships
                                 * glow truth again), 4 = mask ON
                                 * (animator seeded and running). The
                                 * 68K masks ONLY while granted; a
                                 * failed re-seed therefore leaves
                                 * full-fidelity shipping in place —
                                 * the frozen-glow deadlock (first
                                 * PALGLOW build: title-era 0x30FF
                                 * state, unrecognizable, mask stuck
                                 * on) is structurally impossible. */

/* reseed continuation state: last observed phases + how many
 * consecutive vints they FOLLOWED the ambient program (scratch
 * defines above) */

/* COLD (ROM-resident on purpose — runs only on quiet vints after a
 * pause, never in the steady tick). CONTINUATION-CONFIRMED reseed
 * (the 3-landings law again): recognizing one frame is not enough —
 * the transform flash PASSES THROUGH exact ambient states (its
 * ambient programs keep running underneath), and a one-shot reseed
 * re-granted mid-flash, swallowed the next pulse, got revoked, and
 * thrashed (Mike's flash artifacts). Grant only after 3 consecutive
 * vints in which live PAL_SH followed the program: ramp advanced by
 * a legal step (0 stutter / -2 tick / -4 catch-up burst; census
 * gaps), wave state moved 0..2. */
__attribute__((noinline))
static unsigned glow_reseed(void)
{
    unsigned p7, s6, j;
    for (p7 = 0; p7 < 7; p7++) {
        for (j = 0; j < 7; j++)
            if (PAL_SH[GLOW_RAMP0 + j] != (uint16_t)
                (0x4900 + (((j + p7) % 7) << 8)))
                break;
        if (j == 7)
            break;
    }
    for (s6 = 0; s6 < GLOW_WAVE_N; s6++) {
        for (j = 0; j < 5; j++)
            if (PAL_SH[0xA1 + j] != glow_wave[s6][j])
                break;
        if (j == 5)
            break;
    }
    if (p7 == 7 || s6 == GLOW_WAVE_N) {
        glow_streak = 0;                 /* unrecognizable: start over */
        return 0;
    }
    if (glow_streak) {
        unsigned rst = (glow_seen_rp + 7u - p7) % 7;  /* 0/2/4 legal */
        unsigned wst = (unsigned)
            (s6 + GLOW_WAVE_N - glow_seen_ws) % GLOW_WAVE_N;
        if ((rst != 0 && rst != 2 && rst != 4) || wst > 2)
            glow_streak = 0;
    }
    glow_seen_rp = (uint8_t)p7;
    glow_seen_ws = (uint8_t)s6;
    if (++glow_streak < 3)
        return 0;
    glow_streak = 0;
    glow_rp = (uint8_t)p7;
    glow_ws = (uint8_t)s6;
    glow_dw = glow_dwell[s6];
    return 1;
}
#endif
#endif
/* scratch 0x28C90, 16B (region-guard diet; boot-zeroed) */
#define text_grp ((uint8_t (*)[8])0x26028C90)  /* text colors: on-demand */
/* Sticky ownership (persistent across cycles — see build_maps): which
 * color owns each CRAM group / sprite pair, and how long since it was
 * last seen on screen. */
#define grp_key  ((uint8_t *)0x06028360)               /* 32B, BM tail */
#define grp_kind ((uint8_t *)0x06028380)               /* 32B */
#define grp_age  ((uint8_t *)0x060283A0)               /* 32B */
#define pr_key   ((uint8_t *)0x060283C0)               /* 16B */
#define pr_age   ((uint8_t *)0x060283D0)               /* 16B */
/* How many cycles a departed sprite set keeps its CRAM pair before it
 * is released (LOOP19 pair-hold sweep; historical value 90). */
#ifndef PR_HOLD_TICKS
#define PR_HOLD_TICKS 90
#endif
/* PER-PIXEL SPRITE/TILE PRIORITY (segas16b_v.cpp screen_update):
 * tile pixels carry a level — BG cat0=1, BG cat1=2, FG cat0=2,
 * FG cat1=4, text cat0=4, text cat1=8 — and a sprite pixel draws iff
 * (1 << its 2-bit priority field) > level. We recover the level from
 * the composed pixel VALUE via a 256-entry LUT: the allocator knows
 * which CRAM group serves which color, and the prescan records each
 * color's level. Exact except when one color is used at two levels in
 * the same frame (counted in DIAG[15]); sprite-pair groups stay level
 * 0 so sprite-over-sprite remains hardware list-order overwrite. */
/* pri_lut RELOCATED .bss -> fixed block (LOOP 13, same move as
 * tile_grp): the co-owner drift check pushed MDBGALL past the region
 * guard. 0x3E580..0x3E781 is free (tile_grp ends 0x3E580, master stack
 * top 0x3F000). Fully rewritten by every build_maps pass; boot zeroes
 * it below (no crt0-zero dependence). */
#define pri_lut ((uint8_t (*)[256])0x0603E580)         /* [2][256] */
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
#ifdef PAL_VBLANK
/* PALETTE APPLY IN VBLANK (2026-09-08, LOOP27 18). On real 32X silicon a
 * CRAM write is only accepted while PEN is asserted — vblank, hblank, or
 * display-off — and is SILENTLY DROPPED during active scan
 * (srcref/S32X_MiSTer rtl/32X/VDP.sv:170 gates palette access on PEN;
 * :403/:405 assert PEN on VBLK / H_CNT 0x159..0x016 only). Our paints run
 * inside the render window, lines ~20-190 = active scan, so on the MiSTer
 * nearly every one vanished: the whole 32X layer drew black on black,
 * which is the entire "black screen" arc (entry 18). ares accepts them
 * all, which is why fifteen probe rounds never saw it.
 *
 * The fix is the idiom srcref/32X240pTestSuite uses and ships with:
 * pri_vbi_handler (src/hw_32x.c:99) writes the whole palette inside the
 * master's V-blank handler. Here: cram_set keeps updating the mirror and
 * marks the entry dirty, but does NOT touch CRAM; cram_flush_vbl() drains
 * every dirty entry at the flip, which the edge guard already pins inside
 * vblank. 256 bits of dirt, one u32 per 32 entries. */
#ifndef PAL_PEN
static uint32_t cram_dirt[8];
#endif
static inline void cram_flush_vbl(void)
{
    volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM;
    for (int w = 0; w < 8; w++) {
        uint32_t d = cram_dirt[w];
        if (!d) continue;
        cram_dirt[w] = 0;
        do {
            int b = __builtin_ctz(d);
            d &= d - 1;
            int i = w * 32 + b;
            cram[i] = (uint16_t)(cram_mirror[i] & 0x7FFF);
            DIAG[19]++;              /* CRAM writes actually performed */
        } while (d);
    }
}
#endif
#define shadow_lut  ((uint8_t *)0x06028B00)    /* 256B */
static volatile uint8_t shadow_dirty = 1;
#define shadow_cur (*(volatile uint8_t *)0x26028CA0)  /* rebuild cursor;
                                             * scratch (guard diet) */

#ifdef FLICK_FUSE
/* FLICKER FUSION (LOOP 21). The arcade's Zeus/orb translucency is
 * TEMPORAL: the record's presence in the sprite list is duty-modulated
 * (fade-in 1/8 -> 1/4 -> 1/3, hold 2-in-3, fade-out back — measured,
 * tools/zeus_list_probe.lua) and OFF frames remove the record outright
 * (list compacted; there is no hide bit to sample). Sampled below 60Hz
 * that dither aliases: 20Hz strobed it, 30Hz phase-locks the even-period
 * phases. Synthesis: track ZOOMED records across windows; one that
 * toggles presence draws EVERY window from its held copy through an
 * ordered dither whose coverage is the observed on-ratio — the alpha the
 * arcade was expressing, minus the flicker. The on-ratio under-samples
 * true duty during even-period phases (we see every 2nd frame at 30Hz);
 * a per-vint presence signature is the refinement if fades read wrong.
 * State is plain .bss (region guard has the headroom), NOT a fixed
 * block: 0x3A680 is ROWLIVE's under DIRTYROW, and 0x3E780 is inside
 * the master's live stack — both were BUILT AND CAUGHT (trackers
 * wiped / churned every cycle), slot collisions seven and eight for
 * this file. flick_lvl is read by both CPUs' compose through the
 * uncached alias (master rewrites it every k1); the per-slot value is
 * the coverage level (0 = draw normally, r = draw r/8 of pixels via
 * bay[y&3][x&3] < 2r). */
#if !defined(FB_SPR_READ) && !defined(K2_FREE)
#error FLICK_FUSE requires FBSPR=1 (tracks the in-place k1 snapshot) \
       or K2_FREE (tracks the landed-packet snapshot)
#endif
static uint8_t flick_lvl_arr[64];
#define flick_lvl ((volatile uint8_t *)((unsigned)flick_lvl_arr | 0x20000000u))
struct flick_trk {
    uint16_t rec[6];                     /* held d0..d5 */
    uint8_t  hist;                       /* presence, bit0 = this window */
    uint8_t  live;
    uint8_t  stick;                      /* was classified flicker-class:
                                          * re-appearance after a blackout
                                          * (Zeus's lightning gaps, ~8
                                          * windows) resumes the stipple
                                          * immediately instead of paying
                                          * 2-3 windows of raw blink for
                                          * re-classification */
    uint8_t  age;                        /* windows since last seen */
    uint8_t  lvl;                        /* drawn coverage, slewed at most
                                          * 1 step/window toward the
                                          * on-ratio — raw r jumps 2-3
                                          * between windows and reads as
                                          * brightness stutter */
};
static struct flick_trk flick_trk_tab[4];   /* master-only */
static uint8_t flick_bay[16];               /* 4x4 Bayer, boot-built,
                                             * then read-only (RAM, not
                                             * rodata: compose must not
                                             * add a cart read) */
#endif
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
#ifdef R60
/* 40 was calibrated for 30Hz windows (LOOP19 trap note: check what a
 * constant was calibrated against). At 60Hz a full 40-tile batch is
 * the measured 92-line consume spike (137 consumes >28 lines, last
 * slow typ|cnt=0x0028): the DMA overruns vblank into the 10x-slower
 * active-display rate and the post slips past the flip guard — the
 * load-in tear/purple-band cluster in Mike's second pass. 12 tiles
 * always fits the vint-top window; the backlog rides more frames
 * (invisible behind the transitions that generate it). */
#define MD_BATCH     12
#else
#define MD_BATCH     40
#endif
#define MD_TILE_MAX  1120

/* Cache slot for a code, or -1 if it is not resident. Same probe as
 * tile_pixels but without queueing a miss -- the name table must not
 * generate cache traffic just by being built. */
#define MD_BLANK_SLOT 1023

/* LOOP 18: THE MASTER/SLAVE COMPOSE SPLIT IS EVEN AND THE LOAD IS NOT.
 * Compose is 112 rows each (slave 0-36/72-108/144-184, master the
 * complement) and the blit is 56 rows each — but on top of that the
 * MASTER alone carries the flip, the page drain and restore, CRAM,
 * build_maps, the shadow LUT, the sprite snapshot and the band queue
 * itself. (STALE, corrected 2026-09-10: the slave finishes early and
 * idles ~15,500 polls/cycle was true of an older pipeline. MEASURED on
 * the current line, LOOP29 118: the slave idle meter at 0x28FA8 does
 * NOT MOVE during gameplay on either the accepted or the double-
 * buffered build -- 0.0 idle polls per vint. The slave is SATURATED
 * and has no spare capacity.) Its
 * echo is what PUSHES the next band, while the master's progress is
 * what DRAINS one. Pushes therefore outrun drains and the depth-4 queue
 * sheds ~1 band per cycle, every cycle.
 * The dropped band is the MASTER's rows, which is why the tear sits at
 * rows 72 and 144 — exactly where master rows end and slave rows begin.
 * And it is why a uniform speedup cannot fix it: BLITSKIP, DIRTYROW and
 * SPRBAKE together moved the handler mean 91.3 -> 86.5 and left
 * drops/cycle pinned at 0.95-0.97. An asymmetry needs rebalancing, not
 * throughput.
 * BAND_SHIFT moves N rows of every band from the master to the slave.
 * `make BANDSHIFT=12`. 0 is today's even split. */
#ifndef BAND_SHIFT
#define BAND_SHIFT 0
#endif
/* BLIT_SHIFT: same idea for the BLIT split (R60 only) — rows moved
 * from the master's window blit to the slave's. See the pass-8
 * profile at the BLIT_HALF call sites. `make BLITSHIFT=N`. */
#ifndef BLIT_SHIFT
#define BLIT_SHIFT 0
#endif
/* RG2 FULL SHIFT (pass 10, the purple bottom band): the band chain is
 * R0->R1->R2 every frame, so rg2 is ALWAYS last into the ring and the
 * perpetual deferral victim — the same bottom rows, every time, which
 * is why the purple band and the bottom-HUD dropout live there. At
 * BAND_SHIFT>=32 the master's rg2 compose slice is only 8 rows: hand
 * them to the slave outright (rg2 shift 40 = the full 144-224 band)
 * so the master's rg2 band is compose-free, completes fast, and stops
 * squatting the ring. */
/* RG2 full shift REVERTED same night: the purple band survived it
 * in Mike's live pass AND appears in his attract captures, while NO
 * headless run (input scripts, full attract cycle, 8000+ frames
 * scanned) reproduces it at all — the band is live-environment-only
 * and is NOT the deferred-rg2-rows mechanism. The shift's real
 * ledger: defer 1.16->1.09 for tears 105->138. Bad trade. Kept at
 * parity with BAND_SHIFT until the purple is caught in a savestate
 * (ask Mike: save state WHILE the band is on screen). */
#ifndef BAND_SHIFT_RG2
#define BAND_SHIFT_RG2 BAND_SHIFT
#endif
#define SBUF_W 336
/* 8 (top margin) + 224 (screen) + 0. WAS 240 — an 8-row BOTTOM margin
 * that a full scripted play never touched a byte of. sbuf IS the
 * 0x19000 region (80,640 of the 102,400 bytes below the guard, with
 * 21.6KB of RAMCODE under it), so those 8 rows were 2,688 bytes the
 * region guard could not have and job 2 needed.
 * MEASURED, NOT ASSUMED (`make SBUFCANARY=1` + tools/sbuf_canary.lua):
 * every one of the 16 margin rows — 0..7 AND 232..239 — came back
 * byte-intact after attract plus gameplay in all four directions, while
 * all 16 margin COLUMNS came back fully written. The horizontal margin
 * is load-bearing (fine scroll draws from column 8-xf and a 41-tile row
 * reaches 336); the vertical margin is not, because every writer
 * addresses sbuf as (8 + y) with y in [0,224).
 * The TOP margin stays: taking it would mean re-basing that idiom at
 * every call site, and the row it protects is the one an off-by-one
 * would write BEFORE the array. Bottom-only is a one-line change with
 * no call-site churn. */
#define SBUF_H 232
static uint8_t sbuf[SBUF_W * SBUF_H] __attribute__((aligned(4)));

/* ---- REBUILD STAGE 1 (DIRECT_FB): the dst-row abstraction. ----
 * FBBENCH measured FB writes == SDRAM writes (0.98-0.99x), so the
 * staging+blit copy buys nothing; compose writes the FB BACK BANK
 * directly through the cached window, exactly the dst form blit_half
 * used (0x04000200 + y*320 — write-through, stores ride the 4-deep
 * write buffer).
 * WHICH BANK: no plumbing needed. blit_half's own dst never encoded a
 * bank either — 0x04000000 maps the DRAW bank by hardware on both
 * CPUs, and fb_draw_par only labels FBCLEAR halves. The program's one
 * FS write is the k2 flip, and the compose chain for a frame launches
 * AFTER that flip and (nominally) closes before the next one, so the
 * whole interval writes the same bank this cycle's blits would have
 * hit. A chain still open when the next flip fires leaks its tail
 * into the other bank — the pre-existing desync-band class, not a new
 * failure.
 * GEOMETRY: DROW takes the SBUF ROW INDEX (8 + screen y) every writer
 * already computes; sbuf column 8 == screen x 0, so the +8 border
 * offset drops out. FB rows are exactly 320 bytes with NO margins —
 * writers that leaned on sbuf's 8-byte side borders are clipped at
 * their call sites (the opaque merge stops at column 39; the FG edge
 * tiles bound-check). DROW_U is the uncached alias for dst READS
 * (shadow darkening, pp<=1 gates): a cached FB read could answer from
 * a line filled before the last flip — the OTHER bank's pixels.
 * THE ZERO-BYTE LAW: the 32X FB discards BYTE writes of 0x00. Every
 * compose writer stores nonzero pens (base+pen, pen>=1), but the
 * CLEARS must be long fills — a byte clear would be a silent no-op. */
#ifdef DIRECT_FB
#if !defined(WIN_TWO) || !defined(R60)
#error "DIRECT_FB stage 1 is only plumbed for the WIN_TWO+R60 cycle"
#endif
#define DST_STRIDE 320
#define DROW(sr)   ((uint8_t *)(0x04000200u + ((unsigned)(sr) - 8) * 320u))
#define DROW_U(sr) ((uint8_t *)(0x24000200u + ((unsigned)(sr) - 8) * 320u))
#else
#define DST_STRIDE SBUF_W
#define DROW(sr)   (sbuf + (sr) * SBUF_W + 8)
#define DROW_U(sr) (sbuf + (sr) * SBUF_W + 8)
#endif

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

#if defined(FBDMA_PROBE) || defined(FBROWS_PROBE)
static volatile uint32_t fbp_st[8];
#endif
#ifdef BLIT_HASH
/* LEVER B (SESSION 7): PER-BANK CONTENT SKIP, PER GROUP. The ship
 * blit's only content-level skip is "group all-zero AND already zero in
 * this bank"; an opaque group identical to what the bank holds is
 * re-written every frame (ROWSHIP=1 census over 2600 gameplay vints:
 * 48% of shipped ROWS carried content identical to the bank's last
 * ship — the HUD rows 8-31 ~100%, the ground band ~75%; per group the
 * identical fraction is higher, a moving sprite dirties 2-3 of a row's
 * 10 groups). Each non-zero group is hashed (rotl1-xor over its 8
 * longs, which the loop has already loaded: 14 instructions against
 * ~8 x 0.6us of FB write stall) and compared with this BANK's last
 * ship of the group. A row-level hash was tried first and measured
 * SLOWER (50.4 -> 47.4%): rotl-by-5 is five SH-2 rotates, ~480
 * cycles/row, more than the writes it saved.
 * Table = [bank][row][group] u32, 17,920B of .bss (task 0 bought the
 * room). Cached view is safe: every word is read and written by ONE
 * CPU (rows partition slave/master and a row's 10 words are its own).
 * 0 = no valid content (boot, zeroed group, mask relearn); a real hash
 * of 0 is stored as 1. DIAG[10] counts skipped groups. */
static uint32_t blit_grouphash[2 * 224 * 10];
#endif
#ifdef WAIT_PROBE
/* SESSION 7: where the master's ship window goes. RG_COUNT[9] master
 * half ticks, [10] ticks waiting for the slave half after the master's
 * own half, [11] ticks from the SYNC[4] post to the master half start,
 * [12] ships. NEVER SHIP. */
#ifndef ROW_GEN
#define RG_COUNT ((volatile uint32_t *)0x26038E00)
#endif
static uint16_t rg_tpost;
#endif
#ifdef ROWSHIP_PROBE
/* SESSION 7: rows actually shipped by blit_half, UNCACHED so both CPUs'
 * counts survive (fbp_st is a cached .bss word — lost updates). Split
 * by half so the two blitters never RMW one word: [0] rows y<112,
 * [1] rows y>=112, [2] blit_half calls. 0x38F00-0x39000 is the
 * session-7 probe scrap at the top of the .ramtext slot (mars.ld
 * asserts .ramtext ends below it). NEVER SHIP (uncached RMW per row). */
#define RSHIP   ((volatile uint32_t *)0x26038F00)
/* per-row census (SESSION 7 probe scrap 0x38000-0x39000): [bank][row]
 * hash of the last content shipped, per-row ship count, per-row count
 * of ships whose content EQUALLED this bank's last ship (= what a
 * per-bank content-skip would have saved). */
#define RSHASH  ((volatile uint32_t *)0x26038000)   /* 2 x 224 u32, ends 0x38700 */
#define RSCNT   ((volatile uint16_t *)0x26038800)   /* 224 u16 */
#define RSIDENT ((volatile uint16_t *)0x26038A00)   /* 224 u16 */
#endif
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
#define CACHE_SET(code) (((code) ^ ((code) >> 7)) & (CSETS - 1))
/* MD VRAM slot map: the ORIGINAL 128-set fold (task 0 halved the render
 * cache only). */
#define MD_SET(code)    (((code) ^ ((code) >> 7)) & (NSETS - 1))

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
#ifdef TAGKEEP
/* LOOP29 155: a colour set's TILE PATTERN depends only on its (line,
 * pixel->pen map) -- not on the pen COLOURS. mdp_free_set wipes every
 * md_tag entry carrying the set because a re-assign may land it
 * somewhere else, but 154 could not stop the drift free itself (pens
 * are too scarce to avoid sharing: 363 of 366 claims have no free pen),
 * so the question becomes whether the wipe is NEEDED. Defer it: keep
 * the old line and map, and at the re-assign wipe only if they moved.
 * [22] deferred wipes resolved SAME (wipe skipped), [23] MOVED. */
static uint8_t mdp_pend_tag[128];
static uint8_t mdp_pend_line[128];
static uint8_t mdp_pend_map[128 * 8];
static uint8_t mdp_pend_used[128];
static void mdp_wipe_set_tags(unsigned s)
{
    for (int i = 0; i < NSETS * NWAYS; i++)
        if (md_tag[i] != 0xFFFFFFFFu && ((md_tag[i] >> 16) & 0x7F) == s) {
            md_tag[i] = 0xFFFFFFFFu;
            MDA(14);
#ifdef MD_ALLOC_WHY
            /* LOOP29 156: the only number that says whether a wipe costs
             * VISIBLE tiles -- is the slot named by the name table the
             * player is looking at right now? Counters said the churning
             * sets hold no slots at three sampled frames, which is true
             * and not the question: residency is time-varying and the
             * sample was never at a free. Ask at the free. */
            for (int q = 0; q < 2240; q++)
                if ((md_dbg_nt[q] & 0x7FF) == (unsigned)i) {
                    MDA(24);
                    mdalloc_onscr[s & 127]++;
                    break;
                }
#endif
        }
}
#endif
static void mdp_free_set(unsigned s)
{
    if (!mdp_s_line[s])
        return;
#ifdef MD_STATIC
    if (mds_pin[s]) {                        /* table set: never freed
                                              * inside its scene */
        MDS[3]++;
        MDA(15);
        return;
    }
#endif
#ifdef MD_ALLOC_WHY
    if (s == MDA_WATCH && !mdalloc_id[0]) {
        mdalloc_id[0] = 0x100u | s;
        mdalloc_id[1] = mdp_s_line[s];
        mdalloc_id[5] = mdp_s_used[s];
        for (int p = 0; p < 8; p++) {
            mdalloc_id[8 + p] = mdp_s_map[s * 8 + p];
            mdalloc_id[2] = mdp_pen_own[(((unsigned)mdp_s_line[s] - 1) * 16
                                         + mdp_s_map[s * 8 + p]) * 2];
        }
        for (int i = 0, k = 0; i < NSETS * NWAYS && k < 8; i++)
            if (md_tag[i] != 0xFFFFFFFFu
                && ((md_tag[i] >> 16) & 0x7F) == s)
                mdalloc_id[16 + k++] = md_tag[i];
    }
    if (s == MDA_WATCH) mdalloc_id[4]++;
    mdalloc_relo[s & 127]++;
#ifdef MD_STATIC
    mdalloc_pin[s & 127] = mds_pin[s & 127];
#endif
#endif
    unsigned l = (unsigned)(mdp_s_line[s] - 1);
#ifdef TAGKEEP
    uint8_t old_line = mdp_s_line[s], old_used = mdp_s_used[s], old_map[8];
    for (int p = 0; p < 8; p++)
        old_map[p] = mdp_s_map[s * 8 + p];
#endif
#ifndef DRIFT_TOL
#define DRIFT_TOL 18                         /* LOOP29 160: was a bare 18
                                              * at both drift sites; swept
                                              * as a parameter because the
                                              * free it triggers destroys
                                              * on-screen tiles (156). */
#endif
#ifdef PEN_HOLD
    /* LOOP29 158: HOLD THE PENS. 157's land-where-you-were only rescued
     * 4 frees of 16 because this loop releases the set's pens, another
     * set takes them, and the re-assign cannot go home. Set 33 -- 65% of
     * all on-screen tile destruction, eight consecutive tile codes of a
     * large gradient mass with only 4 distinct pens -- is a FADING set,
     * so it is freed and re-assigned over and over while its pens are up
     * for grabs. Keeping the refcount reserves them across the gap. The
     * cost is pens held by a set that is not currently assigned; a set
     * that never comes back leaks them, which is why this is a probe
     * until the release timeout exists. */
    (void)l;
#else
    for (int p = 0; p < 8; p++) {
        unsigned pen;
        if (!(mdp_s_used[s] & (1u << p)))
            continue;                        /* never claimed a pen */
        pen = mdp_s_map[s * 8 + p];
        if (mdp_pen_rc[l * 16 + pen] && !--mdp_pen_rc[l * 16 + pen])
            mdp_line_c[l * 16 + pen] = 0xFFFF;
    }
#endif
    mdp_s_line[s] = 0;
    mdp_s_used[s] = 0;
    MDA(13);
#ifdef TAGKEEP
    mdp_pend_tag[s]  = 1;
    mdp_pend_line[s] = old_line;
    mdp_pend_used[s] = old_used;
    for (int p = 0; p < 8; p++)
        mdp_pend_map[s * 8 + p] = old_map[p];
#else
    for (int i = 0; i < NSETS * NWAYS; i++)
        if (md_tag[i] != 0xFFFFFFFFu && ((md_tag[i] >> 16) & 0x7F) == s) {
            md_tag[i] = 0xFFFFFFFFu;         /* both planes' variants */
            MDA(14);
        }
#endif
    DIAG[37]++;                              /* set frees/invalidations */
}

/* Claim a pen in line l for quantised colour q; returns the pen and
 * bumps its refcount. Exact match first, then a free pen, then the
 * nearest occupied pen (counted — should be rare once pens are only
 * claimed for used pixels). */
#ifdef DRIFT_VOL
/* LOOP29 154: a pen whose OWNER's colour animates. 153 measured that
 * every md_tag relocation in a 4000-frame run is the co-owner drift
 * free, and that colour set 33 alone takes 44 of them: it is freed,
 * re-assigned, SHARES THE SAME ANIMATING PEN AGAIN, and drifts again,
 * ~45 resident tiles dying each round. mdp_s_vol was meant to stop that
 * and cannot -- it needs a free pen and 363 of 366 burned claims have
 * none (2-3 MD CRAM lines, 128 colour sets). Marking the PEN instead
 * needs no free pen: sharing simply skips it and the nearest-colour
 * fallback picks something else. Cleared with the scene tables. */
static uint8_t mdp_pen_vol[MDP_LINES * 16];
#endif
static unsigned mdp_claim_pen(unsigned l, uint16_t q, unsigned s, unsigned p)
{
    unsigned pen16 = 0, freepen = 0;
    for (unsigned pen = 1; pen < 16; pen++) {
        if (mdp_line_c[l * 16 + pen] == q
#ifdef DRIFT_VOL
            && !mdp_pen_vol[l * 16 + pen]
#endif
           ) { pen16 = pen; break; }
        if (!freepen && mdp_line_c[l * 16 + pen] == 0xFFFF)
            freepen = pen;
    }
#ifdef MD_ALLOC_WHY
    if (mdp_s_vol[s] >= 2) {
        MDA(19);                             /* burned set claiming a pen */
        if (!freepen) MDA(20);               /* ... with no exclusive pen left */
        if (pen16) MDA(21);                  /* ... and a shareable match */
    }
#endif
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
#ifdef DRIFT_VOL
            if (mdp_pen_vol[l * 16 + pen]) d += 64;   /* last resort */
#endif
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
static int mdp_near(unsigned s, int p, int p2);

static void mdp_extend_set(unsigned s, uint8_t addmask)
{
    unsigned l = (unsigned)(mdp_s_line[s] - 1);
    for (int p = 0; p < 8; p++) {
        uint16_t q;
        if (!(addmask & (1u << p)))
            continue;
        q = mdp_quant(PAL_SH[s * 8 + p]);
        {
            int p2;
            for (p2 = 0; p2 < 8; p2++)
                if (p2 != p && (mdp_s_used[s] & (1u << p2)) && mdp_near(s, p, p2))
                    break;
            if (p2 < 8) {                    /* NEAR MERGE: one pen */
                mdp_s_map[s * 8 + p] = mdp_s_map[s * 8 + p2];
                mdp_s_qc[s * 8 + p]  = mdp_s_qc[s * 8 + p2];
                mdp_pen_rc[l * 16 + mdp_s_map[s * 8 + p2]]++;
                mdp_s_used[s] |= (uint8_t)(1u << p);
                continue;
            }
        }
#ifdef TAGKEEP
        /* LOOP29 157: land where you were, on the extend path too. */
        if (mdp_pend_tag[s] && mdp_pend_line[s] == mdp_s_line[s]
            && (mdp_pend_used[s] & (1u << p))) {
            unsigned op = mdp_pend_map[s * 8 + p];
            if (op && op < 16
                && (mdp_line_c[l * 16 + op] == 0xFFFF
                    || mdp_line_c[l * 16 + op] == q)) {
                if (mdp_line_c[l * 16 + op] == 0xFFFF) {
                    mdp_line_c[l * 16 + op] = q;
                    mdp_pen_own[(l * 16 + op) * 2]     = (uint8_t)s;
                    mdp_pen_own[(l * 16 + op) * 2 + 1] = (uint8_t)p;
                }
                mdp_pen_rc[l * 16 + op]++;
                mdp_s_map[s * 8 + p] = (uint8_t)op;
                mdp_s_qc[s * 8 + p]  = q;
                mdp_s_used[s] |= (uint8_t)(1u << p);
                MDA(27);                     /* extend kept the old pen */
                continue;
            }
            /* the old pen is gone: the pattern moves, so the tags this
             * set still holds are now wrong -- wipe them here, late, and
             * stop pretending. */
            mdp_pend_tag[s] = 0;
            mdp_wipe_set_tags(s);
            MDA(28);
        }
#endif
        mdp_s_map[s * 8 + p] = (uint8_t)mdp_claim_pen(l, q, s, (unsigned)p);
        mdp_s_qc[s * 8 + p]  = q;
    }
    mdp_s_used[s] |= addmask;
}

/* NEAR MERGE (2026-09-03, Mike's "sky tiles wrong palette"): S16 art
 * dithers gradients with pairs of colours 1-2 steps apart in 5 bits —
 * invisible at 15 bits, but 9-bit quantisation can land the pair one
 * MD level apart and the checker shows (level-1 sky band: (14,18,25) vs
 * (12,17,24) -> (4,5,6) vs (3,4,6)). Two pixels of a set whose raw
 * channels are all within 2/31 share ONE pen: the band goes flat like
 * the arcade's, and the set spends fewer pens. */
static int mdp_near(unsigned s, int p, int p2)
{
#ifndef NEAR_MERGE
    /* OFF by default (2026-09-04): merging at assignment time is wrong
     * for any scene entered through a fade — every colour of a set is
     * near black at the moment of assignment, the pixels merge, and
     * they stay merged when the fade completes (Mike's transformation
     * chevrons went flat, the level load-in never reached its palette).
     * A fade-safe version needs an unmerge on drift; until then the sky
     * band keeps its checker. Build with NEARMERGE=1 to test. */
    (void)s; (void)p; (void)p2; return 0;
#endif
    unsigned a = PAL_SH[s * 8 + p], b = PAL_SH[s * 8 + p2];
    int ar = (int)(((a & 0xF) << 1) | ((a >> 12) & 1));
    int ag = (int)((((a >> 4) & 0xF) << 1) | ((a >> 13) & 1));
    int ab = (int)((((a >> 8) & 0xF) << 1) | ((a >> 14) & 1));
    int br = (int)(((b & 0xF) << 1) | ((b >> 12) & 1));
    int bg = (int)((((b >> 4) & 0xF) << 1) | ((b >> 13) & 1));
    int bb = (int)((((b >> 8) & 0xF) << 1) | ((b >> 14) & 1));
    int dr = ar - br, dg = ag - bg, db = ab - bb;
    if (dr < 0) dr = -dr;
    if (dg < 0) dg = -dg;
    if (db < 0) db = -db;
    return dr <= 2 && dg <= 2 && db <= 2;
}

static int mdp_assign_set(unsigned s, uint8_t stamp, uint8_t mask, int soft);

/* A tile is being claimed for MD residency: fold its pixel values into
 * the set's usage mask and assign/extend the pen mapping to cover
 * exactly the used pixels. FG claims drop bit 0 — the shipper forces
 * FG pixel 0 to pen 0 (transparent), so its (often garbage) colour
 * must not spend a pen. */
#ifdef CSET_CENSUS
/* COLOUR-SET PRESSURE (LOOP28 99). The Mega Drive plane has MDP_LINES*16
 * = 48 pens and a tile may draw from ONE 16-pen line, while a System 16
 * background tile is 3bpp — at most 8 pens out of its colour set. So two
 * sets share a line comfortably and three lines hold six sets. The
 * question the allocator exists to answer is how many DISTINCT sets are
 * live at once; if it is <= 6 there is nothing to evict and the LRU is
 * dead weight. This counts distinct csets per vint: sum, max, samples. */
static uint32_t cs_seen[4];
static uint8_t cs_disp_on = 1;           /* mirror of the game's display
                                          * enable, set where r60_disp_on
                                          * is (declared later than this) */
static void cs_note(unsigned cset)
{
    cs_seen[(cset >> 5) & 3] |= 1u << (cset & 31);
}
static void cs_flush(void)
{
    unsigned n = 0;
    for (int i = 0; i < 4; i++) { n += (unsigned)__builtin_popcount(cs_seen[i]); cs_seen[i] = 0; }
    CEN[39] += n;
    if (n > CEN[40]) CEN[40] = n;
    CEN[41]++;
    /* histogram: does the 6-set capacity hold almost always, or not?
     * [42] <=6 (fits, nothing to evict)  [43] 7-12  [44] 13-24  [45] 25+
     * [46] counts the <=6 vints where the game had the display BLANKED,
     * which is a load and shows nothing anyway. */
    if (n <= 6) CEN[42]++;
    else if (n <= 12) CEN[43]++;
    else if (n <= 24) CEN[44]++;
    else CEN[45]++;
    if (n > 6 && !cs_disp_on) CEN[46]++;    /* over capacity WHILE BLANKED */
}
#endif
static void mdp_note_tile(unsigned cset, unsigned code, int isfg,
                          uint8_t stamp, int soft)
{
    const uint8_t *tp = altbeast_tiles + code * 64;
    uint8_t mask = 0;
    for (int i = 0; i < 64; i++)
        mask |= (uint8_t)(1u << tp[i]);
    if (isfg)
        mask &= 0xFE;
    (void)soft;                              /* soft LINE claims measured
                                              * -12% ships in the soak
                                              * (every pending row is an
                                              * FB row); slots are the
                                              * scarce resource, see the
                                              * claim loop */
    if (!mdp_s_line[cset])
        mdp_assign_set(cset, stamp, mask, 0);
    else if (mask & (uint8_t)~mdp_s_used[cset])
        mdp_extend_set(cset, mask & (uint8_t)~mdp_s_used[cset]);
}

/* Assign a set to a line, claiming pens ONLY for `mask` pixels: greedy
 * best-fit by fewest new pens, evicting LRU sets from the best line if
 * it is full, nearest-colour fallback as last resort. Always succeeds. */
static int mdp_assign_set(unsigned s, uint8_t stamp, uint8_t mask, int soft)
{
    uint16_t qc[8];
    int bestl = 0, bestneed = 99, bestfit = 0;
    for (int p = 0; p < 8; p++)
        qc[p] = mdp_quant(PAL_SH[s * 8 + p]);
#ifdef TAGKEEP
    /* LOOP29 157: LAND WHERE YOU WERE. 155 measured that a drift-freed
     * set NEVER comes back to the same (line, pen map) -- 0 of 59 -- so
     * every free costs a full tag wipe, and 156 measured that 95% of
     * those tags are on screen at that moment. The re-assign moves only
     * because it re-packs greedily from scratch; the set's OLD pens are
     * usually still sitting there holding the right colours (the free
     * released them, so they are free or unchanged). Try the old
     * placement first and the pattern bytes are identical, so the tags
     * -- and the picture -- survive. [25] old placement reused,
     * [26] old placement rejected. */
    /* SUBSET, not equality: mdp_note_tile derives `mask` from ONE tile's
     * 64 pixels, so the re-assign always starts with a SUBSET of what the
     * set had and grows through mdp_extend_set. Requiring equality made
     * this branch dead (0 entries in 4000 frames) -- which is also the
     * real reason the greedy path never landed where it was. */
    if (mdp_pend_tag[s] && mdp_pend_line[s]
        && (mask & (uint8_t)~mdp_pend_used[s]) == 0) {
        unsigned ol = (unsigned)(mdp_pend_line[s] - 1);
        int ok = 1;
        for (int p = 0; p < 8 && ok; p++) {
            unsigned pen;
            if (!(mask & (1u << p)))
                continue;
            pen = mdp_pend_map[s * 8 + p];
            if (!pen || pen > 15) { ok = 0; break; }
            if (mdp_line_c[ol * 16 + pen] != 0xFFFF
                && mdp_line_c[ol * 16 + pen] != qc[p]) {
#ifdef PEN_REPAINT
                /* LOOP29 164: the set that churns is a FADING one, so
                 * when it comes back its colour no longer matches the
                 * pen it left behind and the old placement is rejected
                 * for the wrong reason. A tile's pattern bytes depend on
                 * the pen INDEX, not the pen COLOUR. If the pen is still
                 * OURS -- we own it and PENHOLD kept our refcount, so
                 * nobody else is showing through it -- repaint it to the
                 * new colour and keep the index. Tags survive, colour is
                 * current, and this is the case PENHOLD created. */
                if (mdp_pen_own[(ol * 16 + pen) * 2] == (uint8_t)s
                    && mdp_pen_own[(ol * 16 + pen) * 2 + 1] == (uint8_t)p
                    && mdp_pen_rc[ol * 16 + pen] <= 1)
                    continue;                /* repainted in the apply loop */
#endif
                ok = 0;                      /* someone else took it */
            }
        }
        if (ok) {
            for (int p = 0; p < 8; p++) {
                unsigned pen;
                if (!(mask & (1u << p)))
                    continue;
                pen = mdp_pend_map[s * 8 + p];
                if (mdp_line_c[ol * 16 + pen] == 0xFFFF
#ifdef PEN_REPAINT
                    || (mdp_line_c[ol * 16 + pen] != qc[p]
                        && mdp_pen_own[(ol * 16 + pen) * 2] == (uint8_t)s
                        && mdp_pen_own[(ol * 16 + pen) * 2 + 1] == (uint8_t)p)
#endif
                   ) {
                    mdp_line_c[ol * 16 + pen] = qc[p];
                    mdp_pen_own[(ol * 16 + pen) * 2]     = (uint8_t)s;
                    mdp_pen_own[(ol * 16 + pen) * 2 + 1] = (uint8_t)p;
                }
                mdp_pen_rc[ol * 16 + pen]++;
                mdp_s_map[s * 8 + p] = (uint8_t)pen;
                mdp_s_qc[s * 8 + p]  = qc[p];
            }
            mdp_s_line[s] = mdp_pend_line[s];
#ifdef CAT1_MD
            MDP_LAST_SET(s, (unsigned)mdp_pend_line[s]);
#endif
            mdp_s_used[s] = mask;
            mdp_s_stmp[s] = stamp;
            /* pend stays SET: the set is back on its old line with its
             * old pens for the pixels it has so far, and every later
             * extend must land on the old map too or the pattern moves
             * after all. Cleared only on a reject. */
            MDA(25);                         /* tags kept: same pattern */
            DIAG[35]++;
            return 1;
        }
        MDA(26);
        mdp_pend_tag[s] = 0;
        mdp_wipe_set_tags(s);                /* cannot land where it was */
    }
#endif

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
                if ((mask & (1u << p2))
                    && (qc[p2] == qc[p] || mdp_near(s, p, p2))) dup = 1;
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
    if (!bestfit && soft)
        return 0;                            /* cat-1 claim: no line without
                                              * eviction — the FB keeps the
                                              * cell (CAT1_PEND) */
    if (!bestfit) {
        /* evict LRU sets from the chosen line until it fits or nothing
         * evictable is left */
        for (;;) {
            unsigned victim = 128, vage = 0;
            for (unsigned s2 = 0; s2 < 128; s2++) {
                unsigned age;
                if (s2 == s || mdp_s_line[s2] != (uint8_t)(bestl + 1))
                    continue;
#ifdef MD_STATIC
                if (mds_pin[s2]) { MDS[1]++; continue; }
#endif
                age = (uint8_t)(stamp - mdp_s_stmp[s2]);
                /* >= 12: a set's cells re-stamp every <= 9 windows now
                 * (1 tile + 4 B + 4 A rotation) — the old >= 8 gate
                 * evicted LIVE sets and churned frees/reassigns */
                if (age >= 12 && age >= vage) { vage = age; victim = s2; }
            }
            if (victim == 128)
                break;
            MDA(16);
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
        int p2;
        if (!(mask & (1u << p)))
            continue;
        for (p2 = 0; p2 < p; p2++)
            if ((mask & (1u << p2)) && mdp_near(s, p, p2))
                break;
        if (p2 < p) {                        /* NEAR MERGE: one pen */
            mdp_s_map[s * 8 + p] = mdp_s_map[s * 8 + p2];
            mdp_s_qc[s * 8 + p]  = mdp_s_qc[s * 8 + p2];
            mdp_pen_rc[bestl * 16 + mdp_s_map[s * 8 + p2]]++;  /* free_set
                                                  * decrements per pixel */
            continue;
        }
        mdp_s_map[s * 8 + p] =
            (uint8_t)mdp_claim_pen((unsigned)bestl, qc[p], s, (unsigned)p);
        mdp_s_qc[s * 8 + p] = qc[p];
    }
    mdp_s_line[s] = (uint8_t)(bestl + 1);
#ifdef CAT1_MD
    MDP_LAST_SET(s, (unsigned)(bestl + 1));
#endif
    mdp_s_used[s] = mask;
    mdp_s_stmp[s] = stamp;
#ifdef TAGKEEP
    if (mdp_pend_tag[s]) {
        int same = (mdp_pend_line[s] == mdp_s_line[s])
                && (mdp_pend_used[s] == mask);
        if (same)
            for (int p = 0; p < 8; p++)
                if ((mask & (1u << p))
                    && mdp_pend_map[s * 8 + p] != mdp_s_map[s * 8 + p]) {
                    same = 0; break;
                }
        mdp_pend_tag[s] = 0;
        if (same) MDA(22);                   /* pattern identical: keep */
        else { MDA(23); mdp_wipe_set_tags(s); }
    }
#endif
    DIAG[35]++;                              /* set assigns */
    return 1;
}
#ifdef MD_STATIC
/* Install scene `sc`'s baked pen tables wholesale and invalidate every
 * VRAM slot so each pattern re-converts under the table's remap — the
 * same invalidation mdp_free_set relies on for a relocation, applied to
 * the whole map. Refcounts and owners are rebuilt from the maps so the
 * per-window live CRAM refresh keeps tracking fades exactly as now. */
static void mds_install(unsigned sc, uint8_t stamp)
{
    /* SELECTIVE INVALIDATION: a slot's pattern depends only on its set's
     * (line, pixel->pen map); a set whose dynamic assignment already
     * equals the table keeps its slots (same remap -> same bytes), so an
     * install after the reveal re-converts only the sets that change —
     * the wrong-sky case — instead of blanking the whole plane. */
    uint8_t changed[128];
    for (unsigned s2 = 0; s2 < 128; s2++) {
        int same = mdp_s_line[s2] == mds_s_line[sc][s2];
        if (same && mdp_s_line[s2])
            for (int p = 0; p < 8; p++)
                if ((mds_s_used[sc][s2] & (1u << p))
                    && mdp_s_map[s2 * 8 + p] != mds_s_map[sc][s2 * 8 + p])
                    same = 0;
        changed[s2] = (uint8_t)!same;
    }
    for (int i = 0; i < MDP_LINES * 16; i++) {
        mdp_line_c[i] = mds_line_c[sc][i];
        mdp_pen_rc[i] = 0;
        mdp_pen_own[i * 2] = mdp_pen_own[i * 2 + 1] = 0;
    }
    for (unsigned s2 = 0; s2 < 128; s2++) {
        mdp_s_line[s2] = mds_s_line[sc][s2];
        mdp_s_used[s2] = mds_s_used[sc][s2];
        mdp_s_vol[s2]  = 0;
#ifdef DRIFT_VOL
        if (s2 < MDP_LINES * 16) mdp_pen_vol[s2] = 0;   /* scene install:
                                              * the new tables own the
                                              * lines, so last scene's
                                              * animating pens are gone */
#endif
        mdp_s_stmp[s2] = stamp;
        mds_pin[s2] = mdp_s_line[s2] ? 1 : 0;
        for (int p = 0; p < 8; p++) {
            unsigned pen = mds_s_map[sc][s2 * 8 + p];
            mdp_s_map[s2 * 8 + p] = (uint8_t)pen;
            mdp_s_qc[s2 * 8 + p]  = mdp_s_line[s2] ? mdp_line_c[(mdp_s_line[s2] - 1) * 16 + pen] : 0;
            if (mdp_s_line[s2] && (mdp_s_used[s2] & (1u << p))) {
                unsigned i = (unsigned)(mdp_s_line[s2] - 1) * 16 + pen;
                if (!mdp_pen_rc[i]) {
                    mdp_pen_own[i * 2]     = (uint8_t)s2;
                    mdp_pen_own[i * 2 + 1] = (uint8_t)p;
                }
                mdp_pen_rc[i]++;
            }
        }
    }
    MDA(11);
    for (int i = 0; i < NSETS * NWAYS; i++)
        if (md_tag[i] != 0xFFFFFFFFu && changed[(md_tag[i] >> 16) & 0x7F]) {
            md_tag[i] = 0xFFFFFFFFu;
            MDA(12);
        }
    MDS[0]++;
}

/* Display-off flush: the old scene's tiles never compete with the new
 * scene's for slots. Runs inside the blank the display gate already
 * holds, so nothing visible changes. */
static void mds_flush(void)
{
    MDA(9);
    for (int i = 0; i < NSETS * NWAYS; i++) {
        if (md_tag[i] != 0xFFFFFFFFu) MDA(10);
        md_tag[i] = 0xFFFFFFFFu;
        md_ref[i] = 0;
    }
    for (int i = 0; i < NSETS * NWAYS / 32; i++)
        md_dirty[i] = 0;
    MDS[2]++;
}
#endif
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
#ifdef WIN_TWO
/* region guard: WIN_TWO's code growth — snap moves to the 0x39800 gap
 * (after the wrap trackers; ends 0x399E8 < missq 0x3A000). Fixed
 * block: boot ZEROES it explicitly (a garbage pq would index tilemap
 * pages wild for the first pre-latch window). */
#define snap ((layer_regs *)0x06039940)
#else
static layer_regs snap[2];
#endif

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
    /* POINTER WALK (2026-09-01, pipelining arc D2): the 44 tile columns
     * of a row are CONSECUTIVE tilemap words except at one 64-column
     * page boundary (44 < 64, so at most one split per row; the 128-
     * column wrap is a boundary too). The old loop redid the whole
     * page/row/column address computation per tile: this drain was
     * 0.335v per VINT (0.63v/gen) of master time, on the close's
     * critical path. Same words in the same order (offline-proven
     * against the original index formula, 20000 random cases). */
    const uint8_t lvl_lo = which ? 1 : 2, lvl_hi = which ? 2 : 4;
    for (int r = r0; r < nrows && r < r0 + nr; r++) {
        int vy = (vy0 - yf + r * 8) & 0x1FF;
        int trow = (int)(((unsigned)vy >> 3) & 0x1F);
        int qy = (int)(((unsigned)vy >> 7) & 2);
        const uint16_t *b0 = TILEMAP_C + pq[qy] * 0x800 + trow * 64;
        const uint16_t *b1 = TILEMAP_C + pq[qy + 1] * 0x800 + trow * 64;
        unsigned tx = ((unsigned)(vx00 >> 3) - 1u) & 0x7F;
        int n = 44;
        while (n) {
            const uint16_t *tp = ((tx & 64) ? b1 : b0) + (tx & 63);
            int run = 64 - (int)(tx & 63);
            if (run > n) run = n;
            for (int i = 0; i < run; i++) {
                uint16_t w = tp[i];
                unsigned cc = ((unsigned)w >> 6) & 0x7F;
                tcount[cc]++;
                if (w) {
                    uint8_t lvl = (w & 0x8000) ? lvl_hi : lvl_lo;
                    if (col_lvl[cc] && col_lvl[cc] != lvl)
                        amb_col[cc] = 1;
                    if (lvl > col_lvl[cc])
                        col_lvl[cc] = lvl;
                }
            }
            n -= run;
            tx = (tx + (unsigned)run) & 0x7F;
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
RAMCODE static void bm_tail_body(struct bm_state *a, int par);
#define BMT_DONE ((volatile uint32_t *)0x2603A7B4)   /* [par] bm_tail completions */
#define BMT_AT_CLAIM ((volatile uint32_t *)0x2603A7BC) /* [par] BMT_DONE seen at the claim */
static void bm_tail(struct bm_state *a, int par)
{
    BMT_PAR = (uint32_t)par;
    bm_tail_body(a, par);
    BMT_PAR = 0xFF;
    BMT_DONE[par & 1]++;
}
static void bm_tail_body(struct bm_state *a, int par)
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
#if defined(MD_SPR) && defined(SPR_MD_FREE)
        /* LOOP 27 q4 SPRMDFREE: a record the MD VDP renders needs no
         * 32X pair; a set live ONLY through such records must not hold
         * one (census: ~1 of 14 pairs at every failed late claim). */
        if (d2 & 0x2000)
            continue;
#endif
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
#ifdef PAIR_HOLD
    /* nspr is computed further down for `need`; the ageing loop runs
     * first and has to know demand, so count it here. */
    unsigned nspr_early = 0;
    for (int sc = 0; sc < 64; sc++)
        if (sused[sc]) nspr_early++;
#endif
    for (int p = 1; p < 16; p++)
        if (pr_key[p] != 0xFF) {
            if (sused[pr_key[p]])
                pr_age[p] = 0;
#ifdef PAIR_HOLD
            /* LOOP 19 — HOLD PAIRS ONLY WHILE THERE IS SLACK. Measured
             * with the tile squatters cleared: 6 of the 9 reserved pairs
             * are owned by sets that are NOT on screen, because a
             * departed set keeps its pair until age > 90 — 4.5 seconds
             * at 20Hz. With 9 pairs and 9 sets wanting one, that
             * guarantees starvation no matter how the rest is tuned.
             * The long hold exists so a set that flickers off for a
             * frame does not lose its pen (and get recoloured on
             * return), which is worth keeping WHEN pairs are spare. So
             * make it demand-aware: hold for 90 while there is slack,
             * and release after 2 once demand reaches the reserve. */
            else if (++pr_age[p] > (nspr_early >= 8 ? 2u : 90u))
                pr_key[p] = 0xFF;
#else
            else if (++pr_age[p] > PR_HOLD_TICKS)
                pr_key[p] = 0xFF;
#endif
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
#if defined(SPR_LATE) && defined(SPR_LATE_DIAG)
    /* CAN THE RESOURCE BE FOUND RATHER THAN BOUGHT? Every live sprite
     * colour SET is given its own 16-entry pair, but two sets with the
     * SAME 16 colours do not need two pairs — they need one. §11 already
     * proves the method on the MD side (21 tile sets collapsed to 36
     * distinct colours); nothing does it for 32X sprites.
     * Count live sets vs DISTINCT live palettes, exact 16-word match
     * against PAL_SH's sprite block. If the two differ, the shortfall is
     * bookkeeping, not hardware.
     * [12] worst live sets in a cycle   [13] distinct palettes there
     * [14] total sets   [15] total distinct (running ratio) */
    {
        unsigned nlive = 0, ndist = 0;
        uint8_t rep_sc[16];
        /* NB: not `a` — `sused` is a macro expanding to `a->sused`, so a
         * loop variable named `a` shadows the struct pointer. */
        for (int sa = 0; sa < 64; sa++) {
            if (!sused[sa])
                continue;
            nlive++;
            const volatile uint16_t *pa = PAL_SH + 1024 + sa * 16;
            unsigned dup = 0;
            for (unsigned r = 0; r < ndist && !dup; r++) {
                const volatile uint16_t *pb =
                    PAL_SH + 1024 + rep_sc[r] * 16;
                dup = 1;
                for (int w = 0; w < 16; w++)
                    if (pa[w] != pb[w]) { dup = 0; break; }
            }
            if (!dup && ndist < 16)
                rep_sc[ndist++] = (uint8_t)sa;
        }
    }
#endif
    int need = nspr < 6 ? 6 : (nspr > 10 ? 10 : nspr);
    int bound = 32 - 2 * need;
#ifdef TILE_CLASS
    /* STATIC TILE CLASSES (2026-08-25, LOOP19's untested lever, proven
     * offline: 268 harvested cycles, worst instant needs 11 fade-stable
     * classes vs the 17-20 groups the dynamic walk spends). Groups
     * 1..TILE_NCLASS belong to the classes; the dynamic side (text
     * singles + unobserved tile sets) gets 12..15; pairs get a FIXED
     * 8-pair zone at 16..31. Budget: 11 + 4 + 16 = 31 of 32 — the §11
     * over-subscription is gone by construction. TC_BOUND sweepable:
     * 16 = 8 pairs + 4 dynamic; 14 = 9 pairs + 2 dynamic (worst sprite
     * census is 9-11 live sets, so more pairs beats more dynamics). */
#ifndef TC_BOUND
#define TC_BOUND 14
#endif
    /* 14 measured best BOTH ways (2026-08-26): 9 pairs halve
     * RAMP_DRAWS vs 8 (1893 vs 3303 — the 9th pair matters under the
     * not-live steals), and Mike's corpus graded this arm "inverted
     * sprites are gone". KNOWN COST: the 2-group dynamic zone can
     * starve TEXT singles (his frame 74: HUD digits green). The
     * proper fix is static TEXT classes from an extended harvest
     * (text rows are as fade-stable as tile rows), not giving the
     * pair back. Tracked in the finish plan. */
    bound = TC_BOUND;
#endif
    for (int q = bound; q < 32; q++) {       /* evict IDLE singles from the
                                              * pair zone only: blanket
                                              * eviction of live ones made
                                              * group 14 flip between text
                                              * yellow and sprite pair 7
                                              * every cycle (the yellow
                                              * sprite-ghost artifact) */
        if (grp_key[q] == 0xFF)
            continue;
        if (grp_age[q] > 0) {
            grp_key[q] = 0xFF;
            continue;
        }
#ifdef GRP_RELOC
        /* LOOP 19 — MAKE THE RESERVATION REAL. Measured: the worst cycle
         * needs 8 sprite pairs, 9 are RESERVED above `bound`, and only 6
         * are usable, because live tile singles squat in the reserved
         * zone and a pair needs BOTH its groups. The budget was never
         * short — offline packing (tools/palpack.py) proved it cannot be
         * compressed either, since 18 of 19 sprite sets use all 14 pens.
         * The reservation is simply not enforced.
         * EVICTING a live single is what produced the yellow
         * sprite-ghost. RELOCATION is not eviction: move the sticky
         * claim to a free group BELOW bound and the colour survives, the
         * pair frees, and nothing flips. tile_grp/text_grp are rebuilt
         * from grp_key every cycle (they are cleared to 0xFF and
         * re-derived just below), so moving the claim is the whole job —
         * the mapping follows, and cram_memo repaints the destination
         * because its key changes.
         * If no low group is free, leave the squatter alone: strictly
         * today's behaviour, never worse. */
        {
            int t = -1;
            for (int u = 0; u < bound; u++)
                if (grp_key[u] == 0xFF) { t = u; break; }
            if (t >= 0) {
                grp_key[t]  = grp_key[q];
                grp_kind[t] = grp_kind[q];
                grp_age[t]  = grp_age[q];
                grp_key[q]  = 0xFF;
#ifdef SPR_LATE
                SPRLATE[12]++;               /* squatters relocated */
#endif
            }
#ifdef SPR_LATE
            else SPRLATE[13]++;              /* nowhere to move it */
#endif
        }
#endif
    }

    /* sprites first (a pair = aligned groups 2p/2p+1). Pair 15 is
     * DUAL-PURPOSE: allocatable like any pair (a hard reserve cost one
     * pair and blacked out the player in full scenes), but whenever no
     * color claims it, apply_cram parks the shadow ramp there. Shadows
     * always draw through base 240, so under peak pressure they tint
     * with pair 15's owner instead of going dark — cutscenes, where
     * shadows actually star, run far below capacity. */
    uint8_t shared_pair = 15;
#if defined(SPR_LATE) && defined(SPR_LATE_DIAG)
    /* CAPACITY CENSUS. The late claim failed because pairs were neither
     * free nor stealable, so measure the budget itself rather than the
     * symptom: how many sets WANT a pair this cycle, how many get their
     * own, and how many are forced to share one that is already owned
     * (which is persistent wrong colour, not a one-cycle flash).
     * [4] worst sets wanting a pair   [5] worst that got a unique one
     * [6] worst forced to share       [7] worst `bound` seen (tile side) */
    unsigned cen_want = 0, cen_got = 0, cen_share = 0;
    /* IS THE RESOURCE ABSENT OR BLOCKED? `need` reserves pairs above
     * `bound` for sprites, but a pair is only usable if BOTH its groups
     * are unowned — and the eviction above clears only IDLE singles
     * (grp_age > 0). A LIVE tile single sitting in the reserved zone
     * blocks a pair that was already set aside for sprites.
     * [8] worst groups occupied BELOW bound (the tile zone's own use)
     * [9] worst live singles squatting AT/ABOVE bound (blocked pairs) */
    {
        unsigned lo_used = 0, hi_squat = 0;
        for (int q = 0; q < 32; q++) {
            if (grp_key[q] == 0xFF)
                continue;
            if (q < bound) lo_used++;
            else           hi_squat++;
        }
        if (lo_used  > SPRLATE[8]) SPRLATE[8] = lo_used;
        if (hi_squat > SPRLATE[9]) SPRLATE[9] = hi_squat;
        /* THE NUMBER THAT DECIDES THE FIX. A squatter can be RELOCATED
         * (not evicted — blanket eviction is what caused the yellow
         * sprite-ghost) only if a group is free below bound to move it
         * into. Sample free-below-bound IN THE SAME CYCLE as a squat,
         * and keep the worst case: if this is ever 0 while squatters
         * exist, the tile zone is genuinely full and relocation cannot
         * work; if it stays positive, the blocked pairs are recoverable. */
        if (hi_squat) {
            unsigned lo_free = 0;
            for (int q = 0; q < bound; q++)
                if (grp_key[q] == 0xFF)
                    lo_free++;
            if (SPRLATE[10] == 0 || lo_free < SPRLATE[10])
                SPRLATE[10] = lo_free + 1;    /* +1 so 0 stays distinct */
            SPRLATE[11]++;                    /* cycles with a squatter */
        }
    }
#endif
    for (int sc = 0; sc < 64; sc++)
        spr_pair[par][sc] = 0xFF;
    /* HELD PAIRS FOLLOW THEIR SETS INTO EVERY MAP (2026-09-01, Mike's
     * level-transition orb, state s16_early.bs2): a set absent from THIS
     * snapshot still owns its pair (PAIR_HOLD keeps pr_key), but the map
     * said 0xFF for it. A sprite the game shows on alternate frames is
     * present in every other snapshot, and each map is built one
     * generation behind — so every visible frame met a map from an
     * absent frame and drew in the shadow ramp (sbuf held only pens
     * 240-254 in the orb's box). Used sets below re-assign as before. */
    /* ...but ONLY for sets seen in the PREVIOUS pass (the alternating-
     * frame case). apply_cram paints every set that has a pair in the
     * map, so mapping every held pair made long-absent sets paint
     * their pairs each window — a live set sharing a pair with one got
     * the absent set's colours (Neff black, 2026-09-02) and the FM span
     * grew with the extra paints (the stutter). One pass of memory. */
    for (int q = 1; q < 15; q++)
#ifdef LATE_KEEP
        /* LOOP 27 entry 6: a pair claimed LATE this cycle (pr_age 0, owner
         * absent from the previous snapshot this build scanned) must
         * survive the rebuild — at one cycle per vint the chunked build
         * routinely completes after the claim and wiped it, and the set
         * drew in the shadow ramp for the whole cycle. */
        if (pr_key[q] != 0xFF && (sused_prev[pr_key[q]] || pr_age[q] == 0))
#else
        if (pr_key[q] != 0xFF && sused_prev[pr_key[q]])
#endif
            spr_pair[par][pr_key[q]] = (uint8_t)q;
    for (int sc = 0; sc < 64; sc++)
        /* HELD-PAIR WINDOW (2026-09-05, Zeus): the game shows Zeus one
         * frame in THREE during the scale-in (mamecap 2440-2447: on,
         * off, off) — a ghost flicker. A one-snapshot memory maps the
         * held pair only when the set was in the previous snapshot, so
         * most visible generations found no pair and drew him through
         * the dark shadow pair. Four generations of memory: the pair
         * stays mapped across the off frames and the flicker shows at
         * our cadence, coloured. Bounded, so an absent set stops
         * repainting a shared pair after four (the Neff-black limit). */
        sused_prev[sc] = sused[sc] ? 4
                       : (uint8_t)(sused_prev[sc] ? sused_prev[sc] - 1 : 0);
    for (int sc = 0; sc < 64; sc++) {
        if (!sused[sc])
            continue;
        int p = -1;
        for (int q = 1; q < 16; q++)
            if (pr_key[q] == sc) { p = q; break; }
        if (p < 0)
            for (int q = 14; q >= bound / 2; q--)   /* 14, NOT 15: pair
                                                     * 15 is the SHADOW
                                                     * RAMP — handing it
                                                     * out drew sets in
                                                     * ramp-black and
                                                     * repainting it
                                                     * corrupted every
                                                     * silhouette
                                                     * (2026-08-27) */
                if (pr_key[q] == 0xFF && grp_key[2 * q] == 0xFF
                    && grp_key[2 * q + 1] == 0xFF) { p = q; break; }
        if (p < 0) {                         /* steal the oldest OFF-SCREEN
                                              * pair (age>=3: stealing one
                                              * merely absent THIS scan
                                              * recolors pixels still
                                              * displayed) */
            uint8_t best = 2;
            for (int q = 14; q >= bound / 2; q--)   /* 14: ramp reserved */
                if (pr_key[q] != 0xFF && pr_age[q] > best) {
                    best = pr_age[q]; p = q;
                }
#ifdef TILE_CLASS
            if (p < 0) {
                /* NOT-LIVE STEAL at the map (2026-08-25): with the
                 * static tile classes guaranteeing the pair zone, a
                 * pair whose owner is absent from THIS prescan
                 * (!sused) and has been gone a full cycle (age>=1)
                 * goes to the set that is on screen NOW. The age
                 * floor keeps lightning-blink actors (Zeus) from
                 * being robbed on their off-frame. Claims made HERE
                 * paint in this same window via apply_cram — no
                 * repaint lag, unlike the late-claim rescue. */
                uint8_t ba = 1;
                for (int q = 14; q >= bound / 2; q--)   /* 14: ramp reserved */
                    if (pr_key[q] != 0xFF && !sused[pr_key[q]]
                        && pr_age[q] >= ba) {
                        ba = pr_age[q]; p = q;
                    }
            }
#endif
        }
        if (p < 0) {
            spr_pair[par][sc] = shared_pair;  /* over budget: share */
#if defined(SPR_LATE) && defined(SPR_LATE_DIAG)
            cen_want++; cen_share++;
#endif
            continue;
        }
        pr_key[p] = (uint8_t)sc;
        pr_age[p] = 0;
        grp_key[2 * p] = grp_key[2 * p + 1] = 0xFF;
        spr_pair[par][sc] = (uint8_t)p;
        shared_pair = (uint8_t)p;
#if defined(SPR_LATE) && defined(SPR_LATE_DIAG)
        cen_want++; cen_got++;
#endif
    }
#if defined(SPR_LATE) && defined(SPR_LATE_DIAG)
    if (cen_want  > SPRLATE[4]) SPRLATE[4] = cen_want;
    if (cen_got   > SPRLATE[5]) SPRLATE[5] = cen_got;
    if (cen_share > SPRLATE[6]) SPRLATE[6] = cen_share;
    /* MIN bound, not max: bound = 32 - 2*need, so a SMALLER bound means
     * MORE pairs reserved for sprites. The constrained case is the
     * smallest bound seen, and recording the max measured the cycle
     * where sprites needed least — the opposite of the question. */
    if (SPRLATE[7] == 0 || (unsigned)bound < SPRLATE[7])
        SPRLATE[7] = (unsigned)bound;
    /* TILEDEDUP cleared the tile squatters (6 -> 1) and sprite capacity
     * did NOT move, so something else holds the reserved pairs. Prime
     * suspect: pr_key keeps a pair for its owner until pr_age > 90, so a
     * set that left the screen still owns a pair for ~90 cycles. With 9
     * pairs and 9 sets wanting one, any stale hold starves somebody.
     * [15] worst pairs in the reserved zone held by an ABSENT set
     * ([11] is already the squatter-cycle counter - this file has now
     * paid for TWO counter collisions in one loop; check before reusing). */
    {
        unsigned stale = 0;
        for (int q = 15; q >= bound / 2; q--)
            if (pr_key[q] != 0xFF && !sused[pr_key[q]])
                stale++;
        if (stale > SPRLATE[15]) SPRLATE[15] = stale;
    }
#endif

    /* text + tile singles (mass tile colors before rare ones) */
    for (int c = 0; c < 8; c++)
        text_grp[par][c] = 0xFF;
    for (int c = 0; c < 128; c++)
        tile_grp[par][c] = 0xFF;
    uint8_t shared_tile = 0xFF;
#ifdef TILE_CLASS
    uint16_t tc_claimed = 0;             /* class groups repointed to a
                                          * LIVE member this cycle (first
                                          * live member wins; apply_cram
                                          * then paints from a row that
                                          * is actually being displayed —
                                          * a non-live member's PAL_SH
                                          * row may be mid-fade stale) */
#endif
    for (int pass = 0; pass < 3; pass++) {
        for (int c = 0; c < (pass ? 128 : 8); c++) {
            int kind = pass ? 0 : 1;
            if (pass == 0 && !txused[c]) continue;
#ifdef TEXT_CLASS
            if (pass == 0 && c == 0) {
                /* HUD text class: fixed home in group 0. The boot pin
                 * is PERMANENT — no allocator/steal/evict loop ever
                 * touches index 0 (they all start at 1 or bound) — so
                 * only the per-parity map needs refreshing. */
                text_grp[par][0] = 0;
                continue;
            }
#endif
            if (pass == 1 && tcount[c] < 24) continue;
            if (pass == 2 && (!tcount[c] || tile_grp[par][c] != 0xFF)) continue;
#ifdef TILE_CLASS
            if (kind == 0) {
                uint8_t tcls = tile_class_rom[c];
                if (tcls != 0xFF) {
                    uint8_t tg = (uint8_t)(1 + tcls);
                    tile_grp[par][c] = tg;
                    if (!(tc_claimed & (1u << tcls))) {
                        tc_claimed |= (uint16_t)(1u << tcls);
                        grp_key[tg] = (uint8_t)c;
                        grp_kind[tg] = 0;
                        grp_age[tg] = 0;
                    }
                    continue;            /* fixed home; nothing to claim */
                }
            }
#endif
            int g = -1;
            for (int q = 1; q < bound; q++)
                if (grp_key[q] == (uint8_t)c && grp_kind[q] == kind
                    && pr_key[q >> 1] == 0xFF) { g = q; break; }
#ifdef TILE_DEDUP
            /* LOOP 19 — SHARE BY PALETTE CONTENT, NOT BY SET IDENTITY.
             * Measured over 899 cycles: the live tile colours hold only
             * TEN distinct 8-entry palettes at the worst cycle, and
             * exact dedup is 3.31x — yet the allocator spends 19-20
             * groups on them, because it claims one per COLOUR SET.
             * That over-spend is the whole sprite shortage: tiles ~20 +
             * sprites 16 = ~36 groups against 32.
             * If a group already holds a byte-identical palette, point
             * this colour at it and claim nothing. No remap is needed —
             * the colours ARE the same — so unlike colour-level packing
             * this costs nothing in the draw path, and unlike the
             * `shared_tile` fallback below it is not an approximation.
             * grp_key stays with the owner; apply_cram paints the group
             * from the owner's palette, which is identical by
             * construction. tile_grp is rebuilt every cycle, so a fade
             * that separates two sets is caught on the next rebuild. */
            if (g < 0 && kind == 0) {
                const volatile uint16_t *pc = PAL_SH + c * 8;
                for (int q = 1; q < bound; q++) {
                    if (grp_key[q] == 0xFF || grp_kind[q] != 0
                        || pr_key[q >> 1] != 0xFF)
                        continue;
                    const volatile uint16_t *pk = PAL_SH + grp_key[q] * 8;
                    int same = 1;
                    for (int w = 0; w < 8; w++)
                        if ((pc[w] ^ pk[w]) & 0x7FFF) { same = 0; break; }
                    if (same) {
                        tile_grp[par][c] = (uint8_t)q;
                        grp_age[q] = 0;
#ifdef SPR_LATE
                        SPRLATE[14]++;       /* colours sharing a palette */
#endif
                        break;
                    }
                }
                if (tile_grp[par][c] != 0xFF)
                    continue;                /* shared: claim nothing */
            }
#endif
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
            /* g >= 32 BOUNDS the write: pri_lut[2][256] ends at
             * 0x3E780 = md_pkt[0], and a wild group value (g=32 seen
             * live) wrote priority-level BYTES OF 1 over the packet
             * magic+type every frame — the R60 "wild .bss writer":
             * MD plane dead (no chunk ever consumed), md_phase
             * zeroed, block-statics wiped. MAME wpset caught it:
             * 317 writes of 1 to 0x3E780, all from this loop.
             * DIAG[35] counts wild groups — nonzero means the GROUP
             * ALLOCATOR is handing out 32: find out why. */
            if (g >= 32) {
                if (g != 0xFF)
                    DIAG[35]++;
                continue;
            }
            if (!col_lvl[c])
                continue;
            for (int p = 1; p < 8; p++)
                if (col_lvl[c] > pl[g * 8 + p])
                    pl[g * 8 + p] = col_lvl[c];
        }
        for (int c = 0; c < 8; c++) {
            uint8_t g = text_grp[par][c];
            if (g >= 32) {
                if (g != 0xFF)
                    DIAG[35]++;
                continue;
            }
            if (!txt_lvl[c])
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
#ifdef NO_MAPS
    /* LOOP29 168 ABLATION, never a ship: report the drain instantly
     * complete so the master's maps work costs nothing. The picture is
     * wrong by construction (no name tables, no tile batches); the only
     * number this build produces that means anything is the GENERATION
     * WALL. Slave compose is 1.09 v/gen and master drain 0.44 against a
     * 1.57 wall, and 1.09+0.44 = 1.53, which SMELLS serial -- this says
     * whether it is. */
    (void)par;
    return 1;
#endif
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
 * docs/log/LOOP.md's law for the ares cadence fix. */
#ifdef PAL_PEN
/* PEN-GATED CRAM WRITE (2026-09-08, LOOP27 22). Real silicon accepts a
 * palette write ONLY while PEN is asserted — vblank, hblank, or display
 * off — and drops it silently during active scan (srcref/S32X_MiSTer
 * rtl/32X/VDP.sv:170 gates palette access on PEN; :403/:405 assert it on
 * VBLK or H_CNT 0x159..0x016). PEN is bit 13 of the SAME register we
 * already read for FS: reg 0xA returns {VBLK,HBLK,PEN,11'h0,FEN,FS}
 * (VDP.sv:165), i.e. MARS_VDP_FBCTL & 0x2000.
 *
 * WHY NOT THE VBLANK FLUSH (PAL_VBLANK, entry 19): that flushed at the
 * flip, the only point in our frame that is both vblank AND FM=1 — and
 * on hardware the flip rarely lands, so nothing flushed and the screen
 * went ENTIRELY to the MD backdrop (entry 22). The test suite's
 * apply-in-the-VBI-handler idiom assumes the master owns FM outright,
 * which it does and we do not: our 68K holds FM for most of the frame.
 * HBLANK is the way out — it comes round every scanline, and the render
 * window already holds FM, so PEN is the only thing left to wait for.
 *
 * BOUNDED: ~1200 FRT ticks, about one scanline, then write anyway. An
 * unbounded spin would hang on any machine that never reports PEN, and
 * this must never be able to wedge the pipeline. */
/* v2 (2026-09-08, entry 23): the per-entry spin was WRONG ON HARDWARE.
 * A ~1200-tick wait per changed entry, times the hundreds that change on
 * a scene cut, is hundreds of scanlines of spinning inside the render
 * window — the window blows its budget, compose and blit never run, the
 * framebuffer stays all-zero and the screen goes to a flat CRAM 0.
 * (Mike's MiSTer: uniform green = the BOOT_SHSTAGE stage colour in
 * entry 0, with an empty FB on top of it. ares never showed it because
 * ares reports PEN asserted, so the spin never spun.)
 *
 * BURST instead: wait for PEN ONCE, then write entries back to back for
 * as long as PEN holds — hblank fits many stores — and only wait again
 * when it drops. Plus a hard per-window BUDGET: whatever is still dirty
 * when the budget runs out stays dirty and goes in the next window. The
 * palette can lag a frame; the pipeline must never stall. */
static uint32_t cram_dirt[8];
static void cram_flush_pen(void)
{
    volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM;
    for (int w = 0; w < 8; w++) {
        uint32_t d = cram_dirt[w];
        while (d) {
            /* v5 (entry 28): NO WAIT, EVER. Diffing ares against the RTL
             * (entry 27) showed a palette write outside hblank/vblank
             * does not vanish — it STALLS the SH-2 until PEN, in both
             * implementations (ares io-internal.cpp:355 spins the CPU;
             * VDP.sv:170 withholds ACK_N). So a software wait for PEN is
             * a second copy of a stall the bus already does, which is
             * what starved the window in v1-v2. Check PEN and SKIP: if
             * it is low, leave the rest dirty and come back. The caller
             * that matters runs in vblank, where PEN is asserted for the
             * whole interval and the whole set drains in one burst with
             * no stall at all. */
            /* PEN is up: burst while it holds */
            do {
                int b = __builtin_ctz(d);
                d &= d - 1;
                int i = w * 32 + b;
                cram[i] = (uint16_t)(cram_mirror[i] & 0x7FFF);
                DIAG[19]++;
            } while (d && (MARS_VDP_FBCTL & 0x2000));
        }
        cram_dirt[w] = 0;
    }
}
#endif
static inline void cram_set(volatile uint16_t *dst, int idx, uint16_t v)
{
    if (cram_mirror[idx] != v) {
        cram_mirror[idx] = v;
#ifdef PAL_PEN
        (void)dst;                      /* cram_flush_pen writes by index */
        cram_dirt[idx >> 5] |= 1u << (idx & 31);
#elif defined(PAL_VBLANK)
        /* deferred: the write lands in cram_flush_vbl() at the flip,
         * inside vblank, where hardware actually accepts it. dst is
         * unused on this path — the flush addresses CRAM by index. */
        (void)dst;
        cram_dirt[idx >> 5] |= 1u << (idx & 31);
#else
        dst[0] = (uint16_t)(v & 0x7FFF);
        DIAG[19]++;                     /* CRAM writes actually performed */
#endif
        shadow_dirty = 1;
    }
}

#ifdef CRAM_FLIP
/* ARC B HALF 1 (2026-08-30): REMAP paints deferred to the FLIP.
 * CRAM is global; apply_cram runs mid-scan of the DISPLAYED old
 * generation, so a group REMAP painted there recolors old pixels
 * under the new mapping for the rest of the frame — Mike's blink
 * strips, cross-correlated: pure COLOR events, no displacement.
 * Census: ~15 remap paints/frame, 0.4 value paints. Remaps queue
 * here and drain in flip_span RIGHT AFTER the reveal; VALUE paints
 * (fades) stay live. RING, not reset-drain: the ISR can interrupt
 * a push mid-increment, and a drain that zeroes the length orphans
 * the half-pushed entry into a stale slot (single producer =
 * window path, single consumer = ISR; u8 head/tail, mod 32).
 * Overflow (tail-head >= 32) falls back to a live paint = today's
 * behavior. A declined flip keeps the queue for the next ISR —
 * correct: the reveal has not happened. */
/* v2 (same day): RING -> DEDUPE TABLE + TWO-FLIP HOLD. The bank
 * probe (scratchpad bank_diverge.py, play_level1 frame 1500) caught
 * the mechanism red-handed: indices 209-212 in one bank are 225-228
 * in the other — SAME art, +16 = one sprite pair over. A pair
 * reassignment lands between the banks' compose generations, the
 * pixels carry the pair, CRAM matches ONE bank, and the other
 * shimmers on alternate flips until re-shipped (seconds under
 * compose-skip) — Mike's gravestone flicker, and the old "orb
 * magenta" sightings (the stolen pair shows the STEALER's colors).
 * So a remap paint must wait until BOTH banks carry the new-pair
 * pixels: hold each queued slot for TWO flips. Only the LATEST
 * mapping per slot matters -> direct-map by CRAM 8-slot (base>>3),
 * ~15 remaps/frame collapse to <=32 pending by construction.
 * cq_qn==0 marks empty; producer writes fields then qn LAST (the
 * publish); the ISR consumer paints then clears qn. */
static uint8_t  cq_qbase[32];       /* CRAM entry base (== dst - CRAM) */
static uint16_t cq_qoff[32];        /* PAL_SH word offset of the source */
static uint8_t  cq_qstamp[32];      /* flip counter at enqueue */
static volatile uint8_t cq_qn[32];  /* block length; 0 = empty */
static volatile uint8_t cq_flipno;
static uint8_t  memo_was_remap;     /* set by cram_memo's key-change
                                     * miss, eaten by the next
                                     * cram_paint (the call pattern is
                                     * memo-then-paint everywhere; a
                                     * stray value-paint riding a stale
                                     * flag just lands one frame late,
                                     * invisible for a fade step) */
#endif

/* Convert-and-store one colour-set's block: the inner half of apply_cram,
 * factored out so the land-time painter below runs BYTE-IDENTICAL code
 * rather than a second transcription of the same arithmetic. */
/* 2026-09-05: cart ROM — per changed sprite set at the landing (200B). */
static void cram_paint(volatile uint16_t *dst, volatile uint16_t *src,
                               int base, int n)
{
#ifdef CRAM_FLIP
    if (memo_was_remap) {
        memo_was_remap = 0;
        uint8_t s = (uint8_t)((base >> 3) & 31);
        cq_qn[s] = 0;                /* unpublish FIRST: the ISR (the
                                      * only interleaver — same CPU)
                                      * landing mid-write must skip,
                                      * never paint half-new fields */
        cq_qbase[s] = (uint8_t)base;
        cq_qoff[s] = (uint16_t)(src - PAL_SH);
        cq_qstamp[s] = cq_flipno;
        cq_qn[s] = (uint8_t)n;       /* the publish, LAST */
        return;                      /* painted after BOTH banks
                                      * carry the new-pair pixels */
    }
#endif
    for (int p = 0; p < n; p++)
        cram_set(dst + p, base + p,
                 (uint16_t)(s16_to_mars(src[p]) | (src[p] & 0x8000)));
}
#ifdef CAT1_MD
/* LOOP29 151: ONE COLOUR FOR ONE TILE. Under MDBGALL the framebuffer's
 * only tiles are cat-1 cells drawn over sprite rows; MD plane A draws the
 * same cells everywhere else (C1 step 2). Category 1 is a static ROM bit
 * (LOOP-DECOMPILE, question 5), so the shimmer Mike saw was the two
 * renderers disagreeing on colour along a boundary that moves with the
 * sprites: 5-bit arcade colour in the FB, the MD line's 3-bit pen next
 * to it. Paint the tile set's 32X CRAM entries with the SAME quantised
 * colour the MD line carries (mdp_s_qc, 9-bit bbb ggg rrr, expanded 3->5
 * bits) whenever the set has an MD line; the boundary then separates
 * identical pixels. Sets without a line keep the arcade colour. */
static void cram_paint_tile(volatile uint16_t *dst, int base, unsigned c)
{
    volatile uint16_t *src = PAL_SH + c * 8;
    if (!mdp_s_line[c]) { cram_paint(dst, src, base, 8); return; }
    for (int p = 0; p < 8; p++) {
        uint16_t q = mdp_s_qc[c * 8 + p], v;
        if (q == 0xFFFF) {
            v = (uint16_t)(s16_to_mars(src[p]) | (src[p] & 0x8000));
        } else {
            unsigned r = q & 7, g = (q >> 3) & 7, b = (q >> 6) & 7;
            r = (r << 2) | (r >> 1); g = (g << 2) | (g >> 1); b = (b << 2) | (b >> 1);
            v = (uint16_t)((b << 10) | (g << 5) | r | (src[p] & 0x8000));
        }
        cram_set(dst + p, base + p, v);
    }
}
#endif

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
/* SLOT COLLISION #15 (2026-09-01, Mike's blue-gravestone state
 * s16_flap.bs1): PAL_SETGEN lived at 0x28C00 for 384B — straight
 * through cram_key (0x28C00), cram_keygen (0x28C40) and MDSPR_SAT
 * (0x28D00). Sprite set 0's generation sat in MDSPR_SAT[0], zeroed
 * every generation, so cram_memo's hit for pair 14 stayed true across
 * every palette change: the FIRST paint of sprite set 0 — the intro
 * lightning's blue-white — stayed in CRAM under the gravestone until
 * some other set displaced the pair (on-again-off-again). Moved to the
 * slave stack floor: 0x3F880-0x3F9FF (slave depth ~330B from 0x40000;
 * spr_pair holds 0x3F800-7F; sentinel paint now from 0x3FA00). */
#define PAL_SETGEN ((volatile uint16_t *)0x2603F880) /* slave-written, 192 */
#define CK_TILE 0x0000u
#define CK_TEXT 0x1000u
#define CK_SPR  0x2000u
/* Fixed SDRAM 0x28C00 (the free 256B after shadow_lut): .bss hit the
 * 0x19000 region guard again (2026-09-01, +40B of LTO layout drift);
 * boot-initialized below like cram_mirror/shadow_lut. */
#define cram_key    ((uint16_t *)0x06028C00)   /* (kind|set) per slot, 64B */
#define cram_keygen ((uint16_t *)0x06028C40)   /* set's gen when painted */
static volatile uint8_t cram_hijacked;      /* k2 debug bar clobbered CRAM 255 */

/* Returns 1 if slot already holds color-set `set` at its current
 * generation. The generation is PER SET, so a fade on one color-set no
 * longer invalidates every other group's memo. */
/* out-of-line since the arc-B census (2026-08-30): inline, the
 * counter code duplicated per call site and blew the region guard */
static __attribute__((noinline)) int cram_memo(int slot, uint16_t key,
                                               unsigned set)
{
    uint16_t gen = PAL_SETGEN[set];
    if (cram_key[slot] == key && cram_keygen[slot] == gen)
        return 1;
    /* ARC-B CENSUS (2026-08-30): REMAP paints (a different set takes
     * the slot — the violent recolor class; Mike's blink strips
     * cross-correlated to ZERO displacement = COLOR events) vs
     * VALUE paints (same owner, new gen — fades, must stay live).
     * 0x28FF0/F4 reused from the retired owed census. */
    if (cram_key[slot] != key) {
        (*(volatile uint32_t *)0x26028FF0)++;
#ifdef CRAM_FLIP
        memo_was_remap = 1;          /* route the coming paint to the
                                      * flip queue */
#endif
    } else
        (*(volatile uint32_t *)0x26028FF4)++;
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
    /* CRAM VERIFY ROTOR (2026-09-05, Mike's intermittent "blue
     * gravestone": rom/s16_arttail.bs1 had sprite set 0's pair wearing
     * the intro flash's red/white/blue with PAL_SH holding the greys and
     * PAL_SETGEN[128] still 0 — a memo hit over a stale paint, the third
     * incident of this class (docs/design/BOSSFIGHT.md "first paint stood")). The
     * verify-per-row law: forget one slot's generation per window, so
     * whatever the memo missed repaints within 32 windows. Cost: one
     * redundant 8/16-entry paint per window. */
    {
        static uint8_t cram_rot;
        cram_keygen[cram_rot] = 0xFFFF;
        cram_rot = (uint8_t)((cram_rot + 1) & 31);
    }
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
#ifdef PEN_MATCH
        /* PEN MATCH (2026-09-02, C1 done right, part 1): the FB draws a
         * tile with the MD's OWN colours — the set's 9-bit quantised
         * pens (mdp_quant) re-expanded to the nearest 32X 5-bit level
         * under ares's MD DAC {0,52,87,116,144,172,206,255}/255 —
         * so a tile split across the two renderers (FB on sprite rows,
         * plane A elsewhere) is one colour. q5[] folds mdp_quant's
         * channel rule and that expansion into one 32-entry table;
         * the memo stays (pure quantisation depends on PAL_SH only).
         * NO nearest-pen substitution: tile classes merge sets with
         * identical colours into one group, but each member set has
         * its own MD line and approximations, so alternate parities
         * painted one group two ways every window (4149 vs 876 CRAM
         * writes per 600 frames). Pure quantisation matches the MD
         * whenever the line holds the pen; it differs only under pen
         * exhaustion. Measured cost of the no-memo/shift version:
         * +300 master ticks per window, -3% ships — the window end
         * feeds the next landing on the one-vint cliff. */
        if (cram_memo(g, (uint16_t)(CK_TILE | c), (unsigned)c)) {
            DIAG[20]++;                      /* groups skipped */
            continue;
        }
        {
            static const uint8_t q5[32] = {
                /* (v + 2) >> 2 clamped to 7, then md2x[] */
                0, 0, 6, 6, 6, 6, 11, 11, 11, 11, 14, 14, 14, 14, 18, 18,
                18, 18, 21, 21, 21, 21, 25, 25, 25, 25, 31, 31, 31, 31, 31, 31 };
            for (int p = 0; p < 8; p++) {
                unsigned w = PAL_SH[c * 8 + p];
                unsigned r = ((w >> 12) & 1) | ((w << 1) & 0x1E);
                unsigned gg = ((w >> 13) & 1) | ((w >> 3) & 0x1E);
                unsigned bb = ((w >> 14) & 1) | ((w >> 7) & 0x1E);
                cram_set(cram + g * 8 + p, g * 8 + p,
                         (uint16_t)(q5[r] | (q5[gg] << 5) | (q5[bb] << 10)
                                    | (w & 0x8000)));
            }
        }
#else
        if (cram_memo(g, (uint16_t)(CK_TILE | c), (unsigned)c)) {
            DIAG[20]++;                      /* groups skipped */
            continue;
        }
#ifdef CAT1_MD
        cram_paint_tile(cram + g * 8, g * 8, (unsigned)c);
#else
        cram_paint(cram + g * 8, PAL_SH + c * 8, g * 8, 8);
#endif
#endif
    }
    for (int c = 0; c < 8; c++) {
        uint8_t g = text_grp[par][c];
        if (g == 0xFF)
            continue;
        if (cram_memo(g, (uint16_t)(CK_TEXT | c), (unsigned)c)) {
            DIAG[20]++;
            continue;
        }
#ifdef TEXT_CLASS
        if (g == 0) {
            /* group 0 = the HUD text class: entry 0 IS the through
             * bit — paint pens 1-7 only, never the base entry. */
            cram_paint(cram + 1, PAL_SH + c * 8 + 1, 1, 7);
            continue;
        }
#endif
#ifdef CAT1_MD
        cram_paint_tile(cram + g * 8, g * 8, (unsigned)c);
#else
        cram_paint(cram + g * 8, PAL_SH + c * 8, g * 8, 8);
#endif
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

RAMCODE __attribute__((noinline))
static void compose_layer_regs(int ylo, int yhi, int cpu, int which,
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
                    GAME_TILE_REMAP(code);
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
/* DIRECT_FB: column 40 is screen x 320..327 — sbuf margin bytes the
 * blit never shipped; the FB has no margin there (x 320 of row y IS
 * x 0 of row y+1), so the merge stops at column 39 = x 312..319. */
#ifdef DIRECT_FB
#define MERGE_CMAX 39
#else
#define MERGE_CMAX 40
#endif
#define MERGE_ROW(EXPR0, EXPR1)                                             \
                for (int c = 0; c <= MERGE_CMAX; c++) {                     \
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
                RL_MARK(by + y);
                uint32_t *dst = (uint32_t *)DROW(by + y);
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
#ifdef CAT1_MD
        /* C1 step 2: the cat-1 pass draws ONLY over rows where an SH-2
         * sprite landed this generation (ROWLIVE: cleared by the row
         * clear, marked by compose_sprites' strips — cat1 and text run
         * after). Everywhere else the FB stays 0 = MD-through, and
         * plane A's own priority copy of the tile shows. MD-claimed
         * sprites are handled by the MD (cell pri 1 over sprite pri 0). */
        int c1_all = 1;
        if (catsel == 2) {
            c1_all = 0;
            for (int y = l0; y < l1; y++)
                if (ROWLIVE[by + y]) { c1_all = 1; break; }
            if (!c1_all) {
                /* MD cells not yet resident (scroll-in lag, scene cuts):
                 * screen tile-rows r-1..r (fine-y) — draw the row */
                int sr = r - (yf ? 1 : 0);
                if ((sr >= 0 && sr < 28 && CAT1_PEND[sr])
                    || (sr + 1 >= 0 && sr + 1 < 28 && CAT1_PEND[sr + 1]))
                    c1_all = 1;
            }
        }
#endif
        uint8_t *drow = DROW(by + l0) - xf;
        /* Mark AFTER the column loop, and only if some tile survived the
         * filters. Marking the range up front looked safe and made the
         * whole scheme worthless: this pass sweeps the FULL SCREEN every
         * cycle for FG cat-1, so an unconditional mark marked every row
         * live and stage A measured 0.1% skippable. Almost every column
         * exits early (no cat-1 bit, code 0, cache miss); only the ones
         * that reach the write matter.
         * Row granularity makes this EXACT, not merely conservative: one
         * surviving tile anywhere in the row means the row is not
         * all-zero, whatever the other 40 columns did. */
        int drew = 0;
#ifdef CAT1_MD
        /* column segments: all 41, or only the two edge pairs (0-1 and
         * 39-40: one-generation packet lag on scroll-in). Two ranges
         * through ONE loop body — an in-loop test got unswitched into
         * two copies of the body (+248B of .ramtext, region guard). */
        int cskip = c1_all ? 0 : 37;             /* 2 -> 39 */
#if defined(C1_NOEDGE) || defined(EDGE42)
        /* no FB edge fallback: EDGE42 ships the edge cells a column early
         * (probe C1_NOEDGE measured the win: 1273 vs 1096 ships) */
        for (int c = c1_all ? 0 : 41; c <= 40; c++) {
#else
        for (int c = 0; c <= 40; c++) {
#endif
            if (c == 2)
                c += cskip;
#else
        for (int c = 0; c <= 40; c++) {
#endif
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
                GAME_TILE_REMAP(code);
            const uint8_t *tpx = tile_pixels(code, cpu);
            if (tpx == blank_tile)
                continue;                           /* miss: keep last frame */
            const uint8_t *tp = tpx + l0 * 8;
            uint8_t g = tg[((unsigned)w >> 6) & 0x7F];
            uint8_t base = (uint8_t)((g == 0xFF ? 1 : g) << 3);
            drew = 1;
#ifdef DIRECT_FB
            /* EDGE CLIP: sbuf's 8-byte side borders absorbed the fine-
             * scroll fringe (x = c*8 - xf runs to -7 on the left, 327
             * on the right); the FB has none — those bytes are the
             * NEIGHBOUR ROW's pixels. Only c==0 (xf>0) and c==40 can
             * clip; interior columns keep the unrolled path below. */
            {
                int sx0 = c * 8 - xf;
                if (sx0 < 0 || sx0 > 312) {
                    int ilo = sx0 < 0 ? -sx0 : 0;
                    int ihi = sx0 > 312 ? 320 - sx0 : 8;
                    const uint8_t *tpc = tp;
                    uint8_t *dc = dst;
                    for (int y = l0; y < l1; y++) {
                        for (int i = ilo; i < ihi; i++)
                            if (tpc[i]) dc[i] = (uint8_t)(base + tpc[i]);
                        tpc += 8;
                        dc += DST_STRIDE;
                    }
                    continue;
                }
            }
#endif
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
                dst += DST_STRIDE;
            }
        }
        if (drew)
            for (int y = l0; y < l1; y++)
                RL_MARK(by + y);
    }
}

#ifdef SPR_BAKE
/* LOOP 17 — PRE-DECODED SPRITE FRAMES (tools/bake_sprites.py).
 * The blob is cart rodata: a 2048-slot open-addressed index, then frame
 * records of {u16 rows, u16 flags, u16 bytes, u16 pad, u16 row_off[rows],
 * row payloads}. A row is u16 nsegs then nsegs x {u8 skip, u8 len,
 * u8 pen[len]}. Pens are RAW 1..14 and `base` is added here, which is
 * what lets one baked frame serve every palette pair.
 * The key is what determines the PIXELS and nothing else: sprite data
 * addr, d2 (pitch + flip), bank, unclipped height. Not the colour set,
 * not xpos, not priority.
 * The bake was verified against the live decoder BEFORE this rom
 * existed -- bake_sprites.py renders every row both ways and fails the
 * build on any difference. */
#include "sprbake.h"
extern const uint8_t sprbake_blob[];
/* [0] hits, [1] misses (bakeable, but that frame was never discovered).
 * 0x28FBC is the scrap above the SPRREUSE counters at 0x28FAC -- the two
 * probes do not overlap.
 * The forced-live count (zoomed/gated/shadow) and the baked-row count
 * were BOTH cut for the region guard: they cost ~28 bytes of RAMCODE
 * that the draw loop needed more. Total sprites per cycle already comes
 * from the SPRREUSE probe's [0], so the hit RATE is still recoverable
 * from a paired run; what the bake is WORTH is the handler mean, which
 * costs no bytes at all. */
#define SPRBK ((volatile uint32_t *)0x26028FBC)

/* NOT RAMCODE, and noinline so it cannot drift back in. The region
 * guard here is 432 bytes wide and the draw loop needs all of them; the
 * lookup runs once per SPRITE per strip (~120 calls/cycle) while the
 * draw loop runs once per PIXEL, so this is the half that can afford to
 * fetch from cart rom. Letting gcc choose swung .ramtext by 40 bytes
 * between edits that changed nothing about this function. */
static __attribute__((noinline)) const uint8_t *bake_find(uint16_t addr, uint16_t d2,
                                        unsigned bank, unsigned hgt,
                                        unsigned zm)
{
    /* HEIGHT-TOLERANT (2026-09-01): height is out of the hash and the
     * exact-match — the bake keeps only the tallest variant per
     * (addr,d2,bank) and a shorter draw is a row-PREFIX of it (the
     * caller indexes rows relative to its own top and never reads the
     * record's count). Accept when the baked row count covers the
     * request; a TALLER request than baked stays a miss (a row_off
     * overrun would draw arbitrary memory as pens — the ROUND CLEAR
     * tan-band lesson). */
    uint32_t k = ((uint32_t)addr | ((uint32_t)d2 << 16))
               ^ ((uint32_t)bank << 28) ^ ((uint32_t)zm << 19);
    k *= 0x9E3779B1u;                    /* SH-2 has MUL.L; a 16x16 fold
                                          * clusters -- see the note in
                                          * tools/bake_sprites.py */
    unsigned h = (k >> 18) & SPRBAKE_MASK;
    /* BOUNDED probe: the table is a quarter full and always has an empty
     * slot (bake_sprites.py asserts it), so this terminates on its own --
     * but a corrupt blob must fall back to the live decoder, never spin
     * inside the compose window.
     * Counter-intuitively this is also the SMALLER code: dropping the
     * trip counter for a bare for(;;) grew .ramtext by 24 bytes, because
     * gcc then restructures the loop. Measured, not assumed. */
    for (int p = 0; p < 32; p++) {
        const uint8_t *s = sprbake_blob + 16 + h * SPRBAKE_SLOT_SZ;
        uint32_t off = *(const uint32_t *)(s + 8);
        if (off == 0xFFFFFFFFu) {
#ifdef FLIP_CENSUS
            CEN[13] += 1;                /* MISS: falls back to the live
                                          * per-pixel ROM decoder */
#endif
            return 0;
        }
        if (*(const uint16_t *)s == addr
            && *(const uint16_t *)(s + 2) == d2
            && (*(const uint16_t *)(s + 4) >> 8) == bank
            && (*(const uint16_t *)(s + 4) & 0xFF) >= hgt
            && *(const uint16_t *)(s + 6) == zm) {
#ifdef FLIP_CENSUS
            CEN[13] += 0x10000;          /* baked frame HIT (high half) */
#endif
            return sprbake_blob + off;
        }
        h = (h + 1) & SPRBAKE_MASK;
    }
    /* THE TRIP BOUND MUST RETURN A MISS. Without this line the function
     * falls off the end and hands back whatever is in r0; the caller
     * uses it as a frame pointer and draws arbitrary memory as pens.
     * That is exactly what it did: a tan band of foreign art down the
     * right of the ROUND CLEAR screen, ares savestate 9. gcc said so at
     * the time -- "control reaches end of non-void function" -- and the
     * warning went past in the build noise. A long probe chain only
     * happens on a MISS, so attract (every frame discovered, zero
     * misses) never touched it and neither MAME nor the parity statics
     * could see it. */
    return 0;
}
#endif

#ifdef MD_SPR
/* ============== P3 M2: MD-VDP MOB OFFLOAD (docs/design/P3.md) ==============
 * Claim pass + SAT build, master, once per fresh record landing (the
 * harvest fill site). Claimed records get w2 bit13 (unread by every
 * other consumer — recon 2026-08-28) and never touch compose or the
 * FB; the MD VDP renders them from the baked art at VRAM 0x8000.
 *
 * Scratch lives in FBCLEAR's old block (0x3A300, 896B) — DEAD under
 * DIRECT_FB, whose stage 2 compiled out its only users. SAT image
 * 512B at 0x3A300, palette block 64B at 0x3A500; both published to
 * the FB packet hole (0x1EDC0 pal / 0x1EE00 SAT — the 576B left in
 * the 2KB hole after md_pkt B ends at 0x1EDC0) every window, and the
 * 68K DMAs them beside its existing consumes at FM=0.
 *
 * v1 claim rule: native (w5&0x3FF==0), pp==2 ONLY (94.6% of the
 * arcade sprite mix by census; pp=3-over-FG-cat1 cannot be expressed
 * MD-side), key baked, palette-coherent (one MD line: the frame's
 * anchor set, other sets claim only if their live 14 pens are
 * IDENTICAL — accuracy before speed), X>0 (the x==0 SAT mask trick),
 * caps 20 records / 64 SAT entries. SAT emitted in REVERSE record
 * order: compose paints later-over-earlier, MD shows earlier-link-
 * in-front. */
#include "md_sprart.h"
/* MDSPR ON CANONICAL (2026-08-29): the offload is architecture-
 * independent — only its SCRATCH HOME was DIRECT_FB-specific (it
 * reused FBCLEAR's block, which is live under the blit). Canonical
 * scratch sits in the audited 28D80-28FFF free span (win_pend takes
 * 0x28D80; the REBUILD-era squatters start at 0x28F20). SAT capped
 * at 32 entries BOTH ways — real claims run 8-12 (STALE: MEASURED at
 * 1.0 claims per GENERATION on the level-1 script, 2026-09-10,
 * LOOP29 119 -- 1.3%% of the MD VDP's 80-sprite capacity. Neither cap
 * binds; the palette-coherence rule does) (zombies are 2
 * subsprites each); the 68K DMAs 128 words and VDP entries 32-79
 * stay behind the link-0 terminator. Under canonical every claimed
 * record is TRIPLE leverage: compose shrinks (the deferral tears),
 * its sbuf rows stay zero so DIRTY_ROW's whole-row exit skips them
 * (the window tax), and there is NO staleness hazard — sbuf + the
 * blit keep re-shipping the last coherent frame exactly as always. */
#define MDSPR_NSAT 32
#ifdef DIRECT_FB
#define MDSPR_SAT ((volatile uint16_t *)0x0603A300)   /* FBCLEAR's block */
#define MDSPR_PAL ((volatile uint16_t *)0x0603A500)
#define MDSPR_CNT ((volatile uint32_t *)0x2603A520)
#else
#if defined(ROWSTALE_PROBE)
#error "MD_SPR canonical scratch overlays ROWHASH - build them separately"
#endif
#define MDSPR_SAT ((volatile uint16_t *)0x06028D00)   /* 32 x 4 words;
                                              * ROWHASH's probe-only span
                                              * (0x28D00-0x28EBF). First
                                              * cut used 0x28EC0 for the
                                              * palette block and SMASHED
                                              * md_dirty (0x28EC0-0x28F3F,
                                              * LIVE) - garbage NT cells,
                                              * collision #12. Audit the
                                              * map comment at :636; it
                                              * still says this hole is
                                              * free and it is NOT. */
#define MDSPR_PAL ((volatile uint16_t *)0x06028E00)   /* 16 words */
#define MDSPR_CNT ((volatile uint32_t *)0x26028E20)   /* [0] SAT entries,
                                              * [1] claims — uncached,
                                              * ares-dump-read */
#endif

static int mdspr_pal_equal(unsigned s, unsigned a)
{
    const volatile uint16_t *ps = PAL_SH + 1024 + s * 16;
    const volatile uint16_t *pa = PAL_SH + 1024 + a * 16;
    for (int p = 1; p <= 14; p++)
        if (ps[p] != pa[p])
            return 0;
    return 1;
}

/* PER-SCENE MDSPR (v3, docs/design/BOSSFIGHT.md): each scene owns an art blob,
 * key slice, and anchor. Scene follows the PALSTATIC detect; a
 * switch posts 0xBA50|scene on COMM8 (the heal-channel pattern) so
 * the 68K re-uploads VRAM 0x8000 from the scene's cart blob, and
 * claims SUSPEND for 30 vints while the chunked upload runs
 * (unclaimed records compose SH-2 = status quo, never torn art).
 * The boss's anchor is DYNAMIC between its two sets (the silhouette
 * phases flip 0x23/0x24 mid-fight): sustained-majority hysteresis
 * only — the per-frame anchor flap desynced SAT/CRAM generations
 * (banked bug, see the STATIC-anchor comment below). */
/* state bytes in the MDSPR scratch tail (after MDSPR_CNT's 8 bytes;
 * uncached = ares-dump-readable, and the SDRAM .bss region is FULL —
 * the guard tripped at +8 bytes) */
#define mdspr_scene    (*(volatile uint8_t *)0x26028E28)
#define mdspr_sus      (*(volatile uint8_t *)0x26028E29)
#define mdspr_post     (*(volatile uint8_t *)0x26028E2A)
#define mdspr_danchor  (*(volatile uint8_t *)0x26028E2B)
#define mdspr_flip_run (*(volatile uint8_t *)0x26028E2C)

#ifdef MDSPR_WHY
/* MDSPR REJECTION CENSUS (2026-09-10, LOOP29 119). Claims measured at
 * 1.0 record/generation against a cap of 20 and MD hardware capacity of
 * 80 — 1.3% of the chip. Five rules can reject a claim and they need
 * different fixes, so count them. In .bss deliberately: the 0x28Fxx
 * scratch span is crowded and this repo has numbered its slot
 * collisions to #15; the linker cannot collide. Read the symbol address
 * out of rom/s16.lst. PROBE ONLY. */
static volatile uint32_t mdspr_why[10];
static volatile uint32_t mdspr_nokey_set[64];  /* which colour sets lack
                                          * baked art (LOOP29 119) */   /* volatile: nothing READS this
                                          * array, so -O2 -flto dead-store
                                          * eliminated 8 of the 10 counters
                                          * and the census read all-zero
                                          * (LOOP29 119). */
#endif
__attribute__((noinline)) static void mdspr_claim(void)
{
    uint8_t crec[24], ckey[24];
    unsigned nclaim = 0, nsat = 0;
    uint8_t setn[64];               /* key-matched records per set,
                                     * dynamic-anchor leader election */
    const mdspr_scene_t *sc9;
    unsigned anchor;
#ifdef PAL_STATIC
    {
        unsigned want = (pscene_cur == 1) ? 1u : 0u;
        if (want != mdspr_scene) {
            mdspr_scene = (uint8_t)want;
            mdspr_sus = 30;
            mdspr_post = (uint8_t)(0x50 | want);
            mdspr_danchor = 0;           /* re-derive from new table */
        }
    }
#endif
    sc9 = &mdspr_scenes[mdspr_scene];
    if (!mdspr_danchor)
        mdspr_danchor = (sc9->anchor != 0xFF)
                        ? sc9->anchor
                        : 0xFF;          /* wildcard scene: no anchor
                                          * until the leader election
                                          * sees live records */
    anchor = mdspr_danchor;
#ifdef MDSPR_TOP
    /* LOOP29 132. The scene table PINS the normal scene's anchor to set
     * 0x09, so MD CRAM line 0 always holds 0x09's palette -- and the
     * band census says 0x09 carries 3 records at f3000 while set 0
     * carries 7. Line 0 is being spent on the wrong palette. Run the
     * leader election in EVERY scene, not just the wildcard boss one.
     * The decompile thread's record-count metric (LOOP-DECOMPILE 7-9) is
     * what makes this the right question: the top palettes by RECORD
     * COUNT cover 65-75% of what is drawn. */
    unsigned dynamic = 1;
#else
    unsigned dynamic = (sc9->anchor == 0xFF);
#endif
    if (dynamic)
        for (unsigned z = 0; z < 64; z++)
            setn[z] = 0;
    if (mdspr_sus) {
        mdspr_sus--;
        MDSPR_SAT[0] = MDSPR_SAT[1] = MDSPR_SAT[2] = MDSPR_SAT[3] = 0;
        return;                          /* upload in flight: all SH-2 */
    }
    for (unsigned i = 0; i < 64; i++) {
        volatile uint16_t *e = SPR_SNAP + i * 8;
        uint16_t d2 = e[2];
        if (d2 & 0x8000)
            break;
#ifdef MDSPR_WHY
        mdspr_why[9]++;                      /* live records examined */
#endif
        if (d2 & 0x4000) {
#ifdef MDSPR_WHY
            mdspr_why[2]++;                  /* d2 bit14 set */
#endif
            continue;
        }
        if (e[5] & 0x3FF) {
#ifdef MDSPR_WHY
            mdspr_why[0]++;
#endif
            continue;                        /* zoomed: SH-2 forever */
        }
        uint16_t d4 = e[4];
        if (((d4 >> 6) & 3) != 2) {
#ifdef MDSPR_WHY
            mdspr_why[1]++;
#endif
            continue;                        /* pp==2 only (v1) */
        }
        unsigned top = e[0] & 0xFF, bot = e[0] >> 8;
        if (top >= bot) {
#ifdef MDSPR_WHY
            mdspr_why[1]++;                  /* degenerate top>=bot */
#endif
            continue;
        }
        if ((e[1] & 0x1FF) < 57) {
#ifdef MDSPR_WHY
            mdspr_why[3]++;
#endif
            continue;                        /* X<=0 = SAT mask trick */
        }
        unsigned set = d4 & 0x3F;
#ifdef GAME_ALTBEASTJ
        /* baked keys carry the US bank numbering; fold the game's bank
         * field the same way the art fetch does (GAME_SPR_BANK) */
        unsigned bank = GAME_SPR_BANK((d4 >> 8) & 0xF);
#else
        unsigned bank = (d4 >> 8) & 0xF;    /* raw 4-bit field, as baked */
#endif
        int ki = -1;
        for (unsigned j = sc9->key0; j < (unsigned)(sc9->key0
                                                    + sc9->nkeys); j++) {
            const mdspr_key_t *mk = &mdspr_keys[j];
            if (mk->addr == e[3] && mk->d2lo9 == (d2 & 0x1FF)
                && mk->bank == bank && mk->height == (uint8_t)(bot - top)
                && (mk->set == set || mk->set == 0xFF)) {
                /* 0xFF = SET-AGNOSTIC key (boss heads: the game
                 * marches the set number as the phase counter) */
                ki = (int)j; break;
            }
        }
        if (ki < 0) {
#ifdef MDSPR_WHY
            mdspr_why[4]++;
            mdspr_nokey_set[set & 0x3F]++;
#endif
            continue;
        }
        if (dynamic)
            setn[set]++;
        /* STATIC anchor (bug found on ares, first M2 run): a per-frame
         * "first claimable record" anchor FLAPS with list order and
         * desyncs SAT/CRAM generations. Line 0 permanently tracks
         * MDSPR_ANCHOR's palette (v2: 0x09, the zombies — the class
         * resident through gameplay; the v1 scenery classes claimed
         * ZERO records across the whole 900-1600 stretch). Other sets
         * claim only while their live pens are IDENTICAL. */
        if (anchor > 0x3F) {
#ifdef MDSPR_WHY
            mdspr_why[5]++;
#endif
            continue;                    /* no anchor yet: count only */
        }
        if (set != anchor && !mdspr_pal_equal(set, (unsigned)anchor)) {
#ifdef MDSPR_WHY
            mdspr_why[6]++;
#endif
            continue;
        }
        if (nclaim >= 20 || nsat + mdspr_keys[ki].nsub > MDSPR_NSAT) {
#ifdef MDSPR_WHY
            mdspr_why[7]++;
#endif
            continue;                        /* caps: overflow stays SH-2 */
        }
#ifdef MDSPR_WHY
        mdspr_why[8]++;                      /* CLAIMED */
#endif
        crec[nclaim] = (uint8_t)i;
        ckey[nclaim] = (uint8_t)ki;
        nclaim++;
        nsat += mdspr_keys[ki].nsub;
    }
    /* DYNAMIC ANCHOR, N-ary (the 0x22 lesson: the boss cycles >=3
     * palette sets and a phase my census hadn't sampled left claims
     * at ZERO for the whole fight): leader election over key-matched
     * records. Switch when a leader owns >=3 records while the
     * anchor owns none, held 5 consecutive passes — the banked flap
     * bug was LIST-ORDER oscillation with two sets concurrently
     * live; a clean phase change is safe to follow fast because SAT
     * and the palette block ship in the same consume. */
    if (dynamic) {
        unsigned lead = anchor, ln = 0;
        unsigned an = (anchor <= 0x3F) ? setn[anchor] : 0;
        for (unsigned z = 0; z < 64; z++)
            if (setn[z] > ln) { ln = setn[z]; lead = z; }
#ifdef MDSPR_TOP
        /* MARGIN switch instead of "the anchor owns NOTHING". The old
         * rule only followed a leader once the incumbent was dead, which
         * is right for a boss phase change and useless when two sets are
         * concurrently live -- exactly the normal-scene case. Require a
         * 2-record margin held 5 passes; LOOP29 130 measured the winning
         * trio changing once in 19 consecutive frames, so the flap risk
         * this guard exists for is small and the hysteresis covers it. */
        if (lead != anchor && ln >= an + 2) {
            if (++mdspr_flip_run >= 5) {
                mdspr_flip_run = 0;
                mdspr_danchor = (uint8_t)lead;
            }
        } else {
            mdspr_flip_run = 0;
        }
#else
        if (lead != anchor && ln >= 3 && an == 0) {
            if (++mdspr_flip_run >= 5) {
                mdspr_flip_run = 0;
                mdspr_danchor = (uint8_t)lead;
            }
        } else
            mdspr_flip_run = 0;
#endif
    }
    /* SAT image, reverse record order, link = physical successor */
    unsigned s2 = 0;
    for (int c = (int)nclaim - 1; c >= 0; c--) {
        volatile uint16_t *e = SPR_SNAP + crec[c] * 8;
        const mdspr_key_t *mk = &mdspr_keys[ckey[c]];
#ifndef MDSPR_DOUBLE
        /* MDSPR_DOUBLE (bisect aid): leave records unmarked so compose
         * ALSO draws them — isolates MD-side defects from compose-skip
         * side effects (erase spans, pair churn). NEVER SHIP. */
        e[2] |= 0x2000;                      /* claimed: compose skips */
#endif
        int sx = (int)(e[1] & 0x1FF) - 184;
        unsigned top = e[0] & 0xFF;
        for (unsigned u = 0; u < mk->nsub; u++) {
            const mdspr_sub_t *sb = &mdspr_subs[mk->sub0 + u];
            unsigned x9 = (unsigned)(sx + 128 + sb->dx);
            if (x9 > 511)
                continue;                    /* off right edge: 9-bit X */
            volatile uint16_t *w = MDSPR_SAT + s2 * 4;
            w[0] = (uint16_t)(top + 128 + sb->dy);
            w[1] = (uint16_t)(((uint16_t)sb->size << 8) | (s2 + 1));
#ifdef CAT1_MD
            w[2] = (uint16_t)(MDSPR_TILE0 + sb->tile);   /* pri 0: under
                                                          * plane-A cat1 */
#else
            w[2] = (uint16_t)(0x8000u | (MDSPR_TILE0 + sb->tile));
#endif
            w[3] = (uint16_t)x9;
            s2++;
        }
    }
    if (s2)
        MDSPR_SAT[(s2 - 1) * 4 + 1] &= 0xFF00;   /* last link = 0 */
    else {
        MDSPR_SAT[0] = MDSPR_SAT[1] = MDSPR_SAT[2] = MDSPR_SAT[3] = 0;
    }
    /* palette block: the anchor set's live pens,
     * quantized exactly like the BG pens (mdp_quant + CRAM
     * expansion), shipped every frame so fades track. Entry 15
     * stays 0 (pen 15 never draws). */
    if (anchor <= 0x3F) {
        MDSPR_PAL[0] = (uint16_t)s2;         /* debug: entries live */
        for (int p = 1; p <= 14; p++) {
            uint16_t q = mdp_quant(PAL_SH[1024 + anchor * 16 + p]);
            MDSPR_PAL[p] = (uint16_t)((((q >> 6) & 7) << 9)
                                      | (((q >> 3) & 7) << 5)
                                      | ((q & 7) << 1));
        }
        MDSPR_PAL[15] = 0;
    }
    /* Counters live in the scratch block's own tail, NOT DIAG — both
     * attempts collided ([53]/[54] = md_tag/pages, [10]/[11] =
     * diag_add band-phase ticks; minefield hits #10 and #11). */
    MDSPR_CNT[0] += s2;                      /* SAT entries shipped */
    MDSPR_CNT[1] += nclaim;                  /* records claimed */
}
#endif /* MD_SPR */

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
#ifdef MD_SPR
        if (d2 & 0x2000)                     /* claimed: the MD VDP is
                                              * rendering this record */
            continue;
#endif
        if ((d2 & 0x4000) || top >= bottom)
            continue;
        int xpos = e[1] & 0x1FF;
        int flip = d2 & 0x100;
        int pitch = (int8_t)(d2 & 0xFF);
        uint16_t addr = e[3];
        uint16_t d4 = e[4], d5 = e[5];
        const uint16_t *sd = altbeast_sprites + GAME_SPR_BANK((d4 >> 8) & 0xF) * 0x10000;
        /* sprite pixel shows iff (1 << pp) > tile level (segas16b_v).
         * pp=2 is exact via the layer ORDER (no reads); only pp<=1
         * sprites gate per pixel (they hide behind BG-cat1/FG-cat0).
         * pp=3 approximated as pp=2, occurrences counted. */
        uint8_t pp = (uint8_t)((d4 >> 6) & 3);
        uint8_t thr = (uint8_t)(1u << pp);
#ifdef DIRECT_FB
        /* READ-FREE COMPOSE (2026-08-26): FM_TEST convicted FM=0 FB
         * reads — 1507 mismatch vs 176 match — and the 68K owns FM=0
         * spans for its FB staging, so ANY dst read mid-compose can
         * see garbage (the black shadow patches). Under MDBGALL the
         * pp<=1 gate's dst read is VESTIGIAL anyway: when sprites
         * draw, the FB holds only through-zeros and earlier sprites
         * (cat0/BG live on the MD; cat1/text draw AFTER and cover),
         * so pri_lut[dst] is always level 0 and the gate always
         * passes. Ungate; order enforces the priority. */
        int gated = 0; (void)pmax;
#else
        int gated = (pp <= 1) && (thr <= pmax);
#endif
        int shad = 0;
#ifdef FLICK_FUSE
        uint8_t flv = flick_lvl[i];      /* 0 = normal; r = stipple r/8 */
#else
        enum { flv = 0 };
#endif
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
#ifdef SHADOW_SILH_OLD
            shad = !shadow_dirty && (bottom - top) <= 48;
#else
            /* STALE-LUT DARKENING (2026-08-26, Mike's frames 135/701):
             * the !shadow_dirty term made every shadow record flash
             * SOLID BLACK for the whole 64-chunk LUT rebuild — and the
             * pair steals repaint pens often enough that the rebuild
             * is nearly always in flight (pen drift 1420->3435), so
             * the "black band" across Zeus flickered in and out. A
             * STALE entry darkens toward a slightly old colour —
             * bounded, transient, and invisible next to a solid black
             * bar. Silhouette now only for the HUGE actors the per-
             * pixel budget cap always excluded. */
#ifndef SHAD_CAP
#define SHAD_CAP 48
#endif
            shad = (bottom - top) <= SHAD_CAP;
#endif
        } else {
            uint8_t pr = spr_pair_rd[par][d4 & 0x3F];
#ifdef DRAW_ADOPT
            /* LOOP 27 entry 6 option 1c: the per-parity map is a cache of
             * pr_key (the ownership truth, one byte per pair). At one
             * cycle per vint the slave's draw meets maps that lag the
             * claim; when the map says none, ask the truth (uncached,
             * 14 compares, only for unmapped records). A set with a pair
             * anywhere draws with it; only a pairless set ramps. */
            if (pr == 0xFF) {
                const volatile uint8_t *pk = (const volatile uint8_t *)0x260283C0;
                for (int t = 1; t < 15; t++)
                    if (pk[t] == (uint8_t)(d4 & 0x3F)) { pr = (uint8_t)t; break; }
            }
#endif
#ifdef SPR_LATE
            if (pr == 0xFF) {
                SPRLATE[3]++;                /* drew in the shadow ramp */
                /* LOOP 27 q4 play-pass census (lean slots 4-7, 0x3A7E8..):
                 * WHO draws in the ramp and WHY. [4] on the slave (SP in
                 * its 0x3F800-0x40000 stack), [5] the set already owns a
                 * pair in pr_key (a stale map, adoptable at draw time),
                 * ([6]/[7] belong to the claim-failure census below). */
                {
                    uint32_t sp_;
                    __asm__ __volatile__("mov r15,%0" : "=r"(sp_));
                    if ((sp_ & 0x000FFFFFu) >= 0x0003F800u)
                        SPRLATE[4]++;
                    /* PARITY TEST: does the OTHER parity's map (uncached)
                     * hold a pair for this set right now? [5] */
                    if (((volatile uint8_t (*)[64])0x2603F800)[par ^ 1][d4 & 0x3F] != 0xFF)
                        SPRLATE[5]++;
                    if (BMT_DONE[par & 1] != BMT_AT_CLAIM[par & 1])
                        SPRLATE[6]++;        /* map rebuilt AFTER this cycle's claim */
                }
            }
#endif
#ifdef DIRECT_FB
            /* UNMAPPED = SKIP, not ramp (2026-08-26): under DIRECTFB
             * the late-claim ALWAYS lands (no_pair 0 across the whole
             * battery), so an unmapped set is a ONE-frame condition.
             * One frame of absence is invisible; one frame of shadow-
             * ramp black is Mike's blob. The ramp fallback made sense
             * when misses could be sustained — they cannot any more. */
            if (pr == 0xFF)
                continue;
#endif
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
#ifdef SPR_LINE_PROBE
        {
            int w = pitch < 0 ? -pitch : pitch;
            w *= 4;
            if (w > 320) w = 320;
            unsigned md = ((unsigned)w + 31) >> 5;
            unsigned cs = d4 & 0x3F;
            sl_set[cs >> 5] |= 1u << (cs & 31);
            {
                int sp0 = top < 0 ? 0 : top / 28;
                int sp1 = (bottom - 1) / 28;
                if (sp1 > SL_SPANS - 1) sp1 = SL_SPANS - 1;
                for (int sp = sp0; sp <= sp1; sp++)
                    sl_span[sp][cs >> 5] |= 1u << (cs & 31);
            }
            if (gated || shad || (vzoom | hzoom))
                SLC[5]++;                    /* must stay on the 32X */
            else
                sl_seteg[cs >> 5] |= 1u << (cs & 31);
            for (int yy = top; yy < bottom; yy++) {
                if ((unsigned)yy >= 224) continue;
                SL_SPR(yy)++;
                SL_PX(yy) = (uint16_t)(SL_PX(yy) + w);
                SL_MD(yy) = (uint16_t)(SL_MD(yy) + md);
            }
        }
#endif
#ifdef SPR_BAKE
        /* BAKED FAST PATH. Taken only for what the bake reproduces
         * EXACTLY: native zoom (a scaled strip is not a copy of
         * anything), ungated (pp<=1 tests the destination pixel per
         * pen) and not the darkening shadow path (that READS the
         * destination). Everything else falls through to the live
         * decoder, untouched. The silhouette form of a shadow sprite
         * (shad==0, base 15<<4) IS an ordinary draw, so it qualifies. */
        if (!gated && !shad && !flv) {
            /* SCALEBAKE (2026-09-01): the zoom==0 gate is GONE — the
             * bake now carries pre-scaled frames keyed by the record's
             * zoom field (the boss barrage: one art, 15 quantized
             * steps), and the run drawer below is zoom-agnostic. A
             * miss falls through to the live paths exactly as before,
             * so unbaked zoom levels cost one probe. */
            unsigned otop = d0 & 0xFF;
            const uint8_t *fr = bake_find(e[3], d2, GAME_SPR_BANK((d4 >> 8) & 0xF),
                                          (unsigned)(d0 >> 8) - otop,
                                          (unsigned)d5 & 0x3FF);
            if (fr) {
                const uint8_t *rt = fr + 8;
                SPRBK[0]++;
                for (int y = top; y < bottom; y++) {
                    RL_MARK(8 + y);
                    const uint8_t *sp = fr + *(const uint16_t *)
                                        (rt + ((unsigned)(y - (int)otop) << 1));
                    unsigned ns = *(const uint16_t *)sp;
                    uint8_t *row = DROW(8 + y);
                    int x = xpos;
                    sp += 2;
                    while (ns--) {
                        x += *sp++;
                        unsigned n = *sp++;
                        /* clip the run ONCE instead of testing every
                         * pen: only 184 <= x < 504 is drawn, and that
                         * is the same bound the live path reaches by
                         * testing sx < 320 per pixel (blit_half ships
                         * 320 columns; 320..327 is row pad nobody
                         * displays or reads). */
                        int lo = x < 184 ? 184 : x;
                        int hi = (int)((unsigned)x + n);
                        if (hi > 504)
                            hi = 504;
                        if (hi > lo) {
                            const uint8_t *s = sp + (lo - x);
                            uint8_t *d = row + (lo - 184);
                            int m = hi - lo;
                            do {
                                *d++ = (uint8_t)(base + *s), PENTAP(e[4], *s), s++;
                            } while (--m);
                        }
                        sp += n;
                        x += (int)n;
                    }
                }
                continue;
            }
            SPRBK[1]++;
        }
#endif
        for (int y = top; y < bottom; y++) {
            RL_MARK(8 + y);
            addr = (uint16_t)(addr + pitch);
            yacc = (uint16_t)(yacc + (vzoom << 10));
            if (yacc & 0x8000) {
                addr = (uint16_t)(addr + pitch);
                yacc &= 0x7FFF;
            }

            uint8_t *row = DROW(8 + y);
            const uint8_t *urow = DROW_U(8 + y); /* gate reads (rare:
                                              * pp<=1) — DIRECT_FB reads
                                              * dst through the UNCACHED
                                              * FB alias (a cached read
                                              * could answer with the
                                              * other bank's line) */
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
                        if (sx < 320 && pix != 0 && pix != 15) {            \
                            /* THROUGH pixels have no colour here to       \
                             * darken (they are the MD's, on the other    \
                             * chip) — solid-filling them was the black   \
                             * blob over the temple (Mike 2026-08-26).    \
                             * STIPPLE instead: half the pixels dark,     \
                             * half stay MD — the arcade's own mist is a  \
                             * dither, and at CRT scale 50% dark reads    \
                             * as darkening.                               \
                             * DIRECT_FB (2026-08-26): NO dst read at    \
                             * all — FM=0 FB reads return garbage        \
                             * (FM_TEST 1507 mismatch) and were the      \
                             * black shadow patches. Unconditional       \
                             * stipple; over sprites it dithers 50%      \
                             * dark, which at CRT scale reads as the     \
                             * arcade's darkening. */                     \
                            DFB_SHADOW_PIX(sx)                              \
                        }                                                   \
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
            } else if (hzoom == 0 && !gated && !flv) {  /* gated (pp<=1)
                                              * sprites take the scalar
                                              * path below — the gate (and
                                              * the stipple) stays out of
                                              * the unrolled fast macros */
                /* 1:1 paths. NIB draws one nibble; when the sprite starts
                 * on-screen (xpos >= 184) the sx<320 test is implied by
                 * the x<504 loop bound — the NC variants drop it. */
#define NIB(PIX_EXPR)                                                       \
                    pix = (PIX_EXPR);                                       \
                    { unsigned sx = (unsigned)(x - 184);                    \
                      if ((unsigned)(pix - 1) < 14u && sx < 320)            \
                          row[sx] = (uint8_t)(base + pix), PENTAP(d4, pix); }                 \
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
                            row[sx] = (uint8_t)(base + pix), PENTAP(d4, pix);                \
                        x++;                                                \
                    }
#define ZNIB_G(PIX_EXPR)                                                    \
                    pix = (PIX_EXPR);                                       \
                    xacc = (xacc & 0x3F) + hzoom;                           \
                    if (xacc < 0x40) {                                      \
                        unsigned sx = (unsigned)(x - 184);                  \
                        if (sx < 320 && pix != 0 && pix != 15               \
                            && thr > pl[urow[sx]])                          \
                            row[sx] = (uint8_t)(base + pix), PENTAP(d4, pix);                \
                        x++;                                                \
                    }
#ifdef FLICK_FUSE
#define ZNIB_F(PIX_EXPR)                                                    \
                    pix = (PIX_EXPR);                                       \
                    xacc = (xacc & 0x3F) + hzoom;                           \
                    if (xacc < 0x40) {                                      \
                        unsigned sx = (unsigned)(x - 184);                  \
                        if (sx < 320 && pix != 0 && pix != 15               \
                            && st[(sx + phx) & 3] < lv2)                    \
                            row[sx] = (uint8_t)(base + pix), PENTAP(d4, pix);                \
                        x++;                                                \
                    }
                if (flv) {
                    /* synthesized alpha: dither density carries the
                     * arcade's presence duty (gate dropped if both
                     * apply — flicker class is pp=2 in practice).
                     * phx shifts the pattern one column on alternate
                     * windows: the drawn set alternates instead of
                     * standing (screen-door -> shimmer, the closest
                     * 30Hz gets to the arcade's 60Hz flash) */
                    const uint8_t lv2 = (uint8_t)(flv << 1);
                    const unsigned phx = (unsigned)par & 1u;
                    const uint8_t *st = flick_bay + (((unsigned)y & 3) << 2);
                    while (((xpos - x) & 0x1FF) != 1) {
                        uint16_t w = sd[o];
                        o = (uint16_t)(flip ? o - 1 : o + 1);
                        if (!flip) {
                            ZNIB_F((w >> 12) & 0xF)
                            ZNIB_F((w >> 8) & 0xF)
                            ZNIB_F((w >> 4) & 0xF)
                            ZNIB_F(w & 0xF)
                        } else {
                            ZNIB_F(w & 0xF)
                            ZNIB_F((w >> 4) & 0xF)
                            ZNIB_F((w >> 8) & 0xF)
                            ZNIB_F((w >> 12) & 0xF)
                        }
                        if (pix == 15)
                            break;
                        if (x >= 504)
                            break;
                    }
                } else
#endif
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
#ifdef FLICK_FUSE
#undef ZNIB_F
#endif
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
            uint8_t *dst = DROW(8 + row * 8) + (col - 24) * 8;
            for (int y = 0; y < 8; y++) {
                RL_MARK(8 + row * 8 + y);
                if (tp[0]) dst[0] = (uint8_t)(base + tp[0]);
                if (tp[1]) dst[1] = (uint8_t)(base + tp[1]);
                if (tp[2]) dst[2] = (uint8_t)(base + tp[2]);
                if (tp[3]) dst[3] = (uint8_t)(base + tp[3]);
                if (tp[4]) dst[4] = (uint8_t)(base + tp[4]);
                if (tp[5]) dst[5] = (uint8_t)(base + tp[5]);
                if (tp[6]) dst[6] = (uint8_t)(base + tp[6]);
                if (tp[7]) dst[7] = (uint8_t)(base + tp[7]);
                tp += 8;
                dst += DST_STRIDE;
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
/* 2026-09-02: cart ROM, not .ramtext — gap work, chunked, off the
 * window path (256B freed for EDGE42; region guard). */
static void shadow_lut_chunk(void)
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
#ifdef PG_STICKY
/* Fixed scrap 0x28F40-4F (region guard: CUT_BLANK+PG_STICKY together
 * pushed .bss 16 bytes over 0x19000). NOT zeroed like .bss — boot
 * inits explicitly next to pg_watch. */
#define pg_quiet ((uint8_t *)0x06028F40)     /* [13] consecutive quiet
                                              * captures of a watched page;
                                              * watch drops at 12 (a
                                              * pointer-load mark can
                                              * precede the stream's writes
                                              * by cycles — one quiet
                                              * capture is not proof the
                                              * stream ended) */
#define pg_deep (*(uint8_t *)0x06028F4D)     /* cycles left of deep watch:
                                              * a broad mark (>=8 pages =
                                              * the loaders/clear-alls that
                                              * mark ALL at pointer-load)
                                              * pins every watch for 24
                                              * cycles — scene loads write
                                              * their last pages seconds
                                              * after the one mark they
                                              * ever post */
#endif
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
static uint16_t cycle_dirt;                  /* pages the game wrote into the
                                              * CURRENT draw bank since the
                                              * last flip (file-scope: the k2
                                              * flip span reads it, and LOOP24
                                              * runs that span from the V-ISR
                                              * under VISR_FLIP). Init at the
                                              * top of m_main, like
                                              * pg_pending. */
#ifdef PG_FRESH
/* PER-BANK RESTORE FRESHNESS (LOOP28 95). restore_pages replays TILEMAP_U
 * truth into the bank the flip just handed us, for cycle_dirt | pg_watch.
 * Measured on the double-buffered line: 2.35 pages per flip, of which
 * cycle_dirt contributes 0.10 — the other 2.25 are pg_watch pages whose
 * TRUTH HAS NOT CHANGED since the last time they were written into this
 * same bank, so the copy writes bytes that are already there. At 18.3
 * scanlines per flip and 964 flips that is the bulk of what double
 * buffering costs.
 *
 * A page is fresh in a bank once restored into it, and stops being fresh
 * in BOTH banks the moment cap_page sees its truth change. That is the
 * whole invariant: nothing else writes TILEMAP_U. */
static uint16_t pg_fresh[2];
#endif
#ifdef PG_SKIP_PKT
/* PACKET HOLES IN THE PAGE TRUTH (LOOP29 137). Two pipeline regions sit
 * inside game tile-RAM pages and are written every vint by the pipeline
 * itself: the R60 packet + publish word at 0x12000-0x1283F (page 0, longs
 * 0x000-0x20F) and MD-plane packet B at 0x1E800-0x1EFFF (page 12, longs
 * 0x200-0x3FF). Captured as truth they keep both pages watched forever
 * (pg_watch read 0x1001 in steady play) and get RESTORED across banks --
 * a stale publish word written into the other bank. Neither is game
 * truth; both are skipped by capture AND restore. */
/* LOOP29 138: the R60 packet moved out of page 0 into page 12's first
 * half (packet_fmt.h). Page 0 is the BACKGROUND plane's page and is
 * captured whole again; page 12 is the blank page, holds only pipeline
 * packets, and is skipped whole -- TILEMAP_U page 12 stays the zeros
 * boot wrote, which is what "blank" means. */
#ifdef PG_KEEP_B
/* LOOP29 139 probe: keep capturing/restoring MD-plane packet B (page 12
 * second half) across banks; skip only the R60 packet in the first half. */
#define PG_LO(pg)  ((pg) == 12 ? 0x200 : 0)
#define PG_HI(pg)  0x400
#else
#define PG_LO(pg)  0
#define PG_HI(pg)  ((pg) == 12 ? 0 : 0x400)
#endif
#else
#define PG_LO(pg)  0
#define PG_HI(pg)  0x400
#endif
RAMCODE static void cap_page(int pg)
{
    volatile uint32_t *src = (volatile uint32_t *)(FB_STAGING + pg * 0x800);
    volatile uint32_t *dst = (volatile uint32_t *)(TILEMAP_U + pg * 0x800);
    uint32_t ch = 0;
    for (int i = PG_LO(pg); i < PG_HI(pg); i += 4) {
        uint32_t v0 = src[i + 0], v1 = src[i + 1];
        uint32_t v2 = src[i + 2], v3 = src[i + 3];
        ch |= v0 ^ dst[i + 0]; dst[i + 0] = v0;
        ch |= v1 ^ dst[i + 1]; dst[i + 1] = v1;
        ch |= v2 ^ dst[i + 2]; dst[i + 2] = v2;
        ch |= v3 ^ dst[i + 3]; dst[i + 3] = v3;
    }
#ifdef ROW_GEN
    if (ch)
        RG_PGCAP |= 1u << pg;            /* master only (flip_span / body) */
#endif
    if (ch) {
#ifdef PG_FRESH
        /* truth moved: neither bank holds it any more */
        pg_fresh[0] &= (uint16_t)~(1u << pg);
        pg_fresh[1] &= (uint16_t)~(1u << pg);
#endif
        pg_watch |= (uint16_t)(1u << pg);
#ifdef PG_STICKY
        pg_quiet[pg] = 0;
#endif
    } else {
#ifdef PG_STICKY
        /* 12, not 3: the scene loaders run ~a second (7+ cycles) and
         * write their pages in sequence — page 0's rows can arrive 5+
         * cycles after the pointer-load mark with the page physically
         * untouched in between (measured: sticky=3 caught the clear
         * but missed the eye-face draw into page 0, 1152 words). */
        if (((pg_watch >> pg) & 1) && !pg_deep) {
            if (++pg_quiet[pg] >= 12) {
                pg_watch &= (uint16_t)~(1u << pg);
                pg_quiet[pg] = 0;
            }
        }
#else
        pg_watch &= (uint16_t)~(1u << pg);
#endif
    }
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
#ifdef VB_SPAN
    CEN[30] += (unsigned)__builtin_popcount(bm & 0x1FFF);  /* pages written */
    CEN[31]++;                                             /* calls */
#endif
    for (int pg = 0; pg < 13; pg++) {
        if (!(bm & (1u << pg)))
            continue;
        volatile uint32_t *src = (volatile uint32_t *)(TILEMAP_U + pg * 0x800);
        volatile uint32_t *dst = (volatile uint32_t *)
            (0x04012000u + (unsigned)pg * 0x1000u);
        for (int i = PG_LO(pg); i < PG_HI(pg); i += 4) {
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
/* DISPLAY GATE (2026-09-05, Mike: "boot screen and transitions slow and
 * littered with tiles"). The arcade hides every tilemap load behind
 * its video-enable bit (port 0xC40001 bit 5): both cuts in ref_arcade
 * are four black frames, then the whole scene. Our pipeline lags the
 * 68K's writes by ~40 vints (page copies + MD art uploads), so honouring
 * the bit alone would still uncover the load in progress. Rule: blank
 * the 32X layer while the game says off, and after it says on keep
 * blanking until nothing is pending (no dirty pages, no dirty art
 * slots) for two vints, capped at DISP_HOLD_MAX. While blanked, the
 * page-copy budget opens to all 13 pages and the art batch to 40 —
 * the blank is spent loading, so it is short. The MD side blanks its
 * planes through VDP reg 1 (md_main.c) at the same bit. */
static volatile uint8_t r60_disp_on;     /* from the packet, bit 15 of word 20 */
static uint8_t r60_seq_prev = 0xFF;      /* lost-push belt v3: last landed push sequence */
static uint8_t disp_blank;               /* 32X layer currently blanked */
static uint8_t disp_settle, disp_hold;
static volatile uint8_t md_rot;          /* name-table walk rotations (9 phases) */
static uint8_t disp_rot_on;              /* md_rot when the game said display-on */
#define DISP_HOLD_MAX 60
/* gate census (state_health reads it): high 16 = blanks entered, low 16
 * = vints held blank AFTER the game said display-on (our lag) */
#define DISP_CENSUS (*(volatile uint32_t *)0x26028F7C)   /* free: after CAT1_PEND[28], before DRQR */

/* DISPLAY GATE decision (ROM-resident: called once per vint from the
 * latch point; keeping it out of RAMCODE bought ARTTAIL its region room) */
__attribute__((noinline)) static void disp_gate(void)
{
#ifdef BOOT_GATEOFF
    /* HARDWARE PROBE: never blank; the SH-2 forces the display on every
     * call. If the screen shows the game, the gate's blank/release logic
     * is the blocker (ours). If still black, the SH-2's mode write does
     * not take on this core. */
    MARS_VDP_DISPMODE = MARS_NTSC_FORMAT | MARS_224_LINES | MARS_VDP_PRIO_32X | MARS_VDP_MODE_256;
    disp_blank = 0; r60_disp_on = 1;
    return;
#endif

    uint16_t base = MARS_NTSC_FORMAT | MARS_224_LINES | MARS_VDP_PRIO_32X;
#ifdef MD_STATIC
    /* PENDING MD TABLES (2026-09-07): the PALSTATIC detect fires once per
     * scene and it fires at the TITLE, where the install is (rightly)
     * refused by the distance rule; the level's palette then FADES IN
     * after the reveal (measured: the hold releases before the distance
     * passes), so the retry runs every 4th vint, blank or not, until the
     * live palette is within TOL of the anchor. The install is selective
     * (only sets whose remap changes re-convert), so doing it after the
     * reveal touches the wrong tiles only. */
    if (mds_loadgap) mds_loadgap--;
    if (pscene_cur != 0xFF && pscene_cur < MDSTATIC_N) {
        if (mds_scene_cur != 0xFF
            && mds_table_of[mds_scene_cur] == mds_table_of[pscene_cur]) {
            mds_scene_cur = pscene_cur;           /* same tables: just track
                                                   * (ONLY when something is
                                                   * installed — the first
                                                   * cut tracked here while
                                                   * the load gap was open
                                                   * and never installed) */
        } else if (!mds_loadgap) {
            static uint8_t mds_tick;
            if ((++mds_tick & 3) == 0) {
                const uint16_t *ap = pscene_pal[pscene_cur];
                unsigned dist = 0;
                for (unsigned i = 0; i < 1024; i++)
                    dist += (PAL_SH[i] != ap[i]);
                if (dist <= MDS_TOL) {
                    mds_install(mds_table_of[pscene_cur], disp_hold);
                    mds_scene_cur = pscene_cur;
                    MDS[5]++;                     /* late installs (retry path) */
                }
            }
        }
    }
#endif
    if (!r60_disp_on) {
        if (!disp_blank) {
            MARS_VDP_DISPMODE = (uint16_t)(base | MARS_VDP_MODE_OFF);
            disp_blank = 1;
            DISP_CENSUS += 0x10000u;     /* blanks entered */
#ifdef MD_STATIC
            mds_flush();
#endif
        }
        disp_settle = 0; disp_hold = 0;
    } else if (disp_blank) {
        /* settle = no page pending and the art backlog within ONE
         * batch ("no dirty art at all" never held on the title's
         * cycling backdrop — every cut ran to the 60-vint cap) */
        /* 2026-09-06: AND the cell walk has gone round twice since the
         * game said display-on. The dirt count only knows about cells
         * the walk has visited; the old gate released after 2 vints
         * with most of a fresh scene unwalked (the title card in three
         * bands, ref screenshots_0905_2312). Rotation 1 claims the
         * scene's tiles, rotation 2 places the cells whose art was
         * pending during rotation 1. */
        int dirt = 0;
        for (int i = 0; i < NSETS * NWAYS / 32; i++)
            dirt += __builtin_popcount(md_dirty[i]);
        if (!disp_hold) {
            disp_rot_on = md_rot;                 /* first vint after display-on */
#ifdef ROW_GEN
            RG_MARK_SPAN(0, 224);                 /* new scene: every FB row
                                                   * recomposes during the hold
                                                   * (uncomposed rows showed the
                                                   * old scene's pixels at release) */
#endif
        }
        /* STALE-PAINT FLUSH (2026-09-06): at the demo release the 32X CRAM
         * groups of the cat1 sets still held the title's paints for ~14
         * vints (ares "32X CRAM" dump at release: groups 2-5 = the intro
         * flash family; the grass band drew black) — the (key,gen) memo
         * hit over a stale paint, the open PAL_SETGEN root. Forget every
         * slot's generation at display-on and again on the first settled
         * vint, so the windows inside the hold repaint every live group. */
        if (!disp_hold || disp_settle == 1)
            for (int ci = 0; ci < 32; ci++) cram_keygen[ci] = 0xFFFF;
        disp_settle = (uint8_t)(((uint8_t)(md_rot - disp_rot_on) >= 2
                                 && !pg_pending && dirt == 0)
                                ? disp_settle + 1 : 0);
        if (disp_hold < 255) disp_hold++;
        if (disp_settle >= 5 || disp_hold >= DISP_HOLD_MAX) {
            MARS_VDP_DISPMODE = (uint16_t)(base | MARS_VDP_MODE_256);
            disp_blank = 0;
            DISP_CENSUS += disp_hold;    /* held vints after display-on */
        }
    }
}

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
/* The #define below is unconditional (harmless: nothing reads PSRC unless
 * the probe is on), so guard on the probe ACTUALLY being enabled. */
#if defined(PICKUP_SRC_PROBE) && defined(NATIVE_FRAME)
#error "PICKUP_SRC_PROBE's PSRC is 0x28F50, the shipping NAT_WALL - \
build the probe without NATIVE"
#endif
#define PSRC ((volatile uint32_t *)0x26028F50)
extern volatile uint8_t slave_in_compose;
/* blit_half runs on BOTH CPUs (the slave owns 0..35, 72..107, 144..183),
 * so the probe must fold master rows only — a slave row would index the
 * table out of range and both CPUs would race the same DIAG words. */
#ifdef ROWSTALE_PROBE
__attribute__((always_inline))
static inline int rowslot(int y)
{
#ifdef WIN_TWO
    /* WIN_TWO master ranges: 56-112 at k1, 168-224 at k2 — exactly 112
     * rows, exactly ROWHASH's size. The THIRDS mapping below predates
     * WIN_TWO and silently sampled only 60 of the master's 112 rows on a
     * WIN2 build (72..107 and 168..183 both fell through to -1), so any
     * WIN2-era staleness figure from this probe was taken on a biased
     * 54% subset. */
    if (y >= 56 && y < 112)  return y - 56;        /* 56..111  -> 0..55   */
    if (y >= 168 && y < 224) return y - 168 + 56;  /* 168..223 -> 56..111 */
    return -1;                                     /* slave row: skip */
#else
    if (y >= 36 && y < 72)   return y - 36;       /* 36..71   -> 0..35  */
    if (y >= 108 && y < 144) return y - 108 + 36; /* 108..143 -> 36..71 */
    if (y >= 184 && y < 224) return y - 184 + 72; /* 184..223 -> 72..111 */
    return -1;                                    /* slave row: skip */
#endif
}
#endif

/* DMAC CHANNEL 1 FOR THIS BLIT IS RETIRED, NOT UNTRIED: built, correct
 * (statics pixel-identical), and 1.77x SLOWER on ares — 47.34 -> 83.95
 * us/row with 14% of rows never raising TE. Retired from the tree rather
 * than left behind a flag that would hand someone a 1.77x-slower rom;
 * docs/log/LOOP.md negative 21 carries the numbers and the two traps (CHCR TS
 * bits, per-CPU DMAOR) so it does not have to be rebuilt to be believed.
 * Likewise BLITUNC: cached and uncached FB writes measure 47.34 vs 47.46
 * on ares, so the write-buffer premise below is false — but the CONCLUSION
 * (blit with CPU stores) is right. */
#ifndef DIRECT_FB
/* REBUILD STAGE 2: compose writes the FB back bank directly, so the
 * whole sbuf->FB ship (blit_half and blit_around, both RAMCODE) is
 * compiled out — the bytes return to the region guard. */
#ifdef R60
#define BQ_INCOMPLETE ((volatile uint32_t *)0x26028FC4)
#endif
#ifdef BLIT_SKIP
RAMCODE static void blit_half(int ylo, int yhi, int bank)
#else
RAMCODE static void blit_half(int ylo, int yhi)
#endif
{
#if defined(BLIT_SKIP) && defined(R60)
    /* audit rotation seed: bumps per call, so each row's sampled
     * group shifts over time — a fixed per-row sample let lying rows
     * whose sampled group was legitimately empty escape BOTH audits
     * forever (pass 12b: band back with lies=0, twice). */
    static uint32_t vseq;
    vseq++;
#endif
#ifdef ROW_DEFER
    /* ROW-DEFER (2026-08-25, the purple conviction — docs/design/REBUILD.md):
     * complete-or-defer AT THE SHIP. A band mid-compose has cleared-
     * not-yet-redrawn sbuf rows; shipping them writes MD-through zeros
     * over the bank's coherent last frame — measured landing at frame
     * 700 of the play2 script as 24 through-rows (slave R2, 144-215,
     * BAND_SHIFT 32) in the displayed bank, visible as the purple band
     * where the MD backstop is plane-B junk (tile 0x12D/pal1) and as
     * lower-third sprite dropouts where it is real content. Skip the
     * row instead: the bank keeps last frame's coherent pixels, one
     * band one frame late — the policy the band queue already applies
     * to whole bands, enforced per row here.
     * NARROW GATE, attempt 3. Attempt 1 deferred on band-open alone:
     * 58.7 rows/cycle — the open span covers most of the frame, a
     * quarter of the screen shipped one frame late, motion smeared.
     * Attempt 2 added an 80-long zero scan per open row: semantics
     * right, but the scan sits on the window critical path — cadence
     * 1.044 -> 1.127, rejects 1.9 -> 9.2%. REGRESSION, out.
     * Attempt 3 is O(1): the hazard state "cleared, nothing redrawn
     * yet" is exactly ROWLIVE==0 — the audited mark state the pass-12c
     * verifier proved honest (5376 -> 0 false claims). Defer iff the
     * row's band is open AND ROWLIVE says unmarked. A legit-empty row
     * defers only while its band is open (one cycle max), then ships
     * zeros as before — no permanent-stale hole. A partially-composed
     * row (marked) ships exactly as it always did: the pre-existing
     * minor tear class, not the purple. Masks are read ONCE per call:
     * a mid-blit completion must not split a band across two frames
     * at a random row. DIAG[29] counts deferred rows. */
    uint16_t rd_s = SYNC[8], rd_m = SYNC[9];
    uint16_t rd_owed = SYNC[10];         /* cat1-owed bands: defer whole */
    /* R2 DEFER BOUND (2026-08-31, Mike's frozen smoke, bs1 caught in
     * the act): the Neff-pillar presentation makes rg2's compose span
     * nearly the whole cycle, so the hazard/owed bit is up at almost
     * EVERY ship — R2 composed fresh (sbuf 68.5% different from the
     * banks) while both banks' R2 rows sat BIT-IDENTICAL for seconds.
     * The gate's precondition ("R2's gap is SHORT") dies under that
     * scene. Bound it: after 4 consecutive ships deferred, ship R2
     * anyway — one grass-less-risk frame (the frame-3035 class, once)
     * beats a frozen band; the short-gap win is preserved (a real
     * short gap spans 1-2 ships and still defers). */
    {
        static uint8_t r2_defer_run;
        if (rd_owed & 4u) {
            if (r2_defer_run < 250) r2_defer_run++;
            if (r2_defer_run >= 4) rd_owed &= (uint16_t)~4u;
        } else
            r2_defer_run = 0;
    }
    /* CENSUS RETIRED (2026-08-30, one battery pair each, Mike's frame
     * 2214 = the 2.4x blink regression): owed-arrivals base 1300/1138
     * vs delta 772/1044 — the chronic owed window did NOT widen;
     * R1 STRIP-PHASE arrivals base 84 vs delta 108 — the uncovered
     * "rows marked, cat1 pending" gap DID. The gate below (bit 3,
     * R2's twin) replaced the counters; its defers land in the R1
     * band counter (0x28FCC). */
#endif
#ifdef BLIT_CHASE
    /* Fence publisher = the MASTER only. This routine is shared .ramtext
     * and the slave runs it for its own half; a second writer left the
     * fence stuck at the slave's last row (measured: 220 half-vint
     * timeouts in 1031 generations). CPU told apart by the stack: the
     * slave's stack lives above 0x3F800, the master's below. */
    uint32_t sp_; __asm__ __volatile__("mov r15,%0" : "=r"(sp_));
    const int pub_ = sp_ < 0x0603F800u;
#endif
#ifdef MHALF_PROBE
    if (ylo >= 112) return;              /* CALIBRATION: master half never
                                          * ships (picture wrong by design) */
#endif
#ifdef SHALF_PROBE
    if (ylo < 112) return;               /* CALIBRATION: slave half never ships */
#endif
#ifdef ROW_GEN
    uint32_t rg_shipl[8];                /* 8 uncached reads, not 224 */
    for (int i = 0; i < 8; i++) rg_shipl[i] = RG_SHIPM[i];
    uint32_t rsp_; __asm__ __volatile__("mov r15,%0" : "=r"(rsp_));
    const int rg_cpu = rsp_ < 0x0603F800u ? 0 : 1;   /* master 0, slave 1 */
    (void)rg_cpu;                        /* only the ROW_DEFER exits use it */
#define RG_DEFER_MARK(yy) (RG_DEFER(rg_cpu, (bank) ? 1 : 0)[(yy) >> 5] |= 1u << ((yy) & 31))
#else
#define RG_DEFER_MARK(yy) ((void)0)
#endif
    for (int y = ylo; y < yhi; y++) {
#ifdef BLIT_CHASE
        if (pub_) SYNC[14] = (uint16_t)y;   /* rows < y shipped */
#endif
#ifdef DIRECT_FB
        /* REBUILD STAGE 1: compose already wrote this row into the FB
         * back bank directly — there is nothing to ship. The loop and
         * the slice command flow (SYNC[4]/[5], FBCLEAR, the bank label)
         * stay wired and inert; the machinery dies whole in stage 2. */
        continue;
#endif
#ifdef ROW_GEN
        if (!RG_SHIP(y)) {
#ifdef ROW_GEN_VERIFY
            /* NEVER SHIP: read the bank back UNCACHED; a skipped row
             * must equal sbuf. DIAG[11] mismatches / DIAG[12] checks. */
            {
                const uint32_t *sv = (const uint32_t *)(sbuf + (8 + y) * SBUF_W + 8);
                volatile uint32_t *ck = (volatile uint32_t *)
                    (0x24000000u + 0x200 + (unsigned)y * 320);
                uint32_t bad = 0;
                for (int i = 0; i < 80; i++)
                    bad |= ck[i] ^ sv[i];
                if (bad) RG_COUNT[1]++;
                RG_COUNT[2]++;
            }
#endif
            continue;
        }
        if (0) continue;                 /* identical in sbuf AND both
                                          * banks: shipping it would
                                          * write the same bytes */
#endif
#ifdef FBROWS_PROBE
        fbp_st[0]++;                     /* rows shipped (probe) */
#endif
#ifdef NOBLIT_PROBE
        continue;                        /* CEILING PROBE: ship no rows at
                                          * all (picture stale by design) —
                                          * does the game reach 60 if the
                                          * FB write cost were zero? */
#endif
#ifdef ROW_DEFER
        {
            unsigned rd_open;
            if      (y < 36 + BAND_SHIFT)      rd_open = rd_s & 1u;
            else if (y < 72)                   rd_open = rd_m & 1u;
            else if (y < 108 + BAND_SHIFT)     rd_open = rd_s & 2u;
            else if (y < 144)                  rd_open = rd_m & 2u;
            else if (y < 184 + BAND_SHIFT_RG2) rd_open = rd_s & 4u;
            else                               rd_open = rd_m & 4u;
            if (rd_open && !ROWLIVE[8 + y]) { DIAG[29]++; RG_DEFER_MARK(y); continue; }
            /* R2 MID-BAND HAZARD GATE (bit 2, Mike's 3035): scoped to
             * the short "rows marked by sprite strips, cat1 not yet"
             * window; the bank keeps last frame's coherent grass.
             * ATTEMPT 5 REVERTED HERE (2026-08-30): the R1 twin
             * (bit 3) gated the same window one band up for Mike's
             * 2214 — and the width-filtered corpus grade read it
             * WORSE (2.33 vs 1.54 ungated vs 0.27 baseline blinks/
             * 1000f): mid-action R1 shows its deferred rows where
             * ground-band R2 hides them, AND the 2214 class turned
             * out to be a one-frame VERTICAL band displacement
             * (mixed compose generations at the seam), which no
             * ship-side defer can fix. The law completes: NO
             * ship-side deferral for R0/R1, either window. Bit 3
             * stays published as telemetry only. */
            if ((rd_owed & 4u) && y >= 144) {
                DIAG[29]++;
                RG_DEFER_MARK(y);
                continue;
            }
#ifdef CAT1_OWED_DEFER
            /* ATTEMPT 4 — MEASURED WORSE, OFF BY DEFAULT (2026-08-29):
             * deferring owed-band rows fired ~14 rows/cycle (the owed
             * window overlaps the ship CHRONICALLY under load, not
             * rarely) and every deferral shows 2-frame-old content —
             * heavy-scene grass-blinks went 1 -> 5 on the consecutive
             * A/B. The attempt-1 failure mode at smaller scale. The
             * cat1-blink fix must come from SCHEDULING (the owed drain
             * cannot still be pending when the ship arrives), not from
             * ship-side deferral. Mask publishing (SYNC[10]) stays as
             * telemetry. */
            if (rd_owed & (uint16_t)((y < 72) ? 1u : (y < 144) ? 2u : 4u)) {
                DIAG[29]++;
                RG_DEFER_MARK(y);
                continue;
            }
#else
            (void)rd_owed;
#endif
        }
#endif
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
            /* LOOP 17: the ROW-level answer, which is the one that
             * decides whether the blit can stop shipping rows at all —
             * `any` was computed and thrown away. Groups tell you what a
             * clever blit could skip inside a row; ROWS tell you whether
             * the whole "ship 320x224 every frame" premise still holds
             * now that MDBGALL moved the background to the MD plane.
             * Scrap block (free on a probe build): [0] rows checked,
             * [1] rows entirely transparent. */
            {
                volatile uint32_t *pr = (volatile uint32_t *)0x26028FBC;
                pr[0]++;
                if (!any) pr[1]++;
            }
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
#ifdef BLIT_SKIP
                /* THE NUMBER THAT ACTUALLY SIZES JOB 2. DIAG[32] above
                 * counts rows unchanged since LAST CYCLE, which
                 * overstates what a real dirty-row blit could skip: we
                 * PAGE-FLIP, so the target bank is TWO cycles stale and
                 * a row is only skippable when THAT BANK already holds
                 * this exact content. Simulate the real scheme — a
                 * per-bank remembered hash, updated only when we write —
                 * and count the skips it would actually take.
                 * [50] opportunities, [51] rows a per-bank dirty scheme
                 * would skip. 0x3A300 is free on a non-BLITSKIP... it is
                 * NOT: FBCLEAR lives there. Sits at 0x3A680 instead,
                 * FBCLEAR's own 384B tail -- so this holds 2x48 rows,
                 * SAMPLED, not all 112. Sampling is fine for a ratio. */
                {
                    uint32_t *bh = (uint32_t *)0x0603A680;   /* [2][48] */
                    if (sl < 48) {
                        uint32_t *e = bh + (bank ? 48 : 0) + sl;
                        DIAG[50]++;
                        if (*e == h) DIAG[51]++;
                        else *e = h;
                    }
                }
#endif
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
#ifdef DIRTY_ROW_VERIFY
        /* STAGE A: PROVE THE MARKING IS COMPLETE BEFORE SKIPPING
         * ANYTHING. Nothing is skipped on this build — for every row the
         * marks claim is all-zero, read it and count the times it was
         * not. A missed mark site is a layer that silently vanishes, the
         * same failure class as the deferred-pass trap, and it is not
         * something to discover from a screenshot.
         * Falsify this counter before trusting a zero (comment out one
         * RL_MARK and confirm it screams), exactly as the BLITSKIP
         * verifier was falsified.
         * [0] rows claimed zero, [1] claims that were FALSE, [2] rows. */
        {
            DRVC[2]++;
            if (!ROWLIVE[8 + y]) {
                uint32_t any = 0;
                for (int i = 0; i < 80; i++)
                    any |= src[i];
                DRVC[0]++;
                if (any) {
                    DRVC[1]++;
                    /* WHICH rows lie: 224-bit bitmap at 0x28FD4 (free
                     * after the band counters) — the row set names the
                     * unmarked compose pass. */
                    ((volatile uint32_t *)0x26028FD4)[y >> 5]
                        |= 1u << (y & 31);
                    /* WHAT the lying row HOLDS (first occurrence): the
                     * pen values name the drawer. 0x39740 — the real
                     * free gap (the DREQ ARM is 936 words so landings
                     * run to 0x39750 — 0x39740 was collision #8; and 0x39800 is
                     * md_dbg_base: that address's SEVENTH collision,
                     * caught because the "captured" y read 1029). */
                    if (*(volatile uint32_t *)0x26039750 == 0) {
                        *(volatile uint32_t *)0x26039750 =
                            0x80000000u | (unsigned)y;
                        volatile uint32_t *cap =
                            (volatile uint32_t *)0x26039754;
                        for (int i2 = 0; i2 < 40; i2++)
                            cap[i2] = src[i2];
                    }
                }
            }
        }
#endif
        volatile uint32_t *dst = (volatile uint32_t *)
            (0x04000000u + 0x200 + (unsigned)y * 320);
#ifdef BLIT_SKIP
        /* SKIP THE GROUPS THAT ARE ZERO AND ALREADY ZERO IN THIS BANK.
         * Measured on ares over 221,322 rows (MD_PAYOFF, canonical
         * bundle): 62.7% of 32px groups are entirely transparent, 79.4%
         * of AREA — but only 31.9% of whole ROWS, i.e. the emptiness is
         * SCATTERED, which is why the unit here is the group and not the
         * row. LOOP 9's 13-17% predates MDBGALL and does not apply.
         * The 8 loads are not extra work: the blit has to load these
         * longs to store them, so the OR rides along in registers.
         * (LOOP 9's "per-LONG branching costs more than the store" is a
         * different bet — one test per 8 longs, not one per long.)
         * An SDRAM read is ~5x cheaper than an FB write and 80% of the
         * blit is an FB-write bus-stall floor, so this pays past ~25%
         * skippable. MAME CANNOT SEE THAT WIN — it charges the ~2.7us
         * instruction issue and not the ~47us/row stall — but it is the
         * right place to gate the PIXELS: build without SPRTRUNC and
         * diff against the same build without this flag. */
        {
            volatile uint16_t *fbc = FBCLEAR + (bank ? 224 : 0) + y;
            unsigned was = *fbc, now = was;
#ifdef R60
            int vfy_i = -1;              /* first skipped group this row */
            unsigned vfy_sk = 0;         /* set of skipped groups (audit
                                          * picks a ROTATING member — the
                                          * first-only sample let lies in
                                          * later groups escape forever) */
#endif
#ifdef DIRTY_ROW
            /* STAGE C, and the whole point of job 2: leave the row
             * ENTIRELY ALONE — no loads, no stores, no per-group work.
             * BLITSKIP's group loop still has to READ all 80 longs to
             * discover they are zero, and the loads are the bigger half
             * of the row (29% vs 25% on the ares probes). This is the
             * only exit that pays a row's full cost, and it is available
             * exactly when two independent facts agree: compose says the
             * row is all zeros, and this BANK's mask says all ten groups
             * are already zero here.
             * Both are needed. ROWLIVE alone is not enough — the bank is
             * two cycles stale and may still hold the sprite that was
             * there last time. */
            /* Whole-row exit RESTORED (pass 12c): its danger was never
             * the exit — it was FALSE dead-claims from the deferred
             * cat1/text clear/draw race, killed by MARK-FIRST ordering
             * at the clear sites (DIRTYROWVERIFY: 5376 -> 0 false
             * claims). With marks true, the exit is sound again. */
            if (was == 0x3FF && !ROWLIVE[8 + y])
                continue;
#endif
#ifdef BLIT_HASH
            uint32_t *gh = blit_grouphash + ((bank ? 224 : 0) + y) * 10;
#endif
#ifdef ROWSHIP_PROBE
        RSHIP[y >= 112]++;               /* rows past the whole-row exit */
        {
            uint32_t h = 0x9E3779B9u;
            for (int i = 0; i < 80; i++)
                h = ((h << 5) | (h >> 27)) ^ src[i];
            volatile uint32_t *hb = RSHASH + (bank & 1) * 224 + y;
            RSCNT[y]++;
            if (*hb == h)
                RSIDENT[y]++;
            *hb = h;
        }
#endif
#ifdef BLIT_SKIP_NOSKIP
            /* DIAGNOSTIC ONLY (`make BLITSKIP=1 NOSKIP=1`): run the exact
             * same code path — same loads, same OR, same mask
             * maintenance, same register pressure — but never take the
             * skip. It separates "the mask/bank logic is wrong" from
             * "the extra code shifted MAME's timing and the pipeline
             * settled to a different phase". If a parity static moves
             * with THIS build too, the skip is innocent. */
            was = 0;
#endif
            unsigned g = 1;
#ifdef BLIT_SKIP_COUNT
            volatile uint32_t *bc =
                BSCNT + ((y < 56 || (y >= 112 && y < 168)) ? 0 : 2);
#endif
            for (int i = 0; i < 80; i += 8, g <<= 1) {
                uint32_t v0 = src[i + 0], v1 = src[i + 1],
                         v2 = src[i + 2], v3 = src[i + 3],
                         v4 = src[i + 4], v5 = src[i + 5],
                         v6 = src[i + 6], v7 = src[i + 7];
#ifdef BLIT_SKIP_COUNT
                bc[0]++;
#endif
                if (!(v0 | v1 | v2 | v3 | v4 | v5 | v6 | v7)) {
                    if (was & g) {
#ifdef BLIT_SKIP_COUNT
                        bc[1]++;
#endif
#ifdef BLIT_SKIP_VERIFY
                        /* THE ONLY TEST THAT SETTLES THIS. A pixel diff
                         * between two builds cannot tell corruption from
                         * a pipeline-phase shift — any code added to the
                         * blit moves MAME's SH-2 timing, and the text and
                         * palette layers land a frame differently. So do
                         * not compare frames: ASK THE FRAMEBUFFER. Every
                         * time the mask says "already zero", read the
                         * group back UNCACHED (0x24000000 — the 0x04...
                         * alias would answer from our own cache) and
                         * count the times it was not. Zero over a long
                         * run means the mask never lied, whatever the
                         * pixels look like. Nonzero localises the bug.
                         * NEVER SHIP: 8 uncached FB reads per skip. */
                        {
                            volatile uint32_t *ck = (volatile uint32_t *)
                                (0x24000000u + 0x200 + (unsigned)y * 320);
                            if (ck[i + 0] | ck[i + 1] | ck[i + 2] |
                                ck[i + 3] | ck[i + 4] | ck[i + 5] |
                                ck[i + 6] | ck[i + 7])
                                bc[4]++;
                        }
#endif
#ifdef R60
                        if (vfy_i < 0) vfy_i = i;
                        vfy_sk |= g;
#endif
                        continue;            /* bank already zero here */
                    }
                    now |= g;                /* zeroing it NOW is a write */
#ifdef BLIT_HASH
                    gh[i >> 3] = 0;          /* bank group becomes zero */
#endif
                } else {
                    now &= ~g;
#ifdef BLIT_HASH
                    uint32_t h = v0;
                    h = ((h << 1) | (h >> 31)) ^ v1;
                    h = ((h << 1) | (h >> 31)) ^ v2;
                    h = ((h << 1) | (h >> 31)) ^ v3;
                    h = ((h << 1) | (h >> 31)) ^ v4;
                    h = ((h << 1) | (h >> 31)) ^ v5;
                    h = ((h << 1) | (h >> 31)) ^ v6;
                    h = ((h << 1) | (h >> 31)) ^ v7;
                    if (h == 0) h = 1;
                    if (gh[i >> 3] == h) {
                        DIAG[10]++;          /* bank already holds it */
#ifdef BLIT_HASH_VERIFY
                        /* NEVER SHIP: read the bank back UNCACHED and
                         * count skips whose content is NOT sbuf's. Must
                         * be 0 over a long run — a nonzero is a stale
                         * group on screen. */
                        {
                            volatile uint32_t *ck = (volatile uint32_t *)
                                (0x24000000u + 0x200 + (unsigned)y * 320);
                            if ((ck[i + 0] ^ v0) | (ck[i + 1] ^ v1) |
                                (ck[i + 2] ^ v2) | (ck[i + 3] ^ v3) |
                                (ck[i + 4] ^ v4) | (ck[i + 5] ^ v5) |
                                (ck[i + 6] ^ v6) | (ck[i + 7] ^ v7))
                                DIAG[11]++;
                            DIAG[12]++;      /* skips verified */
                        }
#endif
                        continue;
                    }
                    gh[i >> 3] = h;          /* the stores below ship it */
#endif
                }
                dst[i + 0] = v0;
                dst[i + 1] = v1;
                dst[i + 2] = v2;
                dst[i + 3] = v3;
                dst[i + 4] = v4;
                dst[i + 5] = v5;
                dst[i + 6] = v6;
                dst[i + 7] = v7;
            }
#ifdef R60
            /* VERIFY-PER-ROW (the purple band, PROVEN by the NOSKIP
             * A/B): the mask desyncs under live timing by a path five
             * audits have not found; a lied-to skip preserves stale
             * pixels that CRAM churn paints purple. Wholesale and
             * rotating invalidation both cost too many skips (the R60
             * window lives on them). This keeps every skip and audits
             * ONE skipped group per row by uncached readback (~8 reads
             * per skipping row): on a lie, drop the row's whole mask —
             * the row rewrites honestly next frame. Lies heal in <=2
             * frames; the audit count lands in DIAG[42]. */
            if (vfy_i >= 0) {
                /* pick a ROTATING member of the skipped set (fall back
                 * to the first skip when the rotation misses) */
                unsigned cand = ((unsigned)y + vseq) % 10u;
                if (vfy_sk & (1u << cand))
                    vfy_i = (int)(cand * 8u);
                volatile uint32_t *ck = (volatile uint32_t *)
                    (0x24000000u + 0x200 + (unsigned)y * 320);
                if (ck[vfy_i + 0] | ck[vfy_i + 1] | ck[vfy_i + 2] |
                    ck[vfy_i + 3] | ck[vfy_i + 4] | ck[vfy_i + 5] |
                    ck[vfy_i + 6] | ck[vfy_i + 7]) {
                    now = 0;                 /* mask lied: relearn row */
                    DIAG[42]++;
#ifdef BLIT_HASH
                    for (int q = 0; q < 10; q++)
                        gh[q] = 0;           /* bank content unknown */
#endif
                }
            }
#endif
            if (now != was)
                *fbc = (uint16_t)now;
        }
#elif defined(BLIT_NOLOAD)
        /* READ-COST PROBE, TIMING ONLY. Store a value read ONCE per row
         * instead of 80 times: same 80 stores, 1 load instead of 80.
         * The A/B against the plain build isolates what the sbuf READS
         * cost, and that is now the whole question — removing 57% of
         * the stores removed 14% of the blit, and silencing the other
         * CPU's blit removed 6%, so neither the writes nor the bus
         * contention is where the time goes. Arithmetic from those two:
         * a long store is 0.106 ticks and the residual is 25.7 of 34.2
         * ticks/row (75%). If that residual is the reads, this build
         * drops to ~10 ticks/row. 20 cache lines per row, and
         * cache_purge() runs every window, so every one of them is a
         * guaranteed cold miss — 1.29 ticks each would explain it
         * exactly. *** THE PICTURE WILL BE GARBAGE (every row a flat
         * smear). Timing only. *** */
        {
            uint32_t v = src[0];
            for (int i = 0; i < 80; i += 8) {
                dst[i + 0] = v;
                dst[i + 1] = v;
                dst[i + 2] = v;
                dst[i + 3] = v;
                dst[i + 4] = v;
                dst[i + 5] = v;
                dst[i + 6] = v;
                dst[i + 7] = v;
            }
        }
#else
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
#endif
    }
}

#ifdef R60
/* STALE BEATS HOLE — TRIED AND REVERTED (pass 11): masking on live bq
 * slots skipped slices of bands that are LEGITIMATELY mid-compose
 * every window (the pipeline overlaps by design), so masked rows
 * never got their FIRST blit — which reproduced Mike's purple band
 * DETERMINISTICALLY: never-blitted FB rows keep boot through-pixels
 * and the MD side shows in the hole. THE REPRO IS THE FINDING: the
 * live band = rows missing their blit. The component that decides a
 * row needs no blit is BLITSKIP's already-zero-per-bank bookkeeping —
 * a desync there under live timing leaves persistent through-holes.
 * A/B instrument: NOSKIP=1 (same code, skip never taken).
 * Helper kept for reference; call sites reverted. */
/* ORIGINAL INTENT (wrong premise): the blit used
 * to ship CLEARED-BUT-UNCOMPOSED master band slices — sbuf zeros land
 * in the FB as through-pixels and the MD layer (or backdrop) shows in
 * the gap: the purple bottom band and the mid-screen seam tears at
 * the master-slice rows (~68/140/216 at the shipped calibration).
 * The master publishes a 3-bit incomplete-band mask (uncached SDRAM
 * 0x28FC4) before the window command; BOTH CPUs blit around the
 * masked slices, so those FB rows keep LAST frame's pixels — a
 * complete band one frame late, the project's own doctrine, extended
 * to the blit. */
RAMCODE static void blit_around(int lo, int hi, int bank, unsigned mask)
{
    const int slo[3] = { 36 + BAND_SHIFT, 108 + BAND_SHIFT,
                         184 + BAND_SHIFT_RG2 };
    const int shi[3] = { 72, 144, 224 };
    int y = lo;
    while (y < hi) {
        int ye = hi, skip = 0;
        for (int r = 0; r < 3; r++) {
            if (!(mask & (1u << r)))
                continue;
            if (y >= slo[r] && y < shi[r]) {
                y = shi[r];
                skip = 1;
                break;
            }
            if (slo[r] > y && slo[r] < ye)
                ye = slo[r];
        }
        if (skip)
            continue;
        if (ye > y)
            BLIT_HALF(y, ye, bank);
        y = ye;
    }
    (void)bank;
}
#endif
#endif /* !DIRECT_FB — stage-2: the sbuf->FB ship is gone */

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
 * pre-ack critical path (docs/log/LOOP.md iter4: retry-loop saturation). */
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
#ifdef FB_TEXT_READ
/* LOOP 20 — the text capture, callable from EITHER CPU. Master-side it
 * ran inside the k2 flip block and cost ~10 in-window lines; under
 * TEXTCAP_SLAVE the master posts SYNC[4]=0x4000 at k2 entry and the
 * SLAVE (idle 17,235 polls/cycle) captures in parallel with the
 * master's truth drain, echoing SYNC[6]. Pre-flip either way: the
 * capture MUST read the bank the game just wrote (the post-flip bank
 * self-poisons — see the pre-flip note at the call site). */
/* 2026-09-03: cart ROM — a small per-window loop the I-cache holds after
 * its first pass (68B freed; region guard). */
void text_capture(void)
{
    volatile uint32_t *td = (volatile uint32_t *)TEXT_U;
    volatile uint32_t *ts = (volatile uint32_t *)FB_TEXT;
#if defined(R60) && defined(FB_SPR_READ)
    /* S1 STRIKE (docs/design/PIPELINE.md): sprite snapshot rides the same
     * pre-flip capture — FB_SPR holds the game's own ordered vint
     * upload (full list + terminator every vint, LOOP20 measured),
     * written into THIS draw bank during the gap. Copy through the
     * terminator; SPR_SNAP is the compose's only sprite source.
     * Runs on the slave in parallel with the master's truth drain
     * (or on the master via the fallback path — same function). */
    {
        /* snap_skip lives in the audited 0x3A78C-0x3A7D8 gap (probe
         * builds ran the region guard to zero; .bss is full). */
#define snap_skip (*(volatile uint8_t *)0x2603A7D6)
        if (SYNC[13] || SYNC[9]) {
            if (++snap_skip < 2)
                goto snap_done;          /* bounded: a chronically busy
                                          * compose must not pin sprites
                                          * at 20Hz — after 2 skipped
                                          * refreshes take the split
                                          * risk over the staleness */
        }
        snap_skip = 0;
    }
    {
        /* SNAP LATCH: skip the refresh while a compose chain is mid-
         * flight reading SPR_SNAP (slave sets SYNC[13] around chain
         * commands). An overloaded cycle then shows the WHOLE sprite
         * frame one vint late — uniform, the AUTO-30 semantics —
         * instead of splitting an actor at the compose-half boundary
         * (the shimmering top/bottom Zeus split, 2026-08-26). The
         * game re-uploads the full list every vint, so the next
         * unlatched k2 refreshes cleanly. */
        for (int i = 0; i < 512; i += 8) {
            uint16_t v2;
            SPR_SNAP[i + 0] = FB_SPR[i + 0];
            SPR_SNAP[i + 1] = FB_SPR[i + 1];
            v2 = FB_SPR[i + 2];
            SPR_SNAP[i + 2] = v2;
            SPR_SNAP[i + 3] = FB_SPR[i + 3];
            SPR_SNAP[i + 4] = FB_SPR[i + 4];
            SPR_SNAP[i + 5] = FB_SPR[i + 5];
            SPR_SNAP[i + 6] = FB_SPR[i + 6];
            SPR_SNAP[i + 7] = FB_SPR[i + 7];
            if (v2 & 0x8000)
                break;                   /* terminator copied; tail is
                                          * stale FB, never read */
        }
    }
snap_done: ;
#endif
#if defined(K2_FREE) || defined(R60)
    /* glyphs only: words 0x740+ are dead FB bytes (regs ride the
     * packet); a 1024-long copy would clobber TEXT_U's packet-fed
     * regs — same rule as the master's inline capture. */
    for (int i = 0; i < 928; i += 4) {
#else
    for (int i = 0; i < 1024; i += 4) {
#endif
        td[i + 0] = ts[i + 0];
        td[i + 1] = ts[i + 1];
        td[i + 2] = ts[i + 2];
        td[i + 3] = ts[i + 3];
    }
}
#endif

/* LOOP 24 — THE k2 FLIP-CRITICAL SPAN, extracted whole from the window
 * body (it ran inline there through LOOP23). Order is load-bearing:
 *  1. text capture + truth drain of ALL pending dirty pages while the
 *     FB window still maps the bank the game wrote;
 *  2. flip (write is the commitment; the spin below only waits to SEE
 *     the latch);
 *  3. restore every page dirtied since the last flip into the new draw
 *     bank from TILEMAP_U/TEXT_U, BEFORE the ack — the game writes
 *     staging only post-ack, so a restore clobbering a fresh game write
 *     is impossible by construction, and its read-backs (collision
 *     tst.w, the page-1 scratch pair) never see a stale bank.
 * Callable from TWO places, never both in one cycle: the polled window
 * body (the LOOP23 shipping path, and the fallback), and under
 * VISR_FLIP the master's own V-ISR at vblank entry (the tear fix: a
 * flip issued at polled pickup lands 1-3ms late behind a heavy compose
 * strip and latches past vblank — flip-late-latches 7.2% of cycles on
 * the X state). Mutual exclusion is by construction: the ISR only runs
 * the span on a FRESH k2 post observed with COMM0 clear at ISR entry,
 * and the body only runs it on pickup of that same post when the ISR
 * declined (visr_flip_done clear); between ISR-set and body-consume
 * COMM0 stays live, which makes the ISR bail. Requires FM=1 and V
 * inside vblank at call — the body's V-gate or the 68K's own entry
 * gate (it raises FM before posting) provide that at the two sites.
 * Stays in .text: the region guard has ~450 bytes left, no room in
 * .ramtext, and the body always executed this from cart ROM anyway. */
#ifdef K2_FREE
#ifdef R60
static uint16_t visr_fm0_rd;             /* FM_TEST read-half probe */
static uint8_t  visr_fm0_have;
#endif
static uint16_t visr_t0;                 /* ISR entry FRT — flip_span's
                                          * flip-position counters read it
                                          * (body-fallback calls sample a
                                          * stale one: fallback is ~0.1%,
                                          * tolerated in a probe counter) */
#ifdef FLIP_DEFER
/* DEFERRED FLIP (2026-09-08, LOOP27 9): the edge guard below used to
 * DROP a flip that missed vblank, and at one game-frame per vint it
 * misses most of them (flips on ~17% of vints = the 60Hz blocker).
 * The FPGA RTL says hardware does not tear on a late FBCTL write — it
 * defers: srcref/S32X_MiSTer rtl/32X/VDP.sv latches `FS <= FBCR.FS`
 * only when VBLK. ares approximates it by latching immediately
 * mid-scan (the tear the guard exists to dodge), so we cannot simply
 * write late and rely on the hardware behaviour — Mike's gate is ares.
 * Instead do in software what the silicon does: on a late arrival ARM
 * this flag, and commit the flip at the TOP of the next vblank, where
 * the write is in-window for both ares and hardware. The frame ships
 * one vint later instead of never. */
static volatile uint8_t flip_deferred;
#endif
#endif
#ifdef VB_SPAN
/* VBLANK BUDGET BREAKDOWN (LOOP28 94). FRT ticks from ISR entry to each
 * boundary of the pre-flip path, summed with a count so the reader takes
 * means. ~46 ticks = one scanline; the edge guard is 1650 = 38 lines.
 * CEN[29] is the denominator; every other slot is a running sum. */
/* ONLY the ISR's own calls. flip_span is also reached from the body
 * fallback, where visr_t0 is a stale ISR-entry stamp and the delta is
 * meaningless; mixing the two produced a 65-line "wait for the window"
 * that does not exist (LOOP28 94, corrected in 96). */
static uint8_t vbs_isr;
static uint16_t vbs_t[5], vbs_t2[2];
/* Stamps are BUFFERED and committed only if this call reaches the FS
 * write, so every slot shares one denominator (CEN[36]). Committing as
 * you go mixes calls that declined early into the early slots and not
 * the late ones, which is how entry 94's numbers came out incomparable
 * across rows. */
#define VBS(i) do { if (vbs_isr) vbs_t[i] = (uint16_t)(frt() - visr_t0); } while (0)
#else
#define VBS(slot) do { } while (0)
#endif
#ifdef FLIPRATE_MEANSUM
static uint8_t fs_from_isr;              /* LOOP29 143 probe: flip_span called from the ISR */
#endif
#ifdef TEXTCAP_EARLY
static volatile uint8_t fs_posted_early; /* LOOP29 145: the ISR posted this vint's capture */
#endif
#if defined(TWO_POST) && defined(FB_TEXT_READ)
/* LOOP29 149: the text restore moved from flip_span to the window start
 * (post B), off the ISR's FM span. Same contract: before the ack, so the
 * game never writes text into a bank that lacks the truth. */
RAMCODE static void tp_text_restore(void)
{
    volatile uint32_t *rd = (volatile uint32_t *)(0x04000000u + 0x1F000u);
    volatile uint32_t *rs = (volatile uint32_t *)TEXT_U;
    for (int i = 0; i < 1024; i += 4) {
        rd[i + 0] = rs[i + 0]; rd[i + 1] = rs[i + 1];
        rd[i + 2] = rs[i + 2]; rd[i + 3] = rs[i + 3];
    }
}
#endif
#ifdef PG_SKIP_PKT
static uint32_t tp_lastA[368], tp_lastB[368];   /* the plane packets exactly as
                                                 * the FB holds them (149) */
static uint32_t tp_lastPal[8], tp_lastSat[64];  /* MDSPR palette + SAT likewise */
static uint16_t mir_a, mir_b;            /* packet magic words read pre-flip
                                          * (the mailbox mirror, LOOP29 139) */
#endif
#if defined(FBX_ISRLIFT) && defined(FB_XPORT)
RAMCODE static void fbx_lift(void);      /* defined below (FB_XPORT) */
static unsigned fbx_landed;              /* tentative; the definition
                                          * sits with fbx_lift */
#endif
#ifdef VB_SPAN
static int flip_span_inner(void);
/* clears vbs_isr on EVERY exit, so a body-fallback call that follows an
 * ISR call in the same vint is never counted as one */
static int flip_span(void) { int r = flip_span_inner(); vbs_isr = 0; return r; }
static int flip_span_inner(void)
#else
static int flip_span(void)
#endif
                                         /* 1 = flipped; 0 = DECLINED
                                          * (K2_FREE edge guard: too
                                          * late in vblank — a dropped
                                          * frame beats a mid-scan bank
                                          * swap. Mike's Z screens:
                                          * ~490 of 676 body-fallback
                                          * flips latched IMMEDIATELY
                                          * out of vblank = the tear;
                                          * only 182 deferred.) */
{
#if defined(FBX_ISRLIFT) && defined(FB_XPORT)
    /* LIFT BEFORE THE FLIP, ON THE ISR PATH TOO (LOOP29 137). The 68K's
     * tail blast wrote the packet into the bank that is hidden NOW; the
     * FS write below is the only thing that changes that, so reading
     * here is always the right bank. The body's pre-flip lift (LOOP27
     * 72) only covered the body-fallback flip; on the ISR path the flip
     * happened at vblank top and the body then read the OTHER bank,
     * which is what FBXBOTH's second blast papered over at 12 lines of
     * 68K FB writes per vint. Guarded: a packet lifted and not yet
     * harvested is kept, not re-zeroed. */
    if (!fbx_landed) fbx_lift();
#endif
#ifdef FLIPRATE_MEANSUM
    if (fs_from_isr) CEN[52] += (uint16_t)(frt() - visr_t0) / 46u;  /* post wait, lines */
#endif
#ifdef VB_SPAN

    VBS(0);                             /* entry */
#endif
#ifdef R60
    /* FM_TEST read-half verdict: FM=1 here; compare against the
     * announce-time FM=0 read of the same 68K-untouched word. */
    if (visr_fm0_have) {
        visr_fm0_have = 0;
        if (visr_fm0_rd == *(volatile uint16_t *)0x24011A04u)
            DIAG[24]++;
        else
            DIAG[25]++;
    }
#endif
#ifdef FB_TEXT_READ
#ifdef TEXTCAP_SLAVE
#ifdef TEXTCAP_DUAL
    /* LOOP28 101 DIAGNOSTIC: do the capture inline AND keep the slave
     * post+join. If the band clears, the fault is the missing barrier
     * (the join was letting something else finish); if it stays, the
     * fault is in the capture itself. Saves nothing by design. */
    {
        volatile uint32_t *td = (volatile uint32_t *)TEXT_U;
        volatile uint32_t *ts = (volatile uint32_t *)FB_TEXT;
        for (int i = 0; i < 928; i += 4) {
            td[i + 0] = ts[i + 0]; td[i + 1] = ts[i + 1];
            td[i + 2] = ts[i + 2]; td[i + 3] = ts[i + 3];
        }
    }
#endif
    /* capture runs on the SLAVE, in parallel with the truth
     * drain below. Post BEFORE the drain, join AFTER it:
     * both sides work, the window shortens by the overlap. */
#ifndef TEXTCAP_EARLY
    SYNC[6] = 0;
#ifdef VB_SPAN
    SYNC[7] = 0;
#endif
    SYNC[4] = 0x4000;
#else
    if (!fs_posted_early) { SYNC[6] = 0; SYNC[4] = 0x4000; }  /* body-fallback
                                          * flip on a vint whose ISR did not
                                          * post: post now, join below */
#endif
#else
    /* text capture: 512 longs FB -> TEXT_U truth.
     * MUST RUN PRE-FLIP. The first cut ran in the snapshot
     * block, which at k2 is AFTER the flip — it read the
     * bank the game had NOT written, and the restore then
     * wrote those zeros into every bank: a self-poisoning
     * loop that blanked text and regs permanently (probed:
     * capture ran 296 times, TEXT_U stayed zero, FB had
     * data). Pre-flip this reads the bank the game wrote
     * during the gap — complete current truth — and the
     * post-flip restore propagates it to the other bank. */
    {
        volatile uint32_t *td = (volatile uint32_t *)TEXT_U;
        volatile uint32_t *ts = (volatile uint32_t *)FB_TEXT;
        /* 1024 LONGS = the full 2048-word region. The first
         * cut copied 512 longs — half the region — and the
         * regs at word 0x740 sit in the half it missed;
         * probed as "loop runs, source has data, dest stays
         * zero". Text RAM is 4KB, not the sprite list's 1KB.
         * K2FREE: capture GLYPHS ONLY (464 longs = words
         * 0..0x73F). The game's reg/rowscroll writes moved to
         * the WRAM mirror (patch split) — the FB copies at
         * 0x740+ are dead bytes now, and capturing them would
         * clobber TEXT_U's packet-fed regs. The restore below
         * stays full-width (writing good regs into dead FB
         * space is harmless). Bonus: the ISR flip span's
         * pre-flip half shrinks by ~half its text cost. */
#ifdef R60
        /* R60: text truth at 30Hz — capture every OTHER frame. Text is
         * sparse (the record's own fact) and this is the flip span's
         * fattest fixed cost: halving its rate pulls the FBCTL write
         * ~10 lines earlier, the difference between flipping and
         * declining at 60Hz. */
        static uint8_t r60_txt_alt;
        r60_txt_alt ^= 1;
        /* LOOP28 100: the 30Hz halving is why the inline path renders
         * wrong. The SLAVE path captures EVERY frame, so switching the
         * capture to the master silently also halved its rate, and the
         * restore then spread half-stale text truth into both banks.
         * TEXTCAPFULL restores the every-frame rate; the capture is 4.3
         * scanlines, against the 31.8 the master spends waiting for the
         * slave to pick it up (LOOP28 96). */
#ifdef TEXTCAP_MASK
        /* LOOP29 147: only the 4-row groups the game's text writers
         * marked (COMM2 high byte, posted by the shim). 128 longs per
         * group; group 7 is the last row (928 longs = 29 rows). */
        {
            uint8_t tm = (uint8_t)(MARS_SYS_COMM2 >> 8);
            for (int g = 0; g < 8; g++) {
                if (!(tm & (1u << g))) continue;
                int lo = g * 128, hi = (lo + 128 > 928) ? 928 : lo + 128;
                for (int i = lo; i < hi; i += 4) {
                    td[i + 0] = ts[i + 0]; td[i + 1] = ts[i + 1];
                    td[i + 2] = ts[i + 2]; td[i + 3] = ts[i + 3];
                }
            }
        }
        if (0)
#endif
#ifndef TEXTCAP_FULL
        if (r60_txt_alt)
#endif
        for (int i = 0; i < 928; i += 4) {
#elif defined(K2_FREE)
        for (int i = 0; i < 928; i += 4) {   /* 0x740 words = 928 longs
                                              * (first cut wrote 464 —
                                              * HALF the glyphs — and the
                                              * restore spread the stale
                                              * half into both banks) */
#else
        for (int i = 0; i < 1024; i += 4) {
#endif
            td[i + 0] = ts[i + 0];
            td[i + 1] = ts[i + 1];
            td[i + 2] = ts[i + 2];
            td[i + 3] = ts[i + 3];
        }
    }
#endif
#endif
    /* LIVE DIRTY WORD (COMM10, MD-written before every post), merged at
     * the flip ONLY. The restore set must be complete through THIS vint
     * — the word-80 copy is one window late, and under double-buffering
     * that skew loses the last gap's writes across the flip (game logic
     * corruption: collision reads a stale bank). It is NOT merged at
     * k0/k1: the thunks mark at pointer-load, before their stores, so
     * early captures take HALF-WRITTEN pages into truth — the 32X
     * compose self-corrects, but the MD builder's allocator turns a
     * garbage page into persistent md_tag claims (measured: glyph-field
     * planes after every scene cut). k0/k1 captures run on word-80
     * marks, which arrive one window settled; the k2 mid-stream capture
     * is mandatory and exactly right for the game (restore = the bytes
     * it just wrote), and pg_watch + the builder's claim gate contain
     * its poison. */
#ifdef PAL_PEN
    /* PALETTE DRAIN, at the TOP of flip_span (entry 28). This is the one
     * point in our frame that is inside vblank AND holds FM: the caller
     * has seen the 68K's post, which guarantees FM, and the edge guard
     * downstream is what keeps this within the 38-line vblank. PEN is
     * asserted across all of it, so the whole dirty set drains in one
     * burst with no stall.
     *
     * CRITICALLY IT IS BEFORE EVERY DECLINE PATH — the edge guard, the
     * DIRECT_FB gate, the NATIVE_FRAME gate. PAL_VBLANK put the drain
     * after the flip write, so a declined flip drained nothing, and on
     * hardware most flips decline: CRAM stayed empty and the screen went
     * to the MD backdrop (entry 22). The palette must not depend on the
     * flip landing. */
    cram_flush_pen();
#endif
    VBS(1);                             /* after the palette drain */
    pg_pending |= MARS_SYS_COMM10 & 0x1FFF;
#ifdef PG_STICKY
    /* marks enter WATCH: a pointer-load mark can arrive
     * cycles before the stream's stores, and the first
     * capture then sees no change — sticky watch keeps
     * capturing until 12 quiet cycles prove the stream is
     * really over (the eyehold stale-truth root). Broad
     * marks (loaders) pin deep watch; decremented here,
     * once per cycle. */
    {
        uint16_t m = MARS_SYS_COMM10 & 0x1FFF;
        pg_watch |= m;
        if (__builtin_popcount(m) >= 8) pg_deep = 24;
        else if (pg_deep) pg_deep--;
    }
#endif
    cycle_dirt |= pg_pending;
    pg_pending |= pg_watch;      /* unstable pages: recapture the
                                  * latest stream state pre-flip */
    VBS(2);                             /* after the page merge */
#ifdef DRAIN_CUT
    /* LOOP28 107, THE 60 FPS SHAPE. The flip write must land within 35.9
     * lines of ISR entry or the FPGA defers it to the next vblank and the
     * frame is lost (Mike's MiSTer verdict on U_dblfast: near-perfect
     * frames, far too few of them). 26.9 of those lines are already spent
     * waiting for the 68K's post, and this drain is 25.0 more at 9.23
     * pages a flip. It cannot be shortened into the budget, so it does
     * not belong in the flip path at all: take DRAIN_CUT pages here and
     * leave the rest to the body, which has the whole active display. */
    cap_drain(DRAIN_CUT);
#else
    cap_drain(13);               /* ALL of it — correctness */
#endif
    VBS(3);                             /* after the truth drain */
#ifdef PG_SKIP_PKT
    /* MD-PLANE MAILBOX MIRROR (LOOP29 139). The page-12 restore used to
     * carry MD-plane packet B across the bank swap, and the FPGA NEEDS
     * that: without it the 68K's consume misses the packet the master
     * wrote and the MD planes go black on the MiSTer (vi2 black, vi3
     * with the page-12 capture back: full background). Restoring the
     * half page cost ~6 lines on the pre-flip path and put the FS write
     * past the guard again (vi3: 10637 edge declines in 6915 cycles).
     * So: read the two magic words here (2 loads), and after the latch
     * replay a still-unconsumed packet from the master's own staging
     * image into the new bank, or zero the slot if it was consumed. */
    mir_a = (*(volatile uint32_t *)0x24011A00u) >> 16;
    mir_b = (*(volatile uint32_t *)0x2401E800u) >> 16;
#endif
#if defined(FB_TEXT_READ) && defined(TEXTCAP_SLAVE)
    {
        uint32_t g2 = 2000000;
#ifdef VB_SPAN
        {   /* split the join: latency to pickup, then the copy itself */
            uint32_t g3 = 2000000;
            while (SYNC[7] != 0x4001 && SYNC[6] != 0x4000 && --g3) ;
            if (vbs_isr) vbs_t2[0] = (uint16_t)(frt() - visr_t0);
        }
#endif
        while (SYNC[6] != 0x4000 && SYNC[6] != 0x4002 && --g2) ;
#ifdef VB_SPAN
        if (vbs_isr) vbs_t2[1] = (uint16_t)(frt() - visr_t0);
#endif
        if (!g2 || SYNC[6] == 0x4002) {  /* 0x4002: the slave saw no FM */
            DIAG[22]++;          /* slave never captured: fall
                                  * back on the master, late
                                  * but pre-flip — correctness
                                  * over the overlap win */
            text_capture();
        }
    }
#endif
#ifdef K2_FREE
    /* EDGE GUARD: the FBCTL write must land inside vblank (~38 lines
     * from the vint, tolerance measured by [31]); past that ares often
     * latches IMMEDIATELY mid-scan — the visible tear. visr_t0 is this
     * vblank's ISR entry stamp (the ISR fires every vblank, so it is
     * fresh for the body-fallback path too). Declining keeps the
     * capture+drain already done (truth is truth), keeps cycle_dirt
     * (the restore set carries to the next flip), echoes 0xF1FF so the
     * held 68K releases, and counts DIAG[44]. */
    /* 1748 (the full 38-line vblank) TIGHTENED to 1650 after Mike's
     * play pass reported tearing with flip-pos max = 38: a flip
     * landing in the last ~2 lines of vblank latches mid-scan often
     * enough to see. ~2-line safety margin; the marginal flips
     * become clean declines instead of tears. */
#ifdef FLIPRATE_MEANSUM
    if (fs_from_isr) { CEN[50] += (uint16_t)(frt() - visr_t0) / 46u; CEN[51]++; }  /* at the guard, lines */
#endif
#ifdef FLIP_EDGE_OFF
    /* THE EDGE GUARD, OFF (LOOP27 74). It drops any flip that misses the
     * vblank edge because ares latches FS immediately and tears. Real
     * silicon does not: srcref/S32X_MiSTer rtl/32X/VDP.sv latches
     * FS <= FBCR.FS only when VBLK, i.e. a late write is DEFERRED by the
     * hardware itself. Under FBXPORT the push moves ahead of the post,
     * the post lands ~57 lines later, and this guard then declines
     * essentially every flip: 27.3 Hz -> 1.2 Hz. Expect tearing on ares
     * with this off; expect the FPGA not to tear. */
    if (0) {
#else
    if ((uint16_t)(frt() - visr_t0) > 1650) {
#endif
        MARS_SYS_COMM4 = 0xF1FF;
#ifdef FLIP_CENSUS
        CEN[21]++;                       /* declined: past the vblank edge */
#endif
        DIAG[44]++;
#ifdef FLIP_DEFER
        /* Not a dropped frame any more: arm, and the next vblank's ISR
         * commits it before it waits for that vint's window. Everything
         * the decline path already guarantees still holds — the capture
         * and truth drain above are kept, cycle_dirt carries the restore
         * set to whichever flip actually lands, and the held 68K is
         * released on F1FF. The commit re-runs the capture (the game
         * writes during the intervening vint MUST reach truth, or the
         * restore writes a stale page into the fresh bank — the bank
         * disease the k2 comments describe). DIAG[6] counts arms. */
        flip_deferred = 1;
        DIAG[6]++;
#endif
        return 0;
    }

#endif
#ifdef DIRECT_FB
    /* STAGE-1 FLIP GATE (see dfb_drawn): the interval that just ended
     * launched no compose chain, so the draw bank still holds the
     * frame from TWO intervals ago — flipping would step the display
     * backward. Decline exactly like the edge guard above: capture and
     * truth drain already ran and are kept, cycle_dirt carries the
     * restore set to the next real flip, and the held 68K releases on
     * F1FF. The display holds last frame — the same hold the blit
     * pipeline produced by re-shipping sbuf. DIAG[29] counts held
     * flips (free here: ROW_DEFER is compiled out on R60 ships). */
    if (dfb_nohold ? 0 : !dfb_drawn) {
        MARS_SYS_COMM4 = 0xF1FF;
#ifdef FLIP_CENSUS
        CEN[22]++;                       /* declined: nothing drawn */
#endif
        DIAG[29]++;
        return 0;
    }
    dfb_drawn = 0;
#endif
#ifdef NATIVE_FRAME
    /* WHOLE-FRAME FLIP GATE (the dfb stage-1 pattern, blit-pipeline
     * form): the hidden bank only advances when a CLOSED generation
     * was blitted into it last window. No fresh blit -> the bank
     * holds a 2-flip-old frame and flipping would step the display
     * backward. Decline exactly like the edge guard: truth kept,
     * cycle_dirt carried, the held 68K releases on F1FF. DIAG[29]
     * counts holds (free here: ROW_DEFER is compiled out). */
    if (!nat_shipped) {
        MARS_SYS_COMM4 = 0xF1FF;
#ifdef FLIP_CENSUS
        CEN[23]++;                       /* declined: nothing shipped */
#endif
        DIAG[29]++;
        nat_capt = 1;                /* captures above already ran this
                                      * vint: the body fallback must not
                                      * run a second capture pass inside
                                      * the FM span (D56 was 1190 wasted
                                      * calls on the first battery) */
        return 0;
    }
#ifdef PHASE_CENSUS
    nat_ph_flip();
#endif
    nat_shipped = 0;
#ifdef HS_CENSUS
    HSC_RING[(HSC_IDX & 15) * 2 + 1] = (uint16_t)(0x8000 | hsc_win); HSC_IDX++;  /* ISR flip */
#endif
#endif
    VBS(4);                             /* at the FS write */
#ifdef VB_SPAN
    if (vbs_isr) {
        CEN[36]++;                       /* the one denominator */
        for (int q = 0; q < 5; q++) CEN[24 + q] += vbs_t[q];
        CEN[37] += vbs_t2[0];            /* slave picked the capture up */
        CEN[38] += vbs_t2[1];            /* slave finished it */
    }
#endif
    {
        uint16_t fs_o = MARS_VDP_FBCTL & MARS_VDP_FS;
#ifdef FLIP_CENSUS
        CEN[17]++;                       /* THE FS WRITE — the only thing
                                          * that actually changes the
                                          * displayed framebuffer. Every
                                          * other "flip" counter in this
                                          * file counts reaching a site. */
#endif
        MARS_VDP_FBCTL = fs_o ^ 1;
#ifdef K2_FREE
        /* FLIP ECHO (LOOP24): the k2 68K holds — register-polling from
         * WRAM, ZERO cart/bus traffic — until this lands, then releases
         * the game. The whole pre-flip half (capture + truth drain) ran
         * contention-free, exactly the condition the old full spin
         * provided by accident; only the post-flip half (restore) runs
         * under game concurrency. Echo the WRITE, not the latch: the
         * write is the commitment and a deferred latch must not hold
         * the 68K (the bounded spin below still waits to SEE it).
         * DIAG[45]/[46]: flip position from ISR entry (sum/max) — the
         * number that decides tear-legality, distinct from the span. */
        MARS_SYS_COMM4 = 0xF102;
#ifdef FLIP_CENSUS
        /* FLIP CENSUS (LOOP27 71). The 32X layer changes only at a flip,
         * so flips per V-ISR IS the display refresh rate. Entry 9
         * measured 17% on the 60 Hz builds from COMM traces; these put
         * the same number in a savestate, split by decline reason:
         *   [66] flipped   [67] past the vblank edge
         *   [68] nothing drawn  [69] nothing shipped  [70] V-ISR entries */
        CEN[19]++;                       /* ISR flipped */
#endif
        {
            uint16_t fp = (uint16_t)(frt() - visr_t0);
            DIAG[45] += fp;
            if (fp > DIAG[46]) DIAG[46] = fp;
        }
#endif
#ifdef BLIT_SKIP
        /* The hidden DRAW bank has just changed, so every blit
         * from here on targets the OTHER half of FBCLEAR. This
         * is the only toggle in the program because this is the
         * only FS write in the program. Toggle on the WRITE, not
         * the latch: the flip is committed the moment the write
         * is issued (the bounded spin below only waits to SEE
         * it), and a late latch that toggled late would hand the
         * slave the wrong label for a whole window. */
        fb_draw_par ^= 1;
#endif
#ifdef PAL_PEN
        /* Second drain, AFTER the flip: this is inside vblank, where PEN
         * is asserted for the whole interval, so whatever the window's
         * opportunistic pass could not place lands here in one burst. */
        cram_flush_pen();
#endif
#ifdef PAL_VBLANK
        /* THE PALETTE LANDS HERE: immediately after the flip write, which
         * the edge guard has already pinned inside vblank, so PEN is
         * asserted and hardware accepts every store. FM is still ours at
         * this point (same precondition the bar/capture writes rely on). */
        cram_flush_vbl();
#endif
        /* ONCE THE WRITE IS ISSUED THE FLIP IS COMMITTED. Mike's
         * first pres-2.0 ares state read [31]=10 in 645 cycles:
         * even inside the gate, ares sometimes latches this write
         * LATE. The first cut treated a 200-tick timeout as an
         * abort and skipped the restore — but the write cannot be
         * taken back; the flip landed moments later and the banks
         * swapped UNRESTORED (staging skew = the game's read-backs
         * see a stale bank = garbage tilemap writes). So: wait for
         * the latch as long as it takes, bounded only by ~1.5
         * frames (18000 ticks) as a hang backstop. The stall costs
         * up to a frame with FM=1 on the rare late latch — the
         * LOOP 7j positive-feedback concern — but a correctness
         * violation is not an option, and this is once per cycle
         * at worst, not per band pair. DIAG[31] now counts LATE
         * latches (>200 ticks); state_health flags them as a
         * latency signal, not corruption. */
        {
            uint16_t w0 = frt();
            while ((MARS_VDP_FBCTL & MARS_VDP_FS) != (fs_o ^ 1)
                   && (uint16_t)(frt() - w0) < 18000) ;
            if ((uint16_t)(frt() - w0) >= 200)
                DIAG[31]++;          /* late latch (was: abort) */
        }
#ifdef BOOT_ABBOTH
        /* BOTH-BANKS BAR (2026-09-08, LOOP27 17). The pre-flip bar goes
         * into the bank being drawn; this one goes into the bank the
         * flip just handed us. Two writes per vint, one either side of
         * the flip, so BOTH banks carry the bar and "which bank is
         * displayed" stops being a variable at all. Same rows 8-15,
         * same fixed index 1, still FM=1 and still ours.
         *
         * If the bar is STILL invisible on the FPGA with the mode forced
         * on (BOOTGATEOFF) and the write proven to land (s16_abread came
         * back green), then the master's in-game FB writes do not reach
         * the display no matter which bank they are in, and banks and
         * flips are both eliminated. This is the boot-time fliptest —
         * which WORKED on hardware — moved into the live pipeline. */
        {
            volatile uint32_t *px2 = (volatile uint32_t *)
                (0x24000000u + 0x200u + 8u * 320u);
            for (int i = 0; i < 8 * 320 / 4; i++)
                px2[i] = 0x01010101u;
        }
#endif
#ifdef CRAM_FLIP
        /* ARC B v2: drain slots that have seen TWO flips since their
         * remap — both banks now carry the new-pair pixels, so the
         * mapping switch cannot strand either bank (the frame-1500
         * bank probe: same art, +16 indices = one pair over, in ONE
         * bank only). memo_was_remap is 0 here, so cram_paint paints
         * live (no re-queue). Drain BEFORE the restore half: the
         * paints must beat the beam to the rows. */
        {
            volatile uint16_t *cram = &MARS_CRAM;
            cq_flipno++;
            for (unsigned i = 0; i < 32; i++) {
                uint8_t qn = cq_qn[i];
                if (qn && (uint8_t)(cq_flipno - cq_qstamp[i]) >= 2u) {
                    cram_paint(cram + cq_qbase[i], PAL_SH + cq_qoff[i],
                               cq_qbase[i], qn);
                    cq_qn[i] = 0;
                }
            }
        }
#endif
    }
    /* watched pages restore too: their stream may still
     * be mid-flight, and the truth was recaptured THIS
     * window (pg_pending |= pg_watch above), so the new
     * bank gets the exact base the game's next stores
     * expect — the stream continues seamlessly across
     * the bank swap. */
    /* (PG_STICKY tried restoring only hot pages here —
     * eyehold 8.18 -> 26.56: a watched page NOT restored at
     * the flip leaves the new bank's staging stale, and the
     * NEXT k2 capture reads that stale bank into truth —
     * the bank disease at k2. Capture-wide REQUIRES
     * restore-wide; the deep-watch k2 cost is inherent and
     * is bounded by pg_deep instead.) */
#ifdef VB_SPAN
    CEN[32] += (unsigned)__builtin_popcount(cycle_dirt & 0x1FFF);
    CEN[33] += (unsigned)__builtin_popcount(pg_watch & 0x1FFF);
    VBS(34);                             /* before the restore */
#endif
#ifdef PG_FRESH
    {
        uint16_t bm = (uint16_t)((cycle_dirt | pg_watch) & 0x1FFF);
        bm &= (uint16_t)~pg_fresh[fb_draw_par];
        restore_pages(bm);
        pg_fresh[fb_draw_par] |= bm;
    }
#else
    restore_pages((uint16_t)(cycle_dirt | pg_watch));
#endif
#ifdef PG_SKIP_PKT
    {
        /* md_pktA / md_pkt: the exact FB images the window wrote (the
         * #defines sit later in this file; the addresses are theirs). */
        volatile uint32_t *da = (volatile uint32_t *)0x24011A00u;
        volatile uint32_t *db = (volatile uint32_t *)0x2401E800u;
        if (mir_a == 0xB6B6u) {
            const uint32_t *sa = tp_lastA;   /* NOT the staging: the compose
                                              * rebuilds that after the ack */
            for (int i2 = 1; i2 < 368; i2++) da[i2] = sa[i2];
            da[0] = sa[0] | (disp_blank ? 0x2000u : 0u);
            DIAG[42]++;                  /* A carried across the swap */
        } else
            da[0] = 0;
#ifdef MD_SPR
        {   /* MDSPR palette + SAT: every window rewrites them, every vint
             * the 68K reads them; after a flip the new bank's copy is
             * two windows old. Replay the last written. */
            volatile uint32_t *dp = (volatile uint32_t *)0x2401EDC0u;
            volatile uint32_t *ds = (volatile uint32_t *)0x2401EE00u;
            for (int i2 = 0; i2 < 8; i2++) dp[i2] = tp_lastPal[i2];
            for (int i2 = 0; i2 < 64; i2++) ds[i2] = tp_lastSat[i2];
        }
#endif
        if (mir_b == 0xB6B6u) {
            const uint32_t *sb = tp_lastB;
            for (int i2 = 1; i2 < 368; i2++) db[i2] = sb[i2];
            db[0] = sb[0] | (disp_blank ? 0x2000u : 0u);
            DIAG[39]++;                  /* B carried across the swap */
        } else
            db[0] = 0;
    }
#endif
    VBS(35);                             /* after the restore */
#if defined(FB_TEXT_READ) && !defined(TWO_POST)
    /* text restore: unlike the sprite list (fully rewritten
     * every vint) the game writes text SPARSELY, so the new
     * draw bank is missing every glyph not rewritten since
     * that bank last showed. TEXT_U is complete truth as of
     * this cycle's capture; write it into the fresh bank so
     * (a) the next capture reads a complete region and
     * (b) the game's own text read-backs stay coherent —
     * the same contract restore_pages honours for tiles. */
    {
        volatile uint32_t *rd = (volatile uint32_t *)
            (0x04000000u + 0x1F000u);      /* cached-alias FB
                                            * writes, like the
                                            * blit: write-only,
                                            * rides the buffer */
        volatile uint32_t *rs = (volatile uint32_t *)TEXT_U;
        for (int i = 0; i < 1024; i += 4) {   /* full 4KB */
            rd[i + 0] = rs[i + 0];
            rd[i + 1] = rs[i + 1];
            rd[i + 2] = rs[i + 2];
            rd[i + 3] = rs[i + 3];
        }
    }
#endif
    cycle_dirt = 0;
#ifdef R60
    /* RESTORE-DONE echo: the 68K's push waits for THIS (not the F102
     * flip echo) — from here to the landing the master's only work is
     * the TCR landing-wait, on-chip and bus-quiet, so the FIFO drains
     * at the true DREQ rate instead of 4x slow under capture/restore
     * SDRAM traffic (the 0.24-lines/word handler autopsy). */
    MARS_SYS_COMM4 = 0xF103;
#endif
    return 1;
}

#ifdef VISR_FLIP
/* LOOP 24 — ISR-COST PROBE: the master takes its OWN V-interrupt every
 * vblank and runs the flip span from there, so the flip latches inside
 * vblank REGARDLESS of where the polled pickup lands. This is NOT
 * CMDINT back from the grave (LOOP11: "preemption moves latency to
 * band-completion" — where the cost was the YIELD machinery): there is
 * no yield and no resume protocol; the ISR is a bounded, in-order
 * detour and the interrupted compose strip continues untouched.
 *
 * Probe stage per LOOP24's risk register: body unchanged, 68K k2 spin
 * kept. The ISR therefore WAITS for the 68K's post instead of raising
 * FM itself — the 68K raises FM before posting on both k paths, so a
 * seen post guarantees FM=1, and its own entry gate (V in [DF,E8])
 * guarantees vblank. Worst-case wait is vint-entry latency (~9 lines
 * under FMGATE) + shim preamble; bound 700 FRT ticks (~15 lines) and
 * bail. Every bail leaves the cycle EXACTLY on the LOOP23 shipping
 * path: the body flips at pickup as before.
 *
 * Counters (58-63 reuse the retired CMD_PROBE family and 56
 * TILE_RATE's — those flags never coexist with this one; 49 reuses
 * SPAN_PROBE's, because 57 looked free and is NOT: the MD_BG tile
 * batcher counts tiles-sent there on every shipping build):
 *   [56] body-fallback flips  [49] ISR fires  [58] ISR flips
 *   [59] bail: COMM0 live at entry (previous window still open)
 *   [60] bail: no post inside the bound (idle/rejected vint)
 *   [61] non-k2 post seen (k1 — return, body handles it)
 *   [62] max ISR ticks on flip vints   [63] sum (mean = 63/58) */
#ifdef K2_FREE
RAMCODE static void dreq_rearm(int k);   /* defined below; the ISR arms */
#endif
static volatile uint8_t visr_arm;        /* pipeline initialised; before
                                          * this the ISR returns at once */
#ifdef R60
static volatile uint8_t visr_park;       /* announce seen: a landing is
                                          * in flight — the poll loop
                                          * parks until window pickup */
#endif
static volatile uint8_t visr_flip_done;  /* set by the ISR after the span,
                                          * consumed by the body at pickup
                                          * of the same k2 window */

void visr_vbi(void)
{
    if (!visr_arm)
        return;
    DIAG[49]++;
#ifdef BOOT_FRTCHK
    { static uint16_t t_last; uint16_t t = frt(); *(volatile uint16_t *)0x2000402C = (uint16_t)(t - t_last); t_last = t; }
#endif
#ifdef BOOT_VISRCHK
    *(volatile uint16_t *)0x2000402A = (uint16_t)DIAG[49];   /* probe: V-ISR count on COMM10 */
#endif
    /* VBLANK COUNT on COMM14 (2026-09-06): free after the boot handshake
     * (68K B007 -> master B008); the 68K stamps it at vint entry and at
     * the game's rte so a handler span is read unambiguously in frames.
     * 0xB1xx: the 68K's boot hold accepts any 0xB1xx as "armed". */
    if (MARS_SYS_COMM14 != 0xB007)
        MARS_SYS_COMM14 = (uint16_t)(0xB100 | (DIAG[49] & 0xFF));
    uint16_t t0 = frt();
#ifdef K2_FREE
    visr_t0 = t0;
#ifdef FLIP_CENSUS
    CEN[18]++;                           /* V-ISR entries = denominator */
#ifdef NT_PROBE
    CEN[49]++;
#endif
#ifdef CSET_CENSUS
    cs_flush();                          /* per VINT, the real cadence */
#endif
#ifdef FLIP_RATE_POST
    /* HARDWARE READOUT OF THE FLIP RATE. The FS-write census is SH-2
     * side and the only channel off the FPGA is a screenshot, which is
     * 68K side. So post the count of FS writes in the last 64 V-ISRs on
     * COMM8 in the 0xBBxx form the 68K already latches and clears, and
     * let the value instrument flood it:  64 = every vint, 27 = 27 Hz,
     * 1 = the ares FBXPORT figure. Biased into bit 7 so black cannot be
     * mistaken for a reading. */
    {
        static uint16_t fr_n, fr_base, fr_val;
#ifdef FLIPRATE_DIAG
        /* LOOP29 143: post a DIAG slot's delta instead of FS writes
         * (44 = edge declines, 58 = ISR flips, 60 = no-post bails). */
#define FR_SRC DIAG[FLIPRATE_DIAG]
#else
#define FR_SRC CEN[17]
#endif
#ifdef FLIPRATE_MEANSUM
        /* LOOP29 143: post a MEAN: CEN[MEANSUM] accumulates lines,
         * CEN[51] counts the samples (both stamped in flip_span). */
        static uint16_t fr_cbase;
        if (++fr_n >= 64) {
            uint16_t sn = (uint16_t)(CEN[FLIPRATE_MEANSUM] - fr_base);
            uint16_t cn = (uint16_t)(CEN[51] - fr_cbase);
            fr_val = cn ? (uint16_t)(sn / cn) : 0;
            fr_base = (uint16_t)CEN[FLIPRATE_MEANSUM];
            fr_cbase = (uint16_t)CEN[51];
            fr_n = 0;
        }
#else
        if (++fr_n >= 64) {
            fr_val = (uint16_t)(FR_SRC - fr_base);
            fr_base = (uint16_t)FR_SRC;
            fr_n = 0;
        }
#endif
        /* post when the channel is free OR already carries our own value:
         * on the baseline build COMM8 is busy enough with the BAxx heal
         * traffic that a free-only test never fired on hardware, and the
         * 68K flooded its "never posted" fallback (42) instead. Ours is
         * cleared by the 68K each time it latches, so this never parks a
         * value on the channel — the failure mode that blacked the
         * screen earlier today. */
        {
            uint16_t c8 = MARS_SYS_COMM8;
            if (c8 == 0 || (c8 & 0xFF00u) == 0xBB00u)
                MARS_SYS_COMM8 = (uint16_t)(0xBB80 | (fr_val > 63 ? 63 : fr_val));
        }
    }
#endif
#endif
#ifdef HS_CENSUS
    hsc_win++;
#endif
#endif
    uint16_t c0 = MARS_SYS_COMM0;
    if (c0) {
        DIAG[59]++;                      /* stale: a live window means the
                                          * body owns the FB right now (and
                                          * on overrun the 68K defers this
                                          * vint's post anyway) */
        return;
    }
#if defined(TEXTCAP_EARLY) && defined(TEXTCAP_SLAVE)
    /* TEXT CAPTURE POSTED AT ISR ENTRY (LOOP29 145). On the FPGA the
     * master's own FB reads between entry and the guard cost ~20 lines
     * (144) and the flip misses the guard almost every vint (143). The
     * slave waits for FM itself and copies while the master waits for
     * the 68K's post; flip_span only joins. The game writes no text
     * between here and the post (it is inside the shim), so the truth is
     * the same bytes the after-post capture read. */
    fs_posted_early = 1;                 /* cleared by the body at window pickup */
    SYNC[6] = 0;
    SYNC[4] = 0x4000;
#endif
#ifdef FLIP_DEFER
    int deferred_flipped = 0;
    /* DEFERRED COMMIT: a flip armed last vint outside vblank lands HERE,
     * at the top of this one, where visr_t0 is fresh so flip_span's edge
     * guard passes by construction. COMM0 is clear (checked above), so no
     * window is live and the FB is ours — the same precondition the
     * window path relies on. The frame in the hidden bank was finished
     * last vint; nat_shipped is still set (the decline returned before
     * that gate), so the whole-frame gate accepts it. If flip_span
     * declines again for a REAL reason (no fresh blit), the arm is spent
     * and this vint falls through to the normal window path. DIAG[5]
     * counts commits; DIAG[5] vs DIAG[6] is the deferral's yield. */
    if (flip_deferred) {
        flip_deferred = 0;
#ifdef VB_SPAN
        vbs_isr = 1;
#endif
        if (flip_span()) {
            visr_flip_done = 1;
            deferred_flipped = 1;        /* this vint's flip is spent */
            DIAG[5]++;
            DIAG[58]++;                  /* an ISR flip like any other */
        }
    }
    /* DO NOT RETURN HERE. The loop below services the k1 announce
     * (COMM6 0xB101 -> dreq_rearm + the 0xA001 echo), and under ARMGATE
     * the 68K pushes ONLY after that echo. Returning on the commit
     * starved every push: measured 3.4% speed, the game barely
     * advancing. Fall through, arm and echo as usual, and skip only the
     * second flip. */
#endif
    do {
        c0 = MARS_SYS_COMM0;
        if (c0)
            break;
#ifdef R60
        /* paced spin: the announce-armed wait overlaps the 68K's push --
         * keep the adapter bus clear for the DMAC between checks */
        {
            uint16_t tq = frt();
            while ((uint16_t)(frt() - tq) < 16) ;
        }
#endif
#ifdef K2_FREE
        /* k1 PRE-ANNOUNCE (COMM6): the 68K's k1 post sits behind its
         * entry consumes (mean ~12, max ~55 lines) — no sane wait
         * catches it, and a missed arm kills that cycle's packet. The
         * announce fires at 68K ENTRY, pre-consume: arm, echo for the
         * push gate, done — k1 needs nothing else from this ISR. */
        if (MARS_SYS_COMM6 == 0xB101) {
            MARS_SYS_COMM6 = 0;
            dreq_rearm(1);
            MARS_SYS_COMM4 = 0xA001;
            DIAG[61]++;                  /* k1 family */
#ifdef R60
            SYNC[12] = 1;                /* SLAVE PARK broadcast: the
                                          * slave's service points spin
                                          * bus-quiet while a landing is
                                          * in flight (self-chain removed
                                          * the idle gaps that used to
                                          * provide this quiet for free —
                                          * rejects 2.5->12.7% proved
                                          * they were load-bearing) */
            visr_park = 1;               /* landing incoming: the poll
                                          * loop parks bus-quiet until
                                          * pickup; push drains against
                                          * an idle master */
            /* (pass-10 slave-park flag REMOVED: its consumer died with
             * the park, and 0x28FC0 is MD_PAYOFF's rows counter — the
             * dead writes were clobbering it.) */
            /* FM_TEST (read half): can the SH-2 READ the FB at FM=0?
             * If yes, the pre-flip capture span (cap_drain, mean ~17
             * lines, the DIAG[44] decline driver) can move HERE and
             * overlap the 68K's consumes. Probe: read a packet word
             * the 68K never writes (FM=0, now) and again on the post
             * path (FM=1); [24] match / [25] mismatch. */
            visr_fm0_rd = *(volatile uint16_t *)0x24011A04u;
            visr_fm0_have = 1;
            /* R60: announce and post share the SAME vint — keep
             * polling for the post (the flip). Returning here was why
             * the ISR never flipped: it went home before the post
             * arrived (flips=0, fallback=every frame). */
#else
            return;
#endif
        }
#endif
#ifdef R60
    } while ((uint16_t)(frt() - t0) < 1650);
    /* R60: the shim consumes the MD-plane packets AND runs its push
     * between announce and post (push-before-post: the push drains
     * against this very spin — SDRAM-quiet — per the LOOP25 law), so
     * the wait covers consumes+push (~12-17 lines typical); the flip
     * budget still holds (edge guard at 1748 is the law). */
#else
    } while ((uint16_t)(frt() - t0) < 700);
#endif
    if (!c0) {
        DIAG[60]++;
        return;
    }
#ifdef K2_FREE
    if ((c0 & 0xFFCF) != 0x2000) {       /* not a window post */
        DIAG[61]++;
        return;
    }
    {
        /* THE ISR OWNS ARMING (LOOP24): the 68K pushes right after its
         * post — before the body could possibly arm — and a body rearm
         * would reset TCR under a landing in flight. Per-k buffers:
         * k1 -> SPR_LAND (records may not be clobbered before the k2
         * snapshot), k2 -> SPR_LAND_K2. */
        int vk = (c0 >> 4) & 3;
        /* consume any co-arrived k1 announce: exiting via the post
         * branch used to leave it on COMM6, and the NEXT vint's ISR
         * (k2!) would arm k1 and skip the flip — 342 fallbacks and a
         * records-clobber path on the first pre-announce run */
        MARS_SYS_COMM6 = 0;
#ifndef R60
        dreq_rearm(vk);
#endif  /* R60: the ANNOUNCE is the only armer — the post follows the
         * announce in the same vint and a re-arm here would reset TCR
         * under the landing already in flight */
        MARS_SYS_COMM4 = (uint16_t)(0xA000 | vk);
        /* ARM ECHO: the 68K pushes only after seeing this (it cleared
         * COMM4 before posting). An unarmed push wedges MAME's 68K —
         * defer_access blocks the FIFO write forever with nothing
         * draining — and silently drops words on ares. COMM4 is free
         * here: the S_OK boot handshake is long done and IDLE_TOKEN
         * (its other tenant) is excluded from K2_FREE builds. */
        if (vk != 2) {
            DIAG[61]++;
            return;
        }
#ifdef R60
        if ((uint16_t)(frt() - t0) > 1650) {
#else
        if ((uint16_t)(frt() - t0) > 700) {
#endif
            DIAG[47]++;                  /* k2 seen too late to flip in
                                          * vblank: armed only, the body
                                          * fallback owns the flip (its
                                          * heartbeat gate decides) */
            return;
        }
    }
#else
    if (c0 != 0x2020) {                  /* window signature 0x2000|k<<4;
                                          * only k2 flips */
        DIAG[61]++;
        return;
    }
#endif
#ifdef FLIP_DEFER
    if (deferred_flipped)
        return;                          /* already flipped at the top of
                                          * this vblank; the window was
                                          * still armed and echoed above,
                                          * which is all the 68K needs */
#endif
#ifdef VB_SPAN
    vbs_isr = 1;
#endif
#ifdef TWO_POST
    /* TWO-POST PROTOCOL (LOOP29 149): post A came before the 68K's
     * consumes, so the flip lands at ~5 lines; now hand the FB back for
     * its DMAs and its packet blast (both need FM=0), and eat post A so
     * the body does not open a window on it. The 68K posts B after. */
    {
        int fsr;
#ifdef FLIPRATE_MEANSUM
        fs_from_isr = 1;
#endif
        fsr = flip_span();
#ifdef FLIPRATE_MEANSUM
        fs_from_isr = 0;
#endif
        MARS_SYS_COMM0 = 0;
        MARS_SYS_INTMSK &= 0x7FFF;
        MARS_SYS_COMM4 = fsr ? 0xF104 : 0xF1FE;   /* the 68K's cue: FB handed
                                                    * back, post A eaten. F102
                                                    * came too early (before
                                                    * the mirror and the drop)
                                                    * and raced post B. */
        if (!fsr) return;
    }
    if (0)
#endif
#ifdef FLIPRATE_MEANSUM
    fs_from_isr = 1;
    { int fsr = flip_span(); fs_from_isr = 0; if (!fsr) return; }
    if (0)
#endif
    if (!flip_span())
        return;                          /* declined: body sees the flag
                                          * clear, tries later, declines
                                          * again on the same guard — a
                                          * clean frame drop */
    visr_flip_done = 1;
    DIAG[58]++;
    {
        uint16_t dt = (uint16_t)(frt() - t0);
        if (dt > DIAG[62])
            DIAG[62] = dt;
        DIAG[63] += dt;
    }
}
#endif

#ifndef DIRECT_FB
/* 2026-09-02: cart ROM — once-per-window dispatch on the slave (88B
 * freed for the C1 pen fixes; region guard). */
void slave_window_k(uint16_t cmd)
{
    int k = (cmd >> 4) & 3;
    int skip = (cmd >> 3) & 1;                   /* master lost vblank: no blit */
#ifdef BLIT_SKIP
    /* FB draw-bank label, MASTER-OWNED, published in bit 6 of the blit
     * command. Bit 3 is the vestigial `skip` field above and bits 0-2
     * are bank1 (the game's TILE bank, nothing to do with the FB), so 6
     * is the first genuinely free bit. NEVER re-derive this from FBCTL
     * here — see the fb_draw_par note. */
    int bank = (cmd >> 6) & 1;
#endif
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
     * docs/log/LOOP.md iteration 1b findings. */
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
#ifdef BLIT_SOLO
    /* CONTENTION PROBE, TIMING ONLY — `make ... BLITSOLO=1 WINSPLIT=1`.
     * The slave does not blit at all, so the MASTER's blit runs with no
     * concurrent FB traffic from the other SH-2. Compare its ticks/row
     * against the same build without this flag: if the master gets
     * materially faster, the blit's large per-row fixed cost is BUS
     * CONTENTION between the two CPUs writing the same framebuffer, and
     * the lever is scheduling (stagger the halves) rather than writing
     * fewer bytes. If it does not move, contention is exonerated and the
     * cost is the loads or the per-row setup.
     * *** THE PICTURE WILL BE WRONG: the slave's rows never ship, so
     * half the screen is stale or black. Read ticks/row, ignore the
     * screen, never judge feel on this. *** */
    (void)skip;
#else
    if (!skip) {
#ifdef WIN_TWO
        /* even split: k1 = rows 0-56, k2 = rows 112-168 (master takes
         * 56-112 / 168-224). */
        if (k == 2) {
#ifdef R60
            /* BLIT_SHIFT (pass-8 profile): the blit runs INSIDE the FM
             * window on both CPUs, and the master pays 777 cyc/row vs
             * the slave's 366 (its cap_drain FB reads interleave with
             * the blit writes) — 87K vs 41K cycles/frame for the same
             * 112 rows. Rows moved to the slave shorten the window
             * (the 68K handler tax) and free master gap time for the
             * band drain (the deferral tear). Same archaeology-shaped
             * knob as BANDSHIFT: `make BLITSHIFT=N`. */
            BLIT_HALF(0, 112 + BLIT_SHIFT, bank);  /* R60 slave half */
#else
            BLIT_HALF(112, 168, bank);
#endif
        } else {
            BLIT_HALF(0, 56, bank);
        }
#else
        if (k == 2)
            BLIT_HALF(0, 36, bank);
        else if (k == 0)
            BLIT_HALF(72, 108, bank);
        else
            BLIT_HALF(144, 184, bank);
#endif
    }
#endif
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
#endif /* !DIRECT_FB — slave_window_k served only the slice blit */

/* LOOP 10: the band whose cat1+text is owed to the NEXT window gap.
 * SLAVE-ONLY state — written and read exclusively inside
 * slave_concurrent_k, so there is no cross-CPU coherency question. */
#ifndef NOCAT1DEFER
#define NOCAT1DEFER 0
#endif
#ifdef MD_BG
/* Packet staging: built in the post-ack gap, published in-window
 * by a bare copy. LOOP15: moved from .bss (1536B) to the 0x3E780
 * fixed block — Phase B's builder code pushed _end 656 bytes over
 * the guard, and this buys it back with margin. Master-only,
 * CPU-built and CPU-copied: cached view is correct. Boot init not
 * needed (header words are written every window before publish).
 * PLACEMENT IS STACK-CRITICAL: the first cut put md_pkt's palette
 * words 192 bytes below the 0x3F000 master stack top and the stack
 * CLOBBERED them (green title, correct shapes — CRAM garbage).
 * md_pkt now sits at the block's bottom (0x3E780..0x3ED7F), leaving
 * a stack red-zone above; the NT_WRAP trackers moved to the
 * 0x3E380 gap below tile_grp. THE RED ZONE WAS OVERRUN AGAIN under
 * R60 (640B vs 2176B measured depth — the whole packet transited by
 * stack frames, B channel dead); master stack top moved 0x3F000 ->
 * 0x3F800, red zone now 2688B. */
#define md_pkt ((uint16_t *)0x0603E780)
#endif
static uint8_t cat1_valid, cat1_lo, cat1_hi, cat1_bank, cat1_par;
static uint8_t cat1_rg;                  /* band the record belongs to —
                                          * names the SYNC[8] bit to clear
                                          * when the deferred pass lands */
static uint8_t cat1_t0, cat1_t1;

/* SLAVE LANDING PARK: MEASURED DEAD (pass 10) — per-strip parking on
 * the landing flag moved tears NOT AT ALL (150 -> 149) and cost 300+
 * ISR flips (bands finish later, stale bails up). The strip is the
 * atomic contention unit and the park only fires between strips. The
 * 0x28FC0 flag writes remain (cheap, may serve a later design); the
 * park helper is deleted. Do not re-add without striping finer. */
#ifdef CHAIN_METER
/* link-wall phase meter helper — cart .text ON PURPOSE: RAMCODE bytes
 * are SDRAM bytes and the region guard is at zero. [0] head cat1 drain
 * [1] clear [2] sprites [3] rg2 inline cat1+text [4] links. The block
 * at 0x3A790 sits in the map-audited 0x3A78C-0x3A7D8 gap. */
static void phase_add(int i, uint16_t *t0)
{
    uint16_t t = frt();
    ((volatile uint32_t *)0x2603A790)[i] += (uint16_t)(t - *t0);
    *t0 = t;
}
#endif
#ifdef ROW_GEN
/* Sprite spans of the CURRENT snapshot: built inside the snapshot copy
 * loop (rg_spcur) from words already in registers — a separate pass
 * over SPR_SNAP was 128 uncached reads per generation. A generation
 * without a fresh landing keeps the previous snapshot, and its spans. */
RAMCODE static void rowgen_spans(uint32_t *sp)
{
    for (int i = 0; i < 8; i++) sp[i] = rg_spcur[i];
}
/* At launch (master, nat_window_launch, after latch_layer_regs and
 * the snapshot copy, before the slave is posted). Cart .text: once
 * per generation. */
/* The tilemap pages the FG (cat-1) layer can show: primary + alternate
 * quadrants of the latched regs. A content change in any other page
 * cannot reach the FB (the BG lives on the MD planes). */
static inline uint32_t rowgen_fgpages(void)
{
    const layer_regs *fg = &snap[0];
    uint32_t m = 0;
    for (int i = 0; i < 4; i++)
        m |= (1u << (fg->pq[i] & 15)) | (1u << (fg->pq_a[i] & 15));
    return m;
}
/* What the compose reads to turn a tile/text pen into an sbuf byte for
 * parity par: the group maps. A change here changes sbuf bytes with no
 * pixel moving, so it is an all-rows event for that parity. Sprites
 * (spr_pair) need no signature: sprite rows are always in the span set. */
RAMCODE static uint32_t rowgen_palsig(int par)
{
    const uint32_t *t = (const uint32_t *)tile_grp[par];
    const uint32_t *x = (const uint32_t *)text_grp[par];
    uint32_t h = 0;
    for (int i = 0; i < 32; i++)
        h = ((h << 1) | (h >> 31)) ^ t[i];
    h = ((h << 1) | (h >> 31)) ^ x[0];
    h = ((h << 1) | (h >> 31)) ^ x[1];
    return h;
}
RAMCODE static void rowgen_build(int par)
{
    uint32_t d[8], sp[8], pend[8], pgcap;
    uint32_t f;
    {   /* text rows whose glyph words changed since the last build.
         * Read through the CACHED alias right after the launch's
         * cache_purge (the capture wrote TEXT_U uncached). Kept OUT of
         * text_capture: anything added to the pre-flip path trips the
         * flip edge guard (DIAG[44]) and moves flips into the body
         * (the uncached compare there: ISR flips 1485 -> 38).
         * rotl-xor, not a sum: a digit swap (1200 -> 2100) permutes
         * words and a sum would miss it. */
        const uint32_t *tc = (const uint32_t *)TEXT_C;
        for (unsigned t = 0; t < 28; t++) {
            uint32_t h = 0;
            const uint32_t *r = tc + t * 32;
            for (int i = 0; i < 32; i += 4) {
                h = ((h << 1) | (h >> 31)) ^ r[i + 0];
                h = ((h << 1) | (h >> 31)) ^ r[i + 1];
                h = ((h << 1) | (h >> 31)) ^ r[i + 2];
                h = ((h << 1) | (h >> 31)) ^ r[i + 3];
            }
            if (h != rg_txth[t]) {
                rg_txth[t] = h;
                RG_MARK_SPAN(t * 8, t * 8 + 8);
            }
        }
    }
    {
        uint32_t ps = rowgen_palsig(par);
        if (ps != RG_PALSIG[par & 1]) { RG_PALSIG[par & 1] = ps; RG_COUNT[6]++; RG_FLAGS |= 1; }
        RG_LSIG = ps;
        RG_LPAR = (uint32_t)(par & 1);
    }
    {   /* the FG layer's latched registers moved -> the cat-1 pass
         * output may shift anywhere: all rows. FG ONLY: the BG's regs
         * change every frame the sky parallax moves and the BG is on
         * the MD planes, not in the FB. */
        const uint32_t *w = (const uint32_t *)&snap[0];
        uint32_t h = 0;
        for (unsigned i = 0; i < sizeof(layer_regs) / 4; i++)
            h = ((h << 1) | (h >> 31)) ^ w[i];
        if (h != RG_LRHASH) { RG_LRHASH = h; RG_COUNT[7]++; RG_FLAGS |= 1; }
    }
    /* consume the pending marks with V masked: text_capture marks and
     * cap_page's content flags come from the flip ISR too and must not
     * land between the read and the clear */
    __asm__ __volatile__("mov #-16,r0\n\tldc r0,sr" ::: "r0", "memory");
    for (int i = 0; i < 8; i++) { pend[i] = RG_PEND[i]; RG_PEND[i] = 0; }
    f = RG_FLAGS; RG_FLAGS = 0;
    pgcap = RG_PGCAP; RG_PGCAP = 0;
    __asm__ __volatile__("mov #32,r0\n\tldc r0,sr" ::: "r0", "memory");
    if (pgcap & rowgen_fgpages()) { f |= 1; RG_COUNT[5]++; }   /* FG page content */
    rowgen_spans(sp);
    for (int i = 0; i < 8; i++) {
        d[i] = (f & 1) ? 0xFFFFFFFFu : (pend[i] | sp[i] | RG_SPPREV[i]);
        RG_SPPREV[i] = sp[i];
        RG_ACC(0)[i] |= d[i];
        RG_ACC(1)[i] |= d[i];
    }
    if (f & 1) RG_COUNT[3]++;            /* all-dirty generations */
    RG_COUNT[4]++;
}
/* At ship start (master, before SYNC[4] posts the slave half): publish
 * the row set for bank b and open a fresh accumulator. The deferred
 * masks are written only by a blit that is not running now. */
RAMCODE static void rowgen_ship(int b)
{
    unsigned skipped = 0;
    /* a page capture or a flag that landed AFTER this generation's
     * launch may already be in its composed rows: ship everything
     * (the flag itself is consumed by the next build, which then
     * carries the other bank) */
    uint32_t late = ((RG_FLAGS & 1) || (RG_PGCAP & rowgen_fgpages())
                     || rowgen_palsig((int)RG_LPAR) != RG_LSIG)
                    ? 0xFFFFFFFFu : 0u;
    if (late) RG_COUNT[8]++;
    for (int i = 0; i < 8; i++) {
        uint32_t m = RG_ACC(b)[i] | RG_PEND[i] | late
                   | RG_DEFER(0, b)[i] | RG_DEFER(1, b)[i];
        if (i == 7) m |= 0xFFFFFFFFu; /* bits 224-255: no rows */
        RG_SHIPM[i] = m;
        RG_ACC(b)[i] = 0;
        RG_DEFER(0, b)[i] = 0;
        RG_DEFER(1, b)[i] = 0;
        skipped += (unsigned)__builtin_popcount(~m);
    }
    RG_COUNT[0] += skipped;              /* rows this ship will not read */
}
#endif
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
     * against 0.21 for the other two — docs/log/LOOP.md negative 25).
     * rg = k instead: the outstanding compose is then R(k-1) against a
     * ship of R(k+1), disjoint in every window, and each band gets TWO
     * window-gaps to compose. No second compose slot is needed — the
     * post-ack drain still bounds it after one gap; only the SHIP moves
     * a window later. Cost: one window (~1 frame) of extra latency,
     * applied UNIFORMLY, so the spread between bands — which is what the
     * seams are made of — is unchanged. */
    int rg = k;                                  /* W0->R0, W1->R1, W2->R2 */
#ifdef CHAIN_METER
    uint16_t phm_entry = frt();
#endif
#define PHASE(i) do { } while (0)   /* decomposition banked 2026-08-26:
                                     * 17/12/35/36; stamps retired for
                                     * guard bytes */
    int lo = rg * 72;
    int hi = (rg == 2) ? (184 + BAND_SHIFT_RG2)
                       : (lo + 36 + BAND_SHIFT);         /* slave rows */
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
        PHASE(0);
#ifdef ROW_DEFER
        SYNC[8] &= (uint16_t)~(1u << cat1_rg);  /* band complete: its
                                                 * deferred cat1+text just
                                                 * landed — rows shippable */
        SYNC[10] &= (uint16_t)~(1u << cat1_rg); /* cat1-owed mask clear */
#endif
    }
#ifdef ROW_DEFER
    /* ROW-DEFER (2026-08-25, the purple conviction — docs/design/REBUILD.md): from
     * here until this band's cat1+text land (next link's drain above,
     * or the rg==2 inline drain below), the band's sbuf rows pass
     * through a cleared-not-yet-redrawn state. A blit slice that ships
     * them writes MD-through zeros over the bank's coherent last frame
     * — the purple band (MD plane-B junk, lines 192-215) and the
     * lower-third sprite dropouts. Publish "open" so blit_half defers
     * these rows; the bank then keeps last frame's coherent pixels,
     * one band one frame late — the complete-or-defer policy the band
     * queue already applies, enforced at the ship. SYNC[8] is slave-
     * written only (master mask is SYNC[9]) — no cross-CPU RMW. */
    SYNC[8] |= (uint16_t)(1u << rg);
#endif
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
#if defined(DIRTY_ROW) && !defined(DIRECT_FB)
            /* STAGE B, free once ROWLIVE exists: a row that is already
             * all zeros does not need clearing to all zeros. 336 bytes
             * of SDRAM writes per skipped row, on the COMPOSE side —
             * outside the 68K's window, so it will not show in the
             * handler mean, but it is the slave's time and the slave is
             * what job 3 wants to spend.
             * DIRECT_FB DISABLES THIS SKIP: ROWLIVE says "the row went
             * all-zero LAST interval", but the dst bank is TWO intervals
             * stale — the same two-fact rule as blit_half's whole-row
             * exit. Erase-tightening is stage 2's job. */
            if (!ROWLIVE[8 + r])
                continue;
#endif
#ifdef DIRECT_FB
            uint8_t *d = DROW(8 + r);
            RL_ZERO(8 + r);                    /* MARK-FIRST, as below */
            for (int x = 0; x < 320; x += 4)   /* LONG fills: the FB
                                                * DISCARDS zero BYTE
                                                * writes */
                *(uint32_t *)(d + x) = 0;
#else
            uint8_t *d = &sbuf[(8 + r) * SBUF_W];  /* sbuf row = screen
                                                    * row + 8, per
                                                    * blit_half */
            RL_ZERO(8 + r);                    /* MARK-FIRST: see the
                                                * pass-12c note at the
                                                * master clear — RL_ZERO
                                                * precedes the wipe so a
                                                * racing deferred draw's
                                                * MARK always lands last */
            for (int x = 0; x < SBUF_W; x += 4)
                *(uint32_t *)(d + x) = 0;
#endif
        }
        slave_service_stream();
    }
    PHASE(1);
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
#ifdef ROW_DEFER
    if (rg)
        SYNC[10] |= (uint16_t)(rg == 2 ? 4u : 8u);
                                         /* bit 3 = R1 STRIP-PHASE
                                          * (2026-08-30, Mike's 2214, the
                                          * 2.4x blink regression): R1's
                                          * rows are marked during the
                                          * strips but the owed bit only
                                          * publishes at call end — bit 3
                                          * covers that gap; the ship
                                          * gates on it like R2's.
                                          * bit 2 = R2 MID-BAND (2026-08-30,
                                          * Mike's frame 3035): from the
                                          * first sprite strip until the
                                          * inline cat1+text land, R2's
                                          * rows are MARKED but grass-
                                          * less; a window firing in
                                          * this ~15-line gap shipped
                                          * them (ROW_DEFER passes
                                          * marked rows). R2-scoped
                                          * ONLY: R0/R1's owed gap is
                                          * chronic (attempt 4's
                                          * measured failure). */
#endif
    for (int y = lo; y < hi; y += 12) {      /* 12: finer strips multiply
                                              * the full-height row-walk
                                              * of tall zoomed actors */
        int ye = (y + 12 > hi) ? hi : y + 12;
        compose_sprites(y, ye, par);
        slave_service_stream();
    }
    PHASE(2);
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
    st_s(11);
    if (NOCAT1DEFER) {   /* A/B ARM: cat1+text inline at the TAIL, the
                          * pre-LOOP-10 order. Costs the strobe win back;
                          * exists only to test whether the deferral is
                          * what took playability.
                          * NATIVE: this is the SHIPPING arm, and the
                          * slave composes the FULL text range — sbuf has
                          * exactly ONE writer during a generation, which
                          * deletes the master-text-vs-slave-clear race
                          * outright (the master tail is maps-only). */
        compose_layer(lo, hi, 1, 0, 0, bank1, par, 2);
        slave_service_stream();
        st_s(12);
#ifdef NATIVE_FRAME
        /* slave text ends where the master's rows begin (NAT_TB):
         * full range under BANDSHIFT=36, split under the rebalance */
        compose_text((rg == 0) ? 0 : (rg == 1) ? 9 : 18,
                     NAT_TB(rg), par);
#else
        compose_text((rg == 0) ? 0 : (rg == 1) ? 9 : 18,
                     (rg == 0) ? 4 : (rg == 1) ? 13 : 23, par);
#endif
        slave_service_stream();
#ifdef ROW_DEFER
        SYNC[8] &= (uint16_t)~(1u << rg);    /* inline arm: complete here */
#endif
    }
    cat1_lo   = (uint8_t)lo;
    cat1_hi   = (uint8_t)hi;
    cat1_bank = (uint8_t)bank1;
    cat1_par  = (uint8_t)par;
    cat1_rg   = (uint8_t)rg;
    cat1_t0   = (uint8_t)((rg == 0) ? 0 : (rg == 1) ? 9 : 18);
    cat1_t1   = (uint8_t)((rg == 0) ? 4 : (rg == 1) ? 13 : 23);
    cat1_valid = 1;
#ifdef WIN_TWO
    /* THE LAST BAND HAS NO SUCCESSOR INSIDE THE CYCLE. WIN_TWO launches
     * all three slave halves at k2 and CHAINS them, so R2's owed cat1
     * would not run until the NEXT cycle's first call -- after R2 has
     * already shipped. Rows 144..183 therefore lost their over-sprite
     * tiles every frame: the arcade's tall grass blades at the foot line
     * came out a flat strip (Mike, 2026-08-17), and the man->beast
     * transition lost its foreground there too.
     * The deferral's own premise ("a full window gap sits in front of
     * it", LOOP 10) never applied to this band under WIN_TWO -- there is
     * no gap after the last chain link, only the ship. So draining it
     * here costs the strobe nothing it was actually buying, and R0/R1
     * keep the deferral untouched. */
    if (!NOCAT1DEFER && rg == 2) {
        /* this band's own params are still live -- no reload through
         * the cat1_* record, and rg==2 pins the text rows to 18..23 */
        compose_layer(lo, hi, 1, 0, 0, bank1, par, 2);
        slave_service_stream();
#ifndef MD_BG_TEXT
        compose_text(18, 23, par);
        slave_service_stream();
#endif
        cat1_valid = 0;
        PHASE(3);
#ifdef ROW_DEFER
        SYNC[8] &= (uint16_t)~4u;        /* R2 complete inline: shippable */
        SYNC[10] &= (uint16_t)~4u;       /* mid-band hazard cleared */
#endif
    }
#endif
#ifdef ROW_DEFER
    /* ATTEMPT 4 (2026-08-29, Mike's frames 1600/1601: the GRASS layer
     * — FG cat1, drawn OVER sprites — blinking off for a frame, and
     * the residual thin bottom strip): a band in the "sprites done,
     * cat1 OWED to the next gap" state has MARKED rows, so the
     * shipped ROW_DEFER gate (unmarked-only) lets them ship without
     * their over-sprite tiles. Attempt 1 (defer whole open span) was
     * 58.7 rows/cycle — this publishes only the OWED window on its
     * own mask (SYNC[10], the dead queued-chain slot; #error below
     * keeps it honest): set when a band records its deferred cat1,
     * cleared when the drain lands. The blit defers ALL rows of an
     * owed band — a slipped ship costs one frame of lateness instead
     * of a grass-less band. */
#if defined(QUEUED_CHAIN)
#error "ROW_DEFER's cat1-owed mask reuses SYNC[10] - QCHAIN conflicts"
#endif
    if (cat1_valid)
        SYNC[10] |= (uint16_t)(1u << cat1_rg);
    SYNC[10] &= (uint16_t)~8u;           /* R1 strip-phase window closed
                                          * (owed bit takes over, or the
                                          * band had no cat1 to owe) */
#endif
#ifdef CHAIN_METER
    ((volatile uint32_t *)0x2603A790)[4] += 1;
    ((volatile uint32_t *)0x2603A790)[5] +=
        (uint16_t)(frt() - phm_entry);       /* true wall, entry->exit */
#endif
#ifdef PICKUP_SRC_PROBE
    slave_in_compose = 0;
#endif
}

/* QUICK CLAIM (2026-09-05, Zeus's first frames / the black hit-flash
 * frame): a set that first appears in THIS snapshot has no pair when the
 * slave starts composing, so its pixels get pair-15 indices (the dark
 * shadow pair) for the whole generation — the pair map is built by
 * bm_tail in the maps drain, after launch. CRAM is read at scanout, so
 * apply_cram later in the window paints anything mapped here in time;
 * only the INDICES must be right at launch. Map a held or free pair now,
 * the same way bm_tail does (held first; free = pr_key and both group
 * halves empty, 14 downward). ROM-resident, once per launch, <= 24
 * records. */
#define QCLAIM_N (*(volatile uint32_t *)0x26028FB4)
/* LOOP 27 q4 ramp census timing flags (uncached so the slave sees them
 * live): which parity bm_tail is rebuilding right now (0xFF idle), and
 * whether this cycle's late claim has completed for the composing par. */
__attribute__((noinline))
static void nat_quick_claim(int par)
{
    for (unsigned i = 0; i < 24; i++) {
        volatile uint16_t *d = SPR_SNAP + i * 8;
        uint16_t d2 = d[2], d0 = d[0];
        unsigned sc;
        int p = -1;
        if (d2 & 0x8000)
            break;
        if ((d2 & 0x4000) || (d0 & 0xFF) >= (d0 >> 8))
            continue;
        sc = d[4] & 0x3F;
        if (sc == 0x3F || spr_pair[par][sc] != 0xFF)
            continue;
        for (int q = 1; q < 15; q++)
            if (pr_key[q] == sc) { p = q; break; }
        if (p < 0)
            for (int q = 14; q >= 1; q--)
                if (pr_key[q] == 0xFF && grp_key[2 * q] == 0xFF
                    && grp_key[2 * q + 1] == 0xFF) { p = q; break; }
        if (p < 0) {
            /* no free pair: take the STALEST held pair (bm_tail's LRU
             * rule) — a hit-flash set is one frame long and black is
             * worse than recolouring a set nobody has drawn for a while */
            unsigned best = 8;
            for (int q = 14; q >= 1; q--)
                if (pr_key[q] != 0xFF && grp_key[2 * q] == 0xFF
                    && grp_key[2 * q + 1] == 0xFF && pr_age[q] > best) {
                    best = pr_age[q]; p = q;
                }
        }
        if (p < 0)
            continue;                    /* no pair: the old path */
        pr_key[p] = (uint8_t)sc;
        pr_age[p] = 0;
        spr_pair[par][sc] = (uint8_t)p;
        spr_pair[par ^ 1][sc] = (uint8_t)p;
        QCLAIM_N++;
    }
}

/* TILE-ART RECORD EMITTER (2026-09-05, lifted out of the window for the
 * ART TAIL): walks md_dirty from *scan, writes up to bmax records of
 * [slot][16 pixel words] at dst, clears the dirty bits, drains
 * *pending, reports the first slot in *first (0xFFFF if none). ROM:
 * called once or twice per window, <= 40 records. */
__attribute__((noinline))
static int md_emit_art(volatile uint16_t *dst, int bmax, int *scan,
                       uint16_t *pending, uint16_t *first)
{
    int sent = 0, sl = *scan;
    for (int i = 0; i < NSETS * NWAYS && sent < bmax; i++) {
        sl = (sl + 1) & (NSETS * NWAYS - 1);
        if ((sl & 31) == 0 && !md_dirty[sl >> 5]) {   /* word skip */
            sl += 31; i += 31;
            continue;
        }
        if (!(md_dirty[sl >> 5] & (1u << (sl & 31))))
            continue;
        md_dirty[sl >> 5] &= ~(1u << (sl & 31));
        uint32_t mkey = md_tag[sl];
        if (mkey == 0xFFFFFFFFu)
            continue;
        const uint8_t *px = altbeast_tiles + (mkey & 0xFFFFu) * 64;
        const uint8_t *map = mdp_s_map + ((mkey >> 16) & 0x7F) * 8;
        if (*first == 0xFFFF) *first = (uint16_t)sl;
        dst[sent * 17] = (uint16_t)sl;
        volatile uint8_t *o = (volatile uint8_t *)(dst + sent * 17 + 1);
        if (mkey & 0x80000000u) {
            for (int y = 0; y < 8; y++) {
                const uint8_t *r = px + y * 8;
                for (int kk = 0; kk < 4; kk++) {
                    uint8_t a = r[kk * 2], b = r[kk * 2 + 1];
                    *o++ = (uint8_t)(((a ? map[a] : 0) << 4) | (b ? map[b] : 0));
                }
            }
        } else
        for (int y = 0; y < 8; y++) {
            const uint8_t *r = px + y * 8;
            for (int kk = 0; kk < 4; kk++)
                *o++ = (uint8_t)((map[r[kk * 2]] << 4) | map[r[kk * 2 + 1]]);
        }
        if (*pending) (*pending)--;
        sent++;
    }
    *scan = sl;
    return sent;
}

__attribute__((noinline))
static void hs_compute(uint16_t *dst)
{
    const layer_regs *bl = &snap[1];
    for (int pl2 = 1; pl2 >= 0; pl2--) {
        const layer_regs *w2 = pl2 ? &snap[0] : bl;
        for (int r2 = 0; r2 < 28; r2++) {
            int vxr2 = w2->vx0;
            if (w2->any_special) {
                uint16_t rs2 = w2->rs[r2];
                if (rs2 & 0x8000)
                    vxr2 = w2->vx0_a;
                else if (w2->xs_raw & 0x8000)
                    vxr2 = (int)((0xC0 - (rs2 & 0x3FF)) & 0x3FF);
            }
            *dst++ = (uint16_t)((0 - vxr2) & 0x3FF);
        }
    }
    *dst++ = (uint16_t)(bl->vy0 & 0xFF);
    *dst   = (uint16_t)(snap[0].vy0 & 0xFF);
}
/* the two words the VDP uses in full-screen hscroll mode: strip 0 of
 * plane A (sc[3]) and plane B (sc[7]) — the dense compute's words 0 and
 * 28. sc[3] was unused by the receiver; sc[7] was a sequence word only
 * MDVERIFY read. */
__attribute__((noinline))
static void hs_pair(volatile uint16_t *sc)
{
    uint16_t w[58];
    hs_compute(w);
    sc[3] = w[0];
    sc[7] = w[28];
}

#ifdef HS_SHIP
/* HSCROLL SHIPS WITH THE FRAME (2026-09-05, Mike's "grass in two layers").
 * The MD packet is built in the maps drain from the LAUNCHING generation's
 * latch and lands a vint later; the FB of that generation lands when it
 * closes. On a 2-vint generation the MD planes therefore moved a frame
 * before the FB rows did — the priority grass under the player (FB) and
 * the grass beside it (plane A) scrolled on different frames (measured:
 * frames 492->493, wall/ledge/lower grass -1 px, upper grass 0). The
 * scroll words are now computed at launch into a per-parity PENDING
 * slot, promoted to LIVE when that generation SHIPS, and patched into
 * the packet at copy time — so plane scroll and FB flip land together.
 * Cells keep their absolute placement (full 10-bit hscroll), so they
 * are timing-independent. */
#define HS_CLOSED ((uint16_t *)0x0603F900) /* [58]: 56 hs + vyB + vyA, computed at CLOSE from the
                                             * still-latched regs (no launch can precede a close) */
#define HS_DISP   ((uint16_t *)0x0603F980) /* [58]: the frame on screen; advanced at SHIP */
#define HS_OFFS   ((uint16_t *)0x06028DA0) /* [16]: [0..1] dense-block word offset per packet,
                                             * [3] shipped-this-window, [6] copied-this-window,
                                             * [7] offset used, [8..9] FB packet address (u32) */
__attribute__((noinline))
static void hs_patch(uint16_t *pkt, int idx, volatile uint32_t *dst)
{
    uint16_t ho = HS_OFFS[idx];

    if (!ho)
        return;
    /* the packet copied now lands at the next vint. If a closed frame is
     * waiting, this window blits it and it flips at that same vint (the
     * copy precedes the blit in the window: measured, promote-at-close
     * beat promote-at-ship); otherwise the screen keeps showing the
     * displayed frame and the planes must not move. */
    /* nat_ship_now: set at the last-call close check, cleared only after
     * the master's blit half — at copy time it says exactly whether this
     * window ships (nat_gen_ready alone also covers a generation that
     * closed AFTER the ship decision and will not ship until next window:
     * planes a frame early = Mike's two-layer grass once the zombies
     * make generations 2 vints) */
    /* census (HS_CENSUS, 2026-09-05): a packet copied with the closed
     * scroll flips exactly one vint later, ISR or body alike; the skew
     * came from windows where the SHIP followed the copy — the packet
     * left with the displayed scroll and the next window's carried the
     * closed one a frame late. nat_ship_now is still up in that order
     * (cleared only after the master's blit half), so it covers it. */
    const uint16_t *src = (HS_OFFS[3] || nat_ship_now) ? HS_CLOSED : HS_DISP;
#ifdef HS_CENSUS
    if (HS_OFFS[3] || nat_ship_now) {
        unsigned ix = HSC_IDX & 15;
        HSC_RING[ix * 2] = hsc_win; HSC_RING[ix * 2 + 1] = 0xFFFF;
    }
#endif
    HS_OFFS[3] = 0;
    (void)ho;
    pkt[3] = src[0];                      /* plane A hscroll (strip 0) */
    pkt[7] = src[28];                     /* plane B */
    pkt[4] = src[56];
    pkt[6] = src[57];
    HS_OFFS[6] = 1;                       /* copied this window */
    HS_OFFS[7] = 1;
    *(volatile uint32_t *)(HS_OFFS + 8) = (uint32_t)dst;
}

/* SCROLL-ONLY STUB (moved from hs_promote, see there): when a frame
 * shipped this window but NO packet was copied, the planes would flip a
 * frame behind. Post a 0-record tile chunk carrying the closed scroll
 * into a slot the 68K has consumed AND that has no staged packet
 * waiting (a stub in a pending slot deferred the real packet). */
__attribute__((noinline))
static void hs_stub(void)
{
    volatile uint16_t *fp = (volatile uint16_t *)0x2401E800u;   /* B */
    if (fp[0] == 0xB6B6u || k2f_pendB)
        fp = (volatile uint16_t *)0x24011A00u;                  /* A */
    if (fp[0] == 0xB6B6u || ((fp == (volatile uint16_t *)0x24011A00u) && k2f_pendA))
        return;                           /* both busy: the scroll rides the next packet */
    fp[1] = 0; fp[2] = 0; fp[5] = 0;
    fp[3] = HS_CLOSED[0];
    fp[7] = HS_CLOSED[28];
    fp[4] = HS_CLOSED[56];
    fp[6] = HS_CLOSED[57];
    fp[0] = 0xB6B6u;                      /* magic LAST */
    HS_OFFS[3] = 0;
    HS_OFFS[10]++;                        /* diag: stubs posted */
}

__attribute__((noinline))
static void hs_close(void)
{
    hs_compute(HS_CLOSED);                /* the closing generation's regs */
}
static void hs_promote(void)
{
    /* at SHIP: the closed frame becomes the displayed one at the next
     * vint, exactly when the packet copied in this window lands */
    for (int hi = 0; hi < 58; hi++)
        HS_DISP[hi] = HS_CLOSED[hi];
    HS_OFFS[3] = 1;
    if (HS_OFFS[6]) {
        /* the packet already left for the FB this window with the
         * displayed scroll (ship after copy): rewrite it in place — the
         * SH-2 owns the FB inside the window, the 68K reads it next vint */
        volatile uint16_t *fp = (volatile uint16_t *)*(volatile uint32_t *)(HS_OFFS + 8);
        fp[3] = HS_CLOSED[0];
        fp[7] = HS_CLOSED[28];
        fp[4] = HS_CLOSED[56];
        fp[6] = HS_CLOSED[57];
        HS_OFFS[3] = 0;                   /* already applied */
    }
    /* (2026-09-06: the scroll-only stub moved to hs_stub(), called
     * AFTER the publish. hs_promote runs at SHIP, which precedes the
     * publish in the window, so HS_OFFS[6] was ALWAYS clear here and a
     * stub went out on 62% of windows (HS_OFFS[10]=877/1402), into the
     * very slot the real packet then found unconsumed -> deferred:
     * the MD-plane channel ran at half rate on every shipping build.) */
}
#endif

RAMCODE static void slave_cmd(uint16_t cmd)
{
    SYNC[1] = 0;
    SYNC[0] = cmd;
}

RAMCODE static void slave_wait(uint16_t cmd)
{
    /* R60: BOUNDED (the LOOP26 wedge: a stomped SYNC[0] command never
     * echoes and the master held FM forever — vints starved to 14).
     * ~1.5 frames, then force-reset and count: a lost compose band is
     * a stale band, not a dead machine. */
    uint32_t g = 30000;
    while (SYNC[1] != cmd && --g) ;
    if (!g) {
#ifdef PHASE_CENSUS
        PHS[7] = (uint16_t)SYNC[1];      /* what the slave last echoed */
        PHS[6] |= 0x8000;                /* timeout seen (belt win below) */
#endif
        DIAG[27]++;                      /* slave echo timeout — own slot:
                                          * [20] is apply_cram memo-hits,
                                          * [31] is the late-latch counter */
    }
    SYNC[0] = 0;
}

/* NATIVE LAUNCH — one definition, two call sites. LAUNCH_EARLY calls it
 * just before apply_cram instead of after the ack: the launch needs only
 * the harvest, the reg latch, the page copy and the pair claim (all done
 * by then); CRAM paints, skip bars, DREQ re-arm, publish and the ack do
 * not feed it. Measured 2026-09-01: the launch sat at 0.43v into the
 * vint; the compose gets the difference back. Window locals in/out. */
RAMCODE static void nat_window_launch(int par, uint16_t bank1, uint16_t t_vint,
                                      uint16_t win_no, uint16_t *tcp,
                                      uint16_t *pwp)
{
    (void)win_no;
    uint16_t tile_cmd = *tcp, pend_wait = *pwp;
    (void)t_vint;
    if (nat_gen_open) {
        DIAG[30]++;              /* generation overran a vint
                                  * (the old compose-skip
                                  * meaning, frame-sized) */
        if (++nat_skip_run >= 8) {
            /* WEDGE BELT: a chain that never echoes must
             * not freeze the display forever (the LOOP26
             * stomped-SYNC[0] class). Force a fresh
             * launch; slave_wait below bounds the drain. */
            nat_skip_run = 0;
            nat_gen_open = 0;
            nat_mtask = 0;
#ifdef PHASE_CENSUS
            PHS[6] = (uint16_t)(win_no & 0x7FFF); /* belt win */
#endif
            DIAG[27]++;
        }
    } else
        nat_skip_run = 0;
    if (!nat_gen_open && (!nat_gen_ready
#ifdef BLIT_CHASE
                          || nat_ship_now
#endif
                          )
#ifdef PACE30
        && (uint16_t)(win_no - nat_launch_win) >= 2
#endif
        ) {
#ifdef PACE30
        nat_launch_win = win_no;
#endif
        latch_layer_regs();      /* the generation's ONE
                                  * scroll/rowscroll state */
#ifdef HS_SHIP
#endif
#ifndef FB_SPR_READ
        if (nat_spr_ok) {        /* freshest WHOLE landing ->
                                  * SPR_SNAP (torn landings
                                  * keep last generation) */
            unsigned nr8 = (unsigned)nat_nrec * 8u;
#ifdef ROW_GEN
            for (int q = 0; q < 8; q++) rg_spcur[q] = 0;
#endif
            for (unsigned i = 0; i < nr8; i += 8) {
#ifdef ROW_GEN
                {   /* row span of this record, from the word in hand */
                    uint16_t w0 = SPR_LAND[nat_rec0 + i + 0];
                    int top = w0 & 0xFF, bot = w0 >> 8;
                    if (bot > 224) bot = 224;
                    for (int w = top >> 5; top < bot && w <= (bot - 1) >> 5; w++) {
                        int lo = top - w * 32, hi = bot - w * 32;
                        if (lo < 0) lo = 0;
                        if (hi > 32) hi = 32;
                        rg_spcur[w] |= (hi - lo >= 32) ? 0xFFFFFFFFu
                                     : ((1u << (hi - lo)) - 1u) << lo;
                    }
                    SPR_SNAP[i + 0] = w0;
                }
#else
                SPR_SNAP[i + 0] = SPR_LAND[nat_rec0 + i + 0];
#endif
                SPR_SNAP[i + 1] = SPR_LAND[nat_rec0 + i + 1];
#ifdef MD_SPR
                SPR_SNAP[i + 2] = SPR_LAND[nat_rec0 + i + 2]
                                  & (uint16_t)~0x2000;
#else
                SPR_SNAP[i + 2] = SPR_LAND[nat_rec0 + i + 2];
#endif
                SPR_SNAP[i + 3] = SPR_LAND[nat_rec0 + i + 3];
                SPR_SNAP[i + 4] = SPR_LAND[nat_rec0 + i + 4];
                SPR_SNAP[i + 5] = SPR_LAND[nat_rec0 + i + 5];
                SPR_SNAP[i + 6] = SPR_LAND[nat_rec0 + i + 6];
                SPR_SNAP[i + 7] = SPR_LAND[nat_rec0 + i + 7];
            }
            if (nat_nrec < 64)
                SPR_SNAP[nat_nrec * 8 + 2] = 0x8000;
            nat_spr_ok = 0;
        }
#endif
#ifdef MD_SPR
        nat_quick_claim(par);    /* first-appearance pairs (below) */
        mdspr_claim();           /* lockstep law: MD sprites
                                  * and FB content age and
                                  * refresh together */
#endif
        slave_wait(pend_wait);   /* echo already seen (launch
                                  * gate) — this just clears
                                  * SYNC[0] so the same cmd
                                  * value reads as fresh */
        cache_purge();
#ifdef ROW_GEN
        rowgen_build(par);       /* regs latched, snapshot copied, cache
                                  * fresh for the TEXT_C row hashes */
#endif
        nat_genbit ^= 8;         /* see the declaration: two
                                  * consecutive launches must
                                  * never be bit-identical */
        tile_cmd = (uint16_t)(CMD_TILE | 0x0040 | nat_genbit
                              | (par << 8) | bank1);
        slave_cmd(tile_cmd);
        pend_wait = tile_cmd;
        tile_cmd = 0;            /* no post-ack drain under
                                  * NATIVE: the close check
                                  * polls the echo instead */
        SYNC[9] = 7;             /* master tail open: the
                                  * snapshot stays latched
                                  * for the whole generation */
        nat_par = (uint8_t)par;
        nat_bank = (uint8_t)bank1;
        nat_mrg = 0;             /* master compose program:
                                  * band 0, clear phase */
        nat_mphase = 0;
        nat_my = 0;
        nat_t0 = frt();          /* gen-wall census start */
        nat_mtask = NAT_ALL_SLAVE ? 2 : 1;   /* stage 1 only
                                  * when the master owns rows */
#ifdef PHASE_CENSUS
        PHS[4] = 0;              /* echo not yet seen */
        PHS[5] = 0;              /* pickup not yet seen */

        PHL[0] += (uint16_t)(nat_t0 - t_vint); /* launch offset
                                  * into the vint (calibrates
                                  * the bins against W1) */
#endif
        nat_gen_open = 1;
    }
    *tcp = tile_cmd;
    *pwp = pend_wait;
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
#ifdef K2_FREE
/* LOOP 24: per-k arms into per-k buffers. k1 = 596 (prefix + up to 64
 * records); k2 = the non-slim PAL32 family max (216). Arming >= any
 * possible push keeps the LOOP20 rule (an under-armed TCR exhausts
 * mid-stream and the FIFO never re-syncs — the boot hang). */
#ifdef R60
/* REBUILD: ONE packet, ONE arm, ONE buffer — R60_ARM covers the max
 * family length + FIFO residue; landed reads from TCR as ever. */
#define DREQ_LEN(k)  ((unsigned)R60_ARM)
#elif defined(SPR_FULL)
/* MAME pixel-gate arm: EXACT lengths so TE sets and MAME reads the
 * landing (it cannot read partials). Wedge-safe because SPR_FULL
 * keeps the per-word FIFO belt on the MD side. */
#define DREQ_LEN(k)  ((k) == 2 ? (unsigned)K2F_K2_LEN(K2F_PAL_KMAX) : 596u)
#else
/* +8 words over the max push: an aborted push leaves up to a full
 * FIFO (8 words) of residue, and the next push must NOT overshoot
 * the arm — an exhausted TCR with a full FIFO wedges MAME's 68K
 * (defer_access, forever) and drops words on ares. Oversized arms
 * are the safe direction (LOOP20); landed reads from TCR the same,
 * and a residue-displaced landing (604/144) fails the whitelist and
 * heals next vint. */
#define DREQ_LEN(k)  ((k) == 2 ? (unsigned)K2F_K2_LEN(K2F_PAL_KMAX) + 8u : 604u)
#endif
#elif defined(WIN_TWO)
/* v8 rebalance: the sprite packet (after k1) is standard 596; the
 * k2-tail packet carries pal (optional) + BOTH text chunks: 852 with
 * pal, 596 without — k1 keeps the fat blit, k2 keeps the fat push.
 * Text stays 2 chunks/cycle (the 1/3-rate regression fence). */
/* LOOP 20: the arm stays at the FULL lengths even under the FBTEXT
 * harvest (which shrank the pushes to 92 sprite / 340-84 text). Arming
 * the exact maxima was tried and HANGS AT BOOT: a push longer than the
 * armed TCR exhausts it mid-stream, the FIFO fills, the MD's bounded
 * spin aborts with the FIFO still full, and the stream never
 * re-synchronises (ent froze at 3 windows). Arming big is safe — TE
 * stays clear and `landed` reads the true partial length on ares.
 * THE COST IS TOOLING, NOT CORRECTNESS: MAME cannot read partial
 * landings, so on a harvest build every packet reads landed=0 there and
 * the palette pairs never apply — harvest builds are MAME-COLOUR-BLIND
 * by construction. Gate pixels on N_fbtext (pre-harvest, full packets,
 * MAME-clean at 62.37) and gate the harvest on ares, exactly the
 * SPRTRUNC precedent. */
#define DREQ_LEN(k)  ((k) == 2 ? 852u : 596u)
#else
#define DREQ_LEN(k)  (596u)
#endif

#ifdef FB_XPORT
static uint8_t  fbx_seq_seen;            /* last publish sequence consumed */
static unsigned fbx_landed;              /* words lifted this window */
/* Lift the published packet out of the framebuffer into SPR_LAND.
 * FBXLATE=1 calls this AFTER the flip instead of before it, to measure
 * whether the pre-flip position is actually required. */
RAMCODE static void fbx_lift(void)
{
#ifdef FLIP_CENSUS
    CEN[2]++;                            /* lift block entered */
#endif
    volatile uint16_t *pub = (volatile uint16_t *)FBX_PUB_SH;
    uint16_t pw = pub[0];
    fbx_landed = 0;
#ifdef FLIP_CENSUS
    CEN[5] = pw;
    if ((pw & 0xFF00u) == FBX_MAGIC) {
        CEN[3]++;
        if ((uint8_t)pw == fbx_seq_seen) CEN[4]++;
    }
#endif
    if ((pw & 0xFF00u) == FBX_MAGIC && (uint8_t)pw != fbx_seq_seen) {
        unsigned n = pub[1];
        if (n >= 26 && n <= 924) {
            const volatile uint32_t *sp = (const volatile uint32_t *)FBX_PKT_SH;
            uint32_t *dp = (uint32_t *)SPR_LAND;
            unsigned nl = (n + 1u) >> 1;
            for (unsigned i = 0; i < nl; i++) dp[i] = sp[i];
            fbx_landed = n;
            fbx_seq_seen = (uint8_t)pw;
#ifdef FLIP_CENSUS
            CEN[0]++;
#endif
        }
#ifdef FLIP_CENSUS
        else CEN[1]++;
#endif
    }
}
#endif
RAMCODE static void dreq_rearm(int k)
{
#ifdef FB_XPORT
    /* the FB route never enables DREQ, and an armed DMA pointed at
     * SPR_LAND is a loaded gun aimed at the packet we just copied there */
    (void)k;
    return;
#else
    SH2_DMA_CHCR0 = 0x44E0;
    (void)SH2_DMA_CHCR0;
    SH2_DMA_SAR0 = 0x20004012;          /* DREQ FIFO */
#if defined(R60)
    SH2_DMA_DAR0 = 0x26039000u;         /* SPR_LAND, always (R60_ARM
                                         * = 936w = 1872B, ends 0x39750
                                         * under md_dbg at 0x39800) */
#elif defined(K2_FREE)
    SH2_DMA_DAR0 = (k == 2) ? 0x260394C0u   /* SPR_LAND_K2 */
                            : 0x26039000u;  /* SPR_LAND */
#else
    SH2_DMA_DAR0 = 0x26039000;          /* SPR_LAND (uncached) */
#endif
    SH2_DMA_TCR0 = DREQ_LEN(k);
    SH2_DMA_DRCR0 = 0;
    SH2_DMA_DMAOR = 1;
    SH2_DMA_CHCR0 = 0x44E1;
#endif
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

#ifdef FLICK_FUSE
/* Runs at k1, master, immediately after the FB_SPR -> SPR_SNAP fill and
 * before any band is queued, so compose on either CPU only ever sees a
 * fully-updated flick_lvl — the same ordering contract as the snapshot
 * itself. Tracks ZOOMED records only (the apparition class; shadow
 * colour 0x3F excluded): a tracker matches by colourset + position
 * proximity, keeps an 8-window presence history, and while that history
 * is still TOGGLING the record is stippled at its on-ratio — including
 * on OFF windows, where the held copy is injected back into the list so
 * the dither, not the game's duty cycle, does the fading. A steady
 * record (8/8 present) draws normally; 8/8 absent frees the tracker,
 * which also plays the arcade's real 15-frame lightning blackouts as
 * a ~2-window dither-down instead of a hard cut. */
static uint8_t flick_dirty;              /* nonzero flick_lvl entries exist */
RAMCODE static void flick_update(unsigned zseen)
{
    uint8_t owned[64];
    int nrec = 0;
    int wrote = 0;
    /* the common case — no zoomed record on screen, no tracker alive,
     * no stale levels to clear — must cost the 68K's FM wait NOTHING:
     * this runs in-window and the ares state read window/ack as the
     * handler's bigger half */
    {
        int any = zseen != 0 || flick_dirty;
        for (int t = 0; t < 4 && !any; t++)
            any = flick_trk_tab[t].live;
        if (!any)
            return;
    }
    for (; nrec < 64; nrec++) {
        flick_lvl[nrec] = 0;
        if (SPR_SNAP[nrec * 8 + 2] & 0x8000)
            break;
        owned[nrec] = 0;
    }
    for (int t = 0; t < 4; t++) {
        struct flick_trk *k = &flick_trk_tab[t];
        if (!k->live)
            continue;
        int hit = -1;
        /* two-tier match: position within tolerance first, then same
         * colourset ANYWHERE. A scale step past the tolerance must MOVE
         * the tracker, not orphan it — an orphan kept injecting its
         * held copy while a second tracker took the real record: the
         * giant stale ghost of frame_000800 (ares corpus). */
        for (int pass = 0; pass < 2 && hit < 0; pass++)
            for (int i = 0; i < nrec; i++) {
                volatile uint16_t *e = SPR_SNAP + i * 8;
                uint16_t d0 = e[0];
                int dy, dx;
                if (owned[i] || (e[2] & 0x4000) || (d0 & 0xFF) >= (d0 >> 8))
                    continue;
                if (!(e[5] & 0x3FF))
                    continue;
                if ((e[4] & 0x3F) != (k->rec[4] & 0x3F))
                    continue;
                if (pass == 0) {
                    dy = (int)(d0 & 0xFF) - (int)(k->rec[0] & 0xFF);
                    dx = (int)(e[1] & 0x1FF) - (int)(k->rec[1] & 0x1FF);
                    if (dy < -24 || dy > 24 || dx < -32 || dx > 32)
                        continue;
                }
                hit = i;
                break;
            }
        k->hist = (uint8_t)(k->hist << 1);
        if (hit >= 0) {
            volatile uint16_t *e = SPR_SNAP + hit * 8;
            owned[hit] = 1;
            k->hist |= 1;
            k->age = 0;
            for (int w = 0; w < 6; w++)
                k->rec[w] = e[w];
        } else if (++k->age >= 16) {     /* > the ~8-window blackouts */
            k->live = 0;
            continue;
        }
        {
            unsigned h = k->hist, tg = (h ^ (h >> 1)) & 0x7F;
            unsigned r = 0, ntg = 0;
            for (int b = 0; b < 8; b++) {
                r += (h >> b) & 1;
                ntg += (tg >> b) & 1;
            }
            if (ntg >= 3)
                k->stick = 1;
            else if (r == 8)
                k->stick = 0;            /* settled steady: draw normally */
            if ((ntg >= 3 || k->stick) && r > 0 && r < 8) {
                /* slew the drawn coverage 1 step/window: raw r jumps
                 * 2-3 between windows and reads as brightness stutter,
                 * and ramping from 1 gives the fade-in its ramp even
                 * where 30Hz sampling can't see the true duty */
                if (k->lvl == 0)
                    k->lvl = 1;
                else if (k->lvl < r)
                    k->lvl++;
                else if (k->lvl > r)
                    k->lvl--;
                if (hit >= 0) {
                    flick_lvl[hit] = k->lvl;
                    wrote = 1;
                } else if (k->age <= 4 && nrec < 63) {
                    /* OFF phase: carry the held copy — but only ~8
                     * frames' worth. Longer absences are content (the
                     * lightning blackouts, a real despawn) and a stale
                     * injection past that is a frozen ghost. */
                    volatile uint16_t *e = SPR_SNAP + nrec * 8;
                    SPR_SNAP[(nrec + 1) * 8 + 2] = 0x8000;
                    for (int w = 0; w < 6; w++)
                        e[w] = k->rec[w];
                    flick_lvl[nrec] = k->lvl;
                    owned[nrec] = 1;
                    wrote = 1;
                    nrec++;
                }
            } else
                k->lvl = 0;
        }
    }
    for (int i = 0; i < nrec; i++) {
        volatile uint16_t *e = SPR_SNAP + i * 8;
        uint16_t d0 = e[0];
        if (owned[i] || (e[2] & 0x4000) || (d0 & 0xFF) >= (d0 >> 8))
            continue;
        if (!(e[5] & 0x3FF) || (e[4] & 0x3F) == 0x3F)
            continue;
        for (int t = 0; t < 4; t++) {
            struct flick_trk *k = &flick_trk_tab[t];
            if (k->live)
                continue;
            k->live = 1;
            k->hist = 1;
            k->stick = 0;
            k->age = 0;
            k->lvl = 0;
            for (int w = 0; w < 6; w++)
                k->rec[w] = e[w];
            break;
        }
    }
    flick_dirty = (uint8_t)wrote;
}
#endif

#ifdef FB_BENCH
/* DIRECT-DRAW ECONOMICS SPIKE (2026-08-26): sbuf-vs-FB write costs,
 * FRT-timed at boot. Results 0x3A790[0..4]. Cart .text on purpose. */
__attribute__((noinline)) static void fb_bench(void)
{
        volatile uint32_t *bm = (volatile uint32_t *)0x2603A790;
        uint16_t t0;
        t0 = frt();
        for (int r = 0; r < 100; r++) {
            uint32_t *d = (uint32_t *)&sbuf[(8 + (r % 200)) * SBUF_W + 8];
            for (int x = 0; x < 80; x++) d[x] = 0x11111111u;
        }
        bm[0] = (uint16_t)(frt() - t0);
        t0 = frt();
        for (int r = 0; r < 100; r++) {
            uint32_t *d = (uint32_t *)(0x04000200u + (unsigned)(r % 200) * 320);
            for (int x = 0; x < 80; x++) d[x] = 0x11111111u;
        }
        bm[1] = (uint16_t)(frt() - t0);
        t0 = frt();
        for (int it = 0; it < 100; it++) {
            const uint16_t *sd = altbeast_sprites + ((it * 37) & 0xFFF);
            uint8_t *d = &sbuf[(8 + (it % 180)) * SBUF_W + 8 + (it & 63)];
            for (int y = 0; y < 32; y++) {
                for (int x = 0; x < 8; x++) {
                    uint16_t w = sd[y * 8 + x];
                    uint8_t p0 = (uint8_t)(w >> 12), p1 = (uint8_t)((w >> 8) & 15);
                    uint8_t p2 = (uint8_t)((w >> 4) & 15), p3 = (uint8_t)(w & 15);
                    uint8_t *dd = d + y * SBUF_W + x * 4;
                    if (p0) dd[0] = p0;
                    if (p1) dd[1] = p1;
                    if (p2) dd[2] = p2;
                    if (p3) dd[3] = p3;
                }
            }
        }
        bm[2] = (uint16_t)(frt() - t0);
        t0 = frt();
        for (int it = 0; it < 100; it++) {
            const uint16_t *sd = altbeast_sprites + ((it * 37) & 0xFFF);
            uint8_t *d = (uint8_t *)(0x04000200u
                                     + (unsigned)(it % 180) * 320 + (it & 63));
            for (int y = 0; y < 32; y++) {
                for (int x = 0; x < 8; x++) {
                    uint16_t w = sd[y * 8 + x];
                    uint8_t p0 = (uint8_t)(w >> 12), p1 = (uint8_t)((w >> 8) & 15);
                    uint8_t p2 = (uint8_t)((w >> 4) & 15), p3 = (uint8_t)(w & 15);
                    uint8_t *dd = d + y * 320 + x * 4;
                    if (p0) dd[0] = p0;
                    if (p1) dd[1] = p1;
                    if (p2) dd[2] = p2;
                    if (p3) dd[3] = p3;
                }
            }
        }
        bm[3] = (uint16_t)(frt() - t0);
        t0 = frt();
        for (int i2 = 0; i2 < 10000; i2++)
            *(volatile uint8_t *)(0x04000200u + ((i2 * 149) & 0xFFFF)) = 1;
        bm[4] = (uint16_t)(frt() - t0);
    }
#endif
/* ONE-SHOT BOOT INIT, extracted from m_main (2026-08-29) to CART
 * .text: it ran exactly once yet lived in RAMCODE inside m_main,
 * spending ~1KB of the 0x19000 region guard on code that never runs
 * again. Cart fetch at boot is free (the SH-2s boot from cart; the
 * game is not running). Extraction paid for TEXT_CLASS with room
 * left. Order contract: ends with the COMM14=0x600D SDRAM-resident
 * beacon, exactly as before.  */
#ifdef PHASE_CENSUS
/* ROM-resident (no RAMCODE): called once per generation event */
static inline void ph_acc(unsigned i, uint16_t v)
{
    PH[i] += v;
    if (v > PH[5 + i]) PH[5 + i] = v;
}
static inline void ph_bin(unsigned base, uint16_t v)
{
    int b = ((int)v - 6026) / 1506;      /* 0.5v origin, 0.125v bins */
    if (b < 0) b = 0;
    if (b > 7) b = 7;
    PHH[base + (unsigned)b]++;
}
__attribute__((noinline)) static void st_m(unsigned ev)
{
    unsigned n = STR[23];
    if (n && n < 19) {
        STR[n] = ((uint32_t)ev << 16) | frt();
        STR[23] = n + 1;
    }
}
__attribute__((noinline)) static int nat_ph_check(uint16_t pw)
{
    if (!PHS[5] && SYNC[13])
        PHS[5] = 1;                      /* (PHP pickup sum retired: it
                                          * measured the master's own poll
                                          * lag, not the slave; PHS[6..7]
                                          * now carry the belt/timeout
                                          * stamps) */
    if (SYNC[1] != pw)
        return 0;
    if (!PHS[4]) {
        PHS[4] = 1;
        PHS[0] = frt();
        st_m(0x85);
    }
    return 1;
}
__attribute__((noinline)) static void nat_ph_close(void)
{
    uint16_t now = frt();
    uint16_t wl = (uint16_t)(now - nat_t0);
    NAT_WALL[0] += wl; NAT_WALL[1]++;
    if (wl > NAT_WALL[2]) NAT_WALL[2] = wl;
    uint16_t te = (uint16_t)(PHS[0] - nat_t0);
    uint16_t tm = (uint16_t)(PHS[1] - nat_t0);
    uint16_t last = te > tm ? te : tm;
    ph_acc(0, te);
    ph_acc(1, tm);
    ph_acc(2, (uint16_t)(wl - last));
    ph_bin(0, te);
    ph_bin(8, tm);
    PHS[2] = now;
    st_m(0x86);
}
#define PHPER ((volatile uint16_t *)0x26039920)   /* ship period bins
                                                    * 1,2,3,4+ vints */
__attribute__((noinline)) static void nat_ph_ship(uint16_t tv)
{
    uint16_t now = frt();
    ph_acc(3, (uint16_t)(now - PHS[2]));
    PH[11] += (uint16_t)(now - tv);
    {
        unsigned per = ((uint16_t)(now - PHS[3]) + 6026u) / 12052u;
        if (per < 1) per = 1;
        if (per > 4) per = 4;
        PHPER[per - 1]++;
    }
    PHS[3] = now;
    st_m(0x87);
}
__attribute__((noinline)) static void nat_ph_flip(void)
{
    ph_acc(4, (uint16_t)(frt() - PHS[3]));
    PH[10]++;
    st_m(0x88);
}
#endif
#ifdef BOOT_WSTAGE
#define WSTAGE(col) do { ((volatile uint16_t *)0x20004200)[0] = (col); } while (0)
#else
#define WSTAGE(col) do { } while (0)
#endif
#ifdef BOOT_SHSTAGE
/* HARDWARE BOOT PROBE (2026-09-07, MiSTer): stage word on COMM12 (the
 * 68K's 0x600D wait paints the MD backdrop from it while the 32X display
 * is still off) and 32X CRAM entry 0 (visible once the display is on). */
#define SHSTAGE(n, col) do { *(volatile uint16_t *)0x2000402C = (n); \
                             ((volatile uint16_t *)0x20004200)[0] = (col); } while (0)
#else
#define SHSTAGE(n, col) do { } while (0)
#endif
__attribute__((noinline)) static void m_boot_init(void)
{
    SHSTAGE(6, 0x7C00);                  /* BLUE: boot init entered */
    /* COLD-BOOT ZERO (2026-09-05, Mike's cold-start "blue and white
     * gravestones"). These fixed-address blocks are outside .bss, so a
     * power-on start leaves them as random SDRAM while a reset inherits
     * the previous run's sane values — the symptom was cold-only. The
     * path to the screen: the boot palette storm ships all 64 blocks
     * raw; a push lost with landed==0 left PAL_SH's RANDOM words
     * standing under a shadow that said "shipped" (the lost-push class,
     * now re-marked by the belt). Zero makes any residue black, never
     * a random ramp; the scene probes have no zero-valued entries, so a
     * zeroed PAL_SH cannot false-detect a scene. */
    for (unsigned i = 0; i < 2048; i++) {
        PAL_SH[i] = 0;
        TEXT_U[i] = 0;
    }
    for (unsigned i = 0; i < 512; i++)
        SPR_SNAP[i] = 0;                 /* top>=bot on every record: empty */
#ifdef MDP_LAST_GET
    for (unsigned i = 0; i < 32; i++)
        mdp_s_last[i] = 0;
#endif
#ifndef PHASE_CENSUS
    for (unsigned i = 0; i < 96; i++)
        sused_prev[i] = 0;
#endif
#ifdef HS_CENSUS
    for (unsigned i = 0; i < 32; i++)
        HSC_RING[i] = 0;
#endif

#ifdef R60
    /* STACK WATERMARK SENTINEL. The R60 master call graph measured
     * 2176B deep (LONG-1 stack slots written AT md_pkt[0] 0x3E780
     * under the old 0x3F000 top — the whole B-channel outage). Top is
     * 0x3F800 now; paint md_pkt's end up to just under the live SP so
     * an ares dump shows the true low watermark. md_pkt itself is
     * rebuilt every window, so painting it is safe at boot. */
    {
        uint32_t sp;
        __asm__ __volatile__("mov r15,%0" : "=r"(sp));
        sp -= 64;
        for (uint32_t a = 0x0603ED80u; a < sp; a += 4)
            *(volatile uint32_t *)a = 0xA5A5A5A5u;
    }
#endif

#ifdef SBUF_CANARY
    /* REGION-GUARD PURCHASE (LOOP 18 step 1). sbuf is 336x240 = 80,640
     * bytes and IS the 0x19000 region — 21.6KB of RAMCODE sits below it
     * and the guard is 102.4KB, which is why 88 bytes is all the room
     * job 2 has. 320x224 of that is the screen; the rest is margin for
     * fine scroll and off-edge sprite draw, and NOBODY KNOWS how much of
     * the margin is live.
     * So do not guess: paint every margin byte with a per-row signature
     * and let a play pass say which rows and columns are ever touched.
     * Rows 0..7 and 232..239, and columns 0..7 and 328..335 of every
     * row. tools/sbuf_canary.lua reads it back.
     * Signature is row-dependent so a legitimate write of the same value
     * cannot hide (a flat 0xA5 would be invisible against any code that
     * happens to store 0xA5). */
    for (int r = 0; r < SBUF_H; r++) {
        uint8_t sig = (uint8_t)(0xA5 ^ (r * 7));
        uint8_t *row = sbuf + r * SBUF_W;
        if (r < 8 || r >= 232) {
            for (int c = 0; c < SBUF_W; c++) row[c] = sig;
        } else {
            for (int c = 0; c < 8; c++)   row[c] = sig;
            for (int c = 328; c < 336; c++) row[c] = sig;
        }
    }
#endif
    for (int i = 0; i < 13 * 0x800; i++)
        TILEMAP_U[i] = 0;
    SHSTAGE(7, 0x03FF);                  /* YELLOW: sbuf purchase done */
    for (int i = 0; i < 2048; i++)
        TEXT_U[i] = 0;
#ifdef TILE_CLASS
    for (int k2i = 0; k2i < TILE_NCLASS; k2i++) {
        grp_key[1 + k2i] = tile_class_rep[k2i];  /* classes own 1..NCLASS
                                                  * from boot; repointed
                                                  * to a live member per
                                                  * build_maps cycle */
        grp_kind[1 + k2i] = 0;
        grp_age[1 + k2i] = 0;
    }
#endif
#ifdef TEXT_CLASS
    /* STATIC TEXT CLASS (2026-08-29, the green-HUD-digits fix): text
     * set 0 — the always-on HUD text, ONE palette state across the
     * whole arcade census (516/540 samples; tools/text_census.lua) —
     * pins to GROUP 0, the 32nd group apply_cram reserved for the
     * through bit and never armed. Entries 1-7 are seven virgin CRAM
     * slots (verified zero after 1500 live frames); entry 0 stays the
     * through bit (apply_cram paints group 0 pens 1-7 only). The HUD
     * digits can no longer lose their group to the 2-deep dynamic
     * zone and fall to shared_tile — the green-digit mechanism. */
    grp_key[0] = 0;
    grp_kind[0] = 1;
    grp_age[0] = 0;
#endif
    for (int i = 0; i < CSETS * NWAYS; i++)
        cache_tag[i] = 0xFFFF;
#ifdef ROW_GEN
    for (int i = 0; i < 8 + 32; i++)
        RGU[i] = 0;
    for (int i = 0; i < 8; i++) {        /* both banks unknown: ship all */
        RG_ACC(0)[i] = RG_ACC(1)[i] = 0xFFFFFFFFu;
        RG_SHIPM[i] = 0xFFFFFFFFu;
    }
#endif
#ifdef MD_BG
    for (int i = 0; i < NSETS * NWAYS; i++) {
        md_tag[i] = 0xFFFFFFFFu;             /* fixed block: not .bss-zeroed */
        md_ref[i] = 0;
    }
#endif
#ifdef MD_BG
    for (int i = 0; i < NSETS * NWAYS / 32; i++)
        md_dirty[i] = 0;
#ifdef CUT_BLANK
    /* fixed-scrap counters (0x28FA0 free block): not .bss, so boot
     * must zero them explicitly (emulators zero SDRAM; silicon won't).
     * [0] blanked cells, [1] cut arms. */
    ((volatile uint32_t *)0x26028FA0)[0] = 0;
    ((volatile uint32_t *)0x26028FA0)[1] = 0;
#endif
    *(volatile uint32_t *)0x26028FA8 = 0;    /* slave idle meter (s_main) */
#ifdef BLIT_SKIP
#ifdef BLIT_SKIP_COUNT
    for (int i = 0; i < 6; i++)
        BSCNT[i] = 0;
#endif
#ifdef FBSPR_PROBE
    for (int i = 0; i < 4; i++)
        FBP[i] = 0;
#endif
#ifdef SPR_LATE
#ifdef SPR_LATE_DIAG
    for (int i = 0; i < 16; i++)
#else
    for (int i = 0; i < 10; i++)             /* stop at cache_tag 0x3A800 */
#endif
        SPRLATE[i] = 0;
#ifdef SPR_LATE_DIAG
    for (int i = 0; i < 64; i++)
        SPRPEN[i] = 0;
#endif
#endif
    for (int i = 0; i < 256; i++)
        shadow_lut[i] = (uint8_t)i;          /* COLD-BOOT IDENTITY (Mike,
                                              * 2026-08-27 cold-boot note):
                                              * the fixed block powers up
                                              * ZERO on hardware and ares
                                              * cold boot, so every shadow
                                              * pixel darkened to CRAM[0]
                                              * = black silhouettes until
                                              * the first 64-chunk rebuild
                                              * (~1-2s). Identity = shadow
                                              * briefly ABSENT instead —
                                              * invisible next to black
                                              * blobs. */
#ifdef DIRTY_ROW
    for (int i = 0; i < 232; i++)
        ROWLIVE[i] = 1;                      /* fixed block: boot to LIVE.
                                              * 1 = blit it. The safe
                                              * direction: a lost mark
                                              * costs a blit, a lost
                                              * clear-mark costs a layer. */
#ifdef DIRTY_ROW_VERIFY
    DRVC[0] = DRVC[1] = DRVC[2] = 0;
#endif
#endif
    for (int i = 0; i < 2 * 224; i++)
        FBCLEAR[i] = 0;                      /* fixed block: 0 = unknown =
                                              * write it. Never leave this
                                              * to SDRAM garbage: a stray 1
                                              * bit is a group that never
                                              * gets drawn. */
#endif
#ifdef MD_SPR
    /* per-scene state bytes live in SDRAM scratch (0x28E28+): garbage
     * here is behavior, not diagnostics — zero at boot */
    mdspr_scene = mdspr_sus = mdspr_post = 0;
    mdspr_danchor = mdspr_flip_run = 0;
    MDSPR_CNT[0] = MDSPR_CNT[1] = 0;
#endif
    /* slave busy census (s_main dispatch writes it) */
    ((volatile uint32_t *)0x26028C80)[0] = 0;
#ifdef CAT1_MD
    for (int i9 = 0; i9 < 28; i9++)
        CAT1_PEND[i9] = 0;
#endif
#ifdef PHASE_CENSUS
    for (int i9 = 0; i9 < 16; i9++) {
        PH[i9] = 0;
        PHH[i9] = 0;
    }
    PHL[0] = PHL[1] = 0;
    PHPER[0] = PHPER[1] = PHPER[2] = PHPER[3] = 0;
    for (int i9 = 0; i9 < 24; i9++)
        STR[i9] = 0;
#endif
    ((volatile uint32_t *)0x26028CA4)[0] = 0;
    ((volatile uint32_t *)0x26028CA4)[1] = 0;
    ((volatile uint32_t *)0x26028CA4)[2] = 0;
    *(volatile uint16_t *)0x26028CB0 = 0;
    miss_n[0] = miss_n[1] = 0;
    for (int i9 = 0; i9 < 16; i9++)
        ((volatile uint8_t *)0x26028C90)[i9] = 0;   /* text_grp */
    shadow_cur = 0;
#ifdef GLOW_ANIM
    glow_on = glow_pause = glow_streak = 0;
    glow_rp = glow_ws = glow_dw = 0;
    glow_seen_rp = glow_seen_ws = 0;
#endif
#ifdef NT_WRAP
    for (int i = 0; i < 56; i++) {
        md_dbg_base[i] = 0xFFFF;             /* forces first-visit full rows */
        md_dbg_hs[i] = 0xFFFF;               /* forces first hs ship (hs<=0x3FF) */
    }
#endif
#ifdef WIN_TWO
    {
        volatile uint8_t *sp2 = (volatile uint8_t *)snap;
        for (unsigned i = 0; i < 2 * sizeof(layer_regs); i++)
            sp2[i] = 0;
    }
#endif
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
    for (int i = 0; i < CSETS; i++)
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
        for (int i = 0; i < 256; i++)
            pri_lut[k][i] = 0;      /* fixed block: not .bss-zeroed */
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
#ifdef MD_SPR
    SHSTAGE(7, 0x7FE0);                  /* CYAN: P3 scratch */
    /* P3: SAT/palette scratch starts clean — an all-zero entry 0
     * (link 0) ends the MD sprite scan immediately, so pre-claim
     * publishes ship an EMPTY table, never boot garbage. */
    for (int i = 0; i < 148; i++)
        MDSPR_SAT[i] = 0;                    /* 128 SAT + 16 pal + 2
                                              * counter longs */
#endif
    SYNC[4] = SYNC[5] = 0;              /* (iter4) preempt-blit mailbox */
    SYNC[8] = SYNC[9] = 0;
    SYNC[14] = 224;                     /* blit fence: nothing pending */
    SYNC[10] = 0;                       /* cat1-owed mask (attempt 4) */
    SYNC[12] = 0;                       /* slave-park broadcast clear */
    SYNC[13] = 0;                       /* snap latch clear */              /* ROW-DEFER compose-open masks
                                         * (slave not yet running: the
                                         * one legal cross-writer) */

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
    DIAG[27] = 0;                       /* slave echo timeouts (R60) */
    DIAG[24] = DIAG[25] = 0;           /* FM_TEST read-half probe */
#ifdef WIN_SPLIT_PROBE
    DIAG[23] = DIAG[24] = DIAG[25] = 0; /* LOOP 9 blit / wait / rows split */
#endif
#ifdef FM_TEST
    FMT[0] = FMT[1] = FMT[2] = FMT[3] = 0;
    DRQR[0] = DRQR[1] = DRQR[2] = DRQR[3] = DRQR[4] = 0;
    DRQR[5] = DRQR[6] = 0;      /* drift split: small / catastrophic */
    DRQR[7] = 0;                /* misaligned (FIFO word-loss) packets */
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
#ifdef BLIT_SKIP
    DIAG[50] = DIAG[51] = 0;            /* LOOP 18 per-bank dirty-row sim */
    for (int i = 0; i < 2 * 48; i++)
        ((uint32_t *)0x0603A680)[i] = 0xFFFFFFFFu;
#endif
    for (int i = 0; i < 112; i++)
        ROWHASH[i] = 0xFFFFFFFFu;       /* 0 is a plausible row hash */
#endif
    for (int i = 0; i < 192; i++)
        PAL_SETGEN[i] = 0;              /* before the slave is released */
    for (int i = 0; i < 32; i++) {
        cram_key[i] = 0xFFFF;           /* .bss zeros would alias tile set 0 */
        cram_keygen[i] = 0;             /* fixed SDRAM: no .bss zeroing */
    }

    /* Master is SDRAM-resident from here on: the MD may set RV=1 now. */
#ifdef BOOT_FLIPTEST
    /* HARDWARE PROBE: the display path alone. Bank A rows 0-7 = index 1
     * (magenta), flip; bank B rows 8-15 = index 2 (green), flip; halt.
     * Hardware semantics: the screen shows bank A = one magenta bar at
     * the top. Green bar = the displayed bank is the one being drawn.
     * Both = single buffer. Neither = SH-2 FB writes are not displayed. */
    {
        volatile uint32_t *px = (volatile uint32_t *)(0x24000000u + 0x200u);
        ((volatile uint16_t *)0x20004200)[0] = 0x0000;
        ((volatile uint16_t *)0x20004200)[1] = 0x7C1F;
        ((volatile uint16_t *)0x20004200)[2] = 0x03E0;
        for (int i = 0; i < 8 * 320 / 4; i++) px[i] = 0x01010101u;
        {
            uint16_t fs = MARS_VDP_FBCTL & 1;
            MARS_VDP_FBCTL = fs ^ 1;
            while ((MARS_VDP_FBCTL & 1) == fs) ;
        }
        px = (volatile uint32_t *)(0x24000000u + 0x200u + 8u * 320u);
        for (int i = 0; i < 8 * 320 / 4; i++) px[i] = 0x02020202u;
        {
            uint16_t fs = MARS_VDP_FBCTL & 1;
            MARS_VDP_FBCTL = fs ^ 1;
            while ((MARS_VDP_FBCTL & 1) == fs) ;
        }
        for (;;) ;
    }
#endif
    SHSTAGE(7, 0x7FFF);                  /* WHITE: about to post 0x600D */
    MARS_SYS_COMM14 = 0x600D;

}

#ifdef FBDMA_PROBE
/* FB WRITE PATH PROBE (2026-09-06, ROM-resident: the SDRAM region guard).
 * The blit ships 100-160 rows/frame of 320B SDRAM->FB; if the SH-2's DMAC
 * beats the CPU store loop, the master's 40-110 lines of FB work per vint
 * shrink and the whole frame budget changes. Same source, same dest, same
 * 100 rows, back to back. Counters at 0x396B0 (free: SPR_LAND ends
 * 0x396A8, DIRTYROWVERIFY starts 0x39750).
 *   [0] rows shipped by the real blit   [1] CPU ticks   [2] DMA ticks
 *   [3] 0xD0A1<<16 | DMA timeouts */
__attribute__((noinline)) static void fb_probe(void)
{
    volatile uint32_t *st = fbp_st;
    const uint32_t *src = (const uint32_t *)0x06039A00;   /* md_pktA, idle at init */
    volatile uint32_t *fb = (volatile uint32_t *)0x24000200;
    uint16_t t0 = frt();
    for (int r = 0; r < 100; r++)
        for (int i = 0; i < 80; i++) fb[r * 80 + i] = src[i];
    st[1] = (uint16_t)(frt() - t0);
    SH2_DMA_DMAOR = 1;
    t0 = frt();
    uint32_t te = 0;
    for (int r = 0; r < 100; r++) {
        SH2_DMA_CHCR1 = 0;
        SH2_DMA_SAR1 = (uint32_t)src;
        SH2_DMA_DAR1 = 0x24000200u + (uint32_t)r * 320u;
        SH2_DMA_TCR1 = 80;
        SH2_DMA_CHCR1 = 0x5201;      /* DM=inc SM=inc TS=long AR DE */
        uint32_t g = 200000;
        while (!(SH2_DMA_CHCR1 & 2) && --g) ;
        if (!g) te++;
    }
    st[2] = (uint16_t)(frt() - t0);
    st[3] = 0xD0A10000u | te;
    SH2_DMA_CHCR1 = 0;
}
#endif
RAMCODE void m_main(void)
{
#ifdef FLIP_CENSUS
    /* CALIBRATE THE INSTRUMENT BEFORE TRUSTING IT. DIAG[84] read 0 at
     * this site while the program was plainly running, so writes past
     * some point in the DIAG block do not survive. Bump six candidate
     * addresses here, at a site that MUST execute, and keep whichever
     * ones come back with a large count. */
    CEN[10]++;                           /* m_main entered: must read 1 */
#ifdef CEN_CAL
    /* CALIBRATE EVERY SLOT, not one of them. CEN[0] was verified and the
     * rest assumed, and CEN[12] then reported 470 hits inside a block
     * compiled to `if (0)`. m_main runs exactly once, so after this every
     * slot 0..23 must read exactly 1; any other value means that slot is
     * aliased by something else and must not be used. */
    for (int q = 0; q < 24; q++) if (q != 10) CEN[q]++;
#endif
#endif
    /* Release the secondary SH-2 from its S_OK wait. */
    MARS_SYS_COMM4 = 0;
    SHSTAGE(1, 0x001F);                  /* m_main entered (SDRAM code runs) */

    Hw32xInit(MARS_VDP_MODE_256, 0);
    SHSTAGE(4, 0x001F);                  /* RED: Hw32xInit done */
#ifdef FBDMA_PROBE
    fb_probe();
#endif
    MARS_VDP_DISPMODE = MARS_NTSC_FORMAT | MARS_224_LINES | MARS_VDP_PRIO_32X | MARS_VDP_MODE_256;
    SHSTAGE(5, 0x03E0);                  /* GREEN: display mode set */
    m_boot_init();

    int par = 0;
    uint16_t tile_cmd = 0;                   /* outstanding CMD_TILE, if any */

/* CHAIN ADVANCE, callable from BOTH poll branches (2026-08-26). The
 * meter run put numbers on the 52% overload: chain span 511 lines of
 * which ~489 was the slave WAITING for the next link post (163 lines
 * per link) — because this advance only ran in the no-window branch,
 * and the window branch owns ~190 of every 262 lines. The advance is
 * two SDRAM writes and a bq push; running it at the window branch's
 * safe points removes the latency WITHOUT self-chain's back-to-back
 * slave (the bus-economy law stands: the slave still idles between
 * chains, the master still owns the schedule). */
#define CHAIN_ADVANCE() do {         if (pend_rg && SYNC[1] == pend_wait) {             tile_cmd = (uint16_t)(CMD_TILE | ((uint16_t)pend_rg << 4)                                   | ((uint16_t)pend_par << 8)                                   | pend_bank);             slave_cmd(tile_cmd);             pend_wait = tile_cmd;             BQ_PUSH(pend_rg, pend_par, pend_bank);             pend_rg = (uint8_t)((pend_rg == 1) ? 2 : 0);         } } while (0)
#ifdef WIN_TWO
    uint8_t pend_rg = 0;                     /* slave-launch chain: next band
                                              * (1 then 2, 0 = none) posted
                                              * from the poll loop when the
                                              * previous echo lands */
    uint8_t pend_par = 0, pend_bank = 0;
    uint16_t pend_wait = 0;                  /* the cmd whose SYNC[1] echo
                                              * gates the next chain post —
                                              * INDEPENDENT of tile_cmd,
                                              * which the shared per-window
                                              * drain zeroes (the first cut
                                              * used tile_cmd here and the
                                              * chain wedged: master spun
                                              * on a stale echo, cycle
                                              * dead at 2/65s) */
#endif

    /* Master band work is an INTERRUPTIBLE state machine: queued per
     * window, processed in 12-row strips between COMM0 polls. A busy
     * master tail used to delay window pickup past vblank -> silent
     * blit skip -> that band displayed a full-cycle-old frame (the
     * ares "floating heads / split sprites" staleness). Now pickup
     * latency is bounded by one strip (~0.4ms). */
#ifndef NATIVE_FRAME
    struct band {
        uint8_t on, rg, bpar, bank, phase, s0, cnt;
        uint8_t sub;      /* rows of the current strip already composed:
                           * a strip that yielded to the window RESUMES
                           * here instead of recomputing from its start */
    };
    struct band bq[8];               /* 8-deep (2026-08-25): SELF-CHAIN
                                              * pushes all three master halves
                                              * at once; 4-deep overflowed and
                                              * tripled the drop counters */
    int bq_h = 0, bq_t = 0;
#endif
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
#ifndef NATIVE_FRAME
    uint8_t drop_s0[3] = {0, 0, 0};
#endif
#ifdef NATIVE_FRAME
    /* NATIVE: "master busy" for the maintenance-slot arbitration is
     * the per-generation tail, not a band queue. */
#define MQ_BUSY (nat_gen_open && nat_mtask)
#else
#define MQ_BUSY (bq[bq_h].on)
#endif
#ifdef WIN_TWO
    /* enqueue-with-post (v8 ares: 3 bands enqueued at once vs a
     * depth-4 queue = deferrals 3506 = the cutscene stutter). Each
     * chained band's bq entry is created WHEN its slave cmd posts. */
#define BQ_PUSH(rgv, parv, bankv) do { \
        if (bq[bq_t].on) { \
            /* FULL (2026-08-31, Mike's frozen Neff smoke): dropping \
             * the NEWCOMER starved R2 — the chain pushes R2 LAST \
             * every cycle, so a saturated queue dropped R2's push \
             * ~every cycle of the pillar scene (bs1: 896 drops \
             * concentrated in the ~1000-cycle window; sbuf fresh, \
             * both banks' R2 rows bit-identical). COALESCE instead: \
             * refresh a queued not-yet-started SAME-BAND entry with \
             * the new generation (newest wins = complete-or-defer's \
             * own policy); never touch bq_h (may be mid-strip). A \
             * real drop only when no candidate exists. */ \
            uint8_t ci2 = 8; \
            for (uint8_t qi2 = 0; qi2 < 8; qi2++) \
                if (qi2 != bq_h && bq[qi2].on && bq[qi2].rg == (rgv) \
                    && bq[qi2].phase == 0 && bq[qi2].sub == 0) { \
                    ci2 = qi2; break; } \
            if (ci2 < 8) { struct band *cb2 = &bq[ci2]; \
                cb2->bpar = (uint8_t)(parv); cb2->bank = (uint8_t)(bankv); \
                cb2->s0 = drop_s0[(rgv)]; cb2->cnt = 0; } \
            else { DIAG[13]++; \
                ((volatile uint32_t *)0x26028FC8)[(rgv)]++; } } \
        else { struct band *nb2 = &bq[bq_t]; \
            RD_OPEN_M(rgv); /* ROW-DEFER: master half open until the \
                             * terminator */ \
            nb2->on = 1; nb2->rg = (uint8_t)(rgv); \
            nb2->bpar = (uint8_t)(parv); nb2->bank = (uint8_t)(bankv); \
            nb2->phase = 0; nb2->s0 = drop_s0[(rgv)]; \
            nb2->cnt = 0; nb2->sub = 0; \
            bq_t = (bq_t + 1) & 7; } } while (0)
#endif
    uint16_t t_vint = 0;                 /* FRT at last window pickup */
    uint8_t shadow_stole = 0;            /* one stolen LUT chunk per window */
    pg_pending = 0x1FFF;                 /* dirty tilemap pages awaiting
                                          * copy (write-observer ring:
                                          * bitmap rides the DREQ tail;
                                          * boot = all pages once) */
    cycle_dirt = 0x1FFF;                 /* PRESENTATION 2.0: pages the game
                                          * wrote into the CURRENT draw bank
                                          * since the last flip — exactly the
                                          * pages STALE in the other bank.
                                          * Restored there at the k2 flip
                                          * from TILEMAP_U truth. Boot =
                                          * all pages once, so the ex-decoy
                                          * bank gets a full staging copy at
                                          * the first flip. (File-scope now
                                          * — the flip span moved into
                                          * flip_span(), see LOOP24.) */
#ifdef PG_STICKY
    for (int i = 0; i < 13; i++)         /* fixed scrap: no .bss zeroing */
        pg_quiet[i] = 0;
    pg_deep = 0;
#endif
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
#ifdef CUT_BLANK
#ifdef CAT1_MD
    uint8_t  md_cut_ext = 0;             /* dirtiness extensions of a cut */
#endif
    uint8_t  md_cut = 0;                 /* chunk-visits left in cut mode
                                          * (armed by a claim storm) */
#endif
#endif
#ifndef NATIVE_FRAME
    for (int i = 0; i < 8; i++) {
        bq[i].on = 0;
        bq[i].sub = 0;
    }
#endif
    for (int i = 0; i < 256; i++) {      /* fixed blocks aren't .bss-zeroed */
        cram_mirror[i] = 0;
        shadow_lut[i] = (uint8_t)i;      /* identity until first rebuild */
    }
#ifdef FLICK_FUSE
    {
        static const uint8_t bay[16] = { /* 4x4 Bayer, thresholds 0..15 */
            0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
        for (int i = 0; i < 16; i++)
            flick_bay[i] = bay[i];
        for (int i = 0; i < 64; i++)
            flick_lvl[i] = 0;
        for (int t = 0; t < 4; t++)
            flick_trk_tab[t].live = 0;
    }
#endif
    BM->active = 0;
    DIAG[18] = BUILD_HASH32;             /* savestates self-identify */
#ifdef FB_BENCH
    fb_bench();                          /* direct-draw economics spike:
                                          * see fb_bench() (cart .text —
                                          * RAMCODE bytes are guard
                                          * bytes). NEVER SHIP. */
#endif

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
#ifdef VISR_FLIP
    visr_arm = 1;                        /* everything the flip span touches
                                          * is initialised above; the V-ISR
                                          * may act from the next vblank on */
#endif
    /* PIPELINE-ARMED BEACON (2026-09-06, attract parity): the 68K holds
     * the arcade program at its boot until this lands, so the game's
     * frame 1 starts with a live MD-plane channel — before, the game ran
     * ~45 frames ahead of the pipeline and its blank-loaded title card
     * (frames 1-19, display off) was loaded on-screen instead. COMM14 is
     * the 68K's own 0xB007 beacon register (the slave consumed it long
     * ago in s_main.c); 0xB008 = master armed. */
    while (MARS_SYS_COMM14 != 0xB007) ;  /* the 68K's beacon first (it
                                          * lands ~3 frames after ours) */
    {   /* let the slave's own B007 poll (s_main.c) see it before we
         * overwrite: two frames of FRT is thousands of its polls */
        uint16_t tb0 = frt();
        while ((uint16_t)(frt() - tb0) < 4000) ;
    }
    MARS_SYS_COMM14 = 0xB008;
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
#ifdef BOOT_FMCHK
            if (*(volatile uint16_t *)0x2000402A == 0xDEAD) {
                ((volatile uint16_t *)0x20004200)[0] = 0x001F;     /* RED: 68K says FM stuck */
                MARS_SYS_INTMSK &= 0x7FFF;                          /* try the drop again */
                ((volatile uint16_t *)0x20004200)[0] =
                    (MARS_SYS_INTMSK & 0x8000) ? 0x7C00 : 0x03E0;   /* BLUE: cannot clear / GREEN: cleared */
                *(volatile uint16_t *)0x2000402A = 0;
            }
#endif
#ifdef ARM_GATE
            /* LOOP 27 entry 7 — LATE ANNOUNCE SERVICE. No window is open
             * here. When the previous window overran the vint, the V-ISR
             * bailed "stale" at line 0 and the 68K's announce came after
             * its ack (line ~20+): nobody armed, and the push landed on
             * the old transfer's counter (displaced; Mike's census: 88 of
             * 97 tears). Service it now, exactly as the ISR would: the
             * last landing is harvested (its window closed) and nothing
             * is in flight (the 68K pushes only after this echo). */
            if (MARS_SYS_COMM6 == 0xB101) {
                MARS_SYS_COMM6 = 0;
                dreq_rearm(1);
                MARS_SYS_COMM4 = 0xA001;
                ((volatile uint32_t *)0x2603A7C8)[0]++;   /* late arms (body) */
            }
#endif
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
#ifdef R60
            /* PARK: an announce was seen — a DREQ landing is in flight
             * and the window post follows within lines. Spin quietly on
             * COMM0 (register read, no cart/FB traffic) instead of
             * composing into the landing: the push drains against an
             * idle master. Bounded: ~2 frames, then unpark (a rejected
             * vint never posts). */
            if (visr_park) {
                /* PACED: a tight COMM0 spin hammers the adapter bus the
                 * DMAC drains the FIFO through -- measured 0.28 lines/word
                 * push against a "parked" master vs LOOP7's 0.063 drain.
                 * Idle means BUS-idle: FRT (on-chip, zero bus) between
                 * COMM0 checks every ~16 ticks. */
                uint32_t pk = 40000;
                while (!MARS_SYS_COMM0 && --pk) {
                    uint16_t tq = frt();
                    while ((uint16_t)(frt() - tq) < 16) ;
                }
                visr_park = 0;
#ifndef LAND_PARK
                SYNC[12] = 0;            /* unpark the slave too */
#endif
                continue;                /* re-poll: pick the post up */
            }
#endif
#ifdef SPAN_PROBE
            m_stage = 0;                     /* nothing in flight at poll */
#endif
#if defined(WIN_TWO) && !defined(NATIVE_FRAME)
            /* slave-launch chain: R1 then R2 go out as soon as the
             * previous band's echo lands — mid-gap, off every critical
             * path. One SYNC read per poll visit. */
            CHAIN_ADVANCE();
#endif
#ifdef NATIVE_FRAME
            /* NATIVE generation close, gap-side: slave echo in AND the
             * master tail done -> the blit may ship it next window. */
            NAT_CLOSE_CHECK();
#endif
            if (!MQ_BUSY && maps_owed) {
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
            if (!MQ_BUSY && shadow_dirty) {
                /* palette changed: refresh the shadow LUT, one chunk
                 * per idle visit (silhouette fallback until fresh) */
                uint16_t dt = (uint16_t)(frt() - t_vint);
#ifdef PAL_STATIC
                /* SCENE-CUT LUT RUSH: for the gaps right after a
                 * pscene switch the drain gate widens — the LUT is
                 * the silhouette clock and the cut just invalidated
                 * all of it (Mike's black-silhouette tag). */
                if (dt <= (pscene_rush ? 9000 : 4000)) {
#else
                if (dt <= 4000) {
#endif
                    TOK(0);                  /* busy: a LUT chunk */
#ifdef SPAN_PROBE
                    m_stage = 2;
#endif
                    shadow_lut_chunk();
                }
                continue;
            }
            if (MQ_BUSY && !shadow_stole &&
                (maps_owed || (((win_no & 3) == 0
#ifdef PAL_STATIC
                                || pscene_rush
#endif
                               ) && shadow_dirty))) {
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
#ifdef NATIVE_FRAME
            /* NATIVE MASTER TAIL: the per-generation task list — the
             * three master text half-bands, then the build_maps
             * prescan — replaces the band-queue strip executor. Same
             * quiet-zone discipline (the pre-vint thresholds are the
             * band strips' own, measured numbers); one bounded chunk
             * per poll visit; the slave's whole-frame chain runs
             * concurrently and the generation closes when BOTH sides
             * are done. cache_fill drains as idle maintenance after
             * the tail — same placement the old phase 5 had. */
#if !NAT_ALL_SLAVE
            if (nat_gen_open && nat_mtask == 1) {
                /* MASTER ROW COMPOSE (mtask stage 1 — see NAT_MLO):
                 * 12-row strips, old strip quiet-zone numbers (sprites
                 * get the wider pre-vint margin the band strips earned:
                 * zoomed actors run ~1ms+). Empty ranges (BANDSHIFT=36)
                 * fall through to maps in a handful of no-op visits. */
                uint16_t dt = (uint16_t)(frt() - t_vint);
#ifdef PRESSURE_TEST
                if (dt > (nat_mphase == 1 ? 6000 : 6500))
                    continue;
#else
                if (dt > (nat_mphase == 1 ? 10300 : NAT_DRAIN_CUT))
                    continue;
#endif
                uint16_t tq = frt();
                TOK(0);                  /* busy: a master compose strip */
                if (nat_mphase == 3) {
                    if (NAT_TB(nat_mrg) < NAT_TE(nat_mrg))
                        compose_text(NAT_TB(nat_mrg), NAT_TE(nat_mrg),
                                     nat_par);
                    nat_mphase = 0;
                    nat_my = 0;
                    if (++nat_mrg >= 3) {
                        nat_mrg = 0;
                        nat_mtask = 2;   /* bands done -> maps drain */
                    }
                } else {
                    int lo = NAT_MLO(nat_mrg), hi = NAT_MHI(nat_mrg);
                    int y = lo + nat_my;
                    if (y >= hi) {
                        nat_mphase++;
                        nat_my = 0;
                    } else {
                        int ye = (y + 12 > hi) ? hi : y + 12;
                        switch (nat_mphase) {
                        case 0:          /* MD-through clear, mark-first */
                            for (int r = y; r < ye; r++) {
                                uint8_t *dp = &sbuf[(8 + r) * SBUF_W];
                                RL_ZERO(8 + r);
                                for (int x = 0; x < SBUF_W; x += 4)
                                    *(uint32_t *)(dp + x) = 0;
                            }
                            break;
                        case 1:
                            compose_sprites(y, ye, nat_par);
                            break;
                        default:         /* FG cat1 over sprites */
                            compose_layer(y, ye, 0, 0, 0, nat_bank,
                                          nat_par, 2);
                            break;
                        }
                        nat_my = (uint8_t)(ye - lo);
                    }
                }
                diag_add(12, tq);
                continue;
            }
#endif
            if (nat_gen_open && nat_mtask) {
                uint16_t dt = (uint16_t)(frt() - t_vint);
#ifdef PRESSURE_TEST
                if (dt > 6500)
                    continue;
#else
                if (dt > NAT_DRAIN_CUT) {
#ifdef MTASK_WHY
                    mt_gate_skips++;
#endif
                    continue;
                }
#endif
                uint16_t tq = frt();
#ifdef MTASK_WHY
                mt_drain_visits++;
                uint16_t mt_t0 = frt();
#endif
                TOK(0);                  /* busy: the maps drain */
                /* the tail is maps-ONLY (text moved whole to the slave:
                 * one sbuf writer per generation). Drain chunks until
                 * done or the quiet zone opens — the old dt<=8000
                 * heavy-slot deadline guarded the 4ms UNINTERRUPTIBLE
                 * build_maps; a chunk is bounded at 8 rows, so pickup
                 * latency stays one chunk and the full drain fits the
                 * FIRST gap (off the generation's critical path). */
                {
                    int done;
                    do {
                        done = build_maps_chunk(nat_par ^ 1);
#ifdef MTASK_WHY
                        mt_drain_chunks++;
#endif
                    } while (!done
                             && (uint16_t)(frt() - t_vint) <= NAT_DRAIN_CUT);
#ifdef MTASK_WHY
                    mt_drain_ticks += (uint16_t)(frt() - mt_t0);
                    if (done) mt_done++;
#endif
                    if (done) {
                        nat_mtask = 0;
#ifdef PHASE_CENSUS
                        PHS[1] = frt();  /* master tail done */
#endif
                        SYNC[9] = 0;     /* master tail complete: the
                                          * snapshot may refresh */
                    }
                }
                diag_add(11, tq);
                continue;
            }
            if (!nat_gen_open && (miss_n[0] + miss_n[1])) {
                uint16_t dt = (uint16_t)(frt() - t_vint);
                if (dt <= 8000) {
                    uint16_t tq = frt();
                    TOK(0);              /* busy: an art-cache drain */
                    cache_fill((miss_n[0] + miss_n[1] > 96) ? 384 : 128);
                    diag_add(1, tq);
                }
                continue;
            }
#else /* !NATIVE_FRAME — the band-queue strip executor */
            if (bq[bq_h].on) {
                struct band *b = &bq[bq_h];
                uint16_t dt = (uint16_t)(frt() - t_vint);
                /* PRE-VINT QUIET ZONE: the window period is ~12000 FRT
                 * ticks; past NAT_DRAIN_CUT start NOTHING and just poll, so
                 * pickup latency at the heartbeat gate is ~0. A strip
                 * begun here (~0.4ms = 6 scanlines) blew the 2-line
                 * worst-case gate slack in busy scenes (385 mskips/run,
                 * swait=0, bdrain=0 — the strip WAS the latency).
                 * Shrinking strips instead collapsed throughput to
                 * block-drain saturation (2305 mskips). */
                /* sprite strips can run ~1ms+ when big zoomed actors
                 * (cutscene Zeus head) span the rows — give phase 2 a
                 * wider pre-vint margin (mskips 311/run when it shared
                 * the tile strips' NAT_DRAIN_CUT threshold; 10400 was too wide
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
                if (dt > (b->phase == 2 ? 10300 : NAT_DRAIN_CUT))
#endif
                    continue;               /* sprite strips: gated/shadow
                                             * actors can run ~1.4ms */
                if (b->phase >= 5 && dt > 8000)
                    continue;            /* defer heavy work past the vint
                                          * (build_maps ~4ms uninterruptible:
                                          * a 9600 deadline overran the post) */
                int lo = (b->rg == 2) ? (184 + BAND_SHIFT_RG2)
                                      : (b->rg * 72 + 36 + BAND_SHIFT);
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
                /* CMDINT IS RETIRED (see docs/log/LOOP11.md): the shipping build
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
#if defined(DIRTY_ROW) && !defined(DIRECT_FB)
                        /* STAGE B, master side. The slave has the same
                         * clear in slave_concurrent_k and BOTH must
                         * assert ROWLIVE — the master/slave row split
                         * alternates bands, so a clear that forgets to
                         * mark leaves half the screen permanently
                         * "live" and silently halves the win.
                         * DIRECT_FB disables the skip: the dst bank is
                         * two intervals stale (see the slave clear). */
                        if (!ROWLIVE[8 + r])
                            continue;
#endif
#ifdef DIRECT_FB
                        uint8_t *d = DROW(8 + r);
                        RL_ZERO(8 + r);          /* MARK-FIRST, as below */
                        for (int x = 0; x < 320; x += 4)  /* LONG fills:
                                                * the FB DISCARDS zero
                                                * BYTE writes */
                            *(uint32_t *)(d + x) = 0;
#else
                        uint8_t *d = &sbuf[(8 + r) * SBUF_W];
                        /* MARK-FIRST (pass 12c, the false-dead race):
                         * RL_ZERO BEFORE the pixel wipe. The deferred
                         * cat1/text pass draws these rows from the
                         * OTHER CPU one gap late; with zero-after-wipe
                         * the interleave draw->MARK->RL_ZERO left
                         * pixels present on a dead-marked row (5376
                         * false claims/3600f, rows 176-207 = the
                         * purple band + HUD dropouts). Mark-first
                         * makes every interleave end with the drawer's
                         * MARK last — worst case is a one-frame lost
                         * glyph, never a lying mask. */
                        RL_ZERO(8 + r);
                        for (int x = 0; x < SBUF_W; x++) d[x] = 0;
#endif
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
                    SYNC[9] &= (uint16_t)~(1u << b->rg); /* master half
                                                          * complete (also
                                                          * unlatches the
                                                          * sprite snapshot
                                                          * refresh) */
                    b->on = 0;
                    bq_h = (bq_h + 1) & 7;
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
#endif /* !NATIVE_FRAME */
            continue;
        }
        {
            int k = (c0 >> 4) & 3;
            uint16_t bank1 = MARS_SYS_COMM2 & 7;
#ifdef DIRECT_FB
            (void)bank1;                 /* stage 2: scmd (its only reader)
                                          * is compiled out with the blit */
#endif
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
#ifdef HS_SHIP
            HS_OFFS[6] = 0;              /* new window: nothing copied yet */
#endif

#ifdef PAL_STATIC
            if (pscene_rush)
                pscene_rush--;           /* rush is per-window-gap */
#endif
#ifdef NATIVE_FRAME
            /* generation close, window-side twin of the gap check: a
             * chain that finished in the gap's tail still ships THIS
             * window instead of waiting out a whole vint. */
            NAT_CLOSE_CHECK();
#endif
#ifdef R60
#ifndef LAND_PARK
            SYNC[12] = 0;                /* unpark the slave with us */
#endif
            visr_park = 0;               /* the post is taken — unpark.
                                          * Left set, the park ate the
                                          * ENTIRE inter-window gap:
                                          * strips and the slave chain
                                          * starved, compose never ran,
                                          * slave-tmo 774/1007 frames. */
#endif
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
             * whole compose (the retry-loop saturation, docs/log/LOOP.md iter4).
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
#ifdef FM_GATE
                    /* widened with the MD entry gate (game-masked-IRQ
                     * vint latency): heartbeat carries entry V up to
                     * ~E8 now; give the same slack plus margin */
                    skip = (disp_blank || !r60_disp_on) ? 0
                         : (v < 0xDF || v > 0xEA);
#else
                    skip = (disp_blank || !r60_disp_on) ? 0
                         : (v < 0xDF || v > 0xE4);
#endif
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
                if (skip && k == 2
#ifdef VISR_FLIP
                    && !visr_flip_done       /* the ISR already flipped this
                                              * cycle, legally (it fired at
                                              * vblank entry); a late BODY
                                              * pickup is then not a missed
                                              * flip */
#endif
                    )
                    DIAG[7]++;               /* missed flips = dropped frames
                                              * (blits no longer skip at all:
                                              * they write the hidden bank) */
            /* (reject-loss healing DELETED 2026-08-17: MDVERIFY proved
             * the FB packet channel LOSSLESS on ares — 7079 packets, 0
             * stale re-reads, 0 seq jumps. The belt defended a loss
             * mode that does not exist, and WIN_TWO needed its region-
             * guard bytes back. The loss backstop that DOES matter —
             * the rotating force-full row — stays in the nt builder.) */
#ifdef SPAN_PROBE
                if (skip)
                    DIAG[42 + m_stage]++;    /* v3: miss by master stage */
#endif
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
#ifdef SPR_LINE_PROBE
            if (k == 1) {
                {
                    unsigned na = sl_pop(sl_set[0], sl_set[1]);
                    unsigned ne = sl_pop(sl_seteg[0], sl_seteg[1]);
                    if (na > SLC[6]) SLC[6] = na;
                    if (na > sl_ans[0]) sl_ans[0] = na;
                    if (ne > SLC[7]) SLC[7] = ne;
                    if (na | ne) {
                        SLC[15]++;
                        SLC[8 + (ne <= 1 ? 0 : ne <= 2 ? 1 : ne <= 3 ? 2 :
                                 ne <= 4 ? 3 : ne <= 6 ? 4 : ne <= 8 ? 5 : 6)]++;
                    }
                    /* worst span occupancy at 8, 4, 2 swaps per frame */
                    unsigned w8 = 0, w4 = 0, w2 = 0;
                    for (int sp = 0; sp < SL_SPANS; sp++) {
                        unsigned v = sl_pop(sl_span[sp][0], sl_span[sp][1]);
                        if (v > w8) w8 = v;
                    }
                    for (int sp = 0; sp < SL_SPANS; sp += 2) {
                        unsigned v = sl_pop(sl_span[sp][0] | sl_span[sp+1][0],
                                            sl_span[sp][1] | sl_span[sp+1][1]);
                        if (v > w4) w4 = v;
                    }
                    for (int sp = 0; sp < SL_SPANS; sp += 4) {
                        unsigned a0 = sl_span[sp][0] | sl_span[sp+1][0]
                                    | sl_span[sp+2][0] | sl_span[sp+3][0];
                        unsigned a1 = sl_span[sp][1] | sl_span[sp+1][1]
                                    | sl_span[sp+2][1] | sl_span[sp+3][1];
                        unsigned v = sl_pop(a0, a1);
                        if (v > w2) w2 = v;
                    }
                    if (w8 > SLC[16]) SLC[16] = w8;
                    if (w8 > sl_ans[1]) sl_ans[1] = w8;
                    if (w4 > SLC[17]) SLC[17] = w4;
                    if (w4 > sl_ans[2]) sl_ans[2] = w4;
                    if (w2 > SLC[18]) SLC[18] = w2;
                    if (w2 > sl_ans[3]) sl_ans[3] = w2;
                    if (w8 <= 3) sl_ans[4]++;
                    if (w8 <= 3) SLC[19]++;          /* cycles 8 swaps would serve */
                    if (w4 <= 3) sl_ans[5]++;
                    if (w4 <= 3) SLC[20]++;
                    if (w2 <= 3) sl_ans[6]++;
                    if (w2 <= 3) SLC[21]++;
                    sl_ans[7]++;
                    for (int sp = 0; sp < SL_SPANS; sp++)
                        sl_span[sp][0] = sl_span[sp][1] = 0;
                    sl_set[0] = sl_set[1] = sl_seteg[0] = sl_seteg[1] = 0;
                }
                for (int yy = 0; yy < 224; yy++) {
                    unsigned nm = SL_MD(yy), np = SL_PX(yy);
                    if (nm || np) {
                        SLC[4]++;
                        if (nm > 20)  SLC[0]++;
                        if (np > 320) SLC[1]++;
                        if (nm > SLC[2]) SLC[2] = nm;
                        if (np > SLC[3]) SLC[3] = np;
                    }
                    SL_SPR(yy) = 0; SL_PX(yy) = 0; SL_MD(yy) = 0;
                }
            }
#endif
#ifndef DIRECT_FB
            /* REBUILD STAGE 2: the whole preempt-blit command flow only
             * exists to ship sbuf slices. DIRECT_FB composes into the FB
             * back bank, so the flag build compiles it out — the window
             * shrinks to flip + harvest + CRAM/pages + ack. */
            uint16_t scmd = (uint16_t)(0x3000 | (k << 4) | (par << 8)
                                       | bank1);
            uint32_t guard;
            SYNC[2] = 0;
            SYNC[5] = 0;                      /* (iter4) preempt-blit echo
                                               * (SYNC[3]'s clear retired:
                                               * write-only vestige of the
                                               * old flip-pair protocol) */
#endif
#ifdef FB_XPORT
            /* LIFT THE PACKET BEFORE THE FLIP (LOOP27 72). The 68K wrote
             * it at FM=0 into the bank that was current then; flip_span()
             * below swaps banks. FBXLATE=1 moves this call below the flip
             * so the two positions can be compared on the same rig. */
#ifndef FBX_LATE
#ifdef TEXTCAP_EARLY
            fs_posted_early = 0;         /* this vint's early post is spent */
#endif
#ifdef FBX_ISRLIFT
            if (!fbx_landed)             /* FBXISRLIFT: flip_span's entry
                                          * lifted already on the ISR path;
                                          * this is the body fallback */
#endif
            fbx_lift();
#endif
#endif
            tp = frt();
            /* THE flip — before this window's blit: R0 of the NEXT
             * frame must land in the NEW draw bank (frame N completed
             * at k1). The span itself (capture -> truth drain -> flip
             * -> restore) moved whole into flip_span() above so the
             * V-ISR can run it at vblank entry (LOOP24). Under
             * VISR_FLIP the ISR usually already flipped this cycle;
             * consume its flag INSTEAD of flipping, and the barrier
             * forces reloads of the page/dirty state the ISR
             * mutated behind the compiler's back. */
#ifdef VISR_FLIP
            {
                uint8_t isr_flipped = 0;
                if (k == 2 && visr_flip_done) {
                    visr_flip_done = 0;
                    isr_flipped = 1;
                }
                __asm__ __volatile__("" ::: "memory");
                if (k == 2 && !skip && !isr_flipped) {
#ifdef NATIVE_FRAME
                    if (nat_capt) {
                        nat_capt = 0;        /* ISR already captured and
                                              * declined (whole-frame
                                              * hold): a second capture
                                              * pass inside the FM span
                                              * buys nothing */
                    } else
#endif
                    {
#ifdef HS_CENSUS
                    HSC_RING[(HSC_IDX & 15) * 2 + 1] = hsc_win; HSC_IDX++;  /* body flip */
#endif
#ifdef FLIP_CENSUS
                    CEN[6]++;               /* reached the body flip */
#endif
                    DIAG[56]++; CEN[20]++;    /* body-fallback flip: the ISR
                                              * declined this cycle (bail
                                              * counters say why) */
                    flip_span();
                    }
                }
            }
#else
            if (k == 2 && !skip)
                flip_span();
#endif
            WSTAGE(0x03E0);                      /* GREEN: flip span done */
#if defined(TWO_POST) && defined(FB_TEXT_READ)
            tp_text_restore();                   /* post B: FM=1 again, before the ack */
#endif
            diag_add(6, tp);                 /* slot 6: flip+truth+restore */
#if (defined(DIRECT_FB) || defined(NATIVE_FRAME)) && defined(R60)
            /* STAGE-2 A/B RESULT (2026-08-27): deleting the landing wait
             * with the blit block nearly doubled bad1 (295 -> 549 on the
             * 1900f battery) — the wait was never only blit ordering; it
             * is what lets the DREQ push drain against an idle master
             * before the pre-ack bus work starts. Kept on its own.
             * NATIVE runs it here for the same reason: the blit block
             * below is generation-gated and skip windows must still
             * give the landing a quiet drain.
             * BOUND 4000, not the legacy 1600: that number assumed the
             * blit pickup had already eaten most of the push; with no
             * pickup ahead of this wait the full ~1-2ms push (~720-1440
             * ticks) plus its start latency must fit inside the bound
             * or the harvest tears the landing (bad1). */
#ifdef FM_LATE
            if (0)   /* v2: blit DURING the push; the 68K waits for our ack */
#endif
#ifdef NO_LAND_WAIT
            /* BISECT: the landing wait, removed on the DREQ build too, so
             * the flip collapse under FBXPORT can be attributed. */
            if (0)
#endif
#ifdef FB_XPORT
            /* NOTHING TO DRAIN (LOOP27 67). This wait watches TCR0 go
             * quiet, and on the FB route no DREQ transfer ever runs: TCR
             * sits at R60_ARM, the break condition is unreachable, and
             * the master burned the FULL 4000-tick bound EVERY window.
             * That was the whole of the first FB build's slowdown. The
             * packet is in memory before its publish word exists, so
             * there is no drain to wait for. */
            if (0)
#endif
            {
                WSTAGE(0x001F);                  /* RED: window, at the landing wait */
                uint16_t w0 = frt();
                uint32_t t_prev = SH2_DMA_TCR0 & 0xFFFFFFu;
                while ((uint16_t)(frt() - w0) < 4000) {
                    uint16_t w1 = frt();
                    while ((uint16_t)(frt() - w1) < 64) ;
                    uint32_t t_now = SH2_DMA_TCR0 & 0xFFFFFFu;
                    if (t_now == t_prev && t_now != (uint32_t)R60_ARM)
                        break;
                    t_prev = t_now;
                }
            }
            WSTAGE(0x03FF);                      /* YELLOW: landing wait done */
#endif
#ifdef FM_LATE
            /* FM LATE (2026-09-06): the 68K drops FM after our F103 and
             * pushes at FM=0, then raises FM again for THIS window's FB
             * work (blit, publish, MDSPR) and waits for the ack inside
             * its shim, so game code never runs with FM up. Wait here
             * for that second raise; bounded (a missed raise = today's
             * behaviour, FM already up). */
            {
                uint16_t wf = frt();
                ((volatile uint32_t *)0x26028FF4)[0] = (uint16_t)(wf - t_vint);   /* pickup->landing done */
                (void)wf;   /* v2: FM is already up (raised at the post) */
                ((volatile uint32_t *)0x26028FF4)[1] = (uint16_t)(frt() - t_vint); /* ->FM up */
            }
#endif
#ifndef DIRECT_FB
            tp = frt();
#ifdef NATIVE_FRAME
            /* LAST-CALL CLOSE CHECK: an echo that landed during the
             * flip span or the landing wait above still ships THIS
             * window — without this, a close missing the entry check
             * by microseconds waited out a whole extra vint (the
             * first battery's 2.9-vint generation period). */
            NAT_CLOSE_CHECK();
            /* WHOLE-SCREEN-OR-NOTHING SHIP: the blit runs only on a
             * window holding a closed, unshipped generation. sbuf can
             * never ship mid-compose — a new generation cannot launch
             * while one awaits its blit — so every blit is a coherent
             * whole frame from ONE latched game state. Skip windows
             * shrink to flip-decline + harvest + captures + publish
             * (the 68K gets the difference back). */
#ifdef BLIT_CHASE
            /* BLIT CHASE (2026-09-02, pipelining arc step 2): post the
             * slave's blit half NOW, run the pre-ack work and the
             * launch, and blit the master's half AFTER the launch — the
             * next generation composes while this one ships. Order
             * law: the slave does its blit half before its compose
             * (SYNC[4] drained at compose entry), and SYNC[14] is the
             * first master row not yet shipped; the slave waits on it
             * before bands 1 and 2 (rows 136-223 are the master's). */
            nat_ship_now = (uint8_t)nat_gen_ready;
            if (nat_ship_now) {
#ifdef WAIT_PROBE
                rg_tpost = frt();
#endif
#ifdef ROW_GEN
                rowgen_ship(fb_draw_par ? 1 : 0);   /* before the slave post */
#endif
#ifdef BLIT_SKIP
                scmd |= (uint16_t)(fb_draw_par << 6);
#endif
                SYNC[14] = 112 + BLIT_SHIFT;
                SYNC[4] = scmd;
                cache_purge();
            }
            if (0)
#else
            if (nat_gen_ready)
#endif
#endif
            {
                /* (iter4) PREEMPT MAILBOX: hand the slave its blit half
                 * WITHOUT first draining its concurrent compose. The slave
                 * services SYNC[4] between compose strips, so pickup
                 * latency is <=1 strip instead of a whole compose. The
                 * blitted rows and the region the slave is still composing
                 * are DISJOINT by pipeline construction: Wk blits its
                 * blit-set while the outstanding compose (launched at the
                 * previous window) covers a different region band. */
#ifdef BLIT_SKIP
                /* PUBLISH THE BANK LABEL HERE, NOT AT scmd's construction
                 * — scmd is built BEFORE the k2 flip block and the flip
                 * toggles the parity. Posting it here is what makes the
                 * slave's blit and the master's blit agree on the bank
                 * without either of them reading FBCTL. (The SYNC[5]
                 * echo compares against scmd, so bit 6 must be set
                 * before both the post and the compare — it is.) */
                scmd |= (uint16_t)(fb_draw_par << 6);
#endif
#if defined(R60) && !defined(NATIVE_FRAME)
                /* (NATIVE hoists this wait OUT of the gated block — see
                 * the standalone copy above the blit gate — so skip
                 * windows still drain the landing quietly.) */
                /* WAIT FOR THE LANDING before any blit traffic — the
                 * design rule that inverts LOOP25's FIFO verdict: the
                 * push drains against an idle master. TE never sets
                 * (arm > push), so wait for TCR stability: unchanged
                 * across ~64 ticks AND at least one word landed, or
                 * a 1600-tick bound (the push is ~1-2ms; pickup ate
                 * most of it already). */
                {
                    uint16_t w0 = frt();
                    uint32_t t_prev = SH2_DMA_TCR0 & 0xFFFFFFu;
                    /* Bound 4000, was 1600 (2026-08-28, backported from
                     * the DIRECTFB stage-2 A/B): canonical battery
                     * bad1 150 -> 100 with cadence/rejects unmoved;
                     * bq drops +16% but drops are COHERENT under the
                     * blit (sbuf re-ships the last complete band). */
                    while ((uint16_t)(frt() - w0) < 4000) {
                        uint16_t w1 = frt();
                        while ((uint16_t)(frt() - w1) < 192) ;
                        /* STABILITY 64 -> 192 (2026-08-30, the slowness
                         * conviction): TAILPROBE stamped the 68K push
                         * at ~100 LINES for a 151-word mean packet —
                         * the 68K is not writing, it is STALLED on the
                         * FIFO-full bit while the blit saturates the
                         * bus. 64 ticks of TCR quiet reads a 68K
                         * hiccup as "push done"; the master then blits
                         * and buries the rest of the push. 192 rides
                         * out the hiccups so the push drains against
                         * THIS idle wait instead. */
                        uint32_t t_now = SH2_DMA_TCR0 & 0xFFFFFFu;
                        if (t_now == t_prev && t_now != (uint32_t)R60_ARM)
                            break;
                        t_prev = t_now;
                    }
                }
#endif
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
                 * DO NOT EVEN THESE THIRDS — docs/log/LOOP.md negative 23. */
#ifdef WIN_TWO
                /* 2-window cycle, EVEN split (v8 ares: 144/80 rows made
                 * k1 fat -> rejects 4.7%): k1 ships rows 0-112 (R0 +
                 * R1's head), k2 ships 112-224 (R1's tail + R2). Blit
                 * ranges need no band alignment — deadlines unchanged
                 * (R1 complete by k1, R2 by k2). 56 rows/CPU/window. */
                if (k == 2) {
#ifdef R60
                    /* master half; see BLIT_SHIFT at the slave call */
                    BLIT_HALF(112 + BLIT_SHIFT, 224, fb_draw_par);
#else
                    BLIT_HALF(168, 224, fb_draw_par);
#endif
                } else {
                    BLIT_HALF(56, 112, fb_draw_par);
                }
#else
                if (k == 2)
                    BLIT_HALF(36, 72, fb_draw_par);
                else if (k == 0)
                    BLIT_HALF(108, 144, fb_draw_par);
                else
                    BLIT_HALF(184, 224, fb_draw_par);
#endif
#ifdef WIN_SPLIT_PROBE
                diag_add(23, tb);
#ifdef WIN_TWO
                /* WIN_TWO: the master blits 56-112 at k1 and 168-224 at
                 * k2 — 56 rows either way. The 40/36 below is the LOOP 9
                 * THIRDS split (36-72 / 108-144 / 184-224) and has been
                 * wrong ever since WIN_TWO landed: it divided by ~38
                 * instead of 56, so every "ticks/row" and "full-frame
                 * lines" figure this probe has printed on a WIN2 build is
                 * inflated 1.47x. Ratios between two WIN2 builds were
                 * unaffected (same wrong divisor both sides), which is
                 * why the LOOP 18 A/B conclusion stands — but the
                 * ABSOLUTE numbers in LOOP 18 before this fix do not. */
                DIAG[25] += 56;
#else
                DIAG[25] += (k == 1) ? 40 : 36;
#endif
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
#ifdef NATIVE_FRAME
                nat_gen_ready = 0;
#ifdef PHASE_CENSUS
                nat_ph_ship(t_vint);
#endif
#ifdef HS_SHIP
                hs_promote();
#endif
                nat_shipped = 1;         /* the hidden bank now holds a
                                          * complete generation: the next
                                          * vint's ISR flip is legal */
#endif
            }
            diag_add(5, tp);
#endif /* !DIRECT_FB — stage-2 window diet */

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
#ifdef R60
            /* ================= R60 HARVEST =================
             * ONE packet, same-vint, landed against a body that
             * WAITED for it (see the pre-blit landing wait). Layout
             * from packet_fmt.h R60 family; K from the tag; records
             * offset varies with K. Validation: exact family
             * arithmetic + the magic tail at landed-2. On any
             * failure: apply NOTHING, recover marks from COMM10
             * (c10 insurance), keep last frame's SPR_SNAP. */
            {
                static uint16_t r60_c10_prev;
                /* LOST-PUSH BELT (2026-09-05, Mike's black boss on the JP
                 * rom, rom/s16_altbeastj.bs1): 254 torn landings but only
                 * 243 BAD1 echoes consumed — a push that lands ZERO words
                 * echoed nothing, and a tear whose echo was still
                 * unconsumed when the next tear arrived collapsed into
                 * one. The 68K's shadow then claims the words shipped and
                 * the delta path never sends them again: the boss's
                 * one-shot palette block stayed black for the session.
                 * bad1_post pends the echo until COMM8 is free; arm_seen
                 * (the 68K's k1 announce) turns a zero landing into a
                 * tear too. The 68K re-marks its last TWO pushes. */
#ifdef FB_XPORT
                /* FB TRANSPORT (LOOP27 67). The packet came through the
                 * framebuffer, so there is no TCR to interrogate and no
                 * partial landing to reconstruct: the 68K's publish word
                 * is written after every payload word, and its sequence
                 * says whether this window has a new one. Copy it into
                 * SPR_LAND and the entire harvest below runs unchanged
                 * on identical bytes.
                 * A stale or malformed publish yields landed = 0, which
                 * is the existing "no packet this vint" path — last
                 * frame's records stand. */
                /* the pre-flip lift above already copied it into
                 * SPR_LAND and validated the publish word */
#ifdef FBX_LATE
                fbx_lift();                   /* A/B: after the flip */
#endif
#ifdef FLIP_CENSUS
                CEN[7]++;                     /* harvest reached */
                if (fbx_landed) CEN[8]++;     /* ... with a packet */
#endif
                unsigned landed = fbx_landed;
                fbx_landed = 0;                /* one consumer, one packet */
                int okp = 0;
#else
                unsigned landed = (SH2_DMA_CHCR0 & 2)
                                ? (unsigned)R60_ARM
                                : ((unsigned)R60_ARM
                                   - (SH2_DMA_TCR0 & 0xFFFFFFu));
                int okp = 0;
#endif
                unsigned K = 0, nrec = 0, rs = 0, pb = R60_HDR, rec0 = R60_HDR;
                /* BOUNDED DRAIN-WAIT (pass 9): most "tears" are the
                 * harvest RACING the 68K's still-running push — the
                 * fixed post-wait loses to storm pushes at the
                 * contended drain rate, the packet reads short, and a
                 * frame of records is thrown away + re-marked for
                 * nothing. The exact-length tag lands in the first 22
                 * words, so the intended length is knowable EARLY:
                 * wait for the landing to reach it, bounded. The TCR
                 * poll is the master's own on-chip DMAC register —
                 * ZERO bus traffic, so the wait cannot slow the very
                 * drain it watches. Budget 700 ticks (~15 lines):
                 * rescues near-miss races; true word loss still
                 * shortfalls and tears (fed back as before). */
#ifndef FB_XPORT
                if (landed >= R60_HDR && landed < 924) {
                    unsigned twx = SPR_LAND[R60_W_TAG] & 0x3FFu;
                    if (twx >= 26 && twx <= 924 && landed < twx) {
                        uint16_t t0 = frt();
                        while (landed < twx
                               && (uint16_t)(frt() - t0) < 1200) {
                            landed = (SH2_DMA_CHCR0 & 2)
                                   ? (unsigned)R60_ARM
                                   : ((unsigned)R60_ARM
                                      - (SH2_DMA_TCR0 & 0xFFFFFFu));
                        }
                        DIAG[43] += (uint16_t)(frt() - t0); /* drain-wait
                                                             * census; a
                                                             * header-phase
                                                             * wait (v2)
                                                             * measured
                                                             * WORSE — do
                                                             * not re-add */
                    }
                }
#endif
                if (landed >= 26 && landed <= 924) {
                    unsigned tag = SPR_LAND[R60_W_TAG];
                    K = (tag & 0x8000u) ? ((tag >> R60_KSHIFT) & 15u) : 0u;
                    rs = (tag & R60_RS_BIT) ? 1u : 0u;
                    pb = R60_HDR + (rs ? R60_RS_W : 0u);
#ifdef PAL_DELTA
                    /* v3: the pal section is variable; its padded
                     * payload length rides as the word before the
                     * ids (same trust as the tag — both are behind
                     * the exact-length gate; rec0 is bound-checked
                     * against landed below regardless). */
                    rec0 = K ? (pb + 1u + R60_PAL_IDW + SPR_LAND[pb])
                             : pb;
                    if (rec0 > landed)
                        rec0 = 9999u;
#else
                    rec0 = pb + (K ? (R60_PAL_IDW + K * 32u) : 0u);
#endif
                    /* EXACT-LENGTH GATE (the blue-white wedge): tag bits
                     * 9..0 carry the pushed word count. An interior FIFO
                     * drop of a multiple of 8 words used to VALIDATE
                     * (order preserved -> magic still at landed-2; only
                     * the %8 arithmetic could object) and applied a
                     * SHIFTED payload — sprite blocks froze wrong with
                     * marks cleared and no BAD1. landed must equal the
                     * declared length exactly; anything else is torn. */
#ifdef TEARPROBE
                    /* probe rig (MAME cannot lose FIFO words): fake a
                     * zero landing every 61st, a double tear at 97/98 */
                    {
                        static unsigned tp_n;
                        tp_n++;
                        if (tp_n % 61u == 0) landed = 0;
                        else if (tp_n % 97u == 0 || tp_n % 97u == 1) K = 0, rec0 = 9999u;
                    }
#endif
                    if ((tag & 0x3FFu) != landed)
                        K = 0, rec0 = 9999u;   /* force validation fail */
#ifdef FB_SPR_READ
                    /* v3: no record payload — the packet ends right
                     * after the pal section (records ride FB staging,
                     * S1 strike) */
                    if (landed == rec0 + 2u
                        && *(volatile uint32_t *)
                           (SPR_LAND + landed - 2) == 0xA55A5AA5u) {
                        nrec = 0;
                        okp = 1;
                    }
#else
                    if (landed > rec0 + 2u
                        && ((landed - rec0 - 2u) & 7u) == 0u) {
                        nrec = (landed - rec0 - 2u) >> 3;
                        if (nrec >= 1 && nrec <= R60_REC_MAX
                            && *(volatile uint32_t *)
                               (SPR_LAND + landed - 2) == 0xA55A5AA5u)
                            okp = 1;
                    }
#endif
                }
                if (okp) {
                    /* push-size census: [50] += landed words, [51] += K,
                     * [52] += nrec, over [53] harvests — the handler-diet
                     * numbers (the 68K pays ~0.063 lines/word) */
                    DIAG[50] += landed; DIAG[51] += K;
                    DIAG[52] += nrec;   DIAG[53]++;
                    /* regs -> TEXT_U (latch reads it); rowscroll only
                     * when shipped (v2) — TEXT_U keeps last otherwise */
                    volatile uint32_t *d2 =
                        (volatile uint32_t *)(TEXT_U + 0x740);
                    volatile uint32_t *s2 =
                        (volatile uint32_t *)(SPR_LAND + 0);
                    for (int i = 0; i < 10; i++)
                        d2[i] = s2[i];
                    if (rs) {
                        d2 = (volatile uint32_t *)(TEXT_U + 0x7C0);
                        s2 = (volatile uint32_t *)(SPR_LAND + R60_HDR);
                        for (int i = 0; i < 30; i++)
                            d2[i] = s2[i];
                    }
                    /* tilemap dirty bitmap (13 bits); bit 15 = the game's
                     * display-enable (DISPLAY GATE below) */
                    r60_disp_on = (uint8_t)(SPR_LAND[R60_W_BM] >> 15);
#ifdef CSET_CENSUS
                    cs_disp_on = r60_disp_on;
#endif
                    {   /* LOST-PUSH BELT v3: sequence gap = a push that
                         * landed nothing (no tear to echo). Measured
                         * need: Mike's s16.bs1 lost 104 words (3 blocks)
                         * with 81 tears all echoed. Race-free: judged
                         * only from packets that DID land. */
                        uint8_t sq = (uint8_t)((SPR_LAND[R60_W_BM] >> 13) & 3);
                        /* a gap is +2/+3 (mod 4); equal = the same
                         * landing harvested again (bs1: 49 false
                         * echoes on 64 tears), not a loss */
                        uint8_t d = (uint8_t)((sq - r60_seq_prev) & 3);
                        if (r60_seq_prev != 0xFF && (d == 2 || d == 3)) {
                            MARS_SYS_COMM8 = 0xBAD1;     /* re-mark last two lists */
                            DRQR[11]++;
                        }
                        r60_seq_prev = sq;
                    }
                    pg_pending |= SPR_LAND[R60_W_BM] & 0x1FFF;
                    cycle_dirt |= SPR_LAND[R60_W_BM] & 0x1FFF;
#ifdef PG_STICKY
                    pg_watch |= SPR_LAND[R60_W_BM] & 0x1FFF;
                    if (__builtin_popcount(SPR_LAND[R60_W_BM] & 0x1FFF) >= 8)
                        pg_deep = 24;
#endif
                    /* palette blocks: ids byte-packed 2/word */
#ifdef PAL_DELTA
                    /* v3: ONE walk for both forms — a raw block (id
                     * bit 7) is a delta with implicit full masks and
                     * its 32 words in stream order. Change detect is
                     * per WRITE (v2-exact PAL_SETGEN semantics: a raw
                     * re-ship of unchanged content bumps nothing).
                     * Sets: b<32 = 4 sets of 8 entries (chset bit =
                     * i>>3); b>=32 = 2 sets of 16 (i>>4). */
                    {
                        unsigned poff = pb + 1u + R60_PAL_IDW;
                        for (unsigned j = 0; j < K; j++) {
                            unsigned w = SPR_LAND[pb + 1u + (j >> 1)];
                            unsigned idb = (j & 1) ? (w & 0xFFu) : (w >> 8);
                            unsigned b = idb & 0x3Fu;
                            /* no id belt: the 68K rotor authors ids
                             * masked to 63 (+bit 7 only), and torn
                             * packets died at the exact-length+magic
                             * gates — the v2 continue guarded a state
                             * the gates already exclude, and the
                             * region guard wants the bytes back */
                            if (b >= 32)
                                DIAG[26]++;  /* sprite-half tracer */
                            uint32_t mm;
                            unsigned vv;
                            if (idb & R60_PAL_RAW) {
                                mm = 0xFFFFFFFFu;
                                vv = poff;
                            } else {
                                mm = SPR_LAND[poff]
                                   | ((uint32_t)SPR_LAND[poff + 1] << 16);
                                vv = poff + 2;
                            }
                            volatile uint16_t *dd16 = PAL_SH + b * 32u;
                            unsigned sh = (b < 32) ? 3 : 4;
                            unsigned chset = 0;
                            for (unsigned i = 0; mm; i++, mm >>= 1)
                                if (mm & 1) {
                                    uint16_t v = SPR_LAND[vv++];
                                    if (v != dd16[i])
                                        chset |= 1u << (i >> sh);
                                    dd16[i] = v;
                                }
                            poff = vv;   /* the walk consumed exactly the
                                          * payload (raw: 32 words) */
                            unsigned s0 = (b < 32) ? (b << 2)
                                                   : (128 + ((b - 32) << 1));
                            for (unsigned t = 0; chset; t++, chset >>= 1)
                                if (chset & 1) {
                                    PAL_SETGEN[s0 + t]++;
                                    if (b >= 32)
                                        cram_paint_spr(par, s0 + t - 128);
                                }
                        }
                    }
#else
                    for (unsigned j = 0; j < K; j++) {
                        unsigned w = SPR_LAND[pb + (j >> 1)];
                        unsigned b = (j & 1) ? (w & 0xFF) : (w >> 8);
                        if (b > 63)
                            continue;
                        if (b >= 32)
                            DIAG[26]++;      /* sprite-half tracer */
                        volatile uint32_t *dd =
                            (volatile uint32_t *)(PAL_SH + b * 32u);
                        volatile uint32_t *ss = (volatile uint32_t *)
                            (SPR_LAND + pb + R60_PAL_IDW + j * 32u);
                        unsigned s0, ns, wps;
                        if (b < 32) { s0 = b << 2;                ns = 4; wps = 4; }
                        else        { s0 = 128 + ((b - 32) << 1); ns = 2; wps = 8; }
                        for (unsigned t = 0; t < ns; t++) {
                            uint32_t ch = 0;
                            for (unsigned i = 0; i < wps; i++) {
                                uint32_t v = ss[i];
                                ch |= v ^ dd[i];
                                dd[i] = v;
                            }
                            ss += wps;
                            dd += wps;
                            if (!ch)
                                continue;
                            PAL_SETGEN[s0 + t]++;
                            if (b >= 32)
                                cram_paint_spr(par, s0 + t - 128);
                        }
                    }
#endif
#ifdef PAL_STATIC
                    /* PALSTATIC DETECT+LOAD (only on vints that carried
                     * palette blocks — a scene cut always does). Probe
                     * all scenes; a match that isn't the current scene
                     * loads the baked tile+text half of PAL_SH whole
                     * and bumps every tile/text generation: apply_cram
                     * (later this same window) repaints the full
                     * picture at once instead of trickling for 10-20
                     * vints (Mike's blue-white gravestones). */
                    if (K) {
                        /* v1.1 CONFIRMED DETECT: 8 probe pairs (4
                         * discriminators against EACH other scene —
                         * a full match cannot be another baked
                         * image) and the SAME scene must match on 3
                         * CONSECUTIVE palette landings before it
                         * loads. v1's 4-pair instant load matched
                         * palettes a fade PASSES THROUGH (the pull:
                         * boss = a desaturation of normal). */
                        unsigned hit = PSCENE_N;
                        for (unsigned s = 0; s < PSCENE_N; s++) {
                            const uint16_t *pb2 = pscene_probe[s];
                            unsigned q;
                            for (q = 0; q < 16; q += 2)
                                if (PAL_SH[pb2[q]] != pb2[q + 1])
                                    break;
                            if (q == 16) {
                                hit = s;
                                break;
                            }
                        }
                        if (hit < PSCENE_N)
                            pscene_nomatch = 0;
                        else if (pscene_cur != 0xFF
                                 && ++pscene_nomatch >= 16) {
                            /* 16, not 32: nomatch counts K-vints
                             * only, and the glow mask idles most
                             * vints (rev4 measured the transform
                             * span topping out at 29). Fades stay
                             * safely under (~10 landings). */
                            pscene_nomatch = 0;
                            pscene_cur = 0xFF;
#ifdef MD_STATIC
                            for (unsigned s2 = 0; s2 < 128; s2++)
                                mds_pin[s2] = 0;   /* foreign span: dynamic rules */
                            mds_scene_cur = 0xFF;
#endif
                        }
                        if (hit < PSCENE_N && hit != pscene_cur) {
                            if (hit != pscene_cand) {
                                pscene_cand = (uint8_t)hit;
                                pscene_conf = 1;
                            } else if (++pscene_conf
                                       >= (pscene_cur == 1 ? 30 : 3)) {
                                /* BOSS-SMOKE FLAP FIX (2026-09-01, Mike's
                                 * frames 6546/6558): leaving boss_smoke
                                 * needs 30 consecutive landings — the
                                 * smoke is a FADE (arcade ref 13560 ->
                                 * 13700: teal sky + green text -> a fully
                                 * desaturated world), and its steps
                                 * re-match normal's probes for a few
                                 * landings at a time. Entering stays 3. */
                                pscene_conf = 0;
                                if (hit == 1) {
                                    /* boss_smoke: DETECT ONLY. The scene
                                     * exists for the MD boss-art upload
                                     * (mdspr_claim reads pscene_cur);
                                     * its palette is a fade the delta
                                     * pipeline already carries exactly,
                                     * like the transform span. Loading
                                     * the baked image mid-fade was the
                                     * whole-picture flash. */
                                    pscene_cur = 1;
                                    PSCENE_SW++;
                                } else {
                                    unsigned s = hit;
                                    pscene_cur = (uint8_t)s;
                                    const uint16_t *sp3 = pscene_pal[s];
#ifdef MD_STATIC
                                    /* The MD tables install ONLY for a live
                                     * palette the bake itself would have
                                     * classified as this scene: tile-half
                                     * distance to the anchor within the
                                     * bake's TOL (40). The 8-pair probes
                                     * match at the TITLE too (the game
                                     * preloads the level palette words the
                                     * probes sit on) — the title's own sets
                                     * differ by hundreds of words, and an
                                     * install there pinned 39 level sets
                                     * over the logo (Mike's pass 2026-09-07).
                                     * Measured before the image copy below
                                     * overwrites PAL_SH. */
                                    unsigned mds_dist = 0;
                                    for (unsigned i = 0; i < 1024; i++)
                                        mds_dist += (PAL_SH[i] != sp3[i]);
#endif
                                    for (unsigned i = 0; i < 1024; i += 4) {
                                        PAL_SH[i + 0] = sp3[i + 0];
                                        PAL_SH[i + 1] = sp3[i + 1];
                                        PAL_SH[i + 2] = sp3[i + 2];
                                        PAL_SH[i + 3] = sp3[i + 3];
                                    }
                                    for (unsigned g2 = 0; g2 < 128; g2++)
                                        PAL_SETGEN[g2]++;
#ifdef MD_STATIC
                                    if (s < MDSTATIC_N && mds_dist <= MDS_TOL) {
                                        mds_install(mds_table_of[s], (uint8_t)win_no);
                                        mds_scene_cur = (uint8_t)s;
                                    } else
                                        MDS[4]++;        /* refused: foreign palette
                                                          * (the title) — the hold
                                                          * check retries at the
                                                          * next cut */
                                    mds_loadgap = 32;
#endif
                                    pscene_rush = 8;
                                    /* HEAL CHANNEL (v1.1a): every
                                     * scene load posts 0xBAD2 — the
                                     * 68K re-marks all pal blocks
                                     * force-raw, so this load is
                                     * re-shipped through the differ
                                     * within ~9 vints. A WRONG load
                                     * (the v1 pull) self-heals by
                                     * the same belt. Never load
                                     * PAL_SH without this post. */
                                    MARS_SYS_COMM8 = 0xBAD2;
                                    PSCENE_SW++;
#ifdef GLOW_ANIM
                                    /* baked image wrote the glow
                                     * words at its capture phase;
                                     * re-seed after the heal dust */
                                    glow_pause = 8;
                                    glow_on = 0;
                                    glow_post = 3;
#endif
                                }
                            }
                        } else
                            pscene_conf = 0;    /* no match / current
                                                 * scene: any gap
                                                 * breaks the streak */
                    }
#endif
#ifndef FB_SPR_READ
#ifdef NATIVE_FRAME
                    /* NATIVE: records refresh SPR_SNAP only at the
                     * generation boundary (the launch site) — a mid-
                     * chain refresh is the split-actor shimmer class.
                     * Remember where this vint's whole records landed;
                     * the launch copies the freshest OK landing. */
                    nat_spr_ok = 1;
#ifdef BOOT_PKTCHK
                    { static uint16_t okl; okl++; *(volatile uint16_t *)0x2000402A = okl; }   /* probe: whole landings */
#endif
                    nat_rec0 = (uint16_t)rec0;
                    nat_nrec = (uint16_t)nrec;
#else
                    /* sprite records -> SPR_SNAP, terminator inside */
                    for (unsigned i = 0; i < nrec * 8u; i += 8) {
                        SPR_SNAP[i + 0] = SPR_LAND[rec0 + i + 0];
                        SPR_SNAP[i + 1] = SPR_LAND[rec0 + i + 1];
#ifdef MD_SPR
                        /* scrub bit13 — OUR claim mark. w2 bits 9-13
                         * are unread by the hardware/compose, but
                         * whether the GAME ever sets them is
                         * unverified; a spurious bit13 would hide an
                         * unclaimed record. */
                        SPR_SNAP[i + 2] = SPR_LAND[rec0 + i + 2]
                                          & (uint16_t)~0x2000;
#else
                        SPR_SNAP[i + 2] = SPR_LAND[rec0 + i + 2];
#endif
                        SPR_SNAP[i + 3] = SPR_LAND[rec0 + i + 3];
                        SPR_SNAP[i + 4] = SPR_LAND[rec0 + i + 4];
                        SPR_SNAP[i + 5] = SPR_LAND[rec0 + i + 5];
                        SPR_SNAP[i + 6] = SPR_LAND[rec0 + i + 6];
                        SPR_SNAP[i + 7] = SPR_LAND[rec0 + i + 7];
                    }
                    if (nrec < 64)
                        SPR_SNAP[nrec * 8 + 2] = 0x8000;  /* belt term */
#endif  /* NATIVE_FRAME */
#endif  /* S1 strike: the pre-flip FB_SPR snapshot is authoritative;
         * the push's record payload is dead weight until v3 drops it */
                } else {
                    DRQR[7]++;               /* torn/short: stale beats it */
                    DIAG[17]++;              /* counts as incomplete */
#ifdef R60
                    /* LOOP 27 entry 7 — TEAR CENSUS (GUI-only tears, headless
                     * never enters this branch): DRQR[0..4] are free under
                     * R60. [0] landed 1..25 (header never arrived), [1]
                     * 26..923 (length/tail mismatch), [2] >= 924 (TE set or
                     * TCR stale: nothing was armed this vint), [3] last
                     * landed, [4] last tag length seen. */
                    if (landed > 0) {
                        if (landed < R60_HDR)      DRQR[0]++;
                        else if (landed < 924)     DRQR[1]++;
                        else                       DRQR[2]++;
                        DRQR[3] = landed;
                        DRQR[4] = (landed >= R60_HDR && landed < 924)
                                  ? (SPR_LAND[R60_W_TAG] & 0x3FFu) : 0u;
                    }
#endif
#ifdef NATIVE_FRAME
                    if (landed > 0)
                        nat_spr_ok = 0;      /* the tear overwrote part of
                                              * SPR_LAND: the launch must
                                              * keep last generation's
                                              * SPR_SNAP (stale beats torn) */
#endif
                    if (landed > 0) {
                        /* REAL landing tore (not a no-push vint):
                         * feedback to the 68K so it re-marks what the
                         * packet carried. Without this, ship-twice
                         * survives only ONE consecutive tear — a storm
                         * vint pair loses pal blocks until the game
                         * re-dirties them (sprite set0 stale 280+
                         * frames = Mike's load-in confetti).
                         * 2026-09-05 REVERTED TWO BELT VARIANTS, measured
                         * on Mike's rom/s16.bs1 (build 86a27bc9): (1)
                         * echoing an announced ZERO landing fires on the
                         * harvest-vs-push race every vint, not on losses;
                         * (2) pending the echo behind a busy COMM8
                         * delivered NONE of 148 echoes (68K re-marks 0)
                         * — 367 stale words, every sprite black. The
                         * direct write below consumed 243 of 254 on the
                         * JP state; it stays. */
                        MARS_SYS_COMM8 = 0xBAD1;
                        DRQR[11]++;          /* echoes raised */
                        r60_seq_prev = 0xFF; /* the torn packet's sequence is
                                              * unreadable: do not let the next
                                              * good one echo again (bs1: 43 of
                                              * 114 echoes were this duplicate) */
                    }
                    pg_pending |= r60_c10_prev;
                    cycle_dirt |= r60_c10_prev;
#ifdef PG_STICKY
                    pg_watch |= r60_c10_prev;
#endif
                }
                r60_c10_prev = MARS_SYS_COMM10 & 0x1FFF;
                k2f_spr_ok = 0;              /* the snapshot block must
                                              * NOT re-copy: SPR_SNAP is
                                              * already this frame's */
                {   /* CHAIN-PHASE CENSUS + V-FLOOR (2026-08-30, Mike's
                     * pick A): arrival = frt since window pickup at
                     * landing-done. Sum/count at 0x28FF8/28FFC (the
                     * block's exact tail; FF0/F4 retired above). The
                     * diet finishes landings early and the downstream
                     * chain moved with them — seam generation-mixing
                     * blinks 0.27 -> 1.54/1000f. PD_VFLOOR holds the
                     * chain to the measured BASELINE arrival; the
                     * 68K's freed cycles are untouched (this spends
                     * only the SH-2-side earliness the diet was never
                     * aimed at). Calibrate from the census, both arms,
                     * before trusting the default. */
                    uint16_t arr = (uint16_t)(frt() - t_vint);
                    (*(volatile uint32_t *)0x26028FF8) += arr;
                    (*(volatile uint32_t *)0x26028FFC)++;
#ifdef LAND_PARK
                    SYNC[12] = 0;        /* LANDING DONE: the slave may
                                          * compose through the rest of
                                          * the FM span (probe; the park
                                          * bound covers a missed clear) */
#endif
#if defined(PAL_DELTA) && defined(PD_VFLOOR)
                    while ((uint16_t)(frt() - t_vint) < PD_VFLOOR) ;
#endif
                }
            }
#else  /* !R60 — the K2FREE/legacy harvest families */

#ifdef K2_FREE
            int prev_k = k;               /* SAME-VINT pairing: the packet
                                           * landing now was pushed at THIS
                                           * vint into the ISR's per-k arm.
                                           * The push completes ~25 lines
                                           * in; this harvest sits after
                                           * the blit (~40+) — the magic
                                           * tail catches the rare overlap
                                           * (skip, stale beats torn). */
#elif defined(WIN_TWO)
            int prev_k = (k == 1) ? 2 : 1;   /* 2-window cycle: k1's
                                              * landed packet was pushed
                                              * after k2, and vice versa */
#else
            int prev_k = k ? k - 1 : 2;   /* no %: SH-2 has no divide */
#endif
            unsigned plen = DREQ_LEN(prev_k);
            unsigned landed = (SH2_DMA_CHCR0 & 2)
                            ? plen : (plen - (SH2_DMA_TCR0 & 0xFFFFFFu));
            if (landed > plen) landed = 0;   /* never trust a wild TCR0 */
#ifdef SNAP_ONE
            /* LOOP16 step 1+2a: the sprite list rides the packet pushed
             * after k1 (lands at k2), so the frame snapshot (regs +
             * sprites) can happen at k2 — BEFORE R0's compose — and
             * R0/R1/R2 (k2,k0,k1) all see ONE game state. The old k1
             * snapshot updated MID-frame: R2 composed with newer state
             * than R0/R1 = the band-tear class (sprite halves, 514). */
            int got_spr = (prev_k == 1);
#else
            int got_spr = (prev_k == 0);
#endif
            volatile uint16_t *pkt = SPR_LAND;
#ifdef K2_FREE
            if (!got_spr)
                pkt = SPR_LAND_K2;       /* per-k landing buffers */
#endif
            /* MAGIC-TAIL ALIGNMENT GATE (LOOP 13, savestate-proven).
             * ares drops a 68K FIFO write that races a full FIFO without
             * decrementing the armed length; the overpush dummies then
             * backfill the count, so TCR completes and the WHOLE packet
             * lands displaced -N words — `landed` cannot see it. The MD
             * pushes its two pad words as 0xA55A/0x5AA5; if they are not
             * at their exact position, every word here is suspect and
             * NOTHING is applied — stale beats displaced. The word-80
             * dirty marks in a skipped packet were already cleared
             * MD-side, so recapture ALL pages instead of losing them. */
            /* EXACT mark recovery for poisoned packets: the MD publishes
             * COMM10 from the SAME address the push harvests (0xFFB9FE),
             * in the same handler, with no writer in between — so the
             * COMM10 read at window N equals the word-80 bitmap in the
             * packet PUSHED at window N. One-window latch makes the lost
             * marks recoverable without the 0x1FFF recapture-all, whose
             * ~13-page restore burst at the first ares poison rate (8.6%
             * of packets, bs9 96f2ea21) cost real window/ack margin. */
#ifdef DRQ_PROBE
            /* LOOP 17 PROBE — WHAT DOES `landed` ACTUALLY READ?
             * 0x28FBC u32 x9, decoded by tools/drq_probe.py:
             *  [0] sprite windows      [1] sprite landed==596 (complete)
             *  [2] sprite landed==588 (SHORT PUSH READ AS PARTIAL — the
             *      answer we want)     [3] sprite landed==0
             *  [4] sprite other        [5] last sprite `other` value
             *  [6] text landed==852    [7] text landed==596 (partial,
             *      the optional-palette case)  [8] text other/0
             * If [2] tracks 1/16 of [0], partial TCR0 IS readable and
             * the truncation lands as written. If [2] is 0 and [3]
             * carries them, it is not, and the fix is exact-match
             * arming (LOOP17 next-attempt b). */
            {
                volatile uint32_t *dq = (volatile uint32_t *)0x26028FBC;
                if (got_spr) {
                    dq[0]++;
                    if (landed == 596u)      dq[1]++;
                    else if (landed == 588u) dq[2]++;
                    else if (landed == 0u)   dq[3]++;
                    else { dq[4]++; dq[5] = landed; }
                } else {
                    if (landed == 852u)      dq[6]++;
                    else if (landed == 596u) dq[7]++;
                    else                     dq[8]++;
                }
            }
#endif
            static uint16_t c10_prev;
            int aligned = 0;
#ifdef SPR_TRUNC
            /* LOOP 17 — THE SPRITE PACKET IS VARIABLE-LENGTH. The MD
             * pushes only up to and including the list terminator, so
             * its length is 84 + 8*nrec: the 82-word prefix, nrec 8-word
             * records, and the two magic-tail words. Accept that family;
             * every other check is unchanged, and the magic tail still
             * has to sit at landed-2, so a DISPLACED packet is caught
             * exactly as a full-length one was.
             * ares reports the partial landing truthfully (DRQPROBE:
             * 233 of 234 short pushes read their true length). MAME does
             * NOT model it — there every packet reads 0 and is skipped,
             * which is why this build cannot be gated in MAME. */
#define SPRSHORT_OK || (got_spr && landed >= 92u && landed <= 596u \
                        && ((landed - 84u) & 7u) == 0u)
#elif defined(DRQ_PROBE)
            /* the probe's 588 is a COMPLETE packet one record short —
             * accept it so its magic tail gets checked like any other */
#define SPRSHORT_OK || (landed == 588u && got_spr)
#else
#define SPRSHORT_OK
#endif
#ifdef WIN_TWO
#if defined(PKT_SLIM)
            /* slim era: k1 = bitmap+tag+tail (4 words exactly, the
             * junk record is gone with the prefix), k2 = bare 4 or
             * 8+32K. Nothing pushes 596 any more. */
            if ((got_spr && landed == PKT_K1_LEN)
                || (!got_spr && PKT_K2_OK(landed))) {
#elif defined(FB_TEXT_READ) && defined(PAL32)
            /* THE WHITELIST IS PART OF THE PACKET FORMAT, and the
             * format now has ONE source: packet_fmt.h, which all four
             * consumers (MD builder, published length, this check, the
             * apply below) compile from. The first PAL32 build changed
             * the push by hand and not this list — every palette-
             * carrying packet was rejected whole and ares showed
             * boot-black CRAM (S2_pal32_flick.bs9). MAME cannot catch
             * a whitelist miss: landed reads 0 there and NOTHING here
             * ever runs. */
#ifdef K2_FREE
            if ((K2F_K2_OK(landed) && !got_spr)
                SPRSHORT_OK) {
#else
            if (landed == 596
                || (PKT_K2_OK(landed) && !got_spr)
                SPRSHORT_OK) {
#endif
#elif defined(FB_TEXT_READ)
            /* text packets shrank to 340 (pal pair) / 84 (bare) once the
             * chunks moved in place; sprite arms unchanged. */
            if (landed == 596
                || ((landed == 340u || landed == 84u) && !got_spr)
                SPRSHORT_OK) {
#else
            if (landed == 596 || (landed == 852 && !got_spr) SPRSHORT_OK) {
#endif
#else
            if (landed == 596 || (landed == 340 && !got_spr) SPRSHORT_OK) {
#endif
                /* one 32-bit compare — (landed-2)*2 is /4 for both */
                if (*(volatile uint32_t *)(pkt + landed - 2)
                        == 0xA55A5AA5u)
                    aligned = 1;
                else {
                    DRQR[7]++;               /* complete-but-displaced */
                    pg_pending |= c10_prev;
                    cycle_dirt |= c10_prev;
#ifdef PG_STICKY
                    pg_watch |= c10_prev;
#endif
                }
            }
            c10_prev = MARS_SYS_COMM10 & 0x1FFF;  /* insurance for the
                                                   * packet now in flight */
#ifdef K2_FREE
            if (got_spr) {
                k2f_spr_ok = (uint8_t)aligned;
                if (aligned)
                    k2f_spr_landed = (uint16_t)landed;
            }
#endif
            if (aligned) {
#if !defined(FB_TEXT_READ) || defined(K2_FREE)
                /* prefix regs/rowscroll: SOURCED FROM 0xFF8000 on the MD.
                 * Under plain FBTEXT the game stopped writing that mirror
                 * and this apply was compiled out (boot-stale words would
                 * clobber the capture). K2FREE turns it back ON: the
                 * patch_game split routes the game's vint-context reg/
                 * rowscroll writes (text >= 0xE80) to the mirror again,
                 * the packet prefix carries them live, and the ISR's
                 * capture was trimmed to glyphs so THIS is the only
                 * writer of TEXT_U's reg words. Both k packets carry the
                 * prefix: regs land at vint rate. */
#ifdef K2_FREE
                if (got_spr)     /* the slim k2 packet has no prefix */
#endif
                {
                    volatile uint32_t *d = (volatile uint32_t *)(TEXT_U + 0x740);
                    volatile uint32_t *s = (volatile uint32_t *)(pkt + 0);
                    for (int i = 0; i < 10; i++)
                        d[i] = s[i];
                    d = (volatile uint32_t *)(TEXT_U + 0x7C0);
                    s = (volatile uint32_t *)(pkt + 20);
                    for (int i = 0; i < 30; i++)
                        d[i] = s[i];
                }
#endif
                /* (No cache purge here. LOOP 7a negative 8 proved these
                 * lines are ALREADY cold when latch_layer_regs reads them —
                 * the post-ack full cache_purge() runs every window — and
                 * the targeted purge was bit-identical either way. It was
                 * dead code kept out of caution; .ramtext is scarcer.) */
                /* Shared prefix: bitmap at 80, text base at 81, in BOTH
                 * layouts. The bitmap is applied every aligned window — a
                 * misaligned packet recaptures all pages instead (above). */
#ifdef K2_FREE
                {
                    unsigned pw_bm = got_spr ? PKT_W_BM : K2F_W_BM;
                    pg_pending |= pkt[pw_bm];
                    cycle_dirt |= pkt[pw_bm];
#ifdef PG_STICKY
                    pg_watch |= pkt[pw_bm] & 0x1FFF;
                    if (__builtin_popcount(pkt[pw_bm] & 0x1FFF) >= 8)
                        pg_deep = 24;
#endif
                }
#else
                pg_pending |= pkt[PKT_W_BM];
                cycle_dirt |= pkt[PKT_W_BM];  /* belt for the COMM10 live
                                              * word (provably a superset,
                                              * but a missed stale page
                                              * breaks game logic — cheap
                                              * insurance) */
#ifdef PG_STICKY
                pg_watch |= pkt[PKT_W_BM] & 0x1FFF;
                if (__builtin_popcount(pkt[PKT_W_BM] & 0x1FFF) >= 8)
                    pg_deep = 24;
#endif
#endif  /* !K2_FREE bitmap merge */
                if (!got_spr) {
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
#ifdef K2_FREE
                unsigned w81 = pkt[K2F_W_TAG];
#else
                unsigned w81 = pkt[PKT_W_TAG];
#endif
                unsigned tb = w81 & 0x7FF, src = 82, need = 338;
                /* LAYOUT COMES FROM THE TAG, COMPLETENESS FROM `landed`.
                 * The palette region is laid out FIRST, so a truncated
                 * push that dropped the text still has palette words at
                 * 82 — reading the layout off `landed` instead would copy
                 * them into text RAM. */
#ifdef PAL32
                /* LOOP 22 — dirty 32-word BLOCKS: tag bits 13-11 =
                 * count, ids at words 82..85, payload from 86. Layout
                 * from the tag, completeness from `landed`, exactly as
                 * the pair channel below (which this replaces): ids
                 * ride AHEAD of the payload so truncation can only
                 * drop data, never misroute it. A block is 4 tile sets
                 * (8 words) below word 1024 or 2 sprite sets (16
                 * words) above; the per-set change-detect, SETGEN bump
                 * and the k!=1 sprite-only paint rule carry over
                 * unchanged. */
                if (w81 & 0x8000) {
                    unsigned K = (w81 >> PKT_PAL_KSHIFT) & 7;
#ifdef K2_FREE
                    if (K > K2F_PAL_KMAX)
                        K = K2F_PAL_KMAX;
#else
                    if (K > PKT_PAL_KMAX)
                        K = PKT_PAL_KMAX;
#endif
                    for (unsigned j = 0; j < K; j++) {
#ifdef K2_FREE
                        unsigned b = pkt[K2F_PAL_ID0 + j];
#else
                        unsigned b = pkt[PKT_PAL_ID0 + j];
#endif
                        volatile uint32_t *d, *s;
                        unsigned s0, ns, wps;
                        if (b > 63)
                            continue;
#ifdef GLOW_ANIM
                        /* the 68K spoke about a glow block (fade
                         * storm or heal raw-ship): its truth stands,
                         * the animator yields and must re-seed */
                        if (b == 4 || b == 5) {
                            glow_pause = 8;
                            glow_on = 0;
                            glow_post = 3;   /* grant the mask OFF */
                        }
#endif
                        d = (volatile uint32_t *)(PAL_SH + b * PKT_PAL_BLK);
#ifdef K2_FREE
                        s = (volatile uint32_t *)(pkt + K2F_PAL_PAY0
                                                  + j * PKT_PAL_BLK);
#else
                        s = (volatile uint32_t *)(pkt + PKT_PAL_PAY0
                                                  + j * PKT_PAL_BLK);
#endif
                        if (b < 32) { s0 = b << 2;                ns = 4; wps = 4; }
                        else        { s0 = 128 + ((b - 32) << 1); ns = 2; wps = 8; }
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
                            if (k != 1 && b >= 32)
                                cram_paint_spr(par, s0 + t - 128);
                        }
                    }
                }
#else
                if (w81 & 0x8000) {
                    unsigned r = ((w81 >> 11) & 7) << 1;   /* pair -> region */
                    volatile uint32_t *d =
                        (volatile uint32_t *)(PAL_SH + (r << 7));
                    volatile uint32_t *s =
                        (volatile uint32_t *)(pkt + 82);
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
#endif /* PAL32 */
#ifndef FB_TEXT_READ
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
#ifdef WIN_TWO
                /* SECOND text chunk (v8 rebalance: the k2-tail packet
                 * carries both of the cycle's chunks; the MD advanced
                 * its rotation twice, so chunk 2 targets tb+256 with
                 * the same 2048 wrap). */
                {
                    unsigned tb2 = (tb + 256) & 0x7FF;
                    unsigned src2 = src + 256, need2 = need + 256;
                    if (landed >= need2 && tb2 + 256 <= 2048) {
                        volatile uint32_t *d2 =
                            (volatile uint32_t *)(TEXT_U + tb2);
                        volatile uint32_t *s2 =
                            (volatile uint32_t *)(SPR_LAND + src2);
                        for (int i = 0; i < 128; i += 4) {
                            d2[i + 0] = s2[i + 0];
                            d2[i + 1] = s2[i + 1];
                            d2[i + 2] = s2[i + 2];
                            d2[i + 3] = s2[i + 3];
                        }
                    }
                }
#endif
#endif /* !FB_TEXT_READ: both text chunks skipped (in-place capture) */
                }
            }
#endif  /* !R60 harvest */
#ifdef SNAP_ONE
            if (k == 2) {                    /* frame snapshot at R0 launch */
#else
            if (k == 1) {
#endif
#ifndef NATIVE_FRAME
                latch_layer_regs();          /* scanline-261-style reg latch */
                #ifdef HS_SHIP
                #endif
#else
                ;                            /* NATIVE latches regs at the
                                              * generation launch, never
                                              * mid-chain — one game state
                                              * per displayed frame */
#endif
                /* sprite-list snapshot — DREQ FIFO DMA (the FB staging
                 * copy is retired: the game's vint upload crossed the
                 * FB window exactly when the SH-2 owned the FB, and
                 * ares/hardware DISCARD those MD writes — savestate-
                 * proven 40/64 torn records. The MD shim now walks the
                 * game's order table and pushes the ordered list over
                 * the DREQ FIFO; DMAC0 lands it at SPR_LAND. TE set =
                 * a complete coherent list; TE clear = keep last
                 * frame's list (stale beats torn). */
#ifdef FB_SPR_READ
                /* LOOP 20 STEP 2 — READ THE LIST IN PLACE. Under
                 * FBSPR=1 the game's 0x2B1E upload writes its ordered
                 * list straight into FB staging (patch_game remap
                 * 0x85E000), so FB_SPR holds the same ordered records
                 * the DREQ push used to carry. This copy runs in-window
                 * (FM held), so the FB read is legal, and the window
                 * maps the DRAW bank — the same bank the game wrote
                 * during the gap, because both sides' FB windows track
                 * FS together. The game rewrites the full list plus
                 * terminator every vint, so no restore machinery is
                 * needed: by this k1 the current bank has received at
                 * least one complete upload since the k2 flip.
                 * The DREQ push still runs and still lands; its sprite
                 * payload is simply not read. That is deliberate — this
                 * build gates CORRECTNESS of the in-place read with
                 * everything else unchanged. Dropping the push (the
                 * actual 48-line prize) is step 3, after this is
                 * pixel-clean in MAME and on ares.
                 * The write-discard hazard that retired the original
                 * FB-staging path is extinct: FM has one raise site and
                 * the 68K spins inside the whole FM=1 span. */
                {
#ifdef FLICK_FUSE
                    unsigned zseen = 0;
#endif
                    for (int i = 0; i < 512; i += 8) {
                        uint16_t v2, v5;
                        SPR_SNAP[i + 0] = FB_SPR[i + 0];
                        SPR_SNAP[i + 1] = FB_SPR[i + 1];
                        v2 = FB_SPR[i + 2];
                        SPR_SNAP[i + 2] = v2;
                        SPR_SNAP[i + 3] = FB_SPR[i + 3];
                        SPR_SNAP[i + 4] = FB_SPR[i + 4];
                        v5 = FB_SPR[i + 5];
                        SPR_SNAP[i + 5] = v5;
                        SPR_SNAP[i + 6] = FB_SPR[i + 6];
                        SPR_SNAP[i + 7] = FB_SPR[i + 7];
                        /* stop past the terminator: the tail of the 2KB
                         * region is whatever the game last left there */
                        if (v2 & 0x8000)
                            break;
#ifdef FLICK_FUSE
                        zseen |= v5 & 0x3FF;
#else
                        (void)v5;
#endif
                    }
#ifdef FLICK_FUSE
                    flick_update(zseen);
#endif
                }
                if (0) {                    /* DREQ sprite payload unused */
#elif defined(K2_FREE)
                if (k2f_spr_ok) {           /* k1 packet validated at ITS
                                             * window (one vint ago);
                                             * records stable — the k2
                                             * push landed in its own
                                             * buffer */
#else
                if (got_spr && aligned) {   /* whole list arrived, aligned */
#endif
#if defined(SPR_TRUNC) || defined(DRQ_PROBE)
                    /* copy only what LANDED. Records past it keep the
                     * previous frame's words and are never read: the
                     * terminator that ended the push is inside this
                     * copy, and compose_sprites breaks on it. */
#ifdef K2_FREE
                    const int nw = (int)(k2f_spr_landed - 84u);
#else
                    const int nw = (int)(landed - 84u);
#endif
#else
                    const int nw = 512;
#endif
#ifdef FBSPR_PROBE
                    {
                        int any = 0;
                        for (int r = 0; r * 8 < (int)nw; r++) {
                            const volatile uint16_t *lr =
                                SPR_LAND + 82 + r * 8;
                            if (lr[2] & 0x8000)
                                break;           /* list terminator */
                            FBP[0]++;
                            int hit = 0;
                            for (int q = 0; q < 64 && !hit; q++) {
                                const volatile uint16_t *fr = FB_SPR + q * 8;
                                hit = 1;
                                for (int w = 0; w < 8; w++)
                                    if (fr[w] != lr[w]) { hit = 0; break; }
                            }
                            if (!hit) { FBP[1]++; any = 1; }
                        }
                        FBP[2]++;
                        if (any) FBP[3]++;
                    }
#endif
#ifdef FLICK_FUSE
                    unsigned zseen = 0;
#endif
                    for (int i = 0; i < nw; i += 8) {
                        SPR_SNAP[i + 0] = SPR_LAND[82 + i + 0];
                        SPR_SNAP[i + 1] = SPR_LAND[82 + i + 1];
                        SPR_SNAP[i + 2] = SPR_LAND[82 + i + 2];
                        SPR_SNAP[i + 3] = SPR_LAND[82 + i + 3];
                        SPR_SNAP[i + 4] = SPR_LAND[82 + i + 4];
                        SPR_SNAP[i + 5] = SPR_LAND[82 + i + 5];
                        SPR_SNAP[i + 6] = SPR_LAND[82 + i + 6];
                        SPR_SNAP[i + 7] = SPR_LAND[82 + i + 7];
#ifdef FLICK_FUSE
                        /* same zseen the FB_SPR path gathered: zoomed-
                         * record pens, terminator-bounded (words past
                         * the terminator are stale) */
                        if (!(SPR_LAND[82 + i + 2] & 0x8000))
                            zseen |= SPR_LAND[82 + i + 5] & 0x3FF;
#endif
                    }
#ifdef FLICK_FUSE
                    flick_update(zseen);
#endif
#ifdef SPR_REUSE
                    /* SPRITE-FRAME REUSE PROBE (LOOP16 motion-parity
                     * lane; NEVER SHIP — overlays ROWHASH 0x28D00 like
                     * the other probe families). Once per cycle, walk
                     * the fresh snapshot and ask: how many sprites are
                     * IDENTICAL decode jobs to last cycle (same data
                     * addr, geometry, zoom words)? That repeat rate is
                     * the pre-decoded frame cache's hit ceiling, and
                     * the cost split sizes its win.
                     *   0x28D00 u32 prev[48], 0x28DC0 u32 cur[48],
                     *   0x28E80 u16 prev_n; counters at 0x28FAC:
                     *   [0] sprites, [1] repeats, [2] cost units
                     *   (rows*|pitch|), [3] repeated cost units. */
                    {
                        volatile uint32_t *pv =
                            (volatile uint32_t *)0x26028D00;
                        volatile uint32_t *cu =
                            (volatile uint32_t *)0x26028DC0;
                        volatile uint16_t *pn =
                            (volatile uint16_t *)0x26028E80;
                        volatile uint32_t *ct =
                            (volatile uint32_t *)0x26028FAC;
                        int cn = 0;
                        for (int si = 0; si < 64 && cn < 48; si++) {
                            volatile uint16_t *e2 = SPR_SNAP + si * 8;
                            uint16_t sd2 = e2[2];
                            if (sd2 & 0x8000) break;
                            uint16_t sd0 = e2[0];
                            int tp = sd0 & 0xFF, bt = sd0 >> 8;
                            if ((sd2 & 0x4000) || tp >= bt) continue;
                            int pw = (int8_t)(sd2 & 0xFF);
                            if (pw < 0) pw = -pw;
                            uint32_t cost = (uint32_t)(bt - tp) * (pw ? pw : 1);
                            uint32_t key = ((uint32_t)e2[3] << 16)
                                ^ ((uint32_t)sd2 << 4)
                                ^ ((uint32_t)(e2[4] & 0xFFF) << 12)
                                ^ e2[6];
                            ct[0]++; ct[2] += cost;
                            for (int pi = 0; pi < *pn; pi++)
                                if (pv[pi] == key) {
                                    ct[1]++; ct[3] += cost;
                                    break;
                                }
                            cu[cn++] = key;
                        }
                        for (int pi = 0; pi < cn; pi++)
                            pv[pi] = cu[pi];
                        *pn = (uint16_t)cn;
                    }
#endif
                }
#ifndef R60  /* R60 harvest does its own incomplete accounting */
                /* LOOP 17: under SPR_TRUNC a SHORT packet is the normal
                 * case, so `landed < plen` alone is not a failure. What
                 * still means truncation is short AND not validly
                 * terminated — `aligned` says the magic tail sat at
                 * landed-2, and the tail is the last thing pushed, so
                 * its presence IS completeness. */
#ifdef SPR_TRUNC
                if (landed < plen && !aligned) {
#else
                if (landed < plen) {
#endif
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
#ifdef SNAP_ONE
            }
#endif  /* !R60 */
            if (k == 1) {                    /* k1 housekeeping unchanged */
#endif
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
                    if (disp_blank) budget = 13;         /* DISPLAY GATE: load, nobody watches */
#ifdef PG_ROTOR
                    /* Background truth re-verify: cap_page is compare-
                     * and-copy, so a clean page costs one read pass and
                     * writes nothing new. Bounds ANY missed stream mark
                     * (the MDBGALL eyehold staleness class) to one
                     * rotor period instead of forever. */
                    {
                        static uint8_t pg_rot;
                        pg_pending |= (uint16_t)(1u << pg_rot);
                        if (++pg_rot >= 13) pg_rot = 0;
                        pg_pending |= (uint16_t)(1u << pg_rot);
                        if (++pg_rot >= 13) pg_rot = 0;
                    }
                    if (budget < 5) budget = 5;
#endif
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
                CLAIM_DONE = 0;
#ifdef SPR_LATE
                /* LOOP 19 — LATE CLAIM. spr_pair[] is rebuilt only by
                 * build_maps, and its ONLY caller passes `b->bpar ^ 1`:
                 * the map for THIS parity was built one cycle ago, from
                 * the PREVIOUS cycle's SPR_SNAP. A sprite whose colour
                 * set first appears now is therefore unmapped, reads
                 * 0xFF at draw time, and renders with base 15 — the
                 * SHADOW RAMP. One cycle is three vints, which is
                 * exactly the 1-3 frame colour flashes on the
                 * gravestones, the wolf and the enemies.
                 * build_maps cannot simply move here: ~4ms and
                 * uninterruptible by its own comment. But the expensive
                 * part is the tile/group planning, not the sprite pairs
                 * — so claim ONLY the sets the pre-built map missed,
                 * from pairs nothing owns. One 64-record scan.
                 * Placed after `par ^= 1` and before apply_cram, which
                 * paints straight out of spr_pair[par], so a pair
                 * claimed here is painted in this same window.
                 * A pair is free only if no set owns it (pr_key) AND
                 * no tile group owns either of its halves (grp_key) —
                 * the same test build_maps uses. If none is free we
                 * leave the set unmapped and it flashes as before:
                 * strictly no worse than today. */
#ifdef TILE_CLASS
                /* LIVE-SET MASK (2026-08-25): with the static tile
                 * classes guaranteeing pair supply, the age>=3 steal
                 * caution below is the last thing keeping the late
                 * claim at ~30 wins per 800 misses. A pair whose owner
                 * is NOT in the CURRENT snapshot can be stolen NOW:
                 * this frame's compose will not draw the owner, so the
                 * only cost is a possible one-frame recolour of a
                 * DEPARTING sprite on the still-displayed bank — paid
                 * to buy correct colours for one ARRIVING, which is
                 * strictly the better artifact. First pass: who is
                 * live right now. */
                uint32_t tc_live[2] = { 0, 0 };
                uint32_t tc_live32x[2] = { 0, 0 };   /* census: live via a
                                                      * record the 32X draws */
                unsigned nl_live = 0;                /* live sets (LATE_STEAL0) */
                for (int i = 0; i < 64; i++) {
                    volatile uint16_t *sd = CLAIM_SRC + i * 8;
                    uint16_t sd2 = sd[2];
                    if (sd2 & 0x8000)
                        break;
                    uint16_t sd0 = sd[0];
                    if ((sd2 & 0x4000) || (sd0 & 0xFF) >= (sd0 >> 8))
                        continue;
                    unsigned sc = sd[4] & 0x3F;
#if defined(MD_SPR) && defined(SPR_MD_FREE)
                    if (sd2 & 0x2000)
                        continue;            /* SPRMDFREE: MD-drawn, not live here */
#endif
                    tc_live[sc >> 5] |= 1u << (sc & 31);
                    if (!(sd2 & 0x2000))
                        tc_live32x[sc >> 5] |= 1u << (sc & 31);
                }
#ifdef SPR_LATE
                /* LOOP 27 q4 demand census per late-claim scan: [5] max
                 * live 32X-drawn sets in one snapshot, [6] their sum,
                 * [7] scans (mean = [6]/[7]); MD-claimed records ride
                 * QCLAIM_N's neighbour below. */
                {
                    unsigned nl = (unsigned)(__builtin_popcount(tc_live32x[0])
                                             + __builtin_popcount(tc_live32x[1]));
                    nl_live = (unsigned)(__builtin_popcount(tc_live[0])
                                         + __builtin_popcount(tc_live[1]));
                    unsigned nmd = 0;
                    for (int i = 0; i < 64; i++) {
                        volatile uint16_t *sd = CLAIM_SRC + i * 8;
                        uint16_t sd2 = sd[2];
                        if (sd2 & 0x8000) break;
                        if (sd2 & 0x2000) nmd++;
                    }
                    SPRLATE[7]++;
                    SPRLATE[9] += nmd;                 /* MD-claimed records, sum */
                }
#endif
#endif
                for (int i = 0; i < 64; i++) {
                    volatile uint16_t *sd = CLAIM_SRC + i * 8;
                    uint16_t sd2 = sd[2];
                    if (sd2 & 0x8000)
                        break;               /* list terminator */
                    uint16_t sd0 = sd[0];
                    if ((sd2 & 0x4000) || (sd0 & 0xFF) >= (sd0 >> 8))
                        continue;            /* hidden / zero height */
                    unsigned sc = sd[4] & 0x3F;
                    if (sc == 0x3F)
                        continue;            /* shadows own reserved 15 */
#if defined(MD_SPR) && defined(SPR_MD_FREE)
                    if (sd2 & 0x2000)
                        continue;            /* SPRMDFREE: MD-drawn, needs no pair */
#endif
                    if (spr_pair[par][sc] != 0xFF)
                        continue;            /* already mapped */
                    SPRLATE[0]++;            /* sets the map missed */
                    /* OWNER MATCH FIRST (2026-08-26, the black-Zeus
                     * blob): the OTHER parity's build_maps may have
                     * already given this set a pair — pr_key is shared
                     * across parities but spr_pair is per-parity, and
                     * a parity whose R2 terminator keeps dropping
                     * (load-in) never rebuilds its map. Without this
                     * check the rescue hunts free pairs, finds none
                     * (the set's own pair reads as taken), and the set
                     * ramps BLACK on alternating cycles for as long as
                     * the load lasts — measured 120+ frames of solid-
                     * black Zeus at frame 830, par0=0xFF par1=0x07.
                     * The pair's CRAM already holds this set: adopt,
                     * no paint needed. */
                    {
                        int qo = 0;
                        for (int t = 15; t >= 1; t--)
                            if (pr_key[t] == (uint8_t)sc) { qo = t; break; }
                        if (qo) {
                            spr_pair[par][sc] = (uint8_t)qo;
                            pr_age[qo] = 0;
                            SPRLATE[2]++;    /* claimed in time */
                            continue;
                        }
                    }
                    int q = 14;                  /* 14: ramp reserved */
                    for (; q >= 1; q--)
                        if (pr_key[q] == 0xFF && grp_key[2 * q] == 0xFF
                            && grp_key[2 * q + 1] == 0xFF)
                            break;
                    if (q < 1) {
                        /* MEASURED: a genuinely free pair almost never
                         * exists — 623 of 633 missed sets found none.
                         * pr_key holds a pair for its owner until
                         * pr_age > 90, so the free list is empty most of
                         * the time and "15 pairs vs 11 live sets" was
                         * the wrong capacity model.
                         * build_maps already solves this: it STEALS the
                         * pair whose owner has been absent longest,
                         * requiring age >= 3 so a set merely missing
                         * from THIS scan cannot have its still-displayed
                         * pixels recoloured. Use the same rule and the
                         * same guard — anything build_maps considers
                         * safe to steal is safe here. */
                        uint8_t best = 2;
                        for (int t = 14; t >= 1; t--)   /* 14: ramp reserved */
                            if (pr_key[t] != 0xFF && pr_age[t] > best
                                && grp_key[2 * t] == 0xFF
                                && grp_key[2 * t + 1] == 0xFF) {
                                best = pr_age[t]; q = t;
                            }
#ifdef TILE_CLASS
                        if (q < 1) {
                            /* NOT-LIVE STEAL (see tc_live above): any
                             * pair whose owner is absent from THIS
                             * snapshot is takeable regardless of age —
                             * the owner will not be composed this
                             * frame. Oldest absent owner first. */
                            /* age >= 1: absent for at least one whole
                             * build_maps cycle, not merely this
                             * instant — the arcade BLINKS actors with
                             * the lightning strobe (Zeus), and robbing
                             * a blinker's pair on its off-frame ping-
                             * pongs CRAM, pins shadow_dirty, and drew
                             * his halo as a black silhouette (measured:
                             * ba>=0 arm, s0750). */
                            uint8_t ba = 1;
#ifdef LATE_STEAL0
                            /* LOOP 27 q4 option 1 (demand-aware late
                             * steal): at one cycle per vint every new
                             * set arrives through this path, and the
                             * capacity census at each failure found the
                             * only slack to be a pair whose owner left
                             * THIS snapshot (age 0). build_maps releases
                             * such pairs under demand (PAIR_HOLD: hold 2
                             * once >= 8 sets want pairs); mirror that
                             * here: with >= 9 live sets, age 0 is
                             * stealable. Cost: a possible one-frame
                             * recolour of a DEPARTING actor's still-
                             * displayed bank, the trade the age>=1 rule
                             * already makes for arriving ones. */
                            if (nl_live >= 9)
                                ba = 0;
#endif
                            for (int t = 14; t >= 1; t--) {  /* 14: ramp */
                                uint8_t ow = pr_key[t];
                                if (ow == 0xFF
                                    || grp_key[2 * t] != 0xFF
                                    || grp_key[2 * t + 1] != 0xFF)
                                    continue;
                                if (tc_live[ow >> 5] & (1u << (ow & 31)))
                                    continue;
                                if (pr_age[t] >= ba) {
                                    ba = pr_age[t]; q = t;
                                }
                            }
                        }
#endif
                        if (q < 1) {
                            SPRLATE[1]++;    /* nothing stealable either */
                            /* LOOP 27 q4 CAPACITY CENSUS at the failure:
                             * where did the 14 pairs go? [7] owner live in
                             * this snapshot, [8] a half owned by a tile
                             * group, [9] held by an absent owner too young
                             * to steal. Sum over failures = 14 x [1]. */
                            for (int t = 14; t >= 1; t--) {
                                uint8_t ow = pr_key[t];
                                if (grp_key[2 * t] != 0xFF || grp_key[2 * t + 1] != 0xFF)
                                    SPRLATE[8]++;
                                else if (ow != 0xFF && (tc_live[ow >> 5] & (1u << (ow & 31))))
                                    ;               /* live owner (was [7]; [6]/[7] now time the ramp draws) */
                                else
                                    ;   /* young absent owner (retired count) */
                            }
                            continue;
                        }
                    }
                    pr_key[q] = (uint8_t)sc;
                    pr_age[q] = 0;
                    spr_pair[par][sc] = (uint8_t)q;
                    SPRLATE[2]++;            /* claimed in time */
                }
#endif
                CLAIM_DONE = 1;
                BMT_AT_CLAIM[par & 1] = BMT_DONE[par & 1];
#ifdef LAUNCH_EARLY
                nat_window_launch(par, bank1, t_vint, win_no, &tile_cmd, &pend_wait);
#endif
#ifdef BLIT_CHASE
                if (nat_ship_now) {      /* master's half, after the launch */
                    uint16_t tb2 = frt();
#ifdef WAIT_PROBE
                    RG_COUNT[11] += (uint16_t)(tb2 - rg_tpost);   /* post -> master half start */
#endif
                    BLIT_HALF(112 + BLIT_SHIFT, 224, fb_draw_par);
#ifdef WAIT_PROBE
                    RG_COUNT[9] += (uint16_t)(frt() - tb2);       /* master half */
#endif
                    SYNC[14] = 224;      /* fence open */
                    guard = 2000000;
                    while (SYNC[2] < 1 && --guard) ;   /* slave picked up */
                    if (!guard) DIAG[21]++;
                    DIAG[28]++;
                    guard = 2000000;
#ifdef WAIT_PROBE
                    { uint16_t tw = frt();
#endif
                    while (SYNC[5] != scmd && --guard) ;  /* slave half done */
                WSTAGE(0x7FE0);                      /* CYAN: master blit + slave half done */
#ifdef WAIT_PROBE
                    RG_COUNT[10] += (uint16_t)(frt() - tw); RG_COUNT[12]++; }
#endif
                    if (!guard) DIAG[22]++;
                    SYNC[4] = 0;
                    nat_gen_ready = 0;
#ifdef PHASE_CENSUS
                    nat_ph_ship(t_vint);
#endif
#ifdef HS_SHIP
                    hs_promote();
#endif
                    nat_shipped = 1;
                    nat_ship_now = 0;
                    diag_add(5, tb2);
                }
#endif
                tp = frt();
                apply_cram(par);
                WSTAGE(0x7C1F);                      /* MAGENTA: palette painted */
                diag_add(2, tp);
            }
#ifndef NT_WRAP
            /* SKIP-RATE BARS: excluded under NT_WRAP for the region
             * guard (~150 bytes) and because the wrap flavor sets its
             * own parity baseline — these debug pixels (and the CRAM
             * 255 hijack) are OURS-ONLY content inside the capture
             * area. When NT_WRAP becomes the bundle, whether shipping
             * drops them too (statics would MOVE, downward) is Mike's
             * gate-rebaseline call, not a default. */
            else if (k == 2) {
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
                    uint8_t *b = DROW(8 + 2 + 2 * j);
                    RL_MARK(8 + 2 + 2 * j);
                    for (int i = 0; i < len; i++)
                        b[i] = 0xFF;
                }
            }
#endif  /* !NT_WRAP (skip-rate bars) */
            /* (No k0 capture: an early-capture spread was tried and it
             * reintroduced the mid-stream-capture poison above. k1's
             * settled budget + the k2 drain carry the load.) */

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
#ifndef K2_FREE
            WSTAGE(0x01FF);                      /* ORANGE: at the DREQ re-arm, before the ack path */
            dreq_rearm(k);
#endif  /* K2FREE: the V-ISR armed at vblank, BEFORE the 68K's push —
         * a body rearm here would reset TCR under a completed landing
         * and erase `landed` for this vint's harvest. */
#ifdef R60
            if (k == 2)
                DIAG[9]++;               /* one frame per vint: cycles
                                          * == frames, cadence -> 1.0 */
#else
            if (k == 1)
                DIAG[9]++;
#endif
#ifdef MD_BG
            /* PUBLISH the packet PREPARED IN THE GAP (see the post-ack
             * block at the bottom of the window): the in-window cost is
             * one 1.5KB SDRAM->FB copy (~0.2 lines) instead of the full
             * walk/allocator/shipper. That work inside FM=1 was the
             * 209-line window/ack spans on ares -- the 68K lost ~40% of
             * its frame vs the shipping build and the game played SLOW.
             * Magic long is copied LAST so a partial copy never
             * presents as a valid packet. */
#ifdef K2_FREE
            /* LOOP24 lossless publish: BOTH packets go out HERE, at the
             * k2 window, POST-FLIP — no flip sits between this write and
             * the k1-entry consume, so the consume reads the SAME bank
             * (the old k1-publish of A crossed a flip and was read one
             * generation stale every cycle). Magic handshake: the 68K
             * zeroes word 0 after consuming; a still-set magic means
             * UNCONSUMED — defer (pend stays, the builder holds off) and
             * count. Nothing is ever lost, only late. */
            if (k == 2) {
                disp_gate();                 /* DISPLAY GATE, ROM-resident:
                                              * decided before the publish so
                                              * the packets carry this vint's
                                              * hold state to the 68K */
                WSTAGE(0x03FF);                      /* YELLOW: display gate done */
                volatile uint32_t *d = (volatile uint32_t *)0x24011A00u;
                const uint32_t *ssrc = (const uint32_t *)md_pktA;
#ifdef HS_SHIP
                if (k2f_pendA && 1) hs_patch(md_pktA, 1, d);
#endif
                if (k2f_pendA) {
                    if ((d[0] >> 16) == 0xB6B6u) {
                        DIAG[42]++;          /* A unconsumed: defer */
                    } else {
                        for (int i2 = 1; i2 < 368; i2++)
                            d[i2] = ssrc[i2];
                        d[0] = ssrc[0] | (disp_blank ? 0x2000u : 0u);   /* bit 13: SH-2 holding blank */
                        k2f_pendA = 0;
                    }
                }
#ifdef PG_SKIP_PKT
                /* LOOP29 149: keep the FB bytes exactly (hs_patch edits
                 * the FB copy in place, so the staging is not it) for the
                 * mirror to replay into the other bank after a flip. */
                for (int i2 = 0; i2 < 368; i2++) tp_lastA[i2] = d[i2];
#endif
                WSTAGE(0x7FE0);                      /* CYAN: plane packet A published */
                d = (volatile uint32_t *)0x2401E800u;
                ssrc = (const uint32_t *)md_pkt;
#ifdef HS_SHIP
                if (k2f_pendB && 1) hs_patch(md_pkt, 0, d);
#endif
                WSTAGE(0x0200);                      /* DARK GREEN: B hs_patch done */
                if (k2f_pendB) {
                    DIAG[39]++;
                    if ((d[0] >> 16) == 0xB6B6u) {
                        DIAG[42]++;          /* B unconsumed: defer */
                    } else {
                        for (int i2 = 1; i2 < 368; i2++)
                            d[i2] = ssrc[i2];
                        d[0] = ssrc[0] | (disp_blank ? 0x2000u : 0u);   /* bit 13: SH-2 holding blank */
                        k2f_pendB = 0;
                    }
                }
#ifdef PG_SKIP_PKT
                for (int i2 = 0; i2 < 368; i2++) tp_lastB[i2] = d[i2];
#endif
                WSTAGE(0x7C0F);                      /* PURPLE: B copy done */
#ifdef HS_SHIP
                if (HS_OFFS[3] && !HS_OFFS[6]) hs_stub();   /* shipped, nothing copied */
#endif
                WSTAGE(0x4210);                      /* DARK GREY: hs_stub done; MDSPR publish next */
            }
#else
            {
#ifdef FM_GATE
                /* v8 double buffer: k1 -> A (0x11A00), k2 -> B (0x1E800,
                 * the free 2KB FB hole). The 68K consumes BOTH in the k2
                 * tail, post-flip — nothing overwrites an unconsumed
                 * packet and no consume sits on a raise deadline. */
                volatile uint32_t *d = (volatile uint32_t *)
                    ((k == 1) ? 0x24011A00u : 0x2401E800u);
#else
                volatile uint32_t *d = (volatile uint32_t *)0x24011A00;
#endif
                const uint32_t *ssrc = (const uint32_t *)md_pkt;
#ifdef HS_SHIP
                if (1) hs_patch(md_pkt, 0, d);
#endif
                for (int i2 = 1; i2 < 368; i2++)
                    d[i2] = ssrc[i2];
                d[0] = ssrc[0];
            }
#endif
#endif
#ifdef MD_SPR
            /* P3: publish the SAT + palette blocks into the FB hole
             * EVERY window (both banks end up holding a copy at most
             * one window old — bank-choreography-proof, same doctrine
             * as md_pkt's double buffer). 536 bytes, ~0.13 lines. */
            {
                const uint32_t *ss2 = (const uint32_t *)MDSPR_PAL;
                volatile uint32_t *dd = (volatile uint32_t *)0x2401EDC0u;
                for (int i2 = 0; i2 < 8; i2++)
                    dd[i2] = ss2[i2];
                ss2 = (const uint32_t *)MDSPR_SAT;
                dd = (volatile uint32_t *)0x2401EE00u;
                for (int i2 = 0; i2 < 64; i2++)
                    dd[i2] = ss2[i2];
#ifdef PG_SKIP_PKT
                /* LOOP29 149: the 68K DMAs these after the flip under
                 * TWO_POST, from the other bank; keep them for the mirror */
                {
                    const volatile uint32_t *rp = (const volatile uint32_t *)0x2401EDC0u;
                    for (int i2 = 0; i2 < 8; i2++) tp_lastPal[i2] = rp[i2];
                    for (int i2 = 0; i2 < 64; i2++) tp_lastSat[i2] = dd[i2];
                }
#endif
            }
#endif
#ifdef BOOT_PALTEST
            /* CRAM RESIDUE TEST (2026-09-08, LOOP27 25). s16_palpen5
             * shows the SAME magenta sky / green stones as the abdraw
             * probe — but palpen5 contains NO CRAM 1/2 writes. Either
             * the core keeps 32X CRAM across rom loads and we are
             * looking at RESIDUE from the abdraw runs (in which case
             * nothing of ours lands, and the palette diagnosis is
             * strengthened), or those colours have a source I have not
             * identified (in which case entry 18 is wrong).
             * DIFFERENT COLOURS decide it: hammer CRAM 1 = YELLOW and
             * CRAM 2 = RED every window, direct stores that bypass
             * cram_set and PALPEN entirely, exactly as abdraw did.
             *   yellow sky / red stones -> our hammered writes DO land;
             *     the magenta was residue and CRAM is otherwise empty.
             *   still magenta / green    -> nothing of ours reaches CRAM
             *     at all, not even hammered, and the abdraw picture was
             *     never ours either. */
            ((volatile uint16_t *)0x20004200)[1] = 0x03FF;   /* yellow */
            ((volatile uint16_t *)0x20004200)[2] = 0x001F;   /* red    */
#endif
#ifdef BOOT_FBXFER
            /* THE FB TRANSPORT, READ BACK (2026-09-08, HANDOFF-DREQ job 1).
             * The 68K wrote a 13-word test packet into the FB twice this
             * vint: A at 0x12000 before the post (FM=0, pre-flip), B at
             * 0x12040 inside r60_push (FM=1, post-flip). Both carry a
             * per-vint sequence in word[0].
             * FRESHNESS IS THE WHOLE POINT and it needs no shared clock:
             * remember the sequence seen last window; a region whose
             * sequence advances by exactly 1 every window is being
             * written and read INSIDE ONE WINDOW. A region that repeats
             * or skips is landing in the other bank (or not at all), and
             * a constant-value readback would have called that a pass.
             * Report a saturating run length per region so one capture
             * says "it has worked for 7 windows running", not "it worked
             * once". */
            {
                static uint8_t fbx_prevA, fbx_prevB;
                static uint8_t fbx_runA, fbx_runB;
                static uint8_t fbx_first;
#ifdef BOOT_FBX_NOFB
                /* CONTROL: the same block with NO framebuffer access at
                 * all. Both writers, alone or together, came back BLACK
                 * on hardware while the unmodified value-probe painted —
                 * so the suspect is the master touching the FB HERE, and
                 * K_fbfree's "0 of 4 survived" (which is also d=0, also
                 * black) may never have been a reading at all. */
                uint8_t sA = (uint8_t)(fbx_prevA + 1), sB = (uint8_t)(fbx_prevB + 1);
                int okA = 1, okB = 1;
#else
                volatile uint16_t *fa = (volatile uint16_t *)0x24012000u;
                volatile uint16_t *fb2 = (volatile uint16_t *)0x24012040u;
                uint8_t sA = (uint8_t)fa[0], sB = (uint8_t)fb2[0];
                int okA = 1, okB = 1;
                for (unsigned q = 1; q < 13; q++) {
                    if (fa[q] != (uint16_t)q) okA = 0;
                    if (fb2[q] != (uint16_t)q) okB = 0;
                }
#endif
                if (fbx_first) {
                    if (okA && sA == (uint8_t)(fbx_prevA + 1)) {
                        if (fbx_runA < 7) fbx_runA++;
                    } else fbx_runA = 0;
                    if (okB && sB == (uint8_t)(fbx_prevB + 1)) {
                        if (fbx_runB < 7) fbx_runB++;
                    } else fbx_runB = 0;
                }
                fbx_first = 1;
                fbx_prevA = sA;
                fbx_prevB = sB;
                /* bit7 always set: d=0 is BLACK and black is also what a
                 * screen that never flooded looks like — the first
                 * hardware run of this probe came back all-black and
                 * could not be read either way. With the bias, black
                 * means "the MD never got here", never "zero". */
                if (MARS_SYS_COMM8 == 0)
                MARS_SYS_COMM8 = (uint16_t)(0xBB80 | fbx_runA
                                            | (fbx_runB << 3)
                                            | ((okA && okB) ? 0x40 : 0));
            }
#endif
#ifdef BOOT_FBFREE
            /* WHICH FB REGIONS ARE ACTUALLY FREE? (2026-09-08, LOOP27 66)
             * The FB transport needs ~300 bytes nobody else touches. The
             * documented 2KB hole at 0x1E800 is FULL (md_pkt B 1472B +
             * pal 64B at 0x1EDC0 + SAT 512B at 0x1EE00 = exactly 2KB).
             * Visible pixels end at 0x200 + 224*320 = 0x11A00, which is
             * where md_pkt A starts, and A is 1472B ending ~0x11FC0.
             * So 0x11FC0..0x1E800 LOOKS free — but "looks free" is how I
             * put census counters on top of live WRAM twice tonight.
             * Stamp four candidates with a magic word every window and
             * have the 68K check them; anything that changes is in use.
             * Master writes the sentinels; the 68K reads and reports. */
            {
                ((volatile uint16_t *)0x24012000)[0] = 0xA51;
                ((volatile uint16_t *)0x24014000)[0] = 0xA52;
                ((volatile uint16_t *)0x24018000)[0] = 0xA53;
                ((volatile uint16_t *)0x2401C000)[0] = 0xA54;
            }
#endif
#ifdef BOOT_ABDRAW
            /* A/B WRITER PROBE (2026-09-08, LOOP27 10b). The recovered
             * transcript shows s16_68kdraw put a bar ON SCREEN when the
             * 68K wrote the FB, while every SH-2-driven frame stayed
             * black. This asks the missing half directly: does the
             * MASTER's framebuffer write reach the display?
             *
             * WHY THIS IS NOT JUST BOOT_FBBAR AGAIN: that probe drew at
             * rows 0-7, and rows 0-7 ARE OFF MIKE'S DISPLAY (established
             * the same night, after fbbar had already been read as "no
             * bar"). Its result was uninterpretable, not negative. Same
             * write, rows 8-15, is the whole fix.
             *
             * MAGENTA (CRAM 1), fixed index — not bank-coded. This asks
             * "does the master's write show", not "which bank shows";
             * the bank question needs a sound readback and this is not
             * it. Last FB write of the window, FM still ours, so the
             * compose cannot paint over it. */
            {
                volatile uint32_t *px = (volatile uint32_t *)
                    (0x24000000u + 0x200u + 8u * 320u);
                for (int i = 0; i < 8 * 320 / 4; i++)
                    px[i] = 0x01010101u;
                ((volatile uint16_t *)0x20004200)[1] = 0x7C1F;  /* magenta */
                ((volatile uint16_t *)0x20004200)[2] = 0x03E0;  /* green   */
            }
#endif
#ifdef BOOT_FBBAR
            /* HARDWARE PROBE v2: rows 0-7 of the draw bank = index 1 if the
             * display currently shows bank 1 (so this bank is 0) else index
             * 2; CRAM 1 = magenta, CRAM 2 = green. Alternating magenta/green
             * = flips switch the displayed bank; one steady colour = the
             * display never switches; no bar = the shown bank is one we never
             * write. Last FB write of the window, FM still ours. */
            {
                uint32_t fill = (MARS_VDP_FBCTL & 1) ? 0x01010101u : 0x02020202u;
                volatile uint32_t *px = (volatile uint32_t *)(0x24000000u + 0x200u);
                for (int i = 0; i < 8 * 320 / 4; i++)
                    px[i] = fill;
                ((volatile uint16_t *)0x20004200)[1] = 0x7C1F;
                ((volatile uint16_t *)0x20004200)[2] = 0x03E0;
            }
#endif
            /* (The in-window palette drain was REMOVED, entry 28. The
             * render window is active scan by construction, so every
             * CRAM store here stalls the SH-2 until the next hblank —
             * hundreds of them is the window gone and an empty
             * framebuffer. The palette drains in vblank instead, at the
             * top of flip_span.) */
            WSTAGE(0x6318);                      /* GREY: packet B published, before the FM drop */
#ifdef FM_GATE
            /* LOOP 23: the 68K no longer spins — WE hand the FB back.
             * FM clears BEFORE the ack so a gated store that unblocks
             * on FM==0 can never see COMM0 still claiming a window. */
            MARS_SYS_INTMSK &= 0x7FFF;
#endif
            /* (ack-time census 2026-09-06: mean 60 lines post->ack, 22-133;
             * removed for the JP region guard) */
#ifdef FM_LATE
            ((volatile uint32_t *)0x26028FF4)[2] = (uint16_t)(frt() - t_vint);     /* ->ack */
#endif
            WSTAGE(0x7FFF);                      /* WHITE: acking */
#ifdef BOOT_PKTCHK
            *(volatile uint16_t *)0x2000402C = SPR_LAND[R60_W_BM];       /* probe: landed word 20 */
            *(volatile uint16_t *)0x2000402A = SPR_LAND[R60_W_BM - 1];   /* landed word 19 */
            *(volatile uint16_t *)0x20004028 = SPR_LAND[R60_W_BM + 1];   /* landed word 21 (tag) */
#endif
            MARS_SYS_COMM0 = 0;              /* ack: MD drops FM, game runs */
#ifdef PHASE_CENSUS
            PHL[1] += (uint16_t)(frt() - t_vint);
#endif
#ifdef CMD_INT
            __asm__ __volatile__("mov #32,r0\n\tldc r0,sr"
                                 ::: "r0", "memory");  /* back to level 2 */
#endif
            TOK(0);                          /* busy: post-ack compose follows */
            /* (LOOP15 push guard REVERTED same-day: 1380 ticks of
             * master idle post-ack made ares jitter WORSE — the master
             * had no slack; compose was already consuming the freed
             * spin, and the pad pushed k2 flips past the V-gate
             * (dropped frames). The DREQ race fix is per-word FIFO
             * polling on the 68K push instead — deterministic, and
             * paid out of the 68K's own wrap win.) */
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
#ifndef NATIVE_FRAME
            if (tile_cmd) {
                slave_wait(tile_cmd);
                tile_cmd = 0;
            }
#endif
            diag_add(3, tp);

#if defined(WIN_TWO) && defined(NATIVE_FRAME)
            /* ================= NATIVE LAUNCH =================
             * One generation in flight and it is the WHOLE FRAME.
             * Launch only when the previous generation is closed AND
             * shipped (a launch over an unshipped generation would
             * scribble sbuf under the pending blit). The launch is
             * the ONE point where the frame's inputs latch: layer
             * regs + rowscroll, sprite records, the MD-sprite claim.
             * The slave gets the self-chain command (all three bands,
             * cat1+text inline via NOCAT1DEFER); the master's tail
             * runs as poll-gap chunks (nat_mtask). */
            if (k == 2) {
#ifdef GLOW_ANIM
                /* ONE ARCADE TICK per vint. Paused while the 68K is
                 * shipping glow blocks; resumes only after a clean
                 * re-seed of both phases from live PAL_SH (an
                 * unrecognizable state — foreign scene — keeps it
                 * off, delta pipeline rules those words). */
                /* (disp_gate moved before the publish, blank mode) */
#ifdef MD_SPR
                if (mdspr_post && MARS_SYS_COMM8 == 0) {
                    /* art-upload request first: the scene cut wants
                     * the new blob flowing before anything else */
                    MARS_SYS_COMM8 = (uint16_t)(0xBA00 | mdspr_post);
                    mdspr_post = 0;
                }
#endif
                if (glow_post && MARS_SYS_COMM8 == 0) {
                    /* deliver the pending mask grant; BAD1/BAD2 own
                     * the channel when non-zero — retry next vint */
                    MARS_SYS_COMM8 = (uint16_t)(0xBAD0 + glow_post);
                    glow_post = 0;
                }
                /* SCENE GATE (2026-09-05, Mike's floating head): the bake
                 * is LEVEL-1 GRAVEYARD animation. In the transformation
                 * scene the game holds the wave words red (0x100F) and
                 * rotates the ring on its own; our animator over the
                 * mask painted the flames yellow and the band static
                 * (state s16_cand.bs1: PAL_SH wave 30DF.. vs game 100F).
                 * Play only in the normal scene; elsewhere hand the
                 * words back to the 68K. */
                if (glow_on && pscene_cur != 0) {
                    glow_pause = 8;
                    glow_on = 0;
                    glow_post = 3;           /* grant the mask OFF */
                }
                if (glow_pause)
                    glow_pause--;
                else if (!glow_on) {
                    if (pscene_cur == 0
                        && (glow_on = (uint8_t)glow_reseed()) != 0)
                        glow_post = 4;       /* grant the mask ON */
                } else {
                    glow_rp = (uint8_t)(glow_rp < 2 ? glow_rp + 5
                                                    : glow_rp - 2);
                    for (unsigned j = 0; j < 7; j++)
                        PAL_SH[GLOW_RAMP0 + j] = glow_ring2[glow_rp + j];
                    PAL_SETGEN[19]++;        /* words 0x98-0x9F */
                    if (--glow_dw == 0) {
                        if (++glow_ws == GLOW_WAVE_N)
                            glow_ws = 0;
                        glow_dw = glow_dwell[glow_ws];
                        const uint16_t *gw = glow_wave[glow_ws];
                        for (unsigned j = 0; j < 5; j++) {
                            PAL_SH[0xA1 + j] = gw[j];
                            PAL_SH[0xA9 + j] = gw[j];
                        }
                        PAL_SETGEN[20]++;    /* 0xA0-0xA7 */
                        PAL_SETGEN[21]++;    /* 0xA8-0xAF */
                    }
                }
#endif
#ifndef LAUNCH_EARLY
                nat_window_launch(par, bank1, t_vint, win_no, &tile_cmd, &pend_wait);
#endif
            }
            diag_add(8, tw);
#elif defined(WIN_TWO)
            /* 2-WINDOW CYCLE: ALL THREE bands launch at k2 (one par,
             * one snapshot — the frame is a single game state by
             * construction). R0's slave half goes out now; R1/R2's
             * slave cmds CHAIN from the poll loop as each echo lands
             * (pend_rg). Master rows for all three ride the bq queue
             * exactly as before. k1 launches nothing. */
#ifdef R60
            /* AUTO-30 UNDER LOAD: if last frame's compose chain hasn't
             * echoed, skip this frame's launches entirely — blits ship
             * last frame's coherent rows and the display degrades to
             * 30Hz for exactly the overloaded frames. NEVER wait here:
             * waiting at 60Hz cascades (the LOOP26 boot wedge).
             * pend_rg == 0 TOO (2026-08-25, the Zeus starvation): the
             * old test passed whenever the last LAUNCHED cmd had
             * echoed, then the branch below DROPPED the un-launched
             * R1/R2 links and relaunched R0. Under sustained load
             * (the scale-in) that repeats EVERY frame: R0 composes at
             * 60Hz while R1/R2 never run at all — Zeus's torso rows
             * (72+) starve for seconds; the visible cut line is
             * where the last band that DID run ends. Requiring the
             * chain fully flushed makes overload degrade to 30Hz
             * UNIFORMLY — every band composes every OTHER frame —
             * instead of 60Hz for R0 and 0Hz for the rest. The drop
             * path below is now unreachable except as dead armor. */
            int r60_launch = (!pend_wait || SYNC[1] == pend_wait)
                             && pend_rg == 0;

            /* BOUNDED HOLD (worktree experiment, 2026-08-25):
             * pend_rg==0 holds the chain across frames, but a chain
             * the slave never finishes would skip launches FOREVER —
             * R0 too, worse than the Zeus starvation it fixed. Cap
             * the run: on the 3rd consecutive would-be skip, force
             * the launch once. The launch branch already drops
             * unflushed leftovers (pend_rg = 0 below), so the forced
             * frame is exactly the old drop semantics — one frame of
             * R1/R2 a frame late, then the flush discipline resumes.
             * Measured vs unbounded hold, same battery: overload
             * 61.7 -> 52.1% of cycles, BAD1 250 -> 181, rejects
             * 2.83 -> 2.46%, cadence 1.055 -> 1.048. */
            {
                static uint8_t r60_skip_run; /* consecutive k2 skips */
                if (k == 2) {
                    if (!r60_launch && ++r60_skip_run >= 3) {
                        r60_launch = 1;
                        r60_skip_run = 0;
                    } else if (r60_launch)
                        r60_skip_run = 0;
                }
            }
            if (k == 2 && !r60_launch)
                DIAG[30]++;                  /* frames compose-skipped.
                                              * WAS DIAG[21] — collision
                                              * #9: [21] is the preempt-
                                              * blit pickup-timeout hang
                                              * localiser (state_health),
                                              * and AUTO-30 counting into
                                              * it made every overloaded
                                              * frame read as a caught
                                              * hang (Mike's "385
                                              * timeouts" 2026-08-25). */
            if (k == 2 && r60_launch) {
#else
            if (k == 2) {
#endif
#ifdef MD_SPR
                /* LOCKSTEP (2026-08-29, Mike's zombie-under-gravestone
                 * dropouts): the claim/SAT used to rebuild at EVERY
                 * landing while compose skips ~50% of cycles under
                 * load — the MD sprite layer advanced while the FB
                 * generation froze, and a record whose claim flipped
                 * OFF during the freeze was in NEITHER layer (fresh
                 * SAT dropped it; the frozen FB never drew it). The
                 * claim now runs HERE, at chain launch only: MD
                 * sprites and FB content age and refresh TOGETHER.
                 * Skipped cycles re-publish the unchanged scratch. */
                mdspr_claim();
#endif
#ifdef ROW_DEFER
                SYNC[10] &= (uint16_t)~0xCu; /* new chain: stale hazard
                                              * cannot wedge R2 shipping
                                              * (bit 3 = the R1 strip
                                              * window, same rule) */
#endif
#ifdef DIRECT_FB
                dfb_drawn = 1;
                dfb_curbank = (uint8_t)(MARS_VDP_FBCTL & MARS_VDP_FS);               /* this interval composes:
                                              * the NEXT k2 flip ships a
                                              * really-written bank (see
                                              * the stage-1 flip gate) */
#endif
                pend_rg = 0;                 /* chain unflushed (rare —
                                              * heavy gap): DROP the
                                              * leftovers. Their slave
                                              * halves never ran and no bq
                                              * entry exists, so the blit
                                              * ships last frame's
                                              * COHERENT rows — a region
                                              * one frame late, exactly
                                              * the complete-or-defer
                                              * policy. */
                slave_wait(pend_wait);       /* last chain echo (0 at boot
                                              * passes: SYNC[1] boot = 0) */
#ifdef ROW_GEN
                if (md_cut)
                    RG_FLAGS |= 1;       /* cut mode: conservative
                                          * all-dirty. Page churn is
                                          * tracked at cap_page (content
                                          * compare) against the FG's
                                          * pages in rowgen_build. */
#endif
#ifndef NO_SELF_CHAIN
                /* SELF-CHAIN (2026-08-25, profiler conviction): the
                 * chain's links used to relaunch via THIS poll loop on
                 * each echo — a full master round trip per band, and
                 * the profiler showed the cost: slave 34% idle-polling
                 * (STALE: 0.0 idle polls/vint measured 2026-09-10,
                 * LOOP29 118 -- the slave no longer idles at all)
                 * for its next command, master 13% spinning on SYNC[2],
                 * chain pending at 52% of k2s while BOTH CPUs waited on
                 * each other. Bit 0x40 tells the slave to run all three
                 * bands itself, back to back; the master hears ONE echo
                 * when the whole compose is done. pend_rg stays 0 — the
                 * mid-poll chain site is dead code on this build. */
                tile_cmd = (uint16_t)(CMD_TILE | 0x0040 | (par << 8)
                                      | bank1);
                slave_cmd(tile_cmd);
                pend_wait = tile_cmd;
                pend_rg = 0;
                pend_par = (uint8_t)par;
                pend_bank = (uint8_t)bank1;
#elif defined(QUEUED_CHAIN)
                /* QUEUED CHAIN (2026-08-26): all three links posted at
                 * once — SYNC[0] carries R0 as ever, SYNC[10]/[11]
                 * carry R1/R2 for the SLAVE to pull at gap moments it
                 * detects itself (gate arms QPULL=0/1/2). Kills the
                 * 163-line-per-link master relaunch latency without
                 * self-chain's back-to-back trampling. The master
                 * waits on the FINAL echo only; the mid-poll chain
                 * site is dead (pend_rg stays 0). */
                tile_cmd = (uint16_t)(CMD_TILE | (0 << 4) | (par << 8)
                                      | bank1);
                {
                    uint16_t r1c = (uint16_t)(CMD_TILE | (1 << 4)
                                              | (par << 8) | bank1);
                    uint16_t r2c = (uint16_t)(CMD_TILE | (2 << 4)
                                              | (par << 8) | bank1);
                    slave_cmd(tile_cmd);
                    SYNC[10] = r1c;
                    SYNC[11] = r2c;
                    pend_wait = r2c;         /* chain closes on R2's echo */
                    BQ_PUSH(1, par, bank1);
                    BQ_PUSH(2, par, bank1);
                }
                pend_rg = 0;
                pend_par = (uint8_t)par;
                pend_bank = (uint8_t)bank1;

#else
                tile_cmd = (uint16_t)(CMD_TILE | (0 << 4) | (par << 8)
                                      | bank1);
                slave_cmd(tile_cmd);
                pend_wait = tile_cmd;
                pend_rg = 1;
                pend_par = (uint8_t)par;
                pend_bank = (uint8_t)bank1;

#endif
            }
            diag_add(8, tw);
#ifdef R60
            if (k == 2 && r60_launch)
#else
            if (k == 2)
#endif
            {
                cache_purge();               /* pages/maps changed in-window:
                                              * cached lines are stale */
                int rg = 0;                  /* R0 only; R1/R2's entries
                                              * ride the chain (BQ_PUSH) —
                                              * unless SELF-CHAIN, where
                                              * all three push HERE (the
                                              * chain site is dead). */
#else
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
#endif
#ifndef NATIVE_FRAME
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
#if defined(WIN_TWO) && defined(R60) && !defined(NO_SELF_CHAIN)
                for (rg = 0; rg < 3; rg++)   /* SELF-CHAIN: all three
                                              * master halves enqueue at
                                              * launch (bq is 4 deep) */
#endif
                if (bq[bq_t].on) {
                    DIAG[13]++;              /* queue-full deferrals */
                    /* per-band attribution (pass 12): WHICH band pays
                     * the deferral — the lower-third tear question.
                     * [0x28FC8+rg*4]; state_health prints all three. */
                    ((volatile uint32_t *)0x26028FC8)[rg]++;
                } else {
                    struct band *nb = &bq[bq_t];
                    RD_OPEN_M(rg);           /* ROW-DEFER: master half
                                              * open until the terminator
                                              * (the inline push never
                                              * set it — gap closed
                                              * 2026-08-25) */
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
                    bq_t = (bq_t + 1) & 7;
                }
            }
#endif /* !NATIVE_FRAME — the band-queue enqueue (NATIVE has no queue:
        * the launch above is the whole schedule) */

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
#ifdef K2_FREE
                /* LOSSLESS GATE (LOOP24, Mike's Z screens): a built-but-
                 * unpublished packet must not be overwritten — the tile
                 * batch's dirty-clears and allocator claims already
                 * happened at build time, so losing the packet leaves
                 * md_tag claiming tiles VRAM never received: the load-
                 * screen mess and the garbled splash. Per-k staging:
                 * gap-after-k2 -> md_pktA (published k2 next cycle),
                 * gap-after-k1 -> md_pkt (published k2 THIS cycle).
                 * Both publish POST-FLIP at k2 into a bank no flip
                 * touches before the k1-entry consume (MDVERIFY: the
                 * old k1-publish of A was read one generation stale
                 * EVERY cycle — 2772 seq jumps in 2778 consumes — the
                 * A channel was structurally dead). */
#ifdef R60
                /* parity lives in DIAG[36], NOT a block static: a block
                 * static here read 0 at every entry despite the toggle
                 * (something wipes that .bss neighborhood each frame —
                 * OPEN BUG, find the writer; DIAG scrap is outside it
                 * and doubles as visibility). */
#define r60_pkt_flip (DIAG[36] & 1)
                /* BLANK MODE (2026-09-06, attract parity): while the game
                 * blanks its display or our gate still holds, build BOTH
                 * packets per gap (two phases per vint: tiles + a cell
                 * chunk, or two chunks) — the ship line built one, so a
                 * 600-tile cut took ~80 vints. Display-on keeps one build:
                 * a second consume pushes the post past the V-gate. */
                int md_nbuild = 2;
                /* (one build per vint with the display on tried 2026-09-06 as
                 * a 68K diet: -6 lines, no speed change, and the credit-path
                 * logo rewrite littered for ~100 frames instead of ~25) */
                for (int bi2 = 0; bi2 < md_nbuild; bi2++) {
                DIAG[36] ^= 1;               /* alternate A/B per gap */
                if (r60_pkt_flip ? k2f_pendA : k2f_pendB) {
#else
                if (k == 2 ? k2f_pendA : k2f_pendB) {
#endif
                    DIAG[43]++;              /* build deferred whole */
                } else {
#endif
                /* PACKET at the dead FB block. Header is always valid;
                 * payload alternates tiles / name-table chunk.
                 *   [0] magic  [1] type  [2] param
                 *   [3] hscroll  [4] vscroll   (every window, cheap)
                 *   [8..] payload
                 * type 0: 40 tiles, param = first cache slot
                 * type 1: 280 name-table cells, param = first cell */
#ifdef R60
                uint16_t *sc = r60_pkt_flip ? md_pktA : md_pkt;
#else
                uint16_t *sc =
#ifdef K2_FREE
                    (k == 2) ? md_pktA :
#endif
                    md_pkt;
#endif
                const layer_regs *bl = &snap[1];              /* BG layer */
#ifdef NT_WRAP
                /* wrap protocol: VSRAM carries FULL vy (placement grid
                 * anchored on the primary's coarse; per-strip hscroll
                 * carries full vx). sc[3] unused by the receiver. */
                /* sc[3] = plane A hscroll (hs_pair, below) */
                sc[4] = (uint16_t)(bl->vy0 & 0xFF);           /* plane B vy */
                sc[6] = (uint16_t)(snap[0].vy0 & 0xFF);       /* plane A vy */
#else
                sc[3] = (uint16_t)(-(bl->vx0 & 7) & 0x3FF);
                sc[4] = (uint16_t)(bl->vy0 & 7);              /* plane B vy */
                sc[6] = (uint16_t)(snap[0].vy0 & 7);          /* plane A vy */
#endif
                /* sc[7] = plane B hscroll (hs_pair) — the sequence word retired */    /* sequence: the receiver's
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
#ifdef R60
                DIAG[34] = (uint32_t)md_phase | ((uint32_t)md_forced << 8)
                         | ((uint32_t)md_pending << 16);
#endif
                if (md_phase == 0
                    || (md_pending >= MD_BATCH && md_forced < 2)) {
                    if (md_phase != 0) md_forced++;
#ifdef R60
                    DIAG[40]++;          /* builder: tile batches */
#endif
                    /* ---- tile payload: ship dirty md_tag slots,
                     * pixels straight from cart ROM (legal under unpair,
                     * RV pinned 0 — same read tile_pixels does on miss).
                     * The render cache is NOT consulted: it is transient
                     * and its slots have nothing to do with md_tag's. ---- */
                    int sent = 0, sl = md_scan;
                    /* SPEND THE BLACK (the load-in wall): a scene cut
                     * demands ~800 tiles; at 12/vint that is ~1.1s of
                     * visible jumble. The arcade hides its cut behind a
                     * palette fade-to-black — while cut mode is armed,
                     * open the batch to the legacy 40 (staging block
                     * holds 40: the pre-R60 ship size). The consume
                     * spike this risks is a stutter frame during a
                     * transition; the jumble it removes is a second
                     * long. */
                    /* (DISPLAY GATE tried bmax=40 while blanked: the 68K
                     * consume span went 49 -> 94 lines max and art
                     * records were DROPPED — stale title-card art on
                     * gravestone slots in Mike's s16.bs2. Batch stays.) */
                    /* BLANK MODE: the MD display is truly off (the 68K
                     * mirrors our hold too now), so the consume DMA runs
                     * at the blank rate and the full 40-record staging
                     * lands in ~8 lines. */
                    int bmax = (disp_blank || !r60_disp_on) ? 40 : MD_BATCH;
                    /* (md_cut || display-on tried 2026-09-06: consume max 90
                     * lines — the active-display DMA rate, the batch-40 grave) */
                    sc[2] = 0xFFFF;
                    {
                        uint16_t fs = 0xFFFF;
                        sent = md_emit_art(sc + 8, bmax, &sl, &md_pending, &fs);
                        sc[2] = fs;
                    }
                    md_scan = (uint16_t)sl;
                    /* PENDING SELF-HEAL: the scan just visited EVERY slot
                     * and found nothing dirty — the true backlog is zero,
                     * so any residue is phantom (the 162 wedge). Without
                     * this, one leaked count keeps the demand bias on for
                     * the rest of the session. */
                    if (sent == 0) md_pending = 0;
                    hs_pair(sc);                          /* scroll rides the tile chunk too */
#ifdef HS_SHIP
                    HS_OFFS[(sc == md_pktA) ? 1 : 0] = 1;
#endif
                    sc[1] = 0;
                    sc[5] = (uint16_t)sent;
                    DIAG[57] += (uint32_t)sent;
                    if (md_phase == 0) md_phase = 1; /* forced batches do not
                                                      * advance the rotation */
                } else {
#ifdef ART_TAIL
                    uint16_t art_n = 0;
#endif
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
#ifdef CUT_BLANK
                    uint16_t cb_pend0 = md_pending;  /* claims this chunk
                                                      * = storm detector */
                    /* ARM SIGNAL 2 (the dead-cut fix): cells resolving
                     * to a DIRTY slot = foreign art ON SCREEN this
                     * chunk. The claim delta below never reached 80 in
                     * any measured run (cut mode has never armed);
                     * this counts the symptom itself. */
                    uint16_t cb_dirty = 0;
#endif
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
#ifdef NT_WRAP
                        /* PHASE B: mirror-diffed rows. Placement header
                         * as Phase A (prow anchored on the PRIMARY vy
                         * coarse, c0 from this row's own vx coarse; the
                         * fetch below keeps the row's own vxr/vyr).
                         * Before walking, ALIGN the view-space mirror
                         * with the new placement: shift it by the coarse
                         * step so only genuinely new content will diff;
                         * a vertical/teleport change invalidates the
                         * row. 0xFFFF/0xDEAD never match a real entry
                         * (ent bits 10-12 are always 0). */
#ifdef EDGE42
                        /* EDGE42 (2026-09-02, C1 done right, part 3): the
                         * walk ships the two cells BEYOND the screen edge
                         * (cols -1 and 40) as an optional pair after the
                         * row's span (w1 bit 15), so a column is on the
                         * plane before it scrolls in. No mirror for them
                         * (RAM): the pair goes out when the row header
                         * moved, when either cell is art-pending, on the
                         * row after a pending send, and on the backstop
                         * window. cbrow[j] holds col j-1. */
                        uint16_t cbrow[42];
#define CB(col) cbrow[(col) + 1]
#else
                        uint16_t cbrow[40];
#define CB(col) cbrow[col]
#endif
                        uint16_t *mrow =
                            md_dbg_nt + (isfg ? 1120 : 0) + row * 40;
                        int prow = (((wl->vy0 >> 3) & 0x3F) + row) & 31;
                        int c0w  = (vx00 >> 3) & 63;
                        uint16_t hdr = (uint16_t)((prow << 8) | c0w);
                        /* LOSS BACKSTOP: a diffed span that never lands
                         * (stale-bank re-read after a V-gate reject, or
                         * any dropped window) would diverge FOREVER —
                         * the full-chunk protocol self-healed by
                         * re-shipping everything each rotation. Force
                         * one rotating row per visit back to full-ship
                         * (~1s worst repair, ~+3 lines/window). */
                        if ((int)(win_no % 7u) == row - cell0 / 40)
                            for (int i2 = 0; i2 < 40; i2++)
                                mrow[i2] = 0xFFFF;
                        {
                            uint16_t prev =
                                md_dbg_base[(isfg ? 28 : 0) + row];
                            if (prev != hdr) {
                                int dc = (c0w - (prev & 63)) & 63;
                                if ((prev >> 8) != (unsigned)prow
                                    || (dc > 8 && dc < 56)) {
                                    for (int i2 = 0; i2 < 40; i2++)
                                        mrow[i2] = 0xFFFF;
                                } else if (dc <= 8) {     /* forward */
                                    for (int i2 = 0; i2 < 40 - dc; i2++)
                                        mrow[i2] = mrow[i2 + dc];
                                    for (int i2 = 40 - dc; i2 < 40; i2++)
                                        mrow[i2] = 0xFFFF;
                                } else {                   /* backward */
                                    int db = 64 - dc;
                                    for (int i2 = 39; i2 >= db; i2--)
                                        mrow[i2] = mrow[i2 - db];
                                    for (int i2 = 0; i2 < db; i2++)
                                        mrow[i2] = 0xFFFF;
                                }
                            }
                        }
#else
                        (sc + 8 + 280)[row - cell0 / 40] =
                            (uint16_t)(-(vxr & 7) & 0x3FF);
#endif
                        int vy = (vyr - (vyr & 7) + row * 8) & 0x1FF;
                        const uint16_t *pg0 = TILEMAP_C
                            + pqb[(((unsigned)vy >> 7) & 2)] * 0x800
                            + (((unsigned)vy >> 3) & 0x1F) * 64;
                        const uint16_t *pg1 = TILEMAP_C
                            + pqb[(((unsigned)vy >> 7) & 2) + 1] * 0x800
                            + (((unsigned)vy >> 3) & 0x1F) * 64;
#ifdef CAT1_MD
                        if (isfg)
                            CAT1_PEND[row] = 0;
#endif
#ifdef EDGE42
                        unsigned e_pend = 0;     /* an edge cell is art-pending */
                        for (int col = -1; col <= 40; col++) {
#else
                        for (int col = 0; col < 40; col++) {
#endif
                            unsigned vx = (unsigned)(vx00 + col * 8) & 0x3FF;
                            uint16_t w = ((vx >> 9) & 1 ? pg1 : pg0)[(vx >> 3) & 0x3F];
                            /* 2026-09-02 (Mike's crystal ball, s16_fix2.bs1):
                             * the backstop blanked the BG's bottom band
                             * UNCONDITIONALLY; the round-clear screen has
                             * no FG there and legitimately shows the BG
                             * (the ball's base) — "bottom of the sphere
                             * cut off". Blank only where the FG tilemap
                             * word at the same screen cell is nonzero
                             * (the FG covers it, as jts16_prio.v says). */
                            unsigned fgcov = 1;
                            if (!isfg && row >= 24) {
                                const layer_regs *fl = &snap[0];
                                int fvy = (fl->vy0 - (fl->vy0 & 7) + row * 8)
                                          & 0x1FF;
                                unsigned fvx = (unsigned)((fl->vx0 & ~7)
                                                          + col * 8) & 0x3FF;
                                fgcov = TILEMAP_C[fl->pq[((fvy >> 7) & 2)
                                                        + ((fvx >> 9) & 1)]
                                                  * 0x800
                                                  + ((fvy >> 3) & 0x1F) * 64
                                                  + ((fvx >> 3) & 0x3F)] != 0;
                            }
                            if (!isfg && row >= 24 && fgcov) {
                                /* PURPLE BACKSTOP (2026-08-25). View rows
                                 * 24-27 = visible lines 192-223 — always
                                 * the FB-composed floor; the arcade BG
                                 * there is junk the silicon never shows
                                 * (jts16_prio.v: FG covers it). Shipping
                                 * it faithfully made the MD backstop a
                                 * solid-purple wall (tile 0x12D/pal1),
                                 * and every FB miss at the bottom band
                                 * — level-entry latency, ship races —
                                 * displayed it (Mike's corpus 797-809).
                                 * Blank the cells: a miss now falls
                                 * through both planes to the backdrop
                                 * (pal0[0] black). Also skips the junk
                                 * tiles' pen claims. Revisit if any
                                 * scene legitimately shows BG through
                                 * FB holes at the bottom band. */
                                MDA(8);
#ifdef NT_WRAP
                                CB(col) = MD_BLANK_SLOT;
#else
                                md_dbg_nt[row * 40 + col] = MD_BLANK_SLOT;
                                *o++ = MD_BLANK_SLOT;
#endif
                                continue;
                            }
#ifdef CAT1_MD
                            /* C1 (2026-09-01): FG cat-1 cells go to plane A
                             * WITH the MD priority bit (above MD sprites =
                             * S16 cat1-over-pp2); only empty cells blank.
                             * The FB still composes cat1 over SH-2 sprites,
                             * so the picture is unchanged by construction
                             * (opaque pixels: FB wins with the same tile;
                             * holes: both transparent). */
                            if (isfg && w == 0) {
#else
                            if (isfg && (w == 0 || (w & 0x8000))) {
                                /* FG: empty or cat-1 cell -> transparent */
#endif
#ifdef BG_BLANK0
                                ;
                            } else if (w == 0) {
                                /* LOOP28 104: a CLEARED background cell.
                                 * The blank above is foreground-only, so on
                                 * the background an empty entry rendered as
                                 * tile code 0 with colour set 0 and the
                                 * previous scene's cell survived. The game
                                 * clears the whole 64KB tilemap at a scene
                                 * change (routine 0x36b0), so this is what
                                 * a scene change looks like on the BG. */
#endif
                                MDA(7);
#ifdef NT_WRAP
                                CB(col) = MD_BLANK_SLOT;
#else
                                md_dbg_nt[1120 + row * 40 + col] = MD_BLANK_SLOT;
                                *o++ = MD_BLANK_SLOT;
#endif
                                continue;
                            }
#ifdef NT_PROBE
                            /* LOOP28 106: what does the name-table pass
                             * actually DO with a background cell whose
                             * tilemap word went to zero? [47] cells seen,
                             * [48] of them zero, [49] passes. If the zero
                             * count tracks the title screen's sparse map
                             * then the pass reaches them and the staleness
                             * is downstream — the upload or the slot's
                             * tile data, not this walk. */
                            CEN[47]++;
                            if (!isfg && w == 0) CEN[48]++;
#endif
                            unsigned code = w & 0x1FFF;
                            if (code & 0x1000)
                                code = (code & 0xFFF) + (unsigned)bank1 * 0x1000u;
                            GAME_TILE_REMAP(code);      /* per-game code fold (no-op US) */
                            unsigned cset = ((unsigned)w >> 6) & 0x7F;
                            MDA(0);                     /* cells reaching the allocator */
#ifdef CSET_CENSUS
                            cs_note(cset);   /* every ON-SCREEN tile, not
                                              * just the ones being claimed */
#endif
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
#ifdef TAGKEEP
                            /* LOOP29 155 fix: with the wipe deferred the
                             * tags SURVIVE a free, so the cell HITS and
                             * never reaches mdp_note_tile -- the set
                             * stayed line-less and its cells shipped with
                             * line 0. Measured: 11 frees, 0 resolutions.
                             * Re-assign here, before the lookup, so the
                             * deferred wipe resolves against the new map:
                             * identical -> the tags were worth keeping,
                             * moved -> wipe now, exactly as before. */
                            if (!mdp_s_line[cset])
                                mdp_note_tile(cset, code, isfg,
                                              (uint8_t)win_no, C1_SOFT);
#endif
                            uint32_t key = MD_KEY(code, cset)
                                | (isfg ? 0x80000000u : 0u);
                            unsigned s4m = MD_SET(code) * NWAYS;
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
                                    MDA(1);
                                    break;
                                }
                                if (t == 0xFFFFFFFFu) {
                                    if (i2 == MD_BLANK_SLOT)
                                        break;   /* reserved: fall through
                                                  * to evict ways 0..6 */
                                    done = 1;
                                    mdp_note_tile(cset, code, isfg,
                                                  (uint8_t)win_no,
                                                  C1_SOFT);
                                    md_tag[i2] = key;
                                    md_ref[i2] = (uint8_t)win_no;
                                    /* PENDING HONESTY (the 162 wedge): a
                                     * slot already dirty ships ONCE — a
                                     * second increment is a phantom that
                                     * NEVER drains (drain is per tile
                                     * sent). Storm dirty-evicts banked a
                                     * permanent pending=162 -> demand
                                     * bias always on -> cell rotation at
                                     * 1/3 speed = the banded reveals. */
                                    if (!(md_dirty[i2 >> 5]
                                          & (1u << (i2 & 31))))
                                        md_pending++;
                                    MD_MARK(i2);
                                    DIAG[53]++;          /* md_tag claims (COLLIDES: see MDALLOC) */
                                    MDA(2);
                                    slot = i2;
                                    break;
                                }
                                unsigned age = (uint8_t)
                                    ((uint8_t)win_no - md_ref[i2]);
                                if (age >= vage) { vage = age; victim = i2; }
                            }
#ifdef CAT1_MD
                            if (!done && victim != MD_BLANK_SLOT
                                && C1_SOFT && vage < 12) {
                                /* SLOT PRESSURE (2026-09-03, Mike's black
                                 * cells): a cat-1 tile whose cache set is
                                 * full of HOT ways does not evict — the
                                 * evicted BG tile would re-claim at its
                                 * next visit and the two would ping-pong,
                                 * each visit landing on a freshly dirty
                                 * slot = blank under cut mode = black for
                                 * seconds. The FB keeps the cell instead
                                 * (CAT1_PEND below). Measured before the
                                 * rule: cut-blanks 3.9/gen vs shipping
                                 * 0.8/gen on Mike's states. */
                                done = 1;             /* slot stays BLANK */
                                DIAG[39]++;
                                MDA(4);
                            }
#endif
                            if (!done && victim != MD_BLANK_SLOT) {
                                /* set full: evict the LRU way (see md_ref).
                                 * Cells still naming the victim rewrite
                                 * within 4 chunks (~5 windows). */
                                if (vage < 12) DIAG[39]++;   /* HOT evict:
                                                              * victim's cells
                                                              * likely still on
                                                              * screen */
                                mdp_note_tile(cset, code, isfg,
                                              (uint8_t)win_no,
                                              C1_SOFT);
                                md_tag[victim] = key;
                                md_ref[victim] = (uint8_t)win_no;
                                /* PENDING HONESTY: see the free-way
                                 * claim — dirty victim = ships once,
                                 * count once. THIS site was the leak
                                 * (evicting a not-yet-shipped slot). */
                                if (!(md_dirty[victim >> 5]
                                      & (1u << (victim & 31))))
                                    md_pending++;
                                MD_MARK(victim);
                                DIAG[50]++;              /* evictions (COLLIDES: see MDALLOC) */
                                MDA(3);
                                slot = victim;
                            }
                            uint16_t ent = (uint16_t)(slot
#ifdef CAT1_MD
                                | ((unsigned)(mdp_s_line[cset]
                                              ? mdp_s_line[cset]
                                              : MDP_LAST_GET(cset)) << 13));
#else
                                | ((unsigned)mdp_s_line[cset] << 13));
#endif
#ifdef CAT1_MD
                            if (isfg && (w & 0x8000)) {
                                ent |= 0x8000;           /* MD priority */
                                if (!mdp_s_line[cset]
                                    || slot == MD_BLANK_SLOT
                                    || (md_dirty[slot >> 5]
                                        & (1u << (slot & 31))))
                                    CAT1_PEND[row] = 1;  /* FB keeps it */
                            }
#endif
#ifdef CUT_BLANK
                            /* cut mode: a slot whose art has not shipped
                             * yet still holds the PREVIOUS scene's tile;
                             * ship the blank instead (fade cover). The
                             * next chunk visit after the upload lands
                             * rewrites the real entry. Blank slot is
                             * reserved and never claimed, so its dirty
                             * bit can't be set — no self-blanking. */
                            if (md_dirty[slot >> 5] & (1u << (slot & 31))) {
                                cb_dirty++;
#ifdef EDGE42
                                if (col < 0 || col > 39) e_pend = 1;   /* edge quad */
#endif
                                if (md_cut) {
                                    ent = MD_BLANK_SLOT;
                                    ((volatile uint32_t *)0x26028FA0)[0]++;
                                    MDA(5);
                                }
                                else
                                    /* 2026-09-06 (attract parity): outside
                                     * cut mode a pending cell shipped its
                                     * NEW slot = whatever art that slot held
                                     * before (foreign tiles: the red logo's
                                     * litter, ref frames 294-310). Blank it.
                                     * (Keeping the previous entry was tried:
                                     * a page rewrite comes with a palette
                                     * switch, so the old tiles showed under
                                     * the new colours — the dark logo.) */
                                    { ent = MD_BLANK_SLOT; MDA(6); }
                            }
#endif
#ifdef NT_WRAP
                            CB(col) = ent;
#else
                            md_dbg_nt[(isfg ? 1120 : 0) + row * 40 + col] = ent;
                            *o++ = ent;
#endif
                        }
#ifdef NT_WRAP
                        /* diff against the (aligned) mirror; ship only
                         * the changed span. Mirror updated span-only —
                         * cells outside it already match. */
                        {
                            const uint16_t *cb = &CB(0);
                            int st = 0, en = 39;
                            while (st < 40 && cb[st] == mrow[st]) st++;
                            *o++ = hdr;
#ifdef EDGE42
                            uint32_t ebit = 1u << row;
                            uint32_t *epend = &e42_pend[isfg ? 1 : 0];
                            int e_send = (md_dbg_base[(isfg ? 28 : 0) + row] != hdr)
                                         || e_pend || (*epend & ebit)
                                         || (int)(win_no % 7u) == row - cell0 / 40;
                            if (e_pend) *epend |= ebit; else *epend &= ~ebit;
                            uint16_t eflag = e_send ? 0x8000u : 0u;
#else
                            const uint16_t eflag = 0;
#endif
                            if (st < 40) {
                                while (cb[en] == mrow[en]) en--;
                                *o++ = (uint16_t)((st << 8) | (en - st + 1) | eflag);
                                for (int i2 = st; i2 <= en; i2++) {
                                    *o++ = cb[i2];
                                    mrow[i2] = cb[i2];
                                }
                            } else {
                                *o++ = eflag;            /* nothing changed */
                            }
#ifdef EDGE42
                            if (e_send) {
                                *o++ = cbrow[0];         /* col -1 */
                                *o++ = cbrow[41];        /* col 40 */
                            }
#endif
                            md_dbg_base[(isfg ? 28 : 0) + row] = hdr;
                        }
#undef CB
#endif
                    }
#ifdef NT_WRAP
                    /* ALL-STRIPS hscroll delta tail: scroll moves every
                     * affected strip's hscroll TOGETHER, every game
                     * frame — waiting for each row's chunk visit would
                     * make scrolling steppy (the pre-wrap path shipped a
                     * full-screen value every window). Recompute all 56
                     * strips' full hscroll here, ship only changes.
                     * Layout: [n] then n x [(plane<<7)|strip, value]. */
                    {
#ifdef R60
                        /* R60 DENSE TAIL: two 28-word arrays (plane A
                         * strips, then plane B) — the 68K plays each
                         * with ONE strided DMA (auto-inc 32). The delta
                         * tail's worst case (all 56 strips move = every
                         * scrolling frame) measured 35 beam lines of
                         * per-entry FB reads + port writes on the 68K;
                         * this is <1 line and SMALLER than the delta
                         * tail's worst-case 113 words. */
#ifdef HS_SHIP
                        HS_OFFS[(sc == md_pktA) ? 1 : 0] = 632;
#endif
                        /* SCROLL RIDES EVERY PACKET (2026-09-05): the 68K runs
                         * full-screen hscroll (reg 0x0B = 00), so only the two
                         * strip-0 entries take effect — two HEADER words, sc[3]
                         * (plane A) and sc[7] (plane B), in cell AND tile
                         * chunks. Census: a frame shipped in a tile-chunk
                         * window had no scroll words to move the planes with
                         * it — 21 of 61 flips. */
                        hs_pair(sc);
#ifdef HS_SHIP
                        HS_OFFS[(sc == md_pktA) ? 1 : 0] = 1;
#endif
#ifdef ART_TAIL
                        /* ART TAIL (2026-09-05, the scroll-in pop-in): tile
                         * art used to ship only in tile chunks — phase 0 of
                         * the 9-window rotation, or 40 pending — so a new
                         * column's slots showed their OLD art for up to 9
                         * windows. A cell chunk is ~316 words of a 688-word
                         * body (the palette block sits at 688): ride the
                         * dirty slots along in EVERY window. */
                        if (md_pending && o + 18 <= sc + 688) {   /* idle: no scan */
                            volatile uint16_t *na = o++;
                            uint16_t fs = 0xFFFF;
                            int room = (int)((sc + 688 - o) / 17);
                            int sli = md_scan;
                            int n = md_emit_art(o, room > 24 ? 24 : room,
                                                &sli, &md_pending, &fs);
                            md_scan = (uint16_t)sli;
                            *na = (uint16_t)n;
                            o += n * 17;
                            art_n = (uint16_t)n;
                            DIAG[57] += (uint32_t)n;
                        }
#endif
#else
                        volatile uint16_t *nhs = o;
                        uint16_t n2 = 0;
                        *o++ = 0;
                        for (int pl2 = 0; pl2 < 2; pl2++) {
                            const layer_regs *w2 = pl2 ? &snap[0] : bl;
                            for (int r2 = 0; r2 < 28; r2++) {
                                int vxr2 = w2->vx0;
                                if (w2->any_special) {
                                    uint16_t rs2 = w2->rs[r2];
                                    if (rs2 & 0x8000)
                                        vxr2 = w2->vx0_a;
                                    else if (w2->xs_raw & 0x8000)
                                        vxr2 = (int)((0xC0 - (rs2 & 0x3FF))
                                                     & 0x3FF);
                                }
                                uint16_t hv2 =
                                    (uint16_t)((0 - vxr2) & 0x3FF);
                                /* same loss backstop: each strip
                                 * re-ships every 28 windows regardless */
                                if (md_dbg_hs[pl2 * 28 + r2] != hv2
                                    || (int)((win_no + (pl2 ? 14u : 0u))
                                             % 28u) == r2) {
                                    md_dbg_hs[pl2 * 28 + r2] = hv2;
#ifdef ROW_GEN
                                    {   /* scroll moved on THIS strip:
                                         * its 8 screen lines shift */
                                        int _y0 = r2 * 8;
                                        RG_MARK_SPAN(_y0, _y0 + 8);
                                    }
#endif
                                    *o++ = (uint16_t)((pl2 << 7) | r2);
                                    *o++ = hv2;
                                    n2++;
                                }
                            }
                        }
                        *nhs = n2;
#endif
                    }
                    sc[1] = 1;
#ifdef ART_TAIL
                    if (art_n) sc[1] |= 0x4000;      /* art tail present */
#endif
                    sc[2] = (uint16_t)(cell0 | (isfg ? 0x8000 : 0));
                    sc[5] = (uint16_t)(o - (sc + 8));   /* payload words */
#else
                    sc[1] = 1;
                    sc[2] = (uint16_t)(cell0 | (isfg ? 0x8000 : 0));
                    sc[5] = 280;
#endif
#ifdef R60
                    DIAG[41]++;          /* builder: NT chunks */
#endif
                    md_phase++;
                    if (md_phase > 8) { md_phase = 0; md_rot++; }   /* 1 tile + 4 B + 4 A */
#ifdef CUT_BLANK
                    /* arm/decay: >=80 claims in ONE chunk only happens at
                     * scene cuts (panning claims edge strips, <80). 12
                     * chunk-visits ~= 1.5 rotations under demand bias;
                     * storms re-arm. Trickle pressure (the LOOP13
                     * starvation case) never arms — the bound above is
                     * untouched. */
                    /* cut signal census: max unique claims in one chunk
                     * (honest md_pending delta — ships happen in the
                     * tile branch, never mid-chunk). DIAG[48] free. */
                    {
                        uint16_t cd = (uint16_t)(md_pending - cb_pend0);
                        if (DIAG[48] < cd) DIAG[48] = cd;
                        if (cd >= 24) md_cut = 12;
                    }
#ifdef CAT1_MD
                    /* 2026-09-03: dirtiness EXTENDS a cut (its art is still
                     * landing) but never STARTS one — under slot thrash
                     * cb_dirty >= 24 is the steady state and cut mode
                     * became permanent (blank = black cells). Capped at
                     * 36 extra visits so thrash cannot pin it either. */
                    if (cb_dirty >= 24 && md_cut && md_cut_ext < 36) {
                        md_cut = 12;
                        md_cut_ext++;
                    } else if (md_cut) {
                        md_cut--;
                        if (!md_cut) md_cut_ext = 0;
                    }
#else
                    if (cb_dirty >= 24) {
                        if (!md_cut)
                            ((volatile uint32_t *)0x26028FA0)[1]++;
                        md_cut = 12;
                    } else if (md_cut) {
                        md_cut--;
                    }
#endif
#endif

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
#if defined(PEN_HOLD) && defined(TAGKEEP)
                        /* LOOP29 159: pay PENHOLD's debt. A set that is
                         * freed and never re-assigned holds its pens for
                         * ever. This is the same round-robin that fires
                         * the drift, so it already visits every set; a
                         * held set whose last assign is 24+ windows old
                         * is not coming back, so give the pens up. */
                        if (!mdp_s_line[s2] && mdp_pend_tag[s2]
                            && mdp_pend_line[s2]
                            && (uint8_t)((uint8_t)win_no - mdp_s_stmp[s2]) >= 24) {
                            unsigned hl = (unsigned)(mdp_pend_line[s2] - 1);
                            for (int p = 0; p < 8; p++) {
                                unsigned pen;
                                if (!(mdp_pend_used[s2] & (1u << p)))
                                    continue;
                                pen = mdp_pend_map[s2 * 8 + p];
                                if (pen && pen < 16
                                    && mdp_pen_rc[hl * 16 + pen]
                                    && !--mdp_pen_rc[hl * 16 + pen])
                                    mdp_line_c[hl * 16 + pen] = 0xFFFF;
                            }
                            mdp_pend_tag[s2] = 0;
                            mdp_wipe_set_tags(s2);
                            MDA(29);         /* held pens reclaimed */
                        }
#endif
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
                            /* CO-OWNER PATH (2026-08-15, the yellow/purple
                             * slab family): the live CRAM refresh makes a
                             * shared pen DISPLAY its owner's current
                             * colour, so a co-owner whose own live colour
                             * never moved paints owner-coloured filler —
                             * and the bookkeeping gates below (line_c/qc)
                             * see no drift at all, which is why slabs
                             * persisted with [6]=8. Compare a co-owner
                             * against WHAT THE PEN SHOWS (owner live),
                             * same d^2>=18 catastrophic rule. Lockstep
                             * fades still exit on lq == oc. */
                            {
                                unsigned os = mdp_pen_own[(lb + pen) * 2];
                                unsigned op = mdp_pen_own[(lb + pen) * 2 + 1];
                                if (os != s2 || op != (unsigned)p) {
                                    uint16_t oc =
                                        mdp_quant(PAL_SH[os * 8 + op]);
                                    if (lq != oc) {
                                        int er = (int)(oc & 7) - (int)(lq & 7);
                                        int eg = (int)((oc >> 3) & 7)
                                               - (int)((lq >> 3) & 7);
                                        int eb = (int)((oc >> 6) & 7)
                                               - (int)((lq >> 6) & 7);
                                        unsigned ed = (unsigned)(er * er
                                                    + eg * eg + eb * eb);
                                        if (ed >= DRIFT_TOL) {
                                            DRQR[6]++;
                                            MDA(17);
#ifdef DRIFT_VOL
                                            mdp_pen_vol[lb + pen] = 1;
                                            /* LOOP29 154: mdp_claim_pen
                                             * already prefers an EXCLUSIVE
                                             * pen for a set with vol >= 2,
                                             * and NOTHING EVER INCREMENTED
                                             * mdp_s_vol -- it was written,
                                             * read and left at 0 since it
                                             * was added. A co-owner drift
                                             * is exactly the event that
                                             * says "this set must not
                                             * share": it re-merged onto
                                             * the same conflicting pen and
                                             * drifted again 44 times in
                                             * 4000 frames (153), taking
                                             * ~45 resident tiles with it
                                             * each time. */
                                            if (mdp_s_vol[s2] < 255)
                                                mdp_s_vol[s2]++;
#endif
#ifndef DRIFT_MEASURE_ONLY
                                            mdp_free_set(s2);
                                            break;   /* set gone */
#else
                                            break;
#endif
                                        }
                                        DRQR[5]++;
                                        DIAG[38]++;
                                    }
                                    continue;   /* owner-vs-line_c rules
                                                 * below are owner-only */
                                }
                            }
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
                                if (dd >= DRIFT_TOL) {
                                    DRQR[6]++;   /* catastrophic drift */
#ifdef DRIFT_VOL
                                    if (mdp_s_vol[s2] < 255)
                                        mdp_s_vol[s2]++;
#endif
#ifndef DRIFT_MEASURE_ONLY
                                    MDA(18);
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
                 * offset past both payload types. LOOP15 (NT_WRAP):
                 * md_pkt persists between windows, so comparing against
                 * the block's previous content detects change for free;
                 * sc[1] bit15 tells the receiver whether to stage the
                 * CRAM record at all — outside fades that is 48 slow FB
                 * reads + a 51-word DMA record saved EVERY window. The
                 * bookkeeping (mdp_line_c) still updates every window. */
                {
                    uint16_t chg = 0;
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
#ifdef BOOT_PALRAMP
                        /* KNOWN PATTERN THROUGH THE PALETTE PATH
                         * (2026-09-08, LOOP27 36). The packet's palette
                         * word reads 0x0686 on ares and roughly double
                         * that per channel on hardware (entry 35), which
                         * is what a one-bit shift looks like — but that
                         * could be a wrong SOURCE palette or a corrupted
                         * TRANSPORT, and colour cannot tell them apart.
                         * So stop shipping colours: write a RAMP the 68K
                         * knows exactly, 0x0100+i, and let it check. */
                        cw = (uint16_t)(0x0100 + i);
#endif
                        chg |= (uint16_t)(sc[688 + i] ^ cw);
                        sc[688 + i] = cw;
                    }
#ifdef NT_WRAP
                    if (chg)
                        sc[1] |= 0x8000;     /* palette present this window */
#else
                    (void)chg;
#endif
                }
                sc[0] = 0xB6B6;              /* magic LAST: header valid */
#ifdef K2_FREE
#ifdef R60
                if (r60_pkt_flip) { k2f_pendA = 1; DIAG[37]++; }
                else              { k2f_pendB = 1; DIAG[38]++; }
#else
                if (k == 2) k2f_pendA = 1; else k2f_pendB = 1;
#endif
                }                            /* !pend build gate */
#ifdef R60
                }                            /* bi2: blank-mode double build */
#endif
#endif
            }
#endif
        }
    }
}
