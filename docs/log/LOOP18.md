# LOOP 18 — STOP RACING THE BEAM: make the frame cost what MOVED,
# not what the screen is. HANDOFF KICKOFF.

Fresh session: read this, then `CLAUDE.md` ("What MAME is for now"),
then `TOOLKIT.md`. docs/log/LOOP17.md has the full working log of the day this
came out of. Where LOOPs and ARCHITECTURE.md disagree, ARCHITECTURE
wins. Memory `release-bar-flawless` is the bar.

**TOOLKIT.md IS THE DELIVERABLE** (Mike, emphatically): the port is a
vehicle for a reusable System 16 -> 32X kit. Update TOOLKIT.md AS work
lands, not at the end of the loop.

## WHERE THIS STANDS (2026-08-17, all ares-verified)

**Canonical build:**

    make MDBGALL=1 BQCHUNK=1 CUTBLANK=1 NTWRAP=1 WIN2=1 SPRTRUNC=1

BUILD ed0954c1, region guard `_end 0x06018EE8` (280 bytes spare).
**ALWAYS `python3 tools/build_id.py show rom/s16.32x` BEFORE BELIEVING A
STATE** — a whole measurement round was lost to a line-wrapped make
command that silently dropped four flags.

Mike's play state on this build: **68K handler mean 91.3 lines/vint —
the game gets ~65% of the MD 68K (~50% of the arcade)**, up from 110 /
~58% at the start of LOOP17. Cadence 3.00, V-gate rejects 0.0%,
flip/blit skips 0.3%, dreq_incomplete 0.0%, misaligned 1, slave idle
13,814 polls/cycle. Handler split: window/ack 44.9 vs own-tail 46.5 —
**the two halves are now EQUAL; the tail is no longer the lever.**

Mike's feel on it: "plays faster, frame drops, screen tearing present
but not severe."

### What LOOP17 landed (all committed, nothing pushed)

  - **cat-1 fix** (in the canonical build, `#ifdef WIN_TWO`): WIN_TWO
    chains all three slave bands at k2, so the LAST band R2 (rows
    144..183) had no successor call to drain its deferred FG cat-1 —
    the over-sprite tiles were lost every frame, and the arcade's tall
    grass blades came out a flat strip. R2 now drains at its own tail.
    Mike: "RESTORED!". Costs nothing measurable on identical play.
  - **`SPRTRUNC=1`** — push only the LIVE sprite records (mean 12.5 of
    64). handler mean 106.9 -> 91.3. **Now part of the canonical line.**
  - **`SPRBAKE=1`** — 477 pre-decoded sprite frames, 659KB blob, with a
    build-time pixel-identity gate. Worth ~5 lines on ares (110 ->
    105.2) but **SPRBAKE + SPRTRUNC are 72 bytes OVER the region guard
    together** (_end 0x06019048). Truncation wins on measurement, so
    SPRBAKE is currently parked. Buying those 72 bytes is a
    relocation-playbook job.
  - **`CUT30=1`** — drops the idle beat: cadence 2.04, a real 30Hz
    display. **Bad trade unconditionally**: handler mean 137.8, game
    ~47%, skips 3.8%, window/ack 69.0. The MASTER cannot sustain a
    2-vint cycle. Keep the flag for the CUTSCENE-GATED version (scene
    state 0xFFF031), where the game logic is idle and the CPU is
    affordable. Revisit after the blit gets cheaper.
  - **`DRQPROBE=1` + `tools/drq_probe.py`** — settled that **partial
    DREQ landings ARE readable on ares** (233 of 234 short pushes read
    their true length) and are **invisible in MAME** (0 of 62). This
    unblocked SPRTRUNC and is now a kit invariant.
  - **`tools/health_mame.lua`** — state_health's meters read live in
    MAME on a scripted play, for cheap A/Bs without spending an ares
    pass. MD-side counters only; read it as a RANKING, never a clock.

## THE MEASUREMENTS THAT DEFINE THIS LOOP

**The blit is the binding constraint, and its cost is BYTES WRITTEN.**

  - **80% of the blit is an FB-write bus-stall floor** (LOOP 9, recorded
    in m_main.c): cached vs uncached FB writes measure 47.34 vs 47.46
    us/row, and a DMAC blit was **1.77x SLOWER**. No instruction-side
    rewrite can move it. An SDRAM read is ~5x cheaper than an FB write,
    so **comparing before writing pays for itself past ~25% skippable.**
  - **The frame pipe is mostly empty** (ares, canonical bundle, 221,322
    rows): **62.7% of 32px GROUPS entirely transparent**, 31.9% of whole
    ROWS, 79.4% of AREA. The emptiness is SCATTERED — which is why
    group, not row, is the granularity worth testing at (~63% vs ~32%).
    Both clear the 25% break-even.
  - **This only became true after MDBGALL.** LOOP 9 measured 13-17%
    skippable with the BG still in the framebuffer dirtying every row.
    Re-measure any pre-pivot number before building on it.
  - **The slave is idle** ~13,800 polls/cycle. The second SH-2 is
    largely spectating.

## THE JOB, in order

