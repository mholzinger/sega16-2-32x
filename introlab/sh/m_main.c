/* introlab primary SH-2. The intro doors (doors.c), then THE REGION SELECT: one cabinet, parked on the left of the
 * screen for the US set and on the right for the JP set. Left and right on the pad choose;
 * the cabinet crosses the screen turning the long way round, and changes its marquee and
 * screen art at the moment its back is to the camera. The free half of the screen shows
 * the region's marquee (on the 32X layer, so the cabinet passes in front of it) and a
 * two-item menu on the MD text plane: up/down move, a button confirms. Neither item is
 * wired to anything yet.
 *
 * Motion is paced by vblanks elapsed, not frames drawn, so a move takes the same time at
 * any frame rate. */
#include <stdint.h>
#include "mars.h"
#include "video.h"
#include "cabinet.h"
#include "doors.h"
#include "gen_tables.h"

/* Read back by tools/shot.py (ares --dump sdram at the address of _introlab_diag in the
 * .lst). [0] magic 'INTR'  [1] frames flipped  [2] vblanks elapsed  [3] worst vblanks
 * taken by one frame  [4] faces drawn in the last frame  [5..8] FRT ticks summed over
 * every frame: geometry, primary clear, primary raster, secondary (wake to done)
 * [9] selected region  [10] last menu item confirmed, 1-based (0 = none)
 * [11] doors-scene frames flipped  [12] doors-scene vblanks while the doors were moving. */
