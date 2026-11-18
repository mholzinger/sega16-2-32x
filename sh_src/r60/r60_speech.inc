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
#include <stdint.h>
#include "../speech_tab.h"
#include "../sfx_tab.h"
#include "../sh2zam/adpcm.h"   /* SH2ZAM adpcm-1: exact IMA decode (sh_src/sh2zam/README.md) */
extern const unsigned char sfx_blob[];      /* sh_src/sound_data.s, cart .sfxdata */
#define SP_VOICES 4

#define SP_RATE        16000
#define SP_SCALE       1
#define SAMPLE_MIN     (8 * SP_SCALE)
#define SAMPLE_MAX     (1430 * SP_SCALE)
#define SAMPLE_CENTER  ((SAMPLE_MAX + SAMPLE_MIN) / 2)
#define SOFT_HIGH      (1290 * SP_SCALE)
#define SOFT_LOW       (148 * SP_SCALE)
#define SP_SHIFT       (SP_SCALE == 2 ? 5 : 6)   /* 16-bit PCM -> the duty range */
#define SP_SIDES       2
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
static volatile uint8_t sp_needv[SP_SIDES]; /* side i drained: the slave's IRQ writes 1, the master's fill writes 0 (always through UC) */
volatile uint32_t speech_census[8];         /* [0] dma irqs [1] fills [2] underruns [3] plays [4] active [5] last cmd [6] stops [7] init */

struct sp_req { uint8_t serial, sample, vol, flags; };   /* flags: bit7 stop, bit0 = SFX bank */
static struct sp_req sp_req[SP_VOICES];
struct sp_voice { const uint8_t *adp; uint32_t nibbles, pos; sh2z_ima_state_t st; uint8_t last_serial, hold, active, rate; };
static struct sp_voice vcs[SP_VOICES];
#define vc vcs[0]


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
	if (sfx_map[cmd] != 0xFF) {                  /* SFX voices 1..3, round robin: the oldest is taken */
		static unsigned rr;
		v = 1u + (rr++ % (SP_VOICES - 1u));
		sp_post(v, sfx_map[cmd], 1);
		UC(speech_census[3])++;
	}
}

/* ---- slave side ------------------------------------------------------------------- */
/* THE FILL RUNS ON THE MASTER (sw243). sw239 pumped from the slave's loop (-0.035 flips/vint),
 * sw240-242 decoded inside the slave's DMA IRQ (-0.006 speech only, -0.017 with effects, which
 * fire through the whole attract). The slave is the renderer's pole; the master idles until the
 * next vint after its tick, so it decodes there (r60_main.c m_main, the pp_due wait). The IRQ on
 * the slave only swaps, re-arms and raises the drained side's flag. */
static uint8_t sp_silent; static uint16_t sp_fpos; static uint8_t sp_fside;   /* tentative: defined below */
static uint16_t sp_irqv;                   /* the vint count at the last drain: a side has 3.8 vints */
SPCODE void amb_dma_handler(void)
{
	uint8_t drained = sp_cur;
	sp_cur = (uint8_t)((sp_cur + 1u) % SP_SIDES);
	UC(speech_census[0])++;
	if (UC(sp_needv[sp_cur])) UC(speech_census[2])++;   /* the side starting now was never refilled (or not yet marked silent) */
	sp_irqv = (uint16_t)R60_DIAG[0];
	{   /* the slave owns the voices here: a REAL underrun is a side with sound due that is not done */
		unsigned v, act = 0; for (v = 0; v < SP_VOICES; v++) act |= vcs[v].active;
		if (UC(sp_needv[sp_cur]) && (act || !(sp_silent & (1u << sp_cur)) || (sp_fpos < SP_BUF && sp_fside == sp_cur))) UC(speech_census[2]) += 0x10000u;
	}
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
		if (r->flags & 1u) { const struct speech_ent *e = &sfx_tab[r->sample]; x->adp = sfx_blob + e->off; x->nibbles = e->nibbles; x->rate = SFX_HOLD; }
		else
		{ const struct speech_ent *e = &speech_tab[r->sample]; x->adp = speech_blob + e->off; x->nibbles = e->nibbles; x->rate = SP_HOLD; }
		x->pos = 0; x->st.pred = 0; x->st.index = 0; x->hold = 0; x->active = 1;
	}
	{ unsigned m = 0; for (v = 0; v < SP_VOICES; v++) if (vcs[v].active) m |= 1u << v; UC(speech_census[4]) = m; }
}

