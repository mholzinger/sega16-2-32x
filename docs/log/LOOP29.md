# LOOP29 — the overnight ladder above opt1 (2026-09-09)

Continues LOOP28 (last entry 106). Protocol: `docs/handoff/LOOP-NIGHT-0909.md`.
Ledger: `docs/log/night-0909/LEDGER.md` (rig: `tools/night_run.py`; roms in
`rom/night/`, gitignored). Entries carry `date` output, the build line, md5,
the sliced speed table, and one-sentence conclusions. NEGATIVE RESULTS are
marked in their headings.

All speed numbers: `tools/gameplay_speed.py` semantics on the level-1
script, [1500,4100] total plus four 700-vint windows; 100% = 60 fps.

## 107. A1: THE ACCEPTED LINE REPRODUCES AT 49.7% (01:36-01:38)

    make clean; make ship-us FBXPORT=1       md5 1812b490   _end 0x60135d0
    build c2ef98f8 (HEAD after the rig commits), stamp R60

    total [1500,4100]   49.7%      IRQ4 misses 50.3% of vints
    1500-2200           49.7
    2200-2900           51.6
    2900-3600           50.0
    3600-4100           46.8
    anim mean           11.7  (windows 11.7 = T_singlebuf, LOOP28 93)
    black% f2000/3000/4000   4.4 / 4.4 / 4.4
    SPRLATE[3] ramp draws    1919 over 2600 vints   (the baseline for B)

Frame f2000 read: level-1 intro (Zeus, "I command you to rise"),
sprites and text intact. START-HERE's number and LOOP28 93's anim
figure both reproduce on today's source, so the rig and the tree agree
with the record. Rig cost: 148 s including the clean build.

Note: the rom that was in `rom/s16.32x` before this tick carried the
FBXSTAGE+FBXBOTH stamp (a double-buffer probe, 46.1%, black 17.9%);
it is in the ledger as `smoke_current` and is not a result.

## 108. A2: OPT1 READS 85.2%, THE LAST TWO WINDOWS ARE AT THE BAR (01:41-01:43)

    make clean; make ship-us FBXPORT=1 TXTWRAM=1 LATESTEAL0=1 LATEKEEP=1 DRAWADOPT=1
    md5 b436ea15   _end 0x60135d0   build 237fda56+

    total [1500,4100]   85.2%      IRQ4 misses 14.8% of vints
    1500-2200           58.6
    2200-2900           87.6
    2900-3600           98.9
    3600-4100          100.0
    anim mean           28.0
    black% / colours    3.7/6188  3.6/4632  3.6/3967
    SPRLATE[3]          1099   (base 1919)

Two expectations from the protocol did not hold, and neither is a
problem:

  - md5 b62237e5 cannot reproduce: `sh_src/buildstamp.h` bakes the git
    short hash into DIAG[18] (m_main.c:8541), so every commit changes
    the rom. The protocol's md5 expectation was wrong; the number is
    what reproduces.
  - 85.2 is not 82.3, and LOOP28 already read this line at 85.2/85.3
    (LOOP28 144, 159, 240) later in that session. The 3 points sit
    inside the 5.9-point floor of LOOP28 88.

The `diff vs base` column (1,317,320 bytes) is not the stale-bake test
here: base and opt1 differ in flags, so the asset region moves
legitimately. That test applies only between same-flag siblings.

**The finding.** The last two windows are 98.9 and 100.0: for 1400 of
the 2600 vints the game runs at the bar on this line. The whole
shortfall is the first window (58.6) and part of the second (87.6),
which is the intro and the first enemies. So the remaining cost is not
uniform; it is a heavy-regime cost. A3's timelines should sample the
heavy window (2000-2012 sits in it) and a light one (3000-3012, 98.9).

Frame f2000 read: level-1 with two purple enemies, HUD intact; the
intro caption has already scrolled off because the faster line is
further along at the same frame (the frame-indexed script, LOOP28 88).

## 109. A3: THE TIMELINES — ON OPT1 THE MISSING VINTS HAVE NO SPIN ON THE DECIDING PATH (01:46-01:47)

`tools/frame_timeline.py`, both roms, 12 vints each at 2000 (heavy
window, 58.6% on opt1) and 3000 (light, 98.9%). Lines from vblank start,
262 per vint. Full output in `rom/night/tl_*/`. Reading the columns:
`raise` = the shim's post (end of the shim's own work), `irq4` = the
game's IRQ4 entry, `miss` = the game counted a lost frame at that entry,
`gate` = the first FM spin inside the game's pass and its length,
`idle` = the pass reached the frame-flag wait.

Base, heavy (2000-2011), one game frame = two vints, every pair alike:

    even vint   raise 69-97   irq4 77-105   pass starts 103-134
                SPIN 8-120 lines at 0xffbe54 (the text writer, TXTWRAM off)
                until the master's FM drop at 144-227
    odd vint    raise 65-105  irq4 73-113 = MISS   pass ends (idle) 217-256

    head (shim + IRQ4)   ~105     spin ~110     work ~206     total ~420

Base, light (3000-3011): the same shape, spin 105-119, work ~150,
total ~360. Both regimes cost two vints on base because the spin alone
is 110 lines.

Opt1, heavy (2000-2011), still two vints per frame, different anatomy:

    even vint   raise 66-83   irq4 74-91   pass starts 100-109
                NO spin (fm reads 0/8); first FB writer reached only in
                the next vblank ("drop 1")
    odd vint    irq4 81-91 = MISS at entry; then gate #13 (0xffbdd2)
                at 104-122, spin 14-116 lines to the drop; idle 136-245

    head ~105   work in the even vint 153 (all that fits before the
    next shim)   work remaining ~30-40   -> heavy pass ~185-190 lines

The odd vint's spin comes AFTER the miss is already counted at IRQ4
entry, so on opt1 the spin is not on the deciding path; the miss is
decided by head + heavy pass = ~105 + ~190 = ~295 > 262.

Opt1, light (3000-3011): NO misses, pass starts 92-131, idle at
92-131 of the next vint with 300-1170 idle reads: light work ~150 fits
in the 153-line slot with a few lines to spare, which is the 98.9.

**Attribution.** The vint has three terms before the game's pass can
start: our shim (0 to the raise, 66-98 lines), the game's IRQ4 (the
upload, ~25-30 lines incl. our thunks), and ~2 lines to the pass. The
pass then has 262 minus that head. Light passes (~150) fit, heavy
passes (~190) do not, by ~35-40 lines. THE PRIZE IS THE HEAD, AND THE
SHIM IS TWO THIRDS OF IT.

    shim (this measurement)     66-98 lines, typical ~72
    needed to fit heavy frames  shim <= ~35
    PALROTOROFF ceiling 94.7    consistent: rotor+compares are 21-24
                                of the shim (frame-threshold note)

B1 is therefore the stage trace of the shim on opt1: where do the 72
lines go today, on the FB transport. B2 (the FM span) is NOT a lever
on opt1: no missing vint on this line spins before its miss. B0
(FMLATE) is about the same span and is expected dead; it is cheap,
run it once for the record.

Also seen: base 3008 has no raise at all and IRQ4 at line 27 — a vint
where our shim did not post (the previous vint's pass ran through the
vblank). One in 24 on base; none on opt1.

## 110. A4: THE ROTOR CEILING IS 94.5% BY MISSES, AND THE SCENE TIMER CAN STALL (01:51-01:53)

    make clean; make ship-us <opt1> PALROTOROFF=1     md5 c0c78277
    .build_flags carries -DPALROTOR_OFF                _end ok

                        timer%   IRQ4 misses   by-miss%
    total [1500,4100]    87.3       5.5%         94.5
    1500-2200            83.0    119/700         83.0
    2200-2900            96.6     24/700         96.6
    2900-3600           100.0      0/700        100.0
    3600-4100            62.8      0/500        100.0   <- disagree
    anim mean 10.7; black 8.0; colours ~3500; ramp[3] 731

Raw counters (WRAM 0xFFF02A timer / 0xFFF144 misses) at each point:

    opt1          925/112  1335/402  1948/489  2640/497  3140/497
    opt1_rotoroff 981/59   1562/178  2238/202  2938/202  3252/202
    base          807/176  1155/528  1516/867  1866/1217 2100/1483

Two things.

1. **The ceiling reproduces by the miss counter: 94.5 (LOOP28 86: 94.7).**
   The prize over opt1 is 14.8 - 5.5 = 9.3 points of misses, all of
   it in the two heavy windows (41% -> 17% and 12% -> 3%). The rotor
   and its compares are ~20 lines of the ~72-line shim, and cutting
   them halves the heavy-window misses. Consistent with entry 109's
   arithmetic (head + heavy pass ~295, over by ~35).

2. **The scene timer is not a clock in every game state.** Window
   3600-4100 on the rotor-off rom advanced the timer 314 in 500 vints
   with ZERO misses. Frame f4000 (read): frozen palette, confetti
   ground, HUD gone, black box where the player was — the run diverged
   (frame-indexed inputs on different content) into a state where the
   game does not tick 0xFFF02A. So `speed_total` from the timer can
   read LOW on a build that misses nothing. START-HERE defines speed
   as 100 minus the miss rate; the rig now records both per window
   (`by-miss windows` column) and the raw counters. When they
   disagree, the content diverged and the miss figure is the one the
   bar is defined on.

3. **Opt1 has a visible defect on this script.** opt1 f4000 (read): a
   solid RED RECTANGLE where base f4000 draws the textured tomb; the
   shadow-ramp fallback (SPRLATE[3] = 1099 on opt1). This is the
   failure Mike's pass called on 2026-09-07. Every B candidate sits on
   opt1 and inherits it; it is an SH-2 pair-claim item, not a speed
   one, and it goes in the handoff as the blocker between opt1 and an
   accepted rom.

B1 next: the stage trace of the ~72-line shim on opt1.

## 111. B1: THE SHIM'S 73 LINES, SPLIT — ROTOR 21.5, SHIP 16.5 (01:57)

MEASURED 2026-09-09 01:57, WRITTEN 15:55 from the run's own output; the
loop stopped before this got written (entry 112). The numbers are the
trace's, not a reconstruction.

No probe build was needed. The shipping shim already writes V-counter
stamps at its own stage boundaries (`PSTAMP`, md_main.c:1344), so a
plain `--trace-access` over the stamp words reads the SHIP BUILD's own
timing with nothing added. `tools/stage_lines.py` decodes it:

    ~/src/ares-debug/.../ares-headless --frames 2013 \
      --input discover/inputs/play_level1.csv --trace-access-out t.csv \
      --trace-access 0xFFA09E:0xFFA0CC:push:2000:2012 \
      --trace-access 0xFFB0B0:0xFFB0B8:cons:2000:2012 \
      --trace-access 0xFFA170:0xFFA186:fine:2000:2012 \
      --trace-access 0xA15100:0xA15101:fm:2000:2012 \
      --trace-access 0xA15120:0xA15121:comm0:2000:2012 \
      --trace-access 0x902AAC:0x902AAD:irq4:2000:2012 \
      --trace-access 0xFFF144:0xFFF145:miss:2000:2012  rom/night/opt1.32x
    python3 tools/stage_lines.py t.csv

opt1, 12 vints (2000-2011), median line from vblank start:

    boundary                     line    stage cost
    consume entry                  14
    consume END                  14.5    (MD-plane packets ~0.5)
    r60_push entry                 21    gates+announce 6.5
    rotor+compares DONE          42.5    ** ROTOR + COMPARES 21.5 **
    pal_next / record scan         45    sprite record scan 2.5
    rowscroll compare done       49.5    rowscroll compare 4.5
    selection done                 52    belt + bookkeeping 2.5
    regs staged                  54.5    2.5
    pal staged                     56    1.5
    records+tail staged          72.5    ** SHIP PHASE 16.5 **
    FM RAISE                     72.5
    POST comm0                     73
    game IRQ4 entry              80.5    IRQ4 handoff 7.5
    game MISS written              85    (6 of 12 vints)

**The shim is 73 lines and two blocks are 38 of them (52%).**

  1. **The palette rotor and its compares: 21.5 lines**, entry 21 ->
     42.5. This is the block `PALROTOROFF` deletes, and A4 measured
     deleting it as worth 9.3 points (entry 110). The two measurements
     are independent and they agree, which is the first time this
     lever has been priced in lines and in points at once.
  2. **The ship phase: 16.5 lines**, pal staged (56) -> records+tail
     staged (72.5). This is the packet's words being written into FB
     staging. It has never been separately priced and nothing in the
     dead-end list covers it.

Everything else is small: no stage between them exceeds 6.5 lines, so
there is no third lever here, and a diet that trims the small stages
cannot reach the ~35-line target entry 109 set.

**CAVEAT — A STAMP-ALIASING TRAP, and it would have bitten the next
reader.** `PAL_STAMP2` is NOT in the ship flag set, so 0xFFA0C0-0xFFA0CA
are not stage stamps in this build. But 0xFFA0C0 IS written every vint
by the LOST-PUSH BELT (`lp = (volatile uint8_t*)0xFFA0C0`,
md_main.c:1846), so the raw table shows a plausible-looking
"push.rotor0@51.5" that is really the belt writing its id list. The
decoder now labels that address `belt.ids` instead. Any future stamp
read over 0xFFA0Cx must check `PAL_STAMP2` before believing a name.
This is the same class as the DIAG-slot aliasing in HANDOFF-PIPELINE 5.

Next, and B1's real deliverable: the ship phase's 16.5 lines are 68K
word writes into FB staging, which is WRAM, not the adapter. That is
~150 words at ~0.11 lines/word. Before designing anything, price what
the packet actually carries on these vints (LOOP27 80 said ~150 words
conveying ~85 words of real change) — a packet diet is only worth
building if the redundant half is still there on the FB transport.

## 112. THE LOOP DIED AT 01:58 AFTER 28 MINUTES, AND WHY

Mike asked for an overnight loop and got 28 minutes of one. Recorded
here because the failure is mechanical and cheap to prevent.

    01:30  protocol + rig written
    01:36  A1 base       49.7%
    01:41  A2 opt1       85.2%
    01:46  A3 timelines
    01:51  A4 rotor-off  94.5% by misses
    01:57  B1 stage trace RAN and printed its table
    01:58  last commit. Nothing after.
    15:52  Mike back. 14 hours idle.

**The mechanism.** The loop continues only because each tick ends with
a `ScheduleWakeup` call. Ticks A1-A4 were one measured build each:
run the rig, read a frame, write the entry, schedule. B1 was different
— it was an EXPLORATORY tick (read the shim's source to find out where
to put a probe), and it made four large source-dump reads in one turn.
The turn ended in that reading, before the entry and before the
schedule call, and with no wakeup pending the loop simply stopped.

**Two defects, not one.**

  1. **The wakeup was the LAST step of a tick.** So any tick that runs
     long, errors, or ends unexpectedly kills the whole loop. It is
     the one step whose failure is unrecoverable, and it was scheduled
     last.
  2. **An exploratory tick has no natural size.** A1-A4 were bounded by
     a tool that runs in 150 seconds. "Read the shim and design a
     probe" is bounded by nothing.

**The fix, both cheap.** Schedule the next wakeup FIRST, at the top of
the tick, before doing any work — then the loop survives a tick that
dies for any reason, and the worst case is one repeated tick instead
of a dead night. And split exploratory work into its own tick class
with an explicit output cap. `docs/handoff/LOOP-NIGHT-0909.md` now
says so.

**What survived.** All four ground-truth items and B1's measurement,
because each tick wrote its entry before scheduling the next. Nothing
measured was lost except B1's write-up, recovered above at 15:55 from
the run's own output.

**What did not survive.** `rom/s16.32x` was left holding the
`PALROTOROFF` probe (colours freeze after vint 900) for 14 hours,
because WRAP-UP never ran. Restored 15:53 to `make ship-us FBXPORT=1`,
build de975574, `_end 0x060135D0`. If a rom was played from that path
this morning, that is what it was.

## 113. THE REGRESSION IS FBXPORT, AND EVERY METRIC WE USED HID IT (16:30)

Mike, at the coffee shop: "since you agreed on an arch to move in, I
haven't gotten a single build from you that has moved frames... this
build further has zero moving frames." He is right, it is measurable,
and the accepted rom is the worst one in the ladder.

New instrument: `tools/presented_fps.py`. Over 60 consecutive ares
frames it counts frames differing from their predecessor by >5% of the
active area (MOTION: a scroll step, a sprite moving) and, separately,
by >0.25% (any-change). MOTION is the headline. The any-change count is
the same trap `anim_rate.py` fell into — opt1 reads 12 "new pictures" at
f1800 of which 2 are substantial, and calling that 21 fps was the wrong
story to tell about a picture that updates twice a second.

Same script, same windows, level-1:

    rom                                  MOTION   any-change   windows
    make ship-us          (Sep 8)          5.0       22.0      7,5,3
    make ship-us FBXPORT=1 (ACCEPTED)      1.3        4.0      0,4,0
    opt1 + FBXPORT                         6.3       21.0      2,3,14
    opt1 without FBXPORT                   8.3       16.3      12,4,9

**The accepted rom froze completely for a full second.** Window f3400:
ZERO pixels changed across 60 consecutive frames, not even the HUD. Over
that same stretch its scene timer advanced 350 ticks in 700 vints — so
the LOGIC ran at ~50% while the PICTURE stood still. Both statements
are true of the same rom at the same instant, which is the whole problem.

**Mechanism, already in this log and never acted on.** LOOP28 92: under
FBXPORT the framebuffer flips 9 times in 2988 vints. The master composes
into the bank being displayed, so a new picture appears only when a whole
compose finishes, and a long compose shows as a still frame. FBXPORT
exists for exactly one reason — without it the port is a black screen on
the MiSTer (HANDOFF-PIPELINE 2, screenshot-verified). It buys hardware
boot and it costs 22 -> 4 picture updates a second.

**Why it stayed invisible for eight days.** Every ranking instrument
adopted since 2026-09-01 measures the 68K's logic:

  - `gameplay_speed.py` = scene-timer ticks per vint. Reads 49.7% on a
    rom that freezes for a second.
  - `START-HERE.md`, written 2026-09-09 01:23, states "THE BAR. There is
    only one. 60 frames per second... The metric is gameplay_speed.py"
    and explicitly rules out "flip rate... screen updates per second."
    That instruction is what the overnight loop followed for four hours.
  - LOOP28 93 had ALREADY measured this divergence (logic 49.7 /
    updates 11.7 vs logic 29.6 / updates 25.3) and recorded Mike's play
    pass overruling the gate. The finding was written down and then the
    next document made the gate the only bar anyway.

The Sept 1 era ranked builds on `ships` — generations actually delivered
to screen, printed as an fps by `tools/nat_score.py` — and on "% single-
vint frames". Those are presentation metrics. The bar drifted from what
reaches the player to what the 68K computes, and the port followed it.

**Not done here:** the Sept 1 rom itself was not rebuilt for comparison.
Commit 9dfc8ee does not compile (the auto-commit hook means most commits
are mid-edit snapshots, so history is not a source of buildable
references) and Mike's instruction was reference-only, no importing. The
regression is therefore bisected on FLAGS against today's source, which
is the more useful axis anyway.

**Open, and bigger than FBXPORT:** the best rom in the ladder still only
moves 8.3 times a second. FBXPORT explains 5.0 -> 1.3; it does not
explain 30 -> 5. The next suspects are unconditional (not flag-gated) and
therefore invisible to a flag bisect — chiefly the display gate added
2026-09-05 (md_main.c:4433, mirrors the arcade's video-enable bit into MD
VDP reg 1 and can hold the MD plane OFF) and the Sept 6 attract-parity
work (blank mode, the pipeline-armed handshake, display release waiting
two cell-walk rotations). Those need a probe flag each before they can be
ranked.

## 114. THE GAME ALREADY RUNS OUR PALETTE ALLOCATOR, AND ITS TABLE IS IN OUR WORK RAM (17:40)

**ITS DIAGNOSIS SECTION IS WRONG — CORRECTED BY 115. The addresses, the
verification and the namespace correspondence all stand; the claim that
the missing slot 6 is the shadow-ramp defect does not.**

Mike supplied `srcref/alteredbeast` (Michael J Archer's commented S16
disassembly). Critical read first: it is a DISASSEMBLY, not a
decompilation — 61,014 lines, 44% raw data, 4,132 labels, no reassembly
to a matching rom, and its comments are ChatGPT/DeepSeek-assisted with
the author stating "not 100% perfect" and "no 68000 expert". So nothing
below is believed on its say-so; every claim is re-derived from our own
rom. Handling is jtcores' rule: srcref is gitignored, DERIVE NEVER COPY.

**It is our program.** Three facts our own probes found independently
appear at the same addresses: `sync_check` at 0x3982 is
`tst.b (unk_FFF01C).w / beq.s sync_check` (our frame-flag idle loop, pc
0x903982); 0x2D82 is the one pure delay loop; 0x2AAC is a routine
boundary where we trace IRQ4.

**THE FINDING. Altered Beast contains a reference-counted palette slot
allocator, and we have been running a second one that guesses at it.**

Verified by disassembling OUR `roms/altbeast/prog68k.bin`, not by
reading the .asm — every instruction below is from our own objdump:

    0x3B2E  RequestPaletteUpdate    115 call sites
    0x3BCE  ReleasePaletteSlot       90 call sites  ("decrements
                                     reference count, frees if unused")
    0x3B6C  AllocatePaletteSlot

    3b2e: lea 0xfffff500,%a0     palette_request_table
    3b34: moveb %fp@(11),%d0     palette_index  = object offset $0B
    3b3e: cmpiw #63,%d1          63 = "unassigned"
    3b44: moveb 0xfffff400,%d1   queue head
    3b54: moveb 0xfffff401,%d1   fallback slot
    3b60: clrb  %fp@(10)         palette_bank   = object offset $0A
    3b76: lea 0xfffff480,%a1     slot table, indexed slot*2
    3bbc: lea 0xfffff440,%a0     slot reference counts

The object-structure offsets the .asm documents ($0A palette_bank,
$0B palette_index) are confirmed by the `%fp@(10)` / `%fp@(11)`
addressing in our own binary. That is the cross-check that makes the
rest of its struct table usable.

**The tables are LIVE IN OUR PORT.** ares dump of the shipping build at
f2000, 253 of 384 bytes nonzero, stable across 6 frames:

    0xFFF480 slot table   0032 0003 0008 003A 0049 0058 004B 002F
                          0001 0028 0029 002A 0007 0000 0057 FFFF
    0xFFF440 refcounts       7 0 1 0 0 0 1 1 1 3 3 3 0 0 0 0
    0xFFF400 queue head   0x0F      0xFFF401 fallback  0x03

**AND IT DIAGNOSES THE SHADOW-RAMP DEFECT.** Our SH-2 keeps `pr_key[16]`
(0x060283C0) + `pr_age[16]` with age eviction, and our key is
`d[4] & 0x3F` (m_main.c:2194) — the 6-bit palette field off the sprite
record, which IS the game's slot index. Both allocators read at the same
frame:

    game slots with refcount > 0    0, 2, 6, 7, 8, 9, 10, 11   (8)
    our pairs in use                0, 2,    7, 8, 9, 10, 11   (7)

We get 7 of 8. The game holds a live reference on slot 6 and our LRU
never claimed it; an actor on slot 6 draws with base 15, the shadow ramp
— SPRLATE[3], 1099 draws on opt1, the red rectangle at f4000.

**The change.** Ship the 16 refcount bytes (or the 32-byte slot table)
each vint and assign pairs from the game's own accounting. That deletes
`pr_age`, the age eviction, the late claim, the capacity census and the
ramp fallback, and it cannot disagree with the arcade because it IS the
arcade's allocation. It is also the first concrete instance of LOOP27
80's architecture: ship meaning the game emits, not deltas we discover.

**Not yet verified, and it gates the build:** that our key equals the
game's slot index EXACTLY rather than 7-of-8-by-coincidence. One frame
is not a correspondence proof. Next step is a multi-frame census of
`d[4]&0x3F` against the refcount table before any code is written.

## 115. CORRECTION: THE REFCOUNT TABLE IS NOT A "NEEDS A PAIR" SIGNAL (18:05)

Entry 114 ended by claiming the game's refcount table diagnoses our
shadow-ramp draws, on the evidence that slot 6 is refcounted live in
10 of 10 frames and our `pr_key` never holds it. I was about to build on
that. The verification it demanded first says it is wrong.

Dumped the game's sprite RAM (0xFF7000, the mirror our own push reads,
md_main.c:1332) alongside the refcount table at f2000:

    live sprite records                      16
    palette field (d[4]&0x3F) -> records     0:7  2:1  7:1  9:3  10:2  11:2
    game refcount > 0                        0 2 6 7 8 9 10 11
    our pr_key                               0 2   7 8 9 10 11

    slot 6: game refcount 1, ON-SCREEN RECORDS USING PALETTE 6 = ZERO

