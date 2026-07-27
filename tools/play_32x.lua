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

    if frames == 1300 or frames == 2400 or frames == 3600 or frames == 5400 then
        for tag, screen in pairs(manager.machine.screens) do
            screen:snapshot(string.format('%s/ch_%04d.png',
                '/private/tmp/claude-501/-Users-mikeholzinger-src-sega16-2-32x/821761a1-b62c-404f-824a-6f25c3a157b9/scratchpad', frames))
            break
        end
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
    local line = string.format('f=%d ent=%d skips=%d hv=%04x gpc=%08x', frames, ent, skips, hv, gpc)
    if n > 0 then
        line = line .. string.format(' compose=%.2fms mskips=%d miss=%.1f', tot / n * 1.37e-3, mskips, miss / n)
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
