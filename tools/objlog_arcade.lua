-- Log object 0's (x, y) once per frame from the ARCADE, to compare
-- against our port's OBJLOG ring (NOTES 72 / LOOP29 280).
-- 0xFFC00C = x, 0xFFC010 = y, identified by diffing object 0 across
-- the demo on our side and the same addresses on the arcade program.
--   mame altbeast -video none -sound none -nothrottle -window \
--       -resolution 160x120 -keyboardprovider none -nomouse -nojoystick \
--       -skip_gameinfo -bench 60 -autoboot_script tools/objlog_arcade.lua
local N = tonumber(os.getenv("OBJLOG_N") or "2200")
local OUT = os.getenv("OBJLOG_OUT") or "/tmp/obj_arcade.txt"
local mem, fh, n = nil, io.open(OUT, "w"), 0
emu.register_frame_done(function()
    if mem == nil then
        mem = manager.machine.devices[":maincpu"].spaces["program"]
    end
    local x = mem:read_u16(0xFFC00C)
    local y = mem:read_u16(0xFFC010)
    fh:write(string.format("%d %04X %04X\n", n, x, y))
    n = n + 1
    if n >= N then fh:close(); manager.machine:exit() end
end)
