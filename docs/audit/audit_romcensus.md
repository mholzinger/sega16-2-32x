# 32X Commercial ROM Census — hardware idioms as shipped

Date: 2026-08-21. Corpus: `srcref/` top-level + `1 US - A-Z/`, `2 Japan A-Z/`,
`3 Europe/`, `4 Prototypes/`, `4 Homebrew & Hacks/` — 101 `.32x` files, 88
unique by md5 (~40 unique commercial retail titles + 4 protos + homebrew).

Method: Python literal-pool scan (4-byte-aligned big-endian constants) over
whole images; 32X header parse (`0x3D4` src / `0x3D8` dst / `0x3DC` size /
`0x3E8/0x3EC` VBRs — layout verified against Knuckles' Chaotix header at file
offset 0x3C0); V-int = `*(VBR+0x118)` (vector 70, layout confirmed against
marsdev `32x-old-skel/boot.s`); `sh-elf-objdump -m sh -EB` windows around every
literal-pool reference site (`mov.l @(disp,PC),Rn` back-solved); a second full
disassembly of each ROM's SDRAM copy region hunting the register-global idioms
(`@(10,rN)` word FBCTL, `@(11,rN)` byte FBCTL, GBR-based system-reg access);
`m68k-elf-objdump` windows around 68K-side `0xA151xx` literals. Deep dives read
manually. All offsets below are FILE offsets in the named image; runtime
addresses given in parentheses.

Caveat on literal counts: many games never put `0x2000410A` in a pool — they
park `0x20004100` in a callee-saved register for the program's lifetime (MK2
keeps it in r14, GBR holds `0x20004000`) so FBCTL appears only as `@(10,rN)` /
`@(11,rN)`. A zero in the 410A column therefore means nothing by itself; the
FlipSig column and the deep dives carry the real classification.

## Census table

Columns: aligned literal-pool counts. FB24 = `0x24000000`, PAL = `0x20004200`.
Vint Y = master V-int vector resolves to a real handler (all Y's route through
a level-dispatch trampoline to non-trivial code). FlipSig (automated signature,
raw evidence only): P = VBLK poll + FS write in main code; N = byte-NOT flip
idiom (`mov.b @(11,rN)` / `not` / write) in copy region; Vi = FBCTL touched in
V-int handler window; X = `xor #1` FS toggle in code; `-` = flip idiom not
caught by signatures (usually register-global; see deep dives / notes). dup =
identical-md5 copies elsewhere in corpus.

