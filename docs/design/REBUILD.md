# REBUILD — the 60Hz arcade-perfect architecture

2026-08-21. Mike's bar, verbatim: "nothing is precious. the only answer
is a completely perfect arcade port." Evidence base: four parallel
audits (scratchpad: audit_cannonball.md, audit_romcensus.md,
audit_sdkmodel.md, audit_indictment.md) + the full LOOP record.
The audits' findings are summarized here; their files carry the
citations. This document is the plan of record; LOOP26+ execute it.

## 0. THE VERDICT, ONE PARAGRAPH

The census of ~40 commercial titles found ZERO exceptions: every
shipped 32X game flips at vblank under a V-int-owned clock, toggles
FBCTL with a read-xor-write, keeps FM parked with the SH-2, uses the
68K as a mailbox peer (never a renderer), commits display state by
shadow-then-vblank-commit, and synchronizes the whole frame in ~30
instructions. Cannonball (an arcade-engine port, full source) and
d32xr (the best open 32X codebase) implement the same model
independently. Our port built three generations of window protocol,
a DREQ packet archaeology, and a capture/restore stratum because of
two founding choices — software-rastering all five layers (repaired
by MDBGALL) and letting the game's video state live in the banked
framebuffer (partially repaired by K2FREE) — and every major win in
25 loops was a REMOVAL of our own machinery. The drawing itself fits
a 60Hz frame twice over (6.38ms/frame both-CPU, LOOP20). Nothing in
the silicon wants what we built.

## 1. THE ONE REAL CONSTRAINT (why we can't copy Cannonball verbatim)

