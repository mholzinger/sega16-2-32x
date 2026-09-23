# BlastEm as a measurement instrument

**Status: CANDIDATE, NOT YET TRUSTED.** No number from this emulator may
be quoted in a card until the cross-check in section 5 has been run.
Tracked as MESSAGES O-8.

Convention follows `SILICON.md`: every claim carries the command that
reproduces it, and claims READ FROM SOURCE are marked separately from
claims TESTED.

    binary   /Users/mikeholzinger/bin/blastem-osx-1.0.0/blastem
    source   ~/src/blastem-0c61d0d95463/

---

## 1. Why this exists

`STATE.md` puts the live axis at TRANSPORT, and we have no usable
instrument for it:

    ares          SLAVE-GATED, and charges SH-2 instruction cycles only
                  -- no SDRAM waits, no data cache, no instruction fetch.
                  Removing master work cannot move its number by
                  construction.
    MAME 32x      a convenience model. Renders every NATIVE build as
                  confetti; cannot pixel-gate the line.
    the rig       the only speed authority, and its flip rate varies 6x
                  between COLD runs of the same rom.

BlastEm is interesting for exactly one reason: **it appears to model the
two costs ares omits, and both of them are transport.** See section 3.

**It does not replace ares.** ares keeps exactness and the anchors.

## 2. Setup — reproducible

BlastEm needs the three 32X BIOS ROMs, which are already in this repo's
MAME romset. It opens them CWD-relative with a plain `fopen`
(`32x.c:1416`), so they must sit beside the rom:

    mkdir -p /tmp/blastem-eval && cd /tmp/blastem-eval
    unzip -o -q <repo>/mame/32x.zip -d .
    mv -f 32x_m_bios.bin 32X_M_BIOS.bin
    mv -f 32x_s_bios.bin 32X_S_BIOS.bin
    mv -f 32x_g_bios.bin 32X_G_BIOS.bin
    cp <repo>/rom/s16.32x .

Headless run:

    blastem -b 300 -m 32x s16.32x

**`-b N` is headless: run N frames, then exit.** It is UNDOCUMENTED in
`-h`; the implementation is `blastem.c:429-437` (`headless = 1;
exit_after = atoi(argv[i])`). It is the direct analogue of ares
`--frames N`.

**TESTED 2026-09-23:** the above boots `rom/s16.32x` and completes 300
frames in 0.78 s user (~385 fps), clean exit, with no
unimplemented-instruction errors.

## 3. Fidelity — READ FROM SOURCE, NOT VERIFIED

**These two claims are the entire reason to care, and neither has been
confirmed against anything. Treat them as hypotheses.**

    sh2_util.c:80    sh2_generic_burst_read():
                       sh2->cycles += chunk->burst_cycles
                                      * sh2->opts->gen.clock_divider;
                     = per-region CACHE LINE BURST FILL cost.
                     ares models no instruction fetch and no data cache
                     at all (LESSONS).

    32x.c:1006-1009  s32x_video_sh2_write() returns wait_cycles and:
                       sh2->cycles += wait_cycles;
                     = FRAMEBUFFER WRITE BUS WAITS. Neither ares nor
                     MAME models the FB-write stall floor (CLAUDE.md).

There is also per-write cycle accounting through `write_word_cycles`
(`sh2_util.c:60-67`).

**What this would mean if true:** BlastEm prices the two things the
transport axis is made of. **What it does not mean:** that its numbers
are right. Section 5 is not optional.

## 4. Gaps against `ares-headless`, and the patch

Missing outright:

    --dump region:addr:len:file
    --profile
    --input replay

**The debugger is NOT a workaround.** It has SH-2 breakpoints, `print`,
`disassemble`, and a `frames N` command, but `debug.c:2328` reads stdin
through `fgets_timeout()` with a progress callback.

**TESTED, twice:** piping `help\nquit\n` into `blastem -b 600 -d -m 32x
s16.32x` returns **rc=124 (timeout), zero bytes of output.**

**`-D` (gdb remote) is 68K-ONLY** — `gdb_remote.c` contains zero SH-2
references. Not a path.

### 4.1 Where to patch

The frame loop already has the hook. `genesis.c:613`:

    if(exit_after){
        if (elapsed >= exit_after) {
            exit(0);
        } else {
            exit_after -= elapsed;
        }
    }

