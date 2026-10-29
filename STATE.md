# STATE — what is true right now

**Volatile document. PRUNE IT, do not append to it.**
Last updated: 2026-09-22.

If a line here is stale, fix the line. Do not add a newer line below it.
That is how the logs became unreadable.

---

## THE TWO BRANCHES HAVE DIVERGED AND THE RECORD IS ON THE WRONG ONE (2026-10-17)

    FOUND while debugging the load pacing, and it invalidates some of my own
    measurements, so it goes at the top.
      merge base         1a6f3d49 (2026-10-14)
      rebuild60-p1       64 commits ahead  -- the builder's: STATE, LESSONS,
                         and one port code change
      advisor-bisect     85 commits ahead  -- the advisor's: the entire r60
                         renderer line, sw108 through sw18x
      sh_src/r60/r60_main.c   1346 lines on mine, 3403 on advisor-bisect
      STATE.md                4283 lines on mine, 3137 on advisor-bisect
    EVERY LESSONS ENTRY WRITTEN TODAY EXISTS ONLY ON rebuild60-p1. The
    alignment class, the dead clamp, the 21-frame blank finding: zero hits on
    advisor-bisect. The advisor edits START-HERE on their branch and said
    plainly "no STATE edit from me", so the record and the work have been on
    separate branches all day and neither side noticed.
    WHAT IT COST, concretely: I built and measured the load-pacing change
    against MY tree, whose renderer is 2000 lines behind the line. That is
    why R60_DIAG[0] reads 0 all run long here and my vint clock hit its
    safety bound -- the advisor's answer was right for THEIR tree. Every
    display-frame number I measured for that card (the 32-frame blank, the
    frame positions) came off a renderer that is not the line and should be
    re-taken. The timer-parks result is port-side and should survive.
    A MERGE WOULD BE CLEAN ON THE RECORD: advisor-bisect has ZERO commits
    touching STATE.md or LESSONS.md since the base. The one code commit
    (3fddcd07, port_l2.c + extras.h + cport_prep.py) needs care because the
    advisor may have touched those.
    NOT ACTED ON -- merging into a branch someone is actively building on is
    Mike's call, not mine.

## THREAD LANES (2026-09-27, after a day of both threads working one lane)

Mike is the only shared memory, so every fact crosses a human relay: two
threads is ONE channel, three would be three. The fix for the confusion on
2026-09-27 is a written split, not another thread. The division is CONCURRENCY
AND CONTEXT, never permission -- both threads can build, push, run ares and run
MAME (575375b4).

    BUILDER owns        the shipping renderer and the line
                        the test cartridge (docs/design/TESTCART.md)
                        rig instruments: barcode, value channel, RIGRATE
                        the L1 CAPTURE side: l1_oracle.lua, l1_vectors.py,
                          the vectors themselves, l1_triage.py
                        mister.tv.local -- reach it with
                          tools/mister_push.sh --name mister.tv.local
                          (the DEFAULT is mister.office.local, which is the
                          decompile thread's; --name sets the ssh target and
                          the launch API host TOGETHER, and every push now
                          prints the rig it used)
                        barcode bytes 3 and 4

    DECOMPILE owns      the Ghidra project and the decompiled corpus
                        cport_prep / cport_layout / the prep transforms
                        ON-SH-2 execution: port_selftest.c, gen_selftest.py
                        mister.office.local
                        barcode bytes 5 and 6

WHY L1 SPLITS THERE, and it is not a preference: a HOSTED differential cannot
verify this corpus at all. The 68K and SH-2 are big-endian and the host is not,
and the corpus reaches memory through typed pointers with no chokepoint to
byte-swap at. Execution has to happen on a big-endian target. Capture has to
happen under MAME. So the seam is real.

CROSS-CHECKS STAY OPEN BOTH WAYS -- that is the point of two threads, and on
2026-09-27 it caught a hand-conversion error in one direction and a
self-derived expected value in the other. What does NOT cross is running the
other lane's work.

## DECOMPILE LANE — THE PORT COLD-BOOTS AND DRAWS THE ATTRACT (2026-10-04)

    make line NOSPRBAKE=1 PORTFULL=1 PORTMAIN=1 PORTOS=1 NOGAMEHIGH=1 PORTVIDEO=1 \
         PORTPLAY=1 PORTBOOT=1 PORTCORO=1 && tools/save_build.sh rdN
    python3 tools/port_scorecard.py <dumpdir> rdN        (ares dumps; line beside it)

The SH-2 port boots Altered Beast from the 68K reset vector with NO 68K game in the
rom, runs its own control flow across frames, and the renderer draws from the port's
four video shadows. rd12 (2026-10-04 03:40), frames 0-2401 on ares-headless:

    tilemap words 18760/26624 (line 19520)   PAL_SH 985/2048 (line 1187)
    VRAM art slots 1254/1408 (line 1254)     MD CRAM 43/64 (line 56)   vints/frame 0.99
    picture non-black  f1200 83.4% (line 83.0%)   f2400 75.2% (line 82.6%)
    f500: the ALTERED BEAST logo sprite drawn; f1200: the round-1 background, no actors

Nine port-side defects were found and fixed today, every one from a measurement
(LESSONS 2026-10-04): the vint ran twice a frame (H-int + IE0) and hung the 68K;
`-DPORT_FULL` reached only the SH-2 compiler (MD fetched art 8 KB low); the arcade's
video writers that take their destination in an address register got untranslated
pointers (CPORT_PARAMNORM, from a MAME watchpoint census: tools/video_writers.lua);
the tilemap unpacker chain lost A1 between its two halves; the palette queue derefs
were raw and misaligned; clear_tileram's 64 KB walk crossed into the palette/text/
sprite shadows; the master landed the MD's empty palette pushes over PAL_SH; the
order-list insert stored a pointer's low byte as a slot index; the sprite
terminator's second word went through P68. A 10th -- 0xFFF400 forced to 4 bytes so
the sprite-palette allocator never ran -- is in build.

COMMIT 3e94cc5b (2026-10-06 01:40) IS THE CURRENT PORT BUILD (`make check-demo` ->
build/port/rom/s16.32x; the rig last took f3bec7ae on 2026-10-04 at 23:20). THE GATE IS 20
TARGETS OVER FIVE SCENES, records AND the hardware sprite list: the two attract steps
before demo A (T 0x96/0x28, E 0xCA/0x34), demo A (0x74 0x7C 0xC4 0xEE 0x118 0x140 0x1C0
0x240 0x2B8), the HIGH-SCORE STEP (H 0x2BA at two offsets) and demo B (0xA2 0xD4 0x1FE
0x228 0x246). 18 are exact; the two H targets differ by 7 and 38 bytes -- animation and
position counters one or two steps apart in records 1..6 -- and that is now known NOT to be
the frame clock (it survives PORTCLK1 unchanged). An arcade dump's name carries its scene
(aw_<scene>_<frame>.bin): 0xFFF02A restarts at each attract step, so the same tick occurs
in several scenes, and check_demo refuses a legacy name or a frame outside the scene it
claims. A frozen scene (H, one tick 0x2BA over ~300 frames) is anchored on the frame its
install lands, not on the freeze start.
THE HASH POLL STAYS AND THE HIGH-SCORE INSTALL HAS NOTHING TO FIX (2026-10-07, both read
rather than argued). Marking every tilemap writer reached 35 of 64 and stopped: pp_miss_pages
says the escapes are pages 1..11, the unit holds exactly 37 literal shadow addresses across
8 functions, and the writers of those bursts compute their destination or hold it in work RAM
-- no static mark can see them. The high-score teardown holds its clears AND its install in
ONE game frame (FUN_0001A406: the waits are at 0x1A40E before and 0x1A480 after, and none of
the nine routines between them waits or calls anything that does); the arcade's "clear frame"
at 1167 is a vblank landing inside it, which its blink counter frozen at 1158 across frames
1165-1168 confirms. H's byte counts only mean anything with their frame beside them: 7 at
1222, 57 at 1211, 33 at 1344, 40 at 1365.

NO RIG FROM 2026-10-07 (Mike, at the office): ares and blastem headless only, so every
hardware question below is PARKED, not open -- the ticks-per-second reading, the
PORTTICKFREE mechanism, and any picture verdict. What can still move is ares-measurable:
the tilemap's other writers (the hash poll), the missing clear frame in the high-score
teardown, and the _sbuf rebase costed below.

PORTTICKFREE TAKES THE BACKGROUND OFF THE SCREEN ON SILICON, AND THAT IS PROVEN, NOT
GUESSED (2026-10-06 21:35-21:51, office rig, tools/rig_bg.py, three CONFIRMED launches per
verdict). LESSONS 830 says an FPGA-only background absence belongs to the build's LAYOUT
until a pad control says otherwise, so the control was run both ways:
    pc9ff    PORTTICKFREE=0                PRESENT 3/3   non-black 99.5-100%, top 10-13%
    pc9pad   PORTTICKFREE=0 + 64-byte pad  PRESENT 3/3   non-black 99.5-99.9%
    pc9dpad  PORTTICKFREE=1 + 64-byte pad  ABSENT  0/3   non-black 4.8-7.2%, 93-95% black
    pc9def   PORTTICKFREE=1                ABSENT        (Mike, by eye)
The verdict follows the FLAG and not the pad, in both directions, so the layout lottery is
ruled out and 3c26e914's claim stands. The flag is off by default and the default build's
plane is present on hardware. WHAT IS STILL WRONG ON SILICON IS THE RATE: a slide show, by
Mike's eye about one frame every three seconds, which is ~180x slower than the vint and is a
WAIT, not fetch cost -- the next question, and it cannot be read with a CRAM flood (LESSONS
830 rule 2: a flood destroys the BG palette for the rest of the run), so PORTRIG's channel
has to move to the barcode or to 32X CRAM before it can answer anything.

THE RENDERER UNDER THE PORT (renderer lane, 2026-10-06). The port clock -- port frames per
arcade frame, printed by check_demo as `port clock: xN` -- is x1.005 and the 1-vint share is
99.1%, from x1.19 and 78% at the start of the day. Two causes, both fixed: the FB restore
copied all 13 tilemap pages every flip (14d83721), and the game tick was gated on no window
being posted, so 21338 of 23494 master passes did nothing but re-read COMM0 (265de878).
check-demo also gates the FB's tilemap staging against SDRAM truth at every tick now
(a9649a56), and `make ... PORTRIG=1` posts the clock and the share on the rig's value
channel -- verified as far as the 68K's relay, where the port's palette paint beats the CRAM
flood (ab7f1ad6, the fix shape is MDS_VALUE's alternating windows). Neither rig answered
today. The instrument is `make ... PORTHIST=1`: pp_hist bins
each master frame body by the vints it spanned (1 = the port kept arcade time), and pp_sec_*
bracket the body's sections with the FRT, ISR time subtracted so the decomposition is
disjoint. Vints lost to passes that span 2+ vints track the clock's excess exactly (407
against 408 frames when first measured). What was found, in order:
  * THE FB RESTORE, fixed (14d83721): flip_finish_rest restored all 13 tilemap pages on
    every flip because pg_watch is never cleared in a PORT_VIDEO build. 0.407 -> 0.0476
    vints a call, and three quarters of the whole clock gap.
  * THE PAGE HASH, open and handed over (below): 0.27 vints per game frame, now the largest
    single section, 15% of every overrunning pass.
  * The port tick is 0.144 vints, the vint ISR 0.146, every WAIT together 3% -- the 68K's
    COMM0 post answers in 41 FRT, the slave's SYNC in 149. Nothing waits on anything.
  * 59% of what is left is body compute outside every bracket, in a much smaller pie (165
    lost vints against 502).
  * Never executes in a port build: CRAM paint, text capture, the strip compose at
    m_main.c:14080, cs_flush, nat_ph_flip, and every RIGTASK-labelled unit the rig barcode
    counts. The compose and blit the renderer is built around are not in the master's
    frame-body path at all.

OPEN, FOR THE PORT THREAD (renderer lane, 2026-10-06): mark the tilemap page dirty AT THE
PORT'S STORE LOOPS -- `port_tile_dirty |= 1 << ((addr - 0x06019000) >> 12)` inside the two
`move.w (A0)+,(A2)+` blits (FUN_0000258A / FUN_00002552) -- so the renderer can delete the
13-page content hash it polls with. The hash costs 0.363 vints per game frame, more than the
game tick itself (0.213), and it is 22%% of every master pass that overruns a vint, to find
0.045 changed pages per frame. Marking from the ADDRESS macros instead (A68VID/A68VIDA) was
built and measured: 6 of 90 changes, 93%% missed, because the port stores already-translated
SH-2 pointers in work RAM and never re-forms the address. The renderer's half is in place
(`port_tile_dirty`, `make ... PORTTILEDIRTY=1`).

THE PICTURE IS NOT A FUNCTION OF GAME STATE (2026-10-08). The record half of the gate is now
layout-insensitive: with the frame latch in the key, HEAD and a 688-byte .ramtext pad both
read 0 of 20. THE PICTURE HALF CANNOT BE FIXED THE SAME WAY, and the measurement says why.

At a target where ALL FOUR key parts agree -- tick 0228, game frame 2034, latch 0, and every
live record identical -- HEAD and the pad differ by 5148 of 69632 framebuffer bytes (exactly
the count the gate reports). The framebuffer stores PEN INDICES, so the obvious explanation
was the allocator handing the same art different pens; resolving both through their own CRAM
kills that: COLOURS differ by 6127, MORE than the indices. The picture is materially
different, in colour, for the same game state, with the only difference between the builds
being 688 bytes of DEAD SPACE.

So the renderer's output depends on its own allocation HISTORY -- which colour sets are
pinned to which lines and slots at that moment -- and that history is path-dependent: shift
the timing and the order of events changes with it. This is the same allocator the eye
scene's census pointed at (mdp_assign_set, mds_pin, MDS_REFUSE).

WHAT THAT MEANS FOR THE GATE. Keying the picture on game state cannot work, because game
state does not determine the picture. Three ways out, none free, and the choice is Mike's:
  a. Make the renderer deterministic given game state -- fix the allocator's path dependence.
     The right answer, the largest, and it is also what the eye scene needs.
  b. Compare the picture against the ARCADE (attract_parity) instead of the port's own past.
     Already the oracle for the "wrong on both machines" class, and immune to this entirely.
  c. Tolerate allocator churn in the threshold. 6127 bytes against a STATIC control is far
     too large for that to be anything but blindness.

MEANWHILE THE RECORD HALF IS SOUND AND CAN JUDGE A CODE CHANGE ON ITS OWN: 0 of 20 under a
pad is the proof. A .ramtext batch can be accepted or rejected on records plus the rig, with
the picture read as advisory until (a) or (b) lands.

THE GATE READS 20 OF 20, AND THE TWO STANDING DEFECTS WERE NEVER DEFECTS (2026-10-08).
A 0074 (39 bytes) and A 00c4 (59 bytes) have been reported as HEAD's own for two days. They
are LATCH PHASE: the gate was comparing dumps taken on opposite sides of the game's own
frame boundary.

HOW IT WAS FOUND. A .ramtext pad of 688 bytes -- DEAD SPACE, nothing hot moved, no function
changed -- shifted the port 4 wall frames, changed WHICH targets fail (A 0074 + A 00c4
became A 0140 + B 0246) and took the picture from 0 of 20 to 7 of 20. A change that cannot
alter behaviour altering the gate is the gate's defect, not the build's. Diffing the two
builds at the SAME tick AND the SAME game frame (head f804, pad f800, both tick 0x140, game
frame 784) gives 35 differing bytes, and among them 0xFFF01C: head 0x00, pad 0x01.

0xFFF01C IS THE GAME'S OWN FRAME LATCH -- the cell the arcade's IRQ4 handler self-gates on
(latch clear = publish and advance, latch set = bump 0xFFF144 and do nothing). It says WHICH
SIDE of the game's frame boundary a dump sits on, so two dumps at the same tick and the same
game frame are still different moments when it differs, and the records that differ are one
frame of work apart rather than wrong. Without it the gate measured where the master's vint
happened to land inside the game's frame: a layout lottery.

check_demo now carries latch_of() and uses it in both frame searches -- the nth-anchored one
requires it to equal the arcade's, the tick walk prefers it and falls back to tick distance.
RESULT: HEAD 2 of 20 -> 0 of 20 ("every live record matches the arcade at all 20 ticks"), and
the 688-byte pad ALSO 0 of 20, which is the point: the same reading under a layout change.

STILL OPEN, THE PICTURE HALF. The pad reads picture 7 of 20 where HEAD reads 0, because the
baselines' sidecars carry tick, game frame and adopting rom but NOT the latch, so the check
compares a framebuffer sampled at one phase against a baseline taken at the other. Same fix,
same shape: put the latch in the sidecar and re-adopt. Until that lands the PICTURE cannot
arbitrate a layout change either, and card 2 still cannot land a batch on it.

CARD 2, THE SPEED CARD: THE SH-2 PROFILE EXISTS AND BATCH 1 IS REVERTED (2026-10-08).
ares-headless --profile gives per-PC instruction and cycle counts for BOTH SH-2s;
tools/sh2_profile.py attributes them to rom/s16.lst symbols and ranks by cycles
(tools/pc_profile.py samples the 68K and is the wrong instrument for the port tick).

MASTER SH-2 OVER 1800 FRAMES OF ATTRACT, with where each function lives:
  _m_main                 SDRAM  49.62%   one PC at 0x06034874 is 9.3% of all cycles: the spin
  _visr_vbi               CART   10.70%
  _build_maps_chunk       SDRAM   4.99%
  _port_object_dispatch   CART    2.39%
  _flip_span              CART    2.01%
  ___ashlsi3_r0           CART    1.93%   libgcc: needs a linker placement, not an attribute
  _port_pal_probe         CART    1.83%
  _blit_half              SDRAM   1.76%
  _mdp_note_tile          CART    1.42%
  _irq4_handler           CART    1.26%
The hottest symbol is already in SDRAM and half of it is the idle spin. The targets are the
CART-resident ones, about 23% of master cycles -- which is where the FPGA's penalty lives and
exactly what ares cannot see, because it charges instruction cycles only.

.ramtext IS NEARLY FULL: __ramtext_end 0x06037B50 against a region ending at 0x06038000, so
1200 BYTES FREE, not the 28 KB the slot's extent implies. Any batch is sized against that.

BATCH 1 (visr_vbi 572 B + mdp_quant 116 B = 688 B, 11.66% of master cycles) BUILT, GATED AND
REVERTED. It moved the gate: check-demo 2 of 20 -> 4 of 20, picture 0 of 20 -> 10 of 20,
clock x1.015 -> x1.013, and every target's frame shifted 8-10 (A 0074 597 -> 605, B 0246
2135 -> 2133). On ares a move from cart to SDRAM should cost NOTHING -- no cart-fetch model --
so this is not the move paying off, it is the same resolution floor the MD side measured at
64 bytes of dead .data: changing the layout moves the port's timeline and the gate with it.

THE NEXT MEASUREMENT IS THE CONTROL, not another batch: a NO-OP layout change of the same
688 bytes (pad .ramtext, move nothing hot) gated the same way. If it moves the gate by the
same amount, the batch is innocent and the gate needs a layout-insensitive anchor before any
speed work can land; if it does not, batch 1 really changes behaviour and the reason is in
visr_vbi's placement, not its speed. Until that control exists, NO .ramtext batch can be
told apart from noise, and that is the blocker on card 2 -- not the ranking, which is done.

ALSO NOTE, for when batches do land: the payoff is a RIG measurement against the line of
34.8 ticks/s and 39% one-vint (NOT 47.5 / 73%, struck 2026-10-09) with THREE launches and
differenced readings. ares will show nothing either way. BATCH 1 HAS NOW BEEN MEASURED AND
CONVERTS ZERO: seven launches on mister.office.local, 2026-10-09, shots at 82/152/222 s --
pc9rt1 reads within 0.5 ticks/s and 2 points of pc9rig at every elapsed point, both signs.
It stays on port/batch1.

THE HOUR DEATH IS PARKED: THE 16-BIT-COUNTER CLUE IS WRONG (2026-10-08). The reasoning was
that the rig blacks out at 57 minutes, twice, and 65536 / 3420 s = 19.2 -- the rate the port
presents frames at -- so a uint16_t stepped once per presented frame saturates exactly there.
CENSUS of m_main.c's static and file-scope uint16_t counters that are stepped: SEVEN, and
that is all of them -- tv_real (454), trip_launch (1589), md_pending (2001), tm_gen (3452,
already guarded against wrapping to 0), sb_vc (10549), bc_vc (10659), glow_pend_run (17368).
PORTGENSEED=1 now seeds all seven to 0xFFF0 at their declarations (GS16), four of them being
FUNCTION statics that no boot-time loop can reach.

RESULT: not black. From 0xFFF0 each crosses 0xFFFF within about 16 presented frames, and the
probe ran 2400; it renders normally at 600, 1300 and 2400 with the same non-transparent
pixel counts as the control, while differing from it byte-wise -- so the seeds WERE live and
the saturation simply does not kill the picture. PAL_SETGEN was disproved the same way
earlier. The whole class is out.

WHAT IS LEFT FOR IT, when it is picked up again: the death is on the RIG and has never been
reproduced on ares at all, so the next move is not another counter -- it is to establish
whether ares reproduces it over an hour of emulated time, because every probe so far has
assumed a mechanism that would show on both. The bars dying with the picture says the frame
body stopped painting, which is a master-side stall or a flip that never lands, not a
palette memo.

THE EYE IS PARKED, WITH ITS CENSUS (2026-10-08). The MD allocator's own counters answer
"which stage refuses this cell", and they had never been read on a port rom because
MDALLOCWHY=1 does not LINK there -- the arrays are about 1.5 KB and the port's .bss margin
to the region block is a few hundred bytes. Moving them to probe scrap (0x26038400, under
PORT_PLAY, MD_ALLOC_WHY only) makes the census readable; that change is kept.

CENSUS AT THE EYE FRAME, cumulative to frame 1300, split build:

  stage                                  before     after routing every site
  cells reaching the allocator           270799     ~310000
  key already resident (hit)             266788     310292
  REFUSED: not in the scene table           3173       1528
  blanked: FG cell empty                   42154      75186
  blanked: slot dirty (cut / no cut)    2869/6786
  blanked: bottom band                      3517
  mds_install calls                            4          6

THE TOP REASON WAS REFUSAL, AND IT WAS NOT ONE SITE BUT FOUR. md_round is assigned from
MD_ROUND_GET() at m_main.c 8267, 8296, 8387 and 15625; routing only the first two through
the key meant the other two reset md_round to the raw round and reinstalled round 0's table
immediately after. Routing all four halved the refusals, 3173 -> 1528, and took installs 4
-> 6. THE FACE STILL DID NOT DRAW: 96.6% transparent against 97.2% at HEAD.

So refusal is NOT the whole blocker and the eye is parked here rather than chased further.
What the census says to look at next: "blanked: FG cell empty" is the largest single blank
reason and it GREW (42154 -> 75186) as refusals fell, which is the shape of cells getting
past the refuse rule and then finding no art in the slot -- the set-to-slot assignment
(mdp_assign_set ran 14 times) and the pin budget, not the table and not the refuse rule.

THE 57-MINUTE DEATH: PAL_SETGEN IS NOT THE CAUSE. Seeded to 0xFFF0 (PORTGENSEED=1, kept),
every set 15 steps from the 0xFFFF that aliases cram_keygen's invalid marker, the rom
rendered NORMALLY through 2400 frames. The control says why: generations read max 209 at
frame 600 and 32 at frame 2400 -- small, and NOT monotonic, because they reset per scene, so
0xFFFF is not reachable in an hour. A trap worth recording: the first "control" was the
seeded rom itself, because `make port PORT_EXTRA=...` overwrites build/port/rom/s16.32x --
both sides read 0xFFF1 and agreed perfectly. Next suspect, per Mike: the next uint16_t
stepped once per presented frame -- the file-scope ones are md_pending, glow_pend_run,
trip_launch, tv_real and cb_dirty (tm_gen is already guarded against wrapping to 0).

OPEN CARD -- THE DEMO HALF'S OWN MISSING ART (2026-10-08, pre-existing, not caused by the
split). The corpus says the demos reference colour sets 19, 20 and 21 -- 337,562, 306,930
and 30,359 cells over the round-0 frames, 674,851 together -- and round 0's table does not
carry them, so they render as BACKDROP today, the same defect as the eye's face and in the
scenes that matter most. They cannot simply be added: round 0's 29 shipping sets already
pack to MD lines [14,14,15], one line completely full and 43 of the 45 usable slots gone,
and sets 19-21 need four colours more than remain (their colours are 0x140, 0x180, 0x1C0,
0x1FF and 0x007).

OPEN CARD -- THE EYE DEFICIT, AND ITEM 1 IS ANSWERED: THE LAYER IS THE MD TILE PLANES, BOTH OF
THEM, AND THE MECHANISM IS NOT NEW (2026-10-10). The picture axis' number is the control's
55.1 at the eye anchor and 49.0 at eye pan, at matched game frames (tools/anchor_nine.py).
Identified before theorising, from dumps on both machines at the same game frame -- arcade
frame 1180 / our frame 1203, gf 1170 -- with 4:3 copies in screenshots/eye-1180/
(arcade_1180_43.png, ours_1203_43.png, ours_1203_fb_only_43.png; the directory is gitignored
like every capture):

  THE ARCADE DRAWS THE EYE FROM THE TILEMAP, essentially nothing else:
    tilemap 0x400000   2382 live words, pages [1152, 468, 0, 0, 0, 462, 300, 0] -- FG 0/1, BG 5/6
    text    0x410000   16 live words
    sprites 0x440000   2 live records of 128
    picture            49.1% non-black, 29 distinct colours

  WE DRAW ALMOST NONE OF IT, and what reaches the screen is the framebuffer alone:
    our composite      10.4% non-black, 4 distinct colours
    our FB alone       9.9% non-black, 13 distinct colours (tools/fb_png.py off the same dump)
    plane A 0xC000     792 of 4096 cells live, by CRAM line [0, 510, 281, 1]
    plane B 0xE000     1334 of 4096 cells live, by CRAM line [7, 1071, 180, 76]

  So the two planes hold 2,126 live cells and contribute about HALF A POINT of picture: the
  cells exist, name slots, and resolve to BACKDROP. The ~39 points the arcade draws from tiles
  is the whole 55.1, and it is carried by plane A and plane B together -- not sprites (2
  records), not text (16 words).

AND THE MECHANISM IS THE STATIC HALF'S MISSING TABLE, WHICH MEANS THIS CARD AND THE SHELVED
ONE ARE THE SAME GAP. The eye anchor's corpus frame (1176) reads key 0xFFF022 = 0x0C: the eye
is a STATIC-half scene. The arcade's eye references colour sets 22-36, fifteen of them, and
every one is in the static half's list (11, 22-46, 114-126). Our table carries 30 sets, 72-101,
and ZERO of the fifteen. A set absent from the table is REFUSED an MD line and renders as
backdrop (pal_rounds_md.h's own header), which is exactly what 2,126 cells of nothing is.

  So no colour bisect and no new mechanism is called for. The fix is the static-half table --
  round_split.py's table 10, 39 sets, 31 colours, which fits THREE lines [15, 14, 6] and does
  not even need a fourth. The blocker is the ART BLOB: 172,032 B for 42 blocks against 34,304 B
  of headroom, which the tables-5-9 art skip (262,144 B) pays for. docs/design/SCENE-KEYED-COST.md
  has the chain; THE SHELVED CARD'S STATIC HALF IS THIS CARD'S FIX.

  ONE OPEN THREAD, NOT THEORISED: the composite shows 4 distinct colours where the FB alone
  holds 13, at matching coverage (10.4% vs 9.9%). What reaches the screen is the FB's shape
  with fewer of its colours, and that is unexplained.

  Gate when there is a candidate: anchor_nine, control against it, eye and eye pan beside all
  nine anchors so a fix here cannot cost the static half unseen.

CARD CLOSED AS BUILT, NOT LANDED (2026-10-10, Mike). THE FOUR-LINE ADDITIVE TABLE IS A
MEASURED NO. At MATCHED GAME FRAMES -- tools/anchor_nine.py, each rom bisected to the frame
holding the arcade's own game frame at each anchor, which is the only alignment that holds
between two roms whose vints-per-game-frame differ -- control 8010d9f6 against 4add 7943cb57:

  anchor         arcade   gf    ctl    4add    delta        anchor      arcade   gf    ctl    4add   delta
  boot card          20   32   55.0    55.0    -0.0         face          1060 1053   91.9   74.0   -17.9
  logo rewrite      205  217   53.4    24.8   -28.5         eye           1180 1170   55.1   55.8    +0.7
  logo red          294  306   72.1    85.5   +13.4         eye pan       1360 1350   49.0   49.3    +0.3
  cut black         444  456   34.6    13.6   -21.0         demo 2        1500 1471   96.8  110.2   +13.5
  demo scene        465  458   82.8    87.3    +4.5

BOTH DEMO ANCHORS MOVE AGAINST IT, and the demo half is what the fourth line was for: demo
scene +4.5 and demo 2 +13.5. It reverses the card's one apparent pass -- demo 2 read -4 through
the cut-offset instrument and +13.5 anchored. LINE 4 HELPS THE STATIC HALF AS ALLOCATOR
HEADROOM (logoRW -28.5, cutBlk -21.0, face -17.9) AND FAILS THE DEMO HALF, because dynamic sets
EVICT 19/20/21 from it: mdp_s_line == 4 reads [19,20,21] at the face anchor and [0] at the eye
anchor on 4add, so what the extra line buys lands wherever the allocator puts it and not on the
sets the demos need (m_main.c:4125 searches l < MDP_LINES).

THE PRICE: 4.65% MORE VINTS PER GAME FRAME for identical game progress -- the control reaches
state 0xFFF031 == 0x0C at port frame 473 and 4add at 495, BOTH at game frame 461 -- against a
speed axis at 58% of 60 (34.8 ticks/s, four rig launches).

  Weak cells, said once: cut black and DEMO SCENE sit where the arcade's game-frame counter
  barely moves (gf 456 at arcade 444, 458 at 465 -- two game frames across twenty-one, the
  display blanked across the cut), so alignment has little resolution at either; demo 2 at
  gf 1471 has no such problem. Boot card's minima land at the window's low edge. The absolute
  numbers (55-110) are higher than attract_parity's rows because this metric fixes the moment
  instead of searching twelve lag positions; only the deltas compare.

THE RECORD IS port/mdlines4 (a72523fd) and it does not land. What is KEPT from it: the
allocator cap is DROPPED (it costs face 23 points); bisect 2's publisher fix IS kept and
cherry-picked as 903df0e3 (the fourth line's words were outside `chg`, and sc[1] bit 15 is the
only thing that makes the 68K DMA CRAM at all); tools/anchor_align.py and tools/anchor_nine.py
are the instrument this card produced, and attract_parity now carries the rule that a row at
another rom's offset is not a reading.

THE FOURTH MD CRAM LINE IS FREE DURING THE DEMOS (ANSWERED 2026-10-08) -- AND THE LINE INDEX
IN THE QUESTION WAS WRONG. The MDSPR line is CRAM line 0, not line 3: mdspr_consume DMAs its
15-word palette block to CRAM 0x02 (md_main.c:612) and the tile planes' pal_shadow_w[48] goes
to CRAM 0x20 (md_main.c:1237), so lines 1-3 are the TILE lines -- as STATE already said at
2307 -- and no sprite can ever name line 3. Asking "does a sprite name line 3" would have
answered "no" for the wrong reason.

THE CENSUS. tools/mdspr_census.py walks the VDP's OWN sprite attribute table (VRAM 0xF000, 80
entries, followed by its LINK field exactly as the sprite engine does) at 86 frames, 25 to
2150 step 25, on build/port/rom/pc9rt1.32x, and spot-checked on the plain gate rom
(da0f40fc) at 600/1100/1600/2100:

  ZERO drawn SAT entries at EVERY frame -- the table holds entry 0 only, all words zero,
  which is the link-0 terminator. And MDSPR_CNT (sdram 0x28E20) reads 0 SAT entries shipped
  and 0 records claimed CUMULATIVELY at frame 2150, so no sampling gap between two frames can
  hide a claim that happened and ended.

AND THE CLAIM PASS IS NOT DEAD CODE -- it runs and claims nothing. MDSPR_PAL[0], the pass's
own "entries live" debug word, reads 0 at every frame WHILE pens 1..14 beside it track the
scene: first written between frames 610 and 615 (eee eee eee eee ece ece ece eae... at 615;
a60 420 420 420 200 200 000 eee... at 1500), and MD CRAM line 0 carries 13-14 of those
colours from frame 625 to the end. The port PAINTS the sprite line every vblank and never
REFERENCES it. (Why: mdspr_claim is called at native chain launch and at the landing fill
site, and its v1 claim rule needs pp==2, native, key baked and palette-coherent -- under the
port nothing qualifies.)

SO THE DEMO HALF GETS 15 MORE COLOURS, and the headroom goes from +1 to +16: 4 lines x 15 = 60
usable against the demo half's 44 and the static half's 31. The pack is group-constrained, so
round_split.py's exact search is still what decides -- but 61 together still does not fit one
table, so THE SPLIT SURVIVES, by one colour.

THE PATH, and all three steps are already parameterised:
  1. tools/round_split.py:42  LINES, PER_LINE = 3, 16  ->  4, 16
  2. TILECRAM_LINES=4 python3 tools/bake_tilecram.py --live-dir discover/cram/wide \
       --emit-mds sh_src/pal_rounds_md.h --union-scene-sets --ring mame/altbeast.zip
     (the emitter is LINES*SLOTS everywhere, so mdr_line_c comes out [N][64] on its own; the
     blob format has always been "5 scenes x 4 lines x 16 words" with the fourth 0xFFFF)
  3. build with MDLINES4=1 MDSPROFF=1 -- MDP_LINES4 carries an #error against MD_SPR by
     construction and MDSPROFF is what filters it out
  THE WARNING AT tools/bake_tilecram.py:78 IS STILL TRUE OF THE LINE AND IS NOW FALSE OF THE
  PORT. It says a four-line pack "silently assigns sets to a line the background allocator
  does not own ... every shipping build has MD_SPR" -- and in round 0 that was the sky and
  every tree. The census is exactly what makes that line unowned, and only under the port. Do
  NOT take the fourth line on a NATIVE build without re-running the census there.

