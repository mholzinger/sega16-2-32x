# FULL-PORT — translating the System 16 68K program to the SH-2

**Status: a PROPOSAL for Mike's decision, 2026-09-26. Nothing here is built.**
Written because two measurements taken on the night of 2026-09-25 removed the
objection that had kept this closed, and because the alternative path was
measured dead the same night.

Read `docs/design/PORT-CENSUS.md` first — it holds the instruction census, the
partition, and the hand translation this rests on. This document adds what that
one deferred: scope, sequencing, the fidelity strategy, and the cost.

---

## 0. What this asks for

Translate the game's 68K program to SH-2 code ahead of time, run it on the
32X, and leave the 68K as a VDP and audio servant. It is a large change and it
reverses a founding premise (§2). The case for putting it to a decision now is
that the two numbers which used to bound it have been measured and both moved
in its favour, while the only competing route to 60Hz was measured dead.

## 1. Why now: the two measurements

**The cuts path cannot reach 60Hz.** (LESSONS 2026-09-26, rig.) The remaining
68K-axis inventory is ~46 lines a vint against a ~68-line threshold, and its
largest item is gone: `PALROTOROFF`, the rotor's own ceiling, demonstrably took
on the FPGA (palette blocks flagged 179-234 -> 50-67, four launches) and
produced no pass-length gain. The ares ceiling predicted 12 points. Nothing of
the remaining items is individually rankable above the rig's ~15-line floor.

**The port's performance question is closed, favourably.** SH-2 cycles per
instruction, the figure PORT-CENSUS named as "assumed, and it is the whole
ratio", measured on the rig:

    cache-resident   1.125 cyc/instr   identical in all 16 samples, 2 launches
    all-miss         5.125 - 8.75      both loops disassembly-verified

Against PORT-CENSUS's own terms — 383,333 SH-2 cycles a vint, 8,184 68K work
instructions a vint, the measured 1.27x executed translation ratio:

    cache-resident            3.1% of the vint
    all-miss, best case      13.9%
    all-miss, WORST case     23.7%

**Even if every access misses cache, the game fits in under a quarter of a
vint.** Plan on the 23.7% figure, not the 3.1% one (§6).

## 2. WHAT THIS CONTRADICTS, stated first

`REBUILD.md` §1 is explicit, and it is the plan of record:

> "Every reference runs its game logic ON the SH-2. We run the original System
> 16 68K binary on the MD 68K — **that is the project's premise and its
> fidelity asset.**"

This proposal reverses that premise. No reading of it is compatible with
REBUILD.md §1 as written. If it is adopted, §1 has to be rewritten, and the
risk register in §6 of that document has to be re-derived. **That is the real
cost of this proposal and it should dominate the decision, not the arithmetic
in §1 above.**

## 3. The case, fairly against its own premise

Three arguments, and one concession.

**(a) The premise is what made us the exception.** REBUILD.md §0's own census
of ~40 commercial titles found ZERO exceptions: every shipped 32X game runs its
logic on the SH-2 and "uses the 68K as a mailbox peer (never a renderer)".
Cannonball and d32xr do the same independently. The 68K-on-68K choice is
precisely the thing that put this port outside a consensus the same document
calls unanimous. A full port does not depart from the plan of record's
architecture — it arrives at it.

**(b) The premise is the source of §1's constraint, not a neighbour of it.**
Read REBUILD.md §1's consequence list and every entry is downstream of the game
running on the 68K: the game reads back its tile staging, so video state must
live where a 68K can read it coherently; tile RAM at ~52 KB fits only the FB;
hence FB residency, hence capture/restore, hence the "ONE concession" of an
FM-span per frame. Move the logic to the SH-2 and the reader and the renderer
are the same CPU. The state has no reason to be in the framebuffer at all.

**(c) It deletes the pipeline, which is the project's stated bottleneck.**
CLAUDE.md: "The only thing holding up parity is the pipeline... It costs 2,882
instructions/vint today — as much as the game itself." The pipeline exists to
carry one CPU's decisions to another CPU's renderer. One CPU, no carriage.

**The concession, and it is not small.** The fidelity asset is real. Running
the original binary means the behaviour IS the arcade's by construction, and
standing rule 1 is "Accuracy before speed. Fidelity wins every tradeoff." A
translation replaces "correct by construction" with "correct by verification"
across 25,284 instructions. §5 is the answer to that and it is the part of this
proposal that must be believed for the rest to matter.

## 4. Scope: what "full port" means precisely

**It is a full port of GAME LOGIC. The 68K is not removed.**

    MOVES to the SH-2     the 25,284 reached instructions from the two entry
                          points (reset 0x40E, IRQ4 0x2AAC), and the ~15.6 KB
                          of work-RAM state they own.
    STAYS on the 68K      VDP service (the MD planes, DMA), the audio path,
                          the i8751 MCU shim already in md_src/md_main.c, and
                          the input latch. The 68K becomes what the census says
                          every shipped title makes it: a mailbox peer.
    DELETED               the pipeline, the FB residency of game video state,
                          capture/restore, and the FM-span concession of
                          REBUILD.md §1 — all of them artefacts of the split.

**This is NOT a partial port.** PORT-CENSUS settled that: a partial port needs
shared state in the FM-arbitrated framebuffer hole, and the 68K is locked out
of its own object table for the ~106 lines a vint the master holds FM. ~3,573
state accesses a vint cannot be scheduled around that. `port_partition.py`
measured 572 of 590 state-touching routines as ONE connected component, so
there is no seam to cut along even if the transport were free. **All or
nothing, and it has to be all.**

## 5. The fidelity strategy — the crux

The risk is 25,284 instructions of possible drift. The answer is that this port
is unusually verifiable, for three reasons that do not hold for most ports:

**(a) The whole state is 15.6 KB.** Not an unbounded heap — a bounded,
enumerated work-RAM region, already censused two independent ways that agree
(`vint_census.py` at 14.8 KB by reachability, the builder's WRAMCODE exactness
test at ~16 KB). A full state comparison is 15.6 KB per frame, which is
nothing.

**(b) We have a reference implementation and deterministic replay.** MAME
`altbeast` is the oracle and is always valid (CLAUDE.md). Headless ares is
exact and replays input scripts deterministically. `discover/inputs/
gameplay_level1.csv` (built 2026-09-25) now holds our rom in actual level-1
play for 90% of 20,000 frames — the route that did not exist until last night.

**(c) Entanglement blocks partial DEPLOYMENT, not partial VERIFICATION.**
This is the point that makes the work tractable. You cannot *run* 20% of the
game on the SH-2. You can absolutely *verify* 20% of it: snapshot the 68K state
before and after routine R under the oracle, run the translated R on the SH-2
against the same input state, and compare. Unit-level differential testing
needs no shared address space and no live co-execution. The 97%-one-component
finding does not touch it.

So the verification ladder is:

    L1  per-routine differential, offline. Every translated routine, against
        68K state snapshots taken from the oracle. Exhaustive by construction:
        a routine is not done until its differential is clean.
    L2  whole-program differential per frame. Same inputs into the 68K build
        and the ported build; compare the full 15.6 KB state at every vint
        boundary. First divergent frame names the routine.
    L3  the existing estate. Parity statics where they still mean something,
        the attract corpus, the arcade oracle, the rig picture gate.
    L4  Mike's play pass, unchanged as the acceptance gate.

**L2 is the gate that matters.** A port that survives a 20,000-frame
gameplay-route differential at 15.6 KB per frame is not "probably right".

## 6. What is NOT settled, and must be before committing

Listed honestly; three of these are cheap and one may be structural.

**(a) ANSWERED 2026-09-26 — FAVOURABLY, and for a structural reason. See
"F0(a) RESULT" below.**

**(a, as originally stated) STATE PLACEMENT — the one real practical obstacle I can see.** The
translated game's 15.6 KB needs a home in SDRAM. `_end` currently sits at
0x060183C8 against the region guard at 0x06019000: **~3.1 KB of headroom, and
15.6 KB is needed.** Code size is not the problem (`.text` lives in the 4 MB
cart at 0x02000000, and 88,534 68K bytes translate to ~96-104 KB at the
measured 1.08-1.18x, which the cart absorbs easily). State is. REBUILD.md §5's
BURNS list frees a great deal, but nobody has counted whether it frees 12 KB in
the right region. **Count it before anything else.**

**(b) Cache residency is the real performance variable.** A 15.6 KB working
set against the SH-2's small cache is why the all-miss figure, not the
cache-resident one, is the planning number. The object-table re-stride
(128 -> 144 bytes, 1 KB of padding, every slot its own set, 8x coverage) is now
a measured item: the all-miss loop IS that stride, at a 4.6-7.8x penalty. Only
a port can take it — the 68K has no cache. `CACHELOCK` is in MEASURED DEAD as
"2 KB against a 14 KB working set", which is evidence about this exact shape.

**(c) PART-ANSWERED 2026-09-26. The MASTER cannot host it; the slave arm is
blocked on the instrument. See "F0(c) RESULT" below.**

**(c, as originally stated) Which SH-2 runs the game.** The master is the renderer. The slave's
frame-body idle is NOT free: `SELFCHAIN` measured four ways of filling it and
all four rejected 10-14% against the master-chain's 2.0, because that idle is
load-bearing bus quiet. So "put the game on the idle slave" is exactly the
assumption SELFCHAIN falsified, and this needs a measurement rather than an
intuition.

**(d) ANSWERED 2026-09-26 — FAVOURABLY. See "F0(d) RESULT" below.**

**(d, as originally stated) The 249 pointer-table targets and the 78 register-indirect sites.** The
census recovered them, but an AOT translator needs every indirect branch target
to be known or to fall back safely. A residual indirect-dispatch path is the
usual reason an AOT port needs a small interpreter beside it — and an
interpreter does not fit (PORT-CENSUS: ~50 SH-2 instructions per 68K
instruction = 1.07x the vint). **If a fallback interpreter turns out to be
required for even a few percent of dispatches, re-cost the whole proposal.**
This is the item most likely to invalidate it.

## 7. Sequencing

Each phase gates on the ladder in §5. No phase proceeds on an untested
assumption from the one before.

    F0  ANSWER §6 FIRST. Count the SDRAM the BURNS list frees in the guarded
        region (a). Enumerate the indirect-dispatch targets and prove they
        resolve statically (d). Measure which SH-2 can host the game (c).
        NO TRANSLATION UNTIL (a) and (d) are answered — either can kill this.
    F1  The translator. Emit SH-2 from the census's reached set. Not a
        general 68K emulator: a one-shot AOT for this program, with the L1
        differential harness built alongside it, not after.
    F2  Translate and verify routine by routine at L1. Exhaustive. This is the
        bulk of the labour and it is parallel, mechanical, and gated.
    F3  Bring-up: the ported game running on the SH-2 with the 68K demoted to
        mailbox peer. Gate on L2 over the gameplay route.
    F4  Delete what the split required: pipeline, FB residency of game video
        state, capture/restore, the FM-span concession. REBUILD.md §5's BURNS
        list grows considerably here.
    F5  Mike's play pass, then re-baseline parity on this architecture.

## 8. Cost, stated as a range because it is one

F2 dominates and it is 25,284 instructions across ~590 routines. The hand
translation of `collision_detection` is the only datapoint for effort and it
was one routine. The honest estimate is that F0 is days, F1 is weeks, and F2 is
the kind of work that is either largely automatable — in which case it is weeks
— or is not, in which case it is months. **F0 is what tells you which.** F0 is
also cheap and answers three questions that are worth having regardless.

## 9. For the kit

`TOOLKIT.md`'s deliverable is a reusable System 16 -> 32X kit, and this is the
part of the project that would generalise best or worst, with little between.
The translator, the differential harness and the census tooling are entirely
game-agnostic — they key on the 68K program, and every S16B title is the same
shape. If F2 automates, the kit gets a general S16-to-SH-2 path and the
per-title work collapses to a census plus a verification run. If F2 needs
hand-work per routine, the kit gets a translator that still saves the census
and the harness, but each title costs a port. **F0's item (d) is the fork.**


---

# F0(d) RESULT — the indirect-dispatch census (2026-09-26 13:30 ET)

This was named the item most likely to invalidate the proposal. It does not.

**Scale.** Walking the two entry points plus the 249 pointer-table roots reaches
25,284 instructions, of which **62 are indirect control transfers — 0.245%.**
(78 exist in the whole program; 62 are in the reached set.)

Worth noting on its own: from the two entry points ALONE only **846**
instructions are reachable. 239 of the 249 pointer targets are reachable only
through dispatch. **This program is a dispatch machine** — an AOT must treat
dispatch as the main mechanism, not an edge case.

**Classification of all 62** (40-instruction backward walk, stopping at the
routine top):

    11   WORD-OFFSET JUMP TABLE, pc-relative base     statically resolvable
     3   pointer table via lea pc-relative            statically resolvable
     1   lea ABSOLUTE, single target                  statically resolvable
    12   handler pointer from an object record        resolvable via the 249
    35   register set outside the routine            see below

**The dominant shape is the most AOT-friendly indirect form there is:**

    lea %pc@(0x10104),%a0     ; table base: a LITERAL in the instruction stream
    moveq #0,%d0
    moveb %fp@(35),%d0        ; index: a BYTE, so at most 256 entries
    addw %d0,%d0
    addaw %d0,%a0
    addaw %a0@,%a0            ; 16-bit offsets RELATIVE to the table base
    jmp %a0@

15 distinct table bases are named by a `lea` and every one is a PC-relative
literal, so each table is readable directly out of the image: 0x346A, 0x88EC,
0xD0EC, 0xD5A2, 0x100F6, 0x10104, 0x11E34, 0x128DA, 0x12FF2, 0x12FFC, 0x13926,
0x141B8, 0x148A2, 0x15E60, 0x16938.

**The 35 "unresolved" are not computed targets.** Every one inspected has the
same shape — a lookup subroutine returns the handler in `a0` and signals
validity in the carry flag, then the caller tail-dispatches:

    bsrw 0x8d66      ; lookup: returns a0, sets/clears carry
    bccs 0x8e2e      ; not found -> skip
    jmp %a0@         ; tail-dispatch

So their target universe is whatever the lookup routines read, and those read
ROM tables. All 62 sites reduce to the same thing: **fetch a routine pointer
from a table in ROM and branch to it.**

**What this means for the translator.** No interpreter is required. The AOT
needs one 68K-address -> SH-2-entry map covering the 249 targets plus the
expanded jump tables, and every indirect becomes an indexed call through it.
That is the standard AOT solution and it fits the measured budget trivially.

**The residual risk, and its answer.** `pointer_targets()` is a heuristic scan
(longwords pointing at entry-like addresses), so "249" is not proven complete —
a pointer computed at runtime rather than stored would escape it. The answer is
that **the translator can trap**: an unmapped dispatch address halts loudly
instead of running wrong. Under §5's L2 differential over the 20,000-frame
gameplay route, any missing target surfaces as a halt at a named frame. That
converts a static-completeness proof into a test-coverage problem, which is
what L2 already is.

**Verdict: (d) does not block. F0's remaining blockers are (a) SDRAM placement —
3.1 KB of headroom against 15.6 KB needed — and (c) which SH-2 hosts the game.**
(a) is now the one that can still kill this, and it is a counting exercise
against REBUILD.md §5's BURNS list.


---

# F0(a) RESULT — the state fits, and the port frees the room itself (2026-09-26 14:00 ET)

I framed this as "count what REBUILD.md §5's BURNS list frees below the region
guard". That was the wrong question and it would have given the wrong answer.
Counting it first: below the guard, 94.4 KB of 102.4 KB is committed, and 76 KB
of that is `sbuf`, the 336x232 software compose buffer, which is NOT on the
BURNS list. Headroom is 4,616 B against 15,974 B needed — an 11.1 KB shortfall
that the BURNS list cannot plausibly cover below that line.

**But the guard is not a wall, and what sits behind it is the answer.**
`0x06019000` is `TILEMAP_C` (m_main.c:77) — the **SDRAM tilemap shadow**. The
Makefile's own guard message says so: *"tilemap shadow and friends live there —
grow the region map instead"*. Its size, agreed three independent ways:

    TILEMAP_C 0x06019000 -> TEXT_C 0x06026000        53,248 B = 52 KB
    NPAGES 12 + the blank page 12 = 13 x 4096        53,248 B
    REBUILD.md §1, in its own words: "Tile RAM (~52KB live)"

**That shadow exists ONLY because the game runs on the 68K.** REBUILD.md §1
states the reason: "The game READS BACK its tile staging (15KB of collision
probes + 1KB scratch)... Its video state must be somewhere the 68K can read
coherently... Tile RAM (~52KB live) fits only the FB." The shadow is the SH-2's
readable copy of tile staging the 68K owns. After a full port the game writes
its tile staging directly into SH-2 SDRAM as its own data structure. There is
nothing to shadow, nothing to copy, and no transport.

    port state needed          15,974 B  (15.6 KB)
    tilemap shadow released    53,248 B  (52 KB)
    MARGIN                     37,274 B  (36.4 KB spare)

So the port does not need the BURNS list for this at all: **it frees the region
it needs, and for the same reason it needs it.** That is the §3(b) argument
arriving as a number rather than as prose — every entry in §1's constraint list
is downstream of the premise, and this is the largest of them measured.

**Caveat, stated:** the 15.6 KB figure is the game's WORK-RAM state. The tile
staging itself (the ~52 KB) does not vanish — the ported game still stages
tiles; it just stages them where it lives instead of staging them in the FB and
having them copied. The accounting above is therefore not "52 KB freed for
15.6 KB of state" so much as "the copy disappears and the original moves into
SDRAM, where 52 KB of room already exists for exactly it." Either reading
clears the obstacle; the second is the honest one.

**F0 status: (a) CLEARS. (d) CLEARS (see above). (c) — which SH-2 hosts the
game — is outstanding and needs the rig.** Neither of the two items that could
kill this proposal survived contact with the measurements.


# F0(c) RESULT — which SH-2 can host it (2026-09-26 14:40 ET, rig)

**Instrument first.** Every rig speed reading in the record samples a
per-attract-step accumulator once or twice a launch, and the ~15-line
per-launch spread has swamped effect after effect. `RIGRATE=1` counts game
frames and vints from boot and never resets: barcode byte 5 = game frames per
vint x 255, byte 6 = vints/256 (a settle indicator). It converges — one band
of +-3% across a run — so four launches give four numbers.

**The load.** `PORTLOAD=1` (master) / `=2` (slave), `sh_src/port_load.h`:
10,400 SH-2 instructions a vint, the translated game's count, walking a 16 KB
working set one cache line at a time. 16 KB against the SH-2's 4 KB cache is
4x oversubscribed, so it exercises RESIDENCY — §(b)'s planning variable — and
not issue rate. Exactly 5 instructions an iteration, verified in the
disassembly. `PORTLOADN=n` scales it; the 10x build is the control.

## THE PICTURE GATE (added 2026-09-27, and it changed both answers)

Byte 6 now carries PRESENTED frames a vint x 255 -- the master counts THE FS
WRITE, the only thing that changes the displayed framebuffer -- against byte
5's GAME frames a vint. Both converge from boot. A change that raises 5 while
lowering 6 is buying its number by rendering less, which is the FBXPORT
failure, and the pair is what exposes it.

## The master: it was the PIPELINE, not the load.

    control       147-150   (58.2%)     two launches
    MASTER load   123-132   (49.9%)     two launches

Non-overlapping, a 14% relative loss, and consistent with the arithmetic:
10,400 instructions at the measured 5.1-8.75 all-miss cyc/instr is 13-22% of
a vint. Confirmed directionally on ares. **The renderer has no free 10,400
instructions, so a whole-game-on-the-master plan is out.**


    normal control            game 58.4%   presented 33.5%
    normal + master load      game 50.7%   presented 25.8%
    pipeline-off control      game 62.4%   presented 67.4%
    pipeline-off + load       game 58.0%   presented 65.4%

With today's pipeline the load costs 7.7 points on BOTH metrics, bands
non-overlapping -- and because both move together, that result is genuine and
not a rendering-less artefact.

**With the pipeline ablated (IDLEOFF=1 NOMAPS=1) the same load costs 2.0
points of presented frames and the bands OVERLAP (163-173 against 167-175).**
That is the configuration a full port approximates: no packet building, no
tile batch, no NT chunk, and no cart reads feeding them. **So "whole-game-on-
master is out" is WITHDRAWN.** What the first measurement showed was that the
master cannot host the game ALONGSIDE the pipeline the port deletes, which is
not the configuration the decision is about.

Caveat, stated: NOMAPS renders wrong by construction, so the pipeline-off
presented rate is not comparable in absolute terms to the normal pair. Only
the within-pair comparison (load against no load, both NOMAPS) is valid, and
that is the comparison being made.

## The slave: YES, and the 10x control proves the reading

    control            game 59.1%   presented 33.7%
    SLAVE load 1x      game 59.3%   presented 32.9%     both FLAT
    SLAVE load 10x     game 63.0%   presented 23.4%     opposite directions

At the real load -- 10,400 instructions a vint -- BOTH metrics are flat and
every band overlaps (game 147-156 against 147-155, presented 78-89 against
78-93). **The slave absorbs the translated game's work at no measurable cost
to cadence or to the picture.**

At 10x the two metrics move in OPPOSITE directions: game frames up 3.9 points,
presented frames down 10.3, neither band overlapping. That is the confound the
gate was built for, caught in the act -- and it is also the control proving the
slave hook fires, so the flat 1x reading is real headroom and not the false
null of the first attempt.

## Why this does not contradict SELFCHAIN

SELFCHAIN filled the slave's idle with COUPLED work -- chain and compose --
which serialises against the master and rejected 10-14%. This load is
INDEPENDENT: read-only SDRAM traffic that shares no state and no sequence with
the compose. The idle is not free for work the master must wait on; it does
absorb work the master never waits on. A ported game on the slave is the
second kind only if its state is not on the master's critical path, which is
the next design question.

## Where F0(c) stands

Both hosts are viable, on different terms. The slave takes the load today. The
master takes it once the pipeline it currently runs is gone. A SPLIT is not
required by these numbers, so the choice can be made on state locality rather
than on cycles -- which is where §(b)'s cache argument, and the object table's
128-byte stride, become the deciding factors.


---

# F0(c) RESULT — both hosts viable; the master answer FLIPPED (2026-09-26 19:15 ET)

**F0 is now complete. (a) clears, (c) clears on both arms, (d) clears. None of
the four items that could have blocked this proposal survived measurement.**

## The instrument that made it answerable

Barcode byte 6 now carries PRESENTED frames per vint — the master counts the FS
write, the only thing that changes the displayed framebuffer — converging from
boot beside byte 5's game frames. **A build that raises one while lowering the
other is buying its number by rendering less.** That is the FBXPORT failure made
visible, and no load measurement here means anything without it.

## The master: it was the pipeline, and my objection was the reason

                            game frames   presented
    normal control              58.4%       33.5%
    normal + load               50.7%       25.8%
    pipeline-off control        62.4%       67.4%
    pipeline-off + load         58.0%       65.4%

With `IDLEOFF=1 NOMAPS=1` the same 10,400-instruction load costs **2.0 points of
presented frames with overlapping bands**, against 7.7 points on both metrics
with the pipeline present. **"Whole-game-on-the-master is out" is WITHDRAWN.**
What the first measurement showed was the master failing to host the game
alongside work a port deletes.

Caveat the builder recorded: NOMAPS renders wrong by construction, so only the
WITHIN-PAIR comparison is valid — which is the comparison being made.

## The slave: yes, and the paradox resolved itself

                    game frames   presented
    control             59.1%       33.7%
    slave 1x            59.3%       32.9%
    slave 10x           63.0%       23.4%

At tenfold load the two metrics move in OPPOSITE directions — game frames up
3.9, presented down 10.3, neither band overlapping. That is the FBXPORT signature
caught in the act, and it is why the earlier monotonic rise was not headroom. At
the real load both metrics are flat and every band overlaps, and the 10x arm
doubles as the scaled control proving the hook fires — so the flat reading is not
another false null. **The slave absorbs 10,400 instructions a vint at no
measurable cost to cadence or picture.**

## SELFCHAIN reconciled

SELFCHAIN filled the slave's idle with COUPLED work the master waits on. This
load is independent read-only traffic sharing no state or sequence with the
compose. So: **the slave's idle is not free for work the master must wait on; it
does absorb work the master never waits on.** The earlier negative stands and is
not evidence against this one.

## THE NEW OPEN QUESTION, and it is a design question not a cycle question

Whether a ported game qualifies as "work the master never waits on" depends on
whether its state sits on the master's critical path. **That cuts differently for
the two hosts, and it now argues for the master:**

  * GAME ON THE SLAVE. The master renders, so it must read the game's output —
    sprite lists, tilemap decisions, palette. That is a cross-CPU read of live
    state every frame, i.e. **a pipeline again**, smaller than today's but the
    same shape, and the same FM-arbitration and coherence questions come back.
  * GAME ON THE MASTER. The code that decides what to draw and the code that
    draws are the same CPU reading the same SDRAM. **No cross-CPU read exists**,
    which is the whole point of §3(c). The pipeline-off measurement above says
    this now fits.

So the measurement that flipped the master arm also removes the main reason to
prefer the slave. The remaining discriminator is state locality and cache
residency — the object table's 128-byte stride and its measured 4.6-7.8x penalty
(§6(b)), and the re-stride to 144 bytes that only a port can take.

**On the load figure:** F1.1 now puts the translation target materially below
PORT-CENSUS's 25,284, so 10,400 instructions a vint is an UPPER bound. The
builder's recommendation to treat it as such rather than re-run against a lower
one is right — a bound that holds is worth more than a tighter number.


---

# §4 SCOPE CORRECTED: we are porting the GAME LOOP, not the display half (2026-09-27 00:15 ET)

Mike: "we are looking for the game loop. So much of the other instructions are
assembly for fetching and displaying sprites and tiles to race the beam. So we
already have our own code to talk to the VDP. So much of the 68K code is for
reference."

**§4 as written says "MOVES to the SH-2: the 25,284 reached instructions". That
is wrong, and it is the largest over-statement in this document.** We do not
need the 68K's display code. We have a renderer: MDBGALL draws the tile planes,
TILESMD/CAT1MD the MD path, MDSPR/SPRLIST the sprites, TEXTCAP* the text, the
V-ISR flip discipline the beam. The 68K's version of all of that is REFERENCE --
the spec we already implemented against.

## The split, measured against docs/audit/function_map.md

    category                           fns   bytes    %
    MIXED logic + display              161  36,414  58.4%
    display only (we already do it)     79   9,216  14.8%
    hardware glue (our own VDP path)   117   7,688  12.3%
    leaf/helper                        150   5,063   8.1%
    GAME LOGIC only                     37   2,434   3.9%
    other/named                         16   1,570   2.5%

**Pure display plus hardware glue is 27% of bytes and is simply DELETED from the
port's scope** -- not translated, not verified, not carried. But the honest
finding is the first row: **58.4% of bytes are functions that do BOTH.** The
game's object handlers decide and poke video registers in the same routine, so
the separation is not clean at function granularity.

## WHICH IS WHY THE PATCHER'S TABLES ARE THE PORT'S REWIRING MAP

`tools/game_altbeast.py` already enumerates, hand-derived and byte-verified
against the program, every place the game touches video:

    PAL_DIRTY_SITES         42      palette writes
    TILE_DIRTY_SITES        25      tile-RAM writes
    TEXT_IDIOM               8      text writes
    HARVESTED_HANDLERS      43      object handlers
    FMGATE_ENTRIES/SPANS    34/18   the FM-arbitration gates
    WRAM_BLOCKS / 2         14/6
    ... 249 enumerated entries across 38 tables

So within a MIXED function, the display half is not a translation problem -- it
is a **REDIRECTION** problem, and the redirect list already exists. `patch_game.py`
does exactly this today at the machine-code level: it rewrites S16 video
addresses to 32X ones. The port does the same substitution one level up, in C:
a handler's palette store becomes a call into our palette path, its tile store a
call into ours.

**You rewire ~75 video sites, not 161 functions.**

## Revised scope, replacing §4's list

    TRANSLATE   the game loop, the object handlers' DECISION logic, collision,
                state machines, input response, scoring, level progression.
                The 83 handlers the oracle observed, plus what they call.
    REDIRECT    every display touch inside those handlers, at the ~75 sites the
                patcher's tables already enumerate, to the renderer we have.
    DELETE      the 68K's own tile-fetch, sprite-build, palette-upload, text and
                beam-racing code -- 27% of mapped bytes -- keeping it as the
                REFERENCE it has always been for our implementations.
    DERIVE      the 302 spec-derivable functions (54% by count) rather than
                transcribing them.

