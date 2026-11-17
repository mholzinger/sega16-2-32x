/* smoke68k.c -- the launcher's smoke layer, the 68K half, as it runs inside the port.
 *
 * A position-independent program (built -mpcrel, no writable data of its own) that lives
 * in the launcher's cart page and is run in place by the port's 68K through its cart
 * window (md_src/launcher_smoke.c is the door, and says why the code is here and not
 * there). The picture follows the program in the page (tools/gen_wisps.py):
 *
 *   'SP', tiles, first cell row, cell rows, three MD colour words, 128 wave steps (bytes),
 *   tiles x 8 words, a word a row at two bits a pixel, rows x 64 cells (0 = empty, else 1 + tile)
 *
 * smoke_entry(what, flag, service): 1 uploads the picture to plane A and starts the wave,
 * 2..10 set the brightness 0..8, 0 takes everything away. 11 starts the launcher's
 * background music and 12 stops it: the stream (tools/gen_ambience.py: a 4-byte length,
 * then the port's packed player stream) follows the picture, and service[0] / service[1]
 * are the port's music start and stop (md_src/md_sound.c), or service is 0. 13 starts the
 * room's sounds and 14 stops them: short FM effects (tools/gen_room.py, after the stream)
 * fired at random moments, each from the left, the right or the middle: on FM channels
 * 4-6, or on channel 3 for the ones that use the chip's channel-3 mode. Under them, on
 * channels 1 and 2, a crowd's murmur: two held noisy voices, left and right, whose pitch
 * and level wander.
 * They need the music running: the YM2612 is held in reset with the Z80 until then.
 * 15 takes seventeen words from the launcher, one at a time over COMM2: a VRAM address and
 * sixteen words for it. That is how the floor's carpet (sh/carpet.c) reaches plane B: its
 * tiles go to 0x1000-0x3FFF and its cells to rows 19-27, palette line 3 colours 4-15. The wave is the one part that cannot
 * run from the cart, so `1` copies it (code, then the 128 steps) to SMOKE_RAM and raises
 * *flag; the port's vblank calls it there while the flag is up.
 *
 * VRAM borrowed: up to 512 tiles at 0x4000 and a per-line scroll table at 0xD000. Both
 * read all zero while the launcher is up (ares, launcher06 frame 650: nothing in
 * 0x1000-0x7FFF or 0xB000-0xFFFF before the game's first tick; CRAM line 3 black), so
 * zeroing them and putting the scroll registers back leaves the MD side as the game
 * expects it. */
#include <stdint.h>
#define VCTL  (*(volatile uint16_t*)0xC00004)
#define VWIDE (*(volatile uint32_t*)0xC00004)
#define VDATA (*(volatile uint16_t*)0xC00000)
#define VRAM_AT(a) (VWIDE = ((uint32_t)(0x4000u | ((a) & 0x3FFFu)) << 16) | (((a) >> 14) & 3u))
#define SMOKE_RAM  0x00FF4980uL                  /* md_src/frontend.h: LAUNCHER_SMOKE_RAM */
#define SM_TILES   0x4000u
#define SM_HSCROLL 0xD000u
#define SM_LINE0   112u
#define SM_PAL     49u                           /* palette line 3, colours 1-3 */
#define VINTS      (*(volatile uint16_t*)0xFFB0F0)   /* the port's vblank count */

#define TICK_SLOT   (*(void (*volatile *)(void))(SMOKE_RAM + 0x1B8uL))   /* called by the wave each vblank when set */
#define ROOM        ((volatile struct room *)(SMOKE_RAM + 0x1C0uL))
#define ZBUSREQ     (*(volatile uint16_t*)0xA11100)
#define YM          ((volatile uint8_t*)0xA04000)
struct voice { const uint8_t *at; uint8_t pan, wait; };
struct room { uint16_t rng, on; struct voice v[4]; uint16_t mf[2]; uint8_t mt[2]; };     /* voice 0 is channel 3 (its own mode); 1-3 are channels 4-6 */

#define COMM2       (*(volatile uint16_t*)0xA15122)
#define COMM12      (*(volatile uint16_t*)0xA1512C)
#define CARPET_VRAM 0x1000u                      /* 384 tiles */
#define CARPET_ROW0 19u
#define CARPET_ROWS 9u
/* navy, red, orange, yellow, green, lilac: each far (dim) then near. The five colours are at
 * 72% brightness (45% far) and three quarters of their saturation (Mike, 2026-11-14: the
 * first set, at full strength, was too bright for a dark room). */
