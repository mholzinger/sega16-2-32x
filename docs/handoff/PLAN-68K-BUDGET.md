# The 68000 budget, now that the number is right

LOOP-DECOMPILE 87-88 re-measured the 68K with MAME's collapsed loops
expanded. The old figures were 2.5x low and the "fourfold headroom"
conclusion went with them. This is what the corrected numbers say to do.

---------------------------------------------------------------------
## THE ARITHMETIC

Everything below is measured, level 1, MAME, 20 frames, same input script.
`rom/night/vi39.32x` for ours.

    one vint at 7.670 MHz                     127,841 cycles
    our measured mix                             10.29 cycles/instruction
    so one vint holds about                     12,420 instructions

    our rom TODAY, per vint
      game (rebased 0x9xxxxx)                    4,904
      shim / RAMCODE (0xFFxxxx)                  4,733
      the frame wait                             2,781
      total                                     12,418   <- the vint is full

The game advances once per two vints today, so **per GAME FRAME** it costs
9,808 game instructions and 9,466 shim.

    at 60 Hz one game frame must fit in ONE vint:
      game, per game frame                       9,808
      shim, running once instead of twice        4,733
      needed                                    14,541 instructions
      available                                 12,420
      ------------------------------------------------
      THE GAP                                    2,121 instructions

**Two thousand one hundred instructions a vint is the whole 60 Hz gap on
the 68000.** Not a factor, a percentage: 17%.

---------------------------------------------------------------------
## LEVER 1 — three instructions are a third of the shim

Shim cost per vint by 256-byte block, from the same trace:

    0xFF0D00   876      0xFF0E00   230
    0xFF0900   781      0xFF0F00   222
    0xFF0A00   658      0xFF0B00   157
    0xFF1200   505      0xFF1100   150
    0xFF0100   268      others     886
                                  -----
                                  4,733

and three single PCs carry 1,607 of that:

    0xFF0964   644 / vint
    0xFF0DD0   493 / vint
    0xFF1224   470 / vint

Three tight loops, 34% of the shim, **76% of the whole 60 Hz gap.** Map
them with your own build's `rom/md_start.lst` — I did not attribute them
to symbols because I could not prove that map matches vi39, and guessing
which routine a hot address belongs to is how a session gets spent on the
wrong loop.

## LEVER 2 — our game side costs 20% more than the arcade's

    arcade, per game frame     8,184 work instructions
    ours,   per game frame     9,808
    difference                 1,624

The same game code, 20% more instructions. Worth knowing why before
optimising anything else: if the game is re-running part of its frame
because of how the port releases it, that is 1,624 instructions for a
protocol change rather than a rewrite. LOOP29 182-184 already found the
game DISCARDS a release that arrives while it works.

**Either lever alone nearly closes a 2,121 gap. Both would clear it.**

---------------------------------------------------------------------
## WHAT THIS PLAN ASSUMES, SO IT CAN BE CHECKED

  1. **10.29 cycles per instruction** is our current mix INCLUDING the
     frame wait. At 60 Hz there is no wait, and work instructions may be
     dearer than wait instructions. If the true work mix is 12 cycles, the
     gap widens to about 3,700. **Measure cycles, not just instructions,
     before trusting the margin.**
  2. **MAME's 32X models the 68K honestly** (CLAUDE.md) but not the SH-2.
     Nothing here depends on the SH-2 side.
  3. **Level 1 only.** Level 4 is lighter on the 68K (LOOP-DECOMPILE 87),
     so level 1 is the right case to size against.
  4. The arcade comparison uses the arcade's own attract-driven script,
     ours uses the same script under the port. Same script, different
     machines.

## THE ORDER I WOULD DO IT IN

  1. Map the three PCs to routines with your build's own `md_start.lst`.
  2. Measure CYCLES per vint, not instructions, so the margin is real.
  3. Cut the largest of the three and re-trace. The rig is
     `tools/round_profile.lua` plus the fixed `tools/arcade_trace.py`;
     both now expand collapsed loops.
  4. Only then look at lever 2, because it is a protocol question and
     will take longer than a loop.
