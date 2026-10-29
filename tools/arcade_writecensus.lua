-- Per-frame census of the 68K's WRITES into the ranges tools/patch_game.py
-- rebases (and therefore thunks). Bounds the dynamic thunk multiplier:
-- patch_report.txt counts 192 STATIC sites; this counts EXECUTIONS.
--
-- CAVEAT (LESSONS "MAME lua taps lie on S16B"): taps under-report on this
-- board -- mapper remaps kill them. Read these as a LOWER BOUND on write
-- frequency, never as an exact protocol count.
--
-- out: docs/audit/arcade_writecensus.txt
local mac = manager.machine
local cpu = mac.devices[":maincpu"]
local sp  = cpu.spaces["program"]
local RANGES = {
  {name="tileRAM ", lo=0x400000, hi=0x410000},
  {name="textRAM ", lo=0x410000, hi=0x420000},
  {name="sprRAM  ", lo=0x440000, hi=0x450000},
  {name="palette ", lo=0x840000, hi=0x850000},
  {name="tilebank", lo=0x3F0000, hi=0x400000},
  {name="io      ", lo=0xC40000, hi=0xC44000},
}
local cnt, frames, taps = {}, 0, {}
for i,r in ipairs(RANGES) do cnt[i] = {n=0, tot=0, max=0} end
for i,r in ipairs(RANGES) do
  taps[i] = sp:install_write_tap(r.lo, r.hi-1, r.name, function(offset, data, mask)
    cnt[i].n = cnt[i].n + 1
    return data
  end)
end
local hist = {}
emu.register_frame_done(function()
  frames = frames + 1
  -- settle: ignore boot/attract warmup, sample the demo
  if frames > 1200 and frames <= 3600 then
    for i,r in ipairs(RANGES) do
      local c = cnt[i]
      c.tot = c.tot + c.n
      if c.n > c.max then c.max = c.n end
    end
  end
  for i,r in ipairs(RANGES) do cnt[i].n = 0 end
  if frames == 3600 then
    local f = io.open("docs/audit/arcade_writecensus.txt","w")
    f:write("# 68K WRITES PER FRAME into the ranges patch_game.py rebases.\n")
    f:write("# arcade (mame altbeast), frames 1201-3600 = 2400 sampled.\n")
    f:write("# LOWER BOUND: MAME lua taps under-report on S16B (LESSONS).\n")
    f:write("# 192 static sites are patched; these are EXECUTIONS per frame.\n\n")
    f:write(string.format("%-9s %10s %10s\n","range","mean/frame","max/frame"))
    local grand = 0
    for i,r in ipairs(RANGES) do
      local c = cnt[i]
      grand = grand + c.tot/2400
      f:write(string.format("%-9s %10.1f %10d\n", r.name, c.tot/2400, c.max))
    end
    f:write(string.format("\n%-9s %10.1f\n","TOTAL",grand))
    f:write("\n# Context: the arcade executes 14,209 instructions a vint with\n")
    f:write("# loops expanded, 8,184 of which are WORK (LOOP-DECOMPILE 88).\n")
    f:close()
    mac:exit()
  end
end)
