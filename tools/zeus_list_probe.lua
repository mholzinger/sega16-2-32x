-- ZEUS FLICKER MECHANISM PROBE (arcade oracle, LOOP21).
-- The arcade draws Zeus/orb translucency by 50%-duty 60Hz flicker
-- (mamecap analysis, LOOP20). Question: HOW, at the sprite-list level?
--   (a) hide bit (d2 & 0x4000) toggling per frame, record slot stable
--   (b) record removed/reinserted, list compacted (slots shift)
--   (c) end-marker moved / y-range zeroed / something else
-- The answer decides what a per-vint presence signature must capture
-- for the stipple synthesis (flicker fusion).
--
-- Runs mame altbeast with oracle_shots.lua's coin/start cadence, dumps
-- the first 32 object-RAM records RAW (6 words each) every frame over
-- the round-1 intro window, plus the scene byte 0xFFF031. Snapshots
-- every 100 frames so the log ties to what is on screen.
--   ZP_OUT=csv path (required), ZP_SNAP=snapshot dir (optional)
--   mame altbeast -rompath ./mame -skip_gameinfo -video none -sound none \
--     -nothrottle -window -resolution 160x120 -keyboardprovider none \
--     -nomouse -nojoystick -bench 100 -autoboot_script tools/zeus_list_probe.lua
local out = assert(io.open(os.getenv('ZP_OUT') or '/tmp/zeus_list.csv', 'w'))
local snapdir = os.getenv('ZP_SNAP')
local F0, F1 = 1000, 3200                -- logging window (start fires at 1000)
local frames = 0
local fields = {}
local inited = false
out:write('frame,scene,slot,d0,d1,d2,d3,d4,d5\n')

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
    -- oracle_shots.lua cadence: coin, start, then hold right + jabs so
    -- the run keeps advancing past the intro
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

    if frames >= F0 and frames <= F1 then
        local md = manager.machine.devices[':maincpu'].spaces['program']
        local scene = md:read_u8(0xFFF031)
        for i = 0, 31 do
            local base = 0x440000 + i * 16
            out:write(string.format('%d,%d,%d,%04X,%04X,%04X,%04X,%04X,%04X\n',
                frames, scene, i,
                md:read_u16(base), md:read_u16(base + 2),
                md:read_u16(base + 4), md:read_u16(base + 6),
                md:read_u16(base + 8), md:read_u16(base + 10)))
        end
        if snapdir and frames % 100 == 0 then
            for _, screen in pairs(manager.machine.screens) do
                screen:snapshot(string.format('%s/zp_%04d.png', snapdir, frames))
                break
            end
        end
    end
    if frames > F1 then
        out:close()
        manager.machine:exit()
    end
end)
