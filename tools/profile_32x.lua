local frames = 0
local out = assert(io.open('/private/tmp/claude-501/-Users-mikeholzinger-src-sega16-2-32x/821761a1-b62c-404f-824a-6f25c3a157b9/scratchpad/prof.txt', 'w'))
local names = {'copy','maps','cram','compose','slavewait','blit1','flipwait','blit2','total','n2','bg','fg','spr','text'}
emu.register_frame_done(function()
    frames = frames + 1
    if frames % 600 ~= 0 then return end
    local sh2 = manager.machine.devices[':sega32x:32x_master_sh2'].spaces['program']
    -- DIAG base is 0x06028000 (0x27000 is PAL_SH — this read the palette
    -- as counters and reported nonsense; corrected LOOP 6).
    local n = sh2:read_u32(0x06028000 + 9 * 4)
    if n == 0 then return end
    local line = string.format('f=%d n=%d', frames, n)
    for i = 1, 14 do
        local v = sh2:read_u32(0x06028000 + (i - 1) * 4)
        line = line .. string.format(' %s=%.2fms', names[i], v / n * 1.37e-3)
    end
    out:write(line .. '\n')
    out:flush()
    if frames >= 3000 then manager.machine:exit() end
end)
