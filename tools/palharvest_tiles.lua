-- EXPERIMENT R60/E4: harvest the LIVE **TILE** palette demand for the
-- offline fade-stable pack analysis (palharvest.lua's sprite twin).
--
-- Per sampled cycle it records, for BOTH tile_grp parities (unioned),
-- every tile colour set c (0..127) that currently holds a 32X CRAM
-- group (tile_grp[par][c] != 0xFF), together with:
--   * its CURRENT pixel-usage mask mdp_s_used[c] (bit v set => some
--     resident ROM tile with this colour draws pixel value v) — dumped
--     per cycle, not only at exit, because the mask accumulates as
--     tiles are claimed and the pack must know what was used WHEN,
--   * the group it holds this cycle,
--   * the 8 colours PAL_SH[c*8 .. c*8+7] holds this cycle (fades and
--     colour cycling rewrite these — the whole point of the harvest).
--
-- Layout confirmed against sh_src/m_main.c:
--   PAL_SH tile block  = 0x06027000 + c*16 bytes, 128 sets x 8 words
--                        (cram_paint(cram + g*8, PAL_SH + c*8, ...))
--   sprite block       = PAL_SH + 1024 words (64 sets x 16 words)
--   tile_grp[2][128]   = 0x0603E480 (fixed block, LOOP 13)
--   mdp_s_used[128]    = 0x0603E380 (fixed block)
--   grp_key[32]        = 0x06028360
--
-- Read through the CACHED 0x06 alias: MAME's SH-2 debugger space does
-- not serve 0x26 and returns a clean zero there (TOOLKIT.md).
--
--   PAL_OUT=/tmp/palharvest_t.txt PAL_FRAMES=5400 mame 32x ... \
--       -autoboot_script tools/palharvest_tiles.lua
--
-- Output lines:
--   C f scene n  s0,... sprite record (same as palharvest.lua)
--   T f scene n  cc,mm,gg,w0..w7 ...   one entry per live tile set
--   G f  k0..k31                       grp_key snapshot (FF = free)
--   U sc mask                          sprite pen masks at exit
--   M cc mask                          tile pixel masks at exit
local SPR_SNAP = 0x06028400              -- 64 records x 8 words
local PAL_BASE = 0x06027000              -- tile block: 128 sets x 8 words
local PAL_SPR  = PAL_BASE + 1024 * 2     -- sprite block: 64 sets x 16 words
local TG       = 0x0603E480              -- tile_grp[2][128]
local MUSED    = 0x0603E380              -- mdp_s_used[128]
local GRPKEY   = 0x06028360              -- grp_key[32]
local out = assert(io.open(os.getenv('PAL_OUT') or '/tmp/palharvest_t.txt', 'w'))
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
    -- same scripted playthrough as palharvest.lua (title -> gameplay
    -- -> walk right -> punch -> walk left) so the two corpora cover
    -- the same scenes and fades
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
    local scene = md and md:read_u8(0xFFF031) or 0
    -- SPRITE side (unchanged from palharvest.lua) -----------------------
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
    -- TILE side: UNION of both tile_grp parities (the old harvest broke
    -- out after the first parity with entries and could miss the other) --
    local tl = {}   -- c -> group (prefer par 0's answer, they agree in law)
    for par = 0, 1 do
        for c = 0, 127 do
            local g = sh2:read_u8(TG + par * 128 + c)
            if g ~= 0xFF and tl[c] == nil then tl[c] = g end
        end
    end
    local tn, order = 0, {}
    for c in pairs(tl) do tn = tn + 1; order[#order+1] = c end
    table.sort(order)
    if tn > 0 then
        local parts = {string.format('T %d %02X %d', f, scene, tn)}
        for _, c in ipairs(order) do
            local e = {string.format('%02X,%02X,%02X',
                c, sh2:read_u8(MUSED + c), tl[c])}
            for w = 0, 7 do
                e[#e+1] = string.format('%04X',
                    sh2:read_u16(PAL_BASE + c * 16 + w * 2) & 0x7FFF)
            end
            parts[#parts+1] = table.concat(e, ',')
        end
        out:write(table.concat(parts, ' ') .. '\n')
        -- grp_key snapshot: who owns each of the 32 groups right now
        local gk = {}
        for g = 0, 31 do gk[#gk+1] = string.format('%02X', sh2:read_u8(GRPKEY + g)) end
        out:write(string.format('G %d %s\n', f, table.concat(gk, ',')))
    end
    if f > last then
        for sc = 0, 63 do
            local m = sh2:read_u16(0x0603A7C0 + sc * 2)
            if m ~= 0 then out:write(string.format('U %02X %04X\n', sc, m)) end
        end
        for c = 0, 127 do
            local m = sh2:read_u8(MUSED + c)
            if m ~= 0 then out:write(string.format('M %02X %02X\n', c, m)) end
        end
        out:close(); manager.machine:exit()
    end
end)