/* Decode up to n nibbles of a voice into out[] (one predictor per nibble) with SH2ZAM's exact
 * IMA kernel; returns how many were decoded. The voice ends after its last nibble, as the
 * old per-nibble sp_step did (an active voice always decodes at least one: a 0-nibble entry
 * played one nibble there too). */
#define SP_RUN 32                           /* nibbles per decode run: 64 B of stack per voice */
static SPCODE unsigned sp_run(struct sp_voice *x, int16_t *out, unsigned n)
{
	uint32_t left = x->nibbles > x->pos ? x->nibbles - x->pos : 1u;
	if (n > left) n = left;
	sh2z_ima_decode(&x->st, x->adp, x->pos, out, n);
	x->pos += n;
	if (x->pos >= x->nibbles) x->active = 0;
	return n;
}

/* One voice at unity: |pred >> 6| <= 512 keeps every sample inside the lab's soft-clip band
 * (148..1290 around 719), so the single-voice path has no clip and writes a nibble's two
 * 16 kHz samples directly (speech is an 8 kHz bank). With more than one voice (SFX over
 * speech) the lab's sum + soft clip runs; a voice's `rate` is how many output samples each
 * nibble covers. A side is refilled with silence ONCE after the last voice ends and then
 * left alone (sp_silent): an idle lane costs nothing (sw238 refilled every 64 ms, -0.1
 * flips/vint). */
