# LOOP 12 — FINISH THE MD BACKGROUND (pivot slice 1c)

Kickoff for a FRESH session. Read this, then `ARCHITECTURE.md` sections
9-16 (the pivot's measured facts). `LOOP11.md` is history — its plan
sections are superseded by ARCHITECTURE.md, and several of its numbers
were corrected. **Where the two disagree, ARCHITECTURE.md wins.**

## THE JOB

`make MDBGALL=1` builds a port that moves the BG, FG cat-0 and text
layers off the 32X and onto the Mega Drive VDP. Everything in the path
is written and running. **The MD plane does not yet show the game's
background** — it shows horizontal bars of a wrong tile. Fix that, and
slice 1c is done.

Shipping build is untouched and stays that way: `rom/s16.32x`, parity
**24.26**, title 2.44, eyehold 3.37, `_end 0x06018d38`. All pivot code
is behind `#ifdef MD_BG` / `MD_BG_FG0` / `MD_BG_TEXT`.

## WHY THIS IS WORTH FINISHING (measured, not assumed)

    blit skips        base 23.4%   ->   pivot 0.2%
    composeL0         6792         ->   1494
    composeL1         5720         ->   0
    blit itself       719          ->   777   (unchanged)

Blit skips are the tearing: a skipped blit leaves a third of the screen
holding a stale band, and in stage 1 the bottom third is grass, which is
why it reads as *green* tearing. A 100x reduction kills it.

**It does NOT fix the framerate.** 20 Hz is 3 windows per frame because
a window can only ship 75 rows inside a 38-line vblank, and the blit is
unmoved. Going faster is a separate problem (more rows per vblank, or
stop shipping whole frames). Mike knows and wants this anyway.

## THE THREE SUSPECTS, RANKED

1. **`MD_BLANK_SLOT` is 1023, which the allocator can also hand out.**
   A cell that fails to allocate is indistinguishable from a cell whose
   code legitimately owns slot 1023. Reserve a slot the allocator never
   assigns. ~5 lines.
2. **Ordering.** The shipper alternates one 40-tile batch to four
   280-cell name-table chunks, so a chunk can reference a slot whose
   tile has not been uploaded yet. Early on most referenced slots hold
   nothing. Ship tiles ahead of the cells referencing them, or bias the
   schedule toward tile batches after a burst of new claims.
3. **Scroll sign/offset.** `-(vx0 & 7)` into the hscroll table and
   `+(vy0 & 7)` into VSRAM is a guess, never checked against a still
   frame.

## HOW TO BUILD AND LOOK

    make MDBGALL=1
    # headless capture -- video ON is far too slow with the pivot's
    # per-window work, and snapshots work fine with -video none
    SHOT_DIR=/tmp SHOT_TAG=x SHOT_EVERY=1200 SHOT_LAST=3600 \
      mame 32x -cart rom/s16.32x -rompath ./mame -skip_gameinfo \
      -video none -sound none -nothrottle -autoboot_script <grid.lua>

Counter dump (`diagdump.lua` pattern) reads DIAG from the master at
`0x06028000 + i*4`; the useful slots are [7] blit skips, [9] cycles,
[13] deferrals, [14] misses, [10]/[11] composeL0/L1, [5] blit.

## WHAT IS ALREADY ESTABLISHED — DO NOT RE-DERIVE

  - **MD video composites through the 32X.** Transparency is bit 15 of
    the CRAM ENTRY, not pixel index 0; `cram[0] = 0x8000` arms it.
    Verified on ares (§10).
  - **Tile transport works, verified on ares** (§13): SH-2 converts to
    4bpp planar into the dead FB block at 0x11A00 (FM=1), MD reads
    0x851A00 (FM=0) and uploads. The packet is self-describing and
    idempotent, which is what makes it immune to per-bank staging skew —
    keep that property.
  - **S16 -> MD 4bpp conversion is verified** (`tools/md_tiles.py
    verify`, 16384 tiles, 0 mismatches). High nibble = LEFT pixel.
  - **Patterns fit**: 1363 VRAM slots below the name tables, 599 peak
    distinct. §13.
  - **Colour is not a problem** (§11): MD 3-bit quantisation is ~3%
    error, invisible, and it collapses distinct colours by a third —
    mean 92 -> 58 against MD's 64. Precompute the assignment; do not
    build a runtime merge.
  - **Read-back is ~15 KB** of single-word collision probes (§9), and
    MD VRAM is 68000-readable through the data port, so it stops being
    a blocker once the tilemap lives there.
  - **MD residency must be STABLE.** The render cache is transient and
    cannot be the MD allocator — that cost a debugging cycle (§16).
  - **Parallax is free**: Plane A and Plane B have independent hardware
    scroll. Row/column scroll is not needed for stage 1 (measured, 700
    samples) and Mike's recollection is that later levels are parallax
    only. Defer it until a level needs it.

## TRAPS THAT COST REAL TIME THIS LOOP

  - **Verify an instrument can see a POSITIVE before trusting an
    absence.** The plane reader was wrong three times: it dropped every
    DMA'd write (CD4/CD5 sit above the destination select, so comparing
    the whole code against 1/3/5 misses them), it cannot see VDP traffic
    before its tap installs on frame 1, and installing the tap earlier
    desyncs the control state machine. It published a false headline
    about MK2 twice.
  - **Judge against the RANGE.** ares states under ~3000 cycles
    under-sample maxima badly. A 403-cycle state said the FM window was
    110 lines; a 3709-cycle state said 210 with 8 lines of margin.
  - **`SHASFLAGS` is assigned with `=` BELOW the flag blocks.** A
    `SHASFLAGS +=` written up there is silently discarded. Assembler
    flags go after the base assignment.
  - **Objects depend on `.build_flags`** now, so flag switches force
    rebuilds. Any flag-build measurement older than commit `0faba16` is
    suspect.
  - **Screenshots mislead.** Two of three conclusions drawn from a frame
    this loop were wrong; cross-checking against the savestate flipped
    one from "transport failed" to "transport works". Read the memory.
  - **MAME cannot rank window/pickup work** — its SH-2 is ~3x faster.
    It CAN rank correctness (parity statics) and per-cycle counters.

