/* sndtest PCM lane: SH-2 PWM voice-pool mixer (docs/sound/SOUND.md P1).
 *
 * The core transport — Mars_InitPWM, the DMA1 ping-pong, the IRQ
 * handler, CHCR1/CTRL values, buffer sizing — is 32x-builder
 * sh_src/sound.c (same author, lifted with its hardware comments; that
 * file's history holds the underrun postmortem the sizes come from).
 * The per-game 7-voice pump is replaced by a generic voice pool over a
 * baked sample bank: every sample is IMA ADPCM at the mixer's own
 * 16 kHz, so decode IS mix — no rings, no resample cursors.
 *
 * Cross-CPU contract: primary and secondary run one linked image, so a
 * shared static reached through the 0x20000000 cache-through alias is a
 * mailbox both sides agree on. sfx_play() posts {serial, voice, sample,
 * vol, flags}; the pump consumes on serial change. One outstanding
 * request per voice slot; the lab needs no queue.
 */
#include "mars.h"
#include "sound.h"
#include "speech_bank.h"

#define SND_MIX_RATE 16000

/* PWM duty-cycle compare range [8..1430], center 719 — widened from
 * d32xr's [2..1032] for ~38% more mix headroom (32x-builder). */
#define SAMPLE_MIN 8
#define SAMPLE_MAX 1430
#define SAMPLE_CENTER ((SAMPLE_MAX + SAMPLE_MIN) / 2)

/* Soft-clip at ~80% of the budget: 4:1 compression toward the rails,
 * hard clip as the final backstop. */
#define SOFT_HIGH 1290
#define SOFT_LOW   148

/* Ping-pong SDRAM buffers, 1024 samples = 64 ms each. SIZED FOR
 * STARVATION: 2x256 (16 ms) underran whenever the busy CPU stalled the
 * pump for one long stretch; 64 ms per side plus pump checkpoints is
 * the proven shape. DMA reads cached SDRAM; SH-2 cache is write-through
 * so the mixer's stores reach memory before DMA does. */
#define SND_SAMPLES_PER_BUF 1024

static uint16_t snd_pwm_buf[2][SND_SAMPLES_PER_BUF]
                            __attribute__((aligned(16)));
static volatile uint8_t snd_current_buf_idx;
static volatile uint8_t snd_buf_needs_fill;   /* bit i = buf i needs fill */

/* Cache-through alias: both SH-2s see one coherent copy. */
#define UC(x) (*(volatile __typeof__(x) *)((uintptr_t)&(x) | 0x20000000))

static uint16_t snd_underruns_storage;

/* Per-voice trigger mailbox (primary writes, secondary consumes). */
struct sfx_req {
	uint8_t serial;   /* bumped per post; consume on change */
	uint8_t sample;
	uint8_t vol;      /* 0..255, 255 = unity */
	uint8_t flags;    /* bit0 = loop, bit7 = stop */
};
static struct sfx_req sfx_reqs[SND_MAX_VOICES];

/* Secondary-local voice state (never touched by the primary). */
struct voice {
	const uint8_t *adp;     /* nibble-packed IMA stream */
	uint32_t nibbles;       /* total nibble count */
	uint32_t pos;           /* nibble cursor */
	int32_t  pred;          /* IMA predictor */
	int32_t  idx;           /* IMA step index */
	uint8_t  last_serial;
	uint8_t  vol;
	uint8_t  loop;
	uint8_t  active;
};
static struct voice voices[SND_MAX_VOICES];

/* Standard IMA tables — the matched pair of tools/speech_bake.py. */
static const uint16_t ima_step_table[89] = {
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
	34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
	157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
	598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707,
	1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
	5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
	15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};
static const int8_t ima_index_table[16] = {
	-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8
};

/* Mars_InitPWM — d32xr marshw.c lineage via 32x-builder. Three MONO
 * writes flush the FIFO; CYCLE = SH-2 clock / rate (NTSC 23.0114 MHz —
 * PAL needs 22.8015 MHz, unvalidated); CTRL = 0x0185:
 *   [1:0] TM  = 01 -> mono output
 *   [3:2] RMD = 01 -> right = pulse
 *   [5:4] LMD = 00 -> left  = same as right (mono mode)
 *   [7]   AL  = 1  -> DREQ enable on FIFO-empty
 *   [8]       = 1  -> PWM interrupt enable on FIFO-empty
 * The trailing ramp DC-biases the speaker; skipping it pops at boot. */
