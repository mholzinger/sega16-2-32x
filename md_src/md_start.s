#include "common.h"

	.section .text.keepboot

/* poor man's debug - set bg color */
.macro SETBG _color
		move.l	#0xC0000000,(0xC00004)
		move.w	#\_color,(0xC00000)
.endm

/* set bg color and STOP */
.macro TRPBG _color
		move.l	#0xC0000000,(0xC00004)
		move.w	#\_color,(0xC00000)
	101:
		bra		101b
.endm

/* same as above but keep setting bg color */
.macro TRPBG2 _color
	101:
		move.l	#0xC0000000,(0xC00004)
		move.w	#\_color,(0xC00000)
		bra		101b
.endm


_irq_jmptab:	/* fixed 6-byte entries — trampolines at cart 0x200 index into this */
		.word	0x4EF9
		.long	_start
		.word	0x4EF9
		.long	_except
		.word	0x4EF9
		.long	_hblank
		.word	0x4EF9
		.long	_vblank

_except:
		TRPBG2	0x00E

_start:
	;// Clear Work RAM
		moveq	#0,d0
		move.w	#0x3FFF,d1
		suba.l	a1,a1
	1:
		move.l	d0,-(a1)
		dbf		d1,1b

	;// Copy initialized variables from ROM to Work RAM.
	;// .data is linked at 0xFF0100 (0xFF0000-0xFF00FF is reserved for the
	;// game boot's mapper-mirror writes); the clear loop leaves a1=0xFF0000,
	;// so set the copy destination explicitly.
		lea		__text_end,a0
		lea		0xFF0100,a1
		move.w	#__data_size,d0
		lsr.w	#1,d0
		subq.w	#1,d0
	2:
		move.w	(a0)+,(a1)+
		dbf		d0,2b

	init_joypads:
		lea		IO_BASE,a0
		move.b	#0x40,0x09(a0)
		move.b	#0x40,0x0B(a0)
		move.b	#0x40,0x03(a0)
		move.b	#0x40,0x05(a0)

	init_vdp:
		lea		VDP_CTRL_PORT,a1		/* VDP cmd/sts reg */
		move.w	#0x8000,d0				/* set VDP register 0 */
		move.w	#0x0100,d2
		lea		InitVDPRegs(pc),a0
		moveq	#18,d1
	init_vdp_reg:
		move.b	(a0)+,d0				/* lower byte = register data */
		move.w	d0,(a1)					/* set VDP register */
		add.w	d2,d0					/* + 0x0100 = next register */
		dbra	d1,init_vdp_reg

		move.l	#VRAM_ADDR_CMD,(a1)		/* write VRAM address 0 */
		lea		(VDP_DATA_PORT),a2		/* VDP data reg */
		lea		font_data(pc),a0
		move.w	#45*8-1,d2
	7:
		move.l	(a0)+,d0				/* font fg mask */
		move.l	d0,d1
		not.l	d1						/* font bg mask */
		andi.l	#0x11111111,d0			/* set font fg color */
		andi.l	#0x00000000,d1			/* set font bg color */
		or.l	d1,d0
		move.l	d0,(a2)					/* set tile line */
		dbra	d2,7b

		move.l	#0xC0000000,(a1)		/* write CRAM address 0 */
		move.l	#0x00000CCC,(a2)		/* entry 0 (black) and 1 (lt gray) BGR */
		move.l	#0xC0200000,(a1)		/* write CRAM address 32 */
		move.l	#0x000000A0,(a2)		/* entry 16 (black) BGR and 17 (green) */
		move.l	#0xC0400000,(a1)		/* write CRAM address 64 */
		move.l	#0x0000000A,(a2)		/* entry 32 (black) BGR and 33 (red) */

		move.b	#0,(0xA15107)			/* clear RV - allow SH2 to access ROM */
		move.w	#0,(JoypadState)		/* controller 1 */
		move.l	#0,(VBlankCounter)		/* clear the vblank count */
	0:
		cmp.l	#0x4D5F4F4B,(MARS_COMM0)	/* M_OK */
		bne.s	0b							/* wait for primary ok */
	1:
		cmp.l	#0x535F4F4B,(MARS_COMM4)	/* S_OK */
		bne.s	1b							/* wait for secondary ok */

		move.w	(0xA15100),d0
		or.w	#0x8000,d0
		move.w	d0,(0xA15100)		/* set FM - allow SH2 access to MARS hw */
		move.w	#0xACED,(MARS_COMM8)	/* MD-ready level: releases both SH-2s */

		/* Pad port init (canonical, per d32xr crt0 + SGDK JOY_reset): TH as
		 * an OUTPUT (CTRL bit6) idling HIGH on both ports. Was never done —
		 * the six-button phase table assumes the first transition each frame
		 * is a driven high->low edge; without CTRL setup TH is an input and
		 * the probe writes may not reach the controller at all. */
		move.b	#0x40,(IO_CTRL1)	/* 1P: TH output */
		move.b	#0x40,(IO_CTRL2)	/* 2P: TH output */
		move.b	#0x40,(IO_DATA1)	/* 1P: TH idle high */
		move.b	#0x40,(IO_DATA2)	/* 2P: TH idle high */

		lea		0xFFBFF0,sp			/* boot/shim stack, clear of game work RAM */

		jmp		main				/* RAM-resident; main sets RV=1 itself —
									   setting RV while executing from the
									   0x880000 window kills the fetch */

