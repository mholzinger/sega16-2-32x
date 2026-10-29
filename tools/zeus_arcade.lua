-- ORACLE: does the arcade clear the Zeus playfield text DURING PLAY?
-- Our port's only clear for that rectangle is 0x9052, gated on an object
-- holding the loop (0xFFF148), which never sets in our play window -- so
-- the text sits until the transformation. Ask the arcade the same question:
-- coin up, start, and sample the SAME rectangle 0x9052 clears
-- (text 0x410000 + 0x230: rows 4-23, cols 24-63, 64 cols/row).
--   ZEUS_OUT=/tmp/zeus_arc.txt mame altbeast -rompath ./mame -skip_gameinfo \
--     -video none -sound none -nothrottle -window -resolution 160x120 \
--     -keyboardprovider none -nomouse -nojoystick -bench 200 \
--     -autoboot_script tools/zeus_arcade.lua
local OUT = os.getenv("ZEUS_OUT") or "/tmp/zeus_arc.txt"
local fh  = io.open(OUT, "w")
local mem, n = nil, 0
local TEXT = 0x410000
local function count()
    local cells, chars = 0, {}
    for row = 4, 23 do
        for col = 24, 63 do
            local w = mem:read_u16(TEXT + (row * 64 + col) * 2)
            if w ~= 0 and (w & 0xFF) ~= 0x20 then
                cells = cells + 1
                local c = w & 0xFF
                if c >= 32 and c < 127 then chars[#chars+1] = string.char(c) end
            end
        end
    end
    return cells, table.concat(chars)
end
emu.register_frame_done(function()
    if mem == nil then mem = manager.machine.devices[":maincpu"].spaces["program"] end
    n = n + 1
    -- coin + start so we reach PLAY, where our defect lives
    local p = manager.machine.ioport.ports
    local function set(tag, field, v)
        if p[tag] and p[tag].fields[field] then p[tag].fields[field]:set_value(v) end
    end
    -- real port names, from a port dump: coins and start live on :SERVICE
    if n >= 120 and n <= 128 then set(":SERVICE", "Coin 1", 1)
    elseif n == 129 then set(":SERVICE", "Coin 1", 0) end
    if n >= 200 and n <= 208 then set(":SERVICE", "1 Player Start", 1)
    elseif n == 209 then set(":SERVICE", "1 Player Start", 0) end
    if n % 20 == 0 and n <= 6000 then
        local cells, txt = count()
        local st  = mem:read_u8(0xFFF031)
        local obj = mem:read_u16(0xFFF148)
        fh:write(string.format("f%-5d step=%02X obj=%04X cells=%3d %s\n",
                               n, st, obj, cells, txt:sub(1, 40)))
        fh:flush()
    end
    if n >= 6000 then fh:close(); manager.machine:exit() end
end)
