# HANDOFF — MiSTer FPGA bring-up + the 32X-native 60 Hz arc (2026-09-08)

Session ran two arcs back to back: (A) the 32X-native 60 Hz speed/fidelity
work on ares, and (B) the first-ever bring-up on real 32X hardware (MiSTer
FPGA). Read LOOP27 entries 1-11 for the blow-by-blow; this is the state
and the unfinished edges.

## READ FIRST — the tree is dirty and one change touches the SHIP line

Nothing is committed. Almost everything is behind probe flags EXCEPT one
thing that leaked into the default build and must be dealt with:

- **`Makefile` ~line 584: `ifndef SLVCACHEON` adds `--defsym SLV_CACHE_OFF=1`
  to EVERY build**, and `sh_src/mars_start.s` now has a PERMANENT slave
  SDRAM warm-up (the heartbeat stub, copied to 0x0603FA00 and run before
  the jump to _s_main). Both were added chasing the MiSTer slave hang.
  THEY DID NOT FIX IT (see arc B) and they change the shipping SH-2 boot
  path: slave cache is now OFF and the warm-up runs on every rom, ares
  included. ares still boots and plays with them in, but this is unproven
  weight on the ship line. DECISION NEEDED: revert both (set the default
  back to cache-on, drop the warm-up) OR keep them gated behind a flag
  that is OFF by default. Recommend: gate them behind `MISTERBOOT=1` and
  default OFF until the hang is actually solved, so the ares ship line is
  clean.

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
2. **Black screen once booted.** The 32X framebuffer LAYER doesn't reach
   the display while the pipeline runs; the MD text plane does. Gate and
   mode register RULED OUT (BOOT_GATEOFF, BOOT_MDMODE both black).
   Suspect: blit target-bank parity vs displayed bank under the per-vint
   flip = the same flip question as arc A's blocker. Next probe (unbuilt)
   must read the DISPLAYED bank right after a real flip, blit-parity-aware
   (the no-flip probes were unsound).

## Rom inventory (all in rom/, none committed)
- `s16.32x` — SHIP line, build cf83ac94 (the pre-session control). Untouched.
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
