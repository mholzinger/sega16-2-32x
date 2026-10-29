-- REPLAY A TAPE IN MAME ARCADE ALTBEAST AND DUMP ITS WORK RAM AT THE LISTED FRAMES.
--
-- MAME takes ONE -autoboot_script, and the gameplay oracle needs both halves at once: the
-- tape driving the inputs (tools/replay_csv.lua) and the work-RAM dump (tools/wram_dump.lua).
-- This is those two, sharing one frame hook, with replay_csv's DIP forcing kept verbatim --
-- without it the arcade and the port run different machines (md_main.c:3039, DSW2 0xFD,
-- DSW1 0xFF).
--
--   GD_IN=tools/inputs/level1_boss.csv GD_FRAMES=1200,1216,... GD_OUTDIR=/dir \
--   GD_ADDR=0xFFC000 GD_LEN=0x4000 mame altbeast ... -autoboot_script tools/gameplay_dump.lua
--
-- Writes <GD_OUTDIR>/aw_<frame>.bin per frame and prints GAMEPLAY-DUMP DONE for each.
--
-- RECORDERS (2026-10-22), all optional, all off unless set; tests/gameplay/arcade_oracle.sh
-- turns every one of them on. They measure the arcade's own timeline along the tape so the
-- port can be compared against it row by row:
--   GD_GOD=1             while 0xFFF028 bit0 (P1 in play) is set and 0xFFE018 (P1 lives)
--                        reads below 3, write 3. Never touched outside play. The write
--                        comes AFTER the frame's timeline row and ev dumps (since
--                        2026-10-22; before that it came first and no arcade death could
--                        ever show in a row), so a death reads as one row below 3; the log
--                        line GAMEPLAY-DUMP GOD after-row at frame 50 marks a corpus made
--                        this way.
--   GD_DSW1=66 GD_DSW2=54 the DIP bytes forced on the arcade (c42003 / c42001), hex. The
--                        defaults are what the CURRENT port boots with: PORT_CORO seeds the
--                        arcade's own 0x66/0x54 (sh_src/port_l2.c, l2_io_cell), which are
--                        also MAME's factory defaults. ff/fd was the old 68K-era shim's
--                        choice (md_main.c:7711) and a corpus made with it is a different
--                        game (coinage, lives, difficulty-gated spawns). The latched copy at
--                        0xFFF01A/1B in every wram dump is what parity_compare checks.
--   GD_TIMELINE=<csv>    one row per emulated frame, the TIMELINE CSV FORMAT shared with the
--                        port runner (header below). Flushed every 600 frames.
--   GD_CENSUS=<csv>      frame,slot,status,handler for every active object slot 1-63, every
--                        GD_CENSUS_EVERY frames (default 60).
--   GD_SNAP_EVERY=N      screen snapshot every N frames to GD_OUTDIR/snap_<frame>.png.
--   GD_ONCHANGE=1        dump work RAM 0xFFC000:0x4000, tilemap 0x400000:0x10000, text
--                        0x410000:0x1000, sprite 0x440000:0x800 and palette 0x840000:0x1000
--                        as GD_OUTDIR/ev_<frame>_<what>.bin, plus the screen as
--                        GD_OUTDIR/ev_<frame>.png, whenever 0xFFF142 (level),
--                        0xFFF14E (round), 0xFFF14B (round-clear release), 0xFFF028 (P1
--                        state) or slot 40's status word (0xFFD400, the boss) changes, and
--                        at every GD_FRAMES frame. GD_ONCHANGE_MAX (default 2000) caps the
--                        event count so a status word that flickers cannot fill the disk.
--   GD_END=<frame>       exit at this frame even when it is past the last GD_FRAMES entry.
--   GD_ROUND=N           PROBE: credited play starts at round N (2 = level 3): while P1 is in
--                        play (0xFFF028 bit0) with 0xFFF026 still 0 and the round index
--                        0xFFF14E 0, write 0xFFF14E = 0xFFF142 = N. That is the window between
--                        main_entry's clear (0x5DA) and its one-vint wait (0x640); 0x670 then
--                        derives the level from the index as the real advance (0xBCA) does.
--                        The port's twin is the Makefile probe PORTROUND=N.
--   GD_ENDCLEAR=1        PROBE: while P1 is in play and a scroll lock is up (0xFFF152 != 0)
--                        with 0xFFF154 != 0, zero every live status word in slots 16-39 and
--                        0xFFF154/0xFFF15D, so the lock releases and the end-of-level boss
--                        transforms (0x16C88 needs both). Twin: Makefile PORTENDCLEAR=1.
--
-- gameframe is check_demo.game_frame() (tests/demo-b/check_demo.py:118) ported to lua: the
-- blink timer at 0xFFF020/21 counts periods of alternately 30 and 60 frames, so the game
-- frame is the sum of the periods already elapsed plus the position inside the current one.
local in_path = os.getenv('GD_IN') or 'tools/inputs/level1_boss.csv'
local A0 = tonumber(os.getenv('GD_ADDR') or '0xFFC000')
local LEN = tonumber(os.getenv('GD_LEN') or '0x4000')
local outdir = os.getenv('GD_OUTDIR') or '/tmp'

