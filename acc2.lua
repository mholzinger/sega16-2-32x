local out=assert(io.open("/tmp/acc2.txt","w"))
local ms=manager.machine.devices[":sega32x:32x_master_sh2"]
local msp=ms.spaces["program"]
local everset=0
local f=0
emu.register_frame_done(function()
 f=f+1
 -- sample INTMSK access bit many times per frame via the frame hook is coarse;
 -- just report min/max seen at frame boundaries
 local v=msp:read_u16(0x20004000)
 if (v & 0x8000)~=0 then everset=everset+1 end
 if f%120==0 then
   out:write(string.format("f=%d INTMSK=%04x accessGrantedFrames=%d\n", f, v, everset))
   out:flush()
 end
end)