THE CARD WAS STARTED ON THE CENSUS'S ANSWER AND IS NOW BLOCKED SOMEWHERE ELSE (2026-10-09,
branch port/mdlines4 = 1b00ebb1, two builds spent, neither links). THE COLOUR BLOCKER IS GONE:
tools/round_split.py --demo re-packs round 0's table WITH the sets the demos reference, and the
control and the experiment are one tool -- 3 lines "does not pack into 3 lines of 15", 4 lines
gives 12 colour groups in lines [15, 15, 13, 11] of 15, six pens spare, round 0's own 30 sets
keeping their own descriptions (only 19/20/21 come from table 9, the only table that describes
them). Sets 20 and 21 cost NO colours: they want 0x007 and 0x140, already in set 19's group. The
whole demo-half colour cost is set 19's four colours.

  BLOCKER 1 -- THE ART BLOB, measured twice against the 540,672-byte gap between .tilesmd
  (0x26C000) and .porttest (0x2F0000), mars.ld:177:
      3 lines / 30 sets (the line)   518,656   fits, 22,016 spare
      4 lines / 33 sets (19,20,21)   551,424   OVER BY 10,752
      4 lines / 32 sets (19,20)      547,328   OVER BY  6,656
  Dropping set 21 (4.5% of the three sets' cells) bought ONE 4 KB block, not the three needed.
  THE LAYER-MASK RECOVERY CANNOT PAY FOR IT: bake_tiles_md.py:88-95 zeroes the mask for rounds
  0-4 deliberately, on Mike's level-1 boss capture (a set the audit lists BG-only draws on the
  FG at the boss gate; "the transparency shows as purple"), so round 0's sets must keep both
  variants. The 461 KB recovery is the OTHER one -- tables 5-9 need no art once the attract is
  key-selected -- and it rides the static-half split, which is not in. The alternative is a
  cart-layout decision (lower gen_selftest.py's --max so .porttest shrinks), and .porttest
  exists because 24% of the self-test corpus can ONLY be verified on the target. MIKE'S CALL.

  BLOCKER 2 -- MDLINES4 IS NOT WIRED TO CRAM ON THIS BUILD, three sites, all read off the
  build's own flag stamp and the source:
      a. the 68K consume is 48 words to CRAM 0x20 (md_main.c:1232 length 0x9330, :1237 dest
         entry 16) and pal_shadow_w is [48] (:683) -- the fourth line's pens have nowhere to
         go. It wants 64 words at CRAM 0x00 with entry 0 re-blacked, which :1239 already does.
      b. the SH-2 publishes 48 CRAM words in the packet (m_main.c:628).
      c. MDSPROFF filters -DMD_SPR/-DMDSPR_TOP/-DMDSPR_WHY out of SHCCFLAGS ONLY
         (Makefile:3655); the 68K's -DMDSPR is a separate make variable and the stamp still
         carries it, so the SH-2 stops producing the sprite SAT and palette while the 68K keeps
         DMAing 15 words to CRAM 0x02 every vblank -- into the entries the fourth tile line
         would occupy.
  So "MDLINES4=1 MDSPROFF=1" is a three-site change, not a flag flip, and no picture read off a
  4-line build means anything until a/b/c are done.

WHEN THE SPLIT RETURNS, TWO MEASURED RECOVERIES COME WITH IT (2026-10-08):
  - A SPLIT SUB-TABLE INHERITS ITS SETS' LAYER MASKS from the table they came from. A set
    with no record defaults to 3 (both variants, 4 KB); 36 of the static half's 39 sets draw
    on ONE layer in their harvested table, so the cost is 42 blocks and not 78 -- 72 KB.
  - TABLES 5-9 NEED NO ART ONCE THE ATTRACT IS KEY-SELECTED. They are the harvested attract
    scenes; MD_ROUND_GET masks the round with 7, so 8 and 9 were never reachable, and with
    the attract picked by its key and round bytes >= 5 clamped to 0, nothing can select 5-7
    either. They stay in the header because round_split.py derives the attract table's pen
    maps FROM them, but skipping their blocks took the blob 293 -> 229 (461 KB), which is
    what makes the split fit the 560 KB gap at all.

THE STATIC SUB-TABLE WAS BUILT AND REVERTED: THE TABLE WAS NEVER THE LAST BLOCKER
(2026-10-08). Everything the split needed works -- tools/round_split.py derives round 0's
static half from the corpus, packs its 39 sets into lines [15,14,6], emits table 10 with all
five arrays while leaving tables 0-9 byte for byte, and the blob indexes all 39. The key
reads 0x0C and the round 0x00 in the port's own work RAM at the eye frame, so the selection
fires. AND THE FACE STILL DID NOT DRAW: 97.2% transparent before, 96.6% after.

THREE LAYERS HAD TO BE PEELED TO LEARN THAT, each worth keeping:
  1. mdr_s_map alone does nothing; a set also needs mdr_s_line, mdr_s_used and its colours
     in mdr_line_c (already recorded).
  2. The art blob has a 560 KB gap (.tilesmd 0x26C000 -> .palscenes 0x2F8000) and the
     repack cost 41 blocks, overrunning it. Two recoveries, both measured: a split
     sub-table can INHERIT its sets' layer masks from the table they came from (36 of 39
     sets draw on one layer, 78 blocks -> 42), and tables 5-9 are selectable by nothing once
     the attract is key-selected, so their art need not be baked (293 -> 229 blocks, 461 KB).
  3. THE INSTALL IS GATED ON THE DETECTED SCENE, NOT ON THE TABLE. mds_install runs only
     when PALSTATIC's detected scene moves to a different table; a key change is invisible
     to it. Adding a key-driven install made the picture move but not improve.

PARITY, best over the ladder, all nine scenes:
  scene          HEAD   split only   split + key-driven install
  boot card       51       55            51
  logo rewrite    19       24            24
  logo red        27       26            27
  cut black       35       43            45
  demo scene      45       46            45
  face            19       20            15
  eye             44       34            45
  eye pan         27       25            25
  demo 2          48       47            46
The split alone took the EYE 44 -> 34, its target, but cost boot card and cut black; adding
the key-driven install gave back the eye and cost cut black again. Neither is a clean win,
so both are reverted per the standing rule. NOTE the OFFSET moved 25 -> 23 between HEAD and
the split, which shifts every column -- these numbers are indicative, not a clean A/B.

THE REMAINING BLOCKER IS NOT THE TABLE. The art is baked, indexed, selected and still not
composed, so the next suspect is the set-to-slot assignment (mdp_assign_set, mds_pin and the
MDS_REFUSE rule) -- 39 sets is more than any round has ever pinned at once, and a refused
set renders as backdrop exactly like an absent one. That is where the next hour goes, not
into the tables.

KEPT FROM THE ATTEMPT: tools/round_split.py (the derivation, report-only by default) and a
real bug in tools/check_tilesmd_bytes.py -- it hardcoded the blob index as 0xA00, which is
10 tables x 128 x 2, so an 11th table moved the 68K's art-base immediate to 0x96CB00 and the
guard called a correct rom "compiled with the wrong TILESMD_CART". It derives the index from
TILESMD_SCENES now.

THE ATTRACT'S MISSING ART: THE KEY IS FOUND, THE STATIC HALF FITS, THE DEMO HALF DOES NOT
(2026-10-08). The eye scene draws nothing across the face because its colour sets (22-32)
are absent from round 0's table, and "a set absent here is REFUSED an MD line and renders as
BACKDROP" (pal_rounds_md.h's own header). All 497 arcade dumps read round 0 at 0xFFF142, so
the whole attract is one round, and its table was harvested from three sampled GAMEPLAY
scenes -- 30 sets, 72-101, all level art.

DERIVED FROM THE CORPUS (tools/round_sets.py): dump the arcade's tile RAM at the 497 corpus
frames, collect set = (word & 0x1FFF) >> 6. Round 0 references 71 sets, not 30. The 42
missing ones are 11, 19-21, 22-46 and 114-126 -- the last being THE TEXT LAYER. Every one
already has a measured pen map in a harvested attract table (rounds 5-9); only 11 and 115
disagree between two tables.

THE SPLIT KEY IS 0xFFF022. Labelling each corpus frame by which art family its tile RAM
references: 431 frames demo, 58 static, 8 neither, AND NOT ONE FRAME DRAWS BOTH -- the two
halves never share a screen. Seven work-RAM bytes separate them; 0xFFF022 is the cleanest,
single-valued on each side:

  0xFFF022 = 0x0C   title / high-score / post-demo      (0xFFF024 mirrors it at 0x0D)
  0xFFF022 = 0x09   demo A and demo B                   (0xFFF024 = 0x0A)

It LEADS the art at the high-score -> demo-B cut (key flips at frame 1482, art changes at
1486) and never lags: at the demo-A -> high-score cut the art blanks at 1167 and key and art
land together at 1168. 0xFFF031, the gate's own anchor, also separates them but takes five
values including a transitional 0x1C.

THE BUDGET IS NOT BLOCKS, IT IS MD COLOURS. mdr_line_c[round][48] is 3 MD lines of 16, a set
is assigned to ONE line (mdr_s_line) and its 8 pens index within it, so the real constraint
is a 3x15 packing, not 48 flat. Exact search per key value:

  key 0x0C  static  39 sets (text 114-126 included)  31 colours  ->  lines [15,14,6]  FITS
  key 0x09  demo    32 sets                          44 colours  ->  NO PACKING

The demo half overruns. Its 29 sets as they ship today already pack to [14,14,15] -- one
line completely full, 43 of the 45 usable slots gone -- and the corpus says the demos also
reference sets 19, 20 and 21, which need four more colours than remain. SO THE DEMO HALF HAS
ITS OWN INSTANCE OF THE SAME DEFECT (19-21 are referenced 337k/307k/30k cells and render as
backdrop today), and it cannot be fixed by splitting alone.

NOT ATTEMPTED, FOR THE RECORD: merging the sets into mdr_s_map ALONE does nothing. Measured
-- 42 sets merged, tiles_md.bin 252 -> 330 blocks of 512, and attract_parity did not move by
one count on any of the nine scenes, because a set also needs mdr_s_line, mdr_s_used and its
colours in mdr_line_c. Reverted. tools/round_sets.py reports and REFUSES to write for that
reason; the allocation belongs to tools/bake_tilecram.py.

EVERY ROUND'S TABLE GETS THIS SAME DERIVATION FROM ITS OWN DUMPS ONCE THEY EXIST -- 
`tools/round_sets.py --round N` already takes the round number and only needs a corpus at
that round, which today only round 0 has.

THE HOUR DEATH IS 57 MINUTES, TWICE (2026-10-08, pc9rig on mister.office.local, two fresh
launches, a screenshot every five minutes). Run 1 launched 02:57:35, run 2 at 03:55:24; both
rendered normally at +52 and were FULLY BLACK INCLUDING THE INSTRUMENT BARS at +57. The bars
dying with the picture means the frame body had stopped painting, not that the game stalled.
WHICH COUNTER: the port presents about 19-20 frames a second, and 65536/3420 s = 19.2 -- so
any uint16_t stepped once per presented frame saturates right there. The one whose semantics
break on saturation is PAL_SETGEN (0x2603F880, slave-written, one 16-bit generation per
colour set): cram_memo tests `cram_keygen[slot] == gen` for a memo hit, and 0xFFFF is ALSO
the marker written to cram_keygen to INVALIDATE a slot (m_main.c:5791, 8472). When a set's
generation reaches 0xFFFF every invalidated slot compares equal to it, the memo hits over a
stale paint and that set is never repainted again. CANDIDATE, NOT PROVEN -- no fix attempted.

THE PICTURE IS WRONG ON BOTH MACHINES AND THE PORT'S OWN BASELINES CANNOT SEE IT
(2026-10-07). Mike: "rig and ares look identical", against a census of mine that said the
rig's tile plane differed. BOTH readings were aimed wrong. The census compared different
scenes (withdrawn, see below). Mike's reading holds for the scenes he looked at. At THE EYE
SCENE the two differ, and -- the point -- both differ from the arcade:

  arcade (MAME ref_001180)     full eye, brown face, white sclera, blue iris
  ares   (port, frame 1300)    the logo, the iris circle, INSERT COIN -- NO FACE, NO SCLERA
  rig    (pc9rigc, +41 s)      most of the face in flesh tones, CHECKERED green/white fill
                               inside the eye, a black blob left of centre

Side by side in the session scratchpad as eye_3way.png. THE PORT'S PICTURE BASELINES ARE
SELF-REFERENTIAL -- they compare the port against the port -- so a tile that is wrong on
both machines passes them forever. The arcade corpus is the only oracle that catches this
class, which makes tools/attract_parity.py the instrument, not check-demo's picture check.

AND THAT INSTRUMENT HAD ROTTED. attract_parity.py:27 pins SCRATCH to a DEAD SESSION's
scratchpad (a23722da-...), so --arc defaulted to a corpus directory that no longer exists --
the "nothing in the loop depends on the scratchpad" rule broken in the one tool that grades
against the arcade. Regenerated with tools/arcade_refdump.lua (RD_LAST=6000, fresh NVRAM).
Second trap in the same run: the port never blanks its display at the cut, so --port is
MANDATORY and without it the tool exits "OFFSET: not found".

SCORECARD, build/port/rom/s16.32x, OFFSET 25, mean |luma| diff, best over the ladder:

  scene          now   rd18 (--port)   rd112 (native era)
  boot card       51        -                 -
  logo rewrite    19        -                 -
  logo red        27        -                 -
  cut black       35        -               0 at lag 20
  demo scene      45       44              46 at lag 30+
  face            19        -              25
  eye             44       61              37-39
  eye pan         27        -              27
  demo 2          48       65              41-43 at lag 20+

Against rd18, the port's own last reading: eye 61 -> 44 and demo 2 65 -> 48, demo scene
unchanged. rd112 is the NATIVE era and is not a like comparison -- its "cut black 0" came
from a rom that blanks its display, which is the thing --port exists to work around. CAVEAT:
the corpus is a REGENERATION. MAME from a cold boot with fresh NVRAM should be
deterministic, but the corpus these older numbers were taken against is gone, so a small
shift from the corpus itself cannot be excluded.

ALSO SEEN ON THE RIG: THE PICTURE DIES AFTER ABOUT AN HOUR. Two screenshots at 22:33, 64
minutes into the pc9rigb run, are fully black INCLUDING THE INSTRUMENT BARS -- so the frame
body had stopped painting, not just the game. Not investigated; not reproduced on ares.

THE RIG'S FRAME RATE IS MEASURED (2026-10-07 21:25-21:30, mister.office.local, rom
pc9rig.32x sha1 37cafdc6d6eb3a4e6fa6d2f8b87cd65f1dde3051, pushed also as pc9rigb.32x).
PORTRIG=1 paints ticks/second and the one-vint share as two bars in screen rows 216..223
and tools/rig_bars.py reads them off the rig's own rom-named screenshot. THE COUNTERS ARE
CUMULATIVE SINCE BOOT, so a reading is an average over the whole run INCLUDING the boot,
and the elapsed time has to be quoted with it:

  1  pc9rig   fresh boot, 68 s elapsed    32.4 ticks/second   34.0% one-vint
  2  pc9rig   THE SAME RUN, 202 s         42.4                62.7%
  3  pc9rigb  fresh boot, 56 s            32.6                33.3%
     ares, cumulative to frame 1800 (30 s) 57.2               88.5%

Readings 1 and 3 are independent fresh boots a minute in and they AGREE: ~32.5 and ~33.7%.
Reading 2 IS NOT A THIRD LAUNCH -- /tmp/remote.log carries one `game started` for the
21:25:02 launch and none for the second POST, because the launch API no-ops on an
already-running path (the hazard is already recorded; the lesson is that pushing under a
distinct name is what makes a launch provable, which is why reading 3 went out as
pc9rigb). It is the same run read 134 s later, and that makes it the useful one: differencing
two cumulative readings on one run gives the STEADY-STATE rate, with the boot divided out.

  ticks  (42.4 x 202 - 32.4 x 68) / 134 s  =  47.5 ticks/second
  share  (0.627 x 8565 - 0.340 x 2203) / 6362  =  ~73% one-vint

  *** STRUCK 2026-10-09. BOTH NUMBERS ARE WRONG AND THEY RESTED ON ONE READING. *** Four
  fresh launches of the same rom (pc9rig 37cafdc6) under distinct names, log-proven, shots at
  82/152/222 s, read 33.4 CUMULATIVE at 222 s and differenced to 34.8 TICKS/SECOND and
  39% ONE-VINT. The arithmetic above is sound; its input was reading 2 of 336f13c7 alone, and
  a single reading is not a line. THE LINE IS 34.8 / 39%. Mike, 2026-10-09.

So the FPGA runs the port at about 34.8 game ticks a second steady state, 58% of 60, with
39% of game frames serviced in one vint, against ares' 57.2 and 88.5%. THE GAME DOES NOT
CRAWL. The branch Mike set -- near 60 means the picture path, near 0.3 means the tick
itself -- resolves to the PICTURE PATH: 34.8 is nowhere near the 0.3 ticks/second the eye
measured on pc9ctl in the 2026-10-06 session, so whatever is wrong with the picture now is
its CONTENT and not its rate. The remaining 17% against ares is the size of the FPGA
memory-traffic penalty ares does not charge, which is the known platform constant, not a
new defect.

THE TILE-PLANE CENSUS WAS WRONG AND IS WITHDRAWN (corrected 2026-10-07, same hour). It
reported "present but garbage -- the right palette in the wrong arrangement" off 122
transitions per row against ares' 23 and 8-pixel uniformity of 27.8% against 84.0%. THE TWO
IMAGES WERE DIFFERENT SCENES: the rig's screenshot is the beast close-up with a tiled
background, the ares render was the flat black ALTERED BEAST title card. 22 transitions a
row versus 118 is a title card versus a detailed scene. Mike, looking at both: "rig and ares
look identical", and the rig's own screenshot shows coherent art -- beast sprite, structured
background, the SEGA 1988 line, the instrument bar.

TWO METHOD ERRORS TO NOT REPEAT. (1) Statistics on a picture mean nothing until the two
pictures are the same scene; look at the image first, and the eye settles in one second what
the transition count got wrong. (2) The attract cannot be aligned by game tick across the two
machines: rendered at a matched tick (ares 1830 against the rig's ~1826) ares is on the title
card and the rig is on the beast scene, because the attract sequence depends on each
machine's own NVRAM state. A real comparison needs the same scene on both, or the standing
rule -- a rig-versus-rig control with the canonical rom, never rig against ares.

So the background on the rig is UNCENSUSED. Nothing here contradicts Mike's reading that the
two look identical.

A NOTE ON THE READER, because it cost the first launch: rig_bars' first signature required
a row of exactly two colours with one change. That held on ares and FAILED ON THE RIG --
FB value 0 is TRANSPARENT on the 32X, so the part of the strip the bar does not cover shows
the Mega Drive plane through it, which on ares was black and on the rig carries tiles. The
bars were painted correctly the whole time (white from x=0, runs 162 and 102); the reader
now finds the brightest pen in the image and measures its run from x=0, and reads both
machines.

OPEN, FOR THE PORT THREAD (found from the renderer lane, 2026-10-07): THE PORT ROM'S
CONTENT DEPENDS ON CUMULATIVE STATE IN /tmp, SO TWO BUILDS OF ONE COMMIT ARE NOT THE SAME
ROM. tools/cport_prep.py:111 loads /tmp/cport_overrides.json and tools/cport_build.sh:54-56
unions gcc's undeclared harvest into it and never removes anything, so whatever a FAILED
compile reports becomes a permanent `extern` in extras.h and a permanent definition in
defs.c. Measured: the build shadow's extras.h carried three symbols git's copy does not --
`DAT_0040b230` (the name the A68VID fix retired), and `uVar23`/`uVar4`, which are decompiler
LOCALS the harvest promoted to globals -- all three left over from this afternoon's failed
builds. All three are dead storage in the current corpus (the locals shadow the externs;
DAT_0040b230 appears only in its own definition), and removing them STILL moved the gate:
check-demo 3 of 20 -> 2 of 20 (B 01fe's one-byte slot-40 diff disappeared), A 00c4 60 -> 59
bytes, picture 11 -> 10, at the same clock and the same frames. Dead .bss moves the reading,
which is the same resolution floor the MD side measured at 64 bytes of dead .data.

SEPARATELY, AND NOT THE SAME THING: THE ROM'S SHA1 CHANGES ON EVERY BUILD WHATEVER THE
SOURCE, because the build stamp embeds a timestamp -- `S16Ba2b5aba9 2026-10-07 16:27:07
R60`. Two consecutive builds of one tree read c55f1b21 and 58b3cd88 with identical gate
numbers (2 of 20, clock x1.015, the same port frames), so the sha1 is a run identity and
NEVER a code identity; do not read a changed sha1 as a changed rom. It also means the gate's
dump cache, which is keyed by that sha1, cannot hit across builds: every run re-dumps all
~60 ares probes even when nothing was recompiled, which is most of the 15 minutes it takes.
Keying the cache on the rom with the stamp bytes masked would make an unchanged rebuild
free. Two consequences:
a failed build silently poisons every later rom until /tmp is cleared, and no commit records
which overlay a measurement was taken with. The fix is the port thread's: the undeclared
list belongs in the repo beside the corpus, harvested only from a SUCCEEDING compile, and
pruned of anything the corpus no longer references. Until then every gate number should name
the rom sha1 it measured (check_demo's first line now does).

ALSO: a2b5aba9's headline claim of 20 of 20 DOES NOT REPRODUCE. HEAD's source, built twice
and gated twice with identical results (clock x1.015, rom 14eb91e3), reads 3 of 20, and 2 of
20 once the overlay above is cleaned: A 0074 39 bytes (slots 17-20 + 40) and A 00c4 59 bytes
(slots 19-22, 40, 62-63), both in the attract. The high-score targets that commit was about
ARE exact -- H 02ba#7 and #13 both read 0 -- so the fix it describes landed; the headline
counted those and not the two attract targets. Not corrupted by a mid-run build: the rom
snapshot was in place and named the rom in both runs.

THE PORT'S FRAME AXIS IS NOT THE ARCADE'S AND THE GATE MEASURES IT: one probe at frame 1000
reads the scale off demo A's up-counting tick and check_demo prints it (`port clock:
x1.193` for the current build, x0.997 before PORTCLK1), and the tick walk estimates its own
ticks-per-frame from two probes. Dumps are cached per rom (8 hex of its sha1) -- they were
keyed by frame alone, so an A/B in one work dir silently read the other rom's frames
(43c79608).
PORTCLK1 IS THE FRAME CLOCK AND IT IS THE DEFAULT (3e94cc5b). `pp_due` counted vints owed
(cap 4) and the frame body drained it in a while loop, so a long body ran the game 2-4
times in one master frame and every irq4-driven counter advanced that many times: measured
107 of 2111 master frames with more than one game frame waiting, and 263 vints (12%) that
found one already owed. The flag caps it at 1 and DROPS the backlog. `make check-demo
SWEEP=1` reads 373 ticks compared, 5 differing (was 326 / 24):
  * A 0x24D and E 0x032, the two attract exits: at the right frame NOTHING but counters --
    the blink sub-step 0xFFF021 (17 frames ahead, constant since before the title) and the
    seconds counter 0xFFE014 (2 steps).
  * B 0x254, demo B's exit tick.
  * B 0x163: 0xFFF301/307/309/311, the colour-cycle index, at that one tick.
  * H 0x2BA: the high-score step's union over its ~300 frames.
The seconds counter and the colour cycle are gone everywhere else, and the blink STEP
0xFFF020 now agrees exactly. What the clock costs is the 12% of game frames it drops: the
attract takes 2507 port frames where the arcade takes 2080. The display gate is NOT in this
path (nothing gates pp_due, DISP_HOLD_MAX is a bare constant with no flag), and the arcade's
"IRQ4 mask" is the game's own frame latch 0xFFF01C: irq4_handler publishes only when the
game has consumed the previous frame and otherwise just bumps 0xFFF144 (13 `move.w #imm,%sr`
sites exist in the whole program and all are short critical sections, so none of them is it).
2026-10-05/06 fixes: the inlined cull reads the registers 0x3CD4 returned and check_demo
compares each live record's sprite-list entry (b9487cc6); the attract's install chains
establish A6 (99050c2b, 8605bf0f) -- 0x1FC2/0x20B8 are `lea 0xffffc000,%fp` with every
dependent write resolved to an absolute, so the A6 load vanished while the interleaved
`jsr 0x3B2E` and the chain's final `lea 0x80(a6),a6 ; bsr 0x250A` still ran on it: the
palette slot held cset 0x40 where the arcade holds 0x5D, and the high-score step's seventh
object landed in slot 62 instead of slot 6. The `lea <abs>,%fp` census (tools/disasm_funcs.py,
39 sites in 28 functions) IS NOW CLOSED (0b78e676): FUN_00016E1E and FUN_0001A88C restored
by a table pass, and FUN_00003036/3044/3052/3062 measured to be non-defects -- they read A6
zero times and call nothing, Ghidra having resolved their whole bodies to absolutes.
.portcode sits a few KB under the 4 MB cart end. The port's tick at a given
FRAME varies between builds and it runs two ticks in one frame when behind: compare by
tick, never by frame, and only on dumps whose port_tick_done marker equals the counter.
Every step since rd78 came from a dump against the arcade (LESSONS 2026-10-04):
  rd96   slot 48 survives: FUN_00016b6a's whole-body A6.
  rd100  FFE018/FFE098 survive main_entry's record clear (Ghidra AND gcc fold the
         save/restore -- volatile).
  rd101  word links as pointer bases through W68 (52 sites): the hero KICKS at 0x34
         and the partner spawns at 0x3A.
  rd102  FUN_00011386 hand-derived: the tick-0x76 hang gone.
  rd103  CPORT_PARAMNORM + 11 callees; ROM literals through P68: top score 50000.
  rd104  FUN_0000f74a threads A6 through its installs; FUN_0000a1e0's palette
         request on the partner.
  rd106  eleven carry/D0 helpers hand-derived, 42 call sites rewritten.
  rd107  2-mod-4 templates copied by bytes; symbol addresses stored as values are
         68K (cport_cast.py; cport_build.sh recompiles port.o from the final text).
  rd108-9 the 13 PC-relative jump tables handed to Ghidra (make_jumptables.py) and
         re-exported; the decompiler's `switch(iVar12) { case -0x23: }` handled.
  rd110  P68 is the identity for an SH-2 pointer.
  rd111  FUN_0000fd6c's hit path reads A1 from +0x24.
  rd112  the comma-form 0xE032 search re-reads A5: the trio lands in 18/19/20.
  rd113  FUN_00003706 adds the kill's points in BCD (cRam names, bcdAdjust stub).
  rd114  FUN_00011386's period read once; code-typed script pointers kept as 68K
         values (the hero's flame object).
  rd116  0xE266 publishes A2 for its callers' A2 parameter.
  rd118  `jsr e266 ; beq` tests e266's D0, not a bool computed before the call
         (found with CPORT_PROBE=103e0); `extraout_D0w == -1` is `== 0xffff`.
  rd119  the same for the nested-paren forms (FUN_00014bb8/15434/1650a).
STILL OPEN IN DEMO B (tick-aligned): (1) the credit text blinks out of phase
(FFF020/021, 26 ticks); (2) the title logo's colour-cycle phase at tick 0xA0 (red vs
white; both red at 0xD4); (3) the sound queue FFF03F-FFF055 differs (not pictured).
Gate rd112 (tools/attract_parity.py, luma): cut black 0 at lag 20, demo scene 46 at
lag 30+, face 25, eye 37-39, eye pan 27, demo 2 41-43 at lag 20+ -- the luma gate
cannot see object parity; the tick-aligned dumps do.
OPEN, in order: the other-form attack lookups (0x5062/0x7F58/0x8D66/0xBBC6/0xC9B4/
0xAF4A, 80-100 call sites each, same shape as the hand-derived 0xA3B0) and the 40
`jmp (a0)` sites they feed (OPEN CARD); the general `movea ..,fp ; jsr` publication
(66 sites, OPEN CARD); the demo's later rounds and gameplay (never measured: every
tick-aligned dump so far is demo B round 1); the boot card's bouncing text dots --
prog68k 0x15C0-0x166F fills twelve 4-byte records at 0xFFE800 and steps them into
text RAM 0x410030 for 165 frames; it is a Ghidra gap (no func_ranges entry) so the
port never runs it (same fix as the kind installers: tools/ghidra/make_kind_inits.py);
the renderer's round-2 HUD (logo palette); raw tilemap words in plane A at the boot
card; the FPGA text garbage claim is WITHDRAWN (it was the typing loop, ares showed
it too); BATTERY SAVE card below.
rd60 was:
rd59 + `_DAT_00840000` declared a WORD (prog68k `clr.w 0x840000`; the long store zeroed
text pen 1: the boot card's and title's text drew black/garbled and the PALSTATIC
probe never matched scene 0 -- pscene_cur 0xFF at f300 in every earlier port build,
the line's reads 0). rd60: pscene_cur 0 at f60/f300/f1192, set 0 exact, the HUD text
white. Gate rd60: eye 34-47, eye pan 27-31, face 29-31 at lag 12-20, cut black 13 at
lag 20, demo scene 45-49 at lag 20+, demo 2 42-45 at lag 12+, logo rewrite 31-43 at
lag 20-45, boot card 49-56 (the card's beast panels and the logo's upper half are
still missing on the MD planes with the game's tile RAM exact and STATIC -- measured
f300-f303 and f60-f61: no tilemap page changes -- so it is the MD slot/art pipeline,
not change detection; the line draws both scenes).
rd59 = rd57 + (rd58) the tilemap unpackers yield a frame per 0x1000 stores (the 68K's
pace: the port's demo tick 0 now lands at f451, arcade f464; it was f443) and the
pair's animation script read through P68 (three sites).
rd57 = rd56 + LAB_ data labels byte-typed (the eye-pan camera's script entries were
read 32 bytes apart).
STILL OPEN: the port enters state 08 at f18 where the arcade enters 04 at f18 and 08
at f24 (a 6-frame boot fill the port runs in zero frames -- same class as the
unpackers: clear_tileram/clear_textram at the 68K pace); the FPGA shots of rd55/rd56
(score card ok-ish, red-logo scene garbage) are unmeasured; the eye's small-lag
columns are the remaining ~12-frame lead.
rd56 = rd55 + the MD scene index kept under PORT_VIDEO (the eye's BG garbage was the
renderer dropping pscene_cur to 0xFF sixteen frames into the fade; the line keeps 0).
FPGA (mister.office.local): rd55 showed the high-score card with the logo and HUD and a
few wrong score-table glyphs (07:43); rd56 at the red-logo/score scene showed garbage
text tiles and a wrong BG palette (07:51) -- NOT yet measured, ares is the rig.
rd55 = rd54 + the object templates' word fields at their 68K width (CPORT_WIDTH
0xDD3C/3E, 0xDCEE/F0, 0xDDD8/DA, 0x1984E -- the zombie's palette set 0x32, slot 0
now equals the arcade's). rd55 RUNS ON THE FPGA (mister.office.local, 07:43: the
high-score card with the logo and HUD; some score-table glyphs wrong there).
rd54 was: Since rd37, eleven mechanisms
measured and fixed, each with the arcade/port pair that proved it (LESSONS 2026-10-04):
  1. demo script pointer raw (FUN_00001366): the hero read 0xFF for every input;
  2. credit writer's A0 ("CREDIT 0xFF 0xFF 7");
  3. scripted-spawn dispatch branched on a carry Ghidra read from the wrong instruction;
  4. ten kind-table installers were never Ghidra functions (make_kind_inits.py,
     re-export: 864 functions, 11 added, 1 body changed; 0x24A4 included);
  5. the installer ran with the caller's A6 (wrapper restore) -- found slot published;
  6. FUN_0000edf0/FUN_00010cd6 ROM templates raw + misaligned (ROM_BASE, LD32U);
  7. the pair allocator FUN_0000e032's carry/A5 at six installers (zombies exist);
  8. pointer registers truncated to a 68K short (A68OF16) and widened back (W68);
  9. FUN_000102cc's second template into the pair slot (A6 = A5);
 10. the hero's per-state dispatch publishes D5/D6 (the hero stood in state 9 = jump
     from its first frame with the input record identical to the arcade's);
 11. LAB_xxxx_N symbols placed at label+N (the HUD icon table read one word early).
  Plus colour_cycle_streamer's misaligned longs, port_mark_ring 192 -> 64, and the
  build script's stale-port.o guard (rd44-rd46 were measured on rd43's object).
MEASURED EXACT at the eye scene (port f1192 = arcade f1210, same tick 0x2BA): tile
RAM 13 pages 0 differing words; text RAM rows identical; palette-slot tables equal
but slot 0 (set 0x10 vs the arcade's 0x32 -- the red logo) and the HUD rows 25/26.
Gate rd54: demo scene 48 at lag 20 (was 109 flat), face 28-48 (was 100+), eye 45-58,
eye pan 44-73, demo 2 43-46, logo rewrite 27-46, logo red 27-39.
OPEN, in order:
  a. the eye scene's BG is garbage on the MD planes while the game's tile RAM is
     EXACT -> the renderer's MD slot cache under PORT_VIDEO (page-hash pg_pending,
     SLIMCAP budget, md_tag claims), not the game; persists to f1330;
  b. palette slot 0 takes set 0x10 where the arcade allocates 0x32 at the hero's
     creation (0x3B74/0x3B8E at f464) -- the logo stays white in the demo;
  c. the pair partners carry flags 0xF4/0xF5/0xFC and x 0x114B/0x495E (arcade 0x88,
     x 0x1241) -- the 0x10824 handler on a half-filled record;
  d. the port enters 0C at 441 and creates the hero at 443; the arcade waits
     447 -> 464 (jsr 3838/380a loads between 0x8E4-0x8EA?) -- frame offset only,
     ticks identical (both end the demo at 0x2BA);
  e. the wolves (f352) at tick 0xC1 and the morph timing not yet compared.
