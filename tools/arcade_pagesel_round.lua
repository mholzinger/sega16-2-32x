-- PAGE-SELECT CENSUS, FORCED ROUND (LOOP-DECOMPILE 155 follow-up).
-- The attract only reaches rounds 0-1, but entry 108 established that
-- writing a round into the DIP round table at 0x1848-0x184F changes WHICH
-- ROUND THE ATTRACT DEMOS SHOW (credited play still starts at round 0).
-- That reaches scenes 2/3/4 with no scripted play and no cheats.
--
-- Same reader as tools/arcade_pagesel.lua: POLLS text words 0x740/0x741.
--   PS_ROUND=3 PS_OUT=/tmp/ps3.txt mame altbeast ... -bench 200 \
--     -autoboot_script tools/arcade_pagesel_round.lua
local want = tonumber(os.getenv('PS_ROUND') or '2')
local f=0; local md=nil; local seen={}; local order={}; local patched=false
local out=assert(io.open(os.getenv('PS_OUT') or '/tmp/pagesel_r.txt','w'))

emu.register_frame_done(function()
  f=f+1
  if not md then md=manager.machine.devices[':maincpu'].spaces['program'] end
  if not patched and f>=60 then
    -- patch the ROM REGION, not the program space: program-space writes to
    -- a ROM area do not stick in MAME.
    local rgn = manager.machine.memory.regions[':maincpu']
    for a=0x1848,0x184F do rgn:write_u8(a, want) end
    patched = true
  end
  if f<180 then return end
  local w0 = md:read_u16(0x410E80)
  local w1 = md:read_u16(0x410E82)
  local rnd  = md:read_u8(0xFFF142)
  local bank = md:read_u8(0xFFF095)
  for q=0,3 do
    local p0 = (w0 >> (q*4)) & 0xF
    local p1 = (w1 >> (q*4)) & 0xF
    local k = string.format('%d|%d|%d|%d|%d', rnd, bank, q, p0, p1)
    if not seen[k] then seen[k]={n=0,first=f}; order[#order+1]=k end
    seen[k].n = seen[k].n + 1
  end
  if f >= 9000 then
    out:write(string.format('# forced round %d\n', want))
    out:write('# round bank quad which0 which1 frames firstframe\n')
    for _,k in ipairs(order) do
      local r,b,q,a,c = k:match('(%d+)|(%d+)|(%d+)|(%d+)|(%d+)')
      out:write(string.format('%3d %2d %d  %2d %2d  %6d %7d\n',
        tonumber(r),tonumber(b),tonumber(q),tonumber(a),tonumber(c),
        seen[k].n, seen[k].first))
    end
    out:close(); manager.machine:exit()
  end
end)
