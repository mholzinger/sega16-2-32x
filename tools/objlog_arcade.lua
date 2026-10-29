-- Arcade side of the object-0 position log (NOTES 74 / LOOP29 283).
-- 0xFFF02A has two jobs: a countdown on the attract's card steps and
-- the demo frame counter inside a demo step, re-zeroed at every game
-- start. So scope it exactly as the port does: only demo steps
-- (0xFFF031 in {0x04, 0x0C, 0x14}), only AFTER the reset inside one,
-- logging until the step changes. 0xFFC00C = x, 0xFFC010 = y.
--   OBJLOG_OUT=/tmp/obj_arcade.txt mame altbeast -rompath ./mame \
--     -skip_gameinfo -video none -sound none -nothrottle -window \
--     -resolution 160x120 -keyboardprovider none -nomouse -nojoystick \
--     -bench 120 -autoboot_script tools/objlog_arcade.lua
local N    = tonumber(os.getenv("OBJLOG_N") or "6000")
local OUT  = os.getenv("OBJLOG_OUT") or "/tmp/obj_arcade.txt"
local mem, fh, n = nil, io.open(OUT, "w"), 0
local prev, state = 0, 0
local function isdemo(st) return st == 0x04 or st == 0x0C or st == 0x14 end
emu.register_frame_done(function()
    if mem == nil then mem = manager.machine.devices[":maincpu"].spaces["program"] end
    n = n + 1
    local st = mem:read_u8(0xFFF031)
    local c  = mem:read_u16(0xFFF02A)
    if state == 0 then
        if isdemo(st) and c < prev then
            state = 1
            fh:write(string.format("# step %02X\n", st))
        end
    elseif state == 1 then
        if not isdemo(st) then state = 2
        elseif c < 2048 then
            fh:write(string.format("%d %04X %04X\n", c,
                     mem:read_u16(0xFFC00C), mem:read_u16(0xFFC010)))
        end
    end
    prev = c
    if state == 2 or n >= N then fh:close(); manager.machine:exit() end
end)
