# Two-slot prepared sprite cache

Date: 2026-11-09
Flag: `R60SPRPREP2=1` (requires `R60SPRPREP=1`)
Player candidate: `build/port/rom/wsprep2play01.32x`
SHA-256: `fa7e43244d9c6abb55399349d4fc485a96dc6db785233ec011c499c599d0c181`

## Executive summary

We made the existing 4 KiB prepared-sprite workspace hold two useful poses
instead of one. No memory was added, the proven sprite packet and assembly
renderer were not changed, and the normal path remains the default unless the
new flag is enabled.

On MiSTer, the sprite plot phase in the same seven indexed busy-fight samples
fell from 157.7 to 125.7 timing lines per pass, about 20%. Whole-pass time fell
from 572.6 to 541.7 lines, about 5%. The smaller whole-pass change is expected:
foreground punch, clear, text and synchronization still remain.

## Why this shape

The accepted cache owns `0x0603D800..0x0603E800`, 1,024 words, and previously
gave all of it to one pose. A whole-game census showed:

- 83.9% of observed unscaled occurrences fit within 768 words;
- 35.4% fit within 256 words;
- height at most 24 routes 25.5% of occurrences to the small class, and only
  1.4% of that routed class exceeds 256 words.

The candidate therefore partitions the same arena into a 768-word large slot
and a 256-word small slot. Poses at height 24 or below try the small slot first
and safely fall back to the large slot when they do not fit. Taller poses use
the large slot. `R60SPRPREP2H=n` can override the height threshold for another
game after running the generic census tool.

At ares frame 3001, a same-source one-slot control recorded 665 cache hits. The
two-slot candidate recorded 759 large-slot plus 212 small-slot hits, 971 total:
46% more prepared-pose reuse. Both slots executed; this is not a capacity-only
or dead-code result.

## Rejected packet-format direction

`tools/baked_hole_census.py` also measured transparent four-pixel groups inside
the first-to-last opaque span of every unscaled corpus occurrence. Across all
five level corpora and the level-one tape, only 8.08% of stored groups were
interior holes. A sparse multi-run row format was rejected before integration:
its best-case work ceiling was too small to justify more headers and branches.

## Correctness and gates

- Original prepared-pose ASan/UBSan test: pass.
- New two-slot ASan/UBSan test: large reuse, small reuse, epoch invalidation,
  safe oversize decline and arena canaries pass.
- Working-set policy/lookup/page/epoch test: pass.
- Default-off build differs from accepted `wsline02` only in 12 build-stamp
  bytes.
- Fresh headless MAME level-one oracle: 5,000 frames, correct 66/54 DIPs,
  5,001 timeline rows and six state events.
- ares 6k: 5,989 VInts, 5,992 ticks, ticks track; temporal 48/59 and 55/59.
- ares 65k: 64,989 VInts, 64,991 ticks, 54,812 flips, 0.843 flips/VInt; ticks
  track. Accepted `wsline02` was 0.844.
- ares 125k: 124,989 VInts, 124,991 ticks, 105,580 flips, 0.845
  flips/VInt; ticks track through the old late-hang region.
- Frontend round-one start passed. The round-two case missed its menu input on
  both this candidate and an unmodified `wsline02` control (both ended at the
  same default level/DIP/lives), so it is a harness/baseline refusal rather
  than evidence for or against this renderer change. It was not waived.
- MiSTer capture: `build/accept/rig/cut_fight_wsprep2rig01_nativeart01/`;
  accepted reference: `build/accept/rig/cut_fight_wsline02_nativeart01/`.
- The rig was restored to `wsline02` after the capture.

The repository art-audit wrapper refused the named map because post-link ROM
packing changed loaded data bytes after the ELF was written. This is a provenance
refusal, not a passing or failing art comparison, and it was not bypassed.

## MiSTer phase comparison

Same indexed samples: 5, 10, 13, 15, 18, 21 and 23. Values are average timing
lines per render pass. Natural captures are not frame-locked, so the phase
result is the primary evidence and the percentage is not an FPS claim.

| Phase | `wsline02` | two-slot candidate |
|---|---:|---:|
| total | 572.6 | 541.7 |
| plot | 157.7 | 125.7 |
| punch | 65.1 | 57.1 |
| clear | 36.6 | 29.7 |
| wait | 174.9 | 188.6 |
| master tick | 98.6 | 88.1 |

The plot reduction is the intended effect. Other phase changes include natural
cohort variance; shot 15 has a large wait in both recent candidate captures.

## Status

Play-accepted on 2026-11-10. Mike played `sprengine-line01.32x` and reported no
regressions. Host safety tests, MAME oracle, ares 6k/65k/125k and MiSTer phase
measurement pass. The measured 768/256, height-24 policy is pinned explicitly
in `tools/build_line.sh`; other games remain opt-in and require their own proof.

## Engine extraction (2026-11-10)

The two-slot mechanism is now renderer policy rather than an Altered Beast
constant. `R60SPRPREP2S=n` selects the small slot's word capacity;
`R60SPRPREP2H=n` selects its height route; the large slot receives the rest of
the fixed 1,024-word arena. Compile-time checks reject empty or overlapping
partitions, and the Makefile rejects tuning variables without `R60SPRPREP2=1`.

The host test deliberately uses a non-default 832/192 partition and height 12,
and passes reuse, fallback, epoch, oversize and arena-canary checks. Both that
policy and the default 768/256, height-24 policy cross-build as target ROMs.
The new default-policy ROM `sprengine-default01.32x` differs from the measured
`wsprep2play01.32x` at only 14 byte positions, all inside the build-stamp string
(SHA-256
`d791d406453ccb9307eebb262937f816ba1ad887f081f61257984d096ee08759`).
The non-default cross-build is `sprengine-custom01.32x` (SHA-256
`566acc35e40ecb268583cdd9c9896a56ca20dfef8388c876af4338be0d3d6d9c`).
The default build's fresh ares 6k gate has
5,989 VInts, 5,992 ticks, 5,014 flips, and the same 48/59 and 55/59 temporal
checkpoints.

`tools/baked_hole_census.py --two-slot` now accepts arena, small-capacity and
height candidates and prints capacity eligibility. It explicitly does not call
that table a performance forecast: each game must measure temporal reuse and
hardware phase time before adopting a policy.
