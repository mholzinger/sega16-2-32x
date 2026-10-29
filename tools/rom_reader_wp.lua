-- Who READS a rom block that nothing points at (LOOP-DECOMPILE 85).
--
-- A read TAP does not work here: MAME serves rom reads from the direct
-- access pointer and taps never see them. The control range proved that
-- before anything was concluded from the silence. Debugger watchpoints do
-- work, because installing one disables the fast path for that range.
--
--   mame altbeast ... -debug -debugger none -autoboot_script this
--
-- Each watchpoint STOPS the machine; a periodic callback reads the PC,
-- records it and resumes. Cheap for a rare range, ruinous for a hot one,
-- so keep the ranges narrow and cap the hits.
local RANGES = {
  {0x22000, 0x232A0, 'tile-index block'},
  {0x29000, 0x29E00, 'gap before scene 0 tilemap'},
  {0x01CE2, 0x01D00, 'CONTROL scene descriptor'},
}
local CAP = tonumber(os.getenv('RT_CAP') or '40')
local out = assert(io.open(os.getenv('RT_OUT') or '/tmp/wp.txt', 'w'))
local dbg, st, wps, hits, n = nil, nil, {}, {}, 0
local frames = 0

-- RT_PLAY=1 coins up and starts, so a block that attract never touches
-- still gets its chance. Without it a silent range only means "not in
-- attract", which is a much weaker claim than it looks.
local fields = {}
emu.register_frame_done(function()
  frames = frames + 1
  if os.getenv('RT_PLAY') then
    if next(fields) == nil then
      for _, p in pairs(manager.machine.ioport.ports) do
        for n, fl in pairs(p.fields) do fields[n] = fl end
      end
    end
    local function set(n, v) local x = fields[n]; if x then x:set_value(v) end end
    if frames == 600 or frames == 800 then set('P1 Start', 1); set('P1 A', 1) end
    if frames == 640 or frames == 840 then set('P1 Start', 0); set('P1 A', 0) end
    if frames == 1000 then set('P1 Start', 1) end
    if frames == 1040 then set('P1 Start', 0) end
    if frames > 1200 then
      local q = frames % 240
      set('P1 Right', (q < 120) and 1 or 0)
      set('P1 A', (q % 40 < 8) and 1 or 0)
    end
  end
end)

emu.register_periodic(function()
  if dbg == nil then
    local d = manager.machine.devices[':maincpu']
    dbg, st = d.debug, d.state
    local sp = d.spaces['program']
    -- RT_ROUND starts the game at that round by rewriting the DIP round
    -- table at 0x1848 (LOOP-DECOMPILE 86), so a block that level 1 never
    -- touches still gets its chance on the levels that might.
    local rnd = os.getenv('RT_ROUND')
    if rnd then
      local rg = manager.machine.memory.regions[':maincpu']
      for i = 0, 7 do rg:write_u8(0x1848 + i, tonumber(rnd)) end
      out:write('starting round forced to ' .. rnd .. '\n')
    end
    -- ONE range per run. The watchpoint does not tell lua which address
    -- it fired on, so running them together cannot say which block was
    -- read -- and that is the whole question.
    local sel = tonumber(os.getenv('RT_RANGE') or '0')
    for i, r in ipairs(RANGES) do
      if sel == 0 or sel == i then
        wps[#wps + 1] = dbg:wpset(sp, 'r', r[1], r[2] - r[1], '', '')
        out:write(string.format('watching 0x%05X-0x%05X  %s\n', r[1], r[2], r[3]))
      end
    end
    out:write(string.format('%d watchpoints set\n', #wps))
    out:flush()
    manager.machine.debugger.execution_state = 'run'
    return
  end
  if manager.machine.debugger.execution_state == 'stop' then
    local pc = st['PC'].value
    local k = string.format('%06X', pc)
    hits[k] = (hits[k] or 0) + 1
    n = n + 1
    if hits[k] == 1 then
      out:write(string.format('PC %06X  frame %d\n', pc, frames))
      out:flush()
    end
    if n >= CAP then
      for i = 1, #wps do pcall(function() dbg:wpclear(wps[i]) end) end
      out:write('cap reached, watchpoints cleared\n')
    end
    manager.machine.debugger.execution_state = 'run'
  end
end)
