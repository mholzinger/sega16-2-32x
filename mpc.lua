local out = assert(io.open("/tmp/mpc.txt","w"))
local ms = manager.machine.devices[":sega32x:32x_master_sh2"]
local f=0
emu.register_frame_done(function()
  f=f+1
  if f==60 or f==120 or f==300 or f==600 then
    out:write(string.format("f=%d masterPC=%08x\n", f, ms.state["PC"].value))
    out:flush()
  end
end)
