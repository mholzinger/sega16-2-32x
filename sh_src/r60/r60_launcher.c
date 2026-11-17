/* r60_launcher.c -- loads and runs the launcher from its own cart page, before the game
 * starts. LAUNCHER=1. KIT (the mechanism; the launcher itself is per title: introlab/).
 *
 * THE PAGE (tools/launcher_page.py, sh_src/pages_data.s) is linked after the last game
 * page, so its number is derived from its label and follows the game's page layout. Like
 * every page shown in slot 7 it begins with the music data, which the game's 68K reads
 * through that window on every vblank; the launcher follows at LAUNCHER_BASE. Slot 7
 * (0x380000-0x3FFFFF) is the only paged window and the 68K is the only CPU that can page
 * (md_src/md_bank.c: COMM10 0x5B00 | slot << 5 | page, acked by clearing it).
 *
 * THE PROGRAM in the page is linked for slot 7 (introlab/sh/port.ld) and begins with a
 * table (introlab/sh/port_entry.c)): "LPRG", the two CPUs' entries, its SDRAM image and
 * .bss, a stack for each CPU, and the range of SDRAM it borrows. THE BORROW is the port's
 * copies of the arcade's tile map and text layer, 0x06019000-0x06027000: all zero before
 * the game's first tick, counted here to prove it, and zeroed again before the game runs.
 * The palette copy and the renderer's tables above 0x06027000 are filled during boot and
 * are left alone (the primary's vblank interrupt counts into one of them, and that count
 * is the launcher's clock). The stacks are the port's coroutine stack, which the game
 * paints when it starts; the boot stacks leave under 5 KB above the clip code.
 *
 * WHERE THIS CODE RUNS. A CPU must not fetch from the cart while the 68K writes the mapper
 * (md_bank.c: the write is made under RV=1), and neither SDRAM code region has room
 * (.ramtext 104 bytes, .ramtext2 64). So everything that runs across a page switch is its
 * own section, .launchtext, copied here to 0x06038000: the 4 KB probe scrap, which the game
 * only uses as scratch it fills before each use (r60_planes.c:1280). Its load image sits in
 * the 1.3 KB left under .sounddata (tools/gen_ld_ssf.py --launcher).
 *
 * EACH SH-2 HAS ITS OWN CACHE. Everything both CPUs touch here goes through the uncached
 * mirror, and each CPU purges its cache after every page switch (a line cached from one
 * page must not answer for the other) and before running code the other CPU just copied.
 *
 * launcher_stat, for a headless read-back:
 *   [0] the page number            [4] what the launcher returned (launcher_boot)
 *   [1] the launcher header's tag  [5] slot 7's first word before the switch
 *   [2] the primary's payload sum  [6] the same word after page 8 is restored
 *   [3] the sum in the header      [7] 0x600D: ran and restored; 0xBAD0: page or program
 *                                      not as built, launcher skipped; 0xBAD1: not restored
 *   [8] vblanks the launcher held the boot for
 *   [9] non-zero words found in the borrowed range before the launcher used it (want 0)
 */
#ifdef LAUNCHER
#include <stdint.h>
#include "mars.h"

#define LTEXT __attribute__((noinline, section(".launchtext")))
#define SLOT7 ((volatile uint32_t *)0x22380000u)        /* uncached */
#define LPAGE (SLOT7 + LAUNCHER_BASE / 4u)              /* the launcher's header: the music data comes first (pages_data.s) */
#define GAME_PAGE 8u                                    /* what the 68K maps at boot (md_main.c: cart_bank_set(7, 8)) */
#define LNCH 0x4C4E4348u
#define LPRG 0x4C505247u
#define VBLANKS ((volatile uint32_t *)0x26028E40u)      /* R60_DIAG[0]: the primary's vblank interrupt count (r60_main.c) */
#define CACHE_PURGE() (*(volatile uint8_t *)0xFFFFFE92u = 0x11u)   /* CCR: enabled + purge, as r60_main.c's page switch */

extern const uint8_t launcher_page_blob[];
extern uint32_t launchtext_start[], launchtext_end[], launchtext_load[];
volatile uint32_t launcher_stat_mem[10];
/* primary -> secondary: 2 = .launchtext is in place, 1 = run your entry, 0xFFFF = leave.
 * secondary -> primary: 3 = waiting in SDRAM, 0 = entry returned. */
volatile uint16_t launcher_cmd_mem;
volatile uint32_t launcher_entry2_mem[2];               /* the secondary's entry and stack */
#define launcher_stat   ((volatile uint32_t *)((uintptr_t)launcher_stat_mem | 0x20000000u))
#define launcher_cmd    (*(volatile uint16_t *)((uintptr_t)&launcher_cmd_mem | 0x20000000u))
#define launcher_entry2 ((volatile uint32_t *)((uintptr_t)launcher_entry2_mem | 0x20000000u))

