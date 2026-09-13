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

## 179. THE REDUNDANT GENERATIONS DO NOT EXIST — CLOSED, WITH THE PROTOCOL LOOP BROKEN FIRST (2026-09-11 18:40)

178 said `GENSKIP` could not work because `GAMEGATE` fuses "presented"
with "released". **Both halves are now settled.**

**The protocol loop IS breakable, and cheaply.** No MD-side change was
needed -- the gate already has `GAMEGATE_MAXWAIT`, a fallback release
after N vints without a flip, which is exactly the skip case.
`GAMEGATEWAIT=1` with GENSKIP on:

    game frames / vints   50.7%   (baseline 49.7%, GENSKIP alone 35.3%)

The feedback loop is gone. The game advances at its normal rate whether
we present or not. That knob is worth knowing about independently.

**And with the loop broken the lever still cost flips** -- isr-flips
1,866 against 2,095 -- because the hash source was wrong, which 178
already flagged: `SPR_SNAP` is OUR snapshot, refreshed by `text_capture`
on OUR schedule, so hashing it asks "has our copy changed".

**Fixed to hash `FB_SPR`, where the 68K actually writes the sprite list
(64 records x 16 bytes = 256 longs). The result closes the lever:**

    generations SKIPPED 2     launched 2,110     = 0.1%

    our generations   2,107 / 3,983 vints  =  0.53 / vint
    game frames                            =  0.50 / vint

**There is nothing to reclaim. Every generation we launch corresponds to
a real game frame.** The decompile thread's premise -- "at 42%
single-vint the game is missing roughly 58% of its own vints, and each of
those is a generation spent recomposing an unchanged frame" -- is true of
the ARCADE's architecture and false of ours, for the same reason 178
gave: under GAMEGATE the game does not miss vints, it runs at our rate.
Generations and game frames are the same number to within 6%.

My own "0.62 against 0.50, so a fifth are waste" in 177 came from a
different build's ship count and did not survive being measured on the
line. **Ratio of two numbers from two different builds: the same error
shape as everything else this log has had to retract.**

`GENSKIP` is CLOSED, default-off, and the entry stands as the reason not
to rebuild it. `GAMEGATEWAIT` is the part worth keeping in mind.

**What is actually left, both from the decompile thread and untouched:**

  1. **The hole punch** -- cheaper than I costed it. Four scenes of five
     have no partial cat1 row (one screen-row compare, no bitmap), and a
     cat1 cell whose tile is blank needs no hole at all: 68% of scene 0's
     cells and 82% of scene 1's resolve per cell. Two bits per cell, not
     one. This is what makes vi27's 1.12 wall / 42% single-vint
     SHIPPABLE instead of a measurement.
  2. **`build_maps_chunk`** -- the real 0.44 v/gen, worth 0.29 of wall by
     ablation (168), and nothing has touched it. 177 established I had
     been optimising a different function for three builds.

## 180. GAMEGATEWAIT=1 IS THE FEEL WIN, AND THE BLACK TILE BLEED IS ITS PRICE (2026-09-11 18:50)

Mike ran the 18:37 rom on the MiSTer: **"we have the black background
tile bleed, we have inconsistent frames, but this is in the right
direction, this is FAR closer to playable."**

That rom was the GENSKIP experiment with `PHASECENSUS` still in it, so
the credit does NOT go where it looks. GENSKIP skips 0.1% of generations
(179) -- it does nothing. **The change he felt is
`GAMEGATEWAIT=1`.**

`GAMEGATE_MAXWAIT` is how many vints the gate waits for a flip before
releasing the game anyway. Default 4; at 1 the game is released every
vint regardless of whether we presented.

    vi28 = the line + GAMEGATEWAIT=1, nothing else
    isr-flips 2,055   game frames 50.2%   68K handler 54.2
    nopost 52   fallback 122   skips 0   flip-late 0

**ares cannot see why this feels better**, and that is the point worth
recording. The flip count is slightly LOWER than vi26's 2,095 and the
game-frame rate is unchanged at ~50%. What changed is that the game's
LOGIC and INPUT are no longer waiting on our presentation -- they run on
vblank as the arcade does, and the display updates at whatever rate it
can underneath. A player feels input latency and animation cadence
separately from frame delivery, and no counter in this repo measures the
first two.

**The black background tile bleed is the price and it is the tearing
GAMEGATE was installed to prevent.** With the game advancing every vint
instead of every second one, it writes its tilemap staging twice as
often, so a larger share of `cap_page`'s captures land mid-stream: half
the page is the old scene's columns and half the new. `PG_STICKY` /
`pg_watch` is supposed to catch that by watching a page until two
consecutive captures agree, and at wait=1 it is being outrun.

Two builds on the rig for the trade, one flag apart:

    vi28.32x   GAMEGATEWAIT=1   most responsive, most bleed
    vi29.32x   GAMEGATEWAIT=2   half the game-write rate at the capture

If vi29 keeps the feel and loses the bleed, the capture is simply being
outrun and the fix is to make it keep up rather than to slow the game
down. If vi29 loses the feel too, the trade is real and the fix is in
`cap_page`'s mid-stream detection, not in the gate.

## 181. CORRECTION TO 180, AND AN OPEN FACT: THE GAME COMPLETES ONE PASS EVERY TWO VINTS NO MATTER HOW OFTEN WE RELEASE IT (2026-09-11 19:05)

**180 claimed `GAMEGATEWAIT=1` decoupled the game from our presentation.
It did not.** The gate's release lives INSIDE `if (r60_go)` -- the window
path -- so it can only fire on a vint that runs a window. At wait=1 it
releases on every WINDOW, not every vint, and the game tracked the
window cadence exactly as before:

    vi26 (wait=4)   game frames / vints  49.7%
    vi28 (wait=1)                        50.2%

**So I built the release it was missing.** `GATEFREE=1` grants the
release on a vint with NO window as well, which is what "the game runs on
vblank like the arcade" actually requires:

    vi30 (wait=1 + GATEFREE)  game frames / vints  50.2%
                              game's own missed-frame counter  0.0%

**Unchanged. And zero overruns.** The game is released every vint, never
reports a missed frame, and still advances once every two vints.

**That is a hard number and it contradicts this project's founding
premise.** CLAUDE.md: "The 68000 clock is NOT a loss. The game needs 2780
instructions/vint and our 7.670 MHz budget covers that." The 68K's
measured handler cost is ~54 lines of 262, so ~80% of every vint is the
game's. Yet its pass completes at 0.5/vint with its own overrun detector
silent.

Three readings, and I cannot separate them from this side:

  1. **The release flag is level, not counted.** `0xFFA0F5` is set to 1;
     if the game's wait loop clears it and runs a pass that spans two
     vints, setting it twice buys nothing. That would mean the pass
     genuinely costs ~1.6 vints of available 68K time.
  2. **The release is not the game's only gate.** Something else in the
     main loop is also per-frame, and 0xFFA0F5 is not the binding one.
  3. **`0xFFF02A` is not 1 tick per game frame on this scene.** The
     arcade measures 0.937 ticks per screen frame (MAME, LOOP29's
     arcade-rate run), so it is ~1/frame there -- but "per scene" is in
     its own name and gameplay_speed.py carries a scene-reset guard.

**Question sent to the decompile thread** (NOTES-FROM-DECOMPILE 14): they
have read the main loop and the six IRQ4 workers, so they can say which
flag the loop actually waits on and whether one pass can span two vints.
It is the difference between "the display is the only blocker" -- which
is what every plan in this log assumes -- and "the 68K side has a second
gate nobody has found".

`GATEFREE` stays default-off pending that answer: it is a real change to
the release protocol and it currently buys nothing measurable.

## 182-183. THE RELEASE DISCARD IS WHAT PINNED THIS PORT AT 50%, AND REMOVING IT TAKES THE WALL TO 0.81 AND THE SCREEN TO BLACK (2026-09-11 19:15)

The decompile thread answered 181 (LOOP-DECOMPILE 67). Two findings, and
I built on both.

**Their point 2, settled: the zero was honest.** The game CLEARS its own
missed-frame counter at 0x930, on the main loop's countdown arm, so every
0.0% this repo has read off 0xFFF144 was taken with the instrument being
reset. `MISSKEEP=1` NOPs that clear (bytes asserted first).
**Cumulative misses at f1500, f2600, f4100: 0, 0, 0.** The game really
does never overrun. Their caution was right and the answer was still
zero.

**Their point 1 is the mechanism, and it is bigger than they framed it.**
The wait is four instructions:

    397E  clr.b  $FFF01C     <- DISCARDS any pending release
    3982  tst.b  $FFF01C        spin while zero
    3986  beq.s  0x3982
    3988  dbf    d0,0x397E

IRQ4 increments the byte once per vint. The game finishes a pass, clears
the byte -- throwing away the release that arrived WHILE IT WAS WORKING
-- and waits for a fresh one. **So a pass taking slightly more than one
vint costs exactly two.** That is the 50% every build in this log has
measured, with zero overruns, and it is not our gate, our transport or
our compose. It is four instructions in the game.

`RELBANK=n` replaces the clear with a consume (decrement if non-zero,
clamped), patched through the pal-thunk area with the bytes asserted.
Over the same 4000 vints:

                        vi26      vi31/32 (RELBANK)
    generation wall     1.46v         0.81v
    ships              32.8 fps      53.0 fps
    isr-flips            2,095         3,185
    68K handler mean      54.2          47.1
    bad1                     3            10
    stale                  109           249
    nopost                  52           225

**0.81 is UNDER THE ONE-VINT QUANTUM** -- the first time anything in this
log has crossed it without an ablation.

**And on the rig it is a black screen.** Mike: "31 doesnt update the
screen at all. black attract mode, starting the game only renders
background grass and text hud." The ares counters already show why:
`stale` 109 -> 249 and `nopost` 52 -> 225. With the game running its pass
back to back there is no quiet slot left for our window, so the 68K stops
posting and almost nothing lands. Same producer/consumer collision as
NBUILD1 (172), one level up: **we gave the game its full speed and took
the transport's time to pay for it.**

`cap` turns out not to bind -- RELBANK=1 and RELBANK=2 measure
byte-identical, because IRQ4 increments once per vint and the game
consumes one per pass, so the byte never exceeds 1.

**A measurement trap for the next reader.** `gameplay_speed.py` reported
104.3% game frames for RELBANK, which is impossible. The scene timer is
per-SCENE and scene-dependent in rate and direction; at f1500 vi26 reads
536 and vi31 reads 1555, because the faster build is in a DIFFERENT
SCENE by then. **Comparing the scene timer across builds at the same
frame number compares two different scenes.** Use isr-flips and the
generation wall, which are the same quantity in both.

`RELBANK` is default-off. It is the largest single lever found in this
arc and it is not usable until the transport gets its slot back. The
next move is a release that lands the game's pass and our window in the
same vint without either starving -- which is what `GAMEGATE` was
reaching for and got backwards by discarding instead of scheduling.

probe.32x restored to vi28 so there is something playable on the rig.

## 184. WHY RELBANK GOES BLACK: THE DISCARDED VINT WAS THE TRANSPORT'S SLOT (2026-09-11 19:25)

Mike on vi32, and vi31 before it: **"doesnt update the screen at all.
black attract mode, starting the game only renders background grass and
text hud."** His screenshot of the attract graveyard is the diagnosis:
**the frame that does land is PERFECT** -- statues, temple, tombstones,
grass, HUD, credits line, all clean. The compose is right. Only delivery
has stopped.

That matches the counters exactly: `nopost` 52 -> 225, `stale` 109 ->
249, `bad1` 3 -> 10. One good frame arrives and then the 68K stops
posting.

**So the game's idle vint was not waste. It was the slot our transport
runs in.** The four instructions at 0x397E discard a release and force
the game to wait for a fresh one, and that enforced idle is when the 68K
does our consumes, our post and our packet blast. `RELBANK` hands that
time back to the game, the game takes all of it, and the picture stops.
Same shape as NBUILD1 (172) and NTSKIP's flood (170): **three times now,
making a producer faster has starved the consumer, and each time the
slack that looked like waste was load-bearing.**

**The arithmetic was in CLAUDE.md the whole time.** "The game needs 2780
instructions/vint and our 7.670 MHz budget covers that at any cost up to
46 cycles per instruction." 2780 x 46 = 127,880 cycles; a vint at
7.67 MHz is ~127,800. **The budget covers the game with ZERO margin.**
Our 68K handler costs 47-54 lines of 262, about 20% of the vint, so the
game gets 80% of the cycles it needs, takes 1.25 vints, and quantises to
2. That is the 50%, derived rather than measured, and it says the 68K
clock IS a loss once our own handler is counted against it.

**A measurement of mine that is INVALID, stated before anyone uses it.**
I added a pass counter to the RELBANK thunk and read 1.563 and 1.744
passes per vint -- above 100%, which is impossible when IRQ4 increments
once per vint. The thunk sits at 0x397E, which the `dbf` loop RE-ENTERS
once per frame waited, and two attract callers wait 120 and 240 frames
(LOOP-DECOMPILE 67). So the counter counts thunk entries, not game
frames, and attract's long delays dominate it. **Fourth time this arc
that a counter measured something adjacent to the question.** To do it
properly the count belongs at the gameplay loop's OWN call site (0x904 or
0x922), not in the shared wait.

**Where this leaves the arc.** The wall is 0.81 with RELBANK, which is
under the quantum, and unusable. The two numbers have to come down
together:

    the game's pass         ~1.25 vints incl. our handler
    our 68K handler         47-54 lines of 262  (~0.20 vints)
    both must fit           1.00 vint

Removing our handler ENTIRELY still leaves ~1.05. So 60 Hz needs the
GAME'S pass shortened, which is Mike's own pivot -- patch the program --
and not any further transport or compose work. The decompile thread's
main-loop reading is the asset that makes that possible.

probe.32x is vi28, which plays.

## 185. A CORRECT GAME-FRAME COUNTER, AND THE ERROR CLASS THAT HAS RUINED FIVE MEASUREMENTS TONIGHT (2026-09-11 19:30)

`PASSCOUNT=1` counts one tick per gameplay frame at 0x922, the main
loop's OWN wait call, and nothing else. It exists because none of the
three instruments this repo has used for the game's rate is trustworthy:

    0xFFF02A scene timer   per-SCENE, and scene-dependent in rate AND
                           direction (183)
    0xFFF144 miss counter  the game CLEARS it at 0x930 (182)
    a counter at 0x397E    counts attract's 120- and 240-frame delays
                           as frames (184)

It works -- it scales with run length and saturates when the playthrough
leaves gameplay (285 at f1000, 553 at f2000, flat after). **And the
comparison I built it for is STILL INVALID:**

    over f1000-f2000     line 26.8%   SH-2 ablated 3.7%   RELBANK 0.0%

RELBANK reads ZERO, on the build that measurably made the game FASTER
(flips 2,095 -> 3,185, wall 1.46 -> 0.81). The reason is not the counter.
**The builds are at different points in the playthrough at the same
frame number.** The faster build finished the gameplay section before
f1000, so a window that is mid-level for one build is post-mortem for
another.

**THIS IS THE FIFTH TIME TONIGHT, in five costumes:**

  1. the palette mirror, diffed at one frame against a ramp rotating
     twice per frame (166)
  2. colour-set residency, sampled at round-numbered frames rather than
     at the event (156)
  3. `attract_parity`'s high-k columns, compared across builds with
     different OFFSETs (160)
  4. the scene timer, compared at f1500 across builds in different
     scenes (183)
  5. this

Every one is the same mistake: **two builds sampled at the same WALL
time are not at the same point in the GAME, and any per-frame rate
compared that way measures the divergence, not the change.** The
project's own handoff already says the rule -- `attract_parity.py` is
"aligned on the game's own timeline" and `HANDOFF-SESSION6` says why.
I read that file at the start of this session.

**The only cross-build numbers in this log that are safe** are ones
counted over the same 4000 vints and independent of where the game is:
`isr-flips`, the generation wall, the ship period bins, the 68K handler
mean, and the allocator counters. Every rate with the GAME in its
numerator needs aligning first.

**What the honest version of this measurement needs:** total gameplay
frames divided by the vints spent IN gameplay, per build -- the counter
gives the numerator and its saturation point gives the denominator. Two
bisects per build. Not done; stated so nobody mistakes the table above
for a result.

**And the open question from 184 is still open.** Whether the game's
1.25-vint pass is bus contention from our SH-2 or intrinsic to the
7.67 MHz clock is exactly what the ablation column was meant to answer,
and it did not.

## 188. MIKE'S VERDICT: vi37 IS THE MOST PLAYABLE BUILD SO FAR (2026-09-11 20:15)

