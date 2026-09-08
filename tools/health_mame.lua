-- LOOP 17 — state_health's key meters, read LIVE in MAME, so builds can
-- be A/B'd on an identical scripted play instead of spending an ares
-- play pass per question.
--
--   HM_OUT=path HM_TAG=name mame 32x -cart rom/s16.32x -rompath ./mame \
--       -skip_gameinfo -video none -sound none -nothrottle \
--       -autoboot_script tools/health_mame.lua
--
-- SAME ADDRESSES as tools/state_health.py (MD RAM is 0xFF0000+off there):
--   B0F0 vints            B0D0/B0D2 handler-lines sum
--   A040/A042 window+ack sum      A038/A03A/A03C/A03E consume span
--   DIAG[9] cycles, DIAG[7] blit skips, 0x28FBC bake hits
--
-- READ IT AS A RANKING, NOT A CLOCK: MAME runs the SH-2 ~3x fast, so the
-- absolute handler mean is not ares' number. What it ranks honestly is
-- the MD-side halves against each other on the SAME scene, which is what
-- an A/B needs.
local f=0; local md=nil; local sh=nil; local fields={}; local init=false
local out=assert(io.open(os.getenv('HM_OUT'),'w'))
local function set(n,v) local x=fields[n]; if x then x:set_value(v) end end
local function m16(a) return md:read_u16(0xFF0000+a) end
local function m32(a) return (m16(a)<<16)|m16(a+2) end

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
      sh:read_u32(0x06028024), sh:read_u32(0x0602801C),
      hmean, wmean, hmean-wmean,
      (wn>0) and wssum/wn or 0, m16(0xA03E),
      sh:read_u32(0x06028FBC)))
    -- TAILPROBE=1 only: the tail's own split (LOOP 6 accumulators).
    -- B0D4 stream sum, B0D8 dreq-push sum, B0DC palette-scan sum, all
    -- over the same vints. Zero on a normal build.
    local st,dq,ps = m32(0xB0D4), m32(0xB0D8), m32(0xB0DC)
    if (st+dq+ps) > 0 and vints > 0 then
      out:write(string.format(
        '%s f%d TAILSPLIT stream=%.1f dreq=%.1f palscan=%.1f '..
        '(max dreq=%d palscan=%d stream=%d)\n',
        os.getenv('HM_TAG'), f, st/vints, dq/vints, ps/vints,
        md:read_u8(0xFFB0E8), md:read_u8(0xFFB0EA), md:read_u8(0xFFB0EC)))
      out:flush()
    end
  end
  if f>=3200 then out:close(); manager.machine:exit() end
end)
