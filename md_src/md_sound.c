/* md_sound.c -- the game's music engine on the 68K: the sound lab's Z80 streaming player
 * (sndtest/, docs/sound/SOUND.md) fed from the cart's .sounddata blob through the banked
 * 0x900000 window. Built only with R60SOUND=1. The lab's VU and live mixer are left out.
 *
 * Contract: the SH-2 port posts the arcade's sound-command byte (the MCU mailbox 0xFFF0C4,
 * consumed in sh_src/port_l2.c) as COMM10 = 0x5A00 | cmd; snd_vint(), called at the end of
 * the vint shim, takes it, acks COMM10 = 0 and starts the mapped stream on the Z80, then
 * tops the Z80's ring up. Streams: 8 rendered music tracks (LZSS, loop) and 73 per-command
 * tracks (raw) from tools/sound_blob.py. The speech lane (uPD7759 samples on the SH-2 PWM)
 * is not wired here. */
#ifdef R60_SOUND
#include <stdint.h>
#include "md_z80_player.h"
#include "sound_tab.h"

#define SND_CART_BASE 0x260000uL                 /* sh_src/mars.ld .sounddata, cart 0x260000 (bank 2) */
#define SND_WIN(off)  (*(const volatile uint8_t *)(0x900000uL + (SND_CART_BASE - 0x200000uL) + (off)))
#define BANK_REG      (*(volatile uint16_t *)0xA15104)
#define ZBUSREQ       (*(volatile uint16_t *)0xA11100)
#define ZRESET        (*(volatile uint16_t *)0xA11200)
#define ZRAM          ((volatile uint8_t *)0xA00000)
#define MARS_COMM10   (*(volatile uint16_t *)0xA1512A)
#define MARS_COMM14   (*(volatile uint16_t *)0xA1512E)

static uint8_t  mus_on, mus_loop, mus_compressed, mus_wr, z80_parked = 1;
static uint32_t src_off, src_len, src_pos, loop_off, loop_len;
static uint8_t  lz_bits, lz_phase;
static uint16_t lz_flags, lz_wpos, lz_msrc, lz_mlen;
static uint8_t  lz_win[4096];
uint16_t snd_cmds, snd_starts, snd_sfx; volatile uint16_t snd_last; volatile uint8_t snd_log[64];   /* census, readable from WRAM by symbol */

__attribute__((section(".data"))) static void lz_reset(void) { src_pos = 0; lz_bits = 0; lz_flags = 0; lz_wpos = 0; lz_mlen = 0; }

__attribute__((section(".data"))) static uint8_t lz_next(void)
{
	if (lz_mlen) {
		uint8_t b = lz_win[lz_msrc & 4095]; lz_msrc++; lz_mlen--;
		lz_win[lz_wpos & 4095] = b; lz_wpos++;
		return b;
	}
	if (src_pos >= src_len) {
		if (!mus_loop) return 0xFF;
		if (lz_phase == 0) { lz_phase = 1; src_off = loop_off; src_len = loop_len; }
		if (src_len == 0) return 0xFF;
		lz_reset();
	}
	if (lz_bits == 0) { lz_flags = SND_WIN(src_off + src_pos); src_pos++; lz_bits = 8; }
	{	uint8_t ism = (uint8_t)(lz_flags & 1u); lz_flags >>= 1; lz_bits--;
		if (ism) {
			uint16_t tok = (uint16_t)(((uint16_t)SND_WIN(src_off + src_pos) << 8) | SND_WIN(src_off + src_pos + 1u));
			uint8_t b;
			src_pos += 2;
			lz_mlen = (uint16_t)((tok & 0x0Fu) + 3u);
			lz_msrc = (uint16_t)(lz_wpos - ((tok >> 4) + 1u));
			b = lz_win[lz_msrc & 4095]; lz_msrc++; lz_mlen--;
			lz_win[lz_wpos & 4095] = b; lz_wpos++;
			return b;
		}
	}
	{	uint8_t b = SND_WIN(src_off + src_pos); src_pos++;
		lz_win[lz_wpos & 4095] = b; lz_wpos++;
		return b;
	}
}

__attribute__((section(".data"))) static void fill_block(uint16_t base)
{
	uint16_t i;
	for (i = 0; i < 512u; i++) {
		uint8_t b;
		if (mus_compressed) b = lz_next();
		else if (src_pos < src_len) { b = SND_WIN(src_off + src_pos); src_pos++; }
		else if (mus_loop) { src_pos = 0; b = SND_WIN(src_off); src_pos = 1; }
		else b = 0xFF;
		ZRAM[base + i] = b;
	}
}

__attribute__((section(".data"))) static void ym_write_held(uint8_t reg, uint8_t val)
{
	volatile uint8_t *ym = (volatile uint8_t *)0xA04000;
	uint32_t g = 200; while ((ym[0] & 0x80) && --g) ;
	ym[0] = reg;
	{ volatile uint16_t d; for (d = 0; d < 8; d++) ; }
	g = 200; while ((ym[0] & 0x80) && --g) ;
	ym[1] = val;
	{ volatile uint16_t d; for (d = 0; d < 32; d++) ; }
}

