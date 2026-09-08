-- LOOP 8: compare the MD palette mirror (0xFF9000) against the SH-2
-- palette shadow (PAL_U, 0x06027000) word by word, per 128-word region.
-- The dirty-bit thunks + DREQ delivery replaced a full diff scan, so this
-- is the direct check that every region still converges: a region that
-- stays mismatched is a write site the thunks never mark, or a mask that
-- names the wrong region.
--   PROBE_OUT=/tmp/pal_probe.txt mame 32x -cart rom/s16.32x ...
local frames = 0
local out = assert(io.open(os.getenv('PROBE_OUT') or '/tmp/pal_probe.txt', 'w'))
local last = tonumber(os.getenv('PAL_FRAMES') or '3000')
-- how many frames each region has spent OUT of sync, and the worst word
local bad_frames, worst = {}, {}
for r = 0, 15 do bad_frames[r] = 0; worst[r] = -1 end
local samples = 0

emu.register_frame_done(function()
    frames = frames + 1
    if frames % 10 ~= 0 then return end
    samples = samples + 1
    local sh2 = manager.machine.devices[':sega32x:32x_master_sh2'].spaces['program']
    local md  = manager.machine.devices[':maincpu'].spaces['program']
    for r = 0, 15 do
        local bad = 0
        for i = 0, 127 do
            local w = r * 128 + i
            if md:read_u16(0xFF9000 + w * 2) ~= sh2:read_u16(0x06027000 + w * 2) then
                bad = bad + 1
                if worst[r] < 0 then worst[r] = w end
            end
        end
        if bad > 0 then bad_frames[r] = bad_frames[r] + 1 end
    end
    if frames >= last then
        out:write(string.format('samples=%d (every 10 frames to %d)\n',
                                samples, frames))
        for r = 0, 15 do
            out:write(string.format(
                '  region %2d: out-of-sync in %5d/%d samples (%.1f%%)%s\n',
                r, bad_frames[r], samples, 100 * bad_frames[r] / samples,
                worst[r] >= 0 and string.format('  first bad word %04X', worst[r]) or ''))
        end
        out:close()
        manager.machine:exit()
    end
end)
