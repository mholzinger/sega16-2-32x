# LOOP 7 — Kill the tail: COMM → DREQ, and retire the palette scan

Kickoff doc for a FRESH session. Self-contained: read this + LOOP.md,
then run. LOOP 6 did not ship a cadence win — it found *why* six
iterations of cadence work failed, and built the instruments that make
this arc measurable. Read the "THE BAND'S ORIGIN" section of LOOP.md
before touching code.

## Mission (one sentence)

Move every COMM stream payload onto the DREQ DMA packet and replace the
palette dirty-scan with write-thunks, cutting the 68K's per-vint
handler from ~241 to ~136 of 262 scanlines — the budget a 3-vints/cycle
cadence needs.

## Why this, why now (the falsified-down chain)

The V-gate reject band has sat at 57-66% through six iterations. LOOP 6
found the cause, and it is not a bug to revert — the band was BUILT.
Old savestates in `rom/` date it exactly:

| build     | vints/cycle | V-gate rejects |
|-----------|-------------|----------------|
| a4b51d0   | 3.02        | 0.4%           |
| 8b4ecc2   | 3.10        | 3.1%           |
| ~7215209  | 8.78        | 65.9%          |
| LOOP 6    | 6.99        | 57.1%          |

Three commits in that window each moved a data channel INTO the 68K
vint handler, and their costs are exactly the measured tail:

    180de61 palette MD-RAM mirror + rotating copy -> palscan 45 lines
    24b799a palette over COMM                     -> stream  70 lines
    6466663 sprite list over DREQ FIFO            -> dreq    49 lines
                                                     total  164 lines

against a measured MEAN handler of 170 of 262. Each was individually
correct (the FB cannot carry that data — ares discards MD FB-window
writes, savestate-proven). Nothing to revert. But cumulatively they
turned a 3.0-vints/cycle machine into a 7.0 one. That is the whole
field report: ~8.5Hz is "too slow", and at 57% rejects the two-vblank
ship frequently lands only ONE half of the frame, which is "flashing".
The anti-flash V-gate (bab5f74, 9fbb03e) is NOT broken — its rejecting
is the symptom.

**THE RATIO THAT DECIDES THIS ARC:**

    COMM  70 lines for  ~55 words = 1.27  lines/word
    DREQ  49 lines for  772 words = 0.063 lines/word

COMM costs **20x more per word**, because its cost is an ACK ROUND-TRIP
per 5-word batch, not the payload. The 68K spins on COMM0 waiting for
the SLAVE to service it, and that wait is ELASTIC — it expands to
absorb any 68K time freed elsewhere, which is why every previous
iteration's savings vanished. LOOP 6 proved this both ways on ares:

- `PROBE_spin0` (never block on the slave): rejects 57.1 -> **39.4%**,
  vints/cycle 6.99 -> 4.93, tail 177 -> 120, frame margin 21 -> 80.
- `PROBE_spin64` (cap the wait at 64 polls): **no change at all.**
  So it is "wait at all", not "wait too long".

spin0 is not shippable as-is (it starves text/palette). This arc gets
the same win legitimately, by moving the payload to the channel that
does not block on the slave.

## Starting state

- HEAD: `5e5a601` (branch `unpair-rebase`). Stamped `normal`, gates pass.
- ares (state_health, 1152c7d1 == same code): vints/cycle 6.99, rejects
  57.1%, blit skips 0.6%, dreq_incomplete 59, worst handler total 241 /
  window 64 / tail 177, margin 21 lines, preempt-blit timeouts 0.
- MAME scoreboard (clean, probe-free): title 49.31 / scream 91.68 /
  eyehold 3.37 / demo 52.13 / demo2 23.39, total 43.98. NOTE demo2:
  the LOOP 5 baseline reads 20.86 and this build reads 23.39 — that is
  the anchor's BIMODAL flip, not a regression. The bounded-wait fix
  nudged timing enough to land the other attractor; the diff images are
  structurally identical (same clouds/wolves/sprite mismatches) and
  differ only in the ALTERED BEAST logo, whose palette-cycle glow
  script is one frame out of phase. Verify the same way before calling
  any demo2 move a regression.
- MAME tail (make TAILPROBE=1): mean total 170, stream 70, dreq 49,
  palscan 45; max total 224 / window 18 / tail 206.

## The design

**Step 1 — COMM payloads onto the DREQ packet (the 20x win).**
The packet is currently 772 words = 512 sprite + 1 bitmap + 1 text base
+ 256 text + 2 pad. Add:
- layer regs `0x740-0x753` (20 words) — every vint, must be fresh
- rowscroll `0x7C0-0x7FB` (60 words) — every vint, must be fresh
- palette, rotating chunk (~128 words/vint) + its base word

New packet ~980 words (KEEP IT A MULTIPLE OF 4). The DREQ push grows
~49 -> ~66 lines but the ~70-line COMM stream disappears. The SH-2
applies these from SPR_LAND alongside the existing text chunk.
Projected tail 164 -> ~115, close to spin0's measured 120 but WITH the
palette intact.

