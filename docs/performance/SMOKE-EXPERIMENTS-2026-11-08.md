# Smoke rendering experiments — 2026-11-08

The block renderer has a repeatable fixed-scene hardware gain, but remains experimental.
Accepted `rdplay01` is still the play and restore baseline. No new alpha is
published by this work.

## What changed

The private block renderer checks immutable baked packet bodies during artifact
certification, rather than checking every pen again during drawing. Runtime
admission still checks exact packet membership, header, extent, active mapper
page and generation, and the current GO lease. Mutable or unknown input keeps
its existing validator and fallback behavior.

This is a reusable graphics-pipeline pattern: validate fixed assets once and
retain the checks needed for their current mapping and ownership.

## Hardware evidence

Each row below is a separately launched exploratory pair. The workload is a
frozen sixteen-cloud scene over 2,000 VInts. These are completed batches, not
unique displayed images, normal gameplay FPS or processor utilization.

| Private candidate | Reference batches | Candidate batches | Status |
| --- | ---: | ---: | --- |
| Word block renderer | 113 | 86 | Fewer batches in this pair; held |
| Compact foreground masks | 109 | 104 | Fewer batches in this pair; held |
| Immutable packet admission, pair 1 | 111 | 124 | Qualified diagnostic pair |
| Immutable packet admission, pair 2 | 112 | 124 | Qualified diagnostic pair |
| Immutable packet admission, pair 3 | 111 | 124 | Qualified diagnostic pair |
| Row index versus immutable block baseline, pair 1 | 124 | 124 | Tie in this pair; held |

The row-index pair uses the already optimized immutable block renderer as its
reference. The earlier rows use the existing direct renderer. Do not pool those
references or infer a combined gain from different experiments.

Across the three immutable pairs, the reference completes 334 batches and the
candidate 372 over 6,000 VInts each: **11.38% more completed fixed-scene work**.
ACK totals are separately 335 and 375. All six arms have clean measured health
deltas and independently verified run identity, captures and restoration.
Reference pair 3 has 1,999 service calls; the other five have 2,000. This stays
within the unchanged source-qualified 1,992..2,008 domain. No service-based
renormalization is applied.

| Pair | Reference crossings, start/end | Candidate crossings, start/end | Startup drop totals, R / C | Startup preflight totals, R / C |
| --- | --- | --- | --- | --- |
| 1 | 0 / 1 | 1 / 1 | 0→0 / 1→1 | 1→1 / 1→1 |
| 2 | 0 / 0 | 1 / 1 | 2→2 / 1→1 | 1→1 / 1→1 |
| 3 | 1 / 0 | 1 / 1 | 0→0 / 0→0 | 1→1 / 0→0 |

Two early pair-3 reference screenshots fail footer decoding and receive no
credit; both originals and rejection reasons remain. The later complete sample
sets pass. All 111 qualified screenshots from the three pairs have exactly equal RGB
pixels above row 208. This is a captured-region comparison, not raw-pen or
cadence proof. All trials restore the accepted ROM; its hash and selected path
were reverified at 09:07 UTC.

A separate native smoke test removed the slave's diagnostic write while idle.
Entrance ACK counts were 32 control / 31 quiet over matching 126 native ticks
and VInts. Death counts were 33 / 34, but elapsed windows differed at 124 / 126
VInts; that elapsed comparison is withheld. The polling change stays private.

## Correctness and limits

The immutable pair passes final linked-code checks and fresh headless ares
A/B/C analysis, including native sprite pixels, full framebuffer backing,
physical-bank ownership, spans and ordered metadata. Native reference data comes
from MAME sprite tables and ROM data. Corrected v2 proofs retain the original
failed beam-order checker and raw captures; they do not silently reinterpret
an earlier failed hardware run.

Hardware captures establish measured work counts and health, not raw
framebuffer pixel parity or normal-play cadence. Coherent arcade foreground
presentation remains a separate open problem: it needs a snapshot tied to the
Mega Drive's actually applied plane state. A native tile shadow or generation
counter alone does not prove that. Same-pass backend equivalence can be studied
against the existing renderer without claiming to fix that presentation defect.

