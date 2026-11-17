/* introlab: flat convex polygon fill, shared by the cabinet's untextured faces and the doors.
 * A flat face needs only x along its edges: one divide per edge, and a span is a fill.
 * Framebuffer writes are the expensive part, so a span goes out as longs where it can. */
#include "fx.h"
#include "video.h"

typedef struct { int32_t x, dx; int yend; } xedge_t;

/* Start walking the edge a -> b (a is the upper end) at scanline yy. Scanline yy samples
 * at y = yy + 0.5, so an edge covers scanlines (a.y + 7) >> 4 up to (b.y + 7) >> 4. */
static inline void xedge_start(xedge_t *e, const int32_t *a, const int32_t *b, int yy) {
	int32_t dy = b[1] - a[1];
	e->yend = (b[1] + 7) >> 4;
	if (dy <= 0 || e->yend <= yy) { e->yend = yy; return; }     /* flat, or covers nothing */
	e->dx = sdiv((b[0] - a[0]) << 16, dy);
	e->x  = (a[0] << 12) + mul16(e->dx, ((yy << 4) + 8 - a[1]) << 12);
}

HOT void poly_flat(uint8_t *fb, const int32_t *xy, int n, int y0, int y1, uint8_t colour) {
	int top = 0;
	int32_t ymax = xy[1];
	for (int i = 1; i < n; i++) {
		if (xy[2 * i + 1] < xy[2 * top + 1]) top = i;
		if (xy[2 * i + 1] > ymax) ymax = xy[2 * i + 1];
	}
	int yy = (xy[2 * top + 1] + 7) >> 4, ybot = (ymax + 7) >> 4;
	if (yy < y0) yy = y0;
	if (ybot > y1) ybot = y1;
	if (yy >= ybot) return;

	uint32_t colour4 = colour * 0x01010101u;
	xedge_t ea = { 0, 0, yy }, eb = { 0, 0, yy };       /* the two chains down from the top corner */
	int ia = top, ib = top, used = 0;
	uint8_t *row = fb + yy * FB_W;
	for (; yy < ybot; yy++, row += FB_W) {
		while (yy >= ea.yend) {
			if (used++ >= n) return;
			int nx = ia ? ia - 1 : n - 1;
			xedge_start(&ea, &xy[2 * ia], &xy[2 * nx], yy);
			ia = nx;
		}
		while (yy >= eb.yend) {
			if (used++ >= n) return;
			int nx = ib + 1 < n ? ib + 1 : 0;
			xedge_start(&eb, &xy[2 * ib], &xy[2 * nx], yy);
			ib = nx;
		}
		int32_t xl = ea.x, xr = eb.x;
		if (xl > xr) { xl = eb.x; xr = ea.x; }
		int xa = (xl + 0x7FFF) >> 16, xb = ((xr + 0x7FFF) >> 16) - 1;   /* pixel centres inside */
		if (xa < 0) xa = 0;
		if (xb > FB_W - 1) xb = FB_W - 1;
		int count = xb - xa + 1;
		if (count > 0) {
			uint8_t *p = row + xa;
			while (((uintptr_t)p & 3) && count) { *p++ = colour; count--; }
			for (int k = count >> 2; k > 0; k--) { *(uint32_t *)(void *)p = colour4; p += 4; }
			for (count &= 3; count > 0; count--) *p++ = colour;
		}
		ea.x += ea.dx;
		eb.x += eb.dx;
	}
}
