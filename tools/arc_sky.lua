-- Arcade ORACLE: dump palette entries 32-47 (0x840040) at a series of
-- frames, to settle whether the gradient sky writer at 0x3108 ever takes
-- effect during play. See docs/log/LOOP-DECOMPILE.md 44.
--   mame altbeast -rompath ./mame -skip_gameinfo -video none -sound none \
--     -nothrottle -window -resolution 160x120 -keyboardprovider none \
--     -nomouse -nojoystick -bench 120 -autoboot_script tools/arc_sky.lua
local f, mem = 0, nil
local marks = {300, 700, 1000, 1800, 2400, 3000}
local i = 1
emu.register_frame_done(function()
  f = f + 1
  if not mem then
    for _, c in pairs(manager.machine.devices) do
      if c.tag == ':maincpu' then mem = c.spaces['program'] end
    end
    if not mem then return end
  end
  if i <= #marks and f == marks[i] then
    local w = {}
    for k = 0, 15 do w[#w+1] = string.format("%04X", mem:read_u16(0x840040 + 2*k)) end
    print(string.format("SKY frame %d: %s", f, table.concat(w, " ")))
    i = i + 1
    if i > #marks then manager.machine:exit() end
  end
end)
