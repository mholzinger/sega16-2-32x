/* THE MD PLANES FOR r60 — the slot allocator CARRIED, the walk NEW.
 *
 * The claim/evict loop below is native1 m_main.c:19180-19280 brought across with its
 * state at the addresses it already uses, because it is the most tuned thing in the
 * repo and REBUILD says to keep it. The WALK is new: the old one is woven through
 * cat1code/cat1scr/c1cell_release, the rows-24-27 rowscroll band with its own page set,
 * fgcov and the purple backstop, all of which REBUILD section 5 deletes.
 *
 * The 68K's own consume defines both packet shapes and needs no change:
 *   TYPE 0  (md_main.c:2326-2340)  cnt records of 17 words, slot then 16 words of art;
 *                                  the 68K writes VRAM at slot * 32.
 *   TYPE 1  (md_main.c:2342-2412)  NT_WRAP's seven row records then the hscroll delta
 *                                  tail; nbase 0xC000 plane A, 0xE000 plane B.
 *   palette 48 words at offset 688 -> CRAM entries 16-63, MD lines 1-3.
 */
#include <stdint.h>
#include "mars.h"
#include "r60_tiles.h"

#define R60CODE __attribute__((section(".ramtext")))
/* R60COLD (2026-10-23): the master's once-a-build art fit and the hold fill. In .ramtext on the
 * line; under R60ZOOMFAST they run from cart so the slave's zoom row functions fit the slot
 * (.ramtext ended 20 bytes under its cap on sw307). They cost the master ~4 lines a build on
 * ares; the master idles 150-200 lines a vint. */
#ifdef R60_ZOOMFAST
#define R60COLD
#else
#define R60COLD R60CODE
#endif
#if defined(R60_PHBARS) || defined(R60_ZOOMFAST)
#define R60FITCODE   /* probe builds: the phase timers need .ramtext room; the master's fit runs from cart.
                      * 2026-10-23: under R60ZOOMFAST too (the zoom row functions take the room), see R60COLD */
#else
#define R60FITCODE R60CODE
#endif
/* R60ARTCODE (2026-10-24): the master's per-slot art copy and its tail. Cart under R60ROWFAST /
 * R60PUNCH2, whose slave loops take the .ramtext room (720 bytes over with both); the master idles
 * 150-200 lines a vint on the line (START-HERE MASTER BUDGET). */
#if defined(R60_ROWFAST_OPT) || defined(R60_PUNCH2)
#define R60ARTCODE __attribute__((noinline))
#else
#define R60ARTCODE R60WALKCODE
#endif
#ifdef R60_MBAND_YIELD
void r60_band_yield(void);                        /* r60_main.c: run the master's sprite band now if the slave has posted it */
#define R60_YIELD() r60_band_yield()
#else
#define R60_YIELD() ((void)0)
#endif
extern unsigned r60_unlined;                     /* sw276: defined beside r60_plan_soft below */
#if defined(R60_PRIOINLINE) || defined(R60_PRIOROW) || defined(R60_MBTIME)   /* R60MBTIME: the probe's stamps take the room (ares charges instructions only) */
#define R60WALKCODE  /* the inline mask's loops need .ramtext room; the master's plane walk runs from cart (sw264;
                      * 2026-10-23 PRIOROW too: its keep-masked PLOT5 stores grew r60_sprite by 1.3 KB) */
#else
#define R60WALKCODE R60CODE
#endif
#if (defined(R60_PRIOINLINE) && defined(R60_PHBARS)) || defined(R60_ROWFAST_OPT) || defined(R60_PUNCH2)
#define R60PHWALKCODE  /* and the bars' timers need two more small master helpers out (sw264ph) */
#else
#define R60PHWALKCODE R60CODE
#endif

#define NSETS          128u
#define MD_NWAYS       8u
#define MD_SLOTS       (NSETS * MD_NWAYS)        /* 1024 */
/* THE BLANK IS TILE 0, NOT 1023. The name table writes entry 0 for an empty cell and
 * plane A's table is all zeros, so MD tile 0 is what every empty cell and the whole of
 * plane A show. With slot 0 claimable it held art (measured: 64 non-zero nibbles) and
 * plane A painted that tile over the entire screen in palette line 0, in front of plane
 * B: the planes were loaded, coloured and invisible. Slot 0 is reserved and zeroed. */
#define MD_BLANK_SLOT  0u
#define md_tag         ((uint32_t *)0x0603B400)  /* [1024] longs  -- carried addresses */
#define md_ref         ((uint8_t  *)0x0603B000)  /* [1024] bytes */
#define R60_DISPHOLD_W (*(volatile uint16_t *)0x26028FE0u)   /* the master's display-gate hold (r60_main.c R60_DISPHOLD) */
static inline uint16_t r60_frt_p(void)
{
    uint8_t h = SH2_FRT_FRCH, l = SH2_FRT_FRCL;
    return (uint16_t)((h << 8) | l);
}
#define md_dirty       ((volatile uint32_t *)0x06028EC0)  /* [32] longs; DIAG2 ends at
                                                           * 0x28EC0, do not grow it */
/* THE SLOT KEY IS (PEN MAP, PLANE, CODE). Sets with the same line and the same
 * pixel-to-pen map bake identical art (the title's ten letter sets share one map), so
 * r60_mapid[cset] names the first set with that (line, map) and the key carries it. The
 * plane bit is in the key AND the hash: hashing the code alone put a tile's FG and BG
 * variants in one set and that collision was the whole demo defect (LESSONS.md:3404-3420,
 * old fix m_main.c:3563). The cache is flushed when the plan moves a set. */
#define MD_KEY(code, cset, bg) (((uint32_t)r60_mapid[cset] << 16) | ((bg) << 15) | (code))
#define MD_SET(code, cset, bg) ((((code) ^ ((code) >> 7) ^ ((unsigned)r60_mapid[cset] << 1) ^ ((bg) ? 0x2Bu : 0u))) & (NSETS - 1u))
#define MD_MARK(sl)    (md_dirty[(sl) >> 5] |= 1u << ((sl) & 31u))
/* PINS (2026-10-15): how many shadow cells name each slot. A slot a cell on the table
 * still names is never evicted, so no cell ever shows another tile's art: at forty art
 * records a window the demo cut read 22 stale cells (sw63 census) where twenty read 0,
 * the evictions of a sweep's own slots. Off-screen rows release their cells when the
 * walk skips them, so a scroll cannot pin the cache with rows nobody draws. */
static uint8_t md_pin[MD_SLOTS];
#ifdef R60_BLANKFILL
/* THE SOLID FALLBACK (sw155, 2026-10-17). A BG cell whose art cannot ship this window used to
 * name the blank tile, and the blank over the black backdrop is a BLACK CELL where the arcade
 * draws the set's colour 0 (scr2 is opaque). At the title cut the arcade shows flat 0a60 for
 * thirty frames while the logo's 622 tiles are invisible under an all-blue palette; we showed
 * black blocks for the ~16 windows the art took (Mike's items 1/2/10). The last eight slots
 * (set 127, 1016..1023) hold SOLID tiles, one per (line, pen) in use, pen = the set's
 * colour-0 pen, so an unshipped BG cell is the colour it will mostly be. A stale cell is kept
 * only when it is the SAME tile on the same line (a re-bake); any other stale is garbage.
 * One new solid a window (its record is reserved in the packet); the rest blank until next. */
#define R60_SOLID0     (MD_SLOTS - 8u)           /* 1016: set 127's eight ways */
#define R60_NSOLID     8u
static uint8_t  r60_solid_lp[R60_NSOLID];        /* (line - 1) << 4 | pen, 0 = free */
static uint16_t r60_solid_pin[R60_NSOLID];       /* cells naming each solid (16-bit: a cut names hundreds) */
static unsigned r60_solid_ship, r60_solid_new;   /* the solid assigned this window (+1), 0 = none */
volatile unsigned r60_solid_stats[4];            /* [0] cells sent solid [1] solids assigned [2] refused [3] stale kept */
#define MD_UNPIN(sl)   do { if ((sl) >= R60_SOLID0) { if (r60_solid_pin[(sl) - R60_SOLID0]) r60_solid_pin[(sl) - R60_SOLID0]--; } else if ((sl) && md_pin[(sl)]) md_pin[(sl)]--; } while (0)
#define MD_PIN(sl)     do { if ((sl) >= R60_SOLID0) r60_solid_pin[(sl) - R60_SOLID0]++; else if (sl) md_pin[(sl)]++; } while (0)
static void r60_solid_reset(void)
{
    unsigned i;
    for (i = 0; i < R60_NSOLID; i++) { md_tag[R60_SOLID0 + i] = 0xFFFFFFFEu; md_pin[R60_SOLID0 + i] = 1; r60_solid_lp[i] = 0; r60_solid_pin[i] = 0; }
    r60_solid_new = 0;
}
/* the solid slot for pen `pen` of line `line` (1..4): an existing one, else a free or
 * unreferenced one (assigned and shipped this window), else 0 = blank */
static unsigned r60_solid_get(unsigned line, unsigned pen)
{
    unsigned i, key;
    if (!pen || !line) return 0;
    key = ((line - 1u) << 4) | pen;
    for (i = 0; i < R60_NSOLID; i++) if (r60_solid_lp[i] == key) return R60_SOLID0 + i;
    if (r60_solid_new) { r60_solid_stats[2]++; return 0; }
    for (i = 0; i < R60_NSOLID; i++)
        if (!r60_solid_lp[i] || !r60_solid_pin[i]) {
            r60_solid_lp[i] = (uint8_t)key; r60_solid_pin[i] = 0;
            r60_solid_new = i + 1u; r60_solid_stats[1]++;
            return R60_SOLID0 + i;
        }
    r60_solid_stats[2]++;
    return 0;
}
/* a stale cell is worth keeping only when it is the same art: same line, same code, same map */
extern uint8_t r60_mapid[NSETS];
R60PHWALKCODE static unsigned r60_stale_same(uint16_t cell, unsigned line, unsigned code, unsigned cset)
{
    unsigned os = cell & 0x3FFu;
    uint32_t tg;
    if (!os || os >= R60_SOLID0 || ((cell >> 13) & 3u) != line - 1u) return 0;
    tg = md_tag[os];
    return tg < 0xFFFFFFFEu && (tg & 0x3FFFu) == code && ((tg >> 16) & 0x7Fu) == r60_mapid[cset];
}
#else
#define MD_UNPIN(sl)   do { if ((sl) && md_pin[(sl)]) md_pin[(sl)]--; } while (0)
#define MD_PIN(sl)     do { if (sl) md_pin[(sl)]++; } while (0)
#endif

#define TILEMAP_U      ((const volatile uint16_t *)0x26019000u)  /* 13 pages x 0x800 w */
#define PAL_SH         ((const volatile uint16_t *)0x26027000u)
#define R60_DIAG       ((volatile uint32_t *)0x26028E40u)
#define R60_MDHOLD     (*(volatile uint16_t *)0x26028FE2u)   /* the display gate's hold for the MD (packet bit 13); the slave's word is 0x26028FE0 */
#define R60_DIAG2      ((volatile uint32_t *)0x26028E80u)
extern const uint8_t altbeast_tiles[];           /* 16384 tiles x 64B, one byte a pixel */
extern unsigned r60_artcap;

/* THE PEN PACK, the old renderer's rule made static (m_main.c:622-633, 3855-3917): four
 * MD lines of fifteen pens, pen 0 reserved and never drawn, the backdrop black on purpose
 * (md_main.c:2287-2291). A set lands whole on one line; each of its colours shares an
 * exact match already on the line, else takes a free pen, else the nearest colour
 * (DIAG2[8] counts those). Colour 0 is a real pen for a set the BACKGROUND uses, because
 * the arcade draws scr2 opaque (MAME segas16b_v.cpp:46-47); for an FG-only set pixel 0
 * is transparent and claims nothing (m_main.c:4059, "set 76 pixel 0 is literal magenta").
 * One backdrop cannot carry colour 0: level 1 has six index-0 colours (commit baa33165).
 * Colours are quantised by ROUNDING (+2 >> 2, clamp 7; m_main.c:3595-3602): truncation
 * drew the stage-1 sky's 5/6 checker at 4x contrast and darkened everything.
 * Pen COLOURS re-send from live PAL_SH through the owning (set, pixel) every window, so
 * fades track; the grouping is static between plans. */
#define R60_LINES      4u
static uint16_t r60_lc[R60_LINES * 16];          /* quantised 9-bit colour a pen holds, FFFF free */
static uint8_t  r60_pown_s[R60_LINES * 16];      /* owning set and pixel: the live colour source */
static uint8_t  r60_pown_p[R60_LINES * 16];
uint8_t  r60_smap[NSETS * 8];             /* pixel -> pen, 0 = unmapped */
uint8_t  r60_set_line[NSETS];             /* cset -> 1..4, 0 = not resident */
uint8_t  r60_mapid[NSETS];                /* cset -> first set with the same (line, map) */
uint8_t  r60_set_bg[NSETS];               /* the BG uses the set: colour 0 is a pen */
static uint16_t r60_nt_shadow[2][2048];          /* what the MD name tables hold, per plane */
static uint8_t  r60_rel_wc[2][32];               /* (wc0 & 63) + 1 at each row's last off-screen release, 0 = none */
#define R60_NT_RELEASED 0x0400u                  /* shadow of an off-screen cell whose pin was dropped: bit 10 is no slot (< 1024) and no attribute */
#ifdef R60_KEEPSTALE
static uint16_t r60_nt_prev[2][2048];              /* the cell each shadow entry held before this window's walk: a cell whose new art did not ship keeps it (stale, not black) */
static uint32_t md_reused[MD_SLOTS / 32 + 1];       /* slots re-tagged since the last plan: a stale cell naming one goes blank (sw147: garbage mid-roll) */
#define R60_STALE_OK(cell) (!((md_reused[((cell) & 0x3FFu) >> 5] >> ((cell) & 31u)) & 1u))
#endif

/* cart-resident since sw243: .ramtext is full and this is cold */ static uint16_t r60_quant(uint16_t v)
{
    unsigned r = (((v & 0xFu) << 1) | ((v >> 12) & 1u)) + 2u;
    unsigned g = ((((v >> 4) & 0xFu) << 1) | ((v >> 13) & 1u)) + 2u;
    unsigned b = ((((v >> 8) & 0xFu) << 1) | ((v >> 14) & 1u)) + 2u;
    r >>= 2; g >>= 2; b >>= 2;
    if (r > 7u) r = 7u;
    if (g > 7u) g = 7u;
    if (b > 7u) b = 7u;
    return (uint16_t)((b << 6) | (g << 3) | r);
}

/* ONE PEN for colour q on line l, for set s pixel p: share an exact match, else a free
 * pen, else the nearest. */
#if defined(R60_PENIDX) || !defined(R60_NORECOLOUR)   /* sw283: the recolour/split pass and its state compile on the line too */
/* PENS PER COLOUR INDEX (2026-10-16/17). A palette write must be a CRAM write, not new art:
 * a set's pixel p owns a pen of its own, shared across sets only with an IDENTICAL set at the
 * same index (recoloured together). Under pressure (a line cannot fit the set's indices) the
 * set falls back to the deduping pack (r60_set_mode 0) and behaves as the old line did. */
static uint8_t  r60_set_mode[NSETS];             /* 1 = per-index pens, 0 = deduped (pressure) */
static unsigned r60_claim_mode;                  /* the mode the claims below use */
volatile unsigned r60_pen_stats[8];
volatile unsigned r60_pen_dbg[12];
/* r60_pen_stats: [0] calls [1] in-place recolours [2] splits [3] moves [4] pens with >4 owners
 * [5] disagree-unsettled [6] degraded sets re-packed [7] idle pens freed */