Every reference runs its game logic ON the SH-2. We run the original
System 16 68K binary on the MD 68K — that is the project's premise
and its fidelity asset. Consequences that no reference had to face:
- The game READS BACK its tile staging (15KB of collision probes +
  1KB scratch) and RMWs its palette (fades). Its video state must be
  somewhere the 68K can read coherently — WRAM (64KB, mostly the
  game's) or the FB window. Tile RAM (~52KB live) fits only the FB.
- The 68K clock is 7.67MHz vs the arcade's 10MHz: 0.77x ceiling
  before our handler costs. THE UNMEASURED NUMBER THAT BOUNDS
  EVERYTHING: the arcade game's own frame occupancy (Phase 0).

So the rebuild is the consensus model with exactly ONE concession:
a single, bounded, SH-2-scheduled FM span per frame, because the
game's tile/text/palette staging lives in the FB and the game must
own the bus the rest of the time. Everything else the consensus
model deletes, we delete.

## 2. TARGET ARCHITECTURE

FRAME = ONE VINT. 60Hz native. No k-phases, no window kinds, no idle
beats, no posts, no acks. Every vint is identical.

MASTER V-ISR (the frame clock; the census consensus handler shape —
tick, commit, out):
  1. frame counter++.
  2. FLIP COMMIT: FS read-xor-write (shadow-commit; we are inside
     vblank by construction). Edge-guard rule retained as law: if a
     late entry ever puts this past the window, DECLINE (drop beats
     tear — LOOP24 Z2, precious fact #2).
  3. CRAM COMMIT: apply this frame's palette truth to 32X CRAM
     (d32xr's guarded vblank palette upload — the stock mechanism we
     never used).
  4. DREQ ARM for this frame's push (per-k buffers die; ONE arm).
  5. rte. Target: tens of instructions + the CRAM burst.

MASTER BODY (free-running loop, no pickup, no polling for commands):
  - Composes the NEXT frame's sprites/FG-cat1/text into sbuf (SDRAM)
    continuously. The slave takes its half via the existing mailbox.
  - Once per frame, at its own choosing right after vblank: RAISES
    FM, runs the FM SPAN — blit both halves to the hidden bank
    (hidden-bank blits have no vblank bound), capture tile/text/pal
    truth from the access bank, restore-into-new-bank set, publish
    MD-plane packets — DROPS FM. Budget: blit 3.75ms + captures
    ~0.5ms ≈ 4.3ms. The game owns the bus the other ~12ms.
  - FM=0 is the STEADY STATE (inverse of commercial, forced by the
    constraint in §1); the SH-2's span is the one interruption, and
    the game's FB writers/readers are already pointer-gated against
    it (FMGATE thunks — they survive).

STATE PLACEMENT (final; ends the four-migrations era):
  - FB (sole tenants, all captured/restored around the flip, all
    writer-gated): TILE staging (must — 52KB + read-backs),
    TEXT (4KB, the capture pattern is proven artifact-free),
    PALETTE (4KB — MOVES BACK to FB staging. The FIFO cannot carry
    bulk palette (LOOP25, twice); the capture pattern syncs the FULL
    palette every frame for ~10-20 SH-2 lines and ZERO 68K cost.
    Whole-palette-per-frame deletes: dirty thunk bitmaps, PAL32
    blocks, KMAX, retry, storms, torn-CRAM — the entire #8 stratum.
    Fade RMW reads hit the FB under the same pointer gates as tiles;
    per-bank skew is what capture/restore exists for.)
  - WRAM + one DREQ push per frame: SPRITE live records + LAYER
    REGS/ROWSCROLL (vint-written by the game, ~190 words, per-word
    polled per the FIFO contract, ISR-armed, COMM6 pre-announce
    survives). ~12-15 lines of 68K per frame.
  - MD planes: MDBGALL + staged vblank DMA + the lossless A/B
    publish handshake survive unchanged (the most successful
    subsystem in the repo).

68K VINT SHIM (target ≤10 lines): consume MD-plane packet(s) →
announce+push → return to the game. No spins, no waits, no FM
writes, ever. Game share target ≥72% of arcade (vs 62% today).

PACING GOVERNOR: WWF's vint-countdown divider — the ONE flag the new
build keeps for bring-up: GOVERNOR=2 gives clean 30Hz during
development, GOVERNOR=1 is the ship state. Not a phase machine — a
divider on the same identical frame.

WHAT 60HZ BUYS FREE (LOOP22, reaffirmed by the audits): the
split-blit seam class, scale stepping, cadence aliasing, Zeus's true
temporal translucency (FLICKFUSE stays dead per Mike's ruling),
sprite/input latency. Most of Mike's standing visual complaints are
sub-60 sampling costs.

## 3. PHASE 0 — MEASUREMENTS THAT GATE THE PLAN (before any code)

M1. ARCADE 68K OCCUPANCY — MEASURED 2026-08-21, PASSED.
    Method: the game's vint-wait entry at 0x397E (clr.b $FFF01C +
    poll; found by PC sampling after the 12 stop-#$2300 sites proved
    to be other-mode idles) breakpointed in `mame altbeast`, beam
    line logged per wait entry; occupancy = (wait_V - 224) mod 262.
    Tools: m1_occupancy.lua / m1_occupancy.py. 5,279 frames with
    scripted walk+attack gameplay:
      gameplay: mean 53.9%, p50 56.5%, p95 68.3%, p99 72.9%,
                max 97.7%, ZERO fully-busy frames; frames over the
                0.72 port budget: 1.91%.
      attract:  41 fully-busy frames, all scene loads — where the
                arcade itself runs multi-frame waits.
    VERDICT: the original binary CAN pace 60Hz on the MD 68K with a
    <=10-line handler; ~2% of gameplay frames spill one frame — the
    arcade's own transition behavior, not a new class. Option (c)
    stays in reserve, unneeded. Caveat: the scripted walk is not
    the true worst case (bosses, max spawns); the p99 margin says
    spills stay rare, and the M2 gameplay gate re-verifies.
M2. INPUT PLAYBACK IN ARES-HEADLESS. Two builds shipped broken
    because attract-only gates can't see gameplay. This is the
    missing gate for every phase below; it is next on the ares
    session's list — confirm it lands first.
M3. PUSH BUDGET AT 60. The ~190-word push, per-word polled, against
    the new FM-span timing, measured on headless ares: 68K lines and
    loss rate. (The FIFO contract facts, precious #3, are the law.)
M4. FM-SPAN CEILING. Blit+captures under game-concurrent bus load
    (the K2FREE cart-contention lesson): measure the span on ares
    with the game running; it must fit ~4.5ms worst-scene.

## 4. PHASE SEQUENCE (each phase: MAME structure gate + headless-ares
     gate + region guard; Mike's play pass at P2/P4/P5)

P1. `rebuild60` branch. Strip to the skeleton: ONE code path, no
    flag strata (probes move to probes.mk with their own flags; the
    62-define zoo does not come). The 20Hz/3-window/museum paths,
    SPRBAKE blob, FLICKFUSE, the palette allocator arms-race
    mechanisms, packet archaeology: deleted. patch_game keeps its
    rebase core + gates + mirrors per §2 placement.
P2. FRAME CORE at 60 (governor=1): V-ISR clock+flip+arm, FM-span
    body (blit + captures + restore), MD planes on, tile window
    live, game running. Gate: statics parity, cadence 1.0x vints/
    frame, skips 0, flip-pos inside vblank, M4 numbers hold.
P3. TRANSPORT: the single push (sprites+regs) + whole-palette
    capture residency. Gate: sprite freshness counters, palette
    torn-exposure ~0 by construction, M3 numbers hold.
P4. FULL GAMEPLAY at 60: input-playback gameplay gates (M2) on the
    gravestone stretch; then Mike's play pass. Bar: no tear, no
    seam, no palette family, speed feel = arcade.
P5. PARITY POLISH: re-baseline the parity gate on THIS architecture
    (retire the museum rom); scale/zoom fidelity check at 60;
    shadow/priority against jtcores facts; then the audio arc opens
    (d32xr's complete template: Z80 FM + PWM + DMA1 — our lane is
    empty and the SDK hands us the whole design).

## 5. WHAT SURVIVES / WHAT BURNS

SURVIVES (earned it): MDBGALL + staged-DMA + lossless publish; the
V-ISR flip discipline + edge guard; SPRTRUNC live-record push +
magic-tail/whitelist/per-word FIFO law; packet_fmt single-source
discipline (one small family now); patch_game rebase core + FMGATE
pointer-gates + mirrors; capture/restore in its minimal proven form
(it is the PRICE of FB residency, now covering tiles+text+palette
uniformly); the instrument estate (DIAG map REBUILT clean — eight
slot collisions is an indictment); the 13 precious facts
(audit_indictment.md §5) as law; MAME/ares split-brain rules.

BURNS: the k-phase window machinery and all three generations; the
DREQ packet archaeology beyond the one push; the palette streaming
stack (thunk bitmaps, PAL32, KMAX, retries, storm probe stays as a
verifier); FLICKFUSE; SPRBAKE's 659KB (measured neutral 3x); the
20Hz paths; the museum shipping rom; ~50 of 62 defines.

## 6. RISK REGISTER

R1. M1 fails (arcade needs >75%) → option (c) conversation; nothing
    else in this plan changes the answer. Measure FIRST.
R2. The FM span under contention exceeds budget (M4) → slave takes
    more blit rows (14K idle polls/cycle of headroom measured), or
    the span splits across two vblank-adjacent slices.
R3. Tile-window collisions hurt the game (gated writers waiting) →
    store-level WRAM shadow of the 15KB read-back subset (patch_game
    grows a store-pass; the designed fallback, cost unknown).
R4. Push losses at the new timing (M3) → push timing slides inside
    the frame (the SH-2 signals its quiet span); the contract's
    mitigations are proven.
R5. 60Hz compose misses on heavy sprite scenes → governor=2 (30Hz)
    remains a clean, identical-frame fallback while the compose is
    optimized — never a different architecture again.


## P1/P2 BRING-UP LOG (2026-08-22, first session on rebuild60)

R60=1 builds and RUNS. Headless-ares, 1800 frames, cold boot:

  cadence 1.03 (ONE FRAME PER VINT — the 60Hz frame core is ALIVE)
  ISR flips 1625/1695 (96%), flip-pos mean 22.5 / MAX 28.5 lines,
  flip-late 0 — every flip deep inside vblank, at 60Hz, textbook.
  stale 5%%, declined 5.7%%, misalign 2.6%%, rejects 1.7%%.

Architecture as built: every vint identical — the 68K shim gates ->
announces -> consumes both MD-plane packets -> raises -> posts ->
pushes THE one R60 packet (regs+rowscroll+bitmap+pal-blocks+records,
packet_fmt.h R60 family) -> returns (a short flip-echo tail only).
The master's V-ISR arms on the announce, keeps polling, flips on the
post; the body parks bus-quiet from announce to pickup (the push
lands against an idle master), then blits the FULL frame (master
112-224, slave 0-112), harvests, captures, publishes.

SEVEN bring-up graves, all instrumented-then-fixed:
1. Consume-before-post pushed the post past the ISR window (widened
   to 1100 + announce-first ordering).
2. The ISR's announce path RETURNED — under R60 announce and post
   share a vint, so it never saw the post (flips=0 until fixed).
3. The consume magic-clear was #ifdef K2_FREE — MD side doesn't
   define it under R60: one boot packet re-consumed forever.
4. slave_wait was UNBOUNDED — a stomped SYNC[0] held FM forever
   (vints starved to 14). Bounded + counted.
5. The park flag never cleared at pickup — the poll loop parked
   through the whole gap: compose and the slave chain starved.
6. Launch-when-chain-behind now SKIPS (auto-30 for that frame)
   instead of waiting — waiting cascades at 60Hz.
7. A block-scoped static (packet parity) reads 0 every entry —
   something wipes that .bss neighborhood each frame. OPEN BUG
   (parity relocated to DIAG scrap as a visible workaround; find
   the wild writer before shipping anything).

OPEN DEFECTS (next session, in order):
A. NT-chunk packets never reach the 68K consumed-as-chunks (B0E6=0;
   builds healthy 1:1, publishes flow post-parity-fix) — suspect the
   parity/build-type LOCKSTEP (both period 2) or a consume-side typ
   decode issue. MD plane stays empty until fixed.
B. slave-tmo ~1/2 frames: the compose chain (R0 + pend R1/R2) can't
   cycle 3 bands/16.6ms through the poll-loop chain — restructure
   the launch (all three at window, or slave self-chains).
C. The .bss wild writer (grave 7).
D. 68K handler 85.8 lines at 60Hz (consumes x2 + push + belts) —
   diet after correctness.

## P1/P2 BRING-UP, SESSION 2 (2026-08-22, continued)

Defects A, C, D from the list above are CLOSED; B was a phantom.

**A + C had ONE root cause — MASTER STACK OVERFLOW.** The "wild
writer" was the master's own stack: SP started at 0x3F000 and the R60
call graph ran 2176 bytes deep, straight through the 640-byte red zone
and ALL of md_pkt (0x3E780..0x3ED7F). Caught by MAME watchpoint: 317
LONG stack-slot writes at exactly 0x3E780, master CPU, gap-time; the
build magic verified landing by read-back and was then erased by frame
transit. A survived only because md_pktA lives at 0x39A00. Fix:
master SP -> 0x3F800 (2KB taken from the slave's 4KB; slave measures
176B deep), boot sentinels under R60 paint both zones so any ares dump
reads the true watermark (master 1320B used, 1368B margin). The B
channel came alive the same build: chunks build/publish/consume 1:1,
NT ~34k cells/1800f, plane A shows the wolf statue and graves, plane B
the forest — real round-1 art from VRAM dumps.

**B was a counter collision**: DIAG[20] is apply_cram's memo-hit
counter (healthy ~18/frame); the real slave echo timeout (own slot
DIAG[27] now) reads 0-6 per 1800 frames. No compose restructure needed
at attract load.

**D closed in three measured cuts** (68K handler 99 -> 67 lines):
beam-stamp autopsy of the shim found (1) the consume's WRAM-staging
copy loops — replaced by direct VDP DMA FROM THE FB WINDOW (consume
runs at vint top inside vblank; the staging indirection was a
mid-frame-era artifact) — NT throughput 7x; (2) the hscroll delta
tail at n=56 (35 beam lines on every scrolling frame) — replaced by
dense 28+28-word arrays played as two strided DMAs (auto-inc 32);
(3) the per-word FIFO poll — the residual-spin probe read 2600/2600
(FIFO NEVER fills; the drain always outpaces the 68K), so per-word
polling was pure overhead: one guard per 4-word burst, push span
100 -> 59 lines. Packet v2 (header-first, rowscroll optional via tag
bit10) cut the mean push 344 -> 287 words.

HARDWARE FACTS measured this session:
- The FM rule COVERS READS: SH-2 FB reads at FM=0 return garbage 89%
  of the time on ares (DIAG[24]/[25] probe). Announce-time capture is
  structurally impossible; the pre-flip capture span is load-bearing.
- ares charges ~85 cycles per 68K adapter-register access (FIFO/ctrl).
  -O2 vs -O0 made no measurable difference to the push — access-bound,
  not instruction-bound. Fewer WORDS is the only push lever left.
- A tight SH-2 COMM poll starves the DMAC's FIFO drain (adapter bus
  contention). All R60 park/wait spins are FRT-paced now (~16 ticks
  between COMM reads, on-chip in between): flips +7%, declines -40%.
- BOTH CPUs build at -O0 by default — MDEXTRA/SHEXTRA are empty
  outside `make release`. Every historical measurement is -O0 code.

NEGATIVE RESULTS (do not retry):
- push-before-post: 8 flips in 1602 vints. The push is ~59 lines of
  68K time; a post behind it never makes vblank. The push MUST
  overlap the flip span.
- hold-for-F103 (delay push until restore-done): handler 89 -> 107,
  push no faster. The drain was never the wall (see FIFO-never-full).
  F103 echo kept in flip_span — free, may serve a later design.
- TEXTCAP_SLAVE (now canonical): capture cost off the flip span, but
  declines are variance-dominated at attract; the win is structural,
  not yet visible in the flip rate.

OPEN (next):
- Flip declines 15-25%, run-to-run variance large — size the noise,
  then chase the tail (heavy-frame capture spans).
- Push words: 287 mean. Next diet is structural (FBSPR-style sprite
  records via FB staging = P3 territory; needs the per-bank skew
  story solved for sprite RAM).
- M2 (ares-headless input playback) still gates gameplay validation.

## M2 DELIVERED — GAMEPLAY VALIDATED HEADLESS (2026-08-22)

The ares session shipped `--input file.csv` (frame-indexed playback,
deterministic — 3 identical runs bit-for-bit) plus --trace-flip /
--trace-dreq / --trace-comm. Phase 0's blocking gate is CLOSED.

Input mapping for scripts: coin = MD **Y** (the md_main.c mask
0x0200 is bit 9 = Y; the variable says `xbtn` but the layout
"M X Y Z S A C B R L D U" puts X at bit 10), start = START.

First R60 gameplay run (3600f, coin+start+walk+punch): the game
plays — grave-rise intro, gravestone fight, zombie + white wolf,
score/credits/text all correct in FB renders. Numbers at gameplay
load: cadence 1.04, flips 83% (declines 955/3441 — the heavy-frame
tail), handler 67.8 lines, push 300 words, slave-tmo 0,
harvest-fails 102/3441. NTB ~0 during walk-scroll is LEGIT under
NT_WRAP (scroll moves hscroll, not cells; only seam columns ship).

## THE BANDED REVEALS: pending leak + dead cut mode (2026-08-24)

Mike's 4th pass (BUILD cfe50ad0 — WAS the torn-feedback build; f4d23ea
differs only in log files) moved the disease to the TILE planes: Zeus
materializing in horizontal bands (unfilled + off-palette), title-load
jumble, grass pops, a rowscroll band. Zeus is NOT sprites — the live
table holds 2 records there (dumped 0xFF7000 at the scene; the whole
giant is Plane A/B tiles). Two compounding root causes, both measured
via ares-headless dumps:

1. **md_pending leaked to a permanent 162** (ares dumps: pinned at 162
   in storm AND quiet walk, 500+ frames). The evict claim site counted
   a still-dirty victim TWICE; the drain is per-tile-sent, so the
   phantom never drains. pending>=MD_BATCH held forever -> demand bias
   fired every window -> cell chunks throttled to 1-in-3 windows ->
   full NT rotation 27 windows (~450ms). Mike's red-box bands ARE the
   cell cursor sweeping. Fix: count a claim only when the slot was not
   already dirty (both claim sites) + self-heal (a full scan finding
   zero dirty proves backlog empty -> pending=0). Measured after:
   pending=0 everywhere, chunk rate 32->88 per 100 frames (design
   rate), Zeus reveals without bands.

2. **CUT_BLANK has never been in a shipped build** (it needs
   make CUTBLANK=1 — the canonical line lacked it), and even built-in
   it NEVER ARMED: the >=80 claims/chunk threshold was above reality's
   arm point. Measured max claims/chunk at the scene cut: 280 (a full
   chunk); quiet play never near 24. Re-armed at >=24 on the honest
   pending delta, added the direct symptom counter (cells resolving to
   dirty slots -> cb_dirty, belt at >=24), and SPEND THE BLACK: while
   cut mode is armed the tile batch opens 12->40 (the legacy staging
   size) — a ~800-tile cut lands in ~20 windows instead of ~67, mostly
   behind the game's own fade. Load-in at f800-840: clean scene vs
   full-screen jumble, one confetti frame at the cut boundary.

Full-run health (3600f, same script): cadence 1.038, skips 0, flips
91.3% in-ISR (best yet; was 78.7% same-script pre-fix), handler 102.9
lines, consume mean 7.6 max 92 (the one wide-batch spike, during the
blanked cut), cut arms 2 (both at the scene cut — no false arms).

**CUTBLANK=1 is now part of the canonical build line:**
make MDBGALL=1 NTWRAP=1 SPRTRUNC=1 FBTEXT=1 PAL32=1 FMGATE=1 R60=1 CUTBLANK=1

Open from Mike's list: rowscroll tear band + right-edge garbage column
(f1420, NTWRAP seam), grass single-frame pops (52 torn packets/5208 —
records lost for that frame), purple bottom band (watch: may be the
cut CRAM lag, re-check next pass). DIAG[48] = max claims/chunk census.

## THE BLUE-WHITE WEDGE: silent torn landings (2026-08-24, pass 5)

Mike's pass on 8fd3efa-1 (f3d061df): player + gravestone solid
blue/white for 500 frames. His state: PAL_SH sprite blocks 32/33
stale (32 = the intro storm palette = the blues on screen, 33 = ALL
ZEROS), dirty bits CLEAR, remarks == tears (feedback working). The
tear that wedged them was NEVER DETECTED: validation read the magic
at landed-2 — interior FIFO drops PRESERVE ORDER, so the magic always
sits at landed-2 and only the (landed-rec0-2)%8 arithmetic can
object. A drop of a multiple of 8 words (FIFO is 8 deep, drops burst)
VALIDATED, applied a SHIFTED payload into the wrong blocks, and the
68K cleared the marks believing it shipped. No BAD1, no re-mark,
wedged for the session. Timing decides the victims — my headless
repro attempts converged while Mike's live run wedged 32/33.

Fix: the packet is now SELF-DESCRIBING — tag bits 9..0 carry the
exact word count (max 924 fits), harvest requires landed == declared.
Any drop, any count, any abort -> torn -> BAD1 -> heal. Measured:
tears detected rose 30 -> 51 on the same script (the 21 were the
silent class), remarks 1:1, ZERO stale sprite blocks at f2400 on both
input scripts (was: 2 wedged blocks every run, victims varying).

Also classified: PAL_SH blocks 4/5 "stale" in every dump = the game's
palette color-cycling (values are the same ring, rotated) caught in
flight — expected, not a defect.

Watch item: VISRFLIP in-ISR share swings 76-91% between adjacent
builds on identical scripts (timing resonance with the post-wait).
Tear-relevant numbers are steady: flip-pos max 36 < 38-line vblank,
skips 0, cadence ~1.04. Rank builds on those, not on the ISR share.

## THE TORN HALVES: BANDSHIFT=16 fell off the R60 build (2026-08-24, pass 6)

Mike's pass 6 (8fd3efa6): player torn top/bottom halves, smoke top
half frozen ("no frame updates"), Zeus bands flickering between
palette epochs. Root: DIAG[13] = 5970 in 3124 cycles = 1.9 bands per
frame hit a full depth-4 queue and are NOT enqueued (the LOOP18
complete-or-defer policy). The dropped half-band is always the
MASTER's rows; at 30Hz "a complete band one cycle late looks like the
arcade one frame ago", but at 60Hz ADJACENT bands one frame apart =
torn moving sprites at rows 72/144 — the policy's premise broke at
the cadence change. The slave idles 46-187 polls/cycle throughout:
imbalance, not capacity (LOOP18's exact diagnosis).

LOOP18 built the fix (BANDSHIFT, `make BANDSHIFT=N`) and BANDSHIFT=16
shipped in every LOOP19-24 canonical bundle — it FELL OFF when the
R60 flag line was written. Also: drop_s0 (the rotation the drop
comment promises) is written nowhere — vestigial from the killed
rotation policies; the comment lies.

60Hz re-sweep (3600f, same script, defer/cyc | isr-flips | fallback):
  bs0  1.91 | 2598 | 813
  bs8  1.85 | 3220 | 224
  bs16 1.63 | 3242 | 200   << flips best, cadence 1.034
  bs24 1.42 | 3231 | 213
  bs32 1.37 | 3218 | 224   (stale 135, span max 142 — overshoot)
BANDSHIFT=16 confirmed at 60Hz (same verdict as the 30Hz sweep).
94.2% flips in-ISR = best ever measured. Parity statics unchanged.

**Canonical build line is now:**
make MDBGALL=1 NTWRAP=1 SPRTRUNC=1 FBTEXT=1 PAL32=1 FMGATE=1 R60=1 CUTBLANK=1 BANDSHIFT=16

Open after this pass: residual ~1.6 deferrals/cycle (storm frames —
the tear class shrinks, does not vanish); Zeus flash band flicker has
a second component (palette-flash pen remaps force tile re-ships at
rotation cadence); text impermanence during the storm (f881-885);
purple bottom band (still un-diagnosed); load-in trickle (bounded by
tile transport; the bake is the real fix).

## PASS 7: the rest of the fallen bundle, and three verdicts (2026-08-24)

BLITSKIP=1 DIRTYROW=1 SPRBAKE=1 — the remaining LOOP19-24 canonical
flags — restored to the R60 line (same archaeology as BANDSHIFT: they
fell off when the R60 flag line was written; no R60 conflicts). A/B at
3600f: cadence 1.034 -> 1.023 (best ever), rejects 1.9% -> 1.1%, worst
ISR span 134 -> 81 lines, deferrals 1.63 -> 1.56. Flips flat.

**Canonical:** make MDBGALL=1 NTWRAP=1 SPRTRUNC=1 FBTEXT=1 PAL32=1
FMGATE=1 R60=1 CUTBLANK=1 BANDSHIFT=16 BLITSKIP=1 DIRTYROW=1 SPRBAKE=1

VERDICT — white lightning (Mike item 7, FPGA+MAME refs): the arcade
bolt is ALWAYS yellow (mamecap census f310-335: yellow flicks with the
strike, white never spikes); ours alternates yellow-bolt and
white-bolt frames. This is the §11 PAIR-CAPACITY WALL with a face:
the bolt's set is FORCED TO SHARE an owned pair on starved frames and
draws in the white-robe set's colors. LOOP19 measured the whole
design: SPRLATE tried and insufficient (628/647 misses had no pair to
give); used-pen-aware sharing is the free 2-of-3-pairs recovery;
offline pack is the real fix (JOB 1 in LOOP19). Mike's queue says §11
LAST — the item now has a visible face; ask before promoting.

VERDICT — purple bottom band (items 10/12, f770/f793 + pass-5 f753):
band height matches the MASTER's rg2 slice EXACTLY in both
calibrations (40 rows at shift 0, ~24 at shift 16). It is a deferred
master half-band whose two-frame-old flat ledge pixels index a CRAM
group reassigned that frame to the orb/smoke magenta family — the
deferral disease x the §11 disease, uniform because flat rows use one
entry. No third bug; fades as those two are fixed. Not reproducible
in scripted runs (live-input timing).

VERDICT — black vertical bands at START (item 2, f145): CUT_BLANK's
blank cover working as designed (blank slot instead of foreign art
for one transition frame). The alternative is the old garbage jumble.

Remaining watch: HUD text single-frame dropouts (item 11) = the same
band-deferral mechanism on the text pass — will shrink with deferrals.

## PASS 8: THE PROFILE, AND WHERE THE MASTER'S FRAME GOES (2026-08-24)

First SH-2 profile (ares --profile, 900f gameplay, both CPUs 384K
cycles/frame = exactly one frame):

  MASTER: m_main(sched+waits) 168K | blit_half 87K | cap_drain 44K |
          flip_span 26K | visr 17K | cram_paint 12K | compose ~15K
  SLAVE:  s_main(idle+stream) 212K | compose 47K | sprites 44K |
          blit_half 41K | text 20K | text_capture 14K

The master barely composes (~4% of its frame); its band drain is
starved by FIXED DUTIES + the window. The deferral tear is fed by the
window length, and the window is blit_half + cap_drain.

NEGATIVE RESULT — BLITSHIFT (moving blit rows master->slave): handler
mean UNMOVED across 24/48/72, flips worse at 24/48. The blit is
FB-BUS-BOUND (the ~47us/row stall floor is one shared write path);
scheduling cannot buy it back. The master's 2.1x per-row premium is
cap_drain on the same bus. Knob kept with a do-not-resweep note.

NAMED NEXT SURGERIES (in expected-value order):
1. cap_drain OUT of the window — the write-log ring the copy_pages
   comment already names ("stays the pre-ack floor UNTIL a full
   write-log ring retires its FB read"). -44K master window cycles.
2. Dirty-ROW blit: skip rows unchanged since that bank's last blit
   (compose already marks written rows via RL_*; needs a per-bank row
   generation). The only way the 87K blit shrinks. Static scenes are
   most rows most frames.
3. §11 offline palette pack (LOOP19 JOB 1) — owns the white bolt,
   smoke inversions, and half the purple band. Queued LAST by Mike.

## PASS 8b ("do it"): the drain rebalance lands (2026-08-24 evening)

Mike said "do it" on the surgery arc. Findings before the lever:

- The canonical build is ALREADY -O2 -flto (`all: release`); the
  earlier "-O0 by default" note in this file was WRONG — struck here.
  m_main's 168K/frame is real work + protocol waits (the two hottest
  loops ARE the FRT-paced park/post waits, 47K/frame, bus-idle by
  design — not reclaimable).
- BLITSHIFT=-24 (rows slave->master): handler UNMOVED again, deferrals
  worse. Bus-bound verdict CONFIRMED in both directions. The window
  cannot be shortened by scheduling, only by removing FB words from it.
- BUT deferrals track the MASTER's row load directly — so shedding
  master rows (both splits) is the drain lever even though the window
  stays put.

Sweep (3600f, defer/cyc | isr% | cadence):
  BAND16 BLIT0   1.56 | 93.0 | 1.023   (pass-8 canonical)
  BAND24 BLIT72  1.31 | 91.5 | 1.042
  BAND32 BLIT72  1.17 | 91.2 | 1.042   << SHIPPED
  BAND32 BLIT96  1.24 | 91.0 | 1.050   (slave window creaks: stale 190)
  BAND32 BLIT112 COLLAPSE (rejects 6.5%, stale 766 — slave 224-row
                 blit blows the window; the overshoot wall)
BAND_SHIFT=32 is the geometric max (36 zeroes the master's rg0/rg1
compose; 40 inverts the ranges — do not exceed 32).

**Canonical:** make MDBGALL=1 NTWRAP=1 SPRTRUNC=1 FBTEXT=1 PAL32=1
FMGATE=1 R60=1 CUTBLANK=1 BANDSHIFT=32 BLITSKIP=1 DIRTYROW=1
SPRBAKE=1 BLITSHIFT=72

Trade knowingly taken: deferrals -25% (Mike's torn-halves family) for
flips 93->91.2% and cadence 1.023->1.042 (inside his live-run noise).
His eyes arbitrate; fallback is BAND16/BLIT0. Geometry verified clean
at the extremes (4-row master compose slices render correct).

Still queued: window shrink via cap_drain write-log ring (only lever
left for the 68K handler tax; needs a design pass on the 68K-side
double-write cost), §11 offline pack (LOOP19 JOB 1).

## PASS 9: the tear ledger, and the drain-wait (2026-08-24 night)

Mike's pass 9 on 8b: deferrals DELIVERED (1.74 -> 1.06/frame live) but
purple bands proliferated and dreq word-loss doubled (6.4%). The
honest tear counter (BAD1 remarks, real torn landings only — DIAG[17]
carries no-push pollution) rewrote the ledger on the same 3600f
script:

  dbabb539  BAND16 BLIT0  no flags        51 tears  defer 1.63
  49eb880d  BAND16 BLIT0  +3 flags       239 tears  defer 1.56
  BAND32/BLIT72 +3 flags                 172        defer 1.17
  BAND32/BLIT72 no flags                 626 (!!)   — BLITSKIP is
            LOAD-BEARING under the rebalance: 184 unskipped slave
            rows blow the window (rejects 3.9%, stale 731)
  BLITSKIP only 457 / +DIRTYROW 180 / +SPRBAKE 407 / all three 183
            — DIRTYROW REDUCES tears (fewer FB words in flight)

Slave idle pacing (the master's tight-COMM-spin finding applied to
s_main's poll loops): no measurable tear change (172->183, noise).
Kept anyway — it is the documented discipline and costs <0.4 lines.

ROOT of most tears: the harvest RACES the 68K's still-running push —
the fixed post-wait loses to storm pushes at the contended drain
rate; the packet reads short and a frame of records is discarded for
nothing. FIX: BOUNDED DRAIN-WAIT — the exact-length tag lands in the
first 22 words, so the harvest polls its OWN on-chip DMAC TCR (zero
bus traffic — cannot slow the drain it watches) until landed reaches
the declared length, budget 700 ticks. Tears 183 -> 105, cost ~7
ticks/cycle, flips/handler unchanged. (v2 with a header-phase wait
measured WORSE, 120 — noted, do not re-add.)

Net at pass-9 end (3600f): tears 105 (vs 239 at pass-8 flags), defer
1.16 (vs 1.56), flips 92.5%, handler 102.9, cadence 1.041. The
remaining ~105 are true losses or >700-tick stragglers — the FIFO
word-loss class the write-log ring family would retire wholesale.

## PASS 10: the purple band's structural kill (2026-08-24 late)

Mike pass-10 numbers confirmed pass 9 (flips 93.4% live best-ever,
tears halved, ISR span max 128->54) with the purple band + bottom-HUD
dropouts as the standing complaint. Root (from pass 7, now acted on):
the band chain is R0->R1->R2, so rg2 is ALWAYS last into the ring =
the perpetual deferral victim = the same bottom rows stale every time.

FIX — RG2 FULL SHIFT: at BAND_SHIFT>=32 the master's rg2 compose
slice was only 8 rows (216-224); they are now the SLAVE's (rg2 shift
40 = full 144-224). The bottom pixels are composed by the slave EVERY
frame regardless of ring state — a deferred rg2 band now loses only
its text/cat1 phases, never pixels. The purple band cannot form.

NEGATIVE — SLAVE LANDING PARK: per-strip parking on a landing-busy
flag moved tears 150->149 (nothing) and cost 300+ ISR flips. The
12-row strip is the atomic contention unit; parking between strips
misses it. Helper deleted; flag writes remain at 0x28FC0.

Drain-wait budget 700 -> 1200 (the rg2 shift slowed drains; wider
budget converts deadline-misses back to waits): tears 150 -> 138.

SHIPPED (74740070): tears 138 (vs 105 on 61da2fda — +1%/frame pops,
the price of the purple kill), defer 1.09 (best), flips 90.4%,
handler 102.4, cadence 1.044. Fallback build is 61da2fda's config
(no RG2 shift) if Mike's eyes prefer fewer pops over the band.

## PASS 10b: rg2 REVERTED — the purple band is not ours to see (2026-08-24)

Mike's pass on the rg2 build FALSIFIED the theory cleanly (the exact
capture request paid off): purple bands at f065-155 = the ATTRACT
DEMO, dozens of frames, ~40 rows (the full 184-224 region, not the
master's 8-row slice), UNIFORM purple with UI sprites drawn on top —
and NO headless run reproduces it: input scripts, a full no-input
attract cycle, 8000+ scanned frames, ZERO purple. The band is
LIVE-ENVIRONMENT-ONLY (Mike's desktop-ares + real input). The
geometry note for the next hunt: the band's top edge sits at ~row
184 = the BLIT split boundary; under MD_BG those FB rows are
transparent outside sprites, so uniform purple means either nonzero
FB indices hitting a purple CRAM entry, or the MD backdrop showing
where the ledge cells should be. The failing STATE is needed:
**Mike — when the purple band is on screen, pause and save a state
(any slot), and hand me the .bsX. One state ends this hunt.**

rg2 shift reverted (defer 1.16->1.09 was not worth tears 105->138 for
a band it did not fix). Drain-wait 1200 kept: tears now 103 on the
same script (best), defer 1.18, flips 92.2%, cadence 1.04.

Shipped config: BAND32 BLIT72 + three flags + CUTBLANK + wait1200.

## PASS 11 forensics: the purple has a NAME in CRAM (2026-08-24 last)

Mike's a14eb8a7 state (band not on screen at save — no purple-index
runs in the FB): cram_mirror holds **0x680E = RGB(112,0,208), THE
purple, at entries 24/32/40/48/88/96/104/112 — pen 0 of eight tile
groups.** S16 convention: color 0 of a tile set is arcade-invisible,
so the game parks garbage there; Altered Beast's garbage IS this
purple. apply_cram copies all 8 entries per set, garbage included —
correct per se; the defect is whatever DISPLAYS pen-0 pixels the
arcade never shows.

Leading theory (fits intermittency + live-only): the cat1/BG rows
composed on the 32X paint pixel-0 through tile_grp[par]; a STALE
GROUP MAPPING (the documented "owed maps -> group-1 garbage" class —
maintenance slot starving under live input load) sends pixel-0 to a
group whose set0 slot holds the purple garbage. The band = cat1-BG
coverage rows at the bottom. Headless never starves the maintenance
slot the same way -> never reproduces.

CONFIRMATION NEEDED: the purple-moment savestate (asked twice now).
Its FB will show exactly which indices paint the band; pen-0 of
mapped groups = theory confirmed, and the fix is maps cadence (cheap)
or the LOOP19 offline pack (right).

## PASS 11b: the purple state decoded as far as format allows (2026-08-24)

Mike delivered the purple-moment savestate (in-game, band on screen).
Established from it + the RTL:

1. The band color = 0x680E (RGB 112,0,208) = S16 tile sets' ENTRY-0
   GARBAGE. It sits in our 32X CRAM at pen 0 of eight tile groups
   (24/32/40/48/88/96/104/112) AND in the SHIPPED MD CRAM record's
   line-2 tail pens (0x800/0x804/0x808 quantized family). Both
   palette paths carry it faithfully; the defect is exposure.
2. HARDWARE FACT (jts16_prio.v:87-96, derived): tile-layer
   transparency on silicon is pixel[2:0]==0, and the all-transparent
   fallthrough displays SCR2'S PALETTE SET, ENTRY 0 — the garbage
   entries ARE legally displayable; real games just keep them
   covered. (Kit-grade fact: any S16 port must reproduce this
   fallthrough or expose garbage differently than silicon does.)
3. The 32X FB in the state holds NO purple field (no such index
   runs) -> the band arrives via the MD PLANE path: bottom rows
   resolving to the purple pens (blank/unmapped cells, vscroll
   coverage gap, or pen-owner churn — undetermined).
4. Still ZERO headless reproduction (live-only trigger).

NEXT SESSION OPENING MOVE: finish the bs1 format map (SDRAM+WRAM are
mapped; MD VRAM/VSRAM/CRAM sections are not) and read the actual
name-table rows + vscroll at the band. That names the fix in one
decode. The state to keep: rom/s16.bs1 as delivered (band on
screen) — DO NOT OVERWRITE IT before the decode.

## PASS 11c: THE PURPLE BAND REPRODUCED — by breaking it on purpose (2026-08-24 night)

"Stale beats hole" (mask uncomposed master slices out of the blit)
was built and REVERTED in one measurement: bq-slot-active is true for
some band EVERY window (the compose pipeline overlaps by design), so
masked slices never received their FIRST blit — and the result was
MIKE'S PURPLE BAND, DETERMINISTIC, in a scripted headless run
(sbh_1500: uniform purple rows ~200-216). THE FAILED FIX IS THE
PROOF: never-blitted FB rows keep boot-era through-pixels and the MD
side (purple pens / backdrop) shows in the hole. The live band =
rows missing their blit under live timing.

The component that decides a row/group needs NO blit is BLITSKIP's
"zero AND already zero in this bank" bookkeeping. A per-bank mask
desync under live timing (torn vints, deferral churn) leaves
persistent through-holes = the band. Headless determinism never
desyncs it — which is exactly the observed live-only signature.

A/B INSTRUMENT SHIPPED: rom/s16_noskip.32x = same build + NOSKIP=1
(identical code path, mask maintained, skip never taken; the
Makefile's own control). If the purple band VANISHES on the noskip
rom in live play, the desync is proven and the fix is BLITSKIP's
mask maintenance under R60 (or its retirement at this calibration —
tears cost re-measured either way).

Numbers unchanged this commit: canonical tears 84-103, defer ~1.18,
flips ~92%, cadence 1.04.

## PASS 11d: NOSKIP VERDICT IN — BLITSKIP mask convicted; verify-per-row ships (2026-08-25)

Mike's A/B: **purple band does NOT appear on the noskip rom.** The
BLITSKIP per-bank already-zero mask is convicted: under live timing
it claims zero for groups holding STALE NONZERO pixels; the skip
preserves them and CRAM churn paints them purple. (Why noskip looks
right: zeros get written, through-bit shows the MD ledge.)

Three fixes measured, two dead:
- WHOLESALE invalidation on fallback flips: DEATH SPIRAL (lost skips
  -> longer window -> missed flips -> more fallbacks; rejects 17.6%,
  tears 1529). The R60 window LIVES on the skip rate.
- ROTATING one group-column per frame (~10% skip loss): still too
  much (tears 443, stale 502). The margin is razor thin.
- **VERIFY-PER-ROW (shipped)**: keep every skip; audit ONE skipped
  group per skipping row via uncached readback; on a lie, drop that
  row's whole mask -> honest rewrite next frame. Lies heal <=2
  frames. Cost ~1 line/frame. Headless: tears 114, defer 1.12,
  flips 92.2%, cadence 1.039, and DIAG[42] (lie count) = 0 —
  consistent with the desync being live-only. state_health now
  prints the counter ("blitskip mask lies").

The desync's true PATH remains unfound (five audits: banks/parity,
FBCLEAR coherency, ROWLIVE coherency, missq bounds, autofill — all
clean). DIAG[42] on Mike's next live states measures its RATE; if it
runs hot, pack row/bank into a second slot and corner it. If his
eyes still catch a 1-2 frame bottom flicker, that is the heal
latency — acceptable until the path is found.

## PASS 12 prep: attribution lands, and it INVERTS the theory (2026-08-25)

Mike: purple bands GONE on the verify-per-row build (the fix holds
live). Remaining: lower-third tearing.

Per-band deferral attribution shipped (0x28FC8[3], printed by
state_health "deferrals by band"). Headless 3600f says the OPPOSITE
of the chain-order prediction: R0(top)=2370 R1(mid)=1319 R2(low)=132.
The bottom band barely defers — so the lower-third tear is NOT the
deferral drop. Leading replacement theory: R2's SLAVE compose is the
chain's LAST launch and routinely still mid-flight at the window, so
its rows ship part-new-part-old = a moving tear frontier inside rows
144-216. Next measurements: (a) Mike's live attribution numbers,
(b) FRT stamp of R2-slave completion vs window edge, (c) consecutive
tear frames to locate the seam row (moving frontier vs fixed 144/184).

Also: pass-10's orphaned slave-park flag writes at 0x28FC0 removed —
they were clobbering MD_PAYOFF's whole-rows counter (the 0.0% reads).

## PASS 12: the band's REAL door — the whole-row exit (2026-08-25)

Mike's pass: purple band BACK with mask-lies=0 — the group audit
never fired. That located the leak precisely: the DIRTY_ROW
whole-row exit `continue`s BEFORE the group loop, so verify-per-row
never audits it. DIRTYROWVERIFY (stage A, first run at this
calibration): **16-18K FALSE dead-claims per 3600f (~4.6 rows/frame),
row bitmap = 176-183 + 193-207** — text rows 22/24-25: THE HUD
REGION. The HUD dropouts and the purple band are the same defect:
rows falsely claimed dead, skipped whole, showing stale/through.
(It also explains the noskip result: noskip re-learns the mask
honestly every row, which keeps was==0x3FF truthful and the exit
safe — the A/B was sound, the conviction just belonged to the exit,
not the group skips.)

FIX: VERIFY THE EXIT — before trusting was==0x3FF && !ROWLIVE, read
ONE rotating group of the actual bank uncached; nonzero -> drop the
row's mask and fall through to an honest write. Lies count in
DIAG[42] with the group audit. Headless: tears 116, flips 91.9%,
cadence 1.051, lies 0 (live-only desync, as before).

STILL OPEN: who CREATES the false dead-claims (a draw path whose
RL_MARK doesn't fire for text rows 22/24-25 — compose_text and both
sprite paths audit clean; the creator hunt continues). The healing
makes it cosmetically moot; the hunt matters for the kit.

Mike's live attribution confirmed headless: R0=1062 R1=789 R2=113 —
deferrals are TOP-dominant; the lower-third tear needs the R2-slave
completion-time measurement next.

## PASS 12c: THE FALSE-CLAIM CREATOR, CAUGHT AND KILLED (2026-08-25)

The captured lying row (y=191, an 8x8 glyph at text column 24, pens
13/14) named the drawer: DEFERRED HUD TEXT. Root: the deferred
cat1/text pass draws its band's rows ONE GAP LATE — into rows the
OTHER CPU is already clearing for the next frame. The clear ordering
was wipe-pixels-then-RL_ZERO, so the interleave clear -> draw -> MARK
-> RL_ZERO left pixels on a dead-marked row: 5376 false claims/3600f,
rows 176-207 (text rows 22/24-25) = the purple band + HUD dropouts.
CAT1INLINE=1 confirmed the mechanism (5376 -> 256) but costs the
documented pickup stall (tears 599) — diagnostic only.

FIX — MARK-FIRST ORDERING at both clear sites: RL_ZERO BEFORE the
pixel wipe. Every interleave then ends with the drawer's MARK last;
the worst case is a one-frame lost glyph (the existing self-healing
dropout), never a lying mask. DIRTYROWVERIFY: 5376 -> **0** false
claims. The whole-row exit is RESTORED (it was never the criminal —
its precondition was being lied to), audits stay as the belt.

SHIPPED: tears 127, defer 1.13, flips 92%, cadence 1.043, lies 0.

Probe-archaeology tax paid this pass: scrap collisions #7 (0x39800 =
md_dbg_base) and #8 (0x39740 = inside the 936-word DREQ ARM landing)
— the capture lives at 0x39750. And the audit-sampling lesson: a
per-row-constant "rotation" samples the same group forever; rotate
with a time term or the audit is a placebo.

## PASS 13: THE PURPLE ROW WAS NEVER OURS — overscan renders the line table (2026-08-25)

Mike's third purple state, decoded with the full format map, closed
it: BOTH FB banks correct at every bottom row, REAL 32X CRAM correct
(only through-bit diffs vs the mirror), priority=32X, marks true,
lies=0. The 32X's 224 rows are RIGHT. The purple is DISPLAY LINES
>=224: line-table entries 224-255 were never initialized (zero), so
a renderer that draws NTSC overscan through the 32X shows THE LINE
TABLE AS PIXELS — decoded through the state's real CRAM that row is
lavender (128,152,208) + THE purple (112,0,208) speckle. Current
ares-debug clamps 32X scanlines at 224 (verified in source; headless
screenshots are 243 tall with CLEAN overscan) — Mike's desktop ares
build renders them. Hardware drives border there: REAL SILICON NEVER
SHOWS THIS.

BELT SHIPPED: Hw32xInit points line-table entries 224-255 at image
row 223 (benign repeat where rendered, invisible where clamped).
Every earlier "purple band" verdict that blamed skip/marks should be
re-read against this: the mark-race fix (pass 12c) was real and
stays; the residual row was this.

ASK MIKE: one run of the same rom in the ares-debug DESKTOP app
(build_macos/desktop-ui/Release/ares.app) — if his usual ares still
shows a purple row on this build, the belt missed; if clean in both,
closed for good.

## PURPLE BAND CONVICTED — HEADLESS PAIRED CAPTURE (2026-08-25)

The pass-13 overscan verdict is DEAD, killed by its own protocol run
entirely in ares-headless (no manual capture needed — --screenshot N
and --dump are frame-exact and deterministic, so screenshot+memory
from the same run ARE the paired capture).

Reproduction (rom 6633ecdc, reconstructed input script
`discover/inputs/play2.csv` — the original was lost with a session
scratchpad, now checked in): band visible at frames 700, 1200, 1400
of a 14-frame fan over 500-1800; absent at the other 11. Solid
(144,0,255), screenshot rows 203-226 of 243; active image spans rows
11-234, so the band is ACTIVE LINES 192-215 — inside the 224-line
image. Not overscan. The Hw32xInit belt stays (harmless) but fixed
nothing.

The conviction chain, every link measured from one deterministic
frame (701-frame run, dumps at end of frame 700 = the band frame):

1. Displayed FB bank, band rows 192-215: ALL 320 pixels index 0,
   CRAM[0]=0x8000 = through — the 32X shows the MD layer there.
   Rows 216-223 of the SAME bank have composed content (indices
   36-38); the other bank has full content at 192-223. So the band
   frame is a bank flipped with 24 rows of R2 cleared-to-through but
   never composed. Flip trace: flips occur EVERY frame in vblank
   (vcounter 498-506) — the flip cadence is healthy; the compose of
   those rows lost the race (or deferred) on band frames only.
2. MD side at those lines: vscroll=32 (VSRAM 32,32 both frames), so
   lines 192-223 read plane rows 28-31 (view rows 24-27). Plane A
   there: tile 0x3FF (all pixel 0, transparent). Plane B there:
   40 visible cells of NT word 0x212D = tile 0x12D pal1. Tile 0x12D
   is SOLID pixel 15. pal1[15] = 0x1C4 (ares 9-bit BGR) = RGB
   (146,0,255) = the band color exactly.
3. The shipper mirror (md_dbg_nt @0x3D200) shows OUR OWN shipper
   shipped 0x212D to B rows 24-27 and blank to A rows 23-27 — the
   content is stable across band/no-band frames, not mid-rewrite.
   This is presumably faithful S16 state: BG rows 24-27 are arcade-
   invisible junk (covered by FG/sprites on silicon); our port
   routes the covering FG/pavement through the FB compose (cat1),
   so the MD backstop behind R2 is legitimately garbage.

MECHANISM: purple flash = R2 compose miss at flip. The band is not a
palette bug, not a line-table bug, not an MD transport bug — it is
the visible face of open defect #2 (lower-third compose losing the
race), rendered purple because the MD plane behind R2 happens to
hold solid arcade-junk tile 0x12D/pal1.

FIX SPACE (not yet designed — conviction first, per handoff):
 a. root: make the flip (or the clear) conditional on R2 compose
    completion for that bank — but the whole-row exit is load-
    bearing (401 tears when deleted); design carefully.
 b. mitigation: ship something sane into plane B rows 24-27 (or
    blank them like A) so a missed compose degrades to a black/
    stale flash instead of purple. Cheap, cosmetic, does not fix
    the underlying miss.

Tooling: ares-headless screenshot fan + targeted dumps replaced the
"ask Mike to pause" protocol outright. Purple-row detector:
scan rows for (r>80, b>80, g<min(r,b)-40) pixel majority.

## ROW-DEFER SHIPS — THE PURPLE BAND FIX (2026-08-25, same day as the
## conviction)

The mechanism, refined once more by measurement: bank1's band rows
held good pavement for 40+ frames and went all-through AT frame 700 —
the k2 blit slice copied freshly-CLEARED sbuf rows into the bank. The
complete-or-defer policy guaranteed dropped bands stay coherent, but
an IN-FLIGHT band at a blit slice shipped its cleared-transient rows.
Nothing at the ship point checked.

THE FIX (three attempts, two measured failures — kept for the record):
1. Defer any open-band row at the blit: 58.7 rows/cycle deferred, a
   quarter of the screen one frame late, motion smeared. OUT.
2. Defer open-band rows that read all-zero (80-long scan): semantics
   right, but the scan sits on the window critical path — cadence
   1.044 -> 1.127, rejects 1.9 -> 9.2%. OUT.
3. Defer iff (band open && ROWLIVE[8+y]==0) — O(1), the audited mark
   state pass 12c proved honest. SHIPPED.

Plumbing: SYNC[8] = slave compose-open band mask (set at link entry,
cleared when the band's deferred cat1+text land — next link's drain,
or the rg==2/WIN_TWO inline drain, or the NOCAT1DEFER tail); SYNC[9]
= master mask (set at BQ_PUSH, cleared at the band terminator).
Single-writer per word, no cross-CPU RMW. Masks read once per
blit_half call so a mid-blit completion cannot split a band across
two frames. DIAG[29] counts deferred rows. NOROWDEFER=1 is the A/B
arm (demonstrates the defect; never ship it). ROW_DEFER requires
DIRTY_ROW (ROWLIVE).

MEASURED, 1900f play2 script, A/B against NOROWDEFER same sources:
- purple: baseline arm 24-row solid band at 3/14 sampled frames;
  fix 0 purple rows anywhere. Falsifier run both directions.
- cadence 1.045 vs 1.044, skips 0/0, rejects 2.1 vs 2.0%, handler
  96.6 vs 96.9 lines, BAD1 remarks 93 vs 73 (inside the known
  build-to-build tear variance; SUSPECT-C class, do not re-cite).
- parity statics (no-SPRTRUNC builds): four scenes within 0.02pp,
  eyehold 42.07 vs 43.72 with a same-build rerun at 42.39 — run
  variance, not a move.
- DIAG[29] ~29 rows/cycle deferred: unmarked rows the ship would
  have written as zeros — this is the dirty-row skip win arriving
  as a side effect, band-open-bounded so it cannot go permanently
  stale (a legit-empty row defers at most until its band closes).

Since the lower-third sprite dropouts are the SAME shipped-mid-
compose event where the MD backstop is real content, Mike's play
pass should re-grade open defect #2 (lower-third tearing) and #3
(HUD dropouts) on this build.

## RETRACTION (2026-08-25, same day): ROW-DEFER did NOT fix the band

Mike's desktop-ares corpus (screenshots/, captured 16:16 on build
aed16b2 — AFTER the fix) shows the purple band live at frames
797-809, at player materialization, full width, bottom rows. The
ROW_DEFER gate closed the 24-row slave-half class (192-215; that
part replicated in his corpus — no 24-row band appears) but the
band SURVIVES through the bottom 8 rows 216-223, the master R2
compose half, plus the level-entry window (headless d0490/d0510,
same rows). "FIXED" above is retracted; the defect is OPEN.