Build: `make line NOSPRBAKE=1 PORTFULL=1 PORTMAIN=1 PORTOS=1 NOGAMEHIGH=1 PORTVIDEO=1
PORTPLAY=1 PORTBOOT=1 PORTCORO=1` then `tools/save_build.sh rdN`; the per-build
scripts live in the session scratchpad (rd1dump/build_rdN.sh). Plain `make line`
restores the LINE.

rd31 (2026-10-04 ~08:20) was the previous build: rd29 + every zoom_scale_lookup
caller reloading D3/D6/D7 from the stub (104 sites; only one had it). The eye's iris
draws whole, the heroes and the title logo have their real size. Gate rd31: eye 37-45,
demo 2 43-46, demo scene 44 at lag 20, eye pan 40-79 (sprite strips remain), face 100,
logo 45/73, boot card 62. Open at the eye scene: the demo's logo text rows (4-9) are
not cleared on entry, the credit count (arcade 7, port 0: 0xFFF000 is 7 on the arcade
from its first readable frame, no writer caught), two sprite strips with wrong width.

rd29 (2026-10-04 ~07:40) was the previous build (rd27 + the default high-score
table landing: FUN_00004178's `(undefined4 *)0x4732` -> ROM_BASE). Gate rd29: eye 40-45,
eye pan 33, demo 2 43-46, demo scene 46 at lag 30+, logo 46/73, face 103. The gate is
DETERMINISTIC (rd24 twice: identical tables), so every row move is the build's.

rd27 (2026-10-04 ~06:40) was the previous build: the demo draws the two heroes,
the logo, the health bars and (c)SEGA over the round-1 background; the attract loops
through the high-score screen with ONE live object like the arcade. Since rd18:
spr_chain_id (the 32X sprite-list rebuild) and r60_disp_on (the 32X layer's enable)
were both inputs the 68K's push used to carry and the port carried as zero; the
object table was never cleared at the loop (FUN_00001338: A0 as the int -0x4000 and
three negative displacements rendered as `W68PTR(0xFFFF88u) + p_A0`); slot 40's flag
was a word stored into a uint; the demo's enemy installer took its slot as a bare
constant; the HUD palette queue writes through the 4 KB mirror. Gate (rd27):
demo 2 43-45, eye 34-44, eye pan 32, demo scene 48 at lag 20+; logo 45/73, face 100,
boot card 62 (the early scenes are the worst rows). Still missing at the demo: the
score row and "GET IT"/credits text rows (text writers), the enemies (FUN_0000ee80's
chain never runs), and the HUD palette's one-word shift.

LATER THE SAME DAY (rd18, 2026-10-04 ~05:30): the port ticks ONE game frame per vint
(0.994/frame; it was 0.83 with a flag instead of a counter) and now LEADS the arcade by
8 frames from boot. tools/attract_parity.py --port --arc <arc6k> (the gate, corpus
regenerated with tools/arcade_refdump.lua) on rd18: demo scene 44 at lag 20+, eye 61,
demo 2 65 (rd15: 44 / 111 / 80); the logo scenes 56-74 and face 105 are the worst
rows. Measured since rd12: the sprite table is written only by sprite_build_and_cull
and hide_object_sprite (arcade wp census) and the port hides every object
(hide 11,256 : build 2 in 985 frames) -- the cull in sprite_build_depth_banded is the
live suspect, probed in rd20; slot 40 never activated (flag width, fixed in rd20); the
demo enemy installer FUN_0000f28a took slot 62/63 as a bare 68K constant (CPORT_PARAMNORM).

OPEN, in order: (1) the demo's actors -- at state 0C+500 the port's 0xFFF800 sprite
table has 6 records against the arcade's 30: object spawn/dispatch parity, the lane's
old axis (A5/register file below); (2) PAL_SH sets 64-79 (sprite palettes via the
queue, blocked on the 0xFFF400 width); (3) the text layer is not drawn; (4) the title
screen's purple band and MD CRAM 43/64; (5) `tools/attract_parity.py --port` aligns
on state 0C now -- run it as the gate once (1) moves.

INSTRUMENTS ADDED TODAY: tools/save_build.sh (per-rom .md.lst), tools/md_pc.py (68K
disassembly from the rom bytes), tools/check_tilesmd_bytes.py (on every link),
tools/video_writers.lua + _hist.py (arcade writer census), tools/state_timeline.lua,
tools/cell_timeline.lua, tools/wram_dump.lua, tools/a68.py (never hand-compute an
SDRAM offset), tools/port_scorecard.py, CPORT_ARGLOG + tools/port_arglog_read.py,
the PALPROBE in port_play_tick. The MD .lst is per build; the line is not moved.

## DECOMPILE LANE — THE SEEDED-FRAME NUMBERS (2026-10-03, history)

**THE PLAN OF RECORD IS `docs/design/PORT-ARCH.md`** (Mike approved 1, then 2, then
3 on 2026-10-02). Read it before proposing anything in this lane.

### THE CURRENT NUMBERS, AND THEY ARE THE BEST EVER ON BOTH FIDELITY MEASURES

    make line NOSPRBAKE=1 NOTILESMD=1 PORTFULL=1 PORTSEED=64 PORTMAIN=1 PORTOS=1 \
         RIGBARCODE=1 WRAMCODE=1
    python3 tools/portl2_seed.py rom/<x>.32x 64
      wrong 6 of 16384   control 52   net -46        (was 8 / -44)

    make line NOSPRBAKE=1 NOTILESMD=1 PORTFULL=1 PORTMAIN=1 PORTOS=1 \
         RIGBARCODE=1 WRAMCODE=1
    python3 tools/portl2m_read.py rom/<x>.32x 3000
      PER-FRAME  mean 12.9  control 43.8   net -30.9   last frame 3 wrong of 16384
      FREE-RUN   mean 45.2  control 137.1  net -91.9   NET NEGATIVE AT EVERY SAMPLED
                                                       FRAME, f1 (-37) to f175 (-182)

FOUR generated-port defect classes were fixed on 2026-10-03, each provable from the
68K's own instruction bytes, taking free-run from +31.7 to -91.9 B/frame
(FULL-PORT.md 53-60):

  1. **41 misaligned 32-bit globals, 231 refs.** An SH-2 `mov.l` at an address 2 mod
     4 is a CPU address error; the generator covered computed derefs but not bare
     named globals. Fixed with a packed struct + `asm()` name + macro, the only one
     of three candidate forms that actually emits byte-wise code on this toolchain.
  2. **A 68K pointer held in a LOCAL across two statements.** `iVar1 = LD32S(...)`
     then `*(ushort *)(iVar1 + k)` -- the two-statement form of the `_nested_ptr`
     rule, dereferenced untranslated. That site was the level's spawn-table walker.
  3. **CPORT_HI.** A 16-bit access at a long's own address is its HIGH half on a
     big-endian machine; C's narrowing takes the LOW half. 0xFFF132 is the camera,
     and the width census says 49 movew + 1 addw reads and 5 movew writes against
     exactly 2 long ops, both inside FUN_000039C8's fixed-point advance.
  4. **Handler pointers stored into 68K memory stored an SH-2 address.** 1,154 sites
     of `*(code **)(A6 + 2) = FUN_x` plus 62 named-cell and 8 comparison forms. The
     symbol's own name carries its 68K address, so the rewrite is exact; ST32 is the
     destination because slot+2 is also 2 mod 4. **This one is worth -105 B/frame on
     its own** and it is verified closed by a sweep: no 32-bit value anywhere in the
     port's work RAM is an SH-2 address any more.

### WHAT IS LEFT IS ONE CAUSE, AND THERE IS A ONE-FRAME REPRODUCER

    make line NOSPRBAKE=1 NOTILESMD=1 PORTFULL=1 PORTSEED=108 PORTMAIN=1 PORTOS=1 \
         RIGBARCODE=1 WRAMCODE=1
    python3 tools/portl2_seed.py rom/<x>.32x 108
      wrong 67 of 16384   control 72   net -5      ALL 44 object errors in slot 0

**A5 NEVER REACHES THE PORT'S REGISTER FILE.** The arcade despawns the player at frame
9109 exactly as the port does -- the branch was never wrong -- and then RESPAWNS it in
the same frame (lives 4->3, template from ROM 0x73EC, X = camera_hi + 0x90). The
respawn is gated on bit 5 of 0xFFF028, which the despawn sets with
`bset #$5,(A4)` where `A4 = the word at A5+0x1C` and A5 = 0xFFFFE000, the player-1
record.

A5 is established INSIDE `FUN_000049BA` by `0049CC: movea.w ($3c,A6),A5`, and the port
computes it correctly -- `iVar12 = (int)W68(*(short *)(unaff_A6 + 0x3c))` -- **into a
LOCAL**. The call to `FUN_000053A8` is INDIRECT, so it goes through the generated thunk
`thk_FUN_000053a8(r) { return FUN_000053a8((int)(r[13])); }`, which reads
`_port_reg[13]` = A5, and **nothing ever writes it**. The dispatcher establishes A6 and
only A6.

The cheap textual fix is UNSOUND and that was checked: A6+0x3C is loaded into three
different address registers across the program (25x A0, 3x A1, 8x A5), so "a local from
W68(A6+0x3C) is A5" would corrupt 28 sites to fix 8. The fix needs Ghidra's variable
STORAGE, which this tree already reads, and it is the A5 analogue of
`docs/audit/a6_callsites.json`. FULL-PORT.md 62.

Proved with CPORT_COUNT, not inferred: the gate `FUN_00000DFA` runs (1 entry) and
`FUN_00003858` -- which is only reachable inside the taken branch -- runs 0 times. The
flag cell reads 0x01 at the frame boundary on BOTH machines because the arcade sets and
clears it within the frame, which is exactly why no watchpoint could answer it.

### HISTORY: THE CHAIN THAT GOT THERE (kept because the method is the point)

Before the handler-pointer fix, the divergence was three despawns:
**the port killed slots 0 (THE PLAYER), 16 and 17, which the arcade keeps alive.**
Each death runs FUN_0000EE80's path at 68K 0xEEEA, which decrements 0xFFF154 (the
scroll-speed INDEX, 2 in the arcade and 0 in the port), which makes 0xFFF158 jump
table[2]=0x20 -> table[0]=0x80, which runs the camera 4x fast, which makes
FUN_0000D80E reach the first spawn trigger (0x10E0) at frame ~114 instead of ~160,
which spawns the 13-slot cascade. Every link measured; FULL-PORT.md 58.

**And the timing names the condition.** Slot 16's position first changes at arcade
frame 9038 and slot 17's at 9059 -- exactly the port's first-wrong frames for those
slots. The port matches them byte-for-byte while they are IDLE and breaks the moment
they ACT.

The 68K guards that despawn with `cmpb %d1,%d2` (0xEED0) -- a REGISTER comparison,
this port's known weak point -- and the generated C reaches the same label through
`if (0x10ff < *(ushort *)(unaff_A6 + 0x10))`, a test that is FALSE for the arcade's
actual values. NEXT THREAD, and it is one function.

### THE OUTPUT PATH COSTS NOTHING, AND 25 KB IS THE OPEN PROBLEM

The renderer reads four SDRAM shadows the patched 68K fills (TILEMAP_U 0x19000,
TEXT_U 0x26000, PAL_SH 0x27000, SPR_SNAP 0x28400). `cport_layout.py` now places the
port's S16 video regions AT those four addresses, parsed out of `sh_src/m_main.c`, so
the port's picture IS the renderer's input -- no transport, no packet, no per-frame
cost. `PORTVIDEO=1` stops the renderer capturing the 68K's staging over the tilemap
and the sprite table. Seeded frame unchanged by the move, which is the point.

What is NOT solved: the port's non-video footprint is 25 KB (coroutine stack 8 +
sparse I/O 1 + work RAM 16) and it all sits inside the 32 KB art cache CACHE_C.
Declared in `mars.ld` with ASSERTs, not solved. What has to be counted against it is
everything the port MAKES free (the packet transport's `.ramtext`, its `.bss`
buffers, the MD-side staging), and that count has not been taken.

### NEW INSTRUMENTS (all 2026-10-03)

* `tools/cport_widths.py` — every work-RAM access width out of the 68K's own bytes,
  diffed against the port's declarations. 13 suspects ranked by use count; 0xFFF132
  is #1 (the defect that cost the day) and 0xFFF154 is #5 (the next one). Aligned per
  function from `docs/audit/func_ranges.txt`, so data is never decoded as code.
  **There is a 68K objdump in the tree** (`~/src/marsdev/mars/m68k-elf/bin/`) and
  nothing was using it.
* `port_l2m_out[208..247]` — free-run PER-SLOT first-wrong frame, per-slot wrong
  bytes, and the wrong byte-offset-within-a-slot mask. This is what turned "free-run
  diverges" into "slots 41..53, two bytes each, one per frame".
* `CPORT_COUNT=a,b,c` — entry counters for up to 8 named functions, at
  `port_l2m_out[248..255]`. Needed because PORTWATCH names the function that CHANGED
  a cell, so a function whose writes are idempotent on the current state reads as
  zero spans -- indistinguishable from never having run.
* `PORTWATCH` now arms over the LAST endurance frame too, so `PORTENDURE=N` selects
  which free-run frame is watched. Armed only on a seeded frame it was blind to every
  defect that needs free-run state.
* `tools/l1_order.lua` — traces ONE named arcade frame with MAME's own `trace` action
  (not a lua tap: those under-report on S16B). `L1O_REGS=1` adds the register file
  beside every instruction, which is how A5 was read at the despawn.
* `PORTCORO=1` — a real coroutine (`port_ctx_switch`/`port_ctx_make`, 23
  instructions). Built to test whether the lost SH-2 call stack was the free-run
  divergence; it is NOT (free-run 168.7 -> 171.4, unchanged). Kept, flag-gated and
  off: 264 bytes of measured stack depth for a whole arcade frame, and a vsync-driven
  build will need it.

**THE BASELINE, REPRODUCIBLE FROM THE TREE:**

    make line NOSPRBAKE=1 NOTILESMD=1 PORTFULL=1 PORTSEED=64 RIGBARCODE=1 WRAMCODE=1 \
         CPORT_ROOTS_BASE=<the sixteen>          # rom/A2.32x
    python3 tools/portl2_seed.py rom/A2.32x 64
      PROGRESS MARKER 0xFFFEFE  port 0x0AE2  arcade 0x0AE2      <- same position
      wrong 9 of 16384   control 52   net -43

**THE PORT RUNS THE WHOLE REAL FRAME AND BEATS THE HAND-ROLLED BODY. ADOPTED.**

    make line NOSPRBAKE=1 NOTILESMD=1 PORTFULL=1 PORTSEED=64 PORTMAIN=1 PORTOS=1 \
         RIGBARCODE=1 WRAMCODE=1                      # rom/L_sx16.32x
    python3 tools/portl2_seed.py rom/L_sx16.32x 64
      stack top  port 0x0AE2   arcade 0x0AE2
      wrong 8 of 16384   control 52   net -44         (four-call body: 9, net -43)

16,376 of 16,384 work-RAM bytes from decompiled 68K C on the SH-2, one real arcade
frame, 2.9 ms. SEVEN of the 8 remaining cells are wrong under BOTH bodies, so they
are not about the frame loop: 0xFFED00/10/11 (a sprite-list block), 0xFFF01C,
0xFFF134, 0xFFF352/353. The eighth is 0xFFF028, the credit byte.

STILL NOT DRIVING PIXELS. This is a measurement rom. The next architectural question
is the port's own output path, not more cells.

**(superseded) ONE BYTE OFF THE HAND-ROLLED BODY:**

    make line NOSPRBAKE=1 NOTILESMD=1 PORTFULL=1 PORTSEED=64 PORTMAIN=1 PORTOS=1 \
         RIGBARCODE=1 WRAMCODE=1                      # rom/J_io.32x
    python3 tools/portl2_seed.py rom/J_io.32x 64
      stack top  port 0x0AE2   arcade 0x0AE2
      wrong 10 of 16384   control 52   net -42        (baseline: 9, net -43)

Seven of the ten are also wrong under the four-call body; three are the rotated
frame's own, and it FIXES two the four-call body gets wrong. FULL-PORT.md 39-40.

NEXT: 0xFFF146/147 -- FUN_0000E052 walks its table 172 entries too far while every
documented input matches the arcade, so the divergence is a value read DURING the
frame. The instrument is PORTWATCH on 0xFFF146, not another reading of the chain.

**THE ORACLE IS NOT DETERMINISTIC.** Two identical runs differ by 13 bytes at
0xFFFF02..0xFFFF95, every frame. `docs/proof/l2m/wram_*.bin` is NOT byte-reproducible
and must not be regenerated -- the I/O capture was ADDED beside it. None of the 13
has ever appeared in a wrong-cell list.

**(earlier that day) STEP 1 AS AN ARCHITECTURE.** `frame_rotated` (0x0AE2) is rooted, the
decompiler branches for itself, and the 68K's own stack top reads 0x0AE2 -- the
arcade's value.

    make line NOSPRBAKE=1 NOTILESMD=1 PORTFULL=1 PORTSEED=64 PORTMAIN=1 PORTOS=1 \
         RIGBARCODE=1 WRAMCODE=1                      # rom/I_hand.32x
    python3 tools/portl2_seed.py rom/I_hand.32x 64
      port 0x0AE2  arcade 0x0AE2      wrong 19   control 52   net -33

**10 BYTES WORSE THAN THE FOUR-CALL BODY (-43). NOT ADOPTED.** But every one of the
nineteen is named and there is no unknown bucket: 9 are controller/dip input the
gate does not supply, 2 are FUN_0000E052's D4 parameter, 1 is a timer, 7 are also
wrong under the four-call body -- and the rotated frame FIXES two that body gets
wrong. FULL-PORT.md 38. NEXT: PORT-ARCH 1c, capture the I/O bytes per frame.

**THE LINE IS VERIFIED ON THE RIG** (mister.office.local, 2026-10-02 22:55):
`rom/night/line_relink.32x` launches and runs attract correctly on FPGA -- title
screen and the transformation cut-scene, captures in `screenshots/rig_line_relink/`.
First linkable line since e4fb3e1e, now confirmed on hardware.

**THE LINE BUILDS AGAIN** -- it could not link at all since e4fb3e1e (`mars.ld`
named three PORTFULL objects without wildcards; such a name is a DEMAND to ld).
`rom/night/line_relink.32x`, deterministic across clean builds apart from the
4-byte build stamp, region guard 0x06017DF8, attract renders correctly on ares.
NOT yet on a rig: neither mister.office.local nor mister.tv.local answered
2026-10-02 12:50.

**(superseded) STEP 1'S FIRST ENTRY POINT WAS WRONG:**
`PORTMAIN=1 PORTOS=1` roots 0x005BE and redirects `wait_frame_flag` to
`port_frame_wait`, so the game loop drives and the driver is its vsync. After three
fixes (the 16-bit pointer round-trip, `register0x0000003c` = the 68K SP 0xFFFFFF00,
that SP re-loaded per frame) the frame COMPLETES -- and it is the wrong frame:
marker 0x0094 against the arcade's 0x0AE2, 1163 wrong, net +1111, of which 868 cells
are simply RAM the state-init pass legitimately cleared. FULL-PORT.md 34-37.

**THE NEXT ACTION: a Ghidra function split at 0xAE2.** The frame loop is ROTATED
around its vsync -- `0xAE2 -> tail -> 0xB7C -> 0x97C -> body -> 0xADC` -- and 0xAE2
is the arcade's own resume point, the only entry that gives exactly one frame. 0x97C
is a clean loop head but omits the tail, including `bsr 0x144A` at 0xB04.
**CLAUDE.md's checkpoint is the FIRST action: copy the Ghidra project and back up
`docs/proof/altbeast.c`** -- every tool reads that one file and the edit is not
reliably reversible.

**READ THE PROGRESS MARKER FIRST, ALWAYS.** 0xFFFFFEFE holds the 68K address each
machine last passed. A byte count cannot tell "computed the frame wrongly" from "ran
different code", and those need opposite fixes. `tools/portl2_seed.py` prints it
first and flags a mismatch.

**TWO BUILD DEFECTS FIXED 2026-10-02, both of which invalidate older numbers:**
* `make line` could not link AT ALL since e4fb3e1e (`mars.ld` named three PORTFULL
  objects without wildcards; an object named that way is a DEMAND to ld).
* `.build_flags` never contained a single PORT* probe flag, so switching PORTSEED
  reused the old object. **Any PORT*-flag-only comparison from before 2026-10-02 is
  two readings of the same object.**

Guard: `python3 tools/port_guard.py` -- 12 known violations (8 A6 widths, 3 stack
placeholders, 1 pointer round-trip). Shrinking that file is the measure of progress.

---

## DECOMPILE LANE — EARLIER STATE (2026-09-30 late, kept for the instrument list)

Nothing here touches THE LINE (still rg2). PORTFULL is a probe build and is
mutually exclusive with SPRBAKE.

**MIKE, 2026-09-30: "We can't have a useful play candidate until the SH-2 port of
the 6k code is running. full stop otherwise."** This lane is the critical path.
`docs/handoff/CARD-DECOMPILE-HANDOFF-5.md` §0 argued the opposite and is marked
superseded. Mike also closed the IRQ4-as-signal model outright: with the logic on
the SH-2 the port is not hamstrung by the 68K and owns its own output path, so the
old framebuffer-as-message-queue transport is not a constraint on this work.

    make line NOSPRBAKE=1 PORTFULL=1 PORTTRACE=1 RIGBARCODE=1 WRAMCODE=1
    python3 tools/portl2_read.py   rom/X.32x 900      # ONE frame + the per-slot ladder
    python3 tools/portl2m_read.py  rom/X.32x 40000    # 256 frames, both curves, per-slot
    python3 tools/portl2m_zoom.py  rom/X.32x 20000    # PORTZOOM=N: 32 frames full-res
    python3 tools/port_watch_read.py rom/X.32x 1200   # who wrote a cell

Last rom: `rom/attr2.32x`. Frame cost ~0.8 ms/frame on ares, ~7% of a vint on
hardware at the 1.59x PORTTIME ratio. **The game logic FITS with room; that question
is closed.** 256 frames run with no hang and zero missed indirect calls.

**THE GATE IS 256 FRAMES = 4.3 s, AND THE LENGTH IS DERIVED.** It was 30, which was
never derived -- 30 is this repo's house unit for a sampling window AND the cadence
barrier the port exists to break. The cart holds 574 frames of delta records; the
scorer is O(k^2) so 512 would not finish before a sane dump frame. The scorer binds.
FULL-PORT.md §23, LESSONS "A HOUSE UNIT IS NOT A JUSTIFIED SAMPLE SIZE".

**CORRECTNESS — read the two curves separately, they answer different questions:**

    PER-FRAME  (re-seeds from the arcade every frame; cannot accumulate)
               mean 26.1 wrong of 16384   control 39.7   net -13.6   NET CORRECT
               last frame 28 vs control 55
               of the error: MISSED 21.0/frame + 5.1/frame unchanged-but-wrong
    FREE-RUN   (seeded ONCE, 256 frames; error compounds)
               mean 185.7   control 161.1   last 283 vs 236   net +47   DIVERGING
               of the error: MISSED 155/frame
    ONE SEEDED FRAME  (PORTSEED=64 -- the cell-by-cell instrument)
               20 wrong   control 52   net -32   <- the best single-frame result,
               and +1 and +5 of every sprite record are now CORRECT

    ENDURANCE  900 frames (15 s), host-scored at the endpoint, no cart cost:
               completes, no hang, 0.22 ms/frame = ~2.1% of a vint on hardware.
               1155 wrong vs a control of 1156 -- but read that honestly: at N=900
               the control is 7% of work RAM, so "net -1" means the port's state is
               NO CLOSER to the arcade than doing nothing. It diverges, then
               saturates. The comparison stops discriminating at long range.

    ONE-FRAME LADDER at the seed frame: control 40, full frame 28 (-12),
               slot 0 alone 14 (-26, the BEST), slot 16 alone 44 (+4, the only
               harmful one), slot 17 39 (-1), slot 40 33 (-7)

