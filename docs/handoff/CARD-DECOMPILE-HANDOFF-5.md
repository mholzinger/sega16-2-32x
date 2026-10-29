# HANDOFF 5 — the SH-2 port of the decompiled 68K game

Written 2026-09-30 on Mike's instruction, with his note attached: **"this thread is
too old and we should have had a POC three days ago."** He is right, and the first
section is about that rather than about the work.

---

## 0. READ THIS BEFORE PLANNING ANYTHING

### SUPERSEDED 2026-09-30 BY MIKE, AND THE REVERSAL IS THE POINT

> "We can't have a useful play candidate until the SH-2 port of the 6k code is
> running. full stop otherwise."

**This port IS the critical path.** The section below argued the opposite -- that
the 68K line plus the pipeline was the short path to a playable level 1 and this
track was a speed optimisation. Mike's position is that the 68K line cannot get
there, so the port running IS the gate. Read the rest of this section as history;
do not plan from it. The one part that still stands is that the track must be
measured against SUSTAINED frames rather than one, and FULL-PORT.md 20 now is.

### (HISTORY) The POC does not depend on this work, and I let that go unsaid for three days

**The 68K build already runs the game on the 32X.** Mike watched it: *"Those
barcodes are moving AWEFULLY SLOW. But GUESS WHAT I see the GAME RUNNING!"* and
*"HOLY COW I SEE IT"* (rom `pt22`, on the FPGA). THE LINE is
`rom/night/ring1.32x`, it passes his play gate, and nothing in this card touches
it.

Everything below is a SECOND implementation — the game's logic recompiled onto the
SH-2 — whose payoff is speed, not existence. It is verification-heavy by nature:
you cannot ship a re-implementation of a game you cannot diff against the
original. That is legitimate work and it is **not the short path to a playable
level 1.**

So: if the goal this week is a POC Mike can play, the honest read is that it comes
from the 68K line plus the pipeline, and this port should be costed as a separate
track. If the goal is the 60 Hz architecture, this port is on the critical path and
the numbers below say it is viable.

**Whoever picks this up: say which of those two you are doing, out loud, before
building anything.**

### What this track actually bought

| | |
|---|---|
| does the decompiled game logic FIT in a vint? | **yes, with room** — 362–866 us on ares, 4.5% of a vint; ~7% on hardware at the 1.59x ratio |
| does it RUN on the SH-2? | yes — one full frame, no hangs, every indirect call resolves |
| is it CORRECT? | **not yet. 49 of 16,384 bytes wrong on ONE frame** |

That last number is the whole state of the port and §2 explains exactly how much
it is worth.

---

## 1. ONE COMMAND, AND HOW TO READ IT

    make line NOSPRBAKE=1 PORTFULL=1 RIGBARCODE=1 WRAMCODE=1
    python3 tools/portl2_read.py rom/s16.32x 900

`NOSPRBAKE=1` is required: the port needs the cart window `.sprbake` occupies.
The Makefile owns the whole generation chain (`CPORT_ENV`, one definition) because
running the four steps by hand drifted twice — once leaving the dispatch table
generated from a different root set than the unit, so a state routine was in
neither and every state call silently missed.

Last measured rom: `rom/pma.32x`.

### The ladder is the measurement, not the total

    control (runs nothing)      40 wrong     <- a frame changes 40 bytes
    + irq4_handler              34           -6
    + FUN_00003aae              34           +0   (writes TEXT RAM, invisible here)
    + FUN_0000144a              34           +0   (same)
    + object dispatch           49          +15   (was +150)

    per slot:  slot 0 (player)  17          -23   <- NET CORRECT
               slot 40          39           -1
               slot 16 / 17     50 / 46   +10/+6  (were +40 / +45)

**The control is the calibration.** A frame changes 40 bytes, so a port that does
nothing scores exactly 40 — and it does. Anything below 40 is net-correct work.
Slot 0's handler now reproduces more than half of everything the arcade changed.

---

## 2. WHAT THE 49 IS WORTH, STATED HONESTLY

