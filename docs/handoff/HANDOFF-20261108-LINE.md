# Handoff 2026-11-08: wsline02 is the line, speed phase starts

Audience: the next agent (Claude or Codex) on the sega16-2-32x port. Read this,
then `HANDOFF-CODEX.md` for the hard rules and recipes (they still apply).
Branch advisor-bisect, worktree `.claude/worktrees/advisor-bisect`.

## State

- Mike declared the working-set build the new line (play pass: looks and sounds
  right, Neff smoke less choppy but slower than arcade, boot VERSION US/JP option
  present). Accuracy is reached; the project is now in the speed phase.
- The ROM he played was `wslease01` (SHA256 8b6eded1...). It was not buildable
  from committed source: Codex applied a ~50-line snapshot-lease overlay to
  `sh_src/r60/r60_main.c` in a private build tree. Commit 50172bc9 puts it behind
  `R60SNAPLEASE=1` and adds `tools/build_line.sh NAME`.
- `build/port/rom/wsline02.32x` is that build. It differs from wslease01 in 14
  stamp bytes only (0x67ffd4). It is resident on the office rig.
- Line = candidate flags + R60WORKSET page (45-pose atlas, rebuilt from the
  arcade ROMs by tools/extend_zoom_atlas.py) + R60ZOOMKEEP + R60BOOTBANKS +
  R60WORKSETCLIP + R60SNAPLEASE. 9db88f45 (MD display-release fix) is in its
  ancestry.
- Rig strips, 180 s, 25 shots each: wsline02 A and B have 0 black frames;
  rdplay01 had 2 isolated black captures (so isolated blacks are baseline).
  One garbled logo frame appeared in wsline02 strip A only; not reproduced in B.
  A single strip is not a verdict for an intermittent symptom.
- Ares gates on wsline02 (ACCEPT_SOAK=125001, `tools/accept_ares.sh`): attract
  parity and temporal metric ran (presented 48/59 at 600, 55/59 at 1700). The
  125k soak had not finished when this was written; read
  `build/accept/soak_wsline02.txt`. The artaudit step failed with "no symbol map
  proven to be wsline02.32x's build" (a later build overwrote s16.lst/elf); rerun
  it after rebuilding, or ignore. Nothing about these results is a regression
  claim yet.
- Commits this session: b4b45edd (Codex docs ingested), 50172bc9 (line
  reproducible), 443454bf (STATE and START-HERE record the line).
- Uncommitted and not mine: cfg/altbeast.cfg (coin counter), cfg/genesis.cfg,
  sh_src/mars4.ld, sndtest/md/render_music_psg.h, sndtest/sh/speech_bank8.h.
  Leave them.

## What Codex did (days 2026-10-26 to 2026-11-08), and the approach

It stopped being a bug-fixing thread and became a measurement thread:

1. Rig as instrument. Frozen fixed scenes (sixteen smoke clouds, 2000 vints),
   matched reference/candidate pairs, same harness, accepted ROM restored after
   every run. Unit is completed bank acknowledgments, not FPS.
2. Census first. The cloud sprite source (bank 3, 01d0) is 96.7% of the zoomed
   work. No left-clipped zoomed records occur in the smoke sequence.
3. Bake or batch instead of per-frame work: the pre-baked working-set page,
   immutable packet admission (validate once at build time), and a block
   compositor that writes each surviving pixel once.
4. Exact proofs and failed variants preserved. Every step is pixel and metadata
   equal on the actual slave in ares, with an independent review. Losers (compact
   word scaler, worker threads, row index, cache RAM) stay default-OFF.

Why Claude had not done this: ares charges instructions only, so ares-ranked
speed work moved the rig by about nothing, and rig measurements were parked as
"needs Mike". With free rig access and an automated A/B loop that reasoning no
longer holds.

## Measured numbers (not FPS)

- Smoke entrance: 25 acks per 126 vints before, 31 to 32 with the working set
  (+28%). Death: 28 to 29 -> 32 to 35 (+18%). About 15 completed batches per
  second at entrance against the arcade's 60.
- Zero foreground-mask shortcut: 124 vs 133 whole jobs per 2000 vints (+7%).
  Immutable packet admission: +11.4% over three pairs.
- Duplicate stores: 10,660 sprite and punch word stores on 4,530 unique words in
  frame 33614 (57.5% duplicate). Old rig split: FPGA blit = stores 35 / instructions
  35, so removing the duplicates is worth roughly 25 to 30% of plot time, not 50%.

## What is NOT done and why

- The block compositor has no live caller. Integration needs a new plane-commit
  protocol (MD to SH-2 sidecar, semantic ACK, committed foreground mirrors, pin
  retain/release). Design: `build/codex-sprite-block-audit/runtime-plan/
  INTEGRATION-PLAN.md` and `presented-plane-binding/ACTIONABLE-DESIGN.md`
  (gitignored build dir; copy them into docs/ if they are needed long term).
  Codex's first step was a host-only committed-plane projector. Multi-day, and the
  ceiling is small. Recommendation: do not start it first.
- Normal04-family sustained black on hardware: 9db88f45 is in the line, strips
  are clean, but keep repeating the strip before any new candidate.

