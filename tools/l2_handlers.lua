-- RESUME-ADDRESS CENSUS: every object-slot handler word the game ever holds.
--
-- WHY (FULL-PORT.md 12): an object slot does not hold a function pointer, it holds a
-- CONTINUATION -- a resume address. Measured at one gameplay frame, two of the four live
-- handlers were mid-function (0x049BA is 330 B into FUN_00004870; 0x1A70E is inside no
-- function Ghidra disassembled). A decompiled C function has one entry point, so the port
-- needs the full set of resume addresses before it can express object_dispatcher at all.
--
-- The set is finite (code addresses in a 256 KB program) and observable. This samples it.
--
-- Slot geometry is READ OUT OF object_dispatcher (0x0398E), not guessed:
--
--   pcVar1 = &DAT_ffffc000;
--   do { if (*pcVar1 < '\0') { ... (**(code **)(pcVar1 + 2))(); }
--        pcVar1 = pcVar1 + 0x80; } while (DAT_fffff109 < 0x40);
--
-- base 0xFFC000, stride 0x80, 64 SLOTS (so the pool ENDS at 0xFFDFFF), active =
-- byte 0 bit 7, handler = a 32-bit code pointer at +2 called indirectly.
--
-- THE BOUND MATTERS. A first run scanned to 0xFFFFFF and picked up 0xFFF300 --
-- the game-variable region, not the pool -- whose +2 word "handler" 0x1A70E
-- disassembles as a data table (0007 0001 7FFF 4900 4A00 4B00 ...), not code. One
-- false resume address was enough to put a wrong claim in FULL-PORT.md 12.
--
--   L2H_OUT=/tmp/handlers.txt L2H_EVERY=1 \
--   mame altbeast -rompath ./mame -skip_gameinfo \
--     -cfg_directory cfg/oracle -nowriteconfig \
--     -input_directory ./inp -playback ab_full \
--     -video none -sound none -nothrottle -window -resolution 160x120 \
--     -keyboardprovider none -nomouse -nojoystick -bench <secs> \
--     -autoboot_script tools/l2_handlers.lua
--
-- SIZE -bench ABOVE the playback length or it truncates silently.
local OUT   = os.getenv('L2H_OUT') or '/tmp/handlers.txt'
local LO    = tonumber(os.getenv('L2H_LO') or '0xFFC000')
local HI    = tonumber(os.getenv('L2H_HI') or '0xFFDFFF')   -- 64 slots x 0x80
local STRIDE= tonumber(os.getenv('L2H_STRIDE') or '0x80')
local EVERY = tonumber(os.getenv('L2H_EVERY') or '1')

local mem = manager.machine.devices[':maincpu'].spaces['program']
local seen, nseen, frames = {}, 0, 0
local dirty = false
local dump                 -- forward: the frame hook flushes through it,
                           -- and a Lua local declared later is a DIFFERENT name

print(string.format('l2-handlers: slots 0x%06X-0x%06X stride 0x%X, every %d frame(s) -> %s',
                    LO, HI, STRIDE, EVERY, OUT))

emu.register_frame_done(function()
    frames = frames + 1
    if frames % EVERY ~= 0 then return end
    for a = LO, HI - 5, STRIDE do
        -- active flag in the slot's first word; case 8 of the reset routine sets
        -- _DAT_ffffc000 = 0x8000 alongside the handler at +2
        if mem:read_u16(a) & 0x8000 ~= 0 then
            local h = mem:read_u32(a + 2)
            -- a plausible 68K code address in the 256 KB program, even
            if h >= 0x400 and h < 0x40000 and h % 2 == 0 and not seen[h] then
                seen[h] = frames
                nseen = nseen + 1
                dirty = true
            end
        end
    end
    -- flush periodically: -bench can end the run, and a census that only exists
    -- at exit is a census that can be lost
    if dirty and frames % 600 == 0 then dump(); dirty = false end
end)

-- emu.register_stop DOES NOT EXIST in this MAME (measured 2026-09-28: a fatal
-- script error at load, which aborts the WHOLE script -- the frame_done hook
-- registered above never ran and the log said "Total playback frames: 1", which
-- looks like a playback failure and is not one). The notifier API is
-- emu.add_machine_stop_notifier. Verify a hook name against
--   for k,v in pairs(emu) do if type(v)=='function' then print(k) end end
-- before using it.
dump = function()
    local ks = {}
    for h in pairs(seen) do ks[#ks + 1] = h end
    table.sort(ks)
    local f = io.open(OUT, 'w')
    f:write(string.format('# %d distinct handler/resume addresses over %d frames\n', nseen, frames))
    f:write('# address first_seen_frame\n')
    for _, h in ipairs(ks) do f:write(string.format('0x%05X %d\n', h, seen[h])) end
    f:close()
    print(string.format('l2-handlers: %d distinct addresses over %d frames -> %s',
                        nseen, frames, OUT))
end
emu.add_machine_stop_notifier(dump)
