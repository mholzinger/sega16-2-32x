-- Oracle YM2151 tap: mame altbeast plays ONE music command continuously,
-- tap the real sound driver's YM writes. The reference my offline
-- renderer (tools/snd_render.py) must match (SOUND_DRIVER.md validation).
-- Injects the command once into the 68K->sound mailbox and taps for the
-- whole window (NO per-command reset — this is continuous, unlike
-- cmd_sweep). Env: ST_OUT (log), SB_CMD (hex command, default 94).
--   ST_OUT=/tmp/oracle94.log SB_CMD=94 mame altbeast -rompath ./roms \
--     -skip_gameinfo -video none -sound none -nothrottle -window \
--     -resolution 160x120 -keyboardprovider none -nomouse -nojoystick \
--     -bench 40 -autoboot_script tools/snd_tap94.lua
_G.T94 = { f = 0, out = assert(io.open(os.getenv("ST_OUT"), "w")),
           cmd = tonumber(os.getenv("SB_CMD") or "94", 16) }
local S = _G.T94
local mac = manager.machine
local function stamp(kind, val)
  S.out:write(string.format("%.7f %s %02X\n", mac.time:as_double(), kind, val))
end
_G.T94_sub = emu.register_frame_done(function()
  S.f = S.f + 1
  if S.f == 1 then
    local z80 = mac.devices[":soundcpu"]
    local io_sp = z80.spaces["io"]
    S.mem = mac.devices[":maincpu"].spaces["program"]
    _G.T94_t = io_sp:install_write_tap(0x00, 0xFF, "t94", function(o, d, m)
      local hi = o & 0xC0
      if hi == 0x00 then stamp((o & 1) == 0 and "Y0" or "Y1", d & 0xFF)
      elseif hi == 0x40 then stamp("PC", d & 0xFF)
      elseif hi == 0x80 then stamp("PD", d & 0xFF) end
    end)
  end
  if S.f == 200 then
    stamp("IJ", S.cmd)
    S.mem:write_u8(0xFE0007, S.cmd)      -- 68K sound-command mailbox
  end
  if S.f == 4000 then
    stamp("IX", 0); S.out:close(); mac:exit()
  end
end)
