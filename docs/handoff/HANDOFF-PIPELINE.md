# HANDOFF — the pipeline is the project

2026-09-08, end of session. Read this first. It supersedes the state
sections of HANDOFF-DREQ.md; that document's transport findings stand,
its framing of the problem does not.

Companions: `docs/design/SILICON.md` (what the hardware requires, every
claim with the build that reproduces it), `docs/log/LOOP27.md` entries
65-84 (the working log, with the negative results).

---------------------------------------------------------------------
## 0. BUILD RULES — READ BEFORE MEASURING ANYTHING

Two bugs invalidated about half a day of measurements. Both are still
live in the tree; neither is fixed, only known.

**`make clean` before every measured build.** `rm -f *.o` is NOT enough:
the baked assets (sprbake.bin, tiles.bin, sprites.bin, game_body.bin)
regenerate only when `.build_flags` changes, and `.build_flags` does not
capture every flag. A build can silently inherit the previous flag set's
asset bake. Two roms from the same flag list differed by 1.3 MB, all of
it the asset region shifted, and each reproduced its own measurement.

    same flags, back to back, make clean   1 byte  (the stamp)
    same flags, rm *.o only                1,287,725 bytes

**Diff the roms before believing an A/B.** A pair differing by 2-4 bytes
is the same build. A pair differing by ~1.3 MB has a stale bake in it.

**This shell is zsh: unquoted `$var` does not word-split.** Any helper of
the form `build() { make ship-us $1; }` called as `build "$FLAGS"` hands
make ONE malformed argument. Type the flags literally on the make line,
or put the helper in a `#!/bin/sh` script, where splitting does happen.

---------------------------------------------------------------------
## 1. THE SCOPE (Mike, this session)

**The 68000 clock is not a loss, and the pipeline is the whole project.**

    the game needs             2780 instructions per vint
    our budget                 127,841 cycles/vint at 7.670 MHz
    so it fits at any cost <=  46 cycles per instruction
    the ARCADE runs it at      45.2 cycles per instruction

The arcade's 10 MHz 68000 is stalled on its video bus, not computing. We
do not pay those stalls, so the slower clock is offset by cheaper access
and the game's own code fits. Measured: our shim costs another 2882
instructions/vint — as much as the game itself — and that is the gap.

The game is EVENT-gated (IRQ4 increments 0xFFF01C, the main loop clears
and spins on it), so execution speed never changes its semantics, only
whether a frame lands. Exactly one pure cycle-delay loop exists in the
whole program (0x2D8C, ~1280 cycles).

Work that reduces or reshapes the pipeline is in scope. Work premised on
the 68000 being too slow is not.

---------------------------------------------------------------------
## 2. STATE — the ladder, clean builds, level-1 script, ares

    build                       game speed   display    V at post
    ship line (DREQ)              48.7%      21.5 Hz       -
    opt1 (DREQ)                   60.7%      11.5 Hz      243
    opt1 + FBXPORT                82.3%       0.3 Hz       43
    opt1 + FBXPORT + FBXTAIL      49.1%      11.5 Hz      239

    opt1 = TXTWRAM=1 LATESTEAL0=1 LATEKEEP=1 DRAWADOPT=1

82.3% is the best game rate this project has measured and it reproduces
from a clean rebuild. It is not shippable: the display refreshes 0.3
times a second.

**ON HARDWARE, AND THIS IS THE LAST THING MEASURED THIS SESSION —
FBXPORT IS REQUIRED FOR THE PORT TO RENDER ON THE MiSTer AT ALL.**
Clean builds, one flag apart, screenshot-verified:

    make ship-us                        1 colour   BLACK
    make ship-us FBXPORT=1             84 colours  renders
    make ship-us <opt1 flags>           1 colour   BLACK

So the FB transport is not only a speed change. Without it the shipping
line is a black screen on silicon, which is NOT visible in ares — ares
renders the DREQ line fine. Everything in section 2's ladder is an ares
measurement; on hardware only the FBXPORT builds are alive at all.

The earlier hardware bisect (FBXPORT 86 / +opt1 85 / +CLAIMNEW 57%
black) used STALE-ASSET builds for two of its three roms and must be
redone with `make clean`. The only clean hardware results are the three
lines above; `+opt1 +FBXPORT` and `+CLAIMNEW` have NOT been retested
clean.

