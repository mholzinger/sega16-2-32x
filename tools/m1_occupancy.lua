-- REBUILD Phase 0 / M1 — ARCADE 68K FRAME OCCUPANCY.
-- Altered Beast's main loop idles via `stop #$2300` (12 sites): the
-- CPU halts until the vint wakes it. Breakpoint every STOP site and
-- log (beam line, frame) per entry; idle-per-frame is then the span
-- from the STOP to the next vint (line 224). Frames with NO stop hit
-- are 100%-busy frames — the population that decides whether the
-- original binary can pace 60Hz on the MD's 0.77x 68K.
--   mame altbeast -debug -debugger none -log <headless flags> \
--       -autoboot_script tools/m1_occupancy.lua
-- Decode: tools/m1_occupancy.py (reads error.log).
-- THE canonical idle hook: 0x397E is the vint-wait ENTRY
-- (`clr.b $FFF01C; tst/beq poll`) — executes ONCE per wait period.
-- The 12 `stop #$2300` sites turned out to be other-mode idles (PC
-- sampling showed the attract/game idling at 0x3982); keep them as
-- secondary hooks anyway.
local stops = {0x397e,
               0x1a9e6,0x1b0ca,0x1b178,0x1b1d2,0x1b35a,0x1b4ac,
               0x1b4b0,0x1b50c,0x1b5b2,0x1b5d0,0x1b93a,0x1b9e6}
local frames = 0
local fields = {}
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
    if frames == 600 or frames == 800 then set_input('Coin 1', 1) end
    if frames == 640 or frames == 840 then set_input('Coin 1', 0) end
    if frames == 1000 then set_input('1 Player Start', 1) end
    if frames == 1040 then set_input('1 Player Start', 0) end
    if frames > 1100 then
        local p = frames % 240
        set_input('P1 Right', (p < 120) and 1 or 0)
        set_input('P1 Button 1', (p % 40 < 8) and 1 or 0)
    end
    if frames == 120 then
        for _, a in ipairs(stops) do
            manager.machine.debugger:command(string.format(
                'bpset %x,1,{logerror "M1S %%04X %%08X\\n",beamy,frame;g}', a))
        end
    end
    if frames == 5400 then
        manager.machine.debugger:command('bpclear')
        manager.machine:exit()
    end
end)
