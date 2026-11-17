/* The arcade carpet on the launcher's floor: zigzag strokes running red to lilac, with a
 * head and a tail, in staggered rows, and confetti between them, on navy. Our own drawing,
 * made from the numbers below: nothing is stored.
 *
 * carpet_pixel(x, y) is the colour (0 navy, 1-5 red, orange, yellow, green, lilac) of the
 * floor at a screen pixel, for the launcher's own camera: an eye 45 inches up behind a
 * 408-pixel lens whose horizon is row 133, the floor showing from row CARPET_TOP down. A
 * row is one distance, so the floor point moves along it in equal steps; a division per row
 * and none per pixel. The pattern is 13% under life size (Mike, 2026-11-14).
 *
 * The port draws it once, at the room's start, onto MD plane B, eight pixels by eight
 * (carpet_start, port_video.c). Positions are in 1/256 inch, the motif's own frame in 1/16. */
#include <stdint.h>
#include "carpet.h"

/* a stroke: start (1/16 in), unit direction (8.8), length (1/16 in), 65536 / length */
static const int16_t stroke[8][6] = {
	{-208,88,83,-242,177,371}, {-150,-79,92,239,160,409}, {-93,70,102,-235,144,455},
	{-35,-62,115,229,128,512}, {22,53,131,-220,113,582}, {80,-44,151,207,98,669},
	{-336,56,248,62,132,497},  /* the head, in red */
	{138,35,250,54,115,572},   /* the tail, in lilac */
};
#define COS_T 237                    /* the motif lies 22 degrees off the row, 8.8 */
#define SIN_T (-96)
#define CELL_X (40 * 256)            /* motifs every 40 x 26 inches, alternate rows half a step along */
#define CELL_Y (26 * 256)
#define DOT    (5 * 256)             /* confetti: at most one dot in each 5 inch square */

static uint32_t hash(int32_t i, int32_t j, uint32_t k) {
	uint32_t n = (uint32_t)i * 374761393u + (uint32_t)j * 668265263u + k * 2246822519u;
	n = (n ^ (n >> 13)) * 1274126177u;
	return (n ^ (n >> 16)) & 0xFFFFu;
}

static int32_t fdiv(int32_t a, int32_t b) {            /* floor(a / b), b > 0, a may be negative */
	int32_t q = a / b;
	return (a % b < 0) ? q - 1 : q;
}

static int motif(int32_t u, int32_t v) {
	int32_t j0 = fdiv(v, CELL_Y);
	int found = 0;
	for (int32_t j = j0 - 1; j <= j0 + 1; j++) {
		int32_t off = (j & 1) ? CELL_X / 2 : 0;
		int32_t i0 = fdiv(u - off, CELL_X);
		for (int32_t i = i0 - 1; i <= i0 + 1; i++) {
			int32_t cx = i * CELL_X + CELL_X / 2 + off, cy = j * CELL_Y + CELL_Y / 2;
			int32_t dx = u - cx, dy = v - cy;
			if (dx > 30 * 256 || dx < -30 * 256 || dy > 21 * 256 || dy < -21 * 256) continue;
			dx -= ((int32_t)hash(i, j, 1) - 32768) * 5 >> 8;       /* each motif a little off its place */
			dy -= ((int32_t)hash(i, j, 2) - 32768) * 4 >> 8;
			dx >>= 4; dy >>= 4;                                    /* 1/16 inch from here */
			{
				int32_t lx = (dx * COS_T + dy * SIN_T) >> 8, ly = (dy * COS_T - dx * SIN_T) >> 8;
				if (lx > 24 * 16 || lx < -24 * 16 || ly > 9 * 16 || ly < -9 * 16) continue;
				for (int s = 0; s < 8; s++) {
					const int16_t *k = stroke[s];
					int32_t wx = lx - k[0], wy = ly - k[1];
					int32_t t = (wx * k[2] + wy * k[3]) >> 8, c = (wx * k[3] - wy * k[2]) >> 8;
					int32_t r = s < 6 ? 17 : 13;                   /* half width: 1.05 and 0.8 inch */
					if (c < 0) c = -c;
					if (t < 0 || t > k[4] || c >= r) continue;
					if (s == 6) { if (!found) found = 1; }
					else if (s == 7) { if (!found) found = 5; }
					else { int g = (int)((((s << 16) + t * k[5]) * 5) / (6 << 16)); found = 1 + (g > 4 ? 4 : g); }
				}
			}
		}
	}
	return found;
}

static int floor_colour(int32_t u, int32_t v) {
	int c = motif(u, v);
	if (c) return c;
	{
		int32_t ci = fdiv(u, DOT), cj = fdiv(v, DOT);
		if (hash(ci, cj, 3) < 14418u) {                            /* 22 squares in 100 have a dot */
			int32_t dx = (u - ci * DOT - 256 - ((int32_t)hash(ci, cj, 4) * 3 >> 6)) >> 4;
			int32_t dy = (v - cj * DOT - 256 - ((int32_t)hash(ci, cj, 5) * 3 >> 6)) >> 4;
			if (dx * dx + dy * dy < 141) return 1 + (int)(hash(ci, cj, 6) * 5 >> 16);
		}
	}
	return 0;
}

/* One row's place on the floor: how far away it is, and how far a pixel steps along it. */
static int32_t row_v, row_du;
void carpet_row(int y) {
	row_v = (45 * 408 * 256 * 100 / 87) / (y - 133);               /* distance, 1/256 inch of pattern */
	row_du = row_v / 408;
}
int carpet_pixel(int x) {
	return floor_colour((x - 160) * row_du + 500 * 256, row_v);
}
