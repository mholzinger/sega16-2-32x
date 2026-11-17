# Whole-event smoke measurement and scaler experiments

The playable baseline remains rdplay01, source dfa6c81f, SHA256
`d7d2f4a395013e171c9ccb002e192473b09cc65645ce81549c12fc803326472f`.
Mike accepts full play on this ROM. The old frozen-smoke message was explicitly
withdrawn as stale; it is not a new hang. Every rig probe restores rdplay01.
No new renderer is promoted by these experiments.

Current outcome: KEEP improves completed hardware handoffs by14.9% entrance
and7.5% death in three controlled launches per arm. The normal candidate remains
held for a confirmed extra boot black frame. Every tested small-job master
worker is pixel-correct but slower on hardware; all stay disabled. Accepted
rdplay01 is restored, with remote hash and fresh display advancement verified.
Actual normal rebuilds have zero code/data differences outside the build stamp.

Mike explicitly authorized parallel agents and Claude/API use on November 7.
That supersedes HANDOFF-CODEX rule10 for this task. A bounded, tools-disabled
Claude CLI audit could not authenticate and used zero tokens; no credentials or
configuration were changed. Codex agents performed independent source, pixel,
measurement and launcher reviews.

## Complete native census changes the target

Evidence: `build/codex-clip-smoke/native-census/REPORT.md` and JSON.
A fresh headless MAME run completed40,001 frames with every sprite table.
The earlier bench300 run was incomplete and is excluded. All142 saved tables
match their corresponding every-frame blocks;432,146 active-object rows were
cross-checked. Four first-Neff handlers cover246/131/136/120 native frames.
They contain **zero visible left-clipped zoomed records**. Optimizing only the
left-clipped fallback cannot improve this smoke sequence.

Cloud source bank3,address01d0,pitch20,forward accounts for4,657,843 of4,815,804
zoomed full-row source words (96.7%). These are workload weights decoded through
the terminator, before horizontal early stopping; they are not cycle counts.
Right clipping occurs frequently and remains part of the correctness contract.
Two rare left-clip fixtures2615/2619 and dense smoke fixtures30677/33614 were
captured in a second fresh MAME pass. All33,615 timeline rows match the first
run prefix, and all four selected sprite blocks match. Their native RGB
references have zero mismatches over67,498 visible sprite pixels.

## Compacted-word scaler: correct first fixture, hardware regression

Optional R60ZOOMWORDS changes the horizontal scaler without allocating an art
atlas. It skips wholly left-clipped source words and compacts selected pixels
before the original overwrite stores. Default is OFF. The old full-group padded
right-edge behavior is preserved; transparent zero bytes preserve old pixels.
The forward kernel runs in existing RAM; flipped runs in cartridge because both
in RAM exceeded the unchanged linker cap. No guard was relaxed.

Actual C ASan/UBSan oracle:4,449,972 cases, all16-bit words,32 zoom values,
palettes, flips, alignments, clipping, padded stores, returned x and guard bytes.
The native7544 actual-SH fixture has zero differences across71,680 framebuffer
pens and343,845 screenshot pixels, with18,458 visible native sprite pixels and
zero missing/extra/wrong pens. This is one scene, not full-game parity.

The first real fixed-scene probe zwfixed02-a gives median PLOT1440 scanlines
against the prior original1056, seven valid samples. Applied-bank acknowledgments
are8.23/s in this artificial repeated-plane-copy/full-clear/quiet-sound fixture.
This is a **regression**, not a candidate speed gain. All810 scaled rows in this
fixture are forward; placing flipped code in cartridge cannot explain it.
The audit finds a new count[g] cartridge read per source word, more dispatches
and clipping checks, and spills. Onscreen stores are unchanged, so there is no
store-count saving to pay for the extra instructions. Keep this path out of alpha.
Evidence: `hardware-fixed-word-a.json`, `target-audit/`, `rig/cut_zwfixed02-a/`.

## Durable event recorder

