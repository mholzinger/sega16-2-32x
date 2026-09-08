# What makes this port run on real 32X silicon

2026-09-08. Written because a day of hardware time bought the contents
of this file and none of it is recoverable by reading the code.

This is the REPRODUCIBLE document: every claim below names the build
that produces it, the observation that confirms it, and the file and
line where it lives. If a statement here has no build command next to
it, treat it as unproven.

Companion documents: `docs/handoff/HANDOFF-DREQ.md` (the transport
finding as a handoff), `docs/log/LOOP27.md` entries 10-13 and 65-70 (the
working log, including every dead end).


---------------------------------------------------------------------
## 0. REPRODUCING THE ACCEPTED BUILD

    git checkout mister-keeper-20260908
    make ship-us FBXPORT=1
    # -> rom/s16.32x   _end = 0x060135c8, no BOOT_* defines

Accepted by Mike on the MiSTer 32X core, 2026-09-08 14:17. Rebuilding
from the tag differs from the accepted binary by 36 bytes, all of them
the build stamp (git hash + timestamp at 0x2489D8 and 0x3FFFD4).

Deploy and run:

    scp rom/s16.32x root@mister.office.local:/media/fat/games/S32X/KEEPER.32x
    curl -s -X POST http://mister.office.local:8182/api/games/launch \
         -H 'Content-Type: application/json' \
         -d '{"path":"/media/fat/games/S32X/KEEPER.32x"}'
    ssh root@mister.office.local 'tail -1 /tmp/remote.log'   # MUST say
    # "game started: Sega32X/KEEPER.32x"


---------------------------------------------------------------------
## 1. THE TWO HARDWARE FACTS THE PORT DEPENDS ON

Neither is in any document. Both were derived on hardware, both are
invisible under emulation, and the port does not run on silicon without
respecting both.

### FACT 1 — The slave SH-2 must warm up SDRAM it did not write

The slave's first instruction fetch from a region the MASTER wrote
wedges it on this hardware. Cache on or off, identically. A
register-only spin of any length does NOT clear it. What clears it is
an SDRAM loop **the slave writes itself and then executes** — repeated
instruction fetch plus data access, from its own copy.

    sh_src/mars_start.s:545     the explanation
    sh_src/mars_start.s:559     the copy + jsr, before `jmp _s_main`
    sh_src/mars_start.s:621     the stub itself (9 instructions)
    stub lives at 0x0603FA00    slave sentinel floor, free at boot

The stub spins reading COMM14 until the 68K shim posts 0xB007, ticking
COMM6 as it goes so the 68K can distinguish "slave alive" from "slave
hung", then returns. `_s_main`'s own 0xB007 wait then passes at once.

**PROOF** — four roms, one tree, one flag apart, 2026-09-08 14:10:

    build                                       MiSTer
    make ship-us FBXPORT=1 BOOTGAMERATE=1       BLACK  (1 colour)
    ... MISTERCACHE=1                           BLACK  (1 colour)
    ... MISTERWARM=1                            BOOTS  (18 colours)
    ... MISTERBOOT=1                            BOOTS  (62 colours)

The warm-up is the whole fix; the slave cache-off does nothing.

**COST ON ARES: ZERO.** `tools/ares_diag_at.py ROM 200 400` returns
byte-identical counters with and without it — vints, packets consumed,
tile batches, NT chunks, tiles, rejects, every column.

So it is **default on** since this date. `NOSLVWARM=1` removes it;
`MISTERWARM=1` / `MISTERCACHE=1` isolate the halves (Makefile:912-934).

> Both the Makefile and mars_start.s previously said this stub did NOT
> fix the hang and that the default carried neither half. Both were
> wrong, and six black captures were spent rediscovering it. If you find
> a comment in this tree asserting a negative about hardware, check
> whether anyone ever ran the A/B.

### FACT 2 — The 68K cannot touch the 32X framebuffer at FM=1

FM (bit 15 of 0xA15100) arbitrates framebuffer ownership. At FM=1 the
SH-2 side owns it and **68K framebuffer writes do not land at all** —
they are not slow, not stalled, not partial. They are absent. 68K reads
there return nothing usable.

**PROOF** — `BOOTFBXFER=1` writes the same sequenced 13-word packet
twice per vint, at 0x12000 before the post (FM=0) and at 0x12040 inside
r60_push (FM=1); the master checks both for content AND for freshness
(the sequence must advance by exactly 1 every window) and reports a
saturating run length per region:

    hardware:  runA = 7 (saturated)    runB = 0    content-ok = 0
               FM=0 write lands and is read FRESH, same window, 7 of 7
               FM=1 write never arrives

