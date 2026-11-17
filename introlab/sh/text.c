/* The launcher's words inside the port: a 40 x 28 grid of 8-pixel cells drawn into the 32X
 * page, in front of the cabinet. The lab draws the same words on the MD text plane through
 * its own 68K (video.c); the port's 68K is the game's, so here they are pixels.
 *
 * Every cell that holds a glyph is drawn again after each frame's drawing, because the
 * cabinet's clear may have passed over it. A cell whose glyph went or changed gets the
 * room's background back first, once on each page. The letters are the game's own
 * (tools/gen_arcade_font.py): a face and the shadow drawn into each glyph, so they read on
 * the wall picture and on the floor alike. The chosen region's flag is drawn here too,
 * on the floor under the menu, in the half of the screen the cabinet is not in. */
#include "fx.h"
#include "video.h"
#include "cabinet.h"
#include "font_arcade.h"

#define COLS 40
#define ROWS 28
static uint8_t cell[ROWS][COLS];        /* glyph (1..) | colour << 6; 0 = empty */
static uint8_t stale[ROWS][COLS];       /* bit p: page p shows something this cell no longer holds */
static uint8_t pen[3] = { 255, 255, 255 }, edge;
enum { F_WHITE, F_RED, F_BLUE, F_N };
static uint8_t flag_pen[F_N];
static int flag_region = -1;            /* -1: no flag showing */
static uint8_t flag_stale;              /* bit p: page p may still show a flag that went */

#define FLAG_W 26
#define FLAG_H 16
#define FLAG_Y 188
static const int16_t flag_x[2] = { 203, 91 };       /* centred under each side's marquee */

static int glyph(char c) {              /* ASCII from space; no lower case in the game's set */
	if (c >= 'a' && c <= 'z') c = (char)(c - 32);
	return c >= 32 && c < 32 + FONTA_N ? c - 32 : 0;
}

void ui_clear(void) {
	for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) if (cell[r][c]) { cell[r][c] = 0; stale[r][c] = 3; }
}

void ui_puts(const char *s, int colour, int col, int row) {
	if (row < 0 || row >= ROWS) return;
	for (; *s && col < COLS; s++, col++) {
		int g = glyph(*s);
		uint8_t v = g > 0 && g < FONTA_N ? (uint8_t)(g | (colour << 6)) : 0;
		if (col >= 0 && cell[row][col] != v) { cell[row][col] = v; stale[row][col] = 3; }
	}
}

/* The palette is full (cabinet, wall, floor), so the words borrow from it: for each wanted
 * colour, the entry now on screen that is nearest to it. The cabinet's palette has no green,
 * so the lab's green (the selected item) is white here and its grey is a dimmer grey. */
void ui_colours(void) {
	static const uint8_t want[4 + F_N][3] = { { 15, 15, 17 }, { 31, 31, 31 }, { 31, 6, 4 }, { 4, 5, 12 },
		{ 30, 30, 30 }, { 25, 5, 6 }, { 5, 6, 16 } };
	volatile uint16_t *cram = &MARS_CRAM;
	for (int k = 0; k < 4 + F_N; k++) {
		int best = 0, bestd = 0x7FFFFFFF;
		for (int i = 1; i < 256; i++) {
			uint16_t c = cram[i];
			if (c & 0x8000u) continue;                  /* a colour behind the MD layer (the room's, in the port): the carpet and the smoke would cover it */
			{ int dr = (c & 31) - want[k][0], dg = ((c >> 5) & 31) - want[k][1], db = ((c >> 10) & 31) - want[k][2];
			int d = dr * dr + dg * dg + db * db;
			if (d < bestd) { bestd = d; best = i; } }
		}
		if (k < 3) pen[k] = (uint8_t)best; else if (k == 3) edge = (uint8_t)best; else flag_pen[k - 4] = (uint8_t)best;
	}
}

void ui_erase(uint8_t *fb, int page) {
	uint8_t bit = (uint8_t)(1 << (page & 1));
	if (flag_stale & bit) {                 /* the flag went or changed sides: both places get the room back */
		flag_stale &= (uint8_t)~bit;
		for (int k = 0; k < 2; k++) cabinet_restore(fb, flag_x[k] - 1, FLAG_Y - 1, flag_x[k] + FLAG_W + 1, FLAG_Y + FLAG_H + 1);
	}
	for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) {
		if (!(stale[r][c] & bit)) continue;
		stale[r][c] &= (uint8_t)~bit;
		cabinet_restore(fb, c * 8, r * 8, c * 8 + 8, r * 8 + 8);
	}
}

static void put(uint8_t *fb, const uint8_t *g, int x, int y, uint8_t colour) {
	for (int j = 0; j < 8; j++, y++) {
		uint8_t bits = g[j];
		if (!bits || y >= FB_H) continue;
		uint8_t *p = fb + y * FB_W + x;
		for (int i = 0; i < 8; i++) if ((bits & (0x80 >> i)) && x + i < FB_W) p[i] = colour;
	}
}

/* THE FLAGS: 26 x 16, our own drawing, in a one-pixel frame. Stripes and a dotted corner
 * for the US, a disc for Japan; the region not chosen is drawn in the dim pens. */

void ui_flags(int region) {
	if (region != flag_region) { flag_region = region; flag_stale = 3; }
}

static void flag(uint8_t *fb, int which) {
	const uint8_t *pn = flag_pen;
	uint8_t frame = flag_pen[F_WHITE];
	for (int j = -1; j <= FLAG_H; j++) {
		uint8_t *p = fb + (FLAG_Y + j) * FB_W + flag_x[which];
		for (int i = -1; i <= FLAG_W; i++) {
			int c;
			if (i < 0 || j < 0 || i == FLAG_W || j == FLAG_H) { p[i] = frame; continue; }
			if (which == 0) {
				if (i < 11 && j < 9) c = (i & 1) && (j & 1) ? F_WHITE : F_BLUE;
				else c = ((j * 13) >> 4) & 1 ? F_WHITE : F_RED;
			} else {
				int dx = 2 * i - (FLAG_W - 1), dy = 2 * j - (FLAG_H - 1);
				c = dx * dx + dy * dy <= 81 ? F_RED : F_WHITE;
			}
			p[i] = pn[c];
		}
	}
}

void ui_draw(uint8_t *fb, int page) {
	(void)page;
	if (flag_region >= 0) flag(fb, flag_region);
	for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) {
		uint8_t v = cell[r][c];
		if (!v) continue;
		put(fb, fonta[v & 63][1], c * 8, r * 8, edge);
		put(fb, fonta[v & 63][0], c * 8, r * 8, pen[v >> 6]);
	}
}
