# HANDOFF — decompile thread 3 to thread 4 (2026-09-25, end of session)

**Why this exists:** Mike ended thread 3 because it invented things. Three
times in one day it wrote its own inference into the shared record in the
project's voice:

  1. STATE.md and HANDOFF-FROM-DECOMPILE both said the palette rotor was
     "HELD on Mike's word". He had never ruled on it. The phrase was this
     thread paraphrasing a 2026-08-16 priority list into a decision.
  2. It told Mike `rom/night/notag1.32x` was "TESTABLE". That rom is dated
     2026-09-17 and the line is rg1 (2026-09-24). Mike pasted it to the
     builder before the error was caught.
  3. It wrote "RULE: a rom older than the current line is EVIDENCE, not a
     candidate" into MESSAGES.md's ledger. That rule is the thread's own
     invention. Mike: *"You are hallucinating? I never made up a rule about
     this path."*

**The rule for thread 4, and it is the only one that matters here: your
inferences go in a handoff under your own name. MESSAGES.md, STATE.md and
LESSONS.md carry measurements, quotes, and nothing else.** If you want a rule
adopted, propose it to Mike and wait.

---

## 1. Read these first

    grep -i <keyword> FACTS.md     BEFORE forming any hypothesis. 716 facts,
                                   each with the file:line that measured it.
                                   Built 2026-09-25 by tools/build_facts.py
                                   from LESSONS (headings + 49 addenda),
                                   STATE, MESSAGES (cards + their caveats),
                                   the Makefile's 170 flag comments and the
                                   per-game patcher TABLES. Regenerate after
                                   editing those; --check exits 1 if stale.
    INTENT.md / STATE.md / LESSONS.md / docs/design/REBUILD.md   per CLAUDE.md
    LOOP-PROTOCOL.md               the PREMISE CHECK now opens with that grep
    docs/handoff/HANDOFF-FROM-DECOMPILE.md   sections 1-13, thread 3's output.
                                   Sections 6, 8 and 9 are SPENT or WRONG --
                                   see part 3 below before using any of them.

## 2. DERIVED FROM BYTES TODAY — solid, cite freely

**The palette cycler** (the live thread of work):

    0x30B2   the cycler, 21 instructions. Writes palette RAM DIRECTLY at
             base + slot*16, four movel = 16 bytes = 8 COLOURS. Never uses
             RequestPaletteUpdate (0x3B2E, 116 callers) or
             QueuePaletteUpdate (0x3BEC, 0 direct callers).
    0x2C16   its ONE caller, inside the IRQ4 handler, beside the scroll copy
             (0x2ACA), sprite-table copy (0x2B16) and queue drain (0x2B4C).
    0xFFF300 the slot table. 8-byte entries: [0] slot|0x80, [1] timer,
             [2-5] ring-script pointer, [6-7] ring index. The installer
             zeroes 32 longwords, so the live table is 0xFFF300-0xFFF380 =
             16 entries.
    0x1A6BA  the installer, runs at scene setup.
    0x1A6FA  its script: count word = 3, then (slot, ring pointer) pairs:
               slot 19 -> ring 0x01A720      7 steps, timer 1 EVERY step
               slot 20 -> ring 0x01A7A0      6 steps, timers 4/2/2...
               slot 21 -> ring 0x01A7A0      SAME POINTER as slot 20
             Ring format: [0..1] step count, then N steps of 18 bytes
             (2-byte timer + 16 bytes = 8 colours). Slot 19 step 0 blues
             [31,19,21,23,25,27,29,31], step 1 [31,29,31,19,21,23,25,27] --
             a 7-position rotation with pen 0 pinned. That is the shimmer.
    patch_game.py:1021   OUR PATCHER ALREADY OWNS THIS ROUTINE: "PRECISE
             THUNK A -- 0x30C2, the colour-cycle engine and by far the
             busiest writer (8818 writes over 3000 frames, ~3 per frame)".
             It displaces the lea to a thunk that computes a runtime dirty
             mask. So the cycler is what drives our palette dirty-marking.
    the game never READS palette RAM: all 43 references to 0x84xxxx are
             24 movew + 3 clrw (writes) and 16 lea (address loads).

