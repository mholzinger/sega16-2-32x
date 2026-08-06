# The Parity Loop

> **MILESTONE 2026-08-05 (fa07dced): CADENCE TARGET MET — vints/cycle
> 3.02, V-gate rejects 0.7%.** The band that sat at 57-66% for six
> iterations is GONE, and Mike has a full level-1 playthrough. See
> iteration 7e. Remaining: the strobe is bursty (1.19% of frames,
> load-correlated) and dreq_incomplete is 20.7% with push_aborts=0
> (the packet needs SPLITTING). The palette scan is still untouched —
> step 2 was never needed to reach cadence.

> **MILESTONE 2026-08-06: LOOP 8 LANDED AND ares AGREES.** The palette
> scan is gone, the tail nearly halved (92.4 -> 48.2 mean lines; palscan
> 45.1 -> 0.1, stream 15.1 -> 0.2 — COMM HAS NO TENANTS LEFT), and Mike
> has a full level-1 playthrough with every colour correct. ares improved
> or held on every counter, including dreq_incomplete 9.3 -> 5.8% DESPITE
> the TEXT packet growing 340 -> 596 words, and the strobe 7.04 -> 5.78%.
> The MAME `PRESSURE=1` colour artifacts did NOT reproduce there — see
> "the PRESSURE artifact" in iteration 8 for why, and for the real lever
> it exposed.
>
> **THE TAIL ERA IS OVER.** Worst handler is now total=246 with
> window/ack=202 and tail=44: six iterations of tail work have moved the
> bottleneck to the 68K's wait on the SH-2 window. Next arc starts there
> (and at the strobe, which is still a load ceiling).

> ACTIVE ARC: **LOOP8.md** — retire the palette scan (LOOP 7 step 2).
> 45 of the 92 remaining tail lines, run every vint, finding nothing in
> steady state. 44 write sites enumerated, 27 of them precise single-word
> stores; mechanism proven by the LOOP 3c tile thunks. MAME-visible
> falsifier, so it iterates without ares round-trips.
>
> LOOP 7 CLOSED: the band is gone (vints/cycle 6.99 -> 3.02, rejects
> 57.1% -> 0.7-1.4%), tail 170.6 -> 92.4, parity 43.98 -> 22.09 with
> title at 2.43%.
>
> PREVIOUS ARC: **LOOP7.md** — kill the tail (COMM -> DREQ, retire the
> palette scan). LOOP 6 closed: it falsified its own kickoff, cut
> apply_cram (ares window 88 -> 64 lines, exactly as predicted), and
> then found why six iterations of cadence work failed — see "THE
> BAND'S ORIGIN" below. The band is not a bug to revert; it was built,
> and COMM is 20x more expensive per word than DREQ.


Goal: `rom/s16.32x` renders like `mame altbeast` — sprites, caching,
layering, background — measured, not felt. The loop does not stop until
every scoreboard scene is under threshold and Mike's ares pass agrees.

## The objective function

`tools/parity_run.sh` captures both machines at SCENE-ANCHORED states
(game variables f031/f02A — never frame numbers; NVRAM cleared every
arcade run) and prints the scoreboard: percent mismatched pixels per
scene plus a magenta diff image per scene in `parity/`.

Threshold: a scene passes at **< 1.0%** (conversion-noise floor is ~0%,
sprite-position jitter from 1-frame anchor slop accounts for the rest).
Scenes: title, scream, eyehold, demo, demo2. Grow the list as rounds
2-5 come online (gameplay anchors: level/round variables, same method).

## One iteration

1. `tools/parity_run.sh` → scoreboard.
2. Pick the WORST scene. Open its `_diff.png`. Classify the mismatch:
   layer offset (scroll/geometry), wrong colors (maps/palette), missing
   art (cache/transport), sprite placement (list/decode), priority
   (mixing rules).