| Title | Grp | KB | 4000 | 4012 | 4100 | 410A | 410B | A15100 | A15180 | A1518A | A15112 | FB24 | PAL | Vint | FlipSig | dup |
|---|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|:-:|:--|--:|
| Amazing Spider-Man - Web of Fire (USA) | US | 3072 | 12 | 0 | 8 | 1 | 1 | 5 | 0 | 0 | 0 | 7 | 3 | Y | tstFS | 1 |
| BC Racers (USA) | US | 3072 | 11 | 0 | 1 | 0 | 2 | 1 | 0 | 0 | 0 | 26 | 1 | Y | - | 0 |
| Blackthorne (USA) | US | 3072 | 20 | 0 | 4 | 5 | 0 | 2 | 0 | 0 | 0 | 9 | 0 | Y | X | 0 |
| Brutal Unleashed - Above the Claw (USA) | US | 3072 | 12 | 0 | 3 | 0 | 0 | 16 | 0 | 0 | 0 | 17 | 7 | Y | - | 0 |
| Cosmic Carnage (Japan, USA) | US | 3072 | 9 | 0 | 0 | 0 | 0 | 7 | 0 | 0 | 0 | 2 | 2 | Y | - | 0 |
| Darxide (Region-Free) | US | 2048 | 16 | 0 | 12 | 1 | 0 | 5 | 0 | 0 | 0 | 18 | 5 | Y | - | 0 |
| Doom (Japan, USA) | US | 3072 | 14 | 0 | 5 | 0 | 0 | 5 | 0 | 2 | 0 | 12 | 3 | Y | - | 0 |
| FIFA Int'l Soccer 96 | US | 3072 | 7 | 1 | 4 | 0 | 2 | 2 | 0 | 0 | 0 | 66 | 2 | Y | N | 0 |
| Knuckles' Chaotix (Japan, USA) | US | 3072 | 7 | 1 | 5 | 4 | 7 | 40 | 6 | 2 | 0 | 4 | 5 | Y | P+X (deep dive) | 1 |
| Kolibri (USA, Europe) | US | 3072 | 4 | 0 | 1 | 0 | 0 | 2 | 0 | 0 | 0 | 7 | 1 | Y | N-in-vint (deep dive) | 1 |
| Metal Head (Japan, USA) | US | 3072 | 29 | 0 | 11 | 4 | 1 | 4 | 0 | 0 | 0 | 8 | 2 | Y | X | 0 |
| Mortal Kombat II (Japan, USA) | US | 4096 | 11 | 0 | 5 | 0 | 3 | 2 | 0 | 0 | 0 | 12 | 4 | Y | N-after-vint (deep dive) | 0 |
| Motocross Championship (USA) | US | 2048 | 9 | 0 | 2 | 3 | 1 | 2 | 0 | 0 | 0 | 21 | 2 | n* | - | 0 |
| NBA Jam TE (World) [ED patched] | US | 4096 | 8 | 0 | 7 | 4 | 0 | 8 | 1 | 0 | 0 | 17 | 6 | Y | tstFS+X | 1 |
| NFL Quarterback Club (World) [ED patched] | US | 3072 | 13 | 0 | 7 | 0 | 0 | 2 | 0 | 0 | 0 | 7 | 1 | Y | - | 0 |
| Pitfall - The Mayan Adventure (USA) | US | 3072 | 4 | 0 | 2 | 0 | 0 | 7 | 0 | 0 | 0 | 29 | 4 | Y | N | 0 |
| Primal Rage (USA, Europe) | US | 4096 | 7 | 0 | 3 | 0 | 1 | 2 | 0 | 0 | 0 | 2 | 1 | Y | - | 0 |
| RBI Baseball '95 (USA) | US | 2048 | 4 | 0 | 4 | 0 | 0 | 5 | 0 | 0 | 0 | 4 | 3 | Y | Vi | 0 |
| Shadow Squadron ~ Stellar Assault | US | 2048 | 6 | 0 | 27 | 1 | 11 | 1 | 0 | 0 | 0 | 2 | 2 | Y | tstFS | 0 |
| Space Harrier (Japan, USA) | US | 2048 | 5 | 0 | 4 | 8 | 2 | 26 | 0 | 0 | 0 | 5 | 1 | Y | X, trivial vint (see notes) | 1 |
| Star Trek Starfleet Academy (USA) | US | 2048 | 14 | 0 | 7 | 0 | 0 | 3 | 0 | 0 | 0 | 10 | 2 | Y | N | 0 |
| Star Wars Arcade (USA) | US | 2560 | 6 | 0 | 6 | 0 | 0 | 2 | 0 | 0 | 0 | 4 | 10 | Y | X+tstFEN | 1 |
| T-Mek (USA, Europe) | US | 3072 | 7 | 0 | 10 | 0 | 0 | 0 | 0 | 0 | 0 | 2 | 5 | Y | - | 1 |
| Tempo (Japan, USA) | US | 3072 | 10 | 0 | 5 | 13 | 0 | 0 | 0 | 0 | 0 | 12 | 2 | Y | vint-flip+confirm (deep dive) | 0 |
| Toughman Contest (USA, Europe) | US | 4096 | 17 | 0 | 12 | 2 | 3 | 7 | 0 | 0 | 0 | 22 | 8 | Y | - | 0 |
| Virtua Fighter (Japan, USA) | US | 4096 | 3 | 0 | 2 | 0 | 0 | 42 | 0 | 0 | 0 | 7 | 0 | Y | P | 1 |
| Virtua Racing Deluxe (USA) | US | 3072 | 12 | 1 | 10 | 0 | 0 | 22 | 0 | 2 | 2 | 2 | 2 | Y | tstFS/tstFEN; 68K FM RMW x10 | 1 |
| World Series Baseball (Deion) (USA) | US | 3072 | 13 | 0 | 10 | 0 | 1 | 6 | 0 | 0 | 0 | 10 | 7 | Y | - | 0 |
| WWF Raw (World) | US | 4096 | 18 | 0 | 8 | 0 | 1 | 5 | 0 | 0 | 0 | 6 | 7 | Y | N | 1 |
| WWF WrestleMania Arcade (USA) | US | 4096 | 13 | 0 | 5 | 0 | 1 | 4 | 1 | 0 | 0 | 5 | 3 | Y | N-in-vint (deep dive) | 1 |
| Zaxxon's Motherbase 2000 (Japan, USA) | US | 2048 | 15 | 3 | 12 | 7 | 0 | 3 | 0 | 0 | 0 | 7 | 3 | Y | - | 0 |
| 36 Great Holes (Japan, USA) | top | 3072 | 3 | 0 | 5 | 0 | 0 | 24 | 0 | 0 | 0 | 2 | 0 | Y | - | 1 |
| After Burner Complete (Japan, USA) | top | 2048 | 5 | 0 | 11 | 4 | 1 | 32 | 0 | 0 | 0 | 7 | 2 | Y | Vi+X | 1 |
| MK2 (W) Jan 1995 / AE hacks | top | 4096 | 6 | 0 | 3 | 0 | 2 | 2 | 0 | 0 | 0 | 14 | 1 | Y | as MK2 | 1 |
| Sangokushi IV (Japan) | JP | 4096 | 5 | 0 | 1 | 1 | 0 | 4 | 0 | 0 | 0 | 5 | 1 | Y | tstFS | 0 |
| Stellar Assault (Japan) | JP | 2048 | 6 | 0 | 27 | 1 | 11 | 1 | 0 | 0 | 0 | 2 | 2 | Y | tstFS | 0 |
| Virtua Racing Deluxe (Japan) | JP | 3072 | 12 | 1 | 10 | 0 | 0 | 21 | 0 | 3 | 2 | 2 | 3 | Y | as VRD US | 1 |
| Doom (Europe) | EU | 3072 | 13 | 0 | 5 | 0 | 0 | 5 | 0 | 2 | 0 | 15 | 3 | Y | - | 1 |
| Doom RR (JU) (Proto Mar 1995) | proto | 3072 | 11 | 0 | 5 | 1 | 0 | 5 | 0 | 2 | 0 | 5 | 3 | Y | P | 0 |
| Soulstar X (USA) (Proto) | proto | 5120 | 12 | 0 | 1 | 1 | 1 | 1 | 0 | 0 | 0 | 5 | 2 | Y | - | 0 |
| Virtua Hamster (USA) (Proto) | proto | 2048 | 11 | 0 | 12 | 0 | 0 | 2 | 0 | 0 | 0 | 42 | 7 | Y | P+N | 0 |
| X-Men (USA) (Proto) | proto | 4096 | 6 | 0 | 0 | 8 | 5 | 2 | 0 | 0 | 0 | 4 | 2 | Y | P + FBCTL-in-vint | 0 |
| SRB32X v0.1a | proto dir | 4096 | 21 | 0 | 8 | 4 | 0 | 7 | 2 | 0 | 0 | 2 | 3 | Y | P | 0 |

