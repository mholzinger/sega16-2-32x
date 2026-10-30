/* ALTERED BEAST RUNTIME GLUE (framework 3e, 2026-10-29): hand replacements for this title's own
 * functions -- the packed-BCD score adder, the spawn and despawn walks, the player-slot walk,
 * the zoom lookup -- wired in by the Makefile's CPORT_STUB_WAIT. #included by sh_src/port_l2.c at
 * the point it was cut from, so the translation unit (and the rom) is unchanged; a second title
 * supplies its own games/<title>/glue.c. */

PORTCODE /* ---- PACKED BCD, HAND-WRITTEN FROM docs/audit/bcd_score_add.asm ----------------
 *
 * The 68000 has packed-BCD arithmetic and the game keeps its scores and counters in
 * it. FUN_00003774 is the score adder:
 *
 *     003774  lea   (0x8,A6),A1
 *     003778  move  #0x0,CCR
 *     00377C  abcd  -(A1),-(A0)      x4, LOW byte first, carry chaining through X
 *     003784  bcc.b 0x0000378c
 *     003786  move.l #-0x66666667,(A0)   ; saturate to 0x99999999 on overflow
 *
 * GHIDRA CANNOT EXPRESS IT. It renders each `abcd` as
 *     *p = *p + *q;  bcdAdjust(*p);
 * where `bcdAdjust` is an INTRINSIC IT DOES NOT DEFINE -- defs.c emits
 * `uint bcdAdjust(){return 0;}` -- and the call's result is discarded anyway. So the
 * port did a plain BINARY add and lost the decimal adjust AND the X-flag carry chain
 * that links the four bytes. Measured 2026-10-02: record 48's score bytes came out
 * 0xDCBC where the arcade has 0x8000.
 *
 * 151 abcd/sbcd/nbcd instructions in the program and 33 bcdAdjust call sites in the
 * unit, so this is the score and counter arithmetic throughout the game.
 *
 * Fourth member of the hand-write class, and the clearest case yet of the test:
 * not "is this wrong" but "is there a shape the decompiler has no way to say". */
/* the port is big-endian like the 68K, but these four bytes are not guaranteed
 * 4-aligned, so read them a byte at a time rather than trusting a cast. */
PORTCODE static uint LD32U_PORT(const byte *p)
{
    return ((uint)p[0] << 24) | ((uint)p[1] << 16) | ((uint)p[2] << 8) | (uint)p[3];
}

/* ---- FUN_00003716, HAND-WRITTEN FROM docs/audit/score_tick.asm -----------------
 *
 * It ticks each active player's score block. A6 IS SET BY `lea` AT EACH CALL SITE:
 *
 *     003720  lea (-0xfd8).w,A5      ; A5 = 0xFFFFF028
 *     003724  lea (-0x2000).w,A6     ; A6 = 0xFFFFE000, player 1's score block
 *     003728  btst.b #0,(A5)
 *     00372E  bsr  0x0000374a
 *     003730  clr.l (0x4,A6)
 *     003738  lea (-0x1f80).w,A6     ; A6 = 0xFFFFE080, player 2's
 *     003742  bsr  0x0000374a
 *
 * GHIDRA DROPS BOTH `lea`s. It folded A6 at the uses it could see -- `clr.l (0x4,A6)`
 * came out as `_DAT_ffffe004 = 0` and `_DAT_ffffe084 = 0`, so it KNEW the value -- and
 * then emitted `FUN_0000374a()` with no argument and no A6. A6 is a global in the port
 * (`_r_unaff_A6`), so the callee ran against whatever the last dispatch left: record 40
 * at 0xFFD400. The score adder then BCD-added into an object's flags word, which is why
 * 0xFFD400..03 read 0x99999999 against the arcade's 0x80000001.
 *
 * 210 instructions in the program load A6 (94 lea, 65 movea.l, 51 movea.w). This is the
 * same family as `exg A4,A6` and it is the larger half of it. */
PORTCODE uint port_score_tick(void)
{
    port_watch_hook(0x3716u);
    extern uint FUN_0000374a(void);
    extern byte DAT_fffff026, DAT_fffff028, DAT_fffff029;
    byte *base = (byte *)WRAM_BASE;
    byte *save = _r_unaff_A6;

    if ((DAT_fffff026 & 1u) != 0u)              /* 0x3716 btst #0 ; bne rts */
        return 0;
    if ((DAT_fffff028 & 1u) != 0u) {            /* 0x3728 btst #0,(A5) */
        _r_unaff_A6 = base + (0xFFE000u - 0xFFC000u);   /* 0x3724 lea (-0x2000).w,A6 */
        FUN_0000374a();
    }
    /* 0x3730 clr.l (0x4,A6) -- unconditional, and A6 is 0xFFFFE000 here whether or
     * not the call was made. */
    { byte *p = base + (0xFFE004u - 0xFFC000u); p[0]=p[1]=p[2]=p[3]=0; }

    if ((DAT_fffff029 & 1u) != 0u) {            /* 0x373C */
        _r_unaff_A6 = base + (0xFFE080u - 0xFFC000u);   /* 0x3738 lea (-0x1f80).w,A6 */
        FUN_0000374a();
    }
    { byte *p = base + (0xFFE084u - 0xFFC000u); p[0]=p[1]=p[2]=p[3]=0; }

    _r_unaff_A6 = save;      /* the 68K's A6 is caller-saved across this by convention
                              * here: every caller reloads it. Restoring is the safe
                              * reading and costs nothing. */
    return 0;
}

