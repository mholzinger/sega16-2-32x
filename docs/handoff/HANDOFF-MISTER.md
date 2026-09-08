# HANDOFF — MiSTer FPGA bring-up + the 32X-native 60 Hz arc (2026-09-08)

Session ran two arcs back to back: (A) the 32X-native 60 Hz speed/fidelity
work on ares, and (B) the first-ever bring-up on real 32X hardware (MiSTer
FPGA). Read LOOP27 entries 1-11 for the blow-by-blow; this is the state
and the unfinished edges.

## READ FIRST — the ship-line leak is CLOSED (2026-09-08 02:06)

Nothing is committed. The one change that had leaked into the default
build — slave cache OFF on every rom plus the permanent slave SDRAM
warm-up stub — is now **gated behind `MISTERBOOT=1`, default OFF**.
Both were added chasing the MiSTer slave hang, neither fixed it (arc B),
and both were unproven weight on the ares ship line.

- `Makefile`: the `ifndef SLVCACHEON` blanket is gone. `MISTERBOOT=1`
  now sets `--defsym MISTER_BOOT=1` and implies `SLVCACHEOFF=1`;
  `SLVCACHEOFF=1` alone is still the cache-only probe knob.
- `sh_src/mars_start.s`: the warm-up call site, its literal pool and the
  `_slvstub_*` heartbeat body are all inside `.ifdef MISTER_BOOT`.

Verified on a `make ship-us` build (BUILD c79131d3+ 2026-09-08 02:06:24):
slave `_secondary_cctl` writes `#17` (0x11, purge + cache ON) again, no
`_slvstub`/`_slvprime` symbols in `rom/s16.lst`, region guard
`_end = 0x060135c8` (< 0x06019000). A `MISTERBOOT=1` build was checked
too: `#16` (cache off) and the stub both come back.

NOTE: that rebuild OVERWROTE `rom/s16.32x`, which had been the
pre-session control binary (BUILD cf83ac94+). The new `rom/s16.32x` is
the clean ship line from the current tree; the old binary is not
recoverable byte-for-byte (it was built from a dirty tree).

SEPARATE, PRE-EXISTING: a bare `make` does not compile — `md_hold` /
`md_hold_seen` are declared at `md_src/md_main.c:491`, inside the
`#ifdef MD_BG` region opened at line 397, but used unguarded at line
3423. Every ship build defines MD_BG (MDBGALL=1), so `make ship-us`
is unaffected; only the flagless `make` in CLAUDE.md's build section
breaks. Not touched.

## ARC A — 32X-native 60 Hz (ares) — REAL PROGRESS, awaits a play pass

Goal: run the game at one game-frame per vint (60 Hz) instead of two.
Chain of fixes, all behind flags, all in `rom/s16_txtwram_opt1.32x`:

1. **TXTWRAM** (LOOP27 4): the two text writers at the top of the 68K
   pass (credit line 0x3AAE, health bar 0x4D54) stage in the WRAM text
   mirror instead of the framebuffer; the shim copies their footprints to
   FB text staging at FM=0. Removes the ~83-line FM spin. Walk speed
   49.7 -> 66.4%, idle 48.5 -> 57.3%. Tables in tools/game_altbeast.py
   (`TXT_WRAM_WRITERS`, per-writer P1/P2 selector for the health bar).
2. **Sprite pair map at 60 Hz** (LOOP27 6, option 1): at one cycle/vint
   every new sprite set hits the late claim, which failed for lack of a
   free CRAM pair -> sets drew in the shadow ramp (red silhouettes /
   missing actors). Fix = LATESTEAL0 (steal an age-0 pair whose owner
   left this snapshot, ≥9 live) + LATEKEEP (rebuild keeps late pairs) +
   DRAWADOPT (draw consults pr_key when the map says none). Ramp draws
   893 -> 114 (ship line 19). SPRMDFREE and SLVPAIR exist, measured,
   NOT in the opt1 rom.
