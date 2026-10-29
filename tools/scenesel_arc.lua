-- LOOP-DECOMPILE (session 2). OPEN item 1: "scene 3 will not load under
-- SCENESEL". Entry 66 read 0xFFF142 at FOUR frames of one 32X build and
-- concluded the scene never loads. Four samples cannot prove an absence,
-- and the 32X port is a variable the question does not need.
--
-- This runs the ARCADE (the oracle) with the same eight-byte patch applied
-- to the 68K rom REGION at 0x1CDA, and samples 0xFFF142 EVERY frame.
-- It reports every distinct value with the frame it first appeared.
--
--   SS_N=3 SS_OUT=/tmp/x.txt mame altbeast -rompath ./mame -skip_gameinfo \
--       -video none -sound none -nothrottle -window -resolution 160x120 \
--       -keyboardprovider none -nomouse -nojoystick -bench 80 \
--       -autoboot_script tools/scenesel_arc.lua
local N   = tonumber(os.getenv('SS_N') or '3')
local LAST= tonumber(os.getenv('SS_LAST') or '3600')
local out = assert(io.open(os.getenv('SS_OUT') or '/tmp/scenesel.txt','w'))
local f=0; local init=false; local md=nil
local prev142=-1; local prev14e=-1
local seen={}

emu.register_frame_done(function()
  f=f+1
  if not init then
    init=true
    md = manager.machine.devices[':maincpu'].spaces['program']
    local rg = manager.machine.memory.regions[':maincpu']
    local before={}
    for i=0,7 do before[i+1]=rg:read_u8(0x1CDA+i) end
    out:write(string.format('region 0x1CDA before = %d %d %d %d %d %d %d %d\n',
      before[1],before[2],before[3],before[4],before[5],before[6],before[7],before[8]))
    for i=0,7 do rg:write_u8(0x1CDA+i, N) end
    local after={}
    for i=0,7 do after[i+1]=rg:read_u8(0x1CDA+i) end
    out:write(string.format('region 0x1CDA after  = %d %d %d %d %d %d %d %d  (SS_N=%d)\n',
      after[1],after[2],after[3],after[4],after[5],after[6],after[7],after[8],N))
    out:flush()
  end
  local v142 = md:read_u8(0xFFF142)
  local v14e = md:read_u8(0xFFF14E)
  if not seen[v142] then seen[v142]=f end
  if v142~=prev142 or v14e~=prev14e then
    out:write(string.format('f%-6d f142=%d f14e=%d f026=%02x\n',
      f, v142, v14e, md:read_u8(0xFFF026)))
    prev142=v142; prev14e=v14e
  end
  if f==LAST then
    out:write('-- distinct f142 values (value: first frame) --\n')
    for k=0,255 do if seen[k] then out:write(string.format('  %d: f%d\n',k,seen[k])) end end
    out:close()
    manager.machine:exit()
  end
end)