/* ---- THE 68000's PACKED-BCD ADD, DERIVED FROM srcref/jtcores ------------------
 *
 * Source: srcref/S32X_MiSTer/rtl/FX68K/fx68kAlu.sv, module `aluCorf` (lines 462-494),
 * which jtcores carries as its 68000. DERIVED, not copied -- the repo's rule for RTL.
 *
 * THE 68000 DOES NOT DO A DIGIT-WISE BCD ADD. It does a BINARY add and then applies a
 * decimal CORRECTION FACTOR, and the carry comes out of the correction:
 *
 *     lowC   = half-carry | (low nibble of the binary sum > 9)
 *     htemp  = binary sum + (lowC ? 6 : 0)                 9 bits
 *     highC  = binary carry | (htemp's high nibble > 9) | htemp's bit 8
 *     hNib   = htemp[8:4]  + (highC ? 6 : 0)               5 bits
 *     result = hNib[3:0] : htemp[3:0]
 *     carry  = hNib[4] | binary carry
 *
 * and `> 9` is tested as `nib[3] & (nib[2] | nib[1])`.
 *
 * MY FIRST VERSION ADJUSTED EACH DIGIT BY -10 AND CHAINED THE CARRY MYSELF. That is
 * right for valid packed BCD and wrong for anything else, so it raised carries the
 * 68000 does not -- which is why the saturation at 0x3786 kept firing and why Ghidra
 * marks that block unreachable. FULL-PORT.md 46-48. */
#define BCD_GT9(n)  ((((n) & 8u) != 0u) && (((n) & 6u) != 0u))

PORTCODE static uint port_abcd_byte(uint d, uint sb, uint *x)
{
    uint bin   = d + sb + *x;
    uint hc    = (((d & 0x0Fu) + (sb & 0x0Fu) + *x) > 0x0Fu);
    uint bc    = (bin > 0xFFu);
    uint b     = bin & 0xFFu;
    uint lowC  = hc || BCD_GT9(b & 0x0Fu);
    uint htemp = b + (lowC ? 6u : 0u);                       /* 9 bits */
    uint highC = bc || BCD_GT9((htemp >> 4) & 0x0Fu) || ((htemp >> 8) & 1u);
    uint hNib  = ((htemp >> 4) & 0x1Fu) + (highC ? 6u : 0u); /* 5 bits */
    *x = ((hNib >> 4) & 1u) | bc;
    return (uint)((((hNib & 0x0Fu) << 4) | (htemp & 0x0Fu)) & 0xFFu);
}

PORTCODE static uint port_bcd_add4(byte *dst, const byte *src)
{
    uint x = 0;                       /* the X flag, chained low byte to high */
    int i;
    for (i = 3; i >= 0; i--)          /* big-endian: [3] is the least significant */
        dst[i] = (byte)port_abcd_byte(dst[i], src[i], &x);
    return x;
}

PORTCODE uint port_bcd_score_add(int p_A0);

PORTCODE uint port_score_update(void)
{
    extern uint FUN_000037d0(uint *p_A0, int p_A2);
    byte *a6 = _r_unaff_A6;
    byte *score = a6 + 0;                  /* A0 after the adder: A6+0 */
    byte *hi    = (byte *)WRAM_BASE + (0xFFF010u - 0xFFC000u);   /* lea (-0xff0).w */
    uint a2 = LD32U_PORT(a6 + 8);

    port_bcd_score_add((int)(a6 + 4));     /* 0x3752: A6+0..3 += A6+4..7 */
    FUN_000037d0((uint *)score, (int)a2);  /* 0x3756, with A0 = A6+0 */

    /* 0x375E cmpm.l (A0)+,(A1)+ ; bhi -> skip. The compare is the HIGH SCORE minus
     * the score, so the promote happens when the score is >= the high score. */
    if (LD32U_PORT(hi) <= LD32U_PORT(score)) {
        uint k;
        for (k = 0; k < 4u; k++) hi[k] = score[k];      /* 0x3762 move.l -(A0),-(A1) */
        FUN_000037d0((uint *)hi, 0x4100d2);             /* 0x3766 / 0x376C */
    }
    port_bcd_score_add((int)(a6 + 0x10));  /* 0x3770 + the fallthrough into 0x3774 */
    return 0;
}