**So not claiming slot 6 is CORRECT.** The refcount counts OBJECT
references in the game's actor list — an actor that exists but is
off-screen, inactive, or not drawing still holds one. It is not a
"this palette is on screen" signal, and our allocator is right to ignore
it. At this frame there is no allocator failure at all: six palettes are
on screen, we hold all six, plus slot 8 which `PRHOLD=6` deliberately
keeps for a departed set.

**What survives from 114, and it is still worth having:**

  - The routine addresses and table addresses, all derived from our own
    objdump (0x3B2E / 0x3BCE / 0x3B6C, tables 0xFFF440 / 0xFFF480 /
    0xFFF500).
  - The object offsets $0A palette_bank / $0B palette_index, confirmed
    by `%fp@(10)` / `%fp@(11)` in our binary.
  - **The namespace correspondence, which is proven and useful**: our
    `d[4] & 0x3F` IS the game's palette slot index. 81 game-live slots
    over 10 frames and we hold exactly one the game calls dead. That
    means the game's per-slot data (which palette DATA a slot holds, via
    the 0xFFF500 request table and palette_bank 0-175) can be read
    directly against our own keys with no translation layer.

**What it does NOT give**: a cheaper or more correct answer to "which
sets need a pair THIS cycle". We already compute that from the sprite
records, which is the right source, and at the frames sampled we compute
it correctly.

**Where the defect actually lives, restated honestly.** SPRLATE[3] is
1099 ramp draws on opt1 over 2600 vints, so the failure is real but it
is NOT happening at the frames sampled here. It is a CAPACITY failure
under load — more live sets than pairs — which the capacity census
(m_main.c "CAPACITY CENSUS") was built to measure and which LOOP27 6
already characterised. Finding it needs sampling at a ramp draw, not at
an arbitrary frame.

**Method note for the next session.** The disassembly is a strong source
of ADDRESSES and STRUCTURE and a weak source of SEMANTICS. Both of
today's uses of it followed the same shape: the structural claim
verified perfectly against our binary, and the behavioural inference
drawn from it was wrong until measured against a running frame. Verify
structure by objdump, verify meaning by ares dump, and never skip the
second.

## 116. THE DOUBLE-BUFFER DEAD END WAS CALLED ON THE WRONG METRIC (2026-09-10 03:55, MiSTer back)

START-HERE lists double buffering (`FBXSTAGE`/`FBXBOTH`/`FLIPEDGEOFF`)
as a dead end: "the single-buffered line does NOT tear, so it solves
nothing, and it costs 15 points of game speed." Fifteen points of GAME
SPEED — the logic metric that entry 113 showed is not the bar. Re-ranked
on motion (`tools/presented_fps.py`, level-1 script):

    rom                                MOTION   any-change
    ship-us FBXPORT=1  (ACCEPTED)        1.3        4.0
    ship-us (no FBXPORT, ares only)      6.3       21.7
    T_dblbuf                             6.3       25.0
    U_dblfast                           16.7       35.7

**U_dblfast moves 13x the accepted rom and keeps FBXPORT**, so unlike the
no-FBXPORT builds it is alive on hardware. LOOP28 93 already had Mike's
hardware verdict — the double-buffered rom "shows MORE animation" — and
it was overruled by the speed gate. That was the wrong call and this is
the second time the same instrument made it.

**Hardware, this session.** Rig up, MiSTer main running (unlike the
2026-09-08 night session), screenshot path verified fresh. Deployed and
captured six frames: the attract sequence renders CORRECTLY — logo,
player, enemies, graveyard, grass, HUD, "©SEGA 1988" — and the logo
cycles white -> red -> white across captures, which is the arcade's own
`logo rewrite`/`logo red` scene pair. It is not confetti and it is not
black.

**Known defect, ares, NOT seen in the hardware attract captures:** at
level-1 f2600/f2620 U_dblfast shows intermittent WHITE BLOCKS in the
lower-middle band, 2 of 3 frames, different positions each time — the
uncomposed-rows signature of LOOP28 90's bank disease. Needs a look at
level-1 gameplay on hardware, not just attract.

**Instrument limit found:** MiSTer screenshots throttle to ~4 Hz (12
requests took 3032 ms), so captures CANNOT measure hardware frame rate —
at 1.5 s sampling every pair differs on every build. Hardware motion
needs an in-rom counter painted into the 32X layer (two shots, subtract,
divide by wall time), or Mike's eye. Do not try to infer fps from
screenshot diffs.

**Caveat on the rom itself:** `U_dblfast.32x` is stamped 8f0b0526 from
2026-09-09 00:26 and carries `TEXTCAPMASTER TEXTCAPFULL` on top of the
double-buffer flags. It is last night's binary, not today's source. If
the play pass likes it, rebuild it clean before anything is decided on
it.

## 117. MIKE'S HARDWARE PASS: AN ORDER OF MAGNITUDE, STILL NOT PLAYABLE — AND THE NEXT LIMIT IS THE MASTER'S COMPOSE (04:00)

Play pass on the double-buffered line, MiSTer, his words: "OOOH not
perfect but a significant improvement", then "again we dont have enough
frames to be playable. but we do have a magnitude of improvement".

That is exactly the measurement: accepted 1.3 -> dblfast 16.7 MOTION fps,
13x, and 16.7 is not 60.

**The flag set rebuilds clean and reproduces**, so it is today's source
and not last night's lucky binary:

    make ship-us FBXPORT=1 FBXSTAGE=1 FBXBOTH=1 FLIPEDGEOFF=1 \
                 TEXTCAPMASTER=1 TEXTCAPFULL=1
    build 8bc6f189   _end 0x060135D0   MOTION 16.7  windows [4,40,6]
    (U_dblfast, 8f0b0526, read 16.7 windows [4,40,6] — identical)

Note `TEXTCAPMASTER` is an INVERTED flag (`ifndef TEXTCAPMASTER` gates
the slave path), so a correct build shows `-DTEXTCAP_FULL` and NO master
define. Do not read its absence from `.build_flags` as the flag missing.

**PHASECENSUS on that build, 2600 frames, and it is unambiguous:**

    ships 803 = 19.2 fps          generation wall  2.74 vints
      mtask  2.73   <- the master's per-gen work, launch -> tail done
      echo   1.61      ship 0.58     flip 0.69     lag 0.01
    ship period bins (1/2/3/4+):  28 / 230 / 193 / 352
    mtask latency bins:  60 13 0 5 2 1 1 722   (722 of 804 in the top bin)

`mtask` 2.73 IS the wall 2.74. Echo, ship and flip overlap it and are off
the critical path — this is why every 68K-side lever this project has
chased (the shim diet, the rotor, the FM span, the packet transport) has
moved the logic rate and not the picture. **The 68K is not the frame-rate
constraint and has not been for some time. The master's compose is.**

Only 3% of generations fit in one vint (28 of 803); 44% take four or
more. 60 / 2.74 = 21.9, which is the ~20 Hz on screen.

**The target is now a single number: mtask 2.73 vints -> ~1.0.**

Levers, in the order the evidence supports, none measured yet:

  1. **Draw less on the SH-2.** Every pixel the MD VDP draws is a pixel
     the master does not compose. This is the NATIVE pivot's own thesis
     and it is only partly done (MD_BG, MDSPR, TILECLASS shipped;
     CAT1MD reverted on a play pass for shimmer, LOOP28/cat1md note).
  2. **Split with the slave.** `mtask` is the MASTER's tail. If the slave
     is idle during it, that is a 2x sitting there. `SHIPBLITSHIFT` (24
     today) and `BANDSHIFT` are the existing dials. MEASURE THE SLAVE'S
     OCCUPANCY FIRST — this lever is worth nothing if it is already busy.
  3. The 3-band structure itself: 44% of generations spanning 4+ vints
     suggests the bands are not the right unit under load.

**Instrument note:** `tools/nat_score.py` on a PHASECENSUS build is the
right tool for this question and it already existed. It uses play2.csv,
not the level-1 script, so its ships/fps is not directly comparable to
`presented_fps.py` — use it for the PHASE SPLIT, and presented_fps for
the rate.

## 118. NEGATIVE: THE SLAVE HAS NO SPARE CAPACITY (04:10)

Entry 117 listed "split with the slave" as lever 2 and said to measure
the slave's occupancy before building it. Measured; the lever is dead.

The SLAVE IDLE METER at 0x26028FA8 (s_main.c:384) counts no-command poll
visits, spilling every 64. It is UNCONDITIONAL code — not behind a flag,
so this is not another PALSTREAK-style inert counter (LOOP28 85). Read at
three frames on the play2 script:

    build            f2000   f2600   f3200    idle polls/vint
    base (accepted)   8128    8128    8128         0.0
    dblfast_clean     6528    6528    6528         0.0

Both frozen at a boot-era value; neither moves during gameplay. **The
slave never takes its idle branch, on either build.** Its spare capacity
is zero and handing it compose rows buys nothing.

Corroborated independently: LOOP28 96 measured the master waiting 31.8
lines for the slave to NOTICE a mailbox for a job that then takes 4.3 —
the profile of a processor that is busy, not one that is waiting. The
code comments claiming "the slave finishes early and idles ~15,500
polls/cycle" (m_main.c:1331, Makefile:1243) and "slave 34% idle-polling"
(m_main.c:11964) are STALE and should not be trusted; they predate the
NATIVE pipeline.

**So of entry 117's three levers on mtask 2.73 -> 1.0, only two remain:**

  1. **Draw less on the SH-2** — every pixel the MD VDP takes is a pixel
     the master does not compose. Partly done (MD_BG, MDSPR, TILECLASS);
     CAT1MD was reverted on a play pass for shimmer, not for being wrong
     in principle. This is now the PRIMARY lever.
  3. **Restructure the bands** — 44% of generations span 4+ vints, which
     says the 3-band unit is wrong under load.

Both are SH-2-side. Nothing on the 68K side can move the frame rate from
here, which is the single most useful thing this session established.

## 119. THE MD SPRITE OFFLOAD IS STARVED, AND THE CHAIN IS NOW FULLY CHARACTERISED

Mike: "enough time measuring", and: why are frames capped at 20? Answer
to the second: they are not — the cap is 20 RECORDS per frame and we were
running at 0.83. The MD VDP will draw 80 sprites a frame for free while
the master burns 2.73 vints compositing in software, and we were handing
it 1.3% of its capacity (LOOP29 117 makes that the whole frame-rate gap).

**Instrument.** `make ... MDSPR_WHY=1` counts every rejection in
`mdspr_claim`. Counters live in `.bss` on purpose — the 0x28Fxx scratch
is crowded and this repo has numbered its slot collisions to #15 — read
via the `_mdspr_why` symbol in `rom/s16.lst`.

**Two probe bugs, both of which produced confident wrong answers:**

  1. Counters inserted between `if (cond)` and `continue;` WITHOUT BRACES
     made five `continue`s UNCONDITIONAL, disabling the offload entirely.
     The census then read 0 claims and 0 rejections and looked like a
     finding. Caught only by checking that the reasons summed to the
     records examined. **Any probe that edits control flow must reconcile
     its own totals before a single number is believed.**
  2. `static uint32_t mdspr_why[10]` is never READ by the program, so
     -O2 -flto dead-store-eliminated 8 of the 10 counters. Must be
     `volatile`. Same class as the inert `pal_streak` of LOOP28 85.

**The census, level-1, 597 generations, 2723 live records:**

    reason              before bake   after bake
    NO BAKED KEY           75.2%        33.5%
    palette mismatch        0.0%        41.5%   <- now binding
    CLAIMED                18.1%        21.8%
    zoomed                  3.8%         2.0%
    pp!=2 / degenerate      2.9%         1.1%
    caps hit                0.0%         0.0%   <- never the constraint

**Fix 1, done: the bake was covering one colour set.**
`tools/bake_mdspr.py` SCENES had `('normal','play.csv',(0x09,),40,0x09)`
— set 0x09 only, while the scene used 6944B of its 12288B VRAM window.
(The module docstring claiming sets 0x00/0x10/0x0B is stale.) Widened to
(0x09, 0x0A, 0x0B): keys 8 -> 16, art 6944B -> 9120B, still under cap,
nothing dropped. MDSPR_NKEYS 21 -> 29.

**Result: claims 0.83 -> 0.99 per generation. The rejections MOVED rather
than cleared** — two gates in series, and gate 2 is now the wall.

**Fix 2, NOT done — it is an architecture decision, not a code change.**
The claim rule needs a record's colour set to be palette-coherent with
the MD CRAM line its SAT entry points at. Measured at f2400, pens 1..14:

    0x09 vs 0x0A   0/14 match
    0x09 vs 0x0B   0/14
    0x0A vs 0x0B   0/14      (so they cannot share one line either)
    and NO set in 0..0x3F matches the 0x09 anchor on all 14 pens,
    so there is no free claimant anywhere: 0 of the 2038 missing.

Sets 0x0A and 0x0B are 67.6% of the missing art (689 records each per 600
vints — an exactly-paired actor) and each needs its OWN MD palette line.

**And all four MD lines are allocated** (m_main.c:288): line 0 is the text
grey ramp, which the MDSPR anchor also rides; lines 1-3 are the background
colour pack, 21 S16 sets merged into 45 pens with ~9 pens of slack. Taking
a line for sprites costs background fidelity, and "accuracy before speed"
is a standing rule, so this is Mike's call and not mine.

**The third option, and it is the one the evidence favours.** m_main.c:627
already reasons about mid-frame CRAM swaps and rejects them: "per-SCANLINE
swapping costs 224 interrupts on a 68K that is already the bottleneck."
**That objection is dead** — LOOP29 117 measured the 68K off the critical
path (mtask 2.73 of a 2.74-vint wall). The same comment notes sprites
CLUSTER VERTICALLY and a span census (`sl_span`, 8 x 28-line spans) was
built to price "how FEW swaps buy the demand". A handful of H-interrupts,
not 224, could give sprites their own line only in the bands where they
appear. That census has never been read out.

## 120. THE SPAN CENSUS IS DEAD CODE, AND THE STREAMING PIPELINE IS THE RIGHT SHAPE

**The span census cannot be read, for two independent reasons.**

  1. It reported into `SLC[16..21]`. `SLC` is 0x2603A780, so SLC[16] is
     0x3A7C0 — which is `SPRPEN`, and SLC[22..31] is `SPRLATE`'s lean
     block. Collision #16. Read raw it claimed 403 distinct colour sets
     out of a possible 64.
  2. Relocated to `.bss` (collision-proof, volatile) it counted **1 frame
     in 2600**. Its call site is on a pipeline path `R60`/`NATIVE`
     retired. The instrument has been unreachable for a long time.

So "how few CRAM swaps buy the demand" is still unanswered, and the
answer would cost a re-siting. Parked, not concluded.

## MIKE'S ARCHITECTURE CALL, AND THE NUMBERS THAT SUPPORT IT

His words: the bottleneck was never the sheer amount of sprites and
tiles — that was a misreading of what running the real compiled 68K code
costs. "Our obvious path is to make them at their original size and
construct a pipeline that FEEDS them to the VDP as fast as the 68k
assigns the addresses."

**Priced, by baking every native key with the VRAM cap lifted:**

    all level-1 native keys, MD tile format   230 keys   222,880 B
    boss scene                                 14 keys    16,448 B
    total                                     244 keys   239,328 B (234 KB)

    live records per generation                4.5   (measured, LOOP29 119)
    average art per key                        ~970 B
    ART ON SCREEN AT ONCE                      ~4.4 KB
    existing MDSPR VRAM window                 12 KB   (0x8000-0xB000)
    68K DMA budget per vblank                  ~7 KB

**A frame's sprite art fits in a third of a VRAM window we already have,
and the per-frame DELTA is far smaller** — actors hold an animation frame
for several vints, so only changed keys need filling. Demand-fill from
cart is comfortably inside one vblank.

That reframes the whole offload. The static 16-key bake was never the
right unit: it pre-picks a fixed set by census frequency and everything
else falls to SH-2 compose. A VRAM tile CACHE with 68K demand-fill from
pre-converted cart art serves whatever the game actually asks for, and
the SH-2 composes none of it.

**What the numbers say is still in the way, in order:**

  1. **PALETTE LINES, unchanged and still the hard one.** MD sprites get
     4 CRAM lines; level-1 needs at least 7 colour sets (0x09 0x0A 0x0B
     0x00 0x02 0x07 0x08) and no two of the top three share a single pen
     (LOOP29 119). Serving 0x09+0x0A+0x0B alone would cover 2008 of 2723
     records = **74% of all sprite records**, which is the prize.
  2. **Cart space.** 234 KB of pre-converted art against a cart
     HANDOFF-PIPELINE calls full at 4.00 MB. The SSF2 mapper path is
     documented in SILICON.md 4e but unbuilt.
  3. Runtime cache management (which key is resident, eviction) is new
     68K work — but the 68K has the headroom now (LOOP29 117).

**The bake's rect padding is real but secondary**: MD art averages 970 B
per key against the S16 source's 778 B (180,477 B / 232 keys), so the
bounding-rect padding costs ~25%, not a multiple. Worth fixing, not the
reason we compose in software.

## 121. THE FIRST SPRITE PALETTE LINE IS FREE: THE BG PACK CARRIES 10 DUPLICATE ENTRIES

**WRONG — SUPERSEDED BY 133. The 30-colour figure is a SINGLE-FRAME MD
CRAM read and undercounts the scene's demand; the pen bake's exhaustive
search says 36 colours over 180 samples, which does not fit 2 lines.
The line is NOT free.**

Mike asked what the background actually loses if it gives a palette line
to sprites. Measured off MD CRAM directly (ares `--dump "VDP CRAM"`,
64 entries x 4 lines), not from the pack's own bookkeeping.

    raw distinct CRAM words in lines 1-3   40
    distinct COLOURS after 9-bit decode    30   <- 10 are duplicates

Sampled across the whole level-1 run (f300 700 1100 1500 2000 2800 3400
4000): 29, 30, 30, 30, 30, 30, 30, 30. Never above 30.

**So the background fits in TWO lines (30 pens) with ZERO colour loss,
everywhere in level 1.** The third line is free for sprites today.

The second line is not free. Packing 30 colours into one line (15 pens),
greedy nearest-merge in the 3-bit-per-channel MD space:

    colours that must move   26 of 30
    mean shift               0.51 of 7 steps per channel
    worst shift              1.00 of 7

A one-step shift in eight is subtle but real and it is a play-pass
question, not an arithmetic one.

**The ladder, with the sprite side priced from LOOP29 119's census
(2723 live records per 597 generations):**

    move             BG cost                    sprite gain
    3 -> 2 lines     ZERO (verified 8 frames)   set 0x0A: 689 rec = 25.3%
    2 -> 1 line      26/30 shift, worst 1/7     set 0x0B: 689 rec = 25.3%

**Caveat, stated:** level-1 only. m_main.c:288 claims the worst visible BG
window needs 36 distinct MD-quantised colours, which would NOT fit two
lines (36 > 30). PALSTATIC is already per-scene, so the line allocation
can be per-scene too: three lines where a scene needs them, two where it
does not. The attract scenes have not been sampled.

**Why this was invisible:** the pack was sized against 45 pens and 21
S16 colour sets and never asked how many DISTINCT COLOURS survive the
9-bit quantisation. It is the same shape as LOOP29 119's caps — a budget
defended against the wrong quantity.

## 122. BGPACK2 IS BUILT AND IT BREAKS THE SKY — THE PACKER CANNOT REALISE THE FREE LINE

`make ... BGPACK2=1` (MDP_LINES 3 -> 2, default-off) built clean and MD
CRAM line 3 does go free (all zero, confirmed at f2000 and f3000). The
background does NOT survive it.

    3-line pack   lines 1-3 hold 30 distinct colours
    2-line pack   lines 1-2 hold 21 distinct colours   <- 9 LOST
    frame diff at f2000: 23.0% of pixels
    LOOKED AT IT: the SKY IS BLACK. Trees and the upper band drop to
    backdrop. Not shippable.

**Entry 121's arithmetic was right and its conclusion was wrong, in a
shape this project keeps repeating.** 30 distinct colours do fit in 30
pens. But the packer assigns a line PER COLOUR SET and never dedupes
across lines: with 3 lines it spent 40 CRAM entries on 30 distinct
colours (10 cross-line duplicates), and with 2 lines it simply runs out
and the unplaced sets render as backdrop. The headroom is real; the
allocator cannot reach it.

I measured the right quantity and then assumed an allocator that does
not exist. Same failure as LOOP29 114 (the game's refcount table was
real, the inference from it was not) and LOOP29 119 (the cap was real,
it was not the constraint). **Structure by measurement, behaviour by
measurement, and never infer the second from the first.**

**What would actually buy the line:** global colour dedupe in the pack.
Two sets whose quantised colours coincide should share pens on one line
instead of each taking their own. The measured duplicate count says that
is worth exactly the 10 entries needed. `NEARMERGE=1` already exists for
near-pen merging (Makefile 1623) and was never turned on for this — it
is the nearest existing machinery.

`BGPACK2` stays default-off and is kept: it is the falsifier for any
future dedupe work. If the packer ever dedupes properly, this flag
should render identically to the 3-line line.

## 123. THE MASTER IS NOT BUSY: ONLY 29% OF mtask IS WORK

Built `make ... MTASKWHY=1` to split the master's per-generation tail
before committing to the palette/streaming arc. It changed the diagnosis.

**First, a fact that reframes LOOP29 117.** The ship line sets
BANDSHIFT=36 / RG2SHIFT=40, which trips `NAT_ALL_SLAVE 1` (m_main.c:842):
**every master compose range is EMPTY.** The master composes no bands at
all; its per-generation tail is maps-ONLY (text moved whole to the slave).
So mtask was never sprite compose, and "the master's compose is the
critical path" (entry 117) was wrong in attribution.

**Measured, play2 script, 600 vints, 167-595 generations:**

    mtask span (PHASECENSUS PH[1])     3.32 vints/gen
    ticks actually inside the drain    0.95 vints/gen   = 28.7%
    build_maps_chunk calls             10.0 /gen, 1151 ticks each (25 lines)
    drain branch visits                 1.5 /gen
    gate skips past NAT_DRAIN_CUT      59.7 /gen  (poll iterations in the
                                       last 6% of the vint; NAT_DRAIN_CUT
                                       is 11300 of 12052, so the gate is
                                       WIDE and is not the constraint)

    all accounted master DIAG tasks    0.44 vints/gen

**So the master's SPAN is on the critical path and the master's WORK is
not.** It is waiting. Combined with LOOP29 118 (the slave never takes its
idle branch), the saturated processor is the SLAVE, and mtask is long
because the master's tail queues behind the slave's compose: `echo` has
the slave finishing at 1.61 vints and the master's tail landing 1.7 vints
after that.

**UNRESOLVED DISCREPANCY — do not build on either number yet.** My
`mt_drain_ticks` says the drain costs 0.95 vints/gen; the shipping
`DIAG[11]` says 0.27. `diag_add(11, ...)` is called from three different
sites (m_main.c:8970, 9109, 9179) so the slot is shared across code paths
and I trust my own placement more — but 3.5x is too big to wave through,
and this project has lost days to exactly that. Reconcile before using it.

**What it implies if it holds:** the sprite offload is still the right
direction but for a different reason than entry 119 assumed — it would
unload the SLAVE, which is saturated, not the master, which is idle. The
palette-line work remains the gate on that.

## 124. THE SLAVE SPLIT, AND ENTRY 123'S DISCREPANCY WAS MY OWN DENOMINATOR