R60CODE static unsigned r60_sets_identical(unsigned s, unsigned t)
{
    unsigned k;
    if (s == t) return 1u;
    for (k = 0; k < 8u; k++) if (r60_quant(PAL_SH[s * 8u + k]) != r60_quant(PAL_SH[t * 8u + k])) return 0;
    return 1u;
}
#endif
/* STABILITY PER (SET, PIXEL) (2026-10-25, the face's orange field). The deduping pack folds
 * equal colours onto one pen, and the pen's colour follows ONE owner. A set whose palette cycles
 * some pixels and holds others (set 20, the face's flames: pixels 2-5 run a 16-frame wave through
 * 305F..30BF and red, pixels 6-7 hold the red field 100F, sw321 f900-1090) packed pixels 5-7 onto
 * one pen whenever pixel 5 was red at pack time, pixel 5 drove the pen, and the field went orange
 * a quarter of every cycle (f1066-1114). Stability was tracked per SET, and set 20 never settles,
 * so the recolour pass never split them. Tracked here for all 1024 (set, pixel) entries every
 * window, resident or not, so a set packs with its history: r60_pix_chg is the window of the
 * pixel's last QUANTISED change (a raw change that quantises the same is no change). */
static uint32_t r60_pal_prev[NSETS * 4u];        /* PAL_SH as longs at the last track */
static uint8_t  r60_pix_chg[NSETS * 8u];         /* (uint8) window of the last change; kept within 128 + 64 of now */
static uint8_t  r60_pix_win;
#define R60_PIX_AGE(s, p)  ((uint8_t)(r60_pix_win - r60_pix_chg[(s) * 8u + (p)]))
#ifndef R60_PIX_SETTLE
#define R60_PIX_SETTLE 32u                       /* windows: R60_SETTLE's 8 recolour passes, one every 4th window */
#endif
#define R60_PIX_SETTLED(s, p) (R60_PIX_AGE(s, p) >= R60_PIX_SETTLE)
__attribute__((noinline)) void r60_pix_track(void)   /* cart; the master, once a window, before the recolour pass */
{
    const volatile uint32_t *pl = (const volatile uint32_t *)PAL_SH;
    uint8_t w = ++r60_pix_win;
    unsigned i;
    for (i = 0; i < NSETS * 4u; i++) {
        uint32_t v = pl[i], o = r60_pal_prev[i];
        if (v == o) continue;
        r60_pal_prev[i] = v;
        if (r60_quant((uint16_t)(v >> 16)) != r60_quant((uint16_t)(o >> 16))) r60_pix_chg[i * 2u] = w;
        if (r60_quant((uint16_t)v) != r60_quant((uint16_t)o)) r60_pix_chg[i * 2u + 1u] = w;
    }
    if (!(w & 63u))                              /* clamp: an age stays readable across the 8-bit wrap */
        for (i = 0; i < NSETS * 8u; i++) if ((uint8_t)(w - r60_pix_chg[i]) > 128u) r60_pix_chg[i] = (uint8_t)(w - 128u);
}
/* sets packed with a nearest-colour fallback: re-packed once stale sets are evicted (r60_repair_degraded) */
static uint8_t r60_set_degraded[NSETS];
static uint32_t r60_row_baked[NSETS];            /* per map id: the pen row its slots were baked with (r60_map_sync) */
/* ONE PEN for colour q on line l, for set s pixel p: share an exact match, else a free pen,
 * else the nearest (counted in R60_DIAG[10]). An exact match whose owner is of the other
 * stability class (one settled, one cycling) is shared only when no pen is free: a steady
 * pixel never rides a cycling one's pen while the line has room. */
#ifdef R60_RAWPENS
/* Equal MD quantisations need not survive the game's palette transforms:
 * S16 2C74/4B73 both quantise to 1A2 but Neff's grayscale gives 16D/124.
 * Keep their source identities while space allows instead of rebaking tiles. */
#define R60_RAW_SAME(s,p,t,k) ((PAL_SH[(s)*8u+(p)] & 0x7FFFu) == (PAL_SH[(t)*8u+(k)] & 0x7FFFu))
#else
#define R60_RAW_SAME(s,p,t,k) 1u
#endif
static unsigned r60_claim_pen(unsigned l, uint16_t q, unsigned s, unsigned p)
{
    unsigned pen, freepen = 0, best = 0, bd = 0xFFFFu, xpen = 0, mine = R60_PIX_SETTLED(s, p);
#ifdef R60_PENIDX
    unsigned sharepen = 0;
#endif
    for (pen = 1; pen < 16u; pen++) {
        uint16_t c = r60_lc[l * 16u + pen];
#ifdef R60_PENIDX
        if (c == q) {
            if (!r60_claim_mode) return pen;                       /* deduped: any equal colour */
            if (r60_pown_p[l * 16u + pen] == p && r60_sets_identical(s, r60_pown_s[l * 16u + pen])) return pen;
            if (!sharepen) sharepen = pen;
            continue;
        }
#else
        if (c == q) {
            if (mine == R60_PIX_SETTLED(r60_pown_s[l * 16u + pen], r60_pown_p[l * 16u + pen]) && R60_RAW_SAME(s,p,r60_pown_s[l * 16u + pen],r60_pown_p[l * 16u + pen])) return pen;
            if (!xpen) xpen = pen;               /* another source/class: only if nothing is free */
            continue;
        }
#endif
        if (c == 0xFFFFu) { if (!freepen) freepen = pen; continue; }
        {
            int dr = (int)(c & 7u) - (int)(q & 7u);
            int dg = (int)((c >> 3) & 7u) - (int)((q >> 3) & 7u);
            int db = (int)((c >> 6) & 7u) - (int)((q >> 6) & 7u);
            unsigned d = (unsigned)(dr * dr + dg * dg + db * db);
            if (d < bd) { bd = d; best = pen; }
        }
    }
    if (freepen) {
        r60_lc[l * 16u + freepen] = q;
        r60_pown_s[l * 16u + freepen] = (uint8_t)s;
        r60_pown_p[l * 16u + freepen] = (uint8_t)p;
        return freepen;
    }
#ifdef R60_PENIDX
    if (sharepen) return sharepen;               /* full: share with the coincident set after all */
#endif
    if (xpen) return xpen;                       /* full: a quantised match beats the nearest */
    R60_DIAG[10]++;                              /* nearest-colour fallbacks (low 16) */
    r60_set_degraded[s] = 1u;
    return best ? best : 1u;
}
/* A PEN FOR A PIXEL SPLIT OFF A SHARED PEN. A settled pixel: an exact colour whose owner is
 * settled, else a free pen, else the nearest (stable: next pass the nearest is the pen it is on).
 * A cycling pixel: a free pen only, its own for good; a nearest pen would change with every step
 * of its cycle and re-key the set each pass (sw126's churn). 0 = stay. */
__attribute__((noinline)) static unsigned r60_claim_split(unsigned l, uint16_t q, unsigned s, unsigned p)
{
    unsigned pen, freepen = 0, best = 0, bd = 0xFFFFu, mine = R60_PIX_SETTLED(s, p);
    for (pen = 1; pen < 16u; pen++) {
        uint16_t c = r60_lc[l * 16u + pen];
        if (c == 0xFFFFu) { if (!freepen) freepen = pen; continue; }
        if (c == q && mine && R60_PIX_SETTLED(r60_pown_s[l * 16u + pen], r60_pown_p[l * 16u + pen]) && R60_RAW_SAME(s,p,r60_pown_s[l * 16u + pen],r60_pown_p[l * 16u + pen])) return pen;
        {
            int dr = (int)(c & 7u) - (int)(q & 7u);
            int dg = (int)((c >> 3) & 7u) - (int)((q >> 3) & 7u);
            int db = (int)((c >> 6) & 7u) - (int)((q >> 6) & 7u);
            unsigned d = (unsigned)(dr * dr + dg * dg + db * db);
            if (d < bd) { bd = d; best = pen; }
        }
    }
    if (freepen) {
        r60_lc[l * 16u + freepen] = q;
        r60_pown_s[l * 16u + freepen] = (uint8_t)s;
        r60_pown_p[l * 16u + freepen] = (uint8_t)p;
        return freepen;
    }
    return mine ? best : 0u;
}

/* the pens set s would need on line l: deduped (equal colours collapse, any equal pen on the
 * line counts) or per index (a pen per index unless an identical set already owns one) */
static unsigned r60_pens_needed(unsigned l, const uint16_t *qc, unsigned mask, unsigned s, unsigned idx)
{
    unsigned need = 0, p, k, pen;
    (void)s; (void)idx;
    for (p = 0; p < 8u; p++) {
        unsigned seen = 0;
        if (!(mask & (1u << p))) continue;
#ifdef R60_PENIDX
        if (idx) {
            for (pen = 1; pen < 16u; pen++)
                if (r60_lc[l * 16u + pen] == qc[p] && r60_pown_p[l * 16u + pen] == p && r60_sets_identical(s, r60_pown_s[l * 16u + pen])) { seen = 1; break; }
            if (!seen) need++;
            continue;
        }
#endif
        for (k = 0; k < p; k++) if ((mask & (1u << k)) && qc[k] == qc[p] && R60_RAW_SAME(s,p,s,k)) { seen = 1; break; }
        if (seen) continue;
        for (pen = 1; pen < 16u; pen++) if (r60_lc[l * 16u + pen] == qc[p] && R60_RAW_SAME(s,p,r60_pown_s[l * 16u + pen],r60_pown_p[l * 16u + pen])) { seen = 1; break; }
        if (!seen) need++;
    }
    return need;
}

/* A SET'S LINE, on demand: the line needing the fewest new pens that has them, else the
 * line with the most free pens (the rest go nearest). Packing only ADDS pens, so a set
 * that appears mid-scene costs no flush; the plan every 32 windows re-packs by cell count
 * and flushes only when a resident set would move. force < R60_LINES tries that line only
 * (a resident set under the plan) and returns 0 when it does not fit. */
#ifdef R60_IDPENS
/* IDENTITY PENS (sw165, 2026-10-17). The title logo is ten colour sets (37-46) that stay IDENTICAL
 * through the whole title (MAME palette RAM at 240/280/300/320/400) and arrive at the cut as a flat
 * provisional palette (three colours). The deduping pack folded their seven indices onto two pens
 * owned by whichever set claimed first, so the fade-in and the red flip could only be drawn by
 * re-baking 622 tiles (the "logo roll", ~30 frames). A set that arrives with a PROVISIONAL palette
 * (<= 3 distinct colours), display up, gets a pen PER INDEX owned by itself; sets identical to it
 * that arrive within the next windows (a run of set numbers, |s - owner| <= 15) share its map. Their
 * recolours are CRAM writes. The plan restores identity sets' pens before packing the rest; a
 * member whose colours leave the owner's drops out and re-packs deduped (its cells re-bake). */
static uint8_t  r60_set_id[NSETS];               /* 0 none, else 1 + the family's owner set */
static unsigned r60_id_open, r60_id_openwin;     /* the family open for joining (1 + owner), windows left */
volatile unsigned r60_id_stats[4];               /* [0] families [1] members joined [2] refused (pens) [3] splits */
static unsigned r60_pack_identity(unsigned s, unsigned bg, const uint16_t *qc, unsigned mask)
{
    unsigned p, k, distinct = 0, need = 0, l, bestl = R60_LINES, bestfree = 0;
    uint16_t seen[8];
    /* A presentation hold on a live picture replacement still needs identity
     * pens: the logo's provisional blue palette must fade without rebaking it. */
    extern unsigned r60_bulk_hold;
    /* Altered Beast's bank-2 crystal picture fades four identical colour sets
     * from one flat colour. Preserve its pixel indices while the blanked scene
     * is packed too: merging those indices requires hundreds of tile rebakes
     * during a short fade, leaving horizontal holes in the portrait. */
    if (R60_DISPHOLD_W && !r60_bulk_hold && r60_tile_bank1() != 2u) return 0;
    for (p = 0; p < 8u; p++) {
        if (!(mask & (1u << p))) continue;
        need++;
        for (k = 0; k < distinct; k++) if (seen[k] == qc[p]) break;
        if (k == distinct) seen[distinct++] = qc[p];
    }
    if (distinct > 3u) return 0;
    if (r60_id_open) {                           /* join the open family when identical to its owner now */
        unsigned o = r60_id_open - 1u, same = 1;
        if (r60_set_line[o] && (s > o ? s - o : o - s) <= 15u) {
            for (p = 0; p < 8u && same; p++) if ((mask & (1u << p)) && r60_smap[o * 8u + p] && r60_quant(PAL_SH[o * 8u + p]) != qc[p]) same = 0;
            if (same) {
                unsigned ln = r60_set_line[o] - 1u;
                for (p = 0; p < 8u; p++) {
                    if (!(mask & (1u << p))) { r60_smap[s * 8u + p] = 0; continue; }
                    r60_smap[s * 8u + p] = r60_smap[o * 8u + p] ? r60_smap[o * 8u + p] : (uint8_t)r60_claim_pen(ln, qc[p], s, p);
                }
                r60_set_line[s] = r60_set_line[o]; r60_set_bg[s] = (uint8_t)bg; r60_mapid[s] = r60_mapid[o];
                r60_set_id[s] = (uint8_t)r60_id_open; r60_id_stats[1]++;
                return r60_set_line[s];
            }
        }
    }
    for (l = 0; l < R60_LINES; l++) {            /* a new family: the line with the most free pens, if it has enough */
        unsigned nfree = 0, pen;
        for (pen = 1; pen < 16u; pen++) if (r60_lc[l * 16u + pen] == 0xFFFFu) nfree++;
        if (nfree >= need && nfree > bestfree) { bestfree = nfree; bestl = l; }
    }
    if (bestl == R60_LINES) { r60_id_stats[2]++; return 0; }
    for (p = 0; p < 8u; p++) {
        unsigned pen;
        if (!(mask & (1u << p))) { r60_smap[s * 8u + p] = 0; continue; }
        for (pen = 1; pen < 16u && r60_lc[bestl * 16u + pen] != 0xFFFFu; pen++) ;
        r60_lc[bestl * 16u + pen] = qc[p]; r60_pown_s[bestl * 16u + pen] = (uint8_t)s; r60_pown_p[bestl * 16u + pen] = (uint8_t)p;
        r60_smap[s * 8u + p] = (uint8_t)pen;
    }
    r60_set_line[s] = (uint8_t)(bestl + 1u); r60_set_bg[s] = (uint8_t)bg; r60_mapid[s] = (uint8_t)s;
    r60_set_id[s] = (uint8_t)(s + 1u); r60_id_open = s + 1u; r60_id_openwin = 8u; r60_id_stats[0]++;
    return bestl + 1u;
}
#endif
static unsigned r60_pack_set_on(unsigned s, unsigned bg, unsigned force)
{
    uint16_t qc[8];
    unsigned mask = bg ? 0xFFu : 0xFEu, p, l, bestl = 0, bestneed = 99u, bestfree = 0, idx = 0;
    for (p = 0; p < 8u; p++) qc[p] = r60_quant(PAL_SH[s * 8u + p]);
    r60_set_degraded[s] = 0;                     /* r60_claim_pen marks it again on a nearest fallback */
#ifdef R60_IDPENS
    if (force >= R60_LINES) { unsigned r = r60_pack_identity(s, bg, qc, mask); if (r) return r; }
#endif
#ifdef R60_PENIDX
    idx = 1u;
again:
#endif
    bestl = 0; bestneed = 99u; bestfree = 0;
    for (l = 0; l < R60_LINES; l++) {
        unsigned need, nfree = 0, pen;
        if (force < R60_LINES && l != force) continue;
        for (pen = 1; pen < 16u; pen++) if (r60_lc[l * 16u + pen] == 0xFFFFu) nfree++;
        need = r60_pens_needed(l, qc, mask, s, idx);
        if (need <= nfree) { if (need < bestneed || (need == bestneed && nfree > bestfree)) { bestneed = need; bestl = l; bestfree = nfree; } }
        else if (bestneed == 99u && nfree >= bestfree) { bestl = l; bestfree = nfree; }
    }
#ifdef R60_PENIDX
    if (idx && bestneed == 99u) { idx = 0; goto again; }   /* pressure: this set packs deduped */
    if (force < R60_LINES && bestneed == 99u) return 0;
    r60_set_mode[s] = (uint8_t)idx;
    r60_claim_mode = idx;
#else
    (void)force;
#endif
    for (p = 0; p < 8u; p++)
        r60_smap[s * 8u + p] = (mask & (1u << p)) ? (uint8_t)r60_claim_pen(bestl, qc[p], s, p) : 0u;
    r60_set_line[s] = (uint8_t)(bestl + 1u);
    r60_set_bg[s] = (uint8_t)bg;
    {   /* the map id: the first set with this line and map */
        unsigned t;
        r60_mapid[s] = (uint8_t)s;
        for (t = 0; t < s; t++) {
            if (r60_set_line[t] != r60_set_line[s]) continue;
            for (p = 0; p < 8u; p++) if (r60_smap[t * 8u + p] != r60_smap[s * 8u + p]) break;
            if (p == 8u) { r60_mapid[s] = r60_mapid[t]; break; }
        }
    }
    return bestl + 1u;
}
static unsigned r60_pack_set(unsigned s, unsigned bg) { return r60_pack_set_on(s, bg, R60_LINES); }