**Each frame in isolation is now right. Free-running still drifts.** The port misses
~30 cells a frame and free-running feeds them forward. The control is NON-MONOTONIC
(the arcade's state wanders back toward the seed) and the port is MONOTONIC, which is
why a mean-vs-mean comparison hid this at 30 frames -- compare port to control AT THE
SAME FRAME.

**WHAT MOVED IT, both declared widths (FULL-PORT.md §22/§24):**
`DAT_fffff142` was declared `ushort` and is a BYTE index into a five-entry pointer
table, so `<< 2` landed 1 KB out, `floor_collide` read a garbage collision list and
its branch never fired -- that branch writes three of the four cells the gate had
flagged. And the ARRAY-BASE rule (LESSONS 2026-09-27, implemented in
`gen_selftest.py:665` for the L1 harness since, never applied to the port) fixed
`extern uint DAT_ffffec80` used as `(&DAT_ffffec80)[i]`: per-frame went +12.0 ->
-2.2 and the unchanged-but-wrong class 17 cells -> 1.

**FACTS.md was missing 56 of 67 LESSONS `##` headings**, which is how that rule got
re-derived three days late. `build_facts.py` indexed `###` only. Fixed, 870 -> 937
facts. Two generators still carry two width decisions -- single-source them.

**THE REGISTER CHAIN IS BUILT AND WORKS (FULL-PORT.md §30-30e).** D3 and D6 are
THREADED handler to handler -- each handler's exit value is the next one's entry
value, proven 3 transitions for 3 -- and the port ZEROED them before every call, so
the chain could never form. Now: a register file `_port_reg[15]`, write-back of any
register parameter whose body ASSIGNS it, a RELOAD after every call (a by-value C
parameter is stale after a call; the 68000 had no copy), and the dispatcher carrying
D3/D6 into the next slot. Plus **`zoom_scale_lookup` HAND-WRITTEN from its 25
instructions of 68K** (`docs/audit/zoom_scale_lookup.asm`, via the new
`tools/ghidra/disasm_range.py`), because it has SIX outputs and Ghidra's return holds
TWO -- its decompilation is not wrong, it is structurally incomplete. All derived; no
oracle at run time. **It is the first change in this arc to move the FREE-RUN curve**
(net +30.8 -> +24.7).

The reload is SCOPED to `sprite_build_depth_banded` (`CPORT_RELOAD`) because the
blanket version OVERFLOWED THE CART -- `.portcode`'s budget is fixed at ~464 KB by
`.portrom` and `.portdata` below it. Widening it a few functions at a time, by
measurement, is the next lever.

**NEXT, and it corrects §29:** `+6/+7` of each record needs `uVar10 = 0x6F2C`, which
is **A1 at the zoom call** -- so `extraout_A1` really is A1, a PASS-THROUGH INPUT,
and the oracle said so by showing A1 unchanged across the call. §29 fed it the
return's low half instead. The faithful rule is simpler: **`extraout_X` is the
register file's X after the call.** A1's own source is not a field of the object
record (all 0x80 bytes of four slots checked), so it is computed further up.

**EARLIER (FULL-PORT.md §26/§27): THE DISPATCHER PASSED NO REGISTERS.**
`sprite_build_depth_banded` gates hide-vs-draw on `p_D6`, so objects were never
hidden -- bit 3 of the flag byte set and the sprite record filled, every frame. The
table is now 398 generated THUNKS, each passing its handler's registers in its own
declared order (65 distinct signatures in the unit, so positional passing was
impossible). It also found that handlers had been receiving **undefined** values
from r4-r7, not zeros: defined zeros alone took per-frame from -2.2 to -13.7.

Supplying the ARCADE's registers on top (`PORTREGS=1`) is only -2 further and makes
3 object cells worse -- D6 is a running sprite-record cursor, so one vector per slot
is the wrong D6 for any given frame, and `l1_vectors.jsonl` has no frame number to
select by. Testing §26 properly needs an `l1_oracle` capture at the seeded frame.

**THE OVERNIGHT LOOP AND THE BUILD GUARD (2026-10-01, Mike's instruction).**

    python3 tools/port_guard.py        # 3 checks, fails on a NEW violation
    python3 tools/port_loop.py         # fixed candidate list, flag-only, auto-revert

`port_guard.py` exists because a written rule depends on being remembered at the
moment code is written and a failing build does not -- the array-base width rule was
in LESSONS and implemented in `gen_selftest.py` and was re-derived from scratch
three days later anyway. It checks result-slot bounds, A6 access width against the
arcade's OBSERVED width, and 68K addresses dereferenced without translation. **First
run found 8 real width violations**, including `A6+0x2C` read as 2 bytes where the
arcade only ever reads 1 -- the `DAT_fffff142` class, still live. Baselined in
`docs/audit/port_guard_baseline.txt`; **shrinking that file is the measure of
progress on the class.**

`port_loop.py`'s rules, each from a measured failure: the candidate list is FIXED
before it runs, every candidate is a BUILD FLAG only (no source edits, no Ghidra, no
new instruments -- the three things that produced every regression), and REVERT is
the default path. Its first run returned a clean monotonic negative on the
frame-body list in seven builds with nothing to unpick (FULL-PORT.md §33).

**ROOT CAUSE OF THE RECURRING ERROR CLASS, and it is countable (§33 / Mike's
diagnosis):** the port has NO DECLARED DATA MODEL. `obj_record.h` exists, is
generated, is included in every build and is used **zero** times; the port has
**26,679 raw A6 offsets and 0 named accessors**; the struct names 12 of 128 bytes;
**1,572 sites touch A6+0x2C alone**; the sprite record at 0xFFF800 has no
declaration; and no type separates a 68K address from an SH-2 pointer. So every site
answers "what is here, how wide, which address space, what byte order" by inference
at the point of use. Every width/stride/domain/endian/structure failure is one
instance, and the tool churn is the same failure's second face.

**THE APPROVED PLAN IS `docs/design/PORT-ARCH.md`** (Mike, 2026-10-02: "1 the 2 and
then 3. good plan."). Read it before proposing anything in this lane; FULL-PORT.md
§26-§33 is the execution log that led to it, not the plan.

    1. ROOT 0x005BE AND CALL IT once per frame -- the whole frame loop is that one
       function (Ghidra's `test_mode_screen`, the main routine mis-named), it has
       BRANCHES, and no flat list of addresses can be right. Delete the hand-rolled
       body. DO THIS FIRST.
    2. DECLARE THE DATA MODEL -- full OBJ struct (128 bytes, widths from the
       oracle's OBSERVED accesses), a sprite-record struct for 0xFFF800, `a68` as a
       distinct type, ONE width decision instead of three. Success = gcc catches
       what measurement has been catching, and the guard baseline shrinks to 0.
    3. `a68` APPLIED ACROSS THE UNIT so every domain mix is a compile error.

**OPEN, in order:**
1. **PORT-ARCH step 1** -- root 0x005BE, call it once per frame.
2. **The free-run divergence** (expect most of it to BE the missing two thirds of
   the frame, so re-measure after step 1 before analysing it).
3. **The free-run divergence, continued.** MISSED 160/frame at the endpoint, 207 of the 290
   wrong cells in the object table. Per-slot attribution over sampled frames is
   built (`port_l2m_out[171..187]`) and is the next read.
2. **Per-frame MISSED, 30.1 cells/frame** — the whole remaining per-frame error.
3. Slot 16 is the only slot harmful at the seed frame (+4).

**NOT WINNABLE, do not chase:** `0xFFFF00+` differs by 30 bytes between two MAME runs
of the SAME playback, so the gate can never score that region to zero. It reads 0.

**Nine other width candidates are DEAD by static triage** (FULL-PORT §24a): none is
used in scaled or index arithmetic, so the `<< 2` mechanism cannot apply.

**THE GENERATOR IS NOW AUDITED, not just patched when a cell fails**
(`tools/cport_audit.py`, FULL-PORT §27). First run found 18 calls to a LITERAL 68K
address -- `(*(code *)0x4ebe)()`, which the indirect-call rewrite never matched
because there is no pointer to load -- across 5 targets, none in the dispatch table,
every one a wild jump into low SDRAM. **No failing cell was going to point at them:
those functions are not reached in the gate's frame.** Routed now.
It also ruled OUT, with counts: condition codes carried across a call (0 -- a 68K
sub CAN return its answer in the flags and Ghidra models them per function), BCD
arithmetic (0), and real division/modulo (2 and 9 in the whole unit).

**BUILD NOTE: the PORTFULL probe needs `NOTILESMD=1`.** `.tilesmd`'s load address is
fixed at 0x26A000 and `.ramtext`'s ROM image follows `.data`, so every byte of
`.text` moves it; the unit plus 398 thunks pushed it past and the link failed. The
`.ramtext`/`.tilesmd` ASSERT is now conditional on the section being non-empty -- it
was address-based and refused a build for colliding with a blob it had just
dropped.

**Corpus 846 functions, 603 in the port unit, 0 compile errors.**

## THE LINE

    sw121.32x (advisor-bisect)  THE r60 LINE, 2026-10-16 evening, on Mike's
                                pass on the OFFICE PVM: "brown grass seems
                                fixed, better transitions. lightning still
                                green."
                                sha1 4b4305503cf73760d9e14a464df7cce00ad64176
                                (builder computed it independently and it
                                matches the advisor's), build_id stamp
                                b7251e6a 2026-10-16 21:08:35.
                                COMMIT f4d64f89, PROVEN BY REBUILD
                                2026-10-16: a clean rebuild from that tree
                                with the line flags differs from sw121 in 10
                                BYTES, offsets 4194261-4194288, the build_id
                                stamp and nothing else. (sw114/cac836e8 was
                                13 bytes in the same range; the count varies
                                with the stamp text, the RANGE is what to
                                check.)
                                CONVENIENCE, WITH A CAVEAT: since 58af9167
                                PORT_FLAGS carries R60_LINE_FLAGS, so a plain
                                `make port` on advisor-bisect builds the
                                line's CONFIGURATION. It does NOT build the
                                line: HEAD's rom differs from sw121 by 16
                                CODE bytes -- a flag-off wrapper left by the
                                parked PENIDX -- plus the stamp. Identify the
                                line by its sha1, never by "what make port
                                just produced".
                                FLAGS: sw114's plus R60KEEPSTALE=1
                                R60TXTPOOL=1 (Makefile:1926-1936 -- a cell
                                whose new art did not ship this window keeps
                                last window's; the text colours live in the
                                sprite sets' unused pens 0 and 15).
                                GATES: soak 0.776 flips/vint, timeouts 1,
                                attract_parity cut +30 / cave +5 / eye +5,
                                high-score span 51/59.
                                WHAT MIKE STILL SEES: the green lightning,
                                which is the object byte 0x0A palette defect
                                (builder's), and the text palette block read
                                one word early (builder's). PENIDX is parked
                                at 1af842b3 with its negative results; the
                                logo roll is the advisor's.

    sw114.32x (advisor-bisect)  THE PREVIOUS LINE, 2026-10-16, on Mike's ares
                                pass: "MUCH cleaner! We just need to tighten
                                up transitions."
                                commit cac836e8, sha1
                                9226442faa582bf613a9978fd1defd2b26ee9e83,
                                built 2026-10-16 14:36.
                                BOTH IDENTITY DOUBTS CLOSED BY A REBUILD,
                                not by memory (builder raised, advisor
                                settled): a clean `make port` at cac836e8
                                with the line flags differs from sw114 in
                                EXACTLY 13 BYTES, offsets 4194261-4194288,
                                which is the build_id stamp at the rom's
                                tail. Every code byte is identical, so
                                cac836e8 reproduces the artifact -- the tree
                                held the FAT work at 14:36 and the commit
                                came after the cap sweep. And sw114 was
                                built BEFORE the R60FATCAP Makefile flag
                                existed, so no flag was passed and the cap
                                is the HEADER DEFAULT 160; the worktree's
                                R60_FATCAP=120u belongs to fc120, the last
                                sweep probe, and sw114b's .build_flags has
                                no FATCAP entry at all.
                                THE METHOD IS THE REUSABLE PART: a rom can
                                NEVER match a rebuild by sha1, because the
                                build_id stamp embeds the build time. So
                                "does this commit produce this rom" is
                                answered by a byte diff with the stamp range
                                excluded, never by comparing hashes. 13
                                bytes at the tail is the expected answer;
                                anything else is a real difference.
                                WHAT IT IS: sw112 plus the FAT packet A --
                                under the game's own blank (DISPHOLD 1) both
                                planes' art rides one 3072-word packet A at
                                FB 0x17000 / 68K 0x857000, with the slave
                                leaving FM down R60FATSPAN=4 vints for the
                                consume.
                                PACKET-A MOVE VERIFIED BY THE BUILDER (the
                                check that caught sw102): at cac836e8 the
                                SH-2 publishes at FB_WBASE + 0x17000
                                (r60_main.c:736) and the LIVE 68K consumes
                                0x857000 (r60_shim.h:77) as well as
                                md_main.c. No stale 0x85E000 or 0x1E000 in
                                any live path; the two remaining hits are
                                packet_fmt.h's unrelated FBX_PKT_MD and an
                                m_main.c comment.
                                NUMBERS: 30k soak 0.785 flips/vint (sw112
                                0.789), span timeouts 0, gate-held vints
                                1018 against 1536; attract_parity --port cut
                                collapse +30 (was +90), cave +5, eye +8.
                                Demo cut: blank at our 441, sw112 shows the
                                scene at 520, sw114 at 495, |luma| 30 at 500.
                                OPEN AXIS: transitions. The cuts' remaining
                                lag is the port's 44-tick load (see the
                                load-pacing entry); the advisor has the
                                non-hold transitions.

    sw108.32x (advisor-bisect)  THE PREVIOUS LINE, 2026-10-16, commit 9516068a,
                                sha1 0eee4d397e5037bf263d69384d35dbbc8181d4a0,
                                build id 9516068a. PROMOTED ON MIKE'S ARES
                                PASS against sw81: "cleaner presentation, less
                                tile flicker".
                                It is sw81 plus step A (the canvas offset:
                                paint once, scroll by line table + shift bit,
                                with the bulletin-14 guard) and the completed
                                packet-A move to 0x1E000 / 68K 0x85E000.
                                R60FILL32 (step B) is OFF on the line and
                                stays parked.
                                NUMBERS: soak 0.79 at 30k (sw81 0.80), parity
                                = sw105/sw80 within noise, 68K A-consumed
                                census 1022 at f1268.
                                **THE INSTRUMENTS DID NOT PREDICT THIS AND
                                THAT IS THE ENTRY'S POINT.** Parity and census
                                read sw108 EQUAL to sw81; the soak read it ONE
                                POINT DOWN; I recorded it in this file as "not
                                a line candidate on its own merits" and the
                                eye overruled that. The gain Mike names --
                                less tile flicker -- was never measured as a
                                picture change by anything we own. The
                                plausible mechanism is the clear phase (stale
                                glyphs cleared BEFORE the sprites instead of
                                after) or the pins, neither instrumented.
                                See LESSONS on the temporal instrument gap.

    sw81.32x (advisor-bisect)   THE PREVIOUS LINE, 2026-10-15 late, commit
                                68966d0d, sha1 0bff51572cbbe79df092ae357fc00
                                7b4ba0ec770, same eleven flags (sw79/sw80 are
                                the same design a step earlier and the numbers
                                below name their files). THE DISPLAY GATE.
                                FLAGS -- spell them out, never one shell word
                                (PORT_EXTRA as a single word silently builds
                                the BASE rom):
                                  make port PORT_EXTRA="R60NEW=1 GOVERNOR=1
                                  R60SLAVEWIN=1 R60PLOT3=1 R60CRAMDELTA=1
                                  R60NOBARS=1 R60PLANES=1 PALFIRST=1
                                  R60TEXT=1 R60RECTCLR=1 R60ZOOM=1"
                                WHAT IT DRAWS: the arcade picture on ares --
                                title, graveyard, HUD, logo text, INSERT COIN,
                                high-score table -- the MD background planes,
                                the text layer through 32X CRAM sets 12-14,
                                sprite zoom, and clean cuts. On the office
                                MiSTer: sprites at full density in correct
                                colours and the temple behind the hero. Mike
                                on the sprites: "accurate and very fast".
                                WHY: Mike's sw74 capture (screenshots/, 3,632
                                frames) showed LITTER AT EVERY CUT.
                                WHAT: the arcade's video-enable (port
                                0xC40001 bit 5; MAME misc_io_w,
                                jts16_main.v:247) read from the port's byte at
                                0x0602C801. The master holds while it is off
                                and after it rises until the screen is whole
                                -- cells blanked for want of art under 80 over
                                five windows, three windows running, at least
                                eight after the rise, cap 120. Packet B bit 13
                                drops the MD display in the 68K's R60 consume
                                (reg 1 0x8134/0x8174, ADDED: the k1/k2 block
                                that used to do it is not compiled in the
                                port). A shared word blanks the slave one
                                window behind the MD so sprites never lead the
                                planes.
                                MEASURED (ares, arcade ref +2, sw80_cut_N.png
                                / sw79_hd_N.bin): the demo cut -- arcade black
                                448-460 and whole at 466; sw74 littered from
                                448 through 520+; sw80 black 448-498, whole at
                                500, black fraction 1.00 -> 0.02, NO LITTER.
                                attract_parity (ap80), read correctly -- the
                                number is mean |luma| against the arcade,
                                lower is better, and the lag column where it
                                COLLAPSES is how many frames we trail that
                                scene:
                                  'cut black'   0/0/0 at lags 3-8. We are
                                                IDENTICAL to the arcade while
                                                it is black. sw74 read 20-41
                                                there -- litter on a black
                                                screen.
                                  'demo scene'  collapses at lag 45, floor 30.
                                  'demo 2'      collapses at lag 30.
                                  'boot card'   collapses at lag 90, floor 13.
                                sw74 NEVER collapsed on those scenes, because
                                it stayed littered through lag 90. So trailing
                                by 30-45 frames is the WIN here, not a
                                regression: a clear gain at every cut, and our
                                lag at a cut is now 30-45 frames instead of
                                never matching at all.
                                **THE 30-45 FIGURE IS NOW AT RISK, 2026-10-16,
                                AND THE LOWER END OF IT MORE THAN THE UPPER.**
                                Our game timer RUNS AHEAD of the arcade's: the
                                port ticks every vint, the arcade DROPS ticks
                                under load, so at timer 0x12D we are at our
                                frame 2955 and the arcade at 3002 -- 47 FRAMES
                                of accumulated drift, ~1.6%. attract_parity
                                aligns the whole run on ONE frame offset (-2),
                                so that drift grows through the attract and
                                the LATE rows measure it. 'demo 2' collapsing
                                at lag 30 is in the contaminated region, and
                                the drift is the SAME SIGN AND ORDER as the
                                lag being reported, which is the worst case:
                                it cannot be subtracted by eye.
                                RESOLVED SAME DAY, AND THE CLAIM NARROWS
                                RATHER THAN DIES: the 30-45 stands for THE
                                FIRST CUT ONLY -- the 'cut black' and 'demo
                                scene' rows, which sit before the drift
                                matters. Every later row needs the timer
                                alignment and must not be quoted without it.

                                RE-READ 2026-10-17 AND THE FLOOR HELD. I
                                flagged it as possibly the sw147 text-clear
                                bug rather than the coin state. It is not.
                                attract_parity --port, demo scene 465, at
                                +30/+45/+60/+90:
                                  sw121  34 / 29 / 29 / 30
                                  sw147  51 / 29 / 29 / 26
                                  sw149  45 / 29 / 29 / 31
                                The floor did not move with the fix, because
                                the clear bug only bit when the LONGER string
                                gave way to the shorter one and in this
                                corpus that happens on the ARCADE side, not
                                ours. The attribution below stands as
                                written. QUOTE IT AS 29-31, not 30 -- and
                                note the +30 column (34/51/45) is not the
                                collapse point and should not be read as one.

                                AND THE FLOOR IS OUR OWN OPEN DEFECTS: the 30
                                that 'demo scene' cannot go below is the logo
                                COLOUR PHASE and the CREDITS TEXT -- which are
                                the pal 6 colour 6 missed write and the '2
                                CREDITS 1 PLAY' coin state, both recorded
                                under OPEN DEFECTS above. Fixing those two
                                should lower this floor, which makes them
                                worth more than their size suggests.
                                CADENCE IS BETTER, NOT WORSE. 30k soaks,
                                sw79_soak_*.bin and sw80_soak_6000..30000.bin
                                (rom build/port/rom/sw80.32x), agreeing:
                                  0.81 / 0.79 / 0.80 / 0.80 / 0.80 flips/vint
                                against sw74's 0.77 and the pre-zoom sw58's
                                0.80. 54 gate releases and 1,548 windows held
                                in 30k frames. Holding the display costs
                                nothing and gives back what zoom took -- a
                                held window does less work, which is the
                                opposite of the usual correctness-costs-speed
                                shape.
                                WHAT REMAINS, MEASURED: we show the scene
                                34-45 FRAMES AFTER THE ARCADE. Cause found,
                                and it is the 68K: its consume of a full
                                packet (14 full rows + 61 inline art records)
                                runs line 226 to line 53-59 of the NEXT frame
                                (0xFFB0B0 / 0xFFA170 at f455-475), about 90
                                lines, so the FM=0 span crosses the vint and
                                the master builds a window every OTHER vint
                                (0.5/vint during the hold). It costs ~1 68K
                                line per inline art record and ~1.5 per full
                                row, display off or not -- WHICH IS EXACTLY
                                THE OLD PIPELINE'S MD_BATCH 12-24 RULE, section
                                4 of the inventory. See OPEN CARDS.

    sw74.32x                    THE PREVIOUS LINE (commit 4c8f45b3): the
                                zoomed plot rewritten to a 64-bit drop mask,
                                identity-gated twice before any cadence
                                number, 0.77 flips/vint at 30k. Superseded by
                                sw81, which is faster AND fixes the cuts.
                                Its lineage, for a bisect:
                                  sw58        the line before zoom     0.80
                                  sw60/sw61   + R60ZOOM (0a55d738)     0.74
                                  sw65        + art budget and pins
                                  sw68/69/70  + the pen pack (c12f0151) and
                                              the slave copy at 376 longs
                                              (9cf65304 -- the palette block
                                              runs to word 751, so MD CRAM
                                              line 3 had never landed)
                                  sw71        re-mark on plan adoption:
                                              PARKED, see OPEN CARDS
                                  sw74        the zoomed plot (4c8f45b3) 0.77
                                  sw79/80/81  the display gate (68966d0d) 0.80

    rom/night/ring1.32x         THE LINE, NAMED 2026-09-25 22:50 ON MIKE'S PLAY
                                PASS: "Attract looks great!" = nt1 (the
                                previous line, below) + THE RING BAKE, and
                                nothing else: round 9's tables plus the
                                ring-pen certificate gate, both INERT outside
                                the transformation cut (rounds 0-8 are
                                byte-identical with zero ring flags). Stamp
                                765de95a 22:32:05 R60, normal. Region guard
                                0x06017DF8, OK.
                                THE FIELD: control = 1 blue shade over all
                                78,176 px, strobing. ring1 = 3 coexisting
                                shades whose counts redistribute every frame.
                                Three is the quantiser's ceiling with
                                PAL_QUANT_RAMP off, corroborated by two
                                hardware captures of the title screen -- not
                                a shortfall.
                                REACH IT VIA ATTRACT ONLY: the bots never
                                transform, and replaying arcade inputs into
                                our rom diverges so the round never advances.
                                Any future pass on this cut must let attract
                                run.
                                ON THE RIG since 22:32. nt1 was never pushed;
                                ring1 supersedes it. NOT YET RUN on ring1: a
                                3-launch picture gate of the kind nt1 passed.
                                Its speed is nt1's by construction (no flag
                                or 68K change) but has not been re-measured.
                                OPEN DEFECT, now specified by Mike
                                2026-09-25 23:20: "the errant BLACK tiles
                                appear consistently through level 1
                                gameplay... there hasn't been a where or when,
                                they just happen through level 1." So it is
                                NOT O-6 (text glyphs) -- it is DEFECT 1's
                                family, black tiles in level 1.
                                WHY THE PASSING GATE DOES NOT COVER IT, and
                                this is the whole point: defect 1 was
                                reproduced and FIXED against the BOTTOM-BAND
                                black-cell census (view rows 24-27) in the
                                ATTRACT DEMO (LESSONS 10:50 / 11:45 / 12:20,
                                wc6/wc9). Mike sees it WHOLE-SCREEN in
                                GAMEPLAY. The instrument that reads 0 is
                                measuring a narrower window than where he is
                                looking, so "0 black band cells" and his
                                report are not in conflict and neither is
                                wrong.
                                MECHANISM ALREADY ON RECORD and it predicts
                                this: a priority (cat-1) FG cell whose hash
                                set is FULL OF YOUNG TILES is refused a VRAM
                                slot and left blank, and the backstop blanks
                                the floor under it. Gameplay churns more
                                distinct tiles than a scripted demo, so the
                                sets fill faster than any attract run can
                                show. The cset+plane hash relieved the
                                pressure; it need not have eliminated it under
                                a human's tile load.
                                DO NOT use black_pct -- a whole-frame black
                                fraction, swamped by legitimate black art, and
                                LESSONS 185 already records it as not the
                                instrument for a black-tile card.
                                WHAT IS MISSING: a whole-screen black-cell
                                census over a GAMEPLAY window, plus the slot
                                refusal counter read at the hit frame. The
                                attract corpus cannot answer this one.

    rom/night/nt1.32x           PREVIOUS LINE, 2026-09-25 21:50 ON MIKE'S WORD:
                                "Right lets name that as the line... nt1 that
                                is." = wc9 MINUS THREE FLAGS AND NOTHING ELSE
                                (TAGKEEP, PENHOLD, PENREPAINT; ancestry
                                verified, not assumed -- nt1's source c852403d
                                has defect 1's cset+plane hash fix 5c8c3d5a as
                                an ancestor). Stamp c852403d 19:12:30 R60,
                                normal. Region guard OK.
                                PLAY PASS, Mike: "looks great no regressions.
                                still things to fix, like errant tiles. but no
                                regressions", and separately "my first
                                impression is that it runs smoother... this
                                feels better".
                                PICTURE GATE 3/3, 60 shots: 0 purple band, 0
                                grey level start, 0 black band cells.
                                SPEED (RIGSPAN=4, light barcode RIGBCEVERY=8,
                                control interleaved in the same session, two
                                launches each): passes spanning 2+ vints
                                9,11 vs wc9's 14,13 at ~vint 3290 -- ranges NOT
                                overlapping, ~26% fewer; no separation at the
                                earlier ~vint 2850 sample (4,7 vs 6,6); the
                                same-vint column is FLAT, so the passes that
                                stopped spanning two vints became one-vint.
                                SUPPORTS Mike's impression. NOT A RANKING and
                                not the basis for the naming -- the naming is
                                on gates passed plus his word above.
                                TAGKEEP is FALSIFIED for its own purpose
                                ([22]=0 [23]=26, 100% moved) and is NOT the
                                cut's pen-map cause (byte-identical table).
RIG: ring1.32x, relaunched 2026-09-26
                                04:19 after the night's probes. GATE CLOSED
                                2026-09-26 23:00-23:10: ring1's OWN 3-launch
                                picture gate, 3/3 -- 39 demo frames, 0 black
                                cells, 0 purple in 60 shots; the one outlier
                                is the attract eye screen mid-draw-in.
                                SPEED vs nt1: NULL RESULT, not equality.
                                RIGSPAN=4, four interleaved launches; per-step
                                span>=2 was ring1 {50,46,54,44,40,20} against
                                nt1 {26,46,62,44,16,28}, fully overlapping
                                with nt1 holding both extremes. STATE's old
                                "nt1's speed by construction" is retired: it
                                is now a measurement that resolves nothing,
                                and ring1's _end sits 8 bytes above nt1's,
                                which the resolution floor says is enough to
                                move a ladder on its own.
                                OPEN ON IT: the errant tiles Mike still sees
                                are UNIDENTIFIED. Do not assume O-6; locate
                                them in the corpus and name the layer and set
                                first.

    rom/night/rg2.32x           PREVIOUS LINE (2026-09-25 morning,
                                Mike's ares play pass on rom/s16.32x, which
                                was rg2 at the time: "This is an excellent
                                build"). = rg1 + the BGBOTTOM opacity-cache
                                fix (art-keyed; the purple band). No 68K
                                change. Rig: the fix's census ran on wc4
                                (0/72), rg2 itself has 0 launches -- run 3
                                before the next hand-over.
    rom/night/wc4.32x           CANDIDATE = `make line WRAMCODE=1` = rg2 +
                                WRAMCODE (two block sets, call-site
                                rewrite, 68K hot code in WRAM 0xFF5000).
                                Exact on ares at the same game time; FPGA
                                level-1 demo mean pass 316-330 lines (rg1
                                358-418), one-vint passes ~118 per 900
                                vints (rg1 ~34 per 440); boot-window
                                census 0 purple in 72 launches. In
                                rom/s16.32x for Mike's play pass (09:55).
    rom/night/wc9.32x           CANDIDATE = `make line WRAMCODE=1` (2026-09-25
                                12:50) = wc4 + defect (1)'s fix: the MD slot
                                table's set hash folds colour set and plane
                                (a tile's FG and BG variants no longer share
                                an 8-way set; LESSONS 11:45 "defect 1's
                                mechanism", 12:20 "the hash alone"), plus
                                the bottom-band cover test resolving the FG
                                row exactly, bm_reset and cap_page in ROM.
                                Ares: demo band census 0 black cells (wc5:
                                1-5 a frame), same-game-time picture
                                identical, speed = wc5's. RIG: pictures 3/3
                                (60 shots, 0 band cells, 0 purple, 0 grey);
                                RIGSPAN=11 356/324 and 312 vs control 322/314
                                (the day's launch spread is ~15 lines);
                                RIGSPAN=4 one-vint 120/126 vs 104, three-vint
                                30/20 vs 24. In rom/s16.32x for Mike's play
                                pass. wc6 (64x16), wc7/wc8 (two-choice,
                                MDTWOCHOICE=1 knob) read ~20 lines slower on
                                the FPGA and are superseded.
                                MIKE'S PLAY PASS ON wc9 (rig, 13:52-13:55,
                                screenshots/20260925_135*-s16.png), seven
                                items: (1) Zeus text leaves one glyph (a red
                                C at row 3); (2) the beast-transformation
                                screen arrives full of garbage tiles (the
                                switch-in-view class) and (3) keeps the
                                level's cells in rows 0-2 and 25-27,
                                recoloured green by the cut's palette (the
                                arcade shows black there); (4) black tiles
                                still appear at random in level-1 PLAY --
                                bottom-row cells and a thin black bar -- the
                                demo census did not cover play; (5) the
                                round-clear bonus text never fully renders
                                (glyphs missing: the lost-text-store class,
                                like the Zeus message); (6) the level 1 -> 2
                                crystal-ball scene draws the ball's interior
                                in one magenta palette line (wrong palette);
                                (7) level-2 enemies as black silhouettes
                                (the lost-push / palette-late class).
                                ORACLE BUILT AND RUN on Mike's full-game
                                recording (2026-09-25 16:10: 103,171 game
                                frames, 1,764 arcade snapshots, all five
                                rounds, every transformation bracketed;
                                oracle/ + oracle/scenes.csv). FIRST FINDINGS:
                                (2) the transformation's blue field is FOUR
                                shades on the arcade (5-bit blue 25/23/21/19)
                                and ONE in ours -- our 32X CRAM holds 25 and
                                21, not 23 or 19; 477 of 1120 cells differ,
                                all in rows 4-20; the tile art IS arriving
                                and the MD planes carry no blue, so it is the
                                CUT SCENE'S PEN BAKE, not throughput (LESSONS
                                17:10, CORRECTED 18:20). ITEM 2's CHAIN,
                                measured: the cut's colour-cycling sets
                                19/20/21 do not reach PAL_SH with live values
                                (it holds a cyan ramp the scene never shows);
                                because those sets carry an MD line, the
                                framebuffer follows the MD line's 3-BIT
                                colour, and seven of set 19's eight pens land
                                on ONE blue -- the flat field. The lever is
                                the palette delivery for those sets. The
                                obvious bypass (arcade colour while the cut
                                pages are up) was BUILT AND REVERTED (the red
                                field went pale sage, the face vanished):
                                MEASURED WORSE, CAUSE UNKNOWN, and NOT closed
                                -- it is the only path to an exact field (3-bit
                                space holds three of the four blues at best),
                                and the run that diagnosed it carried the
                                stride error, so it deserves one clean re-run.
                                SETTLED 18:50 with the decompile's §10: the
                                cycler (0x30B2, from IRQ4, 8 colours a slot,
                                already dirty-masked by patch_game.py:1021)
                                DELIVERS -- our 68K palette holds the ramp
                                (5-bit blue 19/21/23/25 at word 153) and
                                PAL_SH matches it in 2036+/2048 words. My
                                "cyan" was the wrong STRIDE (the SH-2 uses
                                8-word groups, the game 16-word sets). The
                                bug is the painter: group 19's eight FB pens
                                are white + SEVEN COPIES of one blue, where
                                3-bit space still affords three shades
                                (25/23/21/19 -> 6/5/5/4). CHAIN COMPLETE
                                19:30 (LESSONS "item 2's full chain"):
                                mdp_s_map group 19 = [14,7,7,7,7,7,7,7] --
                                pens 1-7 all on ONE MD line pen -- while
                                group 20's eight pens spread over six in the
                                same frame, so the allocator is not short of
                                pens. The merge is made at assignment (the
                                cut's fade-in, when the set's colours really
                                are equal) and the drift path TOLERATES
                                shared-pen drift, so the rotation never
                                re-splits it. FIX LOCATED, NOT BUILT: re-split
                                a set whose quantised colours span more levels
                                than the pens it holds (m_main.c ~18135, where
                                mdp_free_set already exists for the volatile
                                case); conditional on the SPAN, not on any
                                drift, because unconditional re-assign
                                invalidated a set's slots ~1/sec. Worth
                                1 blue -> 3 on the line, 1 -> 4 with
                                PALQUANT=1 (decompile's §11: the live
                                (+2)>>2 gives {5,6,7} over the measured
                                ramp, the unused PAL_QUANT_RAMP table gives
                                {4,5,6,7} = the arcade's own shade count).
                                PALQUANT MUST NOT ride in the same build: it
                                differs on 16 of 32 inputs, a global colour
                                change, and neither change would be
                                attributable. THE PRIZE IS MOTION, not
                                shades: the field is static because seven
                                pens share one pen and nothing updates it;
                                the shimmer returns at three levels as
                                readily as at four. TWO FIXES TRIED AND
                                REVERTED (LESSONS 20:10): a span re-split
                                churned (the claim re-merges the freed set
                                every window), and CHEVFIX=1 -- the flag
                                written for exactly this -- did not reach
                                the assignment on this line (groups 19/20
                                both read unassigned at the cut). ALL THREE READS DONE 20:40
                                (LESSONS): my CHEVFIX result was an
                                ALIGNMENT artefact -- aligned to its OWN cut
                                (frames 9371-9489) the gate IS up, the sets
                                ARE assigned, and group 19 is STILL merged.
                                The answer is CAPACITY: sets 19/20/21 all sit
                                on MD LINE 0, which has ONE free pen, so
                                branch (a)'s "prefer exclusive" cannot fire
                                and claim-time sharing merges them while the
                                ring is still uniform. MDP_LINES=3 (48 pens);
                                three 8-pen cyclers need 24 on one line.
                                PEN OWNERSHIP READ (LESSONS 21:30):
                                20 and 21 ALREADY share losslessly (rc 2 on
                                every pen), so the decompile's merge is in
                                effect; the waste is set 20 holding SIX pens
                                at one identical colour while 19's seven
                                pixels share one. Both are frozen FIRST
                                CLAIMS, preserved by TAGKEEP's "land where
                                you were". FOUR FIXES TRIED, none moved
                                mdp_s_map: a drift-site span free (the free
                                is REFUSED for these sets by CHEV_FIX (b) and
                                mds_pin, so it wrecked others), CHEVFIX alone,
                                a span rejection in the landing path (never
                                fires -- the ring is still uniform there),
                                and both together. TAGKEEP FALSIFIED AS THE CAUSE
                                (LESSONS 22:10): today's line minus the
                                family gives a BYTE-IDENTICAL pen table, and
                                mds_pin is 0 for every set, so neither the
                                restore nor the scene pin holds it. Five
                                configurations, one table. The sets are
                                assigned ONCE while the ring is uniform and
                                nothing re-assigns. SOLVED 22:10 (LESSONS "item 2
                                SOLVED"): the pen map is BAKED INTO THE ROM
                                -- baked round 9 set 19 = line 1, used 0xFF,
                                map [14,7,7,7,7,7,7,7], byte-for-byte the
                                live table -- computed by tools/mdpen_bake.py
                                from ONE anchor image whose set 19 was
                                captured BEFORE the cycler's first write
                                (anchor blues [16,15,15,15,15,15,15,15] ->
                                one pen; the live ring needs three). That is
                                why five configurations and four runtime
                                fixes changed nothing: the runtime never
                                chooses this map. FIXED AND VERIFIED 22:35
                                (commit 765de95a, LESSONS "RING BAKE
                                APPLIED"). The fix site was NOT
                                mdpen_bake.py -- that writes the per-SCENE
                                header, which m_main.c:1907 overrides with
                                the per-ROUND tables under MDROUND=1. The
                                live baker is tools/bake_tilecram.py, and it
                                was ALREADY keying this scene per pixel
                                (PIXKEY_SCENES {5,6,9}); its key was
                                tuple(sorted(colours)), the SET a pixel was
                                seen holding, and under a ROTATION every
                                pixel visits that set in a different phase,
                                so all seven collapsed to one pen. The key is
                                now the pixel's ORDERED quantised sequence,
                                read from the rom's own cycler script
                                (tools/ring_script.py demand(), new --ring,
                                REQUIRED). Two pixels share a pen iff they
                                agree at EVERY step, so the key IS the
                                correctness rule: phases never merge and the
                                two slots on one ring share for free. Set 19
                                now takes EIGHT pens, 20/21 share seven,
                                round 9's line 0 fills to exactly 15. Shared
                                ring pens would have FROZEN (install
                                refcounts per owning pixel; the in-place
                                recolour fires only at rc 1 -- the CHEVPEN
                                freeze, which is why 20/21 read 0x0007, step
                                0 of their ring), so the bake emits
                                mdr_pen_ring certifying co-owners agree at
                                every step and the owner drives the recolour
                                whatever the refcount. Rounds 0-8:
                                BYTE-IDENTICAL tables, zero ring flags, so
                                gameplay is untouched. MEASURED on ares, each
                                rom at ITS OWN cut (the bots never transform;
                                the cut is the ATTRACT demo's, ours 1459-1571
                                vs control 1456-1568): control = all 78,176
                                field px ONE blue per frame, the whole field
                                strobing; new = THREE blues at once, counts
                                redistributing every frame. THE PRIZE WAS
                                MOTION and motion is what it bought. Three is
                                the quantiser's ceiling with PAL_QUANT_RAMP
                                off; PALQUANT still MUST NOT ride in the same
                                build. ON THE RIG as ring1.32x 22:32.
                                AWAITING MIKE'S PLAY PASS. (3) does NOT reproduce on ares:
                                the cut's top and bottom rows are 160/160
                                black on both machines through the window, so
                                it is rig-only like the grey-tile level start.
                                (4) black tiles: 0 cells in our play frames on
                                ares at game frames 2000/2600/9100. Our
                                transformation runs ~4000 game frames later
                                than the arcade's (our player takes the orbs
                                at a different time); the scene timer tracks
                                the arcade to within ONE frame throughout.
                                ARES CAN REACH 2-7 with that recording: the
                                ARCADE PLAYTHROUGH ORACLE
                                (docs/design/ORACLE-PLAYTHROUGH.md, LESSONS
                                14:50) -- record a route in MAME, dump the
                                arcade's state and picture per GAME frame,
                                replay it on our rom keyed to the shim's own
                                game-frame counter (0xFFA1D0,
                                ares --input-gameframe). Verified to reach the
                                same point of level 1 with the same enemies
                                and score. WAITS ON: one ~3-minute recorded
                                playthrough (coin, level 1, a transformation,
                                the boss, the round clear, the ball, level 2).
                                The attract's face/eye cut has clean
                                top/bottom rows against the arcade. On (4):
                                wc9 under the play script on ares shows 0
                                black cells in 150 play frames, so the
                                play-time black tiles are rig-only (the
                                residency/throughput class the record
                                already names); the instrument is the
                                TILE_VERIFY blank-reason bits on a barcode
                                rom played by Mike. HYPOTHESIS for (2)+(3):
                                the SH-2's cut detection is COMM14 bit 7 =
                                (0xFFF148 != 0), the solo-object filter
                                (NOTES-FROM-DECOMPILE 17), plus page word
                                0xAAAA; if the in-play transformation runs
                                without the solo filter, the cut pages get
                                the level's pen tables (green rows) and no
                                prefetch (the garbage start). TEST: Mike
                                plays the barcode rom rom/night/wc9s4.32x
                                and screenshots the transformation; byte 8
                                bit 3 of the barcode says whether the cut
                                bit was up. The decompile's gate is the page
                                words alone.
                                Mike's four persistent defects, seen on
                                rg2, 2026-09-25 morning:
                                (1) black tiles pop in randomly through
                                level 1; (2) beast-transition screen:
                                garbage on its top and bottom rows; (3)
                                some frame loss, "the right kind" (vint
                                misses); (4) level 1 -> 2 transition:
                                garbage on the purple screen.
    rom/night/rg1.32x           previous line = `make line` without
                                WRAMCODE (2026-09-24, rig 3/3)
                                = spd2 + ROWGEN=1 (write-tracked row skip:
                                rows no sprite span of the last two
                                snapshots touched, no text/scroll/page/
                                palette delta, skip their clear and their
                                blit; per-bank accumulated dirty masks).
                                Exact on ares under the play script
                                (ROWGENVERIFY 0 / 184,977 skipped rows
                                through the fight); palette gate PASS;
                                play-script rate 50.4% (spd2 50.8%); rig:
                                level-1 demo, level-2 demo, scores clean
                                on 3 launches. On the FPGA it takes the
                                master's blit 68 -> ~53 lines; the game
                                rate has not moved yet (window 110-150
                                post->release vs ~85 needed). Mike's
                                call 2026-09-24 ("rowgen").
    rom/night/spd2.32x          previous line (2026-09-23 00:50)
                                = spd1 + PUBNORB=1 (the publish's replay
                                copies built from SDRAM, not read back
                                from the framebuffer). Compose-frame FM
                                drop 108-112, gate spins 6-14 (line at
                                the day's start: 124-135 / 22-26). ares
                                palgate PASS, 18/19, rig 3/3.
    rom/night/spd1.32x          previous line (2026-09-23 00:20)
                                = cut1 + CRAMISR=1 (the 32X CRAM painted
                                at the flip in vblank, off the FM window)
                                + BLITAUDITDIV=4 (the blit audits one
                                skipping row in four per window). Speed
                                work only: the game-visible FM window on
                                compose frames 124-135 -> 112-120 lines,
                                gate spins 22-26 -> 10-18; game speed
                                still 50.0% (threshold, LESSONS 00:05).
                                ares palgate PASS, 18/19 anchors, rig 3/3.
                                RISK for Mike's eyes: the blit's mask
                                audit heals a lie in <= 8 frames instead
                                of 2 (the purple-band class).
    rom/night/cut1.32x          previous line (2026-09-22 21:20)
                                = eye9 + CUTPREFETCH=1 (the cut's art
                                prefetched on the cut bit; the scene
                                install keeps prefetched slots clean and
                                skips re-marking sets with no line). ares:
                                19/19 anchors clean, palgate PASS, the
                                cut's black period ~45 -> ~15 frames
                                (LESSONS "the cut's transition is the
                                scene install's re-mark"); rig 3/3.
    rom/night/eye9.32x          previous line (2026-09-22 19:50)
                                = eye8 + the glow keyed on the cut pages
                                (harmless; ares 16/19 same transients);
                                rig 3/3.
    rom/night/eye8.32x          previous line (2026-09-22 19:40) = eye6 + rounds
                                bake both tile variants (the level-1 boss
                                gate's purple: LESSONS "single-variant blob
                                drew purple"); ares palgate PASS, scenegate
                                16/19 (cut+10/+30 = the switch, pic+60 one
                                untagged cell in the blank), rig 3/3. Mike
                                to confirm at the boss gate.
    rom/night/eye6.32x          previous line (2026-09-22 19:15)
                                = eye4 + scene 5 harvested inside the mid
                                splash (sets 65/66 no longer overflow) +
                                the OFF state keyed on the cut pages (the
                                level stays intact until the switch).
                                ares 18/19 (cut+10 still re-marks 762 FG
                                cells at the cut bit -- eye7), palgate
                                PASS, rig 3/3. MIKE 2026-09-22 19:20:
                                "YES! You fixed the eyes animation!"
                                REGRESSION (Mike, 19:30): purple through
                                the FG at the level-1 boss gate = the
                                single-variant blob's round masks from the
                                three-scene audit (sets 81/83/84 BG-only
                                there, FG at the gate; colour 0 purple).
                                eye8 = rounds bake both variants.
    rom/night/eye4.32x          previous line (2026-09-22 18:55)
                                = eye2 + the attract cleanup (STATE "ATTRACT
                                CLEANUP 2026-09-22", LESSONS same day):
                                per-cell cat-1 mask slots (the pupil
                                notch), C1MASKTAB=1 on LINE_FLAGS, scene 9
                                keyed on the cut pages and packed alone
                                (the cut's blue), full-row shipping on fast
                                pans and harvested scenes, harvest layer
                                masks. ares: 17/19 anchors (cut+10/+30 are
                                the switch), palgate PASS, eye 223 -> 210,
                                cut f1580 299 -> 14; rig 3/3. Mike saw the
                                notch fixed on eye3. Next: eye5 = scene 5
                                re-harvested inside the mid splash (its
                                old frames were level 2's demo: sets 64-73
                                in the splash, 65/66 overflowed).
    rom/night/eye2.32x          previous line (2026-09-22 18:00)
                                = attract8b + the attract harvest reading
                                plane B at its real address (0xE000; it
                                read 0xD000, empty, so scenes 5-9 had no
                                BG sets: the eye's iris showed set 35 on
                                three CRAM entries -- Mike's "loose
                                tiles") and sets taken from the walker's
                                per-cell mirror (md_dbg_nt) with a layer
                                mask; the tile blob in single-variant
                                2 KB blocks (694 -> 414 KB, LESSONS
                                2026-09-22). ares: 19/19 anchors clean,
                                refusals 37,000 -> 0, palgate PASS; eye
                                vs the arcade phase-matched 239 -> 223
                                cells (yellow), ~105 (blue); rig 3/3.
                                MIKE'S PASS 2026-09-22: "eye2. easily."
                                PALQUANT=1 (ramp-nearest quantiser, one
                                MD level darker on 16/32 inputs; ares
                                eye 223 -> 38 cells vs MAME) was A/B'd
                                on the rig and REJECTED by eye: MAME's
                                tone is not the cabinet's, and the
                                "correct" ramp is ares's, unmeasured on
                                the FPGA. Knob kept, off; a rig ramp
                                probe would settle it. The blue-to-yellow switch is
                                the switch-in-view class (600+ cells for
                                ~40 frames, both builds).
    rom/night/attract8b.32x     previous line (2026-09-22 16:30)
                                = attract7 + the splash re-harvested across
                                its whole palette animation and packed by
                                per-pixel colour history (the logo's red
                                and white phases drew an outline over a
                                dark fill: a pen shared by pixels that
                                animate apart). ares: the logo's pens equal
                                the game's palette at the red frame; rig
                                3/3. The relief-to-logo switch still
                                shows ~20-30 black frames: the game's
                                page clear+refill shown in view (LESSONS
                                2026-09-22) -- the transport-floor
                                design card, not a bug in this build.
    rom/night/attract7.32x      previous line (2026-09-22 14:45) = attract6 +
                                attract step 5 mapped to the round (the
                                second level-1 demo; its black sky was
                                scene 7's table, read through the
                                barcode). ares: second demo's sky full at
                                +150. Rig 3/3 level demos; ONE capture
                                (launch 3, ~113 s) showed the SCORE TABLE
                                without its tiled backdrop -- the same
                                shape (a scene's table not landing on one
                                launch). Readout twin a7bc, 8 score-screen
                                captures over 4 launches: backdrop palette
                                landed (6 entries), 0 stale cells -- not
                                caught; OPEN, rarer than 1/8. The level-1
                                sky loss is CLOSED (it was the step map).
                                On the rig for Mike's pass.
    rom/night/attract6.32x      previous line (2026-09-21 18:15)
                                = attract5 with the 68K boot stack moved
                                to 0xFF4FF0 (.bss headroom). Rig 3/3.
                                OPEN: level 1's sky goes black on some
                                launches (attract5 launch 2/3; the
                                readout twin a5bc read 29 live entries on
                                every level capture across 4 launches --
                                not caught yet). AWAITING MIKE'S PLAY PASS.
    rom/night/attract5.32x      previous (2026-09-21 16:40)
                                = attract4 + ART_TAIL joined to the slim
                                route (tile records ride every cell
                                chunk's tail): the eye's hold 23 -> 9
                                vints, picture 26 -> 19, splash 34 -> 30,
                                no dropped records. Rig: tail4 (same
                                bytes but the stamp) 3/3; attract5 itself
                                2/3 -- launch 2 lost LEVEL 1'S SKY
                                (screenshots/rig_attract5, captures
                                2-4 and 12: black behind the ruins, the
                                sky present in capture 1) = Mike's
                                "revisiting level one shows black
                                background tiles", seen here on the
                                FIRST visit too. Launch-dependent, ares
                                clean: the hardware race class. Readout
                                twin (a5bc) queued x4 to catch it.
    rom/night/attract4.32x      previous line (2026-09-21 14:45)
                                = attract3 + md_pending at file scope (the
                                re-mark paths bump it: orphaned dirty
                                slots had left the transformation's red
                                field black) + scene 9 = harvest + the
                                ground sets. ares scene gate 19/19 clean
                                (the cut drains by +30, matches the
                                arcade from +60: blue zigzag, red field,
                                flames); rig 3/3. Residual: scene-start
                                holds of ~0.3-0.5 s (the load), and the
                                cut's first ~40 frames load in view with
                                a black sky (its sky sets do not fit the
                                table beside the zigzag's). AWAITING
                                MIKE'S PLAY PASS -- on the rig.
    rom/night/attract3.32x      previous line (2026-09-21 14:10)
                                = attract2 + THE ATTRACT BAKE: scenes 5-9
                                (splash, eye, picture, scores, the
                                transformation) harvested from ares and
                                baked into the tables and the tile blob;
                                tables keyed by SCENE from the state word;
                                slim budget 80/vint with a word-capped
                                emitter; hold settles from the game's
                                blank. ares scene gate 19/19 anchors
                                clean; rig 3/3. Residual: a baked scene
                                still shows ~0.3-0.5 s of our hold after
                                the game's blank (the load), and the
                                transformation loads its zigzag field in
                                view over ~1 s (no game blank there).
                                AWAITING MIKE'S PLAY PASS -- on the rig.
    rom/night/attract2.32x      previous line (2026-09-21 11:25)
                                = attract1 + the echo belt's re-mark half
                                actually firing (re-blast spacing counted
                                apart from the packet's age). Rig 4/4
                                launches, every background; the readout
                                twin (attbc2) 3/3 with the palette
                                counters agreeing on both CPUs. Awaiting
                                Mike's play pass -- on the rig.
    rom/night/attract1.32x      previous candidate (2026-09-21 10:45): 1/3
                                on the rig -- the re-mark half was inert
                                = round1 + high-score gates on by default
                                + BGBOTTOM (bottom-band backstop only
                                under opaque FG) + MDSREMARK + the echo
                                belt's second half (re-mark an unechoed
                                push). Rig: 4/4 launches, every
                                background; score table fills; level-2
                                floor complete. OPEN: the eye picture
                                loads in view (~2.5 s) -- its tiles are
                                unbaked (LESSONS attract cards, 5).
                                AWAITING MIKE'S PLAY PASS -- on the rig.
    rom/night/round1.32x        previous line (2026-09-21 09:30)
                                = echo2 + the round's MD table installed
                                on a round change while on screen (the
                                attract's level-2 demo had no background:
                                the game writes its round after the
                                display-on edge and the palette detector
                                cannot see level 2). Rig 3/3: level-1 and
                                level-2 demo backgrounds on every launch.
                                AWAITING MIKE'S PLAY PASS -- it is on the
                                rig.
    rom/night/echo2.32x         previous line (2026-09-21 05:20)
                                = slim31 + FBXECHO=1 (the framebuffer
                                packet echo belt) + the slim diag and
                                palette shadow moved out of slim_art.
                                Rig: pad-5 layout 3/3 (0/6 before),
                                line layout 3/3; ares palette gate PASS
                                on eight layouts. Closes the level-start
                                black background (LESSONS 2026-09-21
                                "lost boot-storm packet"). AWAITING
                                MIKE'S PLAY PASS -- it is on the rig.
    rom/night/slim31.32x        previous line (2026-09-21 01:15).
                                = slim30 + the first 16 BG palette
                                publishes forced (the FB persists across
                                a warm relaunch). Rig: background present
                                on 2 of 3 launches -- a per-launch race,
                                layout-biased (LESSONS 01:30). MIKE'S
                                CALL 2026-09-21: "THIS is the new line!"
                                (Zeus text solved, sprites smoother; the
                                black-background launch is the race).
                                Delta from bldS: docs/design/
                                LINE-DELTA-bldS-slim31.md. Mike played
                                slim30:
                                "background fixed, presentation tighter,
                                frames feel slower, one Zeus glyph left".
    rom/s16.32x                 line-equivalent (stamp bytes only)
    rig (MiSTer)                attract8b (pushed 2026-09-22 16:30)

    rom/night/slim25.32x        previous line. Mike: score table fixed
                                (14:20), then "black background" (18:24,
                                capture). Layout-dependent, see LESSONS.
    rom/night/slim23.32x        previous line (same day): + per-pixel
                                cat-1 masks for unbaked pages (the
                                transform) + the Zeus typewriter's row
                                mark.
    rom/night/slim21.32x        previous line (2026-09-20 03:30): the
                                slim pipeline, before the two fixes.
    rom/night/lineV.32x         the line before that (FB tile route +
                                the boot-stack move). Keep for "was this
                                always broken".
    rom/night/lineT.32x         == tilesmd4 (2026-09-18).

INVARIANT CHECK: rom/s16.32x differs from the line at the stamp bytes
only (`25f3 3fff`: BUILD_HASH32 at 0x25F3xx and the build string at 0x3FFFD4):

    cmp -l rom/s16.32x rom/night/slim21.32x \
      | awk '{printf "%x\n",$1-1}' | cut -c1-4 | sort -u

`rom/s16.32x` must be left holding the LINE build at the end of any
session. The slim diag counters at WRAM 0xFF3400-0xFF340F and the
SLIMVALUE readout are still compiled in; remove once nobody needs them.

WHAT THE LINE CARRIES THAT bldS DID NOT
    - baked MD tiles (TILESMD=1 in LINE_FLAGS). 377 KB at cart
      0x263C00; md_emit_art copies 16 halfwords instead of 64 ROM
      reads + 64 pen lookups + 32 writes.
    - the sky fix: colour index 0 is harvested and packed, so BG sets
      92/93/95/96/97 resolve pixel 0 to 0x01EC instead of the backdrop.
    - Mike on the rig: sky correct, "the framerate feels more
      consistent". Consistency, not mean speed, is the expected shape
      of this change -- it cuts the TAIL (see the transport card).

KNOWN DEFECTS ON THE LINE, accepted as the price of the baseline
    - wolf transformation: FIXED 2026-09-20. The flames are FG
      priority tiles on page 10 (all 800 cells, MAME census); our
      punch erased whole cells on that unbaked page. Per-pixel masks
      from the tile art now (m_main.c c1rt_class).
    - black tile pop-in, worst during the flame wipe (residency).
    - leftover text glyphs (four cells).
    - rounds 1/2/4 lose 12 pinned sets to overflow vs bldS; they fall
      to the framebuffer path. Level 1 (round 0) keeps all 30.

(The 2026-09-24/25 68K-time work -- instruments, the core's ROM wait,
WRAMCODE, the dual-chain count -- is mapped in docs/handoff/HANDOFF-68K-FETCH.md.)

## THE TIERS — what each machine is truth for (Mike's call, 2026-09-25 08:00 ET)

    MiSTer FPGA rig     truth for everything EXCEPT fetch-bound 68K
                        code, and the ACCEPTANCE GATE (Mike's pass,
                        3 launches per verdict) -- unchanged.
    ares (headless)     closer to hardware truth for fetch-bound 68K
                        code: the MiSTer core holds the 68K's DTACK for
                        a full SDRAM access on every MD ROM read (+18%
                        fixed, rtl/32X/IF.sv USE_ROM_WAIT=1); a mask-ROM
                        cartridge adds no wait. ares is optimistic only
                        by the SH-2 ROM-arbitration share (0-15% of
                        cart cycles, real on hardware, small for us).
    MAME 32X            a convenience model; cannot land R60 packets.
How to read a 68K-side rate number: the FPGA's is pessimistic by the
game's cart-fetch share of the pass (its demo pass 358-418 lines vs
ares' 232-246); the hardware figure lies near ares'. SH-2-side numbers
(blit, window, FB rate) stay the FPGA's. The core is not to be
rebuilt (fragments distribution, unverifiable, dies with the next core
update): the port engineers around it (WRAM-resident hot routines).

## THE AXIS — SPEED (Mike's call, 2026-09-24 13:00 ET)

**Direction changed back. Speed is the work; the gate is the span
instrument (RIGSPAN), not the rate counter.**

Mike: "Speed. Chase the pickup tail with the span instrument as the
gate." Target the TAIL, not the mean: a change that improves the mean
ack and leaves the +150-195 outliers alone buys nothing, because the
straddle is what costs a vint. Report as the span distribution (span 1
x6, span 2 x5), never as a percentage; success = the span-2 column at
zero on the level-1 demo. O-10 (the payload) is not the target. If the
tail work stalls, O-6 (leftover text glyphs) is the last defect Mike
has reported seeing.

RE-READ 18:45 ET with the light barcode (RIGBCEVERY=8, no longer
perturbs ares): level-1 demo, passes per vint --
    ares  566 vints: one-vint 242, two-vint 150, three-vint 8 = 71%
    FPGA  440 vints: one-vint 34,  two-vint ~170, three-vint 16 = 50%
The FPGA's deficit is one-vint passes becoming two-vint; the three-vint
column is small on both. Title: one-vint on both. Light-barcode A/Bs:
shim DMAs off (MDQUIET=2) and master idle off (IDLEOFF) leave the FPGA
at ~50% while ares gains from either. RIGSPAN=9 gauge: the FPGA's 68K
is ~5% slower than ares' on cart reads (77-80 vs 74 quarter-lines per
256) and WRAM code (112-115 vs 108) -- a platform constant, ~12 lines
on a ~250-line pass, exactly the margin ares' demo passes sit in. The
lever we own is 68K lines per pass, RANKED (LESSONS "the 68K-line
ranking", 23:40 ET): r60_push's palette rotor + compares 41 lines a
vint, its stage ship 18, the consumes/pump 15, the 0x3A9A text-copy
gate 10 a pass (gated per byte), rowscroll/selection 8, part B ~5;
the chain runs once per vint a pass spans. ~85 of the chain's ~110
lines a vint are ours. Item 4 (BURSTGATE, the 0x3A9A text-copy gate
once per burst) was cut first and converted NOTHING on the FPGA (34
one-vint per 440 vints before and after); our own call counters say the
site runs ~7.7 a pass in our demo, not the arcade's 22 a frame, so the
cut was ~3 lines, and RIGSPAN=10 says 2-vint passes end spread over
their second vint, not piled at the line. The direct read (RIGSPAN=11,
2026-09-25 03:30 ET): the FPGA's mean level-1 demo pass is 362-400
lines against ares' 232-246 -- ~130-150 lines longer, not 12. The 5%
gauge covered cart reads and WRAM loops; the term it did not cover is
68K access to the 32X framebuffer -- gauged too (RIGSPAN=13: +5%),
and the chain split (RIGSPAN=12) is equal on both machines, the blit
alongside changes nothing (RIGSPAN=14). FOUND (RIGSPAN=15, 06:20 ET):
INSTRUCTION FETCH FROM THE CART is 18-32% slower on the FPGA core than
from WRAM (132-148 vs 112 quarter-lines for one loop; ares 115 / 112),
and the game runs entirely from the cart: its pass is +55-65%. That is
the FPGA term, and it is the CORE'S, not hardware's (07:30 ET, LESSONS
"the FPGA's cart-fetch penalty is the core's ROM path"): the MiSTer
core holds the 68K's DTACK for a full SDRAM access on every MD ROM read
(+18% with the SH-2s' art fetches off, up to +32% with them on); a
mask-ROM cartridge adds no wait. The rig has been pessimistic on the
68K by that much since the first rate probe; the week's FPGA-vs-ares
gap was mostly the emulator. Option: rebuild the core with
USE_ROM_WAIT(0) (S32X.sv) -- Mike's call. BURSTGATE stays a knob, off
the line (0 conversions). The palette card (item 1 of the ranking)
is held. WRAMCODE=1 (hot routines in WRAM, tools/wram_reloc.py) MEASURED
(11:45 ET): exact by the scene timer on ares; on the FPGA the demo pass
shortens 30-45 lines (400/418 -> 356, 358/362 -> 332/328), 8 passes per
440 vints convert two-vint -> one-vint (34 -> 42), 4 three-vint -> two.
A quarter to a third of the FPGA's excess; the block list came from the
arcade's profile, the next set needs our own rom's (per-entry counters
or the interrupted-PC ring on the WRAM rom). LINE CANDIDATE wc1 =
`make line WRAMCODE=1` (rom/night/wc1.32x, 2026-09-24 18:00): ares boots
and renders (title, face cut); rig 3 REAL launches (wc1, wc1b, wc1c --
the launch API no-ops on a path already running, LESSONS), 60 shots:
level-1 demo, level-2 demo, scores, eye/face cuts, title all clean; the
black cut frames and the SEGA-screen transition frame match the line's
own corpora. Waits on Mike's play pass, then LINE_FLAGS += WRAMCODE=1.
SUPERSEDED THE SAME EVENING by wc2 = `make line WRAMCODE=1` with the
SECOND block set (rom/night/wc2.32x, 18:30): tools/wram_reloc.py now
re-points every abs.l jsr/jmp site at the WRAM copy (578 sites; the
stubs were 6% of the cart residue) and three ranges from OUR rom's
16-line PC-sampler profile (tools/pc_profile.py; LESSONS "our own rom's
PC profile") fill the region to 3536 of 3584 bytes. Exact on ares at the
same game time (title 426/619, demo 479: only the palette-cycle slot
timers differ, text pages identical). FPGA vs the first set: mean
level-1 demo pass 330-348 -> 316-330 lines (RIGSPAN=11); span histogram
(RIGSPAN=4) per ~900 vints one-vint 94/96 -> 118/114, three-vint 32/36
-> 22/26. Play pass wanted on wc2 (wc1's covers the mechanism, not the
new blocks). PICTURE GATE NOT CLEAN (19:10): three plain launches
(wc2a/b/c, 60 shots) -- launch 3 showed a PURPLE BAND over the bottom
third of the level-1 demo's ground in two consecutive shots 15 s apart
(the purple-band class, but not healed in 8 frames); zero hits in the
other ~600 rig shots on disk (wc1 x3, wc2's other five launches, every
line-era corpus). Reproduction run queued: rg1 and wc2 alternating,
three launches each (scratchpad purple_repro.sh; stopped after one rg1
launch, 0/20, when Mike took the rig). DECODED 19:40 (decompile's call):
the band is EXACTLY view rows 24-27 (pixel rows 192-223), columns 0-25
and 0-22 in the two shots -- the BGBOTTOM backstop's band (LESSONS
2026-09-21 item 3: blank BG cells in rows 24-27 only under a fully
opaque FG tile, decided through a 256-slot art cache). Relocated 68K
code never touches the framebuffer; its only channel is timing, and a
cache verdict is what a shorter pass can race. So: the known backstop
exposed by timing, NOT a new race in the relocated blocks. The
reproduction run, when the rig is free, targets BGBOTTOM: rg1 vs wc2
alternating, and the band's presence decides whether the line races too.
RESULT 23:25: rg1 0/80, wc2 0/60 across three launches each -- not
reproduced; the band stays a 1-in-6-launches event on wc2, unseen on
the line (LESSONS 23:25). wc2's picture gate: five clean of six.
CENSUS (00:13-03:00, 20-shot launches): rg1 0 events in 16 launches;
wc2 2 purple (both the boot's first demo, whole demo, gone at the cut)
+ 1 grey-tile level start in 16. MECHANISM FOUND 03:15 (LESSONS "the
purple band's mechanism"): BGBOTTOM's opacity cache was keyed by tile
code while the art depends on the bank (COMM2), never invalidated; a
bank-stale verdict stayed cached for the scene. FIX = key by art index
(exact: opacity is a function of the art). rom/night/wc4.32x = wc2 +
fix; rom/night/rg2.32x = `make line` + fix (the line has the same
latent race). BOOT-WINDOW CENSUS (03:20-05:50, 5 shots over the first
demo per launch): wc4 0 events in 72 launches; wc2 1 in 36 (its third
overall: 3 in 58 launches tonight, every one the boot's first demo).
At wc2's rate 72 unfixed launches would show ~3.7 events; P(0) ~ 2%.
THE FIX HOLDS. Candidate is now wc4 (= wc2 + fix, `make line
WRAMCODE=1`); rg2 (= `make line`, the fix alone) is the line's own
successor if WRAMCODE waits. Grey-tile level start: 1 on wc2 and 1 on
wc3 in 20-shot launches, 0 in the boot census on either rom; not yet
mechanised (a level-start install read before the map is final).
MIKE 2026-09-24 19:25, playing wc2 on the rig: "FEELS faster." (Play
pass in progress; the purple band not yet reported from play.)
THE FINDING BEHIND IT (LESSONS "our own rom's PC profile"): after
WRAMCODE the cart residue is 13% of the vint, so the core's ROM wait on
it is worth <= 8-10 lines a vint and the WRAM region is now full; the
FPGA's remaining ~90-100 lines a pass over ares are NOT cart fetch.
NIGHT OF 2026-09-24 (Mike: "work all night"): (a) the ROM wait covers
data reads too (decompile, IF.sv:780) but RIGSPAN=9 had already priced
that half at ~1 cycle a read, 2-6 lines a pass (LESSONS 19:45); (b)
the decompile's burst-tenure mechanism (SH-2 cache-line fills holding
the cart port) was tested with RIGSPAN=17 = CARTBENCH's fetch loop at
raw V 0xF0 (the cache_fill slot), 0x40, 0x10, 0x80: 132 at every phase
in the level-1 demo with sporadic spikes to 150 at any phase, ~1-2% of
fetch time, NOT the excess (LESSONS 22:50); (c) RIGSPAN=18 = the PC
sampler's class histogram over the barcode (idle / shim / WRAM game /
cart game shares per step), ares half read: idle 60%, WRAM game 25-29%,
cart game 7-9%, shim 4% (under-sampled: H-int samples during the masked
chain collapse to one); FPGA half queued behind the BGBOTTOM
reproduction. RESULT (LESSONS 23:35): only cart game code moves, 8% ->
12-17% of sampled time (~+10 lines a vint); WRAM code, shim and idle
shares are equal on both machines. The mean-pass excess was the SECOND
CHAIN of tipped passes (each ~110 lines, 85 ours), not a hidden term.
(d) wc3 = wc2 + a third block set in WRAM region 2 (0xFF4600, 1482 B,
the main loop and four handlers; WRAMCODE2=1, a knob): exact on ares,
0 conversions on the rig with interleaved controls (LESSONS 23:50).
THE CART-FETCH AXIS IS SPENT AT wc2. What is left on the 68K axis is
the chain's own lines, all ours, per vint spanned: the palette rotor
41 (LIVE. Mike 2026-09-25: the 2026-08-16 priority order that put
palette LAST is "disposable and no longer serves us" -- it predates the
native pivot, R60, CAT1MD and the MD planes. The rotor is the largest
item left on the 68K axis and the only one above the rig's ~15-line
ranking floor. O-13's caveat still stands: 94.7/82.3 is an ARES number
and does not transfer to this axis -- price it on the rig. The rotor
already skips clean blocks, its
lines go to blocks the game's streamer re-writes unchanged -- PAL_DIET
is the existing backoff knob, a fidelity trade), the stage ship 18,
the consumes 15 (MDQUIET=2 showed no FPGA gain), rowscroll/selection
8, part B 5. Instruments: RIGSPAN=11 +-5 lines, =4 +-10 conversions;
a cut under ~15 lines a vint cannot be ranked on the rig.
Mid-pass push RULED (decompile, HANDOFF-FROM-DECOMPILE §1): the arcade
queues palette writes during the pass and its vblank handler drains
them every vint, so a skipped push delays them; palette-only push on
mid-pass chains is the exact form. PRICED from the chain stamps: it
keeps the rotor+compares (41) and the regs+palette ship, skips only the
rowscroll compare (5), selection (3) and records ship (2) = ~10 lines
per mid-pass chain = ~5 lines a vint on the FPGA (48% of chains), ~3 on
ares: half of BURSTGATE's cut, which converted nothing. NOT BUILT; the
rotor (41, LIVE since Mike's 2026-09-25 call) is the lever. Dual-chain count (RIGSPAN=16): 48% of FPGA demo chains /
32% of ares' enter mid-pass and still push a full window -- ~33 lines a
vint on the FPGA if skipped; palette-timing trade noted in LESSONS.

CAVEAT 17:40 ET: every number below was read through the rig barcode,
whose per-vint 384-word VRAM DMA costs the 68K ~48 lines a vint (LESSONS
"the rig barcode itself"). On ares the LINE's demo passes finish inside
their vint (the 68K idles at every vint, miss counter 0); the barcode
alone makes them straddle. The FPGA reads carry the same handicap.
RIGBCEVERY=8 (barcode every 8th vint) is the re-read in flight; until
it lands the line's true FPGA rate is UNMEASURED.

MEASURED 2026-09-24 16:20 ET (span histogram, race-free hook, level-1
demo, every vint accounted for, barcode every vint = handicapped):
    ares  per 400 vints: 2-vint passes 182-188, 3-vint 8-12   (50%)
    FPGA  per 437 vints: 2-vint passes 22-44,  3-vint 116-130 (35-37%)
The pass NEVER waits on the FM window on either machine (RIGSPAN=2:
max release V per pass = 0), the shim does not wait at vint entry
(fmgate_belt constant), and the IRQ4 chain lands in the same band on
both (RIGWIN4). A pass is ~400 lines of 68K work incl. one IRQ4 chain;
on ares it ends 2-29 lines before the 2-vint deadline, so a few
percent of extra 68K time on the FPGA tips most passes to 3 vints.
The window model (ack line, pickup tail) is DEAD as a frame-rate
lever: the frame is 68K time. Shim DMAs off (MDQUIET=2) recovers
~20% of the tipped passes; the master's cart-reading idle work off
(IDLEOFF) is the A/B in flight. The lever, whichever term it is, is
68K lines per pass -- the shim's own vint work is the part we own.

### The previous axis: DEFECTS (Mike's call, 2026-09-17, superseded)

**Direction changed. Speed was PARKED; defects were the work.**

Mike: "we had a trajectory of moving the build to solve for the frame
updates, and that was useful. but since we have stalled this build for
over a week with no movement updates, might as well solve defects now."

The visual defects were being let through ON PURPOSE while the frame
rate was moving. It stopped moving, so the reason to let them through
expired.

**What this parks (not kills):** the transport ablation, O-1, and the
whole 60 Hz push. The findings below stay true and stay on the shelf:

    master compute ablated    84.7% of the name-table walk removed
                              -> FLAT on the rig
    slave compute ablated     ares' 58.3 fps was SLAVE work on a
                              slave-gated instrument
    transport, NBUILD1 off    0.33 fps against the line's 9.3 = 28x
                              -- a BRAKE REMOVED, not load reduced.
                              Whether reducing transport LOAD has a
                              slope is still the open question, and it
                              still needs rig time.

**The bar has NOT changed.** INTENT still says 60. This is a change of
what we work on now, not of what done means.

## PORT-SIDE DEFECTS FOUND AGAINST THE SPRITE ORACLE (2026-10-16)

    0. SCENE BOUNDARIES COME ~10 FRAMES EARLY, EVERY BOUNDARY. A fidelity
       defect in the port's LOGIC, and the tick rate is NOT the cause --
       that was measured and ruled out:
         arcade  timer 0x26 at frame 502, 0x27D at 1102 = 599 ticks / 600
                 frames (aw_502.bin / aw_1102.bin)
         ours    timer 0x33 at 500,  0x28A at 1100      = 599 ticks / 600
                 frames (sw81_wram_500/1100.bin)
       Identical, one tick a vint on both. So it is not a drain defect, and
       it is not the 60.054 vs 59.92 Hz difference either (that is only 6.7
       frames by frame 3000, against the 47 observed).
       THE LEAD IS IN WHERE SCENES START: 11 ticks ahead at the demo (after
       the 2-frame alignment), 47 ahead by the cave. Whatever the game waits
       on at a scene END -- a sound acknowledge, an MCU mailbox, a timer the
       shim answers immediately -- RETURNS SOONER FOR US than on the arcade.
       INSTRUMENT: the timer at matched frames, per scene. It is also what
       gives attract_parity its alignment, so fixing this fixes the
       instrument and the fidelity together.

    Measured at the CAVE scene on the game's own timer 0x12D, not on frame
    numbers: the arcade's sprite RAM (MAME wram_dump.lua WD_ADDR=0x440000,
    frame 3002, arc_spr_3002.bin) against our sprite table (SPR_SNAP
    0x26028400, our frame 2955, sw81_sprsnap_2955.bin). BOTH ARE THE PORT'S
    LOGIC, NOT THE RENDERER -- the records are wrong or missing before any
    drawing happens.

    INVESTIGATION 2026-10-16 (builder, independent of the advisor's dumps).
    What is now established, and what is not:

    RULED OUT: the sprite-RAM shadow truncation. SPR_SNAP covers only 64 of
    the 128 S16B records (w68.h:142 maps 0x440000..0x440400; cport_layout.py
    _SHADOW_TAIL names "sprite records 64+" as this layout's one failure
    mode), so "records the arcade has that we never create" looked like the
    shadow eating them. IT IS NOT. The arcade's own list at the cave
    TERMINATES AT SLOT 9 -- nine live records, nothing above 64, and the two
    "missing" records sit at slots 6 and 7, well inside the shadow. One MAME
    dump killed it.

    THE ARCADE'S CAVE LIST, decoded (frame 3002, timer 0x12D, 0x440000):
      slot 0  top 110 bot 144  x  39  pitch  11  addr 62106  bank 3  cset  9
      slot 1  top 110 bot 144  x  79  pitch  11  addr 62116  bank 3  cset  9
      slot 2  top 144 bot 178  x  39  pitch -11  addr 62491  bank 3  cset  9
      slot 3  top 144 bot 178  x  79  pitch -11  addr 62501  bank 3  cset  9
      slot 4  top 112 bot 181  x  46  pitch  24  addr 34217  bank 1  cset  0
      slot 5  top  44 bot  87  x 114  pitch  23  addr 24785  bank 1  cset  1
      slot 6  top 142 bot 185  x 289  pitch   4  addr   757  bank 4  cset 13
      slot 7  top 144 bot 184  x 216  pitch  12  addr 14857  bank 4  cset 10  zoom 0x84
      slot 8  top 154 bot 160  x 323  pitch   7  addr 55770  bank 0  cset 12
      slot 9  TERMINATOR

    THE cset PATH, traced end to end and VERIFIED AGAINST THE 68K:
      object byte 0x0A  ->  staging word 4 low byte at 0xFFF800 + idx*16 + 8
                        ->  sprite RAM word 4  ->  renderer's e[4] & 0x0F
      The staging store is hand/AT_FUN_00003d60.c:93
      `puVar9[9] = unaff_A6[10] | unaff_A6[9] << 6`, and the original 68K at
      0x3EE2-0x3EEC is `moveb fp@(9),d0 / lslw #6,d0 / orb fp@(10),d0 /
      moveb d0,a0@+`. SEMANTICALLY IDENTICAL -- both keep only bits 0,1 of
      A6[9] in bits 6,7 and OR A6[10] underneath. The copy loop
      (altbeast_port.c:3961-3973) and the symbol placement are also correct:
      _DAT_00440000 links to 0x06028400, which IS SPR_SNAP.
      So the sprite build is NOT the bug. The bug is upstream, in whatever
      sets object byte 0x0A. There are 87 writers of that byte at byte
      offset (a further 16 `unaff_A6[10]` writes are on WORD pointers and
      are byte 20, not this field -- do not grep this without resolving the
      pointer type, it is the declared-width class again).

    ALIGNMENT RESOLVED 2026-10-16: the advisor's "frame F" means ares
    `--frames F+1` (the dump reflects F+1 vints, the screenshot is F). My
    2956 is their 2955. Verified: at our frame 2956 the timer reads 0x12D
    and the tables correspond exactly. ALIGN ON THE TIMER, never on either
    of our frame labels.

    THE DEFECT LOCALISES TO FOUR BITS. Word 4 at timer 0x12D, both sides:
      slot  ARCADE           OURS
       0-3  F389  cset  9    F380  cset  0
         4  F180  cset  0    F180  IDENTICAL
         5  F181  cset  1    F181  IDENTICAL
         6  F48D  cset 13    (absent)
         7  F48A  cset 10    (absent)
         8  F08C  cset 12    F089  cset  9   (our slot 6)
    On EVERY shared record the BANK nibble (bits 8-11) is correct and bits
    6-7 of the low byte are correct. ONLY BITS 0-3 ARE WRONG. Since word 4
    is built as `A6[10] | A6[9] << 6`, that means A6[9] is RIGHT and
    A6[10]'s low nibble is WRONG, in the same object, read by the same
    instruction pair. So it is not an addressing or pointer-width fault --
    it is a value fault in object byte 0x0A alone.
    AND NOT EVERY OBJECT IS AFFECTED: slots 4 and 5 (bank 1, csets 0 and 1)
    are byte-identical. Whatever sets byte 0x0A works for those two.

    HYPOTHESIS KILLED SAME DAY: "the slot inherits the previous occupant's
    byte 0x0A". It rested on our record at addr 55770 carrying 0x89, which
    is the ARCADE's bolt value -- but OUR bolts carry 0x80 at that same
    tick, so 0x89 cannot have come from them. Coincidence, not a signature.

    IT IS SYSTEMATIC, NOT FIVE RECORDS. Our csets sampled at four of our own
    ticks (sw81, SPR_SNAP):
      timer 0x136  csets 0,0,0,0,0,1,0,0,0,0,0,0
      timer 0x14A  csets 0,1
      timer 0x15B  csets 0,1,8
      timer 0x16D  csets 0,1,9,0,0,0,0,0,0
    against the arcade's 9,9,9,9,0,1,13,10,12 at 0x12D. We emit almost
    nothing but 0 and 1. HYPOTHESIS, NOT YET TESTED: a slot inherits the
    previous occupant's byte 0x0A (at timer 0x16D our addr 55770 carries
    cset 9, which is the BOLTS' value, where the arcade gives it 12).

    ALIGNMENT: I could not reproduce the advisor's pairing. At sw81 frame
    2955 the timer reads 0x12C and the table shares NO address with the
    arcade's list; the advisor reported frame 2955 = timer 0x12D with five
    addresses matching. One of the two alignments is wrong and it matters,
    because "agrees in every field but cset" only means something if the
    records correspond. THE TIMER IS READABLE DIRECTLY: _WRAM_BASE =
    0x0602CC00 and WRAM_68K_ORIGIN = 0xFFC000, so the game's 0xFFF02A is
    SDRAM offset 0x2FC2A -- `--dump sdram:0x2FC2A:0x2:file`. Align on that,
    never on a frame number.

    THE LOAD-PACING DIVERGENCE (advisor's measurement, 2026-10-16): at the
    demo cut the ARCADE's scene timer 0xFFF02A is PARKED AT 0 from frame
    451 to 461 and only counts from the enable (3 at 467, 0x1b at 491),
    while OURS starts counting at ~449 and reaches 0x2C (44 ticks) by the
    enable at ~490. So the arcade's load is a ~16-frame synchronous routine
    with the main-loop timer frozen and ours is 44 frame-paced ticks.
    THIS IS THE BEST CANDIDATE MECHANISM FOR PORT DEFECT 0 (scene
    boundaries ~10 frames early): a timer that advances through every load
    where the arcade's does not will lead by roughly a load per boundary,
    and 47 ticks by the cave is a few boundaries of it.

    RULED OUT AS THE CAUSE, 2026-10-16 (builder), BECAUSE IT IS ALREADY
    FIXED: the multi-frame wait. wait_frame_flag at 0x397E ends `dbf d0`,
    so one call waits D0+1 frames, and port_l2.c:1777 still carries the
    note that the stub "yields once ... NOT fixed here". It reads like the
    whole answer. It is not. Of the 34 `jsr 0x397E` call sites in
    roms/altbeast/prog68k.bin, THIRTY are preceded by `moveq #0,D0` (one
    frame, which the single yield gets right), two more set D0 = 0, and
    only TWO ask for more:
        0x00BA0  move.w #240,D0  -> 241 frames
        0x00D02  move.w #120,D0  -> 121 frames
    and cport_prep.py:4058 ALREADY rewrites exactly those two
    (`_WAITN = {0xBA6: 241, 0xD08: 121}`, found by the game's own
    0xFFFFFEFE return-address breadcrumb). The stale comment in port_l2.c
    is what makes this look open; the code is done. Look elsewhere for the
    44-against-16.

    LEVEL 2'S EMPTY ROSTER: THE ALLOCATOR HANDSHAKE, 2026-10-21/22 (the builder,
    46a0ed46). FUN_0000e032 (0xE032-0xE050) walks A5 over 24 records from 0xFFFFC800
    and returns the free one IN A5 with the carry. A pass already re-read A5 after the
    call -- only where the caller models it as the local `unaff_A5`. TEN callers hold
    it as the PARAMETER `p_A5`, and they are the level-2 roster: kind inits 5, 6, 7,
    10, 11, 15 (0xE316, 0x132A0, 0x12B08, 0x1176A, 0x14A8C, 0x15306) plus 0x143DC and
    0x14A5A. The refresh never fired, `*p_A5 = 0x8000` set the live flag on the record
    they were CALLED with, and the object never existed.

    The enemy count leaks from there and locks the game out: the kind init commits
    0xFFF15D (+3 or +4) BEFORE the search and only the enemy's own death path releases
    it, so the spawner's cap at 0xE0A0 (`cmpi.b #10,0xFFF15D ; bcc` = refuse) is hit
    after three spawns and nothing ever appears again.

    MEASURED with two new probes, PORTHOLDLIVES (hold 0xFFE018 at 3, as
    tools/arcade_level_run.lua does from lua) and PORTKINDCENSUS (accumulate the
    distinct handlers ever live in slots 16-39, keyed by the round). Before the fix,
    68000 frames to round 2:
        round 0: 10 handlers   round 1: ONE (0ee80)   round 2: 3
    with 0xFFF15D = 0 at round 1 tick 0x006C and 10 at tick 0x2E70 with ZERO LIVE
    RECORDS IN THE BAND. That is Mike's "level 2 shows only brown and white wolves".

    THE DIP RETRACTION IS ITSELF WITHDRAWN, 2026-10-22 -- THE COMPARISON WAS VALID.
    It was retracted on the ground that the port sets DSW2 0xFD / DSW1 0xFF at
    md_src/md_main.c:7714 against MAME's 0x54 / 0x66. THAT LINE IS THE 68K SHIM'S
    MAILBOX AND THE SH-2 GAME NEVER READS IT. Under PORT_BOOT -- which is in every
    candidate's flags -- sh_src/port_l2.c:2504 writes the I/O cells itself:
        0xC42001 -> 0x54, 0xC42003 -> 0x66, everything else 0xFF
    byte for byte MAME's arcade defaults, for the reason the comment there gives (a
    cold boot read 0xFF in both and "the port was booting a different machine").
    MEASURED on a play dump of the current candidate: 0xFFF01A = 0x66, 0xFFF01B = 0x54,
    0xFFF030 = 0x02. So both machines are asked for the same roster, the arcade's
    sixteen IS our target, and a missing level-1 or level-2 kind is a PORT DEFECT.
    I propagated the retraction into this file without checking which side of the
    68K/SH-2 split that DIP write lives on; that was the error.

    NONE OF THAT TOUCHES THE DEFECT OR THE FIX, and this is the part to keep: the bug
    is proven from the LISTING (0xE032 returns A5; ten callers held it as a parameter
    and never re-read it) and from a reading that needs no arcade at all -- 0xFFF15D
    standing at its cap of 10 with ZERO live records in the band is a leak on its own
    terms. The roster comparison was corroboration, and it is the only part retracted.

    AFTER the fix the count no longer jams (6, not 10) and the same script no longer
    walks the game: round 0 scroll 0x12E8 at frame 68000 where it used to be in round 2
    at 0x18C0. Enemies now fight back, which is the point, but it means the level-2
    roster is NOT YET CONFIRMED -- the scripted run has to survive long enough to get
    there. A 190000-frame run is the next reading.

    THE COMPARISON IS THE KIND SET, NOT THE RECORDS (the advisor, 2026-10-21): combat
    diverges between the machines within seconds, so a record-by-record diff of level 2
    measures the divergence. And one end-of-run dump cannot see the set either -- the
    band is empty at most instants, which is why the census accumulates.

    THE GATE HEADLINE IS A TICK-LANDING LOTTERY, 2026-10-20 (the builder, three
    times in one session). `check-demo: N of 20 ticks differ` moves when a change
    perturbs TIMING, because each row is compared at whatever tick the port landed
    on and the gate scores a compare at a NEIGHBOURING tick as "same":

        PORTBLANKLATCH   14 -> 16   B 00a2 009f->00a2, B 00d4 00d5->00d4
                                    (the port used to SKIP both; 0 -> 66 and 0 -> 3)
        boss installers  16 -> 13   A 007c 007c->007a, A 00c4 00c4->00c3,
                                    A 01c0 01c0->01bf (all three slipped to a
                                    neighbour and found a 0-cell match there)

    One went the wrong way on a real improvement and the other went the right way
    on a change that cannot touch the attract at all. READ THE PER-ROW LANDED TICK
    AND THE ARCADE DUMP NAME, both of which the table prints; a row whose `landed`
    column changed is not comparable across runs. [[night-sweep-alignment-trap]]

    PORTBLANKLATCH BREAKS THE GAME. DO NOT SHIP IT. 2026-10-20, Mike played
    56a99c76 built with it and the picture is massively broken in-game (repeated
    tile rows across the ground, a red silhouette). His state at tick 0x0425 reads

        0xFFF0F4 = 0x0000   (healthy 0x0101)
        0xFFF0F6 = 0x5555   (healthy 0x5656)

    the S16 tilemap PAGE ASSIGNMENT source, and port_l2.c:1838 -- the comment on
    the very block the flag disables -- says "Stuck at zero, decode_pages hands the
    compose 'page 0 everywhere'". The flag holds 0xFFF01C SET through a load so
    IRQ4 takes its do-nothing branch; that same IRQ4 block is what PUBLISHES those
    registers. The clock fix and the publish are the same mechanism and cannot be
    separated this way. The attract is clean on both builds (0xFFF0F4 = 0x0101 at
    frame 900 each), which is why the gate never saw it -- THE GATE ONLY COVERS THE
    ATTRACT, so no drift fix can be accepted on check-demo alone.

    The +52 -> +16 drift measurement stands; the remedy does not. A correct fix has
    to park the game's clock WITHOUT suppressing the page publish.

    RETRACTED -- "NO PAD INPUT REACHES THE PORT UNDER ares --input" (the builder,
    2026-10-20, wrong for about an hour and written into b50fbe0d as well).
    HEADLESS INPUT WORKS: a coin at frame 200 (`200,y,1` / `230,y,0`) takes
    0xFFF030 to 1 by frame 700 on build/port/rom/s16.32x. What I measured was
    0xC41001/0xC41003/0xC41007 at SDRAM 0x2C900, and l2m_io_live() -- the ONLY
    writer of those three -- is compiled out without R60LIVEPAD (Makefile:2013,
    which also sets PORT_LIVEPAD). They sit at 0xFF whatever the pad does, so the
    reading proved nothing. The same message claimed R60_LIVEPAD was in
    build/port/.build_flags; it is not, and was not in the rom Mike played.
    The coin is X/Y/Z and START never coins (md_src/r60_shim.h:206).


    PORT DEFECT 0 HAS ITS MECHANISM, 2026-10-20 (the builder), 1b79dc54,
    `make port PORTBLANKLATCH=1`. Port game frame minus arcade game frame at the
    SAME wall frame, tests/demo-b/arcade against ares dumps of our own rom:

        boot            +8   flat through X, T, E
        E->A install   +19   -> +27, then FLAT for 600 wall frames of demo A
        H install       +3   -> +30
        H->B install   +19   -> +49, then FLAT for 600 wall frames of demo B
        B end           +3   -> +52

    THE RATE INSIDE EVERY SCENE IS EXACTLY RIGHT. The whole lead is made at the
    transitions. The two +19s are the two load blanks sites 3/4 already pace, and
    the pacing WAS firing (port_blank_paced 36, 4 loads, site 4 length 22). It
    leaked anyway because the hold called port_frame_wait, which IS wait_frame_flag
    and clears 0xFFF01C on entry: every held vint found the latch clear, published,
    and advanced 0xFFF020/21. The arcade's main loop is SUSPENDED inside the load,
    never reaches wait_frame_flag, so its latch stays set, every IRQ4 takes the
    `bump 0xFFF144 and do nothing' branch and its clock PARKS -- arcade gf reads
    1469 at wall 1494, 1495, 1496 and 1497 where we counted 1514, 1515, 1516, 1517.
    port_frame_hold() is the yield without the clear. With it:

        boot +8 | E->A +1 | H +3 | H->B +1 | B end +3  -> +16 at wall 2100 (was +52)

    AND THE GATE HEADLINE GETS WORSE WHILE THE MEASUREMENT GETS TRUER: 16 of 20
    differ against 14. Both of the two extra rows are ticks the port USED TO SKIP:

        B 00a2  landed 009f -> 00a2 (aw_B_1657 -> aw_B_1660)  0 -> 66 cells
        B 00d4  landed 00d5 -> 00d4 (aw_B_1711 -> aw_B_1710)  0 -> 3 cells

    Their old "0" was a compare at a NEIGHBOURING tick, which the gate scores as
    same. B 00d4 at its own tick is 3 cells -- 0:1 8:1 40:1, the trio that appears
    in nearly every row -- and is now the cleanest non-trivial row in the gate.
    B 00a2's 66 is the first honest reading of that tick. Every other row moved by
    1 to 3 cells. The two counts are NOT comparable on those rows; rank this flag
    on the drift table and on which tick each row landed at, not on the headline.

    NOT ON BY DEFAULT. Awaiting Mike's play pass.

    THE PORT GATE, 2026-10-20 (the builder), commit 5c7fc293 on rebuild60-p1,
    8 MB cart. `python3 tests/demo-b/check_demo.py build/port/rom/s16.32x`:
        check-demo: 14 of 20 ticks differ, 18 with stale FB staging
        picture:     1 of 20 ticks differ beyond their control
        NOT REACHED: H 02ba (x2) only -- A 02b8 and B 0246 NOW REACH
    Against the 2026-10-16 reading below: two fewer ticks differ, two more
    reach. What moved it was the cart (ca51f3a3, 4 -> 8 MB) letting the
    closure take all sixteen targets of the kind table at 0x1D2EA, and then
    5c7fc293 giving the last two a dispatch row (they are `bra.s` thunks, so
    neither cport_dispatch loop could place them). B 0228 went 17 cells -> 13.

    AND THE DEMO-A RESIDUE IS PHASE, NOT LOGIC. Measured, not inferred:

    * Slots 62/63 (handler 0x0F352, an oscillating PAIR) differ at six of the
      nine A ticks and are the largest contributor to the count. Their Y
      ALTERNATES 0x0058 <-> ~0x00B8 every frame, and at aw_A_702 THE TWO SIDES
      SWAP: arcade 0x00BA/0x00BC with the port at 0x0058, where every other
      tick has the arcade at 0x0058 and the port at 0x00B6-0x00BC. Both
      machines produce both values; we are one frame out. +0x14 says the same
      thing one step at a time (arcade 0x0080 / port 0x0078, step +0x18 = 8;
      at 702 arcade 0x0010 / port 0x0018). Same family as the round-clear beam.
    * The A 0074 live-set gap (arcade 9,10,11,12,13 live, ours dead -- handler
      0x06CB8, five trail objects) closes by frame 586: aw_A_588 reads 11111 on
      both. We spawn them ~8 frames late, we do not fail to spawn them.
    * A 0118's 1-5 was already resolved as the cycling emitter's phase below.

    So the tick count overstates the defect count, and the open demo-A work is
    sub-tick ALIGNMENT, not a missing or wrong handler. [[night-sweep-alignment-trap]]

    THE PORT GATE ON THE LINE, 2026-10-16: sw108 (9516068a) FAILS IT.
    `DEMO_PROBE=1 python3 tests/demo-b/check_demo.py <rom>`:
        check-demo: 16 of 20 ticks differ, 16 with stale FB staging
        picture:     1 of 20 ticks differ beyond their control
        NOT REACHED: A 02b8, H 02ba (x2), B 0246
        LIVE SET DIFFERS at A 0118 only:
          live arcade [0,1,2,3,4,5,8,17,18,19,20,21,22,40,48,49,62,63]
          live port   [0,      8,17,18,19,20,21,22,40,48,49,62,63]
          -- objects 1,2,3,4,5 LIVE ON THE ARCADE AND DEAD IN OURS.
    **AND THAT READING IS WRONG -- THEY DO SPAWN.** (Builder, corrected
    the same day by measuring instead of inferring.) Objects 1-5 at demo A
    are a CYCLING EMITTER, five slots staggered by 4, and at tick 0xEF OUR
    PORT MATCHES THE ARCADE EXACTLY -- n1/n2 dead with 0x20=FF 0x4E=1F,
    n3/n4/n5 live at 0x20 = 02/06/0A, 0x4E = 1B/13/0B. The mechanism
    works. At tick 0x118 the arcade has all five live and we have all five
    dead because the emitter is at a DIFFERENT PHASE, not because the
    objects are missing.
    What the five records actually differ in at 0x118 is four or five
    bytes, the SAME offsets in every one:
        0x00  live flag   arcade 88          ours 00
        0x07              arcade B2          ours B3   (n2-n5)
        0x0D              arcade A6 9E 96 8E 86 / ours +7 on each
        0x20  life timer  arcade 0F 13 17 1B 1F / ours FF (dead)
        0x4E  ZOOM        arcade 01 18 10 08 00 / ours 1F
    0x4E IS THE ZOOM LEVEL -- port_zoom_scale reads `a6[0x4e] & 0x1f`
    (port_l2.c). 0x20 COUNTS DOWN and the slot dies at 0; 0xFF/0x1F are the
    dead-slot values, not corruption.
    **ANSWERED 2026-10-16: IT IS PURE PHASE. THE EMITTER IS CORRECT.**
    Traced the arcade's own cycle across its dumps (aw_A_690..744): all
    five slots spawn TOGETHER at byte 0x20 = 0F 13 17 1B 1F, each counts
    DOWN one per frame, each dies as it passes 0, all are dead by f738 and
    the set respawns at f744. Cycle ~38 frames.
    OUR SPAWN PARAMETERS ARE IDENTICAL. At our tick 0x100 the five read
    02 06 0A 0E 12, which is exactly 0F 13 17 1B 1F MINUS 13 on all five.
    Ours spawned 13 frames earlier and is counting down at the same rate
    from the same values. Nothing about the emitter is wrong.
    WHAT IS WRONG IS WHEN: ours spawned at about frame 692, the arcade at
    706, so our emitter runs ~14 FRAMES AHEAD here -- the same lead the
    load-pacing entry measures.
    **SO THE GATE'S "live set differs" FAILURES ARE THE TIMING LEAD, NOT
    OBJECT DEFECTS.** Do not open cards against them. The one clock defect
    accounts for the demo A 0x118 live-set failure and very probably the
    cave pool (n18-n22 dead in ours) as well, since that has the same
    signature: objects correct in their own terms, observed at a tick where
    our cycle has moved on.
    STILL ITS OWN DEFECT, because it is a VALUE and not a phase: the
    palette byte 0x0A (class (b) below). A phase difference cannot make a
    live object carry the wrong colour while every other field matches. Demo A tick 0x118 is a far cheaper
    reproducer than the cave -- it is inside the gate's own corpus, the
    arcade dump already exists (aw_A_744.bin), and it is five objects with
    LOW indices (1-5, the STATIC part of the table, not the dynamic pool).
    Start there, not at the cave.
    CAVEAT ON THE OTHER NUMBER: "16 with stale FB staging" is pages 0-11 of
    the old FB tilemap staging, which the r60 renderer does not use. Treat
    it as an r60 artefact until someone shows otherwise, and do NOT read
    "16 of 20" as sixteen independent record defects.
    TRAP, cost me a wrong conclusion: `check_demo.py ... | tail` reports
    TAIL's exit status, so the gate looks like it passed. Run it unpiped or
    check PIPESTATUS.

    THE WHOLE-OBJECT DIFF, 2026-10-16 (builder). The sprite table was the
    wrong place to look: the objects themselves diff cleanly. sw108 at our
    frame 2957 (timer 0x12D, verified by reading 0xFFF02A at SDRAM 0x2FC2A)
    against the arcade at frame 3002, all 64 records at 0xFFC000 + n*0x80,
    0x80 bytes each. THREE DEFECT CLASSES, EACH ONE SINGLE BYTE:

    (a) **RETRACTED BY THE BUILDER, SAME DAY, BEFORE ANYTHING WAS BUILT ON
        IT.** I wrote that byte 0x08 = 0xFF on 17 objects (n23..n39) WAS the
        "records the arcade has that we never create" defect, located, and
        said so to the advisor, who called it the cleanest finding of the
        day. IT IS NOT THE DEFECT. I read byte 0x08 without reading byte
        0x00 beside it. Objects n23..n39 have byte 0x00 = 0x00 ON BOTH
        MACHINES -- they are DEAD on the arcade too and draw nothing there.
        The 0xFF is harmless bookkeeping: the arcade leaves a stale staging
        index on a dead object and we clear ours. A one-byte difference on
        a dead object is not a defect.
        THE LESSON, since this is the second time today: a single-byte diff
        is only a finding once you have read the LIVE FLAG next to it. The
        diff tool should print byte 0x00 on every row, and now does.

    (a') THE REAL DEFECT IN THE SAME PLACE, and it is bigger. Objects
        16..39 are a DYNAMIC POOL (0..15 and 40.. hold byte 0x08 == n on
        both machines, static and identical). Inside the pool:
          n16  arcade live 89, idx 11, cset 0D | ours live 80, idx 10, cset 00  (56 bytes differ)
          n17  arcade live 88, idx 14, cset 0A | ours live 80, idx 11, cset 00  (59 bytes differ)
          n18  arcade LIVE 80                  | ours DEAD 00                   (11)
          n19  arcade LIVE 80                  | ours DEAD 00                   (12)
          n20  arcade LIVE 80, cset 0D         | ours DEAD 00                   (27)
          n21  arcade LIVE 80, cset 0D         | ours DEAD 00                   (22)
          n22  arcade LIVE 80, cset 0D         | ours DEAD 00                   (24)
          n23..n39  DEAD on both               (1 byte, the stale index)
        So SEVEN pool objects are wrong: FIVE THAT NEVER SPAWN AT ALL
        (n18-n22, live on the arcade, dead in ours) and TWO that are live
        in both but in a wholly different state (n16, n17).
        AND THE TWO MISSING SPRITE RECORDS ARE n16 AND n17: the arcade's
        absent records carry cset 13 and cset 10, and n16's arcade cset is
        0x0D (13) and n17's is 0x0A (10). They are live in our port and
        produce no record, because 56 and 59 of their 128 bytes are wrong.
        That is object STATE, not slot allocation and not the renderer.
        Note also the arcade's pool indices are NOT sequential (11, 14, 15,
        16, 10, 12, 13) while ours are (10, 11, 12, 13, 14): the arcade is
        handing out a free list in release order and we are handing out a
        counter. Cosmetic while the set matches; it will not match while
        five objects are missing.

    (b) BYTE 0x0A WRONG ON 10 OBJECTS AS THE ONLY DIFFERING BYTE (plus more
        inside multi-byte records). This is the cset. The value pairs:
            arcade 0x04 -> ours 0x00   x4
            arcade 0x05 -> ours 0x00   x1
            arcade 0x05 -> ours 0x04   x1
            arcade 0x06 -> ours 0x05   x6
            arcade 0x09 -> ours 0x00   x8
            arcade 0x0A -> ours 0x00   x1
            arcade 0x0C -> ours 0x09   x2
            arcade 0x0D -> ours 0x00   x4
        Not a constant offset and not a mask. We emit only {0,1,2,4,5,9}
        and never 6, 0x0A, 0x0C or 0x0D. UNTESTED READING: that looks like
        an ALLOCATION that has run short rather than a copy that has gone
        wrong, which would tie it to (a) -- 17 objects that never claimed a
        slot are 17 claims missing from whatever else is handed out in the
        same pass. Test before believing it.

    (c) BYTE 0x22 OFF BY ONE ON 3 OBJECTS (n0, n8: 0x01 -> 0x02; n40:
        0x5A -> 0x5B), ours AHEAD in each. Byte 0x22 is an animation
        counter (altbeast_port.c:3663 increments it and masks 0x1F). Ours
        being one ahead is the SCENE-BOUNDARY LEAD defect (port defect 0)
        visible inside a single object, which makes it the cheapest
        reproducer we have for it: three bytes, one tick, no scene
        boundary needed.

    19 further objects differ in more than one byte and are not yet split.

    1. WRONG COLOUR SET on five records that agree in EVERY OTHER FIELD.
       Word 4's low nibble, i.e. the palette index the port writes.
         ours cset 0,  arcade 9   -- addr 62106 / 62116 / 62491 / 62501,
                                     bank 3, the dragon's bolts
         ours cset 9,  arcade 12  -- addr 55770, bank 0
    2. TWO RECORDS THE ARCADE HAS AND WE NEVER CREATE at that tick. Not
       skipped at draw time, ABSENT from the table:
         top 142 bot 185 x 289 pitch 4  addr 757   bank 4 cset 13
         top 144 bot 184 x 216 pitch 12 addr 14857 bank 4 cset 10 zoom 0x84
       These are the white creature and the worm from Mike's capture.

## MEASURED DEAD — do not re-propose without new evidence

    STEP A + STEP B        BUILT 2026-10-15, advisor-bisect 2a9f6347, AWAITING
    (the incremental cut)  MIKE'S EYE AND THE BOARD. Step A is the canvas
                          offset: paint once, scroll by line table + shift
                          bit. Step B is R60FILL32 (default OFF): under the
                          hold the slave paints both planes into the bank in
                          3-row bands top down, plus the entering column and
                          the cells under each cleared sprite box,
                          world-anchored, scrolled by the step-A offset,
                          colours committed by the master into 32X sets
                          7/11/15/10, painted only AFTER the plan, MD display
                          off so the unpainted part reads black.
                          TWO ROMS, SAME ELEVEN FLAGS:
                            sw102  flag OFF, the line candidate
                                   sha1 8cc473e5563e72e75aebb9058d1d353f8f02fc04
                            sw103  R60FILL32=1
                                   sha1 3010515e4c81205f22b7cc9492d73b8cfd28dac3
                          THE DECISION IS A TRADE, AND IT IS AN EYE CALL.
                          With the fill the scene appears TOP DOWN from f462
                          (black 0.82) to f540 (0.07), right colours, sprites
                          and text on top -- but the hold runs 0.28 flips/vint.
                          Without it the hold is BLACK at 1 flip/vint and the
                          scene is whole at ~f520, as sw81. So the fill shows
                          something 58 FRAMES EARLIER and is WHOLE 20 FRAMES
                          LATER. Which of those the arcade looks like is
                          Mike's eye; neither number decides it.
                          Note the shape: "whole later" is the SAME trade the
                          record cap (sw82/sw83) made, for a different reason.
                          Step A's own cost: soak 0.80/0.78/0.79/0.79/0.79,
                          ONE POINT under sw81. sw102's soak and parity are
                          still running; the line does not move until they do.
                          Board unread. ares overcharges stores, so the rig
                          MAY run the fill faster -- but see LESSONS: that was
                          measured on the store-dominated blit and the fill is
                          reads and stores both, so the sign is not known.
                          2026-10-16, THE PACKET-A MOVE IS COMPLETE AND GATED.
                          sw102 was withdrawn (it moved md_main.c alone and
                          the live 68K consumed 0 A packets; see LESSONS).
                          Now: SH-2 publishes at FB_WBASE + 0x1E000
                          (r60_main.c:730), the 68K consumes 0x85E000
                          (r60_shim.h:77), and the only surviving 0x851A00 in
                          the live path is a COMMENT at r60_main.c:722.
                          Verified by grep across md_src and sh_src: the other
                          hits are testcart.c, fmdl_proof.h and m_main.c, all
                          probe or old-renderer code.
                          CENSUS GATE PASSED, one read: 68K A-consumed at
                          0xFFA076, f1268 = 1022 (sw106) against sw88's 1022
                          and sw102's 0. With R60FILL32 (sw107) A is consumed
                          THROUGH the hold, 438 at f470 to 468 at f600.
                          Eye luma sw106 9.4 / 8.4 at f1268 / f1400 against
                          sw81's 8.5 / 8.1. READ THAT AS INDISTINGUISHABLE,
                          not as a 0.9 regression: the broken build read 44-75
                          in the same band, so a sub-point difference inside
                          8-14 is not a defect signal. The frames decide it if
                          anyone wants it settled, not the scalar.
                          sw109 MEASURED ACROSS THE WHOLE ATTRACT,
                          2026-10-16 (ap109, sw109_soak_6000/12000/30000):
                          THE TRADE HOLDS EVERYWHERE AND HERE IS THE PAIR.
                            boot card   collapses at lag 20-30 (21/11) where
                                        EVERY other build sat at 55 until lag
                                        90. The strongest single row: a scene
                                        we effectively never matched inside
                                        the window now matches.
                            cut black   25/30/95/81/69/49 from lag 12 against
                                        sw108's 33/3/137/138/138/29
                            demo scene  95/86/80/66/53/45 from lag 8 against
                                        125/133/133/32/29/30
                            demo 2      76/70/55/41/39/30 against
                                        135/135/28/29/32/25
                            face, eye, eye pan  within noise
                          SO: first content ~60 FRAMES EARLIER at every cut
                          and at boot; completion later; cadence 0.73/0.72/
                          0.72 against sw108's 0.79 and sw81's 0.80. The 7
                          points are exactly the 54 holds running at 0.28 for
                          ~60 frames each, so the cost is accounted for and
                          is not a mystery.
                          THE DRIFT CAVEAT DOES NOT APPLY TO THIS COMPARISON.
                          The timer lead is common-mode -- both builds tick
                          every vint and the fill does not touch the 68K --
                          so sw109-against-sw108 at a matched row is valid
                          even where sw109-against-the-ARCADE is not. Drift
                          invalidates "how far we trail", never "which build
                          is better at this row".
                          AND NEITHER HALF OF THE TRADE TRANSFERS TO THE
                          BOARD. The rig presents ~8 fps of 60 and ares
                          cadence numbers do not carry (see the presented-rate
                          ceiling); the fill's cost is reads and stores, which
                          ares charges in both directions. Both the gain and
                          the price have to be re-read on the rig.
                          **R60FILL32 IS PARKED ON MIKE'S EYE (ares),
                          2026-10-16.** His words on the first pass: "sw108 is
                          the more polished rom, with cleaner transitions,
                          tile transitions (still messy in transition but once
                          drawn clean), and consistent sprite movement". So on
                          ares the BLACK HOLD BEATS THE TOP-DOWN REVEAL and
                          the fill's 7 cadence points read as INCONSISTENT
                          SPRITE MOTION.
                          HIS OBJECTION HAS TWO HALVES AND ONLY ONE OF THEM
                          CAN BE RE-OPENED BY THE BOARD:
                            - the cadence (inconsistent sprite movement) is
                              cost, and the cost does not transfer -- if the
                              rig runs the fill cheaper this half may vanish
                            - the preference for a black hold OVER a
                              progressive reveal is a look, not a speed. No
                              rig number changes it.
                          So before spending rig cycles on sw109, establish
                          which half he was weighing. If it is the reveal as
                          such, the board cannot save it.
                          Note "still messy in transition" describes sw108 TOO
                          -- it is the walker's fill latency at a scene change
                          with no game blank (the face at f1045), which is the
                          next renderer card and is independent of the fill.
                          Candidates sw108 (flag off) and sw109 (fill). sw105's soak and parity are the
                          running numbers for flag-off WITHOUT the move.
                          2026-10-16, sw108 (9516068a) LANDED AND MEASURED,
                          AND IT IS NOT A LINE CANDIDATE ON ITS OWN MERITS.
                          Soak 0.80/0.79/0.79/0.79/0.79, the same as sw105
                          and sw88, ONE POINT under sw81's 0.80.
                          attract_parity ap108 identical to sw105 within
                          noise: eye collapses to 14/17/14/8/10 from lag 20,
                          cut black 0/0/0 at lags 3-8, demo at lag 45, boot
                          card at lag 90.
                          So step A costs a point and buys NOTHING standalone
                          -- it is infrastructure for the fill. It earns its
                          place when step B does, or if Mike judges the point
                          free. Do not promote it for being nearly equal.
                          BACK ON THE HOME LAN as of 2026-10-16 evening
                          (10.0.4.94, mister.office.local answers in 1.9 ms),
                          so the board is available again and the entry below
                          is history. THE RIG QUESTIONS THAT WERE PARKED ARE
                          NOW RUNNABLE: chiefly whether hardware charges the
                          32X plane fill less than ares, which is the cadence
                          half of Mike's sw109 objection. The LOOK half (black
                          hold versus progressive reveal) is still not a rig
                          question.
                          THE RIGS WERE NOT DOWN -- WE WERE OFF THE LAN
                          (Mike, 2026-10-16, correcting the builder, who read
                          a failed ping as a dead rig and wrote it here and
                          into the advisor's traffic). The board is simply
                          UNAVAILABLE FROM THIS LOCATION, and work does not
                          stop for it: ares and BlastEm are the instruments.
                          AND BlastEm ANSWERS THE HALF THAT LOOKED BLOCKED.
                          BLASTEM_FB_WAIT is SH-2 clocks per 16-bit FB write,
                          3 = ares's model, 11 = THE RIG'S measured blit rate
                          (held on three spans, BLASTEM.md). The fill is
                          FB-write-bound, so the question "does the rig charge
                          the fill less than ares" is a BlastEm run at wait 3
                          against wait 11, not a rig cycle. Scoped as always:
                          no CPU-to-CPU contention model, and prove
                          same-picture before quoting any timing number.

    THE 32X DRAWS THE CUT, as a FULL REDRAW
                          MEASURED DEAD 2026-10-15 (sw85/86/87, probe
                          parked as sw87_32xfill.patch, line source
                          restored). The card: the slave paints both
                          planes into the bank from the same tiles,
                          pen maps and quantised colours while the MD
                          fills behind its dark display, borrowing 32X
                          CRAM sets 7/11/15/10 that no sampled frame's
                          sprites used. The borrow was sound; the COST
                          is not. A full-screen draw of 2,436 tiles is
                          2.1-2.5 VINTS A CALL even with per-tile pen
                          tables and longword tile reads (R60_GATE_FILLV
                          39 calls / 82 vints by f470, 41 / 102 by f500,
                          sw87_hd_470/500.bin). At that cost the slave's
                          pass outruns the GO/DONE protocol: the master
                          declines every vint (DIAG[8] +1/vint), flips
                          fall to one per TEN frames, and only part of
                          one bank gets painted (sw86 at f470: bank 0
                          zero fill pixels, bank 1 16,456).
                          It is LOAD-BOUND, like the old blit. sw85's
                          first shape was ~15 frames a draw on per-pixel
                          uncached map reads; hoisting the map to a
                          per-tile table bought it down to ~2 vints, and
                          what is left is 156k pixels of reads and
                          stores. There is no table left to hoist.
                          STILL LIVE, as a day's card, and now with its
                          scroll step PROVEN RATHER THAN ASSUMED:
                            - paint the plane into the bank ONCE
                            - scroll it by the LINE TABLE plus the shift
                              bit, which costs nothing and is measured
                              on ares (R60LTPROBE, lt2.32x; see LESSONS)
                            - paint only the ENTERING COLUMN and the
                              cells under last frame's sprite boxes
                            - colours from the BAKED per-scene tables
                              (bake_tilecram.py), NOT an allocator
                          Design its interaction with GO/DONE FIRST: the
                          full redraw died on the protocol, not on the
                          arithmetic. Open on it: the scroll is ares-only
                          until lt2 is read on the office rig, and Sega
                          bulletin #14 (shift bit invalid at line-table
                          low byte $FF) is live for the first time and
                          is not guarded.

    CACHELOCK             2 KB against a 14 KB working set
    footprint split       no cold region exists; a 2-frame window
                          already touches 14,256 B of 15,408
    relocation            .text is the cached cart view; three moves
                          changed touched bytes 25,552 -> 25,568
    NT_SKIP as a SPEED card   84.7% skip -> flat on the rig
    TAGKEEP               FALSIFIED: [22]=0 [23]=26, 100% MOVED, over
                          two independent windows. It defers a wipe
                          that is always needed. STILL ON THE LINE —
                          removing it is an open card.
    MDCHEV                packing the chevron sets changed no pixel
    static bake for runtime-allocated sets   structurally no path
    triple buffering      the pipeline is already full
    re-timing generally
    SH2_CCTL_TW (card T2)
    MDS_NOFLUSH           FALSIFIED 2026-09-17 same day. Dropping
                          mds_flush at the display gate: claims
                          2765->2144 but evictions 499->1022 (no net
                          win) and 41% of pixels wrong. The evidence
                          was an ORDERING ARTIFACT -- the flush runs
                          first and empties the tags, so install's
                          changed[] "wiping only 2" measured nothing.
                          Without the flush it wipes 1023/1024. The
                          flush also clears md_ref/md_dirty, which the
                          install path does not.
    NT ship-skip (NTHASH)  FALSIFIED 2026-09-17 the day it was written.
                          The NT chunk payload encodes ALLOCATOR SLOT
                          INDICES, not tile identity, so a visually
                          static scene still emits a payload that is
                          almost never byte-identical: 3 chunks skipped
                          of 2384. The 280-word hash also put master
                          compute on the window critical path and moved
                          4900 px at f3000 off three skips. Dedup of
                          this stream needs a STABLE ENCODING first --
                          same root as the black tiles.

## SHIPPED — do not cost these as if they were proposals

**ARCHITECTURE.md §4 reads like a proposal because it was written as
one. It shipped.** Both scroll planes are on the MD VDP:

    -DMD_BG -DMD_BG_FG0     both planes on the MD VDP
    -DC1_NOFB               framebuffer cat-1 pass DELETED
    -DC1_PUNCH -DC1_STAMP   sprite suppression under cat-1, per-pixel
    -DMD_SPR -DMDSPR_TOP    MD sprite path
    -DMD_STATIC -DPAL_STATIC -DPEN_MATCH
    -DSET_COLS              replaces bm_scan_rows in GAMEPLAY

**`.build_flags` is NOT the authority for the line.** It is the stamp of
whatever was BUILT LAST, which is usually a probe. At the start of
2026-09-16 it carried `NT_SKIP`, `NT_KEY8`, `BOOT_VALUE` and
`BOOT_FLIPRATE` — none of which are in the line.

    the line's flags   Makefile `LINE_FLAGS` (Makefile:2890), i.e.
                       whatever `make line` passes. That target exists
                       precisely because `make ship-us` alone is not it.
    what is built now  `.build_flags`. Check it MATCHES the line before
                       trusting a measurement against rom/s16.32x.

**Read both before costing anything.**

## OPEN DEFECTS Mike can see

    high-score palettes are       NEW 2026-10-15, ORACLE-VERIFIED, found by
    SHIFTED ONE WORD              the advisor thread, unfixed and unstarted.
                                  At the high-score table (arcade frame 2502,
                                  MAME wram_dump.lua WD_ADDR=0x840000) the
                                  arcade's palette RAM reads
                                    pal 2: 0000 6ffa 4fd8 4fb6 4f94 7fff 7fff 0000
                                    pal 5: 0000 30ff 30be 309c 307a 7fff 7fff 0000
                                  and our PAL_SH (0x06027000, the port's copy
                                  of 0x840000) at the MATCHED frame reads
                                    pal 2: 0000 0000 6ffa 4fd8 4fb6 4f94 7fff 7fff
                                    pal 5: 0000 0000 30ff 30be 309c 307a 7fff 7fff
                                  ours[k+1] = arcade[k], a ONE-WORD SHIFT, for
                                  the table's palettes. PAL 6 MATCHES EXACTLY,
                                  and it is written by a different site, which
                                  is what makes this a single store site and
                                  not a systemic offset.
                                  VISIBLE: on the new text layer the table's
                                  letters draw black-and-white striped, because
                                  pixel value 1 reads entry 0 instead of the
                                  light shade.
                                  CANDIDATE: the site that copies the game's
                                  WRAM palette block (the 0xFF9000 region, the
                                  PAL_DIRTY_SITES list) to 0x840010.. for pals
                                  1-5 lands at +2 bytes -- a pre/post-increment
                                  or an access-width slip in ONE decompiled
                                  loop. That is the port's known silent-wrong-
                                  value class; tools/cport_widths.py ranks
                                  exactly this.
                                  CHECK-DEMO CANNOT SEE IT: it diffs work RAM,
                                  not palette RAM. Any fix needs the palette
                                  compared against the arcade at a matched
                                  frame, which is how it was found.

    the port holds the WRONG       NEW 2026-10-15, oracle-verified, PORT side
    TEXT at the high-score table   and not the renderer: "the renderer draws
                                   what the port holds". At f2200 our text RAM
                                   differs from the arcade's at 38 of 2048
                                   words (MAME wram_dump.lua WD_ADDR=0x410000
                                   at arcade 2202, against the port's copy at
                                   0x26026000): a '2ND' entry, cursor marks on
                                   every other row, and '2 CREDITS 1 PLAY' at
                                   row 24 where the ARCADE HAS 0 CREDITS.
                                   THE CREDIT COUNT IS THE LEAD. The arcade
                                   with no coins inserted shows 0; ours shows
                                   2. That is not a text bug, it is the port's
                                   coin state, so look at the i8751 MCU shim's
                                   coin mailbox (md_main.c, "Stage B:
                                   replicate the i8751 MCU") and at the nine
                                   I/O cells port_play_tick seeds at boot
                                   (port_l2.c: the dips are 0x54 / 0x66 and
                                   everything else 0xFF = nothing pressed). A
                                   coin line stuck active reads as credits.
                                   The '2ND' entry and the cursor marks are
                                   plausibly downstream of the same thing -- a
                                   table in a different state -- but that is a
                                   guess, not a finding.
                                   ALSO AT THAT FRAME: the palette shadow at
                                   0x26027000 matches the arcade except 55
                                   words in entries 8-23, a FADE PHASE at
                                   offset -2. Probably timing rather than
                                   content, like the 34 text cells at f702;
                                   do not chase it with the two palette
                                   defects below until the phase is excluded.

    logo text is WHITE, not RED   NEW 2026-10-15, same oracle (arcade frame
                                  702 = ours 700). The text palettes match the
                                  arcade word for word EXCEPT pal 6 colour 6:
                                  arcade 0x100f, ours 0x7fff. The attract's
                                  "ALTERED BEAST" logo (72 cells, all pal 6,
                                  rows 4-9) is drawn with pixel value 6, so the
                                  arcade shows it RED and ours WHITE. A single
                                  wrong word, not a shift, so a different store
                                  site from the pals 2/5 one.
                                  AND PAL 6 IS CORRECT AT FRAME 2502 AND WRONG
                                  AT 702. That is the constraint worth keeping:
                                  it is not "pal 6's writer is broken", it is
                                  scene- or value-specific, so look for a site
                                  that runs in the attract and not at the table
                                  (or one whose wrongness depends on the value
                                  being written).
                                  MECHANISM, SETTLED: a MISSED WRITE, not a
                                  shift and not an overrun. (An overrun was
                                  hypothesised here and is DISPROVED by the
                                  same dump: arcade pal 6 at 702 is
                                    ffff 30ff 30be 309c 307a ffff 100f 0000
                                  so colour 5 is 0xffff and ours at colour 6 is
                                  0x7fff, which no carry of the previous word
                                  produces.) 0x7fff IS pal 6 colour 6's value
                                  at frame 2502, where ours matched. So between
                                  the table and the logo scene the ARCADE
                                  REWROTE entry 54 from 0x7fff to 0x100f -- the
                                  logo's colour cycle -- AND THE PORT DID NOT.
                                  Ours is the stale earlier value.
                                  SEARCH WINDOW: the cycler runs during the
                                  level attract, arcade frames ~465-1000, and
                                  nowhere near the table.
                                  SO THERE ARE THREE SHAPES, not one family:
                                  a one-word SHIFT (pals 2/5) and a MISSED
                                  single-word WRITE (pal 6 colour 6).
                                  Everything else at that frame agrees: pals
                                  0-5 and 7, and 1,086 of 1,120 text cells. The
                                  34 that differ are the score digits and a
                                  blinking "1 OR 2 PLAYERS" line -- timing, not
                                  content.

    gameplay HUD dropout          FIXED by dropping the TAGKEEP family
                                  (Mike's G-1 verdict 2026-09-17).
                                  Still on the LINE. NEW 2026-09-17, not in any
                                  earlier list though frame_grade.py has
                                  had a detector for it since
                                  2026-08-26. The line drops the lives
                                  portrait, the gold "x2" and the 50000
                                  high score at f2000/f3000/f4000 under
                                  the level-1 play script. "400" and
                                  "INSERT COIN" -- same blue, same row --
                                  ARE drawn, so it is per-cell, not a
                                  layer. Dropping the TAGKEEP family
                                  restores all three (G-1, with Mike).
                                  MECHANISM: NOT the colour-set
                                  allocator -- ruled out by census and
                                  by the permanent group-0 pin at
                                  m_main.c:4132. Look at the text path.
    black tiles, 4.8% of cells    MECHANISM IS RESIDENCY, not stale
                                  tags (rig census 2026-09-17, 20/20
                                  frames decoded, 20 distinct md5s):
                                    noslot  mean 1.20  no way claimed
                                    dirty   mean 1.20  claimed, art
                                                       not shipped yet
                                    cut     mean 0.15  dead code
                                  Both live paths mean THE ART IS NOT
                                  IN VRAM YET when the cell is drawn.
                                  NOT palette, NOT pens, NOT the
                                  allocator, NOT stale tags -- the
                                  earlier "slot reference outliving
                                  its pen map" reading is RETIRED.
                                  So this defect is DOWNSTREAM OF EMIT
                                  THROUGHPUT: it is the same card as
                                  BAKED TILE TRANSPORT below, and the
                                  black-cell count is the honest
                                  instrument for that card.
    ATTRACT CLEANUP 2026-09-22    Mike's three rig captures on eye2:
    (evening)                     (1) eye pupil notch (three iris cells
                                  showing iris where the pupil sprite is):
                                  FIXED in eye3 -- the runtime cat-1 mask
                                  cache (64 slots by code & 63) thrashed on
                                  the harvested scene; class-2 cells now own
                                  a mask slot; C1MASKTAB=1 joined LINE_FLAGS
                                  (C1_STAMP assumed it). ares 223 -> 210
                                  cells, rig 3/3.
                                  (2) transformation cut: black/stipple
                                  around the flames = scene 9's blue (set
                                  19, colour-cycling) OVERFLOWED the bake,
                                  keyed on the cut bit ~15 frames before the
                                  page switch: FIXED in eye4 (keyed on
                                  TEXT_C[0x740] == 0xAAAA, harvested after
                                  the switch, packed alone, per-pixel keys):
                                  f1580 299 -> 14 cells. The ~30-frame
                                  transition at the switch remains (switch-
                                  in-view class). Rig pending.
                                  (3) the pan to the beast eye (9-12 px a
                                  frame): stale entering columns FIXED by
                                  full-row shipping (64-cell plane rows on
                                  fast scrolls and harvested scenes, 24
                                  columns of lead); the remaining black
                                  cells are art requested at the row's next
                                  visit (rotation 9 windows) -- transport
                                  floor. Windows/frame measured 1.00 (0.50-
                                  0.88 across the game's page switch).
                                  (4) eye5: scene 5's harvest frames moved
                                  inside the mid splash (4530-4600 were
                                  level 2's demo: sets 64-73 in the splash,
                                  65/66 overflowed) -- overflow gone, 18/19.
                                  (5b) eye7/eye9: the cut bit no longer
                                  disables the baked scan or stops the
                                  glow (keyed on the cut pages) -- harmless
                                  but NOT the cut+10 count: by cut+10 the
                                  pages are already 10/11 (LESSONS "cut+10
                                  is already the switch"); the pre-switch
                                  window is ~10 frames.
                                  (5) eye6: the OFF state (pictures) keyed
                                  on the cut pages like scene 9, so the
                                  level stays intact until the switch (rig
                                  capture: black bottom band during the
                                  transformation start; ares cut+10 had
                                  901 dirty FG cells). Rig pending.
    transformation screen         zigzag + ornaments DRAW under
                                  CHEVFIX=1 (not on the line).
                                  Colours wrong: 3 distinct against
                                  an expected 7.
                                  MOVED 2026-09-19 on mdb48 (MDBATCH=48,
                                  NO CHEVFIX): 3 -> 4 distinct. The new
                                  one is a second blue (0,0,206 beside
                                  0,0,174) and it is the ZIGZAG, which
                                  had only ever drawn under CHEVFIX.
                                  So the zigzag tiles were never wrong,
                                  they were not ARRIVING -- residency,
                                  fixed by throughput, not by a colour
                                  card. Still 3 short of 7.
                                  2026-09-18 (Mike, rig, tilesmd6/7):
                                  the animating character shows for
                                  exactly ONE FRAME, then only flames
                                  and chevron. NOT caused by the pen
                                  bake -- MDP_LINES is 3, so the tile
                                  tables own MD CRAM lines 1-3 and
                                  MDSPR owns the fourth (m_main.c:595);
                                  no pen the bake allocates can reach a
                                  sprite. Reserving 6 free pens changed
                                  nothing, which is the same evidence.
                                  UNTESTED ON THE LINE -- run bldS and
                                  trigger a transformation. That single
                                  test says regression vs pre-existing
                                  and nothing else should be built for
                                  it until it is done.
                                  BLACK TILES during the flame wipe are
                                  a SEPARATE and now-explained thing:
                                  md_state_on() reports the round OFF
                                  SCREEN through the transformation
                                  (m_main.c:1738, deliberate), so sets
                                  assigned there get their tags wiped
                                  (:6954) and the art must re-ship.
                                  Mike's 02:24:04 frame is full of
                                  black 8x8 holes; 02:24:05 has them
                                  filled. Residency, caught in the act.
    eye screen                    ~50 frames of black from ~f1610
                                  where the arcade shows a picture.
                                  The rom sets the backdrop black
                                  deliberately, so this is EMPTINESS,
                                  not a blackout. Untested against
                                  CHEVFIX.
    ZEUS MESSAGE                  FIXED 2026-09-20 on slim23 (Mike: "fixed
                                  the zeus text"); slim30 marks ALL rows
                                  instead of rows 8-11 (layout, LESSONS),
                                  unverified by Mike. The writer is the object
                                  state routine at 0x56E8: one glyph
                                  every two frames into rows 9/11, then
                                  zeros over the same cells. It was
                                  FM-gated but never MARKED its 4-row
                                  group for TEXTCAPMASK; the mark is in
                                  its thunk now (patch_game.py). The
                                  "leftover glyphs" were the same
                                  defect: lost erase stores.
    shadow-column dither over MD content   untouched

## OPEN CARDS

    (PARKED) R60PLANREMARK PARKED 2026-10-16 WITH ITS NUMBERS, correct but
                          worthless. Re-mark instead of wipe at a plan
                          change, committed behind a flag, default off,
                          correct after two hole-storm fixes (tag map ids
                          remapped, old pins released). NO VISIBLE GAIN: the
                          face transition (f1045-1056, no game blank) reads
                          781 stale cells and luma 60 -> 51 on BOTH sw108 and
                          sw110. Eye 9.5 against 9.4.
                          THE CAUSE IT RULED OUT IS THE VALUE: the stale
                          cells are THE WALKER'S FILL LATENCY after a scene
                          change, not the plan. That is the next card on this
                          symptom, and it is a different mechanism.

    LOAD PACING            LANDED 2026-10-17 NIGHT, sw207 (merge of the
    (the blank)            builder's site table with the advisor's hold
                          fill). Black runs, against the arcade:
                              cut            arcade  sw206  sw207
                              title->demo      21      32     30
                              face->eye         4      23     15
                              eye->demo 2      21      27     31
                          sw206 was the yield clock; sw207 is the measured
                          site table ([3]=[4]=21 vints on R60_DIAG[0]).
                          THE GAME'S HALF IS DONE AND IS NOT THE RESIDUAL.
                          The game now blanks 21 at the loads and 3 at
                          face->eye, which is the arcade's shape. What is
                          left is OURS AFTER THE ENABLE: ~9-10 frames of
                          fill at the loads and ~12 at face->eye.
                          THE REMAINING GAP IS A BUDGET QUESTION, and it is
                          Mike's call rather than a build. Inside the
                          arcade's own 21 the pages land ~8 frames in, so
                          there are ~13 frames of cover available, and the
                          fill runs ~2.5 frames a window (master walk-bound)
                          = about five windows. The arcade finishes its
                          picture inside its 21; we need 21 + 10. So either
                          the fill gets faster, or the blank is held LONGER
                          THAN THE ARCADE'S -- which buys a clean cut at the
                          cost of the fidelity the card was for. Do not
                          quietly pick the second one.
                          Still ungated: the boundary lead to ~0, which is
                          the scene timer against the arcade ACROSS
                          boundaries. The blank length is the symptom; that
                          is the test.

    (HISTORY) THE CARD AS APPROVED, 2026-10-17: "If the arcade 68k
    (the blank)            stays blank for en extended period of frames we
                          should honor it too." DESIGN COSTED AND SITE
                          FOUND; NOT BUILT, awaiting his pick between the
                          two shapes.
                          THE DEFECT: the arcade blanks 21 frames at the
                          title and eye loads, we blank 4, because our load
                          loop is work-paced and the work runs ~5x faster on
                          the SH-2. face->eye is 4 on both and needs nothing.
                          THE DESIGN POINT THAT MATTERS MORE THAN THE CHOICE:
                          hold by BLOCKING, not by gating the display. On the
                          arcade the main loop is suspended inside the load,
                          which is why its scene timer PARKS. A display-only
                          hold blackens the screen and leaves the timer lead
                          exactly where it is. Blocking (port_frame_wait from
                          inside the load's own call stack, which PORT_CORO
                          suspends in place while the driver and renderer
                          keep running) fixes the blank, the scene-timer
                          lead, the emitter phase and attract_parity's
                          late-row alignment in one change.
                          THE SITE, found 2026-10-17: the blank is the game's
                          own VIDEO_EN shadow 0xFFF018 bit 5, pushed to
                          0xC40001 by the vint (altbeast_port.c:3978).
                          `DAT_fffff018 & 0xdf` is BLANK ON and
                          `DAT_fffff018 | 0x20` is BLANK OFF, inline in the
                          scene-step functions (on at :145, off at :187, with
                          the step's frame countdown at :189/:200). 38 writes
                          in all. The hook is the blank-OFF site: if fewer
                          than N frames have passed since blank-on, block the
                          difference.
                          NOT a blanket minimum -- that would wrongly stretch
                          face->eye from 4 to 21. It needs the two-entry
                          table (the two work-paced loads), which is why
                          option B was recommended over a cycle budget.
                          THE GATE IS THE TIMER, NOT THE BLANK: 0xFFF02A at
                          SDRAM 0x2FC2A must PARK through the load as the
                          arcade's does and the boundary lead must go to ~0.
                          The blank length is the symptom.

    THE SPLIT CONSUME      OPEN, 2026-10-15, and it is a CADENCE card, not a
                          cut card. The 68K yields at a V threshold and
                          resumes next vblank, with the slave's copy deferred
                          until the magic clears. It buys windows at 1/vint
                          during scrolling fills; it does NOT shorten the cut,
                          because the cut is bounded by the packet's word
                          count at ~18 words a 68K line (see LESSONS).

    (CLOSED) THE 68K'S     CLOSED 2026-10-15, BOTH SHAPES DEAD, measured. The
    PER-RECORD CONSUME     card was: cap the records per window, or stage them
                          to WRAM before the DMA.
                          CAP (sw82 = 12 a plane, sw83 = 20; sw82_hd_N /
                          sw83_hd_N / sw8x_wr_N): windows stay 0.5/vint at
                          every cap, the consume still ends past the vint, and
                          the scene becomes whole LATER -- sw83 at 522, sw82
                          not by 525, against sw80's 500. The ROWS are the
                          bulk (14 full rows = 588 words) and the cap only
                          touches art.
                          STAGE TO WRAM (sw84, md_main.c probe, reverted):
                          MEASURED DEAD. The 68K's copy loop out of the
                          framebuffer is slower than the DMA it replaces --
                          consume end line 107/72 of the next frame against
                          53, and 36 fewer flips by f553 (483 against 519),
                          because every NORMAL frame pays the copy too.
                          I recommended trying staging first, on the grounds
                          that a cap trades picture completeness for cadence
                          and staging does not. The reasoning was sound and
                          the answer was wrong: the copy costs more than the
                          transfer it removes.

    THE 68K'S PER-RECORD   SUPERSEDED BY THE TWO ABOVE. The
    CONSUME COST          68K's consume of a full packet runs ~90 lines (line
                          226 to line 53-59 of the next frame), so the FM=0
                          span crosses the vint and windows run at 0.5/vint
                          during a display hold -- which is the 34-45 frames
                          we are late at every cut. It costs ~1 68K line per
                          inline art record and ~1.5 per full row whether the
                          display is on or off.
                          TWO SHAPES: stage the records to WRAM before the
                          DMA, as slim_fetch/slim_dma did, or cap records per
                          window at ~24 total so the consume stays inside
                          vblank and windows run 1/vint.
                          THE OLD PIPELINE ALREADY KNEW: MD_BATCH 12-24,
                          section 4 of PLANES-RULES-INVENTORY.md. The
                          inventory predicted this limit before it was hit,
                          which is the clearest argument yet for reading it
                          first.

    RE-MARK ON PLAN       PARKED WITH NUMBERS, 2026-10-15 (advisor, sw71, NOT
    ADOPTION (sw71)       the line). Re-mark instead of wipe when a plan is
                          adopted -- the old feed's rule at m_main.c:3664-3687.
                          Through the demo cut it HELPS: luma against the
                          arcade at f480/495/520 goes 65.7/75.5/49.1 (sw69) ->
                          57.9/47.0/52.8. The EYE at f1268 REGRESSES 9.2 ->
                          30.1, because a re-marked slot keeps its old art
                          under the new pens until its row's turn comes round,
                          and a plan that moves a set to a different line
                          leaves its cells' line bits stale for a whole sweep.
                          DO NOT RE-PROPOSE IT AS A WIN: it is a trade, and
                          the eye is the scene Mike looks at. Any revival has
                          to fix the stale-line-bit half, not just re-measure
                          the cut.

    THE POST-CUT FILL     OPEN, 2026-10-15. The demo scene reads ~100 luma
                          against the arcade for 60 frames after the cut,
                          because every plan adopted during the fade-in
                          flushes. sw71 above was the first attempt at it and
                          is parked. The cost is bounded and visible (60
                          frames, one scene) so it is a card, not a defect.

    BUILD DIR OUTPUTS     Mike, 2026-10-04: "generated files write under build/, not
                          into sh_src/ or md_src/". Today the build regenerates
                          TRACKED files in the source trees and some tools write
                          beside their own path; the shadow tree (tools/
                          shadow_tree.sh, `make port`) copies sh_src/ md_src/ tools/
                          to keep an out-of-tree build from writing through
                          symlinks. It was added for sh_src/portfull/* (cport_prep
                          and cport_layout write the unit and layout.ld there),
                          md_src/game_irq.h (the patcher) and sh_src/tiles.bin (the
                          art baker), plus tools/patch_report.txt. The shadow stays
                          until this card lands. Not built.
    OTHER-FORM ATTACKS    2026-10-04: the hero's attack lookups for the other beast
                          forms (prog68k 0x5062/0x7F58/0x8D66/0xBBC6/0xC9B4/0xAF4A)
                          return a handler in A0 with the carry, like 0xA3B0, and
                          are dispatched by 40 `jmp (a0)` sites Ghidra could not
                          follow (80-100 call sites each with a `bVar = false`
                          placeholder; tools/ghidra/make_jumptables.py lists them as
                          SKIP). Gameplay, not attract. Same treatment as 0xA3B0:
                          make_hero_attacks.py for the targets + a hand-derived
                          lookup + the call-site rewrite in cport_prep.py.
    A6 PUBLICATION        2026-10-04: 66 `movea ..,fp ; jsr` sites in prog68k (19 of
                          them 0x3B2E) run the callee on the dispatcher's A6 in the
                          port; two are hand passes (FUN_0000f74a, FUN_0000a1e0).
                          The general pass: from the disassembly, for each fp move
                          followed by a call, publish `_r_unaff_A6 = <the local
                          Ghidra holds it in>` before the C call (areg_locals.json
                          names the local when it is unambiguous).
    BATTERY SAVE          Mike, 2026-10-04 10:15: "we should build our own
    (high-score table)    battery save for high scores"; parked as a card
                          at his word so it does not derail the port.
                          WHAT THE BOARD DOES: System 16B work RAM is
                          battery-backed; MAME models it as NVRAM over
                          the WHOLE 16 KB work RAM (jts16-ref
                          segas16b.cpp 9317 `m_nvram->set_base(m_workram)`)
                          and this repo's oracle runs carry
                          nvram/altbeast/nvram across sessions -- that
                          is why the arcade shows 412800 x7 where the
                          ROM default (prog68k 0x4732, copied by
                          FUN_00004178) is 50000/40000/.../4000 with
                          names HKR UCH SAT HAG HAS TOS TAK.
                          THE DATA: 0xFFFC00-0xFFFC37 = 7 entries x 8
                          bytes (BCD score long, round byte, 3 name
                          chars); 0xFFFC38 = the CRC word
                          (FUN_000041b2: poly 0x8810 over 0x1C words,
                          A0 = table); 0xFFFC3A-3F = the candidate
                          entry FUN_00004196 posts (0x419E). The boot
                          path FUN_00004178 validates the CRC and
                          falls back to the ROM table; a restore that
                          lands the 0x40 bytes BEFORE FUN_00004178 runs
                          is accepted by the game's own check, no
                          patching.
                          THE 32X MECHANISM (derived from Doom 32X
                          Resurrection, ~/src/32x-builder/srcref/d32xr,
                          not copied): the SH-2 cannot reach cart SRAM;
                          the 68K does, at 0x200000 ODD bytes with
                          0xA130F1 = 3 (read), 1 (write), 2 (off,
                          write-protected), inside sh2_wait/set_rv
                          (crt0.s 565-660). Protocol: COMM2 = offset,
                          COMM0 = 0x0100 read -> COMM2 low byte;
                          COMM0 = 0x0200|byte write; 68K clears COMM0
                          when done (marshw.c 352-376). 64 bytes =
                          64 handshakes, boot-time only.
                          OUR HOOKS: restore in port_l2.c's PORT_BOOT
                          block (where the dips are seeded, ~2040)
                          into WRAM_BASE+0x3C00; save when the table
                          changes -- FUN_00004196's copy (0x4196,
                          called after name entry) or a per-attract
                          compare of the 0x40 bytes against the last
                          saved copy. 68K side: a COMM0 command pair
                          in md_main.c's loop beside the 0x2020 post.
                          Everdrive/MegaSD mappers differ (d32xr
                          rd_med_sram/rd_msd_sram); the MiSTer core
                          and ares both model the standard mapper.
                          GATE: the oracle for parity runs must then
                          be MAME with a FRESH nvram directory
                          (`-nvram_directory <empty dir>`), or the
                          port's SRAM seeded with the oracle's table.

    SCENE ANCHORING       SOLVED for captures, still open for the rig.
                          The anchor is the 68K scene timer WRAM
                          0xFFF02A (one tick per GAME frame), already
                          dumped on every gameplay_speed run and
                          already stored in night_run's `timers`.
                          Two captures are comparable iff their
                          interpolated game frames match -- rom SIZE
                          predicts nothing. O-1's 6x cold-run variance
                          is a RIG problem and is NOT addressed by
                          this.
    drop the TAGKEEP      BUILT (rom/night/notag1.32x) and BLOCKED.
    FAMILY                Not one flag: PEN_REPAINT and PEN_HOLD@16264
                          are nested inside TAGKEEP and die with it,
                          but PEN_HOLD@2622 survives as a pen leak, so
                          the family goes together. It had NEVER
                          COMPILED -- mdp_wipe_set_tags was defined
                          inside #ifdef TAGKEEP and called from
                          mdp_assign_set outside it (fixed 2026-09-16,
                          line build proven identical).
                          black_pct 4.6/4.6/4.1 -> 4.6/4.6/4.2, but
                          that is NOT an answer: it removes 1408 B of
                          .bss, and frame-indexed captures of two roms
                          of different size are different content.
    NTSKIP correctness    per-set push key: fold mdp_s_stmp for the
                          sets a row HOLDS, not allocator state per
                          row. 665 assigns / 800 frames means most of
                          the 84.7% survives. NOT a speed card any
                          more -- the rig says flat -- but the key bug
                          is real and NTKEY8's +24.6 points is real.
    transport ablation    the live axis. Design it against the 6x
                          variance first.
    BATCH CAP / PACKET    MEASURED 2026-09-19, and it is the
    WALL                  throughput lever. DMACENSUS=1, ares, 3000
                          frames of level 1:
                            MDBATCH=24 (line)   8.82 tiles/vint
                            MDBATCH=48         18.61
                            MDBATCH=64         18.61  saturated
                            MDBATCH=96          4.93  COLLAPSES
                            MDBATCH=160         5.22  COLLAPSES
                          8.8 was never demand -- it was the cap. The
                          collapse past 64 is the 688-word packet body
                          overflowing: at 17 words a record only ~40
                          fit.
                          BLACK TILES ARE RESIDENCY, so throughput IS
                          the defect, and this measures the 2-word
                          record directly: 688 words holds ~40 records
                          now and ~344 at 2 words, at which point the
                          limit becomes the 68K's VBLANK budget --
                          ~90 tiles/vint at ~200 cycles/tile against
                          ~18,500 cycles of NTSC vblank. Today's 8.8
                          would cost 10% of vblank to CPU-copy, 18.6
                          costs 20%. The 68K can afford it.
                          CART DMA IS NOT AVAILABLE for this (see
                          LESSONS): the 68K must CPU-copy from cart,
                          which mdspr_upload already does for sprite
                          art. Read srcref for how commercial titles
                          pace a bulk upload before picking a batch.
                          rom/night/mdb48.32x = the free half, one
                          constant, 2.1x. Anchored gate at game frame
                          1124: 138032/343845 px differ and the diff is
                          MORE ART (sky band largely gone, background
                          more complete, several stray glyphs absent);
                          reaches that game frame at emulator 1998 vs
                          the line's 2005. ON THE RIG, awaiting Mike.
    BAKED TILE            LIVE. md_emit_art's per-tile conversion is
    TRANSPORT             baked to ROM (TILESMD=1, 425KB at 0x263C00,
                          byte-identical on 941/941 live VRAM slots).
                          Rig verdict on the BYTE-width build
                          (tilesmd.32x, Mike 2026-09-17): "slightly.
                          but nothing significant" + "the Neff battle
                          feels pretty good".
                          WHY IT WAS ONLY SLIGHT: the bake removed the
                          ARITHMETIC and left the transport at BYTE
                          width -- 32 byte reads + 32 byte writes per
                          tile through a volatile pointer the compiler
                          cannot merge. The plan asks for a cart->VRAM
                          move; what shipped was a byte-copy loop.
                          NOW: halfword, 64 touches -> 32 (41b4dfc),
                          gated against the byte build at VRAM 0 of
                          65536 and pixels 0. rom/night/tilesmd2.32x,
                          ON THE RIG 23:34, awaiting the A/B against
                          the byte build Mike already played.
                          NEXT if it moves: 4-byte moves take it to 8,
                          but the art field must be 4-aligned and the
                          packet stride is 17 words, so alignment
                          alternates -- a packet_fmt card.
                          DO NOT read a flat result as "the bake is
                          the wrong direction": the recorded frame
                          threshold law says cuts under ~50 lines buy
                          nothing, so a real sub-threshold win reads
                          as flat until enough of them stack.
                          CAVEAT held: tilesmd runs ONE GAME FRAME
                          offset from the line. Constant, not
                          drifting. Mike accepted the tolerance.

## INSTRUMENT STATUS

    sw152's POOL PASS     UNPROVEN AND CARRYING NO SPEED CLAIM, 2026-10-17.
                          The 31-entry text-pool pass should stop a
                          pool-dirty pass forcing the 16-bank compare, so
                          R60CMSTATS[2] would read ~1.6 every pass instead of
                          16 on a third of them. THAT IS READ FROM THE CODE,
                          NOT READ BACK -- the stats probe was not rebuilt on
                          sw152. What IS measured: the ring's commit interval
                          is 7 lines on sw151 AND sw152, and the demo is 74
                          passes per 100 vints on both. So the change shows
                          NOTHING in the numbers; it removes 256 uncached
                          reads on a third of passes and that is either below
                          the resolution floor or not firing, and the two are
                          not distinguishable from here.
                          DO NOT ATTRIBUTE A SPEED NUMBER TO IT. If sw152
                          becomes the line, run R60CMSTATS=1 on its flags and
                          read [2]/[0] at 600-700 first. The advisor flagged
                          the gap himself rather than letting the number ride.

    temporal_metric SPANS    NEEDS TIMER ALIGNMENT, 2026-10-17, same as
                          attract_parity. The wolf-span flicker read 36.8k
                          against the arcade's 27.8k, +32%, and the split
                          showed why: WE RUN ~8 FRAMES AHEAD there, so a
                          window matched on FRAME NUMBERS (1700-1760 against
                          1702-1762) catches three whole-logo flips where the
                          arcade's catches two, and puts the rising gargoyle
                          statues inside our window and not theirs. Content
                          phase, not a renderer defect -- confirmed frame by
                          frame, and the "text row" reverts were the
                          scrolling bricks beside the glyphs, not the text.
                          SO: on any span where the content moves, align the
                          two windows on the GAME'S OWN TIMER (0xFFF02A at
                          SDRAM 0x2FC2A) before comparing, or the number
                          carries the timing lead. The static-title span is
                          immune and is where sw149 is checked (2/59 against
                          the arcade's 1/59).
                          THIRD PLACE THE LEAD HAS NOW TURNED UP: scene
                          boundaries ~10 frames early, the emitter 13-14
                          frames ahead, this span ~8. It has corrupted
                          attract_parity's late rows and now temporal_metric's
                          moving spans. It is one defect and it is taxing
                          every instrument we own.

    tools/temporal_metric.py  NEW 2026-10-16 (advisor), and it closes the gap
                          that had decided two builds blind. N consecutive
                          ares screenshots over a span; duplicates collapsed
                          to the DISTINCT PRESENTED sequence first (without
                          that, a slower-presenting build scores as less
                          flickery for its cadence alone); per transition the
                          changed-pixel count; FLICKER = pixels that revert
                          within two presented frames; CADENCE = the
                          distinct-frame pattern and the variance of the
                          inter-flip intervals. --arcade for the corpus
                          baseline, --heatmap for where.
                          READ IT AS DISTANCE FROM THE ARCADE, NEVER AS
                          "more even is better". At the static title the
                          arcade presents 6 distinct frames in 59; sw81 33
                          (uneven, cv 0.71); sw108 59 -- every frame changes.
                          sw108 is the EVENEST and the FURTHEST from the
                          arcade on that span. A one-sided evenness score
                          would have called it perfect. Print the arcade
                          column beside ours on every span.
                          FIRST RUN FOUND A DEFECT IN THE LINE that every
                          still missed: eye span 1250-1310, p50 reverts
                          arcade 224 / sw81 224 / sw108 293, and the heatmap
                          puts sw108's excess on the bottom text row, which
                          alternates 990/847 pixels every frame from 1277.
                          Mechanism (read from code, not measured): step A's
                          clear phase runs BEFORE the master's text list is
                          taken, so the bank that takes a changed cell draws
                          the new glyph over the old while the other bank
                          clears first, and the two alternate at 60 Hz.
                          sw111 is the fix attempt.
                          ANSWERED 2026-10-16: THEY WERE ONE PHENOMENON.
                          sw111 (the text fix) took the title back to 33/59
                          at cv 0.58, from sw108's 59/59. So sw108's
                          "evenness" WAS the text alternation, and what Mike
                          named as cleaner presentation corresponded to being
                          six times further from the arcade's change rate.
                          Full table, distinct presented frames and flicker
                          total, arcade column first:
                            title 280-340   arcade  6/59        14101-era
                                            sw81   33/59 cv0.71
                                            sw108  59/59
                                            sw111  33/59 cv0.58
                            eye 1250-1310   arcade 14101  sw81 16011
                                            sw108  20373  sw111 16157
                            demo 600-660    arcade 59/59 19847
                                            sw81   8914
                                            sw111  30/59 cv0.09  9395
                          READ sw111 HONESTLY: on the eye span it is sw81,
                          not better (16157 vs 16011, both well above the
                          arcade's 14101). What it does is UNDO sw108's
                          regression and keep a cadence gain (cv 0.58 against
                          0.71). It is not a flicker fix.

                          **OUR TEMPORAL ERROR RUNS IN BOTH DIRECTIONS, AND
                          THAT IS THE FINDING UNDER THE TABLE.** At the title
                          the arcade presents 6 distinct frames in 59 and we
                          present 33 -- we change 5.5x TOO OFTEN on a screen
                          it barely redraws. In the demo the arcade presents
                          59 and we present 30 -- we change HALF as often as
                          it does. The renderer is insensitive to whether
                          content changed: it does the same work either way.
                          AND THE TITLE HALF HAS A NAMED MECHANISM ALREADY.
                          A static screen can only produce 33 distinct frames
                          if OUR OWN OUTPUT differs pass to pass, and sw108's
                          text row was exactly that -- the two banks not
                          agreeing, one drawing over the old glyph while the
                          other cleared first. sw111 fixed one instance.
                          33 against 6 says THERE ARE MORE, and the heatmap
                          over a static span finds them the same way it found
                          the text row. That is the next card and it is
                          cheap: no new instrument, one run per span.

    BlastEm fork          PRE-r60 ROMS ONLY, 2026-10-16. The fork (16d3d03)
                          runs rom/port/marks2.32x headless fine, but every
                          r60 rom (sw81/sw108/sw109) dies at startup with
                          "Unimplemented instruction: 0, pc=0", exit 1, no
                          dump, BIOS present. So "BlastEm, wait N" cannot say
                          anything about r60 and the fill's hardware cost is
                          rig-only after all. Fixing the fork is its own card.

    attract_parity, the      2026-10-16. ALIGN ON THE GAME'S OWN TIMER
    TIMER DRIFT              (0xFFF02A) FOR ANYTHING PAST THE FIRST DEMO.
                          attract_parity aligns the entire run on a SINGLE
                          frame offset (-2). Our timer runs ahead of the
                          arcade's -- we tick every vint, the arcade drops
                          ticks under load -- so by timer 0x12D we are at our
                          frame 2955 against the arcade's 3002, 47 frames,
                          ~1.6%. A single offset cannot hold across that, so
                          the late rows measure DRIFT, not lag. This is the
                          night-sweep alignment trap in a new place: match the
                          timer, not the frame number.

    R60_PH (FRT phases)   NEW 2026-10-15, the block at 0x26028FC0. Per-phase
                          FRT timers for the slave pass. It is the ONLY
                          instrument that ranks framebuffer-bound code
                          correctly: ares --profile put the plane fill at 3%
                          of cycles while R60_PH read the same fill at 0.4-0.9
                          VINT a pass (the profile charges instruction cycles
                          only). Demo sprites read 0.9 vint a pass, 3.8 ticks
                          a pixel; fill 0.4-0.9; text 0.3.
                          The FRT is 16-BIT AND WRAPS AT 356 LINES.

    attract_parity        READ IT THIS WAY (2026-10-15, after getting it
                          wrong once): the number is the MEAN |LUMA|
                          DIFFERENCE between our frame and the arcade's, so 0
                          is identical and LOWER IS BETTER. Each row is one
                          arcade scene; the columns are OUR frame offset after
                          that scene's reference frame, lags 0..90. THE COLUMN
                          WHERE THE NUMBER COLLAPSES IS HOW MANY FRAMES WE
                          TRAIL at that scene. A row that never collapses is
                          not "stable", it means we never match it at all --
                          which is why "collapses at lag 45" can be a WIN.
                          AND ON A MOVING SCENE THE EARLY LAG COLUMNS ARE
                          MEANINGLESS. 2026-10-16, the eye-pan row read 54/44
                          at lags 0-3 against another build's 9/9 while BOTH
                          reached 7-10 by lag 45. That is the scene panning
                          between the two frames being compared, not a
                          difference between the builds. Read the collapse,
                          never the head of a moving row.
                          against a build that never collapsed.
                          And a scene's FLOOR is its own content error: the
                          demo scene floors at 30 because of the logo colour
                          phase and the credits text, both open defects.


    BlastEm              THIRD INSTRUMENT, SCOPED (O-8, 2026-09-23 evening,
                         BLASTEM.md 9). Fork github.com/mholzinger/blastem
                         main, tree ~/src/blastem-0c61d0d95463. Fixes:
                         sh2_reset prefetch, headless -b real frames, 68K
                         never stalls under FM (the picture-mismatch bug).
                         Knob BLASTEM_FB_WAIT (SH-2 clocks per 16-bit FB
                         write): 3 = ares's stall model, 11 = the rig's
                         1.6x blit rate, held on three spans. Quote as
                         "BlastEm, wait N". --dump sdram|wram; --trace-
                         comm/flip/dreq (ares columns); BLASTEM_BUS_ARB
                         cart-ROM arbiter from IF.sv (negligible on the
                         line rom, BLASTEM.md 11); DUMPSTAT, FBXSTAT,
                         M68KSTAT, ARBSTAT at exit. The rig's 1.6x is a
                         BLIT-rate figure, not FB-write (BLASTEM.md 10).
                         Rig launches still decide.

    RIG FRAME CAPTURE     THERE IS NONE. Measured 2026-09-19:
                            /dev/MiSTer_cmd tight loop: accepts 769
                              req/s and COALESCES -- 30 requests in
                              39ms produced exactly ONE file.
                            /dev/MiSTer_cmd paced (wait for the file):
                              ~1 frame per 6.8 SECONDS.
                            /dev/fb0: reads at ~36fps, 960x540x32, but
                              it is the OSD layer -- 99.8% black, it
                              does NOT carry core video.
                          CONSEQUENCE, and it is a design rule for every
                          rig probe: A PROBE MUST NOT FLASH. Latch the
                          result in the ROM and hold a STABLE colour, so
                          one slow screenshot captures an answer
                          accumulated over many frames. Five cuts of
                          CARTDMAPROBE were unreadable because they
                          showed a per-frame value and the only way to
                          see it was Mike filming the CRT in slow-mo --
                          which then raised the fair question of whether
                          the camera was inventing the colours.
                          /media/fat/burst.sh on the rig carries these
                          numbers in its header.

    LEVEL-START RACE      CLOSED 2026-09-21 06:45 (LESSONS "lost boot-storm
                          packet" + its 06:30 correction). The master's
                          pre-flip lift overlapped a 68K blast in
                          progress; the finished packet sat in the
                          displayed bank until the master's fill erased
                          it; the two-deep BAD1 belt had rotated past
                          it. FBXECHO=1 (on the line): the master echoes
                          its lifted sequence in packet word 5 bits
                          12-15, the 68K re-blasts an unechoed packet
                          from partb_hook two vints after its blast.
                          Rig: pad-5 0/6 -> 3/3, line 3/3; ares gate
                          PASS on eight layouts. Mike's pass pending.

    TRANSPORT FLOOR       MEASURED 2026-09-21 (LESSONS "The transport floor,
                          measured" + the two dead levers). ART_TAIL on
                          the line took the eye's hold 23 -> 9 vints.
                          Dead: compose skip, flip skip, page budget --
                          the load is bound by the game's own gated
                          tilemap writes and the walk's rotation.
                          BUILT 2026-09-22 20:45 as CUTPREFETCH=1 (on
                          LINE_FLAGS, rom/night/cut1.32x pending rig):
                          on the cut bit the master claims ways for
                          pages 10/11's tiles (free, else untouched
                          for 32 windows) and ships them under scene
                          9's table (md_tag bit 30); mds_install keeps
                          those slots clean and no longer re-marks
                          sets with no line in the new table. ares,
                          anchor-relative: dirty at the switch 965 ->
                          16; black cells +10/+15/+20/+25 = 232/36/19/0
                          (eye9: 357/454/496/595, clear by +55); 0
                          cells vs the arcade at +25. LESSONS "The
                          cut's transition is the scene install's
                          re-mark". Remaining ~15 frames = the row
                          rotation. The pan's end (no install, page 0
                          rewritten by the game) is not covered.
                          ORIGINAL DESIGN (not built): PRE-SWITCH WALK. The
                          arcade shows the transformation complete on
                          its first frame because the game writes the
                          cut's pages (10/11) during the level and only
                          switches page pointers. Walk a scene's pages
                          into the SECOND half of each 64x32 name table
                          (rows 28-31 + the unused columns cannot hold
                          it; use plane A/B's alternate 0x2000 tables:
                          VRAM has room for a second pair) while the
                          current scene shows, then switch the VDP's
                          table registers with the game's pointer
                          switch. Cost: a second walker pass per window
                          during play; the tile slots are shared.
                          Gate: cut+1 non-black == arcade's.
                          VRAM CHECK 2026-09-22: plane bases step 8 KB
                          (reg 82/84), so alternates need 0x8000/0xA000
                          -- the MD sprite art lives there (0x8000-
                          0xAE40 for the boss set, md_sprart_info.h)
                          and the free pieces (0xB000 window, 0xD000,
                          0xF400) total 10 KB against its 11.5 KB. The
                          cheaper form: PREFETCH the next scene's art
                          (the cut's sets 19-21 are known and pinned)
                          while the level shows, then on the page-word
                          change walk all 28 rows as full 64-cell rows
                          in 3 windows (28 x 65 words over three 688-
                          word packets) instead of the 9-window
                          rotation: ~4 frames of transition instead of
                          ~30. Measured today: the cut's transition is
                          f1540-1575 (eye4), the pan's end is the same
                          class (pages 0101 -> 0000 with a new page 0).

    60 Hz FLOOR           MEASURED 2026-09-22 21:00-23:00 (LESSONS "The
                          60 Hz floor on the line, measured" + addenda).
                          The game runs a 2-vint frame at 50% with ZERO
                          misses and the transport posts every vint; the
                          frame is ~327 lines because the game's IRQ4
                          waits for the master's FM-held window (45 lines
                          on frames whose sprites did not change, ~110
                          where they did) and its pass then spins 22-26
                          lines in gated stores. BODYPROF (FRT = 45.8
                          ticks/line, NOT 128): the window body on a
                          compose frame = launch 15 + blit/compose half
                          66 + apply_cram 10 + publish 16; the master
                          then idles 130-160 lines. Cuts measured on
                          ares: BLITAUDITDIV=4 (66 -> 58), CRAMISR
                          (apply_cram 10 -> 0, paints at the flip in
                          vblank), PUBNORB (the 2 x 368 FB read-backs
                          -> SDRAM copies, ~8 lines). FM drop on compose
                          frames 124-135 -> 112-120 with the first two;
                          spd1 = cut1 + CRAMISR + BLITAUDITDIV=4 is in
                          the gates/rig. Dead: TEXTCAPOFF (no change),
                          BLITSHIFT=48 (does not move the half: the
                          half is compose-bound, not copy-bound). NEXT:
                          the compose itself (slave in-window + master
                          tail; SPROF stamps in probe8), the launch's
                          15 lines (records from the FB + hash), then
                          the 68K side (consume 17, post-ack tail 15-20).
                          FLOOR (2026-09-23, BLITPROF): the window is
                          FB-write-bound at ~470 B/line aggregate; a
                          compose frame writes ~28 KB -> ~85 lines
                          minimum vs the 68 the threshold needs. LEVERS
                          = BYTES: MD VDP sprite offload (claims 0 on
                          level 1; MDSPR_WHY census running), BLITHASH
                          (content skip), then EARLYREC. MTASKINWIN
                          dead as built (LESSONS).
                          RIG (RIGBLIT, 2026-09-23): the FPGA's FB
                          write rate is ~1.6x slower than ares (blit
                          67-86 lines per compose window on the level-1
                          demo vs 45-47 in ares). Hardware one-vint
                          needs <= ~15 KB FB writes a frame; the MD
                          offload is capped by the single sprite
                          palette line, and a SECOND line is closed by
                          measurement (LESSONS "Two tile palette lines
                          do not hold level 1": 33 distinct colours,
                          3-4 sets overflow at every frame); BLITHASH
                          has no 18 KB home, and its potential is
                          7-13% while walking (dead). REGIME MAP
                          (LESSONS): level 1 already runs one vint per
                          frame outside the zombie fight (f600-1100,
                          f4400-5900); the fight (f1200-4100) runs 50%;
                          71% overall. The heavy pass (~184) needs the
                          window under ~68; it is ~108 there. Next
                          lever: EARLYREC (compose before the vint, the
                          window = blit + publish), priced on the
                          crowded frames and on the FPGA.
                          DESIGN (23:40, not built): PASS-END STRIKE.
                          The game's IRQ4 is 1-11 lines (ISR-exit V
                          ring); the frame is wait-for-ack + ~10 +
                          pass (147 light / 184 heavy). One vint needs
                          the FM window to end by ~68 (heavy) / ~105
                          (light). The window's cost on compose frames
                          is the slave's sprite compose + the chase
                          blit (~66) because records arrive at the
                          vint. Strike the sprite RAM the moment the
                          game's pass ends (a gate thunk at the idle-
                          loop entry 0x903982, GAMEGATE-style) through
                          the DREQ FIFO into SDRAM (the FB is the 68K's
                          at FM=0, but sbuf is SDRAM), compose on both
                          CPUs while FM is still low, and leave the
                          window = blit + publish (~30-45 lines). The
                          master idles 130-160 lines a vint today, so
                          the compose has room before the vint.
                          REFINED 23:50: the sprite records already
                          reach SDRAM through the DREQ FIFO (r60_push
                          at the post -> SPR_LAND); they are final at
                          the END OF THE GAME'S IRQ4 (its upload), ~90
                          lines after vblank, and today they wait until
                          the next vint's post (170 lines idle). EARLY
                          PUSH: call the records push at fmgate_ret
                          (IRQ4 exit; POST_LATE's site, but ONLY the
                          push -- no raise, no window), let the SH-2
                          poll the DMAC landing in its idle loop and
                          launch the compose on landing; the vint's
                          window then only blits + publishes. No FM is
                          needed for any of it (FIFO + SDRAM).
                          CORRECTION 23:55: on the line (FB_XPORT) the
                          records do NOT ride the FIFO -- r60_push and
                          the SPR_LAND landing are #ifndef FB_XPORT; the
                          68K strikes them into FB_SPR at the vint top
                          (FM=0) and the master reads them at FM=1. So
                          an early compose needs the records over the
                          FIFO again (EARLYREC: the FIFO code exists
                          behind the ifdefs; the FIFO's partial-landing
                          hazards are in memory lost-push-belt /
                          ARMGATE) -- or a different split:
                          BUILT 2026-09-23 as EARLYREC=1 (14 iterations;
                          LESSONS "EARLYREC works end to end on ares and
                          gains nothing there"): the transport is proven
                          on ares (30/30 landings and early launches per
                          60 vints, gates 19/19) but the push costs ~24
                          68K lines there, which is the gain. Decides on
                          the FPGA's FIFO rate (unmeasured) -- or on
                          Mike's SPECULATIVE COMPOSE: predict the next
                          frame's records from the last two, compose in
                          the idle time, ship if the real records match
                          at the post; no push at all. Predictor hit-
                          rate study from per-frame sprite-table dumps
                          is the next measurement.
                          CLOSED 2026-09-23 (rig + study): EARLYREC's
                          IRQ4-exit push RUNS on the FPGA once the hook
                          stays masked (rigerec2 boots, barcode reads)
                          and is SLOWER: level-1 demo 31-35% game frames
                          per vint vs the control's 38-42% (same scene
                          step, same-capture pairs); the 68K's FIFO
                          push is the cost on both machines. DEAD as a
                          speed lever. SPECULATIVE COMPOSE (Mike's
                          idea) measured on spd2 sprite tables: walking
                          the whole frame repeats 77% / velocity-
                          predicts 74% (already one-vint there); in the
                          fight the whole frame predicts 0% (2/19
                          identical), per-record velocity hits 83%.
                          Whole-frame speculation buys nothing where
                          the 2-vint frames are; per-record needs a
                          miss-repair path that is compose-order-bound
                          (overlaps) -- not built, Mike's call. Open
                          lever after both: none priced; the byte
                          budget (<= ~15 KB FB writes a frame on the
                          FPGA) is the design question.
                          2026-09-24 (rig, Mike's "go nuts"): the byte
                          premise is WRONG on the FPGA. DRAM census: the
                          fight's 28 KB is the live picture (graves are
                          sprites), erase < 1 KB (auto-fill dead), and
                          15-88% of live rows are unchanged ship to ship.
                          ROWGEN (write-tracked row skip, exists since
                          2026-08-26) is exact on the line (0/184,977
                          rows) and takes 16 lines off the master's
                          blit on the rig -- and ZERO game frames,
                          because RIGBODY shows the FPGA window is the
                          MASTER path (~106 lines demo, slave wait 0):
                          launch ~7, master half = the blit 68, pickup
                          ~5, publish/ship/rearm ~0. ROWGEN lands at
                          ~87; the threshold is ~68. Candidates on the
                          rig now: ROWGEN + ROWGRP (compose-side group
                          mask, dead on ares by RMW cost, re-priced on
                          the FPGA where the visit cost is real) +
                          BLITAUDITDIV=16. Instruments added: RIGWIN,
                          RIGBODY(A/B), RIGGAME 16-bit; RIGSLAVE dead
                          (slave FRT unconfigured). RIGWIN/RIGWIN3
                          (68K-side, 2 launches): the post lands at
                          13-21 lines after the vint on the FPGA (same
                          as ares) and the 68K's release (first gate
                          that found FM up) at ~160-200 after it: the
                          hardware window is 110-150 lines from post
                          to release vs the ~85 one vint needs on the
                          demo; ROWGEN's 16 do not show above the
                          spread. ROWGRP re-tried: worse on the FPGA
                          and purple stale cells on ares (dead twice).
                          Nothing on the line changed; rig holds spd2.
                          FOUND (RIGPICK): the master picks the post up
                          15-70 lines late (post ~17, pickup 28-89) --
                          the idle chunk builders run 50-255 lines on
                          the FPGA and hold the poll. POSTPOLL=1 (knob,
                          off the line: the tile batch and NT rows
                          yield to a waiting post) halves it on half
                          the samples; game rate unchanged (launch 1).
                          RIGTASK: late pickups sit after the NT chunk
                          task's END (sub-tags) and after the maps
                          drain; POSTPOLL v2 (row skip) +2-3 pts on
                          step 1 only, 3 launches. The untagged span
                          after the task is the master's V-ISR (lift,
                          flip, CRAM paint) -- RIGISR measures it.
                          FOUND (RIGISR..5): the hand-off is the
                          master's V-ISR, 41-82 lines on the FPGA demo:
                          the FPGA latches FS at vblank END (~+38), the
                          ISR spins for it, then copies the publishes +
                          the 4 KB text into the new back bank (19-30
                          lines of FB stores); the post (+17) waits
                          for the ISR's exit. Lift 0-2, text capture
                          0-10, restore_pages 0, CRAM 7-22 (during the
                          spin). ISRSPLIT=1 (knob): the ISR ends at the
                          FS write; flip_finish (latch wait + copies)
                          runs in the window body after the launch;
                          blit_half gates on FLIP_PEND_U on both CPUs.
                          RIG (3 launches each): is1 (copies after the
                          launch) pickup +22..40 but the wolf composes
                          RED (CRAM identical; pens differ; hazard
                          unnamed), rate = control; is3 (copies before
                          the launch, ISRSPLITPRE) exact, rate =
                          control. The post-flip copies are packet B
                          (83% of flips), MDSPR, and the 4 KB text
                          restore; TEXTRESTMASK=1 (exact on ares, both
                          banks == TEXT_U) restores only the groups
                          the game wrote. The FPGA floor as measured:
                          latch at +38, then copies + blit 52 + publish
                          11 must fit in ~68 -> needs the copies near
                          zero AND the slave composing during the
                          latch wait (the is1 order) AND the audit at
                          1-in-16. Line unchanged (rg1).
                          RIG (all 3-launch pairs, control 42 38 / 41
                          38 on the demo): ROWGEN 41 39, POSTPOLL 40
                          38 35 / 44 37, ISRSPLIT 41 41 35, is3 41 40
                          36, TEXTRESTMASK 42 38, +audit16 41 38 --
                          every hand-off cut is rate-neutral. RIGACK
                          (the ack line read directly): the window
                          ends at +110..195 on the demo on every rom
                          (rg1, tr1, shim DMAs off, gate spin slowed);
                          the 68K-silent rom reads 79-126 on a lighter
                          picture. The "body grows inside the 68K's
                          IRQ4" reading is RETRACTED (the ack byte is
                          the previous window's; same-position blit
                          pairs show no start-line effect). OPEN: why
                          the FPGA window ends ~50 lines after ares's
                          with the same stages -- ANSWERED 11:40 ET:
                          no hidden term. SPRSKIP (sprite copy off on
                          odd vints, pacing intact) shows no parity
                          effect; GATESLOW and MDQUIET=2 flat. The ack
                          band is pickup 20-140 + launch 7 + blit
                          53-90 + publish 11; the FPGA's excess is the
                          blit's rate and the pickup's spread. The
                          untried combination is the one rom with
                          every term's fix: ROWGEN + TEXTRESTMASK +
                          POSTPOLL + audit 1-in-16 (stage sum ~+100
                          on the demo) -- RUN 12:15 ET, 3 launches:
                          demo 43 36, step 1 32-35 vs control 42 38 /
                          37 37. No crossing. The threshold model
                          (release by ~+106) is not confirmed on the
                          FPGA. RIGSPAN (fixed hook, 16:20 ET): the
                          level-1 demo is 2-vint passes on ares (94-96%)
                          and 3-vint passes on the FPGA (73-86%); no
                          pass waits on the window on either machine;
                          the frame is 68K time, ~400 lines a pass, and
                          ares sits 2-29 lines under the 2-vint line.
                          (The 12:50 "straddle" reading was the hook's
                          two-read race; retracted in LESSONS.) The lever is an ack that
                          lands early enough on EVERY window (the
                          pickup spread 20-140 is the tail; the body
                          ~90 is fixed): that buys 2-vint frames
                          consistently (~50%, +10 points), not 60 Hz. Knobs proven exact, off the
                          line: POSTPOLL, TEXTRESTMASK (ISR 66-82 ->
                          22-29), ISRSPLIT PRE (is3), GATESLOW.
                          MTASK IN-WINDOW: the master composes its own
                          rows before its blit half instead of idling
                          46 lines behind the slave's compose. The row
                          ownership machinery exists (BANDSHIFT /
                          RG2SHIFT: the master owns [lo+36+BS, hi) of
                          each band as nat_mtask stage 1) but runs in
                          the post-ack tail, one window late for the
                          blit; running the stage-1 chunks inside the
                          window before BLIT_HALF, with the master
                          owning rows (SHIPBANDSHIFT=4 SHIPRG2SHIFT=0;
                          the bare BANDSHIFT/RG2SHIFT lose to the ship
                          list's literals, LESSONS 01:10), halves the
                          compose wall (~-25 lines). BUILT as
                          MTASKINWIN (nat_mchunk factored); probe13
                          measures it with the real split. The
                          chunk body is inline in the poll loop
                          (m_main.c ~12530-12600) and time-boxed; it
                          needs factoring (RAMCODE budget: ~300 B free
                          after BODYPROF's latch trick).
    ATTRACT BAKE          DONE 2026-09-21 14:10 (LESSONS "The attract
                          bake"). Open residue: (a) the ~0.3-0.5 s hold
                          after a picture's blank = the load (1120 cells
                          x 2 planes + art at 80/vint); (b) the
                          transformation's zigzag field loads in view
                          ~1 s (the game does not blank it; its sets
                          are baked, its cells still walk in over 8
                          windows); (c) the level's plane B stays
                          dirty (1120) through the cut and re-ships
                          after -- cosmetic, hidden by the eye's blank.

    RIG BARCODE READOUT   BUILT 2026-09-21 (`make line RIGBARCODE=1`,
                          docs/design/RIG-READOUT.md, decode with
                          tools/rig_barcode.py). 80 bits per capture,
                          written into the picture through the MD
                          window plane (rows 22-27, four-colour cells,
                          six votes per bit), nothing flashes and no
                          palette entry the BG uses below 61 is touched.
                          Carries: vint, packets consumed, palette-
                          flagged consumed, the SH-2's published /
                          flagged counts (packet header words 4/6 high
                          bytes), CRAM 16-47 live count, shadow, round /
                          cut / attract step. Rig 2026-09-21 02:48-02:57:
                          35 of 36 captures decoded over three launches
                          of barcode5; SH-2 published == 68K consumed on
                          every capture; CRAM 16-47 = 29 on every level
                          capture (3/3 launches kept the background).
                          It replaces the CRAM flood for anything with
                          more than one number in it.

    ares                  charges SH-2 INSTRUCTION CYCLES ONLY. No
                          SDRAM/uncached waits, no data cache, no
                          instruction fetch. SLAVE-GATED: the slave
                          reads as critical path.
    the rig (MiSTer)      MASTER-GATED. The only speed authority.
                          Flip rate varies 6x between cold runs --
                          scene-anchor everything.
    MAME (arcade)         the ORACLE. Always valid.
    MAME (our 32x)        a convenience model. Renders every NATIVE
                          build as confetti; cannot pixel-gate the
                          line. MD-side counters are honest.
    counter registries    m_main.c:72 (DIAG, 64 slots, FULL) and
                          m_main.c:718 (mdalloc_ctr, now 48 slots).
                          Written 2026-09-16 from a preprocessor-aware
                          census. READ ONE BEFORE ADDING A COUNTER --
                          no DIAG slot is free in every build.
    DIAG[36]              DOUBLE-BOOKED as r60_pkt_flip, and wiped
                          every frame. Unusable. The registry names
                          NINE more live DIAG collisions -- [35] [37]
                          [38] [39] [42] [50] [51] [52] [53]. Every
                          number read from one is the SUM of two
                          subsystems. [39] and [42] have FOUR owners.
    MDA[16]..[21]         WAS six-way double-booked: the NOTES 51
                          batch census had taken the allocator's own
                          slots, including the mdp_claim_pen pen-
                          starvation trio [19][20][21]. FIXED
                          2026-09-16 -- census moved to [32]..[37],
                          array grown to 48, tools/batch_census.py
                          follows. Any pen-starvation or batch figure
                          from before that date is a SUM.
                          The `[20] > [19]` that flagged it was NOT a
                          subset violation: both are the shipper's,
                          one a word count. The collision was real;
                          that arithmetic was not the proof.
    mdalloc_id[3]         HAD NO WRITER from LOOP29 158 to 2026-09-17
                          and read an unwritten .bss zero. Implemented
                          now (+[6] slots-on-screen). Any figure quoted
                          from it before that date is nothing.
                          Aim the watch: MDALLOCWHY=1 MDAWATCH=<set>.
    mdp_pen_own           stale after a free. NEVER read it without
                          masking on mdp_line_c != 0xFFFF.
    state_health.py       needs a .bs1; headless ares writes none.
                          Use the --dump rebuild.
    black_pct             night_run's frame guard. A WHOLE-FRAME black
                          fraction, swamped by legitimate black art: a
                          large silhouette blob at f4000 moved it 0.1.
                          NOT the instrument for a black-tile card.
    night_run shots       FRAME-INDEXED. Two roms of different image
                          size are photographed at different points in
                          the attract sequence. Diff `_end` before
                          comparing any two captures.
    diff_bytes_vs_base    displacement, not divergence. 1.45 MB was one
                          1408 B shift moving the tail; all 7 generated
                          artifacts were byte-identical. The "~1.3 MB =
                          stale bake" rule is SAME-FLAG only.
