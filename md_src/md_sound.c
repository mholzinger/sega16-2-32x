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

/* PAGED (2026-10-31): the blob is page 8 of the SSF2 cart (sh_src/pages_data.s), shown by slot 7
 * since boot (md_main.c pages it once; the SH-2s never read slot 7). Slot 7 is window bank 3,
 * the shim's resting bank, so no bank switch is needed to read it. */
#define SND_WIN(off)  (*(const volatile uint8_t *)(0x980000uL + (off)))
#define BANK_SND      3
#define BANK_REG      (*(volatile uint16_t *)0xA15104)
#define ZBUSREQ       (*(volatile uint16_t *)0xA11100)
#define ZRESET        (*(volatile uint16_t *)0xA11200)
#define ZRAM          ((volatile uint8_t *)0xA00000)
#define MARS_COMM10   (*(volatile uint16_t *)0xA1512A)
#define MARS_COMM14   (*(volatile uint16_t *)0xA1512E)

static uint8_t  mus_on, mus_loop, mus_compressed, mus_wr, z80_parked = 1;
void snd_command(uint8_t cmd);
static uint8_t  mus_resume;                /* the looping song a ONE-SHOT (round clear, game over, ending: no loop
                                            * point in the rip) interrupted; restarted when the one-shot ends. The
                                            * arcade driver layers the jingle over the song; one Z80 player cannot,
                                            * so the song gives way and comes back (Mike on psg1: the orb jingle
                                            * and level 2's song fought, then only one survived). */
static uint32_t src_off, src_len, src_pos, loop_off, loop_len;
static uint8_t  lz_bits, lz_phase;
static uint16_t lz_flags, lz_wpos, lz_msrc, lz_mlen;
/* THE LZSS WINDOW IS THE Z80 RING (2026-10-31): the window is the last 4096 output bytes, and
 * the ring (8 blocks x 512, ZP_RING) holds exactly those -- every byte lz_next returns is
 * stored there by fill_block before the next call, and a match reaches back at most 4095,
 * into the part of the oldest block not yet overwritten. Reads go through the Z80 bus the
 * fill already holds. A 4 KB WRAM copy pushed .bss into the boot stack fence (md.ld). */
#define LZ_WIN(q) (ZRAM[ZP_RING + ((q) & 4095u)])
uint16_t snd_cmds, snd_starts, snd_sfx; volatile uint16_t snd_last; volatile uint8_t snd_log[64];   /* census, readable from WRAM by symbol */

__attribute__((section(".data"))) static void lz_reset(void) { src_pos = 0; lz_bits = 0; lz_flags = 0; lz_wpos = 0; lz_mlen = 0; }

