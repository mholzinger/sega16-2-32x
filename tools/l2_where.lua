-- WHERE IS THE 68K when the L2 oracle takes its capture?
--
-- tools/l2_oracle.lua captures at MAME's frame_done, which is a VIDEO boundary and
-- says nothing about where the game's own loop stands. The L2 driver assumes the
-- capture brackets one iteration of the frame body (FUN_00003aae, FUN_0000144a,
-- object_dispatcher). Measured 2026-09-28, that assumption costs 155 spurious
-- bytes, so it is worth checking rather than believing.
--
-- Samples the 68K PC at frame_done over a window, and histograms it.
--
--   L2W_OUT=/tmp/where.txt L2W_LO=8990 L2W_HI=9010 \
--   mame altbeast -rompath ./mame -skip_gameinfo -cfg_directory cfg/oracle \
--     -nowriteconfig -input_directory ./inp -playback ab_full \
--     -video none -sound none -nothrottle -window -resolution 160x120 \
--     -keyboardprovider none -nomouse -nojoystick -bench 200 \
--     -autoboot_script tools/l2_where.lua
local OUT = os.getenv('L2W_OUT') or '/tmp/where.txt'
local LO  = tonumber(os.getenv('L2W_LO') or '1')
local HI  = tonumber(os.getenv('L2W_HI') or '100000')

local cpu = manager.machine.devices[':maincpu']
local mem = cpu.spaces['program']
local frames, hist, seq = 0, {}, {}

emu.register_frame_done(function()
    frames = frames + 1
    if frames < LO or frames > HI then return end
    local pc = cpu.state['PC'].value
    hist[pc] = (hist[pc] or 0) + 1
    -- 0xFFFFFEFC is the game's own progress trace: it stores the source address
    -- of the call it is about to make, which names the loop position directly
    seq[#seq + 1] = string.format('%d PC=0x%06X trace=0x%04X F031=0x%02X F02A=0x%04X',
        frames, pc, mem:read_u16(0xFFFEFC), mem:read_u8(0xFFF031), mem:read_u16(0xFFF02A))
    if frames >= HI then
        local f = io.open(OUT, 'w')
        f:write('# 68K PC at frame_done, frames ' .. LO .. '..' .. HI .. '\n')
        local ks = {}
        for pc in pairs(hist) do ks[#ks+1] = pc end
        table.sort(ks, function(a,b) return hist[a] > hist[b] end)
        f:write('# PC histogram:\n')
        for _, pc in ipairs(ks) do f:write(string.format('#   0x%06X  %d\n', pc, hist[pc])) end
        f:write('# per frame:\n')
        for _, l in ipairs(seq) do f:write(l .. '\n') end
        f:close()
        print('l2-where: ' .. #seq .. ' samples -> ' .. OUT)
        manager.machine:exit()
    end
end)
