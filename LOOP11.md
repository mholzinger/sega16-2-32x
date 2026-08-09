# LOOP 11 — THE MK2 PIVOT: hold FM, compose direct, delete the blit

Kickoff doc for a FRESH session. Read this, then LOOP10.md's result
sections, then LOOP.md's negatives list. This is an ARCHITECTURE change,
not another optimisation — LOOP 10 ended with Mike saying the port is
"looking better while sacrificing playability" and "framerate in the first
half is kind of ridiculous", and with four separate micro-optimisations
killed by their own falsifiers. The box is full. Stop adding to it.

## FIRST, THE NUMBER THAT JUSTIFIES ANY OF THIS

Write it down once so nobody re-litigates it. We did not replace a 10 MHz
68000 with 23 MHz SH-2s — we replaced DEDICATED RASTER HARDWARE with
software. System 16 has a tilemap generator and a sprite chip that draw
the screen free, in hardware, every frame. The 32X has a framebuffer, and
every pixel is a CPU store.

    32X framebuffer write bandwidth (measured)  6.76 MB/s
    one pass over a 320x224 8bpp screen         71,680 bytes
    that pass at 60 Hz                          4.30 MB/s

**The entire framebuffer write port is ~1.6 passes over the screen per
frame at 60 Hz.** The blit alone is one full pass. That leaves 0.6 passes
for BG, FG, priority tiles, text and sprites composited with priority —
not enough for ONE layer. The 20 Hz cadence is not a design failure; it
is that number. Any proposal that does not change the number of screen
passes cannot change the framerate.

## THE ANSWER — Knuckles' Chaotix, disassembled. COPY THIS PROTOCOL.

**Three commercial titles keep a BUSY 68000 alongside SH-2 rendering:
Knuckles' Chaotix, Virtua Racing Deluxe, Virtua Fighter.** In VRDX and VF
the 68000 even owns the FBCTL frame flip. Every other title in the library
is the MK2 model — FM set once by the SH-2, 68000 reduced to input+sound.
So a busy 68000 is NOT the thing that forces our 200-scanline handoff.

Chaotix runs a full Sonic engine, the MD VDP tile layers, MD sprite/scroll
DMA, AND arbitrates the 32X framebuffer, every frame. Its FM window is a
few HUNDRED 68000 cycles. Ours is ~200 scanlines ≈ 97,000 cycles. That is
a factor of ~300, and it comes from five decisions, all copyable:

**1. BULK DATA NEVER CROSSES THE FRAMEBUFFER.** Chaotix's entire per-frame
sprite/display list goes 68000 -> DREQ FIFO (`$A15112`) -> SH-2 DMAC ch0
-> SDRAM. The SH-2's CMD interrupt handler does NOT copy anything; it
programs SAR0=`0x20004012`, DAR0=SDRAM, TCR0=length, CHCR0=`0x44E1`,
DMAOR=1, and returns. The DMA drains in the background at zero CPU cost.
VRDX does the same at 0x500 words/frame. **This is exactly the LOOP 11
step-2 path, and it is what every busy-68000 title uses for staging.**

**2. PALETTE IS A CHANGE QUEUE, NOT A BLIT.** The 68000 accumulates
`(CRAM offset, colour)` pairs in MD RAM at `$FFFFD860` during the frame,
then drains only those under FM=0. The window is ~40-60 cycles PER CHANGED
COLOUR — a few hundred cycles typical, ~4 scanlines even at 32 changes,
against ~31 scanlines for a full CRAM write. We already stream palette
region PAIRS (256 words) whether or not they changed; LOOP 10 narrowed the
GENERATION bump to changed sets but still ships the whole pair.

**3. COMM0==0 IS AN IDLE TOKEN, NOT A REQUEST/GRANT.** The SH-2's idle
loop zeroes COMM0, which publishes "I am parked in a loop touching ONLY
COMM0 — nothing in the FB/VDP/CRAM aperture." The 68000 tests it and takes
FM UNILATERALLY. No round trip, no ack, no negotiation latency. The only
guard is that the SH-2 reads COMM0 twice and requires the two reads to
match before acting on a command. **Our protocol is the opposite: the MD
raises FM, signals, and SPINS for the ack. That spin IS the window/ack
span.**

**4. THE 68000 POLLS AND SKIPS; IT DOES NOT WAIT.** The per-frame path is
`tst.w $A15120 / beq take-it / rts` — if the SH-2 is busy the 68000
RETURNS and retries next frame. A missed palette update costs one frame of
stale colours; a blocking wait costs a frame of Sonic physics. The
blocking `bne`-spin form exists only in mode-setup and level-load code.

**5. THE SH-2 IS ALLOWED TO STALL.** Chaotix's V-int handler writes CRAM
and will simply block if it collides with a 68000 FM window. That is fine
BECAUSE the windows are microseconds. Design for "the SH-2 occasionally
eats a short stall", not for "the SH-2 must never be blocked."

Also worth copying verbatim: FM is toggled with BYTE writes to the high
byte of `$A15100` only, so ADEN and nRES are structurally unreachable from
the arbitration code.

### WHAT THIS MEANS FOR THE PIVOT

**The FM-hold pivot is the wrong target.** We do not need to hold FM
forever; we need the window to be microseconds and NON-BLOCKING. That is a
far smaller change than eliminating the handoff, and it does not require
solving the tile-RAM read-back problem first.