/* int launch_call(fn, stack_top, arg): call fn(arg) on another stack. */
int launch_call(uint32_t fn, uint32_t stack_top, uint32_t arg);
__asm__(
"	.section .launchtext,\"ax\"\n"
"	.align 2\n"
"_launch_call:\n"
"	sts.l	pr,@-r15\n"
"	mov	r15,r0\n"
"	mov	r5,r15\n"
"	mov.l	r0,@-r15\n"
"	jsr	@r4\n"
"	mov	r6,r4\n"
"	mov.l	@r15,r15\n"
"	lds.l	@r15+,pr\n"
"	rts\n"
"	nop\n"
"	.text\n");

LTEXT static void launch_page(unsigned page)
{
    while (MARS_SYS_COMM10 != 0) ;
    MARS_SYS_COMM10 = (uint16_t)(0x5B00u | (7u << 5) | page);
    while (MARS_SYS_COMM10 != 0) ;
    CACHE_PURGE();
}

LTEXT static uint32_t launch_sum(void)
{
    uint32_t words = LPAGE[1] >> 2, sum = 0, i;
    if (LPAGE[0] != LNCH || words > (0x80000u - LAUNCHER_BASE - 16u) / 4u) return 0xFFFFFFFFu;
    for (i = 0; i < words; i++) sum += LPAGE[4u + i];
    return sum;
}

LTEXT static void launch_primary(void)
{
    volatile uint32_t *head = LPAGE + 4;                /* the program's table, after the launcher's header */
    unsigned page = (unsigned)(((uintptr_t)launcher_page_blob - 0x02000000u) >> 19);
    uint32_t t0 = *VBLANKS, ok;
    while (launcher_cmd != 3u) ;                        /* the secondary has left the cart */
    launcher_stat[0] = page;
    launcher_stat[5] = SLOT7[0];
    launch_page(page);
    launcher_stat[1] = LPAGE[0];
    launcher_stat[3] = LPAGE[2];
    launcher_stat[2] = launch_sum();
    ok = launcher_stat[1] == LNCH && launcher_stat[2] == launcher_stat[3] && head[0] == LPRG;
    if (ok) {
        volatile uint32_t *s = (volatile uint32_t *)(head[3] | 0x20000000u);
        uint32_t *d, *e, used = 0;
        for (d = (uint32_t *)head[10], e = (uint32_t *)head[11]; d < e; d++) if (*d) used++;
        launcher_stat[9] = used;
        d = (uint32_t *)head[4]; e = (uint32_t *)head[5];
        while (d < e) *d++ = *s++;
        for (d = (uint32_t *)head[6], e = (uint32_t *)head[7]; d < e; ) *d++ = 0;
        launcher_entry2[0] = head[2]; launcher_entry2[1] = head[9];
        launcher_cmd = 1u;
        launcher_stat[4] = (uint32_t)launch_call(head[1], head[8], (uint32_t)VBLANKS);
        while (launcher_cmd != 0u) ;
        for (d = (uint32_t *)head[10], e = (uint32_t *)head[11]; d < e; ) *d++ = 0;   /* the borrow goes back as it came */
    }
    launch_page(GAME_PAGE);
    launcher_stat[6] = SLOT7[0];
    launcher_stat[7] = !ok ? 0xBAD0u : launcher_stat[6] == launcher_stat[5] ? 0x600Du : 0xBAD1u;
    launcher_stat[8] = *VBLANKS - t0;
    launcher_cmd = 0xFFFFu;
}

LTEXT static void launch_secondary(void)
{
    launcher_cmd = 3u;
    for (;;) {
        uint16_t c = launcher_cmd;
        if (c == 0xFFFFu) { CACHE_PURGE(); return; }   /* the game's page is back */
        if (c == 1u) {
            CACHE_PURGE();                              /* the page changed and the primary copied code into SDRAM */
            launch_call(launcher_entry2[0], launcher_entry2[1], 0);
            launcher_cmd = 0;
        }
    }
}

/* The primary, once, before the first game tick (r60_main.c m_main). Returns what the
 * launcher answered: the region in bits 0-1 and, when bit 7 is set, the switch sheet's
 * result (starting round << 8, switch bank 1 << 16, bank 2 << 24). LAUNCHER_NONE when the
 * launcher did not run. */
__attribute__((noinline)) unsigned launcher_boot(void)
{
    uint32_t *d = launchtext_start, *s = launchtext_load;
    while (d < launchtext_end) *d++ = *s++;
    launcher_cmd = 2u;
    launch_primary();
    return launcher_stat[7] == 0xBAD0u ? 0xFFFFFFFFu : launcher_stat[4];
}

/* The secondary, once, after the boot handshake (r60_main.c s_main). */
__attribute__((noinline)) void launcher_secondary(void)
{
    while (launcher_cmd != 2u) ;
    launch_secondary();
}
#endif
