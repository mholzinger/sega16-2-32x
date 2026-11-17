# Sound-page compaction: recovered sprite space

The accepted 32X renderer repeats the same music blob at the start of every
512 KiB mapper page.  That protects live music while slot 7 switches between
level art and the Neff working set, but it also made 394,652 bytes of every
page unavailable to graphics.

`R60SOUNDCOMPACT=1` removes only raw `SND_YMSFX` stream bodies from that blob.
The current 68K player has one Z80 stream lane and deliberately does not play
those bodies because doing so would cut the music (`md_src/md_sound.c`); the
game's effects are instead provided by the separate SH-2 `sfx.bin` lane.  All
rendered music streams, raw music fallbacks, command types and table indices
remain present.  Omitted entries point to one bounded `0xFF` terminator.
Default builds remain unchanged.

## Exact storage result

The full output of `tools/sound_blob.py` from the accepted PSG header is
byte-identical to the existing `sh_src/sound.bin`.  With
`--omit-unplayed-sfx`, the same input produces:

- full blob: 394,652 bytes;
- compact blob: 239,027 bytes;
- recovered on every mapper page: 155,625 bytes;
- smoke working-set free space: 176,424 bytes (previously about 20.8 KiB),
  of which 172,328 bytes remain available for art after the page-resident
  decoder's 4,096-byte reservation;
- each normal level page's baked-art tail: 285,184 bytes.

The baker's independent decode check reports 96.8% of corpus plotter words
served by the expanded level pages.  A first palette-merged macro census was
not suitable for a live renderer: it counted the decoder reservation as art
space and could not preserve the foreground-priority punch after every
original record.

The owner-per-record census now models the 32X overwrite image directly: each
nonzero byte belongs to its last writer, while a zero byte remains transparent
even when another lane in the same 32-bit store is nonzero.  Owner packets are
still priced as physical longword stores, but ownership is per pixel.  That
permits the unchanged sequence `owner stores N -> punch N` in painter order.
The full-surface regression starts from a nonzero background, replays every
selected scalar record and owner packet with a punch between records, and
requires all 320x224 bytes to match.

The live-format audit then found that useful cloud records are right-clipped,
while each repeated arrangement retains one fixed screen anchor.  The packer
therefore uses absolute collision-free signatures and crops owner packets to
the visible screen.  Each 24-byte record descriptor also carries the exact
original `r60_rowpix` total, so no discarded source pixels need rescanning.
The corpus reader accepts either `ev_*_sprite.bin` directories or one
SDRAM-aware ares `--gameframe-watch sdram:0x28400:0x400` CSV.  This matters:
the arcade table is the parity oracle, but the port's palette-remapped absolute
records are the live admission identity.  The captured port route contains
1,062 usable tables and 616 arrangements; the complete opportunity model
removes 45.51% of the target cloud's scalar stores.  With the current page
budget and a minimum 1,000-store saving, MC1 contains seven patterns covering
53 observed presentations and 283,797 weighted avoided stores in 169,528
bytes.  Its SHA-256 is
`addaf5b003c68e39ba81af0fc0c8e0ff1a0b6a1ee7f1b3aebf0d92feae685f68`.

`tools/bake_sprite_workingset.py --macro-packets` verifies and appends MC1
after TP1.  The previously recorded combined-page SHA belongs to the rejected
arcade-signature/whole-word artifact and is no longer a promotion input.  The
embedded-tape diagnostic layout is 2,316 bytes too large when TP1 and the new
MC1 are both present; its read-only admission builds omit TP1.  The normal
accepted art layout must be rebuilt before choosing between reclaiming that
small gap or dropping one lower-value pattern.
The target-portable `r60_sprite_macro.h` parser and collision-free
absolute-record admission helper pass ASan/UBSan; malformed headers,
directories, packets, pens and signatures fail closed.  Its metadata helper
also reconstructs the original scaled record's clear spans, dirty rectangle
and exact `r60_rowpix` charge without drawing the original pixels; it validates
the complete input and output capacity before writing any span.  The packer
keeps physical stores avoided separate from that charge: the normal row copier
counts nonzero source groups, while its right-clipped zoom helper counts
destination stores for nonzero zoom phases.  This was the format used for the
first admission proof; the later live caller is described below.

