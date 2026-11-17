/* opm.c -- YM2151 channel engine; see opm.h for scope, spec and the gate. */
#include "opm.h"
#include "opm_tables.h"

/* OPM_CODE places the engine's functions (SH-2: .ramtext, SDRAM) -- defined by the build */
#ifndef OPM_CODE
#define OPM_CODE
#endif
/* OPM_RAM places the engine's small tables in SDRAM (.data): a never-written static with an
 * initialiser is promoted to .rodata by gcc, and .rodata is the cart, which the engine must
 * not read beside the game (RV=1) and should not read in any hot path */
#ifndef OPM_RAM
#define OPM_RAM
#endif

/* envelope states and rules from jt51_eg.v (state codes as there) */
enum { EG_ATTACK, EG_DECAY1, EG_DECAY2, EG_RELEASE };

/* register offset of each op slot: M1, C1, M2, C2 live at +0, +16, +8, +24 */
OPM_RAM static uint8_t op_slot[4] = { 0, 16, 8, 24 };

/* DT2 as key-code fractions: 600, 781, 950 cents in 1/64 semitones */
OPM_RAM static int16_t dt2_delta[4] = { 0, 384, 500, 608 };

/* The SH-2 has no barrel shifter: a shift by a variable is a libgcc call, and libgcc links
 * into the cart, which the engine must never fetch from beside the game (RV=1). x >> s is
 * computed as the high word of x * 2^(32 - s) (dmulu.l, 2 cycles), from these RAM tables. */
static uint32_t shr16_mul[13];      /* (x << 16) >> s for s = 0..12: 2^(16 - s) */
static uint32_t shr24_mul[8];       /* (x << 24) >> c for c = 0..7:  2^(8 - c) */
static uint32_t pow2_ram[13];       /* 2^s */
static uint32_t shr31_mul[8];       /* (x << 1) >> s for s = 0..7:   2^(31 - s) */
static uint8_t  idx_ram[2][4];      /* jt51_eg.v's step patterns: [0] slow rates, [1] fast */

static inline __attribute__((always_inline)) uint32_t mulhi(uint32_t a, uint32_t b)
{
	return (uint32_t)(((uint64_t)a * b) >> 32);
}



OPM_CODE static uint32_t key_step(const opm_chip *c, uint32_t block_freq, int32_t delta)
{
	uint32_t block = (block_freq >> 10) & 7;
	/* the 4-bit note code is gappy (12 notes over 16 codes): drop every fourth */
	uint32_t code = ((block_freq >> 6) & 15) - ((block_freq >> 8) & 3);
	int32_t f = (int32_t)((code << 6) | (block_freq & 63)) + delta;

	if (f >= 768) {
		f -= 768;
		if (f >= 768) { block++; f -= 768; }
		if (block++ >= 7) return c->step_table[767];
	}
	return mulhi(c->step_table[f] << 1, shr31_mul[block ^ 7]);
}

/* jt51_pg.v: the key code after DT2 -- {octave, note, KF} with gap codes stepped over --
 * which feeds DT1 and the envelope's key scaling as well as the phase */
OPM_CODE static uint32_t keycode_dt2(uint32_t kc, uint32_t kf, uint32_t dt2)
{
	OPM_RAM static uint16_t add[4] = { 0, 512, 628, 800 };
	uint32_t k;

	if ((kc & 3) == 3) kc++;                 /* jt51_pm.v: a gap code means the next note */
	if (kc > 127) return 0x1fbf;             /* the carry saturates to {7, 14, 63} */
	k = (kc << 6) | kf;
	if ((dt2 == 2 && (k & 0xff) > 75) || (dt2 == 3 && (k & 0xff) > 95)) k += 64;
	return k + add[dt2];
}