## Next bounded changes

The two fresh nonce-only repeat pairs are complete. Whole-ROM derivation
proofs verify unchanged layout, code, assets, MD and flags apart from explicit
run markers and build stamps.

A separate private row-index prototype uses 124 bytes of baked offsets to
start each block near its required source rows. In the fixed scene's geometry
it removes 74.84% of irrelevant prefix visits. This is a work-count estimate,
not a speed prediction. Independent host/object review passes: the closure is
7,952 / 8,192 bytes, with no new writable storage and 24 additional stack bytes.
The pixel tests include clipping, partial heights, all XO offsets and the final
physical tail. The preceding source row stays available for physical row spill.
Its separate target pair passes full fresh headless A/B/C and independent
review: all backing bytes, spans, ordered metadata, native/PEN checks and actual
physical-bank ownership agree. The indexed path executes on the slave; static
stack bounds are 1,480 / 1,504 bytes and observed depths 1,120 / 1,144 are only
bounded lower bounds. Raw completed-bank calibration differences are 2,528
pens per arm, separately from the raw frame-8000 PNG differences of 10,160
pixels each; the calibration-derived image check is exact.

The first hardware pair is a tie at 124 complete batches and 125 ACKs per arm,
with matching 2,000 VInts/services, crossings 1/1 and clean measured health.
Startup drops are 1→1 in both, preflight 1→1 reference / 0→0 candidate. Both
restore successfully; accepted hash/path reverified at 10:05 UTC. The 37
qualified screenshots have equal top-208-row RGB; two early reference images
remain rejected. No gain is established, so the row index stays experimental.
No extra pairs are run to pursue an improvement below this measurement's
resolution.

A separate on-chip scratch prototype redirects only the 1,024-byte tile and
128-byte mask into CPU-local data-array RAM, leaving the existing arena and
caller ABI in place. Its default-off object is exactly the 7,508-byte baseline;
the active object fits at 7,672 bytes with no new writable footprint. Independent
host/object review passes, including full pixel oracles and tests of the emitted
transition instructions. Those tests do not prove hardware pipeline or IRQ
safety. The focused cssafe02 conformance cart passes independent cold headless review.
Its extended32,001-frame run authenticates all125 captures and every16 result
page repeatedly, with unchanged32 result words. Physical component testing
awaits independent extended-result review before any drawing trial.
It trades half the cache for scratch memory; no speed improvement is predicted.
Known cache-address readback model defects remain explicitly separate, with no
emulator patch or changed expected hardware bit.

Evidence is local under `build/codex-sprite-block-audit/`:

- `hardware-immutable-meter01/hardware-independent/THREE-PAIR-SUMMARY.json`
- `hardware-immutable-meter01/ABC-QUALIFICATION-REVIEWED-v2.json`
- `hardware-immutable-meter01/nonce-siblings-plan01/INDEPENDENT-DERIVATIVES-REVIEW.json`
- `quiet-idle01/quiet-smoke-firstpair01/hardware/independent/pair01-proof.json`
- `row-checkpoint01/INDEPENDENT-REVIEW.json`
- `row-checkpoint01/target-pair01/headless/ABC-REVIEWED.json`
- `hardware-row-checkpoint-meter01/hardware-independent/PAIR01-SUMMARY.json`
- `smoke-architecture-next01/REPORT.md`
- `natural-oldpath-differential01/PLAN.md`
- `cache-scratch01/COMPONENT-CONTRACT.md`
- `cache-scratch01/INDEPENDENT-REVIEW.json`

Observer04 now captures eight complete normal-play passes with independently
qualified actual source operands. The unchanged generic HOST block compositor
and an independent scalar old-word/PUNCH oracle each reproduce all71,684
pre-text backing bytes and31 structural metadata objects plus rectangle counts
for every pass. Conflicting or missing relevant inputs decline. ActualSH replay
is next; this is not a live caller or speed result. The separate wslease01
normal snapshot ownership candidate passed linked static checks and entered
normal runtime acceptance; no gameplay corruption fix is claimed from the model.

The live operation status is in `STATE.md`; detailed historical evidence remains
in the dated handoff log and original artifact directories.
