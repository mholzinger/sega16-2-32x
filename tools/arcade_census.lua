local screen
for _, s in pairs(manager.machine.screens) do screen = s break end
local mem = manager.machine.devices[":maincpu"].spaces["program"]
local n = 0
local outdir = os.getenv('CENSUS_DIR')
local last = tonumber(os.getenv('RD_LAST') or '6000')
local function dump(addr, len, path)
    local f = io.open(path, 'wb')
    local t = {}
    for i = 0, len - 1, 2 do
        local v = mem:read_u16(addr + i)
        t[#t+1] = string.char(v >> 8, v & 0xFF)
        if #t >= 4096 then f:write(table.concat(t)); t = {} end
    end
    f:write(table.concat(t)); f:close()
end
emu.register_frame_done(function()
    n = n + 1
    screen:snapshot(string.format('ref_%06d.png', n))
    if n % 20 == 0 or (n >= 160 and n <= 240 and n % 5 == 0) or (n >= 440 and n <= 470) then
        dump(0x400000, 0x10000, string.format('%s/tile_%06d.bin', outdir, n))
        dump(0x410000, 0x1000,  string.format('%s/text_%06d.bin', outdir, n))
        dump(0xFFF000, 0x1000,  string.format('%s/wram_%06d.bin', outdir, n))
    end
    if n >= last then manager.machine:exit() end
end)
