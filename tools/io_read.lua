-- read bytes from the 68K address space at one frame (IO/DIP ports included)
--   IR_FRAME=30 IR_ADDRS=0xC42001,0xC42003,0xC41001 mame altbeast ... -autoboot_script tools/io_read.lua
local FR = tonumber(os.getenv('IR_FRAME') or '30'); local frames = 0
local addrs = {}
for s in string.gmatch(os.getenv('IR_ADDRS') or '0xC42001', '[^,]+') do addrs[#addrs+1] = tonumber(s) end
emu.register_frame_done(function()
    frames = frames + 1
    if frames == FR then
        local sp = manager.machine.devices[':maincpu'].spaces['program']
        local out = {}
        for _, a in ipairs(addrs) do out[#out+1] = string.format('%06X=%02X', a, sp:read_u8(a)) end
        print('IOREAD ' .. table.concat(out, ' ')); manager.machine:exit()
    end
end)
