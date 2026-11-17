# Physical block sprite compositor: host and target prototypes

This is a correctness prototype, not an enabled renderer or a hardware speed
claim. Normal ROMs do not include it. It assembles contiguous admitted sprite
runs in a 32×32 scratch tile, then writes their surviving pixels once. RAW and
REPUNCH records remain ordering barriers. KEEP records retain their original
metadata and do not write pixels.

The first actual slave SH-2 execution proof passes in a frozen smoke scene.
Private wsbc03 replaces cloud records 6 through 21; matched wsbr03 uses the old
path with the same code reservation and prep-arena setup. Both preserve the
six-record prefix. Exact completed output matches across all 71,684 backing
bytes, 1,024 ordered spans, 22 records/boxes/punch records and 22 rectangles,
including the arcade-derived native oracle. Full screenshot comparisons have
zero differences. Page-code entry is observed on the slave, never the master;
fatal/canary counts are zero. Observed stack depth is a lower bound, supplemented
by the linked static bound below 2 KiB.

This proof uses GO/publication/completion and actual flip history to map tracked
metadata banks to physical DRAM. A terminal FS bit is not a scanout certificate.
The failed first pair exposed rectangle-bank ownership; the wrongly built second
pair was rejected before execution. Both remain archived. The third proof was
independently reanalyzed, including the unchanged native/PENID oracle. Evidence:
`target-compositor-fixture02/actual-target-proof/REPORT.md` and
`target-compositor-fixture03/independent-target-review/REPORT.md` under the audit
root. Intrusive per-byte counters prevent a speed claim; normal gameplay still
has no compositor caller or live plane-epoch binding.

The subsequent wsbr04/wsbc04 pair removes hot microcounters and replaces the
loader's repeated range scan with a constant-time current-packet bounds check.
Core and scene data remain unchanged. The same complete target checks pass,
with independent reanalysis; unavailable fields remain UINT32_MAX and callback
PC traces prove actual use. This is a second correctness checkpoint. Its
whole-window instruction census includes different amounts of completed and
unfinished work and is not presented as per-pass cost, FPS or hardware speed.
A zero-growth packet-row cursor experiment was evaluated separately.
Evidence: `target-compositor-fast01/actual-target-proof/REPORT.md` and
`independent-target-review/REPORT.md` under that experiment directory.

The wsbr05/wsbc05 experiment compiles the bounded packet-byte read into the
core. Default mode is byte-identical to the previous target code. Specialized
target output passes the same complete checks and independent reanalysis: all
eight audited guarded read sites execute on the slave, and the old loader
callback executes zero times. Closure is 7,100 of 8,192 reserved bytes; the
static stack bound remains below 2 KiB. No packet validation is bypassed.

The separate row-cursor experiment passes 1,462 host cases but saves only 2.27%
of native packet reads while adding 364 code bytes. It remains withheld.

The private wsbr06/wsbc06 word-oriented experiment also passes all complete
target checks and independent reanalysis. It clears and combines four scratch
pixels at a time, retaining bounded byte validation and a checked aligned word
read. All four certified byte sites and the word site execute on the slave;
the old loader stays at zero. All 71,684 bytes, 1,024 spans, 22 metadata records,
native/PENID checks and full screenshots match. Closure is 7,736 of 8,192 bytes;
the static stack bound is 1,424 of 2,048 bytes. The observed 1,080-byte depth is
only a lower bound. This run completes metadata bank0 in physical bank1; the
proof derives that mapping from actual flip history rather than reusing05's.
See `target-compositor-word01/{actual-target-proof,independent-target-review}/REPORT.md`.

No speed benefit over Inline05 is established. A private hardware meter counts
whole sixteen-cloud runs over a common fixed 2,000-VInt interval and paints
retained results afterward. Meter02 passes linked identity, fence and stack
checks, but its first paired headless runs are withheld for measurement. The
fixture intentionally stops the arcade clock while tick calls keep advancing;
the decoder had required live-game clock progress. There are also11/3 measured
preflight rejections. CFG analysis proves most are framebuffer busy/FEN
checks; individual interval causes are not fully traced. Fatal,
canary and consistency flags are zero; retained pages match raw records, and
all postfooter screenshots match same-ROM3999 outside rows216..223.

Revision03 adds a common bounded FEN wait, retains all admission guards and
uses an explicit frozen-fixture clock domain. Its first paired headless runs
still have20/1 interval rejections and remain withheld. FEN includes refresh and
pending flips as well as autofill; the aggregate profiles do not identify every
individual interval rejection's cause.

A separate emulator discrepancy is proven at the unit level: the hardware's
FRC high-byte read latches the low byte into shared TEMP, but canonical ares
returns live bytes. Actual SH-2 instruction/timer tests reproduce a torn sample
and false elapsed-time arithmetic. The manual-correct private model passes
393,235 checks and independent review. A full private rebuild and independent source/archive/header custody checks pass;
the same03 ROM pair is being rerun under that separately named tool. The
canonical tool and correct game read sequence are preserved. This does not yet
attribute the observed FEN failures to the timer discrepancy. Three matched cold-launch pairs, unique trial
signatures and accepted rdplay01 restoration after every arm remain hardware
gates. No meter ROM has been launched on the rig.