`rom/s16_fast49b.32x` (opt1 + FBXPORT) rendered on hardware but was
built with stale assets — do not trust it until rebuilt clean.

The accepted hardware base remains `mister-keeper-20260908`
(`make ship-us FBXPORT=1`).

---------------------------------------------------------------------
## 3. THE BIND — state it before proposing anything

  - The DREQ FIFO needs no FM and costs **~2.4 scanlines per word** on
    hardware (~0.05 in emulation, which is why this was invisible for
    weeks).
  - The framebuffer costs **~0.05 lines per word** and needs **FM=0**,
    which collides with the master's blit window.
  - The 68K **cannot touch the framebuffer at FM=1 at all** — writes are
    dropped, reads are useless. Proven with a sequenced readback.
  - So the FB push must sit at FM=0, and the only FM=0 window before the
    post is AHEAD of it. The post then moves from V=243 (inside vblank,
    where the ISR can still flip) to V=43 (active display), and every
    flip misses.
  - `FBXTAIL=1` moves the push to the vint tail: V at post returns to
    239 and the display to 11.5 Hz, but the tail runs before the game's
    IRQ4, so the game loses 33 points.

Shrinking the packet does not escape it. The packet is ~150 words to
convey ~85 words of real change (LOOP27 80); 85 words through the FIFO
is still ~204 scanlines.

---------------------------------------------------------------------
## 4. THE NEXT STAGE

**Make the master's FM=1 window shorter and scheduled, so an FM=0 window
exists that is not in front of the post.** That is the only escape from
section 3 that the measurements leave open. It is a scheduling change,
not a transport one.

What that has to satisfy:

  1. The post must land at V >= ~224 so the ISR can still flip.
  2. The 68K's FB write needs an FM=0 window of ~2 scanlines (the write
     itself is cheap; it is the ~56 lines of packet BUILD that cost).
  3. The game's IRQ4 must not be pushed later than it is today.

