-- LOOP-DECOMPILE (session 2). Dump the game's LIVE palette (68K work RAM
-- 0xFF9000, 0x1000 bytes) at three frames, the input tools/bake_tilecram.py
-- takes as --live. Same recipe entry 66 used for scenes 0/1/2/4.
--
--   CD_OUT=discover/cram/scene3 CD_F=1500 mame 32x -cart <rom> ...
--       -autoboot_script tools/cram_dump_scene.lua
local BASE = tonumber(os.getenv('CD_F') or '1500')
local OUT  = os.getenv('CD_OUT') or '/tmp/scene'
local N    = tonumber(os.getenv('CD_N') or '-1')   -- rewrite the round->scene
local f=0; local md=nil; local n=0                  -- table in the cart region
local tags={'a','b','c'}

emu.register_frame_done(function()
  f=f+1
  if md==nil then
    md=manager.machine.devices[':maincpu'].spaces['program']
    if N>=0 then
      local rg=nil
      for _,v in pairs(manager.machine.memory.regions) do
        if v.size>=0x400000 then rg=v end end
      -- 0x301CDA: the 68K image sits at cart 0x300000 (bank 3 -> 0x900000)
      local base = (rg:read_u8(0x301CDA)<=4) and 0x301CDA or 0x1CDA
      for i=0,7 do rg:write_u8(base+i, N) end
      print(string.format('cram_dump: round->scene table at 0x%X set to %d', base, N))
    end
  end
  for i=1,3 do
    if f == BASE + (i-1)*4 then
      local fh=assert(io.open(OUT..'_'..tags[i]..'.bin','wb'))
      for a=0,0xFFF do fh:write(string.char(md:read_u8(0xFF9000+a))) end
      fh:close()
      print(string.format('cram_dump: %s_%s.bin at f%d (f142=%d)',
        OUT, tags[i], f, md:read_u8(0xFFF142)))
      n=n+1
    end
  end
  if n==3 then manager.machine:exit() end
end)
