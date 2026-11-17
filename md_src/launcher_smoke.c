/* launcher_smoke.c -- the door the launcher's own 68K code comes through (LAUNCHER,
 * 2026-11-14). KIT.
 *
 * The launcher (introlab/, on its own cart page in slot 7) puts a smoke layer on MD plane
 * A, and only the 68K can reach the MD's VDP. The low cart had 204 bytes left when this
 * was written, so the code that does the work is not here: it is a position-independent
 * 68K program inside the launcher's page (introlab/md/smoke68k.c), found through word 12
 * of the launcher's header table and run in place through the 68K's cart window, the way
 * the music is read. The launcher asks on the frontend mailbox, COMM12: FE_SMOKE | n, and
 * this side hands n to that program and clears the mailbox.
 *
 * The one thing that cannot run from the cart is the per-vblank part: a vblank can land
 * while md_bank.c holds RV=1, when the cart is not where the window says. The program
 * copies that part of itself to LAUNCHER_SMOKE_RAM, in the room between .bss and the boot
 * stack's reserve (md.ld: .bss may grow to 0xFF4C00), and sets launcher_smoke_on; the
 * vblank shim (r60_shim.h) calls it while the flag is up. The flag is down before the
 * launcher returns, so none of this exists once the game runs. */
#include "common.h"
#include "frontend.h"
#if defined(LAUNCHER) && defined(R60_FRONTEND)
#define SM_ROM __attribute__((section(".text.smoke"), noinline))   /* never folded into a RAM caller */
/* word 12 of the launcher's table: the program's SH-2 address inside slot 7 */
#define SM_HEAD (*(const volatile uint32_t*)(0x980000uL + LAUNCHER_BASE + 16u + 12u * 4u))
extern char __bss_end[];

volatile unsigned short launcher_smoke_on = 0;
#ifdef R60_SOUND
/* what the program may call back: the music engine's start and stop (md_sound.c), for the
 * launcher's own background stream, read from its page like any song */
extern void music_start_ex(uint32_t off, uint32_t len, uint8_t loop, uint8_t compressed, uint32_t loff, uint32_t llen);
extern void music_stop(void);
static void (*const sm_service[2])(void) = { (void (*)(void))music_start_ex, music_stop };
#else
#define sm_service ((void (*const *)(void))0)
#endif

SM_ROM void launcher_smoke(unsigned what)
{
	uint32_t at = SM_HEAD;
	const volatile uint16_t *p = (const volatile uint16_t*)(0x980000uL + (at & 0x7FFFFuL));
	if ((at >> 19) != (0x02380000uL >> 19) || p[0] != 0x534Du) return;      /* 'SM', then the entry's offset */
	if ((uint32_t)__bss_end > LAUNCHER_SMOKE_RAM) return;                  /* the copy would land on .bss */
	((void (*)(unsigned, volatile unsigned short *, void (*const *)(void)))((uint32_t)p + p[1]))(what, &launcher_smoke_on, sm_service);
}
#endif
