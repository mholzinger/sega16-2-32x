# Headless arcade validation and mask-loop refinement

Continuation of [the first mask experiment](MASK-BLOCKS-2026-11-06.md).
Audit code: d89dbf14. Mask-loop refinement: 5f633987.
Both MAME headless and ares headless were run. The experimental path remains off;
no new player ROM is promoted. Local evidence: `build/codex-maskblocks/headless/`.

## Fresh arcade reference

A cold headless MAME `altbeast` run replayed `level1_full.csv` through frame 36001,
with matching GOD lives pinning, fresh cfg/NVRAM directories, dummy SDL drivers,
`-video none -sound none -keyboardprovider none -nomouse -nojoystick`.
The log confirms DIP bytes 66/54 and normal round-zero entry. Exact command and
environment are in `arcade/command.json`; Lua exit marker and process exit both pass.

At arcade frame 2729, all five regenerated RAM dumps match the original fixture
inputs byte for byte: sprite, tilemap, text, palette and WRAM. Twelve fresh source
snapshots spanning level one and early level two also drive the independent C mask
oracle. The final host check covers 3512 cases with ASan/UBSan and exact framebuffer
bytes/cleanup metadata. Its backing image now uses the actual 320-pixel linear
stride plus 1024 guard bytes, exercising canvas offsets 0..400 across physical rows.

## Direct native sprite proof

`tools/mask_fixture_compare.py --verify-native-image` now checks each port fixture
against the arcade sprite commands and sprite ROM, rather than only comparing two
port outputs. `tools/sprite_scene_audit.py` explicitly accounts for foreground and
text occlusion and reports per-sprite missing/wrong/extra pixels and priority leaks.
Bank-pen-zero is opaque garbage when present; it is never excused as literal zero.
The audit also requires the reserved through pen for foreground-covered sprite pixels.

For both the old renderer control and refined mask fixture:

| Check | Result |
| --- | ---: |
| Native sprite records checked | 12 |
| Visible sprite pixels | 15962 |
| Sprite pixels covered by priority foreground | 3143 |
| Missing visible pixels | 0 |
| Wrong palette/pen pixels | 0 |
| Foreground leaks / wrong through pens | 0 / 0 |
| Extra opaque pixels in audited scene | 0 |
| Full shown-framebuffer pens differing from control | 0 / 71680 |

The native reference's 15962 visible pixels also match the fresh MAME PNG after
normal native palette conversion. Port comparison uses exact framebuffer pens and
the captured bank-to-native-palette map, not RGB similarity. Actual applied flip
traces select the shown physical bank separately for each run; hidden banks can
be in flight. All source words, both sprite snapshots, scroll/page state and tile
bank are checked before pixel comparison.

The bottom 16 calibration rows are excluded from direct native sprite auditing;
all twelve sprites lie above them. The complete 224-row port-to-control comparison
includes those rows. Fixed-scene/full-clear/PENID results do not prove normal color
delivery or cadence. Zoom/shadow records and sprite priorities 0/1 are refused
where the reference lacks the required compositor. Eight fault-injection tests
check missing pens, remapping, zero artifacts, priority leaks, extra pixels and
explicit coverage exclusions; existing nine sprite-pixel tests still pass.

Final proof: `mb04-native-comparison.json`. Earlier prototype proof:
`native-shown-comparison.json`. Both include input hashes.

## Measured work and bounded optimization

The first prototype was correct but expensive. The refined version still checks
all sixteen source keys before cache reuse. A four-by-four-cell block cannot cross
a 64-by-32-cell physical page, so page/quadrant resolution now occurs once per
lookup; four short row loads replace sixteen cell-address calculations. Key and
priority tests are combined without per-cell branches.

The emitter visits only four-pixel groups intersecting the clipped box. Leading
zero groups are discarded with an unsigned power-of-two multiply, avoiding an
SH-2 variable-shift helper. No new mask arena or dirty-generation assumption was
introduced. The established path and its default flags are unchanged.

Deterministic interpreter prefix profiles isolate frames 2400..3600 of the same
live tape. These are instruction counts, not FPGA time:

| Metric in this 1200-frame window | First prototype mb02 | Refined mb04 |
| --- | ---: | ---: |
| Render passes | 685 | 719 |
| Block lookups | 31277 | 32006 |
| Block builds | 5 | 5 |
| Lookup-helper instructions per lookup | 829.2 | 341.0 |
| Lookup + punch-helper instructions per pass | 89413.9 | 54484.2 |

The two helpers use about 39% fewer instructions per pass in this sampled window.
This excludes caller bookkeeping and is not an FPS claim. Pass counters count launches, so boundary passes can be partial. Different
presentation rates also change the sampled pose mix. All 1024 populated refined cache rows
reconstruct exactly from their stored source keys after the profile and long run.

## Live headless progression

MAME completed 36001 frames. Old tile renderer `perf04`, first mask prototype
`mb02`, and refined `mb04` each completed 40000-frame ares timelines from cold boot
with the same embedded GOD/input tape. The capacity-matched old tile-cache control
`mbcontrol512` also completed the initial 36000-frame run. Ares runs 40001
emulated frames to record timeline rows 1..40000; end diagnostic dumps follow
that run.

| Event or counter | Arcade | Old port perf04 | mb02 | mb04 |
| --- | ---: | ---: | ---: | ---: |
| Boss transform handler first observed | 30667 | 30632 | 30632 | 30632 |
| Round-clear release first observed | 34220 | 35356 | 35356 | 35356 |
| Level two first observed | 34910 | 36405 | 36405 | 36405 |
| End vints / ticks | — | 39986 / 39992 | 39986 / 39992 | 39986 / 39992 |
| Span timeouts | — | 0 | 0 | 0 |
| End framebuffer flips in ares (40001 emulated frames) | — | 35325 | 33504 | 34581 |

At frame 40000 all three port runs have the same captured gameplay fields,
including level/round 1, scroll 11D3, player position, handlers, lives and score.
A one-frame difference in two earlier sampled milestones remains visible in the
refined run; do not call the full timelines identical. The original mask/control
36000-frame comparison likewise has four shared timer keys with different observed
state sets, each containing a common state; raw sampling is not a per-tick trace.

The late arcade-to-port transition is reproduced by the old renderer. It is not
closed as arcade parity or attributed to the new mask code. The new prototype
improves on mb02's ares flip count but still trails the established tile renderer.
That measured model regression is another reason to keep it experimental. Do not
rank FPGA throughput from these model counts; hardware timing remains outstanding.

Current mask headroom: `_end=060172C0`, hot region 2 end `06018EC0` (320 bytes
below the limit), RAMTEXT end `06037D0C` (756 bytes below the limit). All 39 hot
functions / 802 dispatch rows and quantiser agreement over 65536 words pass.

These are diagnostic builds, not releases. Their build stamps precede the local
commits; use `roms.json`, flag files, archived listings and build logs for identity.
`mb03` is an intermediate build; `mb04` is the refined live probe and `mbscene04`
is its correctness-only fixture. Accepted normal p15bpt7 remains the player ROM.
