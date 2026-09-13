# START HERE — read this before anything else

Written 2026-09-09 after a session that lost the plot. If you are a new
session, this file replaces `HANDOFF-PIPELINE.md` as the entry point.
That file is still accurate about the transport but it let a session
spend a night optimising the wrong number.

## THE BAR. There is only one.

**60 frames per second REACHING THE SCREEN.** Two numbers say whether a
build has it, and a build needs BOTH:

    tools/presented_fps.py ROM    MOTION: substantial picture changes
                                  per 60 frames. THE PLAYER'S FRAME RATE.
    tools/gameplay_speed.py ROM   the 68K's logic keeping up with its
                                  own frame. A PRECONDITION, NOT THE BAR.

    accepted line today   logic 49.7%   MOTION 1.3 fps
    the bar               logic  100%   MOTION  60

**CORRECTED 2026-09-09 16:30 (LOOP29 113). The paragraph that stood here
said the logic rate was the only bar and explicitly ruled out "flip
rate... screen updates per second." That was wrong, it was written into
this file at 01:23, and an overnight loop then ranked four hours of
builds on it.** The accepted rom scores 49.7% logic while showing ZERO
changed pixels across 60 consecutive frames — its scene timer advanced
350 ticks over the same second. Mike, unprompted, the next morning: "I
haven't gotten a single build from you that has moved frames."

The two numbers diverge because the port is single-buffered under
FBXPORT: the master composes into the bank being displayed, so a long
compose shows as a still frame no matter how fast the 68K runs. Rank on
MOTION. Use the logic rate for what it measures — whether the 68K
finished its pass — and never as a substitute for looking at the screen.

LOOP28 93 measured this same divergence on 2026-09-08 and Mike's play
pass overruled the gate then too. It has now cost two sessions. If a
future document tells you one number is the whole bar, check it against
`presented_fps.py` before you believe it.

## THE SCOPE

The 68000 clock is not the loss. The game needs 2780 instructions per
vint and the budget covers it.

**BOTH NUMBERS IN THAT SENTENCE ARE WRONG — CORRECTED 2026-09-12
(LOOP-DECOMPILE 88).** They came from a trace parser that ignored MAME's
`(loops for N instructions)` lines, so every loop counted once and the
work was under-reported 2.5x. Level 1, re-measured: the arcade executes
14,209 instructions a vint, 8,184 of them work, at 11.7 cycles each — a
normal 68000 mix, not the bus-bound 45 the old number implied. Our budget
allows 13.3. **That is a 14% margin, not a fourfold one.** Our own rom
does 9,637 work instructions a vint, 4,904 game and 4,733 shim, so
LOOP27's "the shim costs as much as the game" is still exactly right and
everything absolute around it was not. See ARCHITECTURE.md's scope
section for the full correction, and **`docs/handoff/PLAN-68K-BUDGET.md`
for what to do about it: the gap is 1,898 instructions a vint and
`r60_push` alone is 2,621.**

**CORRECTED 2026-09-10 (HANDOFF-20260910 section 2b, LOOP29 117/123/124).
This section used to end "our pipeline costs 2882 instructions per vint,
that is the whole gap". It is not the gap.** Measured per generation —
and a GENERATION IS 3.6 VINTS, not 1:

    MASTER accounted work    0.44 vints/gen   the master is IDLE
    SLAVE compose            1.40 vints/gen   0.0 idle polls per vint
      of which cat1 tiles    0.67             = 48% of it
    MASTER maps drain        0.95 vints/gen

**The 68K is not the frame-rate constraint and has not been for some
time.** That retires a week of shim/rotor/FM/transport work which moved
the logic rate and never the picture.

**CORRECTED AGAIN 2026-09-10 22:35 (LOOP29 161), measured on vi14.
"The SLAVE is the saturated processor" is no longer true, and the lever
this section pointed at is worth 1.6%.** Per generation, on the current
line:

    slave compose WORK      1.09 v   inside a 1.52 v echo phase
    master drain WORK       0.44 v   inside a 1.41 v mtask phase
    generation wall         1.57 v

**Neither processor is saturated. The generation is longer than either
CPU's work, so the cost is in the HANDOFFS between them.** `CAT1MD=1`
cuts the slave's cat1 tiles 43% and its whole compose 21% — and moves
the wall from 1.57 to 1.55 and the ship count 1.6%. Nine percent
pass-through.