PORTCODE uint port_bcd_score_add(int p_A0)
{
    /* A0 pre-decrements from p_A0 over four bytes, A1 from A6+8 over four. */
    byte *dst = (byte *)p_A0 - 4;
    const byte *src = (const byte *)_r_unaff_A6 + 4;
    /* THE SATURATION AT 0x3786 IS DELIBERATELY NOT IMPLEMENTED.
     *
     * `bcc.b 0x378c ; move.l #-0x66666667,(A0)` writes 0x99999999, the packed-BCD
     * maximum, when the four `abcd` carry out -- and GHIDRA MARKS THAT BLOCK
     * "Removing unreachable block (ram,0x00003786)", i.e. its analysis says the carry
     * never happens on real data.
     *
     * Implementing it literally COST FOUR CELLS, measured 2026-10-02: record 40's
     * 0xFFD400..03 came out 0x99999999 against the arcade's 0x80000001, because these
     * operands are not always valid packed BCD and a single -10 adjust per digit
     * raises a carry the 68000 does not. Until the 68000's real `abcd` behaviour for
     * non-BCD operands is derived (srcref/jtcores), the honest model is the one that
     * matches the oracle: no saturation. */
    if (port_bcd_add4(dst, src)) {
        /* 0x3786: `move.l #-0x66666667,(A0)` = 0x99999999, the packed-BCD maximum.
         * KEPT BECAUSE IT MEASURES BETTER, NOT BECAUSE THE MODEL IS RIGHT:
         *   with it     15 wrong -- but record 40's 0xFFD400..03 reads 0x99999999
         *               against the arcade's 0x80000001
         *   without it  19 wrong -- sprite record 67 (0xFFFC31/32/34/35) goes wrong
         * Ghidra marks this block "Removing unreachable block", so on real data the
         * 68000's carry evidently does NOT fire here, and my adder raises one because
         * the operands are not always valid packed BCD. Both models are wrong; this
         * one is wrong in fewer cells. The real answer is the 68000's `abcd` carry
         * behaviour for non-BCD operands, from srcref/jtcores. FULL-PORT.md 47-48. */
        dst[0] = dst[1] = dst[2] = dst[3] = 0x99u;
    }
    return 0;
}

/* ---- walk_player_slots (0x0D5A6), HAND-WRITTEN FROM docs/audit/walk_player_slots.asm
 *
 * WHY. The 68K exchanges A4 and A6 around each collision call:
 *
 *     00D5A8  lea  (-0x4000).w,A4     ; A4 = 0xFFFFC000, the player record
 *     00D5B4  exg  A4,A6              ; SWAP
 *     00D5B6  bsr  0x0000d608         ; ... so A6 is the PLAYER for this call
 *     00D5B8  exg  A4,A6              ; swap back
 *     00D5BE  bsr  0x0000d608         ; and A4 is the player for this one
 *
 * Ghidra renders that pair as `FUN_0000d608(unaff_A6)`. It gets A4's VALUE right and
 * never models that A6 ITSELF CHANGED for the duration of the call -- the decompiler
 * has no way to say "this global is temporarily something else across a call". So
 * inside FUN_0000D608 the port read and wrote `_r_unaff_A6` = the OBJECT where the
 * arcade has the PLAYER, and the collision result landed on record 16's +0x4C
 * instead of the player's. Measured 2026-10-02: that is the rec 16/17 cluster, and
 * it is why those cells read "we wrote it; the arcade did not".
 *
 * `exg %a4,%fp` appears NINE times in the whole program -- eight of them here, one at
 * 0xD54C -- plus a single `exg %a0,%a1`. Ten sites; this is not a broad class.
 *
 * Third member of the hand-write class after zoom_scale_lookup (six outputs against a
 * two-register return) and FUN_0000E16A (`movem` to -(SP)). The test is the same:
 * not "is this wrong" but "is there a shape the decompiler has no way to say".
 * D3 is the chain's, so it lives in _port_reg[3] (FULL-PORT.md 30). */
