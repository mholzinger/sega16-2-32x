-- WATCHPOINT CATCHER (unpair burn-down tool). Arms a 68K watchpoint
-- over WPC_RANGE for frames [WPC_ARM, WPC_DISARM), logging every hit
-- (PC + address) via the debugger action and auto-resuming with `g`.
-- Works on both the 32X port and arcade altbeast (input names adapt).
-- Requires: mame -debug -debugger none -log; hits land in error.log as
-- "WPHIT pc=... a=..." lines.
-- Env: WPC_ARM / WPC_DISARM (frames), WPC_RANGE ("addr,len"),
--      WPC_TYPE ("r" or "w", default "r").
local arm = tonumber(os.getenv('WPC_ARM') or '1350')
local disarm = tonumber(os.getenv('WPC_DISARM') or '1750')
local range = os.getenv('WPC_RANGE') or '808,3f7f0'
local wtype = os.getenv('WPC_TYPE') or 'r'
local frames = 0
local fields = {}
local inited = false
local is32x = manager.machine.system.name == '32x'
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
    if is32x then
        if frames == 600 or frames == 800 then set_input('P1 Start', 1); set_input('P1 A', 1) end
        if frames == 640 or frames == 840 then set_input('P1 Start', 0); set_input('P1 A', 0) end
    else
        if frames == 600 or frames == 800 then set_input('Coin 1', 1) end
        if frames == 640 or frames == 840 then set_input('Coin 1', 0) end
    end
    if frames == 1000 then set_input(is32x and 'P1 Start' or '1 Player Start', 1) end
    if frames == 1040 then set_input(is32x and 'P1 Start' or '1 Player Start', 0) end
    if frames == arm then
        manager.machine.debugger:command(
            'wpset ' .. range .. ',' .. wtype ..
            ',1,{logerror "WPHIT pc=%08X a=%08X\\n",pc,wpaddr;g}')
    end
    if frames == disarm then
        manager.machine.debugger:command('wpclear')
        manager.machine:exit()
    end
end)
