# Sprite overlap pen audit, 2026-11-05

The reported sprite defect has a measured software cause. The baked row loop
OR-ed the bank prefix into every byte of a nonempty group, including zero pens.
Those zeros became nonzero bank-pen0 overwrite-image stores, erasing earlier
sprites underneath. A through CRAM entry is not the same as skipping a write.
The R60BAKEARTCHECK comparison explicitly excused these differences.

The repair colors only nonzero bytes before alignment/carry composition. For
raw bytes0..14, `(v+0x0f0f0f0f)&0x10101010` gives one bit4 flag per nonzero
byte. Multiply by bank0..15 and OR with v. No arithmetic can carry between bytes.
All four alignment loops and trailing carry preserve literal zero. No baker
format or assets change. The self-check now requires exact bytes.

## Reproduction and measured result

All evidence is local, gitignored `build/codex-sprite-pens/`.
Baseline probe p15bpt6g uses p15bpt4 flags plus GOD and the embedded level1_full
tape. Changed probe p15bpt7g uses the same flags and tape with the repair.
The normal rig ROM p15bpt4 remains preserved. Native MAME uses fresh cfg/NVRAM,
SDL dummy drivers, input disabled, same tape/GOD, DIPs66/54, sprite table every
frame2680..2740. No reference pixels come from port sprite RAM.

At arcade frame2729, active sprite drawing words0..5 match the port's historical
snapshot. Decode the reference directly from arcade table and sprites.bin.
Later opaque sprite pens cover earlier ones; transparent pens preserve them.

| Slot | Reference visible pens | Baseline matched | Baseline missing | Changed matched | Changed missing/wrong/extra |
| --- | ---: | ---: | ---: | ---: | ---: |
| 8 player | 953 | 953 | 0 | 953 | 0/0/0 |
| 9 wolf | 1285 | 1137 | 148 | 1285 | 0/0/0 |
| 10 zombie | 987 | 928 | 59 | 987 | 0/0/0 |
| 11 head | 169 | 169 | 0 | 169 | 0/0/0 |

All baseline mismatches are transparent bank-pen0 bytes from the later sprite:
wolf is overwritten by bank12, zombie by bank13. Original arcade2728 sample
has113+59 missing; do not mix those counts with2729's148+59.
Background sprite slots0..7 contain foreground-punched255 pixels and are NOT
claimed exact to an unoccluded sprite-only reference. No full-frame equality,
zoom/shadow coverage, cadence, speed improvement, or hardware closure claimed.

## Alignment trap found during measurement

The live sprite table runs ahead of the displayed bank. Worse, a double-buffered
snapshot can already have been reused by the time the framebuffer is dumped.
Reading the same snapshot address from a later SDRAM dump gave a false2-pixel
enemy offset. Preserve historical snapshots BEFORE their slots are reused.

Baseline displayed at end2701: physical bank1, last flip2700/select0. Historical
snapshot2465 at capture/sd-2699.bin offset0xa450 matches arcade2729 exactly.
Changed displayed at end2702: physical bank0, last flip2700/select1. Historical
snapshot2422 at fixed-capture/sd-2701.bin offset0xa050 matches arcade2729 exactly.
Both dot shifts0. End2702 changed snapshot0 has already been reused for2424,
and describes arcade2733; it is not the source of the shown frame.

The earlier alignment.json describes separate baseline2728 measurement and
must not be used as2729 proof. Each report must retain its own tick/bank metadata.
Physical bank selection follows ares vdp.cpp selectFramebuffer: select1 displays
bank0, select0 displays bank1. Flips are from --trace-flip, not image resemblance.

## Instrument and tests

`tools/sprite_pixel_audit.py` decodes the arcade's own1/2KiB sprite table and
interleaved1MiB ROM, reads an explicitly chosen physical bank using its line
table/dot shift, maps bank pens back to System16 palette/pen, writes per-sprite
raw missing/wrong/extra/punched counts and pixel/owner arrays. It requires caller
alignment evidence, refuses zoom/shadow records, and does not promote raw counts
with foreground occlusion into a defect verdict.

`python3 tests/diagnostics/check_sprite_pixels.py`: nine cases cover signed
pitch, bank wrap, flip, clipping, transparent overwrite order, explicit bank/
line-table/dot shift, count classification, and all65536 four-pen patterns in
all16banks for the packed coloring operation. Quantcheck65536 colors passes.
Changed diagnostic and normal link guards pass. Normal p15bpt7 sourcee819e58c
includes fixadcaa3e7, stamp2026-11-05 23:43:19 R60, SHA256
661f907492dfba6d68ee5f70ca289f497536e1f291c4ad0f1dff89ed0af09a3e.
Normal _end06017080, RAMTEXTend06037e58. God36k ticks35991/vints35985,
flips31585, zero span timeouts. Normal65k ticks64991/vints64986/flips54245 and normal36k35990/35985/30576;
zero span timeouts in both. Normal guard code/data is identical outside the
final64-byte stamp (seven differing timestamp/provenance bytes).

