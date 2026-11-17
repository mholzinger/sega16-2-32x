/* The launcher's display, clock and pad inside the port (the lab's are video.c + hw.c).
 *
 * CLOCK: the port's primary counts its own vblank interrupts; the loader hands that
 * counter's address over (video_port_clock).
 * PAD: the game's 68K posts the pads once per vblank (COMM2 = P1 << 8 | P2 as the arcade's
 * active-low bytes, COMM8 = 0xA5 << 8 | service bits; md_src/r60_shim.h), but only on a
 * vblank that finds FM down, and it then answers on COMM0. So each flip lowers FM, waits
 * for that answer (or two vblanks), raises FM again and swaps the pages, which the game's
 * own loop does once per frame too. Nothing here asks the 68K for anything else. */
#include "fx.h"
#include "video.h"

#define FM_REG (*(volatile uint8_t *)0x20004000)        /* bit 7: the SH-2s own the 32X VDP */
#define COMM2  (*(volatile uint16_t *)0x20004022)
#define COMM8  (*(volatile uint16_t *)0x20004028)

static volatile uint32_t *vints;
static uint16_t current_fb;
static uint32_t last_tick;

void video_port_clock(volatile uint32_t *counter) { vints = counter; }

static void swap_wait(void) {
	MARS_VDP_FBCTL = current_fb ^ 1;
	while ((MARS_VDP_FBCTL & MARS_VDP_FS) == current_fb) ;
	current_fb ^= 1;
}

static void page_setup(void) {
	volatile uint16_t *fb = &MARS_FRAMEBUFFER;
	for (int i = 0; i < 256; i++)
		fb[i] = (uint16_t)((i < FB_H ? i : FB_H - 1) * (FB_W / 2) + 0x100);
	for (int i = 0x100; i < 0x100 + FB_W * FB_H / 2; i++) fb[i] = 0;
}

void video_init(void) {
	FM_REG = 0x80;                      /* the 68K lowers FM at the end of its boot: a page swap at FM = 0 is dropped */
	current_fb = MARS_VDP_FBCTL & MARS_VDP_FS;
	swap_wait(); page_setup();
	swap_wait(); page_setup();
	last_tick = *vints;
}

void video_wave(int amp_x, int amp_y, int phase) {
	volatile uint16_t *fb = &MARS_FRAMEBUFFER;
	for (int y = 0; y < FB_H; y++) {
		int a = phase + y * 6;
		int dx = (amp_x * isin(a)) >> 14;
		int row = y + ((amp_y * isin(a + ANGLE_STEPS / 4)) >> 14);
		if (row < 0) row = 0;
		if (row > FB_H - 1) row = FB_H - 1;
		fb[y] = (uint16_t)(row * (FB_W / 2) + 0x100 + (dx >> 1));
	}
}

uint8_t *video_back(void) { return (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200); }
int video_page(void) { return current_fb ^ 1; }
uint16_t video_vblanks(void) { return (uint16_t)*vints; }

/* S A C B R L D U in bits 7..0, as the lab's 68K published it. */
uint16_t video_pad(void) {
	uint16_t c8 = COMM8;
	if ((c8 & 0xFF00) != 0xA500) return 0;
	uint16_t a = (uint16_t)(~(COMM2 >> 8) & 0xFF), p = 0;
	if (a & 0x20) p |= 0x01;            /* up */
	if (a & 0x10) p |= 0x02;            /* down */
	if (a & 0x80) p |= 0x04;            /* left */
	if (a & 0x40) p |= 0x08;            /* right */
	if (a & 0x04) p |= 0x10;            /* B */
	if (a & 0x01) p |= 0x20;            /* C */
	if (a & 0x02) p |= 0x40;            /* A */
	if (c8 & 0x10) p |= 0x80;           /* Start */
	return p;
}

unsigned video_flip(void) {
	uint16_t seen = MARS_SYS_COMM0;
	uint32_t t0 = *vints;
	FM_REG = 0x00;
	while (MARS_SYS_COMM0 == seen && *vints - t0 < 2u) ;
	FM_REG = 0x80;
	swap_wait();
	uint32_t now = *vints;
	unsigned elapsed = (unsigned)(now - last_tick);
	last_tick = now;
	return elapsed ? elapsed : 1;
}

/* THE SWITCH SHEET is the game's own options screen, run by its 68K on the MD layer
 * (md_src/testcart.c frontend_menu_screen; the mailbox is md_src/frontend.h). FM goes down
 * for it: the 68K turns the 32X layer off while its screen is up and puts the mode back
 * when it closes. It answers with three words, each after the one before is acknowledged,
 * and waits for the mailbox to read 0 before it lets its interrupts run again. */
