# sega16-2-32x

A port of Sega System 16B arcade games to the Sega 32X. The first title
is Altered Beast. The real deliverable is a reusable System 16 to 32X
porting kit, because every System 16B game has the same shape: two
scrolling tile planes, a text layer, a sprite chip, and 128 colour sets.

**Status: pre-beta.** The video pipeline runs on real hardware
semantics (ares) and is graded against the arcade in MAME. Sound is a
separate test rom and is not yet in the shipping build. See
[Status](#status) at the end for what works today.

**This repository contains no Sega code, art, or sound.** The build
takes your own MAME `altbeast` ROM set as input and produces the 32X
rom locally. See [Legal shape](#legal-shape-of-the-build).

---

## Contents

1. [How a System 16B board works](#1-how-a-system-16b-board-works)
2. [What the 32X is, and why it is not a System 16](#2-what-the-32x-is-and-why-it-is-not-a-system-16)
3. [How this port works](#3-how-this-port-works)
4. [Why it succeeds](#4-why-it-succeeds)
5. [Building](#5-building)
6. [The kit and the release tooling](#6-the-kit-and-the-release-tooling)
7. [Legal shape of the build](#legal-shape-of-the-build)
8. [Where things are written down](#where-things-are-written-down)
9. [Status](#status)

---

## 1. How a System 16B board works

Hardware facts here are derived from the open-source jtcores System 16
FPGA core (`srcref/jtcores`, GPL) and the MAME driver, cited by file.
Nothing from either is copied into this repo.

### Processors

| Chip | Role |
|---|---|
| 68000 at 10 MHz | Game logic. Writes tile RAM, sprite RAM, palette RAM, and I/O directly. |
| Z80 | Sound driver, ROM `epr-11671`. Drives a YM2151 FM chip and a uPD7759 ADPCM speech chip. |
| i8751 MCU | The board's conductor. Details below. |
| 315-5195 | Dynamic memory mapper. The game programs eight region windows once at boot. |
| 315-5296 | I/O chip: coins, players, DIP switches, display enable, screen flip. |

### The 68000 memory map, as Altered Beast programs it

The boot code copies a 16-byte table into the mapper. Decoded, it gives
this layout (`NOTES.md`):

| Region | 68K address | Contents |
|---|---|---|
| 0 | `0x000000` to `0x03FFFF` | Program ROM, 256 KB |
| 2 | `0x3F0000` | Tile bank registers |
| 3 | `0xFFC000` to `0xFFFFFF` | Work RAM, 16 KB |
| 4 | `0x440000` | Sprite RAM, 2 KB |
| 5 | `0x400000` and `0x410000` | Tile RAM 64 KB, text RAM 4 KB |
| 6 | `0x840000` | Palette RAM, 4 KB |
| 7 | `0xC40000` | I/O |

The game uses one interrupt, IRQ4, for vblank. Every other vector is a
return stub.

### The MCU is the conductor

The i8751 sits on the mapper's data bus and can read or write any 68K
address, write the Z80 sound latch, and raise 68K interrupts. On this
board the 68000's vblank interrupt comes from the MCU, not from the
video chip. Per frame the MCU reads the coin inputs and writes them
into work RAM, forwards the tile bank request to the bank registers,
pumps the sound mailbox to the Z80, and raises IRQ4. The 68K program
never touches the sound latch or the coin port itself. It reads work
RAM. This matters for the port: replace the MCU and you own the frame
clock, the inputs, and the sound channel with no 8051 emulation.

### Video

The board draws the screen with dedicated raster hardware, for free,
every frame at 60 Hz, 320 by 224.

- **Two scroll planes** (BG and FG). 8 by 8 tiles, 3 bitplanes, 16,384
  tiles in ROM. Each tile-map entry selects one of 128 eight-colour
  sets and carries a priority bit. Whole-screen scroll, plus optional
  row scroll per 8 lines (`jts16_mmr.v:67-68`) and column scroll per
  16 pixels (`jts16_scr.v`). Altered Beast enables neither.
- **A text layer** over both planes, same tile format.
- **A sprite chip** that draws variable-width ragged strips up to full
  screen size (`jts16_obj_draw.v:107`) with hardware shrink to about
  half size in 32 steps (`jts16_obj_draw.v:70`). The werewolf
  transformation and the boss scaling ride on this.
- **A ten-deep priority chain** between the layers
  (`jts16_prio.v:84-95`):

      T1 > S3 > T0 > F1 > S2 > F0 > B1 > S1 > B0 > S0

  T is text, F is the foreground plane, B is the background plane, S
  is a sprite priority level. A sprite can sit between the two halves
  of the same plane. BG colour 0 is opaque (`jts16_prio.v:87`).
- **Palette**: 4,096 entries, 5 bits per gun, plus a shadow mechanism.
  Shadow applies as `shadow & ~pal[15]` (`jts16_colmix.v:88`) and
  scales by 0.75 (`jts16_colmix.v:80-89`).

### Sound

The Z80 runs a sequence driver from `epr-11671`. The same driver ROM
ships in the English and Japanese Altered Beast and in much of the
System 16B library, so decoding it once decodes the library. Music is
YM2151 FM, eight channels. Speech is uPD7759 ADPCM streamed by the Z80
in slave mode from sample ROMs `opr-11672` and `opr-11673`.
`SOUND_DRIVER.md` is the decoded map.

---

## 2. What the 32X is, and why it is not a System 16

The 32X is a Mega Drive with two SH-2s at 23 MHz and a 15-bit-colour
framebuffer bolted on. The Mega Drive's own VDP keeps drawing, and the
32X composites its framebuffer over or under the VDP's picture.

### The one number

We did not replace a 10 MHz 68000 with 23 MHz SH-2s. We replaced
**dedicated raster hardware with software**. System 16 draws the screen
free. On the 32X every pixel is a CPU store into the framebuffer, and
the framebuffer has a measured write bandwidth.

| | |
|---|---|
| 32X framebuffer write bandwidth, measured | 6.76 MB/s |
| One pass over a 320 by 224 8bpp screen | 71,680 bytes |
| That pass at 60 Hz | 4.30 MB/s |
| Budget | about 1.6 screen passes per frame at 60 Hz |

Compositing five layers with a ten-deep priority chain in software
does not fit in 1.6 passes. Any design that does not reduce the number
of pixels the SH-2s write cannot reach 60 Hz. Every idea in this repo
is tested against that number first.

### The Mega Drive VDP is the other renderer

The VDP draws two tile planes and hardware sprites for free, exactly
the way the System 16 tile generator does. What it has:

| System 16 | Mega Drive VDP | Match |
|---|---|---|
| Two scroll planes | Plane A and Plane B | yes |
| 8 by 8 tiles, per-tile palette select | same | yes |
| Per-tile priority bit | same | yes |
| Row scroll per 8 lines | per line | MD is better |
| Column scroll per 16 pixels | per 16 pixels | exact |
| 128 sets of 8 colours, 5 bits per gun | 4 palettes of 16, 3 bits per gun | no |
| Ragged, zoomed sprites | fixed-size sprites, 20 per line | no |
| Ten-deep priority against sprites | one priority bit | no |

### The one composite boundary

The 32X layer and the VDP layer meet at exactly one boundary: per-pixel
transparency in the framebuffer plus a single global "32X over MD" or
"MD over 32X" selector. There is no way to put a 32X pixel between two
VDP planes. That single fact decides the whole architecture.

### The framebuffer is shared, and someone has to own it

The 68000 and the SH-2s both reach the framebuffer, and a flag called
FM says which side owns it. Every commercial title makes a choice here.
We measured all fourteen 32X titles in the library with a VDP-port tap
(`ARCHITECTURE.md` section 2) and replayed recorded gameplay to see
what they draw during play, not in attract mode:

- **Mortal Kombat II** during a fight: the VDP contributes nothing. The
  whole picture is the 32X framebuffer.
- **Knuckles' Chaotix** during a level: the VDP draws a hardware-scrolled
  background on Plane B, and the 32X draws on top.

Those are the two verified gameplay architectures in the library.

---

## 3. How this port works

### The game code runs natively, rebased

The arcade 68000 program is not emulated and not rewritten. It runs on
the Mega Drive's 68000 after `tools/patch_game.py` rebases its hardware
references into the 32X memory map. The patcher scans the binary for
32-bit addresses in System 16 hardware ranges, cross-checks each hit
against the disassembly, and rewrites it. Altered Beast needs 192
sites patched. Tile RAM goes to framebuffer staging, text RAM to MD
work RAM, sprite RAM and palette RAM to framebuffer staging, I/O to
work-RAM mailboxes.

The Mega Drive 68000 runs at 7.67 MHz against the arcade's 10 MHz. The
game's own logic fits; the cost that matters is what the shim adds per
vblank, which is why the write-observer pattern below exists.

### The MD shim replaces the MCU

`md_src/md_main.c` and `md_src/md_start.s` take over the MCU's duties
from the Mega Drive's own vblank: read the pad and write arcade coin
and player bits into the work-RAM addresses the game reads, forward the
tile bank request, pump the sound mailbox, then jump into the game's
own IRQ4 handler. The MCU's 2 KB program checksum is skipped. No 8051
is emulated.

### Knowing what changed: the write-observer pattern

The 68K writes freely to tile RAM, palette RAM, and the sprite list. On
the 32X those live where the SH-2s have to be told about them.
Diffing the mirrors every vblank is ruinous: the palette diff alone
cost 45 of the vblank handler's 92 scanlines to discover, in steady
state, that nothing had changed.

Instead the patcher turns each write site into a jump to a small thunk
in MD RAM that ORs a bit into a dirty bitmap, performs the displaced
write, and returns. The shim ships only dirty regions. Two thunk
families exist today, tiles and palette, and the emitter is generic.
This is the kit's answer to any hardware region the 32X cannot mirror
cheaply, and it applies to every System 16 title.

### The SH-2s implement the video board, not the game

`sh_src/m_main.c` and `sh_src/s_main.c` are a software System 16B
video chip: tile-map pages, scroll, priority, the sprite format with
zoom, flip, and pitch, the text layer, palette conversion, and shadow.
Two of its load-bearing models were checked line for line against the
RTL: the priority formulation is equivalent to `jts16_prio.v:84-95`,
and shadow matches `jts16_colmix.v:88`. The code implements the board,
so it is game-agnostic. Per-game inputs are the tile bank wiring,
screen flip, and board-revision quirks.

### The split: tile planes on the VDP, sprites and priority tiles on the 32X

This is the Chaotix architecture, chosen along the hardware's own seams:

- **Background plane and the foreground plane's non-priority tiles**
  move to the Mega Drive VDP as Plane B and Plane A. The SH-2 converts
  System 16 tiles to MD 4bpp planar patterns and ships them, with the
  name tables and scroll, through a dead block in the framebuffer that
  the 68K reads after the ownership handoff. The packet is
  self-describing and idempotent, so it never has to reason about which
  framebuffer bank the MD sees.
- **Sprites stay on the 32X**, categorically. Hardware zoom, ragged
  strips, and the 20-per-line ceiling rule the VDP out.
- **Foreground priority tiles stay on the 32X**, painted over the
  sprites. That is the escape from the one-boundary problem: the arcade
  puts sprites between the two halves of the foreground plane, and the
  32X can only do that if it owns the upper half. Measured, the
  foreground carries about a quarter of the visible tiles as priority
  tiles, and the background carries almost none.

### Colour: 21 sets into 3 palette lines

The naive count is 128 colour sets against 4 MD palettes. The measured
peak demand is 21 distinct sets on screen at once, and in the worst
scene those 21 sets contain only 36 distinct colours after quantising
to the MD's 3 bits per gun. They pack into MD palette lines 1 to 3 with
line 0 reserved for text. The pack is per-scene, with a per-set
pixel-to-pen remap applied at pattern conversion, live CRAM refresh so
fades reach the MD, and quantisation by rounding rather than
truncation. That last point is not cosmetic: System 16 art dithers pens
one step apart, and truncation rendered the stage-one sky at four times
the arcade's contrast.

The 32X framebuffer is 5-5-5, identical to System 16, so the depth loss
is confined to what moves to the VDP.

### Whole-frame scheduling

The legacy pipeline scheduled the screen in three bands and shipped a
frame every three vblanks, 20 Hz, with the bands able to show two
different game states at once. The current pipeline (`NATIVE.md`)
deletes the band as a unit. One generation is in flight at a time and
the generation is the whole frame: inputs are latched at one point, the
slave composes all rows back to back, the blit ships the whole screen or
nothing, and the flip lands only a freshly blitted bank. Cadence is
60 Hz when compose closes inside a vblank and whole-frame-coherent
30 Hz when it does not. The MD plane scroll packet is built from the
same latch, so planes and sprites move together.

### Sound

The arcade Z80 driver is decoded statically from its ROM
(`tools/snd_seq_decode.py`, `tools/snd_render.py` runs the real driver
in a Z80 core to capture the YM2151 register stream). Speech decodes
straight from the sample ROMs (`tools/pcm_from_rom.py`). Playback on
the 32X is a software YM2151 synthesiser on an SH-2 with PWM output,
because the Mega Drive's YM2612 has six FM channels and the music uses
eight. Speech is IMA ADPCM on a PWM lane. This runs in the separate
`sndtest` rom today and is not yet merged into the shipping build.

---

## 4. Why it succeeds

**It runs the real game.** There is no reimplementation to drift from
the arcade. The 68000 program is Sega's, byte for byte except for 192
rebased addresses and the observer thunks. Timing of the game logic,
enemy behaviour, hit boxes, and the level scripts are the originals.
The port cannot get the gameplay wrong in the way a rewrite can.

**It emulates the board, not the game.** The SH-2 renderer implements
the System 16B video chip. The shim implements the MCU. The patcher is
a generic address rebaser with a per-title exclusion list. The next
title starts from a kit, not from scratch.

**The split follows the hardware's seams, and every seam was measured.**
Which layers move to the VDP was decided by measuring transparent area,
priority tile counts, and colour demand, not by assumption. One of
those measurements overturned the plan: moving the background off the
32X does not shorten the blit, because the blit ships the whole
staging buffer regardless. What it buys is that the master SH-2 stops
being mid-strip when the framebuffer window arrives. Blit skips fell
from 23.4 percent of cycles to 0.2 percent, and that is the entire
mechanism behind the tearing that was the second item on the fix list.

**Hardware truth is a hierarchy, and it is written down.**

1. The arcade in MAME is the oracle for how the game should look.
2. The jtcores RTL is the spec for what the silicon does. Facts cite a
   `.v` file and a line.
3. ares is the hardware-truth proxy for the 32X. MAME's 32X does not
   model SH-2 timing, FIFO loss, or the framebuffer bus stall, so it
   is a convenience model with known limits, listed in `CLAUDE.md`.
4. A play pass on ares is the acceptance gate, and it has overruled the
   metrics repeatedly.

**Negative results are kept.** Every `LOOP*.md` has a section of ideas
that were measured and killed. Most obvious optimisations are in there
with the number that ended them, so they are not retried.

---

## 5. Building

### Requirements

- marsdev with `m68k-elf-gcc` and `sh-elf-gcc`. The build reads
  `MARSDEV`, default `~/src/marsdev/mars`. The current builds use GCC
  15.2.0 for both targets.
- Python 3.
- The MAME `altbeast` ROM set, which you supply. It is never committed
  and the `roms/` directory is ignored.
- MAME, for the parity gate. ares, for hardware-proxy testing.

### Today

    # 1. Place the ROM set. Extract altbeast.zip into roms/altbeast/
    #    and interleave the program ROM:
    #      epr-11907.a7 (even bytes) + epr-11906.a5 (odd bytes)
    #      -> roms/altbeast/prog68k.bin (256 KB)
    # 2. Regenerate the derived tables. They are built from your ROM
    #    set and from ares captures, and are NOT committed:
    make tables                 # tiles/sprites/sprbake/md_sprart/pal/glow
    # 3. Build the shipping rom (the canonical flag line; zsh arrays):
    CANON=(MDBGALL=1 NTWRAP=1 SPRTRUNC=1 FBTEXT=1 PAL32=1 FMGATE=1 R60=1
           CUTBLANK=1 BANDSHIFT=36 RG2SHIFT=40 BLITSKIP=1 DIRTYROW=1
           SPRBAKE=1 BLITSHIFT=24 SPRLATE=1 PRHOLD=6 TILECLASS=1
           TXTCLASS=1 MDSPR=1 ROWDEFER=1 PALDELTA=1 NATIVE=1
           PALSTATIC=1 PALGLOW=1 LAUNCHEARLY=1)
    make "${CANON[@]}"          # shipping rom -> rom/s16.32x
    make <FLAG>=1               # probe builds; see the Makefile header
    make sndtest                # the sound test rom

Objects depend on `.build_flags`, so switching flags forces a rebuild.
Every `make` overwrites `rom/s16.32x`; copy a build you want to keep.

`make tables` runs `tools/gen_tiles.py`, `gen_sprites.py`,
`bake_sprites.py` and `bake_mdspr.py` against your ROM set. The
palette-scene header `sh_src/pal_scenes.h` is the one input a fresh
clone must harvest itself: `tools/palscene_bake.py` consumes
`discover/palscenes/*.palsh`, raw 2048-word dumps of the SH-2 palette
shadow (`PAL_SH`, SDRAM `0x27000`, 4 KB) taken from a headless-ares
run or a savestate, with three files named `normal`, `boss_smoke` and
`transform` as the scene anchors. The script's docstring has the
format and the failure rules. The sound tables under `sndtest/` come
from `tools/soundmap_build.py` and `tools/mus_testgen.py`.

### Gates on every build

- `grep ' _end$' rom/s16.lst` must stay under `0x06019000`. The build
  fails hard if it does not.
- `python3 tools/build_id.py show rom/s16.32x` must read `normal`.
- `tools/nat_score.py rom/s16.32x 1900` on ares-headless: ships, fps,
  handler mean, and the ship-period census (see below). Compare
  against the previous shipping build; a cadence loss is a regression.
- A play pass on ares, by a person.
- `tools/parity_run.sh <dir>` (the scene-anchored MAME diff) gated the
  faithful pipeline. It is INVALID for `NATIVE=1` builds: MAME's 32X
  does not run our scheduler correctly and its captures are noise.

---

## 6. The kit and the release tooling

`TOOLKIT.md` is the living inventory of what is game-agnostic and what
is Altered-Beast-specific. The pipeline shape for any System 16B title:

1. **ROM analysis and patching**, `tools/patch_game.py`. Generic
   scanner, per-title address map and exclusions.
2. **Asset conversion**, `tools/gen_tiles.py` and `tools/gen_sprites.py`.
   The ROM filenames, sizes, and interleave come from `mame -listxml`;
   the offset parity of the sprite ROMs encodes the interleave exactly.
3. **The renderer core**, `sh_src/`. Board-level, reusable.
4. **The MD shim**, `md_src/`. Per-title MCU personality and inputs.
5. **The verification rig**, `tools/*.lua` and `tools/*.py`.

### Release tooling (planned, not yet built)

A release is a git tag plus a ROM-set hash plus a toolchain pin, and a
build from those three must produce the same SHA-256 twice. The kit
will provide three ways to get a rom, in this order of preference.

**Path 1, generate the assets.** Clone the repo, point the tools at
your MAME ROM zip, and produce the game body, tiles, sprites, and sound
data locally.

- `tools/romset.py altbeast.zip` verifies every file against a hash
  manifest, extracts to `roms/altbeast/`, and interleaves
  `prog68k.bin`. Refuses on any mismatch.

**Path 2, scaffold and splice, no compiler needed.** The repo ships a
scaffold rom that is our code with zero-filled holes where the
generated assets go, plus a Python splicer that reads the symbol
addresses from the link map and pastes your locally generated assets
into the holes. Requires Python and your ROM set only. An xdelta patch
was measured and rejected for this path: the arcade ROMs as a source
window save only 105 KB of a 1.08 MB patch, because the tiles, sprites,
and sound in the rom are transformed and xdelta cannot express them as
copies. The patch would carry Sega's data. The splicer does not.

**Path 3, full build from source.** Path 1 plus marsdev and `make`.

- `tools/check_toolchain.sh` compares the GCC versions and the marsdev
  commit against `toolchain.lock` and refuses on mismatch.
- `make release` builds with a clean flag set and the commit's own
  timestamp as the build stamp, so two builds of one tag are identical.
- `tools/release_verify.sh` builds twice from a fresh clone at the tag,
  compares both roms and the play-tested rom by SHA-256, and runs the
  parity statics and the region guard.
- `RELEASE.md` records per tag: commit, ROM-set hashes, toolchain lock,
  rom SHA-256, and the date of the ares play pass.

### Inputs by tier

The reproducible build treats generated files by where they come from.

- **From the ROM set only**: game body, tiles, sprites, sound data.
  Generated at build time, never committed.
- **From the ROM set plus a capture corpus**: tile classification,
  sprite bake tables, palette and glow bakes. The capture corpora
  (palette dumps, screenshots) and the palette-scene header are
  ignored: they are game data. The classification, index and rule
  headers (`tile_classes.h`, `sprbake.h`, `md_sprart.h`,
  `md_sprart_info.h`, `glow_tab.h`) are our analysis output and are
  committed with the generating tool named in the header.
  `make tables` regenerates the ROM-derived ones; the palette-scene
  header needs three PAL_SH dumps of your own (see below).
- **Ours**: the SH-2 and 68K sources, the shim, the tools.

---

## Legal shape of the build

The shipping rom contains Sega's 68000 program, tile and sprite art,
music sequence data, and speech samples. Those are Sega's copyright,
and Altered Beast is still sold. This repository therefore distributes
none of them. It distributes our renderer, shim, patcher, converters,
and the scripts that build the rom from a ROM set you already have.

Concretely:

- `roms/`, `rom/*.32x`, `*.bin`, and `srcref/` are ignored and have
  never been committed.
- Everything generated FROM the ROMs or captured from the running game
  is ignored too: the sound tables and speech PCM under `sndtest/`,
  the baked sprite, tile, palette and glow headers, the palette-scene
  dumps under `discover/`, screenshot corpora, parity captures, and
  the patcher's reports. `make tables` rebuilds them locally.
- No rom file, ROM image, capture, or audio is published. Users build
  their own from a ROM set they own.
- The public branch is `public`. It is an orphan with a clean history;
  the working branches carry captures in their history and are not
  pushed.

---

## Where things are written down

- `ARCHITECTURE.md`: how the 32X library draws a frame, where this port
  sits, and the pivot. Start here.
- `NATIVE.md`: the whole-frame pipeline.
- `TOOLKIT.md`: the reusable kit inventory.
- `SOUND_DRIVER.md`, `SOUND.md`: the decoded Z80 driver and the sound
  plan.
- `LOOP.md`, `LOOP6.md` through `LOOP26.md`: the working log, newest
  last, each with a negative-results section.
- `NOTES.md`: hardware notes and the decoded memory map.
- `CLAUDE.md`: the standing rules and the MAME versus ares split.

---

## Status

2026-09-02. Shipping build `07d77855+` (branch `native1`). Mike's
play pass: "good enough to continue working from; not at 100%
MAME/arcade parity but very, very close."

What works today, on ares:

- The arcade program runs natively on the 68000 with the shim as MCU.
- The Mega Drive VDP draws the background and the non-priority
  foreground from per-scene static palettes (`PALSTATIC`), with the
  game's 18-word palette animation played from a baked table
  (`PALGLOW`).
- The SH-2s compose sprites (zoom, shadow, baked frames), the priority
  foreground and the text layer into the framebuffer, whole frame per
  generation (`NATIVE`), launched before the ack (`LAUNCHEARLY`).
- Boss, transformation and level-transition scenes render correctly:
  gravestone, orb, crystal ball, sphere and boss-smoke fixes are in.

The bar and where we stand against it:

- The ship bar is 60 Hz, or as close as the machine gives. A locked
  30 Hz is not progress and is not shipped.
- Light scenes: about 36% of generations ship in one vint, the rest in
  two. Sprite-heavy scenes: about 9% single-vint, most in two, a
  three-vint tail. Slave compose is about 0.93 vint per generation;
  the map drain is 0.46. Numbers from `PHASECENSUS=1` on Mike's
  savestates; `BOSSFIGHT.md` has the tables.
- The route to 60 in order: chase the blit (launch the next
  generation during the blit, band-ordered), then category-1 tiles on
  plane A with MD-matched pens, then take the map drain off the close
  path. Each has a negative-result record to read first.

Not done:

- Sound is in `sndtest`, not the shipping rom. `INTEGRATION.md` is the
  merge contract; `HANDOFF-SOUND.md` the state of the driver.
- Wolf frames drop during the boss-smoke entrance; the level-2 head's
  light plays level-1 glow rules.
- The release tooling in section 6 is a plan. The per-title config for
  the patcher does not exist yet; the Altered Beast facts live in the
  tools.