Probe-only R60EVENTMETER1 records combined entrance/death phases;2 selects
zoom-cloud handlers1794c/17b54 only. Game-specific classification is separate
from the reusable streaming recorder. Master records completed native tick,
VInt and acknowledgment intervals; snapshot sequence/class tags let the slave
record its actual rendered cohort. Each CPU owns separate record fields. The
first completed events are retained, with explicit partial/missing/tag-gap flags.
No production BSS or atlas is added; the guarded reservation is30E00..31000.
Five rotating footer pages expose retained32-bit words with duplicate rows and
page/phase/field markers. Host tests cover lifecycle, dropped tags, seq wrap,
retention and actual word painting/decoding. The linker rejects hero overlays
which could overlap this probe reservation.

Existing16-bit FRT segment sums can wrap. Work figures are explicitly marked
UNCERTAIN. Native/VInt/ack intervals and pass-edge counts remain separate.
A pass crossing at least two VInt boundaries proves an overrun; zero or one
crossing does not prove completion within one frame. Ares is the correctness
instrument, not a hardware-speed oracle.

The initial ec01/ew01 runs exposed an integration error: event initialization
was inside TC_PADTEST and never executed in normal game builds. Both complete
master intervals126/124 were observed, but magic, event IDs and all slave
records were zero. Footer decoding succeeded and revealed the fault; these
runs are **invalid for slave timing or rendered coverage**. Correct initialization
and a cold ares validation are required before using the recorder on hardware.

## Launcher integrity

mister_push now checks HTTP errors and only newly appended launcher-log bytes,
with a literal requested basename and recognized elapsed-time suffix. It rejects
wrong ROMs, stale logs, truncation and rotation. A successful log confirms the
launcher request, not the display or cold boot. rig_cut aborts capture after a
failed launch, always attempts restoration, and propagates restoration failure.
Thirty mocked launcher cases and five capture/restore cases pass. Physical
picture/advancement verification remains separate.

## Independent keep-pattern experiment

R60ZOOMKEEP is separate from WORDS and retains the old row kernels. Its immutable
512-byte table replaces the64 per-sprite carry decisions. Sixteen bytes are
copied into the existing caller buffer once per zoomed sprite; eight constant
shift iterations retain scalar/check masks. There is no per-row table lookup or
new BSS. Host checks cover288 bounded copies,2,097,152 independent carry decisions
and16,384 reconstructed-mask bits. Hardware timing is pending; default is OFF.

Flag-off guard stripping preserves68,308 main tokens. Full SH preprocessing with
the accepted rdplay01 flags preserves all58,990 expanded tokens exactly, SHA256
`f51fb0eb1abb1ddb520fb46eaf619316b6f13c67849e944fc211e48282ca8e93`.
No new header or changed meter header is included under accepted flags.
Evidence: `target-audit/flagoff-{guard,cpp}-proof.json`.

The corrected ec02 cold16,001-frame ares run sets magic45565035, retains event
IDs1/2, completes both master/slave records and records31/35 rendered samples.
Master native/VInt durations are126/126 and124/124. Records explicitly show
head-partial, tag-gap and uncertain-work flags; no claim that all poses appeared.
These values validate instrumentation, not FPGA speed. The adapter reads a
16-bit native clock; these first-event ranges15cf..164d and1aed..1b69 do not
wrap. Broader event coverage requires width handling. SOLID debug rendering
bypasses normal finish hooks and is rejected with EVENTMETER.

Four additional frozen WORDS/control scene pairs now pass actual headless ares:
frames2615/2619/30677/33614,4000 frames per ROM. Zero differences across286,720
shown framebuffer pens and1,375,380 full screenshot pixels. Both sides have
zero missing/extra/wrong palette/foreground-leak/through-pen errors across67,498
visible native sprite pixels each. Native reference RGB error is0/67,498; port
PENID screenshot error is0/134,996 across all eight images. Inputs, both sprite
snapshots, plane/text/priority state and shown physical banks are checked.
Evidence: `four-scene-proof/{REPORT.md,summary.json,manifest.json}`.
Correctness does not reverse the hardware regression verdict.

KEEP also passes the four independent original-kernel scene comparisons: no
WORDS compiler flag is present. All four actual flag sets add only ZOOMKEEP.
Zero differences across286,720 shown pens and1,375,380 screenshot pixels;
native/PENID/priority checks have the same zero errors and67,498 visible native
pixels as the WORDS proof. These fixtures use full clearing; they do not establish
normal-route clearing or gameplay parity. Evidence: `keep-four-scene-proof/`.

