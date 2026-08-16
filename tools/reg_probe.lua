-- LOOP 7: compare the MD text-RAM mirror against the SH-2 text shadow for
-- the layer-reg + rowscroll block, the two words latch_layer_regs actually
-- uses for X. If they diverge, the transport (COMM or DREQ) is at fault;
-- if they agree, the drift is downstream in the latch/compose.
--   mame 32x -cart rom/s16.32x -rompath ./mame -skip_gameinfo \
--       -video none -sound none -nothrottle -autoboot_script tools/reg_probe.lua
local frames = 0
local out = assert(io.open(os.getenv('PROBE_OUT') or '/tmp/reg_probe.txt', 'w'))
emu.register_frame_done(function()
    frames = frames + 1
    if frames % 300 ~= 0 then return end
    local sh2 = manager.machine.devices[':sega32x:32x_master_sh2'].spaces['program']
    local md  = manager.machine.devices[':maincpu'].spaces['program']
    local function pair(w)
        return md:read_u16(0xFF8000 + w * 2), sh2:read_u16(0x06026000 + w * 2)
    end
    local line = string.format('f=%d', frames)
    for _, w in ipairs({0x740, 0x742, 0x748, 0x74A, 0x74C, 0x74E, 0x7C0, 0x7E0}) do
        local m, s = pair(w)
        line = line .. string.format(' %03X:%04X/%04X%s', w, m, s,
                                     (m == s) and '' or '!')
    end
    out:write(line .. '\n'); out:flush()
    if frames >= 1500 then manager.machine:exit() end
end)
