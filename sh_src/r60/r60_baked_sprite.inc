/* r60_baked_sprite.inc -- the working-set pages, the baked-art lookup and r60_bk_sprite.
 * Part of r60_main.c: included there, in this order, so the compiler sees one unit. */
#include "r60_workingset.h"
/* BEGIN owned snapshot lease helper */
#ifndef PRIVATE_R60_SNAPSHOT_LEASE_H
#define PRIVATE_R60_SNAPSHOT_LEASE_H
/* PRIVATE default-off prototype. No claim of C11 atomics. Storage is existing
 * ordered uncached aligned u16; caller owns single-producer SNAP publication. */
static inline unsigned r60_snap_publish_allowed(const volatile uint16_t *done,
 const volatile uint16_t *go,const volatile uint16_t *draw,uint16_t seq)
{
 return R60_WS_LOAD(done)==R60_WS_LOAD(go) || R60_WS_LOAD(draw)==seq;
}
static inline unsigned r60_ws_claim_latest(volatile uint16_t *go,
 const volatile uint16_t *pending,volatile uint16_t *draw,
 const volatile uint16_t *snap,uint16_t *accepted)
{
 uint16_t old=R60_WS_LOAD(go),next=(uint16_t)(old+1u),seq;
 R60_WS_STORE(go,next);R60_WS_BARRIER();
 if(R60_WS_LOAD(pending)){R60_WS_STORE(go,old);return 0;}
 R60_WS_BARRIER();
 seq=R60_WS_LOAD(snap);R60_WS_BARRIER();
 R60_WS_STORE(draw,seq);*accepted=next;return 1;
}
#endif

/* END owned snapshot lease helper */
#include "r60_comm_adapter.h"
#define R60_WC_ARG uint32_t
#define R60_WC_CODE static __attribute__((noinline,section(".r60workerprobe")))
#include "r60_workingset_clip.h"
#undef R60_WC_CODE
#undef R60_WC_ARG
#include "r60_workingset_data.h"
#include "r60_game_workingset.h"
#define R60_SCALED_BAKE
/* THE BAKED ART (2026-11-04, step 4 of the bake order; Mike: "tune our work to work on RISC").
 * tools/bake_art.py pre-decodes the sprite frames the plotter spends the most ROM words on
 * into the groups format of tools/s16art.py and links them into the cart windows of
 * tools/gen_ld_ssf.py (R60BAKEART). A record whose (bank, addr, pitch, flip) is in the index
 * and whose rows fit the baked height is drawn here as a longword copy with the record's 32X
 * palette-bank bits applied only to nonzero pixels, before the shifted carry PLOT5 uses. What the plotter would have
 * stored is stored: a group only when its pens are not all zero, whole across the right limit,
 * the trailing carry by the pend rule ((x0 & ~3) + 4 * (words processed) < lim), words only while
 * x0 + 4j < lim; the row's end, the span word, the box, r60_rowpix and the rect list follow
 * r60_sprite's ROWFAST formulas, as the art cache's copy does. Zoomed, left-clipped and
 * band-split records, and any row taller than the bake, stay on the plotter (R60ZOOMBAKE
 * optionally admits on-screen scaled poses through a separate exact-zoom index). The FB bytes of a
 * transparent pen stay literal zero: a nonzero overwrite byte erases an earlier sprite.
 * R60BAKEARTCHECK compares exact bytes, with no bank-pen-zero equivalence. */
#if defined(R60_MBAND) || !defined(R60_ROWFAST) || !defined(R60_PLOT5) || !defined(R60_RECTCLR) || !defined(R60_PALMAP) || !defined(R60_SLAVEWIN) || defined(R60_PRIOMASK) || defined(R60_PRIOROW) || !defined(R60_TXTPOOL15)
#error "R60BAKEART replicates the ROWFAST + PLOT5 + RECTCLR + PALMAP + SLAVEWIN plot path over a through pen 0 (R60TXTPOOL15/HYB) and no other"
#endif
#include "artbake.h"
#define R60_BK_INDEX   ((const volatile uint32_t *)(ARTBAKE_WIN0_ADDR))   /* 'AB1', n, bytes, 0, then (key, address) pairs */
extern void r60_bk_rows(uint32_t *a);   /* sh_src/bkcopy.s: the row loop; a[16] as documented there */
/* Named view of r60_bk_rows' fixed 16-word ABI. The ordinal values must stay
 * synchronized with the numeric offsets documented and consumed by bkcopy.s. */