**First, the flagged discrepancy is RESOLVED and both instruments were
right.** Entry 123 could not reconcile my `mt_drain_ticks` (0.95 vint/gen)
with the shipping `DIAG[11]` (0.27). Two different denominators:

    DIAG[9]      595 CYCLES     over 600 vints   (~1 per vint)
    NAT_WALL[1]  167 GENERATIONS                 (~1 per 3.6 vints)
    595 / 167  = 3.56   <- exactly the "3.5x" gap

    DIAG[11] x 595 x 12052 = 1,935,865 ticks
    mt_drain_ticks         = 1,921,403 ticks     agree to 0.7%

START-HERE rule 4 is "divide before you claim" and I broke it. Nothing was
wrong with either counter. **A GENERATION IS NOT A VINT: 167 generations
in 600 vints, one per 3.6.** Every per-gen figure in this log should be
read against that.

**The slave pass split** (st_s/STB, already in the tree under
PHASE_CENSUS; slave FRT is phi/8 = 48208/vint, 4x the master's):

    SLAVE  clear + sprites      0.73 vints/gen
    SLAVE  cat1 tiles           0.67 vints/gen
    SLAVE  compose total        1.40 vints/gen
    MASTER maps drain           0.95 vints/gen
    generation wall             ~2.74-3.32 vints

That is a coherent picture at last: slave compose 1.40 + master drain 0.95
= 2.35 of a ~3-vint wall, the two partly overlapping. Nothing is missing
and nothing is mysterious.

**THE LEVER: cat1 is 48% of the slave's compose work.** The slave is the
saturated processor (LOOP29 118) and category-1 foreground tiles are half
of what it does. `CAT1MD=1` already exists to draw those on MD plane A
instead — it passed the still-image gate on 2026-09-07 and FAILED Mike's
play pass for shimmer and a second palette on the ground band through the
transform (see the cat1md note). It was reverted for a RENDERING defect,
not because the idea was wrong, and it is worth 0.67 vints/gen on the one
processor that has no slack.

**On Mike's notion** (hardcoded map, larger baked sprites): it addresses
the OTHER half. Slave compose splits ~50/50 between sprites (0.73) and
cat1 tiles (0.67), so bigger pre-baked sprite units attack the sprite half
and CAT1MD attacks the tile half. They are complementary, not competing —
but both land on the slave, which is the right target.

## 125. THE INDEXED-TILE BAKE: SHARING DOES NOT PAY, PADDING DOES

Mike's notion: bake SH-2-sized tile pages and have the 68K feed tile
quadrants to the VDP by index from the sprite page, rather than baking
each sprite as its own padded rect.

**Priced on the full bake (244 keys, 239,328 B = 7479 MD 8x8 tiles).**
The MD VDP reads a sprite's tiles CONTIGUOUSLY from a start index, so
sharing is only expressible at whole-run granularity:

    run length        tiles kept     saving
    1 tile             4525 (60.5%)   39%   <- NOT expressible
    2 tiles            5666 (75.8%)   24%
    4 tiles (2x2)      6588 (88.1%)   12%
    8 tiles            7088 (94.8%)    5%
    16 tiles (4x4)     7424 (99.3%)  0.7%

**Sharing collapses with run length**, because each animation frame is
distinct art. At the sub-sprite sizes real sprites use, an indexing layer
buys 12% at best and 0.7% at worst. NOT WORTH BUILDING for the sharing.

**The other half of the notion is right.** 1610 of 7479 tiles (21.5%) are
FULLY BLANK — bounding-rect padding, from `bake_mdspr.py`'s "a key's rect
width is the max drawn extent over its rows". A tighter sub-sprite
decomposition (several small boxes skipping empty regions instead of one
padded rect) recovers most of that, needs no indexing scheme, and is
exactly Mike's earlier "make them at their original size".

**BUT NEITHER IS ON THE CRITICAL PATH, and that is the finding.** After
the LOOP29 119 bake widening the normal scene uses 9120 B of its 12288 B
VRAM window — 74%, not full. The claim census says 41.5% of records now
fail on PALETTE MISMATCH and 0.0% on caps or space. **Art size has never
been what stops a claim.**

So the packing work is real and buys ~21%, and it buys nothing today. It
becomes correct the moment VRAM IS the constraint — which is exactly when
the palette-line problem is solved and claims jump. Second move, not
first. Banked here so it is not re-derived.

## 126. NEGATIVE: SPRITES DO NOT CLUSTER BY BAND, SO CRAM SWAPS BUY NOTHING

The dead span census (LOOP29 120) was going to answer "how few CRAM swaps
buy the demand", on the premise from m_main.c:627 that sprites CLUSTER
VERTICALLY. Measured directly off the game's sprite RAM (0xFF7000, 8-word
records, Y span = w[0] top|bot<<8, colour set = w[4]&0x3F) instead of
re-siting the census:

    frame | b0 b1 b2 b3 b4 b5 b6 b7 | whole frame   (28-line bands)
     1800 |  0  0  0  1  4  4  3  0 |   5
     2600 |  0  0  4  4  4  1  1  0 |   6
     3000 |  0  1  5  4  4  1  2  1 |   6
     3400 |  0  0  3  3  6  3  2  0 |   6
     3800 |  0  0  3  4  4  1  1  0 |   5

    worst BAND 6 colour sets;  worst WHOLE FRAME 6

**The premise is false.** The worst band holds as many sets as the whole
frame — one band routinely carries nearly everything on screen. Banding
buys NOTHING, so per-band CRAM swaps cannot solve the palette problem
however cheap the H-interrupts get. The 68K having spare cycles (LOOP29
117) does not rescue this idea. Closed.

## 127. THE FRAMEBUFFER IS THE TAX: 39% OF THE FRAME PAINTS WHAT THE VDP DRAWS FREE

Mike: "how much cycle time would be wasted building a fuller frame to send
to the VDP before updating the screen? as in the sega16 races the beam,
and the genesis VDP isn't built that way."

Right, and it is measurable. System 16 races the beam; the MD VDP also
renders per scanline out of VRAM in hardware at zero CPU cost. Our port
composes into the 32X FRAMEBUFFER, so an entire 320x224 picture must be
painted in software before anything is shown. Per generation (3.6 vints):

    slave compose (sprites + cat1 tiles)   1.40 vints   FRAMEBUFFER TAX
    master drain (MD name table)           0.95 vints   real VDP feeding
                                                        (needed either way)

**1.40 of 3.6 vints = 39% of the frame budget is spent painting pixels the
Genesis VDP would draw for free** — if they could be expressed as tiles and
sprites inside four palette lines.

And nearly all of them could be. The MDSPR claim census (LOOP29 119) puts
only 2.0-3.8% of live records in the "zoomed: SH-2 forever" bucket, which
is the sole class the VDP genuinely cannot render (no scaling hardware).
The other ~96% are ordinary sprites sitting in software because of PALETTE
LINES, not because of any rendering requirement.

So the palette-line problem is not one blocker among several. It is the
single thing standing between this port and giving 39% of its frame budget
back to hardware that does the work for nothing.

## 128. THE COLLISION TRIAGE, AND A CORRECTION TO MY OWN #16 CLAIM

`tools/sdram_map.py` found five declared-extent collisions. Triaged; two
are correct by construction and three were latent.

**Safe by construction, no action:**

  - `STR` / `sused_prev` at 0x398E0 — mutually exclusive on PHASE_CENSUS
    (with it, `sused_prev` is a real static; without it, it is #defined at
    0x398E0 and `STR` does not exist). Good design.
  - `FBCLEAR` / `MDSPR_PAL` at 0x3A300 — deliberate reuse under DIRECT_FB,
    documented at the declaration.

**Latent, now guarded with `#error` (the house pattern, as at the
MD_SPR/ROWHASH guard):**

  - `SLC` (SPR_LINE_PROBE) vs `SPRLATE` (SPR_LATE_DIAG) / `FBP` (FB_PROBE),
    all 0x3A780. The FBP comment already said "SPR_LATE owns them
    otherwise - do not combine the two flags" and nothing enforced it.
  - `HSC_IDX` (HS_CENSUS) on `win_pend` — win_pend is SHIPPING code in the
    documented 28D80 block. Guarded against NATIVE_FRAME.
  - `PSRC` (PICKUP_SRC_PROBE) on `NAT_WALL` — NAT_WALL is shipping NATIVE
    state. **`make ship-us FBXPORT=1 PICKUPSRC=1` now fails to compile;
    before today it silently wrote the probe over the cadence counters.**

One guard was wrong on the first attempt and the failure is instructive:
the `PSRC` #define is UNCONDITIONAL in m_main.c (harmless, since nothing
reads it without the probe), so guarding on `NATIVE_FRAME` alone broke
every shipping build. It now guards on
`defined(PICKUP_SRC_PROBE) && defined(NATIVE_FRAME)`. Adding a guard
without checking whether the declaration was conditional is the same
class of mistake as everything else this week.

**CORRECTION to LOOP29 120 and the sdram_map docstring.** I wrote that
collision #16 (SLC over SPRPEN) is why the span census read 403 distinct
colour sets out of a possible 64. That is not established. `SPR_LATE_DIAG`
and `HS_CENSUS` have NO dedicated Makefile flag — they are reachable only
via `XDEF=NAME` — so `SPRPEN` is not normally written at all, and it
cannot be assumed to be what corrupted SLC. What IS established about the
census is entry 120's other finding: its call site counted 1 frame in
2600, so it is dead code on the R60/NATIVE path. **The census is dead for
that reason; the 403 remains unexplained and should not be attributed.**

## 129. THE PER-BAND CENSUS: THE CHEAP PALETTES ARE NEVER IN THE CROWDED BAND

The decompile thread (LOOP-DECOMPILE 1-6) answered handoff question 1 —
sharing is not a lever, 12 real palettes need 123 distinct colours at
once and zero pairs fit one 15-pen line — and left one thread open:
five live palettes carry effectively one colour across all 14 pens
(0x00, 0x01, 0x03, 0x07, 0x49), so collapsing them frees four slots.
**Are any of them in the crowded 28-line band?** Measured; no.

**Their claim, verified against our rom bytes** (source 0x242A0 + 28*index,
14 words — their derivation, re-run here):

    13 palette indices have ALL 14 pens identical: 0x00-0x07 (pure
    primaries: black, blue, green, cyan, red, magenta, yellow, white)
    and 0xA9-0xAD.
    Of their five: 0x00, 0x01, 0x03, 0x07 confirmed single-colour.
    0x49 is 13-of-14 identical (30FF then thirteen 7FFF) -- 2 pens, not
    1. The substance holds; it still collapses.

