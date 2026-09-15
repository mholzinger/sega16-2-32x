-- PAGE-SELECT CENSUS (LOOP-DECOMPILE 154 / NOTES 94).
-- Which (BG page, FG page) pairs does the game actually select, per round?
-- bake_cat1vis.py hardcodes ONE pairing (BG 0 / FG 7); this says which ones
-- are real, so the occlusion bake can be parameterised over the truth.
--
-- The page-select registers are S16 text words 0x740 (which=0) and 0x741
-- (which=1), i.e. bytes 0x410E80 / 0x410E82, four 4-bit quadrant pages each
-- (upper-left = bits 0-3, upper-right 4-7, lower-left 8-11, lower-right
-- 12-15, per segaic16 draw_virtual_tilemap and m_main.c decode_pages).
--
-- POLLS, never taps: MAME write/read taps return zero on S16B text RAM
-- (0x410000) and palette RAM. Polling is the only honest reader here.
--
--   mame altbeast -rompath ./mame -skip_gameinfo -video none -sound none \
--     -nothrottle -window -resolution 160x120 -keyboardprovider none \
--     -nomouse -nojoystick -bench 100 -autoboot_script tools/arcade_pagesel.lua
local f=0; local md=nil; local seen={}; local order={}
local out=assert(io.open(os.getenv('PS_OUT') or '/tmp/pagesel.txt','w'))

emu.register_frame_done(function()
  f=f+1
  if not md then md=manager.machine.devices[':maincpu'].spaces['program'] end
  if f<120 then return end
  local w0 = md:read_u16(0x410E80)      -- which=0
  local w1 = md:read_u16(0x410E82)      -- which=1
  local rnd  = md:read_u8(0xFFF142)
  local prog = md:read_u8(0xFFF14E)
  local step = md:read_u8(0xFFF031)
  -- every quadrant of each plane, so a per-quadrant pairing is visible
  for q=0,3 do
    local p0 = (w0 >> (q*4)) & 0xF
    local p1 = (w1 >> (q*4)) & 0xF
    local k = string.format('%d|%d|%d|%d', rnd, q, p0, p1)
    if not seen[k] then
      seen[k] = {n=0, first=f, step=step, prog=prog}
      order[#order+1] = k
    end
    seen[k].n = seen[k].n + 1
  end
  if f >= 5400 then
    out:write('# round quad which0 which1 frames firstframe attractstep progress\n')
    for _,k in ipairs(order) do
      local r,q,a,b = k:match('(%d+)|(%d+)|(%d+)|(%d+)')
      local v = seen[k]
      out:write(string.format('%3d %d  %2d %2d  %6d %7d  0x%02X %3d\n',
        tonumber(r), tonumber(q), tonumber(a), tonumber(b),
        v.n, v.first, v.step, v.prog))
    end
    out:close()
    manager.machine:exit()
  end
end)