The strict on-ROM R60BAKEARTCHECK diagnostic (frontend disabled only in this
probe) executes153262rows by4000frames, with r60_bk_bad/r60_bk_badx/r60_bk_badn
all0. The earlier exception for bank-pen0 is removed; these are exact byte tests.

Independent reference validation: fresh MAME event dumps/screens for2728..2731,
normal RGB derived from documented System16 resistor weights and the arcade's
palette. Every visible pixel of sprites8..11 matches the same-frame MAME RGB
image in all four frames (native-render-validation.json). This validates geometry
and colors; RGB alone cannot distinguish identical-color pens. The port audit
continues to compare palette/pen values, not color similarity. The optional
--arcade-palette/--arcade-image arguments reproduce this native check in reports.

Final same-tick reports: before-2729/report.json and after-2729/report.json, each
with input hashes and its own before/after alignment metadata. Native comparison
counts are scoped to those sprites, not a full-frame equality claim.

## Rig state

User explicitly authorizes full office MiSTer access. /var/log/ACTIVEGAME
confirmed p15bpt4 before work. Baseline and repaired matched-tape probes each
recorded three screenshots under build/accept/rig/cut_codex_sprite_pens{,_fixed}.
The first helper aborted on an unmatched zsh cleanup glob, so Codex immediately
restored p15bpt4 directly. The helper now tolerates an empty directory and uses
an EXIT trap for restoration. The repaired capture completed and confirmed
p15bpt4 restored. Capture stills have not established matched-tick pen equality
on the FPGA or temporal correctness. Those remain acceptance work.

## Calibrated FPGA fixture (2026-11-06)

The initial raw-tick hold probes were rejected: p15bpt8id caught bootFFFF;
p15bpt9id held0710 but did not have the exact target table. The fingerprint hold
p15bpt10id never reached the exact target table under its changed replay timing.
They establish NO hardware pixel equality. Their source hold implementation was
removed rather than retained as a supposedly valid alignment instrument.

The replacement uses `PORTSPRITEFIXTURE=<arcade-table.bin>` and R60PENID. It
follows normal play until raw tick0710, copies the supplied oracle command table
to the video shadow, and holds game logic while the renderer presents it. This
is explicitly a RENDERER UNIT FIXTURE, not a gameplay-route or cadence test.
The generated header/id are ignored ROM-derived local data. The generator refuses
truncating a table with no end marker in the first64records.

R60PENID assigns distinct red=pen/green=bank/blue=0 colors to opaque sprite pens
and draws256 known byte swatches in the bottom16rows. `sprite_penid_check.py`
requires224 unique uniform opaque swatches, uses exact RGB inversion, refuses
rescaled captures and any selected sprite overlapping the calibration strip.
This identifies framebuffer pen IDs without assuming a device's RGB expansion.
Normal palette delivery and cadence remain separate. The default normal ROM
has none of these flags. The normal p15bpt7 remains the playable candidate.

Fixture p15bpt11fx cold ares4000 validation: all1024command bytes match the
oracle in the live video shadow AND both renderer snapshots. Calibration224
unique opaque colors. Selected player/wolf/zombie/head counts953/1285/987/169,
all missing/wrong/extra0/0/0. Evidence fixture11/ares-pens.json and alignment.json.
FPGA fixture PASS in all three unscaled320x224 captures. Each has224 unique
uniform calibration colors, and selected sprites8/9/10/11 have953/1285/987/169
matched pens with missing/wrong/extra0/0/0. Reports fixture11/rig-reports.json;
raw captures build/accept/rig/cut_codex_sprite_fixture11/. This is a measured
renderer-fixture result on the FPGA, not an equality claim from looking at PNGs.
It is NOT full normal-color, live-gameplay, boss, or cadence acceptance.

Normal p15bpt7 is ON THE RIG. After restoring p15bpt4, deploy/launch completed
2026-11-06 00:13:26; /var/log/ACTIVEGAME reports
/media/fat/games/S32X/p15bpt7.32x and remote sha256sum equals local
661f907492dfba6d68ee5f70ca289f497536e1f291c4ad0f1dff89ed0af09a3e.
Normal controls, no tape/GOD/fixture/pen-ID flags. Earlier control p15bpt4 preserved.
Mike confirms on2026-11-06: "You fixed the transparency issues!" The reported
transparency defect is now play-accepted on p15bpt7. This does not establish
complete boss/gameplay coverage or close separate cadence/performance work.
Production fixadcaa3e7; capture restoree819e58c; diagnostic toolsb51bdf17.
Fixture p15bpt11fx SHA256
42e86178e1ede6671cb97319d19046de154f048d22768d0f631ddc06481626c7.
Probe was built from the working tree subsequently committed asb51bdf17;
its stamp alone does not describe those then-uncommitted diagnostic additions.

Normal-source guard: disabled probe blocks excluded, current port_l2.c and
r60_main.c match e819e58c exactly apart from blank lines. Earlier compiled normal
guard differs from p15bpt7 only in seven trailing stamp bytes. No diagnostic
flags, commands, palette IDs or game hold enter the normal ROM.
