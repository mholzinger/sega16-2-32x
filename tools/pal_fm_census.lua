-- PALETTE-vs-FM COLLISION CENSUS (R60 palette-transport arc, step 1).
-- The push conviction says palette is 107 of the packet's 151 words;
-- the redesign wants game palette writes routed thunk-side into
-- FM-gated FB staging instead of riding the DREQ FIFO. Whether that
-- works hangs on WHEN the game's palette writes land relative to the
-- FM=1 span, per frame class (fade vs normal).
--
-- This taps every 68K write to the palette mirror (0xFF9000, 4KB) and
-- records, per frame: write count, live-FM collisions (0xA15100 bit 15
-- at write time), distinct 32-word blocks touched (the packet's K),
-- and a V-position histogram in 16-line buckets. MAME's 68K/VDP
-- timing is honest, so the V distribution is real; MAME's FM span is
-- SHORTER than ares' (SH-2 ~3x fast), so the fm1 column is a LOWER
-- bound on collisions — the ares answer comes from applying the
-- ares-measured FM span to the V buckets offline.
--
-- Inputs replay discover/inputs/*.csv (frame,button,val). csv 'y'
-- (coin on ares' 6-button pad) maps to START+A held — md_main.c:2518
-- accepts that composite on any pad.
--
-- Env: PFC_OUT (csv), PFC_SITES (site table), PFC_INPUTS (input csv),
--      PFC_FRAMES (default 12700), PFC_SNAP (snapshot dir; snapshots
--      at 1000/2000/5000/9000 verify the replay reached gameplay).
local out_path   = os.getenv("PFC_OUT") or "/tmp/pal_fm.csv"
local site_path  = os.getenv("PFC_SITES") or "/tmp/pal_fm_sites.txt"
local in_path    = os.getenv("PFC_INPUTS") or "discover/inputs/play_level1.csv"
local last       = tonumber(os.getenv("PFC_FRAMES") or "12700")
local snap_dir   = os.getenv("PFC_SNAP")
local base, size = 0xFF9000, 0x1000

local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local screen
for _, s in pairs(manager.machine.screens) do screen = s break end
-- V position: the VDP HV counter (V in the high byte, the codebase's
-- 0xDF-style convention). screen.vpos does not exist in this MAME's
-- lua API — the first run died on it (nil >> 4) and lost every
-- bucket and the whole site table.
local function vcounter()
    return mem:read_u16(0xC00008) >> 8
end

-- ---- input replay ------------------------------------------------
local NAME_MAP = {
    a = {"P1 A"}, b = {"P1 B"}, c = {"P1 C"},
    left = {"P1 Left"}, right = {"P1 Right"},
    up = {"P1 Up"}, down = {"P1 Down"},
    start = {"P1 Start"},
    y = {"P1 Start", "P1 A"},      -- coin: START+A held (md_main.c:2518)
}
local fields = {}
for _, port in pairs(manager.machine.ioport.ports) do
    for fname, field in pairs(port.fields) do fields[fname] = field end
end
local events = {}                  -- frame -> { {field, val}, ... }
do
    local f = assert(io.open(in_path, "r"), "no input csv: " .. in_path)
    for line in f:lines() do
        if not line:match("^%s*#") and line:match(",") then
            local fr, name, val = line:match("^(%d+),(%w+),(%d+)")
            if fr then
                local list = events[tonumber(fr)] or {}
                events[tonumber(fr)] = list
                for _, mname in ipairs(NAME_MAP[name] or {}) do
                    if fields[mname] then
                        list[#list + 1] = { fields[mname], tonumber(val) }
                    end
                end
            end
        end
    end
    f:close()
end

-- ---- tap ---------------------------------------------------------
local frames = 0
local NB = 16                      -- v-counter buckets of 16 (0x00..0xFF)
local fw, ffm, fchg, fblk = 0, 0, 0, {}   -- current-frame accumulators
local fvb = {}
for i = 0, NB - 1 do fvb[i] = 0 end
local sites = {}                   -- pc -> {n, fm1, chg, vmin, vmax}
local rows = {}                    -- per-frame csv rows (flushed periodically)

local tap
local function hook(offset, data, mask)
    local v = vcounter()
    local fm = (mem:read_u16(0xA15100) & 0x8000) ~= 0
    -- redundant-vs-changing: the tap runs before the write commits,
    -- so a read returns the OLD value. The glow streamer may rewrite
    -- unchanged words; a word-delta diet hangs on this ratio.
    local chg = (mem:read_u16(offset & ~1) & mask) ~= (data & mask)
    fw = fw + 1
    if fm then ffm = ffm + 1 end
    if chg then fchg = fchg + 1 end
    fblk[(offset - base) >> 6] = true        -- 32-word (64-byte) block
    fvb[v >> 4] = fvb[v >> 4] + 1
    local pc = cpu.state["CURPC"].value
    local s = sites[pc]
    if not s then
        sites[pc] = { n = 1, fm1 = fm and 1 or 0, chg = chg and 1 or 0,
                      vmin = v, vmax = v }
    else
        s.n = s.n + 1
        if fm then s.fm1 = s.fm1 + 1 end
        if chg then s.chg = s.chg + 1 end
        if v < s.vmin then s.vmin = v end
        if v > s.vmax then s.vmax = v end
    end
end
local function install()
    if tap then tap:remove() end
    tap = mem:install_write_tap(base, base + size - 1, "palfm", hook)
end
install()

-- ---- output ------------------------------------------------------
local out = assert(io.open(out_path, "w"))
local hdr = "frame,nw,fm1,chg,nblk,blkmask"
for i = 0, NB - 1 do hdr = hdr .. ",v" .. string.format("%02X", i * 16) end
out:write(hdr .. "\n")

local function dump_sites()
    local f = assert(io.open(site_path, "w"))
    f:write(string.format("# %d frames  fm1 = writes with FM held (MAME lower bound)  chg = value-changing writes\n", frames))
    f:write("# CURPC   writes   fm1    chg   vmin vmax\n")
    local pcs = {}
    for pc in pairs(sites) do pcs[#pcs + 1] = pc end
    table.sort(pcs)
    for _, pc in ipairs(pcs) do
        local s = sites[pc]
        f:write(string.format("%06X %8d %6d %6d    %02X   %02X\n",
                              pc, s.n, s.fm1, s.chg, s.vmin, s.vmax))
    end
    f:close()
end

emu.register_frame_done(function()
    frames = frames + 1
    install()                      -- bank switches can drop taps
    if fw > 0 then
        local nblk, mlo, mhi = 0, 0, 0
        for b in pairs(fblk) do
            nblk = nblk + 1
            if b < 32 then mlo = mlo | (1 << b) else mhi = mhi | (1 << (b - 32)) end
        end
        local row = string.format("%d,%d,%d,%d,%d,%08X%08X",
                                  frames, fw, ffm, fchg, nblk, mhi, mlo)
        for i = 0, NB - 1 do row = row .. "," .. fvb[i] end
        rows[#rows + 1] = row
    end
    fw, ffm, fchg, fblk = 0, 0, 0, {}
    for i = 0, NB - 1 do fvb[i] = 0 end
    local evs = events[frames]
    if evs then
        for _, e in ipairs(evs) do e[1]:set_value(e[2]) end
    end
    if snap_dir and (frames == 1000 or frames == 2000 or frames == 5000
                     or frames == 9000) then
        screen:snapshot(string.format("%s/palfm_%05d.png", snap_dir, frames))
    end
    if #rows >= 256 or frames >= last then
        out:write(table.concat(rows, "\n"))
        if #rows > 0 then out:write("\n") end
        rows = {}
    end
    if frames % 2000 == 0 then dump_sites() end
    if frames >= last then
        dump_sites()
        out:close()
        manager.machine:exit()
    end
end)
