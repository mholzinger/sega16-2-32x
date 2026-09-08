-- TILE-RAM WRITER PROBE for the ARCADE (mame altbeast), interactive play.
-- Logs every 68K write into System 16 tile RAM (0x400000-0x40FFFF) by
-- writer PC and page, and every write to the page/scroll registers in
-- text RAM (0x410E80-0x410E9F). Summary rewritten every 120 frames, so
-- quit MAME whenever the scene of interest has passed.
--   TW_LOG=/tmp/tilewrite.log mame altbeast -rompath ./mame \
--       -autoboot_script tools/tilewrite_probe.lua
-- (interactive: keep the window and keyboard — this one is for Mike's
--  hands, not the headless rig)
local path = os.getenv('TW_LOG') or '/tmp/tilewrite.log'
local mac = manager.machine
local cpu = mac.devices[':maincpu']
local sp = cpu.spaces['program']
local by_pc = {}          -- pc -> {count, pages set}
local regs = {}           -- offset -> {count, last value, last pc}
local frame = 0
local taps = {}
taps[#taps+1] = sp:install_write_tap(0x400000, 0x40FFFF, 'tilewrite',
    function(offset, data, mask)
        local pc = cpu.state['PC'].value
        local page = (offset - 0x400000) >> 12
        local e = by_pc[pc]
        if not e then e = {n = 0, pages = {}}; by_pc[pc] = e end
        e.n = e.n + 1
        e.pages[page] = (e.pages[page] or 0) + 1
    end)
taps[#taps+1] = sp:install_write_tap(0x410E80, 0x410E9F, 'regwrite',
    function(offset, data, mask)
        local pc = cpu.state['PC'].value
        local r = regs[offset]
        if not r then r = {n = 0}; regs[offset] = r end
        r.n = r.n + 1; r.last = data; r.pc = pc; r.frame = frame
    end)
_G.tw_taps = taps            -- anchor: chunk-local handles get GC'd (snd_autopsy law)
local function dump()
    local f = assert(io.open(path, 'w'))
    f:write(string.format("# frame %d — tile RAM writers (pc: total, per page)\n", frame))
    local pcs = {}
    for pc, _ in pairs(by_pc) do pcs[#pcs+1] = pc end
    table.sort(pcs, function(a, b) return by_pc[a].n > by_pc[b].n end)
    for _, pc in ipairs(pcs) do
        local e = by_pc[pc]
        local ps = {}
        for pg, n in pairs(e.pages) do ps[#ps+1] = string.format("p%d:%d", pg, n) end
        table.sort(ps)
        f:write(string.format("pc %06X  writes %d  %s\n", pc, e.n, table.concat(ps, ' ')))
    end
    f:write("# page/scroll register writes (offset: count, last value, last pc, frame)\n")
    for off, r in pairs(regs) do
        f:write(string.format("reg %06X  n %d  last %04X  pc %06X  frame %d\n", off, r.n, r.last, r.pc, r.frame))
    end
    f:close()
end
emu.register_frame_done(function()
    frame = frame + 1
    if frame % 120 == 0 then dump() end
end)
