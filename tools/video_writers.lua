-- ARCADE VIDEO-WRITER CENSUS (2026-10-04). Which 68K PCs write the four S16
-- video regions during the attract -- the list the port's address translation
-- must cover. One line per write: "V <region> <pc> <addr> <data>".
--   VW_DIR=/tmp/vw VW_FROM=1 VW_TO=2000 mame altbeast ... -debug -debugger none \
--       -autoboot_script tools/video_writers.lua
-- Measured need: rd3 (cold-boot port) landed 2359/26624 tilemap words and
-- 25/2048 palette words at frame 2001 against the line's 19520 and 1187 -- the
-- game's bulk video stores go through pointer forms the generator never saw.
local DIR  = os.getenv('VW_DIR') or '/tmp/vw'
local FROM = tonumber(os.getenv('VW_FROM') or '1')
local TO   = tonumber(os.getenv('VW_TO') or '2000')
local frames, armed = 0, false
emu.register_frame_done(function()
    frames = frames + 1
    local dbg = manager.machine.debugger
    -- one marker per frame once armed: "F <frame> <0xFFF031 state>" so the
    -- writes can be binned by the game's own state (tools/video_writers_hist.py --frames)
    if armed then
        local st = manager.machine.devices[':maincpu'].spaces['program']:read_u8(0xFFF031)
        dbg:command(string.format('tracelog "F %d %02X\\n"', frames, st))
    end
    if frames == FROM and not armed then
        dbg:command('trace ' .. DIR .. '/vw.log,maincpu,noloop,{tracelog ""}')
        dbg:command('wpset 400000,10000,w,1,{tracelog "V tile %06X %06X %04X\\n",pc,wpaddr,wpdata; g}')
        dbg:command('wpset 410000,1000,w,1,{tracelog "V text %06X %06X %04X\\n",pc,wpaddr,wpdata; g}')
        dbg:command('wpset 440000,400,w,1,{tracelog "V spr %06X %06X %04X\\n",pc,wpaddr,wpdata; g}')
        dbg:command('wpset 840000,1000,w,1,{tracelog "V pal %06X %06X %04X\\n",pc,wpaddr,wpdata; g}')
        armed = true
        print('video_writers: armed at frame ' .. frames)
    elseif armed and frames >= TO then
        dbg:command('wpclear'); dbg:command('trace off,maincpu')
        print('VIDEO-WRITERS DONE'); manager.machine:exit()
    end
end)
