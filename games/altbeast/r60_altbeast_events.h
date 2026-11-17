#ifndef R60_ALTBEAST_EVENTS_H
#define R60_ALTBEAST_EVENTS_H
/* Game adapter only: handler names are original S16 addresses. The generic
 * meter/transport contains no Altered Beast state or memory-map knowledge.
 * mode1 groups entrance/materialization and death/followup; mode2 isolates
 * the two zoomed cloud handlers. Status and handler are sampled on the master
 * immediately after a completed game tick, never by the rendering CPU. */
static inline unsigned r60_ab_event_class(const volatile uint16_t *boss, unsigned mode)
{
    uint32_t handler;
    if (!(boss[0] & 0x8000u)) return 0u;
    handler = ((uint32_t)boss[1] << 16) | boss[2];
    if (handler == 0x1794Cu || (mode == 1u && handler == 0x16D7Eu)) return 1u;
    if (handler == 0x17B54u || (mode == 1u && handler == 0x17B88u)) return 2u;
    return 0u;
}
#endif
