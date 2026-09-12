# System 16 → 32X Porting Toolkit

Goal: everything built for the Altered Beast port that is NOT
Altered-Beast-specific gets promoted into a reusable toolchain, so the
next System 16B title starts from a kit, not from scratch. This file
inventories what exists, what each piece assumes about the game, and
what must be parameterized.

## The port pipeline (game-agnostic shape)

1. **ROM analysis + patching** — `tools/patch_game.py`
   Rebases the arcade 68K binary's video/IO accesses into the 32X
   memory map (tile RAM → FB staging, text RAM → MD work RAM, sprite
   RAM/palette → FB staging, IO → work-RAM mailboxes).
   - Reusable: the address-class scanner (objdump-driven), the
     A-site confidence rules, the DATA_EXCLUDE mechanism, the
     byte-write→word-write RLE patch pattern.
   - Per-game: the address map itself, boot-region redirects,
     excluded data tables (spawn scripts etc.), byte-writer sites.
   - Also reusable: the WRITE-OBSERVER THUNK EMITTER (two families
     now — `TILE_DIRTY_SITES`, `PAL_DIRTY_SITES`). See "Trapping the
     game's writes" below; this is the kit's answer to any hardware
     region the 32X cannot mirror cheaply, and it generalizes to
     every S16 title.
   - TODO for kit: move per-game facts into a declarative per-title
     config (`games/<title>.toml`) consumed by a generic patcher.

2. **Asset conversion** — `tools/gen_sprites.py` (+ tile equivalent)
   Interleaves the arcade sprite/tile ROMs into 32X-friendly binaries.
   - Reusable as-is for any S16B board set (same ROM interleave);
     parameterize ROM filenames/count per title. The parameters do not
     have to be hand-written — see "MAME is the asset config source".
   - `tools/bake_sprites.py` needs NOTHING per title: it consumes
     `sprites.bin` plus a MAME-discovered job corpus
     (`tools/sprite_discover.*`), and its accuracy gate re-decodes
     every baked frame through a Python port of the live algorithm
     before the ROM links. Already kit-grade.

## MAME is the asset config source — KIT-CORE

Do not hand-transcribe ROM filenames, sizes, or interleave for a new
title. `mame -listxml <driver>` emits the whole manifest, and the
OFFSET PARITY ENCODES THE INTERLEAVE EXACTLY (verified on altbeast,
2026-08-17):

    opr-11674.a14  size=131072  region="tiles"    offset=0
    opr-11675.a15  size=131072  region="tiles"    offset=20000
    opr-11676.a16  size=131072  region="tiles"    offset=40000
    epr-11681.b5   size=131072  region="sprites"  offset=0        hi
    epr-11677.b1   size=131072  region="sprites"  offset=1        lo

Rules read straight off it: `tiles` = N sequential planes at stride
plane_size (3 planes, 8x8, 3bpp on every S16); `sprites` = pairs whose
offsets differ by 1, even = high byte lane, odd = low, one pair per
0x40000 block, bank count = total/0x20000 words. That is precisely
what `gen_tiles.py`/`gen_sprites.py` currently hardcode.

The same XML carries the rest of the per-title personality: the
`<machine>` device list names the board (`sega_sys16b_sprite`,
`sega_315_5195`, `i8751` vs `fd1094`), `region="mcu"` says whether an
MCU must be emulated, a `317-xxxx.key` ROM flags encryption, and
`<display>` gives geometry and refresh.

3. **The renderer core** — `sh_src/m_main.c`, `sh_src/s_main.c`
   A software System 16B video chip for the two SH-2s: tilemap
   pages/scroll/priority, sprite format incl. zoom/flip/pitch, text
   layer, S16 palette conversion, sticky CRAM group allocator, SDRAM
   tile cache (RV-rule-safe concurrent compose), row-following
   3-window pipeline with vblank-gated slice blits.
   - Almost entirely game-agnostic already (it implements the BOARD,
     not the game). Per-game: tile bank register wiring, screen-flip,
     board revision quirks (S16A vs S16B sprite format).

4. **The MD shim** — `md_src/md_main.c`, `md_src/md_start.s`
   Vint window scheduler (V-counter gate + COMM12 heartbeat clock),
   MCU emulation scaffold (coins/inputs/sound mailbox), text-RAM
   streaming, FS-home discipline, boot tracer.
   - Per-game: MCU personality (or no MCU), input mapping, DIP
     defaults.

5. **Verification rig** — `tools/play_32x.lua` (+ objdiff/profiler
   harnesses, statistical screenshot scanners in scratchpad)
   - Scripted coin+start gameplay in MAME, work-RAM counter probes,
     object-slot differ against the reference arcade driver, FRT
     profiler readout, PIL/numpy frame-scan for artifact classes,
     on-screen perf bars for ares ground truth.
   - `tools/pal_tap.lua` — per-PC write census over any address range
     (site list + extents + region masks). Run this BEFORE trusting any
     statically-enumerated write-site list; see "Trapping the game's
     writes". Game-agnostic: point it at a range via env vars.
   - `tools/pal_probe.lua` — diffs an MD mirror against the SH-2 shadow
     per region, so a transport fault can be separated from a fault in
     the consumer. ALWAYS run it on the BASELINE too: the "hot regions
     never converge" reading that sent one fix down the wrong road was
     NORMAL — a 60Hz colour-cycle against a ~20Hz channel is
     permanently mid-flight, and the baseline was worse.
   - `tools/pal_rate.lua` — dirty-bitmap occupancy per unit over time;
     how starvation and ship-rate problems become visible.
   - `tools/strobe_scan.py` — black-frame census over an ares capture
     by FILE SIZE (no PNG decode). Feed it RAW captures; dedup inflates
     the rate by the dedup ratio.
   - TODO for kit: promote the screenshot scanners out of the session
     scratchpad into `tools/scan_frames.py`; make the lua harness
     read per-title input scripts.

## Trapping the game's writes (the write-observer pattern) — KIT-CORE

The single most reusable structure this project has produced, and the
one every S16 title will need. THE PROBLEM IS UNIVERSAL: an arcade 68K
writes freely to hardware (tile RAM, palette, sprite list) that on the
32X lives somewhere the MD cannot cheaply hand to the SH-2s. Mirroring
the writes into MD RAM is easy; knowing WHAT CHANGED is not, and the
naive answer — diff the mirror every vint — is ruinous. Altered Beast's
palette diff cost 45 of the MD vint handler's 92 scanlines to discover,
in steady state, that nothing had changed (LOOP 6 negatives 3-5: not
division-bound, long compares are free on a 16-bit bus, and ablating the
loop body took 45.1 lines -> 0.1, so the loop WAS the entire cost).