**The per-band census** (slot -> index via the request table at 0xFFF500,
Y spans from the game's sprite RAM at 0xFF7000, 28-line bands):

    frame  sets/band                busiest  palettes in it (distinct colours)
    2600   0 0 4 4 4 1 1 0          band 2   0x2F:14 0x28:14 0x29:13 0x2A:11
    3000   0 1 5 4 4 1 2 1          band 2   0x08:14 0x2F:14 0x28:14 0x29:13 0x2A:11
    3400   0 0 3 3 6 3 2 0          band 4   0x32:14 0x08:14 0x2F:14 0x28:14
                                             0x2B:14 0x2C:14

    cheap (<=2 colour) palettes in the busiest band: 0, 0, 0

**And the cheap five are never on screen at all.** They hold slots in the
game's allocator at every sampled frame and NO live sprite record
references them:

    frame 2600/3000: 15 palettes hold a slot, 5 cheap, 0 of those on screen
    frame 3400:      17 palettes hold a slot, 5 cheap, 0 on screen

So collapsing them frees slots in the GAME's 63-slot allocator, which is
not a resource we are short of. It frees nothing in MD CRAM, because they
never occupy an MD line in the first place. **The open thread from
question 1 is closed, negative.**

**What the crowded band actually needs**, and this is the number that
matters: 6 palettes of 11-14 distinct colours each in one 28-line band,
against 3 usable MD lines of 15 pens. There is no packing, collapsing or
swapping of the game's own palettes that fits that. Any solution has to
either reduce what the game asks for (lossy, a play-pass question) or
accept that most sprites stay in software.

## 130. THE SEAM QUESTION: THE WINNING TRIO DOES NOT CHURN

The decompile thread (LOOP-DECOMPILE 7-9) inverted my palette conclusion
by changing the metric. I counted DISTINCT COLOUR SETS per band and got
"6 sets, 3 lines, hopeless" (entry 126). They counted LIVE SPRITE
RECORDS: the top 3 palettes by record count cover 65-75% of everything
drawn, with no colour change and no palette surgery. **Their metric is
the right one** — what matters is how many sprites reach hardware, not
how many palettes exist. The port renders ~4% in hardware today.

They left one question, correctly flagged as a rendering-thread call:
does choosing the three lines per frame create a visible seam when the
winning palette changes? Measured on `rom/night/dblfast_clean.32x`:

    consecutive frames 3000-3019   trio changed 1 of 19  (5%)   [full run]
    (the first read of this, over only 3000-3006, said 0 of 6 -- the
     full 20 frames find one change, so "zero churn" was too strong)
    every 100 frames, 1500-2800    trio changed 4 of 13  (31%)
    mean top-3 coverage            76.2% of live records

    1500-1600  [9,10,11]
    1700-1900  churn: +[2,7]-[10,11], +[8]-[7], +[10,11]-[2,8]
    2000-2500  [9,10,11] STABLE for 600 frames
    2600-2800  [0,9,10]

**Frame-to-frame churn is ~5%, one change in 19 transitions.** The changes cluster at actor
transitions (three in a row across 1700-1900, a scene change) and are
isolated events a handful of times across the level. A reassignment
therefore costs at most ONE frame of those sprites falling back to
software — a blink, not a seam — and `mdspr_danchor` already implements
sustained-majority hysteresis for exactly this case, on one anchor. The
change is to extend it from one line to three.

**Caveat, stated:** 100-frame sampling cannot see changes between
samples, so "4 of 13" is a lower bound on the number of change EVENTS.
What it is not is evidence of per-frame churn, which the consecutive run
rules out directly. Consecutive frames ACROSS a transition (e.g.
1690-1710) would tighten this and were not run.

**Process note:** the sweep's output collided with earlier dumps in
/tmp/pal (files named w2000/w2600 from the mtask work were picked up by
a w*.bin glob and parsed as frames 22000/22600). They were caught by a
short-dump guard, but the scratchpad exists to prevent this and I should
have used it.

## 131. THE FB BANK DIVERGENCE, VERIFIED INDEPENDENTLY AND WORSE THAN REPORTED

**WRONG — RETRACTED BY 135. I used a 64 KB bank stride; the framebuffer
bank is 128 KB (`FBX_PKT_SH` 0x24012000). Every figure below compares the
two HALVES OF ONE BANK against each other, which of course differ: they
are different parts of the same picture. There is no divergence.**

The decompile thread (LOOP-DECOMPILE 10-12) found the two framebuffer
banks diverging in the staged tile region while chasing the Plane B bug,
and correctly handed it to this thread. Verified here on
`rom/night/dblfast_clean.32x` (32X DRAM, two 64 KB banks):

    bank0  f700 -> f1000 :  8320 bytes changed
    bank1  f700 -> f1000 :   347 bytes changed    <- 24x less active
    bank0 vs bank1 @f1000: 57389 bytes differ     <- 88% of the bank

    staged region (bank-relative 0x2000, where FBX_PKT_MD lives):
    bank0 f700->f1000 7498 / bank1 40 / bank0-vs-bank1 36190

Their figures (bank 1 byte-identical across 300 frames, 266 of 40960
differing) do not match mine exactly — almost certainly rom drift, since
`rom/s16.32x` was rebuilt a dozen times on 2026-09-10. **The shape
reproduces and is worse than reported:** one bank is nearly frozen and
the banks disagree on most of their content.

This is LOOP28 91's bank disease, still live. `FBXBOTH` double-writes
the R60 PACKET at the tail and again before the next post; it does NOT
cover the tile staging, so anything the game stages into the current
bank is invisible to a master reading the other one.

**Why it matters more now than last week:** under the DREQ line the FB
never flipped (9 FS writes in 2988 vints, LOOP28 92), so a stale second
bank was inert. The double-buffered line Mike accepted as "an order of
magnitude improvement" flips constantly, which makes this reachable.

OPEN. Not the top item — the three-line sprite offload (entry 130, 70%
of live records into hardware) is worth more and is unblocked — but this
is a confirmed defect on the line we are trying to ship, and it is the
first candidate whenever the double-buffered build shows a tile-level
artifact.

## 132. MDSPRTOP: +19% CLAIMS FROM ONE BETTER-CHOSEN LINE, AND THE 3-LINE PLAN HITS THE BG WALL

**Built and measured.** `make ... MDSPRTOP=1` runs the existing dynamic
anchor election in EVERY scene (it was gated to the wildcard boss scene)
and switches on a 2-record margin held 5 passes, instead of only when the
incumbent owns nothing. Rationale: the scene table pins the normal scene
to set 0x09, which the band census puts at 3 records while set 0 carries
7 — line 0 was being spent on the wrong palette.

    MDSPR_WHY census, level-1, same flags, ~600 vints:

                        without MDSPRTOP   with
      CLAIMED                 477           569     +19%
      NO BAKED KEY           1313          1204
      palette mismatch        890          1076     (a different anchor
      records examined       2785          2931      misses different sets)

    both reconcile: reasons sum to records examined (2785, 2931).

**And a structural fact that caps this whole line of work.** The 68K
uploads the sprite palette as a 15-word DMA to CRAM offset 2
(`md_main.c` mdspr_consume, `0x930F` + `0xC002`) — that is MD CRAM LINE 0,
pens 1-15. The Mega Drive has FOUR palette lines TOTAL, shared globally
between planes and sprites, and ours are spent:

    line 0   text grey ramp + the MDSPR sprite anchor
    line 1-3 the background colour pack (m_main.c:288)

So the decompile thread's "three CRAM lines cover 70% of live records"
(LOOP-DECOMPILE 7-9) is right about the DEMAND and assumes a SUPPLY we do
not have. Three sprite lines means the background drops to one, which is
the `BGPACK2` experiment that blacked the sky (entry 122).

**But entry 122's verdict is not safe, and that matters now.** The black
sky was traced (entry 128 area, and the BGPACK2 investigation) to
`mdp_s_line[s2] = mds_s_line[sc][s2]` at m_main.c:1921 — the MD_STATIC
per-scene path assigning lines from a table BAKED WHEN THE PACK HAD THREE
LINES, so five sets kept pointing at a line whose CRAM was no longer
refreshed. The flag was never fully applied. **Whether the background
actually survives two lines is therefore still unknown**, and it is now
the gate on a 70%-of-sprites prize rather than a curiosity.

Order from here:
  1. MDSPRTOP as measured: +19% claims, one flag, needs a play pass for
     colour flicker at an anchor switch (LOOP29 130 says one switch per
     19 frames, so the exposure is small).
  2. Regenerate the MD_STATIC line tables for a 2-line pack and re-test
     BGPACK2. That is the real experiment; the first one was confounded.
  3. If the background survives, sprites get a second line and the claim
     rate should move far more than 19%.

## 133. THE PALETTE SUPPLY IS CLOSED, BY EXHAUSTIVE SEARCH

The 70%-of-sprites prize (LOOP-DECOMPILE 7-9, entry 132) needs three MD
CRAM lines for sprites. The MD has four total and the background holds
three. So the question is whether the background fits in two, and I have
now asked the one tool that can answer it definitively rather than by
sampling.

`tools/mdpen_bake.py` partitions the scene's colour sets across lines by
EXHAUSTIVE SEARCH, requiring each line's union of quantised colours to be
<= 15 pens, and fails loudly rather than falling back to nearest-colour.
Parameterised its line count (env `MDPEN_LINES`) and asked:

    MDPEN_LINES=3  normal: 39 sets, 36 colours -> lines [15,15,11]
                            (4 pens spare)      BAKES
    MDPEN_LINES=2  BAKE FAIL: scene normal: 39 sets / 36 colours have no
                   exact 2x15 partition -- a real capacity limit, not a
                   bug to paper over

**36 colours do not fit 30 pens. The background needs three lines and
that is arithmetic, not tuning.**

**This corrects entry 121, and the error is the same one I keep making.**
I read MD CRAM at ONE level-1 frame, counted 30 distinct colours, and
declared the third line free. The bake counts 36 over 180 classified
samples. A single frame is not a scene. Entry 122's black sky was the
right verdict reached through a wrong mechanism (I blamed the packer's
dedupe, then the baked `mds_s_line` table); the actual reason is that
there is no partition to find.

**So the palette supply is CLOSED:**

    4 MD CRAM lines total
    3 to the background   (proven: no 2-line partition exists)
    1 to sprites          (shared with the text ramp)

`MDSPRTOP` (entry 132) makes that one line count for +19% claims and is
the end of this road, not a step along it. The 70% figure describes a
demand we cannot supply on this hardware.

**Where the frame-rate work goes instead:** `CAT1MD`. Cat1 tiles are 48%
of the SATURATED processor's compose (entry 124), the flag exists, and it
was reverted on 2026-09-07 for shimmer and a second ground-band palette
through the transform — a rendering defect with a knowable cause, not a
capacity wall. It is now the only large lever left that is not blocked by
hardware.

## 134. MIKE'S PLAY PASS ON dblfast: "MORE FRAMES, NO SPEED" — AND THE NUMBERS AGREE

**The measured ladder below STANDS. The HYPOTHESIS at the end (that bank
incoherency is what FLIPEDGEOFF costs) is RETRACTED by 135: it rested on
entry 131, which was a stride error. FLIPEDGEOFF's 12 points are still
unexplained.**

Hardware pass on `dblfast-20260910.32x`: "I see more concurrent frames
displayed but no speed improvement." Exactly right, and the metrics say
it is a LOSING trade, not a neutral one:

    build                          game logic   IRQ4 miss   MOTION
    base (ship-us FBXPORT=1)          49.7%       50.3%      1.3 fps
    + FBXSTAGE FBXBOTH                47.1%       52.9%      3.7
    + FLIPEDGEOFF TEXTCAP (dblfast)   34.9%       65.1%     16.7

**The double-buffered build runs the GAME 30% slower** (49.7 -> 34.9)
while presenting 13x the frames. The player sees the same or fewer game
events per second, displayed more smoothly. That is what he felt.

**Where the cost is.** `FBXSTAGE+FBXBOTH` is nearly free — 2.6 points —
but buys almost nothing, because the vblank edge guard declines most
flips (3.7 motion). `FLIPEDGEOFF` is what actually buys the 4.5x motion
and it costs 12 points of game speed. With the current flags the choice
is frames OR speed.

**HYPOTHESIS, and it links two open findings.** Flipping should not cost
the 68K anything by itself — the MiSTer RTL defers a late FS write rather
than tearing (LOOP28 91, `srcref/S32X_MiSTer rtl/32X/VDP.sv`). But entry
131 measured the two FB banks diverging by 88%, with one bank nearly
frozen, and `FBXBOTH` double-writes only the R60 packet, not the tile
staging. Under the edge guard the FB almost never flipped (9 FS writes in
2988 vints, LOOP28 92), so that incoherency was UNREACHABLE. Drop the
guard and the FB flips constantly, so the 68K stages into whichever bank
is current while the master reads the other, and work is redone.

**If that holds, the 12 points are not the price of flipping — they are
the price of the bank incoherency, and fixing it gets frames AND speed.**
That makes entry 131 the top item, ahead of CAT1MD: it is the only lead
that could give back both halves of the trade rather than choosing one.

Falsifier: extend the double-write (or a restore-on-flip) to the tile
staging, keep FLIPEDGEOFF, and re-measure both columns. If logic returns
toward 47% while motion stays near 16.7, the hypothesis holds. If logic
stays at 34.9, flipping is intrinsically expensive and the trade is real.

## 135. RETRACTION: THERE IS NO FB BANK DIVERGENCE — I USED THE WRONG STRIDE

Before building the fix entry 134 proposed, I re-checked the measurement
it rested on. It was wrong.

**The 32X framebuffer bank is 128 KB, not 64 KB.** `md_src/packet_fmt.h:170`
puts the R60 packet at `FBX_PKT_SH 0x24012000` — offset 0x12000 into the
SH-2's framebuffer window, which is therefore at least 0x20000 wide.
Entry 131 dumped 0x20000 of 32X DRAM and split it as two 64 KB banks. That
compared the TOP AND BOTTOM HALVES OF A SINGLE BANK. They differ because
they are different parts of the same picture.

Re-measured with the correct 0x20000 stride, `rom/night/dblfast_clean.32x`:

    bank0  f700 -> f1000 :  8667 of 131072 bytes changed
    bank1  f700 -> f1000 :  7283            <- BOTH banks active
    bank0 vs bank1 @f1000:  6583  (5.0%)    <- two different frames
    packet region 0x12000-0x12800, bank0 vs bank1: 60 of 2048 (2.9%)

**Both banks are live, neither is frozen, and a 5% difference between
them is what double buffering is supposed to look like. There is no
incoherency defect.** Entry 131 is retracted and entry 134's hypothesis
with it.

**Note for the decompile thread**: their LOOP-DECOMPILE 10-12 figure
("bank 1 byte-identical to itself 300 frames earlier, 266 of 40960 bytes
differ") should be re-checked against the 0x20000 stride. If they used
64 KB the same artifact applies.

**So FLIPEDGEOFF's 12 points of game speed are UNEXPLAINED.** The honest
next step is to measure where they go — the 68K stage trace
(`tools/stage_lines.py`, which worked on the shim this morning) run with
and without the flag — rather than propose another mechanism. Three
hypotheses today have died on measurement; this one should start there.

## 136. WHERE FLIPEDGEOFF'S 12 POINTS GO: THE MASTER SPINS ON A DEFERRED LATCH WITH FM HELD (2026-09-10)

MEASURED, not inferred. Handoff 5.1 said measure with the shipping
build's own stamps before proposing a mechanism; this entry is that
measurement. Both roms are the ones on disk, `rom/night/dbl_noedge.32x`
(FBXPORT FBXSTAGE FBXBOTH) and `rom/night/dblfast_clean.32x` (the same
+ FLIPEDGEOFF TEXTCAPMASTER TEXTCAPFULL), same rig, same input script,
window vints 2000-2119. Three traces, all read-only:

    ares-headless --frames 2121 --input discover/inputs/play_level1.csv \
      --trace-access-out X.csv \
      --trace-access 0xFFB0B0:0xFFB0B2:cons:2000:2120 \
      --trace-access 0xA15100:0xA15101:fm:2000:2120 \
      --trace-access 0xA15120:0xA15121:comm0:2000:2120 \
      --trace-access 0x902AAC:0x902AAD:irq4:2000:2120 \
      --trace-access 0xFFF144:0xFFF145:miss:2000:2120 rom/night/R.32x
    ares-headless --frames 2121 --input ... --trace-flip X_flip.csv rom/night/R.32x
    python3 tools/stage_lines.py t.csv        (the 12-vint stamp trace, LOOP29 111 form)

`--trace-access` is the 68K bus only (headless-ui/main.cpp:487, the
MegaDrive read/write hooks); SH-2 writes to FBCTL/COMM are invisible to
it. `--trace-flip` is the VDP's own FS write/latch log with the raw
vcounter and a `deferred` flag (vdp.cpp:95-103).

### The numbers

    120 vints                       dbl_noedge      dblfast_clean
    FS writes in window                 0               37
      of which deferred                 0               33   (2121-frame run: 687 writes, 623 deferred)
      written inside vblank             0                4
    deferred-write vcounter             -        21..27, median 26   (active line; latch at 224)
    latch delay                         -        median 198 lines
    68K vint entry late (>line 20)      0               43   (entry at line 50-161)
    lines late, total                   0             2555   = 21 lines per vint
    IRQ4 misses                     61 (50.8%)      81 (67.5%)
    FM raise->drop seen by the 68K   120/120         75/120  (45 never see the drop)
    FM-gate spin polls, 0xffbe54    48054            78879   (+64%; line max 251 -> 261)
    FM-gate spin polls, 0xffbdca    26626            45210

### The chain, each link measured

  1. Under FLIPEDGEOFF the master writes FS at raw vcounter 21-27 —
     active line ~26, i.e. ~64 lines after vblank start — because the
     FBXPORT post lands late (LOOP27 74) and the guard that used to
     decline it is `if (0)`.
  2. ares DEFERS a mid-scan FS write to the next vblank:
     `selectFramebuffer` returns early when `!vblank && latch.mode`
     (ares/md/m32x/vdp.cpp:95-97) and `M32X::vblank()` applies the
     pending select at vblank start (m32x.cpp:72). 623 of 687 writes
     deferred. The Makefile's "ares latches FS immediately mid-scan and
     tears" (line 785) does not describe this fork.
  3. The master then WAITS FOR THE LATCH WITH FM STILL RAISED:
     `while ((MARS_VDP_FBCTL & MARS_VDP_FS) != (fs_o ^ 1) && frt()-w0 <
     18000)` (m_main.c ~6188, "wait for the latch as long as it takes").
     Median 198 lines. FM is not dropped until the restore half after
     it. Exactly the flip vints (2000, 2005, 2008, 2011, 2014, 2018,
     2023 ...) are the 45 vints in which the 68K never sees FM drop.
  4. The game's pass hits gate #22 (the text writer, 0xffbe54, LOOP27
     129) and spins on FM to the end of the frame: +64% polls, spin
     reaching line 261.
  5. The game frame overruns. The NEXT vint's shim entry is late — 33 of
     33 deferred-flip vints are followed by an entry at line 50-161; the
     other 10 late vints are the second vint of the same chain (2001->
     2002, 2009->2010, 2019->2020 ...). IRQ4 misses on every late vint
     (miss rate 1.00 on late, 0.51 on on-time).

Miss rate +16.7 points in this window against the ladder's +12.2
(65.1 vs 52.9 over gameplay_speed's longer interval); same sign, same
order. **That is where the 12 points go: every accepted flip costs the
game the rest of that frame, because the master holds FM through a
latch the hardware defers to vblank.**

### This is hardware behaviour, not an ares artefact

`srcref/S32X_MiSTer/rtl/32X/VDP.sv:400` — `if (VBLK || MODE == 2'b0)
FS <= FBCR.FS;` — and line 165, the FBCR readback returns the LATCHED
`FS`, not the written bit. So on the FPGA the same spin also lasts until
VBLK. The FLIP_EDGE_OFF comment ("expect the FPGA not to tear") is right
about the latch and silent about the readback the spin polls.

Mike's MiSTer verdict on dblfast, "more frames, no speed", is this
entry.

### Caveats, stated

  - dbl_noedge vs dblfast_clean differ in TEXTCAPMASTER/TEXTCAPFULL as
    well as FLIPEDGEOFF. The flip trace ties the delay to the deferred
    flips (33 of 33), not to the text-capture flags, but a
    FLIPEDGEOFF-only rom would make it a one-variable A/B. Not built.
  - The stamp trace shows a second, smaller spin present only on
    dblfast: 29510 FM-register reads from pc 0xff1730 in shim_vblank,
    lines 1-161, 616 polls in late vints vs 37 in on-time ones. It is
    the `fmgate_belt` wait at shim_vblank+0x47e (read 0xA15100 until
    bit 15 clears, bounded 4M iterations) — the shim entering while the
    master still holds FM from the latch spin. A consequence of the late
    flip, not a cause. Identified from the .data disassembly of the
    dblfast md_start.elf (0xff1728-0xff1734).
  - `make clean && make ship-us <dblfast flags>` reproduced
    dblfast_clean.32x in code but NOT byte-for-byte: 43321 bytes differ,
    25460 in the 0x240000 block and 17783 in 0x2f0000 (baked assets),
    63 scattered in 0x41208-0x45603, 1 at the stamp. The asset bake is
    not deterministic across clean builds even at the same flags. Rank
    only roms that were measured, never a "same flags" rebuild.

### What NOT to do

Do not put the edge guard back: it is the 1.3 fps picture. Do not
re-try FLIPDEFER as built (LOOP27 12): committing the WRITE at the ISR
top needs the FM=1 capture that is not there.

### The split this suggests (not built, not measured)

The FS write is committed at issue (m_main.c ~6175: "the write cannot
be taken back"). Nothing between the write and the restore half needs
the latch. So the candidate is: after an FS write that lands outside
vblank, DROP FM AND RETURN (release the game), and run only the
post-latch half — `restore_pages(cycle_dirt | pg_watch)`, the text
restore, the F103 echo — from the V-ISR at the next vblank, once FBCTL
reads the new bank. That is LOOP27 12's option (a): the restore is FB
WRITES, and FM_TEST has counters for whether FM=0 writes land. One trap
the spin was hiding: until the latch, the "new draw bank" is the bank
STILL ON SCREEN, so nothing may compose into it before vblank — the spin
was accidentally the tear guard. Whether the slave starts a compose in
that gap has to be checked before the split is worth building.

## 137. FRAMES AND SPEED: THE FLIP LANDS INSIDE VBLANK ON A GUARD-ON LINE (2026-09-10 04:45)

Built and measured on ares, NOT play-passed. Three flags, all default
off, and the rom is `rom/night/vi.32x`:

    make clean && make ship-us FBXPORT=1 FBXSTAGE=1 FBXPEND=1 FBXISRLIFT=1 \
                               PGSKIPPKT=1 TEXTCAPMASTER=1 TEXTCAPFULL=1

    same measurements, same rig       base/noedge   dblfast     vi
    game logic (gameplay_speed)        47.1/49.7%    34.9%     50.1%
    IRQ4 misses                        52.9/50.3%    65.1%     49.9%
    FS writes per 120 vints (2000+)         0          37        58
      of which deferred to next vblank      0          33         0
      write position (raw vcounter)         -          26       507 = vblank
    FS writes per 2600 frames              12         ~842     1275
    68K post line / IRQ4 line            31 / 94     30 / 95   21 / 72
    late vint entries (of 120)              0          43         0
    torn landings (2600 vints)              0           0         5
    MD CRAM lines 1-3 at f2600           full        full      full
    frame at f2100 / f2600 / f4000       correct     correct   correct

Flips run at ~29 Hz in level play (95-100 per 200 frames, 1 deferred in
2600), every one latched at the vblank it was written in, and the game
logic is 3 points ABOVE the accepted line rather than 12 below it. The
flip rate is generation-bound now: 303 of 600 ISR entries declined for
"nothing shipped" (CEN[23]), 21 on the edge guard.

`presented_fps.py` on the default windows reads 4.3 MOTION for vi
against 16.7 for dblfast, and that number is NOT comparable: the game
runs 43% faster on vi, so the fixed frame windows land on standing
fights (the scene timer reads 837 at f1500 on vi vs 612 on dblfast).
Any-change reads 24.7 fps. See the multi-window sweep below.

### The pre-flip half, VBSPAN on both lines, steady play (600 vints)

    boundary                        dblfast (162 flips)    vi (282 flips)
    ISR entry -> flip_span entry           31.1 lines           21.6
    text capture (TEXTCAPMASTER)            9.8                 10.0
    page merge                              0.7                  0.7
    truth drain                            23.0                  0.2
    at the FS write                        64.6 = vcounter 26   32.6 < 35.9 guard

The two numbers that moved are the two components:

**PGSKIPPKT — the truth drain was capturing the pipeline's own packets.**
`pg_watch` read 0x1001 in steady play: pages 0 and 12. Page 0 holds the
R60 packet + publish word (0x12000-0x1283F), page 12 holds MD-plane
packet B (0x1E800-0x1EFFF); both are written every vint by the
pipeline, so cap_page saw them change every flip and PG_STICKY kept them
watched forever: 2.04 pages x 11.5 lines of capture, plus ~20 lines of
restore, per flip, for bytes that are not game truth. The game itself
writes ZERO tilemap pages in steady level-1 play (60-vint trace of every
68K FB access on dblfast: 627 game + 247 thunk writes, all in FB page
31 = text, lines 101-260). cap_page/restore_pages now skip those two
regions (PG_LO/PG_HI, m_main.c). Isolated on the dblfast line (vd:
dblfast + PGSKIPPKT): renders correctly, CRAM full, 1212 FS writes per
2600 vs ~842, 463 of them no longer deferred.

**FBXISRLIFT + FBXPEND — the packet blast leaves the pre-post slot.**
The 68K's post sat at line 30 because FBXBOTH blasts the R60 packet
into the FB before every post (12 lines of 68K FB writes at FM=0). That
second blast exists because the ISR flips at vblank top and the body's
lift then read the OTHER bank. Two changes:
  - FBXISRLIFT: `fbx_lift()` runs at the top of `flip_span`, before the
    FS write, so it reads the bank the tail blast wrote; the body's lift
    is a guarded fallback (`if (!fbx_landed)`).
  - FBXPEND: the tail blast runs only if FM reads 0; otherwise the
    packet stays staged (WRAM word 0xFFA0FE) and the generated FM-gate
    spin blasts it the moment FM drops, in game context, through a
    vector at 0xFFA0F8 (patch_game.py emits one shared spin routine at
    FMGATE_SPIN_ADDR; every gate thunk jsr's it instead of spinning
    inline; `fbx_late_blast` refuses V >= 0xC0 so no vint can fire
    inside the blast). 2171 of 2518 vints blasted late; the pre-post
    slot is the fallback for the rest.
  The post moved 30 -> 21 and IRQ4 94 -> 72.

### The dead path, so nobody re-walks it

FBXISRLIFT WITHOUT FBXPEND (rom ve: dblfast + FBXISRLIFT, no FBXBOTH):
BLACK SCREEN, MD CRAM lines 1-3 empty from frame 1200, 116 torn
landings per 1500 vints. Cause: a 68K FB write at FM=1 is DROPPED —
ares `bus-external.cpp:45/56` (`if(vdp.framebufferAccess) return;`)
and the FPGA `IF.sv:946` (`if (!ADCR.FM)`) alike — and FBXSTAGE's tail
blast at line ~85 runs inside the master's window (FM up until ~140-
190). FBXBOTH's "redundant" second blast was the only one landing. The
MD palette never loads because the scene classification rides the
packet.

The same rom built with holes but no BOTH and no PEND (invbl2) showed
the identical black-background failure at 30 Hz flips: also transport.

### Traps hit this session, stated once

  - **zsh does not word-split an unquoted variable.** `F="A=1 B=1";
    make ship-us $F` passes ONE argument. Five bisect roms (va-ve, first
    pass) and the first "invbl" were all `make ship-us FBXPORT="1 ..."`
    = the base rom; invbl differed from invbl2 by 1.3 MB for that
    reason. Spell the flags out, or use `eval`/arrays. Verify every
    probe build's `.build_flags` before reading a number from it.
  - `--trace-access` sees the 68K bus only; SH-2 writes to FBCTL/COMM
    are invisible. `--trace-flip` is the flip instrument.
  - A rom whose game logic differs cannot be compared on frame-indexed
    windows: the fixed input script lands on different game moments.
    Compare flips per vint, any-change, and CRAM/screens; MOTION needs
    the game's own timeline.
  - The FBFREE "hole" at 0x12000 sits inside tilemap page 0, which the
    truth machinery captures and restores whole. That is why FBXBOTH's
    LOOP28 91 diagnosis ("the bank it reads is not the bank the blast
    wrote") was true and incomplete: the restore was also copying a
    stale publish word across banks every flip.

### What is NOT established

  - No play pass. Text (HUD) and sprites look right on stills at four
    frames; nothing temporal has been judged by a human.
  - Hardware: the late blast relies on the FM drop being observable in
    game context and on the V-counter guard; both are the same on the
    FPGA by the RTL, but the MiSTer has not run this rom.
  - Torn landings 5 per 2600 (dblfast 0): the late blast and the ISR
    lift are separated by the FM protocol, not by a barrier; find those
    5 before shipping.
  - Motion on a matched game timeline: see the sweep line below.

### The motion sweep (13 windows, 1600-4000, every 200 frames)

    rom            MOTION  any-change   per-window MOTION
    vi               4.7      24.4      3 3 4 11 3 8 8 3 1 2 14 0 1
    dblfast_clean   17.0      33.5      5 4 1 0 1 40 44 44 43 6 10 12 11
    base             2.7       8.2      1 0 2 8 4 4 8 3 1 0 1 1 2
    dbl_noedge       3.4       8.2      0 2 2 1 8 9 8 4 1 0 1 2 6

dblfast's four 40-44 windows are frames 2600-3200, scene timer 991-
1181. vi reaches that timer range at frames 1806-2186, where the
frame-indexed input script is doing something else, and its windows
there read 3-4. **The fixed script gives a faster rom different inputs
at the same game moment, so MOTION is not an A/B across logic rates.**
What IS measured on vi at 2600-2640 (the script's hold-right stretch):
the background band shifts -12 px then -8 px per 20 frames (dblfast: -4,
0) and 48-52% of the area changes per 20 frames, so the planes scroll
and the picture moves; consecutive-frame changes are sprite-sized, under
the 5% MOTION threshold. The honest presented-frame number for vi is the
flip trace: 95-100 FS writes per 200 vints, none deferred.

**Next: Mike's play pass on `rom/night/vi.32x` against `dblfast_clean`,
then the MiSTer.** The MOTION metric needs an input script keyed to the
game's timer before it can rank roms of different speed.

## 138. MIKE'S PASS ON vi: BLACK BACKGROUND — THE PACKET WAS INSIDE THE BACKGROUND PLANE'S PAGE (2026-09-10 12:05)

Mike, ares GUI: `vi.32x` "black background, stuttering frames";
`dblfast_clean` "more frames but gameplay very slow". Headless, vi kept a
full palette and correct stills on every input script to frame 12000,
so his failure is on a scene the scripts never reach.

Cause, by reading rather than reproduction: the R60 packet sat at FB
0x12000 = tilemap PAGE 0, and page 0 is the game's BACKGROUND plane page
(page selects never change: BG = 0, FG = 7, NOTES-FROM-DECOMPILE 2 /
LOOP-DECOMPILE 11). The compose reads TILEMAP_C, the captured truth.
Entry 137's PGSKIPPKT hole stopped capturing page 0 longs 0x000-0x20F,
i.e. name-table rows 0-16, so any scene that shows those rows drew them
from the zeros boot wrote: black. Level 1 does not show them; the title
and later scenes do. And the SHIPPING line has been showing the PACKET
there: `base.32x` attract at frame 2000 renders the title eye with
packet bytes as tiles across its top rows (`scratchpad base_attr_2000`),
which is START-HERE's "attract title never renders" bug, or the visible
half of it.

Fix (packet_fmt.h, m_main.c PG_LO/PG_HI): the packet moves to page 12's
first half, FBX_PKT 0x1E000, publish word 0x1E7F8 (max packet 924
words = 0x738 bytes, fits). Page 12 is the blank page the game never
writes; its second half already carries MD-plane packet B; its first
half is the FB_SPR mirror only the FBSPR probe uses (`#error` guards
the pair). Page 0 is captured whole again; page 12 is skipped whole and
TILEMAP_U page 12 stays zero, which is what "blank" means.

    rom/night/vi2.32x   same flags as 137:
    make clean && make ship-us FBXPORT=1 FBXSTAGE=1 FBXPEND=1 FBXISRLIFT=1 \
                               PGSKIPPKT=1 TEXTCAPMASTER=1 TEXTCAPFULL=1

    game logic 50.1%, misses 49.9% (unchanged from vi)
    FS writes per 500 frames, 0-7000: 221-265, 1 deferred in 3357
    ISR flips 3340 / body 457 / held 3041 / edge 618 in 6838 cycles
    torn landings 4 in 6838; late blasts 6087
    CRAM lines at 7000: [14, 15, 15, 10]
    stills: level 1 f2100/f2600/f6000 correct; ATTRACT TITLE EYE CLEAN
    at frame 2000 where base shows packet garbage in its top rows.

The stutter: vi's inter-flip intervals over 1000 frames are 233 x 2
vints, 13 x 3, 2 x 6, 2 x 1 — a regular 30 Hz, generation-bound (held
= no closed generation on ~45% of vints). dblfast's are 21 x 1, 12 x 2,
71 x 3, 63 x 4. If 30 Hz reads as stutter, the next lever is the slave's
compose (cat1 tiles, 48% of it), not the flip.

Not play-passed. `vi.32x` is superseded; do not hand it over.

## 139. THE FPGA NEEDS THE MD-PLANE MAILBOX MIRRORED ACROSS THE BANK SWAP (2026-09-10 12:25)

Mike's "black background" verdicts on vi and vi2 were from the MiSTer,
not ares. His ares state of vi2 (`rom/night/vi2.bs1`, vint 344,
`tools/state_frame.py` after fixing its VRAM anchor for repeated art)
renders the full Zeus intro; his MiSTer shots of the same scene
(`20260910_161115/161450-vi2.png`) show the 32X layer (Zeus, text, the
grave) over BLACK: the MD planes are missing on hardware only.

Isolation on the rig, one build each, screenshots via /dev/MiSTer_cmd:

    vi3 = vi2 + PGKEEPB (page 12 second half captured/restored again):
          attract graveyard with sky, trees, temple. Background BACK.
          Headless cost: FS write past the guard on most vints, 10637 edge
          declines in 6915 cycles, flips ~21 per 100 (vi2: 48).

So the page-12 restore was doing a second job: carrying MD-plane packet
B (0x1E800) into the new draw bank at every flip. On ares the 68K's
vint-top consume always finds the packet in the bank the master wrote it
to; on the FPGA it does not, and without the mirror the planes never get
their tiles/pens. Why the FPGA differs is NOT established (a
consume-vs-latch ordering difference is the candidate); the mirror is
what the hardware needs, so the mirror stays, made cheap:

**vi4** (`rom/night/vi4.32x`, same flags as vi2): flip_span reads the
two packet magic words before the FS write (two loads) and, after the
latch, replays a still-unconsumed packet A/B from the master's own
staging image (`md_pktA` 0x06039A00 / `md_pkt` 0x0603E780, 368 longs,
blank bit re-applied) into the new bank, or zeroes the slot if it was
consumed -- exactly what the page restore used to do for those bytes,
off the pre-flip path.

    vi4 headless, 0-7000: FS writes 454-501 per 1000 frames, 1 deferred
    of 3340; ISR flips 3325, held 3035, edge 620; torn 4; CRAM full;
    game logic 50.1%, misses 49.9%; level-1 stills correct.
    (DIAG[39]/[42] are shared with the window's pend counters -- the
    "carried" counts there are not separable; give them their own slots
    before reading them.)

MiSTer result for vi4: see the line below.

## 140. HARDWARE SPEED, READ OFF THE GAME'S OWN COUNTER: THE ACCEPTED ROM RUNS AT ~15% ON THE FPGA, vi4 AT ~44% (2026-09-10 14:50)

`BOOTGAMERATE=1` now paints (64 - misses at 0xFFF144 per 64 vints) into
the MD palette (commit 1f00bef; the old probe counted IRQ4 completions,
which run at vint rate on every build and read 64 everywhere). Decoder:
scratchpad gr_decode.py — quantise the dominant MD-plane colour to
3-3-2 bits, value = R | G<<3 | B<<6, bit 7 is the bias. Calibrated on
ares: both roms read 32 = the 50% gameplay_speed reports.

MiSTer, attract demo, nine screenshots 20 s apart per rom, game frames
per 64 vints (the counter resets at scene cuts, so a sample that
straddles a cut reads low; the population is what to read):

    base_gr (accepted line + probe):  25 14 17 55  8  6 11 13 10
    vi4_gr  (LOOP29 139 + probe):     46 28 28 24 48 29 28 26 48

**On silicon the accepted rom runs the game at roughly 15%; vi4 at
roughly 44%, 2-3x faster, with the picture at ~29 Hz.** Mike's verdict
"clean presentation, too slow to play" on vi4 stands: 44% is slow. But
it is the fastest rom the rig has run, and the accepted line is far
slower on hardware than the 47-50% ares shows for it. The ares/FPGA gap
on the base line is the master's FM span: real framebuffer writes and
real SH-2 bus arbitration hold FM longer, and the game's text writers
spin on it. ares does not model that cost; every "logic rate" this log
quotes from ares is an upper bound on the hardware.

**This closes the flip arc.** vi4 is the line to build on. What limits
the game on hardware is FM time, and the game's own gates are the lever
now — the pivot Mike asked for. First step, built as vi5 = vi4 +
TXTWRAM (the text writers store to WRAM and never gate on FM): ares
51.7% (+1.6 over vi4), flips ~42-48 per 100, SPRLATE ramp draws 57 —
the sprite-pair symptom that failed TXTWRAM's play pass on 2026-09-07
is still present but far below that session's 247. Hardware number: see
the line below.

    vi5_gr (vi4 + TXTWRAM + probe), clean pass:  48 64 28 27 13 64 30 27 40

Same population as vi4 (median ~30 vs 28). TXTWRAM does not move the
hardware speed: the text writers' FM spins are not where the silicon's
time goes either. vi4 stays the line; TXTWRAM stays a probe (and its
pre-post footprint copy costs flips, edge declines 3449 in 4000 vints).
The BOOTBURNW/R fetch-tax probes were rebuilt on this line but the W
instrument reads 127 (clamped) on ares before touching hardware; fix
the stamps before reading it on the rig.

## 141. THE PIVOT, PATCH 1: THE GAME'S FRAME RELEASE IS OURS (2026-09-10 18:00)

`GAMEGATE=1` (patch_game.py, Makefile, md_main.c). LOOP-DECOMPILE 22
found the gate: the main loop blocks at 0x397E until IRQ4 releases it
at 0x2AC6, and IRQ4 counts an overrun at 0x2ABE and takes a short path
when the loop is late. Fourteen bytes at 0x2AB8 now read

    jsr (thunk).w ; beq.s 0x2AC6 ; nop ; nop ; bra.w 0x2C06

and the thunk (appended to the FMGATE table) returns Z=1 only when the
shim's go token at 0xFFA0F5 is set AND the loop is waiting, consuming
the token; otherwise Z=0 and IRQ4 takes its own short path, uncounted.
The shim sets the token at its hold exit on a flip echo (F102 or F103 --
the ISR writes both, and reading only F102 halved the game: 26.3%) or
after 4 vints without one (loads and blanks must not freeze).

    vi6 = vi4 + GAMEGATE, ares, 4000 vints:
      game logic 47.9% (vi4 50.1)     overruns counted by the game: 0
      releases 1958 = flips 1877 + fallback 98
      FS writes per 1000: 516 421 478 462 (vi4: ~470)   torn 4
      stills f2100/f3000 correct

The game now advances exactly once per presented frame. Its speed IS
the flip rate; there are no wasted passes and no uncounted short paths.
This does not make it faster than vi4 (the flip rate is the same,
generation-bound, ~29 Hz); it makes the 68K's frame irrelevant to the
budget and hands the pacing to us. The next two patches (sprite writer,
tilemap loop heads) build on that.

## 142. TWO HARDWARE NUMBERS: THE FETCH TAX IS 20%, AND THE FPGA FLIPS AT A THIRD OF ARES' RATE (2026-09-10 18:20)

Both read off the rig with the value instrument, five shots each, after
fixing the probes (commit b1e399d: the burn stamps are converted through
the V-counter jump and moved off words the landing diag clobbers; the
rate probe reports releases under GAMEGATE).

**Fetch tax.** `SHIMBURN=5` loop, 200 volatile iterations:
    from 68K WRAM   ares 24-25 lines   FPGA 25 25 25 25 25
    from cart ROM   ares 24-25 lines   FPGA 32 29 30 31 30
The adapter charges ~20% on ROM-resident 68K code with the pipeline
live. ares charges nothing. Every game instruction runs from ROM, so a
150-185-line game frame is 180-220 on silicon before any shim -- part
of why the accepted rom reads 15% there and 50% on ares.

**Release rate under GAMEGATE (vi6_gr): 17 10 17 16 19 per 64 vints.**
The game paced to flips runs at ~25% on the FPGA against vi4's ~44%
running free (LOOP29 140). So the hardware presents far fewer frames
than ares' 29 Hz -- the slave's compose (real FB write stalls) or the
edge guard on real timing. Measuring the FS-write rate directly on vi4
(`FLIPRATE=1`) next. **GAMEGATE as built is the wrong policy for
hardware today**: pacing the game to a ~10-19 Hz flip clock halves it.
The gate is correct and stays; its token policy must be "release every
vint" until the flip rate is worth pacing to, or better, a policy that
releases on flip OR every vint whose previous frame completed.

## 143. ON THE FPGA THE FLIP MISSES THE GUARD ALMOST EVERY VINT (2026-09-10 18:30)

`FLIPRATE=1` variants posting a DIAG slot's delta per 64 vints
(`FLIPRATEDIAG=N`, commit pending), five rig shots each, ares calibration
in brackets:

    FS writes  (CEN[17])     FPGA  1  7  3 (+2 garbage reads)   [ares 32]
    edge declines (DIAG[44]) FPGA 63 63 63 63 63 (clamped)      [ares 0]
    ISR flips (DIAG[58])     FPGA  3  4 10  5  2                  [ares 32]

So the in-vblank flip that entry 137 built lands on ares and not on
silicon: on the MiSTer the ISR reaches the FS write past the 35.9-line
guard on nearly every vint, and the picture updates 2-6 times a second
-- Mike's "slideshow", which the game-speed numbers (vi4 ~44%) hid. The
ares pre-flip budget (post wait 21.6 + text capture 10.0 + merge/drain
0.9 = 32.6 lines) leaves 3 lines of margin that the hardware does not
have; which term grows on silicon is the next measurement (mean lines
at the guard, mean post wait, no-post bails, same instrument).

Consequences: GAMEGATE (141) paced the game to this 2-6 Hz clock and
halved it (142); it stays default-off. vi4 remains the line to play.

## 144. THE PRE-FLIP HALF ON SILICON: THE 68K SIDE MATCHES ARES, THE MASTER'S FB READS DOUBLE (2026-09-10 18:45)

Same instrument (`FLIPRATEMEAN=50/52`, `FLIPRATEDIAG=60`), five rig
shots each, ares calibration in brackets, lines from ISR entry:

    post wait (flip_span entry)   FPGA 23 21 25 25 24    [ares 21]
    at the guard check            FPGA 43 41 45 44       [ares 31]   (one garbage read)
    no-post bails per 64 vints    FPGA  0  5  1  1       [ares 4]

The 68K's consumes and post land where ares says. What grows is the
master's own work between entry and the guard: text capture (928 longs
of FB reads), packet lift (up to 462 longs), page merge and drain --
~10 lines on ares, ~20 on the FPGA. Real framebuffer reads by the SH-2
cost about twice what ares charges. With the guard at 35.9 the write
misses by ~8 lines on almost every vint (143).

Fix direction: take the FB reads off the master's critical path. The
text capture goes back to the SLAVE, posted at ISR ENTRY (not after the
post): the slave waits for FM itself and copies while the master waits
for the 68K, and the master only joins. LOOP28 96's 31.8-line pickup
latency is hidden under the 21-line post wait instead of added to it.
If that is not enough, the packet lift follows the same route.

## 145. TEXTCAPEARLY: THE TEXT CAPTURE LEAVES THE MASTER'S CRITICAL PATH (2026-09-10 18:50)

`TEXTCAPEARLY=1` with the slave capture (TEXTCAPMASTER off): the master
posts SYNC[4]=0x4000 at V-ISR entry, right after the live-window check;
the slave's handler waits for FM (bounded ~40 lines, answers 0x4002 if
it never rises) and copies; flip_span skips its own post and only
joins, falling back to the inline capture on 0x4002. A body-fallback
flip on a vint whose ISR did not post still posts before its join
(`fs_posted_early`, cleared at window pickup).

    vi7 = FBXPORT FBXSTAGE FBXPEND FBXISRLIFT PGSKIPPKT TEXTCAPEARLY
    ares, 4000 vints: FS writes 474 421 457 477 per 1000, 1 deferred;
    ISR flips 1809, body 814, held 1288, edge 1424, slave-nocap 2, torn 4;
    game logic 49.8%; CRAM full; stills correct.

On ares it is par with vi4 (edge declines somewhat higher: the join
sometimes waits on the slave's pickup). The point is the FPGA, where the
master's inline capture was half of the ~20 lines that missed the guard
(144). Rig numbers: below.

    vi7 on the rig: mean lines at the guard 61 63 42 42 (two "42 no-bias"
    = channel not posted); FS writes per 64 vints 4 0 7 (two not posted).
    NO GAIN ON SILICON. The slave's pickup latency, which ares hides
    under the post wait, adds on the FPGA: its service points come
    between compose strips that run at real speed. vi7 renders fine on
    ares and is not a hardware improvement; vi4 stays the line.
    Next: the 68K's own hardware timeline (entry line, post line), since
    the 21-25-line post wait is now the largest term.

## 146. THE 68K SIDE ON SILICON MATCHES ARES (2026-09-10 19:05)

`BOOTENTRYV=1` / `BOOTPOSTV=1` (value instrument, lines from vblank
start), five rig shots each, vi7 line:

    68K vint entry line   FPGA  5 37  7 (121 garbage)  4    [ares 11]
    68K post line         FPGA 27 23 25 45 (one garbage)    [ares 21]

Entry is early (the 37 is a belt wait on a late ack), the post is where
ares puts it. So the post wait (144) is not the hardware term; the
master's own reads after the post are (~20 lines on silicon vs ~10),
and TEXTCAPEARLY (145) made it worse because the slave's pickup is
late on real strips. What would take those reads off the critical path
is doing them BEFORE the post, at FM=0 -- which ares says returns
garbage (FM_TEST, LOOP27 12) and which has never been checked on the
FPGA. The rom's DIAG[24]/[25] pair (FM=0 read of a 68K-untouched word
vs the FM=1 read) answers that on the rig with the value instrument.

## 147. TEXTCAPMASK: THE TEXT WRITERS SAY WHICH ROWS TO CAPTURE (2026-09-10 19:15)

On silicon the master's reads after the post are ~20 lines and the
guard is 35.9 - 25 (post) = ~11 (144, 146); the text capture (928 longs)
is the fat one, and it copies 29 rows for ~14 game writes a vint that
touch one or two of them. The FM-gate thunks already sit in front of
every text writer, so they now MARK the 4-row group they are about to
write (WRAM byte 0xFFA1A6, `TXTMASK` in patch_game.py):

    0x3A9A / 0x3AA4   shared copy / clear loop heads: group from a1
                      at every iteration, text page (0x85Fxxx) only
    0x3AAE            credit writer: group from its offset var 0xFFF024
    0x153E / 0x4D88   fixed rows 25-26 (group 6)
    0x369C / 0x1ACCA  clear-all: every group

The shim posts the byte in COMM2's high byte (the master reads bits
0-2 for the bank) and clears it when it sees the master's echo; every
8th vint the mask is forced full so an ungated writer is stale for at
most 8 vints. The master's inline capture copies only the marked
groups (128 longs each).

Which gates run in level play, from the 68K trace's FM reads by thunk
address (dbl_noedge, 120 vints): 0x3716 dispatcher flag gate 222/vint
(spinning), 0x3AAE 400/vint (spinning), 0x3A9A 14/vint (the glyph
copies), 0x4D88 1/vint; the clear-alls never. Thunk table 211 -> 265
words, still under the 0xBFF0 ceiling, which LOOP-DECOMPILE 28 shows is
the game's object table at 0xFFC000, not a budget we chose.

    vi8 = vi4 + TEXTCAPMASK, ares 4000 vints:
      FS writes 525 485 475 484 per 1000, 1 deferred (vi4: 501 488 468 481)
      edge declines 229 (vi4 ~350 over 4000), ISR flips 1947, torn 5
      game logic 50.1%, CRAM full, HUD/credit/lives text correct on stills
      mask byte in steady play: 0x01 (the HUD row group)

Rig numbers: below.

    vi8 on the rig, five shots each:
      mean lines at the guard    63 32 31 32 32   (vi4: 43 41 45 44)
      FS writes per 64 vints     16 19 21 23 21   (vi4: 1 7 3)
    The write is inside the 35.9-line guard on silicon now, and the
    FPGA presents ~18 frames a second instead of 2-6. Still short of
    ares' 32 per 64: the remaining declines and the generation holds
    on real strips are the next split. vi8 is the line for Mike's eye.

## 148. vi8 ON THE RIG: THE REMAINING GAP IS THE GUARD'S TAIL, NOT THE COMPOSE (2026-09-10 19:40)

Per 64 vints, five shots each (ares in brackets, vi8):

    edge declines DIAG[44]   32 20 16 33 18   [~2]
    holds DIAG[29]           36 28 31 26 11   [~32]
    game frames (misses)     33 64 39 30 34   [32]  (vi4: 24-48, median 28)
    FS writes                16 19 21 23 21   [30]

Holds match ares: the slave's compose is not slower on silicon in a way
that costs frames. Declines do not: with the guard mean at 31-32 lines
the distribution's tail still crosses 35.9 on a quarter to a half of
the vints. The post wait is the term with the tail (21-27 on the rig,
146), and its hardware excess over ares is the 68K's own path from
entry to post: ~19 lines on silicon (entry 4-7, post 23-27) against ~10
on ares (11 -> 21). The consumes are VDP DMAs out of the framebuffer
through the adapter; measuring their span on the rig next
(`BOOTCONSV=1`: V at cons.mark minus V at cons.entry).

Two levers if that is it: DMA only what changed (the SAT and the
sprite palette go every vint today), or the two-post protocol -- post
before the consumes so the flip lands at ~15 lines, drop FM for the
DMAs, post again for the window; the 139 mirror already carries the
plane packets across the swap that this reorders.

## 149. TWOPOST: THE FLIP AT 8 LINES, AND MD CONTENT THAT GOES WRONG -- PARKED (2026-09-10 20:05)

`TWOPOST=1` (default off; needs FBXPEND): the 68K posts BEFORE its
consumes (post A at line ~5), the ISR flips and then eats post A, drops
FM and echoes F104 (flipped) / F1FE (declined) AFTER its mirror; the 68K
does its consumes at FM=0, raises again and posts B; the window and the
text restore run on post B. Pieces that had to exist for it: the
plane-packet mirror replays the bytes the window WROTE (kept by the
window itself, since `hs_patch` edits the FB copy and the compose
rebuilds the staging after the ack), and the MDSPR palette + SAT are
mirrored the same way. An earlier cut echoed F102 before the drop and
raced post B; another guarded the wrong of the two push sites so the
build ran twice.

    vi9, ares 4000 vints: FS writes at raw vcounter 232 (= ~8 lines
    into vblank) on 1140 of 1145; flips 436-497 per 1000 (par); edge
    declines 229 (par); torn 0; game logic 49.7% (par); IRQ4 ~100
    (vi8: 72).
    BUT: fence cells black, player a silhouette, CRAM line 3 at 7 pens
    of 10-11 -- MD plane content wrong. Same with the consumes moved
    back BEFORE post A (vi9x), so the after-flip consume path and the
    mirror are NOT the cause; something in "ISR eats post A / drops FM /
    post B opens the window / text restore at window start" corrupts
    what packet B delivers (its pal section and tile batches), on ares.
    MDSPR art and the SAT differ from vi8 by 32 and 30 bytes; tile art
    and both name tables differ massively (LRU-dependent, so not by
    itself proof).

Not found tonight. The protocol is the right shape for silicon (the
guard is met by 28 lines) and stays in the tree as a probe. vi8 is the
line: hardware flips 16-23 per 64, game ~35 per 64.

What to try next, in order: (1) build vi9 with MDVERIFY and read the
stale/gap counters against vi8's, not against nothing; (2) diff packet
B's 368 longs in the FB bank the 68K consumes against the master's
tp_lastB on a vint the 68K consumed (a savestate at 2100 has both);
(3) the text restore back into flip_span with TWO_POST, to rule the
move in or out.

## 150. vi10, AND CATEGORY 1 IS A ROM BIT (2026-09-10 20:10)

Mike on vi8 (rig): "sprite art feels solid, backgrounds aren't working
completely." vi8's plane-packet mirror (139) replayed the master's
STAGING buffer, which the compose rebuilds after the ack (found in 149),
so on the FPGA, where the mirror fires on every flip, the planes can
receive the next packet in place of the pending one: tile batches and
NT deltas out of order = partial backgrounds. Sprites do not ride that
path. **vi10** = vi8's flags on the current tree: the mirror replays
the bytes the window wrote (kept by the window, hs_patch included) and
the MDSPR palette + SAT the same way. ares: flips 479-520 per 1000,
edge 242, torn 4, CRAM full, logic 50.0%, stills correct. On the rig at
20:06 for Mike's eye.

**Question 5 answered by the decompile thread (LOOP-DECOMPILE, tools/
bake_cat1map.py -> sh_src/cat1map.bin/.h):** category 1 is bit 15 of
the tile word, written by the unpacker from the ROM stream; static for
the whole scene; verified 20,480 of 20,480 bytes against live tile RAM
at two frames 1,200 apart. Per scene: 11.3% (level 1), 43.8%, 35.9%,
6.2%, 17.2%. Consequences for this thread:
  - CAT1MD's play-pass failure (2026-09-07: "grass feels shimmery") was
    not classification. The promotion is applied by TWO renderers -- the
    FB cat1 pass over sprite rows, MD plane A elsewhere (C1 step 2) --
    drawing the same static tile in 5-bit and 3-bit colour, with the
    boundary moving with the sprites. The fix is one renderer per tile
    for the whole scene, or two renderers that are pixel-identical
    (the FB cat1 pass painted with the MD line's quantised colours).
  - Every compose figure in this log was measured on level 1, the
    cheapest cat1 scene but one; scene 1 is four times the cat1 load.
  - The bitmap can replace the per-frame classification outright.

## 151. CAT1MD RETRY: ONE COLOUR FOR ONE TILE (2026-09-10 20:12)

With category 1 static (150), the shimmer's cause is the C1 step-2
split: the FB draws a cat-1 cell over sprite rows, MD plane A draws it
everywhere else, in different colour depths, along a boundary that moves
with the sprites. Under MDBGALL the FB's only tiles are those cat-1
cells, so the tile sets' 32X CRAM entries can carry the MD line's own
quantised colour: `cram_paint_tile` (m_main.c) paints a set from
`mdp_s_qc` (9-bit, expanded 3->5 bits per channel) whenever the set has
an MD line, arcade colour otherwise. Both renderers then put identical
pixels either side of the boundary, on ares by construction; on the
FPGA the MD and 32X DACs may still differ by a level, which only Mike's
eye can weigh.

    vi11 = vi10 + CAT1MD, ares 4000 vints: flips 483-532 per 1000,
    ISR flips 1996, held 1704 (vi10 1728), edge 246, torn 4, CRAM full,
    logic 50.0%, stills correct incl. the fence cells.

Not play-passed. Scene 1 (43.8% cat1) has not been measured on any
build; level 1 is 11.3%.

    Mike, rig, 20:15: vi10 "YOU FIXED THE BACKGROUNDS!" -- the plane
    packets carried by their written bytes is the hardware fix. vi10 is
    the line. vi11 (CAT1MD colour match) goes to the rig next.

## 152. THE MISSING TILES ARE THE MD RESIDENCY MAP, EMPTY FOR TEN SECONDS AFTER A CUT (2026-09-10 20:40)

Mike on vi11 (rig): "maybe backgrounds are 100% fixed... still lots of
missing tile data."  New instrument, then the number.

**`tools/nt_dump_ares.py`** pulls nt_dump.lua's five evidence regions
(snap/ntmir/mdtag/sline/tmap) out of ares-headless instead of MAME.
nt_dump.lua cannot serve this question any more: under R60 no packet
lands in MAME (CLAUDE.md), so its mirror and tags are not our machine's.
The ares dump reads `md_dbg_nt` (0x3D200) and `md_tag` (0x3B400)
directly, so it needs no DIAG slot and no probe build -- it audits the
SHIPPING rom.

Cell census of the shipped mirror, vi10, per plane (blank = the cell
ships MD_BLANK_SLOT, i.e. nothing is drawn on the MD):

    frame   md_tag claimed   plane B blank / distinct   plane A blank / distinct
     600         764            160 / 349                  314 / 156
    2400         769            160 / 347                  320 /  81
    3000         809            160 / 354                  319 / 117
    1800         117            206 /  54                 1120 /   0

**In the demo the name tables are HEALTHY and the blanks are all by
design.** Plane B's 160 are rows 24-27 exactly -- the bottom-band blank
this file's own comment installs (visible lines 192-223, always FB
floor). Plane A's ~320 are rows 20-27, FG cells whose tilemap word is 0.
Not one cell is a free slot, a never-written entry, or an unclaimed one.
No slot set is saturated: on-screen demand is 428-505 slots spread over
127-128 sets, worst set 7 ways of 8.

**The defect is the cut window.** From ~f1650 to ~f2200 `md_tag` holds
117 of 1024 claims (against 764-809 in the demo) and plane B names 54
distinct slots against 347-354. It refills to 769 only by f2400: about
600 frames, TEN SECONDS, of a depopulated residency map. Plane A being
100% blank across that window is correct -- the game clears the FG
tilemap at a cut -- but plane B's collapse is not.

**It is not an upload backlog.** `md_dirty` popcount is 0 at f600, 1800,
2400 and 3000 and 71 at f2100; no on-screen cell names a dirty slot at
any sample. The shipper is idle. The map is empty because claims are not
happening or are not surviving, not because art is queued behind them.

**COUNTER TRAP, and it cost me an hour: DIAG[39], [50] and [53] are
each written by three different subsystems.** [50] is `+= landed` on the
DREQ path (10413) AND md_tag evictions (13055) AND 5323; [53] is the
packet count (10414) AND md_tag claims (13008); [39] is three sites.
Any "evictions per frame" or "claims per frame" figure read out of them
-- including the ones I built two hypotheses on before checking -- is
the DREQ counters. The minefield this file has numbered to #16 now has
a DIAG-index arm. **Do not read [39]/[50]/[53] for allocator questions;
give the allocator its own block.**

Open: which of the two it is (no claim, or a claim then wiped) needs
that block. `mds_flush` is edge-only (3-4 blanks in the whole run) and
`mds_install`'s invalidation is selective and idempotent, so neither is
an obvious wiper; set relocations are 22 across the window, 176 slots.
None of the three accounts for the gap.

## 153. THE BACKGROUNDS: A CLAIM-AND-WIPE STALEMATE, AND THE WIPER IS THE COLOUR-SET RELOCATION (2026-09-10 20:50)

152 left one question: does a blank cell never claim a slot, or claim one
that something wipes? `MDALLOCWHY=1` (`mdalloc_ctr`, m_main.c) gives the
allocator its own 16 counters and answers it.

**First, where the block goes.** The first cut put it at 0x3A680 -- the
384 bytes the FBCLEAR comment calls spare between FBCLEAR and SLC. It
read back 0x01010101 at every slot. **That window is NOT free, and no
fixed scratch block here is boot-zeroed anyway.** The counters live in
.bss now and `tools/md_alloc_why.py` takes `mdalloc_ctr` out of
rom/s16.lst, the pattern MTASKWHY already uses. Anyone adding a counter
block: .bss, not the scratch map.

Line flags + MDALLOCWHY, per frame during the collapse (f2300-f3000,
tags pinned at 117-193 of 1024):

    free-way claims          3.2 / frame
    mdp_free_set tag wipes   3.2 / frame
    evictions                0.0 / frame   (total frozen since f1700)
    mds_flush                0.0 / frame   (3 calls in the whole run)
    mds_install wipes        0.0 / frame   (1 call, 1 tag)

**It is a stalemate, and the books close exactly.** f2300 -> f3000, 700
frames: claims +1598, free-set wipes +1522, net +76 -- and the resident
tag count goes 117 -> 193, +76. Not approximately. The allocator claims
three slots a frame and `mdp_free_set` destroys three a frame, so the MD
residency map cannot climb out of the hole a cut puts it in.

**The wiper is the COLOUR-SET RELOCATION** (m_main.c, `mdp_free_set`:
every md_tag entry whose set is being moved is invalidated, because the
pattern shipped to VRAM is pen-remapped per S16 colour set, so a set
that changes its MD CRAM line makes its tiles' bytes wrong). Rate: 37
calls over 2400 frames, mean 45 tags each. One relocation every ~65
frames, 45 resident tiles dead each time, against a refill of 3.2/frame
-- 14 frames of refill to undo, except the sets keep moving.

Corollary for 152's census: `blkdrt` (a cell blanked because its slot's
art has not shipped, outside cut mode) runs 0.4/frame before the cut and
4.6-9.6/frame after. Those are Mike's missing tiles, and every one of
them is downstream of the wipe, not of the uploader -- `md_dirty` is
still ~0 (152).

**THE LEVER, and it is the one the sky-allocator note already named:**
128 S16 colour sets compete for 4 MD CRAM lines by LRU, so sets churn
and take their tiles with them. Per-scene STATIC line assignment (the
`MD_STATIC` / `mds_install` baked tables) exists to stop exactly this,
and mds_install is firing ONCE in a 3000-frame run while free_set fires
37 times. Either the static install is not covering the sets that churn,
or something re-enters the dynamic path after it. That is the next
probe: log which csets mdp_free_set moves and whether the scene's baked
table names them.

NEGATIVE, so nobody re-runs them: mds_flush (3 calls) and mds_install
(1 call, 1 tag) are both innocent. Evictions are zero. Slot capacity is
not the problem -- 152 measured on-screen demand at 428-505 slots of
1024 with no set past 7 of 8 ways.

## 154. THE DRIFT FREE IS THE ONLY WIPER, AND TWO WAYS TO STOP IT BOTH FAIL (2026-09-10 21:05)

153 said the wiper is `mdp_free_set`. Splitting its three call sites
(`MDALLOCWHY` [16]/[17]/[18]) says which one, over 4000 frames:

    LRU eviction from a full line        0
    co-owner colour drift               64
    owner-vs-line_c drift                0

**Every single relocation is the co-owner drift**, and colour set 33
alone takes 44 of them. The path: two sets share a CRAM pen, the pen
displays the OWNER's live colour, and a co-owner whose own colour is
d^2 >= 18 away from what the pen shows is painting the wrong colour, so
the set is freed to force a re-assign. It then RE-MERGES ONTO THE SAME
ANIMATING PEN and drifts again, ~45 resident tiles dying each round.

**A/B that proves it owns the background misses** -- `DRIFTMEAS=1`
(count the drift, never free; colour is wrong by construction, it is a
measurement and not a ship):

    frame          2400   3000   4000
    tags, line      117    192    653
    tags, DRIFTMEAS 117    233    800
    blkdrt, line   3136   6986   7264      (cells blanked, art not shipped)
    blkdrt, DRIFTMEAS 1476 1604   1953     -- 3.7x fewer

NEGATIVE 1, `DRIFTVOL=1` on the SET. `mdp_claim_pen` has always
preferred an exclusive pen for a set with `mdp_s_vol >= 2`, and
**`mdp_s_vol` is never incremented anywhere** -- zeroed at boot and at
every scene install, read once, dead since it was written. Incrementing
it on the drift changes NOTHING: 366 burned claims, **363 of them with
no free pen**. MDP_LINES is 2-3, so 30-45 pens serve 128 colour sets and
sharing is not optional.

NEGATIVE 2, the same flag on the PEN. Mark the animating pen instead, so
sharing skips it and the nearest-colour fallback needs no free pen.
Relocations 64 -> 66. The re-assign finds another shared pen whose owner
also animates.

## 155. TAGKEEP: THE WIPE IS NEVER AVOIDABLE, BECAUSE THE RE-ASSIGN ALWAYS MOVES (2026-09-10 21:15)

A tile's shipped pattern depends on its set's (line, pixel->pen map) and
NOT on the pen colours, so `mdp_free_set`'s wipe of every md_tag entry
carrying the set is needed only if the re-assign actually moves it.
`TAGKEEP=1` defers the wipe, keeps the old line and map, and compares at
the re-assign.

**First cut measured 11 frees and 0 resolutions -- my own bug, and worth
recording because it is a trap in this design.** With the wipe deferred
the tags SURVIVE, so the cell HITS in md_tag and never reaches
`mdp_note_tile`, so the set is never re-assigned and its cells ship with
line 0. It scored like DRIFTMEAS because it WAS DRIFTMEAS. Fixed by
re-assigning at the top of the claim loop whenever the set has no line.

With that fixed, over 4000 frames:

    deferred wipes resolved IDENTICAL     0
    deferred wipes resolved MOVED        59

**Zero of 59.** The re-assign never lands on the same (line, map), so the
wipe is never avoidable and TAGKEEP is worth nothing. Residency returns
to the line's numbers exactly (tags 207/659/56, blkdrt 6792/7123/7558
against 192/653/56 and 6986/7264/7699).

