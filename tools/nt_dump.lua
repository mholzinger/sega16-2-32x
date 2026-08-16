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
-- ND_FRAME=<n> dumps at an absolute frame; ND_ANCHOR=eyehold dumps at
-- the parity eyehold state anchor + 20 settle (matches parity_cap and
-- tools/arc_dump.lua so both sides freeze the same game state).
local ms  = manager.machine.devices[":sega32x:32x_master_sh2"]
local msp = ms.spaces["program"]
local dir = os.getenv("ND_OUT") or "/tmp/nt_dump"
local at  = tonumber(os.getenv("ND_FRAME") or "0")
local anchor = os.getenv("ND_ANCHOR")
local armed = nil
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
  local go = (at > 0 and f == at)
  if anchor == "eyehold" and not go then
    local mdp = manager.machine.devices[":maincpu"].spaces["program"]
    if armed then
      armed = armed - 1
      go = (armed <= 0)
    elseif f >= 60
        and mdp:read_u8(0xFFF031) == 0x10
        and mdp:read_u8(0xFFC0A1) == 0x09
        and (mdp:read_u16(0xFFC0A2) & 0xFFF0) == 0x0040 then
      armed = 20
    end
  end
  if go then
    dump("snap.bin",  0x060052E8, 168)
    dump("ntmir.bin", 0x0603D200, 4480)
    dump("mdtag.bin", 0x0603B400, 4096)
    dump("sline.bin", 0x0603C4A0, 128)
    dump("tmap.bin",  0x06019000, 53248)
    dump("textu.bin", 0x06026000, 4096)
    dump("sbuf.bin",  0x06005390, 80640)   -- 336x240 composed indices
    dump("cram.bin",  0x06028900, 512)     -- cram_mirror
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