static const uint16_t carpet_pal[12] = { 0x0200, 0x0422, 0x0226, 0x044A, 0x0246, 0x046A, 0x0266, 0x048A, 0x0464, 0x0486, 0x0644, 0x0868 };

extern const uint16_t smoke_pic[];               /* the picture, right after this program */
extern const uint8_t wave_start[], wave_end[];   /* the part that is copied to RAM */

/* THE WAVE. Runs from RAM; its 128 steps sit right after its last byte there. */
__attribute__((section(".wave"), noinline)) void smoke_wave(void)
{
	const int8_t *step = (const int8_t *)wave_end;
	uint16_t ph = (uint16_t)(VINTS + SM_LINE0);
	VCTL = 0x8F04;                               /* one line a step: plane A's word only */
	VRAM_AT(SM_HSCROLL + SM_LINE0 * 4u);
	for (uint16_t y = SM_LINE0; y < 224u; y++) VDATA = (uint16_t)(int16_t)step[ph++ & 127u];
	VCTL = 0x8F02;
	{ void (*tick)(void) = TICK_SLOT; if (tick) tick(); }   /* the room's sounds, in the cart: nothing pages while the launcher is up */
}

static void light(uint16_t level)                /* 0..8 */
{
	VWIDE = 0xC0000000uL | ((uint32_t)(SM_PAL * 2u) << 16);
	for (uint16_t i = 0; i < 15u; i++) {         /* the three smoke shades, then the carpet's twelve colours */
		uint16_t c = i < 3u ? smoke_pic[4u + i] : carpet_pal[i - 3u], o = 0;
		for (uint16_t sh = 1; sh < 12u; sh += 4u) o |= (uint16_t)(((((c >> sh) & 7u) * level) >> 3) << sh);
		VDATA = o;
	}
}

static void wipe(void)
{
	VRAM_AT(SM_TILES);
	for (uint16_t i = 0; i < smoke_pic[1] * 16u; i++) VDATA = 0;
	VRAM_AT(SM_HSCROLL);
	for (uint16_t i = 0; i < 224u * 2u; i++) VDATA = 0;
	VRAM_AT(0xC000u + smoke_pic[2] * 128u);
	for (uint16_t i = 0; i < smoke_pic[3] * 64u; i++) VDATA = 0;
	VRAM_AT(CARPET_VRAM);                        /* the carpet's tiles and its plane B rows */
	for (uint16_t i = 0; i < 384u * 16u; i++) VDATA = 0;
	VRAM_AT(0xE000u + CARPET_ROW0 * 128u);
	for (uint16_t i = 0; i < CARPET_ROWS * 64u; i++) VDATA = 0;
}

/* Seventeen words from the launcher: it puts one in COMM2 and raises 0xFE2F in COMM12; this
 * side takes it and answers 0xFE2E, except after the last, which the caller clears. */
static void words(void)
{
	for (uint16_t i = 0; i < 17u; i++) {
		uint16_t w, g = 60000;
		if (i) { COMM12 = 0xFE2E; while (COMM12 != 0xFE2Fu && --g) ; if (!g) return; }
		w = COMM2;
		if (!i) VRAM_AT(w); else VDATA = w;
	}
}

static void start(volatile uint16_t *flag)
{
	const uint16_t *b = smoke_pic + 7u;
	const uint8_t *s = wave_start;
	volatile uint32_t ram = SMOKE_RAM;           /* kept apart from the pc-relative labels: gcc otherwise folds the two */
	uint8_t *d = (uint8_t *)ram;
	uint16_t tiles = smoke_pic[1], cells = (uint16_t)(smoke_pic[3] * 64u);
	for (uint16_t n = (uint16_t)(wave_end - wave_start); n; n--) *d++ = *s++;
	for (uint16_t i = 0; i < 64u; i++) { uint16_t w = *b++; *d++ = (uint8_t)(w >> 8); *d++ = (uint8_t)w; }
	TICK_SLOT = 0; ROOM->on = 0;
	wipe();
	light(0);
	VRAM_AT(SM_TILES);
	for (uint16_t i = 0; i < tiles * 8u; i++) {      /* a row: eight 2-bit pixels, spread to the VDP's four bits each */
		uint16_t w = *b++;
		for (uint16_t h = 0; h < 2u; h++) {
			uint16_t o = 0;
			for (uint16_t k = 0; k < 4u; k++) { o = (uint16_t)(o << 4 | (w >> 14)); w <<= 2; }
			VDATA = o;
		}
	}
	VRAM_AT(0xC000u + smoke_pic[2] * 128u);
	for (uint16_t i = 0; i < cells; i++) {
		uint16_t c = *b++;
		VDATA = c ? (uint16_t)(0x6000u | (SM_TILES / 32u - 1u + c)) : 0;   /* palette line 3 */
	}
	VCTL = 0x8D00u | (SM_HSCROLL >> 10);
	VCTL = 0x8B03;                               /* a scroll value for every line */
	*flag = 1;
}

