-- Magic-tail smoke: 3600 frames of attract; print pipeline health +
-- DRQR block (incl. [7] = misaligned packets, must be 0 on MAME).
local out = assert(io.open("/tmp/magic_smoke.txt", "w"))
local ms  = manager.machine.devices[":sega32x:32x_master_sh2"]
local msp = ms.spaces["program"]
local f = 0
emu.register_frame_done(function()
  f = f + 1
  if f % 1200 == 0 then
    local cycles = msp:read_u32(0x06028000 + 9 * 4)
    local skips  = msp:read_u32(0x06028000 + 7 * 4)
    local inc    = msp:read_u32(0x06028000 + 17 * 4)
    local d = {}
    for i = 0, 7 do
      d[#d + 1] = string.format("[%d]=%d", i, msp:read_u32(0x06028F80 + i * 4))
    end
    out:write(string.format("f=%d cycles=%d flipskips=%d dreq_inc=%d DRQR: %s\n",
      f, cycles, skips, inc, table.concat(d, " ")))
    out:flush()
  end
  if f >= 3600 then manager.machine:exit() end
end)