enum r60_bk_row_arg {
    R60_BK_SOURCE,
    R60_BK_DESTINATION,
    R60_BK_ROW_COUNT_OR_SPAN_OVERFLOW,
    R60_BK_PALETTE_ADD,
    R60_BK_WORD_LIMIT,
    R60_BK_SPRITE_X,
    R60_BK_ALIGNED_X,
    R60_BK_CLIP_X,
    R60_BK_SPAN_CURSOR,
    R60_BK_SPAN_END,
    R60_BK_SPAN_HEADER,
    R60_BK_FIRST_Y,
    R60_BK_SHIFT,
    R60_BK_CANVAS_LIMIT,
    R60_BK_OPAQUE_PIXELS,
    R60_BK_MAX_X,
    R60_BK_ROW_ARG_COUNT
};
/* The row copier reads slot 15 as the immutable OR1 cursor before resetting
 * that same slot to its established max-x output.  Reusing the last ABI word
 * avoids an SH-2 displacement that cannot encode a seventeenth word. */
#define R60_BK_OPAQUE_RUN_PLAN R60_BK_MAX_X
#include "r60_sprite_prepare.h"
/* Prepared packets own the fixed 0x0603D800..0x0603E800 slave-private arena
 * documented in RENDER-PREPARATION.md. Two-slot policy divides the same 1,024
 * words; it does not allocate more memory. */
static struct r60_sp_cache r60_sp_cache = {
    .data = (uint32_t *)0x0603D800u, .capacity = R60_SP_LARGE_WORDS
};
static struct r60_sp_cache r60_sp_cache2 = {
    .data = (uint32_t *)(0x0603D800u + R60_SP_LARGE_WORDS * 4u),
    .capacity = R60_SP_SMALL_WORDS
};
#define R60_SP_BEGIN(epoch) r60_sp_begin2(&r60_sp_cache, &r60_sp_cache2, (epoch))
#define R60_SP_GET(key, bank, source, height, rows) \
    r60_sp_get2(&r60_sp_cache, &r60_sp_cache2, (key), (bank), (source), (height), (rows))
/* per record slot, one u16: a baked key's index entry + 1 (the entry's key is re-read from the
 * index to confirm: one cached cart read), or 0x8000 | a 15-bit hash of an UNBAKED key (p15bc,
 * 2026-11-04: a plain "not baked" mark never re-checked the key, so a slot that once held an
 * unbaked record kept every later record on the plotter: 720 copies in the first wave, none
 * after); a hash collision only sends a baked record to the plotter. Optional WORKSETCACHE
 * uses the disjoint typed14-bit miss/admission encoding in r60_workingset.h. 0 = unknown. SDRAM
 * 0x293C0-0x2943F: the 128 bytes between the plane pass's own_n (0x29380, 64 B) and R60_PRKEEP /
 * CX_ROW (0x29440); the rest of the 0x29000 scratch is taken (own_s/own_p, the clear extents,
 * the speech PWM buffer at 0x29800) and .bss + .ramtext2 sit within 200 B of the 0x06019000 guard. */
#define r60_bk_pos ((uint16_t *)0x060293C0u)
#define R60_BK_UNBAKED(key) (0x8000u | ((unsigned)((key) ^ ((key) >> 16) ^ ((key) >> 7)) & 0x7FFFu))
volatile unsigned r60_bk_stat[8];          /* [0] records copied [1] not baked [2] baked but declined (zoom / clip / band / height)
                                            * [3] rows copied [4] index searches [5] FRT ticks in copies [6] FRT ticks in plotter draws [7] plotter pixels */
#ifdef R60_BAKEARTCHECK
#define R60BKCODE __attribute__((noinline))
volatile unsigned r60_bk_ckrows, r60_bk_bad, r60_bk_badx, r60_bk_badn;
/* Correctness-only row buffers in the 1 KiB beyond the prepared-packet payload. */
#define r60_bk_ca ((uint8_t *)0x0603E800u)
#define r60_bk_cb ((uint8_t *)0x0603E990u)
#define r60_sp_checks ((volatile unsigned *)0x2603EB20u)
#else
#define R60BKCODE __attribute__((section(".ramtext2.bk"), noinline))
#endif
/* the index, binary search (cart; a slot's key is looked up once while the record keeps it) */
static __attribute__((noinline)) unsigned r60_bk_lookup(uint32_t key)   /* entry + 1, or the unbaked mark */
{
    const volatile uint32_t *ix = R60_BK_INDEX;
    unsigned n = ix[1], lo = 0, hi = n;
    r60_bk_stat[4]++;
    if (ix[0] != 0x41423100u) return R60_BK_UNBAKED(key);    /* 'AB1\0' */
    while (lo < hi) {
        unsigned mid = (lo + hi) >> 1;
        uint32_t k = ix[4u + mid * 2u];
        if (k == key) return mid + 1u;
        if (k < key) lo = mid + 1u; else hi = mid;
    }
    return R60_BK_UNBAKED(key);
}
#ifdef R60_BKBENCH
/* THE MEMORY-COST MICROBENCHMARK (PROBE, 2026-11-04): the same aligned copy loop over 512 groups
 * (2 KB) from SDRAM and from the cart, into SDRAM and into the overwrite image, FRT ticks each
 * (x8 = SH-2 cycles) in r60_bk_bench[0..3]; [4] = an empty FRT read pair. Once, at the first
 * baked record. Src/dst scratch: SDRAM 0x38000-0x38FFF (probe scrap), cart = the bake's
 * window 0 + 1 KB, FB rows 200-206 (garbage for a frame: a probe rom, never a candidate). */
