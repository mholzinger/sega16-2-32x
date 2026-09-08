-- BLITSKIP skip-rate meter. `make BLITSKIP=1 SKIPCOUNT=1`, then:
--   mame 32x -cart rom/s16.32x -rompath ./mame -skip_gameinfo \
--       -video none -sound none -nothrottle -autoboot_script tools/blitskip_probe.lua
-- Counters (SDRAM 0x0603A680, master's cached alias reads fine here):
--   [0]/[1] slave groups/skipped, [2]/[3] master groups/skipped.
-- MAME cannot price the saving (it charges the ~2.7us instruction issue,
-- not the ~47us/row FB stall) — this is a RATE, not a speed verdict.
-- Drives coin+start into REAL GAMEPLAY (same input pattern as
-- play_32x.lua). Attract mode alone is a soft test: far less sprite
-- churn, so far fewer groups flipping between empty and occupied,
-- which is exactly the transition the mask has to get right.
local frames = 0
local sh2 = nil
local fields, inited = {}, false
local function init_fields()
    for _, port in pairs(manager.machine.ioport.ports) do
        for fname, field in pairs(port.fields) do fields[fname] = field end
    end
    inited = true
end
local function set_input(name, val)
    local f = fields[name]
    if f then f:set_value(val) end
end
emu.register_frame_done(function()
    if not inited then init_fields() end
    frames = frames + 1
    if frames == 600 or frames == 800 then set_input('P1 Start', 1); set_input('P1 A', 1) end
    if frames == 640 or frames == 840 then set_input('P1 Start', 0); set_input('P1 A', 0) end
    if frames == 1000 then set_input('P1 Start', 1) end
    if frames == 1040 then set_input('P1 Start', 0) end
    if frames == 1800 then set_input('P1 Right', 1) end
    if frames == 2400 then set_input('P1 A', 1) end
    if frames == 2410 then set_input('P1 A', 0) end
    if frames == 3000 then set_input('P1 Right', 0) end
    if frames == 3600 then set_input('P1 Left', 1) end
    if frames == 4200 then set_input('P1 Left', 0); set_input('P1 A', 1) end
    if frames == 4210 then set_input('P1 A', 0) end
    if not sh2 then
        for _, c in pairs(manager.machine.devices) do
            if c.tag == ':sh2master' or c.tag == ':32x:sh2master' then
                sh2 = c.spaces['program']
            end
        end
        if not sh2 then
            for tag, c in pairs(manager.machine.devices) do
                if tag:find('sh2') and c.spaces['program'] then
                    sh2 = c.spaces['program']; break
                end
            end
        end
        if not sh2 then return end
    end
    if frames % 300 ~= 0 then return end
    local b = 0x0603A680
    local sg, ss = sh2:read_u32(b), sh2:read_u32(b + 4)
    local mg, ms = sh2:read_u32(b + 8), sh2:read_u32(b + 12)
    local lie = sh2:read_u32(b + 16)   -- BLIT_SKIP_VERIFY: mask lied
    local tg, ts = sg + mg, ss + ms
    print(string.format(
        'f=%d slave %d/%d (%.1f%%) master %d/%d (%.1f%%) TOTAL %d/%d (%.1f%%)',
        frames, ss, sg, sg > 0 and 100.0 * ss / sg or 0,
        ms, mg, mg > 0 and 100.0 * ms / mg or 0,
        ts, tg, tg > 0 and 100.0 * ts / tg or 0)
        .. string.format('  MASK-LIED=%d', lie))
    if frames >= 5400 then manager.machine:exit() end
end)
