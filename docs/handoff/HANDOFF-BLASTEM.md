# HANDOFF — BlastEm as a transport instrument (MESSAGES O-8)

Written 2026-09-23 for a fresh thread. Read this, `docs/design/BLASTEM.md`
(the card; section 5 is the gate, section 7 the current result), then
CLAUDE.md's START HERE. Nothing here is on the line; the rig holds spd2.

## 1. The job

Make BlastEm boot the SH-2 side of a 32X rom, then run the section-5
cross-check: reproduce ONE settled figure on ares, the rig and BlastEm.
Until all three agree on it, no BlastEm number goes into any card.
Disagreement is the finding.

Why anyone wants this: STATE's live axis is the FM window, which is
framebuffer-write-bound (LESSONS 2026-09-23 "the window's floor is the
framebuffer write rate"). ares charges instruction cycles only for
SDRAM and models the FB stall at ~470 B/line; the rig reads ~1.6x slower
(RIGBLIT). BlastEm's source (`sh2_util.c:80` burst fills,
`32x.c:1006-1009` FB write waits) claims to model both costs. Unverified.

## 2. What exists

Source: `~/src/blastem-0c61d0d95463` (`version.inc` 1.0.1-pre; no VCS
dir; `todo.txt` still lists 32X under future work, `CHANGELOG` lists it
as supported -- believe the todo). Builds on macOS with
`brew install glew` (done) then `make -j8` in that dir -> `./blastem`
(27 MB). `pkg-config` must see sdl2 and glew; the Makefile's Darwin
branch is `LIBS=sdl2 glew`.

Patched IN THAT TREE (uncommitted, no VCS -- if the tree is replaced,
reapply from this file):

- `blastem.c` line ~56: `char *dump_specs[32]; int dump_nspecs = 0;`
  and in the argv switch, before `case 'b':`, a `case '-':` that
  accepts `--dump region:addr:len:file` (repeatable, up to 32) and
  fatals on any other `--` switch.
- `genesis.c` after `#include "blastem.h"`: `run_dumps(genesis_context
  *gen)` -- regions `sdram` (`gen->mars->sdram`, 0x40000, address
  masked) and `wram` (`gen->work_ram`, 0x10000); writes bytes in BUS
  order (big-endian words) like ares-headless; called before `exit(0)`
  at BOTH `exit_after` sites (lines ~644 and ~922). It also prints two
  diagnostic lines to stderr, `DUMPSTAT` (both SH-2 pc/cycles/reset,
  adapter reg 0) and `COMMSTAT` (COMM0-7, SH2 INT_CTRL) -- remove or
  gate them once the boot works.

Eval dir: `/tmp/blastem-eval` with `32X_M_BIOS.bin`, `32X_S_BIOS.bin`,
`32X_G_BIOS.bin` (from `mame/32x.zip`, renamed; BIOS is opened
CWD-relative, `32x.c:1416`), `bprof3.32x` (the probe rom, section 4)
and `sh.32x` (Space Harrier, from srcref -- do not commit it anywhere).

## 3. The current result (section 7 of the card)

    cd /tmp/blastem-eval
    ~/src/blastem-0c61d0d95463/blastem -b 300 -m 32x \
        --dump sdram:0x06000000:262144:sd.bin --dump wram:0xFF0000:65536:wr.bin bprof3.32x

- SDRAM all zero at 300 and 1500 frames, for our rom and Space Harrier.
- `DUMPSTAT`: master pc=0x13C, slave 0x1C4, reset=0, cycles advancing,
  adapter reg 0 = 0x0083, `COMMSTAT` all zero, SH2 INT_CTRL 0x0200
  (= BIT_ADEN_SH2 only).
- At `-b 2` the master is at 0x198 (inside the boot path); by `-b 10`
  it is at 0x13C and stays.
- 32X_M_BIOS.bin: 0x100-0x13B are vectors 64-78, every one = 0x13C;
  0x13C is `bra 0x13C; nop` -- the BIOS's trap for an exception it does
  not expect. The reset PC is 0x140. The boot path: 0x140 sets SR,
  zeroes regs, copies 7 longs to 0xFFFFFFE0 (BSC), gbr=0x20004000,
  0x18C `mov.b @(0,gbr),r0; tst #2,r0; bf 0x1b4` (adapter byte bit 1
  clear -> delay loop, sleep), 0x1b4 cache regs 0xFFFFFE91/92, 0x1C0
  SDRAM fill from 0x26040000 downward.
  Disassemble with
  `~/src/marsdev/mars/sh-elf/bin/sh-elf-objdump -D -b binary -m sh2 -EB 32X_M_BIOS.bin`.
- The 68K side: our rom touches 7,927 RAM bytes (its own boot), Space
  Harrier 6 -- both stall in the handshake because M_OK never arrives.

So: the SH-2 side of EVERY 32X rom stops in the BIOS trap within ten
frames. That is the bug to find. Candidates, none verified:

