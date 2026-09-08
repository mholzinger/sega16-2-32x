-- Can the MD sprite chip draw this game's sprites?
--   make MDBGALL=1 BQCHUNK=1 CUTBLANK=1 NTWRAP=1 WIN2=1 SPRLINE=1
-- H40 budget: 20 sprites and 320 sprite-pixels per scanline.
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
    if frames == 4200 then set_input('P1 Left', 0); set_input('P1 A', 1) end
    if frames == 4210 then set_input('P1 A', 0) end
    if not sh2 then
        for tag, c in pairs(manager.machine.devices) do
            if tag:find('sh2') and c.spaces['program'] then sh2 = c.spaces['program']; break end
        end
        if not sh2 then return end
    end
    if frames % 900 ~= 0 then return end
    local b = 0x0603A780
    local over_s = sh2:read_u32(b)
    local over_p = sh2:read_u32(b + 4)
    local max_s  = sh2:read_u32(b + 8)
    local max_p  = sh2:read_u32(b + 12)
    local lines  = sh2:read_u32(b + 16)
    local nomd   = sh2:read_u32(b + 20)
    print(string.format(
        'f=%d lines=%d  OVER 20 sprites: %d (%.2f%%)  OVER 320 px: %d (%.2f%%)  '
        .. 'worst %d sprites / %d px  cannot-go-to-MD sprites: %d',
        frames, lines, over_s, lines>0 and 100*over_s/lines or 0,
        over_p, lines>0 and 100*over_p/lines or 0, max_s, max_p, nomd))
    local mx_all = sh2:read_u32(b + 24)
    local mx_elig = sh2:read_u32(b + 28)
    local cyc = sh2:read_u32(b + 60)
    local hist = {}
    for i = 0, 6 do hist[i+1] = sh2:read_u32(b + 32 + i*4) end
    print(string.format(
        '     PALETTE: worst distinct S16 colour sets per cycle = %d (all) / %d (MD-eligible)',
        mx_all, mx_elig))
    print(string.format(
        '     eligible-set histogram over %d cycles: 1:%d  2:%d  3:%d  4:%d  5-6:%d  7-8:%d  >8:%d',
        cyc, hist[1], hist[2], hist[3], hist[4], hist[5], hist[6], hist[7]))
    local w8,w4,w2 = sh2:read_u32(b+64), sh2:read_u32(b+68), sh2:read_u32(b+72)
    local f8,f4,f2 = sh2:read_u32(b+76), sh2:read_u32(b+80), sh2:read_u32(b+84)
    print(string.format(
        '     RASTER SWAPS: worst sets in any span -- 8 swaps:%d  4 swaps:%d  2 swaps:%d  1 swap:%d',
        w8, w4, w2, mx_elig))
    print(string.format(
        '     cycles fully served by 3 MD lines -- 8 swaps:%d/%d (%.0f%%)  4:%d (%.0f%%)  2:%d (%.0f%%)',
        f8, cyc, cyc>0 and 100*f8/cyc or 0, f4, cyc>0 and 100*f4/cyc or 0,
        f2, cyc>0 and 100*f2/cyc or 0))
    if frames >= 5400 then manager.machine:exit() end
end)
