# DEV NOTES — through the LOOP16 era (2026-08-15..17)

One-screen summary of what this branch delivers, for anyone (including
future us) landing here cold. Full working logs with measurements and
negative results: `docs/log/LOOP14.md`, `docs/log/LOOP15.md`. Architecture: `ARCHITECTURE.md`.

## Where the port stands

- **Presentation is "nearly flawless" on ares** (Mike's words) in the
  MDBGALL configuration: MD VDP draws both tile planes, the 32X
  composes sprites/text/priority tiles over them. All the visual
  corruption classes that dominated LOOP12-14 are dead on hardware
  (verified by play passes + savestate audits).
- **Jitter/DREQ loss is CLOSED**: per-word FIFO polling on the 68K's
  DREQ push took poisoned packets from 1914/session to 1.
- **LOOP16 (the 2-window cycle) LANDED**: single-snapshot compose +
  the MD posting 2 windows per 3 vints — 68K handler mean 134 -> 110
  (game ~58% of the MD 68K), rejects 0.1%, Zeus consistent. Mike's
  A/B vs MAME: backgrounds and character pixel work SOLID.
- **The release bar** (memory release-bar-flawless): flawless =
  arcade parity in look AND MOTION, our methods/palette OK. Scale
  stepping is a must-fix.
- **LOOP17 (open): the sprite BAKE** — measured: 73% of sprite
  decode jobs (74-85% of cost) repeat per cycle; slave idles
  14,427 polls/cycle; 768KB cart free. Hybrid bake with live
  fallback, then cutscene-30Hz and 60Hz-plane-scroll experiments.
  docs/log/LOOP17.md is the handoff.

## Canonical builds

    make                                          # shipping rom (32X-composed everything)
    make MDBGALL=1 BQCHUNK=1 NTWRAP=1 WIN2=1 SPRTRUNC=1 \
         BLITSKIP=1 DIRTYROW=1 SPRBAKE=1 BANDSHIFT=16 \
         FBSPR=1 FBTEXT=1 CUT30=1
    # ^ 2026-08-20 (LOOP20): 20Hz IS DEAD - Mike: "kill the 20hz. keep
    #   working from 30, and we will make it to 60." FBTEXT reads text
    #   RAM in place (68K tail 46 -> 21 lines/vint, the largest single
    #   win of the project) and CUT30 drops the idle beat: a real 30Hz
    #   display on the transport-era handler budget. MAME is COLOUR-
    #   BLIND on this bundle (partial DREQ landings) - pixel-gate on
    #   N_fbtext, everything else on ares.
    # (superseded) make ... BANDSHIFT=16 FBSPR=1
    # ^ FBSPR folded in 2026-08-19 (LOOP20): the game's sprite upload
    #   writes FB staging directly and the SH-2 reads the list in place
    #   - one vint less sprite latency, sprite push 596 -> 92 words.
    #   Twice ares-verified; Mike: "VAST improvements in speed and
    #   smoothness." The write-discard hazard that forced the DREQ
    #   sprite push is extinct under the current FM envelope.
    # (superseded) make MDBGALL=1 BQCHUNK=1 NTWRAP=1 WIN2=1 SPRTRUNC=1 \
    #      BLITSKIP=1 DIRTYROW=1 SPRBAKE=1 BANDSHIFT=16
    # ^ THE canonical line (2026-08-19, LOOP18 close). CUTBLANK REMOVED:
    #   it punched 10,000+ black pixels into the MD plane at every level
    #   start (its "under the fade" premise does not hold there) and was
    #   driving the palette churn - pen drift 2166-3775 -> 782 without it.
    #   BANDSHIFT=16 rebalances compose master->slave: band drops 0.97 ->
    #   0.53/cycle. flip/blit skips 2.8-4.6% -> 0.1%. Handler mean 84.4,
    #   game ~68% of the MD 68K.
    # (superseded) make MDBGALL=1 BQCHUNK=1 CUTBLANK=1 NTWRAP=1 WIN2=1 SPRTRUNC=1
    # Current loop: docs/log/LOOP18.md (blit-bound era). docs/log/LOOP17.md is the log of
    # how we got here.
    # ^ THE canonical line (2026-08-17). SPRTRUNC folded in on the ares
    # verdict: 68K handler mean 106.9 -> 91.3, game ~59% -> ~65% of the
    # MD 68K, protocol clean (dreq_incomplete 0%, misaligned 1).
    # Its PIXELS cannot be judged in MAME — MAME does not model partial
    # DREQ landings, so it shows frozen sprites there by construction.
    # (Only the pixels: MD-side counters rank honestly on this build,
    # and the renderer pixel-gates fine built WITHOUT SPRTRUNC.) ares is
    # cycle-accurate and is the authority on our machine.
    # CHECK THE STAMP BEFORE BELIEVING ANY STATE:
    #   python3 tools/build_id.py show rom/s16.32x   -> SPRTRUNC

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