;// VDP register initialization values
InitVDPRegs:
		dc.b	0x14			/* 80 = HBL INT ON (level 4 = arcade IRQ4), read H/V cnt */
		dc.b	0x54			/* 81 = disp on, VBL INT OFF (adapter level-6 vector
								 * points at a 0x880000-window trampoline — forbidden
								 * under RV=1; ares enforces), DMA on, V28 mode */
		dc.b	0xC000 / 0x400	/* 82 = Name Tbl A */
		dc.b	0xB000 / 0x400	/* 83 = Name Tbl W */
		dc.b	0xE000 / 0x2000	/* 84 = Name Tbl B */
		dc.b	0xFE00 / 0x200	/* 85 = Sprite Attr Tbl */
		dc.b	0x00			/* 86 = always 0 */
		dc.b	0x00			/* 87 = BG color */
		dc.b	0x00			/* 88 = always 0 */
		dc.b	0x00			/* 89 = always 0 */
		dc.b	0xDF			/* 8A = HINT counter 223: one IRQ4 at the last
								 * active line = vblank cadence, arcade-style */
		dc.b	0x00			/* 8B = no EXT INT, full scroll */
		dc.b	0x81			/* 8C = H40 mode, no lace, no shadow/hilite */
		dc.b	0xFC00 / 0x400	/* 8D = HScroll Tbl */
		dc.b	0x00			/* 8E = always 0 */
		dc.b	0x02			/* 8F = data INC */
		dc.b	0x01			/* 90 = Scroll Size */
		dc.b	0x00			/* 91 = W Pos H = left */
		dc.b	0x00			/* 92 = W Pos V = top */

	.align  2

/****************************************************************************
 * All code below this line runs from Work RAM
 ***************************************************************************/

	.section .data

	.global read_joypad
