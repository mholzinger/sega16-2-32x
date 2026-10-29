/* F0(c) SYNTHETIC PORT LOAD (docs/design/FULL-PORT.md, 2026-09-26).
 *
 * THE QUESTION: the master is the renderer; can the translated game's
 * ~10,400 SH-2 instructions a vint live on the slave, on the master, or must
 * the work be split? "Put it on the idle slave" is exactly the assumption
 * SELFCHAIN falsified -- four ways of filling the slave's frame-body idle all
 * rejected 10-14% against the master-chain's 2.0, because that idle is
 * load-bearing bus quiet the compose depends on. So this is a measurement.
 *
 * THE SHAPE MATTERS MORE THAN THE COUNT. FULL-PORT (b): a 15.6 KB working set
 * against the SH-2's 4 KB cache is why the ALL-MISS figure is the planning
 * number, not the cache-resident one. So the load walks a 16 KB window one
 * cache line at a time -- 4x oversubscribed, the same shape CACHELOCK is in
 * MEASURED DEAD for ("2 KB against a 14 KB working set"). An issue-rate loop
 * would answer a question nobody asked.
 *
 * EXACTLY 5 INSTRUCTIONS AN ITERATION (load, stride, wrap, dt, branch) x 2080
 * = 10,400 a vint. Verify against the disassembly before believing any number
 * that comes out of it -- SH2BENCH's counts were checked the same way.
 *
 * Reads only, from SDRAM 0x06020000-0x06023FFF, so it cannot disturb state.
 * The mask works because 0x06023FFF covers every bit the base sets: adding 16
 * walks, and 0x06024000 & mask wraps back to 0x06020000.
 */
#ifndef PORT_LOAD_H
#define PORT_LOAD_H

#ifndef PL_ITERS
#define PL_ITERS  2080u                  /* x5 instructions = 10,400 a vint */
#endif
#define PL_BASE   0x06020000u
#define PL_MASK   0x06023FFFu            /* 16 KB window */

static inline void port_load(void)
{
    uint32_t c = PL_ITERS, p = PL_BASE, tmp;
    uint32_t stride = 16u, mask = PL_MASK;   /* one cache line a step */
    __asm__ __volatile__(
        "1:\n\t"
        "mov.l @%1, %2\n\t"
        "add %3, %1\n\t"
        "and %4, %1\n\t"
        "dt %0\n\t"
        "bf 1b"
        : "+r"(c), "+r"(p), "=&r"(tmp)
        : "r"(stride), "r"(mask)
        : "t");
}
#endif