/* ---- THE SPAWN-TABLE WALKER, HAND-WRITTEN (68K 0xD80E) --------------------------
 *
 * WHY GHIDRA CANNOT RENDER THIS. The 68K's control flow crosses a function boundary
 * in both directions:
 *
 *   d822: lea 0xffffd800,%fp       A6 = SLOT 48, the allocator's scan start
 *   d826: movew 0xfffff13a,%d0     the table cursor
 *   d82a: movew 0xfffff132,%d1     the camera's HIGH half (pixels)
 *   d830: cmpw %a0@(0,%d0:w),%d1   camera vs this entry's trigger X
 *   d834: bcs 0xd856                 camera behind it -> done
 *   d83c: bsr 0xd868               allocate + fill the record from the entry
 *   d840: bcc 0xd852                 no free slot -> leave the loop
 *   d842: moveal %a0@(8),%a1
 *   d846: jsr %a1@                 the entry's own init routine
 *   d84c: addiw #12,%d0            next entry
 *   d850: bra 0xd830               LOOP
 *   d856: movew %d0,0xfffff13a     store the cursor
 *
 * and 0xD868's exhausted path does `bcc 0xd856` -- a JUMP INTO THIS FUNCTION'S
 * EPILOGUE, whose `rts` then returns to 0xD840 inside the loop, which takes its own
 * `bcc` to the proper exit. Correct, and not expressible as a call graph.
 *
 * Ghidra's rendering came out as
 *     if ((bVar5) || (iVar4 = FUN_0000d868(...), !bVar5)) break;
 * where `!bVar5` is TRUE whenever bVar5 is false, so the loop ALWAYS breaks: the
 * cursor never advances and the entry's init routine is DEAD CODE. Measured
 * 2026-10-03 (FULL-PORT.md 66): the arcade spawns an object into slot 48 at frame 158
 * when the camera reaches trigger 0x10E0 and advances its cursor 0x0000 -> 0x000C.
 * The port did neither -- 33 bytes of slot 48 stayed zero.
 *
 * HOW SUCCESS IS DETECTED WITHOUT MODELLING THE CARRY FLAG. 0xD840 branches on the
 * carry 0xD868 leaves, and the port has no general mechanism for condition codes. So
 * this OBSERVES THE EFFECT instead of duplicating the decision: note which slot the
 * allocator would claim (the first of 14 from 0xFFD800 whose status bit 7 is clear --
 * that IS its scan, 0xD868 entry plus 0xD85C), call it, and if that slot is now
 * claimed the allocation happened. Nothing is re-decided; the claim is read back.
 */

#ifdef PORT_FFWATCH
/* MEASUREMENT HOOK (2026-10-04): watch C = object slot 48's word +0x30 becoming
 * non-zero. [176] bits 16+ = prev state; [177] = last tag; [178] = transitions;
 * [179] = 68K frame at the last one; [180] = tag before it; [181] = detecting tag;
 * [182] = A6 at this call; [183] = A6 before it; [184..191] = the last four
 * (tag, A6) pairs before the detection, oldest first. */
void port_watch_hook(unsigned int tag)
{
    static unsigned int hist[8];
    volatile byte *w = (volatile byte *)WRAM_BASE;
    const volatile byte *wu = (const volatile byte *)((unsigned int)WRAM_BASE | 0x20000000u);   /* UNCACHED alias: a DMA or slave write shows at once */
    unsigned int now = ((unsigned int)wu[0x1830] << 8) | wu[0x1831];
    unsigned int prev = port_watch_out[152];   /* own slot: the A/B blocks clobber [176] */
    if (now != 0u && prev == 0u) {
        unsigned int k;
        port_watch_out[178]++; port_watch_out[179] = ((unsigned int)w[0x3020] << 8) | w[0x3021];
        port_watch_out[180] = port_watch_out[177]; port_watch_out[183] = port_watch_out[182]; port_watch_out[181] = tag;
        for (k = 0; k < 8u; k++) port_watch_out[184 + k] = hist[k];
        /* ring of the last 8 transitions at port_watch_out[128..151]: prev tag, detecting tag, 68K frame */
        { unsigned int r = (port_watch_out[178] - 1u) & 7u;
          port_watch_out[128 + r * 3] = port_watch_out[177]; port_watch_out[129 + r * 3] = tag; port_watch_out[130 + r * 3] = port_watch_out[179];
          port_watch_out[156 + r] = port_watch_out[153]; }
    }
    port_watch_out[152] = now ? 1u : 0u;
    port_watch_out[177] = tag;
    port_watch_out[182] = (unsigned int)_r_unaff_A6;
    { unsigned int k; for (k = 0; k < 6u; k++) hist[k] = hist[k + 2]; hist[6] = tag; hist[7] = (unsigned int)_r_unaff_A6; }
}
#else
void port_watch_hook(unsigned int tag) { (void)tag; }
#endif

