# HANDOFF: Codex thread, until Claude credits return (written 2026-10-26)

**Claude resumption, 2026-10-28:** Read `HANDOFF-CLAUDE-20261028.md` first. Mike confirms sw336 still locks in level five after collecting two spirit orbs. No level-five fix is claimed.

Read this first, then `CLAUDE.md` (reading order and gates), then the top 10 entries of
`docs/handoff/START-HERE.md` (newest first; it is the running log). You are taking over the
**advisor** role on a homebrew port of Altered Beast (System 16B) to the Sega 32X. Mike
(the owner) plays the builds; Claude resumes when credits reset (about Wednesday 2026-10-28).

## 1. Where things stand

- **Current locally gated candidate sw336:** game sourceb19abf2c, restores level4 stomach/body byte tests and signed detached-part death cleanup. All required65k/125k/both36k/11frontend/roster/art/quant/temporal/20original object-sprite checks complete (legacy sampling/picture/FB exceptions unchanged). Stomach Miyoo acceptance pending. Level5 OPEN after full playthrough: second white escaped, all visible enemies cleared, player moves in fixed screen before Neff visible. Direct5/Easy and scoped4->5tests pass, so capture full-route lock with sw336-lock2(source67d710cc), readonly footer; no forced gameplay flags, menu retained. Smoke speed OPEN: sw336-timing(sourceb19abf2c), frontend disabled only in timingprobe, natural level1, cold-validated phase bars. Guides docs/handoff/LEVEL5-LOCK.md and SMOKE-TIMING.md; full evidence build/codex-followup-sw335/RESULTS.md. Normal336ROM hash00b64ed20a109114cf38d76379b7f49a3ef417c9ca3072f184174bdc186b58b2. Diagnostic-only additions preserve normal code/data outside stamp.

- **Newest accepted game candidate sw335:** source397f893a, normal local build/port/rom/sw335.32x. Level4 orbit/palette contexts, final-boss state-table/AI/effects and shared smoke-band context fixed. Mike explicitly confirms both battles and cleanup work on Miyoo. All required local checks complete:65k/125k/both36k,11frontend, roster/art/quant/temporal and20original measured object/sprite checkpoints (exactH7/H13 proofs; legacy sampling/picture/FB exceptions unchanged). Evidence build/codex-boss45-sw334/RESULTS.md and newest START-HERE. No hardware speed or full game coverage claim.

- **Frontend introduced in sw334, retained in sw335:** `sw334`, source `3f80ff44`, local `build/port/rom/sw334.32x`. Hold B at boot or B+Start at title; options, level select, difficulty/lives, returnable controller test. Eleven actual-menu cases pass, including all five rounds, title lives latch, defaults, repeated entry and three-button return. All required local checks complete:65k/125k/both36k, roster/art/quant/temporal and20 measured object/sprite checkpoints (H7/H13 direct proofs, legacy sampling/picture exceptions unchanged). Level select/button assignment play-accepted on Miyoo; all11retention tests pass in sw335. See newest START-HERE, `docs/design/FRONTEND.md` and `build/codex-menu-sw333/`. sw333/pad333 preserved.

- **Controller-only diagnostic:** `build/port/rom/pad333.32x`, source `5d1e4496`, ready for Mike on the failing emulator/cart setup. Opens directly into raw handshake/type/button/history display; B freezes, C clears, reset exits. Three 1100-frame ares matrices pass six/three, three/six and absent ports. No six-button detection-rule change, no claim hardware issue fixed. Reuses existing test-cart display and 32x-builder conventions; no external-project edits. Read `docs/handoff/CONTROLLER-TEST.md` and `build/codex-pad-sw333/RESULTS.md`. Source also fixes boot linker alignment; normal guard has only copy-length1593->1594 plus stamp differences, short roster/art/quant pass. Full gameplay gates must run on the next game candidate; sw333/332 ROMs remain unchanged. Optional boot/title menu is implemented and locally gated in sw334, ready for Mike play pass.

