-- L2 ORACLE: the FULL work-RAM image at consecutive frame boundaries.
--
-- L1 vectors record per-call reads and writes, which is enough to verify one routine and
-- not enough to verify a FRAME: for that you need the whole 15.6 KB state before and after,
-- so the first divergent cell can name the routine. That is the gate in FULL-PORT.md 9.
--
-- A frame boundary here is MAME's own frame_done, which is consistent and needs no
-- assumption about where the game's loop happens to be.
--
--   L2_DIR=/tmp/l2 L2_FRAMES=1500,1501,1502 \
--   mame altbeast -rompath ./mame -skip_gameinfo \
--     -cfg_directory cfg/oracle -nowriteconfig \
--     -input_directory ./inp -playback ab_full \
--     -video none -sound none -nothrottle -window -resolution 160x120 \
--     -keyboardprovider none -nomouse -nojoystick -bench <secs> \
--     -autoboot_script tools/l2_oracle.lua
--
-- SIZE -bench ABOVE THE LAST FRAME or it truncates silently (same trap as l1_oracle).
local DIR  = os.getenv('L2_DIR') or '/tmp/l2'
local LO   = tonumber(os.getenv('L2_LO') or '0xFFC000')
local HI   = tonumber(os.getenv('L2_HI') or '0xFFFFFF')
local want = {}
local n = 0
for s in string.gmatch(os.getenv('L2_FRAMES') or '', '[^,]+') do
    local f = tonumber(s)
    if f then want[f] = true; n = n + 1 end
end

-- THE SYSTEM 16 I/O PORTS, CAPTURED PER FRAME ALONGSIDE WORK RAM.
--
-- A frame's inputs are the work-RAM state PLUS the controller and dip state, and
-- only the first was ever captured. The port's MMIO stub returned a constant, so
-- every input-derived cell was wrong by construction -- measured 2026-10-02 as 9 of
-- the 19 cells the rotated real frame gets wrong, written by FUN_00001366:
--     DAT_ffffe028 = ~DAT_00c41003;      -- player 1
--     DAT_ffffe0a8 = ~DAT_00c41007;      -- player 2
-- Worse, the gate PAID the hand-rolled four-call body for not reading the
-- controller at all (LESSONS, "A frame's inputs are the seed PLUS the controller").
--
-- These are the nine addresses the decompiled program actually reads. Order is
-- FIXED and tools/l2_pack.py and sh_src/port_l2.c depend on it.
local IOPORTS = { 0xC40001, 0xC41001, 0xC41003, 0xC41005, 0xC41007,
                  0xC42001, 0xC42003, 0xC43001, 0xC43007 }

-- NEITHER A BUS READ NOR A TAP. BOTH WERE MEASURED AND BOTH ARE WRONG HERE:
--
--   mem:read_u8 on these ports PERTURBS THE MACHINE. A run with the nine reads
--   added differs from a clean run by 12 bytes at 0xFFFF02..0xFFFF95, in EVERY
--   frame -- the System 16 input latches consume an edge when read, so the oracle
--   was answering a question it had changed.
--
--   install_read_tap NEVER FIRES and perturbs anyway: every port came back 0xFF
--   (the tap's default) while the run still differed by the same 12 bytes. The 68K's
--   accesses go through the 315-5195 mapper, so a program-space tap at 0xC40000 does
--   not see them. CLAUDE.md already says this: "MAME lua taps lie on S16B (mapper
--   remaps kill them)".
--
-- manager.machine.ioport reads the INPUT STATE, not the bus. No handler runs, no
-- latch is consumed, and it is exactly what the 68K's read would have returned.
-- The tag -> 68K address map is CONFIRMED, not assumed: the destructive capture read
-- 0x54 at 0xC42001 and 0x33 at 0xC42003, and DSW2/DSW1 read 0x54/0x33 here.
--
-- 0xC40001, 0xC43001 and 0xC43007 are NOT input ports (display/watchdog and the
-- mapper) and have no ioport behind them. They are recorded as 0xFF -- the value the
-- port's own stub fill already holds -- and tools/l2_pack.py marks them uncaptured
-- rather than letting a fabricated byte look like a measurement.
local IOTAG = { [0xC41001] = 'SERVICE', [0xC41003] = 'P1', [0xC41005] = nil,
                [0xC41007] = 'P2', [0xC42001] = 'DSW2', [0xC42003] = 'DSW1' }

local cpu = manager.machine.devices[':maincpu']
local mem = cpu.spaces['program']
local frames, written = 0, 0

print(string.format('l2-oracle: %d frame(s), work RAM 0x%06X-0x%06X (%d B)',
                    n, LO, HI, HI - LO + 1))

emu.register_frame_done(function()
    frames = frames + 1
    if not want[frames] then return end
    local path = string.format('%s/wram_%06d.bin', DIR, frames)
    local f = io.open(path, 'wb')
    if not f then print('l2-oracle: cannot open ' .. path); return end
    -- byte at a time: read_u8 is the only width guaranteed not to alias a
    -- word-only handler, and 16 KB is nothing at this cadence
    local t = {}
    for a = LO, HI do t[#t + 1] = string.char(mem:read_u8(a)) end
    f:write(table.concat(t))
    f:close()
    -- the same frame's I/O, one byte per port in IOPORTS order. Read at frame_done,
    -- which is outside the game's own read window, so it cannot race the 68K.
    local g = io.open(string.format('%s/io_%06d.bin', DIR, frames), 'wb')
    if g then
        local u = {}
        local ports = manager.machine.ioport.ports
        for i = 1, #IOPORTS do
            local tag = IOTAG[IOPORTS[i]]
            local pt  = tag and ports[':' .. tag]
            u[i] = string.char(pt and (pt:read() & 0xFF) or 0xFF)
        end
        g:write(table.concat(u))
        g:close()
    end
    written = written + 1
    print(string.format('l2-oracle: frame %d -> %s', frames, path))
    if written >= n then
        print('l2-oracle: all frames captured')
        manager.machine:exit()
    end
end)
