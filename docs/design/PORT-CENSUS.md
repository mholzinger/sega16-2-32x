# THE PORT CENSUS — what moving the game to the SH-2 actually costs (2026-09-25)

Mike's question: "unless we port the 68K code to our SH-2 we might never get
arcade parity." This is the measurement, from `tools/vint_census.py` over
`roms/altbeast/prog68k.asm`. Reproduce with `python3 tools/vint_census.py`.

## The numbers

    program disassembled            92,681 instructions
    direct call edges                6,158
    register-indirect call sites        78   the object-handler dispatch
    pointer-table targets recovered    249

    REACHED from the two entry points (IRQ4 vector -> 0x2AAC, reset -> 0x40E):
      direct edges only                846 instructions,   2,888 bytes
      plus pointer-table targets    25,284 instructions,  88,534 bytes

    THE STATE that must be reachable by whichever CPU runs the logic:
      FFC000-FFE000   8,192 B   object table, 64 slots x 128 B
      FFE000-FFF000   4,096 B   secondary object/sprite tables
      FFF000-FFF300     768 B   game globals (scene timer, scroll, camera)
      FFF300-FFF400     256 B   palette-cycle slot timers
      FFF400-FFF600     512 B   palette slot allocator
      FFF800-FFFC84   1,156 B   misc / high globals
      FFFF00-FFFF84     132 B   top-of-RAM globals
      TOTAL          15,112 B   (14.8 KB)

    plus FFF600-FFF800 (512 B, the 64-entry palette queue), which shows 0
    static references because it is reached only through a pointer at
    0xFFFFF402 -- a known undercount of this method. Call it ~15.6 KB.

    Cross-check: the builder's WRAMCODE exactness test compares "the 16 KB of
    game WRAM below the stack" (LESSONS, wc8). Two independent directions agree
    on ~15-16 KB.

    ACCESS DENSITY of the reached set:
      43.7% of reached instructions reference memory
      83.9% of those are register-indirect (an object walk through a4/a6)
      at 8,184 WORK instructions a vint -> ~3,573 state accesses a vint
      (2,858-4,287 allowing for path weighting)

## The correction

An earlier answer in this thread said do not port, on the grounds that the
SH-2s cannot see 68000 work RAM and the only shared memory is the ~58 KB
framebuffer hole, so the game's state would have to move there and be fought
over. **That reasoning applies to a PARTIAL port and not to a full one.**

    PARTIAL port (some routines on the SH-2, some on the 68K)
        Shared state must live in the framebuffer hole. 14.8 KB fits, but the
        hole IS the framebuffer: it is FM-arbitrated, and 68K writes at FM=1
        are dropped (LESSONS, the flip-latch entry). The 68K would be locked
        out of its own object table for the ~106 lines a vint the master holds
        FM to compose. ~3,573 accesses a vint cannot be scheduled around a
        106-line blackout. **This is the option that does not work.**

    FULL port (all game logic on the SH-2)
        State goes to SH-2 SDRAM. Private, uncontended, no sharing required.
        The framebuffer constraint does not arise. **The 14.8 KB is not a
        blocker for this option, and the earlier answer was wrong about it.**

## What a full port deletes

This is the part that makes it worth pricing rather than dismissing.
`CLAUDE.md` states the scope: "The only thing holding up parity is the
pipeline: feeding frames and sprites into the 32X at the right frequency. It
costs 2,882 instructions/vint today -- as much as the game itself."

A full port deletes the pipeline, because there is nothing to feed: the code
that decides what to draw would already be running on the CPU that draws.
Gone with it are the R60 packet build and parse, the DREQ landing risk, the
framebuffer staging, the lost-push belt, and the 68K's IRQ4 chain.

The 68K keeps real work: the MD VDP (both scroll planes are on it, STATE
SHIPPED), the MD sprite path, the i8751 shim, and sound.

## What it costs

    to translate      25,284 reached instructions / 88,534 bytes of 68K.
                      This is the STATIC reachable set -- all five rounds and
                      the attract, not one vint -- so it is the porting
                      surface, not the per-frame cost.
    to reproduce      78 register-indirect call sites and 249 pointer-table
                      entries: the object dispatcher's whole mechanism.
    the fidelity bar  Standing rule 1 is accuracy before speed. A translated
                      routine that drifts is a regression, and 25,284
                      instructions is a large surface to prove. The precedent
                      is `tools/wram_reloc.py`, which verifies every relocated
                      block instruction-by-instruction against objdump -- but
                      relocation preserves the instruction stream and
                      translation does not.

## What is NOT established here

  - Whether the SH-2 is fast enough. It is not in doubt on clock, but the
    game's 68K code is written for a 68000's addressing modes and the
    translation's instruction count is unmeasured.
  - Whether a mechanical 68K -> SH-2 translator can meet the fidelity bar, or
    whether the ~200 named routines must be hand-ported.
  - The sound and MCU boundary: the shim currently replicates the i8751
    mailbox for a 68K caller.

## The recommendation

**Do not start a full port as the next card, and do not close the option.**
ares is at 71% single-vint on the level-1 demo; the gap to 100% is 30-60 lines
on a ~240-line pass, and the FPGA's 50% is substantially the MiSTer core's ROM
wait, which a real cartridge does not have. The levers on the board can still
reach the bar, and a port is a rewrite of the game with a fidelity surface 25k
instructions wide.

**The condition that should change that:** if the remaining levers leave ares
below ~85% single-vint, the pipeline's 2,882 instructions/vint become the
larger prize and this census says the memory does not stand in the way.

