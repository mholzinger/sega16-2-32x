# LOOP 10 — the purple hue, then the compose overrun

Kickoff doc for a FRESH session. Self-contained: read this, then LOOP.md
(negatives list, especially 20-24) and LOOP9.md's two PATH sections.
**Do not read LOOP9.md top-down as a plan** — its opening frames the arc
around the blit being the bottleneck and ranks fixes off MAME numbers.
Both premises died in LOOP 9. The corrections are further down that file.

MIKE'S PRIORITY ORDER for the port: **framerate, screen tearing, sprites,
sound.** LOOP 9 moved 1 and 2 and did not finish them. Sound is untouched
and last.

## Where LOOP 9 left it (commit 47e992d)

The game is playable and Mike's verdict on the landing build was "lots of
flicker, some screen tear, but very playable" — better than the build
before it. That is the first thing the smoothness arc has won.

    ares, per window, vblank = 38 lines
                blit     wait    total   in-compose pickups
      k=0     27.08ln  7.00ln  34.08ln       35.2%
      k=1     30.05ln  0.20ln  30.25ln        0.0%
      k=2     27.09ln  0.33ln  27.42ln        2.4%

    restore past vblank  8.3% of windows (heavy scene), worst 67 lines
    vints/cycle 3.24    V-gate rejects 7.3%    blit skips 28.3%
    worst handler  total=149  window/ack=104  tail=45   (frame=262)

## THE TWO NUMBERS THAT DEFINE THIS ARC

**0.744 lines per blitted row, in every window, every session, light or
heavy.** The blit is a fixed 6.76 MB/s hardware floor. It is not the
bottleneck, it is not load-dependent, and nothing instruction-side moves
it — DMA measured 1.77x SLOWER. Stop optimising it.

**7.00 lines of wait in one window out of three.** That is the 80-row
band's compose failing to finish inside its one window gap. It scales
with sprite count. It is the largest recoverable term left and it is
PATH 1 below.

## PATH 2 FIRST — the purple sprite hue (correctness)

Mike, playing ares: **a purple hue on sprites, reduced but not gone**
under LOOP 9's change, plus grass that glitters. LOOP 8 recorded this as
a MAME-PRESSURE-only artifact that "does NOT reproduce on ares". That
line is wrong — PRESSURE was an early warning read as noise.

Mechanism understood, fix already designed: the palette lands at k0/k2
but `apply_cram` paints CRAM only at k1, so a shipped pair reaches CRAM
up to a full cycle late and the per-set memo holds the stale paint until
the next generation bump. Disabling the memo restores the colour and
costs PRESSURE demo2 40.30 -> 28.47, which is why the memo exists.

THE FIX: paint the shipped sets into CRAM in the window the pair LANDS.
It is already pre-ack there, so it is legal. Second, smaller lever from
LOOP 9's Mission 1 item 2: LOOP 8 made PAL_SETGEN bump 32 sets whenever
a region pair ships where the old path bumped the 1-2 that changed —
bump only the sets whose words actually differ.

WHY THIS ONE FIRST: it is correctness, and the standing directive is
accuracy before speed. It is also small, where PATH 1 will churn the same
code the palette timing rides on — do it second and you measure the
palette through a moving floor.

apply_cram is 0.673ms/cycle on ares (2.02x its MAME reading), so this may
pay in the window too, but that is a bonus, not the case for it.

### PATH 2 RESULT (uncommitted, awaiting Mike's ares pass)

Both levers built. Two findings changed the shape of the fix:

**Tile/text must NOT be painted at land time.** A cycle is three vints and
the blitted image stays on screen across all of them, so a CRAM write at
k0/k2 recolours pixels composed against the previous generation. Measured
on the PRESSURE title static — which IS the colour-cycling ALTERED BEAST
logo — 2.44% -> 9.72% with tile/text painted at land, 2.44% without,
sprites painted either way. Shipped: sprite regions only (r >= 8).

**The narrowed PAL_SETGEN bump is the bigger lever.** Per-set copy-and-
compare replaces the blanket 32/16-set bump. Memo skips +57% (11257 ->
17633 groups per 1197 cycles) while CRAM writes actually performed went
UP 1% (23119 -> 23339) — so no palette update is being dropped, which is
the check that rules out staleness as the cause of the demo2 move below.

    scene      normal: HEAD -> new     PRESSURE: HEAD -> new
    title       2.44    2.44  (=)       2.44    2.44  (=)
    eyehold     3.37    3.37  (=)      10.42    3.06  (-7.4)
    scream     47.14   47.14  (=)      47.90   47.14  (-0.8)
    demo       51.47   48.98  (-2.5)   51.13   50.66  (-0.5)
    demo2      21.85   19.35  (-2.5)   22.97   31.66  (+8.7)
    TOTAL      25.26   24.26  (-1.0)   26.97   26.99  (=)

