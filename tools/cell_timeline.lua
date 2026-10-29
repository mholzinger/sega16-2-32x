-- ARCADE CELL TIMELINE: print frame + up to 4 work-RAM bytes whenever any changes.
--   CT_ADDRS=0xFFF148,0xFFF149 CT_TO=600 mame altbeast ... -autoboot_script tools/cell_timeline.lua
local TO = tonumber(os.getenv('CT_TO') or '2000')
local addrs = {}
for s in string.gmatch(os.getenv('CT_ADDRS') or '0xFFF031', '[^,]+') do addrs[#addrs+1] = tonumber(s) end
local frames, last = 0, ''
emu.register_frame_done(function()
    frames = frames + 1
    local sp = manager.machine.devices[':maincpu'].spaces['program']
    local v = {}
    for i, a in ipairs(addrs) do v[i] = string.format('%02X', sp:read_u8(a)) end
    local line = table.concat(v, ' ')
    if line ~= last or os.getenv('CT_ALL') then print(string.format('CELL %d %s', frames, line)); last = line end
    if frames >= TO then print('CELL-TIMELINE DONE'); manager.machine:exit() end
end)