/* DT1 as jt51_pg.v computes it: a 5-bit power-of-two ramp over the key code, limited */
OPM_CODE static int32_t detune1(uint32_t k6, uint32_t dt1)
{
	OPM_RAM static uint8_t pow2[8] = { 16, 17, 19, 20, 22, 24, 26, 29 };
	OPM_RAM static int8_t kadj[4] = { 0, -4, 4, 8 };
	OPM_RAM static uint8_t limit[4] = { 0, 8, 16, 22 };
	uint32_t k, hi, v;

	if ((dt1 & 3) == 0) return 0;
	k = (k6 + (uint32_t)kadj[dt1 & 3]) & 63;
	hi = k >> 3;
	v = pow2[k & 7];
	v = hi <= 4 ? mulhi(v << 16, shr16_mul[4 - hi]) : hi == 5 ? v << 1 : 0;
	if (v > limit[dt1 & 3]) v = limit[dt1 & 3];
	return (dt1 & 4) ? -(int32_t)v : (int32_t)v;
}

/* recompute one operator's cached step, levels and rates from the registers */
OPM_CODE static void cache_op(opm_chip *c, int k, int n)     /* slot k = channel c->chs[k] */
{
	int ch = c->chs[k];
	opm_op *o = &c->ch[k].op[n];
	const uint8_t *r = c->reg;
	uint32_t s = op_slot[n] + ch;
	uint32_t block_freq = ((uint32_t)(r[0x28 + ch] & 0x7f) << 6) | (r[0x30 + ch] >> 2);
	uint32_t dt2 = r[0xc0 + s] >> 6;
	uint32_t k2 = keycode_dt2(r[0x28 + ch] & 0x7f, r[0x30 + ch] >> 2, dt2);
	uint32_t mul = (r[0x40 + s] & 15) * 2;
	int32_t det = detune1((k2 >> 8) & 63, (r[0x40 + s] >> 4) & 7);
	uint32_t step, sl;

	if (mul == 0) mul = 1;
	step = ((key_step(c, block_freq, dt2_delta[dt2]) + (uint32_t)det) * mul) >> 1;
	if (c->rate_hz == 0)
		o->step = step << 6;
	else
		o->step = c->env_kb && c->env_rem == 0 ? step * (c->env_kb << 6)   /* 15625 and 31250 divide 4 MHz: no libgcc division */
		        : (uint32_t)(((uint64_t)step * c->clock_hz) / c->rate_hz);

	o->tl = (uint32_t)((r[0x60 + s] & 0x7f) << 3);
	{ uint32_t l = o->att + o->tl; o->tla = (l > 0x3ff ? 0x3ff : l) << 2; }
	if (n == 0) { opm_ch *h = &c->ch[k]; uint8_t con = r[0x20 + ch]; h->alg = con & 7; h->fbl = (con >> 3) & 7; h->on = (con >> 6) & 3; }
	/* D1L 15 is 48 dB in jt51_eg.v (d1level_II <= 5'h10), not the 93 dB of ymfm */
	sl = r[0xe0 + s] >> 4;
	o->d1l = (uint8_t)(sl == 15 ? 16 : sl);
	o->ks = (uint8_t)mulhi(((k2 >> 8) & 31) << 16, shr16_mul[(r[0x80 + s] >> 6) ^ 3]);
	o->ar = r[0x80 + s] & 31;
	o->d1r = r[0xa0 + s] & 31;
	o->d2r = r[0xc0 + s] & 31;
	o->rr = (uint8_t)(((r[0xe0 + s] & 15) << 1) | 1);
}

/* one envelope update as jt51_eg.v pipelines it: choose the state and rate from the level
 * the slot carries in, then step it. Returns the shift of the counter field the rate uses
 * (for the caller's schedule) with bit 8 set when anything changed. */
