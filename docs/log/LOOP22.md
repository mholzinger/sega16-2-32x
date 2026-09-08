# LOOP 22 — THE 60HZ ARC. 30Hz polish is over by Mike's order.

Mike, closing LOOP21's v2 discussion: *"We have so many screen tearing,
blit and speed issues you've pointed out disappear when targeting
actual 60fps."* He is right, and the code backs him further than the
LOOP20 analysis did: **Presentation 2.0 already removed the vblank
bound from blits** (hidden-bank writes at any time; the ONE FS flip per
cycle is the only vblank-gated operation — see the block comment at
the k2 flip). The 3.75ms blit is NOT a 60Hz wall.

## WHAT DIES AT 60 (do not fix these at 30)

Scale stepping (release-bar item), sprite/input lag, band staleness,
cadence aliasing (all of LOOP21's flicker class), the V-gate drift
arithmetic. These are costs of sub-60 sampling, not defects.
**FLICKFUSE ships only below 60 and must be OFF at 60** — the native
duty dither renders itself there, and the tracker would smooth what
the arcade really flashes.

What does NOT die at 60: the load-in drain (punch items 1/2), colour
judgement items (oracle-first), audio, §11 palette.

## THE TWO BLOCKERS, WITH HARDWARE NUMBERS (R_flickfuse.bs1)

60Hz = one full cycle per vint: compose(N+1) + blit(N+1, hidden bank)
+ one flip inside the V∈[DF,E2] gate. SH-2 drawing fits (6.38ms of
16.66). The 68K does not, yet:

  1. **The FM-held window span**: window/ack mean 75.4 lines/vint,
     worst 240 of 262 — at 60 every vint pays it. Preack showed ~79
     lines is the master not having started; the rest is drain +
     capture + restore + flip the 68K waits out.
  2. **The remaining push** (~21 lines/vint): palette pairs (512w) +
     residual prefix. Sprites are short (92w) and text is dead
     (FBTEXT) already.

## STEP 1 — FBPAL: palette in place (SIZED THIS SESSION)

Arcade ground truth (`tools/pal_diff.lua`, content-diff of
0x840000..0x841FFF over 2400 gameplay frames):

    changed CRAM words/frame: mean 9.7, max 125
    89% of frames change <=16 words; 0.04% change >64

**Sparse, decisively** — so FBPAL is an FBTEXT CLONE, not an FBSPR
no-restore: reroute the game's palette writes (patch_game, currently
0x840000 -> 0xFF9000 mirror) into a free 4KB FB staging slot, capture
the 2048-word region pre-flip at k2 (same size FBTEXT already pays),
restore post-flip. apply_cram reads the capture instead of DREQ pairs;
the 512-word palette packet dies. Open detail: WHICH 4KB FB slot is
free (text took the 0x1F000 hole) — audit the FB map before choosing.

Then TAILPROBE the residual prefix; when it hits ~0, the DREQ protocol
retires and the handler approaches window/ack + nothing.

## STEP 2 — THE WINDOW SPAN

Decompose the 240-line worst (preack_max.py, wait_split.py rigs
exist). Known levers: the ~79-line not-started span (IDLE_TOKEN
machinery exists), capture/drain overlap on the slave (TEXTCAPSLAVE
pattern), restore set size. P's negative result stands: overlap must
hide a DEPENDENCY, not add a join.

## STEP 3 — THE SCHEDULE

Merge k1/k2 into one per-vint window; flip every vint inside the gate;
FLICKFUSE off. Gate each step: TAILPROBE ranking in MAME, ares state
per step (and the Q-baseline savestate is STILL OWED — every handler
number in this file has no 30Hz-canonical hardware baseline until it
lands).

## CARRIED

LOOP21 v2 (`rom/test/R2_flickfuse.32x`) awaits an ares look when
convenient — it validates the tracker mechanics that remain the sub-60
fallback. Not on the 60Hz critical path.

## STEP 1 BUILT: PAL32 (dirty 32-word blocks). MAME-gated, awaiting ares.

`make ... PAL32=1` — three pieces, all flag-guarded, non-flag builds
byte-identical (statics re-run exact, `_end 0x06018138`):
  - **patch_game**: thunks regenerated at 32-word granularity — 8-byte
    block bitmap at 0xFFBA00 (installed all-dirty, data rides at the
    head of pal_thunks[]), `pmark` helper (D0=block -> bset), precise
    thunks A (cycle engine, never straddles) and B (pointer writers,
    marks the straddle pair), static sites as ori.b nibble masks,
    variable-length slots. 756B of 1520B budget.
  - **md shim**: round-robin selection of up to 4 dirty blocks/push,
    ship-twice retry per block, packet = 82 prefix + 4 ids + K*32 + 2
    tail (88+32K, always a multiple of 4); pair channel compiled out.
  - **SH-2**: block apply (ids at words 82..85, payload from 86), same
    per-set change-detect / SETGEN / k!=1 sprite-paint rules.

