# Codex → Claude handoff, 2026-10-28

Mike explicitly requested this handoff because Claude's plan limits have reset.
This is the current entry point. Read `HANDOFF-CODEX.md` for project constraints,
`CLAUDE.md` for repository workflow, and newest `START-HERE.md` entries for history.

## Worktree and source state

Work in `/Users/mikeholzinger/src/sega16-2-32x/.claude/worktrees/advisor-bisect`,
branch `advisor-bisect`. Do not work from the root checkout by accident.

Recent local commits:

- `397f893a`: sw335 level-four/five boss movement, state, palette and death fixes.
- `b19abf2c`: sw336 level-four stomach/body visibility and detached-part cleanup.
- `f00f710c`: optional read-only scroll-lock footer and scoped transition probes.
- `67d710cc`: footer falls back to Neff's record when no ordinary actors remain.

No gameplay changes after b19abf2c. Later commits only add optional diagnostics.
The documentation accompanying this handoff is being saved locally. No push.
Preexisting untracked `sndtest/sh/speech_bank8.h` belongs to the existing tree;
do not add or overwrite it. `docs/proof/altbeast.c` is the sole untracked generator
input: NEVER edit, overwrite, or regenerate it. No rig/config changes were made.

## Highest priority: level-five scroll lock, still OPEN

**Latest report, after testing sw336:** Mike says, “I JUST played
./.claude/worktrees/advisor-bisect/build/port/rom/sw336.32x and the level five
lock is the same. I obtained two [white] wolf, spirit orbs. Locked at the same
area in level five.” Treat this as explicit confirmation sw336 did not fix it.
The new detail is two spirit orbs collected; the player has not collected three.
Do not conflate collecting two orbs with the earlier report that the second
white wolf escaped. The exact wolf/orb sequence is not established.

Earlier confirmed facts:

- He reached level five through earlier levels, not level select.
- Every launch is a cold start. Do not revisit stale-save-state explanations.
- A white wolf escaped offscreen in the reported sequence.
- All visible enemies were cleared.
- Player can still walk/jump; the camera does not scroll.
- It happens before Neff is visible, rather than at a visible Neff encounter.

**No root cause and no level-five repair have been established.** Do not force
clear the enemy count/scroll flag as a substitute for native behavior.

Tests that did NOT reproduce the reported full-route failure:

- Natural level-five probes: ROUND4/GOD only, no forced FORM/ENDCLEAR. Escaped
  whites clear F15A. Default combat and Easy (actual menu DIP94) advance visits
  0→1→2→3 and reach the boss. A no-attack run locks at camera12E8 with four
  ordinary actors still alive; later combat clears them and scrolling resumes.
- Scoped level-four-to-five carry test: native boss defeat/round transition,
  FORM/ENDCLEAR limited to round four and disabled for round clear/level five.
  Level five enters with power0000, remains untransformed, clears escaping wolves,
  advances beyond first Neff encounter. At35995 camera14C0/count0.
- These probes do not cover the user's complete levels1→5 history or exact
  two-orb route. Emulator-frame tapes diverge between builds and emulators.

**Prepared physical diagnostic:** `build/port/rom/sw336-lock2.32x`.
It retains the menu and normal progression, with no forced round, form, enemy
count, lives, or boss health. Read-only footer displays camera/limit, enemy count,
white-active/lock flags, Neff visit, power and DIP, live actor mask, and a cycling
selected actor handler/X/Y/status/state. Prefers undrawn actors; falls back to
slot40 (hex28), Neff, when the ordinary table is empty. A short clip at the lock
can expose an invisible actor or stale count. It has not yet been play-tested by
Mike. Use lock2, not the older sw336-lock variant.

Guide: `docs/handoff/LEVEL5-LOCK.md`. Host tests prove game RAM unchanged,
framebuffer bounds, hold behavior, decoding and selection. Cold actual-menu tests
show a readable footer matching RAM. Code respects RAMTEXT cap. A normal build
with diagnostics disabled matches sw336 code/data outside its build stamp.

Suggested next move: inspect the two-orb route and cross-stage state/counters;
use the prepared full-route footer capture to identify the held condition.
Do not treat passing forced-round boss fixtures as proof of ordinary progression.

## Level-four stomach: implemented and locally verified, play acceptance pending

sw336 restores stomach/body parts41/42. Native19476/194A4 test bit3 of BYTE at
FFD400, but the ushort declaration made generated code check the low byte.
Extended existing `_boss_status_byte` pass in `tools/cport_prep.py` to those
entries, requiring five exact matches and preserving actual word writes.

