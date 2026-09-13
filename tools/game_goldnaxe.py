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
    # TILE RAM (entry 9). Same thunk mechanism as AB: the base-load
    # instruction becomes `jsr thunk`, the thunk ORs page bits (page =
    # tile offset >> 12, 16 pages of 4 KB) into the dirty bitmap and runs
    # the displaced instruction. TARGETS HERE ARE ARCADE ADDRESSES
    # (0x10xxxx); AB's file stores post-remap 0x85xxxx targets because the
    # check runs after remap() — the builder applies MEMMAP's remap to
    # these before the assert. Page bits read from each loop body.
    # (site, opcode word, target, page bits, note)
    'TILE_DIRTY_SITES': [
        (0x01FE2, 0x45F9, 0x100000, 0x003F, "RLE even-byte pass A: 12288 words = 0x100000-0x105FFF"),
        (0x01FEA, 0x45F9, 0x106000, 0x0FC0, "RLE even-byte pass B: 0x106000-0x10BFFF"),
        (0x01FF2, 0x45F9, 0x100001, 0x003F, "RLE odd-byte pass A"),
        (0x01FFA, 0x45F9, 0x106001, 0x0FC0, "RLE odd-byte pass B"),
        (0x02056, 0x43F9, 0x10C000, 0x7000, "RLE bytes into 0x10C000-0x10EFFF (table 0x2A3C[ED4A])"),
        (0x048E2, 0x41F9, 0x102000, 0x000C, "byte strip from ROM 0x5236 into page 2 (length table-driven; page 3 as margin)"),
        (0x04C62, 0x41F9, 0x102000, 0x000C, "RMW: subq.b #1,(a0)+ counters kept IN tile RAM page 2"),
        (0x060F6, 0x47F9, 0x101000, 0x001E, "scratch SAVE: 4096 longs of WRAM 0xFFC000 -> 0x101000-0x104FFF (restore at 0x6126 is read-only: remap only, no thunk)"),
        (0x06356, 0x41F9, 0x1000B2, 0x0001, "37 words"), (0x0635E, 0x41F9, 0x100D32, 0x0001, "37 words"),
        (0x06366, 0x41F9, 0x1000B2, 0x0001, "25 rows stride 128"), (0x0636E, 0x41F9, 0x1000FC, 0x0001, "25 rows stride 128"),
        (0x063AC, 0x43F9, 0x10023A, 0x0003, "4 x 0x63C6 blocks stride 640 (10 x 5 rows)"),
        (0x06ED6, 0x41F9, 0x100000, 0xFFFF, "fill 16384 longs = the whole 64 KB"),
        (0x071F2, 0x43F9, 0x100128, 0x0003, "HYPOTHESIS: a1 handed to code after the 0x3C90 wait; footprint unread"),
        (0x073A6, 0x43F9, 0x106004, 0x0040, "28 rows x 9 longs stride 128 from ROM 0x71CDE"),
        (0x073B6, 0x43F9, 0x101004, 0x0002, "28 rows x 9 longs from ROM 0x718EE"),
        (0x073C4, 0x43F9, 0x100004, 0x0001, "28 rows x 9 longs from table 0x7460[ED4A]"),
        (0x07AE8, 0x41F9, 0x10C000, 0x7000, "computed offset within the 0x10C000 plane"),
        (0x0C4B2, 0x41F9, 0x10C000, 0x7000, "computed offset within the 0x10C000 plane"),
        (0x0C59A, 0x41F9, 0x100000, 0x00FF, "clear 6144 longs, index & 0x7FFC, base 0x100000 (page-select by ED4A)"),
        (0x0C5A0, 0x43F9, 0x108000, 0xFF00, "the same clear's second base"),
        (0x0C5E2, 0x45F9, 0x100000, 0xFFFF, "tilemap builder from table 0xCCE6[ED4A]: 4 x 64 x 16 (bsr 0xC62A); whole plane"),
        (0x0399E, 0x207C, 0x100000, 0xFFFF, "fill 16384 longs (movea.l #imm); the 183,402-write site of the census"),
    ],
    # Block copies whose tile offset is the FIRST WORD OF THE TABLE at a0:
    # `lea base,aN ; adda.w (a0)+,aN ; <rows of 128>`. AB's STRIP_BLITTER
    # precise thunk (page from (A0), marks page and page+1) applies with
    # one change: add the base's own page (0 for 0x100000, 0 for 0x100C00
    # unless the offset crosses, 1 for 0x101000). (site, opcode word,
    # base) — displaced instruction is the 6-byte lea, a0 is intact at it.
    # 4th field = the register holding the table pointer whose first word
    # is the offset (`adda.w (aN)+` follows the lea): a0 except 0x36404
    # (a2, D0DA) and 0x364AE (a3, D0DB).
    'STRIP_BLITTERS': [
        (0x02166, 0x43F9, 0x100C00, 'a0'), (0x050B4, 0x43F9, 0x100C00, 'a0'), (0x0586A, 0x43F9, 0x100000, 'a0'),
        (0x36404, 0x41F9, 0x101000, 'a2'), (0x36484, 0x43F9, 0x100000, 'a0'), (0x364AE, 0x41F9, 0x101000, 'a3'),
        (0x36AD8, 0x43F9, 0x100000, 'a0'), (0x37092, 0x43F9, 0x100000, 'a0'),
    ],
    'STRIP_BLITTER_MOVEW': None,   # AB's `movew (a0)+,d0` clobber does not occur: these use adda.w into an address register
    'STRIP_BLITTER_BASE': None,    # superseded by STRIP_BLITTERS above
    # A tile pointer computed into object fields and used later (like the
    # palette pointer at 0x5538): 0x731A `lea 0x100004,a1` + offsets ->
    # 56(a6) and +4096 -> 60(a6); the store is `move.w (a1),(a0)` at
    # 0x7398 after `movea.l 56(a6),a0` at 0x7390 (206E 0038, 4 bytes).
    # Mark at use, pages 0-1.
    'TILE_PTR_USE_SITES': [(0x07390, 4, 0x0003, "movea.l 56(a6),a0 -> move.w (a1),(a0) at 0x7398")],
    # Read-only tile leas (remap only, never thunked): 0x49DC (copies
    # 0x10F531.. INTO text RAM 0x110531), 0x6126 (scratch restore).
    'TILE_READONLY_LEAS': [0x049DC, 0x06126],
    'TILE_DIRTY_SITES_DUP': [0x577B6, 0x578B0, 0x57B38, 0x57F4A, 0x58700, 0x58FAA, 0x58FC2, 0x591F8, 0x59200, 0x59208, 0x59210,
                             0x5924E, 0x59D6E, 0x5A08A, 0x5A1B2, 0x5A23E, 0x5A24E, 0x5A25C, 0x5A980, 0x5F34A, 0x5F432, 0x5F438, 0x5F47A],
    # The RLE tile loader (0x2004 even pass, 0x201E odd pass) is AB's
    # idiom with a different instruction layout. Even pass at 0x2004:
    #   +0  3E3C 2FFF   move.w #12287,d7      (word budget)
    #   +4  7600        moveq #0,d3
    #   +6  1619        move.b (a1)+,d3       run length      <- RLE_EVEN_PASS (0x200A)
    #   +8  1819        move.b (a1)+,d4       value
    #   +A  1484        move.b d4,(a2)        store even byte
    #   +C  548A        addq.l #2,a2
    #   +E  5347        subq.w #1,d7
    #   +10 6506        bcs +6
    #   +12 51CB FFF6   dbf d3,+A
    #   +16 60EC        bra +4
    # AB's rewrite (+2..+6 -> lsl/move.w, dbf at +A) must be re-laid for
    # this shape; the odd pass at 0x201E stores `move.b (a1)+,(a2)` with a
    # zero-run branch (0x2022/0x2034).
    'RLE_EVEN_PASS': 0x0200A,
    'RLE_ODD_PASS': 0x0201E,

    # ------------------------------------------------------------------
    # TAS (entry 9). The Mega Drive drops the write half of TAS's RMW
    # cycle, so every `tas` becomes a thunk. 22 in the listing, 8 of them
    # in the unreached second half. Thunks are per addressing form (the
    # shim provides them; None until assigned). (site, 4 bytes, thunk)
    'TAS_SITES': [
        (0x0240C, bytes([0x4A,0xEE,0x00,0x49]), None),  # tas 73(a6)
        (0x02458, bytes([0x4A,0xEE,0x00,0x49]), None),  # tas 73(a6)
        (0x024E2, bytes([0x4A,0xF8,0xEC,0x2A]), None),  # tas 0xFFEC2A.w
        (0x03596, bytes([0x4A,0xE8,0x00,0x03]), None),  # tas 3(a0)
        (0x04B86, bytes([0x4A,0xEE,0x00,0x48]), None),  # tas 72(a6)
        (0x08010, bytes([0x4A,0xEE,0x00,0x78]), None),  # tas 120(a6)
        (0x080A2, bytes([0x4A,0xEE,0x00,0x78]), None),  # tas 120(a6)
        (0x3789A, bytes([0x4A,0xEE,0x00,0x46]), None),  # tas 70(a6)
        (0x4506A, bytes([0x4A,0xE9,0x25,0xE1]), None),  # tas 0x25E1(a1)  bank 04: object code? unexecuted in traces
        (0x450C6, bytes([0x4A,0xE9,0x25,0xE1]), None),
        (0x450DA, bytes([0x4A,0xE9,0x25,0xE1]), None),
        (0x45198, bytes([0x4A,0xE9,0x25,0xE1]), None),
        (0x4545E, bytes([0x4A,0xE9,0x25,0xE1]), None),
        (0x45D0C, bytes([0x4A,0xE9,0x3D,0xF9]), None),  # tas 0x3DF9(a1)
        (0x45D62, bytes([0x4A,0xE9,0x25,0xE1]), None),
        (0x46156, bytes([0x4A,0xE9,0x25,0xE1]), None),
    ],
    'TAS_SITES_DUP': [0x55F0C, 0x5641E, 0x57A5C, 0x5AEA8],   # second half; 0x55F0C is an indexed form (4AF1 F27F)

    # ------------------------------------------------------------------
    # TEXT RAM (entry 10). 170 literal sites in the first half; text RAM
    # is a 64-column map, 128 bytes a row, rows 0-27 visible, the layer
    # registers at +0xE80.. (AB entry 11 / jts16_mmr.v).
    # Layer-register writers (AB's MDHSCR shape, `move.w d0,abs.l` = 33C0,
    # and the page selects): remapped to the port's shadow words, never
    # to the FB. IRQ4 writes the four scroll words every vint (0x2FA0-
    # 0x2FCA); the page selects are written at cuts.
    'LAYER_REG_SITES': [
        (0x02FA0, 0x33C0, 0x110E98, "scr1 hpos"), (0x02FAE, 0x33C0, 0x110E90, "scr1 vpos"),
        (0x02FBC, 0x33C0, 0x110E9A, "scr2 hpos"), (0x02FCA, 0x33C0, 0x110E92, "scr2 vpos"),
        (0x05A74, 0x33FC, 0x110E80, "scr1 pages #0x7777 (post-STOP reset)"), (0x05A7C, 0x33FC, 0x110E82, "scr2 pages #0"),
        (0x0639E, 0x33FC, 0x110E80, "scr1 pages #0x7777"), (0x063A6, 0x4279, 0x110E82, "clr.w scr2 pages"),
    ],
    # The shared text copy/clear loop heads — AB's 0x3A9A/0x3AA4 idiom
    # exactly: `move.b (a0)+,(a1)+ ; addq.l #1,a1 ; dbf` at 0x3EB0 and
    # `clr.b (a1) ; addq.l #2,a1 ; dbf` at 0x3EBA (the CLR read-modify-
    # write site). Entered by every text writer with a1 = destination;
    # the census's 0x3EB4/0x3EBE (next-PC) are these loops writing the
    # cutscene speech box (rows 14-25 from column 35) and other strings.
    'TXT_LOOP_HEADS': {'copy': 0x03EB0, 'clear': 0x03EBA},
    # AB's TXT_WRAM_WRITERS idiom, both members present:
    #  - credit line: 0x3EE2 `lea 0x110000,a1 ; adda.w 0xFFEC24,a1` (AB:
    #    0x3AAE with 0xFFF024), clears 9 glyphs stride 2 at 0x3EF2 (AB
    #    cleared 9 and wrote 10 words); the census's 0x3EF6 = 18,585 writes
    #    at 0x110BCE-0x110C60 (row 23, "CREDIT n" bottom centre-right).
    #  - HUD: 0xC750 writes both players' rows: magic pots at row 25 via
    #    0xC7B0 (word at a1+d1 and +128; leas 0xC764/0xC76A P1, 0xC790/0xC796
    #    P2) and name/score/health strings at rows 0-1 via 0xC8A6 (2 rows x
    #    10 words from string table (a1,d0*4); leas 0xC776 P1 = 0x110044,
    #    0xC7A2 P2 = 0x11005A). Per-player select: a0 = 0xFFEC28 / 0xFFEC29
    #    (the credited flags), a6/a5 = the player object 0xFFC000 / 0xFFC200.
    'TXT_WRAM_WRITERS': [
        {'site': 0x03EE2, 'reg': 1, 'off_var': 0xFFEC24, 'words': 9, 'loops': [0x03EB0, 0x03EBA], 'note': 'credit line'},
        {'site': 0x0C764, 'reg': 1, 'alt_sites': [0x0C76A, 0x0C790, 0x0C796], 'sel_var': 0xFFEC29,
         'ranges': [(0xCCA, 0xCD5), (0xD4A, 0xD55), (0xCDA, 0xCE5), (0xD5A, 0xD65)], 'helper': 0x0C7B0, 'note': 'magic pots row 25/26'},
        {'site': 0x0C776, 'reg': 0, 'alt_sites': [0x0C7A2], 'sel_var': 0xFFEC29,
         'ranges': [(0x044, 0x057), (0x0C4, 0x0D7), (0x05A, 0x06D), (0x0DA, 0x0ED)], 'helper': 0x0C8A6, 'note': 'name/score rows 0-1'},
    ],
    'TXT_WRAM_CLEAR_SITES': None,   # AB's scene-level text FILL entry (0x369C); GA's text clears are 0x3972 (16 KB? see 0x3962) and 0x1E7C/0x1FAA/0x2078/0x20A8/0x20E4 (lea 0x110000) — classify at rung 7
    'TEXT_IDIOM': None,             # AB's movew->addw family (abs.w low-word trap on the 0xFF8000 mirror); re-derive once the text remap destination is fixed
    # Every first-half text literal by text-RAM row (offset >> 7), for the
    # rung-7 classification: 22 sites on row 0, 16 on row 25, 8 on row 4,
    # 8 on row 10, the rest scattered; 8 layer-register sites above.
    # Full list: python3 -c "..." over roms/goldnaxe/prog68k.asm, or
    # LOOP-DECOMPILE-GOLDNAXE 10.
    # ------------------------------------------------------------------
    # NOT YET DERIVED — each names the census that derives it.
    'FMGATE_ENTRIES': None, 'FMGATE_SPANS': None,   # FB-writer entries: tile loaders 0x1FE2-0x2066, 0x399E, text writers
    # Every 0x10xxxx/0x11xxxx/0x14xxxx operand objdump prints was listed
    # (95 palette, 57 tile, 170 text); all sit in instruction context. The
    # one data run that decodes to a hardware-looking literal is the ASCII
    # at 0x6818-0x6830 ("M   ") = `move.l 0x202020,d0` at 0x6824, outside
    # the 2 KB sprite window and outside every MEMMAP range. Nothing to
    # exclude yet; re-run the check when TEXT sites are thunked.
    'DATA_EXCLUDE': [],
    'BOOT_JUMPINS': None, 'BOOT_PCREL': None,   # boot copy region for this title is TBD (boot runs from ROM 0x40E; no RAM copy seen yet)
    'SPAWN_META': None, 'SPAWN_LO': None, 'SPAWN_CAP': None,
    'REBASE_TABLES': None, 'HARVESTED_HANDLERS': None, 'HARVEST_BLACKLIST': None, 'HARVEST_BOUND': None,
    'STRIDE_TABLES': None, 'IMM_OVERRIDES': None, 'LOW_VECTOR_READS': None, 'ABSW_JMP': None,
    'DISPATCHERS': None, 'DATA_PTR_NORM': None, 'REBASE_EXCLUDE': None,
}
