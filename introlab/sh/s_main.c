/* introlab secondary SH-2: draws the lower half of whatever frame the primary is building.
 * The primary posts the kind of frame on COMM4; this CPU does its share and answers 0. */
#include "fx.h"
#include "cabinet.h"
#include "doors.h"
#include "crowd.h"

void s_main(void) {
	for (;;) {
		crowd_poll();
		switch (FX_JOB) {
		case CMD_CABINET: cabinet_secondary(); FX_JOB = 0; break;
		case CMD_DOORS:   doors_secondary();   FX_JOB = 0; break;
#ifdef PORT
		case CMD_CROWD_ON:  crowd_begin(); FX_JOB = 0; break;
		case CMD_CROWD_OFF: crowd_end();   FX_JOB = 0; break;
		case CMD_QUIT:    crowd_end(); FX_JOB = 0; return;
#endif
		default: break;
		}
	}
}

#ifndef PORT
/* mars_start.s vectors the DMA1-complete interrupt here; the lab plays no PCM. */
void amb_dma_handler(void) { }
#endif
