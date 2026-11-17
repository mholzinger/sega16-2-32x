/* The SH-2 synth lane: the YM2151's two overflow channels, synthesized on the secondary
 * SH-2 (sndtest/opm) and mixed into the PWM lane beside the YM2612's six (hybrid 3). */
#ifndef OPM_LANE_H
#define OPM_LANE_H
#include <stdint.h>

void    opm_lane_init(void);      /* secondary, once, from snd_init */
void    opm_lane_poll(void);      /* secondary, every pump: follows the 68K's COMM6 {count, cmd} */
int32_t opm_lane_sample(void);    /* one lane sample, 16-bit PCM scale (0 when idle) */
uint32_t opm_lane_cycles(void);   /* FRT ticks the engine took in the last fill (load meter) */
int32_t  opm_lane_landing(void);  /* the last start's landing in samples (+ = the rows waited) */

#endif