Statics pass both builds, and eyehold improves 7.4 points under stress.
The one regression is PRESSURE demo2, which misses this doc's 28.5
falsifier. It is a motion scene, it tracks the narrowed bump alone
(bump-only measured 31.67), and the same scene IMPROVES 2.5 points on the
shipping build — so it reads as a pipeline PHASE shift under cut budgets,
not a lost palette. Flagging it rather than burying it: ares overrules.

Region guard 0x06018c78, 904 bytes headroom (was 952 — the cram_paint
factoring paid for most of the new code). DIAG[50] = sprite sets painted
at land time; 66 per 1197 attract cycles, and attract ships few sprite
palette regions, so gameplay is where this is worth watching.

### THE BIGGEST WIN OF THE ARC WAS NOT PATH 2

`compose_layer_regs` rendered a prescan miss with **group 1**, commented
"neutral". Group 1 is not neutral — it is a live group holding whatever
colour the allocator put there, so a missed tile drew as a solid WHITE or
YELLOW 8x8 block. That is the white slabs in the tree band and the
gravestone row flashing yellow. A miss now reuses the blank_tile path and
leaves last cycle's pixels: **Mike's verdict on the build that differed by
that one line was "the closest we have to playable yet."**

THE 5 PARITY SCENES CANNOT SEE IT. Captures are byte-identical with and
without. Attract misses 1.2% of columns; gameplay misses far more, because
the sprite-pair reservation squeezes the singles budget. Three separate
code changes produced identical scoreboards this arc — do not read that as
"no effect", read it as "attract does not exercise it."

TWO DEAD ENDS, RECORDED SO THEY ARE NOT RE-WALKED:
  1. Falling back to the colour's last group, validated against `grp_key`.
     `grp_key` is the allocator's map and runs a parity AHEAD of what
     apply_cram painted — the demo2 logo rendered in the previous owner's
     RED, 19.35 -> 31.60. Validated against `cram_key` (what CRAM actually
     holds) it is correct but fires for 0.6% of misses. Removed.
  2. Publishing sticky ownership the prescan did not count (kept — it
     closes a real deadlock where apply_cram never painted a group its
     colour genuinely owned) removed only 153 of 23283 misses. Kept for
     correctness, NOT claimed as a fix.

THE METHOD THAT SETTLED IT: a marker build that SKIPPED every tile on the
miss path. The white slabs survived unchanged, which disproved the whole
prescan-miss diagnosis in one capture — after three speculative fixes had
already been written. Build the falsifier before the third fix, not after.

Still open, and now measured: **1.3-2.0 tile colours per cycle are forced
to SHARE another colour's group.** Demand is 12-15 tile + 2-3 text
distinct colours against a singles budget (`bound`) of 19.8 — marginal,
a couple over the line every cycle. That is what actually miscolours the
tree band, and `bound = 32 - 2*need` is where the groups went.

### FLICKER — savestate from Mike's gameplay run (1830 cycles)

    vints/cycle 3.18      V-gate rejects 5.6%    blit skips 24.0%
    restore past vblank 301/5050 = 6.0%, worst 76 lines (vblank=38)
    worst handler total=156 window/ack=113 tail=43 -> 106 lines margin

STROBE CONFIRMED. Mike: flicker scales with sprite count, and with the
three-headed dogs that carry the powerups — large multi-part sprites, so
compose time. That is PATH 1's input scene.

## PATH 1 SECOND — the 80-row compose overruns its window gap

The compose launched post-ack at window k runs during k+1 and is drained
at k+1's post-ack: ONE window gap. Bands are 72/72/80 because 224 rows is
28 tile rows and 28 does not divide by three. The 72s fit; the 80 does
not, so 35.2% of the overlapping window's mailbox pickups land inside a
running compose and the master sits on SYNC[2] for 7.00 lines.

### PATH 1 CANDIDATE (b) IS DEAD — but on 2 metrics, not the 6 first claimed

