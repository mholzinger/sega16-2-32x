local out = assert(io.open("/tmp/reg.txt","w"))
local md = manager.machine.devices[":maincpu"]
local sp = md.spaces["program"]
local f=0
emu.register_frame_done(function()
  f=f+1
  if f==120 or f==180 or f==200 or f==300 or f==600 then
    out:write(string.format("f=%d gamePC=%06x c12=%04x c0=%04x SR=%04x\n",
      f, md.state["CURPC"].value, sp:read_u16(0xA1512C), sp:read_u16(0xA15120), md.state["SR"].value))
    out:flush()
  end
end)
