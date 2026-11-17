# Reuse prepared sprite packets

Experimental, disabled by default. This follows the
[render preparation contracts](../design/RENDER-PREPARATION.md). The goal is to
apply palette-bank bits once to reusable art, then copy ready pixel groups from
RAM. It changes rendering work, not game logic or sprite ordering.

Follow-up: [progress-based packet handoff](SPAN-PROGRESS-2026-11-06.md) passes
the 40k route and a 65k normal-build soak with zero timeouts, while deliberate
consumer stalls still trigger recovery. Earlier failed results below remain
specific to the old sp02 handoff. Hardware performance remains unmeasured.

## Component and storage

`sh_src/r60/r60_sprite_prepare.h` contains a caller-owned single-entry cache.
The game adapter supplies an immutable baked sprite-frame packet, physical art
key, numeric palette bank, height and mapped-page epoch. The component retains
row headers and applies bank bits only to nonzero pixel bytes. Position,
alignment, clipping, framebuffer bank and RGB palette values are deliberately
absent from the key. Palette cycling therefore needs no rebuild. Height and
page epoch prevent stale packet reuse after a source change.

Repeated candidates are admitted; isolated poses leave a useful resident alone.
Preflight checks the complete packet against capacity before overwriting the
resident. Oversized or unsupported records use the existing renderer. A cache
hit returns the complete unaligned packet so all four destination alignments
share it. One slave owns preparation and consumption, within the existing
snapshot/page-switch protocol; this introduces no cross-CPU publication scheme.

Enable with `R60SPRPREP=1 R60BAKEART=1`. It also selects the ready-byte assembly
loop and 512-entry tile-priority cache. `R60SPRRAW=1 R60PRIOCACHE512=1` provides a
capacity-matched control that uses the ready-byte loop for ordinary bank-zero
art without a prepared cache. Neither flag is a player preset.

| SDRAM offsets, end excluded | Use |
| --- | --- |
| 3C400..3D400 | 512 tile masks |
| 3D400..3D800 | 512 tile tags |
| 3D800..3E800 | 1024-word prepared sprite packet |
| 3E800..3EC00 | Spare; row-check buffers/counters in CHECK builds only |

Cache metadata occupies 72 bytes before alignment. The experiment conflicts
with composed masks and the older art cache because they share this arena.
The default keeps the accepted 1024-entry tile cache and original copy loop.
Normal preprocessed renderer tokens and default assembly section bytes match
6204a642 exactly (`default-proof.json`).

## Correctness evidence

Local artifacts: `build/codex-sprite-prepare/`. All probes use a fresh reset.

* `check_sprite_prepare.py`: actual C under ASan/UBSan; all 65536 four-nibble
  groups in all sixteen banks against scalar per-byte coloring, plus reuse,
  height/bank/epoch identity, clipping, capacity, admission and guard canaries.
* `spcheck02`: 4000 ares frames, **113539 strict old-plotter row comparisons;
  zero pixel, extent or pixel-count mismatches**. Prepared copies cover all
  alignments (129/212/196/183) and nineteen nonzero-bank copies. CHECK buffers
  translate canvas coordinates only in the shadow comparison; actual output
  coordinates are unchanged. CHECK overhead is excluded from performance data.
* `sprite_prepare_audit.py`: resident packets reconstructed independently from
  sprite ROM and scalar coloring. Both the CHECK capture and the live capture
  after the page-one switch have zero differing bytes. A corrupted pixel is
  rejected. Stored-packet identity alone does not establish displayed output.
* Fresh MAME headless run reaches frame 4001; all five frame-2729 RAM dumps
  match the prior native fixture exactly. Dummy SDL, fresh cfg/NVRAM,
  `-video none -sound none -keyboardprovider none -nomouse -nojoystick`;
  `arcade/command.json` retains command and environment.
* `spnative02` ares fixture: all twelve arcade sprite records audited against
  native sprite ROM, with foreground/text coverage. **15962 visible pixels,
  3143 foreground-covered pixels; zero missing, wrong, extra or leaked pixels**.
  Whole shown-bank framebuffer equals the old renderer: **0/71680 pens**.
  Actual applied flip trace selects the bank. Native reference colors also
  match the fresh MAME PNG. Both actual port PNGs decode to the expected
  15962 sprite pens with zero errors, using all 224 distinct calibration colors.

