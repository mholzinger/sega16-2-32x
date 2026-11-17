# Row-chunk preparation: preserve the accepted cache first

Status (2026-11-07): decoder, mailbox and optional two-buffer target adapter
are implemented and reviewed. Host tests, actual target pixels/spans and live
sound-enabled health pass. Hardware trials of per-row consumption, four-row
consumption, optional statistic removal and batched production remain slower
than the existing renderer. The final 8/16/32 lead comparison is recorded in
`SMOKE-EVENTS-2026-11-07.md`. **The worker stays off and is withheld.**

The implementation preserves the accepted 4KiB SPRPREP cache. Two 384-byte,
four-row payloads plus 240-byte target state use 1008 bytes of the existing spare
1KiB. Shared controls, payloads and counters remain uncached; the master-private
decoder occupies isolated cached lines. All original memory fences remain.

This rejects the tested small-job adapter, not all dual-CPU designs. Larger jobs
require an explicit cache-capacity trade or different ownership; that trade must
be measured. A separate pre-baked working-set page could avoid permanent global
art eviction, but requires mapper quiescence and a measured phase cache budget.
Neither alternative is implemented or claimed faster. Do not repeat a global
atlas experiment as if its previous storage tax and negative live results were
unknown. Component budgets and census below are design inputs, not speed claims.

## Measured capacity/eligibility census

Inputs: the105 saved active first-Neff smoke snapshots in
build/codex-smoke-health/arcade-smoke-clear whose boss status is active and
handler is16d7e/1794c/17b54/17b88. Count only visible records with hz!=0.
These contain1381 records,74383 rows and1326861 full-row source words.
Decode follows committed tools/s16art.py. Eligibility requires full chunk end
width<=128pixels, x>=0, every chunk row END fits320, and its packet fits the
payload. These are capacity/coverage counts, NOT prepared-hit or speed data.

| Sample | 4-row/384B eligible chunks | Eligible full-row words | 16-row/2048B eligible chunks | Eligible full-row words |
| --- | ---: | ---: | ---: | ---: |
| Native7544 reference |160/208|10913/14457 (75.5%)|41/57|10270/14457 (71.0%)|
| Materialization30677 |186/226|11918/14860|49/63|11066/14860|
| Cloud33614 |136/227|8788/14943|35/63|8445/14943|
| Saved105-smoke corpus |13295/19103|908591/1326861 (68.5%)|3478/5233|864899/1326861 (65.2%)|

Maximum actual payload is320bytes for four rows and1236bytes for sixteen.
There are ZERO size/width declines in this corpus; all ineligible chunks are
clipped. Smaller chunks admit narrow rows beside clipped rows that would
exclude the whole16-row block. Corpus-average four-row chunks use approximately
3.8times as many handoffs, so eligibility does not establish a performance win.
The earlier one-pose mailbox cannot cycle until a complete pose is consumed;
its largest eligible single7544 job represents only1178/14457words (8.1%).

Important general bound:128pixels x16rows is2048 PIXEL bytes plus64 row-header
bytes =2112bytes, before any frame header. It is NOT an unconditional2KiB fit.
Both variants require actual packet capacity checks and deterministic fallback.

Evidence: ROW-CHUNK-CENSUS.json and ROW-CHUNK-VARIANTS.json in this directory.

## Smaller storage and ownership change

Normal accepted SPRPREP map:

| Range, end excluded | Four-row prototype |
| --- | --- |
|3C400..3D800|Accepted512-entry tile-priority cache, untouched|
|3D800..3E800|Accepted4KiB slave-only SPRPREP packet/cache, untouched|
|3E800..3E980|384-byte chunk slot0, master writes/slave reads|
|3E980..3EB00|384-byte chunk slot1, master writes/slave reads|
|3EB00..3EC00|256-byte control/request/cursor/counter budget|

Each slot needs at most32bytes published request metadata (version, copied
16-byte descriptor, target snapshot/record, row start/count and source epoch),
16bytes READY metadata, and4bytes ACK. Two slots consume104control bytes;
a96-byte resumable producer cursor plus32bytes counters leaves24bytes slack.
Require a compile-time sizeof<=256 guard; do not silently enlarge the arena.
No BSS or atlas allocation, priority-capacity change, old-cache loss or stack
borrow. Existing BAKEARTCHECK row buffers overlap this spare region: explicitly
reject that combination. MASKBLOCK/ARTCACHE/MBAND are already incompatible
owners; do not relax those guards. Pixel proofs use host tests and native
shown-bank comparisons rather than overlapping old check buffers.

