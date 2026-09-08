/* sndtest SH-2 -> 68K command senders, trimmed from 32x-builder
 * sh_src/mars.c (same author). Only what the lab uses: the MD text
 * plane, MD CRAM, the YM2612 relay, and the engine's front door. */
#include "mars.h"

void HwMdClearScreen(void) {
	while(MARS_SYS_COMM0) ; // wait until 68000 has responded to any earlier requests
	MARS_SYS_COMM0 = 0x0400; // Clear Screen (Name Table B)
	while(MARS_SYS_COMM0) ;
}

void HwMdSetOffset(unsigned short offset) {
	while(MARS_SYS_COMM0) ;
	MARS_SYS_COMM2 = offset;
	MARS_SYS_COMM0 = 0x0500; // Set offset (into either Name Table B or VRAM)
	while(MARS_SYS_COMM0) ;
}

void HwMdSetNTable(unsigned short word) {
	while(MARS_SYS_COMM0) ;
	MARS_SYS_COMM2 = word;
	MARS_SYS_COMM0 = 0x0600; // Set word at offset in Name Table B
	while(MARS_SYS_COMM0) ;
}

void HwMdSetColor(unsigned short index, unsigned short color) {
	while(MARS_SYS_COMM0) ;
	MARS_SYS_COMM2 = color;
	MARS_SYS_COMM0 = 0x0800 | (index & 0xFF); // Set MD CRAM entry (BGR word)
	while(MARS_SYS_COMM0) ;
}

void HwMdYmCmd(unsigned char op) {
	/* Whole-action YM smoke test (68K case 15): 0 all off, 1 patch +
	 * key on, 2 retrigger, 3 release. Bounded — no COMM handshake may
	 * hold the caller forever. */
	uint32_t guard = 2000000;
	while (MARS_SYS_COMM0 && --guard) ;
	MARS_SYS_COMM0 = (unsigned short)(0x0F00 | op);
	guard = 2000000;
	while (MARS_SYS_COMM0 && --guard) ;
}

void HwMdYmWrite(unsigned char reg, unsigned char val) {
	/* One YM2612 part-I register write (68K case 14). FIRE-AND-FORGET:
	 * waits for the slot to be free, posts, and returns. Callers pace
	 * themselves to ~one write per frame — the frame-spaced stream is
	 * the PROVEN-sounding shape; bursts landed silent (B00246). */
	uint32_t guard = 2000000;
	while (MARS_SYS_COMM0 && --guard) ;
	MARS_SYS_COMM2 = val;
	MARS_SYS_COMM0 = (unsigned short)(0x0E00 | reg);
}

void HwMdMusicCmd(unsigned char op) {
	/* MUSIC control (68K case 33): 1 = start the streaming test track
	 * (player upload + ring prime + Z80 release), 0 = stop (busreq-park
	 * + key-off + PSG silence). Bounded round trip. */
	uint32_t guard = 2000000;
	while (MARS_SYS_COMM0 && --guard) ;
	MARS_SYS_COMM0 = (unsigned short)(0x2100 | op);
	guard = 2000000;
	while (MARS_SYS_COMM0 && --guard) ;
}

void HwMdSndCmd(unsigned char cmd) {
	/* THE ENGINE'S FRONT DOOR (68K case 32): one arcade sound-command
	 * byte, the same value the game's MCU mailbox carries. Blocking
	 * round trip, bounded. */
	uint32_t guard = 2000000;
	while (MARS_SYS_COMM0 && --guard) ;
	MARS_SYS_COMM0 = (unsigned short)(0x2000 | cmd);
	guard = 2000000;
	while (MARS_SYS_COMM0 && --guard) ;
}

void HwMdMixCmd(unsigned char ch, signed char off, unsigned char mute) {
	/* Live mixer (68K case 34): per-channel TL offset (+ = quieter,
	 * 0.75dB/unit) and mute, applied by the 68K feeder as the music
	 * streams. COMM2 is the parameter, written BEFORE COMM0 (the
	 * command server reads it in the handler). */
	uint32_t guard = 2000000;
	while (MARS_SYS_COMM0 && --guard) ;
	MARS_SYS_COMM2 = (unsigned short)(((mute & 1) << 8) | ((unsigned char)off));
	MARS_SYS_COMM0 = (unsigned short)(0x2200 | (ch & 7));
	guard = 2000000;
	while (MARS_SYS_COMM0 && --guard) ;
}

static void NextChr(char c, uint16_t color) {
	if(c >= '0' && c <= '9') {
		c = c - '0' + 2;
	} else if(c >= 'A' && c <= 'Z') {
		c = c - 'A' + 12;
	} else if(c >= 'a' && c <= 'z') {
		c = c - 'a' + 12;
	} else if(c == ':') { c = 38;
	} else if(c == '.') { c = 39;
	} else if(c == '-') { c = 40;
	} else if(c == '>') { c = 41;
	} else if(c == '|') { c = 42;
	} else if(c == '+') { c = 43;
	} else if(c == '%') { c = 44;
	} else if(c == ' ') {
		c = 0;
	} else {
		c = 1;
	}
	HwMdSetNTable(c | color);
}

void HwMdPuts(char *str, uint16_t color, int x, int y) {
	HwMdSetOffset(((y<<6) | x) << 1);
	while(*str) NextChr(*str++, color);
}

void HwMdPutc(char chr, uint16_t color, int x, int y) {
	HwMdSetOffset(((y<<6) | x) << 1);
	NextChr(chr, color);
}
