# Separate rendering responsibilities before broad cleanup

For the actual per-pass order, ownership words and terminology, see
[R60 renderer pass and ownership map](RENDERER-PHASES.md).

2026-11-06. Implementations: optional composed foreground masks, prepared sprite
packets, tile-art preparation/upload batches and progress-aware packet ownership. Production
control remains Mike's accepted p15bpt7. No hardware speed improvement is claimed.
Implementation results: [initial experiment](../performance/MASK-BLOCKS-2026-11-06.md)
and [fresh headless validation/refinement](../performance/HEADLESS-MASK-VALIDATION-2026-11-06.md).
Measurements and rationale: [V-int and hot paths](../performance/VINT-HOTPATHS-2026-11-06.md).
Prepared-packet implementation, controls and failed live-health gate:
[sprite preparation](../performance/SPRITE-PREPARATION-2026-11-06.md).

## Contracts to preserve

| Responsibility | Input and output | Owner |
| --- | --- | --- |
| Simulation | Arcade tick and controls -> object/video state | Game adapter on master |
| Render description | Sprite commands, plane words, scroll/page/bank state | Existing publication/snapshot path |
| Preparation | Source descriptors -> reusable art/opacity/packet work | Renderer, with explicit storage ownership |
| Presentation | Finished framebuffer and MD work -> safe publication/flip | Existing slave/68K handshake and V-int |

Keep arcade simulation time and game logic out of preparation optimizations.
The first component below consumes source words and opacity bytes; it contains
no Altered Beast object handlers, enemy IDs, animation addresses, or tile ROM layout.
The title's bank remapping and decoded-art lookup stay in the adapter.

This is an incremental separation. The existing render description is not yet an
immutable whole-scene snapshot: tile words can change while the slave reads them.
This experiment retains the existing ownership and draw ordering. Do not describe
it as a new synchronization protocol or defer all punches until after all sprites.

## First bounded component: 32x32 foreground opacity

`sh_src/r60/r60_mask_blocks.h`, enabled only by `R60MASKBLOCK=1`, separates
source-word collection, mask composition/cache lookup, clipping, and framebuffer
emission. Each block contains sixteen canonical 8x8 descriptors and 32 opacity
rows. Rows use bit31 for the leftmost pixel. They are independent of color banks.

The adapter supplies physical tile banking and eight opacity bytes per source
tile. On 32X, the emitter writes the reserved through pen via the overwrite image;
it preserves skipped bytes, including literal-zero sprite transparency. It keeps
the old row-count and ymin/ymax contract for sprite cleanup.

A 32x32 group fits inside one physical source page, so collection resolves its
page once and reads four short rows. Emission visits only the clipped four-pixel
groups. A cache hit requires matching world block, page selection, tile bank, and all
sixteen current source words. There is no assumption that a coarse dirty counter
observes every write. Copy tile masks before fetching another: direct-mapped tile
cache collisions must not corrupt an in-progress composition. Invalid physical
pages remain blank. Empty source blocks do not evict useful masks.

Storage stays inside the existing 10 KiB arena:

| Address range (SH-2 SDRAM offset, end excluded) | Experiment |
| --- | --- |
| 3C400..3D400 | 512 canonical tile masks |
| 3D400..3D800 | 512 tile tags |
| 3D800..3E800 | 32 composed masks, or the mutually exclusive prepared-sprite arena |
| 3E800..3EC00 | Complete source keys |

Ordinary data/BSS adds 152 bytes before alignment for cache pointers/counters and
32 tags. Default builds retain the accepted 1024-entry tile cache and old punch.
`R60PRIOCACHE512=1` provides the old punch with the same reduced cache for timing controls.
Span-based repunches continue through the existing path, sharing the reduced tile
cache when the experiment is on. This trial supports single-slave PUNCH2/PUNCHTHRU;
conflicting priority probes and master-band rendering are rejected at compile time.

## Second bounded component: prepared sprite packets

### API and ownership

`sh_src/r60/r60_sprite_prepare.h`, enabled by `R60SPRPREP=1`, keeps a complete
reusable packet in caller-owned storage. It applies the numeric palette bank to
nonzero source pixels once, preserving transparent zeros and row headers. The
existing sprite adapter supplies immutable decoded art and its identity. The
assembly consumer retains alignment, clipping, row spans and framebuffer output
in the existing draw order.

The component returns a prepared packet on a hit or successful admission, and
null when the established plotter should handle the pose. It contains no game
objects or Altered Beast handlers, so another port can reuse it with its own
immutable decoded-art adapter.

### Identity and admission

Identity includes physical art key, bank, height and mapped-page epoch. Placement
and RGB colors do not affect prepared art. Admission requires repeated work;
capacity is checked before replacing a resident. Unsupported art uses the old
renderer. Preparation and consumption belong to the same slave and retain the
existing page-switch handshake. This is bounded source reuse, not a replacement
for immutable scene snapshots or a new synchronization protocol.

### Arena and policy

The cache uses the same freed 4 KiB arena as composed masks, so those features
cannot be combined. Optional `R60SPRPREP2=1` partitions the 1,024-word arena
into two size classes. `R60SPRPREP2S` selects the small-slot capacity (default
256 words), while `R60SPRPREP2H` selects its maximum routed height (default 24);
the remaining words form the large slot. The last 1 KiB after this arena remains
spare, with correctness-only row buffers when `R60BAKEARTCHECK=1`.