local function find_field(names)
    for _, port in pairs(manager.machine.ioport.ports) do
        for fname, field in pairs(port.fields) do
            for _, want in ipairs(names) do
                if fname == want then return field end
            end
        end
    end
end

local btn = {
    y     = find_field({'Coin 1'}),
    start = find_field({'1 Player Start', 'Start 1'}),
    right = find_field({'P1 Right'}), left = find_field({'P1 Left'}),
    up    = find_field({'P1 Up'}),    down = find_field({'P1 Down'}),
    a     = find_field({'P1 Button 1'}),
    b     = find_field({'P1 Button 2'}),
    c     = find_field({'P1 Button 3'}),
}

-- the port's DIPs, so the two machines are the same machine.
--
-- MEASURED 2026-10-22 (scratch dipprobe3.lua, fresh -cfg_directory, four modes in parallel):
-- field:set_value() on a DIP field is a PRESSED TOGGLE, not a setting. With this block the
-- 68K reads DSW1 0x33 / DSW2 0x54 at 0xC42003/0xC42001 (port:read() agrees); with no block
-- or with field.user_value it reads 0xFF / 0xFD. MAME also rewrites cfg/altbeast.cfg (a
-- TRACKED file) at every exit with the toggled values, so each run in the repo cwd started
-- from the previous run's DIPs: the first coin paid 3 credits in one run and 5 in the next.
-- The block is kept as it was, OFF unless GD_DIP_SETVALUE=1; the user_value block below is
-- what forces the DIPs, and every run prints GAMEPLAY-DUMP DIPS with the 68K-visible bytes.
-- tests/gameplay/arcade_oracle.sh passes a fresh -cfg_directory so no run can drift.
if os.getenv('GD_DIP_SETVALUE') == '1' then
for ptag, want in pairs({[':DSW1'] = 0xFF, [':DSW2'] = 0xFD,
                         [':DSWA'] = 0xFF, [':DSWB'] = 0xFD}) do
    local port = manager.machine.ioport.ports[ptag]
    if port then
        for _, field in pairs(port.fields) do
            local m = field.mask
            local shift = 0
            while (m & 1) == 0 do m = m >> 1; shift = shift + 1 end
            field:set_value((want & field.mask) >> shift)
        end
    end
end
end

local DSW1 = tonumber(os.getenv('GD_DSW1') or '66', 16)
local DSW2 = tonumber(os.getenv('GD_DSW2') or '54', 16)
local DIPS = {[':DSW1'] = DSW1, [':DSW2'] = DSW2, [':DSWA'] = DSW1, [':DSWB'] = DSW2}
for ptag, want in pairs(DIPS) do
    local port = manager.machine.ioport.ports[ptag]
    if port then
        for _, field in pairs(port.fields) do
            field.user_value = want & field.mask
        end
    end
end

local tape = {}
local f = io.open(in_path, 'r')
if not f then print('GAMEPLAY-DUMP no tape ' .. in_path); manager.machine:exit() end
for line in f:lines() do
    local fr, name, val = string.match(line, '^(%d+),(%a+),(%d+)')
    if fr and btn[name] then
        fr = tonumber(fr)
        tape[fr] = tape[fr] or {}
        table.insert(tape[fr], { btn[name], tonumber(val) })
    end
end
f:close()
print('GAMEPLAY-DUMP TAPE ' .. in_path)

