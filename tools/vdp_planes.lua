-- Does the MD VDP actually DRAW a tile plane, or is it only fed a HUD?
--
-- tools/vdp_survey.lua proves the 68000 writes to the VDP every frame.
-- It cannot say what appears on screen. This shadows VRAM and VSRAM from
-- the write stream (data port AND 68K->VRAM DMA, which the data port
-- never sees) and then reads the Plane A / Plane B name tables directly.
--
-- Per sample it reports, for each plane's visible 40x28 window:
--   fill    fraction of entries with a non-zero tile index
--   dist    distinct tile indices referenced
--   churn   entries changed since the previous sample
-- plus the H/V scroll values, so a scrolling plane is visible as motion.
--
-- A live tile plane reads as high fill with churn or scroll motion.
-- A HUD reads as low fill, concentrated in a few rows, scroll pinned.
--
--   SV_OUT=out.txt SV_FRAMES=1800 mame 32x -cart "<rom>" -rompath ./mame \
--     -skip_gameinfo -video none -sound none -nothrottle \
--     -autoboot_script tools/vdp_planes.lua
--
-- Emulator-side only; nothing in the guest is perturbed.

local FRAMES = tonumber(os.getenv('SV_FRAMES') or '1800')
local EVERY  = tonumber(os.getenv('SV_EVERY')  or '120')
local out    = assert(io.open(os.getenv('SV_OUT'), 'w'))

local vram, vsram, regs, cram = {}, {}, {}, {}
local addr, code, pend, inc = 0, 0, nil, 2
local dma_words, dma_ops = 0, 0
local cram_wp, cram_wd = 0, 0   -- CRAM writes: data-port vs DMA
-- NOTE: the tap installs on the first frame_done, so ANY VDP traffic
-- during boot is invisible to this shadow. Installing it at script load
-- instead was tried and is WORSE -- it desyncs the two-word control
-- state machine (vramNZ collapsed 31112 -> 2, CRAM writes 1 -> 16425
-- with an all-zero palette). A game that loads its palette once at
-- startup is therefore indistinguishable here from one that never
-- loads a palette at all. Do not read cramNZ as evidence of absence.
local TAPS = {}                 -- keep references: a collected tap stops firing
local frames, inited = 0, false
local fields, prev = {}, {}
local cpu_space

local function vr(a)            -- VRAM word, shadow default 0
  return vram[a & 0xFFFE] or 0
end

local function do_dma(data)
  local mode = (regs[23] or 0) & 0xC0
  local len  = (((regs[20] or 0) << 8) | (regs[19] or 0))
  if len == 0 then len = 0x10000 end
  if len > 32768 then len = 32768 end          -- runaway guard
  dma_ops = dma_ops + 1
  if mode == 0x80 then                          -- VRAM fill
    for _ = 1, len do
      vram[addr & 0xFFFE] = data & 0xFFFF
      addr = (addr + inc) & 0xFFFF
    end
  elseif mode == 0xC0 then                      -- VRAM copy
    local src = (((regs[22] or 0) << 8) | (regs[21] or 0))
    for _ = 1, len do
      vram[addr & 0xFFFE] = vr(src)
      src  = (src + 2) & 0xFFFF
      addr = (addr + inc) & 0xFFFF
    end
  else                                          -- 68K -> VDP
    local src = (((regs[23] or 0) & 0x7F) << 17)
              | (((regs[22] or 0)) << 9) | (((regs[21] or 0)) << 1)
    for _ = 1, len do
      local ok, w = pcall(function() return cpu_space:read_u16(src) end)
      if not ok then break end
      -- DESTINATION IS CD3..CD0; CD4/CD5 are the DMA selects. Comparing
      -- the whole composed code against 1/3/5 silently dropped every
      -- DMA'd word -- which is most of a Genesis game's VRAM traffic.
      local dst = code & 0x0F
      if dst == 1 then vram[addr & 0xFFFE] = w
      elseif dst == 3 then cram[addr & 0x7E] = w; cram_wd = cram_wd + 1
      elseif dst == 5 then vsram[addr & 0x7E] = w end
      dma_words = dma_words + 1
      src  = (src + 2) & 0xFFFFFF
      addr = (addr + inc) & 0xFFFF
    end
  end
end

local function tap()
  cpu_space = manager.machine.devices[':maincpu'].spaces['program']
  TAPS[#TAPS+1] = cpu_space:install_write_tap(0xC00000, 0xC00007, 'vdp',
    function(off, data, mask)
      local a = off & 6
      if a == 0 then
        local dst = code & 0x0F
        if dst == 1 then vram[addr & 0xFFFE] = data & 0xFFFF
        elseif dst == 3 then cram[addr & 0x7E] = data & 0xFFFF; cram_wp = cram_wp + 1
        elseif dst == 5 then vsram[addr & 0x7E] = data & 0xFFFF end
        addr = (addr + inc) & 0xFFFF
      elseif a == 4 then
        if pend == nil and (data & 0xE000) == 0x8000 then
          local r = (data >> 8) & 0x1F
          regs[r] = data & 0xFF
          if r == 15 then inc = data & 0xFF end
        elseif pend == nil then
          pend = data
        else
          code = ((pend >> 14) & 3) | (((data >> 4) & 0x0F) << 2)
          addr = ((pend & 0x3FFF) | ((data & 3) << 14)) & 0xFFFF
          pend = nil
          if (data & 0x0080) ~= 0 then do_dma(0) end
        end
      end
      return data
    end)
