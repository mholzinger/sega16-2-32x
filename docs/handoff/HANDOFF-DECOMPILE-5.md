# HANDOFF — to decompile thread 5 (2026-09-25, 21:00 ET)

From the thread that wrote `HANDOFF-DECOMPILE-4.md`. Read that file too: its
sections 1-3 and 5 stand. **Its section 4 pen-map entry is SUPERSEDED by
section 3 below** — the table it calls byte-identical may be stale in every
build, which is the builder's own finding at 20:40 and it lands squarely on
the conclusion I wrote.

---

## 1. The builder thread is ALIVE and productive — do not restart it

Mike asked at 20:55 whether a new builder thread was needed. Verified: no.
The fingerprint is the commit trailer — **`Co-Authored-By: Claude Fable 5.1`
is the builder, `Claude Opus 5 (1M context)` is this decompile thread.** Two
builder commits landed AFTER the last relay, both of them the relay's own
action items:

    5c24f118  20:11  nt1 rig 3/3 + the speed rank (both asked for at 19:45)
    61c32a62  20:51  the allocator watch read, and the staleness retraction

Use that trailer check instead of asking. `git log --since='2 hours ago'
--format='%h %ad %s' --date=format:'%H:%M'` plus the trailer tells you which
thread did what and when.

## 2. nt1 PASSED EVERYTHING IT WAS SENT FOR — the line question is open

`rom/night/nt1.32x` (2026-09-25 19:12, stamp c852403d R60, normal) = **wc9
minus exactly three flags: TAGKEEP, PENHOLD, PENREPAINT.** Ancestry checked,
not assumed: nt1's source carries defect 1's cset+plane hash fix `5c8c3d5a`.

    play pass    Mike: "looks great no regressions. still things to fix, like
                 errant tiles. but no regressions." Separately: "my first
                 impression is that it runs smoother... this feels better."
    picture gate rig 3 launches, 60 shots: 0 purple band, 0 grey level start,
                 0 black band cells (LESSONS 20:15).
    speed        light barcode RIGBCEVERY=8, RIGSPAN=4, control wc9
                 interleaved in the SAME session, two launches each:
                 ~vint 2850  wc9 6,6   nt1 4,7   no separation
                 ~vint 3290  wc9 14,13 nt1 9,11  NO OVERLAP
                 ~26% fewer 2+vint passes at the later sample. span-0 is FLAT,
                 so the passes that stopped spanning two vints became span-1.
                 The builder's own words: Mike's impression is SUPPORTED, NOT
                 PROVEN -- one sample separates, one does not, two launches
                 each. **It is not a ranking. Do not read a line-move out of
                 it on its own.**

**THE OPEN QUESTION FOR MIKE, and it is the top one:** nt1 has now passed the
play pass, the picture gate and 3 rig launches. rg2 (the recorded LINE) still
has 0 launches. STATE records nt1 as a passed play pass and explicitly does
NOT name it the line (commit 3d516049) because the standing rule is to name
the line when a build passes ITS GATES and I would not declare it on a
supported-not-proven speed read. That call is Mike's and it has not been put
to him in those words. Put it to him.

Why removing the family costs nothing (Makefile:2298-2321): the three flags
are one chain, each link propping up the one above. TAGKEEP defers the
md_tag wipe on the bet that the re-assign lands the set back where it was;
PENHOLD keeps the freed pens reserved so that bet CAN land (4 of 16 without
it); PENREPAINT repaints a held pen whose colour faded so the placement is
not rejected. The bet never pays -- `mdalloc[22]=0` resolved-identical,
`[23]=26` moved, 100% moved over two independent windows. It was deferring a
wipe that is always needed, and the other two rescued the rescue. Dropping
all three also drops a known pen leak (`PEN_HOLD@2622`, no release timeout).

## 3. THE PEN MAP: I QUOTED A TABLE WITHOUT CHECKING ITS VALIDITY FIELD

This is the correction that matters and it is against me, not the builder.

`HANDOFF-DECOMPILE-4` section 4 says five configurations give one
byte-identical pen table, and concludes "the cut's sets are assigned ONCE
while the ring is uniform and NOTHING re-assigns them." **That conclusion is
not established.** At 20:40 the builder read, on the family-off build aligned
to its own cut:

    mdp_s_used[19] = mdp_s_used[20] = mdp_s_used[21] = 0
    while mdp_s_map[19] still reads [14,7,7,7,7,7,7,7] and mdp_s_line[19] is set

