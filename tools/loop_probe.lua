-- Loop-length probe: play each music command and log the sound driver's
-- per-channel sequence pointers (Z80 RAM F840 + slot*0x28 + 3/4, from
-- docs/sound/SOUND_DRIVER.md) every frame. tools/find_loops.py then finds the exact
-- loop period per song (when the full pointer state repeats) so each
-- track can be captured as ONE complete loop, not a timed window.
--   ST_OUT=loops.log SB_DIR=. mame altbeast -rompath ./mame -skip_gameinfo \
--     -video none -sound none -nothrottle -window -resolution 160x120 \
--     -keyboardprovider none -nomouse -nojoystick -bench 2200 \
--     -autoboot_script tools/loop_probe.lua
_G.gen = (_G.gen or 0) + 1
local mygen = _G.gen
local CMDS = { 0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0xB1,0xB2,0xD3 }
local WIN = 6000                         -- ~100s per song
local sdir = os.getenv("SB_DIR") or "."
local sf = io.open(sdir .. "/loop_state.txt", "r")
local idx = 1
if sf then idx = tonumber(sf:read("*l")) or 1; sf:close() end
if idx > #CMDS then manager.machine:exit() return end
local out = assert(io.open(os.getenv("ST_OUT"), "a"))
local mac = manager.machine
local f = 0
emu.register_frame_done(function()
  if _G.gen ~= mygen then return end
  f = f + 1
  if f == 1 then
    _G.lp_mem  = mac.devices[":maincpu"].spaces["program"]
    local z80  = mac.devices[":soundcpu"]
    _G.lp_z80  = z80.spaces["program"]
    local function gag(o,d,m)
      if _G.gen~=mygen then return end
      if f<190 then return end
      if _G.lp_allow then _G.lp_allow=false; return end
      return 0x80
    end
    _G.lp_g1=z80.spaces["io"]:install_read_tap(0xC0,0xFF,"lg1"..mygen,gag)
    _G.lp_g2=z80.spaces["program"]:install_read_tap(0xE800,0xE800,"lg2"..mygen,gag)
  end
  if f == 200 then
    out:write(string.format("IJ %02X\n", CMDS[idx]))
    _G.lp_allow=true
    _G.lp_mem:write_u8(0xFE0007, CMDS[idx])
  end
  if f > 200 and f <= 200 + WIN then
    -- log 8 channel seq pointers (2 bytes each) as one hex line
    local z = _G.lp_z80
    local parts = {}
    for c = 0, 7 do
      local base = 0xF840 + c * 0x28
      local p = z:read_u8(base + 3) | (z:read_u8(base + 4) << 8)
      parts[#parts+1] = string.format("%04X", p)
    end
    out:write((f - 200) .. " " .. table.concat(parts, ",") .. "\n")
  end
  if f == 200 + WIN then
    out:write("IX\n"); out:close()
    local w = assert(io.open(sdir .. "/loop_state.txt", "w"))
    w:write(tostring(idx + 1) .. "\n"); w:close()
    mac:soft_reset()
  end
end)
