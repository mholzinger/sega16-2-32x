# WORKSET mask-block trial: rejected on hardware

## Result

Do not promote the `R60MASKBLOCK` integration attempted on 2026-11-09.
The safe candidate made the real MiSTer punch phase substantially slower even
though its plot and clear phases stayed near the accepted line. All integration
source was reverted; `R60MASKBLOCK` remains excluded from `R60WORKSET`.

The useful conclusion is narrower than "large blocks do not work": composing a
32x32 foreground-opacity mask on demand is the wrong place to spend time. The
next punch optimization should retain the accepted priority-mask cache and batch
its existing row emission directly, without an intermediate composed cache.

## Memory audit

The first two layouts were not eligible for hardware:

- `wsmask01` put keys at `0x06029000`, which collides with palette/recolour
  scratch.
- `wsmask02` and `wsmask03` shared `0x06038000..0x06038A00` with the master
  plane builder. Complete-key checks and then a generation fence prevented
  stale reuse, but neither made concurrent master/slave access safe.

The only hardware candidate, `wsmask04`, used four cache slots in the
slave-private `0x0603E800..0x0603EA80` range: 512 bytes of rows plus 128 bytes
of complete source keys. This is after the accepted SPRPREP payload and before
the WORKSET mailbox at `0x0603EB00`. There is no master writer in that range.

## Correctness gates

- The configurable four-slot header passed the ASan/UBSan scalar oracle:
  34,835 lookups, 847 hits, 26,953 builds, exact pixels and metadata.
- The 6,001-frame ares gate passed: 5,989 VInts, 5,992 game ticks, 4,744 flips,
  4,925 passes. Temporal presentation was 44/59 at frame 600 and 55/59 at
  frame 1700. The first interval is below `wsline02`'s 48/59; the second ties
  its 55/59.
- The natural ares census at frame 3,000 recorded 40,247 lookups, 4,811 hits
  and 11,643 builds: 12.0% hits and 28.9% builds per lookup. The remaining
  lookups reject non-priority/empty blocks without a build.
- A fresh MAME level-one oracle completed at
  `build/accept/wsmask03-mame-oracle`. The subsequent unsafe-candidate port
  replay was stopped after the memory audit invalidated that candidate; it is
  not parity evidence for `wsmask04`.

Art audit refused the named ROM because the build system did not preserve a
provably matching symbol map. This known tooling failure is retained, not
treated as a pass.

## Hardware timing

Candidate ROM:

- `build/port/rom/wsmaskrig04.32x`
- SHA-256 `44d821876b153215bfbc680c3e749e726093316a771c268f3841235a063a25a0`
- capture `build/accept/rig/cut_fight_wsmaskrig04_nativeart01/`
- 23 screenshots; restore log confirms `wsline02` was selected afterward

The same seven indexed, render-busy samples used by the corrected `wsline02`
profile (5, 10, 13, 15, 18, 21, 23) give:

| phase, lines/pass | `wsline02` | `wsmaskrig04` |
| --- | ---: | ---: |
| total | 572.6 | 634.3 |
| plot | 157.7 | 154.3 |
| punch | 65.1 | 148.6 |
| clear | 36.6 | 33.1 |
| wait | 174.9 | 157.7 |
| master tick | 98.6 | 96.0 |

These are natural-attract timing cohorts, not frame-locked pixel comparisons.
They are sufficient for rejection: the intended phase more than doubled while
plot, clear and master tick did not absorb a compensating gain. Candidate heavy
passes reached 704, 720 and 736 lines, versus 624 maximum in the selected
accepted cohort.

## Default-off proof

After reverting the integration, `tools/build_line.sh wsline03` produced a ROM
that differs from accepted `wsline02` at exactly 14 bytes, all inside the build
stamp beginning at file offset `0x67FFD4`. No accepted code or data changed.
