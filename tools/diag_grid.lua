-- MDBGALL capture: periodic snapshots + DIAG counter dump, then exit.
local out = assert(io.open("/tmp/mdbg_diag.txt", "w"))
local ms  = manager.machine.devices[":sega32x:32x_master_sh2"]
local msp = ms.spaces["program"]
local f = 0
local LAST = 3600
emu.register_frame_done(function()
  f = f + 1
  if f % 600 == 0 then
    manager.machine.video:snapshot()
    local d = {}
    for _, i in ipairs({9, 35, 36, 37, 38, 39, 50, 51, 53, 57}) do
      d[#d + 1] = string.format("[%d]=%d", i, msp:read_u32(0x06028000 + i * 4))
    end
    out:write(string.format("f=%d %s\n", f, table.concat(d, " ")))
    out:flush()
  end
  if f >= LAST then
    manager.machine:exit()
  end
end)