OPM_CODE static uint32_t eg_update(opm_op *o, uint32_t eg_cnt)
{
	uint32_t att = o->att, cfg, rate, g, shift, cnt, idx, step, sum_up;
	uint32_t att0 = att, state0 = o->state;
	int keyon_now = o->kon && (!o->klast || o->kflip);
	int keyoff_now = !o->kon && o->klast;
	int ar_off = 0;

	o->klast = o->kon;
	o->kflip = 0;
	if (keyoff_now) {
		o->state = EG_RELEASE;
		cfg = o->rr;
	} else if (keyon_now) {
		o->state = EG_ATTACK;
		cfg = o->ar;
		ar_off = o->ar == 31;
		o->phase = 0u - o->step;    /* jt51 loads 0 this sample instead of adding */
	} else {
		switch (o->state) {
		case EG_ATTACK:
			if (att == 0) { o->state = EG_DECAY1; cfg = o->d1r; }
			else cfg = o->ar;
			break;
		case EG_DECAY1:
			if ((att >> 5) >= o->d1l) { o->state = EG_DECAY2; cfg = o->d2r; }
			else cfg = o->d1r;
			break;
		case EG_DECAY2: cfg = o->d2r; break;
		default:        cfg = o->rr; break;
		}
	}
	/* key scaling adds even to a zero rate (pre_rate_III has no zero test) */
	rate = cfg * 2 + o->ks;
	if (rate > 63) rate = 63;
	g = rate >> 2;
	if (o->state == EG_ATTACK) shift = g >= 11 ? 0 : 11 - g;
	else                       shift = g >= 12 ? 0 : 12 - g;
	cnt = mulhi(eg_cnt << 16, shr16_mul[shift]) & 7;
	if (g >= 12) idx = (g == 15 && o->state == EG_ATTACK) ? 0xff : idx_ram[1][rate & 3];
	else         idx = (g == 0 && o->state != EG_ATTACK) ? 0xfe : idx_ram[0][rate & 3];
	step = g == 0 ? 0 : mulhi(idx << 24, shr24_mul[cnt]) & 1;
	sum_up = (cnt & 1) != o->cnt0;
	o->cnt0 = (uint8_t)(cnt & 1);

	if (ar_off) {
		att = 0;
	} else if (o->state == EG_ATTACK) {
		if (sum_up && att != 0) {
			/* jt51 compares rate_VI[5:1] with 5'hf: rates 30-31 attack at once */
			if ((rate >> 1) == 15) {
				att = 0;
			} else {
				uint32_t s0 = (g == 13 ? att >> 3 : g >= 14 ? att >> 2 : att >> 4) + 1;
				uint32_t sum = g >= 12 ? (step ? s0 << 1 : s0) : (step ? s0 : 0);
				att = sum < att ? att - sum : 0;
			}
		}
	} else if (sum_up) {
		if (att <= 1015) {
			switch (g) {
			case 12: att += step ? 2 : 1; break;
			case 13: att += step ? 4 : 2; break;
			case 14: att += step ? 8 : 4; break;
			case 15: att += 8; break;
			default: att += step ? 2 : 0; break;
			}
		} else {
			att = 0x3ff;
		}
	}
	o->att = att;
	{ uint32_t l = att + o->tl; o->tla = (l > 0x3ff ? 0x3ff : l) << 2; }
	return shift | ((keyon_now || keyoff_now || att != att0 || o->state != state0) ? 256 : 0);
}

/* the sine and the pre-shifted power tables live in RAM (SDRAM on the SH-2): the cart bus
 * must not be in the hot loop. pow_sh[e][m] = opm_pow[m] >> e; from e = 13 up it is 0. */
/* pow_flat[a] = opm_pow[a & 255] >> (a >> 8), one index and no split; it runs to the largest
 * a = max(opm_sin) + max(tla) = 0x859 + 0xffc so the lookup needs no range branch (zero from
 * 13 * 256 up: the operator is inaudible there). 12.5 KB, the same SDRAM the code runs from. */
#define POW_FLAT_N 0x1900
#ifdef OPM_TABLES_CONST
/* beside the game the SDRAM is full: the tables come from the cart as constants, byte for byte
 * what opm_init fills below (tools/opm_tables_flat.py); the SH-2 caches them like its code */
#include "opm_tables_flat.h"
#ifdef OPM_TABLES_SDRAM
/* OPM_TABLES_SDRAM (2026-11-01): the two per-SAMPLE tables copied into SDRAM at init, the
 * per-register-write step table left in the cart. On the FPGA rig every cart read is a wait
 * the emulators do not charge: with the tables in the cart the lane made 2800-4300 of the
 * 8000 samples/s the PWM plays (lane12tp). 8 KB exactly, the room R60PTONE made: the pow
 * table stops at 12 x 256, its last row held 0 and 1 of 2047 (below -66 dB). */
