# Resolve source pages per contiguous row segment

Implementation c3927b0e, optional `R60PLANROWS=1`; default off. This continues
the visually play-accepted [tile-art backend](TILE-PIPELINE-2026-11-06.md).
Evidence: `build/codex-plane-rows/`.

The old plane walk extracted a quadrant/page and computed a physical source
address for every tile. `r60_plane_rows.h` resolves a contiguous physical-row
segment once, then supplies a volatile pointer. The adapter consumes that segment
before resolving the next. No row pixels/words are copied or persistently cached.
Every source word is still read live, once, in the original order before its
existing palette/slot/dirty processing.

The caller supplies the tilemap, physical-page count and page-selection word.
The component handles 64-column page boundaries, four quadrants, 128-column /
64-row world wrapping, negative camera bias represented as unsigned coordinates,
and invalid pages returning zero. The game adapter retains physical tile-bank
mapping, palette planning, pin ownership, row diffs and upload budgets. There is
no new V-int work, game logic, MD protocol change or persistent-cache invalidation.
Four master-owned telemetry counters occupy16bytes; the view/pointer/count use
temporary stack/register storage. Default preprocessing is unchanged.

## Independent correctness checks

`check_plane_rows.py` compiles the actual C component under ASan/UBSan and compares
it to an independent per-cell quadrant/page oracle. **5046273 row cases pass**:
all65536 page-selection words, both horizontal/vertical wrapping, invalid pages,
physical boundaries, unsigned camera bias and reads after a source write. The
inputs include145 saved native tilemap snapshots; hashes are in `host-inputs.json`.

A fresh headless MAME4001-frame run also reproduces all five frame2729 reference
inputs exactly. Fresh cfg/NVRAM, dummy SDL and no GUI/keyboard/joystick; full
command/environment retained in `arcade/command.json`.

Matched final `tpnative05q`/`prnative02q` ares fixtures pass:

| Check | Result |
| --- | ---: |
| Whole shown framebuffer pens differing | 0 / 71680 |
| Full output-image pixels differing, including MD planes/text | 0 / 343845 |
| Native sprite records / visible pens | 12 / 15962 |
| Foreground-covered sprite pens | 3143 |
| Missing/wrong/extra/leaked sprite pens | 0 |
| Committed MD visible cells differing | 0 / 2296 |
| Resident pattern pixels differing | 0 / 44352 |

Actual applied flip traces select the displayed bank; the MD audit uses committed
VRAM/VSRAM scroll, including fine-scroll edges. Both fixture sides disable audio
identically to fit the fixed ROM image fence. Full-sound live/player builds retain
audio. Frozen/PENID/full-clear checks do not prove live whole-game parity,
normal-color cadence or hardware speed. Native sprite calibration-row and
unsupported zoom/shadow/priority exclusions remain explicit.

## Instruction work

Ares interpreter prefixes2400/3600 are subtracted against their own frozen ROM
listings. Planner function entry counts normalize instruction totals per call.

| Variant | Calls | Planner instructions/call | Instructions/V-int |
| --- | ---: | ---: | ---: |
| tp05, original source walk | 1254 | 47360.39 | 49491.61 |
| pr01, boundary check inside the cell loop | 1242 | 45407.75 | 46997.03 |
| pr02, whole-segment inner loop | 1248 | 39603.16 | 41187.29 |

The final planner function executes **16.4% fewer instructions/call** in this
window. Claim/pen helpers did not absorb the removed work: claim instructions
remain similar and pen/pack helper totals are unchanged. Generic shift helper
totals are not attributed to this function. This is scoped instruction frequency,
not measured SH-2/FPGA time or a whole-frame speed claim.

Final window telemetry:7920 source segments for332640 cells. Across the40k live
route:556692 segments for14492604 cells, with no invalid segments. Shared
DIAG2[4/5] are not used for normalization: plane preparation and CRAM readback
both write those counters. The new row counters have one master writer.

## Live behavior and interactive build

`pr02` completes40000 timeline rows:39986V-ints/39992game ticks,35353flip
publications, zero transfer timeouts/malformed records. It enters level two at
36405 and matches tp05's captured final camera/player/lives/score/boss fields.
The initial pr01 variant had37940flip publications; that higher ares counter is
not evidence of a better hardware result. Neither variant is ranked as faster
on hardware from these counters. The refined variant reduces planner work.

`prplay02.32x` retains normal options, lives, controls and full sound. All five
level selects and repeated menu entry/return on both three-/six-button pads
pass; the reserved zero pattern is restored and reused with no malformed record
or timeout. The final normal-build external-input soak completes65001frames:
64986V-ints/64991ticks,55342flip publications, **zero timeouts and zero malformed
records**. All1082 post-startup health samples pass.

Interactive ROM SHA256:
`0d916620b74060fe7f96b034d3578f315dec3e54d48f8178b0eafed8e8c1ddf0`.
Mike subsequently play-accepted this build and shared it with friends as an
alpha. Hardware timing remains separate from these local scoped checks. Full125k/two-tape release regression has not been claimed.

Builds retain parent4dfbb64b stamps; their source was subsequently saved as
c3927b0e. Exact ROM hashes are retained in local manifests. The accepted p15bpt7
hardware ROM and visually accepted tpplay05 remain unchanged; no rig retry/upload.