His same capture also shows, on one screen: Zeus with a missing/
stale horizontal band through his torso during load-in (the
deferral-staleness class made visible by a large animating sprite),
inverted load-in palettes (§11), frozen smoke with its lower half
banded, and right-edge column garbage (~last 64px — possibly the
NTWRAP column margin revealed; NOT previously on the defect list).

Lesson, again: the sparse screenshot fan under-samples; Mike's
play corpus is the acceptance oracle. Sweep densely, grade against
his frames, and never mark a defect fixed off a 14-frame fan.

## PURPLE BACKSTOP: BG view rows 24-27 blanked at the shipper
## (2026-08-25, item 1 of the finish plan)

Root understanding, corrected once more: the FG shipper already
blanks cat-1 cells on plane A (their pixels live in the FB), but the
BG pass shipped S16 BG rows 24-27 faithfully — and in the arcade
those rows are junk the silicon never displays (FG covers them,
jts16_prio.v). Our MD backstop under visible lines 192-223 was
therefore a solid-purple wall (tile 0x12D/pal1), and EVERY bottom-
band FB miss — level-entry latency on the master R2 half (216-223),
ship races (192-215), Mike's corpus 797-809 — displayed it.

Fix: the BG NT shipper ships MD_BLANK_SLOT for view rows 24-27. A
bottom-band miss now falls through both planes to the backdrop
(pal0[0], black). Side benefit: junk tiles no longer claim MD pens.
Documented risk: if any scene legitimately shows BG through FB holes
at lines 192-223, this must become "ship FG there instead" — none
known in Altered Beast (every level floors the bottom band).

Measured (1900f play2, per-frame sweep over the whole entry window
480-569 plus step-5 to 1900, 356 frames): purple 0 anywhere,
including the entry window that stayed purple under ROW_DEFER alone.
Cadence 1.048, skips 0, rejects 2.2%, handler 95.4 lines, BAD1 96
(noise class). Parity statics unmoved (title 67.57 vs 67.48-67.50
across arms; eyehold within its 41.65-43.72 run spread).

NOT CLAIMED FIXED until Mike's corpus says so: the acceptance is a
fresh desktop-ares capture with zero purple frames, graded on his
frames, not headless.

## PURPLE CLOSED BY THE ORACLE; ZEUS SCALE-IN CONVICTED (2026-08-25)

PURPLE BAND: Mike's fresh 1348-frame desktop-ares corpus on build
383ba463 (backstop commit 1722039): ZERO frames with a purple band.
His previous corpus had it at 797-809. Closed by the acceptance
protocol — his frames, not headless.