Why it always moves: the free releases the set's pens, and the assign
then picks the line needing the fewest NEW pens. With 45 pens for 128
sets the packing is unstable, so the set lands somewhere else every time.

**Pixel A/B** (`attract_parity.py`, DRIFTMEAS-equivalent build vs the
line): logo rewrite 50/52 against 36/31 and logo red 88/41/88 against
72/29/73 -- WORSE, the litter the drift free exists to prevent. Demo
scene at k=45: 58 against 133 -- much better. It is a real trade
between attract-screen colour and gameplay backgrounds, not a win.

**WHERE THIS POINTS.** `mds_pin` already makes `mdp_free_set` a no-op
for a set the scene's baked table names, and pin-declines measured
**ZERO across a 4000-frame run** -- the baked table does not name a
single one of the 11 sets that churn. The per-scene tables are the
mechanism that was built for exactly this and they are not covering the
sets that need it. Next: read what `tools/palscene_bake.py` puts in a
scene's table and why set 33 is not in it.

## 156. THE WIPES DESTROY VISIBLE TILES: 95% OF THEM, MEASURED AT THE WIPE (2026-09-10 21:30)

I twice built a story on these counters that the next measurement
undercut, so this entry is the direct question asked at the right
instant. **At each tag wipe, is the slot named by the name table the
player is looking at right now?** (`MDALLOCWHY` [24], a scan of
md_dbg_nt inside the wipe loop.)

    over 4000 frames      drift frees            57
                          tags wiped          2,609
                          WIPED WHILE ON SCREEN 2,473   = 95%