Ordered by (value / risk):
  a. **Invert the handshake** to the idle-token + poll-and-skip form. Our
     68K currently spins for the ack; Chaotix's never does. This alone
     targets the 200-scanline window/ack directly.
     **FIRST ATTEMPT BUILT AND REJECTED — `make IDLETOKEN=1`, kept behind
     the flag with the bug intact so the next attempt starts from the
     diagnosis rather than from scratch.**

         parity  title 66.39% (dx=+24)  scream 73.62%  demo2 51.87%
                 TOTAL 45.12%  against a 24.26% reference

     THE BUG, and it is a design flaw not a typo: the token is published
     READY only at the END of a band-phase step or a build_maps chunk
     (m_main.c:2030, :2149). **When the band queue is EMPTY the master
     never enters those branches, so the token stays BUSY forever** after
     the first ack at :2785. The MD then skips 3 of every 4 windows and
     only takes one via the starvation guard.
     THE FIX: readiness is a property of "nothing in flight", not "work
     just finished". Publish READY on every path that reaches the poll
     with no outstanding step — including the do-nothing path — and BUSY
     only immediately before a heavy call. Every `continue` in that
     branch is a path that must be covered.
     LESSON, same shape as the miss-skip trap: I reasoned about the
     states where work HAPPENS and never asked what the token reads when
     there is no work at all. Enumerate the idle path first.

     **SECOND ATTEMPT — THE PUBLISH BUG IS FIXED AND MEASURED. The
     handshake now behaves; on MAME it COSTS parity, and MAME cannot see
     what it is meant to buy. ares must rank it.**
     ONE publish point, at the top of the no-window branch — the poll
     itself — so every path through it, `continue` included, is covered
     by construction; TOK(0) immediately before each heavy call and never
     cleared, because the next poll visit clears it.

         idle-token skips  4043/5392 vints (75%)  ->  546/5392 (10.1%)
         parity  title 2.44  scream 65.82  eyehold 3.06
                 demo 48.24  demo2 51.80   TOTAL 34.27
                 (attempt (a) 45.12, reference 24.26 — title static is
                  back to exact; the dynamic scenes carry the whole gap)

     75% -> 10% is the diagnosis confirmed: the token now reads READY at
     the poll instant nine times in ten. The residual 10% is the master
     genuinely mid-strip, which is what the protocol is supposed to skip.
     THE COST IS REAL AND IT IS THE SKIPS: 546 skipped windows is 546
     deferred blit phases, the scene runs late against the anchor, and
     scream/demo2 move 47->66 and 19->52. **On MAME that is pure loss —
     its window is ~18 lines (LOOP.md iter6), so there are no held-FM
     scanlines to hand back. The entire payoff is the ~79 lines of
     not-yet-started FM that only ares holds.** This is the "MAME cannot
     see the terms that matter" lesson arriving on schedule; do not read
     34.27 as a verdict, and do not try to tune it on MAME.
     ares A/B pair, both PRESSURE-stamped, same commit:
         rom/PROBE_idletok2.32x       IDLETOKEN=1 PRESSURE=1
         rom/PROBE_base_pressure.32x  PRESSURE=1
     rom/s16.32x is left as the clean shipping build (re-measured after
     all of this: 24.26 / title 2.44 / eyehold 3.37, _end 0x06018d70).
     **ares, PRESSURE build, save slot 1: "extremely choppy" but "fast
     though — so we have 1 win".** Speed is the thing the pivot is for,
     and it moved. Chop is the 546 skipped windows: each one defers a
     blit phase, so the 3-window cycle intermittently becomes 4 and the
     cadence jumps 20 -> 15 Hz. NOTE: that build was PRESSURE-stamped,
     which is a MAME-side proxy (it WIDENS the quiet zone to imitate the
     ares operating point) and should never have gone to ares — it is a
     deliberate handicap there, so "fast" was measured against a
     headwind. All ares ROMs below are stamped `normal`.

     **THE DIAL — `make IDLEGRACE=1`. Best of the three on MAME, and it
     beats the shipping build.** Before skipping, poll COMM4 for as long
     as the flip stays LEGAL — V<=0xE2, the vblank gate's own bound — so
     a grace poll can never produce an illegal flip, and FM stays 0
     throughout (68K time, not held FM, which is the whole distinction
     the Chaotix protocol rests on).

         build          skips/5392   parity TOTAL   title  eyehold
         base                    0         24.26     2.44     3.37
         IDLETOKEN=1           546         34.27     2.44     3.06
         IDLEGRACE=1           492       **23.14**   2.44     3.37

     **THE SKIP COUNT IS NOT WHAT DRIVES PARITY.** 546 -> 492 is a 10%
     change in skips and an 11-point change in parity; both statics
     return to exact. So the damage in the pure-skip form is not the
     dropped windows themselves but WHEN the window gets taken, and that
     mechanism is NOT yet explained — do not build on a theory of it
     until someone measures it. Recorded as an open question, not a
     result.
     Grace is nearly a no-op on skip count for a structural reason worth
     knowing: the poll runs after the V gate has already put us at
     V=0xDF..0xE2, so there are only ~3 scanlines of legal grace left,
     while a strip is 6-22. It almost never succeeds in WAITING — yet it
     still wins. Another reason to distrust the skip-count story.

     ares A/B, three ROMs, same commit, all stamped `normal`:
         rom/ARES_base.32x       rom/ARES_idletok.32x
         rom/ARES_idlegrace.32x
     TRAP FOR THE A/B: an ares savestate contains SDRAM, and RAMCODE
     lives in SDRAM — so a state saved under one build carries THAT
     BUILD'S CODE into whatever ROM you load it against. Make a fresh
     state per build or the comparison is measuring one binary three
     times.

  a-REOPENED. **The "DEAD" verdict below was drawn from samples too
     short to contain the tail, and the tail is the whole point. Read
     the correction at the end of this block before acting on it.**

  a-DEAD (SUPERSEDED). **The premise is stale and the falsifier is
     unambiguous: the FM window is not ack-bound, it is BLIT-bound, and
     the idle token does not move it by one line.**
     `tools/state_health.py` on two ares states — s16.bs1 (baseline,
     BUILD 95d17bcf) and ARES_idlegrace.bs2 (BUILD 0faba162):

                             baseline 95d17bcf   IDLEGRACE 0faba162
         blit windows                    5603                  1200
         MEAN window span           35.6 lines            35.6 lines
           of which blit            28.4 lines            28.5 lines
           of which cram             3.5 lines             3.3 lines
         worst handler total              157                   156
           window/ack                     110                   112
         vints/cycle                     3.03                  3.22

     Mean window span identical to 0.1 lines across 5603 vs 1200
     windows — that is a per-window mean, so it is not a sample-size
     artifact — and the worst case is identical too. **80% of the window
     is the blit doing real work.** There is no ack latency left to
     reclaim, so no handshake change can help: the only thing the idle
     token produces is its cost, vints/cycle 3.03 -> 3.22.

     **THE ~200-LINE WINDOW AND THE ~79-LINE PREACK STALL AT THE TOP OF
     THIS DOC ARE STALE.** They come from b6620f4, and the ares baseline
     block below still quotes "worst handler 253 of 262 (margin 9)". The
     CURRENT baseline is 157 total / 110 window / margin 105. Something
     between b6620f4 and 95d17bcf already fixed it. Every argument in
     part (a) was built on a number nobody re-measured — including the
     one that ranked this above the architecture work.

     Corollary, and it points straight back at the main plan: the window
     is 28.5 lines of blit. **The blit is the target, and the only thing
     that shrinks it is drawing fewer pixels** — which is what moving BG
     and FG cat-0 to the MD VDP does. Nothing in the handshake family
     can compete with that.

     ALSO NOTE, for whoever reads Mike's play reports: s16.bs1 is BUILD
     95d17bcf, i.e. the "extremely choppy but fast" pass was on the OLD
     SHIPPING BUILD, not on any idle-token rom. It was never a verdict
     on this work. Read the BUILD line out of the state before believing
     a play report is about the thing you just built.

     KEPT, not reverted: IDLETOKEN and IDLEGRACE stay behind their flags
     with these results attached, so nobody re-derives them. Neither is
     in the shipping rom (`rom/s16.32x` = base, re-measured 24.26).

     ---- CORRECTION, same day, from a LONG ares session ----

     **The "DEAD" verdict above is wrong, and the way it is wrong is the
     exact failure LOOP 10 warned about: JUDGE AGAINST THE RANGE.** It
     rested on two ares states of 403 and 1889 cycles. A 3709-cycle
     baseline state (`rom/s16.bs1`, BUILD 0faba162, vints/cycle 3.03 so
     it IS the base build) says:

                          MEAN window   worst window/ack   sample
         morning states    35.6 lines        110-112       403 / 1889 cy
         long session      38.2 lines      **210, margin 8**   3709 cy

     Both readings are correct. **The MEAN is blit-bound — 28.8 of 38.2
     lines — and no handshake change can touch it. The TAIL is 210
     lines with 8 lines of margin, it is 5.5x the mean, and the excess
     is NOT blit; it is the ack-spin.** The ~200-line window in this
     doc's header was never stale. My samples were too short to contain
     it, and I retracted a true statement.

     The same state also reproduces the documented baseline that the
     short samples contradicted: blit skips 26.6% (ref 31.5%),
     dreq_incomplete 21.0% (ref 21.4%), restore past vblank 0.7% (ref
     0.8%). The morning figures of 0.7% dreq_incomplete were an
     artifact of sample length. **Treat every ares comparison made on
     under ~3000 cycles as unreliable, including all of mine from that
     round.**

     WHAT THIS MEANS: the idle token targets the TAIL, not the mean, and
     the tail is what a player feels as a hitch. Part (a) is reopened,
     NOT resurrected — it still has to prove itself on a long-sample A/B.
     THE MEASUREMENT THAT SETTLES IT: play `rom/ARES_idlegrace.32x` for
     ~3 minutes, save a state, and compare worst window/ack against the
     base state's 210 / margin 8. Anything shorter cannot answer it.

     ---- THAT A/B WAS RUN. PART (a) IS CLOSED. ----

     Mike, on `rom/ARES_idlegrace.32x`: **"broken the second the game
     starts."** The state agrees, and the acceptance gate outranks every
     metric above:

                        vints/cycle   idle-skips   windows/vint
         base (ares)        3.03          0            0.991
         IDLEGRACE (ares)   7.48         20.5%         0.793

     Cadence collapsed from 20 Hz to roughly 8 Hz.

     **THE STRUCTURAL REASON, and it kills the whole family — not this
     implementation of it. Chaotix's protocol assumes an SH-2 that is
     PARKED most of the time. Ours is SATURATED.** The idle token is
     only free when "is the master busy?" is usually NO, because then
     the 68000 takes FM unilaterally and never waits. Our master is
     rendering every layer of the screen in software; at the poll
     instant it is genuinely mid-strip, so the MD either waits (burning
     68000 time it did not have) or skips (dropping a blit phase). Both
     lose, and the starvation cap turns a busy master into a 3-in-4
     window drop.

     MAME hid this exactly backwards from the usual direction: its SH-2
     is ~3x faster, so the master LOOKED parked (10% skips) and the
     protocol looked viable. On ares it is 20.5%, and each skip costs a
     whole window. **A protocol whose premise is "the master is usually
     idle" cannot be ported to a master that is the bottleneck.** No
     amount of publish-point fixing or grace-window tuning changes that
     premise.

     So the FIRST "DEAD" verdict reached the right answer for the wrong
     reason (it argued from the mean, which was never the target), and
     the correction that reopened it was right about the tail and still
     wrong about the conclusion. The tail IS ack-bound at 210 lines with
     8 lines of margin — that part stands and still needs an answer —
     but poll-and-skip is not it.

     WHAT SURVIVES FOR WHOEVER PICKS UP THE TAIL PROBLEM: the 210-line
     worst-case window is real and the margin is 8 lines. Any fix has to
     work while the master is BUSY, because that is our steady state.
     That rules out anything Chaotix-shaped and points at the two
     remaining levers in this doc — DREQ+DMAC staging (b) and the
     palette change queue (c) — which reduce what crosses the boundary
     rather than renegotiating when it crosses.

     IDLETOKEN=1 and IDLEGRACE=1 stay behind their flags, **NEVER SHIP**,
     with this result attached so the family is not re-attempted.

     TWO TRAPS THIS COST, both of which invalidate earlier numbers:
       - **0xFFB0FA WAS NEVER THE SKIP COUNTER.** 0xFFB0F8 is a LONG —
         the interrupted game PC, md_start.s:254 — so it owns 0xFFB0FA
         and rewrites it every vint. Attempt (a)'s counter read pure
         garbage (14726, 55050, 14722...). Moved to 0xFFB0EE. Check the
         WIDTH of the neighbours before claiming a free diag word.
       - **`make FLAG=1` DID NOT REBUILD UNCHANGED OBJECTS.** Objects
         depended on sources, never on the flag set, so a flag build
         right after a plain build silently kept half the old semantics.
         This produced a run that scored a perfect 24.26 with the SH-2
         publishing tokens and md_main.o containing no poll at all — it
         WAS the baseline, and I nearly wrote it up as a win. Fixed with
         a .build_flags stamp every object depends on (Makefile). **Any
         flag-build measurement taken before this commit should be
         re-measured before it is believed.**
  b. **Move tile staging to DREQ+DMAC** (LOOP 11 step 2, unchanged) — now
     with a proven reference implementation for the SH-2 side.
  c. **Palette as a change queue** rather than whole region pairs.