## First corrected hardware event run

Nine native320x224 captures in ec02-a cover all five footer pages. Repeated
pages are immutable and every signature isEC20. No saturation occurred.

| Event | Native ticks | VInts | Applied bank acks | Rendered cohort samples | Samples crossing >=2 VInt edges |
| --- | ---: | ---: | ---: | ---: | ---: |
| Entrance cloud |126|126|22|21|21|
| Death cloud |124|124|27|27|24|

This directly separates normal native clock advancement from sparse rendering.
Bank acks and sampled rendered passes count different intervals and need not be
identical. Both cohorts retain HEAD_PARTIAL, TAG_GAP and UNCERTAIN flags; no claim
that every pose was rendered. The VInt-edge overrun fraction is a lower bound,
not exact cycle timing. This is a forced-form/end-clear/sound-enabled diagnostic
route, not unrestricted play. Its first completed records remain visible afterward.
Evidence: `rig/ec02-a-event-summary.json`, `rig/cut_ec02-a/`.

Source implementation is committed locally as7d07d33e; launcher reliability as
7a5967db. ec02/ek02 and earlier WORDS probes were frozen from the precommit
worktree (stamps457bab32 or7a5967db); hashes/commands and exact flag differences
identify them, not a claim of a clean release build. `experiment-manifest.json`
records named artifacts. Both experimental renderer flags remain default OFF.

First KEEP hardware trial ek02-a also validates all five pages/signatureEC21.
Entrance126/126 clocks:25 bank acks,26 sampled passes,25 crossing>=2VInt edges.
Death124/124 clocks:28acks,28samples,24 crossing>=2. Raw modular PLOT display
is842/730 versus first control1001/791; these remain uncertain raw work figures.
This is a promising single trial, **not a speed verdict**. Repeating the control
and independent KEEP launches is required to distinguish variance. No new
playable ROM has been promoted. The new generic tools/rig_event_summary.py
summarizes phase1/2 and actual signatures without game/ROM/duration assumptions;
synthetic image bundles and invalid-data rejection pass.

Second original control ec02-b: entrance23acks/23samples, death26/26; clocks
remain126/124. First KEEP result remains above both original controls on entrance,
but repeats are running before a verdict. Raw uncertain PLOT955/790 in control B.

The normal GOD/tape KEEP route zkroute01 completes40,001 ares frames:
39,986VInts,39,992ticks,35,426 publications, zero timeouts/malformed/dropped ticks/
missed indirect dispatches. Level2 starts at36405, identical to rd01. Every
sample matches level/round/boss handler/score/lives/round-clear; all final fields
match. Some sampled tick/position/pad/scroll/status fields differ by brief windows;
one single-sample boss flash is skipped in a frame-aligned status trace. This
is progression/health evidence, not complete state or visible cadence equivalence.
Evidence: `natural-route/zkroute01/{REPORT.md,comparison.json,manifest.json}`.

Normal controller-driven zkplay01 (source68c7327c) is built with exactly the
accepted rdplay01 feature flags plus R60ZOOMKEEP. Long65,001/125,001 external-input
soaks and actual-menu checks are running. It is a candidate, not play acceptance.
A standalone four-row raw chunk decoder and two-slot transport are now being
implemented under host tests; see MASTER-SPRITE-CHUNKS.md. They are not included
in this ROM, and no dual-processor worker speed claim is made.

Second KEEP ek02-b: entrance26acks/26samples, death29acks/28samples. All clocks
still126/124, no saturation, all five pages stable/signatureEC21. Raw uncertain
PLOT837/732. A final control/KEEP pair is running to complete three launches per
arm. Counts describe this forced-route probe, not general FPS or unique images.

Normal zkplay01 SHA256
`fbd61674e0e02d021770e9df1ab1cbf55c9bd7816029e71adfce9b95896a26e3`.
Five actual level-select starts and both three-/six-button repeated-menu
residency checks pass; no malformed records/timeouts/reserved VRAM corruption.
Actual compiler flags add only ZOOMKEEP over accepted rdplay01. Long-soak results
are recorded below. Evidence: `menu/{REPORT.md,results.json,manifest.json}`.

## Three separately launched hardware runs per arm