#if defined(R60_PENIDX) || !defined(R60_NORECOLOUR)
#define R60_DROPSTALE 1
uint8_t r60_set_stale[NSETS];                    /* resident but on no page at the last plan; cleared when the walk uses it */
static unsigned r60_stale_any;
static void r60_drop_stale(void);
/* would set s fit some line with no nearest-colour fallback (the deduping count)? */
static unsigned r60_set_fits(unsigned s, unsigned bg)
{
    uint16_t qc[8];
    uint8_t nfree[R60_LINES];
    unsigned mask = bg ? 0xFFu : 0xFEu, p, l, pen;
    for (l = 0; l < R60_LINES; l++) {            /* eight free pens on a line fit any set: the common case, no quantising */
        nfree[l] = 0;
        for (pen = 1; pen < 16u; pen++) if (r60_lc[l * 16u + pen] == 0xFFFFu) nfree[l]++;
        if (nfree[l] >= 8u) return 1u;
    }
    for (p = 0; p < 8u; p++) qc[p] = r60_quant(PAL_SH[s * 8u + p]);
    for (l = 0; l < R60_LINES; l++)
        if (r60_pens_needed(l, qc, mask, s, 0u) <= nfree[l]) return 1u;
    return 0u;
}
#endif
#if defined(R60_PENIDX) || !defined(R60_NORECOLOUR)
void r60_map_sync(void);
#define R60_MAP_SYNC() r60_map_sync()
#else
#define R60_MAP_SYNC() ((void)0)
#endif
/* the slow path, cart: a set packs or gains colour 0, then the slots baked on a moved row re-mark */
__attribute__((noinline)) static unsigned r60_line_slow(unsigned cset, unsigned bg)
{
    unsigned line;
    if (r60_set_line[cset]) {                    /* FG-packed set now used on the BG: needs colour 0 */
        unsigned l = r60_set_line[cset] - 1u;
#ifdef R60_PENIDX
        r60_claim_mode = r60_set_mode[cset];
#endif
        r60_smap[cset * 8u] = (uint8_t)r60_claim_pen(l, r60_quant(PAL_SH[cset * 8u]), cset, 0u);
        r60_set_bg[cset] = 1u;
        R60_MAP_SYNC();                          /* pixel 0's pen joined the row: the set's slots re-mark, its map id re-derives */
        return r60_set_line[cset];
    }
#ifdef R60_DROPSTALE
    if (r60_stale_any && !r60_set_fits(cset, bg)) r60_drop_stale();   /* evict only under pressure: a stale set's art may come back (the face, f1054) */
#endif
    line = r60_pack_set(cset, bg);
    R60_MAP_SYNC();
    return line;
}
R60PHWALKCODE static unsigned r60_line_for(unsigned cset, unsigned bg)
{
#ifdef R60_DROPSTALE
    r60_set_stale[cset] = 0;                     /* on a page: not stale */
#endif
    if (r60_set_line[cset] && (!bg || r60_set_bg[cset])) return r60_set_line[cset];
    return r60_line_slow(cset, bg);
}

/* THE CLAIM, carried. Primary set, eight ways: a hit refreshes, a free way claims, and
 * a full set evicts the oldest by md_ref. MD_TWO_CHOICE is left out -- measured on the
 * FPGA at 2026-09-25 as costing ~20 lines for four cache lines a hit -- so this is the
 * single-set form the line already ran. */
#define MD_SET2(code, cset, bg) (((((code) * 2654435761u) >> 16) ^ ((unsigned)r60_mapid[cset] * 7u) ^ ((bg) ? 0x55u : 0u)) & (NSETS - 1u))
/* TWO CANDIDATE SETS, AND NEVER EVICT A SLOT USED THIS WINDOW. A slot evicted and
 * re-claimed in the same window leaves the name-table cells written earlier in the
 * window pointing at the new tile until their row comes round again (five windows):
 * the title screen drew its letters as a blue hash of each other's tiles (sw23). With
 * the age-0 refusal a cell goes blank for a window instead; the second set makes that
 * rare. The hit path checks both sets. */
static uint32_t r60_ckey[256]; static uint16_t r60_cslot[256];   /* sw197: recent (key, slot), direct-mapped; a stale entry fails the tag check and costs one compare */
/* ANY UNPINNED SLOT BEFORE A REFUSAL (2026-10-25). Both candidate sets can be pinned by visible
 * cells while hundreds of slots elsewhere are free to evict: level 1's zombie walls (pur8g f20880,
 * 614 pinned, an FG wall cell refused for 700 frames, set 74's colour-0 purple through it). Cart,
 * cold: it runs only where the claim would refuse, at most eight times a window. The slot is found
 * again through the cell's own shadow and the recent-key cache, not the set probe. */
__attribute__((noinline)) static unsigned r60_claim_any(unsigned win)
{
    static unsigned any_win, any_n;
    unsigned i, best = MD_BLANK_SLOT, bage = 0;
    if (any_win != win) { any_win = win; any_n = 0; }
    if (any_n >= 8u) return MD_BLANK_SLOT;
    any_n++;
    for (i = 1; i < MD_SLOTS; i++) {
        unsigned age;
        if (md_pin[i]) continue;
        if (md_tag[i] == 0xFFFFFFFFu) return i;
        age = (uint8_t)((uint8_t)win - md_ref[i]);
        if (age > bage) { bage = age; best = i; }
    }
    return best;
}
R60CODE static unsigned r60_claim(unsigned code, unsigned cset, unsigned win, unsigned bg)
{
    /* ONE PASS OVER THE 16 WAYS (sw195): hit, first free way, oldest victim, all from one loop.
     * Three passes cost a cut's walk 1-3 vints a window (sw194 census, 1344 new cells a window). */
    uint32_t key = MD_KEY(code, cset, bg);
    unsigned base[2], k, w;
    unsigned victim = MD_BLANK_SLOT, vage = 0, freei = MD_BLANK_SLOT;
    /* RECENT-KEY CACHE (sw197): a scene repeats its tiles (sky, ground, walls); the cell that
     * claimed a tile last leaves (key, slot) in a 256-entry direct-mapped table, and the next
     * cell with that tile checks the slot's tag once instead of probing 16 ways. */
    unsigned ci = (code ^ (cset << 3) ^ (bg << 7)) & 255u;
    if (r60_ckey[ci] == key) { unsigned i = r60_cslot[ci]; if (md_tag[i] == key) { md_ref[i] = (uint8_t)win; return i; } }
    base[0] = MD_SET(code, cset, bg) * MD_NWAYS;
    base[1] = MD_SET2(code, cset, bg) * MD_NWAYS;
    for (k = 0; k < 2u; k++)
        for (w = 0; w < MD_NWAYS; w++) {
            unsigned i = base[k] + w;
            uint32_t tg;
            if (i == MD_BLANK_SLOT) continue;
            tg = md_tag[i];
            if (tg == key) { md_ref[i] = (uint8_t)win; r60_ckey[ci] = key; r60_cslot[ci] = (uint16_t)i; return i; }
            if (md_pin[i]) continue;
            if (tg == 0xFFFFFFFFu) { if (freei == MD_BLANK_SLOT) freei = i; }
            else { unsigned age = (uint8_t)((uint8_t)win - md_ref[i]); if (age > vage) { vage = age; victim = i; } }
        }
    if (freei != MD_BLANK_SLOT) {
#ifdef R60_KEEPSTALE
        md_reused[freei >> 5] |= 1u << (freei & 31u);   /* a free way after a flush is a reuse as far as a stale cell is concerned */
#endif
        md_tag[freei] = key; md_ref[freei] = (uint8_t)win; MD_MARK(freei);
        r60_ckey[ci] = key; r60_cslot[ci] = (uint16_t)freei;
        return freei;
    }
    if (victim == MD_BLANK_SLOT || vage == 0) {
        victim = r60_claim_any(win);
        if (victim == MD_BLANK_SLOT) { R60_DIAG2[14]++; return MD_BLANK_SLOT; }
    }
#ifdef R60_KEEPSTALE
    md_reused[victim >> 5] |= 1u << (victim & 31u);   /* a stale cell naming this slot would show the new art: blank it instead */
#endif
    md_tag[victim] = key; md_ref[victim] = (uint8_t)win; MD_MARK(victim);
    r60_ckey[ci] = key; r60_cslot[ci] = (uint16_t)victim;
    return victim;
}

/* ONE TILE'S ART, 32 bytes, exactly the conversion native1 m_main.c:12345-12356 does:
 * one byte a pixel in, two pixels a byte out, through the set's pen map. */
R60ARTCODE static void r60_art(volatile uint16_t *w, unsigned slot)
{
    const uint8_t *px = altbeast_tiles + (md_tag[slot] & 0x3FFFu) * 64u;   /* physical art index; bit 15 is the plane */
    volatile uint8_t *o = (volatile uint8_t *)(w + 1);
#ifdef R60_BLANKFILL
    if (slot >= R60_SOLID0) {                    /* a solid: sixteen words of one pen */
        unsigned pen = r60_solid_lp[slot - R60_SOLID0] & 0xFu, k;
        uint16_t v = (uint16_t)((pen << 12) | (pen << 8) | (pen << 4) | pen);
        w[0] = (uint16_t)slot;
        for (k = 1; k <= 16u; k++) w[k] = v;
        return;
    }
#endif
    const uint8_t *map = r60_smap + ((md_tag[slot] >> 16) & 0x7Fu) * 8u;   /* the map id's map */
    unsigned bg = (md_tag[slot] >> 15) & 1u;        /* BG: pixel 0 is a pen; FG: transparent */
    unsigned y;
    const uint32_t *px32 = (const uint32_t *)px;   /* _altbeast_tiles is 16-aligned (s16.lst) and a tile is 64 bytes */
    (void)o;
    w[0] = (uint16_t)slot;
    /* WORD STORES, LONGWORD LOADS (sw181). The byte form wrote the record a byte at a time, and under
     * R60HOLDDIRECT the record is the framebuffer: a byte store of ZERO to the FB is dropped (LESSONS
     * 10555, sw156-160), so every transparent pixel pair of an FG tile kept whatever byte the region
     * held before. Two words a row land whole. Eight pixels a row come in as two longwords, 16 cached
     * cart loads a tile instead of 64. pen = map[pixel], exactly the old md_emit_art. */
    for (y = 0; y < 8u; y++) {
        uint32_t h = px32[y * 2u], l = px32[y * 2u + 1u];
        unsigned p0 = (h >> 24) & 7u, p1 = (h >> 16) & 7u, p2 = (h >> 8) & 7u, p3 = h & 7u;
        unsigned p4 = (l >> 24) & 7u, p5 = (l >> 16) & 7u, p6 = (l >> 8) & 7u, p7 = l & 7u;
        p0 = (p0 || bg) ? map[p0] : 0u; p1 = (p1 || bg) ? map[p1] : 0u; p2 = (p2 || bg) ? map[p2] : 0u; p3 = (p3 || bg) ? map[p3] : 0u;
        p4 = (p4 || bg) ? map[p4] : 0u; p5 = (p5 || bg) ? map[p5] : 0u; p6 = (p6 || bg) ? map[p6] : 0u; p7 = (p7 || bg) ? map[p7] : 0u;
        w[1u + y * 2u] = (uint16_t)((p0 << 12) | (p1 << 8) | (p2 << 4) | p3);
        w[2u + y * 2u] = (uint16_t)((p4 << 12) | (p5 << 8) | (p6 << 4) | p7);
    }
}

/* THE WALK, new. Plane B, the background: page 0 of the tilemap (the page selects never
 * change -- BG page 0, FG page 7), 64 columns by 32 rows of words, read as the arcade
 * means them: code and cset DO overlap in an S16B tile word and that is not a bug. */
#define TEXT_U         ((const volatile uint16_t *)0x26026000u)  /* S16 text RAM, the port's copy */

/* ONE PLANE'S SEVEN ROWS, FULL WIDTH. The S16 page is 64 cells wide and so is the MD name
 * table, and the game selects one page for all four quadrants (BG 0x0000, FG 0x7777), so
 * S16 page column c IS MD column c and the MD's own hscroll does the scrolling. Each row
 * record is the 68K's full-row form: hdr bit 15, then 64 cells, one DMA (md_main.c,
 * `if (hdr & 0x8000u)`). Returns the write pointer after the rows. */
/* ONE PLANE'S SEVEN ROWS, PLACED BY WORLD POSITION. The System 16 plane is a 2 x 2
 * arrangement of 64 x 32 pages (128 x 64 cells) chosen by the page-select word (text RAM
 * 0x740 FG / 0x741 BG; nibbles UL, UR, LL, LR per segaic16, pages > 12 blank) and
 * scrolled by xs = (0xC0 - TEXT[0x74C+which]) & 0x3FF, ys = TEXT[0x748+which] & 0x1FF.
 * The title's art sits on page 2 with the camera over the left half (pages 0x1212); a
 * walk pinned to page 0 showed nothing there. World cell (wc, wr) goes to MD cell
 * (wc & 63, wr & 31) and the MD's own scroll, hscroll = -xs, vscroll = ys & 0xFF, places
 * it; only the 42 x 29 visible cells are written, the rest of the table is stale and off
 * screen. Each row record is the 68K's (start, count) span, diffed against a shadow. */
unsigned r60_walk_rows = 7u;                     /* rows a window: 7 with the display up, 14 under the game's blank (sw168) */
/* A fast pan updates the entering edge across every physical row. The steady
 * walk still rotates full visible rows; the short strip avoids waiting a rotation
 * for a newly exposed column (Mike's sw327 captures 897..933, 2026-10-26).
 * Six columns lead the camera and ten overlap its entering edge. The largest
 * row block is 8 + 32 * (2 + 16) = 584 words, below B's palette at word 688. */
