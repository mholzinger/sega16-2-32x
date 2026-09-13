"""goldnaxe (set 6 US, i8751 317-0123A): patch tables, HAND-DERIVED.

STATUS 2026-09-12 (LOOP-DECOMPILE-GOLDNAXE 8): MEMMAP, MCU_*, PAL_* are
derived with their consumers cited. Every other key is None, with the
census that will derive it named beside it; the patcher must treat None
as "this idiom does not exist in this title" (patch_game.py raises on a
MISSING key today, which is right; it needs a None path).

Sources: docs/log/LOOP-DECOMPILE-GOLDNAXE.md (entries 2, 3, 5, 6, 8),
docs/audit/goldnaxe/write_census.txt (arcade write census; its PCs are
the instruction AFTER the writer), roms/goldnaxe/prog68k.asm.
Offsets are program-ROM offsets (0x000000-0x07FFFF).

`python3 tools/game_derive.py altbeast goldnaxe` was run first (entry 7):
it carried MCU_SND and MCU_COINS and nothing else; do not regenerate
over this file.
"""

TABLES = {
    # ------------------------------------------------------------------
    # THE MEMORY MAP (entry 2): what the i8751 programs into the 315-5195
    # at runtime, captured from its register writes on the arcade with
    # tools/s16b_map_probe.lua. NOT Altered Beast's, and NOT the table at
    # MCU ROM 0xFEA. patch_game.py's remap() hard-codes AB's source ranges
    # (0x400000 tiles, 0x410000 text, 0x440000 sprites, 0x840000 palette,
    # 0x3F0000 bank, 0xC40000 io); it must read them from here.
    'MEMMAP': {
        'program':  (0x000000, 0x080000),   # 512 KB, region 0
        'tileram':  (0x100000, 0x110000),   # region 5, 64 KB
        'textram':  (0x110000, 0x111000),   # region 5, +0x10000, 4 KB (layer regs at +0xE80..)
        'palette':  (0x140000, 0x141000),   # region 6, 4 KB
        'spriteram':(0x200000, 0x200800),   # region 4 AT BOOT ONLY — see SPRITE_BASE_VAR
        'tilebank': (0x1F2000, 0x1F2004),   # 171-5797: movep.w to 0x1F2001/3 (IRQ4 0x2F94)
        'mul':      (0x1F0000, 0x1F0010),   # 315-5248 multiplier (read AND written)
        'cmptimer': (0x1F1000, 0x1F1010),   # 315-5250 compare/timer (read AND written)
        'rgn2':     (0x1E0000, 0x1E0010),   # 5797 region 2, MAME "unknown_rgn2", written 0x1E0000-8, read 0x1E0008
        'io':       (0xC40000, 0xC44000),   # 315-5296
        'mapper':   (0xFE0000, 0xFE0040),   # 315-5195 regs; the 68K writes reg 3 (0xFE0007) directly at 10 sites
        'workram':  (0xFFC000, 0x1000000),
    },
    # Sprite RAM has no fixed address: the MCU remaps region 4 every vblank
    # (bases seen: 0x20,0x50,0x60,0x70,0x80,0x90 << 16) and publishes it in
    # this long (high word = page). The 68K reads it at 0x2F80 (IRQ4 sprite
    # copy, movea via swap), 0x39BA and 0x568CE (80-record clears). The
    # shim, which replaces the MCU, sets it ONCE to the port's sprite
    # staging buffer. The only sprite LITERALS in the program assume the
    # boot base: 0x5A6C/0x6B70/0x5891C/0x59A08 `move.w #-1,0x200004`
    # (list terminator after a reset) and 0x6824/0x596C2 `move.l
    # 0x202020,d0` (unread yet — HYPOTHESIS: data misdisassembled).
    'SPRITE_BASE_VAR': 0xFFECC4,

    # ------------------------------------------------------------------
    # MCU <-> 68K mailboxes (entries 3, 6). Same idiom as AB (byte, 0xFF =
    # idle, MCU forwards and rewrites 0xFF) at different addresses.
    'MCU_SND':   0xFFECFC,   # popped from the ring 0xFFEC40-5F by IRQ4 (0x331E); MCU 0x82E-0x84F forwards to reg 3
    'MCU_COINS': 0xFFEC96,   # the MCU writes it every vblank (regs 0A-0C = 0xFFEC96); 68K consumer not yet read
    'MCU_BUSY':  0xFFECD4,   # HYPOTHESIS: the third word the MCU reads each vblank (0xFFECD4, 0xFFEC1E, 0xFFECFC);
                             # AB's busy word had the same shape. Consumer to be read before the shim relies on it.
    # The conductor's second half of the vint handshake (entry 6): after
    # the game clears them at 0x3C90 it spins at 0x3CA2 until these four
    # words are back. The MCU writes them every vblank.
    'MCU_SIGNATURE': (0xFFECD8, [0x048C, 0x159D, 0x26AE, 0x37BF]),
    'FRAME_FLAG': 0xFFEC1C,          # clr.b at 0x3C98, tst.b/beq at 0x3C9C; IRQ4 addq.b at 0x2F7A
    'FRAME_SKIP_FLAG': 0xFFEC1E,     # IRQ4 tests it first (0x2F64) and skips the frame side
    'MISSED_FRAME_CTR': 0xFFED4C,    # IRQ4 addq.w when the flag was still set (0x2F72)
    'MCU_ROM_CHECK': (0x0714, 0x0800),   # bytes the MCU reads through the mapper at boot before releasing reset (entry 3)

    # ------------------------------------------------------------------
    # PALETTE (entry 8). Sites are the instruction that carries the palette
    # literal (post-remap 0xFF9xxx); the patcher plants `jsr thunk` over it
    # and the thunk ORs the 256-byte-region mask into PAL_DIRTY, then runs
    # the displaced instruction. Masks are static footprints read from the
    # loop after each site. Region bit r = bytes [r*0x100, r*0x100+0xFF].
    # (site, displaced length, mask, note)
    'PAL_DIRTY_SITES': [
        (0x007FA, 8, 0x0001, "boot: move.w #imm,0x140000"),
        (0x01082, 6, 0x0001, "lea 0x140000,a0; words at +8,+A,+C"),
        (0x01124, 6, 0x0001, "lea 0x140000,a0; words at +8,+A,+C,+2C (DSW-driven colour)"),
        (0x0115A, 6, 0x0001, "lea 0x140050,a0; 4 longs from table 0x2DCC[EC37<<4] — every frame"),
        (0x01176, 6, 0x0001, "lea 0x140060,a0; 4 longs from 0x2DCC[E0B7<<4] + 4 longs #-1 when EC26.6 clear — every frame"),
        (0x020FC, 8, 0x0001, "move.w #53,0x140000"),
        (0x030EE, 6, 0x0001, "IRQ4: lea 0x140040,a0; 4 longs from table 0x66ED0[ECC3&7] or #0x7FFF7FFF — every vint"),
        (0x03B60, 6, 0x0001, "lea 0x140040,a1; 8 longs"),
        (0x03C64, 6, 0x00FF, "clear-all: movea.l #0x140000,a0; 2 x 256 longs from ROM 0x66E90 (0x3C84 helper) = 0x000-0x7FF"),
        (0x0488C, 6, 0x0001, "lea 0x140000,a1; 20 longs"),
        (0x05436, 6, 0x0001, "lea 0x140010,a1; 24 words from pc-rel 0x58F8"),
        (0x05448, 6, 0x0080, "lea 0x140720,a1; 8 x (word, word at +16) = 0x720-0x73F"),
        (0x05A84, 8, 0x0001, "post-STOP reset: move.w #imm,0x140000"), (0x05A8C, 8, 0x0001, "0x140002"), (0x05A94, 8, 0x0001, "0x140004"), (0x05A9C, 8, 0x0001, "0x140006"),
        (0x05AA4, 8, 0x0001, "0x140010"), (0x05AAC, 8, 0x0001, "0x140012"), (0x05AB4, 8, 0x0001, "0x140014"), (0x05ABC, 8, 0x0001, "0x140016"),
        (0x05AC4, 8, 0x0001, "0x140020"), (0x05ACC, 8, 0x0001, "0x140022"), (0x05AD4, 8, 0x0001, "0x140024"), (0x05ADC, 8, 0x0001, "0x140026"),
        (0x060CE, 6, 0x0001, "lea 0x140010,a0; 7 longs from pc-rel 0x69FA"),
        (0x06412, 6, 0x000F, "lea 0x140080,a0; 40 x 8 words from pc-rel 0x69AA = 0x080-0x31F"),
        (0x06B78, 8, 0x0001, "init: move.w #imm,0x140000 (same 12-store block as 0x5A84)"), (0x06B80, 8, 0x0001, "0x140002"), (0x06B88, 8, 0x0001, "0x140004"), (0x06B90, 8, 0x0001, "0x140006"),
        (0x06B98, 8, 0x0001, "0x140010"), (0x06BA0, 8, 0x0001, "0x140012"), (0x06BA8, 8, 0x0001, "0x140014"), (0x06BB0, 8, 0x0001, "0x140016"),
        (0x06BB8, 8, 0x0001, "0x140020"), (0x06BC0, 8, 0x0001, "0x140022"), (0x06BC8, 8, 0x0001, "0x140024"), (0x06BD0, 8, 0x0001, "0x140026"),
        (0x07142, 6, 0x0001, "lea 0x140000,a0; words at +2,+12,+52 / +4,+14,+54"),
        (0x09D82, 8, 0x0001, "move.w #imm,0x140072"), (0x09D8A, 8, 0x0001, "0x140074"), (0x09D92, 8, 0x0001, "0x140076"),
        (0x09D9A, 8, 0x0001, "0x140078"), (0x09DA2, 8, 0x0001, "0x14007A"), (0x09DAA, 8, 0x0001, "0x14007C"),
        (0x36360, 8, 0x0001, "move.w #3822,0x140000"),
        (0x36368, 8, 0x0001, "move.w #3822,0x140064"),
        (0x36378, 6, 0x0060, "lea 0x1405E2,a4; 12 words stride 16 = 0x5E2-0x692"),
        (0x365A0, 8, 0x0001, "move.w #1280,0x140000"),
        (0x365AC, 6, 0x0001, "lea 0x140010,a1; 4 longs from pc-rel 0x36B16"),
        (0x37052, 8, 0x0001, "move.w #27015,0x140000"),
        (0x3711C, 6, 0x0001, "lea 0x140020,a1; 4 longs"),
        (0x3725C, 8, 0x0001, "move.w (a0,d0.w),0x14001E"),
    ],
    # Duplicated copies of the above in the second program half
    # (0x53000-0x5FFFF: a partial copy of the low code, 776/880 bytes equal
    # over 0x3990-0x3D00). NEVER EXECUTED in the traces (173 frames of
    # start/select, 8 of play: no PC above 0x3FFFF except banks 01/03).
    # HYPOTHESIS: unreachable build residue. Listed so the builder can
    # choose to thunk them (27 more slots) or exclude them; either way
    # remap() rebases their literals.
    'PAL_DIRTY_SITES_DUP': [0x53EA2, 0x53F44, 0x53F7A, 0x53F96, 0x56A74, 0x56F34, 0x57760, 0x582CC, 0x582DE,
                            0x58934, 0x5893C, 0x58944, 0x5894C, 0x58954, 0x5895C, 0x58964, 0x5896C, 0x58974, 0x5897C, 0x58984, 0x5898C,
                            0x58F82, 0x592B4, 0x59A10, 0x59A18, 0x59A20, 0x59A28, 0x59A30, 0x59A38, 0x59A40, 0x59A48, 0x59A50, 0x59A58, 0x59A60, 0x59A68,
                            0x59FDA, 0x5CC1A, 0x5CC22, 0x5CC2A, 0x5CC32, 0x5CC3A, 0x5CC42],
    # Queued-pointer palette writer, AB's PAL_THUNK_B idiom EXACTLY: IRQ4
    # calls 0x3280 every vint, which drains a queue at 0xFFF002 (pointer)
    # / 0xFFF006 (count) of (dst, src) pairs: `movea.l (a2)+,a1 ;
    # movea.l (a2)+,a0` at 0x328C (bytes 225A 205A), then 7 longs = 28
    # bytes to a1. The region is only knowable from a1 at write time; the
    # patcher's thunk B (r and r+1 from a1) fits unchanged. Pushers seen:
    # 0x4060 (lea 0x140800 + (d0<<5) + 2 -> queue; the 0x800-0x9DC
    # writes of the census, 60 in 3000 frames).
    'PAL_THUNK_B': [0x328C],
    # A palette POINTER stored into an object field, consumed later:
    # 0x5538 `move.l #0x140720,56(a6)` (2D7C 0014 0720 0038 — the literal is
    # NOT the last long, so it is not a PAL_DIRTY_SITES shape; remap()
    # still rebases it). The stores are 0x55C6 `move.w d0,16(a1)` and
    # 0x55CA `move.w d0,(a1)+` after `movea.l 56(a2),a1` at 0x55A0
    # (226A 0038, 4 bytes = one jsr abs.w). Footprint 0x720-0x73F, mask
    # 0x0080. Mark at USE (a thunk over 0x55A0), not at the store of the
    # pointer, or the mirror ships old colours (LOOP29 166).
    'PAL_PTR_USE_SITES': [(0x055A0, 4, 0x0080, "movea.l 56(a2),a1 -> stores at 0x55C6/0x55CA")],
    # No runtime-offset cycler exists in this title: every per-frame
    # palette writer has a static footprint (0x115A, 0x1176, 0x30EE). AB's
    # PAL_THUNK_A/APOST idiom does not apply.
    'PAL_THUNK_A': None,
    'PAL_THUNK_APOST': None,
    'PAL_LAUNCH': None,     # AB's cycle-launch pointer table; no equivalent found (0x2DCC / 0x66ED0 are colour tables, not scripts)

    # ------------------------------------------------------------------
    # NOT YET DERIVED — each names the census that derives it.
    'TILE_DIRTY_SITES': None,   # 57 literal sites (entry 8 list) + 0x399E movea.l #0x100000 fill; page bits from each loop
    'TEXT_IDIOM': None,         # 170 literal text sites; the movew->addw family is an AB text-writer shape, re-derive
    'TXT_WRAM_CLEAR_SITES': None, 'TXT_WRAM_WRITERS': None,   # HUD writers 0xC8CC/0xC900/0xC918 (census), row table 0x3EB4/0x3EF6
    'FMGATE_ENTRIES': None, 'FMGATE_SPANS': None,   # FB-writer entries: tile loaders 0x1FE2-0x2066, 0x399E, text writers
    'TAS_SITES': None,          # scan `tas` opcodes (4AE8/4AF8/4AEE) in prog68k.asm
    'RLE_EVEN_PASS': None,      # HYPOTHESIS: 0x1FE2/0x1FF2 (lea 0x100000 / 0x100001 = even/odd passes) is the same loader idiom
    'STRIP_BLITTER_MOVEW': None, 'STRIP_BLITTER_BASE': None,
    'DATA_EXCLUDE': None,       # scan for word pairs that decode as 0x0014xxxx/0x0010xxxx/0x0011xxxx inside data (entry 50 rule 2)
    'BOOT_JUMPINS': None, 'BOOT_PCREL': None,   # boot copy region for this title is TBD (boot runs from ROM 0x40E; no RAM copy seen yet)
    'SPAWN_META': None, 'SPAWN_LO': None, 'SPAWN_CAP': None,
    'REBASE_TABLES': None, 'HARVESTED_HANDLERS': None, 'HARVEST_BLACKLIST': None, 'HARVEST_BOUND': None,
    'STRIDE_TABLES': None, 'IMM_OVERRIDES': None, 'LOW_VECTOR_READS': None, 'ABSW_JMP': None,
    'DISPATCHERS': None, 'DATA_PTR_NORM': None, 'REBASE_EXCLUDE': None,
}
