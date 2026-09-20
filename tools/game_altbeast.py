"""altbeast (US, set 8): every program-specific offset the patcher needs.

These are the HAND-DERIVED tables (LOOP 8/22/23, burn-down catches,
Mike's states) that patch_game.py used to carry as literals. The patcher
is game-agnostic now: it reads TABLES[key] and fails loudly on a missing
key. A second title gets its own tools/game_<name>.py, derived from this
one by tools/game_derive.py (disassembly alignment + per-site byte
verification), then corrected by hand where the derivation reports a
miss.

Offsets are program-ROM offsets (0x000000-0x03FFFF). Sites are verified
by the patcher against the program bytes before any patch lands.
"""
from pathlib import Path

_ROOT = Path(__file__).resolve().parent.parent
# runtime-harvested object-handler values (see TOOLKIT.md): every distinct
# handler pointer observed live in the working RV=1 build
_hh = _ROOT / 'tools' / 'harvested_handlers.txt'
HARVESTED = [int(x, 16) for x in _hh.read_text().split() if x.strip()] \
    if _hh.exists() else []

TABLES = {
    # i8751 MCU <-> 68K mailboxes in work RAM: +0 screen-sync busy word,
    # +2 coin/service byte the MCU posts (high byte, inverted), +4 sound
    # command word (0xFF = idle). The US MCU (317-0078) uses 0xFFF0C0/C2/C4;
    # the Japanese MCU (317-0077) uses 0xFFF0D0/D2/D4 in a DIFFERENT order —
    # the 68K program reads only its own MCU's addresses (2026-09-05: the JP
    # rom took no coins on ares). Derived per key from the read sites.
    'MCU_BUSY':  0xFFF0C0,   # screen-sync busy word (68K sets high byte)
    'MCU_COINS': 0xFFF0C2,   # coin/service byte the MCU posts (inverted)
    'MCU_SND':   0xFFF0C4,   # sound command byte (0xFF = idle)
    # regions the operand scan must not touch (raw data)
    'DATA_EXCLUDE': [
        (0x1986, 0x1998),   # boot mapper table (raw bytes, not code)
        (0x1D2DC, 0x1D520), # level event/spawn script: 12-byte records
                            # [camX.w][p1.w][p2.w][0x0040.w][handler.l] — the
                            # 0x0040,0x0000 word pairs LOOK like 0x00400000
                            # tile-RAM operands in objdump's misdisassembly;
                            # patching one corrupted a round-1 spawn (red blob)
    ],
    # tilemap RLE even-byte pass: moveb (a1)+,d2 at +0; the patcher rewrites
    # +2..+6 to lslw/movew and the dbf at +0xA (see patch_game.py)
    'RLE_EVEN_PASS': 0x16CC,
    # strip blitter: movew (a0)+,%d0 that clobbers the patched base -> addw
    'STRIP_BLITTER_MOVEW': 0x2590,
    # strip blitter base load: movel #tiles,%d0 (gets the precise tile thunk)
    'STRIP_BLITTER_BASE': 0x258A,
    # text-writer movew->addw family (offset, old word, new word)
    'TEXT_IDIOM': [
        (0x1B4CC, 0x3039, 0xD079),   # movew 0x1bf56,%d0
        (0x1B660, 0x3039, 0xD079),   # movew 0x1bf06,%d0
        (0x1B95C, 0x303C, 0xD07C),   # movew #imm,%d0 (attract text rows)
        (0x1B964, 0x303C, 0xD07C),
        (0x1B96E, 0x303C, 0xD07C),
        (0x1B97C, 0x303C, 0xD07C),
        (0x1B986, 0x303C, 0xD07C),
        (0x1BAE8, 0x301B, 0xD05B),   # movew (a3)+,%d0
    ],
    # runtime control transfers INTO the displaced 0x400-0x807 region
    # (offset, kind, target): bsrw/braw pc-rel, jmpl abs.l, jmpw abs.w
    'BOOT_JUMPINS': [
        (0x0978, 'bsrw', 0x57E),
        (0x0BD2, 'braw', 0x62A),
        (0x1EEC, 'jmpl', 0x5BE),
        (0x1B5C6, 'jmpw', 0x47E),
    ],
    # boot RAM copy [0x400,0x808): pc-relative -> absolute fixups
    # (offset, old 4 bytes, new 4 bytes)
    'BOOT_PCREL': [
        (0x404, [0x60,0x00,0x26,0xA6], [0x4E,0xF8,0x2A,0xAC]),  # braw 2aac -> jmp (2AAC).w
        (0x440, [0x41,0xFA,0x15,0x44], [0x41,0xF8,0x19,0x86]),  # lea pc(1986) -> lea (1986).w
        (0x4D2, [0x61,0x00,0x10,0x04], [0x4E,0xB8,0x14,0xD8]),  # bsrw 14d8 -> jsr (14D8).w
        (0x4DE, [0x61,0x00,0x0F,0xF8], [0x4E,0xB8,0x14,0xD8]),
        (0x57A, [0x43,0xFA,0x17,0x8E], [0x43,0xF8,0x1D,0x0A]),  # lea pc(1d0a) -> abs
        (0x5D2, [0x61,0x00,0x0D,0x32], [0x4E,0xB8,0x13,0x06]),
        (0x60C, [0x41,0xFA,0x12,0x36], [0x41,0xF8,0x18,0x44]),
        (0x658, [0x41,0xFA,0x11,0xEE], [0x41,0xF8,0x18,0x48]),
        (0x662, [0x41,0xFA,0x16,0x76], [0x41,0xF8,0x1C,0xDA]),
        (0x6B8, [0x61,0x00,0x0C,0x7E], [0x4E,0xB8,0x13,0x38]),
        (0x728, [0x41,0xFA,0x11,0x26], [0x41,0xF8,0x18,0x50]),
        (0x758, [0x61,0x00,0x06,0x86], [0x4E,0xB8,0x0D,0xE0]),
        (0x75C, [0x61,0x00,0x0F,0x36], [0x4E,0xB8,0x16,0x94]),
        (0x7F4, [0x41,0xFA,0x10,0x56], [0x41,0xF8,0x18,0x4C]),
    ],
    # spawn script: per-round META-TABLE of table pointers (consumed at
    # 0xD80E) + stride-12 record lists with the handler long at +8
    'SPAWN_META': (0x1D32A, 0x1D33E),   # 5 per-round table pointers
    'SPAWN_LO': 0x1D300,
    'SPAWN_CAP': 0x1E000,
    # jump tables of ABSOLUTE code pointers (lea %pc@(tbl); movea.l (A0,D0);
    # jmp (A0)); (start, count). 0x6D70/0x6D90 extents are HARD-BOUNDED:
    # 0x6DC0+ is a WORD index table.
    'REBASE_TABLES': [(0x26DC, 8), (0x6D70, 8), (0x6D90, 12), (0x92F0, 6),
                      (0xF556, 5), (0x17E24, 5), (0x1A076, 9)],
    # runtime-harvested handler values and the byte-collision blacklist;
    # occurrences are rebased only below HARVEST_BOUND (packed assets above)
    'HARVESTED_HANDLERS': HARVESTED,
    'HARVEST_BLACKLIST': [0x10000, 0x102, 0x104, 0x106],
    'HARVEST_BOUND': 0x28000,
    # stride-record asset tables: (start, count, stride, pointer offset).
    # 0x1CE2 = the tilemap RLE loader's per-round table [bank.w][srcptr.l]
    'STRIDE_TABLES': [(0x1CE2, 8, 6, 2)],
    # #imm values that ARE pointers despite landing in data registers
    # (0x3C92: sprite frame-table base consumed via adda.l D2)
    'IMM_OVERRIDES': [(0x3C92, 0x255E0)],
    # the game reads its own vector table as CONSTANTS (addal 0x0,%a4)
    'LOW_VECTOR_READS': [0x14932, 0x1493E, 0x307A, 0xABC4],
    # the one abs.w code ref that can't hold 0x9xxxxx: (site, target);
    # thunked via shim RAM 0xFFB3F0 -> jmp 0x900000+target
    'ABSW_JMP': (0x1B5C6, 0x47E),
    # handler-pointer consumption funnels re-pointed at normalizing thunks:
    # (site, expected 6 bytes, thunk abs.w)
    'DISPATCHERS': [
        (0x39A8, [0x20,0x6E,0x00,0x02,0x4E,0x90], 0xB3A0),  # movea.l (2,A6),A0 ; jsr (A0)
        (0xD842, [0x22,0x68,0x00,0x08,0x4E,0x91], 0xB3C0),  # movea.l (8,A0),A1 ; jsr (A1)
    ],
    # stored data-pointer readers normalized at use time: (site, 4 bytes, thunk)
    'DATA_PTR_NORM': [
        (0xDBA8, [0x28,0x6E,0x00,0x24], 0xB340),  # movea.l (0x24,A6),A4 spawn walker
        (0x30D0, [0x20,0x6D,0x00,0x02], 0xB360),  # movea.l (2,A5),A0 palette streamer
    ],
    # TAS latches -> shim thunks: (site, expected 4 bytes, thunk)
    'TAS_SITES': [
        (0x2268,  bytes([0x4A, 0xF8, 0xC0, 0x20]), 0xB380),  # tas $c020.w
        (0xE098,  bytes([0x4A, 0xF8, 0xF1, 0x5A]), 0xB38A),  # tas $f15a.w
        (0xEAC0,  bytes([0x4A, 0xE8, 0x00, 0x3E]), 0xB394),  # tas (3E,A0)
        (0x150B6, bytes([0x4A, 0xE8, 0x00, 0x3E]), 0xB394),  # tas (3E,A0)
        (0x12E84, bytes([0x4A, 0xEE, 0x00, 0x3C]), 0xB3F6),  # tas (3C,A6)
    ],
    # tile-RAM writer roots: (site, opcode word, remapped target, page bits)
    'TILE_DIRTY_SITES': [
        (0x0D12, 0x43F9, 0x852518, 1 << 0),
        (0x0D24, 0x43F9, 0x852C98, 1 << 0),
        (0x0D36, 0x43F9, 0x852596, 1 << 0),
        (0x0D48, 0x43F9, 0x8525B8, 1 << 0),
        (0x0D5A, 0x43F9, 0x852516, 1 << 0),
        (0x0D64, 0x43F9, 0x852538, 1 << 0),
        (0x0D6E, 0x43F9, 0x852C96, 1 << 0),
        (0x0D78, 0x43F9, 0x852CB8, 1 << 0),
        (0x0D9A, 0x43F9, 0x852598, 1 << 0),
        (0x0DA2, 0x43F9, 0x857598, 1 << 5),
        (0x170A, 0x41F9, 0x85D230, 1 << 11),
        (0x174E, 0x41F9, 0x85C230, 1 << 10),
        (0x16BE, 0x41F9, 0x852000, 0x1FFF),   # RLE even pass (page-table fed)
        (0x16DE, 0x41F9, 0x852001, 0x1FFF),   # RLE odd pass
        (0x36B0, 0x207C, 0x852000, 0x1FFF),   # clear-all
        (0x1ACD8, 0x41F9, 0x852000, 0x1FFF),  # clear-all (round)
        (0x1A52C, 0x41F9, 0x852494, 1 << 0),
        (0x1A54C, 0x41F9, 0x857000, 1 << 5),
        (0x1A562, 0x41F9, 0x857516, 1 << 5),
        (0x1B76A, 0x47F9, 0x853000, 1 << 1),  # scratch save
        (0x1B9FA, 0x41F9, 0x8520B2, 1 << 0),
        (0x1BA02, 0x41F9, 0x852D32, 1 << 0),
        (0x1BA0A, 0x41F9, 0x8520B2, 1 << 0),
        (0x1BA12, 0x41F9, 0x8520FC, 1 << 0),
        (0x1BA50, 0x43F9, 0x85223A, 1 << 0),
    ],
    # palette writers: (site, displaced length, region mask, note)
    'PAL_DIRTY_SITES': [
        (0x01EF2, 6, 0x0001, "clr.w 0xFF9000"),
        (0x01F80, 6, 0x0001, "clr.w 0xFF9000"),
        (0x020A0, 6, 0x0001, "clr.w 0xFF9000"),
        (0x1A934, 8, 0x0001, "move.w #imm,0xFF9000"),
        (0x1A93C, 8, 0x0001, "move.w #imm,0xFF9002"),
        (0x1A944, 8, 0x0001, "move.w #imm,0xFF9004"),
        (0x1A94C, 8, 0x0001, "move.w #imm,0xFF9006"),
        (0x1A954, 8, 0x0001, "move.w #imm,0xFF9010"),
        (0x1A95C, 8, 0x0001, "move.w #imm,0xFF9012"),
        (0x1A964, 8, 0x0001, "move.w #imm,0xFF9014"),
        (0x1A96C, 8, 0x0001, "move.w #imm,0xFF9016"),
        (0x1A974, 8, 0x0001, "move.w #imm,0xFF9020"),
        (0x1A97C, 8, 0x0001, "move.w #imm,0xFF9022"),
        (0x1A984, 8, 0x0001, "move.w #imm,0xFF9024"),
        (0x1A98C, 8, 0x0001, "move.w #imm,0xFF9026"),
        (0x1B0E6, 8, 0x0001, "move.w #imm,0xFF9000"),
        (0x1B0EE, 8, 0x0001, "move.w #imm,0xFF9002"),
        (0x1B0F6, 8, 0x0001, "move.w #imm,0xFF9004"),
        (0x1B0FE, 8, 0x0001, "move.w #imm,0xFF9006"),
        (0x1B106, 8, 0x0001, "move.w #imm,0xFF9010"),
        (0x1B10E, 8, 0x0001, "move.w #imm,0xFF9012"),
        (0x1B116, 8, 0x0001, "move.w #imm,0xFF9014"),
        (0x1B11E, 8, 0x0001, "move.w #imm,0xFF9016"),
        (0x1B126, 8, 0x0001, "move.w #imm,0xFF9020"),
        (0x1B12E, 8, 0x0001, "move.w #imm,0xFF9022"),
        (0x1B136, 8, 0x0001, "move.w #imm,0xFF9024"),
        (0x1B13E, 8, 0x0001, "move.w #imm,0xFF9026"),
        (0x025BA, 6, 0x0080, "0x2612 helper, d1=12: 13*16B at 0x720 -> 0x7EF"),
        (0x025E8, 6, 0x0004, "0x2612 helper, d1=9: 10*16B at 0x250 -> 0x2EF"),
        (0x025F0, 6, 0x0001, "0x2612 helper, d1=0: 16B at 0x0B0 -> 0x0BF"),
        (0x0263C, 6, 0x0004, "0x2612 helper, d1=9: 10*16B at 0x250 -> 0x2EF"),
        (0x02B7E, 6, 0x00F0, "table 0x326E rounds 0-4: 0x400 -> 0x71F"),
        (0x02B94, 6, 0x0001, "single move.w at 0x2BA4 -> 0x06C"),
        (0x02BB8, 6, 0x0001, "32 longs = 128B at 0x000 -> 0x07F"),
        (0x03116, 6, 0x0001, "8 longs = 32B at 0x040 -> 0x05F"),
        (0x03846, 6, 0x0001, "8 longs = 32B at 0x040 -> 0x05F"),
        (0x03952, 6, 0x00FF, "2 x 256 longs = 2048B at 0x000 -> 0x7FF"),
        (0x04544, 6, 0x0001, "20 longs = 80B at 0x010 -> 0x05F"),
        (0x170BA, 6, 0x00F0, "table 0x1724C rounds 0-4: 0x400 -> 0x71F"),
        (0x1A4F0, 6, 0x0030, "19*16B at 0x490 -> 0x5BF"),
        (0x1B742, 6, 0x0001, "7*16B at 0x010 -> 0x07F"),
        (0x1BAB6, 6, 0x0007, "40*8 words = 640B at 0x080 -> 0x2FF"),
    ],
    # colour-cycle engine (lea 0xFF9000,A1; write at +((D0&0x7F)<<4))
    'PAL_THUNK_A': 0x30C2,
    # LOOP29 166: the cycler's dirty mark at PAL_THUNK_A fires at 0x30C2,
    # BEFORE its four stores at 0x30F8-0x30FE. A consume landing between
    # the two ships the OLD colours and clears the bit, so the mirror sits
    # one rotation step behind for ever -- measured on sets 19/20/21
    # (LOOP29 165). This is the mark-AFTER site: `lea 8(a5),a5` at 0x3100,
    # four bytes, exactly a jsr abs.w, reached after every store path.
    'PAL_THUNK_APOST': 0x3100,
    # queued-pointer palette writers (moveal (A2)+,A1 ; moveal (A2)+,A0)
    'PAL_THUNK_B': [0x2DC8, 0x3C5A],
    # FM gate entries: (site, displaced length, expected first word, note)
    'FMGATE_ENTRIES': [
        (0x153E, 6, 0x227C, "moveal #txt,%a1"),
        (0x16BE, 6, 0x41F9, "lea tiles,%a0"),
        (0x16DE, 6, 0x41F9, "lea tiles+1,%a0"),
        (0x170A, 6, 0x41F9, "lea tiles,%a0"),
        (0x174E, 6, 0x41F9, "lea tiles,%a0"),
        (0x2552, 6, 0x41F9, "lea rom tbl,%a0"),
        (0x2564, 6, 0x41F9, "lea rom tbl,%a0"),
        (0x2572, 6, 0x41F9, "lea rom tbl,%a0"),
        (0x2580, 6, 0x41F9, "lea rom tbl,%a0"),
        (0x369C, 6, 0x207C, "moveal #text,%a0 (17-caller alt entry)"),
        (0x36B0, 6, 0x207C, "moveal #tiles,%a0"),
        (0x36C4, 6, 0x207C, "moveal #sprram,%a0"),
        (0x3706, 4, 0x41ED, "lea 8(%a5),%a0"),
        (0x3716, 6, 0x0838, "btst #0,flag (flags survive: displaced last)"),
        (0x1A52C, 6, 0x41F9, "round clear: crystal ball (lea page0)"),
        (0x1ACCA, 6, 0x41F9, "round clear-all (lea text)"),
        (0x378E, 4, 0x41F8, "lea wram,%a0"),
        (0x380A, 6, 0x0838, "btst #0,flag"),
        (0x3838, 4, 0x41FA, "lea pc-rel,%a0 -> ABSOLUTE rewrite"),
        (0x3858, 6, 0x0838, "btst #0,flag"),
        (0x3A9A, 4, 0x12D8, "moveb copy loop head (dbf re-enters gate)"),
        (0x3AA4, 4, 0x4211, "clrb clear loop head (the CLR RMW site)"),
        (0x3AAE, 6, 0x43F9, "lea text,%a1"),
        (0x4D88, 4, 0x2029, "movel 16(%a1),%d0"),
        (0x56E8, 4, 0x522E, "addqb frame ctr (follower rewrites CCR)"),
        # LOOP29 225: the ROUND CLEAR typewriter. An object state machine
        # types one glyph every 5 frames -- movew d1,(a1) at 0x6504 for
        # "ROUND CLEAR BONUS " and 0x6560 for the points -- into the text
        # area, once, never rewritten. Outside every span above because the
        # attract never round-clears, so a glyph written while FM=1 was
        # dropped for good: Mike's "RO D CL AR BONU". The displaced subq
        # sets the CCR the bcs at +4 reads; the thunk runs it last.
        # 228: gate the STORE, not the state routine. The state spans
        # deferred 57 windows across the attract (vi72; vi70: 0) -- the
        # glyph loop is reached there by a path the immediates do not show
        # -- and every deferred window is a frame the SH-2 never gets:
        # black tiles on the same-round return. A three-instruction span
        # around each movew d1,(a1) is hit by a vint almost never.
        (0x64FC, 4, 0x323C, "round clear text: movew #512,d1 before the glyph store"),
        (0x6558, 4, 0x323C, "round clear points: movew #512,d1 before the glyph store"),
        # 2026-09-20: THE HIGH-SCORE TABLE (attract). 0x4540 copies a
        # palette, then writes text rows 7 and 9 and seven entry rows via
        # the helpers 0x4612/0x4624/0x4664/0x466C/0x469C (called from
        # nowhere else). Never gated, never marked: its stores were
        # dropped at FM=1 and its rows captured only on the forced full
        # mask. "Broken the entire time" (Mike). GATE THE STORES, not the
        # routine (LOOP29 228): its delay helper 0x4664 sits in the game's
        # own frame wait, so a span over the routine would defer every FM
        # raise for the whole screen. Each store has a 4-byte instruction
        # in front of it to displace; the dbf loops re-enter their gate.
        (0x45EC, 4, 0x0600, "high-score: addib #-48,d0 before the rank store"),
        (0x45FE, 4, 0x0601, "high-score: addib #-96,d1 before the name store"),
        (0x4614, 4, 0x0601, "high-score: addib #-96,d1 before the string store (loop)"),
        (0x4634, 4, 0x14FC, "high-score: moveb #0,(a2)+ leading-zero blank"),
        (0x4648, 4, 0x0601, "high-score: addib #-48,d1 before the digit store"),
        (0x4658, 4, 0x123C, "high-score: moveb #-48,d1 before the zero-digit store"),
        (0x46A2, 4, 0x1081, "high-score: moveb d1,(a0); addql #2,a0 fill loop head"),
    ],
    'FMGATE_SPANS': [(0x153E, 0x155C), (0x16BE, 0x1772), (0x2550, 0x25AA),
                     (0x35CC, 0x3950), (0x3A9A, 0x3AFC), (0x4D80, 0x4D98),
                     (0x56E8, 0x5742), (0x1A52C, 0x1A59E), (0x1ACCA, 0x1ACEC),
                     (0x64FC, 0x6506), (0x6558, 0x6562),
                     (0x45EC, 0x45F2), (0x45FE, 0x4604), (0x4614, 0x461A),
                     (0x4634, 0x4638), (0x4648, 0x4650), (0x4658, 0x4660),
                     (0x46A2, 0x46AA)],
    # LOOP 27 q4 TXTWRAM: text writers at the top of the game's pass, staged
    # in the WRAM text mirror and copied to FB staging by the shim.
    #  - credit line FUN_3aae: a1 = text + *(0xFFF024) (byte offset var),
    #    clears 9 glyphs (stride 2) then CREDIT n / INSERT COIN -> 10 words;
    #    calls the shared copy/clear loop heads 0x3A9A/0x3AA4 (also entered
    #    by FB writers, so they keep their gate for FB destinations)
    #  - health bar FUN_4d54: a0 = text 0xCD0 (P1, d7=-4) / 0xCE0 (P2, +4),
    #    8 longs from table 0x6D5C -> bytes [0xCB4,0xCD4) / [0xCE4,0xD04);
    #    its LOOP 23 gate (0x4D88) and span (0x4D80,0x4D98) go with it
    # scene-level text FILL entries (FB path): a pending footprint copy must
    # not re-plant over the clear (the phantom P2 orbs, 2026-09-07)
    'TXT_WRAM_CLEAR_SITES': [0x369C],
    'TXT_WRAM_WRITERS': [
        {'site': 0x3AAE, 'reg': 1, 'off_var': 0xFFF024, 'words': 10,
         'loops': [0x3A9A, 0x3AA4], 'note': 'credit line'},
        {'site': 0x4D54, 'reg': 0, 'alt_sites': [0x4D62],
         'sel_var': 0xFFF109, 'ranges': [(0xCB4, 0xCD3), (0xCE4, 0xD03)],   # P1 / P2 (tstb 0xFFF109 at 0x4D5C)
         'drop_gates': [0x4D88], 'drop_spans': [(0x4D80, 0x4D98)], 'note': 'health bar'},
    ],
    # palette-cycle launch table entries whose script pointer the harvest
    # missed: ([offsets], expected pointer value)
    'PAL_LAUNCH': ([0x1A704, 0x1A70A], 0x1A78E),
    # regions no pass may touch (word tables forging valid-looking pointers)
    'REBASE_EXCLUDE': [(0x6DC0, 0x6DCA), (0x1AD10, 0x1AD34), (0x7358, 0x73A0),
                       (0xEC32, 0xEC46), (0xECAC, 0xECB0)],
}
