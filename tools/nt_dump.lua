-- Dump the MD nt-builder's whole evidence chain at one frame (LOOP14
-- item 3, eyehold residue): staged layer regs, nt mirror, allocator
-- tags, palette-set lines, and the SDRAM tilemap truth.
--   ND_FRAME=1600 ND_OUT=/tmp/nt_dump mame 32x -cart <MDBGALL rom> \
--     -rompath ./mame ... -autoboot_script tools/nt_dump.lua
-- Files (raw big-endian bytes, decode with tools/nt_audit.py):
--   snap.bin   0x060052E8  168    (layer_regs snap[2]: A then B)
--   ntmir.bin  0x0603D200  4480   (md_dbg_nt [2][28][40] u16)
--   mdtag.bin  0x0603B400  4096   (md_tag u32 x1024)
--   sline.bin  0x0603C4A0  128    (mdp_s_line)
--   tmap.bin   0x06019000  53248  (TILEMAP_C 13 pages x 2K words)
local ms  = manager.machine.devices[":sega32x:32x_master_sh2"]
local msp = ms.spaces["program"]
local dir = os.getenv("ND_OUT") or "/tmp/nt_dump"
local at  = tonumber(os.getenv("ND_FRAME") or "1600")
local function dump(name, base, len)
  local f = assert(io.open(string.format("%s/%s", dir, name), "wb"))
  local t = {}
  for i = 0, len - 1 do t[#t + 1] = string.char(msp:read_u8(base + i)) end
  f:write(table.concat(t))
  f:close()
end
local f = 0
emu.register_frame_done(function()
  f = f + 1
  if f == at then
    dump("snap.bin",  0x060052E8, 168)
    dump("ntmir.bin", 0x0603D200, 4480)
    dump("mdtag.bin", 0x0603B400, 4096)
    dump("sline.bin", 0x0603C4A0, 128)
    dump("tmap.bin",  0x06019000, 53248)
    dump("textu.bin", 0x06026000, 4096)
    do  -- 68K WRAM text shadow (patch_game: 0x410000 -> 0xFF8000)
      local mdp = manager.machine.devices[":maincpu"].spaces["program"]
      local fh = assert(io.open(dir .. "/shadow.bin", "wb"))
      local t = {}
      for i = 0, 4095 do t[#t + 1] = string.char(mdp:read_u8(0xFF8000 + i)) end
      fh:write(table.concat(t))
      fh:close()
    end
    manager.machine:exit()
  end
end)
