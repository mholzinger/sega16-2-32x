! The paged region of the cart: everything above the 4 MB the SH-2 and the 68K address
! straight. Reached through the SSF2 mapper (docs/design/SILICON.md 4e): the 68K writes a
! page number into a 512 KB slot's register (md_src/md_bank.c), and that slot of the low
! 4 MB then shows this page to both CPUs. Pages are numbered from the cart's start, so the
! first page here is page 8 (cart 0x400000). Linked by sh_src/mars4s.ld (CARTSSF=1) at
! 0x02400000; absent from every other build. Slot 7 (0x380000) is the window.
!
! Content (R60SOUND, 2026-10-31): page 8 = the music blob (sh_src/sound.bin, tools/sound_blob.py
! over the lab's render_music_lane.h: the six YM2612 channels of every song command; channels
! 6/7 are the SH-2 synth lane's). The 68K pages slot 7 to page 8 once at boot (md_main.c) and
! md_sound.c reads the blob through its resting bank-3 window at 0x980000. The SH-2s never
! read slot 7.
.ifdef R60_WORKSET_CODE
.ifndef CART_SSF
.error "R60_WORKSET_CODE requires CART_SSF"
.endif
.ifndef R60_WORKSET
.error "R60_WORKSET_CODE requires R60_WORKSET"
.endif
.if R60_WORKSET_CODE - 4096
.if R60_WORKSET_CODE - 8192
.error "R60_WORKSET_CODE must be4096 or8192"
.endif
.endif
.endif
.ifdef CART_SSF
.ifdef R60_WORKSET
		! Physical page 7 is distinct from the normal logical slot-7 level pages.
		! Runtime reads this data only after the quiescent mapper acknowledges 7.
.ifdef R60_WORKSET_CODE
		.section .spriteworkset.prefix,"a"
.else
		.section .spriteworkset,"a"
.endif
		.global _sprite_workset_page
_sprite_workset_page:
.ifdef R60_WORKSET_CODE
		! The sole linker page output owns the reserved code and final64-byte guard.
		.incbin "r60_workingset.bin",0,(0x80000-64-R60_WORKSET_CODE)
.else
		.incbin "r60_workingset.bin"
.endif
.endif
		.section .pages,"a"
		.global _pages_blob
_pages_blob:
.ifdef R60_SOUND
		.global _sound_page
_sound_page:
		.incbin "sound.bin"
.ifdef R60_BAKEART
		! window 0 of the baked art (R60BAKEART): the index + frames in the tail of page 8,
		! 16-aligned after the music blob. The Makefile derives its SH-2 address from
		! sound.bin's size (0x02380000 + the aligned size) and hands it to tools/bake_art.py.
		.balign	16, 0xFF
		.global _artbake_win0
_artbake_win0:
		.incbin "artbake_0.bin"
.endif
		.balign	0x80000, 0xFF
.ifdef ARTBAKE_PAGES
		! PER-LEVEL PAGES (R60BAKEPAGES, 2026-11-04): pages 9.. = the SAME music blob at the same
		! offsets (the 68K's reads never notice a switch) + that level's own baked tail
		! (tools/bake_art.py --pages). The 68K pages slot 7 to 8 + level; page 8 = level 1.
		! (unrolled: gas does not substitute an .irp parameter inside a quoted file name)
.if ARTBAKE_PAGES > 1
		.incbin "sound.bin"
		.balign	16, 0xFF
		.incbin "artbake_p9.bin"
		.balign	0x80000, 0xFF
.endif
.if ARTBAKE_PAGES > 2
		.incbin "sound.bin"
		.balign	16, 0xFF
		.incbin "artbake_p10.bin"
		.balign	0x80000, 0xFF
.endif
.if ARTBAKE_PAGES > 3
		.incbin "sound.bin"
		.balign	16, 0xFF
		.incbin "artbake_p11.bin"
		.balign	0x80000, 0xFF
.endif
.if ARTBAKE_PAGES > 4
		.incbin "sound.bin"
		.balign	16, 0xFF
		.incbin "artbake_p12.bin"
		.balign	0x80000, 0xFF
.endif
.endif
.else
		.fill	0x80000,1,0xFF
.endif
.endif
.ifdef LAUNCHER
		! THE LAUNCHER'S PAGE (LAUNCHER=1, tools/launcher_page.py): the page after the last
		! game page, wherever the game's layout ends. Its number is derived from this label
		! (sh_src/r60/r60_launcher.c), never written down. Shown in slot 7 only before the
		! game starts.
		.section .pages,"a"
		.balign	0x80000, 0xFF
		.global _launcher_page_blob
_launcher_page_blob:
		! the music blob first, at the offsets every slot-7 page has it (see the per-level
		! pages above): the 68K's vblank reads it whatever page is showing
		.incbin "sound.bin"
		.if (. - _launcher_page_blob) > LAUNCHER_BASE
		.error "sound.bin reaches the launcher at LAUNCHER_BASE: raise it in the Makefile and in introlab/sh/port.ld"
		.endif
		.space	LAUNCHER_BASE - (. - _launcher_page_blob), 0xFF
		.incbin "launcher_page.bin"
		.balign	0x80000, 0xFF
.endif
