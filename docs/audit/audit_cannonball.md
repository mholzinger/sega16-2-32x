# Audit: cannonball-outrun-32x frame-pipeline architecture

Reference read exhaustively (all ~4,900 lines): `srcref/cannonball-outrun-32x/`
plus upstream `srcref/cannonball/` and `marsdev/examples/32x-skeleton/` for
contrast. All paths below are relative to
`/Users/mikeholzinger/src/sega16-2-32x/srcref/` unless absolute.

## 0. What this codebase actually is (scope caveat, read first)

This is NOT a port of the arcade 68K binary, and NOT a recompile of
Cannonball's C++. It is an original-work, from-scratch fixed-point C engine
that uses Cannonball as a *gameplay reference* and viciious's d32xr as the
*32X architectural reference* — stated outright in
`cannonball-outrun-32x/README.md:3-8`. Upstream Cannonball itself is Chris
White's C++ conversion of the OutRun 68K program running on SDL hosts
(`cannonball/src/main/engine/outrun.cpp:1-8`), which loads the original
arcade ROMs for assets (`cannonball/src/main/roms.hpp`,
`romloader.cpp`); the 32X port replaces even the assets with procedural
generation (`tools/gen_assets.py`). It also ships **no audio at all** (the
only PWM references are interrupt-clear writes in `src/sh2/crt0.s:184,293`)
and does not use the slave SH2 for work. So part of its simplicity is
reduced scope — but the frame-pipeline architecture is complete, boots in
MAME, and is the interesting part.

It does not build on marsdev; the toolchain is a from-source sh-elf GCC with
Chilly Willy's 32XDK as fallback (`Makefile:9-15`), and the crt0 follows
"established 32X homebrew practice (Chilly Willy)" (`src/sh2/crt0.s:8`).
marsdev's own `examples/32x-skeleton` uses the identical idioms (SH-2 writes
FBCTL and polls FS: `marsdev/examples/32x-skeleton/sh_src/mars.c:37-38`;
68K services a tiny COMM command loop:
`marsdev/examples/32x-skeleton/md_src/md_main.c:34-58`), confirming this is
THE standard 32X pattern, not one project's quirk.

## 1. Where game logic runs

**Everything runs on the master SH-2.** The whole engine (race state
machine, physics, road model, traffic, track) is portable fixed-point C
compiled for SH-2 (`Makefile:45-48` lists `src/engine/*.c` in `SH2_CSRC`),
called once per frame from the master main loop
(`src/sh2/main.c:87` `Race_Tick(pad)`). The 68K side is "deliberately tiny;
all game logic lives on the SH2s" (`src/m68k/md_main.c:7`). Nothing arcade
was recompiled or emulated: the engine "mirrors Cannonball's module split
(oroad/oferrari/traffic/…) but as a single deterministic 60 Hz, fixed-point
engine with zero platform dependencies" (`src/engine/engine.h:2-7`). The 32X
front-end consumes only the `race_t` snapshot plus a track query API
(`src/engine/engine.h:104-130`).

## 2. The frame loop

Exact per-frame sequence, master SH-2, `src/sh2/main.c:75-117`:

1. Spin until 68K handshake `COMM0/2 == "M_OK"` (re-sync after console
   reset) — `main.c:79-82`.
2. Read pads from COMM12|COMM14 — `main.c:84`.
3. `Race_Tick(pad)` — one 60 Hz logic step — `main.c:87`.
4. `render_scene(R)` — full-screen repaint into the back buffer —
   `main.c:91`.
5. `render_hud(R)` — `main.c:93`.
6. `hw_flip(1)` — request flip AND block until it takes — `main.c:95`.
7. Publish telemetry block to fixed SDRAM 0x0603E000 for emulator tests —
   `main.c:98-112`, section placed by `ld/mars32x.ld:66-71`.

**Flip mechanism** (`src/sh2/hw.c:38-49`): toggle a software bank index,
write it to `MARS_VDP_FBCTL` with FEN, then **poll the FS bit** until the
hardware latches the swap: `while ((MARS_VDP_FBCTL & MARS_VDP_FS) !=
s_active_screen);`. The FS latch only changes at vblank, so **the FS poll IS
the vblank wait** — there is no V-interrupt anywhere. All interrupts on both
SH-2s are masked forever; the vector tables are 124 copies of an `rte`
trampoline (`src/sh2/crt0.s:127` "interrupts masked at runtime; polling
design", `crt0.s:132-160`, SR interrupt bits set at `crt0.s:252-254` comment
"ints left disabled (polling design)"). The 60 Hz tick counter is literally
the flip counter (`hw.c:48` `s_frame_count++` inside `hw_wait_flip`;
exported as `hw_tick60()`, `hw.c:61`).

