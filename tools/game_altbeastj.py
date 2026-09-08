"""altbeastj: patch tables DERIVED from tools/game_altbeast.py by
tools/game_derive.py (disassembly alignment + per-site byte checks).
Regenerate with:  python3 tools/game_derive.py altbeast altbeastj
Hand corrections belong in this file AFTER the generated block, keyed
the same way, with the evidence cited. Derivation report:

    OK   BOOT_PCREL 13 fixups (scanner reproduces ref)
    OK   BOOT_JUMPINS [(2466, 'bsrw', 1448), (3068, 'braw', 1620), (7958, 'jmpl', 1512), (112094, 'jmpw', 1150)]
    OK   RLE_EVEN_PASS 0x16f6
    OK   STRIP_BLITTER_MOVEW 0x25ae
    OK   STRIP_BLITTER_BASE 0x25a8
    OK   TEXT_IDIOM 8/8
    OK   DISPATCHERS 2/2
    OK   DATA_PTR_NORM 2/2
    OK   TAS_SITES 5/5
    OK   TILE_DIRTY_SITES 25/25
    OK   PAL_DIRTY_SITES 42/42
    OK   PAL_THUNK_A 0x30da
    OK   PAL_THUNK_B 2/2
    OK   FMGATE_ENTRIES 25/25
    OK   FMGATE_SPANS 9/9
    OK   IMM_OVERRIDES [(15532, 153056)]
    OK   LOW_VECTOR_READS 4/4
    OK   ABSW_JMP (112094, 1150)
    NOTE DATA_EXCLUDE (0x1986,0x1998) -> (0x19b4,0x19c6): bytes differ from reference
    NOTE DATA_EXCLUDE (0x1d2dc,0x1d520) -> (0x1d2f4,0x1d538): bytes differ from reference
    OK   DATA_EXCLUDE 2/2
    OK   REBASE_EXCLUDE 5/5
    NOTE REBASE_TABLES 0x6d90[4]: 0x6dca -> 0x6de2 by the table's own shift (sub-table pointer (data); verify in MAME)
    NOTE REBASE_TABLES 0x6d90[5]: 0x6e22 -> 0x6e3a by the table's own shift (handler in an unaligned regional block; verify in MAME)
    NOTE REBASE_TABLES 0x6d90[6]: 0x6e7a -> 0x6e92 by the table's own shift (handler in an unaligned regional block; verify in MAME)
    NOTE REBASE_TABLES 0x6d90[7]: 0x6ed2 -> 0x6eea by the table's own shift (handler in an unaligned regional block; verify in MAME)
    NOTE REBASE_TABLES 0x6d90[8]: 0x6f2a -> 0x6f42 by the table's own shift (handler in an unaligned regional block; verify in MAME)
    NOTE REBASE_TABLES 0x6d90[9]: 0x6f82 -> 0x6f9a by the table's own shift (handler in an unaligned regional block; verify in MAME)
    NOTE REBASE_TABLES 0x6d90[10]: 0x6fda -> 0x6ff2 by the table's own shift (handler in an unaligned regional block; verify in MAME)
    NOTE REBASE_TABLES 0x6d90[11]: 0x6ed2 -> 0x6eea by the table's own shift (handler in an unaligned regional block; verify in MAME)
    NOTE REBASE_TABLES 0x92f0[5]: 0x10800 is not an instruction start in either program (table over-extent, harmless)
    OK   REBASE_TABLES 7/7
    OK   STRIDE_TABLES [(7436, 8, 6, 2)]
    OK   SPAWN_META (0x1d342,0x1d356) lo 0x1d318 cap 0x1e018 entries ['0x1d360', '0x1d4ee', '0x1d538', '0x1d582', '0x1d584']
    OK   PAL_LAUNCH ['0x1a720', '0x1a726'] -> 0x1a7aa
    OK   HARVEST_BOUND 0x27e70
    OK   HARVESTED_HANDLERS 43 mapped, 0 unmapped []
    NOTE boot region 0x7f8: short branch to 0x82e leaves the copied window (inert: only the 0x408 stub executes from RAM)
    NOTE boot region 0x806: short branch to 0x828 leaves the copied window (inert: only the 0x408 stub executes from RAM)
    OK   MCU_BUSY 0xfff0c0 -> 0xfff0d2 (sites 2)
    OK   MCU_COINS 0xfff0c2 -> 0xfff0d0 (sites 2)
    OK   MCU_SND 0xfff0c4 -> 0xfff0d4 (sites 7)
"""

