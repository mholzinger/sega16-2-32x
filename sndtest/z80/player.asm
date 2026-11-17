; =====================================================================
; SEGA16 SOUND ENGINE — Z80 STREAMING MUSIC PLAYER (docs/sound/SOUND.md P3)
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
; Tempo: YM2612 Timer A paces the 1 ms waits; Timer B keeps the true
; time. A polled overflow flag says only "at least one tick passed", so a
; 1 ms Timer A tick that elapses while the player is busy -- a burst of
; writes, the 68K holding the bus to refill -- is lost: measured
; 2026-10-30 on ares, 2.4% behind the score at 5 s, 4.6% at 33 s, ~7.8%
; over Rise's pass (tools/music_ab.py, the Z80's RD_BLK vs score time).
; Timer B (NB=128, 38.45 ms) loses a tick only if the Z80 is stopped for
; over 38 ms -- the 68K's ring refill holds the bus for 15+ ms while it
; decompresses and parses a block, so a 15.6 ms Timer B still lost 2.5-5%
; (measured, TBN). Its overflows add real time into CREDIT; every Timer A
; tick spends 1 ms of it, and while CREDIT says the player is >45 ms
; behind (more than one Timer B lump), each tick also skips one waited ms
; (catch-up at 2x, never a stall). Timer A is set slightly SLOW (NA=970,
; 1.0138 ms) so it, not the coarse clock, paces normally. (A 68K vblank
; count was tried first: the 68K loop misses ~5% of frames and the cart
; runs with interrupts masked.) The STREAM MUST NEVER WRITE REG $26/$27.
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
.DEFINE CREDIT   $0FF4   ; player-private, 16-bit signed: real time not yet waited, 1/16 ms
.DEFINE TBN      $0FF6   ; instrument: Timer B overflows seen, 16-bit (x 38.45 ms = the player's idea of time)
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
	; Timer A = 970: period = (1024-NA) * 144 / 7670454 = 1.0138 ms
	; (was 971 = 0.9949 ms; slightly slow on purpose, see the header).
	; THE FACTOR IS 144 (24 operators x prescale 6 — ymfm_fm.ipp
	; timer code), NOT 72: the first build used 72, every tick ran
	; 2.008 ms, and the whole score played at half speed (Mike heard
	; it 2026-09-01). Measured, not derived twice.
	ld b,$24
	ld c,$F2                 ; NA[9:2] = 242
	call ym_w0
	ld b,$25
	ld c,$02                 ; NA[1:0] = 2
	call ym_w0
	; Timer B = 128: (256-NB) * 2304 / 7670454 = 38.447 ms = 615.2/16.
	ld b,$26
	ld c,$80                 ; NB = 128
	call ym_w0
	ld b,$27
	ld c,$3F                 ; load A+B, enable A+B flags, reset both
	call ym_w0
	ld hl,0                  ; the clock starts now
	ld (CREDIT),hl
	ld (TBN),hl
	ld hl,RING

play_loop:
	ld a,(YM_A0)             ; Timer B is polled at every opcode, not only in
	bit 1,a                  ; waits: a long burst of writes must not lose it
	call nz,timer_b
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
	bit 1,a                  ; status bit1 = Timer B overflow
	call nz,timer_b          ; 38.45 ms of real time into CREDIT
	ld a,(YM_A0)
	rrca                     ; status bit0 = Timer A overflow
	jr nc,tick_loop
	push bc
	ld b,$27
	ld c,$1F                 ; reset the A flag; loads and enables kept
	call ym_w0
	pop bc
	call credit_tick         ; carry = behind: this tick also skips one ms
	jr nc,_tl_next
	ld a,b
	dec a
	jr z,_tl_refund          ; last tick of this wait: nothing left to skip
	dec b                    ; skip one waited ms
_tl_next:
	djnz tick_loop
	jr play_loop
_tl_refund:
	push hl                  ; give the unused skip's 16 back
	ld hl,(CREDIT)
	ld de,16
	add hl,de
	ld (CREDIT),hl
	pop hl
	jr play_loop             ; b was 1: this tick ends the wait

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
	jr nz,_nb_have
	ld a,(YM_A0)             ; empty: starve-stall, but keep the clock
	bit 1,a                  ; running so the catch-up recovers the stall
	call nz,timer_b          ; (measured: Timer B lost 2.5-5% while starving)
	jr _nb_wait
_nb_have:
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

; ---- the catch-up clock (Timer B, CREDIT in 1/16 ms) -----------------
; timer_b: CREDIT += 615 (38.45 ms), capped at 100 ms so a long stall is
; dropped rather than rushed; resets the B flag. Preserves bc and hl.
timer_b:
	push bc
	push hl
	ld b,$27
	ld c,$2F                 ; reset the B flag; loads and enables kept
	call ym_w0
	ld hl,(TBN)
	inc hl
	ld (TBN),hl
	ld hl,(CREDIT)
	ld de,615
	add hl,de
	bit 7,h
	jr nz,_tb_store          ; still negative: no cap needed
	ld a,h
	cp 7                     ; >= 1792 (112 ms)?
	jr c,_tb_store
	ld hl,1600               ; cap at 100 ms
_tb_store:
	ld (CREDIT),hl
	pop hl
	pop bc
	ret

; credit_tick: one Timer A tick spends 1 ms (16) of CREDIT. Carry set
; when the player is still >45 ms behind: the caller skips one waited ms,
; which spends another 16. CREDIT is floored at -64 (4 ms ahead), so a
; Timer A slightly faster than Timer B cannot bank a debt.
; Preserves bc and hl.
credit_tick:
	push hl
	ld hl,(CREDIT)
	ld de,$FFF0              ; -16
	add hl,de
	bit 7,h
	jr nz,_ct_neg
	; BEHIND = more than 45 ms (720). Real time arrives in 38.4 ms lumps
	; (Timer B) while ticks spend it 1 ms at a time, so CREDIT normally
	; swings 0..38.4; a 2 ms threshold read that swing as lateness and
	; skipped most of each lump (19% fast, 2026-10-30).
	ld a,h
	cp 3
	jr nc,_ct_skip           ; >= 768
	cp 2
	jr c,_ct_store           ; < 512: on time
	ld a,l
	cp $D0
	jr c,_ct_store           ; 512..719: on time
_ct_skip:
	ld de,$FFF0
	add hl,de                ; the skipped ms
	ld (CREDIT),hl
	pop hl
	scf
	ret
_ct_neg:
	ld a,h
	cp $FF
	jr nz,_ct_floor          ; below -256
	ld a,l
	cp $C0
	jr nc,_ct_store          ; -64 .. -1
_ct_floor:
	ld hl,$FFC0              ; -64
_ct_store:
	ld (CREDIT),hl
	pop hl
	or a                     ; carry clear
	ret
