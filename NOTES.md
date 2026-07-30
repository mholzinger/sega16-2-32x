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

## Phase 3 stage C step 2 COMPLETE (2026-07-27): TEXT LAYER ON SCREEN IN ARES

Fixed the last ares gap: the SH-2 now draws BOTH framebuffers each render
window (render_text / flip / render_text), so whichever buffer ares displays
is always current — ares' flip timing no longer matters. ares now shows the
game's text layer at 60 VPS: dark glyphs on the game's live maroon backdrop,
identical to MAME. Proof: docs_text_ares.png.

The complete, WORKING stage-C pipeline (both emulators):
  game 68K -> text RAM 0xFF8000 + palette 0xFFA000 shadows
  -> MD vblank shim streams both over acked COMM bursts (RV=1; CRAM/mem ok)
  -> SH-2 stores into SDRAM shadows
  -> RV=0 render window (MD shim is RAM-resident, needs no ROM):
       apply palette -> CRAM, render 40x28 text tiles -> BOTH framebuffers
  -> RV=1 restored, MD returns to the game's IRQ handler (0x2AAC)
Cost: ~5ms 68K stall/frame, holds 60 VPS.

Key hardware truths banked (all d32xr-confirmed):
- SH-2 framebuffer (DRAM) writes need RV=0; CRAM writes do not.
- MD level-6 VINT vectors through a 0x880000-window trampoline = illegal
  under RV=1; use the writable H-int vector (0x70) at level 4 = arcade IRQ4.
- All SH-2 runtime code must be SDRAM-resident (.ramtext) under RV=1.
- Draw both buffers when flip timing is emulator-divergent.

NEXT: tile (scrolling background) + sprite layers via the same window; then
the diagonal glyph layout needs confirming as real screen content vs a
render-stride quirk (compare against MAME's altbeast attract in the
reference driver).

## Stage C step 3 (tile background) — design findings (2026-07-27)

Measured against the reference altbeast attract (MAME):
- The visible scene is mostly the TILE background (graveyard/temple) + SPRITES.
  The text layer is sparse ("50000", "INSERT COIN", "©SEGA 1988"); our text
  render is correct — it just has little to show in attract (verified: our
  text RAM is empty most frames). So the big visual win is the tile layer.
- Active tilemap footprint is SMALL: only ~2382 nonzero words, in pages 0,1,
  5,6 of the 64KB tile RAM (0x400000). Scroll registers (textram 0xe9e/0xff8/
  0xf24) were 0 during attract (static). MAME write-taps don't fire on the
  mapper's install_ram tile region — read back the RAM to observe it.
- MD RAM budget is roomier than assumed: the shim's .data/.bss ends at
  0xFF052C (~1KB), so 0xFF0600-0xFF7FFF (~30KB) is free — enough to shadow
  the used tile pages.