## Read-only live admission proof

`R60MACROPROBE=1` binds the generated MC1 identity and runs collision-free
admission at the real slave sprite loop, but performs no framebuffer or
bookkeeping writes.  A first frozen-frame run exposed a real boundary defect:
the matcher continued scanning stale sprite RAM after the hardware terminator,
so an otherwise exact formation could be rejected by an old target record.
Admission now stops at the terminator and permits the complete 64-slot table.
The sanitizer suite covers both the exact MAME capture and a synthetic target
record deliberately placed after the terminator.

The renderer fixture now has an explicit `PORTSPRITEFIXTUREBANK=0|1` selector.
Strict two-bank validation remains the default; selecting a bank is required
when a captured inactive bank has no terminator.  Replaying the established
level-one tape into the bank-0 frame from arcade tick 33,618 produced:

- diagnostic ROM: `build/port/rom/macrofixture02.32x`;
- SHA-256: `b243f5f936c1acf86ac3bd28c3bb9bb6ab187ff4c83ddfb028d83bf7c50611ea`;
- 49,793 admission attempts, two admitted bank instances and 32 admitted
  records;
- 1,256 later KEEP/repunch barriers, proving that admission must be planned
  once before the per-record loop rather than decided opportunistically;
- zero canvas-phase declines and zero malformed-header failures;
- 3,988 VInts, 3,992 native ticks, 3,429 diagnostic flips, 3,534 completed
  passes and zero span timeouts.

The OP1 helper has a two-pass packet writer: it validates the complete packet
and destination rectangle before its first store, then adds the runtime
palette base with the established overwrite-image semantics.  Host tests
replay all seven port-native patterns, including a deterministic foreground
punch between every owner packet, and compare the complete 320x224 result
against scalar painter order.  All seven are exact.  The diagnostic ROM still
does not replace the live scalar draw.

A stricter source/metadata/packet planning pass initially rejected the third
record.  That found another census defect before live pixels: decoded sparse
rows do not retain the ZB1 row's exact negative pixel extent, and clipping is
selected from the complete pose width rather than each sparse row's stored
coverage.  The census now restores every exact ZB1 row length and uses the
pose-wide clipping decision for `r60_rowpix`.  Regenerating MC1 changed only
the affected bookkeeping descriptors (the blob remains 170,912 bytes).

The corrected `macrofixture06.32x` read-only run completed two all-or-nothing
plans covering all 32 records.  Source resolution, original metadata,
aggregate span capacity and every owner packet all passed, with zero plan
failures; its 3,988 VInts tracked 3,992 native ticks with zero timeouts.  ROM
SHA-256:
`714262493cd983f2a6cfc6f1e17645a5a52e5986c118df9c316918245030d32f`.