extern unsigned r60_bk_bench_copy(const volatile uint32_t *src, volatile uint32_t *dst, unsigned n, uint32_t base4);
volatile unsigned r60_bk_bench[8];
static __attribute__((noinline)) void r60_bk_bench_run(void)
{
    volatile uint32_t *sd = (volatile uint32_t *)0x06038000u, *dd = (volatile uint32_t *)0x06038800u;
    const volatile uint32_t *cs = (const volatile uint32_t *)(ARTBAKE_WIN0_ADDR + 1024u);
    volatile uint32_t *fb = (volatile uint32_t *)FB_OVR(200u * FB_W);
    unsigned k; uint16_t t0, t1;
    for (k = 0; k < 512u; k++) sd[k] = 0x01020304u * (k + 1u);
    t0 = r60_frt(); t1 = r60_frt(); r60_bk_bench[4] = (uint16_t)(t1 - t0);
#if R60_BKBENCH >= 2
    /* THE PUNCH'S MEMORY (R60BKBENCH=2, 2026-11-05): the FG punch READS the framebuffer through
     * the uncached alias and writes it back (FB_RMW), the one phase that reads the FB. [0]
     * SDRAM->SDRAM copy, [1] FB uncached -> SDRAM copy (the read alone), [2] SDRAM -> FB copy,
     * [3] FB uncached read-modify-write in place (the punch's own motion, a C loop), [5] the
     * same loop over uncached SDRAM (the loop's own cost with a cheap memory under it). */
    {
        const volatile uint32_t *fu = (const volatile uint32_t *)(0x24000000u + FB_ROW0 + 200u * FB_W);
        volatile uint32_t *fw = (volatile uint32_t *)(0x24000000u + FB_ROW0 + 200u * FB_W);
        volatile uint32_t *su = (volatile uint32_t *)0x26038800u;
        t0 = r60_frt(); r60_bk_bench_copy(sd, dd, 512u, 0x10101010u); t1 = r60_frt(); r60_bk_bench[0] = (uint16_t)(t1 - t0);
        t0 = r60_frt(); r60_bk_bench_copy(fu, dd, 512u, 0x10101010u); t1 = r60_frt(); r60_bk_bench[1] = (uint16_t)(t1 - t0);
        t0 = r60_frt(); r60_bk_bench_copy(sd, fb, 512u, 0x10101010u); t1 = r60_frt(); r60_bk_bench[2] = (uint16_t)(t1 - t0);
        t0 = r60_frt(); for (k = 0; k < 512u; k++) fw[k] &= 0x0F0F0F0Fu; t1 = r60_frt(); r60_bk_bench[3] = (uint16_t)(t1 - t0);
        t0 = r60_frt(); for (k = 0; k < 512u; k++) su[k] &= 0x0F0F0F0Fu; t1 = r60_frt(); r60_bk_bench[5] = (uint16_t)(t1 - t0);
        r60_bk_bench[6] = 1;
        return;
    }
#endif
    t0 = r60_frt(); r60_bk_bench_copy(sd, dd, 512u, 0x10101010u); t1 = r60_frt(); r60_bk_bench[0] = (uint16_t)(t1 - t0);
    t0 = r60_frt(); r60_bk_bench_copy(cs, dd, 512u, 0x10101010u); t1 = r60_frt(); r60_bk_bench[1] = (uint16_t)(t1 - t0);
    t0 = r60_frt(); r60_bk_bench_copy(sd, fb, 512u, 0x10101010u); t1 = r60_frt(); r60_bk_bench[2] = (uint16_t)(t1 - t0);
    t0 = r60_frt(); r60_bk_bench_copy(cs, fb, 512u, 0x10101010u); t1 = r60_frt(); r60_bk_bench[3] = (uint16_t)(t1 - t0);
    t0 = r60_frt(); r60_bk_bench_copy(cs, dd, 512u, 0x10101010u); t1 = r60_frt(); r60_bk_bench[5] = (uint16_t)(t1 - t0);   /* cart again: the cache now holds it? */
    r60_bk_bench[6] = 1;
}
#endif
/* MAPPED WORKING-SET PAGE. The master reserves a quiescent renderer, asks the
 * 68K to map slot 7, publishes a new generation after the acknowledgement, and
 * the slave invalidates every page-derived cache before its next pass. COMM10
 * is shared fairly with sound commands. The ownership state machine and the
 * title/engine boundary are documented in WORKING-SET-LIFECYCLE.md. */