ZEUS SCALE-IN (his frames 136-365), mechanism decomposed by
falsifier, one theory at a time:
- TEXT THEORY (Mike's): FALSIFIED in code — compose_text writes only
  nonzero glyph pixels (if(tp[N])), empty cells skipped; text cannot
  erase sprites, only add.
- DREQ TRUNCATION (SPRTRUNC exposure): FALSIFIED by --trace-dreq over
  the intro: 49008 pushes, 0 FIFO misses. The packet arrives whole.
- 64-RECORD CAP: ACQUITTED for this scene — the game's live list at
  the cut moment holds ONE drawable record (Zeus is a single giant
  native-zoom sprite, rec top=17 bottom=111 in OUR OWN SPR_SNAP — the
  machine has the whole body and draws part of it). LATENT KIT BUG
  regardless: jts16_obj_scan.v:58 `reg [6:0] cur_obj` = the silicon
  walks 128 records; our whole pipe (SPR_SNAP 512 words, snapshot and
  compose loops, packet SPRLEN 596) is sized 64. Altered Beast
  terminates its list early so it never bites here; the next S16
  title may not. Recorded for TOOLKIT.md.
- CONVICTED: (a) the INVERSION is §11 — spr_pair[par][color]==0xFF
  (pair unclaimed during the churn) falls back to base=15<<4, the
  SHADOW RAMP: the sprite literally draws in the shadow palette.
  LOOP19 JOB 1 (offline pack) is the fix. (b) the CUT is deferral
  staleness: R0 deferred 1223/1987 cycles (62%!), R1 805 (40%) in
  Mike's own state dump. A stale band shows the PREVIOUS frame's
  Zeus, one growth step smaller — the cut line is last frame's
  bottom edge, which is why it moves, why it persists through the
  whole scale-in, and why static sprites never show it. The fix is
  the FB-word diet (finish plan item 3): bands must fit the frame.

## ZEUS STARVATION FIX: the chain-drop was the cut (2026-08-25)

The refined conviction (one step past "deferral staleness"): the
rows below the cut showed no Zeus EVER — not one frame stale but
STARVED. The k2 launch's AUTO-30 test passed whenever the last
LAUNCHED cmd had echoed, then the branch DROPPED the un-launched
R1/R2 chain links ("chain unflushed (rare — heavy gap)") and
relaunched R0. Under sustained load — the whole Zeus scale-in —
that repeated EVERY frame: R0 composed at 60Hz, R1/R2 at 0Hz.
DIAG[21] on the fixed build says the chain is pending at k2 on 62%
of cycles — "rare" was false; the drop was chronic.

FIX (one condition): r60_launch additionally requires pend_rg==0 —
a pending chain finishes before a new cycle starts. Overload now
degrades to 30Hz compose UNIFORMLY instead of 60/0/0 by band.

Measured (1900f play2): Zeus scale-in COMPLETE — head, arms, ball,
torso, correct palette, text in its right colour (three symptoms,
one cause: the §11 churn and the red text were the starved bands'
pens never claiming). Purple 0/284. Cadence 1.055, skips 0.
WATCHLIST for Mike's pass: BAD1 remarks 250 (was 96) and in-ISR
flips 69% (was 83%) — the uniform-30Hz compose keeps banks open
longer; if his pass sees new tearing, the next lever is the
FB-word diet (finish plan item 3) to raise the fraction of frames
the chain completes in one vint (DIAG[21] is the meter: 62% today,
0% = true 60Hz compose).

## WORKTREE FLEET ROUND 1: BOUNDED HOLD SHIPS, ROWHASH DIES
## (2026-08-25, evening — Mike's "solve it with agents" directive)

Four parallel worktree experiments, one lever each, all scored by
tools-adjacent score_build.py (fixed battery: 1900f play2, per-frame
entry sweep, artifact detectors, corrected DIAG counters):

- CONTROL (agent): worktree materialized at a STALE base commit
  (22295c2, LOOP15!) and measured 20Hz-era code — numbers discarded.
  The mailbox agent's unchanged-tip score is the true control:
  cadence 1.055, overload 61.7%, BAD1 250, rejects 2.83%.
- MAILBOX AUDIT: NO real preempt-blit timeouts exist ([21]/[22] both
  zero once the DIAG collision was fixed); the timeout path is
  already safe/self-healing. Mike's "385 timeouts" was collision #9.
  No change.
- BOUNDED HOLD: cap consecutive chain-hold skips at 3, then force
  one old-semantics drop launch. Beat control on EVERY metric:
  overload 52.1%, BAD1 181, rejects 2.46%, cadence 1.048, purple 0,
  skips 0. SHIPPED (replicated bit-identical in the main tree).
- ROWHASH BLIT SKIP: NEGATIVE — record and do not retry. Content-
  hash row skip (80-long read pass in blit_half): cadence 1.086-
  1.132, overload 67.7-79.3%, BAD1 467-605. Third confirmation of
  the same law (zero-scan attempt 2, DMAC negative 21): DO NOT ADD
  READ PASSES TO THE BLIT — on ares the reads cost more than the
  saved writes.

Parity statics on bounded hold: unmoved (67.36/91.70/43.36/92.18/
92.19 — all within the run spread). Shipping BUILD b3616893.

Worktree-agent traps fixed for the next fleet: buildstamp now uses
`git rev-parse --git-dir` (was hard-coded .git/HEAD — every agent
hit it); worktrees may materialize at a stale base — agents must
`git reset --hard <tip>` and say so; gitignored inputs (roms/,
md_src/sega_blob.bin) need symlinking from the main checkout.

OVERLOAD METER: DIAG[30] at 52% remains the number between us and
true 60Hz compose. The read-pass law kills blit-side dieting; the
remaining levers are COMPOSE-side (fewer sbuf writes: §11 offline
pack removes pen-churn recompose storms; sprite bake coverage) and
the §16 residency/trickle work. §11 is next (finish plan item 4) —
it is ALSO the visible head-palette churn.

## SELF-CHAIN ARC OPENED: latency solved, bus economy exposed
## (2026-08-25, late)

PROFILER FACTS (ares --profile, 810f intro, symbolized): slave 34%
IDLE-POLLING for its next chain link; master 13% spinning on the
SYNC[2] blit echo, 12% in cap_drain; compose_sprites (the zoom walk)
just 5.6% of the slave. Mike's claim confirmed: horsepower is
abundant — the chain's master-roundtrip-per-link launch latency is
what starved compose, not drawing cost.

SELF-CHAIN (cmd bit 0x40 = slave runs R0/R1/R2 back to back, one
echo): compose_skipped 52 -> 26% — the latency theory CONFIRMED.
But cadence 1.048 -> 1.169, rejects 2.5 -> 12.7%, BAD1 181 -> 636:
the slave's idle gaps were LOAD-BEARING BUS QUIET for the 68K's
window spans and the DREQ drains. Adding a slave park during DREQ
landings (SYNC[12] broadcast) did NOT recover it (1.192/13.8%) —
the contention is the 68K WINDOW span, not the landing.

Shipped meanwhile (NOSELFCHAIN=1 arm, BUILD 8c29a32f): bounded hold
+ bq 8-deep + the master-half ROW_DEFER gap fix — cadence 1.049,
rejects 1.98% (best yet), BAD1 172, purple 0, skipped 52%.

OPEN: make self-chain coexist with the bus economy. Variants queued
for the fleet: park-through-window, fused-R0R1+solo-R2, and slave
self-yield on observed COMM0 window activity (on-chip read, no
master edits). SYNC[12] = slave-park broadcast (master-owned).

## SELF-CHAIN ARC CLOSED: FOUR NEGATIVES, ONE LAW (2026-08-25, night)

Fleet round 2 (worktrees) + one direct build, all against the fixed
battery, all with purple 0 / skips 0:

  arm            cadence  rejects  skipped  BAD1
  bounded-hold    1.049    1.98%    52.0%    172   << SHIPS
  raw self-chain  1.169   12.7%     26.4%    636
  + DREQ park     1.192   13.8%     31.2%    648
  + window park   1.184   13.5%     27.1%    641
  + COMM0 yield   1.197   14.0%     45.8%    657
  + FM park       1.142   10.2%     47.1%    617

THE LAW (do not retry a sixth park shape): the slave's frame-body
idle under the master-relaunched chain is LOAD-BEARING bus quiet for
the 68K's FM-held work (~95 lines/frame). Any self-chain arm either
tramples it (rejects 5-7x) or parks enough to protect it and thereby
refunds the latency win (skipped back to ~47% at still-triple
rejects). Variant A's autopsy pinned WHY every signal-keyed park
missed: the injury begins at 68K ENTRY, before the announce, the
post, or any COMM0-visible state; FM itself (INTMSK bit 15) brackets
the right span but parking it costs the win. Bracketing complete.

WHAT REMAINS for true 60Hz compose (skipped 52 -> ~0): make compose
smaller, not differently scheduled — §11 offline pair pack (kills
pen-churn recompose storms AND the visible inversions), MD-side
text (frees ~96 patterns of compose+blit), SPR_BAKE coverage.
Scheduling is mined out; three independent laws now say so.

Makefile default flipped back: NO_SELF_CHAIN is the default;
SELFCHAIN=1 keeps the experiment reachable. Shipping BUILD cf3aa585
= the bounded-hold economy exactly (score bit-identical).

## §11 ARC OPENS: PRHOLD=6 SHIPS (-25% RAMP DRAWS); TILE-CLASS
## TABLE PROVEN VIABLE OFFLINE (2026-08-25, night, fleet round 3)

Fleet: control (found SPRLATE=1 no longer fits the region guard raw —
+1528B of census code; the sweep agent gated diagnostics behind
SPR_LATE_DIAG and the lean build fits at _end 0x06018e00), pair-hold
sweep, sprite-floor, offline tile-pack analysis.

PAIR-HOLD SWEEP (the pr_age>90 hoard): 48/24 bit-identical to 90
(dead range — the age>=3 steal already recycles what demand forces);
12 -> RAMP 5546; **6 is the knee: RAMP_DRAWS 6059->4545 (-25%),
map_missed 1151->737 (-36%), bad1 IMPROVED 221->204**, rejects 2.19,
cadence 1.054, purple 0. 3 regresses (5601): release-return churn is
real; 6 sits above it. `make PRHOLD=n` knob; SHIPPING adds
SPRLATE=1 PRHOLD=6 to the canonical line. Risk for Mike's pass:
counters cannot see recolour-on-return flicker; watch cycling
sprites.

SPRITE-FLOOR: NEGATIVE. need clamps to [6,10] so tiles never exceed
19 groups; a BINDING 8-pair floor leaves RAMP flat (5077 vs 5021) —
in this scenario there are ZERO tile squatters and the reserved
pairs are held by ABSENT sets (raw[15]=6): it is all pr-hold and the
one-cycle-late map. Bound is not the lever here.

TILE-PACK OFFLINE (the LOOP19 untested lever): **POSITIVE AND
DECISIVE. 268 harvested cycles (ares-headless driver, play2): the
worst instant needs 11 fade-stable tile classes (9 with used-pen
merge) against the 17-20 groups tile_grp actually spends. 11 <= 14:
tiles 11 + sprite pairs 16 = 27 of 32 groups — the whole §11
shortage closes with a static ROM table and NO runtime compare.**
Design (agent-delivered, LOOP19-consistent): tile_class[128]
set->class (0xFF -> existing nearest fallback), class_rep[] for
apply_cram painting, class k owns group k; build_maps' tile walk
collapses to a lookup. Caveat: corpus = title + round 1 (which IS
the current deliverable scope); harvest later rounds before calling
it game-wide. Tools shipped: palharvest_tiles.lua,
palharvest_tiles_ares.py, palpack_tiles.py.

ALSO FOUND (kit/docs): stock MAME no longer renders R60 roms AT ALL
(PAL_SH/SPR_SNAP land zero — the whole display transport rides DREQ
behaviour MAME zeroes). CLAUDE.md's "SPRTRUNC is the only case (a)"
is stale; effectively every R60 pixel measurement is ares-only now.

## TILE-CLASS SHIPS: RAMP DRAWS 6059 -> 1893 (2026-08-25, late night)

The LOOP19 endgame, landed in three steps on top of PRHOLD=6:

1. STATIC TILE CLASSES (TILECLASS=1, sh_src/tile_classes.h generated
   by palpack_tiles.py --emit from the 268-cycle harvest): tiles
   collapse to 11 fixed groups (1..11), grp_key repointed to a LIVE
   member per cycle so apply_cram always paints a displayed row;
   dynamic zone (text + unobserved sets) 12..13; sprite pairs a
   FIXED 9-pair zone (TC_BOUND=14). Budget 11+2+18=31 of 32.
2. NOT-LIVE PAIR STEAL at the map: a pair whose owner is absent
   from THIS prescan (!sused) and gone >=1 full cycle goes to a set
   on screen now. age>=1 matters: the arcade BLINKS actors with the
   lightning strobe, and an age>=0 arm robbed blinking Zeus's pair
   on his off-frames — CRAM ping-pong, shadow_dirty pinned, halo
   drawn as a black silhouette. Damped, the artifact is gone.
3. Same not-live steal (age>=1) in the SPRLATE late-claim rescue.

LADDER (RAMP_DRAWS = sprites drawn in the shadow ramp, the measured
form of Mike's inverted-palette frames; same 356-frame battery):
      baseline (hold 90, no rescue)   6059
      PRHOLD=6                        4545
      + TILECLASS + 9 pairs + steals  1893   bad1 204, rejects 2.35,
                                             cadence 1.052, purple 0
Aggressive arm (age>=0 steals): 1101 but blinker thrash — rejected.
Gates: parity statics unmoved (title 67.24 in the 67.2-67.6 spread,
demo/demo2 identical) — the 0xFF fallback carries uncovered scenes.
Known-not-this: the black Zeus torso blob is the shadow-actor
silhouette path (arcade stipples the mist; we fill solid) — the
smoke/§shadow item, pre-existing, next on the list.

CANONICAL LINE ADDS: SPRLATE=1 PRHOLD=6 TILECLASS=1.
Corpus caveat: classes cover title + round 1 (the current
deliverable scope); re-run palharvest_tiles_ares.py + --emit before
later rounds.

## ROW_DEFER RETIRED (2026-08-26): superseded by the backstop, and
## its cost was Mike's boxed strips

Mike's corpus frame 800 (red boxes): thin ~8-12 row strips of
DISPLACED tile content in the mid/lower thirds during scroll. The
displacement magnitude (~2 frames of scroll) named the shipper:
ROW_DEFER defers ~26 rows/frame at the blit, and a deferred row
keeps the bank's TWO-frame-old pixels. The gate existed to stop the
purple band; the BG backstop then made the purple unreachable at the
root. A/B with the gate off: purple 0 (backstop alone holds, full
entry window swept), BAD1 160 (best ever), cadence 1.046, rejects
2.19. The gate is pure cost now — retired to a ROWDEFER=1 flag,
off by default. Mark-first clears and the SYNC[8]/[9] compose-open
masks stay (harmless bookkeeping; the masks may serve a future
progress-aware blit).

## SHADOW ARC (2026-08-26): black bars and the black blob, three
## mechanisms, three fixes, one build

Mike's labels did the diagnosis (9 "correct with black band" frames
vs "all other frames incorrect", frames 135 vs 701 the killer pair —
same scene, bar flickering):

1. STALE-LUT FALLBACK: the !shadow_dirty term silhouetted every
   shadow record for the whole 64-chunk LUT rebuild, and the pair
   steals keep the rebuild nearly always in flight (pen drift
   1420->3435) — the flickering black BAR across Zeus. Now: darken
   through the stale LUT (bounded, transient error) instead of
   going black. SHADOW_SILH_OLD keeps the old arm.
2. SHAD_CAP: the >48-row silhouette cap lifted to 255 — measured
   clean on the battery (the old saturation fear did not
   materialize on today's budget).
3. THROUGH-STIPPLE: a shadow record cannot darken THROUGH pixels
   (their colour lives on the MD chip) — solid-filling them was the
   black BLOB over the temple. Now: checker-stipple (odd columns
   dark, even stay MD) — the arcade's own mist is a dither, and the
   record's darkening of NON-through pixels (Zeus's own body)
   works normally, which is why the blob turned back into Zeus.

Battery: cadence 1.049, rejects 2.30, skips 0, purple 0, BAD1 145
(best ever), RAMP 2539. Canonical: SHADCAP defaults 255.

## SNAP LATCH (2026-08-26): the split-in-half shimmer was a snapshot
## race, exactly as Mike called it

His read — "that seems to be a race condition rather than a screen
tearing bug" — was correct. SPR_SNAP was refreshed by text_capture
EVERY k2, blind to whether a compose chain (slave) or the master's
half-band queue was still reading it. Under overload (52% of cycles
span two frames) half of an actor's rows composed from frame N's
list and half from N+1's — a growing Zeus split at the compose-half
boundary, shimmering top or bottom as the halves alternated ages.

FIX: the refresh is gated on BOTH readers being done — SYNC[13]
(slave chain latch: opened at R0, closed after R2/ALL — it must span
the whole chain, the k2 lands in the gaps BETWEEN links) and SYNC[9]
(the master compose-open mask, revived as unconditional bookkeeping
from the ROW_DEFER retirement). Bounded at 2 consecutive skipped
refreshes so a chronically busy compose degrades to a rare split
rather than pinning sprite motion at 20Hz.

Battery: cadence 1.047, rejects 2.24, skips 0, purple 0, BAD1 166,
map_missed 379 (the stable-snapshot side-effect: fewer first-
appearance misses). An overloaded cycle now shows the WHOLE sprite
frame one vint late — uniform, invisible — instead of an actor cut
at a horizontal seam.

## THE BANDING ARC, MEASURED TO ITS FLOOR (2026-08-26, CHAINMETER)

`make CHAINMETER=1` (probe; SPRLATE dropped to fit the guard) put
hard numbers on the 52% compose overload, and they end the
scheduling era for good:

      chain span (R0 launch -> close observed)   510.7 lines (1.95 frames)
      master round-trip latency per link          163 lines  (x2 = 326/chain)
      slave WALL time inside one link            282.5 lines
      slave pure compose work (cycle profile)     ~7 lines/link

The 282-line link wall is the tell: compose is 7 of it — the rest
is the slave's OWN window slice blit (picked up mid-link via the
service points, ~55 lines x 1-2 windows at the 47us/row FB stall)
plus paced services. A chain structurally cannot fit one frame:
3x282 + 2x163 >> 262. And BOTH latency-removal shapes are again
measured negative on this build (post-blit CHAIN_ADVANCE: clear-
wait 163->115 but cycles 1784->1603; mid-strip advance never took
effect — the identical-numbers trap caught it): ANY slave compose
overlapping FM/window spans loses more to cart-bus contention than
the latency buys. compose_sprites reads sprite art from CART ROM
per pixel (the SPROBE comment named it long ago) and the cart bus
is what the 68K executes from.

THE EXIT (next arc, fleet-sized): SPRITE ART OFF THE CART BUS —
through the SDRAM render cache like tiles already are, or a
prefetch into the per-frame staging. Kill the per-pixel cart reads
and the economy law that has now killed five scheduling variants
loses its cause. Meters stay behind CHAINMETER=1; SPRLATE[4..9]
are the slots.

Cold-boot note (Mike): first run after cold boot shows mostly-black
Zeus — shadow_lut boot state over zeroed CRAM maps everything dark
until the first full rebuild lands under boot load. Queued: identity
LUT until the first rebuild completes.

## QUEUED CHAIN: THREE GATE ARMS, THREE NEGATIVES — THE SCHEDULING
## SPACE IS CLOSED (2026-08-26, evening)

QCHAIN=0/1/2 (R1/R2 pre-posted to SYNC[10]/[11], slave pulls at its
own gate): pull-immediately 1.185/13.9%, pull-during-FM 1.198/14.9%,
pull-outside-FM 1.148/11.3% (cadence/rejects; base 1.048/2.2%).
EIGHT scheduling variants now share one failure signature, and the
FM-phase INVARIANCE of this round is the proof that ends the era:
it is not WHERE the slave's compose lands, it is HOW MUCH bus time
it occupies per frame — the transport saturates at roughly the
~30% slave duty the master-latency chain accidentally enforces.
Scheduling cannot beat saturation. Only removing bus traffic can.
(The law's three prior forms: read-pass, park bracketing, chain
latency. This is the general statement. Do not schedule again.)