NOTE FOR THE DREQ TRUNCATION (21% short lands, cause still unknown): VRDX
pushes 0x500 words/frame through the same FIFO. Our packet is ~594 words.
Volume is NOT the problem. Chaotix waits on the CMD-accepted bit
(`btst #0,$A15103`) BEFORE pushing, and polls FIFO-full (`$A15107` bit 7)
between every 4-word group. Compare against our push loop.

BONUS: Chaotix repurposes the unused tail of the packed-pixel line table
(`$8401F0-$8401FF`, lines 248-255, never displayed at 224) as a 16-byte
bidirectional mailbox — free shared state beyond COMM0-7.

## THE ARCHITECTURE QUESTION, WHICH RANKS BESIDE THE ABOVE

Mike, on reading the above: "this is way faster hardware than the arcade,
so much of this port would come for free." He is right about the thing
that matters — **we software-render tilemaps that the MD VDP could draw
in hardware, with hardware scrolling, at 60 Hz, for zero CPU.** The Mega
Drive shipped an official Altered Beast; the MD alone can draw these
backgrounds. The 32X should be ADDING to that, not redrawing everything.

That is Knuckles' Chaotix's architecture: MD VDP for the tile planes, 32X
for sprites and colour. It is the only shape in the commercial library
that matches our constraints, and it changes the pass count — which per
the number above is the only thing that can.