#define POW_ROWS 12
static uint16_t sin_ram[1024];
static uint16_t pow_flat[POW_ROWS * 256];
static uint8_t  opm_tables_filled;
#else
#define POW_ROWS 13
#define sin_ram  opm_sin1024
#define pow_flat opm_pow_flat
#endif
#else
static uint16_t sin_ram[1024];              /* the quarter-wave log-sine unfolded over the whole period (magnitude; the sign is bit 9) */
static uint16_t pow_flat[POW_FLAT_N];
#endif

static inline __attribute__((always_inline)) int32_t op_out(const opm_op *o, uint32_t phase)
{
	uint32_t i = phase & 1023;
#if defined(OPM_TABLES_CONST) && defined(OPM_TABLES_UNCACHED)
	/* OPM_TABLES_UNCACHED (2026-11-01, rig probe): the two tables read through the SH-2's uncached
	 * window so their 8 KB never evict this code from the 4 KB cache (lane13tc: 2100 cycles a sample
	 * on the rig against ~650 of instructions) */
	uint32_t a = ((const volatile uint16_t *)((uintptr_t)sin_ram | 0x20000000u))[i] + o->tla;
	int32_t v = a < POW_ROWS * 256 ? (int32_t)((const volatile uint16_t *)((uintptr_t)pow_flat | 0x20000000u))[a] : 0;
#elif defined(OPM_TABLES_CONST)
	uint32_t a = sin_ram[i] + o->tla;                  /* the const table is POW_ROWS x 256: range-checked */
	int32_t v = a < POW_ROWS * 256 ? (int32_t)pow_flat[a] : 0;
#else
	int32_t v = (int32_t)pow_flat[sin_ram[i] + o->tla];
#endif
	return (i & 512) ? -v : v;
}

static inline int32_t clamp16(int32_t v)
{
	return v > 32767 ? 32767 : v < -32768 ? -32768 : v;
}

/* feedback: (prev1 + prev2) >> (10 - fb); the SH-2 has no barrel shifter, so the shift is a
 * constant per case instead of a libgcc call per sample */
static uint32_t fb_mul[8];                  /* 2^(32 - (10 - fbl)); 0 for no feedback */
#ifndef OPM_TABLES_CONST
static uint32_t step_ram[768];              /* opm_step, in SDRAM: cache_op reads it on every register write */
#else
#define step_ram opm_step_flat
#endif

static inline __attribute__((always_inline)) int32_t fb_mod(const opm_ch *h)
{
	/* s >> (10 - fbl), arithmetic, as the high word of a signed 32x32 multiply (dmuls.l):
	 * gcc's constant shifts by 3..9 were calls into libgcc helpers in the cart */
	int32_t s = h->fb[0] + h->fb[1];
	return (int32_t)(((int64_t)s * (int32_t)fb_mul[h->fbl]) >> 32);
}

/* One channel sample, one straight-line chain per algorithm (no link tables, no scratch
 * array: that glue was ~380 instructions a channel-sample). The links keep jt51's pipeline
 * delays, measured bit-identical 2026-10-31: C1 and M2 read the PREVIOUS pass's M1 (fb[1]),
 * algorithm 5's M2 the one before that (fb[0]); M2 and C2 read the previous pass's C1
 * (c1_prev); C2 reads this pass's M1 and M2; the output sums this pass's operators. */
