# LOOP 9 — Make it PLAY SMOOTHLY (the window, then the tearing)

Kickoff doc for a FRESH session. Self-contained: read this + LOOP.md
(the negatives list, and iterations 7a-7k + 8), then run.

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

1. **SH-2 DMAC CHANNEL 1 FOR blit_half.** Channel 0 is the DREQ; channel
   1 is free. blit_half is exactly what a DMAC burst is for, and it is
   what the shipped Sega 32X arcade ports do. CAVEAT ALREADY ON RECORD:
   blit_half's own comment says the cached-alias write-buffer path is
   the one shipped ports use — MEASURE BEFORE BELIEVING DMA WINS. This
   is both the framerate lever AND the strobe lever, which is why it is
   first (see Mission 2).
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
   there is a fourth term worth chasing at all.

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

So the answer is the same as Mission 1 item 1: **the blit must FIT.**
Thirds got the mean inside vblank; the tail is 3.5x over. Make the blit
faster or the arc does not move.

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

## Not in this arc (open, ranked)

1. **SPRITES** (Mike's #3). Nothing is known-broken, but it is the next
   correctness arc after smoothness. `tools/objdiff_32x.lua` differs
   object slots against the arcade driver.
2. **blit skips 26.8% of cycles** — each is a stale third. Likely to
   move on its own if Mission 1 lands; re-measure before treating it as
   a separate arc.
3. **The PRESSURE palette artifact** (LOOP 8). Does NOT reproduce on
   ares. Fix is Mission 1 item 2, so it should fall out of this arc for
   free; if that item is skipped, this stays open.
4. **SOUND** (Mike's #4). No Z80, no PWM. The MCU sound mailbox is
   logged to COMM14 and thrown away. Untouched, and last.
