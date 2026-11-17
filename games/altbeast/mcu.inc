/* ALTERED BEAST: THE i8751 MAILBOXES AND THE PER-TICK WORK-RAM RULES. #included by
 * sh_src/port_l2.c inside its per-frame service, where it was cut from, so the translation
 * unit (and the rom) is unchanged. A second title supplies its own games/<title>/mcu.inc:
 * every System 16B board has a different MCU, or none. Uses the includer's `w` (work RAM
 * as bytes), frontend_round and port_live_svc. */
    /* ---- THE i8751, ON THE SH-2 ----------------------------------------------------
     *
     * The arcade's MCU talks to the 68K through three work-RAM mailboxes (US MCU
     * 317-0078; tools/game_altbeast.py): 0xFFF0C0 screen-sync busy, 0xFFF0C2 the
     * coin/service byte the MCU POSTS (inverted, 0 = nothing pressed), 0xFFF0C4 the
     * sound command the 68K posts and the MCU CONSUMES back to 0xFF. md_main.c
     * replicates the MCU for the 68K build ("Stage B"); it writes MD RAM, which a
     * port running on the SH-2 never sees. So a port-alone build had NO MCU.
     *
     * MEASURED 2026-10-04, with the real dips: 0xFFF0C4 held a posted command 0xB6
     * that nothing consumed (the arcade capture holds 0xFF, idle); by tick 1200 the
     * unconsumed queue had spilled 0xB6 into 0xFFF0C0..C2 as well, so MCU_COINS read
     * 0xB6 -- bits 2 and 7 set -- and irq4_handler's `do {} while (DAT_fffff0c2 & 4)`
     * spun forever waiting for an MCU to clear a TEST press that never happened. The
     * master SH-2 hung at 0x0238FBF6 for 183 million iterations (ares --profile), the
     * tick counter stopped at 849, and every per-frame function with it.
     *
     * This is the MD shim's per-vint half, verbatim in behaviour: post idle inputs to
     * MCU_COINS every frame (md_main.c:6729 `MCU_COINS = svc`), consume a pending
     * sound command back to 0xFF (md_main.c:5920-5924 -- logged, no Z80 yet), and the
     * busy flag exactly as it keeps it. Live coins/service come later with the pads. */
    {
        volatile unsigned char *mcu = (volatile unsigned char *)WRAM_BASE + (0xFFF0C0u - 0xFFC000u);
        static unsigned char mcu_busy;
        if (!mcu_busy && mcu[0]) mcu_busy = 1;        /* 68K set the busy high byte */
        else if (mcu_busy)       mcu_busy = 0;        /* no TEXT_SYNC here: one frame */
        if (mcu[4] != 0xFFu && r60_comm_push(R60_COMM,mcu[4])) {
            port_play_dbg[3] = (port_play_dbg[3] & 0xFFFF0000u) | 0xB600u | mcu[4];
            { extern void speech_cmd(unsigned); speech_cmd(mcu[4]); }   /* the slave's PWM speech lane (r60_speech.c) */
            mcu[4] = 0xFFu;
        }
        mcu[2] = (port_live_svc & 0x100u) ? (unsigned char)(port_live_svc & 0xFFu) : 0x00u;   /* MCU_COINS: the MCU posts the service bits pressed (md_main.c old shim: MCU_COINS = svc) */
#ifdef PORT_GODMODE
        {   /* measurement only (Makefile PORTGODMODE): hold P1 lives at 3 while in play, the
             * arcade oracle's GD_GOD rule, so a tape runs a whole level without dying */
            volatile unsigned char *f028 = (volatile unsigned char *)WRAM_BASE + (0xFFF028u - 0xFFC000u);
            volatile unsigned char *e018 = (volatile unsigned char *)WRAM_BASE + (0xFFE018u - 0xFFC000u);
            if ((*f028 & 1u) && *e018 < 3u) *e018 = 3u;
        }
#endif
        if (frontend_round && (w[0x3028] & 1u) && w[0x3026] == 0u && w[0x314E] == 0u) {
            w[0x314E] = (byte)frontend_round;
            w[0x3142] = (byte)frontend_round;
        }
        /* THE MCU SEEDS THE CREDIT COUNT. MEASURED 2026-10-04 on the oracle (MAME
         * altbeast, DIPs 0x54/0x66): 0xFFF000 reads 0xFF through frame 6 and 7 from
         * frame 7 on, with NO 68K write to it from the reset vector onward (MAME
         * watchpoint armed before frame 1) -- the i8751 writes it. The arcade then
         * shows "CREDITS 7" and spawns the credit object; the port showed "INSERT
         * COIN" and lacked slot 6 at the eye scene. Once, at the first tick. */
        /* RETRACTED 2026-10-04 (rd71): the 7 was MAME's NVRAM, not the MCU. The S16B
         * work RAM is battery-backed and MAME saves it to nvram/altbeast/nvram; the
         * oracle measured above had seven credits from an earlier session. A fresh
         * nvram directory reads 0xFFF000 = 0 from frame 7 (cell_timeline, 2026-10-04),
         * and the fresh-NVRAM arcade's table scene draws no "CREDITS 7" line (text row
         * 26 is bare where rd69 typed one). The factory board boots with zero credits;
         * restoring a saved count is the BATTERY SAVE card's job (STATE.md). */
    }