FIRST READING, NOW CORRECTED. Two ares runs of the SAME shipping build
(PROBE_missmark 20:51, s16 21:37) bracket the run-to-run spread, and it is
WIDE. Judge any candidate against the range, never against one run:

    ares                       same build, 2 runs      even bands
    vints/cycle                   3.18 - 3.27             3.31   in range
    V-gate rejects                 5.6 - 8.4%             9.4%   marginal
    restore past vblank            6.1 - 9.0%            11.5%   marginal
    FS restore waits >1 line       6.6 - 9.8%            12.5%   marginal
    blit skips                   23.5 - 35.9%           52.8%   OUTSIDE
    queue-full deferrals/cycle   0.145 - 0.184           0.422   OUTSIDE
    black frames (raw capture)        10.50%            10.18%   SAME

So the verdict holds — deferrals at 2.3x the top of range, blit skips well
past it — but it rests on TWO metrics, not six, and the metric closest to
what Mike actually sees (black-frame rate) did not move at all. The
original "six metrics, all ~2x worse" was one run against one run and
overstated. DO NOT RE-PROPOSE the change; DO re-use the range.

**BASELINE, shipping build: 10.5% of frames are black** (227/2162, 168
separate runs, longest 35). That is the flicker. One frame in ten.

**AND MAME PREDICTED IT.** V-gate rejects went 13.9% -> 18.2% in MAME and
5.6% -> 9.4% in ares — same direction, same rough factor. Amend the
blindness rule: MAME cannot rank the timing MAGNITUDES (it does not model
FB write cost, and flipwait reads a flat 0.000ms there), but DIAG[7], a
discrete count of MD pickups outside the V window, DID rank this change
correctly. That counter is worth trusting as a cheap pre-ares screen.

LIKELY MECHANISM, for whoever tries the next variant: C1 gained rows
144-148 and lost 72-73, i.e. it traded sky for heavy ground rows — and C1
is composed at **k1**, the window that ALREADY carries apply_cram,
copy_pages, latch_layer_regs and the sprite snapshot. The change moved
work into the most loaded window. Evening the bands is not wrong in
itself; evening them without asking which WINDOW each band lands in is.
A variant that keeps the bands even but hands k1 the LIGHTEST one has not
been tried.

### PATH 1 ATTEMPT — candidate (b) as built (superseded by the result above)

Compose bands evened to 74/75/75 against unchanged 72/72/80 ship bands
(`rom/PROBE_bands.32x`). Disjointness re-derived and holds: C(k) runs
during k+1, and C0=[0,74) vs R2=[144,224), C1=[74,149) vs R0=[0,72),
C2=[149,224) vs R1=[72,144) — 2 and 5 rows of margin on the tight pairs.
Parity statics unmoved (title 2.44, eyehold 3.37, TOTAL 24.23), so it
introduces no seams — which is what killed the LOOP 9 ship-band version.

**MAME CANNOT EVALUATE THIS CHANGE.** `flipwait` reads 0.000ms there in
every run, so the wait PATH 1 exists to remove is not merely understated,
it is ABSENT. Add it to the MAME-blindness list next to framebuffer write
cost. The only readable proxy, the V-gate reject rate (DIAG[7]), got
WORSE: 13.9% -> 18.2%. That is a real consequence — a V-gate reject is a
skipped blit is a stale third — but LOOP.md says MAME cannot rank cadence,
and this is exactly such a call. It needs ares, so it waits for one.

Also found while mapping the bands: `ns`, the master's strip count, was
6 for bands 0 and 1 against 36 rows of work — three real strips and THREE
EMPTY ONES (idx 3,4,5 gave y >= hi). Correcting it to 4 alone did NOT
account for the reject increase (the even bands with the original 6/6/4
measured 19.2%, slightly worse still), so the empty slots are not the
load-bearing pacing they looked like.

Two candidates, neither costed:
  a. **Split the big compose across two window gaps** — needs a resumable
     compose or a second outstanding slot (SYNC[0]/SYNC[1] carries one
     command). This is the structural piece of work.
  b. **Even the COMPOSE bands without moving the SHIP bands.** The two are
     independent — compose_layer takes arbitrary row ranges, and only the
     ship set must be composed by pickup. 74/75/75 compose against
     unchanged 72/72/80 ship has never been tried.