THE EXIT, designed and sized (next fleet): SPRITE ART RESIDENCY.
compose_sprites reads art from CART ROM per word (sd[o]); measured
~89 lines/link of the 282-line wall. Tiles already solved this: the
SDRAM render cache + miss queue + budgeted cache_fill. Sprites join
it: art fetches go through cache lookups keyed alongside tile
codes; miss -> enqueue + skip chunk (one frame of hole per NEW art,
the load-in-trickle class, self-healing); the ZOOM WALK re-reads
the same art rows every frame, so steady state is ZERO cart reads
for live sprites — Zeus scale-in becomes cache-hot after its first
frame. Wall per link drops toward the ~7-line work floor, chains
fit ONE frame, the 40-52% overload and its banding die of the same
cure. Risks to engineer: tag-space separation from tile codes,
set-capacity thrash (tiles+sprites sharing 128 sets), the gated/
shadow/zoom draw paths each touch sd[] and must go through one
fetch shim. QCHAIN stays as flags for the post-residency retest:
with the bus pressure gone, the latency wins may finally be free.

## SPRITE-ART RESIDENCY: BUILT, CORRECT, AND NEGATIVE (2026-08-26)

Fleet round: implementation (worktree 8e9118d, SPRRES=1 flag) +
working-set analysis. The implementation is clean — blank-chunk miss
semantics (miss returns the zero tile, decode loops take their own
transparent path bit-exact), misses converge (~1.2k over 1910f),
no persistent holes, gates all pass, region fought to 0x18f68.
AND THE WALL DID NOT MOVE: 278.7 vs 276.5-line control.

WHY (the attribution error, corrected): the "~89 lines/link of
sprite cart reads" from the sprites-off A/B was compose WORK (loop
+ sbuf writes), not cart-bus time — 97% of 1:1 records ride
SPR_BAKE's direct path (excluded by design), and the live decoder's
cart reads were already SH-2-cache burst-amortized. Residency
swapped SDRAM bursts for cart bursts ~1:1. NEGATIVE; flag kept
(SPRRES=1, off). The working-set analysis banks the future case:
sprite chunks fit 128x8 with worst combined set load 5/8 and
adjacent-frame reuse 1.000 — if the BAKE blob (658KB, the actual
97%) ever needs residency/streaming, the geometry is proven.

Also priced for posterity: a 6-insn per-word shim in the decode
loops costs +11 lines of link wall — per-pixel instrumentation
there is never free.

THE WALL'S REMAINING UNKNOWN: sprites-off link wall is 194 lines
for ~5 lines of compose work. What fills it — window-blit pickups
riding inside links (~37/link expected), the sbuf CLEAR pass, cat1,
pacing? NEXT MEASUREMENT (before ANY further design): a phase
decomposition meter inside slave_concurrent_k — FRT stamps per
phase (clear / cat0 / sprites / cat1 / text / service+pace),
CHAINMETER-style. The banding arc proceeds from that number or not
at all; two attribution errors (cart reads, scheduling) have now
each cost a round.

## LINK-WALL PHASE DECOMPOSITION (2026-08-26, CHAINMETER round 2)

