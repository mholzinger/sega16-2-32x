-- Coin+start the 32X build, then catch the 68K at the rise-from-grave freeze.
local frames = 0
local out = assert(io.open('/private/tmp/claude-501/-Users-mikeholzinger-src-sega16-2-32x/821761a1-b62c-404f-824a-6f25c3a157b9/scratchpad/coinhang.txt', 'w'))
local fields = {}
local last_ent, stall = -1, 0

local function set_input(name, val)
    local f = fields[name]
    if f then f:set_value(val) end
end

local inited = false
local function init_fields()
    for tag, port in pairs(manager.machine.ioport.ports) do
        for fname, field in pairs(port.fields) do
            fields[fname] = field
            out:write(string.format('field: %s @ %s\n', fname, tag))
        end
    end
    out:flush()
    inited = true
end

emu.register_frame_done(function()
    if not inited then init_fields() end
    frames = frames + 1
    -- coin: START+A held (shim maps that to coin); then START to begin
    if frames == 600 then set_input('P1 Start', 1); set_input('P1 A', 1) end
    if frames == 660 then set_input('P1 Start', 0); set_input('P1 A', 0) end
    if frames == 780 then set_input('P1 Start', 1) end
    if frames == 840 then set_input('P1 Start', 0) end

    if frames == 900 or frames == 1500 or frames == 2400 or frames == 3600 then
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
    local pc = cpu.state['PC'].value
    local sr = cpu.state['SR'].value
    local busy = mem:read_u8(0xFFF0C0)
    local gpc = mem:read_u32(0xFFB0F8)
    out:write(string.format('f=%d ent=%d pc=%08x sr=%04x busy=%02x gpc=%08x\n', frames, ent, pc, sr, busy, gpc))
    out:flush()
    if ent == last_ent then
        stall = stall + 1
        if stall >= 4 then
            out:write('=== STALLED ===\n')
            for i = 1, 12 do
                out:write(string.format('sample pc=%08x sr=%04x\n',
                    cpu.state['PC'].value, cpu.state['SR'].value))
            end
            local mpc = manager.machine.devices[':sega32x:32x_master_sh2'].state['PC'].value
            local spc = manager.machine.devices[':sega32x:32x_slave_sh2'].state['PC'].value
            out:write(string.format('mpc=%08x spc=%08x\n', mpc, spc))
            out:flush()
            manager.machine:exit()
        end
    else
        stall = 0
    end
    last_ent = ent
    if frames >= 7200 then manager.machine:exit() end
end)
