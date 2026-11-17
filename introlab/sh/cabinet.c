/* introlab: the live cabinet renderer.
 *
 * WHAT IT DOES. Every vertex is rotated into camera space and projected once. Each face is
 * culled by its packed normal, flat-shaded by the same normal, and drawn far to near with
 * no z-buffer: the cabinet's shape allows a fixed order -- far side panel, body, near side
 * panel -- with a depth sort only inside each group (tools/cabinet/cabmesh.py, GROUPS).
 * Faces are convex, so a face is two edge chains walked down the screen. Texture
 * coordinates are perspective-correct at both ends of every span and linear between them.
 *
 * TWO CPUS. The primary builds the frame's draw list (faces in order, with their projected
 * corners), then the screen is split at a row: the primary rasterises above it and the
 * secondary below. They share nothing but the draw list and never write the same row.
 * The SH-2 caches are write-through, so the primary's list is in SDRAM as soon as it is
 * written; the secondary's own cache may hold last frame's copy, so it reads the list
 * through the uncached mirror into a private copy first.
 *
 * FIXED POINT. Vertices are int16 in 1/64 inch. Directions and sin/cos are 2.14. Screen
 * positions are 28.4 while projecting and 16.16 while scanning. w is 2^28 / depth and is
 * carried along edges as w << 8; u*w and v*w are carried unscaled. Texel coordinates are
 * 16.16 inside a span. Texels are stored as base colour x 4, so a pixel is texel | shade. Every 64-bit product is shifted by 16 or 32, which the SH-2 does
 * in one instruction; it has no barrel shifter.
 *
 * DIVIDES. One per vertex, four per edge, two per scanline of a textured face, all 32-bit.
 * They use the SH-2's hardware divider (39 cycles) unless built with SWDIV=1. Each CPU has
 * its own divider and nothing here runs in an interrupt, so no locking is needed. The
 * helpers and the flat fill are shared with the doors scene: fx.h, poly.c. */
#include <stdint.h>
#include "fx.h"
#include "cabinet.h"
#include "cabinet_data.h"
#include "video.h"
#include "crowd.h"

#define FOCAL_PX   408          /* 2.55 x half the screen width: the preview's lens */
#define NEAR_Z     1024         /* 16 inches; nearer than this is not drawn, and u*w*16 fits 32 bits */
/* Light in camera space (x right, y up, z into the screen), 2.14: upper left, behind the
 * viewer. Brightness = AMBIENT + DIFFUSE * (normal . light). */
#define LIGHT_X    (-7373)
#define LIGHT_Y    8192
#define LIGHT_Z    (-12124)
#define AMBIENT    7373         /* 0.45 */
#define DIFFUSE    9830         /* 0.60 */

/* THE ROOM: a back wall and a floor, each a vertical ramp, and the cabinet's shadow on the
 * floor. The background depends only on the row, so drawing it IS the clear: every row has
 * one fill pattern and costs what a plain clear cost. The ramps live in the CRAM entries the
 * cabinet's palette leaves free; neighbouring ramp colours are checkered on alternate rows
 * to double the number of steps. */
#define HORIZON_ROW CAB_BACKDROP_H   /* where the floor meets the back wall's picture */
#define WALL_N      CAB_BACKDROP_NCRAM  /* the back wall's colours, then the floor ramp */
#define ROOM_CRAM   CAB_BACKDROP_CRAM
#define FLOOR_N     10          /* floor ramp: horizon -> front of screen, getting lighter */
#define SHADOW_RX   58          /* the shadow ellipse, pixels */
#define SHADOW_RY   15
#define SHADOW_DX   7           /* pushed right and back a little: the light is upper left, in front */
#define SHADOW_DY   (-3)
#define SHADOW_DARK 7           /* half-steps down the floor ramp inside the shadow */


