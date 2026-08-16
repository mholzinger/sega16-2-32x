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
or a `LOOP*.md` already records a hardware fact with a citation, use
it. Going back to the binaries should be for questions the docs do not
already answer.

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

- **Parity statics** — `tools/parity_run.sh <dir>`: title 2.44,
  eyehold 3.37. The dynamic scenes move with cadence; the statics must
  not.
- **Region guard** — `grep ' _end$' rom/s16.lst` must stay under
  `0x06019000`. The build fails hard if it does not.
- **Shipping rom stamped `normal`** — `python3 tools/build_id.py show
  rom/s16.32x`. `PRESSURE` is a MAME-side proxy that widens the quiet
  zone; it is a handicap on ares and must never be handed over as an
  ares build.
- **Mike's ares play pass.**

## Build and measure

    make                      # shipping rom -> rom/s16.32x
    make <FLAG>=1             # probe builds; see the Makefile header
    tools/parity_run.sh dir   # scene-anchored MAME diff vs the arcade
    tools/state_health.py X   # ares savestate -> pipeline counters

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
- `TOOLKIT.md` — the reusable kit inventory: what is game-agnostic and
  what is Altered-Beast-specific.
- `LOOP.md`, `LOOP6..11.md` — the working log, newest last. Each has a
  NEGATIVE RESULTS section. **Read them before re-trying an idea**;
  most of the obvious optimisations are already in there with the
  measurement that killed them.
- `NOTES.md` — hardware notes and scratch findings.
