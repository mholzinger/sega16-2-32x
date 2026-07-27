#include "mars.h"

/* Referenced by the DMA IRQ vector in mars_start.s; no PWM audio yet. */
__attribute__((section(".ramtext"))) void amb_dma_handler(void)
{
}

extern void slave_render(uint16_t bank1, int par);  /* m_main.c, .ramtext */

/* Secondary SH-2: heartbeat on COMM6 so the MD's boot can prove the slave
 * reached SDRAM code. It must stop free-running once the game starts: the
 * MD->master stream carries batch data through COMM6, and a wild increment
 * races those words (latent corruption the emulators' coarse scheduling
 * mostly hid). The MD posts 0xB007 on COMM14 after it has consumed the
 * heartbeat, strictly before the game (and the stream) start.
 *
 * After that: render worker. Master posts 0xC000|bank on COMM4 inside each
 * render window; slave composes the top half of the frame into sbuf, posts
 * 0xD0 on COMM6, and clears COMM6 once the master lowers the command
 * (COMM6 belongs to the MD stream between windows). */
__attribute__((section(".ramtext"))) void s_main(void)
{
    while (MARS_SYS_COMM14 != 0xB007)
        MARS_SYS_COMM6++;
    for (;;) {
        if ((MARS_SYS_COMM4 & 0xF000) == 0xC000) {
            slave_render(MARS_SYS_COMM4 & 7, (MARS_SYS_COMM4 >> 8) & 1);
            MARS_SYS_COMM6 = 0xD0;
            while ((MARS_SYS_COMM4 & 0xF000) == 0xC000) ;
            MARS_SYS_COMM6 = 0;
        }
    }
}
