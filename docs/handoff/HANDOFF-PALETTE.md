# HANDOFF — MiSTer palette arc (2026-09-08 ~04:30)

Session ran ~02:00-04:30. Ship line is CLEAN and untouched:
`rom/s16.32x`, stamp c79131d3+, `_end = 0x060135c8`. Nothing committed.

## READ FIRST: what is solid vs what is not

SOLID, independent of any screenshot:

1. **PEN gates 32X CRAM writes.** srcref/S32X_MiSTer rtl/32X/VDP.sv:170
   gates palette access on PEN; :403/:405 assert PEN only during VBLK,
   during HBLK (H_CNT 0x159..0x016), or with the display mode off. A
   palette write during active scan is SILENTLY DROPPED on silicon.
   **`sh_src/m_main.c` cram_paint/cram_set have no such guard** and run
   inside the render window (lines ~20-190 = active scan). That is a
   real defect whatever the screen shows.
2. **The FRT is ~46 ticks per scanline** (~12000/vint over 262 lines).
   I sized a wait at 1200 ticks calling it "a scanline"; it was 26.
   Check every constant in this area against 46.
3. **ares DOES model PEN.** DIAG[20]/[21] show constant deferrals on a
   headless run. This class of bug is testable LOCALLY; it does not need
   a hardware round trip per iteration.
4. **PALPEN v5 is free on ares**: 50.5% vs the ship line's 49.7%, CRAM
   stores up 51% once the drain was in the right place.
5. **flip_span requires FM=1** (its own header, m_main.c ~5389), which is
   why the deferred-flip fix (FLIPDEFER) cannot commit at the top of
   vblank — there is no FM there. Measured 3.4% speed; see LOOP27 12.

NOT SOLID:

- **What the magenta/green picture on the MiSTer actually is.** It looks
  exactly like the two colours the abdraw probe hammered (CRAM 1 =
  0x7C1F, CRAM 2 = 0x03E0), but it persists in roms that never write
  them, and a rom that hammers YELLOW/RED instead did not change it.
  Residue, mis-modelled display, or stale capture — undecided.
- **Whether ANY of our palette writes land on hardware.** Never measured
  directly; only inferred from colours, which is why five probes in a row
  failed to separate the hypotheses.

## THE NEXT PROBE (unbuilt, and it is the right one)

**Have the master READ CRAM BACK.** Write a known value to a CRAM entry,
read 0x20004200 back, compare against cram_mirror, publish the verdict.
This is the same readback shape that made `s16_abread` decisive for the
framebuffer question — it answered "does the write land" directly instead
of inferring from pixels. Do that for the palette and the arc unblocks.

## RIG HAZARDS — both cost time tonight

- **Stale captures.** Two MiSTer screenshots were byte-identical to an
  earlier rom's frame (md5 72f2caf6 appearing under two different rom
  names). Never trust a single capture without a freshness marker.
- **The blue-border marker does not work for full-screen 32X content.**
  BOOTTAGBLUE paints the MD backdrop, which only shows where the 32X
  layer does not cover. Any future marker must be INSIDE the 32X layer.
- **~/bin/aresrom** pointed at an empty app bundle (no executable) since
  Aug 21; rewritten this session to pick the first bundle with a real
  binary and exec it directly. If desktop ares misbehaves, check which
  binary it reports on stderr.

## FLAGS ADDED THIS SESSION (all default OFF, ship line clean)

    MISTERBOOT=1   slave SDRAM warm-up + slave cache off (was leaking
                   into every build; gated this session)
    FLIPDEFER=1    deferred flip - BUILT AND FAILED, see LOOP27 12
    PALVBL=1       palette flush at the flip - FAILED on hardware, 22
    PALPEN=1       PEN-gated palette drain, in-window + post-flip - the
                   live candidate, ares-clean, hardware unproven
    BOOTABDRAW/ABREAD/ABBOTH/TAGBLUE/PALTEST   hardware probes
    BOOTABDRAWM/K  bisect halves of ABDRAW

## DOC CORRECTIONS MADE

- LOOP27 **10b**: recovered the full 18-rom probe ladder from Mike's
  session transcript (/tmp/loglog.txt), including the milestone entry 10
  had lost — `s16_68kdraw` PUT A FRAME ON THE SCREEN, red bar, meaning
  the gate had the 32X output off and forcing mode 1 revealed it.
- LOOP27 **13**: the words-vs-bootfix diff HANDOFF-MISTER parked as the
  restart point is DONE. Both roms carry the SAME warm-up stub,
  instruction for instruction. The only difference in the slave boot path
  is BOOT_SHSTAGE. One-variable pair built: s16_fpga_stage /
  s16_fpga_nostage, neither run yet.
- HANDOFF-MISTER.md arc B corrected: "the 32X FB layer never reaches the
  screen" is too strong; it reaches it when the 68K drives it.

## MY ERRORS TONIGHT, so the next session does not repeat them

1. Shipped an unobservable experiment (PALVBL rom with no BOOTGATEOFF —
   the gate holds the 32X output off in-game, so black was guaranteed).
2. Sized a PEN wait 26x too long without doing the tick arithmetic.
3. Anchored two patches to the same line, so both flush call sites landed
   after the flip and the in-window drain was never built — three roms
   measured the wrong thing.
4. Told Mike a no-bar result meant "not bank parity" when it did not.
