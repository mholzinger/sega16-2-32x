# HANDOFF — the Altered Beast decompile thread

Written 2026-09-10 for a session running IN PARALLEL with the rendering
work. You are not blocked on that thread and it is not blocked on you.
Read this whole file before opening Ghidra.

Precedent: this team has already taken Cavern Crawl from binary to a
working C program with Ghidra + Claude. Same method, harder target, and
a different definition of "done" — see THE GOAL.

---------------------------------------------------------------------
## THE GOAL, AND IT IS NOT A COMPILABLE C PROGRAM

We do NOT need to rebuild Altered Beast from source. The port RUNS
Sega's original 68000 binary, instruction for instruction, and will keep
doing so. Nobody is going to compile a decompilation and ship it.

What the rendering thread actually needs is SEMANTICS: which routine
writes what, which table means what, and which constants the game uses.
Every hour spent making code compile is an hour not spent answering a
question the other thread is blocked on.

**Deliverable: annotated facts with addresses, written into
`docs/audit/` as markdown + JSON, each one verified against our own
binary.** Not a source tree.

---------------------------------------------------------------------
## WHAT ALREADY EXISTS — DO NOT REDO THESE

  - **Ghidra 11.2.1**, `/Users/mikeholzinger/src/kyocera-2235/ghidra_11.2.1_PUBLIC/`.
  - **`tools/ghidra_run.sh`** — the headless driver. `import` once, then
    `census` or `script NAME.py [args]`. Language `68000:BE:32:default`,
    loader base 0x0 (the arcade map). The project lives OUTSIDE the repo
    (scratch dir) because it holds the analysed Sega binary.
  - **`tools/ghidra/timing_census.py`** — functions + call graph, backward
    branches with the memory they touch, every instruction on a System
    16B hardware range, the vectors.
  - **Scripts are Jython 2**: ASCII only, `getScriptArgs()` for arguments,
    and stock scripts that call `askFile` fail headless.
  - **`srcref/alteredbeast/`** — Michael J Archer's commented disassembly
    (4.2 MB, 61k lines, 4132 labels). GITIGNORED. See HANDLING.
  - **`docs/log/LOOP29.md` entries 114, 115, 119** — what the disassembly
    has already yielded and where reading it went wrong.

Already established, with addresses, verified against our binary — do
not re-derive:

    0x3982  sync_check          frame-flag spin on 0xFFF01C
    0x2D82  the one delay loop
    0x2AAC  IRQ4 handler entry
    0x3B2E  RequestPaletteUpdate   115 call sites
    0x3BCE  ReleasePaletteSlot      90 call sites
    0x3B6C  AllocatePaletteSlot
    0x3952  set_level_palettes
    object struct: $00 status, $02 routine, $06 sprite_id,
                   $08 sprite_slot, $09 priority, $0A palette_bank,
                   $0B palette_index, $0C x, $0E x_vel, $10 y,
                   $12 y_vel, $14 anim_frame, $16 anim_timer
    live tables: 0xFFF400 queue head, 0xFFF401 fallback slot,
                 0xFFF440 slot refcounts, 0xFFF480 upload queue,
                 0xFFF500 request table (indexed by palette_index)

---------------------------------------------------------------------
## THE VERIFICATION RULE — THIS IS THE WHOLE DISCIPLINE

