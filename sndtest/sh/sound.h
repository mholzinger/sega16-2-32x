/* sndtest PCM lane — SH-2 PWM voice-pool mixer (docs/sound/SOUND.md P1).
 * Secondary-side lifecycle + the primary's trigger API. */
#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

#define SND_MAX_VOICES 4

/* secondary */
void snd_init(void);          /* once, at secondary boot */
void snd_pump(void);          /* idle loop + any long-work checkpoints */
void amb_dma_handler(void);   /* from the DMA1 IRQ (mars_start.s) */

/* either CPU (cache-through mailbox; primary is the intended caller) */
void sfx_play(uint8_t voice, uint8_t sample, uint8_t vol, uint8_t loop);
void sfx_stop(uint8_t voice);
void sfx_stop_all(void);

/* diagnostics */
uint16_t snd_underruns(void);
uint8_t  snd_active_mask(void);

#endif