Keep a minimal COMM path (or none) — if COMM survives at all it must
never be the thing the 68K blocks on.

**Step 2 — palette write-thunks (retire the 45-line scan).**
The scan is 1024 unavoidable MD-RAM reads/vint (512 mirror + 512
sent-copy) and CANNOT be micro-optimised (see negatives 3 and 4 below).
It must stop existing. Same mechanism as the LOOP 3c tile dirty-bit
thunks, which already work: the palette is already remapped to the
0xFF9000 mirror by `patch_game.py`, so the write sites are enumerable
the same way (address-formation sites feeding ordinary stores — exactly
what the existing thunk machinery handles). A dirty bitmap replaces the
whole diff. Ships in the DREQ tail like the tile bitmap already does.

Do Step 1 first: it is most of the win, independently testable, and
does not require touching the patcher.

## HARD-WON LESSONS (do not re-learn these the hard way)

- **DMA packet MUST be a multiple of 4 words.** The DREQ FIFO drains in
  4-word bursts; a non-multiple leaves the tail un-drained -> TE never
  sets -> stale frame. LOOP 5 hit this at 770; fixed at 772.
- **The DMA drains ONE transfer then stops.** Re-arm every window
  (dreq_rearm) and push ONLY on gate-accepted vints (window_ok). An
  off-cycle push into an undrained FIFO BLOCKS the 68K on its
  unconditional group-write — a hard mutual deadlock. This bit TWICE.
- **`dreq_incomplete` is already 59 on ares.** A bigger packet carries
  more payload per failure. If it climbs, SPLIT the packet rather than
  grow it.
- **MAME cannot see the band** (rejects ~0%, 3x faster SH-2) — but it
  CAN see the MD tail. `make TAILPROBE=1` + `tools/win_probe.lua`. Use
  the MEAN (total/stream/dreq/palscan), not the max: the max is spiky
  and did not move under a change that cut real work 6x.
- **PROBES MOVE THE SCOREBOARD.** Instrumentation in the vint path
  shifts V-gate outcomes and changes which frames ship (demo 52.1 ->
  54.6, demo2 20.9 -> 23.4 from probes alone). **Run every gate on a
  probe-free CLEAN build.** An incremental build mis-measured this arc
  once, and a forgotten probe faked a regression that did not exist.
- **demo2's anchor is bimodal** (20.9 / 23.4) under any timing nudge.
  Before calling it a regression, open the diff: if the mismatch is
  structurally identical and differs only in the ALTERED BEAST logo
  (palette-cycle glow phase), it is anchor phase, not corruption.
- **state_health.py defaults to `rom/s16.bs1`** and will happily read a
  stale state. It now warns, but always pass the path explicitly and
  check the BUILD line matches the rom you ran.

## Gates (every commit)

1. Boots + runs attract title->scream->eyehold->demo, NO hang.
2. MAME scoreboard on a CLEAN probe-free build: no scene regresses
   (`tools/parity_run.sh`).
3. Region guard passes, rom stamped `normal`, `make PRESSURE=1` holds.
4. PLAYABILITY PRESERVED — do not break the LOOP 5 milestone.

## Falsifier

state_health on ares: **V-gate rejects toward 0, vints/cycle -> 3,
handler total -> ~136 of 262.** The intermediate target is spin0's
measured 39.4% / 4.93 — if Step 1 does not at least reach that, the
COMM->DREQ migration did not actually remove the blocking wait; check
that the 68K no longer spins on COMM0 anywhere in the tail.

## First moves for the new session

1. Read LOOP.md ("THE BAND'S ORIGIN", iterations 6/6b/6c) and this doc.
2. `make TAILPROBE=1` and run `tools/win_probe.lua` to reproduce the
   mean tail split (170/70/49/45) — that is your before-number.
3. Design the packet extension on paper FIRST, keeping 4-word
   alignment; prototype the SH-2 apply before the MD push.
4. Build a boot/no-hang probe before trusting any packet-size change
   (`tools/win_probe.lua` prints vints + cycles; both must advance).
5. Hand Mike an ares state_health at each milestone — the band is the
   judge, and MAME cannot see it.

## Key files

- `md_src/md_main.c` — MD shim (vint handler, DREQ push, COMM stream,
  palette scan, thunk install)
- `sh_src/m_main.c` — SH-2 master (window, DMA snapshot/apply, apply_cram,
  dreq_rearm)
- `sh_src/s_main.c` — SH-2 slave (compose, COMM stream service, PAL_SETGEN)
- `tools/patch_game.py` — game patching; TILE_DIRTY_SITES is the model
  for the palette thunks
- `tools/win_probe.lua` + `make TAILPROBE=1` — the MD tail split
- `tools/state_health.py` / `tools/state_deadlock.py` — ares verdicts
- `tools/parity_run.sh` — the scoreboard gate
