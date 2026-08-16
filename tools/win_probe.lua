-- LOOP 6 window/tail probe. Decodes the SH-2 DIAG block and the MD
-- shim's handler-span bytes. Needs `make TAILPROBE=1` for the MD tail
-- split + means; the SH-2 window terms read on any build.
--
--   mame 32x -cart rom/s16.32x -rompath ./mame -skip_gameinfo \
--       -video none -sound none -nothrottle -autoboot_script tools/win_probe.lua
--   PROBE_OUT=/tmp/probe.txt to redirect.
--
-- THE PROBES ARE NOT FREE: they add per-vint work to the overloaded
-- tail and shift V-gate outcomes, which MOVES THE SCOREBOARD (measured
-- demo 52.1 -> 54.6, demo2 20.9 -> 23.4). Diagnose with them; run every
-- gate on a probe-free build.
--
-- SH-2 DIAG base 0x06028000, slots from m_main.c diag_add():
--   0 copy_pages  2 apply_cram  5 blit+preempt  6 flipwait
--   7 blit skips  8 TOTAL in-window  9 k1 cycles  17 dreq_incomplete
--   19 CRAM writes performed  20 apply_cram groups skipped
-- MD bytes: B0E8 dreq push / B0EA palette scan / B0EC stream (max spans),
--   B0D0 sum of total spans, B0D4 sum of stream spans, B0F0 vints,
--   B0F4 packed (max total << 8 | that vint's window), B0FC gate skips.
--
-- MAME reference (build a16d97d, attract to 3600 frames):
--   window TOTAL 1.62ms/cycle = blit 1.03 + apply_cram 0.32 + rest
--     (blit is ALL master blit_half; both slave waits measured ~0)
--   MD handler max total 224 / window 18 / tail 206 of 262 lines
--     mean total 170, mean stream 70
--     tail split: dreq 52, palscan 85, stream 117
local frames = 0
local out = assert(io.open(os.getenv('PROBE_OUT') or '/tmp/win_probe.txt', 'w'))
emu.register_frame_done(function()
    frames = frames + 1
    if frames % 300 ~= 0 then return end
    local sh2 = manager.machine.devices[':sega32x:32x_master_sh2'].spaces['program']
    local md  = manager.machine.devices[':maincpu'].spaces['program']
    local function d(i) return sh2:read_u32(0x06028000 + i * 4) end
    local n = d(9)
    if n == 0 then return end
    local ms = 1.37e-3                      -- FRT tick -> ms
    local line = string.format('f=%d cycles=%d bitmap=%04X vints=%d',
        frames, n, md:read_u16(0xFFB9FE), md:read_u16(0xFFB0F0))
    for _, s in ipairs({{0, 'copy_pages'}, {2, 'apply_cram'},
                        {5, 'blit_preempt'}, {6, 'flipwait'}, {8, 'TOTALwin'}}) do
        line = line .. string.format(' %s=%.3fms', s[2], d(s[1]) / n * ms)
    end
    line = line .. string.format(
        ' dreq_inc=%d skips=%d cramwr/cyc=%.1f grpskip/cyc=%.1f',
        d(17), d(7), d(19) / n, d(20) / n)
    -- LOOP 9 (`make WINSPLIT=1` only): slot 5 split into the master's
    -- blit_half alone vs the post-blit waits it has always been bundled
    -- with. us/row is the number a DMAC channel-1 rewrite has to beat;
    -- blit_wait is the part it CANNOT touch. Zero on a normal build.
    if d(25) > 0 then
        line = line .. string.format(
            ' || blit_only=%.3fms blit_wait=%.3fms rows/cyc=%.1f us/row=%.2f',
            d(23) / n * ms, d(24) / n * ms, d(25) / n,
            d(23) / d(25) * ms * 1000)
    end
    -- LOOP 9 (`make ROWSTALE=1` only): master rows byte-identical to the
    -- same row one cycle ago. An FB write costs ~5x an SDRAM read on
    -- ares, so a dirty-row blit pays above roughly 25% here.
    if d(33) > 0 then
        line = line .. string.format(' || rowsame=%d/%d (%.1f%%)',
            d(32), d(33), 100.0 * d(32) / d(33))
    end
    -- LOOP 7c: where the FBCTL restore lands. MAME should read 0% here
    -- (its blit fits inside vblank and it latches immediately anyway);
    -- a nonzero rate on ares IS the black-frame strobe.
    line = line .. string.format(' restore_late=%d/%d worst=%.0fln'
        .. ' latch=%.0ftk/%d',
        d(26), d(28), d(27) / 46.0, d(29) / math.max(d(28), 1), d(30))
    -- MD handler span (frame = 262 lines); same metric state_health
    -- decodes from an ares savestate. TAILPROBE builds for the extras.
    local packed = md:read_u16(0xFFB0F4)
    local tot, win = packed >> 8, packed & 0xFF
    if tot < win then tot = tot + 256 end
    local v = math.max(md:read_u16(0xFFB0F0), 1)
    line = line .. string.format(
        ' || MD total=%d window=%d tail=%d gateskips=%d'
        .. ' [dreq=%d palscan=%d stream=%d] MEANtotal=%.1f MEANstream=%.1f',
        tot, win, tot - win, md:read_u16(0xFFB0FC),
        md:read_u8(0xFFB0E8), md:read_u8(0xFFB0EA), md:read_u8(0xFFB0EC),
        md:read_u32(0xFFB0D0) / v, md:read_u32(0xFFB0D4) / v)
    line = line .. string.format(' MEANdreq=%.1f MEANpalscan=%.1f',
        md:read_u32(0xFFB0D8) / v, md:read_u32(0xFFB0DC) / v)
    out:write(line .. '\n'); out:flush()
    if frames >= 3600 then manager.machine:exit() end
end)
