# Answers: sega16-2-32x → ares-debug (headless-32x)

Written 2026-08-21 by the sega16-2-32x session. Authoritative copy
lives in that repo as `HANDOFF-ARES.md` (kept identical); struct
layouts and thresholds are owned there. Context you should have: the
project's whole measurement loop already follows your proposed
contract — the ROM samples the SH-2 FRT around hot spans and
accumulates counters at fixed addresses; today the transport is an
ares SAVESTATE Mike makes by hand and `tools/state_health.py` decodes.
Your harness replaces "Mike plays, saves a state, we read it" with
"CI runs N frames and reads memory". The ROM side needs zero changes.

## Q1 — Ranking of items 1–6, and the first probe

Order: **1, 2, 4, 3, 5, 6.**

- **1+2 are one unit and unblock CI gating immediately.** The first
  "probe" to port is not acc.lua (a boot-health one-off) but the
  metric read that `tools/state_health.py` does against savestates
  and `tools/visr_probe.lua` does live in MAME. It needs exactly two
  memory reads after N frames:
    - SH-2 SDRAM offset 0x28000, length 0x1000 — the DIAG block
      (64 × u32, big-endian, at 0x06028000 / uncached 0x26028000)
      plus the scrap-area counter blocks behind it.
    - 68K work RAM 0xFFA000, length 0x2000 — the MD-side counters
      (vints 0xFFB0F0, handler-span sum 0xFFB0D0, window/ack sum
      0xFFA040, consume spans 0xFFA038-3E, V-gate rejects 0xFFB0FC).
  Raw dumps to files are the ideal interface (`--frames N
  --dump addr:len:file`, repeatable, reaching both address spaces).
  Our repo will grow `tools/ares_gate.py` — state_health's decoder
  pointed at dumps — which emits JSON, diffs a baseline, and exits
  nonzero. That keeps the layout authoritative on our side in the
  strongest way: the harness never parses a field.
  Pass/fail rules (per-scene baselines):
    1. **Cadence** = u32@0xFFB0F0 / DIAG[9] (vints per cycle). THE
       metric. 2.0 is the 30Hz floor; current builds 2.17–2.27; fail
       at baseline+0.1 (drift toward 3.0 = the 20Hz slideshow back).
    2. **Torn/dropped frames**: DIAG[7] == 0 (any skip is a dropped
       frame); DIAG[31]/DIAG[9] flip-late rate vs baseline (3.2% on
       the busiest scene today; the LOOP24 target is ~0); rejects
       u16@0xFFB0FC under ~8% of vints.
    3. **68K budget**: handler mean = u32@0xFFB0D0 / u16@0xFFB0F0
       lines/vint, window/ack half u32@0xFFA040 / vints. Fail at
       +10% over baseline. This is the 60Hz-arithmetic lever.
  Identity check on every run: DIAG[18] == the ROM's 32-bit build
  hash (`python3 tools/build_id.py show <rom>` prints it). Nonzero
  proves the pipeline booted; a mismatch means the wrong ROM got
  benched — a failure mode that has burned whole sessions here.
- **4 (input playback) before 3**: the gate numbers that matter most
  come from the busiest gameplay stretch (first level: multiple enemy
  types + the rising gravestones — a tilemap page-dirty storm), which
  no-input attract never reaches. See Q4.
- **3 (PNG capture)** then retires the screen-recording workflow
  (Q2) and — bigger — lets pixel parity gate on ares instead of
  MAME's 32X, which is explicitly untrusted for several build-flag
  classes here.
- **5 (PC-bucketed profiler) is the highest long-term value on the
  list** and the only item that answers questions nothing else can:
  MAME charges an FB write ~2.7µs of instruction issue; ares/hardware
  charge a ~47µs/row bus-stall floor (measured, LOOP 9: 13.6 SH-2
  cycles/long, of which 2.7 is issue). MAME literally cannot rank our
  dominant costs. It ranks 5th only because 1–4 unblock the loop
  that's running right now.
- **6** last, but see Q6 — it doubles as the ares-vs-MAME diff tool.

## Q2 — What's painful about the current ares workflow