#if CAB_BANNER_H > 0 && SPLIT_ROW < 100
#error "the banner must fit above the split row"
#endif
#if ROOM_CRAM + WALL_N + FLOOR_N > 256
#error "the cabinet palette leaves no room for the back wall and the floor ramp"
#endif
#if CAB_NSHADE != 4
#error "the pixel writers assume 4 shade levels (texel << 2 | shade)"
#endif

typedef struct {                /* one polygon corner, ready to scan */
	int32_t x, y;               /* 28.4 */
	int32_t w;                  /* 2^28 / depth */
	int32_t uw, vw;             /* texel coordinate x w */
} corner_t;

typedef struct {                /* one face of the frame's draw list */
	corner_t c[4];
	uint8_t  face, shade, nv, tex;  /* tex: this region's texture, or CAB_NOTEX */
} item_t;

typedef struct {                /* everything the secondary needs for one frame */
	uint8_t *fb;
	int32_t  nitems;
	int16_t  shx, shy, pad0, pad1;  /* centre of the cabinet's shadow on the floor */
	int16_t  cx0, cy0, cx1, cy1;    /* the rectangle to clear first (x1, y1 exclusive) */
	item_t   item[CAB_NFACES];
} frame_t;

typedef struct {                /* an edge being walked, values at the current scanline */
	int32_t x, w, uw, vw;       /* x 16.16, w << 8, uw, vw */
	int32_t dx, dw, duw, dvw;   /* per scanline */
	int     yend;               /* first scanline this edge does not cover */
} edge_t;

typedef struct { int32_t x, y, w, z; } proj_t;

static frame_t frame;           /* written by the primary only */
static frame_t frame_s;         /* the secondary's private copy */
static proj_t  proj[CAB_NVERTS];
static uint32_t key[CAB_NFACES];
static uint8_t order[CAB_NFACES];
static uint8_t shade_of[CAB_NFACES];
static int16_t drawn[2][4] = { { 0, 0, FB_W, FB_H }, { 0, 0, FB_W, FB_H } };   /* per page: what needs clearing */
volatile uint16_t cabinet_ticks[4];
static uint32_t row_fill[FB_H];     /* each row's background, as four pixels */
static uint32_t row_shadow[FB_H];   /* the same row inside the shadow */
static int32_t  floor_z;            /* the cabinet's lowest vertex: the floor */
/* Texels as the rasteriser reads them. By default a copy in SDRAM, made once at start-up;
 * TEXROM=1 reads the cart instead. */
#ifdef TEXROM
#define texels cab_texels
#else
static uint8_t texels[sizeof cab_texels];
#endif

/* The back wall's picture, read by both CPUs when they clear. It stays in the cart: it is
 * read in order, a long at a time, and a copy would leave the primary's stack under 2 KB. */
#define backdrop cab_backdrop

static uint16_t room_cram[WALL_N + FLOOR_N];        /* the wall and floor ramps at full light */

/* Scale one 0BBBBBGGGGGRRRRR colour by level/256. */
static inline uint16_t dim(uint16_t c, int level) {
	return (uint16_t)((((c & 0x001F) * level >> 8) & 0x001F) | (((c & 0x03E0) * level >> 8) & 0x03E0)
	                | (((c & 0x7C00) * level >> 8) & 0x7C00));
}

void cabinet_light(int level) {
	volatile uint16_t *cram = &MARS_CRAM;
	if (level > 256) level = 256;
	if (level < 0) level = 0;
	for (int i = 0; i < CAB_NCRAM; i++) cram[i] = (uint16_t)(dim(cab_cram[i], level) | CRAM_FRONT);
	for (int i = 0; i < WALL_N + FLOOR_N; i++) cram[ROOM_CRAM + i] = (uint16_t)(dim(room_cram[i], level) | CRAM_BACK);
}

