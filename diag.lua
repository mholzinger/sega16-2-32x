local out = assert(io.open("/tmp/diag.txt","w"))
local ms = manager.machine.devices[":sega32x:32x_master_sh2"]
local msp = ms.spaces["program"]
local f=0
emu.register_frame_done(function()
  f=f+1
  if f==300 or f==600 then
    -- debug SDRAM at 0x06001000 (SH-2 cached) / 0x26001000 (through)
    local d0=msp:read_u16(0x26001000)
    local d1=msp:read_u16(0x26001002)
    local d2=msp:read_u16(0x26001004)
    local d3=msp:read_u16(0x26001006)
    local cnz=0
    for i=0,255 do if msp:read_u16(0x20004200+i*2)~=0 then cnz=cnz+1 end end
    out:write(string.format("f=%d dbg c0=%04x comm2=%04x conv=%04x loopcount=%04x CRAM_nz=%d\n",
      f, d0,d1,d2,d3,cnz))
    out:flush()
  end
end)
