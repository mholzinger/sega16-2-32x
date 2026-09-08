# STATIC-SCENE — per-scene static MD pen tables + slot flush at the cut

2026-09-07. Mike: "start the static-scene arc." Two confirmed defects
with one root, both diagnosed from his savestates (HANDOFF-SESSION8
section 4b, the transformation-exit smear in section 4):

1. **Random-boot flat sky / missing clouds.** The MD pen pack and the
   VRAM slot map are first-come with LRU eviction, so what a scene
   looks like depends on the order sets and tiles arrive at the cut.
   The sky set (0x5C) landed on a full line on EVERY boot and took
   nearest-colour pens; which pens were nearest was luck.
2. **Scene-cut smear.** The arcade cuts in one frame; ours settles the
   MD planes and pens over ~20 frames while the allocators churn.

PALSTATIC (docs/design/PALSTATIC.md) already made the 32X CRAM side of a
scene static (per-scene PAL_SH image, confirmed detect, heal channel,
glow bake) and ships in SHIP_US. **This arc is the MD half of the same
idea:** the pen lines and the slot map become scene tables too.

## Measured facts the design rests on

From the two states (both BUILD 559b9dd5, both `pscene_cur == 0` =
the baked "normal" scene, i.e. intro and level-1 gameplay are ONE
scene to the detector):

