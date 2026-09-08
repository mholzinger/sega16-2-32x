# HANDOFF — the DREQ FIFO is the frame
2026-09-08, end of an all-night session. This is the complete state.
Read this file first; it supersedes HANDOFF-MISTER.md, HANDOFF-PALETTE.md
and the state sections of HANDOFF-HARDWARE.md.

---------------------------------------------------------------------
## THE ANSWER

**A 68K word write into the 32X DREQ FIFO costs ~2.4 scanlines on real
hardware. The same word into the 32X FRAMEBUFFER costs ~0.1.**

    68K -> 32X DREQ FIFO     20 words    48 scanlines
    68K -> 32X FRAMEBUFFER   20 words     2 scanlines      24x
    whole r60_push, ~52 words            99 scanlines      of 262

Measured on Mike's MiSTer with an exact-value instrument, same build,
same vint, only the destination differing. On ares the same 20 FIFO
words cost 3 scanlines, which is why a decade of tuning against the
emulator never saw it.

**That is the ~95% hardware miss rate.** The handler is ~120-200 lines
of a 262-line frame and the push is 99 of them.

## THE FIX

Carry the packet through the framebuffer instead of the DREQ FIFO; the
master reads it from there. The saving is proven. The remaining work is
PLACEMENT — see "THE JOB" below.

---------------------------------------------------------------------
## HOW WE GOT THERE (every hardware measurement, in order)

