-- Who READS a rom block that nothing points at.
--
-- Two blocks in Altered Beast are reached from a computed base, so no
-- pointer scan can name their consumer (LOOP-DECOMPILE 83). A read tap on
-- the 68K program space records the PC that touches them, which is the
-- consuming instruction itself.
--
--   RT_OUT=file mame altbeast -rompath ./mame -skip_gameinfo ...
--       -autoboot_script tools/rom_reader_tap.lua
-- CONTROL FIRST. A tap that reports nothing is worthless unless a range
-- known to be read reports something, and "no hits" is exactly the shape
-- of a broken rig (LOOP-DECOMPILE 50's absence trap).
local RANGES = {
  {0x22000, 0x232A0, 'tile-index block'},
  {0x29000, 0x29E00, 'between the last upload block and scene 0 tilemap'},
  {0x20000, 0x22000, 'CONTROL: the zoom table, read every sprite'},
  {0x01CE2, 0x01D00, 'CONTROL: the scene descriptor'},
}
local out=assert(io.open(os.getenv('RT_OUT') or '/tmp/tap.txt','w'))
local f=0; local md=nil; local st=nil; local hits={}; local taps={}
emu.register_frame_done(function()
  f=f+1
  if md==nil then
    md=manager.machine.devices[':maincpu'].spaces['program']
    st=manager.machine.devices[':maincpu'].state
    for i,r in ipairs(RANGES) do
      local lo,hi,name = r[1],r[2],r[3]
      local ok,err = pcall(function()
        taps[i]=md:install_read_tap(lo, hi-1, name, function(off, data, mask)
          local pc = st['PC'].value
          local k = string.format('%s|%06X', name, pc)
          local h = hits[k]
          if h then h.n=h.n+1; if off<h.lo then h.lo=off end
                    if off>h.hi then h.hi=off end
          else hits[k]={n=1, lo=off, hi=off, name=name, pc=pc, f=f} end
          return data
        end)
      end)
      if not ok then out:write('tap failed: '..tostring(err)..'\n') end
    end
    out:write(string.format('taps installed: %d\n', #taps))
    out:flush()
  end
  if f==4000 then
    out:write('reader,pc,reads,first_offset,last_offset,first_frame\n')
    for _,h in pairs(hits) do
      out:write(string.format('%s,%06X,%d,%06X,%06X,%d\n',
        h.name, h.pc, h.n, h.lo, h.hi, h.f))
    end
    out:close(); manager.machine:exit()
  end
end)
