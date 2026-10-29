# PARITY HARNESS: gameplay and attract against the arcade (2026-10-22)

One command scores a port rom against the arcade on both halves of the game: the attract
(the existing accept_ares gates) and a played level (a replayed input tape on both machines,
compared by milestones, object kinds and video). It exists so a build can be measured without
anyone at a pad. Mike's play pass stays the acceptance gate; this harness is what runs before
a rom earns one.

## Run it

    make parity ROM=build/port/rom/hp1.32x
    make parity ROM=... TAPE=tools/inputs/level1_full.csv ARCADE=tests/gameplay/arcade_level1_full
    make parity-candidate NAME=hp2 [EXTRA='FLAG=1']     # build a godmode candidate, then parity
    make parity-oracle [TAPE=...] [ARCADE=...]          # regenerate the arcade corpus

`make parity` runs, in order:

1. `ACCEPT_OUT=build/accept tools/accept_ares.sh $(ROM)`: the attract gates, four files in
   build/accept named `<gate>_<rom>.txt`.
2. `python3 tools/parity_play.py $(ROM) --tape $(TAPE) --out build/parity/<rom>`: replays the
   tape on ares-headless and writes the port's timeline.csv and census.csv in the format below.
3. `python3 tools/parity_compare.py --arcade $(ARCADE) --port build/parity/<rom> --rom $(ROM)
   --tape $(TAPE) --accept build/accept --report build/parity/<rom>/REPORT.md`: scores port
   against arcade. The tape is passed explicitly (an older corpus's mame.log has no TAPE
   line; a newer one's is checked against it).

The result is `build/parity/<rom>/REPORT.md`. Report a build as that file's scorecard plus the
commit hash, the same way check-demo results are reported.

Time: the arcade corpus takes about 50 s (MAME runs the tape at ~700 fps); the port replay is
ares-headless at ~65 frames/s, so a 36000-frame tape is 8 to 10 minutes. accept_ares runs its
four gates in parallel and is bounded by its 30001-frame soak.

## What it measures

| section | what | instrument | owner lane |
|---|---|---|---|
| setup and run health | the DIP latch 0xFFF01A/1B read off each side's ev wram dumps, the credits the start consumed and the lives it set, how each side pins lives, a HANG (in-play run of 300+ frames with the tick and game frame frozen) and the stalled-frame count; FAIL on any DIP, start or hang difference. Nothing below is comparable until this passes | parity_compare section 0 | port logic (hang, DIP seed in sh_src/port_l2.c) |
| attract scorecard | flips/vint (soak), attract_parity ladder per scene, temporal (presented frames + flicker at spans 600-660 and 1700-1760), artaudit cells | tools/accept_ares.sh | renderer |
| input proof | the port's latched pad byte 0xFFE028 at every tape edge matches the tape; edges inside a window where the game itself does not read the pad (unlatched) or after a hang (hung) are excused; lag is scored in ACTIVE frames (frames where the tick advanced) | parity_compare section 1 | port logic (the 68K pad post under R60LIVEPAD) |
| milestones | coin, start, player live (handler 0x49BA), first move, first score, score drawn, boss active, boss 0x1794C, round clear (0xFFF14B), ball scene, level 2, first level-2 enemy, lives lost, continue window, game over; frame and tick on each machine; FAIL = reached on the arcade, never on the port | parity_compare section 2 (tests/gameplay/milestones.py prints the same off one corpus) | port logic |
| divergence | (a) by host frame with the measured start offset: hero x within 8 px overall and per 3000-frame window, first frame outside it; FAIL only when that is before the first live enemy. (b) by (segment, tick) with the measured sampling skew: first tick where x, lives lost since the start, or the score differ | parity_compare section 3 | port logic |
| object census | per level and slot range (1-15 trail objects, 16-39 enemies, 40 boss, 41-63 effects): handlers with sample counts; FAIL = arcade-only with 2+ samples, or sustained on the arcade (density 0.1+) and under a tenth of that on the port; handlers first seen beyond the port's furthest scroll are listed as beyond port progress | census.csv on both sides | port logic |
| video | at each milestone both sides reached within 8 frames of an ev dump: tilemap page fill, text rows and sprite count; mean luma diff only when both sides have a screenshot within 8 frames, the ares canvas cropped to its picture. Informational | parity_compare section 5 | renderer |
| port health | _port_l2_out u32[13] missed indirect dispatches (must be 0), u32[40..47] missed targets, u32[48..55] occupancy, read off the port dump | parity_play | port logic |

## Reading REPORT.md

Each section carries PASS or FAIL and the number that decided it. Read top down and stop at
the first FAIL; everything below a failed setup or input proof is noise.

- SETUP FAIL: the two machines are not the same game. DIPs: the port build under PORT_CORO
  seeds 0xC42001=0x54 / 0xC42003=0x66 (sh_src/port_l2.c, from a 2026-10-03 MAME capture made
  while cfg/altbeast.cfg was drifting, see below) and the oracle defaults to ff/fd (the old
  68K shim's values, md_main.c:7711); the report reads both off the latched copy at
  0xFFF01A/1B in the ev wram dumps. Make them agree before reading anything else: either
  regenerate the corpus with `GD_DSW1=66 GD_DSW2=54 make parity-oracle ARCADE=...` or seed
  ff/fd in the port (builder's call). Start: credits consumed and lives set at the start row
  (the hp1 run: 2 credits / 4 lives vs 1 / 3, a consequence of the DIPs). HANG: the longest
  in-play run with both the tick 0xFFF02A and the game frame frozen; the arcade's longest is
  under 30 frames, the hp1 port froze at frame 30924 for the rest of the run with missed
  dispatches 0, i.e. the master's loop stopped outside the handlers. Every row from a hang is
  the frozen state; later sections mark events and milestones in it as hung. Stalled frames
  (tick did not advance, outside a hang) are cadence and belong to the temporal and soak
  gates.
- INPUT PROOF FAIL: the port did not see the tape. Either the rom was not built by `make
  candidate` (bare `make port` has no R60LIVEPAD, the 68K never posts a pad) or the pad post
  is late or dropped. Nothing else in the report is evidence until this passes. A timeline
  row is the state at the START of its frame, before that frame's input applies, so a tape
  edge at frame F is first visible at row F+1: lag 1 is the floor (the arcade reads 1 on
  7835 of 7871 edges), the port reads 2 because the 68K posts the pad one vint before the
  master's tick consumes it, and its lag wobbles with the tick stutter (3 to 5 on some
  releases). A release is searched from the frame after its press landed; a press whose bit
  is already held at the edge (its earlier release never landed) is n/a, not a landing.
  An edge that never lands is "unlatched" when it is one of two or more consecutive
  never-landing edges over which that machine's pad byte did not change (the game is not
  reading the pad: the arcade itself has 8 at the level-2 intro, frames 31804-31932, and
  the port's equivalent windows fall at other frames because it runs at another tape
  phase), and "hung" when its search window reaches a hang. Both are excused and counted.
  FAIL = an edge that lands on one machine and reads never (not unlatched, not hung) on
  the other, or a port lag in ACTIVE frames (frames where its tick advanced, so a frame in
  which the master ran no pass is not charged) more than --lag-slack (3) past the arcade's.
- MILESTONE FAIL: a milestone the arcade reached that the port never did (frame and tick of
  each are printed; the frame delta is informational, there is no distance gate). The
  arcade's frames for the level1_full tape: credit 513, start 902, player live (handler
  0x49BA) 919, first score 1703, boss active 1474, boss 0x1794C 28316, f14b 31113, level 2
  at 31804. "score drawn (first ev text dump)" is the first ev_ text dump at or after the
  first score change whose digits at text 0xB4..0xC4 are nonzero: its frame is a dump
  frame, not a game event, so the row carries no deltas; n/a when the port has no text dump
  to read. "lives lost" rows are n/a against a corpus whose mame.log has no
  `GAMEPLAY-DUMP GOD after-row` line: those oracles wrote GD_GOD before the row, so an
  arcade death could never show (the lua now writes it after the row and the dumps). A
  missing milestone is port logic: a handler that never ran, a wrong width, a missing
  register (docs/handoff/HANDOFF-ADVISOR.md, the port register-file gap).