`used = 0` means no pixel currently holds a pen, so those map bytes are
LEFTOVER from an earlier assignment. The table is consistent across all five
builds **because it is stale in all five**, which is a completely different
statement from "frozen". The live alternative, in the builder's words: a set
freed and re-assigned repeatedly paints from stale `mdp_s_qc` in between, and
that is CHURN, not a freeze.

Also from that run, and worth having: the watch instrument cannot see the
claim at all -- `mdalloc_id`'s capture lives inside `mdp_free_set` and fires
only for the watched set, which is not freed there. Counters over the run:
hit 2,171,772; claim-into-a-free-way 5,988; evict 803; young-victim refusal
984; free_set 8; free refused by the pin 7; drift free 2. **Frees are rare.**

NEXT READS, the builder's, in order, and none is a fix:
  1. sample `mdp_s_used` + `mdp_s_map` + `mdp_s_qc` for 19/20/21 on EVERY
     frame of the cut window, not one frame. Frozen vs churning.
  2. add a claim-time capture under `MDA_WATCH` (in `mdp_assign_set` /
     `mdp_claim_pen`) so the claim is observable at all.

CHEVPEN (c) instrumentation is NO LONGER the named next read -- it was
predicated on the freeze. Do not send it until (1) settles.

**MY HYPOTHESIS, unmeasured, and it SURVIVES the staleness finding — which
is the one reason to keep it on the table:** the installer at `0x1A6BA`
writes the slot table at scene setup; the cycler at `0x30B2` steps it from
IRQ4 (slot 19 on timer 1, every step). Ring step 0 is already 7 distinct
blues, so any uniform-ring window is the BASE PALETTE LOAD, before the
cycler's first write -- one or two vints. The ring script at `0x1A720` is
static ROM data: 7 steps x 8 colours enumerates every colour set 19 will
EVER hold. So the correct pen demand is derivable at scene setup, with no
observation of live palette RAM and no re-claim. That holds whether the set
is frozen or churning, because it never needs to read the field at all.
It is a hypothesis. Label it as one when you relay it.

## 4. What else is LIVE

    errant tiles      Mike named these on nt1 and they are NOT IDENTIFIED
                      against a known defect. Do not assume O-6 (leftover
                      text glyphs). Locate them in the arcade corpus and say
                      which layer and set before proposing a mechanism --
                      that rule exists because pattern-matching a known
                      defect name onto an unlocated visual has misfired here
                      before (the 09-17 silhouette-blob objection, withdrawn:
                      they were legitimate enemy sprites).
    the palette rotor 41 lines a vint, the largest item on the 68K axis and
                      the only one above the rig's ~15-line floor. LIVE on
                      Mike's 2026-09-25 call, which discarded the 2026-08-16
                      priority order: *"anything I said six weeks ago doesn't
                      even match our architecture now ... that's disposable."*
    O-14              move the cycler to the SH-2. Ask #1 still unanswered:
                      what share of the rotor's 41 lines is slots 19/20/21?
                      If most, O-14 subsumes O-13.
    SH-2 cyc/instr    the last assumed number in PORT-CENSUS; builder agreed
                      to measure it on the rig like CARTBENCH.
    the stack         thread 3's arithmetic (BURSTGATE + mid-pass push ~8
                      lines, under the ~15-line floor once WRAMCODE is
                      already on the candidates) is an INFERENCE and still
                      unchecked. Check before a build is spent on it.

## 5. Rules, and the two this thread earned

Everything in `HANDOFF-DECOMPILE-4` section 5 stands: grep FACTS.md first,
read the implementing file for a feature question, never assert a negative
without the grep, verify every rom/flag/attribution before Mike pastes it,
quote him or do not attribute, and a measurement beats an argument including
yours. Added tonight, both from real cost:

  - **Read the VALIDITY field before the data field.** `mdp_s_used` says
    which `mdp_s_map` bytes mean anything. I built a five-configuration
    argument on bytes that `used=0` says were leftovers. The builder found
    it, not me.
  - **Check the emitter for what a counter byte carries before reporting
    it, and filter rig pulls by DATE.** RIGSPAN mode 4's bytes are span>=2
    count/2 and span-0 count/2 (`md_main.c:967`), not one-vint/three-vint;
    and the rig keeps older shots under the same rom name, so a stale
    session silently joins the sample.


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