The alternative sixteen-row plan reuses3D800..3E800 as two2048-byte payloads
and puts metadata/cursor into the spare1KiB. It MUST disable the existing
slave SPRPREP writer. That loses accepted resident reuse and needs an extra
capacity-matched/cache-policy control. It buys fewer notifications and copy
calls, not more nominal eligible source work in the sampled corpus.

## Exact packet and vertical contracts

Packets contain whole output rows; never split a row between slots. Each row
uses existing raw-pen groups: first4 byte, ngroups byte, signed16bit negative
exact kept-pixel END, then ngroups big-endian32bit groups. Trim only complete
leading/trailing all-zero groups. Preserve interior zeros and literal-zero
padding to4bytes. Pens0/15 are zero; END still counts every kept source pixel,
including transparent pixels and the terminating source word's accepted pens.
The four-bit keep phase restarts at zero for EVERY source row; source-word
EOL is only the LAST traversed nibble15. Color with CURRENT slave bank/base4
when emitting. Master never reads or mutates palette mappings/framebuffer.

The existing negative-extent consumer in bkcopy.s:80..88 and132ff must be
selected through an independent extent-format flag, not by enabling
R60ZOOMBAKE and its global atlas reservation. First scope accepts only chunks
whose maximum exact row END stays on screen. This avoids unresolved clipped
source-word overshoot; every unsupported chunk uses the old scaler.

Vertical source state is the CURRENT runtime RF_STEP contract in
r60_main.c:3907..3912: add signed pitch BEFORE each output row, add vz to vacc,
and on carry add pitch again and mask vacc to31. First output row is stepped.
For a chunk starting at relative row r, master starts from the original
address/vacc0 and performs exactly r skipped RF_STEP operations, then steps
before each decoded row. A continued producer can retain that cursor.
Do not reset vertical accumulation at chunk boundaries. Do not add a16bit
address wrap absent from the current runtime's unsigned addr/o arithmetic.

On the slave, even a prepared n-row block advances its original addr/vacc via
n ordinary RF_STEP operations. Four/sixteen cheap steps avoid formula changes
and keep the following scalar fallback exact. No dependency on producer cursor
state is required. The y position advances by n output rows, not source rows.

Consumer integration belongs INSIDE the existing hz/ROWFAST sprite context,
not by calling r60_bk_sprite on shortened synthetic sprites. That whole-record
adapter would reset boxes and append multiple rectangle entries. For a READY
block, call bare r60_bk_rows for its nrows using the current spw/sp_end/sp_hdr,
current y/x0/base4, then carry returned spw, span-full, pixel count and xmax
into the same context. For a miss, run the original row loop over the same
four/sixteen output rows and RF_SPAN each result. Append ONE final rectangle,
update ONE aggregate last_box, and punch at the original whole-sprite point.
This retains sprite order and old cleanup/foreground interaction. The row-end,
span, rect and rowpix contracts are as important as framebuffer equality.

## Requests, READY/ACK, cancellation and source lifetime

Slave is the sole request/ACK writer; master is the sole packet/READY writer.
All shared controls AND payloads use uncached260xxxxx aliases. Existing cached
0603D800 cache stays private to the slave and is not used for worker output.

Each request has an even32bit version; slave marks it odd while changing its
captured descriptor/row target, compiler barrier, then publishes new even
version. Master copies only an even request and rereads its version afterward.
Each packet is BUSY/private while being filled. After all bytes and metadata,
master barriers then publishes READY=the captured request version. Slave uses
it only when version, snapshot, record, source identity/epoch and row range all
match its current context. Master never overwrites a published payload until
ACK equals that READY version. Slave writes ACK after the assembly reader
returns, or explicitly ACKs a stale packet WITHOUT reading it. No lock/TAS or
cross-CPU read-modify-write is needed.

At pass acceptance, identify the first eligible scaled record from the immutable
snapshot and request its first two blocks. The slave can clear/punch earlier
work while the master prepares. At each block boundary, consume READY or fall
back immediately; release the consumed/stale slot, and request the next block
roughly eight output rows ahead for four-row slots. Near a record's end,
request the next eligible record's first blocks before the existing full-sprite
punch, providing lead time without changing draw order.

