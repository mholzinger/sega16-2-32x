#include "mars.h"

/* Referenced by the DMA IRQ vector in mars_start.s; no PWM audio yet. */
__attribute__((section(".ramtext"))) void amb_dma_handler(void)
{
}

#define SYNC        ((volatile uint16_t *)0x26028800)
/* (TEXT_U / PAL_U / PAL_SETGEN / pal_set_of retired with the COMM stream
 * in LOOP 8 — the master owns all three now, off the DREQ packet.) */

extern void slave_window_k(uint16_t cmd);    /* m_main.c .ramtext */
extern void slave_concurrent_k(uint16_t cmd);

/* The preempt-blit mailbox poll. It kept the name from when it also
 * serviced the MD's COMM stream; that stream is gone (LOOP 8), so the
 * 0x2000-family render command on COMM0 is now COMM's only traffic and
 * the master consumes it. */
#ifdef PICKUP_SRC_PROBE
#define PSRC ((volatile uint32_t *)0x26028F50)
volatile uint8_t slave_in_compose;
#endif

__attribute__((section(".ramtext"))) void slave_service_stream(void)
{
    /* (iter4) PREEMPT-BLIT MAILBOX: the master hands the per-window blit
     * here (SYNC[4]) instead of waiting for the whole concurrent compose
     * to drain. This is polled at EVERY stream-service point — between the
     * slave's compose strips and in the idle loop — so the master's blit
     * pickup latency is bounded by one 12-row strip, not a full compose.
     * Clear SYNC[4] BEFORE echoing so a re-entry never double-blits. */
    uint16_t bc = SYNC[4];
    if (bc) {
#ifdef PICKUP_SRC_PROBE
        /* Which side of the dependency are we on? An in-compose pickup
         * means the master is waiting for rows this CPU has not finished
         * composing; an idle-loop pickup means the compose was already
         * done and the mailbox was answered immediately. */
        PSRC[(slave_in_compose ? 0 : 3) + ((bc >> 4) & 3)]++;
#endif
        slave_window_k(bc);          /* blit slave half; SYNC[2] set inside */
        SYNC[4] = 0;
        SYNC[5] = bc;                /* echo: blit path complete */
    }

    /* LOOP 8: THE COMM STREAM IS GONE. Text and the layer/rowscroll regs
     * moved to the DREQ packet in LOOP 7a; the palette batch (0x4800|idx)
     * was COMM's last tenant and the write-thunks retired it — the MD now
     * marks dirty 128-word regions and the master applies them straight
     * off the DREQ packet, PAL_SETGEN included. So COMM0 carries only the
     * window command/ack (consumed by the master), and this function is
     * now purely the preempt-blit mailbox poll it shares the name with.
     * That is the whole point of the arc: COMM cost 1.27 lines per word
     * against DREQ's 0.063, because its cost was an ack round-trip per
     * 5-word batch and the 68K BLOCKED on the slave for every one. */
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
                slave_concurrent_k(cmd);     /* concurrent band compose */
            SYNC[1] = cmd;                   /* done */
            while (SYNC[0] == cmd)           /* wait master clear; keep the */
                slave_service_stream();      /* stream alive meanwhile */
        } else {
            slave_service_stream();
        }
    }
}
