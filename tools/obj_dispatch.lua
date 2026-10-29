-- NAME THE OBJECT HANDLERS BY OBSERVATION (2026-09-27).
--
-- The behavioural core -- 35 functions, 16,208 bytes, 26% of the code -- has
-- almost no static callers, because the object loop reaches it through
-- jsr (a0). Static analysis cannot name those; only watching can.
--
-- THE DISPATCH SITE IS 0x039AC, not the addresses in game_altbeast.py's
-- DISPATCHERS (0x03CEA, 0x03D20). Measured: of the eight largest handlers,
-- seven were observed being called and EVERY call came from 0x039AC, while
-- calls from within 0x40 of the two listed sites reached only three small
-- helpers (0x03C84/0x03CA6/0x03CD4).
--
-- At each dispatch this logs the handler (a0), the object record (a6) and the
-- first eight words of that record. Layout read off the capture:
--   +0 status/flags (0x88xx/0x89xx)   +2 the handler pointer, as a long
--   +6 type         +8 slot/index     +10 sprite attribute
-- A6 walks the object table in 0x80 steps -- the 128-byte stride PORT-CENSUS
-- flags as structurally cache-hostile, visible here in the raw addresses.
--
--   OD_DIR=/tmp/od OD_LEN=2 OD_WINDOWS=1000,5000,... OD_SITE=39ac \
--   mame altbeast -rompath ./mame -skip_gameinfo \
--     -cfg_directory cfg/oracle -nowriteconfig \
--     -input_directory ./inp -playback ab_full \
--     -video none -sound none -nothrottle -window -resolution 160x120 \
--     -keyboardprovider none -nomouse -nojoystick -bench <secs> \
--     -debug -debugger none -autoboot_script tools/obj_dispatch.lua
--
-- A trace must be open for tracelog to have a destination; that is the only
-- reason one is started here. Analyse with tools/obj_handlers.py.
local DIR  = os.getenv('OD_DIR') or '/tmp/od'
local LEN  = tonumber(os.getenv('OD_LEN') or '2')
local SITE = os.getenv('OD_SITE') or '39ac'
local wins, order = {}, {}
for s in string.gmatch(os.getenv('OD_WINDOWS') or '', '[^,]+') do
    local f = tonumber(s)
    if f then wins[f] = true; order[#order + 1] = f end
end
table.sort(order)
local last = order[#order] or 0
local frames, opened, done = 0, nil, 0
-- DISPATCHERS[0] 0x39A8: movea.l (2,A6),A0 ; jsr (A0)  -- handler in A0, record A6
local ACT = '{tracelog "OBJ %08X %08X %04X %04X %04X %04X %04X %04X %04X %04X\\n",' ..
            'a0,a6,w@(a6),w@(a6+2),w@(a6+4),w@(a6+6),w@(a6+8),w@(a6+10),w@(a6+12),w@(a6+14); g}'
-- DISPATCHERS[1] 0xD842: movea.l (8,A0),A1 ; jsr (A1)  -- handler in A1, record A0.
-- A DIFFERENT SHAPE, so it gets its own action rather than being forced into
-- the first one's registers. Tagged OBJ2 so the analyser can keep them apart.
local ACT2 = '{tracelog "OBJ2 %08X %08X %04X %04X %04X %04X %04X %04X %04X %04X\\n",' ..
             'a1,a0,w@(a0),w@(a0+2),w@(a0+4),w@(a0+6),w@(a0+8),w@(a0+10),w@(a0+12),w@(a0+14); g}'
local SITE2 = os.getenv('OD_SITE2')
print(string.format('obj-dispatch: site 0x%s, %d windows x %d frames', SITE, #order, LEN))
emu.register_frame_done(function()
    frames = frames + 1
    local dbg = manager.machine.debugger
    if wins[frames] and not opened then
        dbg:command('trace ' .. string.format('%s/o%06d.log', DIR, frames) .. ',maincpu')
        dbg:command('bpset ' .. SITE .. ',1,' .. ACT)
        if SITE2 and SITE2 ~= '' then dbg:command('bpset ' .. SITE2 .. ',1,' .. ACT2) end
        opened = frames
    elseif opened and frames >= opened + LEN then
        dbg:command('bpclear')
        dbg:command('trace off,maincpu')
        done = done + 1
        opened = nil
        if frames > last then print('OBJ-DISPATCH DONE'); manager.machine:exit() end
    end
end)
