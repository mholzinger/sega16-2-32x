# LOOP28 — the rotor sweep, and the staging-buffer blocker

Continues LOOP27. Entry point for the session state is
`docs/handoff/HANDOFF-PIPELINE.md`.

All builds in this log are clean (`make clean` first, HANDOFF-PIPELINE
section 0), on the line

    make ship-us FBXPORT=1 TXTWRAM=1 LATESTEAL0=1 LATEKEEP=1 DRAWADOPT=1

and measured with `tools/gameplay_speed.py` on the level-1 input script,
frames 1500..4100. The baseline reproduces LOOP27's number exactly:

    82.3%   game-frames 2139 / vints 2600   md5 b62237e5...

## 84. THE STAGING BUFFER HAS 15,872 BYTES OF FREE 68K WRAM

HANDOFF-PIPELINE section 4 named one blocker on the staging-buffer
decomposition: "finding ~1.8 KB of free 68K WRAM ... has not been
audited." Audited. There is 8.8x that much, and it is contiguous.

**Where it is.** `md_src/md.ld` links our .data at 0xFF0100 and .bss
directly after it. In the shipping build:

    __data_start  0xFF0100
    __bss_start   0xFF21A9
    __bss_end     0xFF21D4      <- the linker's high-water mark

The next occupied address above that is `PAL_SHADOW` at 0xFF6000
(`md_src/packet_fmt.h:203`, 2048 words = 4 KB, so 0xFF6000-0xFF6FFF),
then spriteram at 0xFF7000. So the gap is

    0xFF21D4 - 0xFF5FFF        15,916 bytes free

**That it is really free, measured two ways.**

1. Full 64 KB WRAM dumps at frames 1500 / 2600 / 4100 of the level-1
   run: every page from 0xFF2200 to 0xFF5FFF is 0x00 and identical
   across all three.

