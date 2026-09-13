-- OBJECT-HANDLER HARVEST on the arcade (kit tool; Golden Axe rung 5).
-- Every frame, read the handler long at +2 of every active object in the
-- title's object tables and collect the distinct values. These are the
-- code pointers the patcher must relocate that live in DATA records
-- (spawn scripts), invisible to a static scan (AB: HARVESTED_HANDLERS).
-- Layout from the dispatch loops (LOOP-DECOMPILE-GOLDNAXE 10):
--   0x3CD0: 64 objects x 128 B at 0xFFC000, byte 0 bit 7 = active
--   0x3D12: 16 objects x  64 B at 0xFFE100
-- Env: HH_OUT (text file), HH_END (frames, default 5400), HH_PLAY=1 to
-- drive tools/auto_goldnaxe.lua (else attract only).
local out = os.getenv('HH_OUT') or '/tmp/handler_harvest.txt'
local last = tonumber(os.getenv('HH_END') or '5400')
if os.getenv('HH_PLAY') == '1' then dofile('tools/auto_goldnaxe.lua') end
local TABLES = { {0xFFC000, 64, 128}, {0xFFE100, 16, 64} }
local sp = manager.machine.devices[':maincpu'].spaces['program']
local frames, seen, first = 0, {}, {}
emu.register_frame_done(function()
    frames = frames + 1
    for _, t in ipairs(TABLES) do
        local base, n, stride = t[1], t[2], t[3]
        for i = 0, n - 1 do
            local o = base + i * stride
            if (sp:read_u8(o) & 0x80) ~= 0 then
                local h = sp:read_u32(o + 2)
                if h < 0x80000 then seen[h] = (seen[h] or 0) + 1; if not first[h] then first[h] = frames end end
            end
        end
    end
    if frames ~= last then return end
    local vals = {} for h in pairs(seen) do vals[#vals+1] = h end table.sort(vals)
    local f = io.open(out, 'w')
    f:write(string.format('s16b_handler_harvest %s frames 1-%d play=%s: %d distinct handler values\n', manager.machine.system.name, last, os.getenv('HH_PLAY') or '0', #vals))
    for _, h in ipairs(vals) do f:write(string.format('%06x  hits %8d  first f%d\n', h, seen[h], first[h])) end
    f:close(); print(string.format('%d distinct handlers -> %s', #vals, out)); manager.machine:exit()
end)