### CANDIDATE (a) ANSWERED — it is ONE PASS, not the band. FIX SHIPPED.

    ares, Mike's dogs run (tools/pk_pass.py, rom/PROBE_pkpass.32x)
    pass            n      n%      wait%     mean
    idle          3355   87.8%      5.6%     0.16 lines
    cat1 WHOLE     401   10.5%     91.3%    21.17 lines   <-- all of it
    sprites         18    0.5%      1.3%     6.69 lines
    text            46    1.2%      1.7%     3.53 lines

**91.3% of the master's entire pickup wait is behind one pass**, and each
collision costs a mean of 21 lines — over half a vblank. (An earlier
version of this table read 45.50 lines: tools/pk_pass.py had 21.4 FRT
ticks per scanline where the real figure is ~46, so every span was
overstated 2.17x. Fixed in the tool. The RATIOS were never affected.)

The band is not too big — cat1 is the only pass with no service point
inside it AND it ran LAST, so it was still going when the next window's
pickup arrived. The wait was simply whatever remained of it.

THE FIX IS NOT STRIPING IT (that is 7f, which hit its target and lost the
play pass). It is MOVING it: a band's cat1+text are now handed to the
NEXT window gap and run FIRST, with a full gap in front of them. The
striped passes that follow are interruptible, so a late pickup costs one
strip. Same work, same order within a band, and every band still
completes before its ship — R0 striped k0 / cat1 k1 / ships k2, and so on.

Gates: parity BYTE-IDENTICAL to the approved build (24.26, statics 2.44 /
3.37). MAME V-gate screen clean — 14.0% against the approved build's
13.9%, where the even-bands loser read 18.2%. _end 0x06018d70, 656 bytes.

RESULT — matched probe builds, so probe overhead cancels. FALSIFIER MET:

                            BEFORE      AFTER     vs same-build range
    pickup wait/pickup      2.43 ln     0.58 ln   target was <2.00  MET
    in-compose pickups     12.2%        2.6%      target was <5%    MET
    cat1's share of wait   91.3%        absent    zero pickups land there
    restore past vblank     8.2%        1.2%      range 6.1-9.0  OUTSIDE, good
    V-gate rejects          7.4%        1.3%      range 5.6-8.4  OUTSIDE, good
    deferrals/cycle         0.213       0.137     range .145-.184 at/below
    vints/cycle             3.24        3.04      range 3.18-3.27 better
    blit skips             33.5%       35.5%      range 23.5-35.9 unchanged

restore-past-vblank is THE strobe metric and it fell 6.8x, from the middle
of the same-build range to a fifth of its floor. That is the arc's result.

**BUT THE TAIL GOT WORSE, AND IT IS TIGHT.** Worst handler 159 -> 243 of
262 lines: margin 103 -> **19 lines**. Worst restore overrun 76 -> 140
lines. The COMMON case improved enormously and the extreme got sharper.
Part of that is sampling (2577 cycles vs 1434, so a bigger pool to draw a
max from) but not all, and 19 lines is close enough to zero to matter.
WATCH THIS. If a build ever reports a negative margin it is a hang.

