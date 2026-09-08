-- YM TAP for mame 32x + sndtest (docs/sound/SOUND.md P4 debug): log every Z80-side
-- YM2612 write (Z80 $4000-4003) and PSG write ($7F11), plus 68K-side YM
-- writes ($A04000-3), timestamped — the register-level truth of what the
-- player is telling the chip. Also presses P1 A at frame SL_PRESS.
--   SL_LOG=<log> mame 32x -cart rom/sndtest.32x ... -autoboot_script this
local log = assert(io.open(os.getenv('SL_LOG') or '/tmp/snd_ymtap.log', 'w'))
local press_at = tonumber(os.getenv('SL_PRESS') or '600')
local mac = manager.machine

local a_field = nil
for _, port in pairs(mac.ioport.ports) do
    for fname, field in pairs(port.fields) do
        if fname == 'P1 A' then a_field = field end
    end
end

local taps = {}
local installed = false
local frame = 0
local function install()
    -- genesis Z80: ":genvdp"? the sound Z80 is ":soundcpu" on megadriv
    local z80 = mac.devices[':genesis_snd_z80']
    if z80 and z80.spaces and z80.spaces['program'] then
        local sp = z80.spaces['program']
        taps[#taps+1] = sp:install_write_tap(0x4000, 0x4003, 'ymtap_z80',
            function(offset, data, mask)
                log:write(string.format("%.6f Z%d %02X\n",
                    mac.time:as_double(), offset & 3, data & 0xFF))
            end)
        taps[#taps+1] = sp:install_write_tap(0x7F11, 0x7F11, 'psgtap_z80',
            function(offset, data, mask)
                log:write(string.format("%.6f ZP %02X\n",
                    mac.time:as_double(), data & 0xFF))
            end)
        log:write("# z80 taps installed\n")
    else
        log:write("# NO z80 device found\n")
        for name, _ in pairs(mac.devices) do log:write("# dev ", name, "\n") end
    end
    local m68k = mac.devices[':maincpu']
    if m68k then
        local msp = m68k.spaces['program']
        taps[#taps+1] = msp:install_write_tap(0xA04000, 0xA04003, 'ymtap_68k',
            function(offset, data, mask)
                log:write(string.format("%.6f M%d %04X\n",
                    mac.time:as_double(), offset & 3, data))
            end)
        log:write("# 68k tap installed\n")
    end
    log:flush()
end

local sub = emu.add_machine_frame_notifier(function()
    frame = frame + 1
    if not installed then installed = true; install() end
    if a_field then
        if frame == press_at then a_field:set_value(1) end
        if frame == press_at + 30 then a_field:set_value(0) end
    end
    if frame % 600 == 0 then log:flush() end
    if frame == press_at - 10 or frame == press_at + 300 or frame == press_at + 1100 then
        manager.machine.video:snapshot()
    end
end)
