-- ARCADE 68K INSTRUCTION TRACE (the oracle's own execution record).
--
-- Sampling the PC is worthless here: every hook MAME offers fires at a
-- fixed phase, and the game is always in its wait loop at frame end, so
-- a sampler reports 98% idle no matter what the CPU is doing. Memory
-- taps do not see the work RAM in this driver either. What does work is
-- MAME's own instruction trace, which is exact.
--
--   AT_START=7000 AT_LEN=8 AT_OUT=/tmp/arc.log AT_PLAY=1 \
--     mame altbeast -rompath ./mame -skip_gameinfo -video none -sound none \
--       -nothrottle -window -resolution 160x120 -keyboardprovider none \
--       -nomouse -nojoystick -bench 160 -debug -debugger none \
--       -autoboot_script tools/arcade_trace.lua
--
-- AT_PLAY=1 drives the level-1 playthrough (tools/auto_beast.lua) so the
-- trace covers gameplay rather than attract. Analyse with
-- tools/arcade_trace.py.
local START = tonumber(os.getenv('AT_START') or '7000')
local LEN   = tonumber(os.getenv('AT_LEN')   or '8')
local OUT   = os.getenv('AT_OUT') or '/tmp/arc.log'
local frames = 0
if os.getenv('AT_PLAY') == '1' then dofile('tools/auto_beast.lua') end
emu.register_frame_done(function()
    frames = frames + 1
    if frames == START then
        manager.machine.debugger:command("trace " .. OUT .. ",maincpu")
    elseif frames == START + LEN then
        manager.machine.debugger:command("trace off,maincpu")
        print("TRACE DONE " .. OUT)
        manager.machine:exit()
    end
end)