OPM_CODE static int32_t ch_out(opm_ch *h)
{
#ifdef OPM_BENCH_SKIP_OUT           /* cost attribution only (sndtest/sh/opm_bench.c) */
	(void)h; return 0;
#endif
	int32_t m1, c1, m2, r;
	int32_t p1 = h->fb[1], c1o = h->c1_prev;
	m1 = op_out(&h->op[0], (h->op[0].phase >> 16) + (uint32_t)fb_mod(h));
	h->fb_in = (int16_t)m1;
	switch (h->alg) {
	default:
	case 0:
		c1 = op_out(&h->op[1], (h->op[1].phase >> 16) + (uint32_t)(p1 >> 1));
		m2 = op_out(&h->op[2], (h->op[2].phase >> 16) + (uint32_t)(c1o >> 1));
		r  = op_out(&h->op[3], (h->op[3].phase >> 16) + (uint32_t)(m2 >> 1));
		break;
	case 1:
		c1 = op_out(&h->op[1], h->op[1].phase >> 16);
		m2 = op_out(&h->op[2], (h->op[2].phase >> 16) + (uint32_t)((p1 + c1o) >> 1));
		r  = op_out(&h->op[3], (h->op[3].phase >> 16) + (uint32_t)(m2 >> 1));
		break;
	case 2:
		c1 = op_out(&h->op[1], h->op[1].phase >> 16);
		m2 = op_out(&h->op[2], (h->op[2].phase >> 16) + (uint32_t)(c1o >> 1));
		r  = op_out(&h->op[3], (h->op[3].phase >> 16) + (uint32_t)((m1 + m2) >> 1));
		break;
	case 3:
		c1 = op_out(&h->op[1], (h->op[1].phase >> 16) + (uint32_t)(p1 >> 1));
		m2 = op_out(&h->op[2], h->op[2].phase >> 16);
		r  = op_out(&h->op[3], (h->op[3].phase >> 16) + (uint32_t)((c1o + m2) >> 1));
		break;
	case 4:
		c1 = op_out(&h->op[1], (h->op[1].phase >> 16) + (uint32_t)(p1 >> 1));
		m2 = op_out(&h->op[2], h->op[2].phase >> 16);
		r  = op_out(&h->op[3], (h->op[3].phase >> 16) + (uint32_t)(m2 >> 1));
		r  = clamp16(r + c1);
		break;
	case 5:
		c1 = op_out(&h->op[1], (h->op[1].phase >> 16) + (uint32_t)(p1 >> 1));
		m2 = op_out(&h->op[2], (h->op[2].phase >> 16) + (uint32_t)(h->fb[0] >> 1));
		r  = op_out(&h->op[3], (h->op[3].phase >> 16) + (uint32_t)(m1 >> 1));
		r  = clamp16(r + c1); r = clamp16(r + m2);
		break;
	case 6:
		c1 = op_out(&h->op[1], (h->op[1].phase >> 16) + (uint32_t)(p1 >> 1));
		m2 = op_out(&h->op[2], h->op[2].phase >> 16);
		r  = op_out(&h->op[3], h->op[3].phase >> 16);
		r  = clamp16(r + c1); r = clamp16(r + m2);
		break;
	case 7:
		c1 = op_out(&h->op[1], h->op[1].phase >> 16);
		m2 = op_out(&h->op[2], h->op[2].phase >> 16);
		r  = op_out(&h->op[3], h->op[3].phase >> 16);
		r  = clamp16(r + m1); r = clamp16(r + c1); r = clamp16(r + m2);
		break;
	}
	h->c1_prev = (int16_t)c1;
	return r;
}

