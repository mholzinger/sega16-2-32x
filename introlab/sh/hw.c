/* introlab SH-2 -> 68K command senders for the MD text plane (trimmed from sndtest/sh/hw.c). */
#include "mars.h"
#include "video.h"

static void md_cmd(unsigned short cmd, unsigned short param) {
	while (MARS_SYS_COMM0) ;
	MARS_SYS_COMM2 = param;
	MARS_SYS_COMM0 = cmd;
	while (MARS_SYS_COMM0) ;
}

void HwMdClearScreen(void) { md_cmd(0x0400, 0); }

static unsigned short glyph(char c) {
	if (c >= '0' && c <= '9') return (unsigned short)(c - '0' + 2);
	if (c >= 'A' && c <= 'Z') return (unsigned short)(c - 'A' + 12);
	if (c >= 'a' && c <= 'z') return (unsigned short)(c - 'a' + 12);
	switch (c) {
	case ':': return 38; case '.': return 39; case '-': return 40; case '>': return 41;
	case '|': return 42; case '+': return 43; case '%': return 44; case ' ': return 0;
	}
	return 1;
}

void HwMdPuts(char *str, uint16_t color, int x, int y) {
	md_cmd(0x0500, (unsigned short)(((y << 6) | x) << 1));
	while (*str) md_cmd(0x0600, (unsigned short)(glyph(*str++) | color));
}

#ifdef SMOKE
/* THE SMOKE LAYER: our own wisps (tools/gen_wisps.py) on MD plane A, between the wall and
 * the cabinet. The 32X shows a colour in front of the MD picture only when bit 15 of its
 * CRAM word is set, so the cabinet's colours carry it (cabinet_light) and the wall's do
 * not. The 68K owns the motion; this side uploads the picture and sets its three colours. */
#include "smoke_data.h"
#define SMOKE_TILE0 0x100                    /* above the font */
void smoke_start(void) {
	md_cmd(0x0900, SMOKE_TILE0 * 32);
	for (unsigned i = 0; i < SMOKE_NTILES * 16; i++) md_cmd(0x0A00, smoke_tiles[i]);
	md_cmd(0x0900, (unsigned short)(0xC000 + SMOKE_ROW0 * 128));
	for (unsigned i = 0; i < SMOKE_ROWS * 64; i++)
		md_cmd(0x0A00, smoke_map[i] ? (unsigned short)(0x6000 | (SMOKE_TILE0 - 1 + smoke_map[i])) : 0);
	md_cmd(0x0B00, 1);
}
void smoke_stop(void) { md_cmd(0x0B00, 0); }
/* The three shades at level/256, on MD palette line 3. */
void smoke_light(int level) {
	for (int i = 0; i < 3; i++) {
		unsigned c = smoke_shades[i], o = 0;
		for (int sh = 1; sh < 12; sh += 4) o |= ((((c >> sh) & 7) * (unsigned)level) >> 8) << sh;
		md_cmd((unsigned short)(0x0800 | (49 + i)), (unsigned short)o);
	}
}
#endif
