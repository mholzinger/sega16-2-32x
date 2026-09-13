# Sound posts — goldnaxe

Entry 0x3616; 201 call sites (0 in the second half), 42 with a computed command.

Convention (LOOP-DECOMPILE-GOLDNAXE 6, 11): d0.b = command; 0 goes straight to the latch (0x3674, stop-all); others are
de-duplicated against the 32-byte ring 0xFFEC40-5F (count 0xFFEC3C, read pointer 0xFFEC3E) and pushed; IRQ4 pops one per
vint into the mailbox 0xFFECFC (0x3314); the MCU forwards non-0xFF to the Z80 latch and rewrites 0xFF. Command 0x9C is
dropped unless 0xFFEC26 bit 0 and 0xFFEC1B bit 1 are set (0x361C-0x3632; HYPOTHESIS: the demo-sounds gate).


## Commands by frequency of call sites

| cmd | sites |
|---|---|
| 0x97 | 32 |
| 0x95 | 22 |
| 0xA0 | 9 |
| 0x53 | 8 |
| 0xA2 | 6 |
| 0x54 | 6 |
| 0x4A | 6 |
| 0x4B | 6 |
| 0x49 | 4 |
| 0x00 | 3 |
| 0xBD | 3 |
| 0x90 | 3 |
| 0x4C | 3 |
| 0x48 | 3 |
| 0x46 | 3 |
| 0xA6 | 3 |
| 0x41 | 2 |
| 0x9F | 2 |
| 0x9E | 2 |
| 0x4D | 2 |
| 0x51 | 2 |
| 0xA3 | 2 |
| 0xA4 | 2 |
| 0xC5 | 1 |
| 0xC9 | 1 |
| 0xC6 | 1 |
| 0xC4 | 1 |
| 0x40 | 1 |
| 0xCA | 1 |
| 0xC7 | 1 |
| 0x98 | 1 |
| 0xB0 | 1 |
| 0xBC | 1 |
| 0xB3 | 1 |
| 0xB5 | 1 |
| 0x4F | 1 |
| 0x4E | 1 |
| 0xAA | 1 |
| 0x43 | 1 |
| 0x42 | 1 |
| 0x44 | 1 |
| 0xA7 | 1 |
| 0xA8 | 1 |
| 0xA9 | 1 |
| 0xB9 | 1 |
| 0xBA | 1 |
| 0xBF | 1 |
| 0xCC | 1 |

## Every call site

