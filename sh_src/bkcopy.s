! THE BAKED-ART ROW LOOP (R60BAKEART, 2026-11-04). sh_src/r60/r60_main.c r60_bk_sprite fills
! an argument block and calls r60_bk_rows once a record: every row of the baked frame is copied
! from the cart into the overwrite image with the record's 32X bank added to opaque pens, and
! the row's span word, end x, and pixel count are produced exactly as r60_sprite's ROWFAST
! formulas would. Two measurements put this here (R60RIGSPLIT + the per-path timers, Neff
! tape, first wave): gcc's C copy loop was no faster than the decoder (plot 119 -> 127 lines a
! pass: stack spills, three branches a group, libgcc calls for the variable shift, the SH-2
! having no barrel shifter), and with only the group loop in asm the rows still cost 352
! cycles each, ~250 of them the C bookkeeping around a ~90-cycle loop.
!
!   void r60_bk_rows(uint32_t *a)        a: 16 longwords
!     [0]  src     the frame's first row header (cart, cached view); after the u16 h, u16 w
!     [1]  rowp    the overwrite image at (ys * FB_W + xs): the first row's aligned start
!     [2]  nrows   rows to copy (in); sp_full (out: 1 when a span word did not fit)
!     [3]  base4   bank << 4 in every byte
!     [4]  ncap    (lim - x0 + 3) >> 2: the plotter processes word j only while x0 + 4j < lim
!     [5]  x0      the record's x (canvas coordinates, >= xo)
!     [6]  xs      x0 & ~3
!     [7]  lim     FB_W + xo
!     [8]  spw     the span write pointer (in/out)
!     [9]  sp_end  its limit
!     [10] sp_hdr  (ks_cur & 63) << 16 | (xs >> 2) << 8
!     [11] y       the first row's y
!     [12] sel     x0 & 3: 0 aligned, 1/2/3 = shift 8/16/24
!     [13] cap     FB_W + 512
!     [14] npx     out: 4 x the groups whose source pens were not all zero (r60_rowpix's share)
!     [15] xmax    out: the largest row end x; optional run-plan input before
!                  the row loop initializes xmax (R60_ZOOM_RUN only)
!
! Per row (the C it replaces, kept there as the comment):
!     hdr = *src++; j0 = hdr >> 24; ng = (hdr >> 16) & 0xFF; nw = hdr & 0xFFFF
!     nproc = min(nw, ncap); kend = j0 >= nproc ? 0 : min(nproc - j0, ng)
!     trail = xs + 4 * (j0 + kend) < lim          (the next word's store, or the pend rule)
!     npx += copy(src, rowp + 4 * j0, kend, sel, trail); src += ng
!     xe = x0 + 4 * nproc; xe4 = min((xe + 3) & ~3, cap)
!     if xe4 > xs: w = sp_hdr | y << 24 | xe4 >> 2; *spw++ = w, or sp_full
!     xmax = max(xmax, xe); rowp += FB_W; y++
!
! The group loop stores what PLOT5 stores: a group only when its shifted value is non-zero
! (literal zero pixels stay skipped by the overwrite image), the carry as the next
! group's store or by the pend rule. One loop per shift (shlr8/16, shll8/16 only).
.ifdef R60_BAKEART
		.section .ramtext2.bk,"ax"
		.align	2
		.global	_r60_bk_rows
! normal row-loop registers: r8 y, r9 ncap, r10 spw, r11 base4,
!                            r12 a (the block), r13 rowp, r14 src
! R60_BK_HOIST registers:    r8 ncap, r9 bank, r10 bias, r11 carry,
!                            r12 a (the block), r13 rowp, r14 src
! stack: @r15 = rows left
_r60_bk_rows:
		mov.l	r8,@-r15
		mov.l	r9,@-r15
		mov.l	r10,@-r15
		mov.l	r11,@-r15
		mov.l	r12,@-r15
		mov.l	r13,@-r15
		mov.l	r14,@-r15
		sts.l	pr,@-r15
		mov	r4,r12
		mov.l	@(0,r12),r14		! src
		mov.l	@(4,r12),r13		! rowp
.ifdef R60_ZOOM_RUN
		mov.l	@(60,r12),r0		! plan cursor lives below the row counter
		mov.l	r0,@-r15
.endif
		mov.l	@(8,r12),r0		! nrows
		mov.l	r0,@-r15

.ifdef R60_BK_HOIST
	! These values are invariant across every row of this sprite.  The old
	! inner call rebuilt them and saved/restored r8-r11 once per row.
	mov.l	@(12,r12),r9		! base4 -> numeric bank
	shlr16	r9
	shlr8	r9
	shlr2	r9
	shlr2	r9
	mov.l	.Lnibble_bias,r10
	mov.l	.Lnibble_carry,r11
	mov.l	@(16,r12),r8		! ncap; y remains in argument slot 11
.else
	mov.l	@(12,r12),r11		! base4
	mov.l	@(16,r12),r9		! ncap
	mov.l	@(32,r12),r10		! spw
	mov.l	@(44,r12),r8		! y
.endif
		mov	#0,r0
		mov.l	r0,@(8,r12)		! sp_full = 0
		mov.l	r0,@(56,r12)		! npx = 0
		mov.l	r0,@(60,r12)		! xmax = 0
		mov.l	@r15,r0
		tst	r0,r0
		bt	.Lrows_done
.Lrow:
		mov.l	@r14+,r1		! hdr
		mov	r1,r6
		shlr16	r6
		mov	r6,r5
		shlr8	r5			! r5 = j0
		extu.b	r6,r6			! r6 = ng
		extu.w	r1,r1			! r1 = nw
.ifdef R60_RAW_ZOOM_EXTENTS
		exts.w	r1,r0
		mov.l	r0,@(0,r12)		! positive AB1 words / negative ZB1 pixels
		cmp/pz	r0
		bt	.Lzb_extent_ready
		neg	r0,r1			! ZB1 negative extent -> exact pixels
		add	#3,r1
		shlr2	r1			! group count for the unchanged copy loop
.Lzb_extent_ready:
.endif
.ifdef R60_BK_HOIST
	cmp/hs	r8,r1			! nw >= ncap ?
	bf	1f
	mov	r8,r1			! nproc = min(nw, ncap)
.else
	cmp/hs	r9,r1			! nw >= ncap ?
		bf	1f
		mov	r9,r1			! nproc = min(nw, ncap)
.endif
1:		mov	r1,r7
		sub	r5,r7			! r7 = nproc - j0
		cmp/pl	r7			! > 0 ?
		bt	2f
		mov	#0,r7			! kend = 0
		bra	3f
		nop
2:		cmp/hs	r6,r7			! nproc - j0 >= ng ?
		bf	3f
		mov	r6,r7			! kend = min(nproc - j0, ng)
3:		mov.l	@(24,r12),r0		! xs
		mov	r5,r2
		add	r7,r2			! j0 + kend
		shll2	r2
		add	r0,r2			! xs + 4 (j0 + kend)
		mov.l	@(28,r12),r0		! lim
		cmp/gt	r2,r0			! lim > that ?
		movt	r2			! r2 = trail
		mov	r5,r0
		shll2	r0
		add	r13,r0
		mov	r0,r5			! r5 = dst = rowp + 4 j0
		mov	r6,r0
		shll2	r0
		mov.l	r0,@(4,r12)		! stash 4 ng (rowp's slot is free: r13 holds it)
		mov.l	r1,@(16,r12)		! stash nproc (ncap's slot: r9 holds it)
.ifdef R60_ZOOM_RUN
		mov.l	@(4,r15),r1		! hoisted: hide the plan-pointer load latency
.endif
		mov	r14,r4			! src = the row's groups
		mov	r7,r6			! n = kend
.ifdef R60_BK_HOIST
	mov.l	@(12,r12),r7		! base4; transform constants stay hoisted
.else
	mov	r11,r7			! base4
.endif
		mov.l	@(48,r12),r3		! sel

.ifdef R60_ZOOM_RUN
		tst	r1,r1
		bf	.Lplan_row		! scaled plan out of the plain fall-through
.Lplain_copy:
.endif
		bsr	.Lcopy
		nop
.ifdef R60_ZOOM_RUN
.Lcopy_done:
.endif
		mov.l	@(56,r12),r0		! npx += rnp
		add	r1,r0
		mov.l	r0,@(56,r12)
		mov.l	@(4,r12),r0
		add	r0,r14			! src += ng
		mov.l	@(16,r12),r1		! nproc
		shll2	r1
.ifdef R60_RAW_ZOOM_EXTENTS
		mov.l	@(0,r12),r0
		cmp/pz	r0
		bt	.Lzb_end_ready
		neg	r0,r1			! restore exact end, not padded group end
.Lzb_end_ready:
.endif
		mov.l	@(20,r12),r0		! x0
		add	r0,r1			! r1 = xe
		mov.l	@(60,r12),r0		! xmax
		cmp/gt	r0,r1
		bf	4f
		mov.l	r1,@(60,r12)
4:		mov	r1,r2
		add	#3,r2
		mov	#-4,r0
		and	r0,r2			! xe4
		mov.l	@(52,r12),r0		! cap
		cmp/gt	r0,r2
		bf	5f
		mov	r0,r2
5:		mov.l	@(24,r12),r0		! xs
		cmp/gt	r0,r2			! xe4 > xs ?
		bf	.Lnext
		mov.l	@(40,r12),r0		! sp_hdr
.ifdef R60_BK_HOIST
	mov.l	@(44,r12),r3		! y
.else
		mov	r8,r3
.endif
		shll16	r3
		shll8	r3			! y << 24
		or	r3,r0
		shlr2	r2
		or	r2,r0			! w
.ifdef R60_BK_HOIST
	mov.l	@(32,r12),r1		! spw
	mov.l	@(36,r12),r2		! sp_end
	cmp/hs	r2,r1			! spw >= sp_end ?
		bt	6f
	mov.l	r0,@r1
	add	#4,r1
	mov.l	r1,@(32,r12)
		bra	.Lnext
	nop
.else
	mov.l	@(36,r12),r1		! sp_end
	cmp/hs	r1,r10			! spw >= sp_end ?
	bt	6f
	mov.l	r0,@r10
	bra	.Lnext
	add	#4,r10
.endif
6:		mov	#1,r0
		mov.l	r0,@(8,r12)		! sp_full
.Lnext:
		mov.w	.Lfbw,r0
		add	r0,r13			! rowp += FB_W
.ifdef R60_BK_HOIST
	mov.l	@(44,r12),r0
	add	#1,r0
	mov.l	r0,@(44,r12)		! y++
.else
		add	#1,r8			! y++
.endif
		mov.l	@r15,r0
		dt	r0
		mov.l	r0,@r15
		bf	.Lrow
.Lrows_done:
.ifndef R60_BK_HOIST
		mov.l	r10,@(32,r12)		! spw out
.endif
.ifdef R60_ZOOM_RUN
		add	#8,r15
.else
		add	#4,r15
.endif
		lds.l	@r15+,pr
		mov.l	@r15+,r14
		mov.l	@r15+,r13
		mov.l	@r15+,r12
		mov.l	@r15+,r11
		mov.l	@r15+,r10
		mov.l	@r15+,r9
		rts
		mov.l	@r15+,r8

.ifdef R60_ZOOM_RUN
! Validate against this row's resolved kend even though the live caller admits
! plans only for complete atlas rows. A malformed or stale plan falls back;
! it must never turn the unsigned suffix loop into a huge overwrite.
.Lplan_row:
		mov.w	@r1,r0			! big-endian u8 start, u8 count
		extu.w	r0,r0
		add	#2,r1
		mov.l	r1,@(4,r15)		! advance even when this row has no interval
		mov	r0,r1
		extu.b	r1,r1			! count
		tst	r1,r1
		bt	.Lplain_copy
		shlr8	r0
		extu.b	r0,r0			! start
		add	r1,r0			! exclusive end
		cmp/hi	r6,r0			! end > kend
		bt	.Lplain_copy
		sub	r1,r0
		shll8	r0
		or	r1,r0			! restore start<<8 | count
		bsr	.Lcopy_plan
		nop
		bra	.Lcopy_done
		nop
.endif
		.align	2
.Lfbw:		.word	320

! Only opaque pens acquire bank bits. OR-ing base4 into zero bytes turns
! transparency into a nonzero overwrite-image store, punching through sprites
! underneath (wolf113/zombie59 pixels at arcade2728, measured 2026-11-05).
! Each byte is0..14: adding15 carries into bit4 iff nonzero. Multiplying
! those independent bit4 flags by bank0..15 cannot carry between bytes.
		.macro BK_COLOR
		mov	r0,r3
		add	r10,r3
		and	r11,r3
		mul.l	r9,r3
		sts	macl,r3
		or	r3,r0
		.endm
! the group loop: r4 src, r5 dst, r6 n, r7 base4, r3 sel, r2 trail -> r1 rnp. Clobbers r0-r7,MACL.
.Lcopy:
.ifdef R60_SPRRAW
        tst     r7,r7
        bf      .Lbanked
        bra     .Lrawcopy
.ifdef R60_RAW_TRAIL_R7
        mov     r2,r7
.else
        nop
.endif
.Lbanked:

.endif
.ifdef R60_BK_HOIST
	! Preserve only the trail predicate.  r8 is the outer word limit and
	! r9-r11 already hold bank/bias/carry for the complete sprite.
	mov.l	r2,@-r15
.else
		mov.l	r8,@-r15
		mov.l	r9,@-r15
		mov.l	r10,@-r15
		mov.l	r11,@-r15
		mov	r7,r9
		shlr16	r9
		shlr8	r9
		shlr2	r9
		shlr2	r9			! bank number, 0..15
		mov.l	.Lnibble_bias,r10
		mov.l	.Lnibble_carry,r11
		mov	r2,r8			! trail
.endif
		mov	#0,r1			! rnp
		mov	#0,r2			! carry
		tst	r6,r6
		bt	.Ldone
		mov	r3,r0
		cmp/eq	#1,r0
		bt	.La8
		cmp/eq	#2,r0
		bt	.La16
		cmp/eq	#3,r0
		bt	.La24
! aligned: out = acc, no carry
.La0:		mov.l	@r4+,r0
		BK_COLOR
		tst	r0,r0
		bt	1f
		mov.l	r0,@r5
		add	#4,r1
1:		dt	r6
		bf.s	.La0
		add	#4,r5
		bra	.Ldone
		nop
! shift 8: out = carry | acc >> 8, carry = acc << 24
.La8:		mov.l	@r4+,r0
		BK_COLOR
		mov	r0,r3
		shlr8	r3
		or	r2,r3
		mov	r0,r2
		shll16	r2
		shll8	r2
		tst	r3,r3
		bt	1f
		mov.l	r3,@r5
1:		tst	r0,r0
		bt	2f
		add	#4,r1
2:		dt	r6
		bf.s	.La8
		add	#4,r5
		bra	.Ltrail
		nop
! shift 16: out = carry | acc >> 16, carry = acc << 16
.La16:		mov.l	@r4+,r0
		BK_COLOR
		mov	r0,r3
		shlr16	r3
		or	r2,r3
		mov	r0,r2
		shll16	r2
		tst	r3,r3
		bt	1f
		mov.l	r3,@r5
1:		tst	r0,r0
		bt	2f
		add	#4,r1
2:		dt	r6
		bf.s	.La16
		add	#4,r5
		bra	.Ltrail
		nop
! shift 24: out = carry | acc >> 24, carry = acc << 8
.La24:		mov.l	@r4+,r0
		BK_COLOR
		mov	r0,r3
		shlr16	r3
		shlr8	r3
		or	r2,r3
		mov	r0,r2
		shll8	r2
		tst	r3,r3
		bt	1f
		mov.l	r3,@r5
1:		tst	r0,r0
		bt	2f
		add	#4,r1
2:		dt	r6
		bf.s	.La24
		add	#4,r5
! the carry after the last group: the next word's store, or the pend rule (the caller decided)
.Ltrail:	tst	r2,r2
		bt	.Ldone
.ifdef R60_BK_HOIST
		mov.l	@r15,r3
		tst	r3,r3
.else
		tst	r8,r8
.endif
		bt	.Ldone
		mov.l	r2,@r5
.Ldone:
.ifdef R60_BK_HOIST
		rts
		add	#4,r15
.else
		mov.l	@r15+,r11
		mov.l	@r15+,r10
		mov.l	@r15+,r9
		rts
		mov.l	@r15+,r8
.endif
		.align	2
.Lnibble_bias:	.long	0x0F0F0F0F
.Lnibble_carry:	.long	0x10101010

.ifdef R60_ZOOM_RUN
! One branch per source-row segment, never per group. r0=start<<8|count;
! r1/r2 carry the established npx/shift carry across generic, opaque and
! generic segments. The baker emits only runs >=4 and proves every run pen is
! 1..14, so ORing base4 is exactly the normal palette transform.
		.macro BK_PLAN_SEGMENTS sel
		mov.l	@(4,r15),r6		! generic prefix
		tst	r6,r6
		bt	.Lpo\@
.Lpg0\@:
		mov.l	@r4+,r0
		BK_COLOR
.if \sel == 0
		tst	r0,r0
		bt	.Lpg0z\@
		mov.l	r0,@r5
		add	#4,r1
.Lpg0z\@:
.elseif \sel == 1
		mov	r0,r3
		shlr8	r3
		or	r2,r3
		mov	r0,r2
		shll16	r2
		shll8	r2
		tst	r3,r3
		bt	.Lpg0s\@
		mov.l	r3,@r5
.Lpg0s\@:
		tst	r0,r0
		bt	.Lpg0z\@
		add	#4,r1
.Lpg0z\@:
.elseif \sel == 2
		mov	r0,r3
		shlr16	r3
		or	r2,r3
		mov	r0,r2
		shll16	r2
		tst	r3,r3
		bt	.Lpg0s\@
		mov.l	r3,@r5
.Lpg0s\@:
		tst	r0,r0
		bt	.Lpg0z\@
		add	#4,r1
.Lpg0z\@:
.else
		mov	r0,r3
		shlr16	r3
		shlr8	r3
		or	r2,r3
		mov	r0,r2
		shll8	r2
		tst	r3,r3
		bt	.Lpg0s\@
		mov.l	r3,@r5
.Lpg0s\@:
		tst	r0,r0
		bt	.Lpg0z\@
		add	#4,r1
.Lpg0z\@:
.endif
		dt	r6
		bf.s	.Lpg0\@
		add	#4,r5
.Lpo\@:
		mov.l	@(8,r15),r6		! fully opaque interval
		tst	r6,r6
		bt	.Lps\@
.Lpol\@:
		mov.l	@r4+,r0
		or	r7,r0
.if \sel == 0
		mov.l	r0,@r5
.elseif \sel == 1
		mov	r0,r3
		shlr8	r3
		or	r2,r3
		mov	r0,r2
		shll16	r2
		shll8	r2
		mov.l	r3,@r5
.elseif \sel == 2
		mov	r0,r3
		shlr16	r3
		or	r2,r3
		mov	r0,r2
		shll16	r2
		mov.l	r3,@r5
.else
		mov	r0,r3
		shlr16	r3
		shlr8	r3
		or	r2,r3
		mov	r0,r2
		shll8	r2
		mov.l	r3,@r5
.endif
		add	#4,r1
		dt	r6
		bf.s	.Lpol\@
		add	#4,r5
.Lps\@:
		mov.l	@r15,r6		! generic suffix
		tst	r6,r6
		bt	.Lpe\@
.Lpgl\@:
		mov.l	@r4+,r0
		BK_COLOR
.if \sel == 0
		tst	r0,r0
		bt	.Lpgz\@
		mov.l	r0,@r5
		add	#4,r1
.Lpgz\@:
.elseif \sel == 1
		mov	r0,r3
		shlr8	r3
		or	r2,r3
		mov	r0,r2
		shll16	r2
		shll8	r2
		tst	r3,r3
		bt	.Lpgs\@
		mov.l	r3,@r5
.Lpgs\@:
		tst	r0,r0
		bt	.Lpgz\@
		add	#4,r1
.Lpgz\@:
.elseif \sel == 2
		mov	r0,r3
		shlr16	r3
		or	r2,r3
		mov	r0,r2
		shll16	r2
		tst	r3,r3
		bt	.Lpgs\@
		mov.l	r3,@r5
.Lpgs\@:
		tst	r0,r0
		bt	.Lpgz\@
		add	#4,r1
.Lpgz\@:
.else
		mov	r0,r3
		shlr16	r3
		shlr8	r3
		or	r2,r3
		mov	r0,r2
		shll8	r2
		tst	r3,r3
		bt	.Lpgs\@
		mov.l	r3,@r5
.Lpgs\@:
		tst	r0,r0
		bt	.Lpgz\@
		add	#4,r1
.Lpgz\@:
.endif
		dt	r6
		bf.s	.Lpgl\@
		add	#4,r5
.Lpe\@:
		bra	.Lplan_trail
		nop
		.endm

.Lcopy_plan:
		mov.l	r8,@-r15
		mov.l	r9,@-r15
		mov.l	r10,@-r15
		mov.l	r11,@-r15
		mov.l	r3,@-r15		! shift selector
		mov	r0,r11
		mov	r0,r3
		extu.b	r3,r3			! opaque count
		mov.l	r3,@-r15
		shlr8	r11
		extu.b	r11,r11		! generic prefix count
		mov.l	r11,@-r15
		mov	r6,r0
		sub	r11,r0
		sub	r3,r0			! generic suffix count
		mov.l	r0,@-r15
		mov	r2,r8			! trailing-carry permission
		mov	#0,r1
		mov	#0,r2
		mov	r7,r9
		shlr16	r9
		shlr8	r9
		shlr2	r9
		shlr2	r9			! bank number, 0..15
		mov.l	.Lplan_nibble_bias,r10
		mov.l	.Lplan_nibble_carry,r11
		mov.l	@(12,r15),r0
		cmp/eq	#1,r0
		bt	.Lplan8
		cmp/eq	#2,r0
		bt	.Lplan16
		cmp/eq	#3,r0
		bt	.Lplan24
		BK_PLAN_SEGMENTS 0
.Lplan8:
		BK_PLAN_SEGMENTS 1
.Lplan16:
		BK_PLAN_SEGMENTS 2
.Lplan24:
		BK_PLAN_SEGMENTS 3
.Lplan_trail:
		tst	r2,r2
		bt	.Lplan_done
		tst	r8,r8
		bt	.Lplan_done
		mov.l	r2,@r5
.Lplan_done:
		add	#16,r15
		mov.l	@r15+,r11
		mov.l	@r15+,r10
		mov.l	@r15+,r9
		rts
		mov.l	@r15+,r8
		.align	2
.Lplan_nibble_bias:	.long	0x0F0F0F0F
.Lplan_nibble_carry:	.long	0x10101010
.endif

.ifdef R60_SPRRAW
! Bank-zero source or a prepared palette-index packet: copy its ready bytes.
! Same shifted carry, zero skipping and rnp as the colored loop.  The optional
! R60_RAW_TRAIL_R7 path uses the dead base4 register for the trail predicate,
! avoiding one stack store/load per ready-byte row.
.Lrawcopy:
.ifdef R60_RAW_TRAIL_R7
.else
        mov.l   r8,@-r15
        mov     r2,r8
.endif
        mov     #0,r1
        mov     #0,r2
        tst     r6,r6
        bt      .Lrawdone
        mov     r3,r0
        cmp/eq  #1,r0
        bt      .Lraw8
        cmp/eq  #2,r0
        bt      .Lraw16
        cmp/eq  #3,r0
        bt      .Lraw24
.Lraw0: mov.l   @r4+,r0
        tst     r0,r0
        bt      1f
        mov.l   r0,@r5
        add     #4,r1
1:      dt      r6
        bf.s    .Lraw0
        add     #4,r5
        bra     .Lrawdone
        nop
.Lraw8: mov.l   @r4+,r0
        mov     r0,r3
        shlr8   r3
        or      r2,r3
        mov     r0,r2
        shll16  r2
        shll8   r2
        tst     r3,r3
        bt      1f
        mov.l   r3,@r5
1:      tst     r0,r0
        bt      2f
        add     #4,r1
2:      dt      r6
        bf.s    .Lraw8
        add     #4,r5
        bra     .Lrawtrail
        nop
.Lraw16: mov.l  @r4+,r0
        mov     r0,r3
        shlr16  r3
        or      r2,r3
        mov     r0,r2
        shll16  r2
        tst     r3,r3
        bt      1f
        mov.l   r3,@r5
1:      tst     r0,r0
        bt      2f
        add     #4,r1
2:      dt      r6
        bf.s    .Lraw16
        add     #4,r5
        bra     .Lrawtrail
        nop
.Lraw24: mov.l  @r4+,r0
        mov     r0,r3
        shlr16  r3
        shlr8   r3
        or      r2,r3
        mov     r0,r2
        shll8   r2
        tst     r3,r3
        bt      1f
        mov.l   r3,@r5
1:      tst     r0,r0
        bt      2f
        add     #4,r1
2:      dt      r6
        bf.s    .Lraw24
        add     #4,r5
.Lrawtrail:
        tst     r2,r2
        bt      .Lrawdone
.ifdef R60_RAW_TRAIL_R7
        tst     r7,r7
.else
        tst     r8,r8
.endif
        bt      .Lrawdone
        mov.l   r2,@r5
.Lrawdone:
.ifdef R60_RAW_TRAIL_R7
        rts
        nop
.else
        rts
        mov.l   @r15+,r8
.endif
.endif

! PROBE (R60BKBENCH): the aligned group loop alone, for the memory-cost microbenchmark in
! r60_main.c r60_bk_bench: r4 src, r5 dst, r6 n (> 0), r7 base4 -> r0 = 4 x non-zero groups
		.global	_r60_bk_bench_copy
_r60_bk_bench_copy:
		mov	#0,r1
1:		mov.l	@r4+,r0
		tst	r0,r0
		bt.s	2f
		or	r7,r0
		mov.l	r0,@r5
		add	#4,r1
2:		dt	r6
		bf.s	1b
		add	#4,r5
		rts
		mov	r1,r0
.endif
