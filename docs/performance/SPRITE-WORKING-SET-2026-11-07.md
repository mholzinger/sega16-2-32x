# Pre-baked sprite working set

**Current normal06 remains under review.** Both65,001/125,001 health gates,
menus/controllers, assets, native smoke pixels and the natural route pass.
Hardware06 recovers in both180second strips; the original04 failed. A temporal foreground-view defect is proven against actual scanout
history, so promotion stays held. Accepted rdplay01 remains the restore baseline.

## Display-release follow-up

Commit9db88f45 removes three independent caches of MD VDP register1. Every
consumed packet now establishes its requested state. Old actual source proves
all six cross-path stale-release failures; sanitizer tests cover those cases,
external/menu writes and196,608headers. Actual linked MD and MMIO traces prove
all three fixed paths execute. This is a proven source defect, not proof that
it alone caused the hardware blackout.

Normal04's two hardware strips end in repeated all-black samples. Both normal06
strips recover after isolated black captures; matched old-cache05 also recovers.
Causal wshold01 preserves exact04 SH source with only fixed06 MD and qualified
relocations; it also recovers in its180second strip. Thus recovery is observed,
but timing remains involved. Every run restores accepted rdplay01. Fresh
untouched12,001-frame06/04/control/hold01 headless runs recover after bounded
26/26/25/25blackframes and show no transport deadlock.

Normal06's actual MD payload is22,352bytes and matches ROM0x40410. All graphics,
audio and raw-game assets remain exact; changed game executable words are
strictly old/new addresses of the same ELF symbols. Native09 has zero shown pen,
full-PNG/native sprite errors and complete1,024span/22record metadata equality
against the fixed KEEP/native04 scene. Both65k/125k gates have zero timeouts,
malformed records, dropped ticks, missed indirects, FIFO full attempts or budget
yields. At125k there are69page switches and5,008accepted/posted sounds, empty
FIFO/highwater3/pending0. Natural wsroute04 reaches level2 at control frame36405,
score33800/lives3; all440bytes/18fields match across33,328fully copied keys and
all138coherent ordered boss/level states match. Raw/unqualified differences are
retained. Original20 completes with18ordinary exact rows, explicitH7alternate exact,
H13unreached,2legacy picture and19legacy staging flags. No20/20 claim.
The ACK-before-FS privatePOC changes transport order but still fails86qualified
FG pixels; its two greenpixels are pose-confounded. It is not promoted.

Working-set inputs are durable descriptors under `games/altbeast/graphics/`.
The generic empty-base baker reproduces the62,528byte45pose atlas from raw art;
15tests pass. The separate block compositor/protocol and optional code placement
are prototypes, not part of normal06. See SPRITE-BLOCKS-2026-11-07.md.

## Updated hardware trials with the MD fix

Three new matched MODE2 controls/candidates use identical updated MD programs.
Entrance acknowledgments are25/24/25 versus31/32/32 over126native ticks and
126VInts per trial:28.38% higher. Death acknowledgments are28/29/28 versus
35/33/32 over124/124:17.65% higher. Every restore log selects accepted rdplay01.

These are observed bank acknowledgments in selected cloud windows, not unique
images, FPS or CPU utilization. All head-partial/tag-gap/source-uncertain flags
remain recorded. MODE2 excludes mapper entry and normal-page restoration. Older
trials remain separate. Named updated15,610byte MD blobs match across both arms;
art/source bounds are qualified in build/codex-working-set/cloud-updated-summary/.

## Layout and correctness

`R60WORKSET=1` builds one exactly 512 KiB page at physical ROM offset 0x380000.
It contains the unchanged music payload, a bounded AB1 unscaled index and local
packets, the existing 42-pose raw ZB1 scaled atlas, and a 64-byte erased guard.
The generic baker accepts explicit decoded-art inputs and pose manifests. The
Altered Beast adapter supplies only the native phases that select this page;
normal phases return to the ordinary per-level page. Missing keys use the raw
renderer. No additional arena or enlarged cache is allocated.

Sixteen baker tests cover deterministic output, exact budgets, corrupt packets,
overlap and input/output aliasing. Actual synthetic SH links cover exact size,
under/overflow, code overlap, neighboring art and disabled-image identity.

Frozen `wsnative01` executes actual compiled target code using the MAME frame 33614 fixture (native tick 0x7FA6).
Against the existing native/KEEP proof it has zero differing pixels among
71,680 framebuffer pens and 343,845 full-screen PNG pixels. The completed bank's
1,024 spans and 22 punch rectangles also match. The page is actually selected:
3,569 scaled copies and 138,150 copied rows are observed. This is one exact
scene proof, not whole-game pixel or cadence parity.

Normal `wsplay01` preserves the three global bake windows, pages 8 through 12,
and music bytes. Its 65,001-frame headless ares health run has 64,989 VInts,
64,992 ticks, 55,155 flip publications, 1,082 healthy samples, zero span timeouts
and zero malformed tile records. Its named 22,322-byte MD payload matches ROM.

