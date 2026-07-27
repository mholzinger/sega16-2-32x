-- Dump active object slots (status, handler ptr, state bytes) each second.
-- Config via env-ish globals set below per target.
local frames = 0
local out = assert(io.open('/private/tmp/claude-501/-Users-mikeholzinger-src-sega16-2-32x/821761a1-b62c-404f-824a-6f25c3a157b9/scratchpad/obj_ours.txt', 'w'))
local fields = {}
local inited = false
local function init_fields()
    for tag, port in pairs(manager.machine.ioport.ports) do
        for fname, field in pairs(port.fields) do fields[fname] = field end
    end
    inited = true
end
emu.register_frame_done(function()
    if not inited then init_fields() end
    frames = frames + 1
    local f
    local COINB = {'P1 Start', 'P1 A'}
    local STARTB = {'P1 Start'}
    if frames == 600 then for _, n in ipairs(COINB) do f = fields[n]; if f then f:set_value(1) end end end
    if frames == 600 + 30 then for _, n in ipairs(COINB) do f = fields[n]; if f then f:set_value(0) end end end
    if frames == 780 then for _, n in ipairs(STARTB) do f = fields[n]; if f then f:set_value(1) end end end
    if frames == 780 + 30 then for _, n in ipairs(STARTB) do f = fields[n]; if f then f:set_value(0) end end end
    if frames < 780 + 120 or frames % 60 ~= 0 then return end
    local mem = manager.machine.devices[':maincpu'].spaces['program']
    local line = {}
    for slot = 0, 63 do
        local base = 0xFFC000 + slot * 128
        local st = mem:read_u8(base)
        if st >= 0x80 then
            local h = mem:read_u32(base + 2)
            local s1 = mem:read_u8(base + 6)
            local s2 = mem:read_u8(base + 7)
            line[#line + 1] = string.format('%d:%02X/%06X/%02X%02X', slot, st, h & 0xFFFFFF, s1, s2)
        end
    end
    out:write(string.format('f=%d %s\n', frames, table.concat(line, ' ')))
    out:flush()
    if frames >= 780 + 1500 then manager.machine:exit() end
end)