**"PLAYABLE! Still dropping frames but we expect that. but most playable
version so far."**

    rom/night/vi37.32x   md5 c0c0a0ce   ALSO rom/s16.32x
    make ship-us FBXPORT=1 FBXSTAGE=1 FBXPEND=1 FBXISRLIFT=1 PGSKIPPKT=1 \
                 TEXTCAPMASTER=1 TEXTCAPFULL=1 GAMEGATE=1 GAMEGATEWAIT=1 \
                 TEXTCAPEARLY=1 TAGKEEP=1 PENHOLD=1 PENREPAINT=1 \
                 NBUILD1=1 MDSPRTOP=1

    generation wall     1.48v      ships 32.1 fps
    isr-flips           2,095      (the accepted rom's line was 1,724)
    68K handler          54.2 lines
    single-vint frames     20%
    skips / flip-late     0 / 0

**What got it here, in the order it mattered on the rig:**

  1. **The background fix** (152-164). The colour-set drift free was
     destroying ~2,500 on-screen tile slots per 4,000 frames; holding a
     set's pen indices across the free and repainting its own pens took
     that to 644. Mike: "backgrounds render."
  2. **`NBUILD1`** (172). One MD packet build per gap instead of two.
     **MANDATORY on silicon** -- without it the rig is 0.33 fps while
     ares shows a 10% difference.
  3. **`TEXTCAPMASK` OFF** (171). The mask drops an ungated cut-scene
     writer, so the Zeus text rendered as scattered letters across
     several builds. One flag, confirmed by Mike's A/B.
  4. **`GAMEGATEWAIT=1`** (180). Feels better and no counter here can
     see why; the flip count is slightly LOWER. Input and animation
     cadence are felt separately from frame delivery.

**What is still between this and parity, ranked by measured evidence:**

  1. **The tile layers to the VDP** (PLAN-TILES-TO-VDP, steps 1-2 done,
     174). Removing the FB cat-1 pass measured wall 1.46 -> 1.12 and
     single-vint 20% -> 40%. Blocked on the palette: the cat-1 sets must
     be in a STATIC baked table or the allocator churns (a fourth CRAM
     line made it 15x worse, 176). The decompile thread has packs for
     scenes 0, 1, 2 and 4; the emitter needs to build the runtime tables
     from their exhaustive viewport list instead of my sampled harvest.
     **This is the only item with a shippable build at the end of it.**
  2. **`build_maps_chunk`** -- the real 0.44 v/gen, 0.29 of wall by
     ablation (168), never touched. 177 established I spent three builds
     optimising a different function.
  3. **The game's own pass.** 1.25 vints including our handler, and
     CLAUDE.md's own arithmetic covers its 2,780 instructions with zero
     margin -- so our 20% handler is what pushes it over. Whether that is
     bus contention from our SH-2 (fixable) or the 7.67 MHz clock (not)
     is the question that decides the rest, and the ablation meant to
     answer it was invalidated by the alignment error (185).
  4. **The frame-done signal** (LOOP-DECOMPILE 70) -- the game knows
     exactly when its frame is complete and the pipeline infers it. Both
     my readings are retracted (the probe's thunk body is missing from
     the tree) but the channel exists: COMM10 bits 13-15 are spare.

**Parked with a measured reason:** `RELBANK` crosses the one-vint quantum
at 0.81 -- the only thing all session that did -- and takes the
transport's only quiet slot, so the screen stops. It becomes viable after
1 and 2 free 68K time.

## 189. OPTION B BUILT, AND PINNING DOES NOT CONVERGE — THE SETS OUTSIDE THE TABLE MUST BE REFUSED, NOT ASSIGNED (2026-09-11 21:00)

Mike picked option B (overflow to the framebuffer) over per-band CRAM.
Both halves of the palette work are now measured and the result is a
clear design constraint rather than a win.

**B's baker half is in.** `pack()` no longer fails when a palette does not
fit -- it leaves it UNASSIGNED and collects it, so a scene that does not
pack degrades instead of failing the build. On the five scenes as they
stand it catches nothing: all five pack into four lines.

**The emitter is in, and it was the actual blocker.**
`bake_tilecram.py --emit-mds` writes `pal_scenes_md.h` in the RUNTIME
table format from the worst-case VIEWPORT rather than `mdpen_bake.py`'s
sampled harvest. That matters because `mds_install` PINS every set in
its table against `mdp_free_set`, and the whole CAT1MD churn story
(153-164, 176) is sets that are in no table.

**It works, and the proof is a counter that had always read zero:**

    pin DECLINES (mds_pin blocked a free)   5,718
    frees that got through                    128

`mds_pin` declined 5,718 frees. LOOP29 156 measured that same counter at
**ZERO across a 4,000-frame run**, which is what "the baked table does
not name a single one of the sets that churn" meant.

**And the churn did not go away -- it MOVED.** With the viewport's 25
sets pinned, the sets still being freed were different ones: 68 (29
frees), 11 (17), then 122, 115, 35, 126, 125, 124. So I added them:

    pinned sets    lines            on-screen tiles destroyed / 12k
    25 (viewport)  [15,15,11,5]     6,221
    35 (+churners) [14,14,13,11]    6,933
    42 (+the next) [15,15,15,14]    9,671      <-- WORSE

**Pinning more makes it WORSE, and the reason is the same one as the
fourth CRAM line (176).** At 42 sets the four lines are 59 of 60 slots
full. Every set NOT in the table still goes to the dynamic allocator,
and now there is no slack left for it, so it thrashes harder. **The
iteration diverges.** You cannot pin your way out of this incrementally.

**THE DESIGN CONSTRAINT, which is the result worth keeping:** a tile
colour set that is not in the scene's baked table must be **REFUSED an
MD line entirely and drawn by the framebuffer** -- not assigned
dynamically. `mdp_note_tile` currently calls `mdp_assign_set` for any set
with no line, which is what puts it in the allocator's hands. Under
`MD_STATIC` it should return 0 instead, exactly as it already does for a
`soft` cat-1 claim, and the FB should keep those cells.

That is option B's RUNTIME half, which I deferred as speculative when
the baker reported no overflow. It is not speculative: it is the only
thing that makes the static table mean anything, because "in the table"
has to imply "and nothing else gets a line".

It needs one piece that does not exist: **under MDBGALL the FB does not
draw BG/FG-cat0 at all** (m_main.c clears those rows to 0 = MD-through,
7625). The cat-1 path has the fallback already (`CAT1_PEND`); the other
two layers do not. That is the next build.

**Speed, unchanged by any of this and still the best measured:**
wall 1.13, 38.4 fps, **45% single-vint** against the line's 20% -- but
on a build whose backgrounds are worse than vi16's. The speed comes from
`C1NOFB` (175) and is independent of the palette question.

The line is untouched: `pal_scenes_md.h` restored to the harvest bake,
`rom/s16.32x` rebuilds to vi37's numbers (wall 1.48, 32.1 fps).

## 190-191. THE REFUSE RULE GIVES ZERO CHURN AND ONLY WORKS ON LEVEL 1 (2026-09-11 21:20)

`MDSREFUSE=1`: a tile colour set absent from the scene's baked table is
REFUSED an MD palette line instead of being handed to the dynamic
allocator. 189 established this was necessary -- pinning MORE sets makes
the churn worse, so "in the table" has to imply "and nothing else gets a
line".

**It works completely, on the line's own flags, with no CAT1MD:**

    over 12,000 frames          vi16     vi37      vi38 (refuse)
    on-screen tiles destroyed    644     ~1,300        0
    frees that got through        57    128-779        4
    cells blanked, art missing  6,372    ~6,056    3,818
    refusals                     n/a       n/a   169,348

**Zero destroyed tiles.** The allocator stops working, which is the point,
and the level-1 frame renders complete (temple, statues, trees, wolf,
lettered gravestones, grass, player, enemy).

**And it blanks every other scene, which kills it as it stands.**
Mike, in order: "distinctly what's missing is the chevron blue from the
altered beast transformation", then "level 2 and the transition scene
have many missing tiles" with a shot of the cave level black except the
stalactites and a few floor cells.

**The cause is not subtle and I should have seen it before building:
`pal_scenes_md.h` has MDSTATIC_N = 2, slots for `normal` and
`boss_smoke`. There is no table for the transform and none for any round
but the first.** Refusing everything outside a table that only covers
level 1 blanks level 2, the transition and the transformation, exactly as
observed. The rule is correct; its input covers one fifth of the game.

**Two bugs of my own in the rule, found by chasing the transform:**

  1. `mds_scene_cur` is the PSCENE index; the TABLE index is
     `mds_table_of[pscene]`, which is how the runtime maps them
     everywhere else (m_main.c 5354, 5369). I indexed
     `mds_s_line[mds_scene_cur]` directly, so a pscene past MDSTATIC_N-1
     -- the transform is exactly that -- read PAST THE END of the array
     and refused on garbage.
  2. `mds_scene_cur` is 0xFF for "no tables installed", and
     `mds_s_line[0xFF]` is far out of bounds.

Both fixed (bounded through `mds_table_of`), and fixing them did not fix
the symptom, because the symptom is the missing tables.

**A middle ground that does NOT work, measured:** asking for the line
SOFTLY for out-of-table sets -- `mdp_assign_set(..., soft=1)`, which
returns 0 rather than evict -- brings the churn straight back (670
destroyed, 30 frees) while only 29 out-of-table sets ever fit. The churn
comes from the eviction, and out-of-table sets essentially always need
one.

**SO THE DEPENDENCY CHAIN IS NOW EXPLICIT, and it is the decompile
thread's own step 5:**

    SCENESEL probe (LOOP-DECOMPILE 66, already built)
      -> a playthrough that reaches scenes 1-4
      -> live palette dumps per scene
      -> bake_tilecram --emit-mds for all five scenes
      -> MDSREFUSE becomes safe, and the allocator is gone for good

Until then `MDSREFUSE` is default-off. It is not a dead end -- it is the
first thing all session that took the background churn to ZERO -- it just
cannot ship against a one-scene table.

`rom/night/vi39.32x` (md5 c93dbeab) is vi37's flags rebuilt: the line,
with the refuse rule off. On the rig and playing.

## 192. ALL FIVE ROUND TABLES BAKED FROM THE GATED DUMPS, AND THE BLOCKER WAS MINE (2026-09-11 21:40)

The decompile thread's blocker was correct and it was my tool: `--live`
applied live colours to ONE scene per run and fell back to rom for the
other four, and rom is wrong above palette 63 (174). Fixed with
`--live-dir`, which reads each round's OWN gated dumps
(`discover/cram/wide/s<N>_*.bin`, 136-168 per round) and unions them.

**Their five packs, independently reproduced to the slot:**

    round 0   25 palettes   [15,15,11,5]   46 slots   4 lines
    round 1   11 palettes   [14,14, 5]     33 slots   3 lines
    round 2   14 palettes   [13,15]        28 slots   2 lines
    round 3    8 palettes   [15, 7]        22 slots   2 lines
    round 4   15 palettes   [14,14,11]     39 slots   3 lines

Nothing overflows. Only round 0 needs four lines. Their gating result
holds up: the per-round tile palette really is static.

**And the emitter now keys on the GAME'S ROUND, which is the fix for
191.** `sh_src/pal_rounds_md.h`: `mdr_line_c[5][64]`, `mdr_s_line[5][128]`,
`mdr_s_map[5][1024]`, `mdr_s_used[5][128]`. The old table space was
PALSTATIC's palette-DETECTED scenes (`normal`, `boss_smoke`), which is
orthogonal to rounds -- two slots for a five-round game -- and that is
exactly why the refuse rule blanked level 2 and the transition.

**What is still missing, and it is one channel.** The SH-2 cannot read
68K WRAM, so it cannot see the game's round variable. The 68K has to
publish it, and **COMM10 bits 13-15 are spare** (verified 187: the low 13
are the tile-dirty mask and the SH-2 masks with 0x1FFF) -- three bits,
enough for five rounds. The shim writes `(round & 7) << 13`, the master
selects `mdr_*[round]`, and `MDSREFUSE` becomes safe everywhere instead
of only on level 1.

**On the transformation, the thread settled it and it is NOT a scene.**
Two instructions in the program write the scene variable and the table
holds 0-4, so nothing will ever ask for a transform table by number. The
transform recolours the PLAYER, and a player's palette is the object's
own slot and index -- a SPRITE palette. It sits outside everything the
tile baker measures. **So the chevron blue was never going to be covered
by a tile table, and its cover has to come from the sprite side.** That
also means the refuse rule must never be allowed to refuse a sprite set:
the rule belongs to the tile path only, which is where
`mdp_note_tile` sits, so the scoping is already right -- what was wrong
was refusing against a table that could not describe the round.

**Their trap, recorded because it would have poisoned my emitter too:**
the scene variable says which scene is LOADED, not what is on screen.
Between attract screens the game reloads 69 of 128 palettes into the same
work RAM while the variable still reads the forced scene, so a blind dump
gave rounds 3 and 4 fifty-one and fifty-nine slots with overflow -- the
eye title's and the score table's colours wearing the round's name.
Gating on the attract step at 0xFFF031 fixes it. **My own `--live` runs
for round 0 were ungated**, and they agreed with the gated pack (25
palettes, [15,15,11,5], 46 slots) only because round 0 is what the
attract actually shows.

## 193. THE ROUND CHANNEL IS IN: ZERO CHURN, AND LEVEL 2 RENDERS (2026-09-11 21:45)

Three pieces, all small, all measured:

  1. **The 68K publishes the round.** `COMM10` bits 13-15 carry
     `*(uint8_t*)0xFFF142 & 7` -- the game's own scene variable
     (LOOP-DECOMPILE 66) -- at all four COMM10 publish sites. The low 13
     bits stay the tile-dirty mask the SH-2 masks with 0x1FFF (187). The
     SH-2 cannot read 68K wram, so this is the only way it can know which
     round's table to install.
  2. **The static tables are per-ROUND.** `sh_src/pal_rounds_md.h` (192),
     five slots, pointed at through macros so `mds_install`'s body is
     untouched. The old space was PALSTATIC's palette-DETECTED scenes --
     two slots for a five-round game -- which is why 191 blanked level 2.
  3. **The refuse rule keys on the round**, which cannot go out of bounds
     the way indexing by pscene did.

**`MDROUND=1 MDSREFUSE=1`, over 12,000 frames:**

    on-screen tiles destroyed        0      (vi16 644, vi37 ~1,300)
    frees that got through          11
    pin declines                 1,224
    cells blanked, art unshipped 2,572      (vi16 6,372 -- best of the arc)
    refusals                   414,993

**And the thing 191 broke is fixed, verified with their SCENESEL probe.**
Forcing rounds 1 and 2, same frame, refuse against the line:

    round 1   line 56 colours / 5.1% black    refuse 57 / 5.1%
    round 2   line 60 colours / 23.0% black   refuse 65 / 20.3%

**Refuse has MORE colours and LESS black on both.** The round-2 frame is
complete -- cave walls, stalactites, rock platforms, the boulders with
their green and pink detail, ground rocks, HUD -- against Mike's earlier
shot of that level black but for the stalactites.

Transport unchanged: wall 1.47, 32.3 fps, isr-flips 2,067, handler 54.0,
skips 0, flip-late 0. **This is a pure background-quality change and it
takes the dynamic palette allocator out of the tile path entirely** --
the thing entries 153 to 191 have been circling.

    rom/night/vi40.32x   md5 as deployed, on the rig

**Still open and NOT covered by this:** the transformation's chevron
blue. The thread settled that the transform is not a scene -- it
recolours the PLAYER, and a player's palette is a sprite palette, outside
every table the tile baker measures. So it needs the sprite side, and no
round table will ever cover it.

## 194-195. THE ROUND TABLES INSTALL, LEVEL 2 IS FIXED, AND LEVEL 1's TREES GO PINK (2026-09-11 22:05)

**194 -- the level-2 black was a SECOND install site.** 193 patched the
one at m_main.c 5414 and missed the one at 11199, which kept indexing a
ROUND-keyed table with the palette-DETECTED scene -- installing round 0's
or 1's palette while another round ran. **My SCENESEL verification could
not have caught it: the probe never takes that path.** Fixed; forcing
each round now gives healthy frames:

    round 1   57 colours   5.1% black
    round 2   65 colours  20.3% black    (was black but for the stalactites)
    round 3   73 colours   3.7% black
    round 4   75 colours   6.4% black

**195 -- and level 1 regressed: pink trees and a purple band in the
sky.** Everything else in the frame is right (temple, wolf, lettered
gravestones, grass, player, enemy) and it carries MORE colours than the
good build, 100 against vi39's 89, differing on 43.7% of pixels.

**What I ruled out, in order:**

  - **NOT the pen map's sample choice.** `px.setdefault` took whichever
    gated dump sorted first, so a mid-fade sample would have built every
    map from fade colours. Changed to the MODE of the per-pixel vectors
    across all 140 dumps. **Byte-identical result** -- 100 colours, same
    43.7%. The first sample was already the resting state.
  - **NOT the source colours.** Sets 74, 92 and 95 -- the big background
    palettes -- read IDENTICALLY in the decompile thread's gated attract
    dump and in my own in-game play2 dump: `777 676 565 455`,
    `446 346 346 246`, `065 054 043 043`. The data is right.

**So the defect is in the LINE/SLOT assignment or its application, not
the colour data.** The runtime remaps tile pixels through `mdp_s_map` at
emit time (`md_emit_art`: `map[r[kk*2]]`) and paints CRAM from
`mdp_line_c`, and both now come from the baked table -- so a set drawn in
another set's hues means those two disagree somewhere between the pack
and the install.

**The next step is a READBACK VERIFIER, which is Mike's own standing
rule:** dump `mdp_line_c` (0x3C400) and `mdp_s_map` (0x3C500-ish) from a
running frame and diff them against `pal_rounds_md.h`. If they match, the
pack is wrong; if they differ, the install is. That is one ares dump and
one python diff, and it is the only way to tell those two apart --
exactly the kind of thing I should have built before shipping vi41.

`rom/night/vi39.32x` (md5 c93dbeab) is back on the rig: the line, round
tables OFF. It is the build Mike called playable with no obvious
regressions.

**Not covered by any of this, and now clearly its own problem:** the
transformation. Mike's vi41 shot shows the chevron rendering YELLOW and
ORANGE on RED where it should be blue. The thread established the
transform recolours the PLAYER, so it is a sprite palette and no tile
table touches it. Same for the intro cutscene, which vi40 rendered in
flat purple monochrome. **The round tables cover the five playable
levels and nothing else** -- intro, transformation and the transitions
all sit outside them, and the refuse rule starves whatever the last
round's table does not list.

## 196. THE PINK TREES ARE THE TABLE'S ENCODING, NOT ITS COLOURS (2026-09-11 23:50)

195 left the defect somewhere "between the pack and the install" and
proposed an ares readback to tell them apart. **It does not need one. Both
faults are in the emitted header and both are provable offline against the
arcade.**

`tools/mdstatic_oracle.py` walks whichever table the build installs the way
the SH-2 does -- `line = s_line[p]`, `pen = s_map[p*8+pixel]`,
`col = line_c[(line-1)*16+pen]` -- and compares that colour against
`mdpen_bake.quant` of the arcade's own palette RAM word from the decompile
thread's `discover/cram/arcade/sceneN.bin`. On the committed
`pal_rounds_md.h` it reported **every map-referenced palette of every round
wrong**: 25, 11, 14, 8 and 15 of them.

**Fault 1, and this is the pink trees: the table is in the wrong
encoding.** `bake_tilecram --emit-mds` wrote `md_word()`, the MD CRAM word
`(b<<9)|(g<<5)|(r<<1)`. `m_main.c:1167` aliases `mdr_line_c` straight onto
`mds_line_c`, and `mdp_line_c` (m_main.c:333, built at 1692, decoded at
1875, expanded to a CRAM word at 4408 and 14155) is the **9-bit packed**
form `(b<<6)|(g<<3)|r`. So the SH-2 re-read every colour with the wrong
field positions:

    white (7,7,7)  ->  md_word 0xEEE  ->  read as 9-bit (6,5,3)

A dull olive where the trees' white highlight should be. Recognisable
image, wrecked hues, MORE distinct colours than before -- exactly the
100-against-89 and 43.7% of pixels that 195 measured.

**Fault 2: the quantiser disagreed with the runtime's.** `md()` truncated
(`>>2`); `mdp_quant` rounds (`(v5+2)>>2`, clamp 7). On an evenly spaced
ramp truncation lands one step dark, which is why the re-encoded table
still read like the arcade's ramp shifted by a pen. **412 of the
map-referenced pens differed** -- and the SH-2's drift check compares a
table colour against `mdp_quant` of the live word (1842, 2124-2157), so a
truncated table is freed and re-claimed on sight. That is churn the pin
was supposed to stop.

**Fault 3: black was emitted as a free pen.** `'0x%04X' % (v if v else
0xFFFF)` cannot tell `md_word((0,0,0))` from an unassigned slot. 10 pens
across the five rounds, all of them a real black.

**Proof of the diagnosis, in the order it was established.** First the
committed header was reproduced byte-identically from
`--live-dir discover/cram/wide`, so the tool under test was the one that
shipped. Then, comparing with `md_word(md(w))` -- bake's own encoding and
its own truncating quantiser -- the table matched the arcade **exactly, 0
mismatches over all five rounds, every map-referenced palette, every pen**.
So the colour DATA was never wrong; only its encoding and its rounding
were. Fixing both:

    round 0   33 map-referenced palettes   0 wrong colour, 0 dropped pen
    round 1   16                           0, 0
    round 2   16                           0, 0
    round 3   14                           0, 0
    round 4   19                           0, 0

Set coverage is unchanged (25, 11, 14, 8, 15 pinned); the rounding changes
which colours collide, so the line loads move and round 3 now packs into
ONE line instead of two.

**Also measured, and NOT a fault:** a pen the table drops is an MD pixel 0,
which renders transparent -- a hole, not a hue. `--pens` decodes the 3bpp
tile roms for every tile the scene's own map points at and reports only
palettes whose tiles USE a dropped pen. On the old 2-slot `pal_scenes_md.h`
(what vi39 ships) that was 4 pens in total: pen 1 of sets 80 and 82 in
level 1, and all of set 0 in rounds 4 and 5. On the fixed round tables it
is zero. **vi39's own table has no wrong colour anywhere** -- its
disagreements with the arcade are all dropped pens -- so this fault
arrived with the round tables and did not exist before them.

**NOT VERIFIED: the picture.** Nothing here has been built or played. The
claim is that the installed table now agrees with the hardware word for
word, which is necessary and may not be sufficient.

    python3 tools/mdstatic_oracle.py            # round tables, per round
    python3 tools/mdstatic_oracle.py --scenes    # vi39's 2-slot table
    python3 tools/mdstatic_oracle.py --pens      # dropped-pen audit

## 197. THE PINK TREES WERE THE LINE COUNT, NOT THE ENCODING (2026-09-12 00:15)

196 fixed two real faults in the emitted round table and **made the
picture worse**, which is the whole lesson. vi43 on the rig still had pink
trees and gained a purple sky.

**`bake_tilecram.py` packed into FOUR MD CRAM lines. The background
allocator owns THREE.** `m_main.c:326` sets `MDP_LINES 3`; the fourth is
`MDP_LINES4`, and 328 carries `#error "MDP_LINES4 takes the MD sprite line
for tiles"` against `MD_SPR`, which every shipping build defines. So every
set the baker put on line 4 was painted out of the sprite line. In round 0
those sets were:

    92, 93          the SKY
    95, 96, 97, 99  every TREE

`mdpen_bake.py` has always used `NLINES = 3` (tools/mdpen_bake.py:46),
which is why vi39's `pal_scenes_md.h` leaves its fourth block 0xFFFF and
why vi39 looked right. The round-table path was the only one that ever
emitted a line 4.

**And 196 made it worse by exactly the mechanism it fixed.** Correcting the
quantiser changes which colours collide, so round 0's line loads went from
[15, 15, 11, 5] to [14, 14, 15, 10] -- the line-4 casualty list doubled.
Both faults were real; only together do they fix anything.

    LINES = 3, round 0: [14, 15, 14], 24 of 25 sets pinned
                        set 87 OVERFLOWS to the framebuffer (11 cells)
    rounds 1-4 fit entirely, no overflow

**vi44** (`rom/night/vi44.32x`, md5 22adf27c), flag-identical to vi42, on
the rig at 04:11. **Mike's shots: the sky is blue and the trees are
green.** Measured against `ref_arcade/ref_008000` in the same crop:

    sky     vi44 92AECE 7792CE     arcade 7394CE 638CC5
    trees   vi44 007755            arcade 006342
    grass   vi44 73CE00            arcade 73CE00   (exact)

**NOT fixed, and now the visible defect:** black cells along the tree line
and the wall top, a few per frame. They are NOT the overflow sets -- those
are 14 cells in round 0, all at map rows 16-27, and the blacks sit on the
skyline. Un-shipped patterns rendering as backdrop is the standing
explanation (the drift-free residency wipe, LOOP29 152-164).

## 198. LEVEL 4'S SLOWDOWN IS NOT THE BACKGROUND (2026-09-12 00:35)

Mike, on vi44: "level 4 is barely playable ... a frame budget that
overloads in level 4." Two background-side causes ruled out offline, both
against the round-4 table as built:

    level   cells drawn in SOFTWARE      worst 40-col window,
            (set not in the table)       distinct (code,set) pairs
      1        14   0.4%                        685
      2       256   6.7%   (set 1: 248)         482
      3        16   0.5%                        527
      4         6   0.2%                        400
      5        42   1.8%                        528

**Level 4 is the LIGHTEST level on both counts** -- fewest software cells
and fewest distinct patterns to keep resident, and its table needs only
ONE line. So neither the software fallback nor tile residency explains it,
and level 2, which has 18x more software cells, is not the one Mike
flagged. The cause is on the sprite or game-logic side.

**The instrument is the rig, not these tools:** `BOOTGAMERATE=1` paints the
game's own dropped-frame rate (LOOP29 140, START-HERE "THE PIVOT"), and
hardware speed is read only off that.

## 199. THE BLACK TILES AND THE DEAD TRANSITIONS ARE ONE KNOB (2026-09-12 00:40)

Mike on vi44, after 13 rig shots: "lots of black tiles popping in,
transitions scenes really not working, chevron missing from animations
transforms." Two of those three are the same mechanism, and it is not a
bug -- it is a calibration.

**The shipper moves 12 tiles per vint while the display is on.**
`m_main.c:1444` sets `MD_BATCH 12` under R60, and 13268 applies it:
`bmax = (disp_blank || !r60_disp_on) ? 40 : MD_BATCH`. A cell whose
pattern has not reached MD VRAM renders as backdrop, i.e. BLACK. So:

    scrolling in a fresh column   ~50-80 new patterns   4-7 vints of black
    a scene change               the whole working set  1120 / 12 = 93 vints

**93 vints is 1.5 seconds.** That is not "popping in", that is the
transition, which is exactly the shot where Mike sees near-total black
(`20260912_043204-vi44.png`: the transformation orb alone on black;
`043215`: the player on black with only the floor strip). The blanked path
already ships 40, so a transition that blanks gets 28 vints -- still half a
second.

The 12 was deliberate (the comment above it): 40 was calibrated for 30Hz
windows and at 60Hz the 40-tile DMA overruns vblank into the active-display
rate, which is the load-in tear and the purple band from Mike's second
pass. **So this is a two-sided knob with a visible failure at each end, and
the only instrument that ranks it is Mike's eye.**

`make ... MDBATCH=N` now sets it. **`rom/night/vi45.32x` (md5 e4544fd9) is
MDBATCH=24, staged on the rig, NOT launched** -- flag-identical to vi44
apart from `-DMD_BATCH_N=24`. If 24 kills the pop-in without bringing the
tear back, the knob is the answer and the next question is whether a
blank-aware schedule beats a constant.

**NOT MEASURED:** whether 24 overruns vblank. The comment's 92-line consume
spike was measured at 40; nothing has been measured between.

**The third defect is not mine and not this.** The transform chevron is the
decompile thread's open item with a hypothesis already on the record
(commit c0985e8: the records never reach 68K palette RAM; the queue drain's
destination is loaded from the queue, so the 0x840000 -> 0xFF9000 rebase
cannot reach it). `tools/patch_game.py` 1043 does thunk the queued-pointer
writers at 0x2DC8 and 0x3C5A, and 945 records that the enqueue at 0x3C20
forms an already-rebased 0xFF9800 -- so the hypothesis needs the OTHER
enqueue sites checked, not those two. Left with them.

## 200. THE CHEVRON RECORDS DO ARRIVE. THE 68K IS EXONERATED (2026-09-12 01:05)

The decompile thread's reply (commit c0985e8) concluded "the records never
arrive" in 68K palette RAM and hypothesised that the queue drain's
destination, being loaded from the queue rather than an immediate, escapes
the 0x840000 -> 0xFF9000 rebase. **Measured on vi45: both halves are
wrong.**

`tools/chevron_probe.lua` reads all 64 actor lines every frame and reports
any line whose 14 words equal one of records 132-137 -- the blue ramp of
LOOP-DECOMPILE 79 -- so arrival is observed, not inferred from a colour
census. 4000 frames of attract, MAME, where the 68K side reads true:

    arcade   6022 frame-line hits   records 132-137 all 929-1078
    vi45     6616 frame-line hits   records 132-137 all 1028-1181

**Ours writes them MORE often than the arcade, not less.** And the rebase
is in: `tools/patch_game.py` 945 already records that 0x3C20 forms an
already-rebased 0xFF9800, and a writer census over the same lines finds the
game's own drain at 0x2DCC firing 64 times against the arcade's 28. The
only other writers into those lines are three one-shot RAM clears at boot
(0x8806C8, _start at 0x8C0446, and a RAMCODE clear at 0xFF1ECA).

**So the loss is downstream of 68K palette RAM, and the mechanism is the
MD sprite offload's SINGLE LINE.** LOOP-DECOMPILE 79's own trace is the
key: the transform's palette SLOT walks with its record, (0,132) through
(5,137), i.e. actor lines 64 to 69, changing every SECOND frame. The MD
sprite path carries ONE set per frame -- `m_main.c:4415`, the anchor's 14
pens into MDSPR_PAL -- and a record whose set is not the anchor and is not
pen-identical to it is refused at 4317 and left to the framebuffer. The
anchor is elected by a margin held over passes (4346-4370), which cannot
track a set that moves every two frames. **So the player is dropped from
the MD path for the whole transformation**, and what draws it then is the
FB sprite path.

**THE A/B, ON THE RIG AS `rom/night/vi46.32x` (md5 354093fa), NOT
LAUNCHED:** vi45 plus `MDSPROFF=1` (LOOP29 176), which removes the MD sprite
offload entirely and sends every sprite through the framebuffer. If the
chevron appears, the anchor mechanism is the cause and the fix is to let
the transform's six sets ride the line the way the beam does. If it is
still missing, the defect is in the FB sprite path and MDSPR is innocent --
and `SPR_TRUNC` is the next thing to pull, since the master skips a packet
it reads as landed==0.

**Cost:** MDSPROFF puts every sprite back on the SH-2, so vi46 will be
SLOWER. It is a diagnostic, not a candidate.

## 201. RETRACTION: RECORDS 132-137 ARE THE EYE, AND 200's STORY IS WITHDRAWN (2026-09-12 01:25)

Entry 200 measured that records 132-137 reach 68K palette RAM more often in
our port than on the arcade, then built a mechanism on top of that:
transform -> palette slot walks lines 64-69 -> the MD sprite anchor cannot
track it -> chevron dropped. **Mike, on vi46: "no chevron is present. Quit
making shit up." He is right, and here is what the check should have been.**

`/tmp/chv_snap.lua` snapshots the arcade AT the frames where each record
first goes live. Records 132-137 go live at frames 1169, 1176, 1178, 1180,
1182 and 1184, and every one of those frames shows **the EYE** -- the
attract's eyehold scene, blue iris. Not a transformation. So "records
132-137 are the transform chevron" is not established by anything I
measured, and **every inference 200 drew from it is withdrawn**: the MDSPR
anchor story, the claim that the player drops off the MD sprite path during
the transform, and the reading of vi46 as an A/B for it.

**What survives, because it was measured directly:**

  - `tools/chevron_probe.lua`'s counts. Over 4000 attract frames the arcade
    lands 6022 frame-line hits on those six records and vi45 lands 6616.
    Whatever those records drive, our port writes them MORE, not less.
  - The writer census over lines 64-127: the game's own drain at 0x2DCC
    fires 64 times in our port against the arcade's 28, and the only other
    writers are three one-shot boot clears. The rebase is in.

**What I could not get, and why this stalled.** There is no arcade
reference for the chevron in anything this repo can drive headless. The
no-coin attract never transforms -- swept 2400-6000 at 30-frame steps and
5280-5700 at 3-frame steps, contact sheets in /tmp/sheet*.png. And
`tools/auto_beast.lua`'s playthrough dies before collecting three spirit
balls: 13500 frames, GAME OVER, no transformation (/tmp/ab_a.png,
/tmp/ab_b.png). **Until an arcade capture of the transformation exists, any
statement about what the chevron should look like is a guess, and 200 is
what guessing produced.**

**vi46 is still worth Mike's eye for one thing only** -- it is vi45 with the
MD sprite offload off, so it answers "is any sprite defect MDSPR's fault"
regardless of what the chevron is. It is NOT the chevron A/B I called it.

## 202. THE CHEVRON IS TILE SET 19 ON PAGE 11, AND THE ROUND BRANCH NEVER LIFTS THE REFUSAL (2026-09-12 01:35)

Mike: "no chevron is present." Found in the arcade corpus rather than
recalled: the transformation cutscene (ref_arcade 10741-10801, and the
same scene in the attract intro at MAME frames 4460-4540) is the face on a
red field with a BLUE CHEVRON-PATTERNED TILE PLANE across the upper half
and red flames over it. Ours (vi44 041521, vi46 051450) has the face, the
red field and the flames, and the upper half BLACK. The chevron is that
plane.

**What it is made of, from the arcade at frame 4520:** text RAM 0x410E80
reads 0xAAAA 0xBBBB, so the cutscene draws from tilemap pages 10 and 11,
not the level's 0 and 5. Page 11 is 800 cells of colour set 19, a pure
blue ramp (006 006 007 007 007 005 005); page 10 is sets 20-21, the
yellow-to-red ramp. **Set 19 is in no round table.**

**Why it is black:** MDSREFUSE refuses a set absent from the installed
table and refused cells render as backdrop (Makefile, LOOP29 190). The
detector already has the escape -- on a foreign palette span it clears the
pins and sets mds_scene_cur = 0xFF, "dynamic rules" (m_main.c 11146) --
but the MD_ROUND branch of the refuse test (2079) indexed by md_round
alone, and md_round is only ever assigned at the two install sites. The
game's own scene variable 0xFFF142 is the round number and nothing else:
logged every frame across 6000 frames of attract it reads 0, then 1 for
the level-2 demo, then 0; the face, eye and intro carry the last round's
number. So the cutscene inherited round 0's table and refused its own
plane. 191 said the chevron went missing in vi38, the build that
introduced the rule; 193's round keying kept it missing by a different
path.

**Fix:** refuse only while `mds_scene_cur != 0xFF`. When the detector
declares the span foreign the refusal lifts and set 19 goes to the dynamic
allocator, which is what drew it before vi38.

**Known latency, NOT measured:** the detector needs 16 no-match K-vints
before it declares unknown, so the plane may appear late in the cutscene.
If Mike sees it pop in, that number is the next knob.

**Also retracted, for the record:** LOOP-DECOMPILE 79's "the chevron is
records 132-137" (those are the eye's iris, sprite palettes) and my own
200. The chevron is a TILE plane and never touched the actor lines.

`rom/night/vi47.32x` (md5 32a2beed) = vi45 + this fix, flag-identical,
staged on the rig, not launched.

## 203. THE CHEVRON PLANE IS BACK IN HEADLESS ARES, AND THE LIFT IS AGED PER VINT (2026-09-12 01:40)

**202's fix proven, Mike's rule: a long headless run and a read-back.**
ares-headless screenshots every 25 frames, 1100-2600, cropped to the
active 320x224, with a pixel test: red-lower = share of rows 120-200 that
are pure red (the field), blue-upper = share of rows 10-90 that are blue
(the chevron plane; the arcade's own frame reads 0.59).

    frame     vi45 (control)          vi47 (202)
    1575      red .69  blue .00       red .65  blue .00
    1600      red .64  blue .00       red .67  blue .00
    1625      red .65  blue .00       red .67  blue .00
    1650      red .69  blue .00       red .64  blue .03
    1675      red .64  blue .00       red .66  blue .60

The control never shows the plane. vi47 shows it -- at 0.60, the arcade's
figure -- but a hundred frames after the red field appears, which is the
latency 202 flagged: `pscene_nomatch` counts palette LANDINGS (the detect
runs only on K-vints) and a cutscene lands few.

**203: age the miss per vint, for the refuse decision only.**
`mds_miss_age` starts at the first no-match landing, is bumped once per
vint in disp_gate, and is cleared by any matching landing. The refuse rule
now also lifts when it passes 16. The scene LOADER keeps its 16-landing
rule untouched -- that one drives a full palette image load and its
threshold was set against fades (LOOP29 v1.1).

**Risk, stated:** a fade longer than 16 vints now lifts the refusal for its
duration, so sets outside the table can take dynamic lines during a fade.
They are few, the pins come back at the next matching landing, and it is
the pre-vi38 behaviour for that span.

`rom/night/vi48.32x` (md5 6cfbba99) = vi47 + this, flag-identical, staged
on the rig, not launched. Latency measurement at 5-frame steps pending.

**Seen in vi47's 1675 frame and not explained:** black rectangles inside
the blue plane where the arcade has dark-blue chevron tips (pens 6-7 of
set 19 are 005 005). Either those pens were not claimed or their tiles had
not shipped; not measured.

## 204. vi48 FAILED, vi49 PASSES: THE LIFT HAS TO DROP THE PINS TOO (2026-09-12 01:50)

**vi48 never showed the plane.** 5-frame sweep 1540-1700 and 10-frame
sweep 1700-1900: blue-upper 0.00 on every red-field frame, then the scene
ends. Lifting the refusal alone gives set 19 nowhere to go: a pinned set is
never freed (m_main.c 1759) and never evicted (2255), and round 0's table
leaves two free slots across three lines. vi47 only worked because its
lift came through the detector's unknown path, which ALSO clears the pins.

**vi49 does the whole foreign transition on the aged miss** -- the same
three lines the detector runs after 16 landings (11176): pins cleared,
mds_scene_cur = 0xFF -- 16 vints after a no-match landing. The refuse
condition goes back to `mds_scene_cur != 0xFF`.

    our frame   vi49 blue-upper      (arcade reads 0.59 on this test)
    1585-1600   0.00
    1605-1610   0.03                 first cells shipping
    1615-1680   0.59-0.60            the plane, at the arcade's share
    1685        scene over

The red field is on screen from 1585 at the latest, so the plane is up
within ~30 frames of the cut: 16 vints of ageing plus the 800-cell page
shipping at MDBATCH=24.

**The black inside the plane is the arcade's.** Same test on ref_arcade
1104-1160: black-upper 0.28; ours 0.22. The chevron plane has black tips by
design (set 19's pattern), and 203's "not explained" is closed.

`rom/night/vi49.32x` (md5 6eca15cb) = vi47 + 204, flag-identical to vi45,
staged on the rig, not launched. vi48 is withdrawn.

**NOT yet measured:** whether a long fade in play now clears the pins and
brings back churn mid-level. A vi45-vs-vi49 divergence sweep over the
level-1 demo (our frames 500-1560) is running.

## 205-206. THE RETURN FROM A FOREIGN SPAN, AND THE FLAMES (2026-09-12 01:50)

Mike on vi47: level 1 with a black band across the wall, "the player and
wolf are floating", before AND after the transform (054102, 054137). Then
on vi49: "you fixed the chevron! now the flame color is missing" -- the
face scene has the plane, the flames are flat red (054444), and the
opening "rise from your grave" has black blocks across the temple (054317).

**Both are the allocator's state around the foreign transition.**

**205 -- the return.** When the span begins the round's pins drop and its
sets get evicted by the cutscene's. Coming back, the round's table is
re-installed only by the detector's CONFIRMED path, three consecutive
matching landings, and in play the level lands its palette rarely: vi47's
band was still black 20 s after the transform. The round channel is still
valid the whole time, so on the FIRST matching landing while
mds_scene_cur == 0xFF, install md_round's table (detect block, 11170).
Headless ares does not reproduce the band -- the attract's level start is
a full palette load, and vi45/vi49 read the same black share (0.046-0.069)
from frame 2200 on -- so this is the play path, Mike's eye only.

**206 -- the flames.** With the pins dropped the round's sets are still
RESIDENT, and the LRU rule (>= 12 windows) will not evict them yet, so the
cutscene's sets 20-21 (the yellow-to-red flame ramp, 6 colours) land on
nearest-colour pens: the field's red. Earlier builds had coloured flames
only because the refusal kept them OFF the MD plane and the framebuffer
drew them. Fix: FREE every resident set at the transition (disp_gate).
Nothing is lost -- 205 puts the round back whole on return.

    vi50 (md5 b9189cf7) = vi49 + 205
    vi51 (md5 180f6664) = vi50 + 206     both flag-identical, staged

Measurement of vi51 (plane timing, flame share vs the arcade, black share
after the cutscenes) pending.

**206 measured (headless ares, vi51):**

    face window     red field from 1590; plane 0.12 at 1610, 0.60-0.64
                    from 1620 to the end of the scene (arcade 0.59)
    flame share     0.03-0.08 during the scene (arcade 0.01-0.06 on the
                    same test; vi49's flat-red flames would read ~0)
    after the eye   black share 0.048-0.058 at 2200-2400 (vi45: 0.046-0.059)

Plane up ~30 frames after the cut, flames coloured, no residue in the
demo that follows. **vi51 is the candidate for Mike's eye.** What headless
cannot show: the black wall band in PLAY after a transform (205) -- the
attract never takes that path.

## 207-209. vi51's STUTTER MEASURED, THE PALETTE DETECTORS RETIRED, AND THE CUTSCENE KEYED ON THE SCREEN (2026-09-12 02:35)

Mike on vi51: "LOST and lots of inconsistent frame stutter", then "background
palette also off, and leftover zeus letters in gameplay". Every number here
is headless ares; the coined path is `tools/inputs/coin_start_a.csv`, the
play path a coin+start+walk recipe, flips from `--trace-flip`.

**207 -- the stutter, measured.** Flips per 100 frames on the coined path:

    vi45   29 100 57 100 67 69 97 18 50 49 43 50 50 50 50
    vi49   33 100 47 53 48 26 11 11 12 11 11  8 21 50 46
    vi51   33 100 47 53 48 25 10 11 11 10 11 12 11 11 10
    vi52   33 100 48 50 46 28 11 11 11 11 11 10 12 12 10

A 5x collapse from frame 500, in every build since vi47. The allocator's
own counters (MDALLOCWHY probes) on the same card: vi45 refuses 2.4k cells
in 600-1200, vi49-52 refuse 129k -- EVERY cell on screen, every frame,
drawn as backdrop and re-claimed. Side by side at 300-440: vi45 has
md_round=255, no table; vi49-52 have round 0 INSTALLED AT THE TITLE
(MDS[5]=1, the retry path). The retry install needs the pscene detector to
have confirmed 'normal' -- which it does AT THE TITLE in vi45 too
(pscene_cur=0 at 300, the 8-pair probes match there, the code says so) --
and then the 1024-word distance to pass, which is timing luck: PAL_SH on
ares is fed by a FIFO that drops pushes, and a build-layout shift changed
which way it fell. **The title install is a latent hazard my builds
exposed, not one they created. 205's landing install made it certain.**

**208 -- the palette is the wrong signal for a cutscene.** The face, eye
and intro switch tilemap PAGES (0xAAAA/0xBBBB at 0x410E80) and leave the
level's palette words alone. So the 8-pair probes match at the title, the
1024-word distance fails inside the cutscene, and 202-206's aged miss and
pen-match test either fired at the title or re-installed five times in
fifty frames during the face. All of it is retracted. **The signal is what
the allocator already sees: the claim mix.** Per window, cells whose set
is in the installed round's table (t) against cells whose set is not (n).
A round's screen is nearly all t; every cutscene is nearly all n.

**209 -- the mechanism, from vi45's source.** `mds_onscreen = (t > n)`,
evaluated once per vint in disp_gate from a per-CELL count (vi54 counted
claims only and read stale mid-level; a steady screen claims nothing).
Keyed on it: the refuse rule (refuse only while on), the pins (a pinned
set may be freed/evicted only while on), the retry install (never while
off -- never at the title), the eviction age (an off-screen pinned set has
no age to wait out), the cut hold (arms only while on), the ship batch
(40 while off: the animating flames starved the plane's 32 patterns for
60 frames at 24; at 40 it is up on the FIRST red-field frame). The edge
back re-installs the round's table, selective, pins whole; before the
first install the published round classifies, which broke vi53's
chicken-and-egg (nothing installed by 1200).

    build   coin flips (vs vi45)      play path black share   plane
    vi53    identical                 worst diff 0.001        none (no table ever)
    vi54    identical                 worst diff 0.001        1660
    vi55    identical                 worst diff 0.001        1640
    vi56    identical                 worst diff 0.001        1640  (age gate: not it)
    vi57    identical                 worst diff 0.001        1640  (cut hold: not it)
    vi57@40 --                        --                      1590  (shipper: IT)
    vi58    pending                   pending                 pending

Counters on vi55/57 across the face: on-screen drops by 1580 and holds,
no install until the demo returns, no drift frees, no evictions after the
edge; the cut hold stops blanking by 1600 and the plane still waited on
the shipper.

`rom/night/vi58.32x` (md5 6e46bc5a) = all of 209, flag-identical to vi45,
staged on the rig, not launched. vi46-vi57 are withdrawn.

**vi58 measured (headless ares):**

    coin flips/100   vi45 29 100 57 100 67 69 97 18 50 49 43 50 50 50 50
                     vi58 30 100 58 100 76 84 96 11 51 49 43 49 50 50 50
    face             red field from 1575; plane 0.13 at 1580, 0.29 at 1595,
                     0.63 from 1600 to the end of the scene (arcade 0.59);
                     flames 0.03-0.08 (arcade 0.01-0.06)
    demo after eye   black share 0.057-0.064 (vi45 0.046-0.059): ~1% more,
                     the re-install's re-conversion; not measured which cells
    play path        worst black-share difference vs vi45: 0.001

**vi58 is the candidate.** Not shown by headless: the level after a
transform in PLAY, and whether the batch-40 span during a cutscene tears
on hardware.

## 210-211. THE ATTRACT'S WRONG PALETTES: THE IMAGE LOAD AT THE TITLE, AND A DANGLING SET (2026-09-12 03:55)

Mike on vi58: "the titles sliding in are swapped to wrong palette, the
logo in better shape but not perfect", and "the arcade score has honestly
never displayed". The arcade at those frames (MAME, tile+text+palette
dumps at 294 and 2114): page regs 0x1212/0x6767; the logo is page 2, sets
37-46 (a white-with-blue-outline ramp: 7FFF 7FFF 7FFF 0730 0730 0730
0400); the texture under both scenes is page 7, set 11, 800 cells; the
ranking text is the TEXT layer, 25 cells. Colour demand: 5 quantised
colours at the logo, 3 at the ranking. Capacity is not in play.

**210 -- the logo, and the ranking text: the detector's image load.** In
the SH-2's palette mirror at vi58 f340 and f3300, sets 37 and 40 hold the
level-1 image's blue ramp word for word (0FB0 6D90 0C80 0B70 0950 0730
0400) where the arcade title has white. The 8-pair probes match at the
title (the game preloads the level words they sit on -- the code says so
at the install guard), and on the third confirming landing the window-side
code copies the whole scene image, tile half AND text half, over PAL_SH
BEFORE the `mds_dist <= MDS_TOL` guard that protects the MD install. So
the logo's ten sets were drawn in level 1's set-37 colours, and the
ranking's text in level 1's text colours. vi59 puts the copy under the
same guard. Not yet measured: whether real scene cuts still get their
load (they should: the new palette lands before its third confirming
landing, so the distance is small there).

**211 -- the ranking texture: a dangling assignment, OPEN.** Set 11's
mirror words are correct (0C80 FFFF 0B70 ...), so the green comes from the
pens. At f3150-3350: set 11 on line 1, which has 14 FREE pens, with pixel
map [3,0,3,0,2,0,2] -- pixels 1 and 3 on pen 3 whose line_c is 0xFFFF
(free), pixels 2/4/6 (the white words) on pen 0 (transparent, so black),
pixels 5/7 on pen 2 = 0x0E0 = (0,4,3) where the word wants (0,4,6). Three
things wrong at once: a mapped pen that is free (a shared pen released
under set 11 -- refcount), unclaimed pixels (the mask never grew), and a
nearest-colour pen on a line with 14 free. All three point at the dynamic
assign/extend/free path when a set claims during the off-screen span
while pinned co-owners are being evicted. The ranking rendered BLACK on
every build before 209 (refused), so this is the first time it has drawn
at all. Left open; the counters to read are [19]-[21] (burned claims),
[25]-[28] (tagkeep), and mdp_pen_rc for line 1 across 3100-3300.

`rom/night/vi59.32x` (md5 1514f850) = vi58 + 210, flag-identical, staged.

**210 RETRACTED (04:10).** Set 37's words across the slide-in, MAME, arcade
vs ours, sampled every 20 frames:

    arcade  180-200 0FB0 6D90 0C80 | 220 0C80x3 | 240 6EA0 0D90 6C80 | 260-280 0FB0 0D90 0C80 | 300 100Fx3 | 320+ 000F 000D 000D
    ours    180-200 same            | 220 0B70x3 | 240 6D90 0D90 0C80 | 260-300 0FB0 0D90 0C80 | 320 100Fx3 | 340+ 000F 000D 000D

Identical sequence, ours ~20 frames behind. The writer census over sets
37-46 shows the same two writers on both (0x2628 and 0x3976), ours firing
MORE. And in ares the 68K's own palette RAM at 0xFF9000 equals the SH-2
mirror at every sampled frame. So the "level-1 blues" I read in the mirror
at f340 are the game's own pre-flash logo colours, reached later in ares
because the game paces on presented frames. **The image load was not the
cause and vi59's guard fixed nothing** -- it is kept only because it is the
same guard the install already has and measured identical on flips, play
and the face. What "swapped palette" on the rig actually is needs a
capture at the SAME GAME PHASE, keyed on set 37's words (blue / 100F /
000F), not on a frame offset. Not done tonight.

The ranking text (211) is likewise NOT explained by the load; its text
palette needs the same phase-keyed look. 211's texture finding (the
dangling set-11 assignment) stands: it was read from the allocator's own
tables, not from a frame offset.

