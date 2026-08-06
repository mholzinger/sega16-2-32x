# System 16 → 32X Porting Toolkit

Goal: everything built for the Altered Beast port that is NOT
Altered-Beast-specific gets promoted into a reusable toolchain, so the
next System 16B title starts from a kit, not from scratch. This file
inventories what exists, what each piece assumes about the game, and
what must be parameterized.

## The port pipeline (game-agnostic shape)

1. **ROM analysis + patching** — `tools/patch_game.py`
   Rebases the arcade 68K binary's video/IO accesses into the 32X
   memory map (tile RAM → FB staging, text RAM → MD work RAM, sprite
   RAM/palette → FB staging, IO → work-RAM mailboxes).
   - Reusable: the address-class scanner (objdump-driven), the
     A-site confidence rules, the DATA_EXCLUDE mechanism, the
     byte-write→word-write RLE patch pattern.
   - Per-game: the address map itself, boot-region redirects,
     excluded data tables (spawn scripts etc.), byte-writer sites.
   - Also reusable: the WRITE-OBSERVER THUNK EMITTER (two families
     now — `TILE_DIRTY_SITES`, `PAL_DIRTY_SITES`). See "Trapping the
     game's writes" below; this is the kit's answer to any hardware
     region the 32X cannot mirror cheaply, and it generalizes to
     every S16 title.
   - TODO for kit: move per-game facts into a declarative per-title
     config (`games/<title>.toml`) consumed by a generic patcher.

2. **Asset conversion** — `tools/gen_sprites.py` (+ tile equivalent)
   Interleaves the arcade sprite/tile ROMs into 32X-friendly binaries.
   - Reusable as-is for any S16B board set (same ROM interleave);
     parameterize ROM filenames/count per title.

3. **The renderer core** — `sh_src/m_main.c`, `sh_src/s_main.c`
   A software System 16B video chip for the two SH-2s: tilemap
   pages/scroll/priority, sprite format incl. zoom/flip/pitch, text
   layer, S16 palette conversion, sticky CRAM group allocator, SDRAM
   tile cache (RV-rule-safe concurrent compose), row-following
   3-window pipeline with vblank-gated slice blits.
   - Almost entirely game-agnostic already (it implements the BOARD,
     not the game). Per-game: tile bank register wiring, screen-flip,
     board revision quirks (S16A vs S16B sprite format).

4. **The MD shim** — `md_src/md_main.c`, `md_src/md_start.s`
   Vint window scheduler (V-counter gate + COMM12 heartbeat clock),
   MCU emulation scaffold (coins/inputs/sound mailbox), text-RAM
   streaming, FS-home discipline, boot tracer.
   - Per-game: MCU personality (or no MCU), input mapping, DIP
     defaults.

5. **Verification rig** — `tools/play_32x.lua` (+ objdiff/profiler
   harnesses, statistical screenshot scanners in scratchpad)
   - Scripted coin+start gameplay in MAME, work-RAM counter probes,
     object-slot differ against the reference arcade driver, FRT
     profiler readout, PIL/numpy frame-scan for artifact classes,
     on-screen perf bars for ares ground truth.
   - `tools/pal_tap.lua` — per-PC write census over any address range
     (site list + extents + region masks). Run this BEFORE trusting any
     statically-enumerated write-site list; see "Trapping the game's
     writes". Game-agnostic: point it at a range via env vars.
   - `tools/pal_probe.lua` — diffs an MD mirror against the SH-2 shadow
     per region, so a transport fault can be separated from a fault in
     the consumer. ALWAYS run it on the BASELINE too: the "hot regions
     never converge" reading that sent one fix down the wrong road was
     NORMAL — a 60Hz colour-cycle against a ~20Hz channel is
     permanently mid-flight, and the baseline was worse.
   - `tools/pal_rate.lua` — dirty-bitmap occupancy per unit over time;
     how starvation and ship-rate problems become visible.
   - `tools/strobe_scan.py` — black-frame census over an ares capture
     by FILE SIZE (no PNG decode). Feed it RAW captures; dedup inflates
     the rate by the dedup ratio.
   - TODO for kit: promote the screenshot scanners out of the session
     scratchpad into `tools/scan_frames.py`; make the lua harness
     read per-title input scripts.

