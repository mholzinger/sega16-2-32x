-- LOOP 8 diagnostic: how often is the palette dirty word nonzero?
-- A TEXT push that carries a palette region halves its text chunk, so
-- this is the direct measure of what the palette is costing text refresh.
-- Samples 0xFFB9FC once per frame and reports the occupancy per region.
local frames, dirty_frames = 0, 0
local per = {}
for i = 0, 15 do per[i] = 0 end
local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local last = tonumber(os.getenv("PAL_FRAMES") or "2000")
local out = os.getenv("PAL_OUT") or "/tmp/pal_rate.txt"

emu.register_frame_done(function()
    frames = frames + 1
    local d = mem:read_u16(0xFFB9FC)
    if d ~= 0 then
        dirty_frames = dirty_frames + 1
        for i = 0, 15 do
            if (d & (1 << i)) ~= 0 then per[i] = per[i] + 1 end
        end
    end
    if frames >= last then
        local f = assert(io.open(out, "w"))
        f:write(string.format("frames=%d dirty=%d (%.1f%%)\n",
                              frames, dirty_frames, 100 * dirty_frames / frames))
        for i = 0, 15 do
            f:write(string.format("  region %2d: %6d (%.1f%%)\n",
                                  i, per[i], 100 * per[i] / frames))
        end
        f:close()
        manager.machine:exit()
    end
end)
