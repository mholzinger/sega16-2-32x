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

## 10. Where the rig's 1.6x lives, from the MiSTer RTL (loop item 3, 2026-09-23 evening)

Target: the master's blit costs ~10.5 FRT ticks a 32-byte group on the
rig (RIGBLIT/BLITPROF) against 6.53 on ares; 1 tick = 32 SH-2 clocks, so
~336 vs ~209 SH-2 clocks per group. Counted so far, with the source:

**Framebuffer stores (fast).** VDP.sv 210-245: a 4-entry write FIFO
(`VDPFIFO`, lpm_numwords 4) drains one 16-bit word per `FIFO_FB_WAIT`
5 -> 6 system clocks at 53.7 MHz (32X.sv 76-104: CLK_CNT 0-5 per VCLK,
CE_R at 1/3/5 = the 23 MHz SH-2 clock). IF.sv 973-993: the SH-2 access
is latched on CE_F and released on `VDP_ACK_N`, ~3 clocks. BSC.sv
250-290: a CS2 access is T0 -> T1 -> TW (WCR1 = 0x0055 from the BIOS
table at 0x348: 1 wait, `GetAreaW` = 01 -> `WAIT_CNT` 0 then `WAIT_N`)
-> T2, about 4 SH-2 clocks per 16-bit store when the FIFO is not full.
A longword store is two bus cycles (CS2 is 16-bit, `AREA_SZ`).
**And the core has no write buffer:** SH_core.sv 113 `PC_STALL =
(MA_ACTIVE & BUS_WAIT) | ...` with `BUS_WAIT = CACHE_BUSY` (SH7604.sv
240), and BSC.sv T1 clears `DBUSY` only when `!BUS_WE_LATCH`, so a store
holds the data bus, the cache unit and the pipeline until T2. Eight
longword stores a group = 16 cycles of ~5 clocks = ~80 clocks.

**Sprite-buffer reads.** A data-cache miss is a 4-longword burst
(CACHE.sv 565-578: `IBBURST <= 1`, `IBADDR` = line base + next long,
`IBUS_READARRAY`). The SDRAM cycle (BSC.sv 330-420) with MCR = 0x0AB8
decoded through SH7604_pkg.sv 151-166 (MSB first: TRP 0, RCD 0, TRWL 0,
TRAS 01, BE 0, RASD 1, AMX2 1, SZ 0, AMX 11, RFSH 1, RMD 0): 16-bit
SDRAM, page mode, `RCD_WAIT_CNT` = 1 + RCD = 1. A 16-byte line fill is
TRAS + TRCAS (1 wait, then `WAIT_N`) + 8 x TRD = ~12 SH-2 clocks at the
core, PLUS whatever `WAIT_N` adds: 32X.sv 360 `SHWAIT_N = IF_WAIT_N &
~SDR_WAIT`, and S32X.sv 713-731 puts the 32X SDRAM in DDR3 through
`ddram.sv` (`S32X_SDR_WAIT = ddr_busy`), which keeps one 16-byte line
per channel (ddram.sv 53-54, 77-85; `mem_chan` 0 for both SH-2s). So a
line fill is one DDR3 burst plus seven hits; the DDR3 access latency is
board-side and not in this RTL. Two line fills a group.

**Sum so far:** stores ~80 + reads 2 x (12 + DDR wait) + loop overhead
~10. With a DDR wait of 10 clocks that is ~155 clocks a group: BELOW
ares's 209 and less than half the rig's 336. The counted paths do not
reach the measurement, by more than the 20% the brief allows. So the
1.6x is NOT in the FB store path (confirmed) and, unless DDR3 latency is
~90 clocks (3.9 us) per line, not in the data reads either.

**Uncounted, next reads:** (1) instruction fetch -- the blit is LOCKCODE
(m_main.c 7830); `cache_purge()` runs every window; if the locked way is
purged too, every fetch of the loop is a line fill through DDR3, which
ares never charges (it counts instruction cycles only); CACHE.sv
`IBUS` path and CCR.TW handling decide it. (2) the slave's concurrent
DDR3 traffic on the same `mem_chan` evicting the one-line cache between
the master's bursts (ddram.sv 77). (3) refresh: BSC.sv `RFS_REQ` with
MCR.RFSH = 1, TRFS1/2 (`RFS_WAIT_CNT` 3) per RTCOR = 0x59 period. (4)
the actual DDR3 latency figure from the MiSTer framework (sys/ddram),
which this tree does not contain.

