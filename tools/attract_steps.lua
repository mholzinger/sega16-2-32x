-- THE ARCADE'S ATTRACT TIMELINE, as the oracle for "attract never
-- advances past step 0x08" (Mike's open bldS item). Reads the same
-- addresses tools/arcade_pagesel.lua does: step 0xFFF031, round
-- 0xFFF142, progress 0xFFF14E. Every transition, with its frame.
local out = os.getenv("AS_OUT") or "/tmp/attract_steps.txt"
local last = tonumber(os.getenv("AS_FRAMES") or "5400")
local md = manager.machine.devices[":maincpu"].spaces["program"]
local f, prev = 0, nil
local fh = assert(io.open(out, "w"))
fh:write("# frame  step  round  progress\n")
emu.register_frame_done(function()
    f = f + 1
    local s = md:read_u8(0xFFF031)
    local r = md:read_u8(0xFFF142)
    local p = md:read_u8(0xFFF14E)
    local k = string.format("%02X/%02X/%02X", s, r, p)
    if k ~= prev then
        fh:write(string.format("%6d   0x%02X   0x%02X    0x%02X\n", f, s, r, p))
        prev = k
    end
    if f >= last then fh:close(); manager.machine:exit() end
end)