## 212-214. HYSTERESIS FAILED TWICE; THE OFF-SCREEN BATCH IS A KNOB (2026-09-12 04:15)

Mike on vi59, "mostly just missing tile issues": level 2's top band has
vertical streaks -- the documented 40-tile vblank overrun on hardware --
plus a black block; the round-clear text garble is pre-existing (vi44
041722 had it) and the crystal-ball scene is purple on the arcade too.

**Headless does not show the band.** The level-2 attract demo on vi59/60
renders clean at 3800-4700 (frame 4200 viewed: ceiling, stalactites,
dragon, no streaks). What that sweep DID show: vi45's level-2 demo is
more than half black (black share 0.53-0.61, luma 49-65) where vi59
renders it (0.042, luma 103-113) -- the old refuse rule with round 0's
table pinned through the second demo. 209 fixed level 2's demo outright.

**212, vi60 -- off needs n >= 4t for 3 vints:** plane partial (0.19) from
1595 to 1630, full at 1635 (vi58: 1600). Flips and play identical.
**213, vi61 -- and on needs t >= 64:** plane partial (0.12-0.19) from
1620 to 1665, full at 1670; play path worst black diff 0.011 at 1360
(vi58/59: 0.001). Both WITHDRAWN: any damping makes the flag sluggish in
the face, the one scene it exists for, and 213 leaked into play.