| Phase | Original acks A/B/C | KEEP acks A/B/C | Original rendered samples | KEEP rendered samples |
| --- | --- | --- | --- | --- |
| Entrance |22/23/22|25/26/26|21/23/21|26/26/26|
| Death |27/26/27|28/29/29|27/26/26|28/28/30|

Each launch retains native/VInt durations126/126 and124/124, stable five-page
records, expectedEC20/EC21 signatures and explicit partial/gap/uncertain flags.
No counter saturation. The mean acknowledgment gain is14.9% entrance and7.5%
death in THIS instrumented forced route. These acknowledgments are not unique
image counts or general-game FPS. Rendered-cohort samples can include an already
pending tagged pass after the native event boundary; do not divide them blindly
into a presentation rate. The actual event clock acknowledgment difference is
the primary progress comparison. Sparse later photos recover the retained event,
rather than estimating its duration from capture timestamps.

All three changed entrance counts exceed all three originals; the same holds
for death acknowledgments. This supports a repeatable scoped improvement.
Hardware general-game performance, white-wolf flash cadence, full-arcade parity
and player acceptance remain separate. No idle-CPU utilization claim follows.
Evidence: `rig/three-launch-comparison.json` plus all six cut directories.
rdplay01 was restored after each probe, last launch log01:33:28; physical picture
and current remote hash verification will follow before leaving the rig.

Normal zkplay01 completes65,001 external-input ares frames:64,985VInts,
64,991ticks,55,499publications,1,082 healthy periodic samples, zero timeouts and
malformed records. Its125,001-frame result is recorded below. No worker code is in
this candidate. Evidence: `zkplay01-65001/result.json`.

Normal125,001-frame soak also passes:124,985VInts,124,991ticks,105,731publications,
2,082 healthy samples, zero timeouts/malformed records. Both named hashes match.
The supplementary acceptance report records zero art errors at1200/3700 on both
ROMs, matching roster handler sets and65,536 quantizer inputs. Exact temporal
sequences differ: extra short ABA recurrences, no new cells spanning three full
period-two cycles; all examined new-cell states also exist in accepted control.
That does not prove cadence equality. Candidate attract frame469 is black while
control shows gameplay; both show gameplay by captured472. Historical arc6k
corpus lacks a provenance manifest. Paired original object gates are recorded in the completed-gate section below. See `acceptance/RESULTS.md`; no blanket presentation equivalence claim.

The standalone adapter host test now exercises7,936 cases/mode and150,784rows
against old-word pixels/end/store count, including mixed prepared/fallback,
CARTSSF bank6 partial/bank7 absent bounds and failed-generation suppression.
State is232B, total existing spare usage1000B; counters and source lifetime are
explicit. Initial mcnative01 target link fails the unchanged ROM-image cap:
end234B94 vs234000 (2964B), while primaryRAM37FF0 and secondary18C5C fit.
No ROM from that failed build was deployed. Four worker functions occupy3276B
in the saved LTO object; moving them into the already fixed resident gap
2A3300..2A4000 is being guarded explicitly, with a second optional probe gap
297690..298000. Art/audio capacities and original fences must remain intact.
Actual target assembly pixels and nonzero prepared/used rows are required next.

All rig probes ended with rdplay01 restored. Its remote SHA256 matches and two
fresh captures show title then eye, verifying actual displayed advancement.
This is restoration evidence, not an equality/cadence proof.

## Paired original object gate, complete

Both frozen ROMs ran the original check_demo.py with ares-headless-ssf and two
workers each. Accepted rdplay01:18zero-byte rows, H#7=13bytes ataw_H_1170,
H#13NOTREACHED. Candidate zkplay01:19reached rows allzero; H#7 is explicitly
fallbackaw_H_1169 (different selected native frame), H#13stillNOTREACHED.
Do not call that a newly repaired H#7 or20/20exact targets. The old T0096 picture
class remains1057bytes in both; E00ca3114control/3184candidate are the same old
picture class. Legacy framebuffer staging checks remain stale19ticks on both;
they are not actual committed-MD-VRAM evidence. Both tools exit1 for retained
exceptions; compare individual rows rather than declaring a green headline.
Logs/tables: `demo-gate-{rdplay01,zkplay01}.log`, `demo-gate/comparison.json`.