static unsigned r60_walk_cols = 42u, r60_pan_strip, r60_bank1;
static int r60_walk_bias[2];
__attribute__((noinline)) static void r60_pan_walk(uint16_t ha, uint16_t hb)
{
    static uint16_t prev[2]; static unsigned ready, tail[2];
    static int bias[2];
    unsigned h[2], k, strip = 0;
    h[0] = ha; h[1] = hb;
    for (k = 0; k < 2u; k++) {
        int d = (int)((h[k] - prev[k]) & 0x3FFu);
        if (d > 512) d -= 1024;
        if (R60_DISPHOLD_W) tail[k] = 0;
        else if (ready && (d >= 8 || d <= -8)) { tail[k] = 4u; bias[k] = d > 0 ? -6 : 30; }
        if (tail[k]) { tail[k]--; strip = 1; }
        prev[k] = h[k];
    }
    ready = 1;
    r60_pan_strip = strip;
    r60_walk_cols = strip ? 16u : 42u;
    r60_walk_bias[0] = strip ? bias[0] : 0;
    r60_walk_bias[1] = strip ? bias[1] : 0;
    if (strip) r60_walk_rows = 32u;
}
R60WALKCODE static volatile uint16_t *r60_walk_plane(volatile uint16_t *pk, unsigned which,
                                                 unsigned row0, unsigned win,
                                                 unsigned *art_slot, unsigned *nslot,
                                                 unsigned cap)
{
    uint16_t psel = TEXT_U[0x740u + which];
    unsigned xs = (0xC0u - TEXT_U[0x74Cu + which]) & 0x3FFu;
    unsigned ys = TEXT_U[0x748u + which] & 0x1FFu;
    unsigned wc0 = xs >> 3, wr0 = ys >> 3;
    unsigned start_col = wc0 + (unsigned)r60_walk_bias[which];
    volatile uint16_t *cells = pk + 8;
    uint16_t *shadow = r60_nt_shadow[which];
    unsigned r, c;
    for (r = 0; r < r60_walk_rows; r++) {
        unsigned mrow = (row0 + r) & 31u;
        R60_YIELD();
        unsigned wr = wr0 + ((mrow - wr0) & 31u);        /* the visible world row on this MD row */
        unsigned first = 64u, last = 0u;
        uint16_t row[64];
        if (((mrow - wr0) & 31u) >= 29u) {              /* MD rows outside the 29 visible: skip */
            for (c = 0; c < 64u; c++) {                 /* and release what they named */
                unsigned os = shadow[mrow * 64u + c] & 0x3FFu;
                if (os) { MD_UNPIN(os); shadow[mrow * 64u + c] = 0u; }
            }
            *cells++ = (uint16_t)(mrow << 8); *cells++ = 0u; continue;
        }
        /* OFF-SCREEN COLUMNS RELEASE TOO (2026-10-25). The 22 table columns outside the 42 walked
         * kept the pins of cells that scrolled off: on level 1 after the gravestones 211 of 879
         * pinned slots were named only by them, every way of both candidate sets of a wall tile was
         * pinned, the claim refused, and the BG cell drew its set's colour 0 as a solid (set 75,
         * 0x0D07 purple) or the FG cell went blank over a BG pen-0 tile (Mike's purple tiles, sw319).
         * The table entry is left as it is, so the shadow says R60_NT_RELEASED, not 0: it equals no real
         * cell and the walk rewrites the entry when the column is walked again, an empty cell included
         * (a shadow of 0 left the old level's FG standing over the face cut's flames, pur1); a span
         * crossing a released entry writes the blank.
         * Once per row per column position, so a static scene pays nothing (the every-window version
         * landed the attract's packets a frame later from f157 and its cuts 1-2 frames later). */
        for (c = 0; c < 64u; c++) row[c] = shadow[mrow * 64u + c];
        if (r60_pan_strip) {
            r60_rel_wc[which][mrow] = 0; /* retain pins until their entering strip replaces them */
        } else if (r60_rel_wc[which][mrow] != (uint8_t)((wc0 & 63u) + 1u)) {
            r60_rel_wc[which][mrow] = (uint8_t)((wc0 & 63u) + 1u);
            for (c = 42u; c < 64u; c++) {
                unsigned mc = (wc0 + c) & 63u;
                uint16_t sv = row[mc];
                if (sv && sv != 0xFFFFu && sv != R60_NT_RELEASED) { MD_UNPIN(sv & 0x3FFu); shadow[mrow * 64u + mc] = R60_NT_RELEASED; row[mc] = R60_NT_RELEASED; }
            }
        }
        for (c = 0; c < r60_walk_cols; c++) {
            unsigned wc = start_col + c;
            unsigned mcol = wc & 63u;
            unsigned q = ((wc >> 6) & 1u) | (((wr >> 5) & 1u) << 1);
            unsigned page = (psel >> (q * 4u)) & 0xFu;
            uint16_t w = page > 12u ? 0u : TILEMAP_U[page * 0x800u + (wr & 31u) * 64u + (wc & 63u)];
            unsigned code = r60_tile_index(w, r60_bank1);
            unsigned cset = ((unsigned)w >> 6) & 0x7Fu;
            unsigned line, slot;
            uint16_t cell = 0u;
            if (w) {
                R60_DIAG2[4]++;
                line = r60_line_for(cset, which);
                if (!line) r60_unlined++;        /* sw276: a set the pack never lined; enough of them schedule a hard plan */
                if (line) {
                    uint16_t sv0 = shadow[mrow * 64u + mcol];
                    unsigned sl0 = sv0 & 0x3FFu;
                    R60_DIAG2[5]++;
                    /* THE SHADOW'S OWN SLOT FIRST (sw195): a cell that has not changed names a slot whose
                     * tag is this very tile; one compare instead of the 16-way claim. Steady scenes are
                     * almost all such cells. */
#ifdef R60_BLANKFILL
#define R60_FAST_TOP R60_SOLID0                  /* solid slots carry no tag */
#else
#define R60_FAST_TOP MD_SLOTS
#endif
                    if (sv0 != 0xFFFFu && sl0 && sl0 < R60_FAST_TOP && ((sv0 >> 13) & 3u) == line - 1u && md_tag[sl0] == MD_KEY(code, cset, which)) { slot = sl0; md_ref[sl0] = (uint8_t)win; }
                    else
                    slot = r60_claim(code, cset, win, which);
                    if (slot != MD_BLANK_SLOT) {
                        if ((md_dirty[slot >> 5] >> (slot & 31u)) & 1u) {
                            if (*nslot < cap) {
                                md_dirty[slot >> 5] &= ~(1u << (slot & 31u));
                                art_slot[(*nslot)++] = slot;
                            } else {
#ifdef R60_KEEPSTALE
                                slot = 0x8000u; R60_DIAG2[7]++;   /* art not shipped yet: KEEP the old cell (stale beats black; its slot stays pinned by this cell) */
#else
                                slot = MD_BLANK_SLOT; R60_DIAG2[7]++;   /* art not shipped yet: next time (counted with the fit's blanks) */
#endif
                            }
                        }
#ifdef R60_KEEPSTALE
                        if (slot == 0x8000u) {
                            uint16_t sv = shadow[mrow * 64u + mcol];
                            cell = (sv == 0xFFFFu) ? r60_nt_prev[which][mrow * 64u + mcol] : sv;
                            if (cell == 0xFFFFu || cell == R60_NT_RELEASED || !R60_STALE_OK(cell)) cell = 0u;
#ifdef R60_BLANKFILL
                            if (cell && r60_stale_same(cell, line, code, cset)) r60_solid_stats[3]++;
                            else {                   /* not the same art: the set's colour 0 on the BG, blank on the FG */
                                cell = 0u;
                                if (which) { unsigned ss = r60_solid_get(line, r60_smap[cset * 8u]); if (ss) { cell = (uint16_t)(((line - 1u) << 13) | ss); r60_solid_stats[0]++; } }
                            }
#endif
                        } else
#endif
                        cell = (uint16_t)(((line - 1u) << 13) | slot);
                    }
#ifdef R60_BLANKFILL
                    else if (which) {            /* claim REFUSED (both sets' ways pinned, the splash's 800 tiles): the set's colour 0 as a
                                                  * solid, not black (sw229: black cell holes in the cast art on the MiSTer, ares refuses 16 there) */
                        unsigned ss = r60_solid_get(line, r60_smap[cset * 8u]);
                        if (ss) { cell = (uint16_t)(((line - 1u) << 13) | ss); r60_solid_stats[1]++; }
                    }
#endif
                }
            }
            row[mcol] = cell;
            if (cell != shadow[mrow * 64u + mcol]) { if (mcol < first) first = mcol; last = mcol; }
        }
        if (first > last) {
            *cells++ = (uint16_t)(mrow << 8);
            *cells++ = 0u;
        } else {
            unsigned n = r60_pan_strip ? r60_walk_cols : last - first + 1u;
            if (r60_pan_strip) first = start_col & 63u;
            if (!which) (*(volatile uint16_t *)0x26028FB6u)++;   /* R60_FGGEN: the FG changed (the slave's punched sprites key on it) */
            *cells++ = (uint16_t)((mrow << 8) | first);
            *cells++ = (uint16_t)n;
            for (unsigned j = 0; j < n; j++) {
                c = (first + j) & 63u;
                uint16_t nv = (row[c] == R60_NT_RELEASED) ? 0u : row[c];
                unsigned os = shadow[mrow * 64u + c] & 0x3FFu, ns = nv & 0x3FFu;
                if (os != ns) { MD_UNPIN(os); MD_PIN(ns); }
#ifdef R60_KEEPSTALE
                if (shadow[mrow * 64u + c] != 0xFFFFu) r60_nt_prev[which][mrow * 64u + c] = shadow[mrow * 64u + c];   /* a flushed shadow keeps the older cell as the stale fallback */
#endif
                *cells++ = nv; shadow[mrow * 64u + c] = nv;
            }
        }
    }
    return cells;
}

#ifdef R60_CUTPF
/* CUT-ART PRELOAD (R60CUTPF, sw249). The demo's transformation cut (the face, then the flames)
 * is a page switch to pages 10 (FG) and 11 (BG), and the game wrote those pages long before:
 * measured on sw248, both pages are byte-identical from frame 600 to the switch at 1054 (127 FG
 * tiles in sets 20/21, 32 BG tiles in set 19), while the demo leaves ~280 of 1024 slots free.
 * The live cut then shipped that art over ten frames after the switch (records at 1054, 1062-1064,
 * 1069) and the face painted in patches. So while an attract demo runs and those pages are not on
 * screen, walk them PF_ROWS rows a window, pack their sets (identity pens first, nothing that is
 * resident moves), claim FREE ways only (never a victim: the level is in view) and ship their art
 * in packet A's spare room. Each preloaded slot is pinned once (r60_pf_pinned) so the demo's own
 * claims cannot evict it; the pins are released after the window in which the switch is walked,
 * by which time the cells naming those slots hold their own pins. The old pipeline did the same on
 * the state word's cut bit (m_main.c CUT_PREFETCH); here the bit leads by two frames only, so the
 * trigger is the scene (attract step 0x0C, demo 1) and the pages' absence. */
#define PF_ROWS 4u
static uint32_t r60_pf_pinned[MD_SLOTS / 32u];
static unsigned r60_pf_row, r60_pf_npin;
unsigned r60_pf_stats[4];                        /* [0] slots preloaded [1] art shipped [2] no free way [3] releases */
static void r60_pf_release(void)
{
    unsigned i;
    if (!r60_pf_npin) return;
    for (i = 0; i < MD_SLOTS; i++)
        if ((r60_pf_pinned[i >> 5] >> (i & 31u)) & 1u) { MD_UNPIN(i); }
    for (i = 0; i < MD_SLOTS / 32u; i++) r60_pf_pinned[i] = 0;
    r60_pf_npin = 0; r60_pf_row = 0; r60_pf_stats[3]++;
}
__attribute__((noinline)) static unsigned r60_pf_step(unsigned win, unsigned *art, unsigned n, unsigned cap)
{
    extern unsigned char WRAM_BASE[];
    unsigned step = ((const volatile uint8_t *)WRAM_BASE)[0x3031];   /* 68K 0xFFF031, the attract step */
    uint16_t fsel = TEXT_U[0x740u], bsel = TEXT_U[0x741u];
    unsigned r, which;
    static unsigned hold;
    if (fsel == 0xAAAAu || bsel == 0xBBBBu) {   /* the switch: keep the pins one full name rotation (32 rows, 7 a window) */
        if (r60_pf_npin && !hold) hold = 6u;
        if (hold && --hold == 0u) r60_pf_release();
        return n;
    }
    hold = 0;
    if (step != 0x0Cu) { r60_pf_release(); return n; }   /* sw252: demo 1 only; demo 2 (0x14) never selects pages 10/11 (measured, two loops) and the pins raised its flicker 32k -> 51k */
    if (r60_pf_row >= 32u) return n;             /* both pages walked; their pins hold until the switch */
    for (r = 0; r < PF_ROWS && r60_pf_row < 32u; r++, r60_pf_row++)
        for (which = 0; which < 2u; which++) {
            const volatile uint16_t *row = TILEMAP_U + (10u + which) * 0x800u + r60_pf_row * 64u;
            unsigned c;
            for (c = 0; c < 64u; c++) {
                uint16_t w = row[c];
                unsigned code, cset, line, k, i = MD_BLANK_SLOT, freei = MD_BLANK_SLOT;
                uint32_t key;
                if (!w) continue;
                code = r60_tile_index(w, r60_bank1); cset = ((unsigned)w >> 6) & 0x7Fu;
                line = r60_line_for(cset, which);
                if (!line) continue;
                key = MD_KEY(code, cset, which);
                for (k = 0; k < 2u * MD_NWAYS && i == MD_BLANK_SLOT; k++) {
                    unsigned j = (k < MD_NWAYS ? MD_SET(code, cset, which) : MD_SET2(code, cset, which)) * MD_NWAYS + (k & (MD_NWAYS - 1u));
                    if (j == MD_BLANK_SLOT) continue;
                    if (md_tag[j] == key) i = j;
                    else if (md_tag[j] == 0xFFFFFFFFu && freei == MD_BLANK_SLOT && !md_pin[j]) freei = j;
                }
                if (i == MD_BLANK_SLOT) {
                    if (freei == MD_BLANK_SLOT) { r60_pf_stats[2]++; continue; }
                    i = freei; md_tag[i] = key; MD_MARK(i);
#ifdef R60_KEEPSTALE
                    md_reused[i >> 5] |= 1u << (i & 31u);
#endif
                    r60_pf_stats[0]++;
                }
                md_ref[i] = (uint8_t)win;
                if (!((r60_pf_pinned[i >> 5] >> (i & 31u)) & 1u)) { r60_pf_pinned[i >> 5] |= 1u << (i & 31u); MD_PIN(i); r60_pf_npin++; }
                if (((md_dirty[i >> 5] >> (i & 31u)) & 1u) && n < cap) {
                    md_dirty[i >> 5] &= ~(1u << (i & 31u));
                    art[n++] = i; r60_pf_stats[1]++;
                }
            }
        }
    return n;
}
#endif

/* THE ART TAIL: [n] then n inline records (slot|0x4000, 16 art words), the 68K's
 * slim_walk form. The blank tile (slot 0) is re-sent in packet B every window. */