### jtcores SPEC — VERDICT: GO, BUT THE FG LAYER MUST SPLIT ACROSS CHIPS

Full capability table derived from the Verilog (see the commit that added
this section). The headline results:

**WHAT MOVES:** the two scroll layers, and only those. Every structural
feature maps — two planes, 8x8 tiles, per-tile palette select, per-tile
priority bit, whole-screen H/V scroll. Row scroll is per-8-line on S16
(`jts16_mmr.v:67-68`, enabled by bit 15 of the H-scroll reg — our port's
belief CONFIRMED) and the MD does per-LINE, so MD is strictly better.
Column scroll is per-16px on both — an exact match. **MEASURED: Altered
Beast never enables row or column scroll in 700 samples**, so round 1
needs neither. Pattern residency fits: peak 599 scroll + 96 text distinct
patterns is ~22 KB at 4bpp, plus 16 KB of name tables, inside 64 KB VRAM.

**WHAT CANNOT MOVE:** sprites, categorically, on three independent hard
stops — hardware zoom (shrink to ~0.5 in 32 steps, `jts16_obj_draw.v:70`,
which the werewolf transformation rides on), ragged variable-width strips
up to full-screen size (`jts16_obj_draw.v:107`), and the MD's 20-sprite /
320-pixel-per-line ceiling. Not a fidelity argument, an expressibility one.

**THE REAL BLOCKER IS PRIORITY, NOT COLOUR.** The 32X composites against
MD video across exactly ONE boundary: per-pixel transparency plus a single
global 32X-vs-MD selector. System 16 interleaves them ten deep:

    T1 > S3 > T0 > F1 > S2 > F0 > B1 > S1 > B0 > S0      (jts16_prio.v:84-95)

Sprites in the 32X framebuffer with both planes on the MD means every
sprite is in front of everything or behind everything. The player would
stop being occluded by foreground scenery.

**THE ESCAPE, AND IT IS AFFORDABLE — MEASURED:** keep the FG layer's
priority (cat-1) tiles in the 32X framebuffer, painted OVER the sprites,
and let the MD draw only FG cat-0 plus the whole BG. In demo gameplay the
FG carries 307-340 priority tiles of 1189 visible (~26% of the screen);
the BG carries ~4 on average. **So the residual 32X pass is ~0.26 of a
screen against our 1.6-pass budget, leaving ~1.3 passes for sprites.**
That is the whole point of the exercise, and it fits.

**COLOUR IS A GRIND, NOT A WALL — and the usual framing is wrong.** Not
"128 sets vs 4". MEASURED peak demand is **21 distinct tile colour sets on
screen at once** (12 FG + 16 BG, union 21) against an MD capacity of 8 —
because S16 sets are 8 colours and MD palettes are 16, so one MD palette
holds one FG/text set (pens 0-7, colour 0 transparent) AND one BG set
(pens 8-15, since **BG colour 0 is OPAQUE** — `jts16_prio.v:87`, a real
semantic difference). **2.6x, not 32x.** But it is a different 21 every
scene, so it needs a per-scene 21->8 merge, not a static allocation. Plus
5bpp -> 3bpp per channel on the moved layers: banding in the sky and cave
gradients. The 32X framebuffer is 5-5-5, identical to S16, so the loss is
confined to whatever moves.

**TWO PORT BELIEFS CONFIRMED AGAINST THE RTL** — both load-bearing in
compose and both previously unverified:
  - the priority model (BG cat0=1, BG cat1=2, FG cat0=2, FG cat1=4, text
    cat0=4, text cat1=8; sprite draws iff `1<<pp > level`) is EXACTLY
    equivalent to `jts16_prio.v:84-95`. Nuance for the comments: a sprite
    blocked by a priority tile at one layer can still win at a HIGHER
    slot; the scalar-max formulation reproduces that correctly.
  - shadow is `shadow & ~pal[15]` — `jts16_colmix.v:88`, line for line.
    MEASURED: Altered Beast sets bit 15 on 89-119 of 1024 tile entries at
    any moment, so it is not a dead feature.

**TWO UNRESOLVED CONTRADICTIONS**, flagged rather than guessed:
  - Is H scroll suppressed while column scroll is active? The hardware
    notes say yes, jtcores says no (`jts16_scr.v:93`). Unresolved.
  - Shadow arithmetic: jtcores does x0.75 with NO highlight path
    (`jts16_colmix.v:80-89`); the notes describe 1/2 with a highlight.
    We currently match jtcores/MAME. Revisit only if a real PCB disagrees.

VERDICT: **the plan works, but the FG layer splits across both chips
rather than moving wholesale.** If the split proves too complex, priority
— not palette — is what forces the retreat.

**HOLD THE FM PIVOT BELOW UNTIL THE CHAOTIX DISASSEMBLY LANDS.** If a
busy 68000 can drive MD VDP planes while the SH-2s do sprites, that is a
cheaper port than either the current architecture or the FM pivot — and
it generalises to the WHOLE S16 LIBRARY, which is the actual deliverable.
Every System 16 title has this same shape: two scrolling tile planes, a
text layer, a sprite chip, 128 colour sets. Solve it once.

# ---- BACKGROUND BELOW THIS LINE ----
# Everything above is the current plan. Everything below is how it was
# reached, INCLUDING A SUPERSEDED ONE. The FM-hold pivot in "THE PIVOT,
# IN ORDER" was the plan until the Chaotix disassembly showed we do not
# need to hold FM at all — only to make the window microseconds and
# non-blocking. Read it as history, not as instructions. (LOOP9.md became
# a trap exactly this way.) The sections that ARE still live down here:
# MK2 TECHNIQUES, GATES, THE ares BASELINE, and THE METHOD LESSONS.

## WHY: the blit is the tax, and MK2 proves it is optional

Mortal Kombat II (32X, 1995) — fully disassembled, see the technique list
at the bottom — **has no blit at all**. It waits for vblank, toggles FS,
then draws DIRECTLY into the buffer it just hid. No backbuffer, no copy.

