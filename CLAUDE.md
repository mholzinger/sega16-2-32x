# sega16-2-32x

A homebrew port of Sega System 16 arcade games to the Sega 32X. The
working title is Altered Beast; the actual deliverable is a reusable
System 16 -> 32X porting kit (see `TOOLKIT.md`), because every S16B
title has the same shape: two scrolling tile planes, a text layer, a
sprite chip, 128 colour sets.

The port currently renders System 16 entirely in software into the 32X
framebuffer and ships at 20 Hz. `ARCHITECTURE.md` explains why, and
what the plan is.

## What kind of work this is

This is hardware-archaeology and homebrew console development on a
30-year-old platform with no official SDK and no published
documentation for most of what matters. The work is:

- **Deriving hardware behaviour from open-source RTL.** `srcref/jtcores`
  is the authoritative System 16 spec — GPL-licensed Verilog. Cite
  specific `.v` files and line numbers for hardware facts. **Derive
  behaviour from it; never copy its code into this repo.** It is a
  reference for what the silicon does, not a source of source.
- **Studying how commercial 32X titles drove the hardware.** Mike owns
  the cartridges and dumps in `srcref/`. Nobody documented how to make
  the 32X and the Mega Drive VDP cooperate, so the shipped games are
  the only record of which techniques actually work on real silicon.
  We read them to learn the platform's idioms — DMA setup, FM
  arbitration, DREQ FIFO protocol — the same way one reads any
  undocumented system. Findings get written down as hardware facts in
  `ARCHITECTURE.md`; no game code is copied, and nothing from `srcref/`
  is redistributed (it is gitignored).
- **Patching an arcade binary Mike owns** so its video and I/O accesses
  land in the 32X memory map (`tools/patch_game.py`). No copy
  protection is involved or circumvented — System 16B has none of
  relevance here; the work is address rebasing.

Nothing here is about defeating protection, piracy, or redistribution.
It is interoperability and preservation work on hardware whose
documentation was never written.

**Prefer the distilled spec over re-deriving.** When `ARCHITECTURE.md`
or a `docs/log/LOOP*.md` already records a hardware fact with a citation, use
it. Going back to the binaries should be for questions the docs do not
already answer.

## START HERE — read these three, in this order, and nothing else

    INTENT.md          what this project is for, and the bar. Stable.
    STATE.md           what is TRUE RIGHT NOW: the line, the live axis,
                       what is measured dead, what is already shipped,
                       the open defects and cards. PRUNED, not appended.
    LESSONS.md         findings that must never be re-derived, each with
                       the measurement that established it and what it
                       cost when it was violated.

Then read `.build_flags` — but it is the stamp of the LAST BUILD, which
is usually a probe, NOT the line. The line's flags are `LINE_FLAGS` in
the Makefile; `make line` is the only thing that builds it. Then
`LOOP-PROTOCOL.md` if you are relaying to the other thread.

**Do NOT start by reading the logs.** `docs/log/*` (30 files) and
`docs/handoff/*` (31 files) are HISTORY, not state. They are append-only,
so the newest entry wins attention over the better-founded older one —
that is how a month of work got aimed at the wrong axis while the right
answer sat in `START-HERE.md` unread.

**Run the PREMISE CHECK before proposing anything** (`LOOP-PROTOCOL.md`):
what the record already says, what flags are on the line, which
instrument, and whether it is already in MEASURED DEAD. Two cards in one
week were killed by that check *after* a full costing exercise had begun.
Both were free to kill at the top.

`docs/handoff/START-HERE.md` is superseded by `STATE.md`. It is kept for
its transport and rig detail; treat its rankings as history.

## The scope (2026-09-08)

The 68000 clock is NOT a loss. The game needs 2780 instructions/vint and
our 7.670 MHz budget covers that at any cost up to 46 cycles per
instruction; the arcade's own 10 MHz 68000 runs the same code at 45.2,
because it is stalled on its video bus rather than computing. We do not
pay those stalls.

**The only thing holding up parity is the pipeline: feeding frames and
sprites into the 32X at the right frequency, in the shape that
architecture wants.** It costs 2882 instructions/vint today — as much as
the game itself. See `ARCHITECTURE.md` (top) and LOOP27 78-79.

## Standing rules

