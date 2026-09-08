-- Cut profiler (LOOP14 item 2): per-frame time series of the MD tile
-- pipeline around attract scene cuts. Run against the MDBGALL flavor:
--   mame 32x -cart rom/s16_mdbgall_magic.32x -rompath ./mame \
--        -skip_gameinfo -video soft -sound none -nothrottle -str 70 \
--        -autoboot_script tools/cut_profile.lua
-- Output /tmp/cut_profile.txt, one line per emulated frame:
--   f claims sent defer evict hotev tb nc Ventry Vscroll Vtiles Vcells Vcram
-- claims/sent are per-frame deltas of DIAG[53]/DIAG[57] (master SH2);
-- backlog = cumulative claims-sent. tb/nc = MD-side packets consumed
-- (0xFFB0E4/E6). V* = MD beam row probes 0xFFB0B0..B8 (last window's
-- receive-stage timing; V wraps 0xEA + vblank rows).
local out = assert(io.open("/tmp/cut_profile.txt", "w"))
local ms  = manager.machine.devices[":sega32x:32x_master_sh2"]
local msp = ms.spaces["program"]
local md  = manager.machine.devices[":maincpu"]
local mdp = md.spaces["program"]
local f = 0
local p53, p57, p13, p50, p39, ptb, pnc = 0, 0, 0, 0, 0, 0, 0
local function d32(i) return msp:read_u32(0x06028000 + i * 4) end
emu.register_frame_done(function()
  f = f + 1
  local c53, c57 = d32(53), d32(57)
  local c13, c50, c39 = d32(13), d32(50), d32(39)
  local tb = mdp:read_u16(0xFFB0E4)
  local nc = mdp:read_u16(0xFFB0E6)
  out:write(string.format(
    "%d %d %d %d %d %d %d %d %d %d %d %d %d\n",
    f, c53 - p53, c57 - p57, c13 - p13, c50 - p50, c39 - p39,
    tb - ptb, nc - pnc,
    mdp:read_u16(0xFFB0B0), mdp:read_u16(0xFFB0B2),
    mdp:read_u16(0xFFB0B4), mdp:read_u16(0xFFB0B6),
    mdp:read_u16(0xFFB0B8)))
  p53, p57, p13, p50, p39, ptb, pnc = c53, c57, c13, c50, c39, tb, nc
  if f % 600 == 0 then out:flush() end
  if f >= 4200 then out:flush() manager.machine:exit() end
end)
