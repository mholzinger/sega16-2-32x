# Build tile-row deltas without a shadow copy

Implementation `dfa6c81f`, optional `R60ROWDIFF=1`, default off. This follows
Mike's visual acceptance of `prplay02`, which he shared with friends as an
alpha. That ROM remains unchanged. Evidence: `build/codex-row-deltas/`.

The old plane walker copied all 64 name-table shadow cells into a stack row,
then overwrote the walked cells with newly resolved descriptors. Upload emission
only reads the completed walk: either its first..last changed subspan or the
full 16-cell pan strip. The optional path removes that initial copy. Offscreen
pin release reads the authoritative shadow directly, rather than a temporary
copy.

No source reads are skipped. Tile/page resolution, palette planning, slot
claims, pin changes, dirty-art handling, stale/solid fallbacks, row diff bounds,
packet format, simulation and publication ordering retain their existing
behavior. The 64-cell scratch allocation remains; only unnecessary reads/writes
are removed. This is generic plane preparation, with no Altered Beast
object/scene special case.

## Safety checks

`R60ROWDIFFCHECK=1` poisons all scratch cells, tracks which cells the actual
walk assigns, and counts an error if emission reads a cell not assigned in that
row. It also counts wraps, pan-strip rows and offscreen-release rows. This
instrumented walker runs from cart: the first full-sound checker exceeded the
existing RAM-code fence and was rejected. The normal build retains its original
code placement; no memory guard was relaxed. Diagnostic overhead is excluded
from profiles.

A fresh 4,001-frame headless MAME run reproduces frame 2,729 inputs, including
all five binary snapshots and the reference PNG, byte-for-byte. Both the
instrumented quiet fixture `rdnative01q` and final uninstrumented `rdnative02q`
pass these exact ares checks against `prnative02q`:

| Check | Result |
| --- | ---: |
| Shown-bank framebuffer pens differing | 0 / 71680 |
| Full output-image pixels differing | 0 / 343845 |
| Native visible sprite pens / FG-covered pens | 15962 / 3143 |
| Missing/wrong/extra/leaked sprite pens | 0 |
| Committed MD visible cells differing | 0 / 2296 |
| Resident pattern pixels differing | 0 / 44352 |

The instrumented fixture checker observed 28,742 rows and 23,539 emitted cells
with zero unwritten reads. This fixture has the initial four diagnostic
counters; the later live checker adds wrap/strip/release coverage counters
without changing the copy-removal algorithm. The final pixel/VRAM comparison
also passes with all row-check instrumentation disabled and normal walker code
placement. Actual flip traces choose the shown bank, and MD VRAM/VSRAM select
the committed scrolling view, including fine-scroll edges. Both quiet fixtures
disable sound equally to fit their fixed ROM-image fence. Normal/live builds
retain full sound. Frozen/PENID/full-clear checks do not establish live
whole-game parity, cadence, or hardware speed. Existing native sprite
calibration-row and unsupported zoom/shadow/priority exclusions remain.

Default preprocessing matches the accepted alpha source with ROWDIFF off. Normal
`rdplay01.flags` differs from `prplay02.flags` only by `-DR60_ROWDIFF`. Accepted
`prplay02`, `tpplay05` and hardware `p15bpt7` hashes were rechecked.

## Instruction work and live route

Deterministic ares interpreter prefixes 2,400/3,600 are subtracted using each
frozen ROM's own listing. Function-entry counts normalize the planner work:

| Variant | Calls | Planner instructions/call | Instructions/V-int |
| --- | ---: | ---: | ---: |
| pr02, accepted alpha's source-row planner | 1248 | 39603.16 | 41187.29 |
| rd01, no redundant shadow-row copy | 1234 | 37211.98 | 38266.32 |

This is **6.0% fewer planner instructions/call**, not a whole-frame or hardware
speed claim. The windows process 7,920/7,828 visible rows and 332,640/328,776
cells respectively; workload per call is similar. No helper replaces the removed
copy.

The 40,000-row GOD/tape timeline completes 39,986 V-ints / 39,992 ticks and
35,395 flip publications, with zero timeouts/malformed records. Level two begins
at 36,405, matching pr02. Captured final camera/player/lives/score/boss fields
also match. Some intermediate mirror fields differ at the same emulator frame;
these samples are not claimed to be a complete tick-aligned gameplay-equivalence
proof. See per-field counts in `live-proof.json`. Flip publications are not
hardware FPS.

All five current-menu level selects pass, with default DIP 0x54/four lives.
Actual repeated entry/return on three- and six-button pads restores tile pattern
zero, pauses/resumes and records zero timeouts/malformed packets.

## Long runs and player build

The full-sound checker completes 65,001 frames: 64,986 V-ints / 64,991 ticks,
55,292 flip publications, zero timeouts/malformed records and 1,082 healthy
post-startup samples. Its actual walk checks 583,952 rows / 750,853 emitted
cells with **zero unwritten reads**. Coverage includes 9,518 wrapped spans,
12,586 pan-strip rows, 70,058 offscreen-release rows and 78,969 changed rows. It
skips 37,372,928 shadow-copy words, each formerly read and copied to scratch.

The committed `tools/row_delta_audit.py` validates these eight counters and
requires wrap/strip/release coverage. Injected unwritten-read, missing-strip
coverage and inconsistent-copy-count records are all rejected.

The normal build completes 125,001 frames: 124,986 V-ints /
124,991 ticks, 105,652 flip publications, zero timeouts and
zero malformed records. All 2,082 post-startup health samples pass.
This external-input long run is distinct from the checker and GOD route.

`rdplay01.32x` has normal controls/options/lives/sound, no embedded tape, GOD,
fixture, bars, poison or written-mask instrumentation. SHA256:
`d7d2f4a395013e171c9ccb002e192473b09cc65645ce81549c12fc803326472f`. Its
source/build stamp is `dfa6c81f`. Final rdnative02q also uses this source/stamp.
Early rd01/native/checker probes retain parent `ab8afbf1` stamps; manifests and
hashes identify their exact artifacts.

Mike subsequently play-accepted rdplay01, reporting no regressions. It is the
latest accepted local play baseline; the optional flag remains off by default.
The externally hosted alpha may be updated by Mike. Hardware timing and the full
release regression suite remain separate. `hardware-pair.json` identifies the
saved pr02/rd01 timing probes: identical flags except ROWDIFF, same embedded
GOD route and R60RIGSPLIT=3 instrumentation, neither a player ROM. These are ready
for a future hardware A/B, not newly hardware-tested. No rig retry, upload or
config change.

## Repeat the audits

The per-build commands and frozen listings are retained alongside the local
manifests. For example, after capturing the eight-counter checker:

```
python3 tools/row_delta_audit.py \
  build/codex-row-deltas/rdcheck02/sd.bin \
  build/port/rom/rdcheck02.lst \
  --out build/codex-row-deltas/row-audit.json
```

The auditor requires wrap, strip and offscreen-release coverage by default.
`--allow-partial-coverage` is for shorter diagnostic samples; it still rejects
unwritten reads and inconsistent counters. It does not replace the independent
pixel/VRAM comparisons or transfer-health checks.