CONFIRMED AT THE BOSS (rom/s16.bs1, 4095 cycles, Mike: "flicker
diminishing, this might be the smoothest port so far"):

    V-gate rejects 1.2%   restore past vblank 1.1%   vints/cycle 3.04

Both hold under the heaviest load in the game, against a same-build
baseline range of 5.6-8.4% and 6.1-9.0%. The smoothness arc has landed.

### CHASING THE 8-LINE MARGIN — probe built, needs ares

`rom/PROBE_preack.32x` + `tools/preack_max.py`. Splits the master's
PRE-ACK window into blit / SYNC[2] pickup / SYNC[5] echo / copy_pages /
apply_cram, keeps the breakdown of the single WORST window, and adds a
tail histogram of windows over 150/175/200/225/250 lines.

THE HISTOGRAM IS THE POINT. A lone freak at 254 lines and a fat tail
sitting near 250 need opposite responses and the max cannot tell them
apart. Every worst-handler number this arc came from a different run
length (643 to 4095 cycles) and the two worst were the two longest, so
sampling is a live alternative explanation that has to be excluded first.

MAME sanity run (mean 8.9 lines, worst 26.5, zero windows over 150 —
MAME's window is a fifth of ares's, as expected):

    worst MAME window, k=1:  copy_pages 13.2  blit 6.0  cram 4.7
                             pickup 0.0  echo 0.0  other 2.6

### RESULT — THE 8-LINE MARGIN IS NOT WHAT IT LOOKS LIKE. ALARM RETRACTED.

ares, boss run, 11800 windows (rom/PROBE_preack.bs1):

    mean pre-ack 33.1 lines of 262      worst 130.9 lines (k=2)
    windows over 150 lines:  0          over 200: 0    over 250: 0

    worst window, by term:  SYNC[2] pickup wait 95.0   blit 31.7
                            copy_pages 0.0   apply_cram 0.0   other 4.2

**THE MASTER'S PRE-ACK WINDOW NEVER EXCEEDS 131 LINES, IN 11800 SAMPLES.**
Mean 33.1. The port is nowhere near the frame edge and the 8-line margin
is not the operating point — I raised that alarm off a single max from
the longest run of the session, which is exactly the sampling trap this
file warns about two sections up. Retracted.

**BUT THE SAME RUN'S MD-SIDE READING IS window/ack = 210 LINES.** The
master says 131 max, the MD says 210 worst, and they are the same run.
That ~79-line gap is time the MD is already spinning BEFORE the master
starts its window — master PICKUP LATENCY, not master work. That is the
real term behind every worst-handler figure in this file, and no probe
has ever measured it.

SUSPECT, UNVERIFIED: `build_maps` is ~4ms = ~63 lines and explicitly
uninterruptible (see the dt<=8000 deferral gate); a vint arriving inside
it cannot be answered until it finishes. The bq maintenance slots and
cache_fill are the other post-ack candidates. Instrument the gap itself —
MD vint edge to master window start — before touching anything.

COPY_PAGES IS CLEARED. It was 0.0 lines in the worst window (which was
k=2, and copy_pages only runs at k=1). The 8.92x-scaled MAME reasoning
that made it prime suspect was wrong, in the specific way this file keeps
warning about: a MAME term scaled into an ares claim.

Also fixed in tools/preack_max.py: the `blit` slot is diag_add(5), which
is "blit+preempt" and ALREADY CONTAINS the pickup wait. Unsubtracted it
double counts and drives the remainder to -90.8 lines.

### (dead) PRIME SUSPECT: copy_pages. It is the largest term in MAME's worst
window, it runs ONLY at k=1 (which is where the worst window landed), and
it is the term MAME understates MOST — 8.92x per the blindness table,
against blit's 5.05x and apply_cram's 2.02x. 13.2 MAME lines scaled by
8.92 is ~118 ares lines, which would be over half the 211-line window/ack
on its own. The flood path gives it a budget of 7 pages
(`pg_pending >= 0x0FFF ? 7 : 3`); a 7-page window is the thing to look
for in the worst-window breakdown.

VERIFY BEFORE BUILDING. This is a scaled MAME number reasoning about an
ares effect, which is exactly the move that has been wrong all arc.

**HANDLER MARGIN IS DOWN TO 8 LINES (total 254 of 262).** It was 103
before this fix, 19 in the pkpass2 run, 8 at the boss. The trend is
monotonic and the floor is zero — at zero the MD handler does not finish
inside the frame. Treat any further change that grows window/ack as
hostile until this is understood. It is the top risk in the port.

### MIKE'S CHAIN — TESTED, NOT SUPPORTED

ares, 2451 harvests, bucketed by the PREVIOUS window's pre-ack span:

    complete land   n=2115   mean 29.9 lines   max 98.4
    SHORT land      n= 336   mean 26.6 lines   max 97.5    separation -3.3

Short lands sit behind marginally SHORTER windows. Ack latency is not why
the sprite list truncates, so fixing the worker will not fix the sprites
as a side effect. state_health's original rule stands: push_aborts==0
means the 68K pushed the whole packet, so SPLIT THE PACKET. Shortfall is
a mean of 12 words against a worst of 258 — most short lands are near
misses, a few lose a quarter of the list.

### THE SKIP-ON-MISS TRAP — a regression I shipped and Mike caught

`tmiss[c] = 1` on a prescan miss renders nothing and leaves last cycle's
pixels. In the FG passes that is the right trade. In the OPAQUE BG pass
it is not: that pass must write EVERY pixel, and SKIP MEANS NEVER UPDATE.
A colour that misses keeps missing, so one bad frame during an area
transition — tilemap mid-load, placeholder codes — froze permanently. On
Mike's screen: a repeating glyph tiled over sky, cliff and ground alike,
a black blob over the player, a purple band across the bottom.

Fixed as `tmiss[c] = !opaque` — FG skips (stale beats wrong colour), BG
stays live (wrong colour beats frozen). The tell was in the original
comment: "leaves last cycle's pixels". Nobody asked what happens to a
tile that misses EVERY cycle. Ask that of any skip.

### MINED FROM 32x-builder — the doorbell read is a bus-bandwidth cost

That project measured (DEVLOG.md:374-400) an SH-2 polling a comm register
at ~3M reads/sec DELAYING AND DROPPING the 68K's own MMIO traffic, and
fixed it by throttling to ~30K/sec. It recurred with TAS work-stealing at
115K-190K atomic bus ops/sec and was reverted twice.

MEASURED HERE: our master's main loop reads MARS_SYS_COMM0 **2774 times
per window = ~166,000 adapter-MMIO reads/sec** — inside their harmful
band. Our 68K pushes the sprite list over DREQ immediately after the ack,
into the same register space this loop is hammering, and that list lands
SHORT on 21% of cycles. Ack latency was already RULED OUT as the cause
(see Mike's chain above), so this is the live hypothesis.

`rom/PROBE_throttle.32x` (`-DPOLL_THROTTLE`). THROTTLES THE READ, NOT THE
LOOP — the loop body is the band-compose worker and slowing it starves
compose; the dt<=4000 gate's own comment records un-gated maintenance at
94% blit skips against a 31% baseline. So the loop spins at full rate and
only the external read is rate-limited to once per scanline, using the
ON-CHIP FRT (no external bus cycles). Budget: the V-gate accepts a 6-line
window, so 1 line of added pickup latency buys a ~10x traffic cut.

    MAME:  blit skips 14.0% -> 15.0%, blitpre 1.017 -> 1.168ms
Exactly the predicted <=1 line of pickup cost. THE BENEFIT IS INVISIBLE
IN MAME (dreq_incomplete reads 0 there). FALSIFIER, ares:
dreq_incomplete must fall from its 20-21% baseline; abort if blit skips
rise materially above the 23.5-43.5% range.

### CURRENT TARGET — blit skips, and the unmeasured pickup latency

Mike, on the fixed build: "closer to a smooth arcade port, still lots of
screen tearing, plenty of new slowdown, but LOOKING better."

    ares, 6511 cycles: vints/cycle 3.03   V-gate 0.9%   restore>vblank 0.9%
                       blit skips 43.5%   worst handler 246 (16 lines)

V-gate rejects and restore-past-vblank are at their best ever. **Blit
skips are at their worst: 43.5% against a same-build baseline range of
23.5-35.9%.** A skipped blit is a third that does not ship — the screen
holds a stale band for a cycle. That is exactly "tearing plus slowdown"
and it is the one metric outside its range.

MECHANISM, AND IT IS ALREADY INSTRUMENTED: the skip test is
`skip = (v < 0xDF || v > 0xE4)` on the MD's heartbeat V at pickup. A skip
means the master REACHED the window at the wrong scanline — pickup
latency, the same ~79-line term that separates the master's 131-line max
window from the MD's 210-line window/ack. `make SPANPROBE=1` already
histograms pickup-V ([34..41]); `tools/span_hist.py` reads it.
`rom/PROBE_span2.32x` is built.

NOTE: DIAG[50] was a collision — SPAN_PROBE owns [34..51] and the
shipping land-paint counter sat on [50]. Moved to [52]. Any reading of
[50] from a state before 2026-08-07 00:20 mixes the two.

### (superseded) MIKE'S CHAIN — the one hypothesis worth testing next

Mike: "I bet the sprite flicker starts to resolve once we tackle the
worker." The chain, and it fits the measurements better than anything
this file has proposed:

    master picks up the window late  ->  ack lands late
      ->  the MD's tail is squeezed  ->  its sprite-list push truncates
      ->  landed < plen              ->  SPR_SNAP keeps last cycle's list
      ->  sprites pop                ->  "sprite flicker, not screen flicker"

It is supported by arithmetic already on file: at the worst handler the MD
has 262 - 253 = 9 lines of frame left, and the push has to fit in a tail
of 43. Squeeze the tail and the packet cannot land. It also explains why
dreq_incomplete rises with scene weight (13.9% light, 21.1% at the boss)
without needing the packet size to change at all.

`rom/PROBE_dreqcorr.32x` + `tools/dreq_corr.py` test it directly: every
harvest is bucketed by the PREVIOUS window's pre-ack span, which is
exactly the time the MD did not have. Real chain => short lands sit behind
visibly longer windows. MAME cannot decide it (0.2% short there against
ares's 21%) but the plumbing is verified, and even at n=7 the short lands
sat behind longer windows — 12.0 lines against 8.9.

IF IT COMES BACK FLAT, the answer is already written down: push_aborts==0
means the 68K pushed the whole packet, so SPLIT THE PACKET, and ack
latency is a separate problem that does not touch sprites.

MIKE'S REMAINING REPORT IS **SPRITE** FLICKER, EXPLICITLY NOT SCREEN
FLICKER. Leading suspect, and the tooling already names the fix:
`dreq_incomplete = 818 (20.0% of cycles)` with `push_aborts = 0`. Per
state_health's own decision rule, aborts==0 means the 68K pushed the
whole packet and the DMA still did not drain, so the packet must be
SPLIT — a bigger spin budget is useless. On an incomplete land the code
keeps last cycle's SPR_SNAP ("stale beats torn"), so one cycle in five
the entire sprite list is a cycle old. VERIFY BEFORE BUILDING: this arc
produced three confident diagnoses that a single falsifier killed.

NEXT RESIDUAL, for whoever picks this up: `sprites` now carries 69.6% of
the (4.2x smaller) remaining wait — 160 pickups at a mean of 37.11 lines.
That pass IS striped at 12 rows, so a strip should not cost 37 lines; the
suspect is the full-height row-walk of tall zoomed actors being repeated
per strip (see the comment at the sprite loop). Measure before believing.

### (superseded) CANDIDATE (a) WAS BLOCKED ON ONE MEASUREMENT

Before restructuring the compose, know WHICH PASS the master waits behind.
The slave's compose is four striped passes (12 rows, mailbox serviced
between strips) plus `cat1` WHOLE-BAND and `text`. cat1 is uninterruptible
BY DESIGN — 7f striped it, hit its target and lost the play pass on sprite
artifacts. If the wait concentrates there, (a) is a small targeted change;
if it is spread across the striped passes, the band is simply too big and
the split has to be structural. Those are different pieces of work and
LOOP 9's lesson ("BUSY IS NOT COLLIDED") is exactly this mistake.

`rom/PROBE_pkpass.32x` + `tools/pk_pass.py`. The master stamps the pass
the slave was running when it posted the blit command, then charges the
whole SYNC[2] wait to it.

MAME CANNOT ANSWER IT — measured there, 99.9% of 3421 pickups find the
slave already IDLE, because MAME does not model FB write cost so the
compose finishes inside a gap ares blows through. The one pickup that did
land in a pass landed in `cat1 WHOLE` and cost 16.07 lines, the largest
single wait in the run; `text` cost 2.66. n=1, so it is a hint, not a
result. Run the probe on ares.

DO NOT REACH FOR MORE MAILBOX-POLL POINTS. That is iteration 7f: it
bounded pickup latency, hit its target metric, and lost the arc on sprite
artifacts. Polling more often cannot shorten work that has not been done.

MEASURE IT ON SPRITES. Mike: flicker is constant when the level-1
gravestones rise. Sprites are what the compose spends its time on, so a
heavy-sprite scene is the input — the title screen will show nothing.

## Gates (every commit)

1. Boots + full attract, NO hang.
2. `tools/parity_run.sh` on a CLEAN probe-free build. **Judge the STATICS**
   (title, eyehold) — they are render truth. Motion scenes are phase-noisy
   and ares overrules them. Reference, commit 47e992d: title 2.44,
   eyehold 3.37, scream 47.14, demo 51.47, demo2 21.85, TOTAL 25.26.
3. Region guard (`grep ' _end$' rom/s16.lst`, limit 0x06019000; currently
   0x06018c48, 952 bytes headroom), rom stamped `normal`, `make PRESSURE=1`
   builds and runs.
4. **PLAYABILITY PRESERVED — Mike's ares pass is the acceptance gate**, and
   it has overruled the metrics twice.

## Falsifier

PATH 2: the purple hue is gone on Mike's ares pass, with PRESSURE demo2
no worse than 28.5 and the statics unmoved.

PATH 1: `tools/wait_split.py` — the 7.00-line wait under 2 lines and
in-compose pickups under 5% in a heavy-sprite scene, with worst-window
total under 30 lines. Cross-check black-frame rate with
`./capture.sh raw` (NOT dedup — see below) from the SAME run as the
savestate.

## HARD-WON LESSONS (do not re-learn these)

- **MAME DOES NOT MODEL FRAMEBUFFER WRITE COST.** Cached and uncached FB
  aliases read byte-identical there; ares charges 5x. Per-term ares/MAME:
  blit 5.05x, copy_pages 8.92x, apply_cram 2.02x, TOTALwin 3.80x — every
  term understated by a DIFFERENT factor, so MAME cannot even RANK them.
  Iterate on MAME for CORRECTNESS; take every timing verdict from ares.
  Budget for the round trip. (TOOLKIT.md has the null test to re-run this
  on any future title.)
- **NEVER LEAVE A PROBE OR A LOSING CANDIDATE AT `rom/s16.32x`.** A build
  with the deliberately-slow uncached FB path sat at the default rom path
  for 21 minutes and Mike played it; an hour of analysis went into
  explaining flicker that the diagnostic had caused. Probe builds get
  their own `rom/PROBE_*.32x` filename, always.
- **A CONFIRMED MECHANISM IS NOT A LICENCE TO SHIP THE FIRST FIX.** 7f and
  LOOP 9's blit-thirds rebalance both hit their target metric and lost the
  play pass. Four builds in LOOP 9 passed every mechanical gate and lost.
- **AN A/B IS ONLY AN A/B IF THE CONTENT MATCHES.** Single-session ares
  numbers vary ~14x by content. Two savestates read 0.2% vs 10.1% on the
  same metric and it was a light scene against a heavy one, not a result.
- **SAVESTATE COUNTERS ARE CUMULATIVE FROM BOOT; a capture is a slice.**
  0/655 overruns alongside 12.4% black frames is not a contradiction if
  the state was saved before the footage. Save the state at the END of the
  run you captured.
- **`./capture.sh raw` for any per-frame RATE.** Dedup deletes frames that
  match their neighbour, but an overrun frame differs from its neighbour
  by definition, so the survivors are enriched for exactly what you are
  counting. LOOP 7i's phantom "3.3x second cause" was this.
- **STATE THE INVARIANT, THEN MEASURE IT.** The code asserted for nine
  iterations that the blit set and the outstanding compose were "DISJOINT
  by pipeline construction". They were the same band, in all three
  windows, the whole time.
- **"BUSY" IS NOT "COLLIDED".** LOOP 9 diagnosed a data dependency from a
  probe that only measured whether the slave was inside its compose. The
  fix worked and the diagnosis was wrong. Check what a counter counts.

## Probes available (all `make FLAG=1`, all NEVER SHIP)

    WINSPLIT   blit-only vs post-blit waits, rows blitted   win_probe.lua
    SPANPROBE  pickup-V + blit-span histograms, per-k       span_hist.py
    WAITSPLIT  per-window blit vs SYNC[2] wait              wait_split.py
    PICKUPSRC  slave answers mailbox in-compose vs idle     wait_split.py
    ROWSTALE   rows identical to one cycle ago              win_probe.lua
    FMTEST     do SH-2 FB writes land with FM=0             (inline python)
    TAILPROBE  MD tail split + means                        win_probe.lua

Counters live in DIAG (0x28000, 64 slots, 0-61 used) and the 28D00-28FFF
hole. Put new ones where a SAVESTATE reader can find them — ares is the
target that matters and it cannot be scripted.

## Ruled out — do not re-propose (LOOP.md 20-24 has the numbers)

- DMAC channel 1 for the blit: 1.77x slower on ares, 14% of rows dropped.
- Dirty-row blit: 13-17% skippable during scroll against a ~25% break-even.
- Evening the blit thirds: seams, reverted on the play pass.
- Evening the COMPOSE thirds to 74/75/75 (LOOP 10): no seams, statics
  unmoved, and ares still ~2x worse on all six cadence metrics. See the
  PATH 1 section — it loaded k1, the window that was already fullest.
- Composing straight into the FB / shadow bank: FM=0 writes land only
  85.8% of the time, which is worse than a flat no.
- Late window pickup: contributes 0 of the overruns.
- Striping the cat1 pass (7f), splitting past thirds, skip-on-predict,
  atomic ship.

## Not in this arc (open, ranked)

1. **SPRITES** (Mike's #3) — next correctness arc after smoothness.
   `tools/objdiff_32x.lua` differs object slots against the arcade driver.
2. **blit skips 28.3% of cycles** — each is a stale third. Re-measure
   after PATH 1 before treating it as its own arc.
3. **SOUND** (Mike's #4). No Z80, no PWM. Untouched, and last.