Ships are VINT-QUANTISED: 80% take 2 vints, 15% take 1, so a generation
finishing at 1.2 vints still costs 2 and the flip lands at 30 Hz. 60 Hz
means one generation per vint, which needs ~0.6 v/gen removed from a
budget where no single component is that big. Look at the handoffs, not
at the compose.

## THE PIVOT (2026-09-10 18:00, Mike's order)

"It's never the 68K." Stop bending the 32X into a beam-racing shape;
patch the program's gates so it runs to OUR clock. Hardware speed is
read ONLY off the game's own dropped-frame counter painted on the rig
(`BOOTGAMERATE=1`, LOOP29 140): the accepted rom runs the game at ~15%
on the MiSTer, vi4 at ~44%; ares says 50% for both and is an upper
bound. Patch 1 is in: `GAMEGATE=1` (LOOP29 141) makes IRQ4 release the
main loop once per presented frame; overruns drop to zero and the game's
speed IS the flip rate. Line: `rom/night/vi10.32x` (LOOP29 150: vi8 flags, plane-packet mirror replays the written bytes; hardware flips 16-23 per 64 vints, vi4 had 1-7; Mike: backgrounds fixed on the rig).

**CANDIDATE, ON THE RIG AS `probe.32x`, AWAITING MIKE'S EYE (2026-09-10
22:52): `rom/night/vi16.32x` (md5 2f82be3b), LOOP29 152-164.** Mike on vi11: "still
lots of missing tile data." Located, and most of it fixed. The MD tile
residency map is destroyed by the colour-set drift free: 57 events per
4000 frames, ~46 tile slots each, and **95% of those slots are named by
the name table at the moment they are wiped** -- measured AT the event,
because sampling at round-numbered frames gave the opposite answer three
times in a row. Colour set 33, a fading terrain mass of eight
consecutive tile codes with four distinct pens, is 65% of it. vi14 keeps
a set's pen indices across the free so the re-assign lands where it was
and the tile patterns stay valid:

    over 12,000 frames          line -> vi16
    drift frees                  151 -> 58       -62%
    tile slots destroyed       6,235 -> 2,153    -65%
    cells blanked, art missing 16,194 -> 6,372   -61%
    isr-flips                  5,887 -> 5,915    +0.5%
    game logic                  48.7% -> 49.1%
    attract pixels vs arcade                     even or better
    skips, late flips                            0 and 0

The question for Mike's eye is the one he raised: **is there less
missing tile data in the backgrounds.**

**Do not rank this family on `presented_fps`.** It reads 5.9 -> 3.1 on
one of these builds and 5.9 -> 10.6 on another while `isr-flips` stays
flat to within 0.3%. With frame DELIVERY pinned, MOTION is measuring how
big each frame's delta is, and removing background flicker removes
delta. Rank on isr-flips, tiles-destroyed, and the arcade pixel diff.

Next lever after the play pass: the slave's compose (cat1 tiles = 48%),
which sets the flip rate.

## THE LINE (2026-09-12, validated twice on the rig)

**`rom/night/vi39.32x`, md5 c93dbeab, also `rom/s16.32x`. Mike:
playable, no obvious regressions.** (vi37 was the same code; vi39 is it
rebuilt on a clean tree.)

**Read `docs/handoff/HANDOFF-20260912.md` before touching anything** --
it carries the three routes with their measured state, six flags that
render wrong BY CONSTRUCTION, five traps this session paid for, and what
is closed so it is not rebuilt.

    make ship-us FBXPORT=1 FBXSTAGE=1 FBXPEND=1 FBXISRLIFT=1 PGSKIPPKT=1 \
                 TEXTCAPMASTER=1 TEXTCAPFULL=1 GAMEGATE=1 GAMEGATEWAIT=1 \
                 TEXTCAPEARLY=1 TAGKEEP=1 PENHOLD=1 PENREPAINT=1 \
                 NBUILD1=1 MDSPRTOP=1

    wall 1.47v   32.1 fps   isr-flips 2,067   20% single-vint

**THE BAR IS A THRESHOLD, NOT A GRADIENT (LOOP29 168).** 60 Hz is 100%
single-vint. Ships are vint-quantised, so a generation at 1.2 vints
still costs 2 and flips at 30 Hz: nothing is paid until the wall crosses
below 1.00, then it all arrives (15% -> 98% in the ablation). Every diet
before this session was ranked against a gradient that does not exist.
**60 IS reachable** -- ablating the maps drain and the sprite compose
gives 58.3 fps at 98% single-vint, so the pipeline was never the
constraint.

