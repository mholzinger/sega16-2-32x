-- P4 command sweep v5: per-command soft-reset isolation (v3 state-file
-- protocol) + READ-GAG on the sound latch — unauthorized command reads
-- return 0x01 (proven-silent no-op), so the attract stays mute however
-- long the window runs. Boot reads (<1s) pass through so the driver
-- inits normally; the injected byte passes exactly once.
_G.gen = (_G.gen or 0) + 1
local mygen = _G.gen
local sdir = os.getenv("SB_DIR")
local sf = io.open(sdir .. "/sweep_state.txt", "r")
local cmd = 1
if sf then cmd = tonumber(sf:read("*l")) or 1; sf:close() end
if cmd > 255 then manager.machine:exit() return end
local out = assert(io.open(os.getenv("ST_OUT"), "a"))
local mac = manager.machine
local function stamp(kind, val)
  out:write(string.format("%.7f %s %02X\n", mac.time:as_double(), kind, val))
end
local mem
local f = 0
emu.register_frame_done(function()
  if _G.gen ~= mygen then return end
  f = f + 1
  if f == 1 then
    mem = mac.devices[":maincpu"].spaces["program"]
    local z80 = mac.devices[":soundcpu"]
    local function gagfun(o, d, m)
      if _G.gen ~= mygen then return end
      if f < 60 then return end            -- boot commands pass
      if _G.allowed and (d & 0xFF) == _G.allowed then
        _G.allowed = nil                   -- injected byte passes once
        return
      end
      return 0x01
    end
    _G.g1 = z80.spaces["io"]:install_read_tap(0xC0, 0xFF, "g1" .. mygen, gagfun)
    _G.g2 = z80.spaces["program"]:install_read_tap(0xE800, 0xE800,
      "g2" .. mygen, gagfun)
    _G.t1 = z80.spaces["io"]:install_write_tap(0x00, 0xFF, "wt" .. mygen,
      function(o, d, m)
        if _G.gen ~= mygen then return end
        local hi = o & 0xC0
        if hi == 0x00 then stamp((o & 1) == 0 and "Y0" or "Y1", d & 0xFF)
        elseif hi == 0x40 then stamp("PC", d & 0xFF)
        elseif hi == 0x80 then stamp("PD", d & 0xFF) end
      end)
  end
  if f == 200 then
    stamp("IJ", cmd)
    _G.allowed = cmd
    mem:write_u8(0xFE0007, cmd)
  end
  if f == 1400 then
    stamp("IX", 0)
    out:close()
    local w = assert(io.open(sdir .. "/sweep_state.txt", "w"))
    w:write(tostring(cmd + 1) .. "\n")
    w:close()
    mac:soft_reset()
  end
end)

-- ---------------------------------------------------------------------
-- USAGE (from the repo root; ~40 min wall for a full 255-command map):
--   rm -f sweep.log sweep_state.txt
--   ST_OUT=sweep.log SB_DIR=. mame altbeast -rompath ./roms \
--     -skip_gameinfo -video none -sound none -nothrottle -window \
--     -resolution 160x120 -keyboardprovider none -nomouse -nojoystick \
--     -bench 7000 -autoboot_script tools/cmd_sweep.lua
--   python3 tools/soundmap_build.py sweep.log
--
-- LAWS THIS FILE ENCODES (each cost a failed sweep to learn):
-- 1. Taps/space lookups must NOT run at autoboot-script load time —
--    top-level install segfaults MAME 0.288. First frame callback only.
-- 2. mac:soft_reset() RE-RUNS the autoboot script and the old
--    callbacks KEEP FIRING — hence the _G.gen generation token and the
--    state file: one script execution = one command.
-- 3. Write taps on directly-mapped work RAM NEVER FIRE (measured: 0
--    hits on the 0xFFF0C4 sound mailbox) — you cannot gag the game by
--    intercepting its RAM writes. Gag at the Z80's LATCH READ instead:
--    io-space read taps fire and their return value replaces the data.
-- 4. The replacement byte must be a proven-silent no-op for the title's
--    driver (altbeast: 0x01). 0x00 is the stop command; 0x8C HALTS the
--    sound CPU outright.
-- 5. attotime:as_double() is a method — the property form returns a
--    function and the callback dies silently.
