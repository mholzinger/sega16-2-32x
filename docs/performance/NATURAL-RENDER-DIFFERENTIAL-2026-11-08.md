# Natural renderer differential — 2026-11-08

The generic block renderer reproduces eight complete captured normal-play
passes on HOST: every71,684 pre-text framebuffer byte and31 metadata objects
plus rectangle counts. An independent scalar replay and peer review agree.
The third private SH-2 fixture series now matches all eight raw outputs and
metadata, with final independent ownership/stack qualification7382032b. There is still no natural-game caller,
full MD presentation proof or performance claim from this differential.

## Current captured-input and SH-2 status

Observer04 qualifies complete GO1378..1385 passes:64 ROM-backed full-height
DRAW records,64 row copies/PUNCH calls,1618 actual foreground words and3819
mask-byte uses. Cold OFF/ON and qualified02 OFF match all23 guest artifacts;
four named build identities also match. Input runtimefreeze271a01e2 and peer
d48b6cc5 are separate from HOST replayfreezef96a7705/peer6328a1ba. Missing
relevant FG inputs decline; mutations of irrelevant cells, packet validation,
initial spans and final backing bytes are tested. No natural KEEP/PREP/RAW/
REPUNCH/rightclip coverage is inferred from these eight DRAW passes.

Private wsNR fixtures run the unchanged generic core on the SH-2 with parked
native producers, pinned page, checked initial memory, real typed metadata
and existing prep storage. They replace page7 art explicitly, so they are not
playable normal-ROM equivalents. The real SDK build is6224/8192 bytes; only
two private API headers enter its include path. Compiler dependency archives,
linked code relocations, original tile data and final ROM page membership are
checked. Host SDK stubs are excluded.

All eight first-series raw runs record emulator exit0 and exact full memory
extents/custody (raw-only independent receipt4ec9ac7e). Case0 raw data matches
all71,684 bytes and31 actual named metadata objects plus rectangle counts;
the bank identity is derived independently from model source and FS trace.
Final semantic ownership/stack receipt remains separate. Cases1..7 expose a
FIXTURE initializer defect: zero byte writes to normal framebuffer are ignored
by the actual model bus, so changed nonzero pixels cannot be cleared. The
readback gate fails closed before rendering. Original ROMs and failures remain
unchanged. The second series uses normal aligned16-bit read/modify/write with full
word readback: seven cases execute and match, while case2 refuses before core
entry because its single late framebuffer-ready sample is busy. Every initial
readback passes. That refusal is preserved. A third series waits for the first
clear FEN sample with a fixed65,536-read budget, retaining failure on persistent
busy state. All eight third-series raw runs recordexit0/GO1/DONE1, match every
71,684 backing byte and all31 actual typed ELF objects (20,528bytes) plus both
rectangle counts. Root preliminary reports compare finalFS/model-derived bank0;
independent actual ownership/stack qualification now passes (owner30169e1c,
peer7382032b). Observed replay stack488B and bootstrap1536B stay below2KiB;
static conservative replay chain824B includes IRQ allowance. No old verdict
is upgraded, and this remains isolated untimed backend replay, not a speed gain.

The first case0 wrapper failed in postprocessing after guest dumps because its
hash helper received a string. That original failure and exit-code uncertainty
remain preserved. The distinct serial collection records each guest exit before
postprocessing and independently rechecks all inputs. Raw collection is not a
semantic pass or a hardware speed result.

## Why capture the existing renderer

The current PUNCH path reads foreground tile words separately for each sprite.
The master can change them concurrently. A later memory snapshot can therefore
miss a value the renderer actually used. Two overlapping sprites can observe
different foreground states within one pass, so replacing those reads with one
frozen grid is not automatically equivalent.

The observer uses the existing emulator post-instruction hook to record the
actual returned operand. It reads model memory directly, without guest bus
accesses, writes, synchronization or game patches. Sites, opcodes, ROM, tool,
private core archive and source inputs are explicitly bound and reviewed.

## Earlier observer02 result (preserved)

Two cold 2,401-frame runs use the same normal `wsplay09` ROM and existing
level-one controller tape. Observation covers frames 1600–2200. Observer OFF
and ON match all 23 guest artifacts exactly: complete model memory dumps,
command/flip/watch traces, instruction profile and 15 screenshots. The four
named ROM/build artifacts also match. Full model DRAM coverage is `0x80000`
bytes, including both guest-addressable banks.

The unchanged strict parser and independent review confirm:

- Eight selected, completed PUNCH-observed passes.
- 61 PUNCH calls and 1,508 actual returned foreground words.
- 461 mask bindings and no observed eligible-identity conflicts.
- Complete stream, bounded counts and unique DONE links to the raw guest trace.

The first selected GO was claimed at frame 1599, before the observation ROI.
These are completed PUNCH-observed sequences, not proof that every selected
pass was captured from its beginning. These counts do not cover every record,
span repunch or actual row-mask byte use. They do not prove pixel equivalence, stable inputs in unobserved passes,
arcade/MD presentation, scanout cadence, hardware performance or game-wide code
coverage.

## Preserved failure and correction

Observer01 rejected its first runtime stream before recording events. Its guard
expected `0x20000` VDP words, while the actual model allocates `0x40000`. The
host stub had repeated that wrong assumption. The failed source, binary, plan,
stream and outputs remain unchanged and unqualified. Its guest-output equality
covers both active banks but only half the total model allocation.

Observer02 corrects only that guard. Its new tests instantiate the archived
actual allocator and source allocation expressions, verify word/byte units and
bank offsets, and deliberately reject the old size. Fresh tool custody and
fresh OFF/ON runs qualify version02; they do not rescue version01.

## Next component

Keep the existing target renderer authoritative. Capture its actual post-clear
initial backing, supported effective records and immutable packet bytes, the
foreground masks actually needed, and pre-text/completed backing plus structural
metadata. Replay the generic block core offline from those same inputs.
Conflicting, unobserved or unsupported inputs explicitly decline the pass.
Compare all backing bytes, spans and ordered metadata; never compare different
poses or silently disable KEEP. RGB/MD presentation remains a distinct scope.

Evidence is under `build/codex-sprite-block-audit/natural-oldpath-differential01/`:

- `PLAN.md`
- `observer01/failed-run-extent-rca/REPORT.json`
- `observer02/TOOL-REVIEW.json`
- `observer02/neutrality01/NEUTRALITY.json`
- `observer02/neutrality01/INDEPENDENT-REVIEW.json`