| fact | level state | intro state |
|---|---|---|
| colour sets referenced by the slot map | 34 | 35 |
| distinct MD-quantised colours those sets need | 30 | 30 |
| exact 3-line partition (each line's union <= 15) | FOUND, loads 15/15/6 | FOUND, loads 15/15/6 |
| pens the dynamic pack actually used | 43 of 45, sky set all-fallback | 43 of 45, sky set all-fallback |
| slots the two planes reference | 568 | 515 |
| slots the map holds | 1023 of 1024 | 1023 of 1024 |

So: a zero-fallback pen table for the scene exists with 9 pens spare,
and the scene's working set is ~55% of the slot map. The dynamic
allocators fail not from capacity but from ORDER: the title's cycling
backdrop (~1120 codes) fills the map before the scene starts, and the
first sets to arrive at the cut take the pens the sky needed.

The exact partition search is `tools/state_frame.py`'s sibling logic
(depth-first over sets sorted by colour count, prune at 15); it ran in
well under a second for 34 sets, so the bake can afford exhaustive
search per scene and must FAIL LOUDLY when no partition exists rather
than fall back silently.

## Design

### A. Static MD pen table per scene (the fix for the sky)

**Offline bake** (`tools/palscene_bake.py` grows an MD section; input
already exists: `tools/palharvest_tiles_ares.py` records PAL_SH and
`mdp_s_used` per sample frame, and Mike's states carry the same tables):

- Per scene anchor: the set of colour sets live in the scene, each
  with its pixel-usage mask (union over the harvest span, FG pixel 0
  excluded as the runtime does) and its quantised colours from the
  scene's PAL_SH image.
- Exhaustive partition into MDP_LINES (3) lines of 15 pens; pen order
  within a line chosen so that pens shared between sets are exact.
  Emit per scene: `line_c[3][16]`, `s_line[128]`, `s_map[128][8]`,
  `s_used[128]` — the four runtime tables, verbatim.
- Bake fails if any scene has no exact partition, or if a set's
  colours drift within the scene's harvest span by more than the
  fade class (the glow words are excluded the way glow_bake does).

**Runtime, at a confirmed scene load** (the existing `pscene` load
site, right after the PAL_SH image copy):

1. Install the scene's four tables over `mdp_*` in one pass.
2. Invalidate every `md_tag` (the same loop `mdp_free_set` runs, for
   all sets) so every pattern re-converts under the new remaps, and
   mark the name-table walk dirty. This is the mechanism the code
   already trusts for a set relocation.
3. Post the heal (0xBAD2) as today; the display gate is already
   holding the picture blank at a cut and releases only after two
   full cell-walk rotations with no dirt, so the reveal shows the
   table's pens, never the churn.

**Rules inside a known scene:**

- `mdp_assign_set` for a set the table names returns the table's line;
  it never claims dynamically. Sets the table does not name (untrained
  art, later rounds) use the dynamic path on the spare pens only — the
  9 spare pens are the whole dynamic arena. Nearest-colour fallback
  stays as the last resort for THOSE sets and counts in DIAG[36] as
  now; for table sets DIAG[36] must not move (gate).
- Eviction never touches a table set. `mdp_free_set` on a table set is
  a bug and gets a DIAG counter.
- Unknown scene (`pscene_cur == 0xFF`): the dynamic allocator runs
  exactly as today. Same graceful degradation PALSTATIC has.

### B. Slot flush at the cut (the fix for the clouds)

At display-off (`disp_gate`, the `!r60_disp_on` branch, which already
resets `disp_settle`/`disp_hold`): free the whole slot map
(`md_tag[] = 0xFFFFFFFF`, `md_ref[] = 0`, clear `md_dirty`). The old
scene's residue never competes with the new scene's tiles, and LRU
then only ever chooses among tiles the scene itself uses, which is
~55% of capacity. Cost: the walk re-claims ~550 slots during the hold
the gate already imposes — the hold's settle rule ("two rotations, no
dirt") is exactly the condition that those claims have landed.

The arcade never shows a load, so there is no fidelity cost to doing
the flush inside the blank.

v2 (only if the hold measurably lengthens): a baked per-scene tile
list pre-claims slots at the cut instead of waiting for the walk.

### C. Cut atomicity (PALSTATIC's v2 item, unchanged)

Out of scope for v1. With A and B the picture that appears at release
is the scene's final picture; what remains is the ~20-frame hold
versus the arcade's 1-frame cut, which needs the shadow name table +
plane-base flip PALSTATIC.md describes.

## What this does NOT do

- No change to the 32X CRAM side, the PAL_SH images, detect, or heal.
- No change to the compose/blit pipeline, CAT1MD, or the sprite path.
- The transformation span stays on the delta pipeline (PALSTATIC
  measured no stable discriminators there).

## Gates

Every one is headless and reads back from memory (Mike's rule: a
milestone is a long ares run plus a verifier).

1. **State verifier**: `tools/state_frame.py STATE --report`. On the
   two sky states after the change: `stale-line cells 0`, the sky set
   0x5C shows `n/0` (no fallback pixels), cloud codes 0x139C-E and
   0x13B7-8 resident, and the reconstructed frame matches between the
   boots and against `screenshots/sky_states/arcade_intro_f3880_43.png`
   by eye.
2. **Boot-order battery**: N headless boots with the coin/start frame
   jittered (shift `discover/inputs/play_level1.csv` by k vints, k in
   0..29). Dump SDRAM at the same game-timeline frame; the four `mdp_*`
   tables must be byte-identical across all k for a known scene (they
   are a table now), DIAG[36] identical, and the slot map must hold
   only scene tiles. Today's dynamic build is the control and is
   expected to differ across k — that difference IS the bug.
3. **Cut census**: on the play_level1 timeline, the display-gate hold
   per cut (state_health DISPLAY GATE line) must not grow beyond the
   current ~20 vints with the flush in; if it does, v2 pre-claim.
4. **Battery unchanged**: ships / wall / handler / bad1 within noise
   (tools/gameplay_speed.py, the session-7 numbers as control).
5. **Mike's pass**: three cold boots into level 1, the sky and clouds
   identical each time, then the transformation.

## Order of work

1. `palscene_bake.py --md`: emit the MD tables for the scenes the
   corpus knows (normal; boss_smoke is detect-only and inherits
   normal's tables). Fail-loud partition. Verify offline against the
   two states' PAL_SH: the baked table must be a superset of what the
   states needed (30 colours, 34-35 sets).
2. Runtime install at scene load + the table-set rule in
   `mdp_assign_set` + the DIAG counters. Flag `MDSTATIC=1` on top of
   PALSTATIC; SHIP_US only after gates 1-4.
3. Slot flush at display-off, same flag.
4. Gates 1-4, then hand the rom to Mike with the three-cold-boot ask.