## Trapping the game's writes (the write-observer pattern) — KIT-CORE

The single most reusable structure this project has produced, and the
one every S16 title will need. THE PROBLEM IS UNIVERSAL: an arcade 68K
writes freely to hardware (tile RAM, palette, sprite list) that on the
32X lives somewhere the MD cannot cheaply hand to the SH-2s. Mirroring
the writes into MD RAM is easy; knowing WHAT CHANGED is not, and the
naive answer — diff the mirror every vint — is ruinous. Altered Beast's
palette diff cost 45 of the MD vint handler's 92 scanlines to discover,
in steady state, that nothing had changed (LOOP 6 negatives 3-5: not
division-bound, long compares are free on a 16-bit bus, and ablating the
loop body took 45.1 lines -> 0.1, so the loop WAS the entire cost).

THE PATTERN. Patch each write site to `jsr (xxx).w` into a small thunk
in MD RAM that ORs a dirty mask into a bitmap word, runs the displaced
instruction, and returns. The shim ships only dirty units and clears
the bit. Applied twice now: tile pages (LOOP 3c) and palette regions
(LOOP 8, which retired the scan outright — MD tail 92.4 -> 48.2 mean
scanlines, palette term 45.1 -> 0.1).

RULES, each one paid for:
- **Thunk addresses must have low word >= 0x8000.** 68K `abs.w`
  SIGN-EXTENDS, so 0x5E00.w targets low ROM and crashes at the first
  thunked site. Both families hit this.
- **A `jsr (xxx).w` is 4 bytes**, so it can displace a 6- or 8-byte
  instruction directly, or a PAIR of 2-byte ones (that is how the
  register-indirect palette writers were trapped).
- **Never mark ALL-dirty if the extent is derivable.** An ALL-dirty
  mask on a per-frame animator floods the ship budget and lags the
  content (title parity 63-70% on the tile side before 0x258A got a
  precise thunk). Disassemble the loop bound; every Altered Beast
  palette site turned out to be derivable.
- **When the extent is only knowable at runtime, compute it in the
  thunk.** Derive the unit index from the address/index register, index
  a PC-relative mask table, save/restore any scratch register. Cheaper
  than it sounds and far cheaper than over-marking.
- **Mark BEFORE the store, and expect a race.** For loop bases the
  thunk marks, then the loop stores; a vint landing between them ships
  and clears the unit, losing the stores. Fix without a backstop scan:
  ship each freshly-marked unit TWICE (one repeat, two words of state,
  zero RAM reads). The loop has certainly finished a push later.
- **Round-robin the selection, never lowest-bit-first.** Hot units
  starve the rest forever: lowest-bit selection left Altered Beast's
  ENTIRE sprite palette unshipped, dirty in 99.6% of frames.

THE ENUMERATION IS THE DANGEROUS PART, and a static scan is NOT
sufficient. Address-formation scans (`patch_report.txt`) see `lea`/
`move.l #imm` but CANNOT see a writer that took its pointer from a
queue: Altered Beast forms a palette pointer at one site, stores it to
a table, and writes through it from two unrelated loops. Nothing static
attributes those. **Always run a write-tap census first**
(`tools/pal_tap.lua` — a memory tap, not a debugger watchpoint, which
is far too slow for hundreds of writes per frame). It reports per-PC
address ranges AND a per-unit region mask, which both finds the missing
writers and VALIDATES every hand-derived extent: the shared copy helper
observed exactly the union of its four callers' static masks.

## Your emulator may not model the framebuffer at all — KIT-CORE

Every 32X port's hot loop is an SDRAM->framebuffer blit, so the cost of
an FB write is the single most load-bearing number in the whole project.
MAME does not model it. Measured on Altered Beast, one session, four
independent proofs:

    cached vs uncached FB alias    MAME: byte-identical    ares: also equal
    us/row, same instruction stream MAME: 9.43            ares: 47.34  (5.0x)
    DMAC ch1 blit vs CPU stores     MAME: 18% FASTER      ares: 77% SLOWER
    SH-2 FB write with FM=0         MAME: 100% land       ares: 85.8%

