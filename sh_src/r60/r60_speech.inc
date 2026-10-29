/* r60_speech.inc -- THE SPEECH LANE (compiled INTO r60_main.c, see its tail: as a separate unit
 * every call into it was opaque and gcc reloaded the caller's whole live state around it, +860 B
 * in m_main, .ramtext overflowed) (R60SPEECH=1): the sound lab's SH-2 PWM mixer
 * (sndtest/sh/sound.c, Mike's ear 2026-09-01 "sounds correct") on the r60 slave, one voice,
 * playing the arcade's uPD7759 utterances from the cart (sh_src/speech.bin: the lab's bank
 * re-baked at 8 kHz IMA ADPCM by tools/speech_bake.py, cut by tools/speech_blob.py; the 7759
 * itself never exceeds 8 kHz, so nothing is lost; each nibble is held for two 16 kHz mixer
 * samples). The master posts a sound command from the port's MCU block (speech_cmd), the
 * slave decodes into a 2 x 1024-sample ping-pong that DMA1 drains into the PWM FIFO; the
 * DMA-complete IRQ (mars_start.s slav_dma_irq -> amb_dma_handler) swaps and re-arms.
 * MAME 32X SEGFAULTS on PWM-DREQ DMA1 (docs/sound/SOUND.md), so this lane is gated on ares
 * counters (speech_census) and the rig, never in MAME. */
#ifdef R60_SPEECH
#include <stdint.h>
#include "../speech_tab.h"
#ifdef R60_SFX
#include "../sfx_tab.h"
extern const unsigned char sfx_blob[];      /* sh_src/sound_data.s, cart .sfxdata */
#define SP_VOICES 4
#else
#define SP_VOICES 1
#endif

#define SP_RATE        16000
#define SAMPLE_MIN     8
#define SAMPLE_MAX     1430
#define SAMPLE_CENTER  ((SAMPLE_MAX + SAMPLE_MIN) / 2)
#define SOFT_HIGH      1290
#define SOFT_LOW       148
#define SP_BUF         1024                 /* 64 ms a side: the lab's starvation-proof size */
#define SP_HOLD        2                    /* bank rate 8 kHz -> mixer 16 kHz */
#define UC(x) (*(volatile __typeof__(x) *)((uintptr_t)&(x) | 0x20000000u))
#define SPCODE __attribute__((noinline))   /* cart-resident (.text): .ramtext (0x31000-0x38000) is full; a 64 ms side is ~1 ms of cached cart code */

extern const unsigned char speech_blob[];   /* sh_src/sound_data.s, cart */

/* The ping-pong lives at a fixed SDRAM address: .bss must end under 0x06019000 (the renderer's
 * region block). 0x29800-0x2A800 is the top of the gap between the renderer's mailbox page
 * (0x28000-0x29000) and the port's stack (.portstack 0x2A800, grows down from 0x2C800): on ares
 * sw236 writes nothing in 0x29000-0x2A800 through 3500 frames but four 0xFF bytes at 0x29400,
 * which this range clears. (sw237 put it at 0x3C400 = PRIO_MASK, r60_main.c: the priority row
 * masks were overwritten and the rig showed striped pupils and grey sprite blocks.) */
#define sp_buf ((uint16_t (*)[SP_BUF])0x26029800u)   /* uncached alias: the master's stores reach memory before the slave's DMA reads them */
static volatile uint8_t sp_cur;
static volatile uint8_t sp_needv[2];        /* side i drained: the slave's IRQ writes 1, the master's fill writes 0 (always through UC) */
volatile uint32_t speech_census[8];         /* [0] dma irqs [1] fills [2] underruns [3] plays [4] active [5] last cmd [6] stops [7] init */

