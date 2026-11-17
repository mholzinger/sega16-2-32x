/* opm_lane -- the YM2151's two overflow channels, synthesized on the secondary SH-2 and
 * mixed into the PWM lane (hybrid 3, 2026-10-31). KIT.
 *
 * WHY. The YM2612 has six FM channels and the arcade's YM2151 music uses eight; the sound
 * cart squared channels 6/7 on the PSG. The census (tools/opm_chip_census.py) showed no
 * other chip can carry them: they are true 4-op voices, a square misses half their energy,
 * and the YM2612 is full 40% of the time. So they are synthesized here, bit-exact to jt51
 * (sndtest/opm, tools/opm_identity.sh), from the rips' own register writes.
 *
 * HOW. tools/opm_music_gen.py lays each song command's channel-6/7 writes out as (delta in
 * lane samples, reg << 8 | val) rows on the PWM clock (sndtest/sh/opm_music.h), the row
 * clock scaled to the Z80 player's (its millisecond is Timer B quantised: 1.000237). The
 * engine runs INSIDE fill_buffer, one call per output sample, at the mixer's own rate: the
 * ping-pong buffers are its cut-tolerant ring. The song loops whole as the Z80 player does
 * and stops on command 0x00.
 *
 * THE TRIGGER. The 68K primes the Z80 with the song and holds it in reset, publishes
 * COMM6 = {frame, cmd} right after a vblank's COMM12, and releases the Z80 OPM_LANE_FRAMES
 * vblanks later at the same point in the frame (md_main.c). This side sees COMM6 change,
 * waits for the next vblank edge, reads the frame there (COMM12) and where the PWM DMA is
 * (TCR1: samples left on the playing side, plus the queued side if no fill is pending), and
 * sets the song's position so that its sample 0 is the one that plays when the Z80 starts.
 * What is left is a constant (the Z80's own start-up, the FIFO, the 68K's in-frame lag):
 * OPM_LANE_TRIM, measured by tools/opm_lanesync.sh.
 */
#include "mars.h"
#include "sound.h"
#include "../opm/opm.h"
#include "opm_lane.h"
#include "opm_music.h"

#ifndef OPM_LANE_GAIN
#define OPM_LANE_GAIN 488         /* Q8 on the engine's two-channel sum read as 16-bit PCM: 256 measured -8.9 dB
                                   * against jt51 on ares where the YM2612 lane reads -3.3 (tools/opm_stage3.sh,
                                   * stage2); +5.6 dB puts the two lanes level on ares. The rig and Mike's ears
                                   * set the final figure (the 32X mixes PWM into the MD's audio in analogue). */
#endif
#ifndef OPM_LANE_FRAMES
#define OPM_LANE_FRAMES 8         /* vblanks from the COMM6 publish to the Z80 release (md_main.c: the same) */
#endif
#ifndef OPM_LANE_TRIM
#define OPM_LANE_TRIM 720         /* lane samples added to the landing: + = the lane plays later. Measured
                                   * (tools/opm_lanesync.sh, ares): with 0 the lane led the Z80 by 45 ms, constant
                                   * from the first click: the player's catch-up clock sits that far behind */
#endif

static opm_chip lane_chip;
static const struct opm_song *lane_song;
static uint32_t lane_row, lane_due, lane_pos;
static uint16_t lane_comm6_last;
static uint8_t  lane_primed;
static uint32_t lane_ticks;
static int32_t  lane_last_p0;     /* diagnostics: the last landing, in samples */

void opm_lane_init(void)
{
	lane_song = 0;
	lane_primed = 0;
	lane_ticks = 0;
	opm_init(&lane_chip, 4000000, OPM_MUSIC_RATE, OPM_MUSIC_CHMASK);
}

uint32_t opm_lane_cycles(void) { return lane_ticks; }
int32_t  opm_lane_landing(void) { return lane_last_p0; }

/* rows due at or before the position are written now (a landing can skip ahead) */
static void lane_catch_up(void)
{
	const struct opm_song *s = lane_song;
	while (lane_row < s->rows && (int32_t)(lane_due - lane_pos) <= 0) {
		uint16_t rv = s->row[lane_row][1];
		if ((rv >> 8) != 0xff) opm_write(&lane_chip, (uint8_t)(rv >> 8), (uint8_t)rv);
		lane_row++;
		if (lane_row < s->rows) lane_due += s->row[lane_row][0];
	}
}

static void lane_start(const struct opm_song *s, uint16_t c6)
{
	uint32_t f0, f, left, rem, ahead;
	uint8_t pending, pending2;
	int32_t p0;

	opm_init(&lane_chip, 4000000, OPM_MUSIC_RATE, OPM_MUSIC_CHMASK);
	lane_song = s;
	lane_row = 0;
	lane_due = s->row[0][0];

	/* the next vblank edge, then the frame the 68K stamps it with: its 16-bit `timer`,
	 * written as the high word of COMM12 (the low word is COMM14, the feed's counters) */
#define LANE_FRAME() (MARS_SYS_COMM12 >> 16)
	f0 = LANE_FRAME();
	while (MARS_VDP_FBCTL & 0x8000) ;
	while (!(MARS_VDP_FBCTL & 0x8000)) ;
	{
		uint32_t spin = 0;
		while ((f = LANE_FRAME()) == f0 && ++spin < 20000) ;
		if (f == f0) f = f0 + 1;                   /* the 68K is late this frame: assume */
	}
	left = (uint32_t)(((c6 >> 8) + OPM_LANE_FRAMES - f) & 0xff);   /* frames until the Z80 runs */
	do {
		pending = snd_fill_pending();
		rem = SH2_DMA_TCR1 & 0xffffff;
		pending2 = snd_fill_pending();
	} while (pending != pending2);
	ahead = rem + (pending ? 0 : SND_BUF_SAMPLES); /* samples until the next fill's first sample plays */
	p0 = (int32_t)((left * OPM_MUSIC_SPF_Q8) >> 8) - (int32_t)ahead + OPM_LANE_TRIM;
	lane_last_p0 = p0;
	lane_pos = (uint32_t)(0 - p0);                 /* p0 > 0: a delay; p0 < 0: skip into the song */
	lane_catch_up();
}

void opm_lane_poll(void)
{
	uint16_t c6 = MARS_SYS_COMM6;
	int i;
	if (!lane_primed) { lane_primed = 1; lane_comm6_last = c6; return; }
	if (c6 == lane_comm6_last) return;
	lane_comm6_last = c6;
	if ((c6 & 0xff) == 0) { lane_song = 0; return; }      /* the arcade's stop */
	for (i = 0; i < OPM_MUSIC_COUNT; i++)
		if (opm_music[i].cmd == (c6 & 0xff)) { lane_start(&opm_music[i], c6); return; }
	/* any other command: a speech or sfx trigger, the Z80 song plays on and so does this */
}

int32_t opm_lane_sample(void)
{
	const struct opm_song *s = lane_song;
	int32_t v;
	if (!s) return 0;
	lane_catch_up();
	v = opm_sample(&lane_chip);
	if (++lane_pos >= s->samples && (int32_t)lane_pos > 0) {   /* whole-song wrap, as the Z80 player loops */
		lane_pos = 0;
		lane_row = 0;
		lane_due = s->row[0][0];
	}
	return (v * OPM_LANE_GAIN) >> 8;
}