The natural 40,001-frame control/working-set pair reaches level two in both
arms at physical frame 36,405 with matching score, lives and ordered boss-state
sequence. Across 33,347 matched clocks with a completed mirror copy, all 18
decoded fields and all 440 mirrored bytes match. Unqualified samples differ
and remain in the report; final native clocks differ by one tick. Neither result
establishes whole-game pixel equality or displayed cadence.

## First hardware trial: scheduling blocks the gain

Mode-one LIGHT probes include the full entrance/death intervals and a fixed
eight-native-tick postwindow. Both postwindows confirm restoration to a normal
page. Counts below are applied bank acknowledgments, not distinct images/FPS.

| Frozen ROM, one launch each | Entrance, 377 native ticks/VInts | Death, 252 native ticks/VInts |
| --- | ---: | ---: |
| `wscontrol01`, BOOT + KEEP | 173 | 105 |
| `wsevent01`, plus working set and its counters | 141 | 105 |

The working-set arm withholds 49 entrance VInts and 3 death VInts while waiting
to reserve the shared mapper/sound mailbox. This experiment is withheld.

Actual COMM10 traces identify two scheduling faults. Before posting a mapper
request, the prototype blocks snapshots whenever sound occupies the mailbox,
even though the current page can safely render through raw fallback. After a
mapper request, a newly posted sound command can hide an already completed ACK.
In the natural route the MD acknowledges the first map in the same frame, but
page identity publication occurs 50 frames later, after 48 newer sound posts.
This is bookkeeping latency, not a slow physical mapper write.

The isolated `wsevent02` changes only those mailbox rules and retains the same
pose manifest. Original 10,700 and new 42,084 actual-helper interleaving schedules
pass, with no sound consumption/reordering. Frozen target assembly proves RAM
map posting and ACK-gated cart helpers. Fresh `wsnative03` retains exact shown
pens, PNG, native sprite checks and the complete 1,024-span/22-rectangle bank.

However, strict post8 validation **rejects both ares and hardware event02
entrance totals**: the normal page is not restored by the eighth post tick.
Hardware entrance event-only stalls fall from 49 to 2, with 174 event acks;
death has 100 event acks and a valid 8-VInt/6-ack postwindow. Those partial
observations do not establish an inclusive speed win. All raw words and the
rejection are retained. A targeted trace proves the page8 request is ACKed at physical frame6606.
At native exit+8 the post checker still observes page7/pending8, then that same
tick's later page service commits page8. No native tick is skipped. The next
isolated change services the mapper before the native tick, with one call per
loop and its hold decision retained until snapshot publication. The recovery
gate and pose manifest remain unchanged.

Normal `wsplay02` builds and preserves the original global/level/music bytes;
its health run and the natural `wsroute02` run are in progress. Runtime checkpoint
6468af13 and baker checkpoint92fbc5a1 are local, default off. A separate Make
configuration guard rejects WORKSET+EVENTMETER without WORKSETSTATS; normal
stats-off play remains supported.

`wsevent03` moves the sole service before native ticks. Actual frozen assembly
confirms one call, the saved hold decision, and a tick-drain backedge that excludes
the pager. Headless strict health passes both eight-tick postwindows, matching
all footer/RAM words, the named MD payload and zero malformed/timeouts. Its
228/377 and127/252 ares acknowledgments are not hardware speed evidence.

**Hardware03 still rejects entrance post8 restoration.** Raw event-only acks
are173/99 with1/3 busy withholds; death post8 is valid8 VInts/7acks. The accepted
ROM is restored. Shared mailbox arbitration needs a bounded opportunity for the
mapper before another sound command can take a newly free slot. Neither the
recovery deadline nor the failed historical result is discarded.

The three unchanged hardware control launches have inclusive entrance
173/171/179 and death105/101/103 acks, all377/252 native clocks and VInts valid.
Candidate promotion must account for this variation.

`wsplay02` completes65,001-frame health:64,989 VInts,64,992 ticks,55,237
publications,1,082 healthy samples,zero malformed/timeouts; all22,322 named MD
bytes match. Natural `wsroute02` reaches level two at the same frame36405 and
score/lives as control. Across33,337 fully copied matched clocks, all18 decoded
fields and440 mirror bytes match; all health/pending-state gates pass. This is
state coverage, not pixel/cadence parity.

A separate read-only weighted audit finds three missing cloud packets
(key031401D0, zoom198/132/66) need13,388 bytes and fit without removing existing
art. All45 candidate packets decode exactly against raw ROM, and the previous42
bodies are byte-identical. These three account for89.0% of missing drawable
source-word weight in the frozen01 descriptor samples; that is sampled demand,
not actual redraw time or a speed prediction. Existing clipped packets account
for25.76% of sampled source-word weight and remain a larger structural question.
Generic `tools/extend_zoom_atlas.py` is committed at122d2e1c;9 new tests pass,
plus8,192 scaled cases and16 existing working-set tests. It preserves every base
packet and rejects malformed data, aliasing and overflow rather than dropping
poses. The admission cache is separately committed atda412421, default OFF;
cache-OFF WORKSET preprocessing and all five private code sections match frozen03
exactly. Active manifests in the causal scheduling builds remain unchanged.

