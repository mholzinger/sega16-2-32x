#ifndef R60_GAME_WORKINGSET_H
#define R60_GAME_WORKINGSET_H
#include <stdint.h>
#include "r60_altbeast_events.h"
/* Game policy only. Input starts at 68K FFC000; mapper/page numbers belong to
 * the generic renderer. Mode1 includes complete entrance and death sequences. */
static inline unsigned r60_game_workingset(const volatile uint8_t *wram)
{
    return r60_ab_event_class((const volatile uint16_t *)(wram+0x1400u),1u)!=0u;
}
#endif