PORTCODE uint port_spawn_walk(void)
{
    port_watch_hook(0xd80eu);
    /* the driver does not include the generated headers, so `undefined4` is
     * spelled as what ghidra_types.h makes it: unsigned int. */
    extern uint FUN_0000d868(int p_A0, unsigned int p_D0, unsigned int p_D2);
    uint port_call68_at(unsigned int site, unsigned int a68);   /* defined below */
    /* THE CELLS ARE READ THROUGH WRAM_BASE, NOT BY SYMBOL, and that is not style.
     *
     * 0xFFF132 is one of the 41 cells the generated dat_syms.h redeclares as a PACKED
     * STRUCT behind a macro, because it is a 32-bit value at an address 2 mod 4 and a
     * plain SH-2 `mov.l` there is an address error. This driver does not include that
     * header, so `extern unsigned int _DAT_fffff132;` compiled to exactly the
     * misaligned load the macro exists to avoid -- and the camera read back as 0x40
     * instead of 0x10E0, so the walker compared garbage and never spawned. Byte-wise
     * off the base cannot have that problem and says what it is doing. */
#define W8(a)   (base[(a) - 0xFFC000u])
#define W16(a)  ((((uint)W8(a)) << 8) | (uint)W8((a) + 1u))
    const byte *rom = (const byte *)ROM_BASE;
    byte *base = (byte *)WRAM_BASE;
    byte *a6 = base + (0xFFD800u - 0xFFC000u);
    uint tbl, cur, cam;

    /* A0 = the round's trigger table: the LONG at 68K 0x1D32A + mode*4, byte-wise
     * because 0x1D32A is 2 mod 4 and an SH-2 32-bit read there is an address error. */
    {   const byte *q = rom + 0x1D32Au + (W8(0xFFF142u) << 2);
        tbl = (((uint)q[0] << 24) | ((uint)q[1] << 16)
               | ((uint)q[2] << 8) | (uint)q[3]) & 0x3FFFFu;
    }
    cur = W16(0xFFF13Au);
    cam = W16(0xFFF132u);              /* the 68K's `movew 0xfffff132` IS the high half */
    port_spawn_dbg[0] = tbl; port_spawn_dbg[1] = cur; port_spawn_dbg[2] = cam;

    for (;;) {
        const byte *e = rom + ((tbl + cur) & 0x3FFFFu);
        uint trig = ((uint)e[0] << 8) | (uint)e[1];
        byte *pick = 0;
        uint k;
        port_spawn_dbg[5]++;
        if (port_spawn_dbg[5] == 1u) port_spawn_dbg[3] = trig;
        if (cam < trig)
            break;                                  /* 0xD834 */
        for (k = 0; k < 14u; k++)
            if ((signed char)a6[k * 0x80u] >= 0) { pick = a6 + k * 0x80u; break; }
        _r_unaff_A6 = a6;                            /* 0xD822 */
        FUN_0000d868((int)(0xFFFFFFu & (tbl + cur)), cur, 0u);
        if (!pick || (signed char)pick[0] >= 0)
            break;                                   /* 0xD840: nothing was claimed */
        port_spawn_dbg[4]++;
        _r_unaff_A6 = pick;                          /* the init runs on the new slot */
        port_call68_at(8, ((uint)e[8] << 24) | ((uint)e[9] << 16)
                          | ((uint)e[10] << 8) | (uint)e[11]);
        cur = (uint)(unsigned short)(cur + 12u);             /* 0xD84C */
    }
    W8(0xFFF13Au) = (byte)(cur >> 8);                        /* 0xD856 */
    W8(0xFFF13Bu) = (byte)cur;
#undef W8
#undef W16
    return 0;
}

SEC_port_walk_player_slots uint port_walk_player_slots(void)
{
    extern uint FUN_0000d608(int p_A4);
    extern uint FUN_0000d65c(int p_A4, unsigned int p_D3);
    extern byte DAT_fffff028, DAT_fffff029;   /* placed by layout.ld */
    byte *base = (byte *)WRAM_BASE;
    byte *a4, *t;

    _port_reg[3] = 0;                                  /* 0xD5A6 moveq #0,D3 */

#define XCHG_A6()  do { t = a4; a4 = _r_unaff_A6; _r_unaff_A6 = t; } while (0)

    /* ---- player 1, 0xFFFFC000 (0xD5A8..0xD5BE) */
    a4 = base + (0xFFC000u - 0xFFC000u);
    if (DAT_fffff028 == 1u) {
        XCHG_A6(); FUN_0000d608((int)a4); XCHG_A6();
        FUN_0000d65c((int)a4, _port_reg[3]);
        FUN_0000d608((int)a4);
    }
    /* ---- player 2, 0xFFFFC400 (0xD5C0..0xD5D6) */
    a4 = base + (0xFFC400u - 0xFFC000u);
    if (DAT_fffff029 == 1u) {
        XCHG_A6(); FUN_0000d608((int)a4); XCHG_A6();
        FUN_0000d65c((int)a4, _port_reg[3]);
        FUN_0000d608((int)a4);
    }
    /* ---- 0xFFFFC080 (0xD5D8..0xD5EE): the d65c call is skipped when D3.b is set */
    a4 = base + (0xFFC080u - 0xFFC000u);
    if (*(short *)a4 != 0) {
        XCHG_A6(); FUN_0000d608((int)a4); XCHG_A6();
        if ((_port_reg[3] & 0xFFu) == 0u)
            FUN_0000d65c((int)a4, _port_reg[3]);
        FUN_0000d608((int)a4);
    }
    /* ---- 0xFFFFC480 (0xD5F0..0xD606). The last d608 is the FALLTHROUGH into
     * 0xD608, which is that function's own entry -- a tail call, not a branch. */
    a4 = base + (0xFFC480u - 0xFFC000u);
    if (*(short *)a4 != 0) {
        XCHG_A6(); FUN_0000d608((int)a4); XCHG_A6();
        if ((_port_reg[3] & 0xFFu) == 0u)
            FUN_0000d65c((int)a4, _port_reg[3]);
        FUN_0000d608((int)a4);
    }
#undef XCHG_A6
    return 0;
}