1. An interrupt delivered while VBR still points at the BIOS (vectors
   64-78 are the 32X's VRES/V/H/CMD/PWM slots). Check BlastEm's SH-2
   interrupt delivery order at reset and what asserts an interrupt when
   the 68K sets ADEN/RES (`32x.c:665-690` handles S32X_ADAPT_CTRL
   writes; `sh2_util.c:640-760` reset/run). Print the vector number at
   exception entry in the SH-2 core.
2. An SH-2 exception (address error / illegal instruction) in the boot
   path between 0x198 and the trap -- e.g. the SDRAM fill at
   0x26040000 (uncached alias) hitting an unmapped page in
   `base_sh2_map` (`32x.c:1405-1430` builds it). Vectors 4-13 are the
   CPU exceptions; check what the BIOS's 0x10-0x34 entries point to
   (if also 0x13C, the trap does not tell you which).
3. Byte order of the BIOS after `byteswap_rom` (unlikely: the boot path
   executes sensibly to 0x198).

The debugger is NOT a shortcut: `debug.c:2328` uses `fgets_timeout`,
piped input returns rc=124 with no output (tested twice); `-D` gdb
remote is 68K-only. Never run without `-b`: `-d` alone opens an SDL
window and blocks past 120 s.

## 4. The cross-check figure, and how to take it

Figure: the master SH-2's window cycle on the level-1 attract demo,
frames 700-760, from the BODYPROF probe rom `rom/night/bprof3.32x`
(build: `make line BODYPROF=1`; Makefile knob BODYPROF, m_main.c
`bprof[]`). `bprof` sits at SDRAM 0x06003544 (verify in
`rom/s16.lst`, symbol `_bprof`, after any rebuild): 12 longs,
[0] launch [1] master half [2] slave pickup wait [3] slave half wait
[4] ph_ship [5] apply_cram [6] ->dreq_rearm [7] publish->ack
[8] ack->walker slice end [9] slice end->next pickup [10] pickup->pickup
[11] windows. Units: SH-2 FRT ticks; 45.8 ticks per line on ares
(12,001 ticks per 262-line vint, measured pickup-to-pickup).

ares baseline (deterministic, no input script needed):

    A=~/src/ares-debug/build_macos/headless-ui/Release/ares-headless
    for f in 700 760; do $A --frames $f --dump sdram:0x06003544:48:xbp_ares_$f.bin rom/night/bprof3.32x; done

    ares demo f700-760: windows 60, cycle 12001 ticks/window (45.8/line),
    master half 26.6 lines, launch 7.3, apply_cram 9.8, publish->ack 15.8,
    tail 59.9, idle 131.9

BlastEm, once it boots: the same two dumps with
`--dump sdram:0x06003544:48:xbp_bl_$f.bin` at `-b 700` and `-b 760`,
then diff the 12 longs. The compare script is in the previous thread's
scratchpad; it is ten lines (subtract, divide by [11], divide by
[10]/[11]/262). Agreement criteria: windows == 60, ticks/line within a
few percent (the FRT prescaler is the same rom; if the tick rate itself
differs, BlastEm's SH-2 clock is off), and the master half within the
run-to-run spread on ares (~2 lines).

Rig side of the same figure: `make line RIGBARCODE=1 BODYPROF=1
RIGBLIT=1` puts the master's last blit_half lines (FRT/46) on barcode
byte 5; `tools/rig_barcode.py --rom NAME` decodes rig screenshots. On
the level-1 demo the rig read 67-86 against ares 45-47 (LESSONS
2026-09-23 "the FPGA's framebuffer write rate"). That is the number
BlastEm must explain or contradict.

## 5. Rules that bit this arc

- A knob's presence in `.build_flags` is not its value: `BANDSHIFT=4`
  on the command line built with 36 (the ship list's literal won).
  Read the value.
- Every FRT-derived line count before 2026-09-23 in the record assumed
  128 ticks/line; it is 45.8. Recalibrate before comparing.
- "Boots" means both CPUs past the handshake (COMM0 == M_OK/S_OK),
  proven by a dump. The 385 fps headless figure was an SH-2 in a
  two-instruction loop.
- MAME cannot pixel-gate or land packets on R60 builds (CLAUDE.md);
  ares is exact on data and blind to memory timing; the rig is the
  only hardware clock and varies per launch (three launches per
  verdict).

## 6. Done when

1. `COMMSTAT` shows M_OK/S_OK and SDRAM is non-zero on Space Harrier
   and on `bprof3.32x` at `-b 300`.
2. The section-4 figure is taken on BlastEm and written into
   `docs/design/BLASTEM.md` section 7 beside the ares and rig values,
   with the verdict (agree / which one is wrong).
3. Only then: `--profile` (per-PC instruction counts, ares' CSV
   columns `cpu,pc,instructions`) and the `symbols rom/s16.lst` loader
   (`debug.c:3180`), if the instrument earned them.
