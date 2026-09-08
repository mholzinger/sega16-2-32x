# What makes this port run on real 32X silicon

2026-09-08. Reference document for the hardware behaviour this port
depends on. None of it is derivable from the source.

Every claim names the build that produces it, the observation that
confirms it, and the file and line where it is implemented. A statement
with no build command next to it is unproven.

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

Neither appears in any documentation. Both were derived on hardware,
neither reproduces under emulation, and the port does not run on
hardware unless both are respected.

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

Note: the Makefile and mars_start.s previously stated that this stub did
not fix the hang and that the default build carried neither half. Both
statements were incorrect; six captures were spent re-deriving the
result. Treat any negative claim about hardware in this tree as
unverified until the A/B is located.

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

The freshness check is required. With constant test values a
one-window-stale region reads as a pass. An earlier sentinel probe
without this check reported "0 of 4 survived"; that readback was
performed at FM=1 and is not evidence about those regions.


---------------------------------------------------------------------
## 2. THE COST OF THE TWO TRANSPORTS

Same build, same vint, only the destination differing. Measured with
the value instrument (section 4) on the MiSTer:

    68K -> 32X DREQ FIFO      20 words     48 scanlines   (~2.4/word)
    68K -> 32X framebuffer    20 words      1 scanline    (~0.05/word)
                                                          ~48x

    whole r60_push, DREQ route            99 scanlines of 262
    whole r60_push, FB route              57 scanlines

On ares the same 20 FIFO words cost ~3 scanlines. Consequence: speed
changes to this path cannot be ranked on ares, only correctness can.

Ruled out as the mechanism of the FIFO cost, each by measurement:
FIFO-full stalling (spin residual 2600 of 2600 = never full), the
master's drain rate, contention with the master (a deliberate 8-line
delay before pushing dodged 0 lines), packet size as the primary lever.
The cost is per-access and intrinsic. The RTL-level cause is unknown.


---------------------------------------------------------------------
## 3. THE FB TRANSPORT PROTOCOL (FBXPORT=1)

The r60 packet crosses through the framebuffer instead of the DREQ
FIFO. The packet builder is unchanged; only the destination of its ship
primitives differs. The master's harvest therefore parses byte-identical
data and all downstream length, tag and tear rules continue to apply.
This was a design constraint.

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
    DAR0 = SPR_LAND, i.e. targeting the region the packet was just
    copied into. Skipped under FB_XPORT.

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

**Bias every encoded value away from zero** (set a constant high bit).
An encoded d = 0 renders identically to a target that never reached the
flood. Four captures were misread as "probe did not run" when the cause
was a missing build flag.

Four-way colour buckets, used by every probe before 2026-09-08, are
unsuitable: the ORANGE bucket spanned 32-80 scanlines, which exceeds the
effect size of most changes under test.

### Existing instrument flags

    BOOTVALUE=1       the regs stage (20 words shipped), in scanlines
    BOOTVALUETOTAL=1  the WHOLE push, entry -> records+tail shipped
    BOOTVALUESEL=1    the selection/compare phase alone
    BOOTFBXFER=1      the FB transport verdict (runA/runB/content)
    BOOTFBXT=1        the FM=0 FB write, in scanlines
    BOOTGAMERATE=1    game frames per 64 vints (see the caveat below)

### Known failure modes of the rig

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
  7. **BOOT_VALUE builds are not suitable for visual assessment.** The
     flood runs every vint and beats against the game's own palette
     writes, producing a strobe. These builds are for script capture
     only.
  8. **Probe roms are `make ship-us BOOT<X>=1`** — the full ship flag
     set. A bare `make BOOT<X>=1` builds a different, non-booting
     configuration (it turns FM_GATE off and fails to compile).


---------------------------------------------------------------------
## 4b. TRUSTING AN SH-2 COUNTER

**Writes to DIAG slots above ~63 are silently discarded in this build.**
They read back residue: small, plausible numbers. Calibrated at `m_main`
entry, a site that runs exactly once, DIAG[62] reads 1 and DIAG[64] and
DIAG[84] read 0.