Per link (2278 links, cart-resident phase_add helper; launch-latency
meter retired to pay the guard — its number is banked at 161-176):

      head cat1 drain (prev band's deferred pass)   48.5 lines
      sbuf CLEAR pass                               34.4
      sprites (strips + their service pickups)      99.5
      rg2 inline cat1+text (per rg2 link ~3x)      105.4 (avg/link)

OPEN CONTRADICTION, recorded before anyone builds on either number:
these sum to ~860 lines/chain, but the span meter read ~505. The
span meter quantizes to k2 observations and only counts CLOSED
chains, so the phase sums (direct FRT inside the function) are the
likelier truth — meaning chains are WORSE than 2 frames and the
window-blit pickups riding inside compose phases are the bulk of
"sprites" and "rg2 tail". RECONCILE BEFORE DESIGNING: a per-link
wall stamp (entry->exit FRT in slave_concurrent_k itself) settles
it in one run.

What the decomposition already makes actionable regardless:
- the CLEAR is ~100 lines/chain of pure sbuf wipes (DIRTY_ROW skip
  already on) — a real diet candidate (clear only rows the coming
  passes will draw is the old idea, but now it has a price tag);
- the HEAD DRAIN (~145/chain) is the cat1-deferral architecture
  visiting every band twice — NOCAT1DEFER exists as the A/B and was
  playability-negative long ago, but that verdict predates TILECLASS
  and the pen-churn fixes: cheap re-test;
- the rg2 tail concentrates the window-blit pickups (no service
  points inside cat1): the strobe design's bill.

Shipping unchanged: 322630ba-line (BUILD 681f5036+ stamp drift only).

## METER RECONCILED (2026-08-26, late): the phase meter is exact,
## the span meter retired

Entry->exit wall stamp: 282.5 lines/link; phase sum 284.0 — the
decomposition is self-consistent. The old chain-SPAN meter (505)
under-read (k2-quantized, closed-chains-only) and is RETIRED; the
launch-latency number (161-176/link) still stands from its own
meter. CAVEAT FOR ALL SLAVE-SIDE LINE FIGURES: totals exceed
runtime by ~27% at 46 ticks/line, so the SLAVE FRT's tick-per-line
calibration differs from the master's — calibrate before quoting
absolute lines; the RATIOS are clock-free and solid:

      head cat1 drain   17%   (deferral architecture's double visit)
      sbuf clear        12%
      sprites phase     35%   (compose strips + window-blit pickups)
      rg2 tail          36%   (uninterruptible cat1 + text + pickups)

NEXT MOVES, in order of cheapness:
  1. NOCAT1DEFER retest (CAT1INLINE=1): the deferral buys the head
     drain's 17% double-visit and its verdict predates TILECLASS +
     the pen fixes. One battery + eyeball.
  2. Separate the blit-pickup time from true compose inside the
     sprites/rg2 phases (stamp slave_window_k inside the service
     path) — decides whether the wall is compose or blits.
  3. Clear-diet (12%): wipe only rows the coming passes will touch.
  4. Slave-FRT calibration constant for honest absolute numbers.

## CAT1INLINE RETEST (2026-08-26): CURED BUT WALL-NEUTRAL

The old verdict (tears ~600, diagnostic-only) is DEAD — with
TILECLASS + PRHOLD + the shadow/pen fixes the inline arm gates
clean: cadence 1.051, rejects 2.19, bad1 160, purple 0. But the
wall is unchanged (281.7 vs 282.5): the head-drain 17% was the same
work done a link later, not waste. VALUE = architectural: inline
kills the cross-link deferral class (the pass-12c mark race, the
chain-drop cat1-loss edge, the one-gap-late machinery) at zero
measured cost. CANDIDATE for canonical pending Mike's play pass —
flag CAT1INLINE=1, rom preserved in the scratchpad (s16_c1i.32x).

The wall's composition after both retests: ~55% compose passes
whose absolute size is honest work, ~45% window-blit pickups and
services riding inside the phases. The next real lever is the
blit-pickup split (stamp slave_window_k in the service path) and
then the blit itself — which the FB-write floor law bounds. The
banding endgame is therefore likely a CADENCE POLICY decision
(coherent uniform 30Hz compose under load vs split 60) unless the
blit diet finds real rows. Mike's call to make with data in hand.

## ROWGEN: TWO ARMS NEGATIVE, AND ATTRIBUTION ERROR #3 RETRACTED
## (2026-08-26, evening)

ROWGEN (write-tracked row skip) v1 (all-dirty flags) and v2
(granular strip/text-row marks) both changed NOTHING (skipped
52->52-53, wall 282->288-301). The premise is retracted: the
ROWSTALE "96% stable rows" figure was a SAMPLING ARTIFACT — under
BLITSHIFT=72 the master blits only rows 184-223, so the probe's
master-side rowslot sampled the becalmed bottom belt, not the
screen. Real staleness during gameplay is low (scroll + sprites
cover most rows most frames). ROWGEN=1 kept as a flag for static-
heavy scenes; not canonical. THE PATTERN, now three deep (sprite
cart reads, scheduling placement, row staleness): measure the
attribution ON THE SAME ROWS/UNITS the fix would act on, before
building the fix.

SHIPPED INSTEAD, the sweep's unclaimed win: **BLITSHIFT 72 -> 24**
(the rebalance knobs predated TILECLASS's master relief; re-swept).
Full battery: BAD1 127 (was ~160-172), rejects 2.09, cadence 1.048,
skipped 50.3, purple 0, RAMP in family. Statics: see parity run
this commit. CANONICAL LINE: BLITSHIFT=24.

STATE OF THE 60HZ QUESTION after ~15 instrumented attempts: every
scheduling/caching/skipping lever is measured-dead against the same
bus-saturation wall; chains run ~1.7-2 frames on heavy scenes and
the split/banding follows. The remaining REAL exits are the
architecture items PIPELINE.md always named — per-scene bake
(S5/T4), FBSPR-style staged sprite records, MD-side text — each a
session-plus, each shrinking the actual work, none a scheduling
trick. Or the fidelity call (coherent-30 under load). Mike's call.

## CORPUS VERDICTS, 2026-08-26 EVENING (Mike's archived-pair protocol,
## first use — rate-based, alignment-immune, load-in excluded)

Old corpus (BLITSHIFT=72, 945f) vs new (BLITSHIFT=24, 805f), frames
>120 to skip the always-messy splash load-in (Mike's caveat):

      STRIP (displaced/torn strips)  14.2% -> 2.9%   FIVE-FOLD DOWN
      SILH  (solid-black actors)      5.6% -> 8.6%   flat-to-noise*

The BLITSHIFT=24 rebalance is corpus-verified: the strip-banding
class Mike red-boxed is now rare. (*SILH: both corpora show the
GIANT-ACTOR solid-black phase — frame 305 new: Zeus fully blacked
including the head, dithered fringe at the record's bottom edge.
The arcade blacks only the mist-covered lower body and keeps the
head. Solid fill + sparse-art fringe suggests the shadow walk is
filling solid where the mist art is sparse — an art-walk suspect,
queued with the bake arc. Detector also counts legit black wolves;
rates across corpora are scene-mix-sensitive.)

Arcade-parity meter (corpus_compare): 91.5%/74.7% vs broken-era
91.3%/74.1% — FLAT, which indicts the METER (80x56 global L1 cannot
see band-rows or palette classes), not the progress. Rework queued:
artifact-aware per-anchor scoring.

## OWNER-MATCH: THE BLACK-ZEUS BLOB WAS A ONE-LINE BUG (2026-08-26)

Mike's fresh-corpus frame 305 (giant solid-black Zeus, head included)
chased to ground: NOT the shadow path — the FB pixels read 0xFA-0xFD
(pair-15 ramp, CRAM 0x0842). The record is a NORMAL sprite (set 3)
drawn UNMAPPED. The allocator state at frame 830: spr_pair par1[3]=7,
par0[3]=0xFF — one parity mapped, the other permanently not, because
par0's build_maps rides its R2 terminator, which the load-in starves;
and the SPRLATE late-claim NEVER CHECKED EXISTING OWNERSHIP: pr_key
is shared across parities, so the set's own pair read as "taken",
the free-hunt failed, and the set ramped black on alternating cycles
for as long as the load lasted (120+ frames measured).

FIX: owner-match first in the late-claim — if pr_key already names
this set, adopt the pair into this parity's map (CRAM already
correct, no paint). Same-sweep verification: black-blob frames
3/30 -> 0/30. Battery: RAMP_DRAWS 3995 -> 491 (arc total: 6059 ->
491, -92%), no_pair 0 (every miss now claims), bad1 132, cadence
1.05, rejects 2.25, purple 0. Statics unmoved (this commit's parity
run). CLAIM FOR MIKE'S CORPUS: no multi-frame black/inverted actor
phases anywhere; residual wrong-palette flashes should be sub-frame
rarities.

## COLOR FAMILY CLOSED BY THE CORPUS; THE COHERENT-30 A/B SHIPS
## (2026-08-26, night)

Mike, on the owner-match build: "the sprites on screen while out of
sync ARE the right colors." His fresh corpus: SILH 0.0% (was
5.6-8.6%) — the black/inverted actor family is CLOSED by the oracle.
Battery on the same tree: RAMP_DRAWS 249, no_pair 0.

What remains is ONE defect: bands out of sync (top/bottom), the
compose-overload desync. The scheduling ledger is closed; the work-
shrink arcs (bake, MD-text) are sessions. So the fidelity fork goes
to the acceptance gate as an EXPERIENCE, not a debate:

  rom/s16.32x             60Hz ship, desync bands under load (today)
  rom/s16_coherent30.32x  whole frames only — flip declines while
                          any compose is open (SYNC[13]|SYNC[9]),
                          cap 4; heavy scenes step at ~30, NO bands

Coherent-30 battery: cadence 1.043, rejects 2.14, purple 0, RAMP
249 — gates identical to canonical. The play pass picks the interim
policy; the bake arc then grows the 60-fit fraction under whichever
ships.

## 30HZ IS DEAD BY ORDER; THE 60HZ PATH IS THE MD-VDP SPRITE OFFLOAD
## (2026-08-26, night)

Mike: "you keep fucking around with slow pattern steps that have
zero place in our architecture. Why on earth are you regressing to
30hz." COHERENT30 deleted (rom, flag, code). The lesson goes in the
lawbook: when the frame does not fit, the answer is the
architecture's unused hardware, not a cadence compromise.

THE UNUSED HARDWARE: the MD VDP's sprite engine — 80 hardware
sprites, 20/line, rendered by silicon at zero SH-2 cost — while
both SH-2s software-draw every actor. Sized from discover/play.csv
(52,001 record-draws over the gameplay corpus):

      native 1:1 records (MD-spriteable class)   97.6%
      zoomed records (must stay SH-2)             2.4%
      single-32x32-fit                           26.6% (rest tile
                                                  into 2-8 MD sprites)

P3 ARC (next session, fleet): normal actors -> MD VDP sprites (art
via the existing MD residency/transport machinery, records via the
68K vint upload path that already carries the game's own list);
SH-2 compose keeps ONLY zoomed actors + cat1 + text. The sprites
phase (35% of the link wall) and most FB blit coverage leave the
SH-2 frame; the chain drops toward fitting 262 lines — TRUE 60,
no policy compromises. Open engineering: MD VRAM art budget
(working set 4-11KB/frame vs ~40KB free), 20-sprites/line ceiling
on mob scenes (overflow actors fall back to SH-2 compose), S16
pp-priority mapping onto MD sprite-vs-plane priority (pp=2
dominant), and the 68K's window budget for the sprite-table write
(~80 words/frame, DMA-able).

## THE FB-WRITE MYTH FALLS: DIRECT-DRAW ECONOMICS MEASURED
## (2026-08-26, night — the srcref lesson, quantified)

Mike: "I gave you the entire 32x library... WHY ARE YOU IGNORING
YOUR SOURCES AND TOOLS." He was right. srcref/cannonball-outrun-32x
(open source, d32xr idioms) and the pooled refs from Space Harrier/
After Burner/T-MEK all draw sprites STRAIGHT into the FB — no
staging buffer, no blit, anywhere. FBBENCH=1 (FRT-timed at boot):

      100 full-row fills   sbuf 1052 ticks   FB 1031   (0.98x)
      100 masked 32x32     sbuf 27998        FB 27749  (0.99x)
      10k scattered FB byte writes: 0.22 ticks each

FB writes through the cached alias cost the SAME as SDRAM writes.
The sbuf+blit architecture pays the whole 224-row copy (~10ms/frame
across CPUs, the pickups, the standalone clear, the coherence
machinery) for ZERO benefit. Boot-quiet caveat noted; the RATIO is
the structural fact (same bus both sides).

THE REBUILD (DIRECTFB): compose writes the FB back bank directly.
  1. Row-pointer redirection in compose_layer/compose_sprites/
     compose_text and the clears (bank from the existing draw-bank
     plumbing); through semantics unchanged (write 0).
  2. Erase pass replaces the full clear: zero-fill only prev+prev2
     sprite-span rows in the back bank (ROWGEN span machinery).
  3. DELETE: blit_half and the window slice calls, the SYNC[4]
     preempt-blit mailbox, sbuf, FBCLEAR/BLITSKIP, ROWSTALE — the
     windows shrink, the 68K handler shrinks, the chain loses its
     ~45% pickup burden and fits the frame.
  4. Keep: snapshot latch, TILECLASS, pen machinery, MD transport.
Expected: chain ~fits 262 lines -> TRUE 60, bands die of cause.

## DIRECTFB STAGE 1 IN-TREE (2026-08-26, midnight): the pivot works

Agent-built (worktree 9b3176d, integrated + unmapped-skip fix),
DIRECTFB=1 flag, canonical untouched. The numbers vindicate the
srcref architecture wholesale:

      bq drops (the chain's overload)   1284 -> 179
      compose_skipped                    50 -> 45%
      cadence 1.040 / rejects 1.87      (project bests)
      _end 0x06005c38                   (sbuf's 78KB returned)
      picture: layer-complete vs canonical in stills, purple 0

Stage-1 discoveries recorded by the agent (kit-grade): the FB
DISCARDS 0x00 BYTE writes (clears must be long fills — the zero-
byte law); the FB bank needs NO plumbing (0x04000000 maps the draw
bank by hardware); sbuf's ±8 borders become explicit edge clips
(a bordered FB layout collides with FB_STAGING at +0x12000); an
AUTO-30 skip must HOLD the flip (the draw bank is 2 intervals old
without the blit's re-ship) — the F1FF decline shape, DIAG[29].

RESIDUALS before Mike sees it: (1) load-in pair churn (map_missed
352 vs 200 — build_maps now runs every cycle where queue starvation
used to freeze ownership); unmapped sets now SKIP one frame instead
of ramping black (safe: no_pair==0, claims always land) — blob
frames 15 -> 2; (2) black patches at SHADOW records — the darkening
path's uncached FB dst reads under the new concurrency are suspect;
(3) FM=0 concurrent FB writes are an ARES-vs-HARDWARE risk (srcref
titles hold FM; our staging-in-FB cannot) — probe before stage 2
deletes the fallback machinery; (4) bad1 279 elevated (expected to
recover with the stage-2 window diet); (5) parity statics unrun on
the flag build. Stage-2 deletion map is agent-delivered and banked.

## DIRECTFB STAGE 1.5: READ-FREE COMPOSE (2026-08-27, small hours)

FM_TEST convicted the black shadow patches' first layer: FM=0 FB
reads return garbage (1507 mismatch / 176 match) — the 68K owns
FM=0 spans for its FB staging, and the shadow/priority passes read
dst through them. Fix: compose is now READ-FREE under DIRECT_FB —
the pp<=1 gate's dst read is vestigial under MDBGALL (when sprites
draw, the FB holds only through+sprites; cat1/text cover by ORDER)
and is ungated; shadow darkening is an unconditional 50% stipple
(no dst read; over sprites it dithers dark, the arcade's own mist
idiom). Battery: rejects 1.76 (new best), cadence 1.041,
compose_skipped 44.1, purple 0.

RESIDUAL, precisely bounded for the next session: load-in frames
~970-1015 show solid dark patches in ONE bank — pixel signature
indices 244-248 (pair-15, pen-varying, base=240+pix: a NORMAL-form
draw with an unmapped base) despite the unmapped-skip. One compose
path still reaches base=15<<4 with pr==0xFF, load-in only, single
bank. Bisect with the signature; the churn (map_missed ~343,
claimed==missed) is the enabling condition.

DIRECTFB remains a flag build. Canonical untouched and shipping.
Stage 2 (the deletion map, agent-delivered) proceeds after the
residual dies and the FM=0-write hardware question is probed.

## PAIR 15 WAS NEVER ACTUALLY RESERVED (2026-08-27, small hours)

The DIRECTFB load-in patches' final layer, and it reaches back
through the project's whole history: every pair-allocation loop
(build_maps free scan, steal, not-live steal, SPRLATE free scan,
steal, not-live steal) STARTED AT q=15 — the SHADOW RAMP. "Shadows
own reserved 15" was a comment, not an enforcement. Under load-in
churn, sets landed on pair 15, drew in ramp-black until their paint
landed, and the paint then CORRUPTED the silhouette ramp for every
shadow actor. All six loops now start at 14. Same-sweep verification
(DIRECTFB): SOLID-ramp patch frames -> 0/356; the s1000 load-in
frame is clean (white smoke, yellow lightning, no patches).

The fix is UNCONDITIONAL — canonical shares the loops. Canonical
battery with it: cadence 1.051, rejects 2.25, bad1 138, purple 0,
RAMP_DRAWS 313, no_pair 0. Suspect for retroactive attribution:
the yellow sprite-ghost era (group 14/pair 7 flip) and every
"pair-15 silhouette" flash across LOOP19's punch list.

DIRECTFB status after 1.5+: patches dead, read-free compose, gates
green (cadence 1.042, rejects 1.87, skipped 44.6). Remaining before
Mike's capture: bad1 ~300 on the flag build (stage-2 window diet
expected to recover it), the FM=0-write hardware probe, and stage-2
deletions per the banked map. Parity statics are MEANINGLESS for
DIRECTFB (MAME cannot render R60 at all — established 2026-08-25);
the ares corpus protocol is the gate.

## DIRECTFB STAGE 2: WINDOW DIET LANDED (2026-08-27, afternoon)

The banked deletion map executed as compile-out under DIRECT_FB (not
hard deletes — canonical must survive intact until the FM=0-write
hardware question is answered on real silicon). Gated: the master
window-branch preempt segment (scmd, SYNC[2]/[5] clears, SYNC[4]
post, cache_purge, both BLIT_HALF calls, pickup/echo waits, slot-5
meter), the slave 0x3000 dispatch (SYNC[4] survives textcap-only),
slave_window_k, blit_half/blit_around whole (RAMCODE — _end
0x06005c38 -> 0x06005698). Kept per the map: the flip, SYNC[9],
fb_draw_par, cap_drain. Canonical rebuilt and re-batteried
BYTE-FAITHFUL: cadence 1.051, rejects 2.26, bad1 138, bq 1313 —
identical to the recorded reference.

THE LANDING WAIT IS NOT BLIT ORDERING — the map's own caveat #4,
confirmed by its falsifier. Deleting the R60 pre-blit landing wait
with the block nearly doubled bad1 (295 -> 549, 1900f battery).
Restored alone at its legacy 1600-tick bound: 382. The bound had
been sized assuming the blit pickup ate most of the push; with no
pickup ahead of it the full push must fit inside the bound. At 4000
ticks: **bad1 77 — best ever recorded** (canonical 138). 3000 ticks:
86. The wait is load-bearing bus quiet for the DREQ landing, full
stop; it now carries its own comment and lives outside the deleted
block.

STAGE-2 BATTERY (1900f, deterministic, reproduced twice):
      cadence 1.036 (best; 1.5 was 1.042)   skips 0  flip-late 0
      rejects 1.61% (best; 1.5 was 1.88)    bad1 77 (best; 138 canon)
      bq drops 276 (canon 1313)             compose-skip 43.0%
      68K handler mean 99.7 (1.5: 100.2)    D28 blit-windows 0

PAIRED SCREENSHOT SWEEP (920 frames each, identical deterministic
input, frame_grade.py): STRIP canon 171 -> dfb 129; HUD dropouts
233 -> 113; purple-band 0 both; SILH flat (attract dark art).
Distribution: canonical's ATTRACT banding (51/120 flagged frames)
is ZERO under DIRECTFB; gameplay-scroll strips are comparable and
slightly higher in the heavy buckets (20/27/31 -> 34/31/33 per 150)
— each band lands in the FB at its own compose time, so a 2-frame
chain paints a scroll mosaic where the blit shipped a coherent
snapshot. That is the DIRECT_FB trade, now measured.

THE WALL ESTIMATE IN THE DELETION MAP IS RETRACTED. CHAINMETER on
stage 1.5 shows slice-blit pickups cost 12 TICKS each there (the
inert loop) — the map's "~55-110 lines of blit pickups" was measured
on FULL canonical, not the flag build. Link wall: 351 lines (1.5) ->
363 (stage 2). The wall is the scheduling law's load-bearing quiet;
the chain does NOT fit 262 lines by deletion and compose-skip stays
~43%. TRUE-60 compose goes through P3 (MD-VDP sprite offload), not
through more window dieting.

rom/s16.32x now carries the stage-2 DIRECTFB build (60c7f36c) for
Mike's next capture. CLAIM, stated in advance: attract-mode banding
GONE entirely; gameplay strip-tears at parity with canonical or
slightly worse in heavy scroll; colors correct everywhere; no
purple, no ramp patches, no silhouettes; input latency and cadence
best-ever. If his corpus grades the gameplay strips worse than
canonical's, the fallback is one make command.

## THE VINT IS NO PLACE FOR ONE-SHOT VDP WORK (2026-08-28, Mike's
## "only new assets update" corpus — regression found, fixed, logged)

Mike's 1073f capture on 6ff04b8f: frozen FB regions, "MV DAUGHTER"
glyph, his read: a redraw step missing. Root cause BISECTED in three
builds: P3's SAT-zeroing block ran inside md_bg_palette — the FIRST-
PAINT VINT — and its ~320 data-port writes stalled the boot
choreography ~40 frames and left the run degraded. The reg5 SAT move
alone: clean. Fix: the zero moved to the pre-game boot section (VDP
free, no deadlines, next to the MDSPR art upload). Verified: timeline
realigned with the known-good corpus, battery identical (cadence
1.037, rejects 1.61, bad1 73), VRAM SAT reads zero.

PROCESS LESSON, paid for twice now: the battery CANNOT see boot-
choreography damage (counters settle to the same steady state) — a
shipped rom gets a SCREENSHOT SWEEP, every time, no exceptions. The
6ff04b8f handover skipped it because "same flags as the swept build".

RECALIBRATIONS from the investigation, all worth keeping:
  - ISR-flip ratio ~49-53% is NORMAL for DIRECTFB builds (the flip-
    hold declines are the design working). The canonical-era ~90%
    reference no longer applies; do not diagnose degradation from it.
  - **bad1 and DRQR[7] (misaligned packets) move together — bad1 IS
    the FIFO-loss family.** Stage 2's landing-wait retune halved it
    (canonical 138 -> 74/1900f). The residual is the ares silent
    FIFO word-loss floor (74-89 per run on BOTH rigs, deterministic
    and real-play alike).
  - "MV DAUGHTER"/"ER." = the TEXT-CHUNK ROTATION STALENESS window:
    text ships as rotating 256-word chunks (full text RAM every ~4
    vints); a dropped packet leaves its two chunks stale until the
    rotation returns. Self-healing, visible at the ~5% drop rate.
    Deep fix = shrink the drop floor; per-word FIFO polling is
    ALREADY in place (K2_FREE FPUSH), and LOOP25's verdict warns the
    naive alternatives are measured-dead. Scoped future arc.
  - The zombie arm-extension attack draws long thin horizontal
    streaks — LEGIT arcade art, repeatedly misread as smearing
    during this hunt (three separate times). Check jtcores/MAME
    before calling a streak a defect.
  - NEW verified defect, both roms: the GRAVESTONE ROW pops in/out
    frame-to-frame in the graveyard scenes (fix_corpus/dfb_corpus
    1668-1672). Record-flicker or load-in class; queued.

## THE BLIT WAS THE COHERENCE LAYER — DIRECTFB PARKED, CANONICAL
## SHIPS (2026-08-28, Mike's second corpus, 882f on 893a0036)

Mike's frames (210-220 flicker chaos, 600/832 lightning never
clearing, 621 double-Zeus + a one-band-tall slice of the ball
floating mid-temple, 832/834 strips through actors + HUD dropout)
are the DIRECT_FB coherence gap, now named exactly:

**Canonical drops 69% of band composes (bq 1290-1545/1900) and NOT
ONE is visible — the blit re-ships sbuf's last COMPLETE band every
window, so a dropped band is one coherent frame late. DIRECT_FB at
14% drops shows compounding staleness: a dropped band's FB rows are
2 frames old, 4 on the next miss, unbounded under load clustering.**
FBBENCH measured only the blit's WRITE cost (zero); its COHERENCE
value never showed on any counter. The frame grader cannot see this
class either — stale-but-plausible content grades clean in stills —
which is why two deterministic sweeps passed builds Mike's eyes
correctly failed. The play pass out-graded the whole instrument
stack, twice.

DECISION: rom/s16.32x = CANONICAL again. DIRECTFB stays a flag
build, PARKED until it has a recovery layer. Design candidates for
"stage 2.5", none free: (a) compose writes FB + an SDRAM shadow
(the freed 79KB), stale bands re-ship from the shadow — costs +3-7%
compose on the chain that already does not fit; (b) starvation-
capped band queue — bounds the age but keeps 1-2-frame strips; (c)
the other FB bank is NOT CPU-addressable (only the draw bank maps
at 0x24000000), so bank-to-bank recovery copies are impossible —
this is WHY staging existed. Also noted: MDSPR does not actually
require DIRECT_FB — only its scratch home does (FBCLEAR's block is
live under canonical); rehoming the scratch lets the MD-sprite
offload mature on the coherent architecture.

BACKPORT SHIPPED WITH IT: the R60 landing-wait bound 1600 -> 4000
in canonical's window (A/B: bad1 150 -> 100, 2500 -> 109; cadence
1.046-1.049 and rejects 2.26 unmoved; bq +16% = invisible-coherent
under the blit). The visible-tear floor on the rom Mike plays drops
a third for free.

## GREEN HUD DIGITS: STATIC TEXT CLASS IN GROUP 0 (2026-08-29)

The mechanism was explicit in the allocator: with 11 tile classes +
2 dynamic groups, a text single that loses the dynamics falls to
`shared_tile` — the HUD literally borrows a TILE group's palette
(green digits = riding a green tile row). tools/text_census.lua
(new, arcade oracle, 540 samples): 8 text sets live overall, worst
instant 7 distinct palettes, but the ALWAYS-ON set is set 0 (527/540
samples, ONE palette state 516/540) — the HUD.

THE 32ND GROUP: apply_cram reserved group 0 for the through bit and
never armed it; entries 1-7 verified virgin after 1500 live frames.
TXTCLASS=1 pins text set 0 there permanently (boot pin; no
allocator/steal/evict loop touches index 0 — they all start at 1 or
bound). apply_cram paints group-0 pens 1-7 ONLY (entry 0 stays the
through bit); pri_lut already handles g=0. The HUD can no longer
lose its palette. Scene text still rides the 2 dynamics (typical
instant: 2 scene sets -> fits; worst instant 7 = transition frames,
transient shared fallback remains).

PAID FOR by extracting m_main's one-shot boot init (258 lines) into
cart-.text m_boot_init() — it ran ONCE and lived in RAMCODE forever.
Guard: 56B spare -> 552B WITH TXTCLASS. Battery unchanged (1.043 /
2.26 / bad1 104); CRAM group 0 live-verified {8000, HUD pens};
HUD + scene text colors verified in frames. Ships in the line.

## MDSPR RUNS ON CANONICAL — TRIPLE LEVERAGE, COLLISION #12 PAID
## (2026-08-29, Mike's 9703f level-1 corpus: "most correct build so
## far"; residuals banding-lower-third / flicker / slowness)

Two truths from his dump, corrected: the MD_PAYOFF block in
state_health prints GARBAGE on non-MDPAYOFF builds (DIAG 60-63 hold
the VISR family; the `if pay_l:` gate passes on live counters) — his
"47.1% whole rows" was a misread. A real MDPAYOFF build measured:
**90.4% of FB longs zero, 50.0% of rows entirely empty** — and the
DIRTY_ROW whole-row exit ALREADY SHIPS (was==0x3FF && !ROWLIVE), so
empty rows already cost ~nothing. The window tax is content+services.

THE PORT: MD_SPR was never DIRECT_FB-specific — only its scratch
home was. Canonical scratch now sits at 0x28D00 (ROWHASH's probe-
only span, #error vs ROWSTALE builds), SAT capped 32 entries both
architectures (real claims run 8-12). Under canonical every claimed
record is TRIPLE leverage: compose shrinks, its sbuf rows stay zero
for the whole-row exit, and sbuf+blit keep the coherence layer — no
staleness hazard by construction.

COLLISION #12: the first scratch cut put the palette block at
0x28EC0 = md_dirty (LIVE) — garbage NT cells with glyph fragments,
and md_dirty's writes smashed the claim counters right back ("20
claims" was the counter dying, not the claims). The :636 memory-map
comment STILL advertises 28D80-28FFF free; it has FIVE tenants now.

MEASURED (1900f battery + zombie-stretch A/B, TXTCLASS rom vs
+MDSPR): claims 1289 (~0.7/frame whole-run, concentrated in
gameplay); **zombie-stretch deferrals 530 -> 462 (-13%)** — the
band-pressure relief lands exactly where Mike's lower-third banding
lives; cost rejects 2.26 -> 2.53pp whole-run (the two vint DMAs).
bad1 111, cadence 1.046, handler 99.8 — in family. SHIPS for his
next pass; fallback = drop MDSPR=1.

His corpus grades (9703f): STRIP 19.1%, SILH 3.6%, HUD-flag 23%
(attract over-flagging as known). Flicker + slowness levers named
for next: dreq_incomplete 8.7% in heavy play (packet diet /
FB-side record transport) and the ~100-line window/ack tax
(cap_drain write-log ring is the recorded lever).

## THE MONTH-OLD BANDS WERE THE MASTER'S 4-ROW COMPOSE SLICES —
## KILLED BY GEOMETRY (2026-08-29, Mike's red-boxed frames 172/763)

Mike outlined the two persistent band stripes in red: frame 172's
sits at rows ~68-71, frame 763's at rows ~140-143 — EXACTLY the
master SH-2's two half-slices at BANDSHIFT=32 (slave composes
0-67/72-139/144-215; the master fills 68-71/140-143/216-223). The
master's slivers compose at a different pipeline moment than their
neighbours; the stripe is that phase difference, fixed at those rows
forever. BANDSHIFT=36 + RG2SHIFT=40 (new knob) hands ALL rows to
the slave: the three seams cease to exist geometrically.

MEASURED (1900f battery + 460f paired sweep vs c8fd75e6):
  STRIP total 107 -> 70 (-35%); the 68-71 spike -48%, lower -24%
  (survivors = the two-way slave band boundaries at 72/144 — the
  temporal deferral-tear family, which geometry cannot remove)
  deferrals 1406 -> 1271 (the slave ABSORBED the 16 extra rows;
  its idle meter had said so: 434 polls/cycle)
  cadence 1.048, rejects 2.58 flat, handler 99.3, bad1 152 (noisy
  counter, session range 99-156)

HOW IT PERSISTED A MONTH — Mike asked; the honest mechanism:
1. The slices date to the original band-chain split. Their seams sit
   at FIXED rows, but the STRIP detector reports positions that
   NOBODY EVER HISTOGRAMMED — a month of grading compared aggregate
   counts. One y-histogram + Mike's two red boxes localised it in
   minutes.
2. Real fixes of OTHER band families (purple/through rows, attract
   banding, deferral policy) kept moving the aggregate, so progress
   registered while this class survived underneath every one.
3. "BAND_SHIFT=32 is the geometric max — do not exceed" was written
   when the slave was saturated and the master NEEDED compose rows.
   The note calcified into law; nobody re-derived it after TILECLASS
   and MDSPR freed slave capacity. The lawbook needs dates and
   preconditions, not just verdicts.
4. Scripted headless runs under-express the seams (light scenes) and
   single-frame eyeballing misread legit art both ways. The
   localisation instrument existed all along; it was never pointed.

CANONICAL LINE: BANDSHIFT=36 RG2SHIFT=40 replace BANDSHIFT=32.

## BOTTOM-VOID POP-IN KILLED: ROW_DEFER REJOINS THE LINE, AND THE
## RIG NOW PLAYS THE WHOLE LEVEL (2026-08-29, Mike's 7115f corpus)

Mike: "banding LOOKS to be solved" — the month-old class closed.
Remaining: HUD/screen pop-in + the strobe. His corpus localised the
dominant pop-in: 855 cell-pop events, almost all in the BOTTOM BAND
(rows ~192-223, the old purple geography) — the ground/ledge renders
BLACK VOID for stretches (zombies standing on nothing), then pops
back. Mechanism: R2's tail ships MID-COMPOSE (cleared-not-redrawn
rows -> MD-through -> black backdrop), and the seam-kill made R2
fatter (80 rows at RG2SHIFT=40), widening the exposure. The defense
was already in the tree: ROW_DEFER v3 (the purple-era gate, retired
to a flag when the purple died by other means).

RIG UPGRADE THAT MADE IT MEASURABLE: play2.csv only covered ~30s —
the graveyard scenes were UNREACHABLE deterministically (the first
"A/B" measured attract-screen black borders and moved 92->94).
discover/inputs/play_level1.csv now fights through the level
(hold-right + punch cadence + kick bursts, 9000 frames): wolves,
graveyard, green zombies all reached. Full-level regression sweeps
are now a standard instrument.

A/B ON THE NEW RIG (1750-frame paired sweeps, gameplay-gated
bottom-void detector): shipping 40 void frames -> ROWDEFER **0**.
Same-frame visual: black void under the gravestones vs full ground.
Battery: cadence 1.048, rejects 2.47, deferrals 1236 (down),
D29 row-defers 51,340 (~27 rows/cycle kept coherent at the ship).
CANONICAL LINE ADDS: ROWDEFER=1.

THE STROBE, QUANTIFIED from his corpus: zero luminance dips in 7115
frames — it is NOT a dark flash; it is stale-coherent frames.
Motion-cadence metric: **35.4% of consecutive captured pairs are
FROZEN** (runs of 1-5) with catch-up jumps between — content
advances ~30-40Hz bursty on a 60Hz flip. That is the compose-skip
~50% mountain (chain must fit the frame): the window/compose/packet
diet roadmap, now with a corpus metric to gate it.

## NEGATIVE RESULT — ATTEMPT 4 (cat1-owed ship deferral) MEASURED
## WORSE AND DEFAULTED OFF (2026-08-29, same evening)

Mike's frame pair 1600/1601 (prior-session corpus — his correction:
those tags predate the ROWDEFER build) caught the GRASS LAYER (FG
cat1, over-sprite tiles) blinking off for one frame while sprites
stayed — the "sprites done, cat1 OWED to the next gap" state (LOOP
10's deliberate deferral) shipping un-gated: ROW_DEFER only defers
UNMARKED rows and owed rows are marked. Mechanism confirmed in code
(SYNC[8] lifecycle); fix built as attempt 4: publish the owed window
on SYNC[10] (dead QCHAIN slot, #error guard) and defer ALL rows of
an owed band at the ship.

MEASURED WORSE: the owed window overlaps the ship CHRONICALLY under
load (~14 rows/cycle deferred, D29 51K -> 77K), and every deferral
displays 2-frame-old content — heavy-scene consecutive A/B (501
frames, fire-hound stretch): grass-blinks 1 -> 5, large swings
4 -> 10. Attempt 1's failure mode at smaller scale. Ship-side
deferral CANNOT fix this class; the owed drain must be gone before
the ship arrives (SCHEDULING — e.g. the owed pass must not be
pending at window entry). Gate off by default (CAT1_OWED_DEFER),
mask kept as telemetry. Rig note: the blink class does NOT reproduce
under the scripted run at normal scenes (0 blinks in 501 consecutive
frames both builds) — only the heavy stretch shows it; Mike's real
play remains the primary oracle for it.

Also from his tags: 3412 = black-silhouette class (pair-starvation
ramp, transient, open, mechanism known); 3674/3777 = bottom dropout
ON THE PRE-FIX BUILD (consistent with ROWDEFER's target class — his
next capture on the fixed build arbitrates); 3991 HUD dropout — not
identified at full res, needs his description of what is missing.

## LOCKSTEP CLAIMS (2026-08-29 night, Mike: "the exact drop out
## effect is still present — if the zombie sprite is underneath a
## gravestone")

His condition named the mechanism: the MDSPR claim/SAT rebuilt at
EVERY landing while compose skips ~50% of cycles under load — the MD
sprite layer advanced while the FB generation froze. A record whose
claim flipped OFF during a freeze (a zombie crossing the gravestone
line flips pp, and pp!=2 un-claims it) was in NEITHER layer: the
fresh SAT dropped it, the frozen FB never drew it. The claim now
runs at CHAIN LAUNCH only — MD sprites and FB content age and
refresh together; skipped cycles re-publish unchanged scratch.
Battery: rejects 2.42 (best of the MDSPR arms), deferrals 1210,
claims 434/1900 (one per compose generation, as designed). The
scripted rig cannot reproduce the transition dropouts (0 events both
arms, graveyard consecutive window) — Mike's play arbitrates.

Priorities per Mike, standing: sprite flickering, screen tearing,
game speed. All three converge on the content-rate mountain
(window/compose/packet diet); the flicker also has two named
side-classes still open (pair-starvation silhouettes, FIFO loss).

## R2 MID-BAND HAZARD GATE — THE GRAVESTONE DROPOUT MECHANISM
## CLOSED (2026-08-30, Mike's frame 3035 + neighbours)

His 3035 vs 3034/3036: ONE frame where the whole bottom band loses
its grass (FG cat1) while the zombie (sprites) stays — the ship
firing inside R2's ~15-line "sprite strips done, inline cat1+text
not yet" gap. ROW_DEFER passes those rows (marked by the sprite
pass); attempt 4 failed because it gated the CHRONIC R0/R1 owed
windows. R2's gap is short and within one link, so the scoped
version works: SYNC[10] bit 2 = "R2 mid-band hazard" (set before
the sprite strips, cleared after the inline drain, reset at chain
launch so a wedge cannot freeze R2), and the blit defers R2 rows
while it is set — the bank keeps last frame's coherent grass, one
frame late, instead of a grass-less band.

MEASURED (consecutive-frame A/B, both windows): heavy-stretch
grass-blinks 1 -> 0, swings 4 -> 0 (attempt 4 for comparison: 5/10
— WORSE both). Battery: rejects 2.36 (best of the offload arms),
cadence 1.048, D29 ~75K (~11 rows/cycle deferred in the hazard
windows). Lockstep claims (same evening) remain in: SAT rebuilt at
chain launch only, both layers age together.

## THE SLOWNESS IS THE PUSH, AND THE PUSH IS ADAPTER-BOUND
## (2026-08-30, Mike: "slowness lines up WITH the tearing" — correct)

TAILPROBE + V-stamps convicted it directly: the 68K vint handler's
~100-line mean IS the DREQ push — stamped from vblank line ~231 to
ACTIVE LINE 80. Census: mean landed 151 words/push = palette 107
(K mean 3.34 blocks) + records 25 + regs/hdr ~19. At 151 words for
~100 lines the per-word cost is ~0.65 lines — the push is bound by
the 68K's two ADAPTER ACCESSES per word (the margin note had it
verbatim; re-derived the hard way). Stability-window test (64->192
ticks on the landing wait): handler 99.8 -> 99.2, dead end — the
drain is not the constraint, the word count is.