local want, last = {}, 0
for s in string.gmatch(os.getenv('GD_FRAMES') or '', '%d+') do
    local n = tonumber(s)
    want[n] = outdir .. '/aw_' .. s .. '.bin'
    if n > last then last = n end
end
local gd_end = tonumber(os.getenv('GD_END') or '0')
if gd_end > last then last = gd_end end

-- the recorders
local GOD = os.getenv('GD_GOD') == '1'
local ROUND = tonumber(os.getenv('GD_ROUND') or '0')   -- see the GD_ROUND note at the top
local ENDCLEAR = os.getenv('GD_ENDCLEAR') == '1'   -- see the GD_ENDCLEAR note at the top
local tl_path = os.getenv('GD_TIMELINE')
local cs_path = os.getenv('GD_CENSUS')
local CENSUS_EVERY = tonumber(os.getenv('GD_CENSUS_EVERY') or '60')
local SNAP_EVERY = tonumber(os.getenv('GD_SNAP_EVERY') or '0')
local ONCHANGE = os.getenv('GD_ONCHANGE') == '1'
local ONCHANGE_MAX = tonumber(os.getenv('GD_ONCHANGE_MAX') or '2000')

local TL_HEADER = 'frame,tick,gameframe,level,round,scroll,px,py,pstat,phand,lives,score,' ..
                  'credits,f028,f14b,f154,pad,boss_stat,boss_hand,step,demo'
local tl, cs
if tl_path then tl = io.open(tl_path, 'w'); tl:write(TL_HEADER .. '\n') end
if cs_path then cs = io.open(cs_path, 'w'); cs:write('frame,slot,status,handler\n') end

-- check_demo.game_frame(b): n = b[0x3020]; sum(60 if i odd else 30 for i in range(n))
--                           + ((60 if n odd else 30) - 1 - b[0x3021])
local function game_frame(sp)
    local n = sp:read_u8(0xFFF020)
    local sum = 0
    for i = 0, n - 1 do
        if (i & 1) ~= 0 then sum = sum + 60 else sum = sum + 30 end
    end
    local period = ((n & 1) ~= 0) and 60 or 30
    return sum + (period - 1 - sp:read_u8(0xFFF021))
end

