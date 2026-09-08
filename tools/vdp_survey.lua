-- 32X library survey: does the MD VDP draw anything during gameplay?
-- Taps the 68000's VDP port and 32X control writes. Emulator-side only.
local FRAMES  = tonumber(os.getenv('SV_FRAMES') or '3600')
local out     = assert(io.open(os.getenv('SV_OUT'), 'w'))
local frames  = 0
local regs    = {}          -- last value written to each VDP register
local ctrlw, dataw, dma = 0, 0, 0
local fmw, bmw = 0, 0
local lastbm  = -1
local pend    = nil         -- first half of a 32-bit control word
local cd      = 0           -- current code/dest
local dest    = {vram=0, cram=0, vsram=0, other=0}
local win     = {}          -- per-window data-write counts
local wdata, wctrl = 0, 0
local inited  = false
local TAPS    = {}          -- MUST keep references: an unreferenced tap is collected and silently stops firing
local fields  = {}

local function tap()
  local cpu = manager.machine.devices[':maincpu']
  local sp  = cpu.spaces['program']
  TAPS[#TAPS+1] = sp:install_write_tap(0xC00000, 0xC00007, 'vdp', function(off, data, mask)
    local a = off & 6
    if a == 0 then
      dataw = dataw + 1; wdata = wdata + 1
      local code = cd & 0x0F
      if code == 1 then dest.vram = dest.vram + 1
      elseif code == 3 then dest.cram = dest.cram + 1
      elseif code == 5 then dest.vsram = dest.vsram + 1
      else dest.other = dest.other + 1 end
    elseif a == 4 then
      ctrlw = ctrlw + 1; wctrl = wctrl + 1
      if pend == nil and (data & 0xE000) == 0x8000 then
        regs[(data >> 8) & 0x1F] = data & 0xFF          -- register write
      elseif pend == nil then
        pend = data
      else
        cd = ((pend >> 14) & 3) | (((data >> 4) & 3) << 2)
        if (data & 0x0080) ~= 0 then dma = dma + 1 end
        pend = nil
      end
    end
    return data
  end)
  TAPS[#TAPS+1] = sp:install_write_tap(0xA15100, 0xA15101, 'fm', function(off, data, mask)
    fmw = fmw + 1; return data end)
  TAPS[#TAPS+1] = sp:install_write_tap(0xA15180, 0xA15181, 'bm', function(off, data, mask)
    bmw = bmw + 1; lastbm = data; return data end)
end

local function set_input(n, v)
  local f = fields[n]; if f then f:set_value(v) end
end

emu.register_frame_done(function()
  if not inited then
    for _, port in pairs(manager.machine.ioport.ports) do
      for fname, field in pairs(port.fields) do fields[fname] = field end
    end
    tap(); inited = true
    local fn=io.open(os.getenv('SV_OUT')..'.fields','w')
    for k in pairs(fields) do fn:write(k..'\n') end
    fn:close()
  end
  frames = frames + 1
  -- generic attract-buster: pulse Start and A on a rolling cadence
  -- START PAUSES THE GAME: only use it to clear menus (see vdp_planes.lua).
  local ph = (os.getenv('SV_NOINPUT') and -1) or (frames % 40)
  if frames <= 900 then
    if ph == 0  then set_input('P1 Start', 1) end
    if ph == 8  then set_input('P1 Start', 0) end
  elseif ph >= 0 then
    if ph == 0  then set_input('P1 A', 1) end
    if ph == 8  then set_input('P1 A', 0) end
    if ph == 12 then set_input('P1 Right', 1) end
    if ph == 34 then set_input('P1 Right', 0) end
  end
  if frames % 300 == 0 then
    win[#win+1] = {frames, wdata, wctrl}
    wdata, wctrl = 0, 0
  end
  if frames >= FRAMES then
    out:write(string.format('frames=%d ctrl=%d data=%d dma=%d fm_writes=%d bitmap_writes=%d bitmap_last=%04x\n',
      frames, ctrlw, dataw, dma, fmw, bmw, lastbm & 0xFFFF))
    out:write(string.format('dest vram=%d cram=%d vsram=%d other=%d\n',
      dest.vram, dest.cram, dest.vsram, dest.other))
    local r1 = regs[1] or 0
    out:write(string.format('reg01=%02x display=%s  planeA=%02x planeB=%02x sprattr=%02x hscroll=%02x mode=%02x/%02x\n',
      r1, ((r1 & 0x40) ~= 0) and 'ON' or 'off',
      regs[2] or 0, regs[4] or 0, regs[5] or 0, regs[13] or 0, regs[11] or 0, regs[12] or 0))
    out:write('windows(300f): ')
    for _, w in ipairs(win) do out:write(string.format('%d ', w[2])) end
    out:write('\n')
    out:close()
    manager.machine:exit()
  end
end)