/* THE ROOM'S SOUNDS. Where the table is: after the picture and the music stream. */
static const uint16_t *room_table(void)
{
	const uint16_t *a = smoke_pic + 7u + 64u + smoke_pic[1] * 8u + smoke_pic[3] * 64u;
	uint32_t len = ((uint32_t)a[0] << 16) | a[1];
	return (const uint16_t *)((const uint8_t *)(a + 2) + ((len + 1u) & ~1uL));
}

static void ym(uint16_t part, uint8_t reg, uint8_t val)
{
	uint16_t g = 200; while ((YM[0] & 0x80) && --g) ;
	YM[part] = reg;
	g = 200; while ((YM[0] & 0x80) && --g) ;
	YM[part + 1u] = val;
}

/* THE CROWD. Each of channels 1 and 2 is two pairs of operators (algorithm 4). In each pair
 * the first operator feeds back on itself as hard as the chip allows, which turns its sine
 * into noise, and drives the second, which is heard: a band of noise around a low pitch.
 * The notes are held; what makes it a room of people is that pitch and level never sit
 * still (murmur_drift, every fourth vblank). Channel 1 is on the left, 2 on the right. */
static const uint16_t murmur_lo[2] = { 600, 950 }, murmur_hi[2] = { 900, 1400 };   /* F-numbers in block 3: 120-185 Hz, 195-285 Hz */

static void murmur_pitch(uint16_t c, uint16_t f)
{
	ym(0, (uint8_t)(0xA4u + c), (uint8_t)(0x18u | (f >> 8)));
	ym(0, (uint8_t)(0xA0u + c), (uint8_t)f);
}

static void murmur_start(volatile struct room *r, uint16_t level)
{
	static const uint8_t mul[4] = { 0x01, 0x03, 0x01, 0x02 };    /* registers go operator 1, 3, 2, 4 */
	for (uint16_t c = 0; c < 2u; c++) {
		for (uint16_t op = 0; op < 4u; op++) {
			uint8_t o = (uint8_t)(op * 4u + c);
			ym(0, (uint8_t)(0x30u + o), mul[op]);
			ym(0, (uint8_t)(0x40u + o), op < 2u ? (uint8_t)(0x0Cu + op * 8u) : (uint8_t)(level + (op - 2u) * 4u));
			ym(0, (uint8_t)(0x50u + o), 0x1F);                  /* at once */
			ym(0, (uint8_t)(0x60u + o), 0x00);                  /* and held */
			ym(0, (uint8_t)(0x70u + o), 0x00);
			ym(0, (uint8_t)(0x80u + o), 0x05);
			ym(0, (uint8_t)(0x90u + o), 0x00);
		}
		ym(0, (uint8_t)(0xB0u + c), 0x3C);                      /* feedback 7, algorithm 4 */
		ym(0, (uint8_t)(0xB4u + c), c ? 0x40 : 0x80);
		r->mf[c] = (uint16_t)((murmur_lo[c] + murmur_hi[c]) >> 1); r->mt[c] = (uint8_t)level;
		murmur_pitch(c, r->mf[c]);
		ym(0, 0x28, (uint8_t)(0xF0u | c));
	}
}

