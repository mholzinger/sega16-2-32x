local out = assert(io.open("/tmp/shwr.txt","w"))
local ms = manager.machine.devices[":sega32x:32x_master_sh2"]
local msp = ms.spaces["program"]
local n, anyw = 0, 0
msp:install_write_tap(0x20004200, 0x200043FF, "cr", function(o,d,m)
    anyw = anyw + 1
    if n < 12 and d ~= 0 then
        n = n + 1
        out:write(string.format("CRAM write @%x = %04x  (COMM0=%04x COMM2=%04x)\n",
            o, d, msp:read_u16(0x20004020), msp:read_u16(0x20004022)))
        out:flush()
    end
end)
local f=0
emu.register_frame_done(function() f=f+1; if f==600 then
    out:write(string.format("total CRAM writes=%d, master PC=%08x\n", anyw, ms.state["PC"].value)); out:flush() end end)