We compose into `sbuf` in SDRAM and then copy it to the framebuffer. That
copy is ~56 lines of every 262-line frame with the 68K stalled, it is the
largest pre-ack term, and it is a MEASURED HARDWARE FLOOR — 0.744 lines
per blitted row, 6.76 MB/s, identical in every configuration ever tried,
and DMA is 1.77x SLOWER. It cannot be optimised. It can only be DELETED.

It also forces the 3-window pipeline, which is why a complete frame ships
every 3 vints — **20 Hz by design**, with ~30% of cycles skipping a third
on top. That is the framerate Mike is describing.

## THE BLOCKER, STATED EXACTLY (this is the whole problem)

MK2 sets `FM=1` once at boot and never gives the framebuffer back, because
its 68000 does no drawing. Ours cannot, and the reason is a hard resource
constraint, not a refactor:

**The 68K can only write to two places: MD work RAM (64 KB, and the arcade
game already needs most of it) and the 32X framebuffer via the 0x840000
window. It has NO access to 32X SDRAM at all.**

So the arcade game's video RAM was remapped into the framebuffer:

    game tile RAM  0x840000  ->  MD 0x852000   (FB staging)
    game palette             ->  MD 0x85F000   (FB staging)

(`md_src/md_main.c:21`, `:652-656`.) That is the same trick MK2 uses for
scratch — but for us it means the 68K needs `FM=0` during gameplay to land
its video writes, and the SH-2 needs `FM=1` to touch the framebuffer. Hence
the per-frame handoff, hence the window, hence the 68K stall, hence the
blit. Every cost in the port descends from this one mapping.

## THE PATH OUT — and the evidence it is viable

Get the game's video RAM out of the framebuffer, and FM never has to drop.

Two of the three pieces are **already streamed** and no longer need FB
staging at all:
  - the **sprite list**, over the DREQ FIFO into `SPR_LAND` (working)
  - the **palette**, as region pairs in the same DREQ packet (LOOP 10)

That leaves the **tilemap**. It looks like the hard one and it is not:

> "DIRTY-ONLY page sync (write-observer ring): copy at most 3 pending
> pages per k1 — **steady state is ZERO** (1988 design preloads rounds;
> tile writes happen at transitions)."   — `sh_src/m_main.c`, copy_pages

**The tilemap barely changes during gameplay.** The write-observer ring
that detects dirty pages already exists and already works. Streaming a
handful of dirty pages at scene transitions is a far smaller problem than
the 128 KB-per-frame blit it would let us delete.

## THE PIVOT, IN ORDER  *** SUPERSEDED — see WHAT THIS MEANS FOR THE
##                          PIVOT, above. Step 1's measurement is still
##                          good; steps 3-5 are the wrong target. ***

1. **PREMISE VERIFIED (MAME attract, `make TILERATE=1`, 1197 cycles):**

       cycles with ANY dirty tilemap page   4.2%
       pages pending per cycle              0.32
       pages copied per cycle               0.22

   **95.8% of cycles the tilemap does not change.** Streaming a fifth of
   a page per cycle to delete a 128 KB-per-frame blit is not a close call.
   **CONFIRMED ON ares GAMEPLAY** (952 cycles, the scroll-heavy first half
   of level 1 where Mike reports the worst framerate):

       cycles with ANY dirty page   3.8%
       pages copied per cycle       0.18

   LOWER than attract, not higher. The reason is obvious in hindsight and
   worth writing down: **System 16 scrolls with SCROLL REGISTERS, not by
   rewriting tile RAM.** The scroll-heavy stretch is precisely where the
   tilemap does NOT churn. GO.

   NOTE ON THE FIRST INSTRUMENT, which was WRONG. A MAME
   `install_write_tap` on the MD's 0x840000-0x85FFFF window reported ZERO
   writes — and a control tap on MD work RAM froze at exactly 155531
   across frames 600/1200/1800, proving taps stop firing once the game
   runs through the rebased 0x900000 bank window. The zero meant nothing.
   Always run the control before believing a zero.
2. **Move the tilemap to a streamed path.** SMALLER THAN IT LOOKS IN ONE
   WAY AND LARGER IN ANOTHER — read both before starting.

   SMALLER: **the interception already exists.** `patch_game.py` generates
   `md_src/tile_thunks.h`, 228 words of 68K installed at 0xFF5E00. Every
   tile-writing instruction in the arcade game was patched to call a thunk
   that ORs a dirty-page bit into 0xFFB9FE and then performs the write.
   That is how `pg_pending` is set today. We do not need to build write
   detection; we need to redirect where the writes LAND.

   LARGER, AND THIS IS THE OPEN DESIGN QUESTION: **the game READS its own
   tile RAM.** A write-only ring buffer streamed over DREQ is therefore
   not sufficient on its own — the 68K needs a READABLE tile RAM image,
   and it can only read MD work RAM (64 KB, already largely spoken for by
   the arcade game) or the framebuffer. `copy_pages` moves 13 pages at
   0x800 stride, so the image is tens of KB. THAT is the real problem to
   solve, and it was not visible when this doc was written.

   Do not start step 2 until this is answered:
     a. WHICH tile-RAM addresses does the game actually read back, and how
        many bytes do they span? If reads touch only a small subset, only
        that subset needs to stay readable and the rest can be write-only
        and streamed. The thunk generator in `tools/patch_game.py` already
        classifies the game's accesses — extend it to report READS.
     b. If the readable subset is small, the split is: readable window in
        MD RAM + write-only remainder streamed. If it is the whole image,
        the tilemap cannot leave the framebuffer and THE PIVOT NEEDS A
        DIFFERENT SHAPE — most likely arbitrating FM better rather than
        eliminating it, which is what the library survey is looking for.
3. **Relocate the two 68K FB READ-BACKS** — the collision `tst.w`s at
   `0x6936+` and the round-transition scratch in page 1 at `0x1B760`
   (LOOP.md iteration 1b). These are the last non-tilemap FB users.
4. **Hold FM=1 permanently.** The window collapses to a vint handshake
   with no stall: the 68K never waits on the SH-2 again.