static void murmur_drift(volatile struct room *r, uint16_t x, uint16_t level)
{
	for (uint16_t c = 0; c < 2u; c++, x >>= 5) {
		uint16_t f = (uint16_t)(r->mf[c] + (x & 15u) - 7u);
		uint8_t l = (uint8_t)(r->mt[c] + ((x >> 4) & 1u) * 2u - 1u);
		if (f < murmur_lo[c]) f = murmur_lo[c];
		if (f > murmur_hi[c]) f = murmur_hi[c];
		if (l < level) l = (uint8_t)level;
		if (l > level + 10u) l = (uint8_t)(level + 10u);
		r->mf[c] = f; r->mt[c] = l;
		murmur_pitch(c, f);
		ym(0, (uint8_t)(0x48u + c), l); ym(0, (uint8_t)(0x4Cu + c), (uint8_t)(l + 4u));
	}
}

/* One vblank: maybe fire an effect on a free voice, then play this frame of each voice.
 * The Z80 owns the YM2612 (it clocks the song off the chip's timer), so it is paused for
 * the writes, and the address latch is left on the timer's register: if the pause fell
 * between the Z80's own address and data writes, its data still lands where it meant. */
static void room_tick(void)
{
	volatile struct room *r = ROOM;
	const uint16_t *t = room_table();
	uint16_t x = r->rng, wrote = 0;
	const uint8_t *fire = 0;
	if (!r->on) return;
	x ^= (uint16_t)(x << 7); x ^= (uint16_t)(x >> 9); x ^= (uint16_t)(x << 8);
	r->rng = x;
	if ((uint16_t)(x % t[1]) == 0u) fire = (const uint8_t *)t + t[3u + (uint16_t)((x >> 8) % t[0])];
	for (uint16_t v = 0; v < 4u; v++) {
		volatile struct voice *o = &r->v[v];
		const uint8_t *p = o->at;
		uint8_t c;
		if (!p) {                                   /* free: take the effect if it is this voice's kind */
			if (!fire || (*fire != 0u) != (v == 0u)) continue;
			p = fire + 1; fire = 0;
			o->pan = (uint8_t)(0x40u << ((x >> 4) & 1u)) | (uint8_t)(((x >> 5) & 3u) == 0u ? 0xC0u : 0u);   /* left or right, one in four from the middle */
			o->wait = 0;
		}
		if (o->wait) { o->wait--; o->at = p; continue; }
		c = *p++;
		if (c & 0x80u) {
			if (c == 0xFFu) p = 0; else o->wait = (uint8_t)((c & 0x7Fu) - 1u);
			if (p) { o->at = p; continue; }
		}
		if (!wrote) { ZBUSREQ = 0x100; { uint16_t g = 2000; while ((ZBUSREQ & 0x100) && --g) ; } wrote = 1; }
		if (!p) { ym(0, 0x28, v ? (uint8_t)(3u + v) : 2u); o->at = 0; continue; }   /* the effect ended: key off */
		for (; c; c--) {
			uint8_t reg = *p++, val = *p++;
			if (!v) ym(0, reg, reg == 0xB6u ? (uint8_t)((val & 0x3Fu) | o->pan) : val);    /* channel 3, as its game wrote it */
			else if (reg == 0x28u) ym(0, 0x28, (uint8_t)(val | (3u + v)));
			else ym(2, (uint8_t)(reg + v - 1u), reg == 0xB4u ? (uint8_t)((val & 0x3Fu) | o->pan) : val);
		}
		o->at = p;
	}
	if (t[2] && !(VINTS & 3u)) {
		if (!wrote) { ZBUSREQ = 0x100; { uint16_t g = 2000; while ((ZBUSREQ & 0x100) && --g) ; } wrote = 1; }
		murmur_drift(r, (uint16_t)(x >> 3), t[2]);
	}
	if (wrote) { YM[0] = 0x27; ZBUSREQ = 0x000; }
}

/* CHANNEL 3'S OWN MODE is bit 6 of register 0x27, and 0x27 is also the timer the Z80's
 * player clocks the song with: it rewrites the register every millisecond, without the
 * bit. So while the room is on, the player's own constant gets the bit (its two
 * `ld b,0x27 / ld c,n` in Z80 RAM). The player is uploaded afresh by every music start, so
 * nothing of this outlives the launcher. */
static void ch3_mode(uint16_t on)
{
	volatile uint8_t *z = (volatile uint8_t *)0xA00000;
	for (uint16_t i = 0; i < 0x200u; i++)
		if (z[i] == 0x06u && z[i + 1u] == 0x27u && z[i + 2u] == 0x0Eu)
			z[i + 3u] = on ? (uint8_t)(z[i + 3u] | 0x40u) : (uint8_t)(z[i + 3u] & ~0x40u);
}

