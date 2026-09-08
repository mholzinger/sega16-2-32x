# OVERNIGHT LOOP — working state (started 2026-09-08 ~06:20 / 10:20 MiSTer clock)

Mike is asleep. Mandate: debate my own conclusions, challenge everything,
use every resource. Full MiSTer access confirmed.

## RIG — verified working this session

    deploy   scp rom/X.32x root@mister.office.local:/media/fat/games/S32X/probe.32x
    launch   curl -s -X POST http://mister.office.local:8182/api/games/launch \
               -H 'Content-Type: application/json' \
               -d '{"path":"/media/fat/games/S32X/probe.32x"}'
             **NOT VERIFIED — see LOOP27 entry 49.** /tmp/ACTIVEGAME is
             written by mrext itself and proves nothing about the core.
             The MiSTer MAIN BINARY IS NOT RUNNING (no `MiSTer` process),
             so /dev/MiSTer_cmd has no reader, no screenshots are
             possible, and launches may be no-ops. Check /tmp/remote.log
             for a "game started" line as the real signal, and restart
             MiSTer main before trusting any hardware result.
    shot     ssh root@mister.office.local "echo screenshot > /dev/MiSTer_cmd"
             (Mike's `mshot`. NOT firing right now — no new file, API returns
             empty fields. Suspect the display is off/asleep. RETRY each loop;
             if it never works, fall back to ares-only work.)
    fetch    scp root@mister.office.local:/media/fat/screenshots/S32X/<f> .
    ares     ~/src/ares-debug/build_macos/headless-ui/Release/ares-headless
             --frames N --input discover/inputs/play_level1.csv
             --screenshot F:out.png --dump sdram:0x28000:0x100:diag.bin
    build    make ship-us <FLAGS>   (ship rom must end clean: _end < 0x06019000)

## WHERE THE HUNT STANDS (LOOP27 entries 27-43)

MEASURED ON HARDWARE, trustworthy:
  - vints ~60/s (Mike's watch, entry 38)
  - game frames ~3/s  => ~95% miss vs ~50% on ares (entry 40)
  - the 68K's FB consume finishes DEEP IN THE PICTURE (RED) where ares
    finishes in vblank (GREEN) (entry 42)
  - palette transport master->68K is byte-exact (entry 37)
  - the visible picture is the MD PLANE, not the 32X layer (entry 30)

FROM WORKING CODE (entry 43): no shipping 32X program has the 68K read
the framebuffer in bulk. 240p suite = command server, bulk from CART ROM.
Cannonball = 122-line pad reader, zero FB access.
**CORRECTED (entry 47): we do NOT read hundreds of FB words per vint.**
The shipping R60+NT_WRAP path already DMAs everything straight from the
FB; the 68K reads only ~20-40 header words. The copy loop I cited was the
LEGACY #else arm. The suspect is now the FB-SOURCED DMAs themselves
(~71 cells/vint, measured on ares), not 68K reads.

## THE LOOP'S JOB, in priority order

1. CHALLENGE. Every conclusion above is mine and several of tonight's were
   wrong. Re-derive from the docs and the two source trees. Specifically
   doubt: (a) that the consume is the dominant cost rather than one of
   several; (b) that entry 30's "32X layer absent" survives entry 43's
   reading; (c) the ~3 fps figure, which came from timestamps not a watch.
2. BUILD THE FIX on ares: MD-plane records delivered by VDP DMA sourced
   from the FB, 68K issuing register writes only. Gate behind a flag.
   Ares-verify every step (render + gameplay_speed + counters).
3. RETRY the MiSTer capture path each iteration.
4. WRITE DOWN everything, including dead ends, in LOOP27 + this file.

## RULES FOR MYSELF THIS SESSION

  - Check every probe against ares BEFORE it goes near hardware. Three of
    tonight's roms were unobservable or unsound by construction.
  - Read a VALUE, never infer from a colour, when a value is available.
  - Never leave the ship rom dirty: `make ship-us` at the end of any
    build sequence, and verify _end.
  - Do not commit.
