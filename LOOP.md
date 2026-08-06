# The Parity Loop

> ACTIVE ARC: **LOOP7.md** — kill the tail (COMM -> DREQ, retire the
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