**214:** vi59's one-window flag logic restored; the off-screen ship rate
is `MDBATCHOFF=N` (default 40). Two builds staged for Mike to rank the
hardware tear against the plane's arrival:

    vi62   (md5 34810ad3)  MDBATCHOFF=40   plane on the first field frame
    vi62b  (md5 3882eb1c)  MDBATCHOFF=24   plane ~60 frames later, no
                                           40-tile DMA in a cutscene

**The principled signal, for daylight:** LOOP-DECOMPILE's WRAM byte
0xFFF148 is non-zero exactly when 0x3A00 puts pages 10/11 on screen. The
68K shim already publishes the round in COMM10 bits 13-15; carrying that
byte would make the cutscene edge instant and exact and retire the claim
mix. Not built tonight.

**214 measured (headless ares):**

    vi62   coined flips/100 32 100 57 100 77 77 98 13 50 50 46 47 50 50 50  (vi45: 29 100 57 100 67 69 97 18 50 49 43 50 50 50 50)
           play path worst black-share diff vs vi45: 0.002
           face: red field from 1570, plane full from 1590   (20 frames)
    vi62b  face: red field from 1600, plane full from 1650   (50 frames)

vi62 reproduces vi59's numbers. The A/B is the hardware tear, which only
the rig shows, against those 30 frames.

**214 RANKED ON THE RIG (Mike, 04:27).** vi62b: two level-2 shots, top band
clean where vi59 (batch 40 off-screen) had the streaks. So the tear was
the 40-tile transfer during the off-screen span and 24 is the setting.
`MD_BATCH_OFF` defaults to 24 from here; vi62b (md5 3882eb1c) is the
candidate and the plain build line now produces its behaviour. Cost: the
chevron plane arrives 50 frames after the field instead of 20. Still open
on vi62b's shots: a black cell or two in the level-2 floor -- the pop-in
class, MDBATCH=24 in play.

## 215. THE INVISIBLE PLATFORM: THE BAKE READ ONE PAGE PER PLANE (2026-09-12 04:50)

LOOP-DECOMPILE 98 / NOTES 19: the ledge Mike stands on is FG masonry in
sets 82, 87-91 -- 1,226 cells of level 1 on pages 1-4 -- and none of them
was in round 0's table, so the refuse rule drew them as backdrop.
`bake_tilecram.py`'s viewport sweep read FG page 0 and BG page 5 only;
the level's planes walk pages 0-4 and 5-9. `tools/scene_sets.py` (theirs)
prints the gap per round.

