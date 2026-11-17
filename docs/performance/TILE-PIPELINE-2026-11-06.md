# Foreground/background tile-art backend

Optional `R60TILEBATCH=1`, with `R60SPANPROGRESS=1`. Default remains off.
This implements source-pattern conversion, destination grouping, packet encoding,
MD consumption and persistent blank-pattern reuse for both tile planes. Existing
page/scroll planning, palette allocation, VRAM slot ownership and sprite priority
remain the adapters supplying this backend. There is no enemy/scene-specific path.

## Reusable component

`sh_src/r60/r60_tile_batches.h` is independent of game logic and owns no storage.
Inputs are canonical decoded tile pixels, an eight-entry pen map, plane opacity
and destination pattern slots. It packs 4bpp pixels through word stores, preserving
FG transparent zero and BG mapped zero, then groups selected destination slots.
The shared bounds checker validates a run before the MD issues any DMA.

The VDP continues composing native 8x8 patterns through its name tables. Upload
runs contain up to sixteen contiguous VRAM patterns/512 bytes; they are not
spatial 32x32 world squares. Retaining the existing per-pattern VRAM dictionary
avoids duplicating shared art or spending extra VRAM for assembled world blocks.
The new C boundary lets another System16 port supply art/maps/slots without
importing Altered Beast object handlers or animation addresses.

Dirty-slot lists are gathered after the existing capacity fit and palette plan.
A 128-byte caller-owned bitset deduplicates and orders them. Gaps split runs;
unchanged patterns are not uploaded merely to bridge a gap. All uploads in one
tail resolve current immutable art/map identities before publication. VRAM remains
the resident graphics cache. RGB palette cycling retains its existing behavior;
pen-map changes still mark the affected patterns dirty through the allocator.

| Record | Meaning | Words |
| --- | --- | ---: |
| `4000\|slot`, pixels | One nonzero destination slot | 17 |
| `6000\|first`, count, pixels | 2..16 adjacent nonzero slots | 2 + 16*count |
| `5000` | Ensure reserved literal-zero pattern0 is resident | 1 |

The tail count remains a count of logical patterns. Existing mixed-record
consumption is extended at both ends. Runs check logical count, destination pool,
maximum length and framebuffer source boundary. Conservative old packet-fit
budgets remain sufficient; actual packet-A lengths use the new encoded size.
Packet B retains its fixed palette offset/length when the palette is present:
body words saved are not a claim that every framebuffer publication shrinks.

Pattern0 is initialized once through the MD VDP, then reused. The options-screen
adapter invalidates its residency after using the VDP. An otherwise-empty B
packet must still consume its zero command after invalidation. Regular tile
data cannot write reserved slot0 through this backend. No new resident tile
pixel cache or VRAM allocation is introduced. Normal telemetry adds small
counters/state; the set and iterator are temporary master-stack storage.

## Correctness gates

Local evidence: `build/codex-tile-pipeline/`. All emulator runs use cold starts.

* `check_tile_batches.py`: actual C under ASan/UBSan, 131072 color cases against
  scalar pixels, all 1024 destination slots, sparse/reversed/duplicate sets, word
  boundaries, zero command, capacity limits and malformed runs. Independent
  packet interpreter reconstructs VRAM, including untouched patterns/canaries.
* `tile_vram_audit.py`: offline reconstruction from source tile ROM and captured
  pen maps, against committed MD VRAM/VSRAM scroll and name tables. Fine-scroll
  edges are included. Blank and solid patterns are checked explicitly. Fault
  tests reject wrong pattern pixels, stale pattern0, wrong palette cells and
  illegal hardware slots; a newer source camera cannot move the audited viewport.
* Fresh MAME headless4001 run regenerates all five frame2729 inputs byte for byte.
  Dummy SDL, fresh cfg/NVRAM, no GUI/keyboard/joystick; exact command/environment
  retained in `arcade/command.json`.
* Matched `tpcontrol04q`/`tpnative05q` ares fixtures: **0/71680 shown-bank pens
  differ and 0/343845 output-image pixels differ**, including the MD planes/text.
  Twelve native sprite records also pass:15962 visible/3143 FG-covered pixels,
  no missing/wrong/extra/leaked pens. Applied flip traces select the shown bank.
* Independent final MD audit: **2296 committed visible cells, 693 patterns,
  44352 pattern pixels; zero cell/pixel errors or invalid descriptors**.
* Actual options entry/return twice on both three- and six-button pads: pause and
  resume pass; pattern0 initializes twice, is reused208times, and reads back all
  zero after return. No malformed record or timeout. All five starting rounds
  also pass current-layout menu tapes with default DIP54/four lives.
* With the new flag off, preprocessing of MD consume/frontend and SH plane
  preparation equals the prior accepted experimental source. Default unchanged.

Frozen fixtures establish neither live whole-game parity nor normal-color
cadence. Native sprite auditing excludes calibration rows and refuses unsupported
zoom/shadow/priority modes. The MD audit validates captured map/tag promises,
not independent arcade RGB parity. Full-image equality is port-to-port under
matched frozen source state. No equality claim comes from thumbnail inspection.

The enlarged full-sound fixture reached the fixed ROM image fence. Both final
fixture sides disable music/speech/effects identically; the full-sound live and
interactive builds fit normally. No ROM addresses or linker fences were moved.
The earlier mismatched-audio comparison was rejected, not masked: audio/debug
pixels differed. The first host test also rejected an end-of-slot sentinel bug;
its unrun tp01 ROM is retained under `rejected/`, never offered for play.

## Measured work and promotion

Final implementation:396deda3. `tp05` completes 40000 timeline rows in40001
emulated frames:39986V-ints/39992ticks,35371flip publications, **zero timeouts,
zero malformed records and zero invalid producer slots**. It reaches level two
at36405, matching sp03, and has matching captured final camera/player/lives/
score/boss fields. The trace receives7990 ordinary pattern uploads,7141 reused
zero commands and one zero initialization. Grouping3478 patterns into1644 DMAs
yields **6156 data DMAs versus15132 one-DMA logical-pattern equivalents** for
that submitted work (59.3% fewer calls), plus one CPU zero initialization.
This is an operation census within that run, not a paired frame-rate improvement.
Encoded-but-skipped empty packets are excluded. Host flip-publication counts
barely move; none of these measurements establishes an FPGA speed gain.

Interactive `tpplay05.32x` retains normal controls, frontend, lives and full
sound; no embedded tape, GOD mode, fixed scene or timing bars. Its external-input
65001-frame soak completes64986V-ints/64992ticks,55601flip publications,
**zero timeouts and zero malformed records**; all1082 post-startup health samples
pass. Pattern0 is initialized once and reused18909times in this run. The five
level selects and both repeated-menu residency cases above use this final ROM.

SHA256: `a83a9c69a093f6ecac2db2ca1592b5b2209dde47b22325dfa8cf5a4f871406b4`.
The build stamp names parent490fc216; its changes were subsequently committed
as396deda3. Exact ROM hashes/source provenance are retained in local manifests;
the parent stamp alone must not be treated as clean-source identity.

Mike playtested tpplay05 and reports a stellar result; visual play acceptance
is recorded. Hardware timing and full125k/two-tape release regression
remain separate gates. No rig retry/upload or accepted-ROM overwrite occurred.
