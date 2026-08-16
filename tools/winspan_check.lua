-- Print the WINSPAN accumulator (mean/max MD consume span) at exit.
--   mame 32x -cart <rom> ... -str 65 -autoboot_script tools/winspan_check.lua
local mdp = manager.machine.devices[":maincpu"].spaces["program"]
local f = 0
emu.register_frame_done(function()
  f = f + 1
  if f >= 3600 then
    local sum = mdp:read_u32(0xFFA038)
    local n   = mdp:read_u16(0xFFA03C)
    local mx  = mdp:read_u16(0xFFA03E)
    print(string.format("WINSPAN: mean=%.1f lines max=%d n=%d",
      n > 0 and sum / n or 0, mx, n))
    manager.machine:exit()
  end
end)