Per-term, ares/MAME on the same build: blit 5.05x, copy_pages 8.92x,
apply_cram 2.02x, TOTALwin 3.80x. **Every term is understated by a
DIFFERENT factor, so the fast emulator cannot even RANK them** — which
is worse than being uniformly wrong, because a ranking is exactly what
you use it for.

THE KIT RULE: before optimising anything that touches the framebuffer,
run the cached-vs-uncached null test (same instruction stream, swap
0x04000000 for 0x24000000). If your fast emulator reads them identical,
it is not modelling FB cost, and every window measurement it gives you
is fiction. Iterate on it for CORRECTNESS (parity of statics) and take
every timing verdict from the accurate target. Budget for the round
trip; it is not optional.

Corollary for probes: put counters in a fixed SDRAM block the savestate
reader can find, not just in emulator-scripting hooks — the accurate
target is usually the one you cannot script. See `tools/state_health.py`,
`span_hist.py`, `wait_split.py`.

## Compose-to-ship pipelines: the gap must fit the BIGGEST band — KIT-CORE

Any port that composes into a staging buffer and blits to the FB in
slices runs a pipeline: band composed at window k, shipped at window
k+n. Two rules this port paid for:

1. **State the invariant, then MEASURE it.** This code asserted for nine
   iterations that the blit set and the outstanding compose were
   "DISJOINT by pipeline construction". They were the same band, in all
   three windows, the whole time. A comment is not a measurement.
2. **Size the gap for the biggest band, not the average.** 224 rows is
   28 tile rows; 28 does not divide by three, so bands are 72/72/80. The
   72s finish inside one window gap and the 80 does not — so ONE window
   in three pays a wait that scales with scene load, and that window is
   the whole strobe. Widening the gap to two windows (compose k -> ship
   k+2) costs one window of latency applied UNIFORMLY, which leaves the
   band-to-band SPREAD unchanged — and the spread is what seams are made
   of, so it adds no new seam.

Do not fix a compose overrun with more mailbox-poll points. Polling more
often cannot shorten work that has not been done; it costs more compose
time than the bounded latency saves (LOOP.md iteration 7f, reverted).

## MD-hardware landmines for arcade 68K code (kit-critical)