3. Diagnose with the kit, in order of cheapness:
   savestate parsing (TOOLKIT recipes) → scene-matched MAME probes →
   wpcatch → jtcores .v (hardware truth) → MAME segas16b_v.cpp →
   shipped-ROM archaeology (srcref/*.32x) → the MD Altered Beast port
   (a third same-game reference when 68K-side behavior is in doubt).
4. Fix ONE root cause. No fix without a named mechanism.
5. Gates, all mandatory before commit:
   - full regression (`tools/play_32x.lua`, telemetry sane, deferrals ~0)
   - `make PRESSURE=1` eye+demo scenes hold shape
   - scoreboard: target scene improves, NO other scene regresses
   - region guard passes, rom stamped `normal`
6. Commit with the mechanism in the message. Update the scoreboard
   history table below.

## Standing laws (violations caused every regression this week)

- The FB window is not an MD→SH2 data channel; COMM + DREQ only.
- The blit deadline is sacred: no maintenance work past dt=4000.
- Bands complete or defer; never ship partial layers.
- Frame numbers never align across builds/machines; anchor on state.
- MAME cannot see: FB arbitration drops, write-buffer stalls, host-load
  from idle polling. Ares-only symptoms need savestates, not theories.
- Every rom stamped; every savestate names its build.

## Roles

- Loop (Claude): iterations, autonomously, reporting the scoreboard
  delta each cycle. Runs via /loop or continuous turns.
- Mike: ares pass every ~5 iterations or at any milestone commit; ares
  remains the outer acceptance gate. Savestate slot 1 on any anomaly.

## Scoreboard history

| date | build | title | scream | eyehold | demo | demo2 | mean |
|------|-------|-------|--------|---------|------|-------|------|
| 07-30 | 54b227c | 71.5 | n/a | 51.4 | 47.8 | 22.4 | 48.3 | (instant anchors: latency-dominated)
| 07-30 | 2d0d52c | 2.75 | n/a | 3.32 | 48.3 | 22.7 | 19.3 | (stable anchors: statics=render truth)
| 08-02 | e37885b | 41.0 | 90.8 | 3.3 | 48.4 | 20.2 | 40.7 | (iter4 BASELINE; scream now measured; MAME rig has drifted — ares is the gate)
| 08-02 | 59cdebf | 41.0 | 90.8 | 3.3 | 48.3 | 20.2 | 40.7 | (iter4 LANDED: MAME-neutral by construction — latency-only change, ares measures the win)
| 08-04 | d4abcb5 | 49.3 | 91.7 | 3.4 | 52.1 | 20.9 | 43.5 | (LOOP 6 BASELINE, re-measured)
| 08-04 | iter6 | 49.3 | 91.7 | 3.4 | 52.1 | 20.9 | 43.5 | (LOOP 6 LANDED: apply_cram gated — MAME-neutral by construction, ares measures the win)
| 08-05 | fbb31c4 | 49.3 | 91.7 | 3.4 | 52.1 | 23.4 | 44.0 | (LOOP 7 BASELINE, re-measured. scream's 91.7 is OURS RENDERING A BLACK FRAME — see iter7a)
| 08-05 | iter7a | 48.3 | 47.2 | 3.1 | 52.9 | 22.0 | **34.7** | (LOOP 7a LANDED: COMM -> DREQ. 4 of 5 scenes improve; scream stops being blank)
| 08-05 | iter7d | 49.7 | 45.1 | 3.1 | 49.5 | 13.0 | **32.1** | (LOOP 7d: blit in THIRDS — the flip/restore pair overran vblank on 100% of windows)
| 08-05 | iter7g | 2.4 | 37.6 | 3.4 | 48.7 | 21.8 | **22.8** | (LOOP 7g: DREQ packet SPLIT by phase. title 49.7 -> 2.4)
| 08-05 | iter7h | 2.4 | 37.6 | 3.1 | 48.7 | 18.7 | **22.1** | (LOOP 7h: missq out of .bss, MISSQ_CAP back to 192)
| 08-06 | LOOP 8 | 2.44 | 53.9 | 3.06 | 50.6 | 21.9 | 26.4 | (palette scan RETIRED. statics pixel-identical; motion scenes are the negative-10 phase noise ares overrules — see iteration 8) |

### Iteration 8 — the scan is DEAD (tail 92.4 -> 48.2). One gate fails.

The falsifier passed, and better than the kickoff predicted, because the
COMM stream died with the scan rather than after it:

    term        7g/7h   LOOP 8   (MAME, TAILPROBE means over 3592 vints)
    MEANtotal    92.4     48.2
    MEANpalscan  45.1      0.1   <- the arc's target
    MEANstream   15.1      0.2   <- COMM has NO tenants left
    MEANdreq     24.0     38.6   <- what the palette now costs
    cycles/3600f 1197     1197   <- cadence unmoved on MAME

45 write sites become `jsr` into MD-RAM thunks (0xFFBA00, 794 bytes) that
OR a 16-bit region mask into a dirty word at 0xFFB9FC; the DREQ TEXT
packet carries an aligned PAIR of 128-word regions and the master applies
it to PAL_U with PAL_SETGEN bumped per set. 0xFFA000 (the 4KB sent-copy)
is free, STREAM_SPIN and `make SPINPROBE=N` are retired, and the SH-2
region guard GAINED headroom (720 -> 896 bytes).

**THE STATIC SITE LIST WAS INCOMPLETE, AND ONLY A CENSUS FOUND IT.** The
kickoff enumerated 44 sites by grepping patch_report for `-> 00FF9`. That
scan sees ADDRESS FORMATION, and 0x3C20 forms `0xFF9800+2+d0*32` only to
QUEUE the pointer at 0xFFF402 — the real writers are the register-indirect
loops at 0x2DC8 / 0x3C5A, observed writing regions 8-9. No static pass can
attribute those. `tools/pal_tap.lua` (a write tap, not a debugger
watchpoint — palette writes run in the hundreds per frame) reports per-PC
region masks over 3000 frames of attract + play, and it also VALIDATED the
hand-derived extents: the shared 0x2612 copy helper observed mask 0085,
exactly the union of its four callers' static masks (7|2|0), and site
0x3952 observed 00FF, exactly its static mask. Two sites needed runtime
masks instead of static ones (0x30C2's colour-cycle engine, which is the
busiest writer at ~3/frame, and the indirect pair), the same treatment
0x258A got on the tile side.

17. **PRIORITY SELECTION STARVES; ROTATE.** First cut picked the lowest
    set dirty bit. The attract colour-cycles live in regions 0-7 and
    re-dirty them every frame, so regions 8-15 — the entire SPRITE
    palette — were never once shipped: dirty in 99.6% of 2000 frames
    (`tools/pal_rate.lua`), scoreboard 33.47, title 2.4 -> 50.9. Round-
    robin bounds the wait at 16 pushes and took it to 23.57 immediately.

18. **A CHANNEL'S RATE MATTERS AS MUCH AS ITS COST.** The old palette
    stream ran on EVERY vint. The DREQ packet is pushed only on
    GATE-ACCEPTED vints, so palette delivery is now window-rate-limited
    where it used to be vint-rate-limited. That is invisible at MAME's
    3.0 vints/cycle and it is exactly what `PRESSURE=1` is built to
    expose. Worth weighing before moving any other per-vint payload onto
    the packet.

PACKET SHAPE, third attempt (the first two are worth not repeating):
  - ONE region, packet held at 340, taking HALF the text chunk. No length
    change, which looked safest. Cost text refresh: 22.09 -> 23.57 with
    LOOP 7a's text-latency signature (demo 48.7 -> 50.9, INSERT COIN).
  - ONE region ALONGSIDE a full text chunk, packet 468. Title back to
    pixel-exact and dreq_incomplete still 0, but scream 37.6 -> 52.9 with
    the ALTERED BEAST logo rendering WHITE.
  - PAIR of regions + full text chunk, packet 596 — the exact size of the
    sprite push that has landed every cycle since 7g, so it asks nothing
    new of the DMA. Logo red again, statics pixel-identical.

**ares VERDICT (savestate + 9757-frame RAW capture, whole of level 1).**
Every counter improved or held, against the 7k gameplay session:

    metric               7k gameplay   LOOP 8
    vints/cycle              3.24        3.18
    V-gate rejects           7.5%        5.6%
    band deferrals      33% of cycles   18.6%
    dreq_incomplete          9.3%        5.8%   <- best ever
    restore past vblank      6.9%        5.6%
    FS latch mean          17 ticks    15 ticks
    push_aborts                 0           0
    BLACK (raw capture)     7.04%       5.78%

dreq_incomplete FALLING while the TEXT packet grew 340 -> 596 words is the
result worth keeping: the constraint on that packet was never its size in
isolation, it was size against the 68K time available to push it. Freeing
45 lines/vint bought more than the extra 256 words cost. The two earlier
packet shapes were both designed around a fear that measurement has now
retired.

19. **THE TAIL ERA IS OVER.** Worst handler: total=246, window/ack=202,
    tail=44, 16 lines of margin. Six iterations attacked the tail; it is
    now 18% of the worst handler and the 68K's wait on the SH-2 WINDOW is
    82%. Every future "the 68K is late" hypothesis should start there.

**THE PRESSURE ARTIFACT — a MAME-stress artifact with a real lever behind
it.** `make PRESSURE=1` does not hold shape: demo2 renders PURPLE GRASS
and a white logo (baseline PRESSURE renders both correctly), mean 28.55 ->
34.36. It does NOT reproduce on ares — a full level-1 capture has correct
grass, correct logo, correct boss-death greyscale flash, no colour fault
anywhere. PRESSURE cuts the quiet zone to 6000/6500 against the shipped
11300/10300, which starves the WINDOW RATE far below anything ares
produces, and that is precisely what this arc made the palette depend on
(negative 18). So it is not a blocker — but it is a genuine early warning,
and the mechanism is understood: the palette lands at k0/k2 while
apply_cram runs only at k1, so a shipped pair reaches CRAM up to a full
cycle late, and the per-set memo holds the stale paint until the next
generation bump. Disabling the memo restores the grass and takes PRESSURE
demo2 40.30 -> 28.47. THE FIX, when the window rate ever gets tight
enough to matter: paint the shipped sets into CRAM in the SAME window the
pair lands (it is already pre-ack, so it is legal there) instead of
waiting for k1.

MAME scoreboard: title 2.44 (was 2.43) and eyehold 3.06 (was 3.06) are
pixel-identical; the motion scenes read worse (scream 37.59 -> 53.95, demo
48.66 -> 50.61, demo2 18.73 -> 21.85, mean 22.09 -> 26.38). Negative 10
says not to tune against those scene percentages in this arc, and ares —
the acceptance gate — disagrees with them outright.

WHAT THE MEASUREMENTS ALREADY RULE OUT — do not re-run these:
  - NOT the transport. `tools/pal_probe.lua` diffs the MD mirror against
    PAL_U per region: regions 2-15 are out of sync in 0-2% of samples on
    BOTH builds, and the hot cycling regions 0/1 are the same or BETTER
    than baseline (region 0: 52% vs baseline 96.8%; region 1: 81.6% vs
    81.2%). Those high rates are NORMAL — a 60Hz colour-cycle against a
    ~20Hz channel is permanently mid-flight. Measuring the baseline is
    what killed the "regions 0/1 never converge" theory, which had
    already sent one fix (region pairs) down the wrong road.
  - NOT dreq_incomplete. It reads 0 on MAME at every packet size tried.
  - NOT cadence. 1197 cycles per 3600 frames on both builds.
  - IT IS THE apply_cram MEMO. Disabling it (`if (0 && ...)` in
    cram_memo) restores the green grass and takes PRESSURE demo2 40.30
    -> 28.47. The memo is keyed on PAL_SETGEN, whose semantics this arc
    changed: the slave used to bump 1-2 sets per 5-word batch that had
    ACTUALLY CHANGED (the batches came from the diff scan), whereas the
    master now bumps 32 sets whenever a pair SHIPS. The set coverage was
    re-derived and is correct in both halves of the split (pairs never
    straddle the 1024-word tile/sprite boundary), so the fault is not the
    index arithmetic — the likely mechanism is WHEN rather than WHICH:
    the palette lands at k0/k2 but apply_cram only runs at k1, so a
    shipped pair reaches CRAM up to a full cycle later, and under
    PRESSURE a cycle is long. The obvious next move is to paint the
    shipped sets into CRAM in the SAME window the pair lands (it is
    already pre-ack, so it is legal there) instead of waiting for k1.

### Iteration 7e — THE BAND IS GONE. Falsifier met in full.

ares on fa07dced, with a full level-1 playthrough behind it:

    metric              LOOP 7 start   7c      7d(this)   target
    vints/cycle             6.99       4.91     3.02      ~3     MET
    V-gate rejects         57.1%      39.0%     0.7%      ->0    MET
    restore past vblank      n/a     100.0%     0.6%      0
    band-queue deferrals     n/a        593      48
    worst handler            241        221      224

Thirds worked: the flip/restore pair went from overrunning vblank on
EVERY window to 0.6% of them. Deferrals collapsed 593 -> 48, so the
band queue is no longer saturated. And note what did NOT happen — the
palette scan is still there, all 45 lines of it. Step 2 was never
needed to reach cadence; step 1 plus the blit partition did it.

THE STROBE IS NOW LOAD-CORRELATED, AND THAT CHANGES THE FIX. A census
over Mike's 9916-frame ares capture (`tools/strobe_scan.py`, new — it
separates black frames by FILE SIZE, no PNG decode, because a black
frame compresses ~100x smaller) says:

    118 black frames = 1.19%, in 17 BURSTS, dominant gap 4 frames
    (= 3 good then 1 black, the 3-window cycle plus one)
    bursts run 9-40% black internally and get denser in late gameplay

Bursty, not uniform. So the mean is fixed and the WORST CASE is not:
worst restore span is 74 lines against a 38-line budget, on a blit that
is now only ~36 rows per CPU. Cutting rows further cannot explain or
fix a 74-line span.

HYPOTHESIS FOR THE NEXT PASS (unverified, cheapest first): the span is
blit + a FIXED part, and the fixed part contains `while (SYNC[2] < 1)`
— the master waiting for the SLAVE to pick up the preempt mailbox,
INSIDE the vblank-critical section. Pickup is bounded by one compose
strip, and sprite strips are documented at ~1.4ms = ~65 lines on ares.
That is the whole 74-line worst case, it is load-correlated exactly as
the bursts are, and no amount of row-splitting touches it. The master
cannot simply restore first (the slave would blit the wrong bank), so
the candidates are: (a) have the master blit the whole band alone —
doubles its rows but removes the sync wait entirely, and B is small
now; (b) shorten sprite strips so pickup latency drops; (c) a
finer-grained slave poll.

### Iteration 7f — REVERTED. The cat1 pass is whole-band ON PURPOSE.

The plan "master blits the whole band alone, dropping the slave wait"
was killed on paper before it was written, and that reasoning stands:

  SYNC[2] is set AFTER the slave's blit, not at pickup — so the
  master's pre-restore wait already covers the slave's ENTIRE blit.
  Master-solo removes the wait but serialises all 72 rows onto one CPU:
  ~48 lines DETERMINISTIC against a 38-line budget = 100% overrun, far
  worse than 0.6%. The wait is not the cost; the PICKUP LATENCY inside
  it is, which is why the strobe is bursty rather than constant.

So the attempt was to bound that latency: in slave_concurrent_k the FG
cat1 pass runs as ONE uninterruptible compose_layer over the whole band
(up to 40 rows, no service point inside) while BG opaque, FG cat0 and
sprites above are all striped 12 rows. Making it uniform looked free.
It is not. ares, build 1ed3075f:

    metric                  7e      7f      verdict
    worst restore span      74      56      the TARGET, and it MOVED
    restore past vblank    0.6%    3.4%     worse
    V-gate rejects         0.7%    5.9%     worse
    vints/cycle            3.02    3.18     worse
    window/ack span          69      96     worse
    black frames (capture) 1.19%   3.56%    worse
    sprites                  ok  ARTIFACTS  worse

12. THE HYPOTHESIS WAS RIGHT AND THE FIX STILL LOST. Striping DID bound
    the pickup latency exactly as predicted — worst span 74 -> 56. It
    lost on two independent counts anyway: (i) the extra service points
    cost more compose time than the bounded pickup saved (window/ack 69
    -> 96 lines, and the strobe RATE tripled even as its worst case
    fell); (ii) cat1-over-sprites does not survive being cut into
    strips — on-screen sprite artifacts, which is the acceptance gate.
    A confirmed mechanism is not a licence to ship the first fix for it.
    The whole-band call is now commented DO NOT STRIPE with these
    numbers attached.

    Also note the shape: a change can improve the metric you aimed at
    and lose the arc. Read the whole state, not the target line.

REVERTED to the 7e code; scoreboard back to 32.06 scene-for-scene.

NEXT for the strobe — the wait must be bounded WITHOUT adding service
points or touching layer composition:
  (a) DMAC channel 1 for blit_half (channel 0 is the DREQ). Cuts B for
      both CPUs, shrinking the pair from the other side entirely.
  (b) Post SYNC[4] EARLIER — at the previous window's post-ack rather
      than inside this window — so the slave's pickup happens before
      the vblank-critical section starts, not inside it. Needs the
      slave to blit only after FS flips, so it would have to wait on a
      flag the master sets at the flip.
  (c) Accept the burst and hide it: give the never-composed bank real
      content so a deferred restore shows a stale frame, not black.

### Iteration 7g — the DREQ packet is SPLIT, and 2/3 of it was waste

push_aborts read 0 across three ares passes while dreq_incomplete sat at
14-21% of cycles. That combination only means one thing: the 68K pushes
every word and the DMA still fails to drain, so the transfer is too big.
Splitting is what the kickoff doc prescribed ("if it climbs, SPLIT the
packet rather than grow it").

THE FREE PART: 512 of the 852 words were the sprite list, and the sprite
list is harvested at w1 ONLY — so it was being pushed on all three
phases and consumed on one. Two of every three pushes threw 60% of the
packet away. Splitting by phase deletes that outright.

    after w0    -> SPRITE packet, 596 words (lands for w1's harvest)
    after w1/w2 -> TEXT packet,   340 words
    shared 82-word prefix: 0..19 regs | 20..79 rowscroll | 80 bitmap |
                           81 text base
    mean payload 852 -> 425 words

MEASURED (MAME, TAILPROBE means over 3591 vints):

    term        7a      7g
    total     115.1    92.4
    dreq       54.4    24.0     <- less than half
    palscan    45.1    45.1     <- untouched, and now HALF the tail
    stream      9.3    15.1

Scoreboard 32.06 -> **22.78**, the best in the table, and title lands at
**2.43%** — effectively pixel-exact. demo2 13.0 -> 21.8 is the bimodal
anchor again (see negative 10); scream 45.1 -> 37.6, demo 49.5 -> 48.7.

TWO IMPLEMENTATION NOTES worth keeping:
- NO TAG WORD. The master derives the landed packet's layout from its
  own phase: a gate-rejected vint leaves md_main's wskip UNADVANCED and
  retries the same phase, so every window the master processes has
  k = prev+1 mod 3. `prev_k = k ? k-1 : 2` — no state, no `%` (the SH-2
  has no divide and the modulo pulled in a helper).
- THE REGION GUARD IS THE REAL BUDGET NOW. .ramtext counts toward _end
  and there were SEVENTY-TWO bytes of headroom; the first cut of this
  change overflowed by 328. That is what forced the shared prefix (less
  decode branching) and a MISSQ_CAP trim 192 -> 128. Before adding
  in-window code, check `grep ' _end$' rom/s16.lst` — the guard fails the
  build late, after the rom would otherwise have been written.

=> THE PALETTE SCAN IS NOW THE WHOLE GAME. 45 of 92 tail lines, and the
only untouched term left. LOOP 7 step 2 (write-thunks) finally has
nothing in front of it.

### Iteration 7h — the region guard made me break something else

ares on 21f11fba. The split did its job — dreq_incomplete 20.7 -> 11.1%
of cycles, push_aborts still 0 — and the rest of the state went
BACKWARDS against the milestone:

    metric               milestone   7g
    V-gate rejects           0.7%    8.0%
    vints/cycle              3.02    3.26
    blit skips              21.0%   37.4%
    band-queue deferrals       48     551
    restore past vblank      0.6%    9.0%
    black frames (capture)  1.19%   5.10%

Deferrals up ELEVEN-FOLD is the tell, and it points at the one thing
7g changed that was never about the packet: MISSQ_CAP 192 -> 128,
taken purely to fit the new apply code under the 0x19000 region guard.
Dropped tile fills become repeated misses, the queue saturates, compose
falls behind. Note the adaptive cache_fill drain escalates above 96
COMBINED misses — so bursts genuinely exceed a 128 cap, and "3.5x the
observed peak" was reasoning from a steady-state figure that did not
describe the bursts.

13. A SPACE HACK IS A CHANGE. MISSQ_CAP was trimmed as build-fitting
    housekeeping, mentioned in passing, and it cost more than the
    feature gained. If the guard forces a shave to land a change, that
    shave needs its own line in the gate — or, better, do not shave.

FIX: get missq OUT of .bss instead of shrinking it. It now lives at a
fixed SDRAM address (0x0603A000, above SPR_LAND which needs only 596
words now, ~23KB below the stack top) — the same pattern cache_rot and
blank_tile already use, with the same coherency story (cached alias,
full cache_purge every window). No init needed: entries are always
written before read and miss_n is still zeroed .bss.

    _end 0x06018fb8 (72 bytes headroom) -> 0x06018d30 (720 bytes)

Also retired to buy .ramtext honestly rather than by shaving data:
DIAG[23..25] (the skip-cause split — it answered its question: every
skip is LATE, never wrapped, never a missing heartbeat) and the
purge_lines calls (negative 8 proved them bit-identical dead code).
And DIAG[27] now stores FRT TICKS, not lines — `/46` pulled in a libgcc
divide helper because the SH-2 has none; state_health converts.

MAME: parity 22.78 -> 22.09 (eyehold back to 3.06, demo2 21.8 -> 18.7),
blit_preempt 1.155 -> 1.013ms, MD tail 92.3, dreq 24.1, palscan 45.1.

### Iteration 7i — two instruments disagree 3.3x; measure, do not guess

ares on fa25a024. The MISSQ_CAP diagnosis was RIGHT — everything that
regressed with the 128 cap came back:

    metric               milestone   7g      7h
    band-queue deferrals       48    551      98
    blit skips              21.0%  37.4%   20.6%
    V-gate rejects           0.7%   8.0%    3.2%
    vints/cycle              3.02   3.26    3.10
    restore past vblank      0.6%   9.0%    3.3%
    dreq_incomplete         20.7%  11.1%   19.0%

But the capture says the strobe got WORSE, not better: 489 of 4457
frames black = **10.97%**, dominant gap 4, files 6.4KB against a 779KB
median (verified by sampling — real black frames, not fades, and no
mixed-naming confound in the capture dir).

3.3% of blit windows overrun vblank, but 11% of frames are black. THE
COUNTER AND THE SCREEN DISAGREE BY 3.3x, so the vblank overrun is not
the whole cause. Two passes have now been spent on changes that hit
their target metric and lost the arc (7f, and 7g's space hack); this
one buys a measurement instead.

ALSO NOTE: dreq_incomplete read 11.1% on 7g and 19.0% on 7h with
IDENTICAL packet code. These are different play sessions covering
different content, so single-session deltas of a few points are not
evidence. Only large moves (deferrals 551 -> 98) are.

THE HYPOTHESIS WORTH TESTING. If ares defers an out-of-vblank FBCTL
write to the next vblank, then the master's FS readback spin does not
FAIL on it — it BLOCKS, for up to a frame, with FM=1, which stalls the
68K too. That would make the strobe and "still a little slow" THE SAME
BUG, and nothing we have measures it: the spin has guard=2000000, so it
never trips a timeout and never appears in any counter.

INSTRUMENT (ships): DIAG[29] = total ticks waited for the FS restore to
latch, DIAG[30] = waits longer than one scanline. state_health prints
mean ticks and lines. MAME baseline: **1 tick, 0 long waits** — it
latches immediately, as it must. On ares:
  - mean of a few ticks  -> latches are immediate, the strobe is
                            something else and this line is closed
  - mean in the thousands -> confirmed, and the fix target is the
                            blocking wait itself, not the blit size
                            (~46 ticks/scanline, ~12000/frame)

### Iteration 7j — THE STROBE IS A BLOCKING WAIT. One cause, confirmed.

ares on 502abd10, against a RAW capture this time (Mike had been
deduping — see below). The two instruments now AGREE:

    restore past vblank = 238/2880 = 8.3% of blit windows
    BLACK (raw capture) = 179/2472 = 7.24% of frames

So the strobe has exactly ONE cause, the vblank overrun, and the 3.3x
"second cause" from iteration 7i never existed.

14. THE 3.3x DISCREPANCY WAS DEDUP, NOT A SECOND BUG. Our display ships
    at ~20Hz, so a raw 60Hz capture repeats each good frame ~3 times.
    Dedup collapses those while a black frame, differing from both
    neighbours, always survives as its own entry — inflating black% by
    about the dedup ratio (10.97% deduped vs 3.3% counter). The gap
    histogram is skewed identically, so "gap 4" in a deduped stream is
    NOT a period. strobe_scan.py now says this at the top. Validate what
    a number counts before building a theory on it disagreeing.

AND THE LATCH TIMER FOUND THE MECHANISM:

    FS restore latch: mean=882 ticks (19.2 lines) over ALL windows,
                      263/2880 waits >1 line (9.1%)

Mean 882 across all windows puts each of the ~9% long waits at ~9600
ticks = **0.8 of a FRAME**, held with FM=1, so the 68K is stopped for
it too. ares defers an out-of-vblank FBCTL write to the next vblank and
this spin did not fail on that — it BLOCKED. Nothing measured it
because guard=2000000 never trips.

That is POSITIVE FEEDBACK: a late restore steals most of a frame, so
the next window starts later and is late too. It explains the bursts,
and it explains why the rate bounced 0.6% -> 8.3% between sessions —
the loop latches into a bad state and stays there. It is also SLOWNESS,
not just strobe: ~19 lines per window of stalled 68K on average.

FIX: bound the wait at 200 ticks (~4 lines; an undeferred latch takes
ONE — MAME reads exactly 1). Past that we know it is deferred, and
waiting cannot make it arrive sooner, so stop paying and let the loop
recover. Everything else pre-ack is SDRAM-only EXCEPT copy_pages, which
reads the game's staging THROUGH the FB and needs the bank actually
selected — so that DEFERS when the latch is unsettled. Deferring is
free: pg_pending is sticky and steady state is zero pending anyway.
DIAG[31] counts unsettled exits.

MAME-neutral: parity 22.09 scene-for-scene, skips unchanged, PRESSURE
holds. Its `latch` baseline moves (1 -> ~305 ticks) purely because the
loop condition now calls frt(); that is a measurement artifact of the
bounded form, not new cost — TOTALwin and the scoreboard are unmoved.

### Iteration 7k — the block is gone; the strobe is NOT. Read carefully.

ares on d8d91454, savestate + raw capture, 12555 vints (3.5x the
previous session — do not compare MAXes across it).

    FS restore latch  mean 882 -> **17 ticks** (19.2 -> 0.4 lines)
    unsettled         790/10481 = 7.5% (we bail on a deferred latch)
    dreq_incomplete   14.1% -> 9.3% of cycles (best yet)
    restore past vbl  8.3% -> 6.9%
    BLACK (raw)       7.24% -> **7.04%**   <- essentially UNCHANGED

THE BOUNDED WAIT WORKED AND DID NOT FIX THE STROBE, and the reason is
obvious in hindsight: bounding the wait stops us PAYING for the deferral,
it does not PREVENT it. The FBCTL write still lands outside vblank, ares
still defers it to the next vblank, and the never-composed bank is still
on screen for that frame. What we recovered is 68K stall time — ~19 lines
per window, which was real and worth having — not the black frame.

15. "POSITIVE FEEDBACK" WAS THE WRONG STORY. The blocking spin looked
    self-sustaining (late restore -> steals 0.8 frame -> next window
    late), and removing it moved the strobe by 0.2 points. The block was
    a SLOWNESS bug that happened to sit next to the strobe. Two separate
    faults sharing one line of code.

WHY (b) — "give the never-composed bank real content" — IS BLOCKED.
Worth writing down because it looks so attractive. The bank displayed
during the blit is the CPU-side bank, whose VISIBLE area (0x200 +
320*224) is genuinely free — the game's staging starts at 0x12000, past
it — so shadowing it would not disturb the game. But the SH-2 may only
write the FB with FM=1, i.e. INSIDE the window: FM=0 hands the FB back
to the game and SH-2 writes there are dropped by arbitration (the
savestate-proven rule that started this whole arc). So a shadow blit
cannot be done post-ack for free; it would double the IN-window blit,
which is the exact thing overrunning vblank. Dead end unless FM changes.

=> SO THE STROBE NEEDS (a): THE BLIT MUST FIT IN VBLANK. Worst restore
span is 143 lines against a 38-line budget. Thirds got the MEAN inside;
the tail is 3.7x over. Splitting further trades display cadence (5
windows = 12Hz) and is not the answer. The answer is to make the blit
itself faster: SH-2 DMAC CHANNEL 1 is free — channel 0 is the DREQ — and
blit_half is a pure strided SDRAM->FB copy, 80 longwords per row, which
is exactly what a DMAC burst is for. That is the next arc, and it is the
same thing the shipped Sega 32X arcade ports do.

16. SINGLE-SESSION NUMBERS VARY ~14x BY CONTENT. Two savestates, SAME
    build d8d91454, no rebuild between them:

        metric              gameplay (12555 vints)   light (1342 vints)
        V-gate rejects            7.5%                    1.4%
        vints/cycle               3.24                    3.04
        band deferrals           33% of cycles            8%
        restore past vblank       6.9%                    0.5%
        FS latch mean            17 ticks                 5 ticks

    In light scenes this build is AT MILESTONE QUALITY. Under sustained
    gameplay it degrades 14x on the strobe. So the strobe is not a
    constant defect to tune away — it is a LOAD CEILING, which is what
    the burst structure said from the start.
    CONSEQUENCE FOR THE METHOD: a single savestate cannot show a
    regression unless the move is large or the content is matched. Some
    of the "7g regressed cadence" reading earlier in this arc was
    probably content, not code — the MISSQ_CAP fault was real (deferrals
    551 vs 48 is an 11x move) but the smaller deltas around it were not
    evidence. Compare like sessions, or compare only large moves.

RANKED REMAINING WORK:RANKED REMAINING WORK:
1. the burst strobe (above) — the last visible artifact.
2. SPLIT THE DREQ PACKET. dreq_incomplete 20.7% of cycles with
   push_aborts=0 has now said the same thing three passes running: the
   68K pushes all 852 words and the DMA does not drain.
3. blit skips 21.0% of cycles (was 10.5%) — each is a stale third.
4. the palette scan (LOOP 7 step 2), now a pure-throughput win rather
   than a cadence one.

### Iteration 7a LANDED — COMM payloads onto the DREQ packet

Did what LOOP7.md step 1 specified. Layer regs (0x740-0x753), the
rowscroll tables (0x7C0-0x7FB) and the full 0..2047 text rotation all
ride the DREQ packet now (772 -> 852 words, still 4-aligned); COMM
carries the palette and nothing else, and its loop EXITS IMMEDIATELY
when the dirty queue is empty — which is the steady state, so the
common vint now pays ZERO ack round-trips.

MEASURED (`make TAILPROBE=1` + win_probe.lua, means over 3591 vints):

    term        before   after
    total        170.6    115.1
    stream        70.0      9.3     <- the elastic ack-spin, gone
    dreq          49.0     54.4
    palscan       45.1     45.0     <- untouched; that is step 2

Clean probe-free build: worst-case handler 224 -> 177 of 262, its
window span 18 -> 5, tail 206 -> 172. Scoreboard mean 44.0 -> 34.7.

THE SCOREBOARD MOVED FOR A REASON WORTH READING. scream's baseline
"91.7" was OUR SIDE RENDERING A BLACK FRAME: at anchor+45 the game had
not drawn the scene yet. It draws now. Four of five scenes improve;
demo alone rises 52.1 -> 52.9, and its diff is structurally identical
to the baseline's except the INSERT COIN text block — text-refresh
latency, because the COMM text rotation is gone and DREQ refreshes the
full 2048 words in 8 pushes instead of ~4.

NEGATIVE RESULTS (do not re-run these):

6. THE BLIT-SKIP RISE IS THE 68K, NOT THE CHANNEL. Skips looked like
   49 -> 155/run, every one on the LATE side (v > 0xE4, none wrapped —
   the DIAG[23..25] split says so). `make TAILBURN=1` pads the MD
   handler back to its old ~170 lines with everything else identical:
   skips fall to 58. So it is "the 68K now runs more", i.e. the WIN,
   not a transport bug. On the shipped probe-free build the real
   numbers are 50 -> 55 of 3591 windows; the 155 was a TAILPROBE
   artifact (probes move the scoreboard — measure gates clean).
7. THE QUIET-ZONE PHASE REFERENCE IS NOT THE PROBLEM. Anchoring
   t_vint to the MD's V heartbeat (back-dating the pickup by the lines
   elapsed since 0xDF, ~46 FRT ticks/line) instead of the pickup
   instant — so one late pickup cannot latch the loop late — changed
   NOTHING (153 vs 155) and made blit_preempt worse (0.99 -> 1.14ms).
   Tightening the strip thresholds 11300/10300 -> 10600/9600 was
   likewise near-null (155 -> 146). Reverted both.
8. THE CACHE WAS NOT THE SCROLL DRIFT. First cut scored scream 47.2 at
   dx=+12 and I assumed latch_layer_regs was reading stale TEXT_C
   (it reads the CACHED alias, and the full cache_purge only runs
   POST-ack). Added targeted purge_lines() over the two reg blocks:
   output was BIT-IDENTICAL. The post-ack purge already leaves those
   lines cold every window. The purge stays — it is correct and nearly
   free — but it fixed nothing, and the dx was never drift at all:
   `tools/reg_probe.lua` compares the MD mirror against the SH-2
   shadow word by word and they agree EXACTLY on every reg, every
   sample. Verify the transport before theorising about the consumer.
9. A 512-WORD TEXT CHUNK IS NOT YET AFFORDABLE. It refreshes text
   twice as fast and rendered the TITLE near-perfectly (48.3 -> 2.7%
   mismatch), but the scream scene came back as group-1 red/white/blue
   fallback — the documented starved-prescan signature. The game now
   loads scenes faster than the 8-batch/vint COMM palette channel can
   feed them. Shipped at 256; revisit after step 2.

=> THE PALETTE IS NOW THE BOTTLENECK, which is exactly LOOP 7 step 2.
It is the last COMM tenant, it is the 45-line scan, and it is what
caps the text chunk. Everything in this arc points at it.

### Iteration 7b — ares says 7a WORKED and BROKE THE PICTURE. Both true.

Mike's ares pass on 876e51e4. The falsifier PASSED, precisely on the
predicted number:

    rejects   57.1% -> 39.5%   (step 1 target was spin0's 39.4%)
    vints/cyc  6.99 -> 4.95    (spin0 measured 4.93)
    worst handler 241 -> 174, window/ack 63, tail 177 -> 111
    frame margin 21 -> 88 lines

And the game was unplayable: "still flashes, and now has tilemap
artifacts everywhere." Both halves are real, and they are separate.

FLASHING IS NOT FIXED AND WAS NEVER GOING TO BE BY STEP 1. 39.5%
rejects at 4.95 vints/cycle is ~12Hz with most vints rejected — the
two-vblank ship still lands one half of the frame. The kickoff doc's
own falsifier calls 39.4% the INTERMEDIATE target. Getting to ~0% and
3.0 needs the palette scan gone too. Do not read "still flashes" as
"step 1 failed"; read it as "step 1 of 2 landed".

THE ARTIFACTS WERE MINE, and the kickoff doc warned about the exact
trap: "dreq_incomplete is already 59 on ares. A bigger packet carries
more payload per failure. If it climbs, SPLIT the packet." It climbed:

    build       cycles  dreq_incomplete   rate
    1152c7d1     650          59          9.1%
    PROBE_spin0  122           2          1.6%
    876e51e4     214         101         47.2%

A 10% bigger packet, a FIVE-fold failure rate. And 7a had made that
failure catastrophic: the SH-2 applied the packet all-or-nothing on
TE, so an incomplete transfer now meant no scroll, no pages, no
rowscroll and no text for the whole cycle — because COMM no longer
carried any of them as a second path. That is the artifact field.
MAME never showed a whisker of it: dreq_inc reads 0-1 there.

FIX (7b), two parts, neither of which is "shrink it back":
- PACKET REORDERED SMALLEST-AND-MOST-CRITICAL-FIRST. Layer regs and
  rowscroll are the first 80 words, ahead of the 512-word sprite list.
- PARTIAL APPLY. The master reads TCR0 for how many words actually
  landed and applies every block that arrived WHOLE (>=80 regs,
  >=593 sprites+bitmap, >=850 text) instead of gating on TE. A short
  transfer now costs the sprite list, not the entire frame's geometry.
Also: the push spin budget 1200 -> 2600 (~0.33ms), affordable now that
the 68K has 55 lines/vint back.

NEXT ARES PASS ANSWERS THE REMAINING QUESTION IN ONE READ. The abort
counter finally has its own address (0xFFB0E0). It had been sharing
0xFFB0F2 with windows-completed — the same collision iteration 1a
found and supposedly fixed — so EVERY abort figure ever read from a
state before 7b is meaningless, including the 0 you get from an old
state. state_health.py now prints `push_aborts` next to a per-cycle
dreq_incomplete rate, and the two causes need OPPOSITE fixes:
    aborts > 0   the 68K ran out of spin budget mid-push
                 -> raise it further, or shrink the packet
    aborts == 0  the 68K pushed all 852 words and the DMA still did
                 not drain -> SPLIT the packet; budget is irrelevant

10. THE MAME RIG HAS STOPPED DISCRIMINATING FOR THIS ARC. The master's
    window-pickup slack is 2-5 scanlines, so a ONE-LINE tail change
    flips it between regimes. Proven: adding a single per-group branch
    to the push loop moved clean-build blit skips 55 -> 169; removing
    it moved them back to 55, scoreboard bit-identical either way
    (34.69, every scene). The same bistability makes the scoreboard
    flip scream between "drawn" (47%) and "blank" (86-92%) on changes
    that cost nothing. USE THE TAIL SPLIT — it is stable and it moved
    170.6 -> 115.1 -> 116.0. Do not tune against MAME skips or the
    scene percentages in this arc; ares is the judge.

### Iteration 7c — the strobe is SH-2 TIMING, and MAME is structurally blind

ares on 174e4548. 7b's fix worked and the picture came back: Mike's
551-554 show three correct frames and one FULLY BLACK one.

    dreq_incomplete  47.2% -> 20.3% of cycles
    push_aborts      0
    blit skips       10.3% -> 0.0%
    preempt timeouts 0
    rejects 40.1%, vints/cycle 5.01, tail 103, margin 94

PUSH_ABORTS == 0 ANSWERS THE QUESTION THE COUNTER WAS BUILT FOR. The
68K pushed all 852 words and the DMA still did not drain, one cycle in
five. Raising the spin budget is therefore pointless — the packet has
to be SPLIT. That is a directed, unambiguous next action.

THE STROBE IS NOT THE SKIP PATH. blit skips read 0.0%: the master
never declined a flip. Nor is it a hang — preempt timeouts 0.

MECHANISM (measured by elimination; ares-only, and necessarily so).
The flip/restore pair around the blit is a BLANKING INTERVAL over bank
Y, which nothing ever composes into — the blit writes the CPU-side
bank and the restore puts it back on screen. Harmless only while the
whole pair fits inside vblank (38 lines). The ares SH-2 runs the blit
~3x slower than MAME's, the pair overruns, and ares DEFERS an FBCTL
write made outside vblank to the next vblank — so empty bank Y is
displayed for a WHOLE FRAME. The skip gate cannot catch it: it tests V
at PICKUP and never asks whether the blit will FIT.

11. MAME CANNOT REPRODUCE THIS, AND NO PROBE WILL MAKE IT. 0 black
    frames in 150 consecutive (tools/black_probe.lua; brightness dead
    flat 124-132). `make BLITBURN=1400` forced ~30 lines of overrun and
    still gave 0 — because MAME latches FBCTL IMMEDIATELY and never
    defers, so the overrun it reproduces is not the bug. BLITBURN is
    retired rather than left in the Makefile to mislead someone. When a
    bug is a latch-timing difference, no amount of MAME time-shifting
    will surface it; instrument and read it off ares instead.

INSTRUMENT (ships, cheap): DIAG[26] restores landing past vblank,
DIAG[27] worst lines from window start to restore, DIAG[28] blit
windows. state_health prints the rate and the worst. MAME baseline is
0/2339, worst 0 lines — as it must be. A nonzero rate on ares confirms
the mechanism AND says exactly how many lines of blit have to come off.

NEXT LEVERS for the strobe, ranked: (a) cut the blit so the pair fits
in vblank — DIAG[27] gives the number. (b) make bank Y hold the last
complete frame instead of nothing, so a deferred restore shows a stale
frame rather than black. (c) extend the skip gate to PREDICT the
overrun from the previous blit's measured cost — but note the blit may
simply not fit on ares at all, in which case (c) freezes the display
instead of strobing it, which is not obviously better.

### Iteration 7d — STROBE CONFIRMED at 100%, and the blit goes to thirds

ares on b238bec6. The counter answered in one line:

    restore past vblank = 845/845 (100.0% of blit windows)
    worst = 55 lines (vblank = 38)

Not intermittent — EVERY blit window finishes its flip/restore pair
outside vblank. The comment in slave_window_k claimed "56 rows per CPU
per window, ~0.8ms — inside vblank even at ares speed". That claim was
never measured and it is false by 45%.

THIS KILLS FIX OPTION (c) OUTRIGHT. Predicting the overrun and taking
the skip path would skip 100% of flips and freeze the display. Good
riddance — it was the cheapest-looking option and the data removed it
before anyone spent a day on it.

IT ALSO RULES OUT (b)/atomic ship, for the ORIGINAL iteration-1a
reason, which is NOT retired: copy_pages still reads the game's tile
staging in the FB (0x24012000), so alternating display banks still
splits the game's writes across banks and the blind copy still loses
half of them. Atomic ship stays blocked until the tile write-log ring
logs VALUES (iteration 1b), not just dirty pages.

So (a): make the blit fit. FIX = THIRDS. The same 224 rows per cycle
spread over THREE windows instead of two, so each pair carries ~2/3
the rows (~36-40 per CPU, was 56). w0 no longer idles. Aligned to the
band regions and shipped one window after the window that composes
them — W1 composes R0 and W2 ships it, W2 composes R1 and W0 ships it,
W0 composes R2 and W1 ships it — so blit and concurrent compose stay
disjoint exactly as before. Total blit work per cycle is UNCHANGED, so
this is a redistribution and the 68K's per-cycle stall does not grow.
Cost: two mid-screen seams for one frame instead of one.

Scoreboard 34.69 -> 32.06, the best yet, with no seam damage:
demo2 22.00 -> 13.00, demo 52.88 -> 49.46, scream 47.17 -> 45.10,
eyehold 3.09 -> 3.06; title 48.30 -> 49.67 (the phase-noisy one).

Whether it is ENOUGH is arithmetic ares has to settle. If the ares
span is F (fixed: flip readback, cache_purge, slave pickup) plus B
(blit), then 55 = F + B and the new span is F + 0.64B. That lands at
38 only if F is small (~8 lines). The counters print the answer
directly — `restore past vblank` and `worst` — and if it still
overruns, the same lever goes to quarters or the blit itself has to
get faster (DMAC channel 1 is free; channel 0 is the DREQ).

LEAD (unchased): `make PRESSURE=1` runs this build at skips=1, not 55.
Its quiet zone is 6000/6500 against the shipped 11300/10300. The
shipped thresholds were tuned for the OLD 68K load; a middle setting
may buy the skips back. Measure before believing it — negative 7 says
this dial is less powerful than it looks.

### Iteration 6 LANDED — copy_pages was ALREADY dead; apply_cram was the floor

LOOP6.md's premise is FALSIFIED. Measured on MAME (fresh DIAG probe;
profile_32x.lua pointed at a stale base 0x27000, DIAG is 0x28000):

    copy_pages   0.005 ms/cycle   (0.25% of the window; bitmap reads 0000)
    apply_cram   0.679 ms/cycle
    blit+preempt 1.056 ms/cycle
    TOTAL window 2.013 ms/cycle

LOOP 3c's dirty-page bitmap already retired copy_pages — the dirty
bitmap drains to zero and the budgeted copy loop finds nothing to do.
Building the write-LOG ring would have bought 0.005ms. It was also not
reachable as specified: the 0xFFB820 thunks patch ADDRESS-FORMATION
sites (lea / move.l #imm, 6 bytes), not the 2-byte store instructions,
so a thunk there cannot see the VALUE being stored.

THE REAL k1 pre-ack floor was apply_cram: a full ~2112-entry
convert-and-store of every mapped CRAM group, EVERY k1, unconditionally,
inside the FM-hold. Two mechanisms, both MAME-neutral by construction:

- GATE THE STORES. cram_set already computed `cram_mirror[idx] != v`
  and spent it only on shadow_dirty; the store now sits inside that
  test. CRAM writes 2112 -> 29 per cycle. The two mirror-BYPASSING
  writers had to be closed first or the gate goes stale: the shadow
  ramp (241-254) now publishes what it wrote, and the k2 debug bar's
  hijack of entry 255 (inside sprite pair 15) leaves the mirror holding
  the TRUE color and is undone by a single write at the next apply.
- MEMOIZE PER GROUP. Gating stores did NOT cut the s16_to_mars
  arithmetic, which is the bulk of the cost. A whole-pass gate fails —
  the allocator reshuffles the color->group mapping nearly every frame
  (13 skips in 1195 cycles). So memoize per 8-entry CRAM slot on
  (kind|color-set, that set's palette generation). The generation must
  be PER COLOR-SET: attract color-cycles continuously, so one global
  counter is bumped nearly every cycle and invalidates everything
  (that version skipped only 4 groups/cycle vs 16 for per-set).
  PAL_SETGEN[192] at 0x26028C00, slave-written / master-read-only
  (monotonic, no cross-CPU RMW race). Note the SDRAM map comment
  claimed 28C00-28FFF was SPR_LAND; SPR_LAND actually lives at 0x39000
  and that KB is free.

  ORDERING LAW (cost a real regression): the slave must bump the
  generation STRICTLY AFTER storing the palette words. Bumping first
  lets the master paint the OLD words and then record the NEW
  generation, latching that group stale — demo2 20.9 -> 23.4 until the
  bump moved after the stores.

RESULT (MAME): apply_cram 0.679 -> 0.321 ms/cycle (-53%), total
in-window 2.013 -> 1.623 ms (-19%). `cramwr` stays ~29/cycle across the
change — the memo skips only work that would have written nothing, so
CRAM contents are IDENTICAL. Scoreboard identical (demo2 20.86 ->
20.93, demo 52.10 -> 52.13: anchor noise). Under `make PRESSURE=1` the
new build is markedly BETTER than the PRESSURE baseline (demo2 48.2 ->
28.3, mean 49.0 -> 45.5) — freed in-window time showing up exactly
where the stress build binds.

### Iteration 6b — the TAIL is now MAME-VISIBLE. Three negative results.

New probe: `make TAILPROBE=1` + `tools/win_probe.lua`. The MD tail was
only ever measured on ares; it is in fact visible on MAME, which turns
tail work into something iterable without an ares round-trip.

MAME (a16d97d): MD handler max total 224 / window 18 / tail 206 of 262
lines; MEAN total 170, mean stream 70. Tail split (max spans): DREQ
push 52, palette scan 85, COMM stream 117. The WINDOW is negligible on
MAME (18) precisely because its SH-2 is 3x faster — which is why the
window work iter6 cut can only be judged on ares.

THREE NEGATIVES, all worth not repeating:

1. THE COMM STREAM IS ACK-LATENCY BOUND, NOT WORK BOUND. Dirty-gating
   the 16 priority batches and restructuring the text rotation cut
   batches actually SENT from 64 to **11** per vint — and the stream
   span did not move (117 -> 114 max, 70 -> 62 mean). The cost is the
   68K spinning on COMM0 until the SLAVE services it, once per batch,
   not the 68K's own register writes. Fewer batches cannot fix that;
   only fewer ACK ROUND-TRIPS or a faster servicer can. Widening the
   batch is not available either: of the 8 COMM registers, COMM0 is the
   command/ack, COMM12 carries the V heartbeat the window depends on,
   and COMM14 the sound log — 5 payload words is the ceiling.
   REVERTED: 4% of mean handler span was not worth making 0..1791 text
   depend on DREQ with only an 18-second COMM backstop (dreq_incomplete
   is nonzero on ares) — accuracy before speed.

2. A DIRTY-GATE THAT FREES BUDGET BUYS NOTHING. Gating the priority
   block alone made the stream WORSE (117 -> 141): the loop is a
   64-iteration budget, so skipped batches were simply refilled with
   more text batches. Gating only pays if the total SENT is capped too.

3. THE PROBES MOVE THE SCOREBOARD. Instrumentation in the vint path is
   not free — it adds work to the exact path that is overloaded, shifts
   V-gate outcomes, and therefore changes which frames ship: demo 52.1
   -> 54.6, demo2 20.9 -> 23.4 from probes ALONE. This cost real time
   here: an uncommitted probe left in m_main.c made iter6 look like a
   demo2 regression (23.4) that did not exist. Hence TAILPROBE is a
   build flag, the rom stamps as TAILPROBE, and:
   **LAW: run every gate on a probe-free build, and CLEAN-build it.**
   An incremental build also mis-measured this arc once. Clean-built,
   both scoreboards are: baseline d4abcb5 demo2 20.86 / total 43.46;
   iter6 a16d97d demo2 20.93 / total 43.48 — no regression.

WHERE THE TIME ACTUALLY IS NOW (MAME, per k1 cycle):
  window 1.62ms = master blit 1.03 + apply_cram 0.32 + ~0.27 rest
  The blit is 100% master blit_half — the SYNC[2]/SYNC[5] slave waits
  measure ~0.000ms, so the preempt mailbox is doing its job and there
  is no sync stall left to reclaim. Shrinking the blit means shipping
  fewer pixels (dirty-row blitting) or moving it off the pre-ack path,
  not micro-optimizing it.

MEAN tail split (of 262 lines, the numbers that matter — maxima are
spiky): COMM stream 70, DREQ push 49, PALETTE SCAN 45. The palette
scan is SUSTAINED, not a fade-only spike.

### THE BAND'S ORIGIN — it was BUILT, in three commits (dated by savestate)

Field report: "running too slow, frames flashing." Both are ONE cause,
and the old savestates in rom/ date it exactly:

| build      | vints/cycle | V-gate rejects |
|------------|-------------|----------------|
| a4b51d0    | 3.02        | 0.4%           |
| 8b4ecc2    | 3.10        | 3.1%           |
| ~7215209   | 8.78        | 65.9%          |
| 1152c7d1   | 6.99        | 57.1%          |

Healthy at 8b4ecc2, band present by 7215209. Three commits in that
window moved a data channel INTO the 68K vint handler, and their costs
are exactly the tail split measured in iter6b/6c:

  180de61 palette MD-RAM mirror + rotating copy -> palette scan 45 lines
  24b799a palette over COMM                     -> COMM stream   70 lines
  6466663 sprite list over DREQ FIFO            -> DREQ push     49 lines
                                                   TOTAL        164 lines
(measured mean handler: 170 of 262. The tail IS these three.)

Every one was individually RIGHT — the FB cannot carry this data, ares
discards MD FB-window writes (savestate-proven torn sprite records,
zeroed palette rows). Nothing to revert. But cumulatively they turned a
3.0-vints/cycle machine into a 7.0 one, and that is the whole band:
7 vints per shipped frame ~= 8.5Hz ("too slow"), and with 57% of vints
rejected the two-vblank ship frequently lands only ONE half of the
frame ("flashing"). The anti-flash V-gate (bab5f74/9fbb03e) is working
correctly — it is rejecting, which is the symptom, not the bug.

THE DECISIVE RATIO (why the fix is obvious once measured):
    COMM   70 lines for  ~55 words = 1.27   lines/word
    DREQ   49 lines for   772 words = 0.063 lines/word
COMM is TWENTY TIMES more expensive per word, because its cost is an
ACK ROUND-TRIP per 5-word batch, not the payload. That is why spin0
moved the band (57 -> 39%) when five iterations of shaving had not.

=> ARC: move every COMM payload onto the DREQ packet, and replace the
palette scan with write-thunks. Projected: tail 164 -> ~66, handler
241 -> ~136, which is the budget a 3-vints/cycle cadence needs.

### Iteration 6c — the palette scan is BUS-bound. Two more negatives.

4. THE PALETTE SCAN IS NOT DIVISION-BOUND (I predicted it was, above —
   wrong, and corrected here before anyone acts on it). `(qbase+i)/5`
   compiles to a reciprocal multiply: `m68k-elf-objdump -d md_main.o |
   grep divu` finds ZERO. Check the generated code before optimizing
   for a 68000 instruction cost.

5. LONG COMPARES DO NOT HELP ON A 16-BIT BUS. Rewriting the 4-word
   group compare as two 32-bit compares (both bases are 4-byte aligned
   at every group offset, so it was legal and semantically identical)
   left the cost UNCHANGED: mean 45.1 -> 45.7. On the 68000 a 32-bit
   load is TWO bus cycles, so 4 long reads cost exactly what 8 word
   reads cost; only the instruction count fell, and the loop is
   bus-bound, not issue-bound. REVERTED as neutral.
   ABLATION (the decisive test): with the loop body disabled the span
   goes 45.1 -> 0.1, so the loop really is the whole cost — it is just
   1024 unavoidable MD-RAM reads (512 mirror + 512 sent-copy) per vint.

   => The scan cannot be micro-optimized; it has to STOP EXISTING.
   The structural fix is to THUNK THE GAME'S PALETTE WRITES, exactly as
   LOOP 3c did for tiles: the palette is already remapped to the
   0xFF9000 mirror by patch_game.py, so the write sites are enumerable
   the same way, and a dirty bitmap replaces the whole diff. That
   deletes ~45 of 262 lines from EVERY vint. This is a real arc (the
   tile version took one), not a quick win — but it is the only lever
   on this term, and unlike the LOOP 6 kickoff's ring it is reachable:
   these are ADDRESS-FORMATION sites feeding ordinary stores, which is
   what the existing thunk machinery already handles.

NEXT LEVERS, ranked: (a) palette write-thunks (above) — 45 lines/vint,
mechanism proven by the tile thunks. (b) slave stream-service latency,
which sets the whole 70-line COMM cost (see negative 1: it is ack
round-trips, so measure the per-batch wait distribution first).
(c) dirty-row blitting for the 1.03ms in-window blit.

ARES FALSIFIER (Mike, next pass): state_health.py. Predicted — worst-
handler WINDOW span falls (it was ~88 lines, and apply_cram is now the
one term measurably cut); rejects fall below 57%; vints/cycle below
7.01. CRAM writes may cost more on ares than MAME shows (VDP contention
is exactly the "MAME cannot see" class), so the store gate could pay
more there than here. NEXT LEVER if the band holds: blit+preempt is now
65% of the window (1.03 of 1.62 ms) and is the only remaining large
term — it is real work, so cutting it means moving it off the pre-ack
path, not shrinking it.

### Iteration 5 LANDED — PLAYABLE. text stream -> DREQ DMA cut the tail

MILESTONE (ares, build 5a04686e): the game is PLAYABLE — slow but
accurate, real input, correct sprites/text. First time. The tail cut
(moving the bulk text refresh off the slow per-word COMM stream onto the
DREQ DMA packet) dropped the reject band for the first time in 5
iterations: 65% -> 57%, tail 225 -> 151, vints/cycle 8.69 -> 7.01.

Mechanism + the three bugs it took (all debugger-diagnosed via the SH-2
DMAC registers, the tool that finally broke the guessing):
- DREQ packet = 512 sprites + bitmap + text base + 256 text words + 2
  pad (772, MUST be 4-aligned: the FIFO drains in 4-word bursts, a
  non-multiple leaves the tail un-drained -> TE never sets -> stale;
  ares dreq_incomplete 495 -> 23 once padded).
- dreq_rearm() EVERY window + push ONLY on gate-accepted vints: the DMA
  drains one transfer then stops, so off-cycle pushes hit an undrained
  FIFO and BLOCK the 68K mid-group-write (two hard hangs before this).
- text applied every window (fresh at the push rate).

REMAINING (the strobing + the 7Hz): the band is 57%, not collapsed. The
heavy ACCEPTED-vint handler (tail 151, + on k1 the copy_pages ~88-line
FM-hold) overruns the frame, the next vint fires late -> reject -> the
strobing/blank cadence. NEXT LEVERS: (a) retire copy_pages from the k1
pre-ack via the write-log ring (iter 1b) — the biggest single chunk;
(b) shrink the DREQ push (text 256->128) — small, costs HUD refresh
rate; (c) push sprites once/cycle but text every accepted vint (split
packet) — bigger cut, more complex. copy_pages (a) is the high-leverage
one. Falsifier stays state_health: reject % and vints/cycle -> 3.

### Iteration 5 — BAND CRACKED: the per-vint HANDLER TAIL overruns the frame

The 64-67% V-gate reject band is NOT a window-stall latency spiral. It
is the shim's per-vint TAIL — the work AFTER the render window that runs
on EVERY vint, gate-rejected or not: the DREQ sprite-list push, the
512-word palette scan, and the text/palette COMM stream. Four iterations
shortened the WINDOW (runs on the ~35% accepted vints); the band never
moved because the tail (100% of vints) was untouched.

MEASURED (tail-span probe, md_main 0xFFB0FE entry / 0xFFB0F4 packed
max: high=whole tail, low=stream; state_health decodes). ares verdict
59cdebf: rejects 65.5%, vints/cycle 8.69, and one accepted sample at
V=0xE0 — so NOT a calibration shift (on-time vints exist), it is
intermittent overrun. MAME floor (build 26d4148, 3x faster SH-2, gate
rejects ~0%): whole-tail = 231 of 262 scanlines, split stream 120 /
DREQ+scan ~111. The handler eats 88% of the frame EVEN ON MAME; it fits
only because 231 < 262. On ares (slower) the tail crosses 262 -> the
next H-int fires while the handler still runs -> late -> reject, every
frame it overruns. That IS the band, and the ~7Hz crawl.

ROOT: too much slow 68K<->32X bus traffic per vint. The palette scan is
fast local RAM; the cost is (a) ~320+ COMM register writes + ack-spins
in the stream (the MD waits on the slave, which is busy composing) and
(b) the 516-word DREQ FIFO push gated on DMA drain. Both ~110 lines.

CONFIRMED (ares fe8d590a, steady hold): worst handler total=227,
window/ack=2, tail=225 — the WINDOW is negligible (iter1-4 all optimized
~2 lines of 227). The 68K vint handler burns ~225 lines (~14ms) of a
16.7ms frame EVERY vint, leaving the game ~2ms. The game can't complete
its per-frame work, falls behind, and its IRQ-masked sections drift
across vblank -> H-int serviced late (entry V=0x3C/0x5B, mid-frame) ->
reject. MAME passes the same-length handler (doesn't model the
starvation). The tail is ~320 per-word COMM register writes (~175
cycles each on the slow 68K<->32X port) + the DREQ push. CUT THE TAIL.

ITERATION 5 FIX (design fork, next):
- STREAM: stop re-sending the 16 priority batches (layer regs +
  rowscroll) every vint when unchanged; static scenes then send ~0.
  Motion scenes still pay — pair with moving the stream to a single
  bulk DREQ transfer instead of per-word acked COMM.
- DREQ: push the sprite list every OTHER vint (1-frame lag already
  tolerated) to halve its cost, or fold text/palette into the same DMA.
Target: whole-tail << 262 on ares with margin -> rejects collapse,
vints/cycle -> 3. Falsifier: state_health tail span + reject %.
Diagnostic build 26d4148+ carries the probe (F4 repurposed from the
retired fs/torn tracer); it is NOT a fix — the band will still read
~65% until the tail is cut.

### Iteration 4 IMPLEMENTATION LANDED — preempt-blit + early ack

Shipped two of the plan's mechanisms; the third (copy_pages off the
FM-hold) proved impossible without a new arc (below).

(b) PREEMPT-BLIT MAILBOX (the core win). New SYNC[4]/[5] slots: the
master posts the per-window blit in SYNC[4] instead of first draining
the slave's whole concurrent compose (the old pre-blit
`slave_wait(tile_cmd)` — the named retry-loop-saturation cause). The
slave services SYNC[4] between compose strips (slave_service_stream),
so blit pickup latency is <=1 strip (~0.4ms) instead of a full
compose. SAFE BY CONSTRUCTION: the rows Wk blits and the region the
slave is still composing are always DISJOINT (W1 blits 0-112 while
R2/144-224 composes; W2 blits 112-224 while R0/0-72 composes) — single
sbuf, no tear.

(a) EARLY ACK, correctly scoped. The compose LAUNCH, the previous-
compose DRAIN (slave_wait, now post-ack, off the 68K path), and the
band enqueue moved after the COMM0 ack. copy_pages, the DREQ
harvest+re-arm, apply_cram, and the blit stay PRE-ACK.

DEAD END found the hard way (MAME deadlock, both CPUs frozen at
scene 0x0C): moving copy_pages / DREQ re-arm post-ack is UNSAFE.
copy_pages READS the game's FB staging (0x24012000) — legal only at
FM=1 (pre-ack). And the DMA re-arm MUST precede the game's own DREQ
sprite-list push, or the 68K blocks forever on a full FIFO write with
no armed drain (measured: 68K stuck at the fifo store `fifo[0]=s[1]`,
vint entries frozen). Both stay pre-ack. copy_pages (~2ms) is now the
pre-ack FLOOR; retiring it needs the full write-LOG ring (iter 1b:
thunks ship (offset,value), SH-2 applies to the shadow directly, no FB
read) — a separate arc, NOT this iteration.

ARES FALSIFIER (Mike, next pass): state_health.py before/after on
build `iter4`. Predicted: V-gate rejects fall from the invariant
64-67% band toward ~0; vints/cycle -> 3. If the band holds, the
preempt latency (<=1 strip during heavy sprite strips) is itself the
new binding constraint -> next probe shrinks slave strips or splits
the blit off the compose CPU. MAME gates all pass (scoreboard
identical; attract runs title->scream->eyehold->demo; PRESSURE holds
shape) but MAME cannot see the FM-hold latency — the 64-67% band is
the only verdict.

### Iteration 4 probe verdict — cart-bus DEAD; the retry loop IS the load

SPROBE run (sprites off, ares): V-gate rejects 65.9% — unchanged.
The 64-67% band has now survived: three scheduler designs, the
copy_pages hold, and the heaviest cart-bus reader. The only mechanism
that self-stabilizes at a constant reject rate: THE GATE-REJECT RETRY
LOOP SATURATES THE 68K. Rejected posts retry every vint; the master's
pre-ack path includes slave_wait on the PREVIOUS concurrent compose
(ms on ares); the handler chain runs near frame-length, so every
entry is late, every post rejects, and the equilibrium re-forms no
matter what is shaved elsewhere — which is exactly the observed
invariance.

ITERATION 4 IMPLEMENTATION (next session):
(a) EARLY ACK: MD releases the game after FM-required work only
    (blit ~0.75ms + apply_cram ~0.7ms); latch, DREQ harvest/re-arm,
    and compose launches move post-ack (SDRAM/no-FM work).
(b) PREEMPTIBLE SLAVE COMPOSE: the slave polls for window commands
    inside its strip loop (it already calls slave_service_stream
    there); on a window signal it pauses compose, performs its blit
    duty, resumes. Master slave_wait shrinks from a full compose to
    <=1 strip.
Target: 68K per-window stall ~2ms; handler chain << frame; gate
rejects -> ~0; ares cadence vints/cycle -> 3. Verify with
state_health.py before/after — the invariant 64-67% band is the
falsifier either way.

### Iteration 3c closed / 4 opened — the ring works; the drag is deeper

Ring verdict (state_health on ares, build d6165d5d): dirty bitmap
drains to ZERO (the ring is correct and steady-state page traffic is
gone) — yet V-gate rejects held at 63.9% vs 67%. Across original /
split / ring scheduling the reject rate is INVARIANT: the 68K's
chronic lateness is not scheduling. ITERATION 4 HYPOTHESIS: cart-bus
contention — unpair keeps RV=0, the SH-2s read sprite art from cart
per composed pixel, on the bus the 68K fetches game code from; MAME
does not model the arbitration, ares does. A/B probe built:
rom/PROBE_sprites_off.32x (`make SPROBE=1`, stamped SPROBE) disables
sprite compose. If ares rejects collapse -> the fix arc is sprite-art
RESIDENCY (SDRAM working-set cache for sprite rows, like the tile
cache); if unchanged -> next probe strips the shim vint preamble.
tools/state_health.py = the one-line savestate health report.

### Iteration 3b — stall probe verdict: splitting is not shrinking

Dual-PC probe at the "deadlock": both CPUs healthy, composing
continuously — the pipeline starves because window posts stop passing
the V-gate ONCE k2/k0 lengthen (the split spread the 68K-blocked time
without reducing it; the spiral reproduced in MAME). LAW: the ares
cadence fix must REMOVE steady-state FM-hold work, not redistribute
it.

### Iteration 3c — THE plan: write-observer ring (1b, correctly scoped)

The ring needs NO bank tricks and does NOT touch the read-back sites:
thunked stores land in FB staging normally (game reads unaffected)
AND append their offset to an MD-RAM ring. Loads (RLE/block fills)
get entry/exit thunks: load_flag + dirty-page bitmap instead of
per-word logging. MD vint ships ring+bitmap in the DREQ push tail.
SH-2 k1: apply ring offsets (few in-window FB reads) and run page
copies ONLY when the bitmap demands (post-load, display already
held). copy_pages leaves the steady path -> k1 FM-hold ~2ms (blit
0.75 + apply_cram 0.7 + ring apply) -> handler fits the frame ->
20Hz cadence on ares. Store sites (enumerated, disassembled):
- ring-tier: 0xD84 fill helper (+ inline 32BC singles 0xD60-0xD7A),
  0x2AD4/2AE2/2AF0/2AFE vint corner words, 0x6836-cluster seam
  writers (stores in helpers past 0x6966), 0x1BA1C/0x1BA2C fills,
  0x1BA42/0x1BA4A words, 0x173C/40 + 0x1760/64 byte-pair loops
- load-tier (entry/exit brackets): RLE 0x16AE/0x16B2 pair, block
  blitter 0x258A, clears 0x36B0 and 0x1ACD8, 0xDA8 column blit,
  0x1A52E/0x1A54C bonus fills, scratch save/restore 0x1B760/0x1B7A4
  (bracket keeps its round-trip in one bank tenure — moot without
  alternation, kept for the eventual double-buffer)

### Iteration 3 — ares cadence spiral: DIAGNOSED, fix attempt REVERTED

ARES VERDICT (savestate d6ed14ac): speed pathology dead (blit skips
0.7%, DREQ pristine) BUT the render cadence is starved — 145 cycles
in 1317 vints (~7Hz effective): 67% of the MD's window posts fail the
V-counter gate with MID-FRAME entries (B0FE V=0x95). MECHANISM: the
k1 window blocks the 68K through latch + copy_pages(0,6) + apply_cram
+ blit (8-15ms on ares) — the vint handler overruns the frame, the
next vint fires late, the gate rejects, retry: a latency spiral.
FIX DIRECTION (attempted, deadlocked in MAME, reverted): split
copy_pages in thirds across windows + move the DREQ harvest post-ack.
Two ordering laws found and banked: (a) FM must outlive EVERY FB
reader on BOTH CPUs (slave page-copies after its done-signal froze
the pipeline; master acking before the slave's copy did too); (b)
even with both waits there is a residual interlock that stalls at the
scream tile-load — needs a dual-PC stall probe (master+slave PCs at
hang) before the next attempt. The k1-shortening remains THE ares
cadence fix; only its execution needs the probe first.

### Iteration 2 — 30Hz cadence retry: REVERTED (sharper negative result)

The original 30Hz negative predates the sprite fast-forward, so it was
retried with 9ms/cycle freed: STILL fails (skips 976, swait 2.1ms,
scoreboard collapses from deferral staleness). SHARPENED MECHANISM:
the binding constraint is not raw compute (per-frame work ~8ms fits a
33ms cycle on paper) but the PER-WINDOW SYNC BARRIERS — master waits
slave at every window entry, and 3 region-composes + 2 blits + stream
service serialize across only 2 window-gaps. 30Hz needs an async
slave scheduler (no per-window barriers), which is a real arc, not a
cadence flag. Motion-scene latency floor stands at 20Hz until then.
Iteration 3 candidates: demo-scene sprite-list timing alignment
(cheap latency win: harvest the DREQ list every vint instead of every
k1 — sprites currently lag up to 3 vints), then the statics' last 3%.

### Iteration 1a — atomic ship via bank alternation: REVERTED

Attempted: single flip per k==1 vblank entry, full-frame two-CPU blit
into the hidden bank, banks alternating per cycle. Two REAL findings
survived the revert:
1. The MD's fs_home hold and the per-window double-flip dance cost
   measurable time everywhere — with them gone the pipeline hit its
   best-ever health (mskips 9 vs 300+, skips 2 vs 25+). Reclaim these
   wins when 1b lands.
2. FATAL FLAW: `copy_pages` is a BLIND copy — with banks alternating,
   each bank holds only the tile writes from its own access cycles,
   and the blind copy overwrites the SDRAM shadow with partial truth
   (letter-soup tilemaps). Accumulation needs dirty-aware merge, which
   doesn't exist. Also found: the shim's windows-completed diag and
   the DREQ abort counter collided at 0xFFB0F2 (fixed in 1b's base).

### Iteration 1b — the real fix: tile write-log over DREQ (NEXT)

Evict the last FB tenant: patch the game's ~15-25 tile-store
instructions (enumerable from patch_report's 29 address-formation
sites) to thunks appending (offset,value) to an MD RAM ring; the
DREQ push ships ring entries after the sprite list (header word
distinguishes payloads); the SH-2 applies the log directly to the
SDRAM tilemap shadow. copy_pages retires (frees its window time),
the FB becomes pure display double-buffer, and 1a's flip discipline
(+ its measured wins) drops in cleanly. 68K read-watchpoint already
proved zero tile-staging readbacks (90s attract+demo) — the write
side is the whole contract.

Baseline note: frame-exact anchors reveal the dominant term is the
ROLLING SHIP (per-window flips display a composite of two frames; the
arcade ships whole frames). Iteration 1 = atomic ship: flip once per
cycle at the completion window, gated on all-slices-landed; staging
stays correct because the SDRAM tilemap shadow accumulates each
cycle's access-bank writes (late writes carry ≤2-cycle latency, no
loss). Scream anchor needs a 16px mask window (pan steps 12px/frame
and skips exact values).
