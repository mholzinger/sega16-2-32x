-- MULTI-WINDOW ARCADE 68K TRACE over a RECORDED PLAYTHROUGH (2026-09-27).
--
-- WHY THIS EXISTS. tools/arcade_trace.lua takes ONE window and drives the game
-- with a bot, and both limits bit: a 40-60 frame window of the level-1 bot
-- cannot reach rounds 2-5, bosses, deaths, continues or the transformation, so
-- four traces saturated at 2,055 distinct PCs and only 101 of 623 functions had
-- a single instruction proven executed. And a window past the -bench limit
-- never arrives at all -- that is why AT_START 7100/9000/12000 produced no
-- trace: -bench 160 ends the run at frame 9600.
--
-- Two changes. Inputs come from Mike's RECORDED full playthrough (inp/ab_full,
-- 103,300 frames, all five rounds, every boss and transformation) instead of a
-- bot, and one pass takes MANY windows, so reaching frame 100,000 is paid once
-- rather than once per window.
--
--   AT_DIR=/tmp/tr AT_LEN=8 \
--   AT_WINDOWS=1500,3203,5500,7600,9000,10884 \
--   mame altbeast -rompath ./mame -skip_gameinfo \
--     -cfg_directory cfg/oracle -nowriteconfig \
--     -input_directory ./inp -playback ab_full \
--     -video none -sound none -nothrottle -window -resolution 160x120 \
--     -keyboardprovider none -nomouse -nojoystick -bench 1800 \
--     -debug -debugger none -autoboot_script tools/arcade_trace_multi.lua
--
-- SIZE -bench ABOVE THE LAST WINDOW (frames / 60 seconds) or it silently
-- truncates, which is the failure this tool was written to stop repeating.
-- Analyse with tools/f1_coverage.py over AT_DIR.
local DIR = os.getenv('AT_DIR') or '/tmp/tr'
local LEN = tonumber(os.getenv('AT_LEN') or '8')
local wins, order = {}, {}
for s in string.gmatch(os.getenv('AT_WINDOWS') or '', '[^,]+') do
    local f = tonumber(s)
    if f then wins[f] = true; order[#order + 1] = f end
end
table.sort(order)
local last = order[#order] or 0
local frames, opened, n_done = 0, nil, 0
print(string.format('trace-multi: %d windows, %d frames each, last at %d',
                    #order, LEN, last))
emu.register_frame_done(function()
    frames = frames + 1
    if wins[frames] and not opened then
        local path = string.format('%s/t%06d.log', DIR, frames)
        manager.machine.debugger:command('trace ' .. path .. ',maincpu')
        opened = frames
    elseif opened and frames >= opened + LEN then
        manager.machine.debugger:command('trace off,maincpu')
        n_done = n_done + 1
        print(string.format('  window %d done (%d/%d)', opened, n_done, #order))
        opened = nil
        if frames > last then
            print('TRACE-MULTI DONE')
            manager.machine:exit()
        end
    end
end)
