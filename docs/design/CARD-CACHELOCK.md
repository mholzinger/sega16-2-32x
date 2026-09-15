# CARD CACHELOCK: 2 KB of locked, zero-wait instruction memory

Derived 2026-09-15 from `srcref/S32X_MiSTer/rtl/SH/SH7604/CACHE.sv`.
Not built. Not measured on the rig. The RTL is a model of the SH7604 and
the technique it implements is documented SH-2 (cache address array,
cache data array, two-way mode), so the two corroborate; real silicon is
the rig's to confirm.

## The measurement that motivates it

`ares-headless --profile`, master SH-2, steady state (the difference of a
1800-frame and a 2000-frame run, so boot and scene-load code are excluded):

    master instructions        60,056,904 over 200 frames
    working set                21,936 B   (1,371 distinct 16 B lines)
    instruction cache           4,096 B
                            -> 5.4x oversubscribed

    50% of all work in    240 B
    75% of all work in    928 B
    90% of all work in  2,480 B
    95% of all work in  3,840 B
    99% of all work in  8,688 B

The hot core is tiny and the tail is enormous. Nothing can be pinned in a
4-way LRU cache, so the tail evicts the core continuously.

## What the RTL says

Geometry (`CACHE.sv:143-151`, `reg [5:0] LRU [64]`): 64 sets, 4 ways,
16 B lines = 4,096 B; set index is `CBUS_A[9:4]`, so the set pattern
repeats every 1,024 B.

Three facts make the lock possible:

1. **TW freezes ways 0 and 1.** `WayFromLRU(lru, two_way)` (`CACHE.sv:74-89`)
   returns only `4'b0100` or `4'b1000` when `two_way` -- ways 2 and 3.
   With `CCR.TW` set, ways 0 and 1 are never chosen for replacement.

2. **TW does NOT affect hit detection.** `WAY_HIT[0..3]` (`CACHE.sv:148-151`)
   is a tag+valid compare across all four ways unconditionally. A fetch
   that matches a tag in way 0 hits, TW or not.

3. **Both arrays are directly addressable.**
   - data array: `CACHE_DATA_AREA = (CBUS_A[31:29] == 3'b110)` -> 0xC0000000,
     way = `CBUS_A[11:10]`, longword = `CBUS_A[9:2]` (`CACHE.sv:54,318-323`)
   - address (tag) array: `CACHE_ADDR_AREA = 3'b011` -> 0x60000000, with the
     way taken from `CCR.W` (`CACHE.sv:53,291-294,334`)

So: write the hot code into ways 0/1 through 0xC0000000, write matching
tags with the valid bit through 0x60000000, set TW, and 2,048 bytes are
resident and hit forever while every other fetch churns ways 2 and 3.

## This reverses yesterday's TW reading

LOOP29 measured TW at 1.48x COST and read it as "half the cache hurts".
That is right for TW alone -- it halves the cache without putting
anything in the frozen half. It is also the ablation check passing: the
1.48x IS ways 0/1 ceasing to be replaced. Preloaded first, the same bit
is the lock rather than the loss.

## The trap: CP wipes it

`CCR.CP` zeroes all four ways and the LRU (`CACHE.sv:297-303`) -- not just
the replaceable ones. This codebase purges at every window start, so a
lock would be destroyed roughly three times a generation.

Two ways out, and the card must pick one before it is built:
  a. re-establish after each purge: 512 longword data writes + 128 tag
     writes = ~640 stores, order 1.5% of a vint at 3 purges/generation
  b. drop the purge by moving the shared-data reads it protects onto the
     uncached 0x20000000 aliases, many of which this code already uses

## What it is worth

75% of the master's steady-state instruction fetches live in 928 B and
90% in 2,480 B, against 2,048 B of lock. The upper bound is therefore
somewhere near "most of the master's instruction fetches never miss".
What a miss actually costs cannot be measured on ares -- it charges
instruction cycles only -- so the size of the win is a rig question and
this card must be ranked there, on the flip rate, never on ares.