- **TAS never latches on MD/32X**: the MD bus arbiter drops the write
  phase of the 68K locked RMW cycle (MAME, ares, and real hardware all
  drop it; S16 boards don't). Every `tas/bne` one-shot latch in a
  ported game silently re-fires forever. Altered Beast: 5 TAS sites;
  the one at 0x2268 locked the attract sequencer into the infinite
  title/eye loop (velocity-kill add ran twice → camera runaway past
  the transition gate). KIT RULE: opcode-scan every ported binary for
  0x4AC8-0x4AFF words in code, replace each TAS with jsr to a RAM
  thunk: `tst.b` (TAS's exact N/Z, clears V/C) + `st` (no CC) + `rts`.
  See patch_game.py TAS_SITES + md_main.c TAS thunks.

## Hardware truth from jtcores (srcref/jtcores, GPL — derive, never copy)

S16B shares jts16_prio.v/jts16_colmix.v with S16A (jts16_video.v's
MODEL parameter only alters tilemap/obj internals). Facts derived:

- **Shadow is arithmetic** (jts16_colmix.v:80-88): shadowed pixel =
  each 5-bit channel `a - (a>>2)` (×0.75). NOT a palette pick. And
  **palette bit 15 exempts the color from shadowing** (`shadow &
  ~pal[15]`). Our indexed-color FB can't synthesize new colors →
  nearest-CRAM match must TARGET ×0.75 per channel (gap: flagged,
  closest 32X approximation) and must honor the bit-15 exemption.
- **Sprite pixel fields** (jts16_prio.v:52-64): [11:10] priority,
  [9:4] palette, [3:0] color. Palette 0x3F (`&obj[9:4]`) = shadow
  applied to the UNDERLYING tile pixel (confirms the MAME rule).
- **Per-layer sprite thresholds** (jts16_prio.v:83-88): sprite beats
  text iff pp==3; beats FG (scr1) iff pp>=2; beats BG (scr2) iff
  pp>=1 — consistent with the MAME level model.
- **Priority-tile punch-through** (jts16_prio.v:61): a priority tile
  still loses to the sprite where its color index bits [2:0]==0
  (indexes 0 AND 8 punch through, not just 0).
- **Layer opacity in the final mix** (jts16_prio.v:90-99): a tile
  pixel falls through to the next layer when [2:0]==0 (0 AND 8 are
  transparent); a selected sprite pixel is opaque when [3:0]!=0.
- **All-transparent fallback** (jts16_prio.v:87): the visible pixel is
  the BG (scr2) pixel's palette row with index bits [2:0] forced to 0
  — bit 3 of the index SURVIVES (row color 0 or 8), not a fixed
  backdrop color.

Cross-check ledger: verify m_main.c compose against the punch-through
and fallback rules (pp3/amb exactness work item); rebuild shadow_lut
to target ×0.75 + bit-15 exemption.

## ares savestate forensics (no scripting needed on the slowest target)

ares writes slot states next to the ROM (`rom/s16.bs1` = slot 1).
Format: `BST1` header; the 32X SDRAM image is stored RAW but 16-BIT
BYTESWAPPED, near the file head (this build: SDRAM[0] at file offset
0x23B — relocate by searching for a 64-byte .ramtext probe from the
ELF, swap16'd). Recipe: take `.ramtext` bytes from the ELF (objdump -h
for file offsets), swap16, find in the state file, subtract the VMA
offset → SDRAM base; then read DIAG/queue/any SDRAM region with
swap16+big-endian. Ask Mike to save a state MID-ARTIFACT; states are
only valid for the exact ROM build they were taken on. First use:
proved ares drops bands steadily in attract (DIAG[13]=864) while the
rotation fix keeps the read clean — measured the acceptance-gate
emulator's real operating point from a play-test session.

## Shipped-ROM archaeology (srcref/*.32x — Sega's own arcade ports)

Fast technique: scan the binary for 32-bit literals of 32X register/
window addresses (SH-2 code loads addresses from literal pools, so a
plain big-endian u32 scan maps the architecture in seconds — no
disassembly needed for the first pass; Ghidra for control flow when
required). Findings from Space Harrier / After Burner Complete /
T-MEK:
- ALL blit the framebuffer through the CACHED window (0x04000000:
  111-125 refs) not the uncached one (handful) — SH-2 write-through
  cache means cached-area stores ride the 4-deep write buffer instead
  of stalling the bus per word. Write-only paths only; reads keep
  uncached. STOLEN into blit_half.
- COMM barely used (1 literal each), DREQ FIFO not at all: their MD
  side is a stub — the whole game runs on SH-2s. Confirms our
  keep-the-68K-game-running architecture is the harder problem, and
  their MD-side patterns don't transfer; their SH-2 render-side
  disciplines do.

## Hard-won invariants the kit must encode (see NOTES.md for full log)

- RV=1 forbids SH-2 cart access → all hot code in .ramtext, tile/
  sprite pixel reads only in-window or via SDRAM caches; no libgcc
  (variable shifts) in hot paths.
- FBCTL flips ONLY in early vblank, gated by the MD V-counter, with a
  fresh-for-this-window heartbeat — never a stored clock, never the
  32X VBLK bit.
- FB byte-write zero-drop → patch game byte-writers to word writes.
- Blit budget is set by the SLOWEST target (ares ≈ several × MAME);
  slice size must carry 2×+ vblank margin.
- One bank stages, the other displays; staging is never deselected
  while the game runs.
- A CHANNEL'S RATE MATTERS AS MUCH AS ITS COST PER WORD. The MD->SH-2
  channels are not interchangeable even after cost is equalised: the
  COMM stream ran on EVERY vint, while the DREQ packet is pushed only
  on GATE-ACCEPTED vints. Moving a payload from one to the other
  silently makes its delivery WINDOW-rate-limited, which is invisible
  at a healthy cadence and bites under load. Weigh rate, not just the
  lines/word ratio, before moving any per-vint payload onto the packet.
- PACKET SIZE IS ONLY MEANINGFUL AGAINST AVAILABLE 68K TIME. A packet
  that failed to drain at one tail length drained FINE at 75% larger
  once the tail was cut (dreq_incomplete 9.3% -> 5.8% while the text
  packet went 340 -> 596 words). Re-measure size limits after any
  change to handler load; do not inherit them.

## Diagnosis methodology (proven on the unpair burn-down + title hunt)

The debugging kit that found every bug so far, in escalation order:

1. **Arcade oracle first** — `tools/oracle_shots.lua` runs the real
   arcade with the same coin/start/input cadence and screenshots the
   same frame numbers. Diff before theorizing. CLEAN THE NVRAM first:
   persisted credits change the attract flow.
2. **Poison rig** — the dead low copy is 0xFF-filled; any un-rebased
   READ pointer fails loudly in MAME exactly like ares.
3. **wpcatch.lua** — watchpoint catcher (r/w, value logging, both
   machines, env-configured window/range). Diff writer/reader PC sets
   ours-vs-arcade at the same moment; identical sets mean the 68K is
   innocent and the bug is data or renderer-side.
4. **Pipeline-stage verification** — before touching code, verify
   each stage in order: 68K write stream → FB staging → SDRAM shadow
   → latched regs → compose. The title-art hunt burned three wrong
   theories because stages were assumed instead of checked; the
   actual bug (quadrant decode) was in the LAST stage.
5. **Trace diff** — MAME `trace` over a frame window on both
   machines, PC-set clusters name the diverging scene handler;
   first-divergence lockstep needs interrupt-aware alignment.
6. **RAM diff at matched moments** — dump work RAM both machines,
   filter for structured deltas (the +0x90 family). A delta that
   equals the rebase offset is a patcher false positive; check
   rebase_report.txt for the offending pass.

Patcher lessons the kit must encode:
- Harvested-value passes need a VALUE BLACKLIST for byte-collision
  families (0x10000, 0x102/4/6-style); call-time normalization thunks
  make dropping them strictly safe.
- Ascending longs can be a REAL pointer table (0x1989E) or a forged
  one from a byte ramp (0x1AD10 easing curve). Ascending alone proves
  nothing — check what the values point AT.
- Layer-decode conventions (page quadrant nibbles, scroll sign, alt
  register set, rowscroll) must be lifted from the MAME driver
  SOURCE, not inferred from working scenes: a wrong quadrant decode
  hid behind scrolling scenes for weeks because they keep both map
  halves loaded.

## Workflow (agreed with Mike, 2026-07-29)

Interactive sessions for architecture/new-ground (renderer refactors,
sound bring-up, first bring-up of each new title) — ares verdicts and
judgment calls gate these. Autonomous loops for mechanical grinds
with objective pass/fail (per-round parity sweeps against the oracle,
soak tests, bisects). Ares play-testing remains the outer acceptance
gate for everything.

## Roadmap to "kit" status

1. Finish Altered Beast (accuracy + speed + sound) — the reference
   implementation.
2. Extract per-title config: address map, ROM set, input/MCU
   personality, DIP defaults.
3. Generalize patcher + asset converters against that config.
4. Second title (e.g. Golden Axe / Shinobi — S16B, similar boards)
   as the proof the kit holds.

Per-title work the write-observer pattern implies (do it in this
order, it is the cheapest path):
  a. Run the write-tap census on every mirrored hardware range BEFORE
     writing any thunk table — it yields the site list, the real
     extents, and the indirect writers a static scan cannot see.
  b. Derive each site's mask from its loop bound; runtime masks only
     where the disassembly genuinely cannot say.
  c. Size the delivery unit against how much the game actually changes
     per frame, not against what is convenient to address. Altered
     Beast needed a PAIR of 128-word regions per push because its
     colour-cycling sets are rewritten every vint.

## Geometry-convention rule (learned the hard way, 2026-07-30)

Every scroll/offset/sign convention lifted into a port MUST be pinned
against a discriminator scene: art that is UNIQUE and ASYMMETRIC at a
known register value. Periodic content (tiled walls, repeating props)
and symmetric register values (xs=0xC0 -> eff=0) validate BOTH signs
of a conversion — the Altered Beast X-scroll sign was inverted for
the project's entire life while passing oracle screenshot comparisons.
The attract transformation-scream screen (unique full-bleed art,
xs=0) was the first true discriminator. Per title, find such a scene
EARLY and regression-pin it.