TABLES = {
    'MCU_BUSY': 0xFFF0D2,
    'MCU_COINS': 0xFFF0D0,
    'MCU_SND': 0xFFF0D4,
    'DATA_EXCLUDE': [
        (0x19B4, 0x19C6),
        (0x1D2F4, 0x1D538),
    ],
    'RLE_EVEN_PASS': 0x16F6,
    'STRIP_BLITTER_MOVEW': 0x25AE,
    'STRIP_BLITTER_BASE': 0x25A8,
    'TEXT_IDIOM': [
        (0x1B4E4, 0x3039, 0xD079),
        (0x1B678, 0x3039, 0xD079),
        (0x1B974, 0x303C, 0xD07C),
        (0x1B97C, 0x303C, 0xD07C),
        (0x1B986, 0x303C, 0xD07C),
        (0x1B994, 0x303C, 0xD07C),
        (0x1B99E, 0x303C, 0xD07C),
        (0x1BB00, 0x301B, 0xD05B),
    ],
    'BOOT_JUMPINS': [
        (0x9A2, 'bsrw', 0x5A8),
        (0xBFC, 'braw', 0x654),
        (0x1F16, 'jmpl', 0x5E8),
        (0x1B5DE, 'jmpw', 0x47E),
    ],
    'BOOT_PCREL': [
        (0x404, [96, 0, 38, 196], [78, 248, 42, 202]),
        (0x440, [65, 250, 21, 114], [65, 248, 25, 180]),
        (0x4D2, [97, 0, 16, 46], [78, 184, 21, 2]),
        (0x4DE, [97, 0, 16, 34], [78, 184, 21, 2]),
        (0x5A4, [67, 250, 23, 142], [67, 248, 29, 52]),
        (0x5FC, [97, 0, 13, 50], [78, 184, 19, 48]),
        (0x636, [65, 250, 18, 54], [65, 248, 24, 110]),
        (0x682, [65, 250, 17, 238], [65, 248, 24, 114]),
        (0x68C, [65, 250, 22, 118], [65, 248, 29, 4]),
        (0x6E2, [97, 0, 12, 126], [78, 184, 19, 98]),
        (0x752, [65, 250, 17, 38], [65, 248, 24, 122]),
        (0x782, [97, 0, 6, 134], [78, 184, 14, 10]),
        (0x786, [97, 0, 15, 54], [78, 184, 22, 190]),
    ],
    'SPAWN_META': (0x1D342, 0x1D356),
    'SPAWN_LO': 0x1D318,
    'SPAWN_CAP': 0x1E018,
    'REBASE_TABLES': [
        (0x26FA, 8),
        (0x6D88, 8),
        (0x6DA8, 12),
        (0x9308, 6),
        (0xF56E, 5),
        (0x17E3C, 5),
        (0x1A08E, 9),
    ],
    'HARVESTED_HANDLERS': [0x24BA, 0xDAD6, 0x1A72A, 0x49D2, 0x3000A, 0x16C08, 0xEE98, 0x10000, 0x103F8, 0x1083C, 0x106, 0x104, 0x102, 0x811E, 0x811C, 0x811A, 0x30008, 0x55C4, 0x5700, 0x5630, 0x57D4, 0x5686, 0xDC8A, 0x586C, 0xF922, 0xFD82, 0x1006E, 0x10062, 0xF36A, 0x54FE, 0x5514, 0x6CD0, 0x24EA, 0xB0B8, 0xB1C2, 0xB1FE, 0xB19A, 0xB2E4, 0x1338C, 0x137E4, 0x12CD2, 0x12F22, 0x137F8],
    'HARVEST_BLACKLIST': [0x10000, 0x102, 0x104, 0x106],
    'HARVEST_BOUND': 0x27E70,
    'STRIDE_TABLES': [
        (0x1D0C, 8, 6, 2),
    ],
    'IMM_OVERRIDES': [
        (0x3CAC, 0x255E0),
    ],
    'LOW_VECTOR_READS': [0x1494A, 0x14956, 0x3092, 0xABDC],
    'ABSW_JMP': (0x1B5DE, 0x47E),
    'DISPATCHERS': [
        (0x39C2, [32, 110, 0, 2, 78, 144], 0xB3A0),
        (0xD85C, [34, 104, 0, 8, 78, 145], 0xB3C0),
    ],
    'DATA_PTR_NORM': [
        (0xDBC2, [40, 110, 0, 36], 0xB340),
        (0x30E8, [32, 109, 0, 2], 0xB360),
    ],
    'TAS_SITES': [
        (0x2292, bytes([0x4A, 0xF8, 0xC0, 0x20]), 0xB380),
        (0xE0B0, bytes([0x4A, 0xF8, 0xF1, 0x5A]), 0xB38A),
        (0xEAD8, bytes([0x4A, 0xE8, 0x00, 0x3E]), 0xB394),
        (0x150CE, bytes([0x4A, 0xE8, 0x00, 0x3E]), 0xB394),
        (0x12E9C, bytes([0x4A, 0xEE, 0x00, 0x3C]), 0xB3F6),
    ],
    'TILE_DIRTY_SITES': [
        (0xD3C, 0x43F9, 0x852518, 1),
        (0xD4E, 0x43F9, 0x852C98, 1),
        (0xD60, 0x43F9, 0x852596, 1),
        (0xD72, 0x43F9, 0x8525B8, 1),
        (0xD84, 0x43F9, 0x852516, 1),
        (0xD8E, 0x43F9, 0x852538, 1),
        (0xD98, 0x43F9, 0x852C96, 1),
        (0xDA2, 0x43F9, 0x852CB8, 1),
        (0xDC4, 0x43F9, 0x852598, 1),
        (0xDCC, 0x43F9, 0x857598, 32),
        (0x1734, 0x41F9, 0x85D230, 0x800),
        (0x1778, 0x41F9, 0x85C230, 0x400),
        (0x16E8, 0x41F9, 0x852000, 0x1FFF),
        (0x1708, 0x41F9, 0x852001, 0x1FFF),
        (0x36C8, 0x207C, 0x852000, 0x1FFF),
        (0x1ACF4, 0x41F9, 0x852000, 0x1FFF),
        (0x1A544, 0x41F9, 0x852494, 1),
        (0x1A564, 0x41F9, 0x857000, 32),
        (0x1A57A, 0x41F9, 0x857516, 32),
        (0x1B782, 0x47F9, 0x853000, 2),
        (0x1BA12, 0x41F9, 0x8520B2, 1),
        (0x1BA1A, 0x41F9, 0x852D32, 1),
        (0x1BA22, 0x41F9, 0x8520B2, 1),
        (0x1BA2A, 0x41F9, 0x8520FC, 1),
        (0x1BA68, 0x43F9, 0x85223A, 1),
    ],
    'PAL_DIRTY_SITES': [
        (0x1F1C, 6, 1, 'clr.w 0xFF9000'),
        (0x1FAA, 6, 1, 'clr.w 0xFF9000'),
        (0x20CA, 6, 1, 'clr.w 0xFF9000'),
        (0x1A950, 8, 1, 'move.w #imm,0xFF9000'),
        (0x1A958, 8, 1, 'move.w #imm,0xFF9002'),
        (0x1A960, 8, 1, 'move.w #imm,0xFF9004'),
        (0x1A968, 8, 1, 'move.w #imm,0xFF9006'),
        (0x1A970, 8, 1, 'move.w #imm,0xFF9010'),
        (0x1A978, 8, 1, 'move.w #imm,0xFF9012'),
        (0x1A980, 8, 1, 'move.w #imm,0xFF9014'),
        (0x1A988, 8, 1, 'move.w #imm,0xFF9016'),
        (0x1A990, 8, 1, 'move.w #imm,0xFF9020'),
        (0x1A998, 8, 1, 'move.w #imm,0xFF9022'),
        (0x1A9A0, 8, 1, 'move.w #imm,0xFF9024'),
        (0x1A9A8, 8, 1, 'move.w #imm,0xFF9026'),
        (0x1B0FE, 8, 1, 'move.w #imm,0xFF9000'),
        (0x1B106, 8, 1, 'move.w #imm,0xFF9002'),
        (0x1B10E, 8, 1, 'move.w #imm,0xFF9004'),
        (0x1B116, 8, 1, 'move.w #imm,0xFF9006'),
        (0x1B11E, 8, 1, 'move.w #imm,0xFF9010'),
        (0x1B126, 8, 1, 'move.w #imm,0xFF9012'),
        (0x1B12E, 8, 1, 'move.w #imm,0xFF9014'),
        (0x1B136, 8, 1, 'move.w #imm,0xFF9016'),
        (0x1B13E, 8, 1, 'move.w #imm,0xFF9020'),
        (0x1B146, 8, 1, 'move.w #imm,0xFF9022'),
        (0x1B14E, 8, 1, 'move.w #imm,0xFF9024'),
        (0x1B156, 8, 1, 'move.w #imm,0xFF9026'),
        (0x25D8, 6, 128, '0x2612 helper, d1=12: 13*16B at 0x720 -> 0x7EF'),
        (0x2606, 6, 4, '0x2612 helper, d1=9: 10*16B at 0x250 -> 0x2EF'),
        (0x260E, 6, 1, '0x2612 helper, d1=0: 16B at 0x0B0 -> 0x0BF'),
        (0x265A, 6, 4, '0x2612 helper, d1=9: 10*16B at 0x250 -> 0x2EF'),
        (0x2B9C, 6, 240, 'table 0x326E rounds 0-4: 0x400 -> 0x71F'),
        (0x2BB2, 6, 1, 'single move.w at 0x2BA4 -> 0x06C'),
        (0x2BD6, 6, 1, '32 longs = 128B at 0x000 -> 0x07F'),
        (0x312E, 6, 1, '8 longs = 32B at 0x040 -> 0x05F'),
        (0x3860, 6, 1, '8 longs = 32B at 0x040 -> 0x05F'),
        (0x396C, 6, 255, '2 x 256 longs = 2048B at 0x000 -> 0x7FF'),
        (0x455C, 6, 1, '20 longs = 80B at 0x010 -> 0x05F'),
        (0x170D2, 6, 240, 'table 0x1724C rounds 0-4: 0x400 -> 0x71F'),
        (0x1A508, 6, 48, '19*16B at 0x490 -> 0x5BF'),
        (0x1B75A, 6, 1, '7*16B at 0x010 -> 0x07F'),
        (0x1BACE, 6, 7, '40*8 words = 640B at 0x080 -> 0x2FF'),
    ],
    'PAL_THUNK_A': 0x30DA,
    'PAL_THUNK_B': [0x2DE0, 0x3C74],
    'FMGATE_ENTRIES': [
        (0x1568, 6, 0x227C, 'moveal #txt,%a1'),
        (0x16E8, 6, 0x41F9, 'lea tiles,%a0'),
        (0x1708, 6, 0x41F9, 'lea tiles+1,%a0'),
        (0x1734, 6, 0x41F9, 'lea tiles,%a0'),
        (0x1778, 6, 0x41F9, 'lea tiles,%a0'),
        (0x2570, 6, 0x41F9, 'lea rom tbl,%a0'),
        (0x2582, 6, 0x41F9, 'lea rom tbl,%a0'),
        (0x2590, 6, 0x41F9, 'lea rom tbl,%a0'),
        (0x259E, 6, 0x41F9, 'lea rom tbl,%a0'),
        (0x36B4, 6, 0x207C, 'moveal #text,%a0 (17-caller alt entry)'),
        (0x36C8, 6, 0x207C, 'moveal #tiles,%a0'),
        (0x36DC, 6, 0x207C, 'moveal #sprram,%a0'),
        (0x371E, 4, 0x41ED, 'lea 8(%a5),%a0'),
        (0x372E, 6, 0x838, 'btst #0,flag (flags survive: displaced last)'),
        (0x1A544, 6, 0x41F9, 'round clear: crystal ball (lea page0)'),
        (0x1ACE6, 6, 0x41F9, 'round clear-all (lea text)'),
        (0x37A8, 4, 0x41F8, 'lea wram,%a0'),
        (0x3824, 6, 0x838, 'btst #0,flag'),
        (0x3852, 4, 0x41FA, 'lea pc-rel,%a0 -> ABSOLUTE rewrite'),
        (0x3872, 6, 0x838, 'btst #0,flag'),
        (0x3AB4, 4, 0x12D8, 'moveb copy loop head (dbf re-enters gate)'),
        (0x3ABE, 4, 0x4211, 'clrb clear loop head (the CLR RMW site)'),
        (0x3AC8, 6, 0x43F9, 'lea text,%a1'),
        (0x4DA0, 4, 0x2029, 'movel 16(%a1),%d0'),
        (0x5700, 4, 0x522E, 'addqb frame ctr (follower rewrites CCR)'),
    ],
    'FMGATE_SPANS': [
        (0x1568, 0x1586),
        (0x16E8, 0x179C),
        (0x256E, 0x25C8),
        (0x35E4, 0x396A),
        (0x3AB4, 0x3B16),
        (0x4D98, 0x4DB0),
        (0x5700, 0x575A),
        (0x1A544, 0x1A5B6),
        (0x1ACE6, 0x1AD08),
    ],
    'PAL_LAUNCH': ([0x1A720, 0x1A726], 0x1A7AA),
    'REBASE_EXCLUDE': [
        (0x6DD8, 0x6DE2),
        (0x1AD28, 0x1AD4C),
        (0x7370, 0x73B8),
        (0xEC4A, 0xEC5E),
        (0xECC4, 0xECC8),
    ],
}
