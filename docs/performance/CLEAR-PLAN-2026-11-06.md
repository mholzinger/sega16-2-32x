# Clear-plan grouping experiment — withheld

Source `58c3a19a`, optional `R60CLEARPLAN=1`, default off. This is **not** in the
accepted `rdplay01` alpha and has not been deployed to the rig. Evidence:
`build/codex-clear-plan/`.

The portable `r60_clear_plan.h` folds packed row spans into existing caller-owned
row extents and sprite-run metadata. It records first/count once per contiguous
sprite run, rather than updating count for every row span. It retains the
monotonic-row, single-run and 255-span limits. A refused plan is discarded and
the existing fallback clears its scratch. Kept-span rules, actual framebuffer
stores and publication are unchanged. No persistent state is added.

## What passes

The actual C component passes ASan/UBSan against an independent whole-list oracle:
113,125 cases, including every endpoint-byte combination, ignored sprite bits,
initial row bounds/kept bytes, duplicates, increasing/decreasing rows, interrupted
runs, count overflow, invalid rows and 14 captured port span lists. Accepted
plans match complete extents, run starts and counts; refusal decisions match.
Default preprocessed renderer tokens match the accepted source with the flag off.

Deterministic interpreter prefixes 2,400/3,600, each with its own frozen listing:

| Work | rd01 control | cp01 grouped |
| --- | ---: | ---: |
| Function calls | 775 | 781 |
| Successfully processed spans | 574,969 | 580,586 |
| Planner instructions/call | 48,020.64 | 41,736.04 |
| Planner instructions/processed span | 64.73 | 56.14 |

This is 13.1% fewer instructions/call and 13.3% fewer per processed span. No
separate helper absorbed work. Frozen disassemblies identify the per-span PCs.
This precompute runs before a wait and may be hidden by that wait; instruction
savings do not establish a hardware frame-time benefit.

The fixed/full-clear fixture has zero changed framebuffer/output-image pixels,
and the settled MD audit has zero cell/pattern errors. **Full-clear bypasses the
normal clear path:** this verifies integration, not clearing-history correctness.
Normal 65,001-frame soak: 64,986 V-ints / 64,991 tick calls, 55,373 flip
publications, zero timeouts/malformed records, 1,082 healthy samples. Five level
selects and repeated menu returns on three-/six-button pads pass. Two ordinary
60-frame temporal samples were recorded; they are not a cadence-equivalence proof.

## Why it is withheld

The 40,000-frame GOD/tape route does not reproduce the control's progression.
Control rd01 reaches level two at 36,405, ending score 0x33800. cp01 remains in
level one, Neff handler 0x16C6C, score 0x15800 and F154=2. The first score difference
is at emulator frame 2,375 with matching native tick 0x05CE. A healthy tick counter
and pixel still do not override this failed progression check. No root cause is
claimed, and `cpplay01` must not be offered as a replacement alpha.

A complete narrow SH-2 write trace covers frames 2,340..2,389 around that score
difference: 1,806,916 writes, 37 planner entries, 34 successful completed plans.
An independent list oracle matches every completed plan's 896 row bytes,
64 run starts/counts, bank, bounds, input length and edge checksum. Applying
all traced writes to the deterministic seed reconstructs the complete final
256 KiB SDRAM dump with **zero differing bytes**. This finds no clear-plan
metadata fault in that window; it does not explain the route divergence or prove
all calls across the full run.

The earlier broad trace hit the headless tool's 8,000,000-event cap at frame
2,315. Its final-RAM reconstruction fails and it does not reach the score
difference. It is explicitly not a complete 300-frame audit. The narrow trace
avoids that cap. Results and input hashes remain in `target-audit/`.

Current priority is Mike's smoke/flash health benchmark on the accepted renderer,
not promotion of this optional experiment. The accepted alpha hash remains
`d7d2f4a395013e171c9ccb002e192473b09cc65645ce81549c12fc803326472f`.
