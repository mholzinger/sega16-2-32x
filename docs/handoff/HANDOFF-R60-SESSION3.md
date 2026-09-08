# HANDOFF — R60 session 3 (2026-08-23 .. 2026-08-25)

Written at Mike's request after he flagged the late-session reasoning
as hallucinating. He is right to distrust it. This handoff separates
what was MEASURED from what was THEORIZED, flags the specific
conclusions that are suspect, and says how to re-establish ground
truth before building anything else.

## Current state

- Shipping rom: `rom/s16.32x`, BUILD `6633ecdc`, commit `6633ecd`,
  branch `rebuild60`.
- Canonical build line:
  `make MDBGALL=1 NTWRAP=1 SPRTRUNC=1 FBTEXT=1 PAL32=1 FMGATE=1
   R60=1 CUTBLANK=1 BANDSHIFT=32 BLITSKIP=1 DIRTYROW=1 SPRBAKE=1
   BLITSHIFT=72`
- Headless reference numbers (play2.csv, 3600f): cadence 1.04,
  skips 0, real tears (BAD1 remarks) ~126, deferrals ~1.1/frame,
  flips ~92% in-ISR, mask lies 0.
- Mike's live numbers track headless closely (his state dumps print
  everything; state_health gained lines: "blitskip mask lies",
  "deferrals by band R0/R1/R2").
- Evidence files preserved: `rom/purple_band.bs1.evidence` (a purple
  state from build a14eb8a7). His latest `rom/s16.bs1` (24952fa3) was
  also saved with purple on screen.

## SOLID (measured, replicated, or falsifier-tested — safe to build on)

1. **60 Hz transport holds.** Cadence 1.03-1.05 live across 8+ passes,
   zero frame skips, flips 90%+ in-ISR.
2. **Exact-length packet gate** (tag bits 9..0 = word count; harvest
   requires landed==declared). Killed the silent multiple-of-8-drop
   class that wedged sprite palettes (the blue-white player). Mike-
   verified: blue-white gone since.
3. **Bounded drain-wait** (harvest polls own DMAC TCR until declared
   length, 1200-tick cap). Tears 183→~105 on introduction; zero bus
   cost (on-chip register). Still in.
4. **Torn-packet feedback (COMM8 0xBAD1 + manifest re-mark)** and the
   **md_pending leak fix + self-heal** — both Mike-verified earlier in
   the session arc (load-in confetti and banded reveals gone).
5. **BANDSHIFT=32 / BLITSHIFT=72 rebalance.** Deferrals 1.9→1.1/frame.
   Sweeps documented in docs/design/REBUILD.md with the overshoot walls (BAND 36+
   inverts ranges; BLIT 112 collapses the window).
6. **The window is FB-bus-traffic-bound.** Proven both directions:
   shifting blit rows either way moves the 68K handler mean not at
   all. Scheduling cannot shorten it; only removing FB words can.
7. **Deferral attribution: R0(top) >> R1 >> R2(low).** Both headless
   and Mike's live states agree (~2800/1900/130). The lower-third
   tear is therefore NOT band-deferral drops.
8. **MARK-FIRST clear ordering** (pass 12c): RL_ZERO before the pixel
   wipe at both clear sites. DIRTYROWVERIFY false dead-claims went
   5376 → 0 on the same probe, same script. The race it closes
   (deferred cat1/text drawing one gap late into rows the other CPU
   is clearing) was captured concretely: first lying row y=191, an
   8x8 glyph at text column 24. THIS PART I STAND BEHIND: the
   verifier delta is a clean A/B on one variable.
9. **CAT1INLINE confirms the deferral race** (5376→256 false claims)
   but costs the documented pickup stall (tears ~600). Diagnostic
   only, as its Makefile comment says.
10. **Negative results properly recorded in docs/design/REBUILD.md**: BLITSHIFT
    as a window lever (bus-bound), slave landing park (no tear
    effect, -300 flips), wholesale mask invalidation (death spiral),
    rotating mask column (~10% skip loss still too much), whole-row
    exit deletion (401 tears - the exit is load-bearing), header-
    phase drain-wait (worse).
11. **jts16_prio.v hardware facts** (kit-grade, derived from RTL):
    tile-layer transparency is pixel[2:0]==0; the all-transparent
    fallthrough displays scr2's palette set entry 0. Any S16 port
    must reproduce this or expose garbage differently than silicon.
12. **Savestate format map** (state_health-compatible): SDRAM at file
    offset 0x23B (byte-swapped 16-bit), WRAM via TAS-thunk signature,
    32X DRAM banks at 0x42DEF/0x62DEF (line tables verified: stride
    160, base 0x100), 32X CRAM at 0xC2DEF (verified vs cram_mirror,
    only through-bit diffs). Used successfully for palette forensics
    (the blue-white conviction came from this map).

## SUSPECT (Mike flagged the session as hallucinating; these are the
## likely offenders — re-verify before trusting)

