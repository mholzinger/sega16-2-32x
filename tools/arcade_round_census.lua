-- arcade_round_census.lua: the arcade driver (coins, START, walk/punch/kick cadence, lives held
-- at 3) starting credited play at round ROUND (0..4, the GD_ROUND poke of gameplay_dump.lua),
-- dumping the sprite RAM (0x440000, 1 KB) EVERY frame from START until 600 frames after the
-- level flag advances past ROUND, or CAP frames (default 90000). The no-jump punch cadence in
-- the boss arena (scroll >= 0x18a0); when the scroll has not moved for 60 s, alternating 20 s
-- of jump+kick bursts. Env: OUT (dir: spr.bin, log.txt), ROUND, CAP. Run with fresh
-- -nvram_directory/-cfg_directory and -bench CAP/60+100 (tools/art_census_all.py reads the dumps).
-- 2026-11-04: rounds 0, 2, 3 reach the next level, round 4 reaches the ending, round 1 stalls
-- in its arena with the boss alive (art still sampled).
local OUT=os.getenv('OUT') or '/tmp'
local ROUND=tonumber(os.getenv('ROUND') or '0')
local CAP=tonumber(os.getenv('CAP') or '90000')
local frames=0; local fields={}; local inited=false; local out=io.open(OUT..'/log.txt','w')
local spr=io.open(OUT..'/spr.bin','wb')
local last142=-1; local died=-1; local poked=false; local spr0=-1
local cur={}; local lastsc=-1; local scat=0
local function set(n,v) if fields[n] then fields[n]:set_value(v) end cur[n]=v end
emu.register_frame_done(function()
  if not inited then for _,p in pairs(manager.machine.ioport.ports) do for n,f in pairs(p.fields) do fields[n]=f end end inited=true end
  frames=frames+1
  local m=manager.machine.devices[':maincpu'].spaces['program']
  for _,c in ipairs({500,540,580,620,660,700}) do if frames==c then set('Coin 1',1) end if frames==c+12 then set('Coin 1',0) end end
  if frames==901 then set('1 Player Start',1) end
  if frames==941 then set('1 Player Start',0) end
  if ROUND>0 and not poked and frames>900 and (m:read_u8(0xFFF028)&1)~=0 and m:read_u8(0xFFF026)==0 and m:read_u8(0xFFF14E)==0 then
    m:write_u8(0xFFF14E,ROUND); m:write_u8(0xFFF142,ROUND); poked=true
    out:write(string.format('%d round poke %d\n',frames,ROUND)); out:flush()
  end
  if frames>1500 then
    local k=frames%16
    local sc=m:read_u16(0xFFF132)
    if sc~=lastsc then lastsc=sc; scat=frames end
    local stuck=(frames-scat)>3600 and (((frames-scat)//1200)%2==1)
    if sc>=0x18a0 then
      local q=frames%40
      set('P1 Left',0)
      set('P1 Right',1)
      set('P1 Button 3',(stuck and q<6) and 1 or 0)
      set('P1 Button 2',(stuck and q>=8 and q<14) and 1 or 0)
      set('P1 Button 1',(q%10<4) and 1 or 0)
    elseif stuck then
      local q=frames%48
      set('P1 Right',(q<24) and 1 or 0)
      set('P1 Left',(q>=36) and 1 or 0)
      set('P1 Button 3',(q%24<6) and 1 or 0)
      set('P1 Button 2',(q%24>=8 and q%24<14) and 1 or 0)
      set('P1 Button 1',0)
    else
      set('P1 Left',0)
      set('P1 Button 3',0)
      set('P1 Right', ((frames//400)%4)~=3 and 1 or 0)
      set('P1 Button 1', (k<4) and 1 or 0)
      set('P1 Button 2', (k>=8 and k<12) and 1 or 0)
    end
    if m:read_u8(0xFFE018)<3 then m:write_u8(0xFFE018,3) end
    if (m:read_u8(0xFFF028)&0x10)~=0 and frames%60==0 then set('1 Player Start',1) elseif frames%60==30 then set('1 Player Start',0) end
  end
  local l=m:read_u8(0xFFF142)
  if l~=last142 then out:write(string.format('%d level %d round %d f026 %04x\n',frames,l,m:read_u8(0xFFF14E),m:read_u16(0xFFF026))); out:flush(); if last142==ROUND and l==ROUND+1 and frames>2000 then died=frames end; last142=l end
  if frames>=900 then
    if spr0<0 then spr0=frames end
    local b={}
    for i=0,1023 do b[#b+1]=string.char(m:read_u8(0x440000+i)) end
    spr:write(table.concat(b))
  end
  if frames%600==0 then out:write(string.format('%d f132 %04x f152 %02x lv %d rd %d f028 %02x\n',frames,m:read_u16(0xFFF132),m:read_u8(0xFFF152),l,m:read_u8(0xFFF14E),m:read_u8(0xFFF028))); out:flush() end
  if (died>0 and frames>=died+600) or frames>=CAP then out:write(string.format('%d end spr0 %d\n',frames,spr0)); out:close(); spr:close(); manager.machine:exit() end
end)
