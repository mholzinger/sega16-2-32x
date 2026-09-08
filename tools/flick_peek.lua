-- FLICKFUSE state peek (our rom in MAME). Logs per frame: tracker 0
-- live/hist, flick_lvl[0..7], and the first 3 SPR_SNAP records' key
-- words. Fixed-block reads at the 0x06 alias (debugger space does not
-- serve 0x26 — LOOP19 trap). Env: FP_OUT.
local out = assert(io.open(os.getenv('FP_OUT') or '/tmp/flick_peek.txt', 'w'))
local frames = 0
local fields = {}
local inited = false
local function init_fields()
    for tag, port in pairs(manager.machine.ioport.ports) do
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
    if frames >= 1080 and frames <= 1400 then
        local sh = manager.machine.devices[':sega32x:32x_master_sh2'].spaces['program']
        local lvl = ''
        for i = 0, 7 do lvl = lvl .. string.format('%d', sh:read_u8(tonumber(os.getenv("FP_LVL") or "0x06005b94") + i)) end
        local trk = ''
        for t = 0, 3 do
            local b = tonumber(os.getenv("FP_TRK") or "0x06005a6c") + t * 16
            trk = trk .. string.format(' t%d[c%02x h%02x l%d]', t,
                sh:read_u16(b + 8) & 0x3F, sh:read_u8(b + 12), sh:read_u8(b + 13))
        end
        local rec = ''
        for i = 0, 2 do
            local b = 0x06028400 + i * 16
            rec = rec .. string.format(' r%d[%04x %04x %04x %04x]', i,
                sh:read_u16(b), sh:read_u16(b + 4), sh:read_u16(b + 8),
                sh:read_u16(b + 10))
        end
        out:write(string.format('%d lvl=%s%s%s\n', frames, lvl, trk, rec))
        out:flush()
    end
    if frames > 1400 then out:close() manager.machine:exit() end
end)
