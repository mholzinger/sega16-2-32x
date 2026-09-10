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
