-- SND AUTOPSY (SOUND.md P4 debug): full visibility on the music path in
-- mame 32x. Presses P1 A at SL_PRESS, then once per second logs:
--   COMM6 (router ack), COMM14 (feed), Z80 MSTAT/RD/WR (from the Z80's
--   own space), Z80 PC — and screenshots. ALL device lookups happen on
--   the first frame callback: at script-load the memory system is not
--   ready (snd_tap.lua lesson: MAME dies/misbehaves silently).
-- Env: SL_LOG, SL_SNAPDIR, SL_PRESS (default 600).
local log = assert(io.open(os.getenv('SL_LOG') or '/tmp/snd_autopsy.log', 'w'))
local snapdir = os.getenv('SL_SNAPDIR') or '/tmp'
local press_at = tonumber(os.getenv('SL_PRESS') or '600')

-- GLOBALS on purpose: locals in an autoboot chunk are GC'd once the
-- chunk returns, which silently kills the frame notifier (measured:
-- the log stopped at f=120). Same reason snd_ymtap's taps died.
_G.SA = { frame = 0 }
local SA = _G.SA
local function init()
    local mac = manager.machine
    for _, port in pairs(mac.ioport.ports) do
        for fname, field in pairs(port.fields) do
            if fname == 'P1 A' then SA.a_field = field end
        end
    end
    for _, s in pairs(mac.screens) do SA.screen = s break end
    SA.msp = mac.devices[':maincpu'].spaces['program']
    SA.z80 = mac.devices[':genesis_snd_z80']
    SA.zsp = SA.z80.spaces['program']
    log:write("# init ok; a_field=", tostring(SA.a_field ~= nil),
              " screen=", tostring(SA.screen ~= nil), "\n")
    log:flush()
end

_G.SA_sub = emu.add_machine_frame_notifier(function()
    local ok, err = pcall(function()
        SA.frame = SA.frame + 1
        local frame = SA.frame
        if frame == 1 then init() end
        local a_field = SA.a_field
        if a_field then
            if frame == press_at then a_field:set_value(1) end
            if frame == press_at + 30 then a_field:set_value(0) end
        end
        if frame % 60 == 0 or frame == press_at + 5 then
            local msp, zsp, z80 = SA.msp, SA.zsp, SA.z80
            local c6  = msp:read_u16(0xA15126)
            local c14 = msp:read_u16(0xA1512E)
            local mstat = zsp:read_u8(0x0FF2)
            local mgo   = zsp:read_u8(0x0FF3)
            local rd    = zsp:read_u8(0x0FF0)
            local wr    = zsp:read_u8(0x0FF1)
            local pc    = z80.state['PC'].value
            log:write(string.format(
                "f=%4d COMM6=%04X COMM14=%04X MSTAT=%02X MGO=%02X RD=%02X WR=%02X zPC=%04X\n",
                frame, c6, c14, mstat, mgo, rd, wr, pc))
            log:flush()
        end
        if frame == press_at - 10 or frame == press_at + 60 or frame == press_at + 900 then
            SA.screen:snapshot(string.format('%s/autopsy_%04d.png', snapdir, frame))
        end
    end)
    if not ok then log:write("# ERR ", tostring(err), "\n"); log:flush() end
end)
