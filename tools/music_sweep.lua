-- Full-length music capture from mame altbeast (the arcade ORACLE),
-- UNINTERRUPTED. Injects one music command, then GAGS the sound-command
-- latch to 0x80 (a driver no-op — NOT 0x01 which is volume-1, the fade;
-- docs/sound/SOUND_DRIVER.md THE FADE LAW) so the game's attract logic can't stop
-- or change the song. The song then plays and LOOPS on its own; the
-- capture holds the true song, not ~14s + attract contamination.
-- Per-command soft-reset isolation (cmd_sweep v3 protocol).
--   ST_OUT=music.log SB_DIR=. mame altbeast -rompath ./mame -skip_gameinfo \
--     -video none -sound none -nothrottle -window -resolution 160x120 \
--     -keyboardprovider none -nomouse -nojoystick -bench 600 \
--     -autoboot_script tools/music_sweep.lua
_G.gen = (_G.gen or 0) + 1
local mygen = _G.gen
local CMDS = { 0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0xB1,0xB2,0xD3 }
local WIN = 5400                         -- ~90s per song
local sdir = os.getenv("SB_DIR") or "."
local sf = io.open(sdir .. "/music_state.txt", "r")
local idx = 1
if sf then idx = tonumber(sf:read("*l")) or 1; sf:close() end
if idx > #CMDS then manager.machine:exit() return end
local out = assert(io.open(os.getenv("ST_OUT"), "a"))
local mac = manager.machine
local function stamp(kind, val)
  out:write(string.format("%.7f %s %02X\n", mac.time:as_double(), kind, val))
end
local f = 0
emu.register_frame_done(function()
  if _G.gen ~= mygen then return end
  f = f + 1
  if f == 1 then
    _G.msw_mem = mac.devices[":maincpu"].spaces["program"]
    local z80 = mac.devices[":soundcpu"]
    -- gag: after the injected command lands, every sound-latch read
    -- returns 0x80 so nothing else (attract stop, scene change) reaches
    -- the driver. Boot reads (<190) and the one injected read pass.
    local function gag(o, d, m)
      if _G.gen ~= mygen then return end
      if f < 190 then return end
      if _G.msw_allow then _G.msw_allow = false; return end
      return 0x80
    end
    _G.msw_g1 = z80.spaces["io"]:install_read_tap(0xC0, 0xFF, "mg1" .. mygen, gag)
    _G.msw_g2 = z80.spaces["program"]:install_read_tap(0xE800, 0xE800,
      "mg2" .. mygen, gag)
    _G.msw_t = z80.spaces["io"]:install_write_tap(0x00, 0xFF, "mt" .. mygen,
      function(o, d, m)
        if _G.gen ~= mygen then return end
        if f < 190 then return end
        local hi = o & 0xC0
        if hi == 0x00 then stamp((o & 1) == 0 and "Y0" or "Y1", d & 0xFF)
        elseif hi == 0x40 then stamp("PC", d & 0xFF)
        elseif hi == 0x80 then stamp("PD", d & 0xFF) end
      end)
  end
  if f == 200 then
    stamp("IJ", CMDS[idx])
    _G.msw_allow = true                 -- let this one command through
    _G.msw_mem:write_u8(0xFE0007, CMDS[idx])
  end
  if f == 200 + WIN then
    stamp("IX", 0)
    out:close()
    local w = assert(io.open(sdir .. "/music_state.txt", "w"))
    w:write(tostring(idx + 1) .. "\n"); w:close()
    mac:soft_reset()
  end
end)
