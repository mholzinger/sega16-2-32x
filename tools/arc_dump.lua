-- Arcade-side truth dump at the eyehold anchor (same terms as
-- parity_cap.lua): tile RAM 16 pages + text RAM.
--   AD_OUT=dir mame altbeast -rompath ./mame ... \
--     -autoboot_script tools/arc_dump.lua
local dir = os.getenv("AD_OUT") or "/tmp"
local f, mem, armed = 0, nil, nil
emu.register_frame_done(function()
  f = f + 1
  if f < 60 then return end
  if not mem then
    for _, c in pairs(manager.machine.devices) do
      if c.tag == ':maincpu' then mem = c.spaces['program'] end
    end
    if not mem then return end
  end
  if armed then
    armed = armed - 1
    if armed > 0 then return end
  else
    -- eyehold: state 0x10 + the two sub-terms, then settle 20 (parity_cap)
    if mem:read_u8(0xFFF031) ~= 0x10 then return end
    if mem:read_u8(0xFFC0A1) ~= 0x09 then return end
    if (mem:read_u16(0xFFC0A2) & 0xFFF0) ~= 0x0040 then return end
    armed = 20
    return
  end
  local fh = assert(io.open(dir .. "/arc_tmap.bin", "wb"))
  local t = {}
  for i = 0, 0x10FFF do t[#t + 1] = string.char(mem:read_u8(0x400000 + i)) end
  fh:write(table.concat(t))
  fh:close()
  manager.machine:exit()
end)