local function dump_range(sp, path, a0, len)
    local fh = io.open(path, 'wb')
    local buf = {}
    for i = 0, len - 1 do
        buf[#buf + 1] = string.char(sp:read_u8(a0 + i))
        if #buf >= 4096 then fh:write(table.concat(buf)); buf = {} end
    end
    if #buf > 0 then fh:write(table.concat(buf)) end
    fh:close()
end

local EV_RANGES = {
    { 'wram',    0xFFC000, 0x4000 },
    { 'tilemap', 0x400000, 0x10000 },
    { 'text',    0x410000, 0x1000 },
    { 'sprite',  0x440000, 0x800 },
    { 'palette', 0x840000, 0x1000 },
}
local ev_count = 0
local function dump_event(sp, frame, why)
    if ev_count >= ONCHANGE_MAX then return end
    ev_count = ev_count + 1
    for _, r in ipairs(EV_RANGES) do
        dump_range(sp, string.format('%s/ev_%d_%s.bin', outdir, frame, r[1]), r[2], r[3])
    end
    manager.machine.screens[':screen']:snapshot(string.format('%s/ev_%d.png', outdir, frame))
    print(string.format('GAMEPLAY-DUMP EVENT %d %s', frame, why))
end

-- the watched words for GD_ONCHANGE; nil until the first frame sets the baseline
local prev = nil
local function watched(sp)
    return {
        level = sp:read_u8(0xFFF142),
        round = sp:read_u8(0xFFF14E),
        f14b  = sp:read_u8(0xFFF14B),
        f028  = sp:read_u8(0xFFF028),
        boss  = sp:read_u16(0xFFD400),
    }
end

local frames = 0
emu.register_frame_done(function()
    frames = frames + 1
    local evs = tape[frames]
    if evs then for _, e in ipairs(evs) do e[1]:set_value(e[2]) end end
    local sp = manager.machine.devices[':maincpu'].spaces['program']

    if frames == 50 then
        print(string.format('GAMEPLAY-DUMP DIPS c42003 %02x c42001 %02x (want %02x %02x)',
                            sp:read_u8(0xC42003), sp:read_u8(0xC42001), DSW1, DSW2))
        if GOD then print('GAMEPLAY-DUMP GOD after-row') end
    end

    if tl then
        tl:write(string.format(
            '%d,%04x,%d,%02x,%02x,%04x,%04x,%04x,%04x,%08x,%02x,%08x,%02x,%02x,%02x,%02x,%02x,%04x,%08x,%02x,%d\n',
            frames,
            sp:read_u16(0xFFF02A), game_frame(sp),
            sp:read_u8(0xFFF142), sp:read_u8(0xFFF14E), sp:read_u16(0xFFF132),
            sp:read_u16(0xFFC00C), sp:read_u16(0xFFC010),
            sp:read_u16(0xFFC000), sp:read_u32(0xFFC002),
            sp:read_u8(0xFFE018), sp:read_u32(0xFFE000), sp:read_u8(0xFFF000),
            sp:read_u8(0xFFF028), sp:read_u8(0xFFF14B), sp:read_u8(0xFFF154),
            sp:read_u8(0xFFE028),
            sp:read_u16(0xFFD400), sp:read_u32(0xFFD402),
            sp:read_u8(0xFFF031), sp:read_u8(0xFFF026) & 1))
        if frames % 600 == 0 then tl:flush() end
    end

    if cs and frames % CENSUS_EVERY == 0 then
        for slot = 1, 63 do
            local a = 0xFFC000 + slot * 0x80
            local st = sp:read_u16(a)
            if (st & 0x8000) ~= 0 then
                cs:write(string.format('%d,%d,%04x,%08x\n', frames, slot, st, sp:read_u32(a + 2)))
            end
        end
        cs:flush()
    end

    if SNAP_EVERY > 0 and frames % SNAP_EVERY == 0 then
        manager.machine.screens[':screen']:snapshot(string.format('%s/snap_%d.png', outdir, frames))
    end

    local out = want[frames]
    if out then
        local fh = io.open(out, 'wb')
        for i = 0, LEN - 1 do fh:write(string.char(sp:read_u8(A0 + i))) end
        fh:close(); print('GAMEPLAY-DUMP DONE ' .. out)
    end

    if ONCHANGE then
        local now = watched(sp)
        if prev == nil then
            prev = now
        else
            local why = nil
            for k, v in pairs(now) do
                if prev[k] ~= v then
                    why = (why and (why .. ' ') or '') .. string.format('%s %x>%x', k, prev[k], v)
                end
            end
            if out and not why then why = 'frame' end
            if why then dump_event(sp, frames, why) end
            prev = now
        end
    end

    -- the lives hold comes LAST so the row and the dumps above show the death it undoes
    if GOD then
        if (sp:read_u8(0xFFF028) & 1) ~= 0 and sp:read_u8(0xFFE018) < 3 then
            sp:write_u8(0xFFE018, 3)
        end
    end

    -- GD_ROUND: start credited play at round N; the same poke as the port's PORTROUND
    if ROUND > 0 and (sp:read_u8(0xFFF028) & 1) ~= 0 and sp:read_u8(0xFFF026) == 0
       and sp:read_u8(0xFFF14E) == 0 then
        sp:write_u8(0xFFF14E, ROUND); sp:write_u8(0xFFF142, ROUND)
        print(string.format('GAMEPLAY-DUMP ROUND %d at frame %d', ROUND, frames))
    end

    -- GD_ENDCLEAR: at a scroll lock clear the enemy band; the same probe as PORTENDCLEAR
    if ENDCLEAR and frames > 60 and (sp:read_u8(0xFFF028) & 1) ~= 0 and sp:read_u8(0xFFF152) ~= 0
       and sp:read_u8(0xFFF154) ~= 0 then
        for slot = 16, 39 do
            local a = 0xFFC000 + slot * 0x80
            if (sp:read_u8(a) & 0x80) ~= 0 then sp:write_u16(a, 0) end
        end
        sp:write_u8(0xFFF154, 0); sp:write_u8(0xFFF15D, 0)
        print(string.format('GAMEPLAY-DUMP ENDCLEAR at frame %d', frames))
    end

    if last > 0 and frames >= last then
        if tl then tl:close() end
        if cs then cs:close() end
        print('GAMEPLAY-DUMP END ' .. frames .. ' events ' .. ev_count)
        manager.machine:exit()
    end
end)
