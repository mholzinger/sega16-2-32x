#ifndef INTROLAB_VIDEO_H
#define INTROLAB_VIDEO_H
#include <stdint.h>

#define FB_W 320
#define FB_H 224

/* 8-bit packed-pixel mode, 224 lines, line tables written on both pages, both cleared. */
void video_init(void);
/* The page being drawn (not displayed): 320x224 bytes, row-major. */
uint8_t *video_back(void);
/* Which page video_back() is: 0 or 1. */
int video_page(void);
/* Wait for the next vblank the 68K reports, swap pages, return the vblanks that elapsed
 * since the previous flip (1 = 60 Hz, 2 = 30 Hz). */
unsigned video_flip(void);
/* Bend the back page by rewriting its line table: each line is displaced up to amp_x pixels
 * sideways and amp_y rows vertically along a sine that `phase` moves (1024 per cycle).
 * Call before the flip, every frame the effect is on; (0, 0, 0) straightens the page. */
void video_wave(int amp_x, int amp_y, int phase);
/* 32X CRAM entry, 5 bits per channel. */
void video_colour(int index, int r, int g, int b);
/* Vblank count published by the 68K on COMM12. */
uint16_t video_vblanks(void);
/* Pad 1 as the 68K last published it: S A C B R L D U in bits 7..0. */
uint16_t video_pad(void);

/* THE WORDS ON SCREEN: a 40 x 28 grid of 8-pixel cells. The lab puts them on the MD text
 * plane through its 68K (video.c); inside the port they are drawn into the 32X page
 * (text.c), in front of the cabinet as the MD plane was. */
#define UI_GREY  0
#define UI_GREEN 1
#define UI_RED   2
void ui_clear(void);
void ui_puts(const char *s, int colour, int col, int row);
/* Once the room is fully lit: pick the three text colours from the palette on screen. */
void ui_colours(void);
/* Around each frame's drawing: ui_erase before it (cells whose text went or changed),
 * ui_draw after it (every cell that has text). No-ops in the lab. */
void ui_erase(uint8_t *fb, int page);
#ifdef PORT
/* Open the game's own switch sheet for `region` and wait for it to close (port_video.c).
 * Returns 0x80 | starting round << 8 | switch bank 1 << 16 | switch bank 2 << 24. */
unsigned video_sheet(int region);
#endif
void ui_draw(uint8_t *fb, int page);
/* The chosen region's flag on the floor under the menu; -1 takes it away (text.c). The lab has none. */
void ui_flags(int region);
/* THE SMOKE LAYER: our own wisps (tools/gen_wisps.py) on MD plane A, between the wall and
 * the cabinet. The lab drives its own 68K (hw.c, SMOKE=1); the port asks the game's
 * (port_video.c). smoke_light takes 0..256.
 *
 * Which side of the MD picture a 32X colour is on is bit 15 of its CRAM word, and what the
 * bit means follows the display mode's PRI bit. The lab leaves PRI clear (MD in front), so
 * the bit lifts a colour in front: the cabinet's carry it. The port runs with PRI set (32X
 * in front, sh_src/r60/r60_main.c), so the bit drops a colour behind: the room's carry it. */
#if defined(PORT)
void smoke_start(void);
void smoke_stop(void);
void smoke_light(int level);
void ambience_start(void);                   /* the room's background music: the port only */
void ambience_stop(void);
#define CRAM_FRONT 0u
#define CRAM_BACK  0x8000u
#elif defined(SMOKE)
#define ambience_start() ((void)0)
#define ambience_stop() ((void)0)
void smoke_start(void);
void smoke_stop(void);
void smoke_light(int level);
#define CRAM_FRONT 0x8000u
#define CRAM_BACK  0u
#else
#define ambience_start() ((void)0)
#define ambience_stop() ((void)0)
#define smoke_start() ((void)0)
#define smoke_stop() ((void)0)
#define smoke_light(level) ((void)(level))
#define CRAM_FRONT 0u
#define CRAM_BACK  0u
#endif
#endif