That is the background defect, stated without inference: **the colour-set
drift free destroys about 2,500 tiles that are on the screen at the
moment it destroys them**, in 57 events, and the name-table walk refills
at ~3 slots a frame.

**A correction to my own reading in 153-155.** I checked residency for
the 11 churning colour sets at frames 600, 2400 and 3000 and found ZERO
resident slots for every one of them, and nearly concluded the frees were
harmless. Residency is time-varying and none of those three frames is at
a free; the sets hold ~46 slots each at the instant they are freed and
nothing by the time a round-numbered frame comes round. **Sampling a
time-varying quantity away from the event says nothing about the event.**
The same mistake in a different costume as this file's rule 4.

Set 33 is the noisy one (44 of 57 frees). ~~It is NOT the damaging one:
it has zero cells in the tilemap at every sampled frame.~~ **WRONG, and
corrected within the hour by the per-set version of this very counter --
I made the SAME sampling mistake AGAIN, two paragraphs after naming it.**
Ranking colour sets by on-screen tiles destroyed (`mdalloc_onscr`):

    set 33                              1,681   65%
    sets 43, 42, 41                     128 each
    set 38                                114
    sets 46, 45, 44, 40, 39, 37          64-77 each
    total                               2,576 over 11 sets

**Set 33 is two thirds of the whole defect.** The tilemap census that
said otherwise read one frame's 13 pages; the set's cells are on screen
when it is freed and gone by the round-numbered frame I dumped. Third
time in this arc. The rule is not "sample more frames", it is **count at
the event or do not count.**

**THE FIX IS THE ONE THE CODE ALREADY HAS, AND IT IS NOT WIRED.**
`mdp_free_set` returns early for a set the scene's baked table pins
(`mds_pin`), pin-declines measure ZERO across the run, and
`sh_src/pal_scenes_md.h` names 39 sets per scene -- set 33 and sets
38-46 are not among them. `tools/palharvest_tiles_ares.py` defaults to
frames 60..1900, which is attract only: the sets that churn are the ones
that come live AFTER the demo starts, so the bake has never seen them.
Re-harvesting to frame 6000 and re-baking is the change. The bake
partitions sets across 3 CRAM lines by exhaustive search with a hard
15-colour-per-line constraint and fails loudly if no partition exists,
so the risk is a loud failure, not a silent wrong table.

## 157. LAND WHERE YOU WERE: THE WIPES DROP 64% AND THE PIXELS DO NOT MOVE (2026-09-10 21:50)

155 measured that a drift-freed colour set never returns to the same
(line, pen map) -- 0 of 59 -- and never explained WHY. The reason is one
line in `mdp_note_tile`: it derives the re-assign's pixel mask from **ONE
tile's 64 pixels**, a subset of what the set had, so `mdp_assign_set`
re-packs against a smaller demand and lands somewhere else, then grows
through `mdp_extend_set`. It was never going to land where it was.

`TAGKEEP=1` now keeps the old placement instead of comparing to it: on
the re-assign, if the new mask is a SUBSET of the old, put the set back
on its old line with its old pen indices wherever those pens are free or
already hold the right colour, and hold the record open so every later
extend lands on the old map too. Pattern bytes identical -> the tags and
the picture survive. (My first cut required mask EQUALITY and the branch
was dead: 0 entries in 4000 frames. That dead branch is what pointed at
the subset.)

    over 4000 frames              line      TAGKEEP
    drift frees                    59          16
    tag wipes                   2,652         976
    WIPED WHILE ON SCREEN       2,500         897    -64%
    blkdrt (cells blanked)      7,308       2,509    -66%
    resident tags at f4000        617         745

**Pixels: NEUTRAL**, which is the point. `attract_parity.py` against the
arcade, mean |luma| per anchor:

    logo rewrite   line 36/31    TAGKEEP 33/33
    logo red       line 76/30/73 TAGKEEP 76/30/73
    demo scene     line 132/133  TAGKEEP 132/133

Unlike DRIFTMEAS (155: 50/52 and 88/41/88, visibly worse), this costs
nothing on the attract. Game logic identical at 48.7%.

**AND A NUMBER THAT LOOKS LIKE A REGRESSION AND IS NOT, read carefully
because this file has been fooled by its opposite twice.**
`presented_fps.py` over eight windows: line MOTION 5.9 fps, TAGKEEP 3.1.
Halved. But the flip counters over the same 4000 vints:

                     line    TAGKEEP
    isr-flips        1724       1729
    cadence          1.026      1.025
    cycles           3880       3884
    any-change fps    24.6       23.8

**The same number of frames reach the screen.** Flip delivery is
identical to within 0.3%, so the MOTION difference is entirely the SIZE
of each frame's delta, not how many frames arrive. With 64% fewer
on-screen tiles destroyed and 66% fewer blanked cells, what disappeared
from the >5%-delta count is large blocks of background flicking between
real art and blank. START-HERE rule 2 is "anim_rate counts CHANGE, not
correctness" and presented_fps was built to escape that; with the flip
count pinned, it does not escape it here.

I am NOT claiming the build is better because a metric went down. I am
claiming the metric cannot separate these two builds, and the three that
can -- tiles destroyed, cells blanked, and arcade pixel diff -- all
favour TAGKEEP or call it even.

    rom/night/vi12.32x = vi10's flags + TAGKEEP=1
    make ship-us FBXPORT=1 FBXSTAGE=1 FBXPEND=1 FBXISRLIFT=1 PGSKIPPKT=1 \
                 TEXTCAPMASTER=1 TEXTCAPFULL=1 GAMEGATE=1 TEXTCAPEARLY=1 \
                 TEXTCAPMASK=1 TAGKEEP=1

Not play-passed. Mike's eye decides whether the backgrounds are fuller,
and that is the only question it is meant to answer.

**Still open, and it is the majority of what remains.** The gain fades
late: at f6000 frees are back to 62 and on-screen wipes to 2,266. The
old placement is only reusable 4 times in 16-58 attempts, so most frees
still wipe. Set 33 alone is 65% of the damage (156) and CANNOT be fixed
by the per-scene bake: a wider harvest to frame 6000 on real play input
finds sets 37-46 but **never set 33 at all**, and the ten it does find
appear in only 4 samples each with the palette mid-fade, which the bake
rejects by design as a fade witness. **The static table cannot cover a
transient flash palette.** Whatever set 33 is, it is next.

## 158. WHAT SET 33 IS, AND HOLDING ITS PENS: 74% FEWER TILES DESTROYED, MORE FLIPS, BETTER PIXELS (2026-09-10 22:06)

**Identified at the event** (`MDALLOCWHY` arms a one-shot capture in
`mdp_free_set` for `MDA_WATCH`, default 33):

    colour set 33 (0x21), MD line 1, used mask FF
    pen map (pixel->pen)  10 13 13 13 13 15 7 7   -- only 4 DISTINCT pens
    tile codes            0x850 0x851 ... 0x857   -- eight CONSECUTIVE

Rendering those eight from `sh_src/tiles.bin`: a large solid mass with a
soft gradient edge, pens 7 down to 1. Eight consecutive codes, four
distinct colours, all eight pixel values in use. **It is a big terrain
or silhouette mass, and it FADES** -- which is why it drifts, and why it
is 65% of all on-screen tile destruction.

**Why 157 only rescued 4 frees of 16:** `mdp_free_set` releases the
set's CRAM pens, another set takes them, and the re-assign then cannot
go home. `PENHOLD=1` (needs TAGKEEP) keeps the refcount so the pens stay
reserved across the free/re-assign gap.

    over 4000-6000 frames        line    TAGKEEP   +PENHOLD
    drift frees @f6000             62        62        19
    old placement reused          n/a    4 of 16    8 of 16
    WIPED WHILE ON SCREEN       2,500       897       644     -74%
    blkdrt @f6000               8,037     5,483     3,050     -62%
    resident tags @f4000          617       745       800
    isr-flips / 3983 vints      1,724     1,729     1,845     +7%
    game logic                   48.7%     48.7%     48.9%

**Pixels, against the arcade** (`attract_parity.py`, mean |luma|):

    logo rewrite   line 36/31     vi13 35/35      even
    logo red       line 72/29/73  vi13 74/28/74   even
    demo scene k45 line 133       vi13 60         -55%

vi13 gets DRIFTMEAS's demo-scene gain (155: 58) **without** its
logo-screen regression (155: 50/52 and 88/41/88). TAGKEEP's late
degradation is also gone -- frees hold at 19 through f6000 where both
the line and TAGKEEP alone return to 62.

**On `presented_fps`: it still cannot separate these builds, and this
time the reason is visible in the data.** vi13 reads MOTION 10.6 against
the line's 5.9, but the windows are [4,3,59,4,8,5,0,2] against
[7,1,10,7,3,0,17,2] -- one scene-transition window carries it. Drop each
build's largest window and they are even (26 against 30). The metric
that DOES move monotonically here is isr-flips, and it says vi13 presents
7% more frames.

    rom/night/vi13.32x
    make ship-us FBXPORT=1 FBXSTAGE=1 FBXPEND=1 FBXISRLIFT=1 PGSKIPPKT=1 \
                 TEXTCAPMASTER=1 TEXTCAPFULL=1 GAMEGATE=1 TEXTCAPEARLY=1 \
                 TEXTCAPMASK=1 TAGKEEP=1 PENHOLD=1

**KNOWN DEBT, and it must be paid before this is a ship and not a
probe:** a set that is freed and NEVER re-assigned leaks its held pens.
Nothing releases them. Over a 6000-frame run this costs nothing
measurable (CRAM stays full, pixels are even or better), but a scene the
run does not reach could starve the line. The release path is a stamp on
the held pens and a sweep that frees any held longer than N windows --
`mds_install` already zeroes the tables at a scene change, which bounds
the leak to one scene.

Not play-passed. On the rig for Mike.

## 159. PAYING PENHOLD'S DEBT, AND WHAT IT COSTS (2026-09-10 22:16)

158 shipped a known leak: a set freed and never re-assigned holds its
CRAM pens for ever. Paid in the drift loop's own round-robin, which
already visits every set: a held set whose last assign is 24+ windows
old is not coming back, so release its pens and wipe the tags it still
claims.

    over 6000 frames        vi13 (leak)   vi14 (reclaimed)
    reclaims                      n/a            7
    WIPED WHILE ON SCREEN         644          644
    drift frees @f6000             19           19
    blkdrt @f6000               3,050        3,505
    isr-flips / 3983 vints      1,845        1,716
    demo-scene diff vs arcade      60           75

The leak was real but small -- 7 events in 6000 frames. Correctness is
worth the 7. It gives back the flip gain (back to the line's 1724) and
some of the pixel gain, and I am not going to read much into either: the
two builds differ by ~40 lines of code and START-HERE rule 1 says this
family of metric swings 18 points on layout alone.

**vi14 against the line, on the measures that separate them:**

    on-screen tiles destroyed   2,500 -> 644     -74%
    cells blanked, art unshipped 8,037 -> 3,505  -56%
    demo scene vs arcade          133 -> 75      -44%
    logo screens vs arcade      36/31 -> 35/35   even
    isr-flips                   1,724 -> 1,716   even
    game logic                   48.7% -> 48.9%  even

    rom/night/vi14.32x
    make ship-us FBXPORT=1 FBXSTAGE=1 FBXPEND=1 FBXISRLIFT=1 PGSKIPPKT=1 \
                 TEXTCAPMASTER=1 TEXTCAPFULL=1 GAMEGATE=1 TEXTCAPEARLY=1 \
                 TEXTCAPMASK=1 TAGKEEP=1 PENHOLD=1

Not play-passed. This is the candidate: it is the first build in this arc
that improves the background WITHOUT giving anything back on the attract
screens or the clock.

**What is still on the table.** Old placement is reused 8 times of 16, so
half the frees still wipe. The other half fail because the old pens are
genuinely gone -- taken by a set that needed them while this one was
away. Reserving them longer trades CRAM pressure for tile stability and
nobody has measured that curve.

## 160. THE DRIFT TOLERANCE SWEEP, AND A CONFOUND IN attract_parity's LADDER (2026-09-10 22:26)

The threshold at which a co-owner set is declared drifted and freed was
a bare `18` at both sites. It is now `DRIFTTOL=n`, because the free it
triggers destroys ~46 tile slots, 95% of them on screen (156).

    DRIFTTOL   frees@f6000   on-screen wipes   blkdrt@f6000
        18          19             644            3,505
        27          15             508            3,541
        40          15             522            3,232

27 is the knee: 21% fewer on-screen tiles destroyed than 18, and 40 buys
nothing further.

**But the pixel comparison is CONFOUNDED, and it touches numbers I
reported in 157-159.** `attract_parity.py` bisects an OFFSET per build
(the game's own display-blank event) and then walks a k-ladder of lag
columns. The OFFSET is NOT the same across these builds:

    line 166    vi12 182    vi13 195    vi14 195    vi15 177

A build that reaches the cut 18 frames earlier is running faster, and by
k=45 it has accumulated a different lag against the reference. So the
**high-k columns are not a clean cross-build quality metric** — the
"demo scene 133 -> 75 -> 60" ladder in 157-159 mixes picture quality
with speed. The LOW-k columns are the comparable ones, and there
DRIFTTOL=27 is the better build:

    logo rewrite k0/k8   line 36/31   vi14 35/35   vi15 21/20
    logo red k0/k5/k12   line 72/29/73  vi14 74/28/74  vi15 74/28/74

**What is unconfounded** — tiles destroyed and cells blanked — has vi15
ahead on the first and even on the second.

`rom/night/vi15.32x` = vi14 + `DRIFTTOL=27`. I have NOT swapped the rig:
vi14 is deployed and keeps the shipped drift tolerance, which is the
conservative choice for a play pass. If Mike's eye likes vi14's
backgrounds, vi15 is the next one to hand him, and the question it asks
is narrow: is the attract logo cleaner without anything else getting
worse.

## 161. THE SPEED QUESTION, MEASURED ON vi14: CAT1MD CUTS THE SLAVE 21% AND BUYS 1.6% OF FRAMES (2026-09-10 22:35)

Mike: "sprites don't feel on par with mame speed YET." START-HERE names
the next lever as "the slave's compose (cat1 tiles = 48%)". That figure
is RIGHT and the lever is nearly worthless. Both measured on vi14.

**Slave compose split** (PHASECENSUS, STB, per generation, play2 input):

                        vi14      +CAT1MD
    clear + sprites     0.575      0.576
    cat1 tiles          0.512      0.289     -43%
    slave total         1.088      0.864     -21%

cat1 is 47% of the slave's compose, exactly as entry 124 said. CAT1MD
removes 43% of it.

**What that buys:**

                        vi14      +CAT1MD
    generation wall     1.57 v     1.55 v
    ships               2042       2075      +1.6%
    single-vint ships    15%        19%
    echo phase          1.45       1.31
    mtask phase         1.45       1.48

**0.22 vints/gen off the slave moves the wall 0.02.** Nine percent
pass-through. The compose is not what the frame rate is waiting on.

**Why: ships are VINT-QUANTISED.** The period bins say 80% of ships take
2 vints and 15% take 1. A generation that finishes in 1.2 vints still
waits for the next vint, so it costs 2 and the flip lands at 30 Hz. The
bar is one generation per vint. We are at 1.55-1.62 and the slave's
ENTIRE remaining compose is 0.86, so 60 Hz needs ~0.6 vints/gen removed
from a budget where no single component is that big.

**The master is idle, confirmed independently** (`MTASKWHY=1`, which
entry 123 built and never ran on this line):

    mtask PHASE        1.41 vints/gen
    drain WORK         0.437 vints/gen      = 31% of the phase
    drain visits       1.7 /gen
    drains completed   1.00 /gen
    gate skips        26.0 /gen

Entry 123's open question -- "2.73 vints of build_maps WORK, or a short
drain spread thin?" -- answers **spread thin** on vi14: 0.44 vints of
work inside a 1.41-vint phase, completing once per generation in 1.7
visits. The master's accounted work matches entry 123's 0.44 exactly. The
26 gate skips are poll iterations inside the 6% of a vint past
`NAT_DRAIN_CUT` (11300 of 12052 ticks), not 26 lost vints.

**So neither processor is saturated on vi14.** Slave work 1.09, master
work 0.44, wall 1.57. The generation is longer than either CPU's work,
which means the cost is in the HANDOFFS between them, not in the
computing. That is where the next speed work goes, and it is not CAT1MD.

**This supersedes START-HERE's "the SLAVE is the saturated processor"**
(entry 124, measured on the pre-GAMEGATE single-buffered line). On vi14
the slave's own compose census says 1.09 vints of work in a 1.52-vint
echo phase: it is idle 28% of its own phase.

## 162. TWO SLAVE-UTILISATION INSTRUMENTS DISAGREE BY 5x — DO NOT BUILD ON EITHER YET (2026-09-10 22:42)

161 says the cost is in the handoffs, so the next question is how idle
the slave actually is. There are two counters and they do not agree.

    STB (s_main.c st_s, per-band compose sums)
        107,059,046 slave ticks / 4000 vints x 48,208  =  55%
        (= 1.088 vints per generation, entry 161's figure)

    SLAVE BUSY CENSUS (0x28C80, wraps the cmd dispatch)
        11,238,938 ticks / 2000 vints x 48,208         =  11.7%

Both are on vi14, same input, same build family. **A factor of five.**

The likely reason is scope, not a bug: the 0x28C80 wrap covers only the
`if (cmd & 0xF000)` dispatch in `s_main.c`, and under `NATIVE_FRAME` the
strip loop that calls `compose_sprites` / `compose_layer` lives in
`m_main.c` and is entered by both CPUs. If the slave does most of its
compose through that path the census never sees it. I have NOT proven
that, which is exactly why this entry exists.

**It matters because the two readings imply opposite plans.** The
census's own comment states the rule: "~100% = compute-bound (diets
help); well under = the wall is waiting/serialization and diets cannot
move it." At 55% diets are marginal; at 11.7% they are pointless and
every compose optimisation ever measured here was measuring noise.

161's conclusion stands on its own either way, because it rests on a
DIFFERENTIAL and not on a utilisation figure: CAT1MD removes 0.22
vints/gen of slave work and the wall moves 0.02. That measurement does
not care which counter is right.

**Chased, and NOT resolved. Recording the dead ends so nobody re-walks
them.** Measured both counters in ONE run of ONE build (PHASECENSUS on
the vi14 flags, play2 input, 4000 frames):

    0x28C80 busy census   0.484 vints/gen    24.6% of wall
    STB sum               1.088 vints/gen    55.5%

  - NOT cross-CPU contamination. `st_s(11)`/`st_s(12)` are in
    `slave_concurrent_k`, which is called only from `s_main.c`. The
    master never stamps STB.
  - NOT the queued-chain pull escaping the census window. Those calls
    sit after the census closes, but `QUEUED_CHAIN` is not in the build.
  - NOT the census's 16-bit truncation. It looked like the answer --
    the slave FRT is phi/8, so 65,536 ticks is 1.36 vints and the wall
    maxes at 5.3 -- but rewriting the accumulator wrap-safe moved it
    from 0.475 to 0.484. Commands are not running long enough to wrap.
    (Change reverted: it costs the shipping slave instructions and buys
    nothing.)

**What is left is that STB double-counts, and I have not found how.**
STB's stamps are strictly INSIDE the census window, so STB > census is
arithmetically impossible and one of them is lying. Treat BOTH as
suspect.

161's conclusion stands on its own either way, because it rests on a
DIFFERENTIAL and not on a utilisation figure: CAT1MD removes 0.22
vints/gen of slave work and the wall moves 0.02. That measurement does
not care which counter is right.

## 163. vi14 SOAK: THE GAIN HOLDS AT 12,000 FRAMES AND NOTHING DEGRADES (2026-09-10 22:50)

The play pass is a long session and `PENHOLD` touches the palette
allocator, so the risk is pen starvation or a wedge late in a run.
12,000 frames on play2 input, line vs vi14:

    transport            line      vi14
    isr-flips            5,887     5,875
    cadence              1.011     1.011
    skips                    0         0
    flip-late                0         0
    V-gate rejects        0.3%      0.3%
    68K handler mean      57.1      57.4 lines

    allocator            line      vi14
    drift frees            151        65      -57%
    tag wipes            6,235     2,499      -60%
    cells blanked       16,194     7,011      -57%
    pen reclaims           n/a        30

**The background gain holds** -- 60% fewer tiles destroyed at 12,000
frames against 74% at 6,000, so it decays a little but does not
disappear. **Nothing in the transport moves**: identical flips, zero
skips, zero late flips, same handler cost. The pen reclaim (159) fires
30 times in 12,000 frames and CRAM never starves.

One thing to watch on the rig: vi14's worst-case slave span is 78.9
lines against the line's 59.4. It costs no frame in ares (skips and
flip-late are both zero) but the FPGA has less headroom than ares at
every other span this arc has measured.

## 164. PENREPAINT, AND THE CANDIDATE IS vi16 (2026-09-10 22:52)

The sets that churn FADE (158), so when one comes back its colour no
longer matches the pen it left behind and 157's old-placement check
rejects it -- even when the pen is still exclusively ours and nobody
else is showing through it. **A tile's pattern bytes depend on the pen
INDEX, not the pen COLOUR.** `PENREPAINT=1` repaints an
exclusively-owned pen to the set's current colour and keeps the index.

**vi16 = vi14 + PENREPAINT. Everything below is at 12,000 frames on
play2 input, against the line (vi11b):**

                            line      vi14      vi16
    drift frees              151        65        58     -62%
    tag wipes              6,235     2,499     2,153     -65%
    cells blanked         16,194     7,011     6,372     -61%
    old placement reused     n/a   9 of 39  10 of 32
    isr-flips              5,887     5,875     5,915     +0.5%
    cadence                1.011     1.011     1.011
    skips / flip-late        0/0       0/0       0/0
    game logic              48.7%     48.7%     49.1%

    attract pixels vs the arcade (low-k, the comparable columns, 160)
    logo rewrite k0/k8     36/31     35/35     34/34
    logo red k0/k5/k12  72/29/73  74/28/74  74/27/74

**Two thirds of the background tile destruction is gone and nothing is
paid for it** -- pixels even or better, flips up slightly, logic up
slightly, no skips or late flips over 12,000 frames.

    rom/night/vi16.32x   ON THE RIG as probe.32x, md5 2f82be3b
    make ship-us FBXPORT=1 FBXSTAGE=1 FBXPEND=1 FBXISRLIFT=1 PGSKIPPKT=1 \
                 TEXTCAPMASTER=1 TEXTCAPFULL=1 GAMEGATE=1 TEXTCAPEARLY=1 \
                 TEXTCAPMASK=1 TAGKEEP=1 PENHOLD=1 PENREPAINT=1

Not play-passed. The question for Mike's eye is narrow and it is the one
he raised: **is there less missing tile data in the backgrounds.**

**What is left of the defect.** Old placement is still reused only 10
times of 32; the other 22 rejects are pens genuinely taken by another
set while this one was away, and reserving them harder trades CRAM
pressure for tile stability on a curve nobody has measured. At 6,000
frames the gain is 74% and at 12,000 it is 65%, so it decays slowly with
scene variety. `DRIFTTOL=27` (160) is an orthogonal 21% that costs
attract-screen colour nothing and is untested in play.

## 165. [RETRACTED BY 166 -- the writes DO reach the SH-2; it is a one-step phase offset, not missing data. The measurements below stand, the conclusion does not.] THE COLOUR CYCLER'S WRITES NEVER REACH THE SH-2 (2026-09-11 03:10)

The decompile thread asked whether the game's colour cycler is missing
from `PAL_DIRTY_SITES` (LOOP-DECOMPILE 42), then walked the question back
(43) because the queue drain is also absent and sprite palettes work.
**The walk-back is wrong. The original question was right.**

Diff the live 68K palette (WRAM 0xFF9000) against the SH-2 mirror
(SDRAM 0x27000), same frame, headless ares, vi16, play2 input:

    frame 2000     9 words differ, colour sets 19, 20, 21
    frame 4000    14 words differ, colour sets 19, 20, 21, 6
    frame 8000     7 words differ, colour set 19

    live[153]=4900   mirror[153]=4A00
    live[154]=4A00   mirror[154]=4B00      the mirror is one
    live[155]=4B00   mirror[155]=4C00      ROTATION STEP behind

Read the cycler's own slot table (WRAM 0xFFF300, 8 bytes/slot) at f4000:

    slot 0   set 19   active   countdown 1   script 0x91A70E
    slot 1   set 20   active   countdown 3   script 0x91A78E
    slot 2   set 21   active   countdown 3   script 0x91A78E

**The same three sets, from both ends.** Sets 19 and 20 carry 800 and 728
tilemap cells -- 4.1% and 3.7% of every non-empty cell in level 1. So
about 8% of the background renders one cycle step behind, permanently,
and a cycling palette cannot be seen in a still frame by definition. It
has passed every still gate this project has ever run.

Corrections to the decompile thread's reading, both from the
disassembly at 0x30B2: the store loop is FOUR `move.l (a0)+,(a1)+` =
16 bytes = **8 words = one whole colour set**, not six colours; and the
target is `0x840000 + (set & 127) * 16`, one set's line exactly.

**The third path is clean.** 0x3108 writes entries 32-47 = sets 4 and 5,
and neither set appears in the mismatch list at any frame. Its `lea
$840040,a1` IS in PAL_DIRTY_SITES and is working.

**Why the drain's absence proves nothing about the cycler:** the drain
writes SPRITE palettes, which the port delivers over the DREQ palette
packet, not the dirty bitmap. Tile colour sets come through the bitmap,
which is the path with the hole.

**The fix.** The structural rule is real -- a thunk rooted at a constant
`lea` cannot cover a runtime-computed target -- but a thunk RUNS at
runtime with the register loaded, so it can compute the dirty block
itself. Block = `(a1 - 0x840000) / 64` for 32-word PAL32 blocks. 0x30D0
(`movea.l (a5,2),a0`) is four bytes, exactly a `jsr abs.w`, and sits
after a1 is computed and before the stores.

## 166. CORRECTION TO 165: THE CYCLER'S WRITES DO REACH THE SH-2. IT IS A ONE-STEP PHASE OFFSET, NOT MISSING DATA (2026-09-11 03:35)

165 claimed the cycler's palette writes never reach the SH-2 mirror and
told the decompile thread its walk-back was wrong. **I was wrong, and
165's headline is retracted.** What is true is smaller and different.

The mechanism 165 blamed already exists: `PAL_THUNK_A` (patch_game.py,
site 0x30C2) is a RUNTIME thunk built for exactly this writer -- it
masks D0 to the set index, shifts to a 32-word block and calls `pmark`.
It is not missing from the dirty machinery at all. I found it after
writing 165, by reading the patcher instead of the game.

**The fix I then built does not fix anything.** `PALAPOST=1` adds a
second mark AFTER the cycler's four stores (site 0x3100, `lea 8(a5),a5`,
recovering the block from A1), on the theory that marking before the
stores loses a race with the consume. Thunk area 756 -> 780 bytes, so it
installs. Mismatches at f4000/f8000 go 14/7 -> 7/15. **Noise, not a
fix.** Flag stays default-off and is a NEGATIVE.

