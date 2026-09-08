-- WRITE-TAX CENSUS (DISCOVERY-TIMING question 1, docs/handoff/HANDOFF-DISCOVERY.md).
-- Counts, per frame, every 68K write the GAME makes to each rebased
-- hardware region on OUR memory map, plus every entry into the dirty-bit
-- thunk blocks (instruction fetches at the thunk code), so the static
-- census's sites become writes-per-frame. The MD-side 68K in MAME is
-- honest for counts (CLAUDE.md: the 68K side still reads true under
-- R60; only 32X pixels are confetti).
--
-- Ranges are the patcher's targets (tools/patch_game.py remap):
--   tile RAM   -> FB window 0x850000-0x85DFFF   (32X framebuffer staging)
--   text RAM   -> FB 0x85F000-0x85FFFF (FBTEXT) + WRAM 0xFF8000-0xFF8FFF
--   sprite RAM -> WRAM 0xFF7000-0xFF77FF
--   palette    -> WRAM 0xFF9000-0xFF9FFF (+ pal thunks 0xFFBA00..)
--   I/O + bank -> WRAM 0xFFB000-0xFFB04F
--   tile thunks 0xFFB820-0xFFB9E8, pal thunks 0xFFBA00-0xFFBBFF (fetches)
-- Coin+start like tools/play_32x.lua (frames 600/800). Reports per-frame
-- means over WINDOW (default frames 1500-3000, level-1 gameplay), totals,
-- and the top writer PCs per region.
-- Env: WC_OUT (default /tmp/write_census.txt), WC_END (default 3000),
--      WC_A / WC_B (window, default 1500 / 3000).
local out_path = os.getenv("WC_OUT") or "/tmp/write_census.txt"
local last = tonumber(os.getenv("WC_END") or "3000")
local wa = tonumber(os.getenv("WC_A") or "1500")
local wb = tonumber(os.getenv("WC_B") or "3000")
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local regions = {
    { name = "tileram_fb",  lo = 0x850000, hi = 0x85DFFF, kind = "w" },
    { name = "textram_fb",  lo = 0x85F000, hi = 0x85FFFF, kind = "w" },
    { name = "textram_wram",lo = 0xFF8000, hi = 0xFF8FFF, kind = "w" },
    { name = "spriteram",   lo = 0xFF7000, hi = 0xFF77FF, kind = "w" },
    { name = "palette",     lo = 0xFF9000, hi = 0xFF9FFF, kind = "w" },
    { name = "io_bank",     lo = 0xFFB000, hi = 0xFFB04F, kind = "w" },
    { name = "tile_thunk",  lo = 0xFFB820, hi = 0xFFB9E8, kind = "r" },
    { name = "pal_thunk",   lo = 0xFFBA00, hi = 0xFFBBFF, kind = "r" },
}
local frames = 0
local per = {}        -- name -> {frame counts array, total, sites}
local taps = {}
local fields, inited = {}, false
local function init_fields()
    for tag, port in pairs(manager.machine.ioport.ports) do
        for fname, field in pairs(port.fields) do fields[fname] = field end
    end
    inited = true
end
local function set_input(name, val) local f = fields[name]; if f then f:set_value(val) end end

for _, r in ipairs(regions) do
    per[r.name] = { n = 0, cur = 0, win = 0, winframes = 0, sites = {} }
    local p = per[r.name]
    local function hook(offset, data, mask)
        p.cur = p.cur + 1
        if frames >= wa and frames < wb then
            local pc = cpu.state["CURPC"].value
            p.sites[pc] = (p.sites[pc] or 0) + 1
        end
        return data
    end
    if r.kind == "w" then
        taps[#taps + 1] = mem:install_write_tap(r.lo, r.hi, "wc_" .. r.name, hook)
    else
        -- fetch of the thunk's first word = one entry; count only the entry
        -- words (every 16 bytes for tile thunks, every 8 for pal thunks) by
        -- counting all reads and dividing later — reported raw as well.
        taps[#taps + 1] = mem:install_read_tap(r.lo, r.hi, "wc_" .. r.name, hook)
    end
end

emu.register_frame_done(function()
    if not inited then init_fields() end
    frames = frames + 1
    if frames == 600 or frames == 800 then set_input('P1 Start', 1); set_input('P1 A', 1) end
    if frames == 640 or frames == 840 then set_input('P1 Start', 0); set_input('P1 A', 0) end
    for _, r in ipairs(regions) do
        local p = per[r.name]
        p.n = p.n + p.cur
        if frames >= wa and frames < wb then p.win = p.win + p.cur; p.winframes = p.winframes + 1 end
        p.cur = 0
    end
    if frames >= last then
        local f = assert(io.open(out_path, "w"))
        f:write(string.format("frames=%d window=%d-%d\n", frames, wa, wb))
        f:write("region        total    per-frame(window)  top sites (pc:count)\n")
        for _, r in ipairs(regions) do
            local p = per[r.name]
            local top = {}
            for pc, n in pairs(p.sites) do top[#top + 1] = { pc = pc, n = n } end
            table.sort(top, function(a, b) return a.n > b.n end)
            local s = {}
            for i = 1, math.min(6, #top) do s[#s + 1] = string.format("%06X:%d", top[i].pc & 0xFFFFFF, top[i].n) end
            f:write(string.format("%-13s %8d  %10.1f           %s\n", r.name, p.n,
                p.winframes > 0 and p.win / p.winframes or 0, table.concat(s, " ")))
        end
        f:close()
        manager.machine:exit()
    end
end)