end

local function plane(base, wcols)
  local fill, dist, churn, seen = 0, 0, 0, {}
  local key = base
  prev[key] = prev[key] or {}
  local p = prev[key]
  for row = 0, 27 do
    for col = 0, 39 do
      local e = vr(base + (row * wcols + col) * 2)
      local t = e & 0x7FF
      if t ~= 0 then fill = fill + 1 end
      if t ~= 0 and not seen[t] then seen[t] = true; dist = dist + 1 end
      local i = row * 40 + col
      if p[i] ~= nil and p[i] ~= e then churn = churn + 1 end
      p[i] = e
    end
  end
  return fill / 1120.0, dist, churn
end

emu.register_frame_done(function()
  if not inited then
    for _, port in pairs(manager.machine.ioport.ports) do
      for fname, field in pairs(port.fields) do fields[fname] = field end
    end
    tap(); inited = true
  end
  frames = frames + 1
  if not os.getenv('SV_NOINPUT') then
    -- Menu walker. A single Start pulse parks most titles on a character
    -- select; cycling Start/A/B/C with an occasional Right walks through
    -- to gameplay, which is where churn and scroll motion live.
    -- START PAUSES THE GAME. The first version of this walker pulsed
    -- Start every 40 frames forever, which walked into gameplay and then
    -- immediately paused it -- churn read 0 and the scroll froze, and a
    -- screenshot was the only thing that revealed it. Start is for
    -- getting past menus ONLY; past BOOT_FRAMES it is never touched.
    local BOOT = 900
    local function set(n, v) local f = fields[n]; if f then f:set_value(v) end end
    local ph = frames % 40
    if frames <= BOOT then
      local btn = ({'P1 Start', 'P1 A', 'P1 B', 'P1 C'})[((frames // 40) % 4) + 1]
      if ph == 0 then set(btn, 1) end
      if ph == 8 then set(btn, 0) end
    else
      local btn = ({'P1 A', 'P1 B', 'P1 C'})[((frames // 40) % 3) + 1]
      if ph == 0  then set(btn, 1) end
      if ph == 8  then set(btn, 0) end
      if ph == 12 then set('P1 Right', 1) end
      if ph == 34 then set('P1 Right', 0) end
    end
  end
  if frames % EVERY == 0 then
    local wcols = ({[0]=32,[1]=64,[2]=32,[3]=128})[(regs[16] or 0) & 3]
    local pa = ((regs[2] or 0) & 0x38) << 10
    local pb = ((regs[4] or 0) & 0x07) << 13
    local hs = ((regs[13] or 0) & 0x3F) << 10
    local fa, da, ca = plane(pa, wcols)
    local fb, db, cb = plane(pb, wcols)
    -- A game can put pixels on the MD layer WITHOUT the scroll planes:
    -- via hardware sprites, or via the window plane. Empty name tables
    -- alone do not mean "the MD VDP draws nothing".
    local cn, cl = 0, {}
    for _, v in pairs(cram) do
      if v ~= 0 then local k = v & 0x0EEE
        if not cl[k] then cl[k] = true; cn = cn + 1 end end
    end
    local sat = ((regs[5] or 0) & 0x7F) << 9
    local spr = 0
    for i = 0, 79 do
      local y = vr(sat + i * 8) & 0x3FF
      if y ~= 0 then spr = spr + 1 end
    end
    local win = ((regs[3] or 0) & 0x3E) << 10
    local wfill = 0
    for i = 0, 1119 do
      if (vr(win + i * 2) & 0x7FF) ~= 0 then wfill = wfill + 1 end
    end
    -- NULL-RESULT GUARD: an empty name table means nothing unless the
    -- shadow demonstrably holds data. If vram_nz is ~0 the instrument is
    -- blind (a missed DMA path), not the game.
    local vram_nz = 0
    for _, v in pairs(vram) do if v ~= 0 then vram_nz = vram_nz + 1 end end
    out:write(string.format(
      'f=%4d vramNZ=%5d A@%04x fill=%.2f dist=%3d churn=%4d | B@%04x fill=%.2f dist=%3d churn=%4d'
      .. ' | spr=%2d win=%.2f cramNZ=%2d cwP=%d cwD=%d | vscrollA=%5d vscrollB=%5d | dma_ops=%d dma_words=%d\n',
      frames, vram_nz, pa, fa, da, ca, pb, fb, db, cb,
      spr, wfill / 1120.0, cn, cram_wp, cram_wd,
      (vsram[0] or 0) & 0x3FF, (vsram[2] or 0) & 0x3FF,
      dma_ops, dma_words))
    out:flush()
  end
  if frames >= FRAMES then
    out:write('CRAM: ')
    for i = 0, 62, 2 do out:write(string.format('%04x ', cram[i] or 0)) end
    out:write('\n')
    out:close(); manager.machine:exit()
  end
end)
