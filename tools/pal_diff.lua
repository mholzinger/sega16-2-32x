-- changed CRAM words per frame, arcade, gameplay via coin/start cadence
local frames, nfr, sum, mx = 0, 0, 0, 0
local hist = {0,0,0,0,0,0}
local prev = nil
local fields, inited = {}, false
local function init()
    for _, port in pairs(manager.machine.ioport.ports) do
        for fname, field in pairs(port.fields) do fields[fname] = field end
    end
    inited = true
end
local function set_input(n, v) local f = fields[n]; if f then f:set_value(v) end end
emu.register_frame_done(function()
    if not inited then init() end
    frames = frames + 1
    if frames == 600 or frames == 800 then set_input('Coin 1', 1) end
    if frames == 640 or frames == 840 then set_input('Coin 1', 0) end
    if frames == 1000 then set_input('1 Player Start', 1) end
    if frames == 1040 then set_input('1 Player Start', 0) end
    if frames == 1800 then set_input('P1 Right', 1) end
    local mem = manager.machine.devices[':maincpu'].spaces['program']
    local cur = {}
    local ch = 0
    for i = 0, 2047 do
        local v = mem:read_u16(0x840000 + i * 2)
        cur[i] = v
        if prev and prev[i] ~= v then ch = ch + 1 end
    end
    prev = cur
    if frames > 1000 then
        nfr = nfr + 1
        sum = sum + ch
        if ch > mx then mx = ch end
        local b = ch == 0 and 1 or ch <= 16 and 2 or ch <= 64 and 3
                  or ch <= 256 and 4 or ch <= 1024 and 5 or 6
        hist[b] = hist[b] + 1
    end
    if frames >= 3400 then
        print(string.format('PALDIFF nfr=%d mean=%.1f max=%d buckets 0:%d 1-16:%d 17-64:%d 65-256:%d 257-1024:%d >1024:%d',
            nfr, sum/nfr, mx, hist[1],hist[2],hist[3],hist[4],hist[5],hist[6]))
        manager.machine:exit()
    end
end)
