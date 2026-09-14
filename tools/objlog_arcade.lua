-- Arcade side of the object-0 position log (NOTES 73 / LOOP29 281).
-- Indexed on the GAME'S OWN demo frame counter at 0xFFF02A -- the word
-- the tape reads -- so our log and this one share an index exactly and
-- no boot offset or release-rate artifact can shift them.
-- 0xFFC00C = x, 0xFFC010 = y.
--   mame altbeast -rompath ./mame -skip_gameinfo -video none -sound none \
--       -nothrottle -window -resolution 160x120 -keyboardprovider none \
--       -nomouse -nojoystick -bench 60 -autoboot_script tools/objlog_arcade.lua
local N   = tonumber(os.getenv("OBJLOG_N") or "3000")
local OUT = os.getenv("OBJLOG_OUT") or "/tmp/obj_arcade.txt"
local mem, fh, n = nil, io.open(OUT, "w"), 0
local prev, state = 0, 0
emu.register_frame_done(function()
    if mem == nil then
        mem = manager.machine.devices[":maincpu"].spaces["program"]
    end
    n = n + 1
    local c = mem:read_u16(0xFFF02A)
    -- 0xFFF02A is zeroed at EVERY game start, so successive demos reuse
    -- the same indices. Capture ONE demo: stop the first time the
    -- counter goes backwards.
    -- state 0: wait for a RESET (counter goes backwards) -- that is a
    -- fresh game start and the only well-defined point to sync on.
    -- state 1: log until the NEXT reset. So both machines capture the
    -- same demo instance and boot differences cannot shift it.
    if state == 0 then
        if c < prev then state = 1 end
    elseif state == 1 then
        if c < prev then state = 2
        elseif c < 2048 then
            fh:write(string.format("%d %04X %04X\n", c,
                     mem:read_u16(0xFFC00C), mem:read_u16(0xFFC010)))
        end
    end
    prev = c
    if state == 2 or n >= N then fh:close(); manager.machine:exit() end
end)