TAILPROBE A/B (attract, colour-cycle worst case, K pinned at 4 by
ship-twice): MEANtotal 73.9 -> 71.1 lines/vint, worst tail 51 -> 46,
cycles identical. Gameplay mean K≈1.6 should price better on ares —
the pair shipped 256 words on 98.9% of pushes; PAL32's mean payload is
~52+4. MAME stays colour-blind (short pushes, the SPRTRUNC class):
colour truth is Mike's ares pass of `rom/test/S_pal32.32x` (isolation)
or `S2_pal32_flick.32x` (full stack + flicker fusion).

## NEGATIVE RESULTS (this step)

  - **The displaced instruction has exactly ONE valid source: the copy
    the region-pair pass reads (post-remap, pre-jsr), now saved as
    `pal_disp_saved`.** Both wrong sources were built and caught by
    probes: reading `hrom` after the pair pass embedded the pair jsr
    (68K wedged at vint 2170 jumping into the reused thunk area —
    win_probe froze); reading `orig_rom` embedded PRE-REMAP 0x840000
    targets, so every static-site palette write landed in the MD's FB
    WINDOW over the display image (full-screen garbage from attract).
    An image diff (expected: 48 jsr-target bytes) plus a thunk-word
    dump found it in two runs.
  - md_main.o now depends on pal_thunks.h in the Makefile; the
    PAL_THUNKS_PAL32 #error pair caught the stale-header build first.

## ARES ROUND 1: THE WHITELIST — the packet format has FOUR parts

Mike's S2 pass: green background, everything black — boot CRAM, the
palette never applied on hardware (savestate `S2_pal32_flick.bs9`,
BUILD e9dae4f2: dreq misaligned=0, i.e. the magic-tail check never even
ran). Cause: the SH-2's landed-length WHITELIST still read
`340 || 84` — PAL32's 88+32K lengths (120..216) were rejected WHOLE,
silently, every push. Bare 84-word pushes passed, so structure ran and
only colour died.

**The packet format is four coupled pieces: the MD builder, the
published length, the SH-2 whitelist, and the SH-2 apply. The first
PAL32 build changed three.** And MAME cannot catch the fourth: landed
reads 0 there, the whitelist never executes — a whitelist miss is
INVISIBLE everywhere except hardware. Rule recorded at the check
itself: any packet-shape change touches the whitelist in the same
commit.

Fix in `c293a93c`; ares roms `S3_pal32.32x` / `S4_pal32_flick.32x`.

## "WHY ARE YOU GUESSING? WE HAVE THE MATH" — Mike, and the closure

The whitelist bug (and both thunk-source bugs) were DERIVATION
failures, not measurement gaps: every one was statically decidable and
built wrong anyway, because the derivation lived in prose and was
copied by hand into N places. Closed mechanically:

  - **`md_src/packet_fmt.h` is the packet format's single source.**
    All four consumers (MD builder, published length, SH-2 whitelist
    via PKT_K2_OK, SH-2 apply offsets) compile from it; _Static_asserts
    pin burst alignment, the concrete length family {84,120,152,184,
    216}, and the tag-field width. A shape change now recompiles every
    consumer or fails the build.
  - **The generated 68K thunks are verified by DISASSEMBLY, not desk-
    checking**: unidasm over pal_thunks[] confirmed pmark, both precise
    thunks and every static slot instruction-for-instruction, and all
    45 jsr targets in the patched image land on true slot boundaries.
  - Refactor proven pure by rom byte-diff: only the build stamps and
    one register-swapped equality compare differ.

## PAL32 ARES-VERIFIED ("we have color! we have gameplay!") + STEP 2: PKTSLIM

Mike's S4 pass: colour and gameplay clean at 30Hz. His state
(S4_pal32_flick.bs9): tail 29.4 (from 31.3), window/ack 75.8 — the
window span is now formally the bigger half and the next surgery.

**PKTSLIM (`make ... PKTSLIM=1`, requires PAL32):** words 0..79 of the
prefix are dead under FBSPR+FBTEXT (regs ride the text capture) and the
k1 junk record with them. Packets: k1 = 4 words (bitmap+tag+tail), k2 =
4 bare / 8+32K. Every length and offset — including the whitelist and
the SPRLEN/SPRGRP overrides — compiles from packet_fmt.h; the pinned
slim family is {4, 40, 72, 104, 136}.

TAILPROBE (attract, same rig as the PAL32 row):

               pre-PAL32   PAL32    +PKTSLIM
    MEANtotal      73.9     71.1        60.9   lines/vint
    MEANdreq       32.0     30.3        18.5
    worst tail       51       46          34
    flip skips       10       16           5

The junk words cost DOUBLE their count: each was also an NT_WRAP
per-word FIFO poll. (LOOP20 open: dreq mean was 48.6.)

