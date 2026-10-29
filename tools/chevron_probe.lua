-- CHEVRON PROBE. The transform chevron is actor palette records 132-137
-- (a blue ramp), cycled by the table at 0x26CC on the player object's own
-- anim timer, every SECOND frame (LOOP-DECOMPILE 79). The queue at 0x3BEC
-- copies 14 words from rom 0x242A0+idx*28 to palette line $0A.
--
-- This reports, per frame, every actor line (64-127) whose 14 words equal
-- one of those records -- so "did the chevron records arrive, and on which
-- line" is answered directly instead of inferred from a colour census.
--
-- Env: CHV_OUT, CHV_FRAMES, CHV_BASE (0xFF9800 ours / 0x840800 arcade),
--      CHV_ROM (the game rom holding the records).
local out   = os.getenv("CHV_OUT") or "/tmp/chevron.txt"
local last  = tonumber(os.getenv("CHV_FRAMES") or "4000")
local base  = tonumber(os.getenv("CHV_BASE") or "0xFF9800")
local rompath = os.getenv("CHV_ROM") or "roms/altbeast/prog68k.bin"

local rec = {}                                  -- [132..137] = {14 words}
do
    local f = assert(io.open(rompath, "rb"))
    for idx = 132, 137 do
        f:seek("set", 0x242A0 + idx * 28)
        local b, w = f:read(28), {}
        for k = 1, 14 do
            w[k] = string.byte(b, k*2-1) * 256 + string.byte(b, k*2)
        end
        rec[idx] = w
    end
    f:close()
end

local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local frames, hits, seen = 0, {}, {}

emu.register_frame_done(function()
    frames = frames + 1
    for line = 0, 63 do                          -- actor lines 64..127
        local a = base + line * 32 + 2
        for idx = 132, 137 do
            local ok = true
            for k = 1, 14 do
                if mem:read_u16(a + (k-1)*2) ~= rec[idx][k] then ok = false; break end
            end
            if ok then
                hits[#hits+1] = string.format("%6d  line %3d  record %3d",
                                              frames, 64 + line, idx)
                seen[idx] = (seen[idx] or 0) + 1
                break
            end
        end
    end
    if frames >= last then
        local f = assert(io.open(out, "w"))
        f:write(string.format("# chevron probe, %d frames, base %06X\n", frames, base))
        f:write(string.format("# %d frame-line hits\n", #hits))
        for i = 1, math.min(#hits, 400) do f:write(hits[i], "\n") end
        f:write("# per-record frame count\n")
        for idx = 132, 137 do
            f:write(string.format("#   record %3d  %d\n", idx, seen[idx] or 0))
        end
        f:close()
        manager.machine:exit()
    end
end)
