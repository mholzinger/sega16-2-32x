# Altered Beast: function map, rebuilt without Ghidra

`tools/func_profile_ref.py`. Same 555 functions as
`function_map.md`, with the arcade hardware surface kept SEPARATE
from work RAM — the old `hardware` class conflated them and was
wrong for 72 of the 117 rows that carried it (LOOP-DECOMPILE 78).

| entry | size | callers | class | hw | fields | evidence |
|---|---|---|---|---|---|---|
| 0x00404 | 4 | 0 | leaf/helper | - | - | signature |
| 0x00408 | 4 | 1 | leaf/helper | - | - | signature |
| 0x0040C | 2 | 1 | leaf/helper | - | - | signature |
| 0x0040E | 364 | 0 | arcade hw | io tbank | - | signature |
| 0x00500 | 128 | 1 | leaf/helper | - | - | signature |
| 0x0057E | 64 | 1 | arcade hw | text | - | signature |
| 0x005BE | 1830 | 2 | test_mode_screen | io | - | READ |
| 0x00BD6 | 2 | 1 | leaf/helper | - | - | signature |
| 0x00D12 | 114 | 1 | arcade hw | vram | - | signature |
| 0x00D84 | 10 | 2 | leaf/helper | - | - | signature |
| 0x00D8E | 26 | 1 | arcade hw | vram | - | signature |
| 0x00DA8 | 22 | 1 | leaf/helper | - | - | signature |
| 0x00DBE | 34 | 1 | arcade hw | text | - | signature |
| 0x00DE0 | 26 | 1 | leaf/helper | - | - | signature |
| 0x00DFA | 212 | 1 | leaf/helper | - | - | signature |
| 0x00ECE | 306 | 1 | arcade hw | io | - | signature |
| 0x01000 | 384 | 0 | arcade hw | io | - | signature |
| 0x01180 | 18 | 3 | leaf/helper | - | - | signature |
| 0x01192 | 94 | 2 | leaf/helper | - | - | signature |
| 0x011F0 | 22 | 2 | leaf/helper | - | - | signature |
| 0x01206 | 22 | 2 | leaf/helper | - | - | signature |
| 0x0121C | 78 | 2 | leaf/helper | - | - | signature |
| 0x0126A | 78 | 1 | leaf/helper | - | - | signature |
| 0x012EC | 26 | 1 | leaf/helper | - | - | signature |
| 0x01306 | 50 | 1 | leaf/helper | - | - | signature |
| 0x01338 | 38 | 3 | leaf/helper | - | - | signature |
| 0x0135E | 8 | 2 | leaf/helper | - | - | signature |
| 0x01366 | 228 | 2 | read_controls_or_demo | io | - | READ |
| 0x0144A | 142 | 5 | credit_prompt_select | text | - | READ |
| 0x014D8 | 16 | 1 | leaf/helper | - | - | signature |
| 0x014E8 | 50 | 1 | arcade hw | text | - | signature |
| 0x0153E | 36 | 4 | leaf/helper | - | - | signature |
| 0x0162E | 66 | 0 | leaf/helper | - | - | signature |
| 0x01670 | 36 | 1 | leaf/helper | - | - | signature |
| 0x01694 | 42 | 1 | unpack_level_tilemap | - | - | READ |
| 0x016BE | 32 | 1 | tilemap_unpack_hi | vram | - | READ |
| 0x016DE | 44 | 1 | tilemap_unpack_lo | vram | - | READ |
| 0x0170A | 40 | 1 | tilemap_block_04 | vram | - | READ |
| 0x01732 | 28 | 1 | leaf/helper | - | - | signature |
| 0x0174E | 38 | 1 | tilemap_block_a5 | vram | - | READ |
| 0x01E54 | 128 | 5 | leaf/helper | - | - | signature |
| 0x01ED4 | 30 | 1 | leaf/helper | - | - | signature |
| 0x01EF2 | 142 | 1 | arcade hw | pal | - | signature |
| 0x01F80 | 288 | 1 | arcade hw | pal | $00 $02 $06 $08 $0A $0C | signature |
| 0x020A0 | 482 | 1 | arcade hw | pal | $00 $02 $06 $08 $0A $0C | signature |
| 0x02282 | 10 | 1 | leaf/helper | - | - | signature |
| 0x02362 | 34 | 0 | object routine | - | $0C $10 | signature |
| 0x024EC | 30 | 1 | object routine | - | $08 $0C $10 $4E | signature |
| 0x0250A | 72 | 1 | draws + motion + state change + palette | - | $00 $02 $06 $08 $0A $0C | signature |
| 0x02552 | 18 | 1 | leaf/helper | - | - | signature |
| 0x02564 | 14 | 1 | leaf/helper | - | - | signature |
| 0x02572 | 14 | 1 | leaf/helper | - | - | signature |
| 0x02580 | 10 | 1 | leaf/helper | - | - | signature |
| 0x0258A | 34 | 4 | leaf/helper | - | - | signature |
| 0x025AC | 40 | 1 | arcade hw | pal | - | signature |
| 0x025D4 | 62 | 1 | arcade hw | pal | - | signature |
| 0x02612 | 16 | 2 | leaf/helper | - | - | signature |
| 0x02622 | 20 | 2 | leaf/helper | - | - | signature |
| 0x02636 | 46 | 1 | arcade hw | pal | - | signature |
| 0x02AAC | 784 | 1 | irq4_handler | io pal text | - | READ |
| 0x02DBC | 54 | 1 | palette_queue_drain | - | - | READ |
| 0x02DF2 | 94 | 2 | leaf/helper | - | - | signature |
| 0x02E50 | 36 | 1 | leaf/helper | - | - | signature |
| 0x02E74 | 112 | 1 | input_edges | - | - | READ |
| 0x02EE4 | 170 | 1 | leaf/helper | - | - | signature |
| 0x02F82 | 56 | 1 | leaf/helper | - | - | signature |
| 0x02FBA | 30 | 2 | leaf/helper | - | - | signature |
| 0x02FE4 | 82 | 2 | arcade hw | io | - | signature |
| 0x03036 | 14 | 1 | object routine | - | $10 | signature |
| 0x03044 | 14 | 1 | object routine | - | $12 | signature |
| 0x03052 | 16 | 1 | motion | - | $14 | signature |
| 0x03062 | 80 | 1 | object routine | - | $00 $0E $10 $16 | signature |
| 0x030B2 | 86 | 1 | colour_cycle_streamer | pal | - | READ |
| 0x03108 | 32 | 1 | sky_palette_gradient | pal | - | READ |
| 0x03128 | 68 | 1 | leaf/helper | - | - | signature |
| 0x0316C | 14 | 1 | leaf/helper | - | - | signature |
| 0x0317A | 22 | 1 | leaf/helper | - | - | signature |
| 0x03190 | 106 | 1 | leaf/helper | - | - | signature |
| 0x031FA | 40 | 1 | leaf/helper | - | - | signature |
| 0x03352 | 104 | 77 | sound_enqueue | - | - | READ |
| 0x033BA | 134 | 2 | leaf/helper | - | - | signature |
| 0x03440 | 36 | 1 | leaf/helper | - | - | signature |
| 0x03464 | 6 | 0 | leaf/helper | - | - | signature |
| 0x0346A | 30 | 2 | leaf/helper | - | - | signature |
| 0x03488 | 14 | 1 | leaf/helper | - | - | signature |
| 0x034FE | 118 | 3 | leaf/helper | - | - | signature |
| 0x0369C | 20 | 5 | clear_textram | - | - | READ |
| 0x036B0 | 20 | 5 | clear_tileram | - | - | READ |
| 0x036C4 | 66 | 4 | clear_spriteram_and_pool | - | - | READ |
| 0x03706 | 12 | 12 | leaf/helper | - | - | signature |
| 0x03716 | 52 | 1 | object routine | - | $04 | signature |
| 0x0374A | 42 | 1 | arcade hw | text | $04 $08 $10 | signature |
| 0x03774 | 26 | 1 | object routine | - | $08 | signature |
| 0x0378E | 12 | 1 | arcade hw | text | - | signature |
| 0x037D0 | 58 | 2 | leaf/helper | - | - | signature |
| 0x0380A | 46 | 1 | arcade hw | text | - | signature |
| 0x03838 | 32 | 1 | sky_palette_flat | pal | - | READ |
| 0x03858 | 68 | 3 | leaf/helper | - | - | signature |
| 0x0389C | 14 | 1 | leaf/helper | - | - | signature |
| 0x038AA | 22 | 1 | leaf/helper | - | - | signature |
| 0x038C0 | 106 | 1 | leaf/helper | - | - | signature |
| 0x0392A | 40 | 1 | leaf/helper | - | - | signature |
| 0x03952 | 32 | 4 | set_level_palettes | - | - | READ |
| 0x03972 | 12 | 1 | leaf/helper | - | - | signature |
| 0x0397E | 16 | 11 | wait_frame_flag | - | - | READ |
| 0x0398E | 58 | 6 | object_dispatcher | - | $00 $02 $80 | READ |
| 0x039C8 | 56 | 4 | leaf/helper | - | - | signature |
| 0x03A00 | 120 | 4 | leaf/helper | - | - | signature |
| 0x03A9A | 10 | 8 | textram_stride_write | - | - | READ |
| 0x03AA4 | 10 | 4 | textram_clear_run | - | - | READ |
| 0x03AAE | 90 | 5 | draw_credits_line | text | - | READ |
| 0x03B08 | 38 | 4 | leaf/helper | - | - | signature |
| 0x03B2E | 62 | 62 | request_palette_update | - | $0A $0B | READ |
| 0x03B6C | 98 | 1 | allocate_palette_slot | - | $0A | READ |
| 0x03BCE | 30 | 59 | release_palette_slot | - | $0A | READ |
| 0x03BEC | 98 | 1 | build_palette_upload_queue | pal | $0A $0B | READ |
| 0x03C84 | 34 | 3 | sprite_frame_lookup | - | $06 | READ |
| 0x03CA6 | 16 | 3 | order_list_insert | - | $2F | READ |
| 0x03CB6 | 30 | 2 | object routine | - | $08 | signature |
| 0x03CD4 | 64 | 3 | zoom_scale_lookup | - | $4E | READ |
| 0x03D14 | 76 | 19 | sprite_build_depth_banded | - | $10 $2C $2F | READ |
| 0x03DD8 | 298 | 34 | sprite_build_and_cull | - | $00 $08 $09 $0A $0C $10 | READ |
| 0x03F04 | 28 | 76 | hide_object_sprite | - | $00 $08 | READ |
| 0x03F20 | 4 | 30 | integrate_position | - | - | READ |
| 0x03F24 | 14 | 4 | motion | - | $0C $14 | signature |
| 0x03F32 | 14 | 2 | motion | - | $10 $1A | signature |
| 0x03F40 | 50 | 10 | clamp_x_velocity | - | $14 $16 $18 | READ |
| 0x03F72 | 50 | 26 | clamp_y_velocity | - | $1A $1C $1E | READ |
| 0x03FBE | 38 | 5 | rng_next | - | - | READ |
| 0x04178 | 64 | 1 | leaf/helper | - | - | signature |
| 0x04196 | 20 | 1 | leaf/helper | - | - | signature |
| 0x041AA | 8 | 1 | leaf/helper | - | - | signature |
| 0x041B2 | 26 | 1 | leaf/helper | - | - | signature |
| 0x041CC | 36 | 1 | leaf/helper | - | - | signature |
| 0x04212 | 38 | 1 | leaf/helper | - | - | signature |
| 0x04238 | 28 | 1 | leaf/helper | - | - | signature |
| 0x04254 | 34 | 1 | leaf/helper | - | - | signature |
| 0x04276 | 186 | 1 | leaf/helper | - | - | signature |
| 0x04330 | 434 | 1 | arcade hw | io | - | signature |
| 0x0442E | 36 | 1 | leaf/helper | - | - | signature |
| 0x0451E | 22 | 2 | leaf/helper | - | - | signature |
| 0x04534 | 12 | 1 | leaf/helper | - | - | signature |
| 0x04540 | 210 | 1 | arcade hw | pal text | - | signature |
| 0x04612 | 18 | 2 | leaf/helper | - | - | signature |
| 0x04624 | 64 | 1 | leaf/helper | - | - | signature |
| 0x04664 | 8 | 3 | leaf/helper | - | - | signature |
| 0x0466C | 48 | 1 | leaf/helper | - | - | signature |
| 0x0469C | 34 | 1 | leaf/helper | - | - | signature |
| 0x047CC | 58 | 3 | claim/lock + palette | - | $0B $3C | signature |
| 0x04806 | 44 | 3 | claim/lock + palette | - | $0B $3C | signature |
| 0x04832 | 2 | 1 | leaf/helper | - | - | signature |
| 0x04870 | 1222 | 2 | arcade hw | text | $00 $08 $0A $0B $0C $10 | signature |
| 0x04D3A | 26 | 0 | leaf/helper | - | - | signature |
| 0x04D54 | 70 | 1 | arcade hw | text | $4A | signature |
| 0x04D9A | 538 | 1 | animation + motion + sound | - | $14 $16 $18 $1A $20 $21 | signature |
| 0x04FB4 | 546 | 3 | animation + motion + sound | - | $14 $16 $18 $20 $21 $22 | signature |
| 0x05062 | 102 | 1 | object routine | - | $2E | signature |
| 0x0523C | 76 | 1 | animation + sound | - | $20 $21 $22 $24 | signature |
| 0x05288 | 158 | 1 | sound | - | $1C $20 $4A $4B | signature |
| 0x05326 | 76 | 1 | motion | - | $0C $14 $16 $18 $1A $21 | signature |
| 0x05430 | 182 | 0 | animation + motion + palette + sound | - | $09 $14 $16 $20 $21 $22 | signature |
| 0x054E6 | 22 | 0 | state change | - | $02 $22 | signature |
| 0x054FC | 52 | 0 | animation + draws + despawns + palette | - | $-74 $-70 $-51 $00 $0C $10 | signature |
| 0x05530 | 124 | 1 | leaf/helper | - | - | signature |
| 0x055AC | 108 | 1 | claim/lock + draws + despawns + state change + sound | - | $02 $21 $22 $3E $4E | signature |
| 0x05618 | 86 | 1 | claim/lock + despawns + state change | - | $02 $21 $22 $3E $4E | signature |
| 0x0566E | 84 | 1 | claim/lock + despawns + palette | - | $00 $21 $22 $3E $4E | signature |
| 0x056C2 | 38 | 1 | arcade hw | text | - | signature |
| 0x056E8 | 92 | 1 | animation | - | $00 $21 $22 $24 | signature |
| 0x05744 | 50 | 1 | sound | - | $80 | signature |
| 0x05776 | 70 | 1 | claim/lock + state change + palette | - | $00 $02 $06 $09 $0B $0C | signature |
| 0x057BC | 98 | 1 | claim/lock + draws + despawns + palette | - | $00 $22 $3C | signature |
| 0x05854 | 80 | 0 | despawns + palette | - | $00 $22 $4E | signature |
| 0x058A4 | 64 | 1 | leaf/helper | - | - | signature |
| 0x058E4 | 68 | 1 | leaf/helper | - | - | signature |
| 0x05928 | 50 | 1 | animation + state change | - | $02 $21 $22 $24 $6C | signature |
| 0x0595A | 48 | 0 | animation + despawns + palette | - | $00 $21 $22 | signature |
| 0x059D2 | 64 | 0 | claim/lock + draws + state change | - | $02 $3E | signature |
| 0x059FA | 30 | 0 | claim/lock + draws | - | $3C $3E | signature |
| 0x05A30 | 66 | 0 | claim/lock + draws + state change + palette | - | $02 $06 $0B $3E | signature |
| 0x05A72 | 70 | 0 | animation + claim/lock + draws + motion + state change | - | $02 $0C $10 $14 $16 $18 | signature |
| 0x05AB8 | 52 | 0 | animation + motion + state change | - | $02 $0C $1A $21 $22 $24 | signature |
| 0x05AEC | 64 | 0 | animation + motion | - | $0C $14 $18 $21 $22 | signature |
| 0x05B2C | 70 | 1 | arcade hw | text | $6A | signature |
| 0x05B72 | 144 | 1 | claim/lock + palette | - | $0C $10 $3C | signature |
| 0x05C02 | 30 | 1 | claim/lock + state change + sound | - | $02 $3E | signature |
| 0x05C20 | 68 | 0 | animation + claim/lock + despawns + motion + palette | - | $00 $3E $3F | signature |
| 0x05C64 | 154 | 1 | palette + sound | - | $0A $0B $0C $10 $6C $6D | signature |
| 0x05CFE | 100 | 1 | claim/lock + draws + despawns + palette | - | $00 $0A $3E $4E | signature |
| 0x05D62 | 162 | 1 | leaf/helper | - | - | signature |
| 0x05E04 | 78 | 1 | leaf/helper | - | - | signature |
| 0x05E52 | 80 | 1 | animation + state change | - | $02 $21 $22 $24 $6C | signature |
| 0x05EA2 | 76 | 0 | animation + state change | - | $02 $21 $22 $80 $100 | signature |
| 0x05EEE | 14 | 0 | object routine | - | $22 | signature |
| 0x05EFC | 26 | 1 | leaf/helper | - | - | signature |
| 0x05F16 | 146 | 1 | animation + claim/lock + state change + palette | - | $00 $02 $09 $0B $20 $21 | signature |
| 0x05FA8 | 132 | 1 | claim/lock | - | $00 $02 $09 $0B $20 $21 | signature |
| 0x0602C | 12 | 0 | object routine | - | $23 | signature |
| 0x06038 | 64 | 1 | animation + claim/lock + state change | - | $02 $21 $22 $24 $3E $6C | signature |
| 0x06078 | 110 | 1 | animation + despawns | - | $20 $21 $22 $2E | signature |
| 0x060E6 | 130 | 1 | animation + motion + state change + palette | - | $00 $02 $06 $09 $0A $0B | signature |
| 0x06168 | 50 | 0 | claim/lock + state change + sound | - | $02 $10 $3E $6C | signature |
| 0x0619A | 60 | 1 | claim/lock + state change + sound | - | $02 $22 $3E $6C | signature |
| 0x061D6 | 16 | 1 | state change + sound | - | $02 | signature |
| 0x061E6 | 94 | 0 | motion + palette | - | $0A $10 $1C $1E $22 $48 | signature |
| 0x062B6 | 164 | 0 | animation + claim/lock + motion + state change + sound | - | $02 $10 $14 $16 $18 $1A | signature |
| 0x0635A | 60 | 0 | animation + claim/lock + state change + palette | - | $02 $0B $21 $22 $24 $3E | signature |
| 0x06396 | 30 | 0 | state change | - | $02 $21 $22 | signature |
| 0x063B4 | 24 | 0 | leaf/helper | - | - | signature |
| 0x063CC | 130 | 1 | object routine | - | $00 $02 $09 $0B $21 $22 | signature |
| 0x0644E | 74 | 0 | animation + claim/lock + draws + despawns + palette | - | $00 $21 $22 $3E $4E | signature |
| 0x06498 | 26 | 1 | leaf/helper | - | - | signature |
| 0x064B2 | 40 | 1 | claim/lock + state change | - | $02 $22 $24 $3E $6C | signature |
| 0x064DA | 92 | 1 | claim/lock + state change | - | $02 $22 $24 $3E $6C | signature |
| 0x06536 | 116 | 1 | claim/lock | - | $00 $22 $24 $3E $6C | signature |
| 0x065AA | 32 | 47 | restore_position | - | $0C $10 $28 $2A $40 $44 | READ |
| 0x065CA | 124 | 23 | animate_variant | - | $00 $06 $0C $10 $21 $22 | READ |
| 0x06646 | 14 | 1 | object routine | - | $21 | signature |
| 0x06654 | 72 | 16 | animation_peek_next | - | $21 $22 $24 $2E $50 $52 | READ |
| 0x0669C | 162 | 38 | animate | - | $00 $06 $0C $10 $21 $22 | READ |
| 0x0673E | 4 | 1 | object routine | - | $21 | signature |
| 0x06742 | 12 | 2 | leaf/helper | - | - | signature |
| 0x0674E | 70 | 24 | animation_frame_fetch | - | $21 $24 $2E $48 $49 $5C | READ |
| 0x06936 | 12 | 0 | leaf/helper | - | - | signature |
| 0x06942 | 12 | 0 | leaf/helper | - | - | signature |
| 0x0694E | 12 | 0 | leaf/helper | - | - | signature |
| 0x0695A | 12 | 0 | leaf/helper | - | - | signature |
| 0x06966 | 64 | 0 | leaf/helper | - | - | signature |
| 0x069A6 | 64 | 0 | leaf/helper | - | - | signature |
| 0x069E6 | 80 | 0 | leaf/helper | - | - | signature |
| 0x06A36 | 80 | 0 | leaf/helper | - | - | signature |
| 0x06A96 | 26 | 0 | motion | - | $0C $14 $2C | signature |
| 0x06AB0 | 32 | 0 | motion | - | $0C $14 $2C | signature |
| 0x06B0A | 158 | 5 | object routine | - | $49 $6A | signature |
| 0x06BA8 | 156 | 3 | object routine | - | $49 $6A | signature |
| 0x06C44 | 116 | 2 | object routine | - | $00 $02 $09 $0B $0C $10 | signature |
| 0x06CB8 | 62 | 0 | despawns + palette | - | $00 $06 $20 $24 $4E $6C | signature |
| 0x06DCA | 88 | 0 | leaf/helper | - | - | signature |
| 0x06E22 | 88 | 0 | leaf/helper | - | - | signature |
| 0x07CA0 | 1152 | 2 | animation + motion + sound | - | $14 $16 $18 $1A $20 $21 | signature |
| 0x07F58 | 102 | 1 | object routine | - | $2E | signature |
| 0x08246 | 80 | 1 | palette | - | $09 $2E | signature |
| 0x08296 | 172 | 0 | animation + motion + palette + sound | - | $0A $0B $14 $16 $20 $21 | signature |
| 0x08342 | 22 | 1 | state change | - | $02 $22 | signature |
| 0x08358 | 56 | 1 | animation + claim/lock + draws + despawns + palette | - | $00 $0C $10 $2F $3C $4E | signature |
| 0x08AAE | 518 | 2 | animation + motion + sound | - | $14 $16 $18 $1A $20 $21 | signature |
| 0x08CB4 | 556 | 3 | animation + motion + sound | - | $14 $16 $18 $20 $21 $22 | signature |
| 0x08D66 | 102 | 1 | object routine | - | $2E | signature |
| 0x08F46 | 78 | 1 | animation + sound | - | $20 $21 $22 $24 | signature |
| 0x09052 | 32 | 2 | arcade hw | text | - | signature |
| 0x09072 | 92 | 0 | palette | - | $0A $0B $2E $4B $6A $74 | signature |
| 0x090F4 | 218 | 1 | animation + claim/lock + state change + palette + sound | - | $00 $02 $09 $0A $0B $21 | signature |
| 0x091CE | 162 | 1 | despawns + palette + sound | - | $-44 $00 $09 $0A $0C $10 | signature |
| 0x09270 | 88 | 1 | animation + palette | - | $00 $06 $0A $0C $10 $21 | signature |
| 0x09EEC | 518 | 1 | animation + motion + sound | - | $14 $16 $18 $1A $20 $21 | signature |
| 0x0A0F2 | 682 | 3 | animation + motion + sound | - | $14 $16 $18 $20 $21 $22 | signature |
| 0x0A1E0 | 166 | 1 | collision + claim/lock + palette + sound | - | $09 $2E $3C $60 $62 $66 | signature |
| 0x0A286 | 264 | 1 | collision + animation + draws + despawns + state change + palette | - | $00 $02 $06 $0B $0C $10 | signature |
| 0x0A38E | 34 | 1 | animation + draws | - | $21 $22 | signature |
| 0x0A3B0 | 102 | 1 | object routine | - | $2E | signature |
| 0x0A5A4 | 10 | 1 | motion | - | $1A $1C | signature |
| 0x0A5AE | 102 | 1 | palette | - | $09 $2E $80 | signature |
| 0x0A614 | 80 | 1 | despawns + palette | - | $-74 $-70 $-60 $00 $0C $10 | signature |
| 0x0A6BA | 72 | 1 | animation + sound | - | $20 $21 $22 $24 $72 | signature |
| 0x0ADDC | 356 | 1 | animation + motion + sound | - | $14 $16 $18 $1A $1C $1E | signature |
| 0x0AF4A | 66 | 1 | object routine | - | $2E | signature |
| 0x0AF8C | 276 | 1 | claim/lock + motion + state change + palette + sound | - | $00 $02 $06 $09 $0A $0B | signature |
| 0x0B0A0 | 226 | 1 | draws + despawns + motion + palette | - | $00 $06 $0A $0B $0C $10 | signature |
| 0x0B182 | 40 | 1 | draws + despawns | - | $00 $06 $22 $4E | signature |
| 0x0B1AA | 60 | 1 | draws + palette | - | $-60 $-5C $-40 $-3C $-C $06 | signature |
| 0x0B1E6 | 30 | 0 | despawns + state change + palette | - | $00 $02 $20 | signature |
| 0x0B204 | 200 | 1 | animation + claim/lock + state change + palette + sound | - | $00 $02 $08 $09 $0B $21 | signature |
| 0x0B2CC | 60 | 1 | animation + draws + despawns + palette | - | $00 $23 $30 $48 | signature |
| 0x0B7AC | 518 | 1 | animation + motion + sound | - | $14 $16 $18 $1A $20 $21 | signature |
| 0x0B9B2 | 698 | 3 | animation + motion + sound | - | $14 $16 $18 $20 $21 $22 | signature |
| 0x0BAA0 | 138 | 1 | collision + claim/lock + palette + sound | - | $09 $2E $3C $60 $62 $66 | signature |
| 0x0BB2A | 234 | 1 | collision + draws + despawns + palette | - | $00 $06 $0C $22 $2E $30 | signature |
| 0x0BBC6 | 102 | 1 | object routine | - | $2E | signature |
| 0x0BE18 | 72 | 1 | animation + sound | - | $20 $21 $22 $24 $72 | signature |
| 0x0C4EA | 518 | 2 | animation + motion + sound | - | $14 $16 $18 $1A $20 $21 | signature |
| 0x0C6F0 | 782 | 3 | animation + motion + sound | - | $14 $16 $18 $20 $21 $22 | signature |
| 0x0C7E6 | 182 | 1 | collision + claim/lock + palette + sound | - | $2E $3C $60 $62 $66 $80 | signature |
| 0x0C89C | 246 | 1 | collision + animation + draws + despawns + motion + state change + palette | - | $00 $02 $06 $0C $10 $14 | signature |
| 0x0C992 | 34 | 1 | animation + draws | - | $21 $22 | signature |
| 0x0C9B4 | 102 | 1 | object routine | - | $2E | signature |
| 0x0CBDC | 66 | 1 | palette | - | $09 $80 | signature |
| 0x0CC1E | 92 | 1 | despawns + palette | - | $-60 $-5E $-40 $-3C $00 $0C | signature |
| 0x0CCD0 | 72 | 1 | animation + sound | - | $20 $21 $22 $24 $72 | signature |
| 0x0D448 | 54 | 1 | leaf/helper | - | - | signature |
| 0x0D47E | 58 | 36 | hitbox_a_expand | - | $0C $10 $30 $31 $32 $33 | READ |
| 0x0D4B8 | 58 | 25 | hitbox_b_expand | - | $0C $10 $50 $51 $52 $53 | READ |
| 0x0D4F2 | 58 | 6 | hitbox_c_expand | - | $0C $10 $5C $5D $5E $5F | READ |
| 0x0D54E | 84 | 0 | collision | - | $30 $34 $36 $38 $3A $4B | signature |
| 0x0D5A2 | 4 | 1 | leaf/helper | - | - | signature |
| 0x0D5A6 | 98 | 34 | walk_player_slots | - | - | READ |
| 0x0D608 | 84 | 1 | collision | - | $30 $34 $36 $38 $3A $4B | signature |
| 0x0D65C | 124 | 1 | collision | - | $23 $30 $34 $36 $38 $3A | signature |
| 0x0D6D8 | 100 | 1 | leaf/helper | - | - | signature |
| 0x0D73C | 58 | 1 | collision | - | $4B $54 $56 $58 $5A | signature |
| 0x0D776 | 152 | 3 | object routine | - | $0C $10 $2E $4C | signature |
| 0x0D80E | 78 | 1 | leaf/helper | - | - | signature |
| 0x0D868 | 94 | 1 | object routine | - | $00 $08 $09 $0C $10 $20 | signature |
| 0x0D8BA | 28 | 17 | collide_box_b | - | - | READ |
| 0x0D8D6 | 198 | 1 | collision + motion | - | $0C $0E $10 $12 $14 $1A | signature |
| 0x0D99C | 90 | 17 | floor_collide | - | $10 $12 $1A $2C $54 $56 | READ |
| 0x0D9F6 | 94 | 1 | state change + palette | - | $00 $4A | signature |
| 0x0DABC | 220 | 1 | claim/lock + despawns + motion + palette + sound | - | $00 $0A $0C $1A $20 $3E | signature |
| 0x0DB98 | 32 | 1 | sound | - | $24 | signature |
| 0x0DBB8 | 184 | 1 | draws + palette | - | $0C $10 | signature |
| 0x0DC70 | 68 | 0 | draws + despawns + motion + palette | - | $00 $10 $4E $50 $52 | signature |
| 0x0DCB4 | 2 | 1 | leaf/helper | - | - | signature |
| 0x0DE46 | 16 | 0 | leaf/helper | - | - | signature |
| 0x0E032 | 32 | 6 | leaf/helper | - | - | signature |
| 0x0E052 | 150 | 1 | object routine | - | $00 | signature |
| 0x0E0F4 | 130 | 1 | object routine | - | $00 $08 $09 $0C $10 $20 | signature |
| 0x0E16A | 126 | 1 | despawns | - | $00 $08 $78 $80 | signature |
| 0x0E1E8 | 36 | 10 | tick_kill_counters | - | $7C | READ |
| 0x0E20C | 48 | 4 | claim/lock | - | $0C $10 $28 $2A $2E $2F | signature |
| 0x0E23C | 42 | 5 | object routine | - | $2E $40 | signature |
| 0x0E266 | 92 | 5 | object routine | - | $7C | signature |
| 0x0E2C2 | 26 | 1 | object routine | - | $40 | signature |
| 0x0E2DC | 20 | 3 | object routine | - | $2F | signature |
| 0x0E2F0 | 24 | 2 | object routine | - | $20 | signature |
| 0x0E316 | 294 | 0 | claim/lock + palette + sound | - | $0B $0C $10 $1E $20 $3C | signature |
| 0x0E42E | 70 | 1 | animation | - | $00 $08 $0C $10 $21 $22 | signature |
| 0x0E474 | 1494 | 0 | collision + animation + claim/lock + draws + despawns + motion + palette + sound | - | $00 $08 $0B $0C $10 $14 | signature |
| 0x0E974 | 20 | 1 | motion | - | $14 $1A $1E | signature |
| 0x0EA5E | 264 | 0 | animation + claim/lock + draws + motion | - | $10 $14 $1A $21 $22 $23 | signature |
| 0x0EDF0 | 26 | 0 | object routine | - | $00 $08 | signature |
| 0x0EE80 | 452 | 0 | collision + animation + claim/lock + draws + despawns + motion + palette + sound | - | $00 $0A $0C $10 $14 $20 | signature |
| 0x0F14C | 64 | 1 | animation | - | $0C $21 $22 $24 $2E $4A | signature |
| 0x0F18C | 14 | 2 | motion | - | $14 $16 $18 | signature |
| 0x0F19A | 280 | 0 | animation + motion | - | $2C $4B | signature |
| 0x0F1AA | 22 | 0 | object routine | - | $21 $22 $4B | signature |
| 0x0F1C0 | 202 | 0 | claim/lock + despawns + palette | - | $00 $0A $21 $22 $3E $4B | signature |
| 0x0F28A | 200 | 1 | palette | - | $00 $0A $0B $0C $10 $2F | signature |
| 0x0F352 | 238 | 0 | collision + animation + draws + despawns + motion + palette + sound | - | $00 $06 $0A $0C $10 $14 | signature |
| 0x0F440 | 70 | 1 | object routine | - | $30 | signature |
| 0x0F486 | 74 | 1 | collision | - | $34 $36 $38 $3A $4B | signature |
| 0x0F4D0 | 98 | 1 | collision | - | $34 $36 $38 $3A $4B | signature |
| 0x0F724 | 24 | 1 | object routine | - | $20 | signature |
| 0x0F742 | 8 | 0 | object routine | - | $00 | signature |
| 0x0F77C | 30 | 0 | leaf/helper | - | - | signature |
| 0x0F8C0 | 74 | 0 | animation | - | $00 $08 $0C $10 $21 $22 | signature |
| 0x100B0 | 38 | 2 | object routine | - | $00 $7F | signature |
| 0x102F4 | 8 | 1 | object routine | - | $00 | signature |
| 0x102FC | 24 | 1 | object routine | - | $20 | signature |
| 0x10394 | 76 | 0 | animation | - | $00 $08 $20 $21 $22 $23 | signature |
| 0x103E0 | 1092 | 0 | collision + animation + claim/lock + draws + despawns + motion + palette + sound | - | $00 $10 $14 $1A $21 $22 | signature |
| 0x10824 | 320 | 0 | collision + animation + claim/lock + despawns + motion + palette | - | $00 $14 $1A $1C $1E $21 | signature |
| 0x10CFE | 8 | 1 | object routine | - | $00 | signature |
| 0x10D06 | 24 | 1 | object routine | - | $20 | signature |
| 0x10D1E | 20 | 0 | claim/lock + despawns | - | $00 $3C | signature |
| 0x10E06 | 26 | 0 | despawns + palette | - | $00 | signature |
| 0x1124E | 220 | 0 | draws + palette + sound | - | $0C $10 $20 $2E $2F $78 | signature |
| 0x1132A | 92 | 0 | claim/lock + draws + despawns + motion + palette | - | $00 $3C $4E $50 $52 $7F | signature |
| 0x11386 | 42 | 6 | object routine | - | $0C $2E $7E | signature |
| 0x1176A | 204 | 0 | motion + palette | - | $00 $0B $0C $10 $14 $1C | signature |
| 0x11836 | 80 | 1 | animation + motion | - | $00 $08 $14 $20 $21 $22 | signature |
| 0x11886 | 880 | 0 | collision + animation + draws + motion + sound | - | $00 $08 $4B $78 | signature |
| 0x118C6 | 28 | 1 | despawns + palette | - | $00 | signature |
| 0x11CE8 | 296 | 0 | collision + animation + claim/lock + despawns + motion + palette | - | $00 $0C $10 $14 $1A $21 | signature |
| 0x1208E | 146 | 0 | motion + palette | - | $00 $08 $0B $14 $1C $1E | signature |
| 0x1211A | 508 | 0 | collision + animation + claim/lock + draws + despawns + motion + palette + sound | - | $00 $0C $10 $14 $1A $21 | signature |
| 0x12422 | 160 | 0 | motion + palette | - | $00 $08 $0B $14 $1C $1E | signature |
| 0x124BC | 1008 | 0 | collision + animation + claim/lock + draws + despawns + motion + palette + sound | - | $00 $06 $0C $10 $14 $1A | signature |
| 0x12B08 | 378 | 0 | motion + palette | - | $00 $0B $10 $1A $1C $1E | signature |
| 0x12C70 | 74 | 1 | animation + motion | - | $00 $08 $14 $21 $22 $23 | signature |
| 0x12CBA | 428 | 0 | animation + draws + despawns + motion + palette + sound | - | $00 $08 $09 $10 $14 $1A | signature |
| 0x12F0A | 98 | 0 | claim/lock + despawns + palette | - | $00 $0C $10 $28 $2A $2E | signature |
| 0x132A0 | 144 | 0 | claim/lock | - | $0C $2E $3C $4A $78 | signature |
| 0x13324 | 28 | 1 | object routine | - | $00 $08 $2F | signature |
| 0x13340 | 52 | 1 | animation + palette | - | $00 $0C $10 $21 $22 $23 | signature |
| 0x13374 | 1124 | 0 | collision + animation + claim/lock + draws + despawns + motion + palette + sound | - | $00 $10 $14 $1C $21 $22 | signature |
| 0x1349A | 100 | 1 | object routine | - | $0C $2E $6C | signature |
| 0x13776 | 86 | 1 | claim/lock + motion + state change + palette | - | $02 $06 $09 $0B $0C $10 | signature |
| 0x137CC | 20 | 0 | claim/lock + despawns | - | $00 $3C | signature |
| 0x137E0 | 32 | 0 | claim/lock + draws + motion | - | $3E $4E | signature |
| 0x13AA6 | 134 | 0 | animation + palette | - | $00 $08 $1C $1E $20 $21 | signature |
| 0x14152 | 60 | 1 | collision | - | $54 $56 | signature |
| 0x1435C | 128 | 0 | animation + palette | - | $00 $08 $1C $1E $20 $21 | signature |
| 0x14A78 | 2 | 0 | leaf/helper | - | - | signature |
| 0x14A8C | 228 | 1 | claim/lock + palette + sound | - | $0C $10 $1E $20 $3C $3E | signature |
| 0x14B5E | 90 | 1 | animation + palette | - | $00 $08 $0C $10 $20 $21 | signature |
| 0x152E2 | 2 | 0 | leaf/helper | - | - | signature |
| 0x15306 | 264 | 1 | claim/lock + palette | - | $1C $1E $20 $2E $3C $4A | signature |
| 0x153EC | 72 | 1 | animation | - | $00 $08 $20 $21 $22 $23 | signature |
| 0x15434 | 1422 | 0 | collision + animation + claim/lock + draws + despawns + motion + sound | - | $00 $08 $23 $40 $4B $78 | signature |
| 0x154A6 | 10 | 1 | leaf/helper | - | - | signature |
| 0x154B0 | 16 | 1 | despawns + palette | - | $00 | signature |
| 0x154C0 | 34 | 1 | leaf/helper | - | - | signature |
| 0x15B1E | 774 | 0 | animation + claim/lock + despawns + motion + state change + palette + sound | - | $00 $02 $06 $09 $0B $0C | signature |
| 0x16484 | 134 | 0 | animation + palette | - | $00 $08 $1C $1E $20 $21 | signature |
| 0x1650A | 972 | 0 | collision + animation + claim/lock + draws + despawns + motion + palette + sound | - | $00 $14 $1A $21 $22 $23 | signature |
| 0x16B6A | 134 | 1 | animation + motion + state change + palette | - | $00 $02 $0B $14 $1C $1E | signature |
| 0x16BF0 | 124 | 0 | state change | - | $00 $02 $21 $22 $6C | signature |
| 0x16C6C | 144 | 0 | animation + claim/lock + motion + state change + sound | - | $00 $02 $14 $21 $22 $24 | signature |
| 0x16CE0 | 92 | 0 | collision + animation + draws + motion | - | $0C $2C $2D | signature |
| 0x16D7E | 134 | 0 | claim/lock + palette + sound | - | $20 $23 $3E $4E | signature |
| 0x16E04 | 26 | 2 | leaf/helper | - | - | signature |
| 0x16E1E | 118 | 1 | animation + claim/lock + state change + palette | - | $00 $02 $09 $0B $21 $22 | signature |
| 0x16E94 | 64 | 1 | animation + claim/lock + despawns + palette | - | $00 $3C | signature |
| 0x16ED4 | 14 | 2 | object routine | - | $44 | signature |
| 0x16EE2 | 12 | 3 | leaf/helper | - | - | signature |
| 0x16EEE | 24 | 1 | leaf/helper | - | - | signature |
| 0x16F06 | 272 | 2 | animation + state change + palette | - | $00 $02 $09 $0A $0B $0C | signature |
| 0x17016 | 134 | 1 | animation + draws + despawns + palette | - | $00 $06 $0A $20 $21 $22 | signature |
| 0x1709C | 170 | 1 | arcade hw | pal | - | signature |
| 0x17146 | 54 | 5 | leaf/helper | - | - | signature |
| 0x17180 | 8 | 3 | leaf/helper | - | - | signature |
| 0x17188 | 114 | 1 | leaf/helper | - | - | signature |
| 0x171FA | 36 | 1 | leaf/helper | - | - | signature |
| 0x1768A | 20 | 1 | leaf/helper | - | - | signature |
| 0x1769E | 214 | 0 | claim/lock + state change + palette | - | $02 $0A $0B $22 $23 $2F | signature |
| 0x17774 | 92 | 1 | claim/lock + palette | - | $00 $06 $09 $0A $0C $10 | signature |
| 0x177D0 | 58 | 5 | despawns + palette | - | $0A $10 $44 | signature |
| 0x1782C | 48 | 1 | animation + state change | - | $02 $0C $10 $21 $22 $24 | signature |
| 0x1785C | 78 | 0 | animation + palette | - | $0A $10 $21 $22 | signature |
| 0x178AA | 48 | 1 | animation + state change | - | $02 $0C $10 $21 $22 $24 | signature |
| 0x178DA | 114 | 0 | animation + palette | - | $0A $10 $21 $22 $6C | signature |
| 0x1794C | 140 | 1 | claim/lock + despawns + state change + sound | - | $02 $0C $10 $20 $22 $23 | signature |
| 0x179D8 | 364 | 0 | claim/lock + state change + palette + sound | - | $02 $0A $10 $22 $23 $3E | signature |
| 0x17B44 | 16 | 0 | state change + sound | - | $02 | signature |
| 0x17B54 | 52 | 1 | claim/lock + despawns + state change + palette | - | $02 $20 $22 $3E $3F | signature |
| 0x17B88 | 10 | 0 | object routine | - | $22 | signature |
| 0x17B9A | 1 | 0 | leaf/helper | - | - | signature |
| 0x17BDC | 24 | 0 | leaf/helper | - | - | signature |
| 0x17BF8 | 34 | 1 | leaf/helper | - | - | signature |
| 0x17C1A | 168 | 1 | palette + sound | - | $0C $10 $6C | signature |
| 0x17CC2 | 88 | 0 | animation + claim/lock + motion + state change | - | $02 $16 $1A $1C $1E $21 | signature |
| 0x17D1A | 44 | 0 | animation + motion + state change | - | $02 $14 $21 $22 $24 $4B | signature |
| 0x17D46 | 44 | 0 | animation + state change | - | $02 $21 $22 $24 $2C $4B | signature |
| 0x17D72 | 148 | 0 | collision + animation + draws + despawns + motion + palette | - | $00 $21 $22 $2C $2D $30 | signature |
| 0x180C2 | 100 | 0 | claim/lock + palette | - | $00 $06 $09 $0A $0C $10 | signature |
| 0x18146 | 158 | 0 | despawns + state change | - | $02 $21 $22 | signature |
| 0x18162 | 130 | 0 | animation + palette | - | $0A $10 $21 $22 $24 $4B | signature |
| 0x181E4 | 40 | 0 | animation + state change | - | $02 $21 $22 $24 $6C | signature |
| 0x1820C | 130 | 0 | animation + claim/lock + palette | - | $0A $10 $21 $22 $3E $4B | signature |
| 0x1828E | 54 | 0 | animation + despawns | - | $-5F $21 $22 $4B | signature |
| 0x182C4 | 30 | 0 | despawns | - | $4B | signature |
| 0x182E2 | 122 | 0 | claim/lock + despawns + state change + sound | - | $02 $20 $22 $23 $3E $3F | signature |
| 0x1835C | 172 | 0 | claim/lock + state change + sound | - | $02 $10 $22 $23 $3E $3F | signature |
| 0x18408 | 16 | 0 | state change + sound | - | $02 | signature |
| 0x18418 | 52 | 1 | claim/lock + despawns + state change + palette | - | $02 $20 $22 $3E $3F | signature |
| 0x1844C | 16 | 0 | object routine | - | $22 | signature |
| 0x18472 | 24 | 0 | leaf/helper | - | - | signature |
| 0x1848E | 242 | 1 | palette + sound | - | $0A $0B $0C $10 $6C $74 | signature |
| 0x18580 | 272 | 0 | collision + animation + claim/lock + draws + motion + state change + palette | - | $00 $02 $0A $14 $16 $1A | signature |
| 0x18690 | 60 | 0 | animation + draws + despawns + palette | - | $00 $0A $21 $22 $74 $75 | signature |
| 0x1892A | 286 | 0 | animation + state change + palette | - | $00 $02 $0A $0B $21 $22 | signature |
| 0x18A48 | 222 | 1 | animation + claim/lock + state change + sound | - | $0C $20 $23 $3E $3F $4B | signature |
| 0x18AFC | 78 | 1 | animation | - | $0C $10 $21 $22 | signature |
| 0x18B4A | 70 | 1 | animation | - | $0C $10 $21 $22 | signature |
| 0x18B90 | 242 | 1 | animation + claim/lock + despawns + palette | - | $0A $0C $10 $21 $22 $3E | signature |
| 0x18C82 | 26 | 1 | leaf/helper | - | - | signature |
| 0x18D50 | 290 | 0 | collision + animation + draws + motion + state change | - | $02 $21 $22 $23 $24 $2C | signature |
| 0x18D8E | 42 | 1 | animation + draws + despawns + palette | - | $00 $21 $22 | signature |
| 0x18EC6 | 16 | 0 | state change + sound | - | $02 | signature |
| 0x18ED6 | 38 | 1 | claim/lock + despawns + state change + palette | - | $02 $20 $22 $3E | signature |
| 0x18EFC | 16 | 0 | object routine | - | $22 | signature |
| 0x1918A | 84 | 1 | leaf/helper | - | - | signature |
| 0x191DE | 256 | 1 | state change + palette | - | $00 $02 $06 $0A $0B $16 | signature |
| 0x192DE | 328 | 1 | animation + claim/lock + draws + motion + state change + palette + sound | - | $02 $0A $0C $14 $16 $18 | signature |
| 0x19426 | 16 | 0 | state change + sound | - | $02 | signature |
| 0x19436 | 38 | 1 | claim/lock + despawns + state change + palette | - | $02 $20 $22 $3E | signature |
| 0x1945C | 16 | 0 | object routine | - | $22 | signature |
| 0x1946C | 52 | 1 | draws + despawns + palette | - | $0A $0C $10 | signature |
| 0x194A0 | 156 | 1 | claim/lock | - | $0C $10 $20 $22 $23 $3E | signature |
| 0x1953C | 90 | 1 | motion + state change + sound | - | $02 $08 $14 $23 | signature |
| 0x19596 | 48 | 1 | despawns + palette | - | $00 $0C $23 | signature |
| 0x195C6 | 102 | 1 | draws + motion | - | $30 $31 $32 $33 $48 $4E | signature |
| 0x1962C | 132 | 1 | palette | - | $40 $44 | signature |
| 0x196B0 | 286 | 0 | collision + animation + draws + motion | - | $02 $21 $22 $24 $4B | signature |
| 0x196D8 | 42 | 0 | animation + draws + despawns + palette | - | $00 $21 $22 | signature |
| 0x199C0 | 68 | 0 | leaf/helper | - | - | signature |
| 0x19A04 | 218 | 1 | animation + claim/lock + despawns + state change + palette | - | $00 $02 $08 $09 $0A $0B | signature |
| 0x19ADE | 522 | 1 | collision + animation + claim/lock + draws + despawns + motion + state change + palette + sound | - | $00 $02 $0A $0B $20 $22 | signature |
| 0x19BF0 | 34 | 1 | despawns + palette | - | $-80 $-76 $-51 $00 $0A $2F | signature |
| 0x19CF8 | 38 | 0 | object routine | - | $44 $6C | signature |
| 0x19D1E | 24 | 1 | leaf/helper | - | - | signature |
| 0x19F66 | 32 | 0 | object routine | - | $24 | signature |
| 0x19F86 | 76 | 1 | state change + palette | - | $00 $02 $09 $0B $21 $22 | signature |
| 0x19FD2 | 48 | 0 | animation + despawns + palette | - | $00 $21 $22 | signature |
| 0x1A002 | 86 | 1 | animation | - | $00 $0C $10 $21 $24 $2E | signature |
| 0x1A406 | 220 | 1 | leaf/helper | - | - | signature |
| 0x1A4E2 | 74 | 1 | arcade hw | pal | - | signature |
| 0x1A52C | 94 | 1 | arcade hw | vram | - | signature |
| 0x1A58A | 20 | 2 | leaf/helper | - | - | signature |
| 0x1A59E | 114 | 1 | state change + palette | - | $00 $02 $06 $0B $0C $10 | signature |
| 0x1A610 | 150 | 1 | draws + despawns + palette + sound | - | $00 $06 $0C $10 $22 $40 | signature |
| 0x1A6A6 | 20 | 1 | leaf/helper | - | - | signature |
| 0x1A6BA | 60 | 1 | leaf/helper | - | - | signature |
| 0x1A800 | 102 | 2 | claim/lock + motion | - | $0E $1A $1E $22 $26 $3A | signature |
| 0x1A866 | 38 | 1 | object routine | - | $10 | signature |
| 0x1A88C | 114 | 1 | object routine | - | $00 $0E $46 | signature |
| 0x1A8FE | 12 | 2 | object routine | - | $0E $10 | signature |
| 0x1A90A | 26 | 1 | leaf/helper | - | - | signature |
| 0x1A9E6 | 32 | 0 | arcade hw | io | $0C $0D | signature |
| 0x1AA06 | 70 | 0 | arcade hw | text | $08 $1A $26 $3A $3E $42 | signature |
| 0x1AA4C | 16 | 1 | leaf/helper | - | - | signature |
| 0x1AA5C | 74 | 0 | arcade hw | text | $00 $04 $10 $14 $16 $18 | signature |
| 0x1AAA6 | 16 | 1 | leaf/helper | - | - | signature |
| 0x1AACA | 12 | 2 | leaf/helper | - | - | signature |
| 0x1AAD6 | 8 | 2 | leaf/helper | - | - | signature |
| 0x1AADE | 20 | 2 | leaf/helper | - | - | signature |
| 0x1AAF2 | 92 | 1 | leaf/helper | - | - | signature |
| 0x1AB4E | 12 | 1 | leaf/helper | - | - | signature |
| 0x1AB5A | 16 | 1 | leaf/helper | - | - | signature |
| 0x1AB6A | 56 | 3 | leaf/helper | - | - | signature |
| 0x1ABA2 | 36 | 2 | leaf/helper | - | - | signature |
| 0x1ABC6 | 34 | 1 | leaf/helper | - | - | signature |
| 0x1ABE8 | 38 | 1 | leaf/helper | - | - | signature |
| 0x1AC0E | 24 | 1 | leaf/helper | - | - | signature |
| 0x1AC26 | 26 | 1 | leaf/helper | - | - | signature |
| 0x1AC40 | 2 | 1 | leaf/helper | - | - | signature |
| 0x1AC42 | 20 | 1 | leaf/helper | - | - | signature |
| 0x1AC56 | 32 | 0 | arcade hw | text | - | signature |
| 0x1AC76 | 4 | 6 | leaf/helper | - | - | signature |
| 0x1AC7A | 4 | 1 | leaf/helper | - | - | signature |
| 0x1AC7E | 6 | 6 | leaf/helper | - | - | signature |
| 0x1AC8A | 20 | 0 | arcade hw | text | - | signature |
| 0x1AC9E | 44 | 0 | arcade hw | text | - | signature |
| 0x1ACCA | 14 | 0 | arcade hw | text | - | signature |
| 0x1ACD8 | 20 | 0 | arcade hw | vram | - | signature |
| 0x1AF4C | 42 | 1 | object routine | - | $48 | signature |
| 0x1B0BE | 138 | 0 | arcade hw | objram pal text | - | signature |
| 0x1B160 | 24 | 2 | leaf/helper | - | - | signature |
| 0x1B178 | 64 | 2 | arcade hw | io | - | signature |
| 0x1B1B8 | 20 | 1 | leaf/helper | - | - | signature |
| 0x1B228 | 68 | 0 | leaf/helper | - | - | signature |
| 0x1B26C | 102 | 0 | arcade hw | text | - | signature |
| 0x1B2D2 | 56 | 1 | leaf/helper | - | - | signature |
| 0x1B30A | 74 | 0 | arcade hw | text | - | signature |
| 0x1B476 | 22 | 0 | arcade hw | text | - | signature |
| 0x1B4F2 | 14 | 0 | leaf/helper | - | - | signature |
| 0x1B7CE | 14 | 0 | arcade hw | text | - | signature |
| 0x1B7DC | 58 | 0 | arcade hw | text | - | signature |
| 0x1B816 | 32 | 0 | leaf/helper | - | - | signature |
| 0x1B81E | 22 | 0 | arcade hw | text | - | signature |
| 0x1B834 | 18 | 0 | leaf/helper | - | - | signature |
| 0x1B846 | 82 | 0 | leaf/helper | - | - | signature |
| 0x1B852 | 4 | 0 | leaf/helper | - | - | signature |
| 0x1B856 | 68 | 0 | arcade hw | text | - | signature |
| 0x1B8EA | 36 | 1 | leaf/helper | - | - | signature |
| 0x1B9AA | 54 | 0 | leaf/helper | - | - | signature |
| 0x1B9F6 | 34 | 0 | arcade hw | vram | - | signature |
| 0x1BA18 | 16 | 1 | leaf/helper | - | - | signature |
| 0x1BA28 | 12 | 1 | leaf/helper | - | - | signature |
| 0x1BA34 | 54 | 0 | arcade hw | text vram | - | signature |
| 0x1BA6A | 76 | 1 | leaf/helper | - | - | signature |
| 0x1BAB6 | 22 | 1 | arcade hw | pal | - | signature |
| 0x1BACC | 12 | 1 | leaf/helper | - | - | signature |
| 0x1BAE2 | 50 | 2 | leaf/helper | - | - | signature |
| 0x1BB14 | 4 | 8 | leaf/helper | - | - | signature |
| 0x1BB22 | 6 | 5 | leaf/helper | - | - | signature |

