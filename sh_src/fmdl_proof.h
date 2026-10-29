/* THE ARCHITECTURE, PROVED END TO END (2026-09-27, Mike: "a test that proves
 * our original architecture in pure simple clean code").
 *
 * WHAT WAS MISSING. TESTCART already timed the transport's three legs --
 * assemble, blast, lift -- but the 68K performed all three. Nothing anywhere
 * proved that the SH-2 LIFTS AND EXECUTES what the 68K staged, which is the
 * spine of this port: the 68K serialises a per-frame command list into the
 * framebuffer, hands the window over, and the SH-2 runs it.
 *
 * THE WHOLE PROTOCOL, and it is four steps:
 *
 *     FM=0   the 68K owns the FB window; it writes the list at 0x851A00
 *     FM=1   the 68K hands the window to the SH-2 and posts FMDL_GO
 *            the SH-2 lifts from 0x24011A00, executes, posts FMDL_DONE
 *     FM=0   the 68K takes the window back and reads the result
 *
 * The two sides see the SAME memory at different addresses -- 68K 0x840000 is
 * SH-2 0x24000000 -- and FM is the only thing that decides whose turn it is.
 * That handoff is the part no test covered and the part everything depends on.
 *
 * THE LIST FORMAT IS TWO WORDS A RECORD, terminated by 0xFFFF:
 *     word 0   destination, in FB words from 0x24000200 (the visible area)
 *     word 1   the value to store
 * Deliberately the simplest thing that is still a display list: the executor
 * below is six lines, so when the round trip fails the failure is in the
 * HANDOFF and not in an interpreter nobody wants to debug.
 */
#ifndef FMDL_PROOF_H
#define FMDL_PROOF_H

#define FMDL_LIST ((volatile uint16_t *)0x24011A00)   /* = 68K 0x851A00 */
#define FMDL_FB   ((volatile uint16_t *)0x24000200)   /* visible FB, word 0 */
#define FMDL_END  0xFFFFu
#define FMDL_MAX  256u                 /* refuse to run away on a bad list */

/* Returns how many records it executed, which the 68K checks against how many
 * it wrote. A count that is short says the list was truncated in transit; a
 * count that matches with wrong pixels says the WRITES did not land. Two
 * different failures, and the 68K can tell them apart from one number. */
__attribute__((section(".porttest"), noinline))
static uint16_t fmdl_exec(void)
{
    uint16_t n = 0;
    while (n < FMDL_MAX && FMDL_LIST[n * 2u] != FMDL_END) {
        FMDL_FB[FMDL_LIST[n * 2u]] = FMDL_LIST[n * 2u + 1u];
        n++;
    }
    return n;
}
#endif /* FMDL_PROOF_H */
