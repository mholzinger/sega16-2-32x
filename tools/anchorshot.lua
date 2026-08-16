-- Screenshot at the demo game-state anchor (+45f settle), then exit.
local md = manager.machine.devices[":maincpu"]
local mem = md.spaces["program"]
local f, armed, done = 0, nil, false
emu.register_frame_done(function()
  f = f + 1
  if f < 60 or done then return end
  if armed == nil then
    if mem:read_u8(0xFFF031) == 0x14 and mem:read_u16(0xFFF02A) == 0x0060 then
      armed = 45
    end
  elseif armed > 0 then
    armed = armed - 1
    if armed == 0 then
      manager.machine.video:snapshot()
      done = true
      manager.machine:exit()
    end
  end
  if f > 14000 then manager.machine:exit() end
end)