R60ARTCODE static unsigned r60_art_tail2(volatile uint16_t *cells, const unsigned *art_slot,
                                      unsigned nslot, const unsigned *art2, unsigned n2, unsigned with_blank)
{
    unsigned i, k, n = 0;
    volatile uint16_t *cnt = cells++;
    if (with_blank) {
        *cells++ = 0x4000u;
        for (k = 0; k < 16u; k++) *cells++ = 0u;
        n++;
#ifdef R60_BLANKFILL
        if (r60_solid_ship) {                    /* the solid assigned this window: its record is reserved */
            r60_art(cells, r60_solid_ship);
            cells[0] = (uint16_t)(r60_solid_ship | 0x4000u);
            cells += 17; n++;
            r60_solid_ship = 0;
        }
#endif
    }
    for (i = 0; i < nslot; i++) {
        if (!(i & 7u)) R60_YIELD();
        r60_art(cells, art_slot[i]);
        cells[0] = (uint16_t)(art_slot[i] | 0x4000u);
        cells += 17; n++;
    }
    for (i = 0; i < n2; i++) {                   /* a second list: the other plane's art under a fat window */
        r60_art(cells, art2[i]);
        cells[0] = (uint16_t)(art2[i] | 0x4000u);
        cells += 17; n++;
    }
    *cnt = (uint16_t)n;
    return n;
}
R60COLD static unsigned r60_art_tail(volatile uint16_t *cells, const unsigned *art_slot,
                                     unsigned nslot, unsigned with_blank)
{
    return r60_art_tail2(cells, art_slot, nslot, (const unsigned *)0, 0u, with_blank);
}

/* THE ART BUDGET IS THE PACKET'S FREE SPACE (2026-10-15). The walk claims art up to
 * R60_ARTMAX before the rows' size is known; what does not fit behind the rows is given
 * back: its dirty bit returns (shipped next visit) and every cell this packet wrote that
 * names it goes blank, in the packet and in the shadow, so no cell ever names art the
 * 68K has not landed. Measured need: the boot card is 800 unique tiles and the demo cut
 * 581; at ten a plane the card took ~100 windows and the demo still had 117 holes 90
 * frames after its cut (sw61 cell census). The 68K's consume ends by line 238 of 262
 * with that ten, so vblank holds the rest. */
R60FITCODE static unsigned r60_art_fit_n(volatile uint16_t *pk, volatile uint16_t *e, unsigned avail,
                                      unsigned *art_slot, unsigned n, unsigned which);
R60COLD static unsigned r60_art_fit(volatile uint16_t *pk, volatile uint16_t *e, unsigned limit,
                                    unsigned *art_slot, unsigned n, unsigned which)
{
    unsigned used = (unsigned)(e - pk), avail;
    if (used + 1u >= limit) avail = 0; else avail = (limit - used - 1u) / 17u;
    /* THE BLANK RECORD COUNTS (sw166, 2026-10-17). Packet B's tail is the count word, the
     * blank (slot 0) record, then the art; this budget had counted the art alone, so a full
     * tail (25 art + blank = 26 records) ran 17 words past 688 and the palette block, written
     * after the tail, overwrote the LAST art record: its slot's cells stayed black until some
     * later window re-shipped it (the title cut's 4x3 grid of black cells, slot 895; the
     * 68K also read that record as slot 0 with palette words for art). */
    if (which && avail) avail--;                 /* the blank record */
#ifdef R60_BLANKFILL
    if (which && avail) avail--;                 /* one record kept for this window's solid (the blank's packet) */
#endif
    return r60_art_fit_n(pk, e, avail, art_slot, n, which);
}
R60FITCODE static unsigned r60_art_fit_n(volatile uint16_t *pk, volatile uint16_t *e, unsigned avail,
                                      unsigned *art_slot, unsigned n, unsigned which)
{
    unsigned i;
    uint16_t *shadow = r60_nt_shadow[which];
    volatile uint16_t *q;
    if (n <= avail) return n;
    for (i = avail; i < n; i++) md_dirty[art_slot[i] >> 5] |= 1u << (art_slot[i] & 31u);
    {   /* sw231: the given-back slots as a bitmap, one test a cell; the k-loop was cells x
         * (n - avail) compares, ~56k a plane at a cut (0.25 frame of the fat window's 2-2.7) */
        static uint32_t gb[MD_SLOTS / 32u];
        for (i = 0; i < MD_SLOTS / 32u; i++) gb[i] = 0;
        for (i = avail; i < n; i++) gb[art_slot[i] >> 5] |= 1u << (art_slot[i] & 31u);
    q = pk + 8;
    while (q < e) {                              /* every row record this packet carries */
        unsigned mrow = q[0] >> 8, first = q[0] & 0xFFu, cnt = q[1], c;
        q += 2;
        for (c = 0; c < cnt; c++) {
            unsigned sl = q[c] & 0x3FFu, k;
            if (!q[c]) continue;
            if (!((gb[sl >> 5] >> (sl & 31u)) & 1u)) continue;
            for (k = avail; k < n; k++)
                if (art_slot[k] == sl) {
#ifdef R60_KEEPSTALE
                    uint16_t pv = r60_nt_prev[which][mrow * 64u + ((first + c) & 63u)];   /* row spans may wrap */
                    if (pv == 0xFFFFu || pv == R60_NT_RELEASED || !R60_STALE_OK(pv)) pv = 0u;
#ifdef R60_BLANKFILL
                    {   unsigned ln = ((q[c] >> 13) & 3u) + 1u, mid = (md_tag[sl] >> 16) & 0x7Fu;
                        if (pv && r60_stale_same(pv, ln, md_tag[sl] & 0x3FFFu, mid)) r60_solid_stats[3]++;
                        else {
                            pv = 0u;
                            if (which) { unsigned ss = r60_solid_get(ln, r60_smap[mid * 8u]); if (ss) { pv = (uint16_t)(((ln - 1u) << 13) | ss); r60_solid_stats[0]++; } }
                        }
                    }
#endif
                    q[c] = pv; shadow[mrow * 64u + ((first + c) & 63u)] = pv; MD_UNPIN(sl); MD_PIN(pv & 0x3FFu); R60_DIAG2[7]++; break;
#else
                    q[c] = 0u; shadow[mrow * 64u + ((first + c) & 63u)] = 0u; MD_UNPIN(sl); R60_DIAG2[7]++; break;
#endif
                }
        }
        q += cnt;
    }
    }
    return avail;
}

/* BOTH PLANES, BOTH PACKETS. A = the FG plane (S16 scr1, page 7) to MD plane A, B = the
 * BG plane (scr2, page 0) to MD plane B with the palette block. Scroll: the old renderer's
 * chain (native1 m_main.c:4603 vx0 = (0xC0 - xraw) & 0x3FF; :12412 word = -vx0), i.e.
 * MD hscroll = (xraw - 0xC0) & 0x3FF, xraw = TEXT[0x74C + which]. Both packets carry both
 * words because the 68K writes 0xFC00/0xFC02 from every packet (md_main.c:2021). Rows
 * cost 8 + 7 x 65 = 463 words; the tail fits 13 records before the palette at 688. */
unsigned r60_pkta_words = 736u;                  /* words of packet A the master copies to the FB this window */
#ifdef R60_HOLDDIRECT
unsigned r60_pkta_cap = 3072u;                   /* packet A's word cap this window (11264 direct) */
unsigned r60_artmax = 160u;                      /* the walk's art list this window (500 direct) */
#endif
unsigned r60_pktb_words = 752u;                  /* words of packet B (through the palette block when it ships) */
R60CODE unsigned r60_planes_build(volatile uint16_t *pa, volatile uint16_t *pb,
                                  unsigned row0, unsigned win)
{
    r60_bank1 = r60_tile_bank1();
#ifndef R60_ARTMAX
#define R60_ARTMAX 160u                          /* the walk's list; the packet's free space bounds what ships (r60_art_fit) */
#endif
#ifdef R60_HOLDDIRECT
    /* THE DIRECT HOLD PACKET (sw169, 2026-10-17). Under the game's blank the master writes
     * packet A straight into the framebuffer (no staging copy, no flip): 11264 words a
     * window, 500 art records, where the staged fat packet held 3072 and 160. The art lists
     * live in the probe scrap (0x38000, 4 KB, free on the line: mars.ld:323). */
#define R60_PKTA_CAP r60_pkta_cap
#define R60_ARTLIST  r60_artmax
    unsigned *art_a = (unsigned *)0x06038000u, *art_b = (unsigned *)0x06038800u, na = 0, nb = 0, nart;   /* 0x38000-0x38FFF: not .ramtext's to grow into (2026-10-23: the 0x29000 scratch was tried and holds the speech PWM buffer at 0x29800) */
#else
#define R60_PKTA_CAP 3072u                       /* packet A under a FAT window, words: the staging and the FB region (0x17000..0x1E000) hold it */
#define R60_ARTLIST  R60_ARTMAX
    unsigned art_a[R60_ARTMAX], art_b[R60_ARTMAX], na = 0, nb = 0, nart;
#endif
    volatile uint16_t *e, *eb;
    unsigned fat = r60_artcap > 40u;             /* FAT window (the master sets artcap 160 under the blank hold): both planes' art rides packet A */
#ifdef R60_EMPTYPKT
    unsigned a_empty = 0;
#endif
    uint16_t hsA = (uint16_t)((TEXT_U[0x74Cu] - 0xC0u) & 0x3FFu);   /* = -xs mod 1024 */
    uint16_t hsB = (uint16_t)((TEXT_U[0x74Du] - 0xC0u) & 0x3FFu);
    uint16_t vsA = (uint16_t)(TEXT_U[0x748u] & 0xFFu);             /* ys mod 256: the NT is 32 rows */
    uint16_t vsB = (uint16_t)(TEXT_U[0x749u] & 0xFFu);
    unsigned l, p;

#ifdef R60_BLANKFILL
    r60_solid_new = 0; r60_solid_ship = 0;
#endif
    /* UNDER THE GAME'S BLANK, TWICE THE ROWS (sw168, 2026-10-17). A cut's art reached the
     * screen at the pace the 7-row rotation visited it: the face-to-eye cut shipped 1-2
     * records on most of its 14 windows while the fat packet had room for 140, and we held
     * black 32 frames where the arcade shows 4. Fourteen rows a window visits the screen in
     * two; packet B's rows still end before the palette block (14 x 44 + 8 = 624 < 688). */
    /* 14, NOT 16 (sw256). Packet B's rows must end before its palette block at word 688: 16 full
     * rows are 16 x 44 + 8 = 712 words, the palette is written after the rows and overwrote B's
     * last cells and its art-tail count, and the 68K read a colour as the count and walked
     * thousands of garbage "records" through the framebuffer (slim_diag: 2041 dropped and 160
     * bogus baked records per event; junk art DMA'd into random slots; tools/artaudit.py: 19
     * persistent wrong cells after START, 47 in the second logo splash). 14 x 44 + 8 = 624. */
    r60_walk_rows = (R60_DISPHOLD_W == 1u) ? 14u : 7u;
#ifdef R60_CUTPF
    {   /* A LIVE PAGE SWITCH WALKS 14 ROWS (sw250). With the cut's art preloaded the switch windows
         * carry almost no art, and the face painted over five 7-row windows. For three windows after
         * either page select changes with the display up, walk 14 rows: packet B's rows still end
         * before the palette block (14 x 44 + 8 = 624 < 688), the 68K's row cap is 16, and the
         * rotation advances by the rows walked (r60_main.c row0). */
        static uint32_t pf_prev; static unsigned pf_churn;
        uint32_t ps = ((uint32_t)TEXT_U[0x740u] << 16) | TEXT_U[0x741u];
        if (ps != pf_prev) { pf_prev = ps; pf_churn = 3u; }
        if (R60_DISPHOLD_W != 0u) pf_churn = 0;  /* a switch under the hold is filled by the hold's own 16-row windows */
        if (pf_churn) { pf_churn--; r60_walk_rows = 14u; }
    }
#endif
#ifdef R60_CHURNCAP
    if (R60_DISPHOLD_W == 0u && r60_artcap > 40u) r60_walk_rows = 16u;   /* sw210: a churn window (live cut) walks 16 rows too; the picture is changing anyway */
#endif
    r60_pan_walk(hsA, hsB);
#ifdef R60_IDPENS
    if (r60_id_openwin && --r60_id_openwin == 0u) r60_id_open = 0;
    {   /* a member whose colours left its owner's leaves the family: its cells re-pack deduped */
        unsigned s2;
        for (s2 = 0; s2 < NSETS; s2++) {
            unsigned o = r60_set_id[s2] ? r60_set_id[s2] - 1u : 0u, p2;
            if (!r60_set_id[s2] || o == s2 || !r60_set_line[s2]) continue;
            for (p2 = 0; p2 < 8u; p2++)
                if (r60_smap[s2 * 8u + p2] && r60_quant(PAL_SH[s2 * 8u + p2]) != r60_quant(PAL_SH[o * 8u + p2])) {
                    r60_set_id[s2] = 0; r60_set_line[s2] = 0; r60_set_bg[s2] = 0; r60_mapid[s2] = (uint8_t)s2; r60_id_stats[3]++;
                    break;
                }
        }
    }
#endif
    { extern volatile uint16_t r60_census[8]; r60_census[2] = r60_frt_p(); }   /* phase timers (sw196 probe): [2] walk B [3] walk A [5] fits + tails, FRT ticks */
    e = r60_walk_plane(pb, 1u, row0, win, art_b, &nb, r60_artcap < R60_ARTLIST ? r60_artcap : R60_ARTLIST);   /* BG = scr2 -> plane B */
    { extern volatile uint16_t r60_census[8]; uint16_t _t = r60_frt_p(); r60_census[2] = (uint16_t)(_t - r60_census[2]); r60_census[3] = _t; }
    eb = e;
    if (!fat) {
        nb = r60_art_fit(pb, e, 688u, art_b, nb, 1u);    /* the palette block sits at 688 */
#ifdef R60_BLANKFILL
        if (r60_solid_new) r60_solid_ship = R60_SOLID0 + r60_solid_new - 1u;
#endif
        nart = r60_art_tail(e, art_b, nb, 1u);
        r60_pktb_words = (unsigned)(e - pb) + 1u + 17u * nart;
    } else { nart = 0; r60_pktb_words = (unsigned)(e - pb) + 1u; }   /* FAT: B's art goes out on A; B's tail is written after A's fit */
    pb[1] = (uint16_t)(0xC001u | (R60_MDHOLD ? 0x2000u : 0u));   /* type 1, palette (15), art tail (14), MD display hold (13) */
    pb[2] = (uint16_t)(win & 0xFFu);             /* plane B; low byte = this window's sequence (sw173: the 68K skips a stale, unconsumed packet) */
    pb[3] = hsA; pb[7] = hsB; pb[4] = vsB; pb[6] = vsA;
    pb[5] = (uint16_t)(r60_walk_rows * 64u);   /* rows walked x 64: the 68K's row loop bound (sw168) */

    e = r60_walk_plane(pa, 0u, row0, win, art_a, &na, r60_artcap < R60_ARTLIST ? r60_artcap : R60_ARTLIST);   /* FG = scr1 -> plane A */
    { extern volatile uint16_t r60_census[8]; uint16_t _t = r60_frt_p(); r60_census[3] = (uint16_t)(_t - r60_census[3]); r60_census[5] = _t; }
    if (!fat) {
#ifdef R60_CUTPF
        if (na == 0u && nb == 0u)                /* sw251: only in a window whose own walk shipped no art, so a scene's fill is never delayed */
            na = r60_pf_step(win, art_a, na, r60_artcap < R60_ARTLIST ? r60_artcap : R60_ARTLIST);   /* the cut's art in A's spare room; what does not fit is given back below */
#endif
        na = r60_art_fit(pa, e, 736u, art_a, na, 0u);    /* 368 longs a packet */
        nart += r60_art_tail(e, art_a, na, 0u);
        r60_pkta_words = (unsigned)(e - pa) + 1u + 17u * na;   /* exact: the publish copies only this (752 longs a pass cost ~9 lines of FB writes) */
#ifdef R60_EMPTYPKT
        a_empty = (na == 0u && (unsigned)(e - pa) == 8u + 14u) ? 0x1000u : 0u;   /* EMPTY: seven rows of nothing, no art: the 68K may skip the walk (its empty consume cost 8 lines) */
#endif
    } else {
        /* FAT WINDOW: A carries its rows, the blank slot, its art, then B's art, up to
         * R60_PKTA_CAP words. Under the display gate's hold the slave draws nothing, so
         * the 68K may take the frame: a record is ~1 line of VDP DMA, so a scene fills in
         * a few windows instead of ten (2026-10-16, the title rewrite and the cut lag). */
        unsigned used = (unsigned)(e - pa);
        unsigned avail = used + 1u + 17u >= R60_PKTA_CAP ? 0u : (R60_PKTA_CAP - used - 1u - 17u) / 17u;
        unsigned share_b;
#ifdef R60_BLANKFILL
        if (avail) avail--;                      /* the solid's record */
#endif
        /* THE BG GETS ITS SHARE (sw171): A's list took the whole budget and the eye's BG never
         * landed under the hold (plane B all blank at release). B is served first up to half. */
        share_b = nb < avail / 2u ? nb : avail / 2u;
        na = r60_art_fit_n(pa, e, avail - share_b, art_a, na, 0u);
        nb = r60_art_fit_n(pb, eb, avail > na ? avail - na : 0u, art_b, nb, 1u);
#ifdef R60_BLANKFILL
        if (r60_solid_new) r60_solid_ship = R60_SOLID0 + r60_solid_new - 1u;
#endif
        nart = r60_art_tail2(e, art_a, na, art_b, nb, 1u);
        *eb = 0u;                                /* B's tail: no records */
        r60_pkta_words = used + 1u + 17u * nart;
    }
    pa[1] = (uint16_t)(0x4001u | (R60_MDHOLD ? 0x2000u : 0u));   /* type 1, art tail, no palette; the MD hold rides BOTH packets (sw113: A without it turned the display back on until B landed ~150 lines later, fragments of the loading scene) */
#ifdef R60_EMPTYPKT
    pa[1] |= (uint16_t)a_empty;                /* written after the header, which the line above rebuilds */
#endif
    pa[2] = (uint16_t)(0x8000u | (win & 0xFFu)); /* plane A; low byte = the window sequence */
    pa[3] = hsA; pa[7] = hsB; pa[4] = vsB; pa[6] = vsA;
    pa[5] = (uint16_t)(r60_walk_rows * 64u);

    /* THE PALETTE, 64 words at offset 688 of B -> CRAM 0-63: every pen's colour read live
     * from its owning (set, pixel) so fades track; a free pen and pen 0 are black. A TILE
     * colour set is EIGHT entries (cset * 8 + pixel; MAME segaic16: color = (data >> 6) &
     * 0x7F). MD CRAM is 0000BBB0GGG0RRR0 (bake_tilecram.py:114-125). */
    for (l = 0; l < R60_LINES; l++)
        for (p = 0; p < 16u; p++) {
            uint16_t q = 0u;
            if (p && r60_lc[l * 16u + p] != 0xFFFFu)
                q = r60_quant(PAL_SH[r60_pown_s[l * 16u + p] * 8u + r60_pown_p[l * 16u + p]]);
            pb[688u + l * 16u + p] = (uint16_t)(((q >> 6) << 9) | (((q >> 3) & 7u) << 5) | ((q & 7u) << 1));
        }
    /* THE COMPARE BELOW RUNS ONCE, AFTER THE WHOLE BLOCK IS WRITTEN. Until 2026-10-17 it sat
     * inside the per-pen loop above (a missing brace): palprev was refreshed every pen, so
     * by the last pen "same" could only see pen 63, and the EMPTY bit, ORed and never
     * cleared, was set on every packet B. Under R60_EMPTYPKT the 68K then skipped every B
     * and the MD palette never moved after boot (sw151/sw152: the boot-card fade and the
     * title fade-in did not render, MD CRAM constant from frame 150). R60_PALGATE failed
     * the same way in sw12x ("gating broke MD colours, cause unknown") -- same brace. */
    {   /* SHIP THE PALETTE ONLY WHEN IT CHANGED (or every 32 windows): the 68K's CRAM DMA and
         * the publish's 64 longs are otherwise spent on identical words every pass */
        static uint16_t palprev[64]; static unsigned palwin;
        unsigned k, same = 1;
        for (k = 0; k < 64u; k++) if (pb[688u + k] != palprev[k]) { same = 0; break; }
#ifdef R60_PALGATE
        {   /* sw232: after a palette change the block rides the next 8 windows too. The 68K skips a
             * stale-sequence packet whole (sw173), and on the MiSTer a palette that rode only the
             * window of its change was lost until the 32-window resend: a green eye for a second. */
            static unsigned pal_hold;
            if (!same) { pal_hold = 8u; r60_pal_changes++; }
            else if (pal_hold) pal_hold--;
            if (same && !pal_hold && ((++palwin) & 31u)) pb[1] = (uint16_t)(pb[1] & ~0x8000u);   /* no palette this window */
        }
        else
#else
        (void)same; (void)palwin;
#endif
        { for (k = 0; k < 64u; k++) palprev[k] = pb[688u + k]; r60_pktb_words = 752u; }
#ifdef R60_EMPTYPKT
        {   /* EMPTY B: no changed cells, no art beyond the blank record, palette and scroll words as last window */
            static uint16_t scr_prev[4]; static unsigned scr_init;
            unsigned rows_empty = (unsigned)(eb - pb) == 8u + 14u, pal_same = same, scr_same;
            scr_same = scr_init && scr_prev[0] == pb[3] && scr_prev[1] == pb[7] && scr_prev[2] == pb[4] && scr_prev[3] == pb[6];
            scr_prev[0] = pb[3]; scr_prev[1] = pb[7]; scr_prev[2] = pb[4]; scr_prev[3] = pb[6]; scr_init = 1;
            if (!fat && rows_empty && nb == 0u && pal_same && scr_same) pb[1] |= 0x1000u;
        }
#endif
    }
    { extern volatile uint16_t r60_census[8]; r60_census[5] = (uint16_t)(r60_frt_p() - r60_census[5]); }
    return nart;
}

