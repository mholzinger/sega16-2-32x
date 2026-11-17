-- arcade_orb_run.lua: drive level 1 so the hero COLLECTS THE SPIRIT BALLS and transforms.
-- tools/arcade_level_run.lua's walk (six coins + START, right held, punch/kick cadence,
-- lives held at 3) never hits the white wolf, so no corpus run ever selects the
-- transformation pages 10/11 (checked 2026-11-05 over all 826 level-1 dumps). This driver
-- reads the object table each frame: when a wolf (handler 0x00EE80, the white one flashes
-- sprite palette 7/8) is within WOLF_RANGE px of the hero it turns to face it and punches
-- (a 2 of 8 frames, b 2 of 8); after a wolf dies it walks toward the nearest new object
-- (the ball) for BALL_FRAMES frames. Everything it presses is written as a tape
-- ($OUT/tape.csv, frame,button,value edges, the make_tape.py names) so the same run
-- replays through tools/gameplay_dump.lua and, baked into a PORTTAPEPAD rom, on the port.
-- $OUT/log.txt: hero x / form (+0x6A of slot 0) / pages (text 0x740/0x741) every 50 frames
-- and at every change. Headless flags as in CLAUDE.md; -bench covers END/60 + 20.
--   OUT=dir END=12000 mame altbeast -rompath ./mame ... -autoboot_script tools/arcade_orb_run.lua
local OUT = os.getenv('OUT') or '/tmp'
local END = tonumber(os.getenv('END') or '12000')
local WOLF_RANGE = tonumber(os.getenv('WOLF_RANGE') or '340')
local BALL_FRAMES = tonumber(os.getenv('BALL_FRAMES') or '240')
local NEAR = tonumber(os.getenv('NEAR') or '48')
local ABOVE = tonumber(os.getenv('ABOVE') or '80')
local JUMPKICK = tonumber(os.getenv('JUMPKICK') or '0')
local SNAP_FROM = tonumber(os.getenv('SNAP_FROM') or '0')
local SNAP_TO = tonumber(os.getenv('SNAP_TO') or '-1')
if SNAP_TO < 0 then SNAP_TO = 1e9 end
local SNAP_EVERY = tonumber(os.getenv('SNAP_EVERY') or '1000000000')
-- THE PORT'S DIPS (66/54, what tools/gameplay_dump.lua forces): without this block MAME reads
-- FF/FD and the tape's run differs from its replay (2026-11-05: the first wolf died under
-- FF/FD at frame 4157 and survived the same inputs under 66/54; the whole tape was void).
local DSW1 = tonumber(os.getenv('GD_DSW1') or '66', 16)
local DSW2 = tonumber(os.getenv('GD_DSW2') or '54', 16)
for ptag, want in pairs({[':DSW1'] = DSW1, [':DSW2'] = DSW2, [':DSWA'] = DSW1, [':DSWB'] = DSW2}) do
    local port = manager.machine.ioport.ports[ptag]
    if port then for _, field in pairs(port.fields) do field.user_value = want & field.mask end end
end
local frames = 0
local fields = {}
local inited = false
local log = io.open(OUT .. '/log.txt', 'w')
local tape = io.open(OUT .. '/tape.csv', 'w')
tape:write('# altbeast level 1 orb tape, tools/arcade_orb_run.lua\n# frame,button,value; y = coin. Edges only.\n')
local NAME = { y = 'Coin 1', start = '1 Player Start', right = 'P1 Right', left = 'P1 Left',
               a = 'P1 Button 1', b = 'P1 Button 2', c = 'P1 Button 3' }
local cur = {}
local function press(n, v)
    local f = fields[NAME[n]]
    if f then f:set_value(v) end
    if cur[n] ~= v then cur[n] = v; tape:write(string.format('%d,%s,%d\n', frames, n, v)) end