This runs at a frame boundary with `gen` in scope, and `gen->mars` is
the 32X context. (There is a second identical site at `genesis.c:890`
for the other run mode — patch both or factor one helper.)

Everything a dump needs is reachable from `gen->mars` (`32x.h:82-100`):

    sh2_context  *main;      master SH-2 context (regs, cycles)
    sh2_context  *sub;       slave SH-2 context
    uint16_t     *sdram;     SDRAM -- PAL_SH, TEXT_C, TILEMAP_C live here
    uint16_t     *rom;
    s32x_video    video;     framebuffer + 32X VDP regs
    uint16_t      regs[];    system regs
    uint16_t      dreq_fifo[8];

So `--dump` is: parse `region:addr:len:file`, and at the `exit_after`
site write the bytes out before `exit(0)`. Match ares' argument spelling
so existing probe scripts port with a binary swap.

`--profile` is the same shape: a per-PC instruction counter on
`gen->mars->main` / `->sub`, dumped at the same point. **Emit the same
CSV columns ares does** (`cpu,pc,instructions`) or the existing analysis
scripts will not read it.

### 4.2 One bonus worth taking while in there

`debug.c:3180` implements a `symbols <file>` loader. Wiring `rom/s16.lst`
into it would let probes name `TEXT_C` instead of hexing `0x06026000`.

**That failure class is expensive here:** a wrong base address cost two
builds and one misread census in the 2026-09-16 arc, and
`m_main.c:1329`'s memory map exists only because grep is not a
free-space test.

## 5. Cross-check protocol — before any number is quoted

`LESSONS.md` lists eight instruments that lied inside a single arc. Every
one of them was believed first and checked afterwards.

**Do not point this at O-1 until all three agree on something already
settled.** Pick a figure whose value is not in dispute, and reproduce it
on ares, the rig, and BlastEm:

    agreement        BlastEm may be quoted, SCOPED to the transport
                     axis, with the instrument named in every claim.
    disagreement     that is the finding. Which one is wrong is a real
                     result and is worth more than the measurement you
                     wanted.

**The specific failure mode to avoid:** a BlastEm number appearing in a
card before this section has been run. If that happens, it is the
2026-09-16 pattern repeating with a new tool.

## 6. Known traps

**Never run it without `-b`.** `-d` alone opens an SDL window and blocks.
**TESTED:** `blastem -d -m 32x s16.32x` with piped stdin hung past a
120 s timeout and had to be killed. Same class as the MAME
screen/keyboard rule in `CLAUDE.md`.

**`-h` does not list `-b`.** Trust `blastem.c:429`, not the usage text.

**It is a separate SH-2 timing model from ares, not a refinement of it.**
Numbers from the two are not comparable without the section 5
cross-check, and a BlastEm cycle count is not an ares cycle count.

## 7. Section-5 result, 2026-09-23 (second pass): boots after a one-line core fix; the gate FAILS on the figure

The first pass (same day, earlier) stopped at "the SH-2 side never
boots". The cause was found with a per-instruction trace patched into the
generated core: BlastEm's `sh2_reset` (sh2.cpu, `sh2_reset`) does not
clear the prefetch buffer. The 68K G-BIOS pulses the SH-2 reset
(`0xA15100` 0x0082 -> 0x0081 -> 0x0083, at 68K MCLK 1127686 and 2673300)
while the master is in the BIOS delay loop at 0x196-0x19A. On the
restart at 0x140 the core consumed the stale `prefetch_next` = 0x8BFC,
the loop's `bf`, which taken from 0x140 lands on 0x13C -- the BIOS
trap. No interrupt was involved (none was ever due; candidates 1-3 of
the handoff were all wrong). Fix, in `sh2.cpu` `sh2_reset`, before
`ocall periph_reset`:

    prefetch_full = 0
    delay_slot = 0
    did_mem = 0

