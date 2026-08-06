# LOOP 9 — Make it PLAY SMOOTHLY (the window, then the tearing)

## SUPERSEDED — START FROM LOOP10.md

This is a RECORD of LOOP 9, not a plan. It was written as a kickoff and
its opening premises did not survive the arc:

- "THE ONE NUMBER" below (worst handler 246/202/44) is stale; it now
  reads 149/104/45.
- Mission 1's ranking is built on MAME window terms, and MAME turned out
  to model NO framebuffer write cost at all — it understates the window
  3.8x with a different factor per term, so it cannot rank them.
- Mission 1 item 1 (the DMAC blit) is DEAD, measured 1.77x slower.
- The strobe is not "the blit must FIT". It is the 80-row band's compose
  overrunning its window gap. The blit is a fixed 0.744 lines/row floor.

Read top-down and you get this arc's conclusions in reverse order behind
premises they replaced. The corrections are inline further down, each
next to the claim it replaces. **For the next session, read LOOP10.md.**

MIKE'S PRIORITY ORDER for the whole remaining port: **framerate, screen
tearing, sprites, sound** — and explicitly: *"right now it's just getting
this to play smoothly first."* So this arc is 1 and 2. Sprites are a
correctness arc after that; sound is last and untouched.

## Where LOOP 8 left it

The game is playable, accurate, and Mike has a full level-1 playthrough
with every colour correct. Six iterations of tail work are DONE and the
bottleneck has moved.

    metric               LOOP 7 start   now (ares gameplay)
    vints/cycle              6.99            3.18
    V-gate rejects          57.1%            5.6%
    MD tail (mean)           170.6           48.2   (MAME)
    dreq_incomplete            n/a            5.8%   (best ever)
    BLACK frames (raw)         n/a            5.78%
    parity mean              43.98           26.4   (MAME, see negative 10)

## THE ONE NUMBER THAT DEFINES THIS ARC

    worst handler: total=246  window/ack=202  tail=44   (frame = 262)

**202 of 246 lines is the 68K waiting on the SH-2 render window.** The
tail is finished as a target — it is 18% of the worst case and every
remaining lever there is small. 16 lines of margin means the handler is
still occasionally lapping the frame, and that is what "not smooth" is.

Read that number again before proposing anything: for six iterations the
answer to "the 68K is late" was the tail. It no longer is.

## Where the window time actually goes (MAME, per k1 cycle)

    TOTALwin 1.771ms = blit_preempt 1.016 + apply_cram 0.339
                     + copy_pages 0.026 + flipwait 0.000 + ~0.39 rest

The blit is 57% of the window and it is REAL WORK — a strided SDRAM->FB
copy, 80 longwords per row. Shrinking it means moving pixels faster, not
doing less; `slave_wait`/flipwait already measure ~0, so there is no sync
stall left to reclaim (LOOP 6b).

## Mission 1 — cut the window

RANKED, cheapest-credible first:

