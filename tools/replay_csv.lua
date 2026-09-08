-- REPLAY an ares input csv (frame,button,value) in MAME *arcade*
-- altbeast — the frame-for-frame identity experiment: identical
-- inputs should produce an identical game-logic timeline (score,
-- deaths, spawns) if the port's premise holds. Also forces the
-- port's DIP configuration (DSW2 0xFD, DSW1 0xFF - md_main.c:3039)
-- so the machines match.
--
-- Env: RC_IN (csv), RC_FRAMES, RC_SNAPDIR, RC_SNAPS (csv frames).
local in_path = os.getenv('RC_IN') or 'discover/inputs/play_wolf2.csv'
local max_frames = tonumber(os.getenv('RC_FRAMES') or '12700')
local snapdir = os.getenv('RC_SNAPDIR') or '/tmp'
local snaps = {}
for s in string.gmatch(os.getenv('RC_SNAPS') or '5900,8000,12100', '[^,]+') do
    snaps[tonumber(s)] = true
end

local function find_field(names)
    for _, port in pairs(manager.machine.ioport.ports) do
        for fname, field in pairs(port.fields) do
            for _, want in ipairs(names) do
                if fname == want then return field end
            end
        end
    end
end
local MAP = {
    y     = find_field({'Coin 1'}),
    start = find_field({'1 Player Start', 'Start 1'}),
    right = find_field({'P1 Right'}), left = find_field({'P1 Left'}),
    up    = find_field({'P1 Up'}),    down = find_field({'P1 Down'}),
    a     = find_field({'P1 Button 1'}),   -- punch
    b     = find_field({'P1 Button 2'}),   -- kick
    c     = find_field({'P1 Button 3'}),   -- jump
}

-- force the port's dips: DSW2=0xFD, DSW1=0xFF (active-low raw bytes)
for _, port in pairs(manager.machine.ioport.ports) do
    local tag = port.device and port.device.tag or ''
    for fname, field in pairs(port.fields) do
        if field.is_toggle or field.settinglist then end
    end
end
for ptag, want in pairs({[':DSW1'] = 0xFF, [':DSW2'] = 0xFD,
                         [':DSWA'] = 0xFF, [':DSWB'] = 0xFD}) do
    local port = manager.machine.ioport.ports[ptag]
    if port then
        for _, field in pairs(port.fields) do
            local m = field.mask
            local shift = 0
            local mm = m
            while mm ~= 0 and (mm & 1) == 0 do shift = shift + 1; mm = mm >> 1 end
            field:set_value((want & m) >> shift)
        end
    end
end

local events = {}
local f = io.open(in_path, 'r')
for line in f:lines() do
    if not line:match('^%s*#') then
        local fr, name, val = line:match('^(%d+),(%w+),(%d+)')
        if fr and MAP[name] then
            local t = events[tonumber(fr)] or {}
            events[tonumber(fr)] = t
            t[#t + 1] = { MAP[name], tonumber(val) }
        end
    end
end
f:close()

local screen
for _, s in pairs(manager.machine.screens) do screen = s break end
local frames = 0
emu.register_frame_done(function()
    frames = frames + 1
    local evs = events[frames]
    if evs then
        for _, e in ipairs(evs) do e[1]:set_value(e[2]) end
    end
    if snaps[frames] and screen then
        screen:snapshot(string.format('%s/rc_%05d.png', snapdir, frames))
    end
    if frames >= max_frames then manager.machine:exit() end
end)