THE PATTERN. Patch each write site to `jsr (xxx).w` into a small thunk
in MD RAM that ORs a dirty mask into a bitmap word, runs the displaced
instruction, and returns. The shim ships only dirty units and clears
the bit. Applied twice now: tile pages (LOOP 3c) and palette regions
(LOOP 8, which retired the scan outright — MD tail 92.4 -> 48.2 mean
scanlines, palette term 45.1 -> 0.1).

RULES, each one paid for:
- **Thunk addresses must have low word >= 0x8000.** 68K `abs.w`
  SIGN-EXTENDS, so 0x5E00.w targets low ROM and crashes at the first
  thunked site. Both families hit this.
- **A `jsr (xxx).w` is 4 bytes**, so it can displace a 6- or 8-byte
  instruction directly, or a PAIR of 2-byte ones (that is how the
  register-indirect palette writers were trapped).
- **Never mark ALL-dirty if the extent is derivable.** An ALL-dirty
  mask on a per-frame animator floods the ship budget and lags the
  content (title parity 63-70% on the tile side before 0x258A got a
  precise thunk). Disassemble the loop bound; every Altered Beast
  palette site turned out to be derivable.
- **When the extent is only knowable at runtime, compute it in the
  thunk.** Derive the unit index from the address/index register, index
  a PC-relative mask table, save/restore any scratch register. Cheaper
  than it sounds and far cheaper than over-marking.
- **Mark BEFORE the store, and expect a race.** For loop bases the
  thunk marks, then the loop stores; a vint landing between them ships
  and clears the unit, losing the stores. Fix without a backstop scan:
  ship each freshly-marked unit TWICE (one repeat, two words of state,
  zero RAM reads). The loop has certainly finished a push later.
- **Round-robin the selection, never lowest-bit-first.** Hot units
  starve the rest forever: lowest-bit selection left Altered Beast's
  ENTIRE sprite palette unshipped, dirty in 99.6% of frames.

THE ENUMERATION IS THE DANGEROUS PART, and a static scan is NOT
sufficient. Address-formation scans (`patch_report.txt`) see `lea`/
`move.l #imm` but CANNOT see a writer that took its pointer from a
queue: Altered Beast forms a palette pointer at one site, stores it to
a table, and writes through it from two unrelated loops. Nothing static
attributes those. **Always run a write-tap census first**
(`tools/pal_tap.lua` — a memory tap, not a debugger watchpoint, which
is far too slow for hundreds of writes per frame). It reports per-PC
address ranges AND a per-unit region mask, which both finds the missing
writers and VALIDATES every hand-derived extent: the shared copy helper
observed exactly the union of its four callers' static masks.

## Your emulator may not model the framebuffer at all — KIT-CORE

Every 32X port's hot loop is an SDRAM->framebuffer blit, so the cost of
an FB write is the single most load-bearing number in the whole project.
MAME does not model it. Measured on Altered Beast, one session, four
independent proofs:

    cached vs uncached FB alias    MAME: byte-identical    ares: also equal
    us/row, same instruction stream MAME: 9.43            ares: 47.34  (5.0x)
    DMAC ch1 blit vs CPU stores     MAME: 18% FASTER      ares: 77% SLOWER
    SH-2 FB write with FM=0         MAME: 100% land       ares: 85.8%

Per-term, ares/MAME on the same build: blit 5.05x, copy_pages 8.92x,
apply_cram 2.02x, TOTALwin 3.80x. **Every term is understated by a
DIFFERENT factor, so the fast emulator cannot even RANK them** — which
is worse than being uniformly wrong, because a ranking is exactly what
you use it for.

THE KIT RULE: before optimising anything that touches the framebuffer,
run the cached-vs-uncached null test (same instruction stream, swap
0x04000000 for 0x24000000). If your fast emulator reads them identical,
it is not modelling FB cost, and every window measurement it gives you
is fiction. Iterate on it for CORRECTNESS (parity of statics) and take
every timing verdict from the accurate target. Budget for the round
trip; it is not optional.

Corollary for probes: put counters in a fixed SDRAM block the savestate
reader can find, not just in emulator-scripting hooks — the accurate
target is usually the one you cannot script. See `tools/state_health.py`,
`span_hist.py`, `wait_split.py`.

## Compose-to-ship pipelines: the gap must fit the BIGGEST band — KIT-CORE

Any port that composes into a staging buffer and blits to the FB in
slices runs a pipeline: band composed at window k, shipped at window
k+n. Two rules this port paid for:

1. **State the invariant, then MEASURE it.** This code asserted for nine
   iterations that the blit set and the outstanding compose were
   "DISJOINT by pipeline construction". They were the same band, in all
   three windows, the whole time. A comment is not a measurement.
2. **Size the gap for the biggest band, not the average.** 224 rows is
   28 tile rows; 28 does not divide by three, so bands are 72/72/80. The
   72s finish inside one window gap and the 80 does not — so ONE window
   in three pays a wait that scales with scene load, and that window is
   the whole strobe. Widening the gap to two windows (compose k -> ship
   k+2) costs one window of latency applied UNIFORMLY, which leaves the
   band-to-band SPREAD unchanged — and the spread is what seams are made
   of, so it adds no new seam.

Do not fix a compose overrun with more mailbox-poll points. Polling more
often cannot shorten work that has not been done; it costs more compose
time than the bounded latency saves (docs/log/LOOP.md iteration 7f, reverted).

## MD-hardware landmines for arcade 68K code (kit-critical)

