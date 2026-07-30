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
   - TODO for kit: promote the screenshot scanners out of the session
     scratchpad into `tools/scan_frames.py`; make the lua harness
     read per-title input scripts.

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
