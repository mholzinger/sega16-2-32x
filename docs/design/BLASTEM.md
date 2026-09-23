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
