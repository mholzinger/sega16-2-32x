-- WHICH ORDER DOES THE ARCADE CALL THEM IN, inside ONE frame (2026-10-03).
--
-- The port and the arcade enter free-run frame 109 with slot 0 BYTE-IDENTICAL
-- (+0x21 = 0x03, +0x22 = 0x01) and leave it differently: the arcade advances the
-- animation, the port takes FUN_000053A8's despawn at 0x53E6. Both see
-- `A6[0x21]==3 && A6[0x22]==1`, so the branch is not the question -- the ORDER is.
-- FULL-PORT.md 61.
--
-- A PC TRACE, NOT A LUA TAP. Lua taps under-report on S16B (LESSONS, "MAME lua taps
-- lie on S16B": a mapper remap kills them and MCU/Z80 taps missed 17 latch events),
-- so they are good for bulk counts and nothing else. MAME's own `trace` action is
-- the supported path and is what tools/l1_oracle.lua already uses.
--
-- The whole instruction trace for one frame is ~5,000 lines, which is small enough to
-- keep and grep rather than filtering inside the emulator -- and an unfiltered trace
-- cannot be accused of having filtered out the answer.
--
--   L1O_DIR=/tmp/l1o L1O_FRAMES=9108,9109 \
--   mame altbeast -rompath ./mame -skip_gameinfo \
--     -cfg_directory cfg/oracle -nowriteconfig \
--     -input_directory ./inp -playback ab_full \
--     -video none -sound none -nothrottle -window -resolution 160x120 \
--     -keyboardprovider none -nomouse -nojoystick -bench 200 \
--     -debug -debugger none -autoboot_script tools/l1_order.lua
--
-- SIZE -bench ABOVE THE LAST FRAME or the run truncates silently: frames are 1/60 s,
-- so 9109 frames needs about 152 emulated seconds.
local DIR = os.getenv('L1O_DIR') or '/tmp/l1o'
local want, order = {}, {}
for s in string.gmatch(os.getenv('L1O_FRAMES') or '', '[^,]+') do
    local f = tonumber(s)
    if f then want[f] = true; order[#order + 1] = f end
end
table.sort(order)
local last = order[#order] or 0
local frames, open_at = 0, nil
print(string.format('l1-order: %d frame(s), last at %d -> %s', #order, last, DIR))
emu.register_frame_done(function()
    frames = frames + 1
    local dbg = manager.machine.debugger
    if open_at then
        dbg:command('wpclear')
        dbg:command('trace off,maincpu')
        print(string.format('  frame %d traced', open_at))
        open_at = nil
        if frames > last then print('L1-ORDER DONE'); manager.machine:exit() end
    end
    if want[frames] then
        -- L1O_REGS=1 adds the full 68K register file beside every instruction. Off by
        -- default because it is ~5x the bytes; on when the question is "what was in
        -- A5 here", which is exactly the question the port's dispatcher cannot answer
        -- (it establishes A6 and nothing else -- FULL-PORT.md 62).
        -- MAME NAMES A7 'sp': writing a7 parses as the HEX CONSTANT 0xA7 and the
        -- register silently reads as a literal (tools/l1_oracle.lua found that first).
        local act = ''
        if os.getenv('L1O_REGS') then
            act = ',{tracelog " ; a5=%08X a6=%08X d0=%08X d5=%08X d6=%08X d7=%08X\n",'
                  .. 'a5,a6,d0,d5,d6,d7}'
        end
        dbg:command('trace ' .. string.format('%s/f%06d.log', DIR, frames) ..
                    ',maincpu,,' .. string.sub(act, 2))
        -- L1O_WP=ADDR:LEN logs every WRITE to that work-RAM span into the same log,
        -- as `W <addr> <data> <bits>`. This is the only way to answer "who wrote
        -- this cell" when reading the code has pointed at a routine that turns out
        -- to be unreachable -- which is exactly what happened chasing slot 16's
        -- +0x4B: the one site that writes all three missed fields is the
        -- player-versus-player collision check, dead in a one-player scene.
        local wp = os.getenv('L1O_WP')
        if wp then
            local a, n = string.match(wp, '([^:]+):(.+)')
            dbg:command(string.format(
                'wpset %s,%s,w,1,{tracelog "W %%08X %%08X %%d\n",wpaddr,wpdata,wpsize; g}',
                a, n))
        end
        open_at = frames
    end
end)
