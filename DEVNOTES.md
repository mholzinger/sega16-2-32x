# DEV NOTES — the LOOP14/15 era (2026-08-15..16)

One-screen summary of what this branch delivers, for anyone (including
future us) landing here cold. Full working logs with measurements and
negative results: `LOOP14.md`, `LOOP15.md`. Architecture: `ARCHITECTURE.md`.

## Where the port stands

- **Presentation is "nearly flawless" on ares** (Mike's words) in the
  MDBGALL configuration: MD VDP draws both tile planes, the 32X
  composes sprites/text/priority tiles over them. All the visual
  corruption classes that dominated LOOP12-14 are dead on hardware
  (verified by play passes + savestate audits).
- **Jitter/DREQ loss is CLOSED**: per-word FIFO polling on the 68K's
  DREQ push took poisoned packets from 1914/session to 1. Baseline is
  "nearly playable".
- **Remaining queue**: game speed (structural: the MD 68000 is 0.77x
  the arcade's 10 MHz before our vint handler takes its share; next
  lever is DREQ cadence redesign — the push is 3x oversampled for a
  20 Hz compose), then band tearing (compose all 3 bands from one
  latched snapshot), then cosmetics (sky palette quantization = §11,
  demoted to last by Mike).

## Canonical builds

    make                                          # shipping rom (32X-composed everything)
    make MDBGALL=1 BQCHUNK=1 CUTBLANK=1 NTWRAP=1  # the MDBGALL verdict rom (BUILD 989432e4 era)

Gates on every commit: parity statics title 2.44 / eyehold 3.37 EXACT
(shipping flavor); `grep ' _end$' rom/s16.lst` < 0x06019000; rom
stamped `normal`; Mike's ares play pass. NOTE: MDBGALL-flavor parity
anchors are a phase lottery (loads/animation race the capture) — judge
those by diff anatomy, not %.

## What landed in this era

1. **PG_STICKY** (folded into MDBGALL): the stale-tilemap-truth root
   fix. Marks enter the page watch, watch drops only after 12 quiet
   captures, broad loader marks pin deep watch 24 cycles. Law learned:
   capture-wide REQUIRES restore-wide (banks go incoherent otherwise).
2. **CUT_BLANK**: scene-cut claim storms blank stale-art cells instead
   of showing the previous scene's tiles; the fade covers the reveal.
3. **NT_WRAP**: the MD planes became wrapped 64x32 scroll surfaces
   (native-MD style) with per-strip full hscroll (exact per-band
   parallax for the first time) and **mirror-diffed rows** — only
   changed cells ship. 68K packet-consume: 47 -> ~15 lines/window
   (the WINSPAN meter in state_health). Includes a loss backstop
   (rotating force-full row) and reject-loss healing.
4. **Per-word DREQ push polling**: closed the FIFO word-loss race by
   construction. Cost ~13 lines of the ~32 NT_WRAP freed.
5. **Instruments** (tools/): cut_profile.lua, cut_snap.lua,
   frame_snap.lua, nt_dump.lua, arc_dump.lua, nt_audit.py,
   winspan_check.lua, WINSPAN meter in state_health.py, MDVERIFY
   decode. The parity harness + magic_smoke are unchanged.

## Dead ends of the era (never re-attempt; details in the LOOPs)

- PGROTOR (blind background truth re-verify): reads diverged FB banks
  outside the restore set — backgrounds visibly cycle on ares.
- Restore-narrow under PG_STICKY (eyehold 8 -> 27).
- Master post-ack idle pad for the DREQ race: the master has no slack;
  cost dropped frames ("jitter VERY high"). The 68K-side per-word poll
  is the correct fix.
- Fixed-address map is FULL: two slot collisions this era (mdp_s_used,
  and md_pkt's palette words under the master stack red-zone — stack
  dips >=576 bytes below 0x3F000). New arrays go in .bss; audits read
  addresses from the flavor-matched lst.
- Probe roms are frozen at their build commit — check which fixes
  postdate a rom before handing it over.

## Verification model (unchanged, works)

MAME + scripted inputs is the measurement rig (parity vs the real
arcade rom, deterministic attract). Mike's ares play pass is the
acceptance gate; ares is the hardware-truth proxy. Savestates decode
via tools/state_health.py and tools/bs9_audit.py. MAME cannot rank
SH2-side timing (its SH2 is ~3x fast) and cannot reproduce ares FIFO
word loss — those verdicts need an ares state.