Three of those flags are load-bearing in ways ares cannot show you:
**`NBUILD1` is MANDATORY** (without it the rig is 0.33 fps and ares sees
a 10% difference), **`TEXTCAPMASK` must stay OFF** (it drops the Zeus
cut-scene text), and **`GAMEGATEWAIT=1`** feels better while measuring
slightly worse. See LOOP29 188 for the full ranking of what is left.

## WHICH FILE IS THE BUILD (2026-09-11, after this cost Mike three launches)

**`rom/s16.32x` is whatever was built LAST, and that is usually a probe.**
It went to Mike twice as a playable build when it was a measurement build
that renders black by construction, and `probe.32x` on the rig sat three
builds stale pointing at a corrupt one.

The discipline, from here:

  - **`rom/s16.32x` is left holding the LINE build** at the end of any
    session or handover. A probe build gets copied to `rom/night/` or the
    scratch directory and `rom/s16.32x` gets rebuilt back to the line.
  - **`probe.32x` on the rig tracks whatever build is being asked about**,
    and the message names the file explicitly.
  - **A build is not handed over until it is ON the rig.** One command
    copies it and launches it, and it is the last step of any build worth
    Mike's time:

        tools/mister_push.sh rom/night/vi43.32x     # copy + launch
        tools/mister_push.sh -n rom/s16.32x         # copy only

    Details and overrides in `docs/handoff/HANDOFF-DREQ.md` "THE RIG".
    Naming a path and stopping is not a handover; Mike had to run this
    himself on 2026-09-12.
  - A probe that renders wrong BY CONSTRUCTION (`NOMAPS`, `NOCLEAR`,
    `SPROBE`, `C1NOFB`, `RELBANK`, `GENSKIP`) says so in the same
    sentence as its number.

## THE LINE: `rom/night/bldB.32x` (md5 493d4984) = `rom/s16.32x`, NAMED BY MIKE 2026-09-12 20:15 ("Behind the grass stellar lockdown on the progress"; PLAN-SINGLE-VINT Build B)