## DEAD ENDS — DO NOT RE-ATTEMPT

  - **Chaotix's idle-token / poll-and-skip handshake** (`IDLETOKEN`,
    `IDLEGRACE`). Its premise is a PARKED SH-2; ours is saturated. ares:
    cadence 3.03 -> 7.48, "broken the second the game starts."
  - **Interrupt-driven pickup** (`CMDINT`). Preemption moves latency
    from window-pickup to band-completion and the pipeline has no slack
    there: deferrals 4.6x, skips 66%. Also the prize (8.8% of vints) was
    always smaller than the price (the ISR alone cost 3.1 points).
  - **A dirty/block-skipping blit as a framerate fix.** Even with BG,
    FG cat-0 AND text removed, only 31.8% of the screen goes transparent
    (25.7% at 32-pixel granularity) against a ~25% break-even. §15.

## AFTER 1C

1. Precompute the palette assignment (§11) and replace the grey ramp.
2. Wire FG cat-0 onto Plane A (1c does BG on Plane B only).
3. ares pass: capture + savestate. `tools/row_health.py` scores a build
   from a capture alone and tracks what Mike perceives as choppiness;
   `tools/state_health.py` reads the counters from a state.

## RESULT — 1c DONE (2026-08-09)

The MD plane shows the real background. None of the three suspects was
the root cause; see ARCHITECTURE.md §16 for the full account. Short
form:

  1. `cache_tag`/`md_tag` were placed INSIDE PAL_SH (0x27000-0x28000
     is the palette stream target, not free); the slave clobbered both
     with palette words every batch. Moved to 0x3A800/0x3B000.
     Also: the md_tag allocator described in §16 had never actually
     been written — the array was initialised and read, never claimed
     into. Instrument lesson again: `md_tag used=1024` with
     `claims=1` was the tell.
  2. First-come-no-eviction dies on the title screen (~1120 cycling
     codes > 1024 slots). Added per-set LRU eviction (`md_ref`
     window stamps).
  3. The name-table pass needed the same per-band alt-set/rowscroll
     selection as compose_layer (cloud band), plus per-strip cell-mode
     hscroll on the MD side (reg 11 = 02).

Suspect 1 (blank slot) and 2 (ordering) were fixed as designed —
reserved slot 1023 + demand-biased tile batches — but neither was the
headline. Suspect 3 (scroll signs) was right all along: title dx=+0.

