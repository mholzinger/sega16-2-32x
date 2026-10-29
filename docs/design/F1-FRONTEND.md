# F1 step 1 — the translator's front end, and why it cannot be the existing disassembly

**2026-09-26 14:30 ET. Findings, not code. The build this selects is a
recursive-descent 68K decoder.**

F1 is "the translator, with the L1 differential harness built alongside it".
Before writing an emitter I censused what it must cover and what it can trust.
The second question turned out to be the live one.

## What the emitter must cover

Mnemonic census over the reached set (`vint_census` walk from the two entry
points plus the pointer-table roots):

    162 distinct mnemonic forms
     58 forms cover 95% of reached instructions
     98 forms cover 99%
    104 distinct BASE operations with the size suffix stripped

`move` in all sizes is 6,826 = 27%. The head is unremarkable and small:
jsr 1141, lea 791, movel 788, beqs 760, btst 719, moveq 659, cmpib 645,
orl 633, bnes 590, rts 514, clrb 511. **58 forms is a tractable emitter.**

## The flag strategy is already established, and it is why 1.27x is achievable

`docs/design/collision_sh2.s` (the hand translation) does NOT materialise a
68K CCR. It fuses each flag-setter with its consuming branch:

    68K   tst.b d(a4) ; beq L      ->   SH-2   mov.b @(r0,r4),r1
                                               tst  r1, r1
                                               bt   L

The 68K sets N/Z/V/C on most operations; the SH-2 has one T bit. Materialising
a CCR per instruction is what makes naive 68K-on-RISC translation slow.
**Fusion is the design, it is proven on a real routine, and the translator
should treat an UNFUSED flag consumer as the exceptional path**, not the rule.
Its register discipline is also already set: fixed aN/dN -> rN assignment, with
loop-invariant table offsets established once at scene setup (r7, r9-r12) rather
than reloaded per call.

## THE FRONT END IS THE PROBLEM

`roms/altbeast/prog68k.asm` is a LINEAR disassembly, and it is not reliable
over the region where a third of the reached code lives:

                              reached instrs    .short (undecoded)
    below 0x20000                   17,405           109   =  0.6%
    at or above 0x20000              7,879         2,850   = 36%

**36% against 0.6%.** I first read the high region as genuinely code, on the
grounds that 559 direct call/branch targets land above 0x20000. **THAT WAS WRONG
— see the correction at the end of this document. Those edges are phantom.**

The 2,959 undecoded words form 989 contiguous runs, the longest 127 words, and
544 of the runs are a single isolated word. That is the signature of a LINEAR
disassembler losing alignment: it walks into an embedded data blob (a jump
table, a sprite record), resyncs at the wrong parity, and emits garbage until it
re-aligns by luck. Only 15 of the 2,959 coincide with the 15 jump-table bases
F0(d) identified, so embedded tables are not the bulk of it — misalignment is.

## What this selects

**The translator's front end must be a RECURSIVE-DESCENT decoder**: start at the
two entry points and the resolved dispatch targets, decode one instruction,
follow its edges, and never decode a byte that control flow does not reach. A
linear disassembly cannot be repaired into this; code/data separation is the
whole difficulty and only reachability answers it.

This is the right architecture regardless, because an AOT needs exactly the same
thing the decoder produces: the set of reachable instructions with their edges.
It also subsumes the heuristics the census currently leans on —
`pointer_targets()` guesses roots by scanning for longwords that point at
entry-like addresses, and a recursive decoder driven by F0(d)'s resolved jump
tables replaces that guess with a derivation.

**Consequence for the numbers already in the record, stated plainly:** the
"25,284 reached instructions" figure includes ~2,959 undecoded words, so the
true reached-code count is nearer 22,300, and the per-instruction ratios that
divide by it are correspondingly ~12% optimistic. That does not threaten the
budget (the port needs 3.1% of a vint cache-resident, 23.7% all-miss), but
`PORT-CENSUS.md`'s instruction count should be re-derived once the decoder
exists rather than carried forward.

## Build order for F1

    F1.1  recursive-descent 68K decoder. Roots: reset 0x40E, IRQ4 0x2AAC, and
          the jump tables F0(d) resolved (15 pc-relative bases, word offsets,
          byte index). Output: reachable instructions + edges + an explicit
          UNRESOLVED list. Gate: the UNRESOLVED list is empty or enumerated.
    F1.2  the L1 differential harness, BEFORE the emitter. Snapshot 68K state
          around a routine under the oracle; run the candidate SH-2 routine on
          the same input state; compare. No emitter output is trusted without it.
    F1.3  the emitter, 58 forms first, with the fusion pass. Gate each form on
          F1.2.
    F1.4  regenerate collision_detection and diff against collision_sh2.s --
          the one routine with a known-good hand translation. First real
          confidence check on the whole chain.


---

# CORRECTION — 0x20000+ IS DATA, and the edges into it were phantom (2026-09-26 15:00 ET)

Above I concluded the high region was genuinely code because 573 direct
call/branch targets in the linear listing land there. Ghidra's independent
recursive descent falsifies it:

    linear-listing edges landing above 0x20000                      573
      originating at an instruction GHIDRA ALSO DECODED               0
      originating at bytes Ghidra does not consider code            573

**Zero of 573.** Every one of those edges comes out of a byte sequence that a
recursive-descent disassembler never reaches. They are exactly what a linear
listing produces in a data region: garbage decodes whose displacement fields
read as branches to arbitrary addresses. The argument I built on them was
circular — I used the broken listing's own output as evidence that the broken
region was code.

Corroboration: **Ghidra's highest decoded instruction is 0x1AF74**, just past
the colour-cycler script at 0x1A6FA (which is data embedded at the end of the
code). So the program's code occupies roughly 0x400-0x1B000 — about 107 KB of
the 256 KB image — and the remaining ~148 KB is art, tables and level data. That
is an entirely ordinary S16B layout and it should have been the first hypothesis.

