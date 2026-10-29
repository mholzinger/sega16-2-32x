-- L1 DIFFERENTIAL HARNESS, ORACLE SIDE (2026-09-27).
--
-- Captures the reference vectors a translated 68K routine is verified against:
-- for every call executed inside the window, the FULL 68K register file at
-- entry and at return, and the exact WRAM the call touched.
--
-- WHY THIS SHAPE. Reading a decompilation to decide whether it is right does
-- not scale to 623 functions. A vector does: run the translated routine from
-- the recorded entry state and require the recorded exit state and the recorded
-- writes. Three mechanisms, all inside MAME's own debugger, all proven here:
--   R lines   a trace ACTION logs d0-d7/a0-a6/sp/sr beside every instruction
--             (MAME names A7 'SP'; writing a7 silently parses as the HEX
--              CONSTANT 0xA7 and the call detection then finds nothing)
--   W lines   a write watchpoint over WRAM logs every (address, data, WIDTH
--             in bits -- a seeded byte and word are otherwise indistinguishable)
--   D lines   a READ watchpoint does the same for inputs -- required, see the
--             wpset comment below
--   PC lines  the instruction trace itself, which gives the call structure
-- A lua-driven breakpoint loop was tried first and SEGFAULTED MAME (rc 139);
-- the debugger's own action strings are the supported path.
--
--   L1_DIR=/tmp/l1 L1_LEN=2 L1_WINDOWS=1500,5500,9000 \
--   mame altbeast -rompath ./mame -skip_gameinfo \
--     -cfg_directory cfg/oracle -nowriteconfig \
--     -input_directory ./inp -playback ab_full \
--     -video none -sound none -nothrottle -window -resolution 160x120 \
--     -keyboardprovider none -nomouse -nojoystick -bench <secs> \
--     -debug -debugger none -autoboot_script tools/l1_oracle.lua
--
-- SIZE -bench ABOVE THE LAST WINDOW or it truncates silently. Extract with
-- tools/l1_vectors.py. ~150 B an instruction, so keep L1_LEN small.
local DIR = os.getenv('L1_DIR') or '/tmp/l1'
local LEN = tonumber(os.getenv('L1_LEN') or '2')
local wins, order = {}, {}
for s in string.gmatch(os.getenv('L1_WINDOWS') or '', '[^,]+') do
    local f = tonumber(s)
    if f then wins[f] = true; order[#order + 1] = f end
end
table.sort(order)
local last = order[#order] or 0
local frames, opened, done = 0, nil, 0
local REGS = '{tracelog "R %08X %08X %08X %08X %08X %08X %08X %08X ' ..
             '%08X %08X %08X %08X %08X %08X %08X %08X %04X\\n",' ..
             'd0,d1,d2,d3,d4,d5,d6,d7,a0,a1,a2,a3,a4,a5,a6,sp,sr}'
print(string.format('l1-oracle: %d windows, %d frames each, last at %d',
                    #order, LEN, last))
emu.register_frame_done(function()
    frames = frames + 1
    local dbg = manager.machine.debugger
    if wins[frames] and not opened then
        dbg:command('trace ' .. string.format('%s/v%06d.log', DIR, frames) ..
                    ',maincpu,,' .. REGS)
        -- every WRAM write, address and data, inside the same log
        dbg:command('wpset ff0000,10000,w,1,{tracelog "W %08X %08X %d\\n",wpaddr,wpdata,wpsize; g}')
        -- AND EVERY READ. Without this a vector cannot be replayed: most of
        -- this game's functions take NO register inputs at all -- they read
        -- absolute work RAM (DAT_fffff028 and friends), so the entry state
        -- that matters is MEMORY, not registers. Reads give the input set
        -- exactly, the same way writes give the output set, and instruction
        -- fetch cannot pollute them because code lives in ROM, not WRAM.
        dbg:command('wpset ff0000,10000,r,1,{tracelog "D %08X %08X %d\\n",wpaddr,wpdata,wpsize; g}')
        opened = frames
    elseif opened and frames >= opened + LEN then
        dbg:command('wpclear')
        dbg:command('trace off,maincpu')
        done = done + 1
        print(string.format('  window %d done (%d/%d)', opened, done, #order))
        opened = nil
        if frames > last then print('L1-ORACLE DONE'); manager.machine:exit() end
    end
end)