Two things this changes downstream:

  - **The fidelity surface shrinks again.** §3's concession was re-priced to
    ~16 KB / 35 handlers by the tag classification; this cuts further, because a
    handler's display half is verified by our renderer's existing gates (Mike's
    play pass, the rig picture gate) rather than by an L1 differential.
  - **HARVESTED_HANDLERS holds 43 and the oracle observed 83.** Worth
    reconciling: either the patcher's list is a subset by design, or 40 handlers
    are unpatched. That is a question for the builder's capture, and it matters
    because those are the functions the port must carry.

## 8. THE PORT LINKS (2026-09-27)

First time the whole thing has linked. **819 functions, 463,732 B of SH-2 text, 1,468 B
of BSS, zero undefined symbols.**

    tools/cport_prep.py docs/proof/altbeast.c docs/proof/cport
    tools/cport_cast.py docs/proof/cport/altbeast_port.c -I docs/proof/cport
    tools/cport_layout.py docs/proof/cport
    sh-elf-gcc -m2 -ffreestanding -nostdlib -Wl,-dT,layout.ld \
        port.o defs.o romimg.o -lgcc -o port.elf

Four things had to be fixed to get there, each a symbol-naming fault:

* the pseudo-register DEFINITIONS still used the old names after the `_r_` rename
* `layout.ld` carries PROVIDEs only, so it is an AUXILIARY script (`-Wl,-dT`), never the
  main one
* `layout.ld` defines `_WRAM_BASE` in the LINKER namespace and the SH-ELF compiler adds an
  underscore to a C name, so C must say `WRAM_BASE`
* `cport_layout.py` needed Ghidra's `_N` suffix in its parser -- the third tool to need it

### What stands between this and playing level 1

Linked is not run. In rough order of risk rather than effort:

1. **IT HAS NEVER EXECUTED.** No entry point is wired into a rom and nothing has run a
   single instruction of it.
2. **INPUT AND SOUND ARE STUBS.** `layout.ld` places 56 MMIO symbols in a zeroed stub
   block, so the port reads 0 for the pad and its sound writes go nowhere. The pad has to
   be fed from the 68K into the port's input cells.
3. **THE OUTPUT HAS NOWHERE TO GO.** Our renderer reads the game's sprite and tile state
   out of MD work RAM. The port keeps that state in SDRAM at `_WRAM_BASE`, so the
   renderer's source of truth has to move -- which is the actual 68K/SH-2 division, not a
   detail.
4. **THE FRAME PROTOCOL IS UNDECIDED.** `wait_frame_flag` is in the image and spins on a
   flag the 68K used to set. Something has to set it now.
5. **WHICH SH-2 RUNS IT IS STILL OPEN** (§6(c)). The master is the renderer, and
   `SELFCHAIN` measured the slave's frame-body idle as load-bearing bus quiet, so "put it
   on the idle slave" is the assumption that measurement falsified.
6. **647 of 771 FUNCTIONS HAVE NO ORACLE VECTORS AT ALL.** 124 have them; 27 of 28
   attempted are verified (95 of 99 cases). The rest are unverified, not verified-clean.

**The next step is L2 on a SINGLE FRAME, and it needs nothing from Mike or from hardware:**
seed the port's work RAM from an oracle frame boundary, run one frame, and compare all
15.6 KB against the next boundary. That is the gate. If one frame matches, the path to playable
is engineering; if it does not, the first divergent cell names the routine.

## 9. IT FITS: 241 KB, and the space is already there (2026-09-27)

The whole port is 453 KB of SH-2 text and the largest contiguous free run in the 4 MB cart
is 252 KB, so "it does not fit" was the first answer. **It is the wrong answer, because the
rom does not need the whole corpus.**

    corpus                                      770 functions,  453 KB
    reachable from the frame entry (0x40E)       244 (32%),     ~144 KB
    + the 104 observed object handlers           410 (53%),     ~241 KB est.
    MEASURED, not estimated                      410,            135 KB
    largest contiguous free cart run                            252 KB

**CORRECTION, same session: there is NO 252 KB free run. The largest truly unallocated gap
is 55 KB, and 135 KB does not fit.**

The "584 KB free in runs of 252/172/96/56" figure came from scanning the cart image for
0x00/0xFF FILL BYTES -- and a fill-only run INSIDE an allocated section is not free space. The
252 KB "gap" at 0x001000 is padding inside `.text`, which is LMA 0x000000 size 0x2630C4. The
real allocation:

    .text                        2,444 KB
    .tilesmd                       506 KB
    .gamehigh (68K program)        256 KB
    .sprbake                       665 KB
    + .ramtext, .palscenes, .mdsprart, .porttest
    TRUE gaps: 55 KB, 7 KB, 3 KB, 1 KB

**Measuring fill instead of allocation is the same error as sizing by a count ratio** -- both
produced a confident number about the wrong quantity. The cart is ~3.8 MB of 4 MB used.

So placing the port means FREEING something, and for a probe the answer is easy: **`.sprbake`
is 665 KB of baked sprite art that a game-logic probe does not need.** A PORTRUN build drops
`SPR_BAKE` and takes 0x340000. Shipping is a different question and the SSF2 mapper past 4 MB
(`SILICON.md` §4e) is the honest answer there.

**135 KB against 55 KB truly free in the current layout.** The 241 KB estimate was a
function-count ratio and it was nearly DOUBLE the truth: the reachable functions are
SMALLER than average, because the handlers are leaf-ish state machines rather than the big
setup routines the ratio assumed. **A size taken from a count ratio is not a size.**

Two other things fell out of measuring it properly. The reduced port's diagnostics drop from
38 warnings to **8** (4 compare-distinct-pointer, 2 pointer-to-int, 1 shift-count) -- the
excluded 360 functions carried nearly all of them. And a standalone link reads 391 KB only
because it pulls in `romimg.o`, a 256 KB copy of prog68k.bin; **the cart already holds the
68K program at 0x300000**, so in-rom `_ROM_BASE` points at 0x02300000 and the copy is not
needed (`cport_layout.py --rom-base 0x02300000`). Zero undefined symbols either way. No need to drop `SPR_BAKE` (603 KB at 0x340000), and no
need for the SSF2 mapper past 4 MB (§4e).

**The handlers have to be added as ROOTS, not found by reachability.** `object_dispatcher`
statically reaches THREE functions, because it dispatches through a pointer table -- so a
naive call-graph walk from the frame entry misses the entire game. The 112 handlers observed
over Mike's full playthrough (`docs/proof/obj_handlers.md`, 14,078 dispatches) are the
answer; 104 of them match a decompiled function. **A reachability number that does not
account for indirect dispatch is not a size estimate, it is a lower bound that looks like
one** -- 32% versus 53% is the difference.

Free space is FRAGMENTED: 584 KB total but in runs of 252, 172, 96, 56 KB. The 252 KB run at
0x001000 is the only one that takes 241 KB, so placement is forced rather than chosen.

### Revised path to a playable level 1

Unchanged in substance from §8, but the first item is now bounded work rather than an open
question:

1. **Place the 410-function reachable set at 0x001000** and get it to link inside the rom.
2. **Feed the pad** from the 68K into the port's input cells (the MMIO stub block).
3. **Move the renderer's source of truth** from MD work RAM to `_WRAM_BASE` in SDRAM. This
   is the 68K/SH-2 division and the largest single piece.
4. **Decide the frame protocol** -- something must set the flag `wait_frame_flag` spins on.
5. **Decide which SH-2 runs it** (§6(c); `SELFCHAIN` falsified "the idle slave").

And the gate before any of it is worth building: **L2 on one frame.** Seed work RAM from an
oracle frame boundary, run one frame, compare 15.6 KB. Needs a full-RAM capture at two
consecutive boundaries, which the L1 vectors do not contain -- that is one MAME script.

## 10. THE L2 GATE IS CHEAPER THAN EXPECTED: 40 BYTES A FRAME (2026-09-27)

`tools/l2_oracle.lua` captures the FULL 16,384-byte work-RAM image at consecutive frame
boundaries (MAME's own `frame_done`, so no assumption about where the game's loop sits).
Captured from Mike's full playthrough, `inp/ab_full`:

    attract  (frames 1500 -> 1501)   50 of 16,384 bytes change   40 runs
    gameplay (frames 9000 -> 9001)   40 of 16,384 bytes change   38 runs

      object table 0xFFC000-0xFFDFFF   26 B
      globals      0xFFF000-0xFFF7FF    7 B
      high         0xFFF800+            4 B
      sprite/other 0xFFE000-0xFFEFFF    3 B

**A frame changes 0.2% of the state.** §5(b) proposed L2 as "compare the full 15.6 KB per
frame, which is nothing" -- it is far better than that: the port has to reproduce about
**forty byte-changes in thirty-eight runs**, and any divergence is one cell against a nearly
static background. This is the sharpest differential available anywhere in this project.

It also says something about the port's risk profile. 26 of the 40 bytes are in the object
table, which is `object_dispatcher` and the handlers -- so the frame differential is
dominated by exactly the code path with the most oracle coverage (104 of 112 observed
handlers are decompiled) rather than by the parts that are unverified.

Images kept at `docs/proof/l2/wram_009000.bin` and `wram_009001.bin` (gitignored, ROM-derived).

**What remains for the gate is now only the port EXECUTING**: seed 0xFFC000-0xFFFFFF from the
9000 image, run one frame, diff against 9001. No capture work is left.

## 11. THE HARDWARE PRICE OF DECOMPILED CODE: 1.59x ARES (2026-09-28)

§6(b) asked what decompiled 68K code costs on real hardware, and said ares
structurally cannot answer it — ares charges instruction cycles only, with no
data cache and no SDRAM waits (LESSONS: "ares charges instructions only"). The
measurement now exists.

**Instrument.** `PORTTEST=1 RIGBARCODE=1 PORTTIME=1`. The self-test's 99 oracle
cases each bracket their CALL — and only the call — with a free-run-timer read
(`sh_src/port_selftest.c`, generated). Seeding, comparison and the bounded clear
are outside the bracket: charging harness cost to the port is what made an earlier
rig build a slideshow. `port_test_out[11]` accumulates; `tools/porttest_read.py`
reads the full 16-bit value from an ares dump, and barcode byte 5 carries it
shifted for the FPGA.

| | FRT ticks | us for 99 calls | % of a 16.69 ms vint |
|---|---|---|---|
| ares (instruction cycles only) | 479 | 666–804 | 4.0–4.8% |
| MiSTer S32X FPGA core | 760 | 1057–1195 | 6.3–7.2% |

**The transferable constant is the ratio: 1.59x.** It converts any future ares
measurement of ported code into a hardware estimate, which matters because ares
is the fast, repeatable, exact rig and the FPGA is a 90-second round trip.

Mean per call: 6.7 us on ares, 10.7 us on the FPGA. Worst single call 25 ticks
(34.8 us, 0.2% of a vint).

### How much of this to believe

- **The ratio is solid.** Each per-call delta truncates toward zero by up to one
  tick, so both totals understate by the same +<=99 ticks; the ratio is robust
  where the absolute figures carry a 100-tick band (that is what the ranges are).
- **Confirmed at two divisors, because the first reading was a trap.** At
  `PORTTIME_SHIFT=3` the FPGA read byte 5 = 95 — and the pass count that byte
  normally carries is also 95. Indistinguishable. `PORTTIMESHIFT=2` read 188–193
  = 752–772 ticks, which agrees with 760–767, and the preprocessed source confirms
  `port_test_out[10]` is not compiled in under PORTTIME. Two divisors, or do not
  believe a PORTTIME number.
- **It does NOT say the game fits in a vint.** 99 oracle vectors are not a frame.
  They are 29 functions called with captured arguments, not the frame's actual
  call graph or its actual working set. The next measurement is the L2 one-frame
  driver (§10) timed with this same instrument — that one IS a frame, and it is
  the number that answers the question.