Three limits, all of which matter more than the number:

1. **It is ONE frame.** `docs/proof/l2/` holds two captures, 9000 and 9001.
   49 bytes wrong on one frame says nothing about a thousand frames, and play needs
   sustained correctness. **A multi-frame gate is the single most valuable thing
   left to build** — `tools/l2_oracle.lua` already captures any frame list; the
   driver would need to chain N frames and compare each.
2. **It observes WORK RAM ONLY.** Text layer, tilemaps, sprite list and palette are
   invisible to it. Two of the three top-level frame calls (`FUN_00003aae`,
   `FUN_0000144a`) write text RAM and correctly score 0 here — a perfect 0 would
   mean the game's STATE is reproduced, not its picture.
3. **The object handlers are entered without their registers.** `port_handlers` is
   an unprototyped table and the dispatcher calls with no arguments. Direct calls
   get their parameters (that is where the 176 -> 49 came from); table-entered
   handlers do not. **The oracle already has the answer: 232 vectors captured at
   the dispatcher's own call site (`caller 0039AC`, 68 distinct handlers) record
   the full register file.** That is the next concrete fix and the remaining +15
   is where it would land.

---

## 3. THE MACHINERY, AND WHY EACH PIECE EXISTS

| tool | what it is for |
|---|---|
| `tools/portl2_read.py` | the gate. Reads `port_l2_out[]` out of an ares dump |
| `tools/l2_oracle.lua` | arcade work RAM at chosen frame boundaries |
| `tools/l2_where.lua` | WHERE the 68K is at a capture. It is inside `wait_frame_flag` (PC 0x003984–8, every frame in the window), so a capture pair brackets exactly one loop iteration |
| `tools/l1_oracle.lua` | per-call register file + reads + writes. **The single most useful instrument here** |
| `tools/ghidra/param_fixpoint.sh` | declares register PARAMETERS to convergence — 549 functions. This is what took the frame 176 -> 49 |
| `tools/ghidra/set_ret_storage.py` | declares return storage — took `extraout_` reads 912 -> 292 |
| `tools/ghidra/extraout_producers.py` | which CALL produced each `extraout_` value, from the SSA |
| `tools/ghidra/reg_convention.py` | 1,637 pseudo-register -> 68K register mappings |
| `tools/harvest_ptr_tables.py` | jump-table targets, both levels |
| `tools/handler_plausible.py` | filters junk handler addresses by A6-relative use |
| `PORTTRACE=1` | the port's own call trace (`-finstrument-functions`), to diff against the arcade's progress markers |
| `PORTTIME=1` | FRT cost of decompiled code. **1.59x ares on hardware** — use it to convert ares numbers instead of a rig round trip |

### The two decompiler facts the whole port rests on

**Ghidra models the READ side of 68K register passing and not the write side.** It
invents `unaff_D3w` / `in_D1` / `extraout_A0` for every inherited register and the
port turns them into globals — 977 reads, 0 writes, 82% of functions. Two
declarations fix it, and nothing else does:

- **return storage** makes the PRODUCER's body materialise. `sprite_frame_lookup` is
  34 bytes that Ghidra rendered `{ return; }` and became a real table lookup.
- **parameter storage** makes the decompiler stop DELETING the argument setup at
  call sites. Without it the caller's value is not in the C at all — 5,276 direct
  calls, arguments nameable at none.

Both are transitive. Parameters need a fixpoint and the set must ACCUMULATE: a
declared register stops being a pseudo-register, so a set rebuilt each round
removes what the last round declared (the count oscillated 153/161/159/163 until
that was fixed).

---

## 4. TRAPS THAT COST REAL TIME — DO NOT REDISCOVER THESE

1. **A 32-bit access at a 2-byte-aligned address is an SH-2 ADDRESS ERROR.** The
   68000 allows it; **147 of 302 placed symbols are 2 mod 4**. Symptom is a silent
   wrong value, or a build spending 40% of its cycles at PC 0x00000002 in
   `main_irq`. Packed accessors (`LD32U`/`ST32`) handle it.