The patch is kept as `docs/design/blastem-sh2-reset.patch`, and the tree is
now a git fork, `github.com/mholzinger/blastem` (branch `main`; the
upstream Mercurial snapshot is the base commit, tagged `hg-0c61d0d95463`;
the fix and the `--dump` hook are the first two commits on top; then the
headless frame-count fix, a cache reset on SH-2 reset, and the sub-interrupt
recompute typo, 2026-09-23, all verified by SDRAM dumps). `make` regenerates `sh2.c` from `sh2.cpu` (Makefile:452; ~5 min, 31 MB
of generated C under LTO). With it, `-b 300 -m 32x`: bprof3 master at
0x020474B2 (cart), slave at 0x06037Bxx (SDRAM), 54,426 non-zero SDRAM
bytes; Space Harrier master 0x060015B8, slave 0x0600016C, COMM1-7 live,
55,378 bytes. Both CPUs are past the handshake. "Boots" now holds.

Two more core defects seen in passing, NOT fixed, impact unmeasured:
`sh7095_reset` (sh7095.c:11) leaves the cache enabled across a reset;
`s32x_68k_sysreg_write` (32x.c, `case S32X_INT_CTRL`) recomputes the
SUB interrupt on `mars->main`.

**`-b N` was not N frames -- FIXED in the fork (commit a4be2d9, "vdp:
count one frame per frame in headless mode"): headless never set
`pushed_frame`, so the frame-complete test fired twice a frame. New
`-b 301` reproduces old `-b 600` byte for byte; `-b 700` now reads game
vint 672 against ares 674. Every `-b` value quoted below section 8's
table was taken on the old count. Original measurement: between `-b 700` and `-b 760` the
SH-2 advanced 80.64 M cycles = 26.9 M MCLK = 30 NTSC frames, and the
port's 68K vint counter (WRAM 0xFFB0F0) read 325 at `-b 700` where ares
`--frames 700` reads 674. One `-b` unit is half a frame (genesis.c:642
counts `elapsed` VDP frames; vdp.c increments `frame` at two sites,
2254 and 3109; which one double-counts is unverified). Align on the
game's own vint counter, never on N.

## 8. The section-4 figure on all three, aligned on the game's vint counter

Figure: master SH-2 window cycle, level-1 attract demo, BODYPROF rom
`rom/night/bprof3.32x`, `bprof[]` at SDRAM 0x06003544, 60 game vints.
ares frames 700-760 = vints 674-734; BlastEm `-b 1400`-`1520` = vints
673-733 (same scene: display-gate mailbox 0xA0, 2 blanks on both).
Compare script: `/tmp/blastem-eval/xbp_compare.py`.

    per window (lines, 45.8 FRT ticks/line)   ares      BlastEm
    windows in 60 vints                          60           60
    cycle, ticks/window                       12001        12004
    launch                                      7.3          8.3
    master half (the FB blit)                  26.6         20.0
    slave pickup wait                           9.4          7.2
    apply_cram                                  9.8         13.6
    publish->ack                               15.8         10.6
    ack->walker slice end (tail)               59.9         85.8
    slice end->next pickup (idle)             131.9        116.5

(Retaken on the fixed count, `-b 700`-`760`: identical per-window
values, vints 672-732.) The FRT tick rate agrees to 0.03% (the SH-2 clock is right). Cadence
agrees (one window per game vint on both). The FB-write-bound term
does not: BlastEm's master half is 0.75x ares. The rig's reading of the
same term on its own instrument (RIGBLIT, LESSONS 2026-09-23) is 67-86
lines against ares 45-47, i.e. 1.6x ares. BlastEm moves the OPPOSITE
way from the hardware. Its FB write waits (32x.c:1006-1009) do not
reproduce the FPGA's write rate; they undercharge even ares's stall
model. 68K side at the same vint: packets consumed 596 (ares) vs 557,
entry rejects 41 vs 84, held vints 41 vs 36 -- the transport also lands
differently on the 68K side.

**Verdict: disagreement, and the one that is wrong for our purpose is
BlastEm.** ares and the rig bracket the truth from below and above on
the FB write rate; BlastEm sits below ares. STATUS: NOT AN INSTRUMENT
for the transport axis. It is a working third 32X emulator with a boot
fix and a `--dump`, usable for logic cross-checks (it agrees with ares
on the FRT clock, the cadence and the window count), not for FB-write
timing. Section 3's costs are now measured, not hypotheses: the burst
fill and FB wait models exist and are too cheap. `--profile` and the
symbol loader (handoff section 6.3) were not built; the instrument did
not earn them.

## 9. Second cross-check, 2026-09-23 evening: the picture mismatch was a BlastEm 68K stall, and the fork now brackets ares and the rig

Section 8's "BlastEm undercharges FB writes" was measured on a WRONG
PICTURE. Chasing why BlastEm's slave stored zero sprite groups (sbuf rows
0-144 empty and frozen across frames, DIAG[50] landed words half of
ares's) went through and killed, in order: partial DREQ landings (the
line's transport is FBXPORT, the FIFO is idle: FIFOSTAT all zero), the
data cache (cache-off confounds timing), 68K FB writes dropped under FM
(FBXSTAT: zero drops), an FM race. The 68K's program counter at exit
(M68KSTAT) settled it: the 68K never left the port's vint shim, and the
arcade program's work RAM was frozen (1150 bytes, unchanged 700->760).
Cause: BlastEm spun the 68K until FM dropped on EVERY 68K access to the
VDP registers, palette or framebuffer windows (32x.c `s32x_68k_read`,
`s32x_fb_read_*`, `s32x_overwrite_write_*`: `while (FM) { m68k->cycles
+= MAX_SH2_CYCLES/3; sync }`). The shim polls FS at 0xA1518A and reads
the FB staging under FM by design, so each such access cost the 68K the
rest of the SH-2's window and the game starved. The adapter does not
stall: IF.sv `MD_VDP_SEL && ADCR.FM -> VDP_DTACK_N <= 0` (cycle
acknowledged at once, no VDP transaction); ares returns open bus. Fork
commit "32x: never stall the 68K on FM". After it, frames 700-760:

                        ares    BlastEm (any wait)
    windows in 60 vints   60      59-60
    master groups      10494      10250
    slave groups       12328      11799
    game RAM changing    292        333 bytes

Same picture. Then the framebuffer-write knob (`BLASTEM_FB_WAIT`, SH-2
clocks per 16-bit FB write, upstream = 0) moves a real figure. Master
blit lines per call (BLITPROF ticks/calls/45.8), the RIGBLIT quantity:

    wait      0     3     7    10    12    14  | ares | rig (LESSONS)
    700-760  35.6  47.4  62.4  73.7  80.7  88.1 | 46.8 | 67-86
    ratio    0.74  0.97  1.27  1.51  1.66  1.82 | 1.00 | ~1.6

Wait 3 reproduces ares (its 13.6 cycles a longword). Wait 11 reproduces
the rig, and holds on two more spans: 1500-1560 ratio 1.60 (ares 46.5 vs
BlastEm 75.8 lines/call), 3400-3460 ratio 1.49 (45.3 vs 90.0 with more
groups per call). Groups per call agree with ares within 3% on every
span, so the same bytes are being moved.

**What the knob is.** A proxy, not a model. The MiSTer RTL (VDP.sv
`VDPFIFO` 4 entries, `FIFO_FB_WAIT` 5 -> 6 system clocks a word at 53.7
MHz; SH7604 BSC.sv CS2 cycle T0/T1/TW/T2 with WCR1 = 0x0055 one wait;
IF.sv SH_VDP handshake ~3 clocks) says the FPGA's framebuffer write path
is FASTER than ares's stall model, not 1.6x slower. So the rig's 1.6x
lives elsewhere: the blit's sprite-buffer reads through sdram.sv (CAS 2,
tRCD 2 at 107 MHz, single-access writes) and the core's pipeline on
external stores are the candidates, unmeasured. Wait 11 reproduces the
EFFECT on the blit; it does not say which cycles the FPGA spends.
LESSONS' "the FPGA's framebuffer write rate" should read "the FPGA's
blit rate": the attribution to FB writes came from the same instrument
and is not supported by the RTL.

**Section-5 verdict, revised:** ares and BlastEm(wait 3) agree on the
figure; BlastEm(wait 11) and the rig agree on it; and the rig-vs-ares
ratio is reproduced on three spans. BlastEm is now a usable third
instrument for the transport axis, SCOPED: quote it as "BlastEm, wait N"
with N stated, use wait 3 to cross-check ares-side logic and wait 11 to
rank FB-bound cards the rig's way, and take the rig's three launches
before believing any ranking. Its `-b N` is real frames since fork
commit a4be2d9. It still models NO adapter-bus contention between the
CPUs (a COMM poll costs the other side nothing), so the 0.28 vs 0.063
lines/word hardware figure is outside it until an arbiter derived from
IF.sv exists.
