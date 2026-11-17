-- AUTO-MDBEAST (2026-11-02): drive the MEGA DRIVE Altered Beast through level 1 in MAME's
-- genesis driver (~18x realtime) while logging the game's sound mailbox, so the 1988 port's
-- own "when music plays" is measured, not guessed (Mike: the MD release is the source of truth
-- for when music plays back). The pattern is tools/auto_beast.lua's (hold right, attack on a
-- cadence); the Mega Drive game needs Start twice, the second inside the title's ~300-frame
-- window, and a Start during the demo restarts the boot.
--
--   mame genesis -cart "srcref/genesis/Altered Beast (USA, Europe).md" -skip_gameinfo \
--       -video none -sound none -nothrottle -window -resolution 160x120 -keyboardprovider none \
--       -nomouse -nojoystick -bench 400 -autoboot_script tools/auto_mdbeast.lua
--   (add -sound auto -wavwrite OUT.wav for the audio; -sound none mutes the chips)
--
-- Env: MB_PERIOD (attack cadence frames, default 12), MB_OUT (log csv: frame, FE20, FE1A,
-- level 0xFFB174, lives/score candidates), MB_SNAPDIR, MB_SNAPS (comma frames), MB_FRAMES,
-- MB_JUMP (1 = kick with B held, a jumping kick: the MD's B is jump).
local period = tonumber(os.getenv('MB_PERIOD') or '12')
local out_path = os.getenv('MB_OUT') or 'build/gate/mdab/auto.csv'
local snapdir = os.getenv('MB_SNAPDIR') or 'build/gate/mdab/auto'
local max_frames = tonumber(os.getenv('MB_FRAMES') or '24000')
local jump = (os.getenv('MB_JUMP') or '0') == '1'
-- MB_DUMPS: comma frames at which the whole 68K work RAM is dumped (MB_SNAPDIR/wram_<frame>.bin):
-- diffing dumps across a death finds the lives and health bytes to freeze (MB_FREEZE "addr=val,...")
local dumps = {}
for s in string.gmatch(os.getenv('MB_DUMPS') or '', '[^,]+') do dumps[tonumber(s)] = true end
local freezes = {}
for s in string.gmatch(os.getenv('MB_FREEZE') or '', '[^,]+') do
    local a, v = s:match('(%w+)=(%w+)')
    if a then freezes[#freezes + 1] = {tonumber(a), tonumber(v)} end
end
local freeze_from = tonumber(os.getenv('MB_FREEZE_FROM') or '2300')
local snaps = {}
for s in string.gmatch(os.getenv('MB_SNAPS') or '2200,4000,6000,8000,10000,12000,14000,16000,18000,20000,22000', '[^,]+') do
    snaps[tonumber(s)] = true
end
local out = assert(io.open(out_path, 'w'))
out:write('frame,fe20,fe1a,level,b140,px,py\n')
local function find_field(names)
    for _, port in pairs(manager.machine.ioport.ports) do
        for fname, field in pairs(port.fields) do
            for _, want in ipairs(names) do
                if fname == want then return field end
            end
        end
    end
end
local start = find_field({'P1 Start'})
local right = find_field({'P1 Right'})
local btna  = find_field({'P1 A'})
local btnb  = find_field({'P1 B'})
local btnc  = find_field({'P1 C'})
local mem = manager.machine.devices[':maincpu'].spaces['program']
local screen
for _, s in pairs(manager.machine.screens) do screen = s break end
local frames = 0
local last = ''
local function press(field, on) if field then field:set_value(on and 1 or 0) end end
local function tick()
    frames = frames + 1
    local f = frames
    -- the two Starts: one in the demo (goes to the boot sequence), one inside the title window
    press(start, (f >= 1500 and f < 1506) or (f >= 1850 and f < 1856))
    if f >= 2300 then
        press(right, true)
        local k = f % period
        press(btna, k < 4)                                  -- punch
        press(btnc, k >= period / 2 and k < period / 2 + 4) -- kick
        if jump then press(btnb, (f % (period * 3)) < 6) end
    end
    local fe20 = mem:read_u8(0xFFFE20)
    local fe1a = mem:read_u16(0xFFFE1A)
    local level = mem:read_u8(0xFFB174)
    local b140 = mem:read_u8(0xFFB140)
    local px = mem:read_u16(0xFFB218)       -- camera x (grew 220->544 over 1600 frames of walking, build/gate/mdab/dump)
    local py = mem:read_u8(0xFFB120)        -- the 3-segment health (3,3,2,1 across two hits)
    local row = string.format('%02X,%04X,%d,%02X', fe20, fe1a, level, b140)
    if row ~= last then out:write(string.format('%d,%s,%d,%d\n', f, row, px, py)); last = row end
    if f % 600 == 0 then out:write(string.format('%d,%s,%d,%d\n', f, row, px, py)) end   -- a position sample every 10 s
    if snaps[f] then screen:snapshot(string.format('f%05d.png', f)) end   -- lands in -snapshot_directory
    -- freezes apply only once the game runs (MB_FREEZE_FROM, default 2300): the respawn timer
    -- frozen from boot held the attract at the title and the game never started
    if f >= freeze_from then for _, fz in ipairs(freezes) do mem:write_u8(fz[1], fz[2]) end end
    if dumps[f] then
        local fh = assert(io.open(string.format('%s/wram_%05d.bin', snapdir, f), 'wb'))
        local parts = {}
        for a = 0xFF0000, 0xFFFFFF, 1 do parts[#parts + 1] = string.char(mem:read_u8(a)) end
        fh:write(table.concat(parts)); fh:close()
    end
    if f >= max_frames then out:close(); manager.machine:exit() end
end
emu.register_frame_done(tick)