5. **Compose straight into the hidden framebuffer.** Flip first, then
   draw into the buffer just hidden (MK2's order). The blit is deleted,
   `sbuf` is deleted, and the 3-window pipeline is no longer forced —
   the frame rate ceiling stops being 20 Hz.

NOTE ON THE 85.8% MEASUREMENT. LOOP 9 recorded "composing straight into
the FB: FM=0 writes land only 85.8% of the time, worse than a flat no."
**That was measured at FM=0**, which is the failure mode this pivot
removes. It is not evidence against step 5; it is evidence FOR step 4.
Re-run `make FMTEST=1` with FM held and confirm 100% before building on it.

## MK2 TECHNIQUES WORTH TAKING (all DISASSEMBLED from the 1995 ROM)

1. **Overwrite image at `0x24020000`** — the VDP discards any byte written
   as `0x00`, so transparency is free hardware: no mask, no compare, no
   branch per pixel. MK2 uses it for 37 of its 48 FB references and
   reserves CRAM index 0 permanently. **`compose_sprites` currently gates
   per pixel with uncached destination reads, and sprites are our largest
   remaining wait term (69.6% of it).** Highest-value item on this list.
2. **VDP auto-fill for clears** (`AFSA 0x4106`, `AFLEN 0x4105`,
   `AFDATA 0x4108`, poll `0x410B` bit 1). Zero CPU stores. **Re-arm every
   256 words — auto-fill wraps inside a 256-word block.** Never used here.
3. **35 KB of scratch inside the framebuffer** between the line table and
   the bitmap, physically separate from the 256 KB SDRAM. We are at
   **656 bytes** of region-guard headroom and have been rationing
   `.ramtext` for three loops.
4. **`GBR = 0x20004000`** makes every system register, all 8 COMM
   registers and the VDP block a one-instruction access with no literal
   pool. Same pressure, same relief.
5. **Stride wider than the screen** (MK2: 368 bytes against 320 pixels) so
   sprites hang off the edge with no clipping logic. Costs 10 KB.
6. **Toggle FS with `not` on the low byte of `FBCTL`** — bits 7-1 are
   read-only status, so complementing the byte is a clean toggle with no
   mask and no read-modify-write.

## WHAT MK2 GETS WRONG (do not copy)

It ships 668 bytes/frame through COMM in **67 two-phase handshakes**, with
the master SH-2 spinning inside its interrupt handler and the 68000
spinning in its loop, both blocked the whole time. It never uses the DREQ
FIFO or the SH-2 DMAC. **Our streaming path is already well ahead of the
commercial bar** — that is worth knowing before assuming MK2 is better at
everything.

## GATES (unchanged, and they have overruled the metrics repeatedly)

1. Boots + full attract, no hang.
2. `tools/parity_run.sh` on a CLEAN probe-free build. **Judge the STATICS**
   (title, eyehold). Reference at b6620f4: title 2.44, eyehold 3.37,
   scream 47.14, demo 48.98, demo2 19.35, TOTAL 24.26.
3. Region guard (`grep ' _end$' rom/s16.lst`, limit 0x06019000; currently
   0x06018d70, 656 bytes), rom stamped `normal`, `make PRESSURE=1` builds.
4. **PLAYABILITY — Mike's ares pass is the acceptance gate.**

## THE ares BASELINE THIS PIVOT MUST BEAT (b6620f4, 4365 cycles)

    vints/cycle 3.03    V-gate rejects 1.0%    blit skips 31.5%
    restore past vblank 0.8%   dreq_incomplete 21.4%
    worst handler 253 of 262 lines (margin 9)
    black frames 10.5% of a raw capture
    same-build run-to-run spread is WIDE: blit skips have read 23.5-43.5%
    on the same code. Judge against the RANGE, never one run.

## THE METHOD LESSONS FROM LOOP 10 — these cost the most time

- **BUILD THE FALSIFIER BEFORE THE THIRD FIX.** Three speculative fixes
  were written for a prescan-miss diagnosis that a single marker build
  disproved in one capture.
- **MAME CANNOT SEE THE TERMS THAT MATTER.** `flipwait` reads a flat
  0.000ms and `dreq_incomplete` reads 0 there. But `DIAG[7]` (V-gate
  rejects) DID rank a change correctly — it is a valid cheap pre-ares
  screen. Know which is which.
- **ASK WHAT HAPPENS ON EVERY CYCLE, not just one.** "Skip and leave last
  cycle's pixels" is fine once and catastrophic forever.
- **JUDGE AGAINST THE RANGE.** A single max from the longest run of a
  session is not a trend; it read as an 8-line crisis that 11800 windows
  then showed was a mean of 33.1 lines.

# ---- GREEN TEARING: DIAGNOSED. IT IS THE SAME ROOT AS THE 210-LINE TAIL ----

Mike, on the milestone build (`rom/s16.bs1`, base, 3709 cycles): "lots
of green tearing but more stable than any build we have produced."

## It is NOT the bank flip

Ruled out with the frame itself, pulled straight out of the ares state:
the 32X framebuffer at bank A renders correctly (stage 1 graveyard,
grey stone, green grass), and **bank B is all zeros**. The port paints
one bank only, so a missed restore shows BLACK, not green — which is
what NOTES.md recorded years ago and what the counter says now:
restore past vblank 67/10138 = **0.7%**, against 5.6% in LOOP 9. That
path is at its best ever and cannot account for "lots".

(Method note: the frame was recovered by reading the FB region out of
the .bs1 and colouring it with `cram_mirror`, which lives at a FIXED
SDRAM address, `0x06028900` — not in .bss, so it is in the state and
easy to find. This is a cheap and repeatable way to see exactly what
ares displayed. Bank A image sits at file offset ~0x062df0 in a
1010331-byte BST1 state; bank B is +0x20000.)

## It IS blit skips holding a stale band

    blit skips 988/3709 cycles = 26.6%

LOOP 10 already named this and the wording is exact: *"A skipped blit
is a third that does not ship — the screen holds a stale band for a
cycle. That is exactly tearing plus slowdown."* 26.6% sits inside the
documented same-build range (23.5–43.5%), so this build did not
regress; it is the standing defect.

**Why GREEN: the stale band is a horizontal third, and in stage 1 the
bottom third is grass.** A held-back bottom third against a moving
scene reads as a green tear line. The colour is a property of the
scene, not of a palette bug — the palette is fine, 229 live entries and
the frame renders true.

## The root cause is PICKUP LATENCY, and it is the 210-line tail

The skip test is `v < 0xDF || v > 0xE4` on the MD heartbeat V at
pickup. A skip means the master REACHED the window at the wrong
scanline. The master polls `MARS_SYS_COMM0` at the top of its main loop
(m_main.c:2011) — **there is no interrupt path for window pickup** — so
pickup cannot happen until the strip in flight finishes. A strip is
0.4–1.4 ms (6–22 scanlines) and build_maps is ~4 ms.

That is the same ~79-line term that makes the MD's worst window/ack 210
lines against the master's own 131. **The tearing and the 8-lines-of-
margin tail are ONE defect measured two ways.** The pre-vint quiet zone
(`dt > 11300 -> start nothing`) exists only to bound it, and it costs
throughput to do so.

## THE NEXT THING TO BUILD: interrupt-driven pickup

Make the master take the window on the 32X CMD interrupt instead of
discovering it at the next poll. Pickup latency stops being "however
long the current strip has left" and becomes interrupt latency.

**This is the one shape that satisfies the constraint part (a) died
on.** The idle token needed the master to be idle; ours never is. An
interrupt does not care that the master is busy — that is the entire
point of an interrupt. And it is what Chaotix actually does: its SH-2
services the 68000's command in the CMD interrupt handler and returns
immediately (see the protocol section at the top of this doc).

WHAT HAS TO BE SOLVED, and it is the real work: the ISR fires mid-strip,
so either the strip must be resumable, or the ISR does only the
FM-critical part (flip + blit + re-arm) and leaves compose to the main
loop. The second is closer to what the code already separates at the
early-ack point (m_main.c:2811).

FALSIFIER, cheap and decisive: blit skips must fall well below 26.6%
and worst window/ack well below 210 — on a LONG ares state (>3000
cycles; see the sample-length correction above, which cost a full round
of wrong conclusions). MAME cannot rank this: its master is ~3x faster,
so its pickup latency is small already.

## CMDPROBE — built, working, and waiting on one ares state

`make CMDPROBE=1`. The MD asserts CMD INT to the primary SH-2
(`move.w #0x0001,0xA15102`, d32xr src-md/crt0.s:3143) immediately
BEFORE posting COMM0; the master's `main_cmd_irq` — a stub until now —
timestamps the arrival into DIAG[58] and counts fires in DIAG[57]; the
main loop subtracts at pickup into DIAG[59] max / [60] sum / [61] n.
**The ISR does no work and pickup is still by polling, so rendering is
unchanged.** ~46 FRT ticks per scanline.

MAME (3600 frames): ISR fires 3590, **mean 0.4 -> 1.3 lines, max 23.6
-> 26.9 lines**, rising with load. Small, exactly as expected where the
master is ~3x faster — MAME cannot answer this question, it can only
prove the plumbing works. Which it now does.

**THE ARES RUN THAT DECIDES THE ISR REWRITE:** play
`rom/ARES_cmdprobe.32x` for ~3 minutes (>3000 cycles — anything shorter
cannot see the tail, see the sample-length correction above), save a
state, and read DIAG[59]/[60]/[61]. If the recovered budget is the
~79 lines the preack probe implied, the ISR restructure is justified.
If it is small, it is not, and that is a cheap no.

CAVEAT, stated rather than buried: the probe is NOT free. Parity moves
24.26 -> 26.62 (demo2 19.35 -> 31.60) from the extra per-vint MMIO
write and the ISR entry, so it slightly perturbs the thing it measures.
Good enough for a first-order "is there 79 lines here"; not a build to
compare parity against. NEVER SHIP.

THREE TRAPS THIS COST, all worth not repeating:
  - **`SHASFLAGS` is assigned with `=` BELOW the flag blocks**, so a
    `SHASFLAGS +=` written up there is silently discarded and the
    assembler conditional compiles to nothing. Assembler flags must be
    appended after the base assignment. Same shape as the flag-stamp
    bug: a flag that looks set and is not.
  - **gas needs `.ifdef`, not `#ifdef`** — `sh-elf-as` does not run cpp
    on `.s` files.
  - **ORDER: raise the interrupt BEFORE posting COMM0.** Raised after,
    the master had already polled and picked up the window before the
    68000 reached the write, so it read the PREVIOUS vint's stamp and
    every sample came out at ~one frame (229 lines mean). The bug was
    visible only because the number was absurd.

### CMDPROBE ON ares — THE ISR REWRITE IS JUSTIFIED, and the mean nearly hid it

`rom/ARES_cmdprobe.bs1`, 3708 cycles, 11125 sampled pickups:

    CMD ISR fires        11125  (one per vint, plumbing good)
    MEAN pickup latency  83 ticks = 1.8 lines
    MAX                  UNUSABLE — 279 lines, longer than a frame

**Read the mean alone and you kill the rewrite. That would be wrong.**
1.8 lines says "the master notices almost immediately", and for 88% of
windows it does. The defect lives in the other 12%, and the same state
measures it: `blit skips = 1342` against 11125 pickups = **12.1% of
pickups are late enough to miss the master's `v > 0xE4` accept bound**.

Decomposing the mean over those two populations (robust to the
assumption — prompt pickup 0.1/0.2/0.4 lines gives 14.2/13.5/12.0):

    88% of pickups   prompt, an interrupt would gain nothing
    12% of pickups   ~13 lines late  <- exactly a compose strip in
                                        flight (strips run 6-22 lines)

**Those 12% ARE the blit skips, which are the stale bands, which are
the green tearing.** An interrupt preempts the strip; polling cannot.
This is the one lever that reaches the defect, and unlike part (a) it
does not care that the master is saturated.

The accept window is only 6 lines wide (`skip = v < 0xDF || v > 0xE4`,
m_main.c:2278) and the MD's own gate posts inside V 0xDF..0xE2, so a
pickup has 2-5 lines of headroom. A 13-line strip overruns it every
time. Widening the bound instead was already tried and rejected: V of
0xE5-0xE6 left ~1.7ms of vblank and missed the restore, which is why
the gate's upper bound was tightened to 0xE2 in the first place.

MAX WAS CONTAMINATED and the fix is in: the guard was `d < 20000`
(435 lines), so a window the MD never posted or the master never picked
up left a delta spanning frames, and the 16-bit FRT wraps every ~1425
lines. Bound is now one frame (12052 ticks). Added DIAG[62] (> 3 lines)
and DIAG[63] (> 10 lines) so the next run reports the SHAPE directly
instead of requiring the decomposition above — `rom/ARES_cmdprobe2.32x`.

NOTE the probe's own cost is visible here too: blit skips read 36.2% of
cycles in this state against 26.6% in the base state, and V-gate
rejects 1.3% against 0.9%. Its per-vint MMIO write and ISR entry make
the thing it measures worse, so treat 12.1% as an upper bound on the
prize and re-check against a base state.

## CMDINT — interrupt-driven pickup, BUILT. `make CMDINT=1 YIELDROWS=6`

**Design, and it is deliberately NOT "move the window body into the
ISR".** That would mean 640 lines of blit/CRAM/copy_pages running
re-entrantly against a half-finished strip, and it is not necessary.
The ISR does no window work at all:

  1. MD raises CMD INT (`0xA15102`) immediately before posting COMM0.
  2. `main_cmd_irq` sets ONE byte — `win_pend` at 0x26028D80, uncached
     so the strip loop sees it without a purge — and returns.
  3. The row phases (0-3) are RANGE calls, so the strip is cut into
     YIELD_ROWS-row chunks AT THE CALL SITE and the flag is tested
     between them. **The hot inner loops are untouched.**
  4. `b->sub` records rows completed, so a yielded strip RESUMES rather
     than recomputing. Phases 4/5/default stay atomic.

Pickup latency becomes one chunk instead of one strip. That is the
whole mechanism, and it works while the master is saturated — the
condition part (a) died on.

STARVATION GUARD, written before shipping the bug this time: the ISR
fires microseconds BEFORE COMM0 is written, so win_pend is briefly set
with no command visible — expected. A raise with no post EVER following
would make every strip yield forever and stop compose dead, so after 64
fruitless yields the flag is cleared. DIAG[63] counts trips and should
read ~0; nonzero means raises without posts. DIAG[62] counts yields.

### MAME can only show the COST. Ablation, since that is what it is good for:

    baseline                         24.26
    YIELDROWS=12 (no yielding)       26.20   <- ISR + MD write overhead
    YIELDROWS=6                      26.74   <- yielding itself: +0.5
    YIELDROWS=4                      30.52   <- too fine, round-trip dominates

So the restructure's fixed cost is ~2 points (the same ~2 CMDPROBE
showed for the identical ISR + MMIO overhead) and chunking at 6 rows
adds only half a point. 4 rows is past the knee — each yield costs a
full main-loop round trip through the quiet-zone and dt checks, and
three of them per strip is worse than the latency it saves.
**title stays 2.44 exact at every setting**, which is the correctness
gate; the dynamic scenes move with cadence and cannot rank this.

