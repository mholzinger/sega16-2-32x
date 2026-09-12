-- Where the 68000 spends its time, per ROUND, on the arcade.
--
-- LOOP-DECOMPILE 86 counted what each round asks for and found level 4
-- below average on every count. A count is not a cost, so this traces
-- every executed instruction for a fixed window and histograms the PC.
--
-- Sampling was tried first and does not work: emu.register_periodic fires
-- ONCE PER FRAME, always at the same point, so its "profile" is one
-- address. MAME's own trace is exact and cheap enough for half a second.
--
--   RP_N=<round> RP_OUT=trace.txt mame altbeast ... -debug -debugger none
local N     = tonumber(os.getenv('RP_N') or '0')
local START = tonumber(os.getenv('RP_START') or '2000')
local LEN   = tonumber(os.getenv('RP_LEN') or '60')
local OUT   = os.getenv('RP_OUT') or '/tmp/trace.txt'
local f=0; local md=nil; local fields={}; local tracing=false

emu.register_frame_done(function()
  f=f+1
  if md==nil then
    md=manager.machine.devices[':maincpu'].spaces['program']
    local rg=manager.machine.memory.regions[':maincpu']
    for i=0,7 do rg:write_u8(0x1848+i, N) end
    for _,p in pairs(manager.machine.ioport.ports) do
      for n,fl in pairs(p.fields) do fields[n]=fl end end
    manager.machine.debugger.execution_state='run'
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
  if f==START then
    manager.machine.debugger:command('trace '..OUT..',maincpu')
    tracing=true
    print(string.format('round_profile: round=%d f142=%d f14e=%d trace on at f%d',
      N, md:read_u8(0xFFF142), md:read_u8(0xFFF14E), f))
  end
  if f==START+LEN and tracing then
    manager.machine.debugger:command('trace off,maincpu')
    print('round_profile: trace off at f'..f)
    manager.machine:exit()
  end
end)
