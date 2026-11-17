/* sndtest PCM lane — SH-2 PWM voice-pool mixer (docs/sound/SOUND.md P1).
 * Secondary-side lifecycle + the primary's trigger API. */
#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

#define SND_MAX_VOICES 4

/* samples per DMA side (two sides, ping-pong). 1024 = 64 ms, the proven shape for the voice
 * pool; the synth lane (OPM_LANE) doubles it: the engine's busiest 64 ms of Rise cost 65 ms
 * on ares (2026-10-31) and the ring must cover peaks, not the average. The lane's landing
 * arithmetic (opm_lane.c) reads it too. */
#ifdef OPM_LANE
#define SND_BUF_SAMPLES 2048
#else
#define SND_BUF_SAMPLES 1024
#endif

/* secondary */
void snd_init(void);          /* once, at secondary boot */
void snd_pump(void);          /* idle loop + any long-work checkpoints */
void amb_dma_handler(void);   /* from the DMA1 IRQ (mars_start.s) */

/* either CPU (cache-through mailbox; primary is the intended caller) */
void sfx_play(uint8_t voice, uint8_t sample, uint8_t vol, uint8_t loop);
void sfx_stop(uint8_t voice);
void sfx_stop_all(void);

/* diagnostics */
uint8_t  snd_fill_pending(void);   /* a drained side awaits its fill (the lane's trigger reads it) */
uint16_t snd_underruns(void);
uint8_t  snd_active_mask(void);
uint16_t snd_late(void);
uint16_t snd_fill_peak(void);
uint16_t snd_fill_avg(void);

#endif