All exact unless marked. Rig fully autonomous — no human in the loop
except where noted.

    vints arriving                    ~60/s        (Mike's watch)
    game frames advancing             ~3/s         => ~95% miss
    the consume's DURATION            8-24 lines   (ares <8)
    handler entry -> consume start    <16 lines    (ares 4-7)
    handler TAIL, biggest stage       the DREQ push, 4/4 captures
    the push's length                 96-160 lines (ares 44-47)
    FIFO-full spin residual           2600 = NEVER FULL (ares 2600 too)
    push, which internal stage        the regs stage = ship 20 words
    regs stage: setup vs words        the WORDS, 4/4
    regs stage exact                  48 lines     (ares 3)
    regs stage + 8-line delay         55-58        (delay added 8, dodged 0)
    whole push, full packet           99 lines
    whole push, cut to 20 words       63 lines     (~1.1 lines/marginal word)
    20 writes to a COMM register      32-80 lines  (bucketed)
    20 writes to the FRAMEBUFFER      2 lines
    FB free-region sentinel mask      0 of 4 survived

## WHAT IS ELIMINATED BY MEASUREMENT (not by argument)

The palette path. The FB-sourced VDP DMAs. The entire consume.
Everything before the consume. DMA-to-CRAM. 68K framebuffer READS (fast:
2 lines per 20 words). FIFO-full stalling. The master's drain rate.
Packet size as the primary lever. Push timing / contention with the
master. The display gate. The mode register. Bank/flip parity as the
black-screen cause.

## CLAIMS I MADE AND THEN WITHDREW — do not rebuild on these

  1. "32X CRAM writes are silently dropped outside hblank/vblank."
     WRONG. ares and the RTL agree they STALL until PEN; neither drops.
  2. "DMA-to-CRAM does not land on this core." WRONG — RTL vdp.sv:728
     and :765: CRAM_WE and VRAM_WE have identical gating.
  3. "Our 68K reads hundreds of FB words per vint; the fix is to move
     MD-plane records to FB-sourced DMA." WRONG for the shipping build:
     R60+NT_WRAP already DMAs everything; the 68K reads ~20-40 header
     words. I had read the legacy `#else` arm.
  4. "The FB consume eats the frame." WRONG — it is 8-24 lines.
  5. "Words are the whole lever on hardware." OVERSTATED — cutting
     52->20 words saved 36 of 99 lines, real but not the mechanism.
  6. "Deploy and launch are autonomous, verified." The launch check was
     reading /tmp/ACTIVEGAME, which mrext writes itself. Use
     /tmp/remote.log.
  7. "The MiSTer's display is asleep." It was not — MiSTer main had died.
  8. PALVBL and PALPEN as black-screen fixes. PALVBL made hardware
     strictly worse; PALPEN v1 starved the window with a wait sized 26x
     too long. Current PALPEN is ares-clean and free but fixes a
     different defect (the PEN stall), not this one.

---------------------------------------------------------------------
## THE JOB (next session starts here)

**1. FIND ~300 BYTES OF FB THAT SURVIVE A FLIP.**
The documented 2KB hole at 0x1E800 is FULL: md_pkt B (1472B) + palette
(64B at 0x1EDC0) + SAT (512B at 0x1EE00) = exactly 2KB. Visible pixels
end at 0x200 + 224*320 = 0x11A00, which is where md_pkt A begins; A ends
~0x11FC0.
A sentinel probe of 0x12000/0x14000/0x18000/0x1C000 returned **0 of 4
surviving** — almost certainly BANK PARITY (master writes the draw bank,
a flip intervenes, the 68K reads the other). Redo it bank-aware.
**Likely answer: write the packet to BOTH banks.** At 2 lines per 20
words, double-writing is ~4 lines against the FIFO's 48 — still 12x, and
it removes the free-region hunt and the parity reasoning entirely.

**2. SEQUENCE IT AGAINST FLIP AND FM.** The 68K writes at FM=0, the
master reads at FM=1. The window protocol already orders that; the
buffer must not move under the reader.

**3. TORN-PACKET DETECTION.** Magic/sequence word written LAST, exactly
as md_consume already does with 0xB6B6.

**4. REPOINT THE DREQ MACHINERY.** ARMGATE's 0xA001 echo, the lost-push
belt, the torn-landing census and the flip hold are all built on DREQ
semantics; they get simplified or removed.

---------------------------------------------------------------------
## THE RIG (fully working, all gotchas)

    deploy   scp rom/X.32x root@mister.office.local:/media/fat/games/S32X/NAME.32x
    launch   curl -s -X POST http://mister.office.local:8182/api/games/launch \
               -H 'Content-Type: application/json' \
               -d '{"path":"/media/fat/games/S32X/NAME.32x"}'
    VERIFY   ssh root@mister.office.local 'tail -1 /tmp/remote.log'
             -> must show "game started: Sega32X/NAME.32x".
             **NEVER trust /tmp/ACTIVEGAME — mrext writes it itself.**
    shot     ssh root@mister.office.local "echo screenshot > /dev/MiSTer_cmd"
    fetch    scp root@mister.office.local:/media/fat/screenshots/S32X/*NAME*.png .
    reboot   ssh root@mister.office.local reboot     <- Mike: always allowed

GOTCHAS THAT COST HOURS:
  - **If MiSTer main is not running** (`ps aux | grep MiSTer`), the cmd
    FIFO has no reader: writes vanish, blocking writes HANG the ssh
    session, no screenshots, and launches may be no-ops. Just reboot.
  - **Screenshots can come back STALE** — three this session were
    byte-identical to an earlier rom's frame (md5 72f2caf6 under three
    names). The filename carries the rom name; check it, and prefer a
    probe whose output cannot be confused with a previous one.
  - **A verdict painted on MD CRAM 0 is invisible** when the MD plane
    covers the screen. Flood all 64 entries at vint top instead.
  - **On ares, MD-palette floods are hidden BEHIND the working 32X
    layer**, so an ares screenshot of a flood probe reads BOOT_SHSTAGE's
    backdrop, not the verdict. **Verify flood probes on ares by dumping
    WRAM, never by screenshot.**
  - WRAM 0xFFA242 and 0xFFA248 are already in use; census counters there
    returned garbage. Audit before picking addresses.
  - MD CRAM READ command is `(addr << 16) | 0x0002`; `0x0020` is VRAM
    read and returns a constant.

## THE VALUE INSTRUMENT (use this, not buckets)

`BOOTVALUE=1` floods the MD palette with a number encoded as a colour —
MD CRAM is 9 bits, exactly enough for 0-255:

    R = d & 7 , G = (d >> 3) & 7 , B = (d >> 6) & 3

Decode from a capture histogram: `d = (b>>5<<6)|(g>>5<<3)|(r>>5)`.
One screenshot, one exact number. `BOOTVALUETOTAL=1` switches it from the
regs stage to the whole push. The decoder is
scratchpad/decode.py in this session; it is six lines, rewrite it.

Four-way colour buckets wasted several rounds — ORANGE spans 32-80 lines,
wide enough to hide the entire effect of a change.

---------------------------------------------------------------------
## TREE STATE

  - **Ship rom CLEAN**: `rom/s16.32x`, build c79131d3+,
    `_end = 0x060135c8`, and `.build_flags` carries NO probe defines.
  - **NOTHING COMMITTED.** All work is uncommitted in the working tree.
  - Dirty: Makefile, md_src/md_main.c, md_src/md_start.s,
    sh_src/m_main.c, sh_src/mars.c, sh_src/mars_start.s, plus files
    already dirty before this session.
  - Probe flags added, all default OFF: MISTERBOOT, FLIPDEFER, PALVBL,
    PALPEN, DMACENSUS, BOOTABDRAW/ABDRAWM/ABDRAWK/ABREAD/ABBOTH,
    BOOTTAGBLUE, BOOTMDPAL, BOOTPALTEST/PALWRAM/PALPEEK/PALSHOW/PALRAMP/
    PALDIRECT, BOOTCRAMCHK, BOOTMOTION, BOOTMOTIONGAME, BOOTSPAN,
    BOOTSTAGEMAX, BOOTPRECONSUME, BOOTTAIL, BOOTPUSHLEN, BOOTPUSHWHERE,
    BOOTSPIN, BOOTNOPOLL, BOOTREGLEN, BOOTCOMMTIME, BOOTPUSHCUT,
    BOOTPUSHDELAY, BOOTSETUP, BOOTVALUE, BOOTVALUETOTAL, BOOTFBTIME,
    BOOTFBFREE.
  - 20 probe roms staged on the MiSTer at /media/fat/games/S32X/
    (1_span2 through J_fbtime, K_fbfree).
  - `MISTERBOOT=1` gates the slave SDRAM warm-up and slave-cache-off that
    had leaked into every build at the start of the session.

## LOG

LOOP27 entries 27-66 carry the full account including every dead end.
The method lesson, which cost about fifteen hardware round trips: every
probe that INFERRED FROM A PICTURE settled nothing; every probe that READ
A VALUE landed. Ask hardware only what ares cannot answer, and check the
probe on ares first — that caught an unobservable rom, a latched-white
verdict, a wrong CRAM command and a bad probe index before they reached
the MiSTer.
