/* sndtest secondary SH-2 — the PCM lane's home (docs/sound/SOUND.md P1). The
 * voice-pool mixer (sound.c) owns this CPU: init once, then pump
 * forever. amb_dma_handler lives in sound.c, reached via the DMA1 IRQ
 * chain in mars_start.s.
 *
 * NOPWM probe hook: `make sndtest SNDEXTRA='-O2 -fomit-frame-pointer
 * -DNOPWM'` builds the lane fully disabled — used to isolate
 * boot-time audio artifacts to the PWM engine vs the MD side. */
#include "sound.h"

void s_main(void)
{
#ifndef NOPWM
	snd_init();
	for (;;)
		snd_pump();
#else
	for (;;)
		;
#endif
}
