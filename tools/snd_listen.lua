-- SND LISTEN (SOUND.md P4 debug): drive the sndtest menu in mame 32x with
-- a scripted A press and let -wavwrite capture what the YM/PSG actually
-- output. The menu boots on CMD 94 (round-1 BGM) as of 2026-09-01, so one
-- A press = music_start via the router. Pair with:
--   mame 32x -cart rom/sndtest.32x -rompath ./mame -skip_gameinfo \
--     -video none -sound none -nothrottle -window -resolution 160x120 \
--     -keyboardprovider none -nomouse -nojoystick -seconds_to_run 45 \
--     -wavwrite <abs path>.wav -autoboot_script tools/snd_listen.lua
-- Env: SL_PRESS (frame to press A, default 600), SL_LOG (field-discovery log).
local log = assert(io.open(os.getenv('SL_LOG') or '/tmp/snd_listen.log', 'w'))
local press_at = tonumber(os.getenv('SL_PRESS') or '600')
local mac = manager.machine

local a_field = nil
for pname, port in pairs(mac.ioport.ports) do
    for fname, field in pairs(port.fields) do
        log:write(pname, " :: ", fname, "\n")
        if fname == 'P1 A' or fname == 'P1 Button A' or fname == 'A' then
            a_field = field
            log:write("  ^^ using as A\n")
        end
    end
end
log:flush()

local frame = 0
local sub = emu.add_machine_frame_notifier(function()
    frame = frame + 1
    if a_field then
        if frame == press_at then a_field:set_value(1) end
        if frame == press_at + 30 then a_field:set_value(0) end
    end
end)
