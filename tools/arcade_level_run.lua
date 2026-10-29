-- arcade_level_run.lua: drive  through level 1 into level 2, unattended.
-- Six coins + START, then walk/punch/kick; lives held at 3 (0xFFE018); at the boss
-- (0xFFF132 >= 0x18A0) jump, jump-kick and punch while holding right. Measured 2026-10-21:
-- the boss dies and 0xFFF142 goes 0 -> 1 at frame 31807; the crystal-ball scene runs
-- 31684-31808; level 2 is live from 31824. Logs to $OUT/log.txt, snapshots every 120
-- frames to $OUT/snap/. Run with the headless flags in CLAUDE.md, e.g.
--   OUT=/tmp/lv mame altbeast -rompath ./mame -skip_gameinfo -window -resolution 160x120 --     -keyboardprovider none -nomouse -nojoystick -bench 1550 -autoboot_script tools/arcade_level_run.lua
local OUT=os.getenv('OUT') or '/tmp'
local frames=0; local fields={}; local inited=false; local out=io.open(OUT..'/log.txt','w')
local last142=-1; local lastsnap=0
local function set(n,v) if fields[n] then fields[n]:set_value(v) end end
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
      set('P1 Button 3',(q<4) and 1 or 0)
      set('P1 Button 2',(q>=8 and q<12) and 1 or 0)
      set('P1 Button 1',(q>=24 and q<28) and 1 or 0)
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
  if l~=last142 then out:write(string.format('%d level %d f026 %04x\n',frames,l,m:read_u16(0xFFF026))); out:flush(); last142=l end
  if frames%300==0 then out:write(string.format('%d tick %04x f132 %04x lv %d f026 %04x pl0 %04x lives %d\n',frames,m:read_u16(0xFFF02A),m:read_u16(0xFFF132),l,m:read_u16(0xFFF026),m:read_u16(0xFFC000),m:read_u8(0xFFE018))); out:flush() end
  if frames%120==0 then manager.machine.screens[':screen']:snapshot(OUT..'/snap/f'..frames..'.png') end
  if frames>=90000 then out:close(); manager.machine:exit() end
end)