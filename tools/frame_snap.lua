-- Snapshot arbitrary frames: FS_FRAMES="100 200 300" FS_DIR=outdir
--   mame 32x -cart <rom> ... -autoboot_script tools/frame_snap.lua
local dir = os.getenv("FS_DIR") or "/tmp"
local want, last = {}, 0
for n in string.gmatch(os.getenv("FS_FRAMES") or "", "%d+") do
  local v = tonumber(n)
  want[v] = true
  if v > last then last = v end
end
local f = 0
emu.register_frame_done(function()
  f = f + 1
  if want[f] then
    for _, screen in pairs(manager.machine.screens) do
      screen:snapshot(string.format('%s/f%05d.png', dir, f))
    end
  end
  if f > last then manager.machine:exit() end
end)