void cabinet_init(void) {
#ifndef TEXROM
	for (unsigned i = 0; i < sizeof cab_texels; i++) texels[i] = cab_texels[i];
#endif
	for (int i = 0; i < WALL_N; i++) room_cram[i] = cab_backdrop_cram[i];
	for (int i = 0; i < FLOOR_N; i++)               /* warmer floor, brightest at the viewer's feet */
		room_cram[WALL_N + i] = (uint16_t)COLOR(2 + (6 * i) / (FLOOR_N - 1), 1 + (5 * i) / (FLOOR_N - 1), 4 + (6 * i) / (FLOOR_N - 1));
#ifdef PORT
	uint32_t shadow_pen = 1; int darkest = 0x7FFF;
	for (int i = 1; i < CAB_NCRAM; i++) {
		int c = cab_cram[i], l = (c & 31) + ((c >> 5) & 31) + ((c >> 10) & 31);
		if (l < darkest) { darkest = l; shadow_pen = (uint32_t)i; }
	}
#endif
	for (int y = 0; y < FB_H; y++) {
		/* A ramp of N colours gives 2N-1 levels: even levels are one colour, odd levels a
		 * checker of two. `lit` is the row's level, `dark` the level inside the shadow. */
		int base, lit, dark;
		if (y < HORIZON_ROW) { row_fill[y] = row_shadow[y] = 0; continue; }   /* the wall is a picture */
		{
			base = ROOM_CRAM + WALL_N;
			lit  = ((y - HORIZON_ROW) * (2 * FLOOR_N - 2)) / (FB_H - 1 - HORIZON_ROW);
			dark = lit > SHADOW_DARK ? lit - SHADOW_DARK : 0;
		}
		for (int pass = 0; pass < 2; pass++) {
			int lv = pass ? dark : lit;
			uint32_t a = (uint32_t)(base + lv / 2), b = (uint32_t)(base + (lv + 1) / 2);
#ifdef PORT
			/* The port's floor is the carpet, on the MD layer in front of these colours, so
			 * a darker floor colour would not show. The shadow is every other pixel in the
			 * cabinet's darkest colour, which is in front of the carpet, and the carpet
			 * shows through the pixels between. */
			if (pass) a = shadow_pen;
#endif
			if (y & 1) { uint32_t t = a; a = b; b = t; }
			(pass ? row_shadow : row_fill)[y] = (a << 24) | (b << 16) | (a << 8) | b;
		}
	}
	floor_z = 0;
	for (int i = 0; i < CAB_NVERTS; i++) if (cab_verts[i][2] < floor_z) floor_z = cab_verts[i][2];
	fx_timer_start();
}

/* Start walking the edge a -> b (a is the upper end) at scanline yy. Scanline yy samples
 * at y = yy + 0.5, so the edge covers scanlines (a.y + 7) >> 4 up to (b.y + 7) >> 4. */
static HOT void edge_start(edge_t *e, const corner_t *a, const corner_t *b, int yy) {
	int32_t dy = b->y - a->y;
	e->yend = (b->y + 7) >> 4;
	if (dy <= 0 || e->yend <= yy) { e->yend = yy; return; }     /* flat, or covers nothing */
	int32_t pre = ((yy << 4) + 8 - a->y) << 12;                 /* a to the sample, x 2^16 / 16 */
	e->dx  = sdiv((b->x - a->x) << 16, dy);
	e->dw  = sdiv((b->w - a->w) << 12, dy);
	e->duw = sdiv((b->uw - a->uw) << 4, dy);
	e->dvw = sdiv((b->vw - a->vw) << 4, dy);
	e->x   = (a->x << 12) + mul16(e->dx, pre);
	e->w   = (a->w << 8)  + mul16(e->dw, pre);
	e->uw  = a->uw + mul16(e->duw, pre);
	e->vw  = a->vw + mul16(e->dvw, pre);
}

/* Texel coordinate (16.16) at an edge: (u*w) / w, by one reciprocal shared with v. */
static inline void edge_uv(const edge_t *e, int32_t umax, int32_t vmax, int32_t *u, int32_t *v) {
	int32_t w = e->w >> 8;
	uint32_t r = (uint32_t)sdiv(0x7FFFFFFF, w > 1 ? w : 2) << 1;        /* 2^32 / w */
	int32_t uu = e->uw > 0 ? (int32_t)(((uint64_t)(uint32_t)e->uw * r) >> 16) : 0;
	int32_t vv = e->vw > 0 ? (int32_t)(((uint64_t)(uint32_t)e->vw * r) >> 16) : 0;
	*u = uu > umax ? umax : uu;
	*v = vv > vmax ? vmax : vv;
}

