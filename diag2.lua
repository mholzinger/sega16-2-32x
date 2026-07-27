local out = assert(io.open("/tmp/diag2.txt","w"))
local ms = manager.machine.devices[":sega32x:32x_master_sh2"]
local msp = ms.spaces["program"]
local f=0
emu.register_frame_done(function()
  f=f+1
  if f==300 or f==500 then
    out:write(string.format("f=%d CRAM[0](rawCOMM0)=%04x  CRAM[1](0x7FFF test)=%04x\n",
      f, msp:read_u16(0x20004200), msp:read_u16(0x20004202)))
    out:flush()
  end
end)
