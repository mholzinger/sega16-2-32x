-- GENERIC ARCADE WRITE CENSUS: MAME watchpoints on WP_RANGES ("FFF800:400,FFEC80:100"),
-- armed from WP_FROM to WP_TO, one "W <range#> <pc> <addr> <data>" line per write plus
-- "F <frame> <state>" markers (tools/video_writers_hist.py reads the same shape with
-- region names = range numbers).   WP_DIR=/tmp/wp WP_FROM=940 WP_TO=950
local DIR  = os.getenv('WP_DIR') or '/tmp/wp'
local FROM = tonumber(os.getenv('WP_FROM') or '1'); local TO = tonumber(os.getenv('WP_TO') or '60')
local ranges = {}
for s in string.gmatch(os.getenv('WP_RANGES') or 'FFF800:400', '[^,]+') do
    local a, l = s:match('(%x+):(%x+)'); ranges[#ranges+1] = {a, l}
end
local frames, armed = 0, false
local function arm(dbg)
    dbg:command('trace ' .. DIR .. '/wp.log,maincpu,noloop,{tracelog ""}')
    for i, r in ipairs(ranges) do
        dbg:command(string.format('wpset %s,%s,w,1,{tracelog "V r%d %%06X %%06X %%04X\\n",pc,wpaddr,wpdata; g}', r[1], r[2], i))
    end
    armed = true
end
-- WP_FROM=0: arm before the first frame runs (the boot's own writes)
if FROM == 0 then arm(manager.machine.debugger) end
emu.register_frame_done(function()
    frames = frames + 1
    local dbg = manager.machine.debugger
    if armed then
        local st = manager.machine.devices[':maincpu'].spaces['program']:read_u8(0xFFF031)
        dbg:command(string.format('tracelog "F %d %02X\\n"', frames, st))
    end
    if frames == FROM and not armed then
        dbg:command('trace ' .. DIR .. '/wp.log,maincpu,noloop,{tracelog ""}')
        for i, r in ipairs(ranges) do
            dbg:command(string.format('wpset %s,%s,w,1,{tracelog "V r%d %%06X %%06X %%04X\\n",pc,wpaddr,wpdata; g}', r[1], r[2], i))
        end
        armed = true
    elseif armed and frames >= TO then
        dbg:command('wpclear'); dbg:command('trace off,maincpu'); print('WP-CENSUS DONE'); manager.machine:exit()
    end
end)