/* Draw the part of one face that lies in rows [y0, y1). */
static HOT void draw_face(uint8_t *fb, const item_t *it, int y0, int y1) {
	const corner_t *c = it->c;
	if (it->tex == CAB_NOTEX) {             /* half the spans in a frame: the cheap shared fill */
		int32_t xy[8];
		for (int i = 0; i < it->nv; i++) { xy[2 * i] = c[i].x; xy[2 * i + 1] = c[i].y; }
		poly_flat(fb, xy, it->nv, y0, y1, (uint8_t)((cab_faces[it->face].base << 2) | it->shade));
		return;
	}
	int n = it->nv, top = 0, ymax = c[0].y;
	for (int i = 1; i < n; i++) {
		if (c[i].y < c[top].y) top = i;
		if (c[i].y > ymax) ymax = c[i].y;
	}
	int yy = (c[top].y + 7) >> 4, ybot = (ymax + 7) >> 4;
	if (yy < y0) yy = y0;
	if (ybot > y1) ybot = y1;
	if (yy >= ybot) return;

	int shade = it->shade;
	const uint8_t *tex = texels + cab_tex[it->tex].offset;
	int32_t umax = ((int32_t)cab_tex[it->tex].w << 16) - 1;
	int32_t vmax = ((int32_t)cab_tex[it->tex].h << 16) - 1;
	uint32_t shade2 = (uint32_t)shade * 0x0101u;

	edge_t ea, eb;                      /* the two chains; which is left is decided per line */
	int ia = top, ib = top, used = 0;
	ea.yend = eb.yend = yy;
	uint8_t *row = fb + yy * FB_W;
	for (; yy < ybot; yy++, row += FB_W) {
		while (yy >= ea.yend) {
			if (used++ >= n) return;
			int nx = ia ? ia - 1 : n - 1;
			edge_start(&ea, &c[ia], &c[nx], yy);
			ia = nx;
		}
		while (yy >= eb.yend) {
			if (used++ >= n) return;
			int nx = ib + 1 < n ? ib + 1 : 0;
			edge_start(&eb, &c[ib], &c[nx], yy);
			ib = nx;
		}
		const edge_t *l = &ea, *r = &eb;
		if (l->x > r->x) { l = &eb; r = &ea; }
		int xa = (l->x + 0x7FFF) >> 16;             /* first pixel centre at or right of l */
		int xb = ((r->x + 0x7FFF) >> 16) - 1;       /* last pixel centre left of r */
		int clip = 0;
		if (xa < 0) { clip = -xa; xa = 0; }
		if (xb > FB_W - 1) xb = FB_W - 1;
		if (xb >= xa) {
			uint8_t *p = row + xa;
			int count = xb - xa + 1;
			/* Framebuffer writes are the expensive part, so pixels go out as wide as the
			 * address allows: longs for a flat span, pairs for a textured one. */
			{
				int32_t ul, vl, ur, vr;
				edge_uv(l, umax, vmax, &ul, &vl);
				edge_uv(r, umax, vmax, &ur, &vr);
				int len = count + clip;
				int32_t rc = (int32_t)recip_tab[len > 320 ? 320 : len];
				/* v is carried << 7 so its high word is already row * 128 (plus fraction
				 * bits, masked off): the texel address costs no shifts. */
				vl <<= CAB_TEX_SHIFT; vr <<= CAB_TEX_SHIFT;
				int32_t du = mul16(ur - ul, rc);
				int32_t dv = mul16(vr - vl, rc);
				ul += du * clip; vl += dv * clip;
#define TEXEL() tex[(((uint32_t)vl >> 16) & ~127u) + ((uint32_t)ul >> 16)]
				if ((uintptr_t)p & 1) { *p++ = (uint8_t)(TEXEL() | shade); ul += du; vl += dv; count--; }
				for (int k = count >> 1; k > 0; k--) {
					uint32_t a = TEXEL(); ul += du; vl += dv;
					uint32_t b = TEXEL(); ul += du; vl += dv;
					*(uint16_t *)(void *)p = (uint16_t)((a << 8) | b | shade2);
					p += 2;
				}
				if (count & 1) *p = (uint8_t)(TEXEL() | shade);
#undef TEXEL
			}
		}
		ea.x += ea.dx; ea.w += ea.dw; ea.uw += ea.duw; ea.vw += ea.dvw;
		eb.x += eb.dx; eb.w += eb.dw; eb.uw += eb.duw; eb.vw += eb.dvw;
	}
}