**This makes the port SMALLER, not larger.** The instruction count to translate
is bounded by the low region, so PORT-CENSUS's 25,284 is inflated by
data-region garbage in a way the earlier ~12% estimate understated. Fewer
instructions is less fidelity surface, and fidelity is the proposal's only
remaining risk (§3's concession). The count must be re-derived from the decoder
rather than adjusted by a factor.

**What is still open in the low region.** Ghidra now decodes 12,456
instructions over 536 functions after one seeding round (from 8,822 / 234
unseeded), but that is still only 37.4% of the low region's bytes as code. The
seeding fixpoint is NOT finished, and finishing it is the rest of F1.1.

## F1.1 revised, in light of the Ghidra rig

Do NOT write a new decoder. The rig already has the right engine and a seeding
loop built for this exact problem (`tools/ghidra/seed_harvest.py` ->
`seed_apply.py`, a documented fixpoint; LOOP-DECOMPILE 19 records that seeding a
function at every address fragments the call graph 433 -> 2121, so only genuine
entries become functions). What it needs:

    1. HARVEST FROM GHIDRA'S LISTING, NOT FROM OBJDUMP. seed_harvest currently
       reads the linear .dis to find seeds for Ghidra — using the unreliable
       listing to repair itself. Point it at Ghidra's own instruction list
       (instr_list.py) and iterate to a fixpoint.
    2. TEACH IT SHAPE C. It knows two dispatch shapes: a pc-relative table of
       LONGS, and a routine pointer stored into an object field. F0(d)'s
       dominant shape is neither — it is a WORD-OFFSET table:
           lea %pc@(TABLE),%a0 ; byte index ; addw ; addaw %d0,%a0
           addaw %a0@,%a0      ; target = TABLE + (int16)word[TABLE + i*2]
           jmp %a0@
       11 sites, 15 bases (listed in the F0(d) result above). A first pass
       recovered only 19 targets, but it was gated on the linear listing's
       instruction starts — the same circularity as above. Re-run it gated on
       GHIDRA's starts, or ungated and let seed_apply's repair pass reject.
    3. P-CODE IS AVAILABLE AND IS THE EMITTER'S INPUT. Measured 5.6 P-code ops
       per instruction over 12,456 instructions. P-code makes the 68K's flag
       writes explicit, so DEAD-FLAG ELIMINATION over P-code is the fusion pass
       that collision_sh2.s performs by hand — generalised rather than
       pattern-matched per mnemonic pair. This is a better emitter input than
       the 58 mnemonic forms, and it is why F1.3's shape should be decided after
       F1.1 rather than before.


---

# F1.1 STATE, 2026-09-26 15:45 ET — real progress, NOT finished

## What was gained

    Ghidra, unseeded (raw binary import)        8,822 instructions   234 functions
    + seed_harvest's 303 seeds + the vectors   12,456               536
    + jump tables expanded correctly           13,037

New scripts, all Ghidra-native (they match on instruction objects, never on
disassembly text): `f1_export.py` (coverage by region, P-code, computed jumps),
`f1_seeds.py` (three dispatch shapes, iterated to a fixpoint), `f1_tables.py`
(F0(d)'s 15 tables), `f1_gap.py` (classifies the undefined space).

## THE JUMP-TABLE STRUCTURE, derived — this was the real find

Each table sits at the END of its code block, and its entries are int16 offsets
RELATIVE TO THE TABLE BASE, hence NEGATIVE, pointing back into the block that
precedes it. **0x8000 is a sentinel and terminates the table.** Example:

    table 0x11E34, preceded by the 2,248-byte gap 0x113B0-0x11C78:
      [0] FE44 -444 -> 0x11C78     [4] FD76  -650 -> 0x11BAA
      [1] FDB8 -584 -> 0x11BEC     [5] FD74  -652 -> 0x11BA8
      [2] FD5A -678 -> 0x11B8E     [6] FBF4 -1036 -> 0x11A28
      [3] FE3E -450 -> 0x11C72     [7] 8000  SENTINEL

Entry counts across the 15 bases: 2, 4, 0, 0, 14, 7, 7, 5, 10, 5, 7, 5, 11, 6,
8 = **90 distinct targets, against 19 from the first attempt.** The first
attempt found one entry per table because it gated each target on being an
already-decoded instruction start — using the linear listing, which does not
decode these blocks. **The targets are undecoded precisely BECAUSE they are only
reachable here**, so gating on prior decoding can only ever return the first
one. That circularity has now bitten this work twice in one day (see also the
0x20000+ correction above); it is the shape to watch for.

Two bases, 0xD0EC and 0xD5A2, yield zero entries. They came from `find_base`
running over the linear listing, so they are the likeliest to be wrong.

## WHAT IS STILL OPEN, and it is the bulk

    UNDEFINED in 0x400-0x20000    76,870 B in 384 gaps
      in gaps >= 256 B            55,315 B  = 72%
      in gaps <  32 B              1,669 B

The big gaps persist and barely moved when the tables were expanded (the largest
in-code ones went 4,600 -> 4,078 B and 3,426 -> 3,418 B). So the table targets
land at the gap EDGES but the bodies do not decode onward, which means either
the targets are slightly off or those blocks are not linear code from that
point.

**The open question for F1.1 is what those ~55 KB are.** Two hypotheses, and
they need distinguishing before more seeding effort:

  1. DATA INTERLEAVED IN THE CODE REGION. S16B titles embed animation scripts,
     sprite tables and level data among routines. If so, F1.1 is closer to done
     than the byte count suggests and the instruction total is near 13,000.
  2. CODE REACHED BY A MECHANISM NOT YET MODELLED. Shape B (a routine pointer
     stored into an object field) is only partly covered — seed_harvest found
     275 immediate targets from 293 sites by regex over the linear listing, and
     that should be re-derived natively.

**Do not guess between them.** The test is structural: sample the big gaps and
ask whether their bytes look like 68K instruction streams or like tables. That
is the next measurement, and it is cheap.

## Also confirmed, and it shapes F1.3

P-code is available at 5.6 ops per instruction. Dead-flag elimination over
P-code is the generalised form of the `tst`/`bt` fusion `collision_sh2.s`
performs by hand. Decide F1.3's emitter input after F1.1 closes, not before.


---

# F1.1: THE ORACLE'S EXECUTION IS THE ONLY NON-CIRCULAR SEED SOURCE (2026-09-26 16:15 ET)

Mike pointed at the prior Kyocera firmware decompile (`~/src/kyocera-2235`) as
reference. Two things there transfer directly, and one of them broke the
circularity this work kept hitting.

## The method

`tools/levelgen_harness/emu_trace.py` in that project traces the real ROM under
Unicorn to get ground truth rather than inferring it. Unicorn is not installed
here, but **this repo already has the same instrument and it is the sanctioned
oracle**: `tools/arcade_trace.lua` runs `mame altbeast` — always valid per
CLAUDE.md — with `AT_PLAY=1` driving `tools/auto_beast.lua` through a real
level-1 playthrough, and MAME's own instruction trace is exact.

    AT_START=7000 AT_LEN=40 AT_OUT=/tmp/arc_cov.log AT_PLAY=1 \
      mame altbeast -rompath ./mame -skip_gameinfo -video none -sound none \
        -nothrottle -window -resolution 160x120 -keyboardprovider none \
        -nomouse -nojoystick -bench 200 -debug -debugger none \
        -autoboot_script tools/arcade_trace.lua

**Executed code needs no heuristic and resolves every dispatch by
construction.** It is the one seed source that cannot be circular — unlike
gating targets on prior decoding, which failed twice today.

## What it settled

    distinct executed PCs (40 frames of gameplay)           1,677
    range                                            0x00404-0x1AF74
    executed PCs at or above 0x20000                           0

**Zero executed PCs above 0x20000** — an independent third confirmation that the
region is data, after Ghidra's recursive descent and the phantom-edge analysis.
And the executed range ends at 0x1AF74, exactly Ghidra's highest instruction.

    executed PCs Ghidra already decoded                     1,474
    executed PCs Ghidra did NOT decode                        203   <- real code
    seeded and decoded                                        200

Instructions 13,037 -> 13,526; low-region code 39.3% -> 40.9%. Three PCs would
not decode (0x107FC, 0x0FDCE, 0x0FDD2) — probably operand words logged inside a
collapsed loop, or a conflicting instruction already present.

## THE ASYMMETRY, which bounds what this method can ever do

**An executed PC PROVES code. The ABSENCE of executed PCs in a gap proves
nothing** — one 40-frame playthrough does not reach every routine. So the oracle
trace closes the code set from BELOW and never licenses calling a gap data. The
low region still has 75,726 undefined bytes, 57.8%, and they remain unclassified.

The way to push the bound up is more and broader trace: longer runs, attract as
well as gameplay, later rounds, death and continue paths. That is cheap to run
and it is the next thing to do. What it cannot do is prove a negative, so F1.1
will close with an explicit list of never-executed gaps that the translator must
either translate blind or trap on — and the trap is already the design (F0(d)).

## The other transfer: how to track F2

`~/src/kyocera-2235/analysis/coverage.md` is a per-function coverage table with
a three-state status — PORTED / ANALYZED / UNTOUCHED — plus a per-function
"guessed purpose" and the evidence for it, and a summary that reports the count
and percentage in each state. That project drove 12 -> 83 PORTED of 93 functions
through an overnight loop against it.

**That is exactly the instrument F2 needs** (§7: "translate and verify routine by
routine at L1. Exhaustive."), and it is a proven format rather than one invented
here. F2 should open by generating the same table from Ghidra's 536 functions,
with a fourth column for the L1 differential's verdict.


---

# F1.1 does NOT close here — SUPERSEDED, see "FULL COVERAGE IS THE BAR" below
# (kept for the trace-saturation measurement, which stands)
#
# execution tracing SATURATES (2026-09-26 16:45 ET)

Four arcade traces across boot, attract, title and level-1 gameplay
(`tools/f1_coverage.py`, new, merges them and reports the asymmetry rather than
hiding it):

    trace      distinct PCs   new      cumulative
    attract          560      +560          560
    boot             410      +105          665
    play (L1)      1,677    +1,328        1,993
    title            444       +62        2,055

    union 2,055 distinct PCs, range 0x00404-0x1AF74
    executed at or above 0x20000                    0   (fourth confirmation)
    executed PCs Ghidra now MISSES                  5   (seeded)

**It has saturated.** Boot added 105 PCs, title added 62. Ghidra decodes 13,526
instructions and only 2,055 of them were ever executed — **11,476 decoded
instructions, 85%, are never reached by any of these playthroughs.** That is not
a defect in the traces; a 60-frame window of level 1 cannot exercise five
rounds, bosses, deaths and continues.

**And decisively: every one of the ten largest gaps contains ZERO executed
PCs.**

    0x1AF76-0x20000  20,618 B   0 executed   (above the code end: data)
    0x14844-0x15832   4,078 B   0
    0x15B1E-0x16878   3,418 B   0
    0x0E2F0-0x0EF2C   3,132 B   0
    0x11CE8-0x12804   2,844 B   0
    0x10964-0x1132A   2,502 B   0
    0x01774-0x01E4E   1,754 B   0
    0x113B0-0x11A28   1,656 B   0
    0x1390C-0x13F18   1,548 B   0
    0x099C6-0x09EEC   1,318 B   0

So tracing cannot classify them, and more tracing will not: the asymmetry says
absence is not evidence, and the method has visibly plateaued. Chasing full
execution coverage of a five-round game through traces is open-ended work with
no termination condition.

## Therefore F1.1's deliverable is the list, not a classification

    Ghidra-decoded instructions                      13,526
      of which executed by some trace                 2,055   PROVEN code
      of which never executed                        11,476   translate or trap
    undefined bytes 0x400-0x20000                    76,462 in 285 gaps
      in gaps >= 256 B                               59,438 in 39 gaps
      of those gaps containing an executed PC              0

This is the honest close, and F0(d) already designed for it: **the translator
traps on an unmapped dispatch and on entry to any address it did not translate.**
Under §5's L2 differential over the 20,000-frame gameplay route, anything the
static analysis missed halts at a named frame instead of running wrong. That
converts the 59,438 unclassified bytes from a blocking question into a
test-coverage question — which is what the proposal's verification ladder is.

**What would actually shrink the list**, if it ever needs shrinking: not more
traces, but the arcade's own attract-mode round progression driven long enough to
reach rounds 2-5, plus a death-and-continue path. `tools/auto_beast.lua` drives
to frame 13,500 and three late windows (7100, 9000, 12000) produced no trace at
all in this run — worth fixing if and only if the trap-based approach proves
insufficient in F2. It should not gate F1.1.


---

# FULL COVERAGE IS THE BAR, and the gap classification puts it within reach (2026-09-26 17:15 ET)

**Two corrections to the section above, both Mike's.**

**1. The "85% of code is never reached" figure was not a finding and I should not
have presented it as one.** It conflated two populations that need separating:

  * **11,478 instructions Ghidra DECODED but no trace executed.** These are
    fine. We know they are code, we know their extents, they translate normally,
    and **none of them needs a trap.** They are the rest of the game — rounds
    2-5, the other enemies' AI, boss patterns, death and continue. A 60-frame
    level-1 trace not touching them is the expected result. Mike: "These are just
    instructions for enemy AI and level layout."
  * **59,438 bytes in 39 gaps Ghidra has NOT decoded.** This is the only open
    question, and it is far smaller than the earlier framing implied.

**2. "Translate blind or trap" is too low a bar.** Mike, on the prior Kyocera
work: we did not have Cavern Crawl until the entire game was decoded — and
`~/src/kyocera-2235/analysis/coverage.md` shows it reached **0 UNTOUCHED of 93
functions**. Standing rule 1 is fidelity over everything. The trap stays as a
SAFETY NET (F0(d)), not as the plan.

## The gap classification — what references each gap, not whether a trace ran it

Mike's framing selected the test: AI is code reached by dispatch, level layout is
data reached by a pointer load, so the REFERENCES into a gap classify it and no
further tracing is needed (`tools/ghidra/f1_gapref.py`, new).

    39 gaps >= 256 B, 59,438 bytes total

    DATA        35,156 B   data references only, from decoded code
    CODE             0 B   *** NOT ONE GAP HAS A FLOW REFERENCE ***
    UNREFERENCED 24,282 B  no reference of any kind from decoded code

**Zero flow references into any gap.** If a gap were an unreached routine
reachable by a call, Ghidra would show one. So 35,156 bytes — 59% of the gap
space — is provably not code and never needed decoding. The largest,
0x1AF76-0x20000 at 20,618 B with 17 data refs, is the level/art data past the
code end; 0x01774-0x01E4E has 40 data refs; 0x07034-0x07414 has 63.

## The 24,282 unreferenced bytes split again, and the split is informative

8 of the 19 unreferenced gaps END within 1 KB below a jump-table base:

    0x15B1E-0x16878  3,418 B   table 0x16938 is +192 B past its end
    0x11CE8-0x12804  2,844 B   table 0x128DA is +214 B
    0x1390C-0x13F18  1,548 B   table 0x141B8 is +672 B
    0x128B6-0x12D72  1,212 B   table 0x12FF2 is +640 B
    0x14152-0x144DA    904 B   table 0x148A2 is +968 B
    0x14528-0x146F0    456 B   table 0x148A2 is +434 B
    0x11A72-0x11B8E    284 B   table 0x11E34 is +678 B
    0x0D334-0x0D448    276 B   table 0x0D5A2 is +346 B
                     -------
                     10,942 B

That is the shape derived earlier: **each table sits at the end of its code block
and dispatches backwards into it.** These gaps are those blocks. They are CODE,
and the reason they are undecoded is that the table expansion is incomplete — 90
targets were extracted across 15 bases (2, 4, 0, 0, 14, 7, 7, 5, 10, 5, 7, 5,
11, 6, 8) and two bases yielded nothing at all.

## So the finish line is ~13 KB, not 59 KB

    59,438 B  gaps >= 256 B
    -35,156   provably DATA (data refs, zero flow refs)  -> not code, excluded
    -10,942   jump-table bodies (8 gaps adjacent to a base) -> finish the tables
    =13,340 B in 11 gaps: THE GENUINE UNKNOWN

**~13 KB of a 256 KB image is what stands between here and full coverage.** That
is a tractable, terminating target rather than an open-ended search, which is why
the trap should not be the plan.

## Revised F1.1 remainder

    1. Finish the jump tables. Two bases (0xD0EC, 0xD5A2) yield zero entries and
       came from find_base over the LINEAR listing, so they are the likeliest to
       be wrong — re-derive them from Ghidra's own decoded dispatch sites. Then
       re-extract all 15 with the sentinel rule and disassemble. Expected to
       account for the 10,942 B.
    2. Classify the remaining 11 gaps (13,340 B) one at a time. They have no
       references and no table adjacency, so the next evidence is structural:
       do their bytes read as 68K instruction streams or as tables? A gap that
       resists both tests gets named in the record, not waved through.
    3. Generate the coverage table in Cavern Crawl's format — per function,
       PORTED / ANALYZED / UNTOUCHED, with the evidence — from Ghidra's 536
       functions. That is the instrument that drove 12 -> 83 of 93 there, and it
       is how "the entire game is decoded" becomes checkable rather than claimed.


---

# F1.1 STATE after using docs/audit (2026-09-26 17:45 ET) — and the HEAT MAP

The prior work's saved seeds (`docs/audit/altbeast_seeds.json`) plus the 131
functions in `docs/audit/function_map.md` that a fresh import misses:

    instructions      13,533 -> 20,355
    functions            536 ->    720   (NOT 2,121: see the seed split below)
    low-region code     40.9% ->  60.8%  = 79,676 B, matching the map's 60.9 KB

Seed split matters: all 2,229 as DISASSEMBLY seeds, only the 823 genuine entries
as FUNCTION seeds. Passing them all as function seeds gives 2,121 functions,
which is the exact failure LOOP-DECOMPILE 19 records.

## The heat map (tools/f1_heatmap.py, tools/ghidra/f1_classify.py)

Partial EXECUTION coverage is normal -- one playthrough does not run every
routine -- so the map exists to make that trackable instead of alarming.
`# executed   + decoded code   . data   ? undecoded inside the code band`

    000000  ????????#?++++++++######+++####+##++######??++???????????????+++##++#########????????#####+###+#
    003000  ###+??##++????##++######+#######???+##+++++###??+++#########++++####++++++++++++?++++++++++++++?
    ...
    01B000  ++++++++++++++++++++++??????????????????????????????????????????????????????????????????????????
    01E000  ?????????????+?????++???????????????????????????????????????????????????????????????????????????
    021000  ................................................................................................

**It is BAND-AWARE, and that is the point.** An earlier render reported "UNKNOWN
67.8%", which was 128 KB of art and level tables counted as unknown. Four
independent measurements put no code above ~0x1F000: Ghidra's recursive descent
finds none, the oracle executed no PC there across four traces, every branch edge
into it originates in bytes Ghidra never reaches, and the region carries only
data references. So above the code end, undecoded means DATA.

    code, EXECUTED           2,055 B    0.8%
    code, not executed      77,621 B   29.6%   fine: other rounds, other enemies
    data                   137,075 B   52.2%
    UNKNOWN                 45,393 B   17.3%

    UNKNOWN inside the code band 0x400-0x1F000 = 36.0% OF THE BAND
    <- that, and only that, is the distance to full coverage

## What remains

45,393 bytes inside the code band. `docs/audit/altbeast_seeds.json` carries
19,137 unused `repair_seeds` and `docs/audit/bound_repairs.md` documents a
bounds-repair pass (`tools/ghidra/fix_bounds.py`, `fix_bounds2.py`) that has not
been run against this import. Run those before deriving anything new -- the
afternoon's lesson is that this repo already holds more of the answer than it
looks like.


---

# F1.1 DELIVERS C: 623 of 623 functions decompiled (2026-09-26 20:45 ET)

Mike's instruction: "We want to have working code that we can port to risc,
either as C or as assembly. I dont care whatever is easiest to manage." C is
easiest, and Ghidra's decompiler produces it directly.

    bounds-repair pass (fix_bounds.py, finally run against this import)
      deleted 96 zero-bodied uncalled functions -- including the 73 ori.b
      fragments on zero words -- and extended 69
      => 623 functions, matching the 622 estimate from the size distribution

    tools/ghidra/f1_decompile.py
      named 209 functions from docs/audit/function_map.md
      DECOMPILED 623 of 623, ZERO failures, 4 seconds, 1.09 MB
      -> docs/proof/altbeast.c (gitignored: ROM-derived)

The output is portable as-is for plumbing. Two samples:

    void rng_next(void) {
      if (_DAT_fffff014 == 0) { _DAT_fffff014 = 0x2a6d365a; }
      _DAT_fffff014 = _DAT_fffff014 * 0x29;
      _DAT_fffff014 = (uint)(ushort)((short)(_DAT_fffff014 >> 0x10) +
                      (short)_DAT_fffff014) << 0x10 | _DAT_fffff014 & 0xffff;
    }

    void unpack_level_tilemap(void) {
      DAT_fffff095 = (undefined)*(undefined2 *)
                     (&DAT_00001ce2 + (short)((DAT_fffff142 & 7) * 6));
      tilemap_unpack_hi(); tilemap_unpack_lo();
      tilemap_block_a5(); tilemap_block_04();
    }

68K work RAM appears as `_DAT_fffffxxx` globals, which for the port become
SDRAM globals -- a mechanical rename, and the 15.6 KB state region F0(a) placed.

## The coverage table (docs/proof/coverage.md)

Generated in the format that drove the Kyocera project from 12 to 83 PORTED of
93 (`tools/ghidra/f1_coverage_table.py`):

    623 functions | 79,720 B | 20,363 instrs | 623 decompiled | 35 core | 101 with EXEC

Per function: entry, bytes, instructions, callers, name, class, CORE (5+
behaviour tags = an object handler), EXEC (instructions an oracle trace was
observed executing), STATUS. **STATUS starts at ANALYZED and becomes PORTED by
hand as F2 lands each routine and its L1 differential passes**, so "the entire
game is decoded" is checkable rather than claimed.

Only **101 of 623 functions have any executed instruction** — which is the trace
saturation, not a defect, and it is why the builder's trace campaign is the task
that moves this number.

## What this changes about F1.3

The emitter question ("58 mnemonic forms, or P-code with dead-flag elimination")
is now only relevant for the ~35 object handlers, if at all. **For the 525
plumbing functions the answer is: compile the decompiled C for SH-2 with the
existing toolchain.** That is why F1.3 should be decided after F1.1 rather than
before, and it is decided differently than either option assumed.


---

# TARGET LANGUAGE: C. 415 of 623 functions ALREADY COMPILE for SH-2 (2026-09-26 21:30 ET)

Mike: "whats the target portable language?" and earlier "either as C or as
assembly. I dont care whatever is easiest to manage." **C, and it is settled by
measurement rather than preference.**

    tools/cport_prep.py docs/proof/altbeast.c <outdir>
    sh-elf-gcc -m2 -ffreestanding -c -O2 -I<outdir> altbeast_port.c

    FUNCTIONS THAT COMPILE FOR SH-2 TODAY:  415 of 623  = 66.6%

Three reasons C wins here, in order of weight:

  1. **BIG-ENDIAN ON BOTH SIDES.** The 32X SH-2 build is `elf32-sh` / `-m2` =
     big-endian, the same as the System 16 68000. Struct layout, byte order and
     word packing carry over with NO rewrite. That is the fact that makes
     decompiled C portable at all.
  2. **Zero new toolchain.** `sh_src/` is already C built by marsdev's
     sh-elf-gcc with `-ffreestanding`; translated functions drop into the
     existing build. An assembly path would need the emitter F1.3 was going to
     be, and now does not need to be.
  3. **The budget affords it.** F0 measured the port at 3.1% of a vint
     cache-resident and 23.7% all-miss. Even if gcc were 2x worse than hand
     assembly it fits. Hand assembly stays available for whatever measurement
     later shows needs it -- `docs/design/collision_sh2.s` is the reference for
     that case.

## What `tools/cport_prep.py` generates (all mechanical, nothing hand-written)

    ghidra_types.h   Ghidra's output typedefs, freestanding (no stdint.h)
    ghidra_rt.h      its intrinsics: CONCATnn / SUBnn / ZEXT / SEXT -- plain
                     shifts, because both sides are big-endian
    protos.h         614 forward declarations, taken from the definitions
    dat_syms.h       a declaration for all 499 absolute globals, by region:
                       83 program ROM (const data)
                       27 S16 video/IO hardware
                      362 68K WORK RAM -> the ported state (F0(a)'s 15.6 KB)
                       27 other
    altbeast_port.c  the corpus with those included

Error count over the whole corpus fell 1,701 -> 589 as those landed.

## THE ONE UPSTREAM CAUSE OF THE REMAINING 208

    PSEUDO-REGISTERS: 45 distinct, and the distribution is lopsided
      unaff_A6    9,821 refs      <- the current-object pointer
      unaff_D3w     771
      unaff_D6w     462
      unaff_D7w     392
      in_D0b        297
      extraout_A1   281

`unaff_A6` alone is 9,821 references. **Ghidra has no calling convention
declared for this program**, so a register the caller sets and the callee reads
becomes an `unaff_` pseudo-variable, and a result returned in D0 becomes a
`void` function whose value is then used -- which is exactly the residual error
profile (211 "invalid use of void expression", the rest trailing off).

**This is one fix in one place, and it is upstream in Ghidra, not in
postprocessing.** The convention is already documented by the hand translation:
`docs/design/collision_sh2.s` fixes r14 = state base and r4 = a4 = the current
object slot, and the game's own idiom is the current object in a6 with the slot
base in a4. Declaring that -- a6/a4/a5 as parameters, d0 as the return -- should
convert most of the 208, because A6 is *unaffected*: passed through, not
modified, which in a single-threaded game loop means it is semantically a
global `cur_obj` pointer.

## Next, in order

    1. Declare the 68K calling convention in Ghidra (a6, a4, a5 in; d0 out) and
       re-decompile. Measure 415/623 again -- this is the one change with real
       leverage on it.
    2. Type the 362 WORK RAM globals into the ported state structure. They are
       F0(a)'s 15.6 KB and the region already has room.
    3. The residual 31 cross-function `goto LAB_xxxx` sites need reading, not
       generating. They are named in the failing list, not waved through.
    4. Then F1.2's consumer half: compile a function, run it against the
       builder's reference vectors, mark it PORTED in docs/proof/coverage.md.


---

# 504 of 623 FUNCTIONS COMPILE FOR SH-2 (80.9%) — 2026-09-26 22:30 ET

Coverage after the builder's 315 trace-proven seeds and a reproducible rebuild
(`tools/ghidra/f1_rebuild.sh`):

    instructions 20,956   functions 623   code 82,228 B = 62.7% of the low region
    (was 20,363 / 623 / 79,720 B = 60.8%)

Compile progression, each step a generated fix in `tools/cport_prep.py`:

    baseline: types + intrinsics + 614 protos + 499 globals      415  66.6%
    + halt_* intrinsics, xRam globals, void->uint retyping        477  76.6%
    + bare `return;` -> `return 0;` in retyped fns, func_0x       504  80.9%

**The fix that mattered most was retyping void functions whose result is
used.** The 68K returns in D0 and Ghidra did not model it, so 302 "invalid use
of void expression" errors all traced to one cause. A non-void function with no
return statement is legal C and closer to the hardware truth than `void`.

## The remaining 119, by class — all bounded, none mysterious

    91  invalid use of void expression   results consumed in forms the retyping
                                        regex misses (through pointers, casts)
    49  assignment to 'byte' from ...    a global read at several widths; the
                                        declaration needs to be the widest use
    31  'LAB_xxxx' undeclared            CROSS-FUNCTION goto. Genuinely needs
                                        reading, not generating -- these are
                                        functions whose bounds are still wrong,
                                        or real shared tails. Listed in
                                        <outdir>/failing.txt, not waved through.
     9  func_0xNNNNNNNN                  an unresolved call target

Three of the four are more generation. **The 31 cross-function gotos are the
only ones needing a human**, and they are named.

## What the convention experiment cost, recorded so it is not repeated

Declaring the 68K calling convention in Ghidra -- A6/A0/D0... as custom-storage
parameters plus a forced D0 return on the 451 functions that showed a
pseudo-register -- **dropped the compile rate from 415 to 253 and raised
whole-corpus errors from 589 to 812.** Pseudo-registers fell 45 -> 29, so it
half-worked, but forcing a return on functions that return nothing, and adding
parameters the call sites do not pass, costs more than it buys.

Worse, **the revert did not restore the original state**: reverting to
DYNAMIC_STORAGE left signatures altered and the baseline measured 303 instead of
415. That is why `tools/ghidra/f1_rebuild.sh` exists -- in-place edits to an
analysed project are not reliably reversible; re-importing is. Any future
experiment on the project should run after a rebuild, not on top of one.

The right model for A6 remains a GLOBAL, not a parameter: it is unaffected
across calls, so in a single-threaded game loop it IS the current-object
pointer the dispatcher at 0x039AC maintains. `pseudoregs.h` declares it that
way, and `obj_record.h` carries the layout the builder read off the oracle.


---

# WHAT "100%" MEANS, AND HALF THE FUNCTIONS DO NOT NEED TRANSCRIBING (2026-09-26 23:45 ET)

Mike: "Half of the functions we can just derive. we understand the hardware
platform it's written to intimately." **Measured against the map: he is right,
and it reframes the target.**

    NAMED behaviours (derivable from the name + the hw spec)  35 fns   4,600 B
    hardware glue  (derivable from the S16 memory map)       117 fns   7,688 B
    leaf/helper    (small, derivable by reading)             150 fns   5,063 B
    ----------------------------------------------------------------------------
    DERIVABLE                              302 of 560 = 54% of functions
                                        17,351 of 62,385 B = 28% of bytes

The named ones are exactly the hardware-shaped work: `unpack_level_tilemap`,
`tilemap_unpack_hi/lo`, `tilemap_block_04/a5`, `sprite_frame_lookup`,
`sprite_build_and_cull`, `sprite_build_depth_banded`, `zoom_scale_lookup`,
`animate`, `animate_variant`, `clamp_x_velocity/y`, `integrate_position`,
`hitbox_a/b/c_expand`, `floor_collide`, `collide_box_b`, `rng_next`,
`order_list_insert`, `clear_textram/tileram/spriteram_and_pool`,
`textram_stride_write`, `input_edges`, `wait_frame_flag`, `object_dispatcher`,
`irq4_handler`, `colour_cycle_streamer`.

## THREE DIFFERENT 100%s. Only one of them is the finish line.

**A. COMPILE RATE — 578 of 680 = 85.0%. NOT the goal.** This measures how much
of Ghidra's literal transcription survives gcc. It is a useful proxy while the
corpus is the artifact, but ~15% of the residue is transcription noise on
functions we would hand-write anyway. Chasing it to 100% would be work spent
polishing output we intend to replace.

**B. CODE/DATA CLASSIFICATION — 64.0% of the low region is code, 34.8%
(45,577 B) undecoded. "100%" is the wrong word:** some of the band is genuinely
data (the reference pass put 59% of the big gaps as data-referenced, with ZERO
flow references anywhere). The right target is **every byte classified as code
or data with evidence**, and shape D showed the lever: a new install idiom found
108 routine pointers and 89 functions in one pass. More idioms, not more tracing.

**C. FUNCTIONS WITH A CORRECT, VERIFIED SH-2 IMPLEMENTATION. This is the finish
line**, and it is the `STATUS` column of `docs/proof/coverage.md`. 100% means
every function PORTED with a passing L1 differential. Cavern Crawl reached
0 UNTOUCHED of 93 against exactly this, and its definition of PORTED is the one
to copy: *"behavior reconstructed... matched by cited FUN_ address or clear
behavioural equivalence"* -- reconstructed, not transcribed.

## So the route to (C) splits, and the split is Mike's

    302 functions (54%), 28% of bytes   DERIVE from the hardware spec and the
                                        name. Write clean C, verify against the
                                        L1 vectors. Ghidra's transcription is a
                                        cross-check, not the source.
    258 functions (46%), 72% of bytes   TRANSLATE from the decompiled C, and
                                        verify. This is where the 85% compile
                                        rate actually matters -- and it is the
                                        83 object handlers plus the entangled
                                        logic around them.

Two consequences worth stating plainly:

  - **The derivable half is where the port gets BETTER than a transcription.**
    `integrate_position` is 4 bytes of 68K; written from the spec it is one line
    of C that the SH-2 compiler can inline. The 128-byte object stride can
    become 144 in the same pass (FULL-PORT §6(b), a measured 4.6-7.8x cache
    penalty), which only a port can take.
  - **The non-derivable half is where fidelity risk lives**, which is the same
    conclusion §3 reached from the tag classification, now with a function list
    attached rather than a percentage.

## Immediate next steps, in order

    1. Hand-write the 35 NAMED functions from the spec, verify each against the
       builder's L1 vectors, mark PORTED. These are the ones where "we
       understand the platform intimately" is literally true.
    2. Re-run the gap reference pass (tools/ghidra/f1_gapref.py) now that
       coverage is 64.0%, to re-split the remaining 45,577 B into data and
       undiscovered code.
    3. Hunt more install idioms the way shape D was found: the residue is
       routine pointers, and each idiom is worth ~100 functions.
    4. Stop tracking compile rate as the headline. Track (C).


---

# WHY 100% COVERAGE, AND THE EXACT LEDGER (2026-09-27 00:45 ET)

Mike: "We still have a NEED to identify 100% of the decompiled code coverage, but
as a way to ensure we only have what we need to port the game logic."

**That is the right reason and it is not the one I had been working to.** Full
classification is not a porting target -- it is a COMPLETENESS PROOF for the
port's scope. The argument runs negatively: we can only claim "we have all the
game logic" once nothing is unaccounted for, because logic could be hiding in the
unclassified space. So coverage is how the scope reduction in §4 becomes
trustworthy rather than hopeful.

## The ledger, code band 0x400-0x1F000 = 125,952 bytes

    decoded CODE                83,840   66.6%
    data (defined + referenced)  1,842    1.4%
    UNCLASSIFIED                40,270   32.0%

And the unclassified space, by gap size -- this is where the next work is:

        1-    8 B :   97 gaps,    321 B    alignment padding
        9-   32 B :  112 gaps,  2,081 B
       33-  128 B :  251 gaps, 18,515 B    <- 46% of the unknown
      129-  256 B :   37 gaps,  6,173 B
      257- 1024 B :   21 gaps,  8,592 B
     1025-    + B :    2 gaps,  4,588 B

**251 gaps of 33-128 bytes hold 46% of it, and the mean real function is 102
bytes.** That is function-shaped, not data-shaped. And the reference pass agrees
from the other side: over the big gaps, 19,112 B are data-referenced, 5,802 B are
unreferenced, and **CODE/MIXED is still ZERO -- no gap anywhere has a flow
reference into it.** So whatever those 251 blocks are, nothing CALLS them: they
are reached by a pointer, as every other discovery here has been.

## Two idiom hunts tonight: one worked, one fragmented

**BROADENING SHAPE D to any destination register: marginal.** `move.l An,(d16,Am)`
for all Am, not just A6 -- 138 sites, 122 targets against the A6-only scan's 108.
Destinations are A6 (112) and A5 (10) and nothing else, so A6 really was the whole
story. 12 newly disassembled, 687 functions. (Opcode note for whoever repeats
this: `move.l An,(d16,Am)` is `0x2000 | Am<<9 | 0x148 | An`; I first dropped the
source-mode bits and matched zero sites.)

**TABLES OF ABSOLUTE LONGS: FRAGMENTED THE CALL GRAPH, REVERTED.** Scanning for
runs of 4+ consecutive longs pointing into the code band found 14 tables and 227
targets, and seeding them gave 25 new instructions but took functions 687 -> 859.
The size distribution convicts it: functions of 257-1K fell 42 -> 30 and >1K fell
7 -> 6 while 17-64 B jumped 226 -> 329. That is LOOP-DECOMPILE 19's failure again
-- spurious function starts splitting real functions.

The cause is identifiable and the scan is salvageable: **two of the 14 "tables"
are false positives that dominate it** -- 0x00004 with 251 entries is the 68K
VECTOR TABLE, whose targets are all 0x400-0x40C, and 0x06DCA with 154 entries is
the zero-word data region already identified beside the dispatch tables at
0x06D70/0x06DA0. Filter tables whose targets cluster in a span under ~0x20, and
whose base sits in known data, and retry.

## So the route to a complete classification

    1. Retry the long-table scan with the two false-positive filters above. The
       remaining 12 tables are plausible and were never applied cleanly.
    2. The 251 mid-size gaps are the prize. Nothing flow-references them, so
       find the idiom that reaches them -- that is how shape D was found, and
       each idiom so far has been worth 25-108 functions.
    3. Classify, do not decode, what resists: a gap with data references and no
       flow reference is data, and the port does not carry it. The goal is every
       byte accounted for with EVIDENCE, not every byte decoded.
    4. Only then is "we have all the game logic" a claim rather than a hope.


---

# IT COMPILES. 734 functions, 132,220 bytes of SH-2 code, ZERO errors (2026-09-26 23:35 ET)

    tools/cport_build.sh
      pass 1: 0 errors
      *** SH-2 OBJECT PRODUCED ***

    port.o   elf32-sh, architecture sh2
      .text  0x2047C = 132,220 bytes
      734 functions emitted (T symbols)

**132,220 bytes of SH-2 from 85,016 bytes of 68K = 1.56x**, which sits inside the
range the hand translation predicted (collision_sh2.s: 1.08-1.18x bytes
idiomatic, 1.84x literal). An independent corroboration of that estimate from a
completely different route.

## The error count went 721 -> 0. Every fix was generated, none hand-edited

    721  starting point (whole corpus as one translation unit)
    246  -fpermissive + downgrade GCC 15's pointer/integer conversions
     88  typedef bool/true/false (Ghidra emits them; freestanding C99 has none)
     72  extras.h: declare whatever gcc calls undeclared, by feedback
     34  :: -> __ BEFORE signature normalisation; BADSPACEBASE as a typedef;
          dereferenced DATs declared as pointers; Ghidra's X._2_2_ sub-field
          syntax rewritten to a cast
     15  sanitise symbol names carrying string content (s_TAKYOUR_NAME__<>_...)
      5  subscripted function names read through a byte pointer; (code) casts
          to (byte); duplicate map names suffixed
      2  `code *` locals that are subscripted rather than called, retyped
      0

**Two dialect facts did most of the damage and neither is a bug in the code:**

  - **GCC 15.2 promotes 68K-normal pointer/integer punning to errors.**
    `_DAT_fffff0ec = &DAT_00410e80;` is exactly what the 68K does.
    `-Wno-error=incompatible-pointer-types -Wno-error=int-conversion` and 475
    errors vanish.
  - **C23 makes `f()` mean `f(void)`**, so Ghidra's inconsistent arities became
    57 "too many arguments". `-std=gnu99` restores the unprototyped meaning,
    which is what an un-analysed 68K function actually is.

## The one structural decision: every function is `uint NAME()`

Ghidra has no calling convention for this program, so its parameter lists are
invention -- the bodies read their inputs from `unaff_A6` / `in_D0b`
pseudo-registers, which `pseudoregs.h` declares as globals. Normalising all 734
to `uint NAME()` is both what compiles and what is HONEST about what we know.
Real signatures get declared one function at a time as the port understands each
against the L1 vectors.

## A corroboration worth noting

The naming pass produced `irq4_handler` at **0x00404** (4 bytes) and a second at
**0x02AAC** (784 bytes), which the dedupe suffixed. That is exactly the builder's
finding from the harness: *"the IRQ lands on the thunk at 0x000404, not on the
handler body at 0x2AAC"*. Two independent routes, same structure.

## What this does and does not mean

**Does:** the entire decompiled program is now valid C that a real SH-2 compiler
accepts, reproducibly, from one command, with every transformation generated and
explained. That is the artifact the port consumes, and the baseline any hand-
written replacement is diffed against.

**Does not:** it is not correct, and it is not meant to be run. Nothing has been
verified against a reference vector yet; the pseudo-register globals are a
placeholder for a calling convention; and per Mike's own reframing, 54% of these
functions will be REPLACED by spec-derived code rather than shipped as
transcriptions. Compiling was never the goal -- it is the floor that makes
per-function verification possible.


---

# COVERAGE CLOSED: code accounted for to within 280 bytes (2026-09-27 00:10 ET)

## The final ledger, code band 0x400-0x1F000 = 125,952 bytes

    decoded CODE          86,246   68.5%
    data (defined + ref)   1,797    1.4%
    unclassified          37,909   30.1%

## And the unclassified 30% is resolved by WHERE each gap begins

`tools/ghidra/f1_gapedge.py` (new) classifies every gap by the instruction
immediately before it. A gap that starts after `rts`/`rte`/`jmp`/`bra` is a CODE
BOUNDARY -- the previous function ended and something unreached begins. A gap that
starts MID-FLOW, after an ordinary instruction, is what an embedded table looks
like: the disassembler walked up to it and stopped.

    after a TERMINATOR -> candidate CODE    :    2 gaps,     280 B
    MID-FLOW           -> embedded DATA     :  367 gaps,  39,306 B

**The heuristic is not a guess -- it was 100% predictive on its first pass:** 42
of 42 terminator-adjacent gaps decoded cleanly, with no fragmentation (large
function counts held at 43 and 7). Iterated to a fixpoint
(`tools/ghidra/f1_gapedge_loop.sh`), it converged with **2 gaps of 280 bytes
left that will not decode.**

And the reference pass agrees from an independent direction: over the big gaps,
**CODE/MIXED remains ZERO -- not one gap anywhere has a flow reference into it.**

**So: the CODE is closed to within 280 bytes across 2 gaps.** Everything else
unclassified is mid-flow, and two independent signals say data.

**Caveat, stated:** mid-flow position is strong evidence, not proof. A gap could
in principle be a function whose first instruction fails to decode. 367 of them
all being that is implausible, and zero flow references supports data, but the 2
resisting gaps and the mid-flow population are what an audit would re-examine.

## Coverage progression tonight

    functions      623 -> 680 -> 687 -> 734 -> 771
    instructions 20,363 -> 21,407 -> 21,719 -> 22,294
    code           60.8% -> 64.0% -> 64.9% -> 65.8% -> 68.5% of the band

Each step from a different idiom, none from brute force:

    the builder's 315 trace-proven PCs              +593 instructions
    shape D: lea pc@(H),An ; move.l An,(d,A6)       +89 functions (108 targets)
    strided pointer tables (strides 4-32, all offsets)  +71 instructions
    the gap-edge heuristic, iterated to a fixpoint   +42 functions, 42/42 hit


---

# IT LINKS: a runnable SH-2 image, and a fourth confirmation of F0(a) (2026-09-27 11:15 ET)

`tools/cport_prep.py` now also emits `defs.c` -- one definition per extern, so the
corpus does not merely compile but LINKS.

    verify.elf   elf32-sh, architecture sh2, 181,012 bytes
      .text   0x210F8 = 135,416 bytes at 0x0C010000
      .data   4 B    .bss  0x978

**The only symbols the corpus could not resolve were four libgcc helpers** --
`__sdivsi3`, `__ashlsi3_r0`, `__ashiftrt_r4_7/8` -- which SH-2 needs because it
has no barrel shifter and no divide instruction. Linking `libgcc.a` closed them.
Nothing in the decompiled game itself was missing.

## THE FOURTH CONFIRMATION OF F0(a)'s STATE BUDGET

Classifying the 588 absolute symbols by region:

    WRAM           401 symbols   68K span 0xFFC000-0xFFFF82 = 16,258 B
    program ROM    101
    S16 video/IO    27
    S16 palette     18
    S16 I/O ports   41   (0xC40001 the display gate, 0xC41001.. the I/O block)

**16,258 bytes against F0(a)'s budgeted 15,974 -- a 1.8% difference**, and this is
the FOURTH independent derivation of that figure:

    vint_census.py reachability                      14.8 KB
    the builder's WRAMCODE exactness test           ~16   KB
    FULL-PORT F0(a)'s working figure                 15,974 B
    the decompiled corpus's own symbol span          16,258 B

Four routes, one number. The 52 KB tilemap shadow the port frees (F0(a)) covers it
three times over.

## What remains before a vector can be run

**A linker script placing each WRAM symbol at its TRUE 68K offset.** Today the
linker places them wherever it likes, so pointer arithmetic that crosses symbols
-- `*(undefined2 *)(&DAT_x + 2)` -- does not reach the next symbol. Assigning
`_DAT_ffffXXXX = WRAM_BASE + (0xXXXX - 0xC000)` makes the block contiguous and
correct, and it is the same layout the port needs anyway.

Two smaller items: the SH ABI prefixes C symbols with `_`, so the entry is
`__start` not `_start`; and qemu's `r2d` board boots to its own reset vector at
0xAC800000 rather than the ELF entry, so the harness needs either a board-aware
stub or a different execution route.

**Stated plainly: nothing has been verified against a vector yet.** The image
links and is structurally complete; correctness work has not started.


---

# TRUE-ADDRESS LAYOUT DONE; EXECUTION ROUTE STILL OPEN (2026-09-27 11:25 ET)

`tools/cport_layout.py` (new) places every absolute symbol at its real address,
which was the structural blocker: the linker had been scattering work-RAM
symbols, so any access crossing one -- `*(undefined2 *)(&DAT_x + 2)`, which the
game does constantly -- landed nowhere.

    wram   401 symbols   68K 0xFFC000-0xFFFF82 = 16,259 B, ONE contiguous block
    rom    101 symbols   the REAL prog68k.bin linked in, so constant reads work
    mmio    56 symbols   zeroed stubs (0xC40001 the display gate, the I/O block)
    odd      *           anything above the 68K's 24-bit bus, given its own block

**Verified by the placement itself:** `__DAT_fffff014` links at 0x0C043014, and
0xFFF014 - 0xFFC000 = 0x3014 against a WRAM base of 0x0C040000. Exact.

    verify.elf   elf32-sh, sh2, 442,992 bytes
      .text   0x210FC @ 0x0C010000
      .romimg 0x40000 @ 0x0C100000   <- the real 256 KB program
      .wram   0x04000 @ 0x0C040000
      .mmio   0x20000 @ 0x0C060000

Note the layout is generated from `dat_syms.h`, so it regenerates with the
corpus and is not a hand-maintained map. Ghidra writes work RAM in the
sign-extended short form (0xFFFFF014 for the 68K's 0xFFF014) because that is what
`abs.w` addressing produces; the tool converts. And the SH ABI prefixes C symbols
with one underscore, so both spellings are PROVIDEd.

## The execution route is NOT solved, and I stopped rather than sink more into it

`qemu-system-sh4 -M r2d` accepts the image and `-device loader,file=...,cpu-num=0`
correctly sets `pc=0x0C010000` -- but the CPU does not execute it. `-kernel` does
not load a bare-metal ELF at all (memory at 0x0C010000 reads zero). This is board
bring-up, not port work.

**Two candidate routes, and the second is better:**

  1. Continue with qemu: find why r2d will not run from a loaded image (reset
     overriding PC, CPU halted, or the SDRAM controller not initialised). Board
     archaeology, and qemu is an SH-4 system emulator being asked to be an SH-2
     unit-test harness.
  2. **Use the project's own SH-2 environment.** ares-headless already runs 32X
     roms exactly (LESSONS: "headless ares SH-2 = instruction cycles only", and
     it is the project's exactness instrument), and `tools/state_health.py` and
     `ares_diag_at.py` already read SH-2 state out of savestates. A test rom that
     calls one ported function with a vector's entry state and dumps the result
     is the same shape as every probe this project already builds -- and it runs
     on the instrument the project already trusts, rather than a third emulator
     whose fidelity nobody here has characterised.

Route 2 also has a property route 1 does not: it tests the function **on the
target**, so a passing differential means something about the 32X rather than
about qemu.