**What the memory actually shows.** Set 19 over five consecutive frames,
live (WRAM 0xFF9000+19*16) against mirror (SDRAM 0x27000+19*16):

    f8000  live  7FFF 4900 4A00 4B00 4C00 4D00 4E00 4F00
           mir   7FFF 4A00 4B00 4C00 4D00 4E00 4F00 4900
    f8001  live  7FFF 4E00 4F00 4900 4A00 4B00 4C00 4D00
           mir   7FFF 4F00 4900 4A00 4B00 4C00 4D00 4E00

The mirror is the live ramp **rotated by exactly one position**, at every
frame. It is not stale, not torn, and not equal to the previous frame's
live either (the live ramp advances TWO steps per frame, so a whole-frame
lag would show as a two-step rotation). The mirror is one CYCLER STEP
behind, which is half a frame.

**So the defect is a one-step phase offset in a colour animation** -- the
cycling band animates correctly, slightly out of phase with the arcade.
Not missing tile data, not a wrong palette, and nothing to do with what
Mike reported. It is a fidelity item worth recording and worth nobody's
night.

**The method error, and it is the one this arc keeps making.** I diffed
two snapshots of a quantity that changes every frame and read the
difference as staleness. A ramp that rotates twice per frame CANNOT agree
between two observers sampled at different points in the frame, so the
diff was always going to be non-zero and it proves nothing on its own.
The test that settled it was consecutive frames, which distinguishes a
lag from an offset from a freeze. **Diffing a moving target needs the
time axis, and I reached for the fix before I had it.**

Entry 165's measurements all stand -- the sets, the slot table, the cell
counts, the disassembly corrections (four `move.l`, 8 words, one whole
colour set). Its conclusion does not.

## 167. MIKE'S RIG VERDICT: THE BACKGROUNDS RENDER. NEW BUG: LEFTOVER ZEUS TEXT (2026-09-11 03:20)

vi16 on the MiSTer, Mike: **"backgrounds render - but... leftover text
from Zeus."** The screenshot shows level 1 with correct background, sky,
tombstones, grass and sprites, and two stale glyph groups burned into the
upper middle of the playfield: `FRO` at about screen row 5 and `OH` at
row 7 — remnants of the Zeus cut-scene text, still on the text layer
after the scene changed.

**The background arc (152-164) is DONE and vi16 is its result.** The
colour-set drift free was destroying ~2,500 on-screen tile slots per
4,000 frames; holding a set's pen indices across the free and repainting
its own pens took that to 644 and the picture with it.

**The new bug is the TEXT layer, not the tiles.** It is a different
subsystem (TEXTCAPMASK's 4-row groups, LOOP29 147) and the failure shape
says so: the stale rows are two SMALL GROUPS, not a region, and they
survive a scene change. TEXTCAPMASK ships only the groups a gated writer
marked, with a forced full mask every 8th vint as the backstop. A
clear-all that runs through an UNGATED path would clear WRAM and never
mark, so the master would keep re-shipping its stale copy — and 147
records that the clear-alls (0x369C, 0x1ACCA) "never" fire in the
traced window, so they have never been exercised in this design.
First suspect, not yet measured.

**Mike's priority, verbatim: "focus on parity in speed. and frames. do
not wait for me."** So the text bug is LOGGED AND PARKED here, with the
suspect written down, and this thread goes to the generation wall.

**A counter collision I made and am recording rather than leaving.** I
started a reject-reason census for the remaining old-placement rejects
and put it in `mdalloc_ctr[16..20]` — slots 154's free-site split
already uses. The reading came back "notsubset 65", which is the
drift-free count, not a reject reason. Reverted, unbuilt, and noted
because this log now has TWO counter-collision entries (152's DIAG arm
and this one) and the lesson did not take the first time: **a counter
block needs a written map before the second user, not after.**

## 168. THE WALL DECOMPOSED BY ABLATION: 60 Hz IS REACHABLE, IT IS A THRESHOLD AT 1.00 VINT, AND SPRITE COMPOSE IS THE MASS (2026-09-11 03:35)

Four builds, same flags but for the ablation, 4000 frames, play2 input.
Ablated builds render WRONG by construction; the only number they
produce that means anything is the generation wall.

    build                          wall     ships      single-vint
    vi16 (full)                    1.60v   2042 = 31.3fps    15%
    - master maps drain            1.31v   2219 = 34.2fps    27%
    - maps AND sprite compose      0.90v   3808 = 58.3fps    98%
    - maps, sprites AND cat1       0.45v   3808 = 58.7fps    99%

**58.3 fps with 98% single-vint ships.** The transport, the flip, the
DREQ, the window, the echo chain, the 68K handler -- all of it -- fits
inside one vint with room to spare. **The protocol floor is 0.45 to 0.90
vints. The pipeline is not the constraint and has not been the
constraint. The COMPUTE is.**

**THIS OVERTURNS MY OWN ENTRY 161**, which concluded "neither processor
is saturated, the cost is in the HANDOFFS between them, not in the
computing." That was reasoned from utilisation figures and it is wrong.
The handoffs cost 0.45-0.90 of a vint; the compute costs the other 0.7
to 1.15, and it is the compute that puts the wall over the line.

**AND IT EXPLAINS 161's NINE PERCENT PASS-THROUGH, which was the real
clue.** CAT1MD removes 0.22 v/gen and moves the frame rate 1.6%,
because the wall goes 1.57 -> 1.55 and **both are in the two-vint
bucket**. Ships are vint-quantised: a generation that finishes at 1.2
vints still costs 2 and flips at 30 Hz.

    THE TARGET IS A THRESHOLD, NOT A GRADIENT.
    Nothing is paid until the wall crosses BELOW 1.00 vint.
    Then everything is paid at once: 15% -> 98% single-vint.

Every compose diet measured in this log was measured against a gradient
that does not exist. That is why eight days of work moved numbers and
never moved the picture.

**The budget, and what has to come out.** Wall 1.60, need under 1.00, so
0.60+ v/gen must go. What is available, per generation:

    slave  clear + sprites    0.589      <- the mass
    slave  cat1 tiles         0.509      (CAT1MD takes 0.22 of it)
    master maps drain         0.44       (worth 0.29 of wall, measured)

Removing the maps drain entirely buys 0.29 of wall and 3 fps, so it
alone cannot do it. **Removing the sprite compose is what takes 34.2 to
58.3.** The sprite half is the lever, it is the largest single item, and
Mike named it himself (entry 124: "hardcoded map, larger baked sprites").
`SPR_BAKE` already exists and ships -- 389 baked records, 665.5 KB of a
768 KB blob -- so the question is what fraction of the 0.589 it already
covers and what the rest is doing.

That is the next measurement, and for the first time in this arc there is
a number to aim at that is known to pay: **wall < 1.00**.

## 169. THE NAME TABLE IS REBUILT 2,240 CELLS A GENERATION TO PRODUCE THE SAME ANSWER: 100% OF ROWS ARE SKIPPABLE (2026-09-11 03:50)

168 said the master's maps drain is worth 0.29 of the 1.60-vint wall.
This is what the drain is doing.

**The row clear is innocent, and I checked it first.** `NOCLEAR=1`
ablation (skip the per-row sbuf clear entirely): wall 1.60 -> 1.65,
ships 2042 -> 2031. It costs NOTHING -- the `DIRTY_ROW`/ROWLIVE skip
above it already handles it. (First cut of that ablation went into the
`DIRECT_FB` arm, which **is not in the shipping flags** -- dead code,
and the "identical numbers" it produced were real. Check the arm you
are ablating is the one that compiles.)

**The sprite bake is also already doing its job.** 75,793 hits against
2,243 misses over 4000 frames = **97.1%**, 37.1 hits and 1.1 misses per
generation. Extending the bake is worth 3%.

**So the maps drain is the name-table walk, and the walk is redundant.**
Diff the shipped mirror (`md_dbg_nt`, 2 planes x 28 rows x 40 cells)
between nearby frames of the same run:

    f3000 vs f3001    56 of 56 rows IDENTICAL
    f3000 vs f3002    56 of 56
    f3000 vs f3003    55 of 56
    f1500, f2200, f4500, f6000, all +2    56 of 56 every time

**Nothing changes.** And the reason is structural, not luck: `MD_BG`
scrolls with the VDP's own hscroll registers (`HS_SHIP`, hscr_thunks),
so a scroll does not rewrite the name table at all. `NT_WRAP` then
shifts the mirror by the scroll delta and ships only the newly exposed
column. The table changes when a new tile COLUMN enters (one of 40 per
8 pixels) or when the game writes the tilemap. The walk recomputes all
2,240 cells every generation regardless.

**Where the real flag combinations stand** (measured, 4000 frames):

    vi16                                    1.60v   31.3 fps
    CAT1MD + MDSPRTOP                       1.55v   32.1 fps
    NOMAPS + CAT1MD                         1.17v   36.8 fps
    NOMAPS + MDSPRTOP + CAT1MD              1.11v   39.0 fps
    NOMAPS + SPROBE (sprites off)           0.90v   58.3 fps

So a legitimate maps-skip plus CAT1MD plus MDSPRTOP lands about 1.11 --
**39 fps, and still the wrong side of the 1.00 threshold.** The last
0.12 has to come out of the 0.589 sprite compose, where the bake is
already at 97% and the MD offload claims ~1 record of 37.

**THE SKIP, and the four hazards I found reading the walk.** Per
(plane,row) keep the row's input key -- `vxr`, `vyr`, `pqb[0..3]` -- and
a tilemap generation counter bumped by `cap_page` whenever a page's
content changes. Skip the 42-column loop when both match. Hazards, all
of which a naive `continue` gets wrong:

  1. **`md_ref` must still be re-stamped** or the residency LRU evicts
     live tiles -- the walk is the re-stamp source and the code's own
     comment sizes the window at <= 9 windows. Re-stamp from the mirror
     row's 40 slots: 40 byte writes against 42 tilemap reads plus
     claims plus packing.
  2. **The `NT_WRAP` mirror shift must not be skipped** -- except that
     an unchanged row key implies `dc == 0`, so placing the skip AFTER
     the shift block makes this free.
  3. **`CAT1_PEND[row]` is cleared at the top of the column loop** and
     re-set per cell. Skipping must leave the previous value, so the
     skip has to land BEFORE that clear.
  4. **The span ship's bookkeeping** -- under `NT_WRAP` the ship is a
     changed-span diff, so an unchanged row ships nothing anyway; the
     `#else` arm's `*o++` per cell would break, but that arm is not in
     the shipping flags.

`NOMAPS=1` and `NOCLEAR=1` are committed as default-off ablations. They
render wrong by construction and exist only to produce the wall.

## 170. NTSKIP: THE WALK SKIPS 91% OF ROWS, AND vi20 IS FASTER AND CLEANER THAN vi16 ON BOTH AXES (2026-09-11 04:00)

`NTSKIP=1` implements 169's skip: per (plane,row) key of
(vxr, vyr, pq[0..2]) plus a tilemap generation `cap_page` bumps whenever
a page's content moves. All four of 169's hazards respected -- the skip
re-stamps `md_ref` from the mirror row, sits after the `NT_WRAP` shift
and before the `CAT1_PEND` clear.

    rows skipped 43,946   walked 4,158   =  91.4% skipped
    wall 1.60 -> 1.34, ships 2042 -> 2209

**And it broke the transport, which is the interesting part.** First
measurement:

    68K handler mean   58.3 -> 84.2 lines
    consume mean        5.7 -> 16.2
    nopost             315 -> 2,766 of 3,988
    isr-flips        1,724 -> 715

With the walk nearly free the master runs all eight cell phases in ONE
gap instead of eight, and floods the transport: the 68K cannot finish
its consumes inside the window and stops posting. **Making a producer
faster starved the consumer.** `NBUILD1=1` (one packet build per gap
instead of two) is the brake and recovers most of it: handler 60.5,
consume 10.6, nopost 630, isr-flips 1,638.

**The 58 fps ablation does NOT starve the 68K** (handler 59.5, consume
5.8, nopost 134), so this is an NTSKIP-specific throughput artefact and
not a wall that 60 Hz has to hit.

**vi20 = NTSKIP + NBUILD1 + MDSPRTOP.** Against vi16, everything
measured over the same runs:

                              vi16      vi20
    generation wall           1.60v     1.42v
    ships                  31.3fps   32.5fps
    isr-flips / 3982 vints    1,724     2,001    +16%
    68K handler mean            58.3      58.7
    consume mean                 5.7       7.7
    skips / flip-late            0/0       0/1
    on-screen tiles destroyed     644         1     over 12,000 frames
    cells blanked, art missing  6,372     4,603

**One on-screen tile destroyed in 12,000 frames.** Skipping the walk
also stabilises the residency, because a skipped row makes no claims and
no evictions -- the background fix and the speed fix compound instead of
fighting.

**CAT1MD ruins it again, third time.** `NTSKIP+NBUILD1+CAT1MD+MDSPRTOP`
reads 37.5 fps and 42% single-vint, the best speed of the night, and its
allocator numbers are on-screen destroyed 174 and blanked 15,196 against
vi20's 1 and 4,603. Same trade as vi17: CAT1MD pushes every cat-1 colour
set into the MD residency map and the churn comes back. **Do not stack
CAT1MD without reading the allocator counters** -- this is the third
build where it looked like a speed win and was a background regression.

    rom/night/vi20.32x   ON THE RIG as vi20.32x and probe.32x
                         md5 20ef3c94
    make ship-us FBXPORT=1 FBXSTAGE=1 FBXPEND=1 FBXISRLIFT=1 PGSKIPPKT=1 \
                 TEXTCAPMASTER=1 TEXTCAPFULL=1 GAMEGATE=1 TEXTCAPEARLY=1 \
                 TEXTCAPMASK=1 TAGKEEP=1 PENHOLD=1 PENREPAINT=1 \
                 NTSKIP=1 NBUILD1=1 MDSPRTOP=1

**CAVEAT, and it needs Mike's eye rather than another ares run:**
`attract_parity.py` produced NO comparisons for vi20 -- every anchor
column is a dash and the OFFSET bisect returned 122 against vi16's 195.
vi20 reaches the game's display-blank cut 73 frames earlier, which is
consistent with it running faster, but it means the attract oracle
cannot align it and I have no pixel gate for this build. Direct frames
at f2400 render normally (97 distinct colours, 7.6% black against vi16's
110 and 4.8%, and the two are at different game moments because vi20 is
ahead). **Fixing the parity rig's alignment for a faster build is the
next tooling job.**

## 171. THE SKIP NEEDS TWO BACKSTOPS, AND THE ZEUS TEXT IS NOW THE OPEN BUG (2026-09-11 13:45)

**Mike's rig pass on vi20: corrupt blocks across the upper half of the
Zeus screen and dropped glyphs** -- against ONE destroyed on-screen tile
in 12,000 ares frames. The divergence is the bug, not the ares number.
`NT_SKIP` trusts `cap_page`'s content compare to notice every tilemap
change, and this repo's own notes have FB reads coming back stale on the
FPGA where ares reads them true. A skip that never expires, fed by a
change signal that can be lost, serves the previous scene for ever.

Two backstops, both cheap:

  - `NT_MAXAGE` (default 8): a row is walked regardless once it has gone
    that many windows unwalked. Same shape as TEXTCAPMASK's forced-full
    mask every 8th vint.
  - **bump the tilemap generation on display-blank entry.** A scene cut
    rewrites the whole tilemap; the display gate is the one signal that
    says everything is stale, so spend it.

**They did not just fix correctness, they IMPROVED the transport:**

                          vi16    vi20     vi21     vi22
    isr-flips            1,724   2,001    2,113    2,095
    68K handler mean      58.3    58.7     54.0     53.1
    consume mean           5.7     7.7      5.6      5.5
    nopost                 315     583       63       70
    fallback               663     732      138      137
    ships                31.3f   32.5f    33.1f       -

vi20's unbounded skip was flooding the transport in a way `NBUILD1`
only half-braked; bounding it fixed the rest. **vi22 -- NBUILD1 +
MDSPRTOP with NO NTSKIP at all -- reads within noise of vi21 on every
transport number**, which says most of the transport win was NBUILD1 and
the age bound, not the skip. And vi21's allocator numbers are WORSE than
vi16's (on-screen destroyed 1,347 against 644), because a forced walk
makes claims again.

So NTSKIP's 0.18 of wall is not currently worth its risk, and **vi22 is
the conservative build**: vi16's background behaviour, +21% flips, 68K
handler BETTER than baseline.

**Mike's pass on the next build: "MUCH better"** -- backgrounds clean on
the Zeus screen, temple and columns and grass all correct. **The
remaining defect is the ZEUS TEXT**, and he notes it has not worked
across the last several builds: the cut-scene lines render as scattered
single letters (`C A Y` / `A` / `S F R U` where the arcade reads
"RISE FROM YOUR GRAVE"). Entry 167 logged this and parked it; it is now
the open bug.

**Suspect, unchanged from 167 and now the thing being tested:**
`TEXTCAPMASK` ships only the 4-row groups a gated writer marked. A
cut-scene writer that is not in the FM-gate thunk table writes WRAM and
never marks, so only the rows caught by the forced-full mask every 8th
vint arrive -- which is exactly "some letters, most missing, stable
across builds".

**I could not reproduce it in ares** and that is worth recording: at
every frame sampled from 200 to 1400 with `play2` input the 68K text
shadow holds 5-6 live cells, so none of the available inputs reaches the
Zeus intro. The shadow-vs-capture diff I built for it is also reading
rows 0-3 and 24-26 as permanently mismatched, which is the HUD and which
UPDATES -- the same moving-target trap as 166, so those 38 mismatches
mean nothing on their own.

A/B on the rig instead, one flag apart:

    vi22.32x               TEXTCAPMASK ON   (the mask is suspect)
    vi23_notextmask.32x    TEXTCAPMASK OFF

If the Zeus lines render on vi23 and not vi22, the mask is dropping an
ungated writer and the fix is a thunk for it. If both are broken, the
mask is innocent and the writer never reaches WRAM in the first place.

## 172. THE PACKET BRAKE IS MANDATORY ON SILICON, AND ares UNDER-MODELS THE CONSUMER BY TWO ORDERS OF MAGNITUDE (2026-09-11 14:10)

Mike's rig, three builds one flag apart:

    vi25  = vi16 - TEXTCAPMASK                 "a slideshow, 1 frame / 3 seconds"
    vi24  = vi25 + MDSPRTOP                    "same story, unplayable"
    vi23  = vi25 + MDSPRTOP + NBUILD1          "frames working again"

**`NBUILD1` -- one MD packet build per gap instead of two -- is the
difference between 0.33 fps and playable. On the hardware it is not an
optimisation, it is a requirement.**

ares says the opposite, or near enough to have fooled me: isr-flips 1,869
for vi25 against 2,065 for vi23, a 10% gap, no hint of a cliff. **On
silicon the same change is 100x.** I removed the brake on 171's reasoning
that "most of the transport win was NBUILD1 and the age bound, not the
skip" and that it cost backgrounds -- both true in ares, both irrelevant
next to this.

**Why two builds per gap falls off a cliff.** `md_nbuild` is how many MD
packets the master PREPARES per gap. Each one has to be CONSUMED by the
68K as a VDP DMA inside its vint. Queue faster than the 68K can drain and
the handler overruns its window, the post is missed, the flip is
declined, and the screen stops updating -- exactly the 2,766-missed-posts
signature NTSKIP produced in 170. The brake does not gate graphics; it
matches the producer to the consumer. **Past the consumer's rate, more
packets deliver FEWER frames.** ares' modelled consumer is fast enough
that the second build nearly fits; the FPGA's is not, and the cliff is
between them.

**Standing consequence: `NBUILD1=1` belongs in the ship line and any
build handed to the rig without it is invalid.** And every transport
A/B in this log measured in ares alone should be read as a ranking of
ares' consumer, not ours (CLAUDE.md says this about MAME; it is now
measured about ares too).