extern char WRAM_BASE[];
volatile uint16_t r60_page_gen;                 /* completed switches; the slave reads it uncached */
static volatile uint16_t r60_page_cur = 8u;
static volatile uint16_t r60_page_pending;
#define R60_PAGE_PENDING (*(volatile uint16_t *)((uintptr_t)&r60_page_pending | 0x20000000u))
#define R60_PAGE_CURRENT (*(volatile uint16_t *)((uintptr_t)&r60_page_cur | 0x20000000u))
typedef char r60_ws_ab1_base_guard[(SPRITE_WORKSET_AB1_ADDRESS==ARTBAKE_WIN0_ADDR)?1:-1];
typedef char r60_ws_bounds_guard[(
    SPRITE_WORKSET_PAGE_BYTES>=SPRITE_WORKSET_GUARD_BYTES &&
    SPRITE_WORKSET_AB1_ENTRIES<=32767u && SPRITE_WORKSET_AB1_BYTES>=16u &&
    SPRITE_WORKSET_AB1_ENTRIES<=(SPRITE_WORKSET_AB1_BYTES-16u)/8u &&
    SPRITE_WORKSET_AB1_ADDRESS>=SPRITE_WORKSET_PAGE_ADDRESS &&
    SPRITE_WORKSET_AB1_ADDRESS-SPRITE_WORKSET_PAGE_ADDRESS<=SPRITE_WORKSET_PAGE_BYTES-SPRITE_WORKSET_GUARD_BYTES &&
    SPRITE_WORKSET_AB1_BYTES<=SPRITE_WORKSET_PAGE_BYTES-SPRITE_WORKSET_GUARD_BYTES-(SPRITE_WORKSET_AB1_ADDRESS-SPRITE_WORKSET_PAGE_ADDRESS) &&
    SPRITE_WORKSET_ZB1_BYTES>=16u && SPRITE_WORKSET_ZB1_ENTRIES<=(SPRITE_WORKSET_ZB1_BYTES-16u)/12u &&
    SPRITE_WORKSET_ZB1_ADDRESS>=SPRITE_WORKSET_AB1_ADDRESS &&
    SPRITE_WORKSET_ZB1_ADDRESS-SPRITE_WORKSET_AB1_ADDRESS>=SPRITE_WORKSET_AB1_BYTES &&
    SPRITE_WORKSET_ZB1_ADDRESS-SPRITE_WORKSET_PAGE_ADDRESS<=SPRITE_WORKSET_PAGE_BYTES-SPRITE_WORKSET_GUARD_BYTES &&
    SPRITE_WORKSET_ZB1_BYTES<=SPRITE_WORKSET_PAGE_BYTES-SPRITE_WORKSET_GUARD_BYTES-(SPRITE_WORKSET_ZB1_ADDRESS-SPRITE_WORKSET_PAGE_ADDRESS)
    )?1:-1];
static __attribute__((noinline,section(".r60worker"))) unsigned r60_ws_game_want(void)
{
    return r60_game_workingset((const volatile uint8_t *)((uintptr_t)WRAM_BASE | 0x20000000u));
}
volatile unsigned r60_page_stat[4];             /* [0] switches [1] vints withheld (ack wait) [2] vints withheld (slave or mailbox busy) [3] the page shown */
static inline unsigned r60_page_want(void)
{
    if (r60_ws_game_want()) return R60_WS_PAGE;
    unsigned lv = ((volatile unsigned char *)WRAM_BASE)[0x3142];
    return 8u + (lv < (unsigned)ARTBAKE_PAGES ? lv : 0u);
}
/* Both helpers execute while RV is restored. The reserve helper MUST return
 * to RAM before COMM10 is posted; commit requires ACK0 or a later sound post. */
static __attribute__((noinline,section(".r60worker"))) unsigned r60_ws_reserve_page(unsigned want)
{
    unsigned decision = r60_ws_reserve_or_defer(&R60_PAGE_PENDING,(uint16_t)want,
        &R60_DONE,&R60_GO,&R60_DRAWSEQ,&R60_SNAPSEQ,&MARS_SYS_COMM10);
    if (decision == R60_WS_WAIT &&
        r60_comm_wait_first(R60_COMM,1u,R60_DIAG[0]))
        r60_page_stat[2]++;
    return decision;
}
static __attribute__((noinline,section(".r60worker"))) void r60_ws_commit_page(void)
{
    r60_page_stat[0]++;
    r60_page_stat[3]=R60_PAGE_PENDING;
    r60_ws_commit(&R60_PAGE_CURRENT,
        (volatile uint16_t *)((uintptr_t)&r60_page_gen | 0x20000000u),&R60_PAGE_PENDING);
    r60_comm_map_committed(R60_COMM);
}
/* Cart policy returns before any mapper post. Upper16 is a proposed MMIO word;
 * lowbit holds snapshots. Audio dequeue occurs only after the RAM store. */