void r60_planes_reset(void)
{
    unsigned i;
    for (i = 0; i < MD_SLOTS; i++) { md_tag[i] = 0xFFFFFFFFu; md_ref[i] = 0; md_pin[i] = 0; }
    md_tag[MD_BLANK_SLOT] = 0xFFFFFFFEu;        /* never matches a key, never free */
#ifdef R60_BLANKFILL
    r60_solid_reset();
#endif
    for (i = 0; i < 4096u; i++) r60_nt_shadow[0][i] = 0xFFFFu;   /* both planes: everything differs */
    for (i = 0; i < 32u; i++) md_dirty[i] = 0;
    for (i = 0; i < NSETS; i++) { r60_set_line[i] = 0; r60_set_bg[i] = 0; r60_mapid[i] = (uint8_t)i; }
    for (i = 0; i < R60_LINES * 16u; i++) r60_lc[i] = 0xFFFFu;
    for (i = 0; i < NSETS * 8u; i++) r60_smap[i] = 0;
    for (i = 0; i < NSETS; i++) { r60_row_baked[i] = 0; r60_set_degraded[i] = 0; }   /* every row zero, every tag free */
#ifdef R60_IDPENS
    for (i = 0; i < NSETS; i++) r60_set_id[i] = 0;
    r60_id_open = 0; r60_id_openwin = 0;
#endif
}

/* THE LINE PLAN, per scene: pack the sets by cell count (the BG page weighted double),
 * largest first, so the lines go to what covers the screen and not to whatever the first
 * window's rows held (sw13: 395,198 cells with content, 7,617 with a line). Costs 4,096
 * reads, run by the master every 32 windows. The new pack is built in a scratch copy and
 * adopted only if a resident set would change line or map; adoption flushes the slot
 * cache (a wiped tag leaves cells naming the next claimant's art, so the shadow is
 * invalidated with it: m_main.c:3664-3687). */
#if defined(R60_PENIDX) || !defined(R60_NORECOLOUR)   /* sw283: the recolour/split pass and its state compile on the line too */
__attribute__((noinline)) static void r60_remap_id(unsigned s)   /* cart-resident (sw283: .ramtext is full) */
{   /* the map id: the first set with this line and map (as r60_pack_set derives it) */
    unsigned t, p;
    r60_mapid[s] = (uint8_t)s;
    for (t = 0; t < s; t++) {
        if (!r60_set_line[t] || r60_set_line[t] != r60_set_line[s]) continue;
        for (p = 0; p < 8u; p++) if (r60_smap[t * 8u + p] != r60_smap[s * 8u + p]) break;
        if (p == 8u) { r60_mapid[s] = r60_mapid[t]; break; }
    }
}
/* A SLOT'S ART IS A FUNCTION OF ITS MAP ID'S ROW (2026-10-25, the green sky). r60_art bakes a slot
 * keyed on map id m through r60_smap[m], so a slot is correct exactly while that row is the row it
 * was baked with. Every path that rewrote a row used to carry its own re-mark, and three did not:
 * a set unlined (the recolour's many-owner split, an identity family drop-out) re-packs with
 * r60_mapid = itself and a NEW row, and its key then hit its own old slots. Level 1's sky (BG set
 * 92) re-packed as an identity family on pens 8-15 while its 14 resident slots (tags
 * 0x5c9700-0x5c970e) held art baked on pens 1-5: CRAM right, VRAM stale, a green sky at the round
 * clear on the tapeLR run for ~1000 frames (f39140-40160). So the rule is enforced in one place: the
 * row each map id's slots were baked with is kept, and any id whose row differs has its slots
 * re-marked; a resident set whose map id no longer carries its row re-derives one. Called after
 * every path that packs or moves pens (r60_line_for's slow path, the recolour pass, the plan). */
static uint32_t r60_row(unsigned s)
{
    const uint8_t *m = r60_smap + s * 8u;
    return ((uint32_t)m[0] << 28) | ((uint32_t)m[1] << 24) | ((uint32_t)m[2] << 20) | ((uint32_t)m[3] << 16)
         | ((uint32_t)m[4] << 12) | ((uint32_t)m[5] << 8) | ((uint32_t)m[6] << 4) | (uint32_t)m[7];
}
volatile unsigned r60_sync_stats[2];             /* [0] map ids whose row moved [1] slots re-marked (probe, read by symbol) */
__attribute__((noinline)) void r60_map_sync(void)
{
    uint8_t hit[NSETS];
    unsigned s, i, any = 0;
    for (s = 0; s < NSETS; s++) {
        uint32_t r = r60_row(s);
        hit[s] = 0;
        if (r != r60_row_baked[s]) { r60_row_baked[s] = r; hit[s] = 1; any = 1; r60_sync_stats[0]++; }
    }
    for (s = 0; s < NSETS; s++)
        if (r60_set_line[s] && r60_mapid[s] != s && r60_row(s) != r60_row(r60_mapid[s])) r60_remap_id(s);
    if (!any) return;
    for (i = 1; i < MD_SLOTS; i++) {
        uint32_t tg = md_tag[i];
#ifdef R60_BLANKFILL
        if (i >= R60_SOLID0) break;              /* solids carry their pen, not a map */
#endif
        if (tg >= 0xFFFFFFFEu) continue;
        if (hit[(tg >> 16) & 0x7Fu]) { MD_MARK(i); r60_sync_stats[1]++; }
    }
}
/* RECOLOUR IN PLACE, every 4th window. A pen whose owners all moved to one colour takes it (the
 * title logo's white->red: a CRAM write). Owners that DISAGREE are split only once the keeper,
 * the steadiest of them, has held its colour R60_PIX_SETTLE windows: a fade-in starts every set
 * as one flat colour (the logo shared the water's pens, sw124), and splitting mid-fade re-keyed
 * sets every window (sw126: the eye scene went black). After the split the pixels that left the
 * keeper's colour own new pens and their sets' slots are re-marked (r60_map_sync); their tiles
 * re-bake over five windows. */
