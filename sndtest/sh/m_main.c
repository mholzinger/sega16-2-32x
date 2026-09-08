/* sndtest primary SH-2 — the sound-engine lab menu (SOUND.md P0).
 *
 * The whole UI is the arcade's own sound-test idiom: pick a command
 * byte, post it through the engine's front door (HwMdSndCmd -> 68K
 * router), hear the result. The MD text plane draws it; the 32X VDP
 * stays untouched (blank layer, MD shows through).
 *
 * Pad map (COMM8, published unsolicited by the 68K every frame):
 *   bit0 U  bit1 D  bit2 L  bit3 R  bit4 B  bit5 C  bit6 A  bit7 START
 * UP/DOWN +-1, LEFT/RIGHT +-16 on the command byte; A posts it; B
 * posts 0x00 (the customary all-off); C cycles the YM smoke test
 * (68K case 15: patch+key-on / retrigger / release / off).
 */
#include <stdint.h>
#include "mars.h"
#include "sound.h"
#include "speech_bank.h"

extern void HwMdSndCmd(unsigned char cmd);
extern void HwMdMusicCmd(unsigned char op);
extern void HwMdMixCmd(unsigned char ch, signed char off, unsigned char mute);

#define PAL_GREY  0x0000   /* boot font palette lines (md_start.s CRAM) */
#define PAL_GREEN 0x2000
#define PAL_RED   0x4000

#define PAD_U 0x0001
#define PAD_D 0x0002
#define PAD_L 0x0004
#define PAD_R 0x0008
#define PAD_B 0x0010
#define PAD_C 0x0020
#define PAD_A 0x0040
#define PAD_S 0x0080
#define PAD_MODE 0x0800   /* six-button: 0 0 0 1 M X Y Z S A C B R L D U */

static char hexdig(unsigned v) { v &= 15; return (char)(v < 10 ? '0' + v : 'A' + (v - 10)); }

static void put_hex8(int x, int y, unsigned v, uint16_t color) {
	char s[3];
	s[0] = hexdig(v >> 4); s[1] = hexdig(v); s[2] = 0;
	HwMdPuts(s, color, x, y);
}

static void put_hex16(int x, int y, unsigned v, uint16_t color) {
	char s[5];
	s[0] = hexdig(v >> 12); s[1] = hexdig(v >> 8);
	s[2] = hexdig(v >> 4);  s[3] = hexdig(v); s[4] = 0;
	HwMdPuts(s, color, x, y);
}

/* Channel VU (visual aid for balancing the mix, Mike 2026-09-02): 8 bars,
 * FM ch0-5 + PSG 0-1, level 0..15 per channel from the 68K feeder
 * (COMM10 = ch0-3 nibbles, COMM2 = ch4-7), synced to the block the Z80
 * is playing. Instant rise, slow decay; only changed rows are redrawn.
 * HORIZONTAL bars, one row per channel, in the region nothing else
 * uses: rows VU_Y0..VU_Y0+7, label at column VU_LX, track columns
 * VU_X0..VU_X0+VU_LEN-1. The font has no solid block (unmapped chars
 * are the tile-1 dot), so lit cells are '|' on a '.' track. Levels come
 * from the 68K per ~75ms sub-window (see music_feed), so the bars track
 * individual notes. */
#define VU_LX   24                    /* channel label            */
#define VU_MK   25                    /* '>' = mixer selection    */
#define VU_X0   26                    /* bar track 26..35         */
#define VU_LEN  10
#define VU_OX   37                    /* mixer offset text 37..39 */
#define VU_Y0   13
static uint8_t vu_shown[8], vu_drawn[8];
static uint8_t vu_tick;

/* Live mixer (Mike 2026-09-03): MODE toggles mixer mode. In it,
 * LEFT/RIGHT pick a channel, UP/DOWN change its level by 3 TL (~2dB;
 * UP = louder), C mutes/unmutes. Each change goes to the 68K feeder
 * (HwMdMixCmd -> case 34) and lands within ~1s. Rows show the offset. */