| site | cmd | via | call | function | half |
|---|---|---|---|---|---|
| 0x019F8 | 0x00 | moveq | jsr | FUN_000019cc |  |
| 0x01B42 | 0x00 | moveq | jsr | - |  |
| 0x366EC | 0x00 | moveb | jsr | - |  |
| 0x01B4A | 0x40 | moveq | jmp | - |  |
| 0x015C8 | 0x41 | movew | jsr | - |  |
| 0x0166A | 0x41 | movew | jsr | - |  |
| 0x18106 | 0x42 | moveq | jsr | - |  |
| 0x17D9A | 0x43 | moveq | jmp | - |  |
| 0x18464 | 0x44 | moveq | jsr | - |  |
| 0x14C70 | 0x46 | moveq | jmp | - |  |
| 0x14D44 | 0x46 | moveq | jsr | - |  |
| 0x14DDC | 0x46 | moveq | jsr | - |  |
| 0x1149E | 0x48 | moveq | jmp | - |  |
| 0x1157C | 0x48 | moveq | jsr | - |  |
| 0x1161E | 0x48 | moveq | jsr | - |  |
| 0x22128 | 0x49 | moveq | jmp | - |  |
| 0x221A8 | 0x49 | moveq | jsr | - |  |
| 0x23278 | 0x49 | moveq | jmp | - |  |
| 0x232F8 | 0x49 | moveq | jsr | - |  |
| 0x2464A | 0x4A | moveq | jmp | - |  |
| 0x246CA | 0x4A | moveq | jsr | - |  |
| 0x25E16 | 0x4A | moveq | jmp | - |  |
| 0x25E96 | 0x4A | moveq | jsr | - |  |
| 0x27528 | 0x4A | moveq | jmp | - |  |
| 0x275A0 | 0x4A | moveq | jsr | - |  |
| 0x28A3C | 0x4B | moveq | jmp | - |  |
| 0x28ABC | 0x4B | moveq | jsr | - |  |
| 0x29A56 | 0x4B | moveq | jmp | - |  |
| 0x29AD6 | 0x4B | moveq | jsr | - |  |
| 0x2A9AA | 0x4B | moveq | jmp | - |  |
| 0x35972 | 0x4B | moveq | jsr | - |  |
| 0x0DBAE | 0x4C | moveq | jmp | - |  |
| 0x0DC82 | 0x4C | moveq | jsr | - |  |
| 0x0DD1A | 0x4C | moveq | jsr | - |  |
| 0x09346 | 0x4D | moveb | jmp | - |  |
| 0x09378 | 0x4D | moveb | jsr | - |  |
| 0x099C2 | 0x4E | moveq | jsr | - |  |
| 0x08EDA | 0x4F | moveb | jsr | - |  |
| 0x09502 | 0x51 | moveb | jmp | - |  |
| 0x0970C | 0x51 | moveb | jmp | - |  |
| 0x1D190 | 0x53 | moveq | jmp | - |  |
| 0x1D210 | 0x53 | moveq | jsr | - |  |
| 0x1E56A | 0x53 | moveq | jmp | - |  |
| 0x1E5EA | 0x53 | moveq | jsr | - |  |
| 0x1F95E | 0x53 | moveq | jmp | - |  |
| 0x1F9DE | 0x53 | moveq | jsr | - |  |
| 0x20D70 | 0x53 | moveq | jmp | - |  |
| 0x20DF0 | 0x53 | moveq | jsr | - |  |
| 0x1959E | 0x54 | moveq | jmp | - |  |
| 0x1966A | 0x54 | moveq | jsr | - |  |
| 0x1A9F0 | 0x54 | moveq | jmp | - |  |
| 0x1AA70 | 0x54 | moveq | jsr | - |  |
| 0x1BDB6 | 0x54 | moveq | jmp | - |  |
| 0x1BE36 | 0x54 | moveq | jsr | - |  |
| 0x0D874 | 0x90 | moveq | jsr | - |  |
| 0x1115A | 0x90 | moveq | jsr | - |  |
| 0x14936 | 0x90 | moveq | jsr | FUN_00014576 |  |
| 0x0EE18 | 0x95 | moveq | jsr | FUN_0000ee00 |  |
| 0x0FA5E | 0x95 | moveq | jsr | FUN_0000fa46 |  |
| 0x10616 | 0x95 | moveq | jsr | FUN_000105fe |  |
| 0x126D6 | 0x95 | moveq | jsr | FUN_000126be |  |
| 0x1331C | 0x95 | moveq | jsr | FUN_00013304 |  |
| 0x13E62 | 0x95 | moveq | jsr | FUN_00013e4a |  |
| 0x15ED2 | 0x95 | moveq | jsr | FUN_00015eba |  |
| 0x16AB8 | 0x95 | moveq | jsr | FUN_00016aa0 |  |
| 0x175EE | 0x95 | moveq | jsr | FUN_000175d6 |  |
| 0x1958C | 0x95 | moveq | jsr | - |  |
| 0x1A9DA | 0x95 | moveq | jsr | - |  |
| 0x1BDA0 | 0x95 | moveq | jsr | - |  |
| 0x1D17A | 0x95 | moveq | jsr | - |  |
| 0x1E554 | 0x95 | moveq | jsr | - |  |
| 0x1F948 | 0x95 | moveq | jsr | - |  |
| 0x20D5A | 0x95 | moveq | jsr | - |  |
| 0x22112 | 0x95 | moveq | jsr | - |  |
| 0x23262 | 0x95 | moveq | jsr | - |  |
| 0x28A26 | 0x95 | moveq | jsr | - |  |
| 0x29A40 | 0x95 | moveq | jsr | - |  |
| 0x2A994 | 0x95 | moveq | jsr | - |  |
| 0x35800 | 0x95 | moveq | jmp | - |  |
| 0x09CCE | 0x97 | moveq | jsr | - |  |
| 0x0DD98 | 0x97 | moveq | jmp | - |  |
| 0x0DE16 | 0x97 | moveq | jmp | FUN_0000ddd6 |  |
| 0x1169C | 0x97 | moveq | jmp | - |  |
| 0x1171A | 0x97 | moveq | jmp | FUN_000116da |  |
| 0x14E5A | 0x97 | moveq | jmp | - |  |
| 0x14ED8 | 0x97 | moveq | jmp | FUN_00014e98 |  |
| 0x18500 | 0x97 | moveq | jmp | - |  |
| 0x196E8 | 0x97 | moveq | jmp | - |  |
| 0x1973E | 0x97 | moveq | jmp | FUN_00019708 |  |
| 0x1AAEE | 0x97 | moveq | jmp | - |  |
| 0x1AB44 | 0x97 | moveq | jmp | FUN_0001ab0e |  |
| 0x1BEB4 | 0x97 | moveq | jmp | - |  |
| 0x1BF0A | 0x97 | moveq | jmp | - |  |
| 0x1D28E | 0x97 | moveq | jmp | - |  |
| 0x1D2E4 | 0x97 | moveq | jmp | FUN_0001d2ae |  |
| 0x1E668 | 0x97 | moveq | jmp | - |  |
| 0x1E6BE | 0x97 | moveq | jmp | FUN_0001e688 |  |
| 0x1FA5C | 0x97 | moveq | jmp | - |  |
| 0x1FAB2 | 0x97 | moveq | jmp | FUN_0001fa7c |  |
| 0x20E6E | 0x97 | moveq | jmp | - |  |
| 0x20EC4 | 0x97 | moveq | jmp | - |  |
| 0x22226 | 0x97 | moveq | jmp | - |  |
| 0x2227C | 0x97 | moveq | jmp | FUN_00022246 |  |
| 0x23376 | 0x97 | moveq | jmp | - |  |
| 0x233CC | 0x97 | moveq | jmp | FUN_00023396 |  |
| 0x24748 | 0x97 | moveq | jmp | - |  |
| 0x2479E | 0x97 | moveq | jmp | - |  |
| 0x25F14 | 0x97 | moveq | jmp | - |  |
| 0x25F6A | 0x97 | moveq | jmp | - |  |
| 0x2761E | 0x97 | moveq | jmp | - |  |
| 0x27678 | 0x97 | moveq | jmp | - |  |
| 0x078AE | 0x98 | moveb | jsr | FUN_000077c4 |  |
| 0x02462 | 0x9E | moveb | jsr | - |  |
| 0x078E6 | 0x9E | moveb | jsr | - |  |
| 0x02416 | 0x9F | moveb | jsr | - |  |
| 0x3220A | 0x9F | moveq | jsr | - |  |
| 0x0BA2C | 0xA0 | moveq | jsr | - |  |
| 0x28B3A | 0xA0 | moveq | jmp | - |  |
| 0x28B90 | 0xA0 | moveq | jmp | - |  |
| 0x29B54 | 0xA0 | moveq | jmp | - |  |
| 0x29BAA | 0xA0 | moveq | jmp | - |  |
| 0x2AA02 | 0xA0 | moveq | jmp | - |  |
| 0x2AA58 | 0xA0 | moveq | jmp | FUN_0002aa22 |  |
| 0x35858 | 0xA0 | moveq | jmp | - |  |
| 0x358AE | 0xA0 | moveq | jmp | - |  |
| 0x0ED72 | 0xA2 | moveq | jsr | - |  |
| 0x1265A | 0xA2 | moveq | jsr | - |  |
| 0x15E2C | 0xA2 | moveq | jsr | - |  |
| 0x2B958 | 0xA2 | moveq | jsr | - |  |
| 0x2CA0E | 0xA2 | moveq | jsr | - |  |
| 0x2DACC | 0xA2 | moveq | jsr | - |  |
| 0x17E6A | 0xA3 | moveq | jsr | - |  |
| 0x18154 | 0xA3 | moveq | jsr | - |  |
| 0x17F36 | 0xA4 | moveq | jsr | - |  |
| 0x17F58 | 0xA4 | moveq | jsr | - |  |
| 0x24634 | 0xA6 | moveq | jsr | - |  |
| 0x25E00 | 0xA6 | moveq | jsr | - |  |
| 0x2751A | 0xA6 | moveq | jsr | - |  |
| 0x23B6E | 0xA7 | moveq | jsr | - |  |
| 0x2829A | 0xA8 | moveq | jsr | - |  |
| 0x2ECFC | 0xA9 | moveq | jmp | FUN_0002eca8 |  |
| 0x0C410 | 0xAA | movew | jsr | - |  |
| 0x08302 | 0xB0 | moveb | jsr | - |  |
| 0x085C0 | 0xB3 | moveb | jmp | - |  |
| 0x086A0 | 0xB5 | moveb | jmp | - |  |
| 0x35590 | 0xB9 | moveq | jsr | - |  |
| 0x355EA | 0xBA | moveq | jsr | - |  |
| 0x08520 | 0xBC | moveb | jmp | - |  |
| 0x08110 | 0xBD | moveb | jsr | - |  |
| 0x08B40 | 0xBD | moveb | jsr | - |  |
| 0x3207E | 0xBD | moveq | jsr | FUN_0003207c |  |
| 0x3593A | 0xBF | moveq | jsr | - |  |
| 0x01AE8 | 0xC4 | moveb | jsr | - |  |
| 0x00DBE | 0xC5 | moveb | jsr | - |  |
| 0x0199E | 0xC6 | moveq | jsr | - |  |
| 0x071E2 | 0xC7 | moveq | jsr | - |  |
| 0x01272 | 0xC9 | moveq | jsr | - |  |
| 0x0484E | 0xCA | moveb | jsr | - |  |
| 0x36540 | 0xCC | moveb | jsr | - |  |
| 0x010C8 | computed | computed | jsr | - |  |
| 0x01A74 | computed | computed | jsr | FUN_00001a20 |  |
| 0x01AC8 | computed | computed | jmp | - |  |
| 0x01B28 | computed | computed | jsr | - |  |
| 0x07D7C | computed | computed | jmp | - |  |
| 0x07F24 | computed | computed | jsr | - |  |
| 0x08852 | computed | computed | jsr | - |  |
| 0x0889A | computed | computed | jsr | - |  |
| 0x09AFA | computed | computed | jsr | - |  |
| 0x09F7C | computed | computed | jsr | - |  |
| 0x0A952 | computed | computed | jmp | FUN_0000a938 |  |
| 0x0DAEE | computed | computed | jsr | - |  |
| 0x113D4 | computed | computed | jsr | - |  |
| 0x14BB0 | computed | computed | jsr | - |  |
| 0x194D4 | computed | computed | jsr | - |  |
| 0x1A922 | computed | computed | jsr | - |  |
| 0x1BCE8 | computed | computed | jsr | - |  |
| 0x1D0C2 | computed | computed | jsr | - |  |
| 0x1E49C | computed | computed | jsr | - |  |
| 0x1F890 | computed | computed | jsr | - |  |
| 0x20CA2 | computed | computed | jsr | - |  |
| 0x2205A | computed | computed | jsr | - |  |
| 0x231AA | computed | computed | jsr | - |  |
| 0x2457C | computed | computed | jsr | - |  |
| 0x25D48 | computed | computed | jsr | - |  |
| 0x27462 | computed | computed | jsr | - |  |
| 0x2896E | computed | computed | jsr | - |  |
| 0x29988 | computed | computed | jsr | - |  |
| 0x2A8DC | computed | computed | jsr | - |  |
| 0x2BA02 | computed | computed | jsr | FUN_0002b9e8 |  |
| 0x2CAB8 | computed | computed | jsr | FUN_0002ca38 |  |
| 0x2DB76 | computed | computed | jsr | FUN_0002db5c |  |
| 0x2EDB0 | computed | computed | jsr | FUN_0002ed96 |  |
| 0x2FE72 | computed | computed | jsr | FUN_0002fe58 |  |
| 0x30F48 | computed | computed | jsr | - |  |
| 0x3230A | computed | computed | jsr | FUN_000322f0 |  |
| 0x3343A | computed | computed | jsr | FUN_00033420 |  |
| 0x344B2 | computed | computed | jsr | FUN_00034498 |  |
| 0x3571E | computed | computed | jsr | - |  |
| 0x359A8 | computed | computed | jmp | - |  |
| 0x366D2 | computed | computed | jsr | - |  |
| 0x3778A | computed | computed | jsr | - |  |