A zero-in-the-baseline scan does not prove a slot is free — a slot that
discards writes also reads zero.

Use `CEN` (0x2602FF00, m_main.c), and check `CEN[10]`, which is
incremented once at `m_main` entry: **if it does not read exactly 1, no
other slot in that run means anything.**

This cost a false alarm on 2026-09-08: the FB transport was reported at
0.2% packet delivery and is in fact at 100% (819 published, 819 taken, 0
bad, 0 stale, both lift positions).

## 4c. DELIVERY AND REFRESH, MEASURED

    FB transport delivery      819 published / 819 taken     100%
    lift before the flip       100%      lift after the flip  100%

    display refresh (900 frames, ares)
      DREQ FIFO      695 flips / 875 vints    79%   47.7 Hz
      FB transport   821 flips / 881 vints    93%   55.9 Hz

A vint may decline in the V-ISR (past the vblank edge) and then flip in
the window body; the frame is flipped late, not dropped. **The flip is
therefore not what limits the observed frame rate.**

---------------------------------------------------------------------
## 4d. THE 68000 CLOCK DEFICIT — THE GATE UNDER ALL THE OTHERS

The port runs the arcade's own 68000 program. The two machines do not
run it at the same speed, and the difference is fixed in hardware. From
MAME's machine definitions (`mame -listxml`):

    System 16B  maincpu  MC68000  10,000,000 Hz
    32X / MD    maincpu  MC68000   7,670,453 Hz

**Our 68000 runs at 76.7% of the arcade's**, so identical code takes
1.304x as long here before any port overhead is added.

What that does and does not imply:

  - It is NOT a 76.7% ceiling on frame rate. The ceiling is
    1 / (1.304 * U), where U is the fraction of a frame the ARCADE's
    68000 spends working. 60 fps is unreachable only if U > 0.767.
  - The port currently measures 82.0% game-frame rate, which is ABOVE
    the naive clock ratio. That is only possible if U is well under 1,
    i.e. the arcade game leaves most of its frame idle — so the clock
    deficit is not what limits us today. Port overhead is.

**U is not yet measured properly.** Sampling the arcade's PC once per
frame (`register_frame_done`) reports 99.6% idle, but that hook fires at
the END of a frame, exactly when the game is waiting, so the figure is
phase-biased and worthless as a magnitude. A sound measurement needs
either many samples spread across each frame or a debugger trace of one
frame counting instructions inside vs outside the idle loop at
0x003980..0x003990 (0x903980 in our rebased map).

The comparable measurement on our side already exists: the game-handler
length ring (V at entry 0xFFA380, V at rte 0xFFA300, md_start.s). The
same quantity on the arcade, in scanlines, would give the full picture:
excess over 1.304x is port overhead — cart-bus contention through the
adapter and our shim — rather than clock.

---------------------------------------------------------------------
## 4e. PAST 4 MB: THE SSF2 MAPPER WORKS ON 32X

Our cart is FULL — 4.00 MB used, zero trailing free bytes — so any
"bake it uncompressed" plan is a cart-size question. It has an answer,
and there is a shipping example.

**Doom 32X Resurrection v3.0/v3.1 is 5,242,880 bytes.** Its header says:

    console field   "SEGA SSF"          (not "SEGA 32X")
    ROM end         0x4FFFFF            5.00 MB declared

