# INTENT — what this project is for

**Stable document. Changes only when Mike changes the goal.**
If you are starting work, read this, then `STATE.md`, then `LESSONS.md`.
Do not read the logs first. They are history, not state.

---

## The deliverable

A reusable **System 16 → Sega 32X porting kit**. Altered Beast is the
working title and the proving ground; the kit is the product. Every S16B
title has the same shape — two scrolling tile planes, a text layer, a
sprite chip, 128 colour sets — so solving it once solves the library.

`TOOLKIT.md` is the living inventory of what is game-agnostic versus
Altered-Beast-specific.

## The bar

**60 fps. Nothing else is the bar.**

Report progress as **% of frames that are single-vint**. 60 Hz is 100%
single-vint.

**The bar is a THRESHOLD, not a gradient.** Ships are vint-quantised: a
generation at 1.2 vints still costs 2 and flips at 30 Hz. Nothing is paid
until the wall crosses below 1.00 v/gen — then it all arrives at once.
A card that removes real work and moves the frame rate zero points has
not necessarily failed.

**Current: MOTION 9.3 fps.** See `STATE.md`.

## Non-negotiables

1. **Accuracy before speed.** The port must look and play exactly like
   the MAME source. A faster build that drifts is a regression.
2. **Mike's play pass is the acceptance gate.** It has overruled the
   metrics repeatedly. Metrics rank; Mike decides.
3. **Measure before arguing.** Re-read the counter before building on it.
   See `LESSONS.md` — roughly a third of this project's instruments have
   lied at least once.

## What kind of work this is

Hardware archaeology on a platform with no SDK and no documentation for
most of what matters:

- **Deriving hardware behaviour from open-source RTL.** `srcref/jtcores`
  is the authoritative System 16 spec. Cite `.v` files and line numbers.
  **Derive from it; never copy it into this repo.**
- **Studying how commercial 32X titles drove the hardware.** Nobody
  documented how the 32X and MD VDP cooperate; the shipped games are the
  only record. Findings become hardware facts here. No code is copied.
- **Patching an arcade binary Mike owns** so its video and I/O land in
  the 32X memory map. Address rebasing, no protection involved.

Nothing here is about defeating protection, piracy, or redistribution.

## Roles

**Two threads, and Mike relays between them.**

- **DECOMPILE** reads the arcade 68K program and derives game facts from
  bytes. Answers "what does the original do."
- **BUILDER** measures our port and builds. Answers "what does ours do."

**When they disagree, that gap is the bug.** A measurement beats an
argument, including a well-reasoned one.

`LOOP-PROTOCOL.md` defines how they exchange and what a message must
carry.

## What "done" means

The kit ports a second S16B title with no new engine work, and Altered
Beast passes Mike's play pass at 60 fps.