A missed deadline cancels that request version; its rows are drawn normally.
Producer checks cancellation/pp_due after a small fixed source-word quantum
(e.g.four), abandoning obsolete partial packets without publishing. A race that
publishes an old version after cancellation produces a stale READY, never a
valid pixel source; the consumer ACKs it at the next boundary. That can waste
work/one slot opportunity, but cannot stall gameplay or overwrite live data.
At pass end discard/ACK outstanding old-snapshot packets and advance request
versions. The producer never waits for the slave, nor vice versa.

Capture descriptors into request metadata; never continue reading live
SPR_SNAP or a snapshot slot that can be recycled. For this port, raw sprite
ROM02052870..02152870 is fixed outside remapped slot7, so producer raw reads
have no borrowed baked-page lifetime. Do not read r60_bk_pos or mapped baked
pointers from the master. Future mapped-source adapters must cancel before a
page-switch request and refresh source caches after epoch changes; every READY
job carries/checks source epoch. Existing page/snapshot ownership stays intact.

## Recommendation and viability gate

Prototype the spare1KiB/four-row option first: existing cache and accepted art
stay intact, every sampled smoke row block fits, and the protocol is bounded.
Implement/host-test the queue and resumable raw-row encoder before adapter
integration; then test mixed prepared/fallback chunk sequences across every
hz/vz/flip/align/bank, cancellations, oversize/EOL faults, sequence wrap, and
per-record span/box/rect/pixel accounting. Native MAME/ares and long route gates
still apply. Hardware must record requested/prepared/used/late chunk counts
alongside complete entrance/death native/VInt/ack clocks.

The unresolved blocker is throughput, not packet capacity: only two small
blocks can lead; the slave may consume them faster than idle master decode
can refill. Every miss takes the old path, and extra handoffs/uncached reads
can cost more than saved decode. Shared ROM/SDRAM contention remains material
(the historical master cart idle-work lessons still apply). No eligibility
percentage predicts an event-duration gain. If exact pixels/health hold but
handoff cost dominates and prepared-hit counts show useful demand, compare
2KiB/sixteen-row slots as a SECOND experiment against controls that explicitly
price the lost old SPRPREP cache. Do not trade away the accepted cache first.

The design census did not mutate source or run a new rig test. A bounded decoder and transport are now being implemented separately with host tests; no ROM integration or speed result is claimed.

## Implemented component boundaries

`sh_src/r60/r60_sprite_chunk.h` captures a bounded descriptor and emits one to
four complete raw-pen rows, at most four source words per step. Packet size is
zero until successful completion. It preserves runtime32-bit source arithmetic,
including signed pitch/flip and vertical carry, and conservatively rejects
source/output/terminator/128-pixel limits. Source contents and mapped lifetime
must stay immutable until DONE/error/cancel. No partial packet may be published.

`sh_src/r60/r60_chunk_mailbox.h` provides two caller-owned slots with distinct
request/ACK and payload/READY writers. SH compilation requires an explicit
publication barrier. Alignment/uncached access and all outstanding generation
lifetimes are adapter requirements. A held reader must never be discarded;
ERROR generations need suppression and stale READY slots need draining so the
worker cannot spin uselessly. Expected request/read state is a short-lived local
or existing context, not an extra unbudgeted shared copy.

Confirmed SH sizes: slots104B, producer lease20B, decoder64B, counters32B =220B.
The two384B payloads are separate: total988B in the existing1024B spare region.
The conservative96B decoder allowance gives252B of control, not a new arena.

Actual C ASan/UBSan decoder tests pass4,360,699 cases and179,322 independent
old-word framebuffer projections, including all65,536 source words ×32 horizontal
zooms ×2 flips and661 native smoke chunks ×8 output alignments. Mailbox tests
pass four million cross-owner operations with3,181 actual full reads and2,824
stale discards, plus3,000 sustained recovery pairs. The random scheduler's
initial mostly-vacuous coverage was caught in independent review and corrected;
minimum successful-read/discard thresholds now enforce meaningful execution.
Independent review found no remaining blocking component defect. These are
component proofs; no dual-CPU ROM, cadence, exact live pixel or speed claim follows.