3. **Torn landings / frozen sprite list** (LOOP27 7-8): the freeze was
   pushes into an UNARMED DMA after a window overran the vint and the
   V-ISR bailed "stale" without consuming the announce. Fix = ARMGATE:
   master arms at the announce from its idle loop (and clears COMM4 at
   announce time so a stale echo can't pass); 68K pushes only after the
   0xA001 echo. Wolf-script tears 172 -> 1. Cost: ~2.5% of vints ship no
   packet (announce serviced late) — refine by servicing the announce in
   the master's busy waits too.

**THE ONE REMAINING 60 Hz BLOCKER (LOOP27 9):** with all the above, the
framebuffer only FLIPS on ~17% of vints (ship line 41% = 30 Hz), because
the K2FREE edge guard (m_main.c ~5531) DECLINES any flip that misses the
38-line vblank, and at one game-frame/vint the post (~27 lines) + pre-flip
span (~51) miss it. Not load (slave 17% busy). **Fix to build:** replace
"decline a flip outside vblank" with a request that LATCHES at the next
vblank — which is exactly what the FPGA RTL does (srcref/S32X_MiSTer
rtl/32X/VDP.sv: `FS <= FBCR.FS` only when `VBLK`). The ares edge guard was
an emulator approximation; the RTL confirms hardware defers, not tears.
This is the next real task for 60 Hz and it now has an RTL ground truth.

Gates for arc A: `tools/gameplay_speed.py ROM --extra 0x3A7D8:40` (speed +
SPR_LATE counters — A/B sprite work by COUNTERS, never speed: builds
differing by census code alone span 49-74%), `tools/attract_parity.py`,
`tools/mdstatic_gate.py --frame 350 --frame 1321`, `tools/frame_timeline.py`
(the per-vint 68K clock, added this session). `rom/s16_txtwram_opt1.32x`
awaits Mike's ares play pass; the last one showed the frozen sprites that
ARMGATE then fixed — needs a re-pass.

## ARC B — MiSTer FPGA bring-up — CLOSED by Mike, do not re-hunt

**Altered Beast BOOTS AND RUNS on real 32X FPGA hardware** (MiSTer
S32X_MiSTer core, RTL cloned to srcref/S32X_MiSTer). Proven working on
silicon via a colour-beacon bisection (dozens of one-flag `BOOT_*` roms):
68K boot, bank-3 (>2 MB) reads, both SH-2s, DREQ landings (word 20 =
0x6000, ares-identical), flips, 68K FB writes, MD-plane DMA, FRT rate,
V-ISR, compose/blit content, MD text plane on screen.

TWO PROBLEMS, both OPEN, Mike CLOSED the investigation (memory:
mister-boot-closed — do not chase unless he re-opens):

1. **Slave SDRAM first-fetch hang.** The slave hangs on the first fetch
   after `jmp _s_main` unless primed. The OLD gated stub in
   `rom/s16_words.32x` BOOTS the slave (confirmed: stripes render, all 3
   ROM-storage modes). The PERMANENT warm-up (`rom/s16_bootfix` /
   `rom/s16_bootbeacon`, heartbeat stub + cache off, now in mars_start.s)
   does NOT (beacon RED). **Restart point if re-opened: DIFF those two
   builds** — same stub, one boots and one doesn't, so the difference is
   findable, not a fresh hunt. ROM Storage (SDRAM/DDR3/Auto) RULED OUT.
2. **Black screen once booted.** CORRECTED 2026-09-08 from the session
   transcript (LOOP27 entry 10b): the 32X framebuffer layer DOES reach
   the screen on this core — `rom/s16_68kdraw.32x` put a bar up, with
   the title text behind it, when the **68K** wrote the FB and flipped
   from its own side. It does not reach the screen when the **SH-2
   pipeline** drives it. Gate and mode register are still RULED OUT
   (BOOT_GATEOFF and BOOT_MDMODE both black) — and note those two being
   black while 68kdraw is VISIBLE is the sharpest clue in the arc: the
   discriminator is who writes and who flips, not which switch is set.
   The parked suspect (blit target-bank parity vs displayed bank under
   the per-vint flip) is still plausible but is no longer the only one.
   Next probe (unbuilt) must read the DISPLAYED bank right after a real
   flip, blit-parity-aware (the no-flip probes were unsound), and must
   draw its markers at ROW 8 OR BELOW — rows 0-7 are off Mike's display.

   LOOSE END, never reconciled: `s16_gateint` reported RED (the master
   never sees the game's display-on flag) but `s16_words` then showed
   landed word 20 = 0x6000 on both MiSTer and ares. 0x6000 has no bit
   15, so if bit 15 were that flag ares would not see it either — yet
   ares works. Either the flag is not bit 15 of word 20 or the gateint
   probe read the wrong thing. The session closed on the byte-identity
   and left the RED unexplained.

## Rom inventory (all in rom/, none committed)
- `s16.32x` — SHIP line, REBUILT 2026-09-08 02:06 (c79131d3+) with the
  MiSTer leak gated out. Replaces the cf83ac94+ pre-session control.
- `s16_txtwram_opt1.32x` — arc A stack (TXTWRAM+LATESTEAL0+LATEKEEP+DRAWADOPT+ARMGATE). For Mike's ares play pass.
- `s16_txtwram_census.32x` — opt1 + SPR_LATE census counters.
- `s16_words.32x` — MiSTer probe; BOOTS THE SLAVE (arc B restart control).
- `s16_bootfix.32x` / `s16_bootbeacon.32x` / `s16_bootfixA/B` — the failed permanent warm-up + variants.

## Tools added this session
- `tools/frame_timeline.py` — per-vint 68K timeline (ares-debug
  `--trace-access lo:hi:name:fa:fb`, beam-stamped).
- ares-debug (~/src/ares-debug, UNCOMMITTED): `--trace-access` +
  `--trace-access-out`, `vdpBeam()` in system.cpp, and v,h columns on
  `--trace-comm` and `--trace-dreq`. Built into
  build_macos/headless-ui/Release/ares-headless.

## First thing next session
Decide the SLV_CACHE_OFF / warm-up default (gate it OFF for the ship
line — see top). Then either: arc A flip-latch fix for 60 Hz (has RTL
ground truth now), or leave arc B closed. Do not fire MiSTer probes
unless Mike re-opens it.