/* ---- FUN_0000E16A, HAND-WRITTEN FROM docs/audit/despawn_index.asm --------------
 *
 * WHAT IT DOES. It stamps a depth/priority index into byte +8 of all 24 object
 * records at 0xFFFFC800 (stride 0x80), counting up from 0x10, walking the records
 * linearly but DIVERTING down the +0x78 link chain wherever one exists and
 * remembering in A5 where it left the linear walk. Records whose word 0 is
 * non-negative also get hide_object_sprite() called on them.
 *
 * The arcade's own answer at the seed frame is the whole column, and it is the
 * oracle this was written against:
 *
 *     10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F 20 21 22 23 24 25 26 27
 *
 * WHY IT IS HAND-WRITTEN. The decompiler cannot express 0xE1C4/0xE1CE:
 *
 *     movem.w {D1w D0w},-(SP)   ;  save the two counters
 *     jsr     0x00003F04        ;  hide_object_sprite
 *     movem.w (SP)+,{D0 D1}     ;  restore them
 *
 * Ghidra flattened the moving stack pointer into fixed offsets from a single
 * `register0x0000003c` and got the arithmetic INCONSISTENT -- it wrote the counter
 * at SP-4 and read it back at SP-6, two different cells -- then decremented its
 * model of SP by 4 per iteration with no matching restore. Measured 2026-10-02 on
 * the rotated-frame rom: the port produced
 *
 *     10 11 12 66 CE FF FF FF ... FF
 *
 * three correct, two garbage from the phantom stack, then its own 0xFF
 * initialisation for the remaining nineteen -- 21 of that rom's 132 wrong cells,
 * the single largest cluster. This is the same class as `zoom_scale_lookup` and
 * `port_object_dispatch`: hand-write ONLY what the decompiler structurally cannot
 * express, transcribe from the disassembly, and archive the listing
 * (docs/design/PORT-ARCH.md 1).
 *
 * TWO 68K FORMS THAT NEEDED CARE, both derived from the listing and not guessed:
 *   * `movea.w A6w,A5` / `movea.w A5w,A6` / `movea.w (0x78,A6),A6` are 16-bit
 *     address-register moves. Here both operands are work-RAM pointers, so the
 *     port keeps real SH-2 pointers and the truncation simply does not happen --
 *     which is what W68/A68OF do for the generated code.
 *   * `tas D2b` sets bit 7 and sets Z from the OLD value, so the A5 save at 0xE1E0
 *     happens only on the FIRST diversion. */
