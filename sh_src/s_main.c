#include "mars.h"

/* Referenced by the DMA IRQ vector in mars_start.s; no PWM audio yet. */
__attribute__((section(".ramtext"))) void amb_dma_handler(void)
{
}

#define TEXT_U      ((volatile uint16_t *)0x26025000)
#define SYNC        ((volatile uint16_t *)0x26027800)

extern void slave_window_k(uint16_t cmd);    /* m_main.c .ramtext */
extern void slave_tile_third(uint16_t cmd);

/* MD stream servicing lives on the SLAVE now: the master spends the
 * inter-window gap composing tiles, so it can't poll COMM0. Batches are
 * MD-written COMM payloads; acking = clearing COMM0. The 0x2000 render
 * command is left for the master (distinct pattern; both CPUs poll, only
 * one consumes each kind). */
__attribute__((section(".ramtext"))) void slave_service_stream(void)
{
    uint16_t c0 = MARS_SYS_COMM0;
    if (c0 & 0x4000) {                       /* text batch */
        int idx = c0 & 0x7FF;
        uint16_t w0 = MARS_SYS_COMM2, w1 = MARS_SYS_COMM4, w2 = MARS_SYS_COMM6;
        uint16_t w3 = MARS_SYS_COMM8, w4 = MARS_SYS_COMM10;
        if (idx + 4 < 2048) {
            TEXT_U[idx + 0] = w0;
            TEXT_U[idx + 1] = w1;
            TEXT_U[idx + 2] = w2;
            TEXT_U[idx + 3] = w3;
            TEXT_U[idx + 4] = w4;
        }
        MARS_SYS_COMM0 = 0;
    }
}

/* Secondary SH-2: boot heartbeat on COMM6 until the MD's 0xB007 beacon
 * (proves the slave reached SDRAM code), then: render worker + stream
 * servicer. Commands arrive via the SDRAM SYNC mailbox — NOT the COMM
 * registers, which carry MD stream payloads whenever the game runs. */
__attribute__((section(".ramtext"))) void s_main(void)
{
    while (MARS_SYS_COMM14 != 0xB007)
        MARS_SYS_COMM6++;

    for (;;) {
        uint16_t cmd = SYNC[0];
        if (cmd & 0xF000) {
            if ((cmd & 0xF000) == 0x3000)
                slave_window_k(cmd);         /* slice blit + row-region compose */
            else
                slave_tile_third(cmd);       /* concurrent tile third */
            SYNC[1] = cmd;                   /* done */
            while (SYNC[0] == cmd)           /* wait master clear; keep the */
                slave_service_stream();      /* stream alive meanwhile */
        } else {
            slave_service_stream();
        }
    }
}
