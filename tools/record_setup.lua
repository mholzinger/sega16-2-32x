-- RECORD an arcade playthrough for the port's oracle (2026-09-25).
-- Interactive MAME (window + keyboard ON), with MAME's own input recorder:
--
--   mame altbeast -rompath ./mame -skip_gameinfo \
--        -cfg_directory cfg/oracle -nowriteconfig \
--        -input_directory ./inp -record ab_full \
--        -autoboot_script tools/record_setup.lua
--
-- THE DIPS COME FROM cfg/oracle, NOT FROM THIS SCRIPT. MAME's DIP defaults are
-- exactly the bytes md_main.c:7472 assumes (DSW1 0xFF = 1 coin/1 credit,
-- DSW2 0xFD = 3 lives, normal, demo sounds on); cfg/oracle/altbeast.cfg is
-- cfg/altbeast.cfg with its DIP overrides removed and the key bindings kept,
-- because the committed cfg sets 0x66/0x54 instead and the recorded game logic
-- must be the one our rom runs. Forcing them from lua was tried and does NOT
-- work: field:set_value snaps to the settinglist and produced 0xEE/0x01.
--
-- This script now only prints the frame count every 600 frames, so the notes
-- can name the moments worth aligning on.
-- Play through: coin, start, level 1, the transformation, the boss, the round
-- clear, the ball, into level 2. Then quit MAME (Esc). The .inp lands in
-- MAME's inp/ directory (or -inppath). Replay headless with tools/inp_dump.lua.
local frames = 0
emu.register_frame_done(function()
    frames = frames + 1
    if frames % 600 == 0 then print(string.format('record_setup: frame %d', frames)) end
end)