2. ares write/read census (`--count-writes` / `--count-reads`) over
   0xFF2200-0xFF5FFF:

    | run              | frames | writes total | writes after f100 | reads |
    |------------------|-------:|-------------:|------------------:|------:|
    | level-1 script   |   4100 |       15,872 |                 0 |     - |
    | attract, no coin |   9000 |       15,872 |                 0 |     0 |

   All 15,872 writes are at frames 2-7 and come from exactly two boot
   clear loops: PC 0x8C044C (our `md_start.s` clear) and PC
   0x8806CC-0x8806DA (the game's own movem RAM clear). Nothing writes
   the region after boot, and in 9000 attract frames nothing READS it.

**Caveats, stated.** The census covers level 1 and attract, not every
scene. Two further guards already exist: the region is inside our own
linker's arena rather than the game's work RAM (which is 0xFFC000 and
up), and `tools/patch_game.py` scans the whole ROM for absolute
references into 0xFF0000-0xFFBFFF. 32 of that scan's warnings have low
words inside the gap, but they are the shape of 68K opcode and immediate
words (0x30FF, 0x3FFF, 0x4040, 0x41F8) rather than addresses, and the
runtime census agrees with reading them as false positives.

**How to take it.** Declare the buffer as a normal static array in
`md_src/md_main.c`. The linker grows .bss upward from 0xFF21D4 and would
not collide until 0xFF6000. There is no guard on that collision today —
`md.ld` should get an `ASSERT(__bss_end <= 0xFF6000, ...)` before anyone
leans on this.

## 85. PALSTREAK / PALBACKOFF WERE DEAD CODE — THE COUNTER WAS NEVER WRITTEN

LOOP27 80a left the rotor diet as the open lever on the 4.1 points
between the rotor being on and PALROTOR_OFF. The sweep it asked for
reads one number, because the flags did nothing.

`pal_streak[64]` (md_main.c:1277) is declared, and it is READ at the
backoff test:

    pal_streak[r] >= PAL_STREAK_N && kcap == 8 && ((fr2 + r) & PAL_BACKOFF_M)

It was never written anywhere. It is a static array, so it is zero for
the life of the run, and the test is false for every N >= 1. Measured,
before the fix, four different backoff depths:

    PALSTREAK=1 PALBACKOFF=3     82.3%   game-frames 2139
    PALSTREAK=1 PALBACKOFF=7     82.3%   game-frames 2139
    PALSTREAK=1 PALBACKOFF=15    82.3%   game-frames 2139
    PALSTREAK=1 PALBACKOFF=63    82.3%   game-frames 2139

Identical down to the frame count, with four different rom md5s. That is
the signature of an inert flag, and it is why the sweep was worth
running even though it measured nothing: it PROVED the inertness rather
than assuming it.

**Fixed** (md_main.c, this session): `pal_streak[r]` increments at the
fast pre-scan's equal exit, and resets to 0 both when a raw block is
force-shipped and when the mask walk finds `cnt` changed words.

## 86. THE ROTOR CEILING IS 94.7%, NOT 86.1%

Re-measured `PALROTOROFF=1` (no palette visits at all after vint 900,
colours freeze) clean, on the FBXPORT line:

    baseline                    82.3%
    PALROTOROFF=1               94.7%      md5 a1f49935...

LOOP27 80a recorded that ceiling as 86.1% and the prize as 4.1 points.
That was measured on the DREQ line. On the FB transport the prize is
**12.4 points**, which makes the palette path the largest single
scheduled cost left in the 68K's vint.

## 87. THE SPEED GATE PRINTED GARBAGE WHEN THE SCENE TIMER RESET

`tools/gameplay_speed.py` read the scene timer at frame A and frame B
and reported `(tb - ta) & 0xFFFF` game-frames. 0xFFF02A is a PER-SCENE
counter and it restarts when the scene does. Any window crossing that
reset came out as a huge positive:

    rom            window        printed
    base.32x       [3000,5500]   2546.7%
    base.32x       [1500,6000]   1445.1%

Both are the level-1 script running off the end of the scene it holds.
The gate had no guard, so a mis-sized window produced a plausible-looking
line ("game-frames 63668 / vints 2500") rather than an error.

**Fixed:** the tool now refuses to print a number when the timer goes
backwards, names the reset, and exits 2. The canonical window is
unchanged (`[1500,4100]` still reads 82.3% / 2139 game-frames on the same
rom). The level-1 script holds one scene from about f1200 to f4500; stay
inside it.

## 88. THE LEVEL-1 LADDER MOVES 18 POINTS ON CODE LAYOUT ALONE

This is the session's main result and it governs how every other number
in this log should be read.

**The sweep that provoked it.** With the streak counter implemented
(entry 85), 25 backoff settings on the FBXPORT line, all clean builds,
all distinct roms, canonical window:

    PALSTREAK   M=1     M=3     M=7    M=15    M=63
        0      65.9%   77.2%   74.9%  85.7%   87.0%
        1      76.1%   53.7%   82.6%  75.0%   87.4%
        2      81.8%   77.0%   85.2%  81.3%   78.7%
        4      83.3%   82.2%   54.2%  84.0%   82.4%
        8      67.5%   67.9%   78.8%  85.8%   80.6%

    default (diet compiled in, N=4 M=3)   82.2%
    baseline (diet inert)                 82.3%

There is no surface here. Adjacent settings differ by 29 points
(N=1: M=3 is 53.7, M=7 is 82.6), and the two worst cells sit next to
two of the best.

**The control that explains it.** `LAYOUTPROBE=1` adds 64 bytes of
unreferenced `.data` inside `r60_push` and changes nothing else — no
branch, no store, no read. Clean builds, same flags otherwise:

    shipping line                       85.3%   (2219 game-frames)
    shipping line + LAYOUTPROBE=1       67.2%   (1748 game-frames)

**18.1 points from 64 bytes of padding.** The ladder's sensitivity to a
change that cannot affect behaviour is larger than almost every
difference the sweep above reports.

**Where those 18 points come from, sliced into 700-vint windows:**

    rom            [1500,   [2200,   [2900,   [3600,   canonical
                    2200)    2900)    3600)    4300)   [1500,4100)
    base            53.1%    86.0%    95.3%    99.7%     82.3%
    gated           59.0%    88.4%    98.7%    88.4%     85.3%
    LAYOUTPROBE     55.0%    58.6%    65.3%    99.3%     67.2%

The first window is the only one where all three roms are looking at
nearly the same content: they enter it within 22 game-frames of each
other. There the spread is **5.9 points**. After that the trajectories
separate — by f4300 the padded build is 414 game-frames behind the
baseline, so its later windows cover earlier, easier content, and the
comparison stops being like-for-like. Window 4 shows the padded rom at
99.3% precisely because it is behind.

So the ladder carries two errors at once: a **~6-point floor** from
threshold crossings on aligned content, and an unbounded divergence term
once a build falls behind on a frame-indexed input script. The
canonical-window number is the sum of the two.

**Why, and why it is not a broken metric.** The port sits on the IRQ4
threshold (the frame threshold law, LOOP27). Speed is 100 minus the
miss rate, and a miss is all-or-nothing per frame. A few cycles of
address arithmetic — absolute addresses shifting across a boundary
changes 68000 instruction lengths — move a handful of vints across the
deadline, and each crossing costs a whole game frame. The metric is
measuring something real. It is just measuring it on one deterministic
trajectory that sits on a knife edge.

**Rules this imposes on every future measurement:**

  1. **A difference under ~6 points is not evidence, and one under ~18
     points on the canonical window needs the per-window slices before
     it is.** 48.7 -> 60.7 -> 82.3 (HANDOFF section 2) survives this;
     any ranking of two builds a few points apart does not.
  2. **Report more than one window.** The same rom reads 67.2% over
     [1500,2600] and 93.3% over [2600,4100] because the content differs,
     not the machine.
  3. **A flag whose payoff is a few points needs a different
     instrument** — the handler-mean A/B in `tools/health_mame.lua`, or
     `tools/frame_timeline.py`'s per-vint 68K clock — not this ladder.

**What the sweep therefore concluded:** nothing about the rotor diet. It
is left OFF in the shipping build (entry 85's `PAL_DIET` gate), the
counter fix is kept because the code was broken either way, and the
12.4-point PALROTOROFF ceiling (entry 86) stays the real target because
it is the one gap comfortably outside this noise.
## 89. THE STAGING SPLIT IS BUILT, AND IT MOVES THE POST BACK INTO VBLANK

HANDOFF-PIPELINE section 4's decomposition, implemented as `FBXSTAGE=1`
(needs `FBXPORT`, default off).

**What it does.** The packet BUILD — rotor, palette compare, record
packing — writes into `fbx_stage[936]` in WRAM instead of straight into
the framebuffer, and runs after the post, where FM=1 does not apply to
it. `r60_blast()` then copies staging into the framebuffer at the tail,
in the FM=0 window the master's ack opens, and writes the publish word
last. One vint of packet latency, which the harvest already tolerates.
The buffer is 1872 of the 15,916 free bytes from entry 84; `md.ld`'s
ASSERT is the fence and `__bss_end` moves 0xFF21D0 -> 0xFF27FA.

**It does the thing it was designed to do.** V at post, from the 68K's
own stamp at 0xFFA0A0:

    make ship-us FBXPORT=1 <opt1>                 V at post = 21
    make ship-us FBXPORT=1 <opt1> FBXSTAGE=1      V at post = 243

243 is inside vblank, so the ISR can flip again. Measured with ares
`--trace-flip` over frames 1500-4100, which is the display truth:

    build                                   game speed   flips
    make ship-us FBXPORT=1                     49.7%      0.0 Hz
    make ship-us FBXPORT=1 FBXSTAGE=1          48.8%     21.9 Hz
    + opt1                                     85.2%      0.0 Hz
    + opt1 + FBXSTAGE=1                        50.3%     14.8 Hz

On the accepted hardware line the split costs 0.9 points — inside the
resolution floor of entry 88 — and takes the flip rate from nothing to
21.9 Hz. On the opt1 line it costs 35 points, which is outside the floor
and real: the ~69-line build now sits between the post and the game's
IRQ4, which is section 4's constraint 3 violated.

## 90. BUT A FLIPPING FB-TRANSPORT BUILD RENDERS BLACK, AND ALWAYS DID

Before treating any of entry 89's flip rates as progress: **count what is
on the screen, not how often it changes.** Eight consecutive frames
(2400-2407) of the level-1 run, percentage of the frame that is pure
black:

    make ship-us FBXPORT=1                0 Hz     18% black,  95 colours
    make ship-us FBXPORT=1 FBXSTAGE=1  21.9 Hz     98% black,  35 colours
    make ship-us FBXPORT=1 FBXTAIL=1   ~12 Hz      94% black,  66 colours
    make ship-us  (DREQ ship line)     21.5 Hz     17% black, 102 colours

Not alternating — every one of the eight frames is 98% black. So:

  1. **The build that "never refreshes" is the one showing the game.**
     At 0 Hz the master keeps composing into the single visible buffer.
     A flip rate read on its own inverted the ranking.
  2. **This is not FBXSTAGE's bug.** `FBXTAIL`, which predates it, does
     the same thing. Both are FB-transport builds that flip; both go
     black. The DREQ ship line flips at the same rate and renders.
  3. **So the defect is the second bank, not the post's position.** The
     master composes into one buffer; the flip swaps to one that was
     never composed. `sh_src/m_main.c` already names this — "the bank
     disease the k2 comments describe" — and carries `cycle_dirt` and
     the restore set for it. Under FBXPORT with no flips that machinery
     never ran, so the fault has been latent.

**FBXSTAGE is therefore NOT a shipping candidate**, and it is kept
default-off. What it bought is a correct diagnosis: section 4's next
stage is done and it was not the thing in the way. The FB transport's
real blocker is composing the second bank.
## 91. THE FRAMEBUFFER SWAPS AND IT EATS THE PACKET — ONE PER FLIP

Entry 90 said a flipping FB-transport build renders black and blamed the
second bank. Right region, wrong mechanism. The master's own lift census
(`FLIPCENSUS=1`, CEN[10]=1 verified on every run below), 3000 frames:

    build                       lifts/windows   stale seq   FS writes
    FBXPORT=1                    2908 / 2908         0           9
    + FBXSTAGE=1                 1428 / 2921      1180        1186

**1180 stale against 1186 flips: one lost packet per flip.** The packet
lives IN the framebuffer (0x12000, `FBX_PKT_MD`), the framebuffer window
maps one bank at a time, and the bank the master's lift reads is not the
bank the 68K's blast wrote. The master then finds an already-seen
sequence, keeps last frame's records, and — because the PALETTE rides
the same packet — CRAM never updates. 98% black is a missing palette,
not a missing image.

`FBXLATE=1` (lift below the flip instead of above it) does not help:
1302 lifted, 1395 stale. The position of the lift is not the variable.

**The fix, `FBXBOTH=1`:** write the packet twice, at the tail and again
before the next post, with a flip possibly between them, carrying the
SAME sequence so a master that already lifted it skips it. Two ~2-line
copies. It works completely:

    + FBXSTAGE=1 FBXBOTH=1       2889 / 2890         0          41
                                 f2400: 88 colours, 17% black

Delivery goes to 2889 of 2890 and the picture comes back.

**But full delivery overruns the master's vblank.** FS writes collapse
to 41: the master now harvests and applies a packet every window instead
of every other one, and the flip write lands past the vblank edge guard,
which declines it. Dropping the guard (`FLIPEDGEOFF=1`, which the RTL
says is safe on real silicon — `srcref/S32X_MiSTer rtl/32X/VDP.sv`
defers a late FS write instead of tearing):

    + FBXBOTH=1 FLIPEDGEOFF=1    2879 / 2880         0         993
                                 f2400: 110 colours, 17% black
                                 20.0 Hz, game speed 29.6%

That is the first FB-transport build that flips AND shows the game.

## 92. CORRECTION: "0 Hz" NEVER MEANT A FROZEN SCREEN

Entry 90 and HANDOFF section 2 both read the flip-rate column as refresh
rate. It is not. `make ship-us FBXPORT=1` writes FS nine times in 2988
vints and its picture still changes 24.5% of pixels over 50 frames — the
master composes into the bank that is being displayed, so updates appear
without a flip. It is SINGLE-BUFFERED, not frozen.

So the FBXPORT line's problem was never "the display refreshes 0.3 times
a second". It is that it composes into the visible bank, which is a
tearing question and therefore a play-pass question, not something any
screenshot count answers. The whole double-buffer effort above is worth
its cost only if that tearing is visible.

**What to ask before spending more on this:** does the current MiSTer
build tear? If it does not, single-buffered is the answer and entries
89-91 are insurance. If it does, the ladder is:

    FBXPORT=1                                49.7%   single-buffered
    + FBXSTAGE=1 FBXBOTH=1                   45.9%   single-buffered
      (delivery fixed, edge guard declines)
    + FLIPEDGEOFF=1                          29.6%   20.0 Hz, renders

and the 16 points between the last two are the master's vblank budget —
which is section 4's original target, reached from the other side.
## 93. MIKE'S PLAY PASS OVERRULED THE GATE AGAIN, AND IT WAS RIGHT

Hardware verdict on the two roms of entry 91, MiSTer, level 1:

  - **Neither rom tears.** So entry 92's open question is answered:
    composing into the visible bank is not visibly wrong, and the whole
    double-buffer effort is insurance rather than a fix.
  - **The double-buffered rom shows MORE animation** — while measuring
    29.6% against 49.7% on the speed gate.

The second half is the finding. `gameplay_speed.py` measures the game's
LOGIC advancing: scene-timer ticks per vint. It says nothing about how
often a new picture reaches the player. Measured (new tool,
`tools/anim_rate.py`: one second of consecutive ares frames per window,
counting frames that differ from their predecessor by >0.05% of the
screen):

    build            logic rate    screen updates/sec at f1800/2600/3400
    T_singlebuf         49.7%           21, 14, 0      mean 11.7
    T_dblbuf            29.6%           30, 26, 20     mean 25.3

**The build with the faster logic presents less than half the
animation**, and in one window the single-buffered picture DID NOT
CHANGE FOR A FULL SECOND while its logic rate said 49.7%.

Why: single-buffered, the master composes into the bank being displayed,
so a new picture appears only as fast as it can compose a whole one, and
a long compose shows as a still frame. Double-buffered, every flip
presents a complete frame and the flip rate is the floor.

**What this changes.** The speed gate has been the project's ranking
instrument and it has been measuring the wrong half of the pipeline.
Nothing measured with it is wrong about logic; it is just not the number
a player experiences. From here, rank a presentation change on
`anim_rate.py` and keep `gameplay_speed.py` for what it actually is —
whether the 68K is keeping up with its own frame.

Entry 88's resolution floor applies to both: a trajectory-sensitive
metric on a frame-indexed script. Three windows, not one.
## 94. WHERE THE MASTER'S VBLANK GOES — MEASURED, NOT REASONED

`VBSPAN=1` stamps the FRT through `flip_span`'s pre-flip path into
CEN[24..28] with CEN[29] as the count. ~46 ticks is one scanline; the
edge guard is 1650 (35.9 lines). Double-buffered line, 3000 frames,
CEN[10]=1:

    boundary                          ticks   lines   delta
    ISR entry -> flip_span entry       2997    65.1   +65.1
    after the palette drain            3009    65.4    +0.3
    after the page merge               3028    65.8    +0.4
    after the truth drain (cap_drain)  4316    93.8   +28.0
    edge guard                         1650    35.9

**THIS TABLE IS WRONG. Corrected in entry 96 — read that instead.** Two
faults: `flip_span` is also called from the body fallback, where
`visr_t0` is a stale ISR stamp, and the rows had different denominators
because a stamp was committed as it was reached rather than buffered
until the call finished. DIAG[47], the exit this entry blamed, is
measured at ZERO. The entry is kept because the mistake is the point:
the numbers looked coherent and were not.

## 95. NEGATIVE: PER-BANK RESTORE FRESHNESS BUYS 2 LINES AND NO SPEED

`restore_pages` replays TILEMAP_U truth into the bank the flip just
handed us, for `cycle_dirt | pg_watch`. Measured per flip:

    pages restored          2.35 of 13
      from cycle_dirt       0.10
      from pg_watch         2.25
    restore span           18.3 lines

96% of the work is watched pages, not dirtied ones, which looks like
pure waste: a page whose truth has not changed since it was last written
into this same bank is being rewritten with bytes already there.
`PGFRESH=1` tracks that — a page is fresh in a bank once restored into
it and stops being fresh in BOTH the moment `cap_page` sees its truth
change, which is the whole invariant because nothing else writes truth.

    pages restored          2.35 -> 2.05
    restore span            18.3 -> 16.3 lines
    logic rate              30.3% -> 29.6%   (no change; entry 88 floor)

**It is not waste.** The watched pages genuinely change truth almost
every cycle — they are hot streams, which is exactly why PG_STICKY
watches them. The restore is doing necessary work and 16.3 lines is
close to irreducible under this design. Kept default-off with this entry
attached so nobody re-derives it.

**So the 20-point gap between single- and double-buffered is not one
thing, and it is not the restore.** The master holds FM from the post to
its ack, the game's gated writers spin for all of it, and double
buffering adds the whole ~94-line flip path to that window. The two fat
terms are the 65-line wait for the 68K's window and `cap_drain`'s 28.
## 96. THE VBLANK BUDGET, MEASURED CORRECTLY THIS TIME

Entry 94's table was wrong twice over. Fixed: `flip_span` is reached
from the body fallback as well as the ISR, and only ISR calls have a
meaningful `visr_t0`; and the stamps are now BUFFERED and committed only
if the call reaches the FS write, so every row shares one denominator.
`VBSPAN=1`, double-buffered line, 3000 frames, CEN[10]=1, 821 samples:

    boundary                        ticks   lines   delta
    ISR entry -> flip_span entry     1240    26.9   +26.9
    after the palette drain          1264    27.5    +0.5
    after the page merge             1298    28.2    +0.7
    after the truth drain            2426    52.7   +24.5
    slave PICKED UP the capture      3890    84.6   +31.8
    slave FINISHED the capture       4088    88.9    +4.3
    at the FS write                  4100    89.1    +0.3
    edge guard                       1650    35.9

Three real terms, and the largest is not work:

  1. **26.9 lines waiting for the 68K's k2 window.** Irreducible from
     the master's side — the capture cannot run before the game has
     finished writing text, and the post is what says it has.
  2. **24.5 lines in `cap_drain`.** Real work, correctness-critical.
  3. **31.8 lines waiting for the SLAVE to notice a mailbox**, for a
     capture that then takes 4.3. The master delegates a 4-line job and
     waits 32 for the pickup. `TEXTCAP_SLAVE`'s design note says the
     post-before-drain / join-after-drain overlap shortens the window;
     it does, and the slave is still 31.8 lines late.

## 97. THE FALSE POSITIVE: A CORRUPT BUILD SCORED 3x ON THE NEW GATE

`TEXTCAPMASTER=1` (added here, default off) runs the text capture inline
instead of posting it to the slave. It removes exactly the term entry 96
identified:

    pre-flip path         89.1 -> 57.8 lines
    logic rate, dbl-buf   30.3% -> 34.8%
    logic rate, shipping  49.7% -> 49.3%
    screen updates/sec    11.7 -> 36.7      on the shipping line

Three times the animation at the same logic rate, which would have been
the largest presentation win in this log.

**The frame is confetti.** The bottom third of the shipping-line build
is red/white/blue noise, the player sprite is corrupt, the HUD icons are
wrong. On the double-buffered line the same flag renders a good frame
with a mangled text layer. The inline path is bit-rotted; it was written
for an older pipeline.

**Nothing automatic caught it.** The numbers that looked fine:

    metric                      good frame   confetti frame
    distinct colours                    95              61
    fraction pure black                18%             21%
    colour transitions per row       126.3           124.1

Colour count, black fraction and a transition-density noise metric are
all in range on the broken frame, because the game's own art is busy.
`anim_rate.py` scores it HIGH precisely because noise changes every
frame.

**The rule this forces:** `anim_rate.py` counts change, not
correctness. No cheap metric here separates a good frame from a corrupt
one. Before believing any presentation number, LOOK at a frame, or run
`tools/attract_parity.py` against the arcade corpus, which is the only
oracle that judges pixels. The tool now prints this in its own output.

Standing state after this: the shipping line and the double-buffered
line of entry 93 are the only two configurations with verified-good
frames. `TEXTCAPMASTER` is kept default-off with this entry attached.
## 98. THE RE-MEASUREMENT: EVERY NEGATIVE HELD, AND THE GATE IS DETERMINISTIC

Mike's instruction: re-measure what was crossed off, and do not mark it
measured without counting twice. Protocol: each flag built CLEAN TWICE,
independently, three disjoint windows per build, a frame captured from
each. Result counts only if both builds agree.

    flag        pass 1 windows      pass 2 windows      verdict
    baseline    58.6 87.6 98.9      58.6 87.6 98.9      —
    PALNOCMP    45.4 51.7 57.0      45.4 51.7 57.0      NEGATIVE HOLDS
    FBXTAIL     50.0 50.0 50.0      50.0 50.0 50.0      HOLDS (a cadence
                                                        lock, not a rate)
    CLAIMNEW    53.1 72.7 97.6      53.1 72.7 97.6      HOLDS

**Both passes are byte-identical in every case, from roms with different
md5s.** So the gate is perfectly deterministic for a given source; the
18-point spread of entry 88 is sensitivity to code LAYOUT, not run-to-run
noise. That is a sharper and much more usable rule:

> An A/B is trustworthy exactly when the two builds differ only in the
> thing under test, and untrustworthy the moment a change moves code
> around. Two clean builds of the same source will agree exactly.

PALNOCMP is slower in all three windows, so LOOP27 80a stands.

## 99. TWO CLAIMS OF MINE, BOTH WRONG THE SAME WAY

Both were totals read as rates. Recording the pattern, not just the
corrections.

**"System 16 has 2048 palette entries against our 256, an 8:1 squeeze."**
The 2048 is the size of palette RAM, not the number of colours in use.
Measured: 294-314 distinct VALUES live at once, and the arcade's own
frames (`ref_arcade`, ten samples) show at most **112 distinct colours on
screen**, typically 17-106. Against 256 slots that is 2x headroom, not a
squeeze — **on the 32X layer**.

**But the sky is not on the 32X layer.** The background runs through the
Mega Drive plane, which has `MDP_LINES` = 3 lines x 16 pens = 48, and a
tile may draw from ONE line. An S16 background tile is 3bpp, so <= 8
pens, so six sets fit without eviction. Distinct colour sets ON SCREEN
per vint, 4188 vints:

    <= 6  (fits, nothing to evict)   70.7%
    7-12                             22.9%
    13-24                             6.3%
    25+                               0.1%
    mean 5.72, max 45

So the allocator has real work about 29% of the time and Mike's lookup
table is right for the 32X layer and wrong for the MD plane. What the
shipping build actually pays, though, is small: `MDSTATIC`'s per-scene
pinning means only **87 set assigns in 4200 frames**, with 93
nearest-colour substitutions — 93 wrong colours per 70 seconds, each
persisting until reassigned. Real, and not the big lever.

**"Patching three routines deletes 136,411 writes."** True as a total and
misleading as a rate. Tile-staging writes per frame over 3000 frames:
mean 162, median 116, p90 197, max 10856. **Ten frames (0.3%) carry 19%
of all the writes** — the clears and the RLE loader are level-load
bursts, and the display gate blanks loads anyway. Amortised the patch is
45 writes a frame. Worth doing, not a pipeline fix.

## 100. THE INLINE TEXT CAPTURE, ALMOST

Entry 97's `TEXTCAPMASTER` rendered confetti. The cause is one line: the
inline path carries an R60 alternation that captures text every OTHER
frame, while the slave path it replaces captures every frame. Switching
the capture to the master silently halved its rate, and the restore then
spread half-stale truth into both banks. `TEXTCAPFULL=1` restores the
rate.

    build                              logic   updates/sec   frame
    shipping                           49.7%      11.7       correct
    shipping + TCM + TCFULL            49.6%      12.7       one bad object
    double-buffered (C)                30.3%      25.3       correct
    double-buffered + TCM + TCFULL     34.9%      36.0       one bad band

On the shipping line the saving buys nothing, because that line declines
almost every flip anyway and the pre-flip path's length never matters.
On the double-buffered line, where the flip actually happens, it is
**+4.6 points of logic and +10.7 screen updates a second** — the best
presentation result in this log.

**And it still has a defect**, so it is not a win yet: a band of the
stone wall renders as black-and-white garbage. HUD, sprites, background
and text are all correct.

**Hypothesis for that band, untested:** the join we deleted was also
acting as a barrier that let the SLAVE finish its blit half. Without it
the master reaches the flip earlier and reveals a band the slave had not
written. If so the fix is to wait on the slave's BLIT rather than on its
text capture — a much shorter wait — not to put the capture back.

That is the first thing to test next.
## 101. THE "CORRUPT BAND" WAS THE GAME'S OWN FENCE

Entry 100 called a defect on the double-buffered inline-capture build: a
band of the stone wall rendering as black-and-white garbage. It is the
balustrade. The unmodified control build renders the identical fence at
the identical frame. I read game art as corruption, which is the third
false reading in this log and the second one caught only by taking a
control.

The `TEXTCAP_DUAL` diagnostic (inline capture AND the slave post+join,
so the barrier is kept and only the capture moves) was built to separate
"the join was a barrier" from "the capture is wrong". It answered a
question that did not exist. It is kept, default off, because the
question WILL exist again the next time a barrier is removed.

## 102. THE ARCADE SCORECARD ON BOTH CANDIDATES

`tools/attract_parity.py` against `ref_arcade`, mean |luma| diff per
scene across a lag ladder. The control is the double-buffered build of
entry 93; the candidate adds the inline capture at full rate.

    scene           control (lag 0 / best)   candidate (lag 0 / best)
    boot card              30 / 25                 31 / 26
    logo rewrite           21 / 20                 59 / 29
    logo red               75 / 27                 75 / 28
    cut black               5 / 0                   5 / 0
    demo scene            132 / 48                132 / 48
    face                  106 / 99                105 / 99
    eye                   110 / 105                111 / 105
    eye pan               113 / 109                109 / 109
    demo 2                 46 / 46                  48 / 46

Two readings, and the second matters more than the first.

**On the candidate:** no parity regression except `logo rewrite`, which
is worse at low lag (59 vs 21) and recovers by lag 12. Every other scene
is within a point or two. So the +4.6 logic and +10.7 screen updates a
second of entry 100 cost nothing measurable in pixels.

**On the port as a whole:** `face`, `eye` and `eye pan` sit at 99-113
mean luma diff across EVERY lag column, in both builds. That is not a
timing lag, it is a scene that does not match. `cut black` collapses to
0, so the rig and the alignment work. **Three attract scenes are simply
wrong and have been all along**, and no speed work touches them. That is
the largest untouched parity gap in the project and nothing in this log
was aimed at it.

## 103. STANDING STATE AT THE END OF THE NIGHT

    build                                  logic   updates/s   parity
    ship-us FBXPORT=1  (accepted)          49.7%      11.7     control
    + FBXSTAGE FBXBOTH FLIPEDGEOFF         30.3%      25.3     control
    + TEXTCAPMASTER TEXTCAPFULL            34.9%      36.0     no regression

`rom/U_dblfast.32x` is the third line, stamped 8f0b0526, for the MiSTer.
It is the first build that both flips and presents 36 screen updates a
second, and its logic rate is still well below the accepted line's.

Next, in the order I would take them:

  1. **The face/eye scenes.** Largest measured parity gap, untouched,
     and the oracle rig already scores it.
  2. The 26.9-line wait for the 68K's post and `cap_drain`'s 24.5 —
     what is left of the vblank budget after the slave wait is gone.
  3. The tile-routine patch, as a level-load smoothing item, not a
     pipeline fix (entry 99).
## 104. THE MISSING TITLE SCREEN, AND WHY IT IS A NAME-TABLE UPDATE BUG

Entry 102 called the `face` / `eye` / `eye pan` scores the largest
untouched parity gap. Diagnosed. It is not timing and not a lag.

**What the arcade shows** at attract frame 1360: a full-screen brown eye,
the ALTERED BEAST logo in blue, CREDIT 1, (C)SEGA 1988.

**What we show** at the aligned frame: the DEMO's graveyard background,
still animating, with the logo (white, not blue), INSERT COIN and the
copyright drawn over it. The eye never appears, at any frame.

**The game is not at fault.** Our own tilemap truth between a demo frame
and the title frame:

    TILEMAP_U demo f1000 vs title f1834   86.8% of bytes differ
    nonzero words                         11639  ->  2424
    MD VRAM                               17.5% of bytes differ
    32X CRAM                              262 of 512 bytes differ

The game rewrote the tilemap, our capture took it, and both planes'
hardware state moved. The title map is SPARSE — the eye plus a lot of
cleared cells — and the cleared cells are where the graveyard survives.

**So cells that go to zero are not being updated.** The name-table pass
blanks an empty cell only on the FOREGROUND (`isfg && (w == 0 || w &
0x8000)`); on the background an entry that became 0 keeps whatever slot
it had. The previous scene therefore survives underneath every new one.

**`BGBLANK0=1` proves it and is the wrong fix.** Blanking a zero
background cell makes the title screen appear — eye, blue logo, correct
text, structurally right. And it breaks the demo: the grass band turns
to confetti, because **tile code 0 is a REAL TILE** (it is the grass),
not a sentinel for "empty". Blanking it deletes legitimate art and
exposes the 32X framebuffer underneath, which holds stale bytes in rows
the MD plane normally covers.

**The correct fix is neither skip nor blank: a cell that changes to 0
must be UPDATED to draw tile 0.** That renders the title screen's cleared
field as tile 0 of colour set 0 (which is what the arcade's own hardware
does) and leaves the demo's grass intact. The bug is that the update path
treats 0 as "nothing to do".

`BGBLANK0` is kept default-off as the proof, with this entry attached.

**Second finding, free:** the confetti that `BGBLANK0` exposes in the
demo's ground band is the same signature entry 97 saw under
TEXTCAPMASTER. There is standing garbage in the 32X framebuffer's lower
rows which the Mega Drive plane normally hides. It is not caused by
either flag; both merely uncover it. Worth its own probe.

**Third, and it changes the colour work:** with the eye actually drawn,
its palette is visibly wrong — the arcade's browns render as red, white
and navy. That is the Mega Drive plane's 48-pen limit meeting a
full-screen image, which is exactly the pressure entry 99 measured at
7-45 sets on 29% of vints. The allocator's cost has now been SEEN, not
just counted.
## 105. RULED OUT ON THE WAY: SH-2 CACHE COHERENCY

The compose walks `TILEMAP_C` at 0x06019000, the CACHED alias of
`TILEMAP_U` at 0x26019000, and `cap_page` writes truth through the
uncached alias. That is the shape of a classic write-uncached /
read-cached staleness bug and it would explain the surviving graveyard
exactly.

It is not the cause. `cache_purge()` runs every window (m_main.c 9582,
9648) and again whenever pages or maps change in-window (12008, 12027).
Recorded so the next session does not spend the same hour on it.

What is left, and where to look next: the MD RESIDENCY ALLOCATOR's
`md_tag`, which by its own comment keeps a slot "as long as it stays on
screen". A cell whose tilemap word became 0 may be holding its claimed
slot rather than releasing it, and the name-table pass is chunked
(`build_maps_chunk`, 4 chunks), so a scene change is spread over
several windows. Neither explains a graveyard that survives 200+ frames
on its own, so the first probe is to instrument what the name-table pass
actually writes for a cell that went to zero.
## 106. THE BACKGROUND NAME TABLE IS NOT REACHING PLANE B

Entry 104's diagnosis was wrong in its last step and the probe found the
real one. Instrumented and dumped at the same frame, demo (f1000) vs
title (f1834):

    what                                    demo        title
    tilemap truth, nonzero words           11639        2424
    truth pages holding data              p0..p12    p0-p3, p10-p12
                                                     (p4-p9 all zero)
    page-select words (0xFF8E80)      0101 5656    0101 5656  UNCHANGED
    our SDRAM name table, distinct BG slots   341           4
    our SDRAM name table, distinct FG slots    52           1
    MD VRAM Plane A, distinct entries         148           2
    MD VRAM Plane B, distinct entries         338         207

Read the last two rows against the two above them.

  - The page selects do NOT change, so both scenes use BG pages 0/1 and
    FG pages 5/6. The game rewrites those pages' CONTENTS; at the title,
    pages 0/1 hold the eye and pages 5/6 are empty.
  - **Our name-table pass is correct.** It produced 4 distinct BG slots
    and 1 FG slot at the title — a near-blank field, which is right for a
    sparse title map.
  - **Plane A followed it (148 -> 2). Plane B did not (338 -> 207).**

So the background name table is computed correctly in SDRAM and never
reaches Plane B. Plane B keeps the demo's graveyard and draws it over the
32X framebuffer, which had the eye all along — which is why `BGBLANK0`
appeared to "fix" the title screen in entry 104. It was not drawing the
eye; it was getting the stale plane out of the way.

**That also explains the demo regression.** Blanking exposed the
framebuffer everywhere, including rows where the MD plane is the only
thing drawing, so the grass vanished. `BGBLANK0` is not a partial fix, it
is a different bug's workaround, and it stays off.

**The bug is the BG name-table upload path to Plane B**, not the walk,
not the capture, not the cache, not the page selects. Every one of those
is now measured innocent. Plane A's upload works from the same pass, so
the two paths differ and that difference is the whole defect.

Next probe, and it is a narrow one: instrument the upload for Plane B —
how many cells it ships per window and which — against Plane A's, at the
frame the scene changes.