Attempted: patcher remap of tile RAM 0x400000-0x406FFF -> 0xFF0600 shadow
(16 class-A lea refs caught). Result unclear — the tile shadow read back
empty and the game appeared stuck, BUT the PC-sampling diagnostic was biased
(register_frame_done fires in vblank = always catches the shim, never the
game's mid-frame loop; it reported game=0 even for the KNOWN-GOOD text build).
Reverted to keep HEAD clean. NEXT SESSION must re-attempt with UNBIASED
instrumentation (screen snapshot / CPU-side markers, not per-frame PC), and:
  1. remap tile RAM to the 0xFF0600 shadow (verify game still boots via ares
     visual, not PC sampling);
  2. stream the tile shadow to SH-2 SDRAM (COMM burst like text; ~45 frames
     for a full 28KB refresh — fine for a static attract background, or add
     DREQ FIFO for bandwidth);
  3. render the System-16B background: 64x32 tile pages, page-select from
     textram[0xe9e/2], row/col scroll from textram[0xf80.../0xf30...], tile
     format = same 3bpp chunky tiles (code&0x1fff via bank, colour bits).
     Reference: segaic16.cpp tilemap_16b_draw_layer (~line 820).

## Stage C step 3: WHY the tilemap needs SDRAM (definitive, 2026-07-27)

Re-ran the tile capture with UNBIASED instrumentation (palette-shadow
nonzero count = proof of game liveness, not per-frame PC). Result: capturing
tile RAM to a partial MD shadow (0xFF0600, 28KB) DID break the game
(pal_nz stuck at 1 vs 45 in the good build).

ROOT CAUSE (exact): the game's tilemap fill loop at 0x16BE writes ~40KB of
tile RAM in one pass:
    16be: lea 0x400000,a0 ; 16c4: move #20479,d1
    16ce: move.b d2,(a0) ; 16d0: addq #2,a0 ; dbf d1  -> a0 spans
    0x400000..0x409FFE (pages 0-9, 40KB).
With the base patched to 0xFF0600, this clears 40KB from 0xFF0600 up to
~0xFFA5FE, clobbering the sprite/text/palette shadows AND the game's own
work RAM -> corruption -> stuck.

BUDGET PROOF the tilemap can't fit MD RAM (64KB total):
  game work RAM (mapper region 3, FIXED) 0xFFC000-0xFFFFFF = 16KB
  shadows (text+sprite+palette+IO)                          ~14KB
  tilemap written span (pages 0-9)                           40KB
  40 + 16 + 14 = 70KB > 64KB. Even pages 0-6 (28KB) collides.
=> The System-16 tilemap MUST live in 32X SDRAM (256KB); the transfer is
   mandatory, not optional.

TRANSFER DESIGN for next session (two viable paths):
1. DREQ FIFO (68K->SH-2 DMA): the canonical bulk path. Needs the tile data
   in a 68K-addressable staging buffer to DREQ from — but the game writes
   40KB in one loop we can't chunk. So pair with a FULL 64KB tile-RAM
   region... which doesn't fit MD RAM. Dead end unless we intercept the
   fill loop.
2. Framebuffer-staging: remap tile RAM -> 32X framebuffer off-screen area
   (0x840000-based, 68K-writable, SH-2-readable at 0x24000000; the bank is
   128KB, display uses ~72KB, ~56KB off-screen holds the 40KB tilemap). The
   catch is the double-buffer flip (68K's 0x840000 = the same back bank the
   SH-2 renders into). Resolve by running a SINGLE display buffer (no flip)
   so the off-screen tilemap and on-screen render coexist in one bank, or by
   writing the tilemap to a fixed offset in BOTH banks.
3. PRAGMATIC: patch the fill loop's count (0x16C4 move #20479) + base so the
   game writes a SMALLER tilemap we can shadow — only if altbeast truly uses
   just pages 0,1,5,6 and the loop is the sole writer. Fragile; verify first.

Reverted to the clean text-layer build (HEAD text milestone). The tilemap
transfer is the next focused subsystem; text + palette layers remain fully
working on both emulators.

## Stage C step 3: the tile-transfer implementation plan (d32xr-referenced)

The tilemap MUST reach SH-2 SDRAM. The clean, canonical path is the DREQ
FIFO — and d32xr has a complete, working implementation to port:
  src-md/crt0.s ~3180-3300: the 68K DREQ send. Sequence:
    - dest -> 0xA1510C (SH DREQ dest = SDRAM addr)
    - length(words, rounded to units of 4) -> 0xA15110
    - set 68S bit: 0xA15107 = 0x04  (starts the SH-2 DREQ DMA)
    - stream words to the FIFO at 0xA15112, polling FIFO-full at
      0xA15107 bit 7; the SH-2's DMA channel drains FIFO -> SDRAM
    - clear 68S (0xA15107=0) when done
  SH-2 side: DMA channel 0, source = DREQ FIFO, dest = SDRAM, DREQ-triggered.

SOURCE for the DREQ = the framebuffer: remap tile RAM 0x400000-0x40FFFF ->
the 32X framebuffer (0x840000, 128KB, 68K-writable & DREQ-sourceable). The
game fills the tilemap there; the shim DREQs it (40KB) to a persistent SDRAM
tilemap; the SH-2 renders the display from SDRAM (never from the live
framebuffer, avoiding the double-buffer/flip conflict). Only re-DREQ when the
tilemap changes (dirty flag on the game's tile-bank write, or the fill-loop
completion) — the game writes the tilemap on scene load, not per frame.

Then extend the existing RV=0 render window (SH-2 already draws text there)
to also composite the background tilemap: System-16B BG/FG pages from
textram[0xe9e/2], row/col scroll from textram[0xf80.../0xf30...], same 3bpp
chunky tiles, priority vs text. Reference: segaic16.cpp
tilemap_16b_draw_layer (~line 820).

This is a self-contained subsystem (68K DREQ send ~40 lines + SH-2 DMA-from-
FIFO handler + the tilemap compositor) — the right size for one focused
session. The text + palette layers are fully working on both emulators; the
tile background slots into the same proven render window.

## Stage C step 3 IMPLEMENTED (2026-07-27): BACKGROUND LAYERS ON SCREEN

MAME shows the full title scene — temple, lion monument, sky gradient, red
ALTERED BEAST logo, live palette — arcade-accurate minus sprites. The DREQ
FIFO plan was DROPPED for something simpler: tile RAM is remapped to a
framebuffer STAGING area the SH-2 reads directly (both CPUs address the FB;
DREQ would have been pointless indirection — d32xr needs it only because
its sources are MD-side ROM/RAM, not the FB).

Architecture (the working pipeline):
- patcher: tile RAM 0x400000-0x40FFFF -> FB staging 0x852000 (byte offset
  0x12000 in the access bank, past the 0x11A00 display image; game-touched
  span is pages 0-11 = 48KB, fits under the 0x20000 bank limit).
- FM ownership: FM=0 during gameplay (68K owns the FB window so the game's
  staged tile writes land); the vblank shim raises FM=1 only for the RV=0
  render window, restores both before returning to the game.
- SH-2 render window: copy 2 staging pages -> SDRAM tilemap shadow
  (0x06020000, 16 pages; full refresh every 6 windows), purge cache, apply
  CRAM, compose ONCE into a padded 336x240 SDRAM screen buffer (BG opaque,
  FG transparent, TEXT on top), then blit it to BOTH framebuffers with an
  EVEN number of FBCTL flips (keeps the staging bank stable). Tiles are
  read straight from cart ROM (legal at RV=0) — no SDRAM tile cache.
- Compose is SPLIT across both SH-2s: slave renders tile rows 0-13 on a
  COMM4 0xC000|bank command, posts 0xD0 on COMM6; master renders the rest,
  waits, blits. (COMM6 doubles as the MD stream lane — master clears it
  before each command; slave stops its boot heartbeat at the 0xB007 beacon,
  fixing a latent slave-tick-vs-stream race.)
- Dynamic CRAM allocation: a scene uses tile colors 0-127 -> palette
  entries to 1023, but 256-color mode has 32 groups of 8. Per-CPU lazy
  alloc maps (master groups 20-31, slave 8-19, 0-7 identity shared with
  text) rebuilt every frame; CRAM applied at window start from last
  frame's maps (steady-state exact). Palette stream widened to 1024
  entries (32 pal + 32 text batches/vblank, alternating).
- Render window (~25ms) exceeds a frame, so the shim opens one only every
  SECOND H-int — back-to-back windows leave a pending H-int at every rte
  and STARVE the game to a standstill (diagnosed via work-RAM entry/window
  counters at 0xFFB0F0/2 — MMIO reads from lua lie, work RAM doesn't).

HARD-WON FACTS:
1. ALL byte writes to the 32X framebuffer drop zero bytes (MAME
   mega32x.cpp m68k_dram_w, "tested on real hw" — both windows). The
   game's tilemap RLE loaders are byte-writers (0x16BE even bytes, 0x16DE
   odd). Fix IN THE GAME CODE: patch the even pass to write words
   (value<<8|0x00) — same byte count (lslw #8,d2 / movew d2,(a0)+), plus
   a dbf retarget — which pre-zeroes every odd byte, so the odd pass's
   zero writes become no-ops on already-zero bytes. Verified: the pair
   0x16AE/0x16B2 is the only call site (the 0x1d2d0 "16be" hits are an
   ascending data table, not pointers).
2. PATCHER BUG (major find): objdump prints IMMEDIATE operands in DECIMAL
   (moveal #4261713,%a1 = #0x410751), so the class-A "operand confirmed in
   disassembly" test missed every movea.l/move.l #hw-address pointer load
   — 27 real sites, including the boot stashing POINTERS TO THE PAGE
   REGISTERS in work RAM (movel #0x410E80,0xFFF0EC) and palette/sprite
   base loads. Symptom: page-select regs never reached the shadow ->
   background rendered as sparse dots. Fix: also accept the exact decimal
   "#<value>" form. 166 -> 193 sites.
3. System-16B PAGE/SCROLL REGS (NOTES had the 16A layout — wrong): the
   LATCHED set is pages 0xE80+2*which, yscroll 0xE90+, xscroll 0xE98+
   (which: 0=FG 1=BG). Raw pages word quadrants (1024x512 virtual, 16
   pages): UL=(p>>4)&0xF UR=p&0xF LL=(p>>12)&0xF LR=(p>>8)&0xF. Screen:
   vx=(sx-(0xC0-xsc))&0x3FF, vy=(sy-ysc)&0x1FF. Tile: code=data&0x1FFF,
   color=(data>>6)&0x7F, bank slot=code>>12, banksize 0x1000, bank[0]
   always 0 (MCU), bank[1] = the 0xFFF095 request &7. Draw order: BG
   opaque -> FG -> TEXT, all pen0-transparent except BG.
4. Attract palette truth (reference dump): ~1200 nonzero entries, tile
   colors 11-101 (entries to 815) — an 8bpp pixel==entry scheme can never
   work; dynamic allocation is mandatory. Demo scenes scroll (xscroll
   live), ysc=0x20 constant, no row/col scroll flags in attract.
5. MAME screen:snapshot works under -video bgfx/default but silently
   kills the machine under -video none.

STATE: MAME verified end-to-end (title scene arcade-accurate, scene
changes tracked, page regs streaming, tilemap shadow == reference 19520
nonzero words). Game speed ~1/3 real time (window every 2nd H-int + ~25ms
windows). White/black interstitial screens are believed correct (their
art is SPRITES — next phase; splash background really is palette entry 0
= white). ares: verification pending this session.

NEXT: sprites (the attract is mostly sprite art), then window cost: the
compose split can go finer (thirds via wider COMM protocol), the blit can
skip unchanged rows, and scroll-only frames could scroll-blit. Sound
untouched. Diagnostic counters 0xFFB0F0/0xFFB0F2 left in the shim.

## Stage C step 4 (2026-07-27): SPRITES RENDER — full scene composition

MAME attract now shows the complete System-16B scene: hero, wolves,
zombies over the tile background with text on top — shapes and most
colors arcade-correct. Verified interactive in ares by the user: COIN +
START work, HUD text and the lives sprites display, game plays (slowly).

Sprite pipeline (mirrors sega16sp.cpp sega_sys16b_sprite_device::draw):
- Sprite RAM 0x440000 -> FB staging 0x85E000 (SH-2 0x2401E000). Only 4
  patch sites; the game's uploads are all movew/movel (verified 0x2B1E
  loop) so the FB zero-byte-drop hazard doesn't apply. The SH-2 reads the
  list IN PLACE during compose — compose precedes any flip, so no SDRAM
  copy is needed at all.
- tools/gen_sprites.py: 8 ROMs -> sprites.bin, 1MB 16-bit-BE stream
  matching MAME's region (even byte = b5-b8, odd = b1-b4, four 256KB pair
  blocks); .rodata in cart, read at RV=0. Cart now 2.4MB.
- Entry format implemented faithfully: d0 bottom<<8|top, d1 xpos 0x1FF
  (screen x = raw-184), d2 end/hide/hflip/signed pitch, d3 word addr, d4
  bank(identity,%8)/color(0-63), d5 vzoom/hzoom (5 bits each, accumulator
  skip — attract USES zoom, 8 of 18 sprites in the reference dump).
  4 nibbles/word MSB-first (LSB flipped), pen 0/15 clear, last nibble 15
  ends the row; addr += pitch BEFORE each row. Palette entry =
  1024 + color*16 + pen. Draw order BG, FG, SPRITES, TEXT.

Allocation rework (unified prescan replaces per-CPU lazy alloc): the
master walks both visible tilemap windows + the sprite list at window
start, then assigns 8-pen groups to tile colors (ascending from 8) and
ALIGNED 16-pen group-pairs to sprite colors (descending from pair 15),
clamping where they meet. CRAM applied same-window (no color lag); slave
reads the maps post-purge (complete before the COMM4 go). Reference dump:
<=7 sprite colors + scene tile colors fit 32 groups in practice.

CPU split fixed at SCREEN row 112 for ALL layers (a tile-row-index split
drifts with yscroll and lets one CPU's tiles overwrite the other's
sprites near the seam). Palette stream widened to all 2048 entries
(sprite half converges in ~13 frames — transient wrong sprite colors on
scene changes until dirty-tracking exists).

PIXEL-VALUE-0 TRANSPARENCY (ares vs MAME divergence, likely real hw): in
256-color mode with 32X priority, pixel value 0 shows the MD layer
through (ares implements; MAME shows CRAM[0]). The opaque BG therefore
NEVER emits value 0: BG tile color 0 maps to an alias group (bg0_grp,
CRAM copy of entries 0-7). Interstitial screens: white in MAME.

KNOWN GAPS: (1) game speed ~1/3 — the ~25-30ms window every 2nd H-int
starves the game; next: finer SH-2 split / dirty-row blits / scroll-aware
compose. (2) sprite-vs-tile priority bits ignored (sprites always over
BG/FG, under TEXT). (3) shadow/hilite pen unimplemented. (4) sprite
palette lag on scene cuts.

## Stage C step 4b (2026-07-27): ares BANK-TEARING root-caused and FIXED

Symptom: in ares, gameplay scenes showed only text/sprite HUD over the
green MD backdrop — but boot-loaded scenes (attract title) rendered fine.

ROOT CAUSE (proven with an MD-side FS tracer, sticky magenta border +
work-RAM mirror at 0xFFB0F4): ares — like real hardware outside vblank —
LATCHES FBCTL writes at the next vblank. Our two blind mid-window toggles
both computed from a STALE FS readback, collapsing into a net ONE flip
per window: the access bank alternated every window, and the game's
FB-staged tile/sprite writes (which span many frames during an RLE scene
load) tore across the two banks. Boot-loaded scenes survived because
they were staged before the first render window. MAME latches flips
immediately (control run: torn=0), which is why it never showed this.

FIX (verified-flip discipline, no cost on immediate-latch emulators):
- SH-2: blit bank A; write FS toggle; SPIN until FS reads back flipped;
  blit bank B; restore FS with an ABSOLUTE write (never a toggle — a
  toggle recomputed from stale readback is how the parity broke).
- MD shim: after the render ack + FM=0, HOLD the game until FS reads
  back at its steady value (0xFFB0F6, sampled at handler entry) — the
  game must never run while its staging bank is deselected.
ares now renders full gameplay scenes at 60 VPS (round-1 intro: Zeus
sprite, lightning, temple background, story text — user-played).
Tracer stays in the shim: MAGENTA border = parity broken again.

LESSON for the bank: NEVER derive an FBCTL write from a fresh FBCTL
readback mid-frame — under deferred latching the readback is stale.
Always track the intended FS value and write it absolutely.

## Stage C step 5 (2026-07-27): render window PROFILED and OPTIMIZED

FRT-based phase profiler added (sysclk/32 ticks, accumulators in SDRAM at
0x26033000, lua-readable; slots include per-layer compose). Baseline
window: title 27ms, demo scenes ~48ms; the 68K handler ran at ~40Hz and
the game got ~12% of its cycles.

Optimizations landed (measure -> fix -> re-measure):
1. PACKED 32-bit BG compose: per tile row, decode all 42 cells once
   (pointer + color base replicated x4 lanes — pens <=7 never carry on
   the byte-lane add), then per line pre-add the base per tile and
   SHIFT-MERGE adjacent tiles' rows so every store is an aligned uint32
   despite fine-x scroll. Shift-case branch hoisted out of the pixel
   loop. BG half: 10ms -> ~5ms (title), demo ~12ms (cache misses from
   scrolling tile churn dominate there).
2. PRESCAN MOVED TO THE SLAVE with double-buffered color maps (window
   parity in the COMM4 command): the slave builds NEXT window's maps
   after its compose half, overlapping the master's compose+blits; the
   master's slave-wait moved after its blits. Master path -4.1ms; colors
   lag the scene by one window (invisible in steady state).
3. Sprite fast paths: unzoomed sprites (flipped or not — hzoom is what
   forces the accumulator) get tight 4-nibble loops; ALL modes bail at
   x>=504 (x is monotonic in every mode; nothing visible past column
   503, and MAME only scans on for RAM-writeback side effects we don't
   emulate). Demo sprite half: 17ms -> 9ms.
4. Bank-fs0 blit only every 8th window (it is displayed only for the few
   ms between the two verified flip latches): -2ms on 7 of 8 windows.

RESULT: title window 27 -> 11ms, demo ~48 -> ~36ms; handler 40 -> 56Hz;
windows 27.5/s of the 30 max; the game reaches full attract palette in
2s of emulated time vs 10-18s. Perceived speed several times better.

REMAINING window cost (demo marginal): BG ~12ms + FG ~10ms (dense-scene
tile compose, cache-miss bound) + sprites ~9ms. Next levers: FG packed
path with per-tile opacity classes, dynamic row split (master is the
critical path only in sprite-heavy halves), every-H-int cadence for
title-class windows (<16.7ms), and dirty-scene burst copies. Profiler
left wired — DIAG[10..13] = per-layer master compose.

## THE SPEED ENDGAME (architecture, discussed 2026-07-27)

This is a PORT, not emulation: the arcade 68K binary runs natively on
the MD 68K; the SH-2s run our own reimplementation of the S16B video
chips. The hard wall is BUS EXCLUSIVITY, not CPU horsepower: while the
game owns the cart (RV=1), the SH-2s cannot touch cart ROM, where the
2MB of tile/sprite pixel data lives — so game and renderer take turns,
and heavy scenes stall the game ~36ms per cycle (= the ~1/3 speed).

Step-6 plan: SDRAM TILE CACHE so tile compose runs CONCURRENTLY with
the game (SDRAM + FB staging are legal SH-2 reads at RV=1). Scenes use
1-3K distinct tiles of the 16K; the slave's prescan already walks every
visible tilemap word, so it can drive cache fills; scene loads (tile
bank writes / RLE bursts) are the natural fill moment via short RV
pulses. Sprites stay in-window (~9ms) — frames too big/dynamic to
cache. Target stall ~15ms -> 70-90% arcade speed. Also: fixed window
cadence to kill the 2-vs-3-H-int stutter even before it's faster.

## Stage C step 6 LANDED (2026-07-27): concurrent tile compose via SDRAM cache

The step-6 plan above is implemented and committed. Render window
5.9-13.6ms (was 27-48); handler 58.5Hz; game cycle share ~4x. Details in
the commit and the m_main.c header comment. Key pieces: 64KB 2-way tile
cache (XOR-folded sets, miss queues, budgeted in-window ROM fills, flat
placeholder tiles until filled), BG/FG compose at RV=1 on both CPUs,
stream servicing on the slave, SDRAM SYNC mailboxes for master<->slave
(COMM regs belong to the MD stream), and a hardware-faithful once-per-
window scroll/page register latch.

KNOWN COSMETIC ISSUE (predates the cache — arrived with the sprite
build): small fixed wrong-color blocks at the top/bottom screen edges in
some scenes. Signature is unmapped-color groups (purple/green flats).
Investigated: NOT the reg-latch race (fixed and artifact persists), not
cache thrash (predates cache). Next suspects: sprite-list mutation
between the slave's prescan (window N) and sprite compose (window N+1) —
1-frame-stale spr_pair mappings for newly-spawned sprites — or edge
tiles whose colors the prescan sees but whose grouping overflows.
Repro: MAME attract temple scene, blocks at screen top/bottom edges.

## Rise-from-grave FREEZE fixed (2026-07-27) — two systemic lessons

Symptom: coin+start, "rise from your grave", smoke appears, game freezes
(first found by the user in ares; reproduced in MAME with scripted
inputs — the attract demo never exercises this path, so it was latent).

Diagnosis chain worth remembering: game PC captured from the exception
frame at each H-int (_vblank stores 2(sp) to 0xFFB0F8 — unbiased,
handler-bias-proof) -> object-slot differential vs the reference arcade
run (slot 59's intro sequencer at handler 0x5618 never advanced; the
0xFFF14C intro countdown frozen) -> MAME debugger trace showed the 68K
eating whole frames in the shim's stream spins -> slave SH-2 PC sampled
INSIDE ___ashrsi3 in cart ROM.

LESSON 1 — NO VARIABLE SHIFTS IN SH-2 SDRAM CODE: SH-2 (SH7604) has no
variable-shift instruction; GCC emits libgcc helper CALLS, and libgcc
lives in .text = cart ROM. Any RV=1-path code with a runtime shift
faults its CPU on ares (and crawls everywhere). GCC even strength-
reduces ternaries like (x & 0x100) ? 2 : 0 into signed-shift helper
calls. Constant shifts only (switch on the shift case if needed); cast
to unsigned before right-shifting; VERIFY per build that .ramtext
literal pools contain no 0x0204xxxx call targets (objdump -s).

LESSON 2 — BOUND HANDLER TIME GLOBALLY, NOT PER-WAIT: the stream's
per-batch 400-iteration spins each individually "worked", but with the
SH-2s acking at ~1ms cadence mid-compose, 64 batches trickled through
at ~1ms apiece = 60+ms handler entries = game starved to a standstill
that then SUSTAINED ITSELF (frozen game -> same scene -> same load).
The ack-wait budget is now TOTAL across the stream section (~2ms).

## State after the freeze fix (user-played, 2026-07-27 evening)

Full gameplay works in ares: rise completes, way more sprites, no
freeze. Remaining complaints: "dog slow" + "flickerfest and artifacts".

Flicker: the every-8th-window fs0 blit was the big one (under deferred
FBCTL latching the fs0 bank is DISPLAYED during each window's flip-latch
wait; skipping its blit showed up-to-8-window-old frames). Reverted —
fs0 blits every window again.

Remaining artifact/slowness sources, ranked for next session:
1. THE COMM STREAM IS NOW THE WRONG ARCHITECTURE. With the SH-2s
   compose-busy, batches ack at ~1ms; the bounded budget (2ms/entry)
   protects the game but throttles palette/text refresh — sprite/tile
   colors lag seconds behind scene changes (= many of the "artifacts").
   PLAN: kill the stream. Stage the PALETTE in the framebuffer like
   tiles/sprites (game palette writes verified word-size — movew sites;
   4KB at FB offset 0x1F000 free) and read it in-window. TEXT can't be
   naively FB-staged (the game writes 0x00 BYTES at 0x410002 — the
   zero-byte drop would break the MCU screen-sync handshake); options:
   keep a small COMM text stream, or MD-side copy text shadow into FB
   staging with word writes during the vblank handler (2KB = ~1ms 68K).
2. Cache-miss placeholder tiles during scroll/scene loads (flat-color
   blocks). Tunables: fill budget, prefetch a column ahead using the
   scroll delta, bigger cache (3-way needs an SDRAM re-pack).
3. Speed: gameplay windows are sprite-bound (in-window ~7-10ms) plus
   concurrent compose stretching the cadence when scenes are dense.
   Sprite compose could ALSO leave the window via an SDRAM sprite-frame
   cache (same trick as tiles; frames are larger — needs eviction), or
   at least overlap the two CPUs better within the window.
4. Sprite priority bits + shadow pen still unimplemented (visual
   correctness, not speed).

## Speed RESOLVED (2026-07-27 night): two-CPU window, game at full pace

Follow-ups to the freeze fix, in order: palette moved to FB staging
(0x85F000; all 44 game palette writes are word/long — stream now carries
text only, color lag gone); then the render window went fully two-CPU:
the slave composes AND blits rows 0-111 of both banks (FM grants the
SH-2 SIDE — either CPU may write the FB), flip edges handshaken via
SYNC[2]/[3], CMD_WIN issued before the master's housekeeping so
everything overlaps, and the slave's prescan overlaps the master's tail.
Sprite 1:1 paths gained no-clip variants (xpos>=184 => x<504 implies
sx<320).

MEASURED (MAME, scripted gameplay): window ~10-11ms, blit phases 1.3 +
1.0ms, slavewait/flipwait 0.00, handler 60/60 H-ints — and the game's
interrupted-PC sample sits in ITS OWN idle vblank-wait loop (0x3982):
frame logic completes with cycles to spare, arcade-style pacing.
Display is 30fps (window every 2nd H-int buys the game its cycles).

## Evening sweep (2026-07-27): the last structural render bugs

Fixed in sequence, each MAME+ares verified:
1. Palette -> FB staging (stream now text-only; color lag gone).
2. Two-CPU window (slave composes AND blits its half; SYNC[2]/[3] flip
   handshake; full overlap). Game logic reached full 60Hz pacing — its
   interrupted-PC idles in the vblank-wait loop.
3. Black sprites in ares: the slave's prescan ran post-flip and read the
   sprite list from the WRONG BANK (deferred latch). Sprite list now
   snapshots to SDRAM at window start; all sprite readers use it.
   RULE: nothing reads FB staging after the first flip edge.
4. 4-way tile cache (256 sets, same 64KB): round-1 sky-gradient codes
   aliased 3+ deep in 2-way and thrashed forever (black band part 1).
5. VERTICAL SCROLL SIGN: vy = sy + ysc. MAME's convention is
   asymmetric — the driver negates X itself (0xC0 - xsc) but passes Y
   raw (positive scrolly moves the source window DOWN). The minus form
   had the screen top wrapping to the virtual map's bottom since the
   FIRST background milestone (phantom rock band + black gap; the
   title layout coincidentally looked plausible). Found via
   byte-identical shadow diffs + the user's annotated screenshot.

Gameplay layout now frame-for-frame arcade-identical. Remaining visual
gaps: sprite-vs-tile priority bits (sprites always on top — zombies
should walk BEHIND gravestones), shadow pen, off-screen sprite color
prescan at extreme edges. Then sound.

## FPGA-reference accuracy pass (2026-07-27, user-supplied MiSTer shots)

fpgascreens/ holds MiSTer FPGA arcade captures — the pixel ground truth
for color/layout diffs (user-provided; add more scenes as needed).

Fixed against them:
1. CRAM cross-contamination: overflowing tile colors marched into
   groups 24-31 = sprite pair CRAM (gravestones in zombie skin tones,
   sprites in stone colors). build_maps now reserves the scene's sprite
   pairs FIRST (pairs 16-nspr..15, floor pair 6), caps tile groups
   below, clamps tile overflow to the last legal tile group.
2. Sprite-vs-tile priority: census shows all altbeast gameplay sprites
   at pp=2, no shadow pens, 780 cat-1 tiles. Exact sega16b rule
   ((1<<pp) > topmost tile mark) reduces to: FG category-1 tiles
   recompose OVER sprites, in-window, from the SDRAM tile cache, split
   across CPUs. compose_layer has a catsel param (0 all / 1 cat0 /
   2 cat1); the concurrent FG pass draws cat0 only. If later rounds use
   pp<2 sprites or BG-cat1-over-sprite, extend with more passes (the
   census tool is tools/ + scratchpad pph_ref.lua pattern).

MAME gameplay now color-matches the FPGA captures. Agreed direction:
hardware accuracy + sprite work before more speed tuning.

## Late-evening sweep 2 (2026-07-27): squares eradicated, speed restored

User-driven fixes, in order (screenshots/ holds a 1171-frame capture of
the resulting build — combat, gore, colors all arcade-correct):
1. Full staging->shadow copy every window (was a 2-page rotor, ~200ms
   stale during scroll = roaming garbled squares).
2. Cache misses draw NOTHING (keep last frame's pixels) instead of
   placeholder blocks — animated tiles show their previous frame.
3. cache_fill races: data-then-tag write order + fills moved after the
   slave's in-window cache reads (torn plausible-but-wrong tiles).
4. PATCHER DATA CORRUPTION found: the level event/spawn script at
   0x1D2DC-0x1D520 (12-byte records [camX][p1][p2][0x0040][handler.l])
   misdisassembles so param+handler-high pairs look like 0x00400000
   operands; one got remapped -> corrupted round-1 spawn (red blob).
   Table excluded. LESSON: operand-confirmed != code — check the
   surrounding disassembly for data-table patterns before trusting an
   A-site in unexplored regions.
5. THE FLASHING-IN-PLACE SQUARES (user: "exact same patterns while
   standing still"): animated tiles cycle codes 0x100/0x400 apart; ANY
   byte-fold collides the family into one cache set -> 5+ hot codes
   over 4 ways churned every window (miss queue showed one code 12x per
   frame). 8-way x 128 sets + (code ^ code>>7) fold: misses 23 -> 3.
6. Speed regression (window 19ms): the prescan had landed on the
   slave's in-window critical path, then didn't fit its concurrent
   phase either. Now on the MASTER's concurrent tail (lighter half):
   slvw 4.4->0, tcmd 6->1, window ~11ms in gameplay with all accuracy
   features intact.

RESIDUAL (only visible defect in the 1171-frame capture): small purple
flecks at the far LEFT screen edge, ~30% of frames. Signature matches
the g==0xFF -> group 1 fallback: freshly scrolled-in edge columns whose
colors the one-window-old prescan hasn't mapped yet. Candidate fix:
prescan one tile column beyond both edges (c = -1..42), or fall back to
the nearest mapped color instead of group 1.

## SOUND PLAN: mine the official MD port (megadriveref/)

The user provided the retail MD port ROM: megadriveref/"Altered Beast
(USA, Europe).md" (512KB, header GM 00054002-02, verified). Value:
1. It contains a COMPLETE working YM2612+Z80 sound implementation of
   the same music/SFX. Our MCU shim already captures every arcade sound
   command byte (0xFFF0C4 mailbox, logged on COMM14 as 0x50xx). If we
   map arcade command bytes -> MD-port command bytes, we embed their
   Z80 driver + FM patches and skip YM2151 emulation entirely.
   Z80 control code (busreq 0xA11100 / reset 0xA11200 / Z80 RAM upload
   0xA00000 refs) clusters at ROM 0x48A0-0x4A1E — start there to find
   the driver blob and the 68K-side sound-command entry point.
2. Their asset/palette compromises are the answer key if background
   layers ever move to the idle MD VDP (4-palette limit is the catch).
CAUTION: the 32X shim owns the Z80 bus request lines today (PAUSE_Z80
in read_joypad); embedding their driver means real Z80 code running —
coordinate bus ownership carefully.

## THE THREE-PHASE WINDOW CADENCE (fixes both "STILL so slow" and "just flashing")

Why the port stayed slow no matter what the windows measured: ALL
timing was measured in MAME, which latches FBCTL instantly. ares (and
hardware) defer FBCTL writes made OUTSIDE vblank to the next vblank.
Every mid-frame flip edge could stall up to a full frame — invisible
in MAME, dominant in ares. Fix: only flip during vblank, where the
latch is immediate on every implementation.

Architecture (commit after 5d0021e): the MD vblank handler opens one
short window per H-int, cycling THREE phases:
- COMPOSE window (0x2100, ~7ms, mid-frame, NO flips): tile_cmd wait,
  staging copies, CRAM, sprite compose + FG cat-1 + text on top of the
  concurrently-composed tiles in sbuf. Longest 68K stall.
- BLIT window A (0x2000, ~1ms, inside vblank): flip to display bank Y,
  both CPUs blit a QUARTER (56 rows each) shipping sbuf rows 0-112,
  flip back to staging bank X.
- BLIT window B (0x2010, ~1ms, inside vblank): same for rows 112-224;
  then (frame fully shipped) master launches the concurrent tile
  compose (CMD_TILE) for the next frame + build_maps prescan on its
  tail — AFTER the blit so new tiles never erase un-shipped sprites.

Frame ships every 3 H-ints (20Hz display update); windows steal
~9ms/50ms = 18% of 68K time (vs 29% before) so the GAME runs faster.

POSTMORTEM — the "just flashing" build (5d0021e, two-phase): the
full-frame blit window measured ~2.5ms, LONGER than the 2.4ms NTSC
vblank (38 lines x 63.5us). The flip-back missed blanking; ares
deferred it a full frame; the display showed the raw staging bank
(garbage) AND the resuming game wrote staging into the wrong bank
until the latch. MAME's instant latch showed a perfect picture.
LESSON: any FBCTL flip pair must fit inside 2.4ms WITH handshake
margin — budget ~1ms, never ~2.5ms. Measure against the vblank
budget, not against "does MAME look right".

Field verification (user's 2016-frame capture of the PRE-two-window
build): zero left-edge purple frames (prescan c=-1..42 fix confirmed,
was ~30%), zero black frames. Remaining visible artifact class:
scroll-burst stale-tile patches (cache misses draw nothing; a burst of
freshly scrolled-in codes keeps old pixels for a few windows until
cache_fill catches up, e.g. flat foliage-green rectangles over the
left wall in frame 1800). Candidate fixes: raise cache_fill budget,
or prioritize on-screen misses over prefetch.

## ARES ROUND 2: the gate alone wasn't enough — 56-row blit slices

Field report on the gated 3-phase build (bab5f74): "still flashing but
not as a strobe light. now its flashing between frame renders.
gameplay slow." User frame dump: the flash frame is SOLID BLACK with a
green bottom strip (5716.png) between two correct gameplay frames.

Diagnosis: black = bank X displayed for a full frame. Bank X's frame
area (0x200-0x117FC) is never written (staging lives at 0x12000+), all
zeros -> every pixel hits CRAM[0] = 0x0000, through-bit clear = opaque
black. So the blit window's flip-back was missing vblank END: ares'
FB writes are several times slower than MAME's (112-row half = 0.53ms
MAME), the blit ran past the ~2.3ms of remaining vblank, the restore
deferred a frame, and bank X displayed for one full frame per cycle.
The "slow gameplay" was the same event: the MD FS-home gate holds the
68K until the deferred restore latches.

Fixes this round:
1. EARLY-VBLANK GATE (bab5f74): MD requires its VDP V counter in
   0xDF-0xE6 before opening a blit window; otherwise skip and RETRY
   the same phase next vint. Kills all mid-frame FBCTL writes (the
   prior "strobe" flashing: compose overruns -> pending vint fires at
   rte mid-frame -> flip mid-frame -> deferred/collapsed latches).
   NOTE: vint fires with V still reading 0xDF (line 223), measured in
   MAME; the 32X VBLK bit (0xA1518A bit 15) is UNUSABLE for this —
   MAME sets it in a 32X callback that can run after the 68K enters
   the handler.
2. 56-ROW BLIT SLICES: 5-phase cycle (compose + 4 slices, 0x2000/
   0x2010/0x2020/0x2030, each CPU 28 rows ≈ 0.3ms MAME). Budget holds
   even if the emulator is 4-5x slower. Display ships at ~12Hz until
   the compose window shrinks — stability first, speed next.

LESSON: vblank budgets must be set against the SLOWEST target
(ares FB-write timing), not MAME's; every flip pair needs 2x+ margin
inside the 2.4ms. Diag counters: 0xFFB0FC gate skips, 0xFFB0FE HV at
last blit attempt.

## ROW-FOLLOWING PIPELINE: no dedicated compose vint — 20Hz display

The dedicated compose window was the display-rate ceiling (cycle =
compose + N slices). Now every vint window does BOTH: the vblank
flip-pair + 75-row slice blit of the SHIPPING frame, then in-window
compose of the NEXT frame's sprites/cat1/text into rows the blit
pointer has already passed. Tile thirds run concurrent between windows
(SDRAM cache). Cycle = 3 vints = 20Hz display; 68K pause total ~11ms
per 50ms cycle in heavy scenes (MAME).

Row schedule (regions tile-aligned at 72/144; slices 75/75/74):
- W0: blit rows 0-74; ext: finish shipping frame rows 144-224 with the
  OLD parity (slave 144-184, master 184-224), then latch regs,
  SPR_SNAP (slave, gated on SYNC[3] so the master's tail finishes
  before the old snapshot dies), copy_pages, par^=1, apply_cram,
  cache_fill. Launch tile third 0 (next frame rows 0-72).
- W1: blit 75-149; ext: sprites+cat1+text rows 0-72 (halves at 36).
  Launch third 1 (72-144).
- W2: blit 150-223; ext: rows 72-144. Launch third 2 (144-224) +
  build_maps(par^1) on the master tail.
Every region composed is a strict subset of rows already shipped this
cycle — new-frame pixels never land in un-shipped rows.

Safety: all windows gated on V in 0xDF-0xE2; if the master loses
vblank waiting on a straggling tile third, it SKIPS the slice (bit 3
of the slave cmd; stale band for one cycle, never a mid-frame flip).
Both CPUs cache_purge before blitting (slice rows may hold the other
CPU's composes from last cycle).

D32XR/Backrooms mining verdict (32x-builder/D32XR_MINING.md, local
srcref): there is NO faster FB path — no FB DMA (FIFO runs
peripheral->SDRAM, RV-gated), AUTOFILL is constant-only, parallel CPU
stores are the state of the art and our unrolled long-store blit
already matches it. Field-measured ares budget: 75 rows/vblank clean
(0 black in 4214 frames with the tight gate), 112 rows over. 30Hz
(2-slice cycle) therefore needs a ~35% faster inner blit loop
(candidate: D32XR's load/store pipelining) — future work.

## ROW-FOLLOWING ROUND 2: heartbeat clock + window rebalance

Field report on aaef286: black-frame bursts (one per cycle in heavy
stretches), slow, jumping frames. Two causes, both fixed:
1. The master's flip-vs-skip check used the 32X FBCTL VBLK bit, which
   is NOT trustworthy at command-pickup time (and the first fix
   attempt — MD publishing its V counter on COMM12 during the ack
   spin — had a stale-read race: the master could read the PREVIOUS
   window's final mid-frame heartbeat and silently skip nearly every
   blit: black bands + palette-drifted stale slices, compose dropping
   11.4->7.7ms was the tell). Fix: MD writes a fresh 0xD0xx-tagged
   V-counter heartbeat BEFORE posting the window command and keeps
   refreshing it during the spin; the master trusts only that tag
   (skip if V outside 0xDF-0xE4) and counts skips in DIAG[7].
2. W0 was fat (leftover + snapshot + CRAM + fills), shrinking the
   concurrent gap that feeds the slowest tile third -> stragglers ->
   vblank eaten at the next window. Rebalanced: CRAM applies at W1,
   miss fills drain at W2; W0 keeps leftover + snapshot only.
MAME: 5460-frame coined run, no stalls, mskips 29/~1800 cycles
(legit late windows -> one-cycle stale band, never a mid-frame flip).
LESSON: any cross-CPU "am I in vblank" decision must consume a value
PUBLISHED FRESH FOR THIS WINDOW — a mailbox holding last window's
clock reads as confidently, and wrongly, as a live one.

## STICKY CRAM ALLOCATOR (kills the power-effect full-screen strobe)

Field report: "any powered effects or flashing sprites cause the
entire screen to flash." Cause: the positional allocator re-numbered
EVERY group whenever the used-color set changed — a sprite flashing
its color each frame reshuffled the whole map every cycle, and pixels
already on screen (composed with last cycle's map) pointed at
re-purposed CRAM entries.

Now colors OWN groups across cycles (grp_key/grp_kind/grp_age,
pr_key/pr_age): keep if seen, age if not, decay after ~90 cycles;
new colors claim free slots, then steal only OFF-SCREEN ones
(age>=3 — stealing an age-1 slot recolors pixels still displayed).
Singles (tile/text) allocate lowest-first; sprite PAIRS (aligned
2p/2p+1) highest-first, with a demand-driven boundary (6..10 pairs)
reserving pair space — without it tiles starved the sprites (MAME
field test: red-silhouette player, purple-less zombies).

Residual: rise-scene text renders yellow (FPGA ground truth: red) —
some text/tile color lands in a shared or re-sourced group; chase
with a CRAM differ vs the reference arcade driver (toolkit item).

ALSO this round: TOOLKIT.md added — the project's second deliverable
is a reusable S16->32X porting kit; that file is the living inventory
of which pieces are game-agnostic and what must become per-title
config.

## THE UNPAIR PROJECT (GO): rebase the game to 0x880000, pin RV=0

Field metrics (perf bars, ares, heavy scenes): total 68K pause mean
18.4ms/50ms cycle (37%), sprites 10.2ms of it, blit 6.3ms. The pause
exists ONLY because sprite/tile art lives in cart ROM and RV=1 forbids
SH-2 cart access — the game's code shares that cart at address 0.

The escape is the STANDARD commercial-32X memory model: the 68K
executes cart code through the 0x880000 window (works at RV=0, bus-
arbitrated concurrently with SH-2 cart reads — how Doom 32X runs).
Program is 0x40000 bytes = fits the fixed 512KB window, no banking.
Plan: patcher rebases every absolute ROM reference +0x880000, RV
pinned 0, SH-2s read art anytime -> sprite/tile compose leaves the
windows entirely; windows shrink to snapshot+blits (~4-5ms/cycle
= ~9% tax vs 37%).

tools/rebase_scan.py census (toolkit module #1): J=1477 jsr/jmp,
L/A/E=67 lea/pea/movea/abs.l, M=303 pointer-stores (movel #addr into
object struct fields +36/+2 — handler/script pointers), D=39 spawn
handlers, W~174 abs.w-encoded (mostly data-as-code noise; real ones
like "jmp 0x47e" @1B5C6 need thunks — 16-bit slots can't hold
0x88xxxx). Burn-down rig: wild jumps land in 32X vector space and the
0xFFB0F8 interrupted-PC sampler names the culprit.

68K-side changes: vint via the RV=0 adapter vector hooks (d32xr crt0
reference), boot jumps 0x880000+entry, shim chain targets +0x880000,
RV toggles deleted. Step 1 = memory-model swap only (windows
unchanged — zero perf delta expected, isolates correctness risk);
step 2 = pull sprite/tile compose out of the windows (the payoff).

Also confirmed already-right: controller reading lives on the 68K
(Backrooms lesson) — SH-2s never touch pads.

## REBASE DESIGN (settled, pre-implementation)

At RV=0 the 32X adapter delivers 68K interrupts to FIXED cart offsets:
HBlank -> 0x880880, VBlank -> 0x8808C0 (d32xr crt0.s layout — stubs
there dispatch via a RAM pointer). Our cart image holds GAME bytes at
those offsets (game body at 0x808+ for the RV=1 model), so:

1. APPEND a second copy of the (rebased) game body at image offset
   0x240000 (past all SH-2 data; zero layout churn for linked SH-2
   cart addresses). The 68K reaches it through the BANKED 0x900000
   window, bank register 0xA15104 = 2, set once at boot. Uniform
   rebase delta: arcade addr A -> 0x940000 + A.
2. OVERWRITE cart offsets 0x880-0x8FF with d32xr-style vector stubs
   (jmp via RAM pointer -> shim handlers). The low game copy is dead
   at RV=0 (execution and data reads all go through 0x94xxxx).
3. Patcher rebase pass (class-A confirmed operands in [0x100,
   0x40000)): EA/hex-confirmed always; long immediates only when the
   destination is an address register or a memory store (the 303
   pointer-store class); #imm into data registers left for runtime
   burn-down. Spawn-table handler longs +delta. abs.w code refs
   (16-bit slot) get work-RAM thunks via sign-extended negative
   abs.w (0xFFFFB0xx) when hit.
4. Shim: RV never set to 1; game-entry and vint-chain targets
   +0x940000; FM windows unchanged (step 1 = memory model only,
   perf-neutral by design; step 2 pulls compose out of windows).

## REBASE DESIGN v2 (supersedes v1 details above — simpler)

Key realization: the HIGH copy at cart 0x240000 is the FULL 256KB
arcade image (offsets 0x0-0x3FFFF), not body-only. Consequences:
- The game's displaced boot bytes (arcade 0x400-0x7FF, unplaceable in
  cart LOW space because the Sega security blob owns those offsets)
  exist at cart 0x240400+ = 68K 0x940400+ — the boot executes IN
  PLACE with pc-relative code intact. The 0xFFB400 RAM stash, ALL
  bfix conversions, and the four jump-in patches DIE. Their whole
  reason (displacement) is gone.
- game_high.bin patch set = existing HW staging/IO passes + RLE
  word-write patch + the rebase pass (+0x940000) + ONE thunk
  (0x1B5C6 jmp (47E).w -> jmp (B3F0).w; shim thunk jmp 0x94047E.l).
- The LOW copy shrinks to just the adapter vector stubs:
  cart 0x880 (level 4): rte;
  cart 0x8C0 (level 6): move.l (B3FC).w,-(sp); rts   [2F38 B3FC 4E75]
  — 68K vint jumps via the work-RAM pointer 0xFFB3FC -> shim_vblank.
  (MAME confirms: at adapter-enable, 0x0-0x3FFFFF = 32X vector ROM,
  fixed L4/L6 vectors -> 0x880880/0x8808C0; H-int vector at 0x70-73
  is a writable register. 0x880000 fixed window + 0x900000 banked
  window install exactly as the design needs.)
- Boot: set 0xA15104 bank=2, write 0xFFB3FC=&shim_vblank, never set
  RV=1, enter the game at 0x940000+reset-PC. Shim vint chain target
  0x942AAC. RV toggles deleted from the window scheduler.
- Rebase pass rules (patcher): class-A confirmed operands in
  [0x100,0x40000): hex-confirmed EA/jsr/jmp/lea always rebased;
  decimal-confirmed long immediates only into %aN or memory stores;
  #imm->%dN skipped (runtime burn-down); .short/.long ctx lines
  excluded (data-as-code false positives); spawn-table handler longs
  at +8 of each 12-byte record rebased explicitly.
Branch: unpair-rebase. Main stays playable (2be2a5f-era) meanwhile.

## UNPAIR STEP 1 LANDED (branch unpair-rebase): the game runs rebased

Burn-down log (each caught via MAME trace/stack instrumentation in
_vblank — 16 stack words at 0xFFB100 every vint):
1. Mode-dispatcher jump table 0x26DC (lea pc-table; movea.l (A0,D0);
   jmp (A0)) -> harvested ALL such tables from the disassembly;
   word-OFFSET variant self-heals (addaw (A0),A0) and needs nothing.
2. Handler-pointer tables consumed via movel %aN@...,@(2) after a
   pc-lea -> harvested by idiom scan.
3. Spawn-script region is TWO-LEVEL: meta-table at 0x1D32A of 5 per-
   round record-table pointers, records stride 12 with handler at +8
   (walker at 0xD842); tables extend past the old DATA_EXCLUDE bound.
4. Two-level dispatch at 0x4C00 (table of sub-tables at 0x6DA0) ->
   RECURSIVE table expansion (entry pointing at >=3 plausible pointers
   is itself a table).
5. Runtime handler harvest (toolkit move): run the WORKING RV=1 build,
   sample live object-handler longs (slots 0xFFC000/0xFFD800 +2) over
   attract+gameplay -> 43 distinct values -> rebase all data
   occurrences (tools/harvested_handlers.txt, 181 sites).
6. THE CLOSER — dispatcher normalization: the two consumption funnels
   (0x39A8 object dispatcher, 0xD842 spawn walker) re-pointed at shim
   RAM thunks (0xFFB3A0/0xFFB3C0) that add +0x900000 to any low
   pointer AT CALL TIME. Every handler-table format, enumerated or
   not, is covered. After this: NO drops in 5400 frames of coined
   gameplay ("no drop" from the stack-catch harness).

Total: 2289 static rebases + 2 dispatcher thunks + 1 abs.w thunk.
MAME-verified: boots to title, coined gameplay through the wolf
scene, window telemetry healthy (gpc idles at 0x903982). The vint
delivery needed NO changes — the adapter's writable H-int vector at
0x70 (already in use, field-proven on ares) works at any RV.
Worktree lesson re-learned: compound commands with cd drift the
shell's cwd — the patcher once silently ran from the main worktree.

NEXT: ares smoke test, then STEP 2 — pull sprite/tile compose out of
the pause windows (cart readable at any time now).

## === RESUME POINT (2026-07-30c) — BLACK ACTORS = SPRITE-PAIR CRAM ZEROS ===
Mike's ares attract savestate (rom/s16.bs1, demo scene with ALL
actors as black silhouettes, tiles/HUD fine — screenshots/2100.png):
- shadow_dirty=1 after minutes: idle-only LUT rebuild STARVED under
  ares load. FIXED this commit: guaranteed-progress steal (one chunk
  per window even with the queue busy; full refresh <=3.2s any load).
- CRAM entries 160-207 (sprite pairs 10-12) ALL ZERO -> actors
  black. apply_cram's sprite loop reads FB_PAL+1024+sc*16 (palette
  staging IN THE FRAMEBUFFER) via spr_pair[par][sc]. Suspect: the
  palette snapshot reads the WRONG BANK at ares timing (the intro-
  cast snapshot/bank-restore race family) — game wrote sprite rows
  into bank A, snapshot read bank B (never-written = zeros).
- Also confirmed: real palette data carries bit 15 (0xFFFF entries,
  groups 1/4/12) — the jts16 shadow-exemption is live on real data.
NEXT PROBE (savestate-only, no ares scripting): locate BOTH 32X
DRAM framebuffers in the .bs1 (search for FB_PAL sprite rows with
actor colors), diff FB_PAL region bank A vs bank B: colors-in-one/
zeros-in-other proves the bank-skew read. _spr_pair from s16.lst
gives the pair mapping to check which sc rows map to pairs 10-12.
CONFIRMED (same savestate): reconstructed the S16 words for mirror
group 12 (inverse s16_to_mars) and searched the state file: exactly
TWO copies — the game's 68K work-RAM palette buffer (~MD 0xE5B0)
and ONE framebuffer bank's FB_PAL. The other bank NEVER got those
rows: palette writes land only in the bank staging at write time.
Any row written during the other bank's tenure is zeros in the bank
apply_cram happens to snapshot -> black sprite pairs. SH-2 cannot
read the other bank (single draw-bank window), so no SH-2-side
fallback exists.
SUPERSEDED by 24b799a — the FB copy below was ALSO dropped on ares
(FB-window arbitration discards MD writes while SH-2 owns the FB;
proven: mirror populated, FB rows still zero). Palette now rides the
acked COMM stream (tag 0x4800) into PAL_SH at SDRAM 0x27000; MD
diff-scans mirror quarters, FIFO dirty queue (LIFO starved rows
under fade churn). VERIFIED: PAL_SH==mirror word-for-word; MAME
gameplay actors correctly colored; eye scene clean.
METHOD WARNING (cost a lot of this session): frame-number
comparisons ACROSS BUILDS are invalid — mskips drift shifts the
game timeline minutes apart; at f=1700 one build is in gameplay,
another still in the eye hold. Scene-match or reg-match, always.
REMAINING KNOWN (pre-existing, next arc): full-width band of
repeated sprite blocks at deck level in gameplay (sprite-list
decode/terminator suspect — visible in MAME, scene-stable);
ares gameplay text-strip garbage; tearing (atomic-ship umbrella).

OLD (superseded): palette writes remapped to a stable MD RAM mirror
(0xFF9000); shim vint copies one 512-word quarter/vint into the
staging bank's FB_PAL — both banks always complete, snapshot can
never read never-written rows. Fade RMW reads hit real RAM. Known
gap: fresh rows lag <=4 vints (~67ms) — brief dark flash on hard
scene-palette loads (MAME: transient blob at f=1500 player-spawn,
clean by 1700). If ares objects: prioritize dirty quarters (MD
tracks which quarter got game writes since last copy, copies dirty
first, 2/vint burst). Also landed same session: guaranteed shadow-
LUT progress (steal one chunk/window under load). BOTH ares
symptoms from the attract savestate now have fixes in rom/s16.32x.
Savestate parse one-liners: sdram file base 0x23B (probe-relocate
per TOOLKIT recipe); MD RAM base = find swap16(TAS thunk bytes
4a38c02050f8c0204e75) minus 0xB380.

## === MILESTONE (2026-07-30b) — ARES CLEAN READ ON THE ATTRACT ===
Mike: "the eyes are working without the issues! only screen tearing,
but thats on the roadmap. we have a clean read!" — the full attract
rotation (title -> eyes -> demo -> (C)SEGA) passes the acceptance
gate. His ares savestate (rom/s16.bs1, parsed per TOOLKIT recipe)
shows DIAG[13]=864: ares drops bands steadily in attract and the
rotating drop-resume absorbs it invisibly — diagnosis confirmed on
the gate emulator itself. Ares pp3=1034/amb=9: priority-exactness
work is live next. OPEN (ordered): tearing (atomic-ship umbrella),
shadow x0.75 + pal bit-15 exemption (jts16_colmix.v), priority
punch-through/fallback (jts16_prio.v), attract pacing debt.

## === RESUME POINT (2026-07-30) — EYE "WHITE BAND" = LOCKED DROP ===
Mike's ares report post-TAS-fix: attract rotates, but the eye scene
has a moving white band stripe. DIAGNOSED: drop-oldest under ares's
sustained mild overload victimized the SAME strips every cycle — a
locked stale stripe (mustard eyelid bar from an earlier pan position
+ growing lavender gap). MAME never drops there (bdrain=0), so it
was reproduced with PRESSURE BUILDS (quiet zone temporarily cut
11300->6500: eye hold drops ~1 band/2 cycles = the ares regime; the
identical stripe appeared, proving the mechanism). FIX (a4b51d0):
rotating drop-resume — per-region stale frontier drop_s0[]; a
dropped band's next same-region band starts its strip walk at the
frontier (stale rows first), bounding any row's staleness to ~2
cycles. Bands now walk strips by rotating index (s0+cnt, wraps; 6
strips R0/R1, 4 strips R2) instead of linear y. Validated: clean at
moderate pressure WITH active drops; severe pressure spreads
staleness instead of locking (expected); zero-pressure identical to
before. METHOD (toolkit): pressure builds — throttle your own
budget until MAME misbehaves like the slowest target, fix at that
operating point, then restore. Regression: skips=13, scenes intact.
NEXT: Mike's ares verdict on the eye scene ("clean read" candidate).
If residual shimmer on ares, an ares savestate taken mid-artifact
(+DIAG[13] drop counter at 0x06028000+52) tells us the actual ares
drop rate; the structural cure remains the atomic-ship rework.

## === RESUME POINT (2026-07-29b) — ATTRACT STALL SOLVED: TAS ===
ROOT CAUSE (not a patcher bug — a HARDWARE divergence): the game
latches one-shots with tas/bne. On System 16B, TAS's locked
read-modify-write works; on the Mega Drive/32X the bus arbiter DROPS
THE WRITE PHASE of the 68K RMW cycle (the classic Gargoyles-era MD
quirk — modeled by MAME, ares, and real hardware). Every TAS latch
in the game silently never sets.
THE STALL: attract gate at 0x2260 (script actor, record C100):
  cmpi.w #$1001,$c00c ; bcc wait     <- eye-camera X must park
  tas $c020 ; bne skip               <- once-only latch (BROKEN)
  add.w #$c00,$c014                  <- kill velocity F400 -> 0000
  tst.b $c0a0 ; beq wait             <- anim-done flag (set by the
  jmp $1e54                             0x23DC driver's terminator)
With TAS never latching, the velocity add fired TWICE (F400 -> 0C00
= +12px/frame): the camera ran away past x=0x1001, and since x is
tested BEFORE the done flag, the transition was locked out forever
= infinite title/eye loop + the runaway pan ("broken consecutive
animations"). Gate-variable logging (gatelog.lua pattern: c00c/
c0a0/c0a1/c0a2/c014/f02d) proved it against the arcade: arcade adds
once and parks; ours added twice and ran away.
THE FIX: patch_game.py TAS_SITES — full-binary opcode scan found
exactly 5 real TAS instructions (0x2268 $c020, 0xE098 $f15a, 0xEAC0
+ 0x150B6 (3E,A0), 0x12E84 (3C,A6); other 4AC8-4AFF words are
data). Each 2-word TAS -> jsr to an MD-RAM thunk (md_main.c):
tst.b (TAS's exact N/Z, clears V/C) + st (sets latch, no CC) + rts.
Thunks at FFB380/FFB38A/FFB394/FFB3F6 (B3F6+10 = B400 boot copy,
exact fit). VERIFIED: transition fires (f031 0x10->0x14, hold ends
at c0a0=FF exactly like arcade), demo gameplay scene renders, loop
cycles back to (C)SEGA — full arcade attract rotation. 5460-frame
regression: 45 skips, no stalls.
LESSON (toolkit-grade, whole-library): S16->32X ports must scan for
and replace EVERY TAS; MD hardware breaks them all. Also: log the
GATE VARIABLES of a stuck state machine side-by-side with the
oracle before tracing — two 70-line logs named the bug in one look.
ALSO NEW: srcref/jtcores (Jotego FPGA S16, GPL-3.0) is now the
authoritative hardware spec — derive behavior from the Verilog,
cite the .v file, never copy code (GPL). Key: jts16_prio.v,
jts16_colmix.v, jts16_shadow.v, jts16_obj*.v, jts16b_mapper.v.
NEXT: ares verdict on the attract rotation; then verify priority/
shadow/sprite rules against jtcores Verilog (pp3/amb exactness),
attract pacing debt (mskips ~900 heavy scenes), atomic-ship rework.

## === RESUME POINT (2026-07-31) — ATTRACT STALL PINNED TO ONE GATE ===
State: unpair-rebase @ bd48d00 + uncommitted md_main.c (rowscroll
priority streaming, 16 batches) — commit with the fix.
MIKE'S REPORT: first eye animation correct, consecutive ones broken,
loops until coin. FULLY CHARACTERIZED via synchronized state logging
(synclog.lua: f026/f02A/f031 + pages/xs same-run):
- Timeline IDENTICAL to arcade through scream (AAAA) + eye hold
  (pages 0101, xs 0x140, f031=0x10, f02A frozen 0x2BA = WAIT state).
- ARCADE at hold+146 frames: sequencer fires — f02A reset 0, f031
  0x10->0x14 (writer = 0x1E4E block, the game-mode entry with stack
  reset at 0x901E5A) -> demo replay runs (index +1/frame, script #
  from f031 bits 3-4 via pointer table 0x1834 -> scripts 0x3E4B0/
  0x3EDB0/0x3F6B0 — all verified byte-identical, pointers correctly
  rebased).
- OURS: THE TRANSITION NEVER FIRES. f02A/f031 frozen; at hold+160
  the scream-scene camera resumes panning -12px/frame FOREVER
  (pages cycle 0101->1212->2323->3434->4040 every ~340 frames = the
  runaway "broken animations"). Demo replay never starts (player
  would run right if it did — actually the world scrolls with NO
  demo at all).
- Input mailboxes verified idle (0xFF); poison sweep of the window
  CLEAN; demo scripts unmodified.
NEXT PROBE (one wp-chain): find the eye-hold TIMER — wp READ on
fff031 during the hold names the sequencer poll PC; dasm it; its
timer/condition variable RAM-diffed ours-vs-arcade at hold start;
expect ONE corrupted duration/trigger value (the +0x90 family) or a
condition on a subsystem we stub (RULED OUT by sndlog probe: sound
mailbox idle 0xFF, MCU_BUSY 0, f144 vint-lag counter frozen = main
loop healthy — the stall is purely the sequencer's trigger). (the
0x1E4E path resets SP — it's the mode bootstrap reached by a jump
from the sequencer). DEFINITIVE NEXT STEP: trace-diff 5 frames
around the arcade's hold-end (its f031 0x10->0x14 moment) vs ours at
the same phase — the first divergent branch in the sequencer polling
loop IS the broken condition. Use synchronized same-run frame
anchors, not -debug-skewed windows.
Timing note: -debug shifts attract timing by frames — synchronized
same-run logs only; separate wp runs have skewed windows.

## === RESUME POINT (2026-07-30d) — EYE GEOMETRY EXACT ===
State: branch unpair-rebase @ 688a9e5. Eye-sequence geometry now
PIXEL-MATCHED to the arcade at register-matched frames. Three fixes
on top of the sign fix: 10-bit xscroll (latch masked to 9 bits —
every pan past the map midpoint was 512px off), rowscroll-x path
still had the pre-fix negated sign, and PRIORITY REG STREAMING (the
rotating text stream gave each word 0-6.4 frames random staleness;
pages could be fresher than their xscroll neighbor → mismatched
compose state during fast pans; the reg block 0x740-0x753 now ships
every vint, SH-2 tracks MD within ~1 frame).
METHOD ADDITIONS: reg-stamped sequence capture (seqcap.lua — align
machines by REGISTER STATE, never frame number: attract rotations
differ and drift); reg-matched trigger capture (regmatch pattern);
remember the NVRAM-credits trap applies to EVERY arcade run.
Remaining ledger: attract pacing (mskips ~900 in heavy scenes),
allocator capacity, big-shadow cap, pp3/amb exactness, text-layer
palette phase (logo white vs teal at matched frames — text palette
streaming timing, small). Structural rework (atomic ship) still the
umbrella for the pacing class. Ares verdict wanted on 688a9e5.

## === RESUME POINT (2026-07-30c) — SCROLL X SIGN FIXED ===
State: branch unpair-rebase @ 2cad330. THE EYES ARE SOLVED — and the
root cause was bigger than the eyes: the X scroll conversion was
SIGN-INVERTED since the project began (vx = screen + ((0xC0-xs)
&0x3FF), we had the negation). Survived every oracle comparison
because the title is sign-agnostic (xs=0xC0) and gameplay backgrounds
are locally periodic. Pinned by the attract TRANSFORMATION SCREAM
(unique art, cols 24-63, xs=0): sign_scream.png is now pixel-
identical to the arcade. The eye "split"/"band"/"skin fragments" =
wrapped-column views of correctly-loaded pages — geometry, not
loading, not pacing. Also fixed en route this arc: BG/FG reg
mapping verified correct against segaic16 (FOREGROUND=idx0=word
0x740); page A/B content verified word-identical to arcade during
transitions; reg stream verified live-tracking (~1-frame lag).
The 2026-07-30b "visible loading state" diagnosis was WRONG — kept
for the record; the atomic-ship re-plumb remains queued for the
pacing ledger (attract mskips ~950, band drops) but the eye/scream
GEOMETRY is now exact.
Mike's next ares pass should show: eyes/scream/attract art all
correctly framed; backgrounds possibly subtly improved everywhere
(any place the old sign wrapped unnoticed).

## === RESUME POINT (2026-07-30b) — EYE DIAGNOSIS COMPLETE (superseded) ===
Root cause found via live reg logging (reglog.lua pattern): the eye
TRANSITION loads the next eye into pages 0xA/0xB (regs flip to
AAAA/BBBB, xscroll 0) while the old eye shows from pages 0/1. The
arcade finishes that load fast enough that the intermediate state is
never displayed (yarc_1200-1300 show only complete frames). OURS
holds the mid-load state on screen for MANY frames — the 68K's tile
writes go through the contended MD FB window and our render windows
tax it ~15% — so the wrap-seam at xscroll 0 (the "black band") and
the mismatched page/palette pairing (wrong pupil colors) are the
VISIBLE LOADING STATE, not render bugs. Stream/decode/compose all
verified correct (SH-2 shadow tracks MD regs with ~1-frame lag; page
content byte-identical to arcade at scene-matched frames).
CONSEQUENCE: the entire remaining accuracy ledger — eye transitions,
attract pacing, band staleness, big-shadow cap, pp3-over-cat1, amb
colors — funnels into ONE architectural item: the ATOMIC-SHIP +
STAGING-STREAM re-plumb (shrink/eliminate 68K stalls, complete-frame
handoff, per-pixel priority sideband). That is THE next work item,
designed fresh at full context next session.

## === RESUME POINT (2026-07-30) — EYE-SCENE DEFECTS PINNED ===
Mike's ares round on 4c6aa1a: eyes DECODING (quadrant fix ✓) but not
MAME-matched. Both defects now REPRODUCED IN MAME on the FIRST
(uncoined) attract loop — earlier captures sampled the post-game loop
which renders clean, hiding them. Deterministic, locally debuggable:
1. VERTICAL SPLIT-BAND: ours shows two half-eyes with a black seam
   (y32_1400); the arcade NEVER splits — it morphs the eye FULL-FRAME
   by palette animation (yarc_1200-1300: blue pupil phases). Not
   rowscroll/alt-set (arcade regs at the scene: rowscroll all zero,
   plain scrolls, BG pages=0000, FG pages=5555).
2. WRONG PUPIL COLORS (magenta vs blue/gold phases; later full
   red/white chaos at y32_1800) — palette-cycle phase/script wrong;
   same family as the 0x1A6FA launch-table fixes (more scripts or
   phase state likely broken).
Timeline note: ours runs ~150 frames behind arcade from boot overhead
— SCENE-match, don't frame-match. Probe queued for next session:
diff OUR SDRAM shadow pages (now 0x06019000!) at the split moment vs
arcade tile RAM at its scene-matched frame (~ -150): content split =
load/copy path; content correct = compose path. Then wp the palette
stream slots (0xF300 family) ours-vs-arcade through the morph.
TOOL NOTE: probe luas from the old scratchpad have STALE region
addresses (shadow was 0x18000, DIAG 0x27000) — regenerate from the
new map (shadow 0x19000, TEXT 0x26000, DIAG 0x28000, SNAP 0x28400).
FB_PAL reads from lua at frame_done race the access bank — reads of
0x2401F000 returning zeros are AMBIGUOUS, use in-window state or the
cram_mirror (0x06... .bss, dumpable via nm address) instead.

## === RESUME POINT (2026-07-29 late) ===
State: branch unpair-rebase @ 4c6aa1a. Accuracy round 2 landed: TRUE
SHADOW DARKENING for standard shadow sprites (nearest-darker CRAM via
shadow_lut, lazy 4-entry idle rebuilds from cram_mirror; silhouette
fallback while dirty and for actors >48 rows tall — the giant
cutscene cast at 2x/pixel saturated both CPUs into band staleness,
a worse inaccuracy). PACING LESSONS bought with blood this round:
- 64-entry LUT chunks = ~10ms of multiplies (est. said 0.5) — SIZE
  idle work by MEASURED cost, not arithmetic hope.
- The slave may NEVER run a multi-ms pass without stream service:
  one whole-half shadow-sprite call stalled the MD past its vint
  gate (458 skips). Strips + service between, always.
- Finer strips are NOT free for tall zoomed sprites: every strip
  call walks the sprite's FULL height for address stepping.
ACCURACY LEDGER (current approximations, all measured/logged):
1. Shadow actors >48 rows: silhouette (attract/cutscene only).
2. pp=3 sprites vs FG-cat1/text-cat0: approximated as pp=2 (DIAG[16]
   sightings = shadows only so far).
3. amb= colors at two priority levels: 5-7 in title scenes (pri_lut
   takes max level there).
4. Column scroll: unimplemented (no sighting yet).
All four resolve structurally in the SIDEBAND + ATOMIC-SHIP rework
(per-pixel priority plane + complete-frame handoff) — next major.
Attract pacing debt: mskips ~1k in the post-game attract (gated
emergence actors); gameplay clean (MD skips 2, mskips 169, compose
2.60ms). Mike's ares verdict wanted on 4c6aa1a.

## === RESUME POINT (2026-07-29 evening) ===
State: branch unpair-rebase @ 7ef5526. ACCURACY MANDATE ACTIVE (Mike:
"accuracy before speed — must look and play exactly like its MAME
source"; memory: accuracy-before-speed).
Landed: EXACT sprite/tile priority mixing per segas16b_v.cpp — pp=2
exact by layer order (measured dominant), pp<=1 per-pixel gated via
the value->level pri_lut (these are the intro emergence actors that
rise BEHIND the gravestone — Mike's "layer eating" report was partly
THIS, now hardware-correct), pp=3 counted (only shadows). Gates kept
OUT of the unrolled hot macros (register-spill lesson: +43% sprite
time; uncached-read variant even worse via SDRAM bus contention).
ALSO: found+fixed a LONG-STANDING .bss/tilemap-shadow overlap at
0x18000 (cache tags stomped every window; steady-state miss ~10/win
instead of ~0 in every prior build). Regions now at 0x19000+, and the
MAKEFILE FAILS THE BUILD if __end crosses the base — kit rule: every
hard-coded memory map gets an enforced boundary check.
Telemetry legend additions: amb= colors at 2 levels (LUT approx
engaged, 5-9 in title scenes only), pp3= pp==3 sprite sightings.
Verification: compose 2.50ms = baseline, ch_1700 matches the arcade
oracle full cast for the first time; mskips 107 concentrated where
big zoomed gated actors pay the gate honestly (gameplay clean).
Accuracy queue (in order): 1. Mike's ares pass on 7ef5526.
2. Shadow sprites: real darkening (arcade adds a shadowed palette
copy; we currently draw silhouettes). 3. amb-color exactness (title
scenes) + pp=3-over-cat1 exactness — both fold into the atomic-ship
compose rework (priority sideband). 4. Column scroll (log-if-seen).
5. Allocator capacity (nearest-color overflow). Then atomic ship.

## === RESUME POINT (2026-07-29 afternoon) ===
NEGATIVE RESULT (measured, load-bearing): 30Hz/2-slice cadence does
NOT fit — full-frame compose at a 2-vint cycle is 1.5x the sustained
budget (slave half-region stopped finishing between windows: swait
1.5ms, mskips 1323, bdrain 4402, MD gate slipping to 0xE9+). Reverted
to the 3-phase/20Hz config (mskips 5). CONCLUSION: the pipeline is
COMPUTE-BOUND at 20Hz full-frame rate; ares smoothness must come from
ATOMIC shipping, not rate. Next architecture step is therefore the
double-sbuf whole-frame handoff at 20Hz:
  - compose frame N+1 into sbuf_B while slices of completed frame N
    ship from sbuf_A; swap pointers only when N+1 is COMPLETE (all
    bands + sprites + text). A band that misses its deadline delays
    the swap one cycle (whole frame holds, still coherent) instead of
    showing a stale band.
  - memory: second buffer needs ~72-80KB. Candidates: shrink the tile
    cache (direct-ROM misses make small caches viable — measure miss
    cost first) and/or drop sbuf borders for the ship buffer.
  - eye-screen artifacts on ares (vertical black band) are timing-
    class: MAME same build renders them clean (e32_4950/5250).
Ares verdict from Mike on fa1faf8: EYES/title art confirmed working
("accomplished the impossible"), gameplay still has the staleness
class — matches the atomic-ship diagnosis.
Workflow decision (Mike): interactive for architecture; /loop for
oracle-verified parity grinds (rounds 2-5) once gameplay stabilizes.
End goal recorded: the whole sega16 library ported via the kit.

## === RESUME POINT (2026-07-29) ===
State: branch unpair-rebase @ fa1faf8. TITLE ART RESTORED (the "eyes"
regression Mike reported was never a regression — the attract title
scenes had NEVER rendered in the port). Root cause: page-select
quadrant decode X-swapped vs segaic16 (UL=bits0-3, UR=4-7, LL=8-11,
LR=12-15). Gameplay had survived because scrolling scenes keep both
X-halves loaded; the title is a single-half scene. Landed with it:
ALT register set + per-row parallax xscroll support in compose_layer
(fast path for the common case) — the rounds-2+ parallax groundwork.
Also this session: direct-ROM tile misses (placeholder machinery was
an RV=1 artifact; cold scenes now always correct), drop-oldest band
queue (attract block-drain cascade -> isolated stale bands),
tools/oracle_shots.lua + wpcatch.lua now take the LOCAL rompath:
MAME ROMs live in ./mame/ (gitignored) — no NAS needed.
DIAGNOSIS LESSONS (toolkit): clean-NVRAM before arcade-oracle attract
comparisons (persisted credits change the attract flow); scene-flow
"skips" can be layer-decode bugs (data was present and correct at
every stage — staging, shadow, regs — only the final quadrant lookup
was wrong); verify each pipeline STAGE (68K write stream -> staging
-> SDRAM shadow -> regs -> compose) before touching code.
Full run: mskips 5, compose 2.51ms, gameplay screenshot-identical,
title frame-matched vs arcade.
Next: 1. Mike's ares pass (title, intro, smear). 2. Whole-frame-ship
refactor proposal (compose into off-screen FB bank, flip per frame:
kills band staleness/tearing wholesale). 3. Roadmap: rounds 2-5,
sound, allocator polish, column scroll (log-if-seen), merge.

## === OLD RESUME POINT (2026-07-28 evening) ===
State: branch unpair-rebase @ 3c99d3b. THE ENTRANCE ANIMATIONS ARE
BACK (Zeus head + orb + lightning + gravestone + rise smoke, verified
frame-matched vs the arcade oracle) plus Mike's "P1 spawns at P2"
fixed (+0x90 pan-speed corruption) and shadow sprites render as dark
silhouettes. Two root causes closed the intro:
1. Harvest false positives: value 0x00010000 patched at 72 DATA sites
   (movement/spawn records); one was the cutscene pan speed 0x0001 ->
   0x0091. Now a VALUE BLACKLIST {0x10000,0x102,0x104,0x106} in the
   harvest pass — call-time thunks cover any real handler.
2. SH-2 snapshot race: the slave's W1 SPR_SNAP copy raced the
   master's FB bank restore and read display-bank zeros — short
   cutscene sprite lists vanished whole. Snapshot moved to the master
   after its own restore.
TOOLKIT ADDITIONS (permanent): tools/oracle_shots.lua (frame-matched
arcade ground truth), tools/wpcatch.lua (r/w watchpoint catcher with
values, both machines), the paired-trace + RAM-diff + build-buffer
diff pattern (ours-vs-arcade at the same frame answers "which side
broke" in one run).
LESSONS: ascending byte ramps forge ascending "pointer" longs (the
0x1AD10 easing curve); ascending REAL pointer tables exist too
(0x1989E — reverting it stalls boot); collision-family harvested
values are best killed by VALUE, not by region.
Pacing state: sprite strips pre-vint margin 10800 ticks, tiles 11300,
heavy 8000, cache_fill 128/shot. Full coined run: mskips 13 to end of
gameplay, bdrain 0; post-game ATTRACT still saturates bands (bdrain
1523, mskips->143) — polish item, gameplay unaffected.
Next: 1. Mike's ares verdict (rom/s16.32x). 2. Attract-scene band
saturation. 3. Roadmap unchanged (30Hz retest, rounds 2-5, sound,
parallax, allocator polish, merge).

## === OLD RESUME POINT (2026-07-28) ===
State: branch unpair-rebase @ 5f7f9ed. Step 2 complete + band-staleness
fix VERIFIED in MAME: interruptible band state machine + heavy-phase
self-pacing + PRE-VINT QUIET ZONE (master starts no band work >11300
FRT ticks after pickup; polls only). Full coined 5460-frame run:
mskips 2 (was 385, and 923 before self-pacing), bdrain 0, MD skips 1,
compose 2.32ms/cycle, sprites whole in screenshots.
DIAGNOSIS TRAIL (toolkit lesson): swait/bdrain telemetry exonerated
slave-wait and queue-full; the pickup latency WAS the 12-row strip
(~0.4ms = 6 scanlines) vs the heartbeat gate's 2-line worst-case
slack. Shrinking strips (6/4-row) backfired: per-strip overhead
saturated throughput -> block-drain feedback loop, 2305 mskips. The
quiet zone keeps full-size strips AND ~0 pickup latency for ~0.7ms
idle/frame. LESSON: when a latency budget is blown, idle before the
deadline beats shrinking the work unit — overhead scales with count.
Next actions, in order:
1. ARES: Mike tests rom/s16.32x — verdict wanted on floating heads /
   split sprites (band staleness) and overall choppiness.
2. Known open artifacts: solid-red player / red legs / white+yellow
   rectangles = CRAM allocator capacity (polish pass queued); purple
   title field; tearing at rows 75/150 (30Hz/2-slice retest queued).
3. Then: rounds 2-5 asset sweep, sound (megadriveref Z80 driver),
   shadow pen, row/column parallax, merge to main after approval
   (strip debug bars first).

## === OLD RESUME POINT (2026-07-27 evening) ===
State: branch unpair-rebase @ 9140cf8 — the rebased (RV=0, 0x900000-
window) build boots and plays in MAME; ares smoke test PENDING (build
was launched in ares, verdict not yet reported). Main branch = last
field-approved build (sticky CRAM, 20Hz row-following pipeline).
Next actions, in order:
1. Mike's ares verdict on the unpair build (boot? artifacts? speed
   should feel UNCHANGED — step 1 was memory-model only).
2. STEP 2: pull sprite/tile compose out of the pause windows (cart is
   SH-2-readable at any time now) — windows shrink to snapshot+blits,
   ~18ms pause -> ~4-5ms. This is the game-speed payoff.
3. Watch list: implausible demo-HUD score seen once in MAME (possible
   harvested-handler false positive in a data table); the 13 skipped
   immediates in tools/rebase_report.txt are the remaining burn-down
   candidates if anything else acts odd.

## POISON RIG + READ-POINTER BURN-DOWN (unpair, continued)

TOOLKIT BREAKTHROUGH: the dead low copy (cart 0x808-0x3FFFF) is now
0xFF-POISONED. MAME leaves the cart readable at low addresses (the
leniency that hid every missed READ pointer), so poisoning makes MAME
fail EXACTLY like ares — the whole remaining burn-down runs in the
scripted rig, no ares round-trips. Catches so far with it:
- Sprite frame-table base #0x255E0 in %d2 (a deliberately-skipped
  data-register immediate; consumed via adda.l -> movea.l (A0) ->
  address-error cascade into the adapter vector weeds). Fixed via a
  REBASE_IMMEDIATES override list.
- Detector note: mask the sampled 68K PC to 24 bits (abs.w-called
  thunks report as 0xFFFFB3xx).
Earlier the same evening, the ares field probes (word-value bars) had
proven the garbage tilemap was 68K-side: the RLE loader's stride-6
per-round source table at 0x1CE2 read poison. Global lea-table sweep,
mixed-table classifier (runtime addresses pass through), stride-record
tables, odd data pointers — all landed in the patcher.
STATUS: full coined 5400-frame run clean on the poisoned build; tiles/
sprites/gameplay render; REMAINING: patchy wrong-color BG rows (sky
shows brick/water strips) — now MAME-reproducible; next tool is a
TILEMAP_U staging diff old-vs-new build to name the loader.
ALSO WATCH: implausible score digits (possible over-rebase false
positive) — the staging/RAM diff will catch its table too.

## THE MOVEW-CLOBBER IDIOM FAMILY (latent since the FIRST staging remap!)

wp-harness catch: the strip-blitter at 0x258A builds dests as
  movel #BASE,%d0 ; movew (a0)+,%d0   (REPLACES d0's low word)
— the arcade relied on BASE having a ZERO low word (0x400000). Our
staging bases don't (tile 0x852000, text 0xFF8000): the +0x2000/+0x8000
bias silently died at every such site, in EVERY build to date. This is
the long-standing "misassembled sky strip" artifact class AND stray
text-writer stores into 0xFF0xxx (shim .data!). Fix: movew -> ADDW at
9 audited sites (offsets never carry). Excluded lookalikes: delay
counters / tile-attribute builders (movew #0xA000).
LESSON (toolkit, permanent): any base remap to a non-64K-aligned
address must audit the movew-into-low-word idiom — grep every
movel #BASE,%dN and trace the next d0 write.
Tools this round: wpset-with-condition + lua stop-poll harness
(wpcatch.lua pattern), staging page differ old-vs-new builds.
REMAINING: sky brick/water patch rows still visible in MAME under
poison — next isolated target (suspect: another writer family or the
row-scroll tables); rig is fully local now.

## OVER-REBASE REGRESSION PAIR (caught by Mike's ares screenshots)

1. Red-silhouette mis-spawns: the spawn-record walker rebased past
   table ends (no shape check) into spawn PARAMS. Records are
   [camX][p1][p2][flag 0x0000|0x0040][handler.l], camX=0xFFFF
   terminates — walker now shape-gated (31 verified handlers).
2. Scores of 900100/1800200/2700300 = BCD points +0x900000: the
   pointer-store heuristic rebased `movel #imm,<mem>` immediates —
   score awards to 0xFFF032. Rule DROPPED entirely: real handler
   stores are normalized at call time by the dispatcher thunks, data
   mailboxes stay untouched. (2056 static rebases now, down from the
   over-eager 2354 — recall handled by thunks.)
LESSON: consumption-point normalization (thunks) beats aggressive
static sweeps for ambiguous immediates — sweep only what's PROVEN
pointer-shaped by type or table structure.
OPEN: sky brick/water patch rows (round-1 BG) + round-2+ demo garble
+ purple fleck columns + Zeus sprite dropouts. Scene-synced staging
differ rigged (rise-text trigger); old-build trigger needs longer
timeout (its boot is slower).

## SKY-PATCH CLASS: narrowed to RLE pass interleave corruption

Scene-locked staging census (coined rise, f=1400, both builds):
OLD pages 0-9 = fully populated real maps (both-bytes ~1789/page,
exactly 256 zero words/page = blank stripes). NEW = same maps but
scatter-corrupted: ~150-470 words/page missing their LOW byte,
~110-256 missing their HIGH byte, blank stripes filled with junk.
So the per-round source table fix WORKS (maps load); the corruption
is in the write path: the two RLE passes (patched word-write pass-1
at 0x16C0 base 0x852000, odd-byte pass-2 at 0x16E0 base 0x852001)
are landing out of phase with each other in the NEW build only.
NEXT PROBES (local rig):
1. Dump staging at TITLE in both builds — does corruption pre-date
   the round-1 load (leftover from an earlier loader) or arrive with
   it?
2. wp-trace pass-1 vs pass-2 write ADDRESSES for one page in the new
   build (wpset on one page + PC log) — mis-phase will show as
   address skew between the passes.
3. Suspect list: something the rebase changed in the RLE callers'
   flow (bank word consumed differently?), or a second loader
   touching the same pages between passes.

## SKY-PATCH CLASS SOLVED: harvested-value byte collisions in packed streams

The pass-2 RLE writer control run nailed it (old wrote the cell via
the zero-RUN path at 0x16F8, new via the LITERAL path at 0x1704 =
stream desync), and the report cross-check found the cause: harvested
handler VALUES 0x102/0x106 byte-collide inside the compressed map
streams — the occurrence pass sprayed 79 +0x900000 corruptions across
0x29E00-0x3E3xx. Occurrence matching now bounded below the asset
region (<0x28000, where all real handler tables live). Scene-locked
staging diff: ~14,000 corrupt words -> 1 (single word, page 9,
dump-instant race suspect). MAME visuals: rise + gameplay match the
main build.
LESSON (toolkit): value-occurrence patching MUST be region-bounded or
context-gated — small harvested values WILL collide inside packed
data. The full diagnostic chain that solved this: scene-locked
staging diff -> byte-pattern census -> single-cell watchpoint with
old-build CONTROL run -> report cross-check.

## PLAYER-INVISIBLE REGRESSION: the @(36) sprite-definition stores

Dropping the memory-dst immediate rule took the `movel #ptr,%fp@(36)`
family with it — object field +36 is the SPRITE DEFINITION pointer,
consumed as DATA (movea (36,A6)) with no thunk at the consumer. Rule
restored PRECISELY: register-indirect destinations only (%aN@(k) /
%fp@(k)); ABSOLUTE destinations stay excluded (those were the BCD
score-award mailboxes). 2243 static rebases.
Field state per Mike: sky fixed ("MUCH better"), gravestone wiggle
correct, frames still dropping on ares (53-60 VPS — queued with the
step-2 speed work).

## OPEN ITEMS AFTER THE SKY/PLAYER FIX ROUND (field: "MUCH better")

Confirmed fixed on ares: sky/tilemaps, gravestone rise animation,
scores, spawn params, player visibility (in MAME).
OPEN, in priority order:
1. ARES-ONLY: P1 spawns at/near the P2 X (far right, half off-screen);
   MAME object state is byte-identical old-vs-new (slot dumps).
   Poison covers ROM reads, so the divergence domain is: (a) reads of
   0x000-0x0FF (EXCLUDED from rebasing; MAME models the adapter vector
   area as writable-hint-vector + bios, ares differs) — census the
   game's low-vector reads next; or (b) window-timing vs the game's
   sprite-list/spawn sequence. 
2. White skyline rectangles (tree-row tiles) — MAME-reproducible,
   wp-harness next.
3. Choppiness/frame drops on ares (53-60 VPS) — belongs to step-2
   (pull compose out of windows) which is still the end-goal.
4. Round-2+ garble sweep; Zeus sprite edge dropouts.

## LOW-VECTOR CONSTANT READS (the ares-only P1-position suspect)

Census found 4 game references below 0x100 — the killers:
0x14930/0x1493C `addal 0x0,%a4` = the game ADDS vector[0]
(0xFFFFFF00 = -0x100, a size-optimized constant) to an address
register. At RV=0, MAME and ares serve DIFFERENT adapter bytes at
address 0 -> ares-only pointer/position skew. Plus tstb 0x0 (flag
test of a vector byte) and oriw #0,0x0 (RMW). All four redirected to
the high copy's authentic arcade vector bytes (operand 0 ->
0x900000). TOOLKIT: the rebase census must include [0x000,0x100) —
vector-table-as-constants is a real 68K idiom.

## ROUND-2 DISPATCHER FIX + CLEAN 20K SWEEP

The round-2 attract crash chain: the two-level dispatcher's WORD
index table at 0x6DC0 pair-reads as 0x00030004 — a forged "valid ROM
pointer" that passed every classifier (three different sweeps
re-swallowed it). Resolution: REBASE_EXCLUDE hard list applied as a
final revert pass over game_high.bin (heuristics cannot reject forged
pairs; exclusion is the only safe answer — same philosophy as
DATA_EXCLUDE in the original patcher).
RESULT: 20,000-frame attract sweep (all round demos) with ZERO
address-space escapes under poison. Ares build includes the
low-vector constant fix (P1-position candidate) — awaiting field
verdict on: P1 spawn position, round-2+ demo visuals, choppiness.

## FIELD TRIAGE: remaining round-1 artifacts are ALLOCATOR-CAPACITY class

Mike's "top half eats player sprite" = the demo player's LEGS sprite
color overflowing to the shared CRAM pair (multi-part S16 characters;
torso color owns a pair, legs color over budget -> flat red). Same
family as the white skyline rectangles (tile-group overflow sharing).
NOT rebase bugs — the pointer surgery is stable (20k-frame sweeps
clean). Allocator follow-ups (bounded, later): nearest-color overflow
fallback instead of last-assigned; revisit the 6..10 pair budget
clamp for sprite-heavy scenes.

DECISION: pivot to UNPAIR STEP 2 — pull sprite/tile compose out of
the pause windows (cart is SH-2-readable at any time under RV=0).
This is the payoff: 68K pause ~18ms -> ~4-5ms per cycle, the
choppiness/speed fix Mike has asked for throughout. Plan sketch:
- Sprite compose moves from window exts to the CONCURRENT phase
  (SPR_SNAP + spr maps already SDRAM; cart sprite reads now legal
  anytime). Same row-following schedule, just post-ack.
- Tile thirds unchanged (already concurrent).
- Windows shrink to: snapshot (SPR_SNAP/pages/regs/CRAM) + blit
  slices + cache fills (fills can ALSO go concurrent now — no RV
  constraint on cart tile reads).
- Cycle can then shorten (2-slice/112-row retest is BACK on the
  table since blits get the whole vblank to themselves).

## UNPAIR STEP 2 LANDED: full concurrent compose — 68K pause 3.4ms/cycle

Windows now hold ONLY: vblank slice blits + (W0) staging snapshot
(regs, pages, SPR_SNAP) + apply_cram. ALL composition — tiles AND
sprites/cat1/text — runs concurrent with the game (RV=0 makes cart
art readable anytime). Per-band schedule after Wk's ack: each CPU
composes tiles then sprites on ITS OWN rows of band R(k) (row splits
= no cross-CPU ordering). Leftover machinery + SYNC[3] handshake
deleted; fills concurrent (master tail); build_maps unchanged at k2.
MAME gameplay: total in-window time 3.38ms/cycle (was ~11-12), no
stalls, misses ~3. Projected ares tax ~10% (was 37%).
Notes from the round:
- Master must cache_purge before its post-ack band work (pages/maps
  changed in-window).
- The "yellow ghosts" that appeared are AUTHENTIC: the arcade's
  grave-emergence effect sprites (FPGA ground truth shows the same
  yellow block) — step 2's budget renders sprites older builds
  dropped.
- Sprite color 0x3F (shadow/darken) sprites are SKIPPED for now
  (proper darkening is a residual); pair-zone eviction now spares
  live singles (group-14 text/pair-7 thrash).

## SNAPSHOT PHASE-SHIFT (step-2 follow-up, from Mike's 3-image report)

Step 2 exposed a sprite-list race: the game's own vint handler (which
rebuilds the sprite hardware list) runs AFTER our window in the
interrupt chain, so a W0 snapshot read a stale/mid-rebuild list once
the game ran at speed — vanishing Zeus, sprites cut at band seams.
Snapshot now taken at W1 (post game-vint, list complete); band
schedule phase-shifted: W1 composes R0, W2 -> R1, W0 -> R2 (each
band still done 2+ vints before its rows ship). build_maps rides
region R2's tail (before the next W1 snapshot).
REMAINING known-visual, queued:
- Slice-seam temporal tearing on fast movers: inherent to progressive
  shipping at full game speed; mitigate with the 30Hz/2-slice retest,
  true fix = whole-frame ship (double sbuf) someday.
- Title-screen purple field / unfinished background: allocator drift
  (suspect the eviction age-gate) — allocator polish pass next.
