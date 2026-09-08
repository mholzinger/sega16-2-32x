-- PALETTE WRITER CENSUS (LOOP 8). Taps every 68K write to the palette
-- mirror (0xFF9000, 4KB) and reports, per writing PC, the ADDRESS RANGE
-- it touches and how often. That is exactly what the dirty-bit thunks
-- need: the site list AND each site's real extent.
--
-- Why a tap and not tools/wpcatch.lua: a debugger watchpoint traps into
-- the debugger on every hit (palette writes run in the hundreds per
-- frame), which is far too slow to cover a whole attract cycle. Taps run
-- inline and cost nothing measurable.
--
-- WHY THIS MEASUREMENT IS MANDATORY: the static enumeration from
-- patch_report.txt only sees ADDRESS-FORMATION sites (lea/move.l #imm).
-- 0x3C20 forms 0xFF9800+d0*32 and STORES THE POINTER into a queue at
-- 0xFFF402; the actual writer is the register-indirect loop at 0x3C5A,
-- which no static scan can attribute. A missed writer means permanently
-- stale colours once the diff scan dies.
--
-- Env: PAL_OUT (default /tmp/pal_sites.txt), PAL_FRAMES (default 3000).
local out_path = os.getenv("PAL_OUT") or "/tmp/pal_sites.txt"
local last = tonumber(os.getenv("PAL_FRAMES") or "3000")
local base = tonumber(os.getenv("PAL_BASE") or "0xFF9000")
local size = tonumber(os.getenv("PAL_SIZE") or "0x1000")

local sites = {}          -- pc -> {lo, hi, n, first_frame}
local frames = 0
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local tap

local function hook(offset, data, mask)
    local pc = cpu.state["CURPC"].value
    local s = sites[pc]
    if not s then
        -- opcode words at the writing PC: CURPC alone is ambiguous across
        -- the cart's several MD-visible windows (0x88xxxx / 0x8Cxxxx /
        -- 0x90xxxx all alias the same ROM), and the opcode names the site.
        local ok, w0 = pcall(function() return mem:read_u16(pc) end)
        local _, w1 = pcall(function() return mem:read_u16(pc + 2) end)
        sites[pc] = { lo = offset, hi = offset, n = 1, f = frames, rgn = {},
                      op = ok and string.format("%04X %04X", w0, w1 or 0) or "????" }
        s = sites[pc]
    else
        if offset < s.lo then s.lo = offset end
        if offset > s.hi then s.hi = offset end
        s.n = s.n + 1
    end
    -- REGION SET, not just lo/hi. The dirty-bit design marks 128-word
    -- (256-byte) regions, and a shared helper's lo/hi says nothing about
    -- which regions in between it really touches — the static per-caller
    -- masks can only be validated against the observed SET.
    s.rgn[((offset - base) >> 8) & 15] = true
end

local function install()
    if tap then tap:remove() end
    tap = mem:install_write_tap(base, base + size - 1, "paltap", hook)
end
install()

local function find_field(names)
    for _, port in pairs(manager.machine.ioport.ports) do
        for fname, field in pairs(port.fields) do
            for _, want in ipairs(names) do
                if fname == want then return field end
            end
        end
    end
end
local coin  = find_field({"Coin 1", "P1 A"})
local start = find_field({"1 Player Start", "Start 1", "P1 Start"})
local btn1  = find_field({"P1 Button 1", "P1 A"})
local btn2  = find_field({"P1 Button 2", "P1 B"})
local left  = find_field({"P1 Left"})
local right = find_field({"P1 Right"})

local function dump()
    local f = assert(io.open(out_path, "w"))
    local pcs = {}
    for pc in pairs(sites) do pcs[#pcs + 1] = pc end
    table.sort(pcs)
    f:write(string.format("# palette writers, %d frames, range %06X..%06X\n",
                          frames, base, base + size - 1))
    f:write("# CURPC       lo     hi     rgnmask writes firstframe opcode\n")
    for _, pc in ipairs(pcs) do
        local s = sites[pc]
        local m = 0
        for r in pairs(s.rgn) do m = m | (1 << r) end
        f:write(string.format("%06X  %06X %06X  %04X  %7d  %6d  %s\n",
                              pc, s.lo, s.hi, m, s.n, s.f, s.op or ""))
    end
    f:close()
end

emu.register_frame_done(function()
    frames = frames + 1
    install()                      -- bank switches can drop taps
    -- attract runs on its own; coin+start at 10s/12s reaches real play,
    -- then mash so gameplay palettes (fades, hits, level-ups) are covered.
    if coin  and frames >= 600 and frames < 610 then coin:set_value(1)
    elseif coin and frames == 610 then coin:set_value(0) end
    if start and frames >= 720 and frames < 730 then start:set_value(1)
    elseif start and frames == 730 then start:set_value(0) end
    if frames > 800 then
        local ph = frames % 120
        if btn1  then btn1:set_value(ph < 20 and 1 or 0) end
        if btn2  then btn2:set_value(ph >= 40 and ph < 50 and 1 or 0) end
        if right then right:set_value(ph >= 60 and ph < 100 and 1 or 0) end
        if left  then left:set_value(ph >= 100 and 1 or 0) end
    end
    if frames % 500 == 0 then dump() end
    if frames >= last then dump(); manager.machine:exit() end
end)
