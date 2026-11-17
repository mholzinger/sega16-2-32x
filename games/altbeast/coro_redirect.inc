/* ALTERED BEAST: where the vblank handler's fake returns land. The arcade's IRQ rewrites its
 * own return address to restart the program at two places; the port starts a fresh game
 * thread at the same place. #included by sh_src/port_l2.c coro_entry(), where it was cut
 * from. Uses the includer's irq_redirect, coro_returned and the two stack pointers. */
    {   /* A REDIRECT STARTS THE FRESH THREAD WHERE THE rte WOULD HAVE LANDED. */
        uint _t = irq_redirect;
        irq_redirect = 0;
        if (_t == 0x1E4Eu) {
            /* 0x1E4E is six bytes above the sequencer and its whole content is
             * `move.b #4,0xFFF031`; falling into 0x1E54 then adds 4, so the step
             * lands on 8, the title. FUN_00001e54 begins AT 0x1E54, so the step
             * write has to be done here or the step just advances normally. */
            DAT_fffff031 = 4;
            FUN_00001e54(0, 0, 0, 0, 0);
            coro_returned++;
            for (;;) port_ctx_switch(&coro_game_sp, coro_drv_sp);
        }
        if (_t == 0x05BEu) {
            main_entry();
            coro_returned++;
            for (;;) port_ctx_switch(&coro_game_sp, coro_drv_sp);
        }
    }
