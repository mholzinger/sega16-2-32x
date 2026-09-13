# HANDOFF — the Golden Axe decompile thread

Written 2026-09-12 by the Altered Beast decompile thread. Short on
purpose: the method is the same as AB's, the game is new. Read this,
then `docs/log/LOOP-DECOMPILE.md` entries 50, 71 and 88, then
`TOOLKIT.md` from "Stage 3 title: GOLDEN AXE" to the end of "Per-title
work". Nothing else is required before the first command.

---------------------------------------------------------------------
## WHEN THIS STARTS

Stage 0 (Altered Beast at one generation per vint) keeps priority, and
the rendering thread stays on it until the wall is under 1.00
(`docs/handoff/PLAN-SINGLE-VINT.md`). This thread can READ Golden Axe
now; it hands nothing to a builder until Mike opens the builder side
for it. Until then there is nobody to duplicate (entry 71), which will
not stay true: the moment a builder is on this title, `git log
--oneline -20` before every finding.

---------------------------------------------------------------------
## WHAT IS ON DISK (verified 2026-09-12)

`roms/goldnaxe/` and `mame/goldnaxe.zip`, set 6 US, `mame -listroms
goldnaxe` clean:

    program   epr-12545.ic2 + epr-12544.ic1   2 x 256 KB = 512 KB (AB: 256 KB)
    tiles     epr-12385/12386/12387           3 x 128 KB
    sprites   mpr-12378..12383                6 x 256 KB
    Z80       epr-12390.ic8                   32 KB  (AB: epr-11671 -- a DIFFERENT program)
    PCM       mpr-12384.ic6                   128 KB (uPD7759)
    MCU       317-0123a.c2                    4 KB   (AB US: 317-0078)
    mapper    315-5298.b9                     PLD

Same board as AB: System 16B, 315-5195 mapper, YM2151 + uPD7759, i8751
conductor. jtcores `srcref/jtcores/cores/s16b/hdl/` is the spec for all
of it (derive and cite, never copy).

---------------------------------------------------------------------
## RUNGS, IN ORDER

Each rung has a command, an output file, and one sentence in the log.
Ballpark: AB's decompile took three sessions and 107 entries to the
facts the native renderer needed; Golden Axe's program is twice the
size, but the kit and the tools exist now. Two to three sessions to
rung 5 is the honest guess, one session if `game_derive.py` aligns
well.