The supplied disassembly is a STRONG source of ADDRESSES and STRUCTURE
and a WEAK source of SEMANTICS. Its author says so himself ("not 100%
perfect", "I am no 68000 expert", "I wasn't too concerned about the
enemy handler routines") and its Python tooling was AI-assisted.

Both times the rendering thread used it on 2026-09-09, the structural
claim verified perfectly and the behavioural inference drawn from it was
WRONG until measured on a running frame:

  - The palette slot allocator is real, at the stated addresses, with
    the stated object offsets. The inference that its refcount table
    signals "this palette needs a hardware slot" was wrong: a refcount
    counts OBJECT references, including actors that are off-screen and
    drawing nothing (LOOP29 115).

So, for every claim you publish:

  1. **STRUCTURE by objdump against OUR rom.** `roms/altbeast/prog68k.bin`,
     `m68k-elf-objdump -D -b binary -m 68000 --start-address=0xNNNN`.
     If our bytes do not disassemble to what the .asm says, the .asm is
     wrong and our bytes win.
  2. **MEANING by a running frame.** `ares-headless --dump wram:ADDR:LEN:file`
     against `rom/s16.32x` on `discover/inputs/play_level1.csv`. A table
     that is never written, or written with values your reading cannot
     explain, is not understood yet.
  3. Publish the claim WITH both pieces of evidence. A claim with only
     (1) is a hypothesis and must be labelled one.

Never write "this routine does X" when what you verified is "this
routine is at address X".

---------------------------------------------------------------------
## THE QUESTIONS, RANKED BY WHAT THE OTHER THREAD IS STUCK ON

### 1. PALETTE DEMAND — the current hard blocker, do this first

The port can give the MD VDP at most 4 CRAM palette lines (one is the
text ramp today). Measured on level-1: **6 distinct sprite colour sets
coexist in a single 28-line band**, and no two of the top three share a
single pen. That is why ~96% of sprite records are drawn in slow
software instead of by hardware (LOOP29 126, 127).

`palette_bank` is a COMPILE-TIME CONSTANT per actor type — the game does
`move.w #$5D,palette_bank(a6)` at 0x1FE0, `#$84` at 0x253C, and so on.
So the full demand is statically enumerable and you can answer:

  - Every `palette_bank` constant the program ever writes, with the
    routine and the actor type that writes it. A Ghidra script over all
    writes to offset $0A of the object base is the shape of this.
  - Which sets are LEVEL-1 actors versus boss / attract / other stages.
  - The mapping palette_bank (0-175) -> the palette DATA it loads, via
    `set_level_palettes` (0x3952) and the request table at 0xFFF500.
  - **The question that matters: can any two frequently-co-resident
    actor classes be made to share one palette without changing what the
    game looks like?** If two classes already load identical or near
    identical colours under different bank numbers, that is a free win
    and the other thread can act on it immediately.

### 2. THE TILEMAP UNPACKER — the one open rendering bug

`unpack_level_tilemap` with `decode_hi_part`, `rle_outter_loop`,
`rle_inner_loop`, `literal_writes`. START-HERE's open bug is that the
background name table is computed correctly in SDRAM and never reaches
MD Plane A/B at the attract title. Understanding how the game builds and
rewrites tilemap pages would let the other thread instrument the right
path instead of bisecting.

### 3. THE SPRITE-RAM WRITERS — the architecture the port wants

LOOP27 80 measured the whole pipeline at 85 words of real change per
frame conveyed by 2882 shim instructions, and specified the fix: a
write-through at the patch thunk, O(writes), deleting the rotor, the
compare, the shadow, the dirty bitmap and the packing. What blocks it is
that we intercept writes blindly. Name the routines that write sprite
RAM and text RAM, and what each one means, and that architecture becomes
safe to build.

### 4. SCENE AND LEVEL STRUCTURE

Which actor classes exist per level, and the attract-mode flow
(`on_attract_mode`, `play_game_demo`). Feeds the per-scene static
palette and art allocation the port already does per scene.

**Explicitly NOT wanted:** enemy AI behaviour, scoring, collision
detail, sound driver internals (already covered in
`docs/sound/SOUND_DRIVER.md`), or anything that only matters if you were
rebuilding the game.

---------------------------------------------------------------------
## HANDLING — non-negotiable

  - `srcref/` and `docs/audit/` are GITIGNORED and stay that way. They
    name and quote Sega's code.
  - Treat `srcref/alteredbeast` exactly as `srcref/jtcores` is treated:
    **DERIVE, NEVER COPY.** Cite `file:line` for a fact. Do not paste
    Sega's disassembly, or the third-party comments on it, into any
    tracked file.
  - Ghidra's project directory lives outside the repo. Never commit it.
  - Anything published to a tracked file is OUR words describing OUR
    measurements: an address, a verified behaviour, and the command that
    reproduces it.

---------------------------------------------------------------------
## WORKING RULES

  - Log to `docs/log/LOOP-DECOMPILE.md`, newest last, numbered entries,
    each with the date, the command, the evidence and a one-sentence
    conclusion. Mark NEGATIVE RESULTS in the heading. If you correct an
    earlier entry, mark the old one WRONG in place and say which entry
    supersedes it — that is the house style and it has saved sessions.
  - Do not modify anything under `sh_src/`, `md_src/` or `tools/` that
    the rendering thread uses. New Ghidra scripts go in `tools/ghidra/`;
    that directory is yours.
  - `git pull --rebase` before you commit; the other thread commits to
    the same branch through an auto-commit hook and will be noisy.
  - Dry voice: claim, number, `file:line`. Say plainly what is unproven.
