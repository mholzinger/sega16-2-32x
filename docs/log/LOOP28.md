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
