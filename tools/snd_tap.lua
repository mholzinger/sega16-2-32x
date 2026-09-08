-- SOUND ENGINE TAP (SOUND.md P1/P4): log the arcade sound board's whole
-- I/O life from mame altbeast — every Z80 sound-CPU io write (YM2151 at
-- ports 00/01, uPD7759 control/bank at 40, uPD7759 data at 80) and every
-- sound-command latch read (mem E800 / io C0). The log is the oracle:
-- tools/upd7759_decode.py rebuilds the speech samples from the port-80
-- stream, and the port-00/01 stream is the OPM register log the P4
-- transcoder consumes.
--   ST_OUT=file mame altbeast -rompath ./roms -skip_gameinfo \
--     -video none -sound none -nothrottle -window -resolution 160x120 \
--     -keyboardprovider none -nomouse -nojoystick -bench 200 \
--     -autoboot_script tools/snd_tap.lua
-- Lines: <machine-time> <kind> <hexval>   kind: Y0/Y1 (YM addr/data),
-- PC (7759 ctrl), PD (7759 data), CM (latch read = command byte).
local out = assert(io.open(os.getenv("ST_OUT") or "/tmp/snd_tap.log", "w"))
local mac = manager.machine

local function stamp(kind, val)
  -- as_double is a METHOD (t:as_double()); the property form returns the
  -- function object, string.format errors, and MAME swallows callback
  -- errors SILENTLY — the tap just stops logging. Measured, not read.
  out:write(string.format("%.7f %s %02X\n", mac.time:as_double(), kind, val))
end

-- Taps are installed on the FIRST frame callback, not at script load:
-- installing from the autoboot script's top level segfaulted MAME 0.288
-- (exit 139, no output) — the memory system is not ready that early.
local taps = {}
local function install_taps()
  local z80 = mac.devices[":soundcpu"]
  local io_sp = z80.spaces["io"]
  local pr_sp = z80.spaces["program"]
  -- io write tap: partial decode on A7:A6 exactly like the board
  --   (jts16b_snd.v:100-109 / segas16b.cpp sound_portmap mirrors)
  taps[#taps+1] = io_sp:install_write_tap(0x00, 0xFF, "snd_wtap",
    function(offset, data, mask)
      local hi = offset & 0xC0
      if hi == 0x00 then
        stamp((offset & 1) == 0 and "Y0" or "Y1", data & 0xFF)
      elseif hi == 0x40 then
        stamp("PC", data & 0xFF)
      elseif hi == 0x80 then
        stamp("PD", data & 0xFF)
      end
    end)
  -- latch reads: memory E800 and io C0 mirrors both ack the command
  taps[#taps+1] = pr_sp:install_read_tap(0xE800, 0xE800, "snd_rtap_m",
    function(offset, data, mask)
      stamp("CM", data & 0xFF)
    end)
  taps[#taps+1] = io_sp:install_read_tap(0xC0, 0xFF, "snd_rtap_i",
    function(offset, data, mask)
      stamp("CM", data & 0xFF)
    end)
end

local f = 0
emu.register_frame_done(function()
  f = f + 1
  if f == 1 then install_taps() end
  if f % 600 == 0 then out:flush() end
  if f >= tonumber(os.getenv("ST_FRAMES") or "10800") then
    out:close()
    manager.machine:exit()
  end
end)
