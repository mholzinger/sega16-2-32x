/* opm.h -- a YM2151 (OPM) channel engine in portable integer C, for the SH-2.
 *
 * WHY (2026-10-30, hybrid 1). The YM2612 has 6 FM channels and the arcade's YM2151 music
 * uses 8, so two parts were squashed onto PSG squares. The hybrid plan keeps the YM2612
 * for 6 and synthesizes the other two here, mixed into the 32X PWM -- which ares does not
 * low-pass (m32x/pwm.cpp:52, measured), so they keep the arcade's brightness.
 *
 * Scope is what Altered Beast's channels 6/7 use (census over all 8 songs): phase from
 * KC/KF/DT1/DT2/MUL, the envelope with key scaling, the 8 algorithms and feedback. No LFO
 * (PMS/AMS 0 everywhere) and no noise (reg 0x0F bit 7 never set); both are ignored.
 *
 * The spec is jt51 (srcref/jtcores/modules/jt51, the YM2151 RTL the arcade core runs;
 * GPL, derived from, never copied). tools/opm_ab.py renders the same register stream
 * through jt51 under Verilator and through this, sample by sample at the native rate.
 *
 * Rates: opm_init(clock_hz, rate_hz, ...) with rate_hz = 0 runs at the chip's own clock/64
 * (System 16B clocks its YM2151 at 4 MHz, jts16b_snd.v:24, so 62.5 kHz); any
 * other rate scales the phase steps and runs the envelope clock off a fractional
 * accumulator, so one call = one output sample. Feedback stays a one-sample delay.
 *
 * Measured against jt51 (tools/opm_ab.py; the test sequence of tools/opm_testseq.py):
 *   native   2026-10-31: every operator link, every algorithm, feedback 0-7 and every
 *            envelope/pitch test are BIT-IDENTICAL to jt51 (SNR 200 dB) once the
 *            pipeline's link delays are modelled (see ch_out) and both sides land writes
 *            on the same samples (tools/opm_ab.py, THE WRITE CONVENTION): 0 of 97 tests
 *            flagged on jt51's ROM step table, all 8 songs within 0.12 dB per 0.25 s
 *            window. One thing remains and it is not logic: the formula step table is
 *            up to 2.7 cents off the chip's ROM (4 pitch tests fail on the formula, none
 *            on the ROM). Write timing matters on the chip itself: whether a decay takes
 *            its first step depends on the envelope counter's phase (period 3 samples) at
 *            the key-on, and jt51_kon.v keys the four operators across two passes when a
 *            write lands mid-pass; a real driver's phase is arbitrary, so per note the
 *            board itself varies by up to 8 attenuation units (0.75 dB at D1R 31).
 *   lower    level within 0.32 dB at 31.25/22.05/16 kHz, but rendering straight at the
 *   rates    lower rate aliases the bright feedback voices: Rise, Power Up, Big Bad Wolf
 *            run 2-6 dB hot above 1 kHz even at 31.25 kHz. The rate is hybrid 2's call.
 *
 * SH-2 cost (2026-10-31, sndtest/sh/opm_bench.c on ares, which charges instruction cycles
 * only): two feedback-7 voices = 1951 cycles a sample at 62.5 kHz (5.3 SH-2s), 2844 at
 * 31.25 kHz (the envelope ticks twice per output sample). Attribution: envelope ~900
 * (eight eg_update calls, ~112 each), operator output ~775 (~97 an operator), overhead ~280.
 * The envelope cost is avoidable without changing a sample: attenuation only moves when the
 * counter-field bit flips (every 3 * 2^shift samples), so updates can be scheduled.
 * After the schedule, op_out inlined and the tables in RAM (all gate-exact): 1068 a sample
 * at 62.5 kHz (2.9 SH-2s), 1325 at 31.25 kHz (1.8). ares --profile: no hot spot left, the
 * cost is spread glue (~380 instructions a channel-sample); the next 2x is a restructure.
 * Where that leaves the hybrid: the sound cart can afford two voices at 31.25 kHz on its
 * idle master; beside the game it cannot run at any rate without that restructure and a
 * lower rate, which is a design decision, not a tweak.
 *
 * Hybrid 2b (2026-10-31, soundlab c07eedac..): the restructure, in four exact steps, each
 * byte-identical to the build before it (tools/opm_identity.sh: the 97-test sequence and
 * Rise, native and 15625 Hz). Per-algorithm straight-line chains and a chip-wide wake
 * (1067 -> 647 at 62.5 kHz); the sine unfolded, the power table flat and branchless, a
 * silent channel skipped, the idle chip ticks batched (885 -> 663 at 15.6 kHz); the
 * bookkeeping cut; every variable shift replaced by a multiply-high (the SH-2 has no
 * barrel shifter: gcc called libgcc helpers in the CART for them, and for its constant
 * shifts by 3..9) and every table the engine reads moved to SDRAM (OPM_RAM), so no
 * per-sample or per-write path touches the cart (RV=1 beside the game). Only opm_init and
 * the general-rate step division (unreached at 15625 / 31250) still link to libgcc.
 * The in-game shape is a song replay (tools/opm_bench.sh, SONG=): at 15625 Hz on ares,
 * channels 6+7 of Rise 392, Power Up 773, Boss 774, Big Bad Wolf 602, Closed In 602
 * cycles a sample = 0.27-0.53 of one SH-2; synthetic 2ch@15.6k 601, 2ch@62.5k 552.
 * Attribution on Power Up: bookkeeping ~230, envelope ~220, chain ~320. A linear sine with
 * a hardware multiply would save ~24 of the ~155-cycle chain (the lookups are a third of
 * it) for 7% overall at the price of bit-exactness: not taken.
 */