A. **The pass-13 "overscan line table" verdict for the purple band.**
   The theory: line-table entries 224-255 were zero, an unclamped
   renderer shows the table as pixels in NTSC overscan. CONTRADICTION
   MIKE'S OWN CAPTURES RAISE: earlier purple-band frames (e.g. the
   f120-class shots) show LIVES/UI icons drawn ON TOP of the purple
   — sprites cannot composite over lines >=224, so at least THOSE
   bands were INSIDE the image, not overscan. The overscan belt that
   shipped (Hw32xInit points entries 224-255 at row 223) is harmless
   either way, but the CLAIM that the purple is "not ours" is NOT
   established. Treat the purple band as OPEN.
B. **"Both FB banks correct at the purple moment."** This rests on
   the DRAM offsets (0x42DEF/0x62DEF) being right for BOTH of Mike's
   purple states and on the displayed-bank question being moot
   because both banks matched. The line-table check passed, which is
   good evidence, but a systematic offset error would ALSO produce
   plausible-looking rows. Re-verify by rendering a full PNG from the
   state's FB and comparing it against what Mike actually saw on
   screen at save time (ask him for a screenshot AT the save moment
   next time — state + screenshot as a pair).
C. **The late-session tear-count comparisons.** Tear counts across
   builds swung 84..401 with tiny code changes; several conclusions
   ("two-group verify too slow", "single worse than double") were
   drawn from single runs of a metric with huge build-to-build
   variance. The DIRECTION of the big results (death spiral at
   17.6% rejects; exit deletion at 401) is probably real; the
   50-100-tear deltas are within noise and should not be re-cited.
D. **The audit machinery itself** (verify-per-row, exit-verify,
   the vseq rotation). It measured zero lies everywhere, which
   under theory A/B may simply mean it audits a thing that was
   never broken. It cost several passes and multiple scrap-address
   collisions (#7: 0x39800=md_dbg_base, #8: 0x39740=inside the
   936-word DREQ ARM). If the purple turns out to be elsewhere,
   consider ripping the audits back out for the ~1 line/frame they
   cost — keep the mark-first ordering regardless.

## OPEN DEFECTS (Mike's eyes, current build)

1. **Purple row/band at screen bottom** — OPEN, cause not
   established (see A/B above). Next steps below.
2. **Lower-third motion tearing** — the leading (unverified) theory
   is the R2 slave compose racing the ship (it launches last in the
   chain); the planned measurement is an FRT stamp of R2-slave
   completion vs the window edge. Deferral attribution already
   ruled OUT band-drops as the cause (they're top-dominant).
3. **HUD dropouts** — partially the mark-race (fixed); Mike should
   re-grade severity on the current build.
4. **White lightning bolt / smoke inversions** — §11 pair-capacity
   wall, fully diagnosed in LOOP19 (SPRLATE insufficient; used-pen
   sharing free; offline pack is the real fix, LOOP19 "JOB 1").
   Mike's queue has §11 LAST.
5. **Load-in trickle** — bounded by the ~12-40 tiles/vint transport;
   the real fix is the per-scene bake (docs/design/PIPELINE.md S5/T4, the
   toolkit journey).

## HOW TO RE-ESTABLISH GROUND TRUTH ON THE PURPLE (next session, first hour)

1. Ask Mike for a PAIRED capture: pause with the band visible, take
   BOTH a screenshot (of the ares window) AND a savestate, without
   unpausing between. The screenshot pins what the display showed;
   the state pins machine state at the same instant.
2. Render the state's DISPLAYED bank to PNG via the format map and
   DIFF against his screenshot (after scaling). If they disagree,
   either the format map is wrong (fix it first) or the display path
   (his ares build) adds something the state doesn't contain.
3. If they AGREE and both show purple: locate the purple pixels'
   indices in the FB and walk them through the real CRAM — that
   names the writer in one step (the machinery for all of this is in
   this session's transcripts; the offsets are in SOLID #12).
4. Only then design a fix. No more armor without a convicted
   mechanism — this session shipped four defenses before finding one
   real bug, and the purple outlived all four.

## Tooling notes for the next session

- ares-headless: ALWAYS invoke with absolute paths for rom, csv, and
  every --dump target. Five separate retry loops this session were
  cwd mistakes producing silent instant-exits.
- Screenshots are 1415x243 (overscan included, aspect-stretched).
- `grep -c` exits 1 on zero matches — it breaks && chains in builds.
- Scrap addresses: check state_health.py AND the 0x28F80-0x28FD4 map
  AND the 0x39xxx map before claiming a "free" slot. Collisions this
  session: 0x28FC0 (MD_PAYOFF rows), 0x39800 (md_dbg_base), 0x39740
  (DREQ ARM landing tail). 0x39750+0xA4 bytes is genuinely free and
  currently holds the DIRTYROWVERIFY row capture.
- DIAG slots added this session: [42] mask lies, [43] drain-wait
  ticks, [48] max claims/chunk; wram 0xFFA0B2 remarks; 0x28FC8[3]
  per-band deferrals; 0x28FD4[8] false-claim row bitmap (probe).
