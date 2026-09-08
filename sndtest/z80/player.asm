; =====================================================================
; SEGA16 SOUND ENGINE — Z80 STREAMING MUSIC PLAYER (SOUND.md P3)
;
; Executes from the Genesis Z80's 8KB RAM ($0000-$1FFF), uploaded by
; the 68K through the $A00000 window (the 32x-builder boot dance:
; release reset FIRST, request bus, copy, reset pulse, release).
;
; Architecture is d32xr's, derived not copied: the Z80 OWNS the
; YM2612 + PSG and executes a command stream from a 4KB ring in its
; own RAM; the Z80 cannot reach the cart (32X Tech Note 15), so the
; 68K pushes 512-byte blocks in under short busreq windows, once per
; vblank. Producer-blocking: if the 68K is late the ring runs dry and
; next_byte spins — music STALLS, never glitches.
;
; Tempo: YM2612 Timer A polled at NA=917 -> 1.0043 ms/tick. The
; STREAM MUST NEVER WRITE REG $27 — the timer is the player's clock.
;
; Stream opcodes (tools/mus_testgen.py + the P4 transcoder emit these):
;   $00-$7F        wait n ticks (~1ms each; 0 = no-op)
;   $F0 reg val    YM2612 part I  write ($4000/$4001)
;   $F1 reg val    YM2612 part II write ($4002/$4003)
;   $F2 val        PSG write ($7F11)
;   $FF            end of stream (sets MSTAT=2, spins; 68K restarts)
;
; Mailbox contract (fixed addresses, parsed into z80_player.h by
; tools/z80_pack.py from these .DEFINEs — asm is the single source):
.DEFINE RD_BLK   $0FF0   ; Z80 bumps per 512B block consumed
.DEFINE WR_BLK   $0FF1   ; 68K bumps per 512B block produced
.DEFINE MSTAT    $0FF2   ; 0 waiting-go, 1 playing, 2 ended
.DEFINE MGO      $0FF3   ; 68K sets 1 after priming the ring
.DEFINE RING     $1000   ; 8 x 512B blocks, $1000-$1FFF
.DEFINE STACK    $0FE0
; =====================================================================

.MEMORYMAP
DEFAULTSLOT 0
SLOTSIZE $2000
SLOT 0 $0000
.ENDME
.ROMBANKMAP
BANKSTOTAL 1
BANKSIZE $2000
BANKS 1
.ENDRO
.EMPTYFILL $00

.DEFINE YM_A0    $4000
.DEFINE YM_D0    $4001
.DEFINE YM_A1    $4002
.DEFINE YM_D1    $4003
.DEFINE PSG      $7F11

.BANK 0 SLOT 0
.ORG $0000
	di
	im 1
	jp start
.ORG $0038
	reti
.ORG $0066
	retn

.ORG $0100
start:
	ld sp,STACK
	xor a
	ld (RD_BLK),a
	ld (MSTAT),a
go_wait:                     ; 68K primes the ring, then sets MGO
	ld a,(MGO)
	or a
	jr z,go_wait
	ld a,1
	ld (MSTAT),a
	; Timer A = 971: period = (1024-NA) * 144 / 7670454 = 0.9949 ms.
	; THE FACTOR IS 144 (24 operators x prescale 6 — ymfm_fm.ipp
	; timer code), NOT 72: the first build used 72, every tick ran
	; 2.008 ms, and the whole score played at half speed (Mike heard
	; it 2026-09-01). Measured, not derived twice.
	ld b,$24
	ld c,$F2                 ; NA[9:2] = 242
	call ym_w0
	ld b,$25
	ld c,$03                 ; NA[1:0] = 3
	call ym_w0
	ld b,$27
	ld c,$15                 ; load A + enable A + reset A flag
	call ym_w0
	ld hl,RING

play_loop:
	call next_byte
	cp $80
	jr c,do_wait
	cp $F0
	jr z,op_ym0
	cp $F1
	jr z,op_ym1
	cp $F2
	jr z,op_psg
	cp $FF
	jr z,op_end
	jr play_loop             ; unknown opcode: skip

do_wait:
	or a
	jr z,play_loop
	ld b,a                   ; n ticks
tick_loop:
	ld a,(YM_A0)
	rrca                     ; status bit0 = Timer A overflow
	jr nc,tick_loop
	push bc
	ld b,$27
	ld c,$15                 ; clear flag; timer auto-reloads
	call ym_w0
	pop bc
	djnz tick_loop
	jr play_loop

op_ym0:
	call next_byte
	ld b,a
	call next_byte
	ld c,a
	call ym_w0
	jr play_loop
op_ym1:
	call next_byte
	ld b,a
	call next_byte
	ld c,a
	call ym_w1
	jr play_loop
op_psg:
	call next_byte
	ld (PSG),a
	jr play_loop
op_end:
	ld a,2
	ld (MSTAT),a
end_spin:
	jr end_spin              ; 68K notices MSTAT=2 and reboots us

; ---- YM2612 write, b=reg c=val, busy-honoured ------------------------
; Busy-poll PLUS fixed settle nops: the B00246 law from the 68K path —
; the real chip DROPS writes that land while busy, and a lying status
; read passes the poll instantly. A few Z80 nops (~1.1us each) after
; the address strobe is cheap insurance the 68K path already pays for.
ym_w0:
	call ym_busy
	ld a,b
	ld (YM_A0),a
	nop
	nop
	nop
	nop
	call ym_busy
	ld a,c
	ld (YM_D0),a
	ret
ym_w1:
	call ym_busy
	ld a,b
	ld (YM_A1),a
	nop
	nop
	nop
	nop
	call ym_busy
	ld a,c
	ld (YM_D1),a
	ret
ym_busy:
	ld a,(YM_A0)
	rlca                     ; bit7 = busy
	ret nc
	jr ym_busy

; ---- ring fetch: a = next stream byte; preserves bc ------------------
; Spins while the ring is empty (consumed counter == produced counter).
; HL is the read cursor; 512B block boundaries bump RD_BLK; $2000 wraps
; to $1000.
next_byte:
	push bc
_nb_wait:
	ld a,(WR_BLK)
	ld b,a
	ld a,(RD_BLK)
	cp b
	jr z,_nb_wait            ; empty: starve-stall
	ld a,(hl)
	ld c,a
	inc hl
	ld a,l
	or a
	jr nz,_nb_done           ; not a 256B boundary
	bit 0,h
	jr nz,_nb_done           ; odd page = mid-block
	ld a,h                   ; crossed a 512B block boundary
	cp $20
	jr nz,_nb_count
	ld h,$10                 ; $2000 wraps to $1000
_nb_count:
	ld a,(RD_BLK)
	inc a
	ld (RD_BLK),a
_nb_done:
	ld a,c
	pop bc
	ret