/* THE BANNER: the region's marquee as a flat picture beside the cabinet. It is static, so it
 * is copied into a framebuffer page only when that page does not already show it; after
 * that, the only upkeep is putting back whatever part of it the cabinet's clear rectangle
 * wiped. It must lie above SPLIT_ROW: only the primary touches it. */
/* Fill pixels [x0, x1) of row y with a four-pixel pattern, keeping the pattern's phase. */
static HOT void fill_row(uint8_t *fb, int y, int x0, int x1, uint32_t pat) {
	if (x0 < 0) x0 = 0;
	if (x1 > FB_W) x1 = FB_W;
	uint8_t *p = fb + y * FB_W + x0;
	int n = x1 - x0;
	for (; n > 0 && (x0 & 3); n--, x0++) *p++ = (uint8_t)(pat >> (24 - 8 * (x0 & 3)));
	for (; n >= 4; n -= 4, p += 4) *(uint32_t *)(void *)p = pat;
	for (int k = 0; n > 0; n--, k++) *p++ = (uint8_t)(pat >> (24 - 8 * k));
}

/* Put the room's own background back on pixels [x0, x1) of row y. */
static void back_row(uint8_t *fb, int y, int x0, int x1) {
	if (y >= HORIZON_ROW) { fill_row(fb, y, x0, x1, row_fill[y]); return; }
	if (x0 < 0) x0 = 0;
	if (x1 > FB_W) x1 = FB_W;
	for (int x = x0; x < x1; x++) fb[y * FB_W + x] = backdrop[y * FB_W + x];
}

void cabinet_restore(uint8_t *fb, int x0, int y0, int x1, int y1) {
	if (y0 < 0) y0 = 0;
	if (y1 > FB_H) y1 = FB_H;
	for (int y = y0; y < y1; y++) back_row(fb, y, x0, x1);
}

typedef struct { int16_t region, x, y; } banner_t;       /* region < 0: none */
static banner_t banner_want = { -1, 0, 0 };
static banner_t banner_on[2] = { { -1, 0, 0 }, { -1, 0, 0 } };

void cabinet_banner(int region, int x, int y) {
	banner_want.region = (int16_t)region; banner_want.x = (int16_t)x; banner_want.y = (int16_t)y;
}

/* Copy the part of banner `b` inside the rectangle [x0,x1) x [y0,y1) to the page. */
static void banner_blit(uint8_t *fb, const banner_t *b, int x0, int y0, int x1, int y1) {
	if (x0 < b->x) x0 = b->x;
	if (y0 < b->y) y0 = b->y;
	if (x1 > b->x + CAB_BANNER_W) x1 = b->x + CAB_BANNER_W;
	if (y1 > b->y + CAB_BANNER_H) y1 = b->y + CAB_BANNER_H;
	if (x1 <= x0 || y1 <= y0) return;
	for (int y = y0; y < y1; y++) {
		const uint8_t *s = cab_banner[b->region] + (y - b->y) * CAB_BANNER_W + (x0 - b->x);
		uint8_t *d = fb + y * FB_W + x0;
		int n = x1 - x0;
		while (((uintptr_t)d & 1) && n) { *d++ = *s++; n--; }
		for (; n >= 2; n -= 2, d += 2, s += 2) *(uint16_t *)(void *)d = (uint16_t)((s[0] << 8) | s[1]);
		if (n) *d = *s;
	}
}

