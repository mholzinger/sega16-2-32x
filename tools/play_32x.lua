-- Coin+start the 32X build (proven pattern: two 40-frame coin presses,
-- then start), sample health counters, screenshot gameplay.
local frames = 0
local out = assert(io.open('/private/tmp/claude-501/-Users-mikeholzinger-src-sega16-2-32x/821761a1-b62c-404f-824a-6f25c3a157b9/scratchpad/coinhang.txt', 'w'))
local fields = {}
local last_ent, stall = -1, 0
local inited = false
local function init_fields()
    for tag, port in pairs(manager.machine.ioport.ports) do
        for fname, field in pairs(port.fields) do fields[fname] = field end
    end
    inited = true
end
local function set_input(name, val)
    local f = fields[name]
    if f then f:set_value(val) end
end
emu.register_frame_done(function()
    if not inited then init_fields() end
    frames = frames + 1
    if frames == 600 or frames == 800 then set_input('P1 Start', 1); set_input('P1 A', 1) end
    if frames == 640 or frames == 840 then set_input('P1 Start', 0); set_input('P1 A', 0) end
    if frames == 1000 then set_input('P1 Start', 1) end
    if frames == 1040 then set_input('P1 Start', 0) end
    -- walk right + punch during gameplay to exercise combat paths
    if frames == 1800 then set_input('P1 Right', 1) end
    if frames == 2400 then set_input('P1 A', 1) end
    if frames == 2410 then set_input('P1 A', 0) end
    if frames == 2500 then set_input('P1 A', 1) end
    if frames == 2510 then set_input('P1 A', 0) end
    if frames == 3000 then set_input('P1 Right', 0) end

    if frames == 1300 or frames == 1400 or frames == 1500 or frames == 1600 or frames == 1700 or frames == 2400 or frames == 3600 or frames == 5400 then
        for tag, screen in pairs(manager.machine.screens) do
            screen:snapshot(string.format('%s/ch_%04d.png',
                '/private/tmp/claude-501/-Users-mikeholzinger-src-sega16-2-32x/821761a1-b62c-404f-824a-6f25c3a157b9/scratchpad', frames))
            break
        end
    end
    if frames == 1500 or frames == 1700 then
        -- decode the sprite-list snapshot (SPR_SNAP @ 0x06027400)
        local sh2s = manager.machine.devices[':sega32x:32x_master_sh2'].spaces['program']
        local f = assert(io.open(string.format(
            '/private/tmp/claude-501/-Users-mikeholzinger-src-sega16-2-32x/821761a1-b62c-404f-824a-6f25c3a157b9/scratchpad/spr_%04d.txt', frames), 'w'))
        for i = 0, 63 do
            local b = 0x06027400 + i * 16
            local d0 = sh2s:read_u16(b + 0)
            local d1 = sh2s:read_u16(b + 2)
            local d2 = sh2s:read_u16(b + 4)
            local d3 = sh2s:read_u16(b + 6)
            local d4 = sh2s:read_u16(b + 8)
            local d5 = sh2s:read_u16(b + 10)
            if (d2 & 0x8000) ~= 0 then f:write(string.format('%02d END\n', i)) break end
            f:write(string.format(
                '%02d top=%3d bot=%3d x=%3d hide=%d flip=%d pitch=%4d addr=%04x bank=%2d color=%02x vz=%2d hz=%2d\n',
                i, d0 & 0xFF, d0 >> 8, d1 & 0x1FF, (d2 >> 14) & 1, (d2 >> 8) & 1,
                (d2 & 0xFF) < 128 and (d2 & 0xFF) or (d2 & 0xFF) - 256,
                d3, (d4 >> 8) & 0xF, d4 & 0x3F, (d5 >> 5) & 0x1F, d5 & 0x1F))
        end
        f:close()
    end
    if frames % 60 ~= 0 then return end
    local cpu = manager.machine.devices[':maincpu']
    local mem = cpu.spaces['program']
    local ent = mem:read_u16(0xFFB0F0)
    local skips = mem:read_u16(0xFFB0FC)
    local hv = mem:read_u16(0xFFB0FE)
    local gpc = mem:read_u32(0xFFB0F8)
    -- window timing from the SH-2 profiler
    local sh2 = manager.machine.devices[':sega32x:32x_master_sh2'].spaces['program']
    local n = sh2:read_u32(0x06027000 + 9 * 4)
    local tot = sh2:read_u32(0x06027000 + 8 * 4)
    local mskips = sh2:read_u32(0x06027000 + 7 * 4)
    local miss = sh2:read_u32(0x06027000 + 14 * 4)
    local swait = sh2:read_u32(0x06027000 + 4 * 4)
    local bdrain = sh2:read_u32(0x06027000 + 13 * 4)
    local line = string.format('f=%d ent=%d skips=%d hv=%04x gpc=%08x', frames, ent, skips, hv, gpc)
    if n > 0 then
        line = line .. string.format(' compose=%.2fms mskips=%d miss=%.1f swait=%.2fms bdrain=%d', tot / n * 1.37e-3, mskips, miss / n, swait / n * 1.37e-3, bdrain)
    end
    out:write(line .. '\n')
    out:flush()
    if ent == last_ent then
        stall = stall + 1
        if stall >= 4 then
            out:write('=== STALLED ===\n')
            out:flush()
            manager.machine:exit()
        end
    else
        stall = 0
    end
    last_ent = ent
    if frames >= 5460 then manager.machine:exit() end
end)
