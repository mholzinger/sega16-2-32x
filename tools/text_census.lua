-- P3-era TEXT-CLASS CENSUS (arcade oracle). Question: how many S16
-- text colour sets are ever LIVE, which glyph rows use them, and do
-- their palette rows fade (i.e., are static text classes safe)?
--
--   mame altbeast -rompath ./mame ... -autoboot_script tools/text_census.lua
--   TXTC_OUT=path.csv  TXTC_FRAMES=N
--
-- Text RAM: 0x410000, 2048 words; a glyph word's colour set is
-- bits 11:9 ((d>>9)&7), tile in bits 8:0. Palette rows for text sets
-- live in palette RAM entries set*8.. (3bpp glyphs, 8 pens).
-- CSV per sample: frame, per-set nonzero-glyph counts (8 cols),
-- then per-set palette row hash (8 cols) to detect fades.
local out_path = os.getenv('TXTC_OUT') or '/tmp/text_census.csv'
local max_frames = tonumber(os.getenv('TXTC_FRAMES') or '5400')
local out = assert(io.open(out_path, 'w'))
out:write('frame,n0,n1,n2,n3,n4,n5,n6,n7,'
       .. 'p0,p1,p2,p3,p4,p5,p6,p7\n')
local frames = 0
local mem = manager.machine.devices[':maincpu'].spaces['program']

local function sample()
    local n = {0,0,0,0,0,0,0,0}
    for i = 0, 2047 do
        local d = mem:read_u16(0x410000 + i * 2)
        if (d & 0x1FF) ~= 0 then
            local s = (d >> 9) & 7
            n[s + 1] = n[s + 1] + 1
        end
    end
    local p = {}
    for s = 0, 7 do
        local h = 0
        for e = 0, 7 do
            -- text palette rows: same bank the tile sets use, set s
            local c = mem:read_u16(0x840000 + (s * 8 + e) * 2)
            h = (h * 31 + (c & 0x7FFF)) & 0xFFFFFF
        end
        p[s + 1] = string.format('%06X', h)
    end
    out:write(string.format('%d,%d,%d,%d,%d,%d,%d,%d,%d,%s,%s,%s,%s,%s,%s,%s,%s\n',
        frames, n[1],n[2],n[3],n[4],n[5],n[6],n[7],n[8],
        p[1],p[2],p[3],p[4],p[5],p[6],p[7],p[8]))
end

emu.register_frame_done(function()
    frames = frames + 1
    if frames % 10 == 0 then sample() end
    if frames >= max_frames then
        out:close()
        manager.machine:exit()
    end
end)
