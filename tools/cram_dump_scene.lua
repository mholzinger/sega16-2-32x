-- LOOP-DECOMPILE (session 2). Dump the game's LIVE palette (68K work RAM
-- 0xFF9000, 0x1000 bytes) — the input tools/bake_tilecram.py takes as
-- --live — for any scene, from any rom, WITHOUT a build.
--
-- CD_N rewrites the eight-byte round->scene table at 0x1CDA in the cart
-- region at frame 1, which is what SCENESEL=N does at build time. Doing
-- it here removes the stale-rom failure mode that lost scene 3 for a
-- whole session (entry 72).
--
--   three frames four apart, entry 66's recipe:
--     CD_N=3 CD_OUT=discover/cram/scene3 CD_F=1500 mame 32x -cart <rom> ...
--   a wide sample: every CD_EVERY frames from CD_F to CD_TO
--     CD_N=3 CD_OUT=/tmp/s3 CD_F=600 CD_TO=3000 CD_EVERY=30 ...
--
-- Files are <CD_OUT>_a.bin _b.bin _c.bin in three-frame mode, and
-- <CD_OUT>_f<frame>.bin in wide mode.
local N     = tonumber(os.getenv('CD_N') or '-1')
local BASE  = tonumber(os.getenv('CD_F') or '1500')
local TO    = tonumber(os.getenv('CD_TO') or '0')
local EVERY = tonumber(os.getenv('CD_EVERY') or '4')
local OUT   = os.getenv('CD_OUT') or '/tmp/scene'
local f=0; local md=nil; local n=0
local tags={'a','b','c'}

local manifest=nil
local function grab(name)
  local fh=assert(io.open(name,'wb'))
  for a=0,0xFFF do fh:write(string.char(md:read_u8(0xFF9000+a))) end
  fh:close()
  -- a sidecar so a fade or a round change can be filtered out later:
  -- 0xFFF142 is the scene, and bit 5 of 0xFFF018 is the game's own
  -- display-enable shadow (bclr at 0x638 before a load, bset at 0x8FC).
  if manifest==nil then manifest=assert(io.open(OUT..'_manifest.csv','w'))
    manifest:write('file,frame,f142,f018,disp,f031\n') end
  local g=md:read_u8(0xFFF018)
  -- 0xFFF031 is the ATTRACT STEP, and it decides what is on screen.
  -- Measured on the arcade with the same table patch: 0x0C and 0x14 are
  -- the scene backdrop, 0x10 is the eye title, 0x00 the score table.
  -- A dump taken at 0x10 or 0x00 is another screen's palette wearing the
  -- scene's name, and it is what made the wide union overflow.
  manifest:write(string.format('%s,%d,%d,%02x,%d,%02x\n', name, f,
    md:read_u8(0xFFF142), g, ((g & 0x20) ~= 0) and 1 or 0,
    md:read_u8(0xFFF031)))
  manifest:flush()
  n=n+1
end

emu.register_frame_done(function()
  f=f+1
  if md==nil then
    md=manager.machine.devices[':maincpu'].spaces['program']
    if N>=0 then
      local rg=nil
      for _,v in pairs(manager.machine.memory.regions) do
        if v.size>=0x400000 then rg=v end end
      -- the 68K image sits at cart 0x300000 (bank 3 -> 0x900000); an
      -- arcade rom has it at 0x1CDA. Pick by what reads like the table.
      local base = (rg:read_u8(0x301CDA)<=4) and 0x301CDA or 0x1CDA
      for i=0,7 do rg:write_u8(base+i, N) end
      print(string.format('cram_dump: round->scene table at 0x%X set to %d', base, N))
    end
  end
  if TO > 0 then
    if f>=BASE and f<=TO and (f-BASE)%EVERY==0 then
      grab(string.format('%s_f%d.bin', OUT, f))
    end
    if f>TO then
      print(string.format('cram_dump: %d dumps, f142=%d', n, md:read_u8(0xFFF142)))
      manager.machine:exit()
    end
  else
    for i=1,3 do
      if f == BASE + (i-1)*EVERY then
        grab(string.format('%s_%s.bin', OUT, tags[i]))
        print(string.format('cram_dump: %s_%s.bin at f%d (f142=%d)',
          OUT, tags[i], f, md:read_u8(0xFFF142)))
      end
    end
    if n==3 then manager.machine:exit() end
  end
end)
