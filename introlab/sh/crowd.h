/* The launcher's crowd (crowd.c): a loop of murmur on the 32X's PWM output. Port only. */
#ifndef CROWD_H
#define CROWD_H
#include "fx.h"
#ifdef PORT
extern uint8_t crowd_on;                 /* the secondary's own: it runs the loop */
void crowd_begin(void);                  /* on the secondary (CMD_CROWD_ON) */
void crowd_end(void);                    /* on the secondary (CMD_CROWD_OFF) */
void crowd_turn(void);
/* As often as the secondary can: the side being played has run out, turn to the other.
 * The test is "enabled and ended", which only the secondary's channel can be: this code is
 * also reached by the primary, whose channel 1 the launcher never enables. */
static inline void crowd_poll(void) {
	if (crowd_on && (SH2_DMA_CHCR1 & 3u) == 3u) crowd_turn();
}
#else
static inline void crowd_poll(void) { }
#endif
#endif
