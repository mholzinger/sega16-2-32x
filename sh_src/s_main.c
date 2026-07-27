#include "mars.h"

/* Referenced by the DMA IRQ vector in mars_start.s; no PWM audio yet. */
__attribute__((section(".ramtext"))) void amb_dma_handler(void)
{
}

/* Secondary SH-2: idle heartbeat until the video split gives it work. */
__attribute__((section(".ramtext"))) void s_main(void)
{
    for (;;)
        MARS_SYS_COMM6++;
}
