#include "mars.h"

/* Referenced by the DMA IRQ vector in mars_start.s; no PWM audio yet. */
__attribute__((section(".ramtext"))) void amb_dma_handler(void)
{
}

#define SYNC        ((volatile uint16_t *)0x26028800)

/* Per-CPU FRT (same register addresses, this CPU's counter). Used by
 * the paced idle below; on-chip, zero external bus traffic. */
static inline uint16_t frt_s(void)
{
    uint8_t h = SH2_FRT_FRCH;
    uint8_t l = SH2_FRT_FRCL;
    return (uint16_t)((h << 8) | l);
}
/* (TEXT_U / PAL_U / PAL_SETGEN / pal_set_of retired with the COMM stream
 * in LOOP 8 — the master owns all three now, off the DREQ packet.) */

extern void slave_window_k(uint16_t cmd);    /* m_main.c .ramtext */
#ifdef CHAIN_METER
/* NOTES 78 / LOOP29 292: THE SLAVE CHAIN METER, RELOCATED AND SELF-CALIBRATING.
 *
 * WHY THE OLD ONE WAS VOID: it accumulated at 0x2603A7D8, which is also
 * SPRLATE's lean base (m_main.c ~915) -- and the shipping line carries
 * -DSPR_LATE.  Worse, SPRLATE writes the CACHED alias 0x0603A7D8 from
 * the master while this wrote the UNCACHED 0x2603A7D8 from the slave, so
 * a master writeback silently reverted the slave's sums.  Measured on
 * the attract: busy ticks 0 against 3817 links.  A zero that is a
 * collision, not a measurement.  The 0x3A790 slice meter shares the
 * boot-benchmark's block ([0..4]) for the same reason.
 *
 * HERE: the audited-free PH scratch (0x28E38-0x28E7F, m_main.c ~1157),
 * which is PROBE-ONLY and unused unless PHASE_CENSUS is on -- verified
 * zero across a 4000-frame attract on the line build before use.
 *   [0] slave_concurrent_k busy ticks   [1] links
 *   [2] entry-to-entry ticks            [3] intervals
 *   [4] slave_window_k pickup ticks     [5] pickups
 * [2]/[3] CALIBRATES THE SLAVE TICK against a period the run already
 * knows (frames / generations), so nothing here assumes the phi/8
 * prescale the way a ticks->vints conversion otherwise would. */
#if defined(PHASE_CENSUS)
#error "CHAIN_METER's SCM shares PHASE_CENSUS's PH scratch - build them separately"
#endif
#define SCM ((volatile uint32_t *)0x26028E40)
static uint16_t scm_prev;
static uint8_t  scm_have;
#define SCM_ENTRY() do { \
        uint16_t e_ = frt_s(); \
        if (scm_have) { SCM[2] += (uint16_t)(e_ - scm_prev); SCM[3] += 1; } \
        scm_prev = e_; scm_have = 1; } while (0)
#endif

#if defined(FB_TEXT_READ) && defined(TEXTCAP_SLAVE)
extern void text_capture(void);              /* m_main.c .ramtext */
#endif
extern void slave_concurrent_k(uint16_t cmd);

/* The preempt-blit mailbox poll. It kept the name from when it also
 * serviced the MD's COMM stream; that stream is gone (LOOP 8), so the
 * 0x2000-family render command on COMM0 is now COMM's only traffic and
 * the master consumes it. */
#ifdef PICKUP_SRC_PROBE
#define PSRC ((volatile uint32_t *)0x26028F50)
volatile uint8_t slave_in_compose;
#endif

#ifdef PHASE_CENSUS
/* ONE-GENERATION EVENT TRACE (2026-09-01): 0x398E0-0x3993F, the audited-
 * free 96B. Entry = (event << 16) | FRT16; STR[23] = write index (0 =
 * disarmed; the master arms it at the launch of gen ST_ARM_GEN and
 * freezes it at the next launch). Slave events 1..8 in slave ticks
 * (phi/8), master events 0x81.. in master ticks (phi/32) — see
 * tools/gen_trace.py. */
#define STR ((volatile uint32_t *)0x260398E0)
/* per-band duration sums (slave ticks) + gen count: STR[19..22] —
 * accumulated here from the band stamps (2 = band0 start, 3 = band end)
 * so the dispatch site pays only the calls (region guard). */
