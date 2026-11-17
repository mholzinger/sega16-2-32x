/* The launcher as a program on its own cart page (PORT). The port's loader
 * (sh_src/r60/r60_launcher.c) shows the page in slot 7, reads the table below from its
 * start, copies the SDRAM image, clears .bss and calls the two entries, each on its own
 * stack. Addresses come from port.ld. The table's layout is the contract with the loader. */
#include "fx.h"
#include "video.h"
#include "smoke68k.h"                   /* the 68K smoke program and its picture (md/smoke68k.c) */

extern uint32_t lram_load[], lram_start[], lram_end[], lbss_start[], lbss_end[], lstack_primary[], lstack_secondary[], lborrow_start[], lborrow_end[];
unsigned intro_run(void);
void s_main(void);
void video_port_clock(volatile uint32_t *counter);
unsigned launcher_main(volatile uint32_t *vints);
void launcher_second(void);

volatile uint16_t fx_job_word;

const uint32_t launcher_head[13] __attribute__((section(".lhead"), used)) = {
	0x4C505247u,                        /* "LPRG" */
	(uint32_t)launcher_main,            /* primary: unsigned (*)(volatile uint32_t *vblank_counter): region | the sheet's answer (video.h) */
	(uint32_t)launcher_second,          /* secondary: void (*)(void), returns when the primary is done */
	(uint32_t)lram_load,                /* SDRAM image: where it sits in the page, */
	(uint32_t)lram_start,               /* where it goes, */
	(uint32_t)lram_end,                 /* and where it ends */
	(uint32_t)lbss_start, (uint32_t)lbss_end,
	(uint32_t)lstack_primary, (uint32_t)lstack_secondary,
	(uint32_t)lborrow_start, (uint32_t)lborrow_end,     /* the game's SDRAM this program uses: zero before, zeroed after */
	(uint32_t)smoke68k,                 /* for the port's 68K: md_src/launcher_smoke.c runs it in place */
};

unsigned launcher_main(volatile uint32_t *vints) {
	video_port_clock(vints);
	unsigned choice = intro_run();
	FX_JOB = CMD_QUIT;
	while (FX_JOB != 0) ;
	return choice;
}

void launcher_second(void) { s_main(); }