static uint8_t mix_on, mix_sel;
static int8_t  mix_off[8];
static uint8_t mix_mute[8];

static void mix_draw_row(int c) {
	char s[4];
	int8_t o = mix_off[c];
	if (mix_mute[c]) { s[0] = 'M'; s[1] = 'U'; s[2] = 'T'; }
	else {
		int a = o < 0 ? -o : o;
		s[0] = o < 0 ? '+' : (o > 0 ? '-' : ' ');   /* louder / quieter */
		s[1] = (char)('0' + a / 10); s[2] = (char)('0' + a % 10);
	}
	s[3] = 0;
	HwMdPuts(s, mix_mute[c] ? PAL_GREEN : PAL_GREY, VU_OX, VU_Y0 + c);
	HwMdPutc((mix_on && c == mix_sel) ? '>' : ' ', PAL_GREEN, VU_MK, VU_Y0 + c);
}

static void mix_title(void) {
	HwMdPuts(mix_on ? "MIX U-D C=MUTE" : "CHANNEL VU    ", PAL_GREY,
	         VU_X0, VU_Y0 - 1);
}

static void vu_draw(void) {
	/* COMM10 only (COMM2 is the command parameter register — never
	 * touch it): bit 15 = which half (0: ch0-3, 1: ch4-7), bits 0-11 =
	 * four 3-bit levels. Each half refreshes at 30Hz; the other half
	 * keeps decaying. */
	static const uint8_t l3to4[8] = { 0, 2, 4, 6, 8, 10, 12, 15 };
	uint16_t w = MARS_SYS_COMM10;
	int base = (w & 0x8000) ? 4 : 0;
	vu_tick++;
	for (int c = 0; c < 8; c++) {
		uint8_t lvl = vu_shown[c];
		if (c >= base && c < base + 4)
			lvl = l3to4[(w >> ((c - base) * 3)) & 7];
		if (lvl >= vu_shown[c]) vu_shown[c] = lvl;           /* rise */
		else if (vu_shown[c]) vu_shown[c]--;                  /* decay 1/frame */
		uint8_t w = (uint8_t)((vu_shown[c] * VU_LEN + 7) / 15);
		if (w == vu_drawn[c]) continue;
		int y = VU_Y0 + c;
		uint16_t col = c < 6 ? PAL_GREEN : PAL_GREY;
		/* redraw only the cells between the old and new bar ends */
		uint8_t lo = w < vu_drawn[c] ? w : vu_drawn[c];
		uint8_t hi = w > vu_drawn[c] ? w : vu_drawn[c];
		for (int i = lo; i < hi; i++)
			HwMdPutc(i < w ? '|' : '.', col, VU_X0 + i, y);
		vu_drawn[c] = w;
	}
}

static void vu_init(void) {
	static const char lab[8] = { '0', '1', '2', '3', '4', '5', 'P', 'P' };
	mix_title();
	for (int c = 0; c < 8; c++) {
		HwMdPutc(lab[c], c < 6 ? PAL_GREEN : PAL_GREY, VU_LX, VU_Y0 + c);
		for (int i = 0; i < VU_LEN; i++)
			HwMdPutc('.', PAL_GREY, VU_X0 + i, VU_Y0 + c);
		vu_shown[c] = vu_drawn[c] = 0;
		mix_draw_row(c);
	}
}

