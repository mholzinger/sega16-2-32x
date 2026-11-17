# Composed foreground mask experiment

Follow-up: [fresh MAME/ares validation and refined mask loops](HEADLESS-MASK-VALIDATION-2026-11-06.md).
This document records the first prototype.

First bounded implementation of [render preparation separation](../design/RENDER-PREPARATION.md).
Component commit afda48ba; renderer/diagnostics commit 9cf08473.
Default renderer and accepted p15bpt7 remain unchanged. Hardware speed is **unmeasured**:
the office rig was unreachable and the timing probe did not launch. No promotion.

## Implementation

`R60MASKBLOCK=1` prepares 32x32 opacity masks from sixteen canonical tile words,
validates complete source keys before reuse, and clips/emits each sprite's mask
at its existing draw point. Art banking stays in the game adapter. Source collection,
composition and framebuffer emission have explicit boundaries in a portable C header.
No game simulation, V-int ownership, publication protocol or sprite ordering changes.

The 10 KiB mask arena is partitioned between 512 old tile entries and 32 composed
entries with full keys. It adds 152 ordinary data/BSS bytes before alignment.
`R60PRIOCACHE512=1` supplies an old-punch control with the same smaller tile cache.
Default builds retain the accepted 1024-entry cache and old punch.

The trial retains existing span repunches. It does not yet move all render inputs
into a new immutable scene object or change the master/slave ownership protocol.
A hardware win is required before replacing the accepted path or broad cleanup.

## Checks completed

Evidence directory: `build/codex-maskblocks/` (local, ignored).

- Actual production C cache/emitter vs independent scalar 8x8 per-pixel oracle:
  ASan/UBSan pass, all framebuffer bytes and cleanup metadata equal. 3378 cases
  including randomized inputs and ten native level-one foreground snapshots.
  Alignment, clipping, world/page wrapping, invalid pages, bank changes, source-key
  changes, empty/mixed/solid art and colliding callback storage are exercised.
- Cold ares mb02, 4000 frames: 3985 vints, 3991 ticks, 3059 flips, zero span timeouts.
  78972 block lookups, 17780 hits, 54 builds. These counters are not a speed result.
  Capacity-matched old-path control also passes a cold 4000-frame run:
  3985 vints, 3991 ticks, 3203 flips, zero span timeouts. No ares speed ranking.
  Independently reconstructed 1024 populated cache rows: zero wrong rows.
  Deliberately flipping one cached bit makes the audit report one bad row.
- Fixed native sprite+plane fixture, 4000 frames: all 1024 sprite bytes match the oracle
  in live sprite RAM and both snapshot slots; selected tile pages, text, page/scroll
  registers and tile bank match fixed inputs in both runs. Full-clear diagnostic
  removes prior framebuffer history. Actual ares flip traces select the shown bank.
  **71680 shown pixels compared, zero different pens**; 22647 opaque pixels and
  3143 sprite pixels covered by opaque foreground. No zero/through-pen equivalence
  tolerance, RGB thumbnail verdict, or best-fitting-bank selection.
- Quantiser: 65536 words pass. Existing nine sprite-pixel tests and four source-block
  census tests pass. Generated plane fixture decoder passes exact word/bounds check
  with ASan/UBSan. Default p15bpt7 flags preprocess renderer and port adapter into
  identical tokens before/after these changes (`normal-source-proof.json`).
- mb02 region guards: `_end=060172C0`, hot region 2 end `06018EC0` (320 bytes below
  `06019000`), RAMTEXT end `06037CF4` (780 bytes below `06038000`). 39 hot functions,
  802 dispatch rows retained. Accepted p15bpt7 local SHA256 remains
  `661f907492dfba6d68ee5f70ca289f497536e1f291c4ad0f1dff89ed0af09a3e`.

Full-clear fixture is a rasterization check. It forces redraws and does not prove
normal kept-sprite cadence, all partial-redraw cases, whole-game parity, or normal
color delivery. No new shipping ROM or gameplay acceptance is claimed. The 4000-frame
probe is not a 65k/125k soak. Production preprocessing is unchanged; full shipping gates
and Mike's play pass remain required before enabling this path by default.

## Why the fixture changed

The first sprite-only freeze stopped at raw tick 0x710 in both runs, but FG scroll
stopped two pixels apart. Its output differences were incomparable.
`PORTSPRITEFIXTUREPLANES=<native-prefix>` now supplies fixed selected native tile
pages and all native text/scroll words. Unselected pages are blank; the adapter's
bank request is checked separately. Runs/literals keep the generated fixture inside
existing cart limits. The generated ROM-derived header remains ignored.

With fixed source inputs and normal partial clearing, the raw comparison still
reported 628 differences per physical bank, all 0 versus 255 through bytes. That result
remains in `scene-comparison.json`; it is not an exact-pen pass.
`R60FIXTURECLEAR=1` forces full canvas clearing while the fixture is active.
One hidden bank was caught partway through drawing in the first full-clear dump.
The final comparison uses each run's last **actual applied** flip, not a requested
flip or whichever bank happens to match. In this ares fork, `selectFramebuffer`
(`ares/md/m32x/vdp.cpp:96-105`) maps physical shown bank to `1-select`.
The control's final applied event is frame 3999/select 1; experiment frame 3998/select 0.
Their shown physical banks are respectively 0 and 1, with fixed dot shift 0.
Final proof: `shown-comparison.json`, including all source/dump/listing/trace hashes.

These are frozen rendering inputs rather than a route/tick comparison of simulation.
Do not infer cadence or an arcade-wide visual closure from a still fixture.

## Artifacts and next hardware step

`mb02.32x` is the live level1_full GOD+tape rendering probe, R60RIGSPLIT=3,
frontend disabled and bars visible. SHA256
`7d5018a9a41f8a694447662224c35e985e4c8c6f9f201a68a7f5203c33f00ab4`.
Compare with existing perf04 and the capacity-matched `mbcontrol512.32x`.
`mbscene-clear-{control,experiment}.32x` are correctness-only frozen PENID fixtures,
not performance probes or ROMs for play. mb02 and scene fixtures stamp 55ae5f0b;
the capacity control stamps afda48ba. Both include then-uncommitted work; use
`roms.json`, flag files and build logs for exact identity.

When the office rig is available, run identical live tape captures with automatic
p15bpt7 restoration; compare punch cost, plot/clear spillover, master headroom and
whole-pass intervals. Source validation scans sixteen words even on a hit; a high
hit rate does not establish a win. Do not use ares model cycles to rank hardware.
The old path remains available, and the experiment stays off until this measurement.
