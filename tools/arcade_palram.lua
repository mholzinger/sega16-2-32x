-- Dump the ARCADE's palette RAM (0x840000, 0x1000 bytes) for a chosen
-- scene, in the same layout tools/bake_tilecram.py takes as --live: the
-- port mirrors palette RAM 1:1 at work RAM 0xFF9000, so an arcade dump
-- and a port dump are directly comparable word for word.
--   AP_N=<scene> AP_F=<frame> AP_OUT=file.bin mame altbeast ...
local N=tonumber(os.getenv('AP_N') or '0')
-- AP_F is a frame; AP_STEP waits for an attract step that SHOWS THE SCENE
-- instead (LOOP-DECOMPILE 75: 0x04/0x0C/0x14 are the scene backdrop, 0x10
-- is the eye title and 0x00 the score table), then settles 90 frames.
local F=tonumber(os.getenv('AP_F') or '0')
local STEP=tonumber(os.getenv('AP_STEP') or '12')  -- 0x0C
local armed=nil
local f=0; local md=nil
emu.register_frame_done(function()
  f=f+1
  if md==nil then
    md=manager.machine.devices[':maincpu'].spaces['program']
    local rg=manager.machine.memory.regions[':maincpu']
    for i=0,7 do rg:write_u8(0x1CDA+i, N) end
  end
  if F==0 and armed==nil and f>200 and md:read_u8(0xFFF031)==STEP then
    armed=f+90
  end
  if f==F or f==armed then
    local fh=assert(io.open(os.getenv('AP_OUT'),'wb'))
    for a=0,0xFFF do fh:write(string.char(md:read_u8(0x840000+a))) end
    fh:close()
    print(string.format('arcade_palram: scene %d at f%d, f031=%02x',
      md:read_u8(0xFFF142), f, md:read_u8(0xFFF031)))
    manager.machine:exit()
  end
end)
