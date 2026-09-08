-- LOOP 24 — VISRFLIP ISR-cost probe readout. health_mame.lua's meters
-- plus the V-ISR counter family, on the same scripted play, so the two
-- arms A/B on identical input.
--
--   HM_OUT=path HM_TAG=name mame 32x -cart rom/s16.32x -rompath ./mame \
--       -skip_gameinfo -video none -sound none -nothrottle \
--       -window -resolution 160x120 -keyboardprovider none -nomouse \
--       -nojoystick -bench 100 -autoboot_script tools/visr_probe.lua
--
-- VISR family (DIAG base 0x06028000, see visr_vbi in m_main.c):
--   [56] body-fallback flips  [49] ISR fires  [58] ISR flips
--   ([57] is NOT the fire counter: the MD_BG tile batcher owns it)
--   [59] bail: COMM0 live at entry   [60] bail: no post in bound
--   [61] non-k2 post   [62] max ISR ticks   [63] sum ISR ticks
-- Plus [31] late latches and [7] skips: the flip-health pair the ares
-- state reader watches. On the base arm the family reads all zero.
--
-- CAVEAT (CLAUDE.md): MAME's SH-2 is ~3x fast — [62]/[63] are a
-- structure check here (does the span run, is it bounded), never a
-- cost number. Cost comes from the ares state.
local f=0; local md=nil; local sh=nil; local fields={}; local init=false
local out=assert(io.open(os.getenv('HM_OUT'),'w'))
local function set(n,v) local x=fields[n]; if x then x:set_value(v) end end
local function m16(a) return md:read_u16(0xFF0000+a) end
local function m32(a) return (m16(a)<<16)|m16(a+2) end
local function diag(i) return sh:read_u32(0x06028000+4*i) end

emu.register_frame_done(function()
  f=f+1
  if not init then
    for _,p in pairs(manager.machine.ioport.ports) do
      for n,fl in pairs(p.fields) do fields[n]=fl end end
    md=manager.machine.devices[':maincpu'].spaces['program']
    sh=manager.machine.devices[':sega32x:32x_master_sh2'].spaces['program']
    init=true
  end
  -- proven coin/start pattern (tools/play_32x.lua), then walk+attack
  if f==600 or f==800 then set('P1 Start',1); set('P1 A',1) end
  if f==640 or f==840 then set('P1 Start',0); set('P1 A',0) end
  if f==1000 then set('P1 Start',1) end
  if f==1040 then set('P1 Start',0) end
  if f>1500 then
    local p=f%240
    set('P1 Right',(p<120) and 1 or 0)
    set('P1 A',(p%40<8) and 1 or 0)
  end
  if f==2000 or f==2600 or f==3200 then
    local vints=m16(0xB0F0)
    local hsum=m32(0xB0D0)
    local wsum=m32(0xA040)
    local wn=m16(0xA03C)
    local wssum=m32(0xA038)
    local hmean=(vints>0) and hsum/vints or 0
    local wmean=(vints>0) and wsum/vints or 0
    out:write(string.format(
      '%s f%d vints=%d cycles=%d skips=%d hmean=%.1f window=%.1f tail=%.1f '..
      'span=%.1f spanmax=%d hits=%d\n',
      os.getenv('HM_TAG'), f, vints,
      diag(9), diag(7),
      hmean, wmean, hmean-wmean,
      (wn>0) and wssum/wn or 0, m16(0xA03E),
      sh:read_u32(0x06028FBC)))
    local flips=diag(58)
    out:write(string.format(
      '%s f%d VISR fires=%d flips=%d fallback=%d stale=%d nopost=%d '..
      'k1=%d tickmax=%d tickmean=%.1f latelatch=%d\n',
      os.getenv('HM_TAG'), f, diag(49), flips, diag(56), diag(59),
      diag(60), diag(61), diag(62),
      (flips>0) and diag(63)/flips or 0, diag(31)))
    out:flush()
  end
  if f>=3200 then out:close(); manager.machine:exit() end
end)