end
local wolf_dead_at = -1
local ball_x = nil
local last = { face = 0, face_at = -10 }
emu.register_frame_done(function()
    if not inited then
        for _, p in pairs(manager.machine.ioport.ports) do for n, f in pairs(p.fields) do fields[n] = f end end
        inited = true
        for _, n in ipairs({'y', 'start', 'right', 'left', 'a', 'b', 'c'}) do cur[n] = 0 end
    end
    frames = frames + 1
    local m = manager.machine.devices[':maincpu'].spaces['program']
    for _, c in ipairs({500, 540, 580, 620, 660, 700}) do
        if frames == c then press('y', 1) end
        if frames == c + 12 then press('y', 0) end
    end
    if frames == 901 then press('start', 1) end
    if frames == 941 then press('start', 0) end
    local hx = m:read_u16(0xFFC00C)
    local form = m:read_u8(0xFFC06A)
    local scroll = m:read_u16(0xFFF132)
    if frames > 1500 then
        -- the nearest live wolf, white first
        local wolf, wdx, wolves = nil, 9999, 0
        wolf_a = nil
        for s = 1, 63 do
            local a = 0xFFC000 + s * 0x80
            if m:read_u16(a) ~= 0 and m:read_u32(a + 2) == 0x0000ee80 then
                wolves = wolves + 1
                local wx = m:read_u16(a + 0xC)
                local dx = wx - hx
                local pal = m:read_u8(a + 0xA)
                local d = math.abs(dx) - ((pal == 7 or pal == 8) and 32 or 0)
                if d < wdx then wolf = a; wdx = d; wolf_dx = dx; wolf_hp = m:read_u8(a + 0x4A); wolf_white = (pal == 7 or pal == 8); wolf_pal = pal; wolf_a = a end
            end
        end
        if last.wolves and wolves < last.wolves then wolf_dead_at = frames end
        last.wolves = wolves
        local k = frames % 16
        if wolf and math.abs(wolf_dx) <= (wolf_white and WOLF_RANGE or 64) then
            -- walk to it when far; when it is above (the wall top) jump-kick; else face it
            -- (a 2-frame tap whenever it changes side) and punch/kick as it passes
            local want = wolf_dx > 0 and 1 or -1
            local hy = m:read_u16(0xFFC010)
            -- a ground wolf's y reads 64 above the hero's (origins differ); the wall top is 96+
            local above = wolf_a and (hy - m:read_u16(wolf_a + 0x10)) > ABOVE
            if want ~= last.face then last.face = want; last.face_at = frames end
            local tap = frames - last.face_at < 2
            local far = math.abs(wolf_dx) > NEAR
            -- a wolf that has not moved for 20 frames is waiting (a ledge): walk to it; a
            -- moving one is charging: stand and let it come (walking into it = bitten)
            local wx = m:read_u16(wolf_a + 0xC)
            if last.wolf_a == wolf_a and math.abs(wx - (last.wx or 0)) <= 1 then last.still = (last.still or 0) + 1 else last.still = 0 end
            last.wolf_a = wolf_a; last.wx = wx
            local goto_it = far and last.still > 20
            press('right', (want > 0 and (tap or goto_it)) and 1 or 0)
            press('left', (want < 0 and (tap or goto_it)) and 1 or 0)
            if (above or JUMPKICK == 1) and not far then
                local q = frames % 40
                press('c', q < 2 and 1 or 0)
                press('b', (q >= 10 and q < 12) and 1 or 0)
                press('a', (q >= 24 and q < 26) and 1 or 0)
            else
                local q = frames % 6
                press('a', q < 2 and 1 or 0)
                press('b', (q >= 3 and q < 5) and 1 or 0)
                press('c', 0)
            end
        elseif wolf_dead_at > 0 and frames - wolf_dead_at < BALL_FRAMES then
            -- walk right slowly under the ball: right held, no attacks
            press('right', 1); press('left', 0); press('a', 0); press('b', 0); press('c', 0)
        else
            press('c', 0)
            press('left', 0)
            press('right', ((frames // 400) % 4) ~= 3 and 1 or 0)
            press('a', (k < 4) and 1 or 0)
            press('b', (k >= 8 and k < 12) and 1 or 0)
        end
        if m:read_u8(0xFFE018) < 3 then m:write_u8(0xFFE018, 3) end
        if (m:read_u8(0xFFF028) & 0x10) ~= 0 and frames % 60 == 0 then press('start', 1) elseif frames % 60 == 30 then press('start', 0) end
    end
    local fg, bg = m:read_u16(0x410E80), m:read_u16(0x410E82)
    local e002 = m:read_u8(0xFFE002)
    if frames == 50 then log:write(string.format('DIPS c42003 %02x c42001 %02x\n', m:read_u8(0xC42003), m:read_u8(0xC42001))) end
    local changed = (form ~= last.form) or (fg ~= last.fg) or (e002 ~= last.e002)
    local inwin = frames >= SNAP_FROM and frames <= SNAP_TO and wolf_a ~= nil
    if inwin then
        log:write(string.format('%d W hx %04x hy %04x hst %s h2c %02x h2e %02x wolf_dx %s wy %s whp %s wst %s w2c %02x pad r%d l%d a%d b%d\n',
            frames, hx, m:read_u16(0xFFC010), m:read_u32(0xFFC020) and string.format('%08x', m:read_u32(0xFFC020)), m:read_u8(0xFFC02C), m:read_u8(0xFFC02E),
            tostring(wolf_dx), wolf_a and m:read_u16(wolf_a + 0x10) or -1, tostring(wolf_hp), wolf_a and string.format('%08x', m:read_u32(wolf_a + 0x20)) or '-', wolf_a and m:read_u8(wolf_a + 0x2C) or 0,
            cur.right, cur.left, cur.a, cur.b))
        if frames % SNAP_EVERY == 0 then manager.machine.screens[':screen']:snapshot(string.format('%s/w_%d.png', OUT, frames)) end
    end
    if changed or frames % 50 == 0 then
        log:write(string.format('%d hx %04x scroll %04x form %02x e002 %02x pages %04x/%04x wolves %d wolf_dx %s hp %s pal %s dead_at %d\n',
            frames, hx, scroll, form, e002, fg, bg, last.wolves or 0, tostring(wolf_dx), tostring(wolf_hp), tostring(wolf_pal), wolf_dead_at))
        log:flush()
        if changed and frames > 1500 then manager.machine.screens[':screen']:snapshot(string.format('%s/ev_%d.png', OUT, frames)) end
    end
    last.form = form; last.fg = fg; last.e002 = e002
    if frames >= END then log:close(); tape:close(); manager.machine:exit() end
end)
