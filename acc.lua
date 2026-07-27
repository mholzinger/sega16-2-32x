local out=assert(io.open("/tmp/acc.txt","w"))
local ms=manager.machine.devices[":sega32x:32x_master_sh2"]
local msp=ms.spaces["program"]
local md=manager.machine.devices[":maincpu"]
local mdsp=md.spaces["program"]
local f=0
emu.register_frame_done(function()
 f=f+1
 if f==400 then
  -- framebuffer content (0x24000000 overwrite img), a few words past line table
  local fbnz=0
  for i=0x100,0x100+2000 do if msp:read_u16(0x24000000+i*2)~=0 then fbnz=fbnz+1 end end
  out:write(string.format("INTMSK(20004000)=%04x FBCTL=%04x adapter(A15100)=%04x FB_nonzero(sample)=%d\n",
    msp:read_u16(0x20004000), msp:read_u16(0x2000410A), mdsp:read_u16(0xA15100), fbnz))
  out:flush()
 end
end)
