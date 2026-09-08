-- LOOP 18 job 2, stage A: is the row-zero marking COMPLETE?
--   make MDBGALL=1 BQCHUNK=1 CUTBLANK=1 NTWRAP=1 WIN2=1 DIRTYROWVERIFY=1
-- Nothing is skipped on that build. It reads back every row the marks
-- claim is all-zero and counts the times it was not. LIED must be 0
-- before any skipping is switched on, and the counter must be shown to
-- FIRE when a mark is removed, or the zero means nothing.
local frames, sh2 = 0, nil
local fields, inited = {}, false
local function init_fields()
    for _, port in pairs(manager.machine.ioport.ports) do
        for fname, field in pairs(port.fields) do fields[fname] = field end
    end
    inited = true
end
local function set_input(n, v) local f = fields[n] if f then f:set_value(v) end end
emu.register_frame_done(function()
    if not inited then init_fields() end
    frames = frames + 1
    if frames == 600 or frames == 800 then set_input('P1 Start', 1); set_input('P1 A', 1) end
    if frames == 640 or frames == 840 then set_input('P1 Start', 0); set_input('P1 A', 0) end
    if frames == 1000 then set_input('P1 Start', 1) end
    if frames == 1040 then set_input('P1 Start', 0) end
    if frames == 1800 then set_input('P1 Right', 1) end
    if frames == 2400 then set_input('P1 A', 1) end
    if frames == 2410 then set_input('P1 A', 0) end
    if frames == 3000 then set_input('P1 Right', 0) end
    if frames == 3300 then set_input('P1 Up', 1) end
    if frames == 3400 then set_input('P1 Up', 0) end
    if frames == 3600 then set_input('P1 Left', 1) end
    if frames == 4200 then set_input('P1 Left', 0) end
    if frames == 4500 then set_input('P1 Down', 1) end
    if frames == 4600 then set_input('P1 Down', 0) end
    if not sh2 then
        for tag, c in pairs(manager.machine.devices) do
            if tag:find('sh2') and c.spaces['program'] then sh2 = c.spaces['program']; break end
        end
        if not sh2 then return end
    end
    if frames % 600 ~= 0 then return end
    local b = 0x0603A768
    local zero, lied, rows = sh2:read_u32(b), sh2:read_u32(b + 4), sh2:read_u32(b + 8)
    print(string.format('f=%d rows claimed zero: %d/%d (%.1f%% skippable)   LIED=%d',
        frames, zero, rows, rows > 0 and 100.0 * zero / rows or 0, lied))
    if frames >= 5400 then manager.machine:exit() end
end)
