# LOOP 20 — CLOSED. HANDOFF AT THE TOP; history below.

**Fresh session: read this block, then `CLAUDE.md` ("What MAME is for
now"), then skim the sections below in order. `docs/log/LOOP19.md`/`docs/log/LOOP18.md`
are the prior arcs. Where LOOPs and ARCHITECTURE.md disagree,
ARCHITECTURE wins. Memory `release-bar-flawless` is the bar.**

## WHERE THE PORT STANDS (2026-08-20, end of LOOP20)

**CANONICAL — a real 30Hz display; 20Hz is DEAD by Mike's order
("kill the 20hz. keep working from 30, and we will make it to 60"):**

    make MDBGALL=1 BQCHUNK=1 NTWRAP=1 WIN2=1 SPRTRUNC=1 \
         BLITSKIP=1 DIRTYROW=1 SPRBAKE=1 BANDSHIFT=16 \
         FBSPR=1 FBTEXT=1 CUT30=1

Rom on disk = `rom/test/Q_cut30_harvest.32x` (BUILD 6b17f86d).
**ALWAYS `python3 tools/build_id.py show` + check the state's BUILD
before believing anything.**

The three-day arc in numbers (all ares unless marked):

      LOOP18 open              -> LOOP20 close
      cadence     20Hz            30Hz (2.05 vints/cycle measured)
      68K handler 91.3            69.3 (at 20Hz-equivalent base)
      game gets   65%             ~74% of the MD 68K
      68K tail    46.2            21.1  (the DREQ push halved twice)
      sprite lag  +1 vint         in place, one vint fresher

**What did it: the TRANSPORT ARC.** The 68K was spending 56% of its
stall narrating video RAM over the DREQ FIFO. The game's sprite upload
and text RAM now live IN the framebuffer window and the SH-2 reads them
in place (FBSPR / FBTEXT); the text capture runs pre-flip at k2 with a
post-flip restore (text is written sparsely — the restore is mandatory,
and the post-flip capture self-poisons: both were hit and are documented
below). The push shrank 596->92 (sprite) and 852->340/84 (text).

## GATES — READ BEFORE MEASURING ANYTHING

  - **MAME is COLOUR-BLIND on the canonical bundle** (partial DREQ
    landings read 0 there; palette pairs never apply). Pixel-gate on
    `N_fbtext` (pre-harvest, full packets — MAME-clean at 62.37 vs the
    arcade, the best of the arc). Everything else gates on ares.
  - Shipping rom (`make`) unchanged all arc: `_end 0x06018138`,
    statics title 2.44 dx=0 / eyehold 3.37 dx=0 exact.
  - Region guard: `grep ' _end$' rom/s16.lst` < 0x06019000.
  - Headless MAME: ALWAYS `-window -resolution 160x120
    -keyboardprovider none -bench N` (else it takes Mike's screen), and
    scripts still call manager.machine:exit().

## STILL OWED / OPEN

  - **A Q savestate from Mike**: the 30Hz falsifiers (skips, drops at
    the 2-vint gate) are confirmed only as MAME rankings + his play
    verdict; no hardware state_health yet.
  - **Flicker fusion is the next build item** (design below): 30Hz
    phase-LOCKS Zeus (solid or invisible — Mike confirmed on P).
    Cadence cannot fix it; the stipple can, at any Hz.
  - A cutscene cut has never been re-tested since CUTBLANK came out
    (LOOP19 owed it; still owed).
  - Punch-list residue: slow load-in (tile cut drain — real transport
    design item), right-seam overdraw (tile miss policy at the leading
    edge, localised, small), colour items 8/9/10 (ORACLE-CHECK FIRST —
    the gravestone yellow flash turned out to be the arcade itself).

## THE ROAD FROM 30 TO 60 (committed analysis, not aspiration)

The drawing itself fits a 60Hz frame twice over (blit 3.75ms + compose
2.63ms = 38.3% of 16.66ms). The binding costs, in value order:
  1. **The window** (~48 lines/cycle): blit 56 rows/CPU + flip + drain
     + restore + capture. P PROVED naive slave-offload buys ZERO — the
     join waits out the dependency (LOOP 9's lesson, re-learned twice).
  2. **The remaining push** (~21 lines/vint): palette-in-place next,
     BUT palettes are NOT rewritten wholesale per vint — it needs a
     real coverage design, not the sprite trick. Then the prefix, then
     the DREQ protocol dies.
  3. **Compose depth**: sprites are 2.19 of 2.63ms; the ZOOMED path
     (Zeus) has never been instrumented.

---
(Original kickoff and the arc's working log follow.)


Read this, then `CLAUDE.md`, then `TOOLKIT.md`. `docs/log/LOOP19.md` is the log of
the palette work this supersedes; `docs/log/LOOP18.md` is the blit arc.

**Canonical build** (unchanged, Mike-validated):

    make MDBGALL=1 BQCHUNK=1 NTWRAP=1 WIN2=1 SPRTRUNC=1 \
         BLITSKIP=1 DIRTYROW=1 SPRBAKE=1 BANDSHIFT=16

handler mean 82.5, game ~69% of the MD 68K, skips 0.1%, pen drift 215.

## THE MEASUREMENT THAT REFRAMES EVERYTHING

Mike: *"There is zero reason a game from the 80s cannot run on a 25MHz
dual CPU pair under 60 frames a second... There are FIXED sprites and
tiles. We aren't redrawing a new engine."* He is right, and the numbers
say so plainly.

**The drawing already fits in a 60Hz frame, twice over:**

      one 60Hz frame                16.66 ms
      blit (both CPUs, concurrent)   3.75 ms   22.5%
      compose (tiles+sprites+text)   2.63 ms   15.8%
          of which sprites           2.19 ms   13.1%
      TOTAL DRAWING                  6.38 ms   38.3%
      headroom left                 10.29 ms   61.7%

We ship that at 20Hz. **The drawing occupies 12.8% of the time we
actually give it.** The machine idles ~87% of every cycle.

**And the 68K's stall is not drawing at all:**

      46.2 lines/vint   56%   shipping video state to the SH-2s
      19.6 lines/vint   24%   the blit
      16.7 lines/vint   20%   flip / pages / CRAM / waits

**TAIL_PROBE decomposes that 46.2 (MAME, MD-side counters are honest —
read as a ranking):**

      handler       64.4 lines/vint
      window/ack    16.9
      TAIL          47.5
        DREQ push   48.6      <- the ENTIRE tail
        palette      0.3
        other        ~0

**The tail IS the DREQ push.** Not the game's logic, not the palette
scan — the 68K narrating its own video RAM into a FIFO, for a frame it
had already fully described.

## WHY THE PUSH EXISTS, AND WHY THAT MATTERS

The game's video RAM is ALREADY mapped into the 32X framebuffer by
`tools/patch_game.py`:

      FB_STAGING  0x24012000   game tile RAM
      FB_SPR      0x2401E000   game sprite RAM

**The tilemap is read in place** — `copy_pages` reads FB staging
directly. **`FB_SPR` is defined and NEVER READ.** Nothing in the tree
uses it; every sprite reader goes through `SPR_SNAP`, which is filled
from the DREQ landing.

The source records why, at the `SPR_SNAP` definition:

>  512-word sprite-list snapshot: FB staging is BANK-DEPENDENT and the
>  access bank isn't fs0 after the flip (ares defers the restore latch)
>  — all sprite readers use this copy

and for the palette, at `PAL_SH`:

>  FB staging couldn't carry it: MD FB-window writes are dropped by
>  arbitration when the SH-2 owns the FB (ares/hardware strict, MAME
>  lenient), and per-bank staging left never-written rows as zeros —
>  the black-actor family.

So there are TWO distinct blockers, and they are not the same problem:

  1. **BANK DEPENDENCE.** The FB window maps one of two banks; after a
     flip the SH-2 reads the other one. This is what made the sprite
     snapshot read zeros. **Presentation 2.0 changed the flip protocol
     entirely** (one flip per cycle at k2, `fb_draw_par` now tracks the
     draw bank explicitly for BLITSKIP) — the bank is no longer unknown
     to us. This blocker may simply be stale.
  2. **ARBITRATION DROPS.** MD writes into the FB window are dropped
     while the SH-2 owns the FB. This one is hardware behaviour and is
     NOT stale. It bounds when the game may write, not when we may read.

## THE JOB

**Determine whether the SH-2 can read the game's sprite list in place at
`FB_SPR`, and under which bank and window phase.** If it can, the
largest single item in the entire pipeline disappears.

Order:
  1. **Probe, do not assume.** Read `FB_SPR` from the SH-2 across every
     window phase and both bank parities, and compare word-for-word
     against the DREQ-delivered `SPR_LAND`. That is a pure read probe:
     no behaviour change, and the DREQ copy is the oracle sitting right
     there. Count mismatches per phase/parity.
  2. If some phase agrees, **narrow the push to what still needs it**
     (text, palette) and measure the tail again. Sprites are 84+8*nrec
     words of it; text is 340.
  3. If NO phase agrees, blocker 1 is real and the answer is the bank
     protocol, not the transport — say so and stop.

**Do not start by writing code.** Every LOOP19 fix was built before the
constraint was measured, and four of four were wrong.

## STEP 1 DONE (code audit), STEP 2 BUILT AND MAME-GATED

**Step 1 needed no probe.** The FM envelope is airtight in the current
source: ONE raise site (`md_main.c:479`), the 68K spins inside the whole
FM=1 span, and FM drops before every handler exit INCLUDING the spin
timeout. The comment states the design intent outright: "FM stays 0
outside so the GAME's staged writes land." Game code cannot execute
with FM=1, so the write-discard hazard that forced the 0xFF7000 reroute
is extinct by construction. (An FBSPR_PROBE run had already shown 89% of
landed records absent from FB_SPR — that measured the ABANDONED address,
not the hazard: patch_game moved the upload to 0xFF7000, so nothing
writes FB_SPR today. The probe stays for re-use but its number is about
the old mapping.)

**Step 2 is `make FBSPR=1`,** three deliberate properties:
  - patch_game remaps sprite RAM back to FB staging (0x85E000), env-var
    plumbed, and `game_body.bin` now depends on FLAGSTAMP so flag flips
    rebuild the patched game (the .build_flags trap, closed for this
    artifact).
  - The SH-2 fills SPR_SNAP from FB_SPR in-window (FM held, so the read
    is legal; the window maps the draw bank the game just wrote). It
    stops past the game's own terminator. No restore machinery: the game
    rewrites the full list every vint, so by k1 the current bank has a
    complete upload.
  - **The DREQ push is left fully intact and its sprite payload
    ignored.** This build gates CORRECTNESS only; the 68K saves nothing
    yet. Step 3 (dropping the sprite packet — the ~48-line prize) waits
    for the ares verdict.

**MAME gate: PASS, and slightly BETTER than the baseline vs the
arcade** (same bundle, only FBSPR differing):

      scene      no FBSPR   FBSPR
      title        65.16    58.61
      scream       91.18    91.46
      eyehold      40.13    40.34
      demo         79.66    79.25
      demo2        81.88    80.55
      TOTAL        71.60    70.04

Anchored self-diffs between the two builds are NOT a valid gate here —
the flag makes the sprite list one frame FRESHER by design, so frames
differ by animation phase. Against the arcade, fresher shows up as
closer, which is what the table says. Shipping rom unaffected: statics
2.44/3.37 dx=0 exact.

**THE DECISIVE GATE IS ARES AND ONLY ARES:** the hazard under test is
"ares/hardware strict, MAME lenient" — MAME does not drop the MD's FB
writes, so it cannot prove they land on hardware. `rom/test/M_fbspr.32x`
(stamped FBSPR, canonical bundle + the flag). If sprites are intact on
ares — no torn records, no vanishing actors — the 40/64 era is formally
closed and step 3 collects the 48 lines.

## STEP 3: ARES-VERIFIED IN PLACE, PUSH SHORTENED — AND THE HONEST NUMBER

**Mike's M_fbspr pass: same known issues, nothing new torn, IMPROVED
speed feel.** The write-discard hazard did not fire — the 40/64 era is
formally closed. (The feel improvement is real even with the push
intact: the in-place list is one vint fresher, a full frame less input
latency on sprites.)

Step 3 shortens the sprite push to prefix + 1 junk record + tail = 92
words (the SPRSHORT_OK minimum; the tail stays at landed-2, the SH-2
never reads the record). NOTE: nests inside SPR_TRUNC — FBSPR without
SPRTRUNC still pushes the fixed 596.

**TAILPROBE (MAME ranking): DREQ push 48.6 -> 44.7 lines/vint. Modest,
and the reason reframes the next step: SPRTRUNC had ALREADY collected
most of the sprite-push win (596 -> ~184 mean). The tail is now TEXT +
PALETTE transport** — two 340-596-word packets per cycle (text chunk,
palette pairs) against one 92-word sprite push.

**The same in-place logic extends to both, and the FM audit that
unlocked sprites applies unchanged:**
  - TEXT: the game writes 0x410000 -> 0xFF8000 mirror; remap to FB
    staging and read in place, killing the 256-word text chunk.
  - PALETTE: 0x840000 -> 0xFF9000 mirror (the bank-skew fix predates the
    FM audit, same as the sprite reroute); in place kills the 512-word
    palette pairs. The bank-skew concern needs the same
    full-rewrite-per-vint check sprites got — palettes are NOT fully
    rewritten every vint (fades touch a few words), so this one DOES
    need the restore/coverage story thought through, not assumed.
  - Endgame: prefix regs/rowscroll too, then the push dies entirely and
    the 68K handler is ~20 lines of window/ack + nothing. That is the
    CUT30 funding, and the 60Hz conversation starts there.

`rom/test/M2_fbspr_short.32x` (FBSPR stamp) is the step-3 ares build.
MAME CANNOT PIXEL-GATE IT (SPRTRUNC + short pushes read landed==0 there,
prefix apply skips, regs go stale — the known class); ares only.

## M2 ON ARES: FBSPR IS CANONICAL. Mike: "VAST improvements in speed
## and smoothness. still not 1:1 yet."

      M2 state: handler 82.6, rejects 0.1%, skips 2.0%, protocol clean
      (dreq_incomplete 0, misaligned 1), pen drift 1902/4783 cycles.

**The counters barely moved (82.5 -> 82.6) and the FEEL moved a lot** —
which is itself a finding: the vast improvement is LATENCY, not
throughput. The in-place list cut a full vint of sprite lag, and the
68K's input->screen chain shortened accordingly. The handler mean was
never measuring that. FBSPR is folded into the canonical line
(DEVNOTES updated).

### "Not 1:1 yet" — what stands between here and the arcade

  1. **Cadence: we are 20Hz against the arcade's 60.** The remaining
     unavoidable gap. The road is the rest of the transport: tail is
     44.0 (text + palette packets). Text in place joins the EXISTING
     tilemap page machinery (thunk-marked dirty pages, cap_drain truth,
     restore_pages) — text is NOT rewritten every vint, so unlike
     sprites it needs the coherency treatment tiles already have.
     Palette same, with its quarters-copy retired. Endgame: the push
     dies, the handler is ~20 lines of window/ack, CUT30 becomes
     affordable, and 30Hz -> 60Hz is a scheduling argument, not a CPU
     one.
  2. **Zeus scaling** (items 3/5): the zoomed path, never instrumented.
  3. **The punch-list residue**, triaged in LOOP19 — with the oracle
     check first for anything colour-shaped.

## THE ZEUS ORACLE (mamecap/, 553 arcade frames): ITEM 4 IS CADENCE
## ALIASING, AND THE SCALE TARGET IS QUANTIFIED

Mike captured the full arcade Zeus sequence. Two findings:

**1. The arcade draws Zeus's transparency by 60Hz FLICKER.** The orb
reads present on alternating frames exactly (orbH: 25,0,26,0,27,0...)
and the white-pixel counts show the whole apparition doing the same
(~100 statues-only alternating with ~400+ statues+Zeus). 50% duty at
60Hz = translucency on a CRT.

Sampled at our 20Hz (every 3rd frame), a 50%/60Hz flicker PHASE-LOCKS:
snapshot on the OFF phase = invisible Zeus (punch-list item 4, exactly),
on the ON phase = solid opaque Zeus, drifting = 3-frame appear/vanish
blocks (the fade-out artifacts in items 3/5). **No palette, sprite, or
zoom fix can touch this class. Only cadence can.** Item 4 is formally
reattributed from "sprite bug" to "cadence aliasing", and any punch-list
item involving blink/fade effects needs the same suspicion.

**2. The scale curve, measured:** orb 25 -> 49px over frames 33..128
(~1px per 2-4 frames at 60Hz), a breathing 45<->49 pulse through the
hold, 49 -> 24 over 376..488. The arcade's scale stepping is 1px steps;
at 20Hz we inherit 3x coarser stepping BEFORE any bug of ours. The
release-bar "scale stepping is a must-fix" is therefore mostly a cadence
item too.

**Tooling note: FBSPR incidentally restored MAME's ability to render the
canonical bundle's sprites** (the SH-2 reads FB staging, which lenient
MAME fills correctly — the SPRTRUNC-blinds-MAME limitation is gone for
sprite RENDERING; the DREQ-landed prefix items still can't gate there).
Ours-vs-oracle Zeus comparisons can now run entirely in MAME.

## FBTEXT: TEXT RAM IN PLACE — THE BIGGEST MAME PARITY WIN OF THE ARC

`make FBTEXT=1` (stacked on FBSPR): the game's text writes land at FB
0x85F000 — the 4KB slot the DEAD FB_PAL quarters copy vacated (verified:
no SH-2 reader of the 0x1F000 region, and the quarters copy no longer
exists in md_main.c; only its comment survived). The SH-2 captures the
full 2048-word region into TEXT_U at k2 PRE-FLIP and restores it into
the fresh bank post-flip. That kills the DREQ text chunks AND the
80-word regs/rowscroll prefix at once — the S16 keeps its layer regs at
text words 0x740-0x7FF, so they ride the capture and latch same-cycle
instead of one window late.

      scene      baseline   FBSPR   +FBTEXT
      eyehold      40.13    40.34     5.62    <- text-heavy scene
      demo2        81.88    80.55    69.36
      TOTAL        71.60    70.04    62.37

Push still fully intact (payload ignored) — correctness staging, same as
FBSPR. The harvest (shrinking the TEXT packet) comes after ares.

**Two bugs on the way, both caught by probing not staring:**
  1. **Capture ran post-flip** (the snapshot block sits after the k2
     flip), reading the bank the game never wrote; the restore then
     propagated those zeros into BOTH banks — a self-poisoning loop.
     Sprites survive the same ordering only because the game rewrites
     the full list every vint. Text capture MUST be pre-flip, restore
     post-flip. Probe signature: "loop runs, source has data, dest stays
     zero... in both banks".
  2. **Half-size copy**: 512 longs is the SPRITE list's sizing; text is
     4KB = 1024 longs, and the layer regs live in the half the first cut
     missed.

Open item on the ares pass: a red blob actor appeared in one MAME
gameplay frame (ch_2400) — possibly a transient wrong-palette player
pose, possibly nothing. Watch for it.

## 20HZ IS DEAD. CANONICAL IS 30. THE TARGET IS 60.

Mike, on the Q_cut30_harvest verdict: **"yeah kill the 20hz. keep
working from 30, and we will make it to 60."**

Canonical line (DEVNOTES updated):

    make MDBGALL=1 BQCHUNK=1 NTWRAP=1 WIN2=1 SPRTRUNC=1 \
         BLITSKIP=1 DIRTYROW=1 SPRBAKE=1 BANDSHIFT=16 \
         FBSPR=1 FBTEXT=1 CUT30=1

BANDSHIFT re-swept AT 30HZ: 16 confirmed (MAME drops 110, vs 153 at 8
and 428 at 24 — the slave saturates past 16 at the 2-vint pace).
state_health's cadence label now reads 20Hz as DEAD and 60 as the
target. MAME is COLOUR-BLIND on canonical (partial landings); pixel-gate
on N_fbtext, everything else on ares.

### The road from 30 to 60, honestly

60Hz = a window every vint: the whole compose+blit must fit each frame
with the flip inside the gate. The known remaining levers, in rough
value order:
  1. **The window itself** (~48 lines/cycle): blit 56 rows/CPU is the
     floor; DIRTYROW already skips ~35% of rows. The rest is flip +
     drain + restore + capture. P proved naive slave-offload buys zero
     against the join; overlap has to hide a dependency, not create one.
  2. **The remaining push** (~21 lines/vint at 30Hz): palette pairs in
     place (needs the coverage design — palettes are NOT rewritten
     wholesale), then the prefix, then the push dies and the DREQ
     protocol with it.
  3. **Compose depth**: sprites 2.19ms of the 2.63 — the zoomed path
     and the per-pixel loops. SPRBAKE was neutral; the next idea there
     has to be measured against the throughput-bound lesson.

### FLICKER FUSION — the Zeus fix, cadence-independent (NEXT ITEM)

The arcade draws Zeus/orb on alternating 60Hz frames (50% duty). 20Hz
strobed him at 10Hz; **30Hz phase-LOCKS him** — permanently solid or
permanently invisible. Only 60Hz samples the flicker correctly, so at
ANY lower cadence the transparency must be SYNTHESISED: we read the
sprite list every vint now (FBSPR), so compose can detect a record
alternating present/absent across consecutive vints and render it once
per cycle at 50% via checkerboard stipple — the shadow path's machinery,
different mask. True translucency, arguably better than the CRT trick.
Do this before chasing per-scene flicker bugs; items 3/4/5's remaining
weirdness should collapse into it.

## WHAT LOOP19 SETTLED (do not re-run)

  - The sprite pair map is one cycle behind (`build_maps(b->bpar ^ 1)`).
    Confirmed in code. Real, but its VISIBLE cost is now unknown — see
    the oracle finding below.
  - **The gravestone yellow flash is the ARCADE.** Seven solid-yellow
    frames at 86-88% uniform in 70 frames of `mame altbeast`. Confirmed
    independently by Mike. Items 8/9/10 are the same class of colour
    judgement and MUST be checked against the oracle before being
    treated as defects.
  - Four attempted palette fixes, all measured ineffective, all kept as
    OFF flags: the late claim (35 of 583), `GRPRELOC` (1 of 1,915),
    `PAIR_HOLD` (no change), `TILEDEDUP` (17x WORSE pen drift on ares
    despite a clean MAME result).
  - Sprite sets do not compress: exact dedup 8,314 -> 8,313,
    position-wise 1 in 10,849, used-pen 18 of 19 sets use all 14 pens.
  - `CUT30` on the current build: real 30Hz (2.05 vints/cycle) but
    handler 130.9, game 50%, band drops 1.14/cycle. **30Hz needs a 33%
    cut in per-cycle master work** — which is what this loop is about.

## TRAPS

  - **A probe that reads zero: check SPRTRUNC first.** A SPRTRUNC build
    measured in MAME has NO sprite list — `SPR_SNAP[0]` reads as the
    terminator and `compose_sprites` loops over nothing. This cost a
    full debugging round in LOOP19, the third time that trap fired.
  - **Read fixed-block counters at 0x06, not 0x26.** MAME's SH-2
    debugger space does not serve the uncached alias; it returns a clean
    zero. Prove any counter block with a boot sentinel first.
  - **Check the counter slot is free.** Two collisions in LOOP19 made
    two A/Bs unreadable.
  - **MAME ranks structure; ares prices dynamics.** TILEDEDUP looked
    like a clean structural win in MAME and was a 17x regression on
    hardware, because a palette scheme has to be judged over TIME and
    the analysis only ever looked at one cycle.
  - Headless MAME: `-window -resolution 160x120 -keyboardprovider none
    -bench N` or it takes Mike's screen and keyboard.
