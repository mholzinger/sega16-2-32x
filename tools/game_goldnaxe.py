"""*** NOT A USABLE TABLE YET (2026-09-12, LOOP-DECOMPILE-GOLDNAXE 7) ***
This is the RAW output of game_derive.py altbeast goldnaxe. Golden Axe is a
different program, not a re-link of Altered Beast, so the alignment that
carried altbeastj (98.8% one-to-one) maps almost nothing here: dirty sites
0/25 and 0/42, TAS 0/5, REBASE_TABLES 0/7, and the entries it did map are
UNVERIFIED and partly false (HARVESTED_HANDLERS 0x102/0x104/0x106 are
vector-table addresses, not handlers). Only MCU_SND = 0xFFECFC and
MCU_COINS = 0xFFEC96 are confirmed, by trace (entry 6) and the MCU tap
(entry 3). Every other key is to be hand-derived from the write census
(docs/audit/goldnaxe/write_census.txt) and the consuming instructions,
and the memory map is NOT Altered Beast's (entry 2): tile RAM 0x100000,
text 0x110000, palette 0x140000, sprites via 0xFFECC4, tile bank
0x1F2001/3, math chips 0x1F0000/0x1F1000/0x1E0000.
"""
"""goldnaxe: patch tables DERIVED from tools/game_altbeast.py by
tools/game_derive.py (disassembly alignment + per-site byte checks).
Regenerate with:  python3 tools/game_derive.py altbeast goldnaxe
Hand corrections belong in this file AFTER the generated block, keyed
the same way, with the evidence cited. Derivation report:

    OK   BOOT_PCREL 19 fixups (scanner reproduces ref)
    OK   BOOT_JUMPINS [(2148, 'braw', 1616), (6124, 'braw', 1616), (6146, 'braw', 1616), (6308, 'jmpl', 1612), (6932, 'braw', 1612), (7032, 'braw', 1612), (24402, 'jmpw', 1116), (222300, 'jmpl', 1616), (225864, 'jmpl', 1616), (364038, 'jmpw', 1140)]
    MISS RLE_EVEN_PASS: 0x16cc unmapped
    MISS STRIP_BLITTER_MOVEW: 0x2590 unmapped
    MISS STRIP_BLITTER_BASE: 0x258a unmapped
    MISS TEXT_IDIOM: 0x1b4cc unmapped
    MISS TEXT_IDIOM: 0x1b660 unmapped
    MISS TEXT_IDIOM: 0x1b95c unmapped
    MISS TEXT_IDIOM: 0x1b964 unmapped
    MISS TEXT_IDIOM: 0x1b96e unmapped
    MISS TEXT_IDIOM: 0x1b97c unmapped
    MISS TEXT_IDIOM: 0x1b986 unmapped
    MISS TEXT_IDIOM: 0x1bae8 unmapped
    OK   TEXT_IDIOM 0/8
    MISS DISPATCHERS: 0xd842 unmapped
    OK   DISPATCHERS 1/2
    MISS DATA_PTR_NORM: 0xdba8 unmapped
    MISS DATA_PTR_NORM: 0x30d0 unmapped
    OK   DATA_PTR_NORM 0/2
    MISS TAS_SITES: 0x2268 unmapped
    MISS TAS_SITES: 0xe098 unmapped
    MISS TAS_SITES: 0xeac0 unmapped
    MISS TAS_SITES: 0x150b6 unmapped
    MISS TAS_SITES: 0x12e84 unmapped
    OK   TAS_SITES 0/5
    MISS TILE_DIRTY_SITES: 0xd12 unmapped
    MISS TILE_DIRTY_SITES: 0xd24 unmapped
    MISS TILE_DIRTY_SITES: 0xd36 unmapped
    MISS TILE_DIRTY_SITES: 0xd48 unmapped
    MISS TILE_DIRTY_SITES: 0xd5a unmapped
    MISS TILE_DIRTY_SITES: 0xd64 unmapped
    MISS TILE_DIRTY_SITES: 0xd6e unmapped
    MISS TILE_DIRTY_SITES: 0xd78 unmapped
    MISS TILE_DIRTY_SITES: 0xd9a unmapped
    MISS TILE_DIRTY_SITES: 0xda2 unmapped
    MISS TILE_DIRTY_SITES: 0x170a unmapped
    MISS TILE_DIRTY_SITES: 0x174e unmapped
    MISS TILE_DIRTY_SITES: 0x16be unmapped
    MISS TILE_DIRTY_SITES: 0x16de unmapped
    MISS TILE_DIRTY_SITES: 0x36b0 unmapped
    MISS TILE_DIRTY_SITES: 0x1acd8 -> 0x6ed6: bytes 41f9001000007000 fail check
    MISS TILE_DIRTY_SITES: 0x1a52c unmapped
    MISS TILE_DIRTY_SITES: 0x1a54c unmapped
    MISS TILE_DIRTY_SITES: 0x1a562 unmapped
    MISS TILE_DIRTY_SITES: 0x1b76a unmapped
    MISS TILE_DIRTY_SITES: 0x1b9fa unmapped
    MISS TILE_DIRTY_SITES: 0x1ba02 unmapped
    MISS TILE_DIRTY_SITES: 0x1ba0a unmapped
    MISS TILE_DIRTY_SITES: 0x1ba12 unmapped
    MISS TILE_DIRTY_SITES: 0x1ba50 unmapped
    OK   TILE_DIRTY_SITES 0/25
    MISS PAL_DIRTY_SITES: 0x1ef2 unmapped
    MISS PAL_DIRTY_SITES: 0x1f80 unmapped
    MISS PAL_DIRTY_SITES: 0x20a0 unmapped
    MISS PAL_DIRTY_SITES: 0x1a934 -> 0x6b78: bytes 33fc088800140000 fail check
    MISS PAL_DIRTY_SITES: 0x1a93c -> 0x6b80: bytes 33fc0fff00140002 fail check
    MISS PAL_DIRTY_SITES: 0x1a944 -> 0x6b88: bytes 33fc088800140004 fail check
    MISS PAL_DIRTY_SITES: 0x1a94c -> 0x6b90: bytes 33fc000000140006 fail check
    MISS PAL_DIRTY_SITES: 0x1a954 -> 0x6b98: bytes 33fc088800140010 fail check
    MISS PAL_DIRTY_SITES: 0x1a95c -> 0x6ba0: bytes 33fc0ff000140012 fail check
    MISS PAL_DIRTY_SITES: 0x1a964 -> 0x6ba8: bytes 33fc088800140014 fail check
    MISS PAL_DIRTY_SITES: 0x1a96c -> 0x6bb0: bytes 33fc000000140016 fail check
    MISS PAL_DIRTY_SITES: 0x1a974 -> 0x6bb8: bytes 33fc088800140020 fail check
    MISS PAL_DIRTY_SITES: 0x1a97c -> 0x6bc0: bytes 33fc00ff00140022 fail check
    MISS PAL_DIRTY_SITES: 0x1a984 -> 0x6bc8: bytes 33fc088800140024 fail check
    MISS PAL_DIRTY_SITES: 0x1a98c -> 0x6bd0: bytes 33fc000000140026 fail check
    MISS PAL_DIRTY_SITES: 0x1b0e6 unmapped
    MISS PAL_DIRTY_SITES: 0x1b0ee unmapped
    MISS PAL_DIRTY_SITES: 0x1b0f6 unmapped
    MISS PAL_DIRTY_SITES: 0x1b0fe unmapped
    MISS PAL_DIRTY_SITES: 0x1b106 unmapped
    MISS PAL_DIRTY_SITES: 0x1b10e unmapped
    MISS PAL_DIRTY_SITES: 0x1b116 unmapped
    MISS PAL_DIRTY_SITES: 0x1b11e unmapped
    MISS PAL_DIRTY_SITES: 0x1b126 unmapped
    MISS PAL_DIRTY_SITES: 0x1b12e unmapped
    MISS PAL_DIRTY_SITES: 0x1b136 unmapped
    MISS PAL_DIRTY_SITES: 0x1b13e unmapped
    MISS PAL_DIRTY_SITES: 0x25ba unmapped
    MISS PAL_DIRTY_SITES: 0x25e8 unmapped
    MISS PAL_DIRTY_SITES: 0x25f0 unmapped
    MISS PAL_DIRTY_SITES: 0x263c unmapped
    MISS PAL_DIRTY_SITES: 0x2b7e unmapped
    MISS PAL_DIRTY_SITES: 0x2b94 unmapped
    MISS PAL_DIRTY_SITES: 0x2bb8 unmapped
    MISS PAL_DIRTY_SITES: 0x3116 unmapped
    MISS PAL_DIRTY_SITES: 0x3846 -> 0x3b60: bytes 43f900140040303c fail check
    MISS PAL_DIRTY_SITES: 0x3952 -> 0x3c64: bytes 207c00140000227c fail check
    MISS PAL_DIRTY_SITES: 0x4544 unmapped
    MISS PAL_DIRTY_SITES: 0x170ba unmapped
    MISS PAL_DIRTY_SITES: 0x1a4f0 unmapped
    MISS PAL_DIRTY_SITES: 0x1b742 unmapped
    MISS PAL_DIRTY_SITES: 0x1bab6 unmapped
    OK   PAL_DIRTY_SITES 0/42
    MISS PAL_THUNK_A: 0x30c2 unmapped
    OK   PAL_THUNK_B 2/2
    MISS FMGATE_ENTRIES: 0x153e unmapped
    MISS FMGATE_ENTRIES: 0x16be unmapped
    MISS FMGATE_ENTRIES: 0x16de unmapped
    MISS FMGATE_ENTRIES: 0x170a unmapped
    MISS FMGATE_ENTRIES: 0x174e unmapped
    MISS FMGATE_ENTRIES: 0x2552 unmapped
    MISS FMGATE_ENTRIES: 0x2564 unmapped
    MISS FMGATE_ENTRIES: 0x2572 unmapped
    MISS FMGATE_ENTRIES: 0x2580 unmapped
    MISS FMGATE_ENTRIES: 0x36b0 unmapped
    MISS FMGATE_ENTRIES: 0x36c4 unmapped
    MISS FMGATE_ENTRIES: 0x1a52c unmapped
    MISS FMGATE_ENTRIES: 0x3aae unmapped
    MISS FMGATE_ENTRIES: 0x4d88 unmapped
    MISS FMGATE_ENTRIES: 0x56e8 unmapped
    MISS FMGATE_ENTRIES: 0x64fc unmapped
    MISS FMGATE_ENTRIES: 0x6558 unmapped
    OK   FMGATE_ENTRIES 10/27
    MISS FMGATE_SPANS: (0x153e,0x155c) -> (None,None)
    MISS FMGATE_SPANS: (0x16be,0x1772) -> (None,None)
    MISS FMGATE_SPANS: (0x2550,0x25aa) -> (None,None)
    MISS FMGATE_SPANS: (0x4d80,0x4d98) -> (None,None)
    MISS FMGATE_SPANS: (0x56e8,0x5742) -> (None,None)
    MISS FMGATE_SPANS: (0x1a52c,0x1a59e) -> (None,None)
    MISS FMGATE_SPANS: (0x64fc,0x6506) -> (None,None)
    MISS FMGATE_SPANS: (0x6558,0x6562) -> (None,None)
    OK   FMGATE_SPANS 3/11
    MISS IMM_OVERRIDES: 0x3c92 near 0x40d2: 0 candidates
    OK   IMM_OVERRIDES []
    MISS LOW_VECTOR_READS: 0x14932 unmapped
    MISS LOW_VECTOR_READS: 0x1493e unmapped
    MISS LOW_VECTOR_READS: 0xabc4 unmapped
    OK   LOW_VECTOR_READS 1/4
    MISS ABSW_JMP: 0x1b5c6 -> None
    MISS DATA_EXCLUDE: (0x1986,0x1998): start unmapped
    MISS DATA_EXCLUDE: (0x1d2dc,0x1d520): start unmapped
    OK   DATA_EXCLUDE 0/2
    MISS REBASE_EXCLUDE: (0x6dc0,0x6dca): start unmapped
    MISS REBASE_EXCLUDE: (0x7358,0x73a0): start unmapped
    MISS REBASE_EXCLUDE: (0xec32,0xec46): start unmapped
    MISS REBASE_EXCLUDE: (0xecac,0xecb0): start unmapped
    OK   REBASE_EXCLUDE 1/5
    MISS REBASE_TABLES: 0x26dc unmapped
    MISS REBASE_TABLES: 0x6d70 unmapped
    MISS REBASE_TABLES: 0x6d90 unmapped
    MISS REBASE_TABLES: 0x92f0 unmapped
    MISS REBASE_TABLES: 0xf556 unmapped
    MISS REBASE_TABLES: 0x17e24 unmapped
    MISS REBASE_TABLES: 0x1a076 unmapped
    OK   REBASE_TABLES 0/7
    MISS STRIDE_TABLES: 0x1ce2 -> 0x2a20: records not pointers
    OK   STRIDE_TABLES []
    MISS SPAWN_META: (None, None, None, None)
    MISS PAL_LAUNCH: [None, None] None
    OK   HARVEST_BOUND 0x28000 (unmapped; reference value kept)
    OK   HARVESTED_HANDLERS 4 mapped, 39 unmapped ['0x2490', '0xdabc', '0x1a70e', '0x49ba', '0x3000a', '0x16bf0', '0xee80', '0x103e0', '0x10824', '0x8106', '0x8104', '0x8102', '0x30008', '0x55ac', '0x56e8', '0x5618', '0x57bc', '0x566e', '0xdc70', '0x5854', '0xf90a', '0xfd6c', '0x10058', '0x1004c', '0xf352', '0x54e6', '0x54fc', '0x6cb8', '0x24c0', '0xb0a0', '0xb1aa', '0xb1e6', '0xb182', '0xb2cc', '0x13374', '0x137cc', '0x12cba', '0x12f0a', '0x137e0']
    MISS MCU_BUSY: votes {}
    OK   MCU_COINS 0xfff0c2 -> 0xffec96 (sites 2)
    OK   MCU_SND 0xfff0c4 -> 0xffecfc (sites 2)
"""