- OBJECT CENSUS FAIL: per level the port reached and per slot range (1-15 the hero's trail
  objects, 16-39 enemies, 40 boss, 41-63 effects), a handler the arcade holds for 2+
  census samples and the port never, or one SUSTAINED on the arcade (density, samples per
  sample frame, 0.1 or more) that the port holds at under a tenth of that density: the hp1
  run holds the five 0x6CB8 trail objects at density 2.67 on the arcade and 0.006 on the
  port. A handler the arcade first saw beyond the port's furthest scroll in that level is
  "beyond port progress" (the boss sequence, when the walk never got there) and is listed,
  not failed. Port logic: the spawn table or the kind dispatcher. The census is sampled
  every 60 frames, so a kind alive under 60 frames can be missed on either side (hence the
  2-sample floor). The arcade spawns 0xE474 0xEA5E in level 1 (the context listed them as
  level-2 kinds) and 0xEE80 in both levels.
- DIVERGENCE: two alignments, because the tape is keyed by host frame and the game by its
  pass count. (a) BY HOST FRAME with the start offset (port = arcade + 2 on level1_full):
  the share of frames with both heroes in the play handler and x within 8 px, overall and
  per 3000-frame window, and the first frame outside it. The hp1 run matches on 1641 of
  2081 frames in the first window and diverges at arcade frame 1663, after the first enemy
  (1481); FAIL only when the first divergence comes before any enemy is live on either
  machine. (b) BY (SEGMENT, TICK): in-play rows cut into segments at a level change or a
  tick jump over 16 (the level, the round-transition countdown that counts 0xFFF02A down
  at level 0, the next level), matched by ordinal, the port's tick shifted by a skew
  measured on the opening descent (ares samples at the frame start, after the vint
  advanced the tick and before that tick's pass ran: +1 on hp1). Prints the first tick where
  x, lives LOST since the start (so the setup difference does not mask a death) or the
  score differ. Combat diverges within seconds of the first enemy because the RNG stream
  depends on frame-exact input and hit timing, so everything past the first divergence is
  noise; the FIRST divergence is a lead.
- VIDEO: informational. For each milestone reached on both sides with an ev_ dump within 8
  frames on each side: the tilemap pages whose nonzero-cell count differs, the text rows
  that differ, the sprite counts, and the mean luma diff when both sides have a screenshot
  within 8 frames of the milestone (the ares canvas is 1415x243 with the picture at cols
  65..1344 rows 11..234; it is cropped before both are resized to 320x224), with a
  side-by-side png in the report dir. A corpus from before 2026-10-22 has arcade shots
  only every 300 frames, so no luma row; the lua now snapshots at every ev dump. Renderer
  lane, unless the milestones also fail at that frame.
- ATTRACT FAIL: whichever of soak (>= 0.78 flips/vint), temporal (>= 40 of 59 presented in
  both spans) or artaudit (0 cells both lines) moved against the line. Renderer lane. The
  current line reads soak 0.79 flips/vint, artaudit 0 cells. The attract_parity ladder is
  printed per scene (collapse column, row minimum) and is informational: no rom on record
  collapses every row (boot card reads 55 in every column on all 35 accept files, demo
  scenes bottom out at 25 to 27), so read it against the previous candidate; `--parity-
  collapse N` turns it into a gate. artaudit needs the rom's own symbol map: `make port`
  keeps <name>.lst/.elf/.flags beside <name>.32x for that.
- PORT HEALTH: health.txt under build/parity/<rom>; u32[13] nonzero means an indirect
  dispatch landed on no target; port logic (cport_prep's indirect table).

## Known limits

- ares-headless cannot load savestates and cannot write memory. The arcade oracle holds P1
  lives at 3 (GD_GOD in tools/gameplay_dump.lua) so the tape cannot game over; the port
  cannot be poked the same way, hence PORTGODMODE (Makefile, sh_src/port_l2.c: lives held at
  3 while 0xFFF028 bit0). A PORTGODMODE rom is measurement only and is never handed to Mike.
  `make parity-candidate` builds it for you; `make parity ROM=` on a plain candidate is valid
  but the player will die and the milestones past the first death are meaningless.
- `--dump` reflects the END state of a run, so sampling N frames is N runs; the SDRAM watch
  binary (~/src/bin/ares-headless-sdram, A1) logs the timeline in one run via
  `--gameframe-watch sdram:...`, with `--gameframe-watch-every` global across watches. The
  log row for frame N is the state after N frames; run N+1 frames to capture N.
- Combat diverges within seconds. The measure is milestones and kinds, not bytes. Do not
  read a record diff count as a defect count.
- The arcade corpus is Sega-derived (work RAM, video RAM, snapshots) and gitignored
  (tests/gameplay/arcade*/). Regenerate it with `make parity-oracle`. The tape
  (tools/inputs/level1_full.csv, from tools/make_tape.py) is ours and tracked.
- DIPs: the two machines must boot on the same switches. The oracle forces GD_DSW1/GD_DSW2
  (hex, default ff/fd, the old 68K shim's values) with a fresh -cfg_directory and prints
  `GAMEPLAY-DUMP DIPS c42003 xx c42001 xx` at frame 50; the port build under PORT_CORO
  seeds 66/54 at boot (sh_src/port_l2.c), so a corpus for it is made with `GD_DSW1=66
  GD_DSW2=54`. parity_compare section 0 reads the latched copy at 0xFFF01A/1B off both
  sides' wram dumps and FAILs when they differ. A corpus whose mame.log lacks the DIPS line
  was made on drifting DIPs and is not an oracle (every arcade dump made in the repo cwd
  before 2026-10-22 was; the port's own 66/54 seed came from a capture made in that era).
- Lives pinning: the oracle writes GD_GOD after the row and the dumps (mame.log carries
  `GAMEPLAY-DUMP GOD after-row`), so a death shows as one row below 3; the port's
  PORT_GODMODE runs in the per-tick MCU block and holds lives at 3 or more, so a death from
  above 3 shows and one at 3 may not. Neither pin survives a hang (the loop that would
  write it has stopped).
- MAME cannot run the port (confetti on every R60 build), so the port side is ares only.
- The shared build/port/rom/s16.lst belongs to the LAST build. tools/romsyms.py refuses a rom
  whose s16.elf does not match it (artaudit on hp1 refused once hp0 was built), so the port
  target keeps <name>.lst, <name>.elf and <name>.flags beside every named rom, parity_play
  reads _port_l2_out from <name>.lst when it exists and refuses the shared s16.lst when its
  mtime is not within the rom's build (newer by 2 s or older by 600 s; pass --lst to
  override), and copies the map it used to build/parity/<rom>/raw/symbols.lst.
- Alignment: the port takes the start 2 frames later than the arcade on level1_full (its pad
  lag is 2 frames, the arcade's 1), so by (level, tick) the hero reads ahead and by frame he
  reads behind; the report shows both and says which it used. ares-headless has
  --input-gameframe to key the tape to a memory counter; the tick resets per attract scene,
  so it is not usable before the start, and the harness keys by host frame on both sides.

## TIMELINE CSV FORMAT

Shared by the arcade oracle (tools/gameplay_dump.lua, GD_TIMELINE) and the port runner
(tools/parity_play.py). One row per emulated frame. Header, exactly:

    frame,tick,gameframe,level,round,scroll,px,py,pstat,phand,lives,score,credits,f028,f14b,f154,pad,boss_stat,boss_hand,step,demo

| column | source | width |
|---|---|---|
| frame | emulated frame, decimal | |
| tick | 0xFFF02A | u16 |
| gameframe | tests/demo-b/check_demo.py game_frame() from 0xFFF020/21 | decimal |
| level | 0xFFF142 (0 = level 1) | u8 |
| round | 0xFFF14E | u8 |
| scroll | 0xFFF132 | u16 |
| px, py | slot 0 +0xC, +0x10 | u16 |
| pstat | slot 0 +0 status | u16 |
| phand | slot 0 +2 handler | u32 |
| lives | 0xFFE018 | u8 |
| score | 0xFFE000, BCD | u32 |
| credits | 0xFFF000 | u8 |
| f028 | 0xFFF028 P1 state (bit0 in play, bit4 continue open) | u8 |
| f14b | 0xFFF14B round-clear release | u8 |
| f154 | 0xFFF154 live-enemy count | u8 |
| pad | 0xFFE028 latched P1 pad (inverted buttons) | u8 |
| boss_stat, boss_hand | slot 40 (0xFFD400) status, handler | u16, u32 |
| step | 0xFFF031 attract step (0x08 = demo/play) | u8 |
| demo | 0xFFF026 bit0 | 0/1 |

All numbers hex without 0x except frame. Beside it, census.csv: `frame,slot,status,handler`
for every active slot 1-63 (status bit 15), sampled every CENSUS_EVERY frames (default 60).
On the port the work RAM window is SDRAM offset 0x2CC00 + (addr - 0xFFC000); on the arcade
the 68K addresses are read directly.

A live enemy is (status >> 8) & 0x88 == 0x88 (0x8800 / 0x8900; the mask is on the HIGH
byte). Slot layout: 0 player, 1-15 the hero's trail objects (0x6CB8) and pickups, 16-39
enemies, 40 boss, 41-63 effects, 0x80 bytes each, +0x3E countdown. The DIP latch is at
0xFFF01A (DSW1 = c42003) / 0xFFF01B (DSW2 = c42001), inside the 0xFFF000 watch window.

## Files

- Makefile: targets parity, parity-oracle, parity-candidate (beside candidate)
- tools/accept_ares.sh: attract gates
- tools/parity_play.py: port replay (B1)
- tools/parity_compare.py: scorer and REPORT.md (B2)
- tests/gameplay/arcade_oracle.sh, tools/gameplay_dump.lua: the arcade corpus
- tests/gameplay/milestones.py: milestone reader, works on either timeline
- tools/make_tape.py, tools/inputs/level1_full.csv: the tape
- tests/gameplay/check_gameplay.py: the old skeleton, now points here

## Limits measured on the first runs (2026-10-22)

- **Tick-phase lottery.** Two roms that differ only in where the coroutine stack sits (hp2: top 0x0602C700, hp3: top 0x0602C600) play tools/inputs/level1_full.csv differently: their scene ticks first disagree at frame 1275 (hp3 one tick ahead), the latched pad first differs at frame 4093, the enemy count at 4886, the score at 5664. hp2 kills the level-1 boss and enters level 2 by frame 33000; hp3 stands in the arena with two enemies it never kills. Nothing in the logic differs: the master runs its game tick in a different vint phase, so the same wall-frame input lands on a different game tick. Combat milestones (boss transform, round clear, level 2) therefore say "this tape, this build", not "the port cannot". The fix is to key the tape to the game tick on both machines (ares `--input-gameframe sdram:<port tick counter>` and the lua replayer on the same count); until then compare input landing, the hang detector, the kind census and the early milestones, and read combat milestones with the hp2/hp3 pair in mind.
- **Mid-tick samples.** The timeline samples work RAM at the start of every emulated frame; the master's tick runs inside the frame, so a sample can land mid-tick (hp2 vs hp3: player x differs by 1 px at frame 965 with equal ticks). tests/demo-b/check_demo.py's `port_tick_done` marker exists for this; the timeline runner does not read it yet. Treat one-frame, one-field differences as sampling.
- **What the first run found anyway.** The coroutine stack / I/O page overlap (mars.ld, fixed 2026-10-22) froze the port on the second death at the boss-arena lock; the score is never drawn; enemy kinds e474/ea5e and the hero's trail objects 0x6CB8 never appear on the port (robust across hp1/hp2/hp3).
