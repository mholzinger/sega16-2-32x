-- S16B ARCADE WRITE CENSUS (kit tool; Golden Axe thread rung 3, TOOLKIT step a).
-- Runs on the ARCADE (the oracle), before any thunk table exists, and
-- answers per hardware region: how many 68K writes per frame, from which
-- PCs, over what address extent. Indirect writers (register-addressed
-- copies and fills) are counted the same as literal ones — that is the
-- point; the static census only sees literals.
-- Regions come from the title's live memory map (tools/s16b_map_probe.lua).
-- Sprite RAM is classified by the 64 KB page named in the sprite-base
-- variable at the moment of the write (Golden Axe's MCU moves it).
-- Tap reinstalled every frame (update_mapping() drops it otherwise).
-- Inputs: coin at WC_COIN, start at WC_START, then a walk-right + attack
-- pattern; the window WC_A..WC_B should be gameplay — verify with the
-- snapshots it writes at WC_A and WC_B.
-- Env: WC_OUT, WC_END (3000), WC_A (1500), WC_B (3000), WC_COIN (600),
--      WC_START (800), WC_STARTF ('1 Player Start'), WC_GAME (goldnaxe).
local out_path = os.getenv('WC_OUT') or '/tmp/s16b_write_census.txt'
local last  = tonumber(os.getenv('WC_END') or '3000')
local wa    = tonumber(os.getenv('WC_A') or '1500')
local wb    = tonumber(os.getenv('WC_B') or '3000')
local fcoin = tonumber(os.getenv('WC_COIN') or '600')
local fstart= tonumber(os.getenv('WC_START') or '800')
local game  = os.getenv('WC_GAME') or 'goldnaxe'
local startf= os.getenv('WC_STARTF') or '1 Player Start'   -- S16B start field name (AB: 'P1 Start')
local MAPS = {
  goldnaxe = {
    regions = { {0x100000,0x10FFFF,'tileram'}, {0x110000,0x110FFF,'textram'}, {0x140000,0x140FFF,'palette'},
                {0x1F0000,0x1FFFFF,'bank_math'}, {0x1E0000,0x1EFFFF,'rgn2'}, {0xC40000,0xC43FFF,'io'} },
    sprite_base_var = 0xFFECC4,   -- long; high word = 64 KB page of sprite RAM this frame
  },
  altbeast = {
    regions = { {0x400000,0x40FFFF,'tileram'}, {0x410000,0x410FFF,'textram'}, {0x840000,0x840FFF,'palette'},
                {0x3F0000,0x3FFFFF,'tilebank'}, {0x440000,0x44FFFF,'spriteram'}, {0xC40000,0xC43FFF,'io'} },
  },
}
local map = MAPS[game]
local cpu = manager.machine.devices[':maincpu']; local sp = cpu.spaces['program']
local frames, tap, scr = 0, nil, nil
local per = {}      -- region -> {total, win, frame, pcs = {pc -> {n, lo, hi}}, lo, hi}
local function reg(name) local r = per[name]; if not r then r = {total=0, win=0, frame=0, pcs={}, lo=0xFFFFFF, hi=0}; per[name] = r end return r end
local function classify(a)
    for _, r in ipairs(map.regions) do if a >= r[1] and a <= r[2] then return r[3] end end
    if map.sprite_base_var and (a >> 16) == sp:read_u16(map.sprite_base_var) then return 'spriteram' end
    return string.format('other_%02x', a >> 16)
end
local function hook(offset, data, mask)
    local name = classify(offset); local r = reg(name); local pc = cpu.state['PC'].value
    r.total = r.total + 1; r.frame = r.frame + 1
    if frames >= wa and frames <= wb then r.win = r.win + 1 end
    if offset < r.lo then r.lo = offset end; if offset > r.hi then r.hi = offset end
    local p = r.pcs[pc]; if not p then p = {n=0, lo=offset, hi=offset}; r.pcs[pc] = p end
    p.n = p.n + 1; if offset < p.lo then p.lo = offset end; if offset > p.hi then p.hi = offset end
end
local fields, inited = {}, false
local function set_input(n, v) local f = fields[n]; if f then f:set_value(v) else print('no input field ' .. n) end end
emu.register_frame_done(function()
    frames = frames + 1
    if not inited then
        for _, port in pairs(manager.machine.ioport.ports) do for fname, field in pairs(port.fields) do fields[fname] = field end end
        inited = true
        for _, s in pairs(manager.machine.screens) do scr = s break end
    end
    if tap then tap:remove() end
    tap = sp:install_write_tap(0x000000, 0xFEFFFF, 'wc', hook)
    if frames == fcoin then set_input('Coin 1', 1) end
    if frames == fcoin + 10 then set_input('Coin 1', 0) end
    if frames == fstart then set_input(startf, 1) end
    if frames == fstart + 20 then set_input(startf, 0) end
    -- Golden Axe: character select follows start; Button 1 at +100 picks the first hero
    if frames == fstart + 100 then set_input('P1 Button 1', 1) end
    if frames == fstart + 110 then set_input('P1 Button 1', 0) end
    if frames == fstart + 300 then set_input('P1 Right', 1) end
    for _, f in ipairs({fstart + 400, fstart + 700, fstart + 1000, fstart + 1300, fstart + 1600, fstart + 1900}) do
        if frames == f then set_input('P1 Button 1', 1) end
        if frames == f + 8 then set_input('P1 Button 1', 0) end
    end
    if frames == wa or frames == wb then scr:snapshot(string.format('wc_f%04d.png', frames)) end
    for _, r in pairs(per) do r.frame = 0 end
    if frames ~= last then return end
    local f = io.open(out_path, 'w')
    f:write(string.format('s16b_write_census %s  frames 1-%d, window %d-%d (%d frames)\n', game, last, wa, wb, wb - wa + 1))
    f:write(string.format('coin f%d start f%d; snapshots wc_f%04d/wc_f%04d.png show what the window was\n\n', fcoin, fstart, wa, wb))
    local names = {} for n in pairs(per) do names[#names+1] = n end table.sort(names)
    f:write(string.format('%-12s %9s %10s %8s  %s\n', 'region', 'total', 'win/frame', 'extent', ''))
    for _, n in ipairs(names) do local r = per[n]
        f:write(string.format('%-12s %9d %10.1f  %06x-%06x\n', n, r.total, r.win / (wb - wa + 1), r.lo, r.hi)) end
    for _, n in ipairs(names) do local r = per[n]
        f:write(string.format('\n== %s: writer PCs (count, extent)\n', n))
        local pcs = {} for pc, p in pairs(r.pcs) do pcs[#pcs+1] = {pc, p} end
        table.sort(pcs, function(x, y) return x[2].n > y[2].n end)
        for _, e in ipairs(pcs) do f:write(string.format('   %06x  %8d  %06x-%06x\n', e[1], e[2].n, e[2].lo, e[2].hi)) end
    end
    f:close(); print('wrote ' .. out_path)
    manager.machine:exit()
end)