TABLES = {
    # 'MCU_BUSY': NOT DERIVED — see report
    'MCU_COINS': 0xFFEC96,
    'MCU_SND': 0xFFECFC,
    'DATA_EXCLUDE': [],
    # 'RLE_EVEN_PASS': NOT DERIVED — see report
    # 'STRIP_BLITTER_MOVEW': NOT DERIVED — see report
    # 'STRIP_BLITTER_BASE': NOT DERIVED — see report
    'TEXT_IDIOM': [],
    'BOOT_JUMPINS': [
        (0x864, 'braw', 0x650),
        (0x17EC, 'braw', 0x650),
        (0x1802, 'braw', 0x650),
        (0x18A4, 'jmpl', 0x64C),
        (0x1B14, 'braw', 0x64C),
        (0x1B78, 'braw', 0x64C),
        (0x5F52, 'jmpw', 0x45C),
        (0x3645C, 'jmpl', 0x650),
        (0x37248, 'jmpl', 0x650),
        (0x58E06, 'jmpw', 0x474),
    ],
    'BOOT_PCREL': [
        (0x404, [96, 0, 43, 90], [78, 248, 47, 96]),
        (0x4B0, [97, 0, 26, 88], [78, 184, 31, 10]),
        (0x4BC, [97, 0, 26, 76], [78, 184, 31, 10]),
        (0x520, [65, 250, 39, 38], [65, 248, 44, 72]),
        (0x532, [65, 250, 39, 24], [65, 248, 44, 76]),
        (0x608, [67, 250, 40, 14], [67, 248, 46, 24]),
        (0x6A2, [97, 0, 21, 168], [78, 184, 28, 76]),
        (0x6D0, [65, 250, 35, 216], [65, 248, 42, 170]),
        (0x6F4, [97, 0, 21, 214], [78, 184, 28, 204]),
        (0x72A, [97, 0, 23, 80], [78, 184, 30, 124]),
        (0x74C, [97, 0, 18, 126], [78, 184, 25, 204]),
        (0x76A, [97, 0, 24, 50], [78, 184, 31, 158]),
        (0x782, [97, 0, 22, 248], [78, 184, 30, 124]),
        (0x79E, [96, 0, 3, 130], [78, 248, 11, 34]),
        (0x7A8, [96, 0, 3, 120], [78, 248, 11, 34]),
        (0x7B2, [96, 0, 3, 110], [78, 248, 11, 34]),
        (0x7BC, [96, 0, 3, 100], [78, 248, 11, 34]),
        (0x7C6, [96, 0, 3, 90], [78, 248, 11, 34]),
        (0x7F6, [97, 0, 0, 112], [78, 184, 8, 104]),
    ],
    # 'SPAWN_META': NOT DERIVED — see report
    # 'SPAWN_LO': NOT DERIVED — see report
    # 'SPAWN_CAP': NOT DERIVED — see report
    'REBASE_TABLES': [],
    'HARVESTED_HANDLERS': [0x10000, 0x106, 0x104, 0x102],
    'HARVEST_BLACKLIST': [0x10000, 0x102, 0x104, 0x106],
    'HARVEST_BOUND': 0x28000,
    'STRIDE_TABLES': [],
    'IMM_OVERRIDES': [],
    'LOW_VECTOR_READS': [0x354E],
    # 'ABSW_JMP': NOT DERIVED — see report
    'DISPATCHERS': [
        (0x3CEA, [32, 110, 0, 2, 78, 144], 0xB3A0),
    ],
    'DATA_PTR_NORM': [],
    'TAS_SITES': [],
    'TILE_DIRTY_SITES': [],
    'PAL_DIRTY_SITES': [],
    # 'PAL_THUNK_A': NOT DERIVED — see report
    # 'PAL_THUNK_APOST': NOT DERIVED — see report
    'PAL_THUNK_B': [0x328C, 0x409A],
    'FMGATE_ENTRIES': [
        (0x3962, 6, 0x207C, 'moveal #text,%a0 (17-caller alt entry)'),
        (0x3A20, 4, 0x41ED, 'lea 8(%a5),%a0'),
        (0x3A30, 6, 0x838, 'btst #0,flag (flags survive: displaced last)'),
        (0x6EC8, 6, 0x41F9, 'round clear-all (lea text)'),
        (0x3AA8, 4, 0x41F8, 'lea wram,%a0'),
        (0x3B24, 6, 0x838, 'btst #0,flag'),
        (0x3B52, 4, 0x41FA, 'lea pc-rel,%a0 -> ABSOLUTE rewrite'),
        (0x3B72, 6, 0x838, 'btst #0,flag'),
        (0x3EB0, 4, 0x12D8, 'moveb copy loop head (dbf re-enters gate)'),
        (0x3EBA, 4, 0x4211, 'clrb clear loop head (the CLR RMW site)'),
    ],
    'FMGATE_SPANS': [
        (0x3892, 0x3C62),
        (0x3EB0, 0x3F3C),
        (0x6EC8, 0x6EEA),
    ],
    # 'TXT_WRAM_CLEAR_SITES': NOT DERIVED — see report
    # 'TXT_WRAM_WRITERS': NOT DERIVED — see report
    # 'PAL_LAUNCH': NOT DERIVED — see report
    'REBASE_EXCLUDE': [
        (0x6F0E, 0x6F32),
    ],
}