static __attribute__((noinline,section(".r60worker"))) unsigned r60_ws_plan(unsigned native)
{
    unsigned plan,want,decision;
    uint8_t cmd;
    R60_COMM_IDLE_MAP=0u;

    /* Finish the in-flight mapping before planning any new mailbox work. */
    if (R60_PAGE_PENDING) {
        if (!r60_ws_mapper_acked(MARS_SYS_COMM10)) {
            if (r60_comm_wait_first(R60_COMM,0u,R60_DIAG[0]))
                r60_page_stat[1]++;
            return 1u;
        }
        r60_ws_commit_page();
    }

    want = r60_page_want();
    r60_comm_set_want(R60_COMM,want,R60_PAGE_CURRENT,R60_DIAG[11]);
    plan = r60_comm_plan(R60_COMM,R60_PAGE_CURRENT,R60_DIAG[11]);
    if (plan & R60_COMM_MAP) {
        decision = r60_ws_reserve_page(want);
        if (decision == R60_WS_POST) return ((0x5be0u|want)<<16)|1u;
        R60_COMM_IDLE_MAP=1u;
        /* A busy mailbox retains map intent and drains the slave; no new
         * audio refill until priority expires or the map is committed. */
        return 1u;
    }
    if(native && (plan&R60_COMM_AUDIO) && MARS_SYS_COMM10==0u &&
        r60_comm_can_audio(R60_COMM,R60_DIAG[11]) && r60_comm_peek(R60_COMM,&cmd))
        return ((0x5a00u|cmd)<<16)|((plan&R60_COMM_HOLD)!=0u);
    return (plan&R60_COMM_HOLD)!=0u;
}
static __attribute__((noinline,section(".r60worker"))) void r60_ws_audio_posted(void)
{
    (void)r60_comm_audio_posted(R60_COMM,R60_DIAG[11]);
}
/* This RAM routine alone posts commands. A mapper store is followed solely by
 * RAM return instructions, since RV may immediately become inaccessible. */
__attribute__((noinline,section(".ramtext2"))) static unsigned r60_page_service(unsigned native)
{
    unsigned action,command;
    if (R60_PAGE_PENDING && !r60_ws_mapper_acked(MARS_SYS_COMM10)) {
        if (r60_comm_wait_first(R60_COMM,0u,R60_DIAG[0]))
            r60_page_stat[1]++;
        return 1u;
    }
    action = r60_ws_plan(native);
    command = action >> 16;
    if (command) {
        MARS_SYS_COMM10=(uint16_t)command;
        if ((command&0xff00u)==0x5a00u) r60_ws_audio_posted();
    }
    return action&1u;
}
/* Only ACK publication before the post8 observer; no stale-phase request or
 * audio flush here. The old page remains available while audio is occupied. */
static inline void r60_ws_pre_ack(void)
{
    if (R60_PAGE_PENDING && r60_ws_mapper_acked(MARS_SYS_COMM10))
        r60_ws_commit_page();
}
#define R60_PAGE_HOLD() r60_page_service()
/* slave, at a pass start: a completed switch purges the cache and the slot index cache */
static uint16_t r60_page_seen;
#define R60_PAGE_SLAVE() do { \
    uint16_t g_ = *(volatile uint16_t *)((uintptr_t)&r60_page_gen | 0x20000000u); \
    __asm__ volatile ("" : : : "memory"); \
    if (g_ != r60_page_seen) { unsigned k_; r60_page_seen = g_; *(volatile uint8_t *)0xFFFFFE92u = 0x11u; \
        for (k_ = 0; k_ < 64u; k_++) r60_bk_pos[k_] = 0; } } while (0)
/* the record through the bake, 1 = drawn (spans, box, rects, rowpix as r60_sprite would), 0 = not here */
#ifdef R60_SCALED_BAKE
#define R60_ZB_STAT(k,v) ((void)0)
/* Cold cart helper: generated bounds describe ONLY this page-local raw ZB1.
 * AB1's wire byte count also includes global bodies and is not its local extent. */