The engine feature is opt-in. Altered Beast explicitly enables its play-accepted
768/256-word, height-24 policy. Future games must run
`tools/baked_hole_census.py --two-slot` on their own corpora, choose candidate
capacity/height policies, and benchmark temporal reuse on hardware; capacity
coverage is not a speed claim.

### Proof

Pixel proof and the optional progress handoff pass. The latter includes the
focused long route and stall-recovery test. Altered Beast measurements and gates
are recorded in [the two-slot performance report](../performance/PREPARED-SPRITE-TWO-SLOT-2026-11-09.md).

## Packet ownership: detect stalls through completed work

`sh_src/r60/r60_span_lease.h`, enabled by `R60SPANPROGRESS=1`, separates a
consumer stall from a long, progressing upload. The producer publishes completed
packet work; the single owner observes progress and applies the existing stall
budget. Timer interrupts provide time, not progress. An unchanged counter must
still expire. The adapter owns COMM4 after startup and emits progress only while
consuming the packet. Packet format, draw order and simulation are unchanged.
See [implementation and evidence](../performance/SPAN-PROGRESS-2026-11-06.md).

## Complete tile-art preparation/upload backend

`sh_src/r60/r60_tile_batches.h`, enabled by `R60TILEBATCH=1`, converts canonical
source pixels through caller-supplied pen maps, groups dirty destination patterns,
and validates transfer bounds. Both foreground and background use this path.
The adapters retain page/scroll planning, palette allocation and VRAM residency;
the portable component contains no game objects or scene triggers.

Runs contain up to sixteen adjacent VRAM patterns, preserving existing tile
deduplication and pin lifetimes. These are contiguous uploads, not larger native
VDP tiles or spatial world-block caches. A compact zero-pattern command reuses
immutable blank art. The MD adapter invalidates its residency after menu use;
an otherwise-empty B packet must still initialize it when necessary. Exact
encoded lengths preserve packet budgets and the progress handoff owns access.
See [format, lifecycle and validation](../performance/TILE-PIPELINE-2026-11-06.md).

## Live source-row resolution

`sh_src/r60/r60_plane_rows.h`, enabled by `R60PLANROWS=1`, resolves each
contiguous physical source-row segment once. The caller supplies tilemap storage,
page count and selection. It still reads each volatile word in order, before the
existing per-cell residency work. Page boundaries, world wrapping, invalid
pages and unsigned camera bias are covered by an independent address oracle.
There is no persistent row cache or dirty-generation shortcut. See
[measurements and gates](../performance/PLANE-ROWS-2026-11-06.md).

## Tile-row delta scratch

`R60ROWDIFF=1` removes the initial 64-cell shadow copy from the plane walk.
Every emitted scratch cell belongs to the completed walk, including wrapped
spans and full pan strips. Offscreen releases read the authoritative shadow.
A separate poison/written-mask checker validates this contract in actual port
execution; no source-word caching or new invalidation rule is introduced. See
[work reduction and checks](../performance/ROW-DELTAS-2026-11-06.md).

## Proof and promotion

Run `python3 tests/diagnostics/check_mask_blocks.py` for actual C cache/emitter
against an independent per-pixel 8x8 oracle with ASan/UBSan. Optional arguments
are decoded `tiles.bin` and native snapshot prefixes. The same test checks
row metadata, all alignments, source changes, banking, page/world wrapping, clipping,
empty/solid/mixed art and colliding tile-mask storage.

Both headless runners are required: MAME supplies fresh native reference state,
while ares exercises the port. `--verify-native-image` on the fixture comparator
also checks reference geometry/colors against the MAME screen and reports exact
native sprite pens after foreground/text occlusion.

`tools/mask_block_audit.py SDRAM LISTING TILES --out REPORT` independently rebuilds
every populated SH-2 cache row from its stored keys. It proves composition only.

For a target framebuffer comparison, `PORTSPRITEFIXTUREPLANES=<native-prefix>`
extends the existing `PORTSPRITEFIXTURE` diagnostic with fixed native plane words
and scroll/page registers. Selected physical pages are retained; unused pages are
blanked and the full 13-page shadow is encoded with runs/literals. The generated
ROM-derived header remains ignored. The fixture decoder is tested by
`python3 tests/diagnostics/check_scene_fixture.py`. `R60FIXTURECLEAR=1` forces a full canvas clear while the fixed fixture runs,
removing prior framebuffer history; never benchmark this correctness-only mode.
`tools/mask_fixture_compare.py` verifies source inputs and compares shown-bank
pens selected from actual ares flip traces. The hidden bank can be in flight.
Tile banking remains supplied
by the game adapter and must be checked in the target dump. This is a renderer
fixture, never a gameplay or cadence test. A sprite-table-only freeze is insufficient:
our first comparison froze identical sprites but different foreground scrolls.

Require exact framebuffer pens under identical inputs, then a route-matched FPGA
comparison of phase time and overall pass intervals. Ares timing cannot establish
hardware benefit. Compare the accepted 1024-tile control as well as an equivalent
512-tile control when isolating cache tradeoffs. Measure source validation and cold
composition costs, not merely cache hit rate. Keep the experiment off if it loses.

After a hardware win, run normal gameplay and regression gates, obtain Mike's
play acceptance, promote the winning path, and remove the superseded implementation.
Apply the same sequence to sprite copies/assembled poses and plane packet preparation.
Broad renaming and file reshuffling can follow established ownership boundaries.