int m_main(void) {
	uint8_t  cmd = 0x94;       /* boot on round-1 BGM, not 0x00: command
	                            * 0x00 is the arcade STOP-ALL byte, so a
	                            * menu that boots there makes the first A
	                            * press silent by design (2026-09-01) */
	uint8_t  ym_op = 0;        /* next case-15 op C will send: 1,2,3,0,... */
	uint8_t  music_on = 0;     /* START toggles the P3 streaming test track */
	uint16_t edge_hist = 0;    /* last 4 button edges, newest nibble low:
	                            * 1=A 2=B 3=C 4=START — the input flight
	                            * recorder for pad-mapping disputes */
	uint16_t prev_pad = 0;
	uint16_t held = 0;         /* frames the current direction has been held */
	uint32_t prev_tick = 0;

	HwMdClearScreen();
	HwMdPuts("SEGA16 SOUND LAB", PAL_GREEN, 12, 2);
	HwMdPuts("SYSTEM 16B SOUND ENGINE TEST ROM", PAL_GREY, 4, 3);
	HwMdPuts("CMD", PAL_GREY, 8, 7);
	HwMdPuts("UP-DN +-01   LT-RT +-10", PAL_GREY, 8, 10);
	HwMdPuts("A POST   B POST 00   C YM TEST", PAL_GREY, 8, 11);
	HwMdPuts("LAST POST", PAL_GREY, 8, 14);
	HwMdPuts("COUNT", PAL_GREY, 8, 15);
	HwMdPuts("YM OP", PAL_GREY, 8, 16);
	HwMdPuts("PAD", PAL_GREY, 8, 17);
	HwMdPuts("PCM ACT", PAL_GREY, 8, 18);
	HwMdPuts("UNDERRUN", PAL_GREY, 8, 19);
	HwMdPuts("MUS FEED", PAL_GREY, 8, 20);
	HwMdPuts("BTN HIST", PAL_GREY, 8, 21);   /* 1=A 2=B 3=C 4=ST, newest right */
	HwMdPuts("A PLAYS THE ARCADE COMMAND MAP", PAL_GREEN, 5, 22);
	HwMdPuts("MUSIC 94-9D   SOUND.MD P4", PAL_GREY, 7, 23);
	vu_init();

	for (;;) {
		/* Pace on the 68K's frame tick (word write at COMM12; reading
		 * the 32-bit reg sees it change once per frame). */
		uint32_t t;
		do { t = MARS_SYS_COMM12; } while (t == prev_tick);
		prev_tick = t;

		uint16_t pad  = MARS_SYS_COMM8;
		uint16_t edge = (uint16_t)(pad & ~prev_pad);
		prev_pad = pad;

		/* COMM4 speech doorbell from the 68K router: {serial<<8 | bank
		 * idx}; serial change = play. (COMM4 held the boot handshake's
		 * "S_OK" high word, so prime prev on first pass, not play.) */
		{
			static uint16_t prev_bell = 0xFFFF;
			uint16_t bell = MARS_SYS_COMM4;
			if (prev_bell == 0xFFFF) {
				prev_bell = bell;
			} else if (bell != prev_bell) {
				prev_bell = bell;
				uint8_t idx = (uint8_t)(bell & 0xFF);
				if (idx < SPEECH_BANK_COUNT)
					sfx_play(0, idx, 255, 0);
			}
		}

		/* Auto-repeat for the directions: first repeat after 15 frames,
		 * then every 4. */
		if (pad & (PAD_U | PAD_D | PAD_L | PAD_R)) {
			held++;
			if (held > 15 && (held & 3) == 0)
				edge |= pad & (PAD_U | PAD_D | PAD_L | PAD_R);
		} else {
			held = 0;
		}

		/* MODE toggles the live mixer; the directions and C are then the
		 * mixer's (channel select / level / mute) instead of the command
		 * navigator's. */
		if (edge & PAD_MODE) {
			mix_on = (uint8_t)!mix_on;
			mix_title();
			for (int c = 0; c < 8; c++) mix_draw_row(c);
		}
		if (!mix_on) {
			if (edge & PAD_U) cmd++;
			if (edge & PAD_D) cmd--;
			if (edge & PAD_R) cmd += 0x10;
			if (edge & PAD_L) cmd -= 0x10;
		} else {
			uint8_t prev = mix_sel, changed = 0;
			if (edge & PAD_R) mix_sel = (uint8_t)((mix_sel + 1) & 7);
			if (edge & PAD_L) mix_sel = (uint8_t)((mix_sel + 7) & 7);
			if (edge & PAD_U) { mix_off[mix_sel] -= 3; changed = 1; }   /* louder */
			if (edge & PAD_D) { mix_off[mix_sel] += 3; changed = 1; }   /* quieter */
			if (edge & PAD_C) { mix_mute[mix_sel] ^= 1; changed = 1; }
			if (mix_off[mix_sel] < -60) mix_off[mix_sel] = -60;
			if (mix_off[mix_sel] >  60) mix_off[mix_sel] =  60;
			if (changed)
				HwMdMixCmd(mix_sel, mix_off[mix_sel], mix_mute[mix_sel]);
			if (changed || prev != mix_sel) {
				mix_draw_row(prev);
				mix_draw_row(mix_sel);
			}
			edge &= (uint16_t)~(PAD_U | PAD_D | PAD_L | PAD_R | PAD_C);
		}

		if (edge & PAD_A)     edge_hist = (uint16_t)((edge_hist << 4) | 1);
		if (edge & PAD_B)     edge_hist = (uint16_t)((edge_hist << 4) | 2);
		if (edge & PAD_C)     edge_hist = (uint16_t)((edge_hist << 4) | 3);
		if (edge & PAD_S)     edge_hist = (uint16_t)((edge_hist << 4) | 4);

		/* A+B edging in the SAME frame is never a human intent — it is
		 * the input stack double-firing one physical press (observed on
		 * Mike's ares 2026-09-01: every play was stopped within the
		 * frame, "PCM starting and being immediately cut off"). Play
		 * wins; B still stops when pressed alone. */
		if ((edge & PAD_A) && (edge & PAD_B))
			edge = (uint16_t)(edge & ~PAD_B);

		if (edge & PAD_A) {
			/* The router (68K case 32) is the real dispatcher now:
			 * music/ymsfx go to the Z80 player, speech comes back to
			 * this CPU via the COMM4 doorbell below. */
			HwMdSndCmd(cmd);
		}
		if (edge & PAD_B) {
			HwMdSndCmd(0x00);
			sfx_stop_all();
		}
		if (edge & PAD_C) {
			static const uint8_t ops[4] = { 1, 2, 3, 0 };
			HwMdYmCmd(ops[ym_op]);
			ym_op = (uint8_t)((ym_op + 1) & 3);
		}
		if (edge & PAD_S) {
			/* START cycles: off -> arcade music (P4 cut) -> test
			 * arpeggio -> off. */
			music_on = (uint8_t)((music_on + 1) % 3);
			static const uint8_t mops[3] = { 0, 2, 1 };
			HwMdMusicCmd(mops[music_on]);
		}

		/* Status: the 68K router publishes {count, last byte} on COMM6 —
		 * drawing from COMM6 (not from what we sent) proves the round
		 * trip through the engine's front door. */
		put_hex8(13, 7, cmd, PAL_GREEN);
		{
			uint16_t ack = MARS_SYS_COMM6;
			put_hex8(19, 14, ack & 0xFF, PAL_GREEN);
			put_hex8(19, 15, ack >> 8, PAL_GREY);
		}
		put_hex8(19, 16, ym_op, PAL_GREY);
		put_hex16(19, 17, pad, PAL_GREY);
		put_hex8(19, 18, snd_active_mask(), PAL_GREEN);
		put_hex16(19, 19, snd_underruns(), PAL_GREY);
		/* COMM14 = {produced blk, consumed blk}: moving numbers mean the
		 * Z80 is eating the stream and the 68K is refilling behind it. */
		put_hex16(19, 20, MARS_SYS_COMM14, music_on ? PAL_GREEN : PAL_GREY);
		put_hex16(19, 21, edge_hist, PAL_GREEN);
		vu_draw();
	}
	return 0;
}
