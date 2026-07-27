local out = assert(io.open("/tmp/ps.txt","w"))
local md = manager.machine.devices[":maincpu"]
local sp = md.spaces["program"]
local ms = manager.machine.devices[":sega32x:32x_master_sh2"]
local f=0
emu.register_frame_done(function()
  f=f+1
  if f==300 then
    local nz=0
    for i=0,255 do if sp:read_u16(0xFFA000+i*2)~=0 then nz=nz+1 end end
    out:write(string.format("palShadow nonzero=%d  [0]=%04x [2]=%04x [4]=%04x [10]=%04x\n",
      nz, sp:read_u16(0xFFA000), sp:read_u16(0xFFA002), sp:read_u16(0xFFA004), sp:read_u16(0xFFA014)))
    out:write(string.format("master SH2 PC=%08x\n", ms.state["PC"].value))
    out:flush()
  end
end)