**The MiSTer core's ROM path** (`srcref/S32X_MiSTer/rtl/`):

    IF.sv:780     MD_ROM_SEL = VA[23:20] < 4'h4 & ADCR.ADEN -- a pure address
                  decode. The file references NO 68000 function-code lines, so
                  the RS_MD_WAIT path is entered for operand reads as much as
                  instruction fetches.
    IF.sv:864-877 RS_SH_CONT loops back to RS_SH_WAIT while the SH-2 keeps
                  asserting, so an SH-2 ROM tenure spans consecutive reads.
    CACHE.sv:488  a line fill walks IBADDR[3:2] through 4 longwords with
                  IBBURST asserted: 16 bytes = 8 word transactions on the
                  16-bit cart port.
    m_main.c:7025 cache_fill: 64 bytes per tile as 16 consecutive longwords
                  from the cached cart window, budget to 384 tiles, gated
                  dt = frt() - t_vint <= 8000 -- the early-vint slot.

**The 68K program, structural:**

    vectors       reset -> 0x40E, IRQ4/vblank -> 0x2AAC (both via a jump
                  table at 0x400).
    port census   tools/vint_census.py: 25,284 reached instructions / 88,534
                  bytes from those two entries (249 pointer-table targets
                  recovered past 78 register-indirect sites); 14.8 KB of WRAM
                  state (~15.6 KB with the palette queue, which the method
                  undercounts). 43.7% of reached instructions reference
                  memory, 83.9% of those register-indirect.
    partition     tools/port_partition.py: 572 of 590 state-touching routines
                  (97%) are ONE connected component. The only two separate
                  components are the palette slot allocator (16 routines, 175
                  instructions) and the cycler + installer (2 routines, 51
                  instructions, mutable state = the 256-byte 0xFFF300 table
                  written once per scene). That is card O-14.
    translation   docs/design/collision_sh2.s: collision_detection by hand,
                  1.84x literal / 1.69x idiomatic instructions, 1.08-1.18x
                  bytes, 1.27x executed. Budget is 383,333 SH-2 cycles a vint
                  / 8,184 68K work instructions = 46.8 cycles per 68K
                  instruction, so an INTERPRETER does not fit (50 SH-2
                  instructions per 68K instruction = 1.07x the vint, and the
                  SH-2 is not idle -- it is the renderer). AOT fits at 4-11%.
    quantiser     over the cyclers' ramp (19,21,23,25,27,29,31), the line's
                  (+2)>>2 gives 3 distinct MD levels; PAL_QUANT_RAMP's table
                  (m_main.c:2932, PALQUANT=1, NOT on the line) gives 4. It
                  differs on 16 of 32 inputs, so it is a GLOBAL colour change
                  needing its own play pass.

## 3. WHAT THREAD 3 GOT WRONG TODAY — do not inherit these

    "RIGSPAN=14's bytes have never been reported"   They were, in the LESSONS
        addendum at 05:10 ET (rigc14). Asserted a negative without grepping.
    HANDOFF section 6 (the WRAMCODE block set)      SPENT. The builder had
        committed (0xD5A8, 0xD776) -- including 0xD73C, and starting at
        0xD5A8 so 0xD5A6's moveq falls into the stub -- 22 minutes before
        thread 3 "derived" it. The answer was a code comment in
        tools/game_altbeast.py.
    HANDOFF sections 8 and 9 (ROM-port tenure)      MECHANISM REAL, PREDICTION
        DEAD. The builder measured c17 at four phases: cart fetch 130 at raw
        V 0xF0 (the cache_fill slot) against 132 at 0x40. No phase
        confinement; contention is 1-2% of fetch-bound time, a couple of
        lines a pass. The uncached-mirror lever is NOT worth building.
    the blue field via off_99A2                     WRONG NUMBERING. Those
        four bytes (level 0: 16 99 9B 9D) are palette SET ids for the
        transformation SPRITE; the builder's "sets 19/20/21" are a different
        index space. Thread 3 decoded the wrong sets and got blues topping
        out at 22 with bit 14 clear.
    HANDOFF section 13's pen proposal               REVERSED by the builder:
        sets 20 and 21 ALREADY share losslessly (every pen refcount 2). The
        waste is the other way -- set 20 holds six pens carrying one colour
        while set 19's seven pixels are crammed onto one. Both maps are
        frozen first claims.
    "notag1.32x is TESTABLE"                        FALSE, and it reached the
        builder. See part 1.