Freshness is the whole design. With constant test values a
one-window-stale region reads as a pass, and the earlier sentinel probe
that lacked this check returned "0 of 4 survived" — which was a readback
through the FM=1 dead path, not evidence about those regions.


---------------------------------------------------------------------
## 2. THE COST OF THE TWO TRANSPORTS

Same build, same vint, only the destination differing. Measured with
the value instrument (section 4) on the MiSTer:

    68K -> 32X DREQ FIFO      20 words     48 scanlines   (~2.4/word)
    68K -> 32X framebuffer    20 words      1 scanline    (~0.05/word)
                                                          ~48x

    whole r60_push, DREQ route            99 scanlines of 262
    whole r60_push, FB route              57 scanlines

On ares the same 20 FIFO words cost ~3 scanlines. **That gap is why
weeks of tuning against the emulator never found this.** It is also why
a speed change here cannot be ranked on ares at all — only correctness
can.

Ruled out as the mechanism of the FIFO cost, each by measurement:
FIFO-full stalling (spin residual 2600 of 2600 = never full), the
master's drain rate, contention with the master (a deliberate 8-line
delay before pushing dodged 0 lines), packet size as the primary lever.
It is per-access and intrinsic. **What the RTL is actually doing there
is still unknown.**


---------------------------------------------------------------------
## 3. THE FB TRANSPORT PROTOCOL (FBXPORT=1)

The r60 packet crosses through the framebuffer instead of the DREQ
FIFO. The packet BUILDER is untouched — only the destination of its ship
primitives changes — so the master's harvest parses byte-identical
bytes and every length, tag and tear rule downstream still holds. That
was the design constraint, not an accident.

### Addresses — one source, both sides

    md_src/packet_fmt.h:168     FBX_PKT_MD  0x852000    68K view
    md_src/packet_fmt.h:169     FBX_PUB_MD  0x852800
    md_src/packet_fmt.h:170     FBX_PKT_SH  0x24012000  master view
    md_src/packet_fmt.h:171     FBX_PUB_SH  0x24012800
    md_src/packet_fmt.h:172     FBX_MAGIC   0xB600

Region choice: past the image (ends 0x11A00) and past md_pkt A (ends
~0x11FC0); 936 words = 0x750 bytes, ending well under md_pkt B at
0x1E800. The publish word sits clear of the packet.

### The sequence, per vint

    1.  68K, FM=0, BEFORE the post:  r60_push() builds and writes the
        packet into FBX_PKT_MD           md_src/md_main.c:2762
    2.  68K writes the length at FBX_PUB_MD+2, then the publish word
        0xB600|seq at FBX_PUB_MD — LAST  md_src/md_main.c:2008
    3.  68K raises FM, posts 0x2020
    4.  master, FM=1, window body: reads the publish word, checks the
        magic and that the sequence is NEW, copies `n` words into
        SPR_LAND, sets landed = n   sh_src/m_main.c:9504
    5.  the existing R60 harvest runs unchanged

**The ordering guarantee is the 68000 itself**: it completes writes in
program order, so a publish word written last cannot precede its own
payload. This is the same contract md_consume relies on in the other
direction with 0xB6B6. A stale or malformed publish yields landed = 0,
which is the pre-existing "no packet this vint" path — last frame's
records stand.

**No flip survival is required and no bank parity is involved.** The
68K writes and the master reads inside ONE window with no flip between
them; that is exactly what runA = 7 proves. Do not reintroduce the
both-banks write or the free-region hunt that earlier notes proposed.

### What must be disabled with it, and why

Two pieces of DREQ machinery are actively harmful on this route:

  - **The master's pre-blit landing wait** (`sh_src/m_main.c:9168`)
    polls TCR0 until it stops changing. On the FB route no DREQ transfer
    ever runs, TCR sits at R60_ARM, the break condition is unreachable,
    and the master burns its FULL 4000-tick bound every window. This
    alone made the first FB build (217 scanlines) WORSE than the FIFO
    (99). Disabled under FB_XPORT.
  - **dreq_rearm** (`sh_src/m_main.c:7248`) leaves DMAC0 armed with
    DAR0 = SPR_LAND — a loaded gun pointed at the packet just copied
    there. Skipped under FB_XPORT.

The 68K side likewise skips the DREQ length register write and the DREQ
enable (`*ctrl = 4`), and ARM_GATE's arm check becomes inert: there is
no DMA to arm.

### Why the push had to move ahead of the post

Because of FACT 2 — there is no FM=0 after the post. Push-before-post
was tried and reverted in August because the push was ~90 lines of FIFO
writes and the flip then never made vblank (8 flips in 1602 vints). At
~2 lines that objection goes away with the FIFO.