**1. The program image.** `roms/goldnaxe/prog68k.bin` does not exist
yet; every tool reads it. Build it by interleaving the two program ROMs
the way MAME's `segas16b.cpp` loads them (`ROM_LOAD16_BYTE`, ic2 at the
even byte, ic1 at the odd -- VERIFY against the driver source, do not
trust this line). Proof it is right: the reset vector at offset 4 lands
inside the image and disassembles as a boot that writes the 315-5195
mapper table (AB's is at 0x1986; see `NOTES.md` for the decode).

**2. The rig.** `tools/ghidra_run.sh` is hard-wired to altbeast (lines
12-13); give it a GAME argument, import, then `census`
(`tools/ghidra/timing_census.py` -> `docs/audit/goldnaxe/`). The arcade
ORACLE is `mame goldnaxe`; every headless run takes the flags in
`CLAUDE.md` ("Headless MAME"), including `-bench`. Find the
discriminator scene first (TOOLKIT "Geometry-convention rule"): a
unique, asymmetric full-bleed screen at a known scroll value. Golden
Axe's title art or the first map screen are candidates; pin it in the
log with the register values.

**3. The write-tap census.** TOOLKIT step (a): `tools/write_census.lua`
over every mirrored hardware range (text/tile RAM 0x400000-0x410FFF,
sprite RAM 0x440000, palette 0x840000, I/O 0xC40000) BEFORE any thunk
table exists. This yields the site list, the extents and the indirect
writers. It is also what tells the builder how much the game changes
per frame (TOOLKIT step c: AB needed paired 128-word palette pushes
because its cyclers rewrite every vint; Golden Axe may not).

**4. The frame protocol.** IRQ4 handler and vector (the patcher derives
GAME_IRQ4 from the vector table), the game's frame-flag wait loop, the
display-gate use of port 0xC40001 bit 5, and the 68000 work per vint
from `tools/arcade_trace.py` -- READ ENTRY 88 FIRST: MAME collapses
tight loops into one `(loops for N instructions)` line and the tool now
expands them; the trace regex takes 6- to 8-digit addresses. Report the
same three numbers AB's plan is built on: instructions per vint, the
sound-post routine's share, and the biggest single routine.

**5. The MCU mailboxes and the patcher tables.** `python3
tools/game_derive.py altbeast goldnaxe` -> `tools/game_goldnaxe.py`
plus a miss report; correct every miss by hand from the CONSUMING
instruction (entry 50). The MCU mailboxes (busy / coins / sound
command) come from the 68000's read sites, never from AB's addresses:
altbeastj's MCU used different addresses in a different order and the
port took no coins until that was derived (TOOLKIT "Second title").
DATA_EXCLUDE needs its own list: AB's spawn script looked like tile-RAM
operands to objdump and patching it corrupted a spawn.

**6. Sound, for the sound thread.** `tools/sound_posts.py` maps every
sound-post call site to its argument and caller; re-derive the posting
convention for this program first (AB's is in `docs/sound/HANDOFF-SOUND.md`,
decompile section). The Z80 program is not AB's, so
`docs/sound/SOUND_DRIVER.md`'s map is a starting hypothesis, not a
fact. Output `docs/audit/goldnaxe/sound_posts.md`.

**7. The facts the native renderer asks for**, using AB's entries as
the template (LOOP-DECOMPILE 92-107): page-select shadows and the
scene byte that switches them (AB: 0xFFF148, entries 92-94); the
palette writers and their footprints (entry 95); text-layer writers
(99); the attract / credited bit and the attract step byte (103/105,
and note that AB's first reading of that bit was INVERTED and cost a
regression -- measure it on the arcade before writing it down); the
cat-1 foreground census, whole vs per-pixel (104/106); the sprite
priority census per round (NOTES-FROM-DECOMPILE 36). Same sprite chip,
same record format: word 0 low byte = top, high byte = bottom, word 2
bit 15 = end, word 4 bits 7-6 = priority.

---------------------------------------------------------------------
## RULES THAT DO NOT CHANGE

  - Provenance (entry 50): a claim names the instruction that consumes
    the value, or it is marked as a hypothesis. This test caught all
    six wrong claims of AB's session 1 and none of the 21 right ones.
  - Measure before arguing; re-read a counter before building on it.
  - Accuracy before speed; Mike's play pass on ares / the rig is the
    acceptance gate; MAME's 32X is a convenience model (CLAUDE.md).
  - Nothing from `srcref/` is copied or redistributed; the Ghidra
    project and `docs/audit/*/timing_census.json` stay gitignored.
  - Commit locally; never push unprompted; the auto-commit hook sweeps
    edits into `[wip]` commits, so git timestamps are the clock.

## TRAPS AB PAID FOR (do not pay again)

  1. `set -- $v` in zsh does not word-split; every "flag build" was the
     baseline. Spell flags out and check `.build_flags` per rom.
  2. A per-row coverage figure must be the UNION of rows; the
     nearest-preceding-row count overstated AB's map coverage 28 -> 21.6.
  3. The rom unpacker's zero run is n+1, not n (LOOP29 243); if Golden
     Axe uses the same packer, re-verify on the arcade before counting.
  4. Reading the wrong buffer gives a clean, wrong census: sprite
     records live at 0x440000, not the game's work-RAM staging copy.
  5. A finding the builder acted on an hour ago looks like an open
     question in your own notes (entry 71). `git log` first.

## DELIVERABLES

    docs/log/LOOP-DECOMPILE-GOLDNAXE.md        the log, same format as AB's
    docs/handoff/NOTES-FROM-DECOMPILE-GOLDNAXE.md   handoffs to the builder, numbered
    tools/game_goldnaxe.py                      the patcher tables
    docs/audit/goldnaxe/                        census outputs (json gitignored)
    roms/goldnaxe/prog68k.bin                   rung 1 (gitignored with the rest of roms/)

First command of the new thread:

    ls roms/goldnaxe && mame -listroms goldnaxe -rompath ./mame | tail -3
