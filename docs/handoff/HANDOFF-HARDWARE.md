# HANDOFF — the 32X hardware arc, consolidated and corrected
Written 2026-09-08 ~10:10 by the overnight loop. Supersedes the state
sections of HANDOFF-MISTER.md and HANDOFF-PALETTE.md, both of which
contain claims withdrawn below.

Ship rom CLEAN: `rom/s16.32x`, c79131d3+, `_end = 0x060135c8`, build
flags carry no probe defines. Nothing committed.

## RIG IS BACK (2026-09-08 11:55). Mike rebooted. MiSTer main pid 529,
## full autonomous chain working. **ssh root can `reboot` at any time** —
## four loop iterations sat blocked on this. Verify launches with
## /tmp/remote.log, never /tmp/ACTIVEGAME.
##
## RESULTS SINCE: the consume takes 8-24 lines on hardware (not the bug),
## and it starts within 16 lines of handler entry (not the bug either).
## The time is in the handler TAIL — post, DREQ push, flip-hold echo —
## i.e. where the 68K waits on the master. See LOOP27 53.

## HISTORICAL: the rig was broken and it may have been my fault

**The MiSTer main binary is not running** (LOOP27 49). No `MiSTer`
process; only remote.sh and system daemons. Therefore:
  - `/dev/MiSTer_cmd` has no reader — writes vanish, and blocking writes
    to it hang an ssh session;
  - **no screenshots are possible**;
  - **API launches are not verified** — `/tmp/ACTIVEGAME` is written by
    mrext itself and proves nothing. `/tmp/remote.log` is the real
    signal, and it shows no "game started" line after 10:13:26.

Main was alive at 10:13 and died after. My first automation attempts had
written malformed `load_core` paths and a bad `.mgl` into the FIFO
repeatedly, minutes earlier. **I cannot rule out that I killed it.**
Restart MiSTer main (or reboot) before trusting any hardware result.

## WHAT IS ACTUALLY KNOWN ON HARDWARE

Measured while the rig was working, and still standing:

  1. **It boots and runs.** Both SH-2s, DREQ landings byte-identical to
     ares, flips latch, 68K FB writes land, MD-plane DMA works, FRT rate
     correct, V-ISR fires. (LOOP27 10)
  2. **The slave needs an SDRAM warm-up ares never needed.** Real fix,
     now behind `MISTERBOOT=1`. (LOOP27 10, 13)
  3. **The visible picture is the MD PLANE**, not the 32X layer —
     `BOOTMDPAL` turned the whole screen red. (LOOP27 30)
  4. **Vints arrive at ~60/s** (Mike's watch on the colour wheel, 38).
  5. **Game frames advance at ~3/s** => **~95% miss rate**, against ~50%
     on ares. (40) The single most important hardware number we have.
  6. **The palette transport master->68K is byte-exact** — a known ramp
     arrived unshifted. (37)
  7. **Hardware has never displayed more than 9 distinct colours**;
     ares shows 76-119 on identical roms. (45)
  8. **The 68K's consume ends deep in the visible picture** where ares
     ends in vblank. (42) — but see the caveat in 44.

## WHAT I WITHDREW OVERNIGHT (do not rebuild on these)

  - **"32X CRAM writes are silently dropped outside hblank/vblank"**
    (entry 18). WRONG. ares and the RTL agree they STALL the SH-2 until
    PEN; neither drops. (27)
  - **"DMA-to-CRAM does not land on this core"** (45). Killed by the
    RTL: CRAM_WE and VRAM_WE have identical gating, same FIFO, same
    external-slot requirement. (46)
  - **"Our 68K reads hundreds of FB words every vint" and "the fix is to
    move MD-plane records to FB-sourced DMA"** (43). WRONG for the
    shipping build. R60+NT_WRAP already DMAs everything from the FB; the
    68K reads ~20-40 header words. I had read the legacy `#else` arm. (47)
  - **"Deploy and launch are autonomous, verified"** (44). Launch is not
    verified. (49)
  - **PALVBL / PALPEN as fixes.** PALVBL made hardware strictly worse
    (drain rode the flip, which rarely lands). PALPEN v1 starved the
    window with a wait sized 26x too long. Current PALPEN is ares-clean
    and free (50.5% vs 49.7%) but is a fix for a defect that entry 27
    reframed, not for the black screen.

## SOLVED 2026-09-08 ~13:00: THE DREQ FIFO IS THE FRAME

Measured on hardware with an exact-value instrument (LOOP27 63-65):

    68K -> 32X DREQ FIFO   20 words   48 scanlines
    68K -> 32X FRAMEBUFFER 20 words    2 scanlines      **24x**
    whole push, ~52 words             99 scanlines of a 262-line frame

The fix is to carry the packet through the framebuffer instead of the
DREQ FIFO. The saving is proven; the remaining work is PLACEMENT —
finding ~300 bytes of FB that survive a flip, or writing both banks
(still 12x cheaper). See LOOP27 66 for the four-item job.

Everything else in this arc is eliminated by measurement: palette,
FB-sourced DMAs, the consume, pre-consume, DMA-to-CRAM, 68K FB reads,
FIFO-full, master drain rate, packet size, push timing.

## HISTORICAL — THE OPEN QUESTION AS IT STOOD BEFORE THAT

At a workload ares completes comfortably — ~5.7 FB-sourced DMAs per vint
moving ~91 words, plus 20-40 68K header reads — hardware completes one
game frame in about twenty vints. Every VOLUME measurement says the
workload is modest (48). So the cost is **per-access arbitration**: the
32X framebuffer answering the MD VDP while both SH-2s contend for it.
**ares models none of that**, which is why no ares measurement finds it,
and why this arc has consumed a night of probes.

## THE ONE PROBE WORTH RUNNING FIRST

`rom/s16_span2.32x` — already on the SD card at
`/media/fat/games/S32X/probe.32x`. It buckets the consume's DURATION in
scanlines (V at consume entry 0xFFB0B0 vs V at end 0xFFA176):

    GREEN <8 lines   YELLOW <24   ORANGE <64   RED >=64

ares reads GREEN at every sample.
  - **RED on hardware** -> the consume genuinely is the frame, at a
    workload ares finishes in 8 lines. Per-access arbitration proven by
    elimination, and the fix is fewer/longer DMAs or moving the MD
    plane's source off the framebuffer (what both reference programs do).
  - **GREEN** -> the consume is innocent and entry 42's end-position RED
    was measuring something that runs BEFORE it. Next stamps to bucket:
    0xFFB0B2 (after scroll), 0xFFB0B6 (after cells).

## RIG NOTES EARNED THE HARD WAY

  - Verify a launch with `/tmp/remote.log`, never `/tmp/ACTIVEGAME`.
  - Screenshot filenames derive from the loaded rom — that is the
    freshness check. Three captures this session were byte-identical to
    an earlier rom's frame (md5 72f2caf6 under three names).
  - A verdict painted on MD CRAM 0 is INVISIBLE when the MD plane covers
    the screen. Flood all 64 entries at vint top instead (BOOTMDPAL
    proved it covers).
  - WRAM slots 0xFFA242 and 0xFFA248 are already in use — my census
    counters there returned garbage. Audit before picking addresses.
  - MD CRAM READ command is `(addr << 16) | 0x0002`. `0x0020` is VRAM
    read and returns a constant.
  - Check every probe on ares before hardware. It caught an unobservable
    rom, a latched-white verdict, a wrong CRAM command and a bad probe
    index — four wasted round trips avoided, and four more that were not.