__attribute__((section(".data"))) static void music_stop(void)
{
	mus_on = 0;
	ZBUSREQ = 0x100;
	{ uint32_t g = 200000; while ((ZBUSREQ & 0x100) && --g) ; }
	z80_parked = 1;
	ym_write_held(0x28, 0x00); ym_write_held(0x28, 0x01); ym_write_held(0x28, 0x02);
	ym_write_held(0x28, 0x04); ym_write_held(0x28, 0x05); ym_write_held(0x28, 0x06);
	{ volatile uint8_t *psg = (volatile uint8_t *)0xC00011; *psg = 0x9F; *psg = 0xBF; *psg = 0xDF; *psg = 0xFF; }
}

/* the lab's session-boot dance (sndtest/md/md_main.c music_start_ex): release reset, take the
 * bus, upload the player, zero the mailboxes, prime 3 blocks, MGO, reset pulse, bus back */
__attribute__((section(".data"))) static void music_start_ex(uint32_t off, uint32_t len, uint8_t loop, uint8_t compressed, uint32_t loff, uint32_t llen)
{
	uint16_t i;
	BANK_REG = 2;
	mus_compressed = compressed; src_off = off; src_len = len; loop_off = loff; loop_len = llen;
	lz_phase = (loff == off) ? 1u : 0u;
	lz_reset();
	mus_loop = loop;
	ZRESET = 0x100;
	ZBUSREQ = 0x100;
	{ uint32_t g = 200000; while ((ZBUSREQ & 0x100) && --g) ; }
	for (i = 0; i < Z80_PLAYER_LEN; i++) ZRAM[i] = z80_player[i];
	ZRAM[ZP_RD_BLK] = 0; ZRAM[ZP_WR_BLK] = 0; ZRAM[ZP_MSTAT] = 0; ZRAM[ZP_MGO] = 0;
	for (mus_wr = 0; mus_wr < 3; mus_wr++) fill_block((uint16_t)(ZP_RING + ((uint16_t)mus_wr << 9)));
	ZRAM[ZP_WR_BLK] = mus_wr;
	ZRAM[ZP_MGO] = 1;
	ZRESET = 0x000;
	{ volatile uint16_t d; for (d = 0; d < 64; d++) ; }
	ZBUSREQ = 0x000;
	ZRESET = 0x100;
	z80_parked = 0;
	mus_on = 1;
	BANK_REG = 3;
	snd_starts++;
}

__attribute__((section(".data"))) static void music_feed(void)
{
	uint8_t rd, st, fed = 0;
	if (!mus_on) return;
	BANK_REG = 2;
	ZBUSREQ = 0x100;
	{ uint32_t g = 100000; while ((ZBUSREQ & 0x100) && --g) ; }
	rd = ZRAM[ZP_RD_BLK]; st = ZRAM[ZP_MSTAT];
	while ((uint8_t)(mus_wr - rd) < 2u && fed < 1u) {      /* one block a vint: ~40 68K lines, after the DONE post */
		fill_block((uint16_t)(ZP_RING + (((uint16_t)mus_wr & 7u) << 9)));
		mus_wr++;
		ZRAM[ZP_WR_BLK] = mus_wr;
		fed++;
	}
	ZBUSREQ = 0x000;
	BANK_REG = 3;                                     /* the shim's resting bank (md_main.c restores 3 everywhere) */
	MARS_COMM14 = (uint16_t)(((uint16_t)mus_wr << 8) | rd);
	if (st == 2) music_stop();
}

__attribute__((section(".data"))) void snd_command(uint8_t cmd)
{
	unsigned k;
	snd_log[snd_cmds & 63u] = cmd; snd_cmds++; snd_last = cmd;
	if (cmd == 0x00u) { if (mus_on) music_stop(); return; }   /* the arcade's stop-all byte (docs/sound/HANDOFF-SOUND.md:310) */
	for (k = 0; k < SND_REND_COUNT; k++)
		if (snd_rend[k].cmd == cmd) { music_start_ex(snd_rend[k].intro, snd_rend[k].intro_len, 1, 1, snd_rend[k].loop, snd_rend[k].loop_len); return; }
	{	const struct snd_ent *e = &sndmap[cmd];
		if (e->type == SND_MUSIC) {                 /* a music command the renderer did not cover: raw track */
			const struct snd_trk *t = &snd_trks[e->idx];
			music_start_ex(t->off, t->len, t->loop, 0, t->off, t->len);
		} else if (e->type == SND_YMSFX) {
			snd_sfx++;                              /* ONE Z80 lane: an SFX stream would cut the music (the lab did
			                                         * exactly that, sndtest/md/md_main.c:609). Counted, not played,
			                                         * until the SFX lane exists. */
		}
	}
}

__attribute__((section(".data"))) void snd_vint(void)
{
	uint16_t v = MARS_COMM10;
#ifdef R60_SNDTEST
	{ static uint16_t tv; if (++tv == 300u) v = (uint16_t)(0x5A00u | R60_SNDTEST); }   /* probe: inject one command at vint 300 (R60SNDTEST=<hex>) */
#endif
	if ((v & 0xFF00u) == 0x5A00u) { MARS_COMM10 = 0; snd_command((uint8_t)v); }
	music_feed();
}
#endif /* R60_SOUND */