2. **`(ushort)x != -1` is ALWAYS TRUE.** `dbra` tests a 16-bit counter against
   0xFFFF and C promotes the ushort. `textram_stride_write` — five lines — took
   **89% of all master cycles**. Only unsigned forms are wrong; 106 of 110
   comparisons against -1 are signed and correct.
3. **`objcopy -I binary -O elf32-sh` produces a section that REPORTS contents and
   has none.** `.portrom` was 256 KB of zeros under every number for a day. Use
   `.incbin` (`sh_src/port_rom_data.s`).
4. **68K pointers in DATA must be translated at USE time** (`P68`). No source
   rewrite catches them — the value arrives at runtime. `floor_collide` walked one
   forever at 87.6% of master cycles.
5. **Re-analysing an analysed Ghidra project destroys work. Third occurrence.**
   Decompiler Parameter ID took 788 -> 746 functions and lost 12 KB of code for
   zero gain. `param_id.py` now raises. `f1_rebuild.sh` is the only safe route and
   its header already said so.
6. **`docs/proof/altbeast.c` IS NOT IN GIT** (gitignored, correctly — Sega-derived).
   Back it up before any Ghidra operation. `docs/audit/corpus_sha256.txt` and
   `docs/proof/CORPUS-PROVENANCE.md` carry the hash and the rebuild recipe.
7. **A pattern that matched `NAME()` breaks when functions gain parameters.** It bit
   `cport_dispatch.py` and `CPORT_STUBS` on the same day: the dispatch table fell
   425 -> 202, every handler became "unknown", and **the frame scored 34 — better
   than the control — because the game logic had stopped running.** Only
   `dispatch entered 0` gave it away.
8. **Slot collisions in `port_l2_out[]`, three times.** Results now live in reserved
   blocks with a `'VARM'` marker and canaries either side of the array.

### The instrument discipline that actually worked

- A CONTROL variant that runs nothing, so the scale is calibrated.
- CANARIES around the result array, so a number can prove it was not overwritten.
- A COUNTED MISS instead of a wild jump, so a gap is a number and not a crash.
- `--profile` on a hung rom names the loop in one run. Used it twice, worked twice.
- The game's own progress markers (0xFFFEF4/F6, captured by `l1_oracle.lua`)
  against `PORTTRACE=1`'s call log. **This corrected a conclusion I had reached by
  reading the source** — I said the port early-returned and skipped a call; it does
  not, the control flow already matched and only values were wrong.

---

## 5. THE NEXT THREE THINGS, IN ORDER

**REPLACED 2026-09-30 by FULL-PORT.md 20.** The multi-frame gate (item 2 below) is
built and run: 30 frames, 47-53 wrong each, no hang, zero missed indirect calls.
The decomposition moved item 1 DOWN the list -- missing work is 8.1 cells a frame
and spurious writes are 40.5, so the dispatcher's register vectors are now the
smaller half of the error. The live order is FULL-PORT.md 20's.

### (SUPERSEDED) the order as of the handoff

1. **Supply object handlers their entry registers from the 232 oracle vectors.**
   The remaining +15 lives there. It is the same mechanism as §19, applied at the
   dispatcher instead of at direct calls.
2. **Make the gate multi-frame.** One frame is not play. This is the gate that would
   have told us, three days ago, whether the port was converging or not.
3. **Then re-measure everything.** Every number here is one frame old.

## 6. WHERE THE TIME WENT, SO IT IS NOT REPEATED

Most of the elapsed time was spent on bugs in MY OWN instruments, not on the port:
slot collisions, stale regexes, a destructive Ghidra re-analysis, a corpus that
turned out not to be in git, and an `objcopy` that lied. The port itself moved
207 -> 176 -> 49 in the last few hours once the instruments were trustworthy.

The lesson for the next thread is the one Mike has now made twice: **a verification
track can absorb unbounded time and still produce nothing playable.** Put a date on
the POC, keep the gate cheap, and when the gate and the deliverable diverge, say so
on the day rather than three days later.