## Next steps, in order

1. Read `build/accept/soak_wsline02.txt` (TICKS TRACK expected). If it passes,
   record it in STATE.md and commit. If it hangs, that is the first bug.
2. Profile a FIGHT scene on the rig. Fights run at about 30 presented fps and are
   most of play, so a fight-wide gain is worth more than a smoke-only one. Use
   the level-1 tape (`tools/inputs/level1_full.csv`), the phase bars
   (`R60RIGSPLIT=3`, PORTRIG bars; see `docs/handoff/SMOKE-TIMING.md`) and
   `tools/rig_cut.sh`. Build the probe with `tools/build_line.sh`-style flags
   plus the probe flags; never hand a probe build to Mike.
3. Pick the largest phase and write a default-OFF flag for one change. Gate it:
   `make check-demo`, `tools/accept_ares.sh` (65k and 125k soak, parity,
   temporal, artaudit), the roster (ticks 297-298 at 3000, 697-698 at 3400), then
   a matched rig pair against wsline02 with the accepted ROM restored after.
4. Rig-only items with Mike present: Neff smoke slowdown, FPGA fight frame rate,
   the intermittent logo-transition tile garble (watch for it in strips).

## Process notes

- This session lost time to announcing work instead of doing it. Do the tool
  call in the same turn; report only results. Mike asked for automatic progress.
- Rig: `mister.office.local` is reachable when Mike is home. Push with
  `tools/mister_push.sh --name mister.office.local ROM`; strip with
  `tools/rig_cut.sh ROM NAME FROM TO`. The rig holds the last launch: re-push the
  line (wsline02) after any baseline or probe run, then read `/var/log/ACTIVEGAME`.
- No git push, no git stash, never edit MiSTer.ini or rig config, no em-dashes,
  zsh does not split `$VAR`. API offload only through `tools/ask_api.sh`.
- Memory note: `line-wslease01-2026-11-08.md`.

## Fight profile on the rig (2026-11-08, wsfight01 = wsline02 + R60RIGSPLIT=3, tape level1_full)

Probe `build/port/rom/wsfight01.32x` (tools/build_line.sh wsfight01 R60RIGSPLIT=3
R60NOBARS= R60FRONTEND= PORTGODMODE=1 PORTTAPEPAD=... PORTTAPEARCADE=...). 16 shots,
read with `tools/rig_split.py --wide`; wsline02 restored to the rig afterwards.
Mean over the 9 busy fight shots, slave lines per pass (262 lines = 1 vint):

| phase | lines | share of the 553-line pass |
| --- | ---: | ---: |
| PLOT | 166 | 30% |
| 68K span wait (w.68k) | 127 | 23% |
| PUNCH | 59 | 11% |
| CLEAR | 37 | 7% |
| no-snapshot wait (master tick not done) | 33 | 6% |
| prep | 16 | 3% |
| spr-rest, TEXT, keep, publish, cram | 43 | 8% |

A pass is 2.1 vints on average (1.0 to 2.4 across shots), so fights present about
28 per second, matching the earlier 30 fps note. Master tick uses 110 to 160 of a
262-line window while the slave works.

Reading: the 68K span wait is the slave idle while the 68K owns the framebuffer
(FM=MD) and DMAs the packet. It is a hardware ownership handoff, so it cannot be
overlapped, only shortened by shrinking what the 68K consumes per pass (packet A/B
lengths, `R60_PKTA_LEN`; `R60_FATSPAN` for fat packets). It is larger than the
whole compositor ceiling (about 25-30% of PLOT = 8% of a pass). Order of attack:
(1) what is in the 68K span in fights and can it shrink, (2) PLOT/PUNCH, (3) the
no-snapshot wait (master-bound). Not yet measured: packet lengths in fights.

### Packet sizes and what the 68K-span wait probably is (ares, wsfight01, 6000 frames)

- Packet A in fights: p50 23 words, p75 23, p90 28, p99 81, max 362 (frames 1500-6000).
  It is small, so DMA volume does not explain a 127-line wait.
- Packet B length reads a constant 752 in `r60_pkta_len_mem[2..3]` for all 4500 frames.
  [UNVERIFIED] That slot may simply never be rewritten on the direct-staged path
  (r60_main.c:1794 writes only the A index); do not read it as B's real size.
- [INFERRED] The 127-line mean wait is about half a vint (131 lines): the slave opens the
  span and the 68K services it at its next vint boundary, so every pass is rounded up to a
  whole number of vints. That is the frame-threshold law again (memory:
  frame-threshold-law): a pass that needs 370 lines of draw (PLOT+PUNCH+CLEAR+prep+rest)
  costs 2 vints, one that fits in 262 costs 1. Gains appear when the draw work crosses
  a vint boundary, not smoothly. To reach 1 vint in fights the slave's non-wait work must
  fall from ~370 to under ~262 lines (about -30%): PLOT+PUNCH+CLEAR alone are 262.
- Test of the inference (not yet done): log pass length against `R60_FATSPAN`/COMM0 service
  time on ares, or build a probe that has the 68K poll COMM0 in its main loop and see
  whether w.68k falls.