- **FPGA, not silicon.** And part of the MiSTer core's memory penalty is known to
  be its own: `IF.sv USE_ROM_WAIT=1` puts an SDRAM access behind every MD ROM read
  (see THE LINE's verdict, 2026-09-25). That inflated a previous FPGA-vs-ares gap.
  Here the ported code runs from SDRAM on the SH-2 rather than through the MD ROM
  path, so it should not apply — but 1.59x is an upper bound on the true hardware
  penalty, not a lower one.

## 12. THE OBJECT TABLE IS THE MISSING ROOT SET: 290 FUNCTIONS, 119 KB (2026-09-28)

**This section replaces a wrong one written the same day.** Its first version was
titled "the object system stores resume addresses, not function pointers" and
concluded the port could not use one C function per 68K function for objects. The
census refutes that. The record of the error is kept below because the reasoning
that produced it was the reasonable reading of one frame, and one frame was not
enough.

### The geometry, cited rather than inferred

`object_dispatcher` (0x0398E) is six lines and answers everything:

```c
pcVar1 = &DAT_ffffc000;
do { if (*pcVar1 < '\0') { ... (**(code **)(pcVar1 + 2))(); }
     pcVar1 = pcVar1 + 0x80; } while (DAT_fffff109 < 0x40);
```

Pool at 0xFFC000, stride 0x80, **64 slots** (so it ENDS at 0xFFDFFF), active =
byte 0 bit 7, handler = a 32-bit code pointer at +2, called INDIRECTLY. That
indirection is why no call-graph walk can reach the game logic: a walk from the
frame roots keeps 27 of 770 functions and stops at the table.

### The census

`tools/l2_handlers.lua` samples every active slot's handler word each frame over
`inp/ab_full`. **21,600 frames, 103 distinct addresses** —
`docs/proof/object_handlers.txt`, with the first frame each was seen.

| | count |
|---|---|
| exact function entries | 92 |
| mid-function | 8 |
| in no function Ghidra disassembled | 3 |

**92 of 103 are function entries, and 90 functions hold exactly one address.** If
the object system were coroutine-based — a handler storing where it got to and
being resumed there — a function would hold many addresses. Five hold two; none
holds more. So the handler is a FUNCTION POINTER, the port's unit IS the function,
and the 11 that are not entries are Ghidra under-splitting: Ghidra splits on a
*direct* call, so an entry reached only through this table, with its neighbour
falling through into it, gets merged. Four of the five two-address functions have
offsets [0, N] with N near the end — the shape of a merged tail routine.

### The result

With the handler file as the root set (`CPORT_HANDLERS` is a FILE PATH parsed for
hex addresses, not a name list):

    CPORT_HANDLERS=docs/proof/object_handlers.txt \
    CPORT_ROOTS=FUN_00003aae,FUN_0000144a,object_dispatcher,irq4_handler \
      tools/cport_build.sh /tmp/cport_l2

**290 of 770 functions. 119,224 bytes of .text. 0 compile errors.** That is the
measured size of a playable-frame port, and it replaces §9's 241 KB estimate and
the 310,656 B of the 410-function unit — both of which included code no frame
reaches.

Only **4 addresses** the port cannot express, and they are named:
`0x049BA 0x0F90A 0x0FD6C 0x10DD4`. `cport_prep.py` now PRINTS them rather than
dropping them silently, and the fix is one Ghidra re-run with them as entry points.

### What still has to be looked at

`tools/cport_build.sh` compiles with `-w`. Under `-Wall` the 290-function unit has
these, and they are a review queue, not noise:

| count | warning |
|---|---|
| 27 | control reaches end of non-void function — Ghidra arity; only matters where the value is used |
| 11 | pointer signedness in assignment (`byte *` to `char *`) |
| 7 | array subscript partly outside `byte[1]` — the Ghidra placeholder objects |
| 3 | pointer-to-int cast of different size |
| 3 | comparison of distinct pointer types |
| **1** | **`sStack_6` used uninitialized — a real potential defect** |

### The frame body, and two corrections to how it was described

- **The frame entry is not `FUN_0000040e`.** That is the reset body: the vector
  table in `sh_src/game_high.bin` has reset PC 0x400 -> `bra 0x40E`, and IRQ4 (VBL)
  at 0x404 -> `bra 0x2AAC` = `irq4_handler`.
- **There is no frame function at all.** The body is an inline `do {} while` inside
  the reset routine, identical in switch cases 0 and 8: `FUN_00003aae()`,
  `FUN_0000144a()`, `object_dispatcher()`, then `wait_frame_flag()` as the vsync
  barrier — which an L2 driver must NOT call, it spins on a flag IRQ4 sets. The
  live mode is case 8, read out of the capture (`DAT_fffff031` = 0x08).
- `irq4_handler` is a NAME COLLISION in the port: the 4-byte `bra` stub at 0x404
  took the name, and the real 784-byte handler at 0x2AAC became `irq4_handler__2`.

### The error this section used to contain, kept on purpose

One frame's capture showed four handlers, of which 0x049BA was 330 bytes into
`FUN_00004870` and 0x1A70E was in no known function. Two mid-function values out of
four reads as a coroutine system. Both premises were weaker than they looked:

- **0x1A70E was not a handler at all.** That scan ran to 0xFFFFFF; the pool ends at
  0xFFDFFF. 0xFFF300 is the game-variable region, and the bytes at 0x1A70E are a
  data table (`0007 0001 7FFF 4900 4A00 4B00 4C00 4D00 4E00 4F00`), not 68K code.
  The slot geometry had been corroborated against the frame delta, which was sound
  — but a corroborated stride cannot bound a range the corroboration never tested.
  The bound was available in `object_dispatcher` the whole time.
- **0x049BA alone does not distinguish the two explanations.** A continuation and a
  Ghidra-merged entry look identical in one sample. They separate on the
  DISTRIBUTION, which needs a census: many addresses per function, or one.

Cost: about twenty minutes and one wrong paragraph handed to Mike. The general
lesson is the cheap one — **a claim about a distribution cannot be made from one
sample, and the bound on a scan must come from the code that does the scanning,
not from the plausibility of its output.**

## 13. THE L2 GATE RUNS: ONE FRAME OF DECOMPILED 68K ON THE SH-2 (2026-09-28)

`make line NOSPRBAKE=1 PORTFULL=1 RIGBARCODE=1 WRAMCODE=1` builds a rom that seeds
16,384 bytes of work RAM from an arcade capture, runs the game's frame body on the
SH-2, and diffs all 16,384 bytes against the arcade's next frame.

**It completes. 16,193 of 16,384 bytes match. 900 handler calls, 900 returns, no
unknown handlers, no hangs.** Cost 747 us on ares = 4.5% of a vint; at the 1.59x
hardware ratio (§11) that is ~1.19 ms, **7.1% of a vint on the FPGA**. Whatever
else is unfinished, the game logic is not close to the frame budget.

### The result that matters is the ladder, not the total

Each line adds ONE call to the line above, against the same seed:

| sequence | bytes wrong | vs control | this call |
|---|---|---|---|
| control — runs nothing | **40** | — | — |
| + `irq4_handler` | 36 | −4 | **−4** |
| + `FUN_00003aae` | 36 | −4 | 0 |
| + `FUN_0000144a` | 36 | −4 | 0 |
| + object dispatch | 191 | +151 | **+155** |
| `FUN_00003aae` alone (no irq4) | 40 | 0 | 0 |
| &nbsp;&nbsp;and `FUN_0000144a` | 40 | 0 | 0 |
| &nbsp;&nbsp;and object dispatch | 195 | +155 | +155 |

The control is the calibration: a frame changes 40 bytes, so a port that does
nothing scores exactly 40, and it does. **Every wrong byte beyond that comes from
the object dispatch.** `irq4_handler` is the only call that is net correct.
`FUN_00003aae` and `FUN_0000144a` change nothing at all, which is itself a finding
— they are supposed to do work.

Of the 40 cells the arcade changed, the port gets 21 right; the other 188 wrong
cells are ones the arcade did NOT change, concentrated in object slots at
consistent offsets (+0x00 flags, +0x10, +0x34, +0x38, +0x50..0x67).

### Three things that had to be built to get here, each a real gap

1. **Every indirect call through a 68K code pointer is a wild jump.**
   `(**(code **)(X))()` reads a 68K program address and calls it; on the SH-2 that
   is nowhere. The first rom executed at PC 0x00000002 and then span in `main_irq`
   for 40% of all cycles — a hang whose only symptom was a stalled counter.
   `--profile` found it in one run. All 6 sites now go through `port_call68()`,
   which looks the address up and **counts a miss instead of jumping to it**.
2. **`object_dispatcher` cannot be used as decompiled** — same pointer problem,
   plus the 68K passed the slot base in A6 and the C call passes nothing, so all
   7,208 `unaff_A6` references would read one global. Supplied by hand in
   `sh_src/port_l2.c`, with the table from `tools/cport_dispatch.py` (139 handlers).
3. **`wait_frame_flag` decompiles to `do { } while( true )`** — Ghidra cannot see
   the IRQ that sets the flag. Stubbed via the new `CPORT_STUBS`.

### The capture point is known, not assumed

`tools/l2_where.lua` samples the 68K PC at every `frame_done`: frames 8995–9005 are
**all** at PC 0x003984–0x003988, inside `wait_frame_flag` (0x0397E–0x0398D). So
frame N to N+1 is exactly one iteration of the loop body, and the ladder above is
comparing like with like.

### Refuted on the way

**MMIO polarity.** S16B inputs are active low, so a zeroed MMIO stub should read as
every button held on both players — a good story for why the player object
diverges. Filling the stub with 0xFF instead changed **not one byte**. This path
does not read those stubs. Recorded because it is a plausible theory that will
occur to the next person.

### What this says to do next

The dispatch is the whole error, and ~5 active handlers produce ~155 wrong bytes —
about 31 each, the size of an object's live fields. The likely cause is the rest of
the calling convention: A6 is restored, but a handler also inherits D-registers from
the 68K code before it, and the port gives every one of those a single global
(`_r_unaff_D3w` and friends) rather than the value that handler actually saw. That
is the L1 problem, and L1 vectors already exist for it.

### Two self-inflicted faults worth remembering

- **A slot collision in the result array, for the third time.** Per-variant results
  occupied 16..25 while the tick count was written to 21, so one variant's "byte
  count" was a tick count — 984, then 219, both plausible. Results now live in a
  reserved block (24..39) with a `'VARM'` marker, and no scalar may enter it.
- **An explicit make rule placed before the default target becomes the default
  goal.** `make line PORTFULL=1` built `port.o`, stopped, and **exited 0** — no rom,
  no error, nothing to notice. The PORTFULL rules now sit at the end of the
  Makefile with that written above them.

### 13b. WHAT THE WRONG BYTES LOOK LIKE (same night)

With the driver frozen after 8 sweeps so an ares dump reads the headline variant's
state — it did not, before: a rom reporting 207 dumped 85 wrong bytes, which was a
single-slot probe's score, and a per-cell analysis of it reached the opposite
conclusion to the one below.

**208 wrong bytes, and only 5 of them are cells the port never wrote.** So the
handlers do run and do write; they write wrong values. The 208 sit almost entirely
in the four active object slots (16: 47, 17: 47, 40: 45, 0: 39) with 20 bytes
scattered through the variable region.

The errors are STRUCTURED, which is the useful part. Slot 0's differing 16-bit
fields:

| field | seed | arcade | ours |
|---|---|---|---|
| +0x0C | 113C | 113**B** | **48**3B |
| +0x36 | 113C | 113**B** | **48**3B |
| +0x10 | 10AB | 10A9 | 11E9 |
| +0x42 | 0000 | 0000 | **3700** |
| +0x46 | 0000 | 0000 | **0140** |
| +0x50 | F010 | F010 | 0001 |

What survives testing, and what did not:

- **The handlers run and write.** 203 of the 208 wrong cells hold a value other
  than the seed. This is not a routine taking an early exit — a routine that
  bailed would leave its fields untouched.
- **A "low byte right, high byte wrong" width fault: TESTED AND NOT SUPPORTED.**
  It is true of +0x0C and +0x36 (both 113B -> 483B, low byte 3B exactly right) and
  it looked like a clean signature. Over all 111 wrong 16-bit fields in the four
  active slots it is 23 high-only, 21 low-only, **67 both** — about what
  undirected wrongness gives. The hypothesis came from six cells; the population
  refuses it.
- **Constants do leak in.** 0x0140 appears written into +0x46, +0x52 and +0x5C,
  all of which should be zero, and 0x3700 into +0x42 and +0x48. 0x11E9 − 0x10A9 is
  also 0x140. Unexplained, and the most specific thing on the table.

So the honest state is: the error is broad rather than a single cause with a
signature, and the one concrete thread is the two leaked constants.

That is the thread to pull first: find what writes 0x0140 and 0x3700 into an
object. It will not account for all 208 — that was the claim the population test
just refused — but it is specific, and specific beats broad when picking a start.

**A second oddity, RESOLVED the same night and NOT a defect.** `FUN_00003aae` and
`FUN_0000144a` change 0 bytes of work RAM. That looked like two of the three frame
calls being inert. They are not: both write TEXT RAM. `FUN_00003aae` clears through
`(undefined *)&DAT_00410000 + _DAT_fffff024` — 0x410000 is the S16B text layer —
and both then call `textram_stride_write()`. Their writes land in the MMIO stub by
construction, so scoring 0 is correct.

**This is a scope limit of the L2 gate, and it should be stated wherever the gate's
number is quoted: it observes WORK RAM ONLY.** Everything the frame does to the
text layer, the tilemaps, the sprite list and the palette is invisible to it. A
perfect 0 would mean the game's *state* is reproduced, not its picture. Two of the
three top-level frame calls are already outside what it can see.

### Harness soundness, established rather than assumed

- The control variant runs nothing and scores exactly 40, the frame's own delta.
- The score is stable across ares frames 600 and 900 (208 both times).
- Every variant re-seeds work RAM **and** the pseudo-registers
  (`port_reset_pseudoregs()`, generated into defs.c). Without that reset a
  variant's score depended on which variants ran before it, and the same sequence
  read 191 in one rom and 207 in another.
- `port_call68()` counts an unknown target instead of jumping to it, so a gap in
  the handler census shows up as a number rather than a crash. It reads 0.

## 14. THE REAL GAP: 977 REGISTER READS, ZERO WRITES (2026-09-28)

This is the finding the L2 gate was built to produce, and it is bigger than the
208 wrong bytes that led to it.

The 68000 passes values between routines in registers, and routines read registers
their own caller never set. Ghidra models the READ side faithfully — it invents a
pseudo-register for each one (`unaff_D3w`, `in_D1`, `extraout_A0`) — and
`cport_prep.py` turns each into a global. Counted in the 351-function frame unit:

| | |
|---|---|
| reads of a pseudo-register (`_r_*`) | **977** |
| writes of a pseudo-register, anywhere | **0** |
| functions reading at least one | **288 of 351 (82%)** |
| by kind | `unaff` 620, `extraout` 267, `in` 90 |

**Nothing in the port ever supplies a value.** Every one of those 977 reads
returns zero. `_r_unaff_A6` is the sole exception, and only because the
hand-written dispatcher assigns it (§13).

Ghidra models the read side but not the write side: when it decompiles the
routine that PRODUCES the value, it expresses it as that routine's return value or
as a local, never as an assignment to the pseudo-register the consumer reads. The
chain is broken at every link.

### Why this explains the L2 result exactly

`FUN_000049ba` — the slot 0 (player) handler, and the worst offender in the
per-slot ladder — declares `unaff_D3w`, `extraout_A0`, `extraout_A1` and
`unaff_A6`. It gets a correct A6 and **zero for the other three**. The oracle says
that is wrong: across the 232 L1 vectors captured AT the dispatcher's own indirect
call site (`caller 0039AC`, 68 distinct handlers), the entry registers vary widely —

    D0   4 distinct    A0  68 distinct
    D1  97             A1 102
    D2 106             A2  49
    D3  64             A6  36   (0xFFC000-0xFFDF00 = exactly the object pool)

A6 taking exactly the pool's values re-confirms the calling convention. D1, D2, D3,
A0, A1 taking ~100 distinct values says they carry real data, not leftovers.

### What this means for the plan

- **A per-function L1 differential works and will keep working** — its harness
  SEEDS the pseudo-registers from the oracle vector, which is why the self-test
  reaches 95 of 99. That number was never evidence that the values flow.
- **A whole-frame run cannot be correct until they do.** This is not a bug to find;
  it is a missing half of the model, and it sits under 82% of the ported functions.
- **The L2 gate is still the right instrument.** It found this in one night, it
  attributes error to a call and a slot, and it will measure the fix.

### The three candidate fixes, cheapest first

1. **Per-call-site register plumbing from Ghidra.** Ghidra knows which varnodes
   cross each call; emit, at each call site, the assignments that set the callee's
   inputs from the caller's values. Most faithful, and it needs a Ghidra script
   that reads the high-level function's call inputs rather than guessing.
2. **Return the outputs.** `extraout_X` (267 reads) is the callee's OUTPUT; a
   function could write `_r_extraout_X` before returning. That covers a quarter of
   the reads and needs no call-site work — but only where Ghidra named the producer.
3. **Seed from the oracle at dispatch.** Cheap, and enough to prove the rest of the
   port correct, but it is a harness, not a port: it cannot ship.

Start with 2 measured against the L2 number, because it is self-contained and the
gate will say immediately whether 267 of 977 is worth anything.

### The number that is NOT in doubt

Frame cost: 680–750 us on ares across every run tonight, ~4.1–4.5% of a vint, and
~7% on hardware at the §11 ratio. Whatever the register plumbing costs, it is
arithmetic on values already in registers. **The game logic fits in a vint with
room to spare, and that was the open question in §6.**

### 14b. THE MAPPING EXISTS AND IS NOW EXTRACTED

`tools/ghidra/reg_convention.py` -> `docs/audit/reg_convention.json`.

Ghidra's function PROTOTYPES are useless here — 0 of 788 have parameters, because
Decompiler Parameter ID was never run on this program. But each HighFunction's
local symbol map carries the STORAGE every pseudo-register came from, and that is
the mapping the port needs:

    FUN_000049ba   extraout_A0 <- A0:4   extraout_A1 <- A1:4
                   unaff_A6    <- A6:4   unaff_D3w   <- D3w:2

**1,637 pseudo-register -> 68K register mappings across 788 functions.** What the
corpus reads as inherited input, by register:

| register | functions | | register | functions |
|---|---|---|---|---|
| A6 | 356 | | D3w | 108 |
| A0 | 287 | | D1w | 44 |
| A1 | 183 | | A5 | 43 |
| D6w | 124 | | A4 | 42 |
| D7w | 112 | | A2 | 34 |

A6's 356 is the one the port already supplies, which is consistent with it working
as well as it does. The other ~1,280 are the gap.

This turns §14's fix (1) from "needs a Ghidra script" into "needs the call-site
half": the callee's inputs are known by name, width and register; what remains is
emitting, at each call, the caller's value for each. Ghidra's PcodeOp CALL
varnodes carry that, and it is the next script.

## 15. THE REGISTER GAP CONFIRMED BY EXPERIMENT (2026-09-28)

§14 was a hypothesis. This is the measurement.

`tools/cport_l2seed.py` takes the arcade's own 68K register file at each
dispatcher call — `tools/l1_oracle.lua` at window 9000, caller 0x0039AC, the same
two frames the L2 gate uses — maps it to pseudo-registers through
`docs/audit/reg_convention.json`, and supplies them at the call. Variants 14 and 15
run the frame with the seed; 4 and 8 are the same frame without it.

| | unseeded | **seeded** | recovered |
|---|---|---|---|
| full frame | 207 wrong | **122** | **85 bytes** |
| slot 0 alone | 57 (+17 vs control) | **29 (−11 vs control)** | 28 bytes |

**Slot 0 with its registers supplied is now better than doing nothing** — it
reproduces more of the arcade's frame than the control's 40, where before it was
17 worse. The control is unchanged at 40 throughout, so the comparison is sound.

**It took three assignments.** Only `FUN_000049ba` (slot 0) reads anything besides
A6; the other three handlers in this frame read A6 alone, which the driver already
supplied. So `extraout_A0`, `extraout_A1` and `unaff_D3w`, on ONE handler, are
worth 85 of the 208 wrong bytes.

That 85 is larger than slot 0's own 17-byte contribution, and the excess is the
point: with correct registers slot 0 leaves different global state, and the
handlers dispatched after it inherit it. **The register chain is real and it
carries across handlers.** That is the mechanism §14 said was broken at every link,
now visible working across one link.

### What this does and does not establish

- **Establishes:** the missing register values are the dominant cause of L2
  divergence, not wrong decompilation, not the dispatcher, not address
  translation. §14's fix is the right work.
- **Does not establish:** that 122 goes to 0 by the same route. 122 bytes remain
  with the registers of this frame's four handlers correct, and the next question
  is whether they are further register reads deeper in the call graph (likely —
  977 reads sit across 288 functions, and these four handlers account for a
  handful) or something else.
- **The seed cannot ship.** It is one frame's values read from an oracle. It is an
  instrument for proving the port, exactly like the L1 harness that seeds the same
  globals to reach 95 of 99.

### The order of work this sets

1. **Call-site register plumbing** (§14 fix 1). Now justified by measurement
   rather than argued from a code reading. `docs/audit/reg_convention.json` has
   the callee side: 1,637 mappings, name, width and register. The caller side is
   Ghidra's PcodeOp CALL varnodes.
2. Re-run the L2 gate. It will say what the plumbing is worth, in bytes, the same
   afternoon it is built.
3. Only then worry about what remains.

Frame cost is unchanged by any of this: 680–750 us on ares, ~7% of a vint on
hardware at the §11 ratio.

## 16. THE PLUMBING, BUILT AND MEASURED (2026-09-28, overnight)

§15 proved the register gap was the dominant cause. This is the general fix, and
the gate's verdict on it.

### The mechanism: declare the producer's return storage

A 68K routine whose whole purpose is to compute a register value **decompiles to
nothing**, because the value is never used inside it and no prototype says it is an
output. `sprite_frame_lookup` is 34 bytes of code and Ghidra rendered it:

    void sprite_frame_lookup(void) { return; }

Declaring its return to live in A0 (`tools/ghidra/set_ret_storage.py`) makes the
decompiler materialise the computation:

    int sprite_frame_lookup(void)
    { return *(ushort *)((uint)*(ushort *)(unaff_A6 + 6) * 6 + 0x255e0) + 0x255e0; }

A real table lookup — object field +6 indexing a 6-byte-stride table — which is
exactly the `extraout_A0` its caller was reading as zero.

### Finding every producer exactly, not by heuristic

`tools/ghidra/extraout_producers.py`. Ghidra's SSA knows which op defined each
`extraout_` value. **It is not the CALL** — measured, 375 of 375 are defined by an
`INDIRECT` op, Ghidra's "a call may have changed this", whose `input[1]` is an
`iop` naming the real CALL's sequence number. Following that:

**375 extraout sites, from only 56 distinct producer functions.** Return storage
was declared on 47 of them (6 need a 12-byte three-register type that has no
`undefined` equivalent; 3 had unnamed storage). Result in the corpus:

| | `extraout_` occurrences |
|---|---|
| before | 912 |
| after 47 declarations | **292** |

A 68% reduction, and in the port unit `_r_*` reads fell 977 -> 777.

### Two port-side fixes this exposed

1. **`cport_prep` was truncating packed multi-register returns.** Ghidra expresses
   two registers left by a routine as ONE 8-byte value that the caller unpacks
   (`uVar12 = zoom_scale_lookup(); (uint)uVar12` and `(ulonglong)uVar12 >> 0x20`).
   Normalising every signature to `uint NAME()` kept the low register and silently
   discarded the high one — which is the value the caller wanted. Wide returns are
   now preserved as `ulonglong`; 10 functions in the frame unit need it.
2. **A bare 68K program address used as a pointer base reads garbage.** Ghidra
   emits the 68000's absolute-long addressing verbatim, and the existing `W68`
   macro does not cover it — `W68` handles the absolute SHORT form, a 16-bit
   displacement, and these bases are wider. 53 sites now get `ROM_BASE`. The
   detector had to strip nested sub-expressions first: a literal inside a nested
   load is an index, not a base, and getting that wrong missed 0x255E0 entirely.

### The verdict

| build | corpus | bytes wrong |
|---|---|---|
| §13, no plumbing | 788 functions | **207** |
| + extraout plumbing | 699 functions | **186** |
| + ROM-base translation | 699 functions | **187** |

- **The plumbing is worth 21 bytes**, and it earns that while carrying a corpus
  with 89 FEWER functions (the re-analysis incident, §LESSONS). Net real.
- **The ROM-base translation moved nothing** (186 -> 187, inside noise). It is
  correct — reading an untranslated 68K address is wrong by construction — but
  those 53 sites are not on this frame's path. Kept as correctness, claimed as
  nothing.
- **21 is far short of the 85 the oracle seed recovered in §15.** The difference
  is the point: the seed supplied the CORRECT values; the plumbing supplies
  COMPUTED ones, and they are computed from a chain whose inputs are still zero.

### Where the remaining error actually is

The oracle seed is now a NO-OP: 187 seeded, 187 unseeded. It has nothing left to
supply, because `FUN_000049ba`'s `extraout_A0/A1` are now plumbed and its only
other input is A6, which the dispatcher sets. So the residual error is **not** in
the registers the seed covers.

And the per-slot ladder says where it is: slots 16 (+41), 17 (+46) and 40 (+37)
dominate, and **their handlers read only A6**, which is correct. Inputs right,
output wrong — a translation defect in those bodies or their callees, which is
exactly what an L1 per-function differential is for, and the vectors for the
dispatcher's own call site already exist (232 of them, `caller 0039AC`).

`unaff_*` remains the largest unplumbed class (902 mappings). It is NOT
call-produced — it comes from further up the chain — so `set_ret_storage` does not
reach it, and the next mechanism is different work.

## 17. THE PORT WAS READING A CART FULL OF ZEROS (2026-09-28, late)

The single worst defect found so far, and it had been silently in place under every
number in §13–§16.

`.portrom` — the 256 KB arcade program image the port reads its constant tables out
of — **was 256 KB of zeros in the rom.** Not misplaced, not misaligned: empty.

    prog68k.bin            223,663 nonzero bytes of 262,144
    romimg.o (objcopy'd)       200 nonzero bytes of 262,616  -- all ELF header
    .portrom in the ELF    all zeros
    rom file at 0x340000   all zeros

`sh-elf-objcopy -I binary -O elf32-sh -B sh2 --rename-section .data=.romimg,...`
produced a section with the right SIZE and the right FLAGS (`CONTENTS, ALLOC,
LOAD, READONLY, DATA`) and **no data in it**. `objdump -h` reports it as having
contents; `objdump -s` shows zeros. Replaced with `.incbin` in
`sh_src/port_rom_data.s`, which is what every other blob in this tree already uses
and which demonstrably works — `.portdata`'s 32 KB of oracle capture arrives fine.

### What it was hiding, and the honest accounting

With the image present, the object state machine finally reads its real jump table
(`&DAT_0000f556` -> 0x0000EFD2 rather than 0), and `port_call68` misses fell from
**208 to 6**.

**And then two handlers hang.** `FUN_00016bf0` (slot 40), then `FUN_0000ee80`
(slot 17). Both had been terminating early *because their table reads returned
zero*. So part of the 186-wrong result in §16 was correct-by-not-executing, and the
current rom measures nothing at all: a hang means the driver never reaches its
compare, so every counter reads zero.

That is a REGRESSION IN THE NUMBER and a step FORWARD in the port. The previous
number was partly an artefact of a second bug. There is no way to have both the
old number and the fix.

### The instrument was extended rather than the fix reverted

- `port_l2_out[67]` counts handler calls skipped as known-hanging, and the reader
  prints *"the frame is INCOMPLETE"* whenever it is nonzero, so a number from a
  partial frame can never be read as a whole one.
- `port_call68` now carries a SITE ID, generated by `cport_prep`. All 208 misses
  reported target 0x00000, which identifies the value but not which of the six
  indirect sites read it; three rounds of guessing from the target alone got
  nowhere, and the site id named it immediately (site 3 = the state table).
- Canaries either side of `port_l2_out` confirm nothing wrote through it. They read
  intact, which retired a real worry: `port_l2_out[12]` had once reported 93,168
  handler returns against 40 entries.

### Two more real defects fixed on the way

1. **`PTR_*` and `switchD_*` symbols were fabricated as zeroed 64-byte arrays.**
   `cport_layout` places symbols by matching `(DAT|xxRam)_<hex>`; `PTR_FUN_00006d90`
   does not match, so it fell through to `defs.c` as `byte PTR_FUN_00006d90[64];`
   in .bss. Those are ROM jump tables. 23 are now placed from
   `docs/audit/sym_addrs.txt` — the address cannot come from the name, because
   `PTR_FUN_00006d90` encodes its TARGET, not its own location.
2. **A 32-bit access at a 2-byte-aligned address is an SH-2 ADDRESS ERROR.** The
   68000 allows it and this program uses it freely: **147 of the 302 symbols the
   port places sit at an address that is 2 mod 4**, the object state jump table
   among them. 63 loads and 217 stores now go through packed accessors so gcc
   emits byte-wise code. This is almost certainly also what put an earlier build
   at PC 0x00000002 spinning in `main_irq` for 40% of all master cycles.

### The one number that has not moved all night

Frame cost: 680–860 us on ares across every build, 4.1–5.1% of a vint. The
byte-wise accessors cost about 90 us of that. At the §11 hardware ratio it is
still under 8% of a vint.

### Next, in order

1. **The two hangs.** `FUN_00016bf0` and `FUN_0000ee80`, both now running real code
   for the first time. This is a per-function problem with a per-function
   instrument (L1) and 232 oracle vectors already captured at the dispatcher's own
   call site.
2. `0x0EFD2` still misses despite being in the unit and in the 425-entry dispatch
   table — unexplained, and worth one look before assuming the table is right.
3. Re-measure. Every number in §16 needs re-taking once the frame completes again.

## 18. EVERY INDIRECT CALL RESOLVES, AND THE RESIDUAL TRACES TO ONE MECHANISM (2026-09-29)

Continued from §17's two hangs. `ares --profile` named the first in one run:
`floor_collide` at **87.6% of all master cycles**, walking a table whose base is a
68K pointer loaded from ROM data and used untranslated. That opened a chain of
five real defects, each measured:

| fix | what it was | effect |
|---|---|---|
| `P68()` on pointers loaded from data | 22 sites; the value arrives at RUNTIME, so no source rewrite can catch it | both hangs gone, frame completes |
| nested 32-bit loads | `LD32U(LD32S(X) + Y)` is a double indirection and the inner result is an address too | last 80 misses, all target 0 |
| two-level jump tables | `&PTR_FUN_00006da0` holds pointers to TABLES of function pointers; harvesting one level left 142 targets unreachable | **misses 208 -> 0** |
| `ST32` value extent | scanned to the next `;`, but an assignment is not always a statement — Ghidra emits comma operators inside conditions, so it swallowed the rest of the condition AND the block's opening brace | 443 errors -> 0 |
| `P68()` on pointer RETURNS | a function whose return lives in an A register returns a 68K address; 5 sites | 185 -> 176 wrong |

**Corpus 846 functions, unit 603, 388,828 B text, 0 compile errors, 0 missed
indirect calls, no hangs, canaries intact, 176 of 16,384 bytes wrong (from 207).**

### The instrument that made this tractable

The game writes a progress marker to 0xFFFEF4/F6 before each call, and
`tools/l1_oracle.lua` captures it — so the arcade's exact path through a handler is
known. **Ghidra drops those writes as dead stores**, so the port cannot reproduce
them; `PORTTRACE=1` builds the port unit with `-finstrument-functions` and logs
`this_fn` into a ring (`sh_src/port_l2.c`), which gives the same information from
the other side.

Diffing the two traces for slot 16 corrected a conclusion I had already reached by
reading control flow:

    arcade:  FUN_0000ee80+6 (restore_position) -> +194 (state dispatch) -> 10-call tail
    port:    FUN_0000ee80 -> restore_position -> FUN_0000efd2 -> ... -> FUN_0000669c -> ...

**They match.** I had inferred from the source that the port early-returned before
`FUN_0000669c` and skipped the tail. It does not. The control flow through that
handler is already correct and **only the values are wrong** — which is a much
better position than the reading suggested, and it was only knowable by measuring.

### Where the residual actually lives, traced end to end

Slot 16's `+0x0c` should end the frame at 0x1263 and ours reads 0x126A. The trail:

1. `restore_position` writes 0x1269 — and the ORACLE shows the arcade writing
   exactly that (`0xFFC80C <- 0x1269`), so the port is faithful here.
2. `FUN_0000669c+144` then writes 0x1263 in the arcade — `+0x0c += sVar8` with
   sVar8 = -6.
3. `sVar8` comes from `*(short *)(pbVar9 + 1)`, and `pbVar9` from
   `(byte *)FUN_0000673e()`.
4. `FUN_0000673e` is four bytes of code:
   `return (short)(in_D0w * 0x10) + in_A0;`

**It is a pure function of two inherited registers, and in the port both read
zero.** So it returns 0, `P68(0)` lands at the start of ROM, and the animation
script read from there is meaningless.

That is §14's gap in its purest form: `extraout_*` is now plumbed (§16, declaring
the producer's return storage), but `in_*` and `unaff_*` — **902 mappings** —
require the CALLER to supply the value AT THE CALL SITE. No amount of work on the
callee side reaches them.

### So the next step is unchanged but now proven necessary by a single value

Call-site register plumbing. The callee side is already extracted
(`docs/audit/reg_convention.json`: name, width and register for every one). The
caller side is Ghidra's PcodeOp CALL varnodes. `FUN_0000673e` is the ideal first
test: 4 bytes, two inputs, and its output is checkable against the oracle's own
write of 0xFFC80C.

Frame cost with the tracing probe on is 1047 us; without it, 747-782 us, 4.5-4.7%
of a vint, unchanged all night.

## 19. CALL-SITE REGISTER PLUMBING: 176 -> 49 WRONG (2026-09-30)

§18 ended with the residual traced to one mechanism: `in_*` / `unaff_*` registers,
902 mappings, all reading zero, and no way to recover the caller's value from the
decompiled C. This closes it.

### Why the value was not recoverable, and what made it recoverable

**The decompiler DELETES the argument setup.** The prototype says the function
takes nothing, so the instructions that load D0 and A0 before a call are dead code
and are folded away. Measured: the high-level SSA at both call sites of
`FUN_0000673e` shows no D0 and no A0 at all — `tools/ghidra/call_inputs.py` found
5,276 direct calls and could not name the arguments at any of them.

Declaring the PARAMETERS is what brings the setup back, exactly as declaring the
return storage brought back the producer's body in §16:

    int FUN_0000673e(undefined4 p_D0,int p_A0)
      { return (short)(p_D0._2_2_ * 0x10) + p_A0; }
    -- and at the call sites:
      pbVar12 = (byte *)FUN_0000673e(p_D0,puVar1 + 1);
      pbVar11 = (byte *)FUN_0000673e((uint)unaff_A6[0x21],(int)puVar1 + 3);

**The requirement is transitive and needs a fixpoint.** Declaring a callee's inputs
reveals that the caller inherits them too. `tools/ghidra/param_fixpoint.sh` iterates
to convergence: **549 functions with register parameters**, stable at round 2. The
set must ACCUMULATE — a set rebuilt from scratch each round drops what the previous
round declared (a declared register stops being a pseudo-register), and the count
oscillated 153/161/159/163 forever until that was fixed.

### The result

| | bytes wrong of 16,384 |
|---|---|
| §13, before any of this | 207 |
| §18, after the pointer and dispatch work | 176 |
| **§19, with register parameters** | **49** |
| control — a port that runs nothing | 40 |

The ladder, and this is the part that matters:

| | vs control |
|---|---|
| `irq4_handler` | **−6** |
| `FUN_00003aae`, `FUN_0000144a` (text writers) | 0 |
| object dispatch | **+15** (was +150) |
| **slot 0 alone — the player** | **−23** |
| slot 40 alone | −1 |
| slots 16 / 17 | +10 / +6 (were +40 / +45) |

**The player's handler now reproduces more than half of everything the arcade
changed that frame, and is net correct.** Object dispatch as a whole is within 15
bytes of neutral where it was 150 wrong.

### Three bugs the change exposed, each of which read as a RESULT

1. **`(ushort)x != -1` is always true.** The 68000's `dbra` tests a 16-bit counter
   against 0xFFFF; Ghidra writes `(*(ushort *)X) != -1`, and in C the ushort
   promotes to int so the comparison can never hold. `textram_stride_write` is a
   five-line string copy whose whole loop is that test, and it took **89% of all
   master cycles**. Only the unsigned forms are wrong — 106 of the 110 comparisons
   against -1 in the unit are on signed shorts and are correct.
2. **`cport_dispatch.py` matched `^uint NAME()`**, which stopped matching when
   functions gained parameters. The table fell from 425 entries to 202, every
   object handler became an "unknown handler", and the frame scored **34 of
   16,384 — better than the control — purely because the game logic had stopped
   running.** A number that improves because the work stopped is the most
   dangerous kind, and only `dispatch entered 0` gave it away.
3. **`CPORT_STUBS` matched the same stale pattern**, so `wait_frame_flag`'s stub
   would silently not apply and the rom would hang in a decompiled infinite loop.

### What the dispatcher does NOT get

`port_handlers` is now an unprototyped table and the dispatcher calls with no
arguments. The 68K entered these through a pointer with A6 set and whatever else
the caller happened to leave; the port cannot know that, so it passes nothing —
the same behaviour as before parameters existed. **The win is at DIRECT calls,
where the caller does know.** Supplying an object handler's own entry registers is
a separate question and the oracle already has the answer for it: 232 vectors at
the dispatcher's own call site record the full register file.

## 20. THE MULTI-FRAME GATE: THE PORT DOES 79% OF THE WORK AND SCRIBBLES (2026-09-30)

Mike's correction, and it reverses HANDOFF 5 §0: **"We can't have a useful play
candidate until the SH-2 port of the 68K code is running. full stop otherwise."**
So this port is the critical path, and the question the gate must answer is whether
it SUSTAINS -- not whether it is nearly right once.

§19's 49 was one frame. This is thirty.

### The rig

`tools/l2_pack.py` packs 31 consecutive captures (`mame altbeast -playback ab_full`,
frames 9000-9030) into a seed plus per-frame DELTA records -- 4,764 bytes, because
a frame changes 28-48 of 16,384 bytes. `.portdata` is 32 KB and the two full images
already filled it; 30 more images would want 500 KB.

`port_l2m_run()` runs two curves and `tools/portl2m_read.py` reads them:

    FREE-RUN    seed ONCE, run 30 frames, never re-seed. Error compounds.
    PER-FRAME   re-seed from the arcade's frame k-1 each time. Independent.

No 16 KB expected buffer -- there is 1.4 KB of headroom under the region guard.
`expected(k)` is reconstructed in one ascending pass with a cursor per frame's
(sorted) record list: a k-way merge over RECORDS, not cells, so the cost is
O(16384 + records*k) and the RAM cost is 128 bytes of pointers. The driver code
rides `.portcode` on the cart: ~2 KB of it in `.text` moved `.ramtext`'s ROM image
into `.tilesmd` and the link failed.

### The result

    PER-FRAME   47..53 wrong, mean 48.6    control (does nothing) mean 37.7
    FREE-RUN    49 -> 134 over 30 frames   control 53.1, slope +2.9 B/frame

**The one-frame 49 generalises**: every one of 30 frames scores 47-53. It was not a
lucky frame. 30 frames run with no hang and **zero missed indirect calls**.

### The decomposition, which is the actual finding

`wrong` alone cannot separate two opposite failures. Split by whether the arcade
touched the cell:

| per frame | |
|---|---|
| the arcade's own writes | 37.7 |
| of those, we MISS | **8.1** |
| so we reproduce | **29.6 of 37.7 = 79%** |
| cells the arcade left ALONE and we wrote | **40.5** |

**The port is not failing to do the work. It does 79% of it and then writes ~40
cells the arcade never touched.** "Net harmful by 11 bytes a frame" and "does 79%
of the frame correctly" are the same measurement; only the second one points at a
fix.

### The spurious cells name themselves

Last per-frame frame, first 32 spurious cells:

    objects  FFC000  FFC800 FFC812 FFC81B FFC82C  FFC880 FFC892 FFC89B FFC8AC
             FFD412 FFD41B FFD42C
    sprite   FFEC80-82  FFECC0-C2 FFECC4-C6  FFED00 FFED11 FFED20-22  FFED40 FFED44
    globals  FFF01C  FFF900 FFF903 FFF904

The object-table ones are **the same three fields on every active slot** --
`+0x12`, `+0x1B`, `+0x2C` on slots 16, 17 and 40 -- plus **`+0x00`**, the byte whose
bit 7 is the slot's ACTIVE flag (`port_object_dispatch` tests it). Writing that byte
when the arcade does not is the likeliest source of the free-run divergence: it
changes which handlers run on the next frame.

That is three fields and one flag byte, not forty independent defects.

### A measurement limit worth stating

The 31-frame run differs from the 2026-09-27 capture pair by 30 bytes, **all of them
at 0xFFFF00+**, on the same playback file. That region is not reproducible between
two MAME runs, so the gate can never score it to zero. It is currently 0 wrong in
the free-run curve, so nothing is lost -- but a future number there should not be
chased.

### Next

1. **Find what writes object `+0x00`, `+0x12`, `+0x1B`, `+0x2C`.** `l1_oracle.lua`
   records per-call writes, so the arcade's answer for "who writes +0x12" is a
   query, not an inference. Ours is `PORTTRACE=1`.
2. The sprite-list block `FFEC80-FFED44`.
3. Only then the 232 dispatcher register vectors (HANDOFF 5 §5.1): 8.1 missed
   cells a frame is now the SMALLER half of the error.

## 21. TWO HYPOTHESES KILLED IN AN HOUR, AND ONE INSTRUMENT LABEL CORRECTED (2026-09-30)

§20 left three leads. Two are now dead and the third is sharper.

### A software watchpoint names the writer, and it is one function per cell

327 functions in the unit write object `+0x2C`, so a static grep cannot answer "who
wrote this cell". `-finstrument-functions` already brackets every function in the
port unit (`PORTTRACE=1`), so: save the watched byte on ENTRY, compare on EXIT, and
because **callees exit first, the FIRST exit to report a change is the innermost
writer.** 20 lines, in `sh_src/port_l2.c`.

    cell     innermost writer              change
    FFC812   FUN_00003f32                  0x00 -> 0x40
    FFC81B   clamp_y_velocity              0x00 -> 0x40
    FFC82C   FUN_0000ee80 (slot 16's own)  0x10 -> 0x00
    FFC800   sprite_build_depth_banded     0x80 -> 0x88   <- the ACTIVE flag byte

### DEAD: the IRQ4 ordering hypothesis

`FFC800 0x80 -> 0x88` is a flag bit set during the dispatch, and the capture
boundary is the frame-flag barrier that the vblank interrupt releases -- so IRQ4
landing AFTER the dispatch looked as plausible as before it, and had never been
tested (the ladder only ever tested first-vs-absent, 49 vs 55).

Measured over all 30 frames: **identical to the byte.** 48.6 mean, 8.1 missed, 40.5
unchanged-but-wrong, both ways. A result that identical is the shape of a flag that
never took effect, so the branch counts itself: `[86]` 64 frames ran IRQ4 first,
`[87]` 30 ran it last. The branch ran. **The order genuinely does not matter** --
IRQ4's writes do not interact with the dispatch's.

### CORRECTED: "spurious" did not mean what the tool said it meant

§20 called those 40.5 cells "cells the arcade left ALONE and we wrote". **That is
wrong**, and `l1_vectors.jsonl` refuted it in one query: across 669 captured calls
the arcade writes

    FFC800  46 times      FFC880  28       FFC812  18       FFC81B  16
    FFC82C  10            FFC892   6       FFC89B   7       FFC8AC   6

and still ends the frame where it started. The cells are in the arcade's write set;
what it does not do is CHANGE them by the frame boundary. The honest label is
**UNCHANGED-BUT-WRONG**, and the tool now says that.

This matters for what the fix is. The port is not calling code the arcade does not
call -- `FFC800` is written by the arcade too, and `0x88` is a value it writes there
(to slot 17, from `FUN_00669C`). The port is reaching a different VALUE, which is
the same class of defect as §18's residual, not a control-flow error.

### Next

1. **`FUN_0000ee80` writing `+0x2C` 0x10 -> 0x00 on slot 16.** The arcade writes
   that cell too (10 times) and ends at 0x10. The watchpoint names the function;
   `l1_vectors.jsonl` holds the arcade's entry registers for `00EE80` at caller
   `0039AC`, so this is a single-function diff with both sides available.
2. The three remaining cells, same method.
3. The sprite-list block `FFEC80-FFED44` (26 cells, one region).

## 22. ONE DECLARED WIDTH: FREE-RUN GOES NET CORRECT (2026-09-30)

§21 left four named cells and one named writer each. Chasing the first one found a
defect class, and fixing one symbol moved the whole gate.

### The chain, because every step of it was a measurement

1. `FUN_0000ee80` zeroes `+0x2C` (its line 96) and `floor_collide`, six calls later
   in its tail, is supposed to OR `0x10` back in. The multi-frame gate said `+0x2C`
   ends `0x00`; the arcade ends `0x10`.
2. An ENTRY SAMPLER on `floor_collide` (fires from the `-finstrument-functions`
   hook when `this_fn` matches) said **all four of its conditions pass** on slot 16.
   An EXIT sample said `+0x2C` was still `0x00` on the way out. Both instruments
   agreed, and they contradicted each other's implication, which is what forced the
   next step instead of a guess.
3. The operand I had NOT taken from the code's own expression was the table. The
   code reads `&PTR_DAT_0000dec4 + (DAT_fffff142 << 2)`.
4. **`dat_syms.h` declared `DAT_fffff142` as `ushort`.** The cell holds `01 00`. As
   a byte the index is 1; as a ushort it is `0x0100`, and `<< 2` is `0x400` -- one
   kilobyte past a FIVE-entry pointer table. `floor_collide` read a garbage
   collision list, its loop exited on the first test, and the branch never fired.

Three of the four cells §21 named were exactly the writes that branch makes:
`+0x2C |= 0x10`, `+0x12 = 0`, `+0x1A = 0`. All three are now correct.

### The evidence that the declaration is wrong, not the fix

Two independent arguments, and the fix requires both:

- **The arcade's own access width.** Every read and write in `l1_vectors.jsonl`
  carries a width; `tools/dat_width_observed.py` reduces 669 vectors to the maximum
  observed width per address. `FFFFF142` is only ever accessed **1 byte** wide.
- **A second declared symbol inside the span.** `DAT_fffff143` is declared `byte`
  with 6 refs. Two symbols, one 16-bit cell.

Either alone proves nothing. A 68000 may legitimately `move.w` at `0xFFF142` and
`move.b` at `0xFFF143`, so overlap is not evidence; and the oracle is one window, so
"never seen wide" is a ceiling and not a fact. Together the wide reading is
impossible. And for this symbol there is a third, decisive argument that needs no
oracle at all: the value indexes a five-entry table, so an index of `0x400` is out
of range **by construction**.

### The result

| | per-frame mean | free-run mean | free-run slope | objects region |
|---|---|---|---|---|
| §20 | 48.6 | 104.4 | +2.9 B/frame | 76 wrong |
| **§22** | **40.6** | **52.0** | **+0.6 B/frame** | **3 wrong** |
| control (does nothing) | 37.7 | 53.1 | | |

**WITHDRAWN 2026-09-30, SAME DAY: "the free-run curve is NET CORRECT" was a
WINDOW ARTIFACT.** It read 52.0 against a control of 53.1 at 30 frames with a slope
of +0.6 B/frame. At 256 frames (§23) the same rom reads **305 against a control of
236** and the slope is not flat. The port tracks the control for roughly the first
65 frames and separates from about frame 81. The three named cells and the object
region ARE fixed -- that part stands, and it is why the first 65 frames track -- but
the sustained claim was 30 frames' worth of evidence for a 4-second question.

Per-frame at 30 frames was +2.9 over doing nothing, from +10.9. At 256 frames it is
**+12.0 on the mean and +15 at the last frame.**

### WHAT THIS COST, AND THE TRAP IN IT

The selection rule picks **11** symbols, and clamping all 11 took the free-run curve
to 179 while still fixing those three cells -- one correct fix riding with several
wrong ones. Two of the 11 (`FFFFD400`, `FFFFD580`) are inside the OBJECT TABLE, a
64-entry array of 0x80-byte records: a declared symbol there is one FIELD and its
neighbour is another field of the same record, so "a declared symbol sits inside the
span" is true of nearly every field and carries no information. The object table is
now excluded structurally, and the rest is opt-in per address via `CPORT_CLAMP` so
the gate arbitrates one symbol at a time. Default is the one proven address.

**A fix that is right can still be swamped by a rule that is too broad.** The gate
is what told them apart, and only because it runs 30 frames.

### Next

1. The remaining error is concentrated: `0xFFE000` (the sprite list) 26 cells and
   globals 29. `FFC800` -- `sprite_build_depth_banded` writing `0x88` where the
   arcade ends `0x80` -- is the last of the four original cells.
2. The other 9 clamp candidates, one at a time through `CPORT_CLAMP`. Each is a
   one-build experiment the gate ranks.
3. The 12 MISSED cells are all globals: `FFF02B/2D`, `FFF0E3/E5`, `FFF121/125`,
   `FFF133/134`, `FFF800/801/803`, `FFF913`.

## 23. THE GATE WAS TOO SHORT, AND IT PRODUCED A FALSE VERDICT (2026-09-30)

Mike asked "why 30?" There was no answer: I picked it. Two things followed.

### Where 30 came from

`30` is this repo's house unit for a sampling window (`MESSAGES.md:852`,
`LESSONS.md:5520`, `ORACLE-PLAYTHROUGH.md:71`, `BOSSFIGHT.md:799`) and it is
promoted into **`FACTS.md:655` and `:658`** -- the lookup file CLAUDE.md instructs
you to grep before deriving anything, so the number arrives pre-approved. The record
carried the caveat in the same paragraph (*"23 windows x 30 frames is 690 of 103,000
frames, 0.67%"*) and only the unit was inherited.

And Mike's own reading is the larger anchor: **30 is the CADENCE BARRIER this port
exists to break** (§1: "the cuts path cannot reach 60Hz"; 30Hz appears on 94 lines
of the docs). Borrowing it as a sample size collided with the project's most loaded
number. In LESSONS as "A HOUSE UNIT IS NOT A JUSTIFIED SAMPLE SIZE".

### The length is now derived from two measurements

    CART     .portdata plus the cart's spare holds 574 frames of delta records at
             159 B/frame. Freeing the redundant second full image (it IS the seed
             with delta frame 1 applied) paid for 16 KB of that.
    SCORER   l2m_score's merge is O(records x k), so the per-frame curve is O(k^2):
             512 frames would not finish before a reasonable dump frame.

The scorer binds, so the dense run is **256 frames = 4.3 s at 59.92 Hz**. Endurance
is separate and scored host-side at its endpoint, which costs no cart space.

### What 256 frames says that 30 could not

    FREE-RUN   port 41 -> 305      control 236 at the last frame   net +69
    PER-FRAME  port 41 ->  70      control  55                     net +15

**The control is NON-MONOTONIC and the port is MONOTONIC.** The arcade's state
wanders back toward the seed (control 184 at f65, 122 at f97, 261 at f161, 204 at
f225); the port's error only ever grows. A mean-against-mean comparison hides that
completely, which is why the reader now prints port against control AT THE SAME
FRAME.

The port tracks the control closely for the first ~65 frames (net -15 to +11) and
separates from about f81: +56, +80, +103. **Half a second of evidence said flat and
net correct; four seconds says diverging.**

### Two instrument bugs this exposed, both of the worst kind

1. **The reader summarised an unfinished run.** The summary slots are written after
   the loop, so a partial run reads 0, and a mean of 0 against a control of 156
   printed **"NET CORRECT"** for a curve that was in fact diverging. Same shape as
   §19's frame scoring better than the control because the logic had stopped. The
   reader now refuses to summarise unless `completed >= available`.
2. **One verdict word for two different curves.** Free-run can accumulate, so a
   rising net is divergence; per-frame re-seeds every frame and CANNOT accumulate,
   so a rising net there means later game states are harder -- a different defect
   needing a different fix. The reader now says which.

### Next

The per-frame curve steps at identifiable frames -- f113 (60, control 34), f201 (98,
control 52) -- with no inherited error, so whatever the game does there is something
the port gets wrong on its own. Those frames are the next target, and the free-run
MISSED term (168/frame, now dominant) says the drifted state stops the port doing
the arcade's work at all.

## 24. THE ARRAY-BASE WIDTH RULE, THREE DAYS LATE: PER-FRAME GOES NET CORRECT (2026-09-30)

§23's 256-frame gate said the port was +12.0 bytes a frame worse than doing nothing
on the per-frame curve. It is now **-2.2: net correct across all 256 frames.**

### The fix was already written down and never applied to the port

`LESSONS` 2026-09-27, "Two more width rules from the same root": **a symbol used as
an ARRAY BASE indexes in its own element size.** Ghidra's width is a whole-symbol
inference -- something else in the program reads a long at the address, so it reports
4 B -- but when the body does `(&DAT_x)[i]` the symbol is the base of a BYTE table
and a `uint` declaration scales every index by four.

`tools/gen_selftest.py:665` has implemented it since that day, for the **L1 self-test
harness only**. `cport_prep.py`, which generates the port's own `dat_syms.h`, had no
array-base rule at all. So the port carried the exact bug the lesson describes:

    extern uint DAT_ffffec80;        and the body does (&DAT_ffffec80)[iVar1] = 0xff

and `0xFFEC80`, `0xFFECC0`, `0xFFED20`, `0xFFED40` -- the sprite-list block §20
flagged and §22 left open -- were four of its stores landing four times too far out.

### The result

| | per-frame mean | vs control | free-run last | UNCHANGED-BUT-WRONG cells |
|---|---|---|---|---|
| §23 | 51.8 | **+12.0** | 305 vs 236 | 17 |
| **§24** | **37.6** | **-2.2 NET CORRECT** | 290 vs 236 | **1** |

Per-frame last frame is 55 against a control of 55: **dead level.** The
unchanged-but-wrong class is down from 17 cells to one (`FFF01C`) -- the sprite-list
block is gone exactly as the rule predicts, and the 7.5 cells/frame that remain in
that class were 21.8.

**Free-run still diverges**: 290 against a control of 236 at the last frame, net +54
(was +69), and `MISSED` is 160/frame. Each frame in isolation is now right; the
port still misses ~30 cells a frame, and free-running those feed forward.

### Why it took three days, and it is not only my fault

`build_facts.py` indexed `###` headings and `Addendum` blocks from LESSONS and used
`##` headings only to set the section NAME. **56 of the 67 `##` headings were absent
from FACTS.md** -- the lookup CLAUDE.md instructs you to grep before deriving
anything -- so `grep -i width FACTS.md` returned seven lines and none of them this
rule. I re-derived the principle from scratch, and the grep that was supposed to
stop me could not have. Fixed: FACTS.md 870 -> 937 facts, and
`grep -i 'width rule' FACTS.md` now lands on `LESSONS.md:7712`. Recorded in LESSONS.

**Two generators, two width decisions, one of them three days out of date.** The
general rule is single-source the decision; `gen_selftest.py` and `cport_prep.py`
still each have their own and should not.

### Next

The per-frame `MISSED` term, 30.1 cells/frame, is now the whole remaining error and
it is concentrated: the missed cells at the free-run endpoint are
`FFC000 FFC007 FFC00D FFC00E FFC014..FFC063` -- **slot 0, the player**, handler
`0x049BA`. The object region holds 207 of the 290 wrong cells.

### 24a. The other nine width candidates are LOW priority, by static triage

The scalar-overlap rule (§22) selects 11 addresses; one is fixed (`FFFFF142`), two are
object-table fields and excluded structurally, and the remaining nine were queued as
"one build each, gate-arbitrated". **Static triage removes them from the queue
instead.**

`FFFFF142` was catastrophic for one reason: it is used as `(DAT_fffff142 << 2)`, a
SCALED index into a five-entry pointer table, so a 2-byte read makes the index 0x400
and the read lands a kilobyte out. Checking every remaining candidate for that
pattern -- the symbol immediately followed by `<<` or `*`, or subscripted -- in the
port's own source:

    DAT_ffffd422  8 refs    DAT_ffffe800  5    DAT_fffff028 44    DAT_fffff148  7
    DAT_fffff14a 11         DAT_fffff156  8    DAT_fffff404  0    DAT_ffffff80  4
    _DAT_fffff032 24        DAT_fffff400 24

**None of them is used in scaled or index arithmetic.** A width error on any of these
is a plain value-width error -- the value reads too large by 256x plus its
neighbour -- which is a real class but not a pointer landing out of range, and it is
exactly the class where the overlap argument is weakest, because a 68000 may
legitimately access a word and its low byte as separate symbols.

Nine build-and-measure cycles saved. If one of them turns up as a specific wrong
value later, it has a mechanism and a test then.

### 24b. The gate's cost number was wrapping

`FRT16()` is a 16-bit free-running counter: ~91 ms at the 23.01 MHz / 32 tick. One
span across a 256-frame curve therefore means nothing, and the same build reported
**2.7 ms and 90.7 ms on two runs**. Each FRAME is far below the wrap, so the driver
now sums per-frame deltas into a 32-bit accumulator and the reader prints a
per-frame figure alongside the total. The correctness numbers were never affected;
the cost line was.

## 25. THE ENDURANCE INSTRUMENT, TWO RETRACTED NUMBERS, AND WHERE THE DRIFT LIVES (2026-09-30)

### PORTENDURE=N: free-run N frames and compare the endpoint host-side

`make line NOSPRBAKE=1 PORTFULL=1 PORTENDURE=N ...` seeds work RAM from the capture
frame, runs N frame bodies with no re-seeding and no in-rom scoring, and leaves work
RAM AT THE ENDPOINT. `tools/portl2_endure.py` dumps those 16,384 bytes and diffs
them against the arcade's own capture at frame 9000+N.

It costs **no cart space** -- the comparison is host-side at a single endpoint -- so N
is bounded by neither `.portdata` (574 frames) nor the O(k^2) scorer. And it prints
**every** wrong cell, where the in-rom collector holds the first 32.

### TWO NUMBERS RETRACTED, BOTH FROM THE SAME MISTAKE

**Whatever runs LAST owns work RAM, and an ares `--dump` reports the END state.**
Getting that order wrong produced two plausible, wrong numbers in a row:

| what I read | what it actually measured |
|---|---|
| "241 wrong vs control 236, **net +5**" | the ONE-FRAME ladder's state. `port_l2_run` re-seeds and runs its 16 variants every vint for 8 sweeps AFTER the endurance pass, so the dump was variant 4's output -- one frame from the seed. 241 is simply how far one frame from the seed sits from a capture 256 frames later. |
| "2 wrong, **net -234, NET CORRECT**" | the SCORER SELF-TEST's state. With the self-test last, work RAM held `expected(N)` -- the capture itself. |

Neither looked wrong. The first looked like a port that nearly works, because a state
near the seed is near the control by construction. **A state that resembles the
control is the most dangerous reading this gate can produce**, and it is the third
time this project has hit that shape (§19's frame that scored better than the control
because the logic had stopped; §23's reader summarising an unfinished run).

### The self-test that forced the issue

The in-rom scorer said 295 and the host said 241 for what should be one state. Rather
than pick the nicer number: seed work RAM to `expected(N)` by APPLYING the deltas --
the host proves that image IS the capture, 0 bytes differ -- and score it. **The only
honest answer is 0, and it returned 0.** The scorer was correct, so the states had to
differ, which is what pointed at the ordering.

### With the order fixed, the two instruments corroborate

    in-rom scorer   293 wrong        host diff   295 wrong
    control (seed vs the capture 256 frames later)   236
    -> net +59, and the earlier in-rom curve said +54

**The free-run divergence is real.** It is not an artifact of the scorer.

### Where it lives

    objects 0xFFC000-DFFF   209      globals 0xFFF000-FEFF   70      0xFFE000   16
    by slot:  slot 16 x76   slot 17 x64   slot 0 x36   slot 48 x33

**Slots 16 and 17 carry 140 of the 209 object cells, and both run handler `0x0EE80`.**

### And per-frame, by leave-one-out ablation

The first version of this attribution dispatched ONE slot and was uninformative by
construction -- all four slots returned a net between -4.5 and -9.0, because
dispatching one slot leaves the other three's work undone and every row is dominated
by that. Ablation is the right shape: dispatch all slots EXCEPT one.

    full frame over the sampled frames   48.5 wrong/frame
    skip slot 40    38.5   -10.0   removing it HELPS -- it is doing the damage
    skip slot 0     45.1    -3.5   removing it helps
    skip slot 16    48.0    -0.5   no effect
    skip slot 17    49.0    +0.5   no effect

### Two targets, and they are different questions

1. **PER-FRAME: slot 40, handler `0x16BF0`.** Removing it is worth 10 cells a frame
   with no inherited error, so it writes wrong values on its own.
2. **FREE-RUN: slots 16/17, handler `0x0EE80`.** They hold 140 of the 209 drifted
   object cells but ablate to zero per-frame -- so they are where the accumulated
   error LANDS, which is not the same as where it starts.

## 26. ONE ROOT CAUSE: THE DISPATCHER PASSES NO REGISTERS, SO OBJECTS ARE NEVER HIDDEN (2026-10-01)

§25 left two targets that looked unrelated: slot 40 costing 10 cells a frame
per-frame, and slots 16/17 holding 140 of the 209 drifted object cells. **They are
the same defect**, and it is the one HANDOFF 5 §2.3 named and deferred.

### The trace, end to end

`PORTSEED=64 PORTSKIP=40` isolates slot 40's exact contribution -- the same seeded
frame run with and without that slot, diffed host-side:

    with all slots      34 wrong   control 52   net -18
    without slot 40     24 wrong   ->  slot 40 costs +10 cells
    its damage, named:  FFD400  FFED28  FFFA80 FFFA82..FFFA89

`PORTWATCH=0xFFFA80` names the writer: **`sprite_build_depth_banded`**, writing 0xB8
where the arcade leaves 0. That is the same function that writes slot 16's `+0x00`
as `0x88` where the arcade ends `0x80` (§21).

Reading it explains both at once. The function has one exit that HIDES an object and
one that DRAWS it:

    line 34  *unaff_A6 = *unaff_A6 & 0xf7;                 <- hide: CLEARS bit 3
    line 35  ST32(W68PTR(0xFFF800) + unaff_A6[8]*0x10, 0); <- and zeroes its record
    ...
    line 84  *unaff_A6 = *unaff_A6 | 8;                    <- draw: SETS bit 3
    line 85  puVar9 = W68PTR(0xFFF800) + unaff_A6[8]*0x10; <- and fills the record

`0xFFFA80` is `0xFFF800 + 40*0x10` -- slot 40's record -- and `unaff_A6[8]` reads
0x28 = 40, which the oracle confirms is a 1-BYTE read. **The index is right; the
port simply takes the DRAW path where the arcade takes HIDE.** Slot 16's `0x88` is
line 84's bit 3 and slot 40's filled record is line 85. One cause, two symptoms,
reported as two defects.

### Why the guard does not fire

Three guards jump to `hide_object_sprite`, and the third is

    line 31  sVar1 = sVar7 - (*(ushort *)((byte *)&p_D6 + 2));
    line 53  if (0xdf < sVar1) goto hide_object_sprite;

`p_D6` is a REGISTER PARAMETER. The 232 oracle vectors at the dispatcher's own call
site (`caller 0039AC`) show D6's low word is **never 0**: `0xF800, 0xF810, 0xF820,
0xF830, 0xF840, 0xF900 ...`, stepping by 0x10 -- the sprite record pointer advancing
one 16-byte record per object. **Our dispatcher passes nothing**, because
`port_handlers` is an unprototyped table called with no arguments (§19 says so
explicitly: "the win is at DIRECT calls, where the caller does know"), so `p_D6`
reads 0, `sVar1` stays small, and the guard can never fire.

With the arcade's D6 the subtraction wraps a small `sVar7` to a large positive short
and the object is hidden. With 0 it is drawn. Every frame. For every slot.

### What this costs and what it means

The gate's whole remaining per-frame error is 30 missed cells a frame, and the
sprite-record and active-flag writes of objects that should be invisible are a large
part of it. It also explains the shape of the free-run curve: an object that should
be hidden stays live, so it keeps being dispatched and keeps writing.

**This is the single highest-value fix left, and it was already identified as
"the next concrete fix" in HANDOFF 5 §2.3 and §5.1.** What is new is the mechanism:
not "registers would help" but "D6 gates the hide path, so without it nothing is
ever hidden".

### The fix, and the honest caveat about it

The dispatcher must supply D3/D6/D7. Two routes:

1. **From the oracle** -- the 232 vectors carry the full register file per call. This
   is a DIAGNOSTIC, not a port: it proves the mechanism and bounds the payoff, but a
   shipping port cannot read an arcade capture.
2. **Derived** -- the 68K's `object_dispatcher` computes them, and D6's pattern
   (0xF800 + 0x10 per object) says what it is doing: handing each object its sprite
   record pointer. Ghidra deleted that setup for the same reason it deleted every
   argument setup (§19: the prototype said the function takes nothing), so declaring
   the dispatcher's parameters should bring it back.

Route 1 first, because it is a one-build answer and it tells us whether route 2 is
worth the Ghidra cycle.

## 27. THE AUDIT: A WILD-JUMP CLASS WITH NO FAILING CELL BEHIND IT (2026-10-01)

Mike's question -- "are most of these iterative errors because we are going step by
step through the disassembly?" -- has a measurable answer, and it changed the method.

**No. Not one port defect has been a mistranslated function.** Every one has been a
GENERATOR CLASS, fixed once and moving many functions: argument setup deleted (549
functions, 176 -> 49 wrong), the read-only register model (47 producers), scalar
width, array-base width, misaligned 32-bit access (147 of 302 symbols),
absolute-short addressing (143 references), the `dbra` idiom (89% of master cycles),
68K pointers in data (22 sites, both hangs).

But each was found by a FAILING CELL pointing at it, one at a time, which is the
slowest possible order and only finds classes that happen to be exercised by the
frame under test. `tools/cport_audit.py` asks the question directly instead: for each
way the 68K expresses something C cannot express the same way, does the generator
handle it, and how many instances are in the unit?

### What it found immediately

    uVar2 = (*(code *)0x4ebe)();

**A call to a LITERAL 68K address.** The indirect-call rewrite handles
`(**(code **)(X))()` -- a pointer read FROM MEMORY -- and this form has no pointer to
load, so the pattern never matched and the call went straight to SH-2 address
0x4EBE: low SDRAM, not the 68K program. **18 sites across 5 targets** (0x04EBE,
0x07DB0, 0x08BBE, 0x09FFC, 0x0B8BC), **none of them in the dispatch table**.

**No failing cell was ever going to point at these**, because the functions holding
them are not reached in the gate's frame. That is the audit earning its keep on the
first run. Now routed through the same lookup, so an unknown target is a counted
miss and not a jump; the real fix is to create those five functions in Ghidra and
add them to the roots.

### What it ruled OUT, which is worth as much

    condition codes carried across a CALL (SR/CCR pseudo-registers)      0
    BCD arithmetic (abcd/sbcd -- the score counters on many S16 titles)  0
    unsigned compared against -1 (the dbra idiom)                        0  (pass works)
    raw 68K ROM-range literal used as a pointer base                     0  (pass works)
    functions still returning `undefined`                                0
    real integer division / modulo                                   2 / 9

A 68K subroutine CAN return its answer in the flags, and Ghidra models flags per
function -- that would be the register problem again, in a form no amount of
parameter declaration fixes. **It does not occur here.** Nor does BCD, which would
have made score counters drift silently. Each zero is now a grep away instead of a
re-derivation.

### And it corrected two of my own numbers

* "28,708 unplumbed register references" is **100% `unaff_A6`**, which the dispatcher
  supplies. **Genuinely unplumbed register inputs: 0.** The parameter fixpoint
  (§19) did its job completely, and the raw count was alarming for no reason.
* The division check matched file paths inside comments. Stripping comments first:
  2 divisions and 9 modulos in the whole unit, so the 68000's 16-bit `divu`
  semantics are very nearly a non-issue.

### Still open from the audit

    absolute-short residue (a negative constant on an integer base)     195
    CONCAT forms (a packed multi-register value)                        505
    `code *` declarations that are not calls                            669

195 and 505 are EXPOSURE, not defects -- the record warns that 234 of 377
absolute-short candidates were ordinary pointer arithmetic, so the classifier is
the thing to verify, not the count.

## 28. A WRONG RETURN DECLARATION IS WORSE THAN NONE: 26 FUNCTIONS (2026-10-01)

§27a left the per-frame error at 26.0 cells a frame and located it: the SPRITE
RECORD table at `0xFFF800 + record*0x10`, records 0, 16 and 17, fields +1 +3 +5 +6
+7. `tools/portl2_seed.py` now prints VALUES, and they name the mechanism:

    cell     port  arcade  seed
    FFF805    00     12     12    we wrote it; the arcade did NOT
    FFF806    00     37     37    we wrote it; the arcade did NOT
    FFF807    00     96     96    we wrote it; the arcade did NOT

The arcade wrote those fields back UNCHANGED and we zeroed them. `+6/+7` is
`(short)(uVar10 >> 1)` and `uVar10` comes from `zoom_scale_lookup`, which the port
renders as:

    ulonglong zoom_scale_lookup(int p_A0, undefined4 p_A1)
    { return CONCAT44(p_A0 + 2, p_A1); }

**It computes nothing.** The arcade's version READS memory (`FFC04E`, the slot's
+0x4E) and its outputs are `A0 += 2`, `D0 = 0` and **`D1 = 0x40`** -- the scale.
Ghidra declared the return as `A0:4,A1:4`, and **A1 does not change across any of
the four captured calls**. The caller takes the low half of the pair as a VALUE, so
it reads A1 -- a register the function never touches -- where the real output is D1.

### The general class, and it is measurable without judgement

`tools/ret_storage_audit.py` compares each function's DECLARED return storage
against the registers the oracle shows CHANGING across its own calls. A register
the function never writes cannot be its output, so the caller is reading whatever
was there before. No interpretation required.

**26 functions declare an output they never produce**, including ones called 8
times a frame:

    calls  name                  declared        never changes   actually changes
        8  zoom_scale_lookup     A0:4,A1:4       A1              D0,D1,D3,D6,D7,A0
        8  restore_position      A0:4,D1:4       A0,D1           D0
        6  irq4_handler          D0:4,D1:4       D0,D1           --
        6  FUN_00004254          (extraout)      A0,A1,D0,D1     A4,A5,A6
        6  FUN_000037d0          A0:4            A0              D0,D1,D2,A2

**A wrong declaration is worse than no declaration.** With none, the caller reads an
`extraout_*` global that is at least consistently 0 and shows up in the audit as
unplumbed. With a wrong one, the caller reads a real register that the function
leaves untouched -- which looks plumbed, measures as plumbed, and is silently wrong.
§19's fixpoint declared 549 functions' parameters and 47 functions' return storage
and improved the frame enormously; this says some of those declarations are wrong,
and the same oracle that justified them can check them.

### The discriminator mattered here too, for the third time today

The first run of this audit reported **141** functions, and `A6` was in nearly every
row. `reg_convention.json`'s `storage` map holds Ghidra's own pseudo-register names,
and **`unaff_A6` means UNAFFECTED** -- an input that passes through, which by
definition never changes. Only `extraout_*` is an output. Counting the whole map as
outputs made the test report the obvious. With the prefix honoured: 26, and every
row is a real claim.

### Next

Correcting a declaration needs a Ghidra cycle and the right replacement, and the
oracle gives it only where the evidence is unambiguous. `zoom_scale_lookup` is the
clean case: the caller reads the low half as a value, D1 is 0x40 on all four calls,
and A1 is provably not an output. That one first, then measure before touching the
other 25.

## 29. THE OTHER HALF OF §16: FEED EVERY extraout_ FROM THE CALL THAT PRODUCES IT (2026-10-01)

§16 declared return storage on 47 producers, which made their bodies materialise.
This is the half that was missing, and §28's sprite-record trace walked straight
into it.

### The chain, every link measured

    uVar11 = zoom_scale_lookup(p_A0);   /* returns the sprite scale */
    ...
    uVar10 = extraout_A1;               /* reads a GLOBAL that is 0 */

`uVar10` drives sprite-record fields +6/+7, which came out 0 against the arcade's
0x3796. Ghidra's own SSA (`docs/audit/extraout_producers.json`, 423 sites in 194
callers, **0 produced by an indirect call**) names `zoom_scale_lookup` as the
producer of that exact `extraout_A1`, with storage A1:4.

**And the oracle proves A1 is wrong.** Across all four captured calls
`zoom_scale_lookup` never changes A1; what it changes is D1 = 0x40. So the register
NAME is wrong and the VALUE is real -- which means the port should not model the
register at all. It should read the call's RETURN.

Two fixes, and both were needed:

1. **The return storage, corrected to `A0,D1`** and the body MATERIALISED, which is
   §16's mechanism and the confirmation that the declaration is right:

       undefined8 zoom_scale_lookup(int p_A0)
       { return CONCAT44(p_A0 + 2,
                         (uint)(ushort)(0x40 - (*(byte *)(unaff_A6 + 0x4e) & 0x1f))); }

   It reads `A6 + 0x4E` -- the exact cell the oracle recorded -- and yields 0x40
   when that byte is 0, matching the oracle's D1 exactly.

2. **`cport_prep` now assigns the pseudo-register from the call's return** at every
   producing site. **Assigning the LOCAL, not the global**: cport_prep hoists every
   pseudo-register into a local at function ENTRY, so a later write to the global is
   invisible to the body -- measured, the assignment landed on line 29 and the read
   on line 49 still used the entry copy.

### It works, and the proof is one byte

Sprite-record field +7 went **0x00 -> 0x20**, which is `0x40 >> 1` -- the scale
arriving where it had been zero. The mechanism is confirmed, not inferred.

### And the per-record error is now a different, smaller set

With the extraout feeding plus the FRAME-MATCHED dispatcher registers
(`L1_WINDOWS=9064`, `L1_VEC=`, `PORTREGS=1`):

    before   record 0: +1 +3 +5 +6 +7     records 16/17: +1 +3 +5 +6 +7 +8
    after    record 0: +0 +1 +3           records 16/17: +0 +1 +2 +3
    frame    25 wrong -> 23, net -27 -> -29 against a control of 52

**+5, +6, +7 and +8 are now correct on all three records.** `+5` was independently
pinned before the fix: it is `(char)p_D3 >> 1`, and the arcade's value equals the
DISPATCHER's own D3 for the slot that writes that record --

    record 0 <- slot 16, D3 0x12, +5 wants 0x12
    record 16 <- slot 17, D3 0x16, +5 wants 0x16
    record 17 <- slot 40, D3 0x18, +5 wants 0x18

-- three for three, which is what justified supplying D3 at the dispatcher.

### What is left, and why the oracle cannot supply it

`+0 .. +3` depend on `sVar1 = sVar7 - p_D6`, and `p_D6` at
`sprite_build_depth_banded` is NOT the dispatcher's D6: that function is the LAST of
a ten-call tail, so its D6 is whatever the chain left. Supplying the dispatcher's D6
makes `FFC000` read 0x80 where the arcade has 0x88 -- the guard hides where the
arcade draws -- which is §26's claim refuted by measurement and the conflation named.
The register CHAIN through the tail is the remaining work, and `extraout_`/parameter
declarations are the mechanism for it.

### 29a. And on the 256-frame gate it is marginally WORSE

    thunks alone (§27)                  per-frame mean 26.0   net -13.7
    + extraout feeding (§29)            per-frame mean 26.7   net -13.1
    free-run                            191.4 -> 191.9, net +46 either way

**A mechanically correct fix that does not improve the metric.** The mechanism is
not in doubt -- `+7` went 0x00 -> 0x20, the scale arriving where it had been zero --
and with the register chain supplied (`PORTREGS=1`, frame-matched) the same fix is
clearly better: 25 wrong -> 23, and `+5/+6/+7/+8` correct on all three records.

So the payoff is GATED ON D3. Feeding a value computed from other zero inputs
propagates a half-correct number further than a zero did, and the gate charges for
that. This is the same shape as §17's `.portrom` fix ("a worse number and a better
port") and §22's eleven-symbol clamp, where one right fix was swamped by context.

**It stays**, because the caller reading an unwritten global instead of the value the
call just returned is wrong by construction, and because the 256-frame per-frame
curve is the wrong instrument to judge it while the chain is still broken. The
seed-frame A/B with the chain supplied is the one that answers it, and it says +2.

The seven §28 return-storage corrections measured **neutral** at the seed frame --
26 wrong before and after. Not a regression and not a gain; recorded as such so
nobody re-does them expecting a number.

## 30. D3 AND D6 ARE THREADED HANDLER-TO-HANDLER, AND THAT IS DERIVABLE (2026-10-01)

§26 asked where the dispatcher's D3/D6 come from and could not answer it: the
decompiled `object_dispatcher` sets NEITHER -- it only walks the slots and calls --
so §26 concluded they were "inherited from its caller and threaded through the
handler chain", and §29 was left needing a value the oracle could only supply
artificially.

The frame-matched capture answers it exactly. **Each handler's EXIT D3/D6 is the
next handler's ENTRY D3/D6, three transitions for three:**

    slot  callee     D3 in -> out        D6 in -> out
       0  0x0049BA   00109502 -> 0x12    FA70 -> F800
      16  0x00EE80   0x12     -> 0x16    F800 -> F900
      17  0x00EE80   0x16     -> 0x18    F900 -> F910
      40  0x016BF0   0x18     -> 0x12    F910 -> FA80

    slot 0  exit D3=12 D6=F800  ->  slot 16 entry D3=12 D6=F800   MATCH
    slot 16 exit D3=16 D6=F900  ->  slot 17 entry D3=16 D6=F900   MATCH
    slot 17 exit D3=18 D6=F910  ->  slot 40 entry D3=18 D6=F910   MATCH

**The handlers compute them and pass them along; the dispatcher only preserves
them.** D6 advancing F800 -> F900 -> F910 -> FA80 is the sprite-record cursor being
allocated one record at a time, and D3 is the companion value each handler leaves
for the next.

### Why the port can never form the chain as built

`port_object_dispatch` ZEROES the register array before every handler call:

    for (q = 0; q < 15u; q++) port_cur_regs[q] = 0;

So even a perfect handler cannot hand anything to the next one. That is why
supplying the dispatcher's registers from the oracle helped only the FIRST thing
each value touched, and why §26's prediction misfired: there was never a chain to
seed, only 64 independent calls.

### This is a DERIVED fix, which the oracle version was not

Nothing here needs an arcade capture at run time:

1. **Stop zeroing.** The array persists across the dispatch loop, as the 68K's
   registers did.
2. **Write back.** A handler's exit D3/D6 must land in the array. That is the §16
   mechanism again -- declare the handler's return storage to include D3/D6 and the
   generated thunk unpacks the result into `r[3]`/`r[6]`.

The audit already knows which handlers change which registers:
`FUN_0000EE80` changes D1,D2,D3,D4,D6,A0, so D3 and D6 are among its real outputs.
The thunk signature becomes `uint *r` rather than `const uint *r`.

The one value the chain cannot derive is its SEED -- slot 0's entry D3 is
0x00109502, left by whatever ran before the dispatcher. Starting from zero and
letting the chain build is the first thing to measure, because the arcade's own seed
only matters if the first handler reads D3 before writing it.

### Why this is the whole remaining per-frame error

`+5` of each sprite record is `(char)p_D3 >> 1` and wants exactly the dispatcher's
D3 for the slot writing it (0x12/0x16/0x18 for records 0/16/17 -- three for three,
§29). `+0..+3` want `p_D6`. Both are chain values. With them supplied artificially
the seed frame reads 23 wrong against a control of 52; without them, 26.

### 30a. Declaring return storage CANNOT express the chain, and why

The obvious follow-through -- declare the handlers' return storage as `D3,D6` so the
thunk can write them back -- was done, and it does not work:

    undefined8 FUN_000049ba(undefined4 p_A0, ..., uint p_D3, ...)
        return CONCAT44(p_D3,p_D6);          /* a PASS-THROUGH, not a computation */

The handler does not modify D3 in its own body; a CALLEE does, and that callee's
effect is invisible for exactly the same reason. So the chain needs the return-side
analogue of §19's parameter fixpoint -- and it cannot be built, for a hard reason:

**44 functions change BOTH D3 and D6 in every captured call** (77 change at least
one), `object_dispatcher` and `zoom_scale_lookup` among them. Ghidra's return
storage holds at most TWO registers, because `set_ret_storage` needs an `undefined`
type matching the storage size and there is none at 12 bytes (`set_ret_storage.py`
already notes 6 functions it had to skip for this). A mechanism that tops out at two
registers cannot model a chain that threads more.

### The model the 68K actually has

**Registers are GLOBAL STATE.** §19 declared them as PARAMETERS to recover the
argument setup, which was right for INPUTS -- it took the frame from 176 wrong to
49 -- but a by-value C parameter cannot carry a modification back out, so the same
change severed the write-back path the chain needs.

The faithful model is both at once: pass the value IN as a parameter, and write the
modified value BACK to a register global on exit. That is one generator change and
no Ghidra cycle:

    uint FUN_x(..., uint p_D3, ...)
    {
        ...  p_D3 = something;        /* the body modifies it */
        _reg_D3 = p_D3;               /* ...and it must survive the return */
        return ...;
    }

with `port_object_dispatch` refreshing its register array from those globals after
each handler instead of zeroing it. The port already has per-pseudo-register globals
(`_r_unaff_D3`, `_r_in_D3`), but they are keyed by Ghidra's pseudo-register NAME and
there is no single global per REGISTER -- so the register file has to exist first.

Recorded before building it because it is a design change and not a patch: it makes
the port's register model match the 68000's instead of C's.

### 30b. The seven corrections are REVERTED: free-run +15 worse, per-frame unchanged

Measured on the 256-frame gate, and this is why the gate exists:

                              per-frame mean   free-run mean   free-run last
    zoom_scale_lookup only          26.7           191.9        282  (net +46)
    + the seven §28 corrections     26.7           207.3        297  (net +61)

**Per-frame identical, free-run 15 bytes worse at the endpoint.** A change with no
upside and a measurable downside does not stand, so the corpus is restored to the
pre-seven state (sha `697b84e3…`, extraout sites back to 423) and the Ghidra project
to its pre-seven copy. The rejected corpus is kept at
`~/.s16_backup/altbeast.c.with7` and the rejected project at `ghidra_proj_rejected`.

**Why a "correct" change can be harmful here.** Each of the seven replaced a
declared return register the function never writes with one it does. That changes
what the CALLERS read: the old `extraout_X` disappears and a new one appears. With
the register chain still broken (§30), the new value is no more correct than the old
one -- but it propagates DIFFERENTLY, and the free-run curve compounds whatever it
propagates. The per-frame curve cannot see it because it re-seeds every frame.

The derivation was sound (one register changing in every captured call), the
mechanism was sound, and the result was a regression. That is not a contradiction:
**a locally correct declaration is not automatically an improvement while the
mechanism it feeds is broken** -- the same lesson as §22's eleven-symbol clamp and
§29a's extraout feeding, and the third time today that a right-looking change had to
be judged by the 256-frame curve rather than by its argument.

They should be re-applied AFTER the register chain works, and the audit
(`tools/ret_storage_audit.py`) will still name them.

### 30c. The chain traced end to end, and zoom_scale_lookup is the SOURCE of D3

The write-back pass is in -- 3,647 references in the unit -- and the chain still read
`D3=0 D6=0` after a whole frame. A counter separates the two possible causes, and
the answer is neither of the obvious ones: **5 write-backs EXECUTED** and both
registers still ended 0. The mechanism is live and STARVED.

The frame-matched oracle names every D3 transition at frame 9064, and the source is
unambiguous:

    callee                caller      D3 in -> out
    zoom_scale_lookup     0x003D3E    0 -> 0x24
    zoom_scale_lookup     0x003D3E    0 -> 0x2C
    zoom_scale_lookup     0x003D3E    0 -> 0x30
    FUN_0000669c          0x00EF86    0x12 -> 0x18
    animation_frame_fetch 0x00EF92    0x18 -> 0xE0E8
    walk_player_slots     0x00EF98    0xE0E8 -> 0
    FUN_0000ee80          0x0039AC    0x12 -> 0x16   (the handler, net)

**`zoom_scale_lookup` computes D3 from an entry D3 of ZERO** -- 0x24, 0x2C, 0x30 --
and those are exactly the values §29 derived from the `+5` fields
(`(char)p_D3 >> 1` = 0x12/0x16/0x18). The same 34-byte function is the source of
both the scale AND the D3 that scales it.

### Why its return cannot carry D3, and what does

Its outputs are A0, D0, D1, D3, D6, D7 -- **six registers**, and Ghidra's return
holds two. The two it has are the right two for the caller's two USES: the high half
is dereferenced as a pointer (so A0) and the low half is read as a value (so D1,
0x40, the scale). D3 has nowhere to go.

**A PARAMETER is an in-out channel once the register file exists.** Declare D3 as a
parameter on `zoom_scale_lookup`, Ghidra materialises the code that assigns `p_D3`,
and the write-back pass stores it to `_port_reg[3]`. What is still missing is the
symmetric half: after a call, the CALLER must reload its own `p_D3` from
`_port_reg[3]`, because its by-value copy is stale. Write-back without reload is
half a register.

## 30d. THE CHAIN WORKS: 26 -> 20 WRONG AT THE SEED FRAME, ALL DERIVED (2026-10-01)

Four pieces, in the order they had to go in, and every one derived from the binary
or the oracle rather than supplied at run time.

### 1. zoom_scale_lookup, hand-written from 25 instructions of 68K

`tools/ghidra/disasm_range.py` (new) printed 0x3CD4-0x3D12, kept at
`docs/audit/zoom_scale_lookup.asm`. The function has **SIX outputs** -- A0, D0, D1,
D3, D6, D7 -- and Ghidra's return storage holds TWO, so the decompilation is not
wrong but structurally INCOMPLETE:

    return CONCAT44(p_A0 + 2, 0x40 - (*(byte *)(unaff_A6 + 0x4e) & 0x1f));

correct for A0 and D1, silently dropping D3, D6 and D7. Transcribed by hand into
`port_zoom_scale` (same precedent as the hand-written dispatcher), keeping that
return shape so the generated caller's two uses are unchanged and sending the other
four to the register file.

**Verified against the oracle on both sides**: it runs 4 times a frame, its last D3
is **0x12** -- the arcade's fourth `zoom_scale_lookup` exactly -- and it reads
`A0 = 0x2366520`, which is `ROM_BASE + 0x26520`, the oracle's A0 to the byte.

One trap: `p_A0` is a **68K address** (it comes from `sprite_frame_lookup`, whose
body materialises as `... + 0x255e0`). Dereferenced directly it read SH-2 address 1
and D3 stayed 0 while the write-back fired -- 5 -> 9 write-backs, D6 0 -> 0x48, D3
still 0, which is what made the cause findable.

### 2. The register file, and the write-back

`_port_reg[15]`, written by any function whose body ASSIGNS a register parameter --
**only if it assigns it**, because a function that merely receives D3 and calls
something that modifies it would otherwise clobber the update with its own stale
copy.

### 3. The RELOAD, which is the half that makes it work

A by-value C parameter is stale after a call. The 68000 had no copy: after `jsr`,
D3 IS whatever the callee left. So a function's register parameters are refreshed
from the file after every call to a unit function -- not after runtime helpers
(`LD32U`, `P68`, `port_call68_at`), which are the port's own machinery.

**Scoped, because the blanket version OVERFLOWED THE CART.** A reload after every
call in all 161 functions with a `p_D3` parameter pushed `.portcode` past
0x02400000; its budget is fixed at ~464 KB by `.portrom` and `.portdata` below it.
`CPORT_RELOAD` defaults to `sprite_build_depth_banded` alone -- the one function
whose output the gate reads cell by cell.

### 4. The dispatcher stops zeroing D3/D6

It carries them from `_port_reg` into the next slot, which is what the 68K's
register preservation did.

### The result

    register chain after the frame   D3 = 0x12   (arcade: 0x12)
    seed frame                      20 wrong of 16384   control 52   net -32
    before this section             26 wrong                         net -26

**`+1` and `+5` are now correct on all three sprite records.** `+5` is
`(char)p_D3 >> 1` and it was the prediction that justified the whole chain.

### What the remainder says, and it corrects §29

Left: `+3`, `+6`, `+7` (and `+8` on two records). `+6/+7` is `(short)(uVar10 >> 1)`
and the arcade's 0x3796 needs `uVar10 = 0x6F2C` -- which is **A1 at the zoom call**.
So `extraout_A1` really is A1, a PASS-THROUGH INPUT, and the oracle said so all
along by showing A1 unchanged across the call. §29 fed it the return's low half
(0x40) instead, which is why `+7` reads 0x20.

**That points at a simpler and more faithful rule than §29's**: `extraout_X` is not
"the value the call returned", it is **the register file's X after the call** --
which is exactly what the 68000 means by reading a register after a `jsr`. One rule
for every pseudo-register, replacing the producer-return mapping.

### 30e. And on the 256-frame gate: every curve improves, none regresses

                              seed frame   per-frame mean   free-run mean   free-run last
    before the chain          26  (-26)      26.7 (-13.1)    191.9 (+30.8)     +46
    with the chain            20  (-32)      26.1 (-13.6)    185.7 (+24.7)     +47

**The first change in this arc that moves the free-run curve**: mean net +30.8 ->
+24.7. Everything before it was per-frame only, or neutral, or had to be reverted.

The seed-frame gain is 6 cells and the 256-frame mean moves 0.6, which is what a
reload scoped to ONE function should look like -- `CPORT_RELOAD` covers
`sprite_build_depth_banded` alone because the blanket version overflowed the cart.
Widening it is the obvious next lever and it is bounded by `.portcode`'s 464 KB, so
it has to be widened a few functions at a time, by measurement.

### 30f. Two null results, both worth the measurement

**Widening the reload to the handler changed nothing.** `CPORT_RELOAD` with
`FUN_0000ee80` added: still 20 wrong. And the budget for widening is almost gone --
`.portcode` is **440,972 bytes with 17,780 bytes of headroom** before the 4 MB cart
ends. So "widen the reload" is not the lever §30e guessed it was, in either
direction: it does not help here and there is no room for much of it.

Worth separating: that 441 KB is the PROBE, not the port. A shipping build carries
no delta blob, no 16 KB seed, no gate driver and no instrumentation, so the pressure
is an artifact of the measuring apparatus.

**A1 in the register chain is a DOMAIN ERROR, and it is instructive.** `extraout_A1`
is a pass-through of the caller's A1, so carrying A1 in the file looked like the fix.
With it in, the file read **0x0601C142** -- a real SDRAM pointer, written back
correctly by whichever function uses A1 AS a pointer -- where the zoom call site
wants the arcade's 0x6F2C and uses it as a NUMBER (`uVar10 >> 1` is a screen
coordinate).

**The port keeps A-registers as TRANSLATED SH-2 POINTERS; the 68000 kept 24-bit
addresses that are sometimes integers.** A single global cannot serve both uses, and
nothing in the file records which domain a value is in. So the chain is DATA
REGISTERS ONLY (D3, D6 today), and extending it to an address register needs a
domain tag, not just a slot.

Kept state: 20 wrong at the seed frame against a control of 52 (net -32), `+1` and
`+5` correct on all three sprite records, D3 = 0x12 matching the arcade.

## 31. THE SPRITE RECORDS GO FULLY CORRECT: 26 -> 9 WRONG AT THE SEED FRAME (2026-10-01)

Four register declarations and one hand-written leaf, in sequence, each measured:

    26 wrong (net -26)   the chain built but starved
    20 wrong (net -32)   D3 flows -- +1 and +5 correct
    12 wrong (net -40)   A1 declared on sprite_frame_lookup -- only +3 left
     9 wrong (net -43)   D7 in the chain -- the sprite records are CLEAN

**All three sprite records are now byte-correct**: every field +0 through +9 on
records 0, 16 and 17, against a control of 52 changed cells.

### sprite_frame_lookup's A1 was an undeclared OUTPUT

From the oracle: it ADVANCES A1 -- 0x6D5C -> 0x6F2C -> 0x65FFE -> 0x6587A -- and is
called at 0x3D3A, one instruction before the zoom call. Its declared return was
`A0:4` alone while its real outputs are D0,D1,D2,A0,A1. A0+A1 is two registers,
which FITS, so this one needed no hand-writing:

    return CONCAT44(*(ushort *)(iVar1 + 0x255e0) + 0x255e0,   /* A0 */
                    *(undefined4 *)(iVar1 + 0x255e2));        /* A1 -- new */

### Two faults had to go first, and both were in my own generator

1. **The attributed producer did not declare the register.** Ghidra models a call as
   possibly clobbering any register absent from its signature, so `extraout_A1` was
   attributed to `zoom_scale_lookup` -- return `A0,D1`, no A1 -- while the real
   producer two lines earlier had it. Feeding from the attributed call read the
   WRONG VARIABLE: both calls assign `uVar11` and the later one wins, so `+7` stayed
   0x20 against the arcade's 0x96. `cport_prep` now prefers a call in the same body
   whose declared return CONTAINS the register, and requires its result to be
   ASSIGNED -- merely appearing in the text matched dozens of functions and the
   dict's first hit was not the producer.
2. **`reg_convention.json` was STALE.** It still held `sprite_frame_lookup`'s old
   `A0:4`, so the preference above could not see the new A1 and the feed stayed
   after the zoom call. Regenerating it is what moved the number: the feed lands at
   line 31, before `uVar11` is overwritten, and the frame goes 20 -> 12.

**Any Ghidra signature change invalidates `reg_convention.json` and
`extraout_producers.json`.** Both are generated from the project and both are inputs
to the port. Regenerate both after every declaration, or the next build silently
uses the old model.

### What is left at this frame: 9 cells, none of them sprite

    FFED10 FFED11          the 0xFFE000 region
    FFF02B FFF02D FFF134   we never wrote them; the arcade did  (missed work)
    FFF352 FFF353 FFF01C   we wrote them; the arcade did not

## 32. THE FRAME BODY IS 15 CALLS AND WE RUN FOUR (2026-10-01)

Chasing the last 9 wrong cells found something larger than the cells. Three of them
(`FFF02B`, `FFF02D`, `FFF134`) are written by `FUN_000012ec` and `FUN_000039c8`,
and **neither function is in the port's unit** -- 0 definitions, 0 call sites. So
they are not a register or width problem; they are a COVERAGE gap.

Asking the oracle for every call made from the frame-loop region at frame 9064
(caller 0x400-0xB00) gives the arcade's REAL frame body, in address order:

    0x097C  FUN_000012ec      NOT IN THE UNIT
    0x0980  FUN_00001366      NOT IN THE UNIT
    0x0984  FUN_00000bd6      NOT IN THE UNIT
    0x098E  FUN_00003aae      in the unit
    0x0A32  FUN_000039c8      NOT IN THE UNIT   (a recorded decompilation defect)
    0x0A38  FUN_00003a00      NOT IN THE UNIT
    0x0A3E  FUN_0000d80e      NOT IN THE UNIT
    0x0A4E  FUN_0000e052      NOT IN THE UNIT
    0x0A54  object_dispatcher in the unit
    0x0A5A  FUN_00003716      NOT IN THE UNIT
    0x0A68  FUN_00000dfa      NOT IN THE UNIT
    0x0A7A  FUN_00000ece      NOT IN THE UNIT
    0x0AB8  FUN_00004254      NOT IN THE UNIT
    0x0AD4  FUN_0000dcb4      NOT IN THE UNIT
    0x0ADC  wait_frame_flag   stubbed (correctly -- IRQ4 sets its flag)

**Fifteen calls. The driver runs four**, and one of its four -- `FUN_0000144a` --
does not appear in the arcade's sequence at this frame at all.

### Why the driver had it wrong

`sh_src/port_l2.c`'s own comment says the frame body "is not a function. It is an
inline do/while inside the reset routine (0x40E), identical in switch cases 0 and 8:
`FUN_00003aae(); FUN_0000144a(); object_dispatcher(); wait_frame_flag();`" -- a
sequence derived by READING the reset routine. The oracle says that reading was
wrong, or was of a different switch case than the one gameplay takes.

**This is the §18 lesson again, and I did not apply it.** There, diffing the port's
own call trace against the arcade's progress markers overturned a conclusion reached
by reading the source, and §18 wrote down that control flow must be measured rather
than read. The frame body itself was never put through that test.

### What it means for every number in §13-§31

The per-frame score has been measured against a body that executes **4 of 15**
calls. That does NOT invalidate the register and width findings -- each was an A/B
against the same body, and the sprite records really are byte-correct now -- but the
REMAINING error was never going to close, because eleven of the frame's functions
are not in the port.

It also explains the shape of the free-run curve far better than drift does: a frame
that omits two thirds of its work will diverge no matter how correct the third it
runs.

### Next

Add the eleven as roots (`CPORT_ROOTS`), run the true 15-call sequence in the
driver, and re-measure everything. `FUN_000039c8` is a known decompilation defect
(LESSONS, "Ghidra warns on it itself", two symbols at one address) so it may not
come clean, and that is worth knowing early rather than late.

## 33. THE FLAT FRAME-BODY LIST IS WRONG, AND THE LOOP PROVED IT (2026-10-01, overnight)

`tools/port_loop.py` ran the candidate list it was given and returned a clean
NEGATIVE result -- monotonic, which is the useful shape:

    PORTBODY=1   46 wrong   net  -6   <- best of the candidates
    PORTBODY=2-4 54 wrong   net  +2
    PORTBODY=5   61 wrong   net  +9
    PORTBODY=6-7 69 wrong   net +17
    all 14       97 wrong   net +45   HUNG in 0x0E052

Every length is far worse than the four direct calls' **-43**, and more of the
arcade's own sequence makes it worse. **The sequence was right and my
implementation of it was wrong**, and the 68K says exactly why
(`docs/audit/frame_loop.asm`, 0x970-0xADC, 93 instructions):

1. **IT HAS BRANCHES.** `btst.b #0,(0xfffff026).w ; bne 0xABE` conditionally skips
   the 0x0DFA call; `cmpi.b #6,(0xfffff142).w` gates more. A flat unconditional
   list runs calls the arcade SKIPS -- which is the monotonic worsening, exactly.
2. **TWO CALLS WERE MISSING** from the oracle's fifteen: 0x09052 at 0x996 and
   0x17188 at 0x0ACE. The L1 window did not capture them, so a list built from the
   capture alone was incomplete as well as unconditional.
3. **A6 IS NEVER SET** anywhere in that range, so dispatching through
   `port_call68_at` -- which passes `port_cur_regs`, whose `[14]` is the LAST
   DISPATCHED SLOT -- handed ten of the fourteen an A6 pointing at an object record.

### The real fix is not a list

The whole loop lives inside ONE decompiled function: **0x005BE-0x00BD5**, which
Ghidra named `test_mode_screen` -- the main routine, mis-named -- and it is **not in
the port's unit**. Adding it as a root and calling it once per frame lets the
decompiled code do its own branching, its own conditionals and its own register
setup, instead of me reimplementing a control-flow graph by hand. §32's instinct was
right (measure the body, do not read it) and the conclusion I drew from it was
wrong: the answer was never a sequence of addresses.

**Reverted to the four direct calls**, which measure 9 wrong / net -43 / chain
D3 = 0x12, restored and verified byte for byte.

### What the loop is actually for

This is the first result in the arc that cost nothing to get wrong. Seven builds,
seven measurements, one verdict, no commits to unpick, and the tree left in the
banked state. The loop's rules did that -- fixed candidate list, flag-only changes,
revert as the default path -- not judgement applied at the time.

### 33a. The gate length changed, so these numbers are NOT comparable to §30e's

Verification of the banked rom, and the first thing to say about it is that **the
gate is 176 frames now, not 256** -- the delta chain was trimmed to make cart room
for the frame-body roots (§32). The CONTROL moved with it, so a straight comparison
against the earlier numbers would be false progress of exactly the kind this arc
keeps producing:

                        gate frames   control (per-frame)   control (free-run)
    §30e                    256            39.7                 161.1
    here                    176            43.8                 137.1

With that stated, on the 176-frame gate:

    PER-FRAME   mean 19.3   control 43.8   net -24.5   NET CORRECT
                last frame 62 vs control 53 -> +9
    FREE-RUN    mean 147.0  control 137.1  net  +9.9
                last frame 262 vs 217 -> +45   still DIVERGING
    canaries INTACT, best frame 6 wrong

**The per-frame mean is the strongest it has been** (-24.5 against a control of
43.8) and the LAST frame is net +9 -- the reader's own words, "the per-frame deficit
GROWS WITH GAME STATE", and it cannot be accumulation because every frame re-seeds
from the arcade. Later game states are genuinely harder for the port, which is a
different defect from drift and wants a different fix.

The honest summary for a handover: **the banked rom is net correct per frame by a
wide margin and still diverges when free-running**, and the two gate lengths in this
document are not interchangeable.

## 34. STEP 1 OF PORT-ARCH: ROOTING THE REAL MAIN LOOP, AND FOUR BUILD BUGS IT EXPOSED (2026-10-02)

`docs/design/PORT-ARCH.md` §1, approved: root **0x005BE** and let the decompiled
code do its own branching, instead of the driver reimplementing a 15-call sequence
that the arcade conditionally skips.

### What 0x005BE actually is

`roms/altbeast/prog68k.asm:399` settles the naming question in one line:

    5be:  4ff8 ff00     lea 0xffffff00,%sp
    5c2:  46fc 2300     movew #8960,%sr
    5c6:  0c79 ffff 0000 045a  cmpiw #-1,0x45a
    5ce:  6700 fe38     beqw 0x408
    ...
    62a:  4ff8 ff00     lea 0xffffff00,%sp      <-- the loop top
    640:  4eb9 0000 397e jsr 0x397e              <-- wait_frame_flag, 4 instrs in

It installs the stack pointer and the status register and then loops forever. It is
the game's MAIN ROUTINE; Ghidra's `test_mode_screen` is a misnomer. The `cmpiw
#-1,0x45a` is a soft-reset check against a ROM byte that holds 0x4E71 (`nop`), so
it falls through -- the port does not need to spoof it.

### How one frame is extracted from a function that never returns

Invert control. `wait_frame_flag` (0x397E) is REDIRECTED, not stubbed, to
`port_frame_wait` in `sh_src/port_l2.c`. The game loop drives and the driver is its
vsync:

    yield 1   the prologue (0x5BE..0x63E) has just finished. SEED HERE.
    yield 2   one full iteration has run. Work RAM is the answer; leave by
              __builtin_longjmp into the driver's still-live frame.

THE SEED MUST LAND AT YIELD 1, NOT BEFORE THE CALL. The prologue calls
`clear_textram` / `clear_tileram` / `clear_spriteram_and_pool`; a seed written
before entry is erased by them. Seeding at the first vsync is also exactly where
the arcade's capture was taken, so this is the faithful placement rather than a
workaround.

### FREE-RUN IS REFUSED UNDER PORTMAIN, ON PURPOSE

A longjmp cannot RESUME, so every frame re-enters at 0x5BE and re-runs the
prologue. For the seeded frame and the per-frame curve that is harmless -- the
prologue only writes work RAM and the seed overwrites all 16 KB afterwards. For
free-run it is fatal: nothing accumulates across the boundary, and the curve would
come out as an exact duplicate of the per-frame curve wearing free-run's label.
That is the shape of every false number this project has produced, so the driver
publishes `'NOFR'` in `port_l2m_out[195]` and zeroes the free-run slots instead.
A real resume needs a second stack and a context switch; that is its own step.

### .portcode OVERFLOWED, AND -Os IS THE LEVER

Rooting the main loop takes the closure from 429 functions to 716. At -O2 the unit
is 436,752 bytes of .text, `.portcode` wants 0x76AE4, and the window
(0x0238C000..0x02400000) is 0x74000 -- the link failed with "address 0x2402ae4 ...
is not within region `rom'", over by 10,980 bytes.

    -O2   436,752 B
    -Os   352,820 B      -19%

`CPORT_OPT` (tools/cport_build.sh), exposed as `make ... PORTOS=1`. This rom
measures correctness and drives no pixels, so size is the binding constraint and
speed is not -- but it is still a second variable, so it is A/B'd against the gate
rather than assumed neutral.

### FOUR BUILD BUGS, ALL FOUND BY DOING THIS, ALL FIXED

**1. `.build_flags` NEVER CONTAINED ANY PORT* PROBE FLAG.** The stamp is a
parse-time `$(shell)` at Makefile:3684; every PORT* flag is appended ~550 lines
below it. Measured: the stamp carried `-DPORT_FULL` and not `-DL2M_SEEDAT`, so
`PORTSEED=64` -> `PORTSEED=9` changed nothing make could see and `port_l2.o` was
REUSED. **Every PORT*-flag-only comparison made before 2026-10-02 is a comparison
of two identical objects unless a source file happened to change in between.** The
stamp now sits at the END of the Makefile, where it sees every -D.

**2. THE THUNK GENERATOR SWALLOWED THE FIRST PROTOTYPE.**
`tools/cport_dispatch.py` accumulated raw lines of `protos.h` until it saw a `;`.
The five-line header comment plus `#ifndef`/`#define`/`#include` contain no
semicolon, so they were glued onto the first declaration, the `^`-anchored regex
never matched it, and that handler silently got the NO-ARGUMENT thunk. Invisible
for as long as the first declaration happened to be `uint FUN_00000bd6();`, which
takes no arguments -- so the wrong thunk was the right thunk. Rooting
test_mode_screen moved `FUN_0000057e(byte *p_A1,uint p_D0)` into first place and
gcc said "too few arguments". Now 719 of 719 declarations parse.

**3. A /tmp FILE WAS A BUILD INPUT.** `tools/cport_l2seed.py` read
`/tmp/disp_regs.json`, written ad hoc on 2026-09-28 (commit 16e1b250) and never
committed -- it is ROM-derived, so it could not be. When /tmp was cleared the
PORTFULL build died with a FileNotFoundError and no rom. It now DEGRADES to an
empty seed and says so; the register chain that experiment proved is built into
`cport_prep.py` properly, so it is superseded rather than needed.

**4. THE GENERATED UNIT HAD NO STAMP AT ALL.** `CPORT_ROOTS`, `CPORT_STUBS` and
`CPORT_OPT` decide the unit's CONTENT and none of them is a -D, so none reached
`.build_flags`. `port.o`'s rule saw its two file prereqs unchanged and reused a
unit built from a different root set. There is now a `.port_flags` stamp holding
the root set, the wait-stub form and the opt level, and `port.o` depends on it --
which also makes `CPORT_ROOTS_BASE` safely overridable on the command line, the way
the sixteen-root baseline is reproduced.

### Two strict-mode errors left in the unit, both domain mixes

Under strict flags (no -Wno-error=) the unit has exactly two:

    passing argument 1 of 'tilemap_unpack_hi' makes pointer from integer
    passing argument 2 of 'FUN_0001a58a'      makes pointer from integer

Both are `LD32U(...)` -- a 68K address loaded out of memory -- handed to a
parameter declared as an SH-2 pointer. They are precisely what PORT-ARCH step 3's
`a68` type exists to make impossible, and they are left visible rather than cast
away.

## 35. THE MAIN LOOP RUNS, AND HANGS ON A POINTER ROUND-TRIPPED THROUGH 16 BITS (2026-10-02)

The baseline is REPRODUCIBLE again and matches the banked figure exactly:

    A  16 roots, -O2, four-call body     9 wrong of 16384   control 52   net -43
    C  main loop rooted, PORTMAIN, -Os   HANGS -- no score

C is a HANG, not a result. `port_l2m_out[190]` (written after the frame body) is
still 0 after 8000 ares frames, so the driver entered the seeded frame and never
returned; the 1058 wrong cells a host diff reads off that rom are a MID-FLIGHT
SNAPSHOT and are not a measurement. The other counters agree: frame cost 0 us,
register-chain write-backs 0, port_zoom_scale ran 0 times, and `'MAIN'` never
reached `[192]`.

### WHERE, exactly

`ares-headless --profile` is the instrument (SH-2 PC histogram, both CPUs). The
master spends ~20% of its instructions inside ONE function, spread over two short
PC ranges -- a loop, not a frozen PC:

    0x023CE8F2 +0x2E  2.71%   FUN_0000e16a
    0x023CE8F4 +0x30  2.71%
    0x023CE930 +0x6C  2.01%
    ...

`FUN_0000e16a` (0x0E16A, 126 bytes, class: despawns) scans the object table:

    for (; -1 < *(char *)(psVar10 + 4); psVar10 = psVar10 + 0x40) {
    LAB_0000e1b8:
    }
    ...
    if (!bVar3) { sVar11 = (short)psVar10; }           <-- TRUNCATED to 16 bits
    psVar10 = (short *)(int)psVar10[0x3c];             <-- RE-EXPANDED from 16 bits

The for-loop body is EMPTY -- its only content is a label. It terminates only when
it finds a record whose byte at +8 is negative. `psVar10[0x3c]` is a 16-bit work-RAM
pointer stored in the object record (the `W68` class); cast straight to a pointer it
names SH-2 address 0x0000C880, which is the 32X BIOS. The scan walks off into the
BIOS, finds no sentinel, and never returns.

`w68.h` already carries the correct translation. Nothing applies it here, and NO
TYPE FORBIDS THE MIX.

### THE CLASS IS BOUNDED AND COUNTED

    59 sites in 40 functions   pointer re-expanded from a 16-bit integer, (T *)(int)x
    11 sites                   pointer truncated to a short
    29 sites in  3 functions   writes through register0x0000003c, Ghidra's STACK
                               placeholder -- an uninitialised global in the port

`tools/port_guard.py` now has a fourth check for both forms; the baseline went
8 -> 51 violations, and shrinking it is the measure of progress on the class.
FUN_000049ba -- the player handler the whole register-chain derivation was about --
is in the 40.

### WHAT THIS DOES TO THE APPROVED ORDER

**Step 1 depends on step 3.** PORT-ARCH's order is 1 (call decompiled code), 2
(declare the data model), 3 (the `a68` type). Rooting the main loop reaches code the
four-call body never executed, and that code round-trips pointers through 16-bit
integers. A port with no type separating a 68K address from an SH-2 pointer cannot
run it. The `a68` work is not the last step; it is the precondition for the first.

Step 1's mechanism is sound and stays in (PORTMAIN=1 PORTOS=1, FULL-PORT.md 34);
it cannot be MEASURED until the pointer class is translated.

## 36. THE MAIN LOOP COMPLETES A FRAME, AND IT IS THE WRONG FRAME (2026-10-02)

Three defects fixed in order, each one exposing the next:

### a. The pointer round-trip (22 sites rewritten)

`cport_prep.py` gained a pass with three rules, each decided from a DECLARED width
rather than a shape:

    P1  (T *)(int)(short)<pointer>          ->  (T *)(<pointer>)
    P2  (T *)(int)<16-bit operand>          ->  (T *)W68(<16-bit operand>)
    P3  <16-bit lvalue> = (short)<pointer>  ->  = (short)A68OF(<pointer>)

`A68OF` is new in `w68.h`: the inverse of `W68`, so the value STORED in a 16-bit
cell is the 68K address `W68` will expand. Applied: **0 cancelled, 22 W68-expanded,
2 A68OF-truncated.** 35 of the 59 raw `(T *)(int)` matches already read
`(byte *)(int)W68(...)` -- cast noise around a correct translation -- and the guard
was counting those; it now excludes them, which took the baseline from an
over-counted 51 to an honest **12** (8 A6 widths, 3 stack placeholders, 1 pointer).

It was necessary and not sufficient: still hung, now calling `hide_object_sprite`
from inside the scan rather than only spinning.

### b. register0x0000003c IS THE 68K STACK POINTER

Ghidra names 68K register-space offset **0x3C**, which is **A7**, as
`register0x0000003c`. It could not model `move.w d0,-(sp)` / `move.w (sp)+,d0` as
locals, so the decompiled code spills through it -- including `FUN_0000e16a`'s LOOP
COUNTER and therefore its exit test:

    *(undefined2 *)((int)register0x0000003c + -8) = (short)uVar4;
    hide_object_sprite();
    uVar2 = *(undefined2 *)((int)register0x0000003c + -8);
    if (0x17 < (char)uVar2 + 1) return 0;          <-- the only way out

Defined as a bare `uint` it is 0, so the spill lands at address -8 and the reload
can never terminate the loop. **The value is not a guess:** the program loads it
five times in the listing -- prog68k.asm:272, 326, 365 and at 0x5BE/0x62A -- always
`lea 0xffffff00,%sp`. 0xFFFFFF00 is inside work RAM, so the port's SP is that
cell's SH-2 address and every spill lands where the 68000 put it. `defs.c` now emits
`port_reset_sp()`, and `port_frame_wait` calls it at each frame boundary because
0x62A re-loads SP once per frame and Ghidra dropped that instruction.

**The frame then COMPLETED**: `'MAIN'` in [192], 1 frame, exactly 2 yields,
12 register-chain write-backs, 44.5 ms.

### c. AND IT IS THE WRONG FRAME

    A  four-call body      9 wrong of 16384   control 52   net   -43
    C  main loop rooted  1163 wrong           control 52   net +1111

This is now a real score, and the reason is positional, not arithmetic. The game
writes the 68K address it last passed into **0xFFFFFEFE** -- dead stores to the
decompiler, and the most useful cell in work RAM:

    port 0x0094      arcade 0x0AE2      seed 0x0AE2

0x0AE2 is inside the gameplay frame loop (0x970-0xADC). 0x0094 is not. And the
cells agree: **868 of the 871 wrong cells in 0xFFE000 are ZERO in the port** and
non-zero in BOTH the arcade and the seed -- `clear_textram` / `clear_tileram` /
`clear_spriteram_and_pool`, called at 0x6A0/0x6A6/0x6AC, which sit AFTER the first
wait at 0x640 and therefore INSIDE the measured span.

So entering `test_mode_screen` at its top runs the **state-initialisation pass**.
That is faithful code; it is the wrong part of the program for a mid-gameplay seed.
The crude four-call body, by contrast, reads **0x0AE2 -- exactly the arcade's
position.** It was positioned correctly all along.

### WHAT THIS SETTLES

The mechanism of step 1 is right and the ENTRY is wrong. The arcade at the capture
is suspended at a wait inside 0x970-0xADC; the port has to be put back THERE, and a
`__builtin_longjmp` cannot resume. **The coroutine is not an optimisation for
free-run -- it is the requirement for measuring anything through the real loop.**
A second stack and a context switch (~20 SH-2 instructions: r8-r15 and pr saved,
r15 swapped) is the next step.

`tools/portl2_seed.py` prints the progress marker first now, and says so when the
two machines are in different parts of the program -- a byte count cannot tell
"computed the frame wrongly" from "ran different code", and those need opposite
fixes.

## 37. THE GAMEPLAY FRAME LOOP, ANATOMISED FROM THE LISTING (2026-10-02)

Derived from `roms/altbeast/prog68k.asm`, not inferred. The per-frame loop inside
`test_mode_screen` is:

    HEAD      0x97C    the loop head -- SINGLE ENTRY, seven branches target it
                       (0x952, 0x96C, 0x972 from the service-mode test above it;
                        0xB0E, 0xB6C, 0xB74, 0xB7C from the tail below)
    body      0x97C -> 0xADC
                       0x97C bsr 0x12EC      0x9A4..0xA32  the f14C/f148 ladder
                       0x980 bsr 0x1366      0xA38 jsr 0x3A00   0xA3E jsr 0xD80E
                       0x984 bsr 0x0BD6      0xA4E jsr 0xE052   0xA54 jsr 0x398E
                       0x98E jsr 0x3AAE  OR  0xA5A jsr 0x3716   0xA68 bsr 0x0DFA
                       0x996 jsr 0x9052      0xA7A bsr 0x0ECE   0xAB8 jsr 0x4254
                       (f148 decides)        0xACE jsr 0x17188  0xAD4 jsr 0xDCB4
    VSYNC     0xADC    jsr 0x397E  -- wait_frame_flag. THE FRAME BOUNDARY.
    TAIL      0xAE2 -> 0xB7C
                       dip/credit/continue handling, and 0xB04 bsr 0x144A
    back      0xB0E `cmpi.w #698,(0xfffff02a).w ; bcs 0x97c` and three more
    exits     0xB1A / 0xBAC -> 0x1E54   (game over)   0xBD2 -> 0x62A (state restart)

**THE ARCADE'S CAPTURE IS SUSPENDED AT 0xADC.** Its progress marker reads 0x0AE2 --
the return address of that `jsr` -- which is how we know. So one arcade frame is the
ROTATED span

    0xAE2 -> (tail) -> 0xB7C -> 0x97C -> (body) -> 0xADC

### THIS EXPLAINS THE FOUR-CALL BODY'S -43

`irq4_handler(); FUN_00003aae(); FUN_0000144a(); object_dispatch();` is not an
arbitrary list. In the loop: **0x3AAE sits at 0x98E (head), 0x398E at 0xA54 (body),
and 0x144A at 0xB04 -- in the TAIL.** The four calls straddle the vsync exactly as
the rotation requires, which is why the crude body lands at the arcade's own
position (marker 0x0AE2) and scores -43 while the rooted main loop, entered at the
top of `test_mode_screen`, lands at 0x0094 and scores +1111.

### WHY NEITHER AVAILABLE ENTRY IS THE RIGHT ONE

* **0x5BE** (what PORTMAIN does today) runs the state-initialisation pass first.
  Measured: wrong position, +1111.
* **0x97C** is a clean single-entry loop head, but entering there runs
  `0x97C -> 0xADC` and OMITS THE TAIL -- including `bsr 0x144A` at 0xB04, which the
  four-call body does run. That is the flat list's "skips calls the arcade runs"
  failure in miniature.
* **0xAE2** is the arcade's own resume point and the only entry that gives exactly
  one rotated frame.

0xAE2 is a valid instruction boundary and the span has a single entry, so the next
step is a **Ghidra function split at 0xAE2**, after which the frame body is a rooted
call like any other and the decompiler does its own branching -- which is what
PORT-ARCH step 1 asked for in the first place.

CLAUDE.md requires the Ghidra project to be COPIED before any edit and
`docs/proof/altbeast.c` backed up, because in-place Ghidra edits are not reliably
reversible and every tool in the chain reads that one file. That checkpoint is the
first action of the split, not an afterthought.

### THE COROUTINE IS NOT THE ANSWER TO THIS

Section 36 called for a second stack so `port_frame_wait` could yield instead of
exiting. That is still true for FREE-RUN. It does NOT solve the positional problem:
a coroutine primed at boot resumes in ATTRACT MODE, and reaching the capture's state
would mean replaying 9,064 frames with the right inputs. The 68K's own stack is in
the seeded work RAM, but an SH-2 C call stack cannot be synthesised from 68K return
addresses. **Position comes from the entry point; state comes from the seed.** The
split gives the first, the seed already gives the second.

## 38. THE ROTATED FRAME RUNS, AND ALL NINETEEN REMAINING CELLS ARE NAMED (2026-10-02)

    baseline, four-call body        9 wrong of 16384   control 52   net  -43
    0x005BE rooted               1163 wrong                         net +1111
    0x0AE2 rooted                 132 wrong                         net  +80
    + hand-written despawn index   19 wrong                         net  -33

**STILL 10 BYTES WORSE THAN THE FOUR-CALL BODY. Not adopted.** What changed is that
the port runs the real frame with the decompiler's own branching, and the 68K's own
stack top agrees with the arcade's:

    port 0x0AE2    arcade 0x0AE2    seed 0x0AE2

### THE GHIDRA SPLIT

`tools/ghidra/split_frame_body.py` makes the rotated frame `frame_rotated` at
0x0AE2 -- the address the arcade's suspended `jsr 0x397E` at 0x0ADC pushed, so one
call is exactly one arcade frame. Bodies cannot overlap, so the prologue
0x005BE..0x00629 becomes `main_entry`, which is what it is: it installs SP and SR.

### FUN_0000E16A HAND-WRITTEN (132 -> 19)

From `docs/audit/despawn_index.asm`, 30 instructions. It stamps a depth index
counting up from 0x10 into byte +8 of all 24 object records at 0xFFFFC800, walking
linearly but diverting down the +0x78 link chain and remembering in A5 where it
left the linear walk. The arcade's column is the oracle:

    arcade  10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F 20 21 22 23 24 25 26 27
    port    10 11 12 66 CE FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF

The decompiler cannot express 0xE1C4/0xE1CE -- `movem.w {D1w D0w},-(SP)` / jsr /
`movem.w (SP)+,{D0 D1}`. It flattened the moving SP into fixed offsets from one
`register0x0000003c`, wrote the counter at SP-4 and read it back at SP-6, and
decremented its SP model by 4 per iteration with no restore. Three correct, two
garbage out of the phantom stack, then its own 0xFF initialisation for nineteen.

### ALL NINETEEN REMAINING CELLS, DECOMPOSED -- there is no unknown bucket

    9 cells  THE GATE SUPPLIES NO CONTROLLER INPUT
             0xFFE01E/1F/22/28 and the +0x80 mirror 0xFFE09E/9F/A2/A8 are written by
             FUN_00001366 (`bsr 0x1366` at 0x980, in the loop head) straight out of
             the System 16 I/O window:
                 DAT_ffffe028 = ~DAT_00c41003;      /* player 1 */
                 DAT_ffffe0a8 = ~DAT_00c41007;      /* player 2 */
                 DAT_ffffe01e = DAT_ffffe023 | ~bVar2 & 0xf8;
             plus 0xFFF028, which frame_rotated derives from 0xFFFFF026 -- itself a
             dip/coin input byte. The arcade's 0x40 at 0xFFE028 requires a port read
             of 0xBF; the port reads a constant from its MMIO stub and cannot know
             otherwise.
             THIS IS A GATE DEFICIENCY, NOT A PORT DEFECT. The four-call body never
             reads the controller, so it never writes those cells and never gets
             them wrong -- it leaves the seed values, which are already correct. The
             real frame reads them and is penalised for it.
    2 cells  0xFFF146/147 <- FUN_0000E052's D4 parameter
             (`_DAT_fffff146 = (undefined2)p_D4_00;`). Port 0x05A0, arcade 0x0040.
             The register-convention class, same as the D3/D6 chain.
    1 cell   0xFFF000, a timer. Seed 0x07, arcade 0x07, port 0x03 -- decremented
             four more than the arcade. irq4_handler takes it down by 2 and
             FUN_00001192 / FUN_00002ee4 by 1.
    7 cells  shared with the baseline: 0xFFED00, 0xFFED10, 0xFFED11, 0xFFF01C,
             0xFFF134, 0xFFF352, 0xFFF353.

And the rotated frame FIXES two cells the four-call body gets wrong: 0xFFF02B and
0xFFF02D. The arithmetic closes exactly: 9 - 2 + 9 + 3 = 19.

### THE NEXT STEP IS AN INPUT MODEL, AND IT IS THE LARGEST SINGLE ITEM

A frame's inputs are the seeded work RAM **plus the controller and dip state**,
which is not in work RAM and which the capture does not record. Nine of nineteen
cells are that omission. The fix is to capture the System 16 I/O bytes per frame
alongside the work-RAM deltas (`tools/l1_oracle.lua`, `tools/l2_pack.py`) and have
the port's MMIO stub return the frame's own values instead of a constant. That is a
gate change, it is derivable from the arcade, and it is not circular: the input is
an INPUT to the frame, declared like the seed, not read back from the answer.

## 39. THE GATE GETS THE FRAME'S OTHER INPUT: 19 WRONG -> 10, NET -42 (2026-10-02)

    baseline, four-call body     9 wrong of 16384   control 52   net -43
    rotated real frame          19 wrong                         net -33
    + the frame's own I/O       10 wrong                         net -42

ONE BYTE off the hand-rolled body, while running the whole real frame with the
decompiler's own branching and standing where the arcade stands (stack top 0x0AE2).

### THE CAPTURE, AND THE TWO WAYS OF TAKING IT THAT ARE WRONG

`tools/l2_oracle.lua` now records the nine ports the program reads, per frame, beside
the work-RAM dump; `tools/l2_io_pack.py` packs them (3,144 bytes, 258 frames) and
`sh_src/port_l2_io.s` rides them in `.portdata`. All three methods were measured:

    mem:read_u8            a bus access through the handler; consumes an input latch
    install_read_tap       NEVER FIRES, and refuses an odd start address. The 68K's
                           accesses go through the 315-5195 mapper, so a
                           program-space tap at 0xC40000 does not see them -- which
                           CLAUDE.md already says about taps on S16B
    ioport:read()          no bus access at all. THIS ONE.

The tag map is CONFIRMED, not assumed: `:DSW2`/`:DSW1` read 0x54/0x33, and a bus-read
capture read 0x54 at 0xC42001 and 0x33 at 0xC42003.

**NOT CIRCULAR, AND CHECKED.** The controller state is an INPUT declared like the
seed, never read back out of the answer. The check is a different thing and it
passes: `~io(N)[0xC41003]` equals work RAM's 0xFFE028 at frame N+1 for 239 of 257
frames (the other 18 have that write gated off); player two is 257 of 257. At frame
9064, `~0xBF = 0x40` = the arcade's cell exactly.

### THE MMIO STUBS WERE SPREAD OVER 8.6 MB OF A 256 KB RAM

`cport_layout.py` placed them linearly from `mmio_base`, so 14 symbols fell outside
SDRAM and reached a cell only because the 32X mirrors every 256 KB. Two consequences
nothing reported: `DAT_00440000` (video) and `DAT_00840000` (palette) COLLIDED on one
cell, and `DAT_00c41003`/`DAT_00c41007` -- the two controller ports -- sat outside the
driver's 4 KB fill, reading uninitialised SDRAM. Now compacted one 4 KB page per
distinct 68K page (9 pages, 36 KB), the layout REFUSES on a collision, the driver
takes the size from the linker, and the nine I/O ports are placed whether or not the
root set references them.

### THE ORACLE IS NOT DETERMINISTIC, AND THAT IS NOT MY CAPTURE

Two identical runs -- same script, same playback, same flags -- differ by **13 bytes
at the same 13 addresses in every frame**: 0xFFFF02..0xFFFF95, two 0x80-apart blocks.
That is what every 12-to-14-byte difference in this session was, including the one I
first reported as "the bus read is destructive". **WITHDRAWN.**

So `docs/proof/l2m/wram_*.bin` is NOT byte-reproducible. The committed corpus is
deliberately left untouched -- only `io_*.bin` was added. None of the 13 wobbling
cells has ever appeared in a port wrong-cell list, so no measurement is invalidated,
but a re-capture would move the baseline by up to 13.

## 40. THE LAST THREE CELLS (2026-10-02)

Of the 10, seven are also wrong under the four-call body. Three are the rotated
frame's own, and the rotated frame FIXES two the four-call body gets wrong (0xFFF02B,
0xFFF02D).

**0xFFF146/147 -- `FUN_0000E052` walks its table 172 entries too far.** Port 0x05A0,
arcade 0x0040, and the loop advances `_DAT_fffff146` by 8 an iteration, so
(0x5A0-0x40)/8 = 172. Every documented input to that function MATCHES the arcade:

    DAT_fffff142 mode  0x01 / 0x01      _DAT_fffff132  0x10D4 / 0x10D4
    DAT_fffff030       0x02 / 0x02      _DAT_fffff138  0x10D4 / 0x10D4
    DAT_fffff15a/154/15d all equal      _DAT_fffff146  0x05A0 / 0x0040  <- the output

And the ROM table is placed correctly (`DAT_0001d33e` -> 0x0235D33E) and reads right:
from `prog68k.bin`, `p_A0 = 0x1D340 + 0x0750 = 0x1DA90`, and at `p_A0 + 0x40` the
entry is `0001 10E8 0190 1B00`. The arcade exits on the SECOND test --
`uVar2 (0x10D4) < *(ushort *)(p_A0+0x42) (0x10E8)` -- with `p_D4_00` unchanged.

So the divergence is in a value read DURING the frame that an end-of-frame dump
cannot show. The call is `FUN_0000e052(p_D1, unaff_D4)`, and **D4 is not in the
register chain** (`_CHAINREG = D3, D6, D7`). `p_D4`'s low half is overwritten by
`_DAT_fffff146`, so it cannot be `sVar4` directly -- which means the next instrument
is the watchpoint on 0xFFF146, not another reading of the chain.

**0xFFF028** is the credit/player-active byte, written in the tail as
`DAT_fffff028 = bVar8 & 0xb` from 0xFFFFF026 -- a dip/coin byte. Port 0x00, arcade
0x01: a branch the port takes differently, in the same input family that the capture
has just fixed eight of.

## 41. THE REAL FRAME BEATS THE HAND-ROLLED BODY: 8 WRONG, NET -44 (2026-10-02)

    four-call body (the baseline)    9 wrong of 16384   control 52   net -43
    the rotated real frame           8 wrong                         net -44

**ADOPTED.** `make line NOSPRBAKE=1 NOTILESMD=1 PORTFULL=1 PORTSEED=64 PORTMAIN=1
PORTOS=1 RIGBARCODE=1 WRAMCODE=1`

The port now runs a whole arcade frame of decompiled 68K code with the decompiler's
own branching, stands exactly where the arcade stands (stack top 0x0AE2), and
reproduces 16,376 of 16,384 work-RAM bytes -- better than the four hand-picked calls
it replaces. The cell lists line up exactly:

    shared with the baseline   7   FFED00 FFED10 FFED11 FFF01C FFF134 FFF352 FFF353
    the real frame FIXES       2   FFF02B FFF02D
    the real frame introduces  1   FFF028
    9 - 2 + 1 = 8

### THE LAST TWO CELLS WERE ONE GENERATOR LINE

`FUN_0000E052` was walking its table 172 entries past the end, leaving 0xFFF146 at
0x05A0 where the arcade has 0x0040. Every documented input matched the arcade, the
ROM image was byte-identical to `prog68k.bin`, and the table read correctly on paper.
The fault was in the generated C, and it took two fixes:

**a. A DECLARATION WIDER THAN THE ARCADE'S ACCESS TAKES THE WRONG HALF.**
`_DAT_fffff132` is declared `uint` (4) and the arcade only ever accesses it as a word
(2). `FUN_0000E052` does `ushort uVar2 = _DAT_fffff132`, so the port read 32 bits and
the narrowing assignment took the LOW half -- the word at 0xFFF134, not 0xFFF132.
Its exit test `uVar2 < *(ushort *)(p_A0 + sVar4 + 2)` then never fired.

`cport_prep` already had a clamp to the OBSERVED width, gated on another declared
symbol sitting inside the span. Nothing is declared at 0xFFF133/134/135, so it never
fired. That pre-condition was only ever a cheap way to FIND candidates; `CPORT_CLAMP`
is the gate's one-at-a-time arbitration, so an explicitly allowlisted address now
bypasses it. 80 of 715 symbols disagree with the observed table, 73 of them declared
WIDER -- the direction that truncates to the wrong half.

**b. A SELF-RELATIVE ROM TABLE IS NOT A 16-BIT WORK-RAM POINTER.** The `_SX16` rule
rewrites `(int)*(short *)(...)` to `(int)W68(...)` -- the right translation for a
work-RAM address held in 16 bits -- with a lookahead meant to skip a value that is an
ADDEND rather than an address. The lookahead recognised only `+ identifier`, and the
pc-relative table idiom is written `+ (int)...`:

    p_A0 = (undefined *)((int)*(short *)(&DAT_0001d33e + idx)
                       + (int)(&DAT_0001d33e + idx));

That 16-bit value is the 68000's `lea %pc@(table),%a0 ; adda.w (a0,d0.w),%a0` offset.
W68 turned 0x0750 into `ROM_BASE + 0x750` and the sum was garbage, so the routine
walked a table that is not a table. **This is also what the older "hangs in 0x0E052"
was.** Widening the lookahead to any addend changes exactly **ONE SITE in the whole
unit**, `FUN_0000E052`'s -- measured, 91 rewrites before and 90 after.

### WHAT IS LEFT: 8 CELLS, AND SEVEN ARE OLDER THAN TODAY

Seven are wrong under both bodies and so are not about the frame loop at all:
0xFFED00/10/11 (a sprite-list block), 0xFFF01C, 0xFFF134, 0xFFF352/353. The eighth,
0xFFF028, is the credit/player-active byte the real frame writes from 0xFFFFF026 and
the four-call body never touches -- the same dip/coin input family the capture has
just fixed eight of.

## 42. THE FOUR-CALL BODY NEVER ADVANCED THE GAME, AND ONE DECLARATION WAS THE FREE-RUN (2026-10-02)

### The gate was comparing ports that do not run

0xFFFFF02A is the game's own frame counter, incremented by `FUN_000012EC` at the loop
head (0x97C). Nobody had ever read it. Across one seeded frame:

    arcade                                      +1
    the four-call frame body                    +0    <- never reached the loop head
    the rotated real frame                      +1

**The four-call body's net -43 was a score for something that is not a frame.** It
calls `irq4_handler, FUN_00003aae, FUN_0000144a, object_dispatch` and none of them is
`FUN_000012EC`, so the counter cannot move. A cell count cannot tell "ran the frame
and got cells wrong" from "did not run the frame", and the second scores BETTER
because the control is also nearly still.

Both readers now print the counter FIRST and refuse to let the cell count stand
alone.

### The free-run stall was one over-wide declaration

Free-run looked like it was *diverging* (net +1.7 B/frame). It was *stopping*: 129.5
of 138.8 wrong cells per frame were MISSED -- cells the arcade changed and the port
did not -- and from f7 onward the curve tracked the DO-NOTHING control almost exactly.

The 68K's own stack top says where it stopped: after 8 free-run frames the port sat
at **0x0B94**, the wait inside the NO-PLAYERS path at 0xB80, while the arcade was at
0x0AE2. The listing says why:

    b70: tst.b 0xfffff028 ; bne 0x97c     <- the arcade has 1, branches into the frame
    b78: tst.b 0xfffff029 ; bne 0x97c
    b80: bsr 0x14e8                       <- the port falls here and waits forever

`DAT_fffff028` is declared `ushort` by Ghidra. **Every one of the 45 accesses to it in
the whole program is a BYTE access** -- `cmpib`, `btst`, `moveb`, `bclr`, `bset`,
`tstb`, `clrb`, not one word -- and the arcade's observed width is 1. So
`FUN_00000DFA`'s `bclr #5,0xfffff028` came out as a 16-bit read-modify-write:
`0x0100 & 0xDF = 0x0000`, zeroing the credit byte AND its neighbour. The player went
inactive and the game stopped.

    CPORT_CLAMP ?= FFFFF142,FFFFF132,FFFFF028

With it, after 8 free-run frames the counter advances **8 of 8**, exactly matching the
arcade, and the stack top is back on 0x0AE2.

### WHAT IT COST ON THE OLD GATE, AND WHY IT IS STILL RIGHT

    seeded frame, f028 as ushort     8 wrong   net -44   counter +1, leaves f028 = 00
    seeded frame, f028 as BYTE      71 wrong   net +19   counter +1, leaves f028 = 01
    free-run 8 frames, ushort       55 wrong   net  +1   counter advanced 1 of 8
    free-run 8 frames, BYTE        330 wrong   net +276  counter advanced 8 of 8

The declaration is proved correct by the instruction stream, and the regression is
NEWLY EXECUTING CODE: with the player active the port runs the player logic it had
been skipping and gets 63 more cells wrong. That is the `.portrom` pattern -- a worse
number and a better port -- and this time there is an independent discriminator
saying so, which there was not before.

**ADOPTED.** The 63 cells in the player path, and the 330 at eight free-run frames,
are the honest baseline to work down from.

## 43. THE CAMERA IS 32-BIT, AND A `.w` READ OF IT TAKES THE HIGH HALF (2026-10-02)

Adopting the 0xFFF028 byte declaration let the player run, and the player came out
wrong: eight 16-bit fields of record 0 were ALL high by exactly **+0x1FE0**, and the
watchpoint named `FUN_000049BA` -- the player handler -- writing them from

    *(ushort *)(unaff_A6 + 0xc) = _DAT_fffff132 + 0x18U;

### 0xFFFFF132 IS A 32-BIT FIXED-POINT CAMERA POSITION

`FUN_000039C8`, which the watchpoint on 0xFFF120 named:

    _DAT_fffff138 = _DAT_fffff132;
    _DAT_fffff132 = _DAT_fffff132 + _DAT_fffff158 * 0x100;        /* velocity, 1/256ths */
    _DAT_fffff120 = -((short)((uint)_DAT_fffff132 >> 0x10) + -0x1000);

The integer part is at 0xFFF132 and the fraction at 0xFFF134. **I CLAMPED IT TO A WORD
AND KILLED THE CAMERA** -- `>> 0x10` became constant zero, so 0xFFF120 came out as the
literal 0x1000 against the arcade's 0xFF2C. The arcade's "observed width 2" is an
UNDER-SAMPLE: the capture window saw the `move.w` reads of the top half and not the
long arithmetic. **REVERTED.**

### THE REAL FAULT: A `.w` READ AT THE BASE OF A LONG

    0049DE  move.w  (0xfffff132).w,D0w        <- FUN_000049BA
    00E068  move.w  (0xfffff132).w,D0w        <- FUN_0000E052

reads the two bytes AT 0xFFF132 -- on a big-endian machine the TOP half. Ghidra
renders it as a bare narrowing assignment, `uVar2 = _DAT_fffff132;` with uVar2 a
ushort, and **C defines that as the LOW-order bits**: the bytes at 0xFFF134, the far
half. Hence the player's 0x30EC where the arcade has 0x110C.

`cport_prep` now rewrites it to `(ushort)(X >> 0x10)`. **THE EXCLUSION IS THE
INTERESTING PART.** `rng_next` does

    (short)(_DAT_fffff014 >> 0x10) + (short)_DAT_fffff014

where the bare narrowing really IS the low half -- both halves of one long in one
expression, genuine 32-bit arithmetic and not a memory read. A function that also
takes the symbol's high half is therefore left alone. Of the seven candidates in the
program that is the only one; the other six are all 0xFFFFF132.

### WHERE IT STANDS

    four-call body          9 wrong  net -43   counter +0  <- NEVER RAN A FRAME
    rotated, f028 ushort    8 wrong  net -44   counter +1, but leaves f028 = 00
    rotated, f028 byte     71 wrong  net +19   counter +1, camera dead
    + the high-half fix    25 wrong  net -27   counter +1, camera right

    free-run, 8 frames    315 wrong            counter advanced 8 of 8

**The seeded frame's remaining 25**: rec 16/17 fields +4B/+4C/+76 (the port writes
0xD400 and 0x0100 where the arcade writes nothing), rec 40 +00/+0C (0xEDF0 against
0x8000 -- a record that should read inactive), and the seven that were always wrong
(0xFFED00/10/11, 0xFFF01C, 0xFFF134, 0xFFF352/353) plus 0xFFF141 and 0xFFFC31-35.

**The free-run's 315**: only **7 are MISSED** -- the port is running everything and
computing wrong values, concentrated in object records 16, 17 and 19 (60+ cells each).
That is a per-handler correctness grind, not another structural fault, and it is the
first time the gap has had that character.

## 44. THE FREE-RUN GAP IS THE `a68` CLASS, WITH A NAMED FUNCTION BEHIND IT (2026-10-02)

The free-run's 315 wrong cells at 8 frames are concentrated in object records 16, 17
and 19. Watching record 16's first wrong field (0xFFC84C) names the chain:

    FUN_0000d608  <-  walk_player_slots  <-  FUN_0000ee80  <-  object_dispatcher

`FUN_0000D608` is the collision test. It writes

    *(short *)(unaff_A6 + 0x4c) = (short)p_A4;      /* the collider's 68K address */

and the port stored **0xD400** where the arcade stored nothing. 0xD400 is
`WRAM_BASE + 0x800` with its low word taken -- the A68OF class, a pointer truncated
without translation.

### THE PARAMETER HOLDS BOTH DOMAINS AT ONCE

    uint FUN_0000d608(int p_A4)

and `walk_player_slots` calls it four ways:

    FUN_0000d608(unaff_A6);              /* an SH-2 pointer */
    FUN_0000d608(0xffffc000);            /* a BARE 68K ADDRESS */
    FUN_0000d608(0xffffc400);            /* a BARE 68K ADDRESS */
    FUN_0000d608((int)(&DAT_ffffc080));  /* an SH-2 pointer */

The body then DEREFERENCES it (`*(char *)(p_A4 + 0x48)`) and TRUNCATES it
(`(short)p_A4`). On the SH-2, dereferencing 0xFFFFC000 reads the **on-chip peripheral
register file** (0xFFFF8000-0xFFFFFFFF on the SH7604), so a collision is decided by
whatever a peripheral register happens to hold.

The generator translated `&DAT_ffffc080` -- a symbol -- and left the bare literals
alone, because Ghidra rendered those as integer constants and nothing distinguishes a
68K address from an integer in an `int`.

### THE SCALE, AND WHY IT IS NOT A HEURISTIC FIX

**1,238 sites in 253 functions** use a bare 32-bit 0xFFFFxxxx constant where it could
be an address. Not all of them are: `sound_enqueue(0xffffff92)` is the sound command
-110. Telling them apart needs to know whether the callee treats the parameter as a
pointer, and `FUN_0000D608`'s is declared `int`, so the declaration cannot answer it
either.

`cport_prep` already carries the scar from trying: *"A blanket rewrite substituted 372
sites for the 2 that actually warn and produced 339 errors"*.

**So this is PORT-ARCH step 3 -- the `a68` type -- and its status changes.** It was
"a precondition for step 1" (FULL-PORT.md 35). Step 1 is done without it. It is now
**the thing standing between the port and free-run accuracy**, which is the only thing
standing between the port and a playable build (PORT-OUTPUT.md 3).

## 45. `exg A4,A6` IS NOT MODELLED, AND IT OWNED THE RECORD-16/17 CLUSTER (2026-10-02)

The free-run error concentrated in object records 16, 17 and 19. Watching record 16's
first wrong field (0xFFC84C) gave the chain
`FUN_0000D608 <- walk_player_slots <- FUN_0000EE80 <- object_dispatcher`, and the 68K
named the fault outright (`docs/audit/walk_player_slots.asm`):

    00D5A8  lea  (-0x4000).w,A4     ; A4 = 0xFFFFC000, the player record
    00D5B4  exg  A4,A6              ; SWAP
    00D5B6  bsr  0x0000d608         ; ... so A6 is the PLAYER for this call
    00D5B8  exg  A4,A6              ; swap back
    00D5BE  bsr  0x0000d608         ; and A4 is the player for this one

Ghidra renders the pair as `FUN_0000d608(unaff_A6)`. **It gets A4's VALUE right and
never models that A6 ITSELF CHANGED for the duration of the call** -- a decompiler has
no way to say "this global is temporarily something else across a call". So inside
`FUN_0000D608` the port read and wrote `_r_unaff_A6` = the OBJECT where the arcade has
the PLAYER, and the collision result landed on record 16's +0x4C instead of the
player's. That is precisely why those cells read *"we wrote it; the arcade did not"*.

`exg %a4,%fp` appears **nine times in the whole program** -- eight of them here, one at
0xD54C -- plus a single `exg %a0,%a1`. Ten sites. Not a broad class.

`port_walk_player_slots` is hand-written from the archived listing with the exchange
modelled. Third member of the class after `zoom_scale_lookup` (six outputs against a
two-register return) and `FUN_0000E16A` (`movem` to `-(SP)`).

    seeded frame   25 wrong net -27   ->   18 wrong net -34
    FIXED, and exactly the cells predicted: FFC84B FFC84C FFC876
                                            FFC8CB FFC8CC FFC8CD FFC8F6
    nothing broken
    free-run 8 frames  315 -> 241 wrong, still advancing 8 of 8

### A PASS THAT MISSES ITS TARGET NOW FAILS THE BUILD

Writing the parameter-normalisation pass took three attempts -- wrong pipeline
position, then a brace the pattern did not expect, then an insertion point that landed
PAST THE CLOSING BRACE at file scope -- and each failure was SILENT: the build
succeeded and the rom measured the same. `CPORT_EXPECT` now names the functions a pass
is aimed at and `cport_prep` REFUSES if any came out untouched. A regex over generated
C that stops matching after some other pass reshapes the code is the most common
failure in this toolchain and it had no detector.

**And the normalisation measured ZERO on the seeded frame** -- identical 25-cell set
before and after -- so it is not claimed as a fix. It is correct (the two domains are
disjoint by range) and it bought nothing here; `exg` was the whole cluster.

## 46. PACKED BCD IS NOT IMPLEMENTED (2026-10-02)

Watching record 48's first wrong field (0xFFD800) at frame 176 gave the chain
`FUN_00003774 <- FUN_0000374a <- FUN_00003716`, and 0x3716 is in the arcade's frame
body. The function is the score adder (`docs/audit/bcd_score_add.asm`):

    003774  lea   (0x8,A6),A1
    003778  move  #0x0,CCR
    00377C  abcd  -(A1),-(A0)      x4, LOW byte first, carry chaining through X
    003784  bcc.b 0x0000378c
    003786  move.l #-0x66666667,(A0)   ; saturate to 0x99999999 on overflow

**Ghidra renders each `abcd` as `*p = *p + *q; bcdAdjust(*p);`** -- where `bcdAdjust`
is an intrinsic it does not define (`defs.c` emits `uint bcdAdjust(){return 0;}`) and
the call's result is discarded anyway. So the port did a plain BINARY add and lost
both the decimal adjust and the X-flag carry chain linking the four bytes. Record 48's
score came out 0xDCBC against the arcade's 0x8000.

**151 `abcd`/`sbcd`/`nbcd` instructions in the program, 33 `bcdAdjust` call sites in
the unit** -- the score and counter arithmetic throughout the game.

`port_bcd_score_add` is hand-written with a real packed-BCD add and the overflow
saturation. Fourth member of the hand-write class, after `zoom_scale_lookup`,
`FUN_0000E16A` (`movem` to `-(SP)`) and `walk_player_slots` (`exg A4,A6`).

    frame  64   18 wrong net -34   ->   15 wrong net -37
    frame 176   19 wrong net -14   ->   16 wrong net -17
    fixed 0xFFFC31/32/34/35 (sprite record 67); one new cell, 0xFFD802

### WHERE THE PORT STANDS

    the four-call body (superseded)    9 wrong net -43   DID NOT ADVANCE THE GAME
    the real rotated frame, f64       15 wrong net -37   advances
    the real rotated frame, f176      16 wrong net -17   advances
    free-run, 8 frames               241 wrong           advances 8 of 8

Per-frame accuracy is consistent across game moments (15-16 of 16,384). **Free-run is
the open problem and it is pure compounding** -- ~28 new wrong cells a frame feed the
next frame's inputs. The 33 remaining `bcdAdjust` sites are the next bounded class.

## 47. THE SCORE-UPDATE REWRITE MEASURED WORSE AND IS REVERTED (2026-10-02)

`FUN_0000374A` is the only one of the eight `bcdAdjust`-bearing functions that EXECUTES
in a measured frame (ares `--profile`), and it has a second fault beyond the one §46
fixed: the 68K FALLS THROUGH at 0x3770 into the adder a second time
(`lea (0x10,A6),A0`), which Ghidra INLINED -- so redirecting `FUN_00003774` fixes the
first add and not the second. It also reads `p_A0` before it is ever assigned.

Hand-writing it measured **15 wrong -> 20**, fixing nothing and breaking five cells:

    FFD40E  port 01 / arcade 00
    FFF010..13  port 99 99 99 99 / arcade 00 41 28 00    <- THE HIGH SCORE

**The saturation did it.** 0x3784/0x3786 is `bcc.b ; move.l #-0x66666667,(A0)` --
0x99999999, the packed-BCD maximum, written when the four `abcd` carry out. My adder
raised that carry, the score saturated, and the promote then copied 0x99999999 into
the high score at 0xFFFFF010.

Two things that matter more than the revert:

* **Ghidra marks 0x3786 "Removing unreachable block"** -- in its analysis the carry
  never happens. The bytes being added are not necessarily valid packed BCD (a nibble
  above 9 makes a single `-10` adjust insufficient), so a literal implementation
  raises a carry the 68000 does not.
* **The previous version was WRONG IN A WAY THAT CANCELLED.** Its promote compared
  against `p_A0`, read uninitialised, so the branch was garbage and happened not to
  promote. Making the comparison correct exposed the saturation. A port can be wrong
  twice and measure right.

Reverted; back to 15 wrong, net -37. The remaining BCD work needs the 68000's actual
carry semantics for non-BCD operands, which is a hardware question
(`srcref/jtcores`), not a transcription one.

## 48. THE BCD CARRY IS A HARDWARE QUESTION, AND BOTH MODELS ARE WRONG (2026-10-02)

The saturation at 0x3786 was tested both ways against the gate:

    WITH the saturation      15 wrong net -37
       but record 40's 0xFFD400..03 reads 0x99999999 against the arcade's 0x80000001
    WITHOUT it               19 wrong net -33
       sprite record 67 (0xFFFC31/32/34/35) goes wrong instead

**Neither is the 68000's semantics.** Ghidra marks the block "Removing unreachable
block", so on real data the hardware's carry evidently does not fire there; my adder
raises one because the operands are not always valid packed BCD and a single `-10`
adjust per digit is not what the 68000 does with a nibble above 9.

Kept the saturating version because it measures better, and said so in the code rather
than letting a future reader think the model is right. **The real answer is the
68000's `abcd` carry behaviour for non-BCD operands, which is a hardware question for
`srcref/jtcores`, not a transcription one** -- and it is the first time in this arc
that the next step has been a hardware derivation rather than a decompiler defect.

## 49. THE 68000's abcd, DERIVED FROM THE RTL -- AND IT MEASURES IDENTICAL (2026-10-02)

`srcref/S32X_MiSTer/rtl/FX68K/fx68kAlu.sv`, module `aluCorf` (462-494). **The 68000
does not do a digit-wise BCD add.** It does a BINARY add and applies a decimal
CORRECTION FACTOR, and the carry comes out of the correction:

    lowC   = half-carry | (low nibble of the binary sum > 9)
    htemp  = binary sum + (lowC ? 6 : 0)                    9 bits
    highC  = binary carry | (htemp's high nibble > 9) | htemp bit 8
    hNib   = htemp[8:4]  + (highC ? 6 : 0)                  5 bits
    result = hNib[3:0] : htemp[3:0]
    carry  = hNib[4] | binary carry

with `> 9` tested as `nib[3] & (nib[2] | nib[1])`. Derived, not copied.

**It measures EXACTLY the same: 15 wrong, net -37, not one cell different.** Kept
anyway -- the previous version was invented and this one is derived from the
authoritative spec, and the gate not being able to tell them apart is a fact about
this frame, not about the code.

### WHICH MOVES THE REAL QUESTION

The saturation still fires, so with correct 68000 semantics the carry genuinely is
raised. That means the arithmetic is no longer the suspect. Record 40 at 0xFFD400 holds
**0x80000001 in the arcade both before and after the frame** -- an object flags word,
not a score, and *"we wrote it; the arcade did not"*. So the port is calling the score
adder with A6 = record 40 where the arcade does not call it at all.

**The remaining four cells are a CONTROL-FLOW difference, not an arithmetic one**, and
that is a different hunt from the last four.

## 50. GHIDRA DROPS `lea ...,A6` AT A CALL SITE: 15 WRONG -> 8 (2026-10-02)

    003720  lea (-0xfd8).w,A5     ; A5 = 0xFFFFF028
    003724  lea (-0x2000).w,A6    ; A6 = 0xFFFFE000, player 1's score block
    00372E  bsr 0x0000374a
    003730  clr.l (0x4,A6)
    003738  lea (-0x1f80).w,A6    ; A6 = 0xFFFFE080, player 2's
    003742  bsr 0x0000374a

**Ghidra drops both `lea`s.** It FOLDED A6 at the uses it could see -- `clr.l (0x4,A6)`
came out as `_DAT_ffffe004 = 0` and `_DAT_ffffe084 = 0`, so it knew the value perfectly
well -- and then emitted `FUN_0000374a()` with no argument and no A6. A6 is a global in
the port, so the callee ran against whatever the last dispatch left: **record 40 at
0xFFD400**. The score adder BCD-added into an object's flags word, which is the whole
reason 0xFFD400..03 read 0x99999999 against the arcade's 0x80000001 -- and why the BCD
saturation looked like the culprit for two rounds.

**210 instructions in the program load A6** (94 `lea`, 65 `movea.l`, 51 `movea.w`).
Same family as `exg A4,A6`, and the larger half of it.

    15 wrong net -37  ->  8 wrong net -44
    FIXED: FFD400 FFD401 FFD402 FFD403 FFD40C FFD40D FFD40F   (nothing broken)
    the object-record region goes to ZERO

**Eight cells left at the seeded frame**, and none is in an object record:

    FFED00 FFED10 FFED11   a sprite-list block
    FFF01C FFF134 FFF141   globals
    FFF352 FFF353

Those seven were wrong under the four-call body too (FULL-PORT.md 41) -- they are
older than the frame-loop work and have never been touched.

## 51. THE 176-FRAME CURVES AFTER THE A6 FIX (2026-10-02)

    PER-FRAME (independent)   mean 18.3 wrong   control 43.8   net -25.6
    FREE-RUN  (compounds)     mean 168.7        control 137.1  net +31.7

and at eight frames, free-run is **31 wrong against a control of 54, net -23, NET
CORRECT** -- the first time a free-run has beaten doing nothing.

So the shape is now: **correct for short runs, compounding over long ones.** It
crosses from net-correct to net-wrong somewhere between 8 and 176 frames, which is a
curve that can be bisected rather than a wall. Before the A6 fix the free-run mean was
491.3 (net +354.2); before the 0xFFF028 fix it was "net +1.7" only because the port was
frozen and scoring like the control.

Per-frame is stable at 18.3 across the whole gate, so the compounding is the whole
remaining gap.

## 52. THE A6 CALL-SITE TABLE, AND WHY A BARE ASSIGNMENT BREAKS THE GAME (2026-10-02)

`FUN_00003716` proved the class is worth a lot: ONE restored A6 took the seeded frame
15 -> 8 and the 8-frame free-run 241 -> 31, its first NET CORRECT. 210 instructions in
the program load A6, so the class was scanned mechanically.

`docs/audit/a6_callsites.json`: every (caller, callee, A6) where A6 is loaded with a
WORK-RAM CONSTANT and a call follows before it is reloaded. **27 triples, 20
unambiguous** (the caller calls that callee exactly once). Derived from the
disassembly, not guessed.

### A BARE ASSIGNMENT DROVE THE FRAME COUNTER TO -1217

Applying all twenty as `_r_unaff_A6 = W68PTR(x);` before the call left the seeded frame
at 8 wrong -- unchanged, apparently harmless -- and **the 8-frame free-run advanced the
game's own counter -1217 of 8**. Only the progress check (FULL-PORT.md 42) saw it; the
cell count said 150 wrong and would have been read as ordinary drift.

**A6 is a GLOBAL in the port and the 68000's is not.** On the 68000 every consumer
loads A6 for itself, so nothing downstream depends on the value surviving a call. In
the port the assignment PERSISTS, and the next dispatched handler inherits a pointer to
a credit byte where it expects an object record. The insertions now save and restore:

    { byte *_a6sv = _r_unaff_A6;
      _r_unaff_A6 = (byte *)W68PTR(0xFFF028u);
      FUN_00004276();
      _r_unaff_A6 = _a6sv; }

With that, **all twenty apply safely**: seeded frame 8 wrong net -44, free-run 8 frames
31 wrong net -23 NET CORRECT, counter 8 of 8. `CPORT_A6` is opt-in per caller with
`all` as the measured default, so the gate can still arbitrate one at a time.

### AND THE TABLE GENERATOR HAS A KNOWN HOLE

It walks forward from the `lea` to the next call WITHOUT stopping at the function
boundary. At 0x4272 that ran past the end of `FUN_00004254` and attributed
`bsr 0x4212` -- which belongs to `FUN_00004276` -- to the wrong caller. That entry
matched no call in the C and so did nothing, but the next one like it might. The
fallthrough-into-the-next-function idiom (0x3770, 0x4276) is common in this program and
is what makes the boundary matter.

## 53. THE OUTPUT PATH IS A LAYOUT DECISION (2026-10-03)

The 32X renderer has never read System 16 memory. It reads four SDRAM shadows that
the patched 68K fills through `tools/patch_game.py` `remap()`:

    S16 region        remap() target        renderer shadow      size
    0x400000 tile  -> 0x852000 FB staging -> TILEMAP_U 0x19000   53,248 B (13 pages)
    0x410000 text  -> 0x85F000 / 0xFF8000 -> TEXT_U    0x26000    4,096 B
    0x840000 pal   -> 0xFF9000 MD mirror  -> PAL_SH    0x27000    4,096 B
    0x440000 spr   -> 0x85E000 FB staging -> SPR_SNAP  0x28400    1,024 B

The port writes that same S16 state, on the SH-2, into SDRAM. So
`tools/cport_layout.py` now places the port's video regions AT those four addresses
and **the port's picture IS the renderer's input** -- no transport, no packet, no
DREQ, no per-frame cost at all.

The addresses and the sizes are PARSED OUT OF `sh_src/m_main.c`, each size the
distance to the next define in that SDRAM map, so a renderer-side re-carve carries
the port with it. A hand-copied constant would be a silent wrong answer the first
time that map moves.

Seeded frame UNCHANGED at 8 wrong / net -44 -- the result that matters, because the
bases moved and nothing else did. The generator's new check passed first time: no
port symbol falls past a shadow's end (tile pages 13-15, sprite records 64+), so the
game's own footprint fits the renderer's shadow sizes exactly.

`PORTVIDEO=1` stops the renderer capturing the 68K's staging over the two regions
the port now owns -- `cap_page` (tilemap) and `text_capture`'s FB_SPR loop
(sprites). Text and palette keep the old path: their delivery is fused into the R60
packet lift's change detection (PAL_SETGEN, cram_paint_spr), so replacing the
producer there is a delta scan, not a gate.

### WHAT IS NOT SOLVED: 25 KB

`.portmmio`/`.portwram` were never really free. 0x19000-0x31000 is the renderer's
own `sbuf` and art cache, every byte spoken for. The video regions are now SHARED
with the renderer deliberately -- that is the point. What is left over is the port's
NON-video footprint, and it is one contiguous 25 KB: coroutine stack 8 KB
(0x0602A800), sparse I/O 1 KB (0x0602C800), work RAM 16 KB (0x0602CC00), all inside
CACHE_C's 32 KB. Declared in `sh_src/mars.ld` with ASSERTs, NOT solved. What has to
be counted against it is everything the port MAKES free -- the packet transport's
`.ramtext`, its `.bss` buffers, the MD-side staging -- and that count has not been
taken.

## 54. THE COROUTINE IS BUILT, AND IT REFUTES ITS OWN HYPOTHESIS (2026-10-03)

`PORTCORO=1`: `frame_rotated` YIELDS at each `wait_frame_flag` and RESUMES where it
stood, on its own 8 KB stack. `port_ctx_switch` / `port_ctx_make` in
`sh_src/mars_start.s`, 23 instructions, saving the SH ABI's callee-saved set and PR.

Free-run already ran by re-entry at 0x0AE2 -- there is no prologue there to erase.
What re-entry threw away was the SH-2 CALL STACK at every frame boundary, and a
per-frame error that only compounds is the signature of state lost at a boundary.
Measured over the same 176 frames:

    free-run  re-entry    mean 168.7   control 137.1   net +31.7
    free-run  coroutine   mean 171.4   control 137.1   net +34.4
    per-frame coroutine   mean  18.2   control  43.8   net -25.6  (unchanged)

**The lost call stack is NOT the divergence.** Suspect eliminated for one run. The
mechanism is kept, flag-gated and off: it costs 264 bytes of MEASURED stack depth
for a whole arcade frame (0xA5 paint, watermark published at [199]), it is the
faithful shape, and a vsync-driven build will need it -- a driver that is an
interrupt handler cannot longjmp out through its own frame.

## 55. READ THE CURVE'S SHAPE, NOT ITS MEAN (2026-10-03)

The free-run curve had been read as a mean (+31.7, "diverging") for a day. Its SHAPE
says something completely different:

    f1-f67   net -35 .. -8      the port tracks the arcade BETTER than the control
    f73      +4                 crossover
    f79-f145 ~195-240 wrong     PLATEAU, while the control FALLS from 144 to 122

A fixed plateau is a FIXED SET OF CELLS gone permanently wrong, not accumulating
noise. So the missing instrument was per-slot, and it is 24 words of output:

    [208..223]  per-slot FIRST-WRONG FRAME, 4 slots per word
    [224..239]  per-slot wrong bytes at the last frame
    [240..247]  the wrong byte-offset-WITHIN-a-slot mask, at frame 8 and at the end

    slot  first-wrong  wrong@f176          at frame 8 the ONLY wrong byte offsets
      16       38          71              in ANY object record are +0x00 and +0x2F
      17       59          71
      48        9          31
       0      109          34
      41        2           2   <-- ONE NEW SLOT PER FRAME, EXACTLY 2 BYTES EACH
      ...     ...         ...
      53       14          2

231 of the 292 wrong bytes are in the object region, and the per-slot sum matches
the scorer's own `reg[0]` exactly (231), so the instrument agrees with what it reads.

**And the arcade never activates those slots.** Straight out of the captures: at
frame 9000 AND frame 9080, slots 41-53 are status 0x00, handler 0x000000,
+0x2F = 0x03 -- the resting values every other inactive slot shares. The port was
SPAWNING OBJECTS THE ARCADE DOES NOT, one per frame.

## 56. THREE ADDRESS FAULTS IN THE GENERATED PORT: FREE-RUN +31.7 -> -18.3 (2026-10-03)

                                   free-run mean   net      per-frame mean   net
    the day's start                   168.7       +31.7        18.3        -25.6
    + misaligned 32-bit cells         160.6       +23.6        35.6         -8.2
    + cross-statement P68             136.1        -1.0        35.6         -8.2
    + CPORT_HI, 54 sites              118.8       -18.3        18.3        -25.5

**Free-run is NET CORRECT over 176 frames** -- better than the arcade's own
frame-to-frame delta, compounding its own error the whole way with no re-seed -- and
per-frame is back to its best-ever. The seeded frame is 8 wrong / net -44,
unchanged. `PORT-OUTPUT.md` 6 named the free-run gap as the only thing between the
port and a playable build; this closes it on the mean.

### 56a. 41 MISALIGNED 32-BIT GLOBALS, 231 REFERENCES

An SH7604 32-bit access raises a CPU ADDRESS ERROR unless the address is 4-byte
aligned. A 68000 `.l` needed only 2, and this program uses that constantly. The
generator already routed every COMPUTED `*(uint *)(EXPR)` through packed accessors
for exactly this reason -- but a BARE NAMED GLOBAL has no cast to rewrite, so gcc
trusted the declared type and emitted `mov.l`.

Found from the far end, not by reading: `_DAT_fffff132` is the camera X, 111
references, placed at 0x0602FD32 -- 2 mod 4 -- and objdump on the SHIPPED
`FUN_000039c8` showed `mov.l @r2,r3` / `mov.l r1,@r2` against it. 41 of the 119
32-bit symbols are 2 mod 4. Among them `_DAT_ffffc002`, `_DAT_ffffd402`,
`_DAT_ffffd482`, `_DAT_ffffc082`: an object record is 0x80 bytes and its HANDLER
POINTER is at +0x02, so every named access to an object's handler was one of these.

Three forms were measured on this toolchain BEFORE choosing one (sh-elf-gcc 15.2.0,
-m2 -mb -O2):

    extern uint X;                             -> mov.l   (faults)
    extern uint X __attribute__((aligned(1)))  -> mov.l   (gcc IGNORES it on a scalar)
    packed struct + asm("X") + macro           -> mov.b   (CORRECT)

The third is the mechanism `LD32U`/`ST32` already use, so it is one mechanism and not
two. The canonical `extern` line is left byte-for-byte intact because
`cport_layout.py` parses that header to PLACE the symbols.

### 56b. A 68K POINTER HELD IN A LOCAL ACROSS TWO STATEMENTS

`_nested_ptr` catches `LD32U(LD32S(X) + Y)` -- load and use in one expression. The
decompiler splits it as often as not, and then no single-expression pattern sees it:

    iVar1 = LD32S((&DAT_0001d32a + (mode << 2)));   a 68K ROM address, in a local
    bVar5 = uVar2 < *(ushort *)(iVar1 + sVar3);     dereferenced UNTRANSLATED

That site IS the cascade. `FUN_0000D80E` is the level's SPAWN-TABLE WALKER: it
compares the camera against the next spawn trigger and stops when the camera has not
reached it. `iVar1` is the round's trigger table at 68K 0x0001D4D6, and as an SH-2
address that is past the 32X's 16 KB boot ROM -- so the comparison was against
garbage, the walker did not stop, and `FUN_0000D868` (the slot allocator, whose only
two writes are `*A6 = 0x8000` and `A6[0x2f] = 0`) spawned thirteen objects.

The rewrite rule is the one `_ptr_from_mem` already states and which has not been
wrong yet: a 32-bit value loaded out of 68K memory and then used as an ADDRESS can
only be a 68K address, because 68K code could never have stored an SH-2 pointer
there. Only TOP-LEVEL ADDITIVE TERMS count -- `*(T *)(V + X)` is arithmetic on V,
`*(T *)(V * 4 + &DAT_x)` is V as an INDEX into an SH-2 object and must not be
touched. That distinction is checked, not assumed.

### 56c. CPORT_HI: A NARROWING INSIDE AN EXPRESSION IS A .w READ OF THE HIGH HALF

`_hi_half` only matched a bare `lhs = DAT_x;` with a 16-bit lhs, and skipped any
function reading the same cell with an explicit `>> 0x10` (the rng_next exclusion,
which is real: a long loaded into a REGISTER and then used `.w` IS the low half, and
the decompiled C cannot tell those apart). It therefore missed the common form:

    if (*(ushort *)(unaff_A6 + 0xc) <= (ushort)(_DAT_fffff132 + 0x20U)) ...

On the 68000 that is `cmp.w 0xFFF132,Dn` -- a word read AT the long's own address,
which big-endian makes the HIGH half. The C cast takes the LOW half. The camera is
0x10CC5000 at the seed: high half 0x10CC the position in PIXELS, low half 0x5000 the
fraction. That site clamps the player to the camera's left edge, so the port wrote
0x5000 + 0x18 = 0x5018 into slot 0 +0x0C where the arcade has 0x110C. `PORTWATCH`
named the writer in ONE run: `FUN_000049BA`, slot 0's own handler, 0x11 -> 0x50 at
0xFFC00C.

Safe because a `.w` read AT a long's base can only be its high half -- reaching the
low half means reading at base+2, which the decompiler spells as a different symbol.
The register case is the exception, so this runs over a NAMED CELL LIST (`CPORT_HI`,
default FFFFF132) arbitrated by the gate one address at a time, exactly as
`CPORT_CLAMP` is. The census says the scope is narrow and uniform: 54 sites, ALL on
this one cell, ALL `(ushort)(CELL + CONST)`, and NO other 4-byte cell in the corpus
has the shape.

It also repaired the per-frame regression 56a/56b caused: slot 0's 17 wrong bytes in
the seeded frame are gone, 28 -> 8.

## 57. CPORT_COUNT: THE PORT-SIDE COUNTSITES (2026-10-03)

`CPORT_COUNT=a,b,c` inserts `port_fn_count[i]++;` at the top of each named function
in the generated unit, published at `port_l2m_out[248..255]`. Max 8.

It exists because `PORTWATCH` names the function whose span CHANGED a cell, so a
function whose every write is IDEMPOTENT on the current state reads as zero spans --
indistinguishable from never having run. `FUN_000039C8` is exactly that case:

    _DAT_fffff138 = camera >> 16;   _DAT_fffff132 += _DAT_fffff158 * 0x100;
    _DAT_fffff120 = -((camera >> 0x10) - 0x1000);

With the camera frozen at the seed every one of those writes is a no-op, so watching
any of them is circular.

## 58. WHAT IS LEFT, NAMED RATHER THAN ESTIMATED (2026-10-03)

1. **THE CAMERA STILL DOES NOT ADVANCE.** 0xFFF134 is one of the 8 remaining
   seeded-frame cells: port 0x50, arcade 0x70, "we never wrote it; the arcade did".
   The arcade adds `_DAT_fffff158 * 0x100` = 0x2000 per frame and the port adds
   nothing. `PORTWATCH` on that cell reports ZERO spans, which is why 57 exists.
2. **THE 12 WRITES of that cell** set the whole long (`= 0x1000` -> 0x00001000) where
   the 68000's `move.w #0x1000,0xFFF132` sets the PIXEL half and leaves the fraction.
   Level one starts at pixel 0x1000 and the seed reads 0x10CC, so the word form is
   right -- but those resets do not run in this scene, so the gate cannot arbitrate
   them and they are deliberately left alone.
3. **Free-run's remaining object error is FOUR slots**: 0 (from f109), 16 (f38),
   17 (f59), 48 (f158). The step is at f109, 128 -> 189. Slots 16 and 17 are both
   `FUN_0000EE80`, whose path divergence is already recorded.
4. The 25 KB of section 53.

## 59. HANDLER POINTERS STORED INTO 68K MEMORY: FREE-RUN -91.9 (2026-10-03)

The session's full arc, one gate, 176 frames, each with its own captured inputs:

                                 free-run mean   net        per-frame mean   net     seeded
    start of the session            168.7       +31.7          18.3        -25.6     8 / -44
    + misaligned 32-bit cells       160.6       +23.6          35.6         -8.2
    + cross-statement P68           136.1        -1.0          35.6         -8.2
    + CPORT_HI reads                118.8       -18.3          18.3        -25.5
    + CPORT_HI full width           150.6       +13.5          14.9        -28.9     6 / -46
    + handler-pointer stores         45.2       -91.9          12.9        -30.9     6 / -46

**Free-run is net NEGATIVE at every sampled frame, f1 through f175** (-37 to -182):
the port never does worse than doing nothing, over 176 frames of compounding its own
error with no re-seed. Per-frame's last frame is THREE wrong bytes of 16,384 against
a control of 53.

### THE DEFECT

The decompiled program changes an object's behaviour by writing a code pointer into
the object record:

    *(code **)(unaff_A6 + 2)    = FUN_000059d2;    the object's HANDLER
    *(code **)(unaff_A6 + 0x24) = FUN_000075fa;    its animation SCRIPT

On the 68000 that value is a 68K address. In the generated unit `FUN_000059d2` is an
SH-2 symbol, so the port wrote an SH-2 address into 68K work RAM -- and every
consumer reads it back as a 68K address: `port_object_dispatch` binary-searches
`port_handlers[]` by the 68K address at slot+2, `port_call68_at` looks up the same
way, and `FUN_0000669C` walks the animation script as DATA from A6+0x24.

1,154 of the 1,159 store sites are `= FUN_<hex>`, where the symbol's own name IS the
address, so the rewrite is exact and not inferred. Two more shapes reach 68K memory
the same way and were closed with it: 62 `_DAT_ffffc002 = FUN_00002490;` (a slot's
handler through the NAMED cell -- every LHS checked to be a slot base + 2) and 8
`*(code **)(X) == FUN_0000ec98` comparisons, which were already wrong and which the
store fix would have made wrong the other way.

**Broken twice over**, which is why ST32 is the destination and not a cast. An object
record is 0x80 bytes, so slot+2 is 2 mod 4 and `*(code **)(A6 + 2) = ...` is a 32-bit
store at a misaligned address -- a CPU address error on an SH7604. `_unaligned32` does
not cover it: its cast list is uint/int/undefined4 and this one says `code *`.

### HOW IT WAS FOUND, five measured steps

1. Per-slot first-wrong said slot 16 broke at free-run frame 38.
2. The arcade's own capture shows that enemy's position FIRST CHANGING at frame 9038
   -- the port matched it byte-for-byte while idle and broke the moment it moved.
3. `PORTENDURE=38` + a full-record diff: slot 16 was the ONLY wrong object (49 bytes)
   while the frame as a whole was net -37, with +0x24..0x27 = 0x0238E968 against the
   arcade's 0x0000F5D0. 0x0238xxxx is `.portcode`.
4. `PORTWATCH` on 0xFFC80C named the innermost writer in one run: `FUN_0000669C`,
   the animation stepper, whose first act is `moveal %fp@(36),%a0`.
5. 68K objdump confirmed A6+0x24 is a 32-bit pointer the program walks as data.

### AND THE CLASS IS VERIFIED CLOSED, MECHANICALLY

A 32-bit sweep of the port's whole 16 KB work RAM at free-run frame 109 for values in
the SH-2 ranges (0x06000000-0x06040000 SDRAM, 0x02340000-0x02400000 cart, and their
uncached aliases) finds **three** -- and all three hold EXACTLY what the arcade holds
at the same offsets (0x06020000 at slot 6 +0x08, 0x26020000 at slot 38 +0x08,
0x0601FFFF at 0xFFF400). They are coincidental game data, not leaks. No SH-2 address
reaches 68K memory any more.

## 60. WHAT IS LEFT, AND IT IS ONE OBJECT (2026-10-03)

At free-run frame 109 the port is net -78 and **slot 0, the player, is the only wrong
object** -- slots 16, 17 and 40 are byte-perfect. Its +0x00 is 0x00 where the arcade
has 0x88: the port kills the player.

`PORTWATCH` on 0xFFC000 names `FUN_000053A8` (called from `FUN_000049BA`, the player's
handler) writing 0x88 -> 0x00. The 68K's condition there is

    53d8: cmpib #3,%fp@(33)      A6[0x21] == 3 ?
    53de: bne 0x53ea                no -> rts
    53e0: cmpib #1,%fp@(34)      A6[0x22] == 1 ?
    53e6: beq 0x49f6                yes -> back into FUN_000049BA's death path

and at frame 109 the port's A6[0x21] is 0x03 where the arcade's is 0x00. A6[0x21] is
the ANIMATION FRAME INDEX, walked by `FUN_0000669C` through the script pointer at
A6+0x24 -- which the port now reads correctly (`P68(LD32U(unaff_A6 + 0x24))`, checked)
but which holds 0x74CA against the arcade's 0x76B0. Both are valid 68K script
addresses, so the port is on a DIFFERENT animation state, not a broken pointer. The
remaining divergence is one state machine, one object.

Also still open and unchanged: the 25 KB of section 53, and the 12 remaining width
suspects `tools/cport_widths.py` ranks (0xFFF154 was #5 and turned out not to be a
width defect at all -- its spellings are correct and it was being DECREMENTED too
often, which is how the despawn chain was found).

## 61. THE LAST OBJECT: IDENTICAL STATE IN, DIFFERENT BRANCH OUT (2026-10-03)

The player (slot 0) is the only thing left wrong in free-run, and the divergence is
now pinned to a single frame with both machines in the SAME state.

    frame 108   slot 0 is BYTE-PERFECT (0 of 0x80 wrong), +0x21=03 +0x22=01
                the whole frame is 15 wrong of 16,384 against a control of 123,
                net -108
    frame 109   arcade: +0x21 03 -> 00, +0x24 0x74CA -> 0x76B0, X 0x110C -> 0x1169
                        -- the player starts a new action
                port:   +0x21 stays 03, +0x24 stays 0x74CA, and +0x00 goes 0x88 -> 0
                        -- the player DIES

So this is NOT accumulated drift and NOT a lagged counter: the two machines are
identical going into the frame and leave it differently.

### WHY, AS FAR AS THE BYTES GO

PORTWATCH on 0xFFC000 names `FUN_000053A8`, called from `FUN_000049BA`. Its tail is

    53d8: cmpib #3,%fp@(33)      A6[0x21] == 3 ?
    53de: bne 0x53ea                no -> rts
    53e0: cmpib #1,%fp@(34)      A6[0x22] == 1 ?
    53e6: beq 0x49f6                yes -> 0x49F6

and 0x49F6 really is the despawn, so the port's rendering of it is FAITHFUL:

    49f6: moveaw %a5@(28),%a4    49fa: bset #5,%a4@
    49fe: clrw %fp@(0)           <- A6[0] = 0, the despawn
    4a02: jsr 0x3bce (release_palette_slot)   4a08: jmp 0x3f04 (hide_object_sprite)

CPORT_COUNT over the 109 frames: `FUN_000053A8` is entered **109 times, once per
frame, every frame**, and `FUN_00005430` -- the entry that slot 0's own dispatch index
selects (+0x6A = 0, table 0x06D90[0] = 0x00005430, the same on both machines) -- is
entered **ZERO** times. So the port reaches 53A8 through a deeper level of the
player's three-level dispatch, and reaches it on a frame where the arcade does not.

`A6[0x21] == 3 && A6[0x22] == 1` is TRUE at frame 109's start for BOTH machines, and
the arcade does not despawn. The only reading left is ORDER: the arcade's animation
stepper (`FUN_0000669C`, which decrements +0x22 and on reaching 0 advances +0x21 and
loads the next script) runs BEFORE the state function's despawn test, so by the time
53A8 tests, +0x21 is already 0. **The port tests first.**

That is the class already on record for this handler -- "the arcade's exact path
through FUN_0000EE80 is known and the port takes a different path" -- and the
instrument for it already exists and has not been run: `tools/l1_oracle.lua` captures
the arcade's own progress markers (0xFFFEF4/F6, written before each call it makes), so
the arcade's call ORDER through frame 9109 is recoverable, and `PORTTRACE=1` gives the
port's for the same frame. Comparing the two settles it.

NEXT STEP, concretely: capture the arcade's marker trace for frames 9105-9110, build
`PORTENDURE=109 PORTTRACE=1` with `port_trace_want` armed on slot 0's handler, and
diff the two call sequences.

## 62. THE PLAYER DEFECT, CLOSED TO ONE CAUSE: A5 NEVER REACHES THE REGISTER FILE (2026-10-03)

### A ONE-FRAME REPRODUCER

`PORTSEED=108` scores **67 wrong of 16,384, control 72, net -5**, and ALL 44
object-region errors are in slot 0. The defect that needed a 109-frame free-run to
observe now reproduces in a single seeded frame of ~3 ms. Anyone picking this up
should start there.

### WHAT THE ARCADE DOES, FROM ITS OWN INSTRUCTION TRACE

`tools/l1_order.lua` (new) traces one named frame with MAME's own `trace` action --
not a lua tap, which under-reports on S16B (LESSONS, "MAME lua taps lie on S16B").
`L1O_REGS=1` adds the register file beside every instruction. Frame 9109:

    0053A8: move.l #$74ca, ($24,A6)       ; a5=FFFFE000 a6=FFFFC000
    0053D8: cmpi.b #$3, ($21,A6)          ; == 3  TRUE
    0053E0: cmpi.b #$1, ($22,A6)          ; == 1  TRUE
    0053E6: beq    $49f6                  ; TAKEN -- the arcade despawns too
    0049F6: movea.w ($1c,A5), A4          ; A4 = the word at A5+0x1C = 0xF028
    0049FA: bset   #$5, (A4)              ; set bit 5 of 0xFFF028: "the player died"
    0049FE: clr.w  ($0,A6)                ; despawn
    004A02: jsr    $3bce  (release_palette_slot)
    004A08: jmp    $3f04  (hide_object_sprite)
    ...later in the same frame...
    000DFA: bclr   #$5, $f028.w           ; clear the flag AND TEST ITS OLD VALUE
    000E00: beq    $e64                   ;   already clear -> skip everything
    000E02: subq.b #1, $e018.w            ; lives 4 -> 3
    000E2A: jsr    $47cc                  ; RESPAWN: template from ROM 0x73EC,
                                          ; X = camera_hi + 0x90, Y = 0x10D8

**So the port's branch was never wrong.** The arcade despawns the player at frame
9109 exactly as the port does, and then RESPAWNS it in the same frame. The port does
not, so the slot stays dead and everything downstream follows.

### THE PORT'S GATE IS FAITHFUL; THE FLAG IS NEVER SET

`FUN_00000DFA` in the generated unit is a correct rendering of the gate, including the
`bclr`-sets-Z idiom:

    bVar1 = DAT_fffff028 & 0x20;
    DAT_fffff028 = DAT_fffff028 & 0xdf;
    if (bVar1 != 0) { DAT_ffffe018 += -1; FUN_00003858(...);
                      if (DAT_ffffe018 == 0) {...game over...} else {... FUN_000047cc(); } }

CPORT_COUNT over the seeded frame settles which side of it fails:

    FUN_00000dfa (the gate)                      1   <- it RUNS
    FUN_00003858 (ONLY inside the taken branch)  0   <- the branch is NOT taken
    FUN_000047cc (respawn)                       0
    FUN_0001a800 (game over)                     0

so `DAT_fffff028 & 0x20` was 0: **bit 5 was never set by the despawn.** The cell reads
0x01 at the frame boundary on BOTH machines (the arcade sets and clears it inside the
frame), which is why no watchpoint could answer this and the counter had to.

### THE CAUSE

`0049FA: bset #$5,(A4)` needs `A4 = the word at A5+0x1C`, and A5 = 0xFFFFE000, the
player-1 RECORD. Confirmed independently from the captures: 0xFFE01C holds 0xF028
(player 1's state byte) and 0xFFE018 is player 1's lives (0x04 -> 0x03 across the
frame); player 2's record at 0xFFE080 is zeroed and its lives are 0, consistent with
one player.

A5 is established **inside `FUN_000049BA` itself**:

    0049CC: movea.w ($3c,A6), A5     ; A6+0x3C holds 0xE000, sign-extended to 0xFFFFE000

and the port DOES compute it -- `iVar12 = (int)W68(*(short *)(unaff_A6 + 0x3c));` --
**into a LOCAL**. The call to `FUN_000053A8` is an INDIRECT one, so it goes through
`port_call68_at` and the generated thunk

    static uint thk_FUN_000053a8(const uint *r) { return FUN_000053a8((int)(r[13])); }

which reads `_port_reg[13]` -- A5 -- and **nothing ever writes it**. The dispatcher
establishes A6 and only A6 ("A6 is always the slot base -- the only one derivable
without the oracle"). So A5 arrives as whatever a previous handler left, the
`*(short *)(p_A5 + 0x1c)` read is garbage, and the bset lands somewhere harmless.

### WHY THE CHEAP FIX IS UNSOUND, CHECKED RATHER THAN ASSUMED

The tempting rule is "a local assigned `W68(*(short *)(A6 + 0x3c))` is A5". It is
false. Counted over the whole program's instruction encodings:

    25  moveaw %fp@(60),%a0
     3  moveaw %fp@(60),%a1
     4  moveaw %fp@(60),%a5      (+ 4 more as `movew %fp@(60),%a5`)

A6+0x3C is loaded into THREE different address registers. A textual pass cannot tell
which, and guessing would corrupt 28 sites to fix 8.

### THE FIX, AND IT NEEDS THE GHIDRA SIDE

Ghidra knows which register each local lives in -- that is variable STORAGE, and this
tree already reads it (`tools/ghidra/reg_convention.py`,
`tools/ghidra/set_param_storage.py`, and the A6 call-site table in
`docs/audit/a6_callsites.json`, which solved the same shape for A6). The fix is the
A5 (and general A-register) analogue of that table: at every indirect call site,
publish the registers the function has computed into `_port_reg` before the call.

`PORTREGS=1` is the existing diagnostic for "are the missing registers what stops
this", but it supplies the arcade's file at the DISPATCHER's call site only, so it
does not cover an indirect call made deeper inside a handler. Extending it to inner
call sites would prove the fix's value before the Ghidra work is done.

## 63. THE PLAYER IS RESPAWNED, AND CPORT_A6 HAD NEVER RUN (2026-10-03)

                              free-run mean   net        per-frame   net      seeded
    before                       45.2       -91.9          12.9     -30.9     6 / -46
    after                        42.6       -94.5          12.8     -31.1     6 / -46

The free-run's FIRST WRONG CELL moves from 0xFFC000 -- the player's status byte -- to
0xFFC00D. Slot 0's wrong bytes at f176 go 34 -> 14, and on the one-frame reproducer
(PORTSEED=108) 67 -> 47 wrong, net -5 -> -25, with status 0x88, handler 0x49BA, Y
0x10D8 and lives 0x03 all MATCHING the arcade. The player is no longer killed.

### THREE BUGS, AND THE THIRD IS THE ONE THAT MATTERED

**1. Two register files.** `port_cur_regs` is what the dispatch thunks read;
`_port_reg` is what ported code writes BACK into on return. A pass that published A5
into `_port_reg` before an indirect call produced the CORRECT value -- 0x0602EC00 =
68K 0xFFE000, verified against the arcade's own register trace -- in a place the
callee never looks, and moved the score by one byte. It now goes through a
`port_set_reg()` the driver exports, which writes the live file. That is exactly what
the existing contract already says ("an indirect call has no slot, so it inherits
whatever the dispatch set"); inheriting what the CALLER computed is strictly more
faithful than inheriting the last handler's leftovers.

**2. `extraout_X` is not X's live value.** Ghidra's name means "X as it is AFTER a
call returns" -- a result, not an input -- so publishing it BEFORE a call is
backwards. Including it took the reproducer 68 -> 147 wrong.

**3. CPORT_A6 WAS DEFINED, DOCUMENTED, HASHED INTO PORTSTAMP -- AND NEVER PASSED TO
cport_prep.py.** It was missing from `CPORT_ENV`, so the pass ran with an empty caller
set and restored NOTHING from the day it was written. The table had the exact triple
all along: `FUN_000047cc -> FUN_00004870, A6 = 0xFFC000`, from `lea 0xffffc000,%fp` at
0x47CC. Without it the player's respawn template copy ran with A6 = whatever slot was
dispatched LAST, so it wrote the player's new state into SLOT 40 -- slot 40's record
came back holding 0x49BA, the player's own handler. 30 restore blocks now emit.

Same lesson this Makefile already carries about `.build_flags`: a variable that is
defined, documented and hashed still does nothing unless it reaches the process that
reads it, and **a pass that silently applies to nothing looks exactly like a pass that
applies correctly.**

### HOW THE A5 IDENTITY WAS OBTAINED

Guessing was ruled out first: A6+0x3C is loaded into A0 25 times, A1 3 times and A5 8
times across the program, so no textual rule can say which.
`tools/ghidra/areg_locals.py` (new, READ-ONLY, the project copied first) asks the
decompiler for variable STORAGE -- 2,379 address-register symbols across 685 functions
-- and names FUN_000049BA's `iVar12` as A5. `CPORT_AREG` takes the register indices to
publish, one at a time, arbitrated by the gate like CPORT_CLAMP and CPORT_HI;
publishing every unambiguous register at once measured WORSE.

### THE HORIZON DID NOT MOVE, AND THAT IS THE HONEST RESULT

250-frame endurance: 251 wrong against a control of 240, net **+11** (it was +8). The
176-frame curve improved and the far endpoint did not. The game's own frame counter
matches exactly (1473 both), so the port runs the logic and drifts. The dominant
residual at 250 frames is **slot 16 (61 bytes, first wrong at f146)**, whose handler is
FUN_0000EE80 -- the same function whose path divergence has been on record since
2026-09-30. Slot 0 is 41, slot 48 is 31, slot 54 is 21.

So: the port is strongly correct for ~180 frames and the ceiling is unchanged at ~250.