`YIELDROWS=6` shipped as `rom/ARES_cmdint.32x`.

### THE ares RUN THAT DECIDES IT

Play `rom/ARES_cmdint.32x` >3 minutes, save a state. Compare against the
base state `rom/s16.bs1`:

    blit skips        26.6% of cycles   <- MUST fall well below this
    worst window/ack  208 lines, margin 8
    vints/cycle       3.03              <- must NOT rise (that was
                                           IDLEGRACE's failure mode)

Plus DIAG[62] (yields happening at all) and DIAG[63] (~0 expected).
If blit skips do not move, the 12.1%-late decomposition was wrong and
this comes out with the same NEVER SHIP marking as part (a).

### CMDINT round 1 — REJECTED BY ares, and it was MY BUG, not the design

`rom/ARES_cmdint.bs1`, 3967 cycles:

                        base      CMDPROBE    CMDINT r1
    blit skips         26.6%       36.2%       94.8%   <- of cycles
    deferrals            458         458        3523
    vints/cycle         3.03        3.04        3.04

The counters I added for exactly this said the yielding was innocent:
**DIAG[62] = 509 yields in 3967 cycles (0.1/cycle), DIAG[63] = 0 guard
trips.** The mechanism barely fired, so it could not be the cause — and
CMDPROBE runs the same ISR at the same rate for 36.2%. That isolated it
to the strip restructure, which is only in CMDINT.

THE BUG: the chunk loop ran for EVERY phase, so its `default:` case
swept up phase 4 (compose_text), phase 5 (cache_fill) and the
build_maps terminator and handed each of them an EXTRA full 12-row
cat-2 layer compose on top of their real work. Every band did a whole
redundant layer pass. Gated to `b->phase <= 3`; rebuilt as
`rom/ARES_cmdint2.32x`.

**MAME showed this as ~2 points of parity and I read it as acceptable
overhead.** It was a whole extra compose pass per band. When a change
costs more than its parts should, that is a defect to find, not a
tradeoff to accept.

SEPARATELY, AND IT IS A REAL HEADWIND: CMDPROBE — ISR only, no
restructure — already moves blit skips 26.6% -> 36.2%. **Enabling the
CMD interrupt at one fire per vint costs ~10 points of skip rate on its
own.** Whatever interrupt-driven pickup wins, it must win more than
that before it is worth anything. If round 2 lands between 26.6% and
36.2% the mechanism works but does not pay for its own interrupt, and
the answer is to raise CMD INT only when it can help — not every vint.

### CMDINT round 2 — REJECTED, second bug, also mine

`rom/ARES_cmdint2.bs2`, 4021 cycles: blit skips **70.1%** (base 26.6%),
deferrals 1598 (base 342). Better than round 1's 94.8% — the extra
compose pass was real and fixing it recovered 25 points — but still far
past the 36.2% line I set as the reject threshold.

The counters isolated it again. Per-cycle compose cost came back to
baseline (L0 7850 vs base 7152, L1 6959 vs 6688) and yields stayed rare
(645, 0.16/cycle, guard 0), so the remaining damage was not the
chunking and not the yielding.

**BUG 2: `nb->sub` was never reset at band enqueue.** A band that
yielded mid-strip and was then DROPPED left a non-zero resume cursor in
its queue slot; the next band to land there started its phase-0 strip
part-way in and never composed those rows. And `bq[]` is a STACK local
in `m_main`, with only `.on` cleared at boot, so the cursor began as
garbage. Fixed at both places.

**Also fixed: CMD is now masked across the FM-critical window body.**
The interrupt exists to preempt a compose strip. Inside the window it
can only land in the middle of the blit — the one place with 8 lines of
margin — and CMDPROBE had already measured that cost alone at
26.6% -> 36.2%. CMD is level 8, so masking at 8 blocks it and leaves
VRES (14) and V (12) alone; a CMD raised during the window stays
pending and fires as the mask drops.

### MAME PRE-SCREEN, added because parity alone missed both bugs

Parity moves with cadence and reported the first bug as ~2 points.
Dumping the SAME counters the ares state reports would have caught it
instantly, so that is now the pre-screen (`diagdump.lua` pattern):

                cycles  skips          defer  yields  L0     L1
    base         1197   441 (36.8%)      4      0     8672   7045
    CMDINT r3    1197   384 (32.1%)      8     63     8660   6992

Compose cost is identical to base — the extra pass is gone — and skips
improve slightly even on MAME, where pickup latency is already ~1 line
and there is almost nothing to win. **Never hand over another build on
parity alone; dump the counters first.**

`rom/ARES_cmdint3.32x`. Same three outcomes as before, against base's
26.6% skips / 3.03 vints-per-cycle / 342 deferrals.