struct sp_req { uint8_t serial, sample, vol, flags; };   /* flags: bit7 stop, bit0 = SFX bank */
static struct sp_req sp_req[SP_VOICES];
struct sp_voice { const uint8_t *adp; uint32_t nibbles, pos; int32_t pred, idx; uint8_t last_serial, hold, active, rate; };
static struct sp_voice vcs[SP_VOICES];
#define vc vcs[0]

static const uint16_t ima_step[89] = {
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
	34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
	157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
	598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707,
	1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
	5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
	15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 };
static const int8_t ima_index[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

/* ---- master side: the port's sound command ---------------------------------------- */
static SPCODE void sp_post(unsigned v, unsigned sample, unsigned flags)
{
	volatile struct sp_req *r = &UC(sp_req[v]);
	r->sample = (uint8_t)sample; r->vol = 255; r->flags = (uint8_t)flags;
	r->serial++;                                 /* publish last: the doorbell */
}
void speech_cmd(unsigned cmd)
{
	unsigned v;
	UC(speech_census[5]) = cmd;
	if (cmd == 0u) {                             /* the arcade's stop-all byte */
		for (v = 0; v < SP_VOICES; v++) sp_post(v, 0, 0x80);
		UC(speech_census[6])++; return;
	}
	if (cmd > 0xFFu) return;
	if (speech_map[cmd] != 0xFF) { sp_post(0, speech_map[cmd], 0); UC(speech_census[3])++; return; }
#ifdef R60_SFX
	if (sfx_map[cmd] != 0xFF) {                  /* SFX voices 1..3, round robin: the oldest is taken */
		static unsigned rr;
		v = 1u + (rr++ % (SP_VOICES - 1u));
		sp_post(v, sfx_map[cmd], 1);
		UC(speech_census[3])++;
	}
#endif
}

/* ---- slave side ------------------------------------------------------------------- */
/* THE FILL RUNS ON THE MASTER (sw243). sw239 pumped from the slave's loop (-0.035 flips/vint),
 * sw240-242 decoded inside the slave's DMA IRQ (-0.006 speech only, -0.017 with effects, which
 * fire through the whole attract). The slave is the renderer's pole; the master idles until the
 * next vint after its tick, so it decodes there (r60_main.c m_main, the pp_due wait). The IRQ on
 * the slave only swaps, re-arms and raises the drained side's flag. */
#ifdef R60_SPEECHSLAVE
static uint8_t sp_silent; static uint16_t sp_fpos; static uint8_t sp_fside;   /* tentative: defined below */
static uint16_t sp_irqv;                   /* the vint count at the last drain: a side has 3.8 vints */
#endif
SPCODE void amb_dma_handler(void)
{
	uint8_t drained = sp_cur;
	sp_cur ^= 1;
	UC(speech_census[0])++;
	if (UC(sp_needv[sp_cur])) UC(speech_census[2])++;   /* the side starting now was never refilled (or not yet marked silent) */
#ifdef R60_SPEECHSLAVE
	sp_irqv = (uint16_t)R60_DIAG[0];
	{   /* the slave owns the voices here: a REAL underrun is a side with sound due that is not done */
		unsigned v, act = 0; for (v = 0; v < SP_VOICES; v++) act |= vcs[v].active;
		if (UC(sp_needv[sp_cur]) && (act || !(sp_silent & (1u << sp_cur)) || (sp_fpos < SP_BUF && sp_fside == sp_cur))) UC(speech_census[2]) += 0x10000u;
	}
#endif
	SH2_DMA_SAR1  = (uint32_t)(uintptr_t)sp_buf[sp_cur];
	SH2_DMA_TCR1  = SP_BUF;
	SH2_DMA_CHCR1 = 0x14E5;
	UC(sp_needv[drained]) = 1;
}

static SPCODE void sp_consume(void)
{
	unsigned v;
	for (v = 0; v < SP_VOICES; v++) {
		volatile struct sp_req *r = &UC(sp_req[v]);
		struct sp_voice *x = &vcs[v];
		uint8_t sn = r->serial;
		if (sn == x->last_serial) continue;
		x->last_serial = sn;
		if (r->flags & 0x80) { x->active = 0; continue; }
#ifdef R60_SFX
		if (r->flags & 1u) { const struct speech_ent *e = &sfx_tab[r->sample]; x->adp = sfx_blob + e->off; x->nibbles = e->nibbles; x->rate = SFX_HOLD; }
		else
#endif
		{ const struct speech_ent *e = &speech_tab[r->sample]; x->adp = speech_blob + e->off; x->nibbles = e->nibbles; x->rate = SP_HOLD; }
		x->pos = 0; x->pred = 0; x->idx = 0; x->hold = 0; x->active = 1;
	}
	{ unsigned m = 0; for (v = 0; v < SP_VOICES; v++) if (vcs[v].active) m |= 1u << v; UC(speech_census[4]) = m; }
}

/* Decode one nibble of a voice into its predictor; returns 0 when the voice ends. */
static SPCODE int sp_step(struct sp_voice *x)
{
	uint8_t b = x->adp[x->pos >> 1];
	int nib = (x->pos & 1) ? (b >> 4) : (b & 0x0F);
	int step = ima_step[x->idx];
	int delta = step >> 3;
	if (nib & 4) delta += step;
	if (nib & 2) delta += step >> 1;
	if (nib & 1) delta += step >> 2;
	x->pred += (nib & 8) ? -delta : delta;
	if (x->pred > 32767) x->pred = 32767; else if (x->pred < -32768) x->pred = -32768;
	x->idx += ima_index[nib];
	if (x->idx < 0) x->idx = 0; else if (x->idx > 88) x->idx = 88;
	if (++x->pos >= x->nibbles) { x->active = 0; return 0; }
	return 1;
}

/* One voice at unity: |pred >> 6| <= 512 keeps every sample inside the lab's soft-clip band
 * (148..1290 around 719), so the single-voice path has no clip and writes a nibble's two
 * 16 kHz samples directly (speech is an 8 kHz bank). With more than one voice (SFX over
 * speech) the lab's sum + soft clip runs; a voice's `rate` is how many output samples each
 * nibble covers. A side is refilled with silence ONCE after the last voice ends and then
 * left alone (sp_silent): an idle lane costs nothing (sw238 refilled every 64 ms, -0.1
 * flips/vint). */
static uint8_t sp_silent = 3;               /* bit i = side i holds silence already (speech_init wrote both) */
#if defined(R60_SFX) && SFX_HOLD != SP_HOLD
#error "the mixer steps every voice once per SP_HOLD output samples: bake the SFX bank at the speech rate"
#endif
/* Every bank is 8 kHz (SP_HOLD 2): all active voices step once per TWO output samples and
 * the pair is written at once, so the mix loop runs 512 times a side, not 1024 (sw241 ran
 * per output sample and cost 0.025 flips/vint in the attract, where effects fire constantly). */
static SPCODE void sp_fill(uint16_t *buf)
{
	int i = 0;
	unsigned nact = 0, v, only = 0;
	for (v = 0; v < SP_VOICES; v++) if (vcs[v].active) { nact++; only = v; }
	if (nact == 1) {
		struct sp_voice *x = &vcs[only];
		while (i < SP_BUF && x->active) {
			uint16_t out;
			sp_step(x);
			out = (uint16_t)(SAMPLE_CENTER + (x->pred >> 6));
			buf[i] = out; buf[i + 1] = out; i += SP_HOLD;
		}
	} else if (nact > 1) {
		for (; i < SP_BUF; i += SP_HOLD) {
			int32_t mix = 0, s;
			unsigned any = 0;
			for (v = 0; v < SP_VOICES; v++) {
				struct sp_voice *x = &vcs[v];
				if (!x->active) continue;
				sp_step(x);
				mix += x->pred >> 6; any = 1;
			}
			if (!any) break;
			s = SAMPLE_CENTER + mix;
			if (s > SOFT_HIGH) s = SOFT_HIGH + ((s - SOFT_HIGH) >> 2);
			else if (s < SOFT_LOW) s = SOFT_LOW - ((SOFT_LOW - s) >> 2);
			if (s > SAMPLE_MAX) s = SAMPLE_MAX; else if (s < SAMPLE_MIN) s = SAMPLE_MIN;
			buf[i] = (uint16_t)s; buf[i + 1] = (uint16_t)s;
		}
	}
	for (; i < SP_BUF; i++) buf[i] = SAMPLE_CENTER;
}

/* master side: decode the drained side(s); voices and requests live on the master only */
SPCODE void speech_pump(void)
{
	unsigned b;
	sp_consume();
	for (b = 0; b < 2; b++) {
		uint8_t bit = (uint8_t)(1u << b);
		unsigned v, act = 0;
		if (!UC(sp_needv[b])) continue;
		for (v = 0; v < SP_VOICES; v++) act |= vcs[v].active;
		if (act || !(sp_silent & bit)) {
			sp_fill(sp_buf[b]);
			act = 0; for (v = 0; v < SP_VOICES; v++) act |= vcs[v].active;
			if (act) sp_silent &= (uint8_t)~bit; else sp_silent |= bit;
			UC(speech_census[1])++;
		}
		UC(sp_needv[b]) = 0;
	}
}

#ifdef R60_SPEECHSLAVE
/* THE FILL ON THE SLAVE, IN CHUNKS (R60SPEECHSLAVE, 2026-10-24). With the two-core sprite split the
 * master is the pole of the fight (tick ~60 + band ~80 + planes prepare ~120 lines in a building
 * vint) and a side's fill put up to ~80 more lines on it every ~4 vints (sw313 timeline, fight
 * 600-700); the slave idles between its window and the 68K's span close and spins on the master's
 * band. sw239 pumped whole sides from the slave's loop when the slave was the pole (-0.035). Here
 * the slave fills SP_CHUNK output samples per call, from its idle spins only (the GO wait at the
 * loop top and R60_WAITBODY), so a call never holds the slave more than a few lines. Same samples:
 * requests are taken at a side's start as before (sp_consume), voices step in the same order, and a
 * chunk that finds one voice where the side began with two writes what the mixer would (one voice
 * never reaches the soft clip: |pred >> 6| <= 512). A side has 64 ms (3.8 vints) before it plays. */
#ifndef SP_CHUNK
#define SP_CHUNK 64
#endif
static uint16_t sp_fpos = SP_BUF;          /* the side being filled, the next sample; SP_BUF = none */
static uint8_t  sp_fside;
static SPCODE void sp_fill_range(uint16_t *buf, int i, int end)
{
	unsigned nact = 0, v, only = 0;
	for (v = 0; v < SP_VOICES; v++) if (vcs[v].active) { nact++; only = v; }
	if (nact == 1) {
		struct sp_voice *x = &vcs[only];
		while (i < end && x->active) {
			uint16_t out;
			sp_step(x);
			out = (uint16_t)(SAMPLE_CENTER + (x->pred >> 6));
			buf[i] = out; buf[i + 1] = out; i += SP_HOLD;
		}
	} else if (nact > 1) {
		for (; i < end; i += SP_HOLD) {
			int32_t mix = 0, s;
			unsigned any = 0;
			for (v = 0; v < SP_VOICES; v++) {
				struct sp_voice *x = &vcs[v];
				if (!x->active) continue;
				sp_step(x);
				mix += x->pred >> 6; any = 1;
			}
			if (!any) break;
			s = SAMPLE_CENTER + mix;
			if (s > SOFT_HIGH) s = SOFT_HIGH + ((s - SOFT_HIGH) >> 2);
			else if (s < SOFT_LOW) s = SOFT_LOW - ((SOFT_LOW - s) >> 2);
			if (s > SAMPLE_MAX) s = SAMPLE_MAX; else if (s < SAMPLE_MIN) s = SAMPLE_MIN;
			buf[i] = (uint16_t)s; buf[i + 1] = (uint16_t)s;
		}
	}
	for (; i < end; i++) buf[i] = SAMPLE_CENTER;
}
SPCODE void speech_pump_chunk(void)
{
	unsigned v, act;
	if (sp_fpos >= SP_BUF) {                    /* no side in progress: start the first drained one */
		unsigned b;
		for (b = 0; b < 2u; b++) {
			if (!UC(sp_needv[b])) continue;
			sp_consume();
			act = 0; for (v = 0; v < SP_VOICES; v++) act |= vcs[v].active;
			if (act || !(sp_silent & (1u << b))) { sp_fside = (uint8_t)b; sp_fpos = 0; break; }
			UC(sp_needv[b]) = 0;                /* silence already there */
		}
		if (sp_fpos >= SP_BUF) return;
	}
	{
		/* two vints after the drain with the side not done: the rest now (the spins were short) */
		unsigned b = sp_fside, end = (uint16_t)((uint16_t)R60_DIAG[0] - sp_irqv) >= 2u ? SP_BUF : sp_fpos + SP_CHUNK;
		if (end > SP_BUF) end = SP_BUF;
		sp_fill_range(sp_buf[b], (int)sp_fpos, (int)end);
		sp_fpos = (uint16_t)end;
		if (end >= SP_BUF) {
			act = 0; for (v = 0; v < SP_VOICES; v++) act |= vcs[v].active;
			if (act) sp_silent &= (uint8_t)~(1u << b); else sp_silent |= (uint8_t)(1u << b);
			UC(speech_census[1])++;
			UC(sp_needv[b]) = 0;
		}
	}
}
#endif

/* Mars_InitPWM (d32xr marshw.c lineage via 32x-builder, as in the lab): three MONO writes
 * flush the FIFO, CYCLE = clock / rate, CTRL 0x0185 = mono, right = pulse, DREQ + IRQ on
 * FIFO empty; the ramp DC-biases the speaker so the first sample does not pop. */
SPCODE void speech_init(void)
{
	int sample = SAMPLE_MIN, b, i;
	sp_cur = 0; UC(sp_needv[0]) = 0; UC(sp_needv[1]) = 0;
	for (b = 0; b < 2; b++) for (i = 0; i < SP_BUF; i++) sp_buf[b][i] = SAMPLE_CENTER;
	MARS_PWM_MONO = 1; MARS_PWM_MONO = 1; MARS_PWM_MONO = 1;
	MARS_PWM_CYCLE = (uint16_t)((((23011361 << 1) / SP_RATE + 1) >> 1) + 1);
	MARS_PWM_CTRL = 0x0185;
	while (sample < SAMPLE_CENTER) {
		int reps = (SP_RATE * 2) / (SAMPLE_CENTER - SAMPLE_MIN), ix;
		for (ix = 0; ix < reps; ix++) { while (MARS_PWM_MONO & 0x8000) ; MARS_PWM_MONO = (uint16_t)sample; }
		sample++;
	}
	SH2_DMA_DAR1  = (uint32_t)(uintptr_t)&MARS_PWM_MONO;
	SH2_DMA_DRCR1 = 0;
	SH2_DMA_DMAOR = 1;
	SH2_INT_IPRA  = (uint16_t)((SH2_INT_IPRA & 0xF0FFu) | 0x0400u);   /* DMAC priority 4 */
	SH2_DMA_VCR1  = 66;                                               /* slav_irq -> slav_dma_irq */
	SH2_DMA_SAR1  = (uint32_t)(uintptr_t)sp_buf[0];
	SH2_DMA_TCR1  = SP_BUF;
	SH2_DMA_CHCR1 = 0x14E5;
	UC(speech_census[7]) = 1;
}
#endif /* R60_SPEECH */