static void Mars_InitPWM(int sample_rate, int min_sample, int max_sample)
{
	int centre_sample = (max_sample - min_sample) / 2;

	MARS_PWM_MONO = 1;
	MARS_PWM_MONO = 1;
	MARS_PWM_MONO = 1;

	MARS_PWM_CYCLE = (uint16_t)((((23011361 << 1) / sample_rate + 1) >> 1) + 1);
	MARS_PWM_CTRL = 0x0185;

	int sample = min_sample;
	while (sample < centre_sample) {
		int reps = (sample_rate * 2) / (centre_sample - min_sample);
		for (int ix = 0; ix < reps; ix++) {
			while (MARS_PWM_MONO & 0x8000) {}   /* wait for FIFO slot */
			MARS_PWM_MONO = (uint16_t)sample;
		}
		sample++;
	}
}

/* DMA-complete: flag the drained buffer, swap, re-arm. CHCR1 = 0x14E5 =
 * word transfer, src-increment, dest-fixed, IE, DREQ edge, enable. */
void amb_dma_handler(void)
{
	snd_buf_needs_fill |= (uint8_t)(1 << snd_current_buf_idx);
	snd_current_buf_idx ^= 1;

	/* UNDERRUN: about to drain a buffer the pump never refilled — DMA
	 * will replay stale samples (the audible chop, made countable). */
	if (snd_buf_needs_fill & (uint8_t)(1 << snd_current_buf_idx))
		UC(snd_underruns_storage)++;

	SH2_DMA_SAR1  = (uint32_t)(uintptr_t)snd_pwm_buf[snd_current_buf_idx];
	SH2_DMA_TCR1  = SND_SAMPLES_PER_BUF;
	SH2_DMA_CHCR1 = 0x14E5;
}

/* ---- primary-side API ---------------------------------------------- */

void sfx_play(uint8_t voice, uint8_t sample, uint8_t vol, uint8_t loop)
{
	if (voice >= SND_MAX_VOICES || sample >= SPEECH_BANK_COUNT)
		return;
	volatile struct sfx_req *r =
		(volatile struct sfx_req *)((uintptr_t)&sfx_reqs[voice] | 0x20000000);
	r->sample = sample;
	r->vol    = vol;
	r->flags  = loop ? 1 : 0;
	r->serial++;          /* publish last: serial change is the doorbell */
}

void sfx_stop(uint8_t voice)
{
	if (voice >= SND_MAX_VOICES)
		return;
	volatile struct sfx_req *r =
		(volatile struct sfx_req *)((uintptr_t)&sfx_reqs[voice] | 0x20000000);
	r->flags = 0x80;
	r->serial++;
}

void sfx_stop_all(void)
{
	for (uint8_t v = 0; v < SND_MAX_VOICES; v++)
		sfx_stop(v);
}

uint16_t snd_underruns(void) { return UC(snd_underruns_storage); }

uint8_t snd_active_mask(void)
{
	uint8_t m = 0;
	for (int v = 0; v < SND_MAX_VOICES; v++)
		if (UC(voices[v].active))
			m |= (uint8_t)(1 << v);
	return m;
}

/* ---- secondary-side mixer ------------------------------------------ */

static void consume_reqs(void)
{
	for (int v = 0; v < SND_MAX_VOICES; v++) {
		volatile struct sfx_req *r =
			(volatile struct sfx_req *)((uintptr_t)&sfx_reqs[v] | 0x20000000);
		uint8_t s = r->serial;
		if (s == voices[v].last_serial)
			continue;
		voices[v].last_serial = s;
		if (r->flags & 0x80) {
			voices[v].active = 0;
			continue;
		}
		const struct speech_sample *sp = &speech_bank[r->sample];
		voices[v].adp     = sp->data;
		voices[v].nibbles = sp->nibbles;
		voices[v].pos     = 0;
		voices[v].pred    = 0;
		voices[v].idx     = 0;
		voices[v].vol     = r->vol;
		voices[v].loop    = r->flags & 1;
		voices[v].active  = 1;
	}
}

