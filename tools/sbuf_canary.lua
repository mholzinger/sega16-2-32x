-- Which of sbuf's margin bytes does anything actually write?
-- `make MDBGALL=1 BQCHUNK=1 CUTBLANK=1 NTWRAP=1 WIN2=1 SBUFCANARY=1`
-- then run this. Prints, per margin row and per margin column, whether
-- the boot signature survived a full scripted play. Surviving = dead
-- space = bytes the region guard can have back.
-- sbuf's address is read from rom/s16.lst (symbol _sbuf) via SBUF_ADDR.
local SBUF = tonumber(os.getenv('SBUF_ADDR') or '0x060054A0')
local W, H = 336, 240
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
    if frames ~= 5400 then return end
    local function sig(r) return (0xA5 ~ ((r * 7) & 0xFF)) & 0xFF end
    print('--- sbuf margin survey (intact = never written = reclaimable) ---')
    for _, r in ipairs({0,1,2,3,4,5,6,7,232,233,234,235,236,237,238,239}) do
        local s, n = sig(r), 0
        for c = 0, W - 1 do
            if sh2:read_u8(SBUF + r * W + c) ~= s then n = n + 1 end
        end
        print(string.format('  row %3d: %4d/%d bytes written  %s',
            r, n, W, n == 0 and 'INTACT' or ''))
    end
    for _, c in ipairs({0,1,2,3,4,5,6,7,328,329,330,331,332,333,334,335}) do
        local n = 0
        for r = 8, 231 do
            if sh2:read_u8(SBUF + r * W + c) ~= sig(r) then n = n + 1 end
        end
        print(string.format('  col %3d: %4d/224 rows written  %s',
            c, n, n == 0 and 'INTACT' or ''))
    end
    manager.machine:exit()
end)