#define STB ((volatile uint32_t *)0x2603992C)
#ifndef CEN
#define CEN ((volatile uint32_t *)0x2602FF00)   /* the phase census slots (m_main.c) */
#endif
static uint16_t st_last;
static uint8_t st_band;
/* (the one-generation event ring that lived here is retired — the
 * region guard; its four traces are banked in docs/design/BOSSFIGHT.md) */
__attribute__((section(".ramtext"), noinline)) void st_s(unsigned ev)
{
    uint16_t now = frt_s();
    /* pass sums (all bands): 10 = strip clear, 11 = strip sprites (and the
     * band's compose remainder), 12 = cat1 done, 13 = text + service to the
     * band end (LOOP29 255). 2/3 (band start/end) re-base the clock */
    if (ev >= 10)
        STB[ev - 10] += (uint16_t)(now - st_last);
    st_last = now;
    (void)st_band;
}
#else
#define st_s(ev) ((void)0)
#endif

__attribute__((section(".ramtext"))) void slave_service_stream(void)
{
    /* (iter4) PREEMPT-BLIT MAILBOX: the master hands the per-window blit
     * here (SYNC[4]) instead of waiting for the whole concurrent compose
     * to drain. This is polled at EVERY stream-service point — between the
     * slave's compose strips and in the idle loop — so the master's blit
     * pickup latency is bounded by one 12-row strip, not a full compose.
     * Clear SYNC[4] BEFORE echoing so a re-entry never double-blits. */
    uint16_t bc = SYNC[4];
#if defined(FB_TEXT_READ) && defined(TEXTCAP_SLAVE)
    if (bc == 0x4000) {                  /* k2 text capture (LOOP 20) */
        SYNC[4] = 0;
#ifdef VB_SPAN
        SYNC[7] = 0x4001;                /* LOOP28 96: PICKED IT UP. The
                                          * master's join is its single
                                          * biggest vblank term; this
                                          * splits it into our latency
                                          * and our copy. */
#endif
#ifdef TEXTCAP_EARLY
        /* LOOP29 145: posted at the master's ISR entry, BEFORE the 68K
         * has raised FM. An FB read at FM=0 is garbage, so wait for FM
         * (the 68K's post follows within ~25 lines); if it never comes
         * this vint, say so and let the master decide. */
        {
            uint32_t g = 60000;          /* ~40 lines */
            while (!(MARS_SYS_INTMSK & 0x8000) && --g) ;
            if (!g) { SYNC[6] = 0x4002; return; }
        }
#endif
        text_capture();
        SYNC[6] = 0x4000;                /* echo AFTER the copy lands */
        return;
    }
#endif
#ifndef DIRECT_FB
    /* REBUILD STAGE 2: the master never posts 0x3000 blit commands on
     * the DIRECT_FB build — SYNC[4] survives as the textcap-only
     * mailbox above. */
    if (bc) {
#ifdef CHAIN_METER
        {
            uint16_t bt = frt_s();
            slave_window_k(bc);
            SCM[4] += (uint16_t)(frt_s() - bt);   /* slice-blit pickup time */
            SCM[5] += 1;
            SYNC[4] = 0;
            SYNC[5] = bc;
            return;
        }
#endif
#ifdef PICKUP_SRC_PROBE
        /* Which side of the dependency are we on? An in-compose pickup
         * means the master is waiting for rows this CPU has not finished
         * composing; an idle-loop pickup means the compose was already
         * done and the mailbox was answered immediately. */
        PSRC[(slave_in_compose ? 0 : 3) + ((bc >> 4) & 3)]++;
#endif
        slave_window_k(bc);          /* blit slave half; SYNC[2] set inside */
        SYNC[4] = 0;
        SYNC[5] = bc;                /* echo: blit path complete */
    }
#else
    (void)bc;
#endif /* !DIRECT_FB */

#if defined(R60) && !defined(NO_SELF_CHAIN)
    /* SLAVE PARK (2026-08-25). Under SELF-CHAIN the slave composes
     * back-to-back and the idle gaps that used to leave the bus quiet
     * during DREQ landings are gone — measured: rejects 2.5->12.7%,
     * BAD1 181->636 on the raw self-chain build. The master broadcasts
     * SYNC[12]=1 at the k1 announce (landing in flight) and clears it
     * at the post; every service point spins FRT-quiet (on-chip reads
     * only) while it is set. Bounded ~100 lines so a missed clear can
     * never wedge the compose. */
    /* VARIANT D — PARK ON FM (2026-08-25). Variants keyed to the k1
     * announce, the window post, or a COMM0 sniff all failed the same
     * way (rejects 12.7-14% vs bounded-hold's 2.0): the injury is the
     * 68K's FM-HELD FRAME-BODY work (consumes + window/ack, ~95
     * lines), which STARTS before any of those signals fire. FM
     * itself is the hardware bracket: the 68K raises it at entry,
     * the master drops it when the window service ends (INTMSK bit
     * 15, an on-chip register read — zero external bus). Park while
     * it is set; break for SYNC[4] work (our own blit half and the
     * text capture are commanded INSIDE the FM span and must not sit
     * behind the park), polled every 4th pace so the SDRAM read stays
     * sparse. SYNC[12] is left to its owner but no longer parked on. */
    {
        uint16_t t0p = frt_s();
        unsigned pk = 0;
        while ((MARS_SYS_INTMSK & 0x8000) &&
               (uint16_t)(frt_s() - t0p) < 4600) {
            if ((++pk & 3) == 0) {
                if (SYNC[4])
                    break;
#ifdef LAND_PARK
                /* LANDPARK probe: SYNC[12] now means "landing in flight"
                 * (set at the announce, cleared at landing-done instead
                 * of at pickup) — park only while the DMAC drains. */
                if (!SYNC[12])
                    break;
#endif
            }
            uint16_t tq = frt_s();
            while ((uint16_t)(frt_s() - tq) < 16) ;
        }
    }
#endif

    /* LOOP 8: THE COMM STREAM IS GONE. Text and the layer/rowscroll regs
     * moved to the DREQ packet in LOOP 7a; the palette batch (0x4800|idx)
     * was COMM's last tenant and the write-thunks retired it — the MD now
     * marks dirty 128-word regions and the master applies them straight
     * off the DREQ packet, PAL_SETGEN included. So COMM0 carries only the
     * window command/ack (consumed by the master), and this function is
     * now purely the preempt-blit mailbox poll it shares the name with.
     * That is the whole point of the arc: COMM cost 1.27 lines per word
     * against DREQ's 0.063, because its cost was an ack round-trip per
     * 5-word batch and the 68K BLOCKED on the slave for every one. */
}