The natural embedded route proved why an arcade-trained directory could not be
used: 2,219 structurally valid target-at-head runs produced zero directory
fingerprint hits because the port emits different palette-remapped absolute
records.  A single 33,801-frame SDRAM-aware ares run captured the port table
every emulator frame and became the direct packer input.  It also exposed two
pre-live census errors: ZB1 clipping must use the wire header width (64 in the
failing pose, not the longest decoded row's 62), and overwrite zero bytes must
not erase earlier pens.  Both now have focused host regressions.

`macroprobe09.32x` uses the corrected per-pixel port artifact.  In the first
natural smoke window it recorded seven exact admissions covering 112 records,
seven complete source/metadata/packet plans, and zero source, metadata, packet,
capacity, phase, header or KEEP-barrier failures.  The artifact SHA-256 is
`addaf5b003c68e39ba81af0fc0c8e0ff1a0b6a1ee7f1b3aebf0d92feae685f68`;
the diagnostic ROM SHA-256 is
`ee3460b8ddd28d2fcc2f99de0bfc7583cb52c3865035cf065b4e366d68313c56`.
The fresh headless MAME 600-frame oracle also passed with DIPs 66/54, five
events and a clean end marker.  The next gate is a diagnostic live caller on a
frozen fixture with framebuffer and bookkeeping differentials.  It is not yet
a play candidate.

## Fixture-only live caller

`R60MACROLIVE=1 R60MACROLIVEDRAW=1` now enables an all-or-nothing diagnostic
caller.  It validates the complete pattern, immutable art sources, reconstructed
scalar metadata, aggregate span capacity and all owner packets before publishing
the first span or pixel.  It then applies owner packets in original record order
and preserves each record's clear spans, box, row-pixel count, foreground punch,
punched rows, text damage and retained-sprite state.  Any admission or planning
miss stays on the scalar path.  The flag remains off by default.

The first live implementation uncovered and fixed a hardware-significant bug
before promotion: adding a palette base to all four packet bytes made a
transparent zero byte nonzero in the 32X overwrite image.  Macro packets now use
the same per-byte opaque mask as `bkcopy.s`; a mixed transparent/opaque host
regression covers it.

Matched control/live ROMs compile and call identical code and differ only in a
volatile nonzero data initializer (plus build-stamp digits), so code and BSS
addresses remain identical.  The final production-style `macroab-control05` /
`macroab-live05` A/B disables the read-only probe on both sides.  Over 7,201
ares frames both reached game tick 6,310 and 7,188 VInts with zero span timeouts.
Live admitted three patterns / 48 records and issued 16,023 owner stores with
zero post-plan failures, but completed 6,274 flips / 6,358 passes versus the
control's 6,281 / 6,365: seven fewer.  This live path is therefore rejected as
a speed candidate.  The next optimization is to serialize exact scalar span and
box metadata in MC1, eliminating the current two AB1/ZB1 metadata walks per
admission before considering an SH-2 packet-store kernel.

## Exact-metadata live caller

OP1 v4 stores the scalar box width and aligned end group for every source row
beside each owner packet.  The live planner now reconstructs boxes, clear spans
and the exact `r60_rowpix` charge from those bounded scalars; it no longer
resolves or decodes AB1/ZB1 art merely to recover bookkeeping.  The packer can
decode the immutable raw sprite ROM directly, so generation does not depend on
transient `artbake` outputs.  Host tests validate malformed metadata, replay all
selected patterns pen-for-pen with foreground punches between records, and run
the parser and renderer under ASan/UBSan.

The selected artifact is
`build/codex-sound-compact/smoke-owner-macros-port-v4.mc1`, 171,088 bytes with
6,280 metadata bytes.  It contains seven patterns observed in 48 captured
presentations and models 250,460 weighted avoided physical stores.  SHA-256:
`89ebaa0eabe225f3eafa2bf86b54699912957f72195606c63c44bfd5a3aa2a46`.

Matched `macroab-control06` / `macroab-live06` ROMs differed only in the
volatile enable initializer and build-stamp digits.  Over the embedded
7,201-frame route both reached game tick 6,310 and 7,188 VInts with zero span
timeouts.  Live admitted six complete patterns / 96 records, issued 31,615
owner stores, and reported zero post-plan failures.  Its endpoint was one
publication/pass ahead, but that same +1 phase offset was already present at
game tick 6,150 before the first macro attempt, so its normalized smoke-window
gain was zero.  Seven sampled
ares images included four exact control/live matches; the differing samples
were intermediate cloud states and the streams reconverged exactly.  A fresh
600-frame MAME oracle also ended cleanly with DIPs 66/54 and five events.

The next planner revision keeps the all-or-nothing validation boundary but
commits already-validated spans with a ready writer instead of repeating the
immutable metadata bounds walk.  In the matched `macroab-control07` /
`macroab-live07` run, live again admitted six patterns / 96 records with 31,615
stores and zero failures.  Its raw endpoint delta was +3 publications/passes
from a +2 pre-macro phase offset: normalized across game ticks 6,150..6,310,
the ready writer gained one publication and one completed pass.  This remains a diagnostic
result, not a promoted ROM: exact sampled-image gates and a physical smoke pass
still precede promotion.  The accepted MiSTer ROM was not replaced.

The generated SH-2 disassembly then exposed four byte loads and a shift/or
reconstruction for every aligned packet word.  OP1 already validates every
packet offset as four-byte aligned, and the target is big-endian, so the target
loop now uses one aligned `mov.l`; host tools retain the bytewise portable
decoder.  The matched `macroab-control08` / `macroab-live08` run reached the
same game tick and 7,188 VInts with zero timeouts and zero post-plan failures.
It naturally admitted five formations / 80 records (26,591 owner stores).  Its
raw endpoint delta was +4 publications/passes from a +3 pre-macro phase offset,
so the normalized smoke-window gain remained +1: the cheaper load did not cross
another presentation threshold on this route.  Because this run exercised one
fewer formation than v7, it is not a like-for-like instruction-cost benchmark.  The
follow-up MAME 600-frame oracle again passed with DIPs 66/54, five events and a
clean end marker.

The largest live-planner win came from separating untrusted parsing from
validated-ROM placement.  The public packet entry still scans and validates all
content before its first store.  The live caller, however, consumes a generated
artifact already checked by the packer and immutable page builder; admission
now rechecks only packet screen placement instead of reading every packet word
once in preflight and again in the draw.

On matched `macroab-control09` / `macroab-live09`, the live path began three
publications/passes behind control at game tick 6,150 and ended three ahead at
tick 6,310: a normalized six additional presentations inside the smoke window.
It admitted 16 complete formations / 256 records, issued 83,935 owner stores,
and reported zero post-plan failures and zero span timeouts.  Seven sampled
image pairs were valid cloud phases and the final pair was byte-identical.  The
fresh MAME oracle again passed with DIPs 66/54, five events and a clean end
marker.  This is the first materially positive macro result, but remains a
diagnostic ROM pending a physical smoke/audio/gameplay pass.

## Playable packaging and page-7 closure

The v9 experiment originally depended on resident helper code and a transient
64-record scratch allocation.  A normal playable build could not carry that
layout together with the full port, mapper music and working-set page.  The
packer now emits the largest record count present in the verified MC1 file;
the runtime sizes both planning arrays from that generated bound.  The selected
play artifact needs 16 records, rather than 64.  Its identity is:

- `build/codex-sound-compact/smoke-owner-macros-play-v5.mc1`;
- 169,584 bytes, seven patterns and 47 captured presentations;
- 247,991 weighted avoided stores;
- SHA-256 `b3226df8ce3343ecf597ff90bf940510c88b6ae87a0b7eadf2856e362119609f`.

The otherwise-unused working-set block compositor was replaced, for
`R60MACROLIVE`, by the macro parser/planner/draw closure in the existing page-7
code reservation.  The first C closure occupied 1,784 of 4,096 reserved bytes.
`macrolive-play08.32x` then linked as a normal 6,815,744-byte playable ROM:
page code `0x023FEFC0..0x023FF6B8`, BSS end `0x06017458`, and hot-code end
`0x06018FB0` (80 bytes below the renderer region).  Its SHA-256 is
`68725aa4bed530382a28d26a62a7390af91f5e48e51c7658698b87b950839717`.
Cold 16,500- and 29,000-frame ares gameplay routes completed with zero span
timeouts, and a fresh MAME oracle passed, but those wall-frame tapes did not
reproduce an MC1 signature.  It remains a packaging candidate, not a promoted
renderer.

The exact embedded route needs a smaller artifact because `PORTTAPEPAD` removes
art window 1.  `smoke-owner-macros-tape-v6.mc1` is 163,460 bytes, contains
seven patterns / 47 presentations, has a generated maximum of 16 records and
SHA-256 `ca39f6f1a53798d1b070b1d38a05defa80e96ec59d23930b2216c44bd5ab78d3`.
The full level-one tape also adds 456 bytes below `.ramtext2`, so its diagnostic
hot set returns four low-value functions (416 bytes) to cartridge code.  The
result ends at `0x06018FF0`, 16 bytes below the renderer region.

On the preserved full level-one route, `macroclosure03` proved that the page
closure really executes: five complete formations / 80 records, 25,900 owner
stores, and zero post-plan failures, KEEP barriers or span timeouts.  Its
matched scalar control nevertheless finished one publication/pass ahead.  The
C page closure is therefore a functional packaging proof, not a speed win.

Disassembly showed that its trusted packet entry still accepted six arguments,
loaded two from the stack, saved seven registers and repeated checks already
owned by the all-or-nothing planner.  A closure-local SH-2 leaf now takes its
four inputs in `r4..r7`, saves four registers and returns the store count in
`r0`.  The public portable checked C function remains unchanged for untrusted
callers and host tests.  The leaf is 140 bytes versus the compiler's 264-byte
entry, and the complete page closure is 1,660 bytes.

Matched `macroclosure04-control` / `macroclosure04` ROMs differ only in the
volatile live initializer plus build-stamp digits.  Both reached 7,189 VInts
and 7,191 ticks.  Control completed 6,288 publications / 6,372 passes; live
completed 6,292 / 6,376: **+4 publications and +4 passes**.  Live admitted six
formations / 96 records and issued 31,615 owner stores with zero failures,
KEEP barriers or span timeouts.  The working-set, packer and ASan/UBSan macro
suites pass, and a fresh 600-frame MAME oracle reported DIPs `66/54`, five
events and a clean end marker.  This is still diagnostic evidence: the
assembly leaf requires a target pixel differential and physical smoke/audio
pass before it can become a player ROM.

The target differential now passes.  Its first version incorrectly expected
ordinary RAM readback from `FB_OVR`; the 32X overwrite-image window preserves
the old framebuffer byte wherever a stored pen byte is zero.  The corrected
audit compares every packet-owned (nonzero) byte immediately after the SH-2
leaf returns.  `macroclosure05-audit4` emitted 169 records and 54,148 stores
with **zero owned-pixel mismatches**, zero planner failures, zero KEEP barriers
and zero span timeouts on the preserved level-one route.

`build/port/rom/macrolive-asm01.32x` packages the same leaf as a normal player
ROM with the seven-pattern play artifact and no tape or audit instrumentation.
It is 6,815,744 bytes, SHA-256
`07ecf8fb61be0868fd82e24ff2db72afeac88aa8d399b476f5ff27bab5a909fd`,
and its cold 1,800-frame ares gate completed with zero span timeouts.  It is a
play-test candidate.  A fresh 600-frame headless MAME oracle also passed with
DIPs `66/54`, five events and a clean end marker.  The accepted MiSTer ROM
remains untouched.

## Shared macro assets

MC1 v2 / OP1 v5 separates pattern descriptors from an immutable packet and
metadata pool.  Identical record art is stored once across formations; the
v1/v4 reader and artifacts remain supported.  Under the normal 170,284-byte
budget, `smoke-owner-macros-shared-v7.mc1` fits 60 patterns / 172 captured
presentations in 167,632 bytes, versus 7 patterns / 47 presentations in the
169,584-byte v1 artifact.  Weighted avoided stores rise from 247,991 to
852,384.

The final v2 directory is fingerprint-sorted and admission uses a bounded
binary search; v1 retains its linear lookup.  The normal artifact SHA-256 is
`40256c905430c1a2cefc4eccb040a58d1a46a4650886a014f5f1d19f11b75274`.
The 161,888-byte tape variant exercised 265 completed records and 84,732 owner
stores before the audit cutoff on the preserved full level-one route.  The target audit
reported zero owned-pixel mismatches, planner failures, KEEP barriers or span
timeouts.  Matched `sharedmacro-bin-control01` / `sharedmacro-bin-live01` ROMs differ
only in the live initializer and two build-stamp digits.  Control completed
6,312 publications / 6,397 passes; live completed 6,320 / 6,405: **+8
publications and +8 passes** over 7,201 frames.  The live arm admitted 21
formations / 325 records and issued 103,104 owner stores with zero failures.

The normal player candidate is
`build/port/rom/macrolive-shared02.32x`, SHA-256
`e5dd15774e9eb358dd56f0c97dccc89b8fbb6b815e16b3da81424f783210be61`.
Its cold 1,800-frame ares gate has zero span timeouts, and the fresh 600-frame
MAME oracle passed with DIPs `66/54`, five events and a clean end marker.  It
still needs a physical smoke/audio/gameplay pass; the accepted MiSTer ROM was
not replaced.

## Transformation page/palette handoff

The first shared-macro player pass exposed a renderer synchronization defect,
not a macro-pixel defect.  The beast cut's page selector, palette queue and two
name-table packets cross the game, master SH-2 and 68K at different times.  A
new palette could therefore be presented over the old or half-written table:
wrong-colour flames on entry and briefly mixed tiles on return.

The plane builder now treats a cut transition as a directional transaction.
It starts the MD-plane hold at the player state-15 countdown edge, two game
frames before `FUN_90F4` publishes the cut owner, and retains that hold until
the requested cut or gameplay page has completed its explicit two-packet
switch.  Player slots 0 and 8 are both covered.  Gameplay timing and palette
timing are unchanged; only presentation of the incomplete MD plane is hidden.

The first implementation, `macrolive-transformfix02`, was rejected after a
manual orb-one pass: state 15 is shared by every spirit-orb upgrade, so the
early hold armed on target forms 1/2 and waited indefinitely for a cut that
only target form 3 performs.  The final predicate therefore requires target
form 3 as well as the state/countdown edge.  A dedicated target-form-2 fixture
crossed the former trigger at frames 1672/1676 with the complete playfield
visible before, during and after it.

The forced level-one target-form-3 fixture was inspected frame by frame from
1748 through 1770 and 1935 through 1955.  It contains no frame that combines
the gameplay table with the cut palette or the cut table with the gameplay
palette.  The flame colours are stable through entry and return.  The normal
candidate is `build/port/rom/macrolive-transformfix03.32x`, 6,815,744 bytes,
SHA-256
`932464e6ba9b0a29fbbd73cccee6967890ca04db984a53d1dafb19353df8194b`.
Its cold 9,001-frame ares run reached 8,989 VInts / 8,992 native ticks and zero
span timeouts.  The macro decoder, 24-test working-set suite, three scene
fixtures and packer compile check pass.  A fresh 600-frame MAME oracle also
passed with DIPs `66/54`, five events and a clean end marker.  Physical
transformation and smoke/audio play remain the promotion gate.

## Candidate and gates

Candidate: `build/port/rom/sndcompact01.32x`.

- compact-blob reachability test: pass;
- sprite-transition decoder and packer tests: pass;
- working-set suite: 24 tests pass;
- 9,001-frame ares: 8,989 VInts, 8,992 native ticks, 7,464 diagnostic flips,
  zero span timeouts (accepted smoke line: 7,373 flips);
- 65,001-frame ares soak: 64,989 VInts, 64,991 native ticks, 54,623
  publications (0.84050/VInt), 56,651 completed passes and zero span timeouts;
- 125,001-frame ares soak: 124,989 VInts, 124,991 native ticks, 105,230
  publications (0.84191/VInt), 109,099 completed passes and zero span
  timeouts;
- temporal windows: 48/59 and 56/59 presented frames (accepted: 48/59 and
  55/59);
- attract alignment: unchanged offset -5;
- fresh 600-frame headless MAME arcade oracle: correct `66/54` DIPs, five
  events and a clean end marker.

The physical audio/gameplay pass remains the promotion gate.
The ROM is a storage-and-coverage candidate; it does not yet include a new
macro-cloud renderer.  The accepted MiSTer ROM was not replaced.

## Reproduction

Regenerate the compact source blob before building:

```sh
make -B sh_src/sound.bin R60SOUND=1 R60PSGMUSIC=1 R60SOUNDCOMPACT=1
```

Then add `R60SOUNDCOMPACT=1` to the accepted line's build flags.  The source
stamp includes `compact`, so `make candidate` refuses a stale full or compact
blob instead of silently building the wrong page layout.
