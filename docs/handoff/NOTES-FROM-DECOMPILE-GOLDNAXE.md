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