Two further private candidates pass host checks and independent review: packed
four-byte validation (801,625 scalar differential cases; linked7,980/8,192 bytes)
and foreground row-mask construction (576,000 independent row cases; object
7,924/8,192 bytes). The latter retains the same opacity callback and packet-read
counts; it reduces coordinate/bit calculations. Packed validation now also passes full target execution and independent raw
reanalysis: both completed candidate banks match all backing bytes, metadata,
native/PENID and full screenshots. Both word-read roles execute on the slave.
Foreground row-mask target execution and all hardware benefits remain unproven. These experiments do not establish natural gameplay
integration.

The measured opportunity is framebuffer traffic: the native frame 33614 scene
uses 10,660 sprite/punch word stores for 4,530 unique destination words in 24
physical blocks. That 57.5% duplicate share is an upper-bound opportunity, not
a predicted frame-rate gain. Whole-frame all-baked admission covers few entrance
frames and no death frames; bounded contiguous runs are required.

The host reference preserves physical stride 320, padding wrapping into the next
row, the final four backing bytes, transparent sprite bytes, palette identity,
per-record DRAW then foreground PUNCH, original span slot tags, box and punch
metadata, and arbitrary retained backgrounds. The final 1,430 differential cases
include all 45 scaled packets, all four dot shifts, the native 22-record scene,
nonzero subranges with absolute slot tags, excluded hostile records, KEEP
bridges, and empty/single-DRAW rejection.
Destination-store statistics are explicitly unavailable rather than reported in
the old per-sprite units. Both the original and optimized prototypes pass the
same sanitizer corpus. On the earlier identical 1,401-case corpus, optimization
removes 16.5% of packet-byte reads and 85.5% of foreground-row callbacks, with
identical output stores. These are work counts, not measured hardware time.

The borrowed arena totals 4,080 of the existing 4,096 SPRPREP bytes. A separate
persistent 64-byte lease header must remain outside that workspace; the arena's
header is an immutable context copy. No runtime address is reserved yet. The
protocol prototype requires canonical physical addresses to detect SH cached/
uncached aliases, aligned uncached atomic words and target publication ordering.
The master prepares immutable foreground data; the slave acquires it before
KEEP preflight, then separately waits for clear completion before pixel writes.
A ready unconsumed plan must retire before another native tick/publication; a
32-bit workspace generation alone does not prevent a 16-bit snapshot lap alias.

Foreground generation is only an observation. The native tick can change tile
words before the plane walker updates FGGEN. Runtime integration therefore
needs a content identity that binds all KEEP and fallback punches to the same
frozen foreground; sampling FGGEN alone is insufficient. The snapshot/GO/page
leases must also be proved at the actual source seams.

The named cold sections have 3,336 free bytes. The initial assessment counted
only those sections and omitted a usable 3,748-byte fixed ROM-load gap below
the existing speech boundary. The actual 3,460-byte compositor fits that gap;
64-byte alignment leaves 252 bytes. The guarded 350-byte foreground freezer and adapter
glue can use the separate cold sections, subject to their final linked limits.
Resident placement is linked and checked in `wsfix01`: the 3,460-byte
closure occupies cached `0x02233180..0x02233F04`. Every existing nonstamp ROM
byte remains identical to the disabled normal control. Commit `5da80876`
contains the opt-in placement and strict object/link guards. There is no
renderer caller or runtime execution/performance claim. No accepted hot function or asset moves,
no arena grows, and the original speech boundary remains a hard limit.

An alternative optional 8 KiB page-7 reservation also links without changing any
byte outside its owned code and final build stamp. It remains a placement
experiment with no caller. Its instruction lifetime would need the page lease;
fixed resident placement avoids that restriction. Both modes must preserve
default-off image identity and include the complete readonly code closure.

Evidence: build/codex-sprite-block-audit/REPORT.md, runtime-plan/INTEGRATION-PLAN.md,
frame-plan-review/REPORT.md, and the actual-header diagnostic tests. Runtime
integration, exact native target proofs and paired hardware timing remain gates.

The generic capture helper `r60_fg_window.h` is committed at `fe2b1a15`.
Its 4,235 sanitizer cases cover native map input, complete scrolling/wrapping,
invalid pages, original logical eligibility, tagged physical zero, guards and
15-bit mapped identities. The board mapping hook belongs to the caller. This
captures a caller-selected stable view; it does not certify the shown MD view.

A natural two-pixel case now demonstrates that distinction: the completed sprite
punch uses FG scroll951 while committed MD scroll950 retains foreground pixels.
The sprite pen and palette are correct. End-of-frame bank selection cannot be
used blindly as the scanout witness. The read-only presented-plane audit and
private ACK-before-flip experiment investigate the binding before integration.
See build/codex-sprite-block-audit/presented-plane-binding/REPORT.md and
build/codex-working-set/flip-ack-poc/. No delay or forced KEEP disabling is used.
