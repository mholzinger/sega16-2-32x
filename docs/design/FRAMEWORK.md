# The System 16 framework: engine vs game

Goal (Mike, 2026-10-29): readability, extensibility, and above all a framework that imports the
rest of the System 16B library. Altered Beast plays to completion (`sw344`) and builds from a
MAME zip (`make release-rom`); every step below keeps that rom byte-identical.

## Where the tree stands

`docs/audit/framework_inventory.tsv`: 633 items (every source file, tool, Makefile target and
flag block), classified 2026-10-29 by seven API passes. The passes read headers and grep hits, so
a single verdict is INFERRED until the step that acts on it checks it.

| class | items | meaning |
|---|---|---|
| ENGINE | 108 | reused unchanged by every title |
| ENGINE+TABLE | 127 | generic logic carrying Altered Beast constants inline |
| GAME-AB | 63 | meaningful only for Altered Beast |
| DIAG | 165 | probes and instruments: kept, not built into the rom |
| DEAD | 170 | reads deleted code, a retired experiment, or nothing |

## The blockers, in the order they cost a second title

1. **Constants inline, not in a table.** `cport_prep.py` (684 literal hits), `cport_layout.py`,
   `md_main.c` (0xFFF142 round, 0xFFF02A scene timer, 0xFFF01C latch, 0xFF9000 palette),
   `r60_main.c`/`r60_planes.c` (`altbeast_tiles`, WRAM 0x3031, the US sprite-bank mask),
   `port_l2.c` (score blocks, spawn/despawn, DIPs), the sound command lists in
   `sound_inputs.sh`/`speech_order.py`/`sfx_bake.py`. `tools/game_altbeast.py` + `kit/schema.py`
   already define the table mechanism; most constants never moved into it.
2. **Mixed files.** `sh_src/port_l2.c` is the port runtime (coroutine, call68, frame wait) and
   AB gameplay glue in one file; `md_src/testcart.c` holds the shipping secret menu
   (`frontend_menu_screen`, :2340) inside the test suite.
3. **One corpus assumed.** `docs/decomp/altbeast.c`, `roms/altbeast/prog68k.bin` and the AB
   tape are defaults in the generator, the gates and the parity harness.
4. **Dead weight that looks like framework.** ~22 orphan generated headers (cat1*, pal_*,
   tilecram, setcols, glow, mdlines, sprbake) and ~170 dead tools/flags from the retired renderer.

## Target layout

    games/<title>/
      game.py        THE per-game table (schema-checked by kit/schema.py): romset pairs + SHA-1,
                     WRAM cells, object-slot layout, scene anchors, sound command lists, DIPs,
                     patch sites, the secret menu's content. Generates game_cfg.h for the C side.
                     (done: games/<t>/game.py + tools/emit_game_cfg.py; still inline: cport_prep,
                     the sound command lists, the art symbols, tools/romset.py GAMES)
      decomp/        the committed decompile inputs        (done: games/altbeast/decomp/)
      hand/          hand-derived functions                 (done: games/altbeast/hand/)
      glue.c         title-specific runtime hooks           (done: #included by port_l2.c where it was cut)
      tests/         tapes, demo checks, oracle tables      (today: tests/, tools/inputs/)
    sh_src/r60/      renderer (engine)       sh_src/portfull/  port runtime: w68.h, ghidra_rt, layout
    md_src/          68K helper (engine)     tools/            generic pipeline, GAME-parameterised

`make release-rom GAME=<title>` is the contract: a fresh clone plus `mame/<title>.zip`.

## Order

1. Delete DEAD (check each: `round_sets.py` still feeds `pal_rounds_md.h`), byte-identical.
2. `games/altbeast/game.py` + generated `game_cfg.h`; lift the constants, byte-identical.
3. Split `port_l2.c` and `testcart.c`; move AB files under `games/altbeast/`, byte-identical.
4. Golden Axe smoke test: run romset -> decompile -> cport_prep on `goldnaxe`, list what breaks.

## Step 4: Golden Axe smoke test (2026-10-29)

A fresh clone plus `mame/goldnaxe.zip`, `GAME=goldnaxe`, walked stage by stage. SILENT means it
completed and produced Altered Beast or AB data: the dangerous kind.

| stage | result |
|---|---|
| `tools/romset.py goldnaxe` | OK: 15 chips, 512 KB program, SHA-1 matches |
| `make tables GAME=goldnaxe` | tiles and sprites OK; was SILENT (AB's `discover/*.csv`); now LOUD: discovery is `games/<title>/discover/`, missing = error |
| `make candidate GAME=goldnaxe` | **was SILENT**: `ship-us` forced `GAME=altbeast`, so the "GA" rom was AB (3 bytes apart). Fixed e5fd7ad3 |
| sound lanes | LOUD: `tools/sound_inputs.sh` refuses any title but altbeast until its effect list and speech order are derived |
| `tools/patch_game.py` | loud: asserts AB instruction bytes at fixed offsets (`patch_game.py:327`, 0x200A); the port needs only its `game_irq.h`, `boot_copy.bin` and header outputs |
| decompile half (read, not run) | was SILENT; now LOUD: corpus, program and audit rules follow GAME (`CPORT_CORPUS`, `$(GAMEROMS)`), and `PORT_TITLES` stops a PORTFULL build for any title without CPORT_* tables |
| `games/goldnaxe/game.py` C keys | loud by design: no WRAM_*/menu keys, so game_cfg.h users fail to compile |
| Golden Axe C corpus | does not exist: the Ghidra decompile (and its datwidth/roots/handler tables) has not been run for GA |

**The rule this proves:** a missing per-game input must fail loudly. Three of the five blockers
were silent; all five are loud since 2026-10-29. Next work: thread GAME through the decompile half (corpus, program, unit name, the
CPORT_* tables as game.py keys) and make every AB default an error; split patch_game.py's
port-needed outputs from its 68K-shim patching; GAME-parameterise the sprite discovery inputs and
the sound lists.