The worker's first real target scene mcnative02 proves nonzero execution:
954prepared chunks,1942used rows,722608decoded words. Exact shown pens0/71680,
full screenshot0/343845; native16169visible sprite pixels and PNGPENID checks
allzero errors. But only0.919%eligible rows used the worker:209288fallback rows,
28058cancels,411declines,74stale discards. This artificial fixed scene proves
correctness/low admission, not speed. The proof belongs to its old232B/six-arg
adapter, not subsequent revisions. Evidence: `master-native-proof/REPORT.md`.

The newer adapter isolates the master-only cursor in cached16B lines while
leaving every cross-core request/READY/ACK/payload/counter uncached. State240B,
counteroffset192, total existing spare usage1008B. Host alias-poisoning and pixel/
end/count tests pass. The reduced four-register ABI and cart-only RM wrapper
still leave the full phase probe96Bover primaryRAM (38060), so no such ROM was
produced/deployed. A counter-only LIGHT event probe was added instead of
moving established hot rendering code or relaxing a memory guard. FRT phases
will be explicitly unavailable; native/VInt/ack/cohort/worker counts remain.


## Worker target correctness and negative hardware results

The cached, four-register adapter (native05) matches all 71,680 shown pens and
343,845 full screenshot pixels in the fixed native33614 cloud scene. All 16,169
visible native sprite pixels match. Its completed bank has the same 1,024 spans
and 22 rectangles as control. The opposite bank is a matching in-progress prefix,
not a completed-pass equality claim. Only 1,819 of 211,107 eligible rows used
prepared data (0.862%). Evidence: `master-native-proof05/`.

Preserving a request for the same pose 32 rows ahead (native06) raises use to
10,362 / 211,286 rows (4.904%); 3,327 chunks were prepared. Shown pens, full
screenshot, native sprite pixels and the completed span/rectangle lists remain
exact. This is 5.697 times as many prepared rows, **not** a frame-rate gain.
The source was frozen before subsequent edits: `master-native-proof06/source/`.

Full RIG instrumentation does not fit the unchanged primary RAM cap. Counter-only
LIGHT explicitly disables FRT metrics rather than relaxing guards or moving
established hot rendering code. Its D footer retains native/VInt/ack/cohort
counts; the E footer records eight worker counters for each event. Cross-core
counter samples are individually atomic, not a simultaneous rendered cohort.

| LIGHT hardware trial (one launch each) | Entrance acks / 126 ticks | Death acks / 124 ticks |
| --- | ---: | ---: |
| KEEP, no worker (`mcbare05`) | 24 | 28 |
| Worker hooks, producer idle (`mcidle05`) | 15 | 21 |
| Cached worker, 8-row lead (`mcactive05`) | 12 | 18 |
| Guarded worker, 32-row lead (`mcactive06`) | 12 | 19 |

All retain matching native and VInt event durations. Active06 uses 927 prepared
entrance rows and 731 death rows, vs 226/187 in active05, but remains slower.
Idle hooks alone lose substantial throughput: consumer overhead must be reduced
before producer utilization is useful. These results reject promotion of every
worker variant in this table. They do not demonstrate multicore speedup.
The no-worker LIGHT control paints the same number of footer stores; branch and
instruction footprints are not asserted identical. `rig/*-event-summary.json`
and corresponding worker summaries retain all raw pages and flags.

Live sound-enabled 16,001-frame active05/idle05/active06 tests retain 15,986 VInts
and 15,992 ticks with zero timeouts and malformed records. The full 15,584-byte
MD audio blob was length-checked and byte-matched at ROM offset 0x40410 before
interpreting the MD health fields; the initial physical-PT_LOAD assumption was
rejected. Named builds now retain their MD listing and ELF automatically.
These health checks do not replace target pixel checks or hardware timing.

Current experimental changes batch four prepared rows into one consumer call
and one existing assembly draw. A separate opt-in experiment removes a second
scan used only to reproduce diagnostic pixel-store counts; affected totals will
be explicitly unavailable, never a misleading fallback-only value. Neither
experiment is enabled in zkplay01 or accepted rdplay01.


## Four-row consumer and explicit store-stat availability

