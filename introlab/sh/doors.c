/* introlab: the intro doors. Everything is a flat convex polygon (poly.c), so a frame is a
 * transform of ~430 vertices and ~190 fills. The doors never overlap and each door's
 * polygons are packed back to front (panel, frame, logo), so they are drawn in array order
 * with no sorting. Shading is not per polygon: a door is one flat plane, so its
 * colours are simply dimmed in the palette as it turns (doors_light). The logo's fill is
 * a ramp of bands, each its own palette entry, which is also what lets a highlight run
 * down it for nothing. */
#include "fx.h"
#include "doors.h"
#include "doors_data.h"
#include "video.h"

typedef struct { int32_t xy[8]; uint8_t nv, colour, pad[2]; } dpoly_t;
typedef struct {                /* one frame, primary -> secondary */
	uint8_t *fb;
	int32_t  n;
	int32_t  clear;             /* the room shows between the doors: clear first */
	dpoly_t  p[DOORS_NPOLYS];
} dframe_t;

static dframe_t dframe;         /* written by the primary only */
static dframe_t dframe_s;       /* the secondary's private copy */
static int32_t  pv[DOORS_NVERTS][2];

void doors_light(int level, int angle, int shimmer) {
	volatile uint16_t *cram = &MARS_CRAM;
	int lit = (8192 + (icos(angle) >> 1)) * level >> 8;         /* (0.5 + 0.5 cos) x level, 2.14 */
	for (int i = 0; i < DOORS_NCRAM; i++) {
		int k = i ? lit : level << 6;                           /* the room is not a door */
		int r = doors_rgb[i][0] * k >> 14, g = doors_rgb[i][1] * k >> 14, b = doors_rgb[i][2] * k >> 14;
		/* The shimmer: a highlight that travels down the logo's bands. `shimmer` is the
		 * band it sits on, in 1/16ths; bands within one of it are lifted toward white. */
		if (i >= DOORS_BAND0 && shimmer >= 0) {
			int d = ((i - DOORS_BAND0) << 4) - shimmer;
			if (d < 0) d = -d;
			if (d < 16) {
				int add = (16 - d) * level >> 8;                /* 0..16 */
				r += (31 - r) * add >> 5; g += (31 - g) * add >> 5; b += (31 - b) * add >> 5;
			}
		}
		cram[i] = (uint16_t)COLOR(r, g, b);
	}
}

static HOT void doors_raster(const dframe_t *fr, int y0, int y1) {
	if (fr->clear) {
		uint32_t *p = (uint32_t *)(void *)(fr->fb + y0 * FB_W);
		for (int n = (y1 - y0) * FB_W / 4; n > 0; n--) *p++ = 0;
	}
	for (int i = 0; i < fr->n; i++) poly_flat(fr->fb, fr->p[i].xy, fr->p[i].nv, y0, y1, fr->p[i].colour);
}

void doors_secondary(void) {
	const dframe_t *src = UNCACHED(&dframe);
	dframe_s.fb = src->fb; dframe_s.n = src->n; dframe_s.clear = src->clear;
	const uint32_t *s = (const uint32_t *)src->p;
	uint32_t *d = (uint32_t *)dframe_s.p;
	for (int k = dframe_s.n * (int)(sizeof(dpoly_t) / 4); k > 0; k--) *d++ = *s++;
	doors_raster(&dframe_s, SPLIT_ROW, FB_H);
}

void doors_draw(uint8_t *fb, int angle) {
	int32_t c = icos(angle), s = isin(angle);
	for (int i = 0; i < DOORS_NVERTS; i++) {
		int32_t r = doors_verts[i][0], y = doors_verts[i][1];
		int32_t along = (r * c) >> 14;                          /* how far the swing leaves it across the doorway */
		int32_t x = i < DOORS_NV_LEFT ? along - DOORS_UNIT : DOORS_UNIT - along;
		int32_t w = sdiv(1 << 28, DOORS_DIST + ((r * s) >> 14));   /* swinging inward: further away */
		pv[i][0] = (FB_W / 2) * 16 + mul32(DOORS_FOCAL_PX * 16 * x, w << 4);
		pv[i][1] = (FB_H / 2) * 16 - mul32(DOORS_FOCAL_PX * 16 * y, w << 4);
	}
	for (int i = 0; i < DOORS_NPOLYS; i++) {
		const doors_poly_t *q = &doors_polys[i];
		dpoly_t *d = &dframe.p[i];
		d->nv = q->nv; d->colour = q->kind;
		for (int k = 0; k < q->nv; k++) { d->xy[2 * k] = pv[q->vi[k]][0]; d->xy[2 * k + 1] = pv[q->vi[k]][1]; }
	}
	dframe.fb = fb;
	dframe.n = DOORS_NPOLYS;
	dframe.clear = angle != 0;                                  /* closed doors cover the whole screen */

	FX_JOB = CMD_DOORS;
	doors_raster(&dframe, 0, SPLIT_ROW);
	while (FX_JOB != 0) ;
}
