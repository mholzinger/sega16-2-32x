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

### MCU FULLY REVERSE-ENGINEERED (roms/altbeast/mcu.asm via unidasm)

Vectors: reset→0x180, INT0 (vblank)→0xC00. INT1/timer1 handlers are bare reti.

**Boot sequence** (0x180→0x680→0x880):
1. Hold 68K in RESET (mapper reg2=0x03), self-checksum MCU ROM, internal RAM test
2. Program mapper regs 0x10-0x1F from its OWN table at MCU ROM 0xFEA —
   byte-identical to the 68K's table at 68K ROM 0x1986 (redundant by design)
3. PROTECTION: checksum first 2KB of 68K ROM over the bus (separate 16-bit
   sums of high and low bytes) vs constants at MCU ROM 0xFFA-0xFFD; halt on
   mismatch
4. Release 68K reset (reg2=0x00), write **0x40 to sound latch** (init/silence
   command), enable INT0, enter main loop

**Main loop** (0xA80, continuous polling):
- Read word 0xFFF0C0: if high byte != 0 → set busy flag, poll word 0x410002
  (TEXT RAM) until high byte == 0, clear busy. (Screen-sync handshake; while
  busy, vblank fires IRQ4 only and skips input/bank work.)
- Sound mailbox: read word 0xFFF0C4; if high byte != 0xFF, write it to the
  Z80 sound latch (reg3) and rewrite the mailbox high byte to 0xFF.

**Per vblank** (INT0 handler 0xC00):
1. Read P1 (coin/service, active-low), invert, write to high byte of 0xFFF0C2
   — the 68K reads its coin inputs from work RAM, not from the I/O chip
2. Read low byte of word 0xFFF094 (= 0xFFF095) = tile bank request from 68K;
   write 0x0000 to 0x3F0000 and 0x00:req to 0x3F0002
3. Raise IRQ4 on the 68K (reg4=0x0B) — every frame, unconditionally

**32X shim duties** (complete replacement, no 8051 emulation needed):
- MD vblank (IRQ6, vector 0x78) → shim: read MD pad → arcade bits →
  0xFFF0C2; forward 0xFFF095 → tile bank state; then jump to the game's
  IRQ4 handler (0x2AAC). Patch vector 0x78 in our ROM image.
- Main-loop duties fold into the same handler: pump 0xFFF0C4 sound mailbox,
  honor the 0xFFF0C0/0x410002 handshake.
- Skip the 2KB checksum entirely; send sound cmd 0x40 at boot.

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

## Graphics formats — VERIFIED by decoding to PNG (tools/decode_gfx.py)

- Tiles: 3 planar ROMs (opr-11674/75/76 = planes 0/1/2), 8x8 3bpp,
  8 bytes/tile/plane, 16384 tiles. Verified: font, SEGA logo, terrain visible.
- Sprites: 8 ROMs → 16-bit words (even byte = b5/6/7/8 socket, odd = b1/2/3/4),
  4bpp nibbles MSB-first, variable-width rows, row ends when a word's final
  nibble == 0xF (sega16sp.cpp:1389). Pen 0 = transparent, pen 15 = marker.
  Verified: hero frames, transformation faces, wolves, bosses, logo visible.
- Previews land in gfx-preview/ (gitignored — copyrighted art).

## Testing

- ares emulator installed locally — primary 32X test target for phases 2+.

## PHASE 3 ARCHITECTURE (decided 2026-07-27): RV=1 native execution

Census of prog68k.bin killed full relocation: 1477 jsr/jmp abs.l opcodes +
thousands of data-table pointers (sprite/anim tables) = unpatchable risk.
But hardware refs are tiny: text 63, palette 43, I/O 33, tile 29, bank 4,
sprite 3 literal sites — patchable.

Key discovery (MAME src/mame/shared/mega32x.cpp:359): **RV bit (0xA15106
bit 0) = 1 maps the cart at 0x000100-0x3FFFFF on the MD side** ("NBA Jam TE
relies on this"). Identity mapping: cart offset X = MD address X. So the
arcade binary placed at its native cart offsets runs UNPATCHED for all ROM
self-references. Keep RV=1 permanently during gameplay.

Cart layout:
- 0x000-0x0FF: our cold-boot vectors (adapter overlays this range at runtime)
- 0x100-0x1FF: 32X header; 0x200: adapter-targeted jmp trampoline table
  (retarget to our shim at 0x040410+)
- 0x3F0-0x7FF: Sega security blob (byte-exact, position-locked at 0x3F0)
  — REPLACES the game's 0x400-0x7FF boot code, which we skip anyway (our
  init replicates it: RAM clear, IO shadow init, sound cmd 0x40)
