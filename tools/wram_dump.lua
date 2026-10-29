-- ARCADE WORK-RAM DUMP, no debugger:
--   one frame:   WD_FRAME=500 WD_ADDR=0xFFEC80 WD_LEN=0x100 WD_OUT=/tmp/x.bin
--   many frames: WD_FRAMES=2009,2010,2011 WD_ADDR=0xFFC000 WD_LEN=0x4000 WD_OUTDIR=/dir
--                (writes <WD_OUTDIR>/aw_<frame>.bin for each, one MAME run)
local A0 = tonumber(os.getenv('WD_ADDR') or '0xFFEC80')
local LEN = tonumber(os.getenv('WD_LEN') or '256')
local want = {}
local last = 0
if os.getenv('WD_FRAMES') then
    for f in string.gmatch(os.getenv('WD_FRAMES'), '%d+') do
        local n = tonumber(f); want[n] = (os.getenv('WD_OUTDIR') or '/tmp') .. '/aw_' .. f .. '.bin'
        if n > last then last = n end
    end
else
    last = tonumber(os.getenv('WD_FRAME') or '500'); want[last] = os.getenv('WD_OUT') or '/tmp/wram_dump.bin'
end
local frames = 0
emu.register_frame_done(function()
    frames = frames + 1
    local out = want[frames]
    if out then
        local sp = manager.machine.devices[':maincpu'].spaces['program']
        local f = io.open(out, 'wb')
        for i = 0, LEN - 1 do f:write(string.char(sp:read_u8(A0 + i))) end
        f:close(); print('WRAM-DUMP DONE ' .. out)
    end
    if frames >= last then manager.machine:exit() end
end)
