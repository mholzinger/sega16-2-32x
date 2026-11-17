#ifndef R60_COMM_ADAPTER_H
#define R60_COMM_ADAPTER_H
#include "r60_comm_arbiter.h"
#if defined(R60_MASTERCHUNK) || defined(R60_BAKEARTCHECK) || defined(R60_PLOT5CHECK) || defined(R60_ZOOMCHECK) || defined(R60_PUNCH2CHECK) || defined(R60_MASKBLOCK) || defined(R60_ARTCACHE) || defined(R60_MBAND) || defined(R60_PRIOMASK) || defined(R60_PRIOROW)
#error "Working-set mailbox scratch conflicts with this experiment"
#endif
/* Master-only, uncached; SPRPREP ends at3E800. No slave accesses this FIFO. */
#define R60_COMM ((volatile r60_comm_state *)0x2603EB00u)
/* Master-only latest-policy latch: idle may retry MAP, never AUDIO. */
#define R60_COMM_IDLE_MAP (*(volatile uint32_t *)0x2603EB60u)
typedef char r60_comm_scratch_budget[(sizeof(r60_comm_state)<=256u)?1:-1];
#endif