**The bake now packs the union of every set on all five pages of each
plane**, and orders the packer by CELLS so what overflows is small: the
first rebake put 100/101 (2,012 BG cells) in the framebuffer to fit a
15-cell set. Result against their tool and the arcade oracle:

    round 0   30 sets pinned, lines [14,14,15]   overflow 2, 102, 103 = 211 cells (was 1,553 missing)
    round 1   15 sets, [15,14,15]                overflow 3 = 41 cells
    round 2   16 sets, [14,15,12]                overflow 3, 101 = 7 cells
    round 3   14 sets, [15,9,11]                 fully covered
    round 4   19 sets, [14,15,11]                fully covered
    all five  0 wrong colours, 0 dropped pens vs discover/cram/arcade

`rom/night/vi63.32x` = vi62b's line + these tables, staged, not launched.
Headless checks pending. R60TIGHT (their flag, NOTES 18) is not in it.

## 216-217. THE ALL-PAGES TABLE STARVED THE PLANE OF SLOTS; FREE AT THE EDGE OUT, INSTALL THE PUBLISHED ROUND AT THE EDGE BACK (2026-09-12 13:50)

vi63 (215's tables) regressed the face: the plane drew in ALTERNATE ROWS,
blue-upper 0.13 for the whole scene. Dumps at 1590-1660: flag 0, set 19
on line 1 with pens 180/1C0/140 -- the RIGHT blues, shared exact-match
with pens the bigger table already held -- every cell slot-hit, no
evictions, no frees. Same probe code with vi62b's table: 504 tag wipes
in 1610-1640, plane full at 1660. **The slot cache never evicts a live
tag on its own; it only reuses slots released when a set is freed.** With
30 pinned sets resident and set 19 needing no eviction for pens, nothing
was freed, no slots came back, and the plane got only the slots that
happened to be free. vi62b worked by luck of colour sharing.

**216 (vi64):** at the edge out, free every resident set -- off screen is
off screen -- so the cutscene has slots and exact pens; the edge back
re-installs. Plane full at 1650 against a field at 1605. Play path vs
vi45: worst black diff 0.001; ledge band below vi62b's. BUT the demo
after the eye paid 10.6-11.2% black for 300 frames (vi63: 3.7%).

**217 (vi65):** the edge back installed `md_round`, the REMEMBERED round
-- level 1's, while the demo after the eye is level 2. Install the
PUBLISHED round (MD_ROUND_GET), fall back to the remembered one only when
none is published; classify the claim mix the same way.

    vi65   coined flips/100  32 100 57 100 67 67 96 16 50 47 43 50 50 50 50   (vi45 29 100 57 100 67 69 97 18 50 49 43 50 50 50 50)
           face: red field from 1570, plane full from 1625
           demo after the eye: black 0.037-0.038  (vi45 0.046-0.059, vi64 0.106)
           play path: pending

`rom/night/vi65.32x` (md5 324deb7a) staged, not launched. vi63/vi64
withdrawn.

**217 play path:** vi65 vs vi45 play path: worst black-share difference 0.001 at frame 720. vi65 is the candidate.

## 218. RELEASE THE SLOTS, KEEP THE PENS (2026-09-12 14:05)

Mike on vi65: "missing black tiles after the screen is redrawn from the
wolf transition, persistent until the end of the first level" -- a band at
the horizon (rows 6-7: FG set 74, BG set 92, both in the table). The two
other items in the same shots are pre-existing: the round-clear text
missing letters (vi44 041722 had it) and the black bar on the crystal-ball
screen (vi59 080418 had it).

The sky palettes 0-7 are NOT it: the game zeroes them on the rise of
0xFFF148 and restores them on the fall, and both mirrors (68K 0xFF9000
and PAL_SH) read the rom words within a frame of the fall (measured on the
attract's face, f1715).

What the attract never exercises is a SAME-ROUND return: every edge back
headless lands on a different round or a full level load, where the game
re-uploads the tilemap and every cell is re-claimed. The transformation
returns to level 1 with its tilemap intact. 216's edge-out FREED the sets
-- lines and pixel maps gone -- and something in that re-assignment stayed
wrong for the rest of the level. A tag wipe is the residency loss the
drift-free path already recovers from in play (1730), and it is all the
cutscene needs (the slots). So the edge out now calls mdp_wipe_set_tags
on every resident set and leaves the pens alone.

    vi66   coined flips/100 32 100 57 100 67 67 96 16 50 46 41 50 49 50 50   (vi45 29 100 57 100 67 69 97 18 50 49 43 50 50 50 50)
           face: red field from 1570, plane full from 1570 -- the first
                 field frame, at batch 24: slots free, pens already exact
           demo after the eye: black 0.037
           play path: pending

`rom/night/vi66.32x` (md5 a3e3509a) staged, not launched. The transform
return is Mike's eye only. vi65 withdrawn.

**218 play path:** vi66 vs vi45 play path: worst black-share difference 0.001 at frame 2720; ledge band mean black: vi62b 0.0124  vi66 0.0105. vi66 is the candidate; the transform return is Mike's eye.

## 219. THE HORIZON BAND IS HARDWARE-ONLY, AND IT PREDATES THE TRANSFORM (2026-09-12 14:15)

Mike on vi66: "no change". His five shots all carry the band, INCLUDING
the one before any transformation (180545, score 5800, human form). So
216-218's same-round-return theory is wrong: the band is there from the
start of level 1 on vi66 and absent on vi62b.

Headless at the same rows: vi62b 0.00 black on rows 40-76, vi66 0.00; the
rig's 180545 reads 0.42-0.70 on rows 48-76. **ares does not show it.**
That leaves what hardware does differently -- a VRAM transfer cut short
when it overruns vblank leaves tiles our residency map believes shipped,
black until something re-ships them, which nothing does. Two things
changed vi62b -> vi66 that bear on that: the all-pages table (215: 30
pinned sets, more resident tiles) and the edge-out tag wipe (218: every
tile re-ships at the level load).

Staged for the rig, not launched, both otherwise vi66:

    vi66a  (md5 c93d0373)  vi62b's page-0 table + the tag wipe
    vi66b  (md5 3a72ffcb)  the all-pages table, EDGENOWIPE=1 (no wipe; the
                           chevron plane will draw in alternate rows, 216)

If vi66a is clean, the table's size is the trigger; if vi66b is clean, the
wipe is. If both band, the trigger is elsewhere in 215-218 and vi62b is
the line again.

## 220. vi66b HOLDS THE FIX; THE WIPE IS NARROWED TO THE CUTSCENE'S OWN SETS (2026-09-12 14:30)

Mike: "rom/night/vi66b.32x holds the fix" -- the horizon band is gone with
the tag wipe off (219's A/B). His three remaining items: the round-clear
text (pre-existing), "missing tiles after defeating Neff" (not visible in
the three shots; needs a frame), and the orb's black bar (pre-existing).

**216's reading was wrong.** The slot cache DOES evict: on a tag miss with
a full cache set it takes the LRU way (`md_ref` age, the block after "set
full: evict the LRU way"). The counters from the striped face (vi63)
already said so: [1] slot-hit ~= [0] cells, 2-3 evictions in 30 frames.
The plane's cells were HITTING tags -- tags for those exact tiles from the
previous attract loop, patterns converted under whatever pen map set 19
had then. The mass wipes in vi64-66 fixed the plane by taking those stale
tags with everything else, and the everything-else is what bands on
hardware (a cut transfer at the load leaves tiles our map calls shipped).

**220:** at the edge out, wipe the tags of resident sets that are NOT in
the published round's table -- the cutscene's own -- and nothing else. The
level never re-ships; the plane's stale patterns re-convert.

`rom/night/vi67.32x` (md5 2857e844) staged, not launched. Headless checks
pending. vi66/vi66a/vi66b withdrawn (vi66b's behaviour is what 220 keeps
for the level, plus the plane).

## 221. THE SAME-ROUND RETURN EXISTS HEADLESS, THE FULL WIPE IS RIGHT, AND THE HARDWARE HAZARD IS THE BATCH (2026-09-12 14:30)

vi67 (220's narrowed wipe): plane striped again (max 0.13), AND the window
at 2150-2450 read 10% black -- and that window is the attract's SECOND
LEVEL-1 DEMO (frame 2300 viewed: the graveyard with black rectangles in
the tree row), i.e. a same-round return after the face and the eye. So
headless does reach the class Mike sees after a transformation:

    second level-1 demo, black share    vi65/vi66 (full wipe)  0.037
                                        vi67 (narrowed wipe)   0.106-0.111
                                        vi64 (stale round)     0.106-0.112

The full wipe at the edge out is what keeps that return clean, and 220's
"wipe only the cutscene's sets" cannot work for the plane either: set 19
is assigned AFTER the edge, so it is never resident when the wipe runs.

Mike's rig says the full wipe bands (219, vi66 vs vi66b). Both are
measured, so the change has to keep the wipe and remove the hardware
hazard from the re-ship: with the wipe, the load ships the whole level and
the blanked path sends 40 tiles a vint, the documented vblank overrun.
**221: the blanked load ships at MD_BATCH_OFF (24) too; 40 is gone from
every path.** vi68 = vi66 + that. Cost: a slower load-in (1120 tiles at 24
= 47 vints against 28). `rom/night/vi68.32x` (md5 07f8075b) staged, not
launched; headless checks pending. vi67 withdrawn.

## 222. THE MAILBOX ROUND WAS DIRTY, AND THE STALE-TAG WIPE BELONGS AT ASSIGN TIME (2026-09-12 14:35)

vi68 (full wipe + blanked load at 24): the same-round return 10% black
AND the coined flips down early (20 vs 29, 45 vs 57 in the first
windows) -- the load STALLED at 24, the batch accounting assumes 40 there.
Withdrawn; the blanked load is back at 40.

vi66b measured headless on the same scene: 10% black, plane striped. And
the pair that isolates it: vi63 and vi66b at frame 2300 are the SAME
scene (the attract's second level-1 demo, viewed), vi63 clean (0.037),
vi66b black rectangles in the tree row. Their only difference is 217.

**217 read the round straight from the mailbox, and the mailbox is
dirty.** The 68K builds COMM10 as `word@0xFFB9FE | (round << 13)`, and
that word is the 16-region palette-dirty mask: regions 13-15 are the
actor lines, dirty whenever a sprite palette moves. So the round arrived
as round|dirt -- a wrong table installed on the return, the level's sets
refused, black rectangles. The old code trusted the REMEMBERED round, set
only at guarded installs, and was immune by accident.

**222:** (a) the 68K masks the dirty word to 0x1FFF before OR-ing the
round, at all four post sites in md_main.c; (b) no wipe at the edge out at
all -- 218's full wipe bands hardware (219) and 220's narrow one never
reaches the plane's set; (c) a set ASSIGNED while the round is off screen
gets its tags wiped in mdp_assign_set, which is exactly when the plane's
set appears and touches nothing of the level.

`rom/night/vi69.32x` (md5 46c2a886) staged, not launched; checks pending.
vi67/vi68 withdrawn.

## 223. TWO CAUSES ON THE SAME-ROUND RETURN, BOTH ISOLATED; vi70 (2026-09-12 14:40)

vi69 (222): plane full at 1630 -- the assign-time wipe does its job -- but
the return STILL 10% black. vi69 with `ASSIGNNOWIPE=1`: the return 0.037.
So the return had two stacked causes: the unmasked mailbox round (222,
fixed by the mask -- vi66b's 10%) and the assign-time wipe itself: in the
window before the flag comes back on, the level's own sets are re-assigned
while the round still reads as off screen, and wiping THEIR tags there
left the holes.

**223:** the assign-time wipe excludes the published round's table sets.
Only a cutscene's sets -- the plane's, the eye's -- ever lose tags.

    vi70   coined flips/100  32 100 60 100 67 63 89 21 50 47 41 50 49 50 50   (vi45 29 100 57 100 67 69 97 18 50 49 43 50 50 50 50)
           face: red field from 1600, plane full from 1615  (15 frames, the best yet)
           second level-1 demo (same-round return): black 0.037-0.038
           play path: pending

No edge-out wipe anywhere (218/220/221 all withdrawn), so nothing
re-ships the level in play: 219's hardware band has no trigger left.
`rom/night/vi70.32x` (md5 fadafb08) staged, not launched. vi69 withdrawn.

**223 play path:** vi70 vs vi45 play path: worst black-share difference 0.002 at frame 1600; ledge band mean black: vi62b 0.0124  vi70 0.0106. vi70 is the candidate.

## 224. vi70 ACCEPTED ON THE RIG (Mike, 2026-09-12 14:50)

"Presentation is solid until the final scene with the round clear text
missing tiles and text. Otherwise SOLID presentation." vi70 is the line:
`rom/s16.32x` = `rom/night/vi70.32x` (md5 fadafb08). Build line:

    make ship-us MDSTATIC=1 MDROUND=1 MDSREFUSE=1 FBXPORT=1 FBXSTAGE=1 \
        FBXPEND=1 FBXISRLIFT=1 PGSKIPPKT=1 GAMEGATE=1 GAMEGATEWAIT=1 \
        TEXTCAPMASTER=1 TEXTCAPEARLY=1 TEXTCAPFULL=1 PENHOLD=1 PENREPAINT=1 \
        TAGKEEP=1 MDSPRTOP=1 NBUILD1=1 MDBATCH=24

(MDBATCHOFF defaults to 24, 214.) Open, in order:

  1. The round-clear scene: text-layer glyphs missing ("RO D CL AR BONU",
     pre-existing since at least vi44) and missing tiles in the same
     scene. The text path (TEXTCAP*) has not been touched by 196-223.
  2. The decompile thread's step 2: wire `sh_src/setcols_md.h` into the
     maps drain and measure the scan-versus-tail split (NOTES 21).
  3. Attract only: the logo slide-in palette and the ranking screen (210-211).
  4. The principled cutscene signal, 0xFFF148 through the mailbox (214).

## 225. THE ROUND-CLEAR TEXT: A TYPEWRITER OUTSIDE EVERY FM GATE (2026-09-12 14:50)

Mike's one open item on the accepted vi70: "the final scene with the
round clear text missing tiles and text" -- "RO D CL AR BONU", the same
holes every time.

The text word's low byte is ASCII ("CREDITS 3" in text RAM reads 43 52 45
44 49 54 53), so the string is in rom as text: "ROUND CLEAR BONUS " at
0x7076, "CLEAR"/"BONUS" alone at 0x1BB7E/0x1BC0C/0x7082. One reference,
`lea %pc@(0x7076),%a0` at 0x64C2, inside an OBJECT state machine (slot
0xFFD400, states chained through fp@(2)):

    0x64DA  every 5 frames: movew d1,a1@ at 0x6504 -- ONE glyph of
            "ROUND CLEAR BONUS " into text RAM (0x41033C + 2n), 18 of them
    0x6536  same for the points string at 0x7088 + 14*round, 10 glyphs

Written once, never rewritten. Both are main-loop framebuffer stores
(text RAM is rebased into FB text staging on this line) OUTSIDE every
FMGATE span -- the gate derivation was censused on the attract, and the
attract never round-clears. A glyph whose 5-frame slot lands while FM=1
is dropped (the FB-write rule of the flip-latch entry) and stays missing.
The positions vary run to run because the phase does; Mike's two
readings differ by one letter.

**225:** two FMGATE entries, 0x64DA and 0x6536 (displaced `subqw
#1,fp@(34)`, 4 bytes; the bcs that follows reads its CCR and the thunk
runs the displaced instruction last), with spans (0x64DA,0x6510) and
(0x6536,0x656C). fmgate_derive's lists carry the two store sites.
FMGATE_THUNK_WORDS 222 -> 236.

`rom/night/vi71.32x` (md5 8d934147) = vi70 + 225, staged, not launched.
Rig only: neither emulator drops the FB write, so headless cannot show the
glyphs coming back. Sanity checks (flips, play, face, return) pending.
The "missing tiles" in the same scene are not yet located.

## 226. THE TRAMPOLINE POSTED THE ROUND FIELD RAW (2026-09-12 14:55)

vi71 (225's two gates, a 68K-only change that never fires in the attract)
read 10% on the same-round return where vi70 reads 3.7%, reproducibly
(both re-run: vi70 0.037 x7, vi71 0.101-0.114 x7), with the RIGHT table
installed (md_round 0, flag on, 30 sets with lines). The only 68K change
moved the thunk block's tail by 28 bytes.

**The second COMM10 writer.** `md_start.s` fmgate_partb posts
`move.w (0xFFB9FE),(COMM10)` -- the raw 16-region dirty word, no round,
no mask -- on the vints where the assembly path raises FM. 222 masked the
four C sites only. So the round field carried regions 13-15 (the actor
lines) whenever THIS path posted, and whether the SH-2's read at the
edge back caught this path or the C path was a matter of phase: the
28-byte shift flipped it. **Every "flaky same-round return" since 217 is
this one hazard**, and vi70 was clean by phase.

**226:** the trampoline posts `(dirt & 0x1FFF) | (round << 13)`, the same
word as the C sites. Three short branches to the block's exit labels
widened to .w for the extra bytes.

`rom/night/vi72.32x` (md5 6ea6aabb) = vi71 + 226, staged, not launched.
Checks pending.

## 227. vi72 STATUS: THE TYPEWRITER GATE IS IN; THE ATTRACT'S SAME-ROUND RETURN MOVES WITH ANY 68K CHANGE (2026-09-12 15:00)

    build   68K change                          plane full   second L1 demo black
    vi70    --                                  1615         0.037 (x7 rerun)
    vi71    +2 FMGATE entries (never run here)  1635         0.101-0.114 (x7 rerun)
    vi72    +226 trampoline mask                1685         0.104-0.125
    coined flips: vi71 and vi72 both match vi45; vi72 vs vi70 play path: worst black-share difference 0.002 at frame 1600

226 did not move the return, so the trampoline post was not its cause
either -- though the fix is right by construction and stays. Two gate
entries that never execute in the attract change the return
deterministically, and the plane's arrival moves 50 frames with a mask
in the vint path. **The attract's same-round return is PHASE-SENSITIVE
to 68K-side layout and timing**, and vi70's clean 3.7% is a phase it
happens to sit in. What the black rectangles ARE on that return (tags,
slots, text cells?) is not established; every mechanism I named for them
since 216 has been retracted by a measurement.

**What only the rig decides for vi72:** the round-clear text complete
(225; neither emulator drops the FB write), and level 1 after a
transformation (the same-round return in play). If both are clean, vi72
is the line and the attract figure is noise. If the horizon bands, vi70
stays the line and the typewriter needs a different vehicle (route it
to the WRAM text mirror, TXTWRAM, instead of gating it).

`rom/night/vi72.32x` (md5 6ea6aabb) staged, not launched.

## 228-229. THE RETURN'S BLACK TILES ARE A DEFERRED-WINDOW RACE, PHASE-SENSITIVE TO ANY 68K CHANGE (2026-09-12 15:20)

Mike on vi72: "screen text issue resolved. missing black tiles
reintroduced." Then: "I don't think it's a regression, just another
timing issue overlooked" -- and the measurements agree.

`fmgate_defer` (68K WRAM, md_start.lst) counts windows the shim did not
post: COMM0 still busy from the previous window, or the game interrupted
inside a gated span. Across 2500 attract frames, with the same-round
return (second level-1 demo, aligned on its first bright frame, 2120 in
every build) beside it:

    build   68K change vs vi70                 fmgate_defer   return black
    vi70    --                                 0              0.037
    vi71    +2 gates, wide spans               0              0.101-0.114
    vi72    +226 trampoline mask               57             0.104-0.125
    vi73    narrow spans + 226                 57             --
    vi74    narrow spans, trampoline restored  53             0.093-0.107

No single change explains the deferral count (vi71 0 with wide spans,
vi74 53 with narrower ones and nothing else), and vi71 lost the return
with zero deferrals. **Both numbers move with the 68K's layout, not with
any one edit.** The window handshake has a phase-dependent collision --
the SH-2's window still open when the 68K wants to post, or the post
landing inside a guard -- and a deferred window is a frame whose marks
the SH-2 never sees: on a scene return that is cells never re-walked,
black to the end of the level. vi70 sits in a phase with 0 collisions.

**What stands:** 225's typewriter gate is CORRECT (Mike: the text is
complete on vi72) and lands in any of vi71-vi74; 226 is reverted (the
raw trampoline post was never the return's cause -- 222's C-side mask
stays). **What is parked:** shipping the text fix, until the collision
is fixed at its root, because any 68K change re-rolls the phase.

**The instrument:** `fmgate_defer` at frame 2500 of the attract must read
0 for any 68K-side change to be accepted; the same-round return
(2120-2520, black share 0.037) is its picture. The root is in the window
timing -- the pipeline work -- not in the tables or the gates.

vi70 stays the line. vi74 (md5 29e476db) is the text-fix candidate that
trips the race, staged for whenever the race is closed.

## 230. FOLD 1 BUILT: CAT1MD + C1NOFB ON vi70's LINE (2026-09-12 15:35)

PLAN-SINGLE-VINT fold 1 (8cb6de6): `CAT1MD=1 C1NOFB=1` on vi70's line
(DIRTYROW is in the ship set). vi75, md5 f1d8162f, staged.

    coined flips/100   vi45 29 100 57 100 67 69 97 18 50 49 43 50 50 50 50
                       vi75 31 100 59 100 67 63 89 17 61 56 50 50 51 57 69
    face               red field 1580, plane full 1575 (first frame)
    aligned return     0.044 0.048 0.050 0.045 0.042   (vi70 0.037)
    play path          black share = vi70 within 0.006; luma diverges
                       14-33 through the level because vi75 runs faster
    fmgate_defer       54 at f2500   (vi70 0)

**229's acceptance gate is RETRACTED as a gate.** vi71 read 0 deferrals
with a 10% return; vi75 reads 54 with a 4.5% return. The count is the
handshake's instrument (fold 3's subject), not a predictor of black
tiles, and it moves with SH-2-side changes too -- vi75 changes nothing
on the 68K. The picture gates stand: the face plane, the aligned return,
the play-path black share, the coined flips.

Mike's gate for fold 1 per the plan: the hole punch over sprites, where
the 32X layer wins per pixel. Rig only.

Motion rate (`tools/presented_fps.py`, same recipe as LOOP29 213):

    vi70   MOTION  8.6 fps   any-change 24.8 fps   windows [19, 10, 0, 8, 6]
    vi75   MOTION 11.4 fps   any-change 33.0 fps   windows [33,  8, 0, 9, 7]

Fold 1 buys motion, as the plan priced it: the tiles leave the FB path,
so the SH-2 drains sprites only. The first window (title slide-in) is
where it shows most, 19 -> 33. Third window 0 in both = the eye hold.
vi75 is on the rig; vi70 remains `rom/s16.32x`.

## 231. FOLD 1 ON THE RIG: SPRITES FAST, WHOLE TILE SETS BLACK -- HARDWARE-ONLY, TRANSPORT PROVEN INTACT, CAUSE STILL OPEN (2026-09-12 20:50)

Mike on vi75: "The BIG BIG WIN is player sprites and enemy sprites are
moving MUCH faster" / "you have NAILED sprite performance" / "The
background and foreground tiles aren't being updated properly". His
shots (193545-193850): level 1 with the temple facade, the pedestal and
the tree line BLACK, sky/pillars/grass drawn; the boss scene all grey
(that one is the arcade's own: ref_arcade 13640-15180 greys the picture
when Aggar arrives); round-clear text complete (fold 1 fixed it without
225's gates); level 2 correct.

**Identification.** Arcade ref_005400 (score 1700) is Mike's 193545. Per
set, from the rom tilemap (pages 0/5, tools/scene_sets.py unpacker):

    drawn   92 sky (line 3)   75 pillars/wall (2)   85/86 grass (1)   74
    black   76-79 temple/pedestal (2)   93 95 96 99 trees (3/2)   100/101 (2/3)

Not a line (both classes span lines 1-3) and not a wrong round table (no
round's table keeps {92,75,85} -- round 1 keeps 72-75, round 4 keeps
96-101). Per-cell, not per-set either: the leftmost pillar of set 75 is
black in 193545 while the rest of the row draws.

**Hardware-only, and the rig reproduces it unattended.** ares play run
(vi70 and vi75, /tmp/play.csv, frames 700-2000): tree band and FG band
0.00-0.03 black on BOTH. The rig, attract only, no input:

    launch                     curl POST :8182/api/games/launch {"path":...}
    shot                       ssh root@mister.office.local "echo screenshot > /dev/MiSTer_cmd"
    fetch                      scp root@mister.office.local:/media/fat/screenshots/S32X/<f>
    first level-1 demo         launch+14..28 s      second demo   launch+46..55 s

    rig, first demo      vi75  trees 0.20  fg 0.55-0.66   (195726-195737)
                         vi70  trees 0.00  fg 0.01-0.03   (195908-195920)
    rig, second demo     vi75  trees 0.02  fg 0.71-0.77
                         vi70  trees 0.00  fg 0.00

So the bug is fold 1's and the rig's, and every probe below ran on the
rig without Mike, ~2.5 min a round (build, ares check, push, 4 shots).

**The transport is intact (vi76-vi79, BOOTTILEVER=1, new).** A 68K-side
instrument (TILE_VERIFY, md_main.c consume) reads every tile record's
16 VRAM words back after its DMA and compares them with the FB source;
an SH-2 side (m_main.c TV_BITS) rides packet word 1 bits 8-12/14; the
value instrument floods the verdict. Read on the rig, vi75's line:

    VRAM != FB source            0        FS changed mid-consume       0
    slot out of range            0        SH-2 publish read-back != staging   0
    post-flip replay read-back != tp_lastA  0
    SH-2 FB writes at FM=0       0 (publish and replay)

The 68K consumes exactly what the SH-2 built. Whatever is black was
BUILT black on the SH-2 (blank/refused cells, or zero art).

**vi76-vi85 RETRACTED as evidence of the cause.** The same builds
counted "records whose 16 words are zero" and "emitter records with
all-zero output": 5-7+ on the rig within 20 s against 2 in ares -- but
the ares number was from the PLAY recipe. On the ATTRACT, ares emits
zero records too (4 by f600, 12 by f3000; 23 "non-blank-in-the-bank"
codes by f1680 whose pen maps map every used pixel to 0), and the 2-3
bit saturating counters could not tell the rig's count from that. The
chain (zero records mid-packet, ROM source zero cached and uncached,
code valid, tags coherent) measured the level's own blank tiles.
Lesson: match the scene before comparing a saturating counter, and
size the counter to the expected value.

**vi86 RETRACTED.** 226's masked trampoline post (the raw dirty word in
COMM10's round field on the vints the assembly path posts) re-applied
on vi75's line. Rig unchanged (trees 0.20, fg 0.54); ares' aligned
return went 0.045 -> 0.12-0.15 (the 68K change re-rolled the phase,
227). md_start.s reverted to 229's raw post.

**What the black is, then.** The SH-2's MD-plane state on the rig
diverges from ares by timing alone: which cells claim in which window,
what COMM10 reads at the edge, how often the claim mix flips
mds_onscreen, how many windows a vint gets (flips/vint 0.62 -> 0.91
vi70 -> vi75 in ares). None of it is observable on the rig with an
8-bit-per-capture channel that saturates. The next instrument is the
channel: four CRAM lines carrying four tagged 6-bit values (24 bits per
capture), then the allocator's own counters (MDA 30 refused, MDA 4/8
blanks, MDS[5] edges, md_round) at matched attract seconds against ares.

Fold 1 stands as Mike's speed lever (motion 8.6 -> 11.4 fps, sprites
"MUCH faster"). Presentation line stays vi70. The fmgate_defer count is
54 on vi75/vi86 alike in ares.

    rom/night/vi75.32x  fold 1 (md5 f1d8162f)        on the rig
    rom/night/vi76-85   TILE_VERIFY probes, withdrawn (see above)
    rom/night/vi86.32x  vi75 + 226 mask (md5 67372a94), withdrawn
    rom/s16.32x         vi70 (md5 fadafb08), the line

## 232. WHERE WE ARE, IN VINTS (2026-09-12 21:20)

Mike: "the metric I understand most from you is: vint. where are we and
what's next?" Measured, not recalled. `tools/nat_score.py`, ares, play2,
4000 frames, PHASECENSUS=1 on each line (pc70/pc75 = the same flags plus
the census; wall and ships agree with the census-free roms to 0.01):

    build   wall v/gen   single-vint   ships/s   echo   mtask   ship   flip
    vi70      1.48          18%         32.2     1.42   1.14    0.64   0.69
    vi75      1.11          44%         39.4     1.01   1.01    0.64   0.73
    bar       1.00         ~98%         60

Period bins (1/2/3/4+ vints): vi70 384/1661/51/9, vi75 1092/1366/9/12.
Fold 1 moved the wall by the 0.37 the plan priced (175: 1.12) and the
single-vint share from a fifth to nearly half; the echo phase (slave)
fell 1.42 -> 1.01 with the FB cat-1 pass gone, and mtask (master) 1.14
-> 1.01. The ship phase did not move (0.64): that is the 24-tile batch
inside the window, the "sprites slow while the tiles update" Mike feels.

**On hardware.** BOOTGAMERATE reads 63-64 game frames per 64 vints on
BOTH builds: under GAMEGATE the game never waits, so that probe cannot
rank what Mike sees. New probe `BOOTFLIPRATE=1` (LOOP29 232): presented
frames per 64 vints, from the FS bank changing between vint tops. The
first cut read the wrong register (0xA1510A is the DREQ destination;
the FB control word with FS is 0xA1518A) -- which also voids vi77's
"FS never changed mid-consume" reading in 231; the transport verdict
there stands on the VRAM-equals-source and read-back checks alone.
Rig numbers below when the corrected probe lands.

**Next**, per the plan and the decompile thread's assessment (69045ac):
fold 4 before the measurement channel -- one state word from IRQ4
(round, cutscene byte 0xFFF148, sequence), deleting the two SH-2
mechanisms that depend on timing alone (the claim-mix flag and the
COMM10 edge read). The title screen is the design constraint: it draws
the level-2 cave under logo sets outside every table, and today the
claim mix is what keeps the refuse rule off there. The attract dump of
0xFFF142/0xFFF148 per scene decides how the word is used.

**The hardware number (BOOTFLIPRATE, corrected register, fr70/fr75 =
the two lines plus the probe; attract, unattended):**

    presented frames per 64 vints        ares (play)      RIG first demo   RIG second demo
    vi70                                  46, 32           17 16 (6 face)   15 10
    vi75                                  62, 64           21 19 (7 face)   22 16

So on the FPGA the picture runs at ~15 fps on the line and ~20 fps on
fold 1 -- a hardware wall of ~4 and ~3.2 vints per generation, where
ares reads 1.48 and 1.11. The 3x SH-2 gap CLAUDE.md warns about is the
whole difference; ares ranks, the rig measures. The bar (60 = 64/64) is
three times away on hardware, not 11%. Every fold from here gets ranked
in ares and MEASURED with this probe on the rig, ~3 minutes a build.

## 233. FOLD 4 BUILT: ONE STATE WORD FROM IRQ4 (2026-09-12 21:40)

**The fact first** (ares, vi75, WRAM 0xFFF142/0xFFF148 across the
attract, screenshot per point):

    title 100-300, 3000-3300       round 0   cut 0
    first level-1 demo 700-1300    round 0   cut 0
    FACE 1500-1580                 round 0   cut 1
    EYE 1700-1900                  round 0   cut 0     <- not the face's byte
    second level-1 demo 2100-2800  round 0   cut 0
    level-2 demo 3600-4400         round 1   cut 0

So the game's cutscene byte covers the face only; the title and the eye
read exactly like the level (round 0, cut 0) while their sets sit
outside round 0's table. The state word therefore cannot DELETE the
claim mix: "on" still has to come from what is on screen. What it can
do: (a) carry the round in a word that shares nothing with the dirty
mask -- the 217/226 hazard gone by construction, (b) force OFF the
instant the face begins (no detector lag on the plane).

**233:** `MDSTATE=1`. md_main.c shim_vblank posts COMM14 =
E<seq><cut><round> once per vint at IRQ4's top, starting once the
master has answered the boot beacon (B008); m_main.c reads the round
from it (MD_ROUND_GET), forces `on = 0` while the cutscene bit is set,
and stops the per-window COMM14 diag write. COMM10 keeps its C-side
posts; nothing reads its round field now.

`rom/night/vi87.32x` = vi75's line + MDSTATE=1. Gates pending (ares:
face plane, aligned return, play black; rig: attract demos).

## 234. FOLD 4 COMPLETE: THE ATTRACT STEP RETIRES THE CLAIM MIX (2026-09-12 22:05)

**vi87 (233) never booted, twice.** Cut 1 dropped the master's per-window
0xB1xx answer on COMM14; cut 2 kept it but the 68K's boot hold
(md_main.c "HOLD THE GAME until the master's V-ISR is armed", COMM14 =
B008 or B1xx) started polling AFTER the vint handler had already taken
the channel for the state word: 582,330 reads of tag E at one pc in
400 frames (trace-access on 0xA1512E), the game never released, screen
black on ares and rig alike. The hold now accepts E (the handler only
posts E after it has itself seen B1xx/B008).

**The decompile thread's answer (NOTES-FROM-DECOMPILE 23, f694701):**
"the level's tilemap is on screen" is 0xFFF026 bit 0 (credited play,
with 0xFFF148 clear) or attract step 0xFFF031 bits 2-4 in {1, 3, 5}
(the demo; 3 with the logo over it). Steps 0/2/4 are the high-score
table, the intro pictures and the eye -- uploaded INTO the level pages
through the level's own page tables, which is why 233's page-word idea
could not work. 0xFFF148 belongs to the transformation object alone
(note 17 corrected).

**234:** the state word carries play (bit 3) and the step (bits 2-0).
`md_state_on()`: cut -> off; play -> on; else on iff step in {1,3,5}.
The claim mix no longer decides anything (its counters stay as MDS
diagnostics). Step 3 admits the logo's sets 37-46 and texture 11 past
the refuse rule (`md_state_extra`), keyed on the step, not detected.
`rom/night/vi88.32x` = vi75's line + MDSTATE=1. Gates pending: ares
title/demo/eye/face/return/play, rig attract demos.

**vi88 measured (ares): title 0.96 black (vi75 0.30), demo 0.96, eye
0.71 (0.47), face plane 0.00, return 0.58-0.95, play 0.90; rig: four of
six attract shots ~0.97 black, two CLEAN (213211/213216: level-1 demo
with the logo, trees 0.00 fg 0.04 -- the first clean vi75-line demo the
rig has shown).** The 68K's own posts, decoded: play=1 from frame 23
through the whole attract (the demo is the game started with scripted
input), and step 1 covers the SEGA/blue-wave screen for ~600 frames
before the level's tilemap. "play or step 1/3/5 = on" therefore
installed round 0 and refused every set on those screens. NOTES-FROM-
DECOMPILE 24 asks for the credited-play discriminator and step 1's
sub-phase.

**234b (vi89):** the word decides OFF only where it is certain -- the
transformation byte, and the picture steps 0/2/4 (high-score table,
intro pictures, eye) -- and leaves ON to the claim mix. Everywhere the
word is silent this is vi75; where it speaks, the lag and the timing
dependence of the detector are gone. Gates pending.

**vi89 measured.** ares: title 0.273 (vi75 0.297), demo 0.038, eye
0.485 (0.474), play 0.036-0.043 -- vi75 everywhere the word is silent;
face plane LATE (0.00 at 1575/1580, 0.52 at 1600; vi75 full from 1575)
and the aligned return 0.10-0.12 against vi75's 0.045: the 68K change
re-rolled the deferred-window phase (227/228; fmgate_defer 66). Rig:
IDENTICAL to vi75 -- first demo trees 0.20 fg 0.57-0.76, second demo
0.02-0.06 / 0.58-0.74. The OFF-only word does not touch the rig defect.

## 235. HYPOTHESIS FROM vi88: THE RIG'S BLACK SETS ARE THE CLAIM MIX DECIDING (2026-09-12 22:20)

The one build whose rig demo came out CLEAN on vi75's line was vi88 --
the build that let the WORD force the round ON (step 3) instead of the
claim mix: 213211/213216 trees 0.00 fg 0.04, the level-1 demo with the
logo, temple and trees drawn. Every build where the claim mix decides
ON (vi75, vi89) shows the black sets on the rig, and vi70 (claim mix
too, but a different phase) does not. Consistent with 231's finding
that the transport is intact and the black is built by state: the flag
flapping ON/OFF at hardware phase yields the pins, wipes/refuses sets,
and what was shipped off stays off.

**235 (vi90):** the word forces ON for steps 3 and 5 (unambiguous
demos), OFF for 0/2/4 and the transformation, and leaves step 1 (SEGA
screen, then the demo) to the claim mix. If the rig's step-3 and step-5
captures come out clean while step 1's do not, the mechanism is named
and the rest is the two discriminators asked for in NOTES-FROM-
DECOMPILE 24 (credited play; step 1's sub-phase).

**vi90 measured.** ares: title 0.273, demo 0.038, eye 0.515, return
0.038-0.044 (= vi75), play = vi75; face plane partial at 1580-1600 (the
68K change's phase, 227). Rig: BETTER, not clean -- first demo trees
0.15-0.19 fg 0.15-0.40 (vi75 0.20/0.57), second demo trees 0.07-0.10 fg
0.14-0.18 (vi75 0.02-0.06/0.58-0.74). Forcing ON in steps 3/5 removed
most of the FG black. What is left (214238, 214302): RECTANGLES of FG
cells in the temple facade and the pedestal, and blocks in the tree
row -- per-tile, persistent, not whole sets any more.

## 236. THE RECTANGLES ARE THE SLOT-PRESSURE RULE WITH ITS FALLBACK GONE (2026-09-12 22:40)

CAT1_MD's SLOT PRESSURE rule (2026-09-03, "Mike's black cells"): a cat-1
tile whose cache set is full of hot ways does not evict -- the slot
stays BLANK and "the FB keeps the cell" (CAT1_PEND). Under C1_NOFB
there is no FB pass, so that blank is a black cell, for as long as the
ways stay hot. Hot-way pressure is a function of windows per vint and
claim order, which is exactly what the rig changes against ares -- and
the rectangles are FG cells (the temple is FG sets 76-79, the pedestal
78/79). Not seen on vi70 because vi70 draws cat-1 in the FB.

**236:** `C1_SOFT` is 0 under C1_NOFB: a cat-1 tile evicts like any
other. SH-2 only, no 68K phase change. `rom/night/vi91.32x` = vi90 +
236. If the temple rectangles go and the tree-row blocks stay, the
tree row is the cut-mode blank (md_cut, armed by claim storms) and
gets the same treatment.

**Fold 5 footprint, measured** (ares, vi75, 68K writes into FB text
staging, play frames 900-999):

    pc 0x904D96  health bar   0xCB0-0xCCE   99 of 100 vints   (TXTWRAM covers)
    pc 0x903AC2  credit line  0xD50-0xD60   100 vints         (TXTWRAM covers)
    pc 0x9037E6  0x369C routine 0x0B4-0x0C2   51 vints        (the score: NOT covered)
    pc 0x90380A  same          0x0C2         51 vints
    pc 0xFFBE3C/4C thunk-displaced stores 0x162-0x17A  22-29 vints

So the third writer's per-vint footprint in level 1 is the score line
(8 words at 0x0B4). NOTES-FROM-DECOMPILE 25 asks for the routine's
offset/length source; failing that, a bounds mark at its store loop
(min/max offset per vint) makes the shim copy generic.

**tw75 = vi75 + TXTWRAM (the two covered writers), ares:** wall 1.16
(vi75 1.11), ships 37.3 fps (39.4), handler 60.7 lines (54.4: the
shim's footprint copies), holds 249 (1247), play black 0.036-0.041 (=),
2600 footprint copies by f2000. As the census predicted, ares cannot
show TXTWRAM's gain -- its spins are nil there -- and it charges the
copies. The rig number (BOOTFLIPRATE, fr75 = ~20 fps) is the test.

**236 RETRACTED as the mechanism.** vi91 (C1_SOFT off) on the rig =
vi90: first demo trees 0.17-0.19 fg 0.22-0.43, second 0.12-0.16 /
0.15-0.18; ares unchanged (return 0.038-0.049, play = vi90). The
rectangles (214736: a block over the temple facade at x 90-180,
y 30-110; blocks in the tree row) are not the slot-pressure rule. They
are per-cell and persistent, i.e. cells the name-table build emits as
the blank slot (or whose art shipped blank) and never revisits. The
probe that ranks the reasons is the next build: count, per window, the
cells emitted as MD_BLANK_SLOT by reason (no slot / cut-mode / dirty
under cut), carried in the six packet bits, rig against ares in the
same demo. C1_SOFT stays 0 under C1_NOFB (it is correct by
construction: there is no fallback), it just is not this.

## 237. TXTWRAM ON THE RIG COSTS HALF THE FRAME RATE; THE BLANK-CELL CENSUS (2026-09-12 22:55)

**frtw75 (vi75 + TXTWRAM + BOOTFLIPRATE) on the rig: 9, 1, 20, 11, 9
presented frames per 64 vints against fr75's 21, 19, 7, 22, 16.** The
mirror copies as written (the shim copies each dirty footprint into FB
text staging at FM=0 BEFORE the raise) push the post later on hardware,
where a 68K FB write costs 0.05 lines a word, and the windows per vint
fall. Ares charged it 0.05 v/gen and could not see the rest. So fold 5
cannot ship the mirror this way: the copy has to ride the packet/DREQ
side or land after the post, and the decompile thread's expected gain
(the gate spins) has to be measured against this cost on the rig, not
assumed. Noted for NOTES-FROM-DECOMPILE.

**The census.** A name-table cell is emitted as the blank slot for
three reasons (m_main.c, the NT payload): no way was claimed
(slot == MD_BLANK_SLOT), a dirty slot under cut mode (MDA 5), a dirty
slot outside cut mode (MDA 6, "a pending cell would show foreign art").
The third is the suspect for persistent per-tile black: a slot whose
art never ships stays dirty, and every chunk visit re-blanks its cells.
vi92 = vi91 + BOOTTILEVER carrying, per 64 windows, that count >> 2
(sat 63) in the six packet bits; ares reads the raw statics from SDRAM
at the matching attract frames.

**vi92 read (six-bit channel, dirty-blank cells per 64 WINDOWS >> 2,
sat 63; the SDRAM symbol dump failed, so the raw three-way split is
not in):**

    ares  f960 0   f1320 1   f1680 63   f3000 63   f3300 0
    rig   16s  0   22s   0   28s   63   50s   1    55s   28

Per window the rig blanks no more cells than ares (28 -> ~1.8 a window
in the second demo against ares' saturated >=3.9 around the face). What
differs is windows per vint (~0.3 on the rig against ~1 in ares), so a
blanked cell waits 3x longer for the chunk rotation to revisit it --
and a slot whose art is still dirty at the revisit is blanked again.
Persistent black therefore needs the art to stay dirty across visits:
the ship batch (24 a window at 0.3 windows a vint = 7 tiles a vint on
the rig, against 24 in ares) is the suspect, not the blanking. That is
a rate question the flip-rate probe already frames: the level's tile
demand per vint against 7 shipped. Not measured tonight.

STATE AT 23:05. vi90 (fold 4 OFF-decisions + forced ON in steps 3/5) is
the best fold-1 build on the rig (black halved) and is on the rig; the
line stays vi70. Open, in order: the ship rate on hardware (batch per
window vs windows per vint), fold 5's copy transport (237), the two
discriminators (NOTES 24), fold 3 after fold 5 (decompile census).

## 238. vi90 IN CREDITED PLAY ON THE RIG: THE LEVEL BLACK EXCEPT SKY AND GRASS (2026-09-12 23:15)

Mike: "v90 massive regression" -- 220012/220031: the level-1 start with
the temple, the pillars and the wall black, sky patches and grass
drawn, the Zeus text intact. Whole-set black, worse than vi75's first
run, in PLAY.

Ares does not reproduce it: a coin during the eye (attract step 4,
latecoin.csv) starts a credited game that reads 0.036-0.043 black at
3400-4200 -- vi75's number -- with the step byte stale at 4 for the
whole game, i.e. the word forcing OFF throughout play. So in ares the
dynamic allocator carries the level fine when the round is held off
screen; on the rig it does not (219's class: pins yield, sets churn,
what ships black stays black). Mike coined from the credit screen, so
his step was whichever picture step the attract had reached.

**238 (vi93):** the picture steps no longer force OFF (they return
"undecided" to the claim mix); only the transformation byte forces
OFF; steps 3/5 still force ON. Nothing in the word can now hold the
round off screen in play. The eye/intro lose their lag-free OFF again
until NOTES 24's discriminator exists. vi90 withdrawn.

**vi93 measured.** ares = vi75 everywhere (play 0.036-0.043 for both
coin paths, return 0.038-0.042, face plane 0.43 at 1580). Rig: BACK TO
vi75 -- first demo trees 0.11-0.20 fg 0.59-0.79, second 0.03-0.04 /
0.58-0.72. So vi90's halving of the black came from the picture-step
OFF, not the step-3/5 ON. What an OFF phase gives the level when it
comes back: the ON edge re-installs the round's table (mds_install:
pins whole, changed sets re-converted). On the rig the claim mix
evidently does NOT go off through the eye the way it does in ares (its
t/n mix is a per-window count and the rig has a third of the windows),
so there is no edge, no re-install, and the stale state stays black.
vi90 forced the edge and the demo came back clean; the same forced OFF
with a stale step in play is 238's regression.

**The fix has two halves:** (1) the OFF must come from the word only
while the attract runs -- a credited flag the shim owns (set on the
coin input it already handles, cleared when the step byte moves again
after game over), posted as the play bit instead of 0xFFF026.0; (2) the
edge re-install is the actual medicine, so a periodic re-install while
ON (say every 64 windows, selective as it already is) would give the
rig what ares gets from its edges without depending on any detector.
Both are small. vi93 (no regression against vi75) stays on the rig.

## 239. CREDITED, OWNED BY THE SHIM (2026-09-12 23:30)

The play bit in the state word is now the shim's own flag: set when
the shim sees the coin or start1 input (the same `svc` it posts to the
MCU mailbox), cleared when the game's running bit 0xFFF026.0 falls
(game over). The demo never presses START, so the flag separates a
credited game from the attract without any byte the game reuses.
md_state_on: transformation -> OFF; credited -> the claim mix; picture
steps 0/2/4 -> OFF; demo steps 3/5 -> ON. That is vi90's attract
behaviour with 238's play regression excluded by construction.

`rom/night/vi94.32x` = vi93 + 239. Gates: ares attract/play/late-coin
with the posted words decoded through the credited game; rig attract.

**vi94 measured.** ares: attract/play/late-coin all vi75; the posted
words decoded through the late-coin game: credited 0 -> 1 at the coin
(f2500), the step byte MOVES at game start (5 -> 1 -> 2 by f2539) and
stays 2 -- so 238's regression was exactly a picture step reading 2 in
credited play, and the credited flag now routes play to the claim mix.
Rig attract: = vi75 (fg 0.56-0.74), i.e. vi90's demo gain did not
survive. Either the flag is set on the rig with no one pressing
anything (a floating joypad read: svc & 0x11 from an idle port) or the
picture-step OFF is not what produced vi90's gain. One probe decides
it (paint `shim_credited` and the step through the value instrument in
the attract); not run tonight. vi94 is on the rig as the safe fold-1
build: no regression against vi75 in either mode, the transformation
OFF lag-free.

## 240. vi94 "SLOW, BLACK SILHOUETTES, ZOMBIES NOT MASKED": ATTRIBUTION BY A/B, THE RECOVERY LINE (2026-09-12 23:50)

Mike on vi94 (222156: the Zeus intro over a black level): "slow, black
silhouettes, zombies rising from the ground are not masked."

  - The unmasked zombies are fold 1's known artefact: C1_NOFB deletes
    the FB cat-1 pass, so a sprite pixel wins over the cat-1 ground it
    should be behind (175: "sprites wrongly cover cat-1 tiles where
    they overlap"). It is the hole punch the plan named as Mike's gate
    for fold 1, unchanged since vi75; the suppress-by-bitmap fix (175)
    is not built.
  - The slowness is unattributed: no rig speed number exists for any
    build after vi75 (231-239 are black shares). NOTES 30 names the
    three compiled differences and ranks them: 236's cat-1 eviction
    (more re-ships on the 7-tiles-a-vint machine), 239's credited edge
    (the shim's clear fires one vint after the game clears its bit at
    the start press; a short press leaves credited 0, the stale step 2
    forces the level OFF = vi90's shape), COMM14 traffic last.
  - NOTES 29 measured the game's byte on the arcade: 0xFFF026 bit 0 is
    1 through the attract and 0 through a credited game, with no edge
    (cleared by the start handler, set by the demo start). credited <=>
    bit 0 clear. f028|f029 says a demo is running (step 1 without it =
    the SEGA card).

**240:** the recovery line. MDSTATE keeps the two free halves of fold
4 (round outside the dirty mask; transformation OFF) and the attract
decisions from bytes the game maintains: credited = ~f026.0 (no shim
flag), demo bit in word bit 8, seq shrunk to 3 bits; steps 0/2/4 OFF,
1/3/5 ON when the demo bit is set, step 1 without it OFF. C1_SOFT is
back to vi75's rule under C1_NOFB (236's eviction is the `C1EVICT=1`
knob). `rom/night/vi95.32x` = that line; `fr95` / `fr95e` = the same
with BOOTFLIPRATE, without / with C1EVICT, for the rig A/B against
fr75's 21 19 7 22 16.

**240 measured.** vi95 ares: every gate = vi75 (title 0.273, demo
0.038, eye 0.482, return 0.039-0.040, face plane 0.65 at 1600, late-coin
play 0.036-0.043); the posted words: credited 0 through the coin and
the SEGA card, 1 from the START press on, the game's byte with no edge.

Rig frame rate (presented frames per 64 vints):

    fr75    21 19  7 22 16
    fr95e   19 15 22 21 14      (recovery line + C1EVICT)
    fr95    invalid: Mike launched vi95 over it at 22:32:36; one valid
            capture, 21, before that

So the state word from the game's bytes and the cat-1 eviction knob
together cost the rig nothing in the attract. vi94's slowness is
therefore not COMM14 and not 236; what vi94 had that vi95 does not is
the shim's credited edge (NOTES 30's second suspect): a short START
press left credited 0, the stale step 2 forced the level OFF for the
whole game -- black AND slow, vi90's shape. Play on vi95 is the test.
The chain relaunched vi95 at 22:34:25 on top of Mike's own launch; the
fr95 re-run waits for the rig to be free.

## 241. vi95 ON THE RIG: "WE'VE REPAIRED MOST DEFECTS"; WHAT'S NEXT (2026-09-13 00:20)

Mike on vi95: "We've repaired most defects from the previous attempts.
What's next?" vi95 = vi75's line + MDSTATE (the state word from the
game's own bytes, NOTES 29) is the fold-1 build that plays; vi70 stays
the presentation line until Mike names a new one. Open on vi95, in
Mike's words tonight: the unmasked sprites over cat-1 ground (fold 1's
hole punch, 175's bitmap suppress), residual black tile drops on the
rig (the ship rate, 237), the parked text gates (fold 5).

Next, per PLAN-SINGLE-VINT as re-ordered by the decompile census
(NOTES 24/30): fold 2 first -- the maps scan from `setcols_md.h`, the
next build that moves a vint number; measure the scan/tail split of
the 0.44 v/gen maps drain before counting it. Then fold 5 with the copy
on the packet side (237), then RELBANK on the rig. The hole punch is a
sprite-loop bit test against the baked cat-1 bitmap (175) and goes in
as its own build for Mike's eye.

## 242. FOLD 2 BUILT AND MEASURED: THE MAPS SCAN FROM THE BAKE (2026-09-13 00:45)

`SETCOLS=1` (sh_src/setcols_md.h, NOTES 21): one pass over the 44
columns x 2 quadrant row ranges per plane replaces bm_scan_rows'
2,464 cell reads, when the state word says the level's tilemap is on
screen (credited, or a demo step with the demo bit; never the face or
the picture steps, which upload into the level pages -- NOTES 23) and
every page select is < 10. Otherwise the live scan runs as before.
PHASECENSUS now splits the maps drain (CEN[56..59]).

    ares, play2, 4000 frames      pc95 (census)   sc95 (+SETCOLS)
    wall v/gen                       1.16            1.03
    single-vint                      42%             55%
    ships/s                          37.5            41.1
    mtask v/gen                      1.04            0.78
    echo v/gen                       1.02            0.95
    maps drain: scan / tail          0.279 / 0.135   (per gen, of 0.44)

The plan priced fold 2 at "up to 0.29" (the scan's share): measured
0.279 and the wall moved 0.13 -- the rest of the scan was overlapped by
the slave. mtask fell 0.26. Play picture 0.036-0.043 black, unchanged.
`rom/night/sc95.32x` (md5 31fc920f) = vi95 + SETCOLS.

Check mode (SETCOLSCHECK=1, live vs baked set by set) did not compile
in the first cut (the bm_state field macros); rebuilt as bm_check_cmp.
Numbers to follow. The rig frame rate (BOOTFLIPRATE) is the hardware
number and needs the rig.

sc95's own drain split: scan 0.113 v/gen (7,276 chunks against pc95's
19,560 -- one chunk per plane where the bake is eligible, the live 8-row
chunks where it is not: play2 includes attract time), tail 0.113. The
scan's 0.279 became 0.113, the tail is untouched, as the plan said.

**Fold 2 on the rig:** frsc95 (sc95 + BOOTFLIPRATE) reads 21 18 19 19
21 presented frames per 64 vints against fr95e's 19 15 22 21 14 and
fr75's 21 19 7 22 16 -- no cost, no visible gain: the rig's wall is
elsewhere (the windows-per-vint floor, 232). Ares' 1.03 stands as the
ranking number.

**Check mode read:** live-vs-baked disagree on 6.4 sets a plane in
steady play (f1000-4000, 3,080 planes), 4,086/546 during the load.
Offline, the same two formulas on the ROM tilemap agree on 2,000 random
windows exactly once empty cells are ignored, so either the live tile
RAM is not the ROM's in play or the runtime compare is wrong; the
TILEMAP_C dump against the ROM unpack decides which.

## 243. THE ROM UNPACKER WAS OFF BY ONE ON ZERO RUNS; THE FOLD-2 HEADER WAS COLUMN-SHIFTED (2026-09-13 01:20)

The check mode's 6.4 disagreeing sets a plane were real. Live TILEMAP_C
(ares, play f2000) against the ROM unpack every tool shared
(scene_sets.py, bake_setcols.py, LOOP-DECOMPILE 10's format note):
10,550 of 20,480 words differ, and they differ as RUNS SHIFTED BY ONE
COLUMN -- the low-byte pass's zero escape emits `n + 1` zeros, like the
high-byte runs, not `n if n else 1`:

    zero run = n         10,550 words differ from live tile RAM
    zero run = n + 1          0

So the baked column extents were shifted by a column per zero run
(only the presence at the window's edges disagreed, hence 6/plane and
no visible defect), and the decompile thread's "4,000 windows, 0
mismatches" had both sides sharing the bug. bake_cat1map.py's own
decoder was already right (it matched live byte for byte). Fixed in
both tools, header regenerated and verified against the LIVE dump
(2,000 random windows, 0 mismatches); the check mode re-run and the
wall re-measured with the corrected header are below. 231's level-1
set grid (drawn from the same unpack) was column-shifted too; its
per-set conclusions were qualitative and stand.

## 244. THE HOLE PUNCH, FIRST CUT: CELL-GRANULAR WORKS ON THE ZOMBIES, OVER-CUTS AT THE GRASS (2026-09-13 01:35)

`C1PUNCH=1` (c1p95 = vi95 + the punch): the master's FG name-table
pass writes a 40x28 cell mask of cat-1 cells; the slave's sprite
compose skips pixels of pp < 3 in those cells (all four plot paths:
baked runs, 1:1 NIB/NIB_NC, zoomed ZNIB/ZNIB_G). The mask read from
ares at play f540 is exactly the ground rows 20-27 (the temple facade
is NOT cat-1 in the rom: FG page 0 rows 4-23 read cat-0 throughout,
row 24 cat-1). Zeus's apparent absence at one frame was his own
flicker phase (both builds show him on 26 sweep frames alike).

ares, play f1160, vi95 vs c1p95: the zombie rising at the right edge
shows its whole body over the ground on vi95 and only what is above
the ground on c1p95 -- the masking Mike asked for. BUT the player's
lower legs are cut at the grass line on c1p95: the grass cells are
cat-1 tiles with transparent tufts, and a cell-granular punch removes
the sprite where the arcade shows it through the transparent pixels.
That is exactly the 2-bit hole map `tools/bake_cat1hole.py` (decompile
thread, in the tree) describes: 0 no hole, 1 suppress the whole cell,
2 consult the art per pixel. The per-pixel form is the ship; the cell
form is the measurement. Its unpacker line carried 243's bug; fixed.

**With the corrected header (pcsc95):** wall 1.05 v/gen, 49% single-vint,
ships 39.3/s, mtask 0.75, echo 0.97 (buggy header: 1.03 / 55%; census
alone 1.16 / 42%). The corrected extents are the true ones; the 0.02
between the two headers is inside the run-to-run spread of these
4,000-frame walks (the single-vint share moves with what the demo does
in the last few hundred frames). Fold 2's number is ~1.05, from 1.16.
Check mode with the corrected header: below.

**Check mode, corrected header: 0 presence and 0 level disagreements
over 3,046 planes of steady play** (f1000-4000; the buggy header read
6.43/1.13). Fold 2 is exact by construction and by measurement. Fold-2
line = `SETCOLS=1` on vi95's flags (sc95, md5 446055c8): ares wall
~1.05 v/gen from 1.16, rig frame rate unchanged (frsc95 21 18 19 19 21).

**244b, per pixel (c1p95b, md5 4735c75f):** the master's FG pass writes
the bake's hole class per screen cell (0 none / 1 whole cell / 2 per
pixel) plus the tile index for class 2; the slave punches a class-2
pixel only where the tile's own pixel is opaque (one ROM byte per such
pixel). ares play f1160: the player's legs and the zombies' feet draw
through the grass tufts again, the rising zombie stays masked below
the ground. `sh_src/cat1hole_data.s` links the 25,600-byte map under
C1PUNCH; the rom has ~100 KB of headroom (sprbake ends ~3.9 MB).
Rig frame rate and Mike's eye next.

**244b on the rig: frc1p95b 12 16 8 19 9 presented frames per 64 vints
against 19-21** -- the per-pixel test on every sprite pixel (a load, a
class test, a ROM byte for class 2) cost the slave a third of its
windows. **244c:** the baked-run path tests the class once per CELL;
class 0 copies the run, class 1 skips it, only class 2 walks the art
(one ROM row per cell). c1p95c / frc1p95c built; rig number below.

**244c measured: frc1p95c 16 16 6 21 18** against fr95e 19 15 22 21 14
-- the per-cell form costs the rig nothing measurable; ares f1160 shows
the legs through the grass and the zombies masked below the ground.
`rom/night/c1p95c.32x` (md5 85116f58) = vi95 + C1PUNCH is on the rig
for Mike's eye. If it passes, the fold-1 line is vi95's flags +
C1PUNCH=1, and fold 2 adds SETCOLS=1 on top (sc95 measured separately;
the two do not interact).

NIGHT'S END (02:30). Line: vi70 (presentation) until Mike names one;
fold-1 builds: vi95 (plays), c1p95c (hole punch, on the rig), sc95
(fold 2, exact, 1.05 v/gen ares). Next: fold 5 with the copy on the
packet side (NOTES 25/26 with the decompile thread), then RELBANK on
the rig with BOOTFLIPRATE + BOOTGATECHK.

## 245. BUILD B PASSED; THE LINE IS bldB (2026-09-13 04:00)

Mike on Build B (bldB = c1p95c + SETCOLS=1): "Behind the grass. Stellar
lockdown on the progress." Card in PLAN-SINGLE-VINT: wall 1.29 -> 1.18
v/gen on the fold-1 line, single-vint 31 -> 40%, check 0/0 over 3,010
planes, picture = c1p95c, rig frame rate 21 19 7 15 18. The line is
bldB: folds 1 (tiles to the VDP + the per-pixel hole punch), 2 (the
maps scan from the bake) and 4 (the state word) on one rom. vi70 stays
the presentation reference for the older gates. Next card: fold 5 on
the packet side, then fold 3.

**Build C, shaped (not built):** fold 5's copy on the packet side. The
shim already has the FM=0 framebuffer slot that costs the post nothing:
r60_blast() (the staged FB packet, blasted at the tail when FM is
already 0, else held in fbx_pend and blasted from the gate spin by
fbx_late_blast() or in the pre-post slot). The TXT_WRAM footprint copy
(md_main.c, "TOP-OF-PASS TEXT STAGING", 50 words a dirty vint at 0.05
lines a word on the rig) sits BEFORE the raise today and that is what
halved the rig's frame rate (237). Build C = the same copy factored
into txtw_blast() and called from r60_blast() / fbx_late_blast(): same
FM=0 guarantee, zero post delay. Its card: TXTWRAM=1 on bldB's flags,
rig frame rate against frB (21 19 7 15 18), then NOTES 25's third
writer (0x369C) once the decompile thread names its footprint.

## 246. BUILD C: THE PUNCH'S OWN PRICE, AND THE RAM-CODE BUDGET (2026-09-13 04:40)

The decompile thread's next card (via Mike): the punch's 1:1 and zoomed
sprite paths still test the hole class per pixel; the baked-run path
tests per cell (244c) and cost the rig nothing. Same change, one flag,
~0.13 v/gen by Build A's own price. New landing rule alongside it: a
rig black share counts only across THREE launches.

`C1PCELL=1`: a per-row cache (cell index, class, art row) so the
per-pixel test is a register compare and the class/art fetch happens
once per cell crossed. Two cuts overflowed the SH-2's RAM-code slot:
the line's .ramtext is 28,472 of 28,672 bytes (m_main 18,160 after
LTO, slave_concurrent_k 8,604, s_main 704, blit_half 532, cap_drain
336, fbx_lift 128), so ~200 bytes are all a change may add to the hot
paths. Third cut: the cache on the 1:1 paths (NIB/NIB_NC, 16
expansions) with the refetch out of line; the zoomed paths keep the
per-pixel form; bm_scan_baked_ok moved to ROM. The card (PLAN) carries
the numbers.

**246b, fourth cut links:** one out-of-line RAMCODE function taking the
cache and the pixel x (the call site is a few bytes per expansion), the
zoomed paths per pixel as before, bm_scan_baked fetched from ROM under
the flag. .ramtext 0x6CEC = 27,884 bytes (the line 28,472). Card
measurements running.

**The remaining scan (the decompile's question):** pcB in steady play
reads 2.00 chunks a generation (one baked pass per plane, as designed)
at 573 FRT ticks a chunk = 0.095 v/gen. Forty-four columns x two
segments x ~6 extent compares is ~500 iterations a plane; 573 ticks
for that is ~1.1 ticks an iteration, which is not arithmetic on an
SH-2 -- it is the table READS: setcol_idx and setcol_ent are const in
cart ROM, read through the 32X cart bus that the 68K also owns, and
every entry is three byte loads plus two index loads per column. The
fix is the round's tables in SDRAM at install (mds_install copies
~17 KB for the largest scene); the free SDRAM under the region guard is
~14.7 KB with _end at 0x15548, so it needs a home first (the old
tile-cache half at 0x31000+ is .ramtext now). Not built tonight.

**246c, Build C measured (card in PLAN): FAILS.** pcC wall 1.21 v/gen
against pcB 1.18, single-vint 37% vs 40%, echo 1.14 vs 1.11; picture
equal to B in the attract and the coin-at-title game, but the game
coined during the eye reads 0.08 black against B's 0.04 -- the l3800
frame shows the tree-row rectangles of the same-round-return class
(221/227), re-rolled by an SH-2 timing change this time. Rig: frame
rate 22 18 19 7 21 (= B), attract clean over three launches. The
punch's 0.13 is not the per-pixel class test on the 1:1 paths; the
per-cell call bought nothing. bldB stays the line and is back on the
rig. Next measurement, not a guess: time the baked-run punched loop
against the plain run copy (a slave-side stamp pair), and the master's
mask writes in the name-table pass, before another card is cut.

## 247. THE STAMP PAIR BY SUBTRACTION: THE SLAVE'S PUNCHED LOOP HOLDS THE PRICE; BUILD D IS THE MASK TABLE (2026-09-13 05:30)

Mike: run the stamp pair first, then note 34. Done as an ablation in
the phase census (no new stamps in a full .ramtext): C1NOPLOT (the
slave draws every sprite unpunched, the master still writes the mask)
and C1NOMASK (the master writes no mask; the loops see class 0).

    pcB (the line + census)     wall 1.18   echo 1.11   mtask 0.79
    pcB_noplot                  wall 1.03   echo 0.96   mtask 0.74
    pcB_nomask / nopunch        (below)

The slave's punched plot holds 0.15 of wall; the master's mask writes
cost nothing measurable. Note 34's reading from the code -- the
class-2 art rows read from the cart inside the punched loop -- is the
prediction this matches. Build D therefore: `C1MASKTAB=1`, the scene's
1-bit opacity masks (tools/bake_cat1mask.py, 81-192 tiles a scene,
<= 1.5 KB) copied to SDRAM at mds_install, a mask index per class-2
cell from the name-table pass (binary search of the raw code), and one
SDRAM byte per cell row in the punched loop instead of eight cart
bytes. Nothing added to .ramtext (the loop shrinks). Card in PLAN.

    pcB_nomask                  wall 1.16   echo 1.09   mtask 0.74
    pcB_nopunch                 wall 1.05   echo 0.97   mtask 0.75

So: the mask writes cost 0.00, the punched loops' own shape 0.02
(nomask vs the line), and the class handling with its cart art rows
0.13 (nomask 1.16 -> noplot 1.03). Note 34's reading holds: the art
rows from the cart inside the slave's loop. Build D's first cut spilled
.ramtext by inlining the mask-index search into the name-table pass;
the search is out of line in ROM now.

## 248. BUILD D READS 1.22: THE 0.13 IS THE PUNCHED LOOP'S SHAPE, NOT ITS ART (2026-09-13 06:30)

pcD (the mask table, SDRAM byte per cell row instead of eight cart
bytes) reads wall 1.22, echo 1.14 -- no better than the line's 1.18 /
1.11. So the class-2 art reads were not the price, and the ablation
has to be re-read: noplot 1.03 (no punched plotting at all) against
nomask 1.16 (the punched loop running with class 0 everywhere, no art,
no skips) puts 0.13 in the LOOP'S SHAPE -- the per-cell `while`, its
per-cell k and class load and the un-unrolled inner copy -- and only
0.02 in the class handling. Note 34's reading from the code was the
natural one and it was wrong; the measurement is what settles it.

**Build E (`C1FAST=1`):** before walking cells, a punched run scans its
cells' classes (one byte per cell, 2-9 loads a run); if all are class
0 it takes the ORIGINAL tight copy. Cat-1 is 11% of level 1's cells,
so most runs never see the cell walk. Card in PLAN when D's closes.

## 249. BUILD E READS 1.17; THE PRICE IS THE UNCACHED MASK READS (2026-09-13 07:10)

bldE (the tight copy for all-class-0 runs): wall 1.17 against the
line's 1.18 -- nothing; picture equal to B including the late-coin
game. (Its census rom would not fit .ramtext even with the baked scan
in ROM; the wall is nat_score's own, census-free, and it equals the
census reading on every rom so far.) So neither the art rows (D) nor
the loop's shape (E) is the 0.13, and what both left untouched is the
one thing every punched pixel does: read the cell mask through the
UNCACHED SDRAM alias -- a bus round trip per cell in the baked path
and per PIXEL in the 1:1 and zoomed paths (c1_hit's m[] and cd[]).
noplot (1.03) removed those reads along with everything else.

**Build F (`C1CACHED=1`):** the slave reads cat1scr/cat1code through
its cache. Fresh enough by construction: the slave purges its cache at
every window start, the master's mask writes are write-through, and the
mask is one generation behind the compose by design. One flag on bldB.

**249b. The slave's own number, from the generation trace (PHASECENSUS
`gen_trace.py`, "clear+sprites" pass sum, v/gen, 860-930 gens):**

    pcB (line)          0.389        pcB_nomask         0.383
    pcB_noplot          0.324        pcB_nopunch        0.327
    pcD (SDRAM masks)   0.422        pcF (cached reads) 0.394

So the punch costs the slave's compose 0.065 v/gen, of which 0.059 is
present with NO mask at all (class 0 everywhere, no art, no skips):
the punched run loop's SHAPE -- the per-cell control around every run
-- is the price, and Build D's bit-mask form made it worse (+0.033),
Build F's cached reads changed nothing. 99% of level-1 records take
the baked-run path (SPRBK: 96,304 hits to 859 misses in steady play),
so the 1:1/zoomed paths are irrelevant to it. Build E's fast path was
the right idea; its pre-scan read the mask through the uncached alias
per run, which is the same class of cost it was removing. Build G =
E's fast path with F's cached reads.

## 250. BUILD G: THE FAST PATH FIRES ON 67% OF RUNS AND SAVES NOTHING; THE PRICE IS NOT THE LOOP THAT RUNS (2026-09-13 08:10)

pcG (E's fast path + F's cached reads, with the census): wall 1.18,
echo 1.11 -- the line's numbers exactly; compose pass sum 0.388 v/gen
against the line's 0.389; and the counters say the fast path DOES fire:
790,990 runs took the tight copy against 387,576 that walked cells
(67%). Census-free the wall read 1.16 vs 1.18. So two thirds of the
punched runs now execute byte-for-byte the original copy loop and the
compose sum does not move, while noplot (punch a compile-time 0) reads
0.324. The remaining explanation is that the PRESENCE of the punch
code costs -- compose_sprites is one large RAMCODE function and the
extra paths change the codegen of the loops that still run (register
pressure, spills in the run loop) -- not any punch path that executes.
Cards C-G all changed what executes; none changed that.

**The ablation that separates the two:** `C1RTOFF=1` -- the punch code
compiled exactly as on the line, gated at run time by a volatile byte
that is always 0. If its compose sum reads ~0.32, the cost is the code
running (and something in the walked third is dearer than it looks);
if it reads ~0.39, the cost is codegen, and the fix is structural (the
punched draw in its own function or a separate loop).

Build G's card: no gain (wall 1.16-1.18); picture equal to B; closed.

**250b, a trap in the RAM-code budget:** a `static` function given a
ROM placement (no RAMCODE) is still inlined by LTO into its RAMCODE
caller, so "moved to ROM" moved nothing unless it is also
`noinline`. The C/D card notes that said bm_scan_baked was fetched from
ROM under their flags described an intent, not the binary (they linked
because the rest of the change shrank); the census roms for E, G and
the RTOFF ablation kept overflowing for the same reason. bm_scan_baked,
bm_scan_baked_ok and fbx_lift now carry `noinline` on their ROM
variants; c1mask_find already did.