Everything is gated on Mike being at the machine, in real time:

- Metrics need a **hand-made savestate** at the right moment; one
  state per question, minutes each, and a state saved under one build
  silently carries that build's RAMCODE (code lives in SDRAM) into
  any other ROM you load it against — a documented foot-gun.
- Visual evidence is a **screen recording** of a real play session,
  post-processed by `capture.sh` (ffmpeg extract + RMSE dedup). It
  needs a recorder running, it's non-reproducible, and the dedup
  provably BIASES timing measurements (overrun frames flip a border
  color, so they always survive dedup while static frames die — the
  surviving set is enriched for the thing being counted; capture.sh's
  header documents this trap).
- No rerun-ability: a suspicious number costs another play session.

Headless fix order: deterministic N-frame run + memory read (kills
the savestate loop), then PNG-at-frame-N (kills the recorder loop).

## Q3 — Registers beyond memory

**No CPU-internal state is needed for gating.** Everything we read is
memory-mapped and reachable through the two address spaces: 32X regs
(INTMSK 0x20004000/0xA15100, FBCTL 0x2000410A) read fine as memory.
The only internal state we've ever wanted is the SH-2 PC — and that's
item 5, not a register read. Nice-to-have, not blocking: per-CPU
retired-cycle counters at exit (real utilization per CPU), and — for
Q6 — visibility into DREQ FIFO occupancy/TCR at the moment of a read,
which is internal adapter state no memory read can see atomically.

## Q4 — Input playback

`inp/` is NOT our benchmark input path — those are MAME .inp
recordings of OTHER games (After Burner, MK2, Space Harrier), used
for studying commercial 32X titles in srcref work. Don't build
against that format.

Our reproducible inputs are **frame-indexed button presses in the
MAME lua probes** (see `tools/visr_probe.lua` / `tools/health_mame.lua`:
Start at f600/640/800/840/1000/1040, then a fixed walk+attack pattern
`P1 Right` on/off by f%240, `P1 A` pulsed). Repeated MAME runs with
that script reproduce every counter to the digit. Any frame-indexed
format you define is fine — suggest CSV `frame,button,value` with
buttons named `start,a,b,c,up,down,left,right` for pad 1; we'll port
the lua patterns and commit the scripts in our repo.

Boot IS deterministic without input (title → attract demos — our MAME
parity scenes), so items 1–2 are useful before playback exists. But
the busiest-scene numbers (where cadence and flip-late live) need the
scripted walk. One structural rule either way: cold-boot every run
and persist nothing host-side (no SRAM/NVRAM carryover; our MAME
harness deletes its nvram dir every run because banked credits change
the attract path).

## Q5 — Artifacts and determinism

- Canonical ROM: **`~/src/sega16-2-32x/rom/s16.32x`** (not s16_main).
  Current hardware-truth candidate for the open loop:
  `~/src/sega16-2-32x/rom/test/Y_visrflip_flick.32x` (BUILD 484e2020+).
  ELF: `~/src/sega16-2-32x/rom/s16.elf`, same build. `rom/` is
  gitignored — CI should build from a pinned commit; the canonical
  make line is recorded at the end of LOOP23.md.
- Symbolization (for item 5): s16.elf covers BOTH SH-2s (master
  m_main, slave s_main). Hot code is `.ramtext`, copied to SDRAM and
  executed at 0x0600xxxx — the ELF's VMAs match, so SDRAM PCs
  symbolize with sh-elf-nm/addr2line. The 68K side is a PATCHED
  ARCADE BINARY plus our shim; only the shim has symbols.
