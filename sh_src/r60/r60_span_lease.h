#ifndef R60_SPAN_LEASE_H
#define R60_SPAN_LEASE_H
#include <stdint.h>
/* Single-owner stall watchdog. Progress is published only after completed
 * packet work, never by an interrupt heartbeat. Neither counter needs a reset
 * shared with the producer. Unsigned subtraction handles V-int wrap. */
struct r60_span_lease { uint16_t progress, last_progress_v; };
static inline void r60_span_open(struct r60_span_lease *s, uint16_t progress,
                                 uint16_t now)
{
    s->progress = progress; s->last_progress_v = now;
}
static inline unsigned r60_span_expired(struct r60_span_lease *s,
    uint16_t progress, uint16_t now, unsigned budget)
{
    if (progress != s->progress) {
        s->progress = progress; s->last_progress_v = now;
        return 0;
    }
    return (uint16_t)(now - s->last_progress_v) >= budget;
}
#endif
