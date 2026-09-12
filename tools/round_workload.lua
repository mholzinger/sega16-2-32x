-- What each ROUND asks the game to do, measured on the arcade.
--
-- LOOP29 198 ruled out the background for level 4's slowdown and put the
-- cause on the sprite or game-logic side. This counts the work the game
-- itself creates, per round, with the same input script each time:
--
--   objects   64 slots of 128 bytes at 0xFFC000, bit 7 of byte 0 = active
--   sprites   LIVE records in sprite RAM at 0x440000, 128 x 16 bytes.
--             jts16_obj_scan.v:83-85 and 98-99: word 0 low byte is top,
--             high byte is bottom, and `badobj = top >= bottom`. So a
--             record draws only when top < bottom, and that is the test
--             the hardware itself applies. Counting non-zero order-list
--             bytes instead gives 255 of 256 in every round, which is the
--             stale contents of a list nobody clears.
--   collision slots 48-61, the collision group (LOOP-DECOMPILE 28)
--
-- RW_N picks the starting ROUND by rewriting the DIP round table at
-- 0x1848 (0x64E reads it with (0xFFF031 & 0x18) >> 3), which is how you
-- reach a later level without playing to it.
local N   = tonumber(os.getenv('RW_N') or '0')
local out = assert(io.open(os.getenv('RW_OUT'), 'w'))
local f=0; local md=nil; local fields={}
local nobj,nspr,ncol,samples = 0,0,0,0
local maxobj,maxspr = 0,0
-- A software renderer pays for AREA, not for sprite count. Height comes
-- straight from the record (bottom - top); width comes from the pitch
-- word, which jts16_obj_scan reads at offset 2. Zoom is object $4E
-- (LOOP-DECOMPILE 48), 0-31, and a zoomed sprite costs a scaler pass.
local nlines,maxlines,nzoom,zoomed = 0,0,0,0

emu.register_frame_done(function()
  f=f+1
  if md==nil then
    md=manager.machine.devices[':maincpu'].spaces['program']
    local rg=manager.machine.memory.regions[':maincpu']
    for i=0,7 do rg:write_u8(0x1848+i, N) end
    for _,p in pairs(manager.machine.ioport.ports) do
      for n,fl in pairs(p.fields) do fields[n]=fl end end
  end
  local function set(n,v) local x=fields[n]; if x then x:set_value(v) end end
  if f==600 or f==800 then set('P1 Start',1); set('P1 A',1) end
  if f==640 or f==840 then set('P1 Start',0); set('P1 A',0) end
  if f==1000 then set('P1 Start',1) end
  if f==1040 then set('P1 Start',0) end
  if f>1200 then
    local q=f%240
    set('P1 Right',(q<120) and 1 or 0)
    set('P1 A',(q%40<8) and 1 or 0)
  end
  if f>=1500 and f<=4500 and f%5==0 then
    local o,c=0,0
    for s=0,63 do
      if md:read_u8(0xFFC000+s*128) >= 0x80 then
        o=o+1
        if s>=48 and s<=61 then c=c+1 end
      end
    end
    local sp,lines=0,0
    for i=0,127 do
      local w=md:read_u16(0x440000+i*16)
      local top, bot = (w & 0xFF), (w >> 8)
      if top < bot then sp=sp+1; lines=lines+(bot-top) end
    end
    nlines=nlines+lines
    if lines>maxlines then maxlines=lines end
    for s2=0,63 do
      if md:read_u8(0xFFC000+s2*128) >= 0x80 then
        local z = md:read_u8(0xFFC000+s2*128+0x4E) & 31
        nzoom=nzoom+z
        if z ~= 0 then zoomed=zoomed+1 end
      end
    end
    nobj=nobj+o; nspr=nspr+sp; ncol=ncol+c; samples=samples+1
    if o>maxobj then maxobj=o end
    if sp>maxspr then maxspr=sp end
  end
  if f==4600 then
    out:write(string.format(
      'round=%d objs=%.1f/%d sprites=%.1f/%d lines_mean=%.0f lines_max=%d '..
      'zoom_sum=%.1f zoomed_objs=%.2f\n',
      N, nobj/samples, maxobj, nspr/samples, maxspr,
      nlines/samples, maxlines, nzoom/samples, zoomed/samples))
    out:close(); manager.machine:exit()
  end
end)
