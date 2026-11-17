#ifndef R60_COMM_ARBITER_H
#define R60_COMM_ARBITER_H
#include <stdint.h>
/* Single-master policy/FIFO; caller supplies storage and performs MMIO. There
 * are no addresses, globals, waits or cross-CPU atomicity claims here. The
 * native producer can capture at most one command per completed tick. Full
 * returns backpressure, never an overwrite. full_count counts refused attempts,
 * not lost commands. The caller must retain its source latch on refusal.
 * Explicit native RESET may cancel an UNaccepted source latch; it must not
 * clear this accepted FIFO. Playback is independent of command arbitration. */
#define R60_COMM_CAPACITY 32u
#define R60_COMM_PRIORITY_TICKS 8u
enum {
    R60_COMM_MAP=1u,
    R60_COMM_HOLD=2u,
    R60_COMM_AUDIO=4u
};
typedef struct {
    uint8_t queue[R60_COMM_CAPACITY];
    uint32_t read,write;                 /* monotonic FIFO cursors */
    uint32_t highwater,full_count;       /* diagnostics; full never overwrites */
    uint32_t wanted;                     /* mapper page requested by the game */
    uint32_t priority_tick,priority_valid;
    uint32_t audio_tick,audio_valid;     /* at most one audio post per native tick */
    uint32_t wait_ack_vint,wait_busy_vint,wait_valid;
    uint32_t budget_yields,fair_audio_owed;
} r60_comm_state;
typedef char r60_comm_state_budget[(sizeof(r60_comm_state)<=96u)?1:-1];
static inline uint32_t r60_comm_depth(const volatile r60_comm_state *s)
{
    return s->write-s->read;
}
static inline void r60_comm_inc(volatile uint32_t *n)
{
    if (*n!=UINT32_MAX) ++*n;
}
static inline void r60_comm_init(volatile r60_comm_state *s,unsigned current)
{
    unsigned i;
    for (i=0;i<R60_COMM_CAPACITY;i++) s->queue[i]=0;
    s->read=s->write=s->highwater=s->full_count=0;
    s->wanted=current;
    s->priority_tick=s->priority_valid=0;
    s->audio_tick=s->audio_valid=0;
    s->wait_ack_vint=s->wait_busy_vint=s->wait_valid=0;
    s->budget_yields=s->fair_audio_owed=0;
}
static inline unsigned r60_comm_push(volatile r60_comm_state *s,uint8_t command)
{
    uint32_t depth=r60_comm_depth(s);
    if (depth>=R60_COMM_CAPACITY) {
        r60_comm_inc(&s->full_count);
        return 0;
    }
    s->queue[s->write&(R60_COMM_CAPACITY-1u)]=command;
    ++s->write;
    if (++depth>s->highwater) s->highwater=depth;
    return 1;
}
static inline unsigned r60_comm_peek(const volatile r60_comm_state *s,uint8_t *command)
{
    if (!r60_comm_depth(s)) return 0;
    *command=s->queue[s->read&(R60_COMM_CAPACITY-1u)];
    return 1;
}
/* Caller additionally requires mail0, AUDIO plan and a native completion;
 * idle polling must never flush audio. Check before the MMIO store. */
static inline unsigned r60_comm_can_audio(const volatile r60_comm_state *s,uint32_t tick)
{
    return r60_comm_depth(s) && (!s->audio_valid || s->audio_tick!=tick);
}
/* Call ONLY after posting the peeked command, exactly once. */
static inline unsigned r60_comm_audio_posted(volatile r60_comm_state *s,uint32_t tick)
{
    if (!r60_comm_can_audio(s,tick)) return 0;
    ++s->read;
    s->audio_tick=tick;
    s->audio_valid=1;
    s->fair_audio_owed=0;
    s->priority_valid=0;
    return 1;
}
static inline void r60_comm_set_want(volatile r60_comm_state *s,unsigned wanted,
    unsigned current,uint32_t tick)
{
    (void)tick;
    if (s->wanted!=wanted || wanted==current) s->priority_valid=0;
    s->wanted=wanted;
}
/* No pending map may be overwritten. Caller handles an in-flight mapper
 * first, commits only after its ACK, then calls map_committed. With no pending
 * map, plan grants one map transaction priority, then one queued audio grant.
 * Eight slots of FIFO headroom cover an eight-native-tick drain interval.
 * If insufficient, continue current-page fallback/audio instead of freezing.
 * On budget expiry yield ONE audio grant and retry; do not discard intent or
 * accepted audio. External missing ACK/reader progress is an explicit failed
 * deadline, never guaranteed completion. Each call is finite and nonblocking. */
static inline unsigned r60_comm_plan(volatile r60_comm_state *s,unsigned current,uint32_t tick)
{
    uint32_t depth=r60_comm_depth(s);
    if (s->wanted==current) {
        s->priority_valid=0;
        return R60_COMM_AUDIO;
    }
    if (s->fair_audio_owed && depth) return R60_COMM_HOLD|R60_COMM_AUDIO;
    if (!s->priority_valid) {
        if (depth>R60_COMM_CAPACITY-R60_COMM_PRIORITY_TICKS)
            return R60_COMM_AUDIO;
        s->priority_tick=tick;
        s->priority_valid=1;
    }
    if (tick-s->priority_tick>=R60_COMM_PRIORITY_TICKS && depth) {
        s->fair_audio_owed=1;
        s->priority_valid=0;
        r60_comm_inc(&s->budget_yields);
        return R60_COMM_HOLD|R60_COMM_AUDIO;
    }
    return R60_COMM_HOLD|R60_COMM_MAP;
}
static inline void r60_comm_map_committed(volatile r60_comm_state *s)
{
    s->priority_valid=0;
    s->fair_audio_owed=r60_comm_depth(s)!=0;
}
/* Return1 once per VInt/kind; idle retries must not inflate wait counters.
 * kind0 is ACK wait, kind1 quiescent/resource wait. */
static inline unsigned r60_comm_wait_first(volatile r60_comm_state *s,unsigned kind,uint32_t vint)
{
    volatile uint32_t *last;
    uint32_t bit;
    if (kind>1u) return 0;
    last=kind?&s->wait_busy_vint:&s->wait_ack_vint;
    bit=1u<<kind;
    if ((s->wait_valid&bit) && *last==vint) return 0;
    *last=vint;
    s->wait_valid|=bit;
    return 1;
}
#endif