1. **BLITSKIP attempt 2 — get the BANK IDENTITY right.**
   Attempt 1 is written up in LOOP17 and was REVERTED. The mechanism is
   sound and nearly free: in `blit_half`, OR the 8 longs of each 32px
   group (the blit already LOADS them to store them) and skip the 8
   stores when the group is zero AND this bank already holds zero there.
   Per-bank per-row 10-bit mask, 896B, at the **0x3A000 block ("missq —
   2.3KB nobody owns")**.
   **WHY IT FAILED:** the bank was sniffed per call as
   `MARS_VDP_FBCTL & MARS_VDP_FS`. A window does a first-bank blit, the
   FLIP, then a second-bank blit, and BOTH CPUs call `blit_half` around
   an edge the MASTER owns — the slave can read FS on the wrong side and
   use the wrong half of the mask. Result: skipped writes that were
   needed and stale groups marked clear (Zeus's head vanished, a yellow
   block stood).
   **THE FIX:** take the bank from the flip protocol that already
   sequences this — `SYNC[2]` (slave step 1 = first-bank blit done, 2 =
   second-bank) and `SYNC[3]` (master: flip latched, second bank
   writable). Pass it in or read it from that state. Do not sniff.
   **VALIDATION RECIPE** (this is how attempt 1 was caught in ONE run):
   build it **WITHOUT SPRTRUNC** so MAME renders faithfully, then diff
   gameplay frames against the same build without BLITSKIP. Any pixel
   difference is a bug. MAME **cannot** measure the win (it models the
   ~2.7us instruction issue, not the 47us stall — it showed the cost of
   the compare and none of the saving), so the SPEED verdict is ares.

2. **Then the real prize: compose and ship only what CHANGED.**
   MDBGALL is what unlocks this — the background is on the MD plane, so
   the 32X surface holds sprites over transparency. The classic
   dirty-rect loop becomes available: clear last frame's sprite rects,
   draw this frame's, touch nothing else. Work becomes proportional to
   **what moved**, not to the screen. With 79.4% of area empty that is a
   change of ORDER, not a percentage.
   Same discipline as job 1: it needs per-bank state, because each bank
   is TWO frames stale, not one. Do job 1 first — it is the same skill
   at smaller scale and it teaches the flip protocol's real contract.

3. **Spend the idle slave** (~13,800 polls/cycle) once the blit is
   cheaper and the balance has moved.

4. **Then revisit CUT30, cutscene-gated.** 30Hz was unaffordable because
   the master could not sustain a 2-vint cycle. A cheaper blit changes
   that arithmetic; re-measure rather than inheriting the verdict.

5. Parked: SPRBAKE (needs 72 bytes), sprite-list delta encoding (73% of
   decode jobs repeat frame to frame — SPRTRUNC took the easy half),
   60Hz MD-plane scroll (item 4c: the horizontal half collides with the
   NT_WRAP per-strip hscroll protocol and needs the wrap placement
   geometry — scope it before starting).

## AN IDEA THAT IS ALREADY SETTLED — do not re-run it

**"Bake bigger units (32x32 tiles) to shrink the frame."** It does not
help, and the reason is worth keeping:
  - The blit's cost is BYTES WRITTEN. Assembling from bigger source
    units writes exactly as many bytes as assembling from 8x8 ones.
  - Sprites are ALREADY baked at full-FRAME granularity, not tiles.
  - BG and FG cat-0 are drawn by the **MD VDP**, whose tile size is 8x8
    **in hardware** — not our choice.
  - What is left on the 32X (FG cat-1, text) is sparse and is not where
    the time goes.
It is the right instinct for a COMPOSE-bound renderer. We are
BLIT-bound. The lever is writing fewer bytes, not assembling from
bigger pieces.

## INSTRUMENTS

  - `tools/state_health.py <state>` — the one paste. Handler mean +
    split, cadence, skips, rejects, DREQ health, and now the MD_PAYOFF
    transparency ratios.
  - `tools/health_mame.lua` — the same MD-side meters live in MAME on a
    scripted play. Cheap A/Bs; a RANKING, not a clock.
  - `tools/drq_probe.py` + `make DRQPROBE=1` — partial-DREQ-landing
    probe. Already answered; keep for any future transport change.
  - `make MDPAYOFF=1` — transparency ratios. Inflates the blit (it
    scans); IGNORE every timing on that build.
  - `tools/sprite_discover.*`, `tools/bake_sprites.py` — the sprite bake
    pipeline, with its build-time pixel-identity gate.
  - `tools/parity_run.sh` — our pixels vs the ARCADE's. Gates the
    SHIPPING rom.
  - Mike's `./capture.sh raw` corpus in `screenshots/` — ares truth for
    look and feel.

## TRAPS (paid for; do not re-learn)

  - **CHECK THE BUILD STAMP.** `python3 tools/build_id.py show
    rom/s16.32x`. A line-wrapped make command dropped four flags and
    produced a full round of measurements of the wrong rom; the
    counters screamed it (misaligned 118 = NT_WRAP off, slave idle
    149/cycle = WIN2 off) but the stamp said it instantly.
  - **MAME'S ROLE IS SPLIT** — see CLAUDE.md. `mame altbeast` is the
    look/feel ORACLE. `mame 32x -cart` is a convenience model, NOT an
    authority: no SH-2 timing, no ares FIFO loss, no partial DREQ
    landings, and **no FB write bus-stall floor**.
  - **"MAME CANNOT GATE THIS BUILD" IS A BANNED PHRASE.** It collapses
    two unrelated, much narrower limits and has already talked sessions
    out of cheap valid tests. The two:
    (a) correctness that depends on partial DREQ landings cannot be
        PIXEL-gated in MAME (SPRTRUNC — different code path there);
    (b) a payoff made of fewer FB writes cannot be SPEED-ranked in MAME
        (BLITSKIP — MAME charges the 2.7us issue, not the 47us stall).
    They do not overlap. BLITSKIP's CORRECTNESS gates in MAME better
    than on ares; SPRTRUNC's SPEED ranks in MAME fine. Everything else
    gates normally. "It renders wrong in MAME" is not evidence of a bug
    ONLY under (a).
  - **ares is cycle-accurate; treat its behaviour AS hardware.** It has
    overruled MAME on every disagreement this loop (the bake measured
    speed-NEUTRAL in MAME and moved the handler mean 110 -> 105.2 on
    ares).
  - **A DEFERRED PASS NEEDS A SUCCESSOR.** Single-slot deferral is only
    safe while a next call exists in the same frame; WIN_TWO's chained
    bands broke that and the last band's layer vanished silently.
  - **CODE SIZE IS NOT LINEAR IN SOURCE.** Removing a loop's trip
    counter GREW .ramtext by 24 bytes; `noinline` grew it 40. Measure
    the region guard after every edit; never reason about it.
  - **GNU Make 3.81** — no `&:` grouped targets.
  - **A LINK-ONLY FLAG IS INVISIBLE TO `.build_flags`.** Give every flag
    a `-D` so the stamp changes and objects rebuild.
  - `-Werror=return-type` is on both compilers: a missing `return`
    handed back r0, which got used as a frame pointer and DRAWN.
  - Fixed map is FULL. 0x39800 gap ends 0x399E8 (24B left). The
    **0x3A000 block has ~2.3KB nobody owns** — that is where new
    per-bank state goes.
  - Master stack dips >=576B below 0x3F000 — nothing above 0x3ED80.
  - The DREQ protocol: change push and apply in the SAME commit, whole
    and gated, or not at all.
  - Text below 2 chunks/cycle = documented regression. IDLETOKEN /
    CMDINT / PGROTOR / restore-narrow: dead, see LOOP15/16.

## RESULT — BLITSKIP ATTEMPT 2 IS CORRECT. SPEED VERDICT PENDING ARES.

`make ... BLITSKIP=1`. Canonical bundle builds at `_end 0x06018FA8`
(88 bytes under the guard, down from 280 — BLITSKIP costs 192B).
**The shipping rom is untouched**: the extra argument only exists under
the flag (`BLIT_HALF` macro), plain `make` still lands `_end 0x06018BB0`
and the parity statics still read **title 2.44 dx=0 / eyehold 3.37
dx=0**, both exact.

**THE FIX.** The master owns the only FS write in the program (the k2
flip), so it keeps the bank parity itself (`fb_draw_par`), toggles it at
its own FBCTL write — committed at the WRITE, not the latch — and
publishes it to the slave in **bit 6 of the blit command word**, posted
to SYNC[4] *after* the flip. Bit 3 is the vestigial `skip` field and
bits 0-2 are `bank1` (the game's TILE bank, unrelated), so 6 is the
first free bit. Nothing reads FBCTL. The mask itself is UNCACHED at
0x3A300 (896B; missq ends 0x3A300, cache_tag starts 0x3A800, so 384B
still spare) — attempt 1 died of two CPUs disagreeing about the bank
and a stale cache line is the same bug in a different hat. One 16-bit
read plus at most one 16-bit write per row.

**CORRECTNESS, and the measurement that actually settles it:**

  - **MASK-LIED = 0 in 2,299,352 skips**, attract *and* real gameplay
    (`make ... BLITSKIP=1 SKIPVERIFY=1` + `tools/blitskip_probe.lua`).
    Every skipped group was read back from the framebuffer UNCACHED and
    genuinely held zero. Immune to timing phase, which is the whole
    point — see the negative results below.
  - **THE VERIFIER WAS FALSIFIED FIRST.** Collapse both banks onto one
    mask (one line) and the same run reports **322,809 lies**. A checker
    that cannot fail proves nothing.
  - Skip rate **57% of groups** (slave 67%, master 47%) — squarely in
    line with the 62.7%-transparent prediction, the gap being the groups
    that just went empty and must still be zeroed once.

## NEGATIVE RESULTS — THE INSTRUMENTS THAT CANNOT ANSWER THIS

Four dead ends, all of them plausible, all of them run today. The
general lesson is in TOOLKIT.md ("When a pixel diff cannot answer the
question"); the specifics:

1. **THE PARITY GATE CANNOT SEE BLITSKIP AT ALL.** In the shipping
   configuration the background is still composited into the
   framebuffer, so no group is ever empty: the skip fired **0 times in
   2,011,300 groups**. Every parity number produced on that rom was
   measured on a code path that never executed. *Before gating a change,
   confirm the gate can see it.*
2. **`title` 2.44 -> 2.63 on the shipping rom is NOT the skip.** The
   never-take-the-skip control (`NOSKIP=1`, identical code path)
   reproduced **2.63 exactly**. It is the added instructions moving
   MAME's SH-2 timing so the pipeline settles to a different phase at
   the anchor frame. A parity static is sensitive to *any* code added to
   the blit — do not read a small static movement as corruption without
   this control.
3. **Frame-number-anchored A/B (`play_32x.lua`) is INVALID for any
   timing-perturbing flag.** The two builds were photographed in
   different GAME STATES — one life vs two, different enemies, 64% of
   pixels different. The harness is deterministic (same rom twice =
   byte-identical PNGs); the divergence is real and it is not a bug.
4. **`NOSKIP=1` is a bad control under MDBGALL.** With the skip never
   taken it writes its mask word on nearly every row instead of rarely,
   and those extra uncached writes blow the blit's window budget: the
   title screen came out in dropped black bands that looked exactly like
   the corruption being hunted. Kept for the shipping-config test in (2),
   where it is cheap; do not trust it where the skip rate is high.

Also noted, so nobody chases it: **the MDBGALL build has no ALTERED
BEAST title logo in MAME.** Pre-existing — the baseline without BLITSKIP
is missing it identically, and the two are byte-identical on that scene.

## WHAT IS LEFT ON JOB 1

**The speed verdict is Mike's ares pass** and nothing else can give it —
MAME charges the ~2.7us instruction issue and not the ~47us/row FB
stall, so it shows the cost of the test and none of the saving. Build:

    make MDBGALL=1 BQCHUNK=1 CUTBLANK=1 NTWRAP=1 WIN2=1 SPRTRUNC=1 BLITSKIP=1

stamped `BLITSKIP`, `_end 0x06018FA8`. Baseline to beat: handler mean
**91.3**. Watch flip/blit skips and V-gate rejects as the falsifier — if
the mask work costs more than the stores it saves they climb.

Note for job 2 (dirty-rect compose): 88 bytes of region guard is not
enough to build in. Buying space is now on the critical path, and
`SPRBAKE` (parked, needs 72B) is queued behind the same purchase.

## THE ARES A/B — AND THE PREMISE THIS LOOP OPENED ON IS DEAD

Two of Mike's passes, IDENTICAL bundles (`MDBGALL BQCHUNK NTWRAP WIN2
SPRTRUNC WINSPLIT`, no CUTBLANK — it was 56 bytes over the guard),
differing only in BLITSKIP. Control `a4555860`, skip `ab87376e`.

                          control    BLITSKIP
      68K handler mean      91.1       88.1     -3.0
      window/ack            44.5       41.2     -3.3
      own-tail              46.5       46.8     flat
      blit (DIAG[23])       27.8       23.8     -14.4%
      ticks/row             50.4       43.3     -14.1%
      post-blit wait         1.5        3.7     +2.2
      slave idle/cycle     13,210     16,135
      flip/blit skips        2.9%       2.2%

**BLITSKIP IS A KEEPER: 91.1 -> 88.1 on identical bundles**, ~3.3% of
the 68K back, correctness proven (0 lies in 2.3M skips), 192 bytes.
**And it is EXONERATED on the skip regression** — the control drops MORE
frames than it does (2.9% vs 2.2%), so the ~2% is this bundle or play
variance, not the flag.

### The number that matters: 57% of stores removed bought 14% of the blit

That is the whole finding. Work it through:

  - The blit is **30.5% of the handler mean** (27.8 of 91.1 lines/vint).
  - Removing **57%** of its long stores removed **14.1%** of its cost.
  - So only **~25% of the blit's cost scales with the number of longs
    stored. ~75% is fixed per row.**

**THEREFORE `80% OF THE BLIT IS AN FB-WRITE BUS-STALL FLOOR` IS RETIRED.**
It is a LOOP 9 measurement, taken with the background still in the
framebuffer, and it does not survive contact with this A/B. The
companion premise it travelled with — "an SDRAM read is ~5x cheaper than
an FB write, so comparing before writing pays past ~25% skippable" —
goes with it: reads are evidently NOT cheap relative to writes here,
because we kept 100% of the loads and lost only 14% of the cost.
Anything that cites either number needs re-deriving. (CLAUDE.md rule 3,
earned again: re-read the counter before you build on it.)

**FIRST, VERIFY THE IMPLEMENTATION IS NOT THE EXPLANATION — it is not.**
Disassembled `_blit_half` (6001ac4) on the shipped ELF: the 8 longs are
loaded ONCE into r12/r11/r10/r9/r8/r0/r4/r5, OR-chained into r1, and the
SAME registers are stored. Eight ALU ops per group, no second load pass,
no spill. The compare is as free as it was claimed to be. The shortfall
is in the cost model, not the code.

### What this does to the job list

  - **The ceiling on ALL blit work is 27.8 lines/vint.** A blit that
    cost literally nothing would put the handler mean at 63.3. That is
    the absolute best case for jobs 1 and 2 combined.
  - **Of that, only ~6.9 lines/vint is store-count-sensitive**, and
    BLITSKIP has already taken 4.0 of it. **~2.5 lines/vint remain on
    the whole store-count road.**
  - **JOB 2 (dirty-rect compose) IS THEREFORE DOWNGRADED, NOT
    CANCELLED.** It attacks the same 25% BLITSKIP just attacked, from
    the same end. Its honest upside is single-digit lines, not the
    "change of ORDER" this loop opened by claiming. It also needs region
    guard bytes we do not have (88 spare). Do not start it on the old
    arithmetic.
  - **`own-tail` (46.5) IS NOW THE BIGGER HALF** and has not moved since
    SPRTRUNC. It is the 68K's own work: consume + DREQ push + glue. The
    parked **sprite-list delta encoding** (73% of decode jobs repeat
    frame to frame; SPRTRUNC took only the easy half) lives there.
  - **The open question is what the other 75% of the blit IS.** It is
    per-row and it is not the stores. Candidates, untested: the 80 SDRAM
    loads themselves; per-row address setup; FB bus contention with the
    SLAVE blitting concurrently (both CPUs blit the same framebuffer in
    the same window — if that is it, the fix is scheduling, not fewer
    bytes). **Measure this before building anything else on the blit.**
    A row-level short-circuit will NOT answer it and will not help: to
    know a row is empty you must load all 80 longs anyway, which is
    exactly the cost group-skipping already leaves on the table.

## WHAT THE BLIT ACTUALLY COSTS — FOUR ARES PROBES, ONE ANSWER

All ticks/row on the FIXED divisor (56 master rows/window under WIN_TWO).

      control        34.2   80 loads + 80 stores
      -57% of stores 29.4   saved 4.8  (14%)   BLITSKIP
      -slave's blit  27.6   saved 1.8  ( 6%)   BLITSOLO
      -79 of 80 loads 24.2  saved 10.0 (29%)   BLITNOLOAD

**NO SINGLE COMPONENT EXPLAINS THE BLIT, AND THE PARTS DO NOT ADD UP.**
Loads are worth 29%. Stores extrapolate to 25%. Cross-CPU contention is
6%. That leaves **46% that is neither** — and the shortfall is the
finding, not a measurement error.

**The blit is THROUGHPUT-bound on the memory system as a whole, and
partial removal returns SUB-LINEARLY.** Remove 57% of the stores and you
keep 86% of the cost; remove 99% of the loads and you keep 71%. The SH-2
write buffer is posted and 4 deep, so the surviving stores re-saturate
it; the surviving loads re-fill the same cache lines. Whatever you take
away, the rest closes the gap.

**THEREFORE: only removing a WHOLE ROW — its reads AND its writes —
recovers a row's full 34.2 ticks. Every partial scheme is capped low,
and we have already collected most of what the partial schemes pay.**

This retires the last of the LOOP 9 model. Writes were never the floor
(that is also why cached vs uncached FB writes measured identically,
47.34 vs 47.46 — the probe was varying the thing that did not matter),
reads are not the floor either, and the other CPU is not the floor.

### What it means for the job list

  - **BLITSKIP is done and is near its ceiling.** 14% of the blit for
    192 bytes and zero correctness risk (0 lies in 2.3M skips). Keep it;
    do not extend it. A group-level scheme keeps the loads by
    construction, and the loads are the bigger half.
  - **A row-level short-circuit is USELESS ON ITS OWN.** You cannot know
    a row is empty without loading all 80 longs, which is the cost you
    were trying to avoid. Earlier LOOP 18 text saying to skip rows is
    only correct if the emptiness is known WITHOUT reading.
  - **JOB 2 IS BACK ON, AND IT IS NOW THE ONLY LEVER WITH REAL
    HEADROOM** — but for a different reason than this loop opened with.
    It is not about writing fewer bytes. It is about **COMPOSE telling
    the blit which rows it touched, so the blit never reads them.** A
    skipped row is worth the whole 34.2 ticks. 31.9% of rows are
    entirely transparent, and the frame-to-frame UNCHANGED fraction is
    larger still and has never been measured — measure it first.

### AND IT IS THE SAME LEVER AS THE TEARING (Mike, on A_blitskip:
### "feels close to a good speed. frame skipping and tearing rampant")

The seam is structural, not a bug to hunt. **The flip happens BEFORE
k2's blit**, so a displayed bank holds rows 112-224 from one snapshot
and rows 0-112 from the NEXT — composed 3 vints (~50ms) apart, a fixed
mid-screen tear every frame.

Moving the flip to AFTER k2's blit makes both halves come from one
snapshot and the seam dies. It is not done that way because a
**full-frame blit is 67.3 lines against a 38-line vblank** — flip after
it and the FBCTL write lands outside the gate, which ares defers a whole
frame (LOOP 7c).

**CORRECTION to the first cut of this paragraph: it said the tearing
needs a 44% cheaper blit, computed from the 67.3-line FULL-FRAME figure.
That is wrong.** Only ONE window's blit has to precede the flip, not
both. The master blits 56 rows per window = **35.8 lines against a
38-line vblank** at BLITSKIP's 29.4 ticks/row. Flip-after-blit is
already within ~2 lines of fitting — far too tight to ship (an overrun
is a whole deferred frame, and that may be part of the 2.6% skips
already), but the gap to close is a couple of lines, not 44%.

**Speed is no longer the objective; MOTION is.** Mike's handler mean on
A was 85.8 (best yet, game ~67%) and his verdict was that speed is close
enough.

## HOW MUCH IS ACTUALLY SKIPPABLE — MEASURED, IN MAME, NO ARES PASS

Row staleness is a RENDERING property, not a timing one, so MAME answers
it honestly (`make ... ROWSTALE=1 BLITSKIP=1` + `tools/rowstale_probe.lua`,
scripted coin-start-walk-punch). Two numbers, and the difference between
them is the page flip:

  - **Rows byte-identical to the same row LAST CYCLE: 68.9%** (88% in
    attract). This is the number that looks great and is NOT achievable.
  - **Rows a real PER-BANK dirty scheme could skip: 44.7% cumulative,
    and ~27% MARGINAL in sustained gameplay.** The bank is two cycles
    stale, so a row is skippable only when THAT BANK already holds this
    exact content — simulated here with a per-bank remembered hash,
    which is exactly the algorithm job 2 would run.

**Quote the 27%, not the 69%.** And note it is worst precisely when the
tearing is most visible: heavy action is when rows change.

Two fixes to the probe were needed to get these at all, both of the same
species as the DIAG[25] bug:
  - `rowslot()` still carried the LOOP 9 THIRDS ranges, so on a WIN_TWO
    build it silently sampled 60 of the master's 112 rows (72..107 and
    168..183 both fell through to "slave row: skip"). Any WIN2-era
    staleness figure from this probe was taken on a biased 54% subset.
  - The per-bank simulation samples 48 of 112 rows — that is all that
    fits in FBCLEAR's 384-byte tail. A ratio survives sampling; a total
    would not.

### THE PLAN THAT FALLS OUT

    k2 blit today                     35.8 lines   (vblank 38)
    + job 2 at 27% rows skipped       26.1 lines   (margin 11.9)

**That is the whole tearing fix.** With ~12 lines of margin the flip can
move to AFTER k2's blit, both halves of a bank then come from ONE
snapshot instead of two 50ms apart, and the fixed mid-screen seam dies.
Job 2 is not a speed job any more — it is the enabler for the flip move,
and the ~7.5 lines/vint it also returns is the side benefit.

Order: buy region-guard bytes (88 spare, this does not fit) -> job 2
with a readback verifier -> move the flip -> re-measure skips, because
the flip move is exactly the kind of change that trades a seam for a
dropped frame.



### Design constraints for job 2, before anyone starts

  - Compose must mark EVERY path that writes sbuf — tiles, sprites,
    cat-1, text. A missed path is a permanently stale row, and it is the
    same silent-failure class as the deferred-pass trap.
  - Per-bank again, and for the same reason as BLITSKIP: a bank is TWO
    frames stale, so a row is skippable only against the target bank's
    own history.
  - **Gate it with a readback verifier, not a pixel diff** — for a
    skipped row, compare the framebuffer against sbuf and count
    mismatches. The BLITSKIP verifier is the template; falsify it first.
  - **Region guard: 88 bytes spare on canonical+BLITSKIP.** This does
    not fit. Buying space is now genuinely on the critical path, ahead
    of job 2, with SPRBAKE (72 bytes) queued behind the same purchase.

## STEP 1 DONE — REGION GUARD: 88 BYTES -> 2,776, AND SPRBAKE IS UNPARKED

The guard was never about code bloat. **`sbuf` IS the region**: 336x240
= 80,640 of the 102,400 bytes below 0x19000, with 21.6KB of RAMCODE
underneath it. Every past "shave .ramtext" scramble was fighting for
scraps beside an 80KB array nobody had questioned.

320x224 of sbuf is the screen; the rest is margin for fine scroll and
off-edge sprite draw. **Which margin is live was never measured, so
measure it** (`make SBUFCANARY=1` + `tools/sbuf_canary.lua`): paint every
margin byte with a per-ROW signature — a flat 0xA5 would be invisible
against any code that happens to store 0xA5 — and read it back after a
scripted play with attract, gameplay and all four directions.

      rows 0..7 and 232..239   ALL 16 came back byte-INTACT
      cols 0..7 and 328..335   ALL 16 came back fully written

The horizontal margin is load-bearing: fine scroll draws from column
8-xf and a 41-tile row reaches 336. **The vertical margin is dead**,
because every writer addresses sbuf as `(8 + y)` with y in [0,224).

`SBUF_H 240 -> 232` drops the bottom margin: **2,688 bytes, one line, no
call-site churn.** The TOP margin stays — taking it means re-basing that
idiom everywhere, and the row it protects is the one an off-by-one would
write BEFORE the array.

      canonical + BLITSKIP   _end 0x06018528   2,776 spare (was 88)
      shipping               _end 0x06018130
      + SPRBAKE too          _end 0x06018688   2,424 spare

Gates: shipping parity **title 2.44 dx=0 / eyehold 3.37 dx=0**, both
exact. (scream and demo2 moved 6 and 4 pixels — those are the
cadence-moving scenes, and the boot .bss clear is 2,688 bytes shorter,
which shifts the phase. The two static gates are untouched.) Gameplay
frame checked: bottom rows, grass and the credit line all intact.

**SPRBAKE IS NO LONGER PARKED** — it was 72 bytes over and now has 2,424
to spare. Its ~5-line win was measured against a PRE-SPRTRUNC baseline
(110 -> 105.2) and the two overlap, both attacking sprite work, so its
value on top of the current bundle is unknown. Worth one ares A/B, but
after job 2 — it is a speed win and speed is no longer the objective.

## STEP 2 — DIRTYROW: THE BLIT CAN NOW SKIP A ROW WITHOUT READING IT

`make ... BLITSKIP=1 DIRTYROW=1`, stamped **DIRTYROW**, `_end
0x060185F0`. Shipping rom byte-for-byte unaffected (`_end 0x06018130`,
title 2.44 dx=0 / eyehold 3.37 dx=0).

**THE FACT TRACKED IS "IS THIS ROW ALL ZEROS", NOT "WAS IT WRITTEN".**
Under MD_BG the background lives on the MD plane and sbuf is EXPLICITLY
ZEROED, every row, every cycle — so a write-mark marks everything and is
worthless. The clear is what ESTABLISHES zero; every draw revokes it.
`ROWLIVE[232]`, uncached at 0x3A680 (FBCLEAR's tail), indexed by SBUF
row so no call site has to remember the +8.

Three uses, in dependency order:
  - **Stage A (verify only):** read back every row the marks claim is
    zero, count disagreements. No behaviour change.
  - **Stage B:** a row already all zeros does not need clearing to all
    zeros — 336 bytes of SDRAM writes saved per row, on the compose side.
  - **Stage C, the payoff:** `if (was == 0x3FF && !ROWLIVE[8 + y])
    continue;` — skip the row entirely, LOADS INCLUDED. BLITSKIP's group
    loop still has to read all 80 longs to discover they are zero, and
    the loads are the bigger half. Both conditions are required: ROWLIVE
    alone is not enough because the bank is two cycles stale.

### The measurements, and two mistakes worth keeping

      unconditional range mark      0.1% skippable   LIED=0
      mark only if a tile drew     22.4% skippable   LIED=0
      + the MASTER's clear too     35.6% skippable   LIED=0

**MISTAKE 1: marking the row RANGE up front.** It looked like the safe
direction and it made the whole scheme worthless — the FG cat-1 pass
sweeps the full screen every cycle, so an unconditional mark marked
every row live. Stage A measured 0.1% and the bug was invisible in every
other counter. The fix is to mark only when a tile survives the filters,
which at ROW granularity is EXACT, not conservative: one surviving tile
anywhere in the row means the row is not all-zero.

**MISTAKE 2: only marking the SLAVE's clear.** There are TWO clears —
`slave_concurrent_k` and the master's band-queue phase 0 — and the
master/slave row split alternates bands, so marking one left half the
screen permanently "live". 22.4% -> 35.6% for four lines.

**THE VERIFIER WAS FALSIFIED.** Remove the text layer's mark and the
same run reports **36,088 lies**; with every mark in place, **0**. It
also then reports a tempting 31.5% "skippable" — a broken scheme scores
BETTER than a correct one on the rate alone, which is exactly why the
rate is not the gate.

### AND THE TRAP THIS LOOP DOCUMENTED THIS MORNING, WALKED INTO ANYWAY

The first gameplay frame of the DIRTYROW build came back badly
corrupted — glyph fields across the whole screen. It was built with
**SPRTRUNC**, which CANNOT be pixel-judged in MAME. The control (same
bundle, DIRTYROW off) was corrupted **identically**. Rebuilt without
SPRTRUNC, both render correctly.

Nearly attributed a known emulator artifact to a new change, hours after
writing the rule down. **Build the control before reading the picture,
every time — the rule is not enough on its own.**

### WHAT IS STILL UNKNOWN

The 35.6% is a MAME rate on a faithful (non-SPRTRUNC) build. **What it
is WORTH is an ares number and nothing else can give it** — MAME charges
the instruction issue and not the memory stalls, and the whole premise
of stage C is that removing a row's LOADS pays where removing its stores
did not. Watch:
  - handler mean against **85.8** (A_blitskip, Mike's best),
  - and the k2 blit against **35.8 lines** — under ~26 makes
    flip-after-blit affordable, which is the tearing fix.

## CORRECTION (same day): THE TEARING IS REAL. IT IS AT THE COMPOSE
## BAND BOUNDARIES, NOT THE BLIT BOUNDARY.

The section below concluded "no seam" from whole-row cross-correlation.
**That conclusion was wrong, and the reason is stated in its own caveat
list and then ignored:** a whole-row correlation is dominated by
background pixels, so it is blind to a tear that exists ONLY in the 32X
sprite layer. That is exactly the tear that is there.

Mike produced four frame pairs. Measured by tracking the player's
skin-tone x-centroid down the image:

  - **frame 2115 vs 2116**: upper body ~31 image-px (~7 game px) LEFT of
    the legs, hard step at image y=397.
  - **frame 3713 vs 3714**: the two frames are IDENTICAL down to y=730,
    then diverge — the cleanest possible tear signature — step at y=735.

The two tears are **338 px = 67.8 game rows apart**. The compose bands
are R0=[0,72) R1=[72,144) R2=[144,224): **72 rows apart**. The k1/k2
blit boundary is a SINGLE row (112) and cannot produce two tears 68 rows
apart at all — and 112 sits BETWEEN the two observed tears.

**So the artifact is band-aligned: adjacent COMPOSE BANDS are drawing
the same sprite at different x.** Under SNAP_ONE every band is supposed
to compose from one snapshot per cycle, so either the snapshot is not
shared across bands or the bands are composed in different cycles.

**THIS KILLS THE FLIP-AFTER-BLIT PLAN AS THE FIX.** That plan addresses
the k1/k2 boundary, which the evidence says is not where the tear is.
The vblank-margin work (BLITSKIP + DIRTYROW, 35.8 -> 29.5 lines) stands
on its own as speed and keeps its option value, but it is not the
tearing fix and must not be sold as one.

**A SECOND, DISTINCT DEFECT: BANDING.** Mike's boss-scene pair shows a
large solid RED rectangle over the boss's chest — not a displacement, a
whole region drawn in the wrong colour, and its top edge also sits near
a band boundary. Colour defect, band-aligned, separate from the tear and
separate again from the every-frame palette shimmer measured below.
Three different things; do not conflate them.

NEXT: read the band/snapshot ordering — why do R0 and R1 disagree about
a sprite's x when they share a snapshot? Falsifiable prediction from the
band theory: tears occur at rows ~72 and ~144 and NEVER at 112.

## (SUPERSEDED IN PART) The palette finding — still valid, wrong headline

The "no seam" verdict here is retired by the section above. The palette
measurements below are unaffected and still stand as a SEPARATE defect.

## THE TEARING IS NOT A SEAM. IT IS THE PALETTE. (capture forensics)

**DO NOT BUILD FLIPLAST ON THE STRENGTH OF THIS LOOP'S ARGUMENT.** The
flip-after-blit plan above is architecturally sound and fixes a real
half-frame skew — but Mike's 9,315-frame ares capture of `E_dirtyrow`
says that skew is NOT what he is seeing, and the artifact that IS there
lives in a different subsystem entirely.

**Test 1 — horizontal displacement, the actual definition of a tear.**
Per-16-row-band 1-D cross-correlation between display updates during a
scroll. A tear is a STEP in that profile at the k1/k2 boundary:

      rows   0- 31   +0.0     (HUD, static)
      rows  32-111   -2.0
      rows 112-127   -2.0     <<<< k1/k2 boundary
      rows 128-223   -2.0

Every band from row 32 down moves by exactly -2 together. **No step, no
seam.** And the reason is MDBGALL: the background is on the MD plane
now, and the MD VDP scrolls it as ONE piece — it cannot tear at a 32X
blit boundary because the 32X does not draw it.

**Test 2 — is row 112 anomalous vs its own neighbours?** Native-res
vertical gradient, 200 frames: row 111 1.79x, row 112 1.72x, but row 120
1.41x and row 105 1.18x. Inside the range of ordinary content. Row 168
(the master/slave split) reads 1.65x, same story. **Not structural.**

**What the capture DOES show.** `tools/frame_profiler.py`: 9,315 frames,
**9,266 unique** — "1.01 capture-frames per update", which reads like a
60Hz display and is impossible (state_health says cadence 3.00). The
hash is being broken by something tiny. Measuring the magnitude:

  - median **106 changed pixels of 71,680 (0.1%)** between consecutive
    60Hz frames, ~19 distinct colour pairs;
  - **scattered over 160 of 224 rows**, span 0..188 — not localised, so
    not animation;
  - dominated by ONE pen cycling three ways:
    `(0,0,171) -> (1,0,209) -> (1,0,255) -> (0,0,171)`, plus greens
    `(106,162,143) <-> (96,143,118)` and sky
    `(100,137,205) <-> (117,154,204)`.

That is **PALETTE CHURN**: a handful of pens oscillating every frame
across most of the screen. It matches everything else on the state —
`pen drift small=3708` in that session, and frame_profiler's own sky
verdict, "55% stable, 4 distinct" / "64% stable, 11 distinct" / "69%
stable, 7 distinct", flagged UNSTABLE in every gameplay segment.

**This collides with the stated priority order** (memory
`s16-priority-order`: speed -> band tearing -> cosmetics -> audio ->
§11 palette LAST). On this evidence the palette is not a cosmetic tail
item, it is the dominant VISIBLE defect and the thing standing between
here and `release-bar-flawless`. Mike's call, but the measurement says
reorder.

### Postscript: DIRTYROW's win is scene-dependent

      A_blitskip  (canonical, CUTBLANK)      85.8   skips 2.6%   3352 cyc
      E_dirtyrow  (canonical, CUTBLANK)      86.3   skips 3.3%   3209 cyc
      F  (no CUTBLANK, WINSPLIT, DIRTYROW)   84.2   skips 0.5%   1630 cyc
      "  (no CUTBLANK, WINSPLIT, BLITSKIP)   88.1   skips 2.2%

Against its own bundle DIRTYROW is worth 3.9 lines (88.1 -> 84.2). On
the canonical bundle across two similar-length sessions it measured
NEUTRAL (85.8 -> 86.3). The difference is scene content — the win is
proportional to how many rows are empty, and F's session was half as
long and lighter. **Quote 88.1 -> 84.2 as "on this bundle, this scene",
never as a headline.** It costs nothing and is provably correct, so it
stays; but it is not the 4-line win the first reading suggested.

## THE TEAR IS A DROPPED BAND. IT IS IN THE CODE, ON PURPOSE.

`m_main.c` at the band-queue push, next to `DIAG[13]` ("queue-full
deferrals"), already describes both of Mike's artifacts:

>  streak fairness) traded one artifact for another — stale locked
>  stripes, starved maps, frozen regions, BG-only rows with the FG
>  phases missing (the "red box" report). **A COMPLETE BAND ONE CYCLE
>  LATE LOOKS EXACTLY LIKE THE ARCADE ONE FRAME AGO; A PARTIAL BAND
>  LOOKS LIKE A BROKEN GAME.**

The policy is: when the band queue is full, DROP the band rather than
compose it partially. A dropped band's rows keep last cycle's content
and are recomposed next cycle from the rotating `drop_s0` frontier,
which "bounds any row's staleness to ~2 cycles".

**THE PREMISE IS FALSE FOR SPRITES THAT SPAN A BAND BOUNDARY.** A
complete band one cycle late does look like the arcade one frame ago —
*for that band*. But the band next to it is CURRENT, and a sprite
crossing the boundary is then drawn at two different x positions at
once. That is not "one frame ago", it is a body cut in half, and it is
exactly what Mike photographed at rows ~76 and ~145.

And the same comment names the second artifact: **"BG-only rows with the
FG phases missing (the 'red box' report)"** — a PARTIAL band. Mike's
boss pair is a solid red rectangle with its top edge at a band boundary.
The policy exists to prevent partial bands; it is still happening.

**THE RATE IS THE SMOKING GUN.** `deferrals` IS `DIAG[13]` IS the
band-drop count:

      pre-BLITSKIP control   4412 drops / 3740 cycles = 1.18 per cycle
      E_dirtyrow             3036 / 3209             = 0.95 per cycle
      F (DIRTYROW+WINSPLIT)  1250 / 1630             = 0.77 per cycle

**Roughly ONE BAND DROPPED EVERY CYCLE, every cycle, forever.** Three
bands are pushed per cycle into a depth-4 queue. And the rate falls as
the port gets faster, which is why the whole speed arc has been quietly
reducing the tearing without anyone connecting the two.

### What this means for the job list

  - **The tearing fix is COMPOSE THROUGHPUT, not presentation.** Nothing
    about flips, banks, or blit ordering touches it.
  - **JOB 3 IS NOW JOB 1: spend the idle slave.** 15,602 polls/cycle of
    a second SH-2 doing nothing, against ~1 band dropped per cycle. That
    is the resource and the deficit, and they are the same size problem.
  - **Fallback if throughput cannot close it: make a late band
    INVISIBLE.** If any band is stale, hold the whole previous frame
    rather than ship a mixed one — trading a torn frame for a repeated
    one. Judder instead of a severed body. Cheap to try, and it makes
    the artifact honest rather than broken-looking.
  - The BLITSKIP/DIRTYROW vblank margin remains speed, and speed feeds
    compose throughput, so it was not wasted — but it was never going to
    fix this directly.

### Honest status of the evidence

  - **Hand measurements: solid.** Two frame pairs, tears 67.8 game rows
    apart against a 72-row band pitch, with row 112 lying BETWEEN them.
  - **Corpus sweep: INCONCLUSIVE, do not re-run it blind.** Three
    detectors tried (first-differing-row, clean-step, activity-masked
    transitions). All are dominated by content sparsity — uniform sky
    never differs, so every detector reports the top of the textured
    content (~row 41) or the score digits (~row 19). Isolating a sprite
    DISPLACEMENT at corpus scale needs sprite segmentation, not
    row-change statistics.
  - **Code + counter: agree with the hand measurements.** The mechanism
    is named in the source and the rate is already instrumented.

## CAN THE MD SPRITE CHIP DRAW THIS GAME? MOSTLY — 92% OF LINES FIT.

Mike's architecture challenge: we are software-rasterizing ~13 sprites
on two SH-2s and then blitting 71,680 bytes, next to an idle sprite
chip, on a machine that is nothing like the arcade bus this code was
written for. MDBGALL already proved the trade once — moving the
background to the MD tile planes is what made 62.7% of framebuffer
groups transparent and unlocked BLITSKIP and DIRTYROW.

`make ... SPRLINE=1` + `tools/sprline_probe.lua`, scripted play through
gameplay and the round-1 boss, 158,747 sprite-bearing scanlines:

      MD sprite COUNT limit (20/line)   exceeded on   0.18% of lines  (worst 38)
      MD sprite PIXEL limit (320/line)  exceeded on   8.05% of lines  (worst 808)
      sprites that can NEVER go to MD (zoomed/gated/shadow):  1073 instances

**The count limit is a non-issue. The pixel limit is the real
constraint, and it is exceeded on 8% of lines — so a pure MD sprite path
would drop sprites there, but a HYBRID has 92% of lines free.**

That is the same shape as MDBGALL: hardware takes the common case, the
32X framebuffer takes the residue. And the residue is concentrated where
the 32X is needed anyway — the zoomed/gated/shadow count is 0 for the
whole opening and only climbs at the boss (0 -> 414 -> 761 -> 1073).

**THE 8% IS AN UPPER BOUND.** Width is taken as `|pitch| * 4`, the row
STRIDE, which counts transparent pixels a variable-width S16 sprite row
may never draw. The MD charges a sprite's full width whether or not its
pixels are opaque, so the comparison is right in kind — but the real
overflow can only be lower than this, not higher.

### Why this matters more than one port

Per Mike: **TOOLKIT.md is the deliverable and Altered Beast is the
measurement.** This probe is a per-TITLE feasibility instrument, not an
Altered Beast result. Every S16B game has the same shape — two tile
planes, a text layer, a sprite chip, 128 colour sets — but wildly
different sprite density. Run `SPRLINE=1` against a candidate title and
the two percentages say immediately whether its sprites can go to MD
hardware, all of them, some of them, or none. That is a porting-decision
instrument and it belongs in the kit.

### What it does to the roadmap

The tear is dropped bands; dropped bands are a compose-throughput
deficit; **moving most sprites to MD hardware removes most of compose.**
So the architecture move and the tearing fix are the same work, and
spending the idle slave on compose (job 3) props up a stack this would
delete. Sequence accordingly — but note the counterweight: MD hardware
sprites need MD palette lines, and §11's pack is already the weakest
subsystem (the every-frame shimmer measured above). More MD-drawn
content means more pressure on exactly the thing that is failing.

## RASTER PALETTE SWAPPING DOES NOT SOLVE THIS. THE DEMAND IS
## HORIZONTAL, NOT VERTICAL.

Mike's question: are we swapping the palette mid-frame? **No.**
`md_start.s` sets VDP reg `0x8A = 0xDF` — HINT counter 223, one IRQ4 at
the last active line, "arcade-style" — and `_hblank` is an `rte`. CRAM
is written once per cycle in the window. The three MD palette lines are
STATIC for the whole frame.

That is the classic Mega Drive capability we are not using, so measure
what it would buy before building it. Colour-set mask accumulated per
28-line span, merged for the 4- and 2-swap answers:

      swaps per frame   worst distinct sets in any span
            1 (today)                 11
            2                          9
            4                          9
            8                          8

      cycles fully served by 3 MD lines:
            8 swaps 41%   4 swaps 41%   2 swaps 41%

**Eight raster swaps buy essentially nothing over one.** The reason is
the game's own layout: this is a side-scrolling beat-em-up, so the
actors all stand on the same ground line. The colour demand is spread
ACROSS a scanline, not down the screen, and vertical partitioning cannot
separate what is horizontally coincident. Raster swaps are the right
tool for a game whose palette pressure stacks vertically (status bars,
sky gradients, layered backdrops); they are the wrong tool for this one.

**Do not build HINT-driven CRAM swapping for this title.** Keep the
finding for the kit — it is a per-title question, and the probe answers
it in one run.

### A distinction this loop muddled, now straight

  - **SPRITE colour sets live on the 32X**, whose CRAM is 256 entries —
    `spr_pair` gives 16 pairs. The 11-sets-per-cycle measured above is
    the SPRITE demand and it fits there comfortably. That is why sprites
    are on the 32X, and it is the right call.
  - **BACKGROUND colour sets live on the MD**, three lines of 15 usable
    pens (line 0 is the grey ramp for text). **This is what is
    over-subscribed, and this is what the every-frame shimmer is.**
  - Therefore **the MD-hardware-sprite hybrid is dead on palette, not on
    geometry.** 92% of scanlines fit the sprite chip, but those sprites
    would need MD palette lines, competing with a background pack that
    is already failing. The geometry result stands as a kit measurement;
    the move does not.

### THE FIGURE THE WHOLE PACK RESTS ON WAS MEASURED ON ATTRACT

`m_main.c` §11: *"MEASURED (this loop, live PAL_SH walk over the ATTRACT
SCENES): the visible BG window needs at most 21 distinct S16 colour sets
but only 36 distinct MD-quantised colours — so a COLOUR-level pack into
MD lines 1-3 (3 x 15 usable pens = 45) covers the worst scene with
room."*

45 pens against 36 colours should have nine to spare. In gameplay the
state shows `pen drift small=3708` per session and a pen visibly cycling
three ways every frame. **A pack with room to spare does not churn.** So
the 36 is an attract-mode number and gameplay is not attract — the same
error class as LOOP 9's pre-MDBGALL blit ratios, and CLAUDE.md rule 3
says re-read the counter before building on it.

**NEXT MEASUREMENT (cheap, and the actual root of the shimmer): the
distinct MD-quantised colour count of the visible BG window during
GAMEPLAY, not attract.** If it exceeds 45, the pack is over-subscribed
by construction and no amount of drift tuning fixes it; the answers are
then to buy pens (line 0's ramp), to merge deliberately rather than by
thrash, or to accept it. If it fits in 45, the churn is a bug in the
claim/drift logic and is fixable without changing the budget.

## THE TEAR IS A LOAD IMBALANCE, NOT A SPEED PROBLEM. BANDSHIFT.

**SPRBAKE was neutral, and that negative result is what found this.**
On identical bundles, adding it moved drops/cycle 0.946 -> 0.970 and the
handler mean 86.3 -> 86.5. Nothing. Look at the whole series:

      build                      drops/cycle   handler mean
      pre-BLITSKIP                   1.180         91.1
      E_dirtyrow                     0.946         86.3
      G_sprbake                      0.970         86.5
      F (no CUTBLANK, WINSPLIT)      0.767         84.2

**Drops/cycle is pinned near 0.95 while the handler mean falls 91 -> 86.**
Three large, independent speedups and the band-drop rate does not
follow. A uniform speedup cannot fix this, so it is not a throughput
deficit — **it is an ASYMMETRY**.

### The mechanism, from the source

  - Compose is split **112 rows each** (slave 0-36 / 72-108 / 144-184,
    master the complement) and the blit **56 rows each**. Even.
  - But the MASTER alone also does: the flip, the page drain and
    restore, CRAM, `build_maps`, the shadow LUT, the sprite snapshot,
    and the band queue itself.
  - **The push is gated on the SLAVE's echo** (`SYNC[1] == pend_wait` ->
    `BQ_PUSH`); **the drain is gated on the MASTER's progress**
    (`bq_h` advances only when a band completes all phases).

So the slave finishes early — **15,533 idle polls/cycle** — and its echo
pushes the next band while the master still owes all the extra work.
Pushes outrun drains, the depth-4 queue fills, and it sheds ~1 band per
cycle forever.

**AND THE DROPPED BAND IS ALWAYS THE MASTER'S ROWS.** The master owns
36-72, 108-144, 184-224. Its rows go stale while the slave's stay
current, so the discontinuity lands where master rows meet slave rows:
**rows 72 and 144 — exactly where Mike photographed the tears.** Every
piece of evidence now agrees.

### BANDSHIFT: move N rows of every band from master to slave

`make BANDSHIFT=n`. MAME sweep (ranking only — the effect is scheduling,
not SH-2 timing, and the direction is unambiguous):

      shift   master rows   slave rows   band drops
        0         112          112          201
        8          88          136           88
       16          64          160           76
       24          40          184           47
       32          16          208           30
       36           4          220           17

**85% of the drops gone at shift 32**, monotonically, with compose time
flat (2.62-2.66 ms) and the picture correct at every step.

**Hard limit is 36**: R0 and R1's master ranges reach zero rows there.
Past ~32 the architecture has quietly become "slave renders, master runs
the system" — which may simply be the right shape, given everything the
master carries that the slave does not.

### What this retires

  - **Job 3 ("spend the idle slave") was right all along and for the
    wrong reason.** It is not extra capacity for more work; it is the
    fix for a scheduling imbalance that has been producing the single
    most visible defect in the port.
  - **SPRBAKE stays** (it fits, it is pixel-gated, it is free) but it is
    not a lever. Do not expect it to move anything.
  - The whole blit arc (BLITSKIP, DIRTYROW) remains real speed —
    91.3 -> 86.5 — but none of it was ever going to touch the tearing,
    and now we know why.

**NEXT: ares. `rom/test/H_shift16.32x` (SHIFT16) and
`rom/test/H_shift32.32x` (SHIFT32).** Watch `deferrals` per cycle
against 0.95, and watch the SCREEN — this is a tearing fix, so Mike's
eye is the gate, not the counter.

## BANDSHIFT=32 ON ARES: THE MECHANISM IS CONFIRMED, THE DIAL OVERSHOOTS

                    drops/cyc   hmean   skips   V-rej   win/ack   slave idle
      G_sprbake         0.970    86.5    2.8%       6      39.1      15,533
      H_shift32         0.263    88.5    4.6%      34      42.9      14,756

**Band drops cut 73%** (MAME predicted 85%). The diagnosis holds on real
hardware: the tear was a master/slave load imbalance and moving rows off
the master is the lever.

**But it overshoots.** Frame drops 2.8% -> 4.6%, V-gate rejects 5.7x,
handler mean +2.0, and window/ack up 39.1 -> 42.9 — the master now WAITS
on the slave at the window boundary and misses the vblank gate. We
traded torn frames for dropped ones.

### The number that reframes the fix

**Slave idle barely moved: 15,533 -> 14,756, despite taking 96 more
rows.** If the slave were compute-limited, 96 extra rows would have
eaten its idle time. It did not.

**So the slave is not idle for lack of work — it is idle WAITING FOR
COMMANDS.** Its poll loop spins because the master has not posted
anything yet. That means BANDSHIFT treats the symptom: it hands the
slave more rows, but the master is still the only thing that can hand
out work, and now the master waits on the result.

### Where the fix actually is

The master's problem was never its share of compose ROWS; it is
everything it does that the slave cannot: the flip, the page drain and
restore, CRAM, `build_maps`, the shadow LUT, the sprite snapshot, the
band queue. Moving compose rows off it shortens one duty and lengthens
the critical path it then waits on.

**The cleaner move is to relocate a master-ONLY duty to the slave** —
`build_maps` is the obvious candidate (it is already chunked under
BQCHUNK, it is pure computation over the tilemap, and it does not touch
the FB window or CRAM). That reduces master load WITHOUT putting the
master on a longer wait.

### Where to leave the dial meanwhile

`BANDSHIFT=16` is the untested middle (MAME: drops 76 vs 201 even, vs 30
at 32) and should keep most of the tearing win at a fraction of the
frame-drop cost. **The gate is Mike's eye, not the counters** — this is
a tearing fix, and 73% fewer band drops either shows or it does not.

## THE BLACK BLOCKS AT LEVEL START ARE CUTBLANK. DROP IT.

Mike's frame map of the round-1 opening: frames 20-67 the tilemap paints
in, **68-242 the Zeus fly-in "dropped frames, blitter and wrong
colors"**, 243-479 the scaled animation working, 480-624 the fade-out
broken again.

His frame 140 shows the MD background plane shot through with black
TILE-SHAPED holes — sky, trees and masonry punched out in blocks. That
is not a sprite fault at all. It is `CUTBLANK`, doing exactly what its
own Makefile entry says:

>  at a scene-cut claim storm ... cells whose slot's art has not shipped
>  yet go out as the BLANK slot instead of the stale art the slot still
>  holds — the J-field of foreign art at cuts becomes **blank cells
>  UNDER THE FADE**, filling in as uploads land

**The premise is "under the fade". At a level start there is no fade**,
so the blanks are on a fully-lit screen for as long as the drain takes.

Reproduced and A/B'd in MAME at the same cut (`tools`-scripted coin-in,
level start ~frame 1060), counting near-black pixels per frame:

      frame    CUTBLANK    without
      1060        1,344        171
      1080       11,338         80
      1100       10,263         77
      1120+     identical   identical

**CUTBLANK punches 10,000+ black pixels for the duration of the drain
and the build without it is clean at every single frame.** No stale-art
artifact appears in its place here — the damage is pure. On ares the
window is longer than MAME's (Mike saw ~174 frames, ~2.9s) because the
drain is slower there.

**RECOMMENDATION: drop CUTBLANK from the canonical bundle.**
`rom/test/J_shift16_nocutblank.32x`.

**The honest caveat:** CUTBLANK was added in LOOP14 for a specific
observed artifact — "the J-field of foreign art at cuts" — which was
presumably a CUTSCENE transition, not a level start. Removing it may
bring that back somewhere this test did not look. Re-check a cutscene
cut before closing it out. But it must not stay in the bundle as-is:
it is trading a rare artifact for a guaranteed one on every level start.

### Still open on the Zeus scene

The fade-out range (Mike's frames 480-624) is a DIFFERENT artifact —
his frame 520 has a clean background with stray sprite FRAGMENTS in
wrong colours, not blanked cells. That is sprite/band staleness or the
transparency path, and it is untouched by any of this. Separate defect,
separate hunt.

## GATES ON EVERY COMMIT

  - `tools/parity_run.sh <dir>`: SHIPPING statics title 2.44 / eyehold
    3.37, dx=0 EXACT. (Only meaningful where MAME models the 32X — see
    the trap above.)
  - `grep ' _end$' rom/s16.lst` under `0x06019000`. The build fails hard
    if not.
  - Shipping rom stamped `normal`; probe builds carry their own stamp.
  - **Mike's ares play pass** — the bar is `release-bar-flawless`: look
    AND motion. Ask how it FEELS.
