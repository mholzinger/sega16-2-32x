/* opm_bench -- what the YM2151 channel engine (sndtest/opm) costs on the master SH-2. KIT.
 *
 * Runs once at the sound cart's boot, before the menu: two channels, then eight, each a
 * feedback-7 algorithm-0 voice (Power Up's channel 6, the costliest shape in the songs),
 * keyed on and synthesized for OPM_BENCH_SAMPLES samples in 512-sample chunks timed on the
 * FRT (TCR /128: 5.56 us a tick, the clock sound.c's fill cost uses). Results land in
 * opm_bench_out, SDRAM, for `ares-headless --gameframe-watch sdram:<addr>:32`:
 *   [0] ticks for 2 channels   [1] samples   [2] ticks for 8 channels   [3] samples
 *   [4] 2 channels at 31250 Hz [5] samples   [6] 2 channels at 15625 Hz (8192 samples)
 * Code and engine sit in .ramtext (SDRAM): the cart bus is not what is being measured.
 *
 * WHY (2026-10-31, hybrid 2). The hybrid keeps the YM2612 for six voices and synthesizes
 * the two overflow voices here, into the PWM lane. Whether that fits beside the game, and at
 * what sample rate (rendering under 62.5 kHz aliases the feedback-7 voices, opm.h), is a
 * number nobody had. ares charges instruction cycles only (no SDRAM waits, no cache), so
 * its reading is a floor; the rig's is the ceiling.
 */
#include <stdint.h>
#include "mars.h"
#include "../opm/opm.h"

#define OPM_BENCH_SAMPLES 8192
#define CHUNK 512

volatile uint32_t opm_bench_out[16] __attribute__((aligned(16)));
static opm_chip bench_chip;

#ifdef OPM_BENCH_SONG
/* a song's overflow channels replayed through the engine (tools/opm_song_bench_gen.py):
 * [8] ticks  [9] samples  [10] rate  [12] the costliest 512-sample chunk's ticks */
#include "opm_bench_song.h"
#endif

static inline uint16_t frt(void)
{
	uint8_t h = SH2_FRT_FRCH, l = SH2_FRT_FRCL;
	return (uint16_t)((h << 8) | l);
}

static void patch(opm_chip *c, int ch)
{
	static const uint8_t dtmul[4] = { 0x60, 0x60, 0x30, 0x30 };     /* M1 C1 M2 C2 */
	static const uint8_t tl[4]    = { 0x1c, 0x18, 0x19, 0x14 };
	static const uint8_t ar[4]    = { 0x5f, 0x1f, 0x1f, 0x1f };
	static const uint8_t d1r[4]   = { 0x0d, 0x09, 0x0c, 0x1f };
	static const uint8_t d1lrr[4] = { 0x58, 0xf9, 0x5a, 0x0c };
	static const uint8_t slot[4]  = { 0, 16, 8, 24 };
	opm_write(c, (uint8_t)(0x20 + ch), 0xf8);                        /* RL, fb 7, alg 0 */
	opm_write(c, (uint8_t)(0x28 + ch), (uint8_t)(0x44 + ch));
	opm_write(c, (uint8_t)(0x30 + ch), 0);
	for (int n = 0; n < 4; n++) {
		uint8_t s = (uint8_t)(slot[n] + ch);
		opm_write(c, (uint8_t)(0x40 + s), dtmul[n]);
		opm_write(c, (uint8_t)(0x60 + s), tl[n]);
		opm_write(c, (uint8_t)(0x80 + s), ar[n]);
		opm_write(c, (uint8_t)(0xA0 + s), d1r[n]);
		opm_write(c, (uint8_t)(0xC0 + s), 0);
		opm_write(c, (uint8_t)(0xE0 + s), d1lrr[n]);
	}
	opm_write(c, 0x08, (uint8_t)(0x78 | ch));
}

__attribute__((section(".ramtext"), noinline))
static uint32_t run(uint32_t rate, uint8_t chmask, uint32_t *sum)
{
	uint32_t ticks = 0, acc = 0;
	opm_init(&bench_chip, 4000000, rate, chmask);
	for (int ch = 0; ch < 8; ch++)
		if ((chmask >> ch) & 1) patch(&bench_chip, ch);
	for (int done = 0; done < OPM_BENCH_SAMPLES; done += CHUNK) {
		uint16_t t0 = frt();
		for (int i = 0; i < CHUNK; i++)
			acc += (uint32_t)opm_sample(&bench_chip);
		ticks += (uint16_t)(frt() - t0);
	}
	*sum += acc;
	return ticks;
}

#ifdef OPM_BENCH_SONG
__attribute__((section(".ramtext"), noinline))
static uint32_t run_song(uint32_t *sum)
{
	uint32_t ticks = 0, acc = 0, row = 0, due = 0, done = 0, peak = 0;
	opm_init(&bench_chip, 4000000, OPM_SONG_RATE, OPM_SONG_CHMASK);
	if (row < OPM_SONG_ROWS) due = opm_song_rows[0][0];
	while (done < OPM_SONG_SAMPLES) {
		uint16_t t0 = frt();
		for (int i = 0; i < CHUNK; i++, done++) {
			while (row < OPM_SONG_ROWS && due == done) {
				uint16_t rv = opm_song_rows[row][1];
				if ((rv >> 8) != 0xff) opm_write(&bench_chip, (uint8_t)(rv >> 8), (uint8_t)rv);
				row++;
				if (row < OPM_SONG_ROWS) due += opm_song_rows[row][0];
			}
			acc += (uint32_t)opm_sample(&bench_chip);
		}
		{ uint32_t dt = (uint16_t)(frt() - t0); ticks += dt; if (dt > peak) peak = dt; }
	}
	*sum += acc;
	opm_bench_out[12] = peak;
	return ticks;
}
#endif

void opm_bench(void)
{
	uint32_t sum = 0;
	SH2_FRT_TCR = 2;                                 /* phi/128: 5.56 us a tick, as sound.c runs it */
	opm_bench_out[0] = run(0, 0xC0, &sum);           /* two channels, native rate */
	opm_bench_out[1] = OPM_BENCH_SAMPLES;
	opm_bench_out[2] = run(0, 0xFF, &sum);           /* eight channels */
	opm_bench_out[3] = OPM_BENCH_SAMPLES;
	opm_bench_out[4] = run(31250, 0xC0, &sum);       /* two channels, half rate */
	opm_bench_out[5] = OPM_BENCH_SAMPLES;
	opm_bench_out[6] = run(15625, 0xC0, &sum);       /* two channels, quarter rate (the in-game rate candidate) */
#ifdef OPM_BENCH_SONG
	opm_bench_out[8] = run_song(&sum);
	opm_bench_out[9] = OPM_SONG_SAMPLES;
	opm_bench_out[10] = OPM_SONG_RATE;
#endif
	opm_bench_out[11] = sum;                         /* keeps the synthesis live under -O2 */
	opm_bench_out[7] = 0x0B3C0B3C;                   /* done */
}