Shipping gates re-verified after: title 2.44, eyehold 3.37, TOTAL
24.26, _end 0x06018d38, stamped normal.

Next (unchanged from AFTER 1C): palette precompute (§11) — the
remaining title diff (13.4%) is visibly grey-for-blue placeholder
colours — then FG cat-0 on Plane A, then the ares pass.

## PALETTE SLICE (same day, after 1c): data-side DONE, one MAME question open

§11's palette precompute is built and verified to the 68K bus:
colour-level pack into MD lines 1-3, (code,set)-keyed patterns with
per-set pen remaps, live CRAM refresh (fades track), drift check,
rounding quantiser (truncation rendered the stage-1 sky dither at 4x
contrast). Full account + numbers: ARCHITECTURE.md §17.

Also fixed: the -mshort null-command class bug (MD ints are 16-bit; a
constant-only `<<16` VDP command evaluates to ZERO — cast or die), the
VSRAM plane-B/plane-A misaddress, plane A boot-console remnants drawing
a glyph grid over plane B, the slave still composing BG/FG0 in
software, and both sbuf clears missing the +8 blit row convention.

OPEN: pack-era builds composite the MD plane as backdrop-only ON MAME
(data structures verify correct via data-port read-back; the pre-pack
payload composited fine). Cell-mode hscroll is parked on reg 8B=00
until resolved. NEXT: ares play pass FIRST — MAME was never the
acceptance vehicle for MD-plane visuals — then, only if ares agrees
it's broken, a scene-anchored bisect from fcf68f9 on MAME. Do not trust
frame-indexed screenshots across builds (timeline drift), dead write
taps, or the fake videoram space (§17 instrument list).

## NIGHT SESSION 2 — the void is real, bracketed, and one differential
## away from dead

Mike confirmed the void ON ARES — not a MAME gap. Then a scene-anchored
bisect (tools note below) established, deterministically:

  - `fcf68f9` payload + CURRENT MD receiver → **renders** (metric 11%
    dominant). The whole MD side is exonerated in one step.
  - HEAD payload → void (99.7% flat).
  - HEAD payload with ONE change — pattern bytes shipped as RAW pixels
    instead of through `mdp_s_map` — → **renders** (35.8%, and the art
    is GARBAGE: see below).
  - Same with the map read kept but DISCARDED → renders. The read is
    innocent.
  - Map values masked to pens 0-7 → void. High pens are innocent.
  - Double-send of tile batches (bank-skew theory) → void. Dead end.
  - Layout fix (below) alone → void. Necessary, not sufficient.

**Real bug found and fixed regardless: `mdp_s_qc` is 2048 bytes, the
layout gave it 1536** — sets ≥ 96 overran the LRU stamps, pen owners
and debug mirror. That corruption chain (owners → live CRAM refresh →
line_c → drift storm) is fixed at HEAD.

**The impossible triangle a fresh session must break:** in the void
build, EVERYTHING verifies correct at the data port from the 68K at
window time — name entry (pal 2, real slot), that slot's pattern
(0xCCCC = flat pen 12), CRAM entries 40-47 (real colours) — and the
screen still shows CRAM entry 0 (backdrop) for those pixels. The
renderer disagrees with the data port about the same arrays. One of
these is true and all are testable:
  1. The raw-build "render" is stale/garbage FB content being uploaded
     (its art is a repeating grid, NOT the background — so possibly
     patterns never arrive in EITHER variant and the raw build merely
     uploads nonzero stale bytes where the map build uploads only what
     it wrote... but the map build's read-back shows ITS bytes in
     VRAM).
  2. Mid-frame window retries rewrite some VDP state during active
     display in a way read-backs at window time cannot see.
  3. Something about the two extra SDRAM loads per pixel-pair changes
     the FB write behaviour (timing/serialisation) in a way that
     corrupts the PACKET, and the read-back path is reading the FB
     bank that got the good copy while the MD consumed the bad one.

**Ready-made instruments** (scratchpad `wt-1c` worktree + scripts):
  - `anchorshot.lua` — screenshot at the demo game-state anchor; no
    timeline drift across builds.
  - `voidmetric.py` — % dominant flat colour in the BG region.
    Good ≈ 11%, void = 99.7%. One number per build, ~70s per cycle.
  - Receiver read-backs at 0xFFB0C0-CA (CRAM 40-47, cell (2,0), its
    pattern word); receiver counters at 0xFFB0E0-EC.
  - The worktree builds fcf68f9 with blobs copied and the buildstamp
    git-dep shortcut; `git worktree list` shows it.

