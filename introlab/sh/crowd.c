/* THE CROWD: the murmur of a room of people under the launcher's music, a five-second loop
 * made from the game's own speech (tools/gen_crowd.py), played on the 32X's PWM output.
 *
 * The loop is stored at 5618 samples a second, which is all its 1600 Hz of content needs.
 * Played at that rate, each sample held until the next, it rang: the steps put copies of
 * the sound up around 4-5 kHz (Mike on the rig: "a very distinct tinny sound"). So the PWM
 * runs four times faster, 22472 Hz, and the three samples in between are drawn on a curve
 * through the stored ones (crowd_side), which leaves those copies some 40 dB down.
 *
 * That needs somewhere to put the curve. The game's speech lane has a 4 KB buffer at
 * 0x06029800 for exactly this (sh_src/r60/r60_speech.inc: sp_buf), and the lane is not
 * started until the launcher is over, when speech_init fills the buffer afresh. The crowd
 * uses it as two sides of 1024 samples (45 ms): the secondary's DMA channel 1 feeds the PWM
 * from one side while the other is being drawn. There is no interrupt (the table is the
 * game's): crowd_poll (crowd.h), which the secondary runs between everything else it does,
 * sees a side end, turns the channel to the other at once, and then redraws the one that
 * ended. The PWM holds its last level for the moment the turn takes: in ares a lap of the
 * loop measures 5.007 s against 5.000, which is about 60 us a turn, a sample or so.
 *
 * The PWM period is 1024 clocks and a sample is a level 1..1023 around 512, one sample a
 * period, the same shape as the game's own lane. The channel and the PWM are the game's:
 * saved at the start and put back at the end. */
#include "crowd.h"
#ifdef PORT
#include "crowd_data.h"

#define PWM_MONO   (*(volatile uint16_t *)0x20004038)
#define DMA_DRCR1  (*(volatile uint8_t *)0xFFFFFE72)
#define DMA_OR     (*(volatile uint32_t *)0xFFFFFFB0)
#define CHCR_RUN   0x14E1u               /* source steps by words, PWM request, no interrupt, enabled */
#define SIDE       1024                  /* output samples a side: 256 stored ones */
#define side(i)    ((volatile uint16_t *)(0x26029800u + (uint32_t)(i) * (SIDE * 2u)))   /* through the uncached mirror: the DMA reads memory */

uint8_t crowd_on;
uint16_t crowd_laps;                     /* times round the loop */
static uint8_t playing;                  /* the side the PWM is being fed from */
static uint16_t at;                      /* where in the loop the next side starts */
static struct { uint32_t sar, dar, tcr, chcr; uint16_t ctrl, cycle; uint8_t drcr; } was;

static inline int stored(int i) {        /* the loop is a circle */
	if (i < 0) i += CROWD_WORDS; else if (i >= CROWD_WORDS) i -= CROWD_WORDS;
	return (int)crowd_loop[i];
}

/* Draw one side: 256 stored samples and the three between each pair, on the curve through
 * four neighbours (Catmull-Rom at 1/4, 1/2, 3/4; weights in 128ths). */
static void crowd_side(int which) {
	volatile uint16_t *o = side(which);
	int i = at, a = stored(i - 1), b = stored(i), c = stored(i + 1);
	for (int n = SIDE / 4; n > 0; n--) {
		int d = stored(i + 2), v;
		*o++ = (uint16_t)b;
		v = (-9 * a + 111 * b + 29 * c - 3 * d) >> 7;  *o++ = (uint16_t)(v < 1 ? 1 : v > 1023 ? 1023 : v);
		v = (-8 * a + 72 * b + 72 * c - 8 * d) >> 7;   *o++ = (uint16_t)(v < 1 ? 1 : v > 1023 ? 1023 : v);
		v = (-3 * a + 29 * b + 111 * c - 9 * d) >> 7;  *o++ = (uint16_t)(v < 1 ? 1 : v > 1023 ? 1023 : v);
		a = b; b = c; c = d;
		if (++i >= CROWD_WORDS) { i = 0; crowd_laps++; }
	}
	at = (uint16_t)i;
}

static void feed(int which) {
	(void)SH2_DMA_CHCR1;                 /* the end flag clears on a write after a read */
	SH2_DMA_CHCR1 = CHCR_RUN & ~1u;
	SH2_DMA_SAR1 = (uint32_t)(uintptr_t)side(which);
	SH2_DMA_TCR1 = SIDE;
	SH2_DMA_CHCR1 = CHCR_RUN;
}

void crowd_turn(void) {
	int done = playing;
	playing ^= 1;
	feed(playing);
	crowd_side(done);
}

void crowd_begin(void) {
	if (!CROWD_WORDS || crowd_on) return;
	was.chcr = SH2_DMA_CHCR1;
	SH2_DMA_CHCR1 = was.chcr & ~7u;      /* the game's transfer stops, its interrupt with it */
	was.sar = SH2_DMA_SAR1; was.dar = SH2_DMA_DAR1; was.tcr = SH2_DMA_TCR1; was.drcr = DMA_DRCR1;
	was.ctrl = MARS_PWM_CTRL; was.cycle = MARS_PWM_CYCLE;
	at = 0; playing = 0;
	crowd_side(0); crowd_side(1);
	MARS_PWM_CTRL = 0;
	MARS_PWM_CYCLE = 1024;
	PWM_MONO = 512; PWM_MONO = 512; PWM_MONO = 512;
	MARS_PWM_CTRL = 0x0185;              /* both sides on, ask for a word every period */
	SH2_DMA_DAR1 = (uint32_t)(uintptr_t)&PWM_MONO;
	DMA_DRCR1 = 0;
	DMA_OR = 1;
	crowd_on = 1;
	feed(0);
}

void crowd_end(void) {
	if (!crowd_on) return;
	crowd_on = 0;
	(void)SH2_DMA_CHCR1;
	SH2_DMA_CHCR1 = CHCR_RUN & ~1u;
	MARS_PWM_CTRL = 0;
	MARS_PWM_CYCLE = was.cycle;
	PWM_MONO = was.cycle >> 1; PWM_MONO = was.cycle >> 1; PWM_MONO = was.cycle >> 1;
	MARS_PWM_CTRL = was.ctrl;
	SH2_DMA_SAR1 = was.sar; SH2_DMA_DAR1 = was.dar; SH2_DMA_TCR1 = was.tcr; DMA_DRCR1 = was.drcr;
	SH2_DMA_CHCR1 = was.chcr & ~2u;      /* the game's transfer again, as it was */
}
#endif