SEC_port_despawn_index uint port_despawn_index(void)
{
    /* THE CURSOR IS A 68K ADDRESS, NOT A POINTER (2026-10-23). A6 here is `movea.w`-ed from
     * +0x78 and stepped by `lea (0x80,A6),A6`; both leave work RAM on the arcade whenever a
     * live head's +0x78 names a dead record: the chain's restore (a6 = a5) fires only on an
     * ACTIVE record with +0x78 == -1, so after a dead part the walk never comes back, runs
     * off slot 39 through the sprite list and the globals, wraps the 24-bit bus at 0xFFFFFF
     * into ROM, and stops at its 24th stamp. On the 68000 every write on that excursion lands
     * in ROM and does nothing; the first cut of this driver mapped the cursor straight into
     * the port's SDRAM and the same excursion stamped 0x24/0x25 into r60_line_for's code
     * (sw303g, tape frame 19010, tick 0x46A5: the master returned through a rewritten
     * `sts.l pr` into the name-table shadow and took an illegal-instruction loop; found with
     * the ares SDRAM write watch). Reads follow the 68K map -- work RAM, else program ROM,
     * else the work-RAM mirror of the 0xF00000 region -- and writes land in work RAM only. */
    uint a6 = 0xFFC800u, a5 = a6;
    uint d0 = 0;        /* records indexed  (0xE186 moveq #0,D0) */
    uint d1 = 0x10u;    /* the index to stamp (0xE18A move.b #0x10,D1b) */
    uint d2 = 0;        /* "diverted down a chain" flag (0xE188) */
    uint k;
    byte *base = (byte *)WRAM_BASE + (0xFFC800u - 0xFFC000u);
    short link;

    /* 0xE170-0xE180: every record's +8 <- 0xFF, 24 of them (dbf #0x17) */
    for (k = 0; k < 24u; k++) base[k * 0x80u + 8] = 0xFFu;

    for (;;) {
        byte *p;
        uint inram = (a6 - 0xFFC000u) < 0x4000u;
        if (inram)                      p = (byte *)WRAM_BASE + (a6 - 0xFFC000u);
        else if (a6 < 0x100000u)        p = (byte *)ROM_BASE + (a6 & 0x3FFFEu);      /* the 256 KB program, mirrored */
        else                            p = (byte *)WRAM_BASE + (a6 & 0x3FFEu);      /* 0xF00000-0xFFBFFF: the work-RAM mirror */
        /* 0xE18E tst.b (0x8,A6) ; bpl -> 0xE1B8 : already stamped, step over it */
        if ((signed char)p[8] >= 0) {
            a6 = (a6 + 0x80u) & 0xFFFFFFu;
            continue;
        }
        /* 0xE194 tst.w (0x0,A6) ; bpl -> 0xE1BE : the ACTIVE path, which calls out */
        if (*(short *)p >= 0) {
            if (inram) p[8] = (byte)d1;            /* 0xE1BE */
            d1 = (d1 + 1) & 0xFFu;                 /* 0xE1C2 */
            /* A6 IS HOW THE CALLEE IS TOLD WHICH RECORD. hide_object_sprite takes
             * no declared argument and reads the port's A6, same as every
             * dispatched handler; off work RAM it gets the read view and writes
             * nothing through it (hand/hide_object_sprite.c). D0/D1 are ours and
             * live in locals, which is exactly what the movem pair was protecting. */
            _r_unaff_A6 = p;
            hide_object_sprite();                  /* 0xE1C8 jsr 0x3F04 */
            d0 = (d0 + 1) & 0xFFu;                 /* 0xE1D2 */
            if (d0 >= 0x18u)                       /* 0xE1D4 bcs loop, else rts */
                return 0;
            continue;
        }
        if (inram) p[8] = (byte)d1;                /* 0xE19A */
        d0 = (d0 + 1) & 0xFFu;                     /* 0xE19E */
        if (d0 >= 0x18u)                           /* 0xE1A0 cmpi.b #0x18 ; bcc rts */
            return 0;
        d1 = (d1 + 1) & 0xFFu;                     /* 0xE1A6 */
        link = *(short *)(p + 0x78);
        if (link != -1) {                          /* 0xE1A8 ; bne -> 0xE1DC */
            if (d2 == 0)                           /* 0xE1DC tas D2b ; bne skips */
                a5 = a6;                           /* 0xE1E0 movea.w A6w,A5 */
            d2 = 0x80u;                            /* tas sets bit 7 */
            /* 0xE1E2 movea.w (0x78,A6),A6: a 16-bit value, SIGN-EXTENDED by the 68000
             * (0xC880 -> 0xFFFFC880, 0x0000 -> ROM), on a 24-bit bus. */
            a6 = (uint)(int)link & 0xFFFFFFu;
            continue;
        }
        if (d2 != 0) {                             /* 0xE1B0 tst.b D2b ; beq */
            d2 = 0;                                /* 0xE1B4 clr.b D2b */
            a6 = a5;                               /* 0xE1B6 movea.w A5w,A6 */
        }
        a6 = (a6 + 0x80u) & 0xFFFFFFu;             /* 0xE1B8 lea (0x80,A6),A6 */
    }
}

/* ---- zoom_scale_lookup, HAND-WRITTEN FROM THE 68K ---------------------------
 *
 * 25 instructions at 0x3CD4-0x3D12, transcribed from the disassembly kept at
 * docs/audit/zoom_scale_lookup.asm. Hand-written for the same reason
 * port_object_dispatch is: the decompiler structurally cannot express it.
 *
 * IT HAS SIX OUTPUTS -- A0, D0, D1, D3, D6, D7 -- and Ghidra's return storage holds
 * TWO (there is no `undefined` type at 12 bytes). Declared `A0:4,D1:4` it emits
 *
 *     return CONCAT44(p_A0 + 2, 0x40 - (*(byte *)(unaff_A6 + 0x4e) & 0x1f));
 *
 * which is CORRECT as far as it goes and silently drops D3, D6 and D7. D3 is the
 * one the sprite record needs: field +5 is `(char)p_D3 >> 1` and wants 0x12/0x16/
 * 0x18 for records 0/16/17 (FULL-PORT.md 29/30c).
 *
 * The 68K, annotated:
 *     lea  (0x20000).l,A2        A2 = a 32-byte-per-frame zoom table
 *     move.b (A0)+,D0 ; lsl.w #5 ; adda.w D0,A2    index it by the frame byte
 *     move.b (0x4e,A6),D0 ; andi.w #0x1f           D0 = the object's zoom level
 *     move.b (0,A2,D0),D6                          D6 = the table entry
 *     move.b (A0)+,D7 ; move.w D7,D3               D3 = the SECOND frame byte
 *     moveq #0x40,D1 ; sub.w D0,D1                 D1 = 0x40 - zoom   (the scale)
 *     add.w D7,D7 ; subq #1 ; mulu.w D1,D7         D7 = width * scale ...
 *     add.w D0,D0 ; add.w D0,D0 ; andi.w #0x3f     ... minus a rounding term ...
 *     sub.w D0,D7 ; addi.w #0x3f ; asr.w #6 ; addq #1   ... in 1/64ths, rounded up
 *
 * The RETURN keeps the shape the generated caller expects -- CONCAT44(A0+2, D1) --
 * so its two uses (the high half dereferenced as a pointer, the low half read as
 * the scale) are unchanged. The other four outputs go to the register file, which
 * is how a value survives a return at all (FULL-PORT.md 30). */