Suggested first move next session: put the SH-2's OWN read-back on the
packet in the FB (read back the pattern bytes it just wrote, same
window, count mismatches into DIAG) — that splits theory 3 from the
rest in one run. Then tap the MD's FB READS of the packet region and
compare against what the SH-2 wrote per window (bank by bank).

### Addendum, written 30 seconds after committing: the read-backs may
### all be lies, and that dissolves the impossible triangle

Every read-back in the evidence table (cell entry, pattern word, CRAM
colours) was performed by the receiver IMMEDIATELY AFTER its own
writes, same window, through the same data port. If MAME's 315-5313
read path returns prefetch/FIFO state in that situation, all three
"storage proofs" are echoes of the window's own writes and prove only
that the WRITE PATH saw those values. Verify before trusting: do a
read-back in a LATER window with no intervening write to that address
(or from lua-driven 68K at a quiet frame).

If the read-backs are echoes, the simple theory fits everything
including ares: **the receiver's VDP writes are landing outside
vblank.** The 3-window pipeline runs window handlers mid-frame; VDP
data-port writes during active display go through the 4-slot FIFO
(hardware: 68K stalls; MAME: possibly drops). A build-dependent shift
of a few percent in the window handler's timing (map lookups, CRAM
block, read-back probes) moves which windows' receiver work lands in
vblank — exactly the raw-renders/map-voids knife-edge, with no magic
byte-value dependence at all. The raw build's garbage-grid art =
whatever fraction of pattern writes happened to land.

Test order for next session:
  1. Read-back echo check (above) — cheap, settles the evidence.
  2. Count receiver VDP words vs V-counter position: log the MD's
     V-counter (0xC00008) BEFORE and AFTER the receiver block per
     window into work RAM; correlate windows-outside-vblank with the
     void. If confirmed, the fix is architectural and known from the
     commercial library (§2): move bulk VRAM traffic into DMA and/or
     restrict the receiver's VDP writes to a vblank budget with a
     carry-over cursor (ship fewer words per window, every window).

## NIGHT SESSION 2, part 2 — the grind results

Executed the LOOP12 test order plus four more discriminators. Facts,
each verified in the wt-1c worktree with the anchored metric:

  1. **Echo theory DEAD.** Pre-write read-backs (start of receiver,
     nothing written yet that window) persist across windows:
     cell (2,0)=0x6150, pattern words 0-2 = CCCC CCCC CCCC, CRAM
     40-42 = real colours, cells (2,1)/(2,2)/(2,8) all correct.
     Multi-word, multi-address, pre-write. Storage looks real.
  2. **V-COUNTER: the receiver's VDP work spans V=0xF8 -> V=0x85 —
     ~140 scanlines, every window.** The vint here is the HINT at line
     0xDF; the receiver runs through vblank and 130+ lines of active
     display. This stands alone as a finding: the receiver is far over
     any vblank budget, and on real hardware that means massive
     FIFO-stall time inside the vint handler. Whatever else is true,
     this needs fixing (DMA or a per-window write budget).
  3. **Traffic-poison theory DEAD.** Freeze test (receiver consumes
     but writes nothing after vint 1200): void persists in total VDP
     port silence.
  4. **Scroll fetch coords clean.** VSRAM A/B = 0, hscroll B = 0/-6 at
     read-back. No displacement into unwritten name rows.
  5. **MAME 315-5313 write path verified from source**: data port ->
     vdp_vram_write -> vram_w -> m_vram + mark_dirty on all six gfx
     elements. Renderer reads m_vram (name table raw, patterns via
     gfx decode). No second store, no 32X-specific divergence found
     in the source.
  6. **Savestate round-trip: BG rows still void after restore** (which
     rebuilds gfx caches from m_vram) — but the GRASS rows survive
     with colour while the 32X layer comes back glitched. Strong hint
     that the grass/bottom rows ARE plane B rendering correctly
     (revisit every "grass = 32X cat-1" assumption from tonight), and
     the top rows' data is NOT in m_vram at restore time — directly
     contradicting fact 1 for the same rows.