**Instruction execution was missing from the sum above.** The group loop
is ~40 instructions (blit_half is 544 B, 34 lines, `rom/s16.lst`); at
one clock each with load-use stalls that is ~60 clocks, and on ares it
is the whole non-FB cost (ares: ~209 = 13.6 x 8 FB-stall clocks (109) +
~100 instruction clocks). Revised RTL count per group: instructions ~60
+ store stalls ~80 (16 CS2 cycles, pipeline held each time, no write
buffer) + 2 line fills x (12 + L) with L the DDR3 latency in SH-2
clocks (unknown; 5-10 plausible, doubled when the slave's fills queue on
the same `ddram` channel) = ~175-195. Refresh is negligible: RTCSR 0x08
(CKS = phi/4), RTCOR 0x59 -> one TRFS1/TRFS2 (~5 clocks) every ~356
clocks, 1.4%.

**Conclusion: not located by reading; bracketed.** The counted paths
reach ~180-195 clocks a group. The rig measures ~336. The FB store path
is ~80 of the total on the FPGA and is NOT where the extra ~150 lives.
The candidates for the remainder, ranked by what the record already
shows: (1) instruction-fetch misses -- the master's instruction working
set is 5.4x the 4 KB cache (CARD-CACHELOCK.md, ares --profile), every
fetch miss on the FPGA is a DDR3 line fill with the pipeline held
(SH_core.sv 113), and ares charges nothing for fetches; the rig already
showed presented frames drop 27 -> 21 when the cache is halved (TW only,
CARD-CACHELOCK "RIG RESULT"), i.e. the rig IS miss-bound; (2) the DDR3
latency L itself, unmeasured; (3) both CPUs' fills serialising on
`ddram` channel 0. Consequence for the design record: LESSONS'
"framebuffer write rate" floor is a BLIT-rate floor, and the lever the
RTL points at is instruction locality (keep the group loop resident:
CACHELOCK without its per-purge verification, or place the loop so its
sets are not walked by the sbuf stream), not fewer FB bytes.

**The measurement that settles it (rig, one launch each, not tonight):**
`make line RIGBARCODE=1 BODYPROF=1 RIGBLIT=1` with (a) the loop in the
locked ways (CACHELOCK=1 with cachelock_install's readback removed,
CARD-CACHELOCK "What has to change" 1), (b) the READ-COST probe
(m_main.c 8405: one load per row, same stores), (c) NOBLIT_PROBE. Barcode
byte 5 (blit lines) on each against the 67-86 baseline decomposes the
336 into fetch / data-read / store terms directly. Until then, wait 11 in
the fork reproduces the rig's blit rate and nothing more.

## 11. Adapter-bus arbiter from IF.sv (loop item 1, 2026-09-23 evening): built, and it finds almost nothing to arbitrate

What IF.sv actually serialises between the CPUs: only the cart-ROM path
(`ROM_ST`, 795-910: one state machine, SH-2 word ~2 clocks with burst
words under one grant, MD access ~3 clocks + the board ROM wait, SH-2
first at RS_IDLE). The system registers are NOT arbitrated -- the MD
path (298-460) and the SH-2 path (486-640) are independent always-blocks
with no wait, so an SH-2 COMM poll costs the 68K nothing in the adapter.
The VDP port is shared (`VDP_A/VDP_DO/VDP_ACK_N`, 915-1000) but
partitioned by FM: with FM set the MD is acknowledged without a
transaction, without it the SH-2 never reaches the port.

Fork commit "32x: cart-ROM arbiter behind BLASTEM_BUS_ARB": hooks on the
68K's 0x880000/0x900000 windows and the SH-2's cart region share a
busy-until timestamp (MCLK); occupancies 8 MCLK per MD access, 5 per SH-2
word; a CPU never waits on its own burst nor on an access the slice
scheduler stamped in its future. ARBSTAT prints accesses and waits.

Result on the line rom (rigblit_bl, 760 frames, wait 11): 4.05M 68K cart
accesses, 3.18M SH-2 cart words; 111 68K waits (403 MCLK total), 55 SH-2
waits. Same picture as the knob off (master groups 9795 vs 10125, 60
windows, blit 76.7 lines/call). Analytic bound from the traffic itself:
SH-2 holds the path 4.2K words x 5 MCLK = 2.3% of a frame, the 68K 5.3K
accesses x 8 = 4.7%; expected wait per access = other side's utilisation
x half its mean hold, i.e. < 0.5% of either CPU's frame. The slice
scheduler hides some collisions, but the bound is what the RTL allows.

**Verdict:** adapter arbitration, as the RTL defines it, is negligible
for this rom. The hardware figure the decompile thread cited (a tight
COMM0 poll making the 68K's FIFO push 4.4x slower, 0.28 vs 0.063
lines/word) is not adapter contention: COMM reads are not arbitrated at
all. In the FIFO era it was most plausibly the SH-2's OWN external bus
shared between its CPU (uncached COMM reads) and its DMAC draining the
FIFO -- BSC.sv arbitrates CBUS/DBUS requests against `RFS_REQ` and each
other, and BlastEm leaves it as "TODO: DMAC/CPU contention" (sh7095.c
386). Under FBXPORT there is no DMAC in the transport, so the effect is
moot for the line. The knob stays, default off; nothing in the record
should quote it as a cost.

