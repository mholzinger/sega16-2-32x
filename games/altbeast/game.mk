# ALTERED BEAST: the title's build settings. Included by the top-level Makefile right after
# GAME is known; the engine parts (mk/*.mk) read these and nothing in them names a title.
# A second title supplies its own games/<title>/game.mk with the same variables. What each
# one does, and the measurement behind each entry, is written where it is used:
# mk/decomp.mk (the CPORT_* variable of the same name), mk/line.mk, mk/sndtest.mk.

# ---- the decompile-to-C pipeline (mk/decomp.mk) ------------------------------------
# the frame body's roots, and the boot entry added when the port boots the game itself
GAME_CPORT_ROOTS         = FUN_00003aae,FUN_0000144a,object_dispatcher,irq4_handler
GAME_CPORT_BOOT_ROOT     = FUN_0000040e
# 68K function -> hand replacement in games/altbeast/glue.c or sh_src/port_l2.c
GAME_CPORT_STUBS         = wait_frame_flag->port_frame_wait,FUN_0000e16a->port_despawn_index,walk_player_slots->port_walk_player_slots,FUN_00003774->port_bcd_score_add,FUN_00003716->port_score_tick,FUN_0000d80e->port_spawn_walk
# declarations narrowed to the width the program uses
GAME_CPORT_CLAMP         = FFFFF142,FFFFF028,FFFFF400
# 32-bit cells accessed as their high 16 bits, with the functions exempted
GAME_CPORT_HI            = FFFFF132@FUN_000039c8,FFFFC014@FUN_00005530
# address and data registers restored at call sites Ghidra dropped them from
GAME_CPORT_AREG          = 13,7
GAME_CPORT_DREG          = FUN_00004870
# functions whose pointers are normalised between 68K and SH-2 address spaces
GAME_CPORT_ANYNORM       = FUN_0000d868
GAME_CPORT_PARAMNORM     = FUN_00002622,FUN_00003972,textram_stride_write,FUN_00001732,tilemap_unpack_hi,tilemap_unpack_lo,FUN_0000258a,FUN_00003aa4,FUN_0000f28a,FUN_0000135e,FUN_0000469c,FUN_00004612,FUN_00010394,FUN_00001192,switchD_00002ef0__caseD_f0,FUN_000037d0,FUN_00004276,FUN_00004330,FUN_00004196,FUN_000058e4,FUN_00005e04,FUN_0000f8c0,FUN_00014b5e,FUN_000180c2
# data cells whose real access width differs from the declared type: ADDRESS=bytes
GAME_CPORT_WIDTH         = 00840000=2,0000DD8A=2,0000DD8C=2,0000DCEE=2,0000DCF0=2,0000DD3C=2,0000DD3E=2,0000DDD8=2,0000DDDA=2,00019846=2,00019850=2,0001984E=2,00004120=4,00004124=4,FFFFD400=2,FFFFD580=2,FFFFC460=2,FFFFC462=2,FFFFC464=2,FFFFC466=2,FFFFC48B=1,FFFFC4A0=1,FFFFC4E0=2,FFFFC4E2=2,FFFFC4E4=2,FFFFC4E6=2,FFFFD48A=1,FFFFD4EC=1,FFFFD4ED=1,FFFFD50A=1,FFFFD56A=1,FFFFD56B=1,FFFFD60B=1,FFFFD64E=1,FFFFD822=2,FFFFD900=2,FFFFDE80=2,FFFFDE89=1,FFFFDE8B=1,FFFFDEA1=1,FFFFDEA2=1,FFFFDEAF=1,FFFFDEC0=2,FFFFDEC4=2,FFFFDF21=1,FFFFDFA1=1,FFFFFC82=1,FFFFFC83=1
# hooks called after a marked call site: MARKER=function
GAME_CPORT_AFTER         = 574=port_region_boot
# entry guard of the native working-set emitter: FUNCTION:DEFINE:predicate
GAME_CPORT_ENTRY_GUARDS  = FUN_00002e50:R60_WORKSET:port_sound_mcu_ready
# the hot functions of the PORTHOT option (the line uses GAME_HOT_SET instead)
GAME_CPORT_HOT           = FUN_000103e0,FUN_0000669c,FUN_00010824

# ---- the shipping line (mk/line.mk) ------------------------------------------------
# functions placed in SDRAM, ranked from a profile (tools/hot_set.py)
GAME_HOT_SET             = tools/hot_set_macro.txt
# the zoomed-sprite atlas and the pose manifests under games/altbeast/graphics/
GAME_ZOOM_ATLAS          = neff45
GAME_ZOOM_MANIFEST       = neff-scaled-45.json
GAME_WORKSET_POSES       = neff-unscaled-22.json
# the sprite the macro packets are keyed on (tools/pack_sprite_macros.py --key)
GAME_MACRO_KEY           = 0x031401d0
# where tools/art_corpus.sh records the art census
GAME_ART_CORPUS          = build/gate/neff/art

# ---- the music (mk/sndtest.mk) -----------------------------------------------------
# the YM2151 logs the music is transcoded from, and the sound command each one answers
GAME_SND_PACK_NAME       = vgmrips.net 'Altered Beast (Sega System 16B)', eight .vgz files
GAME_SND_PACK            = srcref/audio/Altered_Beast_(Sega_System_16B)
GAME_SND_SONGS           = "0x90=03 Gaum Hermer (Boss Theme).vgz" \
           "0x91=02 Altered Beast (Power Up).vgz" \
           "0x92=08 Game Over ~ Name Entry.vgz" \
           "0x93=04 Crystal's Theme (Round Clear).vgz" \
           "0x94=01 Rise From Your Grave (Round 1, 4).vgz" \
           "0x95=05 Closed In Upon Me (Round 2, 5).vgz" \
           "0x96=06 I'll Be Back (Round 3).vgz" \
           "0x97=07 Who's Afraid of the Big Bad Wolf (Ending).vgz"