1. ~~**SH-2 DMAC CHANNEL 1 FOR blit_half.**~~ **DEAD — BUILT AND
   MEASURED, 1.77x SLOWER ON ARES.** 47.34 -> 83.95 us/row, restore past
   vblank 0.4-2.2% -> 14.1%, and 14% of rows never completed. Statics
   were pixel-identical so the comparison is sound. MAME called it 18%
   faster; MAME models no FB write cost whatsoever. Full numbers and the
   two implementation traps are LOOP.md negatives 20 and 21 — READ THEM
   BEFORE RE-PROPOSING THIS, it is a whole session.
   The caveat that was already on the record ("MEASURE BEFORE BELIEVING
   DMA WINS") was correct, and for a reason nobody had guessed: the
   cached-alias write buffer buys nothing either (cached 47.34 vs
   uncached 47.46), so the blit is a 6.76 MB/s bus floor and instruction
   issue is only 20% of it. Nothing instruction-side can move this.
2. **apply_cram is 0.339ms and LOOP 8 made it work harder.** PAL_SETGEN
   now bumps 32 sets whenever a region pair ships, where the old COMM
   path bumped only the 1-2 sets a batch actually changed. That
   over-invalidates the per-group memo LOOP 6 built. Two fixes, both
   real: bump only the sets whose words actually differ, or paint the
   shipped sets into CRAM in the window the pair LANDS (k0/k2) instead
   of waiting for apply_cram at k1 — which also closes the PRESSURE
   artifact below.
3. **The ~0.39ms "rest" is unattributed.** Nobody has split it. A
   diag_add() bracket around the pre-ack section would say whether
   there is a fourth term worth chasing at all. On ares it is 0.671ms,
   1.71x the MAME reading and now the SECOND-largest k1 term.

### ares window terms (`make WINSPLIT=1`, decoded from a savestate)

MAME understates every one of these, by a different factor each, so the
ranking above was built on numbers that could not rank. Re-measure on
ares before choosing any window fix.

    term            ares      MAME    ares/MAME
    blit_preempt   5.132     1.017      5.05x
      blit_only    5.037     1.005      5.01x
      blit_wait    0.061     0.012      5.08x
    apply_cram     0.673     0.333      2.02x
    rest           0.671     0.393      1.71x
    copy_pages     0.232     0.026      8.92x
    TOTALwin       6.722     1.769      3.80x

`blit_wait` (the SYNC[2] pickup, FBCTL restore, latch spin and SYNC[5]
echo, all bundled inside slot 5 with the blit until now) is 1.2% of the
term. The blit is the term.

## Mission 2 — the tearing / strobe

Unchanged in character since 7k and it is a LOAD CEILING, not a constant
defect: 5.6% of blit windows on ares gameplay, 0.5% in light scenes,
worst restore span **135 lines against a 38-line vblank budget**. The
counter and a RAW capture now agree (5.6% vs 5.78%), so there is exactly
ONE cause — the flip/restore pair overrunning vblank — and iteration 7i's
"3.3x second cause" never existed (it was dedup).

FIXES ALREADY RULED OUT, WITH REASONS — do not re-propose these:
- **skip-on-predict**: would skip 100% of flips and freeze the display.
- **atomic ship**: copy_pages still reads the game's FB staging.
- **shadow bank / give bank Y real content**: the SH-2 may only write the
  FB with FM=1, i.e. inside the window, so it would double the in-window
  blit — the exact thing overrunning vblank.
- **striping the cat1 pass**: 7f. It bounded the pickup latency exactly
  as predicted (worst span 74 -> 56) and LOST ANYWAY on two independent
  counts, including on-screen sprite artifacts. The whole-band call is
  commented DO NOT STRIPE with the numbers attached.
- **splitting further than thirds**: 5 windows = 12Hz display cadence.
- **DMAC channel 1** (Mission 1 item 1): measured 1.77x slower. Dead.
- **dirty-row blit**: 13-17% skippable during scroll, break-even ~25%.
- **late window pickup**: measured and RULED OUT — see below.

### THE SHAPE OF IT (`make SPANPROBE=1`, ares)

Six iterations treated this as a spike. It is not a spike. The span
from pickup to restore, against a 38-line vblank:

    21-30 lines   67.0%
    31-38 (fits)  32.6%   <- a third of all windows, inside by <7 lines
    39-45 (over)   1.9%
    46-70          0.4%

**The mean already fits — 29 lines of 38.** There is no tail to cut off;
the whole distribution sits ~8 lines too close to the edge and the part
poking over is just its top. The target is a **25% shift left**, not a
2x, and that is why "make the blit faster" kept nearly working.

Two things this ruled out, both cheaply:

- **LATE PICKUP IS NOT A CAUSE.** The span counter starts at pickup, so
  a late pickup was invisible to it while eating the same vblank, and
  the gate accepts a 6-line spread (V=DF..E4). Measured: pickup pins at
  V=E1 (90%), late pickups are 7.7-8.7% of windows, and they contribute
  **0 of the overruns**. The window starts on time.
- **THE UNEVEN THIRD IS THE WHOLE OBSERVED OVERRUN POPULATION.** Per
  window: k=0 mean 27.6 lines / 0 over, k=1 mean **31.2** / **all** the
  overruns, k=2 mean 27.6 / 0 over. k=1 blits 40 master rows where the
  others blit 36, because 224 rows do not divide into three tile-aligned
  bands (72+72+80). Worth 1.8 lines when fixed, not 3.6 — R2's tail can
  only be deferred to k=2 (k=0 composes R2), so the rows land on another
  window rather than vanishing.

The k=1 rebalance was BUILT AND REVERTED — it cost two thin permanent
seams and lost Mike's play pass (LOOP.md negative 23). So the only lever
left on this list is the ~2 lines between the vint and pickup at V=E1,
against a deficit of 8.

### THE MECHANISM, FOUND AND CONFIRMED

`k=1` owns 100% of the past-vblank restores in every session measured,
light or heavy. Its span splits almost exactly in half, and only one
half was ever understood:

    n=1611 windows, ares, heavy scene
              blit      wait     total   rows   blit/row
    k=0     26.79ln   0.21ln   27.00ln    36   0.744ln
    k=1     29.72ln   6.39ln   36.11ln    40   0.743ln
    k=2     26.79ln   0.23ln   27.02ln    36   0.744ln

- **blit/row is CONSTANT** at 0.743-0.752 across every window and every
  session. The FB floor is fixed and load-independent — negative 20
  stands unqualified.
- **k=1's blit excess is +2.93 lines against +2.98 predicted** by its 4
  extra rows. Fully explained, nothing hiding in it.
- **k=1's WAIT is the whole rest of it**: 0.21 lines at k=0/k=2 against
  6.39 at k=1, and it SCALES (3.00 in a lighter session, 6.39 here)
  while the other two do not move at all.

`make PICKUPSRC=1` says why:

    PICKUP SOURCE     in-compose   idle-loop
      k=0                  0  0.0%       543
      k=1                167 32.2%       352
      k=2                  0  0.0%       550

**IT IS THE 80-ROW BAND'S COMPOSE NOT FITTING IN ONE WINDOW GAP.**
The compose launched post-ack at window k runs during window k+1 and is
drained at k+1's post-ack — so it has ONE window gap. R0 and R1 are 72
rows and fit; R2 is 80 and does not. Whichever window overlaps R2's
compose pays the wait, because the slave cannot answer SYNC[4] until the
current pass ends.

CORRECTION, RECORDED BECAUSE IT NEARLY BECAME DOCTRINE: this was first
written up as a DATA DEPENDENCY — the slave composing rows 144-184 while
being told to blit rows 144-184, which is true under the old mapping.
COMPOSE_LEAD2 moved the mapping so the overlapping window composes R2
and blits R1 — DIFFERENT ROWS, no possible dependency — and the wait
moved with the compose and KEPT ITS SIZE (6.39 lines at k=1 before, 7.00
at k=0 after). The PICKUP_SRC probe counts whether the slave was INSIDE
its compose, which is *busy*, not *collided*. Busy was read as collided.
The row overlap was real and was not the cause.

WHY COMPOSE_LEAD2 WINS ANYWAY — and it does, on Mike's play pass, the
first thing this arc has won: it DE-COLLOCATES the two costs. The 40-row
blit and the 80-row-compose wait both landed on k=1 (29.72 + 6.39 =
36.11 lines against 38 available). Now k=1 keeps the big blit and loses
the wait (30.25) while k=0 takes the wait with a small blit (34.08).
Worst window 36.11 -> 34.08. Load balancing, not dependency removal.

        PIPE2, ares      blit     wait    total   in-compose
        k=0            27.08ln  7.00ln  34.08ln     35.2%
        k=1            30.05ln  0.20ln  30.25ln      0.0%
        k=2            27.09ln  0.33ln  27.42ln      2.4%

WHAT IS LEFT: the 7.00-line wait itself, which is the 80-row compose
overrunning its gap. The bands are 72/72/80 because 224 rows is 28 tile
rows and 28 does not divide by 3. Splitting the big compose across two
window gaps needs a resumable compose or a second outstanding slot; the
SYNC mailbox carries one command. Do not reach for more service points —
that is 7f, and 7f lost.

FIELD REPORT (Mike, ares play): flicker scales with SPRITE COUNT — the
level-1 gravestones rising make it constant. That is the same finding
from the other end: sprites are what the compose spends its time on, the
compose is what overruns the gap, so sprite load IS the strobe's input.
Also reported: sprite colour shifting, a purple hue, REDUCED but not
gone under COMPOSE_LEAD2. That is Mission 1 item 2's artifact finally
visible on ares (the palette lands at k0/k2, apply_cram paints at k1),
and it now has an eyewitness rather than only a PRESSURE build.

**READ THIS BEFORE PROPOSING THE NEXT ONE.** Four things have now been
built or measured and all four lost: DMAC ch1 (1.77x slower), dirty-row
blit (skip rate inverted), even thirds (seams), late pickup (not a
cause). The blit is a 6.76 MB/s hardware floor, the distribution is a
shoulder 8 lines from the edge, and every instruction-side and
redistribution-side lever is now spent. What has NOT been tried is
reducing the BYTES: the compose writes every pixel to sbuf and the blit
writes it again to the FB. Composing straight into the FB bank would
delete the blit entirely — it is blocked by "the SH-2 may only write the
FB with FM=1", which is the assumption behind the ruled-out shadow bank
and has never itself been tested.

## Gates (every commit)

1. Boots + full attract, NO hang.
2. `tools/parity_run.sh` on a CLEAN probe-free build. Judge the STATICS
   (title, eyehold) — they are render truth. The motion scenes are
   phase-noisy and ares overrules them (negative 10, and LOOP 8 proved
   it again: MAME called it a 4-point regression, ares called it the
   best build yet).
3. Region guard (`grep ' _end$' rom/s16.lst`, limit 0x06019000, headroom
   is now 896 bytes), rom stamped `normal`, `make PRESSURE=1` runs.
4. PLAYABILITY PRESERVED — Mike's ares pass is the acceptance gate.

## Falsifier

ares `state_health.py`, one line: **worst handler window/ack 202 -> under
~120**, with vints/cycle holding at ~3.0 and rejects at or under 5.6%.
For the strobe: **restore past vblank 5.6% -> under 1%** and worst span
under 38 lines, cross-checked against `strobe_scan.py` on a RAW capture
(black% should track the counter — they agree now, keep it that way).

MAME cannot see the strobe by construction (it latches FBCTL immediately
and never defers — negative 11, and BLITBURN was retired rather than left
in the Makefile to mislead). But TOTALwin and the blit term ARE
MAME-visible, so Mission 1 iterates locally and only the verdict needs
ares.

## HARD-WON LESSONS (do not re-learn these)

- **A confirmed mechanism is not a licence to ship the first fix for
  it.** 7f hit its target metric and lost the arc. Read the whole state.
- **Single-session ares numbers vary ~14x by content** (negative 16).
  Match content or compare only large moves.
- **Feed strobe_scan.py RAW captures.** Dedup inflates black% by the
  dedup ratio and makes the gap histogram lie.
- **Probes move the scoreboard.** Gate on clean probe-free builds.
- **A space hack is a change** (negative 13). MISSQ_CAP 192 -> 128 was
  taken as build-fitting housekeeping and cost 11x band deferrals.
- **Verify what a number counts before theorising on a mismatch.**
- **Measure the BASELINE with any new probe before drawing conclusions
  from it** (LOOP 8): a "hot regions never converge" reading looked
  damning and turned out to be normal — the baseline was worse.
- **Packet/size limits do not survive a change in handler load.** Re-
  measure them; LOOP 8's packet grew 75% and drained BETTER.

## THE TWO PATHS TO CHASE DOWN (banked, in order)

COMPOSE_LEAD2 landed and is the default. These are what it exposed, and
Mike's instruction is that both get chased rather than parked.

### PATH 1 — THE 80-ROW COMPOSE DOES NOT FIT IN ONE WINDOW GAP

The largest single recoverable term left in the window: **7.00 lines**,
paid by whichever window overlaps R2's compose (k=0 now, k=1 before).
The compose launched post-ack at window k runs during k+1 and is drained
at k+1's post-ack — one window gap. R0 and R1 are 72 rows and fit; R2 is
80 and does not, so 35.2% of that window's mailbox pickups land inside a
running compose and the master sits on SYNC[2].

Bands are 72/72/80 because 224 rows is 28 tile rows and 28 does not
divide by three. So either:
  a. **split the big compose across two window gaps** — needs a RESUMABLE
     compose or a second outstanding slot; SYNC[0]/SYNC[1] carries one
     command, so this is the structural piece of work; or
  b. **make the compose bands even without moving the SHIP bands** — the
     two are independent (compose_layer takes arbitrary row ranges; only
     the ship set must be composed by pickup). 74/75/75 compose bands
     against unchanged 72/72/80 ship bands has not been costed.

DO NOT REACH FOR MORE SERVICE POINTS. That is 7f: it bounded pickup
latency, hit its target metric, and lost the arc. And note the
correction above — this is NOT a data dependency, so anything justified
by "the slave is blitting rows it is composing" is reasoning from a
retracted claim.

MIKE'S FIELD SIGNAL, which is the same finding from the other end:
**flicker scales with SPRITE COUNT** — the level-1 gravestones rising
make it constant. Sprites are what the compose spends its time on; the
compose is what overruns the gap. Sprite load IS this path's input, so
a heavy-sprite scene is the place to measure, not the title screen.

### PATH 2 — SPRITE COLOUR SHIFT (the purple hue)

**Now has an eyewitness on ares**, which it never had before: Mike
reports a purple hue on sprites, REDUCED but not gone under
COMPOSE_LEAD2, plus grass that glitters. LOOP 8 recorded this as a
MAME-PRESSURE-only artifact that "does NOT reproduce on ares" — that
line is now wrong, and PRESSURE was an early warning that read as noise.

Mechanism is already understood and the fix already designed (Mission 1
item 2): the palette lands at k0/k2 but apply_cram paints CRAM only at
k1, so a shipped pair reaches CRAM up to a full cycle late and the
per-set memo holds the stale paint until the next generation bump.
Disabling the memo restores the colour and costs PRESSURE demo2
40.30 -> 28.47. THE FIX: paint the shipped sets into CRAM in the window
the pair LANDS (it is already pre-ack, so it is legal there).

That COMPOSE_LEAD2 reduced it is corroboration, not a fix — it changed
the compose-to-ship timing the artifact rides on.

DO THIS ONE FIRST. It is correctness, not smoothness, and the standing
directive is accuracy before speed. It is also small, where PATH 1 will
churn the same code the palette timing rides on — measure the palette
change through a fixed floor, not a moving one.

## Not in this arc (open, ranked)

1. **SPRITES** (Mike's #3). Nothing is known-broken, but it is the next
   correctness arc after smoothness. `tools/objdiff_32x.lua` differs
   object slots against the arcade driver.
2. **blit skips 26.8% of cycles** — each is a stale third. Likely to
   move on its own if Mission 1 lands; re-measure before treating it as
   a separate arc.
3. **SOUND** (Mike's #4). No Z80, no PWM. The MCU sound mailbox is
   logged to COMM14 and thrown away. Untouched, and last.
