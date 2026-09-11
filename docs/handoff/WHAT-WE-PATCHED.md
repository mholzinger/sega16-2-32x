# What the port actually changes in Sega's binary

Generated 2026-09-11 from `tools/patch_report.txt` and
`tools/game_altbeast.py`, against `roms/altbeast/prog68k.bin` (262144
bytes, the two program ROMs concatenated).

## First: the disassembly is complete

    instructions      19102 of the reference's 19137 matched   99.82%
    above the code ceiling (0x1EF1E)                                0
    functions bounded                                             560
      correctly bounded (terminator or fall-through)               98%
    classified                                                    560
      read to their return and named                               45
      classified by signature (hypotheses, marked as such)         515

Every instruction the reference disassembly has, we have. What is thin is
NAMING, not coverage.

## The patches, by what they do

**224 declared sites** in `tools/game_altbeast.py`, plus **192 hardware
references** rewritten by the address sweep. Five families:

    transport: tell the SH-2 what changed      103 sites
    address rebasing: the map moved             62
    timing: keep the 68K out of the FB window   40
    format idioms the port re-implements        11
    hardware that behaves differently            8

**Not one of them changes what the game computes.** Every family is the
same access somewhere else, the same access announced, or the same access
at a safe moment. That is why arcade parity is checkable at all.

### 1. Address rebasing — the arcade map does not exist here

The sweep rewrote 192 hardware references. Where they pointed, and where
they now point:

    textram    -> 32X framebuffer   73    e.g. 0x532 move.l #$4100B4,...
    palette    -> MD work RAM       44    e.g. 0x1EF2 clr.w  $840000
    io         -> MD work RAM       32    e.g. 0x45C  move.b #$80,$C43007
    tileram    -> 32X framebuffer   28    e.g. 0xD12  lea    $400518,a1
    textram    -> MD work RAM       10    e.g. 0x51C  move.l #$410E80,...
    spriteram  -> MD work RAM        4    e.g. 0x2B1E movea.l #$440000,a2
    tilebank   -> MD work RAM        1    e.g. 0x472  lea    $3F0000,a0

The split matters and it is the whole architecture. **The SH-2 cannot see
Mega Drive work RAM and cannot touch the VDP at all.** So anything the
SH-2 must read is rebased into the 32X framebuffer, which both CPUs can
see; anything only the 68K needs stays in MD work RAM. Tile and text data
go to the framebuffer because the SH-2 composes them. Palette, IO and
sprite RAM stay in work RAM because the 68K handles them.

### 2. Transport — the SH-2 cannot see what the 68K wrote

The largest family, 103 sites, and it exists solely because of that
isolation. Each is a `jsr` to a thunk that ORs a bit into a dirty bitmap
and then runs the instruction it displaced. 25 tile sites, 42 palette
sites, 25 palette thunk-B sites, plus text.

**This family shrinks as work moves to the VDP.** A plane the VDP draws
needs no dirty bits, because nothing has to be told.

### 3. Timing — the 68K cannot touch the framebuffer at FM=1

40 sites. A 68K framebuffer write while the FM bit is set is DROPPED, on
ares and on the FPGA. The gate entries and spans keep the game's writes
inside FM=0 windows.

### 4. Hardware that behaves differently — 8 sites

    5  TAS      the MD bus arbiter drops the write phase of the locked
                read-modify-write, so the latch never sets. $3E is an
                object claim lock, so without this two actors both
                believe they own the same target.
    3  MCU      busy, coins and sound command mailboxes.

### 5. Format idioms the port re-implements — 11 sites

The tilemap RLE even pass, the strip blitter, and the text idiom: places
where the port does the same job itself rather than let the game do it
against hardware that is not there.

## Probe patches, not in the shipping line

    MISSKEEP    nops the clr.w at 0x930 so the game stops wiping its own
                missed-frame counter. Every 0.0% ever read off 0xFFF144
                was that clear; the true rate is 50%.
    SCENESEL    rewrites the round->scene table at 0x1CDA to all-N so any
                scene can be measured without playing to it.
    MDSPRPROBE  blanks the sprite record copy to price it. Two points of
                miss rate out of fifty.
    MDHSCR      the game writes MD hscroll directly from its own scroll
                stores. Correct and neutral.