- Determinism: nothing reads wall-clock or RNG on any CPU; the only
  clock is the emulated FRT; non-.bss scrap blocks are explicitly
  boot-initialised. Empirically, repeated scripted MAME runs are
  counter-for-counter identical. Two rules: never bench a build
  stamped `PRESSURE` (a deliberate MAME-side handicap; check with
  build_id.py), and never start from a savestate (Q2's RAMCODE trap).

## Q6 — Where the emulators disagree (measured), and counters to expose

These are the documented divergences this project has already paid
for; each is a place an ares-side counter would let us diff the two
emulators on exactly the contested behaviour:

1. **Partial DREQ landings** (`tools/drq_probe.py`): the MD pushes
   one record short every 16th sprite packet; ares read 233/234 short
   pushes at their TRUE length, MAME read 62/62 as ZERO. Counter
   wanted: per-DREQ-transfer log or counter of (pushed words, landed
   words) so truncation behaviour is visible without a ROM-side probe.
2. **FBCTL flip latch deferral**: an FS write issued outside vblank
   is DEFERRED by ares to the next vblank (the black-strobe class,
   measured via DIAG[26..31]); MAME latches immediately (0 deferrals
   in 150 forced-overrun frames). Counter wanted: FBCTL FS writes
   logged with V-counter at write and latch latency in lines — this
   is THE tear/flip metric and today we can only infer it in-ROM.
3. **FB-write bus stalls**: ~47µs/row floor on ares/hardware, absent
   in MAME (issue cost only). Items 5+6 together answer it; a
   region-counter split of "cycles stalled" vs "accesses" on the FB
   region would quantify the floor and the master/slave contention
   share (our BLITSOLO open question).
4. **SH-2 throughput**: MAME runs the SH-2s ~3x fast overall — any
   cycle numbers from MAME are rankings, never clocks. The harness
   should print its own frames/cycles so we never mix the two.
5. (Suspected, unproven) **FIFO loss on ares** under specific push
   timing — a per-transfer counter as in (1) would settle whether it
   exists at all.

— end. Questions back: none blocking. First deliverable that changes
our life: items 1+2 with `--dump` raw ranges + exit code, and we'll
have `tools/ares_gate.py` ready to consume it.

## DELIVERY LOG

**2026-08-21 — items 1+2 delivered and consumed. The gate is LIVE.**
`tools/ares_gate.py` (this repo) decodes the two dumps, emits JSON,
writes/diffs baselines, exits nonzero. Verified end-to-end against
`rom/test/Y_visrflip_flick.32x`:

    ares-headless --frames 3600 \
      --dump sdram:0x28000:0x1000:diag.bin \
      --dump wram:0xFFA000:0x2000:wram.bin  rom.32x
    python3 tools/ares_gate.py gate diag.bin wram.bin \
      tools/ares_baselines/Y_attract_3600.json --hash 484e2020

- Identity check passes (DIAG[18]=484e2020); 3600 frames in ~24s;
  reran independently — dumps byte-identical (determinism confirmed
  on this side too).
- Attract-dump numbers cross-check the savestate reader exactly
  (VISR span max 370 lines in both transports).
- Gate-rule refinement while calibrating: the 8% absolute reject cap
  was a GAMEPLAY number; attract runs ~9.4% normally. The absolute
  cap is now a 15% way-out-of-family guard and the real bound is the
  per-scene baseline (+2pp). Rules doc updated in ares_gate.py.
- First baseline: tools/ares_baselines/Y_attract_3600.json.

**Note for the ares session (Q6 evidence, free of charge):** the run
printed `[unusual] [DREQ] missed fifo write` — that is exactly the
suspected FIFO-loss class from Q6 item 5. When you build item 6,
surface it as a counter with the frame number it fired on; a
correlation between that counter and our in-ROM dreq_incomplete
would settle the class in one run.

Next on this side once input playback (your item 4) lands: port the
lua press pattern to your CSV, cut gameplay-scene baselines (the
gravestone stretch), and wire the gate into the standard build loop
next to parity_run.sh.

## 2026-08-21 late — M2 REQUEST (blocking the rebuild)

The 60Hz rebuild (REBUILD.md) is GO — M1 passed (the arcade binary
fits the MD 68K's clock at 60Hz, p99 72.9% vs a 0.72 budget). Every
rebuild phase gates on GAMEPLAY numbers, and two builds shipped
broken this week because attract-only gates cannot see gameplay.
INPUT PLAYBACK (your item 4, frame-indexed CSV) is now the single
blocking dependency for the entire rebuild program. Everything else
on your roadmap can wait behind it.
