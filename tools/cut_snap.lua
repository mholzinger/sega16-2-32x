-- CUT_BLANK verification: snapshot the title-cut draw-in window and
-- print the 0x28FA0 scrap counters ([0] blanked cells, [1] cut arms).
--   CS_DIR=outdir mame 32x -cart <rom> -rompath ./mame -skip_gameinfo \
--     -video soft -sound none -nothrottle \
--     -autoboot_script tools/cut_snap.lua
-- Frames 28..64 step 4 cover the f32 claim storm (cut_profile 2026-08-15).
local dir = os.getenv("CS_DIR") or "/tmp"
local ms  = manager.machine.devices[":sega32x:32x_master_sh2"]
local msp = ms.spaces["program"]
local out = assert(io.open("/tmp/cut_snap.txt", "w"))
local f = 0
emu.register_frame_done(function()
  f = f + 1
  if f >= 28 and f <= 64 and f % 4 == 0 then
    for _, screen in pairs(manager.machine.screens) do
      screen:snapshot(string.format('%s/f%03d.png', dir, f))
    end
    out:write(string.format("f=%d blanked=%d arms=%d\n", f,
      msp:read_u32(0x06028FA0), msp:read_u32(0x06028FA4)))
  end
  if f == 120 or f == 240 or f == 420 then
    out:write(string.format("f=%d blanked=%d arms=%d\n", f,
      msp:read_u32(0x06028FA0), msp:read_u32(0x06028FA4)))
    out:flush()
  end
  if f >= 421 then out:flush() manager.machine:exit() end
end)