Mike's observation is exact: slowness and tearing share the span —
the FM window delays the game's gated stores AND the content
generation; both scale with the same ~100 lines.

THE DIET LEDGER (why this is an ARC, not a patch):
  - Palette K-cap: POISONED — K=4 already failed in Mike's own
    captures (black silhouettes + 60-frame miscolored load-in);
    K=8 + storm escalation is the measured equilibrium.
  - Per-group FIFO polling: trades push lines for FIFO loss (the
    flicker he already reports). LOOP25 verdict stands.
  - Game-palette writes -> FM-gated FB staging: conservation of
    pain — the game's palette engine runs right after our handler,
    inside the FM span; gate stalls replace push lines. UNMEASURED
    whether collisions are actually rare on non-fade frames — the
    first measurement of the redesign arc.
  - WRAM->SH-2 DMA: no such channel exists; the FIFO IS the channel.
The floor: regs+records+minimal pal ~50-70 words ~= 40-55 handler
lines. The 50-line gap is palette transport — the LOOP25 "storm
flush through the FB" design, plus a gate-collision census, is the
next arc. Stability 192 kept (harmless, rides out 68K hiccups).

## PALETTE-TRANSPORT ARC, STEPS 1+2: THE CENSUS, THE WORD-DELTA
## PACKET, AND THE 68000-SHAPING LESSON (2026-08-30)

CENSUS FIRST (tools/pal_fm_census.lua — MAME write-tap on the
0xFF9000 mirror, 12.7k-frame full-level rig; MD 68K timing is honest
there). Findings, each one a design input:

- Palette writes land on 12,453 of 12,700 frames — EVERY frame, not
  fades. The glow streamer (game 0x30F8 loop) is 75% of all writes.
- Only ~10 words/frame actually CHANGE (p50 8, p90 17, p99 24,
  p999 81, max 989 — a level reload). The 107-word pal payload was
  ~10x amplified: 32-word block granularity x redundant rewrites
  (the 0x2628 writer is 91% redundant; the fade engine 87% outside
  fades). Chronic churn confined to blocks 1, 4, 5.