/* Secondary SH-2: boot heartbeat on COMM6 until the MD's 0xB007 beacon
 * (proves the slave reached SDRAM code), then: render worker + stream
 * servicer. Commands arrive via the SDRAM SYNC mailbox — NOT the COMM
 * registers, which carry MD stream payloads whenever the game runs. */
#ifdef BLIT_CHASE
#ifndef BAND_SHIFT
#define BAND_SHIFT 0
#endif
/* BLIT CHASE fence: SYNC[14] = first master-blitted row not yet shipped
 * (224 = none pending). Bounded at ~0.5v (slave ticks = phi/8) so a
 * missed post can never wedge the compose (timeouts measured 0 once the
 * master became the only publisher; the counter went for RAM). */
/* 2026-09-05: cart ROM — a SYNC poll loop the I-cache holds (84B of SDRAM
 * freed for the scroll sync; region guard). */
static void slave_fence(unsigned row)
{
    uint16_t t0 = frt_s();
    while (SYNC[14] < row && (uint16_t)(frt_s() - t0) < 24000) ;
}
#endif

__attribute__((section(".ramtext"))) void s_main(void)
{
#ifdef BOOT_SHSTAGE
    *(volatile uint16_t *)0x2000402A = 4;    /* slave probe: SDRAM main entered */
#endif
    while (MARS_SYS_COMM14 != 0xB007)
        MARS_SYS_COMM6++;

#ifdef R60
    /* Slave stack watermark sentinel: top moved 0x40000 stays, but its
     * floor is now 0x3F800 (master top took 2KB). Paint the floor up
     * to just under the live SP; an ares dump reads the watermark. */
    {
        uint32_t sp;
        __asm__ __volatile__("mov r15,%0" : "=r"(sp));
        sp -= 64;
        for (uint32_t a = 0x0603FA00u; a < sp; a += 4)   /* 0x3F800-7F
                                                          * spr_pair,
                                                          * 0x3F880-9FF
                                                          * PAL_SETGEN */
            *(volatile uint32_t *)a = 0x5A5A5A5Au;
    }
#endif

    for (;;) {
        uint16_t cmd = SYNC[0];
        if (cmd & 0xF000) {
            /* SLAVE BUSY CENSUS (2026-09-01, the vint-quantization
             * question): total ticks spent handling ANY command, one
             * wrap at the dispatch site. busy/elapsed over a span
             * (state deltas) = slave utilization: ~100% in the boss
             * fight = compute-bound (diets help); well under = the
             * wall is waiting/serialization and diets cannot move
             * it. Scratch 0x28C80 [0] busy ticks (docs/design/INTEGRATION.md).
             * CALIBRATION: the slave never sets FRT TCR — its ticks
             * are phi/8 = 48208/vint, 4x the master's 12052 (which
             * also means the park bound below is ~25 lines, not the
             * ~100 its comment assumed — pre-existing, load-bearing,
             * left as-is). First reading: normal play = ~36%
             * utilization. */
            uint16_t gw0 = frt_s();
            st_s(1);
#ifdef PHASE_CENSUS
            CEN[53]++;                                   /* 255d: slave commands */
            if ((cmd & 0xF000) == 0x3000) CEN[55]++;     /* window commands */
            else if (cmd & 0x0040) CEN[54]++;            /* chain commands (3 bands) */
#endif
            if ((cmd & 0xF000) == 0x3000) {
#ifndef DIRECT_FB
                slave_window_k(cmd);         /* slice blit + row-region compose */
#endif
            } else if (cmd & 0x0040) {
#ifdef SPR_LIST
                { extern volatile uint8_t spr_chain_id; spr_chain_id++; }   /* card J (a): a new chain, a new list */
#endif
                SYNC[13] = 1;            /* SNAP LATCH (2026-08-26): a
                                          * compose chain is reading
                                          * SPR_SNAP — text_capture must
                                          * NOT refresh it mid-chain, or
                                          * half the rows draw from frame
                                          * N and half from N+1: Mike's
                                          * "Zeus split in half,
                                          * shimmering top or bottom".
                                          * Cleared after the echo. */
                /* SELF-CHAIN (2026-08-25): one command = all three
                 * bands, back to back. The old master-relaunch-per-
                 * echo chain cost a full poll-loop round trip per
                 * link; the profiler showed the slave idling 34% of
                 * the frame WAITING for those relaunches while the
                 * chain "overloaded" 52% of cycles. The per-link
                 * semantics (cat1 recorded at each link, drained at
                 * the next link's head, R2 inline under WIN_TWO) are
                 * unchanged — same function, three calls. */
#ifdef BLIT_CHASE
                if (SYNC[4])             /* our blit half ships BEFORE
                                          * any compose write: sbuf
                                          * rows 0-135 are still the
                                          * closed generation */
                    slave_service_stream();
#endif
                st_s(2);
                slave_concurrent_k((uint16_t)(cmd & ~0x0070));
                st_s(13);                /* 255: text + service since cat1 -> STB[3] */
                st_s(3);
#ifdef BLIT_CHASE
                slave_fence(108 + BAND_SHIFT);   /* band 1 ends inside
                                                  * the master's half */
#endif
                slave_concurrent_k((uint16_t)((cmd & ~0x0070) | 0x10));
                st_s(13);                /* 255: text + service since cat1 -> STB[3] */
                st_s(3);
#ifdef BLIT_CHASE
                slave_fence(224);
#endif
                slave_concurrent_k((uint16_t)((cmd & ~0x0070) | 0x20));
                st_s(13);                /* 255: text + service since cat1 -> STB[3] */
                st_s(3);
            } else {
                /* master-relaunched chain: the latch must span the
                 * WHOLE chain, not one link — the k2 refresh lands in
                 * the gap BETWEEN links. Open at R0, close after R2. */
                if (((cmd >> 4) & 3) == 0)
    #ifdef SPR_LIST
                { extern volatile uint8_t spr_chain_id; spr_chain_id++; }   /* card J (a): a new chain, a new list */
#endif
                SYNC[13] = 1;
#ifdef CHAIN_METER
                {
                    uint16_t bt0 = frt_s();
                    SCM_ENTRY();
                    slave_concurrent_k(cmd);
                    SCM[0] += (uint16_t)(frt_s() - bt0);   /* true busy ticks */
                    SCM[1] += 1;
                }
#else
                slave_concurrent_k(cmd);     /* concurrent band compose */
#endif
            }
            ((volatile uint32_t *)0x26028C80)[0] +=
                (uint16_t)(frt_s() - gw0);
            if ((cmd & 0xF000) == 0x2000
                && ((cmd & 0x0040) || ((cmd >> 4) & 3) == 2))
                SYNC[13] = 0;                /* chain complete (R2 or ALL):
                                              * snapshot may refresh again */
#ifdef QUEUED_CHAIN
            /* QUEUED-CHAIN PULL: R1/R2 wait in SYNC[10]/[11]; take the
             * next at a gate moment instead of waiting a master round
             * trip (measured 163 lines each). Gate arms:
             *   QPULL=0 pull immediately
             *   QPULL=1 pull only while FM=1 (68K quietly polling)
             *   QPULL=2 pull only while FM=0 (68K in its own window) */
            while ((cmd & 0xF000) == 0x2000 && (SYNC[10] | SYNC[11])) {
#if QPULL == 1
                if (!(MARS_SYS_INTMSK & 0x8000)) {
                    slave_service_stream();
                    uint16_t tq = frt_s();
                    while ((uint16_t)(frt_s() - tq) < 16) ;
                    continue;
                }
#elif QPULL == 2
                if (MARS_SYS_INTMSK & 0x8000) {
                    slave_service_stream();
                    uint16_t tq = frt_s();
                    while ((uint16_t)(frt_s() - tq) < 16) ;
                    continue;
                }
#endif
                {
                    uint16_t nc = SYNC[10];
                    if (nc) SYNC[10] = 0; else { nc = SYNC[11]; SYNC[11] = 0; }
#ifdef CHAIN_METER
                    {
                        uint16_t bt0 = frt_s();
                        SCM_ENTRY();
                        slave_concurrent_k(nc);
                        SCM[0] += (uint16_t)(frt_s() - bt0);
                        SCM[1] += 1;
                    }
#else
                    slave_concurrent_k(nc);
#endif
                    if (((nc >> 4) & 3) == 2)
                        SYNC[13] = 0;        /* chain closed at R2 */
                    SYNC[1] = nc;            /* echo the LATEST link */
                }
            }
            if ((cmd & 0xF000) != 0x2000)
                SYNC[1] = cmd;               /* non-chain cmds echo as ever */
#else
            SYNC[1] = cmd;                   /* done */
#endif
            /* (launch-latency meter retired 2026-08-26: its number is
             * banked — 161-176 lines/link across three runs — and the
             * guard needed the bytes for the phase meter.) */
            while (SYNC[0] == cmd) {         /* wait master clear; keep the */
                slave_service_stream();      /* stream alive meanwhile */
#ifdef R60
                /* paced like the idle branch — this spin holds at
                 * vint top too when a band completes near the edge */
                {
                    uint16_t tq = frt_s();
                    while ((uint16_t)(frt_s() - tq) < 16) ;
                }
#endif
            }
        } else {
            /* SLAVE IDLE METER (LOOP16, motion-parity lane): counts
             * no-command poll visits. The one number that says whether
             * the second SH-2 has unused capacity to carry more compose
             * rows (or a higher cutscene cadence). Fixed scrap 0x28FA8
             * (free per the LOOP14 map; CUT_BLANK counters end at
             * 0x28FA8). Relative meter — compare builds/scenes, not an
             * absolute clock. Boot-zeroed in m_main's init (the master
             * zeroes it: the slave may start polling first). */
            /* THROTTLED 64:1 — an uncached write per poll would be bus
             * traffic exactly when the DMAC wants quiet (the poisoned-
             * packet class). Register-local count, spill every 64. */
            {
                static uint32_t sidle_l;
                if (!(++sidle_l & 63))
                    (*(volatile uint32_t *)0x26028FA8) += 64;
            }
            slave_service_stream();
#ifdef R60
            /* PACED IDLE (pass 9): the master's tight-COMM-spin
             * finding, applied to the slave at last — an idle slave
             * hammering SYNC (SDRAM) + the stream mailbox (adapter
             * bus) contends with the DMAC's FIFO drain at exactly
             * push time. Under the pass-8b rebalance the slave is
             * idle MORE at vint top and the tear rate doubled (dreq
             * misaligned 3.6% -> 6.4%). FRT between polls, zero bus:
             * ~16 ticks per visit. Command pickup latency cost is
             * <0.4 lines against pickup budgets of thousands. */
            {
                uint16_t tq = frt_s();
                while ((uint16_t)(frt_s() - tq) < 16) ;
            }
#endif
        }
    }
}