---------------------------------------------------------------------
## 4. THE MEASUREMENT RIG

The only channel off this hardware is a screenshot, ~1 minute per round
trip. Design every probe for that channel.

### The value instrument — read a number, never a picture

MD CRAM is 9 bits, three per channel: exactly enough to carry a byte.
Flood all 64 entries and one capture returns one exact value.

    encode   R = d & 7 ,  G = (d >> 3) & 7 ,  B = (d >> 6) & 3
    decode   d = (b>>5 << 6) | (g>>5 << 3) | (r>>5)

Decoding from a capture is a six-line histogram read; the most common
colour is the flood.

**BIAS EVERY VALUE OFF ZERO** (set a high bit that is always present).
A flooded d = 0 is a black screen and so is a machine that never
reached the flood — four captures were read as "the probe never ran"
when the truth was a missing build flag. A result that cannot be told
from a dead machine is not a measurement.

Four-way colour buckets, which every probe before 2026-09-08 used, are
useless here: the ORANGE bucket spanned 32-80 scanlines, wide enough to
hide the entire effect of any change worth making.

### Existing instrument flags

    BOOTVALUE=1       the regs stage (20 words shipped), in scanlines
    BOOTVALUETOTAL=1  the WHOLE push, entry -> records+tail shipped
    BOOTVALUESEL=1    the selection/compare phase alone
    BOOTFBXFER=1      the FB transport verdict (runA/runB/content)
    BOOTFBXT=1        the FM=0 FB write, in scanlines
    BOOTGAMERATE=1    game frames per 64 vints (see the caveat below)

### Traps, each of which cost hours

  1. **`/tmp/ACTIVEGAME` is written by the launcher, not the core.** It
     reports a launch that never happened. Only `/tmp/remote.log`'s
     "game started" line is evidence.
  2. **If the core's main process has died**, the command FIFO has no
     reader: screenshot requests vanish, a blocking write HANGS the ssh
     session, and launches may be no-ops. `ssh root@... reboot`.
  3. **Captures can come back stale** — three in one session were
     byte-identical to an earlier rom's frame under three different
     names. Check the filename, and prefer probes whose output cannot be
     confused with the previous rom's.
  4. **A verdict on one palette entry is invisible** when something
     covers the plane. Flood all 64.
  5. **On ares the MD flood is hidden BEHIND the 32X layer.** Verify
     probes there by dumping WRAM (`ares-headless --dump wram:ADDR:LEN:file`),
     never by screenshot. Doing so caught an unobservable rom, a latched
     verdict, a wrong CRAM command and a bad probe index before they
     reached the hardware.
  6. **Never park a value on COMM8.** The master pends posts on COMM8
     being free; a probe that left 0xBBxx there permanently stopped the
     arm echo the push gates on, and the screen went black. Ride the
     channel like every other message and let the 68K clear it.
  7. **Never hand a BOOT_VALUE build to a human to judge.** It floods
     the palette every vint and STROBES against the game's own palette
     writes. Those builds exist to be photographed by a script.
  8. **Probe roms are `make ship-us BOOT<X>=1`** — the full ship flag
     set. A bare `make BOOT<X>=1` builds a different, non-booting
     configuration (it turns FM_GATE off and fails to compile).


---------------------------------------------------------------------
## 5. WHAT IS STILL NOT KNOWN

Stated plainly so nobody rebuilds on a guess:

  - **The port's frame rate is not measured.** Two instruments were
    built and both were wrong: the game's scene timer (0xFFF02A) runs at
    scene-dependent rates and in both directions, so it compared attract
    scenes rather than builds; and the game-IRQ4 completion counter at
    `fmgate_ret` reads 64/64 on BOTH transports, so the game enters and
    leaves its handler every vint either way. What stalls is the game's
    own frame advance INSIDE that handler, which neither counter sees.
  - **The claim that the DREQ push was costing the GAME its frames is
    therefore unproven.** The transport is 48x cheaper; that it was the
    thing the game waited on is inference.
  - **The real fps limiter is probably the FLIP, not the transport.**
    LOOP27 entry 9 measured flips landing on 17% of vints = ~10 Hz, and
    the observed rate on ares is ~9 fps. `FLIPDEFER=1` was built to fix
    it and is blocked: `flip_span()` requires FM=1 and there is no FM=1
    at the top of vblank (LOOP27 entry 12). The new FM ordering may
    unblock it — untested.
  - **Why a DREQ FIFO access costs ~2.4 scanlines** is unknown.
  - **Of the FB route's remaining 57 scanlines, only ~2 are transport.**
    The rest is the packet BUILD (selection/compare), now the largest
    single thing in the handler.
