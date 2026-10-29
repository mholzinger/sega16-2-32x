-- REPLAY a recorded arcade playthrough headlessly and dump the ORACLE (2026-09-25).
--
-- Record first (interactive, see tools/record_setup.lua), then:
--
--   ID_OUT=/path/out mame altbeast -rompath ./mame -skip_gameinfo \
--        -cfg_directory cfg/oracle -nowriteconfig \
--        -input_directory ./inp -playback ab_full \
--        -video none -sound none -nothrottle -window -resolution 160x120 \
--        -keyboardprovider none -nomouse -nojoystick -bench <secs> \
--        -snapshot_directory /path/out -autoboot_script tools/inp_dump.lua
--
-- Env: ID_OUT (dir, required), ID_FRAMES (stop, default 20000),
--      ID_EVERY (snapshot period in frames, default 30).
--
-- Writes into ID_OUT:
--   state.csv      one row a frame: frame, gf (the GAME frame, see below), the
--                  game's scene/round/solo/flag/display bytes, and the layer
--                  registers from text RAM (page words + scroll)
--   inputs_gf.csv  the playthrough's inputs re-keyed to gf, in the ares-headless
--                  vocabulary: `gameframe,button,value`. Feed it to our rom with
--                    ares-headless --input inputs_gf.csv --input-gameframe 0xFFA1D0
--                  which keys playback to the shim's own game-frame counter, so a
--                  rom running the game at 50-70% of 60 Hz still takes the arcade's
--                  route at the SAME game frames.
--   MAME snapshots every ID_EVERY frames AND on every scene edge (a change in the
--   page words or the round byte) -- the arcade's picture at every moment ours has
--   to match.
--
-- gf = frames whose END finds the game's frame flag (0xFFF01C) clear, i.e. the
-- game consumed that frame's release. md_main.c shim_vblank counts the same event
-- at vint entry into 0xFFA1D0, so the two counters name the same game frames.
--
-- Inputs are read as a CHANGE FROM IDLE on the raw port value (polarity-agnostic):
-- the reference is sampled on frame 1, where nothing is pressed.
local out = assert(os.getenv('ID_OUT'), 'ID_OUT must name an output directory')
local max_frames = tonumber(os.getenv('ID_FRAMES') or '20000')
local every = tonumber(os.getenv('ID_EVERY') or '30')


local mem = manager.machine.devices[':maincpu'].spaces['program']

-- the fields we log, in the ares-headless button vocabulary
local WANT = {
    {'y',     {'Coin 1'}},
    {'start', {'1 Player Start', 'Start 1', 'P1 Start'}},
    {'up',    {'P1 Up'}},    {'down',  {'P1 Down'}},
    {'left',  {'P1 Left'}},  {'right', {'P1 Right'}},
    {'a',     {'P1 Button 1'}},
    {'b',     {'P1 Button 2'}},
    {'c',     {'P1 Button 3'}},
}
local tracked = {}          -- {name, port, mask}
for _, w in ipairs(WANT) do
    local name, names = w[1], w[2]
    for _, port in pairs(manager.machine.ioport.ports) do
        local found = nil
        for fname, field in pairs(port.fields) do
            for _, want in ipairs(names) do if fname == want then found = field end end
        end
        if found then tracked[#tracked + 1] = {name = name, port = port, mask = found.mask}; break end
    end
end

local csv = assert(io.open(out .. '/state.csv', 'w'))
csv:write('frame,gf,timer,step,round,solo,flag,disp,page0,page1,fgx,fgy,bgx,bgy\n')
local icsv = assert(io.open(out .. '/inputs_gf.csv', 'w'))
icsv:write('# arcade playthrough inputs, keyed to the GAME frame (see tools/inp_dump.lua)\n')
icsv:write('# ares-headless --input this --input-gameframe 0xFFA1D0\n')
-- snaps.csv names every snapshot MAME writes, in order, with the frame and GAME
-- frame it shows -- without it the oracle images cannot be matched to ours
local scsv = assert(io.open(out .. '/snaps.csv', 'w'))
scsv:write('index,frame,gf,round,page0,edge\n')
local snap_i = 0

local frames, gf, last_key = 0, 0, nil
local idle, state, last_gf = {}, {}, {}
local T = 0x410000          -- arcade text RAM; TEXT_C[i] is word i here

emu.register_frame_done(function()
    frames = frames + 1
    local flag = mem:read_u8(0xFFF01C)
    if flag == 0 then gf = gf + 1 end

    -- inputs: change from the idle reference
    for _, t in ipairs(tracked) do
        local raw = t.port:read() & t.mask
        if frames == 1 then
            idle[t.name] = raw; state[t.name] = 0
        else
            local v = (raw ~= idle[t.name]) and 1 or 0
            if v ~= state[t.name] then
                -- MINIMUM ONE-GAME-FRAME HOLD. The counter stalls while the game
                -- is mid-pass, so a real 10-frame arcade press can span ZERO game
                -- frames (the start button at a scene change did, and the press and
                -- its release then cancelled out on our rom -- 2026-09-25). Keep the
                -- keys strictly increasing per button so every press is seen.
                local k = gf
                if last_gf[t.name] and k <= last_gf[t.name] then k = last_gf[t.name] + 1 end
                icsv:write(string.format('%d,%s,%d\n', k, t.name, v))
                last_gf[t.name] = k
                state[t.name] = v
            end
        end
    end

    -- the LAYER page words are 0x740 (FG) and 0x741 (BG) -- m_main.c
    -- latch_layer_regs reads TEXT_C[0x740 + which]. 0x742/0x743 are the
    -- ALTERNATE page sets; logging 0x742 as "page1" (2026-09-25, first cut)
    -- made our BG word look like it disagreed with the arcade's when it did not.
    local p0 = mem:read_u16(T + 0x740 * 2)
    local p1 = mem:read_u16(T + 0x741 * 2)
    csv:write(string.format('%d,%d,%d,%d,%d,%d,%d,%d,%04X,%04X,%04X,%04X,%04X,%04X\n',
        frames, gf,
        mem:read_u16(0xFFF02A), mem:read_u8(0xFFF031), mem:read_u8(0xFFF142),
        mem:read_u8(0xFFF148), flag, mem:read_u8(0xFFF026),
        p0, p1,
        mem:read_u16(T + 0x74C * 2), mem:read_u16(T + 0x748 * 2),
        mem:read_u16(T + 0x74E * 2), mem:read_u16(T + 0x74A * 2)))

    local key = string.format('%04X%04X%d', p0, p1, mem:read_u8(0xFFF142))
    local edge = (key ~= last_key) and 1 or 0
    if frames % every == 0 or edge == 1 then
        manager.machine.video:snapshot()
        scsv:write(string.format('%d,%d,%d,%d,%04X,%d\n', snap_i, frames, gf, mem:read_u8(0xFFF142), p0, edge))
        snap_i = snap_i + 1
    end
    last_key = key

    if frames >= max_frames then csv:close(); icsv:close(); scsv:close(); manager.machine:exit() end
end)