read_joypad:
		PAUSE_Z80
		lea		IO_DATA1,a0
		move.b	5(sp),d0
		beq.s	1f
		lea		IO_DATA2,a0
	1:
		move.w	d2,-(sp)
		/* Mask interrupts across the 4-phase probe: a VINT landing between
		 * TH toggles desyncs the controller's six-button counter and returns
		 * garbage on real hardware (emulators are more forgiving). */
		move.w	sr,-(sp)
		move.w	#0x2700,sr
		bsr.s	get_input		/* - 0 s a 0 0 d u - 1 c b r l d u */
		move.w	d2,d1
		bsr.s	get_input		/* - 0 s a 0 0 d u - 1 c b r l d u */
		bsr.s	get_input		/* - 0 s a 0 0 0 0 - 1 c b m x y z */
		move.w	d2,d0
		bsr.s	get_input		/* - 0 s a 1 1 1 1 - 1 c b r l d u */
		move.w	(sp)+,sr		/* phases done; timing no longer critical */
		/* SGDK-canon detection. The strict 1111 test is KNOWN-FRAGILE:
		 * SGDK's own source documents six-button pads whose 4th phase reads
		 * 0s ("should be read as 1 but in some case we read 0" — wireless
		 * receivers especially), so requiring exact 1111 kills real pads.
		 * The old any-bit test failed the other way (3-button d/u idle HIGH
		 * faked six-button -> phantom M X Y Z from phase 3). Canon threads
		 * it: six-button iff the 3rd TH-low phase shows the 0000 marker AND
		 * the 4th TH-low r/l slots are not hard-zero (wired 0 on a 3-button
		 * pad; up+down-both is the impossible combo this guards against). */
		/* FIELD REVERT / DIAGNOSE (if a tester's pad misbehaves): the live probe
		 * word is on the pad-test HUD -- pause menu -> METRICS, the RAW: and lamp
		 * rows show exactly what this reads. A real six-button pad dropping to
		 * three-button here means this test is too STRICT; a three-button or
		 * wireless pad faking six-button (ghost MODE/X/Y/Z) means too LOOSE. To
		 * back the whole phantom-button fix out (68K + the SH-2 MODE-debounce +
		 * the START-clears-overlays companions): `git revert e5603cf`. To relax
		 * only this gate, drop the 0x0C00 r/l check just below and keep the
		 * phase-3 marker test alone. Origin: smokemonster's field report. */
		andi.w	#0x0C00,d2
		beq.s	9f				/* r/l slots hard-0: three-button pad */
		move.w	d0,d2
		andi.w	#0x0F00,d2
		beq.s	common			/* phase-3 0000 marker present: six-button */
	9:
		move.w	#0x010F,d0		/* three button pad: neutral M X Y Z */
	common:
		lsl.b	#4,d0			/* - 0 s a 0 0 0 0 m x y z 0 0 0 0 */
		lsl.w	#4,d0			/* 0 0 0 0 m x y z 0 0 0 0 0 0 0 0 */
		andi.w	#0x303F,d1		/* 0 0 s a 0 0 0 0 0 0 c b r l d u */
		move.b	d1,d0			/* 0 0 0 0 m x y z 0 0 c b r l d u */
		lsr.w	#6,d1			/* 0 0 0 0 0 0 0 0 s a 0 0 0 0 0 0 */
		or.w	d1,d0			/* 0 0 0 0 m x y z s a c b r l d u */
		eori.w	#0x1FFF,d0		/* 0 0 0 1 M X Y Z S A C B R L D U */
		RESUME_Z80
		move.w	(sp)+,d2
		rts

get_input:
		/* Settle time after each TH toggle: wired pads answer in ns, but
		 * wireless receivers/adapters latch async and return STALE phases
		 * when probed ~1us after the edge — they then fail the six-button
		 * signature and demote to 3-button (extended buttons dead). ~8 nops
		 * = ~4us per edge; whole 4-phase probe still well under 50us. */
		move.b	#0x00,(a0)
		nop
		nop
		nop
		nop
		nop
		nop
		nop
		nop
		move.b	(a0),d2
		move.b	#0x40,(a0)
		lsl.w	#8,d2
		nop
		nop
		nop
		nop
		nop
		nop
		move.b	(a0),d2
		rts

	.global _vblank
_vblank:
		move.l	2(sp),(0xFFB0F8)	/* diagnostics: interrupted (game) PC */
		movem.l	d0-d7/a0-a6,-(sp)
		/* burn-down aid: snapshot 16 words of the interrupted stack to
		 * 0xFFB100 — when a missed rebase pointer drops the game PC low,
		 * the return-address chain here names the jsr that did it. */
		lea		66(sp),a0			/* movem 60 bytes + frame SR2/PC4 */
		lea		0xFFB100,a1
		moveq	#15,d0
	9:	move.w	(a0)+,(a1)+
		dbf		d0,9b
		jsr		shim_vblank			/* C shim: MCU duties (md_main.c) */
		movem.l	(sp)+,d0-d7/a0-a6
		tst.w	(game_running)
		beq.s	1f
		jmp		(0x902AAC).l		/* game IRQ4 handler (rebased high copy);
									   its rte pops our frame */
	1:
_hblank:
		rte