**And it reopens NTSKIP.** The skip needs the brake to be safe, and the
brake is now compulsory, so 171's "not currently worth its risk" no
longer applies. `rom/night/vi26.32x` = vi23's flags + the bounded skip
(NTMAXAGE=8 + the display-gate invalidate):

    ships 2148 = 33.0 fps, wall 1.44v
    68K handler 52.2 lines  (vi16 58.3)
    isr-flips 2,095, nopost 69, fallback 136, skips 0, flip-late 0

Best 68K handler mean of the whole arc. On the rig as probe.32x.

**A process fix, because this cost Mike three launches.** I deployed
vi22-vi25 under their own names and left `probe.32x` pointing at vi20 --
the build with the stale-skip corruption -- so the file he launches by
habit was three builds stale. probe.32x now tracks whatever build is
being asked about, and the name goes in the message.

## 173. THE IRQ4 GATE INVENTORY: SIX WORKERS RUN EVERY VINT WHILE OUR GATE RELEASES THE GAME EVERY SECOND VINT (2026-09-11 14:20)

The decompile thread named all six functions IRQ4 calls (LOOP-DECOMPILE
39, 41, and the sky path in 43). That list is worth re-reading from the
port's side, because `GAMEGATE` changed what a vint MEANS here: IRQ4
still fires 60 times a second and still runs all six, but it only
releases the main loop once per PRESENTED frame, which is currently
every second vint. **Anything IRQ4 does per vint now runs at twice the
game's rate.**

    0x2DBC  palette queue DRAIN     14 words per entry, drains what the
                                    game queued. Faster than the filler:
                                    harmless.
    0x2E50  sound byte streamer     one byte per vint from a ring at
                                    0xFFF040 to the sound latch 0xFFF0C4,
                                    count at 0xFFF03C. Drain-faster-than-
                                    fill again: harmless.
    0x2E74  coin/service EDGES      masks bits 0,1,3 and calls the credit
                                    routines INSIDE the interrupt
                                    (0x2FBA, 0x3036, 0x3044). CLEARED as
                                    an input-loss suspect: the edges never
                                    wait for the main loop, so our gate
                                    cannot drop a coin. Gameplay input is
                                    read by the main loop per game frame
                                    and is unaffected.
    0x30B2  colour cycler           per-slot countdowns tick PER VINT.
                                    See below.
    0x3108  per-scene sky block     16 words into colour entries 32-47
                                    EVERY VINT from a static per-scene
                                    rom table at 0x32AE. See below.
    0x3128  per-vint state machine  gated on 0xFFF026 bit 0, dispatches on
                                    0xFFF028/29 into 0xFFFFE000. Not yet
                                    named; no port claim either way.

**TWO of the six are worth acting on.**

**0x3108, the sky block, is a redundant write we pay for.** It copies
eight longs from `0x32AE + scene*32` into palette entries 32-47 on every
single vint, and those bytes cannot change unless the scene index at
0xFFF142 changes. Its `lea $840040,a1` IS in `PAL_DIRTY_SITES`
(LOOP-DECOMPILE 43 confirmed it, and LOOP29 166 confirmed sets 4 and 5
never go stale), so the thunk marks that block dirty 60 times a second
for ever. The game's own gate is patchable here: compare 0xFFF142
against a saved copy and skip the routine when it matches.
**NOT YET MEASURED -- `PAL_DELTA` is in the ship flags, so the SH-2 may
already be comparing those 32 words away to nothing, in which case the
cost is one compare per vint and not worth a patch.** Measure the
palette packet's per-vint word count with and without the mark before
building anything. (This is the entry-166 lesson: I nearly built the
fix before checking whether the mechanism it assumed was even absent.)

**0x30B2, the colour cycler, runs at twice the game's rate.** Its
countdowns tick per vint; the game advances per presented frame. In the
arcade both are 60 Hz and the cycle is in step with gameplay. Here the
game is at ~30 and the cycler is at 60, so **every glow and fade
animates twice as fast relative to the action as the arcade does.**
Motion-only, so no still-frame gate can see it -- the same family as the
CAT1MD shimmer and the Zeus text.

This also explains a number I could not account for in 166: I measured
set 19's ramp advancing TWO steps per frame, which is what a per-vint
cycler looks like sampled per game frame.

It is an accuracy defect under standing rule 1, and it SELF-RESOLVES the
moment the generation wall goes under 1.00 vint and the game runs at 60.
So it is an argument for the threshold work, not a separate fix — unless
Mike's eye calls it out first, in which case gating the countdown to the
presented frame is a two-instruction change.

## 174. PLAN-TILES-TO-VDP STEP 2: THE BAKER'S INPUT IS FIXED AND SCENE 0 PACKS, RE-DERIVED (2026-09-11 16:20)

The decompile thread's `docs/handoff/PLAN-TILES-TO-VDP.md` is the right
target and **my own numbers agree with its arithmetic independently**:
slave compose is 1.088 v/gen split 0.589 sprites and 0.509 cat1 tiles
(LOOP29 161), the generation wall is 1.44, and 1.44 - 0.509 = 0.93 --
**the wrong side of nothing, the right side of the one-vint quantum
(168).** Moving the tile half to the VDP is the first change all night
whose arithmetic reaches the threshold.

**Step 1 is effectively already passed.** The plan's step 1 -- set plane
A's priority bit on cat1 tiles, background on plane B, sprites low, with
software compose still running underneath -- is what `CAT1_MD` already
does (m_main.c: `ent |= 0x8000` for `isfg && (w & 0x8000)`, with the FB
pass restricted to sprite rows by ROWLIVE). LOOP29 151 made the two
renderers pixel-identical by painting the FB's cat-1 cells with the MD
line's quantised colours, and Mike's rig pass on that build (vi11)
reported no shimmer. **The layering was never the problem.**

**And the plan explains the thing I could not fix all night.** CAT1MD
"works" and still wrecks the backgrounds every time -- 174 on-screen
tiles destroyed and 15,196 blanked cells against vi16's 1 and 4,603
(LOOP29 170), three builds in a row. The reason is now obvious:
**CAT1MD pushes every cat-1 colour set into the DYNAMIC md_tag
allocator**, and 153-164 is the story of that allocator churning. A
BAKED STATIC four-line CRAM assignment has no allocator to churn. The
palette bake is not a detail of the plan, it is the fix for the defect
that killed CAT1MD twice.

**Step 2, the baker's input.** `tools/bake_tilecram.py` read colours from
the rom block at `0x232A0 + blk*0x400`, which holds 64 palettes of 16
bytes, so `base + p*16` is out of range for p >= 64. The plan flags this
for scenes 1-4; **it hits scene 0 too, whose viewport uses palettes
72-103.** Added `--live FILE... --live-scene N`: colours come from the
UNION of live WRAM 0xFF9000 dumps, which covers every colour-cycler state
sampled. Six dumps from vi26 at f2000-f3000, 200 apart, in
`discover/palscenes/live/`.

    scene 0: 25 palettes, lines [15, 15, 11, 5], 46 of 60 slots

**Re-derived, not trusted: exactly the plan's claim.** Step 2's kill
condition -- "scene 0 stops packing into four lines once the colours are
read correctly" -- is NOT met. `sh_src/tilecram.bin` (5 x 4 x 16 words)
and `tilecram.h` (per-palette line + 7-pen slot map) are emitted.

Scenes 1-4 still print, and their numbers are still WRONG -- they use the
rom source above palette 63. Scene 4 reads 57 of 60 slots even on the
optimistic input, so it is the one that may not pack at all. They need
their own live dumps, which needs a playthrough that reaches them, which
is the plan's step 5.

**Step 3 is the build**: install the four baked lines into MD CRAM,
rewrite tile pen values through the map so a tile indexes its assigned
line directly, and stop the FB cat-1 pass. The number to read is
**percentage of single-vint frames** -- 22% on vi26 -- and not fps.

## 175. STEP 3 MEASURED: THE TILE HALF LEAVES COMPOSE, 20% -> 40% SINGLE-VINT, AND THE PER-PIXEL PRIORITY BIT IS THE MISSING PIECE (2026-09-11 16:25)

**First, a hardware fact that changes the plan's implementation.** The
plan's priority table puts sprites at rank 4, between plane A HIGH
(cat1) and plane A LOW (cat0). The 32X cannot interleave with the MD's
internal priority resolution -- but it does not have to, because
**the 32X layer's priority is PER PIXEL**:

    srcref/S32X_MiSTer/rtl/32X/VDP.sv:506
    assign YSO_N = !MODE ? 1'b1 : ~(PRI ^ PIX_COLOR[15]) & YS_N_SYNC;

`PIX_COLOR[15]` is bit 15 of the 32X PALETTE ENTRY for that pixel, XORed
with the global PRI bit. **Our port already relies on this** -- m_main.c
3488: "bit 15 of the CRAM ENTRY, not pixel index == 0", and `cram[0] =
0x8000` is what makes an unwritten FB pixel show the MD.

So cat1 does NOT need drawing over sprites. **A sprite pixel merely
needs SUPPRESSING where a cat-1 cell covers it**, and the MD's plane A
HIGH shows through the hole. That replaces 0.509 v/gen of tile drawing
with a per-cell bit test against a bitmap that is already baked and
static (`sh_src/cat1map.bin`, verified 20480/20480).

**Step 3's measurement.** `C1NOFB=1` deletes the FB cat-1 pass outright
(needs CAT1MD). Same flags otherwise, 4000 frames, play2:

    build                        wall    ships   SINGLE-VINT
    vi26                         1.46v   32.8f       20%
    + CAT1MD                     1.29v   35.5f       35%
    + CAT1MD + C1NOFB            1.12v   37.1f       40%

**Step 3's kill condition -- "the percentage does not move" -- is NOT
met. It doubled.** Slave compose falls 1.088 -> 0.597 v/gen, which is
the 0.49 the cat1 pass was costing, matching 161's 0.509 to within
measurement.

**We are at 1.12 and the quantum is 1.00.** The plan projected 0.77; the
difference is that the slave and master phases overlap, so removing
0.49 of slave work moved the wall 0.34. What is left, per generation:

    slave  clear + sprites + text   0.597
    master maps drain               ~0.44
    echo phase                      1.02   <- slave works 0.597 of it
    mtask phase                     0.96
    wall                            1.12

**0.42 of the echo phase is the slave NOT WORKING** -- waiting for its
launch, parked, or in the handoff. That is where the last 0.12 is
cheapest to find, and it is not more compute removal.

    rom/night/vi27.32x  ON THE RIG as vi27.32x and probe.32x, md5 2cf7569c
    isr-flips 2,418 (vi16 1,724, vi26 2,095) -- best of the arc
    68K handler 53.1, consume 4.6 (BETTER than vi16's 58.3 / 5.7)
    nopost 56, fallback 132, skips 0, flip-late 0

**KNOWN ARTEFACT, and it is why this is not the ship: sprites wrongly
cover cat-1 tiles where they overlap.** The FB writes a sprite pixel,
the 32X layer wins per pixel, and the MD's cat1 is hidden. On level 1
cat-1 is 11.3% of tiles and sprite area is small, so the overlap is
rare -- a sprite briefly in front of a fence it should be behind. Mike's
eye is the right instrument for whether that is visible at all before I
spend the sprite loop's budget masking it.

## 176. THE ALLOCATOR HAS TO GO, NOT GET MORE ROOM — AND TWO BUGS I MADE PROVING IT (2026-09-11 16:45)

Mike on vi27: **"very erratic. smooth frames, then choppy"**, with rig
shots showing large red and yellow blocks over the smoke-cloud sprite
and a full-height dithered yellow band.

**The erratic cadence is not a defect, it is what 42% single-vint LOOKS
like.** The ship period bins are (1038, 1394, 15, 7): 42% of generations
land in one vint and 57% in two. That is a 60 Hz frame followed by a
30 Hz frame, alternating on content — judder by construction. It reads
worse than a steady 30 even though it is measurably faster. **There is
no intermediate that feels good; the bins have to go to ~100% single.**

**The corruption is the palette allocator, and the fix is not more
lines.** I gave the dynamic allocator the fourth CRAM line that MDSPR
was holding (`MDSPROFF=1 MDLINES4=1` -- entry 133's "3 background + 1
sprite" stops applying once sprites stay in the framebuffer, which is
what the plan's four-line pack assumes):

    build                             on-screen tiles destroyed / 12k frames
    vi16                                     644
    vi26                                   1,347
    vi27 (CAT1MD + C1NOFB, 3 lines)        ~2,000
    + 4th line, dynamic allocator          9,845      <-- WORSE
    + 4-line STATIC bake                   3,620

**A fourth line made it FIFTEEN TIMES worse.** More room does not calm a
thrashing allocator; it gives it more to thrash. The static bake helps
(9,845 -> 3,620) and is still 5x vi16, because the bake's 39 sets come
from a HARVEST of live 32X CRAM groups and the cat-1 sets are not in it
-- under CAT1MD they live on the MD and never hold a 32X group. Those
sets stay dynamic and churn.

**So step 3's remaining work is the emitter, not the runtime.**
`mds_install` already installs baked per-scene line/map/used tables and
PINS every set in them against the drift free (LOOP29 156's `mds_pin`).
It needs tables whose set list is `bake_tilecram.py`'s worst-case
VIEWPORT -- exhaustive over scroll positions, 25 palettes for scene 0 --
instead of `mdpen_bake.py`'s sampled harvest. That is a tool change with
no runtime change at all.

**Two bugs of my own, both found by checking rather than by symptom:**

  1. **`MDSPROFF=1` did nothing at all** for its first two measurements.
     I put the `filter-out` mid-Makefile, and `-DMD_SPR` is appended
     LATER, so it filtered an empty list. The two builds I compared with
     and without it were byte-identical in flags. Moved to after every
     `SHCCFLAGS` assignment; now `grep -c MD_SPR` reads 0.
  2. **`mdpen_bake.py` emitted `mds_line_c[N][48]` hardcoded**, so
     `MDPEN_LINES=4` wrote a four-line table into a three-line
     declaration and the fourth line was silently truncated. Sized by
     the line count now.
  3. **And one in my own patch from 174:** the live-colour path built
     the pen map from `sorted(live[p])` -- a SET -- so the emitted map
     was in colour order, not pixel order. Every tile would have indexed
     the wrong slots. `tilecram.h` is re-emitted from a per-pixel list.

All three are the same shape as this log's recurring failure: a
mechanism that looks applied and is not. The counter-collision entries
(152, 167) and the dead-code ablation (169) are the others.

**Where the speed stands.** vi27: wall 1.13, 42% single-vint, isr-flips
2,418, 68K handler 53.1 -- the best transport of the arc, and NOT
shippable until the allocator is out of the cat-1 path.

## 177. NTSKIP IS A DEAD END, AND ITS "91.4% SKIPPED" WAS A COUNTER COLLISION (2026-09-11 18:20)

Following the decompile thread's advice on my three bugs -- "every new
emitter should print what it emitted and fail loudly on a count it did
not expect" -- I went back and read NTSKIP's own counter from an address
I had checked against the scratch map. It had never been checked.

**`NTS` was at 0x28FD0. The band-deferral block is at 0x28FC8, three
longs, ending 0x28FD4. NTS[0] overlapped its third long.** The
"91.4% of rows skipped" in entry 170 was the skip count plus band-2
deferrals. Moved to 0x28FD8 and the scratch map from 0x28F80 to 0x29000
is now written into the source above the define, because this log has
three collision entries (152, 167, 176) and every one was a counter
landing on a live block.

Read correctly, on the same build:

    rows skipped 0   walked 23,800   ->  0.0%

**The flag has been doing nothing.** Two causes, both mine:

  1. **`tm_gen` was global.** `cap_page` bumps it when ANY page's content
     moves, and the game streams tilemap columns continuously while
     scrolling, so every generation invalidated every row. Fixed to
     per-page generations folded into the row key, so a write to a page
     a row does not read costs that row nothing.
  2. **The age bound could never be satisfied.** A row is revisited once
     per full eight-phase walk, and `win_no` advances per window, so the
     age at revisit is 8-16 and `NT_MAXAGE=8` rejected every row. With
     both fixed:

    NTMAXAGE=64    63.9% skipped     21% single-vint
    NTMAXAGE=200   67.5% skipped     21% single-vint
    (zero skips)    0.0% skipped     20% single-vint

**Skipping two thirds of the name-table walk buys ONE POINT. NTSKIP is
dead and comes out of the line.**

**And that corrects my attribution for three builds.** vi20's 1.42 wall,
vi21's and vi26's transport numbers were all credited partly to NTSKIP.
The skip was inert in vi21 and vi26 (age bound) so those gains were
NBUILD1 + MDSPRTOP alone -- which is exactly why vi22, built with no
NTSKIP at all, measured within noise of vi21 (entry 171 noticed the
coincidence and drew the wrong conclusion from it). vi20's skip was
UNBOUNDED, so it did fire, and that is the corruption Mike saw.

**The maps drain is still the real 0.44, and I had the wrong function.**
`NOMAPS=1` ablates `build_maps_chunk`; the cell walk NTSKIP targets is
the packet-build path inside the window. Two different pieces. The
ablation's 0.29 of wall belongs to `build_maps_chunk` and **nothing has
touched it yet.**

**Live from the decompile thread, both unattempted:**

  - **The hole punch is cheaper than I costed it.** Four scenes of five
    have no partial cat1 row, so the test is one screen-row compare with
    no bitmap. Per cell, a cat1 cell whose tile is BLANK needs no hole at
    all -- 1,204 of scene 1's 1,792. 68% of scene 0's cells and 82% of
    scene 1's resolve per cell. **The sprite loop wants two bits per
    cell, not one.**
  - **The game already knows when it did not advance.** On an overrun it
    increments a counter and takes a path writing no scroll and no
    sprite upload, so the composed output is IDENTICAL to the previous
    frame. Our generations run at 0.62/vint against the game's 0.50, so
    a fifth of them recompose an unchanged frame. Detecting it is a byte
    compare over the staged sprite list and the latched regs. It makes
    the frames either side free, which is where a bimodal 42/57 split
    hurts most.

## 178. THE NO-ADVANCE DETECTOR AND GAMEGATE ARE MUTUALLY EXCLUSIVE (2026-09-11 18:30)

Built the decompile thread's judder lever: `GENSKIP=1` refuses to launch
a generation whose input is identical to the last one launched. Hash is
the staged sprite list (256 longs), the latched layer regs (42) and the
per-page tilemap generations (16), with `GEN_MAXAGE=8` forcing a launch
every 8 windows regardless.

**It fires exactly as predicted and makes everything worse:**

                          vi26-family    + GENSKIP
    launch attempts skipped      -          58.4%
    ships                    32.8 fps     18.7 fps
    isr-flips                   2,095        1,190
    single-vint                   20%           3%
    GAME FRAMES / vints         49.7%        35.3%

**The last row is the proof and it is the whole story: the GAME ADVANCED
LESS.** The detector cannot have skipped only redundant frames, because
a redundant frame does not change how fast the game runs.

**Why: `GAMEGATE` ties the game's frame release to our flip** (entry
141 -- IRQ4 releases the main loop once per PRESENTED frame). So a
skipped generation withholds the game's release, the game does not
advance, its sprite staging stays identical, and the next window skips
too. It is a feedback loop: fewer generations -> fewer releases -> fewer
game frames -> more skips.

**The thread's premise is right and it is the arcade's premise: there,
the game advances on vblank REGARDLESS of what the display is doing, so
a vint the game skipped is genuinely free. Under our gate the game only
advances when we present, so "nothing changed" and "we did not let
anything change" are indistinguishable from the SH-2 side.**

**The fix is to separate the two things `GAMEGATE` currently fuses.**
Release the game's frame WITHOUT composing:

    release := (a generation was presented) OR (a generation was skipped
               because its input was identical)

Then the game runs at 60 Hz logic, its staging moves every vint, and we
compose only the frames that actually differ. That decouples the loop and
is strictly better than either half -- but it is a change to the gate
PROTOCOL on the MD side (the release thunk), not an SH-2 change, and the
SH-2 has to tell the 68K "skipped, advance anyway".

`GENSKIP` stays default-off and is a NEGATIVE in its current form. The
hash and the skip mechanics work; the protocol under them does not.

**Also worth stating for the next reader: the hash source was ALSO
wrong** and would have needed fixing even without this. `SPR_SNAP` is
OUR snapshot, refreshed on our schedule by `text_capture`, not the
game's staging. Hashing it asks "has our copy changed", which is not the
question. The game's FB staging is the right source.
