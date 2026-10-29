#include "common.h"
#include "frontend.h"
#ifdef R60_FRONTEND
#if !defined(R60_LIVEPAD) || !defined(R60_IDLECONSUME) || !defined(PORT_FULL)
#error "The frontend requires live pads, the MD idle consumer and the C port"
#endif
extern uint16_t read_joypad(uint8_t);
#define FE_COMM (*(volatile uint16_t*)MARS_COMM12)
#define FE_PHASE (*(volatile uint16_t*)MARS_COMM6)
#define FE_RAM __attribute__((section(".data"), noinline))

FE_RAM void frontend_boot_check(void)
{
    FE_COMM = (read_joypad(0) & 0x10u) ? FE_REQUEST : 0;
}

FE_RAM int frontend_input(unsigned short pad)
{
    static uint16_t last_start;
    /* Keep the game's existing Start/Mode alias for cores that bind Start
     * as Mode. The controller page still lets Mode be inspected as a button. */
    uint16_t start = pad & 0x0880u;
    if (start && !last_start && (pad & 0x10u) && FE_PHASE == FE_TITLE)
        FE_COMM = FE_REQUEST;
    last_start = start;
    return FE_WAITING(FE_COMM);
}

FE_RAM void frontend_service(void)
{
    uint16_t sr;
    if (FE_COMM != FE_GRANTED) return;
    __asm__ volatile ("move.w %%sr,%0\n\tmove.w #0x2700,%%sr" : "=d"(sr) : : "memory");
    frontend_menu_screen();
    /* The screen has restored its VDP state and posted the selected settings.
     * Wait for the game adapter to apply them before restarting MD interrupts. */
    while (FE_COMM != 0) __asm__ volatile ("nop");
    __asm__ volatile ("move.w %0,%%sr" : : "d"(sr) : "memory");
}
#endif
