-- 32X sndtest audio capture for level matching (KIT). Boots the sound
-- test (which starts on cmd 0x94 — set the command with RW_UP/RW_RT
-- presses if needed), presses A to post it, lets MAME's -wavwrite record
-- the YM2612 + PSG mix for RW_SECS seconds, exits.
--   RW_SECS=60 mame 32x -cart rom/sndtest.32x -rompath ./mame \
--     -skip_gameinfo -video none -sound none -nothrottle -window \
--     -resolution 160x120 -keyboardprovider none -nomouse -nojoystick \
--     -seconds_to_run 75 -wavwrite ours_94.wav -autoboot_script tools/rom_wav.lua
-- RW_UP / RW_RT = number of UP / RIGHT presses before A (cmd +1 / +0x10).
_G.gen = (_G.gen or 0) + 1
local mygen = _G.gen
local SECS  = tonumber(os.getenv("RW_SECS") or "60")
local NUP   = tonumber(os.getenv("RW_UP") or "0")
local NRT   = tonumber(os.getenv("RW_RT") or "0")
local PRESS = 240                        -- frame of the A press (after boot)
local mac = manager.machine
local f = 0
local pad, fa, fu, fr
emu.register_frame_done(function()
  if _G.gen ~= mygen then return end
  f = f + 1
  if f == 1 then
    pad = mac.ioport.ports[":ctrl1:mdpad:PAD"]
    fa = pad.fields["P1 A"]; fu = pad.fields["P1 Up"]; fr = pad.fields["P1 Right"]
  end
  -- navigation: each press = 6 frames down, 6 up, starting at frame 120
  local nav = f - 120
  if nav >= 0 and nav < (NUP + NRT) * 12 then
    local i = math.floor(nav / 12); local down = (nav % 12) < 6
    local fld = (i < NUP) and fu or fr
    fld:set_value(down and 1 or 0)
  end
  if f == PRESS then fa:set_value(1) end
  if f == PRESS + 8 then fa:set_value(0) end
  -- No scripted exit: mac:exit() from the 32x driver left the WAV
  -- header unfinalized ("not a WAVE file"). End the session with
  -- -seconds_to_run instead, which closes -wavwrite cleanly.
end)
