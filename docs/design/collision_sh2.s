! collision_detection, 68K 0xD6D8-0xD73C, translated to SH-2 (SH7604).
! Two versions. Counted by tools/count_sh2.py; blank/! lines ignored.
!
! Register discipline, both versions:
!   r4  = a4 (the current object slot)      r5  = d0 (loop counter)
!   r13 = &globals(0xFFF028 equivalent)     r14 = state base (0xFFC000 equiv)
!   r7=0x480  r9=0x80  r10=0x400  r11=0x800  r12=0x80   (established once,
!   at scene setup, not per call -- they are loop-invariant table offsets)

! ---------------------------------------------------------------- LITERAL
! Same struct layout as the arcade, same control flow, instruction for
! instruction. This is the upper bound on a mechanical translation.
_literal:
	sts.l	pr, @-r15
	mov	r14, r4
	mov.b	@r13, r0
	tst	#1, r0
	bt	L1
	mov	#73, r0
	mov.b	@(r0,r4), r1
	tst	r1, r1
	bt	L1
	bsr	collision_bounds_check
	nop
L1:
	mov	r14, r4
	add	r10, r4
	mov.b	@(1,r13), r0
	tst	#1, r0
	bt	L2
	mov	#73, r0
	mov.b	@(r0,r4), r1
	tst	r1, r1
	bt	L2
	bsr	collision_bounds_check
	nop
L2:
	mov	r14, r4
	add	r11, r4
	mov	#24, r5
L3:
	mov	#73, r0
	mov.b	@(r0,r4), r1
	tst	r1, r1
	bt	L4
	bsr	collision_bounds_check
	nop
L4:
	add	r9, r4
	dt	r5
	bf	L3
	mov	r14, r4
	add	r12, r4
	mov.w	@r4, r1
	tst	r1, r1
	bt	L6
	mov	#73, r0
	mov.b	@(r0,r4), r1
	tst	r1, r1
	bt	L6
	bsr	collision_bounds_check
	nop
L6:
	mov	r14, r4
	add	r7, r4
	mov.w	@r4, r1
	tst	r1, r1
	bt	L7
	mov	#73, r0
	mov.b	@(r0,r4), r1
	tst	r1, r1
	bt	L7
	bsr	collision_bounds_check
	nop
L7:
	lds.l	@r15+, pr
	rts
	nop

! --------------------------------------------------------------- IDIOMATIC
! One change to the DATA, which a port controls: attack_active moves from
! struct offset 73 to offset 9, inside SH-2's 4-bit byte displacement. That
! removes the `mov #73,r0` + indexed load at all five test sites.
_idiomatic:
	sts.l	pr, @-r15
	mov	r14, r4
	mov.b	@r13, r0
	tst	#1, r0
	bt	M1
	mov.b	@(9,r4), r1
	tst	r1, r1
	bt	M1
	bsr	collision_bounds_check
	nop
M1:
	mov	r14, r4
	add	r10, r4
	mov.b	@(1,r13), r0
	tst	#1, r0
	bt	M2
	mov.b	@(9,r4), r1
	tst	r1, r1
	bt	M2
	bsr	collision_bounds_check
	nop
M2:
	mov	r14, r4
	add	r11, r4
	mov	#24, r5
M3:
	mov.b	@(9,r4), r1
	tst	r1, r1
	bt	M4
	bsr	collision_bounds_check
	nop
M4:
	add	r9, r4
	dt	r5
	bf	M3
	mov	r14, r4
	add	r12, r4
	mov.w	@r4, r1
	tst	r1, r1
	bt	M6
	mov.b	@(9,r4), r1
	tst	r1, r1
	bt	M6
	bsr	collision_bounds_check
	nop
M6:
	mov	r14, r4
	add	r7, r4
	mov.w	@r4, r1
	tst	r1, r1
	bt	M7
	mov.b	@(9,r4), r1
	tst	r1, r1
	bt	M7
	bsr	collision_bounds_check
	nop
M7:
	lds.l	@r15+, pr
	rts
	nop