SEC_port_zoom_scale unsigned long long port_zoom_scale(int p_A0)
{
    const byte *rom = (const byte *)ROM_BASE;
    /* p_A0 IS A 68K ADDRESS, not an SH-2 pointer. It comes from
     * sprite_frame_lookup(), whose declared return made its body materialise as
     * `*(ushort *)(...) + 0x255e0` -- a 68K ROM address. Dereferencing it directly
     * read SH-2 address 1 and D3 stayed 0 with the write-back firing (measured
     * 2026-10-01: write-backs 5 -> 9, D6 0 -> 0x48, D3 still 0). Same region split
     * as P68. */
    const byte *a0  = rom + ((uint)p_A0 & 0x3FFFFu);
    volatile byte *a6 = (volatile byte *)_r_unaff_A6;
    uint d0, d1, d3, d6, d7;

    d0 = (uint)a0[0];                       /* move.b (A0)+,D0 */
    d3 = (uint)a0[1];                       /* the second byte -- D3 */
    d7 = d3;
    {   uint zoom = (uint)a6[0x4e] & 0x1fu; /* move.b (0x4e,A6) ; andi.w #0x1f */
        /* move.b (0,A2,D0w*1),D6b with A2 = 0x20000 + (frame << 5) */
        d6 = (uint)rom[((0x20000u + (d0 << 5)) + zoom) & 0x3FFFFu];
        d1 = 0x40u - zoom;                  /* moveq #0x40,D1 ; sub.w D0w,D1w */
        d7 = ((d7 * 2u) - 1u) & 0xFFFFu;    /* add.w D7,D7 ; subq.w #1,D7w */
        d7 = (d7 * d1) & 0xFFFFFFFFu;       /* mulu.w D1w,D7 */
        d7 = (d7 - (((zoom * 4u) & 0x3fu))) & 0xFFFFu;   /* the rounding term */
        d7 = (((d7 + 0x3fu) & 0xFFFFu) >> 6) + 1u;       /* asr.w #6 ; addq.w #1 */
        d0 = zoom;
    }
    /* PUBLISH WHAT IT COMPUTED, because "_port_reg[3] reads 0 after the frame" has
     * two causes: this function computes 0, or a LATER write-back clobbers it.
     * walk_player_slots legitimately sets D3 to 0 in the arcade too, so the order
     * matters and guessing it is what the counter is for. */
    /* ITS OWN GLOBALS, not port_l2m_out. Referencing that array from here put it
     * in .portcode and gcc refused: "port_l2m_out causes a section type conflict
     * with probe_sample in section .portcode". The driver copies these out. */
    port_zoom_dbg[0] = d3; port_zoom_dbg[1]++; port_zoom_dbg[2] = (uint)p_A0;
    /* ZOOM PROBE (2026-10-04): the eye's iris records carry pitch 0 and a 16-line
     * height where the arcade's carry 0x0E and 54 lines (rd27 f1192 vs arcade f1200,
     * same sprite data c866/f480). Log what this stub read: id 3 = d3<<16 | a0[0]<<8 |
     * zoom, id 4 = p_A0. port_watch_out[160..] ring, tools/port_arglog_read.py. */
#ifdef PORT_ZOOMLOG  /* floods the 16-slot ring: 2 entries per sprite per frame (rd38) */
    { extern volatile unsigned port_watch_out[192], port_fn_seq;
      unsigned _i_ = (port_watch_out[176]++) & 7u;
      port_watch_out[160 + _i_ * 2] = (3u << 24) | (port_fn_seq & 0xFFFFFFu); port_watch_out[161 + _i_ * 2] = (d3 << 16) | ((uint)a0[0] << 8) | d0;
      _i_ = (port_watch_out[176]++) & 7u;
      port_watch_out[160 + _i_ * 2] = (4u << 24) | (port_fn_seq & 0xFFFFFFu); port_watch_out[161 + _i_ * 2] = (uint)p_A0; }
#endif
    _port_reg[0] = d0; _port_reg[1] = d1; _port_reg[3] = d3;
    _port_reg[6] = d6; _port_reg[7] = d7; _port_reg[8] = (uint)(p_A0 + 2);
    _port_reg_n++;
    return ((unsigned long long)(uint)(p_A0 + 2) << 32) | (unsigned long long)d1;
}