Restored detached19596 parts uncovered unsigned `-1 < DATffffd420`, which never
recognized negative head death state. New hand `FUN_00019596.c` implements native
signed-byte death test and left-camera cutoff, delegates geometry/draw to195C6.

Three generator regressions and seven actual-hand signed-state/boundary cases
pass. Parts draw1633/1633 head samples versus0/643 in sw335; zero wrong neck
placement samples. Alternate-facing normal-input probe reaches HP0, clear/next
stage; zero live detached clones during sampled negative-head death state.
Mike has NOT specifically confirmed the restored stomach on hardware. His new
sw336 report confirms the level-five lock, not acceptance of the body change.

## Smoke entrance/explosion slowdown: OPEN, no speed patch

Do not equate ares throughput with physical frame rate. Ready diagnostic:
`build/port/rom/sw336-timing.32x`, existing R60RIGSPLIT phase bars.
Frontend is disabled ONLY in this diagnostic to fit normal RAMTEXT budget;
it starts level one normally, preserves Start-as-coin and normal controls.
No GOD/FORM/ENDCLEAR/forced ROUND. Normal sw336 keeps its frontend.

Guide: `docs/handoff/SMOKE-TIMING.md`. Capture entrance, ordinary fight and death
with bottom28 rows visible, ideally emulator FPS too. Rows199/200 sprite plot /
FG mask;209 presentation interval;213 master tick;217 background preparation.
Bars add cost, so compare phases within the same probe. Cold2501 validation
shows live counters. No physical measurements, FPS gain or speed fix claimed.
Earlier top-edge death-smoke color report was addressed by shared smoke-band
contexts in sw335; any remaining color anomaly needs a fresh capture.

## ROMs to preserve

| ROM | Source | SHA256 |
| --- | --- | --- |
| sw335.32x | 397f893a | 1e198ccfac148856267007bedf87629ff687390087fa62123bc2ab6b015011af |
| sw336.32x | b19abf2c | 00b64ed20a109114cf38d76379b7f49a3ef417c9ca3072f184174bdc186b58b2 |
| sw336-lock2.32x | 67d710cc | 6261eca917496f769024c8b656ad708e5b53f4b24e2bac578431dd45ed21877f |
| sw336-timing.32x | b19abf2c | fbe9882d4c665627405ce368e63646b9154020bda7acffbc6659b121d4791f16 |

All under `build/port/rom/`. Do not overwrite these named artifacts. Use a new
candidate name for a future fix. Builds share build/port and must run serially.
Normal336 stamp11:30:28; lock2 stamp12:20:51; timing stamp11:40:05, all2026-10-28.

## Completed normal sw336 local gates

Full evidence: `build/codex-followup-sw335/RESULTS.md` and subdirectories.
65k ticks/vints64992/64986;125k124991/124985; normal36k35992/35985;
god36k35991/35985. Eleven actual frontend tests pass; roster297/697 and expected
handlers; art1200/3700 zero; quant65536 PASS; temporal46/59 and55/59 with no new
alternating tiles versus335. check-demo19 reached object/sprite rows zero;
exact H7/H13 direct tick-end proofs complete20 original checkpoints. Existing
H13 frame-end sampling, T1057/E3184 picture flags and legacy FB staging flags
remain; no blanket picture parity/complete-game-coverage claim.

Normal _end060177D0, RAMTEXTend06037FC8,39hot/802dispatch. Thirty-seven known
static map collisions unchanged. Footer adds four RAMTEXT bytes and still fits.
No required local gate remains running. No need to rerun them for this docs-only
handoff; a future gameplay change needs its own focused proof and required gates.

Other accepted progress: sw328 attract and ordinary level2/3 enemies, sw334 menu /
level select / button assignments, sw335 both final battles and large Neff cleanup
are explicitly play-accepted. Preserve those fixes. Broad coverage, multiplayer,
and universal six-button hardware support are not established.

## Practical constraints and direction

Never push or stash. No rig retries when SSH refused; no rig configs. MAME oracle
runs headless/dummy/input-disabled with fresh cfg/NVRAM. Ghidra is available via
`~/tools/ghidra/bin/ghidra-run` and wrapper, not a hardcoded launcher. Native
objdump was sufficient for the latest fixes. Read FACTS before re-deriving.

Mike wants reusable translation patterns and a graphics pipeline usable for the
next System16 port. Shared proven shapes belong in generator passes; isolated
native register/state fixes in hand functions. We may optimize C for 32X while
preserving observable arcade behavior. No broad refactor was done in this session.
Keep progress updates visible; Mike previously thought Codex had stopped when
there was no feedback. Do not bill Claude through `claude -p` or API tooling.