/* Bring one page's banner to the wanted state: erase a stale one, draw a missing one. */
static void banner_sync(uint8_t *fb, int page) {
	banner_t *on = &banner_on[page & 1];
	if (on->region == banner_want.region && on->x == banner_want.x && on->y == banner_want.y) return;
	if (on->region >= 0) {
		for (int y = on->y; y < on->y + CAB_BANNER_H; y++) back_row(fb, y, on->x, on->x + CAB_BANNER_W);
	}
	*on = banner_want;
	if (on->region >= 0) banner_blit(fb, on, 0, 0, FB_W, FB_H);
}

/* Clear the frame's rectangle within rows [y0, y1), then draw every face's part of them. */
static HOT void raster(const frame_t *fr, int y0, int y1, int timed) {
	uint16_t t0 = frt();
	int cy0 = fr->cy0 > y0 ? fr->cy0 : y0, cy1 = fr->cy1 < y1 ? fr->cy1 : y1;
	int x0 = fr->cx0 & ~3, words = ((fr->cx1 + 3) & ~3) - x0;
	if (words > 0) {
		words >>= 2;
		for (int y = cy0; y < cy1; y++) {
			uint32_t *p = (uint32_t *)(fr->fb + y * FB_W + x0);
			int n = words;
			if (y < HORIZON_ROW) {
				const uint32_t *s = (const uint32_t *)(const void *)(backdrop + y * FB_W + x0);
				do { *p++ = *s++; } while (--n);
			} else {
				uint32_t bg = row_fill[y];
				do { *p++ = bg; } while (--n);
			}
		}
	}
	if (timed && banner_want.region >= 0)           /* the primary: put back what the clear wiped */
		banner_blit(fr->fb, &banner_want, x0, cy0, x0 + (words << 2), cy1);
	/* The shadow: an ellipse of the darker floor pattern, under everything else. */
	int sy0 = fr->shy - SHADOW_RY, sy1 = fr->shy + SHADOW_RY + 1;
	if (sy0 < y0) sy0 = y0;
	if (sy0 < HORIZON_ROW) sy0 = HORIZON_ROW;
	if (sy1 > y1) sy1 = y1;
	for (int y = sy0; y < sy1; y++) {
		int d = y - fr->shy;
		int hw = (SHADOW_RX * ellipse_tab[((d < 0 ? -d : d) * 32) / SHADOW_RY]) >> 8;
		fill_row(fr->fb, y, fr->shx - hw, fr->shx + hw, row_shadow[y]);
	}
	uint16_t t1 = frt();
	for (int i = 0; i < fr->nitems; i++) {
		if (!timed) crowd_poll();                   /* the secondary: keep the crowd's loop turning */
		draw_face(fr->fb, &fr->item[i], y0, y1);
	}
	if (timed) {
		cabinet_ticks[1] = (uint16_t)(t1 - t0);
		cabinet_ticks[2] = (uint16_t)(frt() - t1);
	}
}

void cabinet_secondary(void) {
	const frame_t *src = UNCACHED(&frame);
	int n = src->nitems;
	frame_s.fb = src->fb; frame_s.nitems = n;
	frame_s.shx = src->shx; frame_s.shy = src->shy;
	frame_s.cx0 = src->cx0; frame_s.cy0 = src->cy0; frame_s.cx1 = src->cx1; frame_s.cy1 = src->cy1;
	const uint32_t *s = (const uint32_t *)src->item;
	uint32_t *d = (uint32_t *)frame_s.item;
	for (int k = n * (int)(sizeof(item_t) / 4); k > 0; k--) *d++ = *s++;
	raster(&frame_s, SPLIT_ROW, FB_H, 0);
}

