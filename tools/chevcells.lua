-- MDCHEV sizing: how many CELLS does the cycler/chevron plane (colour
-- sets 19/20/21) cover in LIVE tile RAM, after the round has loaded?
--
-- bake_tilecram.py models demand from the ROM's packed tilemaps and
-- counts ZERO cells in 19/20/21 on all ten pages, all five rounds -- so
-- the plane is written at RUNTIME and the bake cannot see it. This walks
-- the arcade's own tile RAM instead (the same source and the same frame
-- as docs/audit/round_sets_definitive.txt, LOOP-DECOMPILE 125).
--
-- S16B map: tilemap page N at 0x400000 + N*0x1000 (0x800 words), text
-- RAM at 0x410000 (word 0x740 = 0x410E80, matching arcade_pagesel*.lua).
-- Colour set = (w >> 6) & 0x7F, as sh_src/m_main.c:15546.
--
-- Env: CC_OUT, CC_FRAME (default 700, entry 125's point).
local out   = os.getenv("CC_OUT") or "/tmp/chevcells.txt"
local at    = tonumber(os.getenv("CC_FRAME") or "700")
local SETS  = { [19]=true, [20]=true, [21]=true }

local md = manager.machine.devices[":maincpu"].spaces["program"]
local frames = 0

emu.register_frame_done(function()
    frames = frames + 1
    if frames ~= at then return end
    local fh = assert(io.open(out, "w"))
    fh:write(string.format("# live tile RAM at frame %d\n", at))
    fh:write("# page  nonblank  in19_20_21   rows       cols       sets\n")
    local tot_nb, tot_hit = 0, 0
    for page = 0, 9 do
        local base = 0x400000 + page * 0x1000
        local nb, hit = 0, 0
        local r0, r1, c0, c1 = 99, -1, 99, -1
        local seen = {}
        for row = 0, 31 do
            for col = 0, 63 do
                local w = md:read_u16(base + (row * 64 + col) * 2)
                if w ~= 0 then
                    nb = nb + 1
                    local cs = (w >> 6) & 0x7F
                    if SETS[cs] then
                        hit = hit + 1
                        seen[cs] = (seen[cs] or 0) + 1
                        if row < r0 then r0 = row end
                        if row > r1 then r1 = row end
                        if col < c0 then c0 = col end
                        if col > c1 then c1 = col end
                    end
                end
            end
        end
        tot_nb, tot_hit = tot_nb + nb, tot_hit + hit
        local sl = {}
        for cs, n in pairs(seen) do sl[#sl+1] = string.format("%d:%d", cs, n) end
        table.sort(sl)
        fh:write(string.format("  %2d   %6d   %8d   %2d-%-2d      %2d-%-2d     %s\n",
            page, nb, hit, r0, r1, c0, c1, table.concat(sl, " ")))
    end
    fh:write(string.format("\nTOTAL nonblank %d   in 19/20/21 %d  (%.1f%%)\n",
        tot_nb, tot_hit, tot_nb > 0 and tot_hit * 100.0 / tot_nb or 0))
    fh:write(string.format("full plane for reference = 32 rows x 64 cols = 2048 cells/page\n"))
    fh:close()
    print(string.format("chevcells: %d nonblank, %d in sets 19/20/21", tot_nb, tot_hit))
    manager.machine:exit()
end)
