#ifndef INTROLAB_FX_H
#define INTROLAB_FX_H
/* Fixed-point and SH-2 helpers shared by the lab's renderers. */
#include <stdint.h>
#include "mars.h"
#include "gen_tables.h"

#ifdef NORAMCODE
#define HOT
#else
#define HOT __attribute__((section(".ramtext")))   /* run from SDRAM, not the cart */
#endif
/* The same address through the uncached mirror: what the other CPU last wrote. */
#define UNCACHED(p) ((__typeof__(p))((uintptr_t)(p) | 0x20000000u))

#define SPLIT_ROW  112          /* the primary draws rows above this, the secondary from here down */
/* FX_JOB, primary -> secondary: draw your half of this kind of frame. The secondary writes 0
 * when done. The lab uses COMM4. Inside the port (PORT) COMM4 belongs to the game's 68K, so
 * the job is a word in SDRAM, read and written through the uncached mirror by both CPUs. */
#define CMD_CABINET 0x00C1
#define CMD_DOORS   0x00C2
#define CMD_QUIT    0x00CF      /* PORT only: the launcher is over, the secondary returns */
#define CMD_CROWD_ON  0x00C3    /* PORT only: the secondary starts the crowd's loop (crowd.c) */
#define CMD_CROWD_OFF 0x00C4    /* and stops it, giving the PWM and its DMA channel back */
#ifdef PORT
extern volatile uint16_t fx_job_word;
#define FX_JOB (*UNCACHED(&fx_job_word))
#else
#define FX_JOB MARS_SYS_COMM4
#endif

static inline int32_t isin(int a) { return sin_tab[a & (ANGLE_STEPS - 1)]; }
static inline int32_t icos(int a) { return sin_tab[(a + ANGLE_STEPS / 4) & (ANGLE_STEPS - 1)]; }

/* (a * b) >> 16 and >> 32, kept to shapes the SH-2 has an instruction for: it has no
 * barrel shifter. */
static inline int32_t mul16(int32_t a, int32_t b) {
	int64_t p = (int64_t)a * b;
	return (int32_t)(((uint32_t)(p >> 32) << 16) | ((uint32_t)p >> 16));
}
static inline int32_t mul32(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 32); }

/* 32-bit signed divide on the SH-2's hardware divider (39 cycles), or libgcc with SWDIV=1.
 * Each CPU has its own divider; nothing in the lab divides inside an interrupt. */
static inline int32_t sdiv(int32_t a, int32_t b) {
#ifdef SWDIV
	return a / b;
#else
	*(volatile int32_t *)0xFFFFFF00 = b;        /* DVSR  */
	*(volatile int32_t *)0xFFFFFF04 = a;        /* DVDNT: the write starts the divide */
	return *(volatile int32_t *)0xFFFFFF04;
#endif
}

/* Free-running timer, 5.56 us per tick once fx_timer_start() has run. Wraps at 0.36 s. */
static inline uint16_t frt(void) {
	uint8_t hi = SH2_FRT_FRCH, lo = SH2_FRT_FRCL;
	return (uint16_t)((hi << 8) | lo);
}
#ifdef PORT
static inline void fx_timer_start(void) { }     /* the port's timer is already running and is not ours to set */
#else
static inline void fx_timer_start(void) { SH2_FRT_TIER = 0x01; SH2_FRT_TCR = 0x02; SH2_FRT_FTCSR = 0; }
#endif

/* Fill the part of a convex polygon inside rows [y0, y1) with one colour. `xy` holds n
 * corners as x0,y0,x1,y1,... in 28.4 screen units, in either winding. One divide per edge. */
void poly_flat(uint8_t *fb, const int32_t *xy, int n, int y0, int y1, uint8_t colour);

#endif
