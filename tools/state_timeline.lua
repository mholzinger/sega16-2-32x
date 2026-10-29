-- ARCADE STATE TIMELINE: print the frame at every change of the game's state byte
-- (0xFFF031), no debugger, no trace. ST_TO = frames to run (default 2000).
--   mame altbeast ... -autoboot_script tools/state_timeline.lua
local TO = tonumber(os.getenv('ST_TO') or '2000')
local frames, last = 0, -1
emu.register_frame_done(function()
    frames = frames + 1
    local st = manager.machine.devices[':maincpu'].spaces['program']:read_u8(0xFFF031)
    if st ~= last then print(string.format('STATE %d %02X', frames, st)); last = st end
    if frames >= TO then print('STATE-TIMELINE DONE'); manager.machine:exit() end
end)
