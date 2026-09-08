local every = 1
local screen
for _, s in pairs(manager.machine.screens) do screen = s break end
local n = 0
local last = tonumber(os.getenv('RD_LAST') or '1500')
emu.register_frame_done(function()
    n = n + 1
    screen:snapshot(string.format('ref_%06d.png', n))
    if n >= last then manager.machine:exit() end
end)