#define R60_OWNCAP 4u                                   /* owners tracked per pen; more = the full scan below */
__attribute__((noinline)) void r60_recolour_pens(void)   /* cart-resident, once a window (sw283); noinline or LTO folds it back into .ramtext */
{
    uint8_t (*own_s)[R60_OWNCAP] = (uint8_t (*)[R60_OWNCAP])0x06029000u, (*own_p)[R60_OWNCAP] = (uint8_t (*)[R60_OWNCAP])0x06029100u, *own_n = (uint8_t *)0x06029380u;   /* 256 + 256 + 64 B in the 0x29000 scratch */
    unsigned l, i, s, p, k;
    r60_pen_stats[0]++;
    /* 1. stability: per (set, pixel), r60_pix_track, every window */
    /* 2. who uses each pen */
    for (i = 0; i < R60_LINES * 16u; i++) own_n[i] = 0;
    for (s = 0; s < NSETS; s++) {
        if (!r60_set_line[s]) continue;
        l = r60_set_line[s] - 1u;
        for (p = 0; p < 8u; p++) {
            unsigned pen = r60_smap[s * 8u + p];
            if (!pen || (!p && !r60_set_bg[s])) continue;
            i = l * 16u + pen;
            if (own_n[i] < R60_OWNCAP) { own_s[i][own_n[i]] = (uint8_t)s; own_p[i][own_n[i]] = (uint8_t)p; own_n[i]++; }
            else { own_n[i] = 0xFFu; r60_pen_stats[4]++; }   /* too many: the full scan below */
        }
    }
    /* 3. agree -> recolour; disagree with a settled keeper -> split. STABILITY IS PER (SET, PIXEL)
     * (r60_pix_track): the keeper is the recorded owner while it is settled, else the owner whose
     * colour has held longest, and the pen takes the keeper's colour and source at once, so a steady pixel stops
     * showing a cycling one's colour on the first pass that sees them disagree. Nobody settled =
     * a fade: wait (sw126). Movers go to a free pen, or a settled pixel to a settled owner's pen
     * of its colour (r60_claim_split); with neither they stay. */
    for (i = 0; i < R60_LINES * 16u; i++) {
        unsigned n = own_n[i], q0, ok = 1, kk, qk;
        if (i == 20u) { r60_pen_dbg[0] = n; r60_pen_dbg[1] = r60_lc[i]; }
        if (!n || r60_lc[i] == 0xFFFFu) continue;
        l = i >> 4;
        if (n == 0xFFu) {
            /* MORE OWNERS THAN THE TABLE HOLDS (sw283): scan every (set, index) on this pen in full.
             * All agreeing -> the pen takes their colour (the title's water, many sets one pen).
             * Disagreeing -> every set that differs from the keeper is UNLINED, so its cells
             * go unlined and the walk re-packs it with its real colours, once its differing pixels
             * have settled or the keeper has (2026-10-25: per pixel; the per-set rule never split a
             * set that cycles one pixel and holds another). A cycling recorded owner first hands
             * the pen to the steadiest user. The crystal-ball picture (Mike's sw282.bs3): sets
             * 73-76, 28 pairs on one pen, seven distinct purples each, never split because the old
             * 12-owner cap gave up. */
            unsigned ks = r60_pown_s[i], kp = r60_pown_p[i], pen = i & 15u, qk2, agree = 1, ksettled;
            if (!R60_PIX_SETTLED(ks, kp)) {
                unsigned bage = R60_PIX_AGE(ks, kp);
                for (s = 0; s < NSETS; s++) {
                    if (!r60_set_line[s] || r60_set_line[s] - 1u != l) continue;
                    for (p = 0; p < 8u; p++)
                        if (r60_smap[s * 8u + p] == pen && (p || r60_set_bg[s]) && R60_PIX_AGE(s, p) > bage) { bage = R60_PIX_AGE(s, p); ks = s; kp = p; }
                }
                r60_pown_s[i] = (uint8_t)ks; r60_pown_p[i] = (uint8_t)kp;
            }
            qk2 = r60_quant(PAL_SH[ks * 8u + kp]); ksettled = R60_PIX_SETTLED(ks, kp);
            for (s = 0; s < NSETS && agree; s++) {
                if (!r60_set_line[s] || r60_set_line[s] - 1u != l) continue;
                for (p = 0; p < 8u; p++) if (r60_smap[s * 8u + p] == pen && (p || r60_set_bg[s]) && r60_quant(PAL_SH[s * 8u + p]) != qk2) { agree = 0; break; }
            }
            if (qk2 != r60_lc[i]) { r60_lc[i] = (uint16_t)qk2; r60_pen_stats[1]++; }
            if (agree) continue;
            for (s = 0; s < NSETS; s++) {
                unsigned dis = 0, dset = 1;
                if (!r60_set_line[s] || r60_set_line[s] - 1u != l) continue;
                for (p = 0; p < 8u; p++)
                    if (r60_smap[s * 8u + p] == pen && (p || r60_set_bg[s]) && r60_quant(PAL_SH[s * 8u + p]) != qk2) { dis = 1; if (!R60_PIX_SETTLED(s, p)) dset = 0; }
                if (dis && (dset || ksettled)) { r60_set_line[s] = 0; r60_set_bg[s] = 0; r60_mapid[s] = (uint8_t)s; R60_DIAG2[13]++; }
            }
            continue;
        }
        q0 = r60_quant(PAL_SH[own_s[i][0] * 8u + own_p[i][0]]);
        for (k = 1; k < n; k++) if (r60_quant(PAL_SH[own_s[i][k] * 8u + own_p[i][k]]) != q0) { ok = 0; break; }
        kk = n;
        for (k = 0; k < n; k++) if (own_s[i][k] == r60_pown_s[i] && own_p[i][k] == r60_pown_p[i]) { kk = k; break; }
        if (kk == n || !R60_PIX_SETTLED(own_s[i][kk], own_p[i][kk])) {   /* a settled recorded owner keeps the pen (a nearest arrival never takes it) */
            if (kk == n) kk = 0;
            for (k = 0; k < n; k++) if (R60_PIX_AGE(own_s[i][k], own_p[i][k]) > R60_PIX_AGE(own_s[i][kk], own_p[i][kk])) kk = k;
        }
        r60_pown_s[i] = own_s[i][kk]; r60_pown_p[i] = own_p[i][kk];   /* the live colour source: the steadiest user */
        if (i == 20u) { r60_pen_dbg[2] = q0; r60_pen_dbg[3] = ok; r60_pen_dbg[4] = own_s[i][0]; r60_pen_dbg[5] = own_p[i][0]; r60_pen_dbg[6] = n > 1u ? own_s[i][1] : 0xFFu; r60_pen_dbg[7] = n > 1u ? own_p[i][1] : 0xFFu; r60_pen_dbg[8] = n > 1u ? r60_quant(PAL_SH[own_s[i][1] * 8u + own_p[i][1]]) : 0xFFFFu; r60_pen_dbg[9] = R60_PIX_AGE(own_s[i][kk], own_p[i][kk]); }
        if (ok) { if (q0 != r60_lc[i]) { r60_lc[i] = (uint16_t)q0; r60_pen_stats[1]++; } continue; }
        qk = r60_quant(PAL_SH[own_s[i][kk] * 8u + own_p[i][kk]]);
        if (!R60_PIX_SETTLED(own_s[i][kk], own_p[i][kk])) { r60_pen_stats[5]++; continue; }
        r60_lc[i] = (uint16_t)qk; r60_pen_stats[2]++;
        for (k = 0; k < n; k++) {
            unsigned t = own_s[i][k], tp = own_p[i][k], qn = r60_quant(PAL_SH[t * 8u + tp]), np;
            if (qn == qk) continue;
            np = r60_claim_split(l, (uint16_t)qn, t, tp);
            if (np && np != (i & 15u)) { r60_smap[t * 8u + tp] = (uint8_t)np; r60_pen_stats[3]++; }
        }
    }
    /* 4. PEN GARBAGE (2026-10-25): a pen no resident set's map names (its sets were unlined by the
     * split above, a family drop-out or the degraded repair, and re-packed elsewhere) stayed taken
     * until the next adopted plan. Freed after four idle passes (16 windows, three walk rotations),
     * by which time every visible cell of a re-packed set names its new pens. */
    {
        static uint8_t idle[R60_LINES * 16u];
        for (i = 0; i < R60_LINES * 16u; i++) {
            if (!(i & 15u) || r60_lc[i] == 0xFFFFu || own_n[i]) { idle[i] = 0; continue; }
            if (++idle[i] >= 4u) { r60_lc[i] = 0xFFFFu; idle[i] = 0; r60_pen_stats[7]++; }
        }
    }
#ifdef R60_DROPSTALE
    /* 5. THE DEGRADED REPAIR (2026-10-25, the eye's pink skin). A set that packed while stale sets
     * held the lines took nearest pens (the eye skin onto demo 1's pinks and greens). The eviction
     * in r60_line_for fires only when a set packs AFTER a plan marked the stale sets; when the
     * skin packed first, nothing ever re-packed it. Once stale sets are known they are dropped,
     * and every degraded resident set is unlined, so the walk re-packs it on its next visit with
     * the freed pens; its old pens go to the garbage pass above. */
    if (r60_stale_any) {
        unsigned deg = 0;
        for (s = 0; s < NSETS; s++) if (r60_set_degraded[s] && r60_set_line[s] && !r60_set_stale[s]) deg = 1;
        if (deg) {
            r60_drop_stale();
            for (s = 0; s < NSETS; s++)
                if (r60_set_degraded[s] && r60_set_line[s]) {
                    r60_set_line[s] = 0; r60_set_bg[s] = 0; r60_mapid[s] = (uint8_t)s; r60_set_degraded[s] = 0;
#ifdef R60_IDPENS
                    r60_set_id[s] = 0;
#endif
                    r60_pen_stats[6]++;
                }
        }
    }
#endif
    /* a moved pixel's set changed its map: r60_map_sync re-marks the slots baked with it */
    r60_map_sync();
}
#endif

/* DROP THE SETS OFF EVERY PAGE (2026-10-24, the attract's eye). A plan that adopts nothing kept
 * the whole old pack, so a set that left every page kept its pens for good: demo 1's sets 74-86
 * and 92-101 still filled all four lines at the eye (arcade f1186), and the eye's skin (sets
 * 22-32, browns 0x16E..0x04B) fell to the nearest pens -- pink and level greens. The plan marks
 * those sets STALE; a set that then packs on demand and does not fit drops them all. Only under
 * pressure: dropping at every plan threw away set 20's cached art, and the face (f1054) re-shipped
 * it for ten frames of solid blocks. Dropping a set moves no resident set, so it flushes nothing: its pens go free unless a resident set shares
 * them (the owner then passes to that set, the live colour source), and its slots are freed or
 * re-keyed to the resident representative of their map. */
volatile unsigned r60_stale_dropped;             /* sets dropped (probe, read by symbol) */
#if defined(R60_PENIDX) || !defined(R60_NORECOLOUR)
static void r60_drop_stale(void)
{
    uint8_t oldid[NSETS], trans[NSETS], drop[NSETS];
    uint8_t us[R60_LINES * 16], up[R60_LINES * 16], used[R60_LINES * 16];
    unsigned s, p, i, any = 0;
    for (s = 0; s < NSETS; s++) {
        drop[s] = (uint8_t)(r60_set_line[s] && r60_set_line[s] <= R60_LINES && r60_set_stale[s]);
        r60_set_stale[s] = 0;
        any |= drop[s];
        oldid[s] = r60_mapid[s];
        trans[s] = 0xFFu;
    }
    r60_stale_any = 0;
    if (!any) return;
    for (s = 0; s < NSETS; s++) {
        if (!drop[s]) continue;
        r60_set_line[s] = 0; r60_set_bg[s] = 0; r60_mapid[s] = (uint8_t)s;
        for (p = 0; p < 8u; p++) r60_smap[s * 8u + p] = 0;
#ifdef R60_IDPENS
        r60_set_id[s] = 0;
        if (r60_id_open == s + 1u) { r60_id_open = 0; r60_id_openwin = 0; }
#endif
        r60_stale_dropped++;
    }
    /* pens: free the ones no resident set names; a shared pen passes to a resident user,
     * preferring one whose colour is the pen's */
    for (i = 0; i < R60_LINES * 16u; i++) used[i] = 0;
    for (s = 0; s < NSETS; s++) {
        unsigned l;
        if (!r60_set_line[s] || r60_set_line[s] > R60_LINES) continue;
        l = r60_set_line[s] - 1u;
        for (p = 0; p < 8u; p++) {
            unsigned pen = r60_smap[s * 8u + p];
            if (!pen || (!p && !r60_set_bg[s])) continue;
            i = l * 16u + pen;
            if (!used[i] || (r60_quant(PAL_SH[s * 8u + p]) == r60_lc[i] && r60_quant(PAL_SH[us[i] * 8u + up[i]]) != r60_lc[i])) {
                us[i] = (uint8_t)s; up[i] = (uint8_t)p;
            }
            used[i] = 1;
        }
    }
    for (i = 0; i < R60_LINES * 16u; i++) {
        if (r60_lc[i] == 0xFFFFu || !(i & 15u)) continue;
        if (!used[i]) { r60_lc[i] = 0xFFFFu; continue; }
        if (drop[r60_pown_s[i]]) { r60_pown_s[i] = us[i]; r60_pown_p[i] = up[i]; }
    }
    /* map ids: a resident set whose representative was dropped takes a resident one; a slot
     * keyed on a map id no resident set carries is freed (its pins keep the art until its
     * cells move, as the plan's re-mark does) */
    for (s = 0; s < NSETS; s++) if (r60_set_line[s]) { r60_remap_id(s); trans[oldid[s]] = r60_mapid[s]; }
    for (i = 1; i < MD_SLOTS; i++) {
        uint32_t tg = md_tag[i];
        unsigned mid;
#ifdef R60_BLANKFILL
        if (i >= R60_SOLID0) break;
#endif
        if (tg >= 0xFFFFFFFEu) continue;
        mid = (tg >> 16) & 0x7Fu;
        if (trans[mid] == 0xFFu) {
            md_tag[i] = 0xFFFFFFFFu;
#ifdef R60_KEEPSTALE
            md_reused[i >> 5] |= 1u << (i & 31u);   /* a stale cell naming it must not show the next claimant */
#endif
        } else if (trans[mid] != mid) md_tag[i] = (tg & 0x0000FFFFu) | ((uint32_t)trans[mid] << 16);
    }
}
#endif

