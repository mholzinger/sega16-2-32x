-- FLICKFUSE A/B GATE (MAME, our rom). play_32x.lua's coin/start
-- cadence, then EVERY frame of the round-1 Zeus apparition window is
-- snapshotted so the blink/stipple behaviour can be measured offline
-- frame-by-frame. MAME is colour-blind on the canonical bundle; this
-- gate measures presence STRUCTURE (consecutive-frame diffs), never
-- colour.  Env: FG_SNAP = output dir.  Range: frames 1080..1400.
local frames = 0
local fields = {}
local inited = false
local snapdir = os.getenv('FG_SNAP') or '/tmp'
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
    if frames >= 1080 and frames <= 1400 then
        for tag, screen in pairs(manager.machine.screens) do
            screen:snapshot(string.format('%s/f_%04d.png', snapdir, frames))
            break
        end
    end
    if frames > 1400 then manager.machine:exit() end
end)