bldB = vi95's flags + `C1PUNCH=1` (fold 1's hole punch, Build A) +
`SETCOLS=1` (fold 2, Build B). Ares wall 1.18 v/gen, 40% single-vint,
check mode 0/0 on 3,010 planes, rig frame rate 21 19 7 15 18 presented
frames per 64 vints, attract black shares zero over three launches.
Every card from here is one change against bldB. The previous line,
vi70 (md5 fadafb08, accepted 2026-09-12 14:50, "otherwise SOLID
presentation", LOOP29 224), is history: vi70 drew cat-1 in the
framebuffer at wall 1.48 / 18%.

`fmgate_defer` (68K WRAM, frame 2500 of the attract) is the handshake's
instrument, NOT a gate (LOOP29 229 set it as one, 230 retracted that:
vi71 0 deferrals with a black return, vi75 54 with a clean one). The
window handshake collides by phase (fold 3's subject); the round-clear
text fix (225, correct, vi74) is parked on it.

**FOLD 1 CANDIDATE, ON THE RIG AS `rom/night/vi75.32x` (md5 f1d8162f):**
vi70's line + `CAT1MD=1 C1NOFB=1`. Headless: plane on the first field
frame, aligned return 4.5%, play-path black = vi70, coined flips above
vi45 through the demo. Mike's gate: the hole punch over sprites.

**FOLD 1 ON THE RIG (2026-09-12 20:50, LOOP29 231).** Mike: "you have
NAILED sprite performance" / "the background and foreground tiles aren't
being updated properly". Whole tile sets of level 1 (temple, pedestal,
trees) render black on the FPGA and not in ares; vi70 is clean on the
same rig. The packet transport is PROVEN intact on the rig (BOOTTILEVER
probes vi76-79: the 68K consumes exactly what the SH-2 built, no FS/FM
hazard) -- the black is built on the SH-2, by a timing-only state
divergence nobody can see yet. Retracted: the zero-record chain
(vi76-85, confounded by the level's own blank tiles) and re-applying
226's trampoline mask (vi86: no change on the rig, worse in ares). Next
instrument: a 24-bit-per-capture value channel (four tagged 6-bit CRAM
floods), then the allocator's own counters at matched attract seconds.
Presentation line stays vi70; vi75 is the speed lever.

**WHERE WE ARE, IN VINTS (LOOP29 232, 2026-09-12 21:20).** ares ranks;
the rig measures (`BOOTFLIPRATE=1`, presented frames per 64 vints off
the FS bank, decode = flood value & 0x7F):

    build      ares wall   single-vint   ares fps   RIG fps (attract demo)
    vi70         1.48         18%          43           ~15
    vi75         1.11         44%          60           ~20
    bar          1.00        ~98%          60            60

The FPGA is ~3x behind ares on the SH-2 side, as CLAUDE.md warns. The
bar is three times away on hardware. Next: fold 4 (one state word from
IRQ4, deleting the claim-mix flag and the COMM10 edge read), then fold
2/3 per the plan; every build gets its rig fps from the probe.

**FOLD 4 STATE (LOOP29 233-237, 2026-09-12 23:00).** `MDSTATE=1` posts one
state word per vint on COMM14 (seq, cutscene 0xFFF148, round 0xFFF142,
play 0xFFF026.0, attract step 0xFFF031>>2). It decides OFF where the
game is certain (transformation; picture steps 0/2/4) and forces ON in
demo steps 3/5; the claim mix still decides elsewhere (the play bit is 1
in the demo, step 1 spans the SEGA screen -- NOTES-FROM-DECOMPILE 24
asks for the two discriminators). On the rig this halved fold 1's black
(vi90); per-tile rectangles remain and are being censused (vi92).
TXTWRAM as written halves the rig frame rate (237): fold 5's copy must
change transport before RELBANK is measured. Any 68K change re-rolls the
ares same-round return (227); judge those builds on the rig.

**vi95 (2026-09-13 00:20, LOOP29 240-241)** = vi75's line + `MDSTATE=1`
with every attract decision from the game's own bytes (NOTES 29:
credited = 0xFFF026.0 clear; step 0xFFF031>>2; demo bit f028|f029).
Mike: "we've repaired most defects." It is the fold-1 build to play;
the rig frame rate matches vi75 (fr95e 19 15 22 21 14). Never let the
word hold the round off in a credited game (vi90/vi94, 238-240). Open
on it: the hole punch (sprites over cat-1 ground, 175), black drops
(ship rate, 237), the text gates (fold 5). NEXT BUILD: fold 2.

**FOLD 2 DONE, HOLE PUNCH BUILT (2026-09-13 02:10, LOOP29 242-244).**
`SETCOLS=1` (sc95 = vi95 + fold 2): the maps scan from the baked
per-column set extents; check mode 0/0 disagreements over 3,046 planes;
ares wall 1.16 -> ~1.05 v/gen, single-vint 42 -> 49-55%; rig frame rate
unchanged (the rig's wall is the windows-per-vint floor). TRAP FIXED ON
THE WAY: the shared rom unpacker's zero runs are n+1 (243) -- every
tool that copied LOOP-DECOMPILE 10's line was column-shifted; verify
any rom-derived table against a live TILEMAP_C dump. `C1PUNCH=1`
(c1p95b): sprite pixels of pp < 3 punched where the FG cell is cat-1,
per pixel where the bake says the tile is partial; the zombies mask,
the legs draw through the grass. Mike's eye is the gate. NEXT: fold 5
with the copy on the packet side (237), then RELBANK on the rig.

**THE FOLD-1 LINE IS c1p95c (Mike, 2026-09-13, PLAN 0b1ff57):** vi95's
flags + `C1PUNCH=1`, md5 85116f58. Builds are lettered from here with a
card in PLAN-SINGLE-VINT (Build A = c1p95c, passed). Build B = fold 2
(`SETCOLS=1`) on c1p95c's flags, measured on that line before it counts.

**THE LINE IS bldB (Mike, 2026-09-13 04:00, LOOP29 245):** c1p95c +
`SETCOLS=1`, md5 493d4984 -- folds 1, 2 and 4 on one rom. Build line:
vi75's flags + MDSTATE=1 C1PUNCH=1 SETCOLS=1 (see Build B's card in
PLAN-SINGLE-VINT for the flag set and the numbers). Next card = fold 5
with the text copy on the packet side, then fold 3 / RELBANK on the rig.

**LANDING RULE (2026-09-13):** a rig black share counts only across THREE
launches -- the same rom read a fifth black at one launch and clean at
the next with nothing changed; one capture is a phase, not a defect.

**THE RIG IS SELF-SERVICE.** No Mike needed for attract-mode probes:

    tools/mister_push.sh rom/night/X.32x          # deploy + launch
    ssh root@mister.office.local "echo screenshot > /dev/MiSTer_cmd"
    scp root@mister.office.local:/media/fat/screenshots/S32X/<newest>.png .
    first level-1 demo at launch+14..28 s, face/eye 38-42 s, second demo 46-55 s

A probe round (build, ares check, push, four shots, decode) is ~2.5 min.
Always md5-guard the build against the previous rom before pushing: a
broken build leaves rom/s16.32x = vi70 and the rig then "reads" vi70.

vi42's exact flag line plus `MDBATCH=24`, with LOOP29 196-199 and 209 on
top: the round tables re-encoded and packed into THREE lines (the pink
trees), the tile ship rate as a knob (the black pop-in), and the cutscene
mechanism keyed on the allocator's claim mix rather than the palette (the
chevron plane, the flat flames, the black wall band, vi51's stutter), plus
210's guard on the scene-image load (harmless, measured identical; it did
NOT fix the title logo -- LOOP29 210's retraction). OPEN, attract only:
the logo's slide-in palette and the ranking screen (211), both need a
capture keyed on the game's own palette phase, not a frame offset.
vi70 = vi62b + the all-pages bake (LOOP29 215: the invisible ledge) + the
published round at the edge back with the MAILBOX MASKED on the 68K (217,
222: the dirty word's regions 13-15 corrupted the round) + stale-tag wipe
only for a cutscene's own sets at assign time (223). No edge-out wipe:
216-221's wipes/frees all withdrawn (219: the full wipe bands hardware;
223: wiping the level's sets on the return leaves holes). Headless: flips
= vi45, play = vi45, ledge band below vi62b, plane 15 frames after the
field, same-round return 3.7%. Headless: flips = vi45, play
path = vi45, ledge band below vi62b, plane 55 frames after the field,
3.7% black after the eye (vi45 4.6%). vi62b = vi59's logic + `MDBATCHOFF=24` (LOOP29 214), now the default:
Mike's rig ranked it -- 40 tore level 2's top band on hardware, 24 does
not, at the cost of the chevron plane arriving 50 frames after the field. vi60/vi61 (hysteresis) withdrawn. Next signal:
0xFFF148 via the mailbox (LOOP-DECOMPILE, LOOP29 214).
Headless ares: coined-path flip rate identical to vi45, play path
identical to vi45, the transformation's blue plane up within 25 frames of
the cut at the arcade's share with coloured flames. NOT shown by headless:
level 1 after a transform in PLAY; hardware tear during the batch-40
cutscene span. vi46-vi57 are withdrawn; LOOP29 200-208 record why.

## THE ACCEPTED ROM

    make ship-us FBXPORT=1          # tag mister-keeper-20260908

FBXPORT is not optional: without it the port is a BLACK SCREEN on real
hardware, which ares does not show you. Every flag added since is
default-off.

## DEAD ENDS — do not re-open without new evidence

  - **Double buffering** (`FBXSTAGE`, `FBXBOTH`, `FLIPEDGEOFF`) — **NO
    LONGER A DEAD END, and this entry was wrong. Corrected 2026-09-10.**
    It was dead-ended for "costing 15 points of game speed", which is the
    LOGIC metric this file's own bar section now disowns. Ranked on
    MOTION it is 1.3 -> 16.7 fps, a 13x improvement, and Mike's MiSTer
    pass called it "an order of magnitude improvement... not enough
    frames to be playable" and later "more concurrent frames displayed
    but no speed improvement".
    It is a REAL TRADE, measured: `FBXSTAGE+FBXBOTH` costs 2.6 points and
    buys almost nothing (the vblank edge guard declines most flips);
    `FLIPEDGEOFF` buys all the motion and costs 12 points of game speed
    (47.1 -> 34.9). Frames OR speed, and nobody has got both.
    **Where those 12 points go is MEASURED (2026-09-10, LOOP29 136):
    with the guard off the FS write lands mid-scan, both ares and the
    FPGA defer the latch to vblank, and the master spins on that latch
    with FM held (~198 lines). The game's text writer waits on FM to
    the end of the frame and the next vint enters late; 33 of 33
    deferred flips did this.** BUILT AND MEASURED, LOOP29 137: the fix
    was not to release FM but to get the FS write INSIDE vblank with the
    guard on — stop capturing the two packet regions as page truth
    (23 lines of drain per flip), lift the packet before the flip on the
    ISR path, and move the 68K's packet blast out of the pre-post slot
    into the FM-gate spin. `make ship-us FBXPORT=1 FBXSTAGE=1 FBXPEND=1
    FBXISRLIFT=1 PGSKIPPKT=1 TEXTCAPMASTER=1 TEXTCAPFULL=1` =
    `rom/night/vi2.32x`: ~29 Hz flips, none deferred, logic 50.1%.
    vi.32x (LOOP29 137) drew black backgrounds on Mike's pass: the
    packet sat in tilemap page 0, the BACKGROUND page — LOOP29 138 moves
    it to page 12 and the attract title eye renders clean for the first
    time. **Awaiting Mike's play pass on vi2; not the accepted rom.**
    The build is `rom/night/dblfast_clean.32x`, on the MiSTer as
    `dblfast-20260910.32x`.
  - **`PALSTREAK` / `PALBACKOFF`.** Were dead code until 2026-09-08 (the
    counter was never written). Fixed, swept 25 ways, no setting wins.
  - **`TEXTCAPMASTER`.** Needs `TEXTCAPFULL` or it renders confetti. Even
    correct it buys nothing on the shipping line.
  - **`PALNOCMP`, `CLAIMNEW`, `FBXTAIL`.** Re-measured twice each in
    2026-09-09; every negative holds.
  - **`BGBLANK0`.** A different bug's workaround. See the open bug below.

## HOW TO MEASURE WITHOUT FOOLING YOURSELF

Four rules, each of which was learned by breaking it:

  1. **`gameplay_speed.py` measures LOGIC.** It is deterministic for a
     given source — two clean builds of the same code agree exactly —
     but it swings **18 points on code layout alone**. So an A/B is
     valid only when the builds differ in the thing under test, and
     never trust a gap under ~6 points.
  2. **`anim_rate.py` counts CHANGE, not correctness.** A build
     rendering confetti scores three times the shipping line.
  3. **LOOK AT A FRAME.** Colour count, black fraction and edge density
     were all in range on a frame that was visibly broken. There is no
     cheap automatic substitute. `tools/attract_parity.py` against
     `ref_arcade` is the only oracle that judges pixels.
  4. **Divide before you claim.** Two wrong conclusions in one session
     came from reading a total as a rate. 136,411 writes over 3000
     frames is 45 a frame, and they are all in 0.3% of the frames.

## THE ONE OPEN BUG, LOCATED

The attract title screen never renders: the arcade shows a full-screen
eye, we show the demo's graveyard with the title text over it. Measured
at the same frame — our name table in SDRAM goes from 341 distinct
background slots to 4, correctly. **MD VRAM Plane A follows it (148 ->
2). Plane B does not (338 -> 207).** The background name table is
computed right and never reaches Plane B. The walk, the capture, the
SH-2 cache and the page selects are each measured innocent.

Next probe: instrument Plane B's upload against Plane A's at the frame
the scene changes. Both come from the same pass, so the difference
between the two upload paths is the entire defect.

## THE LAST SESSION

`docs/handoff/HANDOFF-20260910.md` — the 2026-09-09/10 rendering session,
in full: the FBXPORT motion regression and how eight days of instruments
missed it, the frames-or-speed trade the double-buffered line makes, where
a generation's 3.6 vints actually go (the master is idle, the SLAVE is
saturated, cat1 tiles are 48% of it), the palette question closed by
exhaustive search, the five-item diagnostic refactor, and a full account
of four retracted claims with the failure pattern behind them. Read
section 4 before trusting any LOOP29 entry and section 7 before writing a
probe.

`docs/log/LOOP29.md` entries 107-135 is that session's log.
`docs/handoff/HANDOFF-DECOMPILE-3.md` is the CURRENT brief for the
parallel decompile thread (2026-09-12; -2 and the original are its
history). `docs/handoff/HANDOFF-DECOMPILE.md` briefs the parallel Ghidra thread;
`docs/log/LOOP-DECOMPILE.md` is its log.

## THE WORKING LOG

`docs/log/LOOP28.md` is this session. Entry 94 is marked WRONG in place
and corrected by 96; entry 104's conclusion is superseded by 106. Read
the corrections, not just the entries.