1. **Accuracy before speed.** The port must look and play exactly like
   the MAME source. Fidelity wins every tradeoff. A faster build that
   drifts is a regression.
2. **Mike's ares play pass is the acceptance gate.** It has overruled
   the metrics repeatedly. MAME is the verification rig; ares is the
   hardware-truth proxy when they disagree.
3. **Measure before arguing.** Several sessions have been spent
   optimising against numbers nobody re-measured. Re-read the counter
   before you build on it.

## Gates on every commit

- **Parity statics** — `tools/parity_run.sh <dir> [game]`: title 2.44,
  eyehold 3.37. The dynamic scenes move with cadence; the statics must
  not. It runs our rom inside MAME's 32X, so it only means something
  where MAME models the 32X faithfully — see below. **Those numbers are
  canonical-era: MAME renders every NATIVE build as confetti (BOSSFIGHT
  .md, re-confirmed 2026-09-05 on the accepted nocat1 rom), so the
  statics do NOT gate the shipping line any more.** Pixel truth for
  NATIVE builds is ares (Mike's pass, the screenshot corpora).
- **Region guard** — `grep ' _end$' rom/s16.lst` must stay under
  `0x06019000`. The build fails hard if it does not.
- **Shipping rom stamped `normal`** — `python3 tools/build_id.py show
  rom/s16.32x`. `PRESSURE` is a MAME-side proxy that widens the quiet
  zone; it is a handicap on ares and must never be handed over as an
  ares build.
- **Mike's ares play pass.**

## What MAME is for now (2026-08-17)

The instrumentation has diverged far enough from MAME's 32X that its
role has SPLIT. Keep the three apart — conflating them has cost whole
sessions:

1. **`mame altbeast` (the arcade) is the ORACLE.** Always valid. It is
   what "does it look and feel like the arcade" is measured against,
   and increasingly the comparison is Mike's ares captures against
   arcade captures rather than running our rom in MAME at all.
2. **`mame 32x -cart rom/s16.32x` is a CONVENIENCE MODEL of our
   machine**, not an authority on it. It does not model SH-2 timing
   (~3x fast), ares FIFO loss, the **FB-write bus-stall floor**, or
   **partial DREQ landings** — the last proven by `tools/drq_probe.py`:
   ares read 233 of 234 short pushes at their true length, MAME read 62
   of 62 as zero.

   **THE LIMIT IS PER-FLAG, AND IT IS TWO SEPARATE CLAIMS. Do not
   collapse them into "MAME cannot gate this build" — that blanket
   phrase is false and it talks sessions out of cheap, valid tests.**

   a. **A build whose CORRECTNESS depends on partial DREQ landings
      cannot be PIXEL-gated in MAME** — it takes a different code path
      there, so a diff measures the emulator gap, not the rom.
      `SPRTRUNC=1` is the only current example: the master reads
      landed==0, skips the packet, and MAME shows frozen sprites by
      construction. Gate its pixels by building WITHOUT the flag.
   b. **A build whose PAYOFF is fewer FB writes cannot be SPEED-ranked
      in MAME** — MAME charges the ~2.7us instruction issue and not the
      ~47us/row stall, so it shows the cost of the test and none of the
      saving. `BLITSKIP` is this case. Its CORRECTNESS gates in MAME
      *better than on ares*: a pixel diff against the same build
      without the flag caught attempt 1's bank race in one run.

   Everything else gates in MAME normally. "It renders wrong in MAME"
   is not evidence of a bug ONLY when case (a) applies.

   c. **Under R60 (every build since late August) NO packet lands in
      MAME at all** — measured 2026-09-05: 3897 of 3900 vints read torn,
      with or without SPRFULL. So MAME cannot pixel-gate any current
      rom (that is the confetti), and cannot test anything that rides
      the DREQ landing (palette delivery, the lost-push belt, sprite
      records). The 68K side (RAM, regs, staging, the MCU mailboxes)
      still reads true and is worth a census.
3. **ares + `state_health.py` + the screenshot corpus is our machine's
   truth**, and Mike's play pass is the acceptance gate.

MD-side counters (68K/VDP timing) ARE modelled honestly, which is why
`tools/health_mame.lua` can A/B handler mean and the window/tail split
cheaply — read it as a ranking, never as a clock.

## Headless MAME: do not let it steal the screen or the keyboard

MAME defaults to FULL SCREEN ("otherwise, full screen mode is assumed")
and grabs the keyboard, so a scripted run launched while Mike is typing
blanks his display and eats his input. There is no `mame.ini` in this
repo, so nothing overrides those defaults — the flags have to be on
every invocation:

    mame 32x -cart rom/s16.32x -rompath ./mame -skip_gameinfo \
        -video none -sound none -nothrottle \
        -window -resolution 160x120 \
        -keyboardprovider none -nomouse -nojoystick -bench 100 \
        -autoboot_script tools/<probe>.lua

`-video none` alone is NOT enough — add `-window` (never take the
display) and `-keyboardprovider none` (never take the keyboard).

**Add `-resolution 160x120` too: the popup becomes a thumbnail and
SNAPSHOTS ARE UNAFFECTED.** Measured both ways — `screen:snapshot()`
writes 320x224 regardless, because it captures the emulated screen and
not the window. There is no windowless mode on macOS, so a thumbnail is
the least intrusive available and it costs nothing.
`tools/parity_run.sh` carries them; ad-hoc probe runs must too.

**And always give it `-bench <seconds>` as a HANG BACKSTOP.** Measured:
`-bench` implies `-video none -sound none -nothrottle`, still runs the
`-autoboot_script`, still writes `screen:snapshot()` files, and exits on
its own at the emulated-seconds limit. Every probe here relies on the
lua calling `manager.machine:exit()`; a script bug therefore leaves a
MAME process running forever on Mike's machine. `-bench` makes that
impossible for one flag.

Size it above what the script needs: **frames ≈ 60 x seconds** (NTSC), so
a probe that runs to frame 5400 wants `-bench 100`. Wall time is ~3x
faster than real time (measured 308%), so the backstop is cheap.

`-b` is NOT a background flag — MAME resolves it into `-bios`/`-burnin`/
etc. And **`-background_input` is the opposite of what you want**: it
tells MAME to keep taking input when it loses focus. Leave it off.

A repo-level `mame.ini` would fix it globally, but it would also apply
to `mame altbeast` — the interactive arcade ORACLE, where Mike does want
a window and a keyboard. Keep it on the command line.

## Build and measure

    make                      # shipping rom -> rom/s16.32x
    make <FLAG>=1             # probe builds; see the Makefile header
    tools/parity_run.sh dir   # scene-anchored MAME diff vs the arcade
    tools/state_health.py X   # ares savestate -> pipeline counters
    tools/attract_parity.py rom/s16.32x   # attract vs the arcade no-coin
                              # corpus, headless (ares-headless), aligned on
                              # the game's own timeline; docs/handoff/HANDOFF-SESSION6.md
    tools/ares_diag_at.py rom N ...       # builder/consumer counters at frame N

Objects depend on `.build_flags`, so switching flags forces a rebuild.
Before that existed, `make FLAG=1` after a plain `make` silently reused
objects compiled without the flag and produced measurements that were
really the baseline. Any flag-build number from before commit `0faba16`
should be re-measured.

**Reading ares results:** `tools/state_health.py` prints the state's
BUILD hash. Check it matches the rom you think was played — an ares
savestate contains SDRAM, and RAMCODE lives in SDRAM, so a state saved
under one build carries that build's code into whatever rom you load it
against.

## Where things are written down

- `ARCHITECTURE.md` — how the 32X library draws a frame, where this
  port sits, and the pivot. Start here.
- `docs/design/SILICON.md` — what makes the port run on REAL 32X
  hardware: the slave SDRAM warm-up, the FM=1 framebuffer rule, the FB
  packet transport, the probe rig and its traps. Every claim carries the
  build command that reproduces it. Read it before touching the boot
  path, the packet transport, or before writing any hardware probe.
- `TOOLKIT.md` — the reusable kit inventory: what is game-agnostic and
  what is Altered-Beast-specific.
- `docs/log/LOOP.md`, `docs/log/LOOP6..11.md` — the working log, newest last. Each has a
  NEGATIVE RESULTS section. **Read them before re-trying an idea**;
  most of the obvious optimisations are already in there with the
  measurement that killed them.
- `NOTES.md` — hardware notes and scratch findings.