static uint8_t sp_silent = (1u << SP_SIDES) - 1u;   /* bit i = side i holds silence already (speech_init wrote them all) */
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
		int16_t run[SP_RUN];
		while (i < SP_BUF && x->active) {
			unsigned want = (unsigned)(SP_BUF - i + SP_HOLD - 1) / SP_HOLD, k, n;
			n = sp_run(x, run, want < SP_RUN ? want : SP_RUN);
			for (k = 0; k < n; k++) {
				uint16_t out = (uint16_t)(SAMPLE_CENTER + (run[k] >> SP_SHIFT));
				buf[i] = out; buf[i + 1] = out; i += SP_HOLD;
			}
		}
	} else if (nact > 1) {
		/* each voice decodes a run, then the runs are mixed sample by sample: the same
		 * sums as stepping the voices in turn per sample (they are independent), and a
		 * sample is written while any voice still had a nibble, as before */
		int16_t run[SP_VOICES][SP_RUN];
		unsigned len[SP_VOICES];
		while (i < SP_BUF) {
			unsigned want = (unsigned)(SP_BUF - i + SP_HOLD - 1) / SP_HOLD, most = 0, k;
			if (want > SP_RUN) want = SP_RUN;
			for (v = 0; v < SP_VOICES; v++) {
				len[v] = vcs[v].active ? sp_run(&vcs[v], run[v], want) : 0u;
				if (len[v] > most) most = len[v];
			}
			for (k = 0; k < most; k++) {
				int32_t mix = 0, s;
				for (v = 0; v < SP_VOICES; v++) if (k < len[v]) mix += run[v][k] >> SP_SHIFT;
				s = SAMPLE_CENTER + mix;
				if (s > SOFT_HIGH) s = SOFT_HIGH + ((s - SOFT_HIGH) >> 2);
				else if (s < SOFT_LOW) s = SOFT_LOW - ((SOFT_LOW - s) >> 2);
				if (s > SAMPLE_MAX) s = SAMPLE_MAX; else if (s < SAMPLE_MIN) s = SAMPLE_MIN;
				buf[i] = (uint16_t)s; buf[i + 1] = (uint16_t)s; i += SP_HOLD;
			}
			if (most < want) break;          /* every voice ended inside this run */
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
#define SP_CHUNK 64
static uint16_t sp_fpos = SP_BUF;          /* the side being filled, the next sample; SP_BUF = none */
static uint8_t  sp_fside;
static SPCODE void sp_fill_range(uint16_t *buf, int i, int end)
{
	unsigned nact = 0, v, only = 0;
	for (v = 0; v < SP_VOICES; v++) if (vcs[v].active) { nact++; only = v; }
	if (nact == 1) {
		struct sp_voice *x = &vcs[only];
		int16_t run[SP_RUN];
		while (i < end && x->active) {
			unsigned want = (unsigned)(end - i + SP_HOLD - 1) / SP_HOLD, k, n;
			n = sp_run(x, run, want < SP_RUN ? want : SP_RUN);
			for (k = 0; k < n; k++) {
				uint16_t out = (uint16_t)(SAMPLE_CENTER + (run[k] >> SP_SHIFT));
				buf[i] = out; buf[i + 1] = out; i += SP_HOLD;
			}
		}
	} else if (nact > 1) {
		/* each voice decodes a run, then the runs are mixed sample by sample: the same
		 * sums as stepping the voices in turn per sample (they are independent), and a
		 * sample is written while any voice still had a nibble, as before */
		int16_t run[SP_VOICES][SP_RUN];
		unsigned len[SP_VOICES];
		while (i < end) {
			unsigned want = (unsigned)(end - i + SP_HOLD - 1) / SP_HOLD, most = 0, k;
			if (want > SP_RUN) want = SP_RUN;
			for (v = 0; v < SP_VOICES; v++) {
				len[v] = vcs[v].active ? sp_run(&vcs[v], run[v], want) : 0u;
				if (len[v] > most) most = len[v];
			}
			for (k = 0; k < most; k++) {
				int32_t mix = 0, s;
				for (v = 0; v < SP_VOICES; v++) if (k < len[v]) mix += run[v][k] >> SP_SHIFT;
				s = SAMPLE_CENTER + mix;
				if (s > SOFT_HIGH) s = SOFT_HIGH + ((s - SOFT_HIGH) >> 2);
				else if (s < SOFT_LOW) s = SOFT_LOW - ((SOFT_LOW - s) >> 2);
				if (s > SAMPLE_MAX) s = SAMPLE_MAX; else if (s < SAMPLE_MIN) s = SAMPLE_MIN;
				buf[i] = (uint16_t)s; buf[i + 1] = (uint16_t)s; i += SP_HOLD;
			}
			if (most < want) break;          /* every voice ended inside this run */
		}
	}
	for (; i < end; i++) buf[i] = SAMPLE_CENTER;
}
SPCODE void speech_pump_chunk(void)
{
	unsigned v, act;
	if (sp_fpos >= SP_BUF) {                    /* no side in progress: start the first drained one, in play order */
		unsigned k;
		for (k = 1; k < SP_SIDES; k++) {
			unsigned b = (sp_cur + k) % SP_SIDES;
			if (!UC(sp_needv[b])) continue;
			sp_consume();
			act = 0; for (v = 0; v < SP_VOICES; v++) act |= vcs[v].active;
			if (act || !(sp_silent & (1u << b))) { sp_fside = (uint8_t)b; sp_fpos = 0; break; }
			UC(sp_needv[b]) = 0;                /* silence already there */
		}
		if (sp_fpos >= SP_BUF) return;
	}
	{
		/* two vints after the drain with the side not done: the rest now (the spins were short);
		 * with the synth lane a side is a 512-sample quarter of the ring with three sides of lead,
		 * and a burst would be ~15 ms of the slave: no burst, an unfinished side is an underrun */
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

/* Mars_InitPWM (d32xr marshw.c lineage via 32x-builder, as in the lab): three MONO writes
 * flush the FIFO, CYCLE = clock / rate, CTRL 0x0185 = mono, right = pulse, DREQ + IRQ on
 * FIFO empty; the ramp DC-biases the speaker so the first sample does not pop. */
SPCODE void speech_init(void)
{
	int sample = SAMPLE_MIN, b, i;
	sp_cur = 0; for (b = 0; b < SP_SIDES; b++) UC(sp_needv[b]) = 0;
	for (b = 0; b < SP_SIDES; b++) for (i = 0; i < SP_BUF; i++) sp_buf[b][i] = SAMPLE_CENTER;
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