## 12. ares-style trace hooks in the fork (loop item 2, 2026-09-23 evening)

Fork commit "headless: --trace-comm, --trace-flip, --trace-dreq FILE":
the three CSVs with ares-headless's exact columns (its `--help` was the
spec, and a 120-frame ares run on bprof3.32x the format sample:
`frame,source,comm,value,v,h`; `frame,event,source,select,vcounter,
deferred`; `frame,event,source,value,v,h`). Sources m68k/shm/shs; v,h
are the MD beam at the event (BlastEm: `vcounter`, `hslot*2`; ares: its
own V/H counter -- compare by frame and order, not by beam value).

Check, bprof3.32x, 120 frames: comm 268,764 rows (ares 275,352), the
slave's COMM3 heartbeat ~30K rows a frame on both, the master's writes
on COMM0/1/4/7 present on both; flip 55 rows (ares 95) as write/flip
pairs with the deferred flag; dreq empty on both (FBXPORT). BlastEm's
timeline starts a few frames later than ares's (its boot handshake lands
later; the slave heartbeat begins at frame 12 against ares's 4), the
same offset the vint counter shows, so align traces on the game's own
events (the first master COMM0 write, or the vint counter), never on N.

Use: `blastem -b N -m 32x --trace-comm c.csv --trace-flip f.csv rom` from
the eval dir; diff against `ares-headless --frames N --trace-comm ...`
after aligning. This is the logic tie-breaker the record asked for in
section 8; it does not make any timing claim.

## 13. The rig probe: where the master's blit lines go (2026-09-23 late, four roms, one launch each)

Recipe from section 10, run self-service (tools/mister_push.sh, 20
screenshots 12 s apart per rom, tools/rig_barcode.py; spd2 relaunched
after). Barcode byte 5 = the master's last blit_half in lines, byte 6 =
groups stored / 8. Values per attract scene, all captures listed:

    scene            baseline           read-cost (1 load/row, 80 stores/row)   no-blit   no-audit
    level-1 demo 3   67 [64,65,69,71]   68 [64,65,71,71]                        1         60 [52..65]
    level-1 demo 5   74 [67,67,80,80]   68 [65,68,69,69]                        1         58 [53..62]
    scores           42 [42,42]         44 [43,45]                              1         40 [37,42]
    level-2 demo     59 [31..81]        61 [46..81]                             1         59 [40..74]

The baseline reproduces the record (step 5: 80/67 on both attract
passes, deterministic per position; scores 42; level-2 31-81).

What the four say:
1. **Loads are not the term.** Replacing 80 sprite-buffer loads a row by
   one changes nothing (67 -> 68). The sbuf line fills through DDR3 that
   section 10 suspected are not where the lines go.
2. **The row body is the whole cost.** Skipping it leaves 1 line.
3. **The audit reads are ~15%.** BLITNOAUDIT: 67 -> 60, 74 -> 58.
4. **Stores at the RTL rate explain the read-cost rom.** 112 rows x 80
   longwords = 17,920 word writes in ~59 lines (67 minus the audit) is
   4.8 SH-2 clocks a 16-bit store, the BSC CS2 cycle of section 10.
5. **The baseline stores 2.5x fewer words yet takes the same time**, so
   the work it does INSTEAD costs the same: visiting every group (8
   loads, the zero test, the mask bookkeeping) for the ~60% of groups it
   then skips. 1,120 groups visited a half x ~35 clocks + 444 stored x 16
   words x 4.8 clocks + audit ~10 lines = ~70 lines. That is the rig's
   67-74.

So on the FPGA the blit is stores (at ~4.8 clocks a word, FASTER than
ares's 6.8) plus per-group visiting overhead plus the audit, and the
"1.6x slower than ares" is not one slow path: ares charges the stores
more and the visiting less (no fills, no store-stall on the pipeline),
and the two mis-charges do not cancel. The RTL count in section 10 was
right per stored group and wrong about how many groups the loop
touches. Instruction-fetch misses are NOT needed to explain the number
and are back to "unmeasured", not "likely".

**Lever, from the measurement:** the visited-but-skipped groups are ~40%
of the blit on hardware; a row-level (or band-level) emptiness mask that
avoids visiting them would buy up to that, more than any store-side
saving. Fewer FB bytes still helps at 4.8 clocks a word, i.e. ~0.5 lines
per 32-byte group stored.

**Not yet closed:** the store-all probe (store every group, skip none)
would pin the visiting cost directly; it needs a knob in m_main.c. The
cache-locked variant overflows .ramtext with the barcode flags on.
