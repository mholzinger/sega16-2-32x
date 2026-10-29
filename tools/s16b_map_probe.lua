-- S16B MEMORY-MAP PROBE (kit tool, Golden Axe thread rung 1, 2026-09-12).
-- Answers "what 68K memory map does this title run with?" from a RUNNING
-- arcade machine instead of from a table found in a ROM:
--   1. taps the i8751's external data bus (MAME space 'xdata', 8-bit) and
--      records every 315-5195 register write; regs 0x10-0x1F are the eight
--      (size, base) region pairs. Golden Axe's MCU programs a table that is
--      NOT the one at MCU ROM 0xFEA, so a byte search gives the wrong map.
--   2. counts 68K writes per 64 KB page (with writer PCs) over the run.
--      NOTE: cpu.state PC inside a write tap is the NEXT instruction, so every
--      PC printed is "writer + one instruction" (measured: 0x2F94 movep -> 0x2F96).
--      THE TAP MUST BE REINSTALLED EVERY FRAME: update_mapping()
--      (315_5195.cpp) unmaps 0x000000-0xFFFFFF on every region write,
--      which silently drops a tap installed once at script start
--      (measured: 0 writes with a one-shot tap, 110,300 reinstalled).
--   3. dumps the text-RAM control words (page select / scroll, segaic16
--      offsets 0xE80.. and 0xE90..) at MP_DUMP frames, so a discriminator
--      scene can be pinned with its register values.
-- Env: MP_END (frames, default 1200), MP_DUMP (comma list of frames),
--      MP_TEXT (text-RAM base for the dump, default 0x110000).
--   mame goldnaxe -rompath ./mame <headless flags from CLAUDE.md> -bench 30 \
--        -autoboot_script tools/s16b_map_probe.lua
local last = tonumber(os.getenv('MP_END') or '1200')
local text = tonumber(os.getenv('MP_TEXT') or '0x110000')
local dump = {}
for f in (os.getenv('MP_DUMP') or '720,1200'):gmatch('%d+') do dump[tonumber(f)] = true end
local frames, mw, shown, regs = 0, 0, 0, {}
local msp
for tag, d in pairs(manager.machine.devices) do
    if tag:find('mcu') and d.spaces['xdata'] then msp = d.spaces['xdata'] end
end
if msp then
    msp:install_write_tap(0x00, 0xff, 'mp_mcu', function(offset, data, mask)
        mw = mw + 1; local r = offset & 0x1f
        if r >= 0x10 and (regs[r] ~= data) then
            regs[r] = data
            if shown < 48 then shown = shown + 1; print(string.format('f%-4d MCU -> mapper reg %02x = %02x', frames, r, data)) end
        end
    end)
else print('no MCU xdata space: mapper table not captured (FD1094/no-MCU set?)') end
local cpu = manager.machine.devices[':maincpu']; local sp = cpu.spaces['program']
local n, bypage, bypc, tap = 0, {}, {}, nil
local function hook(offset, data, mask)
    n = n + 1
    local pg = offset >> 16; bypage[pg] = (bypage[pg] or 0) + 1
    local k = string.format('%02x:%06x', pg, cpu.state['PC'].value); bypc[k] = (bypc[k] or 0) + 1
end
emu.register_frame_done(function()
    frames = frames + 1
    if tap then tap:remove() end
    tap = sp:install_write_tap(0x000000, 0xFEFFFF, 'mp_68k', hook)   -- everything but the work-RAM page
    if dump[frames] then
        local a, b = {}, {}
        for i = 0, 7 do a[#a+1] = string.format('%04x', sp:read_u16(text + 0xE80 + 2*i)) end
        for i = 0, 7 do b[#b+1] = string.format('%04x', sp:read_u16(text + 0xE90 + 2*i)) end
        print(string.format('f%-4d textram+E80: %s   +E90: %s', frames, table.concat(a, ' '), table.concat(b, ' ')))
    end
    if frames ~= last then return end
    local t = {} for i = 0x10, 0x1f do t[#t+1] = string.format('%02x', regs[i] or 0xff) end
    print('mapper regs 10-1f (MCU, last value): ' .. table.concat(t, ' ') .. string.format('   %d MCU reg writes in %d frames', mw, frames))
    print(string.format('--- %d 68K writes outside the work-RAM page over %d frames, by 64 KB page:', n, frames))
    local pages = {} for pg in pairs(bypage) do pages[#pages+1] = pg end table.sort(pages)
    for _, pg in ipairs(pages) do
        local w = {} for k, c in pairs(bypc) do if tonumber(k:sub(1,2), 16) == pg then w[#w+1] = {k:sub(4), c} end end
        table.sort(w, function(x, y) return x[2] > y[2] end)
        local s = {} for i = 1, math.min(6, #w) do s[#s+1] = w[i][1] .. ' x' .. w[i][2] end
        print(string.format('   %02xxxxx  %7d   pcs: %s', pg, bypage[pg], table.concat(s, ', ')))
    end
    manager.machine:exit()
end)
