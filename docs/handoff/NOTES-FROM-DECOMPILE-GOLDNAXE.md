# NOTES-FROM-DECOMPILE-GOLDNAXE — handoffs to the builder, numbered

Each note cites the LOOP-DECOMPILE-GOLDNAXE entry that established it.
Nothing here is acted on until Mike opens the builder side for this title.

## 1. The memory map is different, and sprite RAM is a variable (entries 2-3)

Golden Axe (171-5797 board) runs with tile RAM at 0x100000, text RAM at
0x110000, palette at 0x140000, tile bank at 0x1F2001/3, I/O at 0xC4xxxx,
work RAM 0xFFC000-0xFFFFFF. Not AB's 0x400000/0x410000/0x840000/0x3F0000.
The patcher's rebase tables and the tile/text/palette dirty-site scans
must use these ranges (`tools/game_goldnaxe.py`, rung 5).

Sprite RAM has NO fixed address: the i8751 remaps region 4 every vblank
and publishes the base in the long at 0xFFECC4 (high word = page). The
68K's sprite list copy writes through a2 loaded from that variable
(0x2F80-0x2F86), so there are no sprite-RAM literals to rebase. The shim,
which replaces the MCU, sets 0xFFECC4 once to the port's sprite staging
buffer and never rotates it. The 80-record clears at 0x39BA / 0x568CE
read the same variable.

## 2. The MCU checksums 68K ROM 0x714-0x7FE at boot (entry 3)

Observed on the arcade: before releasing the 68K from reset the MCU reads
0x714..0x7FE through the mapper (frame 10), then writes reg 02 <- 00
(reset release). AB's MCU did the same over its first 2 KB. Either keep
those bytes untouched by the patcher (check `DATA_EXCLUDE` / rebase
sites against the range) or, since the shim IS the MCU here, drop the
check. The shim must also send sound-latch 0x40 at boot (frame 11) and
raise IRQ4 per vblank, as it does for AB.

## 3. Tile bank writes are on the 68K, not the MCU (entry 2)

The IRQ4 handler writes the 5704-style bank register itself
(`movep.w d0,1(a0)` at 0x2F94, a0 = 0x1F2000, value from 0xFFEC95, two
bytes per vint). AB routed the request through the MCU. The port's
tile-bank thunk therefore hooks one 68K site, not a mailbox.

## 4. The 5797 math chips are live (entry 5)

Golden Axe writes the 315-5248 multiplier (0x1F0000/2), the 315-5250
compare/timer (0x1F1000-0x1F1008) and region 2 (0x1E0000-0x1E0008,
MAME's `unknown_rgn2`) during play. AB used none. The shim must emulate
the multiplier and compare/timer in the 68K's address space (reads
included; the read census is rung 4) — RTL spec `jts16b_mul.v` and
`jts16b_timer.v` in `srcref/jtcores/cores/s16b/hdl/`, derive only.

## 5. Per-frame delivery units, measured on the arcade (entry 5)

Level-1 play, per frame: tile RAM 0 (stage loaded at the cut), text RAM
142 writes (HUD rows + a 0x110746-0x110CF8 block that is probably a row
scroll table — hypothesis), palette 28 writes over a fixed 24-word
window 0x140050-0x14007E, sprite records 14 (42 writes), I/O 2, tile
bank 2. Golden Axe's per-frame palette is 48 bytes, not AB's paired
128-word pushes.

## 6. The vint handshake has two halves, and the shim owns both (entry 6)

Per vint the conductor must: raise IRQ4 (the handler sets 0xFFEC1C
itself), and AFTER the game has cleared them write the four words
048C 159D 26AE 37BF to 0xFFECD8/DA/DC/DE — the game spins at 0x3CA2
until they are back. Missing the second half hangs the game at the
first frame wait. Missed frames are counted by the game at 0xFFED4C.

## 7. Sound posts go through the mailbox 0xFFECFC (entry 6)

The game queues commands in a ring at 0xFFEC40-5F, IRQ4 pops one per
vint into 0xFFECFC, the MCU forwards any value != 0xFF to the Z80 latch
and rewrites 0xFF. The shim replaces the MCU: read 0xFFECFC each vint,
post if != 0xFF, write 0xFF back. One direct path exists (0x3674 writes
mapper reg 3 at 0xFE0007 itself, used for stop-all); the patcher must
redirect that single site. 0xC43001 is NOT the latch.
