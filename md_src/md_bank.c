/* md_bank.c -- the 68K side of the SSF2 cart mapper (CARTSSF=1). KIT.
 *
 * Only the 68K can page the cart: the mapper's registers sit on the Genesis /TIME strobe,
 * the one cart signal the 32X adapter does not mux (docs/design/SILICON.md 4e, from the
 * MiSTer RTL and d32xr's crt0.s set_bank_page). A write of a page number to 0xA130F1 +
 * 2*slot makes that 512 KB slot of the low 4 MB show that page to the 68K AND the SH-2s.
 * Pages are numbered from the cart's start (page 8 = 0x400000, the first paged byte,
 * sh_src/pages_data.s); the power-on mapping is slot n -> page n.
 *
 * The SH-2 asks through COMM10, the mailbox the port already uses for sound commands
 * (sh_src/port_l2.c: 0x5A00 | cmd): 0x5B00 | slot << 5 | page, acked by clearing it.
 * cart_bank_service() runs in the 68K's idle loop (md_main.c). Repaging a slot the SH-2
 * is reading code or art from is the one hazard, and it is logical, not electrical: a
 * slot's owner is the only requester (the stream lane owns slot 7).
 */
#include "common.h"

#define BANK_RAM __attribute__((section(".data"), noinline))
#define COMM10 (*(volatile uint16_t*)MARS_COMM10)
#define SSF_REG ((volatile uint8_t*)0xA130F1)

/* UNDER RV=1, AS d32xr DOES IT (crt0.s set_bank_page: sh2_wait, set_rv, the write). The MiSTer
 * core muxes the cart address bus to the SH-2's address whenever the SH-2 holds the ROM grant
 * (rtl/32X/32X.sv:351), and a /TIME write is not a ROM cycle, so a bank write landing during a
 * grant reaches cart.sv with the SH-2's address bits in VA[3:1]: the wrong register. RV=1
 * refuses new SH-2 grants (IF.sv:822) for the write's duration. The requesting SH-2 must be
 * running from SDRAM and not touching the cart meanwhile (its ROM access would wait on RV). */
#define DREQ_CTL (*(volatile uint16_t*)0xA15106)
BANK_RAM void cart_bank_set(unsigned slot, unsigned page)
{
	uint16_t d = DREQ_CTL;
	DREQ_CTL = (uint16_t)(d | 1u);                 /* RV = 1 */
	(void)DREQ_CTL; (void)DREQ_CTL;                /* let an in-flight grant finish */
	SSF_REG[2u * (slot & 7u)] = (uint8_t)(page & 31u);
	DREQ_CTL = (uint16_t)(d & ~1u);                /* RV back */
}

BANK_RAM void cart_bank_service(void)
{
	uint16_t v = COMM10;
	if ((v & 0xFF00u) == 0x5B00u) { cart_bank_set((v >> 5) & 7u, v & 31u); COMM10 = 0; }
#ifndef R60_SOUND
	/* No music lane in this build, so nothing consumes the port's sound commands and the
	 * first one would hold the mailbox forever (port_l2.c posts only when it reads 0). */
	else if ((v & 0xFF00u) == 0x5A00u) COMM10 = 0;
#endif
}