BATCH07 consumes a prepared four-row block in one call to the existing assembly
copy, retains one original rectangle/punch operation, and falls back as a block
when data is unavailable. Host tests cover live span capacity, exact negative
ends, zero overwrites, band prefixes/tails, mixed fallback, vertical stepping,
palette alignment, and producer reuse only after ACK. Native33614 target proof
has zero differences across 71,680 shown pens, 343,845 full PNG pixels and 32,338
native-visible screenshot pen IDs. All 1,024 spans in both banks and 22 records,
rectangles and punched-row lists match. Prepared rows: 10,773; chunks: 3,159.
The final row-pixel/full flag is from an in-progress next pass and is not a
synchronized counter comparison. Evidence: `master-native-proof07/REPORT.md`.

Its PENID-only test build initially exceeded unchanged RAM/ROM caps. The guarded
recovery shares the unchanged calibration palette commit from resident cart and
uses the existing worker allocation for diagnostic block-policy code. Normal
non-PENID function placement is unchanged. Final allocations are 3,232/3,328 and
2,280/2,416 bytes; primary RAM ends at 0x37838, load image at 0x2337F8. A stale
listing was caught by the raw-asset check and regenerated from the immutable ELF
before emulation. All successful postlink guards and commands are saved. Failed
inspection-only link artifacts were never executed.

`R60MCSTORESTATS=0` is a separate MASTER-only experiment. It removes both extra
packet pixel rescans and marks DIAG2[10] and bk_stat[7] UINT32_MAX. The latter
cannot be used by BKBENCH in this mode. Worker work counters remain available;
only destination-store totals become unavailable. Default 1 retains exact old
counts. ED-prefixed event signatures distinguish unavailable totals; EC signatures
are unchanged. No gameplay or rendering decision consumes these totals.

Eight actual-adapter ASan/UBSan configurations (active/idle, lead 8/32, statistics
0/1) pass: 7,939 BATCH cases / 130,968 rows and 7,936 legacy cases / 150,784 rows
per configuration. Both modes preserve geometry, end positions, spans and
rectangles. Stats0 boot/export sentinels and Stats1 exact counts pass; Stats0
preprocessing contains no recount helper or calls. Independent review repeats
the matrix and confirms default vs explicit1 preprocessor identity. Both accepted
normal flag profiles remain byte-identical to source68c7327c after preprocessing,
including the PENID-only placement guards. Evidence: `chunk-review/` and
`target-audit/{stats-off-review,final2-flagoff}/`. Target08 performance is pending.


Stats-off target08 proof also passes against BOTH original KEEP and BATCH07:
zero differences over 71,680 pens and 343,845 full PNG pixels per pair, with all
native MAME sprite checks zero. Complete-bank 1,024 spans and 22 records,
rectangles and punched-row lists match; the opposite bank has 136 in-progress
spans and is not claimed complete. Both affected SD exports are 0xFFFFFFFF,
and the linked recount helper is absent. Actual consumption: 10,775 prepared
rows / 249,793 eligible rows (4.314%); no rate inference from that coverage.
Allocations remain bounded: RAM end 0x3780C, secondary 0x18C4C, load image 0x23372C,
worker 3,148/3,328 bytes and probe 2,280/2,416 bytes. `master-native-proof08/REPORT.md`.

Implementation is saved in local commit **7ab208e5**. The 08 compiled Makefile,
main, adapter, hooks, pure headers and assembly byte-match that commit; named
builds retain their original parent d537d04f plus dirty build stamp. They were
not rebuilt merely to change the stamp. Commands, binary hashes and source
snapshots: `*-command.json`, `source-freeze/{mcactive08,mcidle08,mcnative08}/`,
`08-source-commit-identity.json`. Neither experiment is in the normal candidate.


Hardware BATCH07 completes 16 entrance / 22 death acknowledgments; stats-off08
completes 16 / 20 (one launch each, 126/124 native and VInt durations, all retained
pages stable). Corresponding rendered samples are 15/21 and 16/21. Hardware
prepared rows are 895/704 and 871/616. Both are below the matched no-worker LIGHT
control's 24/28. Removing the optional recount does not establish a throughput
improvement or rescue promotion. Both remain withheld; do not substitute their
higher ares acknowledgment counts as hardware evidence. rdplay01 restored after
each, latest launch log03:28:48. `rig/mcactive{07,08}-a-{event,worker}-summary.json`.