**The contradiction to break next:** row-2 read-backs return correct
data through the data port; the same rows are absent from a restored
savestate and from the render. The only tool left that can see the
truth is MAME itself: run with -debug and inspect m_vram at the void
moment, or build MAME with a logprintf in vram_w/vdp_vram_r for
addresses 0xE100 and the pattern base. One hour with the debugger
beats another night of black-box probes — the black box is exhausted:
every probe that can be built from the 68K side has been built and
they disagree at exactly one interface.

Worktree state: wt-1c has HEAD sources + all receiver diag probes
(read-backs at 0xFFB0A0-BE, freeze switch at vint 1200 — REMOVE the
freeze before real use). Main tree is clean at HEAD.

## VERDICT — THE VOID IS SOLVED. It was never a rendering bug.

Ground truth via MAME's save-item registry (`emu.item` on
`:gen_vdp` `0/m_vram` — the ONLY trustworthy window into the real
VRAM; the videoram space is fake, the data port disagrees at pattern
addresses, taps die): the name table, patterns and registers are all
present and correct in the render store, and the renderer draws them
faithfully. The upper rows ARE flat dark tiles — because on System 16
the graveyard wall, trees and statues are the FG LAYER, and the sky
behind them is a dark flat BG. **The "void" was the unwired FG cat-0
plane** (LOOP12 "AFTER 1C" item 2), exposed the same night it became
load-bearing: slice 1c's slave still composed FG cat-0 in software by
omission, and this session's slave-ifdef fix (correct!) removed FG
cat-0's last renderer. Every observation checks out against this:
grass/eyehold/scream rendered (BG-heavy content), fcf68f9 "worked"
(slave FG0), ares agreed (same missing layer), raw patterns showed
garbage "art" (corrupted-but-visible BG vs correct-but-dark BG).

**Proof:** `make MDBG=1` (FG0 still on the 32X) at the demo anchor now
shows the full scene — logo, tombstones, INSERT COIN, sprites, grass —
with the palette pack live underneath. Anchored void metric 86.3%
dominant = the night sky itself.

Parity numbers for the MDBG build remain transition-dominated (the
anchors settle 45f, the MD path rebuilds ~0.5s; §17) — judge on ares.

NEXT (finally, cleanly): wire FG cat-0 onto MD Plane A. Everything it
needs already exists: the allocator keys on (code,set), the pack has
line room measured (FG0 adds ~7-10 sets), plane A is wiped and waiting,
cell-mode hscroll is parked but plane A can ride the same global fine
scroll to start. Then the sky-colour scrutiny (dark vs arcade
grey-blue — check blank-cell rate on sky sets first) and the receiver's
140-scanline VDP span (DMA or a write budget) before Mike's ares pass.

## FG CAT-0 IS ON PLANE A — the two-plane pivot is alive

Wired in the same night the void was solved, because everything it
needed already existed. Design as planned: FG keys carry bit 31 in
md_tag (same tile needs a transparent-pixel-0 pattern variant on
Plane A — the shipper emits pen 0 for pixel 0 on FG keys); the packet
rotation is now 1 tile batch + 4 Plane-B chunks + 4 Plane-A chunks
(sc[2] bit 15 = plane A); FG cells that are empty or cat-1 resolve to
the blank slot; VSRAM 0 / hscroll 0xFC00 carry the FG layer's fine
scroll (sc[6] = FG vy). mdp_free_set's invalidation masks the plane
bit so a set free kills both variants.

At the demo anchor MDBGALL now shows the graveyard on the MD: wall,
statues, tombstones, columns (Plane A) over grass/platform/sky
(Plane B), palette pack live, 32X sprites and cat-1 on top. Pack
pressure with both layers: 7 nearest-fallbacks per minute — the
3-line budget holds with FG0's extra sets, as §11 predicted.

Open items, all normal-grind class now:
  - Sky region renders a teal dither where the arcade has dark
    grey-blue — first suspect: the FG sky cells' set colours or a set
    that should resolve blank; NOT line pressure ([36]=7).
  - Transient garbage strip at the left edge during scene rebuilds.
  - Chunk cadence is now 9 windows per full two-plane refresh; revisit
    if scroll seams show on ares.
  - The receiver's VDP span (~140 scanlines) still wants a DMA or a
    write budget before hardware.
  - Parity anchors still mis-time probe builds; judge on ares.