unsigned r60_plan_soft, r60_plan_refused;        /* sw183: 1 = refuse a plan that moves resident lines; the count refused */
unsigned r60_unlined;                            /* sw276: visible cells the walk skipped because their colour set has no line (counted per window, read by the plan scheduler) */
unsigned r60_plan_adopted, r60_pal_changes;   /* r60_pal_changes: palette blocks that differed from the last (probe, read by symbol) */                       /* sw246: 1 after a plan re-slotted (every cell re-sends); the display gate reads and clears it */
void r60_line_plan(void)   /* ROM: runs every 32 windows (sw165: .ramtext is full) */
{
    static uint16_t hist[NSETS];
#ifdef R60_KEEPSTALE
    { unsigned z; for (z = 0; z < MD_SLOTS / 32u + 1u; z++) md_reused[z] = 0; }   /* a plan period: reuse marks start afresh */
#endif
    static uint8_t  bgu[NSETS];
    static uint16_t lc0[R60_LINES * 16];
    static uint8_t  ps0[R60_LINES * 16], pp0[R60_LINES * 16], map0[NSETS * 8], line0[NSETS], bg0[NSETS], mid0[NSETS], deg0[NSETS];
    unsigned i, pg, changed = 0;
    for (i = 0; i < NSETS; i++) { hist[i] = 0; bgu[i] = 0; }
#ifdef R60_PLANFAST
    /* THE SAME CENSUS, EACH PAGE READ ONCE (R60PLANFAST, 2026-10-24): a page named by k FG quadrants and
     * j BG quadrants adds k + 2j per cell, and marks bgu when j > 0; longword reads, a zero pair skipped.
     * The 8 x 2048-word census was most of the every-32-window soft plan, a 400-line master stall in
     * the fight (sw313+ timeline, 655). Same hist, same bgu. */
    {   uint8_t kfg[16], kbg[16];
        unsigned q;
        for (q = 0; q < 16u; q++) { kfg[q] = 0; kbg[q] = 0; }
        for (pg = 0; pg < 2u; pg++) {
            uint16_t psel = TEXT_U[0x740u + pg];
            for (q = 0; q < 4u; q++) {
                unsigned page = (psel >> (q * 4u)) & 0xFu;
                if (page > 12u) continue;
                if (pg) kbg[page]++; else kfg[page]++;
            }
        }
        for (q = 0; q < 16u; q++) {
            unsigned inc = (unsigned)kfg[q] + 2u * (unsigned)kbg[q], bg = kbg[q] != 0u;
            const volatile uint32_t *pw;
            if (!inc) continue;
            pw = (const volatile uint32_t *)(TILEMAP_U + q * 0x800u);
            for (i = 0; i < 1024u; i++) {
                uint32_t v = pw[i];
                if (!v) continue;
                if (v >> 16) { unsigned k = (v >> 22) & 0x7Fu; hist[k] = (uint16_t)(hist[k] + inc); if (bg) bgu[k] = 1u; }
                if (v & 0xFFFFu) { unsigned k = (v >> 6) & 0x7Fu; hist[k] = (uint16_t)(hist[k] + inc); if (bg) bgu[k] = 1u; }
            }
        }
    }
    if (0)
#endif
    for (pg = 0; pg < 2u; pg++) {                /* pg 0 = FG (0x740), 1 = BG (0x741) */
        uint16_t psel = TEXT_U[0x740u + pg];
        unsigned q;
        for (q = 0; q < 4u; q++) {               /* all four quadrant pages of the plane */
            unsigned page = (psel >> (q * 4u)) & 0xFu;
            const volatile uint16_t *pp;
            if (page > 12u) continue;
            pp = TILEMAP_U + page * 0x800u;
            for (i = 0; i < 2048u; i++) {
                uint16_t w = pp[i];
                if (w) { hist[((unsigned)w >> 6) & 0x7Fu] += pg ? 2u : 1u; if (pg) bgu[((unsigned)w >> 6) & 0x7Fu] = 1u; }
            }
        }
    }
    /* keep the current pack aside, pack afresh */
    for (i = 0; i < R60_LINES * 16u; i++) { lc0[i] = r60_lc[i]; ps0[i] = r60_pown_s[i]; pp0[i] = r60_pown_p[i]; r60_lc[i] = 0xFFFFu; }
    for (i = 0; i < NSETS * 8u; i++) { map0[i] = r60_smap[i]; r60_smap[i] = 0; }
    for (i = 0; i < NSETS; i++) { line0[i] = r60_set_line[i]; bg0[i] = r60_set_bg[i]; mid0[i] = r60_mapid[i]; deg0[i] = r60_set_degraded[i]; r60_set_line[i] = 0; r60_set_bg[i] = 0; r60_mapid[i] = (uint8_t)i; }
#ifdef R60_IDPENS
    for (i = 0; i < NSETS; i++) {                /* identity sets keep their pens exactly: their art must not re-bake */
        unsigned p, l;
        if (!r60_set_id[i]) continue;
        if (!line0[i] || !hist[i]) { r60_set_id[i] = 0; continue; }
        l = line0[i] - 1u;
        for (p = 0; p < 8u; p++) {
            unsigned pen = map0[i * 8u + p];
            if (!pen) continue;
            r60_lc[l * 16u + pen] = lc0[l * 16u + pen]; r60_pown_s[l * 16u + pen] = ps0[l * 16u + pen]; r60_pown_p[l * 16u + pen] = pp0[l * 16u + pen];
            r60_smap[i * 8u + p] = (uint8_t)pen;
        }
        r60_set_line[i] = line0[i]; r60_set_bg[i] = bg0[i]; r60_mapid[i] = mid0[i];
    }
#endif
#ifdef R60_PENIDX
    for (;;) {   /* pass 1: resident sets keep their line when they still fit it (a palette change must not move lines) */
        unsigned best = 0, bi = 0xFFu;
        for (i = 0; i < NSETS; i++) if (!r60_set_line[i] && line0[i] && hist[i] > best) { best = hist[i]; bi = i; }
        if (bi == 0xFFu) break;
        if (!r60_pack_set_on(bi, bgu[bi] | bg0[bi], line0[bi] - 1u)) r60_set_line[bi] = 0xFEu;   /* did not fit: pass 2 */
    }
    for (i = 0; i < NSETS; i++) if (r60_set_line[i] == 0xFEu) r60_set_line[i] = 0;
#endif
    for (;;) {
        unsigned best = 0, bi = 0xFFu;
        for (i = 0; i < NSETS; i++) if (!r60_set_line[i] && hist[i] > best) { best = hist[i]; bi = i; }
        if (bi == 0xFFu) break;
        r60_pack_set(bi, bgu[bi]);
    }
    /* a resident set that moved line or map changes its art: flush. A set the old pack
     * held and this one does not (off every page now) is no change. */
    for (i = 0; i < NSETS && !changed; i++) {
        unsigned p;
#ifdef R60_PLANNEW
        /* A NEW SCENE UNDER THE HOLD IS A CHANGE (R60PLANNEW, 2026-10-24, the green eye). The test
         * below sees only sets resident in BOTH packs, so a cut whose pages and palette land before
         * the walk has packed any of the new sets reads "no change": the old pack is restored, the
         * new sets pack on demand into the dead scene's free pens, sharing pens by colour with sets
         * the game is still cycling, and with the display up every later plan is soft and refused.
         * Whether the walk got there first was a race against the master's speed: PORTHOT2 ran the
         * face->eye plans at f1166/1167 before any eye set was resident (both restored, green skin
         * for the whole scene); the slower line packed eleven eye sets between its plans and the
         * since==1 plan adopted. Behind the blank a fresh pack costs nothing visible. */
        if (!line0[i] && r60_set_line[i] && R60_DISPHOLD_W && !r60_plan_soft) { changed = 1; break; }
#endif
        if (!line0[i] || !r60_set_line[i]) continue;
        if (line0[i] != r60_set_line[i] || (bg0[i] && !r60_set_bg[i])) { changed = 1; break; }
#ifdef R60_PENIDX
        (void)p;   /* pen POSITIONS differ between an incremental pack and a fresh one by order alone; the
                    * resident maps stay (the recolour pass keeps their colours current), only a line move flushes */
#else
        for (p = 0; p < 8u; p++) if (map0[i * 8u + p] != r60_smap[i * 8u + p]) { changed = 1; break; }
#endif
    }
    if (changed && r60_plan_soft) { r60_plan_refused++; changed = 0; }   /* SOFT PLAN (sw183): the display is up, and adopting means every cell re-sent with new line
                                                                          * bits over a rotation while CRAM still holds the old lines -- sw182 eye pan f1392-1401,
                                                                          * 29,364 pixels changed in one frame, blue iris. New sets still pack on demand; the
                                                                          * re-balance waits for the next hold, where the plan runs blanked. */
    if (!changed) {                              /* keep the old pack: its maps are what the slots were baked with */
        for (i = 0; i < R60_LINES * 16u; i++) { r60_lc[i] = lc0[i]; r60_pown_s[i] = ps0[i]; r60_pown_p[i] = pp0[i]; }
        for (i = 0; i < NSETS * 8u; i++) r60_smap[i] = map0[i];
        for (i = 0; i < NSETS; i++) { r60_set_line[i] = line0[i]; r60_set_bg[i] = bg0[i]; r60_mapid[i] = mid0[i]; r60_set_degraded[i] = deg0[i]; }
#ifdef R60_DROPSTALE
        for (i = 0; i < NSETS; i++) {            /* mark, do not drop: the next set that does not fit evicts them */
            r60_set_stale[i] = (uint8_t)(r60_set_line[i] && !hist[i]);
            r60_stale_any |= r60_set_stale[i];
        }
#endif
        R60_MAP_SYNC();
        return;
    }
    r60_plan_adopted = 1;                      /* past the no-change exit: this plan re-slots */
#ifdef R60_DROPSTALE
    { unsigned z; for (z = 0; z < NSETS; z++) r60_set_stale[z] = 0; r60_stale_any = 0; }   /* the fresh pack holds no stale set */
#endif
    R60_DIAG[10] += 0x10000u;                    /* plans adopted (high 16) */
#ifdef R60_PLANREMARK
    /* RE-MARK, DO NOT WIPE (m_main.c:3664-3687). A slot keeps its tag and its cells; one
     * whose set's map or line moved goes dirty and is re-baked on its next visit; a slot
     * whose set left every page is freed (pins keep its art until its cells move). The
     * whole shadow is invalidated so every cell is re-sent with its new line bits within
     * one rotation (five windows): sw71 re-marked without that and the eye kept stale
     * line bits for good (luma 30 against 9). A wipe re-shipped the whole screen at
     * every plan (the face transition in Mike's capture, f1045: 775 stale cells). */
    for (i = 1; i < MD_SLOTS; i++) {
        uint32_t tg = md_tag[i];
        unsigned mid, p, diff = 0;
        if (tg >= 0xFFFFFFFEu) continue;
        mid = (tg >> 16) & 0x7Fu;
        if (!r60_set_line[mid]) { md_tag[i] = 0xFFFFFFFFu; continue; }
        if (line0[mid] != r60_set_line[mid]) diff = 1;
        for (p = 0; p < 8u && !diff; p++) if (map0[mid * 8u + p] != r60_smap[mid * 8u + p]) diff = 1;
        if (diff) MD_MARK(i);
        /* the key's map id follows the new pack's representative, or every hit misses
         * (sw110 first cut: 1,176 holes a plane, the whole cache pinned and unclaimable) */
        md_tag[i] = (tg & 0x0000FFFFu) | ((uint32_t)r60_mapid[mid] << 16);
    }
    for (i = 0; i < 4096u; i++) {                /* release the pins the old cells held, then every cell re-sends */
        unsigned os = r60_nt_shadow[0][i] & 0x3FFu;
        if (r60_nt_shadow[0][i] != 0xFFFFu) MD_UNPIN(os);
        r60_nt_shadow[0][i] = 0xFFFFu;
    }
#else
    for (i = 0; i < MD_SLOTS; i++) { md_tag[i] = 0xFFFFFFFFu; md_ref[i] = 0; md_pin[i] = 0; }
    md_tag[MD_BLANK_SLOT] = 0xFFFFFFFEu;
#ifdef R60_BLANKFILL
    r60_solid_reset();
#endif
    for (i = 0; i < 32u; i++) md_dirty[i] = 0;
    for (i = 0; i < 4096u; i++) r60_nt_shadow[0][i] = 0xFFFFu;   /* every cell re-sent with its new slot */
#endif
    R60_MAP_SYNC();
}

/* STEP B: THE 32X PAINTS THE PLANES UNDER OUR HOLD. The canvas is world-anchored: canvas
 * x = world x - (anchor - 64), screen x = canvas x - k with k = xs_now - anchor + 64, so a
 * tile at world column wc sits at canvas x = wc * 8 - anchor + 64 for the life of the
 * anchor and the line table does the scrolling. r60_fill_cells paints every tile of
 * both planes whose canvas x-range meets [cx0, cx1) for screen rows [row0, row1): the
 * band of a first paint, the column that scrolls in, the cells under a cleared sprite
 * box. BG opaque through the direct image, FG through the overwrite image. Colours: the
 * four MD lines in 32X CRAM sets 7, 11, 15, 10 (no sampled frame's sprites used them),
 * loaded from the packet's palette block by r60_fill_cram. A full-screen paint is dead
 * at 2+ vints (sw87); a band of 7 rows is ~0.4 vint, a column ~0.03. */
#define FB_W           320u
#define FB_H           224u
#define UNC(p)         ((const volatile uint8_t *)((uint32_t)(p) | 0x20000000u))
static const uint8_t r60_fill_set[4] = { 7u, 11u, 15u, 10u };
R60CODE void r60_fill_cram(const volatile uint16_t *palblk)
{
    volatile uint16_t *cram = (volatile uint16_t *)0x20004200u;
    unsigned l, p;
    for (l = 0; l < 4u; l++)
        for (p = 0; p < 16u; p++) {
            unsigned w = palblk[l * 16u + p], r3 = (w >> 1) & 7u, g3 = (w >> 5) & 7u, b3 = (w >> 9) & 7u;
            cram[r60_fill_set[l] * 16u + p] = (uint16_t)(((b3 << 2 | b3 >> 1) << 10) | ((g3 << 2 | g3 >> 1) << 5) | (r3 << 2 | r3 >> 1));
        }
}
R60COLD void r60_fill_cells(volatile uint8_t *fbdir, volatile uint8_t *fbovr,
                            unsigned anchor, unsigned cx0, unsigned cx1, unsigned row0, unsigned row1)
{
    unsigned bank1 = r60_tile_bank1();
    const volatile uint8_t *smap = UNC(r60_smap), *sline = UNC(r60_set_line), *mid = UNC(r60_mapid);
    unsigned which;
    if (cx1 <= cx0 || row1 <= row0) return;
    for (which = 1u; which + 1u >= 1u && which < 2u; which--) {   /* 1 = BG (scr2), then 0 = FG (scr1) */
        uint16_t psel = TEXT_U[0x740u + which];
        unsigned ys = TEXT_U[0x748u + which] & 0x1FFu;
        unsigned wr0 = ys >> 3, fy = ys & 7u;
        int wbase = (int)anchor - 64;                /* world x of canvas x 0 */
        int wc_lo = ((int)cx0 + wbase) >> 3, wc_hi = ((int)cx1 - 1 + wbase) >> 3;
        unsigned r;
        int wc;
        for (r = row0; r < row1 + 1u && r < 29u; r++) {   /* +1: the fine scroll shows a 29th row */
            int y0 = (int)(r * 8u) - (int)fy;
            unsigned wr = wr0 + r;
            for (wc = wc_lo; wc <= wc_hi; wc++) {
                int cx = wc * 8 - wbase;             /* canvas x of this tile */
                unsigned q = (((unsigned)wc >> 6) & 1u) | (((wr >> 5) & 1u) << 1);
                unsigned page = (psel >> (q * 4u)) & 0xFu;
                uint16_t w = page > 12u ? 0u : TILEMAP_U[page * 0x800u + (wr & 31u) * 64u + ((unsigned)wc & 63u)];
                unsigned code = r60_tile_index(w, bank1), cset = ((unsigned)w >> 6) & 0x7Fu, line, base, yy, k8;
                const uint8_t *px;
                const volatile uint8_t *map;
                unsigned pen8[8];
                if (!w) continue;
                line = sline[cset];
                if (!line) continue;
                if (cx < 0) continue;
                map = smap + (unsigned)mid[cset] * 8u;
                base = r60_fill_set[line - 1u] * 16u;
                px = altbeast_tiles + code * 64u;
                for (k8 = 0; k8 < 8u; k8++) { unsigned m = map[k8]; pen8[k8] = which ? (m ? base + m : r60_fill_set[0] * 16u) : (k8 ? base + m : 0u); }   /* BG pixel 0 with no pen: opaque black (set 7 entry 0 = line 0 pen 0 = black), never through to the MD */
                for (yy = 0; yy < 8u; yy++) {
                    int y = y0 + (int)yy;
                    const uint8_t *rw = px + yy * 8u;
                    uint32_t a, b, v0, v1;
                    unsigned off;
                    if (y < 0 || y >= (int)FB_H) continue;
                    a = ((const uint32_t *)rw)[0]; b = ((const uint32_t *)rw)[1];
                    v0 = (pen8[(a >> 24) & 7u] << 24) | (pen8[(a >> 16) & 7u] << 16) | (pen8[(a >> 8) & 7u] << 8) | pen8[a & 7u];
                    v1 = (pen8[(b >> 24) & 7u] << 24) | (pen8[(b >> 16) & 7u] << 16) | (pen8[(b >> 8) & 7u] << 8) | pen8[b & 7u];
                    off = (unsigned)y * FB_W + (unsigned)cx;
                    if (!(cx & 3)) {
                        volatile uint32_t *d = (volatile uint32_t *)((which ? fbdir : fbovr) + off);
                        d[0] = v0; d[1] = v1;
                    } else if (!(cx & 1)) {
                        volatile uint16_t *d = (volatile uint16_t *)((which ? fbdir : fbovr) + off);
                        d[0] = (uint16_t)(v0 >> 16); d[1] = (uint16_t)v0; d[2] = (uint16_t)(v1 >> 16); d[3] = (uint16_t)v1;
                    } else {
                        volatile uint8_t *d = (which ? fbdir : fbovr) + off;
                        d[0] = (uint8_t)(v0 >> 24); d[1] = (uint8_t)(v0 >> 16); d[2] = (uint8_t)(v0 >> 8); d[3] = (uint8_t)v0;
                        d[4] = (uint8_t)(v1 >> 24); d[5] = (uint8_t)(v1 >> 16); d[6] = (uint8_t)(v1 >> 8); d[7] = (uint8_t)v1;
                    }
                }
            }
        }
    }
}

/* the MD CRAM word of line l pen p as the palette block carries it (the commit paints the
 * 32X fill sets from this under mode 2) */
R60CODE uint16_t r60_fill_mdword(unsigned l, unsigned p)
{
    /* through the uncached alias: the commit may run on the other CPU, whose cached copy
     * of these tables is stale (sw96: entry 177 read 29,27,5 for a pen whose colour was
     * 29,21,17; the same class as r60_frame in the slave) */
    const volatile uint16_t *lc = (const volatile uint16_t *)((uint32_t)r60_lc | 0x20000000u);
    const volatile uint8_t *ps = UNC(r60_pown_s), *pp = UNC(r60_pown_p);
    uint16_t q = 0u;
    if (p && l < R60_LINES && lc[l * 16u + p] != 0xFFFFu)
        q = r60_quant(PAL_SH[ps[l * 16u + p] * 8u + pp[l * 16u + p]]);
    return (uint16_t)(((q >> 6) << 9) | (((q >> 3) & 7u) << 5) | ((q & 7u) << 1));
}
