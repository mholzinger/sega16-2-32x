# Keep packet access while completed work advances

Implementation3b8dab16, optional `R60SPANPROGRESS=1`; default remains off.
This addresses the transfer gate found by the
[prepared sprite experiment](SPRITE-PREPARATION-2026-11-06.md).
Local evidence: `build/codex-span-progress/`.

## Ownership contract

Previously the slave reclaimed framebuffer access after a total two/four
V-int-count budget even if the 68K was still consuming a valid packet. The
prepared experiment exposed one such closure before the 68K's acknowledgement,
under a blanked-screen3991-word direct upload. A V-int count is not a count of
complete elapsed frames when access begins mid-frame.

With this flag, the existing two/four budgets measure time since **observed
progress**, rather than total transfer lifetime. The ACK still closes access
normally. Neither budget constant changes; the existing recovery/nametable
resend remains available when progress stops. `R60_SPANV` retains its original
meaning for flip timing and diagnostics; a separate four-byte, slave-owned lease
tracks the last observed progress value and V-int count.

The 68K publishes a monotonically wrapping16-bit counter on COMM4 after bounded
completed work: inline-art DMA, copying a baked record to owned WRAM, fetching
art to WRAM, or a plane-span DMA. The counter is enabled only inside the shim's
FM-owned consume. Timer interrupts and idle polling do not publish progress.
This is packet-drain progress; it does not assert that every staged WRAM tile
is already visible in VRAM. The ACK retains the existing consumer semantics.

COMM4 is shared during startup, then owned by this producer. The headless trace
calls it register index2 (the index is not the byte offset). No new packet format,
source reordering, simulation change or interrupt timing code is introduced.
`R60SPANPROGRESS` requires the C-port R60 mixed-record consumer and SLAVEWIN.
The portable `r60_span_lease.h` contains the stall policy; hardware access stays
in the adapters. Both V-int and progress counters use16-bit wrap. The protocol's
bounded packets and frequent polls must prevent an entire65536-unit progress
wrap between observations.

## Evidence

* Actual C lease test under ASan/UBSan: a progressing upload outlives its total
  budget, a stationary counter expires at the budget even while the IRQ clock
  advances, progress then stall recovers, and both counters wrap independently
  across131072 progress observations.
* `spstall03`, diagnostic only, intentionally stops the 68K after sixteen
  publications. Last progress is frame139/V173/H18. The slave recovers at
  frame142/V224/H35. At1001frames the SH-2 counters show986V-ints/992ticks and
  213backstops. The stalled 68K publishes no further progress or ACK. This proves
  watchdog recovery remains active, not that a deliberately stopped game is playable.
* `sp03`:40001emulatedframes,40000timeline rows,39986V-ints/39992ticks;
  **zero span timeouts**, versus one in sp02. Both reach level two at36405 and
  finish with matching captured level/round/scroll/player/lives/score/boss fields.
  There are35683consecutive progress publications with no counter discontinuity
  and no other COMM4 writer after the first publication. All1339observed span
  closures in35000..36599 use the ACK branch (PC06033BD8), not the backstop.
* Fresh MAME headless4001-frame run: all five regenerated frame2729RAM inputs
  equal the earlier fixture. Fresh cfg/NVRAM, dummy SDL and no GUI/input providers;
  command/environment retained in `arcade/command.json`.
* `spnative03` ares fixture against that native state and the accepted old-path
  control: **0/71680 shown-bank pens differ**. All twelve native sprite records
  pass:15962visible pixels,3143FG-covered, zero missing/wrong/extra/leaked pixels.
  Actual port PNG PENID decoding also has zero wrong visible pens. Native RGB
  reference matches MAME. Applied flip traces choose the displayed bank.
* Accepted-default preprocessing is token-identical for both the MD consumer
  and SH-2 renderer with the flag off. No accepted player ROM was overwritten.

The live runs change physical transfer phase and do not provide per-frame pixel
equality between different builds. The fixed-scene proof excludes calibration
rows from native sprite auditing and refuses unsupported zoom/shadow/priority
modes. It establishes neither live whole-game parity nor normal-color cadence.
The original3991-word packet was not replayed at an identical sub-frame phase;
the passing route, bounded-progress policy and deliberate-stall test are separate
evidence. Headless flip publications35376versus35459in sp02 are not FPGA speed.

## Interactive build

`spplay02.32x` adds this handoff to the playtested prepared-sprite path, retaining
normal frontend, controls, lives and sound. No embedded tape, GOD mode, fixed
fixture or timing bars. All five rounds start through current-layout menu tapes
with default DIP54/four lives. The older frontend script's navigation is stale;
this is not an eleven-case retention claim. The build and manifests are retained
locally; hardware measurement and player acceptance of this new build remain open.

The normal-build external-input soak completes 65,001 emulated frames:
64,986 V-ints, 64,992 game ticks, 55,064 flip publications and **zero span
timeouts**. All 1,082 post-startup health samples also have no timeout and ticks
within twenty of V-ints. This is a 65k scoped regression check, not a complete
125k/two-tape/gameplay-parity release gate. Source default and hardware baseline
remain unchanged.

Interactive ROM SHA256:
`5722cf817b280a8ec419ae672eaed883a893e6150fc137d4e0897c01ae4cb6a2`.

The diagnostic ROMs were built from parent3de1d150 plus the implementation later
committed as3b8dab16. Their actual ROM hashes/stamps are retained in
`protocol-proof.json`; do not relabel them as clean builds of the later commit.
No office-rig retry or upload occurred after the previously unreachable connection.
