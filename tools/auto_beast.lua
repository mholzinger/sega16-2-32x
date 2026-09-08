-- AUTO-BEAST (2026-08-31, the wolf hunt): drive a level-1 playthrough
-- in MAME *arcade* altbeast — same 68K binary and logic timeline as
-- the port, ~3x realtime — to tune a transforming input recipe
-- cheaply, then export the identical timeline as an ares input csv.
--
--   mame altbeast -rompath ./mame -skip_gameinfo -video none -sound none \
--       -nothrottle -window -resolution 160x120 -keyboardprovider none \
--       -nomouse -nojoystick -bench 300 -autoboot_script tools/auto_beast.lua
--
-- Env: AB_PERIOD (attack cadence frames, default 10), AB_OUT (csv of
-- the emitted input timeline), AB_SNAPDIR (snapshot dir), AB_FRAMES.
local period = tonumber(os.getenv('AB_PERIOD') or '10')
local out_path = os.getenv('AB_OUT') or '/tmp/auto_beast.csv'
local snapdir = os.getenv('AB_SNAPDIR') or '/tmp'
local max_frames = tonumber(os.getenv('AB_FRAMES') or '13500')
local snaps = {}
for s in string.gmatch(os.getenv('AB_SNAPS') or '5400,8000,11000,13000', '[^,]+') do
    snaps[tonumber(s)] = true
end

local out = assert(io.open(out_path, 'w'))
out:write('# auto_beast recorded timeline (arcade field -> ares button)\n')

local function find_field(names)
    for _, port in pairs(manager.machine.ioport.ports) do
        for fname, field in pairs(port.fields) do
            for _, want in ipairs(names) do
                if fname == want then return field end
            end
        end
    end
end
local coin  = find_field({'Coin 1'})
local start = find_field({'1 Player Start', 'Start 1'})
local right = find_field({'P1 Right'})
local btn1  = find_field({'P1 Button 1'})   -- punch
local btn2  = find_field({'P1 Button 2'})   -- kick
local screen
for _, s in pairs(manager.machine.screens) do screen = s break end

local frames = 0
local function emit(f, name, v)
    out:write(string.format('%d,%s,%d\n', f, name, v))
end

emu.register_frame_done(function()
    frames = frames + 1
    local f = frames
    -- credits + start
    if f == 300 then coin:set_value(1); emit(f, 'y', 1) end
    if f == 310 then coin:set_value(0); emit(f, 'y', 0) end
    if f == 420 then start:set_value(1); emit(f, 'start', 1) end
    if f == 430 then start:set_value(0); emit(f, 'start', 0) end
    if f == 600 then right:set_value(1); emit(f, 'right', 1) end
    -- alternating attacks, period AB_PERIOD, 5-frame presses
    if f >= 660 then
        local ph = (f - 660) % (period * 2)
        if ph == 0 then btn1:set_value(1); emit(f, 'a', 1) end
        if ph == 5 then btn1:set_value(0); emit(f, 'a', 0) end
        if ph == period then btn2:set_value(1); emit(f, 'b', 1) end
        if ph == period + 5 then btn2:set_value(0); emit(f, 'b', 0) end
    end
    if snaps[f] and screen then
        screen:snapshot(string.format('%s/ab_%05d.png', snapdir, f))
    end
    if f >= max_frames then
        out:close()
        manager.machine:exit()
    end
end)
