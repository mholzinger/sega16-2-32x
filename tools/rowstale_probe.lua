-- LOOP 18: what fraction of blitted rows is byte-identical to the SAME
-- row last cycle? That is the size of job 2 (compose-marked dirty rows),
-- because a row the blit never has to READ is worth its full cost --
-- unlike every partial scheme, which returns sub-linearly.
-- This is a RENDERING property, not a timing one, so MAME answers it
-- honestly. Build WITHOUT SPRTRUNC so the sprite transport is faithful.
--   make MDBGALL=1 BQCHUNK=1 CUTBLANK=1 NTWRAP=1 WIN2=1 ROWSTALE=1
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
    if frames == 3600 then set_input('P1 Left', 1) end
    if frames == 4200 then set_input('P1 Left', 0) end
    if not sh2 then
        for tag, c in pairs(manager.machine.devices) do
            if tag:find('sh2') and c.spaces['program'] then sh2 = c.spaces['program']; break end
        end
        if not sh2 then return end
    end
    if frames % 600 ~= 0 then return end
    local same = sh2:read_u32(0x06028000 + 32 * 4)
    local chk  = sh2:read_u32(0x06028000 + 33 * 4)
    local opp = sh2:read_u32(0x06028000 + 50 * 4)
    local skp = sh2:read_u32(0x06028000 + 51 * 4)
    print(string.format('f=%d unchanged vs last cycle: %d/%d (%.1f%%)   '
        .. 'PER-BANK skippable: %d/%d (%.1f%%)',
        frames, same, chk, chk > 0 and 100.0 * same / chk or 0,
        skp, opp, opp > 0 and 100.0 * skp / opp or 0))
    if frames >= 5400 then manager.machine:exit() end
end)
