#include <assert.h>
#include "../../sh_src/r60/r60_span_lease.h"
int main(void) {
 struct r60_span_lease s;
 r60_span_open(&s,10,100);
 assert(!r60_span_expired(&s,10,103,4));
 assert(r60_span_expired(&s,10,104,4)); /* a live IRQ clock cannot conceal a stall */
 r60_span_open(&s,10,100);
 assert(!r60_span_expired(&s,11,103,4));
 assert(!r60_span_expired(&s,12,106,4)); /* completed work, six total vints */
 assert(!r60_span_expired(&s,12,109,4));
 assert(r60_span_expired(&s,12,110,4)); /* recovery still bounded after last progress */
 r60_span_open(&s,65535,65534);
 assert(!r60_span_expired(&s,0,1,4)); /* both counters wrap independently */
 assert(!r60_span_expired(&s,0,4,4));
 assert(r60_span_expired(&s,0,5,4));
 r60_span_open(&s,0,0);
 for (unsigned i=1;i<=131072;i++) assert(!r60_span_expired(&s,(uint16_t)i,(uint16_t)i,2));
 assert(r60_span_expired(&s,0,2,2));
}