The obvious decomposition, not yet built: do the packet BUILD where the
push is today (after the post, FM=1, WRAM-only work that overlaps the
master's blit) into a staging buffer, and BLAST staging->FB in the ~2
line FM=0 window before the next post. That costs one vint of packet
latency, which the harvest already tolerates.

**The WRAM blocker is CLEARED (LOOP28 84).** There is not 1.8 KB free,
there is **15,916 bytes**, contiguous, from the linker's high-water mark
`__bss_end` = 0xFF21D4 up to `PAL_SHADOW` at 0xFF6000. Proven twice: full
WRAM dumps at three frames show the region all-zero and unchanged, and
an ares write/read census over 4100 level-1 frames and 9000 attract
frames counts ZERO writes after boot and ZERO reads ever. Declare the
staging buffer as a normal static array; the linker grows .bss into it.
Add `ASSERT(__bss_end <= 0xFF6000, ...)` to `md_src/md.ld` first — there
is no collision guard today.

The palette rotor, RE-MEASURED CLEAN on this line (LOOP28 86): the
ceiling is `PALROTOROFF` = **94.7%** against 82.3%, so the prize is
**12.4 points**, not the 4.1 recorded from the DREQ era. That is the
largest single scheduled cost left in the 68K's vint.

**Do not sweep `PALSTREAK=` / `PALBACKOFF=` again.** They were dead code
until 2026-09-08 (the streak counter was declared, read, and never
written — LOOP28 85); with the counter fixed, a 25-point sweep found no
structure, and the metric cannot resolve it anyway (LOOP28 88, and
section 5 below). The flags are now gated OFF by default behind
`PAL_DIET`.

---------------------------------------------------------------------
## 5. INSTRUMENTS — what to trust

  - **`tools/gameplay_speed.py ROM`** is THE speed gate: game-frames per
    vint on the level-1 input script, 100% = 60 game-frames/s. It also
    reports WRAM 0xFFF144, vints where the game's pass had not finished.
    Attract mode and flip rate are NOT the gate; measuring them cost a
    day.

    **IT HAS A RESOLUTION FLOOR, MEASURED (LOOP28 88).**
    `LAYOUTPROBE=1` adds 64 bytes of unreferenced `.data` and changes no
    behaviour at all; it moves the canonical window from 85.3% to 67.2%.
    Sliced into 700-vint windows, 5.9 of those points are a real floor
    (the port sits on the IRQ4 threshold, so a few cycles of address
    arithmetic flip whole frames across the deadline) and the rest is
    trajectory divergence: a build that falls behind on a frame-indexed
    input script is measured on different content. The ladder's big
    steps (48.7 -> 60.7 -> 82.3) survive that. **A ranking of two builds
    a few points apart does not — do not build on one.** Slice the run
    into windows before believing a gap, and for small effects use the
    handler-mean A/B in `tools/health_mame.lua` or
    `tools/frame_timeline.py` instead.

    The same rom also reads 67.2% over [1500,2600] and 93.3% over
    [2600,4100]: the content in the window differs, not the machine.
    And the scene timer at 0xFFF02A RESTARTS with the scene — before
    2026-09-08 a window crossing that reset printed a number anyway
    (2546.7% on the shipping rom over [3000,5500]). The tool now refuses
    and exits 2. The level-1 script holds one scene from ~f1200 to
    ~f4500; stay inside it.
  - **ares `--trace-flip`** is the display-refresh truth. It comes from
    the emulator, not our memory, and it was the only number that stayed
    consistent all session.
  - **`tools/write_census_ares.py`** counts the game's writes per region
    per frame.
  - **`tools/arcade_trace.lua` + `.py`** trace the arcade 68K in MAME
    exactly (instruction counts, IRQ4 length, where the CPU is at
    vblank). PC sampling and memory taps were both tried and both fail —
    every MAME hook fires at a fixed phase where the game is always in
    its wait loop. Do not re-try either.
  - **SH-2 counters: calibrate every slot.** Writes to DIAG slots above
    ~63 are silently discarded and read back residue. `CEN` at
    0x2602FF00 is the census block, `CENCAL=1` calibrates it, and every
    census build carries `CEN[10]` = 1 at m_main entry. **If CEN[10] is
    not exactly 1, nothing else in that run means anything.** Slots 6, 9,
    11, 12, 14, 15, 16 are aliased.
  - **Bias every value the colour instrument carries away from zero.** A
    flooded d=0 renders identically to a machine that never reached the
    flood.

---------------------------------------------------------------------
## 6. RETRACTED THIS SESSION — do not rebuild on these

  1. "The DREQ push is what starves the 68K." The push is 99 of 262
     scanlines and removing it moves the real gate by 1.6 points alone.
     It was never the binding constraint by itself.
  2. "FB transport delivery is 0.2%." It is 100% (819/819). The alarm
     was a lost counter above the DIAG cliff.
  3. "The flip is not the limiter / 93% refresh." That counted code
     REACHING a flip site; flip_span declines internally. The two differ
     by 78x.
  4. "FBXPORT regresses the flip because of the edge guard." The guard
     is not the cause; the post leaving vblank is.
  5. "CLAIMNEW fixes the shadow-ramp draws." On ares, 704 -> 57. On
     hardware, 57% black. Dropped.
  6. "Ship raw instead of comparing the palette" (PALNOCMP), twice:
     74.1% with the shadow kept, 57.9% without. The compare is
     compression and pays for itself.
  7. The 68000 clock ratio (76.7%) as a frame-rate ceiling. It is not,
     because the arcade is bus-bound.

---------------------------------------------------------------------
## 7. OPEN, UNMEASURED

  - Whether our port executes the same 68K instruction COUNT per game
    frame as the arcade. We patch the video accesses, so it may not, and
    ares gives no 68K instruction count (its profiler is SH-2 only).
  - The arcade's own frame utilisation U. Sampling is phase-biased and
    worthless; the honest measurement is a one-frame debugger trace
    counting instructions inside vs outside the idle loop at 0x003980.
  - The compose block shape Mike can see on screen as small squares.
    Frame diffs show full-width row bands, no column banding, so it is
    not the blit slicing — unidentified.
  - Whether a >4 MB cart helps. The mechanism is understood and
    documented (SILICON.md 4e: SSF2 mapper, "SEGA SSF" header, 8 x
    512 KB slots, the 68K pages and the SH-2 must flush its cache), and
    the cart is FULL at 4.00 MB. But the sprite bake already covers
    96.5% of lookups, so more space buys the remaining 3.5%, not a
    transformation.
