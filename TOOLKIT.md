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

## Roadmap to "kit" status

1. Finish Altered Beast (accuracy + speed + sound) — the reference
   implementation.
2. Extract per-title config: address map, ROM set, input/MCU
   personality, DIP defaults.
3. Generalize patcher + asset converters against that config.
4. Second title (e.g. Golden Axe / Shinobi — S16B, similar boards)
   as the proof the kit holds.
