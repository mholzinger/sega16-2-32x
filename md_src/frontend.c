#include "common.h"
#include "frontend.h"
#if !defined(R60_LIVEPAD) || !defined(R60_IDLECONSUME) || !defined(PORT_FULL)
#error "The frontend requires live pads, the MD idle consumer and the C port"
#endif
extern uint16_t read_joypad(uint8_t);
#define FE_COMM (*(volatile uint16_t*)MARS_COMM12)
#define FE_PHASE (*(volatile uint16_t*)MARS_COMM6)
#define FE_RAM __attribute__((section(".data"), noinline))

/* THE POWER-ON MENU DECIDES THE BOOT: the region and the coin switches take effect only
 * before the game's first frame, so the menu offers them only when opened here. */
/* Set before main runs, so it must not live in a .bss a later clear could wipe: a non-zero
 * initialiser keeps it in .data (frontend_boot_check always overwrites it first). */
volatile unsigned short frontend_at_boot = 0xFFFFu;

volatile unsigned short frontend_launch_region = 0;

FE_RAM void frontend_boot_check(void)
{
#ifdef LAUNCHER
    /* The launcher owns power-on: it opens this screen itself (FE_SHEET), so B held at
     * power-on asks for nothing. */
    frontend_at_boot = 0;
    FE_COMM = 0;
#else
    frontend_at_boot = (read_joypad(0) & 0x10u) ? 1u : 0u;
    FE_COMM = frontend_at_boot ? FE_REQUEST : 0;
#endif
}

FE_RAM int frontend_input(unsigned short pad)
{
    static uint16_t last_start;
    /* Keep the game's existing Start/Mode alias for cores that bind Start
     * as Mode. The controller page still lets Mode be inspected as a button. */
    uint16_t start = pad & 0x0880u;
    if (start && !last_start && (pad & 0x10u) && FE_PHASE == FE_TITLE) {
        frontend_at_boot = 0;
        FE_COMM = FE_REQUEST;
    }
    last_start = start;
    return FE_WAITING(FE_COMM);
}

FE_RAM void frontend_service(void)
{
    uint16_t sr, command = FE_COMM;
#ifdef LAUNCHER
    if ((command & 0xFFF0u) == FE_SMOKE) {
        extern void launcher_smoke(unsigned);
        launcher_smoke(command & 15u);
        FE_COMM = 0;
        return;
    }
    if ((command & 0xFFF0u) == FE_SHEET) {
        frontend_at_boot = 2u;                      /* the game has not booted: every switch is live */
        frontend_launch_region = command & 3u;
    } else
#endif
    if (command != FE_GRANTED) return;
    __asm__ volatile ("move.w %%sr,%0\n\tmove.w #0x2700,%%sr" : "=d"(sr) : : "memory");
    frontend_menu_screen();
#if defined(R60) && defined(NT_WRAP) && defined(PAL_FIRST)
    { extern void r60_palette_reset(void); r60_palette_reset(); }
#endif
    { extern void r60_tile_vram_reset(void); r60_tile_vram_reset(); }
    /* The screen has restored its VDP state and posted the selected settings.
     * Wait for the game adapter to apply them before restarting MD interrupts. */
    while (FE_COMM != 0) __asm__ volatile ("nop");
    __asm__ volatile ("move.w %0,%%sr" : : "d"(sr) : "memory");
}