## Class totals

- leaf/helper              169
- object routine           56
- arcade hw                48
- animation + motion + sound 12
- despawns + palette       10
- motion                   9
- animation                9
- animation + palette      9
- animation + state change 7
- collision                7
- collision + animation + claim/lock + draws + despawns + motion + palette + sound 7
- claim/lock + palette     6
- animation + draws + despawns + palette 6
- palette                  6
- animation + sound        5
- claim/lock               5
- state change + sound     5
- motion + palette         5
- state change             4
- animation + motion       4
- claim/lock + state change + sound 4
- state change + palette   4
- claim/lock + despawns + state change + palette 4
- sound                    3
- claim/lock + despawns + palette 3
- palette + sound          3
- animation + claim/lock + state change + palette 3
- collision + claim/lock + palette + sound 3
- claim/lock + palette + sound 3
- animation + motion + palette + sound 2
- claim/lock + state change + palette 2
- claim/lock + draws + despawns + palette 2
- animation + despawns + palette 2
- animation + motion + state change 2
- animation + despawns     2
- animation + motion + state change + palette 2
- animation + claim/lock + motion + state change + sound 2
- animation + claim/lock + draws + despawns + palette 2
- claim/lock + state change 2
- animation + claim/lock + state change + palette + sound 2
- animation + draws        2
- draws + despawns + motion + palette 2
- draws + palette          2
- despawns                 2
- collision + animation + claim/lock + despawns + motion + palette 2
- claim/lock + despawns    2
- collision + animation + draws + motion 2
- animation + claim/lock + despawns + palette 2
- animation + state change + palette 2
- claim/lock + despawns + state change + sound 2
- test_mode_screen         1
- read_controls_or_demo    1
- credit_prompt_select     1
- unpack_level_tilemap     1
- tilemap_unpack_hi        1
- tilemap_unpack_lo        1
- tilemap_block_04         1
- tilemap_block_a5         1
- draws + motion + state change + palette 1
- irq4_handler             1
- palette_queue_drain      1
- input_edges              1
- colour_cycle_streamer    1
- sky_palette_gradient     1
- sound_enqueue            1
- clear_textram            1
- clear_tileram            1
- clear_spriteram_and_pool 1
- sky_palette_flat         1
- set_level_palettes       1
- wait_frame_flag          1
- object_dispatcher        1
- textram_stride_write     1
- textram_clear_run        1
- draw_credits_line        1
- request_palette_update   1
- allocate_palette_slot    1
- release_palette_slot     1
- build_palette_upload_queue 1
- sprite_frame_lookup      1
- order_list_insert        1
- zoom_scale_lookup        1
- sprite_build_depth_banded 1
- sprite_build_and_cull    1
- hide_object_sprite       1
- integrate_position       1
- clamp_x_velocity         1
- clamp_y_velocity         1
- rng_next                 1
- claim/lock + draws + despawns + state change + sound 1
- claim/lock + despawns + state change 1
- claim/lock + draws + state change 1
- claim/lock + draws       1
- claim/lock + draws + state change + palette 1
- animation + claim/lock + draws + motion + state change 1
- animation + claim/lock + despawns + motion + palette 1
- animation + claim/lock + state change 1
- restore_position         1
- animate_variant          1
- animation_peek_next      1
- animate                  1
- animation_frame_fetch    1
- despawns + palette + sound 1
- collision + animation + draws + despawns + state change + palette 1
- claim/lock + motion + state change + palette + sound 1
- draws + despawns         1
- despawns + state change + palette 1
- collision + draws + despawns + palette 1
- collision + animation + draws + despawns + motion + state change + palette 1
- hitbox_a_expand          1
- hitbox_b_expand          1
- hitbox_c_expand          1
- walk_player_slots        1
- collide_box_b            1
- collision + motion       1
- floor_collide            1
- claim/lock + despawns + motion + palette + sound 1
- tick_kill_counters       1
- animation + claim/lock + draws + motion 1
- collision + animation + draws + despawns + motion + palette + sound 1
- draws + palette + sound  1
- claim/lock + draws + despawns + motion + palette 1
- collision + animation + draws + motion + sound 1
- animation + draws + despawns + motion + palette + sound 1
- claim/lock + motion + state change + palette 1
- claim/lock + draws + motion 1
- collision + animation + claim/lock + draws + despawns + motion + sound 1
- animation + claim/lock + despawns + motion + state change + palette + sound 1
- claim/lock + state change + palette + sound 1
- animation + claim/lock + motion + state change 1
- collision + animation + draws + despawns + motion + palette 1
- despawns + state change  1
- animation + claim/lock + palette 1
- collision + animation + claim/lock + draws + motion + state change + palette 1
- animation + claim/lock + state change + sound 1
- collision + animation + draws + motion + state change 1
- animation + claim/lock + draws + motion + state change + palette + sound 1
- draws + despawns + palette 1
- motion + state change + sound 1
- draws + motion           1
- animation + claim/lock + despawns + state change + palette 1
- collision + animation + claim/lock + draws + despawns + motion + state change + palette + sound 1
- draws + despawns + palette + sound 1
- claim/lock + motion      1