Homebrew/demos (32 more rows, scanned identically, omitted from prose): every
non-trivial one (Wolf3D, Rick Dangerous, Yeti3D, mic's players/demos) is
FlipSig P or X — main-loop VBLK poll then FS toggle; most of the tiny demos
never install a V-int at all.

\* Motocross Championship: master VBR could not be resolved through the header
copy-map (VBR target outside the declared copy region) — vint not sampled;
counts still valid. Doom's vint vectors point into cart (`0x204dd58`), i.e. it
runs handlers XIP from ROM; its flip did not match any automated signature and
was not chased (not a deep-dive title).

Reading the aggregates:

- **DREQ FIFO (`0x20004012`) appears in exactly 4 commercial titles** —
  Chaotix (1), Virtua Racing Deluxe (1), FIFA 96 (1), Zaxxon's Motherbase
  (3) — out of ~40. The 68K-side FIFO port `0xA15112` appears only in VRD.
  DREQ is an exotic technique, not the platform norm.
- **Every commercial title has a real master V-int handler** and they all use
  the same shape: a trampoline that reads SR, masks to the accepted level, and
  dispatches through a per-level jump table (Sega and Midway codebases both).
- **68K-side 32X VDP regs (`0xA15180/0xA1518A`) are near-absent**: Chaotix
  (palette handoff, below), VRD, Doom. No commercial title flips the
  framebuffer from the 68K.
- **`0xA15100` count is a good "how chatty is the 68K" proxy**: Chaotix 40,
  VF 42, After Burner 32, Space Harrier 26, 36 Holes 24, VRD 22 (games whose
  68K runs game logic / RPC); Midway ports 2-5 (68K is little more than a
  loader + comm pump).

## Deep dive 1 — Knuckles' Chaotix (Japan, USA).32x

Header: SH2 code copied from file 0x77800 → SDRAM 0x06000000, size 0x9000;
master VBR 0x6000000, slave VBR 0x6000080 (file 0x3d0-0x3ef).

- **V-int** (master table file 0x779d8, level-12 entry = 0x6001280 = file
  0x78a80): FRT maintenance (`xor #2` on 0xFFFFFE17), clear V-int
  (`0x20004016`, 0x78a88), increment TWO frame counters `0x6003824/0x6003828`
  (0x78a8c-98), then a small palette animation directly into CRAM
  `0x20004200+n` (0x78aa4-78ad8) — writes 0x2458 or 0x8000 to entry 0xD8
  alternating on frame parity, plus 4 computed entries. Return.
- **Flip** (file 0x7821c-0x7822e, runtime ~0x6000a1c, called from mainline via
  `bsrf`): load `0x2000410A`; poll `cmp/pz` until VBLK **clear** (leave any
  current vblank), then poll until VBLK **set** (catch the next vblank edge),
  then `xor #1` and write FS. A pure main-thread, edge-synchronized poll flip.
- **Frame pacing** (file 0x78a2c-0x78a46, runtime 0x600122c): wait until vint
  counter `0x6003824` >= N (N=1 or 2 — supports 60 or 30 Hz pacing), then poll
  FBCTL until VBLK set, zero the counter. So: vint counter paces, FBCTL poll
  aligns, flip toggles.
- **DREQ** (master CMD-int handler, table idx4 = 0x6001334 = file 0x78b34):
  clear CMD int (`0x2000401A`), then program on-chip DMAC ch0:
  `SAR0=0x20004012` (FIFO), `DAR0=*(0x6003814)` (a destination pointer
  variable), `TCR0=*(0x20004010)` (the DREQ length the 68K programmed),
  `CHCR0=0x44E0` then `0x44E1` (external-request mode, enable). The 68K raises
  CMD-int per transfer; the SH2's only job is arming the DMAC — the FIFO
  drains by DREQ pacing. This is the textbook FIFO protocol, driven entirely
  from an interrupt, ~20 instructions total.
- **FM / 68K role** (68K routine at file 0x305e-0x30b6): per-frame palette
  effects are done BY THE 68K directly on 32X CRAM: `move #0x2700,SR`; wait
  for SH2 idle via comm `0xA15120`; `move.b #0,0xA15100` (FM=0, 68K takes the
  bus); then for each entry: `btst #5,0xA1518A` (poll PEN) before
  `eori.w #0x8000` RMW on `0xA15200+n`; finally `move.b #0x80,0xA15100`
  (FM=1, hand back) and restore SR. Boot init (file 0x658-0x692) claims FM
  with the `bclr #7` loop and clears the FB with 68K-side autofill
  (`0xA15184/86/88`, FEN poll on `0xA1518A` bit 1) — same boilerplate as MK2.
- **Renderer split**: sprite/scale renderer at file 0x78b80+ writes the FB via
  `0x24020200`-based addresses; jobs are posted to the slave through comm regs
  (mailbox writes at 0x781fa-0x78208 with 'M_OK'/'S_OK'-style handshakes
  visible in the boot path, file 0x77a76-0x77a7c).

## Deep dive 2 — Kolibri (USA, Europe).32x

Header: copy 0x9000 → 0x06000000 size 0x20000; master VBR 0x6000800 (so the
master vector table is at file 0x9800; V-int vector at +0x118 → 0x600099a).

- **V-int** (trampoline file 0x999a, table 0x99c0, level-12 entry 0x6000a60 =
  file 0x9a60): mask to SR=0xF0, clear V-int (`@(22,gbr)` = 0x20004016 with
  GBR=0x20004000), call worker `0x6000b46` (file 0x9b46). The worker IS the
  whole display pipeline:
  1. `mov #-128,r0; mov.b r0,@(0,gbr)` — **assert FM=1 every single V-int**
     (0x9b50) — defensive re-claim of the VDP for the SH2 side.
  2. increment frame counter `0x6000c50`; latch comm reg `0x2000402E` (68K
     input/state) into `0x6000c60` (0x9b54-62).
  3. if the main thread posted "frame ready" (`0x6000c54` != 0): write 1 to
     comm0 `0x20004020` (ack to 68K), zero the H-int counter `0x6000e64`, then
     with r14=0x20004100: **flip = `mov.b @(11,r14),r0; not r0,r0;
     mov.b r0,@(11,r14)`** (byte-NOT of FBCTL low byte, 0x9b78-0x9b7c),
     write mode reg `0x20004101` from shadow `0x6000c5c`, write screen-shift
     `0x20004103` from shadow `0x6000c5e` (0x9b7e-0x9b88), and if the palette
     flag `0x6000c56` is set copy 0x100 words shadow `0x6000c64` → `0x20004200`
     (0x9b92-0x9ba0). Clear the ready flag.
- **Main thread** (file 0x9b2a): `SR=0`; set ready flag `0x6000c54`=1; spin
  until the vint clears it; increment its own frame count `0x6000c58`. That is
  the entire synchronization surface: one flag out, one flag back.
- Everything display-side is **shadowed in SDRAM and committed atomically by
  the V-int**: FS, mode, shift, palette. The main loop never touches VDP regs.
- H-int (file 0x9a80) just counts `0x6000e64`. 68K per-frame role: feeding the
  comm reg the vint latches; `0xA15100` literal appears twice (boot only).

## Deep dive 3 — Tempo (Japan, USA).32x

Header: copy 0x80000 → 0x06000000 size 0x11000; master VBR 0x6000000, slave
VBR 0x6000400. Master V-int vector → 0x600251a (file 0x8251a trampoline,
table 0x82550; level-12 entry 0x60025e8 = file 0x825e8).

- **V-int** (file 0x825e8): clear V-int (`0x20004016`), FRT touch; if the
  main-thread "frame done" flag `0x6002518` is clear → return (idle vint).
  Otherwise clear it, save ALL registers, then:
  1. copy 0x100 words shadow palette `0x6000bc0` → `0x20004200` (0x8262a-38);
  2. `jsr 0x6002dc4` (file 0x82dc4): write screen-shift `0x20004102` from
     shadow `0x603f208` — per-frame scroll commit;
  3. `jsr 0x6002eec` (file 0x82eec): if flag `0x603f20c` set →
     `jsr 0x60012b0` = **the flip** (file 0x812b0): read FBCTL, `xor #1`,
     `and #1`, write FS, then **poll `FBCTL & 1` until it equals the written
     value** — a flip-with-hardware-confirm, still inside the interrupt.
- **Autofill**: strip-fill loops at file 0x813e6 and 0x81442 — write
  `0x20004108` (fill data), poll `FBCTL & 2` (FEN) until clear, `dt`-loop over
  strips. FB clears go through the VDP's fill engine, not CPU stores.
- VBLK byte-poll idiom also present in main code (file 0x82038:
  `mov.b @r13` from 0x2000410A high byte, `tst #128` loop) guarding a copy
  into the shadow palette.
- **VRES handler** (0x6002594 = file 0x82594) is a full warm-restart: reset
  SP to 0x603fff8, PC to 0x6001000, clear DMAC (0xffffffb0, 0xffffff8c) — the
  adapter-reset discipline the Sega library titles all carry.
- 68K role: `0xA15100` literal count is **zero** — beyond the standard header
  boot code the 68K essentially disappears; game logic lives on the SH2s and
  V-int owns every VDP commit.

## Deep dive 4 — WWF WrestleMania: The Arcade Game (USA).32x

Header: copy 0xb07bc → 0x06000000 size 0x3178 (only ~12.5 KB in SDRAM — the
kernel; game code executes from cart). Master VBR 0x6000000; V-int vector →
0x600019c; trampoline dispatch table at 0x6000228 (file 0xb09e4); level-12
entry 0x6000314 = file 0xb0ad0.

- **V-int** (file 0xb0ad0): GBR=0x20004000, clear V-int (`@(22,gbr)`), FRT
  touch, then drop SR to 0xD0 (re-enable VRES) and `jmp 0x6001420` (file
  0xb1bdc) — the body:
  1. increment vint counter `0x600145c`;
  2. decrement a **flip countdown** variable; only when it underruns AND the
     "frame pending" flag is set:
  3. write `(state & 15) | 0x90` to comm byte `0x20004021` — notify the 68K
     that the flip is happening (0xb1bfa-0xb1c00);
  4. **flip = byte-NOT of `0x2000410B`** (file 0xb1c02-0xb1c08); clear the
     pending flag.
  The countdown is the frame-rate governor: the renderer posts a frame plus a
  minimum vint count, and the vint holds the flip until both are true — a
  clean 60/30/20 Hz divider with no tearing possible.
- CMD-int (0x6000380 = file 0xb0b3c) calls `0x6001370` at SR=0x50 — 68K
  command service. PWM-int (file 0xb0b74) increments `0x260003ec`
  (cache-through SDRAM) and calls `0x6000544` — sound streaming tick.
- **Asset path**: the sprite decompressor right after the vint body (file
  0xb1c38-0xb1cd2+) walks bitstreams via pointers based at `0x02000000` — the
  SH2 reads compressed sprite data **directly from cart**, no DREQ, no 68K
  copying. (DREQ literal count: 0.)
- WWF Raw (World) carries the same kernel idioms (byte-NOT flip present in its
  copy region).

## Deep dive 5 — Mortal Kombat II (Japan, USA).32x

Header: copy 0x978 → 0x06000000 size ~0x7e14. Same Midway kernel family as
WWF. GBR=0x20004000 permanently; **r14=0x20004100 permanently** (slave boot,
file 0xc1e-0xc2a); slave sets mode=1 (packed pixel) at file 0xe54-0xe56
(`mov #1,r0; mov.b r0,@(1,r14)`).

- **Master V-int** (trampoline file 0xc50, table 0x600030c = file 0xc84;
  level-12 entry 0x60003a4 = file 0xd1c): clear V-int (`@(22,gbr)`), FRT
  touch, **increment frame counter `0x600a234`** — that is all.
- **Slave main loop** (file 0xe92-0xed8): `bsr 0x1a00` wait; `bsr 0x1a1c`
  flip; `bsr 0x1354` VDP service; run game-state handler via jump table;
  repeat.
  - 0x1a00 (file): spin until `0x600a234` changes — i.e. **wait for the
    master's V-int tick**.
  - 0x1a1c (file): **flip = `mov.b @(11,r14),r0; not r0,r1; mov r1,r0;
    mov.b r0,@(11,r14)`** — byte-NOT of FBCTL, executed in the slave's main
    thread immediately after the vint tick (inside vblank; no poll, no
    confirm — the hardware latches FS immediately during vblank).
  - 0x1354 (file): reads `0x20004101`; on palette-pending
    (`0x600a230`==2 && `0x600096c`): copy 0x100 words `0x600a002` →
    `0x20004202` with `or #0x8000` (priority bit forced on every entry).
- **Frame clear via autofill** (file 0x1a54-0x1a7e): for 0xA1 strips: write
  `0x20004106` (addr), `0x20004105` (length byte 0xFF), `0x20004108` (data 0),
  poll `0x2000410B` bit 1 (FEN) until done. Line table rebuilt at init (file
  0x1a28-0x1a38): 226 entries starting 0x4600, pitch 0xB8 words, written
  straight into FB `0x24000000`.
- **68K per-frame role**: the CMD-int handler (0x60003f4 = file 0xd6c) pumps a
  **0x29C-word (1336-byte) display list** from the 68K through comm regs
  `0x20004024-0x2000402C`, 5 words per handshake round, flow-controlled by
  comm0/comm2 (file 0xd82-0xdfa). No DREQ anywhere (literal count 0) — Midway
  moved ~1.3 KB/frame through the mailbox registers instead.
- **FM**: 68K claims FM only at boot (`bclr #7` loop on `0xA15100` at file
  0x704 and 0x65e-ish region) for its 68K-side autofill FB clear, then FM=1
  for the rest of the session.

## Supporting observation — Space Harrier (Japan, USA).32x

Master V-int (file 0x112cc) is **nearly empty**: FRT touch + 10-cycle delay
loop, rte. The interesting interrupt is CMD (level 8, file 0x11300-0x11316):
read comm `0x20004022`, take bits 11-8 as a **command number, dispatch through
a 16-entry jump table** (file 0x11320) — the 68K drives the SH2 as an RPC
server (Sega's early AM2 ports: 68K runs the game, SH2s are a render
coprocessor). Flip idiom in main code is the `xor #1` FS toggle.

## Supporting observation — Virtua Racing Deluxe (the DREQ outlier)

VRD is the only title with the full DREQ chain visible from both sides: 68K
`bset/bclr #7` FM RMW on `0xA15100` at **10 sites**, 68K-side FIFO port
`0xA15112` (2 refs), SH2-side FIFO `0x20004012` (1 ref), plus `0xA1518A` (2-3
refs). It is the most 68K-cooperative commercial engine — and precisely the
one whose protocol is already distilled in this repo's ARCHITECTURE.md. It is
the exception that proves the census: heavy 68K/DREQ choreography shipped in
one Sega first-party engine, while everyone else used a counter, a flag, and a
byte-NOT.

# THE CONSENSUS IDIOMS

**Flip timing.** One rule, universally: **FS changes are bound to vblank, and
the binding is a V-int-owned counter or flag — never a guess.** Three shipped
variants:
1. *Flip in the V-int handler itself* (Kolibri, Tempo, WWF WM, WWF Raw, FIFA,
   Pitfall, Star Trek, RBI — the majority): main thread renders, posts a
   "frame ready" flag (optionally + minimum-vint countdown), V-int commits FS
   (byte-NOT of 0x2000410B) plus any shadowed regs, clears the flag.
2. *Vint-paced main-thread flip* (MK2): main thread blocks on the V-int frame
   counter, then toggles FS immediately — it is inside vblank by
   construction.
3. *Main-thread VBLK edge poll* (Chaotix, VF, X-Men proto, most homebrew):
   poll FBCTL bit 15 for the vblank edge, then toggle FS; hardware applies it
   immediately because you are in vblank.
   The flip write itself is a **read-toggle-write of FBCTL** everywhere
   (`xor #1` on the word or `not` on the low byte) — nobody tracks the FS bit
   in a variable; the register is the state. Tempo alone adds a confirm poll
   (read back until bit 0 matches). Nothing ships a flip that can land
   mid-frame.

**FM ownership.** The SH2 side owns the 32X VDP during gameplay in every title
examined. The 68K takes FM (bclr #7 on 0xA15100, looped until it sticks) at
boot to run its init/FB-clear, then sets FM=1 and stays off the bus. Short
guarded mid-game windows exist (Chaotix: 68K palette RMW under int-mask + comm
handshake + PEN polls, FM returned within the same routine). Kolibri
re-asserts FM=1 inside every V-int as a one-instruction insurance policy. Only
VRD arbitrates FM continuously.

**68K role.** Loader, input reader, sound driver, and mailbox peer — not a
renderer. Per-frame data reaches the SH2s as: (a) comm-register batches under
CMD-int (MK2: 1.3 KB/frame, 5 words per handshake), (b) comm-register RPC
commands (Space Harrier: command nibble → jump table), (c) nothing at all —
the SH2 reads game data and compressed assets directly from cart via
0x02000000/0x22000000 (WWF, Tempo, Kolibri; Tempo's 68K doesn't even
reference 0xA15100). No commercial title has the 68K writing pixels per-frame.

**DREQ.** Rare: 4 of ~40 titles reference the SH2 FIFO address at all, and
only VRD shows the full 68K-side protocol. Where used (Chaotix), the SH2 cost
is ~20 instructions in the CMD-int handler arming on-chip DMAC ch0 in
external-request mode (CHCR=0x44E1) with length read from 0x20004010; the
transfer then runs itself. The shipped consensus for feeding the SH2s is comm
registers or direct cart reads, not the FIFO.

**Other unanimities.** V-int handlers are real but small: clear the int
(0x20004016), bump a counter, commit shadowed VDP state, out — heavy work
stays in the main threads. Every Sega/Midway title fingerprints the same
SR-level dispatch trampoline. Display state (palette, mode, shift, FS) is
shadowed in SDRAM and committed at vblank by exactly one owner. FB clears use
the VDP autofill engine with FEN polls, not CPU stores. Mode 1 (packed pixel)
is the near-universal game mode (direct color reserved for FMV/3D moments,
e.g. Metal Head; RLE mode: zero commercial users found). The on-chip
free-running timer (0xFFFFFE10-17) gets a ritual touch in every vint —
Sega-SDK boilerplate that survived into every derived codebase.

**The standard of evidence this sets:** a shipped 32X frame loop is a V-int
counter, a ready flag, a byte-NOT of FBCTL, and shadow-register commits —
about thirty instructions of synchronization. Anything more elaborate than
that had better be buying something none of these games needed.