OPM_CODE void opm_init(opm_chip *c, uint32_t clock_hz, uint32_t rate_hz, uint8_t chmask)
{
	uint8_t *p = (uint8_t *)c;
	unsigned i;
	int ch, n;

	for (i = 0; i < sizeof(*c); i++) p[i] = 0;
	for (ch = 0; ch < 8; ch++) c->reg[0x20 + ch] = 0xc0;
	for (ch = 0; ch < OPM_MAXCH; ch++) {
		for (n = 0; n < 4; n++) {
			c->ch[ch].op[n].att = 0x3ff;
			c->ch[ch].op[n].tla = 0x3ff << 2;
			c->ch[ch].op[n].state = EG_RELEASE;
		}
	}
	c->clock_hz = clock_hz;
	c->rate_hz = rate_hz;
	c->chmask = chmask;
	for (ch = 0; ch < 8; ch++) c->chidx[ch] = 0xff;
	for (ch = 0, i = 1; ch < 8; ch++, i <<= 1)
		if ((chmask & i) && c->nch < OPM_MAXCH) { c->chidx[ch] = c->nch; c->chs[c->nch++] = (uint8_t)ch; }
#ifndef OPM_TABLES_CONST
	for (i = 0; i < 768; i++) step_ram[i] = opm_step[i];
#endif
	c->step_table = step_ram;
	c->dirty = 1;
#ifndef OPM_TABLES_CONST
	for (i = 0; i < 1024; i++)
		sin_ram[i] = opm_sin[((i & 256) ? ~i : i) & 255];
	for (i = 0; i < POW_FLAT_N; i++)
		pow_flat[i] = (uint16_t)(i < 13 * 256 ? opm_pow[i & 255] >> (i >> 8) : 0);
#endif
#ifdef OPM_TABLES_SDRAM
	if (!opm_tables_filled) {                          /* once: opm_init runs at every song start */
		for (i = 0; i < 1024; i++) sin_ram[i] = opm_sin1024[i];
		for (i = 0; i < POW_ROWS * 256; i++) pow_flat[i] = opm_pow_flat[i];
		opm_tables_filled = 1;
	}
#endif
	for (ch = 0; ch < OPM_MAXCH; ch++) c->ch[ch].silent = 1;
	for (i = 0; i < 13; i++) { shr16_mul[i] = 1u << (16 - i); pow2_ram[i] = 1u << i; }
	for (i = 0; i < 8; i++) shr24_mul[i] = 1u << (8 - i);
	for (i = 0; i < 8; i++) shr31_mul[i] = 1u << (31 - i);
	for (i = 1; i < 8; i++) fb_mul[i] = 1u << (22 + i);
	fb_mul[0] = 0;
	{
		static const uint8_t idx_fast[4] = { 0x00, 0x88, 0xaa, 0xee };
		static const uint8_t idx_slow[4] = { 0xaa, 0xea, 0xee, 0xfe };
		for (i = 0; i < 4; i++) { idx_ram[0][i] = idx_slow[i]; idx_ram[1][i] = idx_fast[i]; }
	}
	c->env_step = rate_hz * 64;
	if (rate_hz) { c->env_kb = clock_hz / c->env_step; c->env_rem = clock_hz - c->env_kb * c->env_step; }
}

OPM_CODE void opm_write(opm_chip *c, uint8_t reg, uint8_t val)
{
	int ch, n;
	c->reg[reg] = val;
	c->dirty = 1;
	for (ch = 0; ch < c->nch; ch++)
		for (n = 0; n < 4; n++) c->ch[ch].op[n].wake = c->t;
	c->next_wake = c->t;
	if (reg == 0x08) {
		/* the chip's 64-clock write busy puts a sample between a key-off and the key-on
		 * after it; at a lower output rate both can land in one sample, so a key-off is
		 * remembered until the envelope has seen it and the key-on still retriggers */
		uint8_t k = c->chidx[val & 7];
		opm_ch *h;
		int n;
		uint8_t bit = 8;
		if (k == 0xff) return;                 /* not this engine's channel */
		h = &c->ch[k];
		for (n = 0; n < 4; n++, bit <<= 1) {
			uint8_t k = (val & bit) != 0;
			if (h->op[n].kon && !k) h->op[n].kflip = 1;
			h->op[n].kon = k;
		}
	}
}

/* THE ENVELOPE SCHEDULE (2026-10-31, hybrid 2). Updating every operator every sample cost
 * ~900 of 1951 SH-2 cycles a sample for two voices, and almost all of it is no-ops: an
 * attenuation can only move when the counter-field bit the rate selects flips, every
 * 3 * 2^shift samples, and a state can only move at the update after a step or a key
 * event. So after any change an operator takes three more full updates (a key-on's
 * attack -> decay1 -> decay2 settles in that many), then sleeps until the bit flips;
 * released and silent, it sleeps until a write. Register writes wake every operator.
 * Sample-exact: the gate (tools/opm_ab.py, opm_compare.py) reads the same as before. */
/* one chip sample on the envelope clock: jt51_eg.v's counter steps every third sample */
static inline __attribute__((always_inline)) void eg_advance(opm_chip *c)
{
	c->t++;
	if (c->eg_base == 2) { c->eg_cnt = (uint16_t)((c->eg_cnt + 1) & 0x7fff); c->eg_base = 0; }
	else c->eg_base++;
}

