# Priority punch boundary-merge trial — rejected

Date: 2026-11-09
Accepted reference: `wsline02.32x`, SHA-256 `d5b0b8fc80dc8c2cd4a3fb2f25b66606c92ac61e315ac96da9d1e3fd402b3d83`
Candidate: `wspmergerig04.32x`, SHA-256 `4f7c88698b792c8e9d055203a62cf1a33ea0545b21a4054b430329215a2cf1db`

## Question

Can PUNCH2 reduce 32X overwrite-image traffic by holding the trailing longword
of each 8-pixel tile row and OR-merging the leading partial longword of the next
adjacent priority tile?

## Correctness and fit

The candidate passed 20,000 randomized host cases under ASan/UBSan. The model
compared the original overwrite-image store sequence with the merged sequence
across every shift, random masks and random gaps. The compact target helper fit
the production linker caps without raising them. The RIG build reported region
2 at `0x06017100..0x06018e94` (7,572 bytes, below `0x06019000`).

Headless ares health also passed, but its temporal samples were neutral to worse:
45/59 at frame 600 and 55/59 at frame 1700, versus the accepted line's 48/59 and
55/59. That was not used as a hardware-speed claim.

## MiSTer result

The same indexed render-busy fight samples (5, 10, 13, 15, 18, 21 and 23) were
decoded from both 23-shot captures. Values below are average timing-strip lines
per render pass.

| Phase | `wsline02` | boundary merge |
|---|---:|---:|
| total | 572.6 | 621.7 |
| plot | 157.7 | 141.7 |
| punch | 65.1 | 113.1 |
| clear | 36.6 | 30.9 |
| wait | 174.9 | 192.0 |
| master tick | 98.6 | 93.3 |

The candidate reduced sampled plot time, but PUNCH2 bookkeeping and calls cost
far more than the saved boundary writes. Punch rose 74%, and the whole pass rose
9%. Shot 15 also carried a 440-line wait outlier, but removing that outlier would
not reverse the punch-phase loss.

Evidence:

- baseline: `build/accept/rig/cut_fight_wsline02_nativeart01/`
- candidate: `build/accept/rig/cut_fight_wspmergerig04_nativeart01/`

## Decision

Rejected. The integration source and diagnostic were removed. The MiSTer was
restored to `wsline02` by the capture script.

The useful design rule is narrower: do not add per-row or per-cell coordination
to save isolated VDP stores. The next punch attempt must batch at a coarser level
or precompute complete output rows so the inner loop becomes simpler than the
accepted emitter, not more complicated.
