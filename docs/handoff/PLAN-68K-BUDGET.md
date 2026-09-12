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

Priced in cycles rather than instructions (assumption 1 below, now
resolved) the gap is **19,135 cycles, or 1,898 instructions.**

**Under two thousand instructions a vint is the whole 60 Hz gap on the
68000.** Not a factor, a percentage: 15%.

---------------------------------------------------------------------
## LEVER 1 — r60_push IS the gap

**NAMED, 2026-09-12.** `rom/md_start.lst` does not match vi39 — the
embedded `md_start.bin` differs by 2453 bytes — so the current build was
snapshotted WITH its own map, verified byte-identical, and traced. Its
numbers are within 2% of vi39's and the hot addresses are the same, so the
layout is stable across both for these routines.

Shim cost per vint, by routine:

    r60_push                    2,621     54.5%
    r60_ship_words.isra.0         690
    r60_blast.constprop.0         512
    md_consume                    349
    shim_vblank                   324
    read_joypad                   115
    get_input                      80
    _vblank                        46
    md_to_arcade                   37
    everything else                39
                                -------
                                  4,813

**`r60_push` alone is 2,621 instructions a vint — 55% of the shim and MORE
THAN THE WHOLE 60 Hz GAP.** Halving it closes the budget by itself.

The three hottest instructions in the program, all loop heads:

    0xFF0964   644 / vint   r60_ship_words.isra.0+0x2C
    0xFF0DD0   517 / vint   r60_push+0x45C
    0xFF1224   472 / vint   r60_blast.constprop.0+0x4A

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

  1. ~~10.29 cycles per instruction includes the frame wait~~ **RESOLVED
     2026-09-12, and the margin holds.** The wait is `tst.b (xxx).W` +
     `beq.s` taken = 12 + 10 = 22 cycles for two instructions. At 2,736
     idle instructions a vint that is 1,368 iterations = 30,096 cycles, so
     WORK gets 97,745 cycles for 9,698 instructions = **10.08 cycles per
     work instruction** — marginally CHEAPER than the average, not dearer.
     In cycles the 60 Hz requirement is 14,581 instructions x 10.08 =
     146,976 against a 127,841 budget: **over by 19,135 cycles, which is
     1,898 instructions.** `r60_push` is 26,420 cycles.
  2. **MAME's 32X models the 68K honestly** (CLAUDE.md) but not the SH-2.
     Nothing here depends on the SH-2 side.
  3. **Level 1 only.** Level 4 is lighter on the 68K (LOOP-DECOMPILE 87),
     so level 1 is the right case to size against.
  4. The arcade comparison uses the arcade's own attract-driven script,
     ours uses the same script under the port. Same script, different
     machines.

## THE ORDER I WOULD DO IT IN

  1. ~~Map the three PCs~~ done: `r60_push`, `r60_ship_words`,
     `r60_blast`, in that order of cost.
  2. ~~Measure cycles~~ done: 10.08 per work instruction, margin holds.
  3. **Cut `r60_push`.** It is 2,621 instructions a vint against a gap of
     1,898, so it is not one contributor among several — it is the item.
     Its own hottest instruction is at +0x45C.
  4. Re-trace with the same rig and check the number moved.
     `tools/round_profile.lua` plus the fixed `tools/arcade_trace.py`,
     both of which now expand collapsed loops.
  5. Only then lever 2, because it is a protocol question and will take
     longer than a loop.