Ares roms: `T_pktslim_flick.32x` (full stack) / `T2_pktslim.32x`.
Shipping rom unchanged (statics exact, `_end 0x06018138`).

Remaining push after PKTSLIM: ~4-8 words/window + pal blocks — the DREQ
protocol's residue is now mostly its own arm/ack overhead. NEXT: the
window span (75.8 mean / ~207-240 worst on hardware) — decompose with
preack/wait_split rigs, then the k1/k2 merge toward a window every
vint.

## THE WINDOW SPAN, DECOMPOSED ON HARDWARE (T_pktslim_flick.bs9)

PKTSLIM priced on ares: own-tail 29.4 -> 18.4 lines/vint (was 46.2 at
LOOP20 open), handler 95.6, game 64%. The FM-held window is now
everything: 12.78 ms/cycle on hardware (= the 68K's ~201-line wait per
window vint, 77.2 mean):

      blit + preempt          5.75 ms   45%   (the ~47us/row stall floor)
      unaccounted             4.32 ms   34%   (TEXT capture 2048w in-window,
                                              FB_SPR fill, pal apply,
                                              launches, joins)
      flip + drain + restore  1.85 ms   14%   (incl. 4KB text restore/cycle)
      apply_cram              0.65 ms    5%
      copy_pages              0.21 ms    2%

LOOP11's idle-token negative result now reads correctly: the wait was
never pickup lateness — it is real span. A token cannot move it.

## THE 60HZ BIND, NAMED (ARCHITECTURE.md Model A vs us)

Nine of fourteen commercial titles run set-once FM: the 68K NEVER
waits, because the 68K never touches the framebuffer. We moved the
S16's video RAM INTO the framebuffer (FBSPR/FBTEXT/staging — the move
that killed the push), so our 68K must get the FB back every frame,
and it pays with the whole-window spin. The transport arc fixed WHAT
crosses the bus; the spin is now about WHO owns it.

## PROPOSED 60HZ ARCHITECTURE (needs Mike's verdict — this is a pivot)

**Per-site FM gating: the game RUNS during the SH-2 window; only its
FB-bound writes wait.** Every game write that lands in the FB goes
through patch_game already (tile staging, text, sprite upload — the
same site inventory the pal thunks came from). Gate each site on FM
(poll-until-0 at the write, not a whole-window spin in the vint shim):
game logic, AI, audio run concurrently with the SH-2's 12.8ms window;
only actual VRAM stores block, and they complete in the gap. The 68K
stops paying for the window at all — window/ack 77.2 -> ~per-write
poll noise. That is the number that makes 60Hz arithmetic close:
  - 68K at 60Hz: game keeps ~90%+ of every vint (vs 64% now).
  - SH-2 at 60Hz: span must fit alongside compose: blit 5.75 (DIRTYROW
    already helps; more skip = less) + capture/restore after TXT32 (see
    below) + fixed ~1.5 -> ~8-9ms + compose 2.6 < 16.6 ✓ with margin.
Risk class: re-opens the write-discard hazard PER SITE — the FM audit
discipline (one raise site, spin-inside) becomes per-thunk proofs.

**Independent next build either way: TXT32** — the text capture reads
2048 FB words and restores 4KB EVERY cycle for a mean of a few changed
words. Same recipe as PAL32: dirty 32-word blocks via generated write
thunks, capture+restore only dirt. Buys ~1.5-2.5ms of the span and is
the same proven machinery (packet_fmt-style single source, thunk
generator pattern, unidasm verification).

## MIKE'S VERDICT: PER-SITE FM GATING. Fallback if it does not land:
## the SET-ONCE model (FM never returned; game video writes become
## Chaotix change-queues over the now-4-word push).

**FEASIBILITY CENSUS (wpcatch, 68K writes into 0x840000+0x20000, 12
gameplay frames, T2_pktslim):**

      703 writes = ~59 stores/frame, 16 DISTINCT SITES total:
        0x902AD8..0x2B4C  sprite upload loop   -> 0x85E0xx  (~28/frame)
        0x903A9E/AC2/AFE,
        0x904D96          text/HUD printers    -> 0x85FCxx-FE  (~40/frame)
        one tile-column writer                 -> rare
        0xFF099A (OUR shim) -> 0x851Axx        (~1/frame, ours to audit)

59 polls/frame is noise; blocked stores tolerate exactly the staleness
the captures already absorb. The gate build (LOOP23):
  1. patch_game: FM-gate thunks for the 16 sites (tst adapter-ctl FM,
     spin-until-0 at the STORE), generated + unidasm-verified like the
     pal thunks; scene-load bulk writers gate at loop level.
  2. md shim: delete the whole-window spin; vint runs the game tick
     concurrently with the SH-2 window.
  3. Audit the shim's own 0x851Axx write.
  4. Then CUT60: a window every vint.
Both architectures were measurable with ONE census; the census ran
before any code. (derive-dont-guess applied.)