int cabinet_draw(uint8_t *fb, int page, int yaw, int pitch, int dist, int xcentre, int region) {
	uint16_t t0 = frt();
	banner_sync(fb, page);
	/* Camera on a sphere about the cabinet centre, looking at it. All 2.14. */
	int32_t cy = icos(yaw), sy = isin(yaw), cp = icos(pitch), sp = isin(pitch);
	int32_t fx = -((cp * cy) >> 14), fy = -((cp * sy) >> 14), fz = -sp;   /* forward */
	int32_t rx = -sy, ry = cy;                                            /* right (rz = 0) */
	int32_t ux = (ry * fz) >> 14, uy = -((rx * fz) >> 14), uz = (rx * fy - ry * fx) >> 14;
	int32_t ex = -((dist * fx) >> 14), ey = -((dist * fy) >> 14), ez = -((dist * fz) >> 14);
	/* The light, taken into model space once so each face costs three multiplies. */
	int32_t lx = (rx * LIGHT_X + ux * LIGHT_Y + fx * LIGHT_Z) >> 14;
	int32_t ly = (ry * LIGHT_X + uy * LIGHT_Y + fy * LIGHT_Z) >> 14;
	int32_t lz = (uz * LIGHT_Y + fz * LIGHT_Z) >> 14;

	int32_t bx0 = FB_W * 16, by0 = FB_H * 16, bx1 = 0, by1 = 0;           /* this frame's extent, 28.4 */
	for (int i = 0; i < CAB_NVERTS; i++) {
		int32_t dx = cab_verts[i][0] - ex, dy = cab_verts[i][1] - ey, dz = cab_verts[i][2] - ez;
		int32_t zc = (dx * fx + dy * fy + dz * fz) >> 14;
		proj[i].z = zc;
		if (zc < NEAR_Z) continue;
		int32_t xc = (dx * rx + dy * ry) >> 14;
		int32_t yc = (dx * ux + dy * uy + dz * uz) >> 14;
		int32_t w = sdiv(1 << 28, zc);
		int32_t px = xcentre * 16 + mul32(FOCAL_PX * 16 * xc, w << 4);
		int32_t py = (FB_H / 2) * 16 - mul32(FOCAL_PX * 16 * yc, w << 4);
		proj[i].w = w; proj[i].x = px; proj[i].y = py;
		if (px < bx0) bx0 = px;
		if (px > bx1) bx1 = px;
		if (py < by0) by0 = py;
		if (py > by1) by1 = py;
	}

	/* The shadow sits on the floor under the cabinet's centre; it has to be cleared and
	 * redrawn with the cabinet, so it widens this frame's extent. */
	{
		int32_t dx = -ex, dy = -ey, dz = floor_z - ez;
		int32_t zc = (dx * fx + dy * fy + dz * fz) >> 14;
		int32_t w = sdiv(1 << 28, zc > NEAR_Z ? zc : NEAR_Z);
		int32_t sx = xcentre * 16 + mul32(FOCAL_PX * 16 * ((dx * rx + dy * ry) >> 14), w << 4);
		int32_t sy = (FB_H / 2) * 16 - mul32(FOCAL_PX * 16 * ((dx * ux + dy * uy + dz * uz) >> 14), w << 4);
		frame.shx = (int16_t)((sx >> 4) + SHADOW_DX);
		frame.shy = (int16_t)((sy >> 4) + SHADOW_DY);
		if ((frame.shx - SHADOW_RX) * 16 < bx0) bx0 = (frame.shx - SHADOW_RX) * 16;
		if ((frame.shx + SHADOW_RX) * 16 > bx1) bx1 = (frame.shx + SHADOW_RX) * 16;
		if ((frame.shy + SHADOW_RY) * 16 > by1) by1 = (frame.shy + SHADOW_RY) * 16;
	}

	/* Which side panel is nearer decides the group order. */
	static const uint8_t rank_r_near[7] = { 2, 3, 4, 4, 0, 1, 1 };   /* body Redge Rin Rout Ledge Lin Lout */
	static const uint8_t rank_l_near[7] = { 2, 0, 1, 1, 3, 4, 4 };
	const uint8_t *rank = ey > 0 ? rank_r_near : rank_l_near;
	int nd = 0;
	for (int i = 0; i < CAB_NFACES; i++) {
		const cab_face_t *f = &cab_faces[i];
		const int16_t *v0 = cab_verts[f->vi[0]];
		if (f->n[0] * (ex - v0[0]) + f->n[1] * (ey - v0[1]) + f->n[2] * (ez - v0[2]) <= 0) continue;
		int32_t zsum = 0, near = 0;
		for (int k = 0; k < f->nv; k++) {
			int32_t z = proj[f->vi[k]].z;
			if (z < NEAR_Z) near = 1;
			zsum += z;
		}
		if (near) continue;
		int32_t lit = AMBIENT + (((f->n[0] * lx + f->n[1] * ly + f->n[2] * lz) >> 14) * DIFFUSE >> 14);
		if (lit < 0) lit = 0;
		if (lit > 16383) lit = 16383;
		shade_of[i] = (uint8_t)((lit * CAB_NSHADE) >> 14);
		int32_t zmean4 = f->nv == 4 ? zsum : zsum + (zsum >> 2) + (zsum >> 4);   /* ~4 x mean depth */
		key[i] = ((uint32_t)rank[f->group] << 24) | (uint32_t)(0xFFFFFF - (zmean4 & 0xFFFFFF));
		int j = nd++;                                                       /* insertion sort, far first */
		while (j > 0 && key[order[j - 1]] > key[i]) { order[j] = order[j - 1]; j--; }
		order[j] = (uint8_t)i;
	}

	for (int d = 0; d < nd; d++) {
		const cab_face_t *f = &cab_faces[order[d]];
		item_t *it = &frame.item[d];
		it->face = order[d]; it->shade = shade_of[order[d]];
		it->tex = f->tex == CAB_NOTEX ? CAB_NOTEX : cab_region_tex[region][f->tex];
		it->nv = (uint8_t)(f->nv < 3 ? 3 : f->nv);
		for (int k = 0; k < it->nv; k++) {
			const proj_t *p = &proj[f->vi[k]];
			it->c[k].x = p->x; it->c[k].y = p->y; it->c[k].w = p->w;
			it->c[k].uw = f->u[k] * p->w;
			it->c[k].vw = f->v[k] * p->w;
		}
	}

	/* Clear what this page last showed plus what it is about to show, as one rectangle. */
	int16_t now[4] = { (int16_t)(bx0 >> 4), (int16_t)(by0 >> 4), (int16_t)((bx1 >> 4) + 2), (int16_t)((by1 >> 4) + 2) };
	if (now[0] < 0) now[0] = 0;
	if (now[1] < 0) now[1] = 0;
	if (now[2] > FB_W) now[2] = FB_W;
	if (now[3] > FB_H) now[3] = FB_H;
	int16_t *was = drawn[page & 1];
	frame.cx0 = was[0] < now[0] ? was[0] : now[0];
	frame.cy0 = was[1] < now[1] ? was[1] : now[1];
	frame.cx1 = was[2] > now[2] ? was[2] : now[2];
	frame.cy1 = was[3] > now[3] ? was[3] : now[3];
	for (int k = 0; k < 4; k++) was[k] = now[k];
	frame.fb = fb;
	frame.nitems = nd;
	cabinet_ticks[0] = (uint16_t)(frt() - t0);

#ifdef ONECPU
	raster(&frame, 0, FB_H, 1);
	cabinet_ticks[3] = 0;
#else
	uint16_t ts = frt();
	FX_JOB = CMD_CABINET;
	raster(&frame, 0, SPLIT_ROW, 1);
	while (FX_JOB != 0) ;
	cabinet_ticks[3] = (uint16_t)(frt() - ts);
#endif
	return nd;
}