Both08 live health runs complete16,001 ares frames with no span timeouts or
malformed MD records. All14 D/E captures per ROM match retained SD; signatures
EDE7/EDEB identify unavailable FRT/store totals while preserving available worker
counters. Named MD ELF text/data reconstruct the exact15,584-byte ROM payload at
0x40410 before decoding malformed-record fields. `event-live-health/validation-*-08.json`.

A final bounded producer experiment batches up to eight existing four-word
steps per pump. It checks native-tick and lease state between steps, yields to
pending staged tile work, and exports decoded-word deltas once per pump. This
addresses wrapper/ownership/counter traffic instead of merely claiming that
more CPU activity helps. No ready publication, new job or extra RAM is assumed;
measured net hardware throughput remains the acceptance criterion.


## Producer batch activity and final lead grid

Q8's native09 scene remains exact against original KEEP and08: 0/71,680 pens,
0/343,845 full PNG pixels, zero native oracle errors and an exact completed
1,024-span bank / 22 rectangles. Both unavailable exports remain 0xFFFFFFFF.
Target bounds remain enforced: worker3,288/3,328 bytes, probe2,280/2,416,
primary RAM0x3780C and load0x23372C. Normal flag-off input preprocessing still
matches68c7327c. Code: ad7e4e5c; frozen09 inputs match that commit byte-for-byte.

The default/recompiler native run proves batching actually executes: 79/82
observed consecutive pump deltas exceed four source words;33 reach32 words,
which requires all eight bounded quanta. An independent interpreter PC profile
records37,559 continuation backedges and2,736 eight-step-limit exhaustions in
its whole3,000-frame prefix. Those profile counts are not isolated to the cloud.
Both runs finish with the expected native sprite table. This proves activity,
not speed. `q8-activity/REPORT.md` records the corrected physical watch address
and native-file length comparison, raw evidence and reconciliation.

Hardware09 gives16 entrance /19 death acknowledgments (rendered16/20), still
below bare LIGHT24/28. Native intervals are126/124, but its retained VInt
intervals are126/122, so clock-window equality is NOT claimed. It remains
withheld. The full-sound16k health run has zero malformed records/timeouts and
all28 footer captures match retained state. `master-native-proof09/`,
`event-live-health/validation-worker-health-09.json`, `rig/mcactive09-a-*`.

Policy inspection finds forced interior holes with a32-row lead: after rows0/4,
the two slots wait for32/36, so rows8–31 must fall back even with an instantaneous
producer. This is not a universal25% whole-pose ceiling: tail cancellation and
short sprites differ. Lead8 removes those holes but offers only one intervening
four-row block before use; lead16 offers three. The final bounded grid measures
8/16/32 with Q8, rather than assuming any prepared-row count means faster frames.

| Lead | Native fixture prepared rows | Fallback rows | Source words | Pixel/native/complete metadata checks |
| --- | ---: | ---: | ---: | --- |
| 8 (native10) | 1,833 | 246,242 | 774,144 | exact |
| 16 (native11) | 7,634 | 241,355 | 725,533 | exact |
| 32 (native09) | 10,732 | 238,879 | 621,868 | exact |

Each matches both original KEEP and09 for shown pens, full screenshot and native
sprite checks, plus a complete1,024-span bank and22 rectangles/records. Partial
136-span banks are explicitly excluded from completed-bank claims. All state
markers and allocation guards pass. Focused lead16 active/idle Q8 stats0 sanitizer
cases pass; the existing16-configuration matrix already covers lead8/32. Generic
metadata encodes lead, quantum cap and unavailable statistics independently.
10/11 each pass16k sound-enabled health, allD/Ecaptures, exact namedMDblob and
zero malformed/timeouts. `lead-policy-audit/NATIVE-GRID-REPORT.md`, per-build
native reports and `event-live-health/validation-worker-health-10-11.json`.

Grid code is4ad7524b. All four compiled input sets byte-match that commit;
named images retain their parent ad7e4e5c plus dirty stamp and immutable hashes.
The final normal-profile proof is `target-audit/final4-flagoff/proof.json`.
Hardware grid results are recorded below when complete.

## Dense boot QA: promotion remains held