#ifndef OPM_H
#define OPM_H
#include <stdint.h>

typedef struct {
	uint32_t phase;         /* 10.16: bits 16-25 index the sine */
	uint32_t step;          /* per output sample, same format */
	uint32_t att;           /* envelope attenuation, 10 bits */
	uint32_t tl;            /* total level << 3 */
	uint32_t tla;           /* min(att + tl, 0x3ff) << 2: the op's level term, kept by whoever moves att or tl */
	uint8_t  state;         /* jt51's ATTACK, DECAY1, DECAY2, RELEASE */
	uint8_t  kon, klast;    /* key as written, key as the envelope last saw it */
	uint8_t  kflip;         /* a write keyed it off since the envelope last looked */
	uint8_t  cnt0;          /* bit 0 of the envelope counter field it last used */
	uint8_t  ar, d1r, d2r, rr;   /* 5-bit rate configs; rr is jt51's {RR,1} */
	uint8_t  ks;            /* keycode >> (3 - KS) */
	uint8_t  d1l;           /* compared against att >> 5 */
	uint8_t  settle;        /* full updates still owed after a change */
	uint8_t  shift;         /* counter field the last update's rate used */
	uint32_t wake;          /* chip sample of the next full envelope update */
	uint32_t last;          /* chip sample of the last full envelope update */
} opm_op;

typedef struct {
	opm_op   op[4];         /* M1, C1, M2, C2 (ymfm's op1..op4) */
	int16_t  fb[2];
	int16_t  fb_in;
	int16_t  c1_prev;       /* C1's last output: what M2/C2 modulate from (jt51_op.v prev2) */
	uint8_t  alg, fbl, on;   /* reg 0x20's connection, feedback and R|L, cached (opm_sample reads no registers) */
	uint8_t  silent;        /* every operator released and fully attenuated (eg_tick keeps it): the chain outputs 0 */
} opm_ch;

/* OPM_MAXCH: how many channels one chip computes (the slots of ch[]); the chip's channel
 * numbers map onto them through chidx[] (0xff = not this engine's). 8 on the host and in the
 * sound cart; 2 beside the game, where SDRAM is full (2026-10-31). */
#ifndef OPM_MAXCH
#define OPM_MAXCH 8
#endif

typedef struct {
	uint8_t  reg[256];
	opm_ch   ch[OPM_MAXCH];
	uint8_t  chmask;        /* channels this engine computes */
	uint8_t  nch;           /* those channels, listed */
	uint8_t  chs[8];        /* slot k computes channel chs[k] */
	uint8_t  chidx[8];      /* channel n is slot chidx[n], or 0xff */
	uint8_t  dirty;
	uint8_t  eg_base;       /* counts samples 0..2 */
	uint16_t eg_cnt;        /* the envelope counter, +1 every 3 samples (jt51_eg.v) */
	uint32_t t;             /* chip samples so far: the envelope's wake clock */
	uint32_t next_wake;     /* the earliest op wake: eg_tick skips its loops before it */
	uint32_t clock_hz;
	uint32_t rate_hz;       /* 0 = native */
	uint32_t env_acc;       /* native samples owed to the envelope clock, x rate*64 */
	uint32_t env_step;      /* rate_hz * 64: one chip sample's worth of env_acc */
	uint32_t env_kb, env_rem;   /* clock_hz = env_kb * env_step + env_rem: whole chip samples per output sample, and the remainder env_acc collects */
	const uint32_t *step_table;  /* 768 entries; opm_step unless a test swaps it */
} opm_chip;

void    opm_init(opm_chip *c, uint32_t clock_hz, uint32_t rate_hz, uint8_t chmask);
void    opm_write(opm_chip *c, uint8_t reg, uint8_t val);
int32_t opm_sample(opm_chip *c);        /* one output sample, left output, unclipped */

#endif
