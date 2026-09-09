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