- 80% of writes land with FM held EVEN IN MAME (under R60 the SH-2
  owns FM's fall and composes long). The diet ledger's
  "conservation of pain" is measured fact: FM-gated thunk staging
  is dead, and so is "collision-free non-fade frames".

DESIGN SHIPPED — `make ... PALDELTA=1` (R60 layout v3, single source
packet_fmt.h): 68K shadow palette at 0xFF6000 (24KB WRAM gap below
the sprite mirror; the pal_thunks.h "0xFF5E00" comment is stale —
tile thunks install at 0xFFB820). Pack-time compare ships per-block
word deltas (2 mask words + changed words, ascending); id bit 7 =
raw 32-word block; ONE pal-section length word before the ids so
the SH-2 finds rec0 with a single read (a mask-popcount pre-parse
cost ~200B of SDRAM code the 0x19000 guard did not have); section
padded to %4 (burst alignment). Force-raw mask boots ALL-SET (= the
v2 boot storm, shadow synced as a side effect) and is re-set by the
BAD1 echo and the !ok abort — a torn packet leaves the shadow
claiming words the SH-2 never applied, and a delta re-ship against
a lying shadow is permanent stale colour. Ship-twice survives and
becomes WORD-EXACT: the retry compare catches mid-write races;
empty deltas do the dirty->retry transition without a K slot (this
is what keeps the redundant re-markers out of the packet).

GATES (1900f battery, play2.csv, same session, re-measured baseline):

                        base      delta v1   delta 68K-shaped
  landed mean           145.0     51.8       50.6
  K mean                3.25      0.80       0.80
  68K handler mean      98.8      95.5       72.2
  bad1                  176       147        169 (range noise)
  bq-drops D13          1287      1132       854
  compose-skip          53.4%     53.3%      50.9%
  cadence / rejects     1.053/2.63  1.048/2.66  1.056/2.61

  pal_check.py (PAL_SH vs mirror, end of 1900f): both arms show the
  IDENTICAL signature — only block 4's glow-streamer words differ
  (in-flight ship-twice window). The transport is word-exact.

THE 0.65-LINES/WORD CONVICTION, RE-AUTOPSIED. Cutting 93 words
moved the handler 3.3 lines — the per-word model was an ATTRIBUTION
ERROR (the stability-window null was the hint). Section stamps
(0xFFA0B4/BE/B6/B8/BA/BC, kept; ncmp at 0xFFA0D2, ndirty 0xFFA0D4):

- The SHIP phase does scale with words (~0.6 l/w, both arms).
- The v2 selection block (trivial rotor) measured ~25 lines; the
  first delta compare pre-pass ~55-65. A calibrated 4000-cycle
  busy-loop ran at FULL SPEED both in vblank and at active line 87
  — no ambient bus tax, no H-int (bare rte), and moving r60_push
  to RAMCODE changed NOTHING (it stays — correct on principle).
  The span was the C code itself: 6 block-compares = 55 lines =
  ~110 cycles per uint16 indexed compare at -O2. A 7.67MHz 68000
  punishes index arithmetic; the same loop as long-word
  post-increment compares with rotor byte-skip: 55 -> ~30 lines,
  handler 95.5 -> 72.2.

68K share: 61% -> ~72% (metric 1; target >=80%). The remaining fat
is mapped, not mysterious: compare loop still ~5 lines/block (asm
cmpm.l ~= 1 — ~15-20 lines available), rowscroll compare same
disease (~4), and the ship loop runs ~310 cy/word against ~70 cy of
raw poll+write — the LOOP25 "true DREQ rate 0.24 l/w" note agrees
there is another ~2x in it. NEXT SLICE, measured separately.

NEGATIVE RESULTS (this arc): FM-gated FB staging for palette (80%
of writes inside the FM span — census, not vibes); RAMCODE r60_push
as a speed fix (zero effect — kept for correctness of the fetch
path, not speed); the busy-loop calibration (retired from the code,
lesson recorded here).

Parity statics A/B (same rig, same session): PALDELTA 78.07% total
vs baseline 77.68%, every scene >= (eyehold 44.91 vs 43.03). No
MAME-side regression — note the R60 packet is MAME-invisible
(partial landing reads 0), so this gates the surrounding machinery,
not the delta path itself; ares' pal_check gates that. Shipping rom
left as the PALDELTA arm (BUILD 9668fc67) — MIKE'S PLAY PASS IS THE
GATE, and palette is exactly where his eyes out-grade the rig.

## THE 2214 BAND REGRESSION: R1'S STRIP-PHASE GAP, GATED LIKE R2'S
## (2026-08-30, Mike's corpus + frame tags)

Mike's play verdict on PALDELTA: faster, tearing less prevalent but
same places — and the lower-third band BACK (his frame 2214, red
box), plus boss-smoke silhouettes (8816-9723). Triage:

- The SMOKE is the STANDING LOOP25 storm problem ("the black smoke /
  inverted flash / stale grass"), not a tonight regression — the
  storm-flush design remains the fix, and his savestate (catastrophic
  pen re-claims 18, NT-A cells 65k) is the specimen for that arc.
- The BAND is real and MEASURED as a regression: seam-blink detector
  (scratchpad seam_stale.py, validated firing on his exact 2214)
  over his two corpora: PALDELTA 4.22/1000f vs baseline 1.75 —
  2.4x. His eyes were right again.

MECHANISM, two censuses deep: the chronic cat1-OWED window did NOT
widen (owed-arrivals base 1300/1138 vs delta 772/1044 — flat), but
R1's STRIP-PHASE window did (arrivals base 84 vs delta 108, +29% on
the lighter rig load): rows get MARKED during R1's sprite strips
while the owed bit only publishes at compose-call end — the same
uncovered "marked but grass-less" gap that caused Mike's frame 3035
on R2, which only R2 got a gate for. The PALDELTA phase shift (~25
lines earlier chain) pushes more ships into that gap. Note the
class PRE-EXISTED (84 arrivals, 1.75/1000f in the baseline corpus)
— the diet amplified it, it did not create it.

FIX: SYNC[10] bit 3 = R1 strip-phase hazard, R2's literal twin —
set at R1's strips, cleared at the owed publish, launch-reset with
bit 2, and the ship defers R1 rows while set. SCOPED to the strip
window only; the chronic owed window stays ungated (attempt 4).
Battery (BUILD 7003d3dc): cadence 1.057, rejects 2.55% (best delta
arm), handler 72.9, gate cost ~1 deferred row/cycle (D29 68.8k ->
70.7k), bq-drops 789 (best yet), pal equality clean. bad1 196 = top
of its 136-196 noise band — watch, not blocked. Mike's capture
arbitrates the blink rate; the claim in advance: 2214-class blinks
BELOW the 1.75 baseline, since the gate covers what baseline never
did.

Instrument notes: the owed/strip arrival counters (0x28FF0/F4) were
one-battery-pair instruments, retired into this entry; the region
guard priced every byte of this fix (three FATALs, paid by merging
the gate branches and the hazard-set sites).

## ATTEMPT 5 REVERTED, AND THE 2214 CLASS IDENTIFIED: ONE-FRAME
## VERTICAL BAND DISPLACEMENT (2026-08-30, the full-corpus regrade)

Two instrument lessons first, both bought this afternoon:
- capture.sh's 2:50 default silently truncated every long recording
  (fixed: whole recording by default). Mike's boss run was in his
  .mov all along — re-extraction, no replay.
- The seam-blink detector counted LAUNCHED SPRITES crossing the band
  in one captured frame (his 2038: a kicked zombie) as blinks. The
  width discriminator (>50% of band columns changed) separates them;
  2214 itself survives at 0.95 coverage.

WIDTH-FILTERED RATES (the honest numbers; the earlier 4.22/1.75
were sprite-contaminated):
    baseline          0.27 blinks/1000f
    PALDELTA ungated  1.54   (5.7x — the regression is REAL and
                              bigger than first read)
    PALDELTA + R1gate 2.33   (the gate made it WORSE)

ATTEMPT 5 (the R1 strip-phase gate, R2's twin) is REVERTED at the
ship loop: mid-action R1 shows its deferred rows where ground-band
R2 hides them — R2's tolerance is POSITIONAL, not mechanical. With
attempt 4 this completes the law: NO ship-side deferral for R0/R1,
either window. Bit 3 stays published as telemetry.

THE CLASS ITSELF, seen at last (triptych of his 2329/2330/2331):
the band is VERTICALLY DISPLACED ~8px for ONE frame, full width,
then snaps back — not missing grass. The band rendered from a
different scroll/compose GENERATION than its neighbors: the known
one-frame-late band family at the 72/144 seams. PALDELTA's cadence
change (compose-skip 53.4->50.9, bq-drops down, phase ~25 lines
early) made generation mixing at the seam ~5.7x more frequent. A
strip census red herring is recorded above (arrivals 84->108 —
real, uncorrelated with the visible class).

FORWARD PATHS (Mike picks):
A. V-FLOOR: hold the master's chain launch to the old beam phase
   (the diet's SH-2-side earliness was never the goal; the 68K
   share survives either way). Cheap, one build; rig proxy weak —
   his capture grades it.
B. GENERATION-COHERENT BAND SHIPPING: all bands of a displayed
   frame from one compose generation — the real fix, and it is the
   content-rate mountain's foothill (P3 territory), not a patch.

Also filed: a one-frame solid RED BOX artifact at the right edge
during a zombie launch (his 2330, rows ~72-90) — new-ish, small,
queued. The boss-smoke observation stands refined: bottom third
PRESENT but slow during the storm (coherence holding under load,
content rate starved) — storm-arc evidence, not a dropout.

## ATTEMPT A (V-FLOOR) MEASURED DEAD ON THE RIG — B IS THE PATH
## (2026-08-30, three-point sweep, never handed to Mike)

The chain-arrival census (0x28FF8/FFC, kept) put numbers on the
phase shift: baseline lands the packet at 130.7 lines after pickup,
the diet at 103.6 — 27 lines early, matching the word savings. The
V-floor (spin the master to the old phase post-landing) was swept at
6100/5600/5300 ticks: bad1 223-274 (vs 176 base, ~190 no-floor),
compose-skip 56.5-58.8% (vs 53.4/50.9), bq-drops and D29 worse at
every point. Baseline's extra 27 lines were a DISTRIBUTION — the
FIFO still draining, early landings running early — and a clamp
forces every window to the late edge, eating the inter-window slack
that absorbed jitter; the next push collides and tears. A dead spin
cannot reproduce a distribution. Default off; knob (PDFLOOR) and
census stay for probes.

Shipping rom = PALDELTA, no gate, no floor (the 1.54/1000f seam
class stands as the open cost of the diet's 27 lines). ARC B —
GENERATION-COHERENT BAND SHIPPING — is the fix, per Mike: "A and
then obviously B"; A's failure sharpens B's case (the class is
distributional, so only same-generation shipping kills it).

## ARC B OPENED — AND RE-FOUNDED: THE BLINKS ARE COLOR EVENTS, NOT
## GEOMETRY (2026-08-30, cross-correlation over Mike's strips)

Step 0 falsified my own reading twice. Altered Beast level 1 never
scrolls vertically, so the "~8px vertical displacement" eyeball was
suspect; cross-correlating the blink strips against their neighbor
frames: best alignment at dy=0, dx=+-1px, residual HIGH — the strip
is NOT displaced, its VALUES changed full-width for one frame and
reverted. The blinks are CRAM RECOLOR FLASHES. One class now covers
today's whole sighting list: the cloud "regression", the lightning
yellow box, the launch red box, and 2214 itself (green overgrowth
flashing to stone grey reads as "missing" in a still).

Mechanism frame: CRAM is GLOBAL, not banked. apply_cram runs
mid-window = mid-scan of the DISPLAYED (old-generation) frame; a
tile-group REMAP painted there recolors old pixels under the new
mapping for the rest of that frame. cram_memo now counts the two
paint classes (0x28FF0/F4, reused; state_health updated; memo
DE-INLINED — the counters duplicated per call site and blew the
region guard, and the de-inline SAVED 224B net). FIRST CENSUS
(1900f battery): REMAP 14.6/frame, VALUE 0.38/frame — remaps are
not rare events, the allocator reshuffles ~15 slots EVERY frame,
all painted mid-scan; the visible flash is the subset that
recolors what the displayed generation still references. Why the
delta phase quintuples the visible subset stays open — leading
candidate: the 27-line-earlier apply_cram vs the slave's tile_grp
publish (an unfenced ordering the old idle covered).

THE B BUILD, pinned by the numbers: defer tile-group REMAP paints
to the flip boundary (an ISR-time paint queue, <=32 slots, ~15/
frame typical — fits vblank); VALUE paints stay live (fades track).
Content+mapping revealed together. Band-content coherence remains
B's second half if the corpus still shows mixing after the CRAM
half. ISR surgery deferred to a fresh head by design — the flip
span is the most timing-sensitive code in the project.

DIAG COLLISION #14 logged: cram_paint_spr bumps DIAG[52], which the
R60 push census ALSO uses for nrec — every nrec-mean quoted this
session-era was inflated. Fix when the census is next touched.

## ARC B HALF 1 SHIPPED TO THE ROM: CRAMFLIP — REMAP PAINTS AT THE
## REVEAL (2026-08-30 afternoon, Mike called the "tonight" bluff)

`make ... CRAMFLIP=1`: tile/sprite group REMAP paints route through
an ISR ring (32 entries; head/tail, NOT reset-drain — the ISR can
interrupt a push mid-increment and a zeroing drain orphans the
half-pushed entry) and drain in flip_span right after the latch,
before the restore half. Content and CRAM mapping now switch at the
same reveal, structurally: a declined flip holds BOTH. VALUE paints
(fades) stay live; overflow falls back to a live paint. Routing:
cram_memo's key-change miss sets a flag the next cram_paint eats
(memo-then-paint is the universal call pattern; a stray value paint
riding a stale flag lands one frame late — invisible for a fade).

Gates (1900f battery, BUILD 960d67b3): cadence 1.054, rejects 2.61,
handler 72.5, bad1 186 (mid-band), pal equality clean — and the two
best numbers of the day: bq-drops 664, D29 65990. Claim stated in
advance for Mike's pass: the fight-scene band flicker (2214 class),
the cloud flashes, and the yellow/red one-frame boxes all belong to
the remap-recolor class and should drop hard; speed and tearing
unchanged; boss smoke is NOT this class (storm starvation — its arc
is separate) and should look the same.

## CRAMFLIP FIRST FIELD GRADE: AGGREGATE UNMOVED, COMPOSITION
## POSSIBLY CHANGED — MIKE'S EYES ARBITRATE (2026-08-30)

Mike's CRAMFLIP run (BUILD 960d67b3, full boss sequence): transport
healthy end to end — 60Hz, zero skips, flip-pos 27.7 unchanged, ISR
span +7 lines (the drain, as sized), remaps ~15/frame through the
queue. The corpus number: pre-boss seam-blinks 1.01/1000f — INSIDE
the ungated band (0.94-1.54), not at the 0.27 baseline. Half 1 did
NOT move the aggregate.

BUT the residual events look like a different MIX: the 1469 triptych
shows the gravestones ROCK-STABLE across the blink (the stone/vine
recolor that defined the class is absent) while the detector fired
on (a) a sprite leg flashing BLACK at the band edge — the KNOWN-OPEN
pair-starvation silhouette class — plus (b) an invisible-at-crop-
scale full-width tint shift. Candidate explanations for the
remainder, in order: stale ROW_DEFER rows displaying under the new
mapping (half 2's case — deferred rows are 1-2 generations old and
no flip-paired mapping can match ALL of them), the MD-plane pen path
(mdp; the queue only covers 32X CRAM), and plain event-mix
conflation in the detector. Mike grades the build by eye before any
of those is chased.

Mike's eye verdict on CRAMFLIP (2026-08-30): "the gravestone flicker
looks the same, maybe slightly less frequent" — marginal at best,
not the kill. His anchor example: frames 1690-1695 of the CRAMFLIP
corpus. CRAMFLIP stays flagged (rig-neutral, day's-best bq/D29) but
UNPROVEN as the blink fix; the class file stays open with the three
candidates (stale deferred rows / MD-plane pens / mixed classes).

## CRAMFLIP v2: THE BANK PROBE, THE TWO-FLIP HOLD, AND THE BEST
## BATTERY OF THE ERA (2026-08-30, unattended stretch)

The bank probe (scratchpad bank_diverge.py — deterministic run,
dump both FB banks, compare rows index-level) caught the blink's
true shape at frame 1500: indices 209-212 in one bank are 225-228
in the other — SAME art, +16 = ONE SPRITE PAIR over. A pair
reassignment lands between the two banks' compose generations, the
pixels carry the pair, CRAM matches ONE bank, and the other
shimmers on alternate flips until re-shipped (seconds under
compose-skip) = Mike's gravestone flicker AND the historical "orb
magenta" family (the stolen pair shows the stealer's colors).
Frames 2200/3600 showed the OTHER divergence kinds — different
poses, sprite present/absent — which are legitimate content aging,
not the bug. v1's flaw exactly: it paired the mapping with the
NEWEST bank only; the other bank kept flashing.

v2: the drain ring becomes a 32-slot DEDUPE TABLE (latest mapping
per CRAM slot wins by construction; ~15 remaps/frame always fit)
with a TWO-FLIP HOLD — a remap paints only after both banks carry
the new-pair pixels. Producer unpublishes (qn=0) before rewriting
fields so the ISR can never paint a half-written entry. Cost: a
remap's colors land <=2 frames late on new content, once — vs
seconds of alternate-flip shimmer.

GATES (BUILD bedbafad): cadence 1.042, skips 0, handler 75.1 — and
four project-best transport numbers at once: bad1 55 (prev best
136), compose-skip 44.2% (content rate UP from 50-53), bq-drops
591, D29 37.5k (HALF usual). Plausible chain: dedupe cuts ISR paint
volume, enqueue-not-paint shortens windows, composes fit more
often. pal_check: one extra in-flight word in the chronic fade
block (0x7FFF mirror vs 0x100F PAL_SH at block-1 word 54) — noise
envelope, WATCH. Bank divergence unchanged as designed. Mike's eyes
arbitrate the shimmer on his return; claim: the gravestone flicker
drops hard now (the stale-bank case v1 missed is exactly what the
hold covers).

## UNATTENDED SLICES 1+2: THE M1 THRESHOLD FALLS (2026-08-30)

Slice 0 (instrument): ADAPTERCAL — 400 back-to-back DREQ-ctrl reads
= 68.4 cy/read incl. loop (~54 pure): the adapter access is the
floor (~140 cy/word poll+write), the C macro's ~300 was half fat.
Also: CROSS-BUILD BATTERY DELTAS ARE PART CODE-LAYOUT LUCK — the
deterministic runs can't vary per-rom, so v2's bad1=55 vs the next
build's 184 on near-identical code is alignment noise. NEW
INSTRUMENT RULE: within-build section stamps and means over
cross-build transport counters; distrust bad1 deltas < 2x.
(pal_check's block-1 word 54 acquitted too: it REVERSES direction
across runs — the game's fastest-flashing color, perpetually one
cycle in flight, not a stuck delta.)

Slice 1: compare FAST PRE-SCAN (equal blocks skip the mask walk) —
rotor+cmp section 28 -> 18 lines on samples, handler 75.1 -> 74.6.

Slice 2: r60_ship_words — the ship loop in asm. Poll-per-word LAW
KEPT (tst.b precedes every write; only the encoding tightened);
belt goes chunk-level (a wedged FIFO eats <=one call's words as
ares-documented drops; the packet is torn anyway and BAD1 heals).
Applied to the contiguous sections (regs, raw pal blocks, records,
rowscroll); delta words stay C. Gates: cadence 1.054, skips 0,
pal_check clean, bad1 in-band (the correctness canary for a
mis-shipping loop — it did not explode).

HANDLER MEAN 69.9 — under 70 for the first time. Game-68K share
73.3%: M1'S 73% THRESHOLD IS MET — by that arithmetic the game
logic fits its own p99 frame budget for the first time in the
project. Day total: 98.8 -> 69.9 (share 61 -> 73.3). The remaining
path to 80% (~17 lines) is the compare mean, the C delta ship, and
architectural word cuts (records off the push = the graveyarded S1
family — not without an FM-model change).

## CRAMFLIP v2 FAILED MIKE'S EYES — BAND PARKED, SMOKE ARC ACTIVE
## (2026-08-31, his verdict: "STILL fucking there")

Attempt 6 on the band class is dead. The bank divergence is REAL
(measured, frame 1500: same art, one pair over, one bank) but
fixing its CRAM timing did not kill the visible flicker — so the
DOMINANT visible mechanism is something else or additional:
candidates stand as written (pair churn/SPRLATE during fights,
MD-plane pens, the sub-visible tint shifts, my detector conflating
species). PARKED with mechanism file open. Rule honored: three
eye-failures = stop fixing blind; this class needs a display-level
frame-exact instrument, not another timing patch. The corpus
detector's rates and "composition" reads are hereby DEMOTED to
screening-only — Mike's eyes have overruled them twice.

Priority reset per Mike (and his six smoke savestates, which I
mis-filed as "queued" without ever asking him): THE BOSS SMOKE IS
THE ACTIVE ARC. LOOP25's storm flush is the design; his bs1-bs3
states are the specimens; first step is measuring the storm shape
UNDER PALDELTA (the delta transport changed the channel the LOOP25
census measured — kcap-15 raw pushes now drain ~4x the old rate
and the smoke is STILL black, so the starvation point moved:
re-census before building the flush).

## MIKE'S PIVOT DIRECTIVE (2026-08-31): THE FAITHFUL PIPELINE WAS
## FOR UNDERSTANDING ONLY — GO NATIVE

Verbatim intent: the MAME/jtcores-derived pipeline design was for
understanding the S16, not for shipping. The working pivot throws
away the simulated transport shape and renders level 1 with the
32X's own methods. Today's forensics are the closing argument: the
black smoke lives ENTIRELY in the translation relay (game palette
-> mirror -> DREQ -> PAL_SH -> allocator/memo/queue -> CRAM) — at
the frozen black instant the game truth and PAL_SH were CORRECT
and CRAM was black; the arcade has no such relay to break.

THE NATIVE PALETTE DESIGN (the TILECLASS precedent, extended):
Mike's own censuses already prove level 1 fits static allocation —
11 fade-stable tile classes (TILECLASS, proven offline over 268
harvested cycles) and 4-7 concurrent sprite sets vs 256 CRAM
entries. Per-scene STATIC tables, baked offline from the existing
harvest tooling, loaded at scene entry exactly like the arcade
loads its palette RAM. The game's runtime palette WRITES (fades,
glow — ~10 words/frame by census) apply as pure VALUE updates to
fixed slots. WHAT DIES AT RUNTIME: the group allocator, pair
stealing, the memo, CRAMFLIP's queue, and with them the shimmer,
the orb-magenta family, and the smoke's paint-gating class. The
storm problem collapses to "apply ~10 value words/frame," which
the existing delta transport already carries.

## THE SMOKE'S STANDING BLACK: CONVICTED, AND IT WAS MINE
## (2026-08-31, the deterministic trace, end to end)

The method Mike demanded — self-serve, rig-only — closed the case
in one evening: play_level1.csv BEATS THE LEVEL-1 BOSS unaided (the
whole day's Mike-dependence was self-inflicted); the coarse sweep
found black smoke at frames ~8200-8700; the frozen-frame trace
walked one pixel end to end. Verdict chain:

1. Black pixels = tile groups 4/5. CRAM black, mirror==hardware,
   PAL_SH == 68K mirror == game truth == CORRECT GREYS. The relay's
   final PAL_SH->CRAM paint is the broken handoff.
2. The time sweep: the state STANDS for the whole 900-frame window;
   SETGEN[74] stuck at 1; and group 4's "colored" entries are the
   PREVIOUS SCENE'S palette — the remap to the smoke set never
   painted, and the memo (key+gen recorded at classification time,
   BEFORE the deferred paint lands) never retries.
3. The A/B: the SAME frame WITHOUT CRAMFLIP paints group 4
   word-for-word perfectly. CONVICTED: CRAMFLIP v2's queue can
   permanently drop a remap paint. v1/v2 both retired to the
   negatives — the DIAGNOSIS they served (bank divergence; a remap
   may only land when both banks carry the new pixels) stays true
   and becomes a REQUIREMENT the pivot's static design satisfies
   trivially (no runtime remaps at all).

SHIPPING: rom/s16.32x = 776a2c9b — PALDELTA + fast-scan + asm ship,
NO CRAMFLIP. Battery: cadence 1.047, skips 0, handler 69.2 (the M1
threshold holds). The smoke reverts to the pre-CRAMFLIP EPISODIC
storm class (LOOP25's), no longer sticky — the pivot's per-scene
static palettes are its designed kill.

NEGATIVE RESULT (the day's fifth, and the law it teaches): a paint
DEFERRAL layered on a memo that commits at classification time is
unsound — the gate must commit when the PAINT lands, or not defer.
Do not rebuild CRAMFLIP; build the pivot.

Mike's verdict on 776a2c9b (2026-08-31): "Smoke on boss presentation
still frozen in lower third" — NOT black this run. The smoke file
splits cleanly in two: the PALETTE-BLACK component (convicted =
CRAMFLIP, removed) and the R2 CONTENT-FREEZE component (his earlier
"present, just not updated fast") — storm pushes eating the window
budget until R2 composes stop landing. The pivot addresses both:
static palettes turn the desat storm into ~10 value words/frame.
Next: deterministic R2-advance census through the smoke window.

MIKE'S CORPUS, USED (2026-08-31, his 5668-frame 776a2c9b boss run):
the frozen-lower-third is QUANTIFIED — across his smoke window
(frames ~1000-4600), R2's per-frame change rate medians 0.32% and
reads essentially-frozen in 58% of sampled instants, while R0/R1
right above it run 6.5-6.9% median (~30% frozen). The R2 starvation
face of the smoke is real, large, and his. (A crude black-pixel A/B
across corpora was CONFOUNDED by legit fades and differently-shaped
sessions — not usable; the deterministic rig A/B remains the
palette conviction's proof.) Rig-side R2-advance census pending;
suspects for R2's starvation under storm load: the storm push
budget squeezing composes generally, plus the R2 defer machinery
(hazard bit 2 / R2-owed) possibly over-holding across storm-era
chain restarts. The pivot shrinks the storm itself; the census
decides whether the defer path also needs a look.

## THE FULL-LEVEL ORACLE RIG: 80% BUILT, RECIPE PROVEN
## (2026-08-31, the wolf hunt)

The smoke's R2-freeze does NOT reproduce on any boot-fresh scripted
run — because no script so far TRANSFORMS, and the untransformed
level never triggers the Neff presentation (the pillar scene, the
heaviest sprite load in the game, where Mike's freeze lives). The
scripted runs loop gameplay forever; every prior "presentation
window" label on rig sweeps was wrong. Correction filed.

Built toward the reproduction (all committed, discover/inputs/):
- play_wolf2.csv: survives the whole level at walking punch-spam.
- The CONVERGENT RECIPE, proven through wolf 1: dense-screenshot a
  deterministic run (ares-headless --screenshot frame:file — the
  tool that made the old G_r2haz corpora; zsh word-split ate two
  attempts, use arrays), read the contact sheet, insert a
  stationary kick-storm AT the sighted wolf window (edits only
  affect the future — the prefix stays deterministic), re-scan
  downstream, repeat. Wolf 1 now DIES on schedule (~5400); the orb
  spawns but expires while ledge zombies pin the player — the
  remaining work is collection timing + wolves 2-3, mechanical.
- Also seen at contact-sheet scale: the RED-BOX artifact recurs
  constantly in scripted runs (thumbnails 2132-8920) — abundant
  reproduction material for that small open item.

WHY THIS RIG MATTERS BEYOND THE SMOKE: a transforming,
boss-reaching input is the deterministic full-level oracle — every
future fix, the pivot's acceptance gates, and the frame-for-frame
grade all inherit it.

## THE WOLF HUNT, SESSION STATE (2026-08-31, banked)

Ten ares cycles + a MAME-arcade pivot. Standing results:
- KILLS work (stationary kick-storm at the sighted window: wolf 1
  dies on schedule ~5400). COLLECTION does not yet: the orb spawns
  ground-level in a ~80-frame window while the ledge gargoyle pins
  the player; walking-collection (w3 got 1 orb organically) is the
  proven mode, but blind cadence tuning is a random walk — ten
  variants, still 1 orb max.
- CONVICTION EN ROUTE: **the red blob IS the transform orb** —
  frame 5480 shows it spawn as a correct gold ring, 5510 shows it
  as the solid red mass. patch_game.py's own "patching one
  corrupted a round-1 spawn (red blob)" comment was this class.
  The transformation PICKUP renders corrupted in our port. Filed
  as a pivot-ledger item (rebase-layer sprite corruption) — and
  the wolf sprites flash BLACK repeatedly in the same window
  (silhouette class, reproducible on demand now).
- ARCADE-ORACLE ROUTE (tools/auto_beast.lua, committed): tune the
  input recipe in MAME-arcade at ~70s/cycle, export the timeline,
  replay on ares — the frame-for-frame premise makes the transfer
  exact ONLY once dips + button mapping match the port (our DSW1 =
  0xFF; MAME defaults differ — all three arcade runs died by
  ~8000 where ares runs survive, so the config-identity step is
  MANDATORY before trusting transfers). That verification is
  itself the first frame-for-frame identity experiment the project
  will have run.
- Fastest human path remains open: one real playthrough's timing
  read off a capture can seed the exact csv in minutes.

Next session picks up: (1) match MAME dips/mapping to the port,
(2) tune to transform in-arcade, (3) replay on ares -> the
presentation, (4) the R2-freeze census on the true scene.

IDENTITY EXPERIMENT RESULT (2026-08-31, tools/replay_csv.lua): the
exact w2 csv replayed in MAME-arcade DIVERGES — game-over by 12100
where ares is alive and fighting. Not logic infidelity: the S16
seeds randomness from boot-relative counters and the 32X boot
shifts every absolute frame, so identical wall-frame inputs meet
different RNG states and combat chaos separates. LAW: cross-machine
input transfer requires game-event anchoring (not absolute frames);
until then, INPUT TUNING HAPPENS ON ARES ONLY. auto_beast.lua and
replay_csv.lua stay as the arcade-side rig for event-anchored
experiments; the dip force (DSW2 0xFD / DSW1 0xFF) is in
replay_csv.lua and matches md_main.c:3039.

## THE FROZEN SMOKE, TRACED AND BOUNDED (2026-08-31, Mike's bs1
## caught the bit in the act)

The chain, every link on his specimens: corpus census (R2 57%
frozen through the presentation) -> bs1 static split (sbuf 68.5%
DIFFERENT from both FB banks while the banks' R2 rows were
BIT-IDENTICAL: compose alive, SHIP dead) -> SYNC[10] read from the
frozen state: bit 2 (R2 owed/hazard) SET with rg2's slave compose
open. MECHANISM: the Neff-pillar presentation makes rg2's compose
span ~the whole cycle, so the hazard/owed bit covers ~every ship —
the R2 defer gate, built for a "~15-line gap" (frame 3035), defers
R2 forever. The gate's PRECONDITION died under the scene. (This
predates PALDELTA/CRAMFLIP — it was in every build of the week.)

FIX: R2 DEFER BOUND — after 4 consecutive ships deferred, ship R2
anyway; reset when the bit clears. A real short gap spans 1-2
ships and still defers (3035 win preserved); a stuck window
degrades to one grass-less-risk frame instead of a frozen band.
Gates (BUILD b67e1c99): cadence 1.046, skips 0, rejects 2.18
(best), handler 69.2, D29 normal, pal clean. Mike's boss run
arbitrates; claim in advance: the smoke's bottom row ANIMATES.

## THE FREEZE'S TRUE MECHANISM: BQ TAIL-DROP — COALESCING SHIPPED
## (2026-08-31, the counter that was the bug all along)

The R2-defer bound changed nothing, and Mike's fresh states showed
why: R2's per-band DEFER counters read ~0.09/cycle — the defer
gates were never firing. The freeze lives UPSTREAM: BQ_PUSH on a
full 8-deep band queue DISCARDS THE NEWCOMER (DIAG[13], the
"bq-drops" counter every battery has carried), and the chain pushes
R2 LAST every cycle — so a saturated queue drops R2's push ~every
cycle of the pillar scene (bs1: 896 drops concentrated in the
~1000-cycle window). Compose ran (sbuf fresh), the defer gates
idled, and the blit was simply never told about R2. NOTE: the
"deferrals by band" state_health line is actually the per-band DROP
counters (28FC8) — mislabeled all along; relabel when next in the
tool.

FIX: SAME-BAND COALESCING on full — refresh a queued not-yet-
started same-band entry with the new generation (newest wins,
complete-or-defer's own policy); never touch bq_h; a real drop only
without a candidate. Gates (BUILD 62004359): bq-drops 716-1900 ->
207, cadence 1.050, skips 0, handler 69.4, pal clean. The R2-defer
bound stays as a belt. Mike's boss run arbitrates; claim: the
smoke's bottom third animates. His black vertical MD-side bands
remain filed with the storm arc (plane-cell transport saturation,
64.6k cells at the entrance).
