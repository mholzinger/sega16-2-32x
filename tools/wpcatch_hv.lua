-- WATCHPOINT CATCHER + V COLUMN (LOOP 24 K2FREE census). Same shell as
-- wpcatch.lua, but every hit also logs the VDP HV counter, so a
-- writer's CONTEXT is measurable: vint-handler writes cluster at
-- V=E0..F8 (the game's IRQ runs right after vblank), main-loop writes
-- spread across the whole frame. The question this exists to answer:
-- which game FB-staged writers (sprite list, text glyphs, layer regs,
-- rowscroll) are VINT-CONTEXT — those lose their data at the source
-- when FM covers the vint (the k2-spin-kill world), and must move
-- back to WRAM + the DREQ push. Decode: tools/wpcatch_hv.py.
-- Env: WPC_ARM / WPC_DISARM (frames), WPC_RANGE ("addr,len"),
--      WPC_TYPE ("r"/"w"). Requires -debug -debugger none -log;
-- hits land in error.log as "WPHV pc=... a=... hv=...".
local arm = tonumber(os.getenv('WPC_ARM') or '1400')
local disarm = tonumber(os.getenv('WPC_DISARM') or '1800')
local range = os.getenv('WPC_RANGE') or '85f000,1000'
local wtype = os.getenv('WPC_TYPE') or 'w'
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
    if frames > 1100 then
        local p = frames % 240
        set_input('P1 Right', (p < 120) and 1 or 0)
        set_input('P1 A', (p % 40 < 8) and 1 or 0)
    end
    if frames == arm then
        manager.machine.debugger:command(
            'wpset ' .. range .. ',' .. wtype ..
            ',1,{logerror "WPHV pc=%08X a=%08X hv=%04X\\n",pc,wpaddr,w@C00008;g}')
    end
    if frames == disarm then
        manager.machine.debugger:command('wpclear')
        manager.machine:exit()
    end
end)