The fixed-scene fixture excludes the bottom sixteen calibration rows from the
native sprite audit and refuses unsupported zoom/shadow/priority modes. It uses
PENID/full-clear diagnostics: it proves neither normal-color cadence nor live
whole-game parity. Eight scene-auditor fault-injection cases also pass.

## Work measurement and remaining gate

The two live probes retain GOD/level1_full tape and profile-3 instrumentation;
no frontend or fixed-scene override. In 40001 emulated frames:

| Probe | V-ints | Game ticks | Flip-publication counter | Span timeouts |
| --- | ---: | ---: | ---: | ---: |
| spraw01, ready-copy control | 39986 | 39992 | 35408 | 0 |
| sp02, prepared packets | 39986 | 39992 | 35459 | 1 |

Both enter level two at frame 36405 and have matching final captured gameplay
fields. The prepared cache records 145804 lookups, 18458 hits and 969 builds;
483275 words prepared, no capacity declines, mapped-page epoch one. This is
bounded reuse evidence, not an overall speed win. The timeout makes sp02 fail
the current live-health gate; it remains diagnostic and disabled.

The 2400..3600 interpreter window reports 232.868 copy-loop instructions per
copied row versus 233.097 for the ready-copy control. Cache lookup/preparation
adds 566686 instructions plus 7700 pass-begin instructions over that window.
The copy-loop instruction reduction alone does not pay this overhead. RAM versus
ROM access costs require hardware timing; instruction counts and ares flip
counts are not FPGA speed measurements. No hardware test or new player ROM is
claimed. Accepted p15bpt7 remains unchanged.

Read-only timeout tracing reproduces the single counter increment at emulator
frame 35926, slave PC 060360A4, V=224/H=34. The held packet sequence is22524,
direct A3991words (flag stripped), display hold1; B's length slot contains752
but can be stale for a direct packet. FM drops at35923 V209/H241. The next
COMM0 acknowledgement is35927 V0/H81, after the watchdog closes FM at35926
V224/H34. These are ordered write traces, not an inference from still images.
The four-V-int-count watchdog can represent less than four complete frames
when a span opens just before V-blank. That is a scheduling lead, not proof that
the packet was consumed intact. Payload integrity and a robust transfer-work
budget remain open. The production timeout threshold has not been changed.

The experiment is saved in73f7980b; the probes retain their original6204a642-dirty
build stamps and exact ROM/listing hashes in the evidence. No hardware retry was
attempted after the previously unreachable office rig.

## Interactive experimental build

At Mike's request, `build/port/rom/spplay01.32x` enables the experiment with
normal live controls, frontend, lives and sound. Source6766d118, stamp2026-11-06
14:41:09; SHA256
`b3302ecd59352bf0f6a69fe01af2ec771dc9b6b6a44cf89e2716e05c139e6d72`.
Its compiler flags equal p15bpt7 plus SPRPREP/SPRRAW/PRIOCACHE512. No embedded
input tape, GOD mode, fixture or timing bars. No rig upload or default promotion.

Headless ares6001-frame external-input check:5986vints/5991ticks,5214flip
publications, zero span timeouts;24043cache lookups/4307hits/64builds. Boot/title
menu entry and pause/resume checks pass. All five starting rounds pass fresh
current-layout menu tapes with default DIP54 and four lives. The historical
`tests/frontend/check_frontend.py` level-selection route predates the VERSION
row/full DIP sheet and fails identically on accepted p15bpt7; it is not an
eleven-case retention pass. Current-layout script, results, flags comparison and
ROM manifest are in `build/codex-sprite-prepare/play/`. The earlier40k timeout
gate and unmeasured hardware benefit remain open; this is an experimental ROM.

Mike's ares playtest reports no new visible defects and describes any change as
seamless. He explicitly cannot judge performance fully from ares. Record this
as visual play acceptance of spplay01; it does not close the diagnostic timeout
or establish a hardware speed improvement. No ROM or default changed afterward.