Every frame430–501 was captured from both frozen normal ROMs. Control is wholly
black443–468 (26 frames); KEEP candidate443–469 (27). At469 both have completed
tick5, native clock458, tick calls459 and display-enabled state0xA0, but only
control shows the scene. Candidate draws470. This is an actual one-frame extra
black presentation in this test, not established whole-transition phase parity.
`acceptance/boot-transition-dense/REPORT.md` preserves all144 images and clocks.

A separate501-frame trace localizes divergence much earlier: first SH flip at
frame9/MDline19 applies immediately for control but waits until line224 for
candidate. First68K completion, GO, DONE and publication occur one frame later;
MD hold release and the later scene delivery retain that shift. No hz!=0 sprite
was observed before the terminator in any sampled first501-frame sprite table;
that does not rule out between-sample transients. The precise32Xvblank/mode
predicate was not logged, so its underlying cause remains unresolved. This is
not evidence of incorrect lookup math and is not grounds to dismiss the extra
black frame. `acceptance/boot-firstpacket-trace/DIAGNOSIS.md`. No arbitrary boot
delay or unrelated display rewrite was introduced. zkplay01 remains unpromoted.


## Actual normal-image closure

Both final source configurations were rebuilt, without any worker or diagnostic
flags. `offrd01` versus accepted `rdplay01`: all6,815,744 bytes compared,14 changed
bytes confined to the final64-byte build stamp. `offzk01` versus `zkplay01`:11
changed bytes, likewise confined to that stamp. **Zero code/data differences
outside the stamp in either pair.** This checks actual generated ROM/linker
output in addition to preprocessor identity; no extra runtime soak was needed
for a byte-identical image. Original ROMs were not overwritten. Commands, hashes
and exact outside-stamp comparison counts: `normal-image-identity.json` and
`{offrd01,offzk01}-command.json`.


## Final hardware closure: all small-job workers withheld

| LIGHT configuration | Entrance acks | Death acks | Recorded VInts entrance/death |
| --- | ---: | ---: | --- |
| mcbare05 — KEEP without worker | 24 | 28 | 126/124 |
| mcidle05 — legacy idle hooks | 15 | 21 | 126/124 |
| mcactive05 — legacy eight-row lead | 12 | 18 | 126/124 |
| mcactive06 — guarded32-row lead | 12 | 19 | 126/124 |
| mcactive07 — four-row consumer | 16 | 22 | 126/124 |
| mcactive08 — store statistics unavailable | 16 | 20 | 126/124 |
| mcactive09 — Q8 lead32 | 16 | 19 | 126/122 |
| mcactive10 — Q8 lead8 | 16 | 20 | 126/123 |
| mcactive11 — Q8 lead16 | 16 | 21 | 126/124 |

Each row is one separately launched trial with complete retained five-page event
and four-page worker bundles. Native intervals are126/124 in every row, with
explicit partial/gap/uncertainty flags. **Recorded VInt windows differ:** death
is122 in09 and123 in10, versus124 in the other arms. The final audit caught this;
do not force equal clocks or diagnose missed interrupts without further evidence.
These measurements reject promotion of every small-job worker tested. The Q8
producer is demonstrably active; neither more parallel activity, removed
statistics nor the8/16/32 policy grid yields a net hardware win. These single
LIGHT trials are not pooled with the earlier three-launch full-FRT KEEP test,
whose native AND VInt126/124 windows were individually rechecked in all six
bundles. No CPU-utilization percentage or unique-image FPS is inferred.
`rig/final-small-job-grid.json` retains exact counts and bundle references.

The code, host tests and measured negative results remain reusable; all worker
options stay OFF in normal builds. The next structural decision must address
larger jobs or pre-baked working sets with explicit cache/mapper costs. It must
not assume this two-slot/four-row adapter becomes fast merely by enabling both
CPUs. Remaining smoke performance and candidate boot-phase issues are open.
Accepted rdplay01 was restored after the final probe, launch log04:03:13.
Fresh remote SHA256 and actual display verification are in `rig/restore-final/`.

Legacy footer compatibility: lookahead is reported unknown when the guarded-
future bit0x40 is absent. In particular ECA5 does not falsely become a32-row
policy merely because later EC-family builds use that default. Its known Q1
cap is retained. Targeted decoder tests cover this historical uncertainty;
no ROM or runtime code changed for that correction.
