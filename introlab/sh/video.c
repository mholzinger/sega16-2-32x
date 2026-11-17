/* introlab 32X display: init, page flip paced by the 68K's vblank count, palette. */
#include "fx.h"
#include "video.h"

#define COMM12_W (*(volatile uint16_t *)0x2000402C)   /* the 68K writes a 16-bit count */
#define COMM8_W  (*(volatile uint16_t *)0x20004028)

static uint16_t current_fb;
static uint16_t last_tick;

static void swap_wait(void) {
	MARS_VDP_FBCTL = current_fb ^ 1;
	while ((MARS_VDP_FBCTL & MARS_VDP_FS) == current_fb) ;
	current_fb ^= 1;
}

static void page_setup(void) {
	volatile uint16_t *fb = &MARS_FRAMEBUFFER;
	for (int i = 0; i < 256; i++)                       /* line table: word offset of each row */
		fb[i] = (uint16_t)((i < FB_H ? i : FB_H - 1) * (FB_W / 2) + 0x100);
	for (int i = 0x100; i < 0x100 + FB_W * FB_H / 2; i++) fb[i] = 0;
}

void video_init(void) {
	while ((MARS_SYS_INTMSK & MARS_SH2_ACCESS_VDP) == 0) ;   /* the 68K hands the VDP over (FM=1) */
	MARS_VDP_DISPMODE = MARS_224_LINES | MARS_VDP_MODE_256;
	current_fb = MARS_VDP_FBCTL & MARS_VDP_FS;
	swap_wait(); page_setup();
	swap_wait(); page_setup();
	last_tick = COMM12_W;
}

/* THE LINE TABLE AS AN EFFECT. Each display line reads its pixels from wherever its table
 * entry points, so pointing line y at row y + dy, a few pixels along, bends the picture
 * without redrawing it. The table lives in the page, so this is written to the back page
 * before each flip; amplitudes of 0 restore the straight table. Horizontal steps are 2
 * pixels (the entry is a word address) and a shifted line borrows its last pixels from the
 * neighbouring row, which is invisible where the picture's edges are one colour. */
void video_wave(int amp_x, int amp_y, int phase) {
	volatile uint16_t *fb = &MARS_FRAMEBUFFER;
	for (int y = 0; y < FB_H; y++) {
		int a = phase + y * 6;                              /* about 1.3 waves down the screen */
		int dx = (amp_x * isin(a)) >> 14;
		int row = y + ((amp_y * isin(a + ANGLE_STEPS / 4)) >> 14);
		if (row < 0) row = 0;
		if (row > FB_H - 1) row = FB_H - 1;
		fb[y] = (uint16_t)(row * (FB_W / 2) + 0x100 + (dx >> 1));
	}
}

uint8_t *video_back(void) { return (uint8_t *)((uintptr_t)&MARS_FRAMEBUFFER + 0x200); }
int video_page(void) { return current_fb ^ 1; }
uint16_t video_vblanks(void) { return COMM12_W; }
uint16_t video_pad(void) { return COMM8_W; }

unsigned video_flip(void) {
	uint16_t now;
	while ((now = COMM12_W) == last_tick) ;             /* at least one vblank per flip */
	swap_wait();
	unsigned elapsed = (uint16_t)(now - last_tick);
	last_tick = now;
	return elapsed;
}

void video_colour(int index, int r, int g, int b) {
	(&MARS_CRAM)[index & 255] = (uint16_t)COLOR(r, g, b);
}

/* The lab's words go to the MD text plane; palette lines 0, 1, 2 are grey, green, red. */
void ui_clear(void) { HwMdClearScreen(); }
void ui_puts(const char *s, int colour, int col, int row) { HwMdPuts((char *)s, (uint16_t)(colour << 13), col, row); }
void ui_colours(void) { }
void ui_erase(uint8_t *fb, int page) { (void)fb; (void)page; }
void ui_draw(uint8_t *fb, int page) { (void)fb; (void)page; }
void ui_flags(int region) { (void)region; }