static void room(uint16_t on)
{
	volatile struct room *r = ROOM;
	const uint16_t *t = room_table();
	if (on) {
		if (!t[0] || !t[1]) return;                 /* no effects were built in */
		for (uint16_t i = 0; i < sizeof *r; i++) ((volatile uint8_t *)r)[i] = 0;
		r->rng = (uint16_t)(VINTS | 1u);
		ZBUSREQ = 0x100; { uint16_t g = 2000; while ((ZBUSREQ & 0x100) && --g) ; }
		ch3_mode(1);
		if (t[2]) murmur_start(r, t[2]);
		YM[0] = 0x27; ZBUSREQ = 0x000;
		r->on = 1;
		{ volatile uint32_t here = (uint32_t)room_tick; TICK_SLOT = (void (*)(void))here; }
		return;
	}
	if (!r->on) return;
	r->on = 0;
	TICK_SLOT = 0;
	ZBUSREQ = 0x100; { uint16_t g = 2000; while ((ZBUSREQ & 0x100) && --g) ; }
	ch3_mode(0);
	for (uint16_t v = 0; v < 4u; v++) {
		uint16_t part = v ? 2u : 0u; uint8_t c = v ? (uint8_t)(v - 1u) : 2u;
		ym(0, 0x28, v ? (uint8_t)(3u + v) : 2u);
		for (uint8_t op = 0; op < 16u; op += 4u) ym(part, (uint8_t)(0x40u + op + c), 0x7F);
	}
	for (uint16_t c = 0; c < 2u; c++) {              /* the crowd goes quiet */
		ym(0, 0x28, (uint8_t)c);
		ym(0, (uint8_t)(0x48u + c), 0x7F); ym(0, (uint8_t)(0x4Cu + c), 0x7F);
	}
	YM[0] = 0x27; ZBUSREQ = 0x000;
}

typedef void (*music_start_fn)(uint32_t off, uint32_t len, uint8_t loop, uint8_t compressed, uint32_t loff, uint32_t llen);

static void music(uint16_t on, void (*const *service)(void))
{
	const uint16_t *a = smoke_pic + 7u + 64u + smoke_pic[1] * 8u + smoke_pic[3] * 64u;
	uint32_t len = ((uint32_t)a[0] << 16) | a[1];
	if (!service) return;
	if (!on) { service[1](); return; }
	if (!len) return;                            /* no songs were built in: silence */
	{	/* where the stream sits in the music engine's own terms: an offset into the cart window */
		volatile uint32_t win = 0x980000uL;
		uint32_t off = (uint32_t)(a + 2) - win;
		((music_start_fn)service[0])(off, len, 1, 1, off, len);    /* packed, and it loops as a whole */
	}
}

__attribute__((section(".entry"))) void smoke_entry(uint16_t what, volatile uint16_t *flag, void (*const *service)(void))
{
	uint16_t sr;
	if (smoke_pic[0] != 0x5350u) return;
	if (what == 11u || what == 12u) { music(what == 11u, service); return; }
	if (what == 15u) {
		__asm__ volatile ("move.w %%sr,%0\n\tmove.w #0x2700,%%sr" : "=d"(sr) : : "memory");
		words();
		__asm__ volatile ("move.w %0,%%sr" : : "d"(sr) : "memory");
		return;
	}
	if (what == 13u || what == 14u) {
		if (!*flag) return;                         /* the wave carries the tick: no smoke, no room */
		__asm__ volatile ("move.w %%sr,%0\n\tmove.w #0x2700,%%sr" : "=d"(sr) : : "memory");
		room(what == 13u);
		__asm__ volatile ("move.w %0,%%sr" : : "d"(sr) : "memory");
		return;
	}
	__asm__ volatile ("move.w %%sr,%0\n\tmove.w #0x2700,%%sr" : "=d"(sr) : : "memory");
	if (what == 1u) start(flag);
	else if (what >= 2u && what <= 10u) { if (*flag) light((uint16_t)(what - 2u)); }
	else if (*flag) {
		*flag = 0;
		TICK_SLOT = 0;
		VCTL = 0x8B00;
		VCTL = 0x8D3F;                           /* the boot's table, 0xFC00 (md_src/md_main.c) */
		light(0);
		wipe();
	}
	__asm__ volatile ("move.w %0,%%sr" : : "d"(sr) : "memory");
}
