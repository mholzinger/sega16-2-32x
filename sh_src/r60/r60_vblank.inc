/* r60_vblank.inc -- the frame clock: the vertical-interrupt body.
 * Part of r60_main.c: included there, in this order, so the compiler sees one unit. */
/* THE FRAME CLOCK. REBUILD section 2's handler shape: tick, commit, out. */
/* The phase counters outgrow region 1. Keep the probe's frame clock in SDRAM
 * too, using region 2's spare room rather than changing a measured phase to ROM. */
__attribute__((noinline)) static int frontend_vblank_pause(void)
{
    if (!frontend_paused) return 0;
    if (MARS_SYS_COMM14 == 0xB007u) MARS_SYS_COMM14 = 0xB008u;
    pp_due = 0; r60_frame++;
    return 1;
}
#if defined(R60_RIGSPLIT) && defined(PORT_HOT2)
__attribute__((section(".ramtext2")))
#else
R60CODE
#endif
#if defined(R60_RIGSPLIT) && R60_RIGSPLIT >= 3 && !R60_EVENT_LIGHT
static __attribute__((noinline)) void r60_vbi_body(void)
#else
void visr_vbi(void)
#endif
{
    R60_DIAG[0]++;
    if (frontend_vblank_pause()) return;
    if (R60_FM_IS_UP) R60_DIAG2[0]++; else R60_DIAG2[1]++;   /* V-ISR entries by FM state */
    /* ANSWER THE 68K'S BOOT HOLD. md_main.c:7738-7754: the shim posts COMM14=0xB007
     * and then SPINS until the master answers 0xB008 ("hold the game until the
     * master's V-ISR is armed"), 6,000,000 iterations deep -- about 1,900 frames at
     * 7.67 MHz. MEASURED on the skeleton before this line: the 68K's own vint counter
     * 0xFFB0F0 read 2 at frame 700 and md_consume's body ran ONCE for packet A in 700
     * frames, so no MD-plane packet could ever be taken however well it was formed.
     * ANSWER ON SEEING 0xB007, NOT ON THE FIRST ENTRY: the first cut posted 0xB008
     * from the V-ISR's first vint, which lands BEFORE the shim posts 0xB007 -- the
     * shim then overwrote it and waited for an answer that had already been given.
     * COMM14 read 0xB007 for the whole run and the 68K vint count stayed at 2. */
    if (MARS_SYS_COMM14 == 0xB007u) MARS_SYS_COMM14 = 0xB008u;
    R60_DIAG[6] = MARS_SYS_COMM14;       /* the 68K's beacon, as the master sees it */
    /* CRAM COMMIT (REBUILD section 2 step 3), and leaving it out is why the first board
     * launch was BLACK while ares counted 2,427 sprites drawn: the framebuffer held
     * correct 8-bit indices and the 32X palette at 0x20004200 was never written, so every
     * index resolved to colour 0. The diag counters cannot see that; only the board can.
     *
     * Sixteen banks of sixteen, bank b carrying sprite colour-set b, straight from the
     * port's own PAL_SH shadow. S16 packs a channel as four low bits plus one high bit
     * (the same unpack mdp_quant does for the MD), and 32X CRAM is BGR555, so each
     * channel is (nibble << 1) | highbit -- five bits, no quantisation, which is the
     * point of drawing on the 32X side instead of the MD's three. */
    /* THE CRAM COMMIT IS OWED, NOT DONE HERE. It needs TWO things at once that the
     * V-ISR cannot both guarantee: FM=1, because at FM=0 the 68K owns the VDP side and
     * the master's writes are dropped (measured on this rom: 11,297 of 11,513 SH-2
     * framebuffer writes dropped at FM=0); and VBLANK, because 32X CRAM writes need PEN
     * on real hardware. The V-ISR runs at the vint edge, which under the new beat is
     * often INSIDE the 68K's span (194 of 687 vints measured), so a commit here is
     * dropped about a quarter of the time -- which is what the board showed: the right
     * shapes in the right places, speckled, because most of the palette was last
     * frame's. The main loop does it as soon as FM is back up, if still inside vblank.
     *
     * (ares shows none of this: its palette was correct with the commit in the ISR.
     * The rig is the instrument here, as it is for PEN generally.) */
    /* COMMIT AT THE VINT EDGE WHEN FM IS UP, AND NOT AT ALL WHEN IT IS NOT. The edge is
     * inside vblank by construction, which is the PEN window CRAM needs; FM=1 is the
     * other half, because at FM=0 the 68K owns the VDP side. Deferring the commit to
     * the main loop "as soon as both hold" was tried and landed 323 of 687 -- the span
     * is usually still open when vblank runs out. Under GOVERNOR=2 the two states
     * alternate with the beat, so skipping the FM=0 vints costs nothing: the commit
     * lands once per PRESENTED frame, which is the rate the palette changes at. */
    /* GATED ON THE SLAVE BEING IDLE, NOT ON FM. Both are master accesses to the VDP
     * region (32X CRAM is 0x20004200-43FF), and the core has no in-flight guard on the
     * single SH-side VDP path (IF.sv:974-983): an access from one SH-2 re-latches over
     * the other's pending one and that one is LOST. Measured on the board: with the
     * commit running while the slave drew, the sprites came out correctly shaped and
     * almost entirely WHITE (1,750 white px); with every master VDP access confined to
     * the slave-idle window, white fell to 1. After the flip fix a vblank entered with
     * FM up IS mid-pass, so the old FM gate no longer means "the slave is idle".
     * Staying in the V-ISR keeps the commit inside vblank, which is what PEN wants;
     * the frames it skips are counted and cost at most a stale palette. */
    /* BOTH CONDITIONS, AND THAT IS NOT A BELT AND BRACES. "The slave is idle"
     * (DONE == GO) is ALSO true while the 68K's span is open -- the span OPENS on that
     * very condition -- so gating on it alone put the commit at FM=0, where the write
     * is dropped: measured, CRAM committed 695 times and the ares display went back to
     * 23 rows, the border alone. FM up says the master owns the VDP; DONE == GO says
     * the slave is not mid-pass; the renderer needs both before it may touch the
     * region at all. */
    /* THE COMMIT IS NOT HERE ANY MORE. It needs three things at once -- FM up (the
     * master owns the VDP), the slave idle (the core has no in-flight guard on the one
     * SH-side VDP path, IF.sv:974-983), and PEN -- and a vint edge almost never lands
     * where the first two hold: gated on both, it fired 5 times in 1,091 vints and the
     * display went back to the border alone. It runs in the slave-idle window instead,
     * which is outside vblank, so it waits on PEN itself. */
    /* ONE GAME FRAME PER VINT, OWED, NOT CAPPED (sw185). This was `pp_due = 1`, so a master
     * pass longer than a vint lost a game frame outright: sw184 had drained 1522 ticks by
     * vint 1544 (22 lost, sw166 7), most of them under the fat direct builds, and the game
     * ran that much behind the arcade's clock. The arcade's vint never misses; the loop
     * now drains what it owes, two ticks back to back after a long pass. The cap of 8 is
     * a stall guard only; what it drops is counted. */
    if (pp_due < 8u) pp_due++; else r60_census[4]++;
    r60_frame++;
    R60_TVINT = r60_frt();
    R60_EVPUT(1);
#ifdef PORT_RIGCLK
    {   /* PORTRIGCLK=1 (2026-11-03): THE WALL CLOCK PROBE. No window, no x5: every vint latch
         * the master's own vint count and the game's frame counter RAW; r60_bars_paint draws
         * each as 16 binary blocks. Two rig screenshots at known wall times give vints/s (is
         * this handler an honest 60?) and game ticks/s (the pace), nothing in between to
         * saturate. tools/rig_clock.py reads the strip. */
        extern volatile unsigned short port_gf_count;
        R60_BARA = r60_frame; R60_BARB = port_gf_count; }
#endif
    /* THE BARS' WINDOW: every 64 vints, latch the two rates. 64 vints is a flat second
     * and a bit, and x5 puts 64 presented frames at the full 320-pixel width, so the bar
     * reads directly as "how close to one presented frame per vint". */
    if ((r60_frame & 63u) == 0u) {
        {   /* BAR A: how often the 68K answered, over the last 64 vints */
            uint16_t po = (uint16_t)R60_DIAG[14], bk = (uint16_t)R60_DIAG[15];
            uint16_t dp = (uint16_t)(po - R60_WPOST), db = (uint16_t)(bk - R60_WBACK);
            R60_WPOST = po; R60_WBACK = bk;
            R60_WA = (dp + db) ? (uint16_t)(32u * ((8u * dp) / (dp + db))) : 0u;
        }
#if defined(PORT_RIGCLK)
        /* PORTRIGCLK: the bars are latched every vint above, not here */
#elif defined(PORT_RIG)
        {   /* PORTRIG=1 (2026-11-02): bar A = GAME TICKS per 64 vints x5 (port_gf_count, the
             * port's own tick counter: 60 ticks = 300 px, so rig_bars' "ticks/second" reads
             * the game's speed, 100 - the IRQ4 miss rate), bar B = presented frames x5. Mike
             * timed the rig ~10% under the arcade core; the old PORTRIG painter was deleted
             * with the renderer, so the default bars read the PICTURE rate only. */
            extern volatile unsigned short port_gf_count;
            uint16_t f = (uint16_t)R60_DIAG[1], k = port_gf_count;
        R60_BARA = (uint16_t)((uint16_t)(k - R60_BARK) * 5u);
        /* bar B is painted by r60_bars_paint (cart code) from the event ring: .ramtext is full */
        (void)f; R60_BARF = f; R60_BARK = k; }
#else
        {   uint16_t f = (uint16_t)R60_DIAG[1], k = (uint16_t)R60_DIAG[15];
        R60_BARA = (uint16_t)((uint16_t)(f - R60_BARF) * 5u);
        R60_BARB = (uint16_t)((uint16_t)(k - R60_BARK) * 5u);
        R60_BARF = f; R60_BARK = k; }
#endif
    }
    /* FLIP COMMIT, AND THE WHOLE POINT IS THE WORD *FINISHED*. REBUILD section 2 step 2:
     * this handler IS the vblank, so the commit is in the window by construction and needs
     * no V read.
     *
     * THE FIRST CUT FLIPPED EVERY SECOND VINT UNCONDITIONALLY and drew a PREFIX of the
     * sprite list: measured at f700, the port published 17 live records spanning sx
     * -13..311, and the framebuffer held records 0..7 only -- x 0..148 of 320, rows
     * 114..183 matching those eight records exactly, with record 8 (top=79) absent. The
     * art addressing was right; the handshake was missing. MARS_OVERWRITE_IMG follows the
     * WRITE-side bank, so a flip landing mid-list moves the slave's target out from under
     * it and the next pass's clear wipes what it drew. A bank is presented when the slave
     * says it is done with it, never on a timer.
     *
     * THE EDGE GUARD copied from the old renderer read `*(volatile uint16_t *)0xC00008`
     * and FLIPPED ZERO TIMES in 686 vints: that is the MD VDP's H/V counter, a 68K BUS
     * address the SH-2 cannot read. The edge-guard LAW (a late entry declines rather than
     * tears, LOOP24 Z2) needs the SH-2's own vblank evidence; P1 does not have one yet.
     * Noted, not faked. */
    if (R60_DONE != R60_GO || R60_GO == R60_SHOWN || R60_PUB != R60_GO) {
        R60_DIAG[8]++;                   /* DECLINED: the slave is still drawing, or the
                                          * plane packet for this frame is not out yet.
                                          * FM MUST NOT DROP HERE: the slave's writes are
                                          * dropped at FM=0 (measured 11,297 of 11,513 on
                                          * this rom), so a drop mid-draw corrupts the
                                          * frame it is trying to present. */
        return;
    }
    if ((uint16_t)(r60_frame - r60_last_flip) < R60_GOVERNOR) {
        R60_DIAG[9]++;                   /* held by the bring-up governor */
        return;
    }
    /* NEVER REQUEST A FLIP WHILE ONE IS STILL PENDING. A second request on top of an
     * unapplied one is IGNORED -- measured as 78 of 297 passes whose wait for FS timed
     * out, a count that did not move when the bound went from 2,000 ticks to a whole
     * frame, so they were never late applies. The slave then started against the bank
     * still on screen and the old request landed mid-pass, splitting one frame across
     * both banks: 3,563 image pixels in one and the 640 bar pixels in the other.
     *
     * PENDING IS TRACKED HERE, NOT READ FROM FS. Reading FS in the V-ISR to decide
     * cannot work and was tried: at the vblank edge the apply has not happened yet, so
     * FS never matches and the renderer deadlocks at one flip in 685 vints. The main
     * loop, which samples later, sees the match 231 times in 271 and is what clears
     * this. Declining is the edge-guard law in its own words: a drop beats a tear. */
    /* TWO CONDITIONS, AND BOTH ARE DECLINES, NOT WAITS -- REBUILD section 2 step 2:
     * the V-ISR never waits, it drops, because a drop beats a tear (LOOP24 Z2).
     *
     *   1. A FLIP IS STILL PENDING. A second request on top of an unapplied one is
     *      IGNORED, measured as 78 of 297 passes whose wait for FS timed out at ANY
     *      bound -- the slave then started against the bank still on screen and the old
     *      request landed mid-pass. Pending is tracked here rather than read from FS,
     *      because reading FS in the ISR to decide cannot work and was tried: at the
     *      vblank edge the apply has not happened yet, so FS never matches and the
     *      renderer deadlocks at one flip in 685 vints. The main loop, sampling later,
     *      sees it and clears this.
     *   2. WE ARE NOT INSIDE VBLANK. FBCTL bit 15 is the vblank flag. An FS write
     *      outside vblank is DEFERRED to the next one, and a deferred request applies
     *      after the slave has begun its next pass -- the write-side bank then switches
     *      under it. ares shows this is already true here (--trace-flip: 217 of 220
     *      writes inside vblank, 2 deferred, every write followed by its flip at the
     *      same vcounter), so this guard costs ares nothing and is for the board. */
    /* THE V-ISR NO LONGER FLIPS, AND THAT IS THE BUG THIS FIXES.
     *
     * An SH-2 write to MARS_VDP_FBCTL is DROPPED at FM=0: the SH-side VDP access is
     * only latched with FM (MiSTer IF.sv:974), and ares returns before its own trace
     * hook (io-internal.cpp:344-349), so a dropped write leaves no trace row either.
     * The master opens the 68K's FM=0 span on EXACTLY the condition the V-ISR needed in
     * order to flip -- DONE==GO && PUB==GO -- and it does so EARLIER in the vint. So
     * every flip request since the beat went in at FM=0 and was thrown away.
     *
     * THE EVIDENCE WAS IN MY OWN FIRST TRACE AND I WALKED PAST IT: --trace-flip logs
     * 220 SH-2 writes and the LAST IS AT FRAME 447, none in the remaining 650 frames,
     * which I read as the file ending. Framebuffer half 1 has not changed since: 0
     * bytes different across f1097-f1100 while half 0 alternates by 5,276, and two
     * consecutive screenshots are byte-identical. R60_DIAG2[12] -- "gave up waiting for
     * FS" -- counted 247 of them and I read it as a core quirk.
     *
     * The request moves to the span-open site, where FM is still up. It is then OUTSIDE
     * vblank, so the hardware defers it to the next vblank start (VDP.sv:400,
     * `if (VBLK || MODE==0) FS <= FBCR.FS`) -- in time for the 68K's vint, its post, FM
     * back up, and the next GO. This also retires the "FS matched 231 of 271" and
     * "gating on the match deadlocked" censuses recorded in this file: both were this.
     *
     * (The decline counters DIAG2[14]/[15] stay defined and now read zero, which is the
     * cheapest standing check that the flip has not crept back in here.) */
}

#if defined(R60_RIGSPLIT) && R60_RIGSPLIT >= 3 && !R60_EVENT_LIGHT
__attribute__((section(".ramtext2"), noinline))
void visr_vbi(void)
{
    uint16_t t0 = r60_frt(), dt;
    r60_vbi_body();
    dt = (uint16_t)(r60_frt() - t0);
    RSX[65] += dt; RSX[66]++;
    if (dt > RSX[67]) RSX[67] = dt;
}
#endif