## 4. What is LIVE

    the pen map       UPDATED 2026-09-25 22:10 ET, AFTER this handoff was
                      first written -- read this entry, not an earlier copy.
                      The builder's TAGKEEP mechanism ("land where you were"
                      restores the remembered map with no colour dedupe) is
                      FALSIFIED BY MEASUREMENT, theirs, and they withdrew it:
                      today's line minus the whole family (TAGKEEP, PENHOLD,
                      PENREPAINT together, fresh rom rom/night/nt1.32x, NOT
                      the dead notag1), aligned to its OWN cut frame and read
                      as a table not a picture, gives a BYTE-IDENTICAL pen
                      table -- group 19 [14,7,7,7,7,7,7,7], group 20
                      [6,13,12,11,10,9,8,8], 21 sharing 20's pens at
                      refcount 2. mds_pin is 0 for all 128 sets at that
                      moment, so the scene table is not holding them either.
                      FIVE configurations now give one table: the line,
                      CHEVFIX, PENSPAN, both, and the family off.
                      What survives: the cut's sets are assigned ONCE while
                      the ring is still uniform, and NOTHING re-assigns them.
                      The drift rules correctly decline (d^2 = 4 on blue vs
                      DRIFT_TOL 18); no free fires; no landing is involved
                      because there is no second assignment to land.
                      NEXT READ, now the only candidate: instrument CARD
                      CHEVPEN (c), the one-shot re-claim at the moment a ring
                      stops being uniform -- does chev_done advance, does
                      chev_force set, does the re-claim reach mdp_assign_set.
                      The CHEVFIX table is identical too, so it is not
                      firing. LESSONS.md carries this at 22:10; STATE.md's
                      card was rewritten to match (commit bc7465a5).
    nt1 / the family  SEPARATE from the defect above, and a candidate for
                      Mike, not a fix: rom/night/nt1.32x (2026-09-25 19:12)
                      is today's line minus the TAGKEEP family, plus
                      WRAMCODE. It COMPILES CLEAN -- the first family-off
                      build since 2026-09-16, when the mdp_wipe_set_tags
                      #ifdef defect was fixed. TAGKEEP is falsified in
                      MEASURED DEAD ([22]=0 [23]=26) and still on the line.
                      VERIFY BEFORE REPEATING: the "restores the gameplay HUD
                      (lives, x2, 50000)" measurement (FACTS 416, MESSAGES
                      2026-09-16 O-2) and Mike's positive 09-17 verdict
                      ("solid presentation, good colors on all sprites and
                      backgrounds", only defect leftover text glyphs = O-6)
                      were both taken on notag1, the OLD rom -- NOT on nt1.
                      They are the reason to look, not evidence about this
                      build. The 09-17 silhouette-blob objection was
                      WITHDRAWN (legitimate enemy sprites). Not speed-ranked.
    the palette rotor 41 lines a vint, the largest item left on the 68K axis
                      and the only one above the rig's ~15-line ranking floor
                      (STATE.md). Mike 2026-09-25 discarded the 2026-08-16
                      priority order that had kept palette work last:
                      *"anything I said six weeks ago doesn't even match our
                      architecture now ... that's disposable."* O-13's own
                      caveat stands: its 94.7/82.3 ceiling is an ARES number
                      and does not transfer to this axis.
    O-14              move the cycler to the SH-2. Ask #1 is with the builder
                      and unanswered: what share of the rotor's 41 lines is
                      slots 19/20/21? If most, O-14 subsumes O-13.
    O-6               leftover text glyphs, the last visible defect Mike named.
    the stack         The builder accepted that stacking is required under the
                      threshold law. NOTE, and this is thread 3's inference
                      not a measurement: WRAMCODE is already on the wc
                      candidates, so BURSTGATE + the mid-pass push alone is
                      ~8 lines, below the ~15-line rig ranking floor. Check
                      that arithmetic before the builder spends a build on it.
    SH-2 cyc/instr    the last assumed number in PORT-CENSUS. The builder
                      agreed to measure it like CARTBENCH, on the rig.

## 5. Working rules that were earned today

  - **Grep FACTS.md before hypothesising.** Every proper noun: flag, routine,
    subsystem, defect.
  - **For a question about a FEATURE, read the file that implements it first.**
    A block list, a flag, a tool's tables -- the answer is usually a comment.
  - **Never assert a negative about the record** without the grep in the same
    turn.
  - **Mike executes a pasted action item immediately.** Verify every rom date,
    flag membership and attribution BEFORE handing him a paste. A correction
    afterwards is worthless.
  - **Quote him or do not attribute.** And never write a rule in the project's
    voice; propose it.
  - **A measurement beats an argument, including yours.** Thread 3 had six
    mechanisms die to the builder's measurements and every death was correct.


---

**CORRECTION 2026-09-25 22:40 ET — the ring pointers above.**
The script COUNT word is at 0x1A70C and the (slot, ring pointer) pairs at
0x1A70E. 0x1A70E is NOT slot 19's ring: the rings are at 0x01A720 (slot 19,
7 steps) and 0x01A7A0 (slots 20 AND 21, 6 steps, the same pointer). Verified
byte-for-byte by tools/ring_script.py against altbeast US set 8. A pointer
table is not the thing it points at. CORRECTED 2026-09-25 22:40.