**Buffering:** hardware double buffer, nothing more. Drawing always targets
the non-displayed bank because the 0x04000000 FB window maps to it
automatically (`src/sh2/hw.c:3-4`). Both banks get a line table written once
at init (`hw.c:83-87` runs init_line_table + clear + flip twice); after
that the line table is never touched — no per-frame line-table scrolling
tricks.

**60 vs 30 Hz:** there is no split. Logic ticks once per rendered frame and
`Race_Dt() = 1/60` fixed (`src/engine/engine.h:150`, comment at
`engine.h:45,76,90`). Upstream Cannonball has a tick_frame 30/60/120
mechanism (`cannonball/src/main/main.cpp:132-144`,
`frontend/config.cpp:503-512` — "Original game ticks sprites at 30fps but
background scroll at 60fps"); the 32X port **dropped even that**. Honest
ambiguity: nothing in the repo measures achieved hardware frame rate; if
render+flip misses a vblank the game simply slows down (no catch-up, no
frame-skip machinery). The 60 Hz claim (`main.c:3`) is the design target.

## 3. FM ownership

**Set once by the master SH-2 at cold boot, never changed again.**
`src/sh2/crt0.s:249-254`: after the 68K handshake, the master writes 0x80
to the high byte of 0x20004000 — comment: "FM = framebuffer access to SH2,
ints left disabled (polling design)". `hw_video_init` redundantly ORs the
same bit (0x8000 of `MARS_SYS_INTMSK` = sysreg 0x4000) and then waits on it
(`src/sh2/hw.c:71-74`) — belt-and-braces for boot paths where the boot ROM
never granted it. That is the complete FM story: **zero per-frame
ping-pong, zero handshake protocol around it.** The 68K program never
touches 0xA15100 at all — its only adapter registers are the COMM ports at
0xA15120-2E (`src/m68k/md_main.c:23-30`) — and it **never touches the
framebuffer** (no 0x840000/0x860000 access anywhere in `md_main.c` or
`md_boot.s`). The only 0xA15100-region writes on the MD side are inside the
standard Sega security/startup blob executed once at boot
(`src/sh2/blob_security.s`, included at `crt0.s:116`).

## 4. The 68K's entire job

`src/m68k/md_main.c` — 122 lines total, and that includes the comment
block. Everything the MD side does:

- Clear its .bss in work RAM (`md_main.c:82-83`).
- Drop all COMM regs to zero so SH-2s notice a hot restart, wait ~1/10 s
  (`md_main.c:85-89`).
- Configure both pad ports TH-output (`md_main.c:92-93`).
- Raise handshakes: COMM0/2 = "M_OK", COMM4/6 = "S_OK" (`md_main.c:97-99`).
- Forever: read pad1/pad2 (3-step TH toggle, `md_main.c:50-72`), publish to
  COMM12/COMM14 (`md_main.c:108-109`); every 1024 loops re-raise the
  handshake words (`md_main.c:111-116`); every 32 loops publish a 32-bit
  heartbeat counter to COMM8/10 (`md_main.c:117-120`).

No music (no audio exists), no MD VDP graphics (the 68K never touches the
MD VDP after the security blob; the MD plane is left blank), no DREQ, no
interrupts (its Level-2/4/6 handlers are bare `rte`,
`src/m68k/md_boot.s:22-31`).

**Communication channels — the complete list, all COMM registers,
latest-value semantics, no acks after boot:**

| Channel | Direction | Protocol |
|---|---|---|
| COMM0/2 = "M_OK" | 68K→master | boot gate (`crt0.s:230-236`); re-checked by master every frame as liveness (`main.c:79-82`, `main.c:49-54`); re-raised by 68K every 1024 loops |
| COMM4/6 = "S_OK" | 68K→slave | boot gate only (`crt0.s:310-316`); master clears COMM4 first (`crt0.s:239-242`) |
| COMM6 | slave→anyone | free-running heartbeat, overwrites the "OK" of S_OK (`main.c:125-128`) — harmless because nothing re-reads S_OK after boot |
| COMM8/10 | 68K→tests | heartbeat frame counter (`md_main.c:117-120`); COMM8 also used once by the security module checksum gate during boot (`README.md:126-128`) |
| COMM12/14 | 68K→master | pad1/pad2, free-running publish, sampled once per frame (`main.c:84`) |
| SDRAM 0x2603FFF0 | master→HLE boot ROM | slave entry-point mailbox, one write at boot (`crt0.s:245-247,279`) |
| SDRAM 0x0603E000 telemetry | master→emulator harness | one-way, magic-framed, checksummed struct (`main.c:20-38,98-112`) — a test rig, not a runtime channel |

Note the explicit design decision recorded at `md_main.c:8-12`: earlier revs
synced pad publishing to the MD VDP vblank flag; it was unreliable across
emulator paths, so they made the publisher **free-running and pushed the
synchronization to the consumer** — the SH-2 samples once per rendered
frame, "so no synchronization is needed here." (Quirk: `main.c:84` ORs
COMM12|COMM14, merging both pads into one player's input.)

## 5. Slave SH-2

**Unused for work.** It boots through the same crt0, waits for "S_OK", then
runs `sh2_secondary`: an infinite heartbeat counter published to COMM6
(`src/sh2/main.c:120-130` — "idle heartbeat for now (offload planned, see
docs/PORTING.md)" — that doc does not exist in the tree). There is no work
split and therefore no synchronization to design. They shipped a
master-only renderer rather than build a slave protocol.

## 6. VDP mode and palette

- **Mode:** 256-color packed pixel (1 byte/pixel), 224 lines:
  `MARS_VDP_DISPMODE = MARS_224_LINES | MARS_VDP_MODE_256`
  (`src/sh2/hw.c:76`), shift 0 (`hw.c:77`). Not direct-color, not RLE.
- **Line table:** written once per bank at init: row j → word offset
  `j*160 + 0x100`, rows 224-255 all point at one blank line
  (`hw.c:11-27`).
- **CRAM:** the SH-2 writes all 256 entries **once at boot**, before the
  main loop, with plain CPU stores (`hw.c:90-96`, called at `main.c:64`).
  There are **zero runtime CRAM writes**. All per-stage color identity
  (road/grass/sky per 15 stages) is achieved by **palette-index
  indirection into one static 256-entry palette**: theme tables select
  which pre-loaded indices the renderer uses
  (`src/sh2/theme.c:23-45`, consumed per-frame at `src/sh2/render.c:174,
  232-254`). The palette is generated as a single global by
  `tools/gen_assets.py` (`g_palette[256]`, gen_assets.py:470-475).
  Consequence: no CRAM-timing protocol, no vblank-timed palette windows, no
  palette DMA — the entire "palette changes" problem is deleted by data
  layout.

## 7. Performance discipline

- **FB writes:** rendering is flat-color horizontal spans drawn with a
  long-word memset where aligned (`render.c:83-92` hline → memset;
  `src/sh2/libc_min.c:25-38` does 32-bit stores when aligned), i.e. they
  minimize FB transactions per pixel rather than avoiding the FB. Sprites
  are conditional byte stores, transparent-0 skipped
  (`src/sh2/sprites.c:30-34`) — which incidentally also sidesteps the 32X
  dropped-zero-byte-write FB quirk, though the code never mentions it.
  **No per-frame clear**: the scene guarantees full coverage (sky rows
  0..CY, road rows walked down to the horizon, and an explicit
  "horizon gap fill" pass for crest frames, `render.c:294-305`);
  `hw_clear_backbuffer` is called only at init (`hw.c:85`). The FB is
  **never read**.
- **Cache:** purge+enable on both SH-2s at boot (`crt0.s:199-201,256-259`,
  `crt0.s:306-308,322-324`). All rendering goes through the **cached**
  0x04000000 FB window (`32xhw.h:51`; SH7604 cache is write-through so
  stores reach the FB, and since the FB is never read there is no staleness
  hazard). Uncached mirrors are used only where another agent must see the
  bytes now: COMM regs (0x2000xxxx), diag byte 0x2603FFFC, boot mailbox
  0x2603FFF0 (`32xhw.h:11-23`, `main.c:41`, `crt0.s:268,279`).
- **SDRAM layout** (`ld/mars32x.ld:12-19,66-71`): code+rodata run **in
  place from ROM** at 0x02000000 (only .data/.bss are copied to SDRAM by
  crt0, `crt0.s:203-227`); SDRAM holds data/bss, two stacks at
  0x0603F000/0x0603F800, telemetry at 0x0603E000. No RAM-code overlays, no
  hand-managed regions beyond those four lines.
- **Where frame time goes / ALU discipline** (the README's "d32xr idioms",
  `README.md:48-55`): per-depth 1/z tables built once (`render.c:67-81`),
  zero divides in the z-walk and row loops, per-step colors/edges hoisted
  above the scanline loops (`render.c:222-229`), SEG_LEN=64 so all segment
  math is shifts (`engine.h:26-27`), sky ramp via fixed-point accumulator
  (`render.c:94-109`), HUD /10 via reciprocal multiply
  (`src/sh2/hud.c:82-92`), sprites pre-scaled into 4 variants picked by
  height so the blitter pays at most 2 divides per sprite and zero on the
  1:1 path (`sprites.c:16-41`, `sprites.c:65-75`), decor capped at 56 items
  (`render.c:47-48`) and pushed only for segments newly entered this frame
  — with a recorded regression where re-pushing all decor every frame
  caused growing slowdown (`render.c:163-168`).
- No profiling counters exist in the shipped code; the only instrumentation
  is boot-step liveness bytes (`DSTEP`, `main.c:40-44`) and the telemetry
  struct.

## 8. Everything they did NOT build (the indictment list)

Each item is an alternative a "serious" port might build; this codebase
demonstrably ships without it:

1. **No interrupts, on any CPU, ever.** No V-int, H-int, CMD-int, PWM-int;
   VBRs full of `rte` (`crt0.s:127-160`), 68K vectors bare `rte`
   (`md_boot.s:18-31`). The entire vblank apparatus is one FS poll
   (`hw.c:45-49`). Consequence: no interrupt-latency reasoning, no
   masked-section windows, no missed-vint pathology class.
2. **No FM arbitration.** FM is set once at boot by the SH-2
   (`crt0.s:249-254`) and never moves, because the 68K was given no job
   that needs the FB or the 32X VDP. The whole per-frame FM
   handshake/ownership-window problem is absent by construction.
3. **No per-frame packet format between CPUs.** The only cross-CPU data is
   two pad words and four ASCII handshake words in COMM registers, all
   latest-value, no sequence numbers, no acks, no double-buffered packets,
   no consume deadlines (`md_main.c:101-121`, `main.c:79-84`).
4. **No cross-CPU synchronization at runtime.** The 68K publisher is
   free-running; the consumer samples once per frame. They *removed* the
   68K vblank sync when it proved fragile (`md_main.c:8-12`) — the fix for
   a sync bug was deleting the sync.
5. **No slave-SH2 protocol.** Heartbeat only (`main.c:120-130`). They took
   a single-core renderer over a work-split handshake.
6. **No runtime palette machinery.** One static 256-entry CRAM load at
   boot (`main.c:64`, `hw.c:90-96`); themes are index tables
   (`theme.c:23-45`). No CRAM writes to time, ever.
7. **No DREQ / DMA of any kind.** All ROM→SDRAM and CPU→FB movement is
   plain CPU stores (`crt0.s:203-215`, `libc_min.c`). The DREQ FIFO
   protocol, 68K DMA setup, and partial-landing semantics simply do not
   exist here.
8. **No MD VDP usage.** No MD planes, no MD text layer, no 68K graphics
   composition over/under the 32X image; the MD side of the display is
   untouched after the boot blob. One video chip, one owner.
9. **No dirty-rect / partial-update system.** Full repaint with guaranteed
   coverage every frame (`render.c:174-305`); the coverage guarantee also
   deletes the clear.
10. **No frame-pacing machinery.** No 30 Hz logic split (upstream had one:
    `cannonball/src/main/main.cpp:136-144`; the port dropped it), no
    frame-skip, no adaptive cadence — logic rate is welded to flip rate and
    a slow frame just slows the game.
11. **No line-table tricks.** Written once at init (`hw.c:83-87`);
    scrolling/warping via per-frame line-table rewrites was left unbuilt.
12. **No overlays/RAMCODE management.** Code runs from ROM
    (`mars32x.ld:23-35`); SDRAM is data only.
13. **No sound engine** (scope reduction, but note the knock-on: no PWM
    interrupts, no 68K sound driver, no FM-vs-sound-driver contention).

The one place they added machinery a minimal port wouldn't have: the
one-way telemetry block + magic/checksum (`main.c:20-38`) and the DSTEP
liveness bytes — i.e. their added complexity budget went to
**testability**, not to the frame pipeline.

## Cross-check against marsdev skeleton

The independent marsdev `32x-skeleton` example makes the same choices:
SH-2 owns FBCTL and polls FS (`marsdev/examples/32x-skeleton/sh_src/mars.c:37-38`
and 8 more sites), 68K runs a tiny polled COMM command servant for pads/MD-VDP
pokes (`md_src/md_main.c:34-58,73-76`). Two unrelated codebases, one
architecture: **master SH-2 owns the whole visual frame end-to-end; the 68K
is a peripheral controller; vblank is a status poll; COMM regs carry scalars,
not protocols.**
