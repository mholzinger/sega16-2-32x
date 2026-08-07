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