- 0x800-0x403FF: game body at NATIVE offsets, byte-identical
- 0x40400+: our MD shim (linked low @0x040400; reachable via RV identity
  map during gameplay, via 0x8C0400 window pre-RV at boot)
- 1MB+: SH-2 code + tile data

Consequences:
- While RV=1 the SH-2s must NEVER touch ROM (0x02000000/0x22000000) —
  SH-2 code+data must be SDRAM-resident (crt0 copies; link .text to SDRAM).
  Tile data for the renderer: preload subset to SDRAM at boot (pre-RV),
  stream misses later via MD-side FIFO (phase 4 problem).
- MD interrupts: adapter's fixed overlay vectors → cart 0x200 trampolines →
  our shim. The game's own vector table is NEVER consulted. Vblank shim does
  MCU duties (inputs→0xFFF0C2, sound mailbox 0xFFF0C4→(later), tile bank
  0xFFF095) then jmp 0x2AAC (game handler's rte pops the real frame).
- Small-patch set (tools/patch_game.py, to write): text 0x410000→0xFF8000,
  palette 0x840000→0xFFA000, sprites 0x440000→0xFF9800, I/O 0xC4xxxx→
  0xFFB1xx mailboxes (0xC4 low byte lands on MD VDP — MUST all be patched),
  tile RAM 0x400000→bit-bucket for milestone A (0x300000 cart hole).
- RAM budget: game owns 0xFFC000-0xFFFFFF (native). Shim owns 0xFF0000-
  0xFFBFFF: text shadow 4KB, palette 4KB, sprites 2KB, mailboxes, stack.

ORACLE RESULTS (MAME 0.288 + tools/mame_tap.lua, attract + coined play):
1. RESOLVED: the 0x3Fxx calls target 0x3F00-0x3F5F (game body, real code) —
   earlier concern was a hex misread of the 0x3F0 vector padding. Preserved
   natively; non-issue.
2. Game routines DO read live data from 0x400-0x7FF during play (~25 sites)
   => game bytes 0x400-0x7FF must stay VERBATIM. Therefore the security
   blob moves instead: relocate to cart 0x40400 and patch its ~6 internal
   abs refs (lea 0x4C0/0x4D4/0x4E8/0x6BC...). Adapter never checksums cart
   bytes (MAME mega32x.cpp) — blob effects are register writes only.
3. Header area 0x000-0x2FF (ours): only game-visible reads are vector[0]
   (store game's SP 0xFFFFFF00 there; our boot sets its own SP in code) and
   IRQ4 vector fetches at 0x70/0x72 (adapter overlay serves those at
   runtime -> trampolines -> shim). The 0x000-0x7FF full sweep seen in MAME
   is the MCU protection checksum leaking through the bus model — our shim
   replaces the MCU, so it never happens on 32X.
4. Game's own boot AT 0x400 CAN RUN UNMODIFIED (no entry-point surgery):
   mapper-table writes to 0xFE00xx land in MD RAM mirror (reserve
   0xFF0000-0xFF003F), tile-bank movep to 0x3F0000 lands on ROM (ignored),
   I/O sites are in the patch set anyway. VRAM memtest expectations: handle
   during bring-up (may need one branch patch if POST fails on shadow).
5. Warm-restart path jmp 0x47e (abs.w, at 0x1b5c6) is fine unpatched — the
   boot region stays intact and executable.

FINAL cart layout (F-2):
- 0x000-0x0FF our vectors (vector[0]=0xFFFFFF00 for game compat)
- 0x100-0x1FF 32X header; 0x200-0x2FF adapter trampolines -> shim
- 0x300-0x403FF game bytes VERBATIM (only ~175 HW-site patches applied)
- 0x40400+ relocated security blob + MD shim; 1MB+ SH-2 code + tiles

## Phase 2 status (2026-07-27)

Milestone hit: altbeast tiles render on emulated 32X (ares, 59 VPS).
- Build: `make` → rom/s16.32x (md_src 68K blob .incbin'd into sh_src ROM,
  32x-builder pattern, fixed crt0 + COMM4=0 slave release)
- tools/gen_tiles.py: planar → chunky 8bpp at build time (1MB, ROM-resident)
- m_main.c: 256-color mode, word-paired framebuffer writes (byte writes to
  FB are unreliable on hardware), tile sampler across two ROM banks
- Test loop: `open -a ares rom/s16.32x`, then `screencapture -x` + crop
Next: real text/tile-layer renderer driven by a VRAM shadow (phase 3 prep).

## Phase 3 stage A COMPLETE (2026-07-27): boot chain verified in MAME 32x

Hardware truth discovered on the way (all verified against the real BIOS
dump, disassembled in scratchpad mbios.asm):
- The master SH-2 BIOS WORD-COMPARES cart 0x400-0x7FF against a reference
  copy of the Sega security program inside the BIOS (0x36C-0x76B). Those
  bytes are immovable. Final layout (F-3): blob stock at 0x3F0, stolen
  8-byte jmp at 0x800 (only the blob's own fall-through lands there), game
  native from 0x808, game's displaced 0x400-0x807 runs from a RAM copy at
  0xFFB400 (boot_copy.bin: pc-rel refs converted to abs.w, 4 runtime
  jump-in sites retargeted +0xB000).
- Header word 0x18E must be 0: nonzero engages a BIOS whole-ROM checksum
  handshake over COMM8 that is race-prone with fast boots.
- The marsdev cold-boot handshake is one-shot and racy (BIOS posts M_OK
  once; the blob clears COMM0 later). Replaced with sticky reposts of
  M_OK/S_OK + a LEVEL signal: MD writes 0xACED to COMM8 when ready.
  COMM2 can't hold the level (M_OK long write covers COMM0+COMM2).
- mov.w @(disp,Rn),Rm (non-R0) silently assembles as SH2A-only 32-bit
  encoding — illegal on the 32X SH7604s. Watch for it.
- RV=1 must be set from RAM-resident code; setting it while executing from
  the 0x880000 window kills the instruction fetch (reset loop).
- .data VMA moved to 0xFF0100 (0xFF0000-0xFF00FF reserved for the game
  boot's mapper-mirror writes) — _start's copy loop needed an explicit
  destination (the RAM-clear left a1=0xFF0000).

Verified state (MAME 32x + tools/mame_tap.lua-style beacons): MD executes
shim main from work RAM with RV=1 (COMM14=0xB007 beacon, COMM12 heartbeat),
master SH-2 in m_main, slave in s_main, COMM0 idle.

Next (stage B): shim init copies boot_copy to 0xFFB400, installs vblank
MCU-duty handler, jmp 0xFFB400 to run the game's own boot.

## Phase 3 stage B: IN PROGRESS — MCU shim written, boot handshake blocker

Done and correct (in tree):
- tools/patch_game.py REDIR pass: 8 boot-region CONSTANT reads (cmpi/pea/
  move-source) into displaced 0x400-0x807 now retarget to the RAM copy at
  0xFFB400 (+0xB000). Writes to that range left alone (arcade-ROM-faithful:
  writes there are no-ops on real hardware). This fixed the `pea 0x5be` ->
  rts runaway into blob bytes and the `cmpiw #-1,0x45a` false-flag reads.
  objdump wraps long instrs across lines, so operands are located by
  scanning ROM bytes after the opcode, not the wrapped hex.
- md_main.c: full i8751 MCU replacement (shim_vblank) — MD pads -> arcade
  P1/P2 + SERVICE at 0xFFB0xx, coins mirrored to 0xFFF0C2, tile-bank req
  0xFFF095 -> shadow, sound-mailbox 0xFFF0C4 pump, screen-sync handshake.
  DIP defaults DSW2=0xFD / DSW1=0xFF. main: RV=1, copy boot->0xFFB400,
  seed vectors 0/4, jmp game boot.
- md_start.s: _vblank chains shim_vblank then jmp 0x2AAC when game_running.

BLOCKER (next session): SH-2<->68K cold handshake deadlocks with the
stage-B cart. Symptom: MD spins in _start's M_OK wait (0x8c050e), COMM0=0,
COMM8 never latches 0xACED; master SH-2 races AHEAD into m_main (seen at
0x02040fac by frame 6) having consumed a transient ACED, so it never
re-posts M_OK and the MD waits forever. The handshake CODE is byte-identical
to the committed stage-A build (git diff clean) which boots to main
reliably (verified: /tmp/stageA.32x reaches main by frame 6). So the
regression is layout/timing: growing the MD boot binary shifted the SH-2
code, changing handshake timing enough to expose the race.
Leading fix: make the MD->SH2 release a monotonic LEVEL both sides latch
(MD raises ACED and holds; SH-2 waits for level, never a value it can miss),
and have the MD post its OWN readiness (not just wait), so neither side can
win the race. i.e. redesign the 3-way M_OK/S_OK/ACED handshake to be
edge-race-free. Harness: scratchpad/hs2.lua dumps MD PC + both SH-2 PCs +
COMM0/4/8 per frame.

## Phase 3 stage B UPDATE: GAME EXECUTES (2026-07-27)

Milestone: arcade 68K code boots and runs under our MCU shim. Verified in
MAME 32x: ~7s native execution (pc=0x3982-0x39be main loop), shim heartbeat
(COMM12) incrementing, a sound cmd captured via the MCU mailbox
(COMM14=0x5000), text+palette shadows written (~9300 writes).

Real blockers fixed (none were the handshake):
1. RV=1 interrupt-vector table: under RV=1 the 68K reads vectors from cart
   ROM 0x0-0x3FF. Old fill pointed all at the blob (0x3F0), so the first
   VBLANK vectored into the blob -> cleared ACED -> full reset (looked like
   a handshake deadlock). Fixed in mars_start.s: 0x78 VBLANK -> _vblank
   (jmptab 0x40422), 0x70 HBLANK -> _hblank, exceptions/traps -> game rte
   stub (0xFFB408), v1 reset stays -> blob for the 32X security sequence.
2. main never jumped to the game: inline-asm jmp was optimizer-dropped, main
   fell into bss (trap). Now a function-pointer call (real jmp). Removed the
   dead 0x0/0x4 vector writes (cart ROM read-only under RV=1).

REMAINING: after ~7s the game jumps to ODD addr 0xffffd4ff -> address error
-> rte stub can't recover -> fault loop. Game reads a bad pointer/return,
likely from a shadow that differs from real 16B hw (candidate: sprite RAM
readback 0xFF9800, or a polled hw register used as an index). Next: trace
last native PC before fault + the pointer source. Harness:
scratchpad/fault2.lua (exception frame), scratchpad/stageb.lua (per-frame).

## Phase 3 stage B: COMPLETE (2026-07-27) — game runs indefinitely

The odd-address fault was a PATCHER BUG, not a hardware discrepancy. The
"class B" heuristic (patch any 4-byte HW-range value whose neighbours look
pointer-ish) rewrote offset 0x16BD0, which was NOT a pointer — it was
mid-instruction: the 0x0044 displacement of `move.w D0,(0x44,A6)` at 0x16bce
plus the next opcode. The rewrite turned it into `move.w D0,(0xFF,A6)` ->
EA = 0xffffd400+0xff = 0xffffd4ff (odd) -> 68000 address error. Found via
MAME's GUI debugger (`save /tmp/stack.bin,sp,64` -> decode the group-0
frame) cross-referenced with tools/patch_report.txt.

Fix: patch ONLY class-A refs (value confirmed as an instruction operand by
the disassembly). Class B dropped entirely — genuine data-table pointers
into HW RAM are rare (tables point at ROM graphics), so the trade is safe.
Patch count 288 -> 166, all disassembly-confirmed.

Verified: 45s / 2600+ frames of continuous execution, PC roaming the game's
main dispatch (0x3d0e-0x3f10), heartbeat never stalls, VARYING sound
commands emitted through the MCU mailbox (0x5000/0x5041/0x50b6/0x5046 = the
attract-mode audio script). No faults.

STAGE B DONE. The whole architecture is validated: original arcade 68K code
runs natively on the MD 68K with our memory-map shims + full i8751 MCU
replacement. Next: stage C — SH-2 renderer reads the text/tile/sprite/
palette shadows the game is already writing, so it appears on screen.

## Phase 3 stage C: BLOCKED on SH-2 VDP access (root cause found)

(see git log for the full diagnosis)

## Phase 3 stage C: display still blocked — leads narrowed (2026-07-27)

Tried, none fixed the black screen (ares: black s16/Mega32X/60VPS window,
game running):
- CRAM writes gated on FBCTL VBLK bit — no change.
- CRAM writes synced to the 68K vblank counter COMM12 (backrooms' proven
  pattern, m_main.c:1005 "wait for first vblank — palette is writable now")
  — no change.
- 32X display priority set to 32X-over-MD (MARS_VDP_PRIO_32X) so the game's
  unused-but-enabled MD VDP layer doesn't cover the 32X layer — no change.

Confounder: MAME lua space-reads of the 0x2000xxxx MMIO (COMM/CRAM/INTMSK)
do NOT go through the device handlers — they return raw/wrong values (proven:
lua read COMM0 at 0x20004020 = 0 while the CPU sees 0x80be at the cached
0x00004020 alias). So my "CRAM=0 / INTMSK=0 / framebuffer empty" headless
reads are UNRELIABLE. Only the ares visual (black) is ground truth.

Biggest architectural difference vs the working backrooms build: backrooms
keeps **RV=0** (md_start.s:162 "clear RV - allow SH2 to access ROM"); we hold
**RV=1** for the whole game (its ROM self-refs need the cart at 0x000000).
Backrooms' 68K (md_main) also runs a COOPERATIVE loop that services the SH-2
and manages framebuffer access every frame; our shim runs the arcade game
instead and does no such handoff. Prime suspects for the black screen, in
order: (1) RV=1 changes 32X framebuffer/CRAM access arbitration so SH-2
writes don't land — needs verifying whether RV must be 0 during the SH-2's
draw window (would require toggling RV, or a different game-relocation scheme
that doesn't need RV=1); (2) the SH-2 render needs the 68K to hand off
framebuffer access (FM toggle) each frame like backrooms' md_main does, which
our game-running shim omits; (3) the framebuffer flip/currentFB bookkeeping
in our m_main diverged from backrooms' swapBuffers discipline.

NEXT SESSION: study backrooms md_main.c + m_main.c framebuffer handoff end
to end and replicate the exact protocol in the shim; and settle the RV
question with a clean test (SH-2 draws a solid colour with RV forced 0 vs 1,
screenshotted). Everything else (game running indefinitely, palette streamed
to COMM) is solid and committed.

## Phase 3 stage C MILESTONE (2026-07-27): game palette ON SCREEN in MAME

MAME's rendered frame (snap.lua screen:snapshot) shows the 16x16 CRAM swatch
grid filled with Altered Beast's LIVE attract-mode palette — reds, skin
tones, blues, greens, animating with the game. The full pipeline is proven:
game code -> palette shadow (0xFFA000) -> shim COMM stream -> SH-2
s16_to_mars (sBGR4443 -> BGR555) -> CRAM -> 32X display. Proof frame saved
as docs_palette_proof.png (gitignored art? no - it's a palette grid, kept).

Hardware-correctness fixes landed with it (d32xr-informed, srcref/d32xr):
- d32xr treats RV as a BRIEF PULSE (bset/bclr around ROM-DMA only) and keeps
  all runtime SH-2 code in SDRAM (.sdata/.ramtext) because the SH-2 must
  never touch cart ROM while RV=1. Ares enforces this; MAME does not.
- Our SH-2 runtime (m_main, draw_swatches, s_main, amb_dma_handler) is now
  __attribute__((section(".ramtext"))) = SDRAM-resident via the existing
  mars.ld .ramtext mechanism (BIOS module copy carries .data+.ramtext).
- New boot ordering: master posts 0x600D on COMM14 from SDRAM code; slave's
  SDRAM loop ticks COMM6; the MD shim waits for BOTH before setting RV=1.
  (mars.h: COMM14 defined, COMM12 narrowed to u16.)
- Mars_UploadPalette (d32xr marshw.c:154) confirms the INTMSK ACCESS_VDP
  gate + upload-in-vblank-interrupt pattern for later stages.

ARES STILL BLACK: its 32X model diverges from MAME somewhere deeper in our
boot (game may not even be starting there — the MD now waits on SH-2
signals; if ares kills the SH-2s earlier, main never proceeds). NEXT
SESSION'S TOOL: instrument boot progress via the GENESIS VDP background
colour (visible in ares regardless of 32X state — the stage-A red-border
flash proved this path works). Step colours through main's phases to
binary-search where ares diverges.

MAME lua caveat (repeat): space-reads of 0x2000xxxx MMIO bypass handlers;
use screen snapshots or CPU-side effects for truth.

## Phase 3 stage C step 1 COMPLETE (2026-07-27): live palette in BOTH emulators

ARES ROOT CAUSE (found via Genesis-border phase tracer + blink tracer): the
MD VBLANK (level 6) interrupt never fired in ares. The adapter's fixed
level-6 vector points at a 0x880000-WINDOW trampoline (MAME: v78=0x008802AE)
— and window access is forbidden under RV=1, which ares enforces. MAME
doesn't, hence the divergence.

FIX — arcade-faithful and trampoline-free: use the H-INT instead.
- The adapter's H-int vector (0x000070) is WRITABLE RAM (the security blob
  itself writes it; MAME installs a handler for it in all RV states). main
  points it directly at our RAM-resident _vblank (now .global).
- VDP reg 0 = 0x14 (IE1 on), reg 1 = 0x54 (IE0 OFF — kill the broken VINT
  path), reg 10 = 0xDF (counter 223: ONE interrupt at the last active line =
  vblank cadence).
- H-int is 68K LEVEL 4 == the arcade's IRQ4. The game now receives the same
  interrupt level as real System 16B hardware, through its own handler chain.

VERIFIED IN ARES: border blink tracer ran (handler firing), the init rainbow
was fully overwritten by the LIVE game palette (stream working), and the
palette ANIMATES with attract mode (game running, receiving IRQ4). MAME
unchanged-good. Both emulators now show the same thing: the game's live
palette rendered by the 32X.

Boot-phase tracer left in place (RED/YELLOW/CYAN/GREEN border during init;
GREEN border during gameplay = MD backdrop). Blink tracer retired.

NEXT (stage C step 2): stream TEXT RAM (4KB shadow at 0xFF8000) the same
way — or via DREQ FIFO for bandwidth — and have the SH-2 render the text
layer with the SDRAM-preloaded font tiles + live CRAM. That's "SEGA" /
"INSERT COIN" on screen.

## Phase 3 stage C step 2 (2026-07-27): TEXT LAYER renders (MAME-proven)

The SH-2 now renders the System-16B text layer from the game's live text RAM:
MAME frames show real font glyphs (digits/letters) in the game's colours.
Pipeline: game text RAM -> 0xFF8000 shadow -> shim COMM burst stream (1 pal +
up to 63 text batches/frame, acked) -> SH-2 SDRAM text+pal shadows -> render
40x28 visible window (cols 24..63, scrolldx -192) with SDRAM font tiles ->
framebuffer. Text tile format (segaic16.cpp:1024): code=data&0x1FF,
colour=(data>>9)&7, pal entry = colour*8+pen, pen0 transparent. Proof:
docs_text_proof.png.

THE RV/FRAMEBUFFER TENSION (root, verified): SH-2 framebuffer (DRAM) writes
are BLOCKED while RV=1 — even a solid fill is black in ares (CRAM writes are
NOT blocked, which is why step-1 palette worked). The game needs RV=1 for
68K code fetch. Resolution (d32xr RV-pulse, adapted): the MD vblank handler
runs from WORK RAM, so during it the 68K needs no ROM — it drops RV=0,
signals the SH-2 (COMM0=0x2000) to draw the whole frame, waits for the ack,
restores RV=1 before returning to the game. SH-2 renders + applies CRAM +
flips only inside that window. MAME renders correctly with this.

ARES: still black interior (game runs — green border). MAME proves the
render + protocol are correct, so the remaining gap is the ares RV-window
framebuffer timing: likely the SH-2 doesn't complete render_text before the
68K's spin2 budget restores RV=1, or ares needs the flip/access sequenced
differently across the RV toggle. NEXT: (a) widen/measure the render window
(have the SH-2 ack a start marker so the 68K waits on completion, not a spin
count); (b) verify the diagonal glyph layout is the true screen vs a render
stride artifact; (c) consider 68K-side framebuffer writes (0x840000, FM=0)
as an alternative that sidesteps the SH-2 RV block entirely.
