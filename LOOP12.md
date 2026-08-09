# LOOP 12 — FINISH THE MD BACKGROUND (pivot slice 1c)

Kickoff for a FRESH session. Read this, then `ARCHITECTURE.md` sections
9-16 (the pivot's measured facts). `LOOP11.md` is history — its plan
sections are superseded by ARCHITECTURE.md, and several of its numbers
were corrected. **Where the two disagree, ARCHITECTURE.md wins.**

## THE JOB

`make MDBGALL=1` builds a port that moves the BG, FG cat-0 and text
layers off the 32X and onto the Mega Drive VDP. Everything in the path
is written and running. **The MD plane does not yet show the game's
background** — it shows horizontal bars of a wrong tile. Fix that, and
slice 1c is done.

Shipping build is untouched and stays that way: `rom/s16.32x`, parity
**24.26**, title 2.44, eyehold 3.37, `_end 0x06018d38`. All pivot code
is behind `#ifdef MD_BG` / `MD_BG_FG0` / `MD_BG_TEXT`.

## WHY THIS IS WORTH FINISHING (measured, not assumed)

    blit skips        base 23.4%   ->   pivot 0.2%
    composeL0         6792         ->   1494
    composeL1         5720         ->   0
    blit itself       719          ->   777   (unchanged)

Blit skips are the tearing: a skipped blit leaves a third of the screen
holding a stale band, and in stage 1 the bottom third is grass, which is
why it reads as *green* tearing. A 100x reduction kills it.

**It does NOT fix the framerate.** 20 Hz is 3 windows per frame because
a window can only ship 75 rows inside a 38-line vblank, and the blit is
unmoved. Going faster is a separate problem (more rows per vblank, or
stop shipping whole frames). Mike knows and wants this anyway.

## THE THREE SUSPECTS, RANKED

1. **`MD_BLANK_SLOT` is 1023, which the allocator can also hand out.**
   A cell that fails to allocate is indistinguishable from a cell whose
   code legitimately owns slot 1023. Reserve a slot the allocator never
   assigns. ~5 lines.
2. **Ordering.** The shipper alternates one 40-tile batch to four
   280-cell name-table chunks, so a chunk can reference a slot whose
   tile has not been uploaded yet. Early on most referenced slots hold
   nothing. Ship tiles ahead of the cells referencing them, or bias the
   schedule toward tile batches after a burst of new claims.
3. **Scroll sign/offset.** `-(vx0 & 7)` into the hscroll table and
   `+(vy0 & 7)` into VSRAM is a guess, never checked against a still
   frame.

## HOW TO BUILD AND LOOK

    make MDBGALL=1
    # headless capture -- video ON is far too slow with the pivot's
    # per-window work, and snapshots work fine with -video none
    SHOT_DIR=/tmp SHOT_TAG=x SHOT_EVERY=1200 SHOT_LAST=3600 \
      mame 32x -cart rom/s16.32x -rompath ./mame -skip_gameinfo \
      -video none -sound none -nothrottle -autoboot_script <grid.lua>

Counter dump (`diagdump.lua` pattern) reads DIAG from the master at
`0x06028000 + i*4`; the useful slots are [7] blit skips, [9] cycles,
[13] deferrals, [14] misses, [10]/[11] composeL0/L1, [5] blit.

## WHAT IS ALREADY ESTABLISHED — DO NOT RE-DERIVE

  - **MD video composites through the 32X.** Transparency is bit 15 of
    the CRAM ENTRY, not pixel index 0; `cram[0] = 0x8000` arms it.
    Verified on ares (§10).
  - **Tile transport works, verified on ares** (§13): SH-2 converts to
    4bpp planar into the dead FB block at 0x11A00 (FM=1), MD reads
    0x851A00 (FM=0) and uploads. The packet is self-describing and
    idempotent, which is what makes it immune to per-bank staging skew —
    keep that property.
  - **S16 -> MD 4bpp conversion is verified** (`tools/md_tiles.py
    verify`, 16384 tiles, 0 mismatches). High nibble = LEFT pixel.
  - **Patterns fit**: 1363 VRAM slots below the name tables, 599 peak
    distinct. §13.
  - **Colour is not a problem** (§11): MD 3-bit quantisation is ~3%
    error, invisible, and it collapses distinct colours by a third —
    mean 92 -> 58 against MD's 64. Precompute the assignment; do not
    build a runtime merge.
  - **Read-back is ~15 KB** of single-word collision probes (§9), and
    MD VRAM is 68000-readable through the data port, so it stops being
    a blocker once the tilemap lives there.
  - **MD residency must be STABLE.** The render cache is transient and
    cannot be the MD allocator — that cost a debugging cycle (§16).
  - **Parallax is free**: Plane A and Plane B have independent hardware
    scroll. Row/column scroll is not needed for stage 1 (measured, 700
    samples) and Mike's recollection is that later levels are parallax
    only. Defer it until a level needs it.

## TRAPS THAT COST REAL TIME THIS LOOP

  - **Verify an instrument can see a POSITIVE before trusting an
    absence.** The plane reader was wrong three times: it dropped every
    DMA'd write (CD4/CD5 sit above the destination select, so comparing
    the whole code against 1/3/5 misses them), it cannot see VDP traffic
    before its tap installs on frame 1, and installing the tap earlier
    desyncs the control state machine. It published a false headline
    about MK2 twice.
  - **Judge against the RANGE.** ares states under ~3000 cycles
    under-sample maxima badly. A 403-cycle state said the FM window was
    110 lines; a 3709-cycle state said 210 with 8 lines of margin.
  - **`SHASFLAGS` is assigned with `=` BELOW the flag blocks.** A
    `SHASFLAGS +=` written up there is silently discarded. Assembler
    flags go after the base assignment.
  - **Objects depend on `.build_flags`** now, so flag switches force
    rebuilds. Any flag-build measurement older than commit `0faba16` is
    suspect.
  - **Screenshots mislead.** Two of three conclusions drawn from a frame
    this loop were wrong; cross-checking against the savestate flipped
    one from "transport failed" to "transport works". Read the memory.
  - **MAME cannot rank window/pickup work** — its SH-2 is ~3x faster.
    It CAN rank correctness (parity statics) and per-cycle counters.

## DEAD ENDS — DO NOT RE-ATTEMPT

  - **Chaotix's idle-token / poll-and-skip handshake** (`IDLETOKEN`,
    `IDLEGRACE`). Its premise is a PARKED SH-2; ours is saturated. ares:
    cadence 3.03 -> 7.48, "broken the second the game starts."
  - **Interrupt-driven pickup** (`CMDINT`). Preemption moves latency
    from window-pickup to band-completion and the pipeline has no slack
    there: deferrals 4.6x, skips 66%. Also the prize (8.8% of vints) was
    always smaller than the price (the ISR alone cost 3.1 points).
  - **A dirty/block-skipping blit as a framerate fix.** Even with BG,
    FG cat-0 AND text removed, only 31.8% of the screen goes transparent
    (25.7% at 32-pixel granularity) against a ~25% break-even. §15.

## AFTER 1C

1. Precompute the palette assignment (§11) and replace the grey ramp.
2. Wire FG cat-0 onto Plane A (1c does BG on Plane B only).
3. ares pass: capture + savestate. `tools/row_health.py` scores a build
   from a capture alone and tracks what Mike perceives as choppiness;
   `tools/state_health.py` reads the counters from a state.
