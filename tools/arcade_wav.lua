-- Arcade audio capture for level matching (KIT). Injects ONE sound
-- command into the running altbeast driver and gags the latch so
-- attract mode cannot stop it (docs/sound/SOUND_DRIVER.md ATTRACT-CONTAMINATION),
-- then lets MAME's -wavwrite record the real YM2151 + uPD7759 mix.
--   AW_CMD=94 AW_SECS=60 mame altbeast -rompath ./mame -skip_gameinfo \
--     -video none -sound none -nothrottle -window -resolution 160x120 \
--     -keyboardprovider none -nomouse -nojoystick -seconds_to_run 70 \
--     -wavwrite arcade_94.wav -autoboot_script tools/arcade_wav.lua
-- (-sound none still feeds -wavwrite: the mixer runs, only the output
-- module is null. Verified 2026-09-02.)
_G.gen = (_G.gen or 0) + 1
local mygen = _G.gen
local CMD  = tonumber(os.getenv("AW_CMD") or "94", 16)
local SECS = tonumber(os.getenv("AW_SECS") or "60")
local INJECT = 200
local mac = manager.machine
local f = 0
emu.register_frame_done(function()
  if _G.gen ~= mygen then return end
  f = f + 1
  if f == 1 then
    _G.aw_mem = mac.devices[":maincpu"].spaces["program"]
    local z80 = mac.devices[":soundcpu"]
    local function gag(o, d, m)
      if _G.gen ~= mygen then return end
      if f < INJECT - 10 then return end
      if _G.aw_allow then _G.aw_allow = false; return end
      return 0x80                         -- idle latch = no-op (FADE LAW)
    end
    _G.aw_g1 = z80.spaces["io"]:install_read_tap(0xC0, 0xFF, "awg1" .. mygen, gag)
    _G.aw_g2 = z80.spaces["program"]:install_read_tap(0xE800, 0xE800, "awg2" .. mygen, gag)
  end
  if f == INJECT then
    _G.aw_allow = true
    _G.aw_mem:write_u8(0xFE0007, CMD)
  end
  if f >= INJECT + SECS * 60 then
    mac:exit()
  end
end)