static void fill_buffer(uint16_t *buf)
{
	for (int i = 0; i < SND_SAMPLES_PER_BUF; i++) {
		int32_t mix = 0;
		for (int v = 0; v < SND_MAX_VOICES; v++) {
			struct voice *vc = &voices[v];
			if (!vc->active)
				continue;
			/* one IMA nibble per output sample: banks are baked at
			 * the mixer's own 16 kHz (even sample = LOW nibble,
			 * matching tools/speech_bake.py) */
			uint8_t b = vc->adp[vc->pos >> 1];
			int nib = (vc->pos & 1) ? (b >> 4) : (b & 0x0F);
			int step = ima_step_table[vc->idx];
			int delta = step >> 3;
			if (nib & 4) delta += step;
			if (nib & 2) delta += step >> 1;
			if (nib & 1) delta += step >> 2;
			vc->pred += (nib & 8) ? -delta : delta;
			if (vc->pred > 32767) vc->pred = 32767;
			else if (vc->pred < -32768) vc->pred = -32768;
			vc->idx += ima_index_table[nib];
			if (vc->idx < 0) vc->idx = 0;
			else if (vc->idx > 88) vc->idx = 88;
			if (++vc->pos >= vc->nibbles) {
				if (vc->loop) {
					vc->pos = 0; vc->pred = 0; vc->idx = 0;
				} else {
					vc->active = 0;
				}
			}
			/* 16-bit PCM -> PWM span: /64 maps +-32768 onto ~+-512,
			 * then per-voice volume (255 = unity) */
			mix += ((vc->pred >> 6) * (int32_t)vc->vol) >> 8;
		}
		int32_t s = SAMPLE_CENTER + mix;
		if (s > SOFT_HIGH)      s = SOFT_HIGH + ((s - SOFT_HIGH) >> 2);
		else if (s < SOFT_LOW)  s = SOFT_LOW  - ((SOFT_LOW - s) >> 2);
		if (s > SAMPLE_MAX)     s = SAMPLE_MAX;
		else if (s < SAMPLE_MIN) s = SAMPLE_MIN;
		buf[i] = (uint16_t)s;
	}
}

void snd_pump(void)
{
	consume_reqs();
	uint8_t need = snd_buf_needs_fill;
	if (!need)
		return;
	for (int b = 0; b < 2; b++) {
		if (need & (1 << b)) {
			fill_buffer(snd_pwm_buf[b]);
			snd_buf_needs_fill &= (uint8_t)~(1 << b);
		}
	}
}

void snd_init(void)
{
	snd_current_buf_idx = 0;
	snd_buf_needs_fill  = 0;
	UC(snd_underruns_storage) = 0;
	for (int v = 0; v < SND_MAX_VOICES; v++) {
		voices[v].active = 0;
		voices[v].last_serial =
			((volatile struct sfx_req *)
			 ((uintptr_t)&sfx_reqs[v] | 0x20000000))->serial;
	}
	for (int b = 0; b < 2; b++)
		for (int i = 0; i < SND_SAMPLES_PER_BUF; i++)
			snd_pwm_buf[b][i] = SAMPLE_CENTER;

	Mars_InitPWM(SND_MIX_RATE, SAMPLE_MIN, SAMPLE_MAX);

	SH2_DMA_DAR1  = (uint32_t)(uintptr_t)&MARS_PWM_MONO;
	SH2_DMA_DRCR1 = 0;                /* external DREQ source = PWM */
	SH2_DMA_DMAOR = 1;                /* DMAOR.DME */

	/* IPRA [11:8] = DMAC priority 4 (secondary SR mask is 2). */
	SH2_INT_IPRA = (SH2_INT_IPRA & 0xF0FF) | 0x0400;

	/* DMA1 vector -> slot 66 = the "Level 4 & 5" entry already holding
	 * slav_irq, whose dispatch tests for the DMA source and calls
	 * amb_dma_handler (mars_start.s). */
	SH2_DMA_VCR1 = 66;

	/* First transfer: both buffers are silence, the swap chain takes
	 * over from the IRQ. */
	SH2_DMA_SAR1  = (uint32_t)(uintptr_t)snd_pwm_buf[0];
	SH2_DMA_TCR1  = SND_SAMPLES_PER_BUF;
	SH2_DMA_CHCR1 = 0x14E5;
}
