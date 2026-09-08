-- LOOP 19: harvest the LIVE sprite palette demand for the offline packer.
-- Per cycle it records which sprite colour sets are live (exactly the way
-- build_maps computes `sused`: walk SPR_SNAP to the terminator, skip
-- hidden/zero-height records and the 0x3F shadow set) and the 16 colours
-- each of those sets currently holds.
--
-- Palettes are NOT static in ROM -- the game writes palette RAM (fades,
-- colour cycling), so the pack has to be solved over OBSERVED demand, not
-- a ROM table. That is what this dumps.
--
-- Read through the CACHED 0x06 alias: MAME's SH-2 debugger space does not
-- serve 0x26 and returns a clean zero there (TOOLKIT.md).
--   PAL_OUT=/tmp/palharvest.txt mame 32x -cart rom/s16.32x ... \
--       -autoboot_script tools/palharvest.lua
local SPR_SNAP = 0x06028400              -- 64 records x 8 words
local PAL_SPR  = 0x06027000 + 1024 * 2   -- sprite block: 64 sets x 16 words
local out = assert(io.open(os.getenv('PAL_OUT') or '/tmp/palharvest.txt', 'w'))
local every = tonumber(os.getenv('PAL_EVERY') or '6')
local last  = tonumber(os.getenv('PAL_FRAMES') or '5400')
local f, sh2, md = 0, nil, nil
local fields, inited = {}, false
local function init_fields()
    for _, p in pairs(manager.machine.ioport.ports) do
        for n, fl in pairs(p.fields) do fields[n] = fl end
    end
    inited = true
end
local function si(n, v) local x = fields[n] if x then x:set_value(v) end end
emu.register_frame_done(function()
    if not inited then init_fields() end
    f = f + 1
    if f == 600 or f == 800 then si('P1 Start',1); si('P1 A',1) end
    if f == 640 or f == 840 then si('P1 Start',0); si('P1 A',0) end
    if f == 1000 then si('P1 Start',1) end
    if f == 1040 then si('P1 Start',0) end
    if f == 1800 then si('P1 Right',1) end
    if f == 2400 then si('P1 A',1) end
    if f == 2410 then si('P1 A',0) end
    if f == 3000 then si('P1 Right',0) end
    if f == 3600 then si('P1 Left',1) end
    if f == 4200 then si('P1 Left',0); si('P1 A',1) end
    if f == 4210 then si('P1 A',0) end
    if not sh2 then
        for tag, c in pairs(manager.machine.devices) do
            if tag:find('sh2') and c.spaces['program'] then sh2 = c.spaces['program'] end
            if c.tag == ':maincpu' then md = c.spaces['program'] end
        end
        if not sh2 then return end
    end
    if f % every ~= 0 then
        if f > last then out:close(); manager.machine:exit() end
        return
    end
    local live = {}
    for i = 0, 63 do
        local e = SPR_SNAP + i * 16
        local d2 = sh2:read_u16(e + 4)
        if (d2 & 0x8000) ~= 0 then break end
        local d0 = sh2:read_u16(e)
        local top, bot = d0 & 0xFF, (d0 >> 8) & 0xFF
        if (d2 & 0x4000) == 0 and top < bot then
            local sc = sh2:read_u16(e + 8) & 0x3F
            if sc ~= 0x3F then live[sc] = true end
        end
    end
    local n = 0
    for _ in pairs(live) do n = n + 1 end
    if n > 0 then
        local scene = md and md:read_u8(0xFFF031) or 0
        local parts = {string.format('C %d %02X %d', f, scene, n)}
        for sc = 0, 63 do
            if live[sc] then
                local t = {string.format('%02X', sc)}
                for w = 0, 15 do
                    t[#t+1] = string.format('%04X',
                        sh2:read_u16(PAL_SPR + sc * 32 + w * 2) & 0x7FFF)
                end
                parts[#parts+1] = table.concat(t, ',')
            end
        end
        out:write(table.concat(parts, ' ') .. '\n')
    end
    -- TILE side: which tile colours hold a 32X group this cycle, and the
    -- 8 colours each holds. tile_grp[par][c] != 0xFF names the live ones.
    -- Tiles are the BIGGER consumer (19-20 groups vs the sprites' 16) and
    -- are the one side never tested for compression.
    do
        local TG   = 0x0603E480          -- tile_grp[2][128]
        local PALT = 0x06027000          -- tile block: 128 sets x 8 words
        for par = 0, 1 do
            local t = {}
            for c = 0, 127 do
                if sh2:read_u8(TG + par * 128 + c) ~= 0xFF then
                    local e = {string.format('%02X', c)}
                    for w = 0, 7 do
                        e[#e+1] = string.format('%04X',
                            sh2:read_u16(PALT + c * 16 + w * 2) & 0x7FFF)
                    end
                    t[#t+1] = table.concat(e, ',')
                end
            end
            if #t > 0 then
                out:write(string.format('T %d %d %s\n', f, #t, table.concat(t, ' ')))
                break
            end
        end
    end
    if f > last then
        -- pen-usage masks, same run as the palettes (a previous split
        -- across two builds made the packing analysis meaningless)
        for sc = 0, 63 do
            local m = sh2:read_u16(0x0603A7C0 + sc * 2)
            if m ~= 0 then out:write(string.format('U %02X %04X\n', sc, m)) end
        end
        -- tile PIXEL-USAGE masks, already maintained by the MD palette
        -- pack ("gathered from the ROM tile at claim time"): bit v means
        -- some resident tile with this colour draws pixel value v.
        for c = 0, 127 do
            local m = sh2:read_u8(0x0603E380 + c)
            if m ~= 0 then out:write(string.format('M %02X %02X\n', c, m)) end
        end
        out:close(); manager.machine:exit()
    end
end)