- **Branch / worktree:** `advisor-bisect` in
  `/Users/mikeholzinger/src/sega16-2-32x/.claude/worktrees/advisor-bisect`, game source head `b19abf2c` (2026-10-28; diagnostic sources `f00f710c`/`67d710cc`, later documentation commits may follow).
  Do all work there or in a new worktree off it. Never push (`git push` is forbidden).
- **Previous game candidate:** `sw333`, source `ff1c181a`, local `build/port/rom/sw333.32x`. Level-four harpy entry delegates to repaired initializer; two raw signed-link sprite-index accesses map through W68. Natural ROUND3/GOD-only probes now continue after untransformed Neff retreat, no forced form or clear. Required local checks complete:65k/125k/both36k, roster/art/quant/temporal and20 measured object/sprite checkpoints (H13 direct tick-end proof; existing picture/FB exceptions unchanged). Ready for Mike's level-four play pass. Evidence `build/codex-level4-sw332/RESULTS.md`. Mike accepts sw332 boss colour and only noticed the new retreat trap in level four. Harpy/trap acceptance still pending.
- **Previous test candidate:** `sw332`, source `d7435d63`, local `build/port/rom/sw332.32x`. Retains sw331 statue cleanup; restores per-part boss1892A and shot18C82 palette contexts. Weak point now has native normal/flash banks1B/0D instead of0/0; projectiles get own75/8F palettes. Full65k/125k/both36k, roster, art/quant and20 original measured arcade object/sprite checkpoints pass (H#13 exact direct tick-end proof; known picture/legacy-FB exceptions unchanged). Normal flags, no fixture or gameplay probes; hash in newest START-HERE. Mike confirms "Boss color fixed"; do not infer blanket acceptance of other combat cases. His167..1273 recording is OLD sw330, not a331regression. Damage/defeat logic was present in BOTH old and changed controlled tests; changed ordinary-input probe also reachesHP30. Do not claim newly added damage logic or close his inability-to-harm report without play acceptance. The proposed projectile zoom/script-corruption issue was disproved and no such rewrite remains. Evidence `build/codex-boss3-sw331/RESULTS.md`. Prior sw328..sw331 unchanged. Remaining smoke hardware slowdown and uncaptured top-edge boss-death colour tiles stay open. No rig accessed.
- **Accepted control:** `sw328`, source `12231f65`, local `build/port/rom/sw328.32x`. Attract boot/logo/eye fixes play-accepted by Mike on 2026-10-26 ("attract, fixed."). Final 65k/125k and normal/godmode 36k tapes passed; 20 measured check-demo object checkpoints match the arcade (H#7 documented fallback; two existing picture-flag classes remain). Read the newest START-HERE entry and `build/codex-screenshots/RESULTS.md` before promotion. Code/data and flags match measured tile-dac2 except the final build stamp. No rig access.
- **Previous candidate:** `sw327` = `make candidate NAME=sw327` (flags in `CANDIDATE_FLAGS`, Makefile ~4685;
  read it, never copy a stale list). Rom: `build/port/rom/sw327.32x` (snake bodies, ares gated and accepted by Mike on 2026-10-26). It is NOT on the rig
  (Mike is away from the office; `mister.office.local` refuses ssh). Last rig-played build:
  sw325 (copied, played by Mike on 2026-10-25: level 1 "close to parity").
- **Architecture, 3 lines:** the 68K arcade program is decompiled (Ghidra) into C by
  `tools/cport_prep.py` (generator passes) plus hand files `sh_src/portfull/hand/FUN_<addr>.c`;
  that C runs on the master SH-2 ("the port"). The slave SH-2 renders (`sh_src/r60/*`) into the
  32X framebuffer; the 68K only consumes packets and posts pads/coins (`md_src/`). Why: `ARCHITECTURE.md`.
- **Gate vs oracle:** headless **ares** (our fork) runs our rom; **MAME altbeast** is the arcade
  oracle. Mike's play pass is the acceptance gate.

## 2. Hard rules (each was paid for)

1. **Never `git push`, never `git stash`.** Commit locally, small commits, message says what and why.
2. **Mike is on ZSH.** Spell make/ares flags out; never pass flags through an unquoted `$VAR`
   (one argument = usage error); `for x in $VAR` does NOT split in zsh; a glob with no match aborts
   the command; cwd resets between shell calls (use absolute paths).
3. **Never edit `MiSTer.ini` or any rig config.** Rig access (ssh root@mister.office.local,
   `tools/rigpass.sh`, `tools/mister_push.sh --name mister.office.local`) works only when Mike is home
   on the LAN; if ssh is refused, work on ares only and do not retry. Never use `mister.tv.local`.
4. **`docs/proof/altbeast.c` is the generator's input and UNTRACKED (only copy).** Backed up at
   `~/backups/sega16-proof-20261024/`. Never overwrite it; never regenerate it wholesale (no Ghidra
   pipeline reproduces it, LESSONS.md:11502).
5. **Ghidra only through `~/tools/ghidra/bin/ghidra-run`** (reads `~/tools/ghidra/AGENTS.md`;
   `tools/ghidra_run.sh` wraps it). Never hardcode a path; copy a project before mutating it.
6. **ares is the CORRECTNESS instrument, not the speed instrument.** A day of ares-ranked speed work
   (fight 0.55 -> 0.89 passes/vint) moved the FPGA's presented rate by ~0. Speed cards start with a
   RIG phase split (`R60RIGSPLIT=1`), which needs Mike present. Do not report an ares gain as a frame-rate gain.
7. **One rig strip is not a verdict** for an intermittent symptom; re-strip the control first.
8. Gates before calling anything done (section 4). A verdict from you is a lead until the gates and
   Mike's play pass agree. Say what is unproven.
9. Keep `LESSONS.md` for hardware/decompiler facts no code captures; a fix carries its finding in the
   commit message. Record each milestone as a dated entry at the TOP of `docs/handoff/START-HERE.md`.
10. Do not use `claude -p` or the Anthropic API key tools; they bill Mike's Max plan.

## 3. Build and run recipes

Gitignored inputs a **fresh worktree** needs (the advisor-bisect worktree already has them):
`ln -s` `mame roms srcref` and `docs/proof` from advisor-bisect (docs/proof READ-ONLY); copy
`sh_src/{speech.bin,sfx.bin,speech_tab.h,sfx_tab.h,sprites.bin,sound.bin}`, `md_src/sega_blob.bin`,
`sndtest/sh/speech_bank8.{c,h}`, `sndtest/speech/`; `mkdir -p build/port/sndtest/sh build/port/sh_src`;
link `build/port/sndtest/sh/speech_bank8.c` and `build/port/sndtest/speech` to the worktree copies;
`touch` the four sound outputs in `sh_src/` and `build/port/sh_src/`. Builds share `build/port` inside a
worktree: run them one at a time.

```
make candidate NAME=x                       # the candidate flags -> build/port/rom/x.32x, x.lst, x.elf, x.flags
make candidate NAME=x EXTRA="PORTGODMODE=1" # + a probe flag (immortal hero; probes are NEVER for Mike)
tools/build_id.py show build/port/rom/x.32x # stamp = commit
```
.ramtext slot is 0x06031000-0x06038000 (`R60RAMTEXTPEEK=1` lifts the cap to size an overflow; never run a
peek rom); `.bss` must end under 0x06019000; cart-resident helpers need `noinline` (LTO inlines statics).
Symbols: `build/port/rom/x.lst` (SDRAM dump offset = symbol address - 0x06000000).

ares (`~/src/bin/ares-headless-sdram`):
```
ares-headless-sdram --frames N [--input tape.csv --pad1 fighting] \
   [--screenshot N-1:/abs/f.png] [--dump "sdram:0xOFF:0xLEN:/abs/f.bin"] [--dump "VDP CRAM:0:0x80:/abs/c.bin"] \
   [--watch-sdram 0x06lo:0x06hi:fa:fb --watch-sdram-out w.csv] /abs/rom.32x
```
Dumps are END-of-run state (one run per frame of interest). Watch ranges are BUS addresses (0x06000000-based).
Memory map facts: port 68K work RAM 0xFFC000 = SDRAM 0x2CC00 (objects 64 x 0x80 from 0xFFC000, handler
long at +2; credits 0xFFF000 = 0x2FC00; game frame counter 0xFFF02A = 0x2FC2A; sprite list 0xFFF800);
DIAG at 0x28E40 (u32: [0] vints [1] flips [9] slave passes [11] game ticks); DIAG2 0x28E80; mailbox 0x28E00.
Tapes: `tools/inputs/*.csv` (`frame,button,value`, keyed to emulator frames, so they diverge per build);
`tools/make_tape.py`. `tools/inputs/start3_coin.csv` = three START presses only.
Level probes use ZERO-BASED round indices (0 = level 1, 1 = level 2, 2 = level 3). Never on a candidate: `PORTROUND=N` (port) / `GD_ROUND=N tests/gameplay/arcade_oracle.sh`
(MAME) start real play at round N; `PORTENDCLEAR` / `GD_ENDCLEAR` clear the scroll lock;
`PORTGODMODE=1`; `PORTJUMP`, `PORTFORM`, `PORTHOLDLIVES`.

## 4. Gates (run on every candidate; see newest START-HERE entry for sw327 measurements)

| gate | command | expected |
|---|---|---|
| roster | `ares ... --frames 3900 --gameframe sdram:0x2FC2A --gameframe-log att.csv --gameframe-watch sdram:0x2D400:0xC00 --gameframe-watch sdram:0x2FD42:0x2 --gameframe-watch-every 10 rom` then `python3 tools/roster2.py att.csv` | ticks @3000 / @3400 = 297-298 / 697-698; level byte 1 roster `0ee80 12cba 12f0a 13374 137cc 137e0` |
| soak + attract parity + temporal + artaudit | `ACCEPT_OUT=<abs dir> zsh tools/accept_ares.sh <abs rom>` (65k frames, prints `TICKS TRACK` or a hang) | ticks track vints; ~0.84 flips/vint; presented ~46/59 (600-660), ~55/59 (1700-1760); parity rows converge (face by col ~8, eye by col ~3, last column <= ~11); artaudit 0/0 (needs the `.lst` beside the rom) |
| port gate | `make check-demo` (background, 60-90 min under load; compare PER ROW landed tick + cell counts with a run on the unchanged build, never the headline) | 18 of 20 rows 0 cells; H 02ba#7 ~13 cells (fallback); H 02ba#13 NOT REACHED (harness latch artefact) |
| long run | ares `--frames 125000`, read DIAG [11] vs [0] | ticks within ~10 of vints |
| level-1 tape | `--input tools/inputs/level1_full.csv --pad1 fighting --frames 36000` (+PORTGODMODE twin) | ticks track vints |
| heatmap / ghosts | `python3 tools/temporal_metric.py <rom> --start 600 --n 60 --out <dir> --heatmap <png>` + a per-16x16-tile alternation scan (crop hash sequence A?A?A for 3+ cycles) | no tile the control lacks |

Screenshots are the arcade oracle's other half: compare to MAME at the same GAME tick (not emulator
frame). `tools/attract_parity.py` and `tools/artaudit.py <rom> <frame>` automate parts of it.

## 5. Open work, in priority order

**2026-10-28 follow-up:** sw336 body/stomach/part cleanup fixed and fully locally gated; play check pending. Level5 remains open after full1-to5playthrough, escaped second white/all visible cleared/movable player/fixed screen/pre-Neff. Fresh5 and scoped4-to5 tests do not reproduce. sw336-lock2 provides readonly live footer, selecting invisible actors or Neff when none remain; request capture on original route. sw336-timing provides smoke phase bars (starts1, no frontend); physical measurements needed before a speed change. Both diagnostics validated and cap-safe, normal336unchanged. No force-clear repair or performance claim. Guides/evidence above.

**2026-10-28 level-four/five bosses accepted:** source397f893a/sw335. Mike confirms both battles and cleanup work on Miyoo. Native/focused tests and all required local gates complete. Level4 width/palette contexts, final-boss table/AI/effects and shared smoke-band contexts fixed. Evidence build/codex-boss45-sw334/RESULTS.md. Separate smoke hardware slowdown and broader coverage remain open; no universal six-button hardware claim.

**2026-10-27 controller investigation / menu:** pad333 standalone diagnostic ready; Mike can test failing hardware/emulators without six-button access to reach it. Physical results pending. Do not import 32x-builder sticky latch or loosen detection without raw phase evidence. Optional boot/title options, level select, sound test are the agreed frontend direction; beast-per-stage choices need stage/attack validation. Frontend implementation is sw334, retained in sw335; Mike accepts level select/button assignment on Miyoo, and all11retention cases pass. Includes the boot-alignment source change. Source and controls: docs/design/FRONTEND.md; evidence build/codex-menu-sw333/RESULTS.md.

**2026-10-27 level-four harpies / retreat:** source ff1c181a, sw333 locally gated, ready for Mike's level-four play pass. Native14A78 is a branch into existing hand14A8C; generated thunk bypassed it with an old inlined broken initializer. New hand thunk delegates. Harpy14BB8 sprite rotation had two decimal8 signed-link dereferences missed by the hex-only W68 pass, measured bad accessesFFFFC908. Fixed exactly those two extra matches. Harpies now visibly fly with both wings. Two natural ROUND3/GOD-only tapes advance past first untransformed retreat to next visit/checkpoint; no new Neff logic or forced unlocking. Ordinary enemy corruption likely caused the reported trap; actual user acceptance pending. Results `build/codex-level4-sw332/RESULTS.md`.

**2026-10-27 level-three boss play update:** Mike confirms sw332 "Boss color fixed." Do not infer blanket acceptance of every combat/projectile/statue case. Damage exists in directed old/new tests, so do not claim newly added damage rules. See newest START-HERE/RESULTS for exact scope. No broad toolkit refactor performed.

**2026-10-27 acceptance / next direction:** Mike accepts smoke fade-in, eye transition, ordinary enemies, transition and crystal picture. He now wants an approach for complete game coverage, reusable detectable translation patterns, and a graphics pipeline that carries to the next title. Recommendation is being prepared; no broad refactor is authorized merely by this strategy question. Current sw330 ROM unchanged.

**2026-10-26 follow-up complete locally, sw330 play check pending:** RAWPENS/TXTTOUCH committed82ea1250 and enabled in candidate defaults. Smoke palette rebakes and HUD damage tracking fixed in targeted captures; full gates pass. Sw329 unchanged. Hardware speed still needs timing capture; diagnostic pair and instructions are build/codex-smoke-sw329/HARDWARE-TIMING.md. Do not infer FPGA FPS from ares or retry the rig. Read newest START-HERE and build/codex-smoke-sw329/RESULTS.md.

**2026-10-26 current work, sw329 built for play check:** Neff chest redraw dependency, crystal tile bank/aligned word copies/fade palette, and runner 124BC ROM-relative speed fixes are committed as 5d0416cb/6f377c76/00015910. Targeted captures,65k/125k/both36k and20 measured arcade object rows pass (H#13 direct tick-end proof). See newest START-HERE entry and build/codex-neff-sw328/RESULTS.md. sw328 is untouched. Do not claim smoke hardware slowdown or its transient background palette strips fixed. The exact runner teleport needs Mike's play check.

**2026-10-26 attract acceptance:** Mike confirms sw328 boot/logo/eye transition: "attract, fixed." See the newest START-HERE entries for code changes and final-gate status. These attract defects are closed by play acceptance. Mike subsequently confirms ordinary level-2 AND level-3 enemies appear in sw328; those spawn reports are accepted. Neff smoke/chest, crystal-ball layout, the warping tailed enemy, and the level-3 boss (not explicitly confirmed) remain open. See the newest START-HERE entry for screenshot ranges.

**2026-10-26 play-pass update:** Mike confirms these defects with the local sw326 ROM in both ares and hardware; EVERY launch is a cold start: Neff smoke slowdown and chest dropout; level-2 tailed walkers and snake bodies missing in play; level-3 enemies/boss absent; attract eye-pan transition garbage and full-screen logo tile issues. No purple tiles seen; level 1 closer to parity. Local sw326 stamp is 5c18c7b4. Earlier level-3 ares results do not establish hardware acceptance. See newest START-HERE entry.

1. **Level-2 snake bodies merged and ares gated:** 74f48a1c (exact hand file from ac399ef9), local candidate sw327. Full measurements are in the newest START-HERE entry and build/codex-sw326/. Mike confirmed on 2026-10-26: "Bodies draw correctly". Snake bodies are play-accepted; the other defects remain open. Ordinary level-2 and level-3 enemies were subsequently play-accepted on sw328. The purple runner warp is a separate report, addressed in sw329 by the signed ROM-relative speed lookup; its play check is pending.
2. **Neff chest fix in sw329, play check pending:** keeper invalidation propagation eliminates large holes in targeted captures. Original reproduction captured 60 consecutive fight frames at build/codex-sw326/neff/ (contact sheet neff-contact.png). Intermittent black torso gap reproduced; exact arcade animation comparison is still needed. A no-KEEPSPR probe fails compilation with the current clear flags, so no such probe ROM exists.
3. **Mike play-pass items waiting:** sw326 START-as-coin on the Analogue Pocket / Miyoo cores (they hide
   X/Y/Z/MODE); sw325 levels 2 and 3 (enemies, snail boss). Do not touch the rig; ask Mike for results.
4. **Snail boss colours (level 3), fixed sw332 and accepted by Mike:** palette contexts restored for head/parts and shots. Retain reproduction details: originally head drew brown and shots grey; the arcade has pale green / orange.
   Sprite palette lane (`sh_src/r60/r60_planes.c`, set/pen allocation). Reproduce with `PORTROUND=2` +
   `GD_ENDCLEAR`/`PORTENDCLEAR` (the tape cannot aim; a trailing enemy holds the scroll lock) and
   `tools/inputs/level3_fightback*.csv`.
5. **Level4/5 boss battles and cleanup play-accepted in sw335:** native and port probes in build/codex-boss45-sw334/. Broader stage/enemy/attack and multiplayer coverage remains separate work; do not call the entire game covered.
6. **Six extraout feeds the register-class rule cannot express** (each needs a hand file from the 68K
   listing; `~/src/marsdev/mars/m68k-elf/bin/m68k-elf-objdump` on `roms/altbeast/prog68k.bin`, from a real
   function boundary in `docs/audit/func_ranges.txt`): `FUN_00003190`/`FUN_000038c0` extraout_var + the ones
   digit (68K uses D4; port uses a stale byte), `FUN_00004254`/`4276` A0/A1 (0x4238 writes them),
   `FUN_00004330` A1 (a local pointer) and D0w (0x41AA's checksum), `FUN_00013b2c` D0 (restore_position
   writes D0). Details: START-HERE entry "EXTRAOUT FEEDS BY REGISTER CLASS".
7. **Smaller:** coin B decode in 0x40E (`0xFFF00C` 0x02 vs 0x12; unreached; 0x40E is an SP-reset entry, a hand
   file conflicts with the `sprst` pass); credit text blink ~30 frames out of phase; the name entry ends 2 frames
   later than the arcade (input latency); three game frames in one wall frame after the high-score install
   (contradicts the one-tick-per-vint cap, `sh_src/m_main.c:14137`); `FUN_00004276` P2 name entry unexercised.
8. **Needs Mike present (rig):** (a) Neff smoke entrance/exit slowdown on level 1 (ares loses no ticks there; the
   master SH-2 runs out of vint on the FPGA): build `R60RIGSPLIT=1 R60NOBARS=` probe, Mike plays, capture the
   screen each second, read the master's per-vint phases; (b) the fight frame rate (~30 fps presented on the
   FPGA vs ~60 arcade): the slave's framebuffer stores are the floor; the START of any speed card is a rig phase split.
   Findings so far: START-HERE "RIG PHASE SPLIT" and the memory note `ares-speed-not-rig-speed.md`.
9. **Held, not merged (do not merge blindly):** two-core sprite split `R60MBAND*` (hurts the rig: master has no
   idle there); `PORTHOT2` T3 (56 functions; T1 is on the line); the pink-eye-skin eviction branch
   `wf_271ad370-e23-2` (superseded by `palfix`, already on the line as `8ad4985d`); `R60STAGEGATE`; `R60HTPROBE`.

## 6. What was fixed (context, newest first; SHAs are on advisor-bisect, see `git log --oneline`)

START as coin (PORTSTARTCOIN) / credits follow the coinage DIP (`input_edges`, 2 coins = 3 credits) / level 3
enemies + boss + slot 61 / stale-palette class: green sky, pink skin, face-flame orange (`r60_map_sync`) /
purple tiles (off-screen column pins) / game over: CONTINUE countdown + name entry + lives icon / orb loss:
flame + one orb per power level / extraout feeds by register class / intro: Zeus text (width) + yellow bolts
(feed) / green eye (`R60PLANNEW`) / **15-minute attract hang = port stack leak** (`lea sp` entries reached by `jmp`
became recursive calls; fixed by a cport_prep pass + `port_sp_reset`) / werewolf wild jump + 5 form tails /
punch stale cached reads + line-223 garbage / check-demo mid-tick sampling (`tick_end` replay) / Ghidra pins
(`ghidra-run`; 12.2 rolled back: no pipeline reproduces altbeast.c).

## 7. Lessons that will save you a day

- **A "renderer stall" can be a port crash**: read the master's PC / exceptions with the SDRAM write watch
  before the renderer. Soak past 60k frames: slow leaks hide under 30k.
- **Decompile classes recur:** register-file gaps (`unaff_<REG>` AND `p_<REG>`), routines returning two registers,
  declared width vs the 68K access width (`CPORT_WIDTH` in the Makefile), a jump/call through a local holding a
  68K address, an SP-reset entry, a callee that advances A5/A6 (`lea 0x80(a5),a5`) that the caller never sees,
  raw 68K addresses (templates read through the SH-2's own address space: map through `ROM_BASE`).
  A shared shape gets a generator pass; a single function gets a hand file.
- **Identify before theorising:** name the layer / colour set / object in the ARCADE first (MAME dump), then ours.
- **Sampling phase fools gates:** compare at the game's own tick; a rom's timing shift moves transitions by frames.
- **Tapes are keyed to emulator frames** and diverge per build: "same frame" comparisons across builds are weak.
- **Every candidate to Mike:** `make candidate NAME=swNNN`, ares gates, a rig strip when the rig is reachable
  (`tools/rigpass.sh <abs rom> <name>`, read the montage; re-strip the control), then START-HERE entry; copy to
  the rig unlaunched with `tools/mister_push.sh --name mister.office.local -n <rom>` (Mike may be playing).

## 8. Handing back to Claude

Leave `advisor-bisect` clean and building; every gated change committed with its numbers in the message;
a dated top entry in START-HERE listing what you merged, what you left ungated, and what is unproven;
update this file's section 5 (strike done items, add new ones). Mike's "ADHD mode" preference: when you
write to him, lead with the outcome, keep it short, tag facts [CERTAIN]/[LIKELY]/[INFERRED], no em-dashes,
no emoji, and tell him bluntly when he is wrong.
