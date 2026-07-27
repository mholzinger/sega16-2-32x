local out = assert(io.open("/tmp/cmp.txt","w"))
local md = manager.machine.devices[":maincpu"]
local mdsp = md.spaces["program"]
local ms = manager.machine.devices[":sega32x:32x_master_sh2"]
local msp = ms.spaces["program"]
local f=0
emu.register_frame_done(function()
  f=f+1
  if f==300 or f==500 then
    out:write(string.format("f=%d  MD_COMM0(A15120)=%04x  SH2_COMM0(20004020)=%04x  SH2_cached(00004020)=%04x\n",
      f, mdsp:read_u16(0xA15120), msp:read_u16(0x20004020), msp:read_u16(0x00004020)))
    out:write(string.format("      MD_COMM2(A15122)=%04x  SH2_COMM2(20004022)=%04x\n",
      mdsp:read_u16(0xA15122), msp:read_u16(0x20004022)))
    out:flush()
  end
end)