static __attribute__((noinline,section(".r60worker"))) uint32_t r60_zb_lookup(
    uint32_t key,unsigned zoom
    ,const volatile uint8_t **run_plan
    )
{
    unsigned pose_index;
    const volatile uint32_t *frame=r60_ws_zb_find_index(R60_PAGE_CURRENT,
        (const volatile uint32_t *)SPRITE_WORKSET_ZB1_ADDRESS,
        SPRITE_WORKSET_ZB1_BYTES,key,zoom,&pose_index);
    if(!frame)return 0u;
    *run_plan=r60_ws_or_find(R60_PAGE_CURRENT,
        (const volatile uint32_t *)SPRITE_WORKSET_OPAQUE_RUNS_ADDRESS,
        SPRITE_WORKSET_OPAQUE_RUNS_BYTES,pose_index,
        SPRITE_WORKSET_ZB1_ENTRIES,frame[0]>>16);
    return (uint32_t)frame;
}
#endif
static R60BKCODE unsigned r60_bk_sprite(
    const volatile uint16_t *record, unsigned bank, unsigned sprite_slot)
{
#ifdef R60_BKBENCH
    if (!r60_bk_bench[6]) r60_bk_bench_run();
#endif
    int      top   = record[0] & 0xFF, bot = record[0] >> 8;
    int      xpos  = (int)(record[1] & 0x1FF) - 184 + (int)r60_xo;
    unsigned d2    = record[2];
    uint32_t key   = ((uint32_t)(d2 & 0x100u) << 20) | ((uint32_t)R60_SPR_BANK((record[4] >> 8) & 0xF) << 24) | ((uint32_t)(d2 & 0xFFu) << 16) | record[3];
    uint32_t fa;
    unsigned base  = (unsigned)(R60_BANK_OF(record[4] & 0x3Fu) << 4), base4 = base * 0x01010101u;
    int      lim = (int)(FB_W + r60_xo), xo = (int)r60_xo, x0 = xpos, ys, ye, y, xs, cap = (int)(FB_W + 512u), xmax = 0;
    unsigned r, nrows, npx5 = 0, sp_full = 0, bk = bank & 1u, sh, bh, ncap;
    unsigned bx0 = FB_W + 512u, bx1 = 0, by0 = FB_H, by1 = 0;
    uint32_t sp_hdr, *spw, *sp_end;
    unsigned clipped=0u;
    const volatile uint32_t *src;
    const volatile uint8_t *run_plan=0;
    volatile uint8_t *rowp;
#ifdef R60_BAKEARTCHECK
    unsigned ca_addr = record[3];
    int pitch = (int)(signed char)(d2 & 0xFF), flip = d2 & 0x100;
    const uint16_t *sd = game_sprites + R60_SPR_BANK((record[4] >> 8) & 0xF) * 0x10000u;
    const uint16_t *pt5;
    if (!r60_pt_init) r60_pt_build();
    pt5 = r60_pt[flip ? R60_PT_FI : 0u][base >> 4];
#endif
#ifdef R60_SCALED_BAKE
    if (record[5] & 0x3FFu) {
        R60_ZB_STAT(0,1u);
#ifdef R60_BAKEARTCHECK
        /* The existing checker specifically replays unscaled PLOT5 rows. */
        return 0;
#else
        fa = r60_zb_lookup(key, record[5] & 0x3FFu
            ,&run_plan
            );
        if (!fa) return 0;
#endif
    } else
#endif
    {   unsigned pos = r60_bk_pos[sprite_slot];
        if (pos & 0x8000u) { if (pos != R60_BK_UNBAKED(key)) { pos = r60_bk_lookup(key); r60_bk_pos[sprite_slot] = (uint16_t)pos; } }
        else if (pos == 0u || R60_BK_INDEX[4u + (pos - 1u) * 2u] != key) { pos = r60_bk_lookup(key); r60_bk_pos[sprite_slot] = (uint16_t)pos; }
        if (pos & 0x8000u) { r60_bk_stat[1]++; return 0; }
        fa = R60_BK_INDEX[5u + (pos - 1u) * 2u];
    }
    ys = top; ye = bot < (int)FB_H ? bot : (int)FB_H;
    if (
#ifndef R60_SCALED_BAKE
        (record[5] & 0x3FFu) ||
#endif
        top >= bot || xpos >= lim || ys >= ye || x0 < xo) { r60_bk_stat[2]++; return 0; }
    src = (const volatile uint32_t *)fa;
#ifdef R60_SCALED_BAKE
    /* Initial atlas adapter admits fully on-screen scaled rows only. The native
     * word scaler's right-edge overshoot differs from output-group clipping. */
    if ((record[5] & 0x3FFu) && x0 + (int)(src[0] & 0xFFFFu) > lim) clipped=1u;
    if(clipped)run_plan=0;
#endif
    bh = src[0] >> 16;                                   /* u16 h, u16 w */
    nrows = (unsigned)(ye - ys);
    if (nrows > bh) { r60_bk_stat[2]++; return 0; }
#ifdef R60_SCALED_BAKE
    if (record[5] & 0x3FFu) { R60_ZB_STAT(1,1u); R60_ZB_STAT(2,nrows); }
#endif
    src++;
#ifdef R60_SCALED_BAKE
    if (!(record[5] & 0x3FFu))
#endif
    {
        /* Scaled packets have a different extent format. For unscaled AB1 art,
         * base>>4 is the numeric palette bank and src points past the pose
         * height/width header. A prepared hit has already applied that bank,
         * so the established row copier receives base4=0. */
        const uint32_t *prepared = R60_SP_GET(key, base >> 4, src, bh, nrows);
        if (prepared) {
#ifdef R60_BAKEARTCHECK
            r60_sp_checks[(unsigned)x0 & 3u]++;
            if (base) r60_sp_checks[4]++;
#endif
            src = prepared; base4 = 0;
        }
    }
    r60_bk_stat[0]++;
    sh = ((unsigned)x0 & 3u) << 16;                      /* the copy's shift selector (x0 & 3) */
    ncap = (unsigned)(lim - x0 + 3) >> 2;                /* words the plotter processes at most: x0 + 4j < lim */
    xs = x0 & ~3;
    rowp = (volatile uint8_t *)FB_OVR((unsigned)ys * FB_W) + (unsigned)xs;
    sp_hdr = ((uint32_t)(ks_cur & 63u) << 16) | ((uint32_t)(xs >> 2) << 8);
    spw = &r60_rowspans[bk][r60_rowspan_n[bk]]; sp_end = &r60_rowspans[bk][R60_SSPAN_CAP];
    if (x0 > xo && (unsigned)x0 < bx0) bx0 = (unsigned)x0; else if (x0 <= xo) bx0 = r60_xo;
    by0 = (unsigned)ys; by1 = (unsigned)ye;
#ifndef R60_BAKEARTCHECK
    {   /* sh_src/bkcopy.s does the rows: the groups, the bank in every byte, the span words, xe, npx */
        uint32_t args[R60_BK_ROW_ARG_COUNT];
        args[R60_BK_SOURCE] = (uint32_t)src; args[R60_BK_DESTINATION] = (uint32_t)rowp;
        args[R60_BK_ROW_COUNT_OR_SPAN_OVERFLOW] = nrows; args[R60_BK_PALETTE_ADD] = base4;
        args[R60_BK_WORD_LIMIT] = ncap; args[R60_BK_SPRITE_X] = (uint32_t)x0;
        args[R60_BK_ALIGNED_X] = (uint32_t)xs; args[R60_BK_CLIP_X] = (uint32_t)lim;
        args[R60_BK_SPAN_CURSOR] = (uint32_t)spw; args[R60_BK_SPAN_END] = (uint32_t)sp_end;
        args[R60_BK_SPAN_HEADER] = sp_hdr; args[R60_BK_FIRST_Y] = (uint32_t)ys;
        args[R60_BK_SHIFT] = sh >> 16; args[R60_BK_CANVAS_LIMIT] = (uint32_t)cap;
        args[R60_BK_OPAQUE_RUN_PLAN] = (uint32_t)run_plan;
        if(clipped)r60_wc_rows(args,record[5]&31u);else
        r60_bk_rows(args);
        spw = (uint32_t *)args[R60_BK_SPAN_CURSOR];
        sp_full = args[R60_BK_ROW_COUNT_OR_SPAN_OVERFLOW];
        npx5 = args[R60_BK_OPAQUE_PIXELS]; xmax = (int)args[R60_BK_MAX_X];
        (void)r; (void)y;
    }
#else
    for (r = 0; r < nrows; r++, rowp += FB_W) {
        uint32_t hdr = *src++;
        unsigned j0 = hdr >> 24, ng = (hdr >> 16) & 0xFFu, nw = hdr & 0xFFFFu, nproc, kend, xe, rnp;
        y = ys + (int)r;
        nproc = nw < ncap ? nw : ncap;
        kend = j0 >= nproc ? 0u : (nproc - j0 < ng ? nproc - j0 : ng);
        /* sh_src/bkcopy.s: the groups, the bank in every byte, the trailing carry where the plotter
         * would store it (the next word's store, or the pend rule: (x0 & ~3) + 4 * words < lim) */
        {   uint32_t args[R60_BK_ROW_ARG_COUNT];           /* one row through the asm, as the live build would */
            args[R60_BK_SOURCE] = (uint32_t)(src - 1); args[R60_BK_DESTINATION] = (uint32_t)rowp;
            args[R60_BK_ROW_COUNT_OR_SPAN_OVERFLOW] = 1u; args[R60_BK_PALETTE_ADD] = base4;
            args[R60_BK_WORD_LIMIT] = ncap; args[R60_BK_SPRITE_X] = (uint32_t)x0;
            args[R60_BK_ALIGNED_X] = (uint32_t)xs; args[R60_BK_CLIP_X] = (uint32_t)lim;
            args[R60_BK_SPAN_CURSOR] = (uint32_t)spw; args[R60_BK_SPAN_END] = (uint32_t)spw; /* C writes the span */
            args[R60_BK_SPAN_HEADER] = sp_hdr; args[R60_BK_FIRST_Y] = (uint32_t)y;
            args[R60_BK_SHIFT] = sh >> 16; args[R60_BK_CANVAS_LIMIT] = (uint32_t)cap;
            args[R60_BK_OPAQUE_RUN_PLAN] = 0u;
            r60_bk_rows(args); rnp = args[R60_BK_OPAQUE_PIXELS];
        }
        src += ng;
        npx5 += rnp;
        xe = (unsigned)x0 + 4u * nproc;
#ifdef R60_BAKEARTCHECK
        {   /* Exact overwrite bytes, including zero pens: nonzero bank-pen0
             * erases an earlier sprite, unlike a zero overwrite-image byte. */
            unsigned k2, xa, np = 0, nd = 0;
            int bias = xo & ~3;  /* preserve alignment while bounding the RAM row buffers */
            for (k2 = 0; k2 < 400u; k2++) { r60_bk_ca[k2] = 0; r60_bk_cb[k2] = 0; }
            ca_addr = (unsigned)(ca_addr + pitch);
            xa = flip ? r60_plot_row5f(sd, ca_addr, x0-bias, (volatile uint8_t *)r60_bk_ca, lim-bias, xo-bias, pt5, &np)
                      : r60_plot_row5n(sd, ca_addr, x0-bias, (volatile uint8_t *)r60_bk_ca, lim-bias, xo-bias, pt5, &np);
            xa += (unsigned)bias;
            {   uint32_t args[R60_BK_ROW_ARG_COUNT];
                args[R60_BK_SOURCE] = (uint32_t)(src - ng - 1);
                args[R60_BK_DESTINATION] = (uint32_t)(r60_bk_cb + xs-bias);
                args[R60_BK_ROW_COUNT_OR_SPAN_OVERFLOW] = 1u; args[R60_BK_PALETTE_ADD] = base4;
                args[R60_BK_WORD_LIMIT] = ncap; args[R60_BK_SPRITE_X] = (uint32_t)(x0-bias);
                args[R60_BK_ALIGNED_X] = (uint32_t)(xs-bias); args[R60_BK_CLIP_X] = (uint32_t)(lim-bias);
                args[R60_BK_SPAN_CURSOR] = 0; args[R60_BK_SPAN_END] = 0;
                args[R60_BK_SPAN_HEADER] = sp_hdr; args[R60_BK_FIRST_Y] = (uint32_t)y;
                args[R60_BK_SHIFT] = sh >> 16; args[R60_BK_CANVAS_LIMIT] = (uint32_t)cap;
                args[R60_BK_OPAQUE_RUN_PLAN] = 0u;
                r60_bk_rows(args);
            }
            r60_bk_ckrows++;
            if (xa != xe) r60_bk_badx++;
            if (np != rnp) r60_bk_badn++;
            for (k2 = 0; k2 < 400u; k2++) {
                unsigned a = r60_bk_ca[k2], b = r60_bk_cb[k2];
                if (a != b) { nd = 1; break; }
            }
            if (nd) r60_bk_bad++;
        }
#endif
        {   int xe4 = ((int)xe + 3) & ~3;
            if (xe4 > cap) xe4 = cap;
            if (xe4 > xs) {
                uint32_t w_ = sp_hdr | ((uint32_t)(unsigned)y << 24) | (uint32_t)(xe4 >> 2);
                if (spw < sp_end) *spw++ = w_; else sp_full = 1;
            }
            if ((int)xe > xmax) xmax = (int)xe;
        }
    }
#endif
    r60_bk_stat[3] += nrows;
    if (xmax > 0) { unsigned xe2 = (unsigned)xmax > FB_W + r60_xo ? FB_W + r60_xo : (unsigned)xmax; if (xe2 > bx1) bx1 = xe2; }
    r60_rowspan_n[bk] = (uint16_t)(spw - r60_rowspans[bk]);
    if (sp_full) r60_rowspan_full[bk] = 1;
    r60_rowpix += npx5;
    if (by1 > by0 && bx1 > bx0) { r60_last_box[0] = (uint16_t)bx0; r60_last_box[1] = (uint16_t)bx1; r60_last_box[2] = (uint16_t)by0; r60_last_box[3] = (uint16_t)by1; }
    else { r60_last_box[0] = 0; r60_last_box[1] = 0; r60_last_box[2] = 0; r60_last_box[3] = 0; }
    if (by1 > by0 && bx1 > bx0) {
        volatile uint16_t *rc = R60_RECTS(bank);
        unsigned n = R60_RECTN(bank);
        if (n < 64u) {
            rc[n * 4] = (uint16_t)bx0; rc[n * 4 + 1] = (uint16_t)bx1; rc[n * 4 + 2] = (uint16_t)by0; rc[n * 4 + 3] = (uint16_t)by1;
            R60_RECTN(bank) = (uint16_t)(n + 1u);
        }
    }
    return 1;
}
#ifndef R60_PAGE_HOLD                           /* no bake, or a bake without pages: no switch */
#define R60_PAGE_HOLD() 0u
#define R60_PAGE_SLAVE() ((void)0)
#endif

#define R60_SPRITE_S(e_, ylo_, yhi_) r60_sprite((e_), (ylo_), (yhi_), r60_mask, bkp, 0u)
#define R60_PUNCH(box_)          r60_punch((box_))
#define R60_PUNCH_SPANS(n0_, n1_) r60_punch_spans(bkp, (n0_), (n1_))
#define R60_FLIP_NOW() ((void)0)