Central arbitration checkpoint4360f8c6 (default OFF with WORKSET) separates native
sound capture from transport posting. A checked32-command FIFO and policy use88B
of the existing spare control area at3EB00, plus a4B idle-map latch at3EB60.
No new arena/BSS is allocated; actual BSS shrinks16B. Ordinary native emission
backs off while an unaccepted MCU latch remains; explicit native RESET keeps its
original cancellation semantics. Accepted FIFO commands retain order. Playback
continues independently. Only the RAM arbiter posts5A/5B; audio flush is limited
to one post per completed native tick, never idle polling. Mapping has bounded
priority/headroom and fair audio grants. Wait counters deduplicate per VInt.

A million-operation sanitizer test preserves accepted command order through
wrap/full/headroom/fairness cases. Actual generated native guard and extracted
integration tests cover retained latches, speech-once, RESET and frontend due0.
Default-off main/port-driver preprocessing matches accepted originals. Named05
ELF/ROM audit proves all11 audited functions, RAM-only mapper return, audio pop
after posting and scheduler ordering. Remaining primary/RAM2/worker margins are
472/80/1900B; no fence was enlarged. `wsevent05` uses the original42 atlas and22
unscaled manifest; fresh headless/hardware validation is in progress.

`wsevent06` adds only the three45-atlas poses. Thirty-five frozen source files,
compiler flags, named MD ELF and SH symbol maps match05. Executable sections
match except the2-byte atlas-bound literal; outside page7 only that literal and
build timestamp differ. Its headless run is in progress. These are isolated
experiments, not promoted play ROMs or established hardware speed gains.

## Current right-clipped copier

Checkpointb090ad1b adds a separate default-off right-clipped packet copier.
The existing fully onscreen/unscaled paths still call the old assembly directly;
left clips keep raw fallback. Independent host tests cover85,744cases/211,120rows,
including full backing pixels, exact endpoints/spans/npx and execution of the
actual main box/rectangle publication tail. Existing packet metadata suffices.

Frozen `wsnative04` executes2,588 real clipped calls and124,514 rows. Against
original KEEP and native03 it has zero shown-pen/full-PNG/native errors and exact
completed1,024-span/22-record metadata. All assets are preserved; the quiet ROM's
13 changed page8 bytes are solely its recognized final build stamp. Target code
and load mappings, kernel1,088B and all original fences pass.

Full-event hardware observations, inclusive of unchanged post8 restoration:

| ROM | Entrance acks /377 clocks | Death acks /252 clocks |
| --- | ---: | ---: |
| control a/b/c |173 /171 /179|105 /101 /103|
| arbiter05 a |180|107|
| atlas06 a/b |179 /181|110 /111|
| rightclip07 a |182|115|

All listed inclusive native/VInt totals match. The atlas06 first death splits
its252 VInts as246 event +6 post, versus244+8 in other arms; this is retained,
not flattened into per-tick cadence equality. Samples remain small. No unique
frame or whole-game FPS claim follows. Cloud-only mode2 is measured separately
(control first25/28acks over126/124 native+VInts); no entry/restoration accounting
or inclusive total is attached to mode2.

Normal `wsplay04` retains live frontend and has no event, fixture, forced form,
GOD, tape or admission-cache flags. All five menu level starts,3/6button reentry,
tile residency/restoration, unchanged asset regions and full22,322B MD match pass.
Remaining primary/RAM2/probe margins80/1164/1328B stay within existing fences.
Long health, original object and temporal gates are running; promotion is held.
A separate RAM-kernel placement experiment is not included in this normal ROM.

## Boot initialization

The separate optional `R60BOOTBANKS=1` replaces a timing-dependent first FS
write with mode-off, a full HBlank boundary and explicit initialization of both
physical framebuffer banks. A private emulator probe reproduces the original
race: the mode latch changes between the original two ROMs' first mode and FS
writes. Poisoned target execution proves correct line tables, active pixels,
transport tails and selected CRAM entries, without touching the adjacent guard.

Both new BOOT arms show 27 active black frames in the dense boot interval; the
old accepted arm shows 26. Thus the new pair is consistent, but the previous
boot cadence is not restored. A remaining 44-pixel row difference is a live
palette-flash boundary: framebuffer geometry in both banks is identical. No
palette fix or full attract cadence equality is claimed. The BOOT+KEEP arm
passes 65k/125k health and all five menu starts plus repeated controller/menu
tests. Fresh headless MAME attract capture supplies an independent reference.

## Evidence and next decision

Evidence roots: `build/codex-working-set/` (frozen images, sources, commands,
hashes, target and hardware dumps); `build/codex-boot-banks/` (poison, dense boot,
palette, long health and fresh MAME); and
`build/codex-clip-smoke/boot-flip-probe/` (original race reproduction).

Accepted `rdplay01` remains the restore target after every hardware probe.
The working-set and boot experiments are not promoted by these scoped checks.
After the mailbox fix, measure repeated hardware launches including mapper
entry/restoration costs, then decide whether the page is worth retaining.