OPM_CODE static void eg_tick(opm_chip *c)
{
	int ch, n;
	uint32_t nw;
#ifdef OPM_BENCH_SKIP_EG            /* cost attribution only (sndtest/sh/opm_bench.c) */
	(void)c; return;
#endif
	eg_advance(c);
	if ((int32_t)(c->t - c->next_wake) < 0) return;         /* nothing due: the chip-wide earliest wake */
	nw = c->t + 0x40000000u;
	for (ch = 0; ch < c->nch; ch++) {
		opm_ch *h = &c->ch[ch];
		uint32_t quiet = 1;
		for (n = 0; n < 4; n++) {
			opm_op *o = &h->op[n];
			uint32_t r, shift, next;
			if ((int32_t)(c->t - o->wake) < 0) {
				if ((int32_t)(o->wake - nw) < 0) nw = o->wake;
				quiet &= (o->state == EG_RELEASE && o->att == 0x3ff);
				continue;
			}
			if (c->t - o->last > 1) {
				/* woken after sleeping: cnt0 must be what an update one sample ago would
				 * have left, the bit of the counter then under the field it slept with --
				 * a released, silent operator sleeps through flips and reads stale here */
				uint32_t prev = c->eg_cnt - (c->eg_base == 0 ? 1 : 0);
				o->cnt0 = (uint8_t)(mulhi(prev << 16, shr16_mul[o->shift]) & 1);
			}
			r = eg_update(o, c->eg_cnt);
			o->last = c->t;
			o->shift = (uint8_t)(r & 255);
			if (r & 256) o->settle = 3;
			if (o->settle) { o->settle--; o->wake = c->t + 1; if ((int32_t)(o->wake - nw) < 0) nw = o->wake; quiet = 0; continue; }
			if (o->state == EG_RELEASE && o->att == 0x3ff) { o->wake = c->t + 0x40000000u; continue; }
			quiet = 0;
			/* the next sample at which bit `shift` of the counter flips */
			shift = r & 255;
			next = pow2_ram[shift] - (c->eg_cnt & (pow2_ram[shift] - 1));  /* counter ticks away */
			o->wake = c->t + (3 - c->eg_base) + 3 * (next - 1);
			if ((int32_t)(o->wake - nw) < 0) nw = o->wake;
		}
		h->silent = (uint8_t)quiet;
	}
	c->next_wake = nw;
}

OPM_CODE int32_t opm_sample(opm_chip *c)
{
	int32_t sum = 0;
	int k, n;

	if (c->dirty) {
		for (k = 0; k < c->nch; k++)
			for (n = 0; n < 4; n++) cache_op(c, k, n);
		c->dirty = 0;
	}
	if (c->rate_hz == 0) {
		eg_tick(c);
	} else {
		/* k chip samples are owed this output sample (4 at 15.6 kHz). When none of them
		 * reaches the chip-wide wake, they are k counter advances and nothing else: the
		 * same k early returns eg_tick would have taken, without the calls */
		uint32_t k = c->env_kb;
		c->env_acc += c->env_rem;
		if (c->env_acc >= c->env_step) { c->env_acc -= c->env_step; k++; }
		if ((int32_t)(c->t + k - c->next_wake) < 0) {
			/* k counter advances at once: eg_cnt steps every third chip sample */
			uint32_t b = c->eg_base + k, q = 0;
			while (b >= 3) { b -= 3; q++; }
			c->eg_cnt = (uint16_t)((c->eg_cnt + q) & 0x7fff);
			c->eg_base = (uint8_t)b;
			c->t += k;
		} else {
			while (k--) eg_tick(c);
		}
	}
	for (k = 0; k < c->nch; k++) {
		opm_ch *h = &c->ch[k];
		/* a channel whose four operators are released and fully attenuated outputs 0 from
		 * every operator; once its link delays hold zeros too, nothing it computes can be
		 * observed: the chain is a no-op and the phases reload at the next key-on */
		if (h->silent && !(h->fb[0] | h->fb[1] | h->fb_in | h->c1_prev))
			continue;
		h->fb[0] = h->fb[1];
		h->fb[1] = h->fb_in;
		h->op[0].phase += h->op[0].step; h->op[1].phase += h->op[1].step;
		h->op[2].phase += h->op[2].step; h->op[3].phase += h->op[3].step;
		if (h->on & 1) sum += ch_out(h);
	}
	return sum;
}