volatile uint32_t introlab_diag[13] = { 0x494E5452u, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

#define REGION_US    0
#define REGION_JP    1
#define PARK_X_US    80         /* cabinet centre column when parked. The cabinet and its panel
                                * sit 24 pixels nearer the middle than they first did (Mike,
                                * 2026-11-14): together they are centred, the side still says
                                * which region. 24 = three text cells, so the words move with them. */
#define PARK_X_JP    240
#define PARK_YAW     84         /* 30 degrees: the front is turned toward the middle of the screen */
#define SWAY         14         /* +/- 5 degrees of idle sway */
#define PITCH        9          /* 3 degrees up */
#define DIST         9613       /* 150 inches, in 1/64 inch */
#define MOVE_VBL     80         /* vblanks a crossing takes */
#define SWAY_STEP    437        /* 8.8 angle steps per vblank: one sway cycle in 10 seconds */
#define HUD_FRAMES   32

#define PAD_UP       0x0001
#define PAD_DOWN     0x0002
#define PAD_LEFT     0x0004
#define PAD_RIGHT    0x0008
#define PAD_GO       0x00F0     /* B, C, A or Start confirms */

/* The half of the screen the cabinet is not in: the marquee on top, the menu under it. */
#define BANNER_Y     24
#define BANNER_X_US  140        /* cabinet left, panel right */
#define BANNER_X_JP  28
#define MENU_ROW     10         /* MD text rows are 8 pixels */
#define MENU_COL_US  21
#define MENU_COL_JP  7
#define LABEL_ROW    24
#define REGION_ROW   1          /* the gap above the marquee */
#define HUD_ROW      26
#define GREY         UI_GREY
#define GREEN        UI_GREEN
#define RED          UI_RED

#define NITEMS       2
static const char *const item_name[NITEMS] = { "LAUNCH", "DIP SWITCHES" };
#define ITEM_WIDTH   14         /* cursor + the longest name */

static void put_num(int x, int y, unsigned v, int digits, int colour) {
	char s[8];
	s[digits] = 0;
	for (int i = digits - 1; i >= 0; i--) { s[i] = (char)('0' + v % 10); v /= 10; }
	ui_puts(s, colour, x, y);
}

/* Which region is chosen. The port says it beside the cabinet (show_menu: the words over
 * the marquee, the flag under the menu); the lab keeps its two words along the bottom. */
static void show_choice(int region) {
#ifdef PORT
	(void)region;
#else
	ui_puts("US", region == REGION_US ? GREEN : GREY, 12, LABEL_ROW);
	ui_puts("JP", region == REGION_JP ? GREEN : GREY, 26, LABEL_ROW);
#endif
}

/* The menu under the banner. `item` < 0 erases it (the cabinet is crossing). */
static void show_menu(int region, int item, int chosen_colour) {
	int col = region == REGION_US ? MENU_COL_US : MENU_COL_JP;
	/* the region in words, centred over the marquee */
	ui_puts(item < 0 ? "          " : region == REGION_US ? "REGION  US" : "REGION  JP", GREEN, col + 1, REGION_ROW);
	ui_flags(item < 0 ? -1 : region);
	for (int k = 0; k < NITEMS; k++) {
		int row = MENU_ROW + 2 * k;
		if (item < 0) { ui_puts("              ", GREY, col, row); continue; }
		ui_puts(k == item ? ">" : " ", GREEN, col, row);
		ui_puts(item_name[k], k == item ? chosen_colour : GREY, col + 2, row);
	}
}

/* THE DOORS. The closed doors and their logo ripple up out of black and settle; a highlight
 * runs down the logo; then the doors swing inward, fast, and out of view, leaving the
 * screen dark for the room's lights. Start skips it. Timed in vblanks. */
#define DOORS_RISE   70         /* fade up while the ripple dies away */
#define DOORS_HOLD   70         /* still, with the highlight passing */
#define DOORS_OPEN   64         /* the swing */
#define WAVE_X       14         /* the ripple's first amplitude, pixels and rows */
#define WAVE_Y       5
#define PAD_START    0x0080

static void doors_scene(void) {
	unsigned t = 0;
	uint16_t pad_was = 0xFFFF;
	int straight = 0;                       /* consecutive frames drawn with a straight line table */
	while (t < DOORS_RISE + DOORS_HOLD + DOORS_OPEN) {
		int level = t < DOORS_RISE / 2 ? (int)(t * 256 / (DOORS_RISE / 2)) : 256;
		int ripple = t < DOORS_RISE ? (int)(DOORS_RISE - t) : 0;           /* DOORS_RISE..0 */
		int shimmer = -1, angle = 0;
		if (t >= DOORS_RISE && t < DOORS_RISE + DOORS_HOLD) {              /* top to bottom, twice */
			int k = (int)((t - DOORS_RISE) * 2 * 256 / DOORS_HOLD) & 255;
			shimmer = (k * (DOORS_SHIMMER_SPAN + 32) >> 8) - 16;
		} else if (t >= DOORS_RISE + DOORS_HOLD) {
			int k = (int)((t - DOORS_RISE - DOORS_HOLD) * 256 / DOORS_OPEN);
			angle = (k * k) >> 8;                                          /* gathers speed: starts slow, leaves fast */
		}
		doors_draw(video_back(), angle);
		video_wave(WAVE_X * ripple * ripple / (DOORS_RISE * DOORS_RISE), WAVE_Y * ripple * ripple / (DOORS_RISE * DOORS_RISE), (int)t * 28);
		straight = ripple ? 0 : straight + 1;
		unsigned v = video_flip();
		t += v;
		if (angle) { introlab_diag[11]++; introlab_diag[12] += v; }        /* the swing: the costly part */
		doors_light(level, angle, shimmer);                                /* in vblank, for the frame now showing */
		uint16_t pad = video_pad();
		if (pad & ~pad_was & PAD_START) break;
		pad_was = pad;
	}
	for (; straight < 2; straight++) {      /* skipped mid-ripple: leave both pages straight and dark */
		doors_light(0, 0, -1);
		doors_draw(video_back(), 256);
		video_wave(0, 0, 0);
		video_flip();
	}
}

static int select_scene(void);

#ifdef PORT
/* What the switch sheet last answered (video_sheet), 0 while this cabinet's switches are
 * its board's own. A change of region drops it: the other board has other switches. */
static unsigned sheet;

/* Inside the port: the whole launcher, from the doors to LAUNCH. Returns the region chosen
 * in bits 0-1 and, when the sheet was used for it, video_sheet's answer in the rest. */
unsigned intro_run(void) {
	video_init();
	cabinet_init();
	ui_clear();
	doors_scene();
	return (unsigned)select_scene() | sheet;
}
#else
int m_main(void) {
	video_init();
	cabinet_init();
	MARS_SYS_COMM4 = 0;                     /* the boot handshake leaves a marker here */
	ui_clear();
	doors_scene();
	select_scene();                         /* the lab's menu leads nowhere: never returns */
	return 0;
}
#endif

/* THE ROOM. Its lights come up from dark; the text and menu appear once they are full. */
#define LIGHT_STEP   6          /* of 256 per vblank: about 0.7 seconds */

static int select_scene(void) {
	int light = 0, lit = 0;
	cabinet_light(0);
	smoke_light(0);
	smoke_start();

	int region = REGION_US;                 /* where the cabinet is parked, or heading */
	int shown = REGION_US;                  /* the art it is wearing right now */
	int move = 0;                           /* vblanks left of a crossing, 0 = parked */
	unsigned sway = 0, hud_frames = 0, hud_vbl = 0, hud_t[4] = { 0, 0, 0, 0 };
	uint16_t pad_was = 0xFFFF;              /* ignore whatever is held at power-on */
	int item = 0;                           /* menu cursor */
	cabinet_banner(region, BANNER_X_US, BANNER_Y);

	for (;;) {
		/* Parked pose: yaw is negative on the left (front toward the right) and positive
		 * on the right. A crossing eases x across and takes yaw the long way, through the
		 * back view, where the art changes hands. e runs 0..256 with a smooth start and stop. */
		int x, yaw;
		int idle = (SWAY * sin_tab[(sway >> 8) & (ANGLE_STEPS - 1)]) >> 14;
		if (move == 0) {
			x   = region == REGION_US ? PARK_X_US : PARK_X_JP;
			yaw = (region == REGION_US ? -PARK_YAW : PARK_YAW) + idle;
		} else {
			int t = ((MOVE_VBL - move) << 8) / MOVE_VBL;                /* 0..256 */
			int e = (t * t * (3 * 256 - 2 * t)) >> 16;                  /* smoothstep */
			int span = ANGLE_STEPS - 2 * PARK_YAW;                      /* the long way round */
			if (region == REGION_JP) {                                  /* left to right */
				x   = PARK_X_US + (((PARK_X_JP - PARK_X_US) * e) >> 8);
				yaw = -PARK_YAW - ((span * e) >> 8);
			} else {
				x   = PARK_X_JP - (((PARK_X_JP - PARK_X_US) * e) >> 8);
				yaw = PARK_YAW + ((span * e) >> 8);
			}
			if (e >= 128) shown = region;                               /* back to camera: swap the art */
		}
		ui_erase(video_back(), video_page());
		int faces = cabinet_draw(video_back(), video_page(), yaw, PITCH, DIST, x, shown);
		ui_draw(video_back(), video_page());
		unsigned v = video_flip();
		if (light < 256) {                              /* the lights are still coming up */
			light += (int)v * LIGHT_STEP;
			cabinet_light(light);
			smoke_light(light > 256 ? 256 : light);
		} else if (!lit) {                              /* full: now the words */
			lit = 1;
			ambience_start();                               /* the room is lit: the other cabinet's music */
			ui_colours();
			show_choice(region);
			show_menu(region, item, GREEN);
#ifndef NOHUD
			ui_puts("FPS   .", GREY, 1, HUD_ROW);
			ui_puts("G      C      R      S", GREEN, 1, HUD_ROW + 1);
#endif
		}
		sway += v * SWAY_STEP;
		if (move) {
			move = move > (int)v ? move - (int)v : 0;
			if (move == 0) {                    /* arrived: the panel appears on the free side */
				cabinet_banner(region, region == REGION_US ? BANNER_X_US : BANNER_X_JP, BANNER_Y);
				show_menu(region, item, GREEN);
			}
		}

		uint16_t pad = video_pad();
		uint16_t pressed = (uint16_t)(pad & ~pad_was);
		pad_was = pad;
#if defined(PORT) && defined(AUTOLAUNCH)
		/* RIG PROBE (make port AUTOLAUNCH=<vblanks> [AUTOJP=1]): nobody holds the rig's pad, so
		 * the launcher presses for itself: right once the room is lit (AUTOJP), then LAUNCH
		 * AUTOLAUNCH vblanks after the cabinet is parked. Never in a candidate. */
		{
			static unsigned auto_t, auto_went;
			if (lit && move == 0) auto_t += v;
#ifdef AUTOJP
			if (lit && !auto_went && auto_t > 120) { auto_went = 1; auto_t = 0; pressed |= PAD_RIGHT; }
#else
			(void)auto_went;
#endif
			if (auto_t > AUTOLAUNCH && move == 0 && region == (
#ifdef AUTOJP
				REGION_JP
#else
				REGION_US
#endif
				)) { item = 0; pressed |= 0x0040; }
		}
#endif
		if (move == 0 && lit) {
			int want = (pressed & PAD_RIGHT) ? REGION_JP : (pressed & PAD_LEFT) ? REGION_US : region;
			if (want != region) {               /* leave: the panel goes, the cabinet crosses */
				show_menu(region, -1, GREY);
				cabinet_banner(-1, 0, 0);
				region = want; move = MOVE_VBL; show_choice(region);
#ifdef PORT
				sheet = 0;
#endif
			} else if (pressed & (PAD_UP | PAD_DOWN)) {
				item = (item + ((pressed & PAD_DOWN) ? 1 : NITEMS - 1)) % NITEMS;
				show_menu(region, item, GREEN);
			} else if (pressed & PAD_GO) {
				introlab_diag[10] = (uint32_t)(item + 1);
#ifdef PORT
				if (item == 0) {                /* LAUNCH, once the button is up: the game must not see it held */
					while (video_pad() & PAD_GO) video_flip();
					ambience_stop();
					smoke_stop();               /* the MD picture goes back as the game expects it */
					return region;
				}
				ambience_stop();
				smoke_stop();                   /* the sheet takes the MD picture */
				sheet = video_sheet(region);    /* DIP SWITCHES: the game's sheet, then back here */
				cabinet_light(256);
				smoke_start(); smoke_light(256);
				ambience_start();
				pad_was = 0xFFFF;
				continue;
#else
				show_menu(region, item, RED);   /* the lab's menu leads nowhere: acknowledge in red */
#endif
			}
		}

		introlab_diag[1]++;
		introlab_diag[2] += v;
		if (v > introlab_diag[3] && introlab_diag[1] > 4) introlab_diag[3] = v;
		introlab_diag[4] = (uint32_t)faces;
		introlab_diag[9] = (uint32_t)region;
		for (int k = 0; k < 4; k++) { introlab_diag[5 + k] += cabinet_ticks[k]; hud_t[k] += cabinet_ticks[k]; }
#ifndef NOHUD
		hud_vbl += v;
		if (hud_frames + 1 >= HUD_FRAMES && !lit) { hud_frames = hud_vbl = 0; for (int k = 0; k < 4; k++) hud_t[k] = 0; }
		if (++hud_frames == HUD_FRAMES) {       /* frames per second, and mean ticks per phase */
			unsigned fps10 = hud_frames * 600u / hud_vbl;
			put_num(5, HUD_ROW, fps10 / 10, 2, GREY);
			put_num(8, HUD_ROW, fps10 % 10, 1, GREY);
			for (int k = 0; k < 4; k++) { put_num(3 + k * 7, HUD_ROW + 1, hud_t[k] / HUD_FRAMES, 4, GREEN); hud_t[k] = 0; }
			hud_frames = hud_vbl = 0;
		}
#else
		(void)hud_frames; (void)hud_vbl; (void)hud_t; (void)put_num;
#endif
	}
	return region;                          /* not reached: the loop only leaves through LAUNCH */
}
