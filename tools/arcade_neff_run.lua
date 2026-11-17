-- arcade_neff_run.lua: tools/arcade_level_run.lua's driver (six coins + START, walk/punch/kick,
-- lives held at 3, boss cadence at 0xFFF132 >= 0x18A0) plus a per-frame dump of the sprite
-- RAM (0x440000, 64 records x 16 bytes) while in the boss arena, snapshots every 30 frames
-- there, and exit 240 frames after the boss dies (0xFFF142 0 -> 1). Output in $OUT:
-- log.txt, spr.bin (1 KB a frame, framed by arena_frames.txt), snap/fN.png.
local OUT=os.getenv('OUT') or '/tmp'
local frames=0; local fields={}; local inited=false; local out=io.open(OUT..'/log.txt','w')
local spr=io.open(OUT..'/spr.bin','wb'); local af=io.open(OUT..'/arena_frames.txt','w')
local last142=-1; local arena=false; local died=-1
local inp=io.open(OUT..'/inputs.csv','w'); local cur={}
local function set(n,v) if fields[n] then fields[n]:set_value(v) end if cur[n]~=v then cur[n]=v; inp:write(string.format('%d,%s,%d\n',frames,n,v)) end end
emu.register_frame_done(function()
  if not inited then for _,p in pairs(manager.machine.ioport.ports) do for n,f in pairs(p.fields) do fields[n]=f end end inited=true end
  frames=frames+1
  local m=manager.machine.devices[':maincpu'].spaces['program']
  for _,c in ipairs({500,540,580,620,660,700}) do if frames==c then set('Coin 1',1) end if frames==c+12 then set('Coin 1',0) end end
  if frames==901 then set('1 Player Start',1) end
  if frames==941 then set('1 Player Start',0) end
  if frames>1500 then
    local k=frames%16
    if m:read_u16(0xFFF132)>=0x18a0 then
      local q=frames%40
      set('P1 Right',1)
      set('P1 Button 3',0)
      set('P1 Button 2',0)
      set('P1 Button 1',(q%10<4) and 1 or 0)
    else
      set('P1 Button 3',0)
      set('P1 Right', ((frames//400)%4)~=3 and 1 or 0)
      set('P1 Button 1', (k<4) and 1 or 0)
      set('P1 Button 2', (k>=8 and k<12) and 1 or 0)
    end
    if m:read_u8(0xFFE018)<3 then m:write_u8(0xFFE018,3) end
    if (m:read_u8(0xFFF028)&0x10)~=0 and frames%60==0 then set('1 Player Start',1) elseif frames%60==30 then set('1 Player Start',0) end
  end
  local l=m:read_u8(0xFFF142)
  if l~=last142 then out:write(string.format('%d level %d f026 %04x\n',frames,l,m:read_u16(0xFFF026))); out:flush(); if last142==0 and l==1 then died=frames end; last142=l end
  local inarena = frames>1500 and m:read_u16(0xFFF132)>=0x1800
  if inarena then
    if not arena then arena=true; out:write(string.format('%d arena enter\n',frames)); out:flush() end
    local b={}
    for i=0,1023 do b[#b+1]=string.char(m:read_u8(0x440000+i)) end
    spr:write(table.concat(b)); af:write(frames..'\n')
    if frames%30==0 then manager.machine.screens[':screen']:snapshot(OUT..'/snap/f'..frames..'.png') end
  end
  if frames%300==0 then out:write(string.format('%d tick %04x f132 %04x lv %d f026 %04x pl0 %04x lives %d\n',frames,m:read_u16(0xFFF02A),m:read_u16(0xFFF132),l,m:read_u16(0xFFF026),m:read_u16(0xFFC000),m:read_u8(0xFFE018))); out:flush() end
  if (died>0 and frames>=died+240) or frames>=60000 then out:close(); spr:close(); af:close(); inp:close(); manager.machine:exit() end
end)