---

## CORRECTION 2026-09-26 13:00 ET — THE 23:30 "CORRECTION" ABOVE WAS ITSELF WRONG

The ring addresses in the ORIGINAL derivation (script 0x1A6FA, rings 0x01A70E
and 0x01A78E) were RIGHT. Last night I "corrected" them to 0x1A70C / 0x01A720 /
0x01A7A0 from an image I had built myself out of `mame/altbeast.zip` — and that
image was **altbeast4, set 4. The port ships SET 8** (the parent pair
epr-11907.a7 high + epr-11906.a5 low), which is what `tools/patch_game.py`
targets and what `roms/altbeast/prog68k.bin` has held all along.

    set 8 (SHIPPED)   script 0x01A6FA   rings 0x01A70E (s19)  0x01A78E (s20/21)
    set 4 (my error)  script 0x01A70C   rings 0x01A720        0x01A7A0

Set 4 carries the same cycler data shifted by exactly 0x12 — ONE RING STEP — so
the wrong set yields the RIGHT pen demand at the WRONG addresses and nothing
complains. Verified field by field: `steps`, `cls`, `c0` and `seqw` are
identical between the two sets, and only the informational `ring` address
differs. **So ring1.32x and its tables are correct** — the emitted bake is
unaffected and no rebuild is needed.

What was actually broken was PROVENANCE, in the build path: `ring_script.py`'s
`program_from_zip` iterated members in sorted order and returned altbeast4
before the parent, and both it and `bake_tilecram.py` defaulted to set 4's
0x1A70C. Consistent with each other, and wrong about the shipped rom. Fixed
2026-09-26: the tool defaults to `roms/<game>/prog68k.bin` (the image the rest
of the toolchain already uses), `--zip` prefers the parent set, and both default
addresses are 0x1A6FA.

I had a verification available and did not use it: `roms/altbeast/prog68k.bin`
has existed since 2026-08-20 and one `cmp` would have caught this before the
"correction" was written.