__attribute__((section(".data"))) static uint8_t lz_next(void)
{
	if (lz_mlen) {
		uint8_t b = LZ_WIN(lz_msrc); lz_msrc++; lz_mlen--;
		lz_wpos++;
		return b;
	}
	if (src_pos >= src_len) {
		if (!mus_loop) return 0xFF;
		if (lz_phase == 0) { lz_phase = 1; src_off = loop_off; src_len = loop_len; }
		if (src_len == 0) return 0xFF;
		/* THE WRAP KEEPS lz_wpos (2026-11-01, psg1g replay): the window IS the ring, and the ring's
		 * write position is wherever the feed is; lz_reset() put the window position back to 0
		 * while the ring went on, so every match after the wrap read the wrong ring bytes and the
		 * song came back as garbage: the YM2612 fell silent and the PSG played random squares 13 dB
		 * up, 142 s into 0x94 (the whole-song wrap), with no command posted. A fresh stream's
		 * matches never reach before its first byte, so a continuing window is exact. */
		src_pos = 0; lz_bits = 0; lz_flags = 0; lz_mlen = 0;
	}
	if (lz_bits == 0) { lz_flags = SND_WIN(src_off + src_pos); src_pos++; lz_bits = 8; }
	{	uint8_t ism = (uint8_t)(lz_flags & 1u); lz_flags >>= 1; lz_bits--;
		if (ism) {
			uint16_t tok = (uint16_t)(((uint16_t)SND_WIN(src_off + src_pos) << 8) | SND_WIN(src_off + src_pos + 1u));
			uint8_t b;
			src_pos += 2;
			lz_mlen = (uint16_t)((tok & 0x0Fu) + 3u);
			lz_msrc = (uint16_t)(lz_wpos - ((tok >> 4) + 1u));
			b = LZ_WIN(lz_msrc); lz_msrc++; lz_mlen--;
			lz_wpos++;
			return b;
		}
	}
	{	uint8_t b = SND_WIN(src_off + src_pos); src_pos++;
		lz_wpos++;
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

/* not static: the launcher's 68K program starts and stops its own stream with these two (launcher_smoke.c) */
__attribute__((section(".data"))) void music_stop(void)
{
	mus_on = 0;
	ZBUSREQ = 0x100;
	{ uint32_t g = 200000; while ((ZBUSREQ & 0x100) && --g) ; }
	z80_parked = 1;
	ym_write_held(0x28, 0x00); ym_write_held(0x28, 0x01); ym_write_held(0x28, 0x02);
	ym_write_held(0x28, 0x04); ym_write_held(0x28, 0x05); ym_write_held(0x28, 0x06);
	{ volatile uint8_t *psg = (volatile uint8_t *)0xC00011; *psg = 0x9F; *psg = 0xBF; *psg = 0xDF; *psg = 0xFF; }
	/* HOLD THE Z80 IN RESET: parking it on the bus request alone does not hold. read_joypad
	 * (md_main) requests and releases the Z80 bus every frame (ares access trace, psg3g frames
	 * 30625..: 0xff010a writes 0x100, 0xff0182 writes 0, each vint), so the parked player
	 * resumed, drained its stale ring and left the PSG on its last note: a 130 Hz square at
	 * -41 dBFS for 20 s after the boss-death stop (build/tapes/psg3g_l1full, 2026-11-02).
	 * The Z80 reset line also resets the YM2612, which cuts the 12 s release tail the arcade's
	 * stop-all never has. music_start_ex releases the reset with the bus already held. */
	ZRESET = 0x000;
}

/* the lab's session-boot dance (sndtest/md/md_main.c music_start_ex): release reset, take the
 * bus, upload the player, zero the mailboxes, prime 3 blocks, MGO, reset pulse, bus back */
__attribute__((section(".data"))) void music_start_ex(uint32_t off, uint32_t len, uint8_t loop, uint8_t compressed, uint32_t loff, uint32_t llen)
{
	uint16_t i;
	BANK_REG = BANK_SND;
	mus_compressed = compressed; src_off = off; src_len = len; loop_off = loff; loop_len = llen;
	lz_phase = (loff == off) ? 1u : 0u;
	lz_reset();
	mus_loop = loop;
	ZBUSREQ = 0x100;                                 /* request the bus BEFORE releasing the reset: a Z80 parked in
	                                                  * reset by music_stop must not run its stale player first */
	ZRESET = 0x100;
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
	BANK_REG = BANK_SND;
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
	if (st == 2) {                                    /* the one-shot ended (0xFF): the song it cut comes back */
		uint8_t r = mus_resume;
		music_stop();
		mus_resume = 0;
		if (r) snd_command(r);
	}
}

static uint8_t snd_last_loop;              /* the last looping song started (what a one-shot resumes) */
__attribute__((section(".data"))) void snd_command(uint8_t cmd)
{
	unsigned k;
	snd_log[snd_cmds & 63u] = cmd; snd_cmds++; snd_last = cmd;
	if (cmd == 0x00u) {
		mus_resume = 0;
		if (mus_on) music_stop();
		return;
	}   /* the arcade's stop-all byte (docs/sound/HANDOFF-SOUND.md:310) */
	for (k = 0; k < SND_REND_COUNT; k++)
		if (snd_rend[k].cmd == cmd) {
			{	/* loop_len 0 = a one-shot (tools/build_vgm_music.py: no loop point in the rip) */
				uint8_t loops = snd_rend[k].loop_len != 0u;
				if (loops) mus_resume = 0;
				else if (mus_on && mus_loop) mus_resume = snd_last_loop;
				if (loops) snd_last_loop = cmd;
				music_start_ex(snd_rend[k].intro, snd_rend[k].intro_len, loops, 1, snd_rend[k].loop, snd_rend[k].loop_len);
			}
			return;
		}
	{	const struct snd_ent *e = &sndmap[cmd];
		if (e->type == SND_MUSIC) {                 /* a music command the renderer did not cover: raw track */
			const struct snd_trk *t = &snd_trks[e->idx];
			/* the jingle songs 0xB1/0xB2/0xD3 (SOUND_DRIVER.md:69) have EMPTY captures (one 0xFF):
			 * starting one replaced the playing song with nothing, and the YM2612's held notes
			 * and the PSG's last square hummed until the player reported its end (Mike on psg1,
			 * 2026-11-01: Neff's entrance "stuck on a hum", the boss fight muted). Until those
			 * songs are rendered, an empty track leaves the music alone. */
			if (t->len <= 1u) { snd_sfx++; return; }
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
	if ((v & 0xFF00u) == 0x5A00u) { MARS_COMM10 = 0; snd_command((uint8_t)v); }
	music_feed();
}
#endif /* R60_SOUND */
