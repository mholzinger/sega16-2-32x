-- ORACLE RIG: run the real arcade game with the same coin/start cadence
-- as tools/play_32x.lua and screenshot the same frame numbers, so port
-- output can be diffed against ground truth moment-for-moment.
local frames = 0
local fields = {}
local inited = false
local outdir = '/private/tmp/claude-501/-Users-mikeholzinger-src-sega16-2-32x/821761a1-b62c-404f-824a-6f25c3a157b9/scratchpad'
local shots = { [1300]=1, [1400]=1, [1500]=1, [1600]=1, [1700]=1,
                [2400]=1, [3600]=1, [5400]=1 }
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
    if frames == 600 or frames == 800 then set_input('Coin 1', 1) end
    if frames == 640 or frames == 840 then set_input('Coin 1', 0) end
    if frames == 1000 then set_input('1 Player Start', 1) end
    if frames == 1040 then set_input('1 Player Start', 0) end
    if frames == 1800 then set_input('P1 Right', 1) end
    if frames == 2400 then set_input('P1 Button 1', 1) end
    if frames == 2410 then set_input('P1 Button 1', 0) end
    if frames == 2500 then set_input('P1 Button 1', 1) end
    if frames == 2510 then set_input('P1 Button 1', 0) end
    if frames == 3000 then set_input('P1 Right', 0) end
    if shots[frames] then
        for tag, screen in pairs(manager.machine.screens) do
            screen:snapshot(string.format('%s/or_%04d.png', outdir, frames))
            break
        end
    end
    if frames >= 5460 then manager.machine:exit() end
end)