That is the SSF2 mapper. From the MiSTer RTL
(`srcref/S32X_MiSTer/rtl/CART/cart.sv`):

    if (rom_sz > 'h200000)                  // banking enabled over 2 MB
        ROM_BANK[VA[3:1]] <= VDI[4:0];      // 8 slots x 512 KB, 5-bit bank
    ROM_BANK_A = ROM_BANK_EN ? {ROM_BANK[VA[21:19]], VA[18:1]} : ...

Eight 512 KB slots, each selecting one of 32 banks — **up to 16 MB**.
Bank registers are the SSF2 ones at 0xA130F3..0xA130FF. Note also

    wire ROM_LIN_EN = (rom_sz > 'h400000) & ~ROM_BANK_EN & ~s32x;

the LINEAR over-4MB path is explicitly disabled for 32X carts, so
banking is the only route.

**It applies to the SH-2, not just the 68K.** `S32X.sv:666` drives the
cart module's address from either bus:

    CART cart ( .VA(!s32x_rom ? GEN_VA : S32X_CA), ... )

so an SH-2 cart read goes through the same ROM_BANK_A mapping. Art past
4 MB is therefore reachable by the compose path, not only by the 68K.

Verified on our own rig: the headless ares fork runs the 5 MB Doom
Resurrection image (exit 0, SDRAM live), so both rigs can carry it.

Our port does NOT use this today: the header says "SEGA 32X", ROM end
0x3FFFFF, and the "banked 0x900000 window" in patch_game.py is the 32X
adapter's own MD-side ROM window (bank 3 -> cart 0x300000), a different
mechanism. Adopting SSF2 means: the "SEGA SSF" header, the larger ROM
end, and bank writes at 0xA130F3+ around any access past 4 MB.

**WHO CAN PAGE, AND WHETHER IT IS SAFE — from the RTL, not assumed.**

`cart.sv` gates the bank write on `TIME_N`, and in `S32X.sv` TIME_N is
the ONE cart signal that is not muxed:

    .TIME_N(GEN_TIME_N)          // always the Genesis /TIME strobe
    .LWR_N(!s32x_rom ? GEN_LWR_N : S32X_CLWR_N)

So **only the 68K can write the bank registers.** The SH-2 has no TIME_N
and cannot page; it reads through whatever bank the 68K selected.

`s32x_rom` is not per-cycle arbitration — it is set once at ROM download
and never cleared, so on a 32X cart the cart bus is driven from the 32X
side permanently and the MD's cart cycles are PASSED THROUGH the 32X,
exactly as the real adapter sits between the MD and the cart.

The two sides are then serialised by the 32X's own ROM state machine
(`IF.sv`, ROM_ST / RS_IDLE / RS_SH_WAIT, SH_ROM_GRANT vs MD_ROM_WAIT +
MD_ROM_DTACK_N). One cart transaction at a time.

**Therefore there is no torn-read hazard**: a 68K bank write and an SH-2
cart read are separate, serialised bus cycles. The only hazard is
LOGICAL — repaging a 512 KB slot the SH-2 is currently reading code or
art from changes what its next fetch returns. The discipline is
ownership of slots, not timing.

**AND THIS IS THE CART-FETCH TAX.** Section 4d could not measure why 68K
cart fetches are expensive here. It is this state machine: SH-2 and MD
cart accesses take the bus one at a time, so the 68K's instruction
fetches from cart ROM queue behind the SH-2s' art reads — which is
exactly why r60_push was moved to RAMCODE and why the compose is
described as the heaviest cart reader. It is arbitration, not wait
states.

Still untested on the MiSTer: nothing in this section has been run,
only derived.

---------------------------------------------------------------------
## 5. WHAT IS STILL NOT KNOWN

Listed so that later work does not assume them settled:

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
  - **The fps limiter is NOT the flip.** That was the standing theory
    from LOOP27 entry 9 (flips on 17% of vints = ~10 Hz); a corrected
    census does not reproduce it — the framebuffer refreshes at ~56 Hz
    while the observed rate is ~9 fps. The limiter is the GAME's frame
    advance: it enters and leaves its IRQ4 handler every vint on both
    transports, and does not advance a frame each time. What blocks it
    inside that handler is unmeasured, and is the next thing to measure.
  - **Why a DREQ FIFO access costs ~2.4 scanlines** is unknown.
  - **Of the FB route's remaining 57 scanlines, only ~2 are transport.**
    The rest is the packet BUILD (selection/compare), now the largest
    single thing in the handler.
