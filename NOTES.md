# sega16-2-32x — Altered Beast (System 16B) → Sega 32X

Port the original arcade 68000 code to run natively on the Genesis/MD 68K,
with the two 32X SH-2s emulating the System 16B video hardware into the
32X framebuffer.

## Target set

`altbeast` (MAME parent, System 16B, ROM board 171-5521):
- 68K program: `epr-11907.a7` (even) + `epr-11906.a5` (odd) → `roms/altbeast/prog68k.bin` (256KB)
  - Vector table valid: initial SP=`0xFFFFFF00` (work RAM), initial PC=`0x400`
  - **Unencrypted** — no FD1094. Only an i8751 protection MCU (`317-0078.c2`, 4KB, dumped)
- Tiles: `opr-11674/75/76` (3×128KB = 3 bitplanes)
- Sprites: `epr-11677..11684` (8×128KB, 16-bit interleaved)
- Sound: Z80 `epr-11671.a10` + uPD7759 samples `opr-11672/73` (YM2151 + uPD7759)

## Hardware mapping

| System 16B | 32X side |
|---|---|
| 68000 @ 10 MHz | MD 68K @ 7.67 MHz (risk: ~23% deficit; game is slow-paced) |
| Tile/sprite/text layers | SH-2 ×2 software render → 32X 15bpp framebuffer |
| 4096-color palette (5-bit/gun+shadow) | 32X 15bpp direct color |
| 320×224 | 320×224 (native match) |
| Z80 + YM2151 + uPD7759 | TBD: SH-2 YM2151 emu → PWM, or YM2612 retarget |
| i8751 MCU | Study its behavior in MAME, patch/simulate in shim |
| 315-5195 dynamic memory mapper | Game programs it once at boot → hard-wire that layout |

## DECODED: altbeast 68K memory map (from boot code + mapper table)

Boot code at 0x440 copies a 16-byte table from ROM 0x1986 to 0xFE0020-0xFE003E.
The 315-5195 mapper listens on the low byte lane of the ENTIRE address space
after reset, reg index = (word_addr) & 0x1F (315_5195.cpp:497). Those writes
land in regs 0x10-0x1F = eight (size,base) pairs. Table bytes:
`02 00 02 08 00 3f 00 ff 04 44 0d 40 00 84 00 c4`

Decoded (315_5195.cpp:475 compute_region; size code &3 → mask 64K/128K/512K/2M):

| Region | 68K address | What |
|---|---|---|
| 0 | `0x000000-0x03FFFF` | Program ROM (256KB) |
| 1 | `0x080000-0x0BFFFF` | 2nd ROM sockets (empty on altbeast) |
| 2 | `0x3F0000-0x3FFFFF` | Tile bank regs (`rom_5704_bank_w`; boot uses movep at 0x472) |
| 3 | `0xFF0000` (16KB mirrored) | Work RAM, effective `0xFFC000-0xFFFFFF` |
| 4 | `0x440000` | Sprite RAM 2KB (mirrored across 64K) |
| 5 | `0x400000-0x40FFFF` + `0x410000-0x410FFF` | Tile RAM 64KB + Text RAM 4KB |
| 6 | `0x840000` | Palette RAM 4KB |
| 7 | `0xC40000-0xC43FFF` | I/O (315-5296) |

I/O ports (standard_io_r/w, segas16b.cpp:1076):
- `0xC40001` W: bit6 flip, bit5 display-enable, bits2-3 lamps, bits0-1 coin counters
- `0xC41001/3/7` R: SERVICE / P1 / P2
- `0xC42001/3` R: DSW2 / DSW1
- `0xC43007` W: boot writes 0x80 — not in standard map, TODO identify
- Sound latch: NOT in I/O space — see MCU below

Interrupt vectors: ALL point to rte stubs except IRQ4 (vector 0x70) →
0x404 → vblank handler at **0x2AAC**. IRQ4 is the only interrupt used.

## i8751 MCU — it's the system's conductor (critical finding)

The MCU's external data bus maps to the mapper's 32 regs (mcu_data_map,
segas16b.cpp:1944). Through them it can:
- Read/write ANY 68K bus address (regs 0x00-0x01 data latch, 0x07-0x0C address
  latches, reg 0x05 = do-read/do-write trigger)
- Write the Z80 sound latch (reg 0x03) — the 68K program NEVER touches the
  sound latch itself (zero refs to 0xFE0006/7 in the disassembly)
- Raise 68K interrupts (reg 0x04, negative logic: write 0x0B = IRQ4)

Wiring: screen vblank → MCU IRQ0; MCU port1 in = SERVICE inputs (coins!).
So per frame: MCU takes vblank, reads coins, pokes results into work RAM,
passes sound commands 68K→Z80, and raises IRQ4 on the 68K. **The 68K's vblank
comes FROM the MCU.**

Port implication: our shim must replace the MCU. It's 4KB of 8051 code (dumped).
Options: (a) disassemble + reimplement its protocol in the shim (preferred,
it's tiny), (b) run a small 8051 interpreter on the slave SH-2.
MAME runs the real dump for altbeast (no HLE sim exists to crib) — but jts16
`cores/s16b/` and MAME's tturf/wb3 sims (segas16b.cpp:1333) show the protocol
shape.

## References on disk

- MAME driver (sparse clone): `~/src/mame-ref/src/mame/sega/`
  - `segas16b.cpp` (driver, ROM defs at :4804), `segas16b_v.cpp` (video),
    `sega16sp.cpp` (sprites), `segaic16.cpp` (tilemaps), `segaic16_m.cpp` (mapper/math)
- jts16 FPGA (clone): `~/src/jts16-ref/cores/s16b/` — cycle-accurate 16B reference
- MAME ROM sets: `/Volumes/Games/MAME/MAME 0.275 ROMs (merged)/`
- Prior 32X project (toolchain patterns, crt0 fixes): `~/src/32x-builder/`
  - NOTE: its DEVLOG documents that marsdev's stock `mars_start.s` secondary-SH-2 boot is broken — reuse the fixed crt0 from there.

## Toolchain

marsdev at `MARSDEV=~/src/marsdev/mars` — m68k-elf-gcc 15.2.0 + sh-elf-gcc 15.2.0.
Build pattern: copy `~/src/32x-builder/Makefile` md_src/sh_src split.

## Phases

1. **Recon** — disassemble prog68k.bin boot code; find mapper writes, VDP-ish
   accesses, MCU touchpoints. Extract + decode tiles/sprites to PNG to verify
   our understanding of the formats.
2. **Video model** — SH-2 renderer for text layer only (it's the simplest:
   8x8, fixed position); get "INSERT COIN" style output on emulator.
3. **Run the code** — MD 68K executes prog68k with trapped memory map;
   VRAM writes forwarded to SH-2 side (write-through to SDRAM shadow).
4. **Tiles + sprites renderer**, priorities, row/col scroll.
5. **Inputs, MCU behavior, sound.**

## ROMs are NOT committed — .gitignore excludes roms/. Rebuild prog68k.bin:
interleave a7 (even/high) + a5 (odd/low).