---

# THE TRANSLATION RATIO — measured on one routine (2026-09-25)

`collision_detection` (68K 0xD6D8-0xD73C, 32 instructions, 100 bytes) hand
translated to SH-2 in `docs/design/collision_sh2.s`. Counted, not estimated.

## Code size

    68K                              32 instructions   100 bytes
    SH-2, literal translation        59 instructions   118 bytes  1.84x / 1.18x
    SH-2, idiomatic                  54 instructions   108 bytes  1.69x / 1.08x

The literal version keeps the arcade's struct layout and control flow
instruction for instruction. The inflation is not the ISA being weaker; it is
three specific things:

  * **SH-2's byte displacement is 4 bits.** `tst.b 73(a4)` is one 68K
    instruction; on SH-2 offset 73 is out of range, so it becomes
    `mov #73,r0` + `mov.b @(r0,r4),r1` + `tst r1,r1` — three. This happens at
    five sites.
  * **`dbf` is one instruction; `dt` + `bf` is two.**
  * **Non-leaf routines must save PR** (`sts.l pr,@-r15` / `lds.l @r15+,pr`),
    where the 68K's `bsr` stacks the return address itself.

The idiomatic version changes ONE thing, and it is a thing a port controls:
`attack_active` moves from struct offset 73 to offset 9, inside the 4-bit
displacement. That removes the indexed load at all five sites. **The arcade's
struct layout is an input to the 68K's addressing modes; a port gets to choose
its own.**

## Wall time, which is the actual question

Executed per call, with the 24-iteration enemy loop: 68K 146 instructions,
SH-2 186 (1.27x — the loop body translates better than the static count
suggests).

    68K at 12 cyc/instr, 7.67 MHz                     228.4 us

MEASURED ON THE RIG 2026-09-26 (`make line WRAMCODE=1 SH2BENCH=1 RIGBARCODE=1
RIGBCEVERY=8`, two launches, 16 decoded barcodes). The assumed row is kept
beside the measured one because the assumption was wrong at BOTH ends:

                                     assumed     MEASURED (FPGA)
    cache-resident (register ALU)      1.5        1.125    9.1 us   25.1x
    all-miss (SDRAM, stride 128)       4.0        5.125-8.75
                                                  41.4-70.8 us  5.5-3.2x

`1.125` is the floor and it is SOLID: byte 5 read 9 in every one of the 16
samples, across two launches. The all-miss figure is a RANGE because it moves
with bus contention (41, 42, 52, 55, 59, 61, 65, 69, 70 in eighths), which is
itself the finding -- the miss case is not a constant.

So the port's floor is BETTER than assumed (25x, not 18.8x) and its ceiling is
WORSE (3.2x, not 7.1x). The margin survives either way, but it is half what
this document claimed at the pessimistic end, and the spread between the two
ends is now 8x rather than 2.7x. **Which end a routine lands on is therefore
the design question, and it is a cache question.**

Ares, run as the control on the same rom, reads 1.000 and 4.000: near-exact on
the cache-resident loop and ~2x optimistic on the miss-heavy one. That is
"ares charges INSTRUCTION COUNT" (LESSONS) with a number on it.

**12 cyc/instr for the 68K is the project's own measured figure** (arcade 11.7,
our allowance 13.3; LOOP-DECOMPILE 88).

So: **the code grows about 1.7x and runs 7-19x faster.** Even the pessimistic
end is a 7x margin, and the routine translated here is a branch-and-load one,
the kind that flatters the 68K.

## One design input the port would get for free

The object table's stride is 128 bytes. The SH-2 cache is 64 sets x 4 ways x
16-byte lines, indexed by A[9:4] (`CACHE.sv:130-143`), so a 128-byte stride
advances the set index by 8:

    64 slots at stride 128  ->   8 distinct sets, 32 lines of capacity
    stride 144              ->  64 distinct sets, 256 lines of capacity

**At the arcade's 128-byte stride, half the object table structurally cannot be
cache-resident** — 64 slots contend for 32 lines. Re-striding to 144 bytes
gives every slot its own set. The 68K does not care (it has no cache); only a
port can take this, and it is 8x the cache coverage for 1 KB of padding.

**AND THE PRICE IS NOW MEASURED.** The SH2BENCH miss loop above IS this access
pattern -- 512 reads at stride 128, touching 8 sets, contending for 32 lines --
and on the FPGA it costs 5.125-8.75 cycles per instruction against 1.125 for
the same loop shape running cache-resident. That is a 4.6x to 7.8x penalty
bought by nothing but the stride. The 1 KB of padding is the cheapest thing in
this document.

## What this changes about the recommendation

THE BLOCKER IS CLEARED (2026-09-26): the figure is measured, and it does not
overturn the recommendation -- even the all-miss end is 3.2x faster than the
68K. What it changes is WHERE THE RISK IS. It is no longer "is the SH-2 fast
enough"; it is "does the ported routine's data stay in cache", which is a
layout decision the port makes for itself. The stride note below is the first
instance and it is worth 4.6-7.8x on its own.

Superseded reasoning, kept so the change is visible: the SH-2
cycles-per-instruction figure was assumed, and it was the whole ratio. **The
next measurement, if the port is
ever costed seriously, is that figure on the rig** — not another translation.

The verdict above stands: not next, not closed. ares is at 71% single-vint and
the gap is 30-60 lines. But the ratio is now a number rather than a worry, and
it is favourable: a full port's risk is FIDELITY across 25,284 instructions,
not performance.