- **TAS never latches on MD/32X**: the MD bus arbiter drops the write
  phase of the 68K locked RMW cycle (MAME, ares, and real hardware all
  drop it; S16 boards don't). Every `tas/bne` one-shot latch in a
  ported game silently re-fires forever. Altered Beast: 5 TAS sites;
  the one at 0x2268 locked the attract sequencer into the infinite
  title/eye loop (velocity-kill add ran twice → camera runaway past
  the transition gate). KIT RULE: opcode-scan every ported binary for
  0x4AC8-0x4AFF words in code, replace each TAS with jsr to a RAM
  thunk: `tst.b` (TAS's exact N/Z, clears V/C) + `st` (no CC) + `rts`.
  See patch_game.py TAS_SITES + md_main.c TAS thunks.

- **Run the census, do not eyeball it.** `tools/code_stream.py` builds the
  program's instruction stream from the reference disassembly's own
  address list (no Ghidra, no listing), and
  `tools/hazard_census.py --stream` reports every dependency class over
  it, plus a raw-opcode sweep for the classes whose opcode is distinctive
  enough to survive one — TAS, STOP, RESET, TRAPV. The loose ones (CHK,
  MOVEP) are deliberately NOT swept: MOVEP's pattern matches the `0000
  xxxx` longword every rom pointer table is made of, and a list nobody
  reads is worse than no list. Altered Beast output is
  `docs/audit/hazard_census.txt` (LOOP-DECOMPILE 76).

- **STOP is the same shape as TAS and nobody has hit it yet.** 12 sites in
  Altered Beast, all inside the service/test region. The 68000 halts until
  an interrupt the arcade guarantees; on 32X it resumes only if that
  interrupt actually arrives. KIT RULE: census it, and if the port can
  reach test mode at all, patch every site.

- **A read-modify-write on a write-only latch is a hidden hardware
  contract.** Altered Beast has one: `bclr #6,0xC40001`. On the board the
  read returns 0xFF, because jts16b_cabinet.v's A[13:12]==0 arm only
  latches flip and video_en from cpu_dout and never assigns cab_dout
  (:199-202, default at :189). So the instruction writes 0xBF — flip off,
  video ON — and looks harmless. **Any substitute for that address must
  return 0xFF on read**, or the write half puts an arbitrary byte in the
  video latch. KIT RULE: for every hardware address the census marks RMW,
  derive what the board returns on READ before choosing the substitute.

- **Check that the program never writes rom space, and record that you
  checked.** A rebasing port is only safe if nothing stores below the rom
  ceiling. Altered Beast: zero sites. The check is one pass over the
  stream and a clean result is still a kit rule.

## Hardware truth from jtcores (srcref/jtcores, GPL — derive, never copy)

S16B shares jts16_prio.v/jts16_colmix.v with S16A (jts16_video.v's
MODEL parameter only alters tilemap/obj internals). Facts derived:

- **Shadow is arithmetic** (jts16_colmix.v:80-88): shadowed pixel =
  each 5-bit channel `a - (a>>2)` (×0.75). NOT a palette pick. And
  **palette bit 15 exempts the color from shadowing** (`shadow &
  ~pal[15]`). Our indexed-color FB can't synthesize new colors →
  nearest-CRAM match must TARGET ×0.75 per channel (gap: flagged,
  closest 32X approximation) and must honor the bit-15 exemption.
- **Sprite pixel fields** (jts16_prio.v:52-64): [11:10] priority,
  [9:4] palette, [3:0] color. Palette 0x3F (`&obj[9:4]`) = shadow
  applied to the UNDERLYING tile pixel (confirms the MAME rule).
- **Per-layer sprite thresholds** (jts16_prio.v:83-88): sprite beats
  text iff pp==3; beats FG (scr1) iff pp>=2; beats BG (scr2) iff
  pp>=1 — consistent with the MAME level model.
- **Priority-tile punch-through** (jts16_prio.v:61): a priority tile
  still loses to the sprite where its color index bits [2:0]==0
  (indexes 0 AND 8 punch through, not just 0).
- **Layer opacity in the final mix** (jts16_prio.v:90-99): a tile
  pixel falls through to the next layer when [2:0]==0 (0 AND 8 are
  transparent); a selected sprite pixel is opaque when [3:0]!=0.
- **All-transparent fallback** (jts16_prio.v:87): the visible pixel is
  the BG (scr2) pixel's palette row with index bits [2:0] forced to 0
  — bit 3 of the index SURVIVES (row color 0 or 8), not a fixed
  backdrop color.

Cross-check ledger: verify m_main.c compose against the punch-through
and fallback rules (pp3/amb exactness work item); rebuild shadow_lut
to target ×0.75 + bit-15 exemption.

## ares savestate forensics (no scripting needed on the slowest target)

ares writes slot states next to the ROM (`rom/s16.bs1` = slot 1).
Format: `BST1` header; the 32X SDRAM image is stored RAW but 16-BIT
BYTESWAPPED, near the file head (this build: SDRAM[0] at file offset
0x23B — relocate by searching for a 64-byte .ramtext probe from the
ELF, swap16'd). Recipe: take `.ramtext` bytes from the ELF (objdump -h
for file offsets), swap16, find in the state file, subtract the VMA
offset → SDRAM base; then read DIAG/queue/any SDRAM region with
swap16+big-endian. Ask Mike to save a state MID-ARTIFACT; states are
only valid for the exact ROM build they were taken on. First use:
proved ares drops bands steadily in attract (DIAG[13]=864) while the
rotation fix keeps the read clean — measured the acceptance-gate
emulator's real operating point from a play-test session.

## Shipped-ROM archaeology (srcref/*.32x — Sega's own arcade ports)

Fast technique: scan the binary for 32-bit literals of 32X register/
window addresses (SH-2 code loads addresses from literal pools, so a
plain big-endian u32 scan maps the architecture in seconds — no
disassembly needed for the first pass; Ghidra for control flow when
required). Findings from Space Harrier / After Burner Complete /
T-MEK:
- ALL blit the framebuffer through the CACHED window (0x04000000:
  111-125 refs) not the uncached one (handful) — SH-2 write-through
  cache means cached-area stores ride the 4-deep write buffer instead
  of stalling the bus per word. Write-only paths only; reads keep
  uncached. STOLEN into blit_half.
- COMM barely used (1 literal each), DREQ FIFO not at all: their MD
  side is a stub — the whole game runs on SH-2s. Confirms our
  keep-the-68K-game-running architecture is the harder problem, and
  their MD-side patterns don't transfer; their SH-2 render-side
  disciplines do.

## Sprite-frame baking: precompute what the beam-racer re-derived — KIT-CORE

Every S16B title decodes its sprites the same way — a strip walk over
banked ROM, nibble pixels, a per-row pitch, a 0xF end marker — so this
is a kit technique, not an Altered Beast one. Only the ROM data and the
discovered key set are per-game.

**The key is what determines the PIXELS and nothing else**: sprite data
addr, the d2 word (pitch + flip), bank, and unclipped height. NOT the
colour set (it is a runtime `base` add, so leaving it out MERGES frames
that differ only by palette pair and raises the hit rate), not xpos, not
priority, not absolute top.

Pipeline, all three stages reusable (`tools/sprite_discover.*`,
`tools/bake_sprites.py`, the `SPR_BAKE` fast path):

1. **Discover against the ARCADE**, not the port: walk object RAM
   (0x440000 on S16B) over attract plus scripted play and log every
   unique key with its measured strip size. Coverage is never complete
   and does not need to be — an undiscovered frame simply stays on the
   live decoder. Budget from the measured sizes, never from estimates.
2. **Bake offline** into a copy-friendly blob + open-addressed index.
   **THE ACCURACY GATE RUNS BEFORE THE ROM EXISTS**: render every row
   twice — once through a port of the LIVE algorithm, once through the
   emitted record bytes — and fail the build on any difference.
   *Derive the live side INDEPENDENTLY.* Our first checker fed the same
   row list to both renderers, so it proved the format round-tripped and
   never proved the ADDRESSING; a one-row offset shipped straight
   through it and only ares caught it.
3. **Fast path on a hit, live decoder on a miss.** Only what the bake
   reproduces exactly qualifies: native zoom, ungated priority,
   non-darkening. Everything else falls through untouched.

Costs to weigh, both measured: pens stored as BYTES double the cart-bus
traffic versus one word per four pens live (pack nibbles if the bus is
the bottleneck), and the hash must mix LOW bits upward — one animation's
frames sit a few words apart and share d2 exactly, so a cheap 16x16 fold
clustered to 12 probes where a 32-bit multiply gives 4.

## Skip the empty parts of the blit — KIT-CORE

Once the background moves to the host VDP's tile plane, the composited
surface is mostly transparent and most of the blit is shipping nothing.
On 32X hardware ~80% of the blit is an FB-write bus-stall floor, so the
only lever is WRITING FEWER BYTES; an SDRAM read is ~5x cheaper than an
FB write, which puts the break-even at ~25% skippable.

Measure before building it. Instrument the blit to count, per 32-pixel
group and per whole row, how much is entirely transparent. On this port
after the BG moved to the MD plane: **62.7% of groups, 31.9% of rows,
79.4% of area**. Group granularity is worth roughly double row
granularity because the emptiness is SCATTERED — one test per 8 longs,
not per row and not per long (per-LONG branching costs more than the
store it saves).

**THE PART EVERY IMPLEMENTATION GETS WRONG: you page-flip.** A skipped
group does not keep last frame's pixels, it keeps the pixels from TWO
frames ago, IN THAT BANK. So the state is a per-BANK, per-row bitmask of
"this group is already zero here", maintained by the blit as it writes —
never by re-scanning, because scanning costs the win it measures. Note
that zeroing a group that just went empty is itself a write, so it must
be tracked, not assumed.

**And the bank identity must come from the flip protocol, never from the
hardware register.** Sniffing the display-select bit inside the blit is
a race: the flip sits between two blits, both CPUs blit around it, and
the second CPU can read the bit on the wrong side of an edge the first
CPU owns. Wrong half of the mask, needed writes skipped, stale content
left standing. The CPU that owns the flip should keep the parity itself,
toggle it at its own register write (the flip is committed at the write,
not at the latch), and PUBLISH it to the other CPU in the command word
that already sequences the window. Then nothing reads the register.
Initial parity need not match physical reality — it only has to toggle
when the hidden bank changes, and an all-zero mask makes the first cycle
write everything.

Design the mask so a lost or garbage entry costs SPEED, never PIXELS:
0 = "not known to be clear" = write it.

**Gate it with a readback verifier, not a pixel diff.** See the
diagnosis section below — this is the case that taught it.

## Ask the host hardware to draw it before you rasterize it — KIT-CORE

The instinct on a machine with a fast CPU and a slow one is to render in
software on the fast CPU. On a 32X the Mega Drive VDP is sitting right
there with tile planes AND a sprite chip, and every layer you hand it is
a layer that costs the SH-2s nothing — no compose, no staging buffer, no
blit, no bank coherency, no band pipeline.

On this port, moving just the BACKGROUND to the MD tile planes made
62.7% of framebuffer groups transparent and unlocked every optimisation
that followed. The measurement to run before assuming the same for
sprites is the host's PER-LINE budget, because that is what actually
binds:

  - Mega Drive H40: **20 sprites and 320 sprite-pixels per scanline**
    (80 sprites per frame, which is almost never the limit).
  - Count a sprite's FULL width against the line, transparent pixels
    included — that is how the VDP charges it.
  - Count separately the sprites that can never go to hardware at all.
    On System 16 that is the ZOOMED ones (S16 scales sprites; the MD
    VDP cannot), plus any that need per-pixel priority gating or read
    the destination (shadow/darkening).

`make SPRLINE=1` + `tools/sprline_probe.lua` reports all of it per
title. Altered Beast: count limit exceeded on 0.18% of lines, pixel
limit on 8.05%, worst 808 px against a 320 budget. **Read that as: a
hybrid works, a pure hardware path does not.** Hardware takes the common
case, the framebuffer takes the overflow and the zoomed sprites.

**This is a per-TITLE decision, not a kit-wide one.** Every S16B game
has the same architecture and wildly different sprite density; run the
probe against a candidate before choosing its renderer. Two percentages
decide it.

The counterweight to weigh at the same time: hardware layers need
hardware PALETTE. S16 has 128 colour sets against the MD's four 16-colour
lines, and the pen-packing that bridges them is the most fragile part of
this port. Every layer moved to the MD increases pressure on it.

## Raster palette swaps only pay if the demand stacks VERTICALLY — KIT-CORE

Rewriting CRAM mid-frame off a line interrupt is the standard Mega Drive
way to beat a 61-colour budget, and it is the first thing anyone reaches
for when an arcade source palette does not fit. Before building it,
measure whether this game's colour demand is separable by scanline.

Accumulate the set of source colour sets used per horizontal SPAN of the
screen (28 lines is a good granularity), then merge adjacent spans to
get the 4-swap and 2-swap answers from the same pass. Compare the worst
span occupancy against the palette lines you actually have.

On Altered Beast the answer was blunt: 11 distinct sets in a frame, and
**eight raster swaps brought the worst span down only to 8**. The reason
is the genre — a side-scrolling brawler puts every actor on the same
ground line, so the demand is spread ACROSS a scanline and vertical
partitioning cannot separate it. Raster swapping is the right tool for a
game whose pressure stacks vertically (sky gradients, status panels,
layered backdrops) and the wrong tool for one whose pressure is
horizontal.

**Per-title question, one probe run.** Do not inherit the answer.

And keep the two budgets apart, because they are different hardware:
  - **32X framebuffer CRAM: 256 entries.** Roomy. Put the layer with the
    most colour-set churn here — on S16 that is the SPRITES.
  - **Host VDP CRAM: four 16-colour lines**, minus whatever the text
    path reserves. Scarce. This is what limits any layer moved to host
    hardware, and it is the real reason a hardware-sprite path can fail
    even when the sprite GEOMETRY fits comfortably.

## A metric that will not move under speedups is an ASYMMETRY — KIT-CORE

When two processors share a render pipeline, the natural split is by
work: half the rows each. That is almost always wrong, because one of
them is also the system CPU — it owns the flip, the palette, the page
coherency, the map building, the queue. An even split of the SHARED work
is an uneven split of the TOTAL work.

The signature is unmistakable once you look for it: **a defect metric
that refuses to move while everything around it improves.** On this port
the band-drop rate sat at 0.95-0.97 per cycle across three large,
independent optimisations that took the CPU-time metric from 91 to 86.
Drops did not follow, because drops were never about speed.

Diagnose it by asking which side PUSHES work and which side PULLS it. If
the fast CPU's completion is what enqueues the next unit, and the slow
CPU's completion is what dequeues it, the queue fills and sheds by
construction, at a rate set by the imbalance and nothing else. No amount
of making both sides faster changes the ratio.

**And the artifact tells you which CPU is late**: the stale region is
the overloaded CPU's rows, so the visible seam sits exactly at the
boundary between the two CPUs' row ranges. Work backwards from where the
tear is to which processor is behind.

The fix is a tunable split, not more optimisation. Make the boundary a
build-time constant, sweep it, and read the defect metric — here moving
32 of 112 rows from master to slave removed 85% of the drops with
compose time flat.

## Hard-won invariants the kit must encode (see NOTES.md for full log)

- RV=1 forbids SH-2 cart access → all hot code in .ramtext, tile/
  sprite pixel reads only in-window or via SDRAM caches; no libgcc
  (variable shifts) in hot paths.
- FBCTL flips ONLY in early vblank, gated by the MD V-counter, with a
  fresh-for-this-window heartbeat — never a stored clock, never the
  32X VBLK bit.
- FB byte-write zero-drop → patch game byte-writers to word writes.
- Blit budget is set by the SLOWEST target (ares ≈ several × MAME);
  slice size must carry 2×+ vblank margin.
- One bank stages, the other displays; staging is never deselected
  while the game runs.
- A CHANNEL'S RATE MATTERS AS MUCH AS ITS COST PER WORD. The MD->SH-2
  channels are not interchangeable even after cost is equalised: the
  COMM stream ran on EVERY vint, while the DREQ packet is pushed only
  on GATE-ACCEPTED vints. Moving a payload from one to the other
  silently makes its delivery WINDOW-rate-limited, which is invisible
  at a healthy cadence and bites under load. Weigh rate, not just the
  lines/word ratio, before moving any per-vint payload onto the packet.
- PUSH THE LIVE LIST, NOT THE ALLOCATED ONE. The sprite list is 64
  records because the hardware allows 64; the GAME fills a mean of 12.5
  (max 21, measured over 2000 gameplay frames). Publishing
  84 + 8*live_records instead of a fixed 512-word list moved the 68K
  handler mean 83.8 -> 65.4 lines/vint. Any kit target should measure
  its own live-record distribution before sizing a transport.
- A PARTIAL DREQ LANDING IS READABLE ON REAL HARDWARE. `landed =
  armed - TCR0` reports the truth mid-transfer, so a transport can arm
  the MAX and let a short push speak — which is what makes both an
  optional payload and a variable-length list possible. **MAME does not
  model this**: it read 62 of 62 short pushes as zero where ares read
  233 of 234 at their true length (`tools/drq_probe.py`). Any kit
  transport built on partial landings is UNTESTABLE in MAME; verify it
  on hardware or ares, and do not read "renders wrong in MAME" as a bug.
- A DEFERRED PASS NEEDS A SUCCESSOR. Handing work to "the next call"
  through a single-slot record is only safe while a next call exists
  inside the same frame. When the window scheme changed to chain all
  bands back-to-back, the LAST band's deferred foreground layer never
  ran before its ship and the layer silently vanished from those rows.
  Whenever a scheduler is restructured, re-check every deferral for who
  drains it.
- PACKET SIZE IS ONLY MEANINGFUL AGAINST AVAILABLE 68K TIME. A packet
  that failed to drain at one tail length drained FINE at 75% larger
  once the tail was cut (dreq_incomplete 9.3% -> 5.8% while the text
  packet went 340 -> 596 words). Re-measure size limits after any
  change to handler load; do not inherit them.

## Before shaving code for RAM, ask what is actually in the RAM — KIT-CORE

A software renderer on a console with a fixed memory map will eventually
hit a region guard, and the reflex is to shave code: inline less, delete
a counter, drop a probe. On this port that reflex burned whole sessions
for tens of bytes at a time, and code size is not even linear in source
(removing a loop's trip counter GREW the section by 24 bytes).

Look at the symbol sizes first. Here one array — the staging buffer —
was **80,640 of the 102,400 bytes** under the guard, with all the code
fighting over the remaining fifth. One line took 2,688 bytes back, more
than every code shave in the project's history combined.

Staging buffers are usually oversized on purpose: a margin around the
visible area so scroll offsets and off-edge sprites can be drawn without
per-pixel clipping. **That margin is a guess made early and never
revisited.** Do not re-guess it — measure it:

  - Paint every margin byte with a **per-row signature**, not a constant.
    A constant is invisible against any code that happens to store that
    same value.
  - Run a real play pass: attract, gameplay, movement in every direction.
  - Read the margin back. Bytes still holding their signature were never
    written, and are yours.

Here that showed the VERTICAL margin (16 rows) completely untouched
while all 16 margin COLUMNS were fully written — because horizontal fine
scroll draws from a fractional column offset and a full tile row
overhangs the right edge, while every vertical writer was already
clipped to the visible range by construction. A guess would have taken
the wrong one.

Take the far side first. Trimming the END of the buffer needs no code
changes at all; trimming the START means re-basing every index in the
program, and the margin at the start is the one that catches an
off-by-one writing BEFORE the array.

## Emulator debugger reads: prove the address before trusting a zero — KIT-CORE

Counters parked at fixed addresses are the cheapest instrument in this
kind of work, and reading them back from the emulator's scripting API is
where they quietly fail. On SH-2 the same SDRAM is visible through a
cached alias (0x06......) and an uncached one (0x26......), and rom code
writes counters through the UNCACHED pointer so both CPUs see them.

**MAME's SH-2 debugger space serves only the cached alias.** A read at
0x26...... returns zero — not an error, not stale data, a clean zero
that is indistinguishable from "the counter never incremented". That
reads as a negative result and sends you looking for a bug in the
feature instead of the reader.

**So write a SENTINEL before believing any zero.** Have boot store a
recognisable pattern (0xA5A5A500 + index) into the counter block and
read it back through both aliases in one run. It costs one build and it
tells you three things at once: whether the address is right, whether
the init code runs, and which alias the debugger serves.

Do this the first time a counter block is added, not after a zero
surprises you.

## When a pixel diff cannot answer the question — KIT-CORE

A two-build frame diff is the reflex test for "did this change break
rendering". For anything that touches the render loop's TIMING it is not
an oracle at all, and on this port it wasted most of a session twice.

Three failure modes, in the order they bit:

1. **Frame-number anchoring diverges outright.** Scripted play captured
   at fixed frame numbers compared two builds that were by then in
   different GAME STATES — one life vs two, different enemies on screen,
   64% of pixels different. The inputs were identical; the emulated
   machine consumed them at slightly different moments. Any flag that
   moves CPU timing invalidates this instrument completely.
2. **Scene anchoring survives that, and still cannot separate corruption
   from PHASE.** Anchoring on exact game state fixes the divergence, but
   the layers that arrive through a pipeline (text, palette stream) land
   a frame differently when the render loop's cost changes, so frames
   differ for reasons that are not bugs. Adding settle frames does not
   fix it — on a scrolling scene it just anchors somewhere else.
3. **A "do everything except act on it" control can be worse than the
   thing it controls.** The never-take-the-skip build wrote its state
   word on every row instead of rarely; those extra uncached writes blew
   the blit's window budget and produced dropped bands that looked
   exactly like the corruption being hunted.

**What works: make the code check ITSELF against the hardware.** Where
the optimisation says "I can skip this because X is already true", read
X back from the actual device and count the times it was not. Zero over
millions of opportunities is a correctness result no frame diff can
give, and it does not care what the timing did. Here: every skipped
group read back UNCACHED from the framebuffer (the cached alias would
answer from our own write buffer), **0 lies in 2.3 million skips**
across attract and gameplay.

**Then falsify the verifier.** A checker that cannot fail proves
nothing. Deliberately reintroduce the bug the fix was for — one line,
collapse both banks onto one mask — and confirm it screams: **322,809
lies on the same run**. Only then is the zero worth anything.

Corollary: **check that your gate can even SEE the change.** The parity
statics could not see this one at all — in the shipping configuration
the background is still composited into the framebuffer, so no group is
ever empty and the skip fired **0 times in 2,011,300 groups**. The gate
passed on a code path that never ran.

## Diagnosis methodology (proven on the unpair burn-down + title hunt)

The debugging kit that found every bug so far, in escalation order:

1. **Arcade oracle first** — `tools/oracle_shots.lua` runs the real
   arcade with the same coin/start/input cadence and screenshots the
   same frame numbers. Diff before theorizing. CLEAN THE NVRAM first:
   persisted credits change the attract flow.
2. **Poison rig** — the dead low copy is 0xFF-filled; any un-rebased
   READ pointer fails loudly in MAME exactly like ares.
3. **wpcatch.lua** — watchpoint catcher (r/w, value logging, both
   machines, env-configured window/range). Diff writer/reader PC sets
   ours-vs-arcade at the same moment; identical sets mean the 68K is
   innocent and the bug is data or renderer-side.
4. **Pipeline-stage verification** — before touching code, verify
   each stage in order: 68K write stream → FB staging → SDRAM shadow
   → latched regs → compose. The title-art hunt burned three wrong
   theories because stages were assumed instead of checked; the
   actual bug (quadrant decode) was in the LAST stage.
5. **Trace diff** — MAME `trace` over a frame window on both
   machines, PC-set clusters name the diverging scene handler;
   first-divergence lockstep needs interrupt-aware alignment.
6. **RAM diff at matched moments** — dump work RAM both machines,
   filter for structured deltas (the +0x90 family). A delta that
   equals the rebase offset is a patcher false positive; check
   rebase_report.txt for the offending pass.

Patcher lessons the kit must encode:
- Harvested-value passes need a VALUE BLACKLIST for byte-collision
  families (0x10000, 0x102/4/6-style); call-time normalization thunks
  make dropping them strictly safe.
- Ascending longs can be a REAL pointer table (0x1989E) or a forged
  one from a byte ramp (0x1AD10 easing curve). Ascending alone proves
  nothing — check what the values point AT.
- Layer-decode conventions (page quadrant nibbles, scroll sign, alt
  register set, rowscroll) must be lifted from the MAME driver
  SOURCE, not inferred from working scenes: a wrong quadrant decode
  hid behind scrolling scenes for weeks because they keep both map
  halves loaded.

## Workflow (agreed with Mike, 2026-07-29)

Interactive sessions for architecture/new-ground (renderer refactors,
sound bring-up, first bring-up of each new title) — ares verdicts and
judgment calls gate these. Autonomous loops for mechanical grinds
with objective pass/fail (per-round parity sweeps against the oracle,
soak tests, bisects). Ares play-testing remains the outer acceptance
gate for everything.

## Roadmap to "kit" status

Reuse audit, 2026-08-17 (LOC counted, hardcode density measured):
roughly 75% of the ~9.4k core lines carries to title #2 untouched. The
per-title residue concentrates in exactly three buckets — the
patcher's hand-derived tables, the MCU/IO personality, and the parity
scene anchors. Everything else is BOARD-level or 32X-level.

| Component | LOC | Reuse | Per-title residue |
|---|---|---|---|
| `sh_src/` renderer (`m_main.c`, `mars_start.s`, `s_main.c`) | 6055 | ~95% | 3 extern symbol names; sprite bank mask `&7` (`m_main.c:1563`, assumes 1MB sprite ROM); `MD_TILE_MAX`; 16384-tile assumption |
| `md_src/` shim | 1821 | ~80% | MCU personality (~60 lines), input map, DIP defaults, sound mailbox |
| `tools/patch_game.py` | 981 | ~60% | 117 lines of hex tables + 22 hand `bfix` sites + the `remap()` address map |
| `bake_sprites.py` + `sprite_discover.*` | ~950 | ~100% | none |
| `gen_tiles.py`/`gen_sprites.py`/`decode_gfx.py` | 52 | mechanism 100% | every ROM filename (retire via listxml) |
| Verify rig (`parity_cap.lua`, `play_32x.lua`, `state_health.py`) | ~900 | harness generic | scene anchors + input script (inherently per-title) |

The renderer is the good news: it implements the BOARD, not the game.
Its ten `altbeast` mentions are all symbol names for the two ROM
blobs.

**The sound lane (added 2026-09-01, docs/sound/SOUND.md P0-P4) is a fourth kit
column, built title-blind by construction.** The per-title work is a
pipeline RERUN, not code:

| Component | Role | Per-title residue |
|---|---|---|
| `sndtest/z80/player.asm` + 68K router/feeder + SH-2 PWM pool | fixed kit: ring player, command dispatch, speech lane | none — consumes generated data |
| `tools/snd_tap.lua` -> `cmd_sweep.lua` | oracle tap + per-command sweep of the arcade sound board | driver name, latch address, command range |
| `tools/opm2opn.py` + `soundmap_build.py` | OPM->OPN transcode + command-map/track/speech generation | none (S16B boards share YM2151+uPD7759) |
| `tools/upd7759_decode.py` + `speech_bake.py` | speech ROM -> PWM bank | sample-ROM filenames |
| `sndtest.32x` menu | per-title soundboard acceptance rig | landmark commands for the README |

**Sound is DECODED from the ROM, not tapped (2026-09-01 decision).**
The faithful, library-general path is to decode the driver's sequence
data straight from the Z80 sound ROM and the uPD7759 sample ROM —
tapping playback is a fallback, and the isolation-sweep variant of it
actively corrupts music (below). New kit tools:
- `tools/z80dis.py` — dependency-free Z80 disassembler (pure decode).
- `tools/snd_seq_decode.py` — S16B sequence decoder; entry point is the
  master song table (Altered Beast: `word[$03B4 + 2*(cmd&$7F)]`).
- `tools/pcm_from_rom.py` — direct uPD7759 ADPCM decode of the sample
  ROM (derive the algorithm from jtcores `jt7759`).
- `docs/sound/SOUND_DRIVER.md` — the reverse-engineered driver map (I/O ports,
  command table, song-header format, interpreter), with ROM citations.
- `tools/music_sweep.lua` + `tools/build_music.py` — capture every music
  command full-length from the arcade oracle in one MAME run, transcode,
  LZSS-compress, emit `render_music.h`.
- `tools/lzss.py` + md-side `lz_next` — LZSS codec; music opcode streams
  compress ~4x, so full-length tracks fit the 32X 512KB 68K ROM window
  with NO banking. The 68K stream-decompresses into the Z80 ring (4KB
  circular window). Compression beat the size limit — do NOT reach for
  banking when the data is this repetitive.
- **THE FADE LAW** (docs/sound/SOUND_DRIVER.md): sound-command bytes 0x01-0x40 are
  MASTER-VOLUME commands; the sound latch is re-read every IRQ, so the
  value it returns WHEN IDLE becomes the volume every tick. Idle must be
  a no-op (0x80), never 0x01 — that is "volume 1" and fades the music.

BIG reuse win: the S16B sound driver ROM (`epr-11671`) and samples are
SHARED across titles — English and Japanese Altered Beast use the exact
same sound ROMs (MAME segas16b.cpp:39-45), so the decoder built for one
works on the other with zero changes. Per-title differences are
relocated table addresses → `games/<title>.toml`, code stays fixed.

**Capture-method invariant (2026-09-01, the envelope-fade diagnosis):
if you MUST tap instead of decode, streaming MUSIC must be captured from
CONTINUOUS play; the per-command isolation sweep is only valid for the
command map and one-shots.**
`cmd_sweep.lua` soft-resets the sound chip and injects one command with
reads gagged — perfect for classifying commands and for short sfx/
speech (one attack is the whole sound), but it severs the arcade
driver's continuously-running volume/envelope engine. Result on a
looping track: notes key on and change pitch, but the per-note TL/
envelope REFRESH writes never happen, so every voice decays to silence
after its attack ("fades after a couple bars"). Measured on Altered
Beast 0x94: TL and SL/RR registers got 1-2 writes across a 14s track
from the sweep vs 43-158 each from a continuous-play tap. The dedup is
NOT the culprit (it drops only immediate repeats; the arcade's
refreshes are value-varying and survive). Kit rule: source SND_MUSIC
bytes from the continuous-tap transcode path (opm2opn on a gameplay
tap), reserve the sweep for classification + one-shots.

Pipeline QA fixtures that earned their place in debugging (2026-09-01,
the sputter hunt): (1) body write-rate census — compare per-second
YM register-band write rates of a regenerated stream against a
known-good one; a band dropping ~10x means the dedup/snapshot contract
broke; (2) snapshot-liveness check — an opening state snapshot that is
a uniform post-reset template (all TL 7F, SL/RR FF) instead of varied
live patch state is a capture-isolation smell; (3) offline player
simulation (walk the opcode stream in python: duration, keyons,
carrier TLs, opcode alignment) catches structural rot without any
emulator. All three are cheap scripts against `sndmap_data.h` alone.
And the standing lesson: the acceptance instrument for transcode
fidelity is EARS against `mame altbeast`, not band-energy metrics —
measured energy called a broken-envelope stream "music" once.

**Stage 0 — finish Altered Beast.** IN PROGRESS AND KEEPS PRIORITY.
Accuracy, speed, sound; "works" -> arcade-playable is the long pole
and the reference implementation is worth nothing until it clears it.
Stages 1 and 2 were scoped deliberately so they touch NO code on the
shipping path — the asset converters, and a patcher that has already
emitted AB's binary and now sits idle — so neither can regress the
parity gates while stage 0 grinds.

**Stage 1 — asset extraction becomes automatic (~5 hours).**
  1a. `tools/rom_manifest.py`: `mame -listxml <driver>` -> planes,
      sprite pairs, bank count, board type, MCU presence, geometry,
      into `games/<title>.toml`. See "MAME is the asset config
      source" for the decode rules.
  1b. Parameterize `gen_tiles.py`/`gen_sprites.py` off that toml.
  1c. Emit `sh_src/game_cfg.h` from it and retire the two size
      assumptions baked into `m_main.c` (bank mask, tile count).
  Sprite baking needs no work; it is already title-blind.

**Stage 2 — the 68K side becomes declarative (~2 days).**
  2a. Auto-decode the memory map from the game's OWN mapper table.
      AB's was hand-derived from ROM 0x1986 (NOTES.md "DECODED:
      altbeast 68K memory map"). Generic form: scan the program ROM
      for a 16-byte run that decodes through the 315-5195 rules into
      a plausible region set (valid size codes; must contain work
      RAM + palette + tile RAM). This retires the single largest
      hand-derivation in the port.
  2b. Split `patch_game.py` into engine + `games/<title>.toml`. The
      scanner, A-site confidence rules, TAS opcode scan, and thunk
      emitter are already game-agnostic; only the tables move out.
  2c. Wrap the write-tap census (`pal_tap.lua` already takes ranges
      by env var) so "census a hardware region -> candidate dirty-site
      table" is one command. It is step (a) of every new title.

**Stage 3 — prove repeatability on a second title.** The point is not
that a second title runs; it is that the COST is known. Walk a fixed
checkpoint ladder and RECORD HOURS AT EACH RUNG — that number is the
kit's entire value proposition, and it is the only honest answer to
"is this repeatable":
  1. manifest + assets build from an unpacked ROM set
  2. patched binary links and the ROM boots into the MD shim
  3. first attract frame composes recognisably
  4. parity statics land under the title/eyehold-equivalent bar
  5. Mike's ares play pass
Find the title's discriminator scene at rung 3, not later — see
"Geometry-convention rule".

**Encryption gates the title choice** (surveyed 2026-08-17, `-listxml`
grep for `317-*.key`). FD1094 decrypts opcodes as a function of CPU
STATE, so those sets can never yield one flat patchable binary — they
are out of scope for a static patcher, permanently.
  - Clean (no key ROM): `shinobi5` (S16B, and NO MCU — cheapest
    possible second title, skips the whole MCU bucket), `goldnaxe`
    (i8751, same shape as AB), `tturf`, `wb3`, `aliensyn`.
  - Blocked: `goldnaxe3`, `shinobi2`, `eswat`, `passsht` (FD1094);
    `dunkshot` (FD1089); `altbeast2` (317-0066 key).
  - Only `altbeast.zip` is in `./mame`; title #2 needs the dump on
    disk before any of stage 3 can start.

Per-title work the write-observer pattern implies (do it in this
order, it is the cheapest path):
  a. Run the write-tap census on every mirrored hardware range BEFORE
     writing any thunk table — it yields the site list, the real
     extents, and the indirect writers a static scan cannot see.
  b. Derive each site's mask from its loop bound; runtime masks only
     where the disassembly genuinely cannot say.
  c. Size the delivery unit against how much the game actually changes
     per frame, not against what is convenient to address. Altered
     Beast needed a PAIR of 128-word regions per push because its
     colour-cycling sets are rewritten every vint.

## Geometry-convention rule (learned the hard way, 2026-07-30)

Every scroll/offset/sign convention lifted into a port MUST be pinned
against a discriminator scene: art that is UNIQUE and ASYMMETRIC at a
known register value. Periodic content (tiled walls, repeating props)
and symmetric register values (xs=0xC0 -> eff=0) validate BOTH signs
of a conversion — the Altered Beast X-scroll sign was inverted for
the project's entire life while passing oracle screenshot comparisons.
The attract transformation-scream screen (unique full-bleed art,
xs=0) was the first true discriminator. Per title, find such a scene
EARLY and regression-pin it.


## Second title: altbeastj (Juuouki, set 7) — 2026-09-05

`make GAME=altbeastj ...` builds rom/s16_altbeastj.32x from roms/altbeastj.
Milestone 1 (DONE): the pipeline is per-game — GAME reaches both CPUs
as -DGAME_<NAME> and .build_flags; the generators take per-game file
lists; the patcher reads roms/<GAME>, derives GAME_IRQ4 from the vector
table into md_src/game_irq.h (md_start.s uses it), and loads table
overrides from tools/game_<GAME>.py. Facts established:
- Graphics: the Japanese tile and sprite ROMs are the US BYTES in a
  different split (verified equal). MAME's layout puts each tile plane's
  second half at +0x20000 and each sprite pair 0x40000 apart, so the SH-2
  folds codes/banks under GAME_ALTBEASTJ (GAME_TILE_REMAP, GAME_SPR_BANK)
  onto the US images. No new art, no cart growth.
- MCU: 317-0077 vs 317-0078 differ in 867 of 4096 bytes. THEY DO NOT use
  the same mailboxes: US busy/coins/sound = 0xFFF0C0/C2/C4, JP =
  0xFFF0D2/D0/D4 (different order). Per-game keys MCU_BUSY/MCU_COINS/
  MCU_SND; the shim reads them from game_irq.h. (The earlier "same 14
  addresses" claim was wrong and cost Mike a dead pad on ares.)
- Program: 48% of words differ, with shifts; the IRQ4 handler is 0x2ACA
  (US 0x2AAC). The patcher's hand tables (DATA_EXCLUDE, REBASE_TABLES,
  TAS_SITES, TILE_DIRTY_SITES, PAL_DIRTY_SITES, FMGATE_ENTRIES/SPANS,
  REBASE_EXCLUDE) and its expect() site checks are US offsets.
Milestone 2 (DONE 2026-09-05): the patcher is game-agnostic. Every
program-specific offset lives in tools/game_<name>.py TABLES (29 keys;
tools/game_altbeast.py holds the hand-derived US set; the US outputs
are byte-identical to the pre-refactor patcher). The Japanese set was
DERIVED, not hand-scanned: tools/game_align.py aligns the two objdump
listings by instruction shape (98.8% of the US program has a one-to-one
counterpart, mostly at +0x18) and tools/game_derive.py translates every
table entry through the map and verifies it against the target bytes the
way the patcher will (opcode, operand class, table content); the idiom
lists (boot-region pc-relative fixups, jump-ins into the boot region)
are re-scanned from the target listing after the scanner is required to
reproduce the US hand list exactly. Result: 29/29 keys, every code site
verified; the derivation report is the module's docstring. Also fixed:
gen_sprites.py assumed 128KB ROM pairs (the JP set has 64KB pairs); the
SH-2 folds GAME_SPR_BANK/GAME_TILE_REMAP now also cover the sprbake
lookup, the MDSPR key match and the MD-plane residency key (they were
only on the FB paths).
`make ship` builds both titles: rom/s16.32x (US, the accepted nocat1
flag line) and rom/s16_altbeastj.32x (JP, same line).
Milestone 3 (IN PROGRESS): the JP rom boots and its 68K side tracks the
JP arcade frame-for-frame in MAME (state byte 0xFFF031 transitions at
the same frames, layer regs, text name-table and tile-page census equal
at 600/1200/2400). PIXELS CANNOT BE JUDGED IN MAME FOR ANY NATIVE BUILD
(docs/design/BOSSFIGHT.md: MAME renders the MD-plane scheme as confetti — the US
canonical rom is confetti there too). A pixel gate needs either a
pre-NATIVE flag line built for MAME, or Mike's ares pass.
The "kit-baseline" flag subset (canon minus SPRBAKE TILECLASS TXTCLASS
MDSPR PALSTATIC PALGLOW PENMATCH) is NOT a working build: the US program
under it is confetti in MAME exactly like the full line, so it proves
nothing either way; treat it as untested, not as a baseline.