#define COMM12 (*(volatile uint16_t *)0x2000402C)
unsigned video_sheet(int region) {
	unsigned packed, dsw1, dsw2;
	FM_REG = 0x00;
	COMM12 = (uint16_t)(0xFE10 | (region & 3));         /* FE_SHEET | region */
	while ((COMM12 & 0xFF00) != 0xFD00) ;               /* FE_RESULT | round | region << 3 */
	packed = COMM12 & 0xFF;
	COMM12 = 0xFE03;                                    /* FE_MORE1 */
	while ((COMM12 & 0xFF00) != 0xFB00) ;               /* FE_DSW1 */
	dsw1 = COMM12 & 0xFF;
	COMM12 = 0xFE04;                                    /* FE_MORE2 */
	while ((COMM12 & 0xFF00) != 0xFA00) ;               /* FE_DSW2 */
	dsw2 = COMM12 & 0xFF;
	COMM12 = 0;
	FM_REG = 0x80;
	last_tick = *vints;                                 /* the time on the sheet is not motion owed */
	return 0x80u | ((packed & 7u) << 8) | (dsw1 << 16) | (dsw2 << 24);
}

void video_colour(int index, int r, int g, int b) {
	(&MARS_CRAM)[index & 255] = (uint16_t)COLOR(r, g, b);
}

/* THE SMOKE in the port: the game's 68K owns the MD picture, so this side only asks, on the
 * frontend mailbox (FE_SMOKE | n). The 68K runs the launcher's own program for it
 * (md/smoke68k.c, in this page; md_src/launcher_smoke.c is the door). */
static int smoke_shown = -1;
static void smoke_ask(unsigned what) {
	volatile uint16_t *c = (volatile uint16_t *)0x2000402C;
	*c = (uint16_t)(0xFE20 | what);
	while (*c) ;
}
/* THE CARPET (carpet.c) goes to MD plane B under the smoke: 40 x 9 cells from row 19, a
 * tile of its own for each, drawn here a tile at a time and handed over seventeen words at
 * a go (a VRAM address and sixteen words: md/smoke68k.c, 15). Each pixel is 4 + 2 x colour,
 * + 1 where the floor is near and lit; the rows above the floor's first stay clear. */
#include "carpet.h"
static void carpet_words(unsigned addr, const uint16_t *w) {
	volatile uint16_t *c = (volatile uint16_t *)0x2000402C;
	for (int i = 0; i < 17; i++) {
		COMM2 = (uint16_t)(i ? w[i - 1] : addr);
		*c = 0xFE2F;
		while (*c == 0xFE2F) ;
	}
	while (*c) ;
}
static void carpet_start(void) {
	uint16_t w[16];
	for (int r = 0; r < 9; r++) {
		for (int col = 0; col < 40; col++) {
			for (int j = 0; j < 8; j++) {
				int y = (19 + r) * 8 + j;
				uint32_t bits = 0;
				if (y >= CARPET_TOP) {
					carpet_row(y);
					for (int i = 0; i < 8; i++) {
						int x = col * 8 + i;
						bits = bits << 4 | (uint32_t)(4 + 2 * carpet_pixel(x) + ((y + (((x ^ y) & 1) ? 10 : 0)) >= 196));
					}
				}
				w[2 * j] = (uint16_t)(bits >> 16); w[2 * j + 1] = (uint16_t)bits;
			}
			carpet_words(0x1000u + (unsigned)(r * 40 + col) * 32u, w);
		}
		for (int k = 0; k < 3; k++) {                       /* the row's cells: palette line 3 */
			for (int i = 0; i < 16; i++) w[i] = (uint16_t)(k * 16 + i < 40 ? 0x6000 | (0x80 + r * 40 + k * 16 + i) : 0);
			carpet_words(0xE000u + (unsigned)(19 + r) * 128u + (unsigned)k * 32u, w);
		}
	}
}
void smoke_start(void) { smoke_ask(1); smoke_shown = -1; carpet_start(); }
void smoke_stop(void) { smoke_ask(0); }
/* The room's background music (tools/gen_ambience.py), through the same door. */
/* The music first, then the room's sounds over it (tools/gen_room.py): the FM chip only
 * runs while the music does. Stopping goes the other way round. */
static void second(unsigned job) { FX_JOB = (uint16_t)job; while (FX_JOB != 0) ; }
void ambience_start(void) { smoke_ask(11); smoke_ask(13); second(CMD_CROWD_ON); }     /* and the crowd, on the PWM (crowd.c) */
void ambience_stop(void) { second(CMD_CROWD_OFF); smoke_ask(14); smoke_ask(12); }
void smoke_light(int level) {
	int step = level <= 0 ? 0 : level >= 256 ? 8 : level >> 5;
	if (step != smoke_shown) { smoke_shown = step; smoke_ask(2u + (unsigned)step); }
}
